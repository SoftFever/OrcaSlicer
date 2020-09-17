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

// Boost pool: Don't use mutexes to synchronize memory allocation.
#define BOOST_POOL_NO_MT
#include <boost/pool/object_pool.hpp>

namespace Slic3r {

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

namespace FillAdaptive_Internal
{
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
}; // namespace FillAdaptive_Internal

std::pair<double, double> FillAdaptive_Internal::adaptive_fill_line_spacing(const PrintObject &print_object)
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
        bool                     has_support_infill  = nonempty && config.fill_pattern == ipSupportCubic;
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

    FillContext(const FillAdaptive_Internal::Octree &octree, double z_position, int direction_idx) :
        origin_world(octree.origin),
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

    // Center of the root cube in the Octree coordinate system.
    const Vec3d                                                 origin_world;
    const std::vector<FillAdaptive_Internal::CubeProperties>   &cubes_properties;
    // Top of the current layer.
    const double                                                z_position;
    // Order of traversal for this line direction.
    const std::array<int, 8>                                    traversal_order;
    // Rotation of the generated line for this line direction.
    const double                                                cos_a;
    const double                                                sin_a;

    // Linearized tree spanning a single Octree wall, used to connect lines spanning
    // neighboring Octree cells. Unused lines have the Line::a::x set to infinity.
    std::vector<Line>                                           temp_lines;
    // Final output
    std::vector<Line>                                           output_lines;
};

static constexpr double octree_rot[3] = { 5.0 * M_PI / 4.0, Geometry::deg2rad(215.264), M_PI / 6.0 };

Eigen::Quaterniond FillAdaptive_Internal::adaptive_fill_octree_transform_to_world()
{
    return Eigen::AngleAxisd(octree_rot[2], Vec3d::UnitZ()) * Eigen::AngleAxisd(octree_rot[1], Vec3d::UnitY()) * Eigen::AngleAxisd(octree_rot[0], Vec3d::UnitX());
}

Eigen::Quaterniond FillAdaptive_Internal::adaptive_fill_octree_transform_to_octree()
{
    return Eigen::AngleAxisd(- octree_rot[0], Vec3d::UnitX()) * Eigen::AngleAxisd(- octree_rot[1], Vec3d::UnitY()) * Eigen::AngleAxisd(- octree_rot[2], Vec3d::UnitZ());
}

