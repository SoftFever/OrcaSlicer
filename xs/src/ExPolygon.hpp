#ifndef slic3r_ExPolygon_hpp_
#define slic3r_ExPolygon_hpp_

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

#include "Polygon.hpp"

namespace Slic3r {

class ExPolygon
{
    public:
    Polygon contour;
    Polygons holes;
    SV* arrayref();
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

void
perl2expolygon(SV* expoly_sv, ExPolygon& expoly)
{
    AV* expoly_av = (AV*)SvRV(expoly_sv);
    const unsigned int num_polygons = av_len(expoly_av)+1;
    expoly.holes.resize(num_polygons-1);
    
    SV** polygon_sv = av_fetch(expoly_av, 0, 0);
    perl2polygon(*polygon_sv, expoly.contour);
    for (unsigned int i = 0; i < num_polygons-1; i++) {
        polygon_sv = av_fetch(expoly_av, i+1, 0);
        perl2polygon(*polygon_sv, expoly.holes[i]);
    }
}

SV*
expolygon2perl(ExPolygon& expoly) {
    const unsigned int num_holes = expoly.holes.size();
    AV* av = newAV();
    av_extend(av, num_holes);  // -1 +1
    av_store(av, 0, polygon2perl(expoly.contour));
    for (unsigned int i = 0; i < num_holes; i++) {
        av_store(av, i+1, polygon2perl(expoly.holes[i]));
    }
    return sv_bless(newRV_noinc((SV*)av), gv_stashpv("Slic3r::ExPolygon", GV_ADD));
}

}

#endif
