#include "Polyline.hpp"

namespace Slic3r {

Point*
Polyline::last_point() const
{
    return new Point(this->points.back());
}

Lines
Polyline::lines() const
{
    Lines lines;
    lines.reserve(this->points.size() - 1);
    for (Points::const_iterator it = this->points.begin(); it != this->points.end()-1; ++it) {
        lines.push_back(Line(*it, *(it + 1)));
    }
    return lines;
}

#ifdef SLIC3RXS
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
#endif

}
