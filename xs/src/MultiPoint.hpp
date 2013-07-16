#ifndef slic3r_MultiPoint_hpp_
#define slic3r_MultiPoint_hpp_

#include "Point.hpp"
#include <algorithm>
#include <vector>

namespace Slic3r {

class MultiPoint
{
    public:
    Points points;
    void from_SV(SV* poly_sv);
    void from_SV_check(SV* poly_sv);
    SV* to_SV();
    SV* to_SV_pureperl();
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, Point* center);
    void reverse();
};

}

#endif
