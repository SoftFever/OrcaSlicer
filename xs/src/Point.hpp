#ifndef slic3r_Point_hpp_
#define slic3r_Point_hpp_

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

#include <math.h>

namespace Slic3r {

class Point
{
    public:
    long x;
    long y;
    Point(long _x = 0, long _y = 0): x(_x), y(_y) {};
    void rotate(double angle, Point* center);
};

void
Point::rotate(double angle, Point* center)
{
    double cur_x = (double)x;
    double cur_y = (double)y;
    x = (long)( (double)center->x + cos(angle) * (cur_x - (double)center->x) - sin(angle) * (cur_y - (double)center->y) );
    y = (long)( (double)center->y + cos(angle) * (cur_y - (double)center->y) + sin(angle) * (cur_x - (double)center->x) );
}

SV*
point2perl(Point& point) {
    AV* av = newAV();
    av_fill(av, 1);
    av_store_point_xy(av, point.x, point.y);
    return sv_bless(newRV_noinc((SV*)av), gv_stashpv("Slic3r::Point", GV_ADD));
}

}

#endif
