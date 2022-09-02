#include "Exception.hpp"
#include "Print.hpp"
#include "BoundingBox.hpp"
#include "Brim.hpp"
#include "ClipperUtils.hpp"
#include "Extruder.hpp"
#include "Flow.hpp"
#include "Geometry/ConvexHull.hpp"
#include "I18N.hpp"
#include "ShortestPath.hpp"
#include "SupportMaterial.hpp"
#include "Thread.hpp"
#include "GCode.hpp"
#include "GCode/WipeTower.hpp"
#include "Utils.hpp"
#include "PrintConfig.hpp"
#include "Model.hpp"
#include <float.h>

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <boost/filesystem/path.hpp>
#include <boost/format.hpp>
#include <boost/log/trivial.hpp>

// Mark string for localization and translate.
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

template class PrintState<PrintStep, psCount>;
template class PrintState<PrintObjectStep, posCount>;

PrintRegion::PrintRegion(const PrintRegionConfig &config) : PrintRegion(config, config.hash()) {}
PrintRegion::PrintRegion(PrintRegionConfig &&config) : PrintRegion(std::move(config), config.hash()) {}

//BBS
float Print::min_skirt_length = 0;

void Print::clear()
{
	std::scoped_lock<std::mutex> lock(this->state_mutex());
    // The following call should stop background processing if it is running.
    this->invalidate_all_steps();
	for (PrintObject *object : m_objects)
		delete object;
	m_objects.clear();
    m_print_regions.clear();
    m_model.clear_objects();
}

// Called by Print::apply().
// This method only accepts PrintConfig option keys.
bool Print::invalidate_state_by_config_options(const ConfigOptionResolver & /* new_config */, const std::vector<t_config_option_key> &opt_keys)
{
    if (opt_keys.empty())
        return false;

    // Cache the plenty of parameters, which influence the G-code generator only,
    // or they are only notes not influencing the generated G-code.
    static std::unordered_set<std::string> steps_gcode = {
        //BBS
        "additional_cooling_fan_speed",
        "reduce_crossing_wall",
        "max_travel_detour_distance",
        "printable_area",
        //BBS: add bed_exclude_area
        "bed_exclude_area",
        "before_layer_change_gcode",
        "enable_overhang_bridge_fan"
        "overhang_fan_speed",
        "overhang_fan_threshold",
        "slow_down_for_layer_cooling",
        "default_acceleration",
        "deretraction_speed",
        "close_fan_the_first_x_layers",
        "machine_end_gcode",
        "filament_end_gcode",
        "extruder_clearance_height_to_rod",
        "extruder_clearance_height_to_lid",
        "extruder_clearance_radius",
        "extruder_colour",
        "extruder_offset",
        "filament_flow_ratio",
        "reduce_fan_stop_start_freq",
        "fan_cooling_layer_time",
        "full_fan_speed_layer",
        "filament_colour",
        "filament_diameter",
        "filament_density",
        "filament_cost",
        "initial_layer_acceleration",
        "top_surface_acceleration",
        // BBS
        "cool_plate_temp_initial_layer",
        "eng_plate_temp_initial_layer",
        "hot_plate_temp_initial_layer",
        "textured_plate_temp_initial_layer",
        "gcode_add_line_number",
        "layer_change_gcode",
        "fan_min_speed",
        "fan_max_speed",
        "printable_height",
        "slow_down_min_speed",
#ifdef HAS_PRESSURE_EQUALIZER
        "max_volumetric_extrusion_rate_slope_positive",
        "max_volumetric_extrusion_rate_slope_negative",
#endif /* HAS_PRESSURE_EQUALIZER */
        "reduce_infill_retraction",
        "filename_format",
        "retraction_minimum_travel",
        "retract_before_wipe",
        "retract_when_changing_layer",
        "retraction_length",
        "retract_length_toolchange",
        "z_hop",
        "retract_restart_extra",
        "retract_restart_extra_toolchange",
        "retraction_speed",
        "slow_down_layer_time",
        "standby_temperature_delta",
        "machine_start_gcode",
        "filament_start_gcode",
        "change_filament_gcode",
        "wipe",
        // BBS
        "wipe_distance",
        "curr_bed_type",
        "nozzle_volume",
        "chamber_temperature"
    };

    static std::unordered_set<std::string> steps_ignore;

    std::vector<PrintStep> steps;
    std::vector<PrintObjectStep> osteps;
    bool invalidated = false;

    for (const t_config_option_key &opt_key : opt_keys) {
        if (steps_gcode.find(opt_key) != steps_gcode.end()) {
            // These options only affect G-code export or they are just notes without influence on the generated G-code,
            // so there is nothing to invalidate.
            steps.emplace_back(psGCodeExport);
        } else if (steps_ignore.find(opt_key) != steps_ignore.end()) {
            // These steps have no influence on the G-code whatsoever. Just ignore them.
        } else if (
               opt_key == "skirt_loops"
            || opt_key == "skirt_height"
            || opt_key == "draft_shield"
            || opt_key == "skirt_distance"
            || opt_key == "ooze_prevention"
            || opt_key == "wipe_tower_x"
            || opt_key == "wipe_tower_y"
            || opt_key == "wipe_tower_rotation_angle") {
            steps.emplace_back(psSkirtBrim);
        } else if (
               opt_key == "initial_layer_print_height"
            || opt_key == "nozzle_diameter"
            // Spiral Vase forces different kind of slicing than the normal model:
            // In Spiral Vase mode, holes are closed and only the largest area contour is kept at each layer.
            // Therefore toggling the Spiral Vase on / off requires complete reslicing.
            || opt_key == "spiral_mode") {
            osteps.emplace_back(posSlice);
        } else if (
               opt_key == "print_sequence"
            || opt_key == "filament_type"
            || opt_key == "chamber_temperature"
            || opt_key == "nozzle_temperature_initial_layer"
            || opt_key == "filament_minimal_purge_on_wipe_tower"
            || opt_key == "filament_max_volumetric_speed"
            || opt_key == "gcode_flavor"
            || opt_key == "single_extruder_multi_material"
            || opt_key == "nozzle_temperature"
            // BBS
            || opt_key == "cool_plate_temp"
            || opt_key == "eng_plate_temp"
            || opt_key == "hot_plate_temp"
            || opt_key == "textured_plate_temp"
            || opt_key == "enable_prime_tower"
            || opt_key == "prime_tower_width"
            || opt_key == "prime_tower_brim_width"
            //|| opt_key == "wipe_tower_bridging"
            || opt_key == "wipe_tower_no_sparse_layers"
            || opt_key == "flush_volumes_matrix"
            || opt_key == "prime_volume"
            || opt_key == "flush_into_infill"
            || opt_key == "flush_into_support"
            || opt_key == "initial_layer_infill_speed"
            || opt_key == "travel_speed"
            || opt_key == "travel_speed_z"
            || opt_key == "initial_layer_speed") {
            //|| opt_key == "z_offset") {
            steps.emplace_back(psWipeTower);
            steps.emplace_back(psSkirtBrim);
        } else if (opt_key == "filament_soluble"
                || opt_key == "filament_is_support") {
            steps.emplace_back(psWipeTower);
            // Soluble support interface / non-soluble base interface produces non-soluble interface layers below soluble interface layers.
            // Thus switching between soluble / non-soluble interface layer material may require recalculation of supports.
            //FIXME Killing supports on any change of "filament_soluble" is rough. We should check for each object whether that is necessary.
            osteps.emplace_back(posSupportMaterial);
            osteps.emplace_back(posSimplifySupportPath);
        } else if (
               opt_key == "initial_layer_line_width"
            || opt_key == "min_layer_height"
            || opt_key == "max_layer_height"
            || opt_key == "resolution"
            //BBS: when enable arc fitting, we must re-generate perimeter
            || opt_key == "enable_arc_fitting"
            || opt_key == "wall_infill_order") {
            osteps.emplace_back(posPerimeters);
            osteps.emplace_back(posInfill);
            osteps.emplace_back(posSupportMaterial);
            osteps.emplace_back(posSimplifyPath);
            osteps.emplace_back(posSimplifySupportPath);
            steps.emplace_back(psSkirtBrim);
        } else {
            // for legacy, if we can't handle this option let's invalidate all steps
            //FIXME invalidate all steps of all objects as well?
            invalidated |= this->invalidate_all_steps();
            // Continue with the other opt_keys to possibly invalidate any object specific steps.
        }
    }

    sort_remove_duplicates(steps);
    for (PrintStep step : steps)
        invalidated |= this->invalidate_step(step);
    sort_remove_duplicates(osteps);
    for (PrintObjectStep ostep : osteps)
        for (PrintObject *object : m_objects)
            invalidated |= object->invalidate_step(ostep);

    return invalidated;
}

bool Print::invalidate_step(PrintStep step)
{
	bool invalidated = Inherited::invalidate_step(step);
    // Propagate to dependent steps.
    if (step != psGCodeExport)
        invalidated |= Inherited::invalidate_step(psGCodeExport);
    return invalidated;
}

// returns true if an object step is done on all objects
// and there's at least one object
bool Print::is_step_done(PrintObjectStep step) const
{
    if (m_objects.empty())
        return false;
    std::scoped_lock<std::mutex> lock(this->state_mutex());
    for (const PrintObject *object : m_objects)
        if (! object->is_step_done_unguarded(step))
            return false;
    return true;
}

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::object_extruders() const
{
    std::vector<unsigned int> extruders;
    extruders.reserve(m_print_regions.size() * m_objects.size() * 3);
    // BBS
#if 0
    for (const PrintObject *object : m_objects)
		for (const PrintRegion &region : object->all_regions())
        	region.collect_object_printing_extruders(*this, extruders);
#else
    for (const PrintObject* object : m_objects) {
        const ModelObject* mo = object->model_object();
        for (const ModelVolume* mv : mo->volumes) {
            std::vector<int> volume_extruders = mv->get_extruders();
            for (int extruder : volume_extruders) {
                assert(extruder > 0);
                extruders.push_back(extruder - 1);
            }
        }
    }
#endif
    sort_remove_duplicates(extruders);
    return extruders;
}

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::support_material_extruders() const
{
    std::vector<unsigned int> extruders;
    bool support_uses_current_extruder = false;
    // BBS
    auto num_extruders = (unsigned int)m_config.filament_diameter.size();

    for (PrintObject *object : m_objects) {
        if (object->has_support_material()) {
        	assert(object->config().support_filament >= 0);
            if (object->config().support_filament == 0)
                support_uses_current_extruder = true;
            else {
            	unsigned int i = (unsigned int)object->config().support_filament - 1;
                extruders.emplace_back((i >= num_extruders) ? 0 : i);
            }
        	assert(object->config().support_interface_filament >= 0);
            if (object->config().support_interface_filament == 0)
                support_uses_current_extruder = true;
            else {
            	unsigned int i = (unsigned int)object->config().support_interface_filament - 1;
                extruders.emplace_back((i >= num_extruders) ? 0 : i);
            }
        }
    }

    if (support_uses_current_extruder)
        // Add all object extruders to the support extruders as it is not know which one will be used to print supports.
        append(extruders, this->object_extruders());

    sort_remove_duplicates(extruders);
    return extruders;
}

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::extruders() const
{
    std::vector<unsigned int> extruders = this->object_extruders();
    append(extruders, this->support_material_extruders());
    sort_remove_duplicates(extruders);
    return extruders;
}