#ifndef NDEBUG
// Verify that the traversal order of the octree children matches the line direction,
// therefore the infill line may get extended with O(1) time & space complexity.
static bool verify_traversal_order(
    FillContext                         &context,
    const FillAdaptive_Internal::Cube   *cube,
    int                                  depth,
    const Vec2d                         &line_from,
    const Vec2d                         &line_to)
{
    std::array<Vec3d, 8> c;
    Eigen::Quaterniond to_world = FillAdaptive_Internal::adaptive_fill_octree_transform_to_world();
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
    assert(std::abs(dirs[4].z()) < 0.001);
    assert(std::abs(dirs[9].z()) < 0.001);
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
    FillContext                         &context,
    const FillAdaptive_Internal::Cube   *cube,
    // Address of this wall in the octree,  used to address context.temp_lines.
    int                                  address,
    int                                  depth)
{
    assert(cube != nullptr);

    const std::vector<FillAdaptive_Internal::CubeProperties> &cubes_properties = context.cubes_properties;
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
        Vec2d offset(cube->center.x() - context.origin_world.x(), cube->center.y() - context.origin_world.y());
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
        } else if ((new_line.a - last_line.b).cwiseAbs().maxCoeff() > 300) { // SCALED_EPSILON is 100 and it is not enough) {
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
        const FillAdaptive_Internal::Cube *child = cube->children[child_idx];
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

void FillAdaptive::_fill_surface_single(
    const FillParams &             params,
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
        // Convert lines to polylines.
        all_polylines.reserve(lines.size());
        std::transform(lines.begin(), lines.end(), std::back_inserter(all_polylines), [](const Line& l) { return Polyline{ l.a, l.b }; });
    }

    // Crop all polylines
    all_polylines = intersection_pl(std::move(all_polylines), to_polygons(expolygon));

#ifdef ADAPTIVE_CUBIC_INFILL_DEBUG_OUTPUT
    {
        static int iRun = 0;
        export_infill_lines_to_svg(expolygon, all_polylines, debug_out_path("FillAdaptive-initial-%d.svg", iRun++));
    }
#endif /* ADAPTIVE_CUBIC_INFILL_DEBUG_OUTPUT */

    if (params.dont_connect)
        append(polylines_out, std::move(all_polylines));
    else {
        Polylines boundary_polylines;
        Polylines non_boundary_polylines;
        for (const Polyline &polyline : all_polylines)
            // connect_infill required all polylines to touch the boundary.
            if (polyline.lines().size() == 1 && expolygon.has_boundary_point(polyline.lines().front().a) && expolygon.has_boundary_point(polyline.lines().front().b))
                boundary_polylines.push_back(polyline);
            else
                non_boundary_polylines.push_back(polyline);

        if (!boundary_polylines.empty()) {
            boundary_polylines = chain_polylines(boundary_polylines);
            FillAdaptive::connect_infill(std::move(boundary_polylines), expolygon, polylines_out, this->spacing, params);
        }

        append(polylines_out, std::move(non_boundary_polylines));
    }

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

static std::vector<FillAdaptive_Internal::CubeProperties> make_cubes_properties(double max_cube_edge_length, double line_spacing)
{
    max_cube_edge_length += EPSILON;

    std::vector<FillAdaptive_Internal::CubeProperties> cubes_properties;
    for (double edge_length = line_spacing * 2.;; edge_length *= 2.)
    {
        FillAdaptive_Internal::CubeProperties props{};
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

static void transform_center(FillAdaptive_Internal::Cube *current_cube, const Eigen::Matrix3d &rot)
{
#ifndef NDEBUG
    current_cube->center_octree = current_cube->center;
#endif // NDEBUG
    current_cube->center = rot * current_cube->center;
    for (auto *child : current_cube->children)
        if (child)
            transform_center(child, rot);
}

FillAdaptive_Internal::OctreePtr FillAdaptive_Internal::build_octree(const indexed_triangle_set &triangle_mesh, const Vec3d &up_vector, coordf_t line_spacing, bool support_overhangs_only)
{
    assert(line_spacing > 0);
    assert(! std::isnan(line_spacing));

    BoundingBox3Base<Vec3f>     bbox(triangle_mesh.vertices);
    Vec3d                       cube_center      = bbox.center().cast<double>();
    std::vector<CubeProperties> cubes_properties = make_cubes_properties(double(bbox.size().maxCoeff()), line_spacing);
    auto                        octree           = OctreePtr(new Octree(cube_center, cubes_properties));

    if (cubes_properties.size() > 1) {
        for (auto &tri : triangle_mesh.indices) {
            auto a = triangle_mesh.vertices[tri[0]].cast<double>();
            auto b = triangle_mesh.vertices[tri[1]].cast<double>();
            auto c = triangle_mesh.vertices[tri[2]].cast<double>();
            if (support_overhangs_only && ! is_overhang_triangle(a, b, c, up_vector))
                continue;
            double edge_length_half = 0.5 * cubes_properties.back().edge_length;
            Vec3d  diag_half(edge_length_half, edge_length_half, edge_length_half);
            octree->insert_triangle(
                a, b, c,
                octree->root_cube, 
                BoundingBoxf3(octree->root_cube->center - diag_half, octree->root_cube->center + diag_half),
                int(cubes_properties.size()) - 1);
        }
        {
            // Transform the octree to world coordinates to reduce computation when extracting infill lines.
            auto rot = adaptive_fill_octree_transform_to_world().toRotationMatrix();
            transform_center(octree->root_cube, rot);
            octree->origin = rot * octree->origin;
        }
    }

    return octree;
}

void FillAdaptive_Internal::Octree::insert_triangle(const Vec3d &a, const Vec3d &b, const Vec3d &c, Cube *current_cube, const BoundingBoxf3 &current_bbox, int depth)
{
    assert(current_cube);
    assert(depth > 0);

    for (size_t i = 0; i < 8; ++ i) {
        const Vec3d &child_center = child_centers[i];
        // Calculate a slightly expanded bounding box of a child cube to cope with triangles touching a cube wall and other numeric errors.
        // We will rather densify the octree a bit more than necessary instead of missing a triangle.
        BoundingBoxf3 bbox;
        for (int k = 0; k < 3; ++ k) {
            if (child_center[k] == -1.) {
                bbox.min[k] = current_bbox.min[k];
                bbox.max[k] = current_cube->center[k] + EPSILON;
            } else {
                bbox.min[k] = current_cube->center[k] - EPSILON;
                bbox.max[k] = current_bbox.max[k];
            }
        }
        if (triangle_AABB_intersects(a, b, c, bbox)) {
            if (! current_cube->children[i])
                current_cube->children[i] = this->pool.construct(current_cube->center + (child_center * (this->cubes_properties[depth].edge_length / 4)));
            if (depth > 1)
                this->insert_triangle(a, b, c, current_cube->children[i], bbox, depth - 1);
        }
    }
}

} // namespace Slic3r
