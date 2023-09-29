///|/ Copyright (c) Prusa Research 2016 - 2023 Vojtěch Bubník @bubnikv, Pavel Mikuš @Godrak, Lukáš Hejl @hejllukas, Roman Beránek @zavorka, Lukáš Matěna @lukasmatena, Vojtěch Král @vojtechkral
///|/ Copyright (c) SuperSlicer 2023 Remi Durand @supermerill
///|/ Copyright (c) 2016 Sakari Kapanen @Flannelhead
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "../ClipperUtils.hpp"
#include "../ExtrusionEntityCollection.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"
#include "../Fill/FillBase.hpp"
#include "../Geometry.hpp"
#include "../Point.hpp"
#include "../MutablePolygon.hpp"

#include "Support/SupportCommon.hpp"
#include "SupportMaterial.hpp"

#include <clipper/clipper_z.hpp>

#include <cmath>
#include <memory>
#include <boost/log/trivial.hpp>
#include <boost/container/static_vector.hpp>

#include <tbb/parallel_for.h>
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
    #include "../utils.hpp"
    #include "../SVG.hpp"
#endif

#include <cassert>

using namespace Slic3r::FFFSupport;

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
    m_print_config          (&object->print()->config()),
    m_object_config         (&object->config()),
    m_slicing_params        (slicing_params),
    m_support_params        (*object)
{
}

