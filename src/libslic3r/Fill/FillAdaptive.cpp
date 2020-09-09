#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../Surface.hpp"
#include "../Geometry.hpp"
#include "../AABBTreeIndirect.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"
#include "../ShortestPath.hpp"

#include "FillAdaptive.hpp"

namespace Slic3r {

std::pair<double, double> adaptive_fill_line_spacing(const PrintObject &print_object)
{
    // Output, spacing for icAdaptiveCubic and icSupportCubic
    double  adaptive_line_spacing = 0.;
    double  support_line_spacing = 0.;

    enum class Tristate {
        Yes,
        No,
        Maybe
    };
    struct RegionFillData {
        Tristate        has_adaptive_infill;
        Tristate        has_support_infill;
        double          density;
        double          extrusion_width;
    };
    std::vector<RegionFillData> region_fill_data;
    region_fill_data.reserve(print_object.print()->regions().size());
    bool                        build_octree = false;
    for (const PrintRegion *region : print_object.print()->regions()) {
        const PrintRegionConfig &config   = region->config();
        bool                     nonempty = config.fill_density > 0;
        bool                     has_adaptive_infill = nonempty && config.fill_pattern == ipAdaptiveCubic;
        bool                     has_support_infill  = nonempty && false; // config.fill_pattern == icSupportCubic;
        region_fill_data.push_back(RegionFillData({
            has_adaptive_infill ? Tristate::Maybe : Tristate::No,
            has_support_infill ? Tristate::Maybe : Tristate::No,
            config.fill_density,
            config.infill_extrusion_width
        }));
        build_octree |= has_adaptive_infill || has_support_infill;
    }

    if (build_octree) {
        // Compute the average of above parameters over all layers
        for (const Layer *layer : print_object.layers())
            for (size_t region_id = 0; region_id < layer->regions().size(); ++ region_id) {
                RegionFillData &rd = region_fill_data[region_id];
                if (rd.has_adaptive_infill == Tristate::Maybe && ! layer->regions()[region_id]->fill_surfaces.empty())
                    rd.has_adaptive_infill = Tristate::Yes;
                if (rd.has_support_infill == Tristate::Maybe && ! layer->regions()[region_id]->fill_surfaces.empty())
                    rd.has_support_infill = Tristate::Yes;
            }

        double  adaptive_fill_density           = 0.;
        double  adaptive_infill_extrusion_width = 0.;
        int     adaptive_cnt                    = 0;
        double  support_fill_density            = 0.;
        double  support_infill_extrusion_width  = 0.;
        int     support_cnt                     = 0;

        for (const RegionFillData &rd : region_fill_data) {
            if (rd.has_adaptive_infill == Tristate::Yes) {
                adaptive_fill_density           += rd.density;
                adaptive_infill_extrusion_width += rd.extrusion_width;
                ++ adaptive_cnt;
            } else if (rd.has_support_infill == Tristate::Yes) {
                support_fill_density           += rd.density;
                support_infill_extrusion_width += rd.extrusion_width;
                ++ support_cnt;
            }
        }

        auto to_line_spacing = [](int cnt, double density, double extrusion_width) {
            if (cnt) {
                density         /= double(cnt);
                extrusion_width /= double(cnt);
                return extrusion_width / ((density / 100.0f) * 0.333333333f);
            } else
                return 0.;
        };
        adaptive_line_spacing = to_line_spacing(adaptive_cnt, adaptive_fill_density, adaptive_infill_extrusion_width);
        support_line_spacing  = to_line_spacing(support_cnt, support_fill_density, support_infill_extrusion_width);
    }

    return std::make_pair(adaptive_line_spacing, support_line_spacing);
}

void FillAdaptive::_fill_surface_single(
    const FillParams                &params, 
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction, 
    ExPolygon                       &expolygon, 
    Polylines                       &polylines_out)
{
    // Store grouped lines by its direction (multiple of 120°)
    std::vector<Lines> infill_lines_dir(3);
    this->generate_infill_lines(this->adapt_fill_octree->root_cube.get(),
                                this->z, this->adapt_fill_octree->origin,infill_lines_dir,
                                this->adapt_fill_octree->cubes_properties,
                                int(this->adapt_fill_octree->cubes_properties.size()) - 1);

    Polylines all_polylines;
    all_polylines.reserve(infill_lines_dir[0].size() * 3);
    for (Lines &infill_lines : infill_lines_dir)
    {
        for (const Line &line : infill_lines)
        {
            all_polylines.emplace_back(line.a, line.b);
        }
    }

    if (params.dont_connect)
    {
        // Crop all polylines
        polylines_out = intersection_pl(all_polylines, to_polygons(expolygon));
    }
    else
    {
        // Crop all polylines
        all_polylines = intersection_pl(all_polylines, to_polygons(expolygon));

        Polylines boundary_polylines;
        Polylines non_boundary_polylines;
        for (const Polyline &polyline : all_polylines)
        {
            // connect_infill required all polylines to touch the boundary.
            if(polyline.lines().size() == 1 && expolygon.has_boundary_point(polyline.lines().front().a) && expolygon.has_boundary_point(polyline.lines().front().b))
            {
                boundary_polylines.push_back(polyline);
            }
            else
            {
                non_boundary_polylines.push_back(polyline);
            }
        }

        if(!boundary_polylines.empty())
        {
            boundary_polylines = chain_polylines(boundary_polylines);
            FillAdaptive::connect_infill(std::move(boundary_polylines), expolygon, polylines_out, this->spacing, params);
        }

        polylines_out.insert(polylines_out.end(), non_boundary_polylines.begin(), non_boundary_polylines.end());
    }

#ifdef SLIC3R_DEBUG_SLICE_PROCESSING
    {
        static int iRuna = 0;
        BoundingBox bbox_svg = this->bounding_box;
        {
            ::Slic3r::SVG svg(debug_out_path("FillAdaptive-%d.svg", iRuna), bbox_svg);
            for (const Polyline &polyline : polylines_out)
            {
                for (const Line &line : polyline.lines())
                {
                    Point from = line.a;
                    Point to = line.b;
                    Point diff = to - from;

                    float shrink_length = scale_(0.4);
                    float line_slope = (float)diff.y() / diff.x();
                    float shrink_x = shrink_length / (float)std::sqrt(1.0 + (line_slope * line_slope));
                    float shrink_y = line_slope * shrink_x;

                    to.x() -= shrink_x;
                    to.y() -= shrink_y;
                    from.x() += shrink_x;
                    from.y() += shrink_y;

                    svg.draw(Line(from, to));
                }
            }
        }

        iRuna++;
    }
#endif /* SLIC3R_DEBUG */
}

void FillAdaptive::generate_infill_lines(
        FillAdaptive_Internal::Cube *cube,
        double z_position,
        const Vec3d &origin,
        std::vector<Lines> &dir_lines_out,
        const std::vector<FillAdaptive_Internal::CubeProperties> &cubes_properties,
        int depth)
{
    using namespace FillAdaptive_Internal;

    if(cube == nullptr)
    {
        return;
    }

    double z_diff = std::abs(z_position - cube->center.z());

    if (z_diff > cubes_properties[depth].height / 2)
    {
        return;
    }

    if (z_diff < cubes_properties[depth].line_z_distance)
    {
        Point from(
                scale_((cubes_properties[depth].diagonal_length / 2) * (cubes_properties[depth].line_z_distance - z_diff) / cubes_properties[depth].line_z_distance),
                scale_(cubes_properties[depth].line_xy_distance - ((z_position - (cube->center.z() - cubes_properties[depth].line_z_distance)) / sqrt(2))));
        Point to(-from.x(), from.y());
        // Relative to cube center

        double rotation_angle = (2.0 * M_PI) / 3.0;
        for (Lines &lines : dir_lines_out)
        {
            Vec3d offset = cube->center - origin;
            Point from_abs(from), to_abs(to);

            from_abs.x() += int(scale_(offset.x()));
            from_abs.y() += int(scale_(offset.y()));
            to_abs.x() += int(scale_(offset.x()));
            to_abs.y() += int(scale_(offset.y()));

//            lines.emplace_back(from_abs, to_abs);
            this->connect_lines(lines, Line(from_abs, to_abs));

            from.rotate(rotation_angle);
            to.rotate(rotation_angle);
        }
    }

    for(const std::unique_ptr<Cube> &child : cube->children)
    {
        if(child != nullptr)
        {
            generate_infill_lines(child.get(), z_position, origin, dir_lines_out, cubes_properties, depth - 1);
        }
    }
}

void FillAdaptive::connect_lines(Lines &lines, Line new_line)
{
    auto eps = int(scale_(0.10));
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (std::abs(new_line.a.x() - lines[i].b.x()) < eps && std::abs(new_line.a.y() - lines[i].b.y()) < eps)
        {
            new_line.a = lines[i].a;
            lines.erase(lines.begin() + i);
            --i;
            continue;
        }

        if (std::abs(new_line.b.x() - lines[i].a.x()) < eps && std::abs(new_line.b.y() - lines[i].a.y()) < eps)
        {
            new_line.b = lines[i].b;
            lines.erase(lines.begin() + i);
            --i;
            continue;
        }
    }

