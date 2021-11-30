#include "ClipperUtils.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "Layer.hpp"
#include "Print.hpp"
#include "SupportMaterial.hpp"
#include "Fill/FillBase.hpp"
#include "Geometry.hpp"
#include "Point.hpp"
#include "MutablePolygon.hpp"

#include <cmath>
#include <memory>
#include <boost/log/trivial.hpp>
#include <boost/container/static_vector.hpp>

#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>
#include <tbb/task_group.h>

#define SUPPORT_USE_AGG_RASTERIZER

#ifdef SUPPORT_USE_AGG_RASTERIZER
    #include <agg/agg_pixfmt_gray.h>
    #include <agg/agg_renderer_scanline.h>
    #include <agg/agg_scanline_p.h>
    #include <agg/agg_rasterizer_scanline_aa.h>
    #include <agg/agg_path_storage.h>
    #include "PNGReadWrite.hpp"
#else
    #include "EdgeGrid.hpp"
#endif // SUPPORT_USE_AGG_RASTERIZER

// #define SLIC3R_DEBUG

// Make assert active if SLIC3R_DEBUG
#ifdef SLIC3R_DEBUG
    #define DEBUG
    #define _DEBUG
    #undef NDEBUG
    #include "utils.hpp"
    #include "SVG.hpp"
#endif

// #undef NDEBUG
#include <cassert>

namespace Slic3r {

// how much we extend support around the actual contact area
//FIXME this should be dependent on the nozzle diameter!
#define SUPPORT_MATERIAL_MARGIN 1.5 

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
    coord_t pos_x0 = pos(0) + scale_(1.);
    coord_t pos_x = pos_x0;
    coord_t pos_y = pos(1) + scale_(1.5);
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
    pos_y = pos(1)+scale_(2.8);
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
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));
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
    Point legend_pos(bbox.min(0), bbox.max(1));
    bbox.merge(Point(std::max(bbox.min(0) + legend_size(0), bbox.max(0)), bbox.max(1) + legend_size(1)));
    SVG svg(path, bbox);
    const float transparency = 0.5f;
    for (int i = 0; i < n_layers; ++ i)
        svg.draw(union_ex(layers[i]->polygons), support_surface_type_to_color_name(layers[i]->layer_type), transparency);
    for (int i = 0; i < n_layers; ++ i)
        svg.draw(to_polylines(layers[i]->polygons), support_surface_type_to_color_name(layers[i]->layer_type));

    Polygons polygons_support, polygons_interface;
    support_layer.support_fills.polygons_covered_by_width(polygons_support, float(SCALED_EPSILON));
//    support_layer.support_interface_fills.polygons_covered_by_width(polygons_interface, SCALED_EPSILON);
    svg.draw(union_ex(polygons_support), "brown");
    svg.draw(union_ex(polygons_interface), "black");

    export_support_surface_type_legend_to_svg(svg, legend_pos);
    svg.Close();
}
#endif /* SLIC3R_DEBUG */

#ifdef SUPPORT_USE_AGG_RASTERIZER
static std::vector<unsigned char> rasterize_polygons(const Vec2i &grid_size, const double pixel_size, const Point &left_bottom, const Polygons &polygons)
{
    std::vector<unsigned char>                  data(grid_size.x() * grid_size.y());
    agg::rendering_buffer                       rendering_buffer(data.data(), unsigned(grid_size.x()), unsigned(grid_size.y()), grid_size.x());
    agg::pixfmt_gray8                           pixel_renderer(rendering_buffer);
    agg::renderer_base<agg::pixfmt_gray8>       raw_renderer(pixel_renderer);
    agg::renderer_scanline_aa_solid<agg::renderer_base<agg::pixfmt_gray8>> renderer(raw_renderer);
        
    renderer.color(agg::pixfmt_gray8::color_type(255));
    raw_renderer.clear(agg::pixfmt_gray8::color_type(0));

    agg::scanline_p8                            scanline;
    agg::rasterizer_scanline_aa<>               rasterizer;

    auto convert_pt = [left_bottom, pixel_size](const Point &pt) {
        return Vec2d((pt.x() - left_bottom.x()) / pixel_size, (pt.y() - left_bottom.y()) / pixel_size);
    };
    rasterizer.reset();
    for (const Polygon &polygon : polygons) {
        agg::path_storage path;
        auto it = polygon.points.begin();
        Vec2d pt_front = convert_pt(*it);
        path.move_to(pt_front.x(), pt_front.y());
        while (++ it != polygon.points.end()) {
            Vec2d pt = convert_pt(*it);
            path.line_to(pt.x(), pt.y());
        }
        path.line_to(pt_front.x(), pt_front.y());
        rasterizer.add_path(std::move(path));
    }
    agg::render_scanlines(rasterizer, scanline, renderer);
    return data;
}
// Grid has to have the boundary pixels unset.
static Polygons contours_simplified(const Vec2i &grid_size, const double pixel_size, Point left_bottom, const std::vector<unsigned char> &grid, coord_t offset, bool fill_holes)
{
    assert(std::abs(2 * offset) < pixel_size - 10);

    // Fill in empty cells, which have a left / right neighbor filled.
    // Fill in empty cells, which have the top / bottom neighbor filled.
    std::vector<unsigned char>        cell_inside_data;
    const std::vector<unsigned char> &cell_inside = fill_holes ? cell_inside_data : grid;
    if (fill_holes) {
        cell_inside_data = grid;
        for (int r = 1; r + 1 < grid_size.y(); ++ r) {
            for (int c = 1; c + 1 < grid_size.x(); ++ c) {
                int addr = r * grid_size.x() + c;
                if ((grid[addr - 1] != 0 && grid[addr + 1] != 0) ||
                    (grid[addr - grid_size.x()] != 0 && grid[addr + grid_size.x()] != 0))
                    cell_inside_data[addr] = true;
            }
        }
    }

    // 1) Collect the lines.
    std::vector<Line> lines;
    std::vector<std::pair<Point, int>> start_point_to_line_idx;
    for (int r = 1; r < grid_size.y(); ++ r) {
        for (int c = 1; c < grid_size.x(); ++ c) {
            int  addr    = r * grid_size.x() + c;
            bool left    = cell_inside[addr - 1] != 0;
            bool top     = cell_inside[addr - grid_size.x()] != 0;
            bool current = cell_inside[addr] != 0;
            if (left != current) {
                lines.push_back(
                    left ? 
                        Line(Point(c, r+1), Point(c, r  )) : 
                        Line(Point(c, r  ), Point(c, r+1)));
                start_point_to_line_idx.emplace_back(lines.back().a, int(lines.size()) - 1);
            }
            if (top != current) {
                lines.push_back(
                    top ? 
                        Line(Point(c  , r), Point(c+1, r)) :
                        Line(Point(c+1, r), Point(c  , r))); 
                start_point_to_line_idx.emplace_back(lines.back().a, int(lines.size()) - 1);
            }
        }
    }
    std::sort(start_point_to_line_idx.begin(), start_point_to_line_idx.end(), [](const auto &l, const auto &r){ return l.first < r.first; });

    // 2) Chain the lines.
    std::vector<char> line_processed(lines.size(), false);
    Polygons out;
    for (int i_candidate = 0; i_candidate < int(lines.size()); ++ i_candidate) {
        if (line_processed[i_candidate])
            continue;
        Polygon poly;
        line_processed[i_candidate] = true;
        poly.points.push_back(lines[i_candidate].b);
        int i_line_current = i_candidate;
        for (;;) {
            auto line_range = std::equal_range(std::begin(start_point_to_line_idx), std::end(start_point_to_line_idx), 
                std::make_pair(lines[i_line_current].b, 0), [](const auto& l, const auto& r) { return l.first < r.first; });
            // The interval has to be non empty, there shall be at least one line continuing the current one.
            assert(line_range.first != line_range.second);
            int i_next = -1;
            for (auto it = line_range.first; it != line_range.second; ++ it) {
                if (it->second == i_candidate) {
                    // closing the loop.
                    goto end_of_poly;
                }
                if (line_processed[it->second])
                    continue;
                if (i_next == -1) {
                    i_next = it->second;
                } else {
                    // This is a corner, where two lines meet exactly. Pick the line, which encloses a smallest angle with
                    // the current edge.
                    const Line &line_current = lines[i_line_current];
                    const Line &line_next = lines[it->second];
                    const Vector v1 = line_current.vector();
                    const Vector v2 = line_next.vector();
                    int64_t cross = int64_t(v1(0)) * int64_t(v2(1)) - int64_t(v2(0)) * int64_t(v1(1));
                    if (cross > 0) {
                        // This has to be a convex right angle. There is no better next line.
                        i_next = it->second;
                        break;
                    }
                }
            }
            line_processed[i_next] = true;
            i_line_current = i_next;
            poly.points.push_back(lines[i_line_current].b);
        }
    end_of_poly:
        out.push_back(std::move(poly));
    }

    // 3) Scale the polygons back into world, shrink slightly and remove collinear points.
    for (Polygon &poly : out) {
        for (Point &p : poly.points) {
#if 0
            p.x() = (p.x() + 1) * pixel_size + left_bottom.x();
            p.y() = (p.y() + 1) * pixel_size + left_bottom.y();
#else
            p *= pixel_size;
            p += left_bottom;
#endif
        }
        // Shrink the contour slightly, so if the same contour gets discretized and simplified again, one will get the same result.
        // Remove collinear points.
        Points pts;
        pts.reserve(poly.points.size());
        for (size_t j = 0; j < poly.points.size(); ++ j) {
            size_t j0 = (j == 0) ? poly.points.size() - 1 : j - 1;
            size_t j2 = (j + 1 == poly.points.size()) ? 0 : j + 1;
            Point  v  = poly.points[j2] - poly.points[j0];
            if (v(0) != 0 && v(1) != 0) {
                // This is a corner point. Copy it to the output contour.
                Point p = poly.points[j];
                p(1) += (v(0) < 0) ? - offset : offset;
                p(0) += (v(1) > 0) ? - offset : offset;
                pts.push_back(p);
            } 
        }
        poly.points = std::move(pts);
    }
    return out;
}
#endif // SUPPORT_USE_AGG_RASTERIZER

PrintObjectSupportMaterial::PrintObjectSupportMaterial(const PrintObject *object, const SlicingParameters &slicing_params) :
    m_object                (object),
    m_print_config          (&object->print()->config()),
    m_object_config         (&object->config()),
    m_slicing_params        (slicing_params)
{
    m_support_params.first_layer_flow                   = support_material_1st_layer_flow(object, float(slicing_params.first_print_layer_height));
    m_support_params.support_material_flow              = support_material_flow(object, float(slicing_params.layer_height));
    m_support_params.support_material_interface_flow    = support_material_interface_flow(object, float(slicing_params.layer_height));
    m_support_params.support_layer_height_min           = 0.01;

    // Calculate a minimum support layer height as a minimum over all extruders, but not smaller than 10um.
    m_support_params.support_layer_height_min = 1000000.;
    for (auto lh : m_print_config->min_layer_height.values)
        m_support_params.support_layer_height_min = std::min(m_support_params.support_layer_height_min, std::max(0.01, lh));

    if (m_object_config->support_material_interface_layers.value == 0) {
        // No interface layers allowed, print everything with the base support pattern.
        m_support_params.support_material_interface_flow = m_support_params.support_material_flow;
    }

    // Evaluate the XY gap between the object outer perimeters and the support structures.
    // Evaluate the XY gap between the object outer perimeters and the support structures.
    coordf_t external_perimeter_width = 0.;
    coordf_t bridge_flow_ratio = 0;
    for (size_t region_id = 0; region_id < object->num_printing_regions(); ++ region_id) {
        const PrintRegion &region = object->printing_region(region_id);
        external_perimeter_width = std::max(external_perimeter_width, coordf_t(region.flow(*object, frExternalPerimeter, slicing_params.layer_height).width()));
        bridge_flow_ratio += region.config().bridge_flow_ratio;
    }
    m_support_params.gap_xy = m_object_config->support_material_xy_spacing.get_abs_value(external_perimeter_width);
    bridge_flow_ratio /= object->num_printing_regions();

    m_support_params.support_material_bottom_interface_flow = m_slicing_params.soluble_interface || ! m_object_config->thick_bridges ?
        m_support_params.support_material_interface_flow.with_flow_ratio(bridge_flow_ratio) :
        Flow::bridging_flow(bridge_flow_ratio * m_support_params.support_material_interface_flow.nozzle_diameter(), m_support_params.support_material_interface_flow.nozzle_diameter());

    m_support_params.can_merge_support_regions = m_object_config->support_material_extruder.value == m_object_config->support_material_interface_extruder.value;
    if (!m_support_params.can_merge_support_regions && (m_object_config->support_material_extruder.value == 0 || m_object_config->support_material_interface_extruder.value == 0)) {
        // One of the support extruders is of "don't care" type.
        auto object_extruders = m_object->object_extruders();
        if (object_extruders.size() == 1 &&
            *object_extruders.begin() == std::max<unsigned int>(m_object_config->support_material_extruder.value, m_object_config->support_material_interface_extruder.value))
            // Object is printed with the same extruder as the support.
            m_support_params.can_merge_support_regions = true;
    }


    m_support_params.base_angle         = Geometry::deg2rad(float(m_object_config->support_material_angle.value));
    m_support_params.interface_angle    = Geometry::deg2rad(float(m_object_config->support_material_angle.value + 90.));
    m_support_params.interface_spacing  = m_object_config->support_material_interface_spacing.value + m_support_params.support_material_interface_flow.spacing();
    m_support_params.interface_density  = std::min(1., m_support_params.support_material_interface_flow.spacing() / m_support_params.interface_spacing);
    m_support_params.support_spacing    = m_object_config->support_material_spacing.value + m_support_params.support_material_flow.spacing();
    m_support_params.support_density    = std::min(1., m_support_params.support_material_flow.spacing() / m_support_params.support_spacing);
    if (m_object_config->support_material_interface_layers.value == 0) {
        // No interface layers allowed, print everything with the base support pattern.
        m_support_params.interface_spacing = m_support_params.support_spacing;
        m_support_params.interface_density = m_support_params.support_density;
    }

    SupportMaterialPattern  support_pattern = m_object_config->support_material_pattern;
    m_support_params.with_sheath            = m_object_config->support_material_with_sheath;
    m_support_params.base_fill_pattern      = 
        support_pattern == smpHoneycomb ? ipHoneycomb :
        m_support_params.support_density > 0.95 || m_support_params.with_sheath ? ipRectilinear : ipSupportBase;
    m_support_params.interface_fill_pattern = (m_support_params.interface_density > 0.95 ? ipRectilinear : ipSupportBase);
    m_support_params.contact_fill_pattern   =
        (m_object_config->support_material_interface_pattern == smipAuto && m_slicing_params.soluble_interface) ||
        m_object_config->support_material_interface_pattern == smipConcentric ?
        ipConcentric :
        (m_support_params.interface_density > 0.95 ? ipRectilinear : ipSupportBase);
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

// Support layer that is covered by some form of dense interface.
static constexpr const std::initializer_list<PrintObjectSupportMaterial::SupporLayerType> support_types_interface { 
    PrintObjectSupportMaterial::sltRaftInterface, PrintObjectSupportMaterial::sltBottomContact, PrintObjectSupportMaterial::sltBottomInterface, PrintObjectSupportMaterial::sltTopContact, PrintObjectSupportMaterial::sltTopInterface
};

void PrintObjectSupportMaterial::generate(PrintObject &object)
{
    BOOST_LOG_TRIVIAL(info) << "Support generator - Start";

    coordf_t max_object_layer_height = 0.;
    for (size_t i = 0; i < object.layer_count(); ++ i)
        max_object_layer_height = std::max(max_object_layer_height, object.layers()[i]->height);

    // Layer instances will be allocated by std::deque and they will be kept until the end of this function call.
    // The layers will be referenced by various LayersPtr (of type std::vector<Layer*>)
    MyLayerStorage layer_storage;

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating top contacts";

    // Per object layer projection of the object below the layer into print bed.
    std::vector<Polygons> buildplate_covered = this->buildplate_covered(object);

    // Determine the top contact surfaces of the support, defined as:
    // contact = overhangs - clearance + margin
    // This method is responsible for identifying what contact surfaces
    // should the support material expose to the object in order to guarantee
    // that it will be effective, regardless of how it's built below.
    // If raft is to be generated, the 1st top_contact layer will contain the 1st object layer silhouette without holes.
    MyLayersPtr top_contacts = this->top_contact_layers(object, buildplate_covered, layer_storage);
    if (top_contacts.empty())
        // Nothing is supported, no supports are generated.
        return;

#ifdef SLIC3R_DEBUG
    static int iRun = 0;
    iRun ++;
    for (const MyLayer *layer : top_contacts)
        Slic3r::SVG::export_expolygons(
            debug_out_path("support-top-contacts-%d-%lf.svg", iRun, layer->print_z), 
            union_ex(layer->polygons));
#endif /* SLIC3R_DEBUG */

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating bottom contacts";

    // Determine the bottom contact surfaces of the supports over the top surfaces of the object.
    // Depending on whether the support is soluble or not, the contact layer thickness is decided.
    // layer_support_areas contains the per object layer support areas. These per object layer support areas
    // may get merged and trimmed by this->generate_base_layers() if the support layers are not synchronized with object layers.
    std::vector<Polygons> layer_support_areas;
    MyLayersPtr bottom_contacts = this->bottom_contact_layers_and_layer_support_areas(
        object, top_contacts, buildplate_covered,
        layer_storage, layer_support_areas);

#ifdef SLIC3R_DEBUG
    for (size_t layer_id = 0; layer_id < object.layers().size(); ++ layer_id)
        Slic3r::SVG::export_expolygons(
            debug_out_path("support-areas-%d-%lf.svg", iRun, object.layers()[layer_id]->print_z), 
            union_ex(layer_support_areas[layer_id]));
#endif /* SLIC3R_DEBUG */

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating intermediate layers - indices";

    // Allocate empty layers between the top / bottom support contact layers
    // as placeholders for the base and intermediate support layers.
    // The layers may or may not be synchronized with the object layers, depending on the configuration.
    // For example, a single nozzle multi material printing will need to generate a waste tower, which in turn
    // wastes less material, if there are as little tool changes as possible.
    MyLayersPtr intermediate_layers = this->raft_and_intermediate_support_layers(
        object, bottom_contacts, top_contacts, layer_storage);

    this->trim_support_layers_by_object(object, top_contacts, m_slicing_params.gap_support_object, m_slicing_params.gap_object_support, m_support_params.gap_xy);

#ifdef SLIC3R_DEBUG
    for (const MyLayer *layer : top_contacts)
        Slic3r::SVG::export_expolygons(
            debug_out_path("support-top-contacts-trimmed-by-object-%d-%lf.svg", iRun, layer->print_z), 
            union_ex(layer->polygons));
#endif

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating base layers";

    // Fill in intermediate layers between the top / bottom support contact layers, trim them by the object.
    this->generate_base_layers(object, bottom_contacts, top_contacts, intermediate_layers, layer_support_areas);

#ifdef SLIC3R_DEBUG
    for (MyLayersPtr::const_iterator it = intermediate_layers.begin(); it != intermediate_layers.end(); ++ it)
        Slic3r::SVG::export_expolygons(
            debug_out_path("support-base-layers-%d-%lf.svg", iRun, (*it)->print_z), 
            union_ex((*it)->polygons));
#endif /* SLIC3R_DEBUG */

    BOOST_LOG_TRIVIAL(info) << "Support generator - Trimming top contacts by bottom contacts";

    // Because the top and bottom contacts are thick slabs, they may overlap causing over extrusion 
    // and unwanted strong bonds to the object.
    // Rather trim the top contacts by their overlapping bottom contacts to leave a gap instead of over extruding
    // top contacts over the bottom contacts.
    this->trim_top_contacts_by_bottom_contacts(object, bottom_contacts, top_contacts);


    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating interfaces";

    // Propagate top / bottom contact layers to generate interface layers 
    // and base interface layers (for soluble interface / non souble base only)
    auto [interface_layers, base_interface_layers] = this->generate_interface_layers(bottom_contacts, top_contacts, intermediate_layers, layer_storage);

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating raft";

    // If raft is to be generated, the 1st top_contact layer will contain the 1st object layer silhouette with holes filled.
    // There is also a 1st intermediate layer containing bases of support columns.
    // Inflate the bases of the support columns and create the raft base under the object.
    MyLayersPtr raft_layers = this->generate_raft_base(object, top_contacts, interface_layers, base_interface_layers, intermediate_layers, layer_storage);

#ifdef SLIC3R_DEBUG
    for (const MyLayer *l : interface_layers)
        Slic3r::SVG::export_expolygons(
            debug_out_path("support-interface-layers-%d-%lf.svg", iRun, l->print_z), 
            union_ex(l->polygons));
    for (const MyLayer *l : base_interface_layers)
        Slic3r::SVG::export_expolygons(
            debug_out_path("support-base-interface-layers-%d-%lf.svg", iRun, l->print_z), 
            union_ex(l->polygons));
#endif // SLIC3R_DEBUG

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
    layers_sorted.reserve(raft_layers.size() + bottom_contacts.size() + top_contacts.size() + intermediate_layers.size() + interface_layers.size() + base_interface_layers.size());
    layers_append(layers_sorted, raft_layers);
    layers_append(layers_sorted, bottom_contacts);
    layers_append(layers_sorted, top_contacts);
    layers_append(layers_sorted, intermediate_layers);
    layers_append(layers_sorted, interface_layers);
    layers_append(layers_sorted, base_interface_layers);
    // Sort the layers lexicographically by a raising print_z and a decreasing height.
    std::sort(layers_sorted.begin(), layers_sorted.end(), [](auto *l1, auto *l2) { return *l1 < *l2; });
    int layer_id = 0;
    int layer_id_interface = 0;
    assert(object.support_layers().empty());
    for (size_t i = 0; i < layers_sorted.size();) {
        // Find the last layer with roughly the same print_z, find the minimum layer height of all.
        // Due to the floating point inaccuracies, the print_z may not be the same even if in theory they should.
        size_t j = i + 1;
        coordf_t zmax = layers_sorted[i]->print_z + EPSILON;
        for (; j < layers_sorted.size() && layers_sorted[j]->print_z <= zmax; ++j) ;
        // Assign an average print_z to the set of layers with nearly equal print_z.
        coordf_t zavg = 0.5 * (layers_sorted[i]->print_z + layers_sorted[j - 1]->print_z);
        coordf_t height_min = layers_sorted[i]->height;
        bool     empty = true;
        // For snug supports, layers where the direction of the support interface shall change are accounted for.
        size_t   num_interfaces = 0;
        size_t   num_top_contacts = 0;
        double   top_contact_bottom_z = 0;
        for (size_t u = i; u < j; ++u) {
            MyLayer &layer = *layers_sorted[u];
            if (! layer.polygons.empty()) {
                empty             = false;
                num_interfaces   += one_of(layer.layer_type, support_types_interface);
                if (layer.layer_type == sltTopContact) {
                    ++ num_top_contacts;
                    assert(num_top_contacts <= 1);
                    // All top contact layers sharing this print_z shall also share bottom_z.
                    //assert(num_top_contacts == 1 || (top_contact_bottom_z - layer.bottom_z) < EPSILON);
                    top_contact_bottom_z = layer.bottom_z;
                }
            }
            layer.print_z = zavg;
            height_min = std::min(height_min, layer.height);
        }
        if (! empty) {
            // Here the upper_layer and lower_layer pointers are left to null at the support layers, 
            // as they are never used. These pointers are candidates for removal.
            bool   this_layer_contacts_only = num_top_contacts > 0 && num_top_contacts == num_interfaces;
            size_t this_layer_id_interface  = layer_id_interface;
            if (this_layer_contacts_only) {
                // Find a supporting layer for its interface ID.
                for (auto it = object.support_layers().rbegin(); it != object.support_layers().rend(); ++ it)
                    if (const SupportLayer &other_layer = **it; std::abs(other_layer.print_z - top_contact_bottom_z) < EPSILON) {
                        // other_layer supports this top contact layer. Assign a different support interface direction to this layer
                        // from the layer that supports it.
                        this_layer_id_interface = other_layer.interface_id() + 1;
                    }
            }
            object.add_support_layer(layer_id ++, this_layer_id_interface, height_min, zavg);
            if (num_interfaces && ! this_layer_contacts_only)
                ++ layer_id_interface;
        }
        i = j;
    }

    BOOST_LOG_TRIVIAL(info) << "Support generator - Generating tool paths";

#if 0 // #ifdef SLIC3R_DEBUG
    {
        size_t layer_id = 0;
        for (int i = 0; i < int(layers_sorted.size());) {
            // Find the last layer with roughly the same print_z, find the minimum layer height of all.
            // Due to the floating point inaccuracies, the print_z may not be the same even if in theory they should.
            int j = i + 1;
            coordf_t zmax = layers_sorted[i]->print_z + EPSILON;
            bool empty = true;
            for (; j < layers_sorted.size() && layers_sorted[j]->print_z <= zmax; ++j)
                if (!layers_sorted[j]->polygons.empty())
                    empty = false;
            if (!empty) {
                export_print_z_polygons_to_svg(
                    debug_out_path("support-%d-%lf-before.svg", iRun, layers_sorted[i]->print_z).c_str(),
                    layers_sorted.data() + i, j - i);
                export_print_z_polygons_and_extrusions_to_svg(
                    debug_out_path("support-w-fills-%d-%lf-before.svg", iRun, layers_sorted[i]->print_z).c_str(),
                    layers_sorted.data() + i, j - i,
                    *object.support_layers()[layer_id]);
                ++layer_id;
            }
            i = j;
        }
    }
#endif /* SLIC3R_DEBUG */

    // Generate the actual toolpaths and save them into each layer.
    this->generate_toolpaths(object.support_layers(), raft_layers, bottom_contacts, top_contacts, intermediate_layers, interface_layers, base_interface_layers);

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
                    *object.support_layers()[layer_id]);
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
    for (const LayerRegion *region : layer.regions())
        for (const Surface &surface : region->slices.surfaces)
            if (surface.surface_type == surface_type)
                n_polygons_new += surface.expolygon.holes.size() + 1;
    // 2) Collect the new polygons.
    Polygons out;
    out.reserve(n_polygons_new);
    for (const LayerRegion *region : layer.regions())
        for (const Surface &surface : region->slices.surfaces)
            if (surface.surface_type == surface_type)
                polygons_append(out, surface.expolygon);
    return out;
}

