#include "Layer.hpp"
#include "BridgeDetector.hpp"
#include "ClipperUtils.hpp"
#include "PerimeterGenerator.hpp"
#include "Print.hpp"
#include "Surface.hpp"
#include "BoundingBox.hpp"
#include "SVG.hpp"

#include <string>
#include <map>

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
LayerRegion::make_perimeters(const SurfaceCollection &slices, SurfaceCollection* perimeter_surfaces, SurfaceCollection* fill_surfaces)
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
        perimeter_surfaces, 
        fill_surfaces
    );
    
    if (this->layer()->lower_layer != NULL)
        // Cummulative sum of polygons over all the regions.
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
    
#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_fill_surfaces_to_svg_debug("3_process_external_surfaces-initial");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */

#if 0
    SurfaceCollection bottom;
    // For all stBottom && stBottomBridge surfaces:
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
#else
    // 1) Collect bottom and bridge surfaces, each of them grown by a fixed 3mm offset
    // for better anchoring.
    SurfaceCollection bottom;
    SurfaceCollection bridges;
    std::vector<BoundingBox> bridge_bboxes;
    // For all stBottom && stBottomBridge surfaces:
    for (Surfaces::const_iterator surface = surfaces.begin(); surface != surfaces.end(); ++surface) {
        if (!surface->is_bottom()) continue;
        // Grown by 3mm.
        ExPolygons grown = offset_ex(surface->expolygon, +margin);
        for (ExPolygons::const_iterator it = grown.begin(); it != grown.end(); ++it) {
            Surface s       = *surface;
            s.expolygon     = *it;
            if (surface->surface_type == stBottomBridge) {
                bridges.surfaces.push_back(s);
                bridge_bboxes.push_back(get_extents(s));
            } else
                bottom.surfaces.push_back(s);
        }
    }

#if 0
    {
        char path[2048];
        static int iRun = 0;
        sprintf(path, "out\\bridges-before-grouping-%d.svg", iRun ++);
        bridges.export_to_svg(path, true);
    }
#endif

    // 2) Group the bridge surfaces by overlaps.
    std::vector<size_t> bridge_group(bridges.surfaces.size(), (size_t)-1);
    size_t n_groups = 0; 
    for (size_t i = 0; i < bridges.surfaces.size(); ++ i) {
        // A grup id for this bridge.
        size_t group_id = (bridge_group[i] == -1) ? (n_groups ++) : bridge_group[i];
        bridge_group[i] = group_id;
        // For all possibly overlaping bridges:
        for (size_t j = i + 1; j < bridges.surfaces.size(); ++ j) {
            if (! bridge_bboxes[i].overlap(bridge_bboxes[j]))
                continue;
            if (! bridges.surfaces[i].expolygon.overlaps(bridges.surfaces[j].expolygon))
                continue;
            // The two bridge regions intersect. Give them the same group id.
            if (bridge_group[j] != -1) {
                // The j'th bridge has been merged with some other bridge before.
                size_t group_id_new = bridge_group[j];
                for (size_t k = 0; k < j; ++ k)
                    if (bridge_group[k] == group_id)
                        bridge_group[k] = group_id_new;
                group_id = group_id_new;
            }
            bridge_group[j] = group_id;
        }
    }

    // 3) Merge the groups with the same group id.
    {
        SurfaceCollection bridges_merged;
        bridges_merged.surfaces.reserve(n_groups);
        for (size_t group_id = 0; group_id < n_groups; ++ group_id) {
            size_t n_bridges_merged = 0;
            size_t idx_last = (size_t)-1;
            for (size_t i = 0; i < bridges.surfaces.size(); ++ i) {
                if (bridge_group[i] == group_id) {
                    ++ n_bridges_merged;
                    idx_last = i;
                }
            }
            if (n_bridges_merged == 1)
                bridges_merged.surfaces.push_back(bridges.surfaces[idx_last]);
            else if (n_bridges_merged > 1) {
                Slic3r::Polygons polygons;
                for (size_t i = 0; i < bridges.surfaces.size(); ++ i) {
                    if (bridge_group[i] != group_id)
                        continue;
                    const Surface &surface = bridges.surfaces[i];
                    polygons.push_back(surface.expolygon.contour);
                    for (size_t j = 0; j < surface.expolygon.holes.size(); ++ j)
                        polygons.push_back(surface.expolygon.holes[j]);
                }
                ExPolygons expp;
                // without safety offset, artifacts are generated (GH #2494)
                union_(polygons, &expp, true);
                Surface &surface0 = bridges.surfaces[idx_last];
                surface0.expolygon.clear();
                for (size_t i = 0; i < expp.size(); ++ i) {
                    surface0.expolygon = expp[i];
                    bridges_merged.surfaces.push_back(surface0);
                }
            }
        }
        std::swap(bridges_merged, bridges);
    }

