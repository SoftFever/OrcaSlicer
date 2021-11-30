#include "PrintConfig.hpp"
#include "Config.hpp"
#include "I18N.hpp"

#include <set>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <boost/thread.hpp>

#include <float.h>

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

static t_config_enum_values s_keys_map_GCodeFlavor {
    { "reprap",         gcfRepRapSprinter },
    { "reprapfirmware", gcfRepRapFirmware },
    { "repetier",       gcfRepetier },
    { "teacup",         gcfTeacup },
    { "makerware",      gcfMakerWare },
    { "marlin",         gcfMarlinLegacy },
    { "marlin2",        gcfMarlinFirmware },
    { "sailfish",       gcfSailfish },
    { "smoothie",       gcfSmoothie },
    { "mach3",          gcfMach3 },
    { "machinekit",     gcfMachinekit },
    { "no-extrusion",   gcfNoExtrusion }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(GCodeFlavor)

static t_config_enum_values s_keys_map_MachineLimitsUsage {
    { "emit_to_gcode",      int(MachineLimitsUsage::EmitToGCode) },
    { "time_estimate_only", int(MachineLimitsUsage::TimeEstimateOnly) },
    { "ignore",             int(MachineLimitsUsage::Ignore) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(MachineLimitsUsage)

static t_config_enum_values s_keys_map_PrintHostType {
    { "prusalink",      htPrusaLink },
    { "octoprint",      htOctoPrint },
    { "duet",           htDuet },
    { "flashair",       htFlashAir },
    { "astrobox",       htAstroBox },
    { "repetier",       htRepetier },
    { "mks",            htMKS }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(PrintHostType)

static t_config_enum_values s_keys_map_AuthorizationType {
    { "key",            atKeyPassword },
    { "user",           atUserPassword }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(AuthorizationType)

static t_config_enum_values s_keys_map_FuzzySkinType {
    { "none",           int(FuzzySkinType::None) },
    { "external",       int(FuzzySkinType::External) },
    { "all",            int(FuzzySkinType::All) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(FuzzySkinType)

static t_config_enum_values s_keys_map_InfillPattern {
    { "rectilinear",        ipRectilinear },
    { "monotonic",          ipMonotonic },
    { "alignedrectilinear", ipAlignedRectilinear },
    { "grid",               ipGrid },
    { "triangles",          ipTriangles },
    { "stars",              ipStars },
    { "cubic",              ipCubic },
    { "line",               ipLine },
    { "concentric",         ipConcentric },
    { "honeycomb",          ipHoneycomb },
    { "3dhoneycomb",        ip3DHoneycomb },
    { "gyroid",             ipGyroid },
    { "hilbertcurve",       ipHilbertCurve },
    { "archimedeanchords",  ipArchimedeanChords },
    { "octagramspiral",     ipOctagramSpiral },
    { "adaptivecubic",      ipAdaptiveCubic },
    { "supportcubic",       ipSupportCubic }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(InfillPattern)

static t_config_enum_values s_keys_map_IroningType {
    { "top",            int(IroningType::TopSurfaces) },
    { "topmost",        int(IroningType::TopmostOnly) },
    { "solid",          int(IroningType::AllSolid) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(IroningType)

static t_config_enum_values s_keys_map_SlicingMode {
    { "regular",        int(SlicingMode::Regular) },
    { "even_odd",       int(SlicingMode::EvenOdd) },
    { "close_holes",    int(SlicingMode::CloseHoles) }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SlicingMode)

static t_config_enum_values s_keys_map_SupportMaterialPattern {
    { "rectilinear",        smpRectilinear },
    { "rectilinear-grid",   smpRectilinearGrid },
    { "honeycomb",          smpHoneycomb }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SupportMaterialPattern)

static t_config_enum_values s_keys_map_SupportMaterialStyle {
    { "grid",           smsGrid },
    { "snug",           smsSnug }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SupportMaterialStyle)

static t_config_enum_values s_keys_map_SupportMaterialInterfacePattern {
    { "auto",           smipAuto },
    { "rectilinear",    smipRectilinear },
    { "concentric",     smipConcentric }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SupportMaterialInterfacePattern)

static t_config_enum_values s_keys_map_SeamPosition {
    { "random",         spRandom },
    { "nearest",        spNearest },
    { "aligned",        spAligned },
    { "rear",           spRear }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(SeamPosition)

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
    {"outer_and_inner", btOuterAndInner}
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(BrimType)

static const t_config_enum_values s_keys_map_DraftShield = {
    { "disabled", dsDisabled },
    { "limited",  dsLimited  },
    { "enabled",  dsEnabled  }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(DraftShield)

static const t_config_enum_values s_keys_map_ForwardCompatibilitySubstitutionRule = {
    { "disable",        ForwardCompatibilitySubstitutionRule::Disable },
    { "enable",         ForwardCompatibilitySubstitutionRule::Enable },
    { "enable_silent",  ForwardCompatibilitySubstitutionRule::EnableSilent }
};
CONFIG_OPTION_ENUM_DEFINE_STATIC_MAPS(ForwardCompatibilitySubstitutionRule)

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
    def->label = L("Printer technology");
    def->tooltip = L("Printer technology");
    def->enum_keys_map = &ConfigOptionEnum<PrinterTechnology>::get_enum_values();
    def->enum_values.push_back("FFF");
    def->enum_values.push_back("SLA");
    def->set_default_value(new ConfigOptionEnum<PrinterTechnology>(ptFFF));

    def = this->add("bed_shape", coPoints);
    def->label = L("Bed shape");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPoints{ Vec2d(0, 0), Vec2d(200, 0), Vec2d(200, 200), Vec2d(0, 200) });

    def = this->add("bed_custom_texture", coString);
    def->label = L("Bed custom texture");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("bed_custom_model", coString);
    def->label = L("Bed custom model");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("elefant_foot_compensation", coFloat);
    def->label = L("Elephant foot compensation");
    def->category = L("Advanced");
    def->tooltip = L("The first layer will be shrunk in the XY plane by the configured value "
                     "to compensate for the 1st layer squish aka an Elephant Foot effect.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("thumbnails", coPoints);
    def->label = L("G-code thumbnails");
    def->tooltip = L("Picture sizes to be stored into a .gcode and .sl1 / .sl1s files, in the following format: \"XxY, XxY, ...\"");
    def->mode = comExpert;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionPoints());

    def = this->add("layer_height", coFloat);
    def->label = L("Layer height");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("This setting controls the height (and thus the total number) of the slices/layers. "
                   "Thinner layers give better accuracy but take more time to print.");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.3));

    def = this->add("max_print_height", coFloat);
    def->label = L("Max print height");
    def->tooltip = L("Set this to the maximum height that can be reached by your extruder while printing.");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 1200;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(200.0));

    def = this->add("print_host", coString);
    def->label = L("Hostname, IP or URL");
    def->tooltip = L("Slic3r can upload G-code files to a printer host. This field should contain "
                   "the hostname, IP address or URL of the printer host instance. "
                   "Print host behind HAProxy with basic auth enabled can be accessed by putting the user name and password into the URL "
                   "in the following format: https://username:password@your-octopi-address/");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("printhost_apikey", coString);
    def->label = L("API Key / Password");
    def->tooltip = L("Slic3r can upload G-code files to a printer host. This field should contain "
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
//    def->tooltip = L("");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString(""));
    
    def = this->add("printhost_password", coString);
    def->label = L("Password");
//    def->tooltip = L("");
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
//    def->tooltip = L("");
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

    def = this->add("avoid_crossing_perimeters", coBool);
    def->label = L("Avoid crossing perimeters");
    def->tooltip = L("Optimize travel moves in order to minimize the crossing of perimeters. "
                   "This is mostly useful with Bowden extruders which suffer from oozing. "
                   "This feature slows down both the print and the G-code generation.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("avoid_crossing_perimeters_max_detour", coFloatOrPercent);
    def->label = L("Avoid crossing perimeters - Max detour length");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("The maximum detour length for avoid crossing perimeters. "
                     "If the detour is longer than this value, avoid crossing perimeters is not applied for this travel path. "
                     "Detour length could be specified either as an absolute value or as percentage (for example 50%) of a direct travel path.");
    def->sidetext = L("mm or % (zero to disable)");
    def->min = 0;
    def->max_literal = 1000;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(0., false));

    def = this->add("bed_temperature", coInts);
    def->label = L("Other layers");
    def->tooltip = L("Bed temperature for layers after the first one. "
                   "Set this to zero to disable bed temperature control commands in the output.");
    def->sidetext = L("°C");
    def->full_label = L("Bed temperature");
    def->min = 0;
    def->max = 300;
    def->set_default_value(new ConfigOptionInts { 0 });

    def = this->add("before_layer_gcode", coString);
    def->label = L("Before layer change G-code");
    def->tooltip = L("This custom code is inserted at every layer change, right before the Z move. "
                   "Note that you can use placeholder variables for all Slic3r settings as well "
                   "as [layer_num] and [layer_z].");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("between_objects_gcode", coString);
    def->label = L("Between objects G-code");
    def->tooltip = L("This code is inserted between objects when using sequential printing. By default extruder and bed temperature are reset using non-wait command; however if M104, M109, M140 or M190 are detected in this custom code, Slic3r will not add temperature commands. Note that you can use placeholder variables for all Slic3r settings, so you can put a \"M109 S[first_layer_temperature]\" command wherever you want.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("bottom_solid_layers", coInt);
    //TRN To be shown in Print Settings "Bottom solid layers"
    def->label = L("Bottom");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Number of solid layers to generate on bottom surfaces.");
    def->full_label = L("Bottom solid layers");
    def->min = 0;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("bottom_solid_min_thickness", coFloat);
    //TRN To be shown in Print Settings "Top solid layers"
    def->label = L("Bottom");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("The number of bottom solid layers is increased above bottom_solid_layers if necessary to satisfy "
    				 "minimum thickness of bottom shell.");
    def->full_label = L("Minimum bottom shell thickness");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("bridge_acceleration", coFloat);
    def->label = L("Bridge");
    def->tooltip = L("This is the acceleration your printer will use for bridges. "
                   "Set zero to disable acceleration control for bridges.");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("bridge_angle", coFloat);
    def->label = L("Bridging angle");
    def->category = L("Infill");
    def->tooltip = L("Bridging angle override. If left to zero, the bridging angle will be calculated "
                   "automatically. Otherwise the provided angle will be used for all bridges. "
                   "Use 180° for zero angle.");
    def->sidetext = L("°");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("bridge_fan_speed", coInts);
    def->label = L("Bridges fan speed");
    def->tooltip = L("This fan speed is enforced during all bridges and overhangs.");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts { 100 });

    def = this->add("bridge_flow_ratio", coFloat);
    def->label = L("Bridge flow ratio");
    def->category = L("Advanced");
    def->tooltip = L("This factor affects the amount of plastic for bridging. "
                   "You can decrease it slightly to pull the extrudates and prevent sagging, "
                   "although default settings are usually good and you should experiment "
                   "with cooling (use a fan) before tweaking this.");
    def->min = 0;
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("bridge_speed", coFloat);
    def->label = L("Bridges");
    def->category = L("Speed");
    def->tooltip = L("Speed for printing bridges.");
    def->sidetext = L("mm/s");
    def->aliases = { "bridge_feed_rate" };
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(60));

    def = this->add("brim_width", coFloat);
    def->label = L("Brim width");
    def->category = L("Skirt and brim");
    def->tooltip = L("The horizontal width of the brim that will be printed around each object on the first layer. "
                     "When raft is used, no brim is generated (use raft_first_layer_expansion).");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 200;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("brim_type", coEnum);
    def->label = L("Brim type");
    def->category = L("Skirt and brim");
    def->tooltip = L("The places where the brim will be printed around each object on the first layer.");
    def->enum_keys_map = &ConfigOptionEnum<BrimType>::get_enum_values();
    def->enum_values.emplace_back("no_brim");
    def->enum_values.emplace_back("outer_only");
    def->enum_values.emplace_back("inner_only");
    def->enum_values.emplace_back("outer_and_inner");
    def->enum_labels.emplace_back(L("No brim"));
    def->enum_labels.emplace_back(L("Outer brim only"));
    def->enum_labels.emplace_back(L("Inner brim only"));
    def->enum_labels.emplace_back(L("Outer and inner brim"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<BrimType>(btOuterOnly));

    def = this->add("brim_separation", coFloat);
    def->label = L("Brim separation gap");
    def->category = L("Skirt and brim");
    def->tooltip = L("Offset of brim from the printed object. The offset is applied after the elephant foot compensation.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.f));

    def = this->add("clip_multipart_objects", coBool);
    def->label = L("Clip multi-part objects");
    def->tooltip = L("When printing multi-material objects, this settings will make Slic3r "
                   "to clip the overlapping object parts one by the other "
                   "(2nd part will be clipped by the 1st, 3rd part will be clipped by the 1st and 2nd etc).");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("colorprint_heights", coFloats);
    def->label = L("Colorprint height");
    def->tooltip = L("Heights at which a filament change is to occur.");
    def->set_default_value(new ConfigOptionFloats { });

    def = this->add("compatible_printers", coStrings);
    def->label = L("Compatible printers");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("compatible_printers_condition", coString);
    def->label = L("Compatible printers condition");
    def->tooltip = L("A boolean expression using the configuration values of an active printer profile. "
                   "If this expression evaluates to true, this profile is considered compatible "
                   "with the active printer profile.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("compatible_prints", coStrings);
    def->label = L("Compatible print profiles");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("compatible_prints_condition", coString);
    def->label = L("Compatible print profiles condition");
    def->tooltip = L("A boolean expression using the configuration values of an active print profile. "
                   "If this expression evaluates to true, this profile is considered compatible "
                   "with the active print profile.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    // The following value is to be stored into the project file (AMF, 3MF, Config ...)
    // and it contains a sum of "compatible_printers_condition" values over the print and filament profiles.
    def = this->add("compatible_printers_condition_cummulative", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;
    def = this->add("compatible_prints_condition_cummulative", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("complete_objects", coBool);
    def->label = L("Complete individual objects");
    def->tooltip = L("When printing multiple objects or copies, this feature will complete "
                   "each object before moving onto next one (and starting it from its bottom layer). "
                   "This feature is useful to avoid the risk of ruined prints. "
                   "Slic3r should warn and prevent you from extruder collisions, but beware.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("cooling", coBools);
    def->label = L("Enable auto cooling");
    def->tooltip = L("This flag enables the automatic cooling logic that adjusts print speed "
                   "and fan speed according to layer printing time.");
    def->set_default_value(new ConfigOptionBools { true });

    def = this->add("cooling_tube_retraction", coFloat);
    def->label = L("Cooling tube position");
    def->tooltip = L("Distance of the center-point of the cooling tube from the extruder tip.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(91.5f));

    def = this->add("cooling_tube_length", coFloat);
    def->label = L("Cooling tube length");
    def->tooltip = L("Length of the cooling tube to limit space for cooling moves inside it.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.f));

    def = this->add("default_acceleration", coFloat);
    def->label = L("Default");
    def->tooltip = L("This is the acceleration your printer will be reset to after "
                   "the role-specific acceleration values are used (perimeter/infill). "
                   "Set zero to prevent resetting acceleration at all.");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("default_filament_profile", coStrings);
    def->label = L("Default filament profile");
    def->tooltip = L("Default filament profile associated with the current printer profile. "
                   "On selection of the current printer profile, this filament profile will be activated.");
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("default_print_profile", coString);
    def->label = L("Default print profile");
    def->tooltip = L("Default print profile associated with the current printer profile. "
                   "On selection of the current printer profile, this print profile will be activated.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("disable_fan_first_layers", coInts);
    def->label = L("Disable fan for the first");
    def->tooltip = L("You can set this to a positive value to disable fan at all "
                   "during the first layers, so that it does not make adhesion worse.");
    def->sidetext = L("layers");
    def->min = 0;
    def->max = 1000;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts { 3 });

    def = this->add("dont_support_bridges", coBool);
    def->label = L("Don't support bridges");
    def->category = L("Support material");
    def->tooltip = L("Experimental option for preventing support material from being generated "
                   "under bridged areas.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("duplicate_distance", coFloat);
    def->label = L("Distance between copies");
    def->tooltip = L("Distance used for the auto-arrange feature of the plater.");
    def->sidetext = L("mm");
    def->aliases = { "multiply_distance" };
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(6));

    def = this->add("end_gcode", coString);
    def->label = L("End G-code");
    def->tooltip = L("This end procedure is inserted at the end of the output file. "
                   "Note that you can use placeholder variables for all PrusaSlicer settings.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString("M104 S0 ; turn off temperature\nG28 X0  ; home X axis\nM84     ; disable motors\n"));

    def = this->add("end_filament_gcode", coStrings);
    def->label = L("End G-code");
    def->tooltip = L("This end procedure is inserted at the end of the output file, before the printer end gcode (and "
                   "before any toolchange from this filament in case of multimaterial printers). "
                   "Note that you can use placeholder variables for all PrusaSlicer settings. "
                   "If you have multiple extruders, the gcode is processed in extruder order.");
    def->multiline = true;
    def->full_width = true;
    def->height = 120;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionStrings { "; Filament-specific end gcode \n;END gcode for filament\n" });

    def = this->add("ensure_vertical_shell_thickness", coBool);
    def->label = L("Ensure vertical shell thickness");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Add solid infill near sloping surfaces to guarantee the vertical shell thickness "
                   "(top+bottom solid layers).");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    auto def_top_fill_pattern = def = this->add("top_fill_pattern", coEnum);
    def->label = L("Top fill pattern");
    def->category = L("Infill");
    def->tooltip = L("Fill pattern for top infill. This only affects the top visible layer, and not its adjacent solid shells.");
    def->cli = "top-fill-pattern|external-fill-pattern|solid-fill-pattern";
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("monotonic");
    def->enum_values.push_back("alignedrectilinear");
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("hilbertcurve");
    def->enum_values.push_back("archimedeanchords");
    def->enum_values.push_back("octagramspiral");
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Monotonic"));
    def->enum_labels.push_back(L("Aligned Rectilinear"));
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Hilbert Curve"));
    def->enum_labels.push_back(L("Archimedean Chords"));
    def->enum_labels.push_back(L("Octagram Spiral"));
    // solid_fill_pattern is an obsolete equivalent to top_fill_pattern/bottom_fill_pattern.
    def->aliases = { "solid_fill_pattern", "external_fill_pattern" };
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipMonotonic));

    def = this->add("bottom_fill_pattern", coEnum);
    def->label = L("Bottom fill pattern");
    def->category = L("Infill");
    def->tooltip = L("Fill pattern for bottom infill. This only affects the bottom external visible layer, and not its adjacent solid shells.");
    def->cli = "bottom-fill-pattern|external-fill-pattern|solid-fill-pattern";
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values = def_top_fill_pattern->enum_values;
    def->enum_labels = def_top_fill_pattern->enum_labels;
    def->aliases = def_top_fill_pattern->aliases;
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipMonotonic));

    def = this->add("external_perimeter_extrusion_width", coFloatOrPercent);
    def->label = L("External perimeters");
    def->category = L("Extrusion Width");
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for external perimeters. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "If expressed as percentage (for example 200%), it will be computed over layer height.");
    def->sidetext = L("mm or %");
    def->min = 0;
    def->max_literal = 50;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("external_perimeter_speed", coFloatOrPercent);
    def->label = L("External perimeters");
    def->category = L("Speed");
    def->tooltip = L("This separate setting will affect the speed of external perimeters (the visible ones). "
                   "If expressed as percentage (for example: 80%) it will be calculated "
                   "on the perimeters speed setting above. Set to zero for auto.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "perimeter_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(50, true));

    def = this->add("external_perimeters_first", coBool);
    def->label = L("External perimeters first");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Print contour perimeters from the outermost one to the innermost one "
                   "instead of the default inverse order.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("extra_perimeters", coBool);
    def->label = L("Extra perimeters if needed");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Add more perimeters when needed for avoiding gaps in sloping walls. "
                   "Slic3r keeps adding perimeters, until more than 70% of the loop immediately above "
                   "is supported.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("extruder", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Extruder");
    def->category = L("Extruders");
    def->tooltip = L("The extruder to use (unless more specific extruder settings are specified). "
                   "This value overrides perimeter and infill extruders, but not the support extruders.");
    def->min = 0;  // 0 = inherit defaults
    def->enum_labels.push_back(L("default"));  // override label for item 0
    def->enum_labels.push_back("1");
    def->enum_labels.push_back("2");
    def->enum_labels.push_back("3");
    def->enum_labels.push_back("4");
    def->enum_labels.push_back("5");

    def = this->add("extruder_clearance_height", coFloat);
    def->label = L("Height");
    def->tooltip = L("Set this to the vertical distance between your nozzle tip and (usually) the X carriage rods. "
                   "In other words, this is the height of the clearance cylinder around your extruder, "
                   "and it represents the maximum depth the extruder can peek before colliding with "
                   "other printed objects.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(20));

    def = this->add("extruder_clearance_radius", coFloat);
    def->label = L("Radius");
    def->tooltip = L("Set this to the clearance radius around your extruder. "
                   "If the extruder is not centered, choose the largest value for safety. "
                   "This setting is used to check for collisions and to display the graphical preview "
                   "in the plater.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(20));

    def = this->add("extruder_colour", coStrings);
    def->label = L("Extruder Color");
    def->tooltip = L("This is only used in the Slic3r interface as a visual help.");
    def->gui_type = ConfigOptionDef::GUIType::color;
    // Empty string means no color assigned yet.
    def->set_default_value(new ConfigOptionStrings { "" });

    def = this->add("extruder_offset", coPoints);
    def->label = L("Extruder offset");
    def->tooltip = L("If your firmware doesn't handle the extruder displacement you need the G-code "
                   "to take it into account. This option lets you specify the displacement of each extruder "
                   "with respect to the first one. It expects positive coordinates (they will be subtracted "
                   "from the XY coordinate).");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPoints { Vec2d(0,0) });

    def = this->add("extrusion_axis", coString);
    def->label = L("Extrusion axis");
    def->tooltip = L("Use this option to set the axis letter associated to your printer's extruder "
                   "(usually E but some printers use A).");
    def->set_default_value(new ConfigOptionString("E"));

    def = this->add("extrusion_multiplier", coFloats);
    def->label = L("Extrusion multiplier");
    def->tooltip = L("This factor changes the amount of flow proportionally. You may need to tweak "
                   "this setting to get nice surface finish and correct single wall widths. "
                   "Usual values are between 0.9 and 1.1. If you think you need to change this more, "
                   "check filament diameter and your firmware E steps.");
    def->max = 2;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 1. });

    def = this->add("extrusion_width", coFloatOrPercent);
    def->label = L("Default extrusion width");
    def->category = L("Extrusion Width");
    def->tooltip = L("Set this to a non-zero value to allow a manual extrusion width. "
                   "If left to zero, Slic3r derives extrusion widths from the nozzle diameter "
                   "(see the tooltips for perimeter extrusion width, infill extrusion width etc). "
                   "If expressed as percentage (for example: 230%), it will be computed over layer height.");
    def->sidetext = L("mm or %");
    def->min = 0;
    def->max = 1000;
    def->max_literal = 50;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("fan_always_on", coBools);
    def->label = L("Keep fan always on");
    def->tooltip = L("If this is enabled, fan will never be disabled and will be kept running at least "
                   "at its minimum speed. Useful for PLA, harmful for ABS.");
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("fan_below_layer_time", coInts);
    def->label = L("Enable fan if layer print time is below");
    def->tooltip = L("If layer print time is estimated below this number of seconds, fan will be enabled "
                   "and its speed will be calculated by interpolating the minimum and maximum speeds.");
    def->sidetext = L("approximate seconds");
    def->min = 0;
    def->max = 1000;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts { 60 });

    def = this->add("filament_colour", coStrings);
    def->label = L("Color");
    def->tooltip = L("This is only used in the Slic3r interface as a visual help.");
    def->gui_type = ConfigOptionDef::GUIType::color;
    def->set_default_value(new ConfigOptionStrings { "#29B2B2" });

    def = this->add("filament_notes", coStrings);
    def->label = L("Filament notes");
    def->tooltip = L("You can put your notes regarding the filament here.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings { "" });

    def = this->add("filament_max_volumetric_speed", coFloats);
    def->label = L("Max volumetric speed");
    def->tooltip = L("Maximum volumetric speed allowed for this filament. Limits the maximum volumetric "
                   "speed of a print to the minimum of print and filament volumetric speed. "
                   "Set to zero for no limit.");
    def->sidetext = L("mm³/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_loading_speed", coFloats);
    def->label = L("Loading speed");
    def->tooltip = L("Speed used for loading the filament on the wipe tower.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 28. });

    def = this->add("filament_loading_speed_start", coFloats);
    def->label = L("Loading speed at the start");
    def->tooltip = L("Speed used at the very beginning of loading phase.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 3. });

    def = this->add("filament_unloading_speed", coFloats);
    def->label = L("Unloading speed");
    def->tooltip = L("Speed used for unloading the filament on the wipe tower (does not affect "
                      " initial part of unloading just after ramming).");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 90. });

    def = this->add("filament_unloading_speed_start", coFloats);
    def->label = L("Unloading speed at the start");
    def->tooltip = L("Speed used for unloading the tip of the filament immediately after ramming.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 100. });

    def = this->add("filament_toolchange_delay", coFloats);
    def->label = L("Delay after unloading");
    def->tooltip = L("Time to wait after the filament is unloaded. "
                   "May help to get reliable toolchanges with flexible materials "
                   "that may need more time to shrink to original dimensions.");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_cooling_moves", coInts);
    def->label = L("Number of cooling moves");
    def->tooltip = L("Filament is cooled by being moved back and forth in the "
                   "cooling tubes. Specify desired number of these moves.");
    def->max = 0;
    def->max = 20;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts { 4 });

    def = this->add("filament_cooling_initial_speed", coFloats);
    def->label = L("Speed of the first cooling move");
    def->tooltip = L("Cooling moves are gradually accelerating beginning at this speed.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 2.2f });

    def = this->add("filament_minimal_purge_on_wipe_tower", coFloats);
    def->label = L("Minimal purge on wipe tower");
    def->tooltip = L("After a tool change, the exact position of the newly loaded filament inside "
                     "the nozzle may not be known, and the filament pressure is likely not yet stable. "
                     "Before purging the print head into an infill or a sacrificial object, Slic3r will always prime "
                     "this amount of material into the wipe tower to produce successive infill or sacrificial object extrusions reliably.");
    def->sidetext = L("mm³");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 15.f });

    def = this->add("filament_cooling_final_speed", coFloats);
    def->label = L("Speed of the last cooling move");
    def->tooltip = L("Cooling moves are gradually accelerating towards this speed.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 3.4f });

    def = this->add("filament_load_time", coFloats);
    def->label = L("Filament load time");
    def->tooltip = L("Time for the printer firmware (or the Multi Material Unit 2.0) to load a new filament during a tool change (when executing the T code). This time is added to the total print time by the G-code time estimator.");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 0.0f });

    def = this->add("filament_ramming_parameters", coStrings);
    def->label = L("Ramming parameters");
    def->tooltip = L("This string is edited by RammingDialog and contains ramming specific parameters.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionStrings { "120 100 6.6 6.8 7.2 7.6 7.9 8.2 8.7 9.4 9.9 10.0|"
       " 0.05 6.6 0.45 6.8 0.95 7.8 1.45 8.3 1.95 9.7 2.45 10 2.95 7.6 3.45 7.6 3.95 7.6 4.45 7.6 4.95 7.6" });

    def = this->add("filament_unload_time", coFloats);
    def->label = L("Filament unload time");
    def->tooltip = L("Time for the printer firmware (or the Multi Material Unit 2.0) to unload a filament during a tool change (when executing the T code). This time is added to the total print time by the G-code time estimator.");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 0.0f });

    def = this->add("filament_diameter", coFloats);
    def->label = L("Diameter");
    def->tooltip = L("Enter your filament diameter here. Good precision is required, so use a caliper "
                   "and do multiple measurements along the filament, then compute the average.");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloats { 1.75 });

    def = this->add("filament_density", coFloats);
    def->label = L("Density");
    def->tooltip = L("Enter your filament density here. This is only for statistical information. "
                   "A decent way is to weigh a known length of filament and compute the ratio "
                   "of the length to volume. Better is to calculate the volume directly through displacement.");
    def->sidetext = L("g/cm³");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_type", coStrings);
    def->label = L("Filament type");
    def->tooltip = L("The filament material type for use in custom G-codes.");
    def->gui_type = ConfigOptionDef::GUIType::f_enum_open;
    def->gui_flags = "show_value";
    def->enum_values.push_back("PLA");
    def->enum_values.push_back("PET");
    def->enum_values.push_back("ABS");
    def->enum_values.push_back("ASA");
    def->enum_values.push_back("FLEX");
    def->enum_values.push_back("HIPS");
    def->enum_values.push_back("EDGE");
    def->enum_values.push_back("NGEN");
    def->enum_values.push_back("NYLON");
    def->enum_values.push_back("PVA");
    def->enum_values.push_back("PC");
    def->enum_values.push_back("PP");
    def->enum_values.push_back("PEI");
    def->enum_values.push_back("PEEK");
    def->enum_values.push_back("PEKK");
    def->enum_values.push_back("POM");
    def->enum_values.push_back("PSU");
    def->enum_values.push_back("PVDF");
    def->enum_values.push_back("SCAFF");

    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings { "PLA" });

    def = this->add("filament_soluble", coBools);
    def->label = L("Soluble material");
    def->tooltip = L("Soluble material is most likely used for a soluble support.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("filament_cost", coFloats);
    def->label = L("Cost");
    def->tooltip = L("Enter your filament cost per kg here. This is only for statistical information.");
    def->sidetext = L("money/kg");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_spool_weight", coFloats);
    def->label = L("Spool weight");
    def->tooltip = L("Enter weight of the empty filament spool. "
                     "One may weigh a partially consumed filament spool before printing and one may compare the measured weight "
                     "with the calculated weight of the filament with the spool to find out whether the amount "
                     "of filament on the spool is sufficient to finish the print.");
    def->sidetext = L("g");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("filament_settings_id", coStrings);
    def->set_default_value(new ConfigOptionStrings { "" });
    def->cli = ConfigOptionDef::nocli;

    def = this->add("filament_vendor", coString);
    def->set_default_value(new ConfigOptionString(L("(Unknown)")));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("fill_angle", coFloat);
    def->label = L("Fill angle");
    def->category = L("Infill");
    def->tooltip = L("Default base angle for infill orientation. Cross-hatching will be applied to this. "
                   "Bridges will be infilled using the best direction Slic3r can detect, so this setting "
                   "does not affect them.");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 360;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(45));

    def = this->add("fill_density", coPercent);
    def->gui_type = ConfigOptionDef::GUIType::f_enum_open;
    def->gui_flags = "show_value";
    def->label = L("Fill density");
    def->category = L("Infill");
    def->tooltip = L("Density of internal infill, expressed in the range 0% - 100%.");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->enum_values.push_back("0");
    def->enum_values.push_back("5");
    def->enum_values.push_back("10");
    def->enum_values.push_back("15");
    def->enum_values.push_back("20");
    def->enum_values.push_back("25");
    def->enum_values.push_back("30");
    def->enum_values.push_back("40");
    def->enum_values.push_back("50");
    def->enum_values.push_back("60");
    def->enum_values.push_back("70");
    def->enum_values.push_back("80");
    def->enum_values.push_back("90");
    def->enum_values.push_back("100");
    def->enum_labels.push_back("0%");
    def->enum_labels.push_back("5%");
    def->enum_labels.push_back("10%");
    def->enum_labels.push_back("15%");
    def->enum_labels.push_back("20%");
    def->enum_labels.push_back("25%");
    def->enum_labels.push_back("30%");
    def->enum_labels.push_back("40%");
    def->enum_labels.push_back("50%");
    def->enum_labels.push_back("60%");
    def->enum_labels.push_back("70%");
    def->enum_labels.push_back("80%");
    def->enum_labels.push_back("90%");
    def->enum_labels.push_back("100%");
    def->set_default_value(new ConfigOptionPercent(20));

    def = this->add("fill_pattern", coEnum);
    def->label = L("Fill pattern");
    def->category = L("Infill");
    def->tooltip = L("Fill pattern for general low-density infill.");
    def->enum_keys_map = &ConfigOptionEnum<InfillPattern>::get_enum_values();
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("alignedrectilinear");
    def->enum_values.push_back("grid");
    def->enum_values.push_back("triangles");
    def->enum_values.push_back("stars");
    def->enum_values.push_back("cubic");
    def->enum_values.push_back("line");
    def->enum_values.push_back("concentric");
    def->enum_values.push_back("honeycomb");
    def->enum_values.push_back("3dhoneycomb");
    def->enum_values.push_back("gyroid");
    def->enum_values.push_back("hilbertcurve");
    def->enum_values.push_back("archimedeanchords");
    def->enum_values.push_back("octagramspiral");
    def->enum_values.push_back("adaptivecubic");
    def->enum_values.push_back("supportcubic");
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Aligned Rectilinear"));
    def->enum_labels.push_back(L("Grid"));
    def->enum_labels.push_back(L("Triangles"));
    def->enum_labels.push_back(L("Stars"));
    def->enum_labels.push_back(L("Cubic"));
    def->enum_labels.push_back(L("Line"));
    def->enum_labels.push_back(L("Concentric"));
    def->enum_labels.push_back(L("Honeycomb"));
    def->enum_labels.push_back(L("3D Honeycomb"));
    def->enum_labels.push_back(L("Gyroid"));
    def->enum_labels.push_back(L("Hilbert Curve"));
    def->enum_labels.push_back(L("Archimedean Chords"));
    def->enum_labels.push_back(L("Octagram Spiral"));
    def->enum_labels.push_back(L("Adaptive Cubic"));
    def->enum_labels.push_back(L("Support Cubic"));
    def->set_default_value(new ConfigOptionEnum<InfillPattern>(ipStars));

    def = this->add("first_layer_acceleration", coFloat);
    def->label = L("First layer");
    def->tooltip = L("This is the acceleration your printer will use for first layer. Set zero "
                   "to disable acceleration control for first layer.");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("first_layer_acceleration_over_raft", coFloat);
    def->label = L("First object layer over raft interface");
    def->tooltip = L("This is the acceleration your printer will use for first layer of object above raft interface. Set zero "
                   "to disable acceleration control for first layer of object above raft interface.");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("first_layer_bed_temperature", coInts);
    def->label = L("First layer");
    def->full_label = L("First layer bed temperature");
    def->tooltip = L("Heated build plate temperature for the first layer. Set this to zero to disable "
                   "bed temperature control commands in the output.");
    def->sidetext = L("°C");
    def->max = 0;
    def->max = 300;
    def->set_default_value(new ConfigOptionInts { 0 });

    def = this->add("first_layer_extrusion_width", coFloatOrPercent);
    def->label = L("First layer");
    def->category = L("Extrusion Width");
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for first layer. "
                   "You can use this to force fatter extrudates for better adhesion. If expressed "
                   "as percentage (for example 120%) it will be computed over first layer height. "
                   "If set to zero, it will use the default extrusion width.");
    def->sidetext = L("mm or %");
    def->ratio_over = "first_layer_height";
    def->min = 0;
    def->max_literal = 50;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(200, true));

    def = this->add("first_layer_height", coFloatOrPercent);
    def->label = L("First layer height");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("When printing with very low layer heights, you might still want to print a thicker "
                   "bottom layer to improve adhesion and tolerance for non perfect build plates.");
    def->sidetext = L("mm");
    def->ratio_over = "layer_height";
    def->set_default_value(new ConfigOptionFloatOrPercent(0.35, false));

    def = this->add("first_layer_speed", coFloatOrPercent);
    def->label = L("First layer speed");
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to all the print moves "
                   "of the first layer, regardless of their type. If expressed as a percentage "
                   "(for example: 40%) it will scale the default speeds.");
    def->sidetext = L("mm/s or %");
    def->min = 0;
    def->max_literal = 20;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(30, false));

    def = this->add("first_layer_speed_over_raft", coFloatOrPercent);
    def->label = L("Speed of object first layer over raft interface");
    def->tooltip = L("If expressed as absolute value in mm/s, this speed will be applied to all the print moves "
                   "of the first object layer above raft interface, regardless of their type. If expressed as a percentage "
                   "(for example: 40%) it will scale the default speeds.");
    def->sidetext = L("mm/s or %");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(30, false));

    def = this->add("first_layer_temperature", coInts);
    def->label = L("First layer");
    def->full_label = L("First layer nozzle temperature");
    def->tooltip = L("Nozzle temperature for the first layer. If you want to control temperature manually "
                     "during print, set this to zero to disable temperature control commands in the output G-code.");
    def->sidetext = L("°C");
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts { 200 });

    def = this->add("full_fan_speed_layer", coInts);
    def->label = L("Full fan speed at layer");
    def->tooltip = L("Fan speed will be ramped up linearly from zero at layer \"disable_fan_first_layers\" "
                   "to maximum at layer \"full_fan_speed_layer\". "
                   "\"full_fan_speed_layer\" will be ignored if lower than \"disable_fan_first_layers\", in which case "
                   "the fan will be running at maximum allowed speed at layer \"disable_fan_first_layers\" + 1.");
    def->min = 0;
    def->max = 1000;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts { 0 });

    def = this->add("fuzzy_skin", coEnum);
    def->label = L("Fuzzy Skin");
    def->category = L("Fuzzy Skin");
    def->tooltip = L("Fuzzy skin type.");

    def->enum_keys_map = &ConfigOptionEnum<FuzzySkinType>::get_enum_values();
    def->enum_values.push_back("none");
    def->enum_values.push_back("external");
    def->enum_values.push_back("all");
    def->enum_labels.push_back(L("None"));
    def->enum_labels.push_back(L("Outside walls"));
    def->enum_labels.push_back(L("All walls"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<FuzzySkinType>(FuzzySkinType::None));

    def = this->add("fuzzy_skin_thickness", coFloat);
    def->label = L("Fuzzy skin thickness");
    def->category = L("Fuzzy Skin");
    def->tooltip = L("The maximum distance that each skin point can be offset (both ways), "
                     "measured perpendicular to the perimeter wall.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.3));

    def = this->add("fuzzy_skin_point_dist", coFloat);
    def->label = L("Fuzzy skin point distance");
    def->category = L("Fuzzy Skin");
    def->tooltip = L("Perimeters will be split into multiple segments by inserting Fuzzy skin points. "
                     "Lowering the Fuzzy skin point distance will increase the number of randomly offset points on the perimeter wall.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.8));

    def = this->add("gap_fill_enabled", coBool);
    def->label = L("Fill gaps");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Enables filling of gaps between perimeters and between the inner most perimeters and infill.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("gap_fill_speed", coFloat);
    def->label = L("Gap fill");
    def->category = L("Speed");
    def->tooltip = L("Speed for filling small gaps using short zigzag moves. Keep this reasonably low "
                   "to avoid too much shaking and resonance issues. Set zero to disable gaps filling.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(20));

    def = this->add("gcode_comments", coBool);
    def->label = L("Verbose G-code");
    def->tooltip = L("Enable this to get a commented G-code file, with each line explained by a descriptive text. "
                   "If you print from SD card, the additional weight of the file could make your firmware "
                   "slow down.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(0));

    def = this->add("gcode_flavor", coEnum);
    def->label = L("G-code flavor");
    def->tooltip = L("Some G/M-code commands, including temperature control and others, are not universal. "
                   "Set this option to your printer's firmware to get a compatible output. "
                   "The \"No extrusion\" flavor prevents PrusaSlicer from exporting any extrusion value at all.");
    def->enum_keys_map = &ConfigOptionEnum<GCodeFlavor>::get_enum_values();
    def->enum_values.push_back("reprap");
    def->enum_values.push_back("reprapfirmware");
    def->enum_values.push_back("repetier");
    def->enum_values.push_back("teacup");
    def->enum_values.push_back("makerware");
    def->enum_values.push_back("marlin");
    def->enum_values.push_back("marlin2");
    def->enum_values.push_back("sailfish");
    def->enum_values.push_back("mach3");
    def->enum_values.push_back("machinekit");
    def->enum_values.push_back("smoothie");
    def->enum_values.push_back("no-extrusion");
    def->enum_labels.push_back("RepRap/Sprinter");
    def->enum_labels.push_back("RepRapFirmware");
    def->enum_labels.push_back("Repetier");
    def->enum_labels.push_back("Teacup");
    def->enum_labels.push_back("MakerWare (MakerBot)");
    def->enum_labels.push_back("Marlin (legacy)");
    def->enum_labels.push_back("Marlin 2");
    def->enum_labels.push_back("Sailfish (MakerBot)");
    def->enum_labels.push_back("Mach3/LinuxCNC");
    def->enum_labels.push_back("Machinekit");
    def->enum_labels.push_back("Smoothie");
    def->enum_labels.push_back(L("No extrusion"));
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionEnum<GCodeFlavor>(gcfRepRapSprinter));

    def = this->add("gcode_label_objects", coBool);
    def->label = L("Label objects");
    def->tooltip = L("Enable this to add comments into the G-Code labeling print moves with what object they belong to,"
                   " which is useful for the Octoprint CancelObject plugin. This settings is NOT compatible with "
                   "Single Extruder Multi Material setup and Wipe into Object / Wipe into Infill.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(0));

    def = this->add("high_current_on_filament_swap", coBool);
    def->label = L("High extruder current on filament swap");
    def->tooltip = L("It may be beneficial to increase the extruder motor current during the filament exchange"
                   " sequence to allow for rapid ramming feed rates and to overcome resistance when loading"
                   " a filament with an ugly shaped tip.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(0));

    def = this->add("infill_acceleration", coFloat);
    def->label = L("Infill");
    def->tooltip = L("This is the acceleration your printer will use for infill. Set zero to disable "
                   "acceleration control for infill.");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("infill_every_layers", coInt);
    def->label = L("Combine infill every");
    def->category = L("Infill");
    def->tooltip = L("This feature allows to combine infill and speed up your print by extruding thicker "
                   "infill layers while preserving thin perimeters, thus accuracy.");
    def->sidetext = L("layers");
    def->full_label = L("Combine infill every n layers");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    auto def_infill_anchor_min = def = this->add("infill_anchor", coFloatOrPercent);
    def->label = L("Length of the infill anchor");
    def->category = L("Advanced");
    def->tooltip = L("Connect an infill line to an internal perimeter with a short segment of an additional perimeter. "
                     "If expressed as percentage (example: 15%) it is calculated over infill extrusion width. "
                     "PrusaSlicer tries to connect two close infill lines to a short perimeter segment. If no such perimeter segment "
                     "shorter than infill_anchor_max is found, the infill line is connected to a perimeter segment at just one side "
                     "and the length of the perimeter segment taken is limited to this parameter, but no longer than anchor_length_max. "
                     "Set this parameter to zero to disable anchoring perimeters connected to a single infill line.");
    def->sidetext = L("mm or %");
    def->ratio_over = "infill_extrusion_width";
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
    def->set_default_value(new ConfigOptionFloatOrPercent(600, true));

    def = this->add("infill_anchor_max", coFloatOrPercent);
    def->label = L("Maximum length of the infill anchor");
    def->category    = def_infill_anchor_min->category;
    def->tooltip = L("Connect an infill line to an internal perimeter with a short segment of an additional perimeter. "
                     "If expressed as percentage (example: 15%) it is calculated over infill extrusion width. "
                     "PrusaSlicer tries to connect two close infill lines to a short perimeter segment. If no such perimeter segment "
                     "shorter than this parameter is found, the infill line is connected to a perimeter segment at just one side "
                     "and the length of the perimeter segment taken is limited to infill_anchor, but no longer than this parameter. "
                     "Set this parameter to zero to disable anchoring.");
    def->sidetext    = def_infill_anchor_min->sidetext;
    def->ratio_over  = def_infill_anchor_min->ratio_over;
    def->max_literal = def_infill_anchor_min->max_literal;
    def->gui_type    = def_infill_anchor_min->gui_type;
    def->enum_values = def_infill_anchor_min->enum_values;
    def->enum_labels.push_back(L("0 (not anchored)"));
    def->enum_labels.push_back("1 mm");
    def->enum_labels.push_back("2 mm");
    def->enum_labels.push_back("5 mm");
    def->enum_labels.push_back("10 mm");
    def->enum_labels.push_back(L("1000 (unlimited)"));
    def->mode        = def_infill_anchor_min->mode;
    def->set_default_value(new ConfigOptionFloatOrPercent(50, false));

    def = this->add("infill_extruder", coInt);
    def->label = L("Infill extruder");
    def->category = L("Extruders");
    def->tooltip = L("The extruder to use when printing infill.");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("infill_extrusion_width", coFloatOrPercent);
    def->label = L("Infill");
    def->category = L("Extrusion Width");
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for infill. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "You may want to use fatter extrudates to speed up the infill and make your parts stronger. "
                   "If expressed as percentage (for example 90%) it will be computed over layer height.");
    def->sidetext = L("mm or %");
    def->min = 0;
    def->max_literal = 50;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("infill_first", coBool);
    def->label = L("Infill before perimeters");
    def->tooltip = L("This option will switch the print order of perimeters and infill, making the latter first.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("infill_only_where_needed", coBool);
    def->label = L("Only infill where needed");
    def->category = L("Infill");
    def->tooltip = L("This option will limit infill to the areas actually needed for supporting ceilings "
                   "(it will act as internal support material). If enabled, slows down the G-code generation "
                   "due to the multiple checks involved.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("infill_overlap", coFloatOrPercent);
    def->label = L("Infill/perimeters overlap");
    def->category = L("Advanced");
    def->tooltip = L("This setting applies an additional overlap between infill and perimeters for better bonding. "
                   "Theoretically this shouldn't be needed, but backlash might cause gaps. If expressed "
                   "as percentage (example: 15%) it is calculated over perimeter extrusion width.");
    def->sidetext = L("mm or %");
    def->ratio_over = "perimeter_extrusion_width";
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloatOrPercent(25, true));

    def = this->add("infill_speed", coFloat);
    def->label = L("Infill");
    def->category = L("Speed");
    def->tooltip = L("Speed for printing the internal fill. Set to zero for auto.");
    def->sidetext = L("mm/s");
    def->aliases = { "print_feed_rate", "infill_feed_rate" };
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(80));

    def = this->add("inherits", coString);
    def->label = L("Inherits profile");
    def->tooltip = L("Name of the profile, from which this profile inherits.");
    def->full_width = true;
    def->height = 5;
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    // The following value is to be stored into the project file (AMF, 3MF, Config ...)
    // and it contains a sum of "inherits" values over the print and filament profiles.
    def = this->add("inherits_cummulative", coStrings);
    def->set_default_value(new ConfigOptionStrings());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("interface_shells", coBool);
    def->label = L("Interface shells");
    def->tooltip = L("Force the generation of solid shells between adjacent materials/volumes. "
                   "Useful for multi-extruder prints with translucent materials or manual soluble "
                   "support material.");
    def->category = L("Layers and Perimeters");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("mmu_segmented_region_max_width", coFloat);
    def->label = L("Maximum width of a segmented region");
    def->tooltip = L("Maximum width of a segmented region. Zero disables this feature.");
    def->sidetext = L("mm (zero to disable)");
    def->min = 0;
    def->category = L("Advanced");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.f));

    def = this->add("ironing", coBool);
    def->label = L("Enable ironing");
    def->tooltip = L("Enable ironing of the top layers with the hot print head for smooth surface");
    def->category = L("Ironing");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("ironing_type", coEnum);
    def->label = L("Ironing Type");
    def->category = L("Ironing");
    def->tooltip = L("Ironing Type");
    def->enum_keys_map = &ConfigOptionEnum<IroningType>::get_enum_values();
    def->enum_values.push_back("top");
    def->enum_values.push_back("topmost");
    def->enum_values.push_back("solid");
    def->enum_labels.push_back(L("All top surfaces"));
    def->enum_labels.push_back(L("Topmost surface only"));
    def->enum_labels.push_back(L("All solid surfaces"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<IroningType>(IroningType::TopSurfaces));

    def = this->add("ironing_flowrate", coPercent);
    def->label = L("Flow rate");
    def->category = L("Ironing");
    def->tooltip = L("Percent of a flow rate relative to object's normal layer height.");
    def->sidetext = L("%");
    def->ratio_over = "layer_height";
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercent(15));

    def = this->add("ironing_spacing", coFloat);
    def->label = L("Spacing between ironing passes");
    def->category = L("Ironing");
    def->tooltip = L("Distance between ironing lines");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.1));

    def = this->add("ironing_speed", coFloat);
    def->label = L("Ironing");
    def->category = L("Speed");
    def->tooltip = L("Ironing");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(15));

    def = this->add("layer_gcode", coString);
    def->label = L("After layer change G-code");
    def->tooltip = L("This custom code is inserted at every layer change, right after the Z move "
                   "and before the extruder moves to the first layer point. Note that you can use "
                   "placeholder variables for all Slic3r settings as well as [layer_num] and [layer_z].");
    def->cli = "after-layer-gcode|layer-gcode";
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("remaining_times", coBool);
    def->label = L("Supports remaining times");
    def->tooltip = L("Emit M73 P[percent printed] R[remaining time in minutes] at 1 minute"
                     " intervals into the G-code to let the firmware show accurate remaining time."
                     " As of now only the Prusa i3 MK3 firmware recognizes M73."
                     " Also the i3 MK3 firmware supports M73 Qxx Sxx for the silent mode.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("silent_mode", coBool);
    def->label = L("Supports stealth mode");
    def->tooltip = L("The firmware supports stealth mode");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("machine_limits_usage", coEnum);
    def->label = L("How to apply limits");
    def->full_label = L("Purpose of Machine Limits");
    def->category = L("Machine limits");
    def->tooltip = L("How to apply the Machine Limits");
    def->enum_keys_map = &ConfigOptionEnum<MachineLimitsUsage>::get_enum_values();
    def->enum_values.push_back("emit_to_gcode");
    def->enum_values.push_back("time_estimate_only");
    def->enum_values.push_back("ignore");
    def->enum_labels.push_back(L("Emit to G-code"));
    def->enum_labels.push_back(L("Use for time estimate"));
    def->enum_labels.push_back(L("Ignore"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<MachineLimitsUsage>(MachineLimitsUsage::EmitToGCode));

    {
        struct AxisDefault {
            std::string         name;
            std::vector<double> max_feedrate;
            std::vector<double> max_acceleration;
            std::vector<double> max_jerk;
        };
        std::vector<AxisDefault> axes {
            // name, max_feedrate,  max_acceleration, max_jerk
            { "x", { 500., 200. }, {  9000., 1000. }, { 10. , 10.  } },
            { "y", { 500., 200. }, {  9000., 1000. }, { 10. , 10.  } },
            { "z", {  12.,  12. }, {   500.,  200. }, {  0.2,  0.4 } },
            { "e", { 120., 120. }, { 10000., 5000. }, {  2.5,  2.5 } }
        };
        for (const AxisDefault &axis : axes) {
            std::string axis_upper = boost::to_upper_copy<std::string>(axis.name);
            // Add the machine feedrate limits for XYZE axes. (M203)
            def = this->add("machine_max_feedrate_" + axis.name, coFloats);
            def->full_label = (boost::format("Maximum feedrate %1%") % axis_upper).str();
            (void)L("Maximum feedrate X");
            (void)L("Maximum feedrate Y");
            (void)L("Maximum feedrate Z");
            (void)L("Maximum feedrate E");
            def->category = L("Machine limits");
            def->tooltip  = (boost::format("Maximum feedrate of the %1% axis") % axis_upper).str();
            (void)L("Maximum feedrate of the X axis");
            (void)L("Maximum feedrate of the Y axis");
            (void)L("Maximum feedrate of the Z axis");
            (void)L("Maximum feedrate of the E axis");
            def->sidetext = L("mm/s");
            def->min = 0;
            def->mode = comAdvanced;
            def->set_default_value(new ConfigOptionFloats(axis.max_feedrate));
            // Add the machine acceleration limits for XYZE axes (M201)
            def = this->add("machine_max_acceleration_" + axis.name, coFloats);
            def->full_label = (boost::format("Maximum acceleration %1%") % axis_upper).str();
            (void)L("Maximum acceleration X");
            (void)L("Maximum acceleration Y");
            (void)L("Maximum acceleration Z");
            (void)L("Maximum acceleration E");
            def->category = L("Machine limits");
            def->tooltip  = (boost::format("Maximum acceleration of the %1% axis") % axis_upper).str();
            (void)L("Maximum acceleration of the X axis");
            (void)L("Maximum acceleration of the Y axis");
            (void)L("Maximum acceleration of the Z axis");
            (void)L("Maximum acceleration of the E axis");
            def->sidetext = L("mm/s²");
            def->min = 0;
            def->mode = comAdvanced;
            def->set_default_value(new ConfigOptionFloats(axis.max_acceleration));
            // Add the machine jerk limits for XYZE axes (M205)
            def = this->add("machine_max_jerk_" + axis.name, coFloats);
            def->full_label = (boost::format("Maximum jerk %1%") % axis_upper).str();
            (void)L("Maximum jerk X");
            (void)L("Maximum jerk Y");
            (void)L("Maximum jerk Z");
            (void)L("Maximum jerk E");
            def->category = L("Machine limits");
            def->tooltip  = (boost::format("Maximum jerk of the %1% axis") % axis_upper).str();
            (void)L("Maximum jerk of the X axis");
            (void)L("Maximum jerk of the Y axis");
            (void)L("Maximum jerk of the Z axis");
            (void)L("Maximum jerk of the E axis");
            def->sidetext = L("mm/s");
            def->min = 0;
            def->mode = comAdvanced;
            def->set_default_value(new ConfigOptionFloats(axis.max_jerk));
        }
    }

    // M205 S... [mm/sec]
    def = this->add("machine_min_extruding_rate", coFloats);
    def->full_label = L("Minimum feedrate when extruding");
    def->category = L("Machine limits");
    def->tooltip = L("Minimum feedrate when extruding (M205 S)");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 0., 0. });

    // M205 T... [mm/sec]
    def = this->add("machine_min_travel_rate", coFloats);
    def->full_label = L("Minimum travel feedrate");
    def->category = L("Machine limits");
    def->tooltip = L("Minimum travel feedrate (M205 T)");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 0., 0. });

    // M204 P... [mm/sec^2]
    def = this->add("machine_max_acceleration_extruding", coFloats);
    def->full_label = L("Maximum acceleration when extruding");
    def->category = L("Machine limits");
    def->tooltip = L("Maximum acceleration when extruding (M204 P)\n\n"
                     "Marlin (legacy) firmware flavor will use this also "
                     "as travel acceleration (M204 T).");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 1500., 1250. });


    // M204 R... [mm/sec^2]
    def = this->add("machine_max_acceleration_retracting", coFloats);
    def->full_label = L("Maximum acceleration when retracting");
    def->category = L("Machine limits");
    def->tooltip = L("Maximum acceleration when retracting (M204 R)");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 1500., 1250. });

    // M204 T... [mm/sec^2]
    def = this->add("machine_max_acceleration_travel", coFloats);
    def->full_label = L("Maximum acceleration for travel moves");
    def->category = L("Machine limits");
    def->tooltip = L("Maximum acceleration for travel moves (M204 T)");
    def->sidetext = L("mm/s²");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats{ 1500., 1250. });

    def = this->add("max_fan_speed", coInts);
    def->label = L("Max");
    def->tooltip = L("This setting represents the maximum speed of your fan.");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts { 100 });

    def = this->add("max_layer_height", coFloats);
    def->label = L("Max");
    def->tooltip = L("This is the highest printable layer height for this extruder, used to cap "
                   "the variable layer height and support layer height. Maximum recommended layer height "
                   "is 75% of the extrusion width to achieve reasonable inter-layer adhesion. "
                   "If set to 0, layer height is limited to 75% of the nozzle diameter.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("max_print_speed", coFloat);
    def->label = L("Max print speed");
    def->tooltip = L("When setting other speed settings to 0 Slic3r will autocalculate the optimal speed "
                   "in order to keep constant extruder pressure. This experimental setting is used "
                   "to set the highest print speed you want to allow.");
    def->sidetext = L("mm/s");
    def->min = 1;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(80));

    def = this->add("max_volumetric_speed", coFloat);
    def->label = L("Max volumetric speed");
    def->tooltip = L("This experimental setting is used to set the maximum volumetric speed your "
                   "extruder supports.");
    def->sidetext = L("mm³/s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

#ifdef HAS_PRESSURE_EQUALIZER
    def = this->add("max_volumetric_extrusion_rate_slope_positive", coFloat);
    def->label = L("Max volumetric slope positive");
    def->tooltip = L("This experimental setting is used to limit the speed of change in extrusion rate. "
                   "A value of 1.8 mm³/s² ensures, that a change from the extrusion rate "
                   "of 1.8 mm³/s (0.45mm extrusion width, 0.2mm extrusion height, feedrate 20 mm/s) "
                   "to 5.4 mm³/s (feedrate 60 mm/s) will take at least 2 seconds.");
    def->sidetext = L("mm³/s²");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("max_volumetric_extrusion_rate_slope_negative", coFloat);
    def->label = L("Max volumetric slope negative");
    def->tooltip = L("This experimental setting is used to limit the speed of change in extrusion rate. "
                   "A value of 1.8 mm³/s² ensures, that a change from the extrusion rate "
                   "of 1.8 mm³/s (0.45mm extrusion width, 0.2mm extrusion height, feedrate 20 mm/s) "
                   "to 5.4 mm³/s (feedrate 60 mm/s) will take at least 2 seconds.");
    def->sidetext = L("mm³/s²");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));