unsigned int Print::num_object_instances() const
{
	unsigned int instances = 0;
    for (const PrintObject *print_object : m_objects)
        instances += (unsigned int)print_object->instances().size();
    return instances;
}

double Print::max_allowed_layer_height() const
{
    double nozzle_diameter_max = 0.;
    for (unsigned int extruder_id : this->extruders())
        nozzle_diameter_max = std::max(nozzle_diameter_max, m_config.nozzle_diameter.get_at(extruder_id));
    return nozzle_diameter_max;
}

std::vector<ObjectID> Print::print_object_ids() const
{
    std::vector<ObjectID> out;
    // Reserve one more for the caller to append the ID of the Print itself.
    out.reserve(m_objects.size() + 1);
    for (const PrintObject *print_object : m_objects)
        out.emplace_back(print_object->id());
    return out;
}

bool Print::has_infinite_skirt() const
{
    return (m_config.draft_shield == dsEnabled && m_config.skirt_loops > 0) || (m_config.ooze_prevention && this->extruders().size() > 1);
}

bool Print::has_skirt() const
{
    return (m_config.skirt_height > 0 && m_config.skirt_loops > 0) || m_config.draft_shield != dsDisabled;
}

bool Print::has_brim() const
{
    return std::any_of(m_objects.begin(), m_objects.end(), [](PrintObject *object) { return object->has_brim(); });
}

//BBS
StringObjectException Print::sequential_print_clearance_valid(const Print &print, Polygons *polygons, std::vector<std::pair<Polygon, float>>* height_polygons)
{
    StringObjectException single_object_exception;
    auto print_config = print.config();
    Pointfs excluse_area_points = print_config.bed_exclude_area.values;
    Polygons exclude_polys;
    Polygon exclude_poly;
    const Vec3d print_origin = print.get_plate_origin();
    for (int i = 0; i < excluse_area_points.size(); i++) {
        auto pt = excluse_area_points[i];
        exclude_poly.points.emplace_back(scale_(pt.x() + print_origin.x()), scale_(pt.y() + print_origin.y()));
        if (i % 4 == 3) {  // exclude areas are always rectangle
            exclude_polys.push_back(exclude_poly);
            exclude_poly.points.clear();
        }
    }

    std::map<ObjectID, Polygon> map_model_object_to_convex_hull;
    struct print_instance_info
    {
        const PrintInstance *print_instance;
        BoundingBox    bounding_box;
        Polygon        hull_polygon;
    };
    std::vector<struct print_instance_info> print_instance_with_bounding_box;
    {
        // sequential_print_horizontal_clearance_valid
        Polygons convex_hulls_other;
        if (polygons != nullptr)
            polygons->clear();
        std::vector<size_t> intersecting_idxs;

        for (const PrintObject *print_object : print.objects()) {
            assert(! print_object->model_object()->instances.empty());
            assert(! print_object->instances().empty());
            ObjectID model_object_id = print_object->model_object()->id();
            auto it_convex_hull = map_model_object_to_convex_hull.find(model_object_id);
            // Get convex hull of all printable volumes assigned to this print object.
            ModelInstance *model_instance0 = print_object->model_object()->instances.front();
            if (it_convex_hull == map_model_object_to_convex_hull.end()) {
                // Calculate the convex hull of a printable object.
                // Grow convex hull with the clearance margin.
                // FIXME: Arrangement has different parameters for offsetting (jtMiter, limit 2)
                // which causes that the warning will be showed after arrangement with the
                // appropriate object distance. Even if I set this to jtMiter the warning still shows up.
                it_convex_hull = map_model_object_to_convex_hull.emplace_hint(it_convex_hull, model_object_id,
                            print_object->model_object()->convex_hull_2d(Geometry::assemble_transform(
                            { 0.0, 0.0, model_instance0->get_offset().z() }, model_instance0->get_rotation(), model_instance0->get_scaling_factor(), model_instance0->get_mirror())));
            }
            // Make a copy, so it may be rotated for instances.
            Polygon convex_hull0 = it_convex_hull->second;
            const double z_diff = Geometry::rotation_diff_z(model_instance0->get_rotation(), print_object->instances().front().model_instance->get_rotation());
            if (std::abs(z_diff) > EPSILON)
                convex_hull0.rotate(z_diff);
            // Now we check that no instance of convex_hull intersects any of the previously checked object instances.
            for (const PrintInstance &instance : print_object->instances()) {
                Polygon convex_hull_no_offset = convex_hull0, convex_hull;
                convex_hull = offset(convex_hull_no_offset,
                        // Shrink the extruder_clearance_radius a tiny bit, so that if the object arrangement algorithm placed the objects
                        // exactly by satisfying the extruder_clearance_radius, this test will not trigger collision.
                        float(scale_(0.5 * print.config().extruder_clearance_radius.value - EPSILON)),
                        jtRound, scale_(0.1)).front();
                // instance.shift is a position of a centered object, while model object may not be centered.
                // Convert the shift from the PrintObject's coordinates into ModelObject's coordinates by removing the centering offset.
                convex_hull.translate(instance.shift - print_object->center_offset());
                convex_hull_no_offset.translate(instance.shift - print_object->center_offset());
                //juedge the exclude area
                if (!intersection(exclude_polys, convex_hull_no_offset).empty()) {
                    if (single_object_exception.string.empty()) {
                        single_object_exception.string = (boost::format(L("%1% is too close to exclusion area, there will be collisions when printing.")) %instance.model_instance->get_object()->name).str();
                        single_object_exception.object = instance.model_instance->get_object();
                    }
                    else {
                        single_object_exception.string += (boost::format(L("\n%1% is too close to exclusion area, there will be collisions when printing.")) %instance.model_instance->get_object()->name).str();
                        single_object_exception.object = nullptr;
                    }
                    //if (polygons) {
                    //    intersecting_idxs.emplace_back(convex_hulls_other.size());
                    //}
                }

                // if output needed, collect indices (inside convex_hulls_other) of intersecting hulls
                for (size_t i = 0; i < convex_hulls_other.size(); ++i) {
                    if (! intersection(convex_hulls_other[i], convex_hull).empty()) {
                        if (single_object_exception.string.empty()) {
                            single_object_exception.string = (boost::format(L("%1% is too close to others, and collisions may be caused.")) %instance.model_instance->get_object()->name).str();
                            single_object_exception.object = instance.model_instance->get_object();
                        }
                        else {
                            single_object_exception.string += "\n"+(boost::format(L("%1% is too close to others, and collisions may be caused.")) %instance.model_instance->get_object()->name).str();
                            single_object_exception.object = nullptr;
                        }

                        if (polygons) {
                            intersecting_idxs.emplace_back(i);
                            intersecting_idxs.emplace_back(convex_hulls_other.size());
                        }
                    }
                }
                struct print_instance_info print_info {&instance, convex_hull.bounding_box(), convex_hull};
                print_instance_with_bounding_box.push_back(std::move(print_info));
                convex_hulls_other.emplace_back(std::move(convex_hull));
            }
        }
        if (!intersecting_idxs.empty()) {
            // use collected indices (inside convex_hulls_other) to update output
            std::sort(intersecting_idxs.begin(), intersecting_idxs.end());
            intersecting_idxs.erase(std::unique(intersecting_idxs.begin(), intersecting_idxs.end()), intersecting_idxs.end());
            for (size_t i : intersecting_idxs) {
                polygons->emplace_back(std::move(convex_hulls_other[i]));
            }
        }
    }

    //sort the print instance
    std::sort(print_instance_with_bounding_box.begin(), print_instance_with_bounding_box.end(),
        [](auto &l, auto &r) {
            auto ly1       = l.bounding_box.min.y();
            auto ly2       = l.bounding_box.max.y();
            auto ry1       = r.bounding_box.min.y();
            auto ry2       = r.bounding_box.max.y();
            auto inter_min = std::max(ly1, ry1);
            auto inter_max = std::min(ly2, ry2);
            auto lx        = l.bounding_box.min.x();
            auto rx        = r.bounding_box.min.x();
            if (inter_max - inter_min > 0)
                return (lx < rx) || ((lx == rx) && (ly1 < ry1));
            else
                return (ly1 < ry1);
        });

    // sequential_print_vertical_clearance_valid
    {
        // Ignore the last instance printed.
        //print_instance_with_bounding_box.pop_back();
        /*bool has_interlaced_objects = false;
        for (int k = 0; k < print_instance_count; k++)
        {
            auto inst = print_instance_with_bounding_box[k].print_instance;
            auto bbox = print_instance_with_bounding_box[k].bounding_box;
            auto iy1 = bbox.min.y();
            auto iy2 = bbox.max.y();

            for (int i = 0; i < k; i++)
            {
                auto& p = print_instance_with_bounding_box[i].print_instance;
                auto bbox2 = print_instance_with_bounding_box[i].bounding_box;
                auto py1 = bbox2.min.y();
                auto py2 = bbox2.max.y();
                auto inter_min = std::max(iy1, py1); // min y of intersection
                auto inter_max = std::min(iy2, py2); // max y of intersection. length=max_y-min_y>0 means intersection exists
                if (inter_max - inter_min > 0) {
                    has_interlaced_objects = true;
                    break;
                }
            }
            if (has_interlaced_objects)
                break;
        }*/

        double hc1 = scale_(print.config().extruder_clearance_height_to_lid);
        double hc2 = scale_(print.config().extruder_clearance_height_to_rod);
        double printable_height = scale_(print.config().printable_height);

        // if objects are not overlapped on y-axis, they will not collide even if they are taller than extruder_clearance_height_to_rod
        int print_instance_count = print_instance_with_bounding_box.size();
        std::map<const PrintInstance*, std::pair<Polygon, float>> too_tall_instances;
        for (int k = 0; k < print_instance_count; k++)
        {
            auto inst = print_instance_with_bounding_box[k].print_instance;
            auto bbox = print_instance_with_bounding_box[k].bounding_box;
            auto iy1 = bbox.min.y();
            auto iy2 = bbox.max.y();
            (const_cast<ModelInstance*>(inst->model_instance))->arrange_order = k+1;
            double height = (k == (print_instance_count - 1))?printable_height:hc1;
            /*if (has_interlaced_objects) {
                if ((k < (print_instance_count - 1)) && (inst->print_object->height() > hc2)) {
                    too_tall_instances[inst] = std::make_pair(print_instance_with_bounding_box[k].hull_polygon, unscaled<double>(hc2));
                }
            }
            else {
                if ((k < (print_instance_count - 1)) && (inst->print_object->height() > hc1)) {
                    too_tall_instances[inst] = std::make_pair(print_instance_with_bounding_box[k].hull_polygon, unscaled<double>(hc1));
                }
            }*/

            for (int i = k+1; i < print_instance_count; i++)
            {
                auto& p = print_instance_with_bounding_box[i].print_instance;
                auto bbox2 = print_instance_with_bounding_box[i].bounding_box;
                auto py1 = bbox2.min.y();
                auto py2 = bbox2.max.y();
                auto inter_min = std::max(iy1, py1); // min y of intersection
                auto inter_max = std::min(iy2, py2); // max y of intersection. length=max_y-min_y>0 means intersection exists
                if (inter_max - inter_min > 0) {
                    height = hc2;
                    break;
                }
            }
            if (height < inst->print_object->height())
                too_tall_instances[inst] = std::make_pair(print_instance_with_bounding_box[k].hull_polygon, unscaled<double>(height));
        }

        if (too_tall_instances.size() > 0) {
            //return {, inst->model_instance->get_object()};
            for (auto& iter: too_tall_instances) {
                if (single_object_exception.string.empty()) {
                    single_object_exception.string = (boost::format(L("%1% is too tall, and collisions will be caused.")) %iter.first->model_instance->get_object()->name).str();
                    single_object_exception.object = iter.first->model_instance->get_object();
                }
                else {
                    single_object_exception.string += "\n" + (boost::format(L("%1% is too tall, and collisions will be caused.")) %iter.first->model_instance->get_object()->name).str();
                    single_object_exception.object = nullptr;
                }
                if (height_polygons)
                    height_polygons->emplace_back(std::move(iter.second));
            }
        }
    }

    return single_object_exception;
}

