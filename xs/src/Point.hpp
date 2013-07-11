#ifndef slic3r_Point_hpp_
#define slic3r_Point_hpp_

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

#include <math.h>

class Point
{
    public:
    unsigned long x;
    unsigned long y;
    Point(unsigned long _x = 0, unsigned long _y = 0): x(_x), y(_y) {};
    void rotate(double angle, Point* center);
};

void
Point::rotate(double angle, Point* center)
{
    double cur_x = (double)x;
    double cur_y = (double)y;
    x = (unsigned long)( (double)center->x + cos(angle) * (cur_x - (double)center->x) - sin(angle) * (cur_y - (double)center->y) );
    y = (unsigned long)( (double)center->y + cos(angle) * (cur_y - (double)center->y) + sin(angle) * (cur_x - (double)center->x) );
}

SV*
point2perl(Point& point) {
    AV* av = newAV();
    av_fill(av, 1);
    av_store_point_xy(av, point.x, point.y);
    return sv_bless(newRV_noinc((SV*)av), gv_stashpv("Slic3r::Point", GV_ADD));
}

#endif
