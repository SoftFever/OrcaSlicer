#ifndef slic3r_Point_hpp_
#define slic3r_Point_hpp_

extern "C" {
#include "EXTERN.h"
#include "perl.h"
#include "XSUB.h"
#include "ppport.h"
}

namespace Slic3r {

class Point
{
    public:
    unsigned long x;
    unsigned long y;
    Point(unsigned long _x = 0, unsigned long _y = 0): x(_x), y(_y) {};
};

SV*
point2perl(Point& point) {
    AV* av = newAV();
    av_fill(av, 1);
    av_store_point_xy(av, point.x, point.y);
    return (SV*)newRV_noinc((SV*)av);
}

}

#endif