void PrintObjectSupportMaterial::generate(PrintObject &object)
{
    BOOST_LOG_TRIVIAL(info) << "Support generator - Start";

    coordf_t max_object_layer_height = 0.;
    for (size_t i = 0; i < object.layer_count(); ++ i)
        max_object_layer_height = std::max(max_object_layer_height, object.layers()[i]->height);

    // Layer instances will be allocated by std::deque and they will be kept until the end of this function call.
    // The layers will be referenced by various LayersPtr (of type std::vector<Layer*>)
    SupportGeneratorLayerStorage layer_storage;

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating top contacts";

    // Per object layer projection of the object below the layer into print bed.
    std::vector<Polygons> buildplate_covered = this->buildplate_covered(object);

    // Determine the top contact surfaces of the support, defined as:
    // contact = overhangs - clearance + margin
    // This method is responsible for identifying what contact surfaces
    // should the support material expose to the object in order to guarantee
    // that it will be effective, regardless of how it's built below.
    // If raft is to be generated, the 1st top_contact layer will contain the 1st object layer silhouette without holes.
    SupportGeneratorLayersPtr top_contacts = this->top_contact_layers(object, buildplate_covered, layer_storage);
    if (top_contacts.empty())
        // Nothing is supported, no supports are generated.
        return;

#ifdef SLIC3R_DEBUG
    static int iRun = 0;
    iRun ++;
    for (const SupportGeneratorLayer *layer : top_contacts)
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
    SupportGeneratorLayersPtr bottom_contacts = this->bottom_contact_layers_and_layer_support_areas(
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
    SupportGeneratorLayersPtr intermediate_layers = this->raft_and_intermediate_support_layers(
        object, bottom_contacts, top_contacts, layer_storage);

    this->trim_support_layers_by_object(object, top_contacts, m_slicing_params.gap_support_object, m_slicing_params.gap_object_support, m_support_params.gap_xy);

#ifdef SLIC3R_DEBUG
    for (const SupportGeneratorLayer *layer : top_contacts)
        Slic3r::SVG::export_expolygons(
            debug_out_path("support-top-contacts-trimmed-by-object-%d-%lf.svg", iRun, layer->print_z), 
            union_ex(layer->polygons));
#endif

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating base layers";

    // Fill in intermediate layers between the top / bottom support contact layers, trim them by the object.
    this->generate_base_layers(object, bottom_contacts, top_contacts, intermediate_layers, layer_support_areas);

#ifdef SLIC3R_DEBUG
    for (SupportGeneratorLayersPtr::const_iterator it = intermediate_layers.begin(); it != intermediate_layers.end(); ++ it)
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
    SupportGeneratorLayersPtr empty_layers;
    auto [interface_layers, base_interface_layers] = FFFSupport::generate_interface_layers(
        *m_object_config, m_support_params, bottom_contacts, top_contacts, empty_layers, empty_layers, intermediate_layers, layer_storage);

    BOOST_LOG_TRIVIAL(info) << "Support generator - Creating raft";

    // If raft is to be generated, the 1st top_contact layer will contain the 1st object layer silhouette with holes filled.
    // There is also a 1st intermediate layer containing bases of support columns.
    // Inflate the bases of the support columns and create the raft base under the object.
    SupportGeneratorLayersPtr raft_layers = FFFSupport::generate_raft_base(object, m_support_params, m_slicing_params, top_contacts, interface_layers, base_interface_layers, intermediate_layers, layer_storage);

#ifdef SLIC3R_DEBUG
    for (const SupportGeneratorLayer *l : interface_layers)
        Slic3r::SVG::export_expolygons(
            debug_out_path("support-interface-layers-%d-%lf.svg", iRun, l->print_z), 
            union_ex(l->polygons));
    for (const SupportGeneratorLayer *l : base_interface_layers)
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

#ifdef SLIC3R_DEBUG
    SupportGeneratorLayersPtr layers_sorted =
#endif // SLIC3R_DEBUG
    generate_support_layers(object, raft_layers, bottom_contacts, top_contacts, intermediate_layers, interface_layers, base_interface_layers);

    BOOST_LOG_TRIVIAL(info) << "Support generator - Generating tool paths";

#if 0 // #ifdef SLIC3R_DEBUG
    {
        size_t layer_id = 0;
        for (int i = 0; i < int(layers_sorted.size());) {
            // Find the last layer with roughly the same print_z, find the minimum layer height of all.
            // Due to the floating point inaccuracies, the print_z may not be the same even if in theory they should.
            int j = i + 1;
            coordf_t zmax = layers_sorted[i]->print_z + EPSILON;
            bool empty = layers_sorted[i]->polygons.empty();
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
    generate_support_toolpaths(object.support_layers(), *m_object_config, m_support_params, m_slicing_params, raft_layers, bottom_contacts, top_contacts, intermediate_layers, interface_layers, base_interface_layers);

#ifdef SLIC3R_DEBUG
    {
        size_t layer_id = 0;
        for (int i = 0; i < int(layers_sorted.size());) {
            // Find the last layer with roughly the same print_z, find the minimum layer height of all.
            // Due to the floating point inaccuracies, the print_z may not be the same even if in theory they should.
            int j = i + 1;
            coordf_t zmax = layers_sorted[i]->print_z + EPSILON;
            bool empty = layers_sorted[i]->polygons.empty();
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
        style(object_config.support_style.value),
        grid_resolution(object_config.support_base_pattern_spacing.value + support_material_flow.spacing()),
        support_angle(Geometry::deg2rad(object_config.support_angle.value)),
        extrusion_width(support_material_flow.spacing()),
        // support_material_closing_radius(object_config.support_material_closing_radius.value),
        support_material_closing_radius(2.0),
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
        case smsTreeSlim:
        case smsTreeStrong:
        case smsTreeHybrid:

        // Orca: use organic as default
        case smsDefault:
        case smsOrganic:
//            assert(false);
            [[fallthrough]];
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
            if (ep.role() == ExtrusionRole::erOverhangPerimeter && ! ep.polyline.empty())
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
                if (ee2->role() == ExtrusionRole::erBridgeInfill)
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
            if (ep.role() == ExtrusionRole::erOverhangPerimeter && ! ep.polyline.empty()) {
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

    const bool   support_auto    = object_config.enable_support.value && is_auto(object_config.support_type.value);
    const bool   buildplate_only = ! annotations.buildplate_covered.empty();
    // If user specified a custom angle threshold, convert it to radians.
    // Zero means automatic overhang detection.
    // +1 makes the threshold inclusive
    double thresh_angle = object_config.support_threshold_angle.value > 0 ? object_config.support_threshold_angle.value + 1 : 0;
    thresh_angle = std::min(thresh_angle, 89.); // BBS should be smaller than 90
    const double threshold_rad   = Geometry::deg2rad(thresh_angle);
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
        contact_polygons = object_config.raft_expansion.value > 0 ? expand(overhang_polygons, scaled<float>(object_config.raft_expansion.value)) : overhang_polygons;
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
                (layer_id < (size_t)object_config.enforce_support_layers.value) ? 
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

            // if (object_config.dont_support_bridges)
            //     //FIXME Expensive, potentially not precise enough. Misses gap fill extrusions, which bridge.
            //     remove_bridges_from_contacts(print_config, lower_layer, *layerm, fw, diff_polygons);

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
static inline std::pair<SupportGeneratorLayer*, SupportGeneratorLayer*> new_contact_layer(
    const PrintConfig                                   &print_config, 
    const PrintObjectConfig                             &object_config,
    const SlicingParameters                             &slicing_params,
    const coordf_t                                       support_layer_height_min,
    const Layer                                         &layer, 
    SupportGeneratorLayerStorage                        &layer_storage)
{
    double print_z, bottom_z, height;
    SupportGeneratorLayer* bridging_layer = nullptr;
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
            return std::pair<SupportGeneratorLayer*, SupportGeneratorLayer*>(nullptr, nullptr);
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
                    bridging_layer = &layer_storage.allocate(SupporLayerType::TopContact);
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

    SupportGeneratorLayer &new_layer = layer_storage.allocate(SupporLayerType::TopContact);
    new_layer.idx_object_layer_above = layer_id;
    new_layer.print_z  = print_z;
    new_layer.bottom_z = bottom_z;
    new_layer.height   = height;
    return std::make_pair(&new_layer, bridging_layer);
}

static inline void fill_contact_layer(
    SupportGeneratorLayer &new_layer,
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
    bool reduce_interfaces = object_config.support_style.value == smsGrid && layer_id > 0 && !slicing_params.soluble_interface;
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
static void merge_contact_layers(const SlicingParameters &slicing_params, double support_layer_height_min, SupportGeneratorLayersPtr &layers)
{
    // Sort the layers, as one layer may produce bridging and non-bridging contact layers with different print_z.
    std::sort(layers.begin(), layers.end(), [](const SupportGeneratorLayer *l1, const SupportGeneratorLayer *l2) { return l1->print_z < l2->print_z; });

    int i = 0;
    int k = 0;
    {
        // Find the span of layers, which are to be printed at the first layer height.
        int j = 0;
        for (; j < (int)layers.size() && layers[j]->print_z < slicing_params.first_print_layer_height + support_layer_height_min - EPSILON; ++ j);
        if (j > 0) {
            // Merge the layers layers (0) to (j - 1) into the layers[0].
            SupportGeneratorLayer &dst = *layers.front();
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
            SupportGeneratorLayer &dst = *layers[i];
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
SupportGeneratorLayersPtr PrintObjectSupportMaterial::top_contact_layers(
    const PrintObject &object, const std::vector<Polygons> &buildplate_covered, SupportGeneratorLayerStorage &layer_storage) const
{
#ifdef SLIC3R_DEBUG
    static int iRun = 0;
    ++ iRun; 
    #define SLIC3R_IRUN , iRun
#endif /* SLIC3R_DEBUG */

    // Slice support enforcers / support blockers.
    SupportAnnotations annotations(object, buildplate_covered);

    // Output layers, sorted by top Z.
    SupportGeneratorLayersPtr contact_out;

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
    tbb::parallel_for(tbb::blocked_range<size_t>(this->has_raft() ? 0 : 1, num_layers),
        [this, &object, &annotations, &layer_storage, &contact_out]
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
                    auto [new_layer, bridging_layer] = new_contact_layer(*m_print_config, *m_object_config, m_slicing_params, m_support_params.support_layer_height_min, layer, layer_storage);
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
static inline SupportGeneratorLayer* detect_bottom_contacts(
    const SlicingParameters                          &slicing_params,
    const SupportParameters                          &support_params,
    const PrintObject                                &object,
    const Layer                                      &layer,
    // Existing top contact layers, to which this newly created bottom contact layer will be snapped to guarantee a minimum layer height.
    const SupportGeneratorLayersPtr                  &top_contacts,
    // First top contact layer index overlapping with this new bottom interface layer.
    size_t                                            contact_idx,
    // To allocate a new layer from.
    SupportGeneratorLayerStorage                     &layer_storage,
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
    SupportGeneratorLayer &layer_new = layer_storage.allocate_unguarded(SupporLayerType::BottomContact);
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
SupportGeneratorLayersPtr PrintObjectSupportMaterial::bottom_contact_layers_and_layer_support_areas(
    const PrintObject &object, const SupportGeneratorLayersPtr &top_contacts, std::vector<Polygons> &buildplate_covered, 
    SupportGeneratorLayerStorage &layer_storage, std::vector<Polygons> &layer_support_areas) const
{
    if (top_contacts.empty())
        return SupportGeneratorLayersPtr();

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
    SupportGeneratorLayersPtr bottom_contacts;

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
            SupportGeneratorLayer &top_contact = *top_contacts[contact_idx];
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
                    SupportGeneratorLayer *layer_new = detect_bottom_contacts(
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

// Trim the top_contacts layers with the bottom_contacts layers if they overlap, so there would not be enough vertical space for both of them.
void PrintObjectSupportMaterial::trim_top_contacts_by_bottom_contacts(
    const PrintObject &object, const SupportGeneratorLayersPtr &bottom_contacts, SupportGeneratorLayersPtr &top_contacts) const
{
    tbb::parallel_for(tbb::blocked_range<int>(0, int(top_contacts.size())),
        [&bottom_contacts, &top_contacts](const tbb::blocked_range<int>& range) {
            int idx_bottom_overlapping_first = -2;
            // For all top contact layers, counting downwards due to the way idx_higher_or_equal caches the last index to avoid repeated binary search.
            for (int idx_top = range.end() - 1; idx_top >= range.begin(); -- idx_top) {
                SupportGeneratorLayer &layer_top = *top_contacts[idx_top];
                // Find the first bottom layer overlapping with layer_top.
                idx_bottom_overlapping_first = idx_lower_or_equal(bottom_contacts, idx_bottom_overlapping_first, [&layer_top](const SupportGeneratorLayer *layer_bottom){ return layer_bottom->bottom_print_z() - EPSILON <= layer_top.bottom_z; });
                // For all top contact layers overlapping with the thick bottom contact layer:
                for (int idx_bottom_overlapping = idx_bottom_overlapping_first; idx_bottom_overlapping >= 0; -- idx_bottom_overlapping) {
                    const SupportGeneratorLayer &layer_bottom = *bottom_contacts[idx_bottom_overlapping];
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

SupportGeneratorLayersPtr PrintObjectSupportMaterial::raft_and_intermediate_support_layers(
    const PrintObject   &object,
    const SupportGeneratorLayersPtr   &bottom_contacts,
    const SupportGeneratorLayersPtr   &top_contacts,
    SupportGeneratorLayerStorage      &layer_storage) const
{
    SupportGeneratorLayersPtr intermediate_layers;

    // Collect and sort the extremes (bottoms of the top contacts and tops of the bottom contacts).
    SupportGeneratorLayersPtr extremes;
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

    auto layer_extreme_lower = [](const SupportGeneratorLayer *l1, const SupportGeneratorLayer *l2) {
        coordf_t z1 = l1->extreme_z();
        coordf_t z2 = l2->extreme_z();
        // If the layers are aligned, return the top contact surface first.
        return z1 < z2 || (z1 == z2 && l1->layer_type == SupporLayerType::TopContact && l2->layer_type == SupporLayerType::BottomContact);
    };
    std::sort(extremes.begin(), extremes.end(), layer_extreme_lower);

    assert(extremes.empty() || 
        (extremes.front()->extreme_z() > m_slicing_params.raft_interface_top_z - EPSILON && 
          (m_slicing_params.raft_layers() == 1 || // only raft contact layer
           extremes.front()->layer_type == SupporLayerType::TopContact || // first extreme is a top contact layer
           extremes.front()->extreme_z() > m_slicing_params.first_print_layer_height - EPSILON)));

    bool synchronize = this->synchronize_layers();

#ifdef _DEBUG
    // Verify that the extremes are separated by m_support_layer_height_min.
    for (size_t i = 1; i < extremes.size(); ++ i) {
        assert(extremes[i]->extreme_z() - extremes[i-1]->extreme_z() == 0. ||
               extremes[i]->extreme_z() - extremes[i-1]->extreme_z() > m_support_params.support_layer_height_min - EPSILON);
        assert(extremes[i]->extreme_z() - extremes[i-1]->extreme_z() > 0. ||
               extremes[i]->layer_type == extremes[i-1]->layer_type ||
               (extremes[i]->layer_type == SupporLayerType::BottomContact && extremes[i - 1]->layer_type == SupporLayerType::TopContact));
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
        assert(extremes.front()->layer_type == SupporLayerType::TopContact);
        ++ idx_extreme_first;
    }
    for (size_t idx_extreme = idx_extreme_first; idx_extreme < extremes.size(); ++ idx_extreme) {
        SupportGeneratorLayer      *extr2  = extremes[idx_extreme];
        coordf_t      extr2z = extr2->extreme_z();
        if (std::abs(extr2z - m_slicing_params.first_print_layer_height) < EPSILON) {
            // This is a bottom of a synchronized (or soluble) top contact layer, its height has been decided in this->top_contact_layers().
            assert(extr2->layer_type == SupporLayerType::TopContact);
            assert(extr2->bottom_z == m_slicing_params.first_print_layer_height);
            assert(extr2->print_z >= m_slicing_params.first_print_layer_height + m_support_params.support_layer_height_min - EPSILON);
            if (intermediate_layers.empty() || intermediate_layers.back()->print_z < m_slicing_params.first_print_layer_height) {
                SupportGeneratorLayer &layer_new = layer_storage.allocate_unguarded(SupporLayerType::Intermediate);
                layer_new.bottom_z = 0.;
                layer_new.print_z  = m_slicing_params.first_print_layer_height;
                layer_new.height   = m_slicing_params.first_print_layer_height;
                intermediate_layers.push_back(&layer_new);
            }
            continue;
        }
        assert(extr2z >= m_slicing_params.raft_interface_top_z + EPSILON);
        assert(extr2z >= m_slicing_params.first_print_layer_height + EPSILON);
        SupportGeneratorLayer      *extr1  = (idx_extreme == idx_extreme_first) ? nullptr : extremes[idx_extreme - 1];
        // Fuse a support layer firmly to the raft top interface (not to the raft contacts).
        coordf_t      extr1z = (extr1 == nullptr) ? m_slicing_params.raft_interface_top_z : extr1->extreme_z();
        assert(extr2z >= extr1z);
        assert(extr2z > extr1z || (extr1 != nullptr && extr2->layer_type == SupporLayerType::BottomContact));
        if (std::abs(extr1z) < EPSILON) {
            // This layer interval starts with the 1st layer. Print the 1st layer using the prescribed 1st layer thickness.
            // assert(! m_slicing_params.has_raft()); RaftingEdition: unclear where the issue is: assert fails with 1-layer raft & base supports
            assert(intermediate_layers.empty() || intermediate_layers.back()->print_z <= m_slicing_params.first_print_layer_height);
            // At this point only layers above first_print_layer_heigth + EPSILON are expected as the other cases were captured earlier.
            assert(extr2z >= m_slicing_params.first_print_layer_height + EPSILON);
            // Generate a new intermediate layer.
            SupportGeneratorLayer &layer_new = layer_storage.allocate_unguarded(SupporLayerType::Intermediate);
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
                SupportGeneratorLayer &layer_new = layer_storage.allocate_unguarded(SupporLayerType::Intermediate);
                layer_new.print_z  = m_slicing_params.object_print_z_min;
                layer_new.bottom_z = m_slicing_params.raft_interface_top_z;
                layer_new.height   = layer_new.print_z - layer_new.bottom_z;
                intermediate_layers.push_back(&layer_new);
            }
            // Emit all intermediate support layers synchronized with object layers up to extr2z.
            for (; idx_layer_object < object.layers().size() && object.layers()[idx_layer_object]->print_z < extr2z + EPSILON; ++ idx_layer_object) {
                SupportGeneratorLayer &layer_new = layer_storage.allocate_unguarded(SupporLayerType::Intermediate);
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
            if (extr1 != nullptr && extr1->layer_type == SupporLayerType::TopContact &&
                extr1->print_z + m_support_params.support_layer_height_min > extr1->bottom_z + step) {
                // The bottom extreme is a bottom of a top surface. Ensure that the gap 
                // between the 1st intermediate layer print_z and extr1->print_z is not too small.
                assert(extr1->bottom_z + m_support_params.support_layer_height_min < extr1->print_z + EPSILON);
                // Generate the first intermediate layer.
                SupportGeneratorLayer &layer_new = layer_storage.allocate_unguarded(SupporLayerType::Intermediate);
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
            if (! m_slicing_params.soluble_interface && extr2->layer_type == SupporLayerType::TopContact) {
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
                SupportGeneratorLayer &layer_new = layer_storage.allocate_unguarded(SupporLayerType::Intermediate);
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
    const SupportGeneratorLayersPtr   &bottom_contacts,
    const SupportGeneratorLayersPtr   &top_contacts,
    SupportGeneratorLayersPtr         &intermediate_layers,
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
                SupportGeneratorLayer &layer_intermediate = *intermediate_layers[idx_intermediate];
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
                    [&layer_intermediate](const SupportGeneratorLayer *layer){ return layer->bottom_z <= layer_intermediate.print_z - EPSILON; });
                // Collect all the top_contact layer intersecting with this layer.
                for (int idx_top_contact_overlapping = idx_top_contact_above; idx_top_contact_overlapping >= 0; -- idx_top_contact_overlapping) {
                    SupportGeneratorLayer &layer_top_overlapping = *top_contacts[idx_top_contact_overlapping];
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
                        SupportGeneratorLayer &contacts = *top_contacts[i];
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
                    [&layer_intermediate](const SupportGeneratorLayer *layer){ return layer->bottom_print_z() <= layer_intermediate.print_z - EPSILON; });
                // Collect all the bottom_contacts layer intersecting with this layer.
                for (int i = idx_bottom_contact_overlapping; i >= 0; -- i) {
                    SupportGeneratorLayer &layer_bottom_overlapping = *bottom_contacts[i];
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
                layer_intermediate.layer_type = SupporLayerType::Base;

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
    for (SupportGeneratorLayersPtr::const_iterator it = intermediate_layers.begin(); it != intermediate_layers.end(); ++it)
        ::Slic3r::SVG::export_expolygons(
            debug_out_path("support-intermediate-layers-untrimmed-%d-%lf.svg", iRun, (*it)->print_z),
            union_ex((*it)->polygons));
    ++ iRun;
#endif /* SLIC3R_DEBUG */

    this->trim_support_layers_by_object(object, intermediate_layers, m_slicing_params.gap_support_object, m_slicing_params.gap_object_support, m_support_params.gap_xy);
}

void PrintObjectSupportMaterial::trim_support_layers_by_object(
    const PrintObject   &object,
    SupportGeneratorLayersPtr         &support_layers,
    const coordf_t       gap_extra_above,
    const coordf_t       gap_extra_below,
    const coordf_t       gap_xy) const
{
    const float gap_xy_scaled = float(scale_(gap_xy));

    // Collect non-empty layers to be processed in parallel.
    // This is a good idea as pulling a thread from a thread pool for an empty task is expensive.
    SupportGeneratorLayersPtr nonempty_layers;
    nonempty_layers.reserve(support_layers.size());
    for (size_t idx_layer = 0; idx_layer < support_layers.size(); ++ idx_layer) {
        SupportGeneratorLayer *support_layer = support_layers[idx_layer];
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
                SupportGeneratorLayer &support_layer = *nonempty_layers[idx_layer];
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
                            if (region->region().config().detect_overhang_wall.value)
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