// Collect outer contours of all slices of this layer.
// This is useful for calculating the support base with holes filled.
Polygons collect_slices_outer(const Layer &layer)
{
    Polygons out;
    out.reserve(out.size() + layer.lslices.size());
    for (const ExPolygon &expoly : layer.lslices)
        out.emplace_back(expoly.contour);
    return out;
}

struct SupportGridParams {
    SupportGridParams(const PrintObjectConfig &object_config, const Flow &support_material_flow) :
        style(object_config.support_material_style.value),
        grid_resolution(object_config.support_material_spacing.value + support_material_flow.spacing()),
        support_angle(Geometry::deg2rad(object_config.support_material_angle.value)),
        extrusion_width(support_material_flow.spacing()),
        support_material_closing_radius(object_config.support_material_closing_radius.value),
        expansion_to_slice(coord_t(support_material_flow.scaled_spacing() / 2 + 5)),
        expansion_to_propagate(-3) {}

    SupportMaterialStyle    style;
    double                  grid_resolution;
    double                  support_angle;
    double                  extrusion_width;
    double                  support_material_closing_radius;
    coord_t                 expansion_to_slice;
    coord_t                 expansion_to_propagate;
};

class SupportGridPattern
{
public:
    SupportGridPattern(
        // Support islands, to be stretched into a grid. Already trimmed with min(lower_layer_offset, m_gap_xy)
        const Polygons          *support_polygons, 
        // Trimming polygons, to trim the stretched support islands. support_polygons were already trimmed with trimming_polygons.
        const Polygons          *trimming_polygons,
        const SupportGridParams &params) :
        m_style(params.style),
        m_support_polygons(support_polygons), m_trimming_polygons(trimming_polygons),
        m_support_spacing(params.grid_resolution), m_support_angle(params.support_angle),
        m_extrusion_width(params.extrusion_width),
        m_support_material_closing_radius(params.support_material_closing_radius)
    {
        switch (m_style) {
        case smsGrid:
        {
            // Prepare the grid data, it will be reused when extracting support structures.
            if (m_support_angle != 0.) {
                // Create a copy of the rotated contours.
                m_support_polygons_rotated  = *support_polygons;
                m_trimming_polygons_rotated = *trimming_polygons;
                m_support_polygons  = &m_support_polygons_rotated;
                m_trimming_polygons = &m_trimming_polygons_rotated;
                polygons_rotate(m_support_polygons_rotated, - params.support_angle);
                polygons_rotate(m_trimming_polygons_rotated, - params.support_angle);
            }

            // Resolution of the sparse support grid.
            coord_t grid_resolution = coord_t(scale_(m_support_spacing));
            BoundingBox bbox = get_extents(*m_support_polygons);
            bbox.offset(20);
            // Align the bounding box with the sparse support grid.
            bbox.align_to_grid(grid_resolution);

    #ifdef SUPPORT_USE_AGG_RASTERIZER
            m_bbox       = bbox;
            // Oversample the grid to avoid leaking of supports through or around the object walls.
            int extrusion_width_scaled = scale_(params.extrusion_width);
            int oversampling = std::clamp(int(scale_(m_support_spacing) / (extrusion_width_scaled + 100)), 1, 8);
            m_pixel_size = std::max<double>(extrusion_width_scaled + 21, scale_(m_support_spacing / oversampling));
            // Add one empty column / row boundaries.
            m_bbox.offset(m_pixel_size);
            // Grid size fitting the support polygons plus one pixel boundary around the polygons.
            Vec2i grid_size_raw(int(ceil((m_bbox.max.x() - m_bbox.min.x()) / m_pixel_size)),
                                int(ceil((m_bbox.max.y() - m_bbox.min.y()) / m_pixel_size)));
            // Overlay macro blocks of (oversampling x oversampling) over the grid.
            Vec2i grid_blocks((grid_size_raw.x() + oversampling - 1 - 2) / oversampling, 
                              (grid_size_raw.y() + oversampling - 1 - 2) / oversampling);
            // and resize the grid to fit the macro blocks + one pixel boundary.
            m_grid_size = grid_blocks * oversampling + Vec2i(2, 2);
            assert(m_grid_size.x() >= grid_size_raw.x());
            assert(m_grid_size.y() >= grid_size_raw.y());
            m_grid2 = rasterize_polygons(m_grid_size, m_pixel_size, m_bbox.min, *m_support_polygons);

            seed_fill_block(m_grid2, m_grid_size,
                dilate_trimming_region(rasterize_polygons(m_grid_size, m_pixel_size, m_bbox.min, *m_trimming_polygons), m_grid_size),
                grid_blocks, oversampling);

    #ifdef SLIC3R_DEBUG
            {
                static int irun;
                Slic3r::png::write_gray_to_file_scaled(debug_out_path("support-rasterizer-%d.png", irun++), m_grid_size.x(), m_grid_size.y(), m_grid2.data(), 4);
            }
    #endif // SLIC3R_DEBUG

    #else // SUPPORT_USE_AGG_RASTERIZER
            // Create an EdgeGrid, initialize it with projection, initialize signed distance field.
            m_grid.set_bbox(bbox);
            m_grid.create(*m_support_polygons, grid_resolution);
    #if 0
            if (m_grid.has_intersecting_edges()) {
                // EdgeGrid fails to produce valid signed distance function for self-intersecting polygons.
                m_support_polygons_rotated = simplify_polygons(*m_support_polygons);
                m_support_polygons = &m_support_polygons_rotated;
                m_grid.set_bbox(bbox);
                m_grid.create(*m_support_polygons, grid_resolution);
    //            assert(! m_grid.has_intersecting_edges());
                printf("SupportGridPattern: fixing polygons with intersection %s\n",
                    m_grid.has_intersecting_edges() ? "FAILED" : "SUCCEEDED");
            }
    #endif
            m_grid.calculate_sdf();
    #endif // SUPPORT_USE_AGG_RASTERIZER
            break;
        }

        case smsSnug:
        default:
            // nothing to prepare
            break;
        }
    }

    // Extract polygons from the grid, offsetted by offset_in_grid,
    // and trim the extracted polygons by trimming_polygons.
    // Trimming by the trimming_polygons may split the extracted polygons into pieces.
    // Remove all the pieces, which do not contain any of the island_samples.
    Polygons extract_support(const coord_t offset_in_grid, bool fill_holes
#ifdef SLIC3R_DEBUG
        , const char *step_name, int iRun, size_t layer_id, double print_z
#endif
        )
    {
        switch (m_style) {
        case smsGrid:
        {
    #ifdef SUPPORT_USE_AGG_RASTERIZER
            Polygons support_polygons_simplified = contours_simplified(m_grid_size, m_pixel_size, m_bbox.min, m_grid2, offset_in_grid, fill_holes);
    #else // SUPPORT_USE_AGG_RASTERIZER
            // Generate islands, so each island may be tested for overlap with island_samples.
            assert(std::abs(2 * offset_in_grid) < m_grid.resolution());
            Polygons support_polygons_simplified = m_grid.contours_simplified(offset_in_grid, fill_holes);
    #endif // SUPPORT_USE_AGG_RASTERIZER

            ExPolygons islands = diff_ex(support_polygons_simplified, *m_trimming_polygons);

            // Extract polygons, which contain some of the island_samples.
            Polygons out;

            // Sample a single point per input support polygon, keep it as a reference to maintain corresponding
            // polygons if ever these polygons get split into parts by the trimming polygons.
            // As offset_in_grid may be negative, m_support_polygons may stick slightly outside of islands.
            // Trim ti with islands.
            Points samples = island_samples(
                offset_in_grid > 0 ? 
                    // Expanding, thus m_support_polygons are all inside islands.
                    union_ex(*m_support_polygons) :
                    // Shrinking, thus m_support_polygons may be trimmed a tiny bit by islands.
                    intersection_ex(*m_support_polygons, islands));

            std::vector<std::pair<Point,bool>> samples_inside;
            for (ExPolygon &island : islands) {
                BoundingBox bbox = get_extents(island.contour);
                // Samples are sorted lexicographically.
                auto it_lower = std::lower_bound(samples.begin(), samples.end(), Point(bbox.min - Point(1, 1)));
                auto it_upper = std::upper_bound(samples.begin(), samples.end(), Point(bbox.max + Point(1, 1)));
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
                            //FIXME this test is not numerically robust. Particularly, it does not handle horizontal segments at y == point(1) well.
                            // Does the ray with y == point(1) intersect this line segment?
                            for (auto &sample_inside : samples_inside) {
                                if (((*i)(1) > sample_inside.first(1)) != ((*j)(1) > sample_inside.first(1))) {
                                    double x1 = (double)sample_inside.first(0);
                                    double x2 = (double)(*i)(0) + (double)((*j)(0) - (*i)(0)) * (double)(sample_inside.first(1) - (*i)(1)) / (double)((*j)(1) - (*i)(1));
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
            BoundingBox bbox = get_extents(*m_trimming_polygons);
            if (! islands.empty())
                bbox.merge(get_extents(islands));
            if (!out.empty())
                bbox.merge(get_extents(out));
            if (!support_polygons_simplified.empty())
                bbox.merge(get_extents(support_polygons_simplified));
            SVG svg(debug_out_path("extract_support_from_grid_trimmed-%s-%d-%d-%lf.svg", step_name, iRun, layer_id, print_z).c_str(), bbox);
            svg.draw(union_ex(support_polygons_simplified), "gray", 0.25f);
            svg.draw(islands, "red", 0.5f);
            svg.draw(union_ex(out), "green", 0.5f);
            svg.draw(union_ex(*m_support_polygons), "blue", 0.5f);
            svg.draw_outline(islands, "red", "red", scale_(0.05));
            svg.draw_outline(union_ex(out), "green", "green", scale_(0.05));
            svg.draw_outline(union_ex(*m_support_polygons), "blue", "blue", scale_(0.05));
            for (const Point &pt : samples)
                svg.draw(pt, "black", coord_t(scale_(0.15)));
            svg.Close();
    #endif /* SLIC3R_DEBUG */

            if (m_support_angle != 0.)
                polygons_rotate(out, m_support_angle);
            return out;
        }
        case smsSnug:
            // Merge the support polygons by applying morphological closing and inwards smoothing.
            auto closing_distance   = scaled<float>(m_support_material_closing_radius);
            auto smoothing_distance = scaled<float>(m_extrusion_width);
#ifdef SLIC3R_DEBUG
            SVG::export_expolygons(debug_out_path("extract_support_from_grid_trimmed-%s-%d-%d-%lf.svg", step_name, iRun, layer_id, print_z),
                { { { diff_ex(expand(*m_support_polygons, closing_distance), closing(*m_support_polygons, closing_distance, SUPPORT_SURFACES_OFFSET_PARAMETERS)) }, { "closed", "blue",   0.5f } },
                  { { union_ex(smooth_outward(closing(*m_support_polygons, closing_distance, SUPPORT_SURFACES_OFFSET_PARAMETERS), smoothing_distance)) },           { "regularized", "red", "black", "", scaled<coord_t>(0.1f), 0.5f } },
                  { { union_ex(*m_support_polygons) },                                                                                                              { "src",   "green",  0.5f } },
                });
#endif /* SLIC3R_DEBUG */
            //FIXME do we want to trim with the object here? On one side the columns will be thinner, on the other side support interfaces may disappear for snug supports.
            // return diff(smooth_outward(closing(*m_support_polygons, closing_distance, SUPPORT_SURFACES_OFFSET_PARAMETERS), smoothing_distance), *m_trimming_polygons);
            return smooth_outward(closing(*m_support_polygons, closing_distance, SUPPORT_SURFACES_OFFSET_PARAMETERS), smoothing_distance);
        }
        assert(false);
        return Polygons();
    }

#if defined(SLIC3R_DEBUG) && ! defined(SUPPORT_USE_AGG_RASTERIZER)
    void serialize(const std::string &path)
    {
        FILE *file = ::fopen(path.c_str(), "wb");
        ::fwrite(&m_support_spacing, 8, 1, file);
        ::fwrite(&m_support_angle, 8, 1, file);
        uint32_t n_polygons = m_support_polygons->size();
        ::fwrite(&n_polygons, 4, 1, file);
        for (uint32_t i = 0; i < n_polygons; ++ i) {
            const Polygon &poly = (*m_support_polygons)[i];
            uint32_t n_points = poly.size();
            ::fwrite(&n_points, 4, 1, file);
            for (uint32_t j = 0; j < n_points; ++ j) {
                const Point &pt = poly.points[j];
                ::fwrite(&pt.x(), sizeof(coord_t), 1, file);
                ::fwrite(&pt.y(), sizeof(coord_t), 1, file);
            }
        }
        n_polygons = m_trimming_polygons->size();
        ::fwrite(&n_polygons, 4, 1, file);
        for (uint32_t i = 0; i < n_polygons; ++ i) {
            const Polygon &poly = (*m_trimming_polygons)[i];
            uint32_t n_points = poly.size();
            ::fwrite(&n_points, 4, 1, file);
            for (uint32_t j = 0; j < n_points; ++ j) {
                const Point &pt = poly.points[j];
                ::fwrite(&pt.x(), sizeof(coord_t), 1, file);
                ::fwrite(&pt.y(), sizeof(coord_t), 1, file);
            }
        }
        ::fclose(file);
    }

    static SupportGridPattern deserialize(const std::string &path, int which = -1)
    {
        SupportGridPattern out;
        out.deserialize_(path, which);
        return out;
    }

    // Deserialization constructor
    bool deserialize_(const std::string &path, int which = -1)
    {
        FILE *file = ::fopen(path.c_str(), "rb");
        if (file == nullptr)
            return false;

        m_support_polygons = &m_support_polygons_deserialized;
        m_trimming_polygons = &m_trimming_polygons_deserialized;

        ::fread(&m_support_spacing, 8, 1, file);
        ::fread(&m_support_angle, 8, 1, file);
        //FIXME
        //m_support_spacing *= 0.01 / 2;
        uint32_t n_polygons;
        ::fread(&n_polygons, 4, 1, file);
        m_support_polygons_deserialized.reserve(n_polygons);
        int32_t scale = 1;
        for (uint32_t i = 0; i < n_polygons; ++ i) {
            Polygon poly;
            uint32_t n_points;
            ::fread(&n_points, 4, 1, file);
            poly.points.reserve(n_points);
            for (uint32_t j = 0; j < n_points; ++ j) {
                coord_t x, y;
                ::fread(&x, sizeof(coord_t), 1, file);
                ::fread(&y, sizeof(coord_t), 1, file);
                poly.points.emplace_back(Point(x * scale, y * scale));
            }
            if (which == -1 || which == i)
                m_support_polygons_deserialized.emplace_back(std::move(poly));
            printf("Polygon %d, area: %lf\n", i, area(poly.points));
        }
        ::fread(&n_polygons, 4, 1, file);
        m_trimming_polygons_deserialized.reserve(n_polygons);
        for (uint32_t i = 0; i < n_polygons; ++ i) {
            Polygon poly;
            uint32_t n_points;
            ::fread(&n_points, 4, 1, file);
            poly.points.reserve(n_points);
            for (uint32_t j = 0; j < n_points; ++ j) {
                coord_t x, y;
                ::fread(&x, sizeof(coord_t), 1, file);
                ::fread(&y, sizeof(coord_t), 1, file);
                poly.points.emplace_back(Point(x * scale, y * scale));
            }
            m_trimming_polygons_deserialized.emplace_back(std::move(poly));
        }
        ::fclose(file);

        m_support_polygons_deserialized = simplify_polygons(m_support_polygons_deserialized, false);
        //m_support_polygons_deserialized = to_polygons(union_ex(m_support_polygons_deserialized, false));

        // Create an EdgeGrid, initialize it with projection, initialize signed distance field.
        coord_t grid_resolution = coord_t(scale_(m_support_spacing));
        BoundingBox bbox = get_extents(*m_support_polygons);
        bbox.offset(20);
        bbox.align_to_grid(grid_resolution);
        m_grid.set_bbox(bbox);
        m_grid.create(*m_support_polygons, grid_resolution);
        m_grid.calculate_sdf();
        return true;
    }

    const Polygons& support_polygons() const { return *m_support_polygons; }
    const Polygons& trimming_polygons() const { return *m_trimming_polygons; }
    const EdgeGrid::Grid& grid() const { return m_grid; }

#endif // defined(SLIC3R_DEBUG) && ! defined(SUPPORT_USE_AGG_RASTERIZER)

private:
    SupportGridPattern() {}
    SupportGridPattern& operator=(const SupportGridPattern &rhs);

#ifdef SUPPORT_USE_AGG_RASTERIZER
    // Dilate the trimming region (unmask the boundary pixels).
    static std::vector<unsigned char> dilate_trimming_region(const std::vector<unsigned char> &trimming, const Vec2i &grid_size)
    {
        std::vector<unsigned char> dilated(trimming.size(), 0);
        for (int r = 1; r + 1 < grid_size.y(); ++ r)
            for (int c = 1; c + 1 < grid_size.x(); ++ c) {
                //int addr = c + r * m_grid_size.x();
                // 4-neighborhood is not sufficient.
                // dilated[addr] = trimming[addr] != 0 && trimming[addr - 1] != 0 && trimming[addr + 1] != 0 && trimming[addr - m_grid_size.x()] != 0 && trimming[addr + m_grid_size.x()] != 0;
                // 8-neighborhood
                int addr = c + (r - 1) * grid_size.x();
                bool b = trimming[addr - 1] != 0 && trimming[addr] != 0 && trimming[addr + 1] != 0;
                addr += grid_size.x();
                b = b && trimming[addr - 1] != 0 && trimming[addr] != 0 && trimming[addr + 1] != 0;
                addr += grid_size.x();
                b = b && trimming[addr - 1] != 0 && trimming[addr] != 0 && trimming[addr + 1] != 0;
                dilated[addr - grid_size.x()] = b;
            }
        return dilated;
    }

    // Seed fill each of the (oversampling x oversampling) block up to the dilated trimming region.
    static void seed_fill_block(std::vector<unsigned char> &grid, Vec2i grid_size, const std::vector<unsigned char> &trimming,const Vec2i &grid_blocks, int oversampling)
    {
        int size      = oversampling;
        int stride    = grid_size.x();
        for (int block_r = 0; block_r < grid_blocks.y(); ++ block_r)
            for (int block_c = 0; block_c < grid_blocks.x(); ++ block_c) {
                // Propagate the support pixels over the macro cell up to the trimming mask.
                int                  addr      = block_c * size + 1 + (block_r * size + 1) * stride;
                unsigned char       *grid_data = grid.data() + addr;
                const unsigned char *mask_data = trimming.data() + addr;
                // Top to bottom propagation.
                #define PROPAGATION_STEP(offset) \
                    do { \
                        int addr = r * stride + c; \
                        int addr2 = addr + offset; \
                        if (grid_data[addr2] && ! mask_data[addr] && ! mask_data[addr2]) \
                            grid_data[addr] = 1; \
                    } while (0);
                for (int r = 0; r < size; ++ r) {
                    if (r > 0)
                        for (int c = 0; c < size; ++ c)
                            PROPAGATION_STEP(- stride);
                    for (int c = 1; c < size; ++ c)
                        PROPAGATION_STEP(- 1);
                    for (int c = size - 2; c >= 0; -- c)
                        PROPAGATION_STEP(+ 1);
                }
                // Bottom to top propagation.
                for (int r = size - 2; r >= 0; -- r) {
                    for (int c = 0; c < size; ++ c)
                        PROPAGATION_STEP(+ stride);
                    for (int c = 1; c < size; ++ c)
                        PROPAGATION_STEP(- 1);
                    for (int c = size - 2; c >= 0; -- c)
                        PROPAGATION_STEP(+ 1);
                }
                #undef PROPAGATION_STEP
            }
    }
#endif // SUPPORT_USE_AGG_RASTERIZER

#if 0
    // Get some internal point of an expolygon, to be used as a representative
    // sample to test, whether this island is inside another island.
    //FIXME this was quick, but not sufficiently robust.
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
        double l2 = double(v(0))*double(v(0))+double(v(1))*double(v(1));
        if (l2 == 0.)
            return p2;
        double coef = 20. / sqrt(l2);
        return Point(p2(0) + coef * v(0), p2(1) + coef * v(1));
    }
#endif

    // Sample one internal point per expolygon.
    // FIXME this is quite an overkill to calculate a complete offset just to get a single point, but at least it is robust.
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
                            // Take a small fixed number of samples of this polygon for robustness.
                            int num_points  = int(poly.points.size());
                            int num_samples = std::min(num_points, 4);
                            int stride = num_points / num_samples;
                            for (int i = 0; i < num_points; i += stride)
                                pts.push_back(poly.points[i]);
                            break;
                        }
                #endif
            }
        // Sort the points lexicographically, so a binary search could be used to locate points inside a bounding box.
        std::sort(pts.begin(), pts.end());
        return pts;
    } 

    SupportMaterialStyle    m_style;
    const Polygons         *m_support_polygons;
    const Polygons         *m_trimming_polygons;
    Polygons                m_support_polygons_rotated;
    Polygons                m_trimming_polygons_rotated;
    // Angle in radians, by which the whole support is rotated.
    coordf_t                m_support_angle;
    // X spacing of the support lines parallel with the Y axis.
    coordf_t                m_support_spacing;
    coordf_t                m_extrusion_width;
    // For snug supports: Morphological closing of support areas.
    coordf_t                m_support_material_closing_radius;

