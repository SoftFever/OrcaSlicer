#include "Surface.hpp"

namespace Slic3r {

double
Surface::area() const
{
    return this->expolygon.area();
}

bool
Surface::is_solid() const
{
    return this->surface_type == stTop
        || this->surface_type == stBottom
        || this->surface_type == stInternalSolid;
}

bool
Surface::is_bridge() const
{
    return this->surface_type == stBottom
        || this->surface_type == stInternalBridge;
}

#ifdef SLIC3RXS
SV*
Surface::to_SV_ref() {
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Surface::Ref", (void*)this );
    return sv;
}

SV*
Surface::to_SV_clone_ref() const {
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Surface", new Surface(*this) );
    return sv;
}
#endif

}
