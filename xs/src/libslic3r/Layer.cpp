#include "Layer.hpp"
#include "ClipperUtils.hpp"
#include "Geometry.hpp"
#include "Print.hpp"


namespace Slic3r {

Layer::Layer(size_t id, PrintObject *object, coordf_t height, coordf_t print_z,
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
            Polygons region_slices_p = (*layerm)->slices;
            slices_p.insert(slices_p.end(), region_slices_p.begin(), region_slices_p.end());
        }
        union_(slices_p, &slices);
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
    for (std::vector<Points::size_type>::const_iterator it = order.begin(); it != order.end(); ++it) {
        this->slices.expolygons.push_back(slices[*it]);
    }
}

void
Layer::merge_slices()
{
    FOREACH_LAYERREGION(this, layerm) {
        (*layerm)->merge_slices();
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

#ifdef SLIC3RXS
REGISTER_CLASS(Layer, "Layer");
#endif


SupportLayer::SupportLayer(size_t id, PrintObject *object, coordf_t height,
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
