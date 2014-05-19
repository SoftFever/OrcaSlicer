#include "Print.hpp"
#include "BoundingBox.hpp"

namespace Slic3r {

bool
PrintState::started(PrintStep step) const
{
    return this->_started.find(step) != this->_started.end();
}

bool
PrintState::done(PrintStep step) const
{
    return this->_done.find(step) != this->_done.end();
}

void
PrintState::set_started(PrintStep step)
{
    this->_started.insert(step);
}

void
PrintState::set_done(PrintStep step)
{
    this->_done.insert(step);
}

void
PrintState::invalidate(PrintStep step)
{
    this->_started.erase(step);
    this->_done.erase(step);
}

void
PrintState::invalidate_all()
{
    this->_started.clear();
    this->_done.clear();
}

#ifdef SLIC3RXS
REGISTER_CLASS(PrintState, "Print::State");
#endif



PrintRegion::PrintRegion(Print* print)
:   config(), _print(print)
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

PrintConfig &
PrintRegion::print_config()
{
    return this->_print->config;
}

#ifdef SLIC3RXS
REGISTER_CLASS(PrintRegion, "Print::Region");
#endif


PrintObject::PrintObject(Print* print, ModelObject* model_object,
        const BoundingBoxf3 &modobj_bbox)
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
PrintObject::add_layer(int id, coordf_t height, coordf_t print_z,
    coordf_t slice_z)
{
    Layer* layer = new Layer(id, this, height, print_z, slice_z);
    layers.push_back(layer);
    return layer;
}

void
PrintObject::delete_layer(int idx)
{
    LayerPtrs::iterator i = this->layers.begin() + idx;
    Layer* item = *i;
    this->layers.erase(i);
    delete item;
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
    SupportLayer* item = *i;
    this->support_layers.erase(i);
    delete item;
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

    this->_state.invalidate(psSkirt);
    this->_state.invalidate(psBrim);
}

PrintObject*
Print::get_object(int idx)
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
Print::set_new_object(size_t idx, ModelObject *model_object,
        const BoundingBoxf3 &modobj_bbox)
{
    if (idx < 0 || idx >= this->objects.size()) throw "bad idx";

    PrintObjectPtrs::iterator old_it = this->objects.begin() + idx;
    delete *old_it;

    PrintObject *object = new PrintObject(this, model_object, modobj_bbox);
    this->objects[idx] = object;
    return object;
}

void
Print::delete_object(int idx)
{
    PrintObjectPtrs::iterator i = this->objects.begin() + idx;
    PrintObject* item = *i;
    this->objects.erase(i);
    delete item;

    // TODO: purge unused regions

    this->_state.invalidate(psSkirt);
    this->_state.invalidate(psBrim);
}

void
Print::clear_regions()
{
    for (int i = this->regions.size()-1; i >= 0; --i)
        this->delete_region(i);
}

PrintRegion*
Print::get_region(int idx)
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
Print::delete_region(int idx)
{
    PrintRegionPtrs::iterator i = this->regions.begin() + idx;
    PrintRegion* item = *i;
    this->regions.erase(i);
    delete item;
}


#ifdef SLIC3RXS
REGISTER_CLASS(Print, "Print");
#endif


}