//BBS
static StringObjectException layered_print_cleareance_valid(const Print &print, StringObjectException *warning)
{
    std::vector<const PrintInstance*> print_instances_ordered = sort_object_instances_by_model_order(print, true);
    if (print_instances_ordered.size() < 1)
        return {};

    auto print_config = print.config();
    Pointfs excluse_area_points = print_config.bed_exclude_area.values;
    Polygons exclude_polys;
    Polygon exclude_poly;
    const Vec3d print_origin = print.get_plate_origin();
    for (int i = 0; i < excluse_area_points.size(); i++) {
        auto pt = excluse_area_points[i];
        exclude_poly.points.emplace_back(scale_(pt.x() + print_origin.x()), scale_(pt.y() + print_origin.y()));
        if (i % 4 == 3) {  // exclude areas are always rectangle
            exclude_polys.push_back(exclude_poly);
            exclude_poly.points.clear();
        }
    }

    std::map<const PrintInstance*, Polygon> map_model_object_to_convex_hull;
    // sequential_print_horizontal_clearance_valid
    Polygons convex_hulls_other;
    for (int k = 0; k < print_instances_ordered.size(); k++)
    {
        auto& inst = print_instances_ordered[k];
        auto it_convex_hull = map_model_object_to_convex_hull.find(inst);
        // Get convex hull of all printable volumes assigned to this print object.
        const ModelInstance* model_instance0 = inst->model_instance;
        if (it_convex_hull == map_model_object_to_convex_hull.end()) {
            // Calculate the convex hull of a printable object.
            auto convex_hull0 = inst->print_object->model_object()->convex_hull_2d(
                Geometry::assemble_transform(Vec3d::Zero(), model_instance0->get_rotation(), model_instance0->get_scaling_factor(), model_instance0->get_mirror()));

            double z_diff = Geometry::rotation_diff_z(model_instance0->get_rotation(), inst->model_instance->get_rotation());
            if (std::abs(z_diff) > EPSILON)
                convex_hull0.rotate(z_diff);

            // instance.shift is a position of a centered object, while model object may not be centered.
            // Conver the shift from the PrintObject's coordinates into ModelObject's coordinates by removing the centering offset.
            convex_hull0.translate(inst->shift - inst->print_object->center_offset());

            it_convex_hull = map_model_object_to_convex_hull.emplace_hint(it_convex_hull, inst, convex_hull0);
        }
        Polygon& convex_hull = it_convex_hull->second;
        Polygons convex_hulls_temp;
        convex_hulls_temp.push_back(convex_hull);
        if (!intersection(convex_hulls_other, convex_hulls_temp).empty()) {
            if (warning) {
                warning->string = inst->model_instance->get_object()->name + L(" is too close to others, there will be collisions when printing.\n");
                warning->object = inst->model_instance->get_object();
            }
        }
        if (!intersection(exclude_polys, convex_hull).empty()) {
            return {inst->model_instance->get_object()->name + L(" is too close to exclusion area, there will be collisions when printing.\n"), inst->model_instance->get_object()};
            /*if (warning) {
                warning->string = inst->model_instance->get_object()->name + L(" is too close to exclusion area, there will be collisions when printing.\n");
                warning->object = inst->model_instance->get_object();
            }*/
        }
        convex_hulls_other.emplace_back(convex_hull);
    }

    //BBS: add the wipe tower check logic
    const PrintConfig &       config   = print.config();
    int                 filaments_count = print.extruders().size();
    int                 plate_index = print.get_plate_index();
    const Vec3d         plate_origin = print.get_plate_origin();
    float               x            = config.wipe_tower_x.get_at(plate_index) + plate_origin(0);
    float               y            = config.wipe_tower_y.get_at(plate_index) + plate_origin(1);
    float               width        = config.prime_tower_width.value;
    float               a            = config.wipe_tower_rotation_angle.value;
    //float               v            = config.wiping_volume.value;

    float        depth                     = print.wipe_tower_data(filaments_count).depth;
    //float        brim_width                = print.wipe_tower_data(filaments_count).brim_width;

    Polygon     wipe_tower_convex_hull;
    wipe_tower_convex_hull.points.emplace_back(scale_(x), scale_(y));
    wipe_tower_convex_hull.points.emplace_back(scale_(x + width), scale_(y));
    wipe_tower_convex_hull.points.emplace_back(scale_(x + width), scale_(y + depth));
    wipe_tower_convex_hull.points.emplace_back(scale_(x), scale_(y + depth));
    wipe_tower_convex_hull.rotate(a);

    Polygons convex_hulls_temp;
    convex_hulls_temp.push_back(wipe_tower_convex_hull);
    if (!intersection(convex_hulls_other, convex_hulls_temp).empty()) {
        if (warning) {
            warning->string += L("Prime Tower") + L(" is too close to others, and collisions may be caused.\n");
        }
    }
    if (!intersection(exclude_polys, convex_hulls_temp).empty()) {
        /*if (warning) {
            warning->string += L("Prime Tower is too close to exclusion area, there will be collisions when printing.\n");
        }*/
        return {L("Prime Tower") + L(" is too close to exclusion area, and collisions will be caused.\n")};
    }

    return {};
}

//BBS
static std::map<std::string, bool> filament_is_high_temp {
        {"PLA",     false},
        {"PLA-CF",  false},
        {"PETG",    true},
        {"ABS",     true},
        {"TPU",     false},
        {"PA",      true},
        {"PA-CF",   true},
        {"PET-CF",  true},
        {"PC",      true},
        {"ASA",     true}
};

//BBS: this function is used to check whether multi filament can be printed
StringObjectException Print::check_multi_filament_valid(const Print& print)
{
    bool has_high_temperature_filament = false;
    bool has_low_temperature_filament = false;

    auto print_config = print.config();
    std::vector<unsigned int> extruders = print.extruders();

    for (const auto& extruder_idx : extruders) {
        std::string filament_type = print_config.filament_type.get_at(extruder_idx);
        if (filament_is_high_temp.find(filament_type) != filament_is_high_temp.end()) {
            if (filament_is_high_temp[filament_type])
                has_high_temperature_filament = true;
            else
                has_low_temperature_filament = true;
        }
    }

    if (has_high_temperature_filament && has_low_temperature_filament)
        return { L("Can not print multiple filaments which have large difference of temperature together. Otherwise, the extruder and nozzle may be blocked or damaged during printing") };

    return {std::string()};
}

