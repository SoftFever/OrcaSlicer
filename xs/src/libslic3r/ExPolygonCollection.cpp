#include "ExPolygonCollection.hpp"
#include "Geometry.hpp"

namespace Slic3r {

ExPolygonCollection::operator Points() const
{
    Points points;
    Polygons pp = *this;
    for (Polygons::const_iterator poly = pp.begin(); poly != pp.end(); ++poly) {
        for (Points::const_iterator point = poly->points.begin(); point != poly->points.end(); ++point)
            points.push_back(*point);
    }
    return points;
}

ExPolygonCollection::operator Polygons() const
{
    Polygons polygons;
    for (ExPolygons::const_iterator it = this->expolygons.begin(); it != this->expolygons.end(); ++it) {
        polygons.push_back(it->contour);
        for (Polygons::const_iterator ith = it->holes.begin(); ith != it->holes.end(); ++ith) {
            polygons.push_back(*ith);
        }
    }
    return polygons;
}

ExPolygonCollection::operator ExPolygons&()
{
    return this->expolygons;
}

void
ExPolygonCollection::scale(double factor)
{
    for (ExPolygons::iterator it = expolygons.begin(); it != expolygons.end(); ++it) {
        (*it).scale(factor);
    }
}

void
ExPolygonCollection::translate(double x, double y)
{
   for (ExPolygons::iterator it = expolygons.begin(); it != expolygons.end(); ++it) {
        (*it).translate(x, y);
    }
}

void
ExPolygonCollection::rotate(double angle, const Point &center)
{
    for (ExPolygons::iterator it = expolygons.begin(); it != expolygons.end(); ++it) {
        (*it).rotate(angle, center);
    }
}

bool
ExPolygonCollection::contains_point(const Point &point) const
{
    for (ExPolygons::const_iterator it = this->expolygons.begin(); it != this->expolygons.end(); ++it) {
        if (it->contains_point(point)) return true;
    }
    return false;
}

void
ExPolygonCollection::simplify(double tolerance)
{
    ExPolygons expp;
    for (ExPolygons::const_iterator it = this->expolygons.begin(); it != this->expolygons.end(); ++it) {
        it->simplify(tolerance, expp);
    }
    this->expolygons = expp;
}

void
ExPolygonCollection::convex_hull(Polygon* hull) const
{
    Points pp;
    for (ExPolygons::const_iterator it = this->expolygons.begin(); it != this->expolygons.end(); ++it)
        pp.insert(pp.end(), it->contour.points.begin(), it->contour.points.end());
    Slic3r::Geometry::convex_hull(pp, hull);
}

#ifdef SLIC3RXS
REGISTER_CLASS(ExPolygonCollection, "ExPolygon::Collection");
#endif

}
