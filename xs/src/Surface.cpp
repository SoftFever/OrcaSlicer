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
        || this->surface_type == stBottomBridge
        || this->surface_type == stInternalSolid;
}

bool
Surface::is_external() const
{
    return this->surface_type == stTop
        || this->surface_type == stBottom
        || this->surface_type == stBottomBridge;
}

bool
Surface::is_bottom() const
{
    return this->surface_type == stBottom
        || this->surface_type == stBottomBridge;
}

bool
Surface::is_bridge() const
{
    return this->surface_type == stBottomBridge
        || this->surface_type == stInternalBridge;
}

#ifdef SLIC3RXS
void
Surface::from_SV_check(SV* surface_sv)
{
    if (!sv_isa(surface_sv, "Slic3r::Surface") && !sv_isa(surface_sv, "Slic3r::Surface::Ref"))
        CONFESS("Not a valid Slic3r::Surface object");
    // a XS Surface was supplied
    *this = *(Surface *)SvIV((SV*)SvRV( surface_sv ));
}

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