// Precondition: Print::validate() requires the Print::apply() to be called its invocation.
//BBS: refine seq-print validation logic
StringObjectException Print::validate(StringObjectException *warning, Polygons* collison_polygons, std::vector<std::pair<Polygon, float>>* height_polygons) const
{
    std::vector<unsigned int> extruders = this->extruders();

    if (m_objects.empty())
        return {std::string()};

    if (extruders.empty())
        return { L("No extrusions under current settings.") };

    if (extruders.size() > 1) {
        auto ret = check_multi_filament_valid(*this);
        if (!ret.string.empty())
            return ret;
    }

    if (m_config.print_sequence == PrintSequence::ByObject) {
        //BBS: refine seq-print validation logic
        auto ret = sequential_print_clearance_valid(*this, collison_polygons, height_polygons);
    	if (!ret.string.empty())
            return ret;
    }
    else {
        //BBS
        auto ret = layered_print_cleareance_valid(*this, warning);
        if (!ret.string.empty()) {
            return ret;
        }
    }

    if (m_config.spiral_mode) {
        size_t total_copies_count = 0;
        for (const PrintObject *object : m_objects)
            total_copies_count += object->instances().size();
        // #4043
        if (total_copies_count > 1 && m_config.print_sequence != PrintSequence::ByObject)
            return {L("Please select \"By object\" print sequence to print multiple objects in spiral vase mode."), nullptr, "spiral_mode"};
        assert(m_objects.size() == 1);
        if (m_objects.front()->all_regions().size() > 1)
            return {L("The spiral vase mode does not work when an object contains more than one materials."), nullptr, "spiral_mode"};
    }

    if (this->has_wipe_tower() && ! m_objects.empty()) {
        // Make sure all extruders use same diameter filament and have the same nozzle diameter
        // EPSILON comparison is used for nozzles and 10 % tolerance is used for filaments
        double first_nozzle_diam = m_config.nozzle_diameter.get_at(extruders.front());
        double first_filament_diam = m_config.filament_diameter.get_at(extruders.front());
        for (const auto& extruder_idx : extruders) {
            double nozzle_diam = m_config.nozzle_diameter.get_at(extruder_idx);
            double filament_diam = m_config.filament_diameter.get_at(extruder_idx);
            if (nozzle_diam - EPSILON > first_nozzle_diam || nozzle_diam + EPSILON < first_nozzle_diam
                || std::abs((filament_diam - first_filament_diam) / first_filament_diam) > 0.1)
                // BBS: remove L()
                return { ("Different nozzle diameters and different filament diameters is not allowed when prime tower is enabled.") };
        }

        if (m_config.ooze_prevention)
            return { ("Ooze prevention is currently not supported with the prime tower enabled.") };

        // BBS: remove following logic and _L()
#if 0
        if (m_config.gcode_flavor != gcfRepRapSprinter && m_config.gcode_flavor != gcfRepRapFirmware &&
            m_config.gcode_flavor != gcfRepetier && m_config.gcode_flavor != gcfMarlinLegacy && m_config.gcode_flavor != gcfMarlinFirmware)
            return {("The prime tower is currently only supported for the Marlin, RepRap/Sprinter, RepRapFirmware and Repetier G-code flavors.")};

        if ((m_config.print_sequence == PrintSequence::ByObject) && extruders.size() > 1)
            return { L("The prime tower is not supported in \"By object\" print."), nullptr, "enable_prime_tower" };

        // BBS: When prime tower is on, object layer and support layer must be aligned. So support gap should be multiple of object layer height.
        for (size_t i = 0; i < m_objects.size(); i++) {
            const PrintObject* object = m_objects[i];
            const SlicingParameters& slicing_params = object->slicing_parameters();
            if (object->config().adaptive_layer_height) {
                return  { L("The prime tower is not supported when adaptive layer height is on. It requires that all objects have the same layer height."), object, "adaptive_layer_height" };
            }

            if (!object->config().enable_support)
                continue;

            double gap_layers = slicing_params.gap_object_support / slicing_params.layer_height;
            if (gap_layers - (int)gap_layers > EPSILON) {
                return  { L("The prime tower requires \"support gap\" to be multiple of layer height"), object };
            }
        }
#endif

        if (m_objects.size() > 1) {
            bool                                has_custom_layering = false;
            std::vector<std::vector<coordf_t>>  layer_height_profiles;
            for (const PrintObject *object : m_objects) {
                has_custom_layering = ! object->model_object()->layer_config_ranges.empty() || ! object->model_object()->layer_height_profile.empty();
                if (has_custom_layering) {
                    layer_height_profiles.assign(m_objects.size(), std::vector<coordf_t>());
                    break;
                }
            }
            const SlicingParameters &slicing_params0 = m_objects.front()->slicing_parameters();
            size_t            tallest_object_idx = 0;
            if (has_custom_layering)
                PrintObject::update_layer_height_profile(*m_objects.front()->model_object(), slicing_params0, layer_height_profiles.front());
            for (size_t i = 1; i < m_objects.size(); ++ i) {
                const PrintObject       *object         = m_objects[i];
                const SlicingParameters &slicing_params = object->slicing_parameters();
                if (std::abs(slicing_params.first_print_layer_height - slicing_params0.first_print_layer_height) > EPSILON ||
                    std::abs(slicing_params.layer_height             - slicing_params0.layer_height            ) > EPSILON)
                    return {L("The prime tower requires that all objects have the same layer heights"), object, "initial_layer_print_height"};
                if (slicing_params.raft_layers() != slicing_params0.raft_layers())
                    return {L("The prime tower requires that all objects are printed over the same number of raft layers"), object, "raft_layers"};
                // BBS: support gap can be multiple of object layer height, remove _L()
#if 0
                if (slicing_params0.gap_object_support != slicing_params.gap_object_support ||
                    slicing_params0.gap_support_object != slicing_params.gap_support_object)
                    return  {("The prime tower is only supported for multiple objects if they are printed with the same support_top_z_distance"), object};
#endif
                if (!equal_layering(slicing_params, slicing_params0))
                    return  { L("The prime tower requires that all objects are sliced with the same layer heights."), object };
                if (has_custom_layering) {
                    PrintObject::update_layer_height_profile(*object->model_object(), slicing_params, layer_height_profiles[i]);
                    if (*(layer_height_profiles[i].end()-2) > *(layer_height_profiles[tallest_object_idx].end()-2))
                        tallest_object_idx = i;
                }
            }

            // BBS: remove obsolete logics and _L()
#if 0
            if (has_custom_layering) {
                for (size_t idx_object = 0; idx_object < m_objects.size(); ++ idx_object) {
                    if (idx_object == tallest_object_idx)
                        continue;
                    if (layer_height_profiles[idx_object] != layer_height_profiles[tallest_object_idx])
                        return {("The prime tower is only supported if all objects have the same variable layer height"), m_objects[idx_object]};
                }
            }
#endif
        }
    }

	{
		// Find the smallest used nozzle diameter and the number of unique nozzle diameters.
		double min_nozzle_diameter = std::numeric_limits<double>::max();
		double max_nozzle_diameter = 0;
		for (unsigned int extruder_id : extruders) {
			double dmr = m_config.nozzle_diameter.get_at(extruder_id);
			min_nozzle_diameter = std::min(min_nozzle_diameter, dmr);
			max_nozzle_diameter = std::max(max_nozzle_diameter, dmr);
		}

        // BBS: remove L()
#if 0
        // We currently allow one to assign extruders with a higher index than the number
        // of physical extruders the machine is equipped with, as the Printer::apply() clamps them.
        unsigned int total_extruders_count = m_config.nozzle_diameter.size();
        for (const auto& extruder_idx : extruders)
            if ( extruder_idx >= total_extruders_count )
                return ("One or more object were assigned an extruder that the printer does not have.");
#endif

        auto validate_extrusion_width = [/*min_nozzle_diameter,*/ max_nozzle_diameter](const ConfigBase &config, const char *opt_key, double layer_height, std::string &err_msg) -> bool {
            // This may change in the future, if we switch to "extrusion width wrt. nozzle diameter"
            // instead of currently used logic "extrusion width wrt. layer height", see GH issues #1923 #2829.
//        	double extrusion_width_min = config.get_abs_value(opt_key, min_nozzle_diameter);
//        	double extrusion_width_max = config.get_abs_value(opt_key, max_nozzle_diameter);
            double extrusion_width_min = config.get_abs_value(opt_key);
            double extrusion_width_max = config.get_abs_value(opt_key);
        	if (extrusion_width_min == 0) {
        		// Default "auto-generated" extrusion width is always valid.
        	} else if (extrusion_width_min <= layer_height) {
                err_msg = L("Too small line width");
				return false;
			} else if (extrusion_width_max >= max_nozzle_diameter * 2.5) {
                err_msg = L("Too large line width");
				return false;
			}
			return true;
		};
        for (PrintObject *object : m_objects) {
            if (object->has_support_material()) {
                // BBS: remove useless logics and L()
#if 0
				if ((object->config().support_filament == 0 || object->config().support_interface_filament == 0) && max_nozzle_diameter - min_nozzle_diameter > EPSILON) {
                    // The object has some form of support and either support_filament or support_interface_filament
                    // will be printed with the current tool without a forced tool change. Play safe, assert that all object nozzles
                    // are of the same diameter.
                    return {("Printing with multiple extruders of differing nozzle diameters. "
                           "If support is to be printed with the current filament (support_filament == 0 or support_interface_filament == 0), "
                           "all nozzles have to be of the same diameter."), object, "support_filament"};
                }
#endif

                // BBS
#if 0
                if (this->has_wipe_tower() && object->config().independent_support_layer_height) {
                    return {L("The prime tower requires that support has the same layer height with object."), object, "support_filament"};
                }
#endif
            }

            // Do we have custom support data that would not be used?
            // Notify the user in that case.
            if (! object->has_support() && warning) {
                for (const ModelVolume* mv : object->model_object()->volumes) {
                    bool has_enforcers = mv->is_support_enforcer() ||
                        (mv->is_model_part() && mv->supported_facets.has_facets(*mv, EnforcerBlockerType::ENFORCER));
                    if (has_enforcers) {
                        warning->string = L("Support enforcers are used but support is not enabled. Please enable support.");
                        warning->object = object;
                        break;
                    }
                }
            }

            double initial_layer_print_height = m_config.initial_layer_print_height.value;
            double first_layer_min_nozzle_diameter;
            if (object->has_raft()) {
                // if we have raft layers, only support material extruder is used on first layer
                size_t first_layer_extruder = object->config().raft_layers == 1
                    ? object->config().support_interface_filament-1
                    : object->config().support_filament-1;
                first_layer_min_nozzle_diameter = (first_layer_extruder == size_t(-1)) ?
                    min_nozzle_diameter :
                    m_config.nozzle_diameter.get_at(first_layer_extruder);
            } else {
                // if we don't have raft layers, any nozzle diameter is potentially used in first layer
                first_layer_min_nozzle_diameter = min_nozzle_diameter;
            }
            if (initial_layer_print_height > first_layer_min_nozzle_diameter)
                return  {L("Layer height cannot exceed nozzle diameter"), object, "initial_layer_print_height"};

            // validate layer_height
            double layer_height = object->config().layer_height.value;
            if (layer_height > min_nozzle_diameter)
                return  {L("Layer height cannot exceed nozzle diameter"), object, "layer_height"};

            // Validate extrusion widths.
            std::string err_msg;
            if (!validate_extrusion_width(object->config(), "line_width", layer_height, err_msg))
            	return {err_msg, object, "line_width"};
            if (object->has_support() || object->has_raft()) {
                if (!validate_extrusion_width(object->config(), "support_line_width", layer_height, err_msg))
                    return {err_msg, object, "support_line_width"};
            }
            for (const char *opt_key : { "inner_wall_line_width", "outer_wall_line_width", "sparse_infill_line_width", "internal_solid_infill_line_width", "top_surface_line_width" })
				for (const PrintRegion &region : object->all_regions())
                    if (!validate_extrusion_width(region.config(), opt_key, layer_height, err_msg))
		            	return  {err_msg, object, opt_key};
        }
    }


    const ConfigOptionDef* bed_type_def = print_config_def.get("curr_bed_type");
    assert(bed_type_def != nullptr);

    const t_config_enum_values* bed_type_keys_map = bed_type_def->enum_keys_map;
    for (unsigned int extruder_id : extruders) {
        const ConfigOptionInts* bed_temp_opt = m_config.option<ConfigOptionInts>(get_bed_temp_key(m_config.curr_bed_type));
        for (unsigned int extruder_id : extruders) {
            int curr_bed_temp = bed_temp_opt->get_at(extruder_id);
            if (curr_bed_temp == 0 && bed_type_keys_map != nullptr) {
                std::string bed_type_name;
                for (auto item : *bed_type_keys_map) {
                    if (item.second == m_config.curr_bed_type) {
                        bed_type_name = item.first;
                        break;
                    }
                }

                return { format(L("Plate %d: %s does not support filament %s.\n"), this->get_plate_index() + 1,
                                L(bed_type_name), extruder_id + 1) };
            }
        }
    }

    return {};
}

