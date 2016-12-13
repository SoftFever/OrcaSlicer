#include "Layer.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "Print.hpp"
#include "Fill/Fill.hpp"
#include "SVG.hpp"

namespace Slic3r {

Layer::Layer(size_t id, PrintObject *object, coordf_t height, coordf_t print_z,
        coordf_t slice_z)
:   upper_layer(NULL),
    lower_layer(NULL),
    regions(),
    slicing_errors(false),
    slice_z(slice_z),
    print_z(print_z),
    height(height),
    slices(),
    _id(id),
    _object(object)
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

size_t
Layer::id() const
{
    return this->_id;
}

void
Layer::set_id(size_t id)
{
    this->_id = id;
}

PrintObject*
Layer::object()
{
    return this->_object;
}

const PrintObject*
Layer::object() const
{
    return this->_object;
}


size_t
Layer::region_count() const
{
    return this->regions.size();
}

void
Layer::clear_regions()
{
    for (size_t i = 0; i < this->regions.size(); ++ i)
        delete this->regions[i];
    this->regions.clear();
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

// merge all regions' slices to get islands
void
Layer::make_slices()
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
    for (ExPolygons::const_iterator ex = slices.begin(); ex != slices.end(); ++ex)
        ordering_points.push_back(ex->contour.first_point());
    
    // sort slices
    std::vector<Points::size_type> order;
    Slic3r::Geometry::chained_path(ordering_points, order);
    
    // populate slices vector
    for (std::vector<Points::size_type>::const_iterator it = order.begin(); it != order.end(); ++it)
        this->slices.expolygons.push_back(STDMOVE(slices[*it]));
}

void
Layer::merge_slices()
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

template <class T>
bool
Layer::any_internal_region_slice_contains(const T &item) const
{
    FOREACH_LAYERREGION(this, layerm) {
        if ((*layerm)->slices.any_internal_contains(item)) return true;
    }
    return false;
}
template bool Layer::any_internal_region_slice_contains<Polyline>(const Polyline &item) const;

template <class T>
bool
Layer::any_bottom_region_slice_contains(const T &item) const
{
    FOREACH_LAYERREGION(this, layerm) {
        if ((*layerm)->slices.any_bottom_contains(item)) return true;
    }
    return false;
}
template bool Layer::any_bottom_region_slice_contains<Polyline>(const Polyline &item) const;


// Here the perimeters are created cummulatively for all layer regions sharing the same parameters influencing the perimeters.
// The perimeter paths and the thin fills (ExtrusionEntityCollection) are assigned to the first compatible layer region.
// The resulting fill surface is split back among the originating regions.
void
Layer::make_perimeters()
{
    #ifdef SLIC3R_DEBUG
    printf("Making perimeters for layer " PRINTF_ZU "\n", this->id());
    #endif
    
    // keep track of regions whose perimeters we have already generated
    std::set<size_t> done;
    
    FOREACH_LAYERREGION(this, layerm) {
        size_t region_id = layerm - this->regions.begin();
        if (done.find(region_id) != done.end()) continue;
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

void Layer::export_region_slices_to_svg(const char *path)
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
void Layer::export_region_slices_to_svg_debug(const char *name)
{
    static size_t idx = 0;
    this->export_region_slices_to_svg(debug_out_path("Layer-slices-%s-%d.svg", name, idx ++).c_str());
}

void Layer::export_region_fill_surfaces_to_svg(const char *path)
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
void Layer::export_region_fill_surfaces_to_svg_debug(const char *name)
{
    static size_t idx = 0;
    this->export_region_fill_surfaces_to_svg(debug_out_path("Layer-fill_surfaces-%s-%d.svg", name, idx ++).c_str());
}

SupportLayer::SupportLayer(size_t id, PrintObject *object, coordf_t height,
        coordf_t print_z, coordf_t slice_z)
:   Layer(id, object, height, print_z, slice_z)
{
}

SupportLayer::~SupportLayer()
{
}


}
