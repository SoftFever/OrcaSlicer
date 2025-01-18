#include "PrintConfig.hpp"
#include "ClipperUtils.hpp"
#include "Config.hpp"
#include "I18N.hpp"
#include "format.hpp"

#include "GCode/Thumbnails.hpp"
#include <set>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <boost/thread.hpp>
#include <float.h>

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
    while ((pos = resource_str.find(old_str)) != std::string::npos) { resource_str.replace(pos, old_str.length(), new_str); }
}
}

namespace Slic3r {

//! macro used to mark string used at localization,
//! return same string
#define L(s) (s)
#define _(s) Slic3r::I18N::translate(s)

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

#define CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NAME) \
    static t_config_enum_names s_keys_names_##NAME = enum_names_from_keys_map(s_keys_map_##NAME); \
    template<> const t_config_enum_values& ConfigOptionEnum<NAME>::get_enum_values() { return s_keys_map_##NAME; } \
    template<> const t_config_enum_names& ConfigOptionEnum<NAME>::get_enum_names() { return s_keys_names_##NAME; }

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
    { "flashair",       htFlashAir },
    { "astrobox",       htAstroBox },
    { "repetier",       htRepetier },
    { "mks",            htMKS },
    { "esp3d",          htESP3D },
    { "obico",          htObico },
    { "flashforge",     htFlashforge },
    { "simplyprint",    htSimplyPrint },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PrintHostType)

static t_config_enum_values s_keys_map_AuthorizationType {
    { "key",            atKeyPassword },
    { "user",           atUserPassword }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(AuthorizationType)

static t_config_enum_values s_keys_map_GCodeFlavor {
    { "marlin",         gcfMarlinLegacy },
    { "reprap",         gcfRepRapSprinter },
    { "reprapfirmware", gcfRepRapFirmware },
    { "repetier",       gcfRepetier },
    { "teacup",         gcfTeacup },
    { "makerware",      gcfMakerWare },
    { "marlin2",        gcfMarlinFirmware },
    { "sailfish",       gcfSailfish },
    { "klipper",        gcfKlipper },
    { "smoothie",       gcfSmoothie },
    { "mach3",          gcfMach3 },
    { "machinekit",     gcfMachinekit },
    { "no-extrusion",   gcfNoExtrusion }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(GCodeFlavor)


static t_config_enum_values s_keys_map_FuzzySkinType {
    { "none",           int(FuzzySkinType::None) },
    { "external",       int(FuzzySkinType::External) },
    { "all",            int(FuzzySkinType::All) },
    { "allwalls",       int(FuzzySkinType::AllWalls)}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(FuzzySkinType)

static t_config_enum_values s_keys_map_InfillPattern {
    { "concentric",         ipConcentric },
    { "zig-zag",            ipRectilinear },
    { "grid",               ipGrid },
    { "line",               ipLine },
    { "cubic",              ipCubic },
    { "triangles",          ipTriangles },
    { "tri-hexagon",        ipStars },
    { "gyroid",             ipGyroid },
    { "honeycomb",          ipHoneycomb },
    { "adaptivecubic",      ipAdaptiveCubic },
    { "monotonic",          ipMonotonic },
    { "monotonicline",      ipMonotonicLine },
    { "alignedrectilinear", ipAlignedRectilinear },
    { "3dhoneycomb",        ip3DHoneycomb },
    { "hilbertcurve",       ipHilbertCurve },
    { "archimedeanchords",  ipArchimedeanChords },
    { "octagramspiral",     ipOctagramSpiral },
    { "supportcubic",       ipSupportCubic },
    { "lightning",          ipLightning },
    { "crosshatch",         ipCrossHatch}
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
    { "auto", int(WallDirection::Auto) },
    { "ccw",  int(WallDirection::CounterClockwise) },
    { "cw",   int(WallDirection::Clockwise)},
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(WallDirection)

//BBS
static t_config_enum_values s_keys_map_PrintSequence {
    { "by layer",     int(PrintSequence::ByLayer) },
    { "by object",    int(PrintSequence::ByObject) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PrintSequence)

static t_config_enum_values s_keys_map_PrintOrder{
    { "default",     int(PrintOrder::Default) },
    { "as_obj_list", int(PrintOrder::AsObjectList)},
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PrintOrder)

static t_config_enum_values s_keys_map_SlicingMode {
    { "regular",        int(SlicingMode::Regular) },
    { "even_odd",       int(SlicingMode::EvenOdd) },
    { "close_holes",    int(SlicingMode::CloseHoles) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SlicingMode)

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
    { "tree_slim",      smsTreeSlim },
    { "tree_strong",    smsTreeStrong },
    { "tree_hybrid",    smsTreeHybrid },
    { "organic",        smsOrganic }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SupportMaterialStyle)

static t_config_enum_values s_keys_map_SupportMaterialInterfacePattern {
    { "auto",           smipAuto },
    { "rectilinear",    smipRectilinear },
    { "concentric",     smipConcentric },
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
    { "back",           spRear },
    { "random",         spRandom },
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
    { "Customize",         flsCutomize },
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(LayerSeq)

static t_config_enum_values s_keys_map_NozzleType {
    { "undefine",       int(NozzleType::ntUndefine) },
    { "hardened_steel", int(NozzleType::ntHardenedSteel) },
    { "stainless_steel",int(NozzleType::ntStainlessSteel) },
    { "brass",          int(NozzleType::ntBrass) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(NozzleType)

static t_config_enum_values s_keys_map_PrinterStructure {
    {"undefine",        int(PrinterStructure::psUndefine)},
    {"corexy",          int(PrinterStructure::psCoreXY)},
    {"i3",              int(PrinterStructure::psI3)},
    {"hbot",            int(PrinterStructure::psHbot)},
    {"delta",           int(PrinterStructure::psDelta)}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PrinterStructure)

static t_config_enum_values s_keys_map_PerimeterGeneratorType{
    { "classic", int(PerimeterGeneratorType::Classic) },
    { "arachne", int(PerimeterGeneratorType::Arachne) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PerimeterGeneratorType)

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

static void assign_printer_technology_to_unknown(t_optiondef_map &options, PrinterTechnology printer_technology)
{
    for (std::pair<const t_config_option_key, ConfigOptionDef> &kvp : options)
        if (kvp.second.printer_technology == ptUnknown)
            kvp.second.printer_technology = printer_technology;
}

PrintConfigDef::PrintConfigDef()
{
    this->init_common_params();
    assign_printer_technology_to_unknown(this->options, ptAny);
    this->init_fff_params();
    this->init_extruder_option_keys();
    assign_printer_technology_to_unknown(this->options, ptFFF);
    this->init_sla_params();
    assign_printer_technology_to_unknown(this->options, ptSLA);
}

void PrintConfigDef::init_common_params()
{
    ConfigOptionDef* def;

    def = this->add("printer_technology", coEnum);
    //def->label = L("Printer technology");
    def->label = "Printer technology";
    //def->tooltip = L("Printer technology");
    def->enum_keys_map = &ConfigOptionEnum<PrinterTechnology>::get_enum_values();
    def->enum_values.push_back("FFF");
    def->enum_values.push_back("SLA");
    def->set_default_value(new ConfigOptionEnum<PrinterTechnology>(ptFFF));

    def = this->add("printable_area", coPoints);
    def->label = L("Printable area");
    //BBS
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPoints{ Vec2d(0, 0), Vec2d(200, 0), Vec2d(200, 200), Vec2d(0, 200) });

    //BBS: add "bed_exclude_area"
    def = this->add("bed_exclude_area", coPoints);
    def->label = L("Bed exclude area");
    def->tooltip = L("Unprintable area in XY plane. For example, X1 Series printers use the front left corner to cut filament during filament change. "
        "The area is expressed as polygon by points in following format: \"XxY, XxY, ...\"");
    def->mode = comAdvanced;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionPoints{ Vec2d(0, 0) });

    def = this->add("bed_custom_texture", coString);
    def->label = L("Bed custom texture");
    def->mode = comAdvanced;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("bed_custom_model", coString);
    def->label = L("Bed custom model");
    def->mode = comAdvanced;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("elefant_foot_compensation", coFloat);
    def->label = L("Elephant foot compensation");
    def->category = L("Quality");
    def->tooltip = L("Shrink the initial layer on build plate to compensate for elephant foot effect");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def           = this->add("elefant_foot_compensation_layers", coInt);
    def->label    = L("Elephant foot compensation layers");
    def->category = L("Quality");
    def->tooltip  = L("The number of layers on which the elephant foot compensation will be active. "
                       "The first layer will be shrunk by the elephant foot compensation value, then "
                       "the next layers will be linearly shrunk less, up to the layer indicated by this value.");
    def->sidetext = L("layers");
    def->min      = 1;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));	

    def = this->add("layer_height", coFloat);
    def->label = L("Layer height");
    def->category = L("Quality");
    def->tooltip = L("Slicing height for each layer. Smaller layer height means more accurate and more printing time");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("printable_height", coFloat);
    def->label = L("Printable height");
    def->tooltip = L("Maximum printable height which is limited by mechanism of printer");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 214700;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(100.0));

    def = this->add("preferred_orientation", coFloat);
    def->label = L("Preferred orientation");
    def->tooltip = L("Automatically orient stls on the Z-axis upon initial import");
    def->sidetext = L("°");
    def->max = 360;
    def->min = -360;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    // Options used by physical printers

    def = this->add("preset_names", coStrings);
    def->label = L("Printer preset names");
    //def->tooltip = L("Names of presets related to the physical printer");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("bbl_use_printhost", coBool);
    def->label = L("Use 3rd-party print host");
    def->tooltip = L("Allow controlling BambuLab's printer through 3rd party print hosts");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("print_host", coString);
    def->label = L("Hostname, IP or URL");
    def->tooltip = L("Orca Slicer can upload G-code files to a printer host. This field should contain "
        "the hostname, IP address or URL of the printer host instance. "
        "Print host behind HAProxy with basic auth enabled can be accessed by putting the user name and password into the URL "
        "in the following format: https://username:password@your-octopi-address/");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("print_host_webui", coString);
    def->label = L("Device UI");
    def->tooltip = L("Specify the URL of your device user interface if it's not same as print_host");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("printhost_apikey", coString);
    def->label = L("API Key / Password");
    def->tooltip = L("Orca Slicer can upload G-code files to a printer host. This field should contain "
        "the API Key or the password required for authentication.");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("printhost_port", coString);
    def->label = L("Printer");
    def->tooltip = L("Name of the printer");
    def->gui_type = ConfigOptionDef::GUIType::select_open;
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("printhost_cafile", coString);
    def->label = L("HTTPS CA File");
    def->tooltip = L("Custom CA certificate file can be specified for HTTPS OctoPrint connections, in crt/pem format. "
        "If left blank, the default OS CA certificate repository is used.");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString(""));

    // Options used by physical printers

    def = this->add("printhost_user", coString);
    def->label = L("User");
    //    def->tooltip = "";
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("printhost_password", coString);
    def->label = L("Password");
    //    def->tooltip = "";
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString(""));

    // Only available on Windows.
    def = this->add("printhost_ssl_ignore_revoke", coBool);
    def->label = L("Ignore HTTPS certificate revocation checks");
    def->tooltip = L("Ignore HTTPS certificate revocation checks in case of missing or offline distribution points. "
        "One may want to enable this option for self signed certificates if connection fails.");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("preset_names", coStrings);
    def->label = L("Printer preset names");
    def->tooltip = L("Names of presets related to the physical printer");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("printhost_authorization_type", coEnum);
    def->label = L("Authorization Type");
    //    def->tooltip = "";
    def->enum_keys_map = &ConfigOptionEnum<AuthorizationType>::get_enum_values();
    def->enum_values.push_back("key");
    def->enum_values.push_back("user");
    def->enum_labels.push_back(L("API key"));
    def->enum_labels.push_back(L("HTTP digest"));
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionEnum<AuthorizationType>(atKeyPassword));
    
    // temporary workaround for compatibility with older Slicer
    {
        def = this->add("preset_name", coString);
        def->set_default_value(new ConfigOptionString());
    }
}

void PrintConfigDef::init_fff_params()
{
    ConfigOptionDef* def;

    // Maximum extruder temperature, bumped to 1500 to support printing of glass.
    const int max_temp = 1500;

    def = this->add("reduce_crossing_wall", coBool);
    def->label = L("Avoid crossing wall");
    def->category = L("Quality");
    def->tooltip = L("Detour and avoid to travel across wall which may cause blob on surface");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("max_travel_detour_distance", coFloatOrPercent);
    def->label = L("Avoid crossing wall - Max detour length");
    def->category = L("Quality");
    def->tooltip = L("Maximum detour distance for avoiding crossing wall. "
                     "Don't detour if the detour distance is large than this value. "
                     "Detour length could be specified either as an absolute value or as percentage (for example 50%) of a direct travel path. Zero to disable");
    def->sidetext = L("mm or %");
    def->min = 0;
    def->max_literal = 1000;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    // BBS
    def             = this->add("supertack_plate_temp", coInts);
    def->label      = L("Other layers");
    def->tooltip    = L("Bed temperature for layers except the initial one. "
                     "Value 0 means the filament does not support to print on the Cool Plate");
    def->sidetext   = "°C";
    def->full_label = L("Bed temperature");
    def->min        = 0;
    def->max        = 120;
    def->set_default_value(new ConfigOptionInts{35});

    def = this->add("cool_plate_temp", coInts);
    def->label = L("Other layers");
    def->tooltip = L("Bed temperature for layers except the initial one. "
        "Value 0 means the filament does not support to print on the Cool Plate");
    def->sidetext = L("°C");
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 300;
    def->set_default_value(new ConfigOptionInts{ 35 });

    def = this->add("textured_cool_plate_temp", coInts);
    def->label = L("Other layers");
    def->tooltip = L("Bed temperature for layers except the initial one. "
        "Value 0 means the filament does not support to print on the Textured Cool Plate");
    def->sidetext = L("°C");
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 300;
    def->set_default_value(new ConfigOptionInts{ 40 });

    def = this->add("eng_plate_temp", coInts);
    def->label = L("Other layers");
    def->tooltip = L("Bed temperature for layers except the initial one. "
        "Value 0 means the filament does not support to print on the Engineering Plate");
    def->sidetext = L("°C");
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 300;
    def->set_default_value(new ConfigOptionInts{ 45 });

    def = this->add("hot_plate_temp", coInts);
    def->label = L("Other layers");
    def->tooltip = L("Bed temperature for layers except the initial one. "
        "Value 0 means the filament does not support to print on the High Temp Plate");
    def->sidetext = L("°C");
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 300;
    def->set_default_value(new ConfigOptionInts{ 45 });

    def             = this->add("textured_plate_temp", coInts);
    def->label      = L("Other layers");
    def->tooltip    = L("Bed temperature for layers except the initial one. "
                     "Value 0 means the filament does not support to print on the Textured PEI Plate");
    def->sidetext   = L("°C");
    def->full_label = L("Bed temperature");
    def->min        = 0;
    def->max        = 300;
    def->set_default_value(new ConfigOptionInts{45});

    def = this->add("supertack_plate_temp_initial_layer", coInts);
    def->label = L("Initial layer");
    def->full_label = L("Initial layer bed temperature");
    def->tooltip = L("Bed temperature of the initial layer. "
        "Value 0 means the filament does not support to print on the Cool Plate SuperTack");
    def->sidetext = "°C";
    def->min = 0;
    def->max = 120;
    def->set_default_value(new ConfigOptionInts{ 35 });

    def = this->add("cool_plate_temp_initial_layer", coInts);
    def->label = L("Initial layer");
    def->full_label = L("Initial layer bed temperature");
    def->tooltip = L("Bed temperature of the initial layer. "
        "Value 0 means the filament does not support to print on the Cool Plate");
    def->sidetext = L("°C");
    def->min = 0;
    def->max = 120;
    def->set_default_value(new ConfigOptionInts{ 35 });

    def = this->add("textured_cool_plate_temp_initial_layer", coInts);
    def->label = L("Initial layer");
    def->full_label = L("Initial layer bed temperature");
    def->tooltip = L("Bed temperature of the initial layer. "
        "Value 0 means the filament does not support to print on the Textured Cool Plate");
    def->sidetext = L("°C");
    def->min = 0;
    def->max = 120;
    def->set_default_value(new ConfigOptionInts{ 40 });

    def = this->add("eng_plate_temp_initial_layer", coInts);
    def->label = L("Initial layer");
    def->full_label = L("Initial layer bed temperature");
    def->tooltip = L("Bed temperature of the initial layer. "
        "Value 0 means the filament does not support to print on the Engineering Plate");
    def->sidetext = L("°C");
    def->min = 0;
    def->max = 300;
    def->set_default_value(new ConfigOptionInts{ 45 });

    def = this->add("hot_plate_temp_initial_layer", coInts);
    def->label = L("Initial layer");
    def->full_label = L("Initial layer bed temperature");
    def->tooltip = L("Bed temperature of the initial layer. "
        "Value 0 means the filament does not support to print on the High Temp Plate");
    def->sidetext = L("°C");
    def->max = 300;
    def->set_default_value(new ConfigOptionInts{ 45 });

    def             = this->add("textured_plate_temp_initial_layer", coInts);
    def->label      = L("Initial layer");
    def->full_label = L("Initial layer bed temperature");
    def->tooltip    = L("Bed temperature of the initial layer. "
                     "Value 0 means the filament does not support to print on the Textured PEI Plate");
    def->sidetext   = L("°C");
    def->min        = 0;
    def->max        = 300;
    def->set_default_value(new ConfigOptionInts{45});

    def = this->add("curr_bed_type", coEnum);
    def->label = L("Bed type");
    def->tooltip = L("Bed types supported by the printer");
    def->mode = comSimple;
    def->enum_keys_map = &s_keys_map_BedType;
    // Orca: make sure the order of the values is the same as the BedType enum 
    def->enum_values.emplace_back("Supertack Plate");
    def->enum_values.emplace_back("Cool Plate");
    def->enum_values.emplace_back("Engineering Plate");
    def->enum_values.emplace_back("High Temp Plate");
    def->enum_values.emplace_back("Textured PEI Plate");
    def->enum_values.emplace_back("Textured Cool Plate");
    def->enum_labels.emplace_back(L("Cool Plate (SuperTack)"));
    def->enum_labels.emplace_back(L("Smooth Cool Plate"));
    def->enum_labels.emplace_back(L("Engineering Plate"));
    def->enum_labels.emplace_back(L("Smooth High Temp Plate"));
    def->enum_labels.emplace_back(L("Textured PEI Plate"));
    def->enum_labels.emplace_back(L("Textured Cool Plate"));
    def->set_default_value(new ConfigOptionEnum<BedType>(btPC));

    // BBS
    def             = this->add("first_layer_print_sequence", coInts);
    def->label      = L("First layer print sequence");
    def->min        = 0;
    def->max        = 16;
    def->set_default_value(new ConfigOptionInts{0});

    def        = this->add("other_layers_print_sequence", coInts);
    def->label = L("Other layers print sequence");
    def->min   = 0;
    def->max   = 16;
    def->set_default_value(new ConfigOptionInts{0});

    def        = this->add("other_layers_print_sequence_nums", coInt);
    def->label = L("The number of other layers print sequence");
    def->set_default_value(new ConfigOptionInt{0});

    def = this->add("first_layer_sequence_choice", coEnum);
    def->category = L("Quality");
    def->label = L("First layer filament sequence");
    def->enum_keys_map = &ConfigOptionEnum<LayerSeq>::get_enum_values();
    def->enum_values.push_back("Auto");
    def->enum_values.push_back("Customize");
    def->enum_labels.push_back(L("Auto"));
    def->enum_labels.push_back(L("Customize"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<LayerSeq>(flsAuto));

    def = this->add("other_layers_sequence_choice", coEnum);
    def->category = L("Quality");
    def->label = L("Other layers filament sequence");
    def->enum_keys_map = &ConfigOptionEnum<LayerSeq>::get_enum_values();
    def->enum_values.push_back("Auto");
    def->enum_values.push_back("Customize");
    def->enum_labels.push_back(L("Auto"));
    def->enum_labels.push_back(L("Customize"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<LayerSeq>(flsAuto));

    def = this->add("before_layer_change_gcode", coString);
    def->label = L("Before layer change G-code");
    def->tooltip = L("This G-code is inserted at every layer change before lifting z");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("bottom_shell_layers", coInt);
    def->label = L("Bottom shell layers");
    def->category = L("Strength");
    def->sidetext = L("layers"); // ORCA add side text
    def->tooltip =  L("This is the number of solid layers of bottom shell, including the bottom "
                      "surface layer. When the thickness calculated by this value is thinner "
                      "than bottom shell thickness, the bottom shell layers will be increased");
    def->full_label = L("Bottom shell layers");
    def->min = 0;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("bottom_shell_thickness", coFloat);
    def->label = L("Bottom shell thickness");
    def->category = L("Strength");
    def->tooltip = L("The number of bottom solid layers is increased when slicing if the thickness calculated by bottom shell layers is "
                     "thinner than this value. This can avoid having too thin shell when layer height is small. 0 means that "
                     "this setting is disabled and thickness of bottom shell is absolutely determined by bottom shell layers");
    def->full_label = L("Bottom shell thickness");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.));
    
    def = this->add("gap_fill_target", coEnum);
    def->label = L("Apply gap fill");
    def->category = L("Strength");
    def->tooltip = L("Enables gap fill for the selected solid surfaces. The minimum gap length that will be filled can be controlled "
                     "from the filter out tiny gaps option below.\n\n"
                     "Options:\n"
                     "1. Everywhere: Applies gap fill to top, bottom and internal solid surfaces for maximum strength\n"
                     "2. Top and Bottom surfaces: Applies gap fill to top and bottom surfaces only, balancing print speed, "
                     "reducing potential over extrusion in the solid infill and making sure the top and bottom surfaces have "
                     "no pin hole gaps\n"
                     "3. Nowhere: Disables gap fill for all solid infill areas. \n\n"
                     "Note that if using the classic perimeter generator, gap fill may also be generated between perimeters, "
                     "if a full width line cannot fit between them. That perimeter gap fill is not controlled by this setting. \n\n"
                     "If you would like all gap fill, including the classic perimeter generated one, removed, "
                     "set the filter out tiny gaps value to a large number, like 999999. \n\n"
                     "However this is not advised, as gap fill between perimeters is contributing to the model's strength. "
                     "For models where excessive gap fill is generated between perimeters, a better option would be to "
                     "switch to the arachne wall generator and use this option to control whether the cosmetic top and "
                     "bottom surface gap fill is generated");
    def->enum_keys_map = &ConfigOptionEnum<GapFillTarget>::get_enum_values();
    def->enum_values.push_back("everywhere");
    def->enum_values.push_back("topbottom");
    def->enum_values.push_back("nowhere");
    def->enum_labels.push_back(L("Everywhere"));
    def->enum_labels.push_back(L("Top and bottom surfaces"));
    def->enum_labels.push_back(L("Nowhere"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<GapFillTarget>(gftNowhere));
    

    def = this->add("enable_overhang_bridge_fan", coBools);
    def->label = L("Force cooling for overhang and bridge");
    def->tooltip = L("Enable this option to optimize part cooling fan speed for overhang and bridge to get better cooling");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBools{ true });

    def = this->add("overhang_fan_speed", coInts);
    def->label = L("Fan speed for overhang");
    def->tooltip = L("Force part cooling fan to be this speed when printing bridge or overhang wall which has large overhang degree. "
                     "Forcing cooling for overhang and bridge can get better quality for these part");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts { 100 });

    def = this->add("overhang_fan_threshold", coEnums);
    def->label = L("Cooling overhang threshold");
    def->tooltip = L("Force cooling fan to be specific speed when overhang degree of printed part exceeds this value. "
                     "Expressed as percentage which indicates how much width of the line without support from lower layer. "
                     "0% means forcing cooling for all outer wall no matter how much overhang degree");
    def->sidetext = "";
    def->enum_keys_map = &ConfigOptionEnum<OverhangFanThreshold>::get_enum_values();
    def->mode = comAdvanced;
    def->enum_values.emplace_back("0%");
    def->enum_values.emplace_back("10%");
    def->enum_values.emplace_back("25%");
    def->enum_values.emplace_back("50%");
    def->enum_values.emplace_back("75%");
    def->enum_values.emplace_back("95%");
    def->enum_labels.emplace_back("0%");
    def->enum_labels.emplace_back("10%");
    def->enum_labels.emplace_back("25%");
    def->enum_labels.emplace_back("50%");
    def->enum_labels.emplace_back("75%");
    def->enum_labels.emplace_back("95%");
    def->set_default_value(new ConfigOptionEnumsGeneric{ (int)Overhang_threshold_bridge });

    def = this->add("bridge_angle", coFloat);
    def->label = L("Bridge infill direction");
    def->category = L("Strength");
    def->tooltip = L("Bridging angle override. If left to zero, the bridging angle will be calculated "
        "automatically. Otherwise the provided angle will be used for external bridges. "
        "Use 180°for zero angle.");
    def->sidetext = L("°");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("bridge_density", coPercent);
    def->label = L("Bridge density");
    def->category = L("Strength");
    def->tooltip = L("Density of external bridges. 100% means solid bridge. Default is 100%.");
    def->sidetext = L("%");
    def->min = 10;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("bridge_flow", coFloat);
    def->label = L("Bridge flow ratio");
    def->category = L("Quality");
    def->tooltip = L("Decrease this value slightly(for example 0.9) to reduce the amount of material for bridge, "
                     "to improve sag. \n\nThe actual bridge flow used is calculated by multiplying this value with the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2.0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("internal_bridge_flow", coFloat);
    def->label = L("Internal bridge flow ratio");
    def->category = L("Quality");
    def->tooltip = L("This value governs the thickness of the internal bridge layer. This is the first layer over sparse infill. Decrease this value slightly (for example 0.9) to improve surface quality over sparse infill."
                     "\n\nThe actual internal bridge flow used is calculated by multiplying this value with the bridge flow ratio, the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2.0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("top_solid_infill_flow_ratio", coFloat);
    def->label = L("Top surface flow ratio");
    def->category = L("Advanced");
    def->tooltip = L("This factor affects the amount of material for top solid infill. "
                   "You can decrease it slightly to have smooth surface finish. "
                     "\n\nThe actual top surface flow used is calculated by multiplying this value with the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("bottom_solid_infill_flow_ratio", coFloat);
    def->label = L("Bottom surface flow ratio");
    def->category = L("Advanced");
    def->tooltip = L("This factor affects the amount of material for bottom solid infill. "
                     "\n\nThe actual bottom solid infill flow used is calculated by multiplying this value with the filament flow ratio, and if set, the object's flow ratio.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));


    def = this->add("precise_outer_wall",coBool);
    def->label = L("Precise wall");
    def->category = L("Quality");
    def->tooltip  = L("Improve shell precision by adjusting outer wall spacing. This also improves layer consistency.\nNote: This setting "
                       "will only take effect if the wall sequence is configured to Inner-Outer");
    def->set_default_value(new ConfigOptionBool{false});
    
    def = this->add("only_one_wall_top", coBool);
    def->label = L("Only one wall on top surfaces");
    def->category = L("Quality");
    def->tooltip = L("Use only one wall on flat top surface, to give more space to the top infill pattern");
    def->set_default_value(new ConfigOptionBool(false));

    // the tooltip is copied from SuperStudio
    def = this->add("min_width_top_surface", coFloatOrPercent);
    def->label = L("One wall threshold");
    def->category = L("Quality");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("If a top surface has to be printed and it's partially covered by another layer, it won't be considered at a top layer where its width is below this value."
        " This can be useful to not let the 'one perimeter on top' trigger on surface that should be covered only by perimeters."
        " This value can be a mm or a % of the perimeter extrusion width."
        "\nWarning: If enabled, artifacts can be created if you have some thin features on the next layer, like letters. Set this setting to 0 to remove these artifacts.");
    def->sidetext = L("mm or %");
    def->ratio_over = "inner_wall_line_width";
    def->min = 0;
    def->max_literal = 15;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(300, true));

    def = this->add("only_one_wall_first_layer", coBool);
    def->label = L("Only one wall on first layer");
    def->category = L("Quality");
    def->tooltip = L("Use only one wall on first layer, to give more space to the bottom infill pattern");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("extra_perimeters_on_overhangs", coBool);
    def->label = L("Extra perimeters on overhangs");
    def->category = L("Quality");
    def->tooltip = L("Create additional perimeter paths over steep overhangs and areas where bridges cannot be anchored. ");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("overhang_reverse", coBool);
    def->label = L("Reverse on even");
    def->full_label = L("Overhang reversal");
    def->category = L("Quality");
    def->tooltip = L("Extrude perimeters that have a part over an overhang in the reverse direction on even layers. This alternating pattern can drastically improve steep overhangs.\n\nThis setting can also help reduce part warping due to the reduction of stresses in the part walls.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));
    
    def = this->add("overhang_reverse_internal_only", coBool);
    def->label = L("Reverse only internal perimeters");
    def->full_label = L("Reverse only internal perimeters");
    def->category = L("Quality");
    def->tooltip = L("Apply the reverse perimeters logic only on internal perimeters. \n\nThis setting greatly reduces part stresses as they are now distributed in alternating directions. This should reduce part warping while also maintaining external wall quality. This feature can be very useful for warp prone material, like ABS/ASA, and also for elastic filaments, like TPU and Silk PLA. It can also help reduce warping on floating regions over supports.\n\nFor this setting to be the most effective, it is recommended to set the Reverse Threshold to 0 so that all internal walls print in alternating directions on even layers irrespective of their overhang degree.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("counterbore_hole_bridging", coEnum);
    def->label = L("Bridge counterbore holes");
    def->category = L("Quality");
    def->tooltip  = L(
        "This option creates bridges for counterbore holes, allowing them to be printed without support. Available modes include:\n"
         "1. None: No bridge is created.\n"
         "2. Partially Bridged: Only a part of the unsupported area will be bridged.\n"
         "3. Sacrificial Layer: A full sacrificial bridge layer is created.");
    def->mode = comAdvanced;
    def->enum_keys_map = &ConfigOptionEnum<CounterboreHoleBridgingOption>::get_enum_values();
    def->enum_values.emplace_back("none");
    def->enum_values.emplace_back("partiallybridge");
    def->enum_values.emplace_back("sacrificiallayer");
    def->enum_labels.emplace_back(L("None"));
    def->enum_labels.emplace_back(L("Partially bridged"));
    def->enum_labels.emplace_back(L("Sacrificial layer"));
    def->set_default_value(new ConfigOptionEnum<CounterboreHoleBridgingOption>(chbNone));

    def = this->add("overhang_reverse_threshold", coFloatOrPercent);
    def->label = L("Reverse threshold");
    def->full_label = L("Overhang reversal threshold");
    def->category = L("Quality");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Number of mm the overhang need to be for the reversal to be considered useful. Can be a % of the perimeter width."
                     "\nValue 0 enables reversal on every even layers regardless."
                     "\nWhen Detect overhang wall is not enabled, this option is ignored and reversal happens on every even layers regardless.");
    def->sidetext = L("mm or %");
    def->ratio_over = "line_width";
    def->min = 0;
    def->max_literal = 20;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(50, true));

    // Orca: deprecated
    def = this->add("overhang_speed_classic", coBool);
    def->label = L("Classic mode");
    def->category = L("Speed");
    def->tooltip = L("Enable this option to use classic mode");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool{ false });

    def = this->add("enable_overhang_speed", coBool);
    def->label = L("Slow down for overhang");
    def->category = L("Speed");
    def->tooltip = L("Enable this option to slow printing down for different overhang degree");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool{ true });
    
    def = this->add("slowdown_for_curled_perimeters", coBool);
    def->label = L("Slow down for curled perimeters");
    def->category = L("Speed");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Enable this option to slow down printing in areas where perimeters may have curled upwards."
                     "For example, additional slowdown will be applied when printing overhangs on sharp corners like the "
                     "front of the Benchy hull, reducing curling which compounds over multiple layers.\n\n "
                     "It is generally recommended to have this option switched on unless your printer cooling is powerful enough or the "
                     "print speed slow enough that perimeter curling does not happen. If printing with a high external perimeter speed, "
                     "this parameter may introduce slight artifacts when slowing down due to the large variance in print speeds. "
                     "If you notice artifacts, ensure your pressure advance is tuned correctly.\n\n"
                     "Note: When this option is enabled, overhang perimeters are treated like overhangs, meaning the overhang speed is "
                     "applied even if the overhanging perimeter is part of a bridge. For example, when the perimeters are 100% overhanging"
                     ", with no wall supporting them from underneath, the 100% overhang speed will be applied.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool{ true });

    def = this->add("overhang_1_4_speed", coFloatOrPercent);
    def->label = "(10%, 25%)";
    def->category = L("Speed");
    def->full_label = "(10%, 25%)";
    //def->tooltip = L("Speed for line of wall which has degree of overhang between 10% and 25% line width. "
    //                 "0 means using original wall speed");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "outer_wall_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("overhang_2_4_speed", coFloatOrPercent);
    def->label = "[25%, 50%)";
    def->category = L("Speed");
    def->full_label = "[25%, 50%)";
    //def->tooltip = L("Speed for line of wall which has degree of overhang between 25% and 50% line width. "
    //                 "0 means using original wall speed");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "outer_wall_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("overhang_3_4_speed", coFloatOrPercent);
    def->label = "[50%, 75%)";
    def->category = L("Speed");
    def->full_label = "[50%, 75%)";
    //def->tooltip = L("Speed for line of wall which has degree of overhang between 50% and 75% line width. 0 means using original wall speed");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "outer_wall_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("overhang_4_4_speed", coFloatOrPercent);
    def->label = "[75%, 100%)";
    def->category = L("Speed");
    def->full_label = "[75%, 100%)";
    //def->tooltip = L("Speed for line of wall which has degree of overhang between 75% and 100% line width. 0 means using original wall speed");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "outer_wall_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("bridge_speed", coFloat);
    def->label = L("External");
    def->category = L("Speed");
    def->tooltip = L("Speed of the externally visible bridge extrusions. "
                     "\n\nIn addition, if Slow down for curled perimeters is disabled or Classic overhang mode is enabled, "
                     "it will be the print speed of overhang walls that are supported by less than 13%, whether they are part of a bridge "
                     "or an overhang.");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(25));

    def = this->add("internal_bridge_speed", coFloatOrPercent);
    def->label = L("Internal");
    def->category = L("Speed");
    def->tooltip = L("Speed of internal bridges. If the value is expressed as a percentage, it will be calculated based on the bridge_speed. Default value is 150%.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "bridge_speed";
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(150, true));

    def = this->add("brim_width", coFloat);
    def->label = L("Brim width");
    def->category = L("Support");
    def->tooltip = L("Distance from model to the outermost brim line");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("brim_type", coEnum);
    def->label = L("Brim type");
    def->category = L("Support");
    def->tooltip = L("This controls the generation of the brim at outer and/or inner side of models. "
                     "Auto means the brim width is analyzed and calculated automatically.");
    def->enum_keys_map = &ConfigOptionEnum<BrimType>::get_enum_values();
    def->enum_values.emplace_back("auto_brim");
    def->enum_values.emplace_back("brim_ears");
    def->enum_values.emplace_back("outer_only");
    def->enum_values.emplace_back("inner_only");
    def->enum_values.emplace_back("outer_and_inner");
    def->enum_values.emplace_back("no_brim");
    def->enum_labels.emplace_back(L("Auto"));
    def->enum_labels.emplace_back(L("Mouse ear"));
    def->enum_labels.emplace_back(L("Outer brim only"));
    def->enum_labels.emplace_back(L("Inner brim only"));
    def->enum_labels.emplace_back(L("Outer and inner brim"));
    def->enum_labels.emplace_back(L("No-brim"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<BrimType>(btAutoBrim));

    def = this->add("brim_object_gap", coFloat);
    def->label = L("Brim-object gap");
    def->category = L("Support");
    def->tooltip = L("A gap between innermost brim line and object can make brim be removed more easily");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("brim_ears", coBool);
    def->label = L("Brim ears");
    def->category = L("Support");
    def->tooltip = L("Only draw brim over the sharp edges of the model.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("brim_ears_max_angle", coFloat);
    def->label = L("Brim ear max angle");
    def->category = L("Support");
    def->tooltip = L("Maximum angle to let a brim ear appear. \nIf set to 0, no brim will be created. \nIf set to "
                     "~180, brim will be created on everything but straight sections.");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 180;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(125));

    def = this->add("brim_ears_detection_length", coFloat);
    def->label = L("Brim ear detection radius");
    def->category = L("Support");
    def->tooltip = L("The geometry will be decimated before detecting sharp angles. This parameter indicates the "
                     "minimum length of the deviation for the decimation."
                     "\n0 to deactivate");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("compatible_printers", coStrings);
    def->label = L("Compatible machine");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    //BBS.
    def        = this->add("upward_compatible_machine", coStrings);
    def->label = L("upward compatible machine");
    def->mode  = comDevelop;
    def->set_default_value(new ConfigOptionStrings());
    def->cli   = ConfigOptionDef::nocli;

    def = this->add("compatible_printers_condition", coString);
    def->label = L("Compatible machine condition");
    //def->tooltip = L("A boolean expression using the configuration values of an active printer profile. "
    //               "If this expression evaluates to true, this profile is considered compatible "
    //               "with the active printer profile.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("compatible_prints", coStrings);
    def->label = L("Compatible process profiles");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("compatible_prints_condition", coString);
    def->label = L("Compatible process profiles condition");
    //def->tooltip = L("A boolean expression using the configuration values of an active print profile. "
    //               "If this expression evaluates to true, this profile is considered compatible "
    //               "with the active print profile.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    // The following value is to be stored into the project file (AMF, 3MF, Config ...)
    // and it contains a sum of "compatible_printers_condition" values over the print and filament profiles.
    def = this->add("compatible_machine_expression_group", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;
    def = this->add("compatible_process_expression_group", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    //BBS: add logic for checking between different system presets
    def = this->add("different_settings_to_system", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("print_compatible_printers", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("print_sequence", coEnum);
    def->label = L("Print sequence");
    def->tooltip = L("Print sequence, layer by layer or object by object");
    def->enum_keys_map = &ConfigOptionEnum<PrintSequence>::get_enum_values();
    def->enum_values.push_back("by layer");
    def->enum_values.push_back("by object");
    def->enum_labels.push_back(L("By layer"));
    def->enum_labels.push_back(L("By object"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<PrintSequence>(PrintSequence::ByLayer));

    def = this->add("print_order", coEnum);
    def->label = L("Intra-layer order");
    def->tooltip = L("Print order within a single layer");
    def->enum_keys_map = &ConfigOptionEnum<PrintOrder>::get_enum_values();
    def->enum_values.push_back("default");
    def->enum_values.push_back("as_obj_list");
    def->enum_labels.push_back(L("Default"));
    def->enum_labels.push_back(L("As object list"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<PrintOrder>(PrintOrder::Default));

    def = this->add("slow_down_for_layer_cooling", coBools);
    def->label = L("Slow printing down for better layer cooling");
    def->tooltip = L("Enable this option to slow printing speed down to make the final layer time not shorter than "
                     "the layer time threshold in \"Max fan speed threshold\", so that layer can be cooled for longer time. "
                     "This can improve the cooling quality for needle and small details");
    def->set_default_value(new ConfigOptionBools { true });

    def = this->add("default_acceleration", coFloat);
    def->label = L("Normal printing");
    def->tooltip = L("The default acceleration of both normal printing and travel except initial layer");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(500.0));

    def = this->add("default_filament_profile", coStrings);
    def->label = L("Default filament profile");
    def->tooltip = L("Default filament profile when switch to this machine profile");
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("default_print_profile", coString);
    def->label = L("Default process profile");
    def->tooltip = L("Default process profile when switch to this machine profile");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("activate_air_filtration",coBools);
    def->label = L("Activate air filtration");
    def->tooltip = L("Activate for better air filtration. G-code command: M106 P3 S(0-255)");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBools{false});

    def = this->add("during_print_exhaust_fan_speed", coInts);
    def->label   = L("Fan speed");
    def->tooltip=L("Speed of exhaust fan during printing.This speed will overwrite the speed in filament custom gcode");
    def->sidetext = L("%");
    def->min=0;
    def->max=100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts{60});

    def = this->add("complete_print_exhaust_fan_speed", coInts);
    def->label = L("Fan speed");
    def->sidetext = L("%");
    def->tooltip=L("Speed of exhaust fan after printing completes");
    def->min=0;
    def->max=100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts{80});

    def = this->add("close_fan_the_first_x_layers", coInts);
    def->label = L("No cooling for the first");
    def->tooltip = L("Close all cooling fan for the first certain layers. Cooling fan of the first layer used to be closed "
                     "to get better build plate adhesion");
    def->sidetext = L("layers");
    def->min = 0;
    def->max = 1000;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts { 1 });

    def = this->add("bridge_no_support", coBool);
    def->label = L("Don't support bridges");
    def->category = L("Support");
    def->tooltip = L("Don't support the whole bridge area which make support very large. "
                     "Bridge usually can be printing directly without support if not very long");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("thick_bridges", coBool);
    def->label = L("Thick bridges");
    def->category = L("Quality");
    def->tooltip = L("If enabled, bridges are more reliable, can bridge longer distances, but may look worse. "
        "If disabled, bridges look better but are reliable just for shorter bridged distances.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("thick_internal_bridges", coBool);
    def->label = L("Thick internal bridges");
    def->category = L("Quality");
    def->tooltip  = L("If enabled, thick internal bridges will be used. It's usually recommended to have this feature turned on. However, "
                       "consider turning it off if you are using large nozzles.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("dont_filter_internal_bridges", coEnum);
    def->label = L("Filter out small internal bridges (beta)");
    def->category = L("Quality");
    def->tooltip = L("This option can help reducing pillowing on top surfaces in heavily slanted or curved models.\n\n"
                      "By default, small internal bridges are filtered out and the internal solid infill is printed directly"
                      " over the sparse infill. This works well in most cases, speeding up printing without too much compromise"
                      " on top surface quality. \n\nHowever, in heavily slanted or curved models especially where too low sparse"
                     " infill density is used, this may result in curling of the unsupported solid infill, causing pillowing.\n\n"
                      "Disabling this option will print internal bridge layer over slightly unsupported internal"
                      " solid infill. The options below control the amount of filtering, i.e. the amount of internal bridges "
                     "created.\n\n"
                     "Filter - enable this option. This is the default behavior and works well in most cases.\n\n"
                     "Limited filtering - creates internal bridges on heavily slanted surfaces, while avoiding creating "
                     "unnecessary internal bridges. This works well for most difficult models.\n\n"
                     "No filtering - creates internal bridges on every potential internal overhang. This option is useful "
                     "for heavily slanted top surface models. However, in most cases it creates too many unnecessary bridges.");
    def->enum_keys_map = &ConfigOptionEnum<InternalBridgeFilter>::get_enum_values();
    def->enum_values.push_back("disabled");
    def->enum_values.push_back("limited");
    def->enum_values.push_back("nofilter");
    def->enum_labels.push_back(L("Filter"));
    def->enum_labels.push_back(L("Limited filtering"));
    def->enum_labels.push_back(L("No filtering"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<InternalBridgeFilter>(ibfDisabled));


    def = this->add("max_bridge_length", coFloat);
    def->label = L("Max bridge length");
    def->category = L("Support");
    def->tooltip = L("Max length of bridges that don't need support. Set it to 0 if you want all bridges to be supported, and set it to a very large value if you don't want any bridges to be supported.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10));

    def = this->add("machine_end_gcode", coString);
    def->label = L("End G-code");
    def->tooltip = L("End G-code when finish the whole printing");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString("M104 S0 ; turn off temperature\nG28 X0  ; home X axis\nM84     ; disable motors\n"));

    def             = this->add("printing_by_object_gcode", coString);
    def->label      = L("Between Object Gcode");
    def->tooltip    = L("Insert Gcode between objects. This parameter will only come into effect when you print your models object by object");
    def->multiline  = true;
    def->full_width = true;
    def->height     = 12;
    def->mode       = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("filament_end_gcode", coStrings);
    def->label = L("End G-code");
    def->tooltip = L("End G-code when finish the printing of this filament");
    def->multiline = true;
    def->full_width = true;
    def->height = 120;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings { " " });

    def = this->add("ensure_vertical_shell_thickness", coEnum);
    def->label = L("Ensure vertical shell thickness");
    def->category = L("Strength");
    def->tooltip  = L(
        "Add solid infill near sloping surfaces to guarantee the vertical shell thickness (top+bottom solid layers)\nNone: No solid infill "
         "will be added anywhere. Caution: Use this option carefully if your model has sloped surfaces\nCritical Only: Avoid adding solid infill for walls\nModerate: Add solid infill for heavily "
         "sloping surfaces only\nAll: Add solid infill for all suitable sloping surfaces\nDefault value is All.");
    def->enum_keys_map = &ConfigOptionEnum<EnsureVerticalShellThickness>::get_enum_values();
    def->enum_values.push_back("none");
    def->enum_values.push_back("ensure_critical_only");
    def->enum_values.push_back("ensure_moderate");
    def->enum_values.push_back("ensure_all");
    def->enum_labels.push_back(L("None"));
    def->enum_labels.push_back(L("Critical Only"));
    def->enum_labels.push_back(L("Moderate"));
    def->enum_labels.push_back(L("All"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<EnsureVerticalShellThickness>(EnsureVerticalShellThickness::evstAll));
    
    auto def_top_fill_pattern = def = this->add("top_surface_pattern", coEnum);
    def->label = L("Top surface pattern");
    def->category = L("Strength");
    def->tooltip = L("Line pattern of top surface infill");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("zig-zag");
    def->enum_values.push_back("monotonic");
    def->enum_values.push_back("monotonicline");
    def->enum_values.push_back("alignedrectilinear");
    def->enum_values.push_back("hilbertcurve");
    def->enum_values.push_back("archimedeanchords");
    def->enum_values.push_back("octagramspiral");
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Monotonic"));
    def->enum_labels.push_back(L("Monotonic line"));
    def->enum_labels.push_back(L("Aligned Rectilinear"));
    def->enum_labels.push_back(L("Hilbert Curve"));
    def->enum_labels.push_back(L("Archimedean Chords"));
    def->enum_labels.push_back(L("Octagram Spiral"));
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipMonotonicLine));

    def = this->add("bottom_surface_pattern", coEnum);
    def->label = L("Bottom surface pattern");
    def->category = L("Strength");
    def->tooltip = L("Line pattern of bottom surface infill, not bridge infill");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values = def_top_fill_pattern->enum_values;
    def->enum_labels = def_top_fill_pattern->enum_labels;
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipMonotonic));

	def                = this->add("internal_solid_infill_pattern", coEnum);
    def->label         = L("Internal solid infill pattern");
    def->category      = L("Strength");
    def->tooltip       = L("Line pattern of internal solid infill. if the detect narrow internal solid infill be enabled, the concentric pattern will be used for the small area.");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values   = def_top_fill_pattern->enum_values;
    def->enum_labels   = def_top_fill_pattern->enum_labels;
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipMonotonic));
    
    def = this->add("outer_wall_line_width", coFloatOrPercent);
    def->label = L("Outer wall");
    def->category = L("Quality");
    def->tooltip = L("Line width of outer wall. If expressed as a %, it will be computed over the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    def = this->add("outer_wall_speed", coFloat);
    def->label = L("Outer wall");
    def->category = L("Speed");
    def->tooltip = L("Speed of outer wall which is outermost and visible. "
                     "It's used to be slower than inner wall speed to get better quality.");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(60));

    def = this->add("small_perimeter_speed", coFloatOrPercent);
    def->label = L("Small perimeters");
    def->category = L("Speed");
    def->tooltip = L("This separate setting will affect the speed of perimeters having radius <= small_perimeter_threshold "
                   "(usually holes). If expressed as percentage (for example: 80%) it will be calculated "
                   "on the outer wall speed setting above. Set to zero for auto.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "outer_wall_speed";
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(50, true));

    def = this->add("small_perimeter_threshold", coFloat);
    def->label = L("Small perimeters threshold");
    def->category = L("Speed");
    def->tooltip = L("This sets the threshold for small perimeter length. Default threshold is 0mm");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("wall_sequence", coEnum);
    def->label = L("Walls printing order");
    def->category = L("Quality");
    def->tooltip = L("Print sequence of the internal (inner) and external (outer) walls. \n\nUse Inner/Outer for best overhangs. This is because the overhanging walls can adhere to a neighbouring perimeter while printing. However, this option results in slightly reduced surface quality as the external perimeter is deformed by being squashed to the internal perimeter.\n\nUse Inner/Outer/Inner for the best external surface finish and dimensional accuracy as the external wall is printed undisturbed from an internal perimeter. However, overhang performance will reduce as there is no internal perimeter to print the external wall against. This option requires a minimum of 3 walls to be effective as it prints the internal walls from the 3rd perimeter onwards first, then the external perimeter and, finally, the first internal perimeter. This option is recommended against the Outer/Inner option in most cases. \n\nUse Outer/Inner for the same external wall quality and dimensional accuracy benefits of Inner/Outer/Inner option. However, the z seams will appear less consistent as the first extrusion of a new layer starts on a visible surface.\n\n ");
    def->enum_keys_map = &ConfigOptionEnum<WallSequence>::get_enum_values();
    def->enum_values.push_back("inner wall/outer wall");
    def->enum_values.push_back("outer wall/inner wall");
    def->enum_values.push_back("inner-outer-inner wall");
    def->enum_labels.push_back(L("Inner/Outer"));
    def->enum_labels.push_back(L("Outer/Inner"));
    def->enum_labels.push_back(L("Inner/Outer/Inner"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<WallSequence>(WallSequence::InnerOuter));

    def = this->add("is_infill_first",coBool);
    def->label    = L("Print infill first");
    def->tooltip  = L("Order of wall/infill. When the tickbox is unchecked the walls are printed first, which works best in most cases.\n\nPrinting infill first may help with extreme overhangs as the walls have the neighbouring infill to adhere to. However, the infill will slightly push out the printed walls where it is attached to them, resulting in a worse external surface finish. It can also cause the infill to shine through the external surfaces of the part.");
    def->category = L("Quality");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool{false});

    def = this->add("wall_direction", coEnum);
    def->label = L("Wall loop direction");
    def->category = L("Quality");
    def->tooltip = L("The direction which the wall loops are extruded when looking down from the top.\n\nBy default all walls are extruded in counter-clockwise, unless Reverse on even is enabled. Set this to any option other than Auto will force the wall direction regardless of the Reverse on even.\n\nThis option will be disabled if spiral vase mode is enabled.");
    def->enum_keys_map = &ConfigOptionEnum<WallDirection>::get_enum_values();
    def->enum_values.push_back("auto");
    def->enum_values.push_back("ccw");
    def->enum_values.push_back("cw");
    def->enum_labels.push_back(L("Auto"));
    def->enum_labels.push_back(L("Counter clockwise"));
    def->enum_labels.push_back(L("Clockwise"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<WallDirection>(WallDirection::Auto));

    def = this->add("extruder", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Extruder");
    def->category = L("Extruders");
    //def->tooltip = L("The extruder to use (unless more specific extruder settings are specified). "
    //               "This value overrides perimeter and infill extruders, but not the support extruders.");
    def->min = 0;  // 0 = inherit defaults
    def->enum_labels.push_back(L("default"));  // override label for item 0
    def->enum_labels.push_back("1");
    def->enum_labels.push_back("2");
    def->enum_labels.push_back("3");
    def->enum_labels.push_back("4");
    def->enum_labels.push_back("5");
    def->mode = comAdvanced;

    def = this->add("extruder_clearance_height_to_rod", coFloat);
    def->label = L("Height to rod");
    def->tooltip = L("Distance of the nozzle tip to the lower rod. "
        "Used for collision avoidance in by-object printing.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(40));

    // BBS
    def = this->add("extruder_clearance_height_to_lid", coFloat);
    def->label = L("Height to lid");
    def->tooltip = L("Distance of the nozzle tip to the lid. "
        "Used for collision avoidance in by-object printing.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(120));

    def = this->add("extruder_clearance_radius", coFloat);
    def->label = L("Radius");
    def->tooltip = L("Clearance radius around extruder. Used for collision avoidance in by-object printing.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(40));

    def = this->add("nozzle_height", coFloat);
    def->label = L("Nozzle height");
    def->tooltip = L("The height of nozzle tip.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloat(2.5));

    def          = this->add("bed_mesh_min", coPoint);
    def->label   = L("Bed mesh min");
    def->tooltip = L(
        "This option sets the min point for the allowed bed mesh area. Due to the probe's XY offset, most printers are unable to probe the "
        "entire bed. To ensure the probe point does not go outside the bed area, the minimum and maximum points of the bed mesh should be "
        "set appropriately. OrcaSlicer ensures that adaptive_bed_mesh_min/adaptive_bed_mesh_max values do not exceed these min/max "
        "points. This information can usually be obtained from your printer manufacturer. The default setting is (-99999, -99999), which "
        "means there are no limits, thus allowing probing across the entire bed.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPoint(Vec2d(-99999, -99999)));

    def          = this->add("bed_mesh_max", coPoint);
    def->label   = L("Bed mesh max");
    def->tooltip = L(
        "This option sets the max point for the allowed bed mesh area. Due to the probe's XY offset, most printers are unable to probe the "
        "entire bed. To ensure the probe point does not go outside the bed area, the minimum and maximum points of the bed mesh should be "
        "set appropriately. OrcaSlicer ensures that adaptive_bed_mesh_min/adaptive_bed_mesh_max values do not exceed these min/max "
        "points. This information can usually be obtained from your printer manufacturer. The default setting is (99999, 99999), which "
        "means there are no limits, thus allowing probing across the entire bed.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPoint(Vec2d(99999, 99999)));

    def          = this->add("bed_mesh_probe_distance", coPoint);
    def->label   = L("Probe point distance");
    def->tooltip = L("This option sets the preferred distance between probe points (grid size) for the X and Y directions, with the "
                     "default being 50mm for both X and Y.");
    def->min     = 0;
    def->sidetext = L("mm");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionPoint(Vec2d(50, 50)));

    def          = this->add("adaptive_bed_mesh_margin", coFloat);
    def->label   = L("Mesh margin");
    def->tooltip = L("This option determines the additional distance by which the adaptive bed mesh area should be expanded in the XY directions.");
    def->sidetext = L("mm"); // ORCA add side text
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("extruder_colour", coStrings);
    def->label = L("Extruder Color");
    def->tooltip = L("Only used as a visual help on UI");
    def->gui_type = ConfigOptionDef::GUIType::color;
    // Empty string means no color assigned yet.
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings { "" });

    def = this->add("extruder_offset", coPoints);
    def->label = L("Extruder offset");
    //def->tooltip = L("If your firmware doesn't handle the extruder displacement you need the G-code "
    //               "to take it into account. This option lets you specify the displacement of each extruder "
    //               "with respect to the first one. It expects positive coordinates (they will be subtracted "
    //               "from the XY coordinate).");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPoints { Vec2d(0,0) });

    def = this->add("filament_flow_ratio", coFloats);
    def->label = L("Flow ratio");
    def->tooltip = L("The material may have volumetric change after switching between molten state and crystalline state. "
                     "This setting changes all extrusion flow of this filament in gcode proportionally. "
                     "Recommended value range is between 0.95 and 1.05. "
                     "Maybe you can tune this value to get nice flat surface when there has slight overflow or underflow");
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 1. });

    def = this->add("print_flow_ratio", coFloat);
    def->label = L("Flow ratio");
    def->tooltip = L("The material may have volumetric change after switching between molten state and crystalline state. "
                     "This setting changes all extrusion flow of this filament in gcode proportionally. "
                     "Recommended value range is between 0.95 and 1.05. "
                     "Maybe you can tune this value to get nice flat surface when there has slight overflow or underflow. "
                     "\n\nThe final object flow ratio is this value multiplied by the filament flow ratio.");
    def->mode = comAdvanced;
    def->max = 2;
    def->min = 0.01;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("enable_pressure_advance", coBools);
    def->label = L("Enable pressure advance");
    def->tooltip = L("Enable pressure advance, auto calibration result will be overwritten once enabled.");
    def->set_default_value(new ConfigOptionBools{ false });

    def = this->add("pressure_advance", coFloats);
    def->label = L("Pressure advance");
    def->tooltip = L("Pressure advance(Klipper) AKA Linear advance factor(Marlin)");
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0.02 });
    
    // Orca: Adaptive pressure advance option and calibration values
    def = this->add("adaptive_pressure_advance", coBools);
    def->label = L("Enable adaptive pressure advance (beta)");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("With increasing print speeds (and hence increasing volumetric flow through the nozzle) and increasing accelerations, "
                     "it has been observed that the effective PA value typically decreases. "
                     "This means that a single PA value is not always 100% optimal for all features and a compromise value is usually used "
                     "that does not cause too much bulging on features with lower flow speed and accelerations while also not causing gaps on faster features.\n\n"
                     "This feature aims to address this limitation by modeling the response of your printer's extrusion system depending "
                     "on the volumetric flow speed and acceleration it is printing at. Internally, it generates a fitted model that can extrapolate the needed pressure "
                     "advance for any given volumetric flow speed and acceleration, which is then emitted to the printer depending on the current print conditions.\n\n"
                     "When enabled, the pressure advance value above is overridden. However, a reasonable default value above is "
                     "strongly recommended to act as a fallback and for when tool changing.\n\n");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools{ false });
    
    // Orca: Adaptive pressure advance option and calibration values
    def = this->add("adaptive_pressure_advance_model", coStrings);
    def->label = L("Adaptive pressure advance measurements (beta)");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Add sets of pressure advance (PA) values, the volumetric flow speeds and accelerations they were measured at, separated by a comma. "
                     "One set of values per line. For example\n"
                     "0.04,3.96,3000\n0.033,3.96,10000\n0.029,7.91,3000\n0.026,7.91,10000\n\n"
                     "How to calibrate:\n"
                     "1. Run the pressure advance test for at least 3 speeds per acceleration value. It is recommended that the test is run "
                     "for at least the speed of the external perimeters, the speed of the internal perimeters and the fastest feature "
                     "print speed in your profile (usually its the sparse or solid infill). Then run them for the same speeds for the slowest and fastest print accelerations,"
                     "and no faster than the recommended maximum acceleration as given by the Klipper input shaper.\n"
                     "2. Take note of the optimal PA value for each volumetric flow speed and acceleration. You can find the flow number by selecting "
                     "flow from the color scheme drop down and move the horizontal slider over the PA pattern lines. The number should be visible "
                     "at the bottom of the page. The ideal PA value should be decreasing the higher the volumetric flow is. If it is not, confirm that your extruder is functioning correctly."
                     "The slower and with less acceleration you print, the larger the range of acceptable PA values. If no difference is visible, use the PA value from the faster test."
                     "3. Enter the triplets of PA values, Flow and Accelerations in the text box here and save your filament profile\n\n"
                     "");
    def->mode = comAdvanced;
    //def->gui_flags = "serialized";
    def->multiline = true;
    def->full_width = true;
    def->height = 15;
    def->set_default_value(new ConfigOptionStrings{"0,0,0\n0,0,0"});
    
    // xgettext:no-c-format, no-boost-format
    def = this->add("adaptive_pressure_advance_overhangs", coBools);
    def->label = L("Enable adaptive pressure advance for overhangs (beta)");
    def->tooltip = L("Enable adaptive PA for overhangs as well as when flow changes within the same feature. This is an experimental option, "
                     "as if the PA profile is not set accurately, it will cause uniformity issues on the external surfaces before and after overhangs.\n");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools{ false });
    
