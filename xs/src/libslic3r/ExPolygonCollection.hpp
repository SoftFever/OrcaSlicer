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
    ExPolygonCollection(const ExPolygon &expolygon);
    ExPolygonCollection(const ExPolygons &expolygons) : expolygons(expolygons) {};
    operator Points() const;
    operator Polygons() const;
    operator ExPolygons&();
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, const Point &center);
    template <class T> bool contains(const T &item) const;
    bool contains_b(const Point &point) const;
    void simplify(double tolerance);
    Polygon convex_hull() const;
    Lines lines() const;
    Polygons contours() const;
};

}

#endif