#endif /* HAS_PRESSURE_EQUALIZER */

    def = this->add("min_fan_speed", coInts);
    def->label = L("Min");
    def->tooltip = L("This setting represents the minimum PWM your fan needs to work.");
    def->sidetext = L("%");
    def->min = 0;
    def->max = 100;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts { 35 });

    def = this->add("min_layer_height", coFloats);
    def->label = L("Min");
    def->tooltip = L("This is the lowest printable layer height for this extruder and limits "
                   "the resolution for variable layer height. Typical values are between 0.05 mm and 0.1 mm.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0.07 });

    def = this->add("min_print_speed", coFloats);
    def->label = L("Min print speed");
    def->tooltip = L("Slic3r will not scale speed down below this speed.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 10. });

    def = this->add("min_skirt_length", coFloat);
    def->label = L("Minimal filament extrusion length");
    def->tooltip = L("Generate no less than the number of skirt loops required to consume "
                   "the specified amount of filament on the bottom layer. For multi-extruder machines, "
                   "this minimum applies to each extruder.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("notes", coString);
    def->label = L("Configuration notes");
    def->tooltip = L("You can put here your personal notes. This text will be added to the G-code "
                   "header comments.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("nozzle_diameter", coFloats);
    def->label = L("Nozzle diameter");
    def->tooltip = L("This is the diameter of your extruder nozzle (for example: 0.5, 0.35 etc.)");
    def->sidetext = L("mm");
    def->set_default_value(new ConfigOptionFloats { 0.4 });

    def = this->add("host_type", coEnum);
    def->label = L("Host Type");
    def->tooltip = L("Slic3r can upload G-code files to a printer host. This field must contain "
                   "the kind of the host.");
    def->enum_keys_map = &ConfigOptionEnum<PrintHostType>::get_enum_values();
    def->enum_values.push_back("prusalink");
    def->enum_values.push_back("octoprint");
    def->enum_values.push_back("duet");
    def->enum_values.push_back("flashair");
    def->enum_values.push_back("astrobox");
    def->enum_values.push_back("repetier");
    def->enum_values.push_back("mks");
    def->enum_labels.push_back("PrusaLink");
    def->enum_labels.push_back("OctoPrint");
    def->enum_labels.push_back("Duet");
    def->enum_labels.push_back("FlashAir");
    def->enum_labels.push_back("AstroBox");
    def->enum_labels.push_back("Repetier");
    def->enum_labels.push_back("MKS");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionEnum<PrintHostType>(htOctoPrint));

    def = this->add("only_retract_when_crossing_perimeters", coBool);
    def->label = L("Only retract when crossing perimeters");
    def->tooltip = L("Disables retraction when the travel path does not exceed the upper layer's perimeters "
                   "(and thus any ooze will be probably invisible).");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("ooze_prevention", coBool);
    def->label = L("Enable");
    def->tooltip = L("This option will drop the temperature of the inactive extruders to prevent oozing. "
                   "It will enable a tall skirt automatically and move extruders outside such "
                   "skirt when changing temperatures.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("output_filename_format", coString);
    def->label = L("Output filename format");
    def->tooltip = L("You can use all configuration options as variables inside this template. "
                   "For example: [layer_height], [fill_density] etc. You can also use [timestamp], "
                   "[year], [month], [day], [hour], [minute], [second], [version], [input_filename], "
                   "[input_filename_base].");
    def->full_width = true;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString("[input_filename_base].gcode"));

    def = this->add("overhangs", coBool);
    def->label = L("Detect bridging perimeters");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Experimental option to adjust flow for overhangs (bridge flow will be used), "
                   "to apply bridge speed to them and enable fan.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("parking_pos_retraction", coFloat);
    def->label = L("Filament parking position");
    def->tooltip = L("Distance of the extruder tip from the position where the filament is parked "
                      "when unloaded. This should match the value in printer firmware.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(92.f));

    def = this->add("extra_loading_move", coFloat);
    def->label = L("Extra loading distance");
    def->tooltip = L("When set to zero, the distance the filament is moved from parking position during load "
                      "is exactly the same as it was moved back during unload. When positive, it is loaded further, "
                      " if negative, the loading move is shorter than unloading.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(-2.f));

    def = this->add("perimeter_acceleration", coFloat);
    def->label = L("Perimeters");
    def->tooltip = L("This is the acceleration your printer will use for perimeters. "
                     "Set zero to disable acceleration control for perimeters.");
    def->sidetext = L("mm/s²");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("perimeter_extruder", coInt);
    def->label = L("Perimeter extruder");
    def->category = L("Extruders");
    def->tooltip = L("The extruder to use when printing perimeters and brim. First extruder is 1.");
    def->aliases = { "perimeters_extruder" };
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("perimeter_extrusion_width", coFloatOrPercent);
    def->label = L("Perimeters");
    def->category = L("Extrusion Width");
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for perimeters. "
                   "You may want to use thinner extrudates to get more accurate surfaces. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "If expressed as percentage (for example 200%) it will be computed over layer height.");
    def->sidetext = L("mm or %");
    def->aliases = { "perimeters_extrusion_width" };
    def->min = 0;
    def->max_literal = 50;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("perimeter_speed", coFloat);
    def->label = L("Perimeters");
    def->category = L("Speed");
    def->tooltip = L("Speed for perimeters (contours, aka vertical shells). Set to zero for auto.");
    def->sidetext = L("mm/s");
    def->aliases = { "perimeter_feed_rate" };
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(60));

    def = this->add("perimeters", coInt);
    def->label = L("Perimeters");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("This option sets the number of perimeters to generate for each layer. "
                   "Note that Slic3r may increase this number automatically when it detects "
                   "sloping surfaces which benefit from a higher number of perimeters "
                   "if the Extra Perimeters option is enabled.");
    def->sidetext = L("(minimum)");
    def->aliases = { "perimeter_offsets" };
    def->min = 0;
    def->max = 10000;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("post_process", coStrings);
    def->label = L("Post-processing scripts");
    def->tooltip = L("If you want to process the output G-code through custom scripts, "
                   "just list their absolute paths here. Separate multiple scripts with a semicolon. "
                   "Scripts will be passed the absolute path to the G-code file as the first argument, "
                   "and they can access the Slic3r config settings by reading environment variables.");
    def->gui_flags = "serialized";
    def->multiline = true;
    def->full_width = true;
    def->height = 6;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("printer_model", coString);
    def->label = L("Printer type");
    def->tooltip = L("Type of the printer.");
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

    def = this->add("printer_vendor", coString);
    def->label = L("Printer vendor");
    def->tooltip = L("Name of the printer vendor.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("printer_variant", coString);
    def->label = L("Printer variant");
    def->tooltip = L("Name of the printer variant. For example, the printer variants may be differentiated by a nozzle diameter.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("print_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("printer_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("physical_printer_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("raft_contact_distance", coFloat);
    def->label = L("Raft contact Z distance");
    def->category = L("Support material");
    def->tooltip = L("The vertical distance between object and raft. Ignored for soluble interface.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.1));

    def = this->add("raft_expansion", coFloat);
    def->label = L("Raft expansion");
    def->category = L("Support material");
    def->tooltip = L("Expansion of the raft in XY plane for better stability.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(1.5));

    def = this->add("raft_first_layer_density", coPercent);
    def->label = L("First layer density");
    def->category = L("Support material");
    def->tooltip = L("Density of the first raft or support layer.");
    def->sidetext = L("%");
    def->min = 10;
    def->max = 100;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercent(90));

    def = this->add("raft_first_layer_expansion", coFloat);
    def->label = L("First layer expansion");
    def->category = L("Support material");
    def->tooltip = L("Expansion of the first raft or support layer to improve adhesion to print bed.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(3.));

    def = this->add("raft_layers", coInt);
    def->label = L("Raft layers");
    def->category = L("Support material");
    def->tooltip = L("The object will be raised by this number of layers, and support material "
                   "will be generated under it.");
    def->sidetext = L("layers");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("resolution", coFloat);
    def->label = L("Slice resolution");
    def->tooltip = L("Minimum detail resolution, used to simplify the input file for speeding up "
                   "the slicing job and reducing memory usage. High-resolution models often carry "
                   "more detail than printers can render. Set to zero to disable any simplification "
                   "and use full resolution from input.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("gcode_resolution", coFloat);
    def->label = L("G-code resolution");
    def->tooltip = L("Maximum deviation of exported G-code paths from their full resolution counterparts. "
                     "Very high resolution G-code requires huge amount of RAM to slice and preview, "
                     "also a 3D printer may stutter not being able to process a high resolution G-code in a timely manner. "
                     "On the other hand, a low resolution G-code will produce a low poly effect and because "
                     "the G-code reduction is performed at each layer independently, visible artifacts may be produced.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.0125));

    def = this->add("retract_before_travel", coFloats);
    def->label = L("Minimum travel after retraction");
    def->tooltip = L("Retraction is not triggered when travel moves are shorter than this length.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 2. });

    def = this->add("retract_before_wipe", coPercents);
    def->label = L("Retract amount before wipe");
    def->tooltip = L("With bowden extruders, it may be wise to do some amount of quick retract "
                   "before doing the wipe movement.");
    def->sidetext = L("%");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercents { 0. });

    def = this->add("retract_layer_change", coBools);
    def->label = L("Retract on layer change");
    def->tooltip = L("This flag enforces a retraction whenever a Z move is done.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("retract_length", coFloats);
    def->label = L("Length");
    def->full_label = L("Retraction Length");
    def->tooltip = L("When retraction is triggered, filament is pulled back by the specified amount "
                   "(the length is measured on raw filament, before it enters the extruder).");
    def->sidetext = L("mm (zero to disable)");
    def->set_default_value(new ConfigOptionFloats { 2. });

    def = this->add("retract_length_toolchange", coFloats);
    def->label = L("Length");
    def->full_label = L("Retraction Length (Toolchange)");
    def->tooltip = L("When retraction is triggered before changing tool, filament is pulled back "
                   "by the specified amount (the length is measured on raw filament, before it enters "
                   "the extruder).");
    def->sidetext = L("mm (zero to disable)");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 10. });

    def = this->add("retract_lift", coFloats);
    def->label = L("Lift Z");
    def->tooltip = L("If you set this to a positive value, Z is quickly raised every time a retraction "
                   "is triggered. When using multiple extruders, only the setting for the first extruder "
                   "will be considered.");
    def->sidetext = L("mm");
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("retract_lift_above", coFloats);
    def->label = L("Above Z");
    def->full_label = L("Only lift Z above");
    def->tooltip = L("If you set this to a positive value, Z lift will only take place above the specified "
                   "absolute Z. You can tune this setting for skipping lift on the first layers.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("retract_lift_below", coFloats);
    def->label = L("Below Z");
    def->full_label = L("Only lift Z below");
    def->tooltip = L("If you set this to a positive value, Z lift will only take place below "
                   "the specified absolute Z. You can tune this setting for limiting lift "
                   "to the first layers.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

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
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("retract_speed", coFloats);
    def->label = L("Retraction Speed");
    def->full_label = L("Retraction Speed");
    def->tooltip = L("The speed for retractions (it only applies to the extruder motor).");
    def->sidetext = L("mm/s");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 40. });

    def = this->add("deretract_speed", coFloats);
    def->label = L("Deretraction Speed");
    def->full_label = L("Deretraction Speed");
    def->tooltip = L("The speed for loading of a filament into extruder after retraction "
                   "(it only applies to the extruder motor). If left to zero, the retraction speed is used.");
    def->sidetext = L("mm/s");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats { 0. });

    def = this->add("seam_position", coEnum);
    def->label = L("Seam position");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Position of perimeters starting points.");
    def->enum_keys_map = &ConfigOptionEnum<SeamPosition>::get_enum_values();
    def->enum_values.push_back("random");
    def->enum_values.push_back("nearest");
    def->enum_values.push_back("aligned");
    def->enum_values.push_back("rear");
    def->enum_labels.push_back(L("Random"));
    def->enum_labels.push_back(L("Nearest"));
    def->enum_labels.push_back(L("Aligned"));
    def->enum_labels.push_back(L("Rear"));
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionEnum<SeamPosition>(spAligned));

#if 0
    def = this->add("seam_preferred_direction", coFloat);
//    def->gui_type = ConfigOptionDef::GUIType::slider;
    def->label = L("Direction");
    def->sidetext = L("°");
    def->full_label = L("Preferred direction of the seam");
    def->tooltip = L("Seam preferred direction");
    def->min = 0;
    def->max = 360;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("seam_preferred_direction_jitter", coFloat);
//    def->gui_type = ConfigOptionDef::GUIType::slider;
    def->label = L("Jitter");
    def->sidetext = L("°");
    def->full_label = L("Seam preferred direction jitter");
    def->tooltip = L("Preferred direction of the seam - jitter");
    def->min = 0;
    def->max = 360;
    def->set_default_value(new ConfigOptionFloat(30));
#endif

    def = this->add("skirt_distance", coFloat);
    def->label = L("Distance from brim/object");
    def->tooltip = L("Distance between skirt and brim (when draft shield is not used) or objects.");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(6));

    def = this->add("skirt_height", coInt);
    def->label = L("Skirt height");
    def->tooltip = L("Height of skirt expressed in layers.");
    def->sidetext = L("layers");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("draft_shield", coEnum);
    def->label = L("Draft shield");
    def->tooltip = L("With draft shield active, the skirt will be printed skirt_distance from the object, possibly intersecting brim.\n"
                     "Enabled = skirt is as tall as the highest printed object.\n"
                     "Limited = skirt is as tall as specified by skirt_height.\n"
    				 "This is useful to protect an ABS or ASA print from warping and detaching from print bed due to wind draft.");
    def->enum_keys_map = &ConfigOptionEnum<DraftShield>::get_enum_values();
    def->enum_values.push_back("disabled");
    def->enum_values.push_back("limited");
    def->enum_values.push_back("enabled");
    def->enum_labels.push_back(L("Disabled"));
    def->enum_labels.push_back(L("Limited"));
    def->enum_labels.push_back(L("Enabled"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<DraftShield>(dsDisabled));

    def = this->add("skirts", coInt);
    def->label = L("Loops (minimum)");
    def->full_label = L("Skirt Loops");
    def->tooltip = L("Number of loops for the skirt. If the Minimum Extrusion Length option is set, "
                   "the number of loops might be greater than the one configured here. Set this to zero "
                   "to disable skirt completely.");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("slowdown_below_layer_time", coInts);
    def->label = L("Slow down if layer print time is below");
    def->tooltip = L("If layer print time is estimated below this number of seconds, print moves "
                   "speed will be scaled down to extend duration to this value.");
    def->sidetext = L("approximate seconds");
    def->min = 0;
    def->max = 1000;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInts { 5 });

    def = this->add("small_perimeter_speed", coFloatOrPercent);
    def->label = L("Small perimeters");
    def->category = L("Speed");
    def->tooltip = L("This separate setting will affect the speed of perimeters having radius <= 6.5mm "
                   "(usually holes). If expressed as percentage (for example: 80%) it will be calculated "
                   "on the perimeters speed setting above. Set to zero for auto.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "perimeter_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(15, false));

    def = this->add("solid_infill_below_area", coFloat);
    def->label = L("Solid infill threshold area");
    def->category = L("Infill");
    def->tooltip = L("Force solid infill for regions having a smaller area than the specified threshold.");
    def->sidetext = L("mm²");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(70));

    def = this->add("solid_infill_extruder", coInt);
    def->label = L("Solid infill extruder");
    def->category = L("Extruders");
    def->tooltip = L("The extruder to use when printing solid infill.");
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("solid_infill_every_layers", coInt);
    def->label = L("Solid infill every");
    def->category = L("Infill");
    def->tooltip = L("This feature allows to force a solid layer every given number of layers. "
                   "Zero to disable. You can set this to any value (for example 9999); "
                   "Slic3r will automatically choose the maximum possible number of layers "
                   "to combine according to nozzle diameter and layer height.");
    def->sidetext = L("layers");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("solid_infill_extrusion_width", coFloatOrPercent);
    def->label = L("Solid infill");
    def->category = L("Extrusion Width");
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for infill for solid surfaces. "
                   "If left zero, default extrusion width will be used if set, otherwise 1.125 x nozzle diameter will be used. "
                   "If expressed as percentage (for example 90%) it will be computed over layer height.");
    def->sidetext = L("mm or %");
    def->min = 0;
    def->max_literal = 50;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("solid_infill_speed", coFloatOrPercent);
    def->label = L("Solid infill");
    def->category = L("Speed");
    def->tooltip = L("Speed for printing solid regions (top/bottom/internal horizontal shells). "
                   "This can be expressed as a percentage (for example: 80%) over the default "
                   "infill speed above. Set to zero for auto.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "infill_speed";
    def->aliases = { "solid_infill_feed_rate" };
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(20, false));

    def = this->add("solid_layers", coInt);
    def->label = L("Solid layers");
    def->tooltip = L("Number of solid layers to generate on top and bottom surfaces.");
    def->shortcut.push_back("top_solid_layers");
    def->shortcut.push_back("bottom_solid_layers");
    def->min = 0;

    def = this->add("solid_min_thickness", coFloat);
    def->label = L("Minimum thickness of a top / bottom shell");
    def->tooltip = L("Minimum thickness of a top / bottom shell");
    def->shortcut.push_back("top_solid_min_thickness");
    def->shortcut.push_back("bottom_solid_min_thickness");
    def->min = 0;

    def = this->add("spiral_vase", coBool);
    def->label = L("Spiral vase");
    def->tooltip = L("This feature will raise Z gradually while printing a single-walled object "
                   "in order to remove any visible seam. This option requires a single perimeter, "
                   "no infill, no top solid layers and no support material. You can still set "
                   "any number of bottom solid layers as well as skirt/brim loops. "
                   "It won't work when printing more than one single object.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("standby_temperature_delta", coInt);
    def->label = L("Temperature variation");
    def->tooltip = L("Temperature difference to be applied when an extruder is not active. "
                   "Enables a full-height \"sacrificial\" skirt on which the nozzles are periodically wiped.");
    def->sidetext = "∆°C";
    def->min = -max_temp;
    def->max = max_temp;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInt(-5));

    def = this->add("start_gcode", coString);
    def->label = L("Start G-code");
    def->tooltip = L("This start procedure is inserted at the beginning, after bed has reached "
                   "the target temperature and extruder just started heating, and before extruder "
                   "has finished heating. If PrusaSlicer detects M104 or M190 in your custom codes, "
                   "such commands will not be prepended automatically so you're free to customize "
                   "the order of heating commands and other custom actions. Note that you can use "
                   "placeholder variables for all PrusaSlicer settings, so you can put "
                   "a \"M109 S[first_layer_temperature]\" command wherever you want.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString("G28 ; home all axes\nG1 Z5 F5000 ; lift nozzle\n"));

    def = this->add("start_filament_gcode", coStrings);
    def->label = L("Start G-code");
    def->tooltip = L("This start procedure is inserted at the beginning, after any printer start gcode (and "
                   "after any toolchange to this filament in case of multi-material printers). "
                   "This is used to override settings for a specific filament. If PrusaSlicer detects "
                   "M104, M109, M140 or M190 in your custom codes, such commands will "
                   "not be prepended automatically so you're free to customize the order "
                   "of heating commands and other custom actions. Note that you can use placeholder variables "
                   "for all PrusaSlicer settings, so you can put a \"M109 S[first_layer_temperature]\" command "
                   "wherever you want. If you have multiple extruders, the gcode is processed "
                   "in extruder order.");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionStrings { "; Filament gcode\n" });

    def = this->add("color_change_gcode", coString);
    def->label = L("Color change G-code");
    def->tooltip = L("This G-code will be used as a code for the color change");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString("M600"));

    def = this->add("pause_print_gcode", coString);
    def->label = L("Pause Print G-code");
    def->tooltip = L("This G-code will be used as a code for the pause print");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString("M601"));

    def = this->add("template_custom_gcode", coString);
    def->label = L("Custom G-code");
    def->tooltip = L("This G-code will be used as a custom code");
    def->multiline = true;
    def->full_width = true;
    def->height = 12;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("single_extruder_multi_material", coBool);
    def->label = L("Single Extruder Multi Material");
    def->tooltip = L("The printer multiplexes filaments into a single hot end.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("single_extruder_multi_material_priming", coBool);
    def->label = L("Prime all printing extruders");
    def->tooltip = L("If enabled, all printing extruders will be primed at the front edge of the print bed at the start of the print.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("wipe_tower_no_sparse_layers", coBool);
    def->label = L("No sparse layers (EXPERIMENTAL)");
    def->tooltip = L("If enabled, the wipe tower will not be printed on layers with no toolchanges. "
                     "On layers with a toolchange, extruder will travel downward to print the wipe tower. "
                     "User is responsible for ensuring there is no collision with the print.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("slice_closing_radius", coFloat);
    def->label = L("Slice gap closing radius");
    def->category = L("Advanced");
    def->tooltip = L("Cracks smaller than 2x gap closing radius are being filled during the triangle mesh slicing. "
                     "The gap closing operation may reduce the final print resolution, therefore it is advisable to keep the value reasonably low.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.049));

    def = this->add("slicing_mode", coEnum);
    def->label = L("Slicing Mode");
    def->category = L("Advanced");
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

    def = this->add("support_material", coBool);
    def->label = L("Generate support material");
    def->category = L("Support material");
    def->tooltip = L("Enable support material generation.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_material_auto", coBool);
    def->label = L("Auto generated supports");
    def->category = L("Support material");
    def->tooltip = L("If checked, supports will be generated automatically based on the overhang threshold value."\
                     " If unchecked, supports will be generated inside the \"Support Enforcer\" volumes only.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("support_material_xy_spacing", coFloatOrPercent);
    def->label = L("XY separation between an object and its support");
    def->category = L("Support material");
    def->tooltip = L("XY separation between an object and its support. If expressed as percentage "
                   "(for example 50%), it will be calculated over external perimeter width.");
    def->sidetext = L("mm or %");
    def->ratio_over = "external_perimeter_extrusion_width";
    def->min = 0;
    def->max_literal = 10;
    def->mode = comAdvanced;
    // Default is half the external perimeter width.
    def->set_default_value(new ConfigOptionFloatOrPercent(50, true));

    def = this->add("support_material_angle", coFloat);
    def->label = L("Pattern angle");
    def->category = L("Support material");
    def->tooltip = L("Use this setting to rotate the support material pattern on the horizontal plane.");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 359;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("support_material_buildplate_only", coBool);
    def->label = L("Support on build plate only");
    def->category = L("Support material");
    def->tooltip = L("Only create support if it lies on a build plate. Don't create support on a print.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_material_contact_distance", coFloat);
    def->gui_type = ConfigOptionDef::GUIType::f_enum_open;
    def->label = L("Top contact Z distance");
    def->category = L("Support material");
    def->tooltip = L("The vertical distance between object and support material interface. "
                   "Setting this to 0 will also prevent Slic3r from using bridge flow and speed "
                   "for the first object layer.");
    def->sidetext = L("mm");
//    def->min = 0;
    def->enum_values.push_back("0");
    def->enum_values.push_back("0.1");
    def->enum_values.push_back("0.2");
    def->enum_labels.push_back(L("0 (soluble)"));
    def->enum_labels.push_back(L("0.1 (detachable)"));
    def->enum_labels.push_back(L("0.2 (detachable)"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("support_material_bottom_contact_distance", coFloat);
    def->gui_type = ConfigOptionDef::GUIType::f_enum_open;
    def->label = L("Bottom contact Z distance");
    def->category = L("Support material");
    def->tooltip = L("The vertical distance between the object top surface and the support material interface. "
                   "If set to zero, support_material_contact_distance will be used for both top and bottom contact Z distances.");
    def->sidetext = L("mm");
//    def->min = 0;
    def->enum_values.push_back("0");
    def->enum_values.push_back("0.1");
    def->enum_values.push_back("0.2");
    def->enum_labels.push_back(L("same as top"));
    def->enum_labels.push_back(L("0.1"));
    def->enum_labels.push_back(L("0.2"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("support_material_enforce_layers", coInt);
    def->label = L("Enforce support for the first");
    def->category = L("Support material");
    def->tooltip = L("Generate support material for the specified number of layers counting from bottom, "
                   "regardless of whether normal support material is enabled or not and regardless "
                   "of any angle threshold. This is useful for getting more adhesion of objects "
                   "having a very thin or poor footprint on the build plate.");
    def->sidetext = L("layers");
    def->full_label = L("Enforce support for the first n layers");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("support_material_extruder", coInt);
    def->label = L("Support material/raft/skirt extruder");
    def->category = L("Extruders");
    def->tooltip = L("The extruder to use when printing support material, raft and skirt "
                   "(1+, 0 to use the current extruder to minimize tool changes).");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    def = this->add("support_material_extrusion_width", coFloatOrPercent);
    def->label = L("Support material");
    def->category = L("Extrusion Width");
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for support material. "
                   "If left zero, default extrusion width will be used if set, otherwise nozzle diameter will be used. "
                   "If expressed as percentage (for example 90%) it will be computed over layer height.");
    def->sidetext = L("mm or %");
    def->min = 0;
    def->max_literal = 50;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("support_material_interface_contact_loops", coBool);
    def->label = L("Interface loops");
    def->category = L("Support material");
    def->tooltip = L("Cover the top contact layer of the supports with loops. Disabled by default.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_material_interface_extruder", coInt);
    def->label = L("Support material/raft interface extruder");
    def->category = L("Extruders");
    def->tooltip = L("The extruder to use when printing support material interface "
                   "(1+, 0 to use the current extruder to minimize tool changes). This affects raft too.");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(1));

    auto support_material_interface_layers = def = this->add("support_material_interface_layers", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Top interface layers");
    def->category = L("Support material");
    def->tooltip = L("Number of interface layers to insert between the object(s) and support material.");
    def->sidetext = L("layers");
    def->min = 0;
    def->enum_values.push_back("0");
    def->enum_values.push_back("1");
    def->enum_values.push_back("2");
    def->enum_values.push_back("3");
    def->enum_labels.push_back(L("0 (off)"));
    def->enum_labels.push_back(L("1 (light)"));
    def->enum_labels.push_back(L("2 (default)"));
    def->enum_labels.push_back(L("3 (heavy)"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("support_material_bottom_interface_layers", coInt);
    def->gui_type = ConfigOptionDef::GUIType::i_enum_open;
    def->label = L("Bottom interface layers");
    def->category = L("Support material");
    def->tooltip = L("Number of interface layers to insert between the object(s) and support material. "
                     "Set to -1 to use support_material_interface_layers");
    def->sidetext = L("layers");
    def->min = -1;
    def->enum_values.push_back("-1");
    append(def->enum_values, support_material_interface_layers->enum_values);
    def->enum_labels.push_back(L("same as top"));
    append(def->enum_labels, support_material_interface_layers->enum_labels);
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(-1));

    def = this->add("support_material_closing_radius", coFloat);
    def->label = L("Closing radius");
    def->category = L("Support material");
    def->tooltip = L("For snug supports, the support regions will be merged using morphological closing operation."
                     " Gaps smaller than the closing radius will be filled in.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(2));

    def = this->add("support_material_interface_spacing", coFloat);
    def->label = L("Interface pattern spacing");
    def->category = L("Support material");
    def->tooltip = L("Spacing between interface lines. Set zero to get a solid interface.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("support_material_interface_speed", coFloatOrPercent);
    def->label = L("Support material interface");
    def->category = L("Support material");
    def->tooltip = L("Speed for printing support material interface layers. If expressed as percentage "
                   "(for example 50%) it will be calculated over support material speed.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "support_material_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(100, true));

    def = this->add("support_material_pattern", coEnum);
    def->label = L("Pattern");
    def->category = L("Support material");
    def->tooltip = L("Pattern used to generate support material.");
    def->enum_keys_map = &ConfigOptionEnum<SupportMaterialPattern>::get_enum_values();
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("rectilinear-grid");
    def->enum_values.push_back("honeycomb");
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Rectilinear grid"));
    def->enum_labels.push_back(L("Honeycomb"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SupportMaterialPattern>(smpRectilinear));

    def = this->add("support_material_interface_pattern", coEnum);
    def->label = L("Interface pattern");
    def->category = L("Support material");
    def->tooltip = L("Pattern used to generate support material interface. "
                     "Default pattern for non-soluble support interface is Rectilinear, "
                     "while default pattern for soluble support interface is Concentric.");
    def->enum_keys_map = &ConfigOptionEnum<SupportMaterialInterfacePattern>::get_enum_values();
    def->enum_values.push_back("auto");
    def->enum_values.push_back("rectilinear");
    def->enum_values.push_back("concentric");
    def->enum_labels.push_back(L("Default"));
    def->enum_labels.push_back(L("Rectilinear"));
    def->enum_labels.push_back(L("Concentric"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SupportMaterialInterfacePattern>(smipRectilinear));

    def = this->add("support_material_spacing", coFloat);
    def->label = L("Pattern spacing");
    def->category = L("Support material");
    def->tooltip = L("Spacing between support material lines.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(2.5));

    def = this->add("support_material_speed", coFloat);
    def->label = L("Support material");
    def->category = L("Support material");
    def->tooltip = L("Speed for printing support material.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(60));

    def = this->add("support_material_style", coEnum);
    def->label = L("Style");
    def->category = L("Support material");
    def->tooltip = L("Style and shape of the support towers. Projecting the supports into a regular grid "
                     "will create more stable supports, while snug support towers will save material and reduce "
                     "object scarring.");
    def->enum_keys_map = &ConfigOptionEnum<SupportMaterialStyle>::get_enum_values();
    def->enum_values.push_back("grid");
    def->enum_values.push_back("snug");
    def->enum_labels.push_back(L("Grid"));
    def->enum_labels.push_back(L("Snug"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SupportMaterialStyle>(smsGrid));

    def = this->add("support_material_synchronize_layers", coBool);
    def->label = L("Synchronize with object layers");
    def->category = L("Support material");
    def->tooltip = L("Synchronize support layers with the object print layers. This is useful "
                   "with multi-material printers, where the extruder switch is expensive.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_material_threshold", coInt);
    def->label = L("Overhang threshold");
    def->category = L("Support material");
    def->tooltip = L("Support material will not be generated for overhangs whose slope angle "
                   "(90° = vertical) is above the given threshold. In other words, this value "
                   "represent the most horizontal slope (measured from the horizontal plane) "
                   "that you can print without support material. Set to zero for automatic detection "
                   "(recommended).");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 90;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(0));

    def = this->add("support_material_with_sheath", coBool);
    def->label = L("With sheath around the support");
    def->category = L("Support material");
    def->tooltip = L("Add a sheath (a single perimeter line) around the base support. This makes "
                   "the support more reliable, but also more difficult to remove.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("temperature", coInts);
    def->label = L("Other layers");
    def->tooltip = L("Nozzle temperature for layers after the first one. Set this to zero to disable "
                     "temperature control commands in the output G-code.");
    def->sidetext = L("°C");
    def->full_label = L("Nozzle temperature");
    def->min = 0;
    def->max = max_temp;
    def->set_default_value(new ConfigOptionInts { 200 });

    def = this->add("thick_bridges", coBool);
    def->label = L("Thick bridges");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("If enabled, bridges are more reliable, can bridge longer distances, but may look worse. "
                     "If disabled, bridges look better but are reliable just for shorter bridged distances.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("thin_walls", coBool);
    def->label = L("Detect thin walls");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Detect single-width walls (parts where two extrusions don't fit and we need "
                   "to collapse them into a single trace).");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("threads", coInt);
    def->label = L("Threads");
    def->tooltip = L("Threads are used to parallelize long-running tasks. Optimal threads number "
                   "is slightly above the number of available cores/processors.");
    def->readonly = true;
    def->min = 1;
    {
        int threads = (unsigned int)boost::thread::hardware_concurrency();
        def->set_default_value(new ConfigOptionInt(threads > 0 ? threads : 2));
        def->cli = ConfigOptionDef::nocli;
    }

    def = this->add("toolchange_gcode", coString);
    def->label = L("Tool change G-code");
    def->tooltip = L("This custom code is inserted before every toolchange. Placeholder variables for all PrusaSlicer settings "
                     "as well as {toolchange_z}, {previous_extruder} and {next_extruder} can be used. When a tool-changing command "
                     "which changes to the correct extruder is included (such as T{next_extruder}), PrusaSlicer will emit no other such command. "
                     "It is therefore possible to script custom behaviour both before and after the toolchange.");
    def->multiline = true;
    def->full_width = true;
    def->height = 5;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("top_infill_extrusion_width", coFloatOrPercent);
    def->label = L("Top solid infill");
    def->category = L("Extrusion Width");
    def->tooltip = L("Set this to a non-zero value to set a manual extrusion width for infill for top surfaces. "
                   "You may want to use thinner extrudates to fill all narrow regions and get a smoother finish. "
                   "If left zero, default extrusion width will be used if set, otherwise nozzle diameter will be used. "
                   "If expressed as percentage (for example 90%) it will be computed over layer height.");
    def->sidetext = L("mm or %");
    def->min = 0;
    def->max_literal = 50;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(0, false));

    def = this->add("top_solid_infill_speed", coFloatOrPercent);
    def->label = L("Top solid infill");
    def->category = L("Speed");
    def->tooltip = L("Speed for printing top solid layers (it only applies to the uppermost "
                   "external layers and not to their internal solid layers). You may want "
                   "to slow down this to get a nicer surface finish. This can be expressed "
                   "as a percentage (for example: 80%) over the solid infill speed above. "
                   "Set to zero for auto.");
    def->sidetext = L("mm/s or %");
    def->ratio_over = "solid_infill_speed";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloatOrPercent(15, false));

    def = this->add("top_solid_layers", coInt);
    //TRN To be shown in Print Settings "Top solid layers"
    def->label = L("Top");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("Number of solid layers to generate on top surfaces.");
    def->full_label = L("Top solid layers");
    def->min = 0;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("top_solid_min_thickness", coFloat);
    //TRN To be shown in Print Settings "Top solid layers"
    def->label = L("Top");
    def->category = L("Layers and Perimeters");
    def->tooltip = L("The number of top solid layers is increased above top_solid_layers if necessary to satisfy "
    				 "minimum thickness of top shell."
    				 " This is useful to prevent pillowing effect when printing with variable layer height.");
    def->full_label = L("Minimum top shell thickness");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("travel_speed", coFloat);
    def->label = L("Travel");
    def->tooltip = L("Speed for travel moves (jumps between distant extrusion points).");
    def->sidetext = L("mm/s");
    def->aliases = { "travel_feed_rate" };
    def->min = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(130));

    def = this->add("travel_speed_z", coFloat);
    def->label = L("Z travel");
    def->tooltip = L("Speed for movements along the Z axis.\nWhen set to zero, the value "
                     "is ignored and regular travel speed is used instead.");
    def->sidetext = L("mm/s");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("use_firmware_retraction", coBool);
    def->label = L("Use firmware retraction");
    def->tooltip = L("This experimental setting uses G10 and G11 commands to have the firmware "
                   "handle the retraction. This is only supported in recent Marlin.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("use_relative_e_distances", coBool);
    def->label = L("Use relative E distances");
    def->tooltip = L("If your firmware requires relative E values, check this, "
                   "otherwise leave it unchecked. Most firmwares use absolute values.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("use_volumetric_e", coBool);
    def->label = L("Use volumetric E");
    def->tooltip = L("This experimental setting uses outputs the E values in cubic millimeters "
                   "instead of linear millimeters. If your firmware doesn't already know "
                   "filament diameter(s), you can put commands like 'M200 D[filament_diameter_0] T0' "
                   "in your start G-code in order to turn volumetric mode on and use the filament "
                   "diameter associated to the filament selected in Slic3r. This is only supported "
                   "in recent Marlin.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("variable_layer_height", coBool);
    def->label = L("Enable variable layer height feature");
    def->tooltip = L("Some printers or printer setups may have difficulties printing "
                   "with a variable layer height. Enabled by default.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("wipe", coBools);
    def->label = L("Wipe while retracting");
    def->tooltip = L("This flag will move the nozzle while retracting to minimize the possible blob "
                   "on leaky extruders.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBools { false });

    def = this->add("wipe_tower", coBool);
    def->label = L("Enable");
    def->tooltip = L("Multi material printers may need to prime or purge extruders on tool changes. "
                   "Extrude the excess material into the wipe tower.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wiping_volumes_extruders", coFloats);
    def->label = L("Purging volumes - load/unload volumes");
    def->tooltip = L("This vector saves required volumes to change from/to each tool used on the "
                     "wipe tower. These values are used to simplify creation of the full purging "
                     "volumes below.");
    def->set_default_value(new ConfigOptionFloats { 70.f, 70.f, 70.f, 70.f, 70.f, 70.f, 70.f, 70.f, 70.f, 70.f  });

    def = this->add("wiping_volumes_matrix", coFloats);
    def->label = L("Purging volumes - matrix");
    def->tooltip = L("This matrix describes volumes (in cubic milimetres) required to purge the"
                     " new filament on the wipe tower for any given pair of tools.");
    def->set_default_value(new ConfigOptionFloats {   0.f, 140.f, 140.f, 140.f, 140.f,
                                                    140.f,   0.f, 140.f, 140.f, 140.f,
                                                    140.f, 140.f,   0.f, 140.f, 140.f,
                                                    140.f, 140.f, 140.f,   0.f, 140.f,
                                                    140.f, 140.f, 140.f, 140.f,   0.f });

    def = this->add("wipe_tower_x", coFloat);
    def->label = L("Position X");
    def->tooltip = L("X coordinate of the left front corner of a wipe tower");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(180.));

    def = this->add("wipe_tower_y", coFloat);
    def->label = L("Position Y");
    def->tooltip = L("Y coordinate of the left front corner of a wipe tower");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(140.));

    def = this->add("wipe_tower_width", coFloat);
    def->label = L("Width");
    def->tooltip = L("Width of a wipe tower");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(60.));

    def = this->add("wipe_tower_rotation_angle", coFloat);
    def->label = L("Wipe tower rotation angle");
    def->tooltip = L("Wipe tower rotation angle with respect to x-axis.");
    def->sidetext = L("°");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("wipe_tower_brim_width", coFloat);
    def->label = L("Wipe tower brim width");
    def->tooltip = L("Wipe tower brim width");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->min = 0.f;
    def->set_default_value(new ConfigOptionFloat(2.));

    def = this->add("wipe_into_infill", coBool);
    def->category = L("Wipe options");
    def->label = L("Wipe into this object's infill");
    def->tooltip = L("Purging after toolchange will be done inside this object's infills. "
                     "This lowers the amount of waste but may result in longer print time "
                     " due to additional travel moves.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wipe_into_objects", coBool);
    def->category = L("Wipe options");
    def->label = L("Wipe into this object");
    def->tooltip = L("Object will be used to purge the nozzle after a toolchange to save material "
                     "that would otherwise end up in the wipe tower and decrease print time. "
                     "Colours of the objects will be mixed as a result.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("wipe_tower_bridging", coFloat);
    def->label = L("Maximal bridging distance");
    def->tooltip = L("Maximal distance between supports on sparse infill sections.");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10.));

    def = this->add("xy_size_compensation", coFloat);
    def->label = L("XY Size Compensation");
    def->category = L("Advanced");
    def->tooltip = L("The object will be grown/shrunk in the XY plane by the configured value "
                   "(negative = inwards, positive = outwards). This might be useful "
                   "for fine-tuning hole sizes.");
    def->sidetext = L("mm");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("z_offset", coFloat);
    def->label = L("Z offset");
    def->tooltip = L("This value will be added (or subtracted) from all the Z coordinates "
                   "in the output G-code. It is used to compensate for bad Z endstop position: "
                   "for example, if your endstop zero actually leaves the nozzle 0.3mm far "
                   "from the print bed, set this to -0.3 (or fix your endstop).");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    // Declare retract values for filament profile, overriding the printer's extruder profile.
    for (const char *opt_key : {
        // floats
        "retract_length", "retract_lift", "retract_lift_above", "retract_lift_below", "retract_speed", "deretract_speed", "retract_restart_extra", "retract_before_travel",
        // bools
        "retract_layer_change", "wipe",
        // percents
        "retract_before_wipe"}) {
        auto it_opt = options.find(opt_key);
        assert(it_opt != options.end());
        def = this->add_nullable(std::string("filament_") + opt_key, it_opt->second.type);
        def->label 		= it_opt->second.label;
        def->full_label = it_opt->second.full_label;
        def->tooltip 	= it_opt->second.tooltip;
        def->sidetext   = it_opt->second.sidetext;
        def->mode       = it_opt->second.mode;
        switch (def->type) {
        case coFloats   : def->set_default_value(new ConfigOptionFloatsNullable  (static_cast<const ConfigOptionFloats*  >(it_opt->second.default_value.get())->values)); break;
        case coPercents : def->set_default_value(new ConfigOptionPercentsNullable(static_cast<const ConfigOptionPercents*>(it_opt->second.default_value.get())->values)); break;
        case coBools    : def->set_default_value(new ConfigOptionBoolsNullable   (static_cast<const ConfigOptionBools*   >(it_opt->second.default_value.get())->values)); break;
        default: assert(false);
        }
    }
}

void PrintConfigDef::init_extruder_option_keys()
{
    // ConfigOptionFloats, ConfigOptionPercents, ConfigOptionBools, ConfigOptionStrings
    m_extruder_option_keys = {
        "nozzle_diameter", "min_layer_height", "max_layer_height", "extruder_offset",
        "retract_length", "retract_lift", "retract_lift_above", "retract_lift_below", "retract_speed", "deretract_speed",
        "retract_before_wipe", "retract_restart_extra", "retract_before_travel", "wipe",
        "retract_layer_change", "retract_length_toolchange", "retract_restart_extra_toolchange", "extruder_colour",
        "default_filament_profile"
    };

    m_extruder_retract_keys = {
        "deretract_speed",
        "retract_before_travel",
        "retract_before_wipe",
        "retract_layer_change",
        "retract_length",
        "retract_lift",
        "retract_lift_above",
        "retract_lift_below",
        "retract_restart_extra",
        "retract_speed",
        "wipe"
    };
    assert(std::is_sorted(m_extruder_retract_keys.begin(), m_extruder_retract_keys.end()));
}

void PrintConfigDef::init_sla_params()
{
    ConfigOptionDef* def;

    // SLA Printer settings

    def = this->add("display_width", coFloat);
    def->label = L("Display width");
    def->tooltip = L("Width of the display");
    def->min = 1;
    def->set_default_value(new ConfigOptionFloat(120.));

    def = this->add("display_height", coFloat);
    def->label = L("Display height");
    def->tooltip = L("Height of the display");
    def->min = 1;
    def->set_default_value(new ConfigOptionFloat(68.));

    def = this->add("display_pixels_x", coInt);
    def->full_label = L("Number of pixels in");
    def->label = ("X");
    def->tooltip = L("Number of pixels in X");
    def->min = 100;
    def->set_default_value(new ConfigOptionInt(2560));

    def = this->add("display_pixels_y", coInt);
    def->label = ("Y");
    def->tooltip = L("Number of pixels in Y");
    def->min = 100;
    def->set_default_value(new ConfigOptionInt(1440));

    def = this->add("display_mirror_x", coBool);
    def->full_label = L("Display horizontal mirroring");
    def->label = L("Mirror horizontally");
    def->tooltip = L("Enable horizontal mirroring of output images");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("display_mirror_y", coBool);
    def->full_label = L("Display vertical mirroring");
    def->label = L("Mirror vertically");
    def->tooltip = L("Enable vertical mirroring of output images");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("display_orientation", coEnum);
    def->label = L("Display orientation");
    def->tooltip = L("Set the actual LCD display orientation inside the SLA printer."
                     " Portrait mode will flip the meaning of display width and height parameters"
                     " and the output images will be rotated by 90 degrees.");
    def->enum_keys_map = &ConfigOptionEnum<SLADisplayOrientation>::get_enum_values();
    def->enum_values.push_back("landscape");
    def->enum_values.push_back("portrait");
    def->enum_labels.push_back(L("Landscape"));
    def->enum_labels.push_back(L("Portrait"));
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionEnum<SLADisplayOrientation>(sladoPortrait));

    def = this->add("fast_tilt_time", coFloat);
    def->label = L("Fast");
    def->full_label = L("Fast tilt");
    def->tooltip = L("Time of the fast tilt");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(5.));

    def = this->add("slow_tilt_time", coFloat);
    def->label = L("Slow");
    def->full_label = L("Slow tilt");
    def->tooltip = L("Time of the slow tilt");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(8.));

    def = this->add("area_fill", coFloat);
    def->label = L("Area fill");
    def->tooltip = L("The percentage of the bed area. \nIf the print area exceeds the specified value, \nthen a slow tilt will be used, otherwise - a fast tilt");
    def->sidetext = L("%");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(50.));

    def = this->add("relative_correction", coFloats);
    def->label = L("Printer scaling correction");
    def->full_label = L("Printer scaling correction");
    def->tooltip  = L("Printer scaling correction");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats( { 1., 1.} ));

    def = this->add("relative_correction_x", coFloat);
    def->label = L("Printer scaling correction in X axis");
    def->full_label = L("Printer scaling X axis correction");
    def->tooltip  = L("Printer scaling correction in X axis");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("relative_correction_y", coFloat);
    def->label = L("Printer scaling correction in Y axis");
    def->full_label = L("Printer scaling X axis correction");
    def->tooltip  = L("Printer scaling correction in Y axis");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("relative_correction_z", coFloat);
    def->label = L("Printer scaling correction in Z axis");
    def->full_label = L("Printer scaling X axis correction");
    def->tooltip  = L("Printer scaling correction in Z axis");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("absolute_correction", coFloat);
    def->label = L("Printer absolute correction");
    def->full_label = L("Printer absolute correction");
    def->tooltip  = L("Will inflate or deflate the sliced 2D polygons according "
                      "to the sign of the correction.");
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.0));
    
    def = this->add("elefant_foot_min_width", coFloat);
    def->label = L("Elephant foot minimum width");
    def->category = L("Advanced");
    def->tooltip = L("Minimum width of features to maintain when doing elephant foot compensation.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("gamma_correction", coFloat);
    def->label = L("Printer gamma correction");
    def->full_label = L("Printer gamma correction");
    def->tooltip  = L("This will apply a gamma correction to the rasterized 2D "
                      "polygons. A gamma value of zero means thresholding with "
                      "the threshold in the middle. This behaviour eliminates "
                      "antialiasing without losing holes in polygons.");
    def->min = 0;
    def->max = 1;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(1.0));


    // SLA Material settings.

    def = this->add("material_colour", coString);
    def->label = L("Color");
    def->tooltip = L("This is only used in the Slic3r interface as a visual help.");
    def->gui_type = ConfigOptionDef::GUIType::color;
    def->set_default_value(new ConfigOptionString("#29B2B2"));

    def = this->add("material_type", coString);
    def->label = L("SLA material type");
    def->tooltip = L("SLA material type");
    def->gui_type = ConfigOptionDef::GUIType::f_enum_open;   // TODO: ???
    def->gui_flags = "show_value";
    def->enum_values.push_back("Tough");
    def->enum_values.push_back("Flexible");
    def->enum_values.push_back("Casting");
    def->enum_values.push_back("Dental");
    def->enum_values.push_back("Heat-resistant");
    def->set_default_value(new ConfigOptionString("Tough"));

    def = this->add("initial_layer_height", coFloat);
    def->label = L("Initial layer height");
    def->tooltip = L("Initial layer height");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.3));

    def = this->add("bottle_volume", coFloat);
    def->label = L("Bottle volume");
    def->tooltip = L("Bottle volume");
    def->sidetext = L("ml");
    def->min = 50;
    def->set_default_value(new ConfigOptionFloat(1000.0));

    def = this->add("bottle_weight", coFloat);
    def->label = L("Bottle weight");
    def->tooltip = L("Bottle weight");
    def->sidetext = L("kg");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("material_density", coFloat);
    def->label = L("Density");
    def->tooltip = L("Density");
    def->sidetext = L("g/ml");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("bottle_cost", coFloat);
    def->label = L("Cost");
    def->tooltip = L("Cost");
    def->sidetext = L("money/bottle");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("faded_layers", coInt);
    def->label = L("Faded layers");
    def->tooltip = L("Number of the layers needed for the exposure time fade from initial exposure time to the exposure time");
    def->min = 3;
    def->max = 20;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInt(10));

    def = this->add("min_exposure_time", coFloat);
    def->label = L("Minimum exposure time");
    def->tooltip = L("Minimum exposure time");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("max_exposure_time", coFloat);
    def->label = L("Maximum exposure time");
    def->tooltip = L("Maximum exposure time");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(100));

    def = this->add("exposure_time", coFloat);
    def->label = L("Exposure time");
    def->tooltip = L("Exposure time");
    def->sidetext = L("s");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(10));

    def = this->add("min_initial_exposure_time", coFloat);
    def->label = L("Minimum initial exposure time");
    def->tooltip = L("Minimum initial exposure time");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("max_initial_exposure_time", coFloat);
    def->label = L("Maximum initial exposure time");
    def->tooltip = L("Maximum initial exposure time");
    def->sidetext = L("s");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(150));

    def = this->add("initial_exposure_time", coFloat);
    def->label = L("Initial exposure time");
    def->tooltip = L("Initial exposure time");
    def->sidetext = L("s");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(15));

    def = this->add("material_correction", coFloats);
    def->full_label = L("Correction for expansion");
    def->tooltip  = L("Correction for expansion");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloats( { 1., 1., 1. } ));

    def = this->add("material_correction_x", coFloat);
    def->full_label = L("Correction for expansion in X axis");
    def->tooltip  = L("Correction for expansion in X axis");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("material_correction_y", coFloat);
    def->full_label = L("Correction for expansion in Y axis");
    def->tooltip  = L("Correction for expansion in Y axis");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("material_correction_z", coFloat);
    def->full_label = L("Correction for expansion in Z axis");
    def->tooltip  = L("Correction for expansion in Z axis");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("material_notes", coString);
    def->label = L("SLA print material notes");
    def->tooltip = L("You can put your notes regarding the SLA print material here.");
    def->multiline = true;
    def->full_width = true;
    def->height = 13;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("material_vendor", coString);
    def->set_default_value(new ConfigOptionString(L("(Unknown)")));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("default_sla_material_profile", coString);
    def->label = L("Default SLA material profile");
    def->tooltip = L("Default print profile associated with the current printer profile. "
                   "On selection of the current printer profile, this print profile will be activated.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("sla_material_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("default_sla_print_profile", coString);
    def->label = L("Default SLA material profile");
    def->tooltip = L("Default print profile associated with the current printer profile. "
                   "On selection of the current printer profile, this print profile will be activated.");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("sla_print_settings_id", coString);
    def->set_default_value(new ConfigOptionString(""));
    def->cli = ConfigOptionDef::nocli;

    def = this->add("supports_enable", coBool);
    def->label = L("Generate supports");
    def->category = L("Supports");
    def->tooltip = L("Generate supports for the models");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("support_head_front_diameter", coFloat);
    def->label = L("Pinhead front diameter");
    def->category = L("Supports");
    def->tooltip = L("Diameter of the pointing side of the head");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.4));

    def = this->add("support_head_penetration", coFloat);
    def->label = L("Head penetration");
    def->category = L("Supports");
    def->tooltip = L("How much the pinhead has to penetrate the model surface");
    def->sidetext = L("mm");
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("support_head_width", coFloat);
    def->label = L("Pinhead width");
    def->category = L("Supports");
    def->tooltip = L("Width from the back sphere center to the front sphere center");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 20;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("support_pillar_diameter", coFloat);
    def->label = L("Pillar diameter");
    def->category = L("Supports");
    def->tooltip = L("Diameter in mm of the support pillars");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 15;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("support_small_pillar_diameter_percent", coPercent);
    def->label = L("Small pillar diameter percent");
    def->category = L("Supports");
    def->tooltip = L("The percentage of smaller pillars compared to the normal pillar diameter "
                     "which are used in problematic areas where a normal pilla cannot fit.");
    def->sidetext = L("%");
    def->min = 1;
    def->max = 100;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionPercent(50));
    
    def = this->add("support_max_bridges_on_pillar", coInt);
    def->label = L("Max bridges on a pillar");
    def->tooltip = L(
        "Maximum number of bridges that can be placed on a pillar. Bridges "
        "hold support point pinheads and connect to pillars as small branches.");
    def->min = 0;
    def->max = 50;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("support_pillar_connection_mode", coEnum);
    def->label = L("Pillar connection mode");
    def->tooltip = L("Controls the bridge type between two neighboring pillars."
                     " Can be zig-zag, cross (double zig-zag) or dynamic which"
                     " will automatically switch between the first two depending"
                     " on the distance of the two pillars.");
    def->enum_keys_map = &ConfigOptionEnum<SLAPillarConnectionMode>::get_enum_values();
    def->enum_values.push_back("zigzag");
    def->enum_values.push_back("cross");
    def->enum_values.push_back("dynamic");
    def->enum_labels.push_back(L("Zig-Zag"));
    def->enum_labels.push_back(L("Cross"));
    def->enum_labels.push_back(L("Dynamic"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SLAPillarConnectionMode>(slapcmDynamic));

    def = this->add("support_buildplate_only", coBool);
    def->label = L("Support on build plate only");
    def->category = L("Supports");
    def->tooltip = L("Only create support if it lies on a build plate. Don't create support on a print.");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_pillar_widening_factor", coFloat);
    def->label = L("Pillar widening factor");
    def->category = L("Supports");
    def->tooltip = L("Merging bridges or pillars into another pillars can "
                     "increase the radius. Zero means no increase, one means "
                     "full increase.");
    def->min = 0;
    def->max = 1;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("support_base_diameter", coFloat);
    def->label = L("Support base diameter");
    def->category = L("Supports");
    def->tooltip = L("Diameter in mm of the pillar base");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 30;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(4.0));

    def = this->add("support_base_height", coFloat);
    def->label = L("Support base height");
    def->category = L("Supports");
    def->tooltip = L("The height of the pillar base cone");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("support_base_safety_distance", coFloat);
    def->label = L("Support base safety distance");
    def->category = L("Supports");
    def->tooltip  = L(
        "The minimum distance of the pillar base from the model in mm. "
        "Makes sense in zero elevation mode where a gap according "
        "to this parameter is inserted between the model and the pad.");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 10;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("support_critical_angle", coFloat);
    def->label = L("Critical angle");
    def->category = L("Supports");
    def->tooltip = L("The default angle for connecting support sticks and junctions.");
    def->sidetext = L("°");
    def->min = 0;
    def->max = 90;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(45));

    def = this->add("support_max_bridge_length", coFloat);
    def->label = L("Max bridge length");
    def->category = L("Supports");
    def->tooltip = L("The max length of a bridge");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(15.0));

    def = this->add("support_max_pillar_link_distance", coFloat);
    def->label = L("Max pillar linking distance");
    def->category = L("Supports");
    def->tooltip = L("The max distance of two pillars to get linked with each other."
                     " A zero value will prohibit pillar cascading.");
    def->sidetext = L("mm");
    def->min = 0;   // 0 means no linking
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10.0));

    def = this->add("support_object_elevation", coFloat);
    def->label = L("Object elevation");
    def->category = L("Supports");
    def->tooltip = L("How much the supports should lift up the supported object. "
                     "If \"Pad around object\" is enabled, this value is ignored.");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 150; // This is the max height of print on SL1
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.0));

    def = this->add("support_points_density_relative", coInt);
    def->label = L("Support points density");
    def->category = L("Supports");
    def->tooltip = L("This is a relative measure of support points density.");
    def->sidetext = L("%");
    def->min = 0;
    def->set_default_value(new ConfigOptionInt(100));

    def = this->add("support_points_minimal_distance", coFloat);
    def->label = L("Minimal distance of the support points");
    def->category = L("Supports");
    def->tooltip = L("No support points will be placed closer than this threshold.");
    def->sidetext = L("mm");
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(1.f));

    def = this->add("pad_enable", coBool);
    def->label = L("Use pad");
    def->category = L("Pad");
    def->tooltip = L("Add a pad underneath the supported model");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("pad_wall_thickness", coFloat);
    def->label = L("Pad wall thickness");
    def->category = L("Pad");
     def->tooltip = L("The thickness of the pad and its optional cavity walls.");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 30;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(2.0));

    def = this->add("pad_wall_height", coFloat);
    def->label = L("Pad wall height");
    def->tooltip = L("Defines the pad cavity depth. Set to zero to disable the cavity. "
                     "Be careful when enabling this feature, as some resins may "
                     "produce an extreme suction effect inside the cavity, "
                     "which makes peeling the print off the vat foil difficult.");
    def->category = L("Pad");