    def = this->add("adaptive_pressure_advance_bridges", coFloats);
    def->label = L("Pressure advance for bridges");
    def->tooltip = L("Pressure advance value for bridges. Set to 0 to disable. \n\n A lower PA value when printing bridges helps reduce the appearance of slight under extrusion "
                     "immediately after bridges. This is caused by the pressure drop in the nozzle when printing in the air and a lower PA helps counteract this.");
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0.0 });

    def = this->add("line_width", coFloatOrPercent);
    def->label = L("Default");
    def->category = L("Quality");
    def->tooltip = L("Default line width if other line widths are set to 0. If expressed as a %, it will be computed over the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("reduce_fan_stop_start_freq", coBools);
    def->label = L("Keep fan always on");
    def->tooltip = L("If enable this setting, part cooling fan will never be stopped and will run at least "
                     "at minimum speed to reduce the frequency of starting and stopping");
    def->set_default_value(new ConfigOptionBools { false });
    
    def = this->add("dont_slow_down_outer_wall", coBools);
    def->label = L("Don't slow down outer walls");
    def->tooltip = L("If enabled, this setting will ensure external perimeters are not slowed down to meet the minimum layer time. "
                     "This is particularly helpful in the below scenarios:\n\n "
                     "1. To avoid changes in shine when printing glossy filaments \n"
                     "2. To avoid changes in external wall speed which may create slight wall artifacts that appear like z banding \n"
                     "3. To avoid printing at speeds which cause VFAs (fine artifacts) on the external walls\n\n");
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("fan_cooling_layer_time", coFloats);
    def->label = L("Layer time");
    def->tooltip = L("Part cooling fan will be enabled for layers of which estimated time is shorter than this value. "
                     "Fan speed is interpolated between the minimum and maximum fan speeds according to layer printing time");
    def->sidetext = L("s");
    def->min = 0;
    def->max = 1000;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats{ 60.0f });

    def           = this->add("default_filament_colour", coStrings);
    def->label    = L("Default color");
    def->tooltip  = L("Default filament color");
    def->gui_type = ConfigOptionDef::GUIType::color;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionStrings{""});

    def = this->add("filament_colour", coStrings);
    def->label = L("Color");
    def->tooltip = L("Only used as a visual help on UI");
    def->gui_type = ConfigOptionDef::GUIType::color;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings{ "#F2754E" });

    // PS
    def = this->add("filament_notes", coStrings);
    def->label = L("Filament notes");
    def->tooltip = L("You can put your notes regarding the filament here.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings { "" });

    //bbs
    def          = this->add("required_nozzle_HRC", coInts);
    def->label   = L("Required nozzle HRC");
    def->tooltip = L("Minimum HRC of nozzle required to print the filament. Zero means no checking of nozzle's HRC.");
    def->min     = 0;
    def->max     = 500;
    def->mode    = comDevelop;
    def->set_default_value(new ConfigOptionInts{0});

    def = this->add("filament_max_volumetric_speed", coFloats);
    def->label = L("Max volumetric speed");
    def->tooltip = L("This setting stands for how much volume of filament can be melted and extruded per second. "
                     "Printing speed is limited by max volumetric speed, in case of too high and unreasonable speed setting. "
                     "Can't be zero");
    def->sidetext = L("mm³/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 2. });

    def = this->add("machine_load_filament_time", coFloat);
    def->label = L("Filament load time");
    def->tooltip = L("Time to load new filament when switch filament. It's usually applicable for single-extruder multi-material machines. For tool changers or multi-tool machines, it's typically 0. For statistics only");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("machine_unload_filament_time", coFloat);
    def->label = L("Filament unload time");
    def->tooltip = L("Time to unload old filament when switch filament. It's usually applicable for single-extruder multi-material machines. For tool changers or multi-tool machines, it's typically 0. For statistics only");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("machine_tool_change_time", coFloat);
    def->label = L("Tool change time");
    def->tooltip = L("Time taken to switch tools. It's usually applicable for tool changers or multi-tool machines. For single-extruder multi-material machines, it's typically 0. For statistics only");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat { 0. });


    def = this->add("filament_diameter", coFloats);
    def->label = L("Diameter");
    def->tooltip = L("Filament diameter is used to calculate extrusion in gcode, so it's important and should be accurate");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloats { 1.75 });

    /*
        Large format printers with print volumes in the order of 1m^3 generally use pellets for printing.
        The overall tech is very similar to FDM printing. 
        It is FDM printing, but instead of filaments, it uses pellets.

        The difference here is that where filaments have a filament_diameter that is used to calculate 
        the volume of filament ingested, pellets have a particular flow_coefficient that is empirically 
        devised for that particular pellet.

        pellet_flow_coefficient is basically a measure of the packing density of a particular pellet.
        Shape, material and density of an individual pellet will determine the packing density and
        the only thing that matters for 3d printing is how much of that pellet material is extruded by 
        one turn of whatever feeding mehcanism/gear your printer uses. You can emperically derive that
        for your own pellets for a particular printer model.

        We are translating the pellet_flow_coefficient into filament_diameter so that everything works just like it 
        does already with very minor adjustments.

        filament_diameter = sqrt( (4 * pellet_flow_coefficient) / PI )

        sqrt just makes the relationship between flow_coefficient and volume linear.

        higher packing density -> more material extruded by single turn -> higher pellet_flow_coefficient -> treated as if a filament of larger diameter is being used
        All other calculations remain the same for slicing.
    */

    def = this->add("pellet_flow_coefficient", coFloats);
    def->label = L("Pellet flow coefficient");
    def->tooltip = L("Pellet flow coefficient is empirically derived and allows for volume calculation for pellet printers.\n\nInternally it is converted to filament_diameter. All other volume calculations remain the same.\n\nfilament_diameter = sqrt( (4 * pellet_flow_coefficient) / PI )");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloats{ 0.4157 });

    def = this->add("filament_shrink", coPercents);
    def->label = L("Shrinkage (XY)");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Enter the shrinkage percentage that the filament will get after cooling (94% if you measure 94mm instead of 100mm)."
        " The part will be scaled in xy to compensate."
        " Only the filament used for the perimeter is taken into account."
        "\nBe sure to allow enough space between objects, as this compensation is done after the checks.");
    def->sidetext = L("%");
    def->ratio_over = "";
    def->min = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercents{ 100 });
    
    def = this->add("filament_shrinkage_compensation_z", coPercents);
    def->label = L("Shrinkage (Z)");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Enter the shrinkage percentage that the filament will get after cooling (94% if you measure 94mm instead of 100mm)."
        " The part will be scaled in Z to compensate.");
    def->sidetext = L("%");
    def->ratio_over = "";
    def->min = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercents{ 100 });

    def = this->add("filament_loading_speed", coFloats);
    def->label = L("Loading speed");
    def->tooltip = L("Speed used for loading the filament on the wipe tower.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 28. });

    def = this->add("filament_loading_speed_start", coFloats);
    def->label = L("Loading speed at the start");
    def->tooltip = L("Speed used at the very beginning of loading phase.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 3. });

    def = this->add("filament_unloading_speed", coFloats);
    def->label = L("Unloading speed");
    def->tooltip = L("Speed used for unloading the filament on the wipe tower (does not affect "
                      " initial part of unloading just after ramming).");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 90. });

    def = this->add("filament_unloading_speed_start", coFloats);
    def->label = L("Unloading speed at the start");
    def->tooltip = L("Speed used for unloading the tip of the filament immediately after ramming.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 100. });

    def = this->add("filament_toolchange_delay", coFloats);
    def->label = L("Delay after unloading");
    def->tooltip = L("Time to wait after the filament is unloaded. "
                   "May help to get reliable toolchanges with flexible materials "
                   "that may need more time to shrink to original dimensions.");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_cooling_moves", coInts);
    def->label = L("Number of cooling moves");
    def->tooltip = L("Filament is cooled by being moved back and forth in the "
                   "cooling tubes. Specify desired number of these moves.");
    def->max = 0;
    def->max = 20;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts { 4 });

    def = this->add("filament_stamping_loading_speed", coFloats);
    def->label = L("Stamping loading speed");
    def->tooltip = L("Speed used for stamping.");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_stamping_distance", coFloats);
    def->label = L("Stamping distance measured from the center of the cooling tube");
    def->tooltip = L("If set to nonzero value, filament is moved toward the nozzle between the individual cooling moves (\"stamping\"). "
                     "This option configures how long this movement should be before the filament is retracted again.");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_cooling_initial_speed", coFloats);
    def->label = L("Speed of the first cooling move");
    def->tooltip = L("Cooling moves are gradually accelerating beginning at this speed.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 2.2 });

    def = this->add("filament_minimal_purge_on_wipe_tower", coFloats);
    def->label = L("Minimal purge on wipe tower");
    def->tooltip = L("After a tool change, the exact position of the newly loaded filament inside "
                     "the nozzle may not be known, and the filament pressure is likely not yet stable. "
                     "Before purging the print head into an infill or a sacrificial object, Orca Slicer will always prime "
                     "this amount of material into the wipe tower to produce successive infill or sacrificial object extrusions reliably.");
    def->sidetext = L("mm³");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 15. });

    def = this->add("filament_cooling_final_speed", coFloats);
    def->label = L("Speed of the last cooling move");
    def->tooltip = L("Cooling moves are gradually accelerating towards this speed.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 3.4 });

    def = this->add("filament_ramming_parameters", coStrings);
    def->label = L("Ramming parameters");
    def->tooltip = L("This string is edited by RammingDialog and contains ramming specific parameters.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings { "120 100 6.6 6.8 7.2 7.6 7.9 8.2 8.7 9.4 9.9 10.0|"
       " 0.05 6.6 0.45 6.8 0.95 7.8 1.45 8.3 1.95 9.7 2.45 10 2.95 7.6 3.45 7.6 3.95 7.6 4.45 7.6 4.95 7.6" });

    def = this->add("filament_multitool_ramming", coBools);
    def->label = L("Enable ramming for multi-tool setups");
    def->tooltip = L("Perform ramming when using multi-tool printer (i.e. when the 'Single Extruder Multimaterial' in Printer Settings is unchecked). "
                     "When checked, a small amount of filament is rapidly extruded on the wipe tower just before the toolchange. "
                     "This option is only used when the wipe tower is enabled.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("filament_multitool_ramming_volume", coFloats);
    def->label = L("Multi-tool ramming volume");
    def->tooltip = L("The volume to be rammed before the toolchange.");
    def->sidetext = L("mm³");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 10. });

    def = this->add("filament_multitool_ramming_flow", coFloats);
    def->label = L("Multi-tool ramming flow");
    def->tooltip = L("Flow used for ramming the filament before the toolchange.");
    def->sidetext = L("mm³/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 10. });
    
    def = this->add("filament_density", coFloats);
    def->label = L("Density");
    def->tooltip = L("Filament density. For statistics only");
    def->sidetext = L("g/cm³");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_type", coStrings);
    def->label = L("Type");
    def->tooltip = L("The material type of filament");
    def->gui_type = ConfigOptionDef::GUIType::f_enum_open;
    def->gui_flags = "show_value";

    def->enum_values.push_back("ABS");
    def->enum_values.push_back("ABS-GF");
    def->enum_values.push_back("ASA");
    def->enum_values.push_back("ASA-Aero");
    def->enum_values.push_back("BVOH");
    def->enum_values.push_back("PCTG");
    def->enum_values.push_back("EVA");
    def->enum_values.push_back("HIPS");
    def->enum_values.push_back("PA");
    def->enum_values.push_back("PA-CF");
    def->enum_values.push_back("PA-GF");
    def->enum_values.push_back("PA6-CF");
    def->enum_values.push_back("PA11-CF");
    def->enum_values.push_back("PC");
    def->enum_values.push_back("PC-CF");
    def->enum_values.push_back("PCTG");
    def->enum_values.push_back("PE");
    def->enum_values.push_back("PE-CF");
    def->enum_values.push_back("PET-CF");
    def->enum_values.push_back("PETG");
    def->enum_values.push_back("PETG-CF");
    def->enum_values.push_back("PETG-CF10");
    def->enum_values.push_back("PHA");
    def->enum_values.push_back("PLA");
    def->enum_values.push_back("PLA-AERO");
    def->enum_values.push_back("PLA-CF");
    def->enum_values.push_back("PP");
    def->enum_values.push_back("PP-CF");
    def->enum_values.push_back("PP-GF");
    def->enum_values.push_back("PPA-CF");
    def->enum_values.push_back("PPA-GF");
    def->enum_values.push_back("PPS");
    def->enum_values.push_back("PPS-CF");
    def->enum_values.push_back("PVA");
    def->enum_values.push_back("PVB");
    def->enum_values.push_back("SBS");
    def->enum_values.push_back("TPU");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionStrings { "PLA" });

    def = this->add("filament_soluble", coBools);
    def->label = L("Soluble material");
    def->tooltip = L("Soluble material is commonly used to print support and support interface");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("filament_is_support", coBools);
    def->label = L("Support material");
    def->tooltip = L("Support material is commonly used to print support and support interface");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    // BBS
    def = this->add("temperature_vitrification", coInts);
    def->label = L("Softening temperature");
    def->tooltip = L("The material softens at this temperature, so when the bed temperature is equal to or greater than it, it's highly recommended to open the front door and/or remove the upper glass to avoid clogging.");
    def->sidetext = L("°C"); // ORCA add side text
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts{ 100 });

    def = this->add("filament_cost", coFloats);
    def->label = L("Price");
    def->tooltip = L("Filament price. For statistics only");
    def->sidetext = L("money/kg");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_settings_id", coStrings);
    def->set_default_value(new ConfigOptionStrings { "" });
    //BBS: open this option to command line
    //def->cli = ConfigOptionDef::nocli;

    def = this->add("filament_ids", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("filament_vendor", coStrings);
    def->label = L("Vendor");
    def->tooltip = L("Vendor of filament. For show only");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings{L("(Undefined)")});
    def->cli = ConfigOptionDef::nocli;

    def = this->add("infill_direction", coFloat);
    def->label = L("Sparse infill direction");
    def->category = L("Strength");
    def->tooltip = L("Angle for sparse infill pattern, which controls the start or main direction of line");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 360;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(45));

    def = this->add("solid_infill_direction", coFloat);
    def->label = L("Solid infill direction");
    def->category = L("Strength");
    def->tooltip = L("Angle for solid infill pattern, which controls the start or main direction of line");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 360;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(45));

    def = this->add("rotate_solid_infill_direction", coBool);
    def->label = L("Rotate solid infill direction");
    def->category = L("Strength");
    def->tooltip = L("Rotate the solid infill direction by 90° for each layer.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("sparse_infill_density", coPercent);
    def->label = L("Sparse infill density");
    def->category = L("Strength");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Density of internal sparse infill, 100% turns all sparse infill into solid infill and internal solid infill pattern will be used");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->set_default_value(new ConfigOptionPercent(20));

    def = this->add("sparse_infill_pattern", coEnum);
    def->label = L("Sparse infill pattern");
    def->category = L("Strength");
    def->tooltip = L("Line pattern for internal sparse infill");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("zig-zag");
    def->enum_values.push_back("grid");
    def->enum_values.push_back("line");
    def->enum_values.push_back("cubic");
    def->enum_values.push_back("triangles");
    def->enum_values.push_back("tri-hexagon");
    def->enum_values.push_back("gyroid");
    def->enum_values.push_back("honeycomb");
    def->enum_values.push_back("adaptivecubic");
    def->enum_values.push_back("alignedrectilinear");
    def->enum_values.push_back("3dhoneycomb");
    def->enum_values.push_back("hilbertcurve");
    def->enum_values.push_back("archimedeanchords");
    def->enum_values.push_back("octagramspiral");
    def->enum_values.push_back("supportcubic");
    def->enum_values.push_back("lightning");
    def->enum_values.push_back("crosshatch");
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Grid"));
    def->enum_labels.push_back(L("Line"));
    def->enum_labels.push_back(L("Cubic"));
    def->enum_labels.push_back(L("Triangles"));
    def->enum_labels.push_back(L("Tri-hexagon"));
    def->enum_labels.push_back(L("Gyroid"));
    def->enum_labels.push_back(L("Honeycomb"));
    def->enum_labels.push_back(L("Adaptive Cubic"));
    def->enum_labels.push_back(L("Aligned Rectilinear"));
    def->enum_labels.push_back(L("3D Honeycomb"));
    def->enum_labels.push_back(L("Hilbert Curve"));
    def->enum_labels.push_back(L("Archimedean Chords"));
    def->enum_labels.push_back(L("Octagram Spiral"));
    def->enum_labels.push_back(L("Support Cubic"));
    def->enum_labels.push_back(L("Lightning"));
    def->enum_labels.push_back(L("Cross Hatch"));
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipCrossHatch));

    auto def_infill_anchor_min = def = this->add("infill_anchor", coFloatOrPercent);
    def->label = L("Sparse infill anchor length");
    def->category = L("Strength");
    def->tooltip = L("Connect an infill line to an internal perimeter with a short segment of an additional perimeter. "
                     "If expressed as percentage (example: 15%) it is calculated over infill extrusion width. Orca Slicer tries to connect two close infill lines to a short perimeter segment. If no such perimeter segment "
                     "shorter than infill_anchor_max is found, the infill line is connected to a perimeter segment at just one side "
                     "and the length of the perimeter segment taken is limited to this parameter, but no longer than anchor_length_max. "
                     "\nSet this parameter to zero to disable anchoring perimeters connected to a single infill line.");
    def->sidetext = L("mm or %");
    def->ratio_over = "sparse_infill_line_width";
    def->max_literal = 1000;
    def->gui_type = ConfigOptionDef::GUIType::f_enum_open;
    def->enum_values.push_back("0");
    def->enum_values.push_back("1");
    def->enum_values.push_back("2");
    def->enum_values.push_back("5");
    def->enum_values.push_back("10");
    def->enum_values.push_back("1000");
    def->enum_labels.push_back(L("0 (no open anchors)"));
    def->enum_labels.push_back("1 mm");
    def->enum_labels.push_back("2 mm");
    def->enum_labels.push_back("5 mm");
    def->enum_labels.push_back("10 mm");
    def->enum_labels.push_back(L("1000 (unlimited)"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(400, true));

    def = this->add("infill_anchor_max", coFloatOrPercent);
    def->label = L("Maximum length of the infill anchor");
    def->category = L("Strength");
    def->tooltip = L("Connect an infill line to an internal perimeter with a short segment of an additional perimeter. "
                     "If expressed as percentage (example: 15%) it is calculated over infill extrusion width. Orca Slicer tries to connect two close infill lines to a short perimeter segment. If no such perimeter segment "
                     "shorter than this parameter is found, the infill line is connected to a perimeter segment at just one side "
                     "and the length of the perimeter segment taken is limited to infill_anchor, but no longer than this parameter. "
                     "\nIf set to 0, the old algorithm for infill connection will be used, it should create the same result as with 1000 & 0.");
    def->sidetext    = def_infill_anchor_min->sidetext;
    def->ratio_over  = def_infill_anchor_min->ratio_over;
    def->gui_type    = def_infill_anchor_min->gui_type;
    def->enum_values = def_infill_anchor_min->enum_values;
    def->max_literal = def_infill_anchor_min->max_literal;
    def->enum_labels.push_back(L("0 (Simple connect)"));
    def->enum_labels.push_back("1 mm");
    def->enum_labels.push_back("2 mm");
    def->enum_labels.push_back("5 mm");
    def->enum_labels.push_back("10 mm");
    def->enum_labels.push_back(L("1000 (unlimited)"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(20, false));
    
    def = this->add("outer_wall_acceleration", coFloat);
    def->label = L("Outer wall");
    def->tooltip = L("Acceleration of outer walls");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10000));

    def = this->add("inner_wall_acceleration", coFloat);
    def->label = L("Inner wall");
    def->tooltip = L("Acceleration of inner walls");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10000));

    def = this->add("travel_acceleration", coFloat);
    def->label = L("Travel");
    def->tooltip = L("Acceleration of travel moves");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10000));

    def = this->add("top_surface_acceleration", coFloat);
    def->label = L("Top surface");
    def->tooltip = L("Acceleration of top surface infill. Using a lower value may improve top surface quality");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(500));

    def = this->add("outer_wall_acceleration", coFloat);
    def->label = L("Outer wall");
    def->tooltip = L("Acceleration of outer wall. Using a lower value can improve quality");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(500));

    def = this->add("bridge_acceleration", coFloatOrPercent);
    def->label = L("Bridge");
    def->tooltip = L("Acceleration of bridges. If the value is expressed as a percentage (e.g. 50%), it will be calculated based on the outer wall acceleration.");
    def->sidetext = L("mm/s² or %");
    def->min = 0;
    def->mode = comAdvanced;
    def->ratio_over = "outer_wall_acceleration";
    def->set_default_value(new ConfigOptionFloatOrPercent(50,true));

    def = this->add("sparse_infill_acceleration", coFloatOrPercent);
    def->label = L("Sparse infill");
    def->tooltip = L("Acceleration of sparse infill. If the value is expressed as a percentage (e.g. 100%), it will be calculated based on the default acceleration.");
    def->sidetext = L("mm/s² or %");
    def->min = 0;
    def->mode = comAdvanced;
    def->ratio_over = "default_acceleration";
    def->set_default_value(new ConfigOptionFloatOrPercent(100, true));

    def = this->add("internal_solid_infill_acceleration", coFloatOrPercent);
    def->label = L("Internal solid infill");
    def->tooltip = L("Acceleration of internal solid infill. If the value is expressed as a percentage (e.g. 100%), it will be calculated based on the default acceleration.");
    def->sidetext = L("mm/s² or %");
    def->min = 0;
    def->mode = comAdvanced;
    def->ratio_over = "default_acceleration";
    def->set_default_value(new ConfigOptionFloatOrPercent(100, true));

    def = this->add("initial_layer_acceleration", coFloat);
    def->label = L("Initial layer");
    def->tooltip = L("Acceleration of initial layer. Using a lower value can improve build plate adhesive");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(300));

    def = this->add("accel_to_decel_enable", coBool);
    def->label = L("Enable accel_to_decel");
    def->tooltip = L("Klipper's max_accel_to_decel will be adjusted automatically");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));
    
    def = this->add("accel_to_decel_factor", coPercent);
    def->label = L("accel_to_decel");
    def->tooltip = L("Klipper's max_accel_to_decel will be adjusted to this %% of acceleration");
    def->sidetext = L("%");
    def->min = 1;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(50));
    
    def = this->add("default_jerk", coFloat);
    def->label = L("Default");
    def->tooltip = L("Default");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("outer_wall_jerk", coFloat);
    def->label = L("Outer wall");
    def->tooltip = L("Jerk of outer walls");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(9));

    def = this->add("inner_wall_jerk", coFloat);
    def->label = L("Inner wall");
    def->tooltip = L("Jerk of inner walls");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(9));

    def = this->add("top_surface_jerk", coFloat);
    def->label = L("Top surface");
    def->tooltip = L("Jerk for top surface");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(9));

    def = this->add("infill_jerk", coFloat);
    def->label = L("Infill");
    def->tooltip = L("Jerk for infill");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(9));

    def = this->add("initial_layer_jerk", coFloat);
    def->label = L("Initial layer");
    def->tooltip = L("Jerk for initial layer");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(9));

    def = this->add("travel_jerk", coFloat);
    def->label = L("Travel");
    def->tooltip = L("Jerk for travel");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(12));

    def = this->add("initial_layer_line_width", coFloatOrPercent);
    def->label = L("Initial layer");
    def->category = L("Quality");
    def->tooltip = L("Line width of initial layer. If expressed as a %, it will be computed over the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));


    def = this->add("initial_layer_print_height", coFloat);
    def->label = L("Initial layer height");
    def->category = L("Quality");
    def->tooltip = L("Height of initial layer. Making initial layer height to be thick slightly can improve build plate adhesion");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.2));

    //def = this->add("adaptive_layer_height", coBool);
    //def->label = L("Adaptive layer height");
    //def->category = L("Quality");
    //def->tooltip = L("Enabling this option means the height of every layer except the first will be automatically calculated "
    //    "during slicing according to the slope of the model’s surface.\n"
    //    "Note that this option only takes effect if no prime tower is generated in current plate.");
    //def->set_default_value(new ConfigOptionBool(0));

    def = this->add("initial_layer_speed", coFloat);
    def->label = L("Initial layer");
    def->tooltip = L("Speed of initial layer except the solid infill part");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(30));

    def = this->add("initial_layer_infill_speed", coFloat);
    def->label = L("Initial layer infill");
    def->tooltip = L("Speed of solid infill part of initial layer");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(60.0));

    def = this->add("initial_layer_travel_speed", coFloatOrPercent);
    def->label = L("Initial layer travel speed");
    def->tooltip = L("Travel speed of initial layer");
    def->category = L("Speed");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "travel_speed";
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(100, true));

    def = this->add("slow_down_layers", coInt);
    def->label = L("Number of slow layers");
    def->tooltip = L("The first few layers are printed slower than normal. "
                     "The speed is gradually increased in a linear fashion over the specified number of layers.");
    def->category = L("Speed");
    def->sidetext = L("layers"); // ORCA add side text
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("nozzle_temperature_initial_layer", coInts);
    def->label = L("Initial layer");
    def->full_label = L("Initial layer nozzle temperature");
    def->tooltip = L("Nozzle temperature to print initial layer when using this filament");
    def->sidetext = L("°C");
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts { 200 });

    def = this->add("full_fan_speed_layer", coInts);
    def->label = L("Full fan speed at layer");
    def->tooltip = L("Fan speed will be ramped up linearly from zero at layer \"close_fan_the_first_x_layers\" "
                  "to maximum at layer \"full_fan_speed_layer\". "
                  "\"full_fan_speed_layer\" will be ignored if lower than \"close_fan_the_first_x_layers\", in which case "
                  "the fan will be running at maximum allowed speed at layer \"close_fan_the_first_x_layers\" + 1.");
    def->sidetext = L("layer"); // ORCA add side text
    def->min = 0;
    def->max = 1000;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts { 0 });
    
    def = this->add("support_material_interface_fan_speed", coInts);
    def->label = L("Support interface fan speed");
    def->tooltip = L("This fan speed is enforced during all support interfaces, to be able to weaken their bonding with a high fan speed."
        "\nSet to -1 to disable this override."
        "\nCan only be overridden by disable_fan_first_layers.");
    def->sidetext = L("%");
    def->min = -1;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInts{ -1 });
    

    def = this->add("fuzzy_skin", coEnum);
    def->label = L("Fuzzy Skin");
    def->category = L("Others");
    def->tooltip = L("Randomly jitter while printing the wall, so that the surface has a rough look. This setting controls "
                     "the fuzzy position");
    def->enum_keys_map = &ConfigOptionEnum<FuzzySkinType>::get_enum_values();
    def->enum_values.push_back("none");
    def->enum_values.push_back("external");
    def->enum_values.push_back("all");
    def->enum_values.push_back("allwalls");
    def->enum_labels.push_back(L("None"));
    def->enum_labels.push_back(L("Contour"));
    def->enum_labels.push_back(L("Contour and hole"));
    def->enum_labels.push_back(L("All walls"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<FuzzySkinType>(FuzzySkinType::None));

    def = this->add("fuzzy_skin_thickness", coFloat);
    def->label = L("Fuzzy skin thickness");
    def->category = L("Others");
    def->tooltip = L("The width within which to jitter. It's advised to be below outer wall line width");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 1;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(0.3));

    def = this->add("fuzzy_skin_point_distance", coFloat);
    def->label = L("Fuzzy skin point distance");
    def->category = L("Others");
    def->tooltip = L("The average distance between the random points introduced on each line segment");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 5;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(0.8));

    def = this->add("fuzzy_skin_first_layer", coBool);
    def->label = L("Apply fuzzy skin to first layer");
    def->category = L("Others");
    def->tooltip = L("Whether to apply fuzzy skin on the first layer");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(0));

    def = this->add("filter_out_gap_fill", coFloat);
    def->label = L("Filter out tiny gaps");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Don't print gap fill with a length is smaller than the threshold specified (in mm). This setting applies to top, "
                     "bottom and solid infill and, if using the classic perimeter generator, to wall gap fill. ");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));
    
    def = this->add("gap_infill_speed", coFloat);
    def->label = L("Gap infill");
    def->category = L("Speed");
    def->tooltip = L("Speed of gap infill. Gap usually has irregular line width and should be printed more slowly");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(30));

    // BBS
    def          = this->add("precise_z_height", coBool);
    def->label   = L("Precise Z height");
    def->tooltip = L("Enable this to get precise z height of object after slicing. "
                     "It will get the precise object height by fine-tuning the layer heights of the last few layers. "
                     "Note that this is an experimental parameter.");
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionBool(0));

    // BBS
    def = this->add("enable_arc_fitting", coBool);
    def->label = L("Arc fitting");
    def->tooltip = L("Enable this to get a G-code file which has G2 and G3 moves. "
                     "The fitting tolerance is same as the resolution. \n\n"
                     "Note: For Klipper machines, this option is recommended to be disabled. Klipper does not benefit from "
                     "arc commands as these are split again into line segments by the firmware. This results in a reduction "
                     "in surface quality as line segments are converted to arcs by the slicer and then back to line segments "
                     "by the firmware.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(0));
    // BBS
    def = this->add("gcode_add_line_number", coBool);
    def->label = L("Add line number");
    def->tooltip = L("Enable this to add line number(Nx) at the beginning of each G-Code line");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(0));

    // BBS
    def = this->add("scan_first_layer", coBool);
    def->label = L("Scan first layer");
    def->tooltip = L("Enable this to enable the camera on printer to check the quality of first layer");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));


    //BBS
    // def = this->add("spaghetti_detector", coBool);
    // def->label = L("Enable spaghetti detector");
    // def->tooltip = L("Enable the camera on printer to check spaghetti");
    // def->mode = comSimple;
    // def->set_default_value(new ConfigOptionBool(false));

    def = this->add("nozzle_type", coEnum);
    def->label = L("Nozzle type");
    def->tooltip = L("The metallic material of nozzle. This determines the abrasive resistance of nozzle, and "
                     "what kind of filament can be printed");
    def->enum_keys_map = &ConfigOptionEnum<NozzleType>::get_enum_values();
    def->enum_values.push_back("undefine");
    def->enum_values.push_back("hardened_steel");
    def->enum_values.push_back("stainless_steel");
    def->enum_values.push_back("brass");
    def->enum_labels.push_back(L("Undefine"));
    def->enum_labels.push_back(L("Hardened steel"));
    def->enum_labels.push_back(L("Stainless steel"));
    def->enum_labels.push_back(L("Brass"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<NozzleType>(ntUndefine));


    def                = this->add("nozzle_hrc", coInt);
    def->label         = L("Nozzle HRC");
    def->tooltip       = L("The nozzle's hardness. Zero means no checking for nozzle's hardness during slicing.");
    def->sidetext      = L("HRC");
    def->min           = 0;
    def->max           = 500;
    def->mode          = comDevelop;
    def->set_default_value(new ConfigOptionInt{0});

    def = this->add("printer_structure", coEnum);
    def->label = L("Printer structure");
    def->tooltip = L("The physical arrangement and components of a printing device");
    def->enum_keys_map = &ConfigOptionEnum<PrinterStructure>::get_enum_values();
    def->enum_values.push_back("undefine");
    def->enum_values.push_back("corexy");
    def->enum_values.push_back("i3");
    def->enum_values.push_back("hbot");
    def->enum_values.push_back("delta");
    def->enum_labels.push_back(L("Undefine"));
    def->enum_labels.push_back(L("CoreXY"));
    def->enum_labels.push_back(L("I3"));
    def->enum_labels.push_back(L("Hbot"));
    def->enum_labels.push_back(L("Delta"));
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionEnum<PrinterStructure>(psUndefine));

    def = this->add("best_object_pos", coPoint);
    def->label = L("Best object position");
    def->tooltip = L("Best auto arranging position in range [0,1] w.r.t. bed shape.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPoint(Vec2d(0.5, 0.5)));

    def = this->add("auxiliary_fan", coBool);
    def->label = L("Auxiliary part cooling fan");
    def->tooltip = L("Enable this option if machine has auxiliary part cooling fan. G-code command: M106 P2 S(0-255).");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));


    def = this->add("fan_speedup_time", coFloat);
	// Label is set in Tab.cpp in the Line object.
    //def->label = L("Fan speed-up time");
    def->tooltip = L("Start the fan this number of seconds earlier than its target start time (you can use fractional seconds)."
        " It assumes infinite acceleration for this time estimation, and will only take into account G1 and G0 moves (arc fitting"
        " is unsupported)."
        "\nIt won't move fan commands from custom gcodes (they act as a sort of 'barrier')."
        "\nIt won't move fan commands into the start gcode if the 'only custom start gcode' is activated."
        "\nUse 0 to deactivate.");
    def->sidetext = L("s");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("fan_speedup_overhangs", coBool);
    def->label = L("Only overhangs");
    def->tooltip = L("Will only take into account the delay for the cooling of overhangs.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("fan_kickstart", coFloat);
    def->label = L("Fan kick-start time");
    def->tooltip = L("Emit a max fan speed command for this amount of seconds before reducing to target speed to kick-start the cooling fan."
                    "\nThis is useful for fans where a low PWM/power may be insufficient to get the fan started spinning from a stop, or to "
                    "get the fan up to speed faster."
                    "\nSet to 0 to deactivate.");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));


    def = this->add("time_cost", coFloat);
    def->label = L("Time cost");
    def->tooltip = L("The printer cost per hour");
    def->sidetext = L("money/h");
    def->min     = 0;
    def->mode    = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    // Orca: may remove this option later
    def =this->add("support_chamber_temp_control",coBool);
    def->label=L("Support control chamber temperature");
    def->tooltip=L("This option is enabled if machine support controlling chamber temperature\nG-code command: M141 S(0-255)");
    def->mode=comDevelop;
    def->set_default_value(new ConfigOptionBool(true));
    def->readonly=false;

    def =this->add("support_air_filtration",coBool);
    def->label=L("Support air filtration");
    def->tooltip=L("Enable this if printer support air filtration\nG-code command: M106 P3 S(0-255)");
    def->mode=comDevelop;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("gcode_flavor", coEnum);
    def->label = L("G-code flavor");
    def->tooltip = L("What kind of gcode the printer is compatible with");
    def->enum_keys_map = &ConfigOptionEnum<GCodeFlavor>::get_enum_values();
    def->enum_values.push_back("marlin");
    def->enum_values.push_back("klipper");
    def->enum_values.push_back("reprapfirmware");
    //def->enum_values.push_back("repetier");
    //def->enum_values.push_back("teacup");
    //def->enum_values.push_back("makerware");
    def->enum_values.push_back("marlin2");
    //def->enum_values.push_back("sailfish");
    //def->enum_values.push_back("mach3");
    //def->enum_values.push_back("machinekit");
    //def->enum_values.push_back("smoothie");
    //def->enum_values.push_back("no-extrusion");
    def->enum_labels.push_back("Marlin(legacy)");
    def->enum_labels.push_back(L("Klipper"));
    def->enum_labels.push_back("RepRapFirmware");
    //def->enum_labels.push_back("RepRap/Sprinter");
    //def->enum_labels.push_back("Repetier");
    //def->enum_labels.push_back("Teacup");
    //def->enum_labels.push_back("MakerWare (MakerBot)");
    def->enum_labels.push_back("Marlin 2");
    //def->enum_labels.push_back("Sailfish (MakerBot)");
    //def->enum_labels.push_back("Mach3/LinuxCNC");
    //def->enum_labels.push_back("Machinekit");
    //def->enum_labels.push_back("Smoothie");
    //def->enum_labels.push_back(L("No extrusion"));
    def->mode = comAdvanced;
    def->readonly = false;
    def->set_default_value(new ConfigOptionEnum<GCodeFlavor>(gcfMarlinLegacy));

    def          = this->add("pellet_modded_printer", coBool);
    def->label   = L("Pellet Modded Printer");
    def->tooltip = L("Enable this option if your printer uses pellets instead of filaments");
    def->mode    = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_multi_bed_types", coBool);
    def->label = L("Support multi bed types");
    def->tooltip = L("Enable this option if you want to use multiple bed types");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("gcode_label_objects", coBool);
    def->label = L("Label objects");
    def->tooltip = L("Enable this to add comments into the G-Code labeling print moves with what object they belong to,"
                   " which is useful for the Octoprint CancelObject plugin. This settings is NOT compatible with "
                   "Single Extruder Multi Material setup and Wipe into Object / Wipe into Infill.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(1));

    def = this->add("exclude_object", coBool);
    def->label = L("Exclude objects");
    def->tooltip = L("Enable this option to add EXCLUDE OBJECT command in g-code");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("gcode_comments", coBool);
    def->label = L("Verbose G-code");
    def->tooltip = L("Enable this to get a commented G-code file, with each line explained by a descriptive text. "
                   "If you print from SD card, the additional weight of the file could make your firmware "
                   "slow down.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(0));
    
    //BBS
    def = this->add("infill_combination", coBool);
    def->label = L("Infill combination");
    def->category = L("Strength");
    def->tooltip = L("Automatically Combine sparse infill of several layers to print together to reduce time. Wall is still printed "
                     "with original layer height.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));
    
    // Orca: max layer height for combined infill
    def = this->add("infill_combination_max_layer_height", coFloatOrPercent);
    def->label = L("Infill combination - Max layer height");
    def->category = L("Strength");
    def->tooltip = L("Maximum layer height for the combined sparse infill. \n\nSet it to 0 or 100% to use the nozzle diameter (for maximum reduction in print time) or a value of ~80% to maximize sparse infill strength.\n\n"
                     "The number of layers over which infill is combined is derived by dividing this value with the layer height and rounded down to the nearest decimal.\n\n"
                     "Use either absolute mm values (eg. 0.32mm for a 0.4mm nozzle) or % values (eg 80%). This value must not be larger "
                     "than the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(100., true));
    
    def = this->add("sparse_infill_filament", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Infill");
    def->category = L("Extruders");
    def->tooltip = L("Filament to print internal sparse infill.");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("sparse_infill_line_width", coFloatOrPercent);
    def->label = L("Sparse infill");
    def->category = L("Quality");
    def->tooltip = L("Line width of internal sparse infill. If expressed as a %, it will be computed over the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    def = this->add("infill_wall_overlap", coPercent);
    def->label = L("Infill/Wall overlap");
    def->category = L("Strength");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Infill area is enlarged slightly to overlap with wall for better bonding. The percentage value is relative to line width of sparse infill. Set this value to ~10-15% to minimize potential over extrusion and accumulation of material resulting in rough top surfaces.");
    def->sidetext = L("%");
    def->ratio_over = "inner_wall_line_width";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(15));
    
    def = this->add("top_bottom_infill_wall_overlap", coPercent);
    def->label = L("Top/Bottom solid infill/wall overlap");
    def->category = L("Strength");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Top solid infill area is enlarged slightly to overlap with wall for better bonding and to minimize the appearance of pinholes where the top infill meets the walls. A value of 25-30% is a good starting point, minimizing the appearance of pinholes. The percentage value is relative to line width of sparse infill");
    def->sidetext = L("%");
    def->ratio_over = "inner_wall_line_width";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(25));

    def = this->add("sparse_infill_speed", coFloat);
    def->label = L("Sparse infill");
    def->category = L("Speed");
    def->tooltip = L("Speed of internal sparse infill");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(100));

    def = this->add("inherits", coString);
    //def->label = L("Inherits profile");
    def->label = "Inherits profile";
    //def->tooltip = L("Name of parent profile");
    def->tooltip = "Name of parent profile";
    def->full_width = true;
    def->height = 5;
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    // The following value is to be stored into the project file (AMF, 3MF, Config ...)
    // and it contains a sum of "inherits" values over the print and filament profiles.
    def = this->add("inherits_group", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("interface_shells", coBool);
    def->label = L("Interface shells");
    def->label = "Interface shells";
    def->tooltip = L("Force the generation of solid shells between adjacent materials/volumes. "
                  "Useful for multi-extruder prints with translucent materials or manual soluble "
                  "support material");
    def->category = L("Quality");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def           = this->add("mmu_segmented_region_max_width", coFloat);
    def->label    = L("Maximum width of a segmented region");
    def->tooltip  = L("Maximum width of a segmented region. Zero disables this feature.");
    def->sidetext = L("mm");
    def->min      = 0;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def           = this->add("mmu_segmented_region_interlocking_depth", coFloat);
    def->label    = L("Interlocking depth of a segmented region");
    def->tooltip  = L("Interlocking depth of a segmented region. It will be ignored if "
                    "\"mmu_segmented_region_max_width\" is zero or if \"mmu_segmented_region_interlocking_depth\""
                    "is bigger then \"mmu_segmented_region_max_width\". Zero disables this feature.");
    def->sidetext = L("mm"); 
    def->min      = 0;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def           = this->add("interlocking_beam", coBool);
    def->label    = L("Use beam interlocking");
    def->tooltip  = L("Generate interlocking beam structure at the locations where different filaments touch. This improves the adhesion between filaments, especially models printed in different materials.");
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def           = this->add("interlocking_beam_width", coFloat);
    def->label    = L("Interlocking beam width");
    def->tooltip  = L("The width of the interlocking structure beams.");
    def->sidetext = L("mm");
    def->min      = 0.01;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.8));

    def           = this->add("interlocking_orientation", coFloat);
    def->label    = L("Interlocking direction");
    def->tooltip  = L("Orientation of interlock beams.");
    def->sidetext = L("°");
    def->min      = 0;
    def->max      = 360;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(22.5));

    def           = this->add("interlocking_beam_layer_count", coInt);
    def->label    = L("Interlocking beam layers");
    def->tooltip  = L("The height of the beams of the interlocking structure, measured in number of layers. Less layers is stronger, but more prone to defects.");
    def->min      = 1;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionInt(2));

    def           = this->add("interlocking_depth", coInt);
    def->label    = L("Interlocking depth");
    def->tooltip  = L("The distance from the boundary between filaments to generate interlocking structure, measured in cells. Too few cells will result in poor adhesion.");
    def->min      = 1;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionInt(2));

    def           = this->add("interlocking_boundary_avoidance", coInt);
    def->label    = L("Interlocking boundary avoidance");
    def->tooltip  = L("The distance from the outside of a model where interlocking structures will not be generated, measured in cells.");
    def->min      = 0;
    def->category = L("Advanced");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionInt(2));

    def = this->add("ironing_type", coEnum);
    def->label = L("Ironing Type");
    def->category = L("Quality");
    def->tooltip = L("Ironing is using small flow to print on same height of surface again to make flat surface more smooth. "
                     "This setting controls which layer being ironed");
    def->enum_keys_map = &ConfigOptionEnum<IroningType>::get_enum_values();
    def->enum_values.push_back("no ironing");
    def->enum_values.push_back("top");
    def->enum_values.push_back("topmost");
    def->enum_values.push_back("solid");
    def->enum_labels.push_back(L("No ironing"));
    def->enum_labels.push_back(L("Top surfaces"));
    def->enum_labels.push_back(L("Topmost surface"));
    def->enum_labels.push_back(L("All solid layer"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<IroningType>(IroningType::NoIroning));

    def                = this->add("ironing_pattern", coEnum);
    def->label         = L("Ironing Pattern");
    def->tooltip       = L("The pattern that will be used when ironing");
    def->category      = L("Quality");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("zig-zag");
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Rectilinear"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipRectilinear));
    
    def = this->add("ironing_flow", coPercent);
    def->label = L("Ironing flow");
    def->category = L("Quality");
    def->tooltip = L("The amount of material to extrude during ironing. Relative to flow of normal layer height. "
                     "Too high value results in overextrusion on the surface");
    def->sidetext = L("%");
    def->ratio_over = "layer_height";
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(10));

    def = this->add("ironing_spacing", coFloat);
    def->label = L("Ironing line spacing");
    def->category = L("Quality");
    def->tooltip = L("The distance between the lines of ironing");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.1));

    def           = this->add("ironing_inset", coFloat);
    def->label    = L("Ironing inset");
    def->category = L("Quality");
    def->tooltip  = L("The distance to keep from the edges. A value of 0 sets this to half of the nozzle diameter");
    def->sidetext = L("mm");
    def->min      = 0;
    def->max      = 100;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("ironing_speed", coFloat);
    def->label = L("Ironing speed");
    def->category = L("Quality");
    def->tooltip = L("Print speed of ironing lines");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(20));

    def           = this->add("ironing_angle", coFloat);
    def->label    = L("Ironing angle");
    def->category = L("Quality");
    def->tooltip  = L("The angle ironing is done at. A negative number disables this function and uses the default method.");
    def->sidetext = L("°");
    def->min      = -1;
    def->max      = 359;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(-1));

    def = this->add("layer_change_gcode", coString);
    def->label = L("Layer change G-code");
    def->tooltip = L("This gcode part is inserted at every layer change after lift z");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("time_lapse_gcode",coString);
    def->label = L("Time lapse G-code");
    def->multiline = true;
    def->full_width = true;
    def->height =5;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("silent_mode", coBool);
    def->label = L("Supports silent mode");
    def->tooltip = L("Whether the machine supports silent mode in which machine use lower acceleration to print");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("emit_machine_limits_to_gcode", coBool);
    def->label = L("Emit limits to G-code");
    def->category = L("Machine limits");
    def->tooltip  = L("If enabled, the machine limits will be emitted to G-code file.\nThis option will be ignored if the g-code flavor is "
                       "set to Klipper.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("machine_pause_gcode", coString);
    def->label = L("Pause G-code");
    def->tooltip = L("This G-code will be used as a code for the pause print. User can insert pause G-code in gcode viewer");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("template_custom_gcode", coString);
    def->label = L("Custom G-code");
    def->tooltip = L("This G-code will be used as a custom code");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("small_area_infill_flow_compensation", coBool);
    def->label = L("Small area flow compensation (beta)");
    def->tooltip = L("Enable flow compensation for small infill areas");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("small_area_infill_flow_compensation_model", coStrings);
    def->label = L("Flow Compensation Model");
    def->tooltip = L(
        "Flow Compensation Model, used to adjust the flow for small infill "
        "areas. The model is expressed as a comma separated pair of values for "
        "extrusion length and flow correction factors, one per line, in the "
        "following format: \"1.234,5.678\"");
    def->mode = comAdvanced;
    def->gui_flags = "serialized";
    def->multiline = true;
    def->full_width = true;
    def->height = 15;
    def->set_default_value(new ConfigOptionStrings{"0,0", "\n0.2,0.4444", "\n0.4,0.6145", "\n0.6,0.7059", "\n0.8,0.7619", "\n1.5,0.8571", "\n2,0.8889", "\n3,0.9231", "\n5,0.9520", "\n10,1"});

    def = this->add("has_scarf_joint_seam", coBool);
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    {
        struct AxisDefault {
            std::string         name;
            std::vector<double> max_feedrate;
            std::vector<double> max_acceleration;
            std::vector<double> max_jerk;
        };
        std::vector<AxisDefault> axes {
            // name, max_feedrate,  max_acceleration, max_jerk
            { "x", { 500., 200. }, {  1000., 1000. }, { 10. , 10.  } },
            { "y", { 500., 200. }, {  1000., 1000. }, { 10. , 10.  } },
            { "z", {  12.,  12. }, {   500.,  200. }, {  0.2,  0.4 } },
            { "e", { 120., 120. }, {  5000., 5000. }, {  2.5,  2.5 } }
        };
        for (const AxisDefault &axis : axes) {
            std::string axis_upper = boost::to_upper_copy<std::string>(axis.name);
            // Add the machine feedrate limits for XYZE axes. (M203)
            def = this->add("machine_max_speed_" + axis.name, coFloats);
            def->full_label = (boost::format("Maximum speed %1%") % axis_upper).str();
            (void)L("Maximum speed X");
            (void)L("Maximum speed Y");
            (void)L("Maximum speed Z");
            (void)L("Maximum speed E");
            def->category = L("Machine limits");
            def->readonly = false;
            def->tooltip  = (boost::format("Maximum speed of %1% axis") % axis_upper).str();
            (void)L("Maximum X speed");
            (void)L("Maximum Y speed");
            (void)L("Maximum Z speed");
            (void)L("Maximum E speed");
            def->sidetext = L("mm/s");
            def->min = 0;
            def->mode = comSimple;
            def->set_default_value(new ConfigOptionFloats(axis.max_feedrate));
            // Add the machine acceleration limits for XYZE axes (M201)
            def = this->add("machine_max_acceleration_" + axis.name, coFloats);
            def->full_label = (boost::format("Maximum acceleration %1%") % axis_upper).str();
            (void)L("Maximum acceleration X");
            (void)L("Maximum acceleration Y");
            (void)L("Maximum acceleration Z");
            (void)L("Maximum acceleration E");
            def->category = L("Machine limits");
            def->readonly = false;
            def->tooltip  = (boost::format("Maximum acceleration of the %1% axis") % axis_upper).str();
            (void)L("Maximum acceleration of the X axis");
            (void)L("Maximum acceleration of the Y axis");
            (void)L("Maximum acceleration of the Z axis");
            (void)L("Maximum acceleration of the E axis");
            def->sidetext = L("mm/s²");
            def->min = 0;
            def->mode = comSimple;
            def->set_default_value(new ConfigOptionFloats(axis.max_acceleration));
            // Add the machine jerk limits for XYZE axes (M205)
            def = this->add("machine_max_jerk_" + axis.name, coFloats);
            def->full_label = (boost::format("Maximum jerk %1%") % axis_upper).str();
            (void)L("Maximum jerk X");
            (void)L("Maximum jerk Y");
            (void)L("Maximum jerk Z");
            (void)L("Maximum jerk E");
            def->category = L("Machine limits");
            def->readonly = false;
            def->tooltip  = (boost::format("Maximum jerk of the %1% axis") % axis_upper).str();
            (void)L("Maximum jerk of the X axis");
            (void)L("Maximum jerk of the Y axis");
            (void)L("Maximum jerk of the Z axis");
            (void)L("Maximum jerk of the E axis");
            def->sidetext = L("mm/s");
            def->min = 0;
            def->mode = comSimple;
            def->set_default_value(new ConfigOptionFloats(axis.max_jerk));
        }
    }

    // M205 S... [mm/sec]
    def = this->add("machine_min_extruding_rate", coFloats);
    def->full_label = L("Minimum speed for extruding");
    def->category = L("Machine limits");
    def->tooltip = L("Minimum speed for extruding (M205 S)");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloats{ 0., 0. });

    // M205 T... [mm/sec]
    def = this->add("machine_min_travel_rate", coFloats);
    def->full_label = L("Minimum travel speed");
    def->category = L("Machine limits");
    def->tooltip = L("Minimum travel speed (M205 T)");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloats{ 0., 0. });

    // M204 P... [mm/sec^2]
    def = this->add("machine_max_acceleration_extruding", coFloats);
    def->full_label = L("Maximum acceleration for extruding");
    def->category = L("Machine limits");
    def->tooltip = L("Maximum acceleration for extruding (M204 P)");
    //                 "Marlin (legacy) firmware flavor will use this also "
    //                 "as travel acceleration (M204 T).");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->readonly = false;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats{ 1500., 1250. });


    // M204 R... [mm/sec^2]
    def = this->add("machine_max_acceleration_retracting", coFloats);
    def->full_label = L("Maximum acceleration for retracting");
    def->category = L("Machine limits");
    def->tooltip = L("Maximum acceleration for retracting (M204 R)");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->readonly = false;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats{ 1500., 1250. });

    // M204 T... [mm/sec^2]
    def = this->add("machine_max_acceleration_travel", coFloats);
    def->full_label = L("Maximum acceleration for travel");
    def->category = L("Machine limits");
    def->tooltip = L("Maximum acceleration for travel (M204 T), it only applies to Marlin 2");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->readonly = false;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 0., 0. });

    def = this->add("fan_max_speed", coFloats);
    def->label = L("Fan speed");
    def->tooltip = L("Part cooling fan speed may be increased when auto cooling is enabled. "
                     "This is the maximum speed limitation of part cooling fan");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats { 100 });

    def = this->add("max_layer_height", coFloats);
    def->label = L("Max");
    def->tooltip = L("The largest printable layer height for extruder. Used tp limits "
                     "the maximum layer hight when enable adaptive layer height");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("max_volumetric_extrusion_rate_slope", coFloat);
    def->label = L("Extrusion rate smoothing");
    def->tooltip = L("This parameter smooths out sudden extrusion rate changes that happen when " 
    				 "the printer transitions from printing a high flow (high speed/larger width) "
    				 "extrusion to a lower flow (lower speed/smaller width) extrusion and vice versa.\n\n"
    				 "It defines the maximum rate by which the extruded volumetric flow in mm3/sec can change over time. "
    				 "Higher values mean higher extrusion rate changes are allowed, resulting in faster speed transitions.\n\n" 
    				 "A value of 0 disables the feature. \n\n"
    				 "For a high speed, high flow direct drive printer (like the Bambu lab or Voron) this value is usually not needed. "
    				 "However it can provide some marginal benefit in certain cases where feature speeds vary greatly. For example, "
    				 "when there are aggressive slowdowns due to overhangs. In these cases a high value of around 300-350mm3/s2 is "
    				 "recommended as this allows for just enough smoothing to assist pressure advance achieve a smoother flow transition.\n\n"
    				 "For slower printers without pressure advance, the value should be set much lower. A value of 10-15mm3/s2 is a "
    				 "good starting point for direct drive extruders and 5-10mm3/s2 for Bowden style. \n\n"
    				 "This feature is known as Pressure Equalizer in Prusa slicer.\n\n"
    				 "Note: this parameter disables arc fitting.");
    def->sidetext = L("mm³/s²");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));
    
    def = this->add("max_volumetric_extrusion_rate_slope_segment_length", coFloat);
    def->label = L("Smoothing segment length");
    def->tooltip = L("A lower value results in smoother extrusion rate transitions. However, this results in a significantly larger gcode file "
    				 "and more instructions for the printer to process. \n\n"
    				 "Default value of 3 works well for most cases. If your printer is stuttering, increase this value to reduce the number of adjustments made\n\n"
    				 "Allowed values: 0.5-5");
    def->min = 0.5;
    def->max = 5;
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(3.0));
    
    def = this->add("extrusion_rate_smoothing_external_perimeter_only", coBool);
    def->label = L("Apply only on external features");
    def->tooltip = L("Applies extrusion rate smoothing only on external perimeters and overhangs. This can help reduce artefacts due to sharp speed transitions on externally visible "
                     "overhangs without impacting the print speed of features that will not be visible to the user.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));


    def = this->add("fan_min_speed", coFloats);
    def->label = L("Fan speed");
    def->tooltip = L("Minimum speed for part cooling fan");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats { 20 });

    def = this->add("additional_cooling_fan_speed", coInts);
    def->label = L("Fan speed");
    def->tooltip = L("Speed of auxiliary part cooling fan. Auxiliary fan will run at this speed during printing except the first several layers "
                     "which is defined by no cooling layers.\nPlease enable auxiliary_fan in printer settings to use this feature. G-code command: M106 P2 S(0-255)");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInts { 0 });

    def = this->add("min_layer_height", coFloats);
    def->label = L("Min");
    def->tooltip = L("The lowest printable layer height for extruder. Used tp limits "
                     "the minimum layer hight when enable adaptive layer height");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0.07 });

    def = this->add("slow_down_min_speed", coFloats);
    def->label = L("Min print speed");
    def->tooltip = L("The minimum print speed to which the printer slows down to maintain the minimum layer time defined above "
                     "when the slowdown for better layer cooling is enabled.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 10. });

    def = this->add("nozzle_diameter", coFloats);
    def->label = L("Nozzle diameter");
    def->tooltip = L("Diameter of nozzle");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->max = 100;
    def->set_default_value(new ConfigOptionFloats { 0.4 });

    def = this->add("notes", coString);
    def->label = L("Configuration notes");
    def->tooltip = L("You can put here your personal notes. This text will be added to the G-code "
                   "header comments.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("host_type", coEnum);
    def->label = L("Host Type");
    def->tooltip = L("Orca Slicer can upload G-code files to a printer host. This field must contain "
                   "the kind of the host.");
    def->enum_keys_map = &ConfigOptionEnum<PrintHostType>::get_enum_values();
    def->enum_values.push_back("prusalink");
    def->enum_values.push_back("prusaconnect");
    def->enum_values.push_back("octoprint");
    def->enum_values.push_back("duet");
    def->enum_values.push_back("flashair");
    def->enum_values.push_back("astrobox");
    def->enum_values.push_back("repetier");
    def->enum_values.push_back("mks");
    def->enum_values.push_back("esp3d");
    def->enum_values.push_back("crealityprint");
    def->enum_values.push_back("obico");
    def->enum_values.push_back("flashforge");
    def->enum_values.push_back("simplyprint");
    def->enum_labels.push_back("PrusaLink");
    def->enum_labels.push_back("PrusaConnect");
    def->enum_labels.push_back("Octo/Klipper");
    def->enum_labels.push_back("Duet");
    def->enum_labels.push_back("FlashAir");
    def->enum_labels.push_back("AstroBox");
    def->enum_labels.push_back("Repetier");
    def->enum_labels.push_back("MKS");
    def->enum_labels.push_back("ESP3D");
    def->enum_labels.push_back("CrealityPrint");
    def->enum_labels.push_back("Obico");
    def->enum_labels.push_back("Flashforge");
    def->enum_labels.push_back("SimplyPrint");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionEnum<PrintHostType>(htOctoPrint));
    

    def = this->add("nozzle_volume", coFloat);
    def->label = L("Nozzle volume");
    def->tooltip = L("Volume of nozzle between the cutter and the end of nozzle");
    def->sidetext = L("mm³");
    def->mode = comAdvanced;
    def->readonly = false;
    def->set_default_value(new ConfigOptionFloat { 0.0 });

    def = this->add("cooling_tube_retraction", coFloat);
    def->label = L("Cooling tube position");
    def->tooltip = L("Distance of the center-point of the cooling tube from the extruder tip.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(91.5));

    def = this->add("cooling_tube_length", coFloat);
    def->label = L("Cooling tube length");
    def->tooltip = L("Length of the cooling tube to limit space for cooling moves inside it.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.));

    def = this->add("high_current_on_filament_swap", coBool);
    def->label = L("High extruder current on filament swap");
    def->tooltip = L("It may be beneficial to increase the extruder motor current during the filament exchange"
                   " sequence to allow for rapid ramming feed rates and to overcome resistance when loading"
                   " a filament with an ugly shaped tip.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(0));

    def = this->add("parking_pos_retraction", coFloat);
    def->label = L("Filament parking position");
    def->tooltip = L("Distance of the extruder tip from the position where the filament is parked "
                      "when unloaded. This should match the value in printer firmware.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(92.));

    def = this->add("extra_loading_move", coFloat);
    def->label = L("Extra loading distance");
    def->tooltip = L("When set to zero, the distance the filament is moved from parking position during load "
                      "is exactly the same as it was moved back during unload. When positive, it is loaded further, "
                      " if negative, the loading move is shorter than unloading.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(-2.));

    def = this->add("start_end_points", coPoints);
    def->label = L("Start end points");
    def->tooltip  = L("The start and end points which is from cutter area to garbage can.");
    def->mode     = comDevelop;
    def->readonly = true;
    // start and end point is from the change_filament_gcode
    def->set_default_value(new ConfigOptionPoints{Vec2d(30, -3), Vec2d(54, 245)});

    def = this->add("reduce_infill_retraction", coBool);
    def->label = L("Reduce infill retraction");
    def->tooltip = L("Don't retract when the travel is in infill area absolutely. That means the oozing can't been seen. "
                     "This can reduce times of retraction for complex model and save printing time, but make slicing and "
                     "G-code generating slower");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("ooze_prevention", coBool);
    def->label = L("Enable");
    def->tooltip = L("This option will drop the temperature of the inactive extruders to prevent oozing.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("filename_format", coString);
    def->label = L("Filename format");
    def->tooltip = L("User can self-define the project file name when export");
    def->full_width = true;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString("{input_filename_base}_{filament_type[initial_tool]}_{print_time}.gcode"));

    def = this->add("make_overhang_printable", coBool);
    def->label = L("Make overhangs printable");
    def->category = L("Quality");
    def->tooltip = L("Modify the geometry to print overhangs without support material.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("make_overhang_printable_angle", coFloat);
    def->label = L("Make overhangs printable - Maximum angle");
    def->category = L("Quality");
    def->tooltip = L("Maximum angle of overhangs to allow after making more steep overhangs printable."
                     "90° will not change the model at all and allow any overhang, while 0 will "
                     "replace all overhangs with conical material.");
    def->sidetext = L("°");
    def->mode = comAdvanced;
    def->min = 0.;
    def->max = 90.;
    def->set_default_value(new ConfigOptionFloat(55.));

    def = this->add("make_overhang_printable_hole_size", coFloat);
    def->label = L("Make overhangs printable - Hole area");
    def->category = L("Quality");
    def->tooltip = L("Maximum area of a hole in the base of the model before it's filled by conical material."
                     "A value of 0 will fill all the holes in the model base.");
    def->sidetext = L("mm²");
    def->mode = comAdvanced;
    def->min = 0.;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("detect_overhang_wall", coBool);
    def->label = L("Detect overhang wall");
    def->category = L("Quality");
    def->tooltip = L("Detect the overhang percentage relative to line width and use different speed to print. "
                     "For 100%% overhang, bridge speed is used.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("wall_filament", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Walls");
    def->category = L("Extruders");
    def->tooltip = L("Filament to print walls");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("inner_wall_line_width", coFloatOrPercent);
    def->label = L("Inner wall");
    def->category = L("Quality");
    def->tooltip = L("Line width of inner wall. If expressed as a %, it will be computed over the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    def = this->add("inner_wall_speed", coFloat);
    def->label = L("Inner wall");
    def->category = L("Speed");
    def->tooltip = L("Speed of inner wall");
    def->sidetext = L("mm/s");
    def->aliases = { "perimeter_feed_rate" };
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(60));

    def = this->add("wall_loops", coInt);
    def->label = L("Wall loops");
    def->category = L("Strength");
    def->tooltip = L("Number of walls of every layer");
    def->min = 0;
    def->max = 1000;
    def->set_default_value(new ConfigOptionInt(2));
    
    def = this->add("alternate_extra_wall", coBool);
    def->label = L("Alternate extra wall");
    def->category = L("Strength");
    def->tooltip = L("This setting adds an extra wall to every other layer. This way the infill gets wedged vertically between the walls, resulting in stronger prints. \n\nWhen this option is enabled, the ensure vertical shell thickness option needs to be disabled. \n\nUsing lightning infill together with this option is not recommended as there is limited infill to anchor the extra perimeters to.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));
    
    def = this->add("post_process", coStrings);
    def->label = L("Post-processing Scripts");
    def->tooltip = L("If you want to process the output G-code through custom scripts, "
                   "just list their absolute paths here. Separate multiple scripts with a semicolon. "
                   "Scripts will be passed the absolute path to the G-code file as the first argument, "
                   "and they can access the Orca Slicer config settings by reading environment variables.");
    def->gui_flags = "serialized";
    def->multiline = true;
    def->full_width = true;
    def->height = 6;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings());
    
    def = this->add("printer_model", coString);
    //def->label = L("Printer type");
    //def->tooltip = L("Type of the printer");
    def->label = L("Printer type");
    def->tooltip = L("Type of the printer");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("printer_notes", coString);
    def->label = L("Printer notes");
    def->tooltip = L("You can put your notes regarding the printer here.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));
    
    def = this->add("printer_variant", coString);
    //def->label = L("Printer variant");
    def->label = L("Printer variant");
    //def->tooltip = L("Name of the printer variant. For example, the printer variants may be differentiated by a nozzle diameter.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("print_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    //BBS: open this option to command line
    //def->cli = ConfigOptionDef::nocli;

    def = this->add("printer_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    //BBS: open this option to command line
    //def->cli = ConfigOptionDef::nocli;

    def = this->add("raft_contact_distance", coFloat);
    def->label = L("Raft contact Z distance");
    def->category = L("Support");
    def->tooltip = L("Z gap between object and raft. Ignored for soluble interface");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.1));

    def = this->add("raft_expansion", coFloat);
    def->label = L("Raft expansion");
    def->category = L("Support");
    def->tooltip = L("Expand all raft layers in XY plane");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.5));

    def = this->add("raft_first_layer_density", coPercent);
    def->label = L("Initial layer density");
    def->category = L("Support");
    def->tooltip = L("Density of the first raft or support layer");
    def->sidetext = L("%");
    def->min = 10;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(90));

    def = this->add("raft_first_layer_expansion", coFloat);
    def->label = L("Initial layer expansion");
    def->category = L("Support");
    def->tooltip = L("Expand the first raft or support layer to improve bed plate adhesion");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    //BBS: change from 3.0 to 2.0
    def->set_default_value(new ConfigOptionFloat(2.0));

    def = this->add("raft_layers", coInt);
    def->label = L("Raft layers");
    def->category = L("Support");
    def->tooltip = L("Object will be raised by this number of support layers. "
                     "Use this function to avoid wrapping when print ABS");
    def->sidetext = L("layers");
    def->min = 0;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("resolution", coFloat);
    def->label = L("Resolution");
    def->tooltip = L("G-code path is generated after simplifying the contour of model to avoid too much points and gcode lines "
                     "in gcode file. Smaller value means higher resolution and more time to slice");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.01));

    def = this->add("retraction_minimum_travel", coFloats);
    def->label = L("Travel distance threshold");
    def->tooltip = L("Only trigger retraction when the travel distance is longer than this threshold");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 2. });

    def = this->add("retract_before_wipe", coPercents);
    def->label = L("Retract amount before wipe");
    def->tooltip = L("The length of fast retraction before wipe, relative to retraction length");
    def->sidetext = L("%");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercents { 100 });

    def = this->add("retract_when_changing_layer", coBools);
    def->label = L("Retract when change layer");
    def->tooltip = L("Force a retraction when changes layer");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("retract_on_top_layer", coBools);
    def->label = L("Retract on top layer");
    def->tooltip = L("Force a retraction on top layer. Disabling could prevent clog on very slow patterns with small movements, like Hilbert curve");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { true });

    def = this->add("retraction_length", coFloats);
    def->label = L("Length");
    def->full_label = L("Retraction Length");
    def->tooltip = L("Some amount of material in extruder is pulled back to avoid ooze during long travel. "
                     "Set zero to disable retraction");
    def->sidetext = L("mm");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats { 0.8 });

    def = this->add("enable_long_retraction_when_cut",coInt);
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInt {0});

    def = this->add("long_retractions_when_cut", coBools);
    def->label = L("Long retraction when cut(experimental)");
    def->tooltip = L("Experimental feature.Retracting and cutting off the filament at a longer distance during changes to minimize purge."
                     "While this reduces flush significantly, it may also raise the risk of nozzle clogs or other printing problems.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionBools {false});

    def = this->add("retraction_distances_when_cut",coFloats);
    def->label = L("Retraction distance when cut");
    def->tooltip = L("Experimental feature.Retraction length before cutting off during filament change");
    def->mode = comDevelop;
    def->min = 10;
    def->max = 18;
    def->set_default_value(new ConfigOptionFloats {18});

    def = this->add("retract_length_toolchange", coFloats);
    def->label = L("Length");
    //def->full_label = L("Retraction Length (Toolchange)");
    def->full_label = "Retraction Length (Toolchange)";
    //def->tooltip = L("When retraction is triggered before changing tool, filament is pulled back "
    //               "by the specified amount (the length is measured on raw filament, before it enters "
    //               "the extruder).");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 10. });

    def = this->add("z_hop", coFloats);
    def->label = L("Z-hop height");
    def->tooltip = L("Whenever the retraction is done, the nozzle is lifted a little to create clearance between nozzle and the print. "
                     "It prevents nozzle from hitting the print when travel move. "
                     "Using spiral line to lift z can prevent stringing");
    def->sidetext = L("mm");
    def->mode = comSimple;
    def->min = 0;
    def->max = 5;
    def->set_default_value(new ConfigOptionFloats { 0.4 });

    def             = this->add("retract_lift_above", coFloats);
    def->label      = L("Z hop lower boundary");
    def->tooltip    = L("Z hop will only come into effect when Z is above this value and is below the parameter: \"Z hop upper boundary\"");
    def->sidetext   = L("mm");
    def->mode       = comAdvanced;
    def->min        = 0;
    def->set_default_value(new ConfigOptionFloats{0.});

    def             = this->add("retract_lift_below", coFloats);
    def->label      = L("Z hop upper boundary");
    def->tooltip    = L("If this value is positive, Z hop will only come into effect when Z is above the parameter: \"Z hop lower boundary\" and is below this value");
    def->sidetext   = L("mm");
    def->mode       = comAdvanced;
    def->min        = 0;
    def->set_default_value(new ConfigOptionFloats{0.});


    def = this->add("z_hop_types", coEnums);
    def->label = L("Z-hop type");
    def->tooltip = L("Z hop type");
    def->enum_keys_map = &ConfigOptionEnum<ZHopType>::get_enum_values();
    def->enum_values.push_back("Auto Lift");
    def->enum_values.push_back("Normal Lift");
    def->enum_values.push_back("Slope Lift");
    def->enum_values.push_back("Spiral Lift");
    def->enum_labels.push_back(L("Auto"));
    def->enum_labels.push_back(L("Normal"));
    def->enum_labels.push_back(L("Slope"));
    def->enum_labels.push_back(L("Spiral"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnumsGeneric{ ZHopType::zhtSlope });

    def = this->add("travel_slope", coFloats);
    def->label = L("Traveling angle");
    def->tooltip = L("Traveling angle for Slope and Spiral Z hop type. Setting it to 90° results in Normal Lift");
    def->sidetext = L("°");
    def->mode = comAdvanced;
    def->min = 1;
    def->max = 90;
    def->set_default_value(new ConfigOptionFloats { 3 });

    def = this->add("retract_lift_above", coFloats);
    def->label = L("Only lift Z above");
    def->tooltip = L("If you set this to a positive value, Z lift will only take place above the specified absolute Z.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{0.});

    def = this->add("retract_lift_below", coFloats);
    def->label = L("Only lift Z below");
    def->tooltip = L("If you set this to a positive value, Z lift will only take place below the specified absolute Z.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{0.});

    def = this->add("retract_lift_enforce", coEnums);
    def->label = L("On surfaces");
    def->tooltip = L("Enforce Z Hop behavior. This setting is impacted by the above settings (Only lift Z above/below).");
    def->enum_keys_map = &ConfigOptionEnum<RetractLiftEnforceType>::get_enum_values();
    def->enum_values.push_back("All Surfaces");
    def->enum_values.push_back("Top Only");
    def->enum_values.push_back("Bottom Only");
    def->enum_values.push_back("Top and Bottom");
    def->enum_labels.push_back(L("All Surfaces"));
    def->enum_labels.push_back(L("Top Only"));
    def->enum_labels.push_back(L("Bottom Only"));
    def->enum_labels.push_back(L("Top and Bottom"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnumsGeneric{RetractLiftEnforceType ::rletAllSurfaces});

    def = this->add("retract_restart_extra", coFloats);
    def->label = L("Extra length on restart");
    def->tooltip = L("When the retraction is compensated after the travel move, the extruder will push "
                  "this additional amount of filament. This setting is rarely needed.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("retract_restart_extra_toolchange", coFloats);
    def->label = L("Extra length on restart");
    def->tooltip = L("When the retraction is compensated after changing tool, the extruder will push "
                  "this additional amount of filament.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("retraction_speed", coFloats);
    def->label = L("Retraction Speed");
    def->full_label = L("Retraction Speed");
    def->tooltip = L("Speed of retractions");
    def->sidetext = L("mm/s");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 30. });

    def = this->add("deretraction_speed", coFloats);
    def->label = L("De-retraction Speed");
    def->full_label = L("De-retraction Speed");
    def->tooltip = L("Speed for reloading filament into extruder. Zero means same speed with retraction");
    def->sidetext = L("mm/s");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("use_firmware_retraction", coBool);
    def->label = L("Use firmware retraction");
    def->tooltip = L("This experimental setting uses G10 and G11 commands to have the firmware "
                   "handle the retraction. This is only supported in recent Marlin.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("bbl_calib_mark_logo", coBool);
    def->label = L("Show auto-calibration marks");
    def->tooltip = "";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("disable_m73", coBool);
    def->label = L("Disable set remaining print time");
    def->tooltip = L("Disable generating of the M73: Set remaining print time in the final gcode");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("seam_position", coEnum);
    def->label = L("Seam position");
    def->category = L("Quality");
    def->tooltip = L("The start position to print each part of outer wall");
    def->enum_keys_map = &ConfigOptionEnum<SeamPosition>::get_enum_values();
    def->enum_values.push_back("nearest");
    def->enum_values.push_back("aligned");
    def->enum_values.push_back("back");
    def->enum_values.push_back("random");
    def->enum_labels.push_back(L("Nearest"));
    def->enum_labels.push_back(L("Aligned"));
    def->enum_labels.push_back(L("Back"));
    def->enum_labels.push_back(L("Random"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<SeamPosition>(spAligned));

    def = this->add("staggered_inner_seams", coBool);
    def->label = L("Staggered inner seams");
    def->tooltip = L("This option causes the inner seams to be shifted backwards based on their depth, forming a zigzag pattern.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));
    
    def = this->add("seam_gap", coFloatOrPercent);
    def->label = L("Seam gap");
    def->tooltip = L("In order to reduce the visibility of the seam in a closed loop extrusion, the loop is interrupted and shortened by a specified amount.\n"
                     "This amount can be specified in millimeters or as a percentage of the current extruder diameter. The default value for this parameter is 10%.");
    def->sidetext = L("mm or %");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(10,true));

    def = this->add("seam_slope_type", coEnum);
    def->label = L("Scarf joint seam (beta)");
    def->tooltip = L("Use scarf joint to minimize seam visibility and increase seam strength.");
    def->enum_keys_map = &ConfigOptionEnum<SeamScarfType>::get_enum_values();
    def->enum_values.push_back("none");
    def->enum_values.push_back("external");
    def->enum_values.push_back("all");
    def->enum_labels.push_back(L("None"));
    def->enum_labels.push_back(L("Contour"));
    def->enum_labels.push_back(L("Contour and hole"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SeamScarfType>(SeamScarfType::None));

    def = this->add("seam_slope_conditional", coBool);
    def->label = L("Conditional scarf joint");
    def->tooltip = L("Apply scarf joints only to smooth perimeters where traditional seams do not conceal the seams at sharp corners effectively.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("scarf_angle_threshold", coInt);
    def->label = L("Conditional angle threshold");
    def->tooltip = L(
        "This option sets the threshold angle for applying a conditional scarf joint seam.\nIf the maximum angle within the perimeter loop "
        "exceeds this value (indicating the absence of sharp corners), a scarf joint seam will be used. The default value is 155°.");
    def->mode = comAdvanced;
    def->sidetext = L("°");
    def->min = 0;
    def->max = 180;
    def->set_default_value(new ConfigOptionInt(155));

    def = this->add("scarf_overhang_threshold", coPercent);
    def->label = L("Conditional overhang threshold");
    def->category = L("Quality");
    // xgettext:no-c-format, no-boost-format
    def->tooltip  = L("This option determines the overhang threshold for the application of scarf joint seams. If the unsupported portion "
                       "of the perimeter is less than this threshold, scarf joint seams will be applied. The default threshold is set at 40% "
                       "of the external wall's width. Due to performance considerations, the degree of overhang is estimated.");
    def->sidetext = L("%");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(40));

    def = this->add("scarf_joint_speed", coFloatOrPercent);
    def->label = L("Scarf joint speed");
    def->category = L("Quality");
    def->tooltip  = L(
        "This option sets the printing speed for scarf joints. It is recommended to print scarf joints at a slow speed (less than 100 "
         "mm/s).  It's also advisable to enable 'Extrusion rate smoothing' if the set speed varies significantly from the speed of the "
         "outer or inner walls. If the speed specified here is higher than the speed of the outer or inner walls, the printer will default "
         "to the slower of the two speeds. When specified as a percentage (e.g., 80%), the speed is calculated based on the respective "
         "outer or inner wall speed. The default value is set to 100%.");
    def->sidetext = L("mm/s or %");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(100, true));

    def = this->add("scarf_joint_flow_ratio", coFloat);
    def->label = L("Scarf joint flow ratio");
    def->tooltip = L("This factor affects the amount of material for scarf joints.");
    def->mode = comDevelop;
    def->max = 2;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("seam_slope_start_height", coFloatOrPercent);
    def->label = L("Scarf start height");
    def->tooltip = L("Start height of the scarf.\n"
                     "This amount can be specified in millimeters or as a percentage of the current layer height. The default value for this parameter is 0.");
    def->sidetext = L("mm or %");
    def->ratio_over = "layer_height";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("seam_slope_entire_loop", coBool);
    def->label = L("Scarf around entire wall");
    def->tooltip = L("The scarf extends to the entire length of the wall.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("seam_slope_min_length", coFloat);
    def->label = L("Scarf length");
    def->tooltip = L("Length of the scarf. Setting this parameter to zero effectively disables the scarf.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(20));

    def = this->add("seam_slope_steps", coInt);
    def->label = L("Scarf steps");
    def->tooltip = L("Minimum number of segments of each scarf.");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(10));

    def = this->add("seam_slope_inner_walls", coBool);
    def->label = L("Scarf joint for inner walls");
    def->tooltip = L("Use scarf joint for inner walls as well.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("role_based_wipe_speed", coBool);
    def->label = L("Role base wipe speed");
    def->tooltip = L("The wipe speed is determined by the speed of the current extrusion role."
                     "e.g. if a wipe action is executed immediately following an outer wall extrusion, the speed of the outer wall extrusion will be utilized for the wipe action.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));
    
    def = this->add("wipe_on_loops", coBool);
    def->label = L("Wipe on loops");
    def->tooltip = L("To minimize the visibility of the seam in a closed loop extrusion, a small inward movement is executed before the extruder leaves the loop.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));
    
    def = this->add("wipe_before_external_loop", coBool);
    def->label = L("Wipe before external loop");
    def->tooltip = L("To minimize visibility of potential overextrusion at the start of an external perimeter when printing with "
                     "Outer/Inner or Inner/Outer/Inner wall print order, the de-retraction is performed slightly on the inside from the "
                     "start of the external perimeter. That way any potential over extrusion is hidden from the outside surface. \n\nThis "
                     "is useful when printing with Outer/Inner or Inner/Outer/Inner wall print order as in these modes it is more likely "
                     "an external perimeter is printed immediately after a de-retraction move.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wipe_speed", coFloatOrPercent);
    def->label = L("Wipe speed");
    def->tooltip = L("The wipe speed is determined by the speed setting specified in this configuration."
                   "If the value is expressed as a percentage (e.g. 80%), it will be calculated based on the travel speed setting above."
                   "The default value for this parameter is 80%");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "travel_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(80,true));
    
    def = this->add("skirt_distance", coFloat);
    def->label = L("Skirt distance");
    def->tooltip = L("Distance from skirt to brim or object");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 60;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(2));

    def = this->add("skirt_start_angle", coFloat);
    def->label = L("Skirt start point");
    def->tooltip = L("Angle from the object center to skirt start point. Zero is the most right position, counter clockwise is positive angle.");
    def->sidetext = L("°");
    def->min = -180;
    def->max = 180;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(-135));

    def = this->add("skirt_height", coInt);
    def->label = L("Skirt height");
    //def->label = "Skirt height";
    def->tooltip = L("How many layers of skirt. Usually only one layer");
    def->sidetext = L("layers");
    def->mode = comSimple;
    def->max = 10000;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("draft_shield", coEnum);
    def->label = L("Draft shield");
    def->tooltip = L("A draft shield is useful to protect an ABS or ASA print from warping and detaching from print bed due to wind draft. "
                     "It is usually needed only with open frame printers, i.e. without an enclosure. \n\n"
                     "Enabled = skirt is as tall as the highest printed object. Otherwise 'Skirt height' is used.\n"
    				 "Note: With the draft shield active, the skirt will be printed at skirt distance from the object. Therefore, if brims "
                     "are active it may intersect with them. To avoid this, increase the skirt distance value.\n");
    def->enum_keys_map = &ConfigOptionEnum<DraftShield>::get_enum_values();
    def->enum_values.push_back("disabled");
    def->enum_values.push_back("enabled");
    def->enum_labels.push_back(L("Disabled"));
    def->enum_labels.push_back(L("Enabled"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<DraftShield>(dsDisabled));

    def = this->add("skirt_type", coEnum);
    def->label = L("Skirt type");
    def->full_label = L("Skirt type");
    def->tooltip = L("Combined - single skirt for all objects, Per object - individual object skirt.");
    def->enum_keys_map = &ConfigOptionEnum<SkirtType>::get_enum_values();
    def->enum_values.push_back("combined");
    def->enum_values.push_back("perobject");
    def->enum_labels.push_back(L("Combined"));
    def->enum_labels.push_back(L("Per object"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SkirtType>(stCombined));
    
    def = this->add("skirt_loops", coInt);
    def->label = L("Skirt loops");
    def->full_label = L("Skirt loops");
    def->tooltip = L("Number of loops for the skirt. Zero means disabling skirt");
    def->min = 0;
    def->max = 10;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("skirt_speed", coFloat);
    def->label = L("Skirt speed");
    def->full_label = L("Skirt speed");
    def->tooltip = L("Speed of skirt, in mm/s. Zero means use default layer extrusion speed.");
    def->min = 0;
    def->sidetext = L("mm/s");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(50.0));
    
    def = this->add("min_skirt_length", coFloat);
    def->label = L("Skirt minimum extrusion length");
    def->full_label = L("Skirt minimum extrusion length");
    def->tooltip = L("Minimum filament extrusion length in mm when printing the skirt. Zero means this feature is disabled.\n\n"
                     "Using a non zero value is useful if the printer is set up to print without a prime line.\n"
                     "Final number of loops is not taling into account whli arranging or validating objects distance. Increase loop number in such case. ");
    def->min = 0;
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("slow_down_layer_time", coFloats);
    def->label = L("Layer time");
    def->tooltip = L("The printing speed in exported gcode will be slowed down, when the estimated layer time is shorter than this value, to "
                     "get better cooling for these layers");
    def->sidetext = L("s");
    def->min = 0;
    def->max = 1000;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloats { 5.0f });

    def = this->add("minimum_sparse_infill_area", coFloat);
    def->label = L("Minimum sparse infill threshold");
    def->category = L("Strength");
    def->tooltip = L("Sparse infill area which is smaller than threshold value is replaced by internal solid infill");
    def->sidetext = L("mm²");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(15));

    def = this->add("solid_infill_filament", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Solid infill");
    def->category = L("Extruders");
    def->tooltip = L("Filament to print solid infill");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("internal_solid_infill_line_width", coFloatOrPercent);
    def->label = L("Internal solid infill");
    def->category = L("Quality");
    def->tooltip = L("Line width of internal solid infill. If expressed as a %, it will be computed over the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    def = this->add("internal_solid_infill_speed", coFloat);
    def->label = L("Internal solid infill");
    def->category = L("Speed");
    def->tooltip = L("Speed of internal solid infill, not the top and bottom surface");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(100));

    def = this->add("spiral_mode", coBool);
    def->label = L("Spiral vase");
    def->tooltip = L("Spiralize smooths out the z moves of the outer contour. "
                     "And turns a solid model into a single walled print with solid bottom layers. "
                     "The final generated model has no seam");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("spiral_mode_smooth", coBool);
    def->label = L("Smooth Spiral");
    def->tooltip = L("Smooth Spiral smooths out X and Y moves as well, "
                     "resulting in no visible seam at all, even in the XY directions on walls that are not vertical");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("spiral_mode_max_xy_smoothing", coFloatOrPercent);
    def->label = L("Max XY Smoothing");
    def->tooltip = L("Maximum distance to move points in XY to try to achieve a smooth spiral"
                     "If expressed as a %, it will be computed over nozzle diameter");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(200, true));

    def = this->add("timelapse_type", coEnum);
    def->label = L("Timelapse");
    def->tooltip = L("If smooth or traditional mode is selected, a timelapse video will be generated for each print. "
                     "After each layer is printed, a snapshot is taken with the chamber camera. "
                     "All of these snapshots are composed into a timelapse video when printing completes. "
                     "If smooth mode is selected, the toolhead will move to the excess chute after each layer is printed "
                     "and then take a snapshot. "
                     "Since the melt filament may leak from the nozzle during the process of taking a snapshot, "
                     "prime tower is required for smooth mode to wipe nozzle.");
    def->enum_keys_map = &ConfigOptionEnum<TimelapseType>::get_enum_values();
    def->enum_values.emplace_back("0");
    def->enum_values.emplace_back("1");
    def->enum_labels.emplace_back(L("Traditional"));
    def->enum_labels.emplace_back(L("Smooth"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<TimelapseType>(tlTraditional));

    def = this->add("standby_temperature_delta", coInt);
    def->label = L("Temperature variation");
    // TRN PrintSettings : "Ooze prevention" > "Temperature variation"
    def->tooltip = L("Temperature difference to be applied when an extruder is not active. "
                     "The value is not used when 'idle_temperature' in filament settings "
                     "is set to non zero value.");
    def->sidetext = "∆°C";
    def->min = -max_temp;
    def->max = max_temp;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(-5));

    def = this->add("preheat_time", coFloat);
    def->label = L("Preheat time");
    def->tooltip = L("To reduce the waiting time after tool change, Orca can preheat the next tool while the current tool is still in use. "
                     "This setting specifies the time in seconds to preheat the next tool. Orca will insert a M104 command to preheat the tool in advance.");
    def->sidetext = "s";
    def->min = 0;
    def->max = 120;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(30.0));

    def = this->add("preheat_steps", coInt);
    def->label = L("Preheat steps");
    def->tooltip = L("Insert multiple preheat commands(e.g. M104.1). Only useful for Prusa XL. For other printers, please set it to 1.");
    // def->sidetext = "";
    def->min = 1;
    def->max = 10;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInt(1));


    def = this->add("machine_start_gcode", coString);
    def->label = L("Start G-code");
    def->tooltip = L("Start G-code when start the whole printing");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString("G28 ; home all axes\nG1 Z5 F5000 ; lift nozzle\n"));

    def = this->add("filament_start_gcode", coStrings);
    def->label = L("Start G-code");
    def->tooltip = L("Start G-code when start the printing of this filament");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings { " " });

    def = this->add("single_extruder_multi_material", coBool);
    def->label = L("Single Extruder Multi Material");
    def->tooltip = L("Use single nozzle to print multi filament");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("manual_filament_change", coBool);
    def->label = L("Manual Filament Change");
    def->tooltip = L("Enable this option to omit the custom Change filament G-code only at the beginning of the print. "
                    "The tool change command (e.g., T0) will be skipped throughout the entire print. "
                    "This is useful for manual multi-material printing, where we use M600/PAUSE to trigger the manual filament change action.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("purge_in_prime_tower", coBool);
    def->label = L("Purge in prime tower");
    def->tooltip = L("Purge remaining filament into prime tower");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("enable_filament_ramming", coBool);
    def->label = L("Enable filament ramming");
    def->tooltip = L("Enable filament ramming");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));


    def = this->add("wipe_tower_no_sparse_layers", coBool);
    def->label = L("No sparse layers (beta)");
    def->tooltip = L("If enabled, the wipe tower will not be printed on layers with no toolchanges. "
                    "On layers with a toolchange, extruder will travel downward to print the wipe tower. "
                    "User is responsible for ensuring there is no collision with the print.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("single_extruder_multi_material_priming", coBool);
    def->label = L("Prime all printing extruders");
    def->tooltip = L("If enabled, all printing extruders will be primed at the front edge of the print bed at the start of the print.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("slice_closing_radius", coFloat);
    def->label = L("Slice gap closing radius");
    def->category = L("Quality");
    def->tooltip = L("Cracks smaller than 2x gap closing radius are being filled during the triangle mesh slicing. "
        "The gap closing operation may reduce the final print resolution, therefore it is advisable to keep the value reasonably low.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.049));

    def = this->add("slicing_mode", coEnum);
    def->label = L("Slicing Mode");
    def->category = L("Other");
    def->tooltip = L("Use \"Even-odd\" for 3DLabPrint airplane models. Use \"Close holes\" to close all holes in the model.");
    def->enum_keys_map = &ConfigOptionEnum<SlicingMode>::get_enum_values();
    def->enum_values.push_back("regular");
    def->enum_values.push_back("even_odd");
    def->enum_values.push_back("close_holes");
    def->enum_labels.push_back(L("Regular"));
    def->enum_labels.push_back(L("Even-odd"));
    def->enum_labels.push_back(L("Close holes"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SlicingMode>(SlicingMode::Regular));

    def = this->add("z_offset", coFloat);
    def->label = L("Z offset");
    def->tooltip = L("This value will be added (or subtracted) from all the Z coordinates "
                   "in the output G-code. It is used to compensate for bad Z endstop position: "
                   "for example, if your endstop zero actually leaves the nozzle 0.3mm far "
                   "from the print bed, set this to -0.3 (or fix your endstop).");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));
    
    def = this->add("enable_support", coBool);
    //BBS: remove material behind support
    def->label = L("Enable support");
    def->category = L("Support");
    def->tooltip = L("Enable support generation.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_type", coEnum);
    def->label = L("Type");
    def->category = L("Support");
    def->tooltip = L("normal(auto) and tree(auto) is used to generate support automatically. "
                     "If normal(manual) or tree(manual) is selected, only support enforcers are generated");
    def->enum_keys_map = &ConfigOptionEnum<SupportType>::get_enum_values();
    def->enum_values.push_back("normal(auto)");
    def->enum_values.push_back("tree(auto)");
    def->enum_values.push_back("normal(manual)");
    def->enum_values.push_back("tree(manual)");
    def->enum_labels.push_back(L("normal(auto)"));
    def->enum_labels.push_back(L("tree(auto)"));
    def->enum_labels.push_back(L("normal(manual)"));
    def->enum_labels.push_back(L("tree(manual)"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<SupportType>(stNormalAuto));

    def = this->add("support_object_xy_distance", coFloat);
    def->label = L("Support/object xy distance");
    def->category = L("Support");
    def->tooltip = L("XY separation between an object and its support");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 10;
    def->mode = comAdvanced;
    //Support with too small spacing may touch the object and difficult to remove.
    def->set_default_value(new ConfigOptionFloat(0.35));

    def = this->add("support_angle", coFloat);
    def->label = L("Pattern angle");
    def->category = L("Support");
    def->tooltip = L("Use this setting to rotate the support pattern on the horizontal plane.");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 359;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("support_on_build_plate_only", coBool);
    def->label = L("On build plate only");
    def->category = L("Support");
    def->tooltip = L("Don't create support on model surface, only on build plate");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    // BBS
    def           = this->add("support_critical_regions_only", coBool);
    def->label    = L("Support critical regions only");
    def->category = L("Support");
    def->tooltip  = L("Only create support for critical regions including sharp tail, cantilever, etc.");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_remove_small_overhang", coBool);
    def->label = L("Remove small overhangs");
    def->category = L("Support");
    def->tooltip = L("Remove small overhangs that possibly need no supports.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    // BBS: change type to common float.
    // It may be rounded to mulitple layer height when independent_support_layer_height is false.
    def = this->add("support_top_z_distance", coFloat);
    //def->gui_type = ConfigOptionDef::GUIType::f_enum_open;
    def->label = L("Top Z distance");
    def->min = 0;
    def->category = L("Support");
    def->tooltip = L("The z gap between the top support interface and object");
    def->sidetext = L("mm");
//    def->min = 0;
#if 0
    //def->enum_values.push_back("0");
    //def->enum_values.push_back("0.1");
    //def->enum_values.push_back("0.2");
    //def->enum_labels.push_back(L("0 (soluble)"));
    //def->enum_labels.push_back(L("0.1 (semi-detachable)"));
    //def->enum_labels.push_back(L("0.2 (detachable)"));
#endif
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("support_bottom_z_distance", coFloat);
    def->label = L("Bottom Z distance");
    def->category = L("Support");
    def->tooltip = L("The z gap between the bottom support interface and object");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("enforce_support_layers", coInt);
    //def->label = L("Enforce support for the first");
    def->category = L("Support");
    //def->tooltip = L("Generate support material for the specified number of layers counting from bottom, "
    //               "regardless of whether normal support material is enabled or not and regardless "
    //               "of any angle threshold. This is useful for getting more adhesion of objects "
    //               "having a very thin or poor footprint on the build plate.");
    def->sidetext = L("layers");
    //def->full_label = L("Enforce support for the first n layers");
    def->min = 0;
    def->max = 5000;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("support_filament", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label    = L("Support/raft base");
    def->category = L("Support");
    def->tooltip = L("Filament to print support base and raft. \"Default\" means no specific filament for support and current filament is used");
    def->min = 0;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("support_interface_not_for_body",coBool);
    def->label    = L("Avoid interface filament for base");
    def->category = L("Support");
    def->tooltip = L("Avoid using support interface filament to print support base if possible.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("support_line_width", coFloatOrPercent);
    def->label = L("Support");
    def->category = L("Quality");
    def->tooltip = L("Line width of support. If expressed as a %, it will be computed over the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    def = this->add("support_interface_loop_pattern", coBool);
    def->label = L("Interface use loop pattern");
    def->category = L("Support");
    def->tooltip = L("Cover the top contact layer of the supports with loops. Disabled by default.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_interface_filament", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label    = L("Support/raft interface");
    def->category = L("Support");
    def->tooltip = L("Filament to print support interface. \"Default\" means no specific filament for support interface and current filament is used");
    def->min = 0;
    // BBS
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInt(0));

    auto support_interface_top_layers = def = this->add("support_interface_top_layers", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Top interface layers");
    def->category = L("Support");
    def->tooltip = L("Number of top interface layers");
    def->sidetext = L("layers");
    def->min = 0;
    def->enum_values.push_back("0");
    def->enum_values.push_back("1");
    def->enum_values.push_back("2");
    def->enum_values.push_back("3");
    def->enum_labels.push_back("0");
    def->enum_labels.push_back("1");
    def->enum_labels.push_back("2");
    def->enum_labels.push_back("3");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("support_interface_bottom_layers", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Bottom interface layers");
    def->category = L("Support");
    def->tooltip = L("Number of bottom interface layers");
    def->sidetext = L("layers");
    def->min = -1;
    def->enum_values.push_back("-1");
    append(def->enum_values, support_interface_top_layers->enum_values);
    def->enum_labels.push_back(L("Same as top"));
    append(def->enum_labels, support_interface_top_layers->enum_labels);
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("support_interface_spacing", coFloat);
    def->label = L("Top interface spacing");
    def->category = L("Support");
    def->tooltip = L("Spacing of interface lines. Zero means solid interface");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.5));

    //BBS
    def = this->add("support_bottom_interface_spacing", coFloat);
    def->label = L("Bottom interface spacing");
    def->category = L("Support");
    def->tooltip = L("Spacing of bottom interface lines. Zero means solid interface");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.5));

    def = this->add("support_interface_speed", coFloat);
    def->label = L("Support interface");
    def->category = L("Speed");
    def->tooltip = L("Speed of support interface");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(80));

    def = this->add("support_base_pattern", coEnum);
    def->label = L("Base pattern");
    def->category = L("Support");
    def->tooltip = L("Line pattern of support");
    def->enum_keys_map = &ConfigOptionEnum<SupportMaterialPattern>::get_enum_values();
    def->enum_values.push_back("default");
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("rectilinear-grid");
    def->enum_values.push_back("honeycomb");
    def->enum_values.push_back("lightning");
    def->enum_values.push_back("hollow");
    def->enum_labels.push_back(L("Default"));
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Rectilinear grid"));
    def->enum_labels.push_back(L("Honeycomb"));
    def->enum_labels.push_back(L("Lightning"));
    def->enum_labels.push_back(L("Hollow"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SupportMaterialPattern>(smpDefault));

    def = this->add("support_interface_pattern", coEnum);
    def->label = L("Interface pattern");
    def->category = L("Support");
    def->tooltip = L("Line pattern of support interface. "
                     "Default pattern for non-soluble support interface is Rectilinear, "
                     "while default pattern for soluble support interface is Concentric");
    def->enum_keys_map = &ConfigOptionEnum<SupportMaterialInterfacePattern>::get_enum_values();
    def->enum_values.push_back("auto");
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("rectilinear_interlaced");
    def->enum_values.push_back("grid");
    def->enum_labels.push_back(L("Default"));
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Rectilinear Interlaced"));
    def->enum_labels.push_back(L("Grid"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SupportMaterialInterfacePattern>(smipAuto));

    def = this->add("support_base_pattern_spacing", coFloat);
    def->label = L("Base pattern spacing");
    def->category = L("Support");
    def->tooltip = L("Spacing between support lines");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(2.5));

    def = this->add("support_expansion", coFloat);
    def->label = L("Normal Support expansion");
    def->category = L("Support");
    def->tooltip = L("Expand (+) or shrink (-) the horizontal span of normal support");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("support_speed", coFloat);
    def->label = L("Support");
    def->category = L("Speed");
    def->tooltip = L("Speed of support");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(80));

    def = this->add("support_style", coEnum);
    def->label = L("Style");
    def->category = L("Support");
    def->tooltip = L("Style and shape of the support. For normal support, projecting the supports into a regular grid "
                     "will create more stable supports (default), while snug support towers will save material and reduce "
                     "object scarring.\n"
                     "For tree support, slim and organic style will merge branches more aggressively and save "
                     "a lot of material (default organic), while hybrid style will create similar structure to normal support "
                     "under large flat overhangs.");
    def->enum_keys_map = &ConfigOptionEnum<SupportMaterialStyle>::get_enum_values();
    def->enum_values.push_back("default");
    def->enum_values.push_back("grid");
    def->enum_values.push_back("snug");
    def->enum_values.push_back("organic");
    def->enum_values.push_back("tree_slim");
    def->enum_values.push_back("tree_strong");
    def->enum_values.push_back("tree_hybrid");
    def->enum_labels.push_back(L("Default (Grid/Organic"));
    def->enum_labels.push_back(L("Grid"));
    def->enum_labels.push_back(L("Snug"));
    def->enum_labels.push_back(L("Organic"));
    def->enum_labels.push_back(L("Tree Slim"));
    def->enum_labels.push_back(L("Tree Strong"));
    def->enum_labels.push_back(L("Tree Hybrid"));

    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SupportMaterialStyle>(smsDefault));

    def = this->add("independent_support_layer_height", coBool);
    def->label = L("Independent support layer height");
    def->category = L("Support");
    def->tooltip = L("Support layer uses layer height independent with object layer. This is to support customizing z-gap and save print time."
                     "This option will be invalid when the prime tower is enabled.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("support_threshold_angle", coInt);
    def->label = L("Threshold angle");
    def->category = L("Support");
    def->tooltip = L("Support will be generated for overhangs whose slope angle is below the threshold.");
    def->sidetext = L("°");
    def->min = 1;
    def->max = 90;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionInt(30));

    def = this->add("tree_support_branch_angle", coFloat);
    def->label = L("Tree support branch angle");
    def->category = L("Support");
    def->tooltip = L("This setting determines the maximum overhang angle that t he branches of tree support allowed to make."
                     "If the angle is increased, the branches can be printed more horizontally, allowing them to reach farther.");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 60;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(40.));

    def = this->add("tree_support_branch_angle_organic", coFloat);
    def->label = L("Tree support branch angle");
    def->category = L("Support");
    def->tooltip = L("This setting determines the maximum overhang angle that t he branches of tree support allowed to make."
                     "If the angle is increased, the branches can be printed more horizontally, allowing them to reach farther.");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 60;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(40.));

    def = this->add("tree_support_angle_slow", coFloat);
    def->label = L("Preferred Branch Angle");
    def->category = L("Support");
    // TRN PrintSettings: "Organic supports" > "Preferred Branch Angle"
    def->tooltip = L("The preferred angle of the branches, when they do not have to avoid the model. "
                     "Use a lower angle to make them more vertical and more stable. Use a higher angle for branches to merge faster.");
    def->sidetext = L("°");
    def->min = 10;
    def->max = 85;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(25));

    def           = this->add("tree_support_branch_distance", coFloat);
    def->label    = L("Tree support branch distance");
    def->category = L("Support");
    def->tooltip  = L("This setting determines the distance between neighboring tree support nodes.");
    def->sidetext = L("mm");
    def->min      = 1.0;
    def->max      = 10;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.));

    def           = this->add("tree_support_branch_distance_organic", coFloat);
    def->label    = L("Tree support branch distance");
    def->category = L("Support");
    def->tooltip  = L("This setting determines the distance between neighboring tree support nodes.");
    def->sidetext = L("mm");
    def->min      = 1.0;
    def->max      = 10;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("tree_support_top_rate", coPercent);
    def->label = L("Branch Density");
    def->category = L("Support");
    // TRN PrintSettings: "Organic supports" > "Branch Density"
    def->tooltip = L("Adjusts the density of the support structure used to generate the tips of the branches. "
                     "A higher value results in better overhangs but the supports are harder to remove, "
                     "thus it is recommended to enable top support interfaces instead of a high branch density value "
                     "if dense interfaces are needed.");
    def->sidetext = L("%");
    def->min = 5;
    def->max_literal = 35;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(30));

    def = this->add("tree_support_adaptive_layer_height", coBool);
    def->label = L("Adaptive layer height");
    def->category = L("Quality");
    def->tooltip = L("Enabling this option means the height of  tree support layer except the first will be automatically calculated ");
    def->set_default_value(new ConfigOptionBool(1));
    
    def = this->add("tree_support_auto_brim", coBool);
    def->label = L("Auto brim width");
    def->category = L("Quality");
    def->tooltip = L("Enabling this option means the width of the brim for tree support will be automatically calculated");
    def->set_default_value(new ConfigOptionBool(1));
    
    def = this->add("tree_support_brim_width", coFloat);
    def->label = L("Tree support brim width");
    def->category = L("Quality");
    def->min      = 0.0;
    def->tooltip = L("Distance from tree branch to the outermost brim line");
    def->set_default_value(new ConfigOptionFloat(3));

    def = this->add("tree_support_tip_diameter", coFloat);
    def->label = L("Tip Diameter");
    def->category = L("Support");
    // TRN PrintSettings: "Organic supports" > "Tip Diameter"
    def->tooltip = L("Branch tip diameter for organic supports.");
    def->sidetext = L("mm");
    def->min = 0.1f;
    def->max = 100.f;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.8));

    def           = this->add("tree_support_branch_diameter", coFloat);
    def->label    = L("Tree support branch diameter");
    def->category = L("Support");
    def->tooltip  = L("This setting determines the initial diameter of support nodes.");
    def->sidetext = L("mm");
    def->min      = 1.0;
    def->max      = 10;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.));

    def           = this->add("tree_support_branch_diameter_organic", coFloat);
    def->label    = L("Tree support branch diameter");
    def->category = L("Support");
    def->tooltip  = L("This setting determines the initial diameter of support nodes.");
    def->sidetext = L("mm");
    def->min      = 1.0;
    def->max      = 10;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(2.));

    def = this->add("tree_support_branch_diameter_angle", coFloat);
    // TRN PrintSettings: #lmFIXME 
    def->label = L("Branch Diameter Angle");
    def->category = L("Support");
    // TRN PrintSettings: "Organic supports" > "Branch Diameter Angle"
    def->tooltip = L("The angle of the branches' diameter as they gradually become thicker towards the bottom. "
                     "An angle of 0 will cause the branches to have uniform thickness over their length. "
                     "A bit of an angle can increase stability of the organic support.");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 15;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5));

    def = this->add("tree_support_branch_diameter_double_wall", coFloat);
    def->label = L("Branch Diameter with double walls");
    def->category = L("Support");
    // TRN PrintSettings: "Organic supports" > "Branch Diameter"
    def->tooltip = L("Branches with area larger than the area of a circle of this diameter will be printed with double walls for stability. "
                     "Set this value to zero for no double walls.");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 100.f;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(3.));

    def = this->add("tree_support_wall_count", coInt);
    def->label = L("Support wall loops");
    def->category = L("Support");
    def->tooltip = L("This setting specify the count of walls around support");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("tree_support_with_infill", coBool);
    def->label = L("Tree support with infill");
    def->category = L("Support");
    def->tooltip = L("This setting specifies whether to add infill inside large hollows of tree support");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("activate_chamber_temp_control",coBools);
    def->label = L("Activate temperature control");
    def->tooltip = L("Enable this option for automated chamber temperature control. This option activates the emitting of an M191 command before the \"machine_start_gcode\"\n which sets the "
                     "chamber temperature and waits until it is reached. In addition, it emits an M141 command at the end of the print to turn off the chamber heater, if present. \n\n"
                     "This option relies on the firmware supporting the M191 and M141 commands either via macros or natively and is usually used when an active chamber heater is installed.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBools{false});

    def = this->add("chamber_temperature", coInts);
    def->label = L("Chamber temperature");
    def->tooltip = L("For high-temperature materials like ABS, ASA, PC, and PA, a higher chamber temperature can help suppress or reduce warping and potentially lead to higher interlayer bonding strength. "
                     "However, at the same time, a higher chamber temperature will reduce the efficiency of air filtration for ABS and ASA. \n\n"
                     "For PLA, PETG, TPU, PVA, and other low-temperature materials, this option should be disabled (set to 0) as the chamber temperature should be low to avoid extruder clogging caused "
                     "by material softening at the heat break.\n\n"
                     "If enabled, this parameter also sets a gcode variable named chamber_temperature, which can be used to pass the desired chamber temperature to your print start macro, "
                     "or a heat soak macro like this: PRINT_START (other variables) CHAMBER_TEMP=[chamber_temperature]. This may be useful if your printer does not support M141/M191 commands, or if you desire "
                     "to handle heat soaking in the print start macro if no active chamber heater is installed."
                    );
    def->sidetext = L("°C");
    def->full_label = L("Chamber temperature");
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts{0});

    def = this->add("nozzle_temperature", coInts);
    def->label = L("Other layers");
    def->tooltip = L("Nozzle temperature for layers after the initial one");
    def->sidetext = L("°C");
    def->full_label = L("Nozzle temperature");
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts { 200 });

    def = this->add("nozzle_temperature_range_low", coInts);
    def->label = L("Min");
    //def->tooltip = "";
    def->sidetext = L("°C");
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts { 190 });

    def = this->add("nozzle_temperature_range_high", coInts);
    def->label = L("Max");
    //def->tooltip = "";
    def->sidetext = L("°C");
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts { 240 });

    def = this->add("head_wrap_detect_zone", coPoints);
    def->label = "Head wrap detect zone"; //do not need translation
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionPoints{});

    def = this->add("detect_thin_wall", coBool);
    def->label = L("Detect thin wall");
    def->category = L("Strength");
    def->tooltip = L("Detect thin wall which can't contain two line width. And use single line to print. "
                     "Maybe printed not very well, because it's not closed loop");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("change_filament_gcode", coString);
    def->label = L("Change filament G-code");
    def->tooltip = L("This gcode is inserted when change filament, including T command to trigger tool change");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("change_extrusion_role_gcode", coString);
    def->label = L("Change extrusion role G-code");
    def->tooltip = L("This gcode is inserted when the extrusion role is changed");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("top_surface_line_width", coFloatOrPercent);
    def->label = L("Top surface");
    def->category = L("Quality");
    def->tooltip = L("Line width for top surfaces. If expressed as a %, it will be computed over the nozzle diameter.");
    def->sidetext = L("mm or %");
    def->ratio_over = "nozzle_diameter";
    def->min = 0;
    def->max = 1000;
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    def = this->add("top_surface_speed", coFloat);
    def->label = L("Top surface");
    def->category = L("Speed");
    def->tooltip = L("Speed of top surface infill which is solid");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(100));

    def = this->add("top_shell_layers", coInt);
    def->label = L("Top shell layers");
    def->category = L("Strength");
    def->sidetext = L("layers"); // ORCA add side text
    def->tooltip = L("This is the number of solid layers of top shell, including the top "
                     "surface layer. When the thickness calculated by this value is thinner "
                     "than top shell thickness, the top shell layers will be increased");
    def->full_label = L("Top solid layers");
    def->min = 0;
    def->set_default_value(new ConfigOptionInt(4));

    def = this->add("top_shell_thickness", coFloat);
    def->label = L("Top shell thickness");
    def->category = L("Strength");
    def->tooltip = L("The number of top solid layers is increased when slicing if the thickness calculated by top shell layers is "
                     "thinner than this value. This can avoid having too thin shell when layer height is small. 0 means that "
                     "this setting is disabled and thickness of top shell is absolutely determined by top shell layers");
    def->full_label = L("Top shell thickness");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.6));

    def = this->add("travel_speed", coFloat);
    def->label = L("Travel");
    def->tooltip = L("Speed of travel which is faster and without extrusion");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(120));

    def = this->add("travel_speed_z", coFloat);
    //def->label = L("Z travel");
    //def->tooltip = L("Speed of vertical travel along z axis. "
    //                 "This is typically lower because build plate or gantry is hard to be moved. "
    //                 "Zero means using travel speed directly in gcode, but will be limited by printer's ability when run gcode");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("wipe", coBools);
    def->label = L("Wipe while retracting");
    def->tooltip = L("Move nozzle along the last extrusion path when retracting to clean leaked material on nozzle. "
                     "This can minimize blob when print new part after travel");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("wipe_distance", coFloats);
    def->label = L("Wipe Distance");
    def->tooltip = L("Describe how long the nozzle will move along the last path when retracting. \n\nDepending on how long the wipe operation lasts, how fast and long the extruder/filament retraction settings are, a retraction move may be needed to retract the remaining filament. \n\nSetting a value in the retract amount before wipe setting below will perform any excess retraction before the wipe, else it will be performed after.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 1. });

    def = this->add("enable_prime_tower", coBool);
    def->label = L("Enable");
    def->tooltip = L("The wiping tower can be used to clean up the residue on the nozzle and stabilize the chamber pressure inside the nozzle, "
                    "in order to avoid appearance defects when printing objects.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("flush_volumes_vector", coFloats);
    // BBS: remove _L()
    def->label = ("Purging volumes - load/unload volumes");
    //def->tooltip = L("This vector saves required volumes to change from/to each tool used on the "
    //                 "wipe tower. These values are used to simplify creation of the full purging "
    //                 "volumes below.");

    // BBS: change 70.f => 140.f
    def->set_default_value(new ConfigOptionFloats { 140.f, 140.f, 140.f, 140.f, 140.f, 140.f, 140.f, 140.f });

    def = this->add("flush_volumes_matrix", coFloats);
    def->label = L("Purging volumes");
    //def->tooltip = L("This matrix describes volumes (in cubic milimetres) required to purge the"
    //                 " new filament on the wipe tower for any given pair of tools.");
    // BBS: change 140.f => 280.f
    def->set_default_value(new ConfigOptionFloats {   0.f, 280.f, 280.f, 280.f,
                                                    280.f,   0.f, 280.f, 280.f,
                                                    280.f, 280.f,   0.f, 280.f,
                                                    280.f, 280.f, 280.f,   0.f });

    def = this->add("flush_multiplier", coFloat);
    def->label = L("Flush multiplier");
    def->tooltip = L("The actual flushing volumes is equal to the flush multiplier multiplied by the flushing volumes in the table.");
    def->sidetext = "";
    def->set_default_value(new ConfigOptionFloat(0.3));

    // BBS
    def = this->add("prime_volume", coFloat);
    def->label = L("Prime volume");
    def->tooltip = L("The volume of material to prime extruder on tower.");
    def->sidetext = L("mm³");
    def->min = 1.0;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(45.));

    def = this->add("wipe_tower_x", coFloats);
    //def->label = L("Position X");
    //def->tooltip = L("X coordinate of the left front corner of a wipe tower");
    //def->sidetext = L("mm");
    def->mode = comDevelop;
    // BBS: change data type to floats to add partplate logic
    def->set_default_value(new ConfigOptionFloats{ 15. });

    def = this->add("wipe_tower_y", coFloats);
    //def->label = L("Position Y");
    //def->tooltip = L("Y coordinate of the left front corner of a wipe tower");
    //def->sidetext = L("mm");
    def->mode = comDevelop;
    // BBS: change data type to floats to add partplate logic
    def->set_default_value(new ConfigOptionFloats{ 220. });

    def = this->add("prime_tower_width", coFloat);
    def->label = L("Width");
    def->tooltip = L("Width of prime tower");
    def->sidetext = L("mm");
    def->min = 2.0;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(60.));

    def = this->add("wipe_tower_rotation_angle", coFloat);
    def->label = L("Wipe tower rotation angle");
    def->tooltip = L("Wipe tower rotation angle with respect to x-axis.");
    def->sidetext = L("°");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("prime_tower_brim_width", coFloat);
    def->label = L("Brim width");
    def->tooltip = L("Brim width");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->min = 0.;
    def->set_default_value(new ConfigOptionFloat(3.));

    def = this->add("wipe_tower_cone_angle", coFloat);
    def->label = L("Stabilization cone apex angle");
    def->tooltip = L("Angle at the apex of the cone that is used to stabilize the wipe tower. "
                     "Larger angle means wider base.");
    def->sidetext = L("°");
    def->mode = comAdvanced;
    def->min = 0.;
    def->max = 90.;
    def->set_default_value(new ConfigOptionFloat(0.));
    
    def = this->add("wipe_tower_max_purge_speed", coFloat);
    def->label = L("Maximum wipe tower print speed");
    def->tooltip = L("The maximum print speed when purging in the wipe tower and printing the wipe tower sparse layers. "
                     "When purging, if the sparse infill speed or calculated speed from the filament max volumetric speed is lower, the lowest will be used instead.\n\n"
                     "When printing the sparse layers, if the internal perimeter speed or calculated speed from the filament max volumetric speed is lower, the lowest will be used instead.\n\n"
                     "Increasing this speed may affect the tower's stability as well as increase the force with which the nozzle collides with any blobs that may have formed on the wipe tower.\n\n"
                     "Before increasing this parameter beyond the default of 90mm/sec, make sure your printer can reliably bridge at the increased speeds and that ooze when tool changing is well controlled.\n\n"
                     "For the wipe tower external perimeters the internal perimeter speed is used regardless of this setting.");
    def->sidetext = L("mm/s");
    def->mode = comAdvanced;
    def->min = 10;
    def->set_default_value(new ConfigOptionFloat(90.));

    def = this->add("wipe_tower_filament", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Wipe tower");
    def->category = L("Extruders");
    def->tooltip = L("The extruder to use when printing perimeter of the wipe tower. "
                     "Set to 0 to use the one that is available (non-soluble would be preferred).");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("wiping_volumes_extruders", coFloats);
    def->label = L("Purging volumes - load/unload volumes");
    def->tooltip = L("This vector saves required volumes to change from/to each tool used on the "
                     "wipe tower. These values are used to simplify creation of the full purging "
                     "volumes below.");
    def->set_default_value(new ConfigOptionFloats { 70., 70., 70., 70., 70., 70., 70., 70., 70., 70.  });

    def = this->add("flush_into_infill", coBool);
    def->category = L("Flush options");
    def->label = L("Flush into objects' infill");
    def->tooltip = L("Purging after filament change will be done inside objects' infills. "
        "This may lower the amount of waste and decrease the print time. "
        "If the walls are printed with transparent filament, the mixed color infill will be seen outside. "
        "It will not take effect, unless the prime tower is enabled.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("flush_into_support", coBool);
    def->category = L("Flush options");
    def->label = L("Flush into objects' support");
    def->tooltip = L("Purging after filament change will be done inside objects' support. "
        "This may lower the amount of waste and decrease the print time. "
        "It will not take effect, unless the prime tower is enabled.");
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("flush_into_objects", coBool);
    def->category = L("Flush options");
    def->label = L("Flush into this object");
    def->tooltip = L("This object will be used to purge the nozzle after a filament change to save filament and decrease the print time. "
        "Colours of the objects will be mixed as a result. "
        "It will not take effect, unless the prime tower is enabled.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wipe_tower_bridging", coFloat);
    def->label = L("Maximal bridging distance");
    def->tooltip = L("Maximal distance between supports on sparse infill sections.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10.));

    def = this->add("wipe_tower_extra_spacing", coPercent);
    def->label = L("Wipe tower purge lines spacing");
    def->tooltip = L("Spacing of purge lines on the wipe tower.");
    def->sidetext = L("%");
    def->mode = comAdvanced;
    def->min = 100.;
    def->max = 300.;
    def->set_default_value(new ConfigOptionPercent(100.));

    def = this->add("wipe_tower_extra_flow", coPercent);
    def->label = L("Extra flow for purging");
    def->tooltip = L("Extra flow used for the purging lines on the wipe tower. This makes the purging lines thicker or narrower "
                     "than they normally would be. The spacing is adjusted automatically.");
    def->sidetext = L("%");
    def->mode = comAdvanced;
    def->min = 100.;
    def->max = 300.;
    def->set_default_value(new ConfigOptionPercent(100.));

    def = this->add("idle_temperature", coInts);
    def->label = L("Idle temperature");
    def->tooltip = L("Nozzle temperature when the tool is currently not used in multi-tool setups."
                     "This is only used when 'Ooze prevention' is active in Print Settings. Set to 0 to disable.");
    def->sidetext = L("°C");
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts{0});

    def = this->add("xy_hole_compensation", coFloat);
    def->label = L("X-Y hole compensation");
    def->category = L("Quality");
    def->tooltip = L("Holes of object will be grown or shrunk in XY plane by the configured value. "
                     "Positive value makes holes bigger. Negative value makes holes smaller. "
                     "This function is used to adjust size slightly when the object has assembling issue");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("xy_contour_compensation", coFloat);
    def->label = L("X-Y contour compensation");
    def->category = L("Quality");
    def->tooltip = L("Contour of object will be grown or shrunk in XY plane by the configured value. "
                     "Positive value makes contour bigger. Negative value makes contour smaller. "
                     "This function is used to adjust size slightly when the object has assembling issue");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("hole_to_polyhole", coBool);
    def->label = L("Convert holes to polyholes");
    def->category = L("Quality");
    def->tooltip = L("Search for almost-circular holes that span more than one layer and convert the geometry to polyholes."
                     " Use the nozzle size and the (biggest) diameter to compute the polyhole."
                     "\nSee http://hydraraptor.blogspot.com/2011/02/polyholes.html");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("hole_to_polyhole_threshold", coFloatOrPercent);
    def->label = L("Polyhole detection margin");
    def->category = L("Quality");
    // xgettext:no-c-format, no-boost-format
    def->tooltip = L("Maximum defection of a point to the estimated radius of the circle."
                     "\nAs cylinders are often exported as triangles of varying size, points may not be on the circle circumference."
                     " This setting allows you some leeway to broaden the detection."
                     "\nIn mm or in % of the radius.");
    def->sidetext = L("mm or %");
    def->max_literal = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0.01, false));

    def = this->add("hole_to_polyhole_twisted", coBool);
    def->label = L("Polyhole twist");
    def->category = L("Quality");
    def->tooltip = L("Rotate the polyhole every layer.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("thumbnails", coString);
    def->label = L("G-code thumbnails");
    def->tooltip = L("Picture sizes to be stored into a .gcode and .sl1 / .sl1s files, in the following format: \"XxY, XxY, ...\"");
    def->mode = comAdvanced;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionString("48x48/PNG,300x300/PNG"));

    def = this->add("thumbnails_format", coEnum);
    def->label = L("Format of G-code thumbnails");
    def->tooltip = L("Format of G-code thumbnails: PNG for best quality, JPG for smallest size, QOI for low memory firmware");
    def->mode = comAdvanced;
    def->enum_keys_map = &ConfigOptionEnum<GCodeThumbnailsFormat>::get_enum_values();
    def->enum_values.push_back("PNG");
    def->enum_values.push_back("JPG");
    def->enum_values.push_back("QOI");
    def->enum_values.push_back("BTT_TFT");
    def->enum_values.push_back("COLPIC");
    def->enum_labels.push_back("PNG");
    def->enum_labels.push_back("JPG");
    def->enum_labels.push_back("QOI");
    def->enum_labels.push_back("BTT TT");
    def->enum_labels.push_back("ColPic");
    def->set_default_value(new ConfigOptionEnum<GCodeThumbnailsFormat>(GCodeThumbnailsFormat::PNG));

    def = this->add("use_relative_e_distances", coBool);
    def->label = L("Use relative E distances");
    def->tooltip = L("Relative extrusion is recommended when using \"label_objects\" option."
                   "Some extruders work better with this option unchecked (absolute extrusion mode). "
                   "Wipe tower is only compatible with relative mode. It is recommended on "
                   "most printers. Default is checked");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("wall_generator", coEnum);
    def->label = L("Wall generator");
    def->category = L("Quality");
    def->tooltip = L("Classic wall generator produces walls with constant extrusion width and for "
        "very thin areas is used gap-fill. "
        "Arachne engine produces walls with variable extrusion width");
    def->enum_keys_map = &ConfigOptionEnum<PerimeterGeneratorType>::get_enum_values();
    def->enum_values.push_back("classic");
    def->enum_values.push_back("arachne");
    def->enum_labels.push_back(L("Classic"));
    def->enum_labels.push_back(L("Arachne"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<PerimeterGeneratorType>(PerimeterGeneratorType::Arachne));

    def = this->add("wall_transition_length", coPercent);
    def->label = L("Wall transition length");
    def->category = L("Quality");
    def->tooltip = L("When transitioning between different numbers of walls as the part becomes "
        "thinner, a certain amount of space is allotted to split or join the wall segments. "
        "It's expressed as a percentage over nozzle diameter");
    def->sidetext = L("%");
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("wall_transition_filter_deviation", coPercent);
    def->label = L("Wall transitioning filter margin");
    def->category = L("Quality");
    def->tooltip = L("Prevent transitioning back and forth between one extra wall and one less. This "
        "margin extends the range of extrusion widths which follow to [Minimum wall width "
        "- margin, 2 * Minimum wall width + margin]. Increasing this margin "
        "reduces the number of transitions, which reduces the number of extrusion "
        "starts/stops and travel time. However, large extrusion width variation can lead to "
        "under- or overextrusion problems. "
        "It's expressed as a percentage over nozzle diameter");
    def->sidetext = L("%");
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionPercent(25));

    def = this->add("wall_transition_angle", coFloat);
    def->label = L("Wall transitioning threshold angle");
    def->category = L("Quality");
    def->tooltip = L("When to create transitions between even and odd numbers of walls. A wedge shape with"
        " an angle greater than this setting will not have transitions and no walls will be "
        "printed in the center to fill the remaining space. Reducing this setting reduces "
        "the number and length of these center walls, but may leave gaps or overextrude");
    def->sidetext = L("°");
    def->mode = comAdvanced;
    def->min = 1.;
    def->max = 59.;
    def->set_default_value(new ConfigOptionFloat(10.));

    def = this->add("wall_distribution_count", coInt);
    def->label = L("Wall distribution count");
    def->category = L("Quality");
    def->tooltip = L("The number of walls, counted from the center, over which the variation needs to be "
        "spread. Lower values mean that the outer walls don't change in width");
    def->mode = comAdvanced;
    def->min = 1;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("min_feature_size", coPercent);
    def->label = L("Minimum feature size");
    def->category = L("Quality");
    def->tooltip = L("Minimum thickness of thin features. Model features that are thinner than this value will "
        "not be printed, while features thicker than the Minimum feature size will be widened to "
        "the Minimum wall width. "
        "It's expressed as a percentage over nozzle diameter");
    def->sidetext = L("%");
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionPercent(25));

    def = this->add("min_length_factor", coFloat);
    def->label = L("Minimum wall length");
    def->category = L("Quality");
    def->tooltip = L("Adjust this value to prevent short, unclosed walls from being printed, which could increase print time. "
    "Higher values remove more and longer walls.\n\n"
    "NOTE: Bottom and top surfaces will not be affected by this value to prevent visual gaps on the outside of the model. "
    "Adjust 'One wall threshold' in the Advanced settings below to adjust the sensitivity of what is considered a top-surface. "
    "'One wall threshold' is only visible if this setting is set above the default value of 0.5, or if single-wall top surfaces is enabled.");
    def->sidetext = L("mm"); // ORCA add side text
    def->mode = comAdvanced;
    def->min = 0.0;
    def->max = 25.0;
    def->set_default_value(new ConfigOptionFloat(0.5));

    def = this->add("initial_layer_min_bead_width", coPercent);
    def->label = L("First layer minimum wall width");
    def->category = L("Quality");
    def->tooltip = L("The minimum wall width that should be used for the first layer is recommended to be set "
                     "to the same size as the nozzle. This adjustment is expected to enhance adhesion.");
    def->sidetext = L("%");
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionPercent(85));

    def = this->add("min_bead_width", coPercent);
    def->label = L("Minimum wall width");
    def->category = L("Quality");
    def->tooltip = L("Width of the wall that will replace thin features (according to the Minimum feature size) "
        "of the model. If the Minimum wall width is thinner than the thickness of the feature,"
        " the wall will become as thick as the feature itself. "
        "It's expressed as a percentage over nozzle diameter");
    def->sidetext = L("%");
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionPercent(85));

    // Declare retract values for filament profile, overriding the printer's extruder profile.
    for (const char *opt_key : {
        // floats
        "retraction_length", "z_hop", "z_hop_types", "retract_lift_above", "retract_lift_below", "retract_lift_enforce", "retraction_speed", "deretraction_speed", "retract_restart_extra", "retraction_minimum_travel",
        // BBS: floats
        "wipe_distance",
        // bools
        "retract_when_changing_layer", "retract_on_top_layer", "wipe",
        // percents
        "retract_before_wipe",
        "long_retractions_when_cut",
        "retraction_distances_when_cut"
        }) {
        auto it_opt = options.find(opt_key);
        assert(it_opt != options.end());
        def = this->add_nullable(std::string("filament_") + opt_key, it_opt->second.type);
        def->label 		= it_opt->second.label;
        def->full_label = it_opt->second.full_label;
        def->tooltip 	= it_opt->second.tooltip;
        def->sidetext   = it_opt->second.sidetext;
        def->enum_keys_map = it_opt->second.enum_keys_map;
        def->enum_labels   = it_opt->second.enum_labels;
        def->enum_values   = it_opt->second.enum_values;
        def->min        = it_opt->second.min;
        def->max        = it_opt->second.max;
        //BBS: shown specific filament retract config because we hide the machine retract into comDevelop mode
        if ((strcmp(opt_key, "retraction_length") == 0) ||
            (strcmp(opt_key, "z_hop") == 0)||
            (strcmp(opt_key, "long_retractions_when_cut") == 0)||
            (strcmp(opt_key, "retraction_distances_when_cut") == 0))
            def->mode       = comSimple;
        else
            def->mode       = comAdvanced;
        switch (def->type) {
        case coFloats   : def->set_default_value(new ConfigOptionFloatsNullable  (static_cast<const ConfigOptionFloats*  >(it_opt->second.default_value.get())->values)); break;
        case coPercents : def->set_default_value(new ConfigOptionPercentsNullable(static_cast<const ConfigOptionPercents*>(it_opt->second.default_value.get())->values)); break;
        case coBools    : def->set_default_value(new ConfigOptionBoolsNullable   (static_cast<const ConfigOptionBools*   >(it_opt->second.default_value.get())->values)); break;
        case coEnums    : def->set_default_value(new ConfigOptionEnumsGenericNullable(static_cast<const ConfigOptionEnumsGeneric*   >(it_opt->second.default_value.get())->values)); break;
        default: assert(false);
        }
    }

    def = this->add("detect_narrow_internal_solid_infill", coBool);
    def->label = L("Detect narrow internal solid infill");
    def->category = L("Strength");
    def->tooltip = L("This option will auto detect narrow internal solid infill area."
                   " If enabled, concentric pattern will be used for the area to speed printing up."
                   " Otherwise, rectilinear pattern is used by default.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));
}