#if 0
// the bounding box of objects placed in copies position
// (without taking skirt/brim/support material into account)
BoundingBox Print::bounding_box() const
{
    BoundingBox bb;
    for (const PrintObject *object : m_objects)
        for (const PrintInstance &instance : object->instances()) {
        	BoundingBox bb2(object->bounding_box());
        	bb.merge(bb2.min + instance.shift);
        	bb.merge(bb2.max + instance.shift);
        }
    return bb;
}

// the total bounding box of extrusions, including skirt/brim/support material
// this methods needs to be called even when no steps were processed, so it should
// only use configuration values
BoundingBox Print::total_bounding_box() const
{
    // get objects bounding box
    BoundingBox bb = this->bounding_box();

    // we need to offset the objects bounding box by at least half the perimeters extrusion width
    Flow perimeter_flow = m_objects.front()->get_layer(0)->get_region(0)->flow(frPerimeter);
    double extra = perimeter_flow.width/2;

    // consider support material
    if (this->has_support_material()) {
        extra = std::max(extra, SUPPORT_MATERIAL_MARGIN);
    }

    // consider brim and skirt
    if (m_config.brim_width.value > 0) {
        Flow brim_flow = this->brim_flow();
        extra = std::max(extra, m_config.brim_width.value + brim_flow.width/2);
    }
    if (this->has_skirt()) {
        int skirts = m_config.skirt_loops.value;
        if (skirts == 0 && this->has_infinite_skirt()) skirts = 1;
        Flow skirt_flow = this->skirt_flow();
        extra = std::max(
            extra,
            m_config.brim_width.value
                + m_config.skirt_distance.value
                + skirts * skirt_flow.spacing()
                + skirt_flow.width/2
        );
    }

    if (extra > 0)
        bb.offset(scale_(extra));

    return bb;
}
#endif

double Print::skirt_first_layer_height() const
{
    return m_config.initial_layer_print_height.value;
}

Flow Print::brim_flow() const
{
    ConfigOptionFloat width = m_config.initial_layer_line_width;
    if (width.value == 0)
        width = m_print_regions.front()->config().inner_wall_line_width;
    if (width.value == 0)
        width = m_objects.front()->config().line_width;

    /* We currently use a random region's perimeter extruder.
       While this works for most cases, we should probably consider all of the perimeter
       extruders and take the one with, say, the smallest index.
       The same logic should be applied to the code that selects the extruder during G-code
       generation as well. */
    return Flow::new_from_config_width(
        frPerimeter,
		width,
        (float)m_config.nozzle_diameter.get_at(m_print_regions.front()->config().wall_filament-1),
		(float)this->skirt_first_layer_height());
}

Flow Print::skirt_flow() const
{
    ConfigOptionFloat width = m_config.initial_layer_line_width;
    if (width.value == 0)
        width = m_objects.front()->config().line_width;

    /* We currently use a random object's support material extruder.
       While this works for most cases, we should probably consider all of the support material
       extruders and take the one with, say, the smallest index;
       The same logic should be applied to the code that selects the extruder during G-code
       generation as well. */
    return Flow::new_from_config_width(
        frPerimeter,
		width,
		(float)m_config.nozzle_diameter.get_at(m_objects.front()->config().support_filament-1),
		(float)this->skirt_first_layer_height());
}

bool Print::has_support_material() const
{
    for (const PrintObject *object : m_objects)
        if (object->has_support_material())
            return true;
    return false;
}

/*  This method assigns extruders to the volumes having a material
    but not having extruders set in the volume config. */
void Print::auto_assign_extruders(ModelObject* model_object) const
{
    // only assign extruders if object has more than one volume
    if (model_object->volumes.size() < 2)
        return;

//    size_t extruders = m_config.nozzle_diameter.values.size();
    for (size_t volume_id = 0; volume_id < model_object->volumes.size(); ++ volume_id) {
        ModelVolume *volume = model_object->volumes[volume_id];
        //FIXME Vojtech: This assigns an extruder ID even to a modifier volume, if it has a material assigned.
        if ((volume->is_model_part() || volume->is_modifier()) && ! volume->material_id().empty() && ! volume->config.has("extruder"))
            volume->config.set("extruder", int(volume_id + 1));
    }
}

void  PrintObject::set_shared_object(PrintObject *object)
{
    m_shared_object = object;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": this=%1%, found shared object from %2%")%this%m_shared_object;
}

void  PrintObject::clear_shared_object()
{
    if (m_shared_object) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": this=%1%, clear previous shared object data %2%")%this %m_shared_object;
        m_layers.clear();
        m_support_layers.clear();
        m_tree_support_layers.clear();

        m_shared_object = nullptr;

        invalidate_all_steps_without_cancel();
    }
}

void  PrintObject::copy_layers_from_shared_object()
{
    if (m_shared_object) {
        m_layers.clear();
        m_support_layers.clear();
        m_tree_support_layers.clear();

        firstLayerObjSliceByVolume.clear();
        firstLayerObjSliceByGroups.clear();

        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": this=%1%, copied layers from object %2%")%this%m_shared_object;
        m_layers = m_shared_object->layers();
        m_support_layers = m_shared_object->support_layers();
        m_tree_support_layers = m_shared_object->tree_support_layers();

        firstLayerObjSliceByVolume = m_shared_object->firstLayerObjSlice();
        firstLayerObjSliceByGroups = m_shared_object->firstLayerObjGroups();
    }
}

// BBS
BoundingBox PrintObject::get_first_layer_bbox(float& a, float& layer_height, std::string& name)
{
    BoundingBox bbox;
    a = 0;
    name = this->model_object()->name;
    if (layer_count() > 0) {
        auto layer = get_layer(0);
        layer_height = layer->height;
        // only work for object with single instance
        auto shift = instances()[0].shift;
        for (auto bb : layer->lslices_bboxes)
        {
            bb.translate(shift.x(), shift.y());
            bbox.merge(bb);
        }
        for (auto slice : layer->lslices) {
            a += area(slice);
        }
    }
    if (has_brim())
        bbox = firstLayerObjectBrimBoundingBox;
    return bbox;
}

// BBS: map print object with its first layer's first extruder
std::map<ObjectID, unsigned int> getObjectExtruderMap(const Print& print) {
    std::map<ObjectID, unsigned int> objectExtruderMap;
    for (const PrintObject* object : print.objects()) {
        // BBS
        unsigned int objectFirstLayerFirstExtruder = print.config().filament_diameter.size();
        auto firstLayerRegions = object->layers().front()->regions();
        if (!firstLayerRegions.empty()) {
            for (const LayerRegion* regionPtr : firstLayerRegions) {
                if (regionPtr -> has_extrusions())
                    objectFirstLayerFirstExtruder = std::min(objectFirstLayerFirstExtruder,
                        regionPtr->region().extruder(frExternalPerimeter));
            }
        }
        objectExtruderMap.insert(std::make_pair(object->id(), objectFirstLayerFirstExtruder));
    }
    return objectExtruderMap;
}

