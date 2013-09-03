#include "Polyline.hpp"

namespace Slic3r {

Point*
Polyline::last_point() const
{
    return new Point(this->points.back());
}

void
Polyline::lines(Lines &lines) const
{
    lines.clear();
    for (int i = 0; i < this->points.size()-1; i++) {
        lines.push_back(Line(this->points[i], this->points[i+1]));
    }
}

SV*
Polyline::to_SV_ref()
{
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Polyline::Ref", (void*)this );
    return sv;
}

SV*
Polyline::to_SV_clone_ref() const
{
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Polyline", new Polyline(*this) );
    return sv;
}

}
