#include "../ClipperUtils.hpp"
#include "../ExPolygon.hpp"
#include "../Surface.hpp"
#include "../Geometry.hpp"
#include "../Layer.hpp"
#include "../Print.hpp"
#include "../ShortestPath.hpp"

#include "FillAdaptive.hpp"

// for indexed_triangle_set
#include <admesh/stl.h>

#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <numeric>

// Boost pool: Don't use mutexes to synchronize memory allocation.
#define BOOST_POOL_NO_MT
#include <boost/pool/object_pool.hpp>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point.hpp>
#include <boost/geometry/geometries/segment.hpp>
#include <boost/geometry/index/rtree.hpp>


namespace Slic3r {
namespace FillAdaptive {

// Derived from https://github.com/juj/MathGeoLib/blob/master/src/Geometry/Triangle.cpp
// The AABB-Triangle test implementation is based on the pseudo-code in
// Christer Ericson's Real-Time Collision Detection, pp. 169-172. It is
// practically a standard SAT test.
//
// Original MathGeoLib benchmark:
//    Best: 17.282 nsecs / 46.496 ticks, Avg: 17.804 nsecs, Worst: 18.434 nsecs
//
//FIXME Vojtech: The MathGeoLib contains a vectorized implementation.
template<typename Vector> 
bool triangle_AABB_intersects(const Vector &a, const Vector &b, const Vector &c, const BoundingBoxBase<Vector> &aabb)
{
    using Scalar = typename Vector::Scalar;

    Vector tMin = a.cwiseMin(b.cwiseMin(c));
    Vector tMax = a.cwiseMax(b.cwiseMax(c));

    if (tMin.x() >= aabb.max.x() || tMax.x() <= aabb.min.x()
        || tMin.y() >= aabb.max.y() || tMax.y() <= aabb.min.y()
        || tMin.z() >= aabb.max.z() || tMax.z() <= aabb.min.z())
        return false;

    Vector center = (aabb.min + aabb.max) * 0.5f;
    Vector h = aabb.max - center;

    const Vector t[3] { b-a, c-a, c-b };

    Vector ac = a - center;

    Vector n = t[0].cross(t[1]);
    Scalar s = n.dot(ac);
    Scalar r = std::abs(h.dot(n.cwiseAbs()));
    if (abs(s) >= r)
        return false;

    const Vector at[3] = { t[0].cwiseAbs(), t[1].cwiseAbs(), t[2].cwiseAbs() };

    Vector bc = b - center;
    Vector cc = c - center;

    // SAT test all cross-axes.
    // The following is a fully unrolled loop of this code, stored here for reference:
    /*
    Scalar d1, d2, a1, a2;
    const Vector e[3] = { DIR_VEC(1, 0, 0), DIR_VEC(0, 1, 0), DIR_VEC(0, 0, 1) };
    for(int i = 0; i < 3; ++i)
        for(int j = 0; j < 3; ++j)
        {
            Vector axis = Cross(e[i], t[j]);
            ProjectToAxis(axis, d1, d2);
            aabb.ProjectToAxis(axis, a1, a2);
            if (d2 <= a1 || d1 >= a2) return false;
        }
    */

    // eX <cross> t[0]
    Scalar d1 = t[0].y() * ac.z() - t[0].z() * ac.y();
    Scalar d2 = t[0].y() * cc.z() - t[0].z() * cc.y();
    Scalar tc = (d1 + d2) * 0.5f;
    r = std::abs(h.y() * at[0].z() + h.z() * at[0].y());
    if (r + std::abs(tc - d1) < std::abs(tc))
        return false;

    // eX <cross> t[1]
    d1 = t[1].y() * ac.z() - t[1].z() * ac.y();
    d2 = t[1].y() * bc.z() - t[1].z() * bc.y();
    tc = (d1 + d2) * 0.5f;
    r = std::abs(h.y() * at[1].z() + h.z() * at[1].y());
    if (r + std::abs(tc - d1) < std::abs(tc))
        return false;

    // eX <cross> t[2]
    d1 = t[2].y() * ac.z() - t[2].z() * ac.y();
    d2 = t[2].y() * bc.z() - t[2].z() * bc.y();
    tc = (d1 + d2) * 0.5f;
    r = std::abs(h.y() * at[2].z() + h.z() * at[2].y());
    if (r + std::abs(tc - d1) < std::abs(tc))
        return false;

    // eY <cross> t[0]
    d1 = t[0].z() * ac.x() - t[0].x() * ac.z();
    d2 = t[0].z() * cc.x() - t[0].x() * cc.z();
    tc = (d1 + d2) * 0.5f;
    r = std::abs(h.x() * at[0].z() + h.z() * at[0].x());
    if (r + std::abs(tc - d1) < std::abs(tc))
        return false;

    // eY <cross> t[1]
    d1 = t[1].z() * ac.x() - t[1].x() * ac.z();
    d2 = t[1].z() * bc.x() - t[1].x() * bc.z();
    tc = (d1 + d2) * 0.5f;
    r = std::abs(h.x() * at[1].z() + h.z() * at[1].x());
    if (r + std::abs(tc - d1) < std::abs(tc))
        return false;

    // eY <cross> t[2]
    d1 = t[2].z() * ac.x() - t[2].x() * ac.z();
    d2 = t[2].z() * bc.x() - t[2].x() * bc.z();
    tc = (d1 + d2) * 0.5f;
    r = std::abs(h.x() * at[2].z() + h.z() * at[2].x());
    if (r + std::abs(tc - d1) < std::abs(tc))
        return false;

    // eZ <cross> t[0]
    d1 = t[0].x() * ac.y() - t[0].y() * ac.x();
    d2 = t[0].x() * cc.y() - t[0].y() * cc.x();
    tc = (d1 + d2) * 0.5f;
    r = std::abs(h.y() * at[0].x() + h.x() * at[0].y());
    if (r + std::abs(tc - d1) < std::abs(tc))
        return false;

    // eZ <cross> t[1]
    d1 = t[1].x() * ac.y() - t[1].y() * ac.x();
    d2 = t[1].x() * bc.y() - t[1].y() * bc.x();
    tc = (d1 + d2) * 0.5f;
    r = std::abs(h.y() * at[1].x() + h.x() * at[1].y());
    if (r + std::abs(tc - d1) < std::abs(tc))
        return false;

    // eZ <cross> t[2]
    d1 = t[2].x() * ac.y() - t[2].y() * ac.x();
    d2 = t[2].x() * bc.y() - t[2].y() * bc.x();
    tc = (d1 + d2) * 0.5f;
    r = std::abs(h.y() * at[2].x() + h.x() * at[2].y());
    if (r + std::abs(tc - d1) < std::abs(tc))
        return false;

    // No separating axis exists, the AABB and triangle intersect.
    return true;
}

static double dist2_to_triangle(const Vec3d &a, const Vec3d &b, const Vec3d &c, const Vec3d &p)
{
    double out = std::numeric_limits<double>::max();
    const Vec3d v1 = b - a;
    auto        l1 = v1.squaredNorm();
    const Vec3d v2 = c - b;
    auto        l2 = v2.squaredNorm();
    const Vec3d v3 = a - c;
    auto        l3 = v3.squaredNorm();

    // Is the triangle valid?
    if (l1 > 0. && l2 > 0. && l3 > 0.) 
    {
        // 1) Project point into the plane of the triangle.
        const Vec3d n = v1.cross(v2);
        double d = (p - a).dot(n);
        const Vec3d foot_pt = p - n * d / n.squaredNorm();

        // 2) Maximum projection of n.
        int proj_axis;
        n.array().cwiseAbs().maxCoeff(&proj_axis);

        // 3) Test whether the foot_pt is inside the triangle.
        {
            auto inside_triangle = [](const Vec2d& v1, const Vec2d& v2, const Vec2d& v3, const Vec2d& pt) {
                const double d1 = cross2(v1, pt);
                const double d2 = cross2(v2, pt);
                const double d3 = cross2(v3, pt);
                // Testing both CCW and CW orientations.
                return (d1 >= 0. && d2 >= 0. && d3 >= 0.) || (d1 <= 0. && d2 <= 0. && d3 <= 0.);
            };
            bool inside;
            switch (proj_axis) {
            case 0: 
                inside = inside_triangle({v1.y(), v1.z()}, {v2.y(), v2.z()}, {v3.y(), v3.z()}, {foot_pt.y(), foot_pt.z()}); break;
            case 1: 
                inside = inside_triangle({v1.z(), v1.x()}, {v2.z(), v2.x()}, {v3.z(), v3.x()}, {foot_pt.z(), foot_pt.x()}); break;
            default: 
                assert(proj_axis == 2);
                inside = inside_triangle({v1.x(), v1.y()}, {v2.x(), v2.y()}, {v3.x(), v3.y()}, {foot_pt.x(), foot_pt.y()}); break;
            }
            if (inside)
                return (p - foot_pt).squaredNorm();
        }

        // 4) Find minimum distance to triangle vertices and edges.
        out = std::min((p - a).squaredNorm(), std::min((p - b).squaredNorm(), (p - c).squaredNorm()));
        auto t = (p - a).dot(v1);
        if (t > 0. && t < l1)
            out = std::min(out, (a + v1 * (t / l1) - p).squaredNorm());
        t = (p - b).dot(v2);
        if (t > 0. && t < l2)
            out = std::min(out, (b + v2 * (t / l2) - p).squaredNorm());
        t = (p - c).dot(v3);
        if (t > 0. && t < l3)
            out = std::min(out, (c + v3 * (t / l3) - p).squaredNorm());
    }

    return out;
}

// Ordering of children cubes.
static const std::array<Vec3d, 8> child_centers {
    Vec3d(-1, -1, -1), Vec3d( 1, -1, -1), Vec3d(-1,  1, -1), Vec3d( 1,  1, -1),
    Vec3d(-1, -1,  1), Vec3d( 1, -1,  1), Vec3d(-1,  1,  1), Vec3d( 1,  1,  1)
};

// Traversal order of octree children cells for three infill directions,
// so that a single line will be discretized in a strictly monotonous order.
static constexpr std::array<std::array<int, 8>, 3> child_traversal_order {
    std::array<int, 8>{ 2, 3, 0, 1, 6, 7, 4, 5 },
    std::array<int, 8>{ 4, 0, 6, 2, 5, 1, 7, 3 },
    std::array<int, 8>{ 1, 5, 0, 4, 3, 7, 2, 6 },
};

struct Cube
{
    Vec3d center;
#ifndef NDEBUG
    Vec3d center_octree;
#endif // NDEBUG
    std::array<Cube*, 8> children {}; // initialized to nullptrs
    Cube(const Vec3d &center) : center(center) {}
};

struct CubeProperties
{
    double edge_length;     // Lenght of edge of a cube
    double height;          // Height of rotated cube (standing on the corner)
    double diagonal_length; // Length of diagonal of a cube a face
    double line_z_distance; // Defines maximal distance from a center of a cube on Z axis on which lines will be created
    double line_xy_distance;// Defines maximal distance from a center of a cube on X and Y axis on which lines will be created
};

struct Octree
{
    // Octree will allocate its Cubes from the pool. The pool only supports deletion of the complete pool,
    // perfect for building up our octree.
    boost::object_pool<Cube>    pool;
    Cube*                       root_cube { nullptr };
    Vec3d                       origin;
    std::vector<CubeProperties> cubes_properties;