#ifdef SUPPORT_USE_AGG_RASTERIZER
    Vec2i                       m_grid_size;
    double                      m_pixel_size;
    BoundingBox                 m_bbox;
    std::vector<unsigned char>  m_grid2;
#else // SUPPORT_USE_AGG_RASTERIZER
    Slic3r::EdgeGrid::Grid      m_grid;
#endif // SUPPORT_USE_AGG_RASTERIZER

#ifdef SLIC3R_DEBUG
    // support for deserialization of m_support_polygons, m_trimming_polygons
    Polygons                m_support_polygons_deserialized;
    Polygons                m_trimming_polygons_deserialized;
#endif /* SLIC3R_DEBUG */
};

namespace SupportMaterialInternal {
    static inline bool has_bridging_perimeters(const ExtrusionLoop &loop)
    {
        for (const ExtrusionPath &ep : loop.paths)
            if (ep.role() == erOverhangPerimeter && ! ep.polyline.empty())
                return int(ep.size()) >= (ep.is_closed() ? 3 : 2);
        return false;
    }
    static bool has_bridging_perimeters(const ExtrusionEntityCollection &perimeters)
    {
        for (const ExtrusionEntity *ee : perimeters.entities) {
            if (ee->is_collection()) {
                for (const ExtrusionEntity *ee2 : static_cast<const ExtrusionEntityCollection*>(ee)->entities) {
                    assert(! ee2->is_collection());
                    if (ee2->is_loop())
                        if (has_bridging_perimeters(*static_cast<const ExtrusionLoop*>(ee2)))
                            return true;
                }
            } else if (ee->is_loop() && has_bridging_perimeters(*static_cast<const ExtrusionLoop*>(ee)))
                return true;
        }
        return false;
    }
    static bool has_bridging_fills(const ExtrusionEntityCollection &fills)
    {
        for (const ExtrusionEntity *ee : fills.entities) {
            assert(ee->is_collection());
            for (const ExtrusionEntity *ee2 : static_cast<const ExtrusionEntityCollection*>(ee)->entities) {
                assert(! ee2->is_collection());
                assert(! ee2->is_loop());
                if (ee2->role() == erBridgeInfill)
                    return true;
            }
        }
        return false;
    }
    static bool has_bridging_extrusions(const Layer &layer) 
    {
        for (const LayerRegion *region : layer.regions()) {
            if (SupportMaterialInternal::has_bridging_perimeters(region->perimeters))
                return true;
            if (region->fill_surfaces.has(stBottomBridge) && has_bridging_fills(region->fills))
                return true;
        }
        return false;
    }

    static inline void collect_bridging_perimeter_areas(const ExtrusionLoop &loop, const float expansion_scaled, Polygons &out)
    {
        assert(expansion_scaled >= 0.f);
        for (const ExtrusionPath &ep : loop.paths)
            if (ep.role() == erOverhangPerimeter && ! ep.polyline.empty()) {
                float exp = 0.5f * (float)scale_(ep.width) + expansion_scaled;
                if (ep.is_closed()) {
                    if (ep.size() >= 3) {
                        // This is a complete loop.
                        // Add the outer contour first.
                        Polygon poly;
                        poly.points = ep.polyline.points;
                        poly.points.pop_back();
                        if (poly.area() < 0)
                            poly.reverse();
                        polygons_append(out, offset(poly, exp, SUPPORT_SURFACES_OFFSET_PARAMETERS));
                        Polygons holes = offset(poly, - exp, SUPPORT_SURFACES_OFFSET_PARAMETERS);
                        polygons_reverse(holes);
                        polygons_append(out, holes);
                    }
                } else if (ep.size() >= 2) {
                    // Offset the polyline.
                    polygons_append(out, offset(ep.polyline, exp, SUPPORT_SURFACES_OFFSET_PARAMETERS));
                }
            }
    }
    static void collect_bridging_perimeter_areas(const ExtrusionEntityCollection &perimeters, const float expansion_scaled, Polygons &out)
    {
        for (const ExtrusionEntity *ee : perimeters.entities) {
            if (ee->is_collection()) {
                for (const ExtrusionEntity *ee2 : static_cast<const ExtrusionEntityCollection*>(ee)->entities) {
                    assert(! ee2->is_collection());
                    if (ee2->is_loop())
                        collect_bridging_perimeter_areas(*static_cast<const ExtrusionLoop*>(ee2), expansion_scaled, out);
                }
            } else if (ee->is_loop())
                collect_bridging_perimeter_areas(*static_cast<const ExtrusionLoop*>(ee), expansion_scaled, out);
        }
    }

    static void remove_bridges_from_contacts(
        const PrintConfig   &print_config, 
        const Layer         &lower_layer,
        const Polygons      &lower_layer_polygons,
        const LayerRegion   &layerm,
        float                fw, 
        Polygons            &contact_polygons)
    {
        // compute the area of bridging perimeters
        Polygons bridges;
        {
            // Surface supporting this layer, expanded by 0.5 * nozzle_diameter, as we consider this kind of overhang to be sufficiently supported.
            Polygons lower_grown_slices = expand(lower_layer_polygons,
                //FIXME to mimic the decision in the perimeter generator, we should use half the external perimeter width.
                0.5f * float(scale_(print_config.nozzle_diameter.get_at(layerm.region().config().perimeter_extruder-1))),
                SUPPORT_SURFACES_OFFSET_PARAMETERS);
            // Collect perimeters of this layer.
            //FIXME split_at_first_point() could split a bridge mid-way
        #if 0
            Polylines overhang_perimeters = layerm.perimeters.as_polylines();
            // workaround for Clipper bug, see Slic3r::Polygon::clip_as_polyline()
            for (Polyline &polyline : overhang_perimeters)
                polyline.points[0].x += 1;
            // Trim the perimeters of this layer by the lower layer to get the unsupported pieces of perimeters.
            overhang_perimeters = diff_pl(overhang_perimeters, lower_grown_slices);
        #else
            Polylines overhang_perimeters = diff_pl(layerm.perimeters.as_polylines(), lower_grown_slices);
        #endif
            
            // only consider straight overhangs
            // only consider overhangs having endpoints inside layer's slices
            // convert bridging polylines into polygons by inflating them with their thickness
            // since we're dealing with bridges, we can't assume width is larger than spacing,
            // so we take the largest value and also apply safety offset to be ensure no gaps
            // are left in between
            Flow perimeter_bridge_flow = layerm.bridging_flow(frPerimeter);
            float w = float(std::max(perimeter_bridge_flow.scaled_width(), perimeter_bridge_flow.scaled_spacing()));
            for (Polyline &polyline : overhang_perimeters)
                if (polyline.is_straight()) {
                    // This is a bridge 
                    polyline.extend_start(fw);
                    polyline.extend_end(fw);
                    // Is the straight perimeter segment supported at both sides?
                    Point pts[2]       = { polyline.first_point(), polyline.last_point() };
                    bool  supported[2] = { false, false };
                    for (size_t i = 0; i < lower_layer.lslices.size() && ! (supported[0] && supported[1]); ++ i)
                        for (int j = 0; j < 2; ++ j)
                            if (! supported[j] && lower_layer.lslices_bboxes[i].contains(pts[j]) && lower_layer.lslices[i].contains(pts[j]))
                                supported[j] = true;
                    if (supported[0] && supported[1])
                        // Offset a polyline into a thick line.
                        polygons_append(bridges, offset(polyline, 0.5f * w + 10.f));
                }
            bridges = union_(bridges);
        }
        // remove the entire bridges and only support the unsupported edges
        //FIXME the brided regions are already collected as layerm.bridged. Use it?
        for (const Surface &surface : layerm.fill_surfaces.surfaces)
            if (surface.surface_type == stBottomBridge && surface.bridge_angle != -1)
                polygons_append(bridges, surface.expolygon);
        //FIXME add the gap filled areas. Extrude the gaps with a bridge flow?
        // Remove the unsupported ends of the bridges from the bridged areas.
        //FIXME add supports at regular intervals to support long bridges!
        bridges = diff(bridges,
                // Offset unsupported edges into polygons.
                offset(layerm.unsupported_bridge_edges, scale_(SUPPORT_MATERIAL_MARGIN), SUPPORT_SURFACES_OFFSET_PARAMETERS));
        // Remove bridged areas from the supported areas.
        contact_polygons = diff(contact_polygons, bridges, ApplySafetyOffset::Yes);

        #ifdef SLIC3R_DEBUG
            static int iRun = 0;
            SVG::export_expolygons(debug_out_path("support-top-contacts-remove-bridges-run%d.svg", iRun ++),
                { { { union_ex(offset(layerm.unsupported_bridge_edges, scale_(SUPPORT_MATERIAL_MARGIN), SUPPORT_SURFACES_OFFSET_PARAMETERS)) }, { "unsupported_bridge_edges", "orange", 0.5f } },
                  { { union_ex(contact_polygons) },            { "contact_polygons",           "blue",   0.5f } },
                  { { union_ex(bridges) },                     { "bridges",                    "red",    "black", "", scaled<coord_t>(0.1f), 0.5f } } });
        #endif /* SLIC3R_DEBUG */
    }
}

std::vector<Polygons> PrintObjectSupportMaterial::buildplate_covered(const PrintObject &object) const
{
    // Build support on a build plate only? If so, then collect and union all the surfaces below the current layer.
    // Unfortunately this is an inherently serial process.
    const bool            buildplate_only = this->build_plate_only();
    std::vector<Polygons> buildplate_covered;
    if (buildplate_only) {
        BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::buildplate_covered() - start";
        buildplate_covered.assign(object.layers().size(), Polygons());
        //FIXME prefix sum algorithm, parallelize it! Parallelization will also likely be more numerically stable.
        for (size_t layer_id = 1; layer_id < object.layers().size(); ++ layer_id) {
            const Layer &lower_layer = *object.layers()[layer_id-1];
            // Merge the new slices with the preceding slices.
            // Apply the safety offset to the newly added polygons, so they will connect
            // with the polygons collected before,
            // but don't apply the safety offset during the union operation as it would
            // inflate the polygons over and over.
            Polygons &covered = buildplate_covered[layer_id];
            covered = buildplate_covered[layer_id - 1];
            polygons_append(covered, offset(lower_layer.lslices, scale_(0.01)));
            covered = union_(covered);
        }
        BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::buildplate_covered() - end";
    }
    return buildplate_covered;
}

struct SupportAnnotations
{
    SupportAnnotations(const PrintObject &object, const std::vector<Polygons> &buildplate_covered) :
        enforcers_layers(object.slice_support_enforcers()),
        blockers_layers(object.slice_support_blockers()),
        buildplate_covered(buildplate_covered)
    {
        // Append custom supports.
        object.project_and_append_custom_facets(false, EnforcerBlockerType::ENFORCER, enforcers_layers);
        object.project_and_append_custom_facets(false, EnforcerBlockerType::BLOCKER, blockers_layers);
    }

    std::vector<Polygons>         enforcers_layers;
    std::vector<Polygons>         blockers_layers;
    const std::vector<Polygons>&  buildplate_covered;
};

struct SlicesMarginCache
{
    float       offset { -1 };
    // Trimming polygons, including possibly the "build plate only" mask.
    Polygons    polygons;
    // Trimming polygons, without the "build plate only" mask. If empty, use polygons.
    Polygons    all_polygons;
};

// Tuple: overhang_polygons, contact_polygons, enforcer_polygons, no_interface_offset
// no_interface_offset: minimum of external perimeter widths
static inline std::tuple<Polygons, Polygons, Polygons, float> detect_overhangs(
    const Layer             &layer,
    const size_t             layer_id,
    const Polygons          &lower_layer_polygons,
    const PrintConfig       &print_config, 
    const PrintObjectConfig &object_config,
    SupportAnnotations      &annotations, 
    SlicesMarginCache       &slices_margin, 
    const double             gap_xy
#ifdef SLIC3R_DEBUG
    , size_t                 iRun
#endif // SLIC3R_DEBUG
    )
{
    // Snug overhang polygons.
    Polygons overhang_polygons;
    // Expanded for stability, trimmed by gap_xy.
    Polygons contact_polygons;
    // Enforcers projected to overhangs, trimmed
    Polygons enforcer_polygons;

    const bool   support_auto    = object_config.support_material.value && object_config.support_material_auto.value;
    const bool   buildplate_only = ! annotations.buildplate_covered.empty();
    // If user specified a custom angle threshold, convert it to radians.
    // Zero means automatic overhang detection.
    const double threshold_rad   = (object_config.support_material_threshold.value > 0) ?
        M_PI * double(object_config.support_material_threshold.value + 1) / 180. : // +1 makes the threshold inclusive
        0.;
    float        no_interface_offset = 0.f;

    if (layer_id == 0) 
    {
        // This is the first object layer, so the object is being printed on a raft and
        // we're here just to get the object footprint for the raft.
#if 0
        // The following line was filling excessive holes in the raft, see GH #430
        overhang_polygons = collect_slices_outer(layer);
#else
        // Don't fill in the holes. The user may apply a higher raft_expansion if one wants a better 1st layer adhesion.
        overhang_polygons = to_polygons(layer.lslices);
#endif
        // Expand for better stability.
        contact_polygons = expand(overhang_polygons, scaled<float>(object_config.raft_expansion.value));
    }
    else if (! layer.regions().empty())
    {
        // Generate overhang / contact_polygons for non-raft layers.
        const Layer &lower_layer  = *layer.lower_layer;
        const bool   has_enforcer = ! annotations.enforcers_layers.empty() && ! annotations.enforcers_layers[layer_id].empty();

        // Cache support trimming polygons derived from lower layer polygons, possible merged with "on build plate only" trimming polygons.
        auto slices_margin_update = 
            [&slices_margin, &lower_layer, &lower_layer_polygons, buildplate_only, has_enforcer, &annotations, layer_id]
            (float slices_margin_offset, float no_interface_offset) {
            if (slices_margin.offset != slices_margin_offset) {
                slices_margin.offset = slices_margin_offset;
                slices_margin.polygons = (slices_margin_offset == 0.f) ?
                    lower_layer_polygons :
                    // What is the purpose of no_interface_offset? Likely to not trim the contact layer by lower layer regions that are too thin to extrude?
                    offset2(lower_layer.lslices, -no_interface_offset * 0.5f, slices_margin_offset + no_interface_offset * 0.5f, SUPPORT_SURFACES_OFFSET_PARAMETERS);
                if (buildplate_only && !annotations.buildplate_covered[layer_id].empty()) {
                    if (has_enforcer)
                        // Make a backup of trimming polygons before enforcing "on build plate only".
                        slices_margin.all_polygons = slices_margin.polygons;
                    // Trim the inflated contact surfaces by the top surfaces as well.
                    slices_margin.polygons = union_(slices_margin.polygons, annotations.buildplate_covered[layer_id]);
                }
            }
        };

        no_interface_offset = std::accumulate(layer.regions().begin(), layer.regions().end(), FLT_MAX, 
            [](float acc, const LayerRegion *layerm) { return std::min(acc, float(layerm->flow(frExternalPerimeter).scaled_width())); });

        float lower_layer_offset  = 0;
        for (LayerRegion *layerm : layer.regions()) {
            // Extrusion width accounts for the roundings of the extrudates.
            // It is the maximum widh of the extrudate.
            float fw = float(layerm->flow(frExternalPerimeter).scaled_width());
            lower_layer_offset  = 
                (layer_id < (size_t)object_config.support_material_enforce_layers.value) ? 
                    // Enforce a full possible support, ignore the overhang angle.
                    0.f :
                (threshold_rad > 0. ? 
                    // Overhang defined by an angle.
                    float(scale_(lower_layer.height / tan(threshold_rad))) :
                    // Overhang defined by half the extrusion width.
                    0.5f * fw);
            // Overhang polygons for this layer and region.
            Polygons diff_polygons;
            Polygons layerm_polygons = to_polygons(layerm->slices.surfaces);
            if (lower_layer_offset == 0.f) {
                // Support everything.
                diff_polygons = diff(layerm_polygons, lower_layer_polygons);
                if (buildplate_only) {
                    // Don't support overhangs above the top surfaces.
                    // This step is done before the contact surface is calculated by growing the overhang region.
                    diff_polygons = diff(diff_polygons, annotations.buildplate_covered[layer_id]);
                }
            } else if (support_auto) {
                // Get the regions needing a suport, collapse very tiny spots.
                //FIXME cache the lower layer offset if this layer has multiple regions.
#if 0
                //FIXME this solution will trigger stupid supports for sharp corners, see GH #4874
                diff_polygons = opening(
                    diff(layerm_polygons,
                            // Likely filtering out thin regions from the lower layer, that will not be covered by perimeters, thus they
                            // are not supporting this layer.
                            // However this may lead to a situation where regions at the current layer that are narrow thus not extrudable will generate unnecessary supports.
                            // For example, see GH issue #3094
                            opening(lower_layer_polygons, 0.5f * fw, lower_layer_offset + 0.5f * fw, SUPPORT_SURFACES_OFFSET_PARAMETERS)), 
                    //FIXME This opening is targeted to reduce very thin regions to support, but it may lead to
                    // no support at all for not so steep overhangs.
                    0.1f * fw);
#else
                diff_polygons = 
                    diff(layerm_polygons,
                            expand(lower_layer_polygons, lower_layer_offset, SUPPORT_SURFACES_OFFSET_PARAMETERS));
#endif
                if (buildplate_only && ! annotations.buildplate_covered[layer_id].empty()) {
                    // Don't support overhangs above the top surfaces.
                    // This step is done before the contact surface is calculated by growing the overhang region.
                    diff_polygons = diff(diff_polygons, annotations.buildplate_covered[layer_id]);
                }
                if (! diff_polygons.empty()) {
                    // Offset the support regions back to a full overhang, restrict them to the full overhang.
                    // This is done to increase size of the supporting columns below, as they are calculated by 
                    // propagating these contact surfaces downwards.
                    diff_polygons = diff(
                        intersection(expand(diff_polygons, lower_layer_offset, SUPPORT_SURFACES_OFFSET_PARAMETERS), layerm_polygons),
                        lower_layer_polygons);
                }
                //FIXME add user defined filtering here based on minimal area or minimum radius or whatever.
            }

            if (diff_polygons.empty())
                continue;

            // Apply the "support blockers".
            if (! annotations.blockers_layers.empty() && ! annotations.blockers_layers[layer_id].empty()) {
                // Expand the blocker a bit. Custom blockers produce strips
                // spanning just the projection between the two slices.
                // Subtracting them as they are may leave unwanted narrow
                // residues of diff_polygons that would then be supported.
                diff_polygons = diff(diff_polygons,
                    expand(union_(annotations.blockers_layers[layer_id]), float(1000.*SCALED_EPSILON)));
            }

            #ifdef SLIC3R_DEBUG
            {
                ::Slic3r::SVG svg(debug_out_path("support-top-contacts-raw-run%d-layer%d-region%d.svg", 
                    iRun, layer_id, 
                    std::find_if(layer.regions().begin(), layer.regions().end(), [layerm](const LayerRegion* other){return other == layerm;}) - layer.regions().begin()),
                    get_extents(diff_polygons));
                Slic3r::ExPolygons expolys = union_ex(diff_polygons);
                svg.draw(expolys);
            }
            #endif /* SLIC3R_DEBUG */

            if (object_config.dont_support_bridges)
                //FIXME Expensive, potentially not precise enough. Misses gap fill extrusions, which bridge.
                SupportMaterialInternal::remove_bridges_from_contacts(
                    print_config, lower_layer, lower_layer_polygons, *layerm, fw, diff_polygons);

            if (diff_polygons.empty())
                continue;

            #ifdef SLIC3R_DEBUG
            Slic3r::SVG::export_expolygons(
                debug_out_path("support-top-contacts-filtered-run%d-layer%d-region%d-z%f.svg", 
                    iRun, layer_id, 
                    std::find_if(layer.regions().begin(), layer.regions().end(), [layerm](const LayerRegion* other){return other == layerm;}) - layer.regions().begin(),
                    layer.print_z),
                union_ex(diff_polygons));
            #endif /* SLIC3R_DEBUG */

            //FIXME the overhang_polygons are used to construct the support towers as well.
            //if (this->has_contact_loops())
                // Store the exact contour of the overhang for the contact loops.
                polygons_append(overhang_polygons, diff_polygons);

            // Let's define the required contact area by using a max gap of half the upper 
            // extrusion width and extending the area according to the configured margin.
            // We increment the area in steps because we don't want our support to overflow
            // on the other side of the object (if it's very thin).
            {
                //FIMXE 1) Make the offset configurable, 2) Make the Z span configurable.
                //FIXME one should trim with the layer span colliding with the support layer, this layer
                // may be lower than lower_layer, so the support area needed may need to be actually bigger!
                // For the same reason, the non-bridging support area may be smaller than the bridging support area!
                slices_margin_update(std::min(lower_layer_offset, float(scale_(gap_xy))), no_interface_offset);
                // Offset the contact polygons outside.
#if 0
                for (size_t i = 0; i < NUM_MARGIN_STEPS; ++ i) {
                    diff_polygons = diff(
                        offset(
                            diff_polygons,
                            scaled<float>(SUPPORT_MATERIAL_MARGIN / NUM_MARGIN_STEPS),
                            ClipperLib::jtRound,
                            // round mitter limit
                            scale_(0.05)),
                        slices_margin.polygons);
                }
#else
                diff_polygons = diff(diff_polygons, slices_margin.polygons);
#endif
            }
            polygons_append(contact_polygons, diff_polygons);
        } // for each layer.region

        if (has_enforcer)
            if (const Polygons &enforcer_polygons_src = annotations.enforcers_layers[layer_id]; ! enforcer_polygons_src.empty()) {
                // Enforce supports (as if with 90 degrees of slope) for the regions covered by the enforcer meshes.
    #ifdef SLIC3R_DEBUG
                ExPolygons enforcers_united = union_ex(enforcer_polygons_src);
    #endif // SLIC3R_DEBUG
                enforcer_polygons = diff(intersection(layer.lslices, enforcer_polygons_src),
                    // Inflate just a tiny bit to avoid intersection of the overhang areas with the object.
                    expand(lower_layer_polygons, 0.05f * no_interface_offset, SUPPORT_SURFACES_OFFSET_PARAMETERS));
    #ifdef SLIC3R_DEBUG
                SVG::export_expolygons(debug_out_path("support-top-contacts-enforcers-run%d-layer%d-z%f.svg", iRun, layer_id, layer.print_z),
                    { { layer.lslices,                                 { "layer.lslices",              "gray",   0.2f } },
                      { { union_ex(lower_layer_polygons) },            { "lower_layer_polygons",       "green",  0.5f } },
                      { enforcers_united,                              { "enforcers",                  "blue",   0.5f } },
                      { { union_safety_offset_ex(enforcer_polygons) }, { "new_contacts",               "red",    "black", "", scaled<coord_t>(0.1f), 0.5f } } });
    #endif /* SLIC3R_DEBUG */
                if (! enforcer_polygons.empty()) {
                    polygons_append(overhang_polygons, enforcer_polygons);
                    slices_margin_update(std::min(lower_layer_offset, float(scale_(gap_xy))), no_interface_offset);
                    polygons_append(contact_polygons, diff(enforcer_polygons, slices_margin.all_polygons.empty() ? slices_margin.polygons : slices_margin.all_polygons));
                }
            }
        }

    return std::make_tuple(std::move(overhang_polygons), std::move(contact_polygons), std::move(enforcer_polygons), no_interface_offset);
}

