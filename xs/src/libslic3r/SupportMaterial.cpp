#include "ClipperUtils.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "PerimeterGenerator.hpp"
#include "Layer.hpp"
#include "Print.hpp"
#include "SupportMaterial.hpp"
#include "Fill/FillBase.hpp"
#include "EdgeGrid.hpp"
#include "Geometry.hpp"

#include <cmath>
#include <memory>
#include <boost/log/trivial.hpp>

#include <tbb/parallel_for.h>
#include <tbb/atomic.h>
#include <tbb/spin_mutex.h>
#include <tbb/task_group.h>

// #define SLIC3R_DEBUG

// Make assert active if SLIC3R_DEBUG
#ifdef SLIC3R_DEBUG
    #define DEBUG
    #define _DEBUG
    #undef NDEBUG
    #include "SVG.hpp"
#endif

// #undef NDEBUG
#include <cassert>

namespace Slic3r {

// Increment used to reach MARGIN in steps to avoid trespassing thin objects
#define NUM_MARGIN_STEPS 3

// Dimensions of a tree-like structure to save material
#define PILLAR_SIZE (2.5)
#define PILLAR_SPACING 10

//#define SUPPORT_SURFACES_OFFSET_PARAMETERS ClipperLib::jtMiter, 3.
//#define SUPPORT_SURFACES_OFFSET_PARAMETERS ClipperLib::jtMiter, 1.5
#define SUPPORT_SURFACES_OFFSET_PARAMETERS ClipperLib::jtSquare, 0.

#ifdef SLIC3R_DEBUG
const char* support_surface_type_to_color_name(const PrintObjectSupportMaterial::SupporLayerType surface_type)
{
    switch (surface_type) {
        case PrintObjectSupportMaterial::sltTopContact:     return "rgb(255,0,0)"; // "red";
        case PrintObjectSupportMaterial::sltTopInterface:   return "rgb(0,255,0)"; // "green";
        case PrintObjectSupportMaterial::sltBase:           return "rgb(0,0,255)"; // "blue";
        case PrintObjectSupportMaterial::sltBottomInterface:return "rgb(255,255,128)"; // yellow 
        case PrintObjectSupportMaterial::sltBottomContact:  return "rgb(255,0,255)"; // magenta
        case PrintObjectSupportMaterial::sltRaftInterface:  return "rgb(0,255,255)";
        case PrintObjectSupportMaterial::sltRaftBase:       return "rgb(128,128,128)";
        case PrintObjectSupportMaterial::sltUnknown:        return "rgb(128,0,0)"; // maroon
        default:                                            return "rgb(64,64,64)";
    };
}

Point export_support_surface_type_legend_to_svg_box_size()
{
    return Point(scale_(1.+10.*8.), scale_(3.)); 
}

void export_support_surface_type_legend_to_svg(SVG &svg, const Point &pos)
{
    // 1st row
    coord_t pos_x0 = pos.x + scale_(1.);
    coord_t pos_x = pos_x0;
    coord_t pos_y = pos.y + scale_(1.5);
    coord_t step_x = scale_(10.);
    svg.draw_legend(Point(pos_x, pos_y), "top contact"    , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltTopContact));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "top iface"      , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltTopInterface));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "base"           , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltBase));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "bottom iface"   , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltBottomInterface));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "bottom contact" , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltBottomContact));
    // 2nd row
    pos_x = pos_x0;
    pos_y = pos.y+scale_(2.8);
    svg.draw_legend(Point(pos_x, pos_y), "raft interface" , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltRaftInterface));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "raft base"      , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltRaftBase));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "unknown"        , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltUnknown));
    pos_x += step_x;
    svg.draw_legend(Point(pos_x, pos_y), "intermediate"   , support_surface_type_to_color_name(PrintObjectSupportMaterial::sltIntermediate));
}

void export_print_z_polygons_to_svg(const char *path, PrintObjectSupportMaterial::MyLayer ** const layers, size_t n_layers)
{
    BoundingBox bbox;
    for (int i = 0; i < n_layers; ++ i)
        bbox.merge(get_extents(layers[i]->polygons));
    Point legend_size = export_support_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min.x, bbox.max.y);
    bbox.merge(Point(std::max(bbox.min.x + legend_size.x, bbox.max.x), bbox.max.y + legend_size.y));
    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (int i = 0; i < n_layers; ++ i)
        svg.draw(union_ex(layers[i]->polygons), support_surface_type_to_color_name(layers[i]->layer_type), transparency);
    for (int i = 0; i < n_layers; ++ i)
        svg.draw(to_polylines(layers[i]->polygons), support_surface_type_to_color_name(layers[i]->layer_type));
    export_support_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}

void export_print_z_polygons_and_extrusions_to_svg(
    const char                                      *path, 
    PrintObjectSupportMaterial::MyLayer ** const     layers, 
    size_t                                           n_layers,
    SupportLayer                                    &support_layer)
{
    BoundingBox bbox;
    for (int i = 0; i < n_layers; ++ i)
        bbox.merge(get_extents(layers[i]->polygons));
    Point legend_size = export_support_surface_type_legend_to_svg_box_size();
    Point legend_pos(bbox.min.x, bbox.max.y);
    bbox.merge(Point(std::max(bbox.min.x + legend_size.x, bbox.max.x), bbox.max.y + legend_size.y));
    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (int i = 0; i < n_layers; ++ i)
        svg.draw(union_ex(layers[i]->polygons), support_surface_type_to_color_name(layers[i]->layer_type), transparency);
    for (int i = 0; i < n_layers; ++ i)
        svg.draw(to_polylines(layers[i]->polygons), support_surface_type_to_color_name(layers[i]->layer_type));

    Polygons polygons_support, polygons_interface;
    support_layer.support_fills.polygons_covered_by_width(polygons_support, SCALED_EPSILON);
//    support_layer.support_interface_fills.polygons_covered_by_width(polygons_interface, SCALED_EPSILON);
    svg.draw(union_ex(polygons_support), "brown");
    svg.draw(union_ex(polygons_interface), "black");

    export_support_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}
#endif /* SLIC3R_DEBUG */

PrintObjectSupportMaterial::PrintObjectSupportMaterial(const PrintObject *object, const SlicingParameters &slicing_params) :
    m_object                (object),
    m_print_config          (&object->print()->config),
    m_object_config         (&object->config),
    m_slicing_params        (slicing_params),
    m_first_layer_flow      (support_material_1st_layer_flow(object, float(slicing_params.first_print_layer_height))),
    m_support_material_flow (support_material_flow(object, float(slicing_params.layer_height))),
    m_support_material_interface_flow(support_material_interface_flow(object, float(slicing_params.layer_height))), 
    m_support_layer_height_min(0.01)
{
    // Calculate a minimum support layer height as a minimum over all extruders, but not smaller than 10um.
    m_support_layer_height_min = 1000000.;
    for (auto lh : m_print_config->min_layer_height.values)
        m_support_layer_height_min = std::min(m_support_layer_height_min, std::max(0.01, lh));

    if (m_object_config->support_material_interface_layers.value == 0) {
        // No interface layers allowed, print everything with the base support pattern.
        m_support_material_interface_flow = m_support_material_flow;
    }

    // Evaluate the XY gap between the object outer perimeters and the support structures.
    coordf_t external_perimeter_width = 0.;
    for (size_t region_id = 0; region_id < object->region_volumes.size(); ++ region_id) {
        if (! object->region_volumes[region_id].empty()) {
            const PrintRegionConfig &config = object->print()->get_region(region_id)->config;
            coordf_t width = config.external_perimeter_extrusion_width.get_abs_value(slicing_params.layer_height);
            if (width <= 0.)
                width = m_print_config->nozzle_diameter.get_at(config.perimeter_extruder-1);
            external_perimeter_width = std::max(external_perimeter_width, width);
        }
    }
    m_gap_xy = m_object_config->support_material_xy_spacing.get_abs_value(external_perimeter_width);

    m_can_merge_support_regions = m_object_config->support_material_extruder.value == m_object_config->support_material_interface_extruder.value;
    if (! m_can_merge_support_regions && (m_object_config->support_material_extruder.value == 0 || m_object_config->support_material_interface_extruder.value == 0)) {
        // One of the support extruders is of "don't care" type.
        auto object_extruders = m_object->print()->object_extruders();
        if (object_extruders.size() == 1 &&
            *object_extruders.begin() == std::max<unsigned int>(m_object_config->support_material_extruder.value, m_object_config->support_material_interface_extruder.value))
            // Object is printed with the same extruder as the support.
            m_can_merge_support_regions = true;
    }
}

// Using the std::deque as an allocator.
inline PrintObjectSupportMaterial::MyLayer& layer_allocate(
    std::deque<PrintObjectSupportMaterial::MyLayer> &layer_storage, 
    PrintObjectSupportMaterial::SupporLayerType      layer_type)
{ 
    layer_storage.push_back(PrintObjectSupportMaterial::MyLayer());
    layer_storage.back().layer_type = layer_type;
    return layer_storage.back();
}

inline PrintObjectSupportMaterial::MyLayer& layer_allocate(
    std::deque<PrintObjectSupportMaterial::MyLayer> &layer_storage,
    tbb::spin_mutex                                 &layer_storage_mutex,
    PrintObjectSupportMaterial::SupporLayerType      layer_type)
{ 
    layer_storage_mutex.lock();
    layer_storage.push_back(PrintObjectSupportMaterial::MyLayer());
    PrintObjectSupportMaterial::MyLayer *layer_new = &layer_storage.back();
    layer_storage_mutex.unlock();
    layer_new->layer_type = layer_type;
    return *layer_new;
}

inline void layers_append(PrintObjectSupportMaterial::MyLayersPtr &dst, const PrintObjectSupportMaterial::MyLayersPtr &src)
{
    dst.insert(dst.end(), src.begin(), src.end());
}

// Compare layers lexicographically.
struct MyLayersPtrCompare
{
    bool operator()(const PrintObjectSupportMaterial::MyLayer* layer1, const PrintObjectSupportMaterial::MyLayer* layer2) const {
        return *layer1 < *layer2;
    }
};

void PrintObjectSupportMaterial::generate(PrintObject &object)
{
    BOOST_LOG_TRIVIAL(info) << "Support generator - Start";

    coordf_t max_object_layer_height = 0.;
    for (size_t i = 0; i < object.layer_count(); ++ i)
        max_object_layer_height = std::max(max_object_layer_height, object.layers[i]->height);

    // Layer instances will be allocated by std::deque and they will be kept until the end of this function call.
    // The layers will be referenced by various LayersPtr (of type std::vector<Layer*>)
    MyLayerStorage layer_storage;

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating top contacts";

    // Determine the top contact surfaces of the support, defined as:
    // contact = overhangs - clearance + margin
    // This method is responsible for identifying what contact surfaces
    // should the support material expose to the object in order to guarantee
    // that it will be effective, regardless of how it's built below.
    // If raft is to be generated, the 1st top_contact layer will contain the 1st object layer silhouette without holes.
    MyLayersPtr top_contacts = this->top_contact_layers(object, layer_storage);
    if (top_contacts.empty())
        // Nothing is supported, no supports are generated.
        return;

#ifdef SLIC3R_DEBUG
    static int iRun = 0;
    iRun ++;
    for (MyLayersPtr::const_iterator it = top_contacts.begin(); it != top_contacts.end(); ++ it)
        Slic3r::SVG::export_expolygons(
            debug_out_path("support-top-contacts-%d-%lf.svg", iRun, (*it)->print_z), 
            union_ex((*it)->polygons, false));
#endif /* SLIC3R_DEBUG */

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating bottom contacts";

    // Determine the bottom contact surfaces of the supports over the top surfaces of the object.
    // Depending on whether the support is soluble or not, the contact layer thickness is decided.
    // layer_support_areas contains the per object layer support areas. These per object layer support areas
    // may get merged and trimmed by this->generate_base_layers() if the support layers are not synchronized with object layers.
    std::vector<Polygons> layer_support_areas;
    MyLayersPtr bottom_contacts = this->bottom_contact_layers_and_layer_support_areas(
        object, top_contacts, layer_storage,
        layer_support_areas);

#ifdef SLIC3R_DEBUG
    for (size_t layer_id = 0; layer_id < object.layers.size(); ++ layer_id)
        Slic3r::SVG::export_expolygons(
            debug_out_path("support-areas-%d-%lf.svg", iRun, object.layers[layer_id]->print_z), 
            union_ex(layer_support_areas[layer_id], false));
#endif /* SLIC3R_DEBUG */

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating intermediate layers - indices";

    // Allocate empty layers between the top / bottom support contact layers
    // as placeholders for the base and intermediate support layers.
    // The layers may or may not be synchronized with the object layers, depending on the configuration.
    // For example, a single nozzle multi material printing will need to generate a waste tower, which in turn
    // wastes less material, if there are as little tool changes as possible.
    MyLayersPtr intermediate_layers = this->raft_and_intermediate_support_layers(
        object, bottom_contacts, top_contacts, layer_storage);

    this->trim_support_layers_by_object(object, top_contacts, m_slicing_params.soluble_interface ? 0. : m_support_layer_height_min, 0., m_gap_xy);

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating base layers";

    // Fill in intermediate layers between the top / bottom support contact layers, trimm them by the object.
    this->generate_base_layers(object, bottom_contacts, top_contacts, intermediate_layers, layer_support_areas);

#ifdef SLIC3R_DEBUG
    for (MyLayersPtr::const_iterator it = intermediate_layers.begin(); it != intermediate_layers.end(); ++ it)
        Slic3r::SVG::export_expolygons(
            debug_out_path("support-base-layers-%d-%lf.svg", iRun, (*it)->print_z), 
            union_ex((*it)->polygons, false));
#endif /* SLIC3R_DEBUG */

    BOOST_LOG_TRIVIAL(info) << "Support generator - Trimming top contacts by bottom contacts";

    // Because the top and bottom contacts are thick slabs, they may overlap causing over extrusion 
    // and unwanted strong bonds to the object.
    // Rather trim the top contacts by their overlapping bottom contacts to leave a gap instead of over extruding
    // top contacts over the bottom contacts.
    this->trim_top_contacts_by_bottom_contacts(object, bottom_contacts, top_contacts);


    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating interfaces";

    // Propagate top / bottom contact layers to generate interface layers.
    MyLayersPtr interface_layers = this->generate_interface_layers(
        bottom_contacts, top_contacts, intermediate_layers, layer_storage);

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating raft";

    // If raft is to be generated, the 1st top_contact layer will contain the 1st object layer silhouette with holes filled.
    // There is also a 1st intermediate layer containing bases of support columns.
    // Inflate the bases of the support columns and create the raft base under the object.
    MyLayersPtr raft_layers = this->generate_raft_base(top_contacts, interface_layers, intermediate_layers, layer_storage);

#ifdef SLIC3R_DEBUG
    for (MyLayersPtr::const_iterator it = interface_layers.begin(); it != interface_layers.end(); ++ it)
        Slic3r::SVG::export_expolygons(
            debug_out_path("support-interface-layers-%d-%lf.svg", iRun, (*it)->print_z), 
            union_ex((*it)->polygons, false));
#endif /* SLIC3R_DEBUG */

/*
    // Clip with the pillars.
    if (! shape.empty()) {
        this->clip_with_shape(interface, shape);
        this->clip_with_shape(base, shape);
    }
*/

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating layers";

// For debugging purposes, one may want to show only some of the support extrusions.
//    raft_layers.clear();
//    bottom_contacts.clear();
//    top_contacts.clear();
//    intermediate_layers.clear();
//    interface_layers.clear();

    // Install support layers into the object.
    // A support layer installed on a PrintObject has a unique print_z.
    MyLayersPtr layers_sorted;
    layers_sorted.reserve(raft_layers.size() + bottom_contacts.size() + top_contacts.size() + intermediate_layers.size() + interface_layers.size());
    layers_append(layers_sorted, raft_layers);
    layers_append(layers_sorted, bottom_contacts);
    layers_append(layers_sorted, top_contacts);
    layers_append(layers_sorted, intermediate_layers);
    layers_append(layers_sorted, interface_layers);
    // Sort the layers lexicographically by a raising print_z and a decreasing height.
    std::sort(layers_sorted.begin(), layers_sorted.end(), MyLayersPtrCompare());
    int layer_id = 0;
    assert(object.support_layers.empty());
    for (int i = 0; i < int(layers_sorted.size());) {
        // Find the last layer with roughly the same print_z, find the minimum layer height of all.
        // Due to the floating point inaccuracies, the print_z may not be the same even if in theory they should.
        int j = i + 1;
        coordf_t zmax = layers_sorted[i]->print_z + EPSILON;
        for (; j < layers_sorted.size() && layers_sorted[j]->print_z <= zmax; ++j) ;
        // Assign an average print_z to the set of layers with nearly equal print_z.
        coordf_t zavg = 0.5 * (layers_sorted[i]->print_z + layers_sorted[j - 1]->print_z);
        coordf_t height_min = layers_sorted[i]->height;
        bool     empty = true;
        for (int u = i; u < j; ++u) {
            MyLayer &layer = *layers_sorted[u];
            if (! layer.polygons.empty())
                empty = false;
            layer.print_z = zavg;
            height_min = std::min(height_min, layer.height);
        }
        if (! empty) {
            // Here the upper_layer and lower_layer pointers are left to null at the support layers, 
            // as they are never used. These pointers are candidates for removal.
            object.add_support_layer(layer_id ++, height_min, zavg);
        }
        i = j;
    }

    BOOST_LOG_TRIVIAL(info) << "Support generator - Generating tool paths";

    // Generate the actual toolpaths and save them into each layer.
    this->generate_toolpaths(object, raft_layers, bottom_contacts, top_contacts, intermediate_layers, interface_layers);

#ifdef SLIC3R_DEBUG
    {
        size_t layer_id = 0;
        for (int i = 0; i < int(layers_sorted.size());) {
            // Find the last layer with roughly the same print_z, find the minimum layer height of all.
            // Due to the floating point inaccuracies, the print_z may not be the same even if in theory they should.
            int j = i + 1;
            coordf_t zmax = layers_sorted[i]->print_z + EPSILON;
            bool empty = true;
            for (; j < layers_sorted.size() && layers_sorted[j]->print_z <= zmax; ++j)
                if (! layers_sorted[j]->polygons.empty())
                    empty = false;
            if (! empty) {
                export_print_z_polygons_to_svg(
                    debug_out_path("support-%d-%lf.svg", iRun, layers_sorted[i]->print_z).c_str(),
                    layers_sorted.data() + i, j - i);
                export_print_z_polygons_and_extrusions_to_svg(
                    debug_out_path("support-w-fills-%d-%lf.svg", iRun, layers_sorted[i]->print_z).c_str(),
                    layers_sorted.data() + i, j - i,
                    *object.support_layers[layer_id]);
                ++layer_id;
            }
            i = j;
        }
    }
#endif /* SLIC3R_DEBUG */

    BOOST_LOG_TRIVIAL(info) << "Support generator - End";
}

