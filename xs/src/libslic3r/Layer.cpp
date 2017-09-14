#include "Layer.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "Print.hpp"
#include "Fill/Fill.hpp"
#include "SVG.hpp"

#include <boost/log/trivial.hpp>

namespace Slic3r {

Layer::~Layer()
{
    this->lower_layer = this->upper_layer = nullptr;
    for (LayerRegion *region : this->regions)
        delete region;
    this->regions.clear();
}

LayerRegion* Layer::add_region(PrintRegion* print_region)
{
    this->regions.emplace_back(new LayerRegion(this, print_region));
    return this->regions.back();
}

// merge all regions' slices to get islands
void Layer::make_slices()
{
    ExPolygons slices;
    if (this->regions.size() == 1) {
        // optimization: if we only have one region, take its slices
        slices = this->regions.front()->slices;
    } else {
        Polygons slices_p;
        FOREACH_LAYERREGION(this, layerm) {
            polygons_append(slices_p, to_polygons((*layerm)->slices));
        }
        slices = union_ex(slices_p);
    }
    
    this->slices.expolygons.clear();
    this->slices.expolygons.reserve(slices.size());
    
    // prepare ordering points
    Points ordering_points;
    ordering_points.reserve(slices.size());
    for (const ExPolygon &ex : slices)
        ordering_points.push_back(ex.contour.first_point());
    
    // sort slices
    std::vector<Points::size_type> order;
    Slic3r::Geometry::chained_path(ordering_points, order);
    
    // populate slices vector
    for (size_t i : order)
        this->slices.expolygons.push_back(STDMOVE(slices[i]));
}

void Layer::merge_slices()
{
    if (this->regions.size() == 1) {
        // Optimization, also more robust. Don't merge classified pieces of layerm->slices,
        // but use the non-split islands of a layer. For a single region print, these shall be equal.
        this->regions.front()->slices.set(this->slices.expolygons, stInternal);
    } else {
        FOREACH_LAYERREGION(this, layerm) {
            // without safety offset, artifacts are generated (GH #2494)
            (*layerm)->slices.set(union_ex(to_polygons(STDMOVE((*layerm)->slices.surfaces)), true), stInternal);
        }
    }
}

// Here the perimeters are created cummulatively for all layer regions sharing the same parameters influencing the perimeters.
// The perimeter paths and the thin fills (ExtrusionEntityCollection) are assigned to the first compatible layer region.
// The resulting fill surface is split back among the originating regions.
void Layer::make_perimeters()
{
    BOOST_LOG_TRIVIAL(trace) << "Generating perimeters for layer " << this->id();
    
    // keep track of regions whose perimeters we have already generated
    std::set<size_t> done;
    
    FOREACH_LAYERREGION(this, layerm) {
        size_t region_id = layerm - this->regions.begin();
        if (done.find(region_id) != done.end()) continue;
        BOOST_LOG_TRIVIAL(trace) << "Generating perimeters for layer " << this->id() << ", region " << region_id;
        done.insert(region_id);
        const PrintRegionConfig &config = (*layerm)->region()->config;
        
        // find compatible regions
        LayerRegionPtrs layerms;
        layerms.push_back(*layerm);
        for (LayerRegionPtrs::const_iterator it = layerm + 1; it != this->regions.end(); ++it) {
            LayerRegion* other_layerm = *it;
            const PrintRegionConfig &other_config = other_layerm->region()->config;
            
            if (config.perimeter_extruder   == other_config.perimeter_extruder
                && config.perimeters        == other_config.perimeters
                && config.perimeter_speed   == other_config.perimeter_speed
                && config.external_perimeter_speed == other_config.external_perimeter_speed
                && config.gap_fill_speed    == other_config.gap_fill_speed
                && config.overhangs         == other_config.overhangs
                && config.serialize("perimeter_extrusion_width").compare(other_config.serialize("perimeter_extrusion_width")) == 0
                && config.thin_walls        == other_config.thin_walls
                && config.external_perimeters_first == other_config.external_perimeters_first) {
                layerms.push_back(other_layerm);
                done.insert(it - this->regions.begin());
            }
        }
        
        if (layerms.size() == 1) {  // optimization
            (*layerm)->fill_surfaces.surfaces.clear();
            (*layerm)->make_perimeters((*layerm)->slices, &(*layerm)->fill_surfaces);
            (*layerm)->fill_expolygons = to_expolygons((*layerm)->fill_surfaces.surfaces);
        } else {
            SurfaceCollection new_slices;
            {
                // group slices (surfaces) according to number of extra perimeters
                std::map<unsigned short,Surfaces> slices;  // extra_perimeters => [ surface, surface... ]
                for (LayerRegionPtrs::iterator l = layerms.begin(); l != layerms.end(); ++l) {
                    for (Surfaces::iterator s = (*l)->slices.surfaces.begin(); s != (*l)->slices.surfaces.end(); ++s) {
                        slices[s->extra_perimeters].push_back(*s);
                    }
                }
                // merge the surfaces assigned to each group
                for (std::map<unsigned short,Surfaces>::const_iterator it = slices.begin(); it != slices.end(); ++it)
                    new_slices.append(union_ex(it->second, true), it->second.front());
            }
            
            // make perimeters
            SurfaceCollection fill_surfaces;
            (*layerm)->make_perimeters(new_slices, &fill_surfaces);

            // assign fill_surfaces to each layer
            if (!fill_surfaces.surfaces.empty()) { 
                for (LayerRegionPtrs::iterator l = layerms.begin(); l != layerms.end(); ++l) {
                    // Separate the fill surfaces.
                    ExPolygons expp = intersection_ex(to_polygons(fill_surfaces), (*l)->slices);
                    (*l)->fill_expolygons = expp;
                    (*l)->fill_surfaces.set(STDMOVE(expp), fill_surfaces.surfaces.front());
                }
            }
        }
    }
    BOOST_LOG_TRIVIAL(trace) << "Generating perimeters for layer " << this->id() << " - Done";
}

void Layer::make_fills()
{
    #ifdef SLIC3R_DEBUG
    printf("Making fills for layer " PRINTF_ZU "\n", this->id());
    #endif
    for (LayerRegionPtrs::iterator it_layerm = regions.begin(); it_layerm != regions.end(); ++ it_layerm) {
        LayerRegion &layerm = *(*it_layerm);
        layerm.fills.clear();
        make_fill(layerm, layerm.fills);
#ifndef NDEBUG
        for (size_t i = 0; i < layerm.fills.entities.size(); ++ i)
            assert(dynamic_cast<ExtrusionEntityCollection*>(layerm.fills.entities[i]) != NULL);
#endif
    }
}

void Layer::export_region_slices_to_svg(const char *path) const
{
    BoundingBox bbox;
    for (LayerRegionPtrs::const_iterator region = this->regions.begin(); region != this->regions.end(); ++region)
        for (Surfaces::const_iterator surface = (*region)->slices.surfaces.begin(); surface != (*region)->slices.surfaces.end(); ++surface)
            bbox.merge(get_extents(surface->expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min.x, bbox.max.y);
    bbox.merge(Point(std::max(bbox.min.x + legend_size.x, bbox.max.x), bbox.max.y + legend_size.y));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (LayerRegionPtrs::const_iterator region = this->regions.begin(); region != this->regions.end(); ++region)
        for (Surfaces::const_iterator surface = (*region)->slices.surfaces.begin(); surface != (*region)->slices.surfaces.end(); ++surface)
            svg.draw(surface->expolygon, surface_type_to_color_name(surface->surface_type), transparency);
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close(); 
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void Layer::export_region_slices_to_svg_debug(const char *name) const
{
    static size_t idx = 0;
    this->export_region_slices_to_svg(debug_out_path("Layer-slices-%s-%d.svg", name, idx ++).c_str());
}

void Layer::export_region_fill_surfaces_to_svg(const char *path) const
{
    BoundingBox bbox;
    for (LayerRegionPtrs::const_iterator region = this->regions.begin(); region != this->regions.end(); ++region)
        for (Surfaces::const_iterator surface = (*region)->fill_surfaces.surfaces.begin(); surface != (*region)->fill_surfaces.surfaces.end(); ++surface)
            bbox.merge(get_extents(surface->expolygon));
    Point legend_size = export_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min.x, bbox.max.y);
    bbox.merge(Point(std::max(bbox.min.x + legend_size.x, bbox.max.x), bbox.max.y + legend_size.y));

    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (LayerRegionPtrs::const_iterator region = this->regions.begin(); region != this->regions.end(); ++region)
        for (Surfaces::const_iterator surface = (*region)->fill_surfaces.surfaces.begin(); surface != (*region)->fill_surfaces.surfaces.end(); ++surface)
            svg.draw(surface->expolygon, surface_type_to_color_name(surface->surface_type), transparency);
    export_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

// Export to "out/LayerRegion-name-%d.svg" with an increasing index with every export.
void Layer::export_region_fill_surfaces_to_svg_debug(const char *name) const
{
    static size_t idx = 0;
    this->export_region_fill_surfaces_to_svg(debug_out_path("Layer-fill_surfaces-%s-%d.svg", name, idx ++).c_str());
}

}