    Octree(const Vec3d &origin, const std::vector<CubeProperties> &cubes_properties)
        : root_cube(pool.construct(origin)), origin(origin), cubes_properties(cubes_properties) {}

    void insert_triangle(const Vec3d &a, const Vec3d &b, const Vec3d &c, Cube *current_cube, const BoundingBoxf3 &current_bbox, int depth);
};

void OctreeDeleter::operator()(Octree *p) {
    delete p;
}

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
    bool                       build_octree                   = false;
    const std::vector<double> &nozzle_diameters               = print_object.print()->config().nozzle_diameter.values;
    double                     max_nozzle_diameter            = *std::max_element(nozzle_diameters.begin(), nozzle_diameters.end());
    double                     default_infill_extrusion_width = Flow::auto_extrusion_width(FlowRole::frInfill, float(max_nozzle_diameter));
    for (const PrintRegion *region : print_object.print()->regions()) {
        const PrintRegionConfig &config   = region->config();
        bool                     nonempty = config.fill_density > 0;
        bool                     has_adaptive_infill = nonempty && config.fill_pattern == ipAdaptiveCubic;
        bool                     has_support_infill  = nonempty && config.fill_pattern == ipSupportCubic;
        region_fill_data.push_back(RegionFillData({
            has_adaptive_infill ? Tristate::Maybe : Tristate::No,
            has_support_infill ? Tristate::Maybe : Tristate::No,
            config.fill_density,
            config.infill_extrusion_width != 0. ? config.infill_extrusion_width : default_infill_extrusion_width
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

// Context used by generate_infill_lines() when recursively traversing an octree in a DDA fashion
// (Digital Differential Analyzer).
struct FillContext
{
    // The angles have to agree with child_traversal_order.
    static constexpr double direction_angles[3] {
        0.,
        (2.0 * M_PI) / 3.0,
        -(2.0 * M_PI) / 3.0
    };

    FillContext(const Octree &octree, double z_position, int direction_idx) :
        cubes_properties(octree.cubes_properties),
        z_position(z_position),
        traversal_order(child_traversal_order[direction_idx]),
        cos_a(cos(direction_angles[direction_idx])),
        sin_a(sin(direction_angles[direction_idx]))
    {
        static constexpr auto unused = std::numeric_limits<coord_t>::max();
        temp_lines.assign((1 << octree.cubes_properties.size()) - 1, Line(Point(unused, unused), Point(unused, unused)));
    }

    // Rotate the point, uses the same convention as Point::rotate().
    Vec2d rotate(const Vec2d& v) { return Vec2d(this->cos_a * v.x() - this->sin_a * v.y(), this->sin_a * v.x() + this->cos_a * v.y()); }

    const std::vector<CubeProperties>  &cubes_properties;
    // Top of the current layer.
    const double                        z_position;
    // Order of traversal for this line direction.
    const std::array<int, 8>            traversal_order;
    // Rotation of the generated line for this line direction.
    const double                        cos_a;
    const double                        sin_a;

    // Linearized tree spanning a single Octree wall, used to connect lines spanning
    // neighboring Octree cells. Unused lines have the Line::a::x set to infinity.
    std::vector<Line>                   temp_lines;
    // Final output
    std::vector<Line>                   output_lines;
};

static constexpr double octree_rot[3] = { 5.0 * M_PI / 4.0, Geometry::deg2rad(215.264), M_PI / 6.0 };

Eigen::Quaterniond transform_to_world()
{
    return Eigen::AngleAxisd(octree_rot[2], Vec3d::UnitZ()) * Eigen::AngleAxisd(octree_rot[1], Vec3d::UnitY()) * Eigen::AngleAxisd(octree_rot[0], Vec3d::UnitX());
}

Eigen::Quaterniond transform_to_octree()
{
    return Eigen::AngleAxisd(- octree_rot[0], Vec3d::UnitX()) * Eigen::AngleAxisd(- octree_rot[1], Vec3d::UnitY()) * Eigen::AngleAxisd(- octree_rot[2], Vec3d::UnitZ());
}

#ifndef NDEBUG
// Verify that the traversal order of the octree children matches the line direction,
// therefore the infill line may get extended with O(1) time & space complexity.
static bool verify_traversal_order(
    FillContext  &context,
    const Cube   *cube,
    int           depth,
    const Vec2d  &line_from,
    const Vec2d  &line_to)
{
    std::array<Vec3d, 8> c;
    Eigen::Quaterniond to_world = transform_to_world();
    for (int i = 0; i < 8; ++i) {
        int j = context.traversal_order[i];
        Vec3d cntr = to_world * (cube->center_octree + (child_centers[j] * (context.cubes_properties[depth].edge_length / 4.)));
        assert(!cube->children[j] || cube->children[j]->center.isApprox(cntr));
        c[i] = cntr;
    }
    std::array<Vec3d, 10> dirs = {
        c[1] - c[0], c[2] - c[0], c[3] - c[1], c[3] - c[2], c[3] - c[0],
        c[5] - c[4], c[6] - c[4], c[7] - c[5], c[7] - c[6], c[7] - c[4]
    };
    assert(std::abs(dirs[4].z()) < 0.005);
    assert(std::abs(dirs[9].z()) < 0.005);
    assert(dirs[0].isApprox(dirs[3]));
    assert(dirs[1].isApprox(dirs[2]));
    assert(dirs[5].isApprox(dirs[8]));
    assert(dirs[6].isApprox(dirs[7]));
    Vec3d line_dir = Vec3d(line_to.x() - line_from.x(), line_to.y() - line_from.y(), 0.).normalized();
    for (auto& dir : dirs) {
        double d = dir.normalized().dot(line_dir);
        assert(d > 0.7);
    }
    return true;
}
#endif // NDEBUG

static void generate_infill_lines_recursive(
    FillContext     &context,
    const Cube      *cube,
    // Address of this wall in the octree,  used to address context.temp_lines.
    int              address,
    int              depth)
{
    assert(cube != nullptr);

    const std::vector<CubeProperties> &cubes_properties = context.cubes_properties;
    const double z_diff     = context.z_position - cube->center.z();
    const double z_diff_abs = std::abs(z_diff);

    if (z_diff_abs > cubes_properties[depth].height / 2.)
        return;

    if (z_diff_abs < cubes_properties[depth].line_z_distance) {
        // Discretize a single wall splitting the cube into two.
        const double zdist = cubes_properties[depth].line_z_distance;
        Vec2d from(
            0.5 * cubes_properties[depth].diagonal_length * (zdist - z_diff_abs) / zdist,
            cubes_properties[depth].line_xy_distance - (zdist + z_diff) / sqrt(2.));
        Vec2d to(-from.x(), from.y());
        from = context.rotate(from);
        to   = context.rotate(to);
        // Relative to cube center
        const Vec2d offset(cube->center.x(), cube->center.y());
        from += offset;
        to   += offset;
        // Verify that the traversal order of the octree children matches the line direction,
        // therefore the infill line may get extended with O(1) time & space complexity.
        assert(verify_traversal_order(context, cube, depth, from, to));
        // Either extend an existing line or start a new one.
        Line &last_line = context.temp_lines[address];
        Line  new_line(Point::new_scale(from), Point::new_scale(to));
        if (last_line.a.x() == std::numeric_limits<coord_t>::max()) {
            last_line.a = new_line.a;
        } else if ((new_line.a - last_line.b).cwiseAbs().maxCoeff() > 1000) { // SCALED_EPSILON is 100 and it is not enough
            context.output_lines.emplace_back(last_line);
            last_line.a = new_line.a;
        }
        last_line.b = new_line.b;
    }

    // left child index
    address = address * 2 + 1;
    -- depth;
    size_t i = 0;
    for (const int child_idx : context.traversal_order) {
        const Cube *child = cube->children[child_idx];
        if (child != nullptr)
            generate_infill_lines_recursive(context, child, address, depth);
        if (++ i == 4)
            // right child index
            ++ address;
    }
}

#ifndef NDEBUG
//    #define ADAPTIVE_CUBIC_INFILL_DEBUG_OUTPUT
#endif

#ifdef ADAPTIVE_CUBIC_INFILL_DEBUG_OUTPUT
static void export_infill_lines_to_svg(const ExPolygon &expoly, const Polylines &polylines, const std::string &path)
{
    BoundingBox bbox = get_extents(expoly);
    bbox.offset(scale_(3.));

    ::Slic3r::SVG svg(path, bbox);
    svg.draw(expoly);
    svg.draw_outline(expoly, "green");
    svg.draw(polylines, "red");
    static constexpr double trim_length = scale_(0.4);
    for (Polyline polyline : polylines) {
        Vec2d a = polyline.points.front().cast<double>();
        Vec2d d = polyline.points.back().cast<double>();
        if (polyline.size() == 2) {
            Vec2d v = d - a;
            double l = v.norm();
            if (l > 2. * trim_length) {
                a += v * trim_length / l;
                d -= v * trim_length / l;
                polyline.points.front() = a.cast<coord_t>();
                polyline.points.back() = d.cast<coord_t>();
            } else
                polyline.points.clear();
        } else if (polyline.size() > 2) {
            Vec2d b = polyline.points[1].cast<double>();
            Vec2d c = polyline.points[polyline.points.size() - 2].cast<double>();
            Vec2d v = b - a;
            double l = v.norm();
            if (l > trim_length) {
                a += v * trim_length / l;
                polyline.points.front() = a.cast<coord_t>();
            } else
                polyline.points.erase(polyline.points.begin());
            v = d - c;
            l = v.norm();
            if (l > trim_length)
                polyline.points.back() = (d - v * trim_length / l).cast<coord_t>();
            else
                polyline.points.pop_back();
        }
        svg.draw(polyline, "black");
    }
}
#endif /* ADAPTIVE_CUBIC_INFILL_DEBUG_OUTPUT */

static Matrix2d rotation_matrix_from_vector(const Point &vector)
{
    Matrix2d rotation;
    rotation.block<1, 2>(0, 0) = vector.cast<double>().normalized();
    rotation(1, 0)             = -rotation(0, 1);
    rotation(1, 1)             = rotation(0, 0);
    return rotation;
}

struct Intersection
{
    // Index of the closest line to intersect_line
    size_t    closest_line_idx;
    // Copy of closest line to intersect_point, used for storing original line in an unchanged state
    Line      closest_line;
    // Point for which is computed closest line (closest_line)
    Point     intersect_point;
    // Index of the polyline from which is computed closest_line
    size_t    intersect_pl_idx;
    // Pointer to the polyline from which is computed closest_line
    Polyline *intersect_pl;
    // The line for which is computed closest line from intersect_point to closest_line
    Line      intersect_line;
    // Indicate if intersect_point is the first or the last point of intersect_pl
    bool      forward;
    // Indication if this intersection has been proceed
    bool      used = false;

    Intersection(const size_t closest_line_idx,
                 const Line  &closest_line,
                 const Point &intersect_point,
                 size_t       intersect_pl_idx,
                 Polyline    *intersect_pl,
                 const Line  &intersect_line,
                 bool         forward)
        : closest_line_idx(closest_line_idx)
        , closest_line(closest_line)
        , intersect_point(intersect_point)
        , intersect_pl_idx(intersect_pl_idx)
        , intersect_pl(intersect_pl)
        , intersect_line(intersect_line)
        , forward(forward)
    {}
};

static inline Intersection *get_nearest_intersection(std::vector<std::pair<Intersection, double>> &intersect_line, const size_t first_idx)
{
    assert(intersect_line.size() >= 2);
    if (first_idx == 0)
        return &intersect_line[first_idx + 1].first;
    else if (first_idx == (intersect_line.size() - 1))
        return &intersect_line[first_idx - 1].first;
    else if ((intersect_line[first_idx].second - intersect_line[first_idx - 1].second) < (intersect_line[first_idx + 1].second - intersect_line[first_idx].second))
        return &intersect_line[first_idx - 1].first;
    else
        return &intersect_line[first_idx + 1].first;
}

// Create a line based on line_to_offset translated it in the direction of the intersection line (intersection.intersect_line)
static Line create_offset_line(const Line &line_to_offset, const Intersection &intersection, const double scaled_spacing)
{
    Matrix2d rotation          = rotation_matrix_from_vector(line_to_offset.vector());
    Vec2d    offset_vector     = ((scaled_spacing / 2.) * line_to_offset.normal().cast<double>().normalized());
    Vec2d    offset_line_point = line_to_offset.a.cast<double>();
    Vec2d    furthest_point    = (intersection.intersect_point == intersection.intersect_line.a ? intersection.intersect_line.b : intersection.intersect_line.a).cast<double>();

    if ((rotation * furthest_point).y() >= (rotation * offset_line_point).y()) offset_vector *= -1;

    Line  offset_line    = line_to_offset;
    offset_line.translate(offset_vector.x(), offset_vector.y());
    // Extend the line by small value to guarantee a collision with adjacent lines
    offset_line.extend(coord_t(scale_(1.)));
    return offset_line;
};

namespace bg  = boost::geometry;
namespace bgm = boost::geometry::model;
namespace bgi = boost::geometry::index;

// float is needed because for coord_t bgi::intersects throws "bad numeric conversion: positive overflow"
using rtree_point_t   = bgm::point<float, 2, boost::geometry::cs::cartesian>;
using rtree_segment_t = bgm::segment<rtree_point_t>;
using rtree_t         = bgi::rtree<std::pair<rtree_segment_t, size_t>, bgi::rstar<16, 4>>;

static inline rtree_segment_t mk_rtree_seg(const Point &a, const Point &b) {
    return { rtree_point_t(float(a.x()), float(a.y())), rtree_point_t(float(b.x()), float(b.y())) };
}
static inline rtree_segment_t mk_rtree_seg(const Line &l) {
    return mk_rtree_seg(l.a, l.b);
}

// Create a hook based on hook_line and append it to the begin or end of the polyline in the intersection
static void add_hook(const Intersection &intersection, const Line &hook_line, const double scaled_spacing, const int hook_length, const rtree_t &rtree)
{
    Vec2d  hook_vector_norm = hook_line.vector().cast<double>().normalized();
    Vector hook_vector      = (hook_length * hook_vector_norm).cast<coord_t>();
    Line   hook_line_offset = create_offset_line(hook_line, intersection, scaled_spacing);

    Point intersection_point;
    bool  intersection_found = intersection.intersect_line.intersection(hook_line_offset, &intersection_point);
    assert(intersection_found);

    Line hook_forward(intersection_point, intersection_point + hook_vector);
    Line hook_backward(intersection_point, intersection_point - hook_vector);

    auto filter_itself = [&intersection](const auto &item) {
        const rtree_segment_t &seg     = item.first;
        const Point           &i_point = intersection.intersect_point;
        return !((float(i_point.x()) == bg::get<0, 0>(seg) && float(i_point.y()) == bg::get<0, 1>(seg)) ||
                 (float(i_point.x()) == bg::get<1, 0>(seg) && float(i_point.y()) == bg::get<1, 1>(seg)));
    };

    std::vector<std::pair<rtree_segment_t, size_t>> hook_intersections;
    rtree.query(bgi::intersects(mk_rtree_seg(hook_forward)) && bgi::satisfies(filter_itself),
                std::back_inserter(hook_intersections));

    auto max_hook_length = [&hook_intersections, &hook_length](const Line &hook) {
        coord_t max_length = hook_length;
        for (const auto &hook_intersection : hook_intersections) {
            const rtree_segment_t &segment = hook_intersection.first;
            double                 dist    = Line::distance_to(hook.a, Point(bg::get<0, 0>(segment), bg::get<0, 1>(segment)),
                                            Point(bg::get<1, 0>(segment), bg::get<1, 1>(segment)));
            max_length                     = std::min(coord_t(dist), max_length);
        }
        return max_length;
    };

    Line hook_final;
    if (hook_intersections.empty()) {
        hook_final = std::move(hook_forward);
    } else {
        // There is not enough space for the hook, try another direction
        coord_t hook_forward_max_length = max_hook_length(hook_forward);
        hook_intersections.clear();
        rtree.query(bgi::intersects(mk_rtree_seg(hook_backward)) && bgi::satisfies(filter_itself),
                    std::back_inserter(hook_intersections));

        if (hook_intersections.empty()) {
            hook_final = std::move(hook_backward);
        } else {
            // There is not enough space for hook in both directions, shrink the hook
            coord_t hook_backward_max_length = max_hook_length(hook_backward);
            if (hook_forward_max_length > hook_backward_max_length) {
                Vector hook_vector_reduced = (hook_forward_max_length * hook_vector_norm).cast<coord_t>();
                hook_final                 = Line(intersection_point, intersection_point + hook_vector_reduced);
            } else {
                Vector hook_vector_reduced = (hook_backward_max_length * hook_vector_norm).cast<coord_t>();
                hook_final                 = Line(intersection_point, intersection_point - hook_vector_reduced);
            }
        }
    }

    if (intersection.forward) {
        intersection.intersect_pl->points.front() = hook_final.a;
        intersection.intersect_pl->points.emplace(intersection.intersect_pl->points.begin(), hook_final.b);
    } else {
        intersection.intersect_pl->points.back() = hook_final.a;
        intersection.intersect_pl->points.emplace_back(hook_final.b);
    }
}

static Polylines connect_lines_using_hooks(Polylines &&lines, const ExPolygon &boundary, const double spacing, const int hook_length)
{
    rtree_t rtree;
    size_t  poly_idx = 0;
    for (const Polyline &poly : lines) {
        rtree.insert(std::make_pair(mk_rtree_seg(poly.points.front(), poly.points.back()), poly_idx++));
    }

    std::vector<Intersection> intersections;
    {
        const coord_t scaled_spacing = coord_t(scale_(spacing));
        // Keeping the vector of closest points outside the loop, so the vector does not need to be reallocated.
        std::vector<std::pair<rtree_segment_t, size_t>> closest;
        for (size_t line_idx = 0; line_idx < lines.size(); ++line_idx) {
            Polyline &line = lines[line_idx];
            // Lines shorter than spacing are skipped because it is needed to shrink a line by the value of spacing.
            // A shorter line than spacing could produce a degenerate polyline.
            if (line.length() <= (scaled_spacing + SCALED_EPSILON)) continue;

            Point                                           front_point = line.points.front();
            Point                                           back_point  = line.points.back();

            auto filter_itself = [line_idx](const auto &item) { return item.second != line_idx; };

            // Find the nearest line from the start point of the line.
            closest.clear();
            rtree.query(bgi::nearest(rtree_point_t(float(front_point.x()), float(front_point.y())), 1) && bgi::satisfies(filter_itself), std::back_inserter(closest));
            if (((Line) lines[closest[0].second]).distance_to(front_point) <= 1000)
                intersections.emplace_back(closest[0].second, (Line) lines[closest[0].second], front_point, line_idx, &line, (Line) line, true);

            // Find the nearest line from the end point of the line
            closest.clear();
            rtree.query(bgi::nearest(rtree_point_t(float(back_point.x()), float(back_point.y())), 1) && bgi::satisfies(filter_itself), std::back_inserter(closest));
            if (((Line) lines[closest[0].second]).distance_to(back_point) <= 1000)
                intersections.emplace_back(closest[0].second, (Line) lines[closest[0].second], back_point, line_idx, &line, (Line) line, false);
        }
    }

    std::sort(intersections.begin(), intersections.end(),
              [](const Intersection &i1, const Intersection &i2) { return i1.closest_line_idx < i2.closest_line_idx; });

    std::vector<size_t> merged_with(lines.size());
    std::iota(merged_with.begin(), merged_with.end(), 0);

    // Appends the boundary polygon with all holes to rtree for detection if hooks not crossing the boundary
    {
        Point prev = boundary.contour.points.back();
        for (const Point &point : boundary.contour.points) {
            rtree.insert(std::make_pair(mk_rtree_seg(prev, point), poly_idx++));
            prev = point;
        }
        for (const Polygon &polygon : boundary.holes) {
            Point prev = polygon.points.back();
            for (const Point &point : polygon.points) {
                rtree.insert(std::make_pair(mk_rtree_seg(prev, point), poly_idx++));
                prev = point;
            }
        }
    }

    auto update_merged_polyline = [&lines, &merged_with](Intersection &intersection) {
        // Update the polyline index to index which is merged
        for (size_t last = intersection.intersect_pl_idx;;) {
            size_t lower = merged_with[last];
            if (lower == last) {
                merged_with[intersection.intersect_pl_idx] = lower;
                intersection.intersect_pl_idx              = lower;
                break;
            }
            last = lower;
        }

        intersection.intersect_pl = &lines[intersection.intersect_pl_idx];
        // After polylines are merged, it is necessary to update "forward" based on if intersect_point is the first or the last point of intersect_pl.
        if (!intersection.used && !intersection.intersect_pl->points.empty())
            intersection.forward = (intersection.intersect_pl->points.front() == intersection.intersect_point);
    };

    for (size_t min_idx = 0; min_idx < intersections.size(); ++min_idx) {
        std::vector<std::pair<Intersection, double>> intersect_line;
        Matrix2d                                     rotation = rotation_matrix_from_vector(intersections[min_idx].closest_line.vector());
        intersect_line.emplace_back(intersections[min_idx], (rotation * intersections[min_idx].intersect_point.cast<double>()).x());
        // All the nearest points on the same line are projected on this line. Because of it, it can easily find the nearest point
        for (size_t max_idx = min_idx + 1; max_idx < intersections.size(); ++max_idx) {
            if (intersections[min_idx].closest_line_idx != intersections[max_idx].closest_line_idx) break;

            intersect_line.emplace_back(intersections[max_idx], (rotation * intersections[max_idx].intersect_point.cast<double>()).x());
            min_idx = max_idx;
        }

        assert(!intersect_line.empty());
        if (intersect_line.size() <= 1) {
            // On the adjacent line is only one intersection
            Intersection &first_i = intersect_line.front().first;
            if (first_i.used || first_i.intersect_pl->points.empty()) continue;

            add_hook(first_i, first_i.closest_line, scale_(spacing), hook_length, rtree);
            first_i.used = true;
            continue;
        }

        assert(intersect_line.size() >= 2);
        std::sort(intersect_line.begin(), intersect_line.end(), [](const auto &i1, const auto &i2) { return i1.second < i2.second; });
        for (size_t first_idx = 0; first_idx < intersect_line.size(); ++first_idx) {
            Intersection &first_i   = intersect_line[first_idx].first;
            Intersection &nearest_i = *get_nearest_intersection(intersect_line, first_idx);

            update_merged_polyline(first_i);
            update_merged_polyline(nearest_i);

            // The intersection has been processed, or the polyline has been merge to another polyline.
            if (first_i.used || first_i.intersect_pl->points.empty()) continue;

            // A line between two intersections points
            Line   intersection_line(first_i.intersect_point, nearest_i.intersect_point);
            Line   offset_line              = create_offset_line(intersection_line, first_i, scale_(spacing));
            double intersection_line_length = intersection_line.length();

            // Check if both intersections lie on the offset_line and simultaneously get their points of intersecting.
            // These points are used as start and end of the hook
            Point first_i_point, nearest_i_point;
            if (first_i.intersect_line.intersection(offset_line, &first_i_point) &&
                nearest_i.intersect_line.intersection(offset_line, &nearest_i_point)) {
                // Both intersections are so close that their polylines can be connected
                if (!nearest_i.used && !nearest_i.intersect_pl->points.empty() && intersection_line_length <= 2 * hook_length) {
                    if (first_i.intersect_pl_idx == nearest_i.intersect_pl_idx) {
                        // Both intersections are on the same polyline
                        if (!first_i.forward) { std::swap(first_i_point, nearest_i_point); }

                        first_i.intersect_pl->points.front() = first_i_point;
                        first_i.intersect_pl->points.back()  = nearest_i_point;
                        first_i.intersect_pl->points.emplace(first_i.intersect_pl->points.begin(), nearest_i_point);
                    } else {
                        // Both intersections are on different polylines
                        Points merge_polyline_points;
                        size_t first_polyline_size     = first_i.intersect_pl->points.size();
                        size_t nearest_polyline_size   = nearest_i.intersect_pl->points.size();
                        merge_polyline_points.reserve(first_polyline_size + nearest_polyline_size);

                        if (first_i.forward) {
                            if (nearest_i.forward)
                                for (auto it = nearest_i.intersect_pl->points.rbegin(); it != nearest_i.intersect_pl->points.rend(); ++it)
                                    merge_polyline_points.emplace_back(*it);
                            else
                                for (const Point &point : nearest_i.intersect_pl->points)
                                    merge_polyline_points.emplace_back(point);

                            append(merge_polyline_points, std::move(first_i.intersect_pl->points));
                            merge_polyline_points[nearest_polyline_size - 1] = nearest_i_point;
                            merge_polyline_points[nearest_polyline_size]     = first_i_point;
                        } else {
                            append(merge_polyline_points, std::move(first_i.intersect_pl->points));
                            if (nearest_i.forward)
                                for (const Point &point : nearest_i.intersect_pl->points)
                                    merge_polyline_points.emplace_back(point);
                            else
                                for (auto it = nearest_i.intersect_pl->points.rbegin(); it != nearest_i.intersect_pl->points.rend(); ++it)
                                    merge_polyline_points.emplace_back(*it);

                            merge_polyline_points[first_polyline_size - 1] = first_i_point;
                            merge_polyline_points[first_polyline_size]     = nearest_i_point;
                        }

                        merged_with[nearest_i.intersect_pl_idx] = merged_with[first_i.intersect_pl_idx];

                        nearest_i.intersect_pl->points.clear();
                        first_i.intersect_pl->points = merge_polyline_points;
                    }

                    first_i.used   = true;
                    nearest_i.used = true;
                } else {
                    add_hook(first_i, first_i.closest_line, scale_(spacing), hook_length, rtree);
                    first_i.used = true;
                }
            }
        }
    }

    Polylines polylines_out;
    polylines_out.reserve(polylines_out.size() + std::count_if(lines.begin(), lines.end(), [](const Polyline &pl) { return !pl.empty(); }));
    for (Polyline &pl : lines)
        if (!pl.empty()) polylines_out.emplace_back(std::move(pl));
    return polylines_out;
}

coord_t get_hook_length(const double spacing) { return coord_t(scale_(spacing)) * 5; }

void Filler::_fill_surface_single(
    const FillParams              &params,
    unsigned int                   thickness_layers,
    const std::pair<float, Point> &direction,
    ExPolygon                     &expolygon,
    Polylines                     &polylines_out)
{
    assert (this->adapt_fill_octree);

    Polylines all_polylines;
    {
        // 3 contexts for three directions of infill lines
        std::array<FillContext, 3> contexts { 
            FillContext { *adapt_fill_octree, this->z, 0 },
            FillContext { *adapt_fill_octree, this->z, 1 },
            FillContext { *adapt_fill_octree, this->z, 2 }
        };
        // Generate the infill lines along the octree cells, merge touching lines of the same direction.
        size_t num_lines = 0;
        for (auto &context : contexts) {
            generate_infill_lines_recursive(context, adapt_fill_octree->root_cube, 0, int(adapt_fill_octree->cubes_properties.size()) - 1);
            num_lines += context.output_lines.size() + context.temp_lines.size();
        }
        // Collect the lines.
        std::vector<Line> lines;
        lines.reserve(num_lines);
        for (auto &context : contexts) {
            append(lines, context.output_lines);
            for (const Line &line : context.temp_lines)
                if (line.a.x() != std::numeric_limits<coord_t>::max())
                    lines.emplace_back(line);
        }
#if 0
        // Chain touching line segments, convert lines to polylines.
        //all_polylines = chain_lines(lines, 300.); // SCALED_EPSILON is 100 and it is not enough
#else
        // Convert lines to polylines.
        all_polylines.reserve(lines.size());
        std::transform(lines.begin(), lines.end(), std::back_inserter(all_polylines), [](const Line& l) { return Polyline{ l.a, l.b }; });
#endif
    }

    // Crop all polylines
    all_polylines = intersection_pl(std::move(all_polylines), to_polygons(expolygon));

    // After intersection_pl some polylines with only one line are split into more lines
    for (Polyline &polyline : all_polylines) {
        //FIXME assert that all the points are collinear and in between the start and end point.
        if (polyline.points.size() > 2)
            polyline.points.erase(polyline.points.begin() + 1, polyline.points.end() - 1);
    }

#ifdef ADAPTIVE_CUBIC_INFILL_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_infill_lines_to_svg(expolygon, all_polylines, debug_out_path("FillAdaptive-initial-%d.svg", iRun++));
    }
#endif /* ADAPTIVE_CUBIC_INFILL_DEBUG_OUTPUT */

    coord_t   hook_length = get_hook_length(this->spacing);
    Polylines all_polylines_with_hooks = connect_lines_using_hooks(std::move(all_polylines), expolygon, this->spacing, hook_length);

#ifdef ADAPTIVE_CUBIC_INFILL_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_infill_lines_to_svg(expolygon, all_polylines_with_hooks, debug_out_path("FillAdaptive-hooks-%d.svg", iRun++));
    }
#endif /* ADAPTIVE_CUBIC_INFILL_DEBUG_OUTPUT */

    if (params.dont_connect || all_polylines_with_hooks.size() <= 1)
        append(polylines_out, std::move(all_polylines_with_hooks));
    else
        connect_infill(chain_polylines(std::move(all_polylines_with_hooks)), expolygon, polylines_out, this->spacing, params, hook_length);

#ifdef ADAPTIVE_CUBIC_INFILL_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_infill_lines_to_svg(expolygon, polylines_out, debug_out_path("FillAdaptive-final-%d.svg", iRun ++));
    }
#endif /* ADAPTIVE_CUBIC_INFILL_DEBUG_OUTPUT */
}

static double bbox_max_radius(const BoundingBoxf3 &bbox, const Vec3d &center)
{
    const auto p = (bbox.min - center);
    const auto s = bbox.size();
    double r2max = 0.;
    for (int i = 0; i < 8; ++ i)
        r2max = std::max(r2max, (p + Vec3d(s.x() * double(i & 1), s.y() * double(i & 2), s.z() * double(i & 4))).squaredNorm());
    return sqrt(r2max);
}

static std::vector<CubeProperties> make_cubes_properties(double max_cube_edge_length, double line_spacing)
{
    max_cube_edge_length += EPSILON;

    std::vector<CubeProperties> cubes_properties;
    for (double edge_length = line_spacing * 2.;; edge_length *= 2.)
    {
        CubeProperties props{};
        props.edge_length = edge_length;
        props.height = edge_length * sqrt(3);
        props.diagonal_length = edge_length * sqrt(2);
        props.line_z_distance = edge_length / sqrt(3);
        props.line_xy_distance = edge_length / sqrt(6);
        cubes_properties.emplace_back(props);
        if (edge_length > max_cube_edge_length)
            break;
    }
    return cubes_properties;
}

static inline bool is_overhang_triangle(const Vec3d &a, const Vec3d &b, const Vec3d &c, const Vec3d &up)
{
    // Calculate triangle normal.
    auto n = (b - a).cross(c - b);
    return n.dot(up) > 0.707 * n.norm();
}

static void transform_center(Cube *current_cube, const Eigen::Matrix3d &rot)
{
#ifndef NDEBUG
    current_cube->center_octree = current_cube->center;
#endif // NDEBUG
    current_cube->center = rot * current_cube->center;
    for (auto *child : current_cube->children)
        if (child)
            transform_center(child, rot);
}

OctreePtr build_octree(
    // Mesh is rotated to the coordinate system of the octree.
    const indexed_triangle_set  &triangle_mesh,
    // Overhang triangles extracted from fill surfaces with stInternalBridge type,
    // rotated to the coordinate system of the octree.
    const std::vector<Vec3d>    &overhang_triangles, 
    coordf_t                     line_spacing,
    bool                         support_overhangs_only)
{
    assert(line_spacing > 0);
    assert(! std::isnan(line_spacing));

    BoundingBox3Base<Vec3f>     bbox(triangle_mesh.vertices);
    Vec3d                       cube_center      = bbox.center().cast<double>();
    std::vector<CubeProperties> cubes_properties = make_cubes_properties(double(bbox.size().maxCoeff()), line_spacing);
    auto                        octree           = OctreePtr(new Octree(cube_center, cubes_properties));

    if (cubes_properties.size() > 1) {
        Octree *octree_ptr = octree.get();
        double edge_length_half = 0.5 * cubes_properties.back().edge_length;
        Vec3d  diag_half(edge_length_half, edge_length_half, edge_length_half);
        int    max_depth = int(cubes_properties.size()) - 1;
        auto process_triangle = [octree_ptr, max_depth, diag_half](const Vec3d &a, const Vec3d &b, const Vec3d &c) {
            octree_ptr->insert_triangle(
                a, b, c,
                octree_ptr->root_cube,
                BoundingBoxf3(octree_ptr->root_cube->center - diag_half, octree_ptr->root_cube->center + diag_half),
                max_depth);
        };
        auto up_vector = support_overhangs_only ? Vec3d(transform_to_octree() * Vec3d(0., 0., 1.)) : Vec3d();
        for (auto &tri : triangle_mesh.indices) {
            auto a = triangle_mesh.vertices[tri[0]].cast<double>();
            auto b = triangle_mesh.vertices[tri[1]].cast<double>();
            auto c = triangle_mesh.vertices[tri[2]].cast<double>();
            if (! support_overhangs_only || is_overhang_triangle(a, b, c, up_vector))
                process_triangle(a, b, c);
        }
        for (size_t i = 0; i < overhang_triangles.size(); i += 3)
            process_triangle(overhang_triangles[i], overhang_triangles[i + 1], overhang_triangles[i + 2]);
        {
            // Transform the octree to world coordinates to reduce computation when extracting infill lines.
            auto rot = transform_to_world().toRotationMatrix();
            transform_center(octree->root_cube, rot);
            octree->origin = rot * octree->origin;
        }
    }

    return octree;
}

void Octree::insert_triangle(const Vec3d &a, const Vec3d &b, const Vec3d &c, Cube *current_cube, const BoundingBoxf3 &current_bbox, int depth)
{
    assert(current_cube);
    assert(depth > 0);

    // Squared radius of a sphere around the child cube.
    const double r2_cube = Slic3r::sqr(0.5 * this->cubes_properties[-- depth].height + EPSILON);

    for (size_t i = 0; i < 8; ++ i) {
        const Vec3d &child_center_dir = child_centers[i];
        // Calculate a slightly expanded bounding box of a child cube to cope with triangles touching a cube wall and other numeric errors.
        // We will rather densify the octree a bit more than necessary instead of missing a triangle.
        BoundingBoxf3 bbox;
        for (int k = 0; k < 3; ++ k) {
            if (child_center_dir[k] == -1.) {
                bbox.min[k] = current_bbox.min[k];
                bbox.max[k] = current_cube->center[k] + EPSILON;
            } else {
                bbox.min[k] = current_cube->center[k] - EPSILON;
                bbox.max[k] = current_bbox.max[k];
            }
        }
        Vec3d child_center = current_cube->center + (child_center_dir * (this->cubes_properties[depth].edge_length / 2.));
        //if (dist2_to_triangle(a, b, c, child_center) < r2_cube) {
        if (triangle_AABB_intersects(a, b, c, bbox)) {
            if (! current_cube->children[i])
                current_cube->children[i] = this->pool.construct(child_center);
            if (depth > 0)
                this->insert_triangle(a, b, c, current_cube->children[i], bbox, depth);
        }
    }
}

} // namespace FillAdaptive
} // namespace Slic3r
