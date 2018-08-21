#ifndef slic3r_Line_hpp_
#define slic3r_Line_hpp_

#include "libslic3r.h"
#include "Point.hpp"

namespace Slic3r {

class Line;
class Line3;
class Linef3;
class Polyline;
class ThickLine;
typedef std::vector<Line> Lines;
typedef std::vector<Line3> Lines3;
typedef std::vector<ThickLine> ThickLines;

class Line
{
public:
    Line() {}
    explicit Line(Point _a, Point _b): a(_a), b(_b) {}
    explicit operator Lines() const { Lines lines; lines.emplace_back(*this); return lines; }
    void   scale(double factor) { this->a *= factor; this->b *= factor; }
    void   translate(double x, double y) { Vector v(x, y); this->a += v; this->b += v; }
    void   rotate(double angle, const Point &center) { this->a.rotate(angle, center); this->b.rotate(angle, center); }
    void   reverse() { std::swap(this->a, this->b); }
    double length() const { return (b - a).cast<double>().norm(); }
    Point  midpoint() const { return (this->a + this->b) / 2; }
    bool   intersection_infinite(const Line &other, Point* point) const;
    bool   operator==(const Line &rhs) const { return this->a == rhs.a && this->b == rhs.b; }
    double distance_to(const Point &point) const;
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

    Point a;
    Point b;
};

class ThickLine : public Line
{
public:
    ThickLine() : a_width(0), b_width(0) {}
    ThickLine(Point a, Point b) : Line(a, b), a_width(0), b_width(0) {}
    ThickLine(Point a, Point b, double wa, double wb) : Line(a, b), a_width(wa), b_width(wb) {}

    coordf_t a_width, b_width;    
};

class Line3
{
public:
    Line3() {}
    Line3(const Point3& _a, const Point3& _b) : a(_a), b(_b) {}

    double  length() const { return (this->a - this->b).cast<double>().norm(); }
    Vector3 vector() const { return this->b - this->a; }

    Point3 a;
    Point3 b;
};

class Linef
{
public:
    Linef() {}
    explicit Linef(Pointf _a, Pointf _b): a(_a), b(_b) {}

    Pointf a;
    Pointf b;
};

class Linef3
{
public:
    Linef3() {}
    explicit Linef3(Pointf3 _a, Pointf3 _b): a(_a), b(_b) {}
    Pointf3 intersect_plane(double z) const;
    void    scale(double factor) { this->a *= factor; this->b *= factor; }

    Pointf3 a;
    Pointf3 b;
};

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