void PrintConfigDef::init_extruder_option_keys()
{
    // ConfigOptionFloats, ConfigOptionPercents, ConfigOptionBools, ConfigOptionStrings
    m_extruder_option_keys = {
        "nozzle_diameter", "min_layer_height", "max_layer_height", "extruder_offset",
        "retraction_length", "z_hop", "z_hop_types", "travel_slope", "retract_lift_above", "retract_lift_below", "retract_lift_enforce", "retraction_speed", "deretraction_speed",
        "retract_before_wipe", "retract_restart_extra", "retraction_minimum_travel", "wipe", "wipe_distance",
        "retract_when_changing_layer", "retract_on_top_layer", "retract_length_toolchange", "retract_restart_extra_toolchange", "extruder_colour",
        "default_filament_profile","retraction_distances_when_cut","long_retractions_when_cut"
    };

    m_extruder_retract_keys = {
        "deretraction_speed",
        "long_retractions_when_cut",
        "retract_before_wipe",
        "retract_lift_above",
        "retract_lift_below",
        "retract_lift_enforce",
        "retract_on_top_layer",
        "retract_restart_extra",
        "retract_when_changing_layer",
        "retraction_distances_when_cut",
        "retraction_length",
        "retraction_minimum_travel",
        "retraction_speed",
        "travel_slope",
        "wipe",
        "wipe_distance",
        "z_hop",
        "z_hop_types"
    };
    assert(std::is_sorted(m_extruder_retract_keys.begin(), m_extruder_retract_keys.end()));
}