// Collect all polygons of all regions in a layer with a given surface type.
Polygons collect_region_slices_by_type(const Layer &layer, SurfaceType surface_type)
{
    // 1) Count the new polygons first.
    size_t n_polygons_new = 0;
    for (LayerRegionPtrs::const_iterator it_region = layer.regions.begin(); it_region != layer.regions.end(); ++ it_region) {
        const LayerRegion       &region = *(*it_region);
        const SurfaceCollection &slices = region.slices;
        for (Surfaces::const_iterator it = slices.surfaces.begin(); it != slices.surfaces.end(); ++ it) {
            const Surface &surface = *it;
            if (surface.surface_type == surface_type)
                n_polygons_new += surface.expolygon.holes.size() + 1;
        }
    }

    // 2) Collect the new polygons.
    Polygons out;
    out.reserve(n_polygons_new);
    for (LayerRegionPtrs::const_iterator it_region = layer.regions.begin(); it_region != layer.regions.end(); ++ it_region) {
        const LayerRegion       &region = *(*it_region);
        const SurfaceCollection &slices = region.slices;
        for (Surfaces::const_iterator it = slices.surfaces.begin(); it != slices.surfaces.end(); ++ it) {
            const Surface &surface = *it;
            if (surface.surface_type == surface_type)
                polygons_append(out, surface.expolygon);
        }
    }

    return out;
}

// Collect outer contours of all slices of this layer.
// This is useful for calculating the support base with holes filled.
Polygons collect_slices_outer(const Layer &layer)
{
    Polygons out;
    out.reserve(out.size() + layer.slices.expolygons.size());
    for (ExPolygons::const_iterator it = layer.slices.expolygons.begin(); it != layer.slices.expolygons.end(); ++ it)
        out.push_back(it->contour);
    return out;
}

class SupportGridPattern
{
public:
    SupportGridPattern(
        const Polygons &support_polygons, 
        const Polygons &trimming_polygons, 
        coordf_t        support_spacing, 
        coordf_t        support_angle) :
        m_support_polygons(&support_polygons), m_trimming_polygons(&trimming_polygons),
        m_support_spacing(support_spacing), m_support_angle(support_angle)
    {
        if (m_support_angle != 0.) {
            // Create a copy of the rotated contours.
            m_support_polygons_rotated  = support_polygons;
            m_trimming_polygons_rotated = trimming_polygons;
            m_support_polygons  = &m_support_polygons_rotated;
            m_trimming_polygons = &m_trimming_polygons_rotated;
            polygons_rotate(m_support_polygons_rotated, - support_angle);
            polygons_rotate(m_trimming_polygons_rotated, - support_angle);
        }
        // Create an EdgeGrid, initialize it with projection, initialize signed distance field.
        coord_t grid_resolution = coord_t(scale_(m_support_spacing));
        BoundingBox bbox = get_extents(*m_support_polygons);
        bbox.offset(20);
        bbox.align_to_grid(grid_resolution);
        m_grid.set_bbox(bbox);
        m_grid.create(*m_support_polygons, grid_resolution);
        m_grid.calculate_sdf();
        // Extract a bounding contour from the grid, trim by the object.
        m_island_samples = island_samples(*m_support_polygons);
    }

    // Extract polygons from the grid, offsetted by offset_in_grid,
    // and trim the extracted polygons by trimming_polygons.
    // Trimming by the trimming_polygons may split the extracted polygons into pieces.
    // Remove all the pieces, which do not contain any of the island_samples.
    Polygons extract_support(const coord_t offset_in_grid)
    {
        // Generate islands, so each island may be tested for overlap with m_island_samples.
        ExPolygons islands = diff_ex(
            m_grid.contours_simplified(offset_in_grid),
            *m_trimming_polygons, false);

        // Extract polygons, which contain some of the m_island_samples.
        Polygons out;
        std::vector<std::pair<Point,bool>> samples_inside;

        for (ExPolygon &island : islands) {
            BoundingBox bbox = get_extents(island.contour);
            auto it_lower = std::lower_bound(m_island_samples.begin(), m_island_samples.end(), bbox.min - Point(1, 1));
            auto it_upper = std::upper_bound(m_island_samples.begin(), m_island_samples.end(), bbox.max + Point(1, 1));
            samples_inside.clear();
            for (auto it = it_lower; it != it_upper; ++ it)
                if (bbox.contains(*it))
                    samples_inside.push_back(std::make_pair(*it, false));
            if (! samples_inside.empty()) {
                // For all samples_inside count the boundary crossing.
                for (size_t i_contour = 0; i_contour <= island.holes.size(); ++ i_contour) {
                    Polygon &contour = (i_contour == 0) ? island.contour : island.holes[i_contour - 1];
                    Points::const_iterator i = contour.points.begin();
                    Points::const_iterator j = contour.points.end() - 1;
                    for (; i != contour.points.end(); j = i ++) {
                        //FIXME this test is not numerically robust. Particularly, it does not handle horizontal segments at y == point.y well.
                        // Does the ray with y == point.y intersect this line segment?
                        for (auto &sample_inside : samples_inside) {
                            if ((i->y > sample_inside.first.y) != (j->y > sample_inside.first.y)) {
                                double x1 = (double)sample_inside.first.x;
                                double x2 = (double)i->x + (double)(j->x - i->x) * (double)(sample_inside.first.y - i->y) / (double)(j->y - i->y);
                                if (x1 < x2)
                                    sample_inside.second = !sample_inside.second;
                            }
                        }
                    }
                }
                // If any of the sample is inside this island, add this island to the output.
                for (auto &sample_inside : samples_inside)
                    if (sample_inside.second) {
                        polygons_append(out, std::move(island));
                        island.clear();
                        break;
                    }
            }
        }

    #ifdef SLIC3R_DEBUG
        static int iRun = 0;
        ++iRun;
        BoundingBox bbox = get_extents(*m_trimming_polygons);
        if (! islands.empty())
            bbox.merge(get_extents(islands));
        if (!out.empty())
            bbox.merge(get_extents(out));
        SVG svg(debug_out_path("extract_support_from_grid_trimmed-%d.svg", iRun).c_str(), bbox);
        svg.draw(islands, "red", 0.5f);
        svg.draw(union_ex(out), "green", 0.5f);
        svg.draw(union_ex(*m_support_polygons), "blue", 0.5f);
        svg.draw_outline(islands, "red", "red", scale_(0.05));
        svg.draw_outline(union_ex(out), "green", "green", scale_(0.05));
        svg.draw_outline(union_ex(*m_support_polygons), "blue", "blue", scale_(0.05));
        for (const Point &pt : m_island_samples)
            svg.draw(pt, "black", coord_t(scale_(0.15)));
        svg.Close();
    #endif /* SLIC3R_DEBUG */

        if (m_support_angle != 0.)
            polygons_rotate(out, m_support_angle);
        return out;
    }

private:
    SupportGridPattern& operator=(const SupportGridPattern &rhs);

    // Get some internal point of an expolygon, to be used as a representative
    // sample to test, whether this island is inside another island.
    static Point island_sample(const ExPolygon &expoly)
    {
        // Find the lowest point lexicographically.
        const Point *pt_min = &expoly.contour.points.front();
        for (size_t i = 1; i < expoly.contour.points.size(); ++ i)
            if (expoly.contour.points[i] < *pt_min)
                pt_min = &expoly.contour.points[i];

        // Lowest corner will always be convex, in worst case denegenerate with zero angle.
        const Point &p1 = (pt_min == &expoly.contour.points.front()) ? expoly.contour.points.back() : *(pt_min - 1);
        const Point &p2 = *pt_min;
        const Point &p3 = (pt_min == &expoly.contour.points.back()) ? expoly.contour.points.front() : *(pt_min + 1);

        Vector v  = (p3 - p2) + (p1 - p2);
        double l2 = double(v.x)*double(v.x)+double(v.y)*double(v.y);
        if (l2 == 0.)
            return p2;
        double coef = 20. / sqrt(l2);
        return Point(p2.x + coef * v.x, p2.y + coef * v.y);
    }

    static Points island_samples(const ExPolygons &expolygons)
    {
        Points pts;
        pts.reserve(expolygons.size());
        for (const ExPolygon &expoly : expolygons)
            if (expoly.contour.points.size() > 2) {
                #if 0
                    pts.push_back(island_sample(expoly));
                #else 
                    Polygons polygons = offset(expoly, - 20.f);
                    for (const Polygon &poly : polygons)
                        if (! poly.points.empty()) {
                            pts.push_back(poly.points.front());
                            break;
                        }
                #endif
            }
        // Sort the points lexicographically, so a binary search could be used to locate points inside a bounding box.
        std::sort(pts.begin(), pts.end());
        return pts;
    } 

    static Points island_samples(const Polygons &polygons)
    {
        return island_samples(union_ex(polygons));
    }

    const Polygons         *m_support_polygons;
    const Polygons         *m_trimming_polygons;
    Polygons                m_support_polygons_rotated;
    Polygons                m_trimming_polygons_rotated;
    // Angle in radians, by which the whole support is rotated.
    coordf_t                m_support_angle;
    // X spacing of the support lines parallel with the Y axis.
    coordf_t                m_support_spacing;

    Slic3r::EdgeGrid::Grid  m_grid;
    Points                  m_island_samples;
};

// Generate top contact layers supporting overhangs.
// For a soluble interface material synchronize the layer heights with the object, otherwise leave the layer height undefined.
// If supports over bed surface only are requested, don't generate contact layers over an object.
PrintObjectSupportMaterial::MyLayersPtr PrintObjectSupportMaterial::top_contact_layers(
    const PrintObject &object, MyLayerStorage &layer_storage) const
{
#ifdef SLIC3R_DEBUG
    static int iRun = 0;
    ++ iRun; 
#endif /* SLIC3R_DEBUG */

    // Output layers, sorted by top Z.
    MyLayersPtr contact_out;

    // If user specified a custom angle threshold, convert it to radians.
    // Zero means automatic overhang detection.
    const double threshold_rad = (m_object_config->support_material_threshold.value > 0) ? 
        M_PI * double(m_object_config->support_material_threshold.value + 1) / 180. : // +1 makes the threshold inclusive
        0.;

    // Build support on a build plate only? If so, then collect and union all the surfaces below the current layer.
    // Unfortunately this is an inherently serial process.
    const bool            buildplate_only = this->build_plate_only();
    std::vector<Polygons> buildplate_covered;
    if (buildplate_only) {
        BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::top_contact_layers() - collecting regions covering the print bed.";
        buildplate_covered.assign(object.layers.size(), Polygons());
        for (size_t layer_id = 1; layer_id < object.layers.size(); ++ layer_id) {
            const Layer &lower_layer = *object.layers[layer_id-1];
            // Merge the new slices with the preceding slices.
            // Apply the safety offset to the newly added polygons, so they will connect
            // with the polygons collected before,
            // but don't apply the safety offset during the union operation as it would
            // inflate the polygons over and over.
            Polygons &covered = buildplate_covered[layer_id];
            covered = buildplate_covered[layer_id - 1];
            polygons_append(covered, offset(lower_layer.slices.expolygons, scale_(0.01)));
            covered = union_(covered, false); // don't apply the safety offset.
        }
    }

    BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::top_contact_layers() in parallel - start";
    // Determine top contact areas.
    // If generating raft only (no support), only calculate top contact areas for the 0th layer.
    // If having a raft, start with 0th layer, otherwise with 1st layer.
    // Note that layer_id < layer->id when raft_layers > 0 as the layer->id incorporates the raft layers.
    // So layer_id == 0 means first object layer and layer->id == 0 means first print layer if there are no explicit raft layers.
    size_t num_layers = this->has_support() ? object.layer_count() : 1;
    contact_out.assign(num_layers, nullptr);
    tbb::spin_mutex layer_storage_mutex;
    tbb::parallel_for(tbb::blocked_range<size_t>(this->has_raft() ? 0 : 1, num_layers),
        [this, &object, &buildplate_covered, threshold_rad, &layer_storage, &layer_storage_mutex, &contact_out](const tbb::blocked_range<size_t>& range) {
            for (size_t layer_id = range.begin(); layer_id < range.end(); ++ layer_id) 
            {
                const Layer &layer = *object.layers[layer_id];

                // Detect overhangs and contact areas needed to support them.
                // Collect overhangs and contacts of all regions of this layer supported by the layer immediately below.
                Polygons overhang_polygons;
                Polygons contact_polygons;
                Polygons slices_margin_cached;
                float    slices_margin_cached_offset = -1.;
                if (layer_id == 0) {
                    // This is the first object layer, so the object is being printed on a raft and
                    // we're here just to get the object footprint for the raft.
                    // We only consider contours and discard holes to get a more continuous raft.
                    overhang_polygons = collect_slices_outer(layer);
                    // Extend by SUPPORT_MATERIAL_MARGIN, which is 1.5mm
                    contact_polygons = offset(overhang_polygons, scale_(SUPPORT_MATERIAL_MARGIN));
                } else {
                    // Generate overhang / contact_polygons for non-raft layers.
                    const Layer &lower_layer = *object.layers[layer_id-1];
                    for (LayerRegion *layerm : layer.regions) {
                        // Extrusion width accounts for the roundings of the extrudates.
                        // It is the maximum widh of the extrudate.
                        float fw = float(layerm->flow(frExternalPerimeter).scaled_width());
                        float lower_layer_offset = 
                            (layer_id < this->m_object_config->support_material_enforce_layers.value) ? 
                                // Enforce a full possible support, ignore the overhang angle.
                                0.f :
                            (threshold_rad > 0. ? 
                                // Overhang defined by an angle.
                                float(scale_(lower_layer.height / tan(threshold_rad))) :
                                // Overhang defined by half the extrusion width.
                                0.5f * fw);
                        // Overhang polygons for this layer and region.
                        Polygons diff_polygons;
                        Polygons layerm_polygons = to_polygons(layerm->slices);
                        Polygons lower_layer_polygons = to_polygons(lower_layer.slices.expolygons);
                        if (lower_layer_offset == 0.f) {
                            // Support everything.
                            diff_polygons = diff(layerm_polygons, lower_layer_polygons);
                            if (! buildplate_covered.empty()) {
                                // Don't support overhangs above the top surfaces.
                                // This step is done before the contact surface is calculated by growing the overhang region.
                                diff_polygons = diff(diff_polygons, buildplate_covered[layer_id]);
                            }
                        } else {
                            // Get the regions needing a suport, collapse very tiny spots.
                            //FIXME cache the lower layer offset if this layer has multiple regions.
                            diff_polygons = offset2(
                                diff(layerm_polygons,
                                     offset(lower_layer_polygons, lower_layer_offset, SUPPORT_SURFACES_OFFSET_PARAMETERS)), 
                                -0.1f*fw, +0.1f*fw);
                            if (! buildplate_covered.empty()) {
                                // Don't support overhangs above the top surfaces.
                                // This step is done before the contact surface is calculated by growing the overhang region.
                                diff_polygons = diff(diff_polygons, buildplate_covered[layer_id]);
                            }
                            if (diff_polygons.empty())
                                continue;
                            // Offset the support regions back to a full overhang, restrict them to the full overhang.
                            diff_polygons = diff(
                                intersection(offset(diff_polygons, lower_layer_offset, SUPPORT_SURFACES_OFFSET_PARAMETERS), layerm_polygons), 
                                lower_layer_polygons);
                        }
                        if (diff_polygons.empty())
                            continue;

                        #ifdef SLIC3R_DEBUG                        
                        {
                            ::Slic3r::SVG svg(debug_out_path("support-top-contacts-raw-run%d-layer%d-region%d.svg", 
                                iRun, layer_id, 
                                std::find_if(layer.regions.begin(), layer.regions.end(), [layerm](const LayerRegion* other){return other == layerm;}) - layer.regions.begin()), 
                            get_extents(diff_polygons));
                            Slic3r::ExPolygons expolys = union_ex(diff_polygons, false);
                            svg.draw(expolys);
                        }
                        #endif /* SLIC3R_DEBUG */

                        if (this->m_object_config->dont_support_bridges) {
                            // compute the area of bridging perimeters
                            // Note: this is duplicate code from GCode.pm, we need to refactor
                            if (true) {
                                Polygons bridged_perimeters;
                                {
                                    Flow bridge_flow = layerm->flow(frPerimeter, true);
                                    coordf_t nozzle_diameter = m_print_config->nozzle_diameter.get_at(layerm->region()->config.perimeter_extruder-1);
                                    Polygons lower_grown_slices = offset(lower_layer_polygons, 0.5f*float(scale_(nozzle_diameter)), SUPPORT_SURFACES_OFFSET_PARAMETERS);
                                    
                                    // Collect perimeters of this layer.
                                    // TODO: split_at_first_point() could split a bridge mid-way
                                    Polylines overhang_perimeters;
                                    for (ExtrusionEntity* extrusion_entity : layerm->perimeters.entities) {
                                        const ExtrusionEntityCollection *island = dynamic_cast<ExtrusionEntityCollection*>(extrusion_entity);
                                        assert(island != NULL);
                                        for (size_t i = 0; i < island->entities.size(); ++ i) {
                                            ExtrusionEntity *entity = island->entities[i];
                                            ExtrusionLoop *loop = dynamic_cast<Slic3r::ExtrusionLoop*>(entity);
                                            overhang_perimeters.push_back(loop ? 
                                                loop->as_polyline() :
                                                dynamic_cast<const Slic3r::ExtrusionPath*>(entity)->polyline);
                                        }
                                    }
                                    
                                    // workaround for Clipper bug, see Slic3r::Polygon::clip_as_polyline()
                                    for (Polyline &polyline : overhang_perimeters)
                                        polyline.points[0].x += 1;
                                    // Trim the perimeters of this layer by the lower layer to get the unsupported pieces of perimeters.
                                    overhang_perimeters = diff_pl(overhang_perimeters, lower_grown_slices);
                                    
                                    // only consider straight overhangs
                                    // only consider overhangs having endpoints inside layer's slices
                                    // convert bridging polylines into polygons by inflating them with their thickness
                                    // since we're dealing with bridges, we can't assume width is larger than spacing,
                                    // so we take the largest value and also apply safety offset to be ensure no gaps
                                    // are left in between
                                    float w = float(std::max(bridge_flow.scaled_width(), bridge_flow.scaled_spacing()));
                                    for (Polyline &polyline : overhang_perimeters)
                                        if (polyline.is_straight()) {
                                            // This is a bridge 
                                            polyline.extend_start(fw);
                                            polyline.extend_end(fw);
                                            // Is the straight perimeter segment supported at both sides?
                                            if (layer.slices.contains(polyline.first_point()) && layer.slices.contains(polyline.last_point()))
                                                // Offset a polyline into a thick line.
                                                polygons_append(bridged_perimeters, offset(polyline, 0.5f * w + 10.f));
                                        }
                                    bridged_perimeters = union_(bridged_perimeters);
                                }
                                // remove the entire bridges and only support the unsupported edges
                                Polygons bridges;
                                for (const Surface &surface : layerm->fill_surfaces.surfaces)
                                    if (surface.surface_type == stBottomBridge && surface.bridge_angle != -1)
                                        polygons_append(bridges, surface.expolygon);
                                diff_polygons = diff(diff_polygons, bridges, true);
                                polygons_append(bridges, bridged_perimeters);
                                polygons_append(diff_polygons, 
                                    intersection(
                                        // Offset unsupported edges into polygons.
                                        offset(layerm->unsupported_bridge_edges.polylines, scale_(SUPPORT_MATERIAL_MARGIN), SUPPORT_SURFACES_OFFSET_PARAMETERS),
                                        bridges));
                            } else {
                                // just remove bridged areas
                                diff_polygons = diff(diff_polygons, layerm->bridged, true);
                            }
                        } // if (m_objconfig->dont_support_bridges)

                        if (diff_polygons.empty())
                            continue;

                        #ifdef SLIC3R_DEBUG
                        Slic3r::SVG::export_expolygons(
                            debug_out_path("support-top-contacts-filtered-run%d-layer%d-region%d-z%f.svg", 
                                iRun, layer_id, 
                                std::find_if(layer.regions.begin(), layer.regions.end(), [layerm](const LayerRegion* other){return other == layerm;}) - layer.regions.begin(), 
                                layer.print_z),
                            union_ex(diff_polygons, false));
                        #endif /* SLIC3R_DEBUG */

                        if (this->has_contact_loops())
                            polygons_append(overhang_polygons, diff_polygons);

                        // Let's define the required contact area by using a max gap of half the upper 
                        // extrusion width and extending the area according to the configured margin.
                        // We increment the area in steps because we don't want our support to overflow
                        // on the other side of the object (if it's very thin).
                        {
                            //FIMXE 1) Make the offset configurable, 2) Make the Z span configurable.
                            float slices_margin_offset = std::min(lower_layer_offset, float(scale_(m_gap_xy))); 
                            if (slices_margin_cached_offset != slices_margin_offset) {
                                slices_margin_cached_offset = slices_margin_offset;
                                slices_margin_cached = (slices_margin_offset == 0.f) ? 
                                    to_polygons(lower_layer.slices.expolygons) :
                                    offset(lower_layer.slices.expolygons, slices_margin_offset, SUPPORT_SURFACES_OFFSET_PARAMETERS);
                                if (! buildplate_covered.empty()) {
                                    // Trim the inflated contact surfaces by the top surfaces as well.
                                    polygons_append(slices_margin_cached, buildplate_covered[layer_id]);
                                    slices_margin_cached = union_(slices_margin_cached);
                                }
                            }
                            // Offset the contact polygons outside.
                            for (size_t i = 0; i < NUM_MARGIN_STEPS; ++ i) {
                                diff_polygons = diff(
                                    offset(
                                        diff_polygons,
                                        SUPPORT_MATERIAL_MARGIN / NUM_MARGIN_STEPS,
                                        ClipperLib::jtRound,
                                        // round mitter limit
                                        scale_(0.05)),
                                    slices_margin_cached);
                            }
                        }
                        polygons_append(contact_polygons, diff_polygons);
                    } // for each layer.region
                } // end of Generate overhang/contact_polygons for non-raft layers.
                
                // now apply the contact areas to the layer were they need to be made
                if (! contact_polygons.empty()) {
                    // get the average nozzle diameter used on this layer
                    MyLayer     &new_layer = layer_allocate(layer_storage, layer_storage_mutex, sltTopContact);
                    new_layer.idx_object_layer_above = layer_id;
                    if (m_slicing_params.soluble_interface) {
                        // Align the contact surface height with a layer immediately below the supported layer.
                        new_layer.print_z = layer.print_z - layer.height;
                        if (layer_id == 0) {
                            // This is a raft contact layer sitting directly on the print bed.
                            new_layer.height   = m_slicing_params.contact_raft_layer_height;
                            new_layer.bottom_z = m_slicing_params.raft_interface_top_z;
                        } else {
                            // Interface layer will be synchronized with the object.
                            assert(layer_id > 0);
                            new_layer.height = object.layers[layer_id - 1]->height;
                            new_layer.bottom_z = (layer_id == 1) ? m_slicing_params.object_print_z_min : object.layers[layer_id - 2]->print_z;
                        }
                    } else {
                        // Contact layer will be printed with a normal flow, but
                        // it will support layers printed with a bridging flow.
                        //FIXME Probably printing with the bridge flow? How about the unsupported perimeters? Are they printed with the bridging flow?
                        // In the future we may switch to a normal extrusion flow for the supported bridges.
                        // Get the average nozzle diameter used on this layer.
                        coordf_t nozzle_dmr = 0.;
                        for (const LayerRegion *region : layer.regions)
                            nozzle_dmr += region->region()->nozzle_dmr_avg(*m_print_config);
                        nozzle_dmr /= coordf_t(layer.regions.size());
                        new_layer.print_z  = layer.print_z - nozzle_dmr - m_object_config->support_material_contact_distance;
                        new_layer.bottom_z = new_layer.print_z;
                        new_layer.height   = 0.;
                        if (layer_id == 0) {
                            // This is a raft contact layer sitting directly on the print bed.
                            assert(this->has_raft());
                            new_layer.bottom_z = m_slicing_params.raft_interface_top_z; 
                            new_layer.height   = m_slicing_params.contact_raft_layer_height;
                        } else {
                            // Ignore this contact area if it's too low.
                            // Don't want to print a layer below the first layer height as it may not stick well.
                            //FIXME there may be a need for a single layer support, then one may decide to print it either as a bottom contact or a top contact
                            // and it may actually make sense to do it with a thinner layer than the first layer height.
                            if (new_layer.print_z < m_slicing_params.first_print_layer_height - EPSILON) {
                                // This contact layer is below the first layer height, therefore not printable. Don't support this surface.
                                continue;
                            } else if (new_layer.print_z < m_slicing_params.first_print_layer_height + EPSILON) {
                                // Align the layer with the 1st layer height.
                                new_layer.print_z  = m_slicing_params.first_print_layer_height;
                                new_layer.bottom_z = 0;
                                new_layer.height   = m_slicing_params.first_print_layer_height;
                            } else {
                                // Don't know the height of the top contact layer yet. The top contact layer is printed with a normal flow and 
                                // its height will be set adaptively later on.
                            }
                        }
                    }

                    SupportGridPattern support_grid_pattern(
                        // Support islands, to be stretched into a grid.
                        contact_polygons, 
                        // Trimming polygons, to trim the stretched support islands.
                        slices_margin_cached,
                        // How much to offset the extracted contour outside of the grid.
                        m_object_config->support_material_spacing.value + m_support_material_flow.spacing(),
                        Geometry::deg2rad(m_object_config->support_material_angle.value));
                    // 1) infill polygons, expand them by half the extrusion width + a tiny bit of extra.
                    new_layer.polygons = support_grid_pattern.extract_support(m_support_material_flow.scaled_spacing()/2 + 5);
                    // 2) Contact polygons will be projected down. To keep the interface and base layers to grow, return a contour a tiny bit smaller than the grid cells.
                    new_layer.contact_polygons = new Polygons(support_grid_pattern.extract_support(-3));

                    // Even after the contact layer was expanded into a grid, some of the contact islands may be too tiny to be extruded.
                    // Remove those tiny islands from new_layer.polygons and new_layer.contact_polygons.
                    
                    // Store the overhang polygons.
                    // The overhang polygons are used in the path generator for planning of the contact loops.
                    // if (this->has_contact_loops())
                    new_layer.overhang_polygons = new Polygons(std::move(overhang_polygons));
                    contact_out[layer_id] = &new_layer;
                }
            }
        });
    // Compress contact_out, remove the nullptr items.
    remove_nulls(contact_out);
    BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::top_contact_layers() in parallel - end";

    return contact_out;
}

