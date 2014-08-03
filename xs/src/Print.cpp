#include "Print.hpp"
#include "BoundingBox.hpp"
#include <algorithm>

namespace Slic3r {

template <class StepClass>
bool
PrintState<StepClass>::is_started(StepClass step) const
{
    return this->started.find(step) != this->started.end();
}

template <class StepClass>
bool
PrintState<StepClass>::is_done(StepClass step) const
{
    return this->done.find(step) != this->done.end();
}

template <class StepClass>
void
PrintState<StepClass>::set_started(StepClass step)
{
    this->started.insert(step);
}

template <class StepClass>
void
PrintState<StepClass>::set_done(StepClass step)
{
    this->done.insert(step);
}

template <class StepClass>
bool
PrintState<StepClass>::invalidate(StepClass step)
{
    bool invalidated = this->started.erase(step) > 0;
    this->done.erase(step);
    return invalidated;
}

template class PrintState<PrintStep>;
template class PrintState<PrintObjectStep>;


Print::Print()
:   total_used_filament(0),
    total_extruded_volume(0)
{
}

Print::~Print()
{
    clear_objects();
    clear_regions();
}

void
Print::clear_objects()
{
    for (int i = this->objects.size()-1; i >= 0; --i)
        this->delete_object(i);

    this->clear_regions();
}

PrintObject*
Print::get_object(size_t idx)
{
    return objects.at(idx);
}

PrintObject*
Print::add_object(ModelObject *model_object, const BoundingBoxf3 &modobj_bbox)
{
    PrintObject *object = new PrintObject(this, model_object, modobj_bbox);
    objects.push_back(object);
    
    // invalidate steps
    this->invalidate_step(psSkirt);
    this->invalidate_step(psBrim);
    
    return object;
}

PrintObject*
Print::set_new_object(size_t idx, ModelObject *model_object, const BoundingBoxf3 &modobj_bbox)
{
    if (idx >= this->objects.size()) throw "bad idx";

    PrintObjectPtrs::iterator old_it = this->objects.begin() + idx;
    // before deleting object, invalidate all of its steps in order to 
    // invalidate all of the dependent ones in Print
    (*old_it)->invalidate_all_steps();
    delete *old_it;

    PrintObject *object = new PrintObject(this, model_object, modobj_bbox);
    this->objects[idx] = object;
    return object;
}

void
Print::delete_object(size_t idx)
{
    PrintObjectPtrs::iterator i = this->objects.begin() + idx;
    delete *i;
    this->objects.erase(i);

    // TODO: purge unused regions

    this->state.invalidate(psSkirt);
    this->state.invalidate(psBrim);
}

void
Print::clear_regions()
{
    for (int i = this->regions.size()-1; i >= 0; --i)
        this->delete_region(i);
}

PrintRegion*
Print::get_region(size_t idx)
{
    return regions.at(idx);
}

PrintRegion*
Print::add_region()
{
    PrintRegion *region = new PrintRegion(this);
    regions.push_back(region);
    return region;
}

void
Print::delete_region(size_t idx)
{
    PrintRegionPtrs::iterator i = this->regions.begin() + idx;
    delete *i;
    this->regions.erase(i);
}

bool
Print::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys)
{
    std::set<PrintStep> steps;
    
    // this method only accepts PrintConfig option keys
    for (std::vector<t_config_option_key>::const_iterator opt_key = opt_keys.begin(); opt_key != opt_keys.end(); ++opt_key) {
        if (*opt_key == "skirts"
            || *opt_key == "skirt_height"
            || *opt_key == "skirt_distance"
            || *opt_key == "min_skirt_length") {
            steps.insert(psSkirt);
        } else if (*opt_key == "brim_width") {
            steps.insert(psBrim);
            steps.insert(psSkirt);
        } else if (*opt_key == "nozzle_diameter") {
            steps.insert(psInitExtruders);
        } else if (*opt_key == "avoid_crossing_perimeters"
            || *opt_key == "bed_shape"
            || *opt_key == "bed_temperature"
            || *opt_key == "bridge_acceleration"
            || *opt_key == "bridge_fan_speed"
            || *opt_key == "complete_objects"
            || *opt_key == "cooling"
            || *opt_key == "default_acceleration"
            || *opt_key == "disable_fan_first_layers"
            || *opt_key == "duplicate_distance"
            || *opt_key == "end_gcode"
            || *opt_key == "extruder_clearance_height"
            || *opt_key == "extruder_clearance_radius"
            || *opt_key == "extruder_offset"
            || *opt_key == "extrusion_axis"
            || *opt_key == "extrusion_multiplier"
            || *opt_key == "fan_always_on"
            || *opt_key == "fan_below_layer_time"
            || *opt_key == "filament_diameter"
            || *opt_key == "first_layer_acceleration"
            || *opt_key == "first_layer_bed_temperature"
            || *opt_key == "first_layer_speed"
            || *opt_key == "first_layer_temperature"
            || *opt_key == "g0"
            || *opt_key == "gcode_arcs"
            || *opt_key == "gcode_comments"
            || *opt_key == "gcode_flavor"
            || *opt_key == "infill_acceleration"
            || *opt_key == "infill_first"
            || *opt_key == "layer_gcode"
            || *opt_key == "min_fan_speed"
            || *opt_key == "max_fan_speed"
            || *opt_key == "min_print_speed"
            || *opt_key == "notes"
            || *opt_key == "only_retract_when_crossing_perimeters"
            || *opt_key == "output_filename_format"
            || *opt_key == "perimeter_acceleration"
            || *opt_key == "post_process"
            || *opt_key == "retract_before_travel"
            || *opt_key == "retract_layer_change"
            || *opt_key == "retract_length"
            || *opt_key == "retract_length_toolchange"
            || *opt_key == "retract_lift"
            || *opt_key == "retract_restart_extra"
            || *opt_key == "retract_restart_extra_toolchange"
            || *opt_key == "retract_speed"
            || *opt_key == "slowdown_below_layer_time"
            || *opt_key == "spiral_vase"
            || *opt_key == "standby_temperature_delta"
            || *opt_key == "start_gcode"
            || *opt_key == "temperature"
            || *opt_key == "threads"
            || *opt_key == "toolchange_gcode"
            || *opt_key == "travel_speed"
            || *opt_key == "use_firmware_retraction"
            || *opt_key == "use_relative_e_distances"
            || *opt_key == "vibration_limit"
            || *opt_key == "wipe"
            || *opt_key == "z_offset") {
            // these options only affect G-code export, so nothing to invalidate
        } else {
            // for legacy, if we can't handle this option let's invalidate all steps
            return this->invalidate_all_steps();
        }
    }
    
    bool invalidated = false;
    for (std::set<PrintStep>::const_iterator step = steps.begin(); step != steps.end(); ++step) {
        if (this->invalidate_step(*step)) invalidated = true;
    }
    
    return invalidated;
}

