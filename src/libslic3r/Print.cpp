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
#include <algorithm>
#include <unordered_set>
#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>

#include "PrintExport.hpp"

//! macro used to mark string used at localization, 
//! return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {

template class PrintState<PrintStep, psCount>;
template class PrintState<PrintObjectStep, posCount>;

void Print::clear_objects()
{
    tbb::mutex::scoped_lock lock(m_mutex);
	for (PrintObject *object : m_objects)
		delete object;
	m_objects.clear();
    for (PrintRegion *region : m_regions)
        delete region;
    m_regions.clear();
	this->invalidate_all_steps();
}

void Print::delete_object(size_t idx)
{
    tbb::mutex::scoped_lock lock(m_mutex);
    // destroy object and remove it from our container
    delete m_objects[idx];
    m_objects.erase(m_objects.begin() + idx);
    this->invalidate_all_steps();
    // TODO: purge unused regions
}

void Print::reload_object(size_t /* idx */)
{
	ModelObjectPtrs model_objects;
	{
		tbb::mutex::scoped_lock lock(m_mutex);
		/* TODO: this method should check whether the per-object config and per-material configs
			have changed in such a way that regions need to be rearranged or we can just apply
			the diff and invalidate something.  Same logic as apply_config()
			For now we just re-add all objects since we haven't implemented this incremental logic yet.
			This should also check whether object volumes (parts) have changed. */
		// collect all current model objects
		model_objects.reserve(m_objects.size());
		for (PrintObject *object : m_objects)
			model_objects.push_back(object->model_object());
		// remove our print objects
		for (PrintObject *object : m_objects)
			delete object;
		m_objects.clear();
		for (PrintRegion *region : m_regions)
			delete region;
		m_regions.clear();
		this->invalidate_all_steps();
	}
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
    tbb::mutex::scoped_lock lock(m_mutex);
    bool invalidated = false;
    for (PrintObject *object : m_objects)
        invalidated |= object->reload_model_instances();
    return invalidated;
}

PrintObjectPtrs Print::get_printable_objects() const
{
    PrintObjectPtrs printable_objects(m_objects);
    printable_objects.erase(std::remove_if(printable_objects.begin(), printable_objects.end(), [](PrintObject* o) { return !o->is_printable(); }), printable_objects.end());
    return printable_objects;
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

// Called by Print::apply_config().
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
    bool invalidated = m_state.invalidate(step, m_mutex, m_cancel_callback);
    // Propagate to dependent steps.
    //FIXME Why should skirt invalidate brim? Shouldn't it be vice versa?
    if (step == psSkirt)
        invalidated |= m_state.invalidate(psBrim, m_mutex, m_cancel_callback);
    return invalidated;
}

// returns true if an object step is done on all objects
// and there's at least one object
bool Print::is_step_done(PrintObjectStep step) const
{
    if (m_objects.empty())
        return false;
    for (const PrintObject *object : m_objects)
        if (!object->m_state.is_done(step))
            return false;
    return true;
}

