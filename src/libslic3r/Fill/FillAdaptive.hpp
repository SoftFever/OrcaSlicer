#ifndef slic3r_FillAdaptive_hpp_
#define slic3r_FillAdaptive_hpp_

#include "../AABBTreeIndirect.hpp"

#include "FillBase.hpp"

namespace Slic3r {

class PrintObject;

namespace FillAdaptive_Internal
{
    struct CubeProperties
    {
        double edge_length;     // Lenght of edge of a cube
        double height;          // Height of rotated cube (standing on the corner)
        double diagonal_length; // Length of diagonal of a cube a face
        double line_z_distance; // Defines maximal distance from a center of a cube on Z axis on which lines will be created
        double line_xy_distance;// Defines maximal distance from a center of a cube on X and Y axis on which lines will be created
    };

    struct Cube
    {
        Vec3d center;
        std::unique_ptr<Cube> children[8] = {};
        Cube(const Vec3d &center) : center(center) {}
    };

    struct Octree
    {
        std::unique_ptr<Cube> root_cube;
        Vec3d origin;
        std::vector<CubeProperties> cubes_properties;

        Octree(std::unique_ptr<Cube> rootCube, const Vec3d &origin, const std::vector<CubeProperties> &cubes_properties)
            : root_cube(std::move(rootCube)), origin(origin), cubes_properties(cubes_properties) {}

        inline static int find_octant(const Vec3d &i_cube, const Vec3d &current)
        {
            return (i_cube.z() > current.z()) * 4 + (i_cube.y() > current.y()) * 2 + (i_cube.x() > current.x());
        }

        static void propagate_point(
            Vec3d                        point,
            FillAdaptive_Internal::Cube *current_cube,
            int                          depth,
            const std::vector<FillAdaptive_Internal::CubeProperties> &cubes_properties);
    };
}; // namespace FillAdaptive_Internal

//
// Some of the algorithms used by class FillAdaptive were inspired by
// Cura Engine's class SubDivCube
// https://github.com/Ultimaker/CuraEngine/blob/master/src/infill/SubDivCube.h
//
class FillAdaptive : public Fill
{
public:
    virtual ~FillAdaptive() {}

protected:
    virtual Fill* clone() const { return new FillAdaptive(*this); };
	virtual void _fill_surface_single(
	    const FillParams                &params,
	    unsigned int                     thickness_layers,
	    const std::pair<float, Point>   &direction,
	    ExPolygon                       &expolygon,
	    Polylines                       &polylines_out);

	virtual bool no_sort() const { return true; }

    void generate_infill_lines(
        FillAdaptive_Internal::Cube *cube,
        double                       z_position,
        const Vec3d &                origin,
        const Transform3d &          rotation_matrix,
        std::vector<Lines> &         dir_lines_out,
        const std::vector<FillAdaptive_Internal::CubeProperties> &cubes_properties,
        int  depth);

    static void connect_lines(Lines &lines, Line new_line);

    void generate_infill(const FillParams &             params,
                         unsigned int                   thickness_layers,
                         const std::pair<float, Point> &direction,
                         ExPolygon &                    expolygon,
                         Polylines &                    polylines_out,
                         FillAdaptive_Internal::Octree *octree);

public:
    static std::unique_ptr<FillAdaptive_Internal::Octree> build_octree(
        TriangleMesh &triangle_mesh,
        coordf_t      line_spacing,
        const Vec3d & cube_center);

    static void expand_cube(
        FillAdaptive_Internal::Cube *cube,
        const std::vector<FillAdaptive_Internal::CubeProperties> &cubes_properties,
        const AABBTreeIndirect::Tree3f &distance_tree,
        const TriangleMesh &            triangle_mesh,
        int                             depth);
};

class FillSupportCubic : public FillAdaptive
{
public:
    virtual ~FillSupportCubic() = default;

protected:
    virtual Fill* clone() const { return new FillSupportCubic(*this); };

    virtual bool no_sort() const { return true; }

    virtual void _fill_surface_single(
        const FillParams                &params,
        unsigned int                     thickness_layers,
        const std::pair<float, Point>   &direction,
        ExPolygon                       &expolygon,
        Polylines                       &polylines_out);

public:
    static std::unique_ptr<FillAdaptive_Internal::Octree> build_octree_for_adaptive_support(
        TriangleMesh &     triangle_mesh,
        coordf_t           line_spacing,
        const Vec3d &      cube_center,
        const Transform3d &rotation_matrix);
};

// Calculate line spacing for
// 1) adaptive cubic infill
// 2) adaptive internal support cubic infill
// Returns zero for a particular infill type if no such infill is to be generated.
std::pair<double, double> adaptive_fill_line_spacing(const PrintObject &print_object);

} // namespace Slic3r

#endif // slic3r_FillAdaptive_hpp_