// Allocate one, possibly two support contact layers.
// For "thick" overhangs, one support layer will be generated to support normal extrusions, the other to support the "thick" extrusions.
static inline std::pair<PrintObjectSupportMaterial::MyLayer*, PrintObjectSupportMaterial::MyLayer*> new_contact_layer(
    const PrintConfig                                   &print_config, 
    const PrintObjectConfig                             &object_config,
    const SlicingParameters                             &slicing_params,
    const coordf_t                                       support_layer_height_min,
    const Layer                                         &layer, 
    std::deque<PrintObjectSupportMaterial::MyLayer>     &layer_storage,
    tbb::spin_mutex                                     &layer_storage_mutex)
{
    double print_z, bottom_z, height;
    PrintObjectSupportMaterial::MyLayer* bridging_layer = nullptr;
    assert(layer.id() >= slicing_params.raft_layers());
    size_t layer_id = layer.id() - slicing_params.raft_layers();

    if (layer_id == 0) {
        // This is a raft contact layer sitting directly on the print bed.
        assert(slicing_params.has_raft());
        print_z  = slicing_params.raft_contact_top_z;
        bottom_z = slicing_params.raft_interface_top_z;
        height   = slicing_params.contact_raft_layer_height;
    } else if (slicing_params.soluble_interface) {
        // Align the contact surface height with a layer immediately below the supported layer.
        // Interface layer will be synchronized with the object.
        print_z  = layer.bottom_z();
        height   = layer.lower_layer->height;
        bottom_z = (layer_id == 1) ? slicing_params.object_print_z_min : layer.lower_layer->lower_layer->print_z;
    } else {
        print_z  = layer.bottom_z() - slicing_params.gap_support_object;
        bottom_z = print_z;
        height   = 0.;
        // Ignore this contact area if it's too low.
        // Don't want to print a layer below the first layer height as it may not stick well.
        //FIXME there may be a need for a single layer support, then one may decide to print it either as a bottom contact or a top contact
        // and it may actually make sense to do it with a thinner layer than the first layer height.
        if (print_z < slicing_params.first_print_layer_height - EPSILON) {
            // This contact layer is below the first layer height, therefore not printable. Don't support this surface.
            return std::pair<PrintObjectSupportMaterial::MyLayer*, PrintObjectSupportMaterial::MyLayer*>(nullptr, nullptr);
        }
        const bool     has_raft    = slicing_params.raft_layers() > 1;
        const coordf_t min_print_z = has_raft ? slicing_params.raft_contact_top_z : slicing_params.first_print_layer_height;
        if (print_z < min_print_z + support_layer_height_min) {
            // Align the layer with the 1st layer height or the raft contact layer.
            // With raft active, any contact layer below the raft_contact_top_z will be brought to raft_contact_top_z to extend the raft area.
            print_z  = min_print_z;
            bottom_z = has_raft ? slicing_params.raft_interface_top_z : 0;
            height   = has_raft ? slicing_params.contact_raft_layer_height : min_print_z;
        } else {
            // Don't know the height of the top contact layer yet. The top contact layer is printed with a normal flow and 
            // its height will be set adaptively later on.
        }

        // Contact layer will be printed with a normal flow, but
        // it will support layers printed with a bridging flow.
        if (object_config.thick_bridges && SupportMaterialInternal::has_bridging_extrusions(layer)) {
            coordf_t bridging_height = 0.;
            for (const LayerRegion* region : layer.regions())
                bridging_height += region->region().bridging_height_avg(print_config);
            bridging_height /= coordf_t(layer.regions().size());
            coordf_t bridging_print_z = layer.print_z - bridging_height - slicing_params.gap_support_object;
            if (bridging_print_z >= min_print_z) {
                // Not below the first layer height means this layer is printable.
                if (print_z < min_print_z + support_layer_height_min) {
                    // Align the layer with the 1st layer height or the raft contact layer.
                    bridging_print_z = min_print_z;
                }
                if (bridging_print_z < print_z - EPSILON) {
                    // Allocate the new layer.
                    bridging_layer = &layer_allocate(layer_storage, layer_storage_mutex, PrintObjectSupportMaterial::sltTopContact);
                    bridging_layer->idx_object_layer_above = layer_id;
                    bridging_layer->print_z = bridging_print_z;
                    if (bridging_print_z == slicing_params.first_print_layer_height) {
                        bridging_layer->bottom_z = 0;
                        bridging_layer->height = slicing_params.first_print_layer_height;
                    } else {
                        // Don't know the height yet.
                        bridging_layer->bottom_z = bridging_print_z;
                        bridging_layer->height = 0;
                    }
                }
            }
        }
    }

    PrintObjectSupportMaterial::MyLayer &new_layer = layer_allocate(layer_storage, layer_storage_mutex, PrintObjectSupportMaterial::sltTopContact);
    new_layer.idx_object_layer_above = layer_id;
    new_layer.print_z  = print_z;
    new_layer.bottom_z = bottom_z;
    new_layer.height   = height;
    return std::make_pair(&new_layer, bridging_layer);
}

static inline void fill_contact_layer(
    PrintObjectSupportMaterial::MyLayer &new_layer,
    size_t                   layer_id,
    const SlicingParameters &slicing_params,
    const PrintObjectConfig &object_config,
    const SlicesMarginCache &slices_margin, 
    const Polygons          &overhang_polygons, 
    const Polygons          &contact_polygons, 
    const Polygons          &enforcer_polygons, 
    const Polygons          &lower_layer_polygons,
    const Flow              &support_material_flow,
    float                    no_interface_offset
#ifdef SLIC3R_DEBUG
    , size_t                 iRun,
    const Layer             &layer
#endif // SLIC3R_DEBUG
    )
{
    const SupportGridParams grid_params(object_config, support_material_flow);

    Polygons lower_layer_polygons_for_dense_interface_cache;
    auto lower_layer_polygons_for_dense_interface = [&lower_layer_polygons_for_dense_interface_cache, &lower_layer_polygons, no_interface_offset]() -> const Polygons& {
        if (lower_layer_polygons_for_dense_interface_cache.empty())
            lower_layer_polygons_for_dense_interface_cache = 
                //FIXME no_interface_offset * 0.6f offset is not quite correct, one shall derive it based on an angle thus depending on layer height.
            opening(lower_layer_polygons, no_interface_offset * 0.5f, no_interface_offset * (0.6f + 0.5f), SUPPORT_SURFACES_OFFSET_PARAMETERS);
        return lower_layer_polygons_for_dense_interface_cache;
    };

    // Stretch support islands into a grid, trim them. 
    SupportGridPattern support_grid_pattern(&contact_polygons, &slices_margin.polygons, grid_params);
    // 1) Contact polygons will be projected down. To keep the interface and base layers from growing, return a contour a tiny bit smaller than the grid cells.
    new_layer.contact_polygons = std::make_unique<Polygons>(support_grid_pattern.extract_support(grid_params.expansion_to_propagate, true
#ifdef SLIC3R_DEBUG
        , "top_contact_polygons", iRun, layer_id, layer.print_z
#endif // SLIC3R_DEBUG
        ));
    // 2) infill polygons, expand them by half the extrusion width + a tiny bit of extra.
    bool reduce_interfaces = object_config.support_material_style.value != smsSnug && layer_id > 0 && !slicing_params.soluble_interface;
    if (reduce_interfaces) {
        // Reduce the amount of dense interfaces: Do not generate dense interfaces below overhangs with 60% overhang of the extrusions.
        Polygons dense_interface_polygons = diff(overhang_polygons, lower_layer_polygons_for_dense_interface());
        if (! dense_interface_polygons.empty()) {
            dense_interface_polygons =
                diff(
                    // Regularize the contour.
                    expand(dense_interface_polygons, no_interface_offset * 0.1f),
                    slices_margin.polygons);
            // Support islands, to be stretched into a grid.
            //FIXME The regularization of dense_interface_polygons above may stretch dense_interface_polygons outside of the contact polygons,
            // thus some dense interface areas may not get supported. Trim the excess with contact_polygons at the following line.
            // See for example GH #4874.
            Polygons dense_interface_polygons_trimmed = intersection(dense_interface_polygons, *new_layer.contact_polygons);
            // Stretch support islands into a grid, trim them. 
            SupportGridPattern support_grid_pattern(&dense_interface_polygons_trimmed, &slices_margin.polygons, grid_params);
            new_layer.polygons = support_grid_pattern.extract_support(grid_params.expansion_to_slice, false
#ifdef SLIC3R_DEBUG
                , "top_contact_polygons2", iRun, layer_id, layer.print_z
#endif // SLIC3R_DEBUG
                );
    #ifdef SLIC3R_DEBUG
            SVG::export_expolygons(debug_out_path("support-top-contacts-final1-run%d-layer%d-z%f.svg", iRun, layer_id, layer.print_z),
                { { { union_ex(lower_layer_polygons) },               { "lower_layer_polygons",       "gray",   0.2f } },
                    { { union_ex(*new_layer.contact_polygons) },      { "new_layer.contact_polygons", "yellow", 0.5f } },
                    { { union_ex(slices_margin.polygons) },           { "slices_margin_cached",       "blue",   0.5f } },
                    { { union_ex(dense_interface_polygons) },         { "dense_interface_polygons",   "green",  0.5f } },
                    { { union_safety_offset_ex(new_layer.polygons) }, { "new_layer.polygons",         "red",    "black", "", scaled<coord_t>(0.1f), 0.5f } } });
            //support_grid_pattern.serialize(debug_out_path("support-top-contacts-final-run%d-layer%d-z%f.bin", iRun, layer_id, layer.print_z));
            SVG::export_expolygons(debug_out_path("support-top-contacts-final2-run%d-layer%d-z%f.svg", iRun, layer_id, layer.print_z),
                { { { union_ex(lower_layer_polygons) },               { "lower_layer_polygons",       "gray",   0.2f } },
                    { { union_ex(*new_layer.contact_polygons) },      { "new_layer.contact_polygons", "yellow", 0.5f } },
                    { { union_ex(contact_polygons) },                 { "contact_polygons",           "blue",   0.5f } },
                    { { union_ex(dense_interface_polygons) },         { "dense_interface_polygons",   "green",  0.5f } },
                    { { union_safety_offset_ex(new_layer.polygons) }, { "new_layer.polygons",         "red",    "black", "", scaled<coord_t>(0.1f), 0.5f } } });
    #endif /* SLIC3R_DEBUG */
        }
    } else {
        new_layer.polygons = support_grid_pattern.extract_support(grid_params.expansion_to_slice, true
#ifdef SLIC3R_DEBUG
            , "top_contact_polygons3", iRun, layer_id, layer.print_z
#endif // SLIC3R_DEBUG
            );
    }

    if (! enforcer_polygons.empty() && ! slices_margin.all_polygons.empty() && layer_id > 0) {
        // Support enforcers used together with support enforcers. The support enforcers need to be handled separately from the rest of the support.
        
        SupportGridPattern support_grid_pattern(&enforcer_polygons, &slices_margin.all_polygons, grid_params);
        // 1) Contact polygons will be projected down. To keep the interface and base layers from growing, return a contour a tiny bit smaller than the grid cells.
        new_layer.enforcer_polygons = std::make_unique<Polygons>(support_grid_pattern.extract_support(grid_params.expansion_to_propagate, true
#ifdef SLIC3R_DEBUG
            , "top_contact_polygons4", iRun, layer_id, layer.print_z
#endif // SLIC3R_DEBUG
            ));
        Polygons new_polygons;
        bool needs_union = ! new_layer.polygons.empty();
        if (reduce_interfaces) {
            // 2) infill polygons, expand them by half the extrusion width + a tiny bit of extra.
            // Reduce the amount of dense interfaces: Do not generate dense interfaces below overhangs with 60% overhang of the extrusions.
            Polygons dense_interface_polygons = diff(enforcer_polygons, lower_layer_polygons_for_dense_interface());
            if (! dense_interface_polygons.empty()) {
                dense_interface_polygons =
                    diff(
                        // Regularize the contour.
                        expand(dense_interface_polygons, no_interface_offset * 0.1f),
                        slices_margin.all_polygons);
                // Support islands, to be stretched into a grid.
                //FIXME The regularization of dense_interface_polygons above may stretch dense_interface_polygons outside of the contact polygons,
                // thus some dense interface areas may not get supported. Trim the excess with contact_polygons at the following line.
                // See for example GH #4874.
                Polygons dense_interface_polygons_trimmed = intersection(dense_interface_polygons, *new_layer.enforcer_polygons);
                SupportGridPattern support_grid_pattern(&dense_interface_polygons_trimmed, &slices_margin.all_polygons, grid_params);
                // Extend the polygons to extrude with the contact polygons of support enforcers.
                new_polygons = support_grid_pattern.extract_support(grid_params.expansion_to_slice, false
    #ifdef SLIC3R_DEBUG
                    , "top_contact_polygons5", iRun, layer_id, layer.print_z
    #endif // SLIC3R_DEBUG
                    );
            }
        } else {
            new_polygons = support_grid_pattern.extract_support(grid_params.expansion_to_slice, true
    #ifdef SLIC3R_DEBUG
                , "top_contact_polygons6", iRun, layer_id, layer.print_z
    #endif // SLIC3R_DEBUG
                );
        }
        append(new_layer.polygons, std::move(new_polygons));
        if (needs_union)
            new_layer.polygons = union_(new_layer.polygons);
    }

#ifdef SLIC3R_DEBUG
    SVG::export_expolygons(debug_out_path("support-top-contacts-final0-run%d-layer%d-z%f.svg", iRun, layer_id, layer.print_z),
        { { { union_ex(lower_layer_polygons) },               { "lower_layer_polygons",       "gray",   0.2f } },
            { { union_ex(*new_layer.contact_polygons) },      { "new_layer.contact_polygons", "yellow", 0.5f } },
            { { union_ex(contact_polygons) },                 { "contact_polygons",           "blue",   0.5f } },
            { { union_ex(overhang_polygons) },                { "overhang_polygons",          "green",  0.5f } },
            { { union_safety_offset_ex(new_layer.polygons) }, { "new_layer.polygons",         "red",    "black", "", scaled<coord_t>(0.1f), 0.5f } } });
#endif /* SLIC3R_DEBUG */

    // Even after the contact layer was expanded into a grid, some of the contact islands may be too tiny to be extruded.
    // Remove those tiny islands from new_layer.polygons and new_layer.contact_polygons.
                    
    // Store the overhang polygons.
    // The overhang polygons are used in the path generator for planning of the contact loops.
    // if (this->has_contact_loops()). Compared to "polygons", "overhang_polygons" are snug.
    new_layer.overhang_polygons = std::make_unique<Polygons>(std::move(overhang_polygons));
    if (! enforcer_polygons.empty())
        new_layer.enforcer_polygons = std::make_unique<Polygons>(std::move(enforcer_polygons));
}

// Merge close contact layers conservatively: If two layers are closer than the minimum allowed print layer height (the min_layer_height parameter),
// the top contact layer is merged into the bottom contact layer.
static void merge_contact_layers(const SlicingParameters &slicing_params, double support_layer_height_min, PrintObjectSupportMaterial::MyLayersPtr &layers)
{
    // Sort the layers, as one layer may produce bridging and non-bridging contact layers with different print_z.
    std::sort(layers.begin(), layers.end(), [](const PrintObjectSupportMaterial::MyLayer *l1, const PrintObjectSupportMaterial::MyLayer *l2) { return l1->print_z < l2->print_z; });

    int i = 0;
    int k = 0;
    {
        // Find the span of layers, which are to be printed at the first layer height.
        int j = 0;
        for (; j < (int)layers.size() && layers[j]->print_z < slicing_params.first_print_layer_height + support_layer_height_min - EPSILON; ++ j);
        if (j > 0) {
            // Merge the layers layers (0) to (j - 1) into the layers[0].
            PrintObjectSupportMaterial::MyLayer &dst = *layers.front();
            for (int u = 1; u < j; ++ u)
                dst.merge(std::move(*layers[u]));
            // Snap the first layer to the 1st layer height.
            dst.print_z  = slicing_params.first_print_layer_height;
            dst.height   = slicing_params.first_print_layer_height;
            dst.bottom_z = 0;
            ++ k;
        }
        i = j;
    }
    for (; i < int(layers.size()); ++ k) {
        // Find the span of layers closer than m_support_layer_height_min.
        int j = i + 1;
        coordf_t zmax = layers[i]->print_z + support_layer_height_min + EPSILON;
        for (; j < (int)layers.size() && layers[j]->print_z < zmax; ++ j) ;
        if (i + 1 < j) {
            // Merge the layers layers (i + 1) to (j - 1) into the layers[i].
            PrintObjectSupportMaterial::MyLayer &dst = *layers[i];
            for (int u = i + 1; u < j; ++ u)
                dst.merge(std::move(*layers[u]));
        }
        if (k < i)
            layers[k] = layers[i];
        i = j;
    }
    if (k < (int)layers.size())
        layers.erase(layers.begin() + k, layers.end());
}

