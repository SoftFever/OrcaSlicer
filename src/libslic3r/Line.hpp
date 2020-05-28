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

    static double distance_to_squared(const Point &point, const Point &a, const Point &b);
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

extern BoundingBox get_extents(const Lines &lines);

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

#endif
