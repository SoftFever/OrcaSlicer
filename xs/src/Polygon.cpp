#include <myinit.h>
#include "ClipperUtils.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"

namespace Slic3r {

SV*
Polygon::to_SV_ref() {
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Polygon", new Polygon(*this) );
    return sv;
}

Lines
Polygon::lines()
{
    Lines lines;
    for (int i = 0; i < this->points.size()-1; i++) {
        lines.push_back(Line(this->points[i], this->points[i+1]));
    }
    lines.push_back(Line(this->points.back(), this->points.front()));
    return lines;
}

Polyline*
Polygon::split_at_index(int index)
{
    Polyline* poly = new Polyline;
    for (int i = index; i < this->points.size(); i++) {
        poly->points.push_back( this->points[i] );
    }
    for (int i = 0; i <= index; i++) {
        poly->points.push_back( this->points[i] );
    }
    return poly;
}

Polyline*
Polygon::split_at_first_point()
{
    return this->split_at_index(0);
}

bool
Polygon::is_counter_clockwise()
{
    ClipperLib::Polygon* p = new ClipperLib::Polygon();
    Slic3rPolygon_to_ClipperPolygon(*this, *p);
    bool orientation = ClipperLib::Orientation(*p);
    delete p;
    return orientation;
}

bool
Polygon::make_counter_clockwise()
{
    if (!this->is_counter_clockwise()) {
        this->reverse();
        return true;
    }
    return false;
}

bool
Polygon::make_clockwise()
{
    if (this->is_counter_clockwise()) {
        this->reverse();
        return true;
    }
    return false;
}

}
