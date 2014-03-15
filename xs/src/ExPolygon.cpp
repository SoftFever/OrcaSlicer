#include "ExPolygon.hpp"
#include "Geometry.hpp"
#include "Polygon.hpp"
#include "Line.hpp"
#include "ClipperUtils.hpp"

namespace Slic3r {

ExPolygon::operator Points() const
{
    Points points;
    Polygons pp = *this;
    for (Polygons::const_iterator poly = pp.begin(); poly != pp.end(); ++poly) {
        for (Points::const_iterator point = poly->points.begin(); point != poly->points.end(); ++point)
            points.push_back(*point);
    }
    return points;
}

ExPolygon::operator Polygons() const
{
    Polygons polygons;
    polygons.reserve(this->holes.size() + 1);
    polygons.push_back(this->contour);
    for (Polygons::const_iterator it = this->holes.begin(); it != this->holes.end(); ++it) {
        polygons.push_back(*it);
    }
    return polygons;
}

void
ExPolygon::scale(double factor)
{
    contour.scale(factor);
    for (Polygons::iterator it = holes.begin(); it != holes.end(); ++it) {
        (*it).scale(factor);
    }
}

void
ExPolygon::translate(double x, double y)
{
    contour.translate(x, y);
    for (Polygons::iterator it = holes.begin(); it != holes.end(); ++it) {
        (*it).translate(x, y);
    }
}

void
ExPolygon::rotate(double angle, Point* center)
{
    contour.rotate(angle, center);
    for (Polygons::iterator it = holes.begin(); it != holes.end(); ++it) {
        (*it).rotate(angle, center);
    }
}

double
ExPolygon::area() const
{
    double a = this->contour.area();
    for (Polygons::const_iterator it = this->holes.begin(); it != this->holes.end(); ++it) {
        a -= -(*it).area();  // holes have negative area
    }
    return a;
}

bool
ExPolygon::is_valid() const
{
    if (!this->contour.is_valid() || !this->contour.is_counter_clockwise()) return false;
    for (Polygons::const_iterator it = this->holes.begin(); it != this->holes.end(); ++it) {
        if (!(*it).is_valid() || (*it).is_counter_clockwise()) return false;
    }
    return true;
}

bool
ExPolygon::contains_line(const Line* line) const
{
    Polylines pl;
    pl.push_back(*line);
    
    Polylines pl_out;
    diff(pl, *this, pl_out);
    return pl_out.empty();
}

bool
ExPolygon::contains_point(const Point* point) const
{
    if (!this->contour.contains_point(point)) return false;
    for (Polygons::const_iterator it = this->holes.begin(); it != this->holes.end(); ++it) {
        if (it->contains_point(point)) return false;
    }
    return true;
}

Polygons
ExPolygon::simplify_p(double tolerance) const
{
    Polygons pp;
    pp.reserve(this->holes.size() + 1);
    
    // contour
    Polygon p = this->contour;
    p.points = MultiPoint::_douglas_peucker(p.points, tolerance);
    pp.push_back(p);
    
    // holes
    for (Polygons::const_iterator it = this->holes.begin(); it != this->holes.end(); ++it) {
        p = *it;
        p.points = MultiPoint::_douglas_peucker(p.points, tolerance);
        pp.push_back(p);
    }
    simplify_polygons(pp, pp);
    return pp;
}

ExPolygons
ExPolygon::simplify(double tolerance) const
{
    Polygons pp = this->simplify_p(tolerance);
    ExPolygons expp;
    union_(pp, expp);
    return expp;
}

void
ExPolygon::simplify(double tolerance, ExPolygons &expolygons) const
{
    ExPolygons ep = this->simplify(tolerance);
    expolygons.reserve(expolygons.size() + ep.size());
    expolygons.insert(expolygons.end(), ep.begin(), ep.end());
}

void
ExPolygon::medial_axis(double max_width, double min_width, Polylines* polylines) const
{
    // init helper object
    Slic3r::Geometry::MedialAxis ma(max_width, min_width);
    
    // populate list of segments for the Voronoi diagram
    this->contour.lines(&ma.lines);
    for (Polygons::const_iterator hole = this->holes.begin(); hole != this->holes.end(); ++hole)
        hole->lines(&ma.lines);
    
    // compute the Voronoi diagram
    ma.build(polylines);
    
    // extend initial and final segments of each polyline (they will be clipped)
    for (Polylines::iterator polyline = polylines->begin(); polyline != polylines->end(); ++polyline) {
        polyline->extend_start(max_width);
        polyline->extend_end(max_width);
    }
    
    // clip segments to our expolygon area
    intersection(*polylines, *this, *polylines);
}

#ifdef SLIC3RXS
SV*
ExPolygon::to_AV() {
    const unsigned int num_holes = this->holes.size();
    AV* av = newAV();
    av_extend(av, num_holes);  // -1 +1
    
    av_store(av, 0, this->contour.to_SV_ref());
    
    for (unsigned int i = 0; i < num_holes; i++) {
        av_store(av, i+1, this->holes[i].to_SV_ref());
    }
    return newRV_noinc((SV*)av);
}

SV*
ExPolygon::to_SV_ref() {
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::ExPolygon::Ref", this );
    return sv;
}

SV*
ExPolygon::to_SV_clone_ref() const {
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::ExPolygon", new ExPolygon(*this) );
    return sv;
}

SV*
ExPolygon::to_SV_pureperl() const
{
    const unsigned int num_holes = this->holes.size();
    AV* av = newAV();
    av_extend(av, num_holes);  // -1 +1
    av_store(av, 0, this->contour.to_SV_pureperl());
    for (unsigned int i = 0; i < num_holes; i++) {
        av_store(av, i+1, this->holes[i].to_SV_pureperl());
    }
    return newRV_noinc((SV*)av);
}

void
ExPolygon::from_SV(SV* expoly_sv)
{
    AV* expoly_av = (AV*)SvRV(expoly_sv);
    const unsigned int num_polygons = av_len(expoly_av)+1;
    this->holes.resize(num_polygons-1);
    
    SV** polygon_sv = av_fetch(expoly_av, 0, 0);
    this->contour.from_SV(*polygon_sv);
    for (unsigned int i = 0; i < num_polygons-1; i++) {
        polygon_sv = av_fetch(expoly_av, i+1, 0);
        this->holes[i].from_SV(*polygon_sv);
    }
}

void
ExPolygon::from_SV_check(SV* expoly_sv)
{
    if (sv_isobject(expoly_sv) && (SvTYPE(SvRV(expoly_sv)) == SVt_PVMG)) {
        if (!sv_isa(expoly_sv, "Slic3r::ExPolygon") && !sv_isa(expoly_sv, "Slic3r::ExPolygon::Ref"))
            CONFESS("Not a valid Slic3r::ExPolygon object");
        // a XS ExPolygon was supplied
        *this = *(ExPolygon *)SvIV((SV*)SvRV( expoly_sv ));
    } else {
        // a Perl arrayref was supplied
        this->from_SV(expoly_sv);
    }
}
#endif

}
