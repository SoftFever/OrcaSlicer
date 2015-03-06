#include "Layer.hpp"
#include "ClipperUtils.hpp"
#include "Print.hpp"
#include "Surface.hpp"

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

void
LayerRegion::merge_slices()
{
    ExPolygons expp;
    // without safety offset, artifacts are generated (GH #2494)
    union_(this->slices, &expp, true);
    this->slices.surfaces.clear();
    this->slices.surfaces.reserve(expp.size());
    
    for (ExPolygons::const_iterator expoly = expp.begin(); expoly != expp.end(); ++expoly)
        this->slices.surfaces.push_back(Surface(stInternal, *expoly));
}

void
LayerRegion::prepare_fill_surfaces()
{
    /*  Note: in order to make the psPrepareInfill step idempotent, we should never
        alter fill_surfaces boundaries on which our idempotency relies since that's
        the only meaningful information returned by psPerimeters. */
    
    // if no solid layers are requested, turn top/bottom surfaces to internal
    if (this->region()->config.top_solid_layers == 0) {
        for (Surfaces::iterator surface = this->fill_surfaces.surfaces.begin(); surface != this->fill_surfaces.surfaces.end(); ++surface) {
            if (surface->surface_type == stTop) {
                if (this->layer()->object()->config.infill_only_where_needed) {
                    surface->surface_type = stInternalVoid;
                } else {
                    surface->surface_type = stInternal;
                }
            }
        }
    }
    if (this->region()->config.bottom_solid_layers == 0) {
        for (Surfaces::iterator surface = this->fill_surfaces.surfaces.begin(); surface != this->fill_surfaces.surfaces.end(); ++surface) {
            if (surface->surface_type == stBottom || surface->surface_type == stBottomBridge)
                surface->surface_type = stInternal;
        }
    }
        
    // turn too small internal regions into solid regions according to the user setting
    if (this->region()->config.fill_density.value > 0) {
        // scaling an area requires two calls!
        double min_area = scale_(scale_(this->region()->config.solid_infill_below_area.value));
        for (Surfaces::iterator surface = this->fill_surfaces.surfaces.begin(); surface != this->fill_surfaces.surfaces.end(); ++surface) {
            if (surface->surface_type == stInternal && surface->area() <= min_area)
                surface->surface_type = stInternalSolid;
        }
    }
}

#ifdef SLIC3RXS
REGISTER_CLASS(LayerRegion, "Layer::Region");
#endif

}
