#include "ExPolygon.hpp"
#include "Polygon.hpp"

namespace Slic3r {

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

#ifdef SLIC3RXS
SV*
ExPolygon::to_AV() {
    const unsigned int num_holes = this->holes.size();
    AV* av = newAV();
    av_extend(av, num_holes);  // -1 +1
    
    SV* sv = newSV(0);
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
        // a XS ExPolygon was supplied
        *this = *(ExPolygon *)SvIV((SV*)SvRV( expoly_sv ));
    } else {
        // a Perl arrayref was supplied
        this->from_SV(expoly_sv);
    }
}
#endif

}
