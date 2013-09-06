#include "Surface.hpp"

namespace Slic3r {

SV*
Surface::to_SV_ref() {
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Surface::Ref", (void*)this );
    return sv;
}

double
Surface::area() const
{
    return this->expolygon.area();
}

}