// Generate bottom contact layers supporting the top contact layers.
// For a soluble interface material synchronize the layer heights with the object, 
// otherwise set the layer height to a bridging flow of a support interface nozzle.
PrintObjectSupportMaterial::MyLayersPtr PrintObjectSupportMaterial::bottom_contact_layers_and_layer_support_areas(
    const PrintObject &object, const MyLayersPtr &top_contacts, MyLayerStorage &layer_storage,
    std::vector<Polygons> &layer_support_areas) const
{
#ifdef SLIC3R_DEBUG
    static int iRun = 0;
    ++ iRun; 
#endif /* SLIC3R_DEBUG */

    // Allocate empty surface areas, one per object layer.
    layer_support_areas.assign(object.total_layer_count(), Polygons());

    // find object top surfaces
    // we'll use them to clip our support and detect where does it stick
    MyLayersPtr bottom_contacts;

    if (! top_contacts.empty()) 
    {
        // There is some support to be built, if there are non-empty top surfaces detected.
        // Sum of unsupported contact areas above the current layer.print_z.
        Polygons  projection;
        // Last top contact layer visited when collecting the projection of contact areas.
        int       contact_idx = int(top_contacts.size()) - 1;
        for (int layer_id = int(object.total_layer_count()) - 2; layer_id >= 0; -- layer_id) {
            BOOST_LOG_TRIVIAL(trace) << "Support generator - bottom_contact_layers - layer " << layer_id;
            const Layer &layer = *object.get_layer(layer_id);
            // Collect projections of all contact areas above or at the same level as this top surface.
            for (; contact_idx >= 0 && top_contacts[contact_idx]->print_z >= layer.print_z; -- contact_idx) {
                Polygons polygons_new;
                // Contact surfaces are expanded away from the object, trimmed by the object.
                // Use a slight positive offset to overlap the touching regions.
#if 0
                // Merge and collect the contact polygons. The contact polygons are inflated, but not extended into a grid form.
                polygons_append(polygons_new, offset(*top_contacts[contact_idx]->contact_polygons, SCALED_EPSILON));
#else
                // Consume the contact_polygons. The contact polygons are already expanded into a grid form.
                polygons_append(polygons_new, std::move(*top_contacts[contact_idx]->contact_polygons));
#endif
                // These are the overhang surfaces. They are touching the object and they are not expanded away from the object.
                // Use a slight positive offset to overlap the touching regions.
                polygons_append(polygons_new, offset(*top_contacts[contact_idx]->overhang_polygons, float(SCALED_EPSILON)));
                polygons_append(projection, union_(polygons_new));
            }
            if (projection.empty())
                continue;
            Polygons projection_raw = union_(projection);

            // Top surfaces of this layer, to be used to stop the surface volume from growing down.
            tbb::task_group task_group;
            if (! m_object_config->support_material_buildplate_only)
                task_group.run([this, &object, &top_contacts, contact_idx, &layer, layer_id, &layer_storage, &layer_support_areas, &bottom_contacts, &projection_raw] {
                    Polygons top = collect_region_slices_by_type(layer, stTop);
        #ifdef SLIC3R_DEBUG
                    {
                        BoundingBox bbox = get_extents(projection_raw);
                        bbox.merge(get_extents(top));
                        ::Slic3r::SVG svg(debug_out_path("support-bottom-layers-raw-%d-%lf.svg", iRun, layer.print_z), bbox);
                        svg.draw(union_ex(top, false), "blue", 0.5f);
                        svg.draw(union_ex(projection_raw, true), "red", 0.5f);
                        svg.draw_outline(union_ex(projection_raw, true), "red", "blue", scale_(0.1f));
                        svg.draw(layer.slices.expolygons, "green", 0.5f);
                    }
        #endif /* SLIC3R_DEBUG */

                    // Now find whether any projection of the contact surfaces above layer.print_z not yet supported by any 
                    // top surfaces above layer.print_z falls onto this top surface. 
                    // Touching are the contact surfaces supported exclusively by this top surfaces.
                    // Don't use a safety offset as it has been applied during insertion of polygons.
                    if (! top.empty()) {
                        Polygons touching = intersection(top, projection_raw, false);
                        if (! touching.empty()) {
                            // Allocate a new bottom contact layer.
                            MyLayer &layer_new = layer_allocate(layer_storage, sltBottomContact);
                            bottom_contacts.push_back(&layer_new);
                            // Grow top surfaces so that interface and support generation are generated
                            // with some spacing from object - it looks we don't need the actual
                            // top shapes so this can be done here
                            layer_new.height  = m_slicing_params.soluble_interface ? 
                                // Align the interface layer with the object's layer height.
                                object.layers[layer_id + 1]->height :
                                // Place a bridge flow interface layer over the top surface.
                                m_support_material_interface_flow.nozzle_diameter;
                            layer_new.print_z = m_slicing_params.soluble_interface ? object.layers[layer_id + 1]->print_z :
                                layer.print_z + layer_new.height + m_object_config->support_material_contact_distance.value;
                            layer_new.bottom_z = layer.print_z;
                            layer_new.idx_object_layer_below = layer_id;
                            layer_new.bridging = ! m_slicing_params.soluble_interface;
                            //FIXME how much to inflate the top surface?
                            layer_new.polygons = offset(touching, float(m_support_material_flow.scaled_width()), SUPPORT_SURFACES_OFFSET_PARAMETERS);
                            if (! m_slicing_params.soluble_interface) {
                                // Walk the top surfaces, snap the top of the new bottom surface to the closest top of the top surface,
                                // so there will be no support surfaces generated with thickness lower than m_support_layer_height_min.
                                for (size_t top_idx = size_t(std::max<int>(0, contact_idx)); 
                                    top_idx < top_contacts.size() && top_contacts[top_idx]->print_z < layer_new.print_z + this->m_support_layer_height_min; 
                                    ++ top_idx) {
                                    if (top_contacts[top_idx]->print_z > layer_new.print_z - this->m_support_layer_height_min) {
                                        // A top layer has been found, which is close to the new bottom layer.
                                        coordf_t diff = layer_new.print_z - top_contacts[top_idx]->print_z;
                                        assert(std::abs(diff) <= this->m_support_layer_height_min);
                                        if (diff > 0.) {
                                            // The top contact layer is below this layer. Make the bridging layer thinner to align with the existing top layer.
                                            assert(diff < layer_new.height + EPSILON);
                                            assert(layer_new.height - diff >= this->m_support_layer_height_min - EPSILON);
                                            layer_new.print_z  = top_contacts[top_idx]->print_z;
                                            layer_new.height  -= diff;
                                        } else {
                                            // The top contact layer is above this layer. One may either make this layer thicker or thinner.
                                            // By making the layer thicker, one will decrease the number of discrete layers with the price of extruding a bit too thick bridges.
                                            // By making the layer thinner, one adds one more discrete layer.
                                            layer_new.print_z  = top_contacts[top_idx]->print_z;
                                            layer_new.height  -= diff;
                                        }
                                        break;
                                    }
                                }
                            }
                #ifdef SLIC3R_DEBUG
                            Slic3r::SVG::export_expolygons(
                                debug_out_path("support-bottom-contacts-%d-%lf.svg", iRun, layer_new.print_z),
                                union_ex(layer_new.polygons, false));
                #endif /* SLIC3R_DEBUG */
                            // Trim the already created base layers above the current layer intersecting with the new bottom contacts layer.
                            touching = offset(touching, float(SCALED_EPSILON));
                            for (int layer_id_above = layer_id + 1; layer_id_above < int(object.total_layer_count()); ++ layer_id_above) {
                                const Layer &layer_above = *object.layers[layer_id_above];
                                if (layer_above.print_z > layer_new.print_z + EPSILON)
                                    break; 
                                if (! layer_support_areas[layer_id_above].empty()) {
#ifdef SLIC3R_DEBUG
                                    {
                                        BoundingBox bbox = get_extents(touching);
                                        bbox.merge(get_extents(layer_support_areas[layer_id_above]));
                                        ::Slic3r::SVG svg(debug_out_path("support-support-areas-raw-before-trimming-%d-with-%f-%lf.svg", iRun, layer.print_z, layer_above.print_z), bbox);
                                        svg.draw(union_ex(touching, false), "blue", 0.5f);
                                        svg.draw(union_ex(layer_support_areas[layer_id_above], true), "red", 0.5f);
                                        svg.draw_outline(union_ex(layer_support_areas[layer_id_above], true), "red", "blue", scale_(0.1f));
                                    }
#endif /* SLIC3R_DEBUG */
                                    layer_support_areas[layer_id_above] = diff(layer_support_areas[layer_id_above], touching);
#ifdef SLIC3R_DEBUG
                                    Slic3r::SVG::export_expolygons(
                                        debug_out_path("support-support-areas-raw-after-trimming-%d-with-%f-%lf.svg", iRun, layer.print_z, layer_above.print_z),
                                        union_ex(layer_support_areas[layer_id_above], false));
#endif /* SLIC3R_DEBUG */
                                }
                            }
                        }
                    } // ! top.empty()
                });

            Polygons &layer_support_area = layer_support_areas[layer_id];
            task_group.run([this, &projection, &projection_raw, &layer, &layer_support_area, layer_id] {
                // Remove the areas that touched from the projection that will continue on next, lower, top surfaces.
    //            Polygons trimming = union_(to_polygons(layer.slices.expolygons), touching, true);
                Polygons trimming = offset(layer.slices.expolygons, float(SCALED_EPSILON));
                projection = diff(projection_raw, trimming, false);
    #ifdef SLIC3R_DEBUG
                {
                    BoundingBox bbox = get_extents(projection_raw);
                    bbox.merge(get_extents(trimming));
                    ::Slic3r::SVG svg(debug_out_path("support-support-areas-raw-%d-%lf.svg", iRun, layer.print_z), bbox);
                    svg.draw(union_ex(trimming, false), "blue", 0.5f);
                    svg.draw(union_ex(projection, true), "red", 0.5f);
                    svg.draw_outline(union_ex(projection, true), "red", "blue", scale_(0.1f));
                }
    #endif /* SLIC3R_DEBUG */
                remove_sticks(projection);
                remove_degenerate(projection);
        #ifdef SLIC3R_DEBUG
                Slic3r::SVG::export_expolygons(
                    debug_out_path("support-support-areas-raw-cleaned-%d-%lf.svg", iRun, layer.print_z),
                    union_ex(projection, false));
        #endif /* SLIC3R_DEBUG */
                SupportGridPattern support_grid_pattern(
                    // Support islands, to be stretched into a grid.
                    projection, 
                    // Trimming polygons, to trim the stretched support islands.
                    trimming,
                    // How much to offset the extracted contour outside of the grid.
                    m_object_config->support_material_spacing.value + m_support_material_flow.spacing(),
                    Geometry::deg2rad(m_object_config->support_material_angle.value));
                tbb::task_group task_group_inner;
                // 1) Cache the slice of a support volume. The support volume is expanded by 1/2 of support material flow spacing
                // to allow a placement of suppot zig-zag snake along the grid lines.
                task_group_inner.run([this, &support_grid_pattern, &layer_support_area
        #ifdef SLIC3R_DEBUG 
                    , &layer
        #endif /* SLIC3R_DEBUG */
                    ] {
                    layer_support_area = support_grid_pattern.extract_support(m_support_material_flow.scaled_spacing()/2 + 25);
        #ifdef SLIC3R_DEBUG
                    Slic3r::SVG::export_expolygons(
                        debug_out_path("support-layer_support_area-gridded-%d-%lf.svg", iRun, layer.print_z),
                        union_ex(layer_support_area, false));
        #endif /* SLIC3R_DEBUG */
                });
                // 2) Support polygons will be projected down. To keep the interface and base layers from growing, return a contour a tiny bit smaller than the grid cells.
                Polygons projection_new;
                task_group_inner.run([&projection_new, &support_grid_pattern
        #ifdef SLIC3R_DEBUG 
                    , &layer
        #endif /* SLIC3R_DEBUG */
                    ] {
                    projection_new = support_grid_pattern.extract_support(-5);
        #ifdef SLIC3R_DEBUG
                    Slic3r::SVG::export_expolygons(
                        debug_out_path("support-projection_new-gridded-%d-%lf.svg", iRun, layer.print_z),
                        union_ex(projection_new, false));
        #endif /* SLIC3R_DEBUG */
                });
                task_group_inner.wait();
                projection = std::move(projection_new);
            });
            task_group.wait();
        }
        std::reverse(bottom_contacts.begin(), bottom_contacts.end());
        trim_support_layers_by_object(object, bottom_contacts, m_slicing_params.soluble_interface ? 0. : m_support_layer_height_min, 0., m_gap_xy);
    } // ! top_contacts.empty()

    return bottom_contacts;
}

