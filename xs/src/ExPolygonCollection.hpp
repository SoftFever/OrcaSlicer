#ifndef slic3r_ExPolygonCollection_hpp_
#define slic3r_ExPolygonCollection_hpp_

#include <myinit.h>
#include "ExPolygon.hpp"

namespace Slic3r {

class ExPolygonCollection
{
    public:
    ExPolygons expolygons;
    operator Polygons() const;
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, const Point &center);
    bool contains_point(const Point &point) const;
    void simplify(double tolerance);
    void convex_hull(Polygon* hull) const;
};

}

#endif
