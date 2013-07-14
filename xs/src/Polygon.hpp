#ifndef slic3r_Polygon_hpp_
#define slic3r_Polygon_hpp_

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

#include "Point.hpp"

namespace Slic3r {

class Polygon
{
    public:
    Points points;
    void scale(double factor);
    void translate(double x, double y);
    void _rotate(double angle, Point* center);
};

typedef std::vector<Polygon> Polygons;

void
Polygon::scale(double factor)
{
    for (Points::iterator it = points.begin(); it != points.end(); ++it) {
        (*it).x *= factor;
        (*it).y *= factor;
    }
}

void
Polygon::translate(double x, double y)
{
    for (Points::iterator it = points.begin(); it != points.end(); ++it) {
        (*it).x += x;
        (*it).y += y;
    }
}

void
Polygon::_rotate(double angle, Point* center)
{
    for (Points::iterator it = points.begin(); it != points.end(); ++it) {
        (*it).rotate(angle, center);
    }
}

void
perl2polygon(SV* poly_sv, Polygon& poly)
{
    AV* poly_av = (AV*)SvRV(poly_sv);
    const unsigned int num_points = av_len(poly_av)+1;
    poly.points.resize(num_points);
    
    for (unsigned int i = 0; i < num_points; i++) {
        SV** point_sv = av_fetch(poly_av, i, 0);
        AV*  point_av = (AV*)SvRV(*point_sv);
        Point& p = poly.points[i];
        p.x = (unsigned long)SvIV(*av_fetch(point_av, 0, 0));
        p.y = (unsigned long)SvIV(*av_fetch(point_av, 1, 0));
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