// Slicing process, running at a background thread.
void Print::process()
{
    name_tbb_thread_pool_threads_set_locale();

    //compute the PrintObject with the same geometries
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": this=%1%, enter")%this;
    for (PrintObject *obj : m_objects)
        obj->clear_shared_object();

    //add the print_object share check logic
    auto is_print_object_the_same = [this](const PrintObject* object1, const PrintObject* object2) -> bool{
        if (object1->trafo().matrix() != object2->trafo().matrix())
            return false;
        const ModelObject* model_obj1 = object1->model_object();
        const ModelObject* model_obj2 = object2->model_object();
        if (model_obj1->volumes.size() != model_obj2->volumes.size())
            return false;
        bool has_extruder1 = model_obj1->config.has("extruder");
        bool has_extruder2 = model_obj2->config.has("extruder");
        if ((has_extruder1 != has_extruder2)
            || (has_extruder1 && model_obj1->config.extruder() != model_obj2->config.extruder()))
            return false;
        for (int index = 0; index < model_obj1->volumes.size(); index++) {
            const ModelVolume &model_volume1 = *model_obj1->volumes[index];
            const ModelVolume &model_volume2 = *model_obj2->volumes[index];
            if (model_volume1.type() != model_volume2.type())
                return false;
            if (model_volume1.mesh_ptr() != model_volume2.mesh_ptr())
                return false;
            has_extruder1 = model_volume1.config.has("extruder");
            has_extruder2 = model_volume2.config.has("extruder");
            if ((has_extruder1 != has_extruder2)
                || (has_extruder1 && model_volume1.config.extruder() != model_volume2.config.extruder()))
                return false;
            if (!model_volume1.supported_facets.equals(model_volume2.supported_facets))
                return false;
            if (!model_volume1.seam_facets.equals(model_volume2.seam_facets))
                return false;
            if (!model_volume1.mmu_segmentation_facets.equals(model_volume2.mmu_segmentation_facets))
                return false;
            if (model_volume1.config.get() != model_volume2.config.get())
                return false;
        }
        //if (!object1->config().equals(object2->config()))
        //    return false;
        if (model_obj1->config.get() != model_obj2->config.get())
            return false;
        return true;
    };
    int object_count = m_objects.size();
    std::set<PrintObject*> need_slicing_objects;
    for (int index = 0; index < object_count; index++)
    {
        PrintObject *obj =  m_objects[index];
        for (PrintObject *slicing_obj : need_slicing_objects)
        {
            if (is_print_object_the_same(obj, slicing_obj)) {
                obj->set_shared_object(slicing_obj);
                break;
            }
        }
        if (!obj->get_shared_object())
            need_slicing_objects.insert(obj);
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": total object counts %1% in current print, need to slice %2%")%m_objects.size()%need_slicing_objects.size();
    BOOST_LOG_TRIVIAL(info) << "Starting the slicing process." << log_memory_info();
    for (PrintObject *obj : m_objects) {
        if (need_slicing_objects.count(obj) != 0) {
            obj->make_perimeters();
        }
        else {
            if (obj->set_started(posSlice))
                obj->set_done(posSlice);
            if (obj->set_started(posPerimeters))
                obj->set_done(posPerimeters);
        }
    }
    for (PrintObject *obj : m_objects) {
        if (need_slicing_objects.count(obj) != 0) {
            obj->infill();
        }
        else {
            if (obj->set_started(posPrepareInfill))
                obj->set_done(posPrepareInfill);
            if (obj->set_started(posInfill))
                obj->set_done(posInfill);
        }
    }
    for (PrintObject *obj : m_objects) {
        if (need_slicing_objects.count(obj) != 0) {
            obj->ironing();
        }
        else {
            if (obj->set_started(posIroning))
                obj->set_done(posIroning);
        }
    }
    for (PrintObject *obj : m_objects) {
        if (need_slicing_objects.count(obj) != 0) {
            obj->generate_support_material();
        }
        else {
            if (obj->set_started(posSupportMaterial))
                obj->set_done(posSupportMaterial);
        }
    }

    for (PrintObject *obj : m_objects)
    {
        if (need_slicing_objects.count(obj) == 0)
            obj->copy_layers_from_shared_object();
    }
    if (this->set_started(psWipeTower)) {
        m_wipe_tower_data.clear();
        m_tool_ordering.clear();
        if (this->has_wipe_tower()) {
            this->_make_wipe_tower();
        } else if (this->config().print_sequence != PrintSequence::ByObject) {
        	// Initialize the tool ordering, so it could be used by the G-code preview slider for planning tool changes and filament switches.
        	m_tool_ordering = ToolOrdering(*this, -1, false);
            if (m_tool_ordering.empty() || m_tool_ordering.last_extruder() == unsigned(-1))
                throw Slic3r::SlicingError("The print is empty. The model is not printable with current print settings.");
        }
        this->set_done(psWipeTower);
    }
    if (this->set_started(psSkirtBrim)) {
        this->set_status(70, L("Generating skirt & brim"));

        m_skirt.clear();
        m_skirt_convex_hull.clear();
        m_first_layer_convex_hull.points.clear();
        const bool draft_shield = config().draft_shield != dsDisabled;

        if (this->has_skirt() && draft_shield) {
            // In case that draft shield is active, generate skirt first so brim
            // can be trimmed to make room for it.
            _make_skirt();
        }

        //BBS: get the objects' indices when GCodes are generated
        ToolOrdering tool_ordering;
        unsigned int initial_extruder_id = (unsigned int)-1;
        unsigned int final_extruder_id = (unsigned int)-1;
        bool         has_wipe_tower = false;
        std::vector<const PrintInstance*> 					print_object_instances_ordering;
        std::vector<const PrintInstance*>::const_iterator 	print_object_instance_sequential_active;
        std::vector<std::pair<coordf_t, std::vector<GCode::LayerToPrint>>> layers_to_print = GCode::collect_layers_to_print(*this);
        std::vector<unsigned int> printExtruders;
        if (this->config().print_sequence == PrintSequence::ByObject) {
            // Order object instances for sequential print.
            print_object_instances_ordering = sort_object_instances_by_model_order(*this);
            //        print_object_instances_ordering = sort_object_instances_by_max_z(print);
            print_object_instance_sequential_active = print_object_instances_ordering.begin();
            for (; print_object_instance_sequential_active != print_object_instances_ordering.end(); ++print_object_instance_sequential_active) {
                tool_ordering = ToolOrdering(*(*print_object_instance_sequential_active)->print_object, initial_extruder_id);
                if ((initial_extruder_id = tool_ordering.first_extruder()) != static_cast<unsigned int>(-1)) {
                    append(printExtruders, tool_ordering.tools_for_layer(layers_to_print.front().first).extruders);
                }
            }
        }
        else {
            tool_ordering = this->tool_ordering();
            tool_ordering.assign_custom_gcodes(*this);
            has_wipe_tower = this->has_wipe_tower() && tool_ordering.has_wipe_tower();
            //BBS: have no single_extruder_multi_material_priming
#if 0
            initial_extruder_id = (has_wipe_tower && !this->config().single_extruder_multi_material_priming) ?
                // The priming towers will be skipped.
                tool_ordering.all_extruders().back() :
                // Don't skip the priming towers.
                tool_ordering.first_extruder();
#endif
            initial_extruder_id = tool_ordering.first_extruder();
            print_object_instances_ordering = chain_print_object_instances(*this);
            append(printExtruders, tool_ordering.tools_for_layer(layers_to_print.front().first).extruders);
        }

        auto objectExtruderMap = getObjectExtruderMap(*this);
        std::vector<std::pair<ObjectID, unsigned int>> objPrintVec;
        for (const PrintInstance* instance : print_object_instances_ordering) {
            const ObjectID& print_object_ID = instance->print_object->id();
            bool existObject = false;
            for (auto& objIDPair : objPrintVec) {
                if (print_object_ID == objIDPair.first) existObject = true;
            }
            if (!existObject && objectExtruderMap.find(print_object_ID) != objectExtruderMap.end())
                objPrintVec.push_back(std::make_pair(print_object_ID, objectExtruderMap.at(print_object_ID)));
        }
        // BBS: m_brimMap and m_supportBrimMap are used instead of m_brim to generate brim of objs and supports seperately
        m_brimMap.clear();
        m_supportBrimMap.clear();
        m_first_layer_convex_hull.points.clear();
        if (this->has_brim()) {
            Polygons islands_area;
            make_brim(*this, this->make_try_cancel(), islands_area, m_brimMap,
                m_supportBrimMap, objPrintVec, printExtruders);
            for (Polygon& poly_ex : islands_area)
                poly_ex.douglas_peucker(SCALED_RESOLUTION);
            for (Polygon &poly : union_(this->first_layer_islands(), islands_area))
                append(m_first_layer_convex_hull.points, std::move(poly.points));
        }


        if (has_skirt() && ! draft_shield) {
            // In case that draft shield is NOT active, generate skirt now.
            // It will be placed around the brim, so brim has to be ready.
            assert(m_skirt.empty());
            _make_skirt();
        }

        this->finalize_first_layer_convex_hull();
        this->set_done(psSkirtBrim);
    }
    //BBS
    for (PrintObject *obj : m_objects) {
        if (need_slicing_objects.count(obj) != 0) {
            obj->simplify_extrusion_path();
        }
        else {
            if (obj->set_started(posSimplifyPath))
                obj->set_done(posSimplifyPath);
            if (obj->set_started(posSimplifySupportPath))
                obj->set_done(posSimplifySupportPath);
        }
    }

    BOOST_LOG_TRIVIAL(info) << "Slicing process finished." << log_memory_info();
}

// G-code export process, running at a background thread.
// The export_gcode may die for various reasons (fails to process filename_format,
// write error into the G-code, cannot execute post-processing scripts).
// It is up to the caller to show an error message.
std::string Print::export_gcode(const std::string& path_template, GCodeProcessorResult* result, ThumbnailsGeneratorCallback thumbnail_cb)
{
    // output everything to a G-code file
    // The following call may die if the filename_format template substitution fails.
    std::string path = this->output_filepath(path_template);
    std::string message;
    if (!path.empty() && result == nullptr) {
        // Only show the path if preview_data is not set -> running from command line.
        message = L("Exporting G-code");
        message += " to ";
        message += path;
    } else
        message = L("Generating G-code");
    this->set_status(80, message);

    // The following line may die for multiple reasons.
    GCode gcode;
    //BBS: compute plate offset for gcode-generator
    const Vec3d origin = this->get_plate_origin();
    gcode.set_gcode_offset(origin(0), origin(1));
    gcode.do_export(this, path.c_str(), result, thumbnail_cb);
    return path.c_str();
}

