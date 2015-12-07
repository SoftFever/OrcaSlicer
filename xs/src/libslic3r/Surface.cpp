#include "Surface.hpp"

namespace Slic3r {

Surface::operator Polygons() const
{
    return this->expolygon;
}

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
        || this->surface_type == stInternalSolid
        || this->surface_type == stInternalBridge;
}

bool
Surface::is_external() const
{
    return this->surface_type == stTop
        || this->surface_type == stBottom
        || this->surface_type == stBottomBridge;
}

bool
Surface::is_internal() const
{
    return this->surface_type == stInternal
        || this->surface_type == stInternalBridge
        || this->surface_type == stInternalSolid
        || this->surface_type == stInternalVoid;
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

}
