#ifndef slic3r_ExPolygon_hpp_
#define slic3r_ExPolygon_hpp_

#include "Polygon.hpp"
#include <vector>

namespace Slic3r {

class ExPolygon
{
    public:
    Polygon contour;
    Polygons holes;
    bool in_collection;
    void from_SV(SV* poly_sv);
    void from_SV_check(SV* poly_sv);
    SV* to_SV();
    SV* to_SV_ref();
    SV* to_SV_pureperl();
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, Point* center);
};

typedef std::vector<ExPolygon> ExPolygons;
typedef std::vector<ExPolygon*> ExPolygonsPtr;

}

#endif