void Print::_make_skirt()
{
    // First off we need to decide how tall the skirt must be.
    // The skirt_height option from config is expressed in layers, but our
    // object might have different layer heights, so we need to find the print_z
    // of the highest layer involved.
    // Note that unless has_infinite_skirt() == true
    // the actual skirt might not reach this $skirt_height_z value since the print
    // order of objects on each layer is not guaranteed and will not generally
    // include the thickest object first. It is just guaranteed that a skirt is
    // prepended to the first 'n' layers (with 'n' = skirt_height).
    // $skirt_height_z in this case is the highest possible skirt height for safety.
    coordf_t skirt_height_z = 0.;
    for (const PrintObject *object : m_objects) {
        size_t skirt_layers = this->has_infinite_skirt() ?
            object->layer_count() :
            std::min(size_t(m_config.skirt_height.value), object->layer_count());
        skirt_height_z = std::max(skirt_height_z, object->m_layers[skirt_layers-1]->print_z);
    }

    // Collect points from all layers contained in skirt height.
    Points points;

    // BBS
    std::map<PrintObject*, Polygon> object_convex_hulls;
    for (PrintObject *object : m_objects) {
        Points object_points;
        // Get object layers up to skirt_height_z.
        for (const Layer *layer : object->m_layers) {
            if (layer->print_z > skirt_height_z)
                break;
            for (const ExPolygon &expoly : layer->lslices)
                // Collect the outer contour points only, ignore holes for the calculation of the convex hull.
                append(object_points, expoly.contour.points);
        }
        // Get support layers up to skirt_height_z.
        for (const SupportLayer *layer : object->support_layers()) {
            if (layer->print_z > skirt_height_z)
                break;
            layer->support_fills.collect_points(object_points);
        }
        // BBS
        for (const TreeSupportLayer* layer : object->m_tree_support_layers) {
            if (layer->print_z > skirt_height_z)
                break;

            layer->support_fills.collect_points(object_points);
        }

        object_convex_hulls.insert({ object, Slic3r::Geometry::convex_hull(object_points) });

        // Repeat points for each object copy.
        for (const PrintInstance &instance : object->instances()) {
            Points copy_points = object_points;
            for (Point &pt : copy_points)
                pt += instance.shift;
            append(points, copy_points);
        }
    }

    // Include the wipe tower.
    append(points, this->first_layer_wipe_tower_corners());

    // Unless draft shield is enabled, include all brims as well.
    if (config().draft_shield == dsDisabled)
        append(points, m_first_layer_convex_hull.points);

    if (points.size() < 3)
        // At least three points required for a convex hull.
        return;

    this->throw_if_canceled();
    Polygon convex_hull = Slic3r::Geometry::convex_hull(points);

    // Skirt may be printed on several layers, having distinct layer heights,
    // but loops must be aligned so can't vary width/spacing
    // TODO: use each extruder's own flow
    double initial_layer_print_height = this->skirt_first_layer_height();
    Flow   flow = this->skirt_flow();
    float  spacing = flow.spacing();
    double mm3_per_mm = flow.mm3_per_mm();

    std::vector<size_t> extruders;
    std::vector<double> extruders_e_per_mm;
    {
        auto set_extruders = this->extruders();
        extruders.reserve(set_extruders.size());
        extruders_e_per_mm.reserve(set_extruders.size());
        for (auto &extruder_id : set_extruders) {
            extruders.push_back(extruder_id);
            extruders_e_per_mm.push_back(Extruder((unsigned int)extruder_id, &m_config, m_config.single_extruder_multi_material).e_per_mm(mm3_per_mm));
        }
    }

    // Number of skirt loops per skirt layer.
    size_t n_skirts = m_config.skirt_loops.value;
    if (this->has_infinite_skirt() && n_skirts == 0)
        n_skirts = 1;

    // Initial offset of the brim inner edge from the object (possible with a support & raft).
    // The skirt will touch the brim if the brim is extruded.
    auto   distance = float(scale_(m_config.skirt_distance.value) - spacing/2.);
    // Draw outlines from outside to inside.
    // Loop while we have less skirts than required or any extruder hasn't reached the min length if any.
    std::vector<coordf_t> extruded_length(extruders.size(), 0.);
    for (size_t i = n_skirts, extruder_idx = 0; i > 0; -- i) {
        this->throw_if_canceled();
        // Offset the skirt outside.
        distance += float(scale_(spacing));
        // Generate the skirt centerline.
        Polygon loop;
        {
            // BBS. skirt_distance is defined as the gap between skirt and outer most brim, so no need to add max_brim_width
            Polygons loops = offset(convex_hull, distance, ClipperLib::jtRound, float(scale_(0.1)));
            Geometry::simplify_polygons(loops, scale_(0.05), &loops);
			if (loops.empty())
				break;
			loop = loops.front();
        }
        // Extrude the skirt loop.
        ExtrusionLoop eloop(elrSkirt);
        eloop.paths.emplace_back(ExtrusionPath(
            ExtrusionPath(
                erSkirt,
                (float)mm3_per_mm,         // this will be overridden at G-code export time
                flow.width(),
				(float)initial_layer_print_height  // this will be overridden at G-code export time
            )));
        eloop.paths.back().polyline = loop.split_at_first_point();
        m_skirt.append(eloop);
        if (Print::min_skirt_length > 0) {
            // The skirt length is limited. Sum the total amount of filament length extruded, in mm.
            extruded_length[extruder_idx] += unscale<double>(loop.length()) * extruders_e_per_mm[extruder_idx];
            if (extruded_length[extruder_idx] < Print::min_skirt_length) {
                // Not extruded enough yet with the current extruder. Add another loop.
                if (i == 1)
                    ++ i;
            } else {
                assert(extruded_length[extruder_idx] >= Print::min_skirt_length);
                // Enough extruded with the current extruder. Extrude with the next one,
                // until the prescribed number of skirt loops is extruded.
                if (extruder_idx + 1 < extruders.size())
                    ++ extruder_idx;
            }
        } else {
            // The skirt lenght is not limited, extrude the skirt with the 1st extruder only.
        }
    }
    // Brims were generated inside out, reverse to print the outmost contour first.
    m_skirt.reverse();

    // Remember the outer edge of the last skirt line extruded as m_skirt_convex_hull.
    for (Polygon &poly : offset(convex_hull, distance + 0.5f * float(scale_(spacing)), ClipperLib::jtRound, float(scale_(0.1))))
        append(m_skirt_convex_hull, std::move(poly.points));

    // BBS
    const int n_object_skirts = 1;
    const double object_skirt_distance = scale_(1.0);
    for (auto obj_cvx_hull : object_convex_hulls) {
        PrintObject* object = obj_cvx_hull.first;
        for (int i = 0; i < n_object_skirts; i++) {
            distance += float(scale_(spacing));
            Polygon loop;
            {
                // BBS. skirt_distance is defined as the gap between skirt and outer most brim, so no need to add max_brim_width
                Polygons loops = offset(obj_cvx_hull.second, object_skirt_distance, ClipperLib::jtRound, float(scale_(0.1)));
                Geometry::simplify_polygons(loops, scale_(0.05), &loops);
                if (loops.empty())
                    break;
                loop = loops.front();
            }

            // Extrude the skirt loop.
            ExtrusionLoop eloop(elrSkirt);
            eloop.paths.emplace_back(ExtrusionPath(
                ExtrusionPath(
                    erSkirt,
                    (float)mm3_per_mm,         // this will be overridden at G-code export time
                    flow.width(),
                    (float)initial_layer_print_height  // this will be overridden at G-code export time
                )));
            eloop.paths.back().polyline = loop.split_at_first_point();
            object->m_skirt.append(std::move(eloop));
        }
    }
}

Polygons Print::first_layer_islands() const
{
    Polygons islands;
    for (PrintObject *object : m_objects) {
        Polygons object_islands;
        for (ExPolygon &expoly : object->m_layers.front()->lslices)
            object_islands.push_back(expoly.contour);
        if (! object->support_layers().empty())
            object->support_layers().front()->support_fills.polygons_covered_by_spacing(object_islands, float(SCALED_EPSILON));
        if (! object->tree_support_layers().empty()) {
            ExPolygons& expolys_first_layer = object->m_tree_support_layers.front()->lslices;
            for (ExPolygon &expoly : expolys_first_layer) {
                object_islands.push_back(expoly.contour);
            }
        }
        islands.reserve(islands.size() + object_islands.size() * object->instances().size());
        for (const PrintInstance &instance : object->instances())
            for (Polygon &poly : object_islands) {
                islands.push_back(poly);
                islands.back().translate(instance.shift);
            }
    }
    return islands;
}

std::vector<Point> Print::first_layer_wipe_tower_corners(bool check_wipe_tower_existance) const
{
    std::vector<Point> corners;
    if (check_wipe_tower_existance && (!has_wipe_tower() || m_wipe_tower_data.tool_changes.empty()))
        return corners;
    {
        double width = m_config.prime_tower_width + 2*m_wipe_tower_data.brim_width;
        double depth = m_wipe_tower_data.depth + 2*m_wipe_tower_data.brim_width;
        Vec2d pt0(-m_wipe_tower_data.brim_width, -m_wipe_tower_data.brim_width);
        for (Vec2d pt : {
                pt0,
                Vec2d(pt0.x()+width, pt0.y()      ),
                Vec2d(pt0.x()+width, pt0.y()+depth),
                Vec2d(pt0.x(),       pt0.y()+depth)
            }) {
            pt = Eigen::Rotation2Dd(Geometry::deg2rad(m_config.wipe_tower_rotation_angle.value)) * pt;
            // BBS: add partplate logic
            pt += Vec2d(m_config.wipe_tower_x.get_at(m_plate_index) + m_origin(0), m_config.wipe_tower_y.get_at(m_plate_index) + m_origin(1));
            corners.emplace_back(Point(scale_(pt.x()), scale_(pt.y())));
        }
    }
    return corners;
}

void Print::finalize_first_layer_convex_hull()
{
    append(m_first_layer_convex_hull.points, m_skirt_convex_hull);
    if (m_first_layer_convex_hull.empty()) {
        // Neither skirt nor brim was extruded. Collect points of printed objects from 1st layer.
        for (Polygon &poly : this->first_layer_islands())
            append(m_first_layer_convex_hull.points, std::move(poly.points));
    }
    append(m_first_layer_convex_hull.points, this->first_layer_wipe_tower_corners());
    m_first_layer_convex_hull = Geometry::convex_hull(m_first_layer_convex_hull.points);
}

// Wipe tower support.
bool Print::has_wipe_tower() const
{
    if (enable_timelapse_print())
        return true;

    return
        ! m_config.spiral_mode.value &&
        m_config.enable_prime_tower.value &&
        m_config.filament_diameter.values.size() > 1;
}

const WipeTowerData& Print::wipe_tower_data(size_t filaments_cnt) const
{
    // If the wipe tower wasn't created yet, make sure the depth and brim_width members are set to default.
    if (! is_step_done(psWipeTower) && filaments_cnt !=0) {
        // BBS
        double width = m_config.prime_tower_width;
        double layer_height = 0.2; // hard code layer height
        double wipe_volume = m_config.prime_volume;
        if (filaments_cnt == 1 && enable_timelapse_print()) {
            const_cast<Print *>(this)->m_wipe_tower_data.depth = wipe_volume / (layer_height * width);
        } else {
            const_cast<Print *>(this)->m_wipe_tower_data.depth = wipe_volume * (filaments_cnt - 1) / (layer_height * width);
        }
        const_cast<Print*>(this)->m_wipe_tower_data.brim_width = m_config.prime_tower_brim_width;
    }

    return m_wipe_tower_data;
}

bool Print::enable_timelapse_print() const
{
    return m_config.timelapse_no_toolhead.value;
}