    lines.emplace_back(new_line.a, new_line.b);
}

std::unique_ptr<FillAdaptive_Internal::Octree> FillAdaptive::build_octree(
    TriangleMesh &triangle_mesh,
    coordf_t line_spacing,
    const Vec3d &cube_center)
{
    using namespace FillAdaptive_Internal;

    if(line_spacing <= 0 || std::isnan(line_spacing))
    {
        return nullptr;
    }

    Vec3d bb_size = triangle_mesh.bounding_box().size();
    // The furthest point from the center of the bottom of the mesh bounding box.
    double furthest_point = std::sqrt(((bb_size.x() * bb_size.x()) / 4.0) +
                                      ((bb_size.y() * bb_size.y()) / 4.0) +
                                      (bb_size.z() * bb_size.z()));
    double max_cube_edge_length = furthest_point * 2;

    std::vector<CubeProperties> cubes_properties;
    for (double edge_length = (line_spacing * 2); edge_length < (max_cube_edge_length * 2); edge_length *= 2)
    {
        CubeProperties props{};
        props.edge_length = edge_length;
        props.height = edge_length * sqrt(3);
        props.diagonal_length = edge_length * sqrt(2);
        props.line_z_distance = edge_length / sqrt(3);
        props.line_xy_distance = edge_length / sqrt(6);
        cubes_properties.push_back(props);
    }

    if (triangle_mesh.its.vertices.empty())
    {
        triangle_mesh.require_shared_vertices();
    }

    Vec3d rotation = Vec3d((5.0 * M_PI) / 4.0, Geometry::deg2rad(215.264), M_PI / 6.0);
    Transform3d rotation_matrix = Geometry::assemble_transform(Vec3d::Zero(), rotation, Vec3d::Ones(), Vec3d::Ones());

    AABBTreeIndirect::Tree3f aabbTree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
            triangle_mesh.its.vertices, triangle_mesh.its.indices);
    auto octree = std::make_unique<Octree>(std::make_unique<Cube>(cube_center), cube_center, cubes_properties);

    FillAdaptive::expand_cube(octree->root_cube.get(), cubes_properties, rotation_matrix, aabbTree, triangle_mesh, int(cubes_properties.size()) - 1);

    return octree;
}

