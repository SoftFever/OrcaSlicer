#ifndef slic3r_Geometry_hpp_
#define slic3r_Geometry_hpp_

#include "libslic3r.h"
#include "BoundingBox.hpp"
#include "ExPolygon.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"

#include "boost/polygon/voronoi.hpp"
using boost::polygon::voronoi_builder;
using boost::polygon::voronoi_diagram;

namespace Slic3r { namespace Geometry {

// Generic result of an orientation predicate.
enum Orientation
{
    ORIENTATION_CCW = 1,
    ORIENTATION_CW = -1,
    ORIENTATION_COLINEAR = 0
};

// Return orientation of the three points (clockwise, counter-clockwise, colinear)
// The predicate is exact for the coord_t type, using 64bit signed integers for the temporaries.
// which means, the coord_t types must not have some of the topmost bits utilized.
// As the points are limited to 30 bits + signum,
// the temporaries u, v, w are limited to 61 bits + signum,
// and d is limited to 63 bits + signum and we are good.
static inline Orientation orient(const Point &a, const Point &b, const Point &c)
{
    // BOOST_STATIC_ASSERT(sizeof(coord_t) * 2 == sizeof(int64_t));
    int64_t u = int64_t(b(0)) * int64_t(c(1)) - int64_t(b(1)) * int64_t(c(0));
    int64_t v = int64_t(a(0)) * int64_t(c(1)) - int64_t(a(1)) * int64_t(c(0));
    int64_t w = int64_t(a(0)) * int64_t(b(1)) - int64_t(a(1)) * int64_t(b(0));
    int64_t d = u - v + w;
    return (d > 0) ? ORIENTATION_CCW : ((d == 0) ? ORIENTATION_COLINEAR : ORIENTATION_CW);
}

// Return orientation of the polygon by checking orientation of the left bottom corner of the polygon
// using exact arithmetics. The input polygon must not contain duplicate points
// (or at least the left bottom corner point must not have duplicates).
static inline bool is_ccw(const Polygon &poly)
{
    // The polygon shall be at least a triangle.
    assert(poly.points.size() >= 3);
    if (poly.points.size() < 3)
        return true;

    // 1) Find the lowest lexicographical point.
    unsigned int imin = 0;
    for (unsigned int i = 1; i < poly.points.size(); ++ i) {
        const Point &pmin = poly.points[imin];
        const Point &p    = poly.points[i];
        if (p(0) < pmin(0) || (p(0) == pmin(0) && p(1) < pmin(1)))
            imin = i;
    }

    // 2) Detect the orientation of the corner imin.
    size_t iPrev = ((imin == 0) ? poly.points.size() : imin) - 1;
    size_t iNext = ((imin + 1 == poly.points.size()) ? 0 : imin + 1);
    Orientation o = orient(poly.points[iPrev], poly.points[imin], poly.points[iNext]);
    // The lowest bottom point must not be collinear if the polygon does not contain duplicate points
    // or overlapping segments.
    assert(o != ORIENTATION_COLINEAR);
    return o == ORIENTATION_CCW;
}

inline bool ray_ray_intersection(const Vec2d &p1, const Vec2d &v1, const Vec2d &p2, const Vec2d &v2, Vec2d &res)
{
    double denom = v1(0) * v2(1) - v2(0) * v1(1);
    if (std::abs(denom) < EPSILON)
        return false;
    double t = (v2(0) * (p1(1) - p2(1)) - v2(1) * (p1(0) - p2(0))) / denom;
    res(0) = p1(0) + t * v1(0);
    res(1) = p1(1) + t * v1(1);
    return true;
}

inline bool segment_segment_intersection(const Vec2d &p1, const Vec2d &v1, const Vec2d &p2, const Vec2d &v2, Vec2d &res)
{
    double denom = v1(0) * v2(1) - v2(0) * v1(1);
    if (std::abs(denom) < EPSILON)
        // Lines are collinear.
        return false;
    double s12_x = p1(0) - p2(0);
    double s12_y = p1(1) - p2(1);
    double s_numer = v1(0) * s12_y - v1(1) * s12_x;
    bool   denom_is_positive = false;
    if (denom < 0.) {
        denom_is_positive = true;
        denom   = - denom;
        s_numer = - s_numer;
    }
    if (s_numer < 0.)
        // Intersection outside of the 1st segment.
        return false;
    double t_numer = v2(0) * s12_y - v2(1) * s12_x;
    if (! denom_is_positive)
        t_numer = - t_numer;
    if (t_numer < 0. || s_numer > denom || t_numer > denom)
        // Intersection outside of the 1st or 2nd segment.
        return false;
    // Intersection inside both of the segments.
    double t = t_numer / denom;
    res(0) = p1(0) + t * v1(0);
    res(1) = p1(1) + t * v1(1);
    return true;
}

Pointf3s convex_hull(Pointf3s points);
Polygon convex_hull(Points points);
Polygon convex_hull(const Polygons &polygons);

void chained_path(const Points &points, std::vector<Points::size_type> &retval, Point start_near);
void chained_path(const Points &points, std::vector<Points::size_type> &retval);
template<class T> void chained_path_items(Points &points, T &items, T &retval);
bool directions_parallel(double angle1, double angle2, double max_diff = 0);
template<class T> bool contains(const std::vector<T> &vector, const Point &point);
double rad2deg(double angle);
double rad2deg_dir(double angle);
template<typename T> T deg2rad(T angle) { return T(PI) * angle / T(180.0); }
void simplify_polygons(const Polygons &polygons, double tolerance, Polygons* retval);

double linint(double value, double oldmin, double oldmax, double newmin, double newmax);
bool arrange(
    // input
    size_t num_parts, const Vec2d &part_size, coordf_t gap, const BoundingBoxf* bed_bounding_box, 
    // output
    Pointfs &positions);

class MedialAxis {
    public:
    Lines lines;
    const ExPolygon* expolygon;
    double max_width;
    double min_width;
    MedialAxis(double _max_width, double _min_width, const ExPolygon* _expolygon = NULL)
        : expolygon(_expolygon), max_width(_max_width), min_width(_min_width) {};
    void build(ThickPolylines* polylines);
    void build(Polylines* polylines);
    
