#include "Print.hpp"
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "Extruder.hpp"
#include "Flow.hpp"
#include "Geometry.hpp"
#include "SupportMaterial.hpp"
#include "GCode/WipeTowerPrusaMM.hpp"
#include <algorithm>
#include <unordered_set>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>

namespace Slic3r {

template class PrintState<PrintStep, psCount>;
template class PrintState<PrintObjectStep, posCount>;

void Print::clear_objects()
{
    for (int i = int(this->objects.size())-1; i >= 0; --i)
        this->delete_object(i);
    for (PrintRegion *region : this->regions)
        delete region;
    this->regions.clear();
}

void Print::delete_object(size_t idx)
{
    // destroy object and remove it from our container
    delete this->objects[idx];
    this->objects.erase(this->objects.begin() + idx);
    this->invalidate_all_steps();
    // TODO: purge unused regions
}

void Print::reload_object(size_t /* idx */)
{
    /* TODO: this method should check whether the per-object config and per-material configs
        have changed in such a way that regions need to be rearranged or we can just apply
        the diff and invalidate something.  Same logic as apply_config()
        For now we just re-add all objects since we haven't implemented this incremental logic yet.
        This should also check whether object volumes (parts) have changed. */
    
    // collect all current model objects
    ModelObjectPtrs model_objects;
    model_objects.reserve(this->objects.size());
    for (PrintObject *object : this->objects)
        model_objects.push_back(object->model_object());    
    // remove our print objects
    this->clear_objects();
    // re-add model objects
    for (ModelObject *mo : model_objects)
        this->add_model_object(mo);
}

// Reloads the model instances into the print class.
// The slicing shall not be running as the modified model instances at the print
// are used for the brim & skirt calculation.
// Returns true if the brim or skirt have been invalidated.
bool Print::reload_model_instances()
{
    bool invalidated = false;
    for (PrintObject *object : this->objects)
        invalidated |= object->reload_model_instances();
    return invalidated;
}

PrintRegion* Print::add_region()
{
    regions.push_back(new PrintRegion(this));
    return regions.back();
}

// Called by Print::apply_config().
// This method only accepts PrintConfig option keys.
bool Print::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys)
{
    if (opt_keys.empty())
        return false;

    // Cache the plenty of parameters, which influence the G-code generator only,
    // or they are only notes not influencing the generated G-code.
    static std::unordered_set<std::string> steps_ignore = {
        "avoid_crossing_perimeters",
        "bed_shape",
        "bed_temperature",
        "before_layer_gcode",
        "between_objects_gcode",
        "bridge_acceleration",
        "bridge_fan_speed",
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
        "gcode_flavor",
        "infill_acceleration",
        "infill_first",
        "layer_gcode",
        "min_fan_speed",
        "max_fan_speed",
        "max_print_height",
        "min_print_speed",
        "max_print_speed",
        "max_volumetric_speed",
        "max_volumetric_extrusion_rate_slope_positive",
        "max_volumetric_extrusion_rate_slope_negative",
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
        "wipe"
    };

    std::vector<PrintStep> steps;
    std::vector<PrintObjectStep> osteps;
    bool invalidated = false;
    for (const t_config_option_key &opt_key : opt_keys) {
        if (steps_ignore.find(opt_key) != steps_ignore.end()) {
            // These options only affect G-code export or they are just notes without influence on the generated G-code,
            // so there is nothing to invalidate.
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
            || opt_key == "filament_unloading_speed"
            || opt_key == "filament_toolchange_delay"
            || opt_key == "filament_cooling_time"
            || opt_key == "filament_ramming_parameters"
            || opt_key == "gcode_flavor"
            || opt_key == "single_extruder_multi_material"
            || opt_key == "spiral_vase"
            || opt_key == "temperature"
            || opt_key == "wipe_tower"
            || opt_key == "wipe_tower_x"
            || opt_key == "wipe_tower_y"
            || opt_key == "wipe_tower_width"
            || opt_key == "wipe_tower_rotation_angle"
            || opt_key == "wipe_tower_bridging"
            || opt_key == "wiping_volumes_matrix"
            || opt_key == "parking_pos_retraction"
            || opt_key == "cooling_tube_retraction"
            || opt_key == "cooling_tube_length"
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
            steps.emplace_back(psWipeTower);
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
        for (PrintObject *object : this->objects)
            invalidated |= object->invalidate_step(ostep);
    return invalidated;
}

bool Print::invalidate_step(PrintStep step)
{
    bool invalidated = this->state.invalidate(step);
    // Propagate to dependent steps.
    //FIXME Why should skirt invalidate brim? Shouldn't it be vice versa?
    if (step == psSkirt)
        invalidated |= this->state.invalidate(psBrim);
    return invalidated;
}

// returns true if an object step is done on all objects
// and there's at least one object
bool Print::step_done(PrintObjectStep step) const
{
    if (this->objects.empty())
        return false;
    for (const PrintObject *object : this->objects)
        if (!object->state.is_done(step))
            return false;
    return true;
}

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::object_extruders() const
{
    std::vector<unsigned int> extruders;
    
    for (PrintRegion* region : this->regions) {
        // these checks reflect the same logic used in the GUI for enabling/disabling
        // extruder selection fields
        if (region->config.perimeters.value > 0 || this->config.brim_width.value > 0)
            extruders.push_back(region->config.perimeter_extruder - 1);
        if (region->config.fill_density.value > 0)
            extruders.push_back(region->config.infill_extruder - 1);
        if (region->config.top_solid_layers.value > 0 || region->config.bottom_solid_layers.value > 0)
            extruders.push_back(region->config.solid_infill_extruder - 1);
    }
    
    sort_remove_duplicates(extruders);
    return extruders;
}

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::support_material_extruders() const
{
    std::vector<unsigned int> extruders;
    bool support_uses_current_extruder = false;

    for (PrintObject *object : this->objects) {
        if (object->has_support_material()) {
            if (object->config.support_material_extruder == 0)
                support_uses_current_extruder = true;
            else
                extruders.push_back(object->config.support_material_extruder - 1);
            if (object->config.support_material_interface_extruder == 0)
                support_uses_current_extruder = true;
            else
                extruders.push_back(object->config.support_material_interface_extruder - 1);
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

void Print::_simplify_slices(double distance)
{
    for (PrintObject *object : this->objects) {
        for (Layer *layer : object->layers) {
            layer->slices.simplify(distance);
            for (LayerRegion *layerm : layer->regions)
                layerm->slices.simplify(distance);
        }
    }
}

double Print::max_allowed_layer_height() const
{
    double nozzle_diameter_max = 0.;
    for (unsigned int extruder_id : this->extruders())
        nozzle_diameter_max = std::max(nozzle_diameter_max, this->config.nozzle_diameter.get_at(extruder_id));
    return nozzle_diameter_max;
}

// Caller is responsible for supplying models whose objects don't collide
// and have explicit instance positions.
void Print::add_model_object(ModelObject* model_object, int idx)
{
    // Initialize a new print object and store it at the given position.
    PrintObject *object = new PrintObject(this, model_object, model_object->raw_bounding_box());
    if (idx != -1) {
        delete this->objects[idx];
        this->objects[idx] = object;
    } else
        this->objects.emplace_back(object);
    // Invalidate all print steps.
    this->invalidate_all_steps();

    for (size_t volume_id = 0; volume_id < model_object->volumes.size(); ++ volume_id) {
        // Get the config applied to this volume.
        PrintRegionConfig config = this->_region_config_from_model_volume(*model_object->volumes[volume_id]);
        // Find an existing print region with the same config.
        size_t region_id = size_t(-1);
        for (size_t i = 0; i < this->regions.size(); ++ i)
            if (config.equals(this->regions[i]->config)) {
                region_id = i;
                break;
            }
        // If no region exists with the same config, create a new one.
        if (region_id == size_t(-1)) {
            region_id = this->regions.size();
            this->add_region()->config.apply(config);
        }
        // Assign volume to a region.
        object->add_region_volume(region_id, volume_id);
    }

    // Apply config to print object.
    object->config.apply(this->default_object_config);
    normalize_and_apply_config(object->config, model_object->config);
    
    // update placeholders
    {
        // get the first input file name
        std::string input_file;
        std::vector<std::string> v_scale;
        for (const PrintObject *object : this->objects) {
            const ModelObject &mobj = *object->model_object();
            v_scale.push_back(boost::lexical_cast<std::string>(mobj.instances[0]->scaling_factor*100) + "%");
            if (input_file.empty())
                input_file = mobj.input_file;
        }
        
        PlaceholderParser &pp = this->placeholder_parser;
        pp.set("scale", v_scale);
        if (! input_file.empty()) {
            // get basename with and without suffix
            const std::string input_basename = boost::filesystem::path(input_file).filename().string();
            pp.set("input_filename", input_basename);
            const std::string input_basename_base = input_basename.substr(0, input_basename.find_last_of("."));
            pp.set("input_filename_base", input_basename_base);
        }
    }
}

bool Print::apply_config(DynamicPrintConfig config)
{
    // we get a copy of the config object so we can modify it safely
    config.normalize();
    
    // apply variables to placeholder parser
    this->placeholder_parser.apply_config(config);
    
    // handle changes to print config
    t_config_option_keys print_diff = this->config.diff(config);
    this->config.apply_only(config, print_diff, true);
    bool invalidated = this->invalidate_state_by_config_options(print_diff);
    
    // handle changes to object config defaults
    this->default_object_config.apply(config, true);
    for (PrintObject *object : this->objects) {
        // we don't assume that config contains a full ObjectConfig,
        // so we base it on the current print-wise default
        PrintObjectConfig new_config = this->default_object_config;
        // we override the new config with object-specific options
        normalize_and_apply_config(new_config, object->model_object()->config);
        // Force a refresh of a variable layer height profile at the PrintObject if it is not valid.
        if (! object->layer_height_profile_valid) {
            // The layer_height_profile is not valid for some reason (updated by the user or invalidated due to some option change).
            // Invalidate the slicing step, which in turn invalidates everything.
            object->invalidate_step(posSlice);
            // Trigger recalculation.
            invalidated = true;
        }
        // check whether the new config is different from the current one
        t_config_option_keys diff = object->config.diff(new_config);
        object->config.apply_only(new_config, diff, true);
        invalidated |= object->invalidate_state_by_config_options(diff);
    }
    
    // handle changes to regions config defaults
    this->default_region_config.apply(config, true);
    
    // All regions now have distinct settings.
    // Check whether applying the new region config defaults we'd get different regions.
    bool rearrange_regions = false;
    {
        // Collect the already visited region configs into other_region_configs,
        // so one may check for duplicates.
        std::vector<PrintRegionConfig> other_region_configs;
        for (size_t region_id = 0; region_id < this->regions.size(); ++ region_id) {
            PrintRegion       &region = *this->regions[region_id];
            PrintRegionConfig  this_region_config;
            bool               this_region_config_set = false;
            for (PrintObject *object : this->objects) {
                if (region_id < object->region_volumes.size()) {
                    for (int volume_id : object->region_volumes[region_id]) {
                        const ModelVolume &volume = *object->model_object()->volumes[volume_id];
                        if (this_region_config_set) {
                            // If the new config for this volume differs from the other
                            // volume configs currently associated to this region, it means
                            // the region subdivision does not make sense anymore.
                            if (! this_region_config.equals(this->_region_config_from_model_volume(volume))) {
                                rearrange_regions = true;
                                goto exit_for_rearrange_regions;
                            }
                        } else {
                            this_region_config = this->_region_config_from_model_volume(volume);
                            this_region_config_set = true;
                        }
                        for (const PrintRegionConfig &cfg : other_region_configs) {
                            // If the new config for this volume equals any of the other
                            // volume configs that are not currently associated to this
                            // region, it means the region subdivision does not make
                            // sense anymore.
                            if (cfg.equals(this_region_config)) {
                                rearrange_regions = true;
                                goto exit_for_rearrange_regions;
                            }
                        }
                    }
                }
            }
            if (this_region_config_set) {
                t_config_option_keys diff = region.config.diff(this_region_config);
                if (! diff.empty()) {
                    region.config.apply_only(this_region_config, diff);
                    for (PrintObject *object : this->objects)
                        if (region_id < object->region_volumes.size() && ! object->region_volumes[region_id].empty())
                            invalidated |= object->invalidate_state_by_config_options(diff);
                }
                other_region_configs.emplace_back(std::move(this_region_config));
            }
        }
    }

exit_for_rearrange_regions:
    
    if (rearrange_regions) {
        // The current subdivision of regions does not make sense anymore.
        // We need to remove all objects and re-add them.
        ModelObjectPtrs model_objects;
        model_objects.reserve(this->objects.size());
        for (PrintObject *object : this->objects)
            model_objects.push_back(object->model_object());
        this->clear_objects();
        for (ModelObject *mo : model_objects)
            this->add_model_object(mo);
        invalidated = true;
    }

    // Always make sure that the layer_height_profiles are set, as they should not be modified from the worker threads.
    for (PrintObject *object : this->objects)
        if (! object->layer_height_profile_valid)
            object->update_layer_height_profile();
    
    return invalidated;
}

bool Print::has_infinite_skirt() const
{
    return (this->config.skirt_height == -1 && this->config.skirts > 0)
        || (this->config.ooze_prevention && this->extruders().size() > 1);
}

bool Print::has_skirt() const
{
    return (this->config.skirt_height > 0 && this->config.skirts > 0)
        || this->has_infinite_skirt();
}

std::string Print::validate() const
{
    BoundingBox bed_box_2D = get_extents(Polygon::new_scale(config.bed_shape.values));
    BoundingBoxf3 print_volume(Pointf3(unscale(bed_box_2D.min.x), unscale(bed_box_2D.min.y), 0.0), Pointf3(unscale(bed_box_2D.max.x), unscale(bed_box_2D.max.y), config.max_print_height));
    // Allow the objects to protrude below the print bed, only the part of the object above the print bed will be sliced.
    print_volume.min.z = -1e10;
    for (PrintObject *po : this->objects) {
        if (! print_volume.contains(po->model_object()->tight_bounding_box(false)))
            return "Some objects are outside of the print volume.";
    }

    if (this->config.complete_objects) {
        // Check horizontal clearance.
        {
            Polygons convex_hulls_other;
            for (PrintObject *object : this->objects) {
                // Get convex hull of all meshes assigned to this print object.
                Polygon convex_hull;
                {
                    Polygons mesh_convex_hulls;
                    for (const std::vector<int> &volumes : object->region_volumes)
                        for (int volume_id : volumes)
                            mesh_convex_hulls.emplace_back(object->model_object()->volumes[volume_id]->mesh.convex_hull());
                    // make a single convex hull for all of them
                    convex_hull = Slic3r::Geometry::convex_hull(mesh_convex_hulls);
                }
                // Apply the same transformations we apply to the actual meshes when slicing them.
                object->model_object()->instances.front()->transform_polygon(&convex_hull);
                // Grow convex hull with the clearance margin.
                convex_hull = offset(convex_hull, scale_(this->config.extruder_clearance_radius.value)/2, jtRound, scale_(0.1)).front();
                // Now we check that no instance of convex_hull intersects any of the previously checked object instances.
                for (const Point &copy : object->_shifted_copies) {
                    Polygon p = convex_hull;
                    p.translate(copy);
                    if (! intersection(convex_hulls_other, p).empty())
                        return "Some objects are too close; your extruder will collide with them.";
                    polygons_append(convex_hulls_other, p);
                }
            }
        }
        // Check vertical clearance.
        {
            std::vector<coord_t> object_height;
            for (const PrintObject *object : this->objects)
                object_height.insert(object_height.end(), object->copies().size(), object->size.z);
            std::sort(object_height.begin(), object_height.end());
            // Ignore the tallest *copy* (this is why we repeat height for all of them):
            // it will be printed as last one so its height doesn't matter.
            object_height.pop_back();
            if (! object_height.empty() && object_height.back() > scale_(this->config.extruder_clearance_height.value))
                return "Some objects are too tall and cannot be printed without extruder collisions.";
        }
    } // end if (this->config.complete_objects)

    if (this->config.spiral_vase) {
        size_t total_copies_count = 0;
        for (const PrintObject *object : this->objects)
            total_copies_count += object->copies().size();
        // #4043
        if (total_copies_count > 1 && ! this->config.complete_objects.value)
            return "The Spiral Vase option can only be used when printing a single object.";
        if (this->regions.size() > 1)
            return "The Spiral Vase option can only be used when printing single material objects.";
    }

    if (this->config.single_extruder_multi_material) {
        for (size_t i=1; i<this->config.nozzle_diameter.values.size(); ++i)
            if (this->config.nozzle_diameter.values[i] != this->config.nozzle_diameter.values[i-1])
                return "All extruders must have the same diameter for single extruder multimaterial printer.";
    }

    if (this->has_wipe_tower() && ! this->objects.empty()) {
        #if 0
        for (auto dmr : this->config.nozzle_diameter.values)
            if (std::abs(dmr - 0.4) > EPSILON)
                return "The Wipe Tower is currently only supported for the 0.4mm nozzle diameter.";
        #endif
        if (this->config.gcode_flavor != gcfRepRap && this->config.gcode_flavor != gcfMarlin)
            return "The Wipe Tower is currently only supported for the Marlin and RepRap/Sprinter G-code flavors.";
        if (! this->config.use_relative_e_distances)
            return "The Wipe Tower is currently only supported with the relative extruder addressing (use_relative_e_distances=1).";
        SlicingParameters slicing_params0 = this->objects.front()->slicing_parameters();
        for (PrintObject *object : this->objects) {
            SlicingParameters slicing_params = object->slicing_parameters();
            if (std::abs(slicing_params.first_print_layer_height - slicing_params0.first_print_layer_height) > EPSILON ||
                std::abs(slicing_params.layer_height             - slicing_params0.layer_height            ) > EPSILON)
                return "The Wipe Tower is only supported for multiple objects if they have equal layer heigths";
            if (slicing_params.raft_layers() != slicing_params0.raft_layers())
                return "The Wipe Tower is only supported for multiple objects if they are printed over an equal number of raft layers";
            if (object->config.support_material_contact_distance != this->objects.front()->config.support_material_contact_distance)
                return "The Wipe Tower is only supported for multiple objects if they are printed with the same support_material_contact_distance";
            if (! equal_layering(slicing_params, slicing_params0))
                return "The Wipe Tower is only supported for multiple objects if they are sliced equally.";
            bool was_layer_height_profile_valid = object->layer_height_profile_valid;
            object->update_layer_height_profile();
            object->layer_height_profile_valid = was_layer_height_profile_valid;

            if ( this->config.variable_layer_height ) {
                PrintObject* first_object = this->objects.front();
                int i = 0;
                while ( i < first_object->layer_height_profile.size() && i < object->layer_height_profile.size() ) {
                    if (std::abs(first_object->layer_height_profile[i] - object->layer_height_profile[i]) > EPSILON )
                        return "The Wipe tower is only supported if all objects have the same layer height profile";
                    ++i;
                }
            }

            /*for (size_t i = 5; i < object->layer_height_profile.size(); i += 2)
                if (object->layer_height_profile[i-1] > slicing_params.object_print_z_min + EPSILON &&
                    std::abs(object->layer_height_profile[i] - object->config.layer_height) > EPSILON)
                    return "The Wipe Tower is currently only supported with constant Z layer spacing. Layer editing is not allowed.";*/
        }
    }
    
    {
        // find the smallest nozzle diameter
        std::vector<unsigned int> extruders = this->extruders();
        if (extruders.empty())
            return "The supplied settings will cause an empty print.";
        
        std::vector<double> nozzle_diameters;
        for (unsigned int extruder_id : extruders)
            nozzle_diameters.push_back(this->config.nozzle_diameter.get_at(extruder_id));
        double min_nozzle_diameter = *std::min_element(nozzle_diameters.begin(), nozzle_diameters.end());

        unsigned int total_extruders_count = this->config.nozzle_diameter.size();
        for (const auto& extruder_idx : extruders)
            if ( extruder_idx >= total_extruders_count )
                return "One or more object were assigned an extruder that the printer does not have.";

        for (PrintObject *object : this->objects) {
            if ((object->config.support_material_extruder == -1 || object->config.support_material_interface_extruder == -1) &&
                (object->config.raft_layers > 0 || object->config.support_material.value)) {
                // The object has some form of support and either support_material_extruder or support_material_interface_extruder
                // will be printed with the current tool without a forced tool change. Play safe, assert that all object nozzles
                // are of the same diameter.
                if (nozzle_diameters.size() > 1)
                    return "Printing with multiple extruders of differing nozzle diameters. "
                           "If support is to be printed with the current extruder (support_material_extruder == 0 or support_material_interface_extruder == 0), "
                           "all nozzles have to be of the same diameter.";
            }
            
            // validate first_layer_height
            double first_layer_height = object->config.get_abs_value("first_layer_height");
            double first_layer_min_nozzle_diameter;
            if (object->config.raft_layers > 0) {
                // if we have raft layers, only support material extruder is used on first layer
                size_t first_layer_extruder = object->config.raft_layers == 1
                    ? object->config.support_material_interface_extruder-1
                    : object->config.support_material_extruder-1;
                first_layer_min_nozzle_diameter = (first_layer_extruder == size_t(-1)) ? 
                    min_nozzle_diameter : 
                    this->config.nozzle_diameter.get_at(first_layer_extruder);
            } else {
                // if we don't have raft layers, any nozzle diameter is potentially used in first layer
                first_layer_min_nozzle_diameter = min_nozzle_diameter;
            }
            if (first_layer_height > first_layer_min_nozzle_diameter)
                return "First layer height can't be greater than nozzle diameter";
            
            // validate layer_height
            if (object->config.layer_height.value > min_nozzle_diameter)
                return "Layer height can't be greater than nozzle diameter";
        }
    }

    return std::string();
}

// the bounding box of objects placed in copies position
// (without taking skirt/brim/support material into account)
BoundingBox Print::bounding_box() const
{
    BoundingBox bb;
    for (const PrintObject *object : this->objects)
        for (Point copy : object->_shifted_copies) {
            bb.merge(copy);
            copy.translate(object->size);
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
    Flow perimeter_flow = this->objects.front()->get_layer(0)->get_region(0)->flow(frPerimeter);
    double extra = perimeter_flow.width/2;
    
    // consider support material
    if (this->has_support_material()) {
        extra = std::max(extra, SUPPORT_MATERIAL_MARGIN);
    }
    
    // consider brim and skirt
    if (this->config.brim_width.value > 0) {
        Flow brim_flow = this->brim_flow();
        extra = std::max(extra, this->config.brim_width.value + brim_flow.width/2);
    }
    if (this->has_skirt()) {
        int skirts = this->config.skirts.value;
        if (skirts == 0 && this->has_infinite_skirt()) skirts = 1;
        Flow skirt_flow = this->skirt_flow();
        extra = std::max(
            extra,
            this->config.brim_width.value
                + this->config.skirt_distance.value
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
    if (this->objects.empty()) CONFESS("skirt_first_layer_height() can't be called without PrintObjects");
    return this->objects.front()->config.get_abs_value("first_layer_height");
}

Flow Print::brim_flow() const
{
    ConfigOptionFloatOrPercent width = this->config.first_layer_extrusion_width;
    if (width.value == 0) 
        width = this->regions.front()->config.perimeter_extrusion_width;
    if (width.value == 0) 
        width = this->objects.front()->config.extrusion_width;
    
    /* We currently use a random region's perimeter extruder.
       While this works for most cases, we should probably consider all of the perimeter
       extruders and take the one with, say, the smallest index.
       The same logic should be applied to the code that selects the extruder during G-code
       generation as well. */
    return Flow::new_from_config_width(
        frPerimeter,
        width, 
        this->config.nozzle_diameter.get_at(this->regions.front()->config.perimeter_extruder-1),
        this->skirt_first_layer_height(),
        0
    );
}

Flow Print::skirt_flow() const
{
    ConfigOptionFloatOrPercent width = this->config.first_layer_extrusion_width;
    if (width.value == 0) 
        width = this->regions.front()->config.perimeter_extrusion_width;
    if (width.value == 0)
        width = this->objects.front()->config.extrusion_width;
    
    /* We currently use a random object's support material extruder.
       While this works for most cases, we should probably consider all of the support material
       extruders and take the one with, say, the smallest index;
       The same logic should be applied to the code that selects the extruder during G-code
       generation as well. */
    return Flow::new_from_config_width(
        frPerimeter,
        width, 
        this->config.nozzle_diameter.get_at(this->objects.front()->config.support_material_extruder-1),
        this->skirt_first_layer_height(),
        0
    );
}

PrintRegionConfig Print::_region_config_from_model_volume(const ModelVolume &volume)
{
    PrintRegionConfig config = this->default_region_config;
    normalize_and_apply_config(config, volume.get_object()->config);
    normalize_and_apply_config(config, volume.config);
    if (! volume.material_id().empty())
        normalize_and_apply_config(config, volume.material()->config);
    return config;
}

bool Print::has_support_material() const
{
    for (const PrintObject *object : this->objects)
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
    
//    size_t extruders = this->config.nozzle_diameter.values.size();
    for (size_t volume_id = 0; volume_id < model_object->volumes.size(); ++ volume_id) {
        ModelVolume *volume = model_object->volumes[volume_id];
        //FIXME Vojtech: This assigns an extruder ID even to a modifier volume, if it has a material assigned.
        if (! volume->material_id().empty() && ! volume->config.has("extruder"))
            volume->config.opt<ConfigOptionInt>("extruder", true)->value = int(volume_id + 1);
    }
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
    for (const PrintObject *object : this->objects) {
        size_t skirt_layers = this->has_infinite_skirt() ? 
            object->layer_count() : 
            std::min(size_t(this->config.skirt_height.value), object->layer_count());
        skirt_height_z = std::max(skirt_height_z, object->layers[skirt_layers-1]->print_z);
    }
    
    // Collect points from all layers contained in skirt height.
    Points points;
    for (const PrintObject *object : this->objects) {
        Points object_points;
        // Get object layers up to skirt_height_z.
        for (const Layer *layer : object->layers) {
            if (layer->print_z > skirt_height_z)
                break;
            for (const ExPolygon &expoly : layer->slices.expolygons)
                // Collect the outer contour points only, ignore holes for the calculation of the convex hull.
                append(object_points, expoly.contour.points);
        }
        // Get support layers up to skirt_height_z.
        for (const SupportLayer *layer : object->support_layers) {
            if (layer->print_z > skirt_height_z)
                break;
            for (const ExtrusionEntity *extrusion_entity : layer->support_fills.entities)
                append(object_points, extrusion_entity->as_polyline().points);
        }
        // Repeat points for each object copy.
        for (const Point &shift : object->_shifted_copies) {
            Points copy_points = object_points;
            for (Point &pt : copy_points)
                pt.translate(shift);
            append(points, copy_points);
        }
    }

    if (points.size() < 3)
        // At least three points required for a convex hull.
        return;
    
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
            extruders_e_per_mm.push_back(Extruder((unsigned int)extruder_id, &this->config).e_per_mm(mm3_per_mm));
        }
    }

    // Number of skirt loops per skirt layer.
    int n_skirts = this->config.skirts.value;
    if (this->has_infinite_skirt() && n_skirts == 0)
        n_skirts = 1;

    // Initial offset of the brim inner edge from the object (possible with a support & raft).
    // The skirt will touch the brim if the brim is extruded.
    coord_t distance = scale_(std::max(this->config.skirt_distance.value, this->config.brim_width.value));
    // Draw outlines from outside to inside.
    // Loop while we have less skirts than required or any extruder hasn't reached the min length if any.
    std::vector<coordf_t> extruded_length(extruders.size(), 0.);
    for (int i = n_skirts, extruder_idx = 0; i > 0; -- i) {
        // Offset the skirt outside.
        distance += coord_t(scale_(spacing));
        // Generate the skirt centerline.
        Polygon loop;
        {
            Polygons loops = offset(convex_hull, distance, ClipperLib::jtRound, scale_(0.1));
            Geometry::simplify_polygons(loops, scale_(0.05), &loops);
            loop = loops.front();
        }
        // Extrude the skirt loop.
        ExtrusionLoop eloop(elrSkirt);
        eloop.paths.emplace_back(ExtrusionPath(
            ExtrusionPath(
                erSkirt,
                mm3_per_mm,         // this will be overridden at G-code export time
                flow.width,
                first_layer_height  // this will be overridden at G-code export time
            )));
        eloop.paths.back().polyline = loop.split_at_first_point();
        this->skirt.append(eloop);
        if (this->config.min_skirt_length.value > 0) {
            // The skirt length is limited. Sum the total amount of filament length extruded, in mm.
            extruded_length[extruder_idx] += unscale(loop.length()) * extruders_e_per_mm[extruder_idx];
            if (extruded_length[extruder_idx] < this->config.min_skirt_length.value) {
                // Not extruded enough yet with the current extruder. Add another loop.
                if (i == 1)
                    ++ i;
            } else {
                assert(extruded_length[extruder_idx] >= this->config.min_skirt_length.value);
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
    this->skirt.reverse();
}

void Print::_make_brim()
{
    // Brim is only printed on first layer and uses perimeter extruder.
    Flow        flow = this->brim_flow();
    Polygons    islands;
    for (PrintObject *object : this->objects) {
        Polygons object_islands;
        for (ExPolygon &expoly : object->layers.front()->slices.expolygons)
            object_islands.push_back(expoly.contour);
        if (! object->support_layers.empty())
            object->support_layers.front()->support_fills.polygons_covered_by_spacing(object_islands, float(SCALED_EPSILON));
        islands.reserve(islands.size() + object_islands.size() * object->_shifted_copies.size());
        for (const Point &pt : object->_shifted_copies)
            for (Polygon &poly : object_islands) {
                islands.push_back(poly);
                islands.back().translate(pt);
            }
    }
    Polygons loops;
    size_t num_loops = size_t(floor(this->config.brim_width.value / flow.width));
    for (size_t i = 0; i < num_loops; ++ i) {
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
    std::reverse(loops.begin(), loops.end());
    extrusion_entities_append_loops(this->brim.entities, std::move(loops), erSkirt, float(flow.mm3_per_mm()), float(flow.width), float(this->skirt_first_layer_height()));
}

// Wipe tower support.
bool Print::has_wipe_tower() const
{
    return 
        this->config.single_extruder_multi_material.value && 
        ! this->config.spiral_vase.value &&
        this->config.wipe_tower.value && 
        this->config.nozzle_diameter.values.size() > 1;
}

void Print::_clear_wipe_tower()
{
    m_tool_ordering.clear();
    m_wipe_tower_priming.reset(nullptr);
    m_wipe_tower_tool_changes.clear();
    m_wipe_tower_final_purge.reset(nullptr);
}

void Print::_make_wipe_tower()
{
    this->_clear_wipe_tower();
    if (! this->has_wipe_tower())
        return;

    // Let the ToolOrdering class know there will be initial priming extrusions at the start of the print.
    m_tool_ordering = ToolOrdering(*this, (unsigned int)-1, true);
    if (! m_tool_ordering.has_wipe_tower())
        // Don't generate any wipe tower.
        return;

    // Check whether there are any layers in m_tool_ordering, which are marked with has_wipe_tower,
    // they print neither object, nor support. These layers are above the raft and below the object, and they
    // shall be added to the support layers to be printed.
    // see https://github.com/prusa3d/Slic3r/issues/607
    {
        size_t idx_begin = size_t(-1);
        size_t idx_end   = m_tool_ordering.layer_tools().size();
        // Find the first wipe tower layer, which does not have a counterpart in an object or a support layer.
        for (size_t i = 0; i < idx_end; ++ i) {
            const ToolOrdering::LayerTools &lt = m_tool_ordering.layer_tools()[i];
            if (lt.has_wipe_tower && ! lt.has_object && ! lt.has_support) {
                idx_begin = i;
                break;
            }
        }
        if (idx_begin != size_t(-1)) {
            // Find the position in this->objects.first()->support_layers to insert these new support layers.
            double wipe_tower_new_layer_print_z_first = m_tool_ordering.layer_tools()[idx_begin].print_z;
            SupportLayerPtrs::iterator it_layer = this->objects.front()->support_layers.begin();
            SupportLayerPtrs::iterator it_end   = this->objects.front()->support_layers.end();
            for (; it_layer != it_end && (*it_layer)->print_z - EPSILON < wipe_tower_new_layer_print_z_first; ++ it_layer);
            // Find the stopper of the sequence of wipe tower layers, which do not have a counterpart in an object or a support layer.
            for (size_t i = idx_begin; i < idx_end; ++ i) {
                ToolOrdering::LayerTools &lt = const_cast<ToolOrdering::LayerTools&>(m_tool_ordering.layer_tools()[i]);
                if (! (lt.has_wipe_tower && ! lt.has_object && ! lt.has_support))
                    break;
                lt.has_support = true;
                // Insert the new support layer.
                double height    = lt.print_z - m_tool_ordering.layer_tools()[i-1].print_z;
                //FIXME the support layer ID is set to -1, as Vojtech hopes it is not being used anyway.
                auto  *new_layer = new SupportLayer(size_t(-1), this->objects.front(),
                    height, lt.print_z, lt.print_z - 0.5 * height);
                it_layer = this->objects.front()->support_layers.insert(it_layer, new_layer);
                ++ it_layer;
            }
        }
    }

    // Get wiping matrix to get number of extruders and convert vector<double> to vector<float>:
    std::vector<float> wiping_volumes((this->config.wiping_volumes_matrix.values).begin(),(this->config.wiping_volumes_matrix.values).end());

    // Initialize the wipe tower.
    WipeTowerPrusaMM wipe_tower(
        float(this->config.wipe_tower_x.value),     float(this->config.wipe_tower_y.value), 
        float(this->config.wipe_tower_width.value),
        float(this->config.wipe_tower_rotation_angle.value), float(this->config.cooling_tube_retraction.value),
        float(this->config.cooling_tube_length.value), float(this->config.parking_pos_retraction.value),
        float(this->config.wipe_tower_bridging), wiping_volumes, m_tool_ordering.first_extruder());

    //wipe_tower.set_retract();
    //wipe_tower.set_zhop();

    // Set the extruder & material properties at the wipe tower object.
    for (size_t i = 0; i < (int)(sqrt(wiping_volumes.size())+EPSILON); ++ i)
        wipe_tower.set_extruder(
            i, 
            WipeTowerPrusaMM::parse_material(this->config.filament_type.get_at(i).c_str()),
            this->config.temperature.get_at(i),
            this->config.first_layer_temperature.get_at(i),
            this->config.filament_loading_speed.get_at(i),
            this->config.filament_unloading_speed.get_at(i),
            this->config.filament_toolchange_delay.get_at(i),
            this->config.filament_cooling_time.get_at(i),
            this->config.filament_ramming_parameters.get_at(i),
            this->config.nozzle_diameter.get_at(i));

    // When printing the first layer's wipe tower, the first extruder is expected to be active and primed.
    // Therefore the number of wipe sections at the wipe tower will be (m_tool_ordering.front().extruders-1) at the 1st layer.
    // The following variable is true if the last priming section cannot be squeezed inside the wipe tower.
    bool last_priming_wipe_full = m_tool_ordering.front().extruders.size() > m_tool_ordering.front().wipe_tower_partitions;

    m_wipe_tower_priming = Slic3r::make_unique<WipeTower::ToolChangeResult>(
        wipe_tower.prime(this->skirt_first_layer_height(), m_tool_ordering.all_extruders(), ! last_priming_wipe_full));


    // Lets go through the wipe tower layers and determine pairs of extruder changes for each
    // to pass to wipe_tower (so that it can use it for planning the layout of the tower)
    {
        unsigned int current_extruder_id = m_tool_ordering.all_extruders().back();
        for (const auto &layer_tools : m_tool_ordering.layer_tools()) { // for all layers
            if (!layer_tools.has_wipe_tower) continue;
            bool first_layer = &layer_tools == &m_tool_ordering.front();
            wipe_tower.plan_toolchange(layer_tools.print_z, layer_tools.wipe_tower_layer_height, current_extruder_id, current_extruder_id,false);
            for (const auto extruder_id : layer_tools.extruders) {
                if ((first_layer && extruder_id == m_tool_ordering.all_extruders().back()) || extruder_id != current_extruder_id) {
                    wipe_tower.plan_toolchange(layer_tools.print_z, layer_tools.wipe_tower_layer_height, current_extruder_id, extruder_id, first_layer && extruder_id == m_tool_ordering.all_extruders().back());
                    current_extruder_id = extruder_id;
                }
            }
            if (&layer_tools == &m_tool_ordering.back() || (&layer_tools + 1)->wipe_tower_partitions == 0)
                break;
        }
    }

    

    // Generate the wipe tower layers.
    m_wipe_tower_tool_changes.reserve(m_tool_ordering.layer_tools().size());
    wipe_tower.generate(m_wipe_tower_tool_changes);
    
    // Set current_extruder_id to the last extruder primed.
    /*unsigned int current_extruder_id = m_tool_ordering.all_extruders().back();

    for (const ToolOrdering::LayerTools &layer_tools : m_tool_ordering.layer_tools()) {
        if (! layer_tools.has_wipe_tower)
            // This is a support only layer, or the wipe tower does not reach to this height.
            continue;
        bool first_layer = &layer_tools == &m_tool_ordering.front();
        bool last_layer  = &layer_tools == &m_tool_ordering.back() || (&layer_tools + 1)->wipe_tower_partitions == 0;
        wipe_tower.set_layer(
            float(layer_tools.print_z), 
            float(layer_tools.wipe_tower_layer_height),
            layer_tools.wipe_tower_partitions,
            first_layer,
            last_layer);
        std::vector<WipeTower::ToolChangeResult> tool_changes;
        for (unsigned int extruder_id : layer_tools.extruders)
            // Call the wipe_tower.tool_change() at the first layer for the initial extruder 
            // to extrude the wipe tower brim,
            if ((first_layer && extruder_id == m_tool_ordering.all_extruders().back()) || 
            // or when an extruder shall be switched.
                extruder_id != current_extruder_id) {
                tool_changes.emplace_back(wipe_tower.tool_change(extruder_id, extruder_id == layer_tools.extruders.back(), WipeTower::PURPOSE_EXTRUDE));
                current_extruder_id = extruder_id;
            }
        if (! wipe_tower.layer_finished()) {
            tool_changes.emplace_back(wipe_tower.finish_layer(WipeTower::PURPOSE_EXTRUDE));
            if (tool_changes.size() > 1) {
                // Merge the two last tool changes into one.
                WipeTower::ToolChangeResult &tc1 = tool_changes[tool_changes.size() - 2];
                WipeTower::ToolChangeResult &tc2 = tool_changes.back();
                if (tc1.end_pos != tc2.start_pos) {
                    // Add a travel move from tc1.end_pos to tc2.start_pos.
                    char buf[2048];
                    sprintf(buf, "G1 X%.3f Y%.3f F7200\n", tc2.start_pos.x, tc2.start_pos.y);
                    tc1.gcode += buf;
                }
                tc1.gcode += tc2.gcode;
                append(tc1.extrusions, tc2.extrusions);
                tc1.end_pos = tc2.end_pos;
                tool_changes.pop_back();
            }
        }
        m_wipe_tower_tool_changes.emplace_back(std::move(tool_changes));
        if (last_layer)
            break;
    }*/
    
    // Unload the current filament over the purge tower.
    coordf_t layer_height = this->objects.front()->config.layer_height.value;
    if (m_tool_ordering.back().wipe_tower_partitions > 0) {
        // The wipe tower goes up to the last layer of the print.
        if (wipe_tower.layer_finished()) {
            // The wipe tower is printed to the top of the print and it has no space left for the final extruder purge.
            // Lift Z to the next layer.
            wipe_tower.set_layer(float(m_tool_ordering.back().print_z + layer_height), float(layer_height), 0, false, true);
        } else {
            // There is yet enough space at this layer of the wipe tower for the final purge.
        }
    } else {
        // The wipe tower does not reach the last print layer, perform the pruge at the last print layer.
        assert(m_tool_ordering.back().wipe_tower_partitions == 0);
        wipe_tower.set_layer(float(m_tool_ordering.back().print_z), float(layer_height), 0, false, true);
    }
    m_wipe_tower_final_purge = Slic3r::make_unique<WipeTower::ToolChangeResult>(
		wipe_tower.tool_change((unsigned int)-1, false));
}

std::string Print::output_filename()
{
    this->placeholder_parser.update_timestamp();
    try {
        return this->placeholder_parser.process(this->config.output_filename_format.value, 0);
    } catch (std::runtime_error &err) {
        throw std::runtime_error(std::string("Failed processing of the output_filename_format template.\n") + err.what());
    }
}

std::string Print::output_filepath(const std::string &path)
{
    // if we were supplied no path, generate an automatic one based on our first object's input file
    if (path.empty()) {
        // get the first input file name
        std::string input_file;
        for (const PrintObject *object : this->objects) {
            input_file = object->model_object()->input_file;
            if (! input_file.empty())
                break;
        }
        return (boost::filesystem::path(input_file).parent_path() / this->output_filename()).make_preferred().string();
    }
    
    // if we were supplied a directory, use it and append our automatically generated filename
    boost::filesystem::path p(path);
    if (boost::filesystem::is_directory(p))
        return (p / this->output_filename()).make_preferred().string();
    
    // if we were supplied a file which is not a directory, use it
    return path;
}

void Print::set_status(int percent, const std::string &message)
{
    printf("Print::status %d => %s\n", percent, message.c_str());
}

}
