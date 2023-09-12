///|/ Copyright (c) Prusa Research 2023 Pavel Mikuš @Godrak, Oleksandra Iushchenko @YuSanka, Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#include "ExtrusionRole.hpp"
#include "I18N.hpp"

#include <string>
#include <string_view>
#include <cassert>


namespace Slic3r {

// Convert a rich bitmask based ExtrusionRole to a less expressive ordinal GCodeExtrusionRole.
// GCodeExtrusionRole is to be serialized into G-code and deserialized by G-code viewer,
GCodeExtrusionRole extrusion_role_to_gcode_extrusion_role(ExtrusionRole role)
{
    if (role == ExtrusionRole::None)                return GCodeExtrusionRole::None;
    if (role.is_perimeter()) {
        return role.is_bridge() ? GCodeExtrusionRole::OverhangPerimeter :
               role.is_external() ? GCodeExtrusionRole::ExternalPerimeter : GCodeExtrusionRole::Perimeter;
    }
    if (role == ExtrusionRole::InternalInfill)      return GCodeExtrusionRole::InternalInfill;
    if (role == ExtrusionRole::SolidInfill)         return GCodeExtrusionRole::SolidInfill;
    if (role == ExtrusionRole::TopSolidInfill)      return GCodeExtrusionRole::TopSolidInfill;
    if (role == ExtrusionRole::Ironing)             return GCodeExtrusionRole::Ironing;
    if (role == ExtrusionRole::BridgeInfill)        return GCodeExtrusionRole::BridgeInfill;
    if (role == ExtrusionRole::GapFill)             return GCodeExtrusionRole::GapFill;
    if (role == ExtrusionRole::Skirt)               return GCodeExtrusionRole::Skirt;
    if (role == ExtrusionRole::SupportMaterial)     return GCodeExtrusionRole::SupportMaterial;
    if (role == ExtrusionRole::SupportMaterialInterface) return GCodeExtrusionRole::SupportMaterialInterface;
    if (role == ExtrusionRole::WipeTower)           return GCodeExtrusionRole::WipeTower;
    assert(false);
    return GCodeExtrusionRole::None;
}

std::string gcode_extrusion_role_to_string(GCodeExtrusionRole role)
{
    switch (role) {
        case GCodeExtrusionRole::None                         : return L("Unknown");
        case GCodeExtrusionRole::Perimeter                    : return L("Perimeter");
        case GCodeExtrusionRole::ExternalPerimeter            : return L("External perimeter");
        case GCodeExtrusionRole::OverhangPerimeter            : return L("Overhang perimeter");
        case GCodeExtrusionRole::InternalInfill               : return L("Internal infill");
        case GCodeExtrusionRole::SolidInfill                  : return L("Solid infill");
        case GCodeExtrusionRole::TopSolidInfill               : return L("Top solid infill");
        case GCodeExtrusionRole::Ironing                      : return L("Ironing");
        case GCodeExtrusionRole::BridgeInfill                 : return L("Bridge infill");
        case GCodeExtrusionRole::GapFill                      : return L("Gap fill");
        case GCodeExtrusionRole::Skirt                        : return L("Skirt/Brim");
        case GCodeExtrusionRole::SupportMaterial              : return L("Support material");
        case GCodeExtrusionRole::SupportMaterialInterface     : return L("Support material interface");
        case GCodeExtrusionRole::WipeTower                    : return L("Wipe tower");
        case GCodeExtrusionRole::Custom                       : return L("Custom");
        default                             : assert(false);
    }
    return {};
}

GCodeExtrusionRole string_to_gcode_extrusion_role(const std::string_view role)
{
    if (role == L("Perimeter"))
        return GCodeExtrusionRole::Perimeter;
    else if (role == L("External perimeter"))
        return GCodeExtrusionRole::ExternalPerimeter;
    else if (role == L("Overhang perimeter"))
        return GCodeExtrusionRole::OverhangPerimeter;
    else if (role == L("Internal infill"))
        return GCodeExtrusionRole::InternalInfill;
    else if (role == L("Solid infill"))
        return GCodeExtrusionRole::SolidInfill;
    else if (role == L("Top solid infill"))
        return GCodeExtrusionRole::TopSolidInfill;
    else if (role == L("Ironing"))
        return GCodeExtrusionRole::Ironing;
    else if (role == L("Bridge infill"))
        return GCodeExtrusionRole::BridgeInfill;
    else if (role == L("Gap fill"))
        return GCodeExtrusionRole::GapFill;
    else if (role == L("Skirt") || role == L("Skirt/Brim")) // "Skirt" is for backward compatibility with 2.3.1 and earlier
        return GCodeExtrusionRole::Skirt;
    else if (role == L("Support material"))
        return GCodeExtrusionRole::SupportMaterial;
    else if (role == L("Support material interface"))
        return GCodeExtrusionRole::SupportMaterialInterface;
    else if (role == L("Wipe tower"))
        return GCodeExtrusionRole::WipeTower;
    else if (role == L("Custom"))
        return GCodeExtrusionRole::Custom;
    else
        return GCodeExtrusionRole::None;
}

}
