#ifndef slic3r_Point_hpp_
#define slic3r_Point_hpp_

#include <myinit.h>
#include <vector>
#include <math.h>

namespace Slic3r {

class Point
{
    public:
    long x;
    long y;
    explicit Point(long _x = 0, long _y = 0): x(_x), y(_y) {};
    void from_SV(SV* point_sv);
    void from_SV_check(SV* point_sv);
    SV* to_SV_pureperl();
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, Point* center);
    bool coincides_with(Point* point);
};

typedef std::vector<Point> Points;

}

#endif