// Generate top contact layers supporting overhangs.
// For a soluble interface material synchronize the layer heights with the object, otherwise leave the layer height undefined.
// If supports over bed surface only are requested, don't generate contact layers over an object.
PrintObjectSupportMaterial::MyLayersPtr PrintObjectSupportMaterial::top_contact_layers(
    const PrintObject &object, const std::vector<Polygons> &buildplate_covered, MyLayerStorage &layer_storage) const
{
#ifdef SLIC3R_DEBUG
    static int iRun = 0;
    ++ iRun; 
    #define SLIC3R_IRUN , iRun
#endif /* SLIC3R_DEBUG */

    // Slice support enforcers / support blockers.
    SupportAnnotations annotations(object, buildplate_covered);

    // Output layers, sorted by top Z.
    MyLayersPtr contact_out;

    BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::top_contact_layers() in parallel - start";
    // Determine top contact areas.
    // If generating raft only (no support), only calculate top contact areas for the 0th layer.
    // If having a raft, start with 0th layer, otherwise with 1st layer.
    // Note that layer_id < layer->id when raft_layers > 0 as the layer->id incorporates the raft layers.
    // So layer_id == 0 means first object layer and layer->id == 0 means first print layer if there are no explicit raft layers.
    size_t num_layers = this->has_support() ? object.layer_count() : 1;
    // For each overhang layer, two supporting layers may be generated: One for the overhangs extruded with a bridging flow, 
    // and the other for the overhangs extruded with a normal flow.
    contact_out.assign(num_layers * 2, nullptr);
    tbb::spin_mutex layer_storage_mutex;
    tbb::parallel_for(tbb::blocked_range<size_t>(this->has_raft() ? 0 : 1, num_layers),
        [this, &object, &annotations, &layer_storage, &layer_storage_mutex, &contact_out]
        (const tbb::blocked_range<size_t>& range) {
            for (size_t layer_id = range.begin(); layer_id < range.end(); ++ layer_id) 
            {
                const Layer        &layer                = *object.layers()[layer_id];
                Polygons            lower_layer_polygons = (layer_id == 0) ? Polygons() : to_polygons(object.layers()[layer_id - 1]->lslices);
                SlicesMarginCache   slices_margin;

                auto [overhang_polygons, contact_polygons, enforcer_polygons, no_interface_offset] =
                    detect_overhangs(layer, layer_id, lower_layer_polygons, *m_print_config, *m_object_config, annotations, slices_margin, m_support_params.gap_xy
                #ifdef SLIC3R_DEBUG
                                    , iRun
                #endif // SLIC3R_DEBUG
                        );

                // Now apply the contact areas to the layer where they need to be made.
                if (! contact_polygons.empty() || ! overhang_polygons.empty()) {
                    // Allocate the two empty layers.
                    auto [new_layer, bridging_layer] = new_contact_layer(*m_print_config, *m_object_config, m_slicing_params, m_support_params.support_layer_height_min, layer, layer_storage, layer_storage_mutex);
                    if (new_layer) {
                        // Fill the non-bridging layer with polygons.
                        fill_contact_layer(*new_layer, layer_id, m_slicing_params,
                            *m_object_config, slices_margin, overhang_polygons, contact_polygons, enforcer_polygons, lower_layer_polygons,
                            m_support_params.support_material_flow, no_interface_offset
                    #ifdef SLIC3R_DEBUG
                            , iRun, layer
                    #endif // SLIC3R_DEBUG
                            );
                        // Insert new layer even if there is no interface generated: Likely the support angle is not steep enough to require dense interface,
                        // however generating a sparse support will be useful for the object stability.
                        // if (! new_layer->polygons.empty())
                        contact_out[layer_id * 2] = new_layer;
                        if (bridging_layer != nullptr) {
                            bridging_layer->polygons          = new_layer->polygons;
                            bridging_layer->contact_polygons  = std::make_unique<Polygons>(*new_layer->contact_polygons);
                            bridging_layer->overhang_polygons = std::make_unique<Polygons>(*new_layer->overhang_polygons);
                            if (new_layer->enforcer_polygons)
                                bridging_layer->enforcer_polygons = std::make_unique<Polygons>(*new_layer->enforcer_polygons);
                            contact_out[layer_id * 2 + 1] = bridging_layer;
                        }
                    }
                }
            }
        });

    // Compress contact_out, remove the nullptr items.
    remove_nulls(contact_out);

    // Merge close contact layers conservatively: If two layers are closer than the minimum allowed print layer height (the min_layer_height parameter),
    // the top contact layer is merged into the bottom contact layer.
    merge_contact_layers(m_slicing_params, m_support_params.support_layer_height_min, contact_out);

    BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::top_contact_layers() in parallel - end";

    return contact_out;
}

// Find the bottom contact layers above the top surfaces of this layer.
static inline PrintObjectSupportMaterial::MyLayer* detect_bottom_contacts(
    const SlicingParameters                          &slicing_params,
    const PrintObjectSupportMaterial::SupportParams  &support_params,
    const PrintObject                                &object,
    const Layer                                      &layer,
    // Existing top contact layers, to which this newly created bottom contact layer will be snapped to guarantee a minimum layer height.
    const PrintObjectSupportMaterial::MyLayersPtr    &top_contacts,
    // First top contact layer index overlapping with this new bottom interface layer.
    size_t                                            contact_idx,
    // To allocate a new layer from.
    std::deque<PrintObjectSupportMaterial::MyLayer>  &layer_storage,
    // To trim the support areas above this bottom interface layer with this newly created bottom interface layer.
    std::vector<Polygons>                            &layer_support_areas,
    // Support areas projected from top to bottom, starting with top support interfaces.
    const Polygons                                   &supports_projected
#ifdef SLIC3R_DEBUG
    , size_t                                          iRun
    , const Polygons                                 &polygons_new
#endif // SLIC3R_DEBUG
    )
{
    Polygons top = collect_region_slices_by_type(layer, stTop);
#ifdef SLIC3R_DEBUG
    SVG::export_expolygons(debug_out_path("support-bottom-layers-raw-%d-%lf.svg", iRun, layer.print_z),
        { { { union_ex(top) },                                { "top",            "blue",    0.5f } },
            { { union_safety_offset_ex(supports_projected) }, { "overhangs",      "magenta", 0.5f } },
            { layer.lslices,                                  { "layer.lslices",  "green",   0.5f } },
            { { union_safety_offset_ex(polygons_new) },       { "polygons_new",   "red", "black", "", scaled<coord_t>(0.1f), 0.5f } } });
#endif /* SLIC3R_DEBUG */

    // Now find whether any projection of the contact surfaces above layer.print_z not yet supported by any 
    // top surfaces above layer.print_z falls onto this top surface. 
    // Touching are the contact surfaces supported exclusively by this top surfaces.
    // Don't use a safety offset as it has been applied during insertion of polygons.
    if (top.empty())
        return nullptr;

    Polygons touching = intersection(top, supports_projected);
    if (touching.empty())
        return nullptr;

    assert(layer.id() >= slicing_params.raft_layers());
    size_t layer_id  = layer.id() - slicing_params.raft_layers();

    // Allocate a new bottom contact layer.
    PrintObjectSupportMaterial::MyLayer &layer_new = layer_allocate(layer_storage, PrintObjectSupportMaterial::sltBottomContact);
    // Grow top surfaces so that interface and support generation are generated
    // with some spacing from object - it looks we don't need the actual
    // top shapes so this can be done here
    //FIXME calculate layer height based on the actual thickness of the layer:
    // If the layer is extruded with no bridging flow, support just the normal extrusions.
    layer_new.height = slicing_params.soluble_interface ?
        // Align the interface layer with the object's layer height.
        layer.upper_layer->height :
        // Place a bridge flow interface layer or the normal flow interface layer over the top surface.
        support_params.support_material_bottom_interface_flow.height();
    layer_new.print_z = slicing_params.soluble_interface ? layer.upper_layer->print_z :
        layer.print_z + layer_new.height + slicing_params.gap_object_support;
    layer_new.bottom_z = layer.print_z;
    layer_new.idx_object_layer_below = layer_id;
    layer_new.bridging = !slicing_params.soluble_interface && object.config().thick_bridges;
    //FIXME how much to inflate the bottom surface, as it is being extruded with a bridging flow? The following line uses a normal flow.
    layer_new.polygons = expand(touching, float(support_params.support_material_flow.scaled_width()), SUPPORT_SURFACES_OFFSET_PARAMETERS);

    if (! slicing_params.soluble_interface) {
        // Walk the top surfaces, snap the top of the new bottom surface to the closest top of the top surface,
        // so there will be no support surfaces generated with thickness lower than m_support_layer_height_min.
        for (size_t top_idx = size_t(std::max<int>(0, contact_idx));
            top_idx < top_contacts.size() && top_contacts[top_idx]->print_z < layer_new.print_z + support_params.support_layer_height_min + EPSILON;
            ++ top_idx) {
            if (top_contacts[top_idx]->print_z > layer_new.print_z - support_params.support_layer_height_min - EPSILON) {
                // A top layer has been found, which is close to the new bottom layer.
                coordf_t diff = layer_new.print_z - top_contacts[top_idx]->print_z;
                assert(std::abs(diff) <= support_params.support_layer_height_min + EPSILON);
                if (diff > 0.) {
                    // The top contact layer is below this layer. Make the bridging layer thinner to align with the existing top layer.
                    assert(diff < layer_new.height + EPSILON);
                    assert(layer_new.height - diff >= support_params.support_layer_height_min - EPSILON);
                    layer_new.print_z = top_contacts[top_idx]->print_z;
                    layer_new.height -= diff;
                }
                else {
                    // The top contact layer is above this layer. One may either make this layer thicker or thinner.
                    // By making the layer thicker, one will decrease the number of discrete layers with the price of extruding a bit too thick bridges.
                    // By making the layer thinner, one adds one more discrete layer.
                    layer_new.print_z = top_contacts[top_idx]->print_z;
                    layer_new.height -= diff;
                }
                break;
            }
        }
    }

#ifdef SLIC3R_DEBUG
    Slic3r::SVG::export_expolygons(
        debug_out_path("support-bottom-contacts-%d-%lf.svg", iRun, layer_new.print_z),
        union_ex(layer_new.polygons));
#endif /* SLIC3R_DEBUG */

    // Trim the already created base layers above the current layer intersecting with the new bottom contacts layer.
    //FIXME Maybe this is no more needed, as the overlapping base layers are trimmed by the bottom layers at the final stage?
    touching = expand(touching, float(SCALED_EPSILON));
    for (int layer_id_above = layer_id + 1; layer_id_above < int(object.total_layer_count()); ++ layer_id_above) {
        const Layer &layer_above = *object.layers()[layer_id_above];
        if (layer_above.print_z > layer_new.print_z - EPSILON)
            break;
        if (Polygons &above = layer_support_areas[layer_id_above]; ! above.empty()) {
#ifdef SLIC3R_DEBUG
            SVG::export_expolygons(debug_out_path("support-support-areas-raw-before-trimming-%d-with-%f-%lf.svg", iRun, layer.print_z, layer_above.print_z),
                { { { union_ex(touching) },              { "touching", "blue", 0.5f } },
                    { { union_safety_offset_ex(above) }, { "above",    "red", "black", "", scaled<coord_t>(0.1f), 0.5f } } });
#endif /* SLIC3R_DEBUG */
            above = diff(above, touching);
#ifdef SLIC3R_DEBUG
            Slic3r::SVG::export_expolygons(
                debug_out_path("support-support-areas-raw-after-trimming-%d-with-%f-%lf.svg", iRun, layer.print_z, layer_above.print_z),
                union_ex(above));
#endif /* SLIC3R_DEBUG */
        }
    }

    return &layer_new;
}

// Returns polygons to print + polygons to propagate downwards.
// Called twice: First for normal supports, possibly trimmed by "on build plate only", second for support enforcers not trimmed by "on build plate only".
static inline std::pair<Polygons, Polygons> project_support_to_grid(const Layer &layer, const SupportGridParams &grid_params, const Polygons &overhangs, Polygons *layer_buildplate_covered
#ifdef SLIC3R_DEBUG 
    , size_t iRun, size_t layer_id, const char *debug_name
#endif /* SLIC3R_DEBUG */
)
{
    // Remove the areas that touched from the projection that will continue on next, lower, top surfaces.
//            Polygons trimming = union_(to_polygons(layer.slices), touching, true);
    Polygons trimming = layer_buildplate_covered ? std::move(*layer_buildplate_covered) : offset(layer.lslices, float(SCALED_EPSILON));
    Polygons overhangs_projection = diff(overhangs, trimming);

#ifdef SLIC3R_DEBUG
    SVG::export_expolygons(debug_out_path("support-support-areas-%s-raw-%d-%lf.svg", debug_name, iRun, layer.print_z),
        { { { union_ex(trimming) },                           { "trimming",               "blue", 0.5f } },
          { { union_safety_offset_ex(overhangs_projection) }, { "overhangs_projection",   "red", "black", "", scaled<coord_t>(0.1f), 0.5f } } });
#endif /* SLIC3R_DEBUG */

    remove_sticks(overhangs_projection);
    remove_degenerate(overhangs_projection);

#ifdef SLIC3R_DEBUG
    SVG::export_expolygons(debug_out_path("support-support-areas-%s-raw-cleaned-%d-%lf.svg", debug_name, iRun, layer.print_z),
        { { { union_ex(trimming) },              { "trimming",             "blue", 0.5f } },
          { { union_ex(overhangs_projection) },  { "overhangs_projection", "red", "black", "", scaled<coord_t>(0.1f), 0.5f } } });
#endif /* SLIC3R_DEBUG */

    SupportGridPattern support_grid_pattern(&overhangs_projection, &trimming, grid_params);
    tbb::task_group task_group_inner;

    std::pair<Polygons, Polygons> out;

    // 1) Cache the slice of a support volume. The support volume is expanded by 1/2 of support material flow spacing
    // to allow a placement of suppot zig-zag snake along the grid lines.
    task_group_inner.run([&grid_params, &support_grid_pattern, &out
#ifdef SLIC3R_DEBUG 
        , &layer, layer_id, iRun, debug_name
#endif /* SLIC3R_DEBUG */
    ] {
            out.first = support_grid_pattern.extract_support(grid_params.expansion_to_slice, true
#ifdef SLIC3R_DEBUG
                , (std::string(debug_name) + "_support_area").c_str(), iRun, layer_id, layer.print_z
#endif // SLIC3R_DEBUG
            );
#ifdef SLIC3R_DEBUG
            Slic3r::SVG::export_expolygons(
                debug_out_path("support-layer_support_area-gridded-%s-%d-%lf.svg", debug_name, iRun, layer.print_z),
                union_ex(out.first));
#endif /* SLIC3R_DEBUG */
        });

    // 2) Support polygons will be projected down. To keep the interface and base layers from growing, return a contour a tiny bit smaller than the grid cells.
    task_group_inner.run([&grid_params, &support_grid_pattern, &out
#ifdef SLIC3R_DEBUG 
        , &layer, layer_id, &overhangs_projection, &trimming, iRun, debug_name
#endif /* SLIC3R_DEBUG */
    ] {
            out.second = support_grid_pattern.extract_support(grid_params.expansion_to_propagate, true
#ifdef SLIC3R_DEBUG
                , "support_projection", iRun, layer_id, layer.print_z
#endif // SLIC3R_DEBUG
            );
#ifdef SLIC3R_DEBUG
            Slic3r::SVG::export_expolygons(
                debug_out_path("support-projection_new-gridded-%d-%lf.svg", iRun, layer.print_z),
                union_ex(out.second));
#endif /* SLIC3R_DEBUG */
#ifdef SLIC3R_DEBUG
            SVG::export_expolygons(debug_out_path("support-projection_new-gridded-%d-%lf.svg", iRun, layer.print_z),
                { { { union_ex(trimming) },                             { "trimming",               "gray", 0.5f } },
                    { { union_safety_offset_ex(overhangs_projection) }, { "overhangs_projection",   "blue", 0.5f } },
                    { { union_safety_offset_ex(out.second) },           { "projection_new", "red",  "black", "", scaled<coord_t>(0.1f), 0.5f } } });
#endif /* SLIC3R_DEBUG */
        });

    task_group_inner.wait();
    return out;
}

// Generate bottom contact layers supporting the top contact layers.
// For a soluble interface material synchronize the layer heights with the object, 
// otherwise set the layer height to a bridging flow of a support interface nozzle.
PrintObjectSupportMaterial::MyLayersPtr PrintObjectSupportMaterial::bottom_contact_layers_and_layer_support_areas(
    const PrintObject &object, const MyLayersPtr &top_contacts, std::vector<Polygons> &buildplate_covered, 
    MyLayerStorage &layer_storage, std::vector<Polygons> &layer_support_areas) const
{
    if (top_contacts.empty())
        return MyLayersPtr();

#ifdef SLIC3R_DEBUG
    static size_t s_iRun = 0;
    size_t iRun = s_iRun ++;
#endif /* SLIC3R_DEBUG */

    //FIXME higher expansion_to_slice here? why?
    //const auto   expansion_to_slice = m_support_material_flow.scaled_spacing() / 2 + 25;
    const SupportGridParams grid_params(*m_object_config, m_support_params.support_material_flow);
    const bool buildplate_only = ! buildplate_covered.empty();

    // Allocate empty surface areas, one per object layer.
    layer_support_areas.assign(object.total_layer_count(), Polygons());

    // find object top surfaces
    // we'll use them to clip our support and detect where does it stick
    MyLayersPtr bottom_contacts;

    // There is some support to be built, if there are non-empty top surfaces detected.
    // Sum of unsupported contact areas above the current layer.print_z.
    Polygons  overhangs_projection;
    // Sum of unsupported enforcer contact areas above the current layer.print_z.
    // Only used if "supports on build plate only" is enabled and both automatic and support enforcers are enabled.
    Polygons  enforcers_projection;
    // Last top contact layer visited when collecting the projection of contact areas.
    int       contact_idx = int(top_contacts.size()) - 1;
    for (int layer_id = int(object.total_layer_count()) - 2; layer_id >= 0; -- layer_id) {
        BOOST_LOG_TRIVIAL(trace) << "Support generator - bottom_contact_layers - layer " << layer_id;
        const Layer &layer = *object.get_layer(layer_id);
        // Collect projections of all contact areas above or at the same level as this top surface.
#ifdef SLIC3R_DEBUG
        Polygons polygons_new;
        Polygons enforcers_new;
#endif // SLIC3R_DEBUG
        for (; contact_idx >= 0 && top_contacts[contact_idx]->print_z > layer.print_z - EPSILON; -- contact_idx) {
            MyLayer &top_contact = *top_contacts[contact_idx];
#ifndef SLIC3R_DEBUG
            Polygons polygons_new;
            Polygons enforcers_new;
#endif // SLIC3R_DEBUG
            // Contact surfaces are expanded away from the object, trimmed by the object.
            // Use a slight positive offset to overlap the touching regions.
#if 0
            // Merge and collect the contact polygons. The contact polygons are inflated, but not extended into a grid form.
            polygons_append(polygons_new,  offset(*top_contact.contact_polygons,  SCALED_EPSILON));
            if (top_contact.enforcer_polygons)
                polygons_append(enforcers_new, offset(*top_contact.enforcer_polygons, SCALED_EPSILON));
#else
            // Consume the contact_polygons. The contact polygons are already expanded into a grid form, and they are a tiny bit smaller
            // than the grid cells.
            polygons_append(polygons_new,  std::move(*top_contact.contact_polygons));
            if (top_contact.enforcer_polygons)
                polygons_append(enforcers_new, std::move(*top_contact.enforcer_polygons));
#endif
            // These are the overhang surfaces. They are touching the object and they are not expanded away from the object.
            // Use a slight positive offset to overlap the touching regions.
            polygons_append(polygons_new, expand(*top_contact.overhang_polygons, float(SCALED_EPSILON)));
            polygons_append(overhangs_projection, union_(polygons_new));
            polygons_append(enforcers_projection, enforcers_new);
        }
        if (overhangs_projection.empty() && enforcers_projection.empty())
            continue;

        // Overhangs_projection will be filled in asynchronously, move it away.
        Polygons overhangs_projection_raw = union_(std::move(overhangs_projection));
        Polygons enforcers_projection_raw = union_(std::move(enforcers_projection));

        tbb::task_group task_group;
        const Polygons &overhangs_for_bottom_contacts = buildplate_only ? enforcers_projection_raw : overhangs_projection_raw;
        if (! overhangs_for_bottom_contacts.empty())
            // Find the bottom contact layers above the top surfaces of this layer.
            task_group.run([this, &object, &layer, &top_contacts, contact_idx, &layer_storage, &layer_support_areas, &bottom_contacts, &overhangs_for_bottom_contacts
    #ifdef SLIC3R_DEBUG
                , iRun, &polygons_new
    #endif // SLIC3R_DEBUG
                ] {
                    // Find the bottom contact layers above the top surfaces of this layer.
                    MyLayer *layer_new = detect_bottom_contacts(
                        m_slicing_params, m_support_params, object, layer, top_contacts, contact_idx, layer_storage, layer_support_areas, overhangs_for_bottom_contacts
#ifdef SLIC3R_DEBUG
                        , iRun, polygons_new
#endif // SLIC3R_DEBUG
                    );
                    if (layer_new)
                        bottom_contacts.push_back(layer_new);
                });

        Polygons &layer_support_area = layer_support_areas[layer_id];
        Polygons *layer_buildplate_covered = buildplate_covered.empty() ? nullptr : &buildplate_covered[layer_id];
        // Filtering the propagated support columns to two extrusions, overlapping by maximum 20%.
//        float column_propagation_filtering_radius = scaled<float>(0.8 * 0.5 * (m_support_params.support_material_flow.spacing() + m_support_params.support_material_flow.width()));
        task_group.run([&grid_params, &overhangs_projection, &overhangs_projection_raw, &layer, &layer_support_area, layer_buildplate_covered /* , column_propagation_filtering_radius */
#ifdef SLIC3R_DEBUG 
            , iRun, layer_id
#endif /* SLIC3R_DEBUG */
            ] {
                // buildplate_covered[layer_id] will be consumed here.
                std::tie(layer_support_area, overhangs_projection) = project_support_to_grid(layer, grid_params, overhangs_projection_raw, layer_buildplate_covered
#ifdef SLIC3R_DEBUG 
                    , iRun, layer_id, "general"
#endif /* SLIC3R_DEBUG */
                );
                // When propagating support areas downwards, stop propagating the support column if it becomes too thin to be printable.
                //overhangs_projection = opening(overhangs_projection, column_propagation_filtering_radius);
            });

        Polygons layer_support_area_enforcers;
        if (! enforcers_projection.empty())
            // Project the enforcers polygons downwards, don't trim them with the "buildplate only" polygons.
            task_group.run([&grid_params, &enforcers_projection, &enforcers_projection_raw, &layer, &layer_support_area_enforcers
#ifdef SLIC3R_DEBUG 
                , iRun, layer_id
#endif /* SLIC3R_DEBUG */
            ]{
                std::tie(layer_support_area_enforcers, enforcers_projection) = project_support_to_grid(layer, grid_params, enforcers_projection_raw, nullptr
#ifdef SLIC3R_DEBUG 
                    , iRun, layer_id, "enforcers"
#endif /* SLIC3R_DEBUG */
                );
            });

        task_group.wait();

        if (! layer_support_area_enforcers.empty()) {
            if (layer_support_area.empty())
                layer_support_area = std::move(layer_support_area_enforcers);
            else
                layer_support_area = union_(layer_support_area, layer_support_area_enforcers);
        }
    } // over all layers downwards

    std::reverse(bottom_contacts.begin(), bottom_contacts.end());
    trim_support_layers_by_object(object, bottom_contacts, m_slicing_params.gap_support_object, m_slicing_params.gap_object_support, m_support_params.gap_xy);
    return bottom_contacts;
}

