#ifndef slic3r_ExPolygon_hpp_
#define slic3r_ExPolygon_hpp_

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

#include "Polygon.hpp"
#include <vector>

namespace Slic3r {

class ExPolygon
{
    public:
    Polygon contour;
    Polygons holes;
    bool in_collection;
    SV* to_SV(bool pureperl = false, bool pureperl_children = false);
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, Point* center);
};

typedef std::vector<ExPolygon> ExPolygons;

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
ExPolygon::to_SV(bool pureperl, bool pureperl_children)
{
    if (pureperl) {
        const unsigned int num_holes = this->holes.size();
        AV* av = newAV();
        av_extend(av, num_holes);  // -1 +1
        av_store(av, 0, this->contour.to_SV(pureperl_children, pureperl_children));
        for (unsigned int i = 0; i < num_holes; i++) {
            av_store(av, i+1, this->holes[i].to_SV(pureperl_children, pureperl_children));
        }
        return sv_bless(newRV_noinc((SV*)av), gv_stashpv("Slic3r::ExPolygon", GV_ADD));
    } else {
        SV* sv = newSV(0);
        sv_setref_pv( sv, "Slic3r::ExPolygon::XS", this );
        return sv;
    }
}

void
perl2expolygon(SV* expoly_sv, ExPolygon& expoly)
{
    AV* expoly_av = (AV*)SvRV(expoly_sv);
    const unsigned int num_polygons = av_len(expoly_av)+1;
    expoly.holes.resize(num_polygons-1);
    
    SV** polygon_sv = av_fetch(expoly_av, 0, 0);
    expoly.contour.from_SV(*polygon_sv);
    for (unsigned int i = 0; i < num_polygons-1; i++) {
        polygon_sv = av_fetch(expoly_av, i+1, 0);
        expoly.holes[i].from_SV(*polygon_sv);
    }
}

void
perl2expolygon_check(SV* expoly_sv, ExPolygon& expoly)
{
    if (sv_isobject(expoly_sv) && (SvTYPE(SvRV(expoly_sv)) == SVt_PVMG)) {
        // a XS ExPolygon was supplied
        expoly = *(ExPolygon *)SvIV((SV*)SvRV( expoly_sv ));
    } else {
        // a Perl arrayref was supplied
        perl2expolygon(expoly_sv, expoly);
    }
}

}

#endif
