#include "Print.hpp"
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "Extruder.hpp"
#include "Flow.hpp"
#include "Geometry.hpp"
#include "I18N.hpp"
#include "SupportMaterial.hpp"
#include "GCode.hpp"
#include "GCode/WipeTowerPrusaMM.hpp"
#include "Utils.hpp"

//#include "PrintExport.hpp"

#include <float.h>

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <boost/filesystem/path.hpp>
#include <boost/log/trivial.hpp>

//! macro used to mark string used at localization, 
//! return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

template class PrintState<PrintStep, psCount>;
template class PrintState<PrintObjectStep, posCount>;

void Print::clear() 
{
	tbb::mutex::scoped_lock lock(this->state_mutex());
    // The following call should stop background processing if it is running.
    this->invalidate_all_steps();
	for (PrintObject *object : m_objects)
		delete object;
	m_objects.clear();
    for (PrintRegion *region : m_regions)
        delete region;
    m_regions.clear();
    m_model.clear_objects();
}

PrintRegion* Print::add_region()
{
    m_regions.emplace_back(new PrintRegion(this));
    return m_regions.back();
}

PrintRegion* Print::add_region(const PrintRegionConfig &config)
{
    m_regions.emplace_back(new PrintRegion(this, config));
    return m_regions.back();
}