// FN_HIGHER_EQUAL: the provided object pointer has a Z value >= of an internal threshold.
// Find the first item with Z value >= of an internal threshold of fn_higher_equal.
// If no vec item with Z value >= of an internal threshold of fn_higher_equal is found, return vec.size()
// If the initial idx is size_t(-1), then use binary search.
// Otherwise search linearly upwards.
template<typename IteratorType, typename IndexType, typename FN_HIGHER_EQUAL>
IndexType idx_higher_or_equal(IteratorType begin, IteratorType end, IndexType idx, FN_HIGHER_EQUAL fn_higher_equal)
{
    auto size = int(end - begin);
    if (size == 0) {
        idx = 0;
    } else if (idx == IndexType(-1)) {
        // First of the batch of layers per thread pool invocation. Use binary search.
        int idx_low  = 0;
        int idx_high = std::max(0, size - 1);
        while (idx_low + 1 < idx_high) {
            int idx_mid  = (idx_low + idx_high) / 2;
            if (fn_higher_equal(begin[idx_mid]))
                idx_high = idx_mid;
            else
                idx_low  = idx_mid;
        }
        idx =  fn_higher_equal(begin[idx_low])  ? idx_low  :
              (fn_higher_equal(begin[idx_high]) ? idx_high : size);
    } else {
        // For the other layers of this batch of layers, search incrementally, which is cheaper than the binary search.
        while (int(idx) < size && ! fn_higher_equal(begin[idx]))
            ++ idx;
    }
    return idx;
}
template<typename T, typename IndexType, typename FN_HIGHER_EQUAL>
IndexType idx_higher_or_equal(const std::vector<T>& vec, IndexType idx, FN_HIGHER_EQUAL fn_higher_equal)
{
    return idx_higher_or_equal(vec.begin(), vec.end(), idx, fn_higher_equal);
}

// FN_LOWER_EQUAL: the provided object pointer has a Z value <= of an internal threshold.
// Find the first item with Z value <= of an internal threshold of fn_lower_equal.
// If no vec item with Z value <= of an internal threshold of fn_lower_equal is found, return -1.
// If the initial idx is < -1, then use binary search.
// Otherwise search linearly downwards.
template<typename IT, typename FN_LOWER_EQUAL>
int idx_lower_or_equal(IT begin, IT end, int idx, FN_LOWER_EQUAL fn_lower_equal)
{
    auto size = int(end - begin);
    if (size == 0) {
        idx = -1;
    } else if (idx < -1) {
        // First of the batch of layers per thread pool invocation. Use binary search.
        int idx_low  = 0;
        int idx_high = std::max(0, size - 1);
        while (idx_low + 1 < idx_high) {
            int idx_mid  = (idx_low + idx_high) / 2;
            if (fn_lower_equal(begin[idx_mid]))
                idx_low  = idx_mid;
            else
                idx_high = idx_mid;
        }
        idx =  fn_lower_equal(begin[idx_high]) ? idx_high :
              (fn_lower_equal(begin[idx_low ]) ? idx_low  : -1);
    } else {
        // For the other layers of this batch of layers, search incrementally, which is cheaper than the binary search.
        while (idx >= 0 && ! fn_lower_equal(begin[idx]))
            -- idx;
    }
    return idx;
}
template<typename T, typename FN_LOWER_EQUAL>
int idx_lower_or_equal(const std::vector<T*> &vec, int idx, FN_LOWER_EQUAL fn_lower_equal)
{
    return idx_lower_or_equal(vec.begin(), vec.end(), idx, fn_lower_equal);
}

// Trim the top_contacts layers with the bottom_contacts layers if they overlap, so there would not be enough vertical space for both of them.
void PrintObjectSupportMaterial::trim_top_contacts_by_bottom_contacts(
    const PrintObject &object, const MyLayersPtr &bottom_contacts, MyLayersPtr &top_contacts) const
{
    tbb::parallel_for(tbb::blocked_range<int>(0, int(top_contacts.size())),
        [&bottom_contacts, &top_contacts](const tbb::blocked_range<int>& range) {
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
               extremes[i]->extreme_z() - extremes[i-1]->extreme_z() > m_support_params.support_layer_height_min - EPSILON);
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
    size_t idx_extreme_first = 0;
    if (! extremes.empty() && std::abs(extremes.front()->extreme_z() - m_slicing_params.raft_interface_top_z) < EPSILON) {
        // This is a raft contact layer, its height has been decided in this->top_contact_layers().
        // Ignore this layer when calculating the intermediate support layers.
        assert(extremes.front()->layer_type == sltTopContact);
        ++ idx_extreme_first;
    }
    for (size_t idx_extreme = idx_extreme_first; idx_extreme < extremes.size(); ++ idx_extreme) {
        MyLayer      *extr2  = extremes[idx_extreme];
        coordf_t      extr2z = extr2->extreme_z();
        if (std::abs(extr2z - m_slicing_params.first_print_layer_height) < EPSILON) {
            // This is a bottom of a synchronized (or soluble) top contact layer, its height has been decided in this->top_contact_layers().
            assert(extr2->layer_type == sltTopContact);
            assert(extr2->bottom_z == m_slicing_params.first_print_layer_height);
            assert(extr2->print_z >= m_slicing_params.first_print_layer_height + m_support_params.support_layer_height_min - EPSILON);
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
        MyLayer      *extr1  = (idx_extreme == idx_extreme_first) ? nullptr : extremes[idx_extreme - 1];
        // Fuse a support layer firmly to the raft top interface (not to the raft contacts).
        coordf_t      extr1z = (extr1 == nullptr) ? m_slicing_params.raft_interface_top_z : extr1->extreme_z();
        assert(extr2z >= extr1z);
        assert(extr2z > extr1z || (extr1 != nullptr && extr2->layer_type == sltBottomContact));
        if (std::abs(extr1z) < EPSILON) {
            // This layer interval starts with the 1st layer. Print the 1st layer using the prescribed 1st layer thickness.
            // assert(! m_slicing_params.has_raft()); RaftingEdition: unclear where the issue is: assert fails with 1-layer raft & base supports
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
        assert(dist >= m_support_params.support_layer_height_min - EPSILON);
        if (synchronize) {
            // Emit support layers synchronized with the object layers.
            // Find the first object layer, which has its print_z in this support Z range.
            while (idx_layer_object < object.layers().size() && object.layers()[idx_layer_object]->print_z < extr1z + EPSILON)
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
            for (; idx_layer_object < object.layers().size() && object.layers()[idx_layer_object]->print_z < extr2z + EPSILON; ++ idx_layer_object) {
                MyLayer &layer_new = layer_allocate(layer_storage, sltIntermediate);
                layer_new.print_z  = object.layers()[idx_layer_object]->print_z;
                layer_new.height   = object.layers()[idx_layer_object]->height;
                layer_new.bottom_z = (idx_layer_object > 0) ? object.layers()[idx_layer_object - 1]->print_z : (layer_new.print_z - layer_new.height);
                assert(intermediate_layers.empty() || intermediate_layers.back()->print_z < layer_new.print_z + EPSILON);
                intermediate_layers.push_back(&layer_new);
            }
        } else {
            // Insert intermediate layers.
            size_t        n_layers_extra = size_t(ceil(dist / m_slicing_params.max_suport_layer_height)); 
            assert(n_layers_extra > 0);
            coordf_t      step   = dist / coordf_t(n_layers_extra);
            if (extr1 != nullptr && extr1->layer_type == sltTopContact &&
                extr1->print_z + m_support_params.support_layer_height_min > extr1->bottom_z + step) {
                // The bottom extreme is a bottom of a top surface. Ensure that the gap 
                // between the 1st intermediate layer print_z and extr1->print_z is not too small.
                assert(extr1->bottom_z + m_support_params.support_layer_height_min < extr1->print_z + EPSILON);
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

    BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::generate_base_layers() in parallel - start";
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, intermediate_layers.size()),
        [&object, &bottom_contacts, &top_contacts, &intermediate_layers, &layer_support_areas](const tbb::blocked_range<size_t>& range) {
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
                // New polygons for layer_intermediate.
                Polygons polygons_new;

                // Use the precomputed layer_support_areas. "idx_object_layer_above": above means above since the last iteration, not above after this call.
                idx_object_layer_above = idx_lower_or_equal(object.layers().begin(), object.layers().end(), idx_object_layer_above,
                    [&layer_intermediate](const Layer* layer) { return layer->print_z <= layer_intermediate.print_z + EPSILON; });

                // Polygons to trim polygons_new.
                Polygons polygons_trimming; 

                // Trimming the base layer with any overlapping top layer.
                // Following cases are recognized:
                // 1) top.bottom_z >= base.top_z -> No overlap, no trimming needed.
                // 2) base.bottom_z >= top.print_z -> No overlap, no trimming needed.
                // 3) base.print_z > top.print_z  && base.bottom_z >= top.bottom_z -> Overlap, which will be solved inside generate_toolpaths() by reducing the base layer height where it overlaps the top layer. No trimming needed here.
                // 4) base.print_z > top.bottom_z && base.bottom_z < top.bottom_z -> Base overlaps with top.bottom_z. This must not happen.
                // 5) base.print_z <= top.print_z  && base.bottom_z >= top.bottom_z -> Base is fully inside top. Trim base by top.
                idx_top_contact_above = idx_lower_or_equal(top_contacts, idx_top_contact_above, 
                    [&layer_intermediate](const MyLayer *layer){ return layer->bottom_z <= layer_intermediate.print_z - EPSILON; });
                // Collect all the top_contact layer intersecting with this layer.
                for (int idx_top_contact_overlapping = idx_top_contact_above; idx_top_contact_overlapping >= 0; -- idx_top_contact_overlapping) {
                    MyLayer &layer_top_overlapping = *top_contacts[idx_top_contact_overlapping];
                    if (layer_top_overlapping.print_z < layer_intermediate.bottom_z + EPSILON)
                        break;
                    // Base must not overlap with top.bottom_z.
                    assert(! (layer_intermediate.print_z > layer_top_overlapping.bottom_z + EPSILON && layer_intermediate.bottom_z < layer_top_overlapping.bottom_z - EPSILON));
                    if (layer_intermediate.print_z <= layer_top_overlapping.print_z + EPSILON && layer_intermediate.bottom_z >= layer_top_overlapping.bottom_z - EPSILON)
                        // Base is fully inside top. Trim base by top.
                        polygons_append(polygons_trimming, layer_top_overlapping.polygons);
                }

                if (idx_object_layer_above < 0) {
                    // layer_support_areas are synchronized with object layers and they contain projections of the contact layers above them.
                    // This intermediate layer is not above any object layer, thus there is no information in layer_support_areas about
                    // towers supporting contact layers intersecting the first object layer. Project these contact layers now.
                    polygons_new = layer_support_areas.front();
                    double first_layer_z = object.layers().front()->print_z;
                    for (int i = idx_top_contact_above + 1; i < int(top_contacts.size()); ++ i) {
                        MyLayer &contacts = *top_contacts[i];
                        if (contacts.print_z > first_layer_z + EPSILON)
                            break;
                        assert(contacts.bottom_z > layer_intermediate.print_z - EPSILON);
                        polygons_append(polygons_new, contacts.polygons);
                    }
                } else
                    polygons_new = layer_support_areas[idx_object_layer_above];

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
                    svg.draw(union_ex(polygons_new),                    "blue", 0.5f);
                    svg.draw(to_polylines(polygons_new),                "blue");
                    svg.draw(union_safety_offset_ex(polygons_trimming), "red", 0.5f);
                    svg.draw(to_polylines(polygons_trimming),           "red");
                }
        #endif /* SLIC3R_DEBUG */

                // Trim the polygons, store them.
                if (polygons_trimming.empty())
                    layer_intermediate.polygons = std::move(polygons_new);
                else
                    layer_intermediate.polygons = diff(
                        polygons_new,
                        polygons_trimming,
                        ApplySafetyOffset::Yes); // safety offset to merge the touching source polygons
                layer_intermediate.layer_type = sltBase;

        #if 0
                    // coordf_t fillet_radius_scaled = scale_(m_object_config->support_material_spacing);
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
            union_ex((*it)->polygons));
    ++ iRun;
#endif /* SLIC3R_DEBUG */

    this->trim_support_layers_by_object(object, intermediate_layers, m_slicing_params.gap_support_object, m_slicing_params.gap_object_support, m_support_params.gap_xy);
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
                coordf_t z_threshold = support_layer.bottom_print_z() - gap_extra_below + EPSILON;
                idx_object_layer_overlapping = idx_higher_or_equal(
                    object.layers().begin(), object.layers().end(), idx_object_layer_overlapping,
                    [z_threshold](const Layer *layer){ return layer->print_z >= z_threshold; });
                // Collect all the object layers intersecting with this layer.
                Polygons polygons_trimming;
                size_t i = idx_object_layer_overlapping;
                for (; i < object.layers().size(); ++ i) {
                    const Layer &object_layer = *object.layers()[i];
                    if (object_layer.bottom_z() > support_layer.print_z + gap_extra_above - EPSILON)
                        break;
                    polygons_append(polygons_trimming, offset(object_layer.lslices, gap_xy_scaled, SUPPORT_SURFACES_OFFSET_PARAMETERS));
                }
                if (! m_slicing_params.soluble_interface && m_object_config->thick_bridges) {
                    // Collect all bottom surfaces, which will be extruded with a bridging flow.
                    for (; i < object.layers().size(); ++ i) {
                        const Layer &object_layer = *object.layers()[i];
                        bool some_region_overlaps = false;
                        for (LayerRegion *region : object_layer.regions()) {
                            coordf_t bridging_height = region->region().bridging_height_avg(*m_print_config);
                            if (object_layer.print_z - bridging_height > support_layer.print_z + gap_extra_above - EPSILON)
                                break;
                            some_region_overlaps = true;
                            polygons_append(polygons_trimming, 
                                offset(region->fill_surfaces.filter_by_type(stBottomBridge), gap_xy_scaled, SUPPORT_SURFACES_OFFSET_PARAMETERS));
                            if (region->region().config().overhangs.value)
                                // Add bridging perimeters.
                                SupportMaterialInternal::collect_bridging_perimeter_areas(region->perimeters, gap_xy_scaled, polygons_trimming);
                        }
                        if (! some_region_overlaps)
                            break;
                    }
                }
                // $layer->slices contains the full shape of layer, thus including
                // perimeter's width. $support contains the full shape of support
                // material, thus including the width of its foremost extrusion.
                // We leave a gap equal to a full extrusion width.
                support_layer.polygons = diff(support_layer.polygons, polygons_trimming);
            }
        });
    BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::trim_support_layers_by_object() in parallel - end";
}

PrintObjectSupportMaterial::MyLayersPtr PrintObjectSupportMaterial::generate_raft_base(
    const PrintObject   &object,
    const MyLayersPtr   &top_contacts,
    const MyLayersPtr   &interface_layers,
    const MyLayersPtr   &base_interface_layers,
    const MyLayersPtr   &base_layers,
    MyLayerStorage      &layer_storage) const
{
    // If there is brim to be generated, calculate the trimming regions.
    Polygons brim;
    if (object.has_brim()) {
        // Calculate the area covered by the brim.
        const BrimType brim_type       = object.config().brim_type;
        const bool     brim_outer      = brim_type == btOuterOnly || brim_type == btOuterAndInner;
        const bool     brim_inner      = brim_type == btInnerOnly || brim_type == btOuterAndInner;
        const auto     brim_separation = scaled<float>(object.config().brim_separation.value + object.config().brim_width.value);
        for (const ExPolygon &ex : object.layers().front()->lslices) {
            if (brim_outer && brim_inner)
                polygons_append(brim, offset(ex, brim_separation));
            else {
                if (brim_outer)
                    polygons_append(brim, offset(ex.contour, brim_separation, ClipperLib::jtRound, float(scale_(0.1))));
                else
                    brim.emplace_back(ex.contour);
                if (brim_inner) {
                    Polygons holes = ex.holes;
                    polygons_reverse(holes);
                    holes = shrink(holes, brim_separation, ClipperLib::jtRound, float(scale_(0.1)));
                    polygons_reverse(holes);
                    polygons_append(brim, std::move(holes));
                } else
                    polygons_append(brim, ex.holes);
            }
        }
        brim = union_(brim);
    }

    // How much to inflate the support columns to be stable. This also applies to the 1st layer, if no raft layers are to be printed.
    const float inflate_factor_fine      = float(scale_((m_slicing_params.raft_layers() > 1) ? 0.5 : EPSILON));
    const float inflate_factor_1st_layer = std::max(0.f, float(scale_(object.config().raft_first_layer_expansion)) - inflate_factor_fine);
    MyLayer       *contacts         = top_contacts         .empty() ? nullptr : top_contacts         .front();
    MyLayer       *interfaces       = interface_layers     .empty() ? nullptr : interface_layers     .front();
    MyLayer       *base_interfaces  = base_interface_layers.empty() ? nullptr : base_interface_layers.front();
    MyLayer       *columns_base     = base_layers          .empty() ? nullptr : base_layers          .front();
    if (contacts != nullptr && contacts->print_z > std::max(m_slicing_params.first_print_layer_height, m_slicing_params.raft_contact_top_z) + EPSILON)
        // This is not the raft contact layer.
        contacts = nullptr;
    if (interfaces != nullptr && interfaces->bottom_print_z() > m_slicing_params.raft_interface_top_z + EPSILON)
        // This is not the raft column base layer.
        interfaces = nullptr;
    if (base_interfaces != nullptr && base_interfaces->bottom_print_z() > m_slicing_params.raft_interface_top_z + EPSILON)
        // This is not the raft column base layer.
        base_interfaces = nullptr;
    if (columns_base != nullptr && columns_base->bottom_print_z() > m_slicing_params.raft_interface_top_z + EPSILON)
        // This is not the raft interface layer.
        columns_base = nullptr;

    Polygons interface_polygons;
    if (contacts != nullptr && ! contacts->polygons.empty())
        polygons_append(interface_polygons, expand(contacts->polygons, inflate_factor_fine, SUPPORT_SURFACES_OFFSET_PARAMETERS));
    if (interfaces != nullptr && ! interfaces->polygons.empty())
        polygons_append(interface_polygons, expand(interfaces->polygons, inflate_factor_fine, SUPPORT_SURFACES_OFFSET_PARAMETERS));
    if (base_interfaces != nullptr && ! base_interfaces->polygons.empty())
        polygons_append(interface_polygons, expand(base_interfaces->polygons, inflate_factor_fine, SUPPORT_SURFACES_OFFSET_PARAMETERS));
 
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
            new_layer.polygons = inflate_factor_1st_layer > 0 ? expand(base, inflate_factor_1st_layer) : base;
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
            new_layer.contact_polygons = std::make_unique<Polygons>(columns);
        }
    } else {
        if (columns_base != nullptr) {
        // Expand the bases of the support columns in the 1st layer.
            Polygons &raft     = columns_base->polygons;
            Polygons  trimming = offset(m_object->layers().front()->lslices, (float)scale_(m_support_params.gap_xy), SUPPORT_SURFACES_OFFSET_PARAMETERS);
            if (inflate_factor_1st_layer > SCALED_EPSILON) {
                // Inflate in multiple steps to avoid leaking of the support 1st layer through object walls.
                auto  nsteps = std::max(5, int(ceil(inflate_factor_1st_layer / m_support_params.first_layer_flow.scaled_width())));
                float step   = inflate_factor_1st_layer / nsteps;
                for (int i = 0; i < nsteps; ++ i)
                    raft = diff(expand(raft, step), trimming);
            } else
                raft = diff(raft, trimming);
            if (contacts != nullptr)
                columns_base->polygons = diff(columns_base->polygons, interface_polygons);
        }
        if (! brim.empty()) {
            if (columns_base)
                columns_base->polygons = diff(columns_base->polygons, brim);
            if (contacts)
                contacts->polygons = diff(contacts->polygons, brim);
            if (interfaces)
                interfaces->polygons = diff(interfaces->polygons, brim);
            if (base_interfaces)
                base_interfaces->polygons = diff(base_interfaces->polygons, brim);
        }
    }

    return raft_layers;
}

