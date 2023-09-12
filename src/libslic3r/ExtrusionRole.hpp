///|/ Copyright (c) 2023 Robert Schiele @schiele
///|/ Copyright (c) Prusa Research 2023 Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_ExtrusionRole_hpp_
#define slic3r_ExtrusionRole_hpp_

#include "enum_bitmask.hpp"

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

struct ExtrusionRole : public ExtrusionRoleModifiers
{
    constexpr ExtrusionRole(const ExtrusionRoleModifier  bit) : ExtrusionRoleModifiers(bit) {}
    constexpr ExtrusionRole(const ExtrusionRoleModifiers bits) : ExtrusionRoleModifiers(bits) {}

    static constexpr const ExtrusionRoleModifiers None{};
    // Internal perimeter, not bridging.
    static constexpr const ExtrusionRoleModifiers Perimeter{ ExtrusionRoleModifier::Perimeter };
    // External perimeter, not bridging.
    static constexpr const ExtrusionRoleModifiers ExternalPerimeter{ ExtrusionRoleModifier::Perimeter | ExtrusionRoleModifier::External };
    // Perimeter, bridging. To be or'ed with ExtrusionRoleModifier::External for external bridging perimeter.
    static constexpr const ExtrusionRoleModifiers OverhangPerimeter{ ExtrusionRoleModifier::Perimeter | ExtrusionRoleModifier::Bridge };
    // Sparse internal infill.
    static constexpr const ExtrusionRoleModifiers InternalInfill{ ExtrusionRoleModifier::Infill };
    // Solid internal infill.
    static constexpr const ExtrusionRoleModifiers SolidInfill{ ExtrusionRoleModifier::Infill | ExtrusionRoleModifier::Solid };
    // Top solid infill (visible).
    //FIXME why there is no bottom solid infill type?
    static constexpr const ExtrusionRoleModifiers TopSolidInfill{ ExtrusionRoleModifier::Infill | ExtrusionRoleModifier::Solid | ExtrusionRoleModifier::External };
    // Ironing infill at the top surfaces.
    static constexpr const ExtrusionRoleModifiers Ironing{ ExtrusionRoleModifier::Infill | ExtrusionRoleModifier::Solid | ExtrusionRoleModifier::Ironing | ExtrusionRoleModifier::External };
    // Visible bridging infill at the bottom of an object.
    static constexpr const ExtrusionRoleModifiers BridgeInfill{ ExtrusionRoleModifier::Infill | ExtrusionRoleModifier::Solid | ExtrusionRoleModifier::Bridge | ExtrusionRoleModifier::External };
//    static constexpr const ExtrusionRoleModifiers InternalBridgeInfill{ ExtrusionRoleModifier::Infill | ExtrusionRoleModifier::Solid | ExtrusionRoleModifier::Bridge };
    // Gap fill extrusion, currently used for any variable width extrusion: Thin walls outside of the outer extrusion,
    // gap fill in between perimeters, gap fill between the inner perimeter and infill.
    //FIXME revise GapFill and ThinWall types, split Gap Fill to Gap Fill and ThinWall.
    static constexpr const ExtrusionRoleModifiers GapFill{ ExtrusionRoleModifier::Thin }; // | ExtrusionRoleModifier::External };
//    static constexpr const ExtrusionRoleModifiers ThinWall{ ExtrusionRoleModifier::Thin };
    static constexpr const ExtrusionRoleModifiers Skirt{ ExtrusionRoleModifier::Skirt };
    // Support base material, printed with non-soluble plastic.
    static constexpr const ExtrusionRoleModifiers SupportMaterial{ ExtrusionRoleModifier::Support };
    // Support interface material, printed with soluble plastic.
    static constexpr const ExtrusionRoleModifiers SupportMaterialInterface{ ExtrusionRoleModifier::Support | ExtrusionRoleModifier::External };
    // Wipe tower material.
    static constexpr const ExtrusionRoleModifiers WipeTower{ ExtrusionRoleModifier::Wipe };
    // Extrusion role for a collection with multiple extrusion roles.
    static constexpr const ExtrusionRoleModifiers Mixed{ ExtrusionRoleModifier::Mixed };

    bool is_perimeter() const { return this->ExtrusionRoleModifiers::has(ExtrusionRoleModifier::Perimeter); }
    bool is_external_perimeter() const { return this->is_perimeter() && this->is_external(); }
    bool is_infill() const { return this->ExtrusionRoleModifiers::has(ExtrusionRoleModifier::Infill); }
    bool is_solid_infill() const { return this->is_infill() && this->ExtrusionRoleModifiers::has(ExtrusionRoleModifier::Solid); }
    bool is_external() const { return this->ExtrusionRoleModifiers::has(ExtrusionRoleModifier::External); }
    bool is_bridge() const { return this->ExtrusionRoleModifiers::has(ExtrusionRoleModifier::Bridge); }

    bool is_support() const { return this->ExtrusionRoleModifiers::has(ExtrusionRoleModifier::Support); }
    bool is_support_base() const { return this->is_support() && ! this->is_external(); }
    bool is_support_interface() const { return this->is_support() && this->is_external(); }
    bool is_mixed() const { return this->ExtrusionRoleModifiers::has(ExtrusionRoleModifier::Mixed); }
};

// Special flags describing loop
enum ExtrusionLoopRole {
    elrDefault,
    elrContourInternalPerimeter,
    elrSkirt,
};

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
