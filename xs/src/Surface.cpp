#include "Surface.hpp"
#ifdef SLIC3RXS
#include "perlglue.hpp"
#endif

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

REGISTER_CLASS(Surface, "Surface");

void
Surface::from_SV_check(SV* surface_sv)
{
    if (!sv_isa(surface_sv, perl_class_name(this)) && !sv_isa(surface_sv, perl_class_name_ref(this)))
        CONFESS("Not a valid %s object", perl_class_name(this));
    // a XS Surface was supplied
    *this = *(Surface *)SvIV((SV*)SvRV( surface_sv ));
}

SV*
Surface::to_SV_ref() {
    SV* sv = newSV(0);
    sv_setref_pv( sv, perl_class_name_ref(this), (void*)this );
    return sv;
}

SV*
Surface::to_SV_clone_ref() const {
    SV* sv = newSV(0);
    sv_setref_pv( sv, perl_class_name(this), new Surface(*this) );
    return sv;
}
#endif

}