// Convert some of the intermediate layers into top/bottom interface layers as well as base interface layers.
std::pair<PrintObjectSupportMaterial::MyLayersPtr, PrintObjectSupportMaterial::MyLayersPtr> PrintObjectSupportMaterial::generate_interface_layers(
    const MyLayersPtr   &bottom_contacts,
    const MyLayersPtr   &top_contacts,
    MyLayersPtr         &intermediate_layers,
    MyLayerStorage      &layer_storage) const
{
//    my $area_threshold = $self->interface_flow->scaled_spacing ** 2;

    std::pair<MyLayersPtr, MyLayersPtr> base_and_interface_layers;
    MyLayersPtr &interface_layers       = base_and_interface_layers.first;
    MyLayersPtr &base_interface_layers  = base_and_interface_layers.second;

    // distinguish between interface and base interface layers
    // Contact layer is considered an interface layer, therefore run the following block only if support_material_interface_layers > 1.
    // Contact layer needs a base_interface layer, therefore run the following block if support_material_interface_layers > 0, has soluble support and extruders are different.
    bool   soluble_interface_non_soluble_base =
        // Zero z-gap between the overhangs and the support interface.
        m_slicing_params.soluble_interface && 
        // Interface extruder soluble.
        m_object_config->support_material_interface_extruder.value > 0 && m_print_config->filament_soluble.get_at(m_object_config->support_material_interface_extruder.value - 1) && 
        // Base extruder: Either "print with active extruder" not soluble.
        (m_object_config->support_material_extruder.value == 0 || ! m_print_config->filament_soluble.get_at(m_object_config->support_material_extruder.value - 1));
    bool   snug_supports                 = m_object_config->support_material_style.value == smsSnug;
    int num_interface_layers_top         = m_object_config->support_material_interface_layers;
    int num_interface_layers_bottom      = m_object_config->support_material_bottom_interface_layers;
    if (num_interface_layers_bottom < 0)
        num_interface_layers_bottom = num_interface_layers_top;
    int num_base_interface_layers_top    = soluble_interface_non_soluble_base ? std::min(num_interface_layers_top / 2, 2) : 0;
    int num_base_interface_layers_bottom = soluble_interface_non_soluble_base ? std::min(num_interface_layers_bottom / 2, 2) : 0;

    if (! intermediate_layers.empty() && (num_interface_layers_top > 1 || num_interface_layers_bottom > 1)) {
        // For all intermediate layers, collect top contact surfaces, which are not further than support_material_interface_layers.
        BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::generate_interface_layers() in parallel - start";
        // Since the intermediate layer index starts at zero the number of interface layer needs to be reduced by 1.
        -- num_interface_layers_top;
        -- num_interface_layers_bottom;
        int num_interface_layers_only_top    = num_interface_layers_top    - num_base_interface_layers_top;
        int num_interface_layers_only_bottom = num_interface_layers_bottom - num_base_interface_layers_bottom;
        interface_layers.assign(intermediate_layers.size(), nullptr);
        if (num_base_interface_layers_top || num_base_interface_layers_bottom)
            base_interface_layers.assign(intermediate_layers.size(), nullptr);
        auto smoothing_distance              = m_support_params.support_material_interface_flow.scaled_spacing() * 1.5;
        auto minimum_island_radius           = m_support_params.support_material_interface_flow.scaled_spacing() / m_support_params.interface_density;
        auto closing_distance                = smoothing_distance; // scaled<float>(m_object_config->support_material_closing_radius.value);
        tbb::spin_mutex layer_storage_mutex;
        // Insert a new layer into base_interface_layers, if intersection with base exists.
        auto insert_layer = [&layer_storage, &layer_storage_mutex, snug_supports, closing_distance, smoothing_distance, minimum_island_radius](
                MyLayer &intermediate_layer, Polygons &bottom, Polygons &&top, const Polygons *subtract, SupporLayerType type) -> MyLayer* {
            assert(! bottom.empty() || ! top.empty());
            // Merge top into bottom, unite them with a safety offset.
            append(bottom, std::move(top));
            // Merge top / bottom interfaces. For snug supports, merge using closing distance and regularize (close concave corners).
            bottom = intersection(
                snug_supports ?
                    smooth_outward(closing(std::move(bottom), closing_distance + minimum_island_radius, closing_distance, SUPPORT_SURFACES_OFFSET_PARAMETERS), smoothing_distance) :
                    union_safety_offset(std::move(bottom)),
                intermediate_layer.polygons);
            if (! bottom.empty()) {
                //FIXME Remove non-printable tiny islands, let them be printed using the base support.
                //bottom = opening(std::move(bottom), minimum_island_radius);
                if (! bottom.empty()) {
                    MyLayer &layer_new = layer_allocate(layer_storage, layer_storage_mutex, type);
                    layer_new.polygons   = std::move(bottom);
                    layer_new.print_z    = intermediate_layer.print_z;
                    layer_new.bottom_z   = intermediate_layer.bottom_z;
                    layer_new.height     = intermediate_layer.height;
                    layer_new.bridging   = intermediate_layer.bridging;
                    // Subtract the interface from the base regions.
                    intermediate_layer.polygons = diff(intermediate_layer.polygons, layer_new.polygons);
                    if (subtract)
                        // Trim the base interface layer with the interface layer.
                        layer_new.polygons = diff(std::move(layer_new.polygons), *subtract);
                    //FIXME filter layer_new.polygons islands by a minimum area?
        //                  $interface_area = [ grep abs($_->area) >= $area_threshold, @$interface_area ];
                    return &layer_new;
                }
            }
            return nullptr;
        };
        tbb::parallel_for(tbb::blocked_range<int>(0, int(intermediate_layers.size())),
            [&bottom_contacts, &top_contacts, &intermediate_layers, &insert_layer, 
             num_interface_layers_top, num_interface_layers_bottom, num_base_interface_layers_top, num_base_interface_layers_bottom, num_interface_layers_only_top, num_interface_layers_only_bottom,
             snug_supports, &interface_layers, &base_interface_layers](const tbb::blocked_range<int>& range) {                
                // Gather the top / bottom contact layers intersecting with num_interface_layers resp. num_interface_layers_only intermediate layers above / below
                // this intermediate layer.
                // Index of the first top contact layer intersecting the current intermediate layer.
                auto idx_top_contact_first      = -1;
                // Index of the first bottom contact layer intersecting the current intermediate layer.
                auto idx_bottom_contact_first   = -1;
                auto num_intermediate = int(intermediate_layers.size());
                for (int idx_intermediate_layer = range.begin(); idx_intermediate_layer < range.end(); ++ idx_intermediate_layer) {
                    MyLayer &intermediate_layer = *intermediate_layers[idx_intermediate_layer];
                    Polygons polygons_top_contact_projected_interface;
                    Polygons polygons_top_contact_projected_base;
                    Polygons polygons_bottom_contact_projected_interface;
                    Polygons polygons_bottom_contact_projected_base;
                    if (num_interface_layers_top > 0) {
                        // Top Z coordinate of a slab, over which we are collecting the top / bottom contact surfaces
                        coordf_t top_z              = intermediate_layers[std::min(num_intermediate - 1, idx_intermediate_layer + num_interface_layers_top - 1)]->print_z;
                        coordf_t top_inteface_z     = std::numeric_limits<coordf_t>::max();
                        if (num_base_interface_layers_top > 0)
                            // Some top base interface layers will be generated.
                            top_inteface_z = num_interface_layers_only_top == 0 ?
                                // Only base interface layers to generate.
                                - std::numeric_limits<coordf_t>::max() :
                                intermediate_layers[std::min(num_intermediate - 1, idx_intermediate_layer + num_interface_layers_only_top - 1)]->print_z;
                        // Move idx_top_contact_first up until above the current print_z.
                        idx_top_contact_first = idx_higher_or_equal(top_contacts, idx_top_contact_first, [&intermediate_layer](const MyLayer *layer){ return layer->print_z >= intermediate_layer.print_z; }); //  - EPSILON
                        // Collect the top contact areas above this intermediate layer, below top_z.
                        for (int idx_top_contact = idx_top_contact_first; idx_top_contact < int(top_contacts.size()); ++ idx_top_contact) {
                            const MyLayer &top_contact_layer = *top_contacts[idx_top_contact];
                            //FIXME maybe this adds one interface layer in excess?
                            if (top_contact_layer.bottom_z - EPSILON > top_z)
                                break;
                            polygons_append(top_contact_layer.bottom_z - EPSILON > top_inteface_z ? polygons_top_contact_projected_base : polygons_top_contact_projected_interface, 
                                // For snug supports, project the overhang polygons covering the whole overhang, so that they will merge without a gap with support polygons of the other layers.
                                // For grid supports, merging of support regions will be performed by the projection into grid.
                                snug_supports ? *top_contact_layer.overhang_polygons : top_contact_layer.polygons);
                        }
                    }
                    if (num_interface_layers_bottom > 0) {
                        // Bottom Z coordinate of a slab, over which we are collecting the top / bottom contact surfaces
                        coordf_t bottom_z           = intermediate_layers[std::max(0, idx_intermediate_layer - num_interface_layers_bottom + 1)]->bottom_z;
                        coordf_t bottom_interface_z = - std::numeric_limits<coordf_t>::max();
                        if (num_base_interface_layers_bottom > 0)
                            // Some bottom base interface layers will be generated.
                            bottom_interface_z = num_interface_layers_only_bottom == 0 ? 
                                // Only base interface layers to generate.
                                std::numeric_limits<coordf_t>::max() :
                                intermediate_layers[std::max(0, idx_intermediate_layer - num_interface_layers_only_bottom)]->bottom_z;
                        // Move idx_bottom_contact_first up until touching bottom_z.
                        idx_bottom_contact_first = idx_higher_or_equal(bottom_contacts, idx_bottom_contact_first, [bottom_z](const MyLayer *layer){ return layer->print_z >= bottom_z - EPSILON; });
                        // Collect the top contact areas above this intermediate layer, below top_z.
                        for (int idx_bottom_contact = idx_bottom_contact_first; idx_bottom_contact < int(bottom_contacts.size()); ++ idx_bottom_contact) {
                            const MyLayer &bottom_contact_layer = *bottom_contacts[idx_bottom_contact];
                            if (bottom_contact_layer.print_z - EPSILON > intermediate_layer.bottom_z)
                                break;
                            polygons_append(bottom_contact_layer.print_z - EPSILON > bottom_interface_z ? polygons_bottom_contact_projected_interface : polygons_bottom_contact_projected_base, bottom_contact_layer.polygons);
                        }
                    }
                    MyLayer *interface_layer = nullptr;
                    if (! polygons_bottom_contact_projected_interface.empty() || ! polygons_top_contact_projected_interface.empty()) {
                        interface_layer = insert_layer(
                            intermediate_layer, polygons_bottom_contact_projected_interface, std::move(polygons_top_contact_projected_interface), nullptr,
                            polygons_top_contact_projected_interface.empty() ? sltBottomInterface : sltTopInterface);
                        interface_layers[idx_intermediate_layer] = interface_layer;
                    }
                    if (! polygons_bottom_contact_projected_base.empty() || ! polygons_top_contact_projected_base.empty())
                        base_interface_layers[idx_intermediate_layer] = insert_layer(
                            intermediate_layer, polygons_bottom_contact_projected_base, std::move(polygons_top_contact_projected_base), 
                            interface_layer ? &interface_layer->polygons : nullptr, sltBase);
                }
            });

        // Compress contact_out, remove the nullptr items.
        remove_nulls(interface_layers);
        remove_nulls(base_interface_layers);
        BOOST_LOG_TRIVIAL(debug) << "PrintObjectSupportMaterial::generate_interface_layers() in parallel - end";
    }
    
    return base_and_interface_layers;
}

static inline void fill_expolygon_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    ExPolygon              &&expolygon,
    Fill                    *filler,
    const FillParams        &fill_params,
    float                    density,
    ExtrusionRole            role,
    const Flow              &flow)
{
    Surface surface(stInternal, std::move(expolygon));
    Polylines polylines;
    try {
        polylines = filler->fill_surface(&surface, fill_params);
    } catch (InfillFailedException &) {
    }
    extrusion_entities_append_paths(
        dst,
        std::move(polylines),
        role,
        flow.mm3_per_mm(), flow.width(), flow.height());
}

static inline void fill_expolygons_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    ExPolygons             &&expolygons,
    Fill                    *filler,
    const FillParams        &fill_params,
    float                    density,
    ExtrusionRole            role,
    const Flow              &flow)
{
    for (ExPolygon &expoly : expolygons)
        fill_expolygon_generate_paths(dst, std::move(expoly), filler, fill_params, density, role, flow);
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
    fill_params.density     = density;
    fill_params.dont_adjust = true;
    fill_expolygons_generate_paths(dst, std::move(expolygons), filler, fill_params, density, role, flow);
}

static inline void fill_expolygons_with_sheath_generate_paths(
    ExtrusionEntitiesPtr    &dst,
    const Polygons          &polygons,
    Fill                    *filler,
    float                    density,
    ExtrusionRole            role,
    const Flow              &flow,
    bool                     with_sheath,
    bool                     no_sort)
{
    if (polygons.empty())
        return;

    if (! with_sheath) {
        fill_expolygons_generate_paths(dst, closing_ex(polygons, float(SCALED_EPSILON)), filler, density, role, flow);
        return;
    }

    FillParams fill_params;
    fill_params.density     = density;
    fill_params.dont_adjust = true;

    double spacing = flow.scaled_spacing();
    // Clip the sheath path to avoid the extruder to get exactly on the first point of the loop.
    double clip_length = spacing * 0.15;

    for (ExPolygon &expoly : closing_ex(polygons, float(SCALED_EPSILON), float(SCALED_EPSILON + 0.5*flow.scaled_width()))) {
        // Don't reorder the skirt and its infills.
        std::unique_ptr<ExtrusionEntityCollection> eec;
        if (no_sort) {
            eec = std::make_unique<ExtrusionEntityCollection>();
            eec->no_sort = true;
        }
        ExtrusionEntitiesPtr &out = no_sort ? eec->entities : dst;
        // Draw the perimeters.
        Polylines polylines;
        polylines.reserve(expoly.holes.size() + 1);
        for (size_t i = 0; i <= expoly.holes.size();  ++ i) {
            Polyline pl(i == 0 ? expoly.contour.points : expoly.holes[i - 1].points);
            pl.points.emplace_back(pl.points.front());
            pl.clip_end(clip_length);
            polylines.emplace_back(std::move(pl));
        }
        extrusion_entities_append_paths(out, polylines, erSupportMaterial, flow.mm3_per_mm(), flow.width(), flow.height());
        // Fill in the rest.
        fill_expolygons_generate_paths(out, offset_ex(expoly, float(-0.4 * spacing)), filler, fill_params, density, role, flow);
        if (no_sort && ! eec->empty())
            dst.emplace_back(eec.release());
    }
}

// Support layers, partially processed.
struct MyLayerExtruded
{
    MyLayerExtruded& operator=(MyLayerExtruded &&rhs) {
        this->layer = rhs.layer;
        this->extrusions = std::move(rhs.extrusions);
        m_polygons_to_extrude = std::move(rhs.m_polygons_to_extrude);
        rhs.layer = nullptr;
        return *this;
    }

    bool empty() const {
        return layer == nullptr || layer->polygons.empty();
    }

    void set_polygons_to_extrude(Polygons &&polygons) { 
        if (m_polygons_to_extrude == nullptr) 
            m_polygons_to_extrude = std::make_unique<Polygons>(std::move(polygons));
        else
            *m_polygons_to_extrude = std::move(polygons);
    }
    Polygons& polygons_to_extrude() { return (m_polygons_to_extrude == nullptr) ? layer->polygons : *m_polygons_to_extrude; }
    const Polygons& polygons_to_extrude() const { return (m_polygons_to_extrude == nullptr) ? layer->polygons : *m_polygons_to_extrude; }

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
            if (m_polygons_to_extrude == nullptr) {
                // This layer has no extrusions generated yet, if it has no m_polygons_to_extrude (its area to extrude was not reduced yet).
                assert(this->extrusions.empty());
                m_polygons_to_extrude = std::make_unique<Polygons>(this->layer->polygons);
            }
            Slic3r::polygons_append(*m_polygons_to_extrude, std::move(*other.m_polygons_to_extrude));
            *m_polygons_to_extrude = union_safety_offset(*m_polygons_to_extrude);
            other.m_polygons_to_extrude.reset();
        } else if (m_polygons_to_extrude != nullptr) {
            assert(other.m_polygons_to_extrude == nullptr);
            // The other layer has no extrusions generated yet, if it has no m_polygons_to_extrude (its area to extrude was not reduced yet).
            assert(other.extrusions.empty());
            Slic3r::polygons_append(*m_polygons_to_extrude, other.layer->polygons);
            *m_polygons_to_extrude = union_safety_offset(*m_polygons_to_extrude);
        }
        // 2) Merge the extrusions.
        this->extrusions.insert(this->extrusions.end(), other.extrusions.begin(), other.extrusions.end());
        other.extrusions.clear();
        // 3) Merge the infill polygons.
        Slic3r::polygons_append(this->layer->polygons, std::move(other.layer->polygons));
        this->layer->polygons = union_safety_offset(this->layer->polygons);
        other.layer->polygons.clear();
    }

    void polygons_append(Polygons &dst) const {
        if (layer != NULL && ! layer->polygons.empty())
            Slic3r::polygons_append(dst, layer->polygons);
    }

    // The source layer. It carries the height and extrusion type (bridging / non bridging, extrusion height).
    PrintObjectSupportMaterial::MyLayer  *layer { nullptr };
    // Collect extrusions. They will be exported sorted by the bottom height.
    ExtrusionEntitiesPtr                  extrusions;