bool
Print::invalidate_step(PrintStep step)
{
    bool invalidated = this->state.invalidate(step);
    
    // propagate to dependent steps
    if (step == psSkirt) {
        this->invalidate_step(psBrim);
    } else if (step == psInitExtruders) {
        FOREACH_OBJECT(this, object) {
            (*object)->invalidate_step(posPerimeters);
            (*object)->invalidate_step(posSupportMaterial);
        }
    }
    
    return invalidated;
}

bool
Print::invalidate_all_steps()
{
    // make a copy because when invalidating steps the iterators are not working anymore
    std::set<PrintStep> steps = this->state.started;
    
    bool invalidated = false;
    for (std::set<PrintStep>::const_iterator step = steps.begin(); step != steps.end(); ++step) {
        if (this->invalidate_step(*step)) invalidated = true;
    }
    return invalidated;
}

// returns 0-based indices of used extruders
std::set<size_t>
Print::extruders() const
{
    std::set<size_t> extruders;
    
    FOREACH_REGION(this, region) {
        extruders.insert((*region)->config.perimeter_extruder - 1);
        extruders.insert((*region)->config.infill_extruder - 1);
    }
    FOREACH_OBJECT(this, object) {
        extruders.insert((*object)->config.support_material_extruder - 1);
        extruders.insert((*object)->config.support_material_interface_extruder - 1);
    }
    
    return extruders;
}

void
Print::_simplify_slices(double distance)
{
    FOREACH_OBJECT(this, object) {
        FOREACH_LAYER(*object, layer) {
            (*layer)->slices.simplify(distance);
            FOREACH_LAYERREGION(*layer, layerm) {
                (*layerm)->slices.simplify(distance);
            }
        }
    }
}

double
Print::max_allowed_layer_height() const
{
    std::vector<double> nozzle_diameter;
    
    std::set<size_t> extruders = this->extruders();
    for (std::set<size_t>::const_iterator e = extruders.begin(); e != extruders.end(); ++e) {
        nozzle_diameter.push_back(this->config.nozzle_diameter.get_at(*e));
    }
    
    return *std::max_element(nozzle_diameter.begin(), nozzle_diameter.end());
}

void
Print::init_extruders()
{
    if (this->state.is_done(psInitExtruders)) return;
    this->state.set_done(psInitExtruders);
    
    // enforce tall skirt if using ooze_prevention
    // FIXME: this is not idempotent (i.e. switching ooze_prevention off will not revert skirt settings)
    if (this->config.ooze_prevention && this->extruders().size() > 1) {
        this->config.skirt_height.value = -1;
        if (this->config.skirts == 0) this->config.skirts.value = 1;
    }
    
    this->state.set_done(psInitExtruders);
}

bool
Print::has_support_material() const
{
    FOREACH_OBJECT(this, object) {
        PrintObjectConfig &config = (*object)->config;
        if (config.support_material || config.raft_layers > 0 || config.support_material_enforce_layers > 0)
            return true;
    }
    return false;
}


#ifdef SLIC3RXS
REGISTER_CLASS(Print, "Print");
#endif


}
