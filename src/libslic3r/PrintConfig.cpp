#include "PrintConfig.hpp"
#include "CommonDefs.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "PrintConfigConstants.hpp"
#include "BeltTransform.hpp"
#include "ClipperUtils.hpp"
#include "Config.hpp"
#include "Geometry.hpp"
#include "FilamentMixer.hpp"
#include "MaterialType.hpp"
#include "I18N.hpp"
#include "enum_bitmask.hpp"
#include "format.hpp"

#include "GCode/Thumbnails.hpp"
#include <numeric>
#include <cstddef>
#include <algorithm>
#include <cassert>
#include <map>
#include <boost/algorithm/string/classification.hpp>
#include <iterator>
#include <cstdlib>
#include "libslic3r.h"
#include <boost/algorithm/string/predicate.hpp>
#include <cmath>
#include <boost/algorithm/string/constants.hpp>
#include <limits>
#include <memory>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>
#include <cstdint>
#include <set>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <boost/thread.hpp>
#include <float.h>
#include <string>
#include <vector>
#include <utility>
#include <sstream>
#include <unordered_map>

namespace {
std::set<std::string> SplitStringAndRemoveDuplicateElement(const std::string &str, const std::string &separator)
{
    std::set<std::string> result;
    if (str.empty()) return result;

    std::string strs = str + separator;
    size_t      pos;
    size_t      size = strs.size();

    for (int i = 0; i < size; ++i) {
        pos = strs.find(separator, i);
        if (pos < size) {
            std::string sub_str = strs.substr(i, pos - i);
            result.insert(sub_str);
            i = pos + separator.size() - 1;
        }
    }

    return result;
}

void ReplaceString(std::string &resource_str, const std::string &old_str, const std::string &new_str)
{
    std::string::size_type pos = 0;
    while ((pos = resource_str.find(old_str, pos)) != std::string::npos) {
        resource_str.replace(pos, old_str.length(), new_str);
        pos += new_str.length(); //advance position to continue after replacement
    }
}
}

