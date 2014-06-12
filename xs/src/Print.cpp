#include "Print.hpp"
#include "BoundingBox.hpp"

namespace Slic3r {

template <class StepClass>
bool
PrintState<StepClass>::started(StepClass step) const
{
    return this->_started.find(step) != this->_started.end();
}

template <class StepClass>
bool
PrintState<StepClass>::done(StepClass step) const
{
    return this->_done.find(step) != this->_done.end();
}

template <class StepClass>
void
PrintState<StepClass>::set_started(StepClass step)
{
    this->_started.insert(step);
}

template <class StepClass>
void
PrintState<StepClass>::set_done(StepClass step)
{
    this->_done.insert(step);
}

template <class StepClass>
void
PrintState<StepClass>::invalidate(StepClass step)
{
    this->_started.erase(step);
    this->_done.erase(step);
}

template <class StepClass>
bool
PrintState<StepClass>::invalidate_all()
{
    bool empty = this->_started.empty();
    this->_started.clear();
    this->_done.clear();
    return !empty;  // return true if we invalidated something
}

template class PrintState<PrintStep>;
template class PrintState<PrintObjectStep>;


PrintRegion::PrintRegion(Print* print)
    : _print(print)
{
}

PrintRegion::~PrintRegion()
{
}

Print*
PrintRegion::print()
{
    return this->_print;
}

#ifdef SLIC3RXS
REGISTER_CLASS(PrintRegion, "Print::Region");
#endif


PrintObject::PrintObject(Print* print, ModelObject* model_object, const BoundingBoxf3 &modobj_bbox)
:   _print(print),
    _model_object(model_object)
{
    region_volumes.resize(this->_print->regions.size());

    // Compute the translation to be applied to our meshes so that we work with smaller coordinates
    {
        // Translate meshes so that our toolpath generation algorithms work with smaller
        // XY coordinates; this translation is an optimization and not strictly required.
        // A cloned mesh will be aligned to 0 before slicing in _slice_region() since we
        // don't assume it's already aligned and we don't alter the original position in model.
        // We store the XY translation so that we can place copies correctly in the output G-code
        // (copies are expressed in G-code coordinates and this translation is not publicly exposed).
        this->_copies_shift = Point(
            scale_(modobj_bbox.min.x), scale_(modobj_bbox.min.y));

        // TODO: $self->_trigger_copies;

        // Scale the object size and store it
        Pointf3 size = modobj_bbox.size();
        this->size = Point3(scale_(size.x), scale_(size.y), scale_(size.z));
    }
}

PrintObject::~PrintObject()
{
}

Print*
PrintObject::print()
{
    return this->_print;
}

ModelObject*
PrintObject::model_object()
{
    return this->_model_object;
}

void
PrintObject::add_region_volume(int region_id, int volume_id)
{
    if (region_id >= region_volumes.size()) {
        region_volumes.resize(region_id + 1);
    }

    region_volumes[region_id].push_back(volume_id);
}

size_t
PrintObject::layer_count()
{
    return this->layers.size();
}

void
PrintObject::clear_layers()
{
    for (int i = this->layers.size()-1; i >= 0; --i)
        this->delete_layer(i);
}

Layer*
PrintObject::get_layer(int idx)
{
    return this->layers.at(idx);
}

Layer*
PrintObject::add_layer(int id, coordf_t height, coordf_t print_z, coordf_t slice_z)
{
    Layer* layer = new Layer(id, this, height, print_z, slice_z);
    layers.push_back(layer);
    return layer;
}

void
PrintObject::delete_layer(int idx)
{
    LayerPtrs::iterator i = this->layers.begin() + idx;
    delete *i;
    this->layers.erase(i);
}

size_t
PrintObject::support_layer_count()
{
    return this->support_layers.size();
}

void
PrintObject::clear_support_layers()
{
    for (int i = this->support_layers.size()-1; i >= 0; --i)
        this->delete_support_layer(i);
}

SupportLayer*
PrintObject::get_support_layer(int idx)
{
    return this->support_layers.at(idx);
}

SupportLayer*
PrintObject::add_support_layer(int id, coordf_t height, coordf_t print_z,
    coordf_t slice_z)
{
    SupportLayer* layer = new SupportLayer(id, this, height, print_z, slice_z);
    support_layers.push_back(layer);
    return layer;
}

void
PrintObject::delete_support_layer(int idx)
{
    SupportLayerPtrs::iterator i = this->support_layers.begin() + idx;
    delete *i;
    this->support_layers.erase(i);
}

bool
PrintObject::invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys)
{
    std::set<PrintObjectStep> steps;
    
    // this method only accepts PrintObjectConfig and PrintRegionConfig option keys
    for (std::vector<t_config_option_key>::const_iterator opt_key = opt_keys.begin(); opt_key != opt_keys.end(); ++opt_key) {
        if (*opt_key == "perimeters"
            || *opt_key == "extra_perimeters"
            || *opt_key == "gap_fill_speed"
            || *opt_key == "overhangs"
            || *opt_key == "perimeter_extrusion_width"
            || *opt_key == "thin_walls"
            || *opt_key == "external_perimeters_first") {
            steps.insert(posPerimeters);
        } else if (*opt_key == "resolution"
            || *opt_key == "layer_height"
            || *opt_key == "first_layer_height"
            || *opt_key == "xy_size_compensation"
            || *opt_key == "raft_layers") {
            steps.insert(posSlice);
        } else if (*opt_key == "support_material"
            || *opt_key == "support_material_angle"
            || *opt_key == "support_material_extruder"
            || *opt_key == "support_material_extrusion_width"
            || *opt_key == "support_material_interface_layers"
            || *opt_key == "support_material_interface_extruder"
            || *opt_key == "support_material_interface_spacing"
            || *opt_key == "support_material_interface_speed"
            || *opt_key == "support_material_pattern"
            || *opt_key == "support_material_spacing"
            || *opt_key == "support_material_threshold"
            || *opt_key == "dont_support_bridges") {
            steps.insert(posSupportMaterial);
        } else if (*opt_key == "interface_shells"
            || *opt_key == "infill_only_where_needed"
            || *opt_key == "bottom_solid_layers"
            || *opt_key == "top_solid_layers"
            || *opt_key == "infill_extruder"
            || *opt_key == "infill_extrusion_width") {
            steps.insert(posPrepareInfill);
        } else if (*opt_key == "fill_angle"
            || *opt_key == "fill_pattern"
            || *opt_key == "solid_fill_pattern"
            || *opt_key == "infill_every_layers"
            || *opt_key == "solid_infill_below_area"
            || *opt_key == "solid_infill_every_layers"
            || *opt_key == "top_infill_extrusion_width") {
            steps.insert(posInfill);
        } else if (*opt_key == "fill_density"
            || *opt_key == "solid_infill_extrusion_width") {
            steps.insert(posPerimeters);
            steps.insert(posPrepareInfill);
        } else if (*opt_key == "external_perimeter_extrusion_width"
            || *opt_key == "perimeter_extruder") {
            steps.insert(posPerimeters);
            steps.insert(posSupportMaterial);
        } else if (*opt_key == "bridge_flow_ratio") {
            steps.insert(posPerimeters);
            steps.insert(posInfill);
        } else {
            // for legacy, if we can't handle this option let's signal the caller to invalidate all steps
            return false;
        }
    }
    
    for (std::set<PrintObjectStep>::const_iterator step = steps.begin(); step != steps.end(); ++step)
        this->invalidate_step(*step);
    
    return true;
}

