#include "Layer.hpp"
#include "Print.hpp"

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

Flow
LayerRegion::flow(FlowRole role, bool bridge, double width) const
{
    return this->_region->flow(
        role,
        this->_layer->height,
        bridge,
        this->_layer->id() == 0,
        width,
        *this->_layer->object()
    );
}

#ifdef SLIC3RXS
REGISTER_CLASS(LayerRegion, "Layer::Region");
#endif

}
