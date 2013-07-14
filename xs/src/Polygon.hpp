#ifndef slic3r_Polygon_hpp_
#define slic3r_Polygon_hpp_

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

#include "Polyline.hpp"

namespace Slic3r {

class Polygon : public Polyline {};

typedef std::vector<Polygon> Polygons;

void
perl2polygon(SV* poly_sv, Polygon& poly)
{
    AV* poly_av = (AV*)SvRV(poly_sv);
    const unsigned int num_points = av_len(poly_av)+1;
    poly.points.resize(num_points);
    
    for (unsigned int i = 0; i < num_points; i++) {
        SV** point_sv = av_fetch(poly_av, i, 0);
        perl2point(*point_sv, poly.points[i]);
    }
}

SV*
polygon2perl(Polygon& poly) {
    const unsigned int num_points = poly.points.size();
    AV* av = newAV();
    av_extend(av, num_points-1);
    for (unsigned int i = 0; i < num_points; i++) {
        av_store(av, i, point2perl(poly.points[i]));
    }
    return sv_bless(newRV_noinc((SV*)av), gv_stashpv("Slic3r::Polygon", GV_ADD));
}

}

#endif