// FN_HIGHER_EQUAL: the provided object pointer has a Z value >= of an internal threshold.
// Find the first item with Z value >= of an internal threshold of fn_higher_equal.
// If no vec item with Z value >= of an internal threshold of fn_higher_equal is found, return vec.size()
// If the initial idx is size_t(-1), then use binary search.
// Otherwise search linearly upwards.
template<typename T, typename FN_HIGHER_EQUAL>
size_t idx_higher_or_equal(const std::vector<T*> &vec, size_t idx, FN_HIGHER_EQUAL fn_higher_equal)
{
    if (vec.empty()) {
        idx = 0;
    } else if (idx == size_t(-1)) {
        // First of the batch of layers per thread pool invocation. Use binary search.
        int idx_low  = 0;
        int idx_high = std::max(0, int(vec.size()) - 1);
        while (idx_low + 1 < idx_high) {
            int idx_mid  = (idx_low + idx_high) / 2;
            if (fn_higher_equal(vec[idx_mid]))
                idx_high = idx_mid;
            else
                idx_low  = idx_mid;
        }
        idx =  fn_higher_equal(vec[idx_low])  ? idx_low  :
              (fn_higher_equal(vec[idx_high]) ? idx_high : vec.size());
    } else {
        // For the other layers of this batch of layers, search incrementally, which is cheaper than the binary search.
        while (idx < vec.size() && ! fn_higher_equal(vec[idx]))
            ++ idx;
    }
    return idx;
}

// FN_LOWER_EQUAL: the provided object pointer has a Z value <= of an internal threshold.
// Find the first item with Z value <= of an internal threshold of fn_lower_equal.
// If no vec item with Z value <= of an internal threshold of fn_lower_equal is found, return -1.
// If the initial idx is < -1, then use binary search.
// Otherwise search linearly downwards.
template<typename T, typename FN_LOWER_EQUAL>
int idx_lower_or_equal(const std::vector<T*> &vec, int idx, FN_LOWER_EQUAL fn_lower_equal)
{
    if (vec.empty()) {
        idx = -1;
    } else if (idx < -1) {
        // First of the batch of layers per thread pool invocation. Use binary search.
        int idx_low  = 0;
        int idx_high = std::max(0, int(vec.size()) - 1);
        while (idx_low + 1 < idx_high) {
            int idx_mid  = (idx_low + idx_high) / 2;
            if (fn_lower_equal(vec[idx_mid]))
                idx_low  = idx_mid;
            else
                idx_high = idx_mid;
        }
        idx =  fn_lower_equal(vec[idx_high]) ? idx_high :
              (fn_lower_equal(vec[idx_low ]) ? idx_low  : -1);
    } else {
        // For the other layers of this batch of layers, search incrementally, which is cheaper than the binary search.
        while (idx >= 0 && ! fn_lower_equal(vec[idx]))
            -- idx;
    }
    return idx;
}

// Trim the top_contacts layers with the bottom_contacts layers if they overlap, so there would not be enough vertical space for both of them.
void PrintObjectSupportMaterial::trim_top_contacts_by_bottom_contacts(
    const PrintObject &object, const MyLayersPtr &bottom_contacts, MyLayersPtr &top_contacts) const
{
    tbb::parallel_for(tbb::blocked_range<int>(0, int(top_contacts.size())),
        [this, &object, &bottom_contacts, &top_contacts](const tbb::blocked_range<int>& range) {
            int idx_bottom_overlapping_first = -2;
            // For all top contact layers, counting downwards due to the way idx_higher_or_equal caches the last index to avoid repeated binary search.
            for (int idx_top = range.end() - 1; idx_top >= range.begin(); -- idx_top) {
                MyLayer &layer_top = *top_contacts[idx_top];
                // Find the first bottom layer overlapping with layer_top.
                idx_bottom_overlapping_first = idx_lower_or_equal(bottom_contacts, idx_bottom_overlapping_first, [&layer_top](const MyLayer *layer_bottom){ return layer_bottom->bottom_print_z() - EPSILON <= layer_top.bottom_z; });
                // For all top contact layers overlapping with the thick bottom contact layer:
                for (int idx_bottom_overlapping = idx_bottom_overlapping_first; idx_bottom_overlapping >= 0; -- idx_bottom_overlapping) {
                    const MyLayer &layer_bottom = *bottom_contacts[idx_bottom_overlapping];
                    assert(layer_bottom.bottom_print_z() - EPSILON <= layer_top.bottom_z);
                    if (layer_top.print_z < layer_bottom.print_z + EPSILON) {
                        // Layers overlap. Trim layer_top with layer_bottom.
                        layer_top.polygons = diff(layer_top.polygons, layer_bottom.polygons);
                    } else
                        break;
                }
            }
        });
}

PrintObjectSupportMaterial::MyLayersPtr PrintObjectSupportMaterial::raft_and_intermediate_support_layers(
    const PrintObject   &object,
    const MyLayersPtr   &bottom_contacts,
    const MyLayersPtr   &top_contacts,
    MyLayerStorage      &layer_storage) const
{
    MyLayersPtr intermediate_layers;

    // Collect and sort the extremes (bottoms of the top contacts and tops of the bottom contacts).
    MyLayersPtr extremes;
    extremes.reserve(top_contacts.size() + bottom_contacts.size());
    for (size_t i = 0; i < top_contacts.size(); ++ i)
        // Bottoms of the top contact layers. In case of non-soluble supports,
        // the top contact layer thickness is not known yet.
        extremes.push_back(top_contacts[i]);
    for (size_t i = 0; i < bottom_contacts.size(); ++ i)
        // Tops of the bottom contact layers.
        extremes.push_back(bottom_contacts[i]);
    if (extremes.empty())
        return intermediate_layers;

    auto layer_extreme_lower = [](const MyLayer *l1, const MyLayer *l2) {
        coordf_t z1 = l1->extreme_z();
        coordf_t z2 = l2->extreme_z();
        // If the layers are aligned, return the top contact surface first.
        return z1 < z2 || (z1 == z2 && l1->layer_type == PrintObjectSupportMaterial::sltTopContact && l2->layer_type == PrintObjectSupportMaterial::sltBottomContact);
    };
    std::sort(extremes.begin(), extremes.end(), layer_extreme_lower);

    assert(extremes.empty() || 
        (extremes.front()->extreme_z() > m_slicing_params.raft_interface_top_z - EPSILON && 
          (m_slicing_params.raft_layers() == 1 || // only raft contact layer
           extremes.front()->layer_type == sltTopContact || // first extreme is a top contact layer
           extremes.front()->extreme_z() > m_slicing_params.first_print_layer_height - EPSILON)));

    bool synchronize = this->synchronize_layers();

#ifdef _DEBUG
    // Verify that the extremes are separated by m_support_layer_height_min.
    for (size_t i = 1; i < extremes.size(); ++ i) {
        assert(extremes[i]->extreme_z() - extremes[i-1]->extreme_z() == 0. ||
               extremes[i]->extreme_z() - extremes[i-1]->extreme_z() > this->m_support_layer_height_min - EPSILON);
        assert(extremes[i]->extreme_z() - extremes[i-1]->extreme_z() > 0. ||
               extremes[i]->layer_type == extremes[i-1]->layer_type ||
               (extremes[i]->layer_type == sltBottomContact && extremes[i - 1]->layer_type == sltTopContact));
    }
#endif

    // Generate intermediate layers.
    // The first intermediate layer is the same as the 1st layer if there is no raft,
    // or the bottom of the first intermediate layer is aligned with the bottom of the raft contact layer.
    // Intermediate layers are always printed with a normal etrusion flow (non-bridging).
    size_t idx_layer_object = 0;
    for (size_t idx_extreme = 0; idx_extreme < extremes.size(); ++ idx_extreme) {
        MyLayer      *extr2  = extremes[idx_extreme];
        coordf_t      extr2z = extr2->extreme_z();
        if (std::abs(extr2z - m_slicing_params.raft_interface_top_z) < EPSILON) {
            // This is a raft contact layer, its height has been decided in this->top_contact_layers().
            assert(extr2->layer_type == sltTopContact);
            continue;
        }
        if (std::abs(extr2z - m_slicing_params.first_print_layer_height) < EPSILON) {
            // This is a bottom of a synchronized (or soluble) top contact layer, its height has been decided in this->top_contact_layers().
            assert(extr2->layer_type == sltTopContact);
            assert(extr2->bottom_z == m_slicing_params.first_print_layer_height);
            assert(extr2->print_z >= m_slicing_params.first_print_layer_height + this->m_support_layer_height_min - EPSILON);
            if (intermediate_layers.empty() || intermediate_layers.back()->print_z < m_slicing_params.first_print_layer_height) {
                MyLayer &layer_new = layer_allocate(layer_storage, sltIntermediate);
                layer_new.bottom_z = 0.;
                layer_new.print_z  = m_slicing_params.first_print_layer_height;
                layer_new.height   = m_slicing_params.first_print_layer_height;
                intermediate_layers.push_back(&layer_new);
            }
            continue;
        }
        assert(extr2z >= m_slicing_params.raft_interface_top_z + EPSILON);
        assert(extr2z >= m_slicing_params.first_print_layer_height + EPSILON);
        MyLayer      *extr1  = (idx_extreme == 0) ? nullptr : extremes[idx_extreme - 1];
        // Fuse a support layer firmly to the raft top interface (not to the raft contacts).
        coordf_t      extr1z = (extr1 == nullptr) ? m_slicing_params.raft_interface_top_z : extr1->extreme_z();
        assert(extr2z >= extr1z);
        assert(extr2z > extr1z || (extr1 != nullptr && extr2->layer_type == sltBottomContact));
        if (std::abs(extr1z) < EPSILON) {
            // This layer interval starts with the 1st layer. Print the 1st layer using the prescribed 1st layer thickness.
            assert(! m_slicing_params.has_raft());
            assert(intermediate_layers.empty() || intermediate_layers.back()->print_z <= m_slicing_params.first_print_layer_height);
            // At this point only layers above first_print_layer_heigth + EPSILON are expected as the other cases were captured earlier.
            assert(extr2z >= m_slicing_params.first_print_layer_height + EPSILON);
            // Generate a new intermediate layer.
            MyLayer &layer_new = layer_allocate(layer_storage, sltIntermediate);
            layer_new.bottom_z = 0.;
            layer_new.print_z  = extr1z = m_slicing_params.first_print_layer_height;
            layer_new.height   = extr1z;
            intermediate_layers.push_back(&layer_new);
            // Continue printing the other layers up to extr2z.
        }
        coordf_t      dist   = extr2z - extr1z;
        assert(dist >= 0.);
        if (dist == 0.)
            continue;
        // The new layers shall be at least m_support_layer_height_min thick.
        assert(dist >= m_support_layer_height_min - EPSILON);
        if (synchronize) {
            // Emit support layers synchronized with the object layers.
            // Find the first object layer, which has its print_z in this support Z range.
            while (idx_layer_object < object.layers.size() && object.layers[idx_layer_object]->print_z < extr1z + EPSILON)
                ++ idx_layer_object;
            if (idx_layer_object == 0 && extr1z == m_slicing_params.raft_interface_top_z) {
                // Insert one base support layer below the object.
                MyLayer &layer_new = layer_allocate(layer_storage, sltIntermediate);
                layer_new.print_z  = m_slicing_params.object_print_z_min;
                layer_new.bottom_z = m_slicing_params.raft_interface_top_z;
                layer_new.height   = layer_new.print_z - layer_new.bottom_z;
                intermediate_layers.push_back(&layer_new);
            }
            // Emit all intermediate support layers synchronized with object layers up to extr2z.
            for (; idx_layer_object < object.layers.size() && object.layers[idx_layer_object]->print_z < extr2z + EPSILON; ++ idx_layer_object) {
                MyLayer &layer_new = layer_allocate(layer_storage, sltIntermediate);
                layer_new.print_z  = object.layers[idx_layer_object]->print_z;
                layer_new.height   = object.layers[idx_layer_object]->height;
                layer_new.bottom_z = (idx_layer_object > 0) ? object.layers[idx_layer_object - 1]->print_z : (layer_new.print_z - layer_new.height);
                assert(intermediate_layers.empty() || intermediate_layers.back()->print_z < layer_new.print_z + EPSILON);
                intermediate_layers.push_back(&layer_new);
            }
        } else {
            // Insert intermediate layers.
            size_t        n_layers_extra = size_t(ceil(dist / m_slicing_params.max_suport_layer_height)); 
            assert(n_layers_extra > 0);
            coordf_t      step   = dist / coordf_t(n_layers_extra);
            if (extr1 != nullptr && extr1->layer_type == sltTopContact &&
                extr1->print_z + this->m_support_layer_height_min > extr1->bottom_z + step) {
                // The bottom extreme is a bottom of a top surface. Ensure that the gap 
                // between the 1st intermediate layer print_z and extr1->print_z is not too small.
                assert(extr1->bottom_z + this->m_support_layer_height_min < extr1->print_z + EPSILON);
                // Generate the first intermediate layer.
                MyLayer &layer_new = layer_allocate(layer_storage, sltIntermediate);
                layer_new.bottom_z = extr1->bottom_z;
                layer_new.print_z  = extr1z = extr1->print_z;
                layer_new.height   = extr1->height;
                intermediate_layers.push_back(&layer_new);
                dist = extr2z - extr1z;
                n_layers_extra = size_t(ceil(dist / m_slicing_params.max_suport_layer_height));
                if (n_layers_extra == 0)
                    continue;
                // Continue printing the other layers up to extr2z.
                step = dist / coordf_t(n_layers_extra);
            }
            if (! m_slicing_params.soluble_interface && extr2->layer_type == sltTopContact) {
                // This is a top interface layer, which does not have a height assigned yet. Do it now.
                assert(extr2->height == 0.);
                assert(extr1z > m_slicing_params.first_print_layer_height - EPSILON);
                extr2->height = step;
                extr2->bottom_z = extr2z = extr2->print_z - step;
                if (-- n_layers_extra == 0)
                    continue;
            }
            coordf_t extr2z_large_steps = extr2z;
            // Take the largest allowed step in the Z axis until extr2z_large_steps is reached.
            for (size_t i = 0; i < n_layers_extra; ++ i) {
                MyLayer &layer_new = layer_allocate(layer_storage, sltIntermediate);
                if (i + 1 == n_layers_extra) {
                    // Last intermediate layer added. Align the last entered layer with extr2z_large_steps exactly.
                    layer_new.bottom_z = (i == 0) ? extr1z : intermediate_layers.back()->print_z;
                    layer_new.print_z = extr2z_large_steps;
                    layer_new.height = layer_new.print_z - layer_new.bottom_z;
                }
                else {
                    // Intermediate layer, not the last added.
                    layer_new.height = step;
                    layer_new.bottom_z = extr1z + i * step;
                    layer_new.print_z = layer_new.bottom_z + step;
                }
                assert(intermediate_layers.empty() || intermediate_layers.back()->print_z <= layer_new.print_z);
                intermediate_layers.push_back(&layer_new);
            }
        }
    }

#ifdef _DEBUG
    for (size_t i = 0; i < top_contacts.size(); ++i)
        assert(top_contacts[i]->height > 0.);
#endif /* _DEBUG */

    return intermediate_layers;
}

