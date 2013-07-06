#include "myinit.h"
#include "Point.hpp"

Point::~Point() {}


SV*
Point::_toPerl() {
    AV* av = newAV();
    av_fill(av, 1);
    av_store_point_xy(av, x, y);
    return (SV*)newRV_noinc((SV*)av);
}
