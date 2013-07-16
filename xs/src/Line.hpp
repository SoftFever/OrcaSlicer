#ifndef slic3r_Line_hpp_
#define slic3r_Line_hpp_

#include <myinit.h>
#include "Point.hpp"

namespace Slic3r {

class Line
{
    public:
    Point a;
    Point b;
    Line() {};
    explicit Line(Point _a, Point _b): a(_a), b(_b) {};
    void from_SV(SV* line_sv);
    void from_SV_check(SV* line_sv);
    SV* to_SV();
    SV* to_SV_ref();
    SV* to_SV_pureperl();
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, Point* center);
    void reverse();
};

typedef std::vector<Line> Lines;

}

#endif