// Called by Print::apply().
// This method only accepts PrintConfig option keys.
bool Print::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys)
{
    if (opt_keys.empty())
        return false;

    // Cache the plenty of parameters, which influence the G-code generator only,
    // or they are only notes not influencing the generated G-code.
    static std::unordered_set<std::string> steps_gcode = {
        "avoid_crossing_perimeters",
        "bed_shape",
        "bed_temperature",
        "before_layer_gcode",
        "between_objects_gcode",
        "bridge_acceleration",
        "bridge_fan_speed",
        "colorprint_heights",
        "cooling",
        "default_acceleration",
        "deretract_speed",
        "disable_fan_first_layers",
        "duplicate_distance",
        "end_gcode",
        "end_filament_gcode",
        "extrusion_axis",
        "extruder_clearance_height",
        "extruder_clearance_radius",
        "extruder_colour",
        "extruder_offset",
        "extrusion_multiplier",
        "fan_always_on",
        "fan_below_layer_time",
        "filament_colour",
        "filament_diameter",
        "filament_density",
        "filament_notes",
        "filament_cost",
        "filament_max_volumetric_speed",
        "first_layer_acceleration",
        "first_layer_bed_temperature",
        "first_layer_speed",
        "gcode_comments",
        "gcode_label_objects",
        "infill_acceleration",
        "layer_gcode",
        "min_fan_speed",
        "max_fan_speed",
        "max_print_height",
        "min_print_speed",
        "max_print_speed",
        "max_volumetric_speed",
#ifdef HAS_PRESSURE_EQUALIZER
        "max_volumetric_extrusion_rate_slope_positive",
        "max_volumetric_extrusion_rate_slope_negative",
#endif /* HAS_PRESSURE_EQUALIZER */
        "notes",
        "only_retract_when_crossing_perimeters",
        "output_filename_format",
        "perimeter_acceleration",
        "post_process",
        "printer_notes",
        "retract_before_travel",
        "retract_before_wipe",
        "retract_layer_change",
        "retract_length",
        "retract_length_toolchange",
        "retract_lift",
        "retract_lift_above",
        "retract_lift_below",
        "retract_restart_extra",
        "retract_restart_extra_toolchange",
        "retract_speed",
        "single_extruder_multi_material_priming",
        "slowdown_below_layer_time",
        "standby_temperature_delta",
        "start_gcode",
        "start_filament_gcode",
        "toolchange_gcode",
        "threads",
        "travel_speed",
        "use_firmware_retraction",
        "use_relative_e_distances",
        "use_volumetric_e",
        "variable_layer_height",
        "wipe",
        "wipe_tower_x",
        "wipe_tower_y",
        "wipe_tower_rotation_angle"
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
               opt_key == "skirts"
            || opt_key == "skirt_height"
            || opt_key == "skirt_distance"
            || opt_key == "min_skirt_length"
            || opt_key == "ooze_prevention") {
            steps.emplace_back(psSkirt);
        } else if (opt_key == "brim_width") {
            steps.emplace_back(psBrim);
            steps.emplace_back(psSkirt);
        } else if (
               opt_key == "nozzle_diameter"
            || opt_key == "resolution") {
            osteps.emplace_back(posSlice);
        } else if (
               opt_key == "complete_objects"
            || opt_key == "filament_type"
            || opt_key == "filament_soluble"
            || opt_key == "first_layer_temperature"
            || opt_key == "filament_loading_speed"
            || opt_key == "filament_loading_speed_start"
            || opt_key == "filament_unloading_speed"
            || opt_key == "filament_unloading_speed_start"
            || opt_key == "filament_toolchange_delay"
            || opt_key == "filament_cooling_moves"
            || opt_key == "filament_minimal_purge_on_wipe_tower"
            || opt_key == "filament_cooling_initial_speed"
            || opt_key == "filament_cooling_final_speed"
            || opt_key == "filament_ramming_parameters"
            || opt_key == "gcode_flavor"
            || opt_key == "high_current_on_filament_swap"
            || opt_key == "infill_first"
            || opt_key == "single_extruder_multi_material"
            || opt_key == "spiral_vase"
            || opt_key == "temperature"
            || opt_key == "wipe_tower"
            || opt_key == "wipe_tower_width"
            || opt_key == "wipe_tower_bridging"
            || opt_key == "wiping_volumes_matrix"
            || opt_key == "parking_pos_retraction"
            || opt_key == "cooling_tube_retraction"
            || opt_key == "cooling_tube_length"
            || opt_key == "extra_loading_move"
            || opt_key == "z_offset") {
            steps.emplace_back(psWipeTower);
        } else if (
               opt_key == "first_layer_extrusion_width" 
            || opt_key == "min_layer_height"
            || opt_key == "max_layer_height") {
            osteps.emplace_back(posPerimeters);
            osteps.emplace_back(posInfill);
            osteps.emplace_back(posSupportMaterial);
            steps.emplace_back(psSkirt);
            steps.emplace_back(psBrim);
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
    //FIXME Why should skirt invalidate brim? Shouldn't it be vice versa?
    if (step == psSkirt)
		invalidated |= Inherited::invalidate_step(psBrim);
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
	tbb::mutex::scoped_lock lock(this->state_mutex());
    for (const PrintObject *object : m_objects)
        if (! object->is_step_done_unguarded(step))
            return false;
    return true;
}

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::object_extruders() const
{
    std::vector<unsigned int> extruders;
    extruders.reserve(m_regions.size() * 3);
    for (const PrintRegion *region : m_regions)
        region->collect_object_printing_extruders(extruders);
    sort_remove_duplicates(extruders);
    return extruders;
}

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::support_material_extruders() const
{
    std::vector<unsigned int> extruders;
    bool support_uses_current_extruder = false;

    for (PrintObject *object : m_objects) {
        if (object->has_support_material()) {
            if (object->config().support_material_extruder == 0)
                support_uses_current_extruder = true;
            else
                extruders.push_back(object->config().support_material_extruder - 1);
            if (object->config().support_material_interface_extruder == 0)
                support_uses_current_extruder = true;
            else
                extruders.push_back(object->config().support_material_interface_extruder - 1);
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
        instances += (unsigned int)print_object->copies().size();
    return instances;
}

void Print::_simplify_slices(double distance)
{
    for (PrintObject *object : m_objects) {
        for (Layer *layer : object->m_layers) {
            layer->slices.simplify(distance);
            for (LayerRegion *layerm : layer->regions())
                layerm->slices.simplify(distance);
        }
    }
}

double Print::max_allowed_layer_height() const
{
    double nozzle_diameter_max = 0.;
    for (unsigned int extruder_id : this->extruders())
        nozzle_diameter_max = std::max(nozzle_diameter_max, m_config.nozzle_diameter.get_at(extruder_id));
    return nozzle_diameter_max;
}

// Add or remove support modifier ModelVolumes from model_object_dst to match the ModelVolumes of model_object_new
// in the exact order and with the same IDs.
// It is expected, that the model_object_dst already contains the non-support volumes of model_object_new in the correct order.
void Print::model_volume_list_update_supports(ModelObject &model_object_dst, const ModelObject &model_object_new)
{
	typedef std::pair<const ModelVolume*, bool> ModelVolumeWithStatus;
	std::vector<ModelVolumeWithStatus> old_volumes;
    old_volumes.reserve(model_object_dst.volumes.size());
	for (const ModelVolume *model_volume : model_object_dst.volumes)
		old_volumes.emplace_back(ModelVolumeWithStatus(model_volume, false));
	auto model_volume_lower = [](const ModelVolumeWithStatus &mv1, const ModelVolumeWithStatus &mv2){ return mv1.first->id() <  mv2.first->id(); };
	auto model_volume_equal = [](const ModelVolumeWithStatus &mv1, const ModelVolumeWithStatus &mv2){ return mv1.first->id() == mv2.first->id(); };
    std::sort(old_volumes.begin(), old_volumes.end(), model_volume_lower);
    model_object_dst.volumes.clear();
    model_object_dst.volumes.reserve(model_object_new.volumes.size());
    for (const ModelVolume *model_volume_src : model_object_new.volumes) {
		ModelVolumeWithStatus key(model_volume_src, false);
		auto it = std::lower_bound(old_volumes.begin(), old_volumes.end(), key, model_volume_lower);
		if (it != old_volumes.end() && model_volume_equal(*it, key)) {
            // The volume was found in the old list. Just copy it.
            assert(! it->second); // not consumed yet
            it->second = true;
            ModelVolume *model_volume_dst = const_cast<ModelVolume*>(it->first);
			// For support modifiers, the type may have been switched from blocker to enforcer and vice versa.
			assert((model_volume_dst->is_support_modifier() && model_volume_src->is_support_modifier()) || model_volume_dst->type() == model_volume_src->type());
            model_object_dst.volumes.emplace_back(model_volume_dst);
			if (model_volume_dst->is_support_modifier()) {
				// For support modifiers, the type may have been switched from blocker to enforcer and vice versa.
				model_volume_dst->set_type(model_volume_src->type());
				model_volume_dst->set_transformation(model_volume_src->get_transformation());
			}
            assert(model_volume_dst->get_matrix().isApprox(model_volume_src->get_matrix()));
        } else {
            // The volume was not found in the old list. Create a new copy.
            assert(model_volume_src->is_support_modifier());
            model_object_dst.volumes.emplace_back(new ModelVolume(*model_volume_src));
            model_object_dst.volumes.back()->set_model_object(&model_object_dst);
        }
    }
    // Release the non-consumed old volumes (those were deleted from the new list).
	for (ModelVolumeWithStatus &mv_with_status : old_volumes)
        if (! mv_with_status.second)
            delete mv_with_status.first;
}

static inline void model_volume_list_copy_configs(ModelObject &model_object_dst, const ModelObject &model_object_src, const ModelVolumeType type)
{
    size_t i_src, i_dst;
    for (i_src = 0, i_dst = 0; i_src < model_object_src.volumes.size() && i_dst < model_object_dst.volumes.size();) {
        const ModelVolume &mv_src = *model_object_src.volumes[i_src];
        ModelVolume       &mv_dst = *model_object_dst.volumes[i_dst];
        if (mv_src.type() != type) {
            ++ i_src;
            continue;
        }
        if (mv_dst.type() != type) {
            ++ i_dst;
            continue;
        }
        assert(mv_src.id() == mv_dst.id());
        // Copy the ModelVolume data.
        mv_dst.name   = mv_src.name;
		static_cast<DynamicPrintConfig&>(mv_dst.config) = static_cast<const DynamicPrintConfig&>(mv_src.config);
        //FIXME what to do with the materials?
        // mv_dst.m_material_id = mv_src.m_material_id;
        ++ i_src;
        ++ i_dst;
    }
}

static inline void layer_height_ranges_copy_configs(t_layer_config_ranges &lr_dst, const t_layer_config_ranges &lr_src)
{
    assert(lr_dst.size() == lr_src.size());
    auto it_src = lr_src.cbegin();
    for (auto &kvp_dst : lr_dst) {
        const auto &kvp_src = *it_src ++;
        assert(std::abs(kvp_dst.first.first  - kvp_src.first.first ) <= EPSILON);
        assert(std::abs(kvp_dst.first.second - kvp_src.first.second) <= EPSILON);
        // Layer heights are allowed do differ in case the layer height table is being overriden by the smooth profile.
        // assert(std::abs(kvp_dst.second.option("layer_height")->getFloat() - kvp_src.second.option("layer_height")->getFloat()) <= EPSILON);
        kvp_dst.second = kvp_src.second;
    }
}

static inline bool transform3d_lower(const Transform3d &lhs, const Transform3d &rhs) 
{
    typedef Transform3d::Scalar T;
    const T *lv = lhs.data();
    const T *rv = rhs.data();
    for (size_t i = 0; i < 16; ++ i, ++ lv, ++ rv) {
        if (*lv < *rv)
            return true;
        else if (*lv > *rv)
            return false;
    }
    return false;
}

static inline bool transform3d_equal(const Transform3d &lhs, const Transform3d &rhs) 
{
    typedef Transform3d::Scalar T;
    const T *lv = lhs.data();
    const T *rv = rhs.data();
    for (size_t i = 0; i < 16; ++ i, ++ lv, ++ rv)
        if (*lv != *rv)
            return false;
    return true;
}

struct PrintInstances
{
    Transform3d     trafo;
    Points          copies;
    bool operator<(const PrintInstances &rhs) const { return transform3d_lower(this->trafo, rhs.trafo); }
};

// Generate a list of trafos and XY offsets for instances of a ModelObject
static std::vector<PrintInstances> print_objects_from_model_object(const ModelObject &model_object)
{
    std::set<PrintInstances> trafos;
    PrintInstances           trafo;
    trafo.copies.assign(1, Point());
    for (ModelInstance *model_instance : model_object.instances)
        if (model_instance->is_printable()) {
            trafo.trafo = model_instance->get_matrix();
            // Set the Z axis of the transformation.
            trafo.copies.front() = Point::new_scale(trafo.trafo.data()[12], trafo.trafo.data()[13]);
            trafo.trafo.data()[12] = 0;
            trafo.trafo.data()[13] = 0;
            auto it = trafos.find(trafo);
            if (it == trafos.end())
                trafos.emplace(trafo);
            else
                const_cast<PrintInstances&>(*it).copies.emplace_back(trafo.copies.front());
        }
    return std::vector<PrintInstances>(trafos.begin(), trafos.end());
}

// Compare just the layer ranges and their layer heights, not the associated configs.
// Ignore the layer heights if check_layer_heights is false.
bool layer_height_ranges_equal(const t_layer_config_ranges &lr1, const t_layer_config_ranges &lr2, bool check_layer_height)
{
    if (lr1.size() != lr2.size())
        return false;
    auto it2 = lr2.begin();
    for (const auto &kvp1 : lr1) {
        const auto &kvp2 = *it2 ++;
        if (std::abs(kvp1.first.first  - kvp2.first.first ) > EPSILON ||
            std::abs(kvp1.first.second - kvp2.first.second) > EPSILON ||
            (check_layer_height && std::abs(kvp1.second.option("layer_height")->getFloat() - kvp2.second.option("layer_height")->getFloat()) > EPSILON))
            return false;
    }
    return true;
}

Print::ApplyStatus Print::apply(const Model &model, const DynamicPrintConfig &config_in)
{
#ifdef _DEBUG
    check_model_ids_validity(model);
#endif /* _DEBUG */

    // Make a copy of the config, normalize it.
    DynamicPrintConfig config(config_in);
	config.option("print_settings_id",    true);
	config.option("filament_settings_id", true);
	config.option("printer_settings_id",  true);
    config.normalize();
    // Collect changes to print config.
    t_config_option_keys print_diff  = m_config.diff(config);
    t_config_option_keys object_diff = m_default_object_config.diff(config);
    t_config_option_keys region_diff = m_default_region_config.diff(config);
    t_config_option_keys placeholder_parser_diff = this->placeholder_parser().config_diff(config);

    // Do not use the ApplyStatus as we will use the max function when updating apply_status. 
    unsigned int apply_status = APPLY_STATUS_UNCHANGED;
    auto update_apply_status = [&apply_status](bool invalidated)
        { apply_status = std::max<unsigned int>(apply_status, invalidated ? APPLY_STATUS_INVALIDATED : APPLY_STATUS_CHANGED); };
    if (! (print_diff.empty() && object_diff.empty() && region_diff.empty()))
        update_apply_status(false);

    // Grab the lock for the Print / PrintObject milestones.
	tbb::mutex::scoped_lock lock(this->state_mutex());

    // The following call may stop the background processing.
    if (! print_diff.empty())
        update_apply_status(this->invalidate_state_by_config_options(print_diff));
    // Apply variables to placeholder parser. The placeholder parser is used by G-code export,
    // which should be stopped if print_diff is not empty.
	if (! placeholder_parser_diff.empty()) {
        update_apply_status(this->invalidate_step(psGCodeExport));
		PlaceholderParser &pp = this->placeholder_parser();
		pp.apply_only(config, placeholder_parser_diff);
        // Set the profile aliases for the PrintBase::output_filename()
		pp.set("print_preset",    config.option("print_settings_id")->clone());
		pp.set("filament_preset", config.option("filament_settings_id")->clone());
		pp.set("printer_preset",  config.option("printer_settings_id")->clone());
    }

    // It is also safe to change m_config now after this->invalidate_state_by_config_options() call.
    m_config.apply_only(config, print_diff, true);
    // Handle changes to object config defaults
    m_default_object_config.apply_only(config, object_diff, true);
    // Handle changes to regions config defaults
    m_default_region_config.apply_only(config, region_diff, true);
    
    class LayerRanges
    {
    public:
        LayerRanges() {}
        // Convert input config ranges into continuous non-overlapping sorted vector of intervals and their configs.
        void assign(const t_layer_config_ranges &in) {
            m_ranges.clear();
            m_ranges.reserve(in.size());
            // Input ranges are sorted lexicographically. First range trims the other ranges.
            coordf_t last_z = 0;
            for (const std::pair<const t_layer_height_range, DynamicPrintConfig> &range : in) {
//            for (auto &range : in) {
			if (range.first.second > last_z) {
                    coordf_t min_z = std::max(range.first.first, 0.);
                    if (min_z > last_z + EPSILON) {
                        m_ranges.emplace_back(t_layer_height_range(last_z, min_z), nullptr);
                        last_z = min_z;
                    }
                    if (range.first.second > last_z + EPSILON) {
						const DynamicPrintConfig* cfg = &range.second;
                        m_ranges.emplace_back(t_layer_height_range(last_z, range.first.second), cfg);
                        last_z = range.first.second;
                    }
                }
            }
            if (m_ranges.empty())
                m_ranges.emplace_back(t_layer_height_range(0, DBL_MAX), nullptr);
            else if (m_ranges.back().second == nullptr)
                m_ranges.back().first.second = DBL_MAX;
            else
                m_ranges.emplace_back(t_layer_height_range(m_ranges.back().first.second, DBL_MAX), nullptr);
        }
        const DynamicPrintConfig* config(const t_layer_height_range &range) const {
            auto it = std::lower_bound(m_ranges.begin(), m_ranges.end(), std::make_pair< t_layer_height_range, const DynamicPrintConfig*>(t_layer_height_range(range.first - EPSILON, range.second - EPSILON), nullptr));
            assert(it != m_ranges.end());
            assert(it == m_ranges.end() || std::abs(it->first.first  - range.first ) < EPSILON);
            assert(it == m_ranges.end() || std::abs(it->first.second - range.second) < EPSILON);
            return (it == m_ranges.end()) ? nullptr : it->second;
        }
        std::vector<std::pair<t_layer_height_range, const DynamicPrintConfig*>>::const_iterator begin() const { return m_ranges.cbegin(); }
        std::vector<std::pair<t_layer_height_range, const DynamicPrintConfig*>>::const_iterator end() const { return m_ranges.cend(); }
    private:
        std::vector<std::pair<t_layer_height_range, const DynamicPrintConfig*>> m_ranges;
    };
    struct ModelObjectStatus {
        enum Status {
            Unknown,
            Old,
            New,
            Moved,
            Deleted,
        };
        ModelObjectStatus(ObjectID id, Status status = Unknown) : id(id), status(status) {}
		ObjectID     id;
        Status       status;
        LayerRanges  layer_ranges;
        // Search by id.
        bool operator<(const ModelObjectStatus &rhs) const { return id < rhs.id; }
    };
    std::set<ModelObjectStatus> model_object_status;

    // 1) Synchronize model objects.
    if (model.id() != m_model.id()) {
        // Kill everything, initialize from scratch.
        // Stop background processing.
        this->call_cancel_callback();
        update_apply_status(this->invalidate_all_steps());
        for (PrintObject *object : m_objects) {
            model_object_status.emplace(object->model_object()->id(), ModelObjectStatus::Deleted);
			update_apply_status(object->invalidate_all_steps());
			delete object;
        }
        m_objects.clear();
        for (PrintRegion *region : m_regions)
            delete region;
        m_regions.clear();
        m_model.assign_copy(model);
		for (const ModelObject *model_object : m_model.objects)
			model_object_status.emplace(model_object->id(), ModelObjectStatus::New);
    } else {
        if (model_object_list_equal(m_model, model)) {
            // The object list did not change.
			for (const ModelObject *model_object : m_model.objects)
				model_object_status.emplace(model_object->id(), ModelObjectStatus::Old);
        } else if (model_object_list_extended(m_model, model)) {
            // Add new objects. Their volumes and configs will be synchronized later.
            update_apply_status(this->invalidate_step(psGCodeExport));
            for (const ModelObject *model_object : m_model.objects)
                model_object_status.emplace(model_object->id(), ModelObjectStatus::Old);
            for (size_t i = m_model.objects.size(); i < model.objects.size(); ++ i) {
                model_object_status.emplace(model.objects[i]->id(), ModelObjectStatus::New);
                m_model.objects.emplace_back(ModelObject::new_copy(*model.objects[i]));
				m_model.objects.back()->set_model(&m_model);
            }
        } else {
            // Reorder the objects, add new objects.
            // First stop background processing before shuffling or deleting the PrintObjects in the object list.
            this->call_cancel_callback();
            update_apply_status(this->invalidate_step(psGCodeExport));
            // Second create a new list of objects.
            std::vector<ModelObject*> model_objects_old(std::move(m_model.objects));
            m_model.objects.clear();
            m_model.objects.reserve(model.objects.size());
            auto by_id_lower = [](const ModelObject *lhs, const ModelObject *rhs){ return lhs->id() < rhs->id(); };
            std::sort(model_objects_old.begin(), model_objects_old.end(), by_id_lower);
            for (const ModelObject *mobj : model.objects) {
                auto it = std::lower_bound(model_objects_old.begin(), model_objects_old.end(), mobj, by_id_lower);
                if (it == model_objects_old.end() || (*it)->id() != mobj->id()) {
                    // New ModelObject added.
					m_model.objects.emplace_back(ModelObject::new_copy(*mobj));
					m_model.objects.back()->set_model(&m_model);
                    model_object_status.emplace(mobj->id(), ModelObjectStatus::New);
                } else {
                    // Existing ModelObject re-added (possibly moved in the list).
                    m_model.objects.emplace_back(*it);
                    model_object_status.emplace(mobj->id(), ModelObjectStatus::Moved);
                }
            }
            bool deleted_any = false;
			for (ModelObject *&model_object : model_objects_old) {
                if (model_object_status.find(ModelObjectStatus(model_object->id())) == model_object_status.end()) {
                    model_object_status.emplace(model_object->id(), ModelObjectStatus::Deleted);
                    deleted_any = true;
                } else
                    // Do not delete this ModelObject instance.
                    model_object = nullptr;
            }
            if (deleted_any) {
                // Delete PrintObjects of the deleted ModelObjects.
                std::vector<PrintObject*> print_objects_old = std::move(m_objects);
                m_objects.clear();
                m_objects.reserve(print_objects_old.size());
                for (PrintObject *print_object : print_objects_old) {
                    auto it_status = model_object_status.find(ModelObjectStatus(print_object->model_object()->id()));
                    assert(it_status != model_object_status.end());
                    if (it_status->status == ModelObjectStatus::Deleted) {
                        update_apply_status(print_object->invalidate_all_steps());
                        delete print_object;
                    } else
                        m_objects.emplace_back(print_object);
                }
                for (ModelObject *model_object : model_objects_old)
                    delete model_object;
            }
        }
    }

    // 2) Map print objects including their transformation matrices.
    struct PrintObjectStatus {
        enum Status {
            Unknown,
            Deleted,
            Reused,
            New
        };
        PrintObjectStatus(PrintObject *print_object, Status status = Unknown) : 
            id(print_object->model_object()->id()),
            print_object(print_object),
            trafo(print_object->trafo()),
            status(status) {}
        PrintObjectStatus(ObjectID id) : id(id), print_object(nullptr), trafo(Transform3d::Identity()), status(Unknown) {}
        // ID of the ModelObject & PrintObject
        ObjectID          id;
        // Pointer to the old PrintObject
        PrintObject     *print_object;
        // Trafo generated with model_object->world_matrix(true) 
        Transform3d      trafo;
        Status           status;
        // Search by id.
        bool operator<(const PrintObjectStatus &rhs) const { return id < rhs.id; }
    };
    std::multiset<PrintObjectStatus> print_object_status;
    for (PrintObject *print_object : m_objects)
        print_object_status.emplace(PrintObjectStatus(print_object));

    // 3) Synchronize ModelObjects & PrintObjects.
    size_t num_extruders = m_config.nozzle_diameter.size();
    for (size_t idx_model_object = 0; idx_model_object < model.objects.size(); ++ idx_model_object) {
        ModelObject &model_object = *m_model.objects[idx_model_object];
        auto it_status = model_object_status.find(ModelObjectStatus(model_object.id()));
        assert(it_status != model_object_status.end());
        assert(it_status->status != ModelObjectStatus::Deleted);
		const ModelObject& model_object_new = *model.objects[idx_model_object];
		const_cast<ModelObjectStatus&>(*it_status).layer_ranges.assign(model_object_new.layer_config_ranges);
        if (it_status->status == ModelObjectStatus::New)
            // PrintObject instances will be added in the next loop.
            continue;
        // Update the ModelObject instance, possibly invalidate the linked PrintObjects.
        assert(it_status->status == ModelObjectStatus::Old || it_status->status == ModelObjectStatus::Moved);
        // Check whether a model part volume was added or removed, their transformations or order changed.
        // Only volume IDs, volume types and their order are checked, configuration and other parameters are NOT checked.
        bool model_parts_differ         = model_volume_list_changed(model_object, model_object_new, ModelVolumeType::MODEL_PART);
        bool modifiers_differ           = model_volume_list_changed(model_object, model_object_new, ModelVolumeType::PARAMETER_MODIFIER);
        bool support_blockers_differ    = model_volume_list_changed(model_object, model_object_new, ModelVolumeType::SUPPORT_BLOCKER);
        bool support_enforcers_differ   = model_volume_list_changed(model_object, model_object_new, ModelVolumeType::SUPPORT_ENFORCER);
        if (model_parts_differ || modifiers_differ || 
            model_object.origin_translation         != model_object_new.origin_translation   ||
            model_object.layer_height_profile       != model_object_new.layer_height_profile ||
            ! layer_height_ranges_equal(model_object.layer_config_ranges, model_object_new.layer_config_ranges, model_object_new.layer_height_profile.empty())) {
            // The very first step (the slicing step) is invalidated. One may freely remove all associated PrintObjects.
            auto range = print_object_status.equal_range(PrintObjectStatus(model_object.id()));
            for (auto it = range.first; it != range.second; ++ it) {
                update_apply_status(it->print_object->invalidate_all_steps());
                const_cast<PrintObjectStatus&>(*it).status = PrintObjectStatus::Deleted;
            }
            // Copy content of the ModelObject including its ID, do not change the parent.
            model_object.assign_copy(model_object_new);
        } else if (support_blockers_differ || support_enforcers_differ) {
            // First stop background processing before shuffling or deleting the ModelVolumes in the ModelObject's list.
            this->call_cancel_callback();
            update_apply_status(false);
            // Invalidate just the supports step.
            auto range = print_object_status.equal_range(PrintObjectStatus(model_object.id()));
            for (auto it = range.first; it != range.second; ++ it)
                update_apply_status(it->print_object->invalidate_step(posSupportMaterial));
            // Copy just the support volumes.
            model_volume_list_update_supports(model_object, model_object_new);
        }
        if (! model_parts_differ && ! modifiers_differ) {
            // Synchronize Object's config.
            bool object_config_changed = model_object.config != model_object_new.config;
			if (object_config_changed)
				static_cast<DynamicPrintConfig&>(model_object.config) = static_cast<const DynamicPrintConfig&>(model_object_new.config);
            if (! object_diff.empty() || object_config_changed) {
                PrintObjectConfig new_config = PrintObject::object_config_from_model_object(m_default_object_config, model_object, num_extruders);
                auto range = print_object_status.equal_range(PrintObjectStatus(model_object.id()));
                for (auto it = range.first; it != range.second; ++ it) {
                    t_config_option_keys diff = it->print_object->config().diff(new_config);
                    if (! diff.empty()) {
                        update_apply_status(it->print_object->invalidate_state_by_config_options(diff));
                        it->print_object->config_apply_only(new_config, diff, true);
                    }
                }
            }
            // Synchronize (just copy) the remaining data of ModelVolumes (name, config).
            //FIXME What to do with m_material_id?
			model_volume_list_copy_configs(model_object /* dst */, model_object_new /* src */, ModelVolumeType::MODEL_PART);
			model_volume_list_copy_configs(model_object /* dst */, model_object_new /* src */, ModelVolumeType::PARAMETER_MODIFIER);
            layer_height_ranges_copy_configs(model_object.layer_config_ranges /* dst */, model_object_new.layer_config_ranges /* src */);
            // Copy the ModelObject name, input_file and instances. The instances will be compared against PrintObject instances in the next step.
            model_object.name       = model_object_new.name;
            model_object.input_file = model_object_new.input_file;
            model_object.clear_instances();
            model_object.instances.reserve(model_object_new.instances.size());
            for (const ModelInstance *model_instance : model_object_new.instances) {
                model_object.instances.emplace_back(new ModelInstance(*model_instance));
                model_object.instances.back()->set_model_object(&model_object);
            }
        }
    }

    // 4) Generate PrintObjects from ModelObjects and their instances.
    {
        std::vector<PrintObject*> print_objects_new;
        print_objects_new.reserve(std::max(m_objects.size(), m_model.objects.size()));
        bool new_objects = false;
        // Walk over all new model objects and check, whether there are matching PrintObjects.
        for (ModelObject *model_object : m_model.objects) {
            auto range = print_object_status.equal_range(PrintObjectStatus(model_object->id()));
            std::vector<const PrintObjectStatus*> old;
            if (range.first != range.second) {
                old.reserve(print_object_status.count(PrintObjectStatus(model_object->id())));
                for (auto it = range.first; it != range.second; ++ it)
                    if (it->status != PrintObjectStatus::Deleted)
                        old.emplace_back(&(*it));
            }
            // Generate a list of trafos and XY offsets for instances of a ModelObject
            PrintObjectConfig config = PrintObject::object_config_from_model_object(m_default_object_config, *model_object, num_extruders);
            std::vector<PrintInstances> new_print_instances = print_objects_from_model_object(*model_object);
            if (old.empty()) {
                // Simple case, just generate new instances.
                for (const PrintInstances &print_instances : new_print_instances) {
                    PrintObject *print_object = new PrintObject(this, model_object, false);
					print_object->set_trafo(print_instances.trafo);
                    print_object->set_copies(print_instances.copies);
                    print_object->config_apply(config);
                    print_objects_new.emplace_back(print_object);
                    // print_object_status.emplace(PrintObjectStatus(print_object, PrintObjectStatus::New));
                    new_objects = true;
                }
                continue;
            }
            // Complex case, try to merge the two lists.
            // Sort the old lexicographically by their trafos.
            std::sort(old.begin(), old.end(), [](const PrintObjectStatus *lhs, const PrintObjectStatus *rhs){ return transform3d_lower(lhs->trafo, rhs->trafo); });
            // Merge the old / new lists.
            auto it_old = old.begin();
            for (const PrintInstances &new_instances : new_print_instances) {
				for (; it_old != old.end() && transform3d_lower((*it_old)->trafo, new_instances.trafo); ++ it_old);
				if (it_old == old.end() || ! transform3d_equal((*it_old)->trafo, new_instances.trafo)) {
                    // This is a new instance (or a set of instances with the same trafo). Just add it.
                    PrintObject *print_object = new PrintObject(this, model_object, false);
                    print_object->set_trafo(new_instances.trafo);
                    print_object->set_copies(new_instances.copies);
                    print_object->config_apply(config);
                    print_objects_new.emplace_back(print_object);
                    // print_object_status.emplace(PrintObjectStatus(print_object, PrintObjectStatus::New));
                    new_objects = true;
                    if (it_old != old.end())
                        const_cast<PrintObjectStatus*>(*it_old)->status = PrintObjectStatus::Deleted;
                } else {
                    // The PrintObject already exists and the copies differ.
					PrintBase::ApplyStatus status = (*it_old)->print_object->set_copies(new_instances.copies);
                    if (status != PrintBase::APPLY_STATUS_UNCHANGED)
						update_apply_status(status == PrintBase::APPLY_STATUS_INVALIDATED);
					print_objects_new.emplace_back((*it_old)->print_object);
					const_cast<PrintObjectStatus*>(*it_old)->status = PrintObjectStatus::Reused;
				}
            }
        }
        if (m_objects != print_objects_new) {
            this->call_cancel_callback();
			update_apply_status(this->invalidate_all_steps());
            m_objects = print_objects_new;
            // Delete the PrintObjects marked as Unknown or Deleted.
            bool deleted_objects = false;
            for (auto &pos : print_object_status)
                if (pos.status == PrintObjectStatus::Unknown || pos.status == PrintObjectStatus::Deleted) {
                    update_apply_status(pos.print_object->invalidate_all_steps());
                    delete pos.print_object;
					deleted_objects = true;
                }
			if (new_objects || deleted_objects)
				update_apply_status(this->invalidate_steps({ psSkirt, psBrim, psWipeTower, psGCodeExport }));
			if (new_objects)
	            update_apply_status(false);
        }
        print_object_status.clear();
    }

    // 5) Synchronize configs of ModelVolumes, synchronize AMF / 3MF materials (and their configs), refresh PrintRegions.
    // Update reference counts of regions from the remaining PrintObjects and their volumes.
    // Regions with zero references could and should be reused.
    for (PrintRegion *region : m_regions)
        region->m_refcnt = 0;
    for (PrintObject *print_object : m_objects) {
        int idx_region = 0;
        for (const auto &volumes : print_object->region_volumes) {
            if (! volumes.empty())
				++ m_regions[idx_region]->m_refcnt;
            ++ idx_region;
        }
    }

    // All regions now have distinct settings.
    // Check whether applying the new region config defaults we'd get different regions.
    for (size_t region_id = 0; region_id < m_regions.size(); ++ region_id) {
        PrintRegion       &region = *m_regions[region_id];
        PrintRegionConfig  this_region_config;
        bool               this_region_config_set = false;
        for (PrintObject *print_object : m_objects) {
            const LayerRanges *layer_ranges;
            {
                auto it_status = model_object_status.find(ModelObjectStatus(print_object->model_object()->id()));
                assert(it_status != model_object_status.end());
                assert(it_status->status != ModelObjectStatus::Deleted);
                layer_ranges = &it_status->layer_ranges;
            }
            if (region_id < print_object->region_volumes.size()) {
                for (const std::pair<t_layer_height_range, int> &volume_and_range : print_object->region_volumes[region_id]) {
                    const ModelVolume        &volume             = *print_object->model_object()->volumes[volume_and_range.second];
                    const DynamicPrintConfig *layer_range_config = layer_ranges->config(volume_and_range.first);
                    if (this_region_config_set) {
                        // If the new config for this volume differs from the other
                        // volume configs currently associated to this region, it means
                        // the region subdivision does not make sense anymore.
                        if (! this_region_config.equals(PrintObject::region_config_from_model_volume(m_default_region_config, layer_range_config, volume, num_extruders)))
                            // Regions were split. Reset this print_object.
                            goto print_object_end;
                    } else {
                        this_region_config = PrintObject::region_config_from_model_volume(m_default_region_config, layer_range_config, volume, num_extruders);
						for (size_t i = 0; i < region_id; ++ i) {
							const PrintRegion &region_other = *m_regions[i];
							if (region_other.m_refcnt != 0 && region_other.config().equals(this_region_config))
								// Regions were merged. Reset this print_object.
								goto print_object_end;
						}
                        this_region_config_set = true;
                    }
                }
            }
            continue;
        print_object_end:
            update_apply_status(print_object->invalidate_all_steps());
            // Decrease the references to regions from this volume.
            int ireg = 0;
            for (const std::vector<std::pair<t_layer_height_range, int>> &volumes : print_object->region_volumes) {
                if (! volumes.empty())
                    -- m_regions[ireg]->m_refcnt;
                ++ ireg;
            }
            print_object->region_volumes.clear();
        }
        if (this_region_config_set) {
            t_config_option_keys diff = region.config().diff(this_region_config);
            if (! diff.empty()) {
                region.config_apply_only(this_region_config, diff, false);
                for (PrintObject *print_object : m_objects)
                    if (region_id < print_object->region_volumes.size() && ! print_object->region_volumes[region_id].empty())
                        update_apply_status(print_object->invalidate_state_by_config_options(diff));
            }
        }
    }

    // Possibly add new regions for the newly added or resetted PrintObjects.
    for (size_t idx_print_object = 0; idx_print_object < m_objects.size(); ++ idx_print_object) {
        PrintObject        &print_object0 = *m_objects[idx_print_object];
        const ModelObject  &model_object  = *print_object0.model_object();
        const LayerRanges *layer_ranges;
        {
            auto it_status = model_object_status.find(ModelObjectStatus(model_object.id()));
            assert(it_status != model_object_status.end());
            assert(it_status->status != ModelObjectStatus::Deleted);
            layer_ranges = &it_status->layer_ranges;
        }
        std::vector<int>   regions_in_object;
        regions_in_object.reserve(64);
        for (size_t i = idx_print_object; i < m_objects.size() && m_objects[i]->model_object() == &model_object; ++ i) {
            PrintObject &print_object = *m_objects[i];
			bool         fresh = print_object.region_volumes.empty();
            unsigned int volume_id = 0;
            unsigned int idx_region_in_object = 0;
            for (const ModelVolume *volume : model_object.volumes) {
                if (! volume->is_model_part() && ! volume->is_modifier()) {
					++ volume_id;
					continue;
				}
                // Filter the layer ranges, so they do not overlap and they contain at least a single layer.
                // Now insert a volume with a layer range to its own region.
                for (auto it_range = layer_ranges->begin(); it_range != layer_ranges->end(); ++ it_range) {
                    int region_id = -1;
                    if (&print_object == &print_object0) {
                        // Get the config applied to this volume.
                        PrintRegionConfig config = PrintObject::region_config_from_model_volume(m_default_region_config, it_range->second, *volume, num_extruders);
                        // Find an existing print region with the same config.
    					int idx_empty_slot = -1;
    					for (int i = 0; i < (int)m_regions.size(); ++ i) {
    						if (m_regions[i]->m_refcnt == 0) {
                                if (idx_empty_slot == -1)
                                    idx_empty_slot = i;
                            } else if (config.equals(m_regions[i]->config())) {
                                region_id = i;
                                break;
                            }
    					}
                        // If no region exists with the same config, create a new one.
    					if (region_id == -1) {
    						if (idx_empty_slot == -1) {
    							region_id = (int)m_regions.size();
    							this->add_region(config);
    						} else {
    							region_id = idx_empty_slot;
                                m_regions[region_id]->set_config(std::move(config));
    						}
                        }
                        regions_in_object.emplace_back(region_id);
                    } else
                        region_id = regions_in_object[idx_region_in_object ++];
                    // Assign volume to a region.
    				if (fresh) {
    					if (region_id >= print_object.region_volumes.size() || print_object.region_volumes[region_id].empty())
    						++ m_regions[region_id]->m_refcnt;
    					print_object.add_region_volume(region_id, volume_id, it_range->first);
    				}
                }
				++ volume_id;
			}
        }
    }

    // Update SlicingParameters for each object where the SlicingParameters is not valid.
    // If it is not valid, then it is ensured that PrintObject.m_slicing_params is not in use
    // (posSlicing and posSupportMaterial was invalidated).
    for (PrintObject *object : m_objects)
        object->update_slicing_parameters();

#ifdef _DEBUG
    check_model_ids_equal(m_model, model);
#endif /* _DEBUG */

	return static_cast<ApplyStatus>(apply_status);
}

bool Print::has_infinite_skirt() const
{
    return (m_config.skirt_height == -1 && m_config.skirts > 0)
        || (m_config.ooze_prevention && this->extruders().size() > 1);
}

bool Print::has_skirt() const
{
    return (m_config.skirt_height > 0 && m_config.skirts > 0)
        || this->has_infinite_skirt();
}

// Precondition: Print::validate() requires the Print::apply() to be called its invocation.
std::string Print::validate() const
{
    if (m_objects.empty())
        return L("All objects are outside of the print volume.");

    if (m_config.complete_objects) {
        // Check horizontal clearance.
        {
            Polygons convex_hulls_other;
            for (const PrintObject *print_object : m_objects) {
                assert(! print_object->model_object()->instances.empty());
                assert(! print_object->copies().empty());
                // Get convex hull of all meshes assigned to this print object.
                ModelInstance *model_instance0 = print_object->model_object()->instances.front();
                Vec3d          rotation        = model_instance0->get_rotation();
                rotation.z() = 0.;
                // Calculate the convex hull of a printable object centered around X=0,Y=0. 
                // Grow convex hull with the clearance margin.
                // FIXME: Arrangement has different parameters for offsetting (jtMiter, limit 2)
                // which causes that the warning will be showed after arrangement with the
                // appropriate object distance. Even if I set this to jtMiter the warning still shows up.
                Polygon        convex_hull0    = offset(
                    print_object->model_object()->convex_hull_2d(
                        Geometry::assemble_transform(Vec3d::Zero(), rotation, model_instance0->get_scaling_factor(), model_instance0->get_mirror())),
                    float(scale_(0.5 * m_config.extruder_clearance_radius.value)), jtRound, float(scale_(0.1))).front();
                // Now we check that no instance of convex_hull intersects any of the previously checked object instances.
                for (const Point &copy : print_object->copies()) {
                    Polygon convex_hull = convex_hull0;
                    convex_hull.translate(copy);
                    if (! intersection(convex_hulls_other, convex_hull).empty())
                        return L("Some objects are too close; your extruder will collide with them.");
                    polygons_append(convex_hulls_other, convex_hull);
                }
            }
        }
        // Check vertical clearance.
        {
            std::vector<coord_t> object_height;
            for (const PrintObject *object : m_objects)
                object_height.insert(object_height.end(), object->copies().size(), object->size(2));
            std::sort(object_height.begin(), object_height.end());
            // Ignore the tallest *copy* (this is why we repeat height for all of them):
            // it will be printed as last one so its height doesn't matter.
            object_height.pop_back();
            if (! object_height.empty() && object_height.back() > scale_(m_config.extruder_clearance_height.value))
                return L("Some objects are too tall and cannot be printed without extruder collisions.");
        }
    } // end if (m_config.complete_objects)

    if (m_config.spiral_vase) {
        size_t total_copies_count = 0;
        for (const PrintObject *object : m_objects)
            total_copies_count += object->copies().size();
        // #4043
        if (total_copies_count > 1 && ! m_config.complete_objects.value)
            return L("The Spiral Vase option can only be used when printing a single object.");
        if (m_regions.size() > 1)
            return L("The Spiral Vase option can only be used when printing single material objects.");
    }

    if (m_config.single_extruder_multi_material) {
        for (size_t i=1; i<m_config.nozzle_diameter.values.size(); ++i)
            if (m_config.nozzle_diameter.values[i] != m_config.nozzle_diameter.values[i-1])
                return L("All extruders must have the same diameter for single extruder multimaterial printer.");
    }

    if (this->has_wipe_tower() && ! m_objects.empty()) {
        if (m_config.gcode_flavor != gcfRepRap && m_config.gcode_flavor != gcfRepetier && m_config.gcode_flavor != gcfMarlin)
            return L("The Wipe Tower is currently only supported for the Marlin, RepRap/Sprinter and Repetier G-code flavors.");
        if (! m_config.use_relative_e_distances)
            return L("The Wipe Tower is currently only supported with the relative extruder addressing (use_relative_e_distances=1).");

        if (m_objects.size() > 1) {
            bool                                has_custom_layering = false;
            std::vector<std::vector<coordf_t>>  layer_height_profiles;
            for (const PrintObject *object : m_objects) {
                has_custom_layering = ! object->model_object()->layer_config_ranges.empty() || ! object->model_object()->layer_height_profile.empty();      // #ys_FIXME_experiment
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
                    return L("The Wipe Tower is only supported for multiple objects if they have equal layer heights");
                if (slicing_params.raft_layers() != slicing_params0.raft_layers())
                    return L("The Wipe Tower is only supported for multiple objects if they are printed over an equal number of raft layers");
                if (object->config().support_material_contact_distance != m_objects.front()->config().support_material_contact_distance)
                    return L("The Wipe Tower is only supported for multiple objects if they are printed with the same support_material_contact_distance");
                if (! equal_layering(slicing_params, slicing_params0))
                    return L("The Wipe Tower is only supported for multiple objects if they are sliced equally.");
                if (has_custom_layering) {
                    PrintObject::update_layer_height_profile(*object->model_object(), slicing_params, layer_height_profiles[i]);
                    if (*(layer_height_profiles[i].end()-2) > *(layer_height_profiles[tallest_object_idx].end()-2))
                        tallest_object_idx = i;
                }
            }

            if (has_custom_layering) {
                const std::vector<coordf_t> &layer_height_profile_tallest = layer_height_profiles[tallest_object_idx];
                for (size_t idx_object = 0; idx_object < m_objects.size(); ++ idx_object) {
                    const PrintObject           *object               = m_objects[idx_object];
                    const std::vector<coordf_t> &layer_height_profile = layer_height_profiles[idx_object];
                    bool                         failed               = false;
                    if (layer_height_profile_tallest.size() >= layer_height_profile.size()) {
                        int i = 0;
                        while (i < layer_height_profile.size() && i < layer_height_profile_tallest.size()) {
                            if (std::abs(layer_height_profile_tallest[i] - layer_height_profile[i])) {
                                failed = true;
                                break;
                            }
                            ++ i;
                            if (i == layer_height_profile.size() - 2) // this element contains this objects max z
                                if (layer_height_profile_tallest[i] > layer_height_profile[i]) // the difference does not matter in this case
                                    ++ i;
                        }
                    } else
                        failed = true;
                    if (failed)
                        return L("The Wipe tower is only supported if all objects have the same layer height profile");
                }
            }
        }
    }
    
	{
		// find the smallest nozzle diameter
		std::vector<unsigned int> extruders = this->extruders();
		if (extruders.empty())
			return L("The supplied settings will cause an empty print.");

		// Find the smallest used nozzle diameter and the number of unique nozzle diameters.
		double min_nozzle_diameter = std::numeric_limits<double>::max();
		double max_nozzle_diameter = 0;
		for (unsigned int extruder_id : extruders) {
			double dmr = m_config.nozzle_diameter.get_at(extruder_id);
			min_nozzle_diameter = std::min(min_nozzle_diameter, dmr);
			max_nozzle_diameter = std::max(max_nozzle_diameter, dmr);
		}

#if 0
        // We currently allow one to assign extruders with a higher index than the number
        // of physical extruders the machine is equipped with, as the Printer::apply() clamps them.
        unsigned int total_extruders_count = m_config.nozzle_diameter.size();
        for (const auto& extruder_idx : extruders)
            if ( extruder_idx >= total_extruders_count )
                return L("One or more object were assigned an extruder that the printer does not have.");
#endif

        for (PrintObject *object : m_objects) {
            if (object->config().raft_layers > 0 || object->config().support_material.value) {
				if ((object->config().support_material_extruder == 0 || object->config().support_material_interface_extruder == 0) && max_nozzle_diameter - min_nozzle_diameter > EPSILON) {
                    // The object has some form of support and either support_material_extruder or support_material_interface_extruder
                    // will be printed with the current tool without a forced tool change. Play safe, assert that all object nozzles
                    // are of the same diameter.
                    return L("Printing with multiple extruders of differing nozzle diameters. "
                           "If support is to be printed with the current extruder (support_material_extruder == 0 or support_material_interface_extruder == 0), "
                           "all nozzles have to be of the same diameter.");
                }
                if (this->has_wipe_tower()) {
    				if (object->config().support_material_contact_distance == 0) {
    					// Soluble interface
    					if (object->config().support_material_contact_distance == 0 && ! object->config().support_material_synchronize_layers)
    						return L("For the Wipe Tower to work with the soluble supports, the support layers need to be synchronized with the object layers.");
    				} else {
    					// Non-soluble interface
    					if (object->config().support_material_extruder != 0 || object->config().support_material_interface_extruder != 0)
    						return L("The Wipe Tower currently supports the non-soluble supports only if they are printed with the current extruder without triggering a tool change. "
    							     "(both support_material_extruder and support_material_interface_extruder need to be set to 0).");
    				}
                }
            }
            
            // validate first_layer_height
            double first_layer_height = object->config().get_abs_value("first_layer_height");
            double first_layer_min_nozzle_diameter;
            if (object->config().raft_layers > 0) {
                // if we have raft layers, only support material extruder is used on first layer
                size_t first_layer_extruder = object->config().raft_layers == 1
                    ? object->config().support_material_interface_extruder-1
                    : object->config().support_material_extruder-1;
                first_layer_min_nozzle_diameter = (first_layer_extruder == size_t(-1)) ? 
                    min_nozzle_diameter : 
                    m_config.nozzle_diameter.get_at(first_layer_extruder);
            } else {
                // if we don't have raft layers, any nozzle diameter is potentially used in first layer
                first_layer_min_nozzle_diameter = min_nozzle_diameter;
            }
            if (first_layer_height > first_layer_min_nozzle_diameter)
                return L("First layer height can't be greater than nozzle diameter");
            
            // validate layer_height
            if (object->config().layer_height.value > min_nozzle_diameter)
                return L("Layer height can't be greater than nozzle diameter");
        }
    }

    return std::string();
}

// the bounding box of objects placed in copies position
// (without taking skirt/brim/support material into account)
BoundingBox Print::bounding_box() const
{
    BoundingBox bb;
    for (const PrintObject *object : m_objects)
        for (Point copy : object->m_copies) {
            bb.merge(copy);
            copy += to_2d(object->size);
            bb.merge(copy);
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
        int skirts = m_config.skirts.value;
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

double Print::skirt_first_layer_height() const
{
    if (m_objects.empty()) 
        throw std::invalid_argument("skirt_first_layer_height() can't be called without PrintObjects");
    return m_objects.front()->config().get_abs_value("first_layer_height");
}

Flow Print::brim_flow() const
{
    ConfigOptionFloatOrPercent width = m_config.first_layer_extrusion_width;
    if (width.value == 0) 
        width = m_regions.front()->config().perimeter_extrusion_width;
    if (width.value == 0) 
        width = m_objects.front()->config().extrusion_width;
    
    /* We currently use a random region's perimeter extruder.
       While this works for most cases, we should probably consider all of the perimeter
       extruders and take the one with, say, the smallest index.
       The same logic should be applied to the code that selects the extruder during G-code
       generation as well. */
    return Flow::new_from_config_width(
        frPerimeter,
		width,
        (float)m_config.nozzle_diameter.get_at(m_regions.front()->config().perimeter_extruder-1),
		(float)this->skirt_first_layer_height(),
        0
    );
}

Flow Print::skirt_flow() const
{
    ConfigOptionFloatOrPercent width = m_config.first_layer_extrusion_width;
    if (width.value == 0) 
        width = m_regions.front()->config().perimeter_extrusion_width;
    if (width.value == 0)
        width = m_objects.front()->config().extrusion_width;
    
    /* We currently use a random object's support material extruder.
       While this works for most cases, we should probably consider all of the support material
       extruders and take the one with, say, the smallest index;
       The same logic should be applied to the code that selects the extruder during G-code
       generation as well. */
    return Flow::new_from_config_width(
        frPerimeter,
		width,
		(float)m_config.nozzle_diameter.get_at(m_objects.front()->config().support_material_extruder-1),
		(float)this->skirt_first_layer_height(),
        0
    );
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
            volume->config.opt<ConfigOptionInt>("extruder", true)->value = int(volume_id + 1);
    }
}

// Slicing process, running at a background thread.
void Print::process()
{
    BOOST_LOG_TRIVIAL(info) << "Staring the slicing process." << log_memory_info();
    for (PrintObject *obj : m_objects)
        obj->make_perimeters();
    this->set_status(70, L("Infilling layers"));
    for (PrintObject *obj : m_objects)
        obj->infill();
    for (PrintObject *obj : m_objects)
        obj->generate_support_material();
    if (this->set_started(psSkirt)) {
        m_skirt.clear();
        if (this->has_skirt()) {
            this->set_status(88, L("Generating skirt"));
            this->_make_skirt();
        }
        this->set_done(psSkirt);
    }
	if (this->set_started(psBrim)) {
        m_brim.clear();
        if (m_config.brim_width > 0) {
            this->set_status(88, L("Generating brim"));
            this->_make_brim();
        }
       this->set_done(psBrim);
    }
    if (this->set_started(psWipeTower)) {
        m_wipe_tower_data.clear();
        if (this->has_wipe_tower()) {
            //this->set_status(95, L("Generating wipe tower"));
            this->_make_wipe_tower();
        }
       this->set_done(psWipeTower);
    }
    BOOST_LOG_TRIVIAL(info) << "Slicing process finished." << log_memory_info();
}

// G-code export process, running at a background thread.
// The export_gcode may die for various reasons (fails to process output_filename_format,
// write error into the G-code, cannot execute post-processing scripts).
// It is up to the caller to show an error message.
std::string Print::export_gcode(const std::string &path_template, GCodePreviewData *preview_data)
{
    // output everything to a G-code file
    // The following call may die if the output_filename_format template substitution fails.
    std::string path = this->output_filepath(path_template);
    std::string message;
    if (! path.empty() && preview_data == nullptr) {
        // Only show the path if preview_data is not set -> running from command line.
        message = L("Exporting G-code");
        message += " to ";
        message += path;
    } else
        message = L("Generating G-code");
    this->set_status(90, message);

    // The following line may die for multiple reasons.
    GCode gcode;
    gcode.do_export(this, path.c_str(), preview_data);
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
    for (const PrintObject *object : m_objects) {
        Points object_points;
        // Get object layers up to skirt_height_z.
        for (const Layer *layer : object->m_layers) {
            if (layer->print_z > skirt_height_z)
                break;
            for (const ExPolygon &expoly : layer->slices.expolygons)
                // Collect the outer contour points only, ignore holes for the calculation of the convex hull.
                append(object_points, expoly.contour.points);
        }
        // Get support layers up to skirt_height_z.
        for (const SupportLayer *layer : object->support_layers()) {
            if (layer->print_z > skirt_height_z)
                break;
            for (const ExtrusionEntity *extrusion_entity : layer->support_fills.entities)
                append(object_points, extrusion_entity->as_polyline().points);
        }
        // Repeat points for each object copy.
        for (const Point &shift : object->m_copies) {
            Points copy_points = object_points;
            for (Point &pt : copy_points)
                pt += shift;
            append(points, copy_points);
        }
    }

    if (points.size() < 3)
        // At least three points required for a convex hull.
        return;
    
    this->throw_if_canceled();
    Polygon convex_hull = Slic3r::Geometry::convex_hull(points);
    
    // Skirt may be printed on several layers, having distinct layer heights,
    // but loops must be aligned so can't vary width/spacing
    // TODO: use each extruder's own flow
    double first_layer_height = this->skirt_first_layer_height();
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
            extruders_e_per_mm.push_back(Extruder((unsigned int)extruder_id, &m_config).e_per_mm(mm3_per_mm));
        }
    }

    // Number of skirt loops per skirt layer.
    int n_skirts = m_config.skirts.value;
    if (this->has_infinite_skirt() && n_skirts == 0)
        n_skirts = 1;

    // Initial offset of the brim inner edge from the object (possible with a support & raft).
    // The skirt will touch the brim if the brim is extruded.
    Flow   brim_flow = this->brim_flow();
    double actual_brim_width = brim_flow.spacing() * floor(m_config.brim_width.value / brim_flow.spacing());
    auto   distance = float(scale_(std::max(m_config.skirt_distance.value, actual_brim_width) - spacing/2.));
    // Draw outlines from outside to inside.
    // Loop while we have less skirts than required or any extruder hasn't reached the min length if any.
    std::vector<coordf_t> extruded_length(extruders.size(), 0.);
    for (int i = n_skirts, extruder_idx = 0; i > 0; -- i) {
        this->throw_if_canceled();
        // Offset the skirt outside.
        distance += float(scale_(spacing));
        // Generate the skirt centerline.
        Polygon loop;
        {
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
                flow.width,
				(float)first_layer_height  // this will be overridden at G-code export time
            )));
        eloop.paths.back().polyline = loop.split_at_first_point();
        m_skirt.append(eloop);
        if (m_config.min_skirt_length.value > 0) {
            // The skirt length is limited. Sum the total amount of filament length extruded, in mm.
            extruded_length[extruder_idx] += unscale<double>(loop.length()) * extruders_e_per_mm[extruder_idx];
            if (extruded_length[extruder_idx] < m_config.min_skirt_length.value) {
                // Not extruded enough yet with the current extruder. Add another loop.
                if (i == 1)
                    ++ i;
            } else {
                assert(extruded_length[extruder_idx] >= m_config.min_skirt_length.value);
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
}

void Print::_make_brim()
{
    // Brim is only printed on first layer and uses perimeter extruder.
    Flow        flow = this->brim_flow();
    Polygons    islands;
    for (PrintObject *object : m_objects) {
        Polygons object_islands;
        for (ExPolygon &expoly : object->m_layers.front()->slices.expolygons)
            object_islands.push_back(expoly.contour);
        if (! object->support_layers().empty())
            object->support_layers().front()->support_fills.polygons_covered_by_spacing(object_islands, float(SCALED_EPSILON));
        islands.reserve(islands.size() + object_islands.size() * object->m_copies.size());
        for (const Point &pt : object->m_copies)
            for (Polygon &poly : object_islands) {
                islands.push_back(poly);
                islands.back().translate(pt);
            }
    }
    Polygons loops;
    size_t num_loops = size_t(floor(m_config.brim_width.value / flow.spacing()));
    for (size_t i = 0; i < num_loops; ++ i) {
        this->throw_if_canceled();
        islands = offset(islands, float(flow.scaled_spacing()), jtSquare);
        for (Polygon &poly : islands) {
            // poly.simplify(SCALED_RESOLUTION);
            poly.points.push_back(poly.points.front());
            Points p = MultiPoint::_douglas_peucker(poly.points, SCALED_RESOLUTION);
            p.pop_back();
            poly.points = std::move(p);
        }
        polygons_append(loops, offset(islands, -0.5f * float(flow.scaled_spacing())));
    }

    loops = union_pt_chained(loops, false);
    // The function above produces ordering well suited for concentric infill (from outside to inside).
    // For Brim, the ordering should be reversed (from inside to outside).
    std::reverse(loops.begin(), loops.end());
    extrusion_entities_append_loops(m_brim.entities, std::move(loops), erSkirt, float(flow.mm3_per_mm()), float(flow.width), float(this->skirt_first_layer_height()));
}

// Wipe tower support.
bool Print::has_wipe_tower() const
{
    return 
        m_config.single_extruder_multi_material.value && 
        ! m_config.spiral_vase.value &&
        m_config.wipe_tower.value && 
        m_config.nozzle_diameter.values.size() > 1;
}

void Print::_make_wipe_tower()
{
    m_wipe_tower_data.clear();
    if (! this->has_wipe_tower())
        return;

    // Get wiping matrix to get number of extruders and convert vector<double> to vector<float>:
    std::vector<float> wiping_matrix(cast<float>(m_config.wiping_volumes_matrix.values));
    // Extract purging volumes for each extruder pair:
    std::vector<std::vector<float>> wipe_volumes;
    const unsigned int number_of_extruders = (unsigned int)(sqrt(wiping_matrix.size())+EPSILON);
    for (unsigned int i = 0; i<number_of_extruders; ++i)
        wipe_volumes.push_back(std::vector<float>(wiping_matrix.begin()+i*number_of_extruders, wiping_matrix.begin()+(i+1)*number_of_extruders));

    // Let the ToolOrdering class know there will be initial priming extrusions at the start of the print.
    m_wipe_tower_data.tool_ordering = ToolOrdering(*this, (unsigned int)-1, true);
    if (! m_wipe_tower_data.tool_ordering.has_wipe_tower())
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
            SupportLayerPtrs::const_iterator it_layer = m_objects.front()->support_layers().begin();
            SupportLayerPtrs::const_iterator it_end   = m_objects.front()->support_layers().end();
            for (; it_layer != it_end && (*it_layer)->print_z - EPSILON < wipe_tower_new_layer_print_z_first; ++ it_layer);
            // Find the stopper of the sequence of wipe tower layers, which do not have a counterpart in an object or a support layer.
            for (size_t i = idx_begin; i < idx_end; ++ i) {
                LayerTools &lt = const_cast<LayerTools&>(m_wipe_tower_data.tool_ordering.layer_tools()[i]);
                if (! (lt.has_wipe_tower && ! lt.has_object && ! lt.has_support))
                    break;
                lt.has_support = true;
                // Insert the new support layer.
                double height    = lt.print_z - m_wipe_tower_data.tool_ordering.layer_tools()[i-1].print_z;
                //FIXME the support layer ID is set to -1, as Vojtech hopes it is not being used anyway.
                it_layer = m_objects.front()->insert_support_layer(it_layer, -1, height, lt.print_z, lt.print_z - 0.5 * height);
                ++ it_layer;
            }
        }
    }
    this->throw_if_canceled();

    // Initialize the wipe tower.
    WipeTowerPrusaMM wipe_tower(
        float(m_config.wipe_tower_x.value),     float(m_config.wipe_tower_y.value), 
        float(m_config.wipe_tower_width.value),
        float(m_config.wipe_tower_rotation_angle.value), float(m_config.cooling_tube_retraction.value),
        float(m_config.cooling_tube_length.value), float(m_config.parking_pos_retraction.value),
        float(m_config.extra_loading_move.value), float(m_config.wipe_tower_bridging), 
        m_config.high_current_on_filament_swap.value, m_config.gcode_flavor, wipe_volumes,
        m_wipe_tower_data.tool_ordering.first_extruder());

    //wipe_tower.set_retract();
    //wipe_tower.set_zhop();

    // Set the extruder & material properties at the wipe tower object.
    for (size_t i = 0; i < number_of_extruders; ++ i)
        wipe_tower.set_extruder(
            i, 
            WipeTowerPrusaMM::parse_material(m_config.filament_type.get_at(i).c_str()),
            m_config.temperature.get_at(i),
            m_config.first_layer_temperature.get_at(i),
			(float)m_config.filament_loading_speed.get_at(i),
			(float)m_config.filament_loading_speed_start.get_at(i),
			(float)m_config.filament_unloading_speed.get_at(i),
			(float)m_config.filament_unloading_speed_start.get_at(i),
			(float)m_config.filament_toolchange_delay.get_at(i),
            m_config.filament_cooling_moves.get_at(i),
			(float)m_config.filament_cooling_initial_speed.get_at(i),
			(float)m_config.filament_cooling_final_speed.get_at(i),
            m_config.filament_ramming_parameters.get_at(i),
			(float)m_config.nozzle_diameter.get_at(i));

    m_wipe_tower_data.priming = Slic3r::make_unique<WipeTower::ToolChangeResult>(
        wipe_tower.prime((float)this->skirt_first_layer_height(), m_wipe_tower_data.tool_ordering.all_extruders(), false));

    // Lets go through the wipe tower layers and determine pairs of extruder changes for each
    // to pass to wipe_tower (so that it can use it for planning the layout of the tower)
    {
        unsigned int current_extruder_id = m_wipe_tower_data.tool_ordering.all_extruders().back();
        for (auto &layer_tools : m_wipe_tower_data.tool_ordering.layer_tools()) { // for all layers
            if (!layer_tools.has_wipe_tower) continue;
            bool first_layer = &layer_tools == &m_wipe_tower_data.tool_ordering.front();
            wipe_tower.plan_toolchange((float)layer_tools.print_z, (float)layer_tools.wipe_tower_layer_height, current_extruder_id, current_extruder_id, false);
            for (const auto extruder_id : layer_tools.extruders) {
                if ((first_layer && extruder_id == m_wipe_tower_data.tool_ordering.all_extruders().back()) || extruder_id != current_extruder_id) {
                    float volume_to_wipe = wipe_volumes[current_extruder_id][extruder_id];             // total volume to wipe after this toolchange
                    // Not all of that can be used for infill purging:
                    volume_to_wipe -= (float)m_config.filament_minimal_purge_on_wipe_tower.get_at(extruder_id);

                    // try to assign some infills/objects for the wiping:
                    volume_to_wipe = layer_tools.wiping_extrusions().mark_wiping_extrusions(*this, current_extruder_id, extruder_id, volume_to_wipe);

                    // add back the minimal amount toforce on the wipe tower:
                    volume_to_wipe += (float)m_config.filament_minimal_purge_on_wipe_tower.get_at(extruder_id);

                    // request a toolchange at the wipe tower with at least volume_to_wipe purging amount
                    wipe_tower.plan_toolchange((float)layer_tools.print_z, (float)layer_tools.wipe_tower_layer_height, current_extruder_id, extruder_id,
                                               first_layer && extruder_id == m_wipe_tower_data.tool_ordering.all_extruders().back(), volume_to_wipe);
                    current_extruder_id = extruder_id;
                }
            }
            layer_tools.wiping_extrusions().ensure_perimeters_infills_order(*this);
            if (&layer_tools == &m_wipe_tower_data.tool_ordering.back() || (&layer_tools + 1)->wipe_tower_partitions == 0)
                break;
        }
    }

    // Generate the wipe tower layers.
    m_wipe_tower_data.tool_changes.reserve(m_wipe_tower_data.tool_ordering.layer_tools().size());
    wipe_tower.generate(m_wipe_tower_data.tool_changes);
    m_wipe_tower_data.depth = wipe_tower.get_depth();

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
		wipe_tower.tool_change((unsigned int)-1, false));

    m_wipe_tower_data.used_filament = wipe_tower.get_used_filament();
    m_wipe_tower_data.number_of_toolchanges = wipe_tower.get_number_of_toolchanges();
}