//     def->tooltip = L("");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 30;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.));
    
    def = this->add("pad_brim_size", coFloat);
    def->label = L("Pad brim size");
    def->tooltip = L("How far should the pad extend around the contained geometry");
    def->category = L("Pad");
    //     def->tooltip = L("");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 30;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.6));

    def = this->add("pad_max_merge_distance", coFloat);
    def->label = L("Max merge distance");
    def->category = L("Pad");
     def->tooltip = L("Some objects can get along with a few smaller pads "
                      "instead of a single big one. This parameter defines "
                      "how far the center of two smaller pads should be. If they"
                      "are closer, they will get merged into one pad.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(50.0));

    // This is disabled on the UI. I hope it will never be enabled.
//    def = this->add("pad_edge_radius", coFloat);
//    def->label = L("Pad edge radius");
//    def->category = L("Pad");
////     def->tooltip = L("");
//    def->sidetext = L("mm");
//    def->min = 0;
//    def->mode = comAdvanced;
//    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("pad_wall_slope", coFloat);
    def->label = L("Pad wall slope");
    def->category = L("Pad");
    def->tooltip = L("The slope of the pad wall relative to the bed plane. "
                     "90 degrees means straight walls.");
    def->sidetext = L("°");
    def->min = 45;
    def->max = 90;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(90.0));

    def = this->add("pad_around_object", coBool);
    def->label = L("Pad around object");
    def->category = L("Pad");
    def->tooltip = L("Create pad around object and ignore the support elevation");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));
    
    def = this->add("pad_around_object_everywhere", coBool);
    def->label = L("Pad around object everywhere");
    def->category = L("Pad");
    def->tooltip = L("Force pad around object everywhere");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("pad_object_gap", coFloat);
    def->label = L("Pad object gap");
    def->category = L("Pad");
    def->tooltip  = L("The gap between the object bottom and the generated "
                      "pad in zero elevation mode.");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 10;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("pad_object_connector_stride", coFloat);
    def->label = L("Pad object connector stride");
    def->category = L("Pad");
    def->tooltip = L("Distance between two connector sticks which connect the object and the generated pad.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(10));

    def = this->add("pad_object_connector_width", coFloat);
    def->label = L("Pad object connector width");
    def->category = L("Pad");
    def->tooltip  = L("Width of the connector sticks which connect the object and the generated pad.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.5));

    def = this->add("pad_object_connector_penetration", coFloat);
    def->label = L("Pad object connector penetration");
    def->category = L("Pad");
    def->tooltip  = L(
        "How much should the tiny connectors penetrate into the model body.");
    def->sidetext = L("mm");
    def->min = 0;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.3));
    
    def = this->add("hollowing_enable", coBool);
    def->label = L("Enable hollowing");
    def->category = L("Hollowing");
    def->tooltip = L("Hollow out a model to have an empty interior");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));
    
    def = this->add("hollowing_min_thickness", coFloat);
    def->label = L("Wall thickness");
    def->category = L("Hollowing");
    def->tooltip  = L("Minimum wall thickness of a hollowed model.");
    def->sidetext = L("mm");
    def->min = 1;
    def->max = 10;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(3.));
    
    def = this->add("hollowing_quality", coFloat);
    def->label = L("Accuracy");
    def->category = L("Hollowing");
    def->tooltip  = L("Performance vs accuracy of calculation. Lower values may produce unwanted artifacts.");
    def->min = 0;
    def->max = 1;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(0.5));
    
    def = this->add("hollowing_closing_distance", coFloat);
    def->label = L("Closing distance");
    def->category = L("Hollowing");
    def->tooltip  = L(
        "Hollowing is done in two steps: first, an imaginary interior is "
        "calculated deeper (offset plus the closing distance) in the object and "
        "then it's inflated back to the specified offset. A greater closing "
        "distance makes the interior more rounded. At zero, the interior will "
        "resemble the exterior the most.");
    def->sidetext = L("mm");
    def->min = 0;
    def->max = 10;
    def->mode = comExpert;
    def->set_default_value(new ConfigOptionFloat(2.0));

    def = this->add("material_print_speed", coEnum);
    def->label = L("Print speed");
    def->tooltip = L(
        "A slower printing profile might be necessary when using materials with higher viscosity "
        "or with some hollowed parts. It slows down the tilt movement and adds a delay before exposure.");
    def->enum_keys_map = &ConfigOptionEnum<SLAMaterialSpeed>::get_enum_values();
    def->enum_values.push_back("slow");
    def->enum_values.push_back("fast");
    def->enum_labels.push_back(L("Slow"));
    def->enum_labels.push_back(L("Fast"));
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SLAMaterialSpeed>(slamsSlow));
}

