#include "Point.hpp"

namespace Slic3r {

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
Point::to_SV_pureperl() {
    AV* av = newAV();
    av_fill(av, 1);
    av_store(av, 0, newSViv(this->x));
    av_store(av, 1, newSViv(this->y));
    return newRV_noinc((SV*)av);
}

void
Point::from_SV(SV* point_sv)
{
    AV* point_av = (AV*)SvRV(point_sv);
    this->x = (long)SvIV(*av_fetch(point_av, 0, 0));
    this->y = (long)SvIV(*av_fetch(point_av, 1, 0));
}

void
Point::from_SV_check(SV* point_sv)
{
    if (sv_isobject(point_sv) && (SvTYPE(SvRV(point_sv)) == SVt_PVMG)) {
        *this = *(Point*)SvIV((SV*)SvRV( point_sv ));
    } else {
        this->from_SV(point_sv);
    }
}

}
