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

SV*
ExPolygon::to_SV() {
    const unsigned int num_holes = this->holes.size();
    AV* av = newAV();
    av_extend(av, num_holes);  // -1 +1
    
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Polygon", new Polygon(this->contour) );
    av_store(av, 0, sv);
    
    for (unsigned int i = 0; i < num_holes; i++) {
        sv = newSV(0);
        sv_setref_pv( sv, "Slic3r::Polygon", new Polygon(this->holes[i]) );
        av_store(av, i+1, sv);
    }
    return newRV_noinc((SV*)av);
}

SV*
ExPolygon::to_SV_ref() {
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::ExPolygon", new ExPolygon(*this) );
    return sv;
}

SV*
ExPolygon::to_SV_pureperl()
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

}
