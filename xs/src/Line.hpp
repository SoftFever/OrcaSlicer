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
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, Point* center);
    void reverse();
    double length() const;
    Point* midpoint() const;
    
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

#endif