namespace Slic3r {


// Filament types are defined in MaterialType.


size_t get_extruder_index(const GCodeConfig& config, unsigned int filament_id)
{
    if (filament_id < config.filament_map.size()) {
        return config.filament_map.get_at(filament_id)-1;
    }
    return 0;
}

double nozzle_diameter_for_filament(const PrintConfig& config, int filament_id, bool is_bbl_printer)
{
    int extruder = filament_id;
    if (is_bbl_printer && config.nozzle_diameter.size() > 1 &&
        filament_id >= 1 && static_cast<size_t>(filament_id - 1) < config.filament_map.size())
        extruder = config.filament_map.get_at(filament_id - 1);
    return config.nozzle_diameter.get_at(extruder - 1);
}


// Orca: input shaping values types by flavor
std::vector<std::string> get_shaper_type_values_for_flavor(GCodeFlavor flavor)
{
    switch (flavor) {
    case GCodeFlavor::gcfKlipper:
        return {"Default", "MZV", "ZV", "ZVD", "EI", "2HUMP_EI", "3HUMP_EI"};
    case GCodeFlavor::gcfRepRapFirmware:
        return {"Default", "MZV", "ZV", "ZVD", "ZVDD", "ZVDDD", "EI2", "EI3", "DAA"};
    case GCodeFlavor::gcfMarlinFirmware:
        return {"ZV"};
    case GCodeFlavor::gcfMarlinLegacy:
        return {};
    default:
        break;
    }
    return {"Default"};
}

static t_config_enum_names enum_names_from_keys_map(const t_config_enum_values &enum_keys_map)
{
    t_config_enum_names names;
    int cnt = 0;
    for (const auto& kvp : enum_keys_map)
        cnt = std::max(cnt, kvp.second);
    cnt += 1;
    names.assign(cnt, "");
    for (const auto& kvp : enum_keys_map)
        names[kvp.second] = kvp.first;
    return names;
}

#define CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NAME)     static t_config_enum_names s_keys_names_##NAME = enum_names_from_keys_map(s_keys_map_##NAME);     template<> const t_config_enum_values& ConfigOptionEnum<NAME>::get_enum_values() { return s_keys_map_##NAME; }     template<> const t_config_enum_names& ConfigOptionEnum<NAME>::get_enum_names() { return s_keys_names_##NAME; }

static t_config_enum_values s_keys_map_PrinterTechnology {
    { "FFF",            ptFFF },
    { "SLA",            ptSLA }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PrinterTechnology)

static t_config_enum_values s_keys_map_PrintHostType {
    { "prusalink",      htPrusaLink },
    { "prusaconnect",   htPrusaConnect },
    { "octoprint",      htOctoPrint },
    { "crealityprint",  htCrealityPrint },
    { "duet",           htDuet },
    { "ultimaker",      htUltiMaker },
    { "flashair",       htFlashAir },
    { "astrobox",       htAstroBox },
    { "repetier",       htRepetier },
    { "mks",            htMKS },
    { "esp3d",          htESP3D },
    { "obico",          htObico },
    { "flashforge",     htFlashforge },
    { "simplyprint",    htSimplyPrint },
    { "elegoolink",     htElegooLink },
    { "3dprinteros",    ht3DPrinterOS },
    { "moonraker",      htMoonraker }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PrintHostType)

static t_config_enum_values s_keys_map_AuthorizationType {
    { "key",            atKeyPassword },
    { "user",           atUserPassword }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(AuthorizationType)

static t_config_enum_values s_keys_map_GCodeFlavor {
    { "marlin",         gcfMarlinLegacy },
    { "klipper",        gcfKlipper },
    { "reprapfirmware", gcfRepRapFirmware },
    { "repetier",       gcfRepetier },
    { "marlin2",        gcfMarlinFirmware },
    { "reprap",         gcfRepRapSprinter },
    { "teacup",         gcfTeacup },
    { "makerware",      gcfMakerWare },
    { "sailfish",       gcfSailfish },
    { "smoothie",       gcfSmoothie },
    { "mach3",          gcfMach3 },
    { "machinekit",     gcfMachinekit },
    { "no-extrusion",   gcfNoExtrusion }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(GCodeFlavor)

static t_config_enum_values s_keys_map_BedTempFormula {
    { "by_first_filament",int(BedTempFormula::btfFirstFilament) },
    { "by_highest_temp", int(BedTempFormula::btfHighestTemp)}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(BedTempFormula)

// Orca
static t_config_enum_values s_keys_map_PowerLossRecoveryMode {
    { "printer_configuration", int(PowerLossRecoveryMode::PrinterConfiguration) },
    { "enable",                 int(PowerLossRecoveryMode::Enable) },
    { "disable",                int(PowerLossRecoveryMode::Disable) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PowerLossRecoveryMode)

static t_config_enum_values s_keys_map_CenterOfSurfacePattern{
    {"each_surface", int(CenterOfSurfacePattern::Each_Surface)},
    {"each_model", int(CenterOfSurfacePattern::Each_Model)},
    {"each_assembly", int(CenterOfSurfacePattern::Each_Assembly)}};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(CenterOfSurfacePattern)

static t_config_enum_values s_keys_map_FuzzySkinType {
    { "none",           int(FuzzySkinType::None) },
    { "external",       int(FuzzySkinType::External) },
    { "hole",           int(FuzzySkinType::Hole) },
    { "all",            int(FuzzySkinType::All) },
    { "allwalls",       int(FuzzySkinType::AllWalls)},
    { "disabled_fuzzy", int(FuzzySkinType::Disabled_fuzzy)}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(FuzzySkinType)

static t_config_enum_values s_keys_map_NoiseType {
    { "classic",        int(NoiseType::Classic) },
    { "perlin",         int(NoiseType::Perlin) },
    { "billow",         int(NoiseType::Billow) },
    { "ridgedmulti",    int(NoiseType::RidgedMulti) },
    { "voronoi",        int(NoiseType::Voronoi) },
    { "ripple",         int(NoiseType::Ripple) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NoiseType)

static t_config_enum_values s_keys_map_WipeTowerType {
    { "type1",          int(WipeTowerType::Type1) },
    { "type2",          int(WipeTowerType::Type2) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(WipeTowerType)


static t_config_enum_values s_keys_map_FuzzySkinMode {
    { "displacement",   int(FuzzySkinMode::Displacement) },
    { "extrusion",      int(FuzzySkinMode::Extrusion) },
    { "combined",       int(FuzzySkinMode::Combined)}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(FuzzySkinMode)

static t_config_enum_values s_keys_map_TopSurfaceExpansionDirection {
    { "inward_and_outward", int(TopSurfaceExpansionDirection::InwardAndOutward) },
    { "inward",             int(TopSurfaceExpansionDirection::Inward) },
    { "outward",            int(TopSurfaceExpansionDirection::Outward) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(TopSurfaceExpansionDirection)

static t_config_enum_values s_keys_map_InfillPattern {
    { "monotonic", ipMonotonic },
    { "monotonicline", ipMonotonicLine },
    { "rectilinear", ipRectilinear },
    { "alignedrectilinear", ipAlignedRectilinear },
    { "zigzag", ipZigZag },
    { "crosszag", ipCrossZag },
    { "lockedzag", ipLockedZag },
    { "line", ipLine },
    { "grid", ipGrid },
    { "triangles", ipTriangles },
    { "tri-hexagon", ipStars },
    { "cubic", ipCubic },
    { "adaptivecubic", ipAdaptiveCubic },
    { "quartercubic", ipQuarterCubic },
    { "supportcubic", ipSupportCubic },
    { "lightning", ipLightning },
    { "honeycomb", ipHoneycomb },
    { "3dhoneycomb", ip3DHoneycomb },
    { "lateral-honeycomb", ipLateralHoneycomb },
    { "lateral-lattice", ipLateralLattice },
    { "crosshatch", ipCrossHatch },
    { "tpmsd", ipTpmsD },
    { "tpmsfk", ipTpmsFK },
    { "gyroid", ipGyroid },
    { "concentric", ipConcentric },
    { "spiralinset", ipSpiralInset },
    { "hilbertcurve", ipHilbertCurve },
    { "archimedeanchords", ipArchimedeanChords },
    { "octagramspiral", ipOctagramSpiral }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(InfillPattern)

static t_config_enum_values s_keys_map_IroningType {
    { "no ironing",     int(IroningType::NoIroning) },
    { "top",            int(IroningType::TopSurfaces) },
    { "topmost",        int(IroningType::TopmostOnly) },
    { "solid",          int(IroningType::AllSolid) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(IroningType)

//BBS
static t_config_enum_values s_keys_map_WallInfillOrder {
    { "inner wall/outer wall/infill",     int(WallInfillOrder::InnerOuterInfill) },
    { "outer wall/inner wall/infill",     int(WallInfillOrder::OuterInnerInfill) },
    { "inner-outer-inner wall/infill",     int(WallInfillOrder::InnerOuterInnerInfill) },
    { "infill/inner wall/outer wall",     int(WallInfillOrder::InfillInnerOuter) },
    { "infill/outer wall/inner wall",     int(WallInfillOrder::InfillOuterInner) },
    { "inner-outer-inner wall/infill",     int(WallInfillOrder::InnerOuterInnerInfill)}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(WallInfillOrder)

//BBS
static t_config_enum_values s_keys_map_WallSequence {
    { "inner wall/outer wall",     int(WallSequence::InnerOuter) },
    { "outer wall/inner wall",     int(WallSequence::OuterInner) },
    { "inner-outer-inner wall",    int(WallSequence::InnerOuterInner)}

};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(WallSequence)

//Orca
static t_config_enum_values s_keys_map_WallDirection{
    { "ccw",  int(WallDirection::CounterClockwise) },
    { "cw",   int(WallDirection::Clockwise)},
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(WallDirection)

//Orca
static t_config_enum_values s_keys_map_SurfaceFillOrder{
    { "default",  int(SurfaceFillOrder::Default) },
    { "outward",  int(SurfaceFillOrder::Outward) },
    { "inward",   int(SurfaceFillOrder::Inward) },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SurfaceFillOrder)

//Orca
static t_config_enum_values s_keys_map_TpmsAdaptiveMode{
    { "disabled",       int(TpmsAdaptiveMode::Disabled) },
    { "distance_warp",  int(TpmsAdaptiveMode::DistanceWarp) },
    { "smooth_blend",   int(TpmsAdaptiveMode::SmoothBlend) },
    { "stepped_shells", int(TpmsAdaptiveMode::SteppedShells) },
    { "lobes",          int(TpmsAdaptiveMode::Lobes) },
    { "normal_z",       int(TpmsAdaptiveMode::NormalZ) },
    { "normal_y",       int(TpmsAdaptiveMode::NormalY) },
    { "normal_x",       int(TpmsAdaptiveMode::NormalX) },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(TpmsAdaptiveMode)

static t_config_enum_values s_keys_map_TpmsAdaptiveGradient{
    { "linear",      int(TpmsAdaptiveGradient::Linear) },
    { "quadratic",   int(TpmsAdaptiveGradient::Quadratic) },
    { "exponential", int(TpmsAdaptiveGradient::Exponential) },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(TpmsAdaptiveGradient)

//BBS
static t_config_enum_values s_keys_map_PrintSequence {
    { "by layer",     int(PrintSequence::ByLayer) },
    { "by object",    int(PrintSequence::ByObject) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PrintSequence)

static t_config_enum_values s_keys_map_PrintOrder{
    { "default",     int(PrintOrder::Default) },
    { "as_obj_list", int(PrintOrder::AsObjectList)},
    { "best_of",       int(PrintOrder::BestOfStrategies)},
    { "snake", int(PrintOrder::Snake)},
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PrintOrder)

static t_config_enum_values s_keys_map_SlicingMode {
    { "regular",        int(SlicingMode::Regular) },
    { "even_odd",       int(SlicingMode::EvenOdd) },
    { "close_holes",    int(SlicingMode::CloseHoles) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SlicingMode)

static t_config_enum_values s_keys_map_BeltRotationAxis {
    { "none", int(BeltRotationAxis::None) },
    { "x",    int(BeltRotationAxis::X) },
    { "y",    int(BeltRotationAxis::Y) },
    { "z",    int(BeltRotationAxis::Z) },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(BeltRotationAxis)

static t_config_enum_values s_keys_map_RemapAxis {
    { "pos_x", int(RemapAxis::PosX) },
    { "pos_y", int(RemapAxis::PosY) },
    { "pos_z", int(RemapAxis::PosZ) },
    { "neg_x", int(RemapAxis::NegX) },
    { "neg_y", int(RemapAxis::NegY) },
    { "neg_z", int(RemapAxis::NegZ) },
    { "rev_x", int(RemapAxis::RevX) },
    { "rev_y", int(RemapAxis::RevY) },
    { "rev_z", int(RemapAxis::RevZ) },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(RemapAxis)

static t_config_enum_values s_keys_map_SupportMaterialPattern {
    { "rectilinear",        smpRectilinear },
    { "rectilinear-grid",   smpRectilinearGrid },
    { "honeycomb",          smpHoneycomb },
    { "lightning",          smpLightning },
    { "default",            smpDefault},
    { "hollow",               smpNone},
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SupportMaterialPattern)

static t_config_enum_values s_keys_map_SupportMaterialStyle {
    { "default",        smsDefault },
    { "grid",           smsGrid },
    { "snug",           smsSnug },
    { "organic",        smsTreeOrganic },
    { "tree_slim",      smsTreeSlim },
    { "tree_strong",    smsTreeStrong },
    { "tree_hybrid",    smsTreeHybrid }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SupportMaterialStyle)

static t_config_enum_values s_keys_map_SupportMaterialInterfacePattern {
    { "auto",           smipAuto },
    { "rectilinear",    smipRectilinear },
    { "concentric",     smipConcentric },
    { "spiralinset",    smipSpiralInset },
    { "rectilinear_interlaced", smipRectilinearInterlaced},
    { "grid",           smipGrid }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SupportMaterialInterfacePattern)

static t_config_enum_values s_keys_map_SupportType{
    { "normal(auto)",   stNormalAuto },
    { "tree(auto)", stTreeAuto },
    { "normal(manual)", stNormal },
    { "tree(manual)", stTree }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SupportType)

static t_config_enum_values s_keys_map_SeamPosition {
    { "nearest",        spNearest },
    { "aligned",        spAligned },
    { "aligned_back",   spAlignedBack },
    { "back",           spRear },
    { "random",         spRandom }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SeamPosition)

// Orca
static t_config_enum_values s_keys_map_SeamScarfType{
    { "none",           int(SeamScarfType::None) },
    { "external",       int(SeamScarfType::External) },
    { "all",            int(SeamScarfType::All) },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SeamScarfType)

// Orca
static t_config_enum_values s_keys_map_EnsureVerticalShellThickness{
    { "none",           int(EnsureVerticalShellThickness::evstNone) },
    { "ensure_critical_only",         int(EnsureVerticalShellThickness::evstCriticalOnly) },
    { "ensure_moderate",            int(EnsureVerticalShellThickness::evstModerate) },
    { "ensure_all",         int(EnsureVerticalShellThickness::evstAll) },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(EnsureVerticalShellThickness)

// Orca
static t_config_enum_values s_keys_map_InternalBridgeFilter {
    { "disabled",        ibfDisabled },
    { "limited",        ibfLimited },
    { "nofilter",           ibfNofilter },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(InternalBridgeFilter)

static t_config_enum_values s_keys_map_EnableExtraBridgeLayer {
    { "disabled",        eblDisabled },
    { "external_bridge_only",        eblExternalBridgeOnly },
    { "internal_bridge_only",        eblInternalBridgeOnly },
    { "apply_to_all",           eblApplyToAll },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(EnableExtraBridgeLayer)

// Orca
static t_config_enum_values s_keys_map_GapFillTarget {
    { "everywhere",        gftEverywhere },
    { "topbottom",        gftTopBottom },
    { "nowhere",           gftNowhere },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(GapFillTarget)

static const t_config_enum_values s_keys_map_SLADisplayOrientation = {
    { "landscape",      sladoLandscape},
    { "portrait",       sladoPortrait}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SLADisplayOrientation)

static const t_config_enum_values s_keys_map_SLAPillarConnectionMode = {
    {"zigzag",          slapcmZigZag},
    {"cross",           slapcmCross},
    {"dynamic",         slapcmDynamic}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SLAPillarConnectionMode)

static const t_config_enum_values s_keys_map_SLAMaterialSpeed = {
    {"slow", slamsSlow},
    {"fast", slamsFast}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SLAMaterialSpeed);

static const t_config_enum_values s_keys_map_BrimType = {
    {"no_brim",         btNoBrim},
    {"outer_only",      btOuterOnly},
    {"inner_only",      btInnerOnly},
    {"outer_and_inner", btOuterAndInner},
    {"auto_brim", btAutoBrim},  // BBS
    {"brim_ears", btEar},     // Orca
    {"painted", btPainted},  // BBS
    {"leading_edge_only", btLeadingEdgeOnly},  // belt printers
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(BrimType)

// using 0,1 to compatible with old files
static const t_config_enum_values s_keys_map_TimelapseType = {
    {"0",       tlTraditional},
    {"1",       tlSmooth}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(TimelapseType)

static const t_config_enum_values s_keys_map_SkirtType = {
    { "combined", stCombined },
    { "perobject", stPerObject }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SkirtType)

static const t_config_enum_values s_keys_map_DraftShield = {
    { "disabled", dsDisabled },
    { "enabled",  dsEnabled  }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(DraftShield)

static const t_config_enum_values s_keys_map_ForwardCompatibilitySubstitutionRule = {
    { "disable",        ForwardCompatibilitySubstitutionRule::Disable },
    { "enable",         ForwardCompatibilitySubstitutionRule::Enable },
    { "enable_silent",  ForwardCompatibilitySubstitutionRule::EnableSilent }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(ForwardCompatibilitySubstitutionRule)

static const t_config_enum_values s_keys_map_OverhangFanThreshold = {
    { "0%",         Overhang_threshold_none },
    { "10%",        Overhang_threshold_1_4  },
    { "25%",        Overhang_threshold_2_4  },
    { "50%",        Overhang_threshold_3_4  },
    { "75%",        Overhang_threshold_4_4  },
    { "95%",        Overhang_threshold_bridge  }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(OverhangFanThreshold)

// BBS
static const t_config_enum_values s_keys_map_BedType = {
    { "Default Plate",      btDefault },
    { "Supertack Plate",    btSuperTack },
    { "Cool Plate",         btPC },
    { "Engineering Plate",  btEP  },
    { "High Temp Plate",    btPEI  },
    { "Textured PEI Plate", btPTE },
    { "Textured Cool Plate", btPCT }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(BedType)

// BBS
static const t_config_enum_values s_keys_map_LayerSeq = {
    { "Auto",              flsAuto },
    { "Customize",         flsCustomize },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(LayerSeq)

static t_config_enum_values s_keys_map_NozzleType {
    { "undefine",       int(NozzleType::ntUndefine) },
    { "hardened_steel", int(NozzleType::ntHardenedSteel) },
    { "stainless_steel", int(NozzleType::ntStainlessSteel)},
    { "tungsten_carbide", int(NozzleType::ntTungstenCarbide)},
    { "brass",          int(NozzleType::ntBrass) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NozzleType)

static t_config_enum_values s_keys_map_FanDirection {
    { "undefine",       int(FanDirection::fdUndefine) },
    { "left",           int(FanDirection::fdLeft) },
    { "right",          int(FanDirection::fdRight) },
    { "both",           int(FanDirection::fdBoth) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(FanDirection)

static t_config_enum_values s_keys_map_PrinterStructure {
    {"undefine",        int(PrinterStructure::psUndefine)},
    {"corexy",          int(PrinterStructure::psCoreXY)},
    {"i3",              int(PrinterStructure::psI3)},
    {"hbot",            int(PrinterStructure::psHbot)},
    {"delta",           int(PrinterStructure::psDelta)}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PrinterStructure)

static t_config_enum_values s_keys_map_InputShaperType {
    {"Default", int(InputShaperType::Default)},
    {"MZV",     int(InputShaperType::MZV)},
    {"ZV",      int(InputShaperType::ZV)},
    {"ZVD",     int(InputShaperType::ZVD)},
    {"ZVDD",    int(InputShaperType::ZVDD)},
    {"ZVDDD",   int(InputShaperType::ZVDDD)},
    {"EI",     int(InputShaperType::EI)},
    {"EI2",     int(InputShaperType::EI2)},
    {"2HUMP_EI",int(InputShaperType::TwoHumpEI)},
    {"EI3",     int(InputShaperType::EI3)},
    {"3HUMP_EI",int(InputShaperType::ThreeHumpEI)},
    {"DAA",     int(InputShaperType::DAA)},
    {"Disable", int(InputShaperType::Disable)}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(InputShaperType)

static t_config_enum_values s_keys_map_PerimeterGeneratorType{
    { "classic", int(PerimeterGeneratorType::Classic) },
    { "arachne", int(PerimeterGeneratorType::Arachne) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PerimeterGeneratorType)

static t_config_enum_values s_keys_map_ToolChangeOrderingType {
    { "default", int(ToolChangeOrderingType::Default) },
    { "cyclic",  int(ToolChangeOrderingType::Cyclic) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(ToolChangeOrderingType)

static const t_config_enum_values s_keys_map_ZHopType = {
    { "Auto Lift",          zhtAuto },
    { "Normal Lift",        zhtNormal },
    { "Slope Lift",         zhtSlope },
    { "Spiral Lift",        zhtSpiral }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(ZHopType)

static const t_config_enum_values s_keys_map_RetractLiftEnforceType = {
    {"All Surfaces",        rletAllSurfaces},
    {"Top Only",         rletTopOnly},
    {"Bottom Only",      rletBottomOnly},
    {"Top and Bottom",      rletTopAndBottom}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(RetractLiftEnforceType)

static const t_config_enum_values  s_keys_map_GCodeThumbnailsFormat = {
    { "PNG", int(GCodeThumbnailsFormat::PNG) },
    { "JPG", int(GCodeThumbnailsFormat::JPG) },
    { "QOI", int(GCodeThumbnailsFormat::QOI) },
    { "BTT_TFT", int(GCodeThumbnailsFormat::BTT_TFT) },
    { "COLPIC", int(GCodeThumbnailsFormat::ColPic) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(GCodeThumbnailsFormat)

static const t_config_enum_values s_keys_map_CounterboreHoleBridgingOption{
    { "none", chbNone },
    { "partiallybridge", chbBridges },
    { "sacrificiallayer", chbFilled },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(CounterboreHoleBridgingOption)

static const t_config_enum_values s_keys_map_WipeTowerWallType{
    {"rectangle", wtwRectangle},
    {"cone", wtwCone},
    {"rib", wtwRib},
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(WipeTowerWallType)

static const t_config_enum_values s_keys_map_ExtruderType = {
    { "Direct Drive",   etDirectDrive },
    { "Bowden",        etBowden }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(ExtruderType)

static const t_config_enum_values s_keys_map_NozzleVolumeType = {
    { "Standard",  nvtStandard },
    { "High Flow", nvtHighFlow },
    { "TPU High Flow", nvtTPUHighFlow },
    { "Hybrid", nvtHybrid },
    { "E3D High Flow", nvtE3DHighFlow },
    { "Extra High Flow", nvtExtraHighFlow }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NozzleVolumeType)

static const t_config_enum_values s_keys_map_FilamentMapMode = {
    { "Auto For Flush", fmmAutoForFlush },
    { "Auto For Match", fmmAutoForMatch },
    { "Manual", fmmManual },
    { "Nozzle Manual", fmmNozzleManual }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(FilamentMapMode)

// IMEX: keep the legacy hyphenated wire strings so existing presets / 3MFs
// deserialize unchanged after the coString -> coEnum migration.
static const t_config_enum_values s_keys_map_ImexToolLayout = {
    { "front-left",  int(ImexToolLayout::FrontLeft)  },
    { "front-right", int(ImexToolLayout::FrontRight) },
    { "rear-left",   int(ImexToolLayout::RearLeft)   },
    { "rear-right",  int(ImexToolLayout::RearRight)  }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(ImexToolLayout)

static const t_config_enum_values s_keys_map_ImexVizTheme = {
    { "standard",      int(ImexVizTheme::Standard)      },
    { "deuteranopia",  int(ImexVizTheme::Deuteranopia)  },
    { "tritanopia",    int(ImexVizTheme::Tritanopia)    },
    { "high_contrast", int(ImexVizTheme::HighContrast)  }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(ImexVizTheme)

// PrimeVolumeMode. Serialized string keys must stay stable; they round-trip through .3mf.
static const t_config_enum_values s_keys_map_PrimeVolumeMode = {
    { "Default", pvmDefault },
    { "Saving",  pvmSaving },
    { "Fast",    pvmFast }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PrimeVolumeMode)


//BBS
std::string get_extruder_variant_string(ExtruderType extruder_type, NozzleVolumeType nozzle_volume_type)
{
    std::string variant_string;

    if (extruder_type > etMaxExtruderType) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", unsupported ExtruderType=%1%")%extruder_type;
        //extruder_type = etDirectDrive;
        return variant_string;
    }
    auto nozzle_volume_types = get_valid_nozzle_volume_type();
    if (nozzle_volume_types.count(nozzle_volume_type) == 0) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", unsupported NozzleVolumeType=%1%")%nozzle_volume_type;
        //extruder_type = etDirectDrive;
        return variant_string;
    }
    variant_string = s_keys_names_ExtruderType[extruder_type];
    variant_string+= " ";
    variant_string+= s_keys_names_NozzleVolumeType[nozzle_volume_type];
    return variant_string;
}

int find_variant_index(const std::string& variant, int variant_id_1based, const std::vector<std::string>& variant_list, const std::vector<int>& variant_ids_1based)
{
    const int count = int(variant_list.empty() ? variant_ids_1based.size() : variant_list.size());
    if (count == 0)
        return 0;
    auto same_id = [&](int index) {
        return variant_id_1based < 0 || variant_ids_1based.empty() || (index < int(variant_ids_1based.size()) && variant_ids_1based[index] == variant_id_1based);
    };
    for (int index = 0; index < int(variant_list.size()); ++index)
        if (variant_list[index] == variant && same_id(index))
            return index;
    // Without this variant, use the id's own first variant (usually Standard), not variant index 0,
    // which belongs to the first filament or extruder.
    for (int index = 0; index < count; ++index)
        if (same_id(index))
            return index;
    return -1;
}

std::vector<int> map_variant_indices(const std::vector<std::string>& variants, const std::vector<int>& ids,
                                     const std::vector<std::string>& from_variants, const std::vector<int>& from_ids)
{
    const size_t count = variants.empty() ? ids.size() : variants.size();
    std::vector<int> variant_index(count);
    for (size_t index = 0; index < count; ++index) {
        if (!ids.empty() && index >= ids.size()) {
            variant_index[index] = -1;
            continue;
        }
        variant_index[index] = find_variant_index(index < variants.size() ? variants[index] : std::string(),
                                                  ids.empty() ? -1 : ids[index], from_variants, from_ids);
    }
    return variant_index;
}

int get_config_index_base(NozzleVolumeType volume_type, ExtruderType extruder_type, int variant_id_1based, const std::vector<std::string>& variant_list, const std::vector<int>& variant_ids_1based)
{
    assert(variant_list.size() == variant_ids_1based.size());
    const int index = find_variant_index(get_extruder_variant_string(extruder_type, volume_type), variant_id_1based, variant_list, variant_ids_1based);
    return std::max(index, 0);
}

std::set<NozzleVolumeType> get_extruder_supported_nozzle_volume_types(const DynamicPrintConfig &printer_config, int extruder_id)
{
    std::set<NozzleVolumeType> supported_types;

    auto *variant_list   = printer_config.option<ConfigOptionStrings>("extruder_variant_list");
    auto *extruder_types = printer_config.option<ConfigOptionEnumsGeneric>("extruder_type");
    if (!variant_list || !extruder_types || extruder_id < 0 ||
        extruder_id >= (int) variant_list->values.size() || extruder_id >= (int) extruder_types->values.size())
        return supported_types;

    const ExtruderType extruder_type = ExtruderType(extruder_types->values[extruder_id]);
    for (NozzleVolumeType volume_type : get_valid_nozzle_volume_type()) {
        // An unsupported extruder type yields an empty name, which would match any list.
        const std::string variant = get_extruder_variant_string(extruder_type, volume_type);
        if (!variant.empty() && variant_list->values[extruder_id].find(variant) != std::string::npos)
            supported_types.insert(volume_type);
    }
    return supported_types;
}

std::string get_nozzle_volume_type_string(NozzleVolumeType nozzle_volume_type)
{
    if (nozzle_volume_type > nvtMaxNozzleVolumeType) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", unsupported NozzleVolumeType=%1%") % nozzle_volume_type;
        return "";
    }
    return s_keys_names_NozzleVolumeType[nozzle_volume_type];
}

std::vector<std::map<int, int>> get_extruder_ams_count(const std::vector<std::string>& strs)
{
    std::vector<std::map<int, int>> extruder_ams_counts;
    for (const std::string& str : strs) {
        std::map<int, int> ams_count_info;
        if (str.empty()) {
            extruder_ams_counts.emplace_back(ams_count_info);
            continue;
        }
        std::vector<std::string> ams_infos;
        boost::algorithm::split(ams_infos, str, boost::algorithm::is_any_of("|"));
        for (const std::string& ams_info : ams_infos) {
            std::vector<std::string> numbers;
            boost::algorithm::split(numbers, ams_info, boost::algorithm::is_any_of("#"));
            assert(numbers.size() == 2);
            ams_count_info.insert(std::make_pair(stoi(numbers[0]), stoi(numbers[1])));
        }
        extruder_ams_counts.emplace_back(ams_count_info);
    }
    return extruder_ams_counts;
}

std::vector<std::string> save_extruder_ams_count_to_string(const std::vector<std::map<int, int>> &extruder_ams_count)
{
    std::vector<std::string> extruder_ams_count_str;
    for (size_t i = 0; i < extruder_ams_count.size(); ++i) {
        std::ostringstream oss;
        const auto &item = extruder_ams_count[i];
        for (auto it = item.begin(); it != item.end(); ++it) {
            oss << it->first << "#" << it->second;
            if (std::next(it) != item.end()) {
                oss << "|";
            }
        }
        extruder_ams_count_str.push_back(oss.str());
    }
    return extruder_ams_count_str;
}

// Maps a full extruder variant string such as "Direct Drive High Flow" to its
// NozzleVolumeType: the extruder-type prefix is stripped, the remainder is trimmed
// and looked up as a volume-type name. Returns nvtHybrid when the string cannot
// be parsed.
NozzleVolumeType convert_to_nvt_type(const std::string &variant_str) {
    const auto &ext_types = ConfigOptionEnum<ExtruderType>::get_enum_names();

    auto trim = [](std::string &s) {
        s.erase(s.find_last_not_of(" \t\r\n") + 1);
        s.erase(0, s.find_first_not_of(" \t\r\n"));
    };

    for (auto ext_type : ext_types) {
        size_t pos = variant_str.find(ext_type);
        if (pos == std::string::npos)
            continue;

        std::string result = variant_str;
        result.erase(pos, ext_type.size());
        trim(result);

        auto iter = s_keys_map_NozzleVolumeType.find(result);
        if (iter != s_keys_map_NozzleVolumeType.end())
            return NozzleVolumeType(iter->second);
    }

    return nvtHybrid;
}

// Parses the extruder_nozzle_stats option: one "|"-separated token list per
// extruder, each token
// "<volume_type_name>#<count>". Empty string => no per-type stats for that extruder.
std::vector<std::map<NozzleVolumeType, int>> get_extruder_nozzle_stats(const std::vector<std::string> &strs)
{
    std::vector<std::map<NozzleVolumeType, int>> extruder_nozzle_counts;
    for (const std::string &str : strs) {
        std::map<NozzleVolumeType, int> nozzle_count_map;
        if (str.empty()) {
            extruder_nozzle_counts.emplace_back(nozzle_count_map);
            continue;
        }
        std::vector<std::string> nozzle_infos;
        boost::algorithm::split(nozzle_infos, str, boost::algorithm::is_any_of("|"));
        for (auto &nozzle_info : nozzle_infos) {
            std::vector<std::string> attr;
            boost::algorithm::split(attr, nozzle_info, boost::algorithm::is_any_of("#"));
            // Guard against malformed device/project data: skip tokens without a "<type>#<count>"
            // shape or with an unknown volume-type key instead of throwing (.at) or reading attr[1]
            // out of bounds. Corrupt tokens are dropped and logged; the rest still parse.
            if (attr.size() < 2) {
                BOOST_LOG_TRIVIAL(warning) << "get_extruder_nozzle_stats: skipping malformed token '" << nozzle_info << "'";
                continue;
            }
            auto it = s_keys_map_NozzleVolumeType.find(attr[0]);
            if (it == s_keys_map_NozzleVolumeType.end()) {
                BOOST_LOG_TRIVIAL(warning) << "get_extruder_nozzle_stats: skipping unknown nozzle volume type '" << attr[0] << "'";
                continue;
            }
            NozzleVolumeType volume_type = NozzleVolumeType(it->second);
            int              nozzle_count = std::atoi(attr[1].c_str());
            nozzle_count_map[volume_type] = nozzle_count;
        }
        extruder_nozzle_counts.emplace_back(nozzle_count_map);
    }
    return extruder_nozzle_counts;
}

std::vector<std::string> save_extruder_nozzle_stats_to_string(const std::vector<std::map<NozzleVolumeType, int>> &extruder_nozzle_stats)
{
    std::vector<std::string> extruder_nozzle_count_str;
    for (size_t idx = 0; idx < extruder_nozzle_stats.size(); ++idx) {
        std::ostringstream oss;
        const auto        &item = extruder_nozzle_stats[idx];
        for (auto it = item.begin(); it != item.end(); ++it) {
            oss << get_nozzle_volume_type_string(it->first) << "#" << it->second;
            if (std::next(it) != item.end())
                oss << "|";
        }
        extruder_nozzle_count_str.emplace_back(oss.str());
    }
    return extruder_nozzle_count_str;
}








































} // namespace Slic3r

#include <cereal/types/polymorphic.hpp>
CEREAL_REGISTER_TYPE(Slic3r::DynamicPrintConfig)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::DynamicConfig, Slic3r::DynamicPrintConfig)