// returns 0-based indices of used extruders
std::vector<unsigned int> Print::object_extruders() const
{
    std::vector<unsigned int> extruders;
    
    for (PrintRegion* region : m_regions) {
        // these checks reflect the same logic used in the GUI for enabling/disabling
        // extruder selection fields
        if (region->config().perimeters.value > 0 || m_config.brim_width.value > 0)
            extruders.push_back(region->config().perimeter_extruder - 1);
        if (region->config().fill_density.value > 0)
            extruders.push_back(region->config().infill_extruder - 1);
        if (region->config().top_solid_layers.value > 0 || region->config().bottom_solid_layers.value > 0)
            extruders.push_back(region->config().solid_infill_extruder - 1);
    }
    
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
        instances += print_object->copies().size();
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

static PrintRegionConfig region_config_from_model_volume(const PrintRegionConfig &default_region_config, const ModelVolume &volume)
{
    PrintRegionConfig config = default_region_config;
    normalize_and_apply_config(config, volume.get_object()->config);
    normalize_and_apply_config(config, volume.config);
    if (! volume.material_id().empty())
        normalize_and_apply_config(config, volume.material()->config);
    return config;
}

// Caller is responsible for supplying models whose objects don't collide
// and have explicit instance positions.
void Print::add_model_object(ModelObject* model_object, int idx)
{
    tbb::mutex::scoped_lock lock(m_mutex);
    // Initialize a new print object and store it at the given position.
    PrintObject *object = new PrintObject(this, model_object, model_object->raw_bounding_box());
    if (idx != -1) {
        delete m_objects[idx];
        m_objects[idx] = object;
    } else
        m_objects.emplace_back(object);
    // Invalidate all print steps.
    this->invalidate_all_steps();

    // Set the transformation matrix without translation from the first instance.
    if (! model_object->instances.empty())
        object->set_trafo(model_object->instances.front()->world_matrix(true));

    size_t volume_id = 0;
    for (const ModelVolume *volume : model_object->volumes) {
        if (! volume->is_model_part() && ! volume->is_modifier())
            continue;
        // Get the config applied to this volume.
        PrintRegionConfig config = region_config_from_model_volume(m_default_region_config, *volume);
        // Find an existing print region with the same config.
        size_t region_id = size_t(-1);
        for (size_t i = 0; i < m_regions.size(); ++ i)
            if (config.equals(m_regions[i]->config())) {
                region_id = i;
                break;
            }
        // If no region exists with the same config, create a new one.
        if (region_id == size_t(-1)) {
            region_id = m_regions.size();
            this->add_region(config);
        }
        // Assign volume to a region.
        object->add_region_volume(region_id, volume_id);
        ++ volume_id;
    }

    // Apply config to print object.
    object->config_apply(this->default_object_config());
    {
        //normalize_and_apply_config(object->config(), model_object->config);
        DynamicPrintConfig src_normalized(model_object->config);
        src_normalized.normalize();
        object->config_apply(src_normalized, true);
    }
    
    this->update_object_placeholders();
}

bool Print::apply_config(DynamicPrintConfig config)
{
    tbb::mutex::scoped_lock lock(m_mutex);

    // we get a copy of the config object so we can modify it safely
    config.normalize();
    
    // apply variables to placeholder parser
    m_placeholder_parser.apply_config(config);
    
    // handle changes to print config
    t_config_option_keys print_diff = m_config.diff(config);
    m_config.apply_only(config, print_diff, true);
    bool invalidated = this->invalidate_state_by_config_options(print_diff);
    
    // handle changes to object config defaults
    m_default_object_config.apply(config, true);
    for (PrintObject *object : m_objects) {
        // we don't assume that config contains a full ObjectConfig,
        // so we base it on the current print-wise default
        PrintObjectConfig new_config = this->default_object_config();
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
        t_config_option_keys diff = object->config().diff(new_config);
        object->config_apply_only(new_config, diff, true);
        invalidated |= object->invalidate_state_by_config_options(diff);
    }
    
    // handle changes to regions config defaults
    m_default_region_config.apply(config, true);
    
    // All regions now have distinct settings.
    // Check whether applying the new region config defaults we'd get different regions.
    bool rearrange_regions = false;
    {
        // Collect the already visited region configs into other_region_configs,
        // so one may check for duplicates.
        std::vector<PrintRegionConfig> other_region_configs;
        for (size_t region_id = 0; region_id < m_regions.size(); ++ region_id) {
            PrintRegion       &region = *m_regions[region_id];
            PrintRegionConfig  this_region_config;
            bool               this_region_config_set = false;
            for (PrintObject *object : m_objects) {
                if (region_id < object->region_volumes.size()) {
                    for (int volume_id : object->region_volumes[region_id]) {
                        const ModelVolume &volume = *object->model_object()->volumes[volume_id];
                        if (this_region_config_set) {
                            // If the new config for this volume differs from the other
                            // volume configs currently associated to this region, it means
                            // the region subdivision does not make sense anymore.
                            if (! this_region_config.equals(region_config_from_model_volume(m_default_region_config, volume))) {
                                rearrange_regions = true;
                                goto exit_for_rearrange_regions;
                            }
                        } else {
                            this_region_config = region_config_from_model_volume(m_default_region_config, volume);
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
                t_config_option_keys diff = region.config().diff(this_region_config);
                if (! diff.empty()) {
                    region.config_apply_only(this_region_config, diff, false);
                    for (PrintObject *object : m_objects)
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
        model_objects.reserve(m_objects.size());
        for (PrintObject *object : m_objects)
            model_objects.push_back(object->model_object());
        this->clear_objects();
        for (ModelObject *mo : model_objects)
            this->add_model_object(mo);
        invalidated = true;
    }

    // Always make sure that the layer_height_profiles are set, as they should not be modified from the worker threads.
    for (PrintObject *object : m_objects)
        if (! object->layer_height_profile_valid)
            object->update_layer_height_profile();
    
    return invalidated;
}

// Test whether the two models contain the same number of ModelObjects with the same set of IDs
// ordered in the same order. In that case it is not necessary to kill the background processing.
static inline bool model_object_list_equal(const Model &model_old, const Model &model_new)
{
    if (model_old.objects.size() != model_new.objects.size())
        return false;
    for (size_t i = 0; i < model_old.objects.size(); ++ i)
        if (model_old.objects[i]->id() != model_new.objects[i]->id())
            return false;
    return true;
}

// Test whether the new model is just an extension of the old model (new objects were added
// to the end of the original list. In that case it is not necessary to kill the background processing.
static inline bool model_object_list_extended(const Model &model_old, const Model &model_new)
{
    if (model_old.objects.size() >= model_new.objects.size())
        return false;
    for (size_t i = 0; i < model_old.objects.size(); ++ i)
        if (model_old.objects[i]->id() != model_new.objects[i]->id())
            return false;
    return true;
}

static inline bool model_volume_list_changed(const ModelObject &model_object_old, const ModelObject &model_object_new, const ModelVolume::Type type)
{
    bool modifiers_differ = false;
    size_t i_old, i_new;
    for (i_old = 0, i_new = 0; i_old < model_object_old.volumes.size() && i_new < model_object_new.volumes.size();) {
        const ModelVolume &mv_old = *model_object_old.volumes[i_old];
        const ModelVolume &mv_new = *model_object_new.volumes[i_old];
        if (mv_old.type() != type) {
            ++ i_old;
            continue;
        }
        if (mv_new.type() != type) {
            ++ i_new;
            continue;
        }
        if (mv_old.id() != mv_new.id())
            return true;
        //FIXME test for the content of the mesh!
        //FIXME test for the transformation matrices!
        ++ i_old;
        ++ i_new;
    }
    for (; i_old < model_object_old.volumes.size(); ++ i_old) {
        const ModelVolume &mv_old = *model_object_old.volumes[i_old];
        if (mv_old.type() == type)
            // ModelVolume was deleted.
            return true;
    }
    for (; i_new < model_object_new.volumes.size(); ++ i_new) {
        const ModelVolume &mv_new = *model_object_new.volumes[i_new];
        if (mv_new.type() == type)
            // ModelVolume was added.
            return true;
    }
    return false;
}

static inline void model_volume_list_update_supports(ModelObject &model_object_dst, const ModelObject &model_object_src)
{
    // 1) Delete the support volumes from model_object_dst.
    {
        std::vector<ModelVolume*> dst;
        dst.reserve(model_object_dst.volumes.size());
        for (ModelVolume *vol : model_object_dst.volumes) {
            if (vol->is_support_modifier())
                dst.emplace_back(vol);
            else
                delete vol;
        }
        model_object_dst.volumes = std::move(dst);
    }
    // 2) Copy the support volumes from model_object_src to the end of model_object_dst.
    for (ModelVolume *vol : model_object_src.volumes) {
        if (vol->is_support_modifier())
            model_object_dst.volumes.emplace_back(vol->clone(&model_object_dst));
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
            const Vec3d &offst = model_instance->get_offset();
            trafo.trafo = model_instance->world_matrix(true);
            trafo.copies.front() = Point::new_scale(offst(0), offst(1));
            auto it = trafos.find(trafo);
            if (it == trafos.end())
                trafos.emplace(trafo);
            else
                const_cast<PrintInstances&>(*it).copies.emplace_back(trafo.copies.front());
        }
    return std::vector<PrintInstances>(trafos.begin(), trafos.end());
}

Print::ApplyStatus Print::apply(const Model &model, const DynamicPrintConfig &config_in)
{
    // Make a copy of the config, normalize it.
    DynamicPrintConfig config(config_in);
    config.normalize();
    // Collect changes to print config.
    t_config_option_keys print_diff  = m_config.diff(config);
    t_config_option_keys object_diff = m_default_object_config.diff(config);
    t_config_option_keys region_diff = m_default_region_config.diff(config);

    // Do not use the ApplyStatus as we will use the max function when updating apply_status. 
    unsigned int apply_status = APPLY_STATUS_UNCHANGED;
    auto update_apply_status = [&apply_status](bool invalidated)
        { apply_status = std::max<unsigned int>(apply_status, invalidated ? APPLY_STATUS_INVALIDATED : APPLY_STATUS_CHANGED); };
    if (! (print_diff.empty() && object_diff.empty() && region_diff.empty()))
        update_apply_status(false);

    // Grab the lock for the Print / PrintObject milestones.
    tbb::mutex::scoped_lock lock(m_mutex);

    // The following call may stop the background processing.
    update_apply_status(this->invalidate_state_by_config_options(print_diff));
    // Apply variables to placeholder parser. The placeholder parser is used by G-code export,
    // which should be stopped if print_diff is not empty.
    if (m_placeholder_parser.apply_config(config))
        update_apply_status(this->invalidate_step(psGCodeExport));

    // It is also safe to change m_config now after this->invalidate_state_by_config_options() call.
    m_config.apply_only(config, print_diff, true);
    // Handle changes to object config defaults
    m_default_object_config.apply_only(config, object_diff, true);
    // Handle changes to regions config defaults
    m_default_region_config.apply_only(config, region_diff, true);
    
    struct ModelObjectStatus {
        enum Status {
            Unknown,
            Old,
            New,
            Moved,
            Deleted,
        };
        ModelObjectStatus(ModelID id, Status status = Unknown) : id(id), status(status) {}
        ModelID                 id;
        Status                  status;
        t_config_option_keys    object_config_diff;
        // Search by id.
        bool operator<(const ModelObjectStatus &rhs) const { return id < rhs.id; }
    };
    std::set<ModelObjectStatus> model_object_status;

    // 1) Synchronize model objects.
    if (model.id() != m_model.id()) {
        // Kill everything, initialize from scratch.
        // Stop background processing.
        m_cancel_callback();
        update_apply_status(this->invalidate_all_steps());
        for (PrintObject *object : m_objects) {
            model_object_status.emplace(object->model_object()->id(), ModelObjectStatus::Deleted);
            delete object;
        }
        m_objects.clear();
        for (PrintRegion *region : m_regions)
            delete region;
        m_regions.clear();
        m_model = model;
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
                m_model.objects.emplace_back(model.objects[i]->clone(&m_model));
            }
        } else {
            // Reorder the objects, add new objects.
            // First stop background processing before shuffling or deleting the PrintObjects in the object list.
            m_cancel_callback();
            this->invalidate_step(psGCodeExport);
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
                    m_model.objects.emplace_back((*it)->clone(&m_model));
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
        PrintObjectStatus(ModelID id) : id(id), print_object(nullptr), trafo(Transform3d::Identity()), status(Unknown) {}
        // ID of the ModelObject & PrintObject
        ModelID          id;
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
    for (size_t idx_model_object = 0; idx_model_object < model.objects.size(); ++ idx_model_object) {
        ModelObject &model_object = *m_model.objects[idx_model_object];
        auto it_status = model_object_status.find(ModelObjectStatus(model_object.id()));
        assert(it_status != model_object_status.end());
        assert(it_status->status != ModelObjectStatus::Deleted);
        if (it_status->status == ModelObjectStatus::New)
            // PrintObject instances will be added in the next loop.
            continue;
        // Update the ModelObject instance, possibly invalidate the linked PrintObjects.
        assert(it_status->status == ModelObjectStatus::Old || it_status->status == ModelObjectStatus::Moved);
        const ModelObject &model_object_new = *model.objects[idx_model_object];
        // Check whether a model part volume was added or removed, their transformations or order changed.
        bool model_parts_differ         = model_volume_list_changed(model_object, model_object_new, ModelVolume::MODEL_PART);
        bool modifiers_differ           = model_volume_list_changed(model_object, model_object_new, ModelVolume::PARAMETER_MODIFIER);
        bool support_blockers_differ    = model_volume_list_changed(model_object, model_object_new, ModelVolume::SUPPORT_BLOCKER);
        bool support_enforcers_differ   = model_volume_list_changed(model_object, model_object_new, ModelVolume::SUPPORT_ENFORCER);
        if (model_parts_differ || modifiers_differ || 
            model_object.origin_translation         != model_object_new.origin_translation   ||
            model_object.layer_height_ranges        != model_object_new.layer_height_ranges  || 
            model_object.layer_height_profile       != model_object_new.layer_height_profile ||
            model_object.layer_height_profile_valid != model_object_new.layer_height_profile_valid) {
            // The very first step (the slicing step) is invalidated. One may freely remove all associated PrintObjects.
            auto range = print_object_status.equal_range(PrintObjectStatus(model_object.id()));
            for (auto it = range.first; it != range.second; ++ it) {
                update_apply_status(it->print_object->invalidate_all_steps());
                const_cast<PrintObjectStatus&>(*it).status = PrintObjectStatus::Deleted;
            }
            // Copy content of the ModelObject including its ID, reset the parent.
            model_object.assign(&model_object_new);
        } else if (support_blockers_differ || support_enforcers_differ) {
            // First stop background processing before shuffling or deleting the ModelVolumes in the ModelObject's list.
            m_cancel_callback();
            // Invalidate just the supports step.
            auto range = print_object_status.equal_range(PrintObjectStatus(model_object.id()));
            for (auto it = range.first; it != range.second; ++ it)
                update_apply_status(it->print_object->invalidate_step(posSupportMaterial));
            // Copy just the support volumes.
            model_volume_list_update_supports(model_object, model_object_new);
        }
        if (! model_parts_differ && ! modifiers_differ) {
            // Synchronize the remaining data of ModelVolumes (name, config, m_type, m_material_id)
            // Synchronize Object's config.
            t_config_option_keys &this_object_config_diff = const_cast<ModelObjectStatus&>(*it_status).object_config_diff;
            this_object_config_diff = model_object.config.diff(model_object_new.config);
            if (! this_object_config_diff.empty())
                model_object.config.apply_only(model_object_new.config, this_object_config_diff, true);
            if (! object_diff.empty() || ! this_object_config_diff.empty()) {
                PrintObjectConfig new_config = m_default_object_config;
                normalize_and_apply_config(new_config, model_object.config);
                auto range = print_object_status.equal_range(PrintObjectStatus(model_object.id()));
                for (auto it = range.first; it != range.second; ++ it) {
                    t_config_option_keys diff = it->print_object->config().diff(new_config);
                    if (! diff.empty()) {
                        update_apply_status(it->print_object->invalidate_state_by_config_options(diff));
                        it->print_object->config_apply_only(new_config, diff, true);
                    }
                }
            }
            model_object.name       = model_object_new.name;
            model_object.input_file = model_object_new.input_file;
            model_object.clear_instances();
            for (const ModelInstance *model_instance : model_object_new.instances)
                model_object.add_instance(*model_instance);
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
            PrintObjectConfig config = m_default_object_config;
            normalize_and_apply_config(config, model_object->config);
            std::vector<PrintInstances> new_print_instances = print_objects_from_model_object(*model_object);
            if (old.empty()) {
                // Simple case, just generate new instances.
                for (const PrintInstances &print_instances : new_print_instances) {
                    PrintObject *print_object = new PrintObject(this, model_object, model_object->raw_bounding_box());
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
                    PrintObject *print_object = new PrintObject(this, model_object, model_object->raw_bounding_box());
                    print_object->set_trafo(new_instances.trafo);
                    print_object->set_copies(new_instances.copies);
                    print_object->config_apply(config);
                    print_objects_new.emplace_back(print_object);
                    // print_object_status.emplace(PrintObjectStatus(print_object, PrintObjectStatus::New));
                    new_objects = true;
                    if (it_old != old.end())
                        const_cast<PrintObjectStatus*>(*it_old)->status = PrintObjectStatus::Deleted;
                } else if ((*it_old)->print_object->copies() != new_instances.copies) {
                    // The PrintObject already exists and the copies differ.
                    if ((*it_old)->print_object->copies().size() != new_instances.copies.size())
                        update_apply_status(this->invalidate_step(psWipeTower));
					if ((*it_old)->print_object->set_copies(new_instances.copies)) {
						// Invalidated
						static PrintStep steps[] = { psSkirt, psBrim, psGCodeExport };
						update_apply_status(this->invalidate_multiple_steps(steps, steps + 3));
					}
					print_objects_new.emplace_back((*it_old)->print_object);
					const_cast<PrintObjectStatus*>(*it_old)->status = PrintObjectStatus::Reused;
				}
            }
        }
        if (m_objects != print_objects_new) {
            m_cancel_callback();
            m_objects = print_objects_new;
            // Delete the PrintObjects marked as Unknown or Deleted.
            bool deleted_objects = false;
            for (auto &pos : print_object_status)
                if (pos.status == PrintObjectStatus::Unknown || pos.status == PrintObjectStatus::Deleted) {
                    // update_apply_status(pos.print_object->invalidate_all_steps());
                    delete pos.print_object;
					deleted_objects = true;
                }
			if (deleted_objects) {
				static PrintStep steps[] = { psSkirt, psBrim, psWipeTower, psGCodeExport };
				update_apply_status(this->invalidate_multiple_steps(steps, steps + 4));
			}
            update_apply_status(new_objects);
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
            if (region_id < print_object->region_volumes.size()) {
                for (int volume_id : print_object->region_volumes[region_id]) {
                    const ModelVolume &volume = *print_object->model_object()->volumes[volume_id];
                    if (this_region_config_set) {
                        // If the new config for this volume differs from the other
                        // volume configs currently associated to this region, it means
                        // the region subdivision does not make sense anymore.
                        if (! this_region_config.equals(region_config_from_model_volume(m_default_region_config, volume)))
                            // Regions were split. Reset this print_object.
                            goto print_object_end;
                    } else {
                        this_region_config = region_config_from_model_volume(m_default_region_config, volume);
                        for (size_t i = 0; i < region_id; ++ i)
                            if (m_regions[i]->config().equals(this_region_config))
                                // Regions were merged. Reset this print_object.
                                goto print_object_end;
                        this_region_config_set = true;
                    }
                }
            }
            continue;
        print_object_end:
            update_apply_status(print_object->invalidate_all_steps());
            // Decrease the references to regions from this volume.
            int ireg = 0;
            for (const std::vector<int> &volumes : print_object->region_volumes) {
                if (! volumes.empty())
                    -- m_regions[ireg];
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
        std::vector<int>    map_volume_to_region(model_object.volumes.size(), -1);
        for (size_t i = idx_print_object; i < m_objects.size() && m_objects[i]->model_object() == &model_object; ++ i) {
            PrintObject &print_object = *m_objects[i];
			bool         fresh = print_object.region_volumes.empty();
            unsigned int volume_id = 0;
            for (const ModelVolume *volume : model_object.volumes) {
                if (! volume->is_model_part() && ! volume->is_modifier())
                    continue;
                int region_id = -1;
                if (&print_object == &print_object0) {
                    // Get the config applied to this volume.
                    PrintRegionConfig config = region_config_from_model_volume(m_default_region_config, *volume);
                    // Find an existing print region with the same config.
                    for (int i = 0; i < (int)m_regions.size(); ++ i)
                        if (config.equals(m_regions[i]->config())) {
                            region_id = i;
                            break;
                        }
                    // If no region exists with the same config, create a new one.
                    if (region_id == size_t(-1)) {
                        for (region_id = 0; region_id < m_regions.size(); ++ region_id)
                            if (m_regions[region_id]->m_refcnt == 0) {
                                // An empty slot was found.
                                m_regions[region_id]->set_config(std::move(config));
                                break;
                            }
                        if (region_id == m_regions.size())
                            this->add_region(config);
                    }
                    map_volume_to_region[volume_id] = region_id;
                } else
                    region_id = map_volume_to_region[volume_id];
                // Assign volume to a region.
				if (fresh) {
					if (region_id >= print_object.region_volumes.size() || print_object.region_volumes[region_id].empty())
						++ m_regions[region_id]->m_refcnt;
					print_object.add_region_volume(region_id, volume_id);
				}
                ++ volume_id;
            }
        }
    }

    // Always make sure that the layer_height_profiles are set, as they should not be modified from the worker threads.
    for (PrintObject *object : m_objects)
        if (! object->layer_height_profile_valid)
            object->update_layer_height_profile();

    this->update_object_placeholders();
	return static_cast<ApplyStatus>(apply_status);
}

// Update "scale", "input_filename", "input_filename_base" placeholders from the current m_objects.
void Print::update_object_placeholders()
{
    // get the first input file name
    std::string input_file;
    std::vector<std::string> v_scale;
    for (const PrintObject *object : m_objects) {
        const ModelObject &mobj = *object->model_object();
        // CHECK_ME -> Is the following correct ?
        v_scale.push_back("x:" + boost::lexical_cast<std::string>(mobj.instances[0]->get_scaling_factor(X) * 100) +
            "% y:" + boost::lexical_cast<std::string>(mobj.instances[0]->get_scaling_factor(Y) * 100) +
            "% z:" + boost::lexical_cast<std::string>(mobj.instances[0]->get_scaling_factor(Z) * 100) + "%");
        if (input_file.empty())
            input_file = mobj.input_file;
    }
    
    PlaceholderParser &pp = m_placeholder_parser;
    pp.set("scale", v_scale);
    if (! input_file.empty()) {
        // get basename with and without suffix
        const std::string input_basename = boost::filesystem::path(input_file).filename().string();
        pp.set("input_filename", input_basename);
        const std::string input_basename_base = input_basename.substr(0, input_basename.find_last_of("."));
        pp.set("input_filename_base", input_basename_base);
    }
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

std::string Print::validate() const
{
    if (m_objects.empty())
        return L("All objects are outside of the print volume.");

    if (m_config.complete_objects) {
        // Check horizontal clearance.
        {
            Polygons convex_hulls_other;
            for (PrintObject *object : m_objects) {
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
                convex_hull = offset(convex_hull, scale_(m_config.extruder_clearance_radius.value)/2, jtRound, scale_(0.1)).front();
                // Now we check that no instance of convex_hull intersects any of the previously checked object instances.
                for (const Point &copy : object->m_copies) {
                    Polygon p = convex_hull;
                    p.translate(copy);
                    if (! intersection(convex_hulls_other, p).empty())
                        return L("Some objects are too close; your extruder will collide with them.");
                    polygons_append(convex_hulls_other, p);
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
        if (m_config.gcode_flavor != gcfRepRap && m_config.gcode_flavor != gcfMarlin)
            return L("The Wipe Tower is currently only supported for the Marlin and RepRap/Sprinter G-code flavors.");
        if (! m_config.use_relative_e_distances)
            return L("The Wipe Tower is currently only supported with the relative extruder addressing (use_relative_e_distances=1).");
        SlicingParameters slicing_params0 = m_objects.front()->slicing_parameters();

        const PrintObject* tallest_object = m_objects.front(); // let's find the tallest object
        for (const auto* object : m_objects)
            if (*(object->layer_height_profile.end()-2) > *(tallest_object->layer_height_profile.end()-2) )
                    tallest_object = object;

        for (PrintObject *object : m_objects) {
            SlicingParameters slicing_params = object->slicing_parameters();
            if (std::abs(slicing_params.first_print_layer_height - slicing_params0.first_print_layer_height) > EPSILON ||
                std::abs(slicing_params.layer_height             - slicing_params0.layer_height            ) > EPSILON)
                return L("The Wipe Tower is only supported for multiple objects if they have equal layer heigths");
            if (slicing_params.raft_layers() != slicing_params0.raft_layers())
                return L("The Wipe Tower is only supported for multiple objects if they are printed over an equal number of raft layers");
            if (object->config().support_material_contact_distance != m_objects.front()->config().support_material_contact_distance)
                return L("The Wipe Tower is only supported for multiple objects if they are printed with the same support_material_contact_distance");
            if (! equal_layering(slicing_params, slicing_params0))
                return L("The Wipe Tower is only supported for multiple objects if they are sliced equally.");
            bool was_layer_height_profile_valid = object->layer_height_profile_valid;
            object->update_layer_height_profile();
            object->layer_height_profile_valid = was_layer_height_profile_valid;

            if ( m_config.variable_layer_height ) { // comparing layer height profiles
                bool failed = false;
                if (tallest_object->layer_height_profile.size() >= object->layer_height_profile.size() ) {
                    int i = 0;
                    while ( i < object->layer_height_profile.size() && i < tallest_object->layer_height_profile.size()) {
                        if (std::abs(tallest_object->layer_height_profile[i] - object->layer_height_profile[i])) {
                            failed = true;
                            break;
                        }
                        ++i;
                        if (i == object->layer_height_profile.size()-2) // this element contains this objects max z
                            if (tallest_object->layer_height_profile[i] > object->layer_height_profile[i]) // the difference does not matter in this case
                                ++i;
                    }
                }
                else
                    failed = true;

                if (failed)
                    return L("The Wipe tower is only supported if all objects have the same layer height profile");
            }
        }
    }
    
    {
        // find the smallest nozzle diameter
        std::vector<unsigned int> extruders = this->extruders();
        if (extruders.empty())
            return L("The supplied settings will cause an empty print.");
        
        std::vector<double> nozzle_diameters;
        for (unsigned int extruder_id : extruders)
            nozzle_diameters.push_back(m_config.nozzle_diameter.get_at(extruder_id));
        double min_nozzle_diameter = *std::min_element(nozzle_diameters.begin(), nozzle_diameters.end());
        unsigned int total_extruders_count = m_config.nozzle_diameter.size();
        for (const auto& extruder_idx : extruders)
            if ( extruder_idx >= total_extruders_count )
                return L("One or more object were assigned an extruder that the printer does not have.");

        for (PrintObject *object : m_objects) {
            if ((object->config().support_material_extruder == -1 || object->config().support_material_interface_extruder == -1) &&
                (object->config().raft_layers > 0 || object->config().support_material.value)) {
                // The object has some form of support and either support_material_extruder or support_material_interface_extruder
                // will be printed with the current tool without a forced tool change. Play safe, assert that all object nozzles
                // are of the same diameter.
                if (nozzle_diameters.size() > 1)
                    return L("Printing with multiple extruders of differing nozzle diameters. "
                           "If support is to be printed with the current extruder (support_material_extruder == 0 or support_material_interface_extruder == 0), "
                           "all nozzles have to be of the same diameter.");
            }
            
            // validate first_layer_height
            double first_layer_height = object->config().get_abs_value(L("first_layer_height"));
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
        m_config.nozzle_diameter.get_at(m_regions.front()->config().perimeter_extruder-1),
        this->skirt_first_layer_height(),
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
        m_config.nozzle_diameter.get_at(m_objects.front()->config().support_material_extruder-1),
        this->skirt_first_layer_height(),
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
    BOOST_LOG_TRIVIAL(info) << "Staring the slicing process.";
    for (PrintObject *obj : m_objects)
        obj->make_perimeters();
    this->throw_if_canceled();
    this->set_status(70, "Infilling layers");
    for (PrintObject *obj : m_objects)
        obj->infill();
    this->throw_if_canceled();
    for (PrintObject *obj : m_objects)
        obj->generate_support_material();
    this->throw_if_canceled();
    if (! m_state.is_done(psSkirt)) {
        this->set_started(psSkirt);
        m_skirt.clear();
        if (this->has_skirt()) {
            this->set_status(88, "Generating skirt");
            this->_make_skirt();
        }
        this->set_done(psSkirt);
    }
    this->throw_if_canceled();
    if (! m_state.is_done(psBrim)) {
        this->set_started(psBrim);
        m_brim.clear();
        if (m_config.brim_width > 0) {
            this->set_status(88, "Generating brim");
            this->_make_brim();
        }
       this->set_done(psBrim);
    }
    this->throw_if_canceled();
    if (! m_state.is_done(psWipeTower)) {
        this->set_started(psWipeTower);
        m_wipe_tower_data.clear();
        if (this->has_wipe_tower()) {
            //this->set_status(95, "Generating wipe tower");
            this->_make_wipe_tower();
        }
       this->set_done(psWipeTower);
    }
    BOOST_LOG_TRIVIAL(info) << "Slicing process finished.";
}

// G-code export process, running at a background thread.
// The export_gcode may die for various reasons (fails to process output_filename_format,
// write error into the G-code, cannot execute post-processing scripts).
// It is up to the caller to show an error message.
void Print::export_gcode(const std::string &path_template, GCodePreviewData *preview_data)
{
    // prerequisites
    this->process();
    
    // output everything to a G-code file
    // The following call may die if the output_filename_format template substitution fails.
    std::string path = this->output_filepath(path_template);
    std::string message = "Exporting G-code";
    if (! path.empty()) {
        message += " to ";
        message += path;
    }
    this->set_status(90, message);

    // The following line may die for multiple reasons.
    GCode gcode;
    gcode.do_export(this, path.c_str(), preview_data);
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
    PrintObjectPtrs printable_objects = get_printable_objects();
    for (const PrintObject *object : printable_objects) {
        size_t skirt_layers = this->has_infinite_skirt() ?
            object->layer_count() : 
            std::min(size_t(m_config.skirt_height.value), object->layer_count());
        skirt_height_z = std::max(skirt_height_z, object->m_layers[skirt_layers-1]->print_z);
    }
    
    // Collect points from all layers contained in skirt height.
    Points points;
    for (const PrintObject *object : printable_objects) {
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
    Flow brim_flow = this->brim_flow();
    double actual_brim_width = brim_flow.spacing() * floor(m_config.brim_width.value / brim_flow.spacing());
    coord_t distance = scale_(std::max(m_config.skirt_distance.value, actual_brim_width) - spacing/2.);
    // Draw outlines from outside to inside.
    // Loop while we have less skirts than required or any extruder hasn't reached the min length if any.
    std::vector<coordf_t> extruded_length(extruders.size(), 0.);
    for (int i = n_skirts, extruder_idx = 0; i > 0; -- i) {
        this->throw_if_canceled();
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
    PrintObjectPtrs printable_objects = get_printable_objects();
    for (PrintObject *object : printable_objects) {
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
    // see https://github.com/prusa3d/Slic3r/issues/607
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
                it_layer = m_objects.front()->insert_support_layer(it_layer, size_t(-1), height, lt.print_z, lt.print_z - 0.5 * height);
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
        float(m_config.extra_loading_move.value), float(m_config.wipe_tower_bridging), wipe_volumes,
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
            m_config.filament_loading_speed.get_at(i),
            m_config.filament_loading_speed_start.get_at(i),
            m_config.filament_unloading_speed.get_at(i),
            m_config.filament_unloading_speed_start.get_at(i),
            m_config.filament_toolchange_delay.get_at(i),
            m_config.filament_cooling_moves.get_at(i),
            m_config.filament_cooling_initial_speed.get_at(i),
            m_config.filament_cooling_final_speed.get_at(i),
            m_config.filament_ramming_parameters.get_at(i),
            m_config.nozzle_diameter.get_at(i));

    m_wipe_tower_data.priming = Slic3r::make_unique<WipeTower::ToolChangeResult>(
        wipe_tower.prime(this->skirt_first_layer_height(), m_wipe_tower_data.tool_ordering.all_extruders(), false));

    // Lets go through the wipe tower layers and determine pairs of extruder changes for each
    // to pass to wipe_tower (so that it can use it for planning the layout of the tower)
    {
        unsigned int current_extruder_id = m_wipe_tower_data.tool_ordering.all_extruders().back();
        for (auto &layer_tools : m_wipe_tower_data.tool_ordering.layer_tools()) { // for all layers
            if (!layer_tools.has_wipe_tower) continue;
            bool first_layer = &layer_tools == &m_wipe_tower_data.tool_ordering.front();
            wipe_tower.plan_toolchange(layer_tools.print_z, layer_tools.wipe_tower_layer_height, current_extruder_id, current_extruder_id,false);
            for (const auto extruder_id : layer_tools.extruders) {
                if ((first_layer && extruder_id == m_wipe_tower_data.tool_ordering.all_extruders().back()) || extruder_id != current_extruder_id) {
                    float volume_to_wipe = wipe_volumes[current_extruder_id][extruder_id];             // total volume to wipe after this toolchange
                    // Not all of that can be used for infill purging:
                    volume_to_wipe -= m_config.filament_minimal_purge_on_wipe_tower.get_at(extruder_id);

                    // try to assign some infills/objects for the wiping:
                    volume_to_wipe = layer_tools.wiping_extrusions().mark_wiping_extrusions(*this, current_extruder_id, extruder_id, volume_to_wipe);

                    // add back the minimal amount toforce on the wipe tower:
                    volume_to_wipe += m_config.filament_minimal_purge_on_wipe_tower.get_at(extruder_id);

                    // request a toolchange at the wipe tower with at least volume_to_wipe purging amount
                    wipe_tower.plan_toolchange(layer_tools.print_z, layer_tools.wipe_tower_layer_height, current_extruder_id, extruder_id,
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

std::string Print::output_filename() const
{
    DynamicConfig cfg_timestamp;
    PlaceholderParser::update_timestamp(cfg_timestamp);
    try {
        return this->placeholder_parser().process(m_config.output_filename_format.value, 0, &cfg_timestamp);
    } catch (std::runtime_error &err) {
        throw std::runtime_error(L("Failed processing of the output_filename_format template.") + "\n" + err.what());
    }
}

std::string Print::output_filepath(const std::string &path) const
{
    // if we were supplied no path, generate an automatic one based on our first object's input file
    if (path.empty()) {
        // get the first input file name
        std::string input_file;
        for (const PrintObject *object : m_objects) {
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

void Print::export_png(const std::string &dirpath)
{
//    size_t idx = 0;
//    for (PrintObject *obj : m_objects) {
//        obj->slice();
//        this->set_status(int(floor(idx * 100. / m_objects.size() + 0.5)), "Slicing...");
//        ++ idx;
//    }
//    this->set_status(90, "Exporting zipped archive...");
//    print_to<FilePrinterFormat::PNG>(*this,
//        dirpath,
//        float(m_config.bed_size_x.value),
//        float(m_config.bed_size_y.value),
//        int(m_config.pixel_width.value),
//        int(m_config.pixel_height.value),
//        float(m_config.exp_time.value),
//        float(m_config.exp_time_first.value));
//    this->set_status(100, "Done.");
}

// Returns extruder this eec should be printed with, according to PrintRegion config
int Print::get_extruder(const ExtrusionEntityCollection& fill, const PrintRegion &region)
{
    return is_infill(fill.role()) ? std::max<int>(0, (is_solid_infill(fill.entities.front()->role()) ? region.config().solid_infill_extruder : region.config().infill_extruder) - 1) :
                                    std::max<int>(region.config().perimeter_extruder.value - 1, 0);
}

} // namespace Slic3r

