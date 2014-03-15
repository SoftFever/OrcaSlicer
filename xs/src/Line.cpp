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
Line::rotate(double angle, Point* center)
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
    return this->a.distance_to(&(this->b));
}

Point*
Line::midpoint() const
{
    return new Point ((this->a.x + this->b.x) / 2.0, (this->a.y + this->b.y) / 2.0);
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
Line::coincides_with(const Line* line) const
{
    return this->a.coincides_with(&line->a) && this->b.coincides_with(&line->b);
}

double
Line::distance_to(const Point* point) const
{
    return point->distance_to(this);
}

double
Line::atan2_() const
{
    return atan2(this->b.y - this->a.y, this->b.x - this->a.x);
}

double
Line::direction() const
{
    double atan2 = this->atan2_();
    return (atan2 == PI) ? 0
        : (atan2 < 0) ? (atan2 + PI)
        : atan2;
}

Vector
Line::vector() const
{
    return Vector(this->b.x - this->a.x, this->b.y - this->a.y);
}

#ifdef SLIC3RXS
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
        if (!sv_isa(line_sv, "Slic3r::Line") && !sv_isa(line_sv, "Slic3r::Line::Ref"))
            CONFESS("Not a valid Slic3r::Line object");
        *this = *(Line*)SvIV((SV*)SvRV( line_sv ));
    } else {
        this->from_SV(line_sv);
    }
}

SV*
Line::to_AV() {
    AV* av = newAV();
    av_extend(av, 1);
    
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Point::Ref", &(this->a) );
    av_store(av, 0, sv);
    
    sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Point::Ref", &(this->b) );
    av_store(av, 1, sv);
    
    return newRV_noinc((SV*)av);
}

SV*
Line::to_SV_ref() {
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Line::Ref", this );
    return sv;
}

SV*
Line::to_SV_clone_ref() const {
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Line", new Line(*this) );
    return sv;
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

}
