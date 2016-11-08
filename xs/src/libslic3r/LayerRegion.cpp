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
    Surfaces bottom;
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
            ExPolygons expolygons;
            expolygons.push_back(surface->expolygon);
            BridgeDetector bd(
                expolygons,
                lower_layer->slices,
                this->flow(frInfill, true, this->layer()->height).scaled_width()
            );
            
            #ifdef SLIC3R_DEBUG
            printf("Processing bridge at layer " PRINTF_ZU ":\n", this->layer()->id();
            #endif
            
            if (bd.detect_angle()) {
                angle = bd.angle;
            
                if (this->layer()->object()->config.support_material) {
                    polygons_append(this->bridged, bd.coverage());
                    this->unsupported_bridge_edges.append(bd.unsupported_edges()); 
                }
            }
        }
        
        for (ExPolygons::const_iterator it = grown.begin(); it != grown.end(); ++it) {
            Surface s       = *surface;
            s.expolygon     = *it;
            s.bridge_angle  = angle;
            bottom.push_back(s);
        }
    }
#else
    // 1) Collect bottom and bridge surfaces, each of them grown by a fixed 3mm offset
    // for better anchoring.
    // Bottom surfaces, grown.
    Surfaces                    bottom;
    // Bridge surfaces, initialy not grown.
    Surfaces                    bridges;
    // Bridge expolygons, grown, to be tested for intersection with other bridge regions.
    std::vector<Polygons>       bridges_grown;
    // Bounding boxes of bridges_grown.
    std::vector<BoundingBox>    bridge_bboxes;
    // For all stBottom && stBottomBridge surfaces:
    for (Surfaces::const_iterator surface = surfaces.begin(); surface != surfaces.end(); ++surface) {
        if (surface->surface_type == stBottom || lower_layer == NULL) {
            // Grown by 3mm.
            surfaces_append(bottom, offset_ex(surface->expolygon, float(margin)), *surface);
        } else if (surface->surface_type == stBottomBridge) {
            bridges.push_back(*surface);
            // Grown by 3mm.
            bridges_grown.push_back(offset(surface->expolygon, float(margin)));
            bridge_bboxes.push_back(get_extents(bridges_grown.back()));
        }
    }

#if 0
    {
        static int iRun = 0;
        bridges.export_to_svg(debug_out_path("bridges-before-grouping-%d.svg", iRun ++), true);
    }
#endif

    if (lower_layer != NULL)
    {
        // 2) Group the bridge surfaces by overlaps.
        std::vector<size_t> bridge_group(bridges.size(), (size_t)-1);
        size_t n_groups = 0; 
        for (size_t i = 0; i < bridges.size(); ++ i) {
            // A grup id for this bridge.
            size_t group_id = (bridge_group[i] == -1) ? (n_groups ++) : bridge_group[i];
            bridge_group[i] = group_id;
            // For all possibly overlaping bridges:
            for (size_t j = i + 1; j < bridges.size(); ++ j) {
                if (! bridge_bboxes[i].overlap(bridge_bboxes[j]))
                    continue;
                if (intersection(bridges_grown[i], bridges_grown[j], false).empty())
                    continue;
                // The two bridge regions intersect. Give them the same group id.
                if (bridge_group[j] != -1) {
                    // The j'th bridge has been merged with some other bridge before.
                    size_t group_id_new = bridge_group[j];
                    for (size_t k = i; k < j; ++ k)
                        if (bridge_group[k] == group_id)
                            bridge_group[k] = group_id_new;
                    group_id = group_id_new;
                }
                bridge_group[j] = group_id;
            }
        }

        // 3) Merge the groups with the same group id, detect bridges.
        {
            for (size_t group_id = 0; group_id < n_groups; ++ group_id) {
                size_t n_bridges_merged = 0;
                size_t idx_last = (size_t)-1;
                for (size_t i = 0; i < bridges.size(); ++ i) {
                    if (bridge_group[i] == group_id) {
                        ++ n_bridges_merged;
                        idx_last = i;
                    }
                }
                if (n_bridges_merged == 0)
                    // This group has no regions assigned as these were moved into another group.
                    continue;
                // Collect the initial ungrown regions and the grown polygons.
                ExPolygons  initial;
                Polygons    grown;
                for (size_t i = 0; i < bridges.size(); ++ i) {
                    if (bridge_group[i] != group_id)
                        continue;
                    initial.push_back(STDMOVE(bridges[i].expolygon));
                    polygons_append(grown, bridges_grown[i]);
                }
                // detect bridge direction before merging grown surfaces otherwise adjacent bridges
                // would get merged into a single one while they need different directions
                // also, supply the original expolygon instead of the grown one, because in case
                // of very thin (but still working) anchors, the grown expolygon would go beyond them
                BridgeDetector bd(
                    initial,
                    lower_layer->slices,
                    //FIXME parameters are not correct!
                    // flow(FlowRole role, bool bridge = false, double width = -1) const;
                    this->flow(frInfill, true, this->layer()->height).scaled_width()
                );
                #ifdef SLIC3R_DEBUG
                printf("Processing bridge at layer " PRINTF_ZU ":\n", this->layer()->id());
                #endif
                if (bd.detect_angle()) {
                    bridges[idx_last].bridge_angle = bd.angle;
                    if (this->layer()->object()->config.support_material) {
                        polygons_append(this->bridged, bd.coverage());
                        this->unsupported_bridge_edges.append(bd.unsupported_edges()); 
                    }
                }
                // without safety offset, artifacts are generated (GH #2494)
                surfaces_append(bottom, union_ex(grown, true), bridges[idx_last]);
            }
        }

    #if 0
        {
            static int iRun = 0;
            bridges.export_to_svg(debug_out_path("bridges-after-grouping-%d.svg", iRun ++), true);
        }
    #endif
    }
