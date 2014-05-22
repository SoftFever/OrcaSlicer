#include <myinit.h>
#include "ClipperUtils.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"

namespace Slic3r {

Polygon::operator Polygons() const
{
    Polygons pp;
    pp.push_back(*this);
    return pp;
}

Polygon::operator Polyline() const
{
    Polyline polyline;
    this->split_at_first_point(&polyline);
    return polyline;
}

Point&
Polygon::operator[](Points::size_type idx)
{
    return this->points[idx];
}

const Point&
Polygon::operator[](Points::size_type idx) const
{
    return this->points[idx];
}

Point
Polygon::last_point() const
{
    return this->points.front();  // last point == first point for polygons
}

Lines
Polygon::lines() const
{
    Lines lines;
    this->lines(&lines);
    return lines;
}

void
Polygon::lines(Lines* lines) const
{
    lines->reserve(lines->size() + this->points.size());
    for (Points::const_iterator it = this->points.begin(); it != this->points.end()-1; ++it) {
        lines->push_back(Line(*it, *(it + 1)));
    }
    lines->push_back(Line(this->points.back(), this->points.front()));
}

void
Polygon::split_at_vertex(const Point &point, Polyline* polyline) const
{
    // find index of point
    for (Points::const_iterator it = this->points.begin(); it != this->points.end(); ++it) {
        if (it->coincides_with(point)) {
            this->split_at_index(it - this->points.begin(), polyline);
            return;
        }
    }
    CONFESS("Point not found");
}

void
Polygon::split_at_index(int index, Polyline* polyline) const
{
    polyline->points.reserve(this->points.size() + 1);
    for (Points::const_iterator it = this->points.begin() + index; it != this->points.end(); ++it)
        polyline->points.push_back(*it);
    for (Points::const_iterator it = this->points.begin(); it != this->points.begin() + index + 1; ++it)
        polyline->points.push_back(*it);
}

void
Polygon::split_at_first_point(Polyline* polyline) const
{
    this->split_at_index(0, polyline);
}

void
Polygon::equally_spaced_points(double distance, Points* points) const
{
    Polyline polyline;
    this->split_at_first_point(&polyline);
    polyline.equally_spaced_points(distance, points);
}

double
Polygon::area() const
{
    ClipperLib::Path p;
    Slic3rMultiPoint_to_ClipperPath(*this, p);
    return ClipperLib::Area(p);
}

bool
Polygon::is_counter_clockwise() const
{
    ClipperLib::Path p;
    Slic3rMultiPoint_to_ClipperPath(*this, p);
    return ClipperLib::Orientation(p);
}

bool
Polygon::is_clockwise() const
{
    return !this->is_counter_clockwise();
}

bool
Polygon::make_counter_clockwise()
{
    if (!this->is_counter_clockwise()) {
        this->reverse();
        return true;
    }
    return false;
}

bool
Polygon::make_clockwise()
{
    if (this->is_counter_clockwise()) {
        this->reverse();
        return true;
    }
    return false;
}

bool
Polygon::is_valid() const
{
    return this->points.size() >= 3;
}

bool
Polygon::contains_point(const Point &point) const
{
    // http://www.ecse.rpi.edu/Homepages/wrf/Research/Short_Notes/pnpoly.html
    bool result = false;
    Points::const_iterator i = this->points.begin();
    Points::const_iterator j = this->points.end() - 1;
    for (; i != this->points.end(); j = i++) {
        if ( ((i->y > point.y) != (j->y > point.y))
            && ((double)point.x < (double)(j->x - i->x) * (double)(point.y - i->y) / (double)(j->y - i->y) + (double)i->x) )
            result = !result;
    }
    return result;
}

Polygons
Polygon::simplify(double tolerance) const
{
    Polygon p = *this;
    p.points = MultiPoint::_douglas_peucker(p.points, tolerance);
    
    Polygons pp;
    pp.push_back(p);
    simplify_polygons(pp, pp);
    return pp;
}

void
Polygon::simplify(double tolerance, Polygons &polygons) const
{
    Polygons pp = this->simplify(tolerance);
    polygons.reserve(polygons.size() + pp.size());
    polygons.insert(polygons.end(), pp.begin(), pp.end());
}

// Only call this on convex polygons or it will return invalid results
void
Polygon::triangulate_convex(Polygons* polygons) const
{
    for (Points::const_iterator it = this->points.begin() + 2; it != this->points.end(); ++it) {
        Polygon p;
        p.points.reserve(3);
        p.points.push_back(this->points.front());
        p.points.push_back(*(it-1));
        p.points.push_back(*it);
        
        // this should be replaced with a more efficient call to a merge_collinear_segments() method
        if (p.area() > 0) polygons->push_back(p);
    }
}

// center of mass
Point
Polygon::centroid() const
{
    double area_temp = this->area();
    double x_temp = 0;
    double y_temp = 0;
    
    Polyline polyline;
    this->split_at_first_point(&polyline);
    for (Points::const_iterator point = polyline.points.begin(); point != polyline.points.end() - 1; ++point) {
        x_temp += (double)( point->x + (point+1)->x ) * ( (double)point->x*(point+1)->y - (double)(point+1)->x*point->y );
        y_temp += (double)( point->y + (point+1)->y ) * ( (double)point->x*(point+1)->y - (double)(point+1)->x*point->y );
    }
    
    return Point(x_temp/(6*area_temp), y_temp/(6*area_temp));
}

#ifdef SLIC3RXS
REGISTER_CLASS(Polygon, "Polygon");

void
Polygon::from_SV_check(SV* poly_sv)
{
    if (sv_isobject(poly_sv) && !sv_isa(poly_sv, perl_class_name(this)) && !sv_isa(poly_sv, perl_class_name_ref(this)))
        CONFESS("Not a valid %s object", perl_class_name(this));
    
    MultiPoint::from_SV_check(poly_sv);
}
#endif

}