// At this stage there shall be intermediate_layers allocated between bottom_contacts and top_contacts, but they have no polygons assigned.
// Also the bottom/top_contacts shall have a layer thickness assigned already.
void PrintObjectSupportMaterial::generate_base_layers(
    const PrintObject   &object,
    const MyLayersPtr   &bottom_contacts,
    const MyLayersPtr   &top_contacts,
    MyLayersPtr         &intermediate_layers,
    const std::vector<Polygons> &layer_support_areas) const
{
#ifdef SLIC3R_DEBUG
    static int iRun = 0;
#endif /* SLIC3R_DEBUG */

    if (top_contacts.empty())
        // No top contacts -> no intermediate layers will be produced.
        return;

    // coordf_t fillet_radius_scaled = scale_(m_object_config->support_material_spacing);

    BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::generate_base_layers() in parallel - start";
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, intermediate_layers.size()),
        [this, &object, &bottom_contacts, &top_contacts, &intermediate_layers, &layer_support_areas](const tbb::blocked_range<size_t>& range) {
            // index -2 means not initialized yet, -1 means intialized and decremented to 0 and then -1.
            int idx_top_contact_above           = -2;
            int idx_bottom_contact_overlapping  = -2;
            int idx_object_layer_above          = -2;
            // Counting down due to the way idx_lower_or_equal caches indices to avoid repeated binary search over the complete sequence.
            for (int idx_intermediate = int(range.end()) - 1; idx_intermediate >= int(range.begin()); -- idx_intermediate)
            {
                BOOST_LOG_TRIVIAL(trace) << "Support generator - generate_base_layers - creating layer " << 
                    idx_intermediate << " of " << intermediate_layers.size();
                MyLayer &layer_intermediate = *intermediate_layers[idx_intermediate];
                // Layers must be sorted by print_z. 
                assert(idx_intermediate == 0 || layer_intermediate.print_z >= intermediate_layers[idx_intermediate - 1]->print_z);

                // Find a top_contact layer touching the layer_intermediate from above, if any, and collect its polygons into polygons_new.
                idx_top_contact_above = idx_lower_or_equal(top_contacts, idx_top_contact_above, 
                    [&layer_intermediate](const MyLayer *layer){ return layer->bottom_z <= layer_intermediate.print_z - EPSILON; });

                // New polygons for layer_intermediate.
                Polygons polygons_new;

                // Use the precomputed layer_support_areas.
                idx_object_layer_above = std::max(0, idx_lower_or_equal(object.layers, idx_object_layer_above, 
                    [&layer_intermediate](const Layer *layer){ return layer->print_z <= layer_intermediate.print_z + EPSILON; }));
                polygons_new = layer_support_areas[idx_object_layer_above];

                // Polygons to trim polygons_new.
                Polygons polygons_trimming; 

                // Trimming the base layer with any overlapping top layer.
                // Following cases are recognized:
                // 1) top.bottom_z >= base.top_z -> No overlap, no trimming needed.
                // 2) base.bottom_z >= top.print_z -> No overlap, no trimming needed.
                // 3) base.print_z > top.print_z  && base.bottom_z >= top.bottom_z -> Overlap, which will be solved inside generate_toolpaths() by reducing the base layer height where it overlaps the top layer. No trimming needed here.
                // 4) base.print_z > top.bottom_z && base.bottom_z < top.bottom_z -> Base overlaps with top.bottom_z. This must not happen.
                // 5) base.print_z <= top.print_z  && base.bottom_z >= top.bottom_z -> Base is fully inside top. Trim base by top.
                int idx_top_contact_overlapping = idx_top_contact_above;
                while (idx_top_contact_overlapping >= 0 && 
                       top_contacts[idx_top_contact_overlapping]->bottom_z > layer_intermediate.print_z - EPSILON)
                    -- idx_top_contact_overlapping; 
                // Collect all the top_contact layer intersecting with this layer.
                for (; idx_top_contact_overlapping >= 0; -- idx_top_contact_overlapping) {
                    MyLayer &layer_top_overlapping = *top_contacts[idx_top_contact_overlapping];
                    if (layer_top_overlapping.print_z < layer_intermediate.bottom_z + EPSILON)
                        break;
                    // Base must not overlap with top.bottom_z.
                    assert(! (layer_intermediate.print_z > layer_top_overlapping.bottom_z + EPSILON && layer_intermediate.bottom_z < layer_top_overlapping.bottom_z - EPSILON));
                    if (layer_intermediate.print_z <= layer_top_overlapping.print_z + EPSILON && layer_intermediate.bottom_z >= layer_top_overlapping.bottom_z - EPSILON)
                        // Base is fully inside top. Trim base by top.
                        polygons_append(polygons_trimming, layer_top_overlapping.polygons);
                }

                // Trimming the base layer with any overlapping bottom layer.
                // Following cases are recognized:
                // 1) bottom.bottom_z >= base.top_z -> No overlap, no trimming needed.
                // 2) base.bottom_z >= bottom.print_z -> No overlap, no trimming needed.
                // 3) base.print_z > bottom.bottom_z && base.bottom_z < bottom.bottom_z -> Overlap, which will be solved inside generate_toolpaths() by reducing the bottom layer height where it overlaps the base layer. No trimming needed here.
                // 4) base.print_z > bottom.print_z  && base.bottom_z >= bottom.print_z -> Base overlaps with bottom.print_z. This must not happen.
                // 5) base.print_z <= bottom.print_z && base.bottom_z >= bottom.bottom_z -> Base is fully inside top. Trim base by top.
                idx_bottom_contact_overlapping = idx_lower_or_equal(bottom_contacts, idx_bottom_contact_overlapping, 
                    [&layer_intermediate](const MyLayer *layer){ return layer->bottom_print_z() <= layer_intermediate.print_z - EPSILON; });
                // Collect all the bottom_contacts layer intersecting with this layer.
                for (int i = idx_bottom_contact_overlapping; i >= 0; -- i) {
                    MyLayer &layer_bottom_overlapping = *bottom_contacts[i];
                    if (layer_bottom_overlapping.print_z < layer_intermediate.bottom_print_z() + EPSILON)
                        break; 
                    // Base must not overlap with bottom.top_z.
                    assert(! (layer_intermediate.print_z > layer_bottom_overlapping.print_z + EPSILON && layer_intermediate.bottom_z < layer_bottom_overlapping.print_z - EPSILON));
                    if (layer_intermediate.print_z <= layer_bottom_overlapping.print_z + EPSILON && layer_intermediate.bottom_z >= layer_bottom_overlapping.bottom_print_z() - EPSILON)
                        // Base is fully inside bottom. Trim base by bottom.
                        polygons_append(polygons_trimming, layer_bottom_overlapping.polygons);
                }

        #ifdef SLIC3R_DEBUG
                {
                    BoundingBox bbox = get_extents(polygons_new);
                    bbox.merge(get_extents(polygons_trimming));
                    ::Slic3r::SVG svg(debug_out_path("support-intermediate-layers-raw-%d-%lf.svg", iRun, layer_intermediate.print_z), bbox);
                    svg.draw(union_ex(polygons_new, false), "blue", 0.5f);
                    svg.draw(to_polylines(polygons_new), "blue");
                    svg.draw(union_ex(polygons_trimming, true), "red", 0.5f);
                    svg.draw(to_polylines(polygons_trimming), "red");
                }
        #endif /* SLIC3R_DEBUG */

                // Trim the polygons, store them.
                if (polygons_trimming.empty())
                    layer_intermediate.polygons = std::move(polygons_new);
                else
                    layer_intermediate.polygons = diff(
                        polygons_new,
                        polygons_trimming,
                        true); // safety offset to merge the touching source polygons
                layer_intermediate.layer_type = sltBase;

        #if 0
                    // Fillet the base polygons and trim them again with the top, interface and contact layers.
                    $base->{$i} = diff(
                        offset2(
                            $base->{$i}, 
                            $fillet_radius_scaled, 
                            -$fillet_radius_scaled,
                            # Use a geometric offsetting for filleting.
                            JT_ROUND,
                            0.2*$fillet_radius_scaled),
                        $trim_polygons,
                        false); // don't apply the safety offset.
                }
        #endif
            }
        });
    BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::generate_base_layers() in parallel - end";

#ifdef SLIC3R_DEBUG
    for (MyLayersPtr::const_iterator it = intermediate_layers.begin(); it != intermediate_layers.end(); ++it)
        ::Slic3r::SVG::export_expolygons(
            debug_out_path("support-intermediate-layers-untrimmed-%d-%lf.svg", iRun, (*it)->print_z),
            union_ex((*it)->polygons, false));
    ++ iRun;
#endif /* SLIC3R_DEBUG */

    trim_support_layers_by_object(object, intermediate_layers,  m_slicing_params.soluble_interface ? 0. : m_support_layer_height_min,  m_slicing_params.soluble_interface ? 0. : m_support_layer_height_min, m_gap_xy);
}

void PrintObjectSupportMaterial::trim_support_layers_by_object(
    const PrintObject   &object,
    MyLayersPtr         &support_layers,
    const coordf_t       gap_extra_above,
    const coordf_t       gap_extra_below,
    const coordf_t       gap_xy) const
{
    const float gap_xy_scaled = float(scale_(gap_xy));

    // Collect non-empty layers to be processed in parallel.
    // This is a good idea as pulling a thread from a thread pool for an empty task is expensive.
    MyLayersPtr nonempty_layers;
    nonempty_layers.reserve(support_layers.size());
    for (size_t idx_layer = 0; idx_layer < support_layers.size(); ++ idx_layer) {
        MyLayer *support_layer = support_layers[idx_layer];
        if (! support_layer->polygons.empty() && support_layer->print_z >= m_slicing_params.raft_contact_top_z + EPSILON)
            // Non-empty support layer and not a raft layer.
            nonempty_layers.push_back(support_layer);
    }

    // For all intermediate support layers:
    BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::trim_support_layers_by_object() in parallel - start";
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, nonempty_layers.size()),
        [this, &object, &nonempty_layers, gap_extra_above, gap_extra_below, gap_xy_scaled](const tbb::blocked_range<size_t>& range) {
            size_t idx_object_layer_overlapping = size_t(-1);
            for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++ idx_layer) {
                MyLayer &support_layer = *nonempty_layers[idx_layer];
                // BOOST_LOG_TRIVIAL(trace) << "Support generator - trim_support_layers_by_object - trimmming non-empty layer " << idx_layer << " of " << nonempty_layers.size();
                assert(! support_layer.polygons.empty() && support_layer.print_z >= m_slicing_params.raft_contact_top_z + EPSILON);
                // Find the overlapping object layers including the extra above / below gap.
                coordf_t z_threshold = support_layer.print_z - support_layer.height - gap_extra_below + EPSILON;
                idx_object_layer_overlapping = idx_higher_or_equal(
                    object.layers, idx_object_layer_overlapping, 
                    [z_threshold](const Layer *layer){ return layer->print_z >= z_threshold; });
                // Collect all the object layers intersecting with this layer.
                Polygons polygons_trimming;
                size_t i = idx_object_layer_overlapping;
                for (; i < object.layers.size(); ++ i) {
                    const Layer &object_layer = *object.layers[i];
                    if (object_layer.print_z - object_layer.height > support_layer.print_z + gap_extra_above - EPSILON)
                        break;
                    polygons_append(polygons_trimming, (Polygons)object_layer.slices);
                }
                if (! this->m_slicing_params.soluble_interface) {
                    // Collect all bottom surfaces, which will be extruded with a bridging flow.
                    for (; i < object.layers.size(); ++ i) {
                        const Layer &object_layer = *object.layers[i];
                        bool some_region_overlaps = false;
                        for (LayerRegion* region : object_layer.regions) {
                            coordf_t nozzle_dmr = region->region()->nozzle_dmr_avg(*this->m_print_config);
                            if (object_layer.print_z - nozzle_dmr > support_layer.print_z + gap_extra_above - EPSILON)
                                break;
                            some_region_overlaps = true;
                            polygons_append(polygons_trimming, to_polygons(region->slices.filter_by_type(stBottomBridge)));
                        }
                        if (! some_region_overlaps)
                            break;
                    }
                }
                // $layer->slices contains the full shape of layer, thus including
                // perimeter's width. $support contains the full shape of support
                // material, thus including the width of its foremost extrusion.
                // We leave a gap equal to a full extrusion width.
                support_layer.polygons = diff(
                    support_layer.polygons,
                    offset(polygons_trimming, gap_xy_scaled, SUPPORT_SURFACES_OFFSET_PARAMETERS));
            }
        });
    BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::trim_support_layers_by_object() in parallel - end";
}

PrintObjectSupportMaterial::MyLayersPtr PrintObjectSupportMaterial::generate_raft_base(
    const MyLayersPtr   &top_contacts,
    const MyLayersPtr   &interface_layers,
    const MyLayersPtr   &base_layers,
    MyLayerStorage      &layer_storage) const
{
    // How much to inflate the support columns to be stable. This also applies to the 1st layer, if no raft layers are to be printed.
    const float inflate_factor_fine      = float(scale_((m_slicing_params.raft_layers() > 1) ? 0.5 : EPSILON));
    const float inflate_factor_1st_layer = float(scale_(3.)) - inflate_factor_fine;
    MyLayer       *contacts      = top_contacts    .empty() ? nullptr : top_contacts    .front();
    MyLayer       *interfaces    = interface_layers.empty() ? nullptr : interface_layers.front();
    MyLayer       *columns_base  = base_layers     .empty() ? nullptr : base_layers     .front();
    if (contacts != nullptr && contacts->print_z > std::max(m_slicing_params.first_print_layer_height, m_slicing_params.raft_contact_top_z) + EPSILON)
        // This is not the raft contact layer.
        contacts = nullptr;
    if (interfaces != nullptr && interfaces->bottom_print_z() > m_slicing_params.raft_interface_top_z + EPSILON)
        // This is not the raft column base layer.
        interfaces = nullptr;
    if (columns_base != nullptr && columns_base->bottom_print_z() > m_slicing_params.raft_interface_top_z + EPSILON)
        // This is not the raft interface layer.
        columns_base = nullptr;

    Polygons interface_polygons;
    if (contacts != nullptr && ! contacts->polygons.empty())
        polygons_append(interface_polygons, offset(contacts->polygons, inflate_factor_fine, SUPPORT_SURFACES_OFFSET_PARAMETERS));
    if (interfaces != nullptr && ! interfaces->polygons.empty())
        polygons_append(interface_polygons, offset(interfaces->polygons, inflate_factor_fine, SUPPORT_SURFACES_OFFSET_PARAMETERS));
 
    // Output vector.
    MyLayersPtr raft_layers;

    if (m_slicing_params.raft_layers() > 1) {
        Polygons base;
        Polygons columns;
        if (columns_base != nullptr) {
            base = columns_base->polygons;
            columns = base;
            if (! interface_polygons.empty())
                // Trim the 1st layer columns with the inflated interface polygons.
                columns = diff(columns, interface_polygons);
        }
        if (! interface_polygons.empty()) {
            // Merge the untrimmed columns base with the expanded raft interface, to be used for the support base and interface.
            base = union_(base, interface_polygons); 
        }
        // Do not add the raft contact layer, only add the raft layers below the contact layer.
        // Insert the 1st layer.
        {
            MyLayer &new_layer = layer_allocate(layer_storage, (m_slicing_params.base_raft_layers > 0) ? sltRaftBase : sltRaftInterface);
            raft_layers.push_back(&new_layer);
            new_layer.print_z = m_slicing_params.first_print_layer_height;
            new_layer.height  = m_slicing_params.first_print_layer_height;
            new_layer.bottom_z = 0.;
            new_layer.polygons = offset(base, inflate_factor_1st_layer);
        }
        // Insert the base layers.
        for (size_t i = 1; i < m_slicing_params.base_raft_layers; ++ i) {
            coordf_t print_z = raft_layers.back()->print_z;
            MyLayer &new_layer  = layer_allocate(layer_storage, sltRaftBase);
            raft_layers.push_back(&new_layer);
            new_layer.print_z  = print_z + m_slicing_params.base_raft_layer_height;
            new_layer.height   = m_slicing_params.base_raft_layer_height;
            new_layer.bottom_z = print_z;
            new_layer.polygons = base;
        }
        // Insert the interface layers.
        for (size_t i = 1; i < m_slicing_params.interface_raft_layers; ++ i) {
            coordf_t print_z = raft_layers.back()->print_z;
            MyLayer &new_layer = layer_allocate(layer_storage, sltRaftInterface);
            raft_layers.push_back(&new_layer);
            new_layer.print_z = print_z + m_slicing_params.interface_raft_layer_height;
            new_layer.height  = m_slicing_params.interface_raft_layer_height;
            new_layer.bottom_z = print_z;
            new_layer.polygons = interface_polygons;
            //FIXME misusing contact_polygons for support columns.
            new_layer.contact_polygons = new Polygons(columns);
        }
    } else if (columns_base != nullptr) {
        // Expand the bases of the support columns in the 1st layer.
        columns_base->polygons = diff(
            offset(columns_base->polygons, inflate_factor_1st_layer),
            offset(m_object->layers.front()->slices.expolygons, scale_(m_gap_xy), SUPPORT_SURFACES_OFFSET_PARAMETERS));
        if (contacts != nullptr)
            columns_base->polygons = diff(columns_base->polygons, interface_polygons);
    }

    return raft_layers;
}

// Convert some of the intermediate layers into top/bottom interface layers.
PrintObjectSupportMaterial::MyLayersPtr PrintObjectSupportMaterial::generate_interface_layers(
    const MyLayersPtr   &bottom_contacts,
    const MyLayersPtr   &top_contacts,
    MyLayersPtr         &intermediate_layers,
    MyLayerStorage      &layer_storage) const
{
//    my $area_threshold = $self->interface_flow->scaled_spacing ** 2;

    MyLayersPtr interface_layers;
    // Contact layer is considered an interface layer, therefore run the following block only if support_material_interface_layers > 1.
    if (! intermediate_layers.empty() && m_object_config->support_material_interface_layers.value > 1) {
        // For all intermediate layers, collect top contact surfaces, which are not further than support_material_interface_layers.
        BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::generate_interface_layers() in parallel - start";
        interface_layers.assign(intermediate_layers.size(), nullptr);
        tbb::spin_mutex layer_storage_mutex;
        tbb::parallel_for(tbb::blocked_range<size_t>(0, intermediate_layers.size()),
            [this, &bottom_contacts, &top_contacts, &intermediate_layers, &layer_storage, &layer_storage_mutex, &interface_layers](const tbb::blocked_range<size_t>& range) {
                // Index of the first top contact layer intersecting the current intermediate layer.
                size_t idx_top_contact_first = size_t(-1);
                // Index of the first bottom contact layer intersecting the current intermediate layer.
                size_t idx_bottom_contact_first = size_t(-1);
                for (size_t idx_intermediate_layer = range.begin(); idx_intermediate_layer < range.end(); ++ idx_intermediate_layer) {
                    MyLayer &intermediate_layer = *intermediate_layers[idx_intermediate_layer];
                    // Top / bottom Z coordinate of a slab, over which we are collecting the top / bottom contact surfaces.
                    coordf_t top_z    = intermediate_layers[std::min<int>(intermediate_layers.size()-1, idx_intermediate_layer + m_object_config->support_material_interface_layers - 1)]->print_z;
                    coordf_t bottom_z = intermediate_layers[std::max<int>(0, int(idx_intermediate_layer) - int(m_object_config->support_material_interface_layers) + 1)]->bottom_z;
                    // Move idx_top_contact_first up until above the current print_z.
                    idx_top_contact_first = idx_higher_or_equal(top_contacts, idx_top_contact_first, [&intermediate_layer](const MyLayer *layer){ return layer->print_z >= intermediate_layer.print_z; });
                    // Collect the top contact areas above this intermediate layer, below top_z.
                    Polygons polygons_top_contact_projected;
                    for (size_t idx_top_contact = idx_top_contact_first; idx_top_contact < top_contacts.size(); ++ idx_top_contact) {
                        const MyLayer &top_contact_layer = *top_contacts[idx_top_contact];
                        if (top_contact_layer.bottom_z - EPSILON > top_z)
                            break;
                        polygons_append(polygons_top_contact_projected, top_contact_layer.polygons);
                    }
                    // Move idx_bottom_contact_first up until touching bottom_z.
                    idx_bottom_contact_first = idx_higher_or_equal(bottom_contacts, idx_bottom_contact_first, [bottom_z](const MyLayer *layer){ return layer->print_z >= bottom_z - EPSILON; });
                    // Collect the top contact areas above this intermediate layer, below top_z.
                    Polygons polygons_bottom_contact_projected;
                    for (size_t idx_bottom_contact = idx_bottom_contact_first; idx_bottom_contact < bottom_contacts.size(); ++ idx_bottom_contact) {
                        const MyLayer &bottom_contact_layer = *bottom_contacts[idx_bottom_contact];
                        if (bottom_contact_layer.print_z - EPSILON > intermediate_layer.bottom_z)
                            break;
                        polygons_append(polygons_bottom_contact_projected, bottom_contact_layer.polygons);
                    }

                    if (polygons_top_contact_projected.empty() && polygons_bottom_contact_projected.empty())
                        continue;

                    // Insert a new layer into top_interface_layers.
                    MyLayer &layer_new = layer_allocate(layer_storage, layer_storage_mutex,
                        polygons_top_contact_projected.empty() ? sltBottomInterface : sltTopInterface);
                    layer_new.print_z    = intermediate_layer.print_z;
                    layer_new.bottom_z   = intermediate_layer.bottom_z;
                    layer_new.height     = intermediate_layer.height;
                    layer_new.bridging   = intermediate_layer.bridging;
                    interface_layers[idx_intermediate_layer] = &layer_new;

                    polygons_append(polygons_top_contact_projected, polygons_bottom_contact_projected);
                    polygons_top_contact_projected = union_(polygons_top_contact_projected, true);
                    layer_new.polygons = intersection(intermediate_layer.polygons, polygons_top_contact_projected);
                    //FIXME filter layer_new.polygons islands by a minimum area?
        //                $interface_area = [ grep abs($_->area) >= $area_threshold, @$interface_area ];
                    intermediate_layer.polygons = diff(intermediate_layer.polygons, polygons_top_contact_projected, false);
                }
            });

        // Compress contact_out, remove the nullptr items.
        remove_nulls(interface_layers);
        BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::generate_interface_layers() in parallel - start";
    }
    
    return interface_layers;
}