private:
    // In case the extrusions are non-empty, m_polygons_to_extrude may contain the rest areas yet to be filled by additional support.
    // This is useful mainly for the loop interfaces, which are generated before the zig-zag infills.
    std::unique_ptr<Polygons>             m_polygons_to_extrude;
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

    Flow flow = interface_flow_src.with_height(top_contact_layer.layer->height);

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
                        const Vec2d v_seg(coordf_t(p2(0)) - coordf_t(p1(0)), coordf_t(p2(1)) - coordf_t(p1(1)));
                        const Vec2d v_cntr(coordf_t(p1(0) - center_last(0)), coordf_t(p1(1) - center_last(1)));
                        coordf_t a = v_seg.squaredNorm();
                        coordf_t b = 2. * v_seg.dot(v_cntr);
                        coordf_t c = v_cntr.squaredNorm() - circle_distance * circle_distance;
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
                            center_last    = Point(p1(0) + coord_t(v_seg(0) * t), p1(1) + coord_t(v_seg(1) * t));
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
                    for (const Point &center : circle_centers) {
                        circles.push_back(circle);
                        circles.back().translate(center);
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
        for (int i = 1; i < n_contact_loops; ++ i)
            polygons_append(loop_polygons,
                opening(
                    loops0, 
                    i * flow.scaled_spacing() + 0.5f * flow.scaled_spacing(),
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
        loop_lines = intersection_pl(loop_lines, expand(overhang_polygons, scale_(SUPPORT_MATERIAL_MARGIN)));
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
        std::move(loop_lines),
        erSupportMaterialInterface, flow.mm3_per_mm(), flow.width(), flow.height());
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
//
// Modulate thickness (increase bottom_z) of extrusions_in_out generated for this_layer
// if they overlap with overlapping_layers, whose print_z is above this_layer.bottom_z() and below this_layer.print_z.
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
        for (ExtrusionEntity *ee : extrusions_in_out) {
            ExtrusionPath *path = dynamic_cast<ExtrusionPath*>(ee);
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
        frag.polylines = intersection_pl(path_fragments.back().polylines, polygons_trimming);
        path_fragments.back().polylines = diff_pl(path_fragments.back().polylines, polygons_trimming);
        // Adjust the extrusion parameters for a reduced layer height and a non-bridging flow (nozzle_dmr = -1, does not matter).
        assert(this_layer.print_z > overlapping_layer.print_z);
        frag.height = float(this_layer.print_z - overlapping_layer.print_z);
        frag.mm3_per_mm = Flow(frag.width, frag.height, -1.f).mm3_per_mm();
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
        ExtrusionPathFragmentEndPointAccessor& operator=(const ExtrusionPathFragmentEndPointAccessor&) {
            return *this;
        }

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
                //const double d2 = (pt_end - pt_current).cast<double>.squaredNorm();
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
    SupportLayerPtrs    &support_layers,
    const MyLayersPtr   &raft_layers,
    const MyLayersPtr   &bottom_contacts,
    const MyLayersPtr   &top_contacts,
    const MyLayersPtr   &intermediate_layers,
    const MyLayersPtr   &interface_layers,
    const MyLayersPtr   &base_interface_layers) const
{
    // loop_interface_processor with a given circle radius.
    LoopInterfaceProcessor loop_interface_processor(1.5 * m_support_params.support_material_interface_flow.scaled_width());
    loop_interface_processor.n_contact_loops = this->has_contact_loops() ? 1 : 0;

    std::vector<float>      angles { m_support_params.base_angle };
    if (m_object_config->support_material_pattern == smpRectilinearGrid)
        angles.push_back(m_support_params.interface_angle);

    BoundingBox bbox_object(Point(-scale_(1.), -scale_(1.0)), Point(scale_(1.), scale_(1.)));

//    const coordf_t link_max_length_factor = 3.;
    const coordf_t link_max_length_factor = 0.;

    float raft_angle_1st_layer  = 0.f;
    float raft_angle_base       = 0.f;
    float raft_angle_interface  = 0.f;
    if (m_slicing_params.base_raft_layers > 1) {
        // There are all raft layer types (1st layer, base, interface & contact layers) available.
        raft_angle_1st_layer  = m_support_params.interface_angle;
        raft_angle_base       = m_support_params.base_angle;
        raft_angle_interface  = m_support_params.interface_angle;
    } else if (m_slicing_params.base_raft_layers == 1 || m_slicing_params.interface_raft_layers > 1) {
        // 1st layer, interface & contact layers available.
        raft_angle_1st_layer  = m_support_params.base_angle;
        if (this->has_support())
            // Print 1st layer at 45 degrees from both the interface and base angles as both can land on the 1st layer.
            raft_angle_1st_layer += 0.7854f;
        raft_angle_interface  = m_support_params.interface_angle;
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
        [this, &support_layers, &raft_layers, 
            &bbox_object, raft_angle_1st_layer, raft_angle_base, raft_angle_interface, link_max_length_factor]
            (const tbb::blocked_range<size_t>& range) {
        for (size_t support_layer_id = range.begin(); support_layer_id < range.end(); ++ support_layer_id)
        {
            assert(support_layer_id < raft_layers.size());
            SupportLayer &support_layer = *support_layers[support_layer_id];
            assert(support_layer.support_fills.entities.empty());
            MyLayer      &raft_layer    = *raft_layers[support_layer_id];

            std::unique_ptr<Fill> filler_interface = std::unique_ptr<Fill>(Fill::new_from_type(m_support_params.interface_fill_pattern));
            std::unique_ptr<Fill> filler_support   = std::unique_ptr<Fill>(Fill::new_from_type(m_support_params.base_fill_pattern));
            filler_interface->set_bounding_box(bbox_object);
            filler_support->set_bounding_box(bbox_object);

            // Print the support base below the support columns, or the support base for the support columns plus the contacts.
            if (support_layer_id > 0) {
                const Polygons &to_infill_polygons = (support_layer_id < m_slicing_params.base_raft_layers) ? 
                    raft_layer.polygons :
                    //FIXME misusing contact_polygons for support columns.
                    ((raft_layer.contact_polygons == nullptr) ? Polygons() : *raft_layer.contact_polygons);
                if (! to_infill_polygons.empty()) {
                    assert(! raft_layer.bridging);
                    Flow flow(float(m_support_params.support_material_flow.width()), float(raft_layer.height), m_support_params.support_material_flow.nozzle_diameter());
                    Fill * filler = filler_support.get();
                    filler->angle = raft_angle_base;
                    filler->spacing = m_support_params.support_material_flow.spacing();
                    filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / m_support_params.support_density));
                    fill_expolygons_with_sheath_generate_paths(
                        // Destination
                        support_layer.support_fills.entities,
                        // Regions to fill
                        to_infill_polygons,
                        // Filler and its parameters
                        filler, float(m_support_params.support_density),
                        // Extrusion parameters
                        erSupportMaterial, flow,
                        m_support_params.with_sheath, false);
                }
            }

            Fill *filler = filler_interface.get();
            Flow  flow = m_support_params.first_layer_flow;
            float density = 0.f;
            if (support_layer_id == 0) {
                // Base flange.
                filler->angle = raft_angle_1st_layer;
                filler->spacing = m_support_params.first_layer_flow.spacing();
                density       = float(m_object_config->raft_first_layer_density.value * 0.01);
            } else if (support_layer_id >= m_slicing_params.base_raft_layers) {
                filler->angle = raft_angle_interface;
                // We don't use $base_flow->spacing because we need a constant spacing
                // value that guarantees that all layers are correctly aligned.
                filler->spacing = m_support_params.support_material_flow.spacing();
                assert(! raft_layer.bridging);
                flow          = Flow(float(m_support_params.support_material_interface_flow.width()), float(raft_layer.height), m_support_params.support_material_flow.nozzle_diameter());
                density       = float(m_support_params.interface_density);
            } else
                continue;
            filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / density));
            fill_expolygons_with_sheath_generate_paths(
                // Destination
                support_layer.support_fills.entities, 
                // Regions to fill
                raft_layer.polygons,
                // Filler and its parameters
                filler, density,
                // Extrusion parameters
                (support_layer_id < m_slicing_params.base_raft_layers) ? erSupportMaterial : erSupportMaterialInterface, flow, 
                // sheath at first layer
                support_layer_id == 0, support_layer_id == 0);
        }
    });

    struct LayerCacheItem {
        LayerCacheItem(MyLayerExtruded *layer_extruded = nullptr) : layer_extruded(layer_extruded) {}
        MyLayerExtruded         *layer_extruded;
        std::vector<MyLayer*>    overlapping;
    };
    struct LayerCache {
        MyLayerExtruded                                     bottom_contact_layer;
        MyLayerExtruded                                     top_contact_layer;
        MyLayerExtruded                                     base_layer;
        MyLayerExtruded                                     interface_layer;
        MyLayerExtruded                                     base_interface_layer;
        boost::container::static_vector<LayerCacheItem, 5>  nonempty;

        void add_nonempty_and_sort() {
            for (MyLayerExtruded *item : { &bottom_contact_layer, &top_contact_layer, &interface_layer, &base_interface_layer, &base_layer })
                if (! item->empty())
                    this->nonempty.emplace_back(item);
            // Sort the layers with the same print_z coordinate by their heights, thickest first.
            std::stable_sort(this->nonempty.begin(), this->nonempty.end(), [](const LayerCacheItem &lc1, const LayerCacheItem &lc2) { return lc1.layer_extruded->layer->height > lc2.layer_extruded->layer->height; });
        }
    };
    std::vector<LayerCache>             layer_caches(support_layers.size());

    tbb::parallel_for(tbb::blocked_range<size_t>(n_raft_layers, support_layers.size()),
        [this, &support_layers, &bottom_contacts, &top_contacts, &intermediate_layers, &interface_layers, &base_interface_layers, &layer_caches, &loop_interface_processor, 
            &bbox_object, &angles, link_max_length_factor]
            (const tbb::blocked_range<size_t>& range) {
        // Indices of the 1st layer in their respective container at the support layer height.
        size_t idx_layer_bottom_contact   = size_t(-1);
        size_t idx_layer_top_contact      = size_t(-1);
        size_t idx_layer_intermediate     = size_t(-1);
        size_t idx_layer_interface        = size_t(-1);
        size_t idx_layer_base_interface   = size_t(-1);
        const auto fill_type_first_layer  = ipRectilinear;
        auto filler_interface       = std::unique_ptr<Fill>(Fill::new_from_type(m_support_params.contact_fill_pattern));
        // Filler for the 1st layer interface, if different from filler_interface.
        auto filler_first_layer_ptr = std::unique_ptr<Fill>(range.begin() == 0 && m_support_params.contact_fill_pattern != fill_type_first_layer ? Fill::new_from_type(fill_type_first_layer) : nullptr);
        // Pointer to the 1st layer interface filler.
        auto filler_first_layer     = filler_first_layer_ptr ? filler_first_layer_ptr.get() : filler_interface.get();
        // Filler for the base interface (to be used for soluble interface / non soluble base, to produce non soluble interface layer below soluble interface layer).
        auto filler_base_interface  = std::unique_ptr<Fill>(base_interface_layers.empty() ? nullptr : 
            Fill::new_from_type(m_support_params.interface_density > 0.95 || m_support_params.with_sheath ? ipRectilinear : ipSupportBase));
        auto filler_support         = std::unique_ptr<Fill>(Fill::new_from_type(m_support_params.base_fill_pattern));
        filler_interface->set_bounding_box(bbox_object);
        if (filler_first_layer_ptr)
            filler_first_layer_ptr->set_bounding_box(bbox_object);
        if (filler_base_interface)
            filler_base_interface->set_bounding_box(bbox_object);
        filler_support->set_bounding_box(bbox_object);
        for (size_t support_layer_id = range.begin(); support_layer_id < range.end(); ++ support_layer_id)
        {
            SupportLayer &support_layer = *support_layers[support_layer_id];
            LayerCache   &layer_cache   = layer_caches[support_layer_id];
            float         interface_angle_delta = m_object_config->support_material_style.value == smsSnug ? 
                (support_layer.interface_id() & 1) ? float(- M_PI / 4.) : float(+ M_PI / 4.) :
                0;

            // Find polygons with the same print_z.
            MyLayerExtruded &bottom_contact_layer = layer_cache.bottom_contact_layer;
            MyLayerExtruded &top_contact_layer    = layer_cache.top_contact_layer;
            MyLayerExtruded &base_layer           = layer_cache.base_layer;
            MyLayerExtruded &interface_layer      = layer_cache.interface_layer;
            MyLayerExtruded &base_interface_layer = layer_cache.base_interface_layer;
            // Increment the layer indices to find a layer at support_layer.print_z.
            {
                auto fun = [&support_layer](const MyLayer *l){ return l->print_z >= support_layer.print_z - EPSILON; };
                idx_layer_bottom_contact  = idx_higher_or_equal(bottom_contacts,     idx_layer_bottom_contact,  fun);
                idx_layer_top_contact     = idx_higher_or_equal(top_contacts,        idx_layer_top_contact,     fun);
                idx_layer_intermediate    = idx_higher_or_equal(intermediate_layers, idx_layer_intermediate,    fun);
                idx_layer_interface       = idx_higher_or_equal(interface_layers,    idx_layer_interface,       fun);
                idx_layer_base_interface  = idx_higher_or_equal(base_interface_layers, idx_layer_base_interface,fun);
            }
            // Copy polygons from the layers.
            if (idx_layer_bottom_contact < bottom_contacts.size() && bottom_contacts[idx_layer_bottom_contact]->print_z < support_layer.print_z + EPSILON)
                bottom_contact_layer.layer = bottom_contacts[idx_layer_bottom_contact];
            if (idx_layer_top_contact < top_contacts.size() && top_contacts[idx_layer_top_contact]->print_z < support_layer.print_z + EPSILON)
                top_contact_layer.layer = top_contacts[idx_layer_top_contact];
            if (idx_layer_interface < interface_layers.size() && interface_layers[idx_layer_interface]->print_z < support_layer.print_z + EPSILON)
                interface_layer.layer = interface_layers[idx_layer_interface];
            if (idx_layer_base_interface < base_interface_layers.size() && base_interface_layers[idx_layer_base_interface]->print_z < support_layer.print_z + EPSILON)
                base_interface_layer.layer = base_interface_layers[idx_layer_base_interface];
            if (idx_layer_intermediate < intermediate_layers.size() && intermediate_layers[idx_layer_intermediate]->print_z < support_layer.print_z + EPSILON)
                base_layer.layer = intermediate_layers[idx_layer_intermediate];

            if (m_object_config->support_material_interface_layers == 0) {
                // If no top interface layers were requested, we treat the contact layer exactly as a generic base layer.
                if (m_support_params.can_merge_support_regions) {
                    if (base_layer.could_merge(top_contact_layer)) 
                        base_layer.merge(std::move(top_contact_layer));
                    else if (base_layer.empty())
                        base_layer = std::move(top_contact_layer);
                }
            } else {
                loop_interface_processor.generate(top_contact_layer, m_support_params.support_material_interface_flow);
                // If no loops are allowed, we treat the contact layer exactly as a generic interface layer.
                // Merge interface_layer into top_contact_layer, as the top_contact_layer is not synchronized and therefore it will be used
                // to trim other layers.
                if (top_contact_layer.could_merge(interface_layer))
                    top_contact_layer.merge(std::move(interface_layer));
            } 
            if ((m_object_config->support_material_interface_layers == 0 || m_object_config->support_material_bottom_interface_layers == 0) && m_support_params.can_merge_support_regions) {
                if (base_layer.could_merge(bottom_contact_layer))
                    base_layer.merge(std::move(bottom_contact_layer));
                else if (base_layer.empty() && ! bottom_contact_layer.empty() && ! bottom_contact_layer.layer->bridging)
                    base_layer = std::move(bottom_contact_layer);
            } else if (bottom_contact_layer.could_merge(top_contact_layer))
                top_contact_layer.merge(std::move(bottom_contact_layer));
            else if (bottom_contact_layer.could_merge(interface_layer))
                bottom_contact_layer.merge(std::move(interface_layer));

#if 0
            if ( ! interface_layer.empty() && ! base_layer.empty()) {
                // turn base support into interface when it's contained in our holes
                // (this way we get wider interface anchoring)
                //FIXME The intention of the code below is unclear. One likely wanted to just merge small islands of base layers filling in the holes
                // inside interface layers, but the code below fills just too much, see GH #4570
                Polygons islands = top_level_islands(interface_layer.layer->polygons);
                polygons_append(interface_layer.layer->polygons, intersection(base_layer.layer->polygons, islands));
                base_layer.layer->polygons = diff(base_layer.layer->polygons, islands);
            }
#endif

            // Top and bottom contacts, interface layers.
            for (size_t i = 0; i < 3; ++ i) {
                MyLayerExtruded &layer_ex = (i == 0) ? top_contact_layer : (i == 1 ? bottom_contact_layer : interface_layer);
                if (layer_ex.empty() || layer_ex.polygons_to_extrude().empty())
                    continue;
                bool interface_as_base = m_object_config->support_material_interface_layers.value == 0 || 
                    (m_object_config->support_material_bottom_interface_layers == 0 && &layer_ex == &bottom_contact_layer);
                //FIXME Bottom interfaces are extruded with the briding flow. Some bridging layers have its height slightly reduced, therefore
                // the bridging flow does not quite apply. Reduce the flow to area of an ellipse? (A = pi * a * b)
                auto interface_flow = layer_ex.layer->bridging ?
                    Flow::bridging_flow(layer_ex.layer->height, m_support_params.support_material_bottom_interface_flow.nozzle_diameter()) :
                    (interface_as_base ? &m_support_params.support_material_flow : &m_support_params.support_material_interface_flow)->with_height(float(layer_ex.layer->height));
                filler_interface->angle = interface_as_base ?
                        // If zero interface layers are configured, use the same angle as for the base layers.
                        angles[support_layer_id % angles.size()] :
                        // Use interface angle for the interface layers.
                        m_support_params.interface_angle + interface_angle_delta;
                double density = interface_as_base ? m_support_params.support_density : m_support_params.interface_density;
                filler_interface->spacing = interface_as_base ? m_support_params.support_material_flow.spacing() : m_support_params.support_material_interface_flow.spacing();
                filler_interface->link_max_length = coord_t(scale_(filler_interface->spacing * link_max_length_factor / density));
                fill_expolygons_generate_paths(
                    // Destination
                    layer_ex.extrusions, 
                    // Regions to fill
                    union_safety_offset_ex(layer_ex.polygons_to_extrude()),
                    // Filler and its parameters
                    filler_interface.get(), float(density),
                    // Extrusion parameters
                    erSupportMaterialInterface, interface_flow);
            }

            // Base interface layers under soluble interfaces
            if ( ! base_interface_layer.empty() && ! base_interface_layer.polygons_to_extrude().empty()) {
                Fill *filler = filler_base_interface.get();
                //FIXME Bottom interfaces are extruded with the briding flow. Some bridging layers have its height slightly reduced, therefore
                // the bridging flow does not quite apply. Reduce the flow to area of an ellipse? (A = pi * a * b)
                assert(! base_interface_layer.layer->bridging);
                Flow interface_flow = m_support_params.support_material_flow.with_height(float(base_interface_layer.layer->height));
                filler->angle   = m_support_params.interface_angle + interface_angle_delta;
                filler->spacing = m_support_params.support_material_interface_flow.spacing();
                filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / m_support_params.interface_density));
                fill_expolygons_generate_paths(
                    // Destination
                    base_interface_layer.extrusions, 
                    //base_layer_interface.extrusions,
                    // Regions to fill
                    union_safety_offset_ex(base_interface_layer.polygons_to_extrude()),
                    // Filler and its parameters
                    filler, float(m_support_params.interface_density),
                    // Extrusion parameters
                    erSupportMaterial, interface_flow);
            }

            // Base support or flange.
            if (! base_layer.empty() && ! base_layer.polygons_to_extrude().empty()) {
                Fill *filler = filler_support.get();
                filler->angle = angles[support_layer_id % angles.size()];
                // We don't use $base_flow->spacing because we need a constant spacing
                // value that guarantees that all layers are correctly aligned.
                assert(! base_layer.layer->bridging);
                auto flow = m_support_params.support_material_flow.with_height(float(base_layer.layer->height));
                filler->spacing = m_support_params.support_material_flow.spacing();
                filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / m_support_params.support_density));
                float density = float(m_support_params.support_density);
                bool  sheath  = m_support_params.with_sheath;
                bool  no_sort = false;
                if (base_layer.layer->bottom_z < EPSILON) {
                    // Base flange (the 1st layer).
                    filler = filler_first_layer;
                    filler->angle = Geometry::deg2rad(float(m_object_config->support_material_angle.value + 90.));
                    density = float(m_object_config->raft_first_layer_density.value * 0.01);
                    flow = m_support_params.first_layer_flow;
                    // use the proper spacing for first layer as we don't need to align
                    // its pattern to the other layers
                    //FIXME When paralellizing, each thread shall have its own copy of the fillers.
                    filler->spacing = flow.spacing();
                    filler->link_max_length = coord_t(scale_(filler->spacing * link_max_length_factor / density));
                    sheath  = true;
                    no_sort = true;
                }
                fill_expolygons_with_sheath_generate_paths(
                    // Destination
                    base_layer.extrusions,
                    // Regions to fill
                    base_layer.polygons_to_extrude(),
                    // Filler and its parameters
                    filler, density,
                    // Extrusion parameters
                    erSupportMaterial, flow,
                    sheath, no_sort);

            }

            // Merge base_interface_layers to base_layers to avoid unneccessary retractions
            if (! base_layer.empty() && ! base_interface_layer.empty() && ! base_layer.polygons_to_extrude().empty() && ! base_interface_layer.polygons_to_extrude().empty() &&
                base_layer.could_merge(base_interface_layer))
                base_layer.merge(std::move(base_interface_layer));

            layer_cache.add_nonempty_and_sort();

            // Collect the support areas with this print_z into islands, as there is no need
            // for retraction over these islands.
            Polygons polys;
            // Collect the extrusions, sorted by the bottom extrusion height.
            for (LayerCacheItem &layer_cache_item : layer_cache.nonempty) {
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
                layer_cache_item.overlapping.reserve(20);
                coordf_t bottom_z = layer_cache_item.layer_extruded->layer->bottom_print_z() + EPSILON;
                auto add_overlapping = [&layer_cache_item, bottom_z](const MyLayersPtr &layers, size_t idx_top) {
                    for (int i = int(idx_top) - 1; i >= 0 && layers[i]->print_z > bottom_z; -- i)
                        layer_cache_item.overlapping.push_back(layers[i]);
                };
                add_overlapping(top_contacts, idx_layer_top_contact);
                if (layer_cache_item.layer_extruded->layer->layer_type == sltBottomContact) {
                    // Bottom contact layer may overlap with a base layer, which may be changed to interface layer.
                    add_overlapping(intermediate_layers,   idx_layer_intermediate);
                    add_overlapping(interface_layers,      idx_layer_interface);
                    add_overlapping(base_interface_layers, idx_layer_base_interface);
                }
                // Order the layers by lexicographically by an increasing print_z and a decreasing layer height.
                std::stable_sort(layer_cache_item.overlapping.begin(), layer_cache_item.overlapping.end(), [](auto *l1, auto *l2) { return *l1 < *l2; });
            }
            if (! polys.empty())
                expolygons_append(support_layer.support_islands.expolygons, union_ex(polys));
        } // for each support_layer_id
    });

    // Now modulate the support layer height in parallel.
    tbb::parallel_for(tbb::blocked_range<size_t>(n_raft_layers, support_layers.size()),
        [&support_layers, &layer_caches]
            (const tbb::blocked_range<size_t>& range) {
        for (size_t support_layer_id = range.begin(); support_layer_id < range.end(); ++ support_layer_id) {
            SupportLayer &support_layer = *support_layers[support_layer_id];
            LayerCache   &layer_cache   = layer_caches[support_layer_id];
            // For all extrusion types at this print_z, ordered by decreasing layer height:
            for (LayerCacheItem &layer_cache_item : layer_cache.nonempty) {
                // Trim the extrusion height from the bottom by the overlapping layers.
                modulate_extrusion_by_overlapping_layers(layer_cache_item.layer_extruded->extrusions, *layer_cache_item.layer_extruded->layer, layer_cache_item.overlapping);
                support_layer.support_fills.append(std::move(layer_cache_item.layer_extruded->extrusions));
            }
        }
    });

#ifndef NDEBUG
    struct Test {
        static bool verify_nonempty(const ExtrusionEntityCollection *collection) {
            for (const ExtrusionEntity *ee : collection->entities) {
                if (const ExtrusionPath *path = dynamic_cast<const ExtrusionPath*>(ee))
                    assert(! path->empty());
                else if (const ExtrusionMultiPath *multipath = dynamic_cast<const ExtrusionMultiPath*>(ee))
                    assert(! multipath->empty());
                else if (const ExtrusionEntityCollection *eecol = dynamic_cast<const ExtrusionEntityCollection*>(ee)) {
                    assert(! eecol->empty());
                    return verify_nonempty(eecol);
                } else
                    assert(false);
            }
            return true;
        }
    };
    for (const SupportLayer *support_layer : support_layers)
    assert(Test::verify_nonempty(&support_layer->support_fills));
#endif // NDEBUG
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
        grid.reserve(size_t(ceil(bb.size()(0) / pillar_spacing)) * size_t(ceil(bb.size()(1) / pillar_spacing)));
        for (coord_t x = bb.min(0); x <= bb.max(0) - pillar_size; x += pillar_spacing) {
            for (coord_t y = bb.min(1); y <= bb.max(1) - pillar_size; y += pillar_spacing) {
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