void PrintConfigDef::init_filament_option_keys()
{
    m_filament_option_keys = {
        "filament_diameter", "min_layer_height", "max_layer_height",
        "retraction_length", "z_hop", "z_hop_types", "retract_lift_above", "retract_lift_below", "retract_lift_enforce", "retraction_speed", "deretraction_speed",
        "retract_before_wipe", "retract_restart_extra", "retraction_minimum_travel", "wipe", "wipe_distance",
        "retract_when_changing_layer", "retract_on_top_layer", "retract_length_toolchange", "retract_restart_extra_toolchange", "filament_colour",
        "default_filament_profile","retraction_distances_when_cut","long_retractions_when_cut"/*,"filament_seam_gap"*/
    };

    m_filament_retract_keys = {
        "deretraction_speed",
        "long_retractions_when_cut",
        "retract_before_wipe",
        "retract_lift_above",
        "retract_lift_below",
        "retract_lift_enforce",
        "retract_restart_extra",
        "retract_when_changing_layer",
        "retract_on_top_layer",
        "retraction_distances_when_cut",
        "retraction_length",
        "retraction_minimum_travel",
        "retraction_speed",
        "wipe",
        "wipe_distance",
        "z_hop",
        "z_hop_types"
    };
    assert(std::is_sorted(m_filament_retract_keys.begin(), m_filament_retract_keys.end()));
}

