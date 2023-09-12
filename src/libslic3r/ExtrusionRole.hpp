///|/ Copyright (c) 2023 Robert Schiele @schiele
///|/ Copyright (c) Prusa Research 2023 Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_ExtrusionRole_hpp_
#define slic3r_ExtrusionRole_hpp_

#include "enum_bitmask.hpp"
#include "ExtrusionEntity.hpp"

#include <string>
#include <string_view>
#include <cstdint>

namespace Slic3r {

enum class ExtrusionRoleModifier : uint16_t {
// 1) Extrusion types
    // Perimeter (external, inner, ...)
    Perimeter,
    // Infill (top / bottom / solid inner / sparse inner / bridging inner ...)
    Infill,
    // Variable width extrusion
    Thin,
    // Support material extrusion
    Support,
    Skirt,
    Wipe,
// 2) Extrusion modifiers
    External,
    Solid,
    Ironing,
    Bridge,
// 3) Special types
    // Indicator that the extrusion role was mixed from multiple differing extrusion roles,
    // for example from Support and SupportInterface.
    Mixed,
    // Stopper, there should be maximum 16 modifiers defined for uint16_t bit mask.
    Count
};
// There should be maximum 16 modifiers defined for uint16_t bit mask.
static_assert(int(ExtrusionRoleModifier::Count) <= 16, "ExtrusionRoleModifier: there must be maximum 16 modifiers defined to fit a 16 bit bitmask");

using ExtrusionRoleModifiers = enum_bitmask<ExtrusionRoleModifier>;
ENABLE_ENUM_BITMASK_OPERATORS(ExtrusionRoleModifier);



// Be careful when editing this list as many parts of the code depend
// on the values of these ordinars, for example
// GCodeViewer::Extrusion_Role_Colors
enum class GCodeExtrusionRole : uint8_t {
    None,
    Perimeter,
    ExternalPerimeter,
    OverhangPerimeter,
    InternalInfill,
    SolidInfill,
    TopSolidInfill,
    Ironing,
    BridgeInfill,
    GapFill,
    Skirt,
    SupportMaterial,
    SupportMaterialInterface,
    WipeTower,
    // Custom (user defined) G-code block, for example start / end G-code.
    Custom,
    // Stopper to count number of enums.
    Count
};

// Convert a rich bitmask based ExtrusionRole to a less expressive ordinal GCodeExtrusionRole.
// GCodeExtrusionRole is to be serialized into G-code and deserialized by G-code viewer,
GCodeExtrusionRole extrusion_role_to_gcode_extrusion_role(ExtrusionRole role);

std::string gcode_extrusion_role_to_string(GCodeExtrusionRole role);
GCodeExtrusionRole string_to_gcode_extrusion_role(const std::string_view role);

}

#endif // slic3r_ExtrusionRole_hpp_
