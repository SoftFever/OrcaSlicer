#ifndef slic3r_Line_hpp_
#define slic3r_Line_hpp_

#include <myinit.h>
#include "Point.hpp"

namespace Slic3r {

class Line;
class Linef3;
class Polyline;
typedef std::vector<Line> Lines;

class Line
{
    public:
    Point a;
    Point b;
    Line() {};
    explicit Line(Point _a, Point _b): a(_a), b(_b) {};
    std::string wkt() const;
    operator Lines() const;
    operator Polyline() const;
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, const Point &center);
    void reverse();
    double length() const;
    Point midpoint() const;
    void point_at(double distance, Point* point) const;
    Point point_at(double distance) const;
    bool intersection_infinite(const Line &other, Point* point) const;
    bool coincides_with(const Line &line) const;
    double distance_to(const Point &point) const;
    bool parallel_to(double angle) const;
    bool parallel_to(const Line &line) const;
    double atan2_() const;
    double orientation() const;
    double direction() const;
    Vector vector() const;
    Vector normal() const;
    
    #ifdef SLIC3RXS
    void from_SV(SV* line_sv);
    void from_SV_check(SV* line_sv);
    SV* to_AV();
    SV* to_SV_pureperl() const;
    #endif
};

class Linef
{
    public:
    Pointf a;
    Pointf b;
    Linef() {};
    explicit Linef(Pointf _a, Pointf _b): a(_a), b(_b) {};
};

class Linef3
{
    public:
    Pointf3 a;
    Pointf3 b;
    Linef3() {};
    explicit Linef3(Pointf3 _a, Pointf3 _b): a(_a), b(_b) {};
    Pointf3 intersect_plane(double z) const;
    void scale(double factor);
    
    #ifdef SLIC3RXS
    void from_SV(SV* line_sv);
    void from_SV_check(SV* line_sv);
    SV* to_SV_pureperl() const;
    #endif
};

}

// start Boost
#include <boost/polygon/polygon.hpp>
namespace boost { namespace polygon {
    template <>
    struct geometry_concept<Line> { typedef segment_concept type; };

    template <>
    struct segment_traits<Line> {
        typedef coord_t coordinate_type;
        typedef Point point_type;
    
        static inline point_type get(const Line& line, direction_1d dir) {
            return dir.to_int() ? line.b : line.a;
        }
    };
} }
// end Boost

#endif