void PrintConfigDef::handle_legacy(t_config_option_key &opt_key, std::string &value)
{
    // handle legacy options
    if (opt_key == "extrusion_width_ratio" || opt_key == "bottom_layer_speed_ratio"
        || opt_key == "first_layer_height_ratio") {
        boost::replace_first(opt_key, "_ratio", "");
        if (opt_key == "bottom_layer_speed") opt_key = "first_layer_speed";
        try {
            float v = boost::lexical_cast<float>(value);
            if (v != 0)
                value = boost::lexical_cast<std::string>(v*100) + "%";
        } catch (boost::bad_lexical_cast &) {
            value = "0";
        }
    } else if (opt_key == "gcode_flavor") {
        if (value == "makerbot")
            value = "makerware";
        else if (value == "marlinfirmware")
            // the "new" marlin firmware flavor used to be called "marlinfirmware" for some time during PrusaSlicer 2.4.0-alpha development.
            value = "marlin2";
    } else if (opt_key == "fill_density" && value.find("%") == std::string::npos) {
        try {
            // fill_density was turned into a percent value
            float v = boost::lexical_cast<float>(value);
            value = boost::lexical_cast<std::string>(v*100) + "%";
        } catch (boost::bad_lexical_cast &) {}
    } else if (opt_key == "randomize_start" && value == "1") {
        opt_key = "seam_position";
        value = "random";
    } else if (opt_key == "bed_size" && !value.empty()) {
        opt_key = "bed_shape";
        ConfigOptionPoint p;
        p.deserialize(value, ForwardCompatibilitySubstitutionRule::Disable);
        std::ostringstream oss;
        oss << "0x0," << p.value(0) << "x0," << p.value(0) << "x" << p.value(1) << ",0x" << p.value(1);
        value = oss.str();
    } else if ((opt_key == "perimeter_acceleration" && value == "25")
        || (opt_key == "infill_acceleration" && value == "50")) {
        /*  For historical reasons, the world's full of configs having these very low values;
            to avoid unexpected behavior we need to ignore them. Banning these two hard-coded
            values is a dirty hack and will need to be removed sometime in the future, but it
            will avoid lots of complaints for now. */
        value = "0";
    } else if (opt_key == "support_material_pattern" && value == "pillars") {
        // Slic3r PE does not support the pillars. They never worked well.
        value = "rectilinear";
    } else if (opt_key == "skirt_height" && value == "-1") {
    	// PrusaSlicer no more accepts skirt_height == -1 to print a draft shield to the top of the highest object.
        // A new "draft_shield" enum config value is used instead.
    	opt_key = "draft_shield";
        value = "enabled";
    } else if (opt_key == "draft_shield" && (value == "1" || value == "0")) {
        // draft_shield used to be a bool, it was turned into an enum in PrusaSlicer 2.4.0.
        value = value == "1" ? "enabled" : "disabled";
    } else if (opt_key == "octoprint_host") {
        opt_key = "print_host";
    } else if (opt_key == "octoprint_cafile") {
        opt_key = "printhost_cafile";
    } else if (opt_key == "octoprint_apikey") {
        opt_key = "printhost_apikey";
    } else if (opt_key == "preset_name") {
        opt_key = "preset_names";
    } /*else if (opt_key == "material_correction" || opt_key == "relative_correction") {
        ConfigOptionFloats p;
        p.deserialize(value);

        if (p.values.size() < 3) {
            double firstval = p.values.front();
            p.values.emplace(p.values.begin(), firstval);
            value = p.serialize();
        }
    }*/

    // Ignore the following obsolete configuration keys:
    static std::set<std::string> ignore = {
        "duplicate_x", "duplicate_y", "gcode_arcs", "multiply_x", "multiply_y",
        "support_material_tool", "acceleration", "adjust_overhang_flow",
        "standby_temperature", "scale", "rotate", "duplicate", "duplicate_grid",
        "start_perimeters_at_concave_points", "start_perimeters_at_non_overhang", "randomize_start",
        "seal_position", "vibration_limit", "bed_size",
        "print_center", "g0", "threads", "pressure_advance", "wipe_tower_per_color_wipe"
#ifndef HAS_PRESSURE_EQUALIZER
        , "max_volumetric_extrusion_rate_slope_positive", "max_volumetric_extrusion_rate_slope_negative",
#endif /* HAS_PRESSURE_EQUALIZER */
        "serial_port", "serial_speed",
        // Introduced in some PrusaSlicer 2.3.1 alpha, later renamed or removed.
        "fuzzy_skin_perimeter_mode", "fuzzy_skin_shape",
    };

    // In PrusaSlicer 2.3.0-alpha0 the "monotonous" infill was introduced, which was later renamed to "monotonic".
    if (value == "monotonous" && (opt_key == "top_fill_pattern" || opt_key == "bottom_fill_pattern" || opt_key == "fill_pattern"))
        value = "monotonic";

    if (ignore.find(opt_key) != ignore.end()) {
        opt_key = "";
        return;
    }

    if (! print_config_def.has(opt_key)) {
        opt_key = "";
        return;
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
        auto ecr_opt = cfg.option<ConfigOptionFloat>("extruder_clearance_radius");
        auto dd_opt  = cfg.option<ConfigOptionFloat>("duplicate_distance");
        auto co_opt  = cfg.option<ConfigOptionBool>("complete_objects");

        if (!ecr_opt || !dd_opt || !co_opt) 
            ret = 0.;
        else {
            // min object distance is max(duplicate_distance, clearance_radius)
            ret = (co_opt->value && ecr_opt->value > dd_opt->value) ?
                      ecr_opt->value : dd_opt->value;
        }
    }

    return ret;
}