#if 0
    {
        char path[2048];
        static int iRun = 0;
        sprintf(path, "out\\bridges-after-grouping-%d.svg", iRun ++);
        bridges.export_to_svg(path, true);
    }
#endif

    // 4) Detect bridge directions.
    if (lower_layer != NULL) {
        for (size_t i = 0; i < bridges.surfaces.size(); ++ i) {
            Surface &surface = bridges.surfaces[i];
            /*  detect bridge direction before merging grown surfaces otherwise adjacent bridges
                would get merged into a single one while they need different directions
                also, supply the original expolygon instead of the grown one, because in case
                of very thin (but still working) anchors, the grown expolygon would go beyond them */
            BridgeDetector bd(
                surface.expolygon,
                lower_layer->slices,
                this->flow(frInfill, this->layer()->height, true).scaled_width()
            );
            #ifdef SLIC3R_DEBUG
            printf("Processing bridge at layer %zu:\n", this->layer()->id();
            #endif            
            if (bd.detect_angle()) {
                surface.bridge_angle = bd.angle;
                if (this->layer()->object()->config.support_material) {
                    Polygons coverage = bd.coverage();
                    this->bridged.insert(this->bridged.end(), coverage.begin(), coverage.end());
                    this->unsupported_bridge_edges.append(bd.unsupported_edges()); 
                }
            }
        }
    }
    bottom.surfaces.insert(bottom.surfaces.end(), bridges.surfaces.begin(), bridges.surfaces.end());
#endif

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

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_fill_surfaces_to_svg_debug("3_process_external_surfaces-final");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
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

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    export_region_slices_to_svg_debug("2_prepare_fill_surfaces");
#endif /* SLIC3R_DEBUG_SLICE_PROCESSING */
}

double
LayerRegion::infill_area_threshold() const
{
    double ss = this->flow(frSolidInfill).scaled_spacing();
    return ss*ss;
}


void LayerRegion::export_region_slices_to_svg(const char *path)
{
    BoundingBox bbox;
    for (Surfaces::const_iterator surface = this->slices.surfaces.begin(); surface != this->slices.surfaces.end(); ++surface)
        bbox.merge(get_extents(surface->expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min.x, bbox.max.y);
    bbox.merge(Point(std::max(bbox.min.x + legend_size.x, bbox.max.x), bbox.max.y + legend_size.y));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (Surfaces::const_iterator surface = this->slices.surfaces.begin(); surface != this->slices.surfaces.end(); ++surface)
        svg.draw(surface->expolygon, surface_type_to_color_name(surface->surface_type), transparency);
    for (Surfaces::const_iterator surface = this->fill_surfaces.surfaces.begin(); surface != this->fill_surfaces.surfaces.end(); ++surface)
        svg.draw(surface->expolygon.lines(), surface_type_to_color_name(surface->surface_type));
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void LayerRegion::export_region_slices_to_svg_debug(const char *name)
{
    static std::map<std::string, size_t> idx_map;
    size_t &idx = idx_map[name];
    char path[2048];
    sprintf(path, "out\\LayerRegion-slices-%s-%d.svg", name, idx ++);
    this->export_region_slices_to_svg(path);
}

void LayerRegion::export_region_fill_surfaces_to_svg(const char *path) 
{
    BoundingBox bbox;
    for (Surfaces::const_iterator surface = this->fill_surfaces.surfaces.begin(); surface != this->fill_surfaces.surfaces.end(); ++surface)
        bbox.merge(get_extents(surface->expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min.x, bbox.max.y);
    bbox.merge(Point(std::max(bbox.min.x + legend_size.x, bbox.max.x), bbox.max.y + legend_size.y));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (Surfaces::const_iterator surface = this->fill_surfaces.surfaces.begin(); surface != this->fill_surfaces.surfaces.end(); ++surface)
        svg.draw(surface->expolygon, surface_type_to_color_name(surface->surface_type), transparency);
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void LayerRegion::export_region_fill_surfaces_to_svg_debug(const char *name)
{
    static std::map<std::string, size_t> idx_map;
    size_t &idx = idx_map[name];
    char path[2048];
    sprintf(path, "out\\LayerRegion-fill_surfaces-%s-%d.svg", name, idx ++);
    this->export_region_fill_surfaces_to_svg(path);
}

}
 