void PrintConfigDef::init_sla_params()
{
    ConfigOptionDef* def;

    // SLA Printer settings

    def = this->add("display_width", coFloat);
    def->label = " ";
    def->tooltip = " ";
    def->min = 1;
    def->set_default_value(new ConfigOptionFloat(120.));

    def = this->add("display_height", coFloat);
    def->label = " ";
    def->tooltip = " ";
    def->min = 1;
    def->set_default_value(new ConfigOptionFloat(68.));

    def = this->add("display_pixels_x", coInt);
    def->full_label = " ";
    def->label = ("X");
    def->tooltip = " ";
    def->min = 100;
    def->set_default_value(new ConfigOptionInt(2560));

    def = this->add("display_pixels_y", coInt);
    def->label = ("Y");
    def->tooltip = " ";
    def->min = 100;
    def->set_default_value(new ConfigOptionInt(1440));

    def = this->add("display_mirror_x", coBool);
    def->full_label = " ";
    def->label = " ";
    def->tooltip = " ";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("display_mirror_y", coBool);
    def->full_label = " ";
    def->label = " ";
    def->tooltip = " ";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("display_orientation", coEnum);
    def->label = " ";
    def->tooltip = " ";
    def->enum_keys_map = &ConfigOptionEnum<SLADisplayOrientation>::get_enum_values();
    def->enum_values.push_back("landscape");
    def->enum_values.push_back("portrait");
    def->enum_labels.push_back(" ");
    def->enum_labels.push_back(" ");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SLADisplayOrientation>(sladoPortrait));

    def = this->add("fast_tilt_time", coFloat);
    def->label = " ";
    def->full_label = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.));

    def = this->add("slow_tilt_time", coFloat);
    def->label = " ";
    def->full_label = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(8.));

    def = this->add("area_fill", coFloat);
    def->label = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(50.));

    def = this->add("relative_correction", coFloats);
    def->label = " ";
    def->full_label = " ";
    def->tooltip  = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats( { 1., 1.} ));

    def = this->add("relative_correction_x", coFloat);
    def->label = " ";
    def->full_label = " ";
    def->tooltip  = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("relative_correction_y", coFloat);
    def->label = " ";
    def->full_label = " ";
    def->tooltip  = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("relative_correction_z", coFloat);
    def->label = " ";
    def->full_label = " ";
    def->tooltip  = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("absolute_correction", coFloat);
    def->label = " ";
    def->full_label = " ";
    def->tooltip  = " ";
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("elefant_foot_min_width", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("gamma_correction", coFloat);
    def->label = " ";
    def->full_label = " ";
    def->tooltip  = " ";
    def->min = 0;
    def->max = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.0));


    // SLA Material settings.

    def = this->add("material_colour", coString);
    def->label = " ";
    def->tooltip = " ";
    def->gui_type = ConfigOptionDef::GUIType::color;
    def->set_default_value(new ConfigOptionString("#29B2B2"));

    def = this->add("material_type", coString);
    def->label = " ";
    def->tooltip = " ";
    def->gui_type = ConfigOptionDef::GUIType::f_enum_open;   // TODO: ???
    def->gui_flags = "show_value";
    def->enum_values.push_back("Tough");
    def->enum_values.push_back("Flexible");
    def->enum_values.push_back("Casting");
    def->enum_values.push_back("Dental");
    def->enum_values.push_back("Heat-resistant");
    def->set_default_value(new ConfigOptionString("Tough"));

    def = this->add("initial_layer_height", coFloat);
    def->label = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.3));

    def = this->add("bottle_volume", coFloat);
    def->label = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 50;
    def->set_default_value(new ConfigOptionFloat(1000.0));

    def = this->add("bottle_weight", coFloat);
    def->label = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("material_density", coFloat);
    def->label = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("bottle_cost", coFloat);
    def->label = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("faded_layers", coInt);
    def->label = " ";
    def->tooltip = " ";
    def->min = 3;
    def->max = 20;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(10));

    def = this->add("min_exposure_time", coFloat);
    def->label = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("max_exposure_time", coFloat);
    def->label = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(100));

    def = this->add("exposure_time", coFloat);
    def->label = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(10));

    def = this->add("min_initial_exposure_time", coFloat);
    def->label = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("max_initial_exposure_time", coFloat);
    def->label = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(150));

    def = this->add("initial_exposure_time", coFloat);
    def->label = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(15));

    def = this->add("material_correction", coFloats);
    def->full_label = " ";
    def->tooltip  = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats( { 1., 1., 1. } ));

    def = this->add("material_correction_x", coFloat);
    def->full_label = " ";
    def->tooltip  = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("material_correction_y", coFloat);
    def->full_label = " ";
    def->tooltip  = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("material_correction_z", coFloat);
    def->full_label = " ";
    def->tooltip  = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("material_vendor", coString);
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("default_sla_material_profile", coString);
    def->label = " ";
    def->tooltip = " ";
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("sla_material_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("default_sla_print_profile", coString);
    def->label = " ";
    def->tooltip = " ";
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("sla_print_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("supports_enable", coBool);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("support_head_front_diameter", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.4));

    def = this->add("support_head_penetration", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("support_head_width", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->max = 20;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("support_pillar_diameter", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->max = 15;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("support_small_pillar_diameter_percent", coPercent);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 1;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(50));

    def = this->add("support_max_bridges_on_pillar", coInt);
    def->label = " ";
    def->tooltip = " ";
    def->min = 0;
    def->max = 50;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("support_pillar_connection_mode", coEnum);
    def->label = " ";
    def->tooltip = " ";
    def->enum_keys_map = &ConfigOptionEnum<SLAPillarConnectionMode>::get_enum_values();
    def->enum_values.push_back("zigzag");
    def->enum_values.push_back("cross");
    def->enum_values.push_back("dynamic");
    def->enum_labels.push_back(" ");
    def->enum_labels.push_back(" ");
    def->enum_labels.push_back(" ");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SLAPillarConnectionMode>(slapcmDynamic));

    def = this->add("support_buildplate_only", coBool);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_pillar_widening_factor", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->min = 0;
    def->max = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("support_base_diameter", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->max = 30;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(4.0));

    def = this->add("support_base_height", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("support_base_safety_distance", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip  = " ";
    def->sidetext = " ";
    def->min = 0;
    def->max = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("support_critical_angle", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->max = 90;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(45));

    def = this->add("support_max_bridge_length", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(15.0));

    def = this->add("support_max_pillar_link_distance", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;   // 0 means no linking
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10.0));

    def = this->add("support_object_elevation", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->max = 150; // This is the max height of print on SL1
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.0));

    def = this->add("support_points_density_relative", coInt);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->set_default_value(new ConfigOptionInt(100));

    def = this->add("support_points_minimal_distance", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("pad_enable", coBool);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("pad_wall_thickness", coFloat);
    def->label = " ";
    def->category = " ";
     def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->max = 30;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(2.0));

    def = this->add("pad_wall_height", coFloat);
    def->label = " ";
    def->tooltip = " ";
    def->category = " ";
    def->sidetext = " ";
    def->min = 0;
    def->max = 30;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("pad_brim_size", coFloat);
    def->label = " ";
    def->tooltip = " ";
    def->category = " ";
    def->sidetext = " ";
    def->min = 0;
    def->max = 30;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.6));

    def = this->add("pad_max_merge_distance", coFloat);
    def->label = " ";
    def->category = " ";
     def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(50.0));

    def = this->add("pad_wall_slope", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 45;
    def->max = 90;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(90.0));

    def = this->add("pad_around_object", coBool);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("pad_around_object_everywhere", coBool);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("pad_object_gap", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip  = " ";
    def->sidetext = " ";
    def->min = 0;
    def->max = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("pad_object_connector_stride", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->sidetext = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10));

    def = this->add("pad_object_connector_width", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip  = " ";
    def->sidetext = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.5));

    def = this->add("pad_object_connector_penetration", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip  = " ";
    def->sidetext = " ";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.3));

    def = this->add("hollowing_enable", coBool);
    def->label = " ";
    def->category = " ";
    def->tooltip = " ";
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("hollowing_min_thickness", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip  = " ";
    def->sidetext = " ";
    def->min = 1;
    def->max = 10;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(3.));

    def = this->add("hollowing_quality", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip  = " ";
    def->min = 0;
    def->max = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.5));

    def = this->add("hollowing_closing_distance", coFloat);
    def->label = " ";
    def->category = " ";
    def->tooltip  = " ";
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(2.0));

    def = this->add("material_print_speed", coEnum);
    def->label = " ";
    def->tooltip = " ";
    def->enum_keys_map = &ConfigOptionEnum<SLAMaterialSpeed>::get_enum_values();
    def->enum_values.push_back("slow");
    def->enum_values.push_back("fast");
    def->enum_labels.push_back(" ");
    def->enum_labels.push_back(" ");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SLAMaterialSpeed>(slamsFast));
}

void PrintConfigDef::handle_legacy(t_config_option_key &opt_key, std::string &value)
{
    //BBS: handle legacy options
    if (opt_key == "enable_wipe_tower") {
        opt_key = "enable_prime_tower";
    } else if (opt_key == "wipe_tower_width") {
        opt_key = "prime_tower_width";
    } else if (opt_key == "wiping_volume") {
        opt_key = "prime_volume";
    } else if (opt_key == "wipe_tower_brim_width") {
        opt_key = "prime_tower_brim_width";
    } else if (opt_key == "tool_change_gcode") {
        opt_key = "change_filament_gcode";
    } else if (opt_key == "bridge_fan_speed") {
        opt_key = "overhang_fan_speed";
    } else if (opt_key == "infill_extruder") {
        opt_key = "sparse_infill_filament";
    }else if (opt_key == "solid_infill_extruder") {
        opt_key = "solid_infill_filament";
    }else if (opt_key == "perimeter_extruder") {
        opt_key = "wall_filament";
    }else if(opt_key == "wipe_tower_extruder") {
        opt_key = "wipe_tower_filament";
    }else if (opt_key == "support_material_extruder") {
        opt_key = "support_filament";
    } else if (opt_key == "support_material_interface_extruder") {
        opt_key = "support_interface_filament";
    } else if (opt_key == "support_material_angle") {
        opt_key = "support_angle";
    } else if (opt_key == "support_material_enforce_layers") {
        opt_key = "enforce_support_layers";
    } else if ((opt_key == "initial_layer_print_height"   ||
                opt_key == "initial_layer_speed"          ||
                opt_key == "internal_solid_infill_speed"  ||
                opt_key == "top_surface_speed"            ||
                opt_key == "support_interface_speed"      ||
                opt_key == "outer_wall_speed"             ||
                opt_key == "support_object_xy_distance")     && value.find("%") != std::string::npos) {
        //BBS: this is old profile in which value is expressed as percentage.
        //But now these key-value must be absolute value.
        //Reset to default value by erasing these key to avoid parsing error.
        opt_key = "";
    } else if (opt_key == "inherits_cummulative") {
        opt_key = "inherits_group";
    } else if (opt_key == "compatible_printers_condition_cummulative") {
        opt_key = "compatible_machine_expression_group";
    } else if (opt_key == "compatible_prints_condition_cummulative") {
        opt_key = "compatible_process_expression_group";
    } else if (opt_key == "cooling") {
        opt_key = "slow_down_for_layer_cooling";
    } else if (opt_key == "timelapse_no_toolhead") {
        opt_key = "timelapse_type";
    } else if (opt_key == "timelapse_type" && value == "2") {
        // old file "0" is None, "2" is Traditional
        // new file "0" is Traditional, erase "2"
        value = "0";
    } else if (opt_key == "support_type" && value == "normal") {
        value = "normal(manual)";
    } else if (opt_key == "support_type" && value == "tree") {
        value = "tree(manual)";
    } else if (opt_key == "support_type" && value == "hybrid(auto)") {
        value = "tree(auto)";
    } else if (opt_key == "support_base_pattern" && value == "none") {
        value = "hollow";
    } else if (opt_key == "different_settings_to_system") {
        std::string copy_value = value;
        copy_value.erase(std::remove(copy_value.begin(), copy_value.end(), '\"'), copy_value.end()); // remove '"' in string
        std::set<std::string> split_keys = SplitStringAndRemoveDuplicateElement(copy_value, ";");
        for (std::string split_key : split_keys) {
            std::string copy_key = split_key, copy_value = "";
            handle_legacy(copy_key, copy_value);
            if (copy_key != split_key) {
                ReplaceString(value, split_key, copy_key);
            }
        }
    } else if (opt_key == "overhang_fan_threshold" && value == "5%") {
        value = "10%";
    } else if( opt_key == "wall_infill_order" ) {
        if (value == "inner wall/outer wall/infill" || value == "infill/inner wall/outer wall") {
            opt_key = "wall_sequence";
            value = "inner wall/outer wall";
        } else if (value == "outer wall/inner wall/infill" || value == "infill/outer wall/inner wall") {
            opt_key = "wall_sequence";
            value = "outer wall/inner wall";
        } else if (value == "inner-outer-inner wall/infill") {
            opt_key = "wall_sequence";
            value = "inner-outer-inner wall";
        } else {
            opt_key = "wall_sequence";
        }
    }
    else if(opt_key == "ensure_vertical_shell_thickness") {
        if(value == "1") {
            value = "ensure_all";
        }
        else if (value == "0"){
            value = "ensure_moderate";
        }
    }
    else if (opt_key == "sparse_infill_anchor") {
        opt_key = "infill_anchor";
    } 
    else if (opt_key == "sparse_infill_anchor_max") {
        opt_key = "infill_anchor_max";
    }
    else if (opt_key == "chamber_temperatures") {
        opt_key = "chamber_temperature";
    }
    else if (opt_key == "thumbnail_size") {
        opt_key = "thumbnails";
    }
    else if (opt_key == "top_one_wall_type" && value != "none") {
        opt_key = "only_one_wall_top";
        value = "1";
    }
    else if (opt_key == "initial_layer_flow_ratio") {
        opt_key = "bottom_solid_infill_flow_ratio";
    }
    else if(opt_key == "ironing_direction") {
        opt_key = "ironing_angle";
    }
    else if(opt_key == "counterbole_hole_bridging") {
        opt_key = "counterbore_hole_bridging";
    }
    else if (opt_key == "draft_shield" && value == "limited") {
        value = "disabled";
    } else if (opt_key == "overhang_speed_classic") {
        value = "0";
    }

    // Ignore the following obsolete configuration keys:
    static std::set<std::string> ignore = {
        "acceleration", "scale", "rotate", "duplicate", "duplicate_grid",
        "bed_size",
        "print_center", "g0", "wipe_tower_per_color_wipe", 
        "support_sharp_tails","support_remove_small_overhangs", "support_with_sheath",
        "tree_support_collision_resolution", "tree_support_with_infill",
        "max_volumetric_speed", "max_print_speed",
        "support_closing_radius",
        "remove_freq_sweep", "remove_bed_leveling", "remove_extrusion_calibration",
        "support_transition_line_width", "support_transition_speed", "bed_temperature", "bed_temperature_initial_layer",
        "can_switch_nozzle_type", "can_add_auxiliary_fan", "extra_flush_volume", "spaghetti_detector", "adaptive_layer_height",
        "z_hop_type", "z_lift_type", "bed_temperature_difference","long_retraction_when_cut",
        "retraction_distance_when_cut",
        "extruder_type",
        "internal_bridge_support_thickness","extruder_clearance_max_radius", "top_area_threshold", "reduce_wall_solid_infill","filament_load_time","filament_unload_time",
        "smooth_coefficient", "overhang_totally_speed"
    };

    if (ignore.find(opt_key) != ignore.end()) {
        opt_key = "";
        return;
    }

    if (! print_config_def.has(opt_key)) {
        opt_key = "";
        return;
    }
}

// Called after a config is loaded as a whole.
// Perform composite conversions, for example merging multiple keys into one key.
// Don't convert single options here, implement such conversion in PrintConfigDef::handle_legacy() instead.
void PrintConfigDef::handle_legacy_composite(DynamicPrintConfig &config)
{
    if (config.has("thumbnails")) {
        std::string extention;
        if (config.has("thumbnails_format")) {
            if (const ConfigOptionDef* opt = config.def()->get("thumbnails_format")) {
                extention = opt->enum_values.at(config.option("thumbnails_format")->getInt());
            }
        }

        std::string thumbnails_str = config.opt_string("thumbnails");
        auto [thumbnails_list, errors] = GCodeThumbnails::make_and_check_thumbnail_list(thumbnails_str, extention);

        if (errors != enum_bitmask<ThumbnailError>()) {
            std::string error_str = "\n" + Slic3r::format("Invalid value provided for parameter %1%: %2%", "thumbnails", thumbnails_str);
            error_str += GCodeThumbnails::get_error_string(errors);
            throw BadOptionValueException(error_str);
        }

        if (!thumbnails_list.empty()) {
            const auto& extentions = ConfigOptionEnum<GCodeThumbnailsFormat>::get_enum_names();
            thumbnails_str.clear();
            for (const auto& [ext, size] : thumbnails_list)
                thumbnails_str += Slic3r::format("%1%x%2%/%3%, ", size.x(), size.y(), extentions[int(ext)]);
            thumbnails_str.resize(thumbnails_str.length() - 2);

            config.set_key_value("thumbnails", new ConfigOptionString(thumbnails_str));
        }
    }

    if (config.has("wiping_volumes_matrix") && !config.has("wiping_volumes_use_custom_matrix")) {
        // This is apparently some pre-2.7.3 config, where the wiping_volumes_matrix was always used.
        // The 2.7.3 introduced an option to use defaults derived from config. In case the matrix
        // contains only default values, switch it to default behaviour. The default values
        // were zeros on the diagonal and 140 otherwise.
        std::vector<double> matrix = config.opt<ConfigOptionFloats>("wiping_volumes_matrix")->values;
        int num_of_extruders = int(std::sqrt(matrix.size()) + 0.5);
        int i = -1;
        bool custom = false;
        for (int j = 0; j < int(matrix.size()); ++j) {
            if (j % num_of_extruders == 0)
                ++i;
            if (i != j % num_of_extruders && !is_approx(matrix[j], 140.)) {
                custom = true;
                break;
            }
        }
        config.set_key_value("wiping_volumes_use_custom_matrix", new ConfigOptionBool(custom));
    }
}

const PrintConfigDef print_config_def;

DynamicPrintConfig DynamicPrintConfig::full_print_config()
{
	return DynamicPrintConfig((const PrintRegionConfig&)FullPrintConfig::defaults());
}

DynamicPrintConfig::DynamicPrintConfig(const StaticPrintConfig& rhs) : DynamicConfig(rhs, rhs.keys_ref())
{
}

DynamicPrintConfig* DynamicPrintConfig::new_from_defaults_keys(const std::vector<std::string> &keys)
{
    auto *out = new DynamicPrintConfig();
    out->apply_only(FullPrintConfig::defaults(), keys);
    return out;
}

double min_object_distance(const ConfigBase &cfg)
{
    const ConfigOptionEnum<PrinterTechnology> *opt_printer_technology = cfg.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
    auto printer_technology = opt_printer_technology ? opt_printer_technology->value : ptUnknown;

    double ret = 0.;

    if (printer_technology == ptSLA)
        ret = 6.;
    else {
        //BBS: duplicate_distance seam to be useless
        constexpr double duplicate_distance = 6.;
        auto ecr_opt = cfg.option<ConfigOptionFloat>("extruder_clearance_radius");
        auto co_opt  = cfg.option<ConfigOptionEnum<PrintSequence>>("print_sequence");

        if (!ecr_opt || !co_opt)
            ret = 0.;
        else {
            // min object distance is max(duplicate_distance, clearance_radius)
            ret = ((co_opt->value == PrintSequence::ByObject) && ecr_opt->value > duplicate_distance) ?
                      ecr_opt->value : duplicate_distance;
        }
    }

    return ret;
}

void DynamicPrintConfig::normalize_fdm(int used_filaments)
{
    if (this->has("extruder")) {
        int extruder = this->option("extruder")->getInt();
        this->erase("extruder");
        if (extruder != 0) {
            if (!this->has("sparse_infill_filament"))
                this->option("sparse_infill_filament", true)->setInt(extruder);
            if (!this->has("wall_filament"))
                this->option("wall_filament", true)->setInt(extruder);
            // Don't propagate the current extruder to support.
            // For non-soluble supports, the default "0" extruder means to use the active extruder,
            // for soluble supports one certainly does not want to set the extruder to non-soluble.
            // if (!this->has("support_filament"))
            //     this->option("support_filament", true)->setInt(extruder);
            // if (!this->has("support_interface_filament"))
            //     this->option("support_interface_filament", true)->setInt(extruder);
        }
    }

    if (!this->has("solid_infill_filament") && this->has("sparse_infill_filament"))
        this->option("solid_infill_filament", true)->setInt(this->option("sparse_infill_filament")->getInt());

    if (this->has("spiral_mode") && this->opt<ConfigOptionBool>("spiral_mode", true)->value) {
        {
            // this should be actually done only on the spiral layers instead of all
            auto* opt = this->opt<ConfigOptionBools>("retract_when_changing_layer", true);
            opt->values.assign(opt->values.size(), false);  // set all values to false
            // Disable retract on layer change also for filament overrides.
            auto* opt_n = this->opt<ConfigOptionBoolsNullable>("filament_retract_when_changing_layer", true);
            opt_n->values.assign(opt_n->values.size(), false);  // Set all values to false.
        }
        {
            this->opt<ConfigOptionInt>("wall_loops", true)->value       = 1;
            this->opt<ConfigOptionBool>("alternate_extra_wall", true)->value = false;
            this->opt<ConfigOptionInt>("top_shell_layers", true)->value = 0;
            this->opt<ConfigOptionPercent>("sparse_infill_density", true)->value = 0;
        }
    }

    if (auto *opt_gcode_resolution = this->opt<ConfigOptionFloat>("resolution", false); opt_gcode_resolution)
        // Resolution will be above 1um.
        opt_gcode_resolution->value = std::max(opt_gcode_resolution->value, 0.001);

    // BBS
    ConfigOptionBool* ept_opt = this->option<ConfigOptionBool>("enable_prime_tower");
    if (used_filaments > 0 && ept_opt != nullptr) {
        ConfigOptionBool* islh_opt = this->option<ConfigOptionBool>("independent_support_layer_height", true);
        //ConfigOptionBool* alh_opt = this->option<ConfigOptionBool>("adaptive_layer_height");
        ConfigOptionEnum<PrintSequence>* ps_opt = this->option<ConfigOptionEnum<PrintSequence>>("print_sequence");

        ConfigOptionEnum<TimelapseType>* timelapse_opt = this->option<ConfigOptionEnum<TimelapseType>>("timelapse_type");
        bool is_smooth_timelapse = timelapse_opt != nullptr && timelapse_opt->value == TimelapseType::tlSmooth;
        if (!is_smooth_timelapse && (used_filaments == 1 || ps_opt->value == PrintSequence::ByObject)) {
            ept_opt->value = false;
        }

        if (ept_opt->value) {
            if (islh_opt)
                islh_opt->value = false;
            //if (alh_opt)
            //    alh_opt->value = false;
        }
        /* BBS: MusangKing - not sure if this is still valid, just comment it out cause "Independent support layer height" is re-opened.
        else {
            if (islh_opt)
                islh_opt->value = true;
        }
        */
    }
}

//BBS:divide normalize_fdm to 2 steps and call them one by one in Print::Apply
void DynamicPrintConfig::normalize_fdm_1()
{
    if (this->has("extruder")) {
        int extruder = this->option("extruder")->getInt();
        this->erase("extruder");
        if (extruder != 0) {
            if (!this->has("sparse_infill_filament"))
                this->option("sparse_infill_filament", true)->setInt(extruder);
            if (!this->has("wall_filament"))
                this->option("wall_filament", true)->setInt(extruder);
            // Don't propagate the current extruder to support.
            // For non-soluble supports, the default "0" extruder means to use the active extruder,
            // for soluble supports one certainly does not want to set the extruder to non-soluble.
            // if (!this->has("support_filament"))
            //     this->option("support_filament", true)->setInt(extruder);
            // if (!this->has("support_interface_filament"))
            //     this->option("support_interface_filament", true)->setInt(extruder);
        }
    }

    if (!this->has("solid_infill_filament") && this->has("sparse_infill_filament"))
        this->option("solid_infill_filament", true)->setInt(this->option("sparse_infill_filament")->getInt());

    if (this->has("spiral_mode") && this->opt<ConfigOptionBool>("spiral_mode", true)->value) {
        {
            // this should be actually done only on the spiral layers instead of all
            auto* opt = this->opt<ConfigOptionBools>("retract_when_changing_layer", true);
            opt->values.assign(opt->values.size(), false);  // set all values to false
            // Disable retract on layer change also for filament overrides.
            auto* opt_n = this->opt<ConfigOptionBoolsNullable>("filament_retract_when_changing_layer", true);
            opt_n->values.assign(opt_n->values.size(), false);  // Set all values to false.
        }
        {
            this->opt<ConfigOptionInt>("wall_loops", true)->value       = 1;
            this->opt<ConfigOptionBool>("alternate_extra_wall", true)->value = false;
            this->opt<ConfigOptionInt>("top_shell_layers", true)->value = 0;
            this->opt<ConfigOptionPercent>("sparse_infill_density", true)->value = 0;
        }
    }

    if (auto *opt_gcode_resolution = this->opt<ConfigOptionFloat>("resolution", false); opt_gcode_resolution)
        // Resolution will be above 1um.
        opt_gcode_resolution->value = std::max(opt_gcode_resolution->value, 0.001);

    return;
}

t_config_option_keys DynamicPrintConfig::normalize_fdm_2(int num_objects, int used_filaments)
{
    t_config_option_keys changed_keys;
    ConfigOptionBool* ept_opt = this->option<ConfigOptionBool>("enable_prime_tower");
    if (used_filaments > 0 && ept_opt != nullptr) {
        ConfigOptionBool* islh_opt = this->option<ConfigOptionBool>("independent_support_layer_height", true);
        //ConfigOptionBool* alh_opt = this->option<ConfigOptionBool>("adaptive_layer_height");
        ConfigOptionEnum<PrintSequence>* ps_opt = this->option<ConfigOptionEnum<PrintSequence>>("print_sequence");

        ConfigOptionEnum<TimelapseType>* timelapse_opt = this->option<ConfigOptionEnum<TimelapseType>>("timelapse_type");
        bool is_smooth_timelapse = timelapse_opt != nullptr && timelapse_opt->value == TimelapseType::tlSmooth;
        if (!is_smooth_timelapse && (used_filaments == 1 || (ps_opt->value == PrintSequence::ByObject && num_objects > 1))) {
            if (ept_opt->value) {
                ept_opt->value = false;
                changed_keys.push_back("enable_prime_tower");
            }
            //ept_opt->value = false;
        }

        if (ept_opt->value) {
            if (islh_opt) {
                if (islh_opt->value) {
                    islh_opt->value = false;
                    changed_keys.push_back("independent_support_layer_height");
                }
                //islh_opt->value = false;
            }
            //if (alh_opt) {
            //    if (alh_opt->value) {
            //        alh_opt->value = false;
            //        changed_keys.push_back("adaptive_layer_height");
            //    }
            //    //alh_opt->value = false;
            //}
        }
        /* BBS：MusangKing - use "global->support->Independent support layer height" widget to replace previous assignment
        else {
            if (islh_opt) {
                if (!islh_opt->value) {
                    islh_opt->value = true;
                    changed_keys.push_back("independent_support_layer_height");
                }
                //islh_opt->value = true;
            }
        }
        */
    }

    return changed_keys;
}

void  handle_legacy_sla(DynamicPrintConfig &config)
{
    for (std::string corr : {"relative_correction", "material_correction"}) {
        if (config.has(corr)) {
            if (std::string corr_x = corr + "_x"; !config.has(corr_x)) {
                auto* opt = config.opt<ConfigOptionFloat>(corr_x, true);
                opt->value = config.opt<ConfigOptionFloats>(corr)->values[0];
            }

            if (std::string corr_y = corr + "_y"; !config.has(corr_y)) {
                auto* opt = config.opt<ConfigOptionFloat>(corr_y, true);
                opt->value = config.opt<ConfigOptionFloats>(corr)->values[0];
            }

            if (std::string corr_z = corr + "_z"; !config.has(corr_z)) {
                auto* opt = config.opt<ConfigOptionFloat>(corr_z, true);
                opt->value = config.opt<ConfigOptionFloats>(corr)->values[1];
            }
        }
    }
}

void DynamicPrintConfig::set_num_extruders(unsigned int num_extruders)
{
    const auto &defaults = FullPrintConfig::defaults();
    for (const std::string &key : print_config_def.extruder_option_keys()) {
        if (key == "default_filament_profile")
            // Don't resize this field, as it is presented to the user at the "Dependencies" page of the Printer profile and we don't want to present
            // empty fields there, if not defined by the system profile.
            continue;
        auto *opt = this->option(key, false);
        assert(opt != nullptr);
        assert(opt->is_vector());
        if (opt != nullptr && opt->is_vector())
            static_cast<ConfigOptionVectorBase*>(opt)->resize(num_extruders, defaults.option(key));
    }
}

// BBS
void DynamicPrintConfig::set_num_filaments(unsigned int num_filaments)
{
    const auto& defaults = FullPrintConfig::defaults();
    for (const std::string& key : print_config_def.filament_option_keys()) {
        if (key == "default_filament_profile")
            // Don't resize this field, as it is presented to the user at the "Dependencies" page of the Printer profile and we don't want to present
            // empty fields there, if not defined by the system profile.
            continue;
        auto* opt = this->option(key, false);
        assert(opt != nullptr);
        assert(opt->is_vector());
        if (opt != nullptr && opt->is_vector())
            static_cast<ConfigOptionVectorBase*>(opt)->resize(num_filaments, defaults.option(key));
    }
}

//BBS: pass map to recording all invalid valies
std::map<std::string, std::string> DynamicPrintConfig::validate(bool under_cli)
{
    // Full print config is initialized from the defaults.
    const ConfigOption *opt = this->option("printer_technology", false);
    auto printer_technology = (opt == nullptr) ? ptFFF : static_cast<PrinterTechnology>(dynamic_cast<const ConfigOptionEnumGeneric*>(opt)->value);
    switch (printer_technology) {
    case ptFFF:
    {
        FullPrintConfig fpc;
        fpc.apply(*this, true);
        // Verify this print options through the FullPrintConfig.
        return Slic3r::validate(fpc, under_cli);
    }
    default:
        //FIXME no validation on SLA data?
        return std::map<std::string, std::string>();
    }
}

std::string DynamicPrintConfig::get_filament_type(std::string &displayed_filament_type, int id)
{
    auto* filament_id = dynamic_cast<const ConfigOptionStrings*>(this->option("filament_id"));
    auto* filament_type = dynamic_cast<const ConfigOptionStrings*>(this->option("filament_type"));
    auto* filament_is_support = dynamic_cast<const ConfigOptionBools*>(this->option("filament_is_support"));

    if (!filament_type)
        return "";

    if (!filament_is_support) {
        if (filament_type) {
            displayed_filament_type = filament_type->get_at(id);
            return filament_type->get_at(id);
        }
        else {
            displayed_filament_type = "";
            return "";
        }
    }
    else {
        bool is_support = filament_is_support ? filament_is_support->get_at(id) : false;
        if (is_support) {
            if (filament_id) {
                if (filament_id->get_at(id) == "GFS00") {
                    displayed_filament_type = "Sup.PLA";
                    return "PLA-S";
                }
                else if (filament_id->get_at(id) == "GFS01") {
                    displayed_filament_type = "Sup.PA";
                    return "PA-S";
                }
                else {
                    if (filament_type->get_at(id) == "PLA") {
                        displayed_filament_type = "Sup.PLA";
                        return "PLA-S";
                    }
                    else if (filament_type->get_at(id) == "PA") {
                        displayed_filament_type = "Sup.PA";
                        return "PA-S";
                    }
                    else {
                        displayed_filament_type = filament_type->get_at(id);
                        return filament_type->get_at(id);
                    }
                }
            }
            else {
                if (filament_type->get_at(id) == "PLA") {
                    displayed_filament_type = "Sup.PLA";
                    return "PLA-S";
                } else if (filament_type->get_at(id) == "PA") {
                    displayed_filament_type = "Sup.PA";
                    return "PA-S";
                } else {
                    displayed_filament_type = filament_type->get_at(id);
                    return filament_type->get_at(id);
                }
            }
        }
        else {
            displayed_filament_type = filament_type->get_at(id);
            return filament_type->get_at(id);
        }
    }
    return "PLA";
}

bool DynamicPrintConfig::is_custom_defined()
{
    auto* is_custom_defined = dynamic_cast<const ConfigOptionStrings*>(this->option("is_custom_defined"));
    if (!is_custom_defined || is_custom_defined->empty())
        return false;
    if (is_custom_defined->get_at(0) == "1")
        return true;
    return false;
}

//BBS: pass map to recording all invalid valies
//FIXME localize this function.
std::map<std::string, std::string> validate(const FullPrintConfig &cfg, bool under_cli)
{
    std::map<std::string, std::string> error_message;
    // --layer-height
    if (cfg.get_abs_value("layer_height") <= 0) {
        error_message.emplace("layer_height", L("invalid value ") + std::to_string(cfg.get_abs_value("layer_height")));
    }
    else if (fabs(fmod(cfg.get_abs_value("layer_height"), SCALING_FACTOR)) > 1e-4) {
        error_message.emplace("layer_height", L("invalid value ") + std::to_string(cfg.get_abs_value("layer_height")));
    }

    // --first-layer-height
    if (cfg.initial_layer_print_height.value <= 0) {
        error_message.emplace("initial_layer_print_height", L("invalid value ") + std::to_string(cfg.initial_layer_print_height.value));
    }

    // --filament-diameter
    for (double fd : cfg.filament_diameter.values)
        if (fd < 1) {
            error_message.emplace("filament_diameter", L("invalid value ") + cfg.filament_diameter.serialize());
            break;
        }

    // --nozzle-diameter
    for (double nd : cfg.nozzle_diameter.values)
        if (nd < 0.005) {
            error_message.emplace("nozzle_diameter", L("invalid value ") + cfg.nozzle_diameter.serialize());
            break;
        }

    // --perimeters
    if (cfg.wall_loops.value < 0) {
        error_message.emplace("wall_loops", L("invalid value ") + std::to_string(cfg.wall_loops.value));
    }

    // --solid-layers
    if (cfg.top_shell_layers < 0) {
        error_message.emplace("top_shell_layers", L("invalid value ") + std::to_string(cfg.top_shell_layers));
    }
    if (cfg.bottom_shell_layers < 0) {
        error_message.emplace("bottom_shell_layers", L("invalid value ") + std::to_string(cfg.bottom_shell_layers));
    }

    if (cfg.use_firmware_retraction.value &&
        cfg.gcode_flavor.value != gcfKlipper &&
        cfg.gcode_flavor.value != gcfSmoothie &&
        cfg.gcode_flavor.value != gcfRepRapSprinter &&
        cfg.gcode_flavor.value != gcfRepRapFirmware &&
        cfg.gcode_flavor.value != gcfMarlinLegacy &&
        cfg.gcode_flavor.value != gcfMarlinFirmware &&
        cfg.gcode_flavor.value != gcfMachinekit &&
        cfg.gcode_flavor.value != gcfRepetier)
        error_message.emplace("use_firmware_retraction","--use-firmware-retraction is only supported by Klipper, Marlin, Smoothie, RepRapFirmware, Repetier and Machinekit firmware");

    if (cfg.use_firmware_retraction.value)
        for (unsigned char wipe : cfg.wipe.values)
             if (wipe)
                error_message.emplace("use_firmware_retraction", "--use-firmware-retraction is not compatible with --wipe");
                
    // --gcode-flavor
    if (! print_config_def.get("gcode_flavor")->has_enum_value(cfg.gcode_flavor.serialize())) {
        error_message.emplace("gcode_flavor", L("invalid value ") + cfg.gcode_flavor.serialize());
    }

    // --fill-pattern
    if (! print_config_def.get("sparse_infill_pattern")->has_enum_value(cfg.sparse_infill_pattern.serialize())) {
        error_message.emplace("sparse_infill_pattern", L("invalid value ") + cfg.sparse_infill_pattern.serialize());
    }

    // --top-fill-pattern
    if (! print_config_def.get("top_surface_pattern")->has_enum_value(cfg.top_surface_pattern.serialize())) {
        error_message.emplace("top_surface_pattern", L("invalid value ") + cfg.top_surface_pattern.serialize());
    }

    // --bottom-fill-pattern
    if (! print_config_def.get("bottom_surface_pattern")->has_enum_value(cfg.bottom_surface_pattern.serialize())) {
        error_message.emplace("bottom_surface_pattern", L("invalid value ") + cfg.bottom_surface_pattern.serialize());
    }

    // --soild-fill-pattern
    if (!print_config_def.get("internal_solid_infill_pattern")->has_enum_value(cfg.internal_solid_infill_pattern.serialize())) {
        error_message.emplace("internal_solid_infill_pattern", L("invalid value ") + cfg.internal_solid_infill_pattern.serialize());
    }

    // --skirt-height
    if (cfg.skirt_height < 0) {
        error_message.emplace("skirt_height", L("invalid value ") + std::to_string(cfg.skirt_height));
    }

    // --bridge-flow-ratio
    if (cfg.bridge_flow <= 0) {
        error_message.emplace("bridge_flow", L("invalid value ") + std::to_string(cfg.bridge_flow));
    }
    
    // --bridge-flow-ratio
    if (cfg.bridge_flow <= 0) {
        error_message.emplace("internal_bridge_flow", L("invalid value ") + std::to_string(cfg.internal_bridge_flow));
    }

    // extruder clearance
    if (cfg.extruder_clearance_radius <= 0) {
        error_message.emplace("extruder_clearance_radius", L("invalid value ") + std::to_string(cfg.extruder_clearance_radius));
    }
    if (cfg.extruder_clearance_height_to_rod <= 0) {
        error_message.emplace("extruder_clearance_height_to_rod", L("invalid value ") + std::to_string(cfg.extruder_clearance_height_to_rod));
    }
    if (cfg.extruder_clearance_height_to_lid <= 0) {
        error_message.emplace("extruder_clearance_height_to_lid", L("invalid value ") + std::to_string(cfg.extruder_clearance_height_to_lid));
    }
    if (cfg.nozzle_height <= 0)
        error_message.emplace("nozzle_height", L("invalid value ") + std::to_string(cfg.nozzle_height));

    // --extrusion-multiplier
    for (double em : cfg.filament_flow_ratio.values)
        if (em <= 0) {
            error_message.emplace("filament_flow_ratio", L("invalid value ") + cfg.filament_flow_ratio.serialize());
            break;
        }

    // --spiral-vase
    //for non-cli case, we will popup dialog for spiral mode correction
    if (cfg.spiral_mode && under_cli) {
        // Note that we might want to have more than one perimeter on the bottom
        // solid layers.
        if (cfg.wall_loops != 1) {
            error_message.emplace("wall_loops", L("Invalid value when spiral vase mode is enabled: ") + std::to_string(cfg.wall_loops));
            //return "Can't make more than one perimeter when spiral vase mode is enabled";
            //return "Can't make less than one perimeter when spiral vase mode is enabled";
        }

        if (cfg.sparse_infill_density > 0) {
            error_message.emplace("sparse_infill_density", L("Invalid value when spiral vase mode is enabled: ") + std::to_string(cfg.sparse_infill_density));
            //return "Spiral vase mode can only print hollow objects, so you need to set Fill density to 0";
        }

        if (cfg.top_shell_layers > 0) {
            error_message.emplace("top_shell_layers", L("Invalid value when spiral vase mode is enabled: ") + std::to_string(cfg.top_shell_layers));
            //return "Spiral vase mode is not compatible with top solid layers";
        }

        if (cfg.enable_support ) {
            error_message.emplace("enable_support", L("Invalid value when spiral vase mode is enabled: ") + std::to_string(cfg.enable_support));
            //return "Spiral vase mode is not compatible with support";
        }
        if (cfg.enforce_support_layers > 0) {
            error_message.emplace("enforce_support_layers", L("Invalid value when spiral vase mode is enabled: ") + std::to_string(cfg.enforce_support_layers));
            //return "Spiral vase mode is not compatible with support";
        }
    }

    // extrusion widths
    {
        double max_nozzle_diameter = 0.;
        for (double dmr : cfg.nozzle_diameter.values)
            max_nozzle_diameter = std::max(max_nozzle_diameter, dmr);
        const char *widths[] = {
            "outer_wall_line_width",
            "inner_wall_line_width",
            "sparse_infill_line_width",
            "internal_solid_infill_line_width",
            "top_surface_line_width",
            "support_line_width",
            "initial_layer_line_width" };
        for (size_t i = 0; i < sizeof(widths) / sizeof(widths[i]); ++ i) {
            std::string key(widths[i]);
            if (cfg.get_abs_value(key, max_nozzle_diameter) > 2.5 * max_nozzle_diameter) {
                error_message.emplace(key, L("too large line width ") + std::to_string(cfg.get_abs_value(key)));
                //return std::string("Too Large line width: ") + key;
            }
        }
    }

    // Out of range validation of numeric values.
    for (const std::string &opt_key : cfg.keys()) {
        const ConfigOption      *opt    = cfg.optptr(opt_key);
        assert(opt != nullptr);
        const ConfigOptionDef   *optdef = print_config_def.get(opt_key);
        assert(optdef != nullptr);
        bool out_of_range = false;
        switch (opt->type()) {
        case coFloat:
        case coPercent:
        case coFloatOrPercent:
        {
            auto *fopt = static_cast<const ConfigOptionFloat*>(opt);
            out_of_range = fopt->value < optdef->min || fopt->value > optdef->max;
            break;
        }
        case coFloats:
        case coPercents:
            for (double v : static_cast<const ConfigOptionVector<double>*>(opt)->values)
                if (v < optdef->min || v > optdef->max) {
                    out_of_range = true;
                    break;
                }
            break;
        case coInt:
        {
            auto *iopt = static_cast<const ConfigOptionInt*>(opt);
            out_of_range = iopt->value < optdef->min || iopt->value > optdef->max;
            break;
        }
        case coInts:
            for (int v : static_cast<const ConfigOptionVector<int>*>(opt)->values)
                if (v < optdef->min || v > optdef->max) {
                    out_of_range = true;
                    break;
                }
            break;
        default:;
        }
        if (out_of_range) {
            if (error_message.find(opt_key) == error_message.end())
                error_message.emplace(opt_key, opt->serialize() + L(" not in range ") +"[" + std::to_string(optdef->min) + "," + std::to_string(optdef->max) + "]");
            //return std::string("Value out of range: " + opt_key);
        }
    }

    // The configuration is valid.
    return error_message;
}

// Declare and initialize static caches of StaticPrintConfig derived classes.
#define PRINT_CONFIG_CACHE_ELEMENT_DEFINITION(r, data, CLASS_NAME) StaticPrintConfig::StaticCache<class Slic3r::CLASS_NAME> BOOST_PP_CAT(CLASS_NAME::s_cache_, CLASS_NAME);
#define PRINT_CONFIG_CACHE_ELEMENT_INITIALIZATION(r, data, CLASS_NAME) Slic3r::CLASS_NAME::initialize_cache();
#define PRINT_CONFIG_CACHE_INITIALIZE(CLASSES_SEQ) \
    BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CACHE_ELEMENT_DEFINITION, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_SEQ)) \
    int print_config_static_initializer() { \
        /* Putting a trace here to avoid the compiler to optimize out this function. */ \
        BOOST_LOG_TRIVIAL(trace) << "Initializing StaticPrintConfigs"; \
        BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CACHE_ELEMENT_INITIALIZATION, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_SEQ)) \
        return 1; \
    }
PRINT_CONFIG_CACHE_INITIALIZE((
    PrintObjectConfig, PrintRegionConfig, MachineEnvelopeConfig, GCodeConfig, PrintConfig, FullPrintConfig,
    SLAMaterialConfig, SLAPrintConfig, SLAPrintObjectConfig, SLAPrinterConfig, SLAFullPrintConfig))
static int print_config_static_initialized = print_config_static_initializer();

//BBS: remove unused command currently
CLIActionsConfigDef::CLIActionsConfigDef()
{
    ConfigOptionDef* def;

    // Actions:
    /*def = this->add("export_obj", coBool);
    def->label = L("Export OBJ");
    def->tooltip = L("Export the model(s) as OBJ.");
    def->set_default_value(new ConfigOptionBool(false));*/

/*
    def = this->add("export_svg", coBool);
    def->label = L("Export SVG");
    def->tooltip = L("Slice the model and export solid slices as SVG.");
    def->set_default_value(new ConfigOptionBool(false));
*/

    /*def = this->add("export_sla", coBool);
    def->label = L("Export SLA");
    def->tooltip = L("Slice the model and export SLA printing layers as PNG.");
    def->cli = "export-sla|sla";
    def->set_default_value(new ConfigOptionBool(false));*/

    def = this->add("export_3mf", coString);
    def->label = "Export 3MF";
    def->tooltip = "Export project as 3MF.";
    def->cli_params = "filename.3mf";
    def->set_default_value(new ConfigOptionString("output.3mf"));

    def = this->add("export_slicedata", coString);
    def->label = "Export slicing data";
    def->tooltip = "Export slicing data to a folder.";
    def->cli_params = "slicing_data_directory";
    def->set_default_value(new ConfigOptionString("cached_data"));

    def = this->add("load_slicedata", coStrings);
    def->label = "Load slicing data";
    def->tooltip = "Load cached slicing data from directory";
    def->cli_params = "slicing_data_directory";
    def->set_default_value(new ConfigOptionString("cached_data"));

    /*def = this->add("export_amf", coBool);
    def->label = L("Export AMF");
    def->tooltip = L("Export the model(s) as AMF.");
    def->set_default_value(new ConfigOptionBool(false));*/

    def = this->add("export_stl", coBool);
    def->label = "Export STL";
    def->tooltip = "Export the objects as single STL.";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("export_stls", coString);
    def->label = "Export multiple STLs";
    def->tooltip = "Export the objects as multiple STLs to directory";
    def->set_default_value(new ConfigOptionString("stl_path"));

    /*def = this->add("export_gcode", coBool);
    def->label = L("Export G-code");
    def->tooltip = L("Slice the model and export toolpaths as G-code.");
    def->cli = "export-gcode|gcode|g";
    def->set_default_value(new ConfigOptionBool(false));*/

    /*def = this->add("gcodeviewer", coBool);
    // BBS: remove _L()
    def->label = ("G-code viewer");
    def->tooltip = ("Visualize an already sliced and saved G-code");
    def->cli = "gcodeviewer";
    def->set_default_value(new ConfigOptionBool(false));*/

    def = this->add("slice", coInt);
    def->label = "Slice";
    def->tooltip = "Slice the plates: 0-all plates, i-plate i, others-invalid";
    def->cli = "slice";
    def->cli_params = "option";
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("help", coBool);
    def->label = "Help";
    def->tooltip = "Show command help.";
    def->cli = "help|h";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("uptodate", coBool);
    def->label = "UpToDate";
    def->tooltip = "Update the configs values of 3mf to latest.";
    def->cli = "uptodate";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("downward_check", coStrings);
    def->label = "downward machines check";
    def->tooltip = "check whether current machine downward compatible with the machines in the list";
    def->cli_params = "\"machine1.json;machine2.json;...\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("load_defaultfila", coBool);
    def->label = "Load default filaments";
    def->tooltip = "Load first filament as default for those not loaded";
    def->cli_params = "option";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("min_save", coBool);
    def->label = "Minimum save";
    def->tooltip = "export 3mf with minimum size.";
    def->cli_params = "option";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("min_save", coBool);
    def->label = L("Minimum save");
    def->tooltip = L("export 3mf with minimum size.");
    def->cli_params = "option";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("mtcpp", coInt);
    def->label = "mtcpp";
    def->tooltip = "max triangle count per plate for slicing.";
    def->cli = "mtcpp";
    def->cli_params = "count";
    def->set_default_value(new ConfigOptionInt(1000000));

    def = this->add("mstpp", coInt);
    def->label = "mstpp";
    def->tooltip = "max slicing time per plate in seconds.";
    def->cli = "mstpp";
    def->cli_params = "time";
    def->set_default_value(new ConfigOptionInt(300));

    // must define new params here, otherwise comamnd param check will fail
    def = this->add("no_check", coBool);
    def->label = L("No check");
    def->tooltip = L("Do not run any validity checks, such as gcode path conflicts check.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("normative_check", coBool);
    def->label = "Normative check";
    def->tooltip = "Check the normative items.";
    def->cli_params = "option";
    def->set_default_value(new ConfigOptionBool(true));

    /*def = this->add("help_fff", coBool);
    def->label = L("Help (FFF options)");
    def->tooltip = L("Show the full list of print/G-code configuration options.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("help_sla", coBool);
    def->label = L("Help (SLA options)");
    def->tooltip = L("Show the full list of SLA print configuration options.");
    def->set_default_value(new ConfigOptionBool(false));*/

    def = this->add("info", coBool);
    def->label = "Output Model Info";
    def->tooltip = "Output the model's information.";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("export_settings", coString);
    def->label = "Export Settings";
    def->tooltip = "Export settings to a file.";
    def->cli_params = "settings.json";
    def->set_default_value(new ConfigOptionString("output.json"));

    def = this->add("pipe", coString);
    def->label = "Send progress to pipe";
    def->tooltip = "Send progress to pipe.";
    def->cli_params = "pipename";
    def->set_default_value(new ConfigOptionString(""));
}

//BBS: remove unused command currently
CLITransformConfigDef::CLITransformConfigDef()
{
    ConfigOptionDef* def;

    // Transform options:
    /*def = this->add("align_xy", coPoint);
    def->label = L("Align XY");
    def->tooltip = L("Align the model to the given point.");
    def->set_default_value(new ConfigOptionPoint(Vec2d(100,100)));

    def = this->add("cut", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model at the given Z.");
    def->set_default_value(new ConfigOptionFloat(0));*/

/*
    def = this->add("cut_grid", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model in the XY plane into tiles of the specified max size.");
    def->set_default_value(new ConfigOptionPoint());

    def = this->add("cut_x", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model at the given X.");
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("cut_y", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model at the given Y.");
    def->set_default_value(new ConfigOptionFloat(0));
*/

    /*def = this->add("center", coPoint);
    def->label = L("Center");
    def->tooltip = L("Center the print around the given center.");
    def->set_default_value(new ConfigOptionPoint(Vec2d(100,100)));*/

    def = this->add("arrange", coInt);
    def->label = "Arrange Options";
    def->tooltip = "Arrange options: 0-disable, 1-enable, others-auto";
    def->cli_params = "option";
    //def->cli = "arrange|a";
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("repetitions", coInt);
    def->label = "Repetions count";
    def->tooltip = "Repetions count of the whole model";
    def->cli_params = "count";
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("ensure_on_bed", coBool);
    def->label = L("Ensure on bed");
    def->tooltip = L("Lift the object above the bed when it is partially below. Disabled by default");
    def->set_default_value(new ConfigOptionBool(false));

    /*def = this->add("copy", coInt);
    def->label = L("Copy");
    def->tooltip =L("Duplicate copies of model");
    def->min = 1;
    def->set_default_value(new ConfigOptionInt(1));*/

    /*def = this->add("duplicate_grid", coPoint);
    def->label = L("Duplicate by grid");
    def->tooltip = L("Multiply copies by creating a grid.");*/

    def = this->add("assemble", coBool);
    def->label = "Assemble";
    def->tooltip = "Arrange the supplied models in a plate and merge them in a single model in order to perform actions once.";
    //def->cli = "merge|m";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("convert_unit", coBool);
    def->label = "Convert Unit";
    def->tooltip = "Convert the units of model";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("orient", coInt);
    def->label = L("Orient Options");
    def->tooltip = L("Orient options: 0-disable, 1-enable, others-auto");
    //def->cli = "orient|o";
    def->set_default_value(new ConfigOptionInt(0));

    /*def = this->add("repair", coBool);
    def->label = L("Repair");
    def->tooltip = L("Repair the model's meshes if it is non-manifold mesh");
    def->set_default_value(new ConfigOptionBool(false));*/

    def = this->add("rotate", coFloat);
    def->label = L("Rotate");
    def->tooltip = L("Rotation angle around the Z axis in degrees.");
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("rotate_x", coFloat);
    def->label = "Rotate around X";
    def->tooltip = "Rotation angle around the X axis in degrees.";
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("rotate_y", coFloat);
    def->label = L("Rotate around Y");
    def->tooltip = L("Rotation angle around the Y axis in degrees.");
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("scale", coFloat);
    def->label = "Scale";
    def->tooltip = "Scale the model by a float factor";
    def->cli_params = "factor";
    def->set_default_value(new ConfigOptionFloat(1.f));

    /*def = this->add("split", coBool);
    def->label = L("Split");
    def->tooltip = L("Detect unconnected parts in the given model(s) and split them into separate objects.");

    def = this->add("scale_to_fit", coPoint3);
    def->label = L("Scale to Fit");
    def->tooltip = L("Scale to fit the given volume.");
    def->set_default_value(new ConfigOptionPoint3(Vec3d(0,0,0)));*/
}

CLIMiscConfigDef::CLIMiscConfigDef()
{
    ConfigOptionDef* def;

    /*def = this->add("ignore_nonexistent_config", coBool);
    def->label = L("Ignore non-existent config files");
    def->tooltip = L("Do not fail if a file supplied to --load does not exist.");

    def = this->add("config_compatibility", coEnum);
    def->label = L("Forward-compatibility rule when loading configurations from config files and project files (3MF, AMF).");
    def->tooltip = L("This version of OrcaSlicer may not understand configurations produced by the newest OrcaSlicer versions. "
                     "For example, newer OrcaSlicer may extend the list of supported firmware flavors. One may decide to "
                     "bail out or to substitute an unknown value with a default silently or verbosely.");
    def->enum_keys_map = &ConfigOptionEnum<ForwardCompatibilitySubstitutionRule>::get_enum_values();
    def->enum_values.push_back("disable");
    def->enum_values.push_back("enable");
    def->enum_values.push_back("enable_silent");
    def->enum_labels.push_back(L("Bail out on unknown configuration values"));
    def->enum_labels.push_back(L("Enable reading unknown configuration values by verbosely substituting them with defaults."));
    def->enum_labels.push_back(L("Enable reading unknown configuration values by silently substituting them with defaults."));
    def->set_default_value(new ConfigOptionEnum<ForwardCompatibilitySubstitutionRule>(ForwardCompatibilitySubstitutionRule::Enable));*/

    /*def = this->add("load", coStrings);
    def->label = L("Load config file");
    def->tooltip = L("Load configuration from the specified file. It can be used more than once to load options from multiple files.");*/

    def = this->add("load_settings", coStrings);
    def->label = "Load General Settings";
    def->tooltip = "Load process/machine settings from the specified file";
    def->cli_params = "\"setting1.json;setting2.json\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("load_filaments", coStrings);
    def->label = "Load Filament Settings";
    def->tooltip = "Load filament settings from the specified file list";
    def->cli_params = "\"filament1.json;filament2.json;...\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("skip_objects", coInts);
    def->label = "Skip Objects";
    def->tooltip = "Skip some objects in this print";
    def->cli_params = "\"3,5,10,77\"";
    def->set_default_value(new ConfigOptionInts());

    def = this->add("clone_objects", coInts);
    def->label = "Clone Objects";
    def->tooltip = "Clone objects in the load list";
    def->cli_params = "\"1,3,1,10\"";
    def->set_default_value(new ConfigOptionInts());

    def = this->add("uptodate_settings", coStrings);
    def->label = "load uptodate process/machine settings when using uptodate";
    def->tooltip = "load uptodate process/machine settings from the specified file when using uptodate";
    def->cli_params = "\"setting1.json;setting2.json\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("uptodate_filaments", coStrings);
    def->label = "load uptodate filament settings when using uptodate";
    def->tooltip = "load uptodate filament settings from the specified file when using uptodate";
    def->cli_params = "\"filament1.json;filament2.json;...\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("downward_check", coBool);
    def->label = "downward machines check";
    def->tooltip = "if enabled, check whether current machine downward compatible with the machines in the list";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("downward_settings", coStrings);
    def->label = "downward machines settings";
    def->tooltip = "the machine settings list need to do downward checking";
    def->cli_params = "\"machine1.json;machine2.json;...\"";
    def->set_default_value(new ConfigOptionStrings());
    
    def = this->add("load_assemble_list", coString);
    def->label = "Load assemble list";
    def->tooltip = "Load assemble object list from config file";
    def->cli_params = "assemble_list.json";
    def->set_default_value(new ConfigOptionString());

    /*def = this->add("output", coString);
    def->label = L("Output File");
    def->tooltip = L("The file where the output will be written (if not specified, it will be based on the input file).");
    def->cli = "output|o";

    def = this->add("single_instance", coBool);
    def->label = L("Single instance mode");
    def->tooltip = L("If enabled, the command line arguments are sent to an existing instance of GUI OrcaSlicer, "
                     "or an existing OrcaSlicer window is activated. "
                     "Overrides the \"single_instance\" configuration value from application preferences.");*/

/*
    def = this->add("autosave", coString);
    def->label = L("Autosave");
    def->tooltip = L("Automatically export current configuration to the specified file.");
*/

    def = this->add("datadir", coString);
    def->label = L("Data directory");
    def->tooltip = L("Load and store settings at the given directory. This is useful for maintaining different profiles or including configurations from a network storage.");


    def = this->add("outputdir", coString);
    def->label = "Output directory";
    def->tooltip = "Output directory for the exported files.";
    def->cli_params = "dir";
    def->set_default_value(new ConfigOptionString());

    def = this->add("debug", coInt);
    def->label = "Debug level";
    def->tooltip = "Sets debug logging level. 0:fatal, 1:error, 2:warning, 3:info, 4:debug, 5:trace\n";
    def->min = 0;
    def->cli_params = "level";
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("enable_timelapse", coBool);
    def->label = "Enable timeplapse for print";
    def->tooltip = "If enabled, this slicing will be considered using timelapse";
    def->set_default_value(new ConfigOptionBool(false));

#if (defined(_MSC_VER) || defined(__MINGW32__)) && defined(SLIC3R_GUI)
    /*def = this->add("sw_renderer", coBool);
    def->label = L("Render with a software renderer");
    def->tooltip = L("Render with a software renderer. The bundled MESA software renderer is loaded instead of the default OpenGL driver.");
    def->min = 0;*/
#endif /* _MSC_VER */

    def = this->add("load_custom_gcodes", coString);
    def->label = L("Load custom gcode");
    def->tooltip = L("Load custom gcode from json");
    def->cli_params = "custom_gcode_toolchange.json";
    def->set_default_value(new ConfigOptionString());

    def = this->add("load_filament_ids", coInts);
    def->label = "Load filament ids";
    def->tooltip = "Load filament ids for each object";
    def->cli_params = "\"1,2,3,1\"";
    def->set_default_value(new ConfigOptionInts());

    def = this->add("allow_multicolor_oneplate", coBool);
    def->label = "Allow multiple color on one plate";
    def->tooltip = "If enabled, the arrange will allow multiple color on one plate";
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("allow_rotations", coBool);
    def->label = "Allow rotatations when arrange";
    def->tooltip = "If enabled, the arrange will allow rotations when place object";
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("avoid_extrusion_cali_region", coBool);
    def->label = "Avoid extrusion calibrate region when doing arrange";
    def->tooltip = "If enabled, the arrange will avoid extrusion calibrate region when place object";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("skip_modified_gcodes", coBool);
    def->label = "Skip modified gcodes in 3mf";
    def->tooltip = "Skip the modified gcodes in 3mf from Printer or filament Presets";
    def->cli_params = "option";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("makerlab_name", coString);
    def->label = "MakerLab name";
    def->tooltip = "MakerLab name to generate this 3mf";
    def->cli_params = "name";
    def->set_default_value(new ConfigOptionString());

    def = this->add("makerlab_version", coString);
    def->label = "MakerLab version";
    def->tooltip = "MakerLab version to generate this 3mf";
    def->cli_params = "version";
    def->set_default_value(new ConfigOptionString());

    def = this->add("metadata_name", coStrings);
    def->label = "metadata name list";
    def->tooltip = "matadata name list added into 3mf";
    def->cli_params = "\"name1;name2;...\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("metadata_value", coStrings);
    def->label = "metadata value list";
    def->tooltip = "matadata value list added into 3mf";
    def->cli_params = "\"value1;value2;...\"";
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("allow_newer_file", coBool);
    def->label = "Allow 3mf with newer version to be sliced";
    def->tooltip = "Allow 3mf with newer version to be sliced";
    def->cli_params = "option";
    def->set_default_value(new  ConfigOptionBool(false));
}

const CLIActionsConfigDef    cli_actions_config_def;
const CLITransformConfigDef  cli_transform_config_def;
const CLIMiscConfigDef       cli_misc_config_def;

DynamicPrintAndCLIConfig::PrintAndCLIConfigDef DynamicPrintAndCLIConfig::s_def;

void DynamicPrintAndCLIConfig::handle_legacy(t_config_option_key &opt_key, std::string &value) const
{
    if (cli_actions_config_def  .options.find(opt_key) == cli_actions_config_def  .options.end() &&
        cli_transform_config_def.options.find(opt_key) == cli_transform_config_def.options.end() &&
        cli_misc_config_def     .options.find(opt_key) == cli_misc_config_def     .options.end()) {
        PrintConfigDef::handle_legacy(opt_key, value);
    }
}

// SlicingStatesConfigDefs

// Create a new config definition with a label and tooltip
// Note: the L() macro is already used for LABEL and TOOLTIP
#define new_def(OPT_KEY, TYPE, LABEL, TOOLTIP) \
        def = this->add(OPT_KEY, TYPE); \
        def->label = L(LABEL); \
        def->tooltip = L(TOOLTIP);

ReadOnlySlicingStatesConfigDef::ReadOnlySlicingStatesConfigDef()
{
    ConfigOptionDef* def;

    def = this->add("zhop", coFloat);
    def->label = L("Current z-hop");
    def->tooltip = L("Contains z-hop present at the beginning of the custom G-code block.");
}

ReadWriteSlicingStatesConfigDef::ReadWriteSlicingStatesConfigDef()
{
    ConfigOptionDef* def;

    def = this->add("position", coFloats);
    def->label = L("Position");
    def->tooltip = L("Position of the extruder at the beginning of the custom G-code block. If the custom G-code travels somewhere else, "
                     "it should write to this variable so OrcaSlicer knows where it travels from when it gets control back.");

    def = this->add("e_retracted", coFloats);
    def->label = L("Retraction");
    def->tooltip = L("Retraction state at the beginning of the custom G-code block. If the custom G-code moves the extruder axis, "
                     "it should write to this variable so OrcaSlicer de-retracts correctly when it gets control back.");

    def = this->add("e_restart_extra", coFloats);
    def->label = L("Extra de-retraction");
    def->tooltip = L("Currently planned extra extruder priming after de-retraction.");

   def = this->add("e_position", coFloats);
   def->label = L("Absolute E position");
   def->tooltip = L("Current position of the extruder axis. Only used with absolute extruder addressing.");
}

OtherSlicingStatesConfigDef::OtherSlicingStatesConfigDef()
{
    ConfigOptionDef* def;

    def = this->add("current_extruder", coInt);
    def->label = L("Current extruder");
    def->tooltip = L("Zero-based index of currently used extruder.");

    def = this->add("current_object_idx", coInt);
    def->label = L("Current object index");
    def->tooltip = L("Specific for sequential printing. Zero-based index of currently printed object.");

    def = this->add("has_wipe_tower", coBool);
    def->label = L("Has wipe tower");
    def->tooltip = L("Whether or not wipe tower is being generated in the print.");

    def = this->add("initial_extruder", coInt);
    def->label = L("Initial extruder");
    def->tooltip = L("Zero-based index of the first extruder used in the print. Same as initial_tool.");

    def = this->add("initial_tool", coInt);
    def->label = L("Initial tool");
    def->tooltip = L("Zero-based index of the first extruder used in the print. Same as initial_extruder.");

    def = this->add("is_extruder_used", coBools);
    def->label = L("Is extruder used?");
    def->tooltip = L("Vector of booleans stating whether a given extruder is used in the print.");

    // Options from PS not used in Orca
    //    def = this->add("initial_filament_type", coString);
    //    def->label = L("Initial filament type");
    //    def->tooltip = L("String containing filament type of the first used extruder.");

    def          = this->add("has_single_extruder_multi_material_priming", coBool);
    def->label   = L("Has single extruder MM priming");
    def->tooltip = L("Are the extra multi-material priming regions used in this print?");

    new_def("initial_no_support_extruder", coInt, "Initial no support extruder", "Zero-based index of the first extruder used for printing without support. Same as initial_no_support_tool.");
    new_def("in_head_wrap_detect_zone", coBool, "In head wrap detect zone", "Indicates if the first layer overlaps with the head wrap zone.");
}

PrintStatisticsConfigDef::PrintStatisticsConfigDef()
{
    ConfigOptionDef* def;

    def = this->add("extruded_volume", coFloats);
    def->label = L("Volume per extruder");
    def->tooltip = L("Total filament volume extruded per extruder during the entire print.");

    def = this->add("total_toolchanges", coInt);
    def->label = L("Total toolchanges");
    def->tooltip = L("Number of toolchanges during the print.");

    def = this->add("extruded_volume_total", coFloat);
    def->label = L("Total volume");
    def->tooltip = L("Total volume of filament used during the entire print.");

    def = this->add("extruded_weight", coFloats);
    def->label = L("Weight per extruder");
    def->tooltip = L("Weight per extruder extruded during the entire print. Calculated from filament_density value in Filament Settings.");

    def = this->add("extruded_weight_total", coFloat);
    def->label = L("Total weight");
    def->tooltip = L("Total weight of the print. Calculated from filament_density value in Filament Settings.");

    def = this->add("total_layer_count", coInt);
    def->label = L("Total layer count");
    def->tooltip = L("Number of layers in the entire print.");

    // Options from PS not used in Orca
    /*    def = this->add("normal_print_time", coString);
    def->label = L("Print time (normal mode)");
    def->tooltip = L("Estimated print time when printed in normal mode (i.e. not in silent mode). Same as print_time.");

    def = this->add("num_printing_extruders", coInt);
    def->label = L("Number of printing extruders");
    def->tooltip = L("Number of extruders used during the print.");

    def = this->add("print_time", coString);
    def->label = L("Print time (normal mode)");
    def->tooltip = L("Estimated print time when printed in normal mode (i.e. not in silent mode). Same as normal_print_time.");

    def = this->add("printing_filament_types", coString);
    def->label = L("Used filament types");
    def->tooltip = L("Comma-separated list of all filament types used during the print.");

    def = this->add("silent_print_time", coString);
    def->label = L("Print time (silent mode)");
    def->tooltip = L("Estimated print time when printed in silent mode.");

    def = this->add("total_cost", coFloat);
    def->label = L("Total cost");
    def->tooltip = L("Total cost of all material used in the print. Calculated from filament_cost value in Filament Settings.");

    def = this->add("total_weight", coFloat);
    def->label = L("Total weight");
    def->tooltip = L("Total weight of the print. Calculated from filament_density value in Filament Settings.");

    def = this->add("total_wipe_tower_cost", coFloat);
    def->label = L("Total wipe tower cost");
    def->tooltip = L("Total cost of the material wasted on the wipe tower. Calculated from filament_cost value in Filament Settings.");

    def = this->add("total_wipe_tower_filament", coFloat);
    def->label = L("Wipe tower volume");
    def->tooltip = L("Total filament volume extruded on the wipe tower.");

    def = this->add("used_filament", coFloat);
    def->label = L("Used filament");
    def->tooltip = L("Total length of filament used in the print.");*/
}

ObjectsInfoConfigDef::ObjectsInfoConfigDef()
{
    ConfigOptionDef* def;

    def = this->add("num_objects", coInt);
    def->label = L("Number of objects");
    def->tooltip = L("Total number of objects in the print.");

    def = this->add("num_instances", coInt);
    def->label = L("Number of instances");
    def->tooltip = L("Total number of object instances in the print, summed over all objects.");

    def = this->add("scale", coStrings);
    def->label = L("Scale per object");
    def->tooltip = L("Contains a string with the information about what scaling was applied to the individual objects. "
                     "Indexing of the objects is zero-based (first object has index 0).\n"
                     "Example: 'x:100% y:50% z:100'.");

    def = this->add("input_filename_base", coString);
    def->label = L("Input filename without extension");
    def->tooltip = L("Source filename of the first object, without extension.");

    new_def("input_filename", coString, "Full input filename", "Source filename of the first object.");
    new_def("plate_name", coString, "Plate name", "Name of the plate sliced.");
}

DimensionsConfigDef::DimensionsConfigDef()
{
    ConfigOptionDef* def;

    const std::string point_tooltip   = L("The vector has two elements: x and y coordinate of the point. Values in mm.");
    const std::string bb_size_tooltip = L("The vector has two elements: x and y dimension of the bounding box. Values in mm.");

    def = this->add("first_layer_print_convex_hull", coPoints);
    def->label = L("First layer convex hull");
    def->tooltip = L("Vector of points of the first layer convex hull. Each element has the following format:"
                     "'[x, y]' (x and y are floating-point numbers in mm).");

    def = this->add("first_layer_print_min", coFloats);
    def->label = L("Bottom-left corner of first layer bounding box");
    def->tooltip = point_tooltip;

    def = this->add("first_layer_print_max", coFloats);
    def->label = L("Top-right corner of first layer bounding box");
    def->tooltip = point_tooltip;

    def = this->add("first_layer_print_size", coFloats);
    def->label = L("Size of the first layer bounding box");
    def->tooltip = bb_size_tooltip;

    def = this->add("print_bed_min", coFloats);
    def->label = L("Bottom-left corner of print bed bounding box");
    def->tooltip = point_tooltip;

    def = this->add("print_bed_max", coFloats);
    def->label = L("Top-right corner of print bed bounding box");
    def->tooltip = point_tooltip;

    def = this->add("print_bed_size", coFloats);
    def->label = L("Size of the print bed bounding box");
    def->tooltip = bb_size_tooltip;

    new_def("first_layer_center_no_wipe_tower", coFloats, "First layer center without wipe tower", point_tooltip);
    new_def("first_layer_height", coFloat, "First layer height", "Height of the first layer.");
}

TemperaturesConfigDef::TemperaturesConfigDef()
{
    ConfigOptionDef* def;

    new_def("bed_temperature", coInts, "Bed temperature", "Vector of bed temperatures for each extruder/filament.")
    new_def("bed_temperature_initial_layer", coInts, "Initial layer bed temperature", "Vector of initial layer bed temperatures for each extruder/filament. Provides the same value as first_layer_bed_temperature.")
    new_def("bed_temperature_initial_layer_single", coInt, "Initial layer bed temperature (initial extruder)", "Initial layer bed temperature for the initial extruder. Same as bed_temperature_initial_layer[initial_extruder]")
    new_def("chamber_temperature", coInts, "Chamber temperature", "Vector of chamber temperatures for each extruder/filament.")
    new_def("overall_chamber_temperature", coInt, "Overall chamber temperature", "Overall chamber temperature. This value is the maximum chamber temperature of any extruder/filament used.")
    new_def("first_layer_bed_temperature", coInts, "First layer bed temperature", "Vector of first layer bed temperatures for each extruder/filament. Provides the same value as bed_temperature_initial_layer.")
    new_def("first_layer_temperature", coInts, "First layer temperature", "Vector of first layer temperatures for each extruder/filament.")
}


TimestampsConfigDef::TimestampsConfigDef()
{
    ConfigOptionDef* def;

    def = this->add("timestamp", coString);
    def->label = L("Timestamp");
    def->tooltip = L("String containing current time in yyyyMMdd-hhmmss format.");

    def = this->add("year", coInt);
    def->label = L("Year");

    def = this->add("month", coInt);
    def->label = L("Month");

    def = this->add("day", coInt);
    def->label = L("Day");

    def = this->add("hour", coInt);
    def->label = L("Hour");

    def = this->add("minute", coInt);
    def->label = L("Minute");

    def = this->add("second", coInt);
    def->label = L("Second");
}

OtherPresetsConfigDef::OtherPresetsConfigDef()
{
    ConfigOptionDef* def;

    def = this->add("print_preset", coString);
    def->label = L("Print preset name");
    def->tooltip = L("Name of the print preset used for slicing.");

    def = this->add("filament_preset", coString);
    def->label = L("Filament preset name");
    def->tooltip = L("Names of the filament presets used for slicing. The variable is a vector "
                     "containing one name for each extruder.");

    def = this->add("printer_preset", coString);
    def->label = L("Printer preset name");
    def->tooltip = L("Name of the printer preset used for slicing.");

    def = this->add("physical_printer_preset", coString);
    def->label = L("Physical printer name");
    def->tooltip = L("Name of the physical printer used for slicing.");

    def          = this->add("num_extruders", coInt);
    def->label   = L("Number of extruders");
    def->tooltip = L("Total number of extruders, regardless of whether they are used in the current print.");
}


static std::map<t_custom_gcode_key, t_config_option_keys> s_CustomGcodeSpecificPlaceholders{
    // Machine Gcode
    {"machine_start_gcode",         {}},
    {"machine_end_gcode",           {"layer_num", "layer_z", "max_layer_z", "filament_extruder_id"}},
    {"before_layer_change_gcode",   {"layer_num", "layer_z", "max_layer_z"}},
    {"layer_change_gcode",          {"layer_num", "layer_z", "max_layer_z"}},
    {"timelapse_gcode",             {"layer_num", "layer_z", "max_layer_z"}},
    {"change_filament_gcode",       {"layer_num", "layer_z", "max_layer_z", "next_extruder", "previous_extruder", "fan_speed",
                               "first_flush_volume", "flush_length_1", "flush_length_2", "flush_length_3", "flush_length_4",
                               "new_filament_e_feedrate", "new_filament_temp", "new_retract_length",
                               "new_retract_length_toolchange", "old_filament_e_feedrate", "old_filament_temp", "old_retract_length",
                               "old_retract_length_toolchange", "relative_e_axis", "second_flush_volume", "toolchange_count", "toolchange_z",
                               "travel_point_1_x", "travel_point_1_y", "travel_point_2_x", "travel_point_2_y", "travel_point_3_x",
                               "travel_point_3_y", "x_after_toolchange", "y_after_toolchange", "z_after_toolchange"}},
    {"change_extrusion_role_gcode", {"layer_num", "layer_z", "extrusion_role", "last_extrusion_role"}},
    {"printing_by_object_gcode",    {}},
    {"machine_pause_gcode",         {}},
    {"template_custom_gcode",       {}},
    //Filament Gcode
    {"filament_start_gcode",        {"filament_extruder_id"}},
    {"filament_end_gcode",          {"layer_num", "layer_z", "max_layer_z", "filament_extruder_id"}},
};

const std::map<t_custom_gcode_key, t_config_option_keys>& custom_gcode_specific_placeholders()
{
    return s_CustomGcodeSpecificPlaceholders;
}

CustomGcodeSpecificConfigDef::CustomGcodeSpecificConfigDef()
{
    ConfigOptionDef* def;

// Common Defs
    def = this->add("layer_num", coInt);
    def->label = L("Layer number");
    def->tooltip = L("Index of the current layer. One-based (i.e. first layer is number 1).");

    def = this->add("layer_z", coFloat);
    def->label = L("Layer z");
    def->tooltip = L("Height of the current layer above the print bed, measured to the top of the layer.");

    def = this->add("max_layer_z", coFloat);
    def->label = L("Maximal layer z");
    def->tooltip = L("Height of the last layer above the print bed.");

    def = this->add("filament_extruder_id", coInt);
    def->label = L("Filament extruder ID");
    def->tooltip = L("The current extruder ID. The same as current_extruder.");

// change_filament_gcode
    new_def("previous_extruder", coInt, "Previous extruder", "Index of the extruder that is being unloaded. The index is zero based (first extruder has index 0).");
    new_def("next_extruder", coInt, "Next extruder", "Index of the extruder that is being loaded. The index is zero based (first extruder has index 0).");
    new_def("relative_e_axis", coBool, "Relative e-axis", "Indicates if relative positioning is being used");
    new_def("toolchange_count", coInt, "Toolchange count", "The number of toolchanges throught the print");
    new_def("fan_speed", coNone, "", ""); //Option is no longer used and is zeroed by placeholder parser for compatability
    new_def("old_retract_length", coFloat, "Old retract length", "The retraction length of the previous filament");
    new_def("new_retract_length", coFloat, "New retract length", "The retraction lenght of the new filament");
    new_def("old_retract_length_toolchange", coFloat, "Old retract length toolchange", "The toolchange retraction length of the previous filament");
    new_def("new_retract_length_toolchange", coFloat, "New retract length toolchange", "The toolchange retraction length of the new filament");
    new_def("old_filament_temp", coInt, "Old filament temp", "The old filament temp");
    new_def("new_filament_temp", coInt, "New filament temp", "The new filament temp");
    new_def("x_after_toolchange", coFloat, "X after toolchange", "The x pos after toolchange");
    new_def("y_after_toolchange", coFloat, "Y after toolchange", "The y pos after toolchange");
    new_def("z_after_toolchange", coFloat, "Z after toolchange", "The z pos after toolchange");
    new_def("first_flush_volume", coFloat, "First flush volume", "The first flush volume");
    new_def("second_flush_volume", coFloat, "Second flush volume", "The second flush volume");
    new_def("old_filament_e_feedrate", coInt, "Old filament e feedrate", "The old filament extruder feedrate");
    new_def("new_filament_e_feedrate", coInt, "New filament e feedrate", "The new filament extruder feedrate");
    new_def("travel_point_1_x", coFloat, "Travel point 1 x", "The travel point 1 x");
    new_def("travel_point_1_y", coFloat, "Travel point 1 y", "The travel point 1 y");
    new_def("travel_point_2_x", coFloat, "Travel point 2 x", "The travel point 2 x");
    new_def("travel_point_2_y", coFloat, "Travel point 2 y", "The travel point 2 y");
    new_def("travel_point_3_x", coFloat, "Travel point 3 x", "The travel point 3 x");
    new_def("travel_point_3_y", coFloat, "Travel point 3 y", "The travel point 3 y");
    new_def("flush_length_1", coFloat, "Flush Length 1", "The first flush length");
    new_def("flush_length_2", coFloat, "Flush Length 2", "The second flush length");
    new_def("flush_length_3", coFloat, "Flush Length 3", "The third flush length");
    new_def("flush_length_4", coFloat, "Flush Length 4", "The fourth flush length");

// change_extrusion_role_gcode
    std::string extrusion_role_types = "Possible Values:\n[\"Perimeter\", \"ExternalPerimeter\", "
                                                     "\"OverhangPerimeter\", \"InternalInfill\", \"SolidInfill\", \"TopSolidInfill\", \"BottomSurface\", \"BridgeInfill\", \"GapFill\", \"Ironing\", "
                                                     "\"Skirt\", \"Brim\", \"SupportMaterial\", \"SupportMaterialInterface\", \"SupportTransition\", \"WipeTower\", \"Mixed\"]";

    new_def("extrusion_role", coString, "Extrusion role", "The new extrusion role/type that is going to be used\n" + extrusion_role_types);
    new_def("last_extrusion_role", coString, "Last extrusion role", "The previously used extrusion role/type\nPossible Values:\n" + extrusion_role_types);
}

const CustomGcodeSpecificConfigDef custom_gcode_specific_config_def;

#undef new_def

uint64_t ModelConfig::s_last_timestamp = 1;

static Points to_points(const std::vector<Vec2d> &dpts)
{
    Points pts; pts.reserve(dpts.size());
    for (auto &v : dpts)
        pts.emplace_back( coord_t(scale_(v.x())), coord_t(scale_(v.y())) );
    return pts;
}

Points get_bed_shape(const DynamicPrintConfig &config)
{
    const auto *bed_shape_opt = config.opt<ConfigOptionPoints>("printable_area");
    if (!bed_shape_opt) {

        // Here, it is certain that the bed shape is missing, so an infinite one
        // has to be used, but still, the center of bed can be queried
        if (auto center_opt = config.opt<ConfigOptionPoint>("center"))
            return { scaled(center_opt->value) };

        return {};
    }

    return to_points(bed_shape_opt->values);
}

Points get_bed_shape(const PrintConfig &cfg)
{
    return to_points(cfg.printable_area.values);
}

Points get_bed_shape(const SLAPrinterConfig &cfg) { return to_points(cfg.printable_area.values); }

Polygon get_bed_shape_with_excluded_area(const PrintConfig& cfg)
{
    Polygon bed_poly;
    bed_poly.points = get_bed_shape(cfg);

    Points excluse_area_points = to_points(cfg.bed_exclude_area.values);
    Polygons exclude_polys;
    Polygon exclude_poly;
    for (int i = 0; i < excluse_area_points.size(); i++) {
        auto pt = excluse_area_points[i];
        exclude_poly.points.emplace_back(pt);
        if (i % 4 == 3) {  // exclude areas are always rectangle
            exclude_polys.push_back(exclude_poly);
            exclude_poly.points.clear();
        }
    }
    auto tmp = diff({ bed_poly }, exclude_polys);
    if (!tmp.empty()) bed_poly = tmp[0];
    return bed_poly;
}
bool has_skirt(const DynamicPrintConfig& cfg)
{
    auto opt_skirt_height = cfg.option("skirt_height");
    auto opt_skirt_loops = cfg.option("skirt_loops");
    auto opt_draft_shield = cfg.option("draft_shield");
    return (opt_skirt_height && opt_skirt_height->getInt() > 0 && opt_skirt_loops && opt_skirt_loops->getInt() > 0)
        || (opt_draft_shield && opt_draft_shield->getInt() != dsDisabled);
}
float get_real_skirt_dist(const DynamicPrintConfig& cfg) {
    return has_skirt(cfg) ? cfg.opt_float("skirt_distance") : 0;
}
static bool is_XL_printer(const std::string& printer_notes)
{
    return boost::algorithm::contains(printer_notes, "PRINTER_VENDOR_PRUSA3D")
        && boost::algorithm::contains(printer_notes, "PRINTER_MODEL_XL");
}

bool is_XL_printer(const DynamicPrintConfig &cfg)
{
    auto *printer_notes = cfg.opt<ConfigOptionString>("printer_notes");
    return printer_notes && is_XL_printer(printer_notes->value);
}

bool is_XL_printer(const PrintConfig &cfg)
{
    return is_XL_printer(cfg.printer_notes.value);
}
} // namespace Slic3r

#include <cereal/types/polymorphic.hpp>
CEREAL_REGISTER_TYPE(Slic3r::DynamicPrintConfig)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::DynamicConfig, Slic3r::DynamicPrintConfig)
