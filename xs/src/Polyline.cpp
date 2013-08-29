#include "Polyline.hpp"

namespace Slic3r {

Lines
Polyline::lines()
{
    Lines lines;
    for (int i = 0; i < this->points.size()-1; i++) {
        lines.push_back(Line(this->points[i], this->points[i+1]));
    }
    return lines;
}

SV*
Polyline::to_SV_ref() const
{
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Polyline", new Polyline(*this) );
    return sv;
}

}