void Print::_make_wipe_tower()
{
    m_wipe_tower_data.clear();

    // Get wiping matrix to get number of extruders and convert vector<double> to vector<float>:
    std::vector<float> flush_matrix(cast<float>(m_config.flush_volumes_matrix.values));

    // BBS
    const unsigned int number_of_extruders = (unsigned int)(sqrt(flush_matrix.size()) + EPSILON);
    // Extract purging volumes for each extruder pair:
    std::vector<std::vector<float>> wipe_volumes;
    for (unsigned int i = 0; i<number_of_extruders; ++i)
        wipe_volumes.push_back(std::vector<float>(flush_matrix.begin()+i*number_of_extruders, flush_matrix.begin()+(i+1)*number_of_extruders));

    // Let the ToolOrdering class know there will be initial priming extrusions at the start of the print.
    // BBS: priming logic is removed, so don't consider it in tool ordering
    m_wipe_tower_data.tool_ordering = ToolOrdering(*this, (unsigned int)-1, false);

    // if enable_timelapse_print(), update all layer_tools parameters(has_wipe_tower, wipe_tower_partitions)
    if (enable_timelapse_print()) {
        std::vector<LayerTools>& layer_tools_array = m_wipe_tower_data.tool_ordering.layer_tools();
        for (LayerTools& layer_tools : layer_tools_array) {
            layer_tools.has_wipe_tower = true;
            if (layer_tools.wipe_tower_partitions == 0) {
                layer_tools.wipe_tower_partitions = 1;
            }
        }
    }

    if (!m_wipe_tower_data.tool_ordering.has_wipe_tower())
        // Don't generate any wipe tower.
        return;

    // Check whether there are any layers in m_tool_ordering, which are marked with has_wipe_tower,
    // they print neither object, nor support. These layers are above the raft and below the object, and they
    // shall be added to the support layers to be printed.
    // see https://github.com/prusa3d/PrusaSlicer/issues/607
    {
        size_t idx_begin = size_t(-1);
        size_t idx_end   = m_wipe_tower_data.tool_ordering.layer_tools().size();
        // Find the first wipe tower layer, which does not have a counterpart in an object or a support layer.
        for (size_t i = 0; i < idx_end; ++ i) {
            const LayerTools &lt = m_wipe_tower_data.tool_ordering.layer_tools()[i];
            if (lt.has_wipe_tower && ! lt.has_object && ! lt.has_support) {
                idx_begin = i;
                break;
            }
        }
        if (idx_begin != size_t(-1)) {
            // Find the position in m_objects.first()->support_layers to insert these new support layers.
            double wipe_tower_new_layer_print_z_first = m_wipe_tower_data.tool_ordering.layer_tools()[idx_begin].print_z;
            auto it_layer = m_objects.front()->support_layers().begin();
            auto it_end   = m_objects.front()->support_layers().end();
            for (; it_layer != it_end && (*it_layer)->print_z - EPSILON < wipe_tower_new_layer_print_z_first; ++ it_layer);
            // Find the stopper of the sequence of wipe tower layers, which do not have a counterpart in an object or a support layer.
            for (size_t i = idx_begin; i < idx_end; ++ i) {
                LayerTools &lt = const_cast<LayerTools&>(m_wipe_tower_data.tool_ordering.layer_tools()[i]);
                if (! (lt.has_wipe_tower && ! lt.has_object && ! lt.has_support))
                    break;
                lt.has_support = true;
                // Insert the new support layer.
                double height    = lt.print_z - (i == 0 ? 0. : m_wipe_tower_data.tool_ordering.layer_tools()[i-1].print_z);
                //FIXME the support layer ID is set to -1, as Vojtech hopes it is not being used anyway.
                it_layer = m_objects.front()->insert_support_layer(it_layer, -1, 0, height, lt.print_z, lt.print_z - 0.5 * height);
                ++ it_layer;
            }
        }
    }
    this->throw_if_canceled();

    // Initialize the wipe tower.
    // BBS: in BBL machine, wipe tower is only use to prime extruder. So just use a global wipe volume.
    WipeTower wipe_tower(m_config, m_plate_index, m_origin, m_config.prime_volume, m_wipe_tower_data.tool_ordering.first_extruder(),
        m_wipe_tower_data.tool_ordering.empty() ? 0.f : m_wipe_tower_data.tool_ordering.back().print_z);

    //wipe_tower.set_retract();
    //wipe_tower.set_zhop();

    // Set the extruder & material properties at the wipe tower object.
    for (size_t i = 0; i < number_of_extruders; ++ i)
        wipe_tower.set_extruder(i, m_config);

    // BBS: remove priming logic
    //m_wipe_tower_data.priming = Slic3r::make_unique<std::vector<WipeTower::ToolChangeResult>>(
    //    wipe_tower.prime((float)this->skirt_first_layer_height(), m_wipe_tower_data.tool_ordering.all_extruders(), false));

    // Lets go through the wipe tower layers and determine pairs of extruder changes for each
    // to pass to wipe_tower (so that it can use it for planning the layout of the tower)
    {
        // BBS: priming logic is removed, so get the initial extruder by first_extruder()
        unsigned int current_extruder_id = m_wipe_tower_data.tool_ordering.first_extruder();
        for (auto &layer_tools : m_wipe_tower_data.tool_ordering.layer_tools()) { // for all layers
            if (!layer_tools.has_wipe_tower) continue;
            bool first_layer = &layer_tools == &m_wipe_tower_data.tool_ordering.front();
            wipe_tower.plan_toolchange((float)layer_tools.print_z, (float)layer_tools.wipe_tower_layer_height, current_extruder_id, current_extruder_id, false);
            for (const auto extruder_id : layer_tools.extruders) {
                // BBS: priming logic is removed, so no need to do toolchange for first extruder
                if (/*(first_layer && extruder_id == m_wipe_tower_data.tool_ordering.all_extruders().back()) || */extruder_id != current_extruder_id) {
                    float volume_to_purge = wipe_volumes[current_extruder_id][extruder_id];

                    // Not all of that can be used for infill purging:
                    //volume_to_purge -= (float)m_config.filament_minimal_purge_on_wipe_tower.get_at(extruder_id);
                    volume_to_purge -= (float)m_config.nozzle_volume;

                    // try to assign some infills/objects for the wiping:
                    volume_to_purge = layer_tools.wiping_extrusions().mark_wiping_extrusions(*this, current_extruder_id, extruder_id, volume_to_purge);

                    // add back the minimal amount toforce on the wipe tower:
                    //volume_to_purge += (float)m_config.filament_minimal_purge_on_wipe_tower.get_at(extruder_id);
                    volume_to_purge += (float)m_config.nozzle_volume;

                    // request a toolchange at the wipe tower with at least volume_to_wipe purging amount
                    wipe_tower.plan_toolchange((float)layer_tools.print_z, (float)layer_tools.wipe_tower_layer_height,
                                               current_extruder_id, extruder_id, m_config.prime_volume, volume_to_purge);
                    current_extruder_id = extruder_id;
                }
            }
            layer_tools.wiping_extrusions().ensure_perimeters_infills_order(*this);

            // if enable timelapse, slice all layer
            if (enable_timelapse_print())
                continue;

            if (&layer_tools == &m_wipe_tower_data.tool_ordering.back() || (&layer_tools + 1)->wipe_tower_partitions == 0)
                break;
        }
    }

    // Generate the wipe tower layers.
    m_wipe_tower_data.tool_changes.reserve(m_wipe_tower_data.tool_ordering.layer_tools().size());
    wipe_tower.generate(m_wipe_tower_data.tool_changes);
    m_wipe_tower_data.depth = wipe_tower.get_depth();
    m_wipe_tower_data.brim_width = wipe_tower.get_brim_width();

    // Unload the current filament over the purge tower.
    coordf_t layer_height = m_objects.front()->config().layer_height.value;
    if (m_wipe_tower_data.tool_ordering.back().wipe_tower_partitions > 0) {
        // The wipe tower goes up to the last layer of the print.
        if (wipe_tower.layer_finished()) {
            // The wipe tower is printed to the top of the print and it has no space left for the final extruder purge.
            // Lift Z to the next layer.
            wipe_tower.set_layer(float(m_wipe_tower_data.tool_ordering.back().print_z + layer_height), float(layer_height), 0, false, true);
        } else {
            // There is yet enough space at this layer of the wipe tower for the final purge.
        }
    } else {
        // The wipe tower does not reach the last print layer, perform the pruge at the last print layer.
        assert(m_wipe_tower_data.tool_ordering.back().wipe_tower_partitions == 0);
        wipe_tower.set_layer(float(m_wipe_tower_data.tool_ordering.back().print_z), float(layer_height), 0, false, true);
    }
    m_wipe_tower_data.final_purge = Slic3r::make_unique<WipeTower::ToolChangeResult>(
        wipe_tower.tool_change((unsigned int)(-1)));

    m_wipe_tower_data.used_filament = wipe_tower.get_used_filament();
    m_wipe_tower_data.number_of_toolchanges = wipe_tower.get_number_of_toolchanges();
}

// Generate a recommended G-code output file name based on the format template, default extension, and template parameters
// (timestamps, object placeholders derived from the model, current placeholder prameters and print statistics.
// Use the final print statistics if available, or just keep the print statistics placeholders if not available yet (before G-code is finalized).
std::string Print::output_filename(const std::string &filename_base) const
{
    // Set the placeholders for the data know first after the G-code export is finished.
    // These values will be just propagated into the output file name.
    DynamicConfig config = this->finished() ? this->print_statistics().config() : this->print_statistics().placeholders();
    config.set_key_value("num_filaments", new ConfigOptionInt((int)m_config.nozzle_diameter.size()));
    return this->PrintBase::output_filename(m_config.filename_format.value, ".gcode", filename_base, &config);
}

//BBS: add gcode file preload logic
void Print::set_gcode_file_ready()
{
    this->set_started(psGCodeExport);
	this->set_done(psGCodeExport);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ <<  boost::format(": done");
}
//BBS: add gcode file preload logic
void Print::set_gcode_file_invalidated()
{
    this->invalidate_step(psGCodeExport);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ <<  boost::format(": done");
}

//BBS: add gcode file preload logic
void Print::export_gcode_from_previous_file(const std::string& file, GCodeProcessorResult* result, ThumbnailsGeneratorCallback thumbnail_cb)
{
    try {
        GCodeProcessor processor;
        const Vec3d origin = this->get_plate_origin();
        processor.set_xy_offset(origin(0), origin(1));
        //processor.enable_producers(true);
        processor.process_file(file);

        *result = std::move(processor.extract_result());
    } catch (std::exception & /* ex */) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ <<  boost::format(": found errors when process gcode file %1%") %file.c_str();
        throw Slic3r::RuntimeError(
            std::string("Failed to process the G-code file ") + file + " from previous 3mf\n");
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ <<  boost::format(":  process the G-code file %1% successfully")%file.c_str();
}

DynamicConfig PrintStatistics::config() const
{
    DynamicConfig config;
    std::string normal_print_time = short_time(this->estimated_normal_print_time);
    std::string silent_print_time = short_time(this->estimated_silent_print_time);
    config.set_key_value("print_time", new ConfigOptionString(normal_print_time));
    config.set_key_value("normal_print_time", new ConfigOptionString(normal_print_time));
    config.set_key_value("silent_print_time", new ConfigOptionString(silent_print_time));
    config.set_key_value("used_filament",             new ConfigOptionFloat(this->total_used_filament / 1000.));
    config.set_key_value("extruded_volume",           new ConfigOptionFloat(this->total_extruded_volume));
    config.set_key_value("total_cost",                new ConfigOptionFloat(this->total_cost));
    config.set_key_value("total_toolchanges",         new ConfigOptionInt(this->total_toolchanges));
    config.set_key_value("total_weight",              new ConfigOptionFloat(this->total_weight));
    config.set_key_value("total_wipe_tower_cost",     new ConfigOptionFloat(this->total_wipe_tower_cost));
    config.set_key_value("total_wipe_tower_filament", new ConfigOptionFloat(this->total_wipe_tower_filament));
    return config;
}

DynamicConfig PrintStatistics::placeholders()
{
    DynamicConfig config;
    for (const std::string &key : {
        "print_time", "normal_print_time", "silent_print_time",
        "used_filament", "extruded_volume", "total_cost", "total_weight",
        "total_toolchanges", "total_wipe_tower_cost", "total_wipe_tower_filament"})
        config.set_key_value(key, new ConfigOptionString(std::string("{") + key + "}"));
    return config;
}

std::string PrintStatistics::finalize_output_path(const std::string &path_in) const
{
    std::string final_path;
    try {
        boost::filesystem::path path(path_in);
        DynamicConfig cfg = this->config();
        PlaceholderParser pp;
        std::string new_stem = pp.process(path.stem().string(), 0, &cfg);
        final_path = (path.parent_path() / (new_stem + path.extension().string())).string();
    } catch (const std::exception &ex) {
        BOOST_LOG_TRIVIAL(error) << "Failed to apply the print statistics to the export file name: " << ex.what();
        final_path = path_in;
    }
    return final_path;
}

} // namespace Slic3r