static inline void fill_expolygons_generate_paths(
    ExtrusionEntitiesPtr    &dst, 
    const ExPolygons        &expolygons,
    Fill                    *filler,
    float                    density,
    ExtrusionRole            role, 
    const Flow              &flow)
{
    FillParams fill_params;
    fill_params.density = density;
    fill_params.complete = true;
    fill_params.dont_adjust = true;
    for (ExPolygons::const_iterator it_expolygon = expolygons.begin(); it_expolygon != expolygons.end(); ++ it_expolygon) {
        Surface surface(stInternal, *it_expolygon);
        extrusion_entities_append_paths(
            dst,
            filler->fill_surface(&surface, fill_params),
            role, 
            flow.mm3_per_mm(), flow.width, flow.height);
    }
}

static inline void fill_expolygons_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    ExPolygons             &&expolygons,
    Fill                    *filler,
    float                    density,
    ExtrusionRole            role,
    const Flow              &flow)
{
    FillParams fill_params;
    fill_params.density = density;
    fill_params.complete = true;
    fill_params.dont_adjust = true;
    for (ExPolygons::iterator it_expolygon = expolygons.begin(); it_expolygon != expolygons.end(); ++ it_expolygon) {
        Surface surface(stInternal, std::move(*it_expolygon));
        extrusion_entities_append_paths(
            dst,
            filler->fill_surface(&surface, fill_params),
            role,
            flow.mm3_per_mm(), flow.width, flow.height);
    }
}

// Support layers, partially processed.
struct MyLayerExtruded
{
    MyLayerExtruded() : layer(nullptr), m_polygons_to_extrude(nullptr) {}
    ~MyLayerExtruded() { delete m_polygons_to_extrude; m_polygons_to_extrude = nullptr; }

    bool empty() const {
        return layer == nullptr || layer->polygons.empty();
    }

    void set_polygons_to_extrude(Polygons &&polygons) { 
        if (m_polygons_to_extrude == nullptr) 
            m_polygons_to_extrude = new Polygons(std::move(polygons)); 
        else
            *m_polygons_to_extrude = std::move(polygons);
    }
    Polygons& polygons_to_extrude() { return (this->m_polygons_to_extrude == nullptr) ? layer->polygons : *this->m_polygons_to_extrude; }
    const Polygons& polygons_to_extrude() const { return (this->m_polygons_to_extrude == nullptr) ? layer->polygons : *this->m_polygons_to_extrude; }

    bool could_merge(const MyLayerExtruded &other) const {
        return ! this->empty() && ! other.empty() &&
            std::abs(this->layer->height - other.layer->height) < EPSILON &&
            this->layer->bridging == other.layer->bridging;
    }

    // Merge regions, perform boolean union over the merged polygons.
    void merge(MyLayerExtruded &&other) {
        assert(this->could_merge(other));
        // 1) Merge the rest polygons to extrude, if there are any.
        if (other.m_polygons_to_extrude != nullptr) {
            if (this->m_polygons_to_extrude == nullptr) {
                // This layer has no extrusions generated yet, if it has no m_polygons_to_extrude (its area to extrude was not reduced yet).
                assert(this->extrusions.empty());
                this->m_polygons_to_extrude = new Polygons(this->layer->polygons);
            }
            Slic3r::polygons_append(*this->m_polygons_to_extrude, std::move(*other.m_polygons_to_extrude));
            *this->m_polygons_to_extrude = union_(*this->m_polygons_to_extrude, true);
            delete other.m_polygons_to_extrude;
            other.m_polygons_to_extrude = nullptr;
        } else if (this->m_polygons_to_extrude != nullptr) {
            assert(other.m_polygons_to_extrude == nullptr);
            // The other layer has no extrusions generated yet, if it has no m_polygons_to_extrude (its area to extrude was not reduced yet).
            assert(other.extrusions.empty());
            Slic3r::polygons_append(*this->m_polygons_to_extrude, other.layer->polygons);
            *this->m_polygons_to_extrude = union_(*this->m_polygons_to_extrude, true);
        }
        // 2) Merge the extrusions.
        this->extrusions.insert(this->extrusions.end(), other.extrusions.begin(), other.extrusions.end());
        other.extrusions.clear();
        // 3) Merge the infill polygons.
        Slic3r::polygons_append(this->layer->polygons, std::move(other.layer->polygons));
        this->layer->polygons = union_(this->layer->polygons, true);
        other.layer->polygons.clear();
    }

    void polygons_append(Polygons &dst) const {
        if (layer != NULL && ! layer->polygons.empty())
            Slic3r::polygons_append(dst, layer->polygons);
    }

    // The source layer. It carries the height and extrusion type (bridging / non bridging, extrusion height).
    PrintObjectSupportMaterial::MyLayer  *layer;
    // Collect extrusions. They will be exported sorted by the bottom height.
    ExtrusionEntitiesPtr                  extrusions;
    // In case the extrusions are non-empty, m_polygons_to_extrude may contain the rest areas yet to be filled by additional support.
    // This is useful mainly for the loop interfaces, which are generated before the zig-zag infills.
    Polygons                             *m_polygons_to_extrude;
};

typedef std::vector<MyLayerExtruded*> MyLayerExtrudedPtrs;

struct LoopInterfaceProcessor
{
    LoopInterfaceProcessor(coordf_t circle_r) :
        n_contact_loops(0),
        circle_radius(circle_r),
        circle_distance(circle_r * 3.)
    {
        // Shape of the top contact area.
        circle.points.reserve(6);
        for (size_t i = 0; i < 6; ++ i) {
            double angle = double(i) * M_PI / 3.;
            circle.points.push_back(Point(circle_radius * cos(angle), circle_radius * sin(angle)));
        }
    }

    // Generate loop contacts at the top_contact_layer,
    // trim the top_contact_layer->polygons with the areas covered by the loops.
    void generate(MyLayerExtruded &top_contact_layer, const Flow &interface_flow_src) const;

    int         n_contact_loops;
    coordf_t    circle_radius;
    coordf_t    circle_distance;
    Polygon     circle;
};

void LoopInterfaceProcessor::generate(MyLayerExtruded &top_contact_layer, const Flow &interface_flow_src) const
{
    if (n_contact_loops == 0 || top_contact_layer.empty())
        return;

    Flow flow = interface_flow_src;
    flow.height = float(top_contact_layer.layer->height);

    Polygons overhang_polygons;
    if (top_contact_layer.layer->overhang_polygons != nullptr)
        overhang_polygons = std::move(*top_contact_layer.layer->overhang_polygons);

    // Generate the outermost loop.
    // Find centerline of the external loop (or any other kind of extrusions should the loop be skipped)
    ExPolygons top_contact_expolygons = offset_ex(union_ex(top_contact_layer.layer->polygons), - 0.5f * flow.scaled_width());

    // Grid size and bit shifts for quick and exact to/from grid coordinates manipulation.
    coord_t circle_grid_resolution = 1;
    coord_t circle_grid_powerof2 = 0;
    {
        // epsilon to account for rounding errors
        coord_t circle_grid_resolution_non_powerof2 = coord_t(2. * circle_distance + 3.);
        while (circle_grid_resolution < circle_grid_resolution_non_powerof2) {
            circle_grid_resolution <<= 1;
            ++ circle_grid_powerof2;
        }
    }

    struct PointAccessor {
        const Point* operator()(const Point &pt) const { return &pt; }
    };
    typedef ClosestPointInRadiusLookup<Point, PointAccessor> ClosestPointLookupType;
    
    Polygons loops0;
    {
        // find centerline of the external loop of the contours
        // Only consider the loops facing the overhang.
        Polygons external_loops;
        // Holes in the external loops.
        Polygons circles;
        Polygons overhang_with_margin = offset(union_ex(overhang_polygons), 0.5f * flow.scaled_width());
        for (ExPolygons::iterator it_contact_expoly = top_contact_expolygons.begin(); it_contact_expoly != top_contact_expolygons.end(); ++ it_contact_expoly) {
            // Store the circle centers placed for an expolygon into a regular grid, hashed by the circle centers.
            ClosestPointLookupType circle_centers_lookup(coord_t(circle_distance - SCALED_EPSILON));
            Points circle_centers;
            Point  center_last;
            // For each contour of the expolygon, start with the outer contour, continue with the holes.
            for (size_t i_contour = 0; i_contour <= it_contact_expoly->holes.size(); ++ i_contour) {
                Polygon     &contour = (i_contour == 0) ? it_contact_expoly->contour : it_contact_expoly->holes[i_contour - 1];
                const Point *seg_current_pt = nullptr;
                coordf_t     seg_current_t  = 0.;
                if (! intersection_pl(contour.split_at_first_point(), overhang_with_margin).empty()) {
                    // The contour is below the overhang at least to some extent.
                    //FIXME ideally one would place the circles below the overhang only.
                    // Walk around the contour and place circles so their centers are not closer than circle_distance from each other.
                    if (circle_centers.empty()) {
                        // Place the first circle.
                        seg_current_pt = &contour.points.front();
                        seg_current_t  = 0.;
                        center_last    = *seg_current_pt;
                        circle_centers_lookup.insert(center_last);
                        circle_centers.push_back(center_last);
                    }
                    for (Points::const_iterator it = contour.points.begin() + 1; it != contour.points.end(); ++it) {
                        // Is it possible to place a circle on this segment? Is it not too close to any of the circles already placed on this contour?
                        const Point &p1 = *(it-1);
                        const Point &p2 = *it;
                        // Intersection of a ray (p1, p2) with a circle placed at center_last, with radius of circle_distance.
                        const Pointf v_seg(coordf_t(p2.x) - coordf_t(p1.x), coordf_t(p2.y) - coordf_t(p1.y));
                        const Pointf v_cntr(coordf_t(p1.x - center_last.x), coordf_t(p1.y - center_last.y));
                        coordf_t a = dot(v_seg);
                        coordf_t b = 2. * dot(v_seg, v_cntr);
                        coordf_t c = dot(v_cntr) - circle_distance * circle_distance;
                        coordf_t disc = b * b - 4. * a * c;
                        if (disc > 0.) {
                            // The circle intersects a ray. Avoid the parts of the segment inside the circle.
                            coordf_t t1 = (-b - sqrt(disc)) / (2. * a);
                            coordf_t t2 = (-b + sqrt(disc)) / (2. * a);
                            coordf_t t0 = (seg_current_pt == &p1) ? seg_current_t : 0.;
                            // Take the lowest t in <t0, 1.>, excluding <t1, t2>.
                            coordf_t t;
                            if (t0 <= t1)
                                t = t0;
                            else if (t2 <= 1.)
                                t = t2;
                            else {
                                // Try the following segment.
                                seg_current_pt = nullptr;
                                continue;
                            }
                            seg_current_pt = &p1;
                            seg_current_t  = t;
                            center_last    = Point(p1.x + coord_t(v_seg.x * t), p1.y + coord_t(v_seg.y * t));
                            // It has been verified that the new point is far enough from center_last.
                            // Ensure, that it is far enough from all the centers.
                            std::pair<const Point*, coordf_t> circle_closest = circle_centers_lookup.find(center_last);
                            if (circle_closest.first != nullptr) {
                                -- it;
                                continue;
                            }
                        } else {
                            // All of the segment is outside the circle. Take the first point.
                            seg_current_pt = &p1;
                            seg_current_t  = 0.;
                            center_last    = p1;
                        }
                        // Place the first circle.
                        circle_centers_lookup.insert(center_last);
                        circle_centers.push_back(center_last);
                    }
                    external_loops.push_back(std::move(contour));
                    for (Points::const_iterator it_center = circle_centers.begin(); it_center != circle_centers.end(); ++ it_center) {
                        circles.push_back(circle);
                        circles.back().translate(*it_center);
                    }
                }
            }
        }
        // Apply a pattern to the external loops.
        loops0 = diff(external_loops, circles);
    }

    Polylines loop_lines;
    {
        // make more loops
        Polygons loop_polygons = loops0;
        for (size_t i = 1; i < n_contact_loops; ++ i)
            polygons_append(loop_polygons, 
                offset2(
                    loops0, 
                    - int(i) * flow.scaled_spacing() - 0.5f * flow.scaled_spacing(), 
                    0.5f * flow.scaled_spacing()));
        // Clip such loops to the side oriented towards the object.
        // Collect split points, so they will be recognized after the clipping.
        // At the split points the clipped pieces will be stitched back together.
        loop_lines.reserve(loop_polygons.size());
        std::unordered_map<Point, int, PointHash> map_split_points;
        for (Polygons::const_iterator it = loop_polygons.begin(); it != loop_polygons.end(); ++ it) {
            assert(map_split_points.find(it->first_point()) == map_split_points.end());
            map_split_points[it->first_point()] = -1;
            loop_lines.push_back(it->split_at_first_point());
        }
        loop_lines = intersection_pl(loop_lines, offset(overhang_polygons, scale_(SUPPORT_MATERIAL_MARGIN)));
        // Because a closed loop has been split to a line, loop_lines may contain continuous segments split to 2 pieces.
        // Try to connect them.
        for (int i_line = 0; i_line < int(loop_lines.size()); ++ i_line) {
            Polyline &polyline = loop_lines[i_line];
            auto it = map_split_points.find(polyline.first_point());
            if (it != map_split_points.end()) {
                // This is a stitching point.
                // If this assert triggers, multiple source polygons likely intersected at this point.
                assert(it->second != -2);
                if (it->second < 0) {
                    // First occurence.
                    it->second = i_line;
                } else {
                    // Second occurence. Join the lines.
                    Polyline &polyline_1st = loop_lines[it->second];
                    assert(polyline_1st.first_point() == it->first || polyline_1st.last_point() == it->first);
                    if (polyline_1st.first_point() == it->first)
                        polyline_1st.reverse();
                    polyline_1st.append(std::move(polyline));
                    it->second = -2;
                }
                continue;
            }
            it = map_split_points.find(polyline.last_point());
            if (it != map_split_points.end()) {
                // This is a stitching point.
                // If this assert triggers, multiple source polygons likely intersected at this point.
                assert(it->second != -2);
                if (it->second < 0) {
                    // First occurence.
                    it->second = i_line;
                } else {
                    // Second occurence. Join the lines.
                    Polyline &polyline_1st = loop_lines[it->second];
                    assert(polyline_1st.first_point() == it->first || polyline_1st.last_point() == it->first);
                    if (polyline_1st.first_point() == it->first)
                        polyline_1st.reverse();
                    polyline.reverse();
                    polyline_1st.append(std::move(polyline));
                    it->second = -2;
                }
            }
        }
        // Remove empty lines.
        remove_degenerate(loop_lines);
    }
    
    // add the contact infill area to the interface area
    // note that growing loops by $circle_radius ensures no tiny
    // extrusions are left inside the circles; however it creates
    // a very large gap between loops and contact_infill_polygons, so maybe another
    // solution should be found to achieve both goals
    // Store the trimmed polygons into a separate polygon set, so the original infill area remains intact for
    // "modulate by layer thickness".
    top_contact_layer.set_polygons_to_extrude(diff(top_contact_layer.layer->polygons, offset(loop_lines, float(circle_radius * 1.1))));

    // Transform loops into ExtrusionPath objects.
    extrusion_entities_append_paths(
        top_contact_layer.extrusions,
        STDMOVE(loop_lines),
        erSupportMaterialInterface, flow.mm3_per_mm(), flow.width, flow.height);
}

#ifdef SLIC3R_DEBUG
static std::string dbg_index_to_color(int idx)
{
    if (idx < 0)
        return "yellow";
    idx = idx % 3;
    switch (idx) {
        case 0: return "red";
        case 1: return "green";
        default: return "blue";
    }
}
#endif /* SLIC3R_DEBUG */