void
PrintObject::invalidate_step(PrintObjectStep step)
{
    this->state.invalidate(step);
    
    // propagate to dependent steps
    if (step == posPerimeters) {
        this->invalidate_step(posPrepareInfill);
        this->_print->invalidate_step(psSkirt);
        this->_print->invalidate_step(psBrim);
    } else if (step == posPrepareInfill) {
        this->invalidate_step(posInfill);
    } else if (step == posInfill) {
        this->_print->invalidate_step(psSkirt);
        this->_print->invalidate_step(psBrim);
    } else if (step == posSlice) {
        this->invalidate_step(posPerimeters);
        this->invalidate_step(posSupportMaterial);
    }
}


#ifdef SLIC3RXS
REGISTER_CLASS(PrintObject, "Print::Object");
#endif


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

    this->state.invalidate(psSkirt);
    this->state.invalidate(psBrim);
}

PrintObject*
Print::get_object(size_t idx)
{
    return objects.at(idx);
}

PrintObject*
Print::add_object(ModelObject *model_object,
        const BoundingBoxf3 &modobj_bbox)
{
    PrintObject *object = new PrintObject(this, model_object, modobj_bbox);
    objects.push_back(object);
    return object;
}

PrintObject*
Print::set_new_object(size_t idx, ModelObject *model_object, const BoundingBoxf3 &modobj_bbox)
{
    if (idx >= this->objects.size()) throw "bad idx";

    PrintObjectPtrs::iterator old_it = this->objects.begin() + idx;
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
        } else {
            // for legacy, if we can't handle this option let's signal the caller to invalidate all steps
            return false;
        }
    }
    
    for (std::set<PrintStep>::const_iterator step = steps.begin(); step != steps.end(); ++step)
        this->invalidate_step(*step);
    
    return true;
}

void
Print::invalidate_step(PrintStep step)
{
    this->state.invalidate(step);
    
    // propagate to dependent steps
    if (step == psSkirt) {
        this->invalidate_step(psBrim);
    } else if (step == psInitExtruders) {
        for (PrintObjectPtrs::iterator object = this->objects.begin(); object != this->objects.end(); ++object) {
            (*object)->invalidate_step(posPerimeters);
            (*object)->invalidate_step(posSupportMaterial);
        }
    }
}


#ifdef SLIC3RXS
REGISTER_CLASS(Print, "Print");
#endif


}
