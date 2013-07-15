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
    void scale(double factor);
    void translate(double x, double y);
    void rotate(double angle, Point* center);
    bool coincides_with(Point* point);
};

typedef std::vector<Point> Points;

void
Point::scale(double factor)
{
    this->x *= factor;
    this->y *= factor;
}

void
Point::translate(double x, double y)
{
    this->x += x;
    this->y += y;
}

void
Point::rotate(double angle, Point* center)
{
    double cur_x = (double)this->x;
    double cur_y = (double)this->y;
    this->x = (long)( (double)center->x + cos(angle) * (cur_x - (double)center->x) - sin(angle) * (cur_y - (double)center->y) );
    this->y = (long)( (double)center->y + cos(angle) * (cur_y - (double)center->y) + sin(angle) * (cur_x - (double)center->x) );
}

bool
Point::coincides_with(Point* point)
{
    return this->x == point->x && this->y == point->y;
}

SV*
point2perl(Point& point) {
    AV* av = newAV();
    av_fill(av, 1);
    av_store_point_xy(av, point.x, point.y);
    return sv_bless(newRV_noinc((SV*)av), gv_stashpv("Slic3r::Point", GV_ADD));
}

void
perl2point(SV* point_sv, Point& point)
{
    AV*  point_av = (AV*)SvRV(point_sv);
    point.x = (unsigned long)SvIV(*av_fetch(point_av, 0, 0));
    point.y = (unsigned long)SvIV(*av_fetch(point_av, 1, 0));
}

void
perl2point_check(SV* point_sv, Point& point)
{
    if (sv_isobject(point_sv) && (SvTYPE(SvRV(point_sv)) == SVt_PVMG)) {
        point = *(Point*)SvIV((SV*)SvRV( point_sv ));
    } else {
        perl2point(point_sv, point);
    }
}

}

#endif
