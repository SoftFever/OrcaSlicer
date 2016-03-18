#include "Layer.hpp"
#include "BridgeDetector.hpp"
#include "ClipperUtils.hpp"
#include "PerimeterGenerator.hpp"
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
LayerRegion::make_perimeters(const SurfaceCollection &slices, SurfaceCollection* fill_surfaces)
{
    this->perimeters.clear();
    this->thin_fills.clear();
    
    PerimeterGenerator g(
        // input:
        &slices,
        this->layer()->height,
        this->flow(frPerimeter),
        &this->region()->config,
        &this->layer()->object()->config,
        &this->layer()->object()->print()->config,
        
        // output:
        &this->perimeters,
        &this->thin_fills,
        fill_surfaces
    );
    
    if (this->layer()->lower_layer != NULL)
        g.lower_slices = &this->layer()->lower_layer->slices;
    
    g.layer_id              = this->layer()->id();
    g.ext_perimeter_flow    = this->flow(frExternalPerimeter);
    g.overhang_flow         = this->region()->flow(frPerimeter, -1, true, false, -1, *this->layer()->object());
    g.solid_infill_flow     = this->flow(frSolidInfill);
    
    g.process();
}

void
LayerRegion::process_external_surfaces(const Layer* lower_layer)
{
    const Surfaces &surfaces = this->fill_surfaces.surfaces;
    const double margin = scale_(EXTERNAL_INFILL_MARGIN);
    
    SurfaceCollection bottom;
    for (Surfaces::const_iterator surface = surfaces.begin(); surface != surfaces.end(); ++surface) {
        if (!surface->is_bottom()) continue;
        
        ExPolygons grown = offset_ex(surface->expolygon, +margin);
        
        /*  detect bridge direction before merging grown surfaces otherwise adjacent bridges
            would get merged into a single one while they need different directions
            also, supply the original expolygon instead of the grown one, because in case
            of very thin (but still working) anchors, the grown expolygon would go beyond them */
        double angle = -1;
        if (lower_layer != NULL) {
            BridgeDetector bd(
                surface->expolygon,
                lower_layer->slices,
                this->flow(frInfill, this->layer()->height, true).scaled_width()
            );
            
            #ifdef SLIC3R_DEBUG
            printf("Processing bridge at layer %zu:\n", this->layer()->id();
            #endif
            
            if (bd.detect_angle()) {
                angle = bd.angle;
            
                if (this->layer()->object()->config.support_material) {
                    Polygons coverage = bd.coverage();
                    this->bridged.insert(this->bridged.end(), coverage.begin(), coverage.end());
                    this->unsupported_bridge_edges.append(bd.unsupported_edges()); 
                }
            }
        }
        
        for (ExPolygons::const_iterator it = grown.begin(); it != grown.end(); ++it) {
            Surface s       = *surface;
            s.expolygon     = *it;
            s.bridge_angle  = angle;
            bottom.surfaces.push_back(s);
        }
    }
    
    SurfaceCollection top;
    for (Surfaces::const_iterator surface = surfaces.begin(); surface != surfaces.end(); ++surface) {
        if (surface->surface_type != stTop) continue;
        
        // give priority to bottom surfaces
        ExPolygons grown = diff_ex(
            offset(surface->expolygon, +margin),
            (Polygons)bottom
        );
        for (ExPolygons::const_iterator it = grown.begin(); it != grown.end(); ++it) {
            Surface s   = *surface;
            s.expolygon = *it;
            top.surfaces.push_back(s);
        }
    }
    
    /*  if we're slicing with no infill, we can't extend external surfaces
        over non-existent infill */
    SurfaceCollection fill_boundaries;
    if (this->region()->config.fill_density.value > 0) {
        fill_boundaries = SurfaceCollection(surfaces);
    } else {
        for (Surfaces::const_iterator it = surfaces.begin(); it != surfaces.end(); ++it) {
            if (it->surface_type != stInternal)
                fill_boundaries.surfaces.push_back(*it);
        }
    }
    
    // intersect the grown surfaces with the actual fill boundaries
    SurfaceCollection new_surfaces;
    {
        // merge top and bottom in a single collection
        SurfaceCollection tb = top;
        tb.surfaces.insert(tb.surfaces.end(), bottom.surfaces.begin(), bottom.surfaces.end());
        
        // group surfaces
        std::vector<SurfacesPtr> groups;
        tb.group(&groups);
        
        for (std::vector<SurfacesPtr>::const_iterator g = groups.begin(); g != groups.end(); ++g) {
            Polygons subject;
            for (SurfacesPtr::const_iterator s = g->begin(); s != g->end(); ++s) {
                Polygons pp = **s;
                subject.insert(subject.end(), pp.begin(), pp.end());
            }
            
            ExPolygons expp = intersection_ex(
                subject,
                (Polygons)fill_boundaries,
                true // to ensure adjacent expolygons are unified
            );
            
            for (ExPolygons::const_iterator ex = expp.begin(); ex != expp.end(); ++ex) {
                Surface s = *g->front();
                s.expolygon = *ex;
                new_surfaces.surfaces.push_back(s);
            }
        }
    }
    
    /* subtract the new top surfaces from the other non-top surfaces and re-add them */
    {
        SurfaceCollection other;
        for (Surfaces::const_iterator s = surfaces.begin(); s != surfaces.end(); ++s) {
            if (s->surface_type != stTop && !s->is_bottom())
                other.surfaces.push_back(*s);
        }
        
        // group surfaces
        std::vector<SurfacesPtr> groups;
        other.group(&groups);
        
        for (std::vector<SurfacesPtr>::const_iterator g = groups.begin(); g != groups.end(); ++g) {
            Polygons subject;
            for (SurfacesPtr::const_iterator s = g->begin(); s != g->end(); ++s) {
                Polygons pp = **s;
                subject.insert(subject.end(), pp.begin(), pp.end());
            }
            
            ExPolygons expp = diff_ex(
                subject,
                (Polygons)new_surfaces
            );
            
            for (ExPolygons::const_iterator ex = expp.begin(); ex != expp.end(); ++ex) {
                Surface s = *g->front();
                s.expolygon = *ex;
                new_surfaces.surfaces.push_back(s);
            }
        }
    }
    
    this->fill_surfaces = new_surfaces;
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

double
LayerRegion::infill_area_threshold() const
{
    double ss = this->flow(frSolidInfill).scaled_spacing();
    return ss*ss;
}

}
