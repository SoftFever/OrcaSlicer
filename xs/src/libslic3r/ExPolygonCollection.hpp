#ifndef slic3r_ExPolygonCollection_hpp_
#define slic3r_ExPolygonCollection_hpp_

#include <myinit.h>
#include "ExPolygon.hpp"
#include "Line.hpp"
#include "Polyline.hpp"

namespace Slic3r {

class ExPolygonCollection;
typedef std::vector<ExPolygonCollection> ExPolygonCollections;

class ExPolygonCollection
{
    public:
    ExPolygons expolygons;
    
    ExPolygonCollection() {};
    ExPolygonCollection(const ExPolygons &expolygons) : expolygons(expolygons) {};
    operator Points() const;
    operator Polygons() const;
    operator ExPolygons&();
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, const Point &center);
    bool contains_point(const Point &point) const;
    bool contains_line(const Line &line) const;
    bool contains_polyline(const Polyline &polyline) const;
    void simplify(double tolerance);
    void convex_hull(Polygon* hull) const;
};

}

#endif