#endif

    // Collect top surfaces and internal surfaces.
    // Collect fill_boundaries: If we're slicing with no infill, we can't extend external surfaces over non-existent infill.
    Surfaces        top;
    Surfaces        internal;
    Polygons        fill_boundaries;
    // This loop destroys the surfaces (aliasing this->fill_surfaces.surfaces) by moving into top/internal/fill_boundaries!
    {
        // bottom_polygons are used to trim inflated top surfaces.
        const Polygons bottom_polygons = to_polygons(bottom);
        fill_boundaries.reserve(number_polygons(surfaces));
        bool has_infill = this->region()->config.fill_density.value > 0.;
        for (Surfaces::iterator surface = this->fill_surfaces.surfaces.begin(); surface != this->fill_surfaces.surfaces.end(); ++surface) {
            if (surface->surface_type == stTop)
                // Collect the top surfaces, inflate them and trim them by the bottom surfaces.
                // This gives the priority to bottom surfaces.
                surfaces_append(
                    top,
                    STDMOVE(diff_ex(offset(surface->expolygon, float(margin)), bottom_polygons)),
                    *surface); // template
            bool internal_surface = surface->surface_type != stTop && ! surface->is_bottom();
            if (has_infill || surface->surface_type != stInternal) {
                if (internal_surface)
                    // Make a copy as the following line uses the move semantics.
                    internal.push_back(*surface);
                polygons_append(fill_boundaries, STDMOVE(surface->expolygon));
            } else if (internal_surface)
                internal.push_back(STDMOVE(*surface));
        }
    }

    Surfaces new_surfaces;

    // Merge top and bottom in a single collection.
    surfaces_append(top, STDMOVE(bottom));
    // Intersect the grown surfaces with the actual fill boundaries.
    for (size_t i = 0; i < top.size(); ++ i) {
        Surface &s1 = top[i];
        if (s1.empty())
            continue;
        Polygons polys;
        polygons_append(polys, STDMOVE(s1));
        for (size_t j = i + 1; j < top.size(); ++ j) {
            Surface &s2 = top[j];
            if (! s2.empty() && surfaces_could_merge(s1, s2))
                polygons_append(polys, STDMOVE(s2));
        }
        surfaces_append(
            new_surfaces,
            STDMOVE(intersection_ex(polys, fill_boundaries, true)),
            s1);
    }
    
    // Subtract the new top surfaces from the other non-top surfaces and re-add them.
    Polygons new_polygons = to_polygons(new_surfaces);
    for (size_t i = 0; i < internal.size(); ++ i) {
        Surface &s1 = internal[i];
        if (s1.empty())
            continue;
        Polygons polys;
        polygons_append(polys, STDMOVE(s1));
        for (size_t j = i + 1; j < internal.size(); ++ j) {
            Surface &s2 = internal[j];
            if (! s2.empty() && surfaces_could_merge(s1, s2))
                polygons_append(polys, STDMOVE(s2));
        }
        ExPolygons new_expolys = diff_ex(polys, new_polygons);
        polygons_append(new_polygons, to_polygons(new_expolys));
        surfaces_append(new_surfaces, STDMOVE(new_expolys), s1);
    }
    
    this->fill_surfaces.surfaces = STDMOVE(new_surfaces);

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
    this->export_region_slices_to_svg(debug_out_path("LayerRegion-slices-%s-%d.svg", name, idx ++).c_str());
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
    this->export_region_fill_surfaces_to_svg(debug_out_path("LayerRegion-fill_surfaces-%s-%d.svg", name, idx ++).c_str());
}

}
 