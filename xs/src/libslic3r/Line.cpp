#include "Geometry.hpp"
#include "Line.hpp"
#include "Polyline.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace Slic3r {

std::string
Line::wkt() const
{
    std::ostringstream ss;
    ss << "LINESTRING(" << this->a.x << " " << this->a.y << ","
        << this->b.x << " " << this->b.y << ")";
    return ss.str();
}

Line::operator Lines() const
{
    Lines lines;
    lines.push_back(*this);
    return lines;
}

Line::operator Polyline() const
{
    Polyline pl;
    pl.points.push_back(this->a);
    pl.points.push_back(this->b);
    return pl;
}

void
Line::scale(double factor)
{
    this->a.scale(factor);
    this->b.scale(factor);
}

void
Line::translate(double x, double y)
{
    this->a.translate(x, y);
    this->b.translate(x, y);
}

void
Line::rotate(double angle, const Point &center)
{
    this->a.rotate(angle, center);
    this->b.rotate(angle, center);
}

void
Line::reverse()
{
    std::swap(this->a, this->b);
}

double
Line::length() const
{
    return this->a.distance_to(this->b);
}

Point
Line::midpoint() const
{
    return Point((this->a.x + this->b.x) / 2.0, (this->a.y + this->b.y) / 2.0);
}

void
Line::point_at(double distance, Point* point) const
{
    double len = this->length();
    *point = this->a;
    if (this->a.x != this->b.x)
        point->x = this->a.x + (this->b.x - this->a.x) * distance / len;
    if (this->a.y != this->b.y)
        point->y = this->a.y + (this->b.y - this->a.y) * distance / len;
}

Point
Line::point_at(double distance) const
{
    Point p;
    this->point_at(distance, &p);
    return p;
}

bool
Line::intersection_infinite(const Line &other, Point* point) const
{
    Vector x = this->a.vector_to(other.a);
    Vector d1 = this->vector();
    Vector d2 = other.vector();

    double cross = d1.x * d2.y - d1.y * d2.x;
    if (std::fabs(cross) < EPSILON)
        return false;

    double t1 = (x.x * d2.y - x.y * d2.x)/cross;
    point->x = this->a.x + d1.x * t1;
    point->y = this->a.y + d1.y * t1;
    return true;
}

bool
Line::coincides_with(const Line &line) const
{
    return this->a.coincides_with(line.a) && this->b.coincides_with(line.b);
}

double
Line::distance_to(const Point &point) const
{
    return point.distance_to(*this);
}

double
Line::atan2_() const
{
    return atan2(this->b.y - this->a.y, this->b.x - this->a.x);
}

double
Line::orientation() const
{
    double angle = this->atan2_();
    if (angle < 0) angle = 2*PI + angle;
    return angle;
}

double
Line::direction() const
{
    double atan2 = this->atan2_();
    return (fabs(atan2 - PI) < EPSILON) ? 0
        : (atan2 < 0) ? (atan2 + PI)
        : atan2;
}

bool
Line::parallel_to(double angle) const {
    return Slic3r::Geometry::directions_parallel(this->direction(), angle);
}

bool
Line::parallel_to(const Line &line) const {
    return this->parallel_to(line.direction());
}

Vector
Line::vector() const
{
    return Vector(this->b.x - this->a.x, this->b.y - this->a.y);
}

Vector
Line::normal() const
{
    return Vector((this->b.y - this->a.y), -(this->b.x - this->a.x));
}

#ifdef SLIC3RXS

REGISTER_CLASS(Line, "Line");

void
Line::from_SV(SV* line_sv)
{
    AV* line_av = (AV*)SvRV(line_sv);
    this->a.from_SV_check(*av_fetch(line_av, 0, 0));
    this->b.from_SV_check(*av_fetch(line_av, 1, 0));
}

void
Line::from_SV_check(SV* line_sv)
{
    if (sv_isobject(line_sv) && (SvTYPE(SvRV(line_sv)) == SVt_PVMG)) {
        if (!sv_isa(line_sv, perl_class_name(this)) && !sv_isa(line_sv, perl_class_name_ref(this)))
            CONFESS("Not a valid %s object", perl_class_name(this));
        *this = *(Line*)SvIV((SV*)SvRV( line_sv ));
    } else {
        this->from_SV(line_sv);
    }
}

SV*
Line::to_AV() {
    AV* av = newAV();
    av_extend(av, 1);
    
    av_store(av, 0, perl_to_SV_ref(this->a));
    av_store(av, 1, perl_to_SV_ref(this->b));
    
    return newRV_noinc((SV*)av);
}

SV*
Line::to_SV_pureperl() const {
    AV* av = newAV();
    av_extend(av, 1);
    av_store(av, 0, this->a.to_SV_pureperl());
    av_store(av, 1, this->b.to_SV_pureperl());
    return newRV_noinc((SV*)av);
}
#endif

Pointf3
Linef3::intersect_plane(double z) const
{
    return Pointf3(
        this->a.x + (this->b.x - this->a.x) * (z - this->a.z) / (this->b.z - this->a.z),
        this->a.y + (this->b.y - this->a.y) * (z - this->a.z) / (this->b.z - this->a.z),
        z
    );
}

void
Linef3::scale(double factor)
{
    this->a.scale(factor);
    this->b.scale(factor);
}

#ifdef SLIC3RXS
REGISTER_CLASS(Linef3, "Linef3");
#endif

}
