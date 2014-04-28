#include <myinit.h>
#include "ClipperUtils.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"
#ifdef SLIC3RXS
#include "perlglue.hpp"
#endif

namespace Slic3r {

Polygon::operator Polygons() const
{
    Polygons pp;
    pp.push_back(*this);
    return pp;
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

Polyline*
Polygon::split_at(const Point &point) const
{
    // find index of point
    for (Points::const_iterator it = this->points.begin(); it != this->points.end(); ++it) {
        if (it->coincides_with(point))
            return this->split_at_index(it - this->points.begin());
    }
    CONFESS("Point not found");
    return NULL;
}

Polyline*
Polygon::split_at_index(int index) const
{
    Polyline* poly = new Polyline;
    poly->points.reserve(this->points.size() + 1);
    for (Points::const_iterator it = this->points.begin() + index; it != this->points.end(); ++it)
        poly->points.push_back(*it);
    for (Points::const_iterator it = this->points.begin(); it != this->points.begin() + index + 1; ++it)
        poly->points.push_back(*it);
    return poly;
}

Polyline*
Polygon::split_at_first_point() const
{
    return this->split_at_index(0);
}

Points
Polygon::equally_spaced_points(double distance) const
{
    Polyline* polyline = this->split_at_first_point();
    Points pts = polyline->equally_spaced_points(distance);
    delete polyline;
    return pts;
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
            && (point.x < (j->x - i->x) * (point.y - i->y) / (j->y - i->y) + i->x) )
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

#ifdef SLIC3RXS

REGISTER_CLASS(Polygon, "Polygon");

SV*
Polygon::to_SV_ref() {
    SV* sv = newSV(0);
    sv_setref_pv( sv, perl_class_name_ref(this), (void*)this );
    return sv;
}

SV*
Polygon::to_SV_clone_ref() const {
    SV* sv = newSV(0);
    sv_setref_pv( sv, perl_class_name(this), new Polygon(*this) );
    return sv;
}

void
Polygon::from_SV_check(SV* poly_sv)
{
    if (sv_isobject(poly_sv) && !sv_isa(poly_sv, perl_class_name(this)) && !sv_isa(poly_sv, perl_class_name_ref(this)))
        CONFESS("Not a valid %s object", perl_class_name(this));
    
    MultiPoint::from_SV_check(poly_sv);
}
#endif

}
