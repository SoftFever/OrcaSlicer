#include "Surface.hpp"

namespace Slic3r {

double
Surface::area() const
{
    return this->expolygon.area();
}

#ifdef SLIC3RXS
SV*
Surface::to_SV_ref() {
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Surface::Ref", (void*)this );
    return sv;
}
#endif

}