// When extruding a bottom interface layer over an object, the bottom interface layer is extruded in a thin air, therefore
// it is being extruded with a bridging flow to not shrink excessively (the die swell effect).
// Tiny extrusions are better avoided and it is always better to anchor the thread to an existing support structure if possible.
// Therefore the bottom interface spots are expanded a bit. The expanded regions may overlap with another bottom interface layers,
// leading to over extrusion, where they overlap. The over extrusion is better avoided as it often makes the interface layers
// to stick too firmly to the object.
void modulate_extrusion_by_overlapping_layers(
    // Extrusions generated for this_layer.
    ExtrusionEntitiesPtr                               &extrusions_in_out,
    const PrintObjectSupportMaterial::MyLayer          &this_layer,
    // Multiple layers overlapping with this_layer, sorted bottom up.
    const PrintObjectSupportMaterial::MyLayersPtr      &overlapping_layers)
{
    size_t n_overlapping_layers = overlapping_layers.size();
    if (n_overlapping_layers == 0 || extrusions_in_out.empty())
        // The extrusions do not overlap with any other extrusion.
        return;

    // Get the initial extrusion parameters.
    ExtrusionPath *extrusion_path_template = dynamic_cast<ExtrusionPath*>(extrusions_in_out.front());
    assert(extrusion_path_template != nullptr);
    ExtrusionRole extrusion_role  = extrusion_path_template->role();
    float         extrusion_width = extrusion_path_template->width;

    struct ExtrusionPathFragment
    {
        ExtrusionPathFragment() : mm3_per_mm(-1), width(-1), height(-1) {};
        ExtrusionPathFragment(double mm3_per_mm, float width, float height) : mm3_per_mm(mm3_per_mm), width(width), height(height) {};

        Polylines       polylines;
        double          mm3_per_mm;
        float           width;
        float           height;
    };

    // Split the extrusions by the overlapping layers, reduce their extrusion rate.
    // The last path_fragment is from this_layer.
    std::vector<ExtrusionPathFragment> path_fragments(
        n_overlapping_layers + 1, 
        ExtrusionPathFragment(extrusion_path_template->mm3_per_mm, extrusion_path_template->width, extrusion_path_template->height));
    // Don't use it, it will be released.
    extrusion_path_template = nullptr;

#ifdef SLIC3R_DEBUG
    static int iRun = 0;
    ++ iRun;
    BoundingBox bbox;
    for (size_t i_overlapping_layer = 0; i_overlapping_layer < n_overlapping_layers; ++ i_overlapping_layer) {
        const PrintObjectSupportMaterial::MyLayer &overlapping_layer = *overlapping_layers[i_overlapping_layer];
        bbox.merge(get_extents(overlapping_layer.polygons));
    }
    for (ExtrusionEntitiesPtr::const_iterator it = extrusions_in_out.begin(); it != extrusions_in_out.end(); ++ it) {
        ExtrusionPath *path = dynamic_cast<ExtrusionPath*>(*it);
        assert(path != nullptr);
        bbox.merge(get_extents(path->polyline));
    }
    SVG svg(debug_out_path("support-fragments-%d-%lf.svg", iRun, this_layer.print_z).c_str(), bbox);
    const float transparency = 0.5f;
    // Filled polygons for the overlapping regions.
    svg.draw(union_ex(this_layer.polygons), dbg_index_to_color(-1), transparency);
    for (size_t i_overlapping_layer = 0; i_overlapping_layer < n_overlapping_layers; ++ i_overlapping_layer) {
        const PrintObjectSupportMaterial::MyLayer &overlapping_layer = *overlapping_layers[i_overlapping_layer];
        svg.draw(union_ex(overlapping_layer.polygons), dbg_index_to_color(int(i_overlapping_layer)), transparency);
    }
    // Contours of the overlapping regions.
    svg.draw(to_polylines(this_layer.polygons), dbg_index_to_color(-1), scale_(0.2));
    for (size_t i_overlapping_layer = 0; i_overlapping_layer < n_overlapping_layers; ++ i_overlapping_layer) {
        const PrintObjectSupportMaterial::MyLayer &overlapping_layer = *overlapping_layers[i_overlapping_layer];
        svg.draw(to_polylines(overlapping_layer.polygons), dbg_index_to_color(int(i_overlapping_layer)), scale_(0.1));
    }
    // Fill extrusion, the source.
    for (ExtrusionEntitiesPtr::const_iterator it = extrusions_in_out.begin(); it != extrusions_in_out.end(); ++ it) {
        ExtrusionPath *path = dynamic_cast<ExtrusionPath*>(*it);
        std::string color_name;
        switch ((it - extrusions_in_out.begin()) % 9) {
            case 0: color_name = "magenta"; break;
            case 1: color_name = "deepskyblue"; break;
            case 2: color_name = "coral"; break;
            case 3: color_name = "goldenrod"; break;
            case 4: color_name = "orange"; break;
            case 5: color_name = "olivedrab"; break;
            case 6: color_name = "blueviolet"; break;
            case 7: color_name = "brown"; break;
            default: color_name = "orchid"; break;
        }
        svg.draw(path->polyline, color_name, scale_(0.2));
    }
#endif /* SLIC3R_DEBUG */

    // End points of the original paths.
    std::vector<std::pair<Point, Point>> path_ends; 
    // Collect the paths of this_layer.
    {
        Polylines &polylines = path_fragments.back().polylines;
        for (ExtrusionEntitiesPtr::const_iterator it = extrusions_in_out.begin(); it != extrusions_in_out.end(); ++ it) {
            ExtrusionPath *path = dynamic_cast<ExtrusionPath*>(*it);
            assert(path != nullptr);
            polylines.emplace_back(Polyline(std::move(path->polyline)));
            path_ends.emplace_back(std::pair<Point, Point>(polylines.back().points.front(), polylines.back().points.back()));
        }
    }
    // Destroy the original extrusion paths, their polylines were moved to path_fragments already.
    // This will be the destination for the new paths.
    extrusions_in_out.clear();

    // Fragment the path segments by overlapping layers. The overlapping layers are sorted by an increasing print_z.
    // Trim by the highest overlapping layer first.
    for (int i_overlapping_layer = int(n_overlapping_layers) - 1; i_overlapping_layer >= 0; -- i_overlapping_layer) {
        const PrintObjectSupportMaterial::MyLayer &overlapping_layer = *overlapping_layers[i_overlapping_layer];
        ExtrusionPathFragment &frag = path_fragments[i_overlapping_layer];
        Polygons polygons_trimming = offset(union_ex(overlapping_layer.polygons), float(scale_(0.5*extrusion_width)));
        frag.polylines = intersection_pl(path_fragments.back().polylines, polygons_trimming, false);
        path_fragments.back().polylines = diff_pl(path_fragments.back().polylines, polygons_trimming, false);
        // Adjust the extrusion parameters for a reduced layer height and a non-bridging flow (nozzle_dmr = -1, does not matter).
        assert(this_layer.print_z > overlapping_layer.print_z);
        frag.height = float(this_layer.print_z - overlapping_layer.print_z);
        frag.mm3_per_mm = Flow(frag.width, frag.height, -1.f, false).mm3_per_mm();
#ifdef SLIC3R_DEBUG
        svg.draw(frag.polylines, dbg_index_to_color(i_overlapping_layer), scale_(0.1));
#endif /* SLIC3R_DEBUG */
    }

#ifdef SLIC3R_DEBUG
    svg.draw(path_fragments.back().polylines, dbg_index_to_color(-1), scale_(0.1));
    svg.Close();
#endif /* SLIC3R_DEBUG */

    // Now chain the split segments using hashing and a nearly exact match, maintaining the order of segments.
    // Create a single ExtrusionPath or ExtrusionEntityCollection per source ExtrusionPath.
    // Map of fragment start/end points to a pair of <i_overlapping_layer, i_polyline_in_layer>
    // Because a non-exact matching is used for the end points, a multi-map is used.
    // As the clipper library may reverse the order of some clipped paths, store both ends into the map.
    struct ExtrusionPathFragmentEnd
    {
        ExtrusionPathFragmentEnd(size_t alayer_idx, size_t apolyline_idx, bool ais_start) :
            layer_idx(alayer_idx), polyline_idx(apolyline_idx), is_start(ais_start) {}
        size_t layer_idx;
        size_t polyline_idx;
        bool   is_start;
    };
    class ExtrusionPathFragmentEndPointAccessor {
    public:
        ExtrusionPathFragmentEndPointAccessor(const std::vector<ExtrusionPathFragment> &path_fragments) : m_path_fragments(path_fragments) {}
        // Return an end point of a fragment, or nullptr if the fragment has been consumed already.
        const Point* operator()(const ExtrusionPathFragmentEnd &fragment_end) const {
            const Polyline &polyline = m_path_fragments[fragment_end.layer_idx].polylines[fragment_end.polyline_idx];
            return polyline.points.empty() ? nullptr :
                (fragment_end.is_start ? &polyline.points.front() : &polyline.points.back());
        }
    private:
        ExtrusionPathFragmentEndPointAccessor& operator=(const ExtrusionPathFragmentEndPointAccessor&);
        const std::vector<ExtrusionPathFragment> &m_path_fragments;
    };
    const coord_t search_radius = 7;
    ClosestPointInRadiusLookup<ExtrusionPathFragmentEnd, ExtrusionPathFragmentEndPointAccessor> map_fragment_starts(
        search_radius, ExtrusionPathFragmentEndPointAccessor(path_fragments));
    for (size_t i_overlapping_layer = 0; i_overlapping_layer <= n_overlapping_layers; ++ i_overlapping_layer) {
        const Polylines &polylines = path_fragments[i_overlapping_layer].polylines;
        for (size_t i_polyline = 0; i_polyline < polylines.size(); ++ i_polyline) {
            // Map a starting point of a polyline to a pair of <layer, polyline>
            if (polylines[i_polyline].points.size() >= 2) {
                map_fragment_starts.insert(ExtrusionPathFragmentEnd(i_overlapping_layer, i_polyline, true));
                map_fragment_starts.insert(ExtrusionPathFragmentEnd(i_overlapping_layer, i_polyline, false));
            }
        }
    }

    // For each source path:
    for (size_t i_path = 0; i_path < path_ends.size(); ++ i_path) {
        const Point &pt_start = path_ends[i_path].first;
        const Point &pt_end   = path_ends[i_path].second;
        Point pt_current = pt_start;
        // Find a chain of fragments with the original / reduced print height.
        ExtrusionMultiPath multipath;
        for (;;) {
            // Find a closest end point to pt_current.
            std::pair<const ExtrusionPathFragmentEnd*, coordf_t> end_and_dist2 = map_fragment_starts.find(pt_current);
            // There may be a bug in Clipper flipping the order of two last points in a fragment?
            // assert(end_and_dist2.first != nullptr);
            assert(end_and_dist2.first == nullptr || end_and_dist2.second < search_radius * search_radius);
            if (end_and_dist2.first == nullptr) {
                // New fragment connecting to pt_current was not found.
                // Verify that the last point found is close to the original end point of the unfragmented path.
                //const double d2 = pt_end.distance_to_sq(pt_current);
                //assert(d2 < coordf_t(search_radius * search_radius));
                // End of the path.
                break;
            }
            const ExtrusionPathFragmentEnd &fragment_end_min = *end_and_dist2.first;
            // Fragment to consume.
            ExtrusionPathFragment &frag = path_fragments[fragment_end_min.layer_idx];
            Polyline              &frag_polyline = frag.polylines[fragment_end_min.polyline_idx];
            // Path to append the fragment to.
            ExtrusionPath         *path = multipath.paths.empty() ? nullptr : &multipath.paths.back();
            if (path != nullptr) {
                // Verify whether the path is compatible with the current fragment.
                assert(this_layer.layer_type == PrintObjectSupportMaterial::sltBottomContact || path->height != frag.height || path->mm3_per_mm != frag.mm3_per_mm);
                if (path->height != frag.height || path->mm3_per_mm != frag.mm3_per_mm) {
                    path = nullptr;
                }
                // Merging with the previous path. This can only happen if the current layer was reduced by a base layer, which was split into a base and interface layer.
            }
            if (path == nullptr) {
                // Allocate a new path.
                multipath.paths.push_back(ExtrusionPath(extrusion_role, frag.mm3_per_mm, frag.width, frag.height));
                path = &multipath.paths.back();
            }
            // The Clipper library may flip the order of the clipped polylines arbitrarily.
            // Reverse the source polyline, if connecting to the end.
            if (! fragment_end_min.is_start)
                frag_polyline.reverse();
            // Enforce exact overlap of the end points of successive fragments.
            assert(frag_polyline.points.front() == pt_current);
            frag_polyline.points.front() = pt_current;
            // Don't repeat the first point.
            if (! path->polyline.points.empty())
                path->polyline.points.pop_back();
            // Consume the fragment's polyline, remove it from the input fragments, so it will be ignored the next time.
            path->polyline.append(std::move(frag_polyline));
            frag_polyline.points.clear();
            pt_current = path->polyline.points.back();
            if (pt_current == pt_end) {
                // End of the path.
                break;
            }
        }
        if (!multipath.paths.empty()) {
            if (multipath.paths.size() == 1) {
                // This path was not fragmented.
                extrusions_in_out.push_back(new ExtrusionPath(std::move(multipath.paths.front())));
            } else {
                // This path was fragmented. Copy the collection as a whole object, so the order inside the collection will not be changed
                // during the chaining of extrusions_in_out.
                extrusions_in_out.push_back(new ExtrusionMultiPath(std::move(multipath)));
            }
        }
    }
    // If there are any non-consumed fragments, add them separately.
    //FIXME this shall not happen, if the Clipper works as expected and all paths split to fragments could be re-connected.
    for (auto it_fragment = path_fragments.begin(); it_fragment != path_fragments.end(); ++ it_fragment)
        extrusion_entities_append_paths(extrusions_in_out, std::move(it_fragment->polylines), extrusion_role, it_fragment->mm3_per_mm, it_fragment->width, it_fragment->height);
}

