#ifndef slic3r_Line_hpp_
#define slic3r_Line_hpp_

#include <myinit.h>
#include "Point.hpp"

namespace Slic3r {

class Line;
class Polyline;

class Line
{
    public:
    Point a;
    Point b;
    Line() {};
    explicit Line(Point _a, Point _b): a(_a), b(_b) {};
    std::string wkt() const;
    operator Polyline() const;
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, const Point &center);
    void reverse();
    double length() const;
    Point* midpoint() const;
    void point_at(double distance, Point* point) const;
    Point point_at(double distance) const;
    bool coincides_with(const Line &line) const;
    double distance_to(const Point &point) const;
    double atan2_() const;
    double direction() const;
    Vector vector() const;
    
    #ifdef SLIC3RXS
    void from_SV(SV* line_sv);
    void from_SV_check(SV* line_sv);
    SV* to_AV();
    SV* to_SV_ref();
    SV* to_SV_clone_ref() const;
    SV* to_SV_pureperl() const;
    #endif
};

typedef std::vector<Line> Lines;

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