    private:
    class VD : public voronoi_diagram<double> {
    public:
        typedef double                                          coord_type;
        typedef boost::polygon::point_data<coordinate_type>     point_type;
        typedef boost::polygon::segment_data<coordinate_type>   segment_type;
        typedef boost::polygon::rectangle_data<coordinate_type> rect_type;
    };
    VD vd;
    std::set<const VD::edge_type*> edges, valid_edges;
    std::map<const VD::edge_type*, std::pair<coordf_t,coordf_t> > thickness;
    void process_edge_neighbors(const VD::edge_type* edge, ThickPolyline* polyline);
    bool validate_edge(const VD::edge_type* edge);
    const Line& retrieve_segment(const VD::cell_type* cell) const;
    const Point& retrieve_endpoint(const VD::cell_type* cell) const;
};

// Sets the given transform by assembling the given transformations in the following order:
#if ENABLE_MIRROR
// 1) mirror
// 2) scale
// 3) rotate X
// 4) rotate Y
// 5) rotate Z
// 6) translate
void assemble_transform(Transform3d& transform, const Vec3d& translation = Vec3d::Zero(), const Vec3d& rotation = Vec3d::Zero(), const Vec3d& scale = Vec3d::Ones(), const Vec3d& mirror = Vec3d::Ones());
#else
// 1) scale
// 2) rotate X
// 3) rotate Y
// 4) rotate Z
// 5) translate
void assemble_transform(Transform3d& transform, const Vec3d& translation = Vec3d::Zero(), const Vec3d& rotation = Vec3d::Zero(), const Vec3d& scale = Vec3d::Ones());
#endif // ENABLE_MIRROR

// Returns the transform obtained by assembling the given transformations in the following order:
#if ENABLE_MIRROR
// 1) mirror
// 2) scale
// 3) rotate X
// 4) rotate Y
// 5) rotate Z
// 6) translate
Transform3d assemble_transform(const Vec3d& translation = Vec3d::Zero(), const Vec3d& rotation = Vec3d::Zero(), const Vec3d& scale = Vec3d::Ones(), const Vec3d& mirror = Vec3d::Ones());
#else
// 1) scale
// 2) rotate X
// 3) rotate Y
// 4) rotate Z
// 5) translate
Transform3d assemble_transform(const Vec3d& translation = Vec3d::Zero(), const Vec3d& rotation = Vec3d::Zero(), const Vec3d& scale = Vec3d::Ones());
#endif // ENABLE_MIRROR

// Returns the euler angles extracted from the given rotation matrix
// Warning -> The matrix should not contain any scale or shear !!!
Vec3d extract_euler_angles(const Eigen::Matrix<double, 3, 3, Eigen::DontAlign>& rotation_matrix);

// Returns the euler angles extracted from the given affine transform
// Warning -> The transform should not contain any shear !!!
Vec3d extract_euler_angles(const Transform3d& transform);
} }

#endif
