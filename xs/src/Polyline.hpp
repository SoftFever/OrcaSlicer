#ifndef slic3r_Polyline_hpp_
#define slic3r_Polyline_hpp_

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

#include "Point.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace Slic3r {

class Polyline
{
    public:
    Points points;
    void from_SV(SV* poly_sv);
    void from_SV_check(SV* poly_sv);
    SV* to_SV(bool pureperl = false, bool pureperl_children = false);
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, Point* center);
    void reverse();
    protected:
    virtual char* perl_class() {
        return (char*)"Slic3r::Polyline";
    }
};

typedef std::vector<Polyline> Polylines;

void
Polyline::scale(double factor)
{
    for (Points::iterator it = points.begin(); it != points.end(); ++it) {
        (*it).scale(factor);
    }
}

void
Polyline::translate(double x, double y)
{
    for (Points::iterator it = points.begin(); it != points.end(); ++it) {
        (*it).translate(x, y);
    }
}

void
Polyline::rotate(double angle, Point* center)
{
    for (Points::iterator it = points.begin(); it != points.end(); ++it) {
        (*it).rotate(angle, center);
    }
}

void
Polyline::reverse()
{
    std::reverse(this->points.begin(), this->points.end());
}

void
Polyline::from_SV(SV* poly_sv)
{
    AV* poly_av = (AV*)SvRV(poly_sv);
    const unsigned int num_points = av_len(poly_av)+1;
    this->points.resize(num_points);
    
    for (unsigned int i = 0; i < num_points; i++) {
        SV** point_sv = av_fetch(poly_av, i, 0);
        perl2point_check(*point_sv, this->points[i]);
    }
}

void
Polyline::from_SV_check(SV* poly_sv)
{
    if (sv_isobject(poly_sv) && (SvTYPE(SvRV(poly_sv)) == SVt_PVMG)) {
        *this = *(Polyline*)SvIV((SV*)SvRV( poly_sv ));
    } else {
        this->from_SV(poly_sv);
    }
}

SV*
Polyline::to_SV(bool pureperl, bool pureperl_children) {
    const unsigned int num_points = this->points.size();
    AV* av = newAV();
    av_extend(av, num_points-1);
    for (unsigned int i = 0; i < num_points; i++) {
        av_store(av, i, this->points[i].to_SV(pureperl_children));
    }
    return sv_bless(newRV_noinc((SV*)av), gv_stashpv(this->perl_class(), GV_ADD));
}

}

#endif
