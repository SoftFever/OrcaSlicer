#include "Point.hpp"
#include <math.h>

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
    this->x = (long)round( (double)center->x + cos(angle) * (cur_x - (double)center->x) - sin(angle) * (cur_y - (double)center->y) );
    this->y = (long)round( (double)center->y + cos(angle) * (cur_y - (double)center->y) + sin(angle) * (cur_x - (double)center->x) );
}

bool
Point::coincides_with(const Point* point) const
{
    return this->x == point->x && this->y == point->y;
}

int
Point::nearest_point_index(const Points points) const
{
    int idx = -1;
    long distance = -1;
    
    for (Points::const_iterator it = points.begin(); it != points.end(); ++it) {
        /* If the X distance of the candidate is > than the total distance of the
           best previous candidate, we know we don't want it */
        long d = pow(this->x - (*it).x, 2);
        if (distance != -1 && d > distance) continue;
        
        /* If the Y distance of the candidate is > than the total distance of the
           best previous candidate, we know we don't want it */
        d += pow(this->y - (*it).y, 2);
        if (distance != -1 && d > distance) continue;
        
        idx = it - points.begin();
        distance = d;
        
        if (distance < EPSILON) break;
    }
    
    return idx;
}

Point*
Point::nearest_point(Points points) const
{
    return &(points.at(this->nearest_point_index(points)));
}

double
Point::distance_to(const Point* point) const
{
    double dx = ((double)point->x - this->x);
    double dy = ((double)point->y - this->y);
    return sqrt(dx*dx + dy*dy);
}

#ifdef SLIC3RXS
SV*
Point::to_SV_ref() {
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Point::Ref", (void*)this );
    return sv;
}

SV*
Point::to_SV_clone_ref() const {
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Point", new Point(*this) );
    return sv;
}

SV*
Point::to_SV_pureperl() const {
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
#endif

}