void DynamicPrintConfig::normalize_fdm()
{
    if (this->has("extruder")) {
        int extruder = this->option("extruder")->getInt();
        this->erase("extruder");
        if (extruder != 0) {
            if (!this->has("infill_extruder"))
                this->option("infill_extruder", true)->setInt(extruder);
            if (!this->has("perimeter_extruder"))
                this->option("perimeter_extruder", true)->setInt(extruder);
            // Don't propagate the current extruder to support.
            // For non-soluble supports, the default "0" extruder means to use the active extruder,
            // for soluble supports one certainly does not want to set the extruder to non-soluble.
            // if (!this->has("support_material_extruder"))
            //     this->option("support_material_extruder", true)->setInt(extruder);
            // if (!this->has("support_material_interface_extruder"))
            //     this->option("support_material_interface_extruder", true)->setInt(extruder);
        }
    }

    if (!this->has("solid_infill_extruder") && this->has("infill_extruder"))
        this->option("solid_infill_extruder", true)->setInt(this->option("infill_extruder")->getInt());

    if (this->has("spiral_vase") && this->opt<ConfigOptionBool>("spiral_vase", true)->value) {
        {
            // this should be actually done only on the spiral layers instead of all
            auto* opt = this->opt<ConfigOptionBools>("retract_layer_change", true);
            opt->values.assign(opt->values.size(), false);  // set all values to false
            // Disable retract on layer change also for filament overrides.
            auto* opt_n = this->opt<ConfigOptionBoolsNullable>("filament_retract_layer_change", true);
            opt_n->values.assign(opt_n->values.size(), false);  // Set all values to false.
        }
        {
            this->opt<ConfigOptionInt>("perimeters", true)->value       = 1;
            this->opt<ConfigOptionInt>("top_solid_layers", true)->value = 0;
            this->opt<ConfigOptionPercent>("fill_density", true)->value = 0;
        }
    }

    if (auto *opt_gcode_resolution = this->opt<ConfigOptionFloat>("gcode_resolution", false); opt_gcode_resolution)
        // Resolution will be above 1um.
        opt_gcode_resolution->value = std::max(opt_gcode_resolution->value, 0.001);
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

std::string DynamicPrintConfig::validate()
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
        return Slic3r::validate(fpc);
    }
    default:
        //FIXME no validation on SLA data?
        return std::string();
    }
}

