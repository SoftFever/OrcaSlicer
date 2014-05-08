#include "Point.hpp"
#include "Line.hpp"
#include <cmath>
#include <sstream>

namespace Slic3r {

bool
Point::operator==(const Point& rhs) const
{
    return this->coincides_with(rhs);
}

std::string
Point::wkt() const
{
    std::ostringstream ss;
    ss << "POINT(" << this->x << " " << this->y << ")";
    return ss.str();
}

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
Point::rotate(double angle, const Point &center)
{
    double cur_x = (double)this->x;
    double cur_y = (double)this->y;
    this->x = (coord_t)round( (double)center.x + cos(angle) * (cur_x - (double)center.x) - sin(angle) * (cur_y - (double)center.y) );
    this->y = (coord_t)round( (double)center.y + cos(angle) * (cur_y - (double)center.y) + sin(angle) * (cur_x - (double)center.x) );
}

bool
Point::coincides_with(const Point &point) const
{
    return this->x == point.x && this->y == point.y;
}

int
Point::nearest_point_index(const Points &points) const
{
    PointConstPtrs p;
    p.reserve(points.size());
    for (Points::const_iterator it = points.begin(); it != points.end(); ++it)
        p.push_back(&*it);
    return this->nearest_point_index(p);
}

int
Point::nearest_point_index(const PointConstPtrs &points) const
{
    int idx = -1;
    double distance = -1;  // double because long is limited to 2147483647 on some platforms and it's not enough
    
    for (PointConstPtrs::const_iterator it = points.begin(); it != points.end(); ++it) {
        /* If the X distance of the candidate is > than the total distance of the
           best previous candidate, we know we don't want it */
        double d = pow(this->x - (*it)->x, 2);
        if (distance != -1 && d > distance) continue;
        
        /* If the Y distance of the candidate is > than the total distance of the
           best previous candidate, we know we don't want it */
        d += pow(this->y - (*it)->y, 2);
        if (distance != -1 && d > distance) continue;
        
        idx = it - points.begin();
        distance = d;
        
        if (distance < EPSILON) break;
    }
    
    return idx;
}

int
Point::nearest_point_index(const PointPtrs &points) const
{
    PointConstPtrs p;
    p.reserve(points.size());
    for (PointPtrs::const_iterator it = points.begin(); it != points.end(); ++it)
        p.push_back(*it);
    return this->nearest_point_index(p);
}

void
Point::nearest_point(const Points &points, Point* point) const
{
    *point = points.at(this->nearest_point_index(points));
}

double
Point::distance_to(const Point &point) const
{
    double dx = ((double)point.x - this->x);
    double dy = ((double)point.y - this->y);
    return sqrt(dx*dx + dy*dy);
}

double
Point::distance_to(const Line &line) const
{
    if (line.a.coincides_with(line.b)) return this->distance_to(line.a);
    
    double n = (double)(line.b.x - line.a.x) * (double)(line.a.y - this->y)
        - (double)(line.a.x - this->x) * (double)(line.b.y - line.a.y);
    
    return std::abs(n) / line.length();
}

/* Three points are a counter-clockwise turn if ccw > 0, clockwise if
 * ccw < 0, and collinear if ccw = 0 because ccw is a determinant that
 * gives the signed area of the triangle formed by p1, p2 and this point.
 * In other words it is the 2D cross product of p1-p2 and p1-this, i.e.
 * z-component of their 3D cross product.
 * We return double because it must be big enough to hold 2*max(|coordinate|)^2
 */
double
Point::ccw(const Point &p1, const Point &p2) const
{
    return (double)(p2.x - p1.x)*(double)(this->y - p1.y) - (double)(p2.y - p1.y)*(double)(this->x - p1.x);
}

double
Point::ccw(const Line &line) const
{
    return this->ccw(line.a, line.b);
}

#ifdef SLIC3RXS

REGISTER_CLASS(Point, "Point");

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
    // get a double from Perl and round it, otherwise
    // it would get truncated
    this->x = lrint(SvNV(*av_fetch(point_av, 0, 0)));
    this->y = lrint(SvNV(*av_fetch(point_av, 1, 0)));
}

void
Point::from_SV_check(SV* point_sv)
{
    if (sv_isobject(point_sv) && (SvTYPE(SvRV(point_sv)) == SVt_PVMG)) {
        if (!sv_isa(point_sv, perl_class_name(this)) && !sv_isa(point_sv, perl_class_name_ref(this)))
            CONFESS("Not a valid %s object (got %s)", perl_class_name(this), HvNAME(SvSTASH(SvRV(point_sv))));
        *this = *(Point*)SvIV((SV*)SvRV( point_sv ));
    } else {
        this->from_SV(point_sv);
    }
}

#endif

void
Pointf::scale(double factor)
{
    this->x *= factor;
    this->y *= factor;
}

void
Pointf::translate(double x, double y)
{
    this->x += x;
    this->y += y;
}

#ifdef SLIC3RXS

REGISTER_CLASS(Pointf, "Pointf");

SV*
Pointf::to_SV_pureperl() const {
    AV* av = newAV();
    av_fill(av, 1);
    av_store(av, 0, newSVnv(this->x));
    av_store(av, 1, newSVnv(this->y));
    return newRV_noinc((SV*)av);
}

bool
Pointf::from_SV(SV* point_sv)
{
    AV* point_av = (AV*)SvRV(point_sv);
    SV* sv_x = *av_fetch(point_av, 0, 0);
    SV* sv_y = *av_fetch(point_av, 1, 0);
    if (!looks_like_number(sv_x) || !looks_like_number(sv_y)) return false;
    
    this->x = SvNV(sv_x);
    this->y = SvNV(sv_y);
    return true;
}
#endif

void
Pointf3::scale(double factor)
{
    Pointf::scale(factor);
    this->z *= factor;
}

void
Pointf3::translate(double x, double y, double z)
{
    Pointf::translate(x, y);
    this->z += z;
}

#ifdef SLIC3RXS
REGISTER_CLASS(Pointf3, "Pointf3");
#endif

}
