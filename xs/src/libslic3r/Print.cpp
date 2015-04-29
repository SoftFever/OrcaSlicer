#include "Print.hpp"
#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "Flow.hpp"
#include "Geometry.hpp"
#include "SupportMaterial.hpp"
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

void
Print::delete_object(size_t idx)
{
    PrintObjectPtrs::iterator i = this->objects.begin() + idx;
    
    // before deleting object, invalidate all of its steps in order to 
    // invalidate all of the dependent ones in Print
    (*i)->invalidate_all_steps();
    
    // destroy object and remove it from our container
    delete *i;
    this->objects.erase(i);

    // TODO: purge unused regions
}

void
Print::reload_object(size_t idx)
{
    /* TODO: this method should check whether the per-object config and per-material configs
        have changed in such a way that regions need to be rearranged or we can just apply
        the diff and invalidate something.  Same logic as apply_config()
        For now we just re-add all objects since we haven't implemented this incremental logic yet.
        This should also check whether object volumes (parts) have changed. */
    
    // collect all current model objects
    ModelObjectPtrs model_objects;
    FOREACH_OBJECT(this, object) {
        model_objects.push_back((*object)->model_object());
    }
    
    // remove our print objects
    this->clear_objects();
    
    // re-add model objects
    for (ModelObjectPtrs::iterator it = model_objects.begin(); it != model_objects.end(); ++it) {
        this->add_model_object(*it);
    }
}