// Returns extruder this eec should be printed with, according to PrintRegion config
int Print::get_extruder(const ExtrusionEntityCollection& fill, const PrintRegion &region)
{
    return is_infill(fill.role()) ? std::max<int>(0, (is_solid_infill(fill.entities.front()->role()) ? region.config().solid_infill_extruder : region.config().infill_extruder) - 1) :
                                    std::max<int>(region.config().perimeter_extruder.value - 1, 0);
}

// Generate a recommended G-code output file name based on the format template, default extension, and template parameters
// (timestamps, object placeholders derived from the model, current placeholder prameters and print statistics.
// Use the final print statistics if available, or just keep the print statistics placeholders if not available yet (before G-code is finalized).
std::string Print::output_filename(const std::string &filename_base) const 
{ 
    // Set the placeholders for the data know first after the G-code export is finished.
    // These values will be just propagated into the output file name.
    DynamicConfig config = this->finished() ? this->print_statistics().config() : this->print_statistics().placeholders();
    return this->PrintBase::output_filename(m_config.output_filename_format.value, ".gcode", filename_base, &config);
}
/*
// Shorten the dhms time by removing the seconds, rounding the dhm to full minutes
// and removing spaces.
static std::string short_time(const std::string &time)
{
    // Parse the dhms time format.
    int days    = 0;
    int hours   = 0;
    int minutes = 0;
    int seconds = 0;
    if (time.find('d') != std::string::npos)
        ::sscanf(time.c_str(), "%dd %dh %dm %ds", &days, &hours, &minutes, &seconds);
    else if (time.find('h') != std::string::npos)
        ::sscanf(time.c_str(), "%dh %dm %ds", &hours, &minutes, &seconds);
    else if (time.find('m') != std::string::npos)
        ::sscanf(time.c_str(), "%dm %ds", &minutes, &seconds);
    else if (time.find('s') != std::string::npos)
        ::sscanf(time.c_str(), "%ds", &seconds);
    // Round to full minutes.
    if (days + hours + minutes > 0 && seconds >= 30) {
        if (++ minutes == 60) {
            minutes = 0;
            if (++ hours == 24) {
                hours = 0;
                ++ days;
            }
        }
    }
    // Format the dhm time.
    char buffer[64];
    if (days > 0)
        ::sprintf(buffer, "%dd%dh%dm", days, hours, minutes);
    else if (hours > 0)
        ::sprintf(buffer, "%dh%dm", hours, minutes);
    else if (minutes > 0)
        ::sprintf(buffer, "%dm", minutes);
    else
        ::sprintf(buffer, "%ds", seconds);
    return buffer;
}
*/
DynamicConfig PrintStatistics::config() const
{
    DynamicConfig config;
    std::string normal_print_time = short_time(this->estimated_normal_print_time);
    std::string silent_print_time = short_time(this->estimated_silent_print_time);
    config.set_key_value("print_time",                new ConfigOptionString(normal_print_time));
    config.set_key_value("normal_print_time",         new ConfigOptionString(normal_print_time));
    config.set_key_value("silent_print_time",         new ConfigOptionString(silent_print_time));
    config.set_key_value("used_filament",             new ConfigOptionFloat (this->total_used_filament / 1000.));
    config.set_key_value("extruded_volume",           new ConfigOptionFloat (this->total_extruded_volume));
    config.set_key_value("total_cost",                new ConfigOptionFloat (this->total_cost));
    config.set_key_value("total_weight",              new ConfigOptionFloat (this->total_weight));
    config.set_key_value("total_wipe_tower_cost",     new ConfigOptionFloat (this->total_wipe_tower_cost));
    config.set_key_value("total_wipe_tower_filament", new ConfigOptionFloat (this->total_wipe_tower_filament));
    return config;
}

DynamicConfig PrintStatistics::placeholders()
{
    DynamicConfig config;
    for (const std::string &key : { 
        "print_time", "normal_print_time", "silent_print_time", 
        "used_filament", "extruded_volume", "total_cost", "total_weight", 
        "total_wipe_tower_cost", "total_wipe_tower_filament"})
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