//FIXME localize this function.
std::string validate(const FullPrintConfig &cfg)
{
    // --layer-height
    if (cfg.get_abs_value("layer_height") <= 0)
        return "Invalid value for --layer-height";
    if (fabs(fmod(cfg.get_abs_value("layer_height"), SCALING_FACTOR)) > 1e-4)
        return "--layer-height must be a multiple of print resolution";

    // --first-layer-height
    if (cfg.first_layer_height.value <= 0)
        return "Invalid value for --first-layer-height";

    // --filament-diameter
    for (double fd : cfg.filament_diameter.values)
        if (fd < 1)
            return "Invalid value for --filament-diameter";

    // --nozzle-diameter
    for (double nd : cfg.nozzle_diameter.values)
        if (nd < 0.005)
            return "Invalid value for --nozzle-diameter";

    // --perimeters
    if (cfg.perimeters.value < 0)
        return "Invalid value for --perimeters";

    // --solid-layers
    if (cfg.top_solid_layers < 0)
        return "Invalid value for --top-solid-layers";
    if (cfg.bottom_solid_layers < 0)
        return "Invalid value for --bottom-solid-layers";

    if (cfg.use_firmware_retraction.value &&
        cfg.gcode_flavor.value != gcfSmoothie &&
        cfg.gcode_flavor.value != gcfRepRapSprinter &&
        cfg.gcode_flavor.value != gcfRepRapFirmware &&
        cfg.gcode_flavor.value != gcfMarlinLegacy &&
        cfg.gcode_flavor.value != gcfMarlinFirmware &&
        cfg.gcode_flavor.value != gcfMachinekit &&
        cfg.gcode_flavor.value != gcfRepetier)
        return "--use-firmware-retraction is only supported by Marlin, Smoothie, RepRapFirmware, Repetier and Machinekit firmware";

    if (cfg.use_firmware_retraction.value)
        for (unsigned char wipe : cfg.wipe.values)
             if (wipe)
                return "--use-firmware-retraction is not compatible with --wipe";

    // --gcode-flavor
    if (! print_config_def.get("gcode_flavor")->has_enum_value(cfg.gcode_flavor.serialize()))
        return "Invalid value for --gcode-flavor";

    // --fill-pattern
    if (! print_config_def.get("fill_pattern")->has_enum_value(cfg.fill_pattern.serialize()))
        return "Invalid value for --fill-pattern";

    // --top-fill-pattern
    if (! print_config_def.get("top_fill_pattern")->has_enum_value(cfg.top_fill_pattern.serialize()))
        return "Invalid value for --top-fill-pattern";

    // --bottom-fill-pattern
    if (! print_config_def.get("bottom_fill_pattern")->has_enum_value(cfg.bottom_fill_pattern.serialize()))
        return "Invalid value for --bottom-fill-pattern";

    // --fill-density
    if (fabs(cfg.fill_density.value - 100.) < EPSILON &&
        ! print_config_def.get("top_fill_pattern")->has_enum_value(cfg.fill_pattern.serialize()))
        return "The selected fill pattern is not supposed to work at 100% density";

    // --infill-every-layers
    if (cfg.infill_every_layers < 1)
        return "Invalid value for --infill-every-layers";

    // --skirt-height
    if (cfg.skirt_height < 0)
        return "Invalid value for --skirt-height";

    // --bridge-flow-ratio
    if (cfg.bridge_flow_ratio <= 0)
        return "Invalid value for --bridge-flow-ratio";

    // extruder clearance
    if (cfg.extruder_clearance_radius <= 0)
        return "Invalid value for --extruder-clearance-radius";
    if (cfg.extruder_clearance_height <= 0)
        return "Invalid value for --extruder-clearance-height";

    // --extrusion-multiplier
    for (double em : cfg.extrusion_multiplier.values)
        if (em <= 0)
            return "Invalid value for --extrusion-multiplier";

    // The following test was commented out after 482841b, see also https://github.com/prusa3d/PrusaSlicer/pull/6743.
    // The backend should now handle this case correctly. I.e., zero default_acceleration behaves as if all others
    // were zero too. This is now consistent with what the UI said would happen.
    // The UI already grays the fields out, there is no more reason to reject it here. This function validates the
    // config before exporting, leaving this check in would mean that config would be rejected before export
    // (although both the UI and the backend handle it).
    // --default-acceleration
    //if ((cfg.perimeter_acceleration != 0. || cfg.infill_acceleration != 0. || cfg.bridge_acceleration != 0. || cfg.first_layer_acceleration != 0.) &&
    //    cfg.default_acceleration == 0.)
    //    return "Invalid zero value for --default-acceleration when using other acceleration settings";

    // --spiral-vase
    if (cfg.spiral_vase) {
        // Note that we might want to have more than one perimeter on the bottom
        // solid layers.
        if (cfg.perimeters > 1)
            return "Can't make more than one perimeter when spiral vase mode is enabled";
        else if (cfg.perimeters < 1)
            return "Can't make less than one perimeter when spiral vase mode is enabled";
        if (cfg.fill_density > 0)
            return "Spiral vase mode can only print hollow objects, so you need to set Fill density to 0";
        if (cfg.top_solid_layers > 0)
            return "Spiral vase mode is not compatible with top solid layers";
        if (cfg.support_material || cfg.support_material_enforce_layers > 0)
            return "Spiral vase mode is not compatible with support material";
    }

    // extrusion widths
    {
        double max_nozzle_diameter = 0.;
        for (double dmr : cfg.nozzle_diameter.values)
            max_nozzle_diameter = std::max(max_nozzle_diameter, dmr);
        const char *widths[] = { "external_perimeter", "perimeter", "infill", "solid_infill", "top_infill", "support_material", "first_layer" };
        for (size_t i = 0; i < sizeof(widths) / sizeof(widths[i]); ++ i) {
            std::string key(widths[i]);
            key += "_extrusion_width";
            if (cfg.get_abs_value(key, max_nozzle_diameter) > 10. * max_nozzle_diameter)
                return std::string("Invalid extrusion width (too large): ") + key;
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
        if (out_of_range)
            return std::string("Value out of range: " + opt_key);
    }

    // The configuration is valid.
    return "";
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

CLIActionsConfigDef::CLIActionsConfigDef()
{
    ConfigOptionDef* def;

    // Actions:
    def = this->add("export_obj", coBool);
    def->label = L("Export OBJ");
    def->tooltip = L("Export the model(s) as OBJ.");
    def->set_default_value(new ConfigOptionBool(false));

/*
    def = this->add("export_svg", coBool);
    def->label = L("Export SVG");
    def->tooltip = L("Slice the model and export solid slices as SVG.");
    def->set_default_value(new ConfigOptionBool(false));
*/

    def = this->add("export_sla", coBool);
    def->label = L("Export SLA");
    def->tooltip = L("Slice the model and export SLA printing layers as PNG.");
    def->cli = "export-sla|sla";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("export_3mf", coBool);
    def->label = L("Export 3MF");
    def->tooltip = L("Export the model(s) as 3MF.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("export_amf", coBool);
    def->label = L("Export AMF");
    def->tooltip = L("Export the model(s) as AMF.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("export_stl", coBool);
    def->label = L("Export STL");
    def->tooltip = L("Export the model(s) as STL.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("export_gcode", coBool);
    def->label = L("Export G-code");
    def->tooltip = L("Slice the model and export toolpaths as G-code.");
    def->cli = "export-gcode|gcode|g";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("gcodeviewer", coBool);
    def->label = L("G-code viewer");
    def->tooltip = L("Visualize an already sliced and saved G-code");
    def->cli = "gcodeviewer";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("slice", coBool);
    def->label = L("Slice");
    def->tooltip = L("Slice the model as FFF or SLA based on the printer_technology configuration value.");
    def->cli = "slice|s";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("help", coBool);
    def->label = L("Help");
    def->tooltip = L("Show this help.");
    def->cli = "help|h";
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("help_fff", coBool);
    def->label = L("Help (FFF options)");
    def->tooltip = L("Show the full list of print/G-code configuration options.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("help_sla", coBool);
    def->label = L("Help (SLA options)");
    def->tooltip = L("Show the full list of SLA print configuration options.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("info", coBool);
    def->label = L("Output Model Info");
    def->tooltip = L("Write information about the model to the console.");
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("save", coString);
    def->label = L("Save config file");
    def->tooltip = L("Save configuration to the specified file.");
    def->set_default_value(new ConfigOptionString());
}

CLITransformConfigDef::CLITransformConfigDef()
{
    ConfigOptionDef* def;

    // Transform options:
    def = this->add("align_xy", coPoint);
    def->label = L("Align XY");
    def->tooltip = L("Align the model to the given point.");
    def->set_default_value(new ConfigOptionPoint(Vec2d(100,100)));

    def = this->add("cut", coFloat);
    def->label = L("Cut");
    def->tooltip = L("Cut model at the given Z.");
    def->set_default_value(new ConfigOptionFloat(0));

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

    def = this->add("center", coPoint);
    def->label = L("Center");
    def->tooltip = L("Center the print around the given center.");
    def->set_default_value(new ConfigOptionPoint(Vec2d(100,100)));

    def = this->add("dont_arrange", coBool);
    def->label = L("Don't arrange");
    def->tooltip = L("Do not rearrange the given models before merging and keep their original XY coordinates.");

    def = this->add("ensure_on_bed", coBool);
    def->label = L("Ensure on bed");
    def->tooltip = L("Lift the object above the bed when it is partially below. Enabled by default, use --no-ensure-on-bed to disable.");
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("duplicate", coInt);
    def->label = L("Duplicate");
    def->tooltip =L("Multiply copies by this factor.");
    def->min = 1;

    def = this->add("duplicate_grid", coPoint);
    def->label = L("Duplicate by grid");
    def->tooltip = L("Multiply copies by creating a grid.");

    def = this->add("merge", coBool);
    def->label = L("Merge");
    def->tooltip = L("Arrange the supplied models in a plate and merge them in a single model in order to perform actions once.");
    def->cli = "merge|m";

    def = this->add("repair", coBool);
    def->label = L("Repair");
    def->tooltip = L("Try to repair any non-manifold meshes (this option is implicitly added whenever we need to slice the model to perform the requested action).");

    def = this->add("rotate", coFloat);
    def->label = L("Rotate");
    def->tooltip = L("Rotation angle around the Z axis in degrees.");
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("rotate_x", coFloat);
    def->label = L("Rotate around X");
    def->tooltip = L("Rotation angle around the X axis in degrees.");
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("rotate_y", coFloat);
    def->label = L("Rotate around Y");
    def->tooltip = L("Rotation angle around the Y axis in degrees.");
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("scale", coFloatOrPercent);
    def->label = L("Scale");
    def->tooltip = L("Scaling factor or percentage.");
    def->set_default_value(new ConfigOptionFloatOrPercent(1, false));

    def = this->add("split", coBool);
    def->label = L("Split");
    def->tooltip = L("Detect unconnected parts in the given model(s) and split them into separate objects.");

    def = this->add("scale_to_fit", coPoint3);
    def->label = L("Scale to Fit");
    def->tooltip = L("Scale to fit the given volume.");
    def->set_default_value(new ConfigOptionPoint3(Vec3d(0,0,0)));
}

CLIMiscConfigDef::CLIMiscConfigDef()
{
    ConfigOptionDef* def;

    def = this->add("ignore_nonexistent_config", coBool);
    def->label = L("Ignore non-existent config files");
    def->tooltip = L("Do not fail if a file supplied to --load does not exist.");

    def = this->add("config_compatibility", coEnum);
    def->label = L("Forward-compatibility rule when loading configurations from config files and project files (3MF, AMF).");
    def->tooltip = L("This version of PrusaSlicer may not understand configurations produced by the newest PrusaSlicer versions. "
                     "For example, newer PrusaSlicer may extend the list of supported firmware flavors. One may decide to "
                     "bail out or to substitute an unknown value with a default silently or verbosely.");
    def->enum_keys_map = &ConfigOptionEnum<ForwardCompatibilitySubstitutionRule>::get_enum_values();
    def->enum_values.push_back("disable");
    def->enum_values.push_back("enable");
    def->enum_values.push_back("enable_silent");
    def->enum_labels.push_back(L("Bail out on unknown configuration values"));
    def->enum_labels.push_back(L("Enable reading unknown configuration values by verbosely substituting them with defaults."));
    def->enum_labels.push_back(L("Enable reading unknown configuration values by silently substituting them with defaults."));
    def->set_default_value(new ConfigOptionEnum<ForwardCompatibilitySubstitutionRule>(ForwardCompatibilitySubstitutionRule::Enable));

    def = this->add("load", coStrings);
    def->label = L("Load config file");
    def->tooltip = L("Load configuration from the specified file. It can be used more than once to load options from multiple files.");

    def = this->add("output", coString);
    def->label = L("Output File");
    def->tooltip = L("The file where the output will be written (if not specified, it will be based on the input file).");
    def->cli = "output|o";

    def = this->add("single_instance", coBool);
    def->label = L("Single instance mode");
    def->tooltip = L("If enabled, the command line arguments are sent to an existing instance of GUI PrusaSlicer, "
                     "or an existing PrusaSlicer window is activated. "
                     "Overrides the \"single_instance\" configuration value from application preferences.");

/*
    def = this->add("autosave", coString);
    def->label = L("Autosave");
    def->tooltip = L("Automatically export current configuration to the specified file.");
*/

    def = this->add("datadir", coString);
    def->label = L("Data directory");
    def->tooltip = L("Load and store settings at the given directory. This is useful for maintaining different profiles or including configurations from a network storage.");

    def = this->add("loglevel", coInt);
    def->label = L("Logging level");
    def->tooltip = L("Sets logging sensitivity. 0:fatal, 1:error, 2:warning, 3:info, 4:debug, 5:trace\n"
                     "For example. loglevel=2 logs fatal, error and warning level messages.");
    def->min = 0;

#if (defined(_MSC_VER) || defined(__MINGW32__)) && defined(SLIC3R_GUI)
    def = this->add("sw_renderer", coBool);
    def->label = L("Render with a software renderer");
    def->tooltip = L("Render with a software renderer. The bundled MESA software renderer is loaded instead of the default OpenGL driver.");
    def->min = 0;
#endif /* _MSC_VER */
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
    const auto *bed_shape_opt = config.opt<ConfigOptionPoints>("bed_shape");
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
    return to_points(cfg.bed_shape.values);
}

Points get_bed_shape(const SLAPrinterConfig &cfg) { return to_points(cfg.bed_shape.values); }

} // namespace Slic3r

#include <cereal/types/polymorphic.hpp>
CEREAL_REGISTER_TYPE(Slic3r::DynamicPrintConfig)
CEREAL_REGISTER_POLYMORPHIC_RELATION(Slic3r::DynamicConfig, Slic3r::DynamicPrintConfig)