void FillAdaptive::expand_cube(
    FillAdaptive_Internal::Cube *cube,
    const std::vector<FillAdaptive_Internal::CubeProperties> &cubes_properties,
    const Transform3d &rotation_matrix,
    const AABBTreeIndirect::Tree3f &distance_tree,
    const TriangleMesh &triangle_mesh, int depth)
{
    using namespace FillAdaptive_Internal;

    if (cube == nullptr || depth == 0)
    {
        return;
    }

    std::vector<Vec3d> child_centers = {
            Vec3d(-1, -1, -1), Vec3d( 1, -1, -1), Vec3d(-1,  1, -1), Vec3d(-1, -1,  1),
            Vec3d( 1,  1,  1), Vec3d(-1,  1,  1), Vec3d( 1, -1,  1), Vec3d( 1,  1, -1)
    };

    double cube_radius_squared = (cubes_properties[depth].height * cubes_properties[depth].height) / 16;

    for (size_t i = 0; i < 8; ++i)
    {
        const Vec3d &child_center = child_centers[i];
        Vec3d child_center_transformed = cube->center + rotation_matrix * (child_center * (cubes_properties[depth].edge_length / 4));

        if(AABBTreeIndirect::is_any_triangle_in_radius(triangle_mesh.its.vertices, triangle_mesh.its.indices,
            distance_tree, child_center_transformed, cube_radius_squared))
        {
            cube->children[i] = std::make_unique<Cube>(child_center_transformed);
            FillAdaptive::expand_cube(cube->children[i].get(), cubes_properties, rotation_matrix, distance_tree, triangle_mesh, depth - 1);
        }
    }
}

} // namespace Slic3r
