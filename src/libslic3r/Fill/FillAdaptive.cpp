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

}

FillAdaptive_Internal::Octree* FillAdaptive::build_octree(
    TriangleMesh &triangleMesh,
    coordf_t line_spacing,
    const BoundingBoxf3 &printer_volume,
    const Vec3d &cube_center)
{
    using namespace FillAdaptive_Internal;

    if(line_spacing <= 0)
    {
        return nullptr;
    }

    // The furthest point from center of bed.
    double furthest_point = std::sqrt(((printer_volume.size()[0] * printer_volume.size()[0]) / 4.0) +
                                      ((printer_volume.size()[1] * printer_volume.size()[1]) / 4.0) +
                                      (printer_volume.size()[2] * printer_volume.size()[2]));
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

    if (triangleMesh.its.vertices.empty())
    {
        triangleMesh.require_shared_vertices();
    }

    Vec3d rotation = Vec3d(Geometry::deg2rad(225.0), Geometry::deg2rad(215.0), Geometry::deg2rad(30.0));
    Transform3d rotation_matrix = Geometry::assemble_transform(Vec3d::Zero(), rotation, Vec3d::Ones(), Vec3d::Ones());

    AABBTreeIndirect::Tree3f aabbTree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(triangleMesh.its.vertices, triangleMesh.its.indices);
    Octree *octree = new Octree{new Cube{cube_center, cubes_properties.size() - 1, cubes_properties.back()}, cube_center};

    FillAdaptive::expand_cube(octree->root_cube, cubes_properties, rotation_matrix, aabbTree, triangleMesh);

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
        Vec3d closest_point = Vec3d::Zero();
        size_t closest_triangle_idx = 0;

        double distance_squared = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(
                triangleMesh.its.vertices, triangleMesh.its.indices, distanceTree, child_center_transformed,
                closest_triangle_idx,closest_point);

        if(distance_squared <= cube_radius_squared) {
            cube->children.push_back(new Cube{child_center_transformed, cube->depth - 1, cubes_properties[cube->depth - 1]});
            FillAdaptive::expand_cube(cube->children.back(), cubes_properties, rotation_matrix, distanceTree, triangleMesh);
        }
    }
}

} // namespace Slic3r
