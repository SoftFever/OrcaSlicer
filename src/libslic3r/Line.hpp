#ifndef slic3r_Line_hpp_
#define slic3r_Line_hpp_

#include "libslic3r.h"
#include "Point.hpp"

namespace Slic3r {

class BoundingBox;
class Line;
class Line3;
class Linef3;
class Polyline;
class ThickLine;
typedef std::vector<Line> Lines;
typedef std::vector<Line3> Lines3;
typedef std::vector<ThickLine> ThickLines;

Linef3 transform(const Linef3& line, const Transform3d& t);

namespace line_alg {

// Distance to the closest point of line.
template<class L, class T, int N>
double distance_to_squared(const L &line, const Vec<N, T> &point)
{
    const Vec<N, double>  v  = (line.b - line.a).template cast<double>();
    const Vec<N, double>  va = (point  - line.a).template cast<double>();
    const double  l2 = v.squaredNorm();  // avoid a sqrt
    if (l2 == 0.0)
        // a == b case
        return va.squaredNorm();
    // Consider the line extending the segment, parameterized as a + t (b - a).
    // We find projection of this point onto the line.
    // It falls where t = [(this-a) . (b-a)] / |b-a|^2
    const double t = va.dot(v) / l2;
    if (t < 0.0)      return va.squaredNorm();  // beyond the 'a' end of the segment
    else if (t > 1.0) return (point - line.b).template cast<double>().squaredNorm();  // beyond the 'b' end of the segment
    return (t * v - va).squaredNorm();
}

template<class L, class T, int N>
double distance_to(const L &line, const Vec<N, T> &point)
{
    return std::sqrt(distance_to_squared(line, point));
}

} // namespace line_alg

class Line
{
public:
    Line() {}
    Line(const Point& _a, const Point& _b) : a(_a), b(_b) {}
    explicit operator Lines() const { Lines lines; lines.emplace_back(*this); return lines; }
    void   scale(double factor) { this->a *= factor; this->b *= factor; }
    void   translate(double x, double y) { Vector v(x, y); this->a += v; this->b += v; }
    void   rotate(double angle, const Point &center) { this->a.rotate(angle, center); this->b.rotate(angle, center); }
    void   reverse() { std::swap(this->a, this->b); }
    double length() const { return (b - a).cast<double>().norm(); }
    Point  midpoint() const { return (this->a + this->b) / 2; }
    bool   intersection_infinite(const Line &other, Point* point) const;
    bool   operator==(const Line &rhs) const { return this->a == rhs.a && this->b == rhs.b; }
    double distance_to_squared(const Point &point) const { return distance_to_squared(point, this->a, this->b); }
    double distance_to(const Point &point) const { return distance_to(point, this->a, this->b); }
    double perp_distance_to(const Point &point) const;
    bool   parallel_to(double angle) const;
    bool   parallel_to(const Line &line) const { return this->parallel_to(line.direction()); }
    double atan2_() const { return atan2(this->b(1) - this->a(1), this->b(0) - this->a(0)); }
    double orientation() const;
    double direction() const;
    Vector vector() const { return this->b - this->a; }
    Vector normal() const { return Vector((this->b(1) - this->a(1)), -(this->b(0) - this->a(0))); }
    bool   intersection(const Line& line, Point* intersection) const;
    double ccw(const Point& point) const { return point.ccw(*this); }
    // Clip a line with a bounding box. Returns false if the line is completely outside of the bounding box.
	bool   clip_with_bbox(const BoundingBox &bbox);
    // Extend the line from both sides by an offset.
    void   extend(double offset);

    static inline double distance_to_squared(const Point &point, const Point &a, const Point &b) { return line_alg::distance_to_squared(Line{a, b}, Vec<2, coord_t>{point}); }
    static double distance_to(const Point &point, const Point &a, const Point &b) { return sqrt(distance_to_squared(point, a, b)); }

    Point a;
    Point b;
};

class ThickLine : public Line
{
public:
    ThickLine() : a_width(0), b_width(0) {}
    ThickLine(const Point& a, const Point& b) : Line(a, b), a_width(0), b_width(0) {}
    ThickLine(const Point& a, const Point& b, double wa, double wb) : Line(a, b), a_width(wa), b_width(wb) {}

    double a_width, b_width;
};

class Line3
{
public:
    Line3() : a(Vec3crd::Zero()), b(Vec3crd::Zero()) {}
    Line3(const Vec3crd& _a, const Vec3crd& _b) : a(_a), b(_b) {}

    double  length() const { return (this->a - this->b).cast<double>().norm(); }
    Vec3crd vector() const { return this->b - this->a; }

    Vec3crd a;
    Vec3crd b;
};

class Linef
{
public:
    Linef() : a(Vec2d::Zero()), b(Vec2d::Zero()) {}
    Linef(const Vec2d& _a, const Vec2d& _b) : a(_a), b(_b) {}

    Vec2d a;
    Vec2d b;
};

class Linef3
{
public:
    Linef3() : a(Vec3d::Zero()), b(Vec3d::Zero()) {}
    Linef3(const Vec3d& _a, const Vec3d& _b) : a(_a), b(_b) {}

    Vec3d   intersect_plane(double z) const;
    void    scale(double factor) { this->a *= factor; this->b *= factor; }
    Vec3d   vector() const { return this->b - this->a; }
    Vec3d   unit_vector() const { return (length() == 0.0) ? Vec3d::Zero() : vector().normalized(); }
    double  length() const { return vector().norm(); }

    Vec3d a;
    Vec3d b;
};

BoundingBox get_extents(const Lines &lines);

} // namespace Slic3r

// start Boost
#include <boost/polygon/polygon.hpp>
namespace boost { namespace polygon {
    template <>
    struct geometry_concept<Slic3r::Line> { typedef segment_concept type; };

    template <>
    struct segment_traits<Slic3r::Line> {
        typedef coord_t coordinate_type;
        typedef Slic3r::Point point_type;
    
        static inline point_type get(const Slic3r::Line& line, direction_1d dir) {
            return dir.to_int() ? line.b : line.a;
        }
    };
} }
// end Boost

#endif // slic3r_Line_hpp_
