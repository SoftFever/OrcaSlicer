#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../Surface.hpp"
#include "../Geometry.hpp"
#include "../AABBTreeIndirect.hpp"

#include "FillAdaptive.hpp"

namespace Slic3r {

void FillAdaptive::_fill_surface_single(
    const FillParams                &params, 
    unsigned int                     thickness_layers,
    const std::pair<float, Point>   &direction, 
    ExPolygon                       &expolygon, 
    Polylines                       &polylines_out)
{
    std::vector<Lines> infill_lines_dir(3);
    this->generate_infill_lines(this->adapt_fill_octree->root_cube.get(), this->z, this->adapt_fill_octree->origin, infill_lines_dir);

    for (Lines &infill_lines : infill_lines_dir)
    {
        for (const Line &line : infill_lines)
        {
            polylines_out.emplace_back(line.a, line.b);
        }
    }

    // Crop all polylines
    polylines_out = intersection_pl(polylines_out, to_polygons(expolygon));

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
        std::vector<Lines> &dir_lines_out)
{
    using namespace FillAdaptive_Internal;

    if(cube == nullptr)
    {
        return;
    }

    double z_diff = std::abs(z_position - cube->center.z());

    if (z_diff > cube->properties.height / 2)
    {
        return;
    }

    if (z_diff < cube->properties.line_z_distance)
    {
        Point from(
                scale_((cube->properties.diagonal_length / 2) * (cube->properties.line_z_distance - z_diff) / cube->properties.line_z_distance),
                scale_(cube->properties.line_xy_distance - ((z_position - (cube->center.z() - cube->properties.line_z_distance)) / sqrt(2))));
        Point to(-from.x(), from.y());
        // Relative to cube center

        float rotation_angle = (2.0 * M_PI) / 3.0;
        for (Lines &lines : dir_lines_out)
        {
            Vec3d offset = cube->center - origin;
            Point from_abs(from), to_abs(to);

            from_abs.x() += scale_(offset.x());
            from_abs.y() += scale_(offset.y());
            to_abs.x() += scale_(offset.x());
            to_abs.y() += scale_(offset.y());

//            lines.emplace_back(from_abs, to_abs);
            this->connect_lines(lines, Line(from_abs, to_abs));

            from.rotate(rotation_angle);
            to.rotate(rotation_angle);
        }
    }

    for(const std::unique_ptr<Cube> &child : cube->children)
    {
        generate_infill_lines(child.get(), z_position, origin, dir_lines_out);
    }
}

void FillAdaptive::connect_lines(Lines &lines, const Line &new_line)
{
    int eps = scale_(0.10);
    bool modified = false;

    for (Line &line : lines)
    {
        if (std::abs(new_line.a.x() - line.b.x()) < eps && std::abs(new_line.a.y() - line.b.y()) < eps)
        {
            line.b.x() = new_line.b.x();
            line.b.y() = new_line.b.y();
            modified = true;
        }

        if (std::abs(new_line.b.x() - line.a.x()) < eps && std::abs(new_line.b.y() - line.a.y()) < eps)
        {
            line.a.x() = new_line.a.x();
            line.a.y() = new_line.a.y();
            modified = true;
        }
    }

    if(!modified)
    {
        lines.push_back(new_line);
    }
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

    Vec3d rotation = Vec3d(Geometry::deg2rad(225.0), Geometry::deg2rad(215.264), Geometry::deg2rad(30.0));
    Transform3d rotation_matrix = Geometry::assemble_transform(Vec3d::Zero(), rotation, Vec3d::Ones(), Vec3d::Ones());

    AABBTreeIndirect::Tree3f aabbTree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
            triangle_mesh.its.vertices, triangle_mesh.its.indices);
    auto octree = std::make_unique<Octree>(
        std::make_unique<Cube>(cube_center, cubes_properties.size() - 1, cubes_properties.back()), cube_center);

    FillAdaptive::expand_cube(octree->root_cube.get(), cubes_properties, rotation_matrix, aabbTree, triangle_mesh);

    return octree;
}

void FillAdaptive::expand_cube(
    FillAdaptive_Internal::Cube *cube,
    const std::vector<FillAdaptive_Internal::CubeProperties> &cubes_properties,
    const Transform3d &rotation_matrix,
    const AABBTreeIndirect::Tree3f &distanceTree,
    const TriangleMesh &triangleMesh)
{
    using namespace FillAdaptive_Internal;

    if (cube == nullptr || cube->depth == 0)
    {
        return;
    }

    std::vector<Vec3d> child_centers = {
            Vec3d(-1, -1, -1), Vec3d( 1, -1, -1), Vec3d(-1,  1, -1), Vec3d(-1, -1,  1),
            Vec3d( 1,  1,  1), Vec3d(-1,  1,  1), Vec3d( 1, -1,  1), Vec3d( 1,  1, -1)
    };

    double cube_radius_squared = (cube->properties.height * cube->properties.height) / 16;

    for (const Vec3d &child_center : child_centers) {
        Vec3d child_center_transformed = cube->center + rotation_matrix * (child_center * (cube->properties.edge_length / 4));

        if(AABBTreeIndirect::is_any_triangle_in_radius(triangleMesh.its.vertices, triangleMesh.its.indices, distanceTree, child_center_transformed, cube_radius_squared)) {
            cube->children.emplace_back(std::make_unique<Cube>(child_center_transformed, cube->depth - 1, cubes_properties[cube->depth - 1]));
            FillAdaptive::expand_cube(cube->children.back().get(), cubes_properties, rotation_matrix, distanceTree, triangleMesh);
        }
    }
}

} // namespace Slic3r