bool
Print::reload_model_instances()
{
    bool invalidated = false;
    FOREACH_OBJECT(this, object) {
        if ((*object)->reload_model_instances()) invalidated = true;
    }
    return invalidated;
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
    std::set<PrintObjectStep> osteps;
    
    // this method only accepts PrintConfig option keys
    for (std::vector<t_config_option_key>::const_iterator opt_key = opt_keys.begin(); opt_key != opt_keys.end(); ++opt_key) {
        if (*opt_key == "skirts"
            || *opt_key == "skirt_height"
            || *opt_key == "skirt_distance"
            || *opt_key == "min_skirt_length"
            || *opt_key == "ooze_prevention") {
            steps.insert(psSkirt);
        } else if (*opt_key == "brim_width") {
            steps.insert(psBrim);
            steps.insert(psSkirt);
        } else if (*opt_key == "nozzle_diameter"
            || *opt_key == "resolution") {
            osteps.insert(posSlice);
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
            || *opt_key == "pressure_advance"
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
        } else if (*opt_key == "first_layer_extrusion_width") {
            osteps.insert(posPerimeters);
            osteps.insert(posInfill);
            osteps.insert(posSupportMaterial);
            steps.insert(psSkirt);
            steps.insert(psBrim);
        } else {
            // for legacy, if we can't handle this option let's invalidate all steps
            return this->invalidate_all_steps();
        }
    }
    
    bool invalidated = false;
    for (std::set<PrintStep>::const_iterator step = steps.begin(); step != steps.end(); ++step) {
        if (this->invalidate_step(*step)) invalidated = true;
    }
    for (std::set<PrintObjectStep>::const_iterator ostep = osteps.begin(); ostep != osteps.end(); ++ostep) {
        FOREACH_OBJECT(this, object) {
            if ((*object)->invalidate_step(*ostep)) invalidated = true;
        }
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

// returns true if an object step is done on all objects
// and there's at least one object
bool
Print::step_done(PrintObjectStep step) const
{
    if (this->objects.empty()) return false;
    FOREACH_OBJECT(this, object) {
        if (!(*object)->state.is_done(step))
            return false;
    }
    return true;
}

// returns 0-based indices of used extruders
std::set<size_t>
Print::object_extruders() const
{
    std::set<size_t> extruders;
    
    FOREACH_REGION(this, region) {
        // these checks reflect the same logic used in the GUI for enabling/disabling
        // extruder selection fields
        if ((*region)->config.perimeters.value > 0 || this->config.brim_width.value > 0)
            extruders.insert((*region)->config.perimeter_extruder - 1);
        
        if ((*region)->config.fill_density.value > 0)
            extruders.insert((*region)->config.infill_extruder - 1);
        
        if ((*region)->config.top_solid_layers.value > 0 || (*region)->config.bottom_solid_layers.value > 0)
            extruders.insert((*region)->config.solid_infill_extruder - 1);
    }
    
    return extruders;
}

// returns 0-based indices of used extruders
std::set<size_t>
Print::support_material_extruders() const
{
    std::set<size_t> extruders;
    
    FOREACH_OBJECT(this, object) {
        if ((*object)->has_support_material()) {
            extruders.insert((*object)->config.support_material_extruder - 1);
            extruders.insert((*object)->config.support_material_interface_extruder - 1);
        }
    }
    
    return extruders;
}

// returns 0-based indices of used extruders
std::set<size_t>
Print::extruders() const
{
    std::set<size_t> extruders = this->object_extruders();
    
    std::set<size_t> s_extruders = this->support_material_extruders();
    extruders.insert(s_extruders.begin(), s_extruders.end());
    
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

/*  Caller is responsible for supplying models whose objects don't collide
    and have explicit instance positions */
void
Print::add_model_object(ModelObject* model_object, int idx)
{
    DynamicPrintConfig object_config = model_object->config;  // clone
    object_config.normalize();

    // initialize print object and store it at the given position
    PrintObject* o;
    {
        BoundingBoxf3 bb = model_object->raw_bounding_box();
        if (idx != -1) {
            // replacing existing object
            PrintObjectPtrs::iterator old_it = this->objects.begin() + idx;
            // before deleting object, invalidate all of its steps in order to 
            // invalidate all of the dependent ones in Print
            (*old_it)->invalidate_all_steps();
            delete *old_it;
            
            this->objects[idx] = o = new PrintObject(this, model_object, bb);
        } else {
            o = new PrintObject(this, model_object, bb);
            objects.push_back(o);
    
            // invalidate steps
            this->invalidate_step(psSkirt);
            this->invalidate_step(psBrim);
        }
    }

    for (ModelVolumePtrs::const_iterator v_i = model_object->volumes.begin(); v_i != model_object->volumes.end(); ++v_i) {
        size_t volume_id = v_i - model_object->volumes.begin();
        ModelVolume* volume = *v_i;
        
        // get the config applied to this volume
        PrintRegionConfig config = this->_region_config_from_model_volume(*volume);
        
        // find an existing print region with the same config
        int region_id = -1;
        for (PrintRegionPtrs::const_iterator region = this->regions.begin(); region != this->regions.end(); ++region) {
            if (config.equals((*region)->config)) {
                region_id = region - this->regions.begin();
                break;
            }
        }
        
        // if no region exists with the same config, create a new one
        if (region_id == -1) {
            PrintRegion* r = this->add_region();
            r->config.apply(config);
            region_id = this->regions.size() - 1;
        }
        
        // assign volume to region
        o->add_region_volume(region_id, volume_id);
    }

    // apply config to print object
    o->config.apply(this->default_object_config);
    o->config.apply(object_config, true);
}

bool
Print::apply_config(DynamicPrintConfig config)
{
    // we get a copy of the config object so we can modify it safely
    config.normalize();
    
    // apply variables to placeholder parser
    this->placeholder_parser.apply_config(config);
    
    bool invalidated = false;
    
    // handle changes to print config
    t_config_option_keys print_diff = this->config.diff(config);
    if (!print_diff.empty()) {
        this->config.apply(config, true);
        
        if (this->invalidate_state_by_config_options(print_diff))
            invalidated = true;
    }
    
    // handle changes to object config defaults
    this->default_object_config.apply(config, true);
    FOREACH_OBJECT(this, obj_ptr) {
        // we don't assume that config contains a full ObjectConfig,
        // so we base it on the current print-wise default
        PrintObjectConfig new_config = this->default_object_config;
        new_config.apply(config, true);
        
        // we override the new config with object-specific options
        {
            DynamicPrintConfig model_object_config = (*obj_ptr)->model_object()->config;
            model_object_config.normalize();
            new_config.apply(model_object_config, true);
        }
        
        // check whether the new config is different from the current one
        t_config_option_keys diff = (*obj_ptr)->config.diff(new_config);
        if (!diff.empty()) {
            (*obj_ptr)->config.apply(new_config, true);
            
            if ((*obj_ptr)->invalidate_state_by_config_options(diff))
                invalidated = true;
        }
    }
    
    // handle changes to regions config defaults
    this->default_region_config.apply(config, true);
    
    // All regions now have distinct settings.
    // Check whether applying the new region config defaults we'd get different regions.
    bool rearrange_regions = false;
    std::vector<PrintRegionConfig> other_region_configs;
    FOREACH_REGION(this, it_r) {
        size_t region_id = it_r - this->regions.begin();
        PrintRegion* region = *it_r;
        
        std::vector<PrintRegionConfig> this_region_configs;
        FOREACH_OBJECT(this, it_o) {
            PrintObject* object = *it_o;
            
            std::vector<int> &region_volumes = object->region_volumes[region_id];
            for (std::vector<int>::const_iterator volume_id = region_volumes.begin(); volume_id != region_volumes.end(); ++volume_id) {
                ModelVolume* volume = object->model_object()->volumes.at(*volume_id);
                
                PrintRegionConfig new_config = this->_region_config_from_model_volume(*volume);
                
                for (std::vector<PrintRegionConfig>::iterator it = this_region_configs.begin(); it != this_region_configs.end(); ++it) {
                    // if the new config for this volume differs from the other
                    // volume configs currently associated to this region, it means
                    // the region subdivision does not make sense anymore
                    if (!it->equals(new_config)) {
                        rearrange_regions = true;
                        goto NEXT_REGION;
                    }
                }
                this_region_configs.push_back(new_config);
                
                for (std::vector<PrintRegionConfig>::iterator it = other_region_configs.begin(); it != other_region_configs.end(); ++it) {
                    // if the new config for this volume equals any of the other
                    // volume configs that are not currently associated to this
                    // region, it means the region subdivision does not make
                    // sense anymore
                    if (it->equals(new_config)) {
                        rearrange_regions = true;
                        goto NEXT_REGION;
                    }
                }
                
                // if we're here and the new region config is different from the old
                // one, we need to apply the new config and invalidate all objects
                // (possible optimization: only invalidate objects using this region)
                t_config_option_keys region_config_diff = region->config.diff(new_config);
                if (!region_config_diff.empty()) {
                    region->config.apply(new_config);
                    FOREACH_OBJECT(this, o) {
                        if ((*o)->invalidate_state_by_config_options(region_config_diff))
                            invalidated = true;
                    }
                }
            }
        }
        other_region_configs.insert(other_region_configs.end(), this_region_configs.begin(), this_region_configs.end());
        
        NEXT_REGION:
            continue;
    }
    
    if (rearrange_regions) {
        // the current subdivision of regions does not make sense anymore.
        // we need to remove all objects and re-add them
        ModelObjectPtrs model_objects;
        FOREACH_OBJECT(this, o) {
            model_objects.push_back((*o)->model_object());
        }
        this->clear_objects();
        for (ModelObjectPtrs::iterator it = model_objects.begin(); it != model_objects.end(); ++it) {
            this->add_model_object(*it);
        }
        invalidated = true;
    }
    
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

void
Print::validate() const
{
    if (this->config.complete_objects) {
        // check horizontal clearance
        {
            Polygons a;
            FOREACH_OBJECT(this, i_object) {
                PrintObject* object = *i_object;
                
                /*  get convex hull of all meshes assigned to this print object
                    (this is the same as model_object()->raw_mesh.convex_hull()
                    but probably more efficient */
                Polygon convex_hull;
                {
                    Polygons mesh_convex_hulls;
                    for (size_t i = 0; i < this->regions.size(); ++i) {
                        for (std::vector<int>::const_iterator it = object->region_volumes[i].begin(); it != object->region_volumes[i].end(); ++it) {
                            Polygon hull = object->model_object()->volumes[*it]->mesh.convex_hull();
                            mesh_convex_hulls.push_back(hull);
                        }
                    }
                
                    // make a single convex hull for all of them
                    convex_hull = Slic3r::Geometry::convex_hull(mesh_convex_hulls);
                }
                
                // apply the same transformations we apply to the actual meshes when slicing them
                object->model_object()->instances.front()->transform_polygon(&convex_hull);
                
                // grow convex hull with the clearance margin
                {
                    Polygons grown_hull;
                    offset(convex_hull, &grown_hull, scale_(this->config.extruder_clearance_radius.value)/2, 1, jtRound, scale_(0.1));
                    convex_hull = grown_hull.front();
                }
                
                // now we check that no instance of convex_hull intersects any of the previously checked object instances
                for (Points::const_iterator copy = object->_shifted_copies.begin(); copy != object->_shifted_copies.end(); ++copy) {
                    Polygon p = convex_hull;
                    p.translate(*copy);
                    if (intersects(a, p))
                        throw PrintValidationException("Some objects are too close; your extruder will collide with them.");
                    
                    union_(a, p, &a);
                }
            }
        }
        
        // check vertical clearance
        {
            std::vector<coord_t> object_height;
            FOREACH_OBJECT(this, i_object) {
                PrintObject* object = *i_object;
                object_height.insert(object_height.end(), object->copies().size(), object->size.z);
            }
            std::sort(object_height.begin(), object_height.end());
            // ignore the tallest *copy* (this is why we repeat height for all of them):
            // it will be printed as last one so its height doesn't matter
            object_height.pop_back();
            if (!object_height.empty() && object_height.back() > scale_(this->config.extruder_clearance_height.value))
                throw PrintValidationException("Some objects are too tall and cannot be printed without extruder collisions.");
        }
    }
    
    if (this->config.spiral_vase) {
        size_t total_copies_count = 0;
        FOREACH_OBJECT(this, i_object) total_copies_count += (*i_object)->copies().size();
        if (total_copies_count > 1)
            throw PrintValidationException("The Spiral Vase option can only be used when printing a single object.");
        if (this->regions.size() > 1)
            throw PrintValidationException("The Spiral Vase option can only be used when printing single material objects.");
    }
    
    {
        // find the smallest nozzle diameter
        std::set<size_t> extruders = this->extruders();
        if (extruders.empty())
            throw PrintValidationException("The supplied settings will cause an empty print.");
        
        std::set<double> nozzle_diameters;
        for (std::set<size_t>::iterator it = extruders.begin(); it != extruders.end(); ++it)
            nozzle_diameters.insert(this->config.nozzle_diameter.get_at(*it));
        double min_nozzle_diameter = *std::min_element(nozzle_diameters.begin(), nozzle_diameters.end());
        
        FOREACH_OBJECT(this, i_object) {
            PrintObject* object = *i_object;
            
            // validate first_layer_height
            double first_layer_height = object->config.get_abs_value("first_layer_height");
            double first_layer_min_nozzle_diameter;
            if (object->config.raft_layers > 0) {
                // if we have raft layers, only support material extruder is used on first layer
                size_t first_layer_extruder = object->config.raft_layers == 1
                    ? object->config.support_material_interface_extruder-1
                    : object->config.support_material_extruder-1;
                first_layer_min_nozzle_diameter = this->config.nozzle_diameter.get_at(first_layer_extruder);
            } else {
                // if we don't have raft layers, any nozzle diameter is potentially used in first layer
                first_layer_min_nozzle_diameter = min_nozzle_diameter;
            }
            if (first_layer_height > first_layer_min_nozzle_diameter)
                throw PrintValidationException("First layer height can't be greater than nozzle diameter");
            
            // validate layer_height
            if (object->config.layer_height.value > min_nozzle_diameter)
                throw PrintValidationException("Layer height can't be greater than nozzle diameter");
        }
    }
}

// the bounding box of objects placed in copies position
// (without taking skirt/brim/support material into account)
BoundingBox
Print::bounding_box() const
{
    BoundingBox bb;
    FOREACH_OBJECT(this, object) {
        for (Points::const_iterator copy = (*object)->_shifted_copies.begin(); copy != (*object)->_shifted_copies.end(); ++copy) {
            bb.merge(*copy);
            
            Point p = *copy;
            p.translate((*object)->size);
            bb.merge(p);
        }
    }
    return bb;
}

// the total bounding box of extrusions, including skirt/brim/support material
// this methods needs to be called even when no steps were processed, so it should
// only use configuration values
BoundingBox
Print::total_bounding_box() const
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

double
Print::skirt_first_layer_height() const
{
    if (this->objects.empty()) CONFESS("skirt_first_layer_height() can't be called without PrintObjects");
    return this->objects.front()->config.get_abs_value("first_layer_height");
}

Flow
Print::brim_flow() const
{
    ConfigOptionFloatOrPercent width = this->config.first_layer_extrusion_width;
    if (width.value == 0) width = this->regions.front()->config.perimeter_extrusion_width;
    
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

Flow
Print::skirt_flow() const
{
    ConfigOptionFloatOrPercent width = this->config.first_layer_extrusion_width;
    if (width.value == 0) width = this->regions.front()->config.perimeter_extrusion_width;
    
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


PrintRegionConfig
Print::_region_config_from_model_volume(const ModelVolume &volume)
{
    PrintRegionConfig config = this->default_region_config;
    {
        DynamicPrintConfig other_config = volume.get_object()->config;
        other_config.normalize();
        config.apply(other_config, true);
    }
    {
        DynamicPrintConfig other_config = volume.config;
        other_config.normalize();
        config.apply(other_config, true);
    }
    if (!volume.material_id().empty()) {
        DynamicPrintConfig material_config = volume.material()->config;
        material_config.normalize();
        config.apply(material_config, true);
    }
    return config;
}

bool
Print::has_support_material() const
{
    FOREACH_OBJECT(this, object) {
        if ((*object)->has_support_material()) return true;
    }
    return false;
}


#ifdef SLIC3RXS
REGISTER_CLASS(Print, "Print");
#endif


}
