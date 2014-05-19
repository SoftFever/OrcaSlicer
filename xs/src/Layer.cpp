#include "Layer.hpp"


namespace Slic3r {

LayerRegion::LayerRegion(Layer *layer, PrintRegion *region)
:   _layer(layer),
    _region(region)
{
}

LayerRegion::~LayerRegion()
{
}

Layer*
LayerRegion::layer()
{
    return this->_layer;
}

PrintRegion*
LayerRegion::region()
{
    return this->_region;
}

#ifdef SLIC3RXS
REGISTER_CLASS(LayerRegion, "Layer::Region");
#endif


Layer::Layer(int id, PrintObject *object, coordf_t height, coordf_t print_z,
        coordf_t slice_z)
:   _id(id),
    _object(object),
    upper_layer(NULL),
    lower_layer(NULL),
    regions(),
    slicing_errors(false),
    slice_z(slice_z),
    print_z(print_z),
    height(height),
    slices()
{
}

Layer::~Layer()
{
    // remove references to self
    if (NULL != this->upper_layer) {
        this->upper_layer->lower_layer = NULL;
    }

    if (NULL != this->lower_layer) {
        this->lower_layer->upper_layer = NULL;
    }

    this->clear_regions();
}

int
Layer::id()
{
    return this->_id;
}

PrintObject*
Layer::object()
{
    return this->_object;
}


size_t
Layer::region_count()
{
    return this->regions.size();
}

void
Layer::clear_regions()
{
    for (int i = this->regions.size()-1; i >= 0; --i)
        this->delete_region(i);
}

LayerRegion*
Layer::get_region(int idx)
{
    return this->regions.at(idx);
}

LayerRegion*
Layer::add_region(PrintRegion* print_region)
{
    LayerRegion* region = new LayerRegion(this, print_region);
    this->regions.push_back(region);
    return region;
}

void
Layer::delete_region(int idx)
{
    LayerRegionPtrs::iterator i = this->regions.begin() + idx;
    LayerRegion* item = *i;
    this->regions.erase(i);
    delete item;
}


#ifdef SLIC3RXS
REGISTER_CLASS(Layer, "Layer");
#endif


SupportLayer::SupportLayer(int id, PrintObject *object, coordf_t height,
        coordf_t print_z, coordf_t slice_z)
:   Layer(id, object, height, print_z, slice_z)
{
}

SupportLayer::~SupportLayer()
{
}

#ifdef SLIC3RXS
REGISTER_CLASS(SupportLayer, "Layer::Support");
#endif


}