void PrintObjectSupportMaterial::generate_toolpaths(
    const PrintObject   &object,
    const MyLayersPtr   &raft_layers,
    const MyLayersPtr   &bottom_contacts,
    const MyLayersPtr   &top_contacts,
    const MyLayersPtr   &intermediate_layers,
    const MyLayersPtr   &interface_layers) const
{
//    Slic3r::debugf "Generating patterns\n";
    // loop_interface_processor with a given circle radius.
    LoopInterfaceProcessor loop_interface_processor(1.5 * m_support_material_interface_flow.scaled_width());
    loop_interface_processor.n_contact_loops = this->has_contact_loops() ? 1 : 0;

    float    base_angle         = Geometry::deg2rad(float(m_object_config->support_material_angle.value));
    float    interface_angle    = Geometry::deg2rad(float(m_object_config->support_material_angle.value + 90.));
    coordf_t interface_spacing  = m_object_config->support_material_interface_spacing.value + m_support_material_interface_flow.spacing();
    coordf_t interface_density  = std::min(1., m_support_material_interface_flow.spacing() / interface_spacing);
    coordf_t support_spacing    = m_object_config->support_material_spacing.value + m_support_material_flow.spacing();
    coordf_t support_density    = std::min(1., m_support_material_flow.spacing() / support_spacing);
    if (m_object_config->support_material_interface_layers.value == 0) {
        // No interface layers allowed, print everything with the base support pattern.
        interface_spacing = support_spacing;
        interface_density = support_density;
    }

    // Prepare fillers.
    SupportMaterialPattern  support_pattern = m_object_config->support_material_pattern;
    bool                    with_sheath     = m_object_config->support_material_with_sheath;
    InfillPattern           infill_pattern;
    std::vector<float>      angles;
    angles.push_back(base_angle);
    switch (support_pattern) {
    case smpRectilinearGrid:
        angles.push_back(interface_angle);
        // fall through
    case smpRectilinear:
        infill_pattern = ipRectilinear;
        break;
    case smpHoneycomb:
    case smpPillars:
        infill_pattern = ipHoneycomb;
        break;
    }
    BoundingBox bbox_object(Point(-scale_(1.), -scale_(1.0)), Point(scale_(1.), scale_(1.)));

//    const coordf_t link_max_length_factor = 3.;
    const coordf_t link_max_length_factor = 0.;

    float raft_angle_1st_layer  = 0.f;
    float raft_angle_base       = 0.f;
    float raft_angle_interface  = 0.f;
    if (m_slicing_params.base_raft_layers > 1) {
        // There are all raft layer types (1st layer, base, interface & contact layers) available.
        raft_angle_1st_layer  = interface_angle;
        raft_angle_base       = base_angle;
        raft_angle_interface  = interface_angle;
    } else if (m_slicing_params.base_raft_layers == 1 || m_slicing_params.interface_raft_layers > 1) {
        // 1st layer, interface & contact layers available.
        raft_angle_1st_layer  = base_angle;
        if (this->has_support())
            // Print 1st layer at 45 degrees from both the interface and base angles as both can land on the 1st layer.
            raft_angle_1st_layer += 0.7854f;
        raft_angle_interface  = interface_angle;
    } else if (m_slicing_params.interface_raft_layers == 1) {
        // Only the contact raft layer is non-empty, which will be printed as the 1st layer.
        assert(m_slicing_params.base_raft_layers == 0);
        assert(m_slicing_params.interface_raft_layers == 1);
        assert(m_slicing_params.raft_layers() == 1 && raft_layers.size() == 0);
    } else {
        // No raft.
        assert(m_slicing_params.base_raft_layers == 0);
        assert(m_slicing_params.interface_raft_layers == 0);
        assert(m_slicing_params.raft_layers() == 0 && raft_layers.size() == 0);
    }

    // Insert the raft base layers.
    size_t n_raft_layers = size_t(std::max(0, int(m_slicing_params.raft_layers()) - 1));
    tbb::parallel_for(tbb::blocked_range<size_t>(0, n_raft_layers),
        [this, &object, &raft_layers, 
            infill_pattern, &bbox_object, support_density, interface_density, raft_angle_1st_layer, raft_angle_base, raft_angle_interface, link_max_length_factor, with_sheath]
            (const tbb::blocked_range<size_t>& range) {
        for (size_t support_layer_id = range.begin(); support_layer_id < range.end(); ++ support_layer_id)
        {
            assert(support_layer_id < raft_layers.size());
            SupportLayer &support_layer = *object.support_layers[support_layer_id];
            assert(support_layer.support_fills.entities.empty());
            MyLayer      &raft_layer    = *raft_layers[support_layer_id];

            std::unique_ptr<Fill> filler_interface = std::unique_ptr<Fill>(Fill::new_from_type(ipRectilinear));
            std::unique_ptr<Fill> filler_support   = std::unique_ptr<Fill>(Fill::new_from_type(infill_pattern));
            filler_interface->set_bounding_box(bbox_object);
            filler_support->set_bounding_box(bbox_object);

            // Print the support base below the support columns, or the support base for the support columns plus the contacts.
            if (support_layer_id > 0) {
                Polygons to_infill_polygons = (support_layer_id < m_slicing_params.base_raft_layers) ? 
                    raft_layer.polygons :
                    //FIXME misusing contact_polygons for support columns.
                    ((raft_layer.contact_polygons == nullptr) ? Polygons() : *raft_layer.contact_polygons);
                if (! to_infill_polygons.empty()) {
                    Flow flow(float(m_support_material_flow.width), float(raft_layer.height), m_support_material_flow.nozzle_diameter, raft_layer.bridging);
                    // find centerline of the external loop/extrusions
                    ExPolygons to_infill = (support_layer_id == 0 || ! with_sheath) ?
                        // union_ex(base_polygons, true) :
                        offset2_ex(to_infill_polygons, float(SCALED_EPSILON), float(- SCALED_EPSILON)) :
                        offset2_ex(to_infill_polygons, float(SCALED_EPSILON), float(- SCALED_EPSILON - 0.5*flow.scaled_width()));            
                    if (! to_infill.empty() && with_sheath) {
                        // Draw a perimeter all around the support infill. This makes the support stable, but difficult to remove.
                        // TODO: use brim ordering algorithm
                        to_infill_polygons = to_polygons(to_infill);
                        // TODO: use offset2_ex()
                        to_infill = offset_ex(to_infill, float(- 0.4 * flow.scaled_spacing()));
                        extrusion_entities_append_paths(
                            support_layer.support_fills.entities, 
                            to_polylines(STDMOVE(to_infill_polygons)),
                            erSupportMaterial, flow.mm3_per_mm(), flow.width, flow.height);
                    }
                    if (! to_infill.empty()) {
                        // We don't use $base_flow->spacing because we need a constant spacing
                        // value that guarantees that all layers are correctly aligned.
                        Fill *filler    = filler_support.get();
                        filler->angle   = raft_angle_base;
                        filler->spacing = m_support_material_flow.spacing();
                        filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / support_density));
                        fill_expolygons_generate_paths(
                            // Destination
                            support_layer.support_fills.entities, 
                            // Regions to fill
                            STDMOVE(to_infill), 
                            // Filler and its parameters
                            filler, float(support_density),
                            // Extrusion parameters
                            erSupportMaterial, flow);
                    }
                }
            }

            Fill *filler = filler_interface.get();
            Flow  flow = m_first_layer_flow;
            float density = 0.f;
            if (support_layer_id == 0) {
                // Base flange.
                filler->angle = raft_angle_1st_layer;
                filler->spacing = m_first_layer_flow.spacing();
                // 70% of density on the 1st layer.
                density       = 0.7f;
            } else if (support_layer_id >= m_slicing_params.base_raft_layers) {
                filler->angle = raft_angle_interface;
                // We don't use $base_flow->spacing because we need a constant spacing
                // value that guarantees that all layers are correctly aligned.
                filler->spacing = m_support_material_flow.spacing();
                flow          = Flow(float(m_support_material_interface_flow.width), float(raft_layer.height), m_support_material_flow.nozzle_diameter, raft_layer.bridging);
                density       = float(interface_density);
            } else
                continue;
            filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / density));
            fill_expolygons_generate_paths(
                // Destination
                support_layer.support_fills.entities, 
                // Regions to fill
                offset2_ex(raft_layer.polygons, float(SCALED_EPSILON), float(- SCALED_EPSILON)),
                // Filler and its parameters
                filler, density,
                // Extrusion parameters
                (support_layer_id < m_slicing_params.base_raft_layers) ? erSupportMaterial : erSupportMaterialInterface, flow);
        }
    });

    struct LayerCacheItem {
        LayerCacheItem(MyLayerExtruded *layer_extruded = nullptr) : layer_extruded(layer_extruded) {}
        MyLayerExtruded         *layer_extruded;
        std::vector<MyLayer*>    overlapping;
    };
    struct LayerCache {
        MyLayerExtruded                 bottom_contact_layer;
        MyLayerExtruded                 top_contact_layer;
        MyLayerExtruded                 base_layer;
        MyLayerExtruded                 interface_layer;
        std::vector<LayerCacheItem>     overlaps;
    };
    std::vector<LayerCache>             layer_caches(object.support_layers.size(), LayerCache());

    tbb::parallel_for(tbb::blocked_range<size_t>(n_raft_layers, object.support_layers.size()),
        [this, &object, &bottom_contacts, &top_contacts, &intermediate_layers, &interface_layers, &layer_caches, &loop_interface_processor, 
            infill_pattern, &bbox_object, support_density, interface_density, interface_angle, &angles, link_max_length_factor, with_sheath]
            (const tbb::blocked_range<size_t>& range) {
        // Indices of the 1st layer in their respective container at the support layer height.
        size_t idx_layer_bottom_contact   = size_t(-1);
        size_t idx_layer_top_contact      = size_t(-1);
        size_t idx_layer_intermediate     = size_t(-1);
        size_t idx_layer_inteface         = size_t(-1);
        std::unique_ptr<Fill> filler_interface = std::unique_ptr<Fill>(Fill::new_from_type(m_slicing_params.soluble_interface ? ipConcentric : ipRectilinear));
        std::unique_ptr<Fill> filler_support   = std::unique_ptr<Fill>(Fill::new_from_type(infill_pattern));
        filler_interface->set_bounding_box(bbox_object);
        filler_support->set_bounding_box(bbox_object);
        for (size_t support_layer_id = range.begin(); support_layer_id < range.end(); ++ support_layer_id)
        {
            SupportLayer &support_layer = *object.support_layers[support_layer_id];
            LayerCache   &layer_cache   = layer_caches[support_layer_id];

            // Find polygons with the same print_z.
            MyLayerExtruded &bottom_contact_layer = layer_cache.bottom_contact_layer;
            MyLayerExtruded &top_contact_layer    = layer_cache.top_contact_layer;
            MyLayerExtruded &base_layer           = layer_cache.base_layer;
            MyLayerExtruded &interface_layer      = layer_cache.interface_layer;
            // Increment the layer indices to find a layer at support_layer.print_z.
            {
                auto fun = [&support_layer](const MyLayer *l){ return l->print_z >= support_layer.print_z - EPSILON; };
                idx_layer_bottom_contact  = idx_higher_or_equal(bottom_contacts,     idx_layer_bottom_contact,  fun);
                idx_layer_top_contact     = idx_higher_or_equal(top_contacts,        idx_layer_top_contact,     fun);
                idx_layer_intermediate    = idx_higher_or_equal(intermediate_layers, idx_layer_intermediate,    fun);
                idx_layer_inteface        = idx_higher_or_equal(interface_layers,    idx_layer_inteface,        fun);
            }
            // Copy polygons from the layers.
            if (idx_layer_bottom_contact < bottom_contacts.size() && bottom_contacts[idx_layer_bottom_contact]->print_z < support_layer.print_z + EPSILON)
                bottom_contact_layer.layer = bottom_contacts[idx_layer_bottom_contact];
            if (idx_layer_top_contact < top_contacts.size() && top_contacts[idx_layer_top_contact]->print_z < support_layer.print_z + EPSILON)
                top_contact_layer.layer = top_contacts[idx_layer_top_contact];
            if (idx_layer_inteface < interface_layers.size() && interface_layers[idx_layer_inteface]->print_z < support_layer.print_z + EPSILON)
                interface_layer.layer = interface_layers[idx_layer_inteface];
            if (idx_layer_intermediate < intermediate_layers.size() && intermediate_layers[idx_layer_intermediate]->print_z < support_layer.print_z + EPSILON)
                base_layer.layer = intermediate_layers[idx_layer_intermediate];

            if (m_object_config->support_material_interface_layers == 0) {
                // If no interface layers were requested, we treat the contact layer exactly as a generic base layer.
                if (m_can_merge_support_regions) {
                    if (base_layer.could_merge(top_contact_layer)) 
                        base_layer.merge(std::move(top_contact_layer));
                    else if (base_layer.empty() && !top_contact_layer.empty() && !top_contact_layer.layer->bridging)
                        std::swap(base_layer, top_contact_layer);
                    if (base_layer.could_merge(bottom_contact_layer))
                        base_layer.merge(std::move(bottom_contact_layer));
                    else if (base_layer.empty() && !bottom_contact_layer.empty() && !bottom_contact_layer.layer->bridging)
                        std::swap(base_layer, bottom_contact_layer);
                }
            } else {
                loop_interface_processor.generate(top_contact_layer, m_support_material_interface_flow);
                // If no loops are allowed, we treat the contact layer exactly as a generic interface layer.
                // Merge interface_layer into top_contact_layer, as the top_contact_layer is not synchronized and therefore it will be used
                // to trim other layers.
                if (top_contact_layer.could_merge(interface_layer))
                    top_contact_layer.merge(std::move(interface_layer));
            } 

            if (! interface_layer.empty() && ! base_layer.empty()) {
                // turn base support into interface when it's contained in our holes
                // (this way we get wider interface anchoring)
                //FIXME one wants to fill in the inner most holes of the interfaces, not all the holes.
                Polygons islands = top_level_islands(interface_layer.layer->polygons);
                polygons_append(interface_layer.layer->polygons, intersection(base_layer.layer->polygons, islands));
                base_layer.layer->polygons = diff(base_layer.layer->polygons, islands);
            }

            // Top and bottom contacts, interface layers.
            for (size_t i = 0; i < 3; ++ i) {
                MyLayerExtruded &layer_ex = (i == 0) ? top_contact_layer : (i == 1 ? bottom_contact_layer : interface_layer);
                if (layer_ex.empty() || layer_ex.polygons_to_extrude().empty())
                    continue;
                //FIXME When paralellizing, each thread shall have its own copy of the fillers.
                bool interface_as_base = (&layer_ex == &interface_layer) && m_object_config->support_material_interface_layers.value == 0;
                Flow interface_flow(
                    float(layer_ex.layer->bridging ? layer_ex.layer->height : (interface_as_base ? m_support_material_flow.width : m_support_material_interface_flow.width)),
                    float(layer_ex.layer->height),
                    m_support_material_interface_flow.nozzle_diameter,
                    layer_ex.layer->bridging);
                filler_interface->angle = interface_as_base ?
                        // If zero interface layers are configured, use the same angle as for the base layers.
                        angles[support_layer_id % angles.size()] :
                        // Use interface angle for the interface layers.
                        interface_angle;
                filler_interface->spacing = m_support_material_interface_flow.spacing();
                filler_interface->link_max_length = coord_t(scale_(filler_interface->spacing * link_max_length_factor / interface_density));
                fill_expolygons_generate_paths(
                    // Destination
                    layer_ex.extrusions, 
                    // Regions to fill
                    union_ex(layer_ex.polygons_to_extrude(), true),
                    // Filler and its parameters
                    filler_interface.get(), float(interface_density),
                    // Extrusion parameters
                    erSupportMaterialInterface, interface_flow);
            }

            // Base support or flange.
            if (! base_layer.empty() && ! base_layer.polygons_to_extrude().empty()) {
                //FIXME When paralellizing, each thread shall have its own copy of the fillers.
                Fill *filler = filler_support.get();
                filler->angle = angles[support_layer_id % angles.size()];
                // We don't use $base_flow->spacing because we need a constant spacing
                // value that guarantees that all layers are correctly aligned.
                Flow flow(
                    float(base_layer.layer->bridging ? base_layer.layer->height : m_support_material_flow.width), 
                    float(base_layer.layer->height), 
                    m_support_material_flow.nozzle_diameter, 
                    base_layer.layer->bridging);
                filler->spacing = m_support_material_flow.spacing();
                filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / support_density));
                float density = float(support_density);
                // find centerline of the external loop/extrusions
                ExPolygons to_infill = (support_layer_id == 0 || ! with_sheath) ?
                    // union_ex(base_polygons, true) :
                    offset2_ex(base_layer.polygons_to_extrude(), float(SCALED_EPSILON), float(- SCALED_EPSILON)) :
                    offset2_ex(base_layer.polygons_to_extrude(), float(SCALED_EPSILON), float(- SCALED_EPSILON - 0.5*flow.scaled_width()));
                if (base_layer.layer->bottom_z < EPSILON) {
                    // Base flange (the 1st layer).
                    filler = filler_interface.get();
                    filler->angle = Geometry::deg2rad(float(m_object_config->support_material_angle.value + 90.));
                    density = 0.5f;
                    flow = m_first_layer_flow;
                    // use the proper spacing for first layer as we don't need to align
                    // its pattern to the other layers
                    //FIXME When paralellizing, each thread shall have its own copy of the fillers.
                    filler->spacing = flow.spacing();
                    filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / density));
                } else if (with_sheath) {
                    // Draw a perimeter all around the support infill. This makes the support stable, but difficult to remove.
                    // TODO: use brim ordering algorithm
                    Polygons to_infill_polygons = to_polygons(to_infill);
                    // TODO: use offset2_ex()
                    to_infill = offset_ex(to_infill, - 0.4 * float(flow.scaled_spacing()));
                    extrusion_entities_append_paths(
                        base_layer.extrusions, 
                        to_polylines(STDMOVE(to_infill_polygons)),
                        erSupportMaterial, flow.mm3_per_mm(), flow.width, flow.height);
                }
                fill_expolygons_generate_paths(
                    // Destination
                    base_layer.extrusions, 
                    // Regions to fill
                    STDMOVE(to_infill), 
                    // Filler and its parameters
                    filler, density,
                    // Extrusion parameters
                    erSupportMaterial, flow);
            }

            layer_cache.overlaps.reserve(4);
            if (! bottom_contact_layer.empty())
                layer_cache.overlaps.push_back(&bottom_contact_layer);
            if (! top_contact_layer.empty())
                layer_cache.overlaps.push_back(&top_contact_layer);
            if (! interface_layer.empty())
                layer_cache.overlaps.push_back(&interface_layer);
            if (! base_layer.empty())
                layer_cache.overlaps.push_back(&base_layer);
            // Sort the layers with the same print_z coordinate by their heights, thickest first.
            std::sort(layer_cache.overlaps.begin(), layer_cache.overlaps.end(), [](const LayerCacheItem &lc1, const LayerCacheItem &lc2) { return lc1.layer_extruded->layer->height > lc2.layer_extruded->layer->height; });
            // Collect the support areas with this print_z into islands, as there is no need
            // for retraction over these islands.
            Polygons polys;
            // Collect the extrusions, sorted by the bottom extrusion height.
            for (LayerCacheItem &layer_cache_item : layer_cache.overlaps) {
                // Collect islands to polys.
                layer_cache_item.layer_extruded->polygons_append(polys);
                // The print_z of the top contact surfaces and bottom_z of the bottom contact surfaces are "free"
                // in a sense that they are not synchronized with other support layers. As the top and bottom contact surfaces
                // are inflated to achieve a better anchoring, it may happen, that these surfaces will at least partially
                // overlap in Z with another support layers, leading to over-extrusion.
                // Mitigate the over-extrusion by modulating the extrusion rate over these regions.
                // The print head will follow the same print_z, but the layer thickness will be reduced
                // where it overlaps with another support layer.
                //FIXME When printing a briging path, what is an equivalent height of the squished extrudate of the same width?
                // Collect overlapping top/bottom surfaces.
                layer_cache_item.overlapping.reserve(16);
                coordf_t bottom_z = layer_cache_item.layer_extruded->layer->bottom_print_z() + EPSILON;
                for (int i = int(idx_layer_bottom_contact) - 1; i >= 0 && bottom_contacts[i]->print_z > bottom_z; -- i)
                    layer_cache_item.overlapping.push_back(bottom_contacts[i]);
                for (int i = int(idx_layer_top_contact) - 1; i >= 0 && top_contacts[i]->print_z > bottom_z; -- i)
                    layer_cache_item.overlapping.push_back(top_contacts[i]);
                if (layer_cache_item.layer_extruded->layer->layer_type == sltBottomContact) {
                    // Bottom contact layer may overlap with a base layer, which may be changed to interface layer.
                    for (int i = int(idx_layer_intermediate) - 1; i >= 0 && intermediate_layers[i]->print_z > bottom_z; -- i)
                        layer_cache_item.overlapping.push_back(intermediate_layers[i]);
                    for (int i = int(idx_layer_inteface) - 1; i >= 0 && interface_layers[i]->print_z > bottom_z; -- i)
                        layer_cache_item.overlapping.push_back(interface_layers[i]);
                }
                std::sort(layer_cache_item.overlapping.begin(), layer_cache_item.overlapping.end(), MyLayersPtrCompare());
            }
            if (! polys.empty())
                expolygons_append(support_layer.support_islands.expolygons, union_ex(polys));
            /* {
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output("islands_" . $z . ".svg",
                    red_expolygons      => union_ex($contact),
                    green_expolygons    => union_ex($interface),
                    green_polylines     => [ map $_->unpack->polyline, @{$layer->support_contact_fills} ],
                    polylines           => [ map $_->unpack->polyline, @{$layer->support_fills} ],
                );
            } */
        } // for each support_layer_id
    });

    // Now modulate the support layer height in parallel.
    tbb::parallel_for(tbb::blocked_range<size_t>(n_raft_layers, object.support_layers.size()),
        [this, &object, &layer_caches]
            (const tbb::blocked_range<size_t>& range) {
        for (size_t support_layer_id = range.begin(); support_layer_id < range.end(); ++ support_layer_id) {
            SupportLayer &support_layer = *object.support_layers[support_layer_id];
            LayerCache   &layer_cache   = layer_caches[support_layer_id];
            for (LayerCacheItem &layer_cache_item : layer_cache.overlaps) {
                modulate_extrusion_by_overlapping_layers(layer_cache_item.layer_extruded->extrusions, *layer_cache_item.layer_extruded->layer, layer_cache_item.overlapping);
                support_layer.support_fills.append(std::move(layer_cache_item.layer_extruded->extrusions));
            }
        }
    });
}

/*
void PrintObjectSupportMaterial::clip_by_pillars(
    const PrintObject   &object,
    LayersPtr           &bottom_contacts,
    LayersPtr           &top_contacts,
    LayersPtr           &intermediate_contacts);

{
    // this prevents supplying an empty point set to BoundingBox constructor
    if (top_contacts.empty())
        return;

    coord_t pillar_size    = scale_(PILLAR_SIZE);
    coord_t pillar_spacing = scale_(PILLAR_SPACING);
    
    // A regular grid of pillars, filling the 2D bounding box.
    Polygons grid;
    {
        // Rectangle with a side of 2.5x2.5mm.
        Polygon pillar;
        pillar.points.push_back(Point(0, 0));
        pillar.points.push_back(Point(pillar_size, 0));
        pillar.points.push_back(Point(pillar_size, pillar_size));
        pillar.points.push_back(Point(0, pillar_size));
        
        // 2D bounding box of the projection of all contact polygons.
        BoundingBox bbox;
        for (LayersPtr::const_iterator it = top_contacts.begin(); it != top_contacts.end(); ++ it)
            bbox.merge(get_extents((*it)->polygons));
        grid.reserve(size_t(ceil(bb.size().x / pillar_spacing)) * size_t(ceil(bb.size().y / pillar_spacing)));
        for (coord_t x = bb.min.x; x <= bb.max.x - pillar_size; x += pillar_spacing) {
            for (coord_t y = bb.min.y; y <= bb.max.y - pillar_size; y += pillar_spacing) {
                grid.push_back(pillar);
                for (size_t i = 0; i < pillar.points.size(); ++ i)
                    grid.back().points[i].translate(Point(x, y));
            }
        }
    }
    
    // add pillars to every layer
    for my $i (0..n_support_z) {
        $shape->[$i] = [ @$grid ];
    }
    
    // build capitals
    for my $i (0..n_support_z) {
        my $z = $support_z->[$i];
        
        my $capitals = intersection(
            $grid,
            $contact->{$z} // [],
        );
        
        // work on one pillar at time (if any) to prevent the capitals from being merged
        // but store the contact area supported by the capital because we need to make 
        // sure nothing is left
        my $contact_supported_by_capitals = [];
        foreach my $capital (@$capitals) {
            // enlarge capital tops
            $capital = offset([$capital], +($pillar_spacing - $pillar_size)/2);
            push @$contact_supported_by_capitals, @$capital;
            
            for (my $j = $i-1; $j >= 0; $j--) {
                my $jz = $support_z->[$j];
                $capital = offset($capital, -$self->interface_flow->scaled_width/2);
                last if !@$capitals;
                push @{ $shape->[$j] }, @$capital;
            }
        }
        
        // Capitals will not generally cover the whole contact area because there will be
        // remainders. For now we handle this situation by projecting such unsupported
        // areas to the ground, just like we would do with a normal support.
        my $contact_not_supported_by_capitals = diff(
            $contact->{$z} // [],
            $contact_supported_by_capitals,
        );
        if (@$contact_not_supported_by_capitals) {
            for (my $j = $i-1; $j >= 0; $j--) {
                push @{ $shape->[$j] }, @$contact_not_supported_by_capitals;
            }
        }
    }
}

sub clip_with_shape {
    my ($self, $support, $shape) = @_;
    
    foreach my $i (keys %$support) {
        // don't clip bottom layer with shape so that we 
        // can generate a continuous base flange
        // also don't clip raft layers
        next if $i == 0;
        next if $i < $self->object_config->raft_layers;
        $support->{$i} = intersection(
            $support->{$i},
            $shape->[$i],
        );
    }
}
*/

} // namespace Slic3r
