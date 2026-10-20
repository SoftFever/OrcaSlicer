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

//! macro used to mark string used at localization,
//! return same string
#define L(s) (s)


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
    //def->tooltip = L("Printer technology.");
    def->enum_keys_map = &ConfigOptionEnum<PrinterTechnology>::get_enum_values();
    def->enum_values.push_back("FFF");
    def->enum_values.push_back("SLA");
    def->set_default_value(new ConfigOptionEnum<PrinterTechnology>(ptFFF));

    def = this->add("printable_area", coPoints);
    def->label = L("Printable area");
    //BBS
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPoints{ Vec2d(0, 0), Vec2d(200, 0), Vec2d(200, 200), Vec2d(0, 200) });

    def = this->add("extruder_printable_area", coPointsGroups);
    def->label = L("Extruder printable area");
    def->mode = comAdvanced;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionPointsGroups{});

    def           = this->add("support_parallel_printheads", coBool);
    def->label    = L("Support parallel printheads");
    def->tooltip  = L("Enable printer settings for machines that can use multiple printheads in parallel.");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionBool{false});

    def           = this->add("parallel_printheads_count", coInt);
    def->label    = L("Parallel printheads count");
    def->tooltip  = L("Set the number of parallel printheads for machines like OrangeStorm Giga printer.");
    def->mode     = comAdvanced;
    def->min      = 1;
    def->max      = 4;
    def->set_default_value(new ConfigOptionInt{1});

    def           = this->add("parallel_printheads_bed_exclude_areas", coStrings);
    def->label    = L("Parallel printheads bed exclude areas");
    def->tooltip  = L("Ordered list of bed exclude areas by parallel printhead count. Item 1 applies to one printhead, item 2 to two printheads, and so on. Leave an item empty for no excluded area.");
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionStrings());

    //BBS: add "bed_exclude_area"
    def = this->add("bed_exclude_area", coPoints);
    def->label = L("Excluded bed area");
    def->tooltip = L("Unprintable area in XY plane. For example, X1 Series printers use the front left corner to cut filament during filament change. "
        "The area is expressed as polygon by points in following format: \"XxY, XxY, ...\"");
    def->mode = comAdvanced;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionPoints{ Vec2d(0, 0) });

    def = this->add("bed_custom_texture", coString);
    def->label = L("Bed custom texture");
    def->mode = comAdvanced;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionString());

    def = this->add("bed_custom_model", coString);
    def->label = L("Bed custom model");
    def->mode = comAdvanced;
    def->gui_type = ConfigOptionDef::GUIType::one_string;
    def->set_default_value(new ConfigOptionString());

    def = this->add("elefant_foot_compensation", coFloat);
    def->label = L("Elephant foot compensation");
    def->category = L("Quality");
    def->tooltip = L("This shrinks the first layer on the build plate to compensate for elephant foot effect.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
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

    def           = this->add("elefant_foot_layers_density", coPercent);
    def->label    = L("Elephant foot layers density");
    def->category = L("Quality");
    def->tooltip  = L("Density of internal solid infill for Elephant foot layers compensation.\n"
                      "The initial value for the second layer is set.\n"
                      "Subsequent layers become linearly denser by the height specified in elefant_foot_compensation_layers.");
    def->sidetext = "%";
    def->min      = 50;
    def->max      = 100;
    def->mode     = comExpert;
    def->set_default_value(new ConfigOptionPercent(100));

    def = this->add("layer_height", coFloat);
    def->label = L("Layer height");
    def->category = L("Quality");
    def->tooltip = L("This is the height for each layer. Smaller layer heights give greater accuracy but longer printing time.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(INITIAL_LAYER_HEIGHT));

    def = this->add("printable_height", coFloat);
    def->label = L("Printable height");
    def->tooltip = L("This is the maximum printable height which is limited by the height of the build area.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->max = 214700;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(100.0));

    def           = this->add("extruder_printable_height", coFloats);
    def->label    = L("Extruder printable height");
    def->tooltip  = L("Maximum printable height of this extruder which is limited by mechanism of printer.");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min      = 0;
    def->max      = 1000;
    def->mode     = comAdvanced;
    def->set_default_value(new ConfigOptionFloatsNullable{0});

    def = this->add("preferred_orientation", coFloat);
    def->label = L("Preferred orientation");
    def->tooltip = L("Automatically orient STL files on the Z axis upon initial import.");
    def->sidetext = u8"°";	// degrees, don't need translation
    def->max = 360;
    def->min = -360;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    // Options used by physical printers

    def = this->add("preset_names", coStrings);
    def->label = L("Printer preset names");
    //def->tooltip = L("Names of presets related to the physical printer.");
    def->mode = comDevelop;
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("bbl_use_printhost", coBool);
    def->label = L("Use 3rd-party print host");
    def->tooltip = L("Allow controlling BambuLab's printer through 3rd party print hosts.");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("use_3mf", coBool);
    def->label = L("Use 3MF instead of G-code");
    def->tooltip = L("Enable this if the printer accepts a 3MF file as the print job. When enabled, Orca Slicer "
                     "sends the sliced file as a .gcode.3mf, instead of a plain .gcode file.");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("printer_agent", coString);
    def->label = L("Printer Agent");
    def->tooltip = L("Select the network agent implementation for printer communication.");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    // Plugin-backed (see ConfigOptionDef::is_plugin_backed), but edited through the Choice widget above
    // rather than a plugin_picker field.
    def->plugin_type = "printer-connection";
    def->set_default_value(new ConfigOptionString(""));

    def = this->add("print_host", coString);
    def->label = L("Hostname, IP or URL");
    // The URL is an example of the format to type, not a link, so it stays in the text.
    // xgettext:no-url-check
    def->tooltip = L("Orca Slicer can upload G-code files to a printer host. This field should contain "
        "the hostname, IP address or URL of the printer host instance. "
        "Print host behind HAProxy with basic auth enabled can be accessed by putting the user name and password into the URL "
        "in the following format: https://username:password@your-octopi-address/");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString());

    def = this->add("print_host_webui", coString);
    def->label = L("Device UI");
    def->tooltip = L("Specify the URL of your device user interface if it's not same as print_host.");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString());

    def = this->add("printhost_apikey", coString);
    def->label = L("API Key / Password");
    def->tooltip = L("Orca Slicer can upload G-code files to a printer host. This field should contain "
        "the API Key or the password required for authentication.");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString());

    def = this->add("flashforge_serial_number", coString);
    def->label = L("Serial Number");
    def->tooltip = L("Flashforge local API requires the printer serial number.");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString());

    def = this->add("printhost_port", coString);
    def->label = L("Printer");
    def->tooltip = L("Name of the printer.");
    def->gui_type = ConfigOptionDef::GUIType::select_open;
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString());

    def = this->add("printhost_cafile", coString);
    def->label = L("HTTPS CA File");
    def->tooltip = L("Custom CA certificate file can be specified for HTTPS OctoPrint connections, in crt/pem format. "
        "If left blank, the default OS CA certificate repository is used.");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString());

    // Options used by physical printers

    def = this->add("printhost_user", coString);
    def->label = L("User");
    //def->tooltip = L("");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString());

    def = this->add("printhost_password", coString);
    def->label = L("Password");
    //def->tooltip = L("");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionString());

    // Only available on Windows.
    def = this->add("printhost_ssl_ignore_revoke", coBool);
    def->label = L("Ignore HTTPS certificate revocation checks");
    def->tooltip = L("Ignore HTTPS certificate revocation checks in the case of missing or offline distribution points. One may want to enable this option for self signed certificates if connection fails.");
    def->mode = comAdvanced;
    def->cli = ConfigOptionDef::nocli;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("preset_names", coStrings);
    def->label = L("Printer preset names");
    def->tooltip = L("Names of presets related to the physical printer.");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionStrings());

    def = this->add("printhost_authorization_type", coEnum);
    def->label = L("Authorization Type");
    //def->tooltip = L("");
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

    // One key per preset type (Preset::plugin_overrides_key), so the print, printer and filament
    // overrides don't clobber each other when the presets merge into one full config. No handle_legacy
    // migration from the shared "plugin_config_overrides" they replace: it only ever shipped in
    // nightlies. Never a text field — GUIType::plugin_config renders a button opening PluginsConfigDialog.
    for (const char* key : {"print_plugin_config_overrides", "printer_plugin_config_overrides", "filament_plugin_config_overrides"}) {
        def = this->add(key, coString);
        def->label = L("Capabilities");
        def->tooltip = L("Configuration for the plugin capabilities this preset uses, overriding the global "
                         "Capabilities configuration. Stored as a raw JSON array and edited through the dialog "
                         "behind the button, never typed in directly.");
        def->gui_type = ConfigOptionDef::GUIType::plugin_config;
        def->mode = comAdvanced;
        def->cli = ConfigOptionDef::nocli;
        def->set_default_value(new ConfigOptionString(""));
    }
}

void PrintConfigDef::init_fff_params()
{
    ConfigOptionDef* def;
    init_fff_process_quality();
    init_fff_filament_filament();
    init_fff_prepare_plate_settings();
    init_fff_internal_preset_plumbing();
    init_fff_internal_print_sequence();
    init_fff_printer_machine_g_code();
    init_fff_process_strength();
    init_fff_filament_cooling();
    init_fff_process_speed();
    init_fff_process_others();
    init_fff_internal_feature_flags();
    init_fff_internal_compatibility();
    init_fff_process_dependencies();
    init_fff_filament_dependencies();
    init_fff_process_support();
    init_fff_filament_advanced();
    init_fff_internal_extruder_variants();
    init_fff_printer_basic_information();
    init_fff_printer_extruder();
    init_fff_filament_notes();
    init_fff_internal_filament_properties();
    init_fff_internal_filament_map();
    init_fff_filament_multimaterial();
    init_fff_internal_wipe_flush();
    init_fff_printer_multimaterial();
    init_fff_internal_fans();
    init_fff_internal_filament_change();
    init_fff_internal_filament_mix();
    init_fff_filament_overrides();
    init_fff_internal_support_extras();
    init_fff_internal_fault_detection();
    init_fff_process_multimaterial();
    init_fff_internal_calibration();
    init_fff_internal_prime_volume();
    init_fff_printer_motion_ability();
    init_fff_internal_machine_kinematics();
    init_fff_printer_notes();
    init_fff_internal_retraction();
    init_fff_internal_travel();
    init_fff_internal_extruder_identity();
    init_fff_internal_imex_and_timelapse();
    init_fff_internal_ams_drying();
    init_fff_internal_hotend_timing();

}

void PrintConfigDef::init_extruder_option_keys()
{
    // ConfigOptionFloats, ConfigOptionPercents, ConfigOptionBools, ConfigOptionStrings
    m_extruder_option_keys = {
        "default_filament_profile",
        "default_nozzle_volume_type",
        "deretraction_speed",
        "extruder_colour",
        "extruder_offset",
        "extruder_printable_height",
        "extruder_type",
        "long_retractions_when_cut",
        "max_layer_height",
        "min_layer_height",
        "nozzle_diameter",
        "nozzle_flush_dataset",
        "nozzle_type",
        "nozzle_volume",
        "retract_after_wipe",
        "retract_before_wipe",
        "retract_length_toolchange",
        "retract_lift_above",
        "retract_lift_below",
        "retract_lift_enforce",
        "retract_restart_extra",
        "retract_restart_extra_toolchange",
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

    m_extruder_retract_keys = {
        "deretraction_speed",
        "long_retractions_when_cut",
        "retract_after_wipe",
        "retract_before_wipe",
        "retract_length_toolchange",
        "retract_lift_above",
        "retract_lift_below",
        "retract_lift_enforce",
        "retract_restart_extra",
        "retract_restart_extra_toolchange",
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
        "default_filament_profile",
        "deretraction_speed",
        "filament_colour",
        "filament_diameter",
        "filament_retract_length_nc",
        // "filament_seam_gap",
        "long_retractions_when_cut",
        "max_layer_height",
        "min_layer_height",
        "retract_after_wipe",
        "retract_before_wipe",
        "retract_length_toolchange",
        "retract_lift_above",
        "retract_lift_below",
        "retract_lift_enforce",
        "retract_restart_extra",
        "retract_restart_extra_toolchange",
        "retract_when_changing_layer",
        "retraction_distances_when_cut",
        "retraction_length",
        "retraction_minimum_travel",
        "retraction_speed",
        "volumetric_speed_coefficients",
        "wipe",
        "wipe_distance",
        "z_hop",
        "z_hop_types",
    };

    m_filament_retract_keys = {
        "deretraction_speed",
        "filament_retract_length_nc",
        "long_retractions_when_cut",
        "retract_after_wipe",
        "retract_before_wipe",
        "retract_lift_above",
        "retract_lift_below",
        "retract_lift_enforce",
        "retract_restart_extra",
        "retract_when_changing_layer",
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
    //def->label = L("");
    //def->tooltip = L("");
    def->min = 1;
    def->set_default_value(new ConfigOptionFloat(120.));

    def = this->add("display_height", coFloat);
    //def->label = L("");
    //def->tooltip = L("");
    def->min = 1;
    def->set_default_value(new ConfigOptionFloat(68.));

    def = this->add("display_pixels_x", coInt);
    //def->full_label = L("");
    def->label = L_CONTEXT("X", "Axis");
    //def->tooltip = L("");
    def->min = 100;
    def->set_default_value(new ConfigOptionInt(2560));

    def = this->add("display_pixels_y", coInt);
    //def->full_label = L("");
    def->label = L_CONTEXT("Y", "Axis");
    //def->tooltip = L("");
    def->min = 100;
    def->set_default_value(new ConfigOptionInt(1440));

    def = this->add("display_mirror_x", coBool);
    //def->full_label = L("");
    //def->label = L("");
    //def->tooltip = L("");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("display_mirror_y", coBool);
    //def->full_label = L("");
    //def->label = L("");
    //def->tooltip = L("");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("display_orientation", coEnum);
    //def->label = L("");
    //def->tooltip = L("");
    def->enum_keys_map = &ConfigOptionEnum<SLADisplayOrientation>::get_enum_values();
    def->enum_values.push_back("landscape");
    def->enum_values.push_back("portrait");
    def->enum_labels.push_back(" ");
    def->enum_labels.push_back(" ");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionEnum<SLADisplayOrientation>(sladoPortrait));

    def = this->add("fast_tilt_time", coFloat);
    //def->label = L("");
    //def->full_label = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.));

    def = this->add("slow_tilt_time", coFloat);
    //def->label = L("");
    //def->full_label = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(8.));

    def = this->add("area_fill", coFloat);
    //def->label = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(50.));

    def = this->add("relative_correction", coFloats);
    //def->label = L("");
    //def->full_label = L("");
    //def->tooltip = L("");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats( { 1., 1.} ));

    def = this->add("relative_correction_x", coFloat);
    //def->label = L("");
    //def->full_label = L("");
    //def->tooltip = L("");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("relative_correction_y", coFloat);
    //def->label = L("");
    //def->full_label = L("");
    //def->tooltip = L("");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("relative_correction_z", coFloat);
    //def->label = L("");
    //def->full_label = L("");
    //def->tooltip = L("");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("absolute_correction", coFloat);
    //def->label = L("");
    //def->full_label = L("");
    //def->tooltip = L("");
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("elefant_foot_min_width", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("gamma_correction", coFloat);
    //def->label = L("");
    //def->full_label = L("");
    //def->tooltip = L("");
    def->min = 0;
    def->max = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.0));


    // SLA Material settings.

    def = this->add("material_colour", coString);
    //def->label = L("");
    //def->tooltip = L("");
    def->gui_type = ConfigOptionDef::GUIType::color;
    def->set_default_value(new ConfigOptionString("#29B2B2"));

    def = this->add("material_type", coString);
    //def->label = L("");
    //def->tooltip = L("");
    def->gui_type = ConfigOptionDef::GUIType::f_enum_open;   // TODO: ???
    def->gui_flags = "show_value";
    def->enum_values.push_back("Tough");
    def->enum_values.push_back("Flexible");
    def->enum_values.push_back("Casting");
    def->enum_values.push_back("Dental");
    def->enum_values.push_back("Heat-resistant");
    def->set_default_value(new ConfigOptionString("Tough"));

    def = this->add("initial_layer_height", coFloat);
    //def->label = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.3));

    def = this->add("bottle_volume", coFloat);
    //def->label = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 50;
    def->set_default_value(new ConfigOptionFloat(1000.0));

    def = this->add("bottle_weight", coFloat);
    //def->label = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("material_density", coFloat);
    //def->label = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("bottle_cost", coFloat);
    //def->label = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("faded_layers", coInt);
    //def->label = L("");
    //def->tooltip = L("");
    def->min = 3;
    def->max = 20;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(10));

    def = this->add("min_exposure_time", coFloat);
    //def->label = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("max_exposure_time", coFloat);
    //def->label = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(100));

    def = this->add("exposure_time", coFloat);
    //def->label = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(10));

    def = this->add("min_initial_exposure_time", coFloat);
    //def->label = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0));

    def = this->add("max_initial_exposure_time", coFloat);
    //def->label = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(150));

    def = this->add("initial_exposure_time", coFloat);
    //def->label = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(15));

    def = this->add("material_correction", coFloats);
    //def->full_label = L("");
    //def->tooltip = L("");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloats( { 1., 1., 1. } ));

    def = this->add("material_correction_x", coFloat);
    //def->full_label = L("");
    //def->tooltip = L("");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("material_correction_y", coFloat);
    //def->full_label = L("");
    //def->tooltip = L("");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("material_correction_z", coFloat);
    //def->full_label = L("");
    //def->tooltip = L("");
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("material_vendor", coString);
    //def->label = L("");
    //def->tooltip = L("");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("default_sla_material_profile", coString);
    //def->label = L("");
    //def->tooltip = L("");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("sla_material_settings_id", coString);
    //def->label = L("");
    //def->tooltip = L("");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("default_sla_print_profile", coString);
    //def->label = L("");
    //def->tooltip = L("");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("sla_print_settings_id", coString);
    //def->label = L("");
    //def->tooltip = L("");
    def->set_default_value(new ConfigOptionString());
    def->cli = ConfigOptionDef::nocli;

    def = this->add("supports_enable", coBool);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("support_head_front_diameter", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.4));

    def = this->add("support_head_penetration", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->mode = comAdvanced;
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(0.2));

    def = this->add("support_head_width", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->max = 20;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("support_pillar_diameter", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->max = 15;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("support_small_pillar_diameter_percent", coPercent);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 1;
    def->max = 100;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionPercent(50));

    def = this->add("support_max_bridges_on_pillar", coInt);
    //def->label = L("");
    //def->tooltip = L("");
    def->min = 0;
    def->max = 50;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionInt(3));

    def = this->add("support_pillar_connection_mode", coEnum);
    //def->label = L("");
    //def->tooltip = L("");
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
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("support_pillar_widening_factor", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    def->min = 0;
    def->max = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.0));

    def = this->add("support_base_diameter", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->max = 30;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(4.0));

    def = this->add("support_base_height", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.0));

    def = this->add("support_base_safety_distance", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->max = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("support_critical_angle", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->max = 90;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(45));

    def = this->add("support_max_bridge_length", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(15.0));

    def = this->add("support_max_pillar_link_distance", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;   // 0 means no linking
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10.0));

    def = this->add("support_object_elevation", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->max = 150; // This is the max height of print on SL1
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(5.0));

    def = this->add("support_points_density_relative", coInt);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->set_default_value(new ConfigOptionInt(100));

    def = this->add("support_points_minimal_distance", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->set_default_value(new ConfigOptionFloat(1.));

    def = this->add("pad_enable", coBool);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(true));

    def = this->add("pad_wall_thickness", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->max = 30;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(2.0));

    def = this->add("pad_wall_height", coFloat);
    //def->label = L("");
    //def->tooltip = L("");
    //def->category = L("");
    //def->sidetext = "";
    def->min = 0;
    def->max = 30;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.));

    def = this->add("pad_brim_size", coFloat);
    //def->label = L("");
    //def->tooltip = L("");
    //def->category = L("");
    //def->sidetext = "";
    def->min = 0;
    def->max = 30;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1.6));

    def = this->add("pad_max_merge_distance", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(50.0));

    def = this->add("pad_wall_slope", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 45;
    def->max = 90;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(90.0));

    def = this->add("pad_around_object", coBool);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("pad_around_object_everywhere", coBool);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("pad_object_gap", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->max = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(1));

    def = this->add("pad_object_connector_stride", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(10));

    def = this->add("pad_object_connector_width", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.5));

    def = this->add("pad_object_connector_penetration", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 0;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.3));

    def = this->add("hollowing_enable", coBool);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionBool(false));

    def = this->add("hollowing_min_thickness", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    //def->sidetext = "";
    def->min = 1;
    def->max = 10;
    def->mode = comSimple;
    def->set_default_value(new ConfigOptionFloat(3.));

    def = this->add("hollowing_quality", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    def->min = 0;
    def->max = 1;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(0.5));

    def = this->add("hollowing_closing_distance", coFloat);
    //def->label = L("");
    //def->category = L("");
    //def->tooltip = L("");
    def->sidetext = L("mm");	// millimeters, CIS languages need translation
    def->min = 0;
    def->max = 10;
    def->mode = comAdvanced;
    def->set_default_value(new ConfigOptionFloat(2.0));

    def = this->add("material_print_speed", coEnum);
    //def->label = L("");
    //def->tooltip = L("");
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
    if (opt_key == "curr_bed_type" && value == "SuperTack Plate") {
        value = "Supertack Plate";
    } else if (opt_key == "enable_wipe_tower") {
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
    } else if (opt_key == "infill_extruder" || opt_key == "sparse_infill_filament") {
        // ORCA: legacy feature-filament selector. Pre-2.4.0-dev these keys were 1-based and the
        // default value "1" meant "the first/active filament". The current scheme uses a dedicated
        // key where 0 = "Default" (inherit the object/part filament) and 1..N = explicit filament.
        // Renaming to the new *_id key here means every config (process presets, 3mf project
        // settings, imported gcode) is translated uniformly on load - not just the version-gated
        // 3mf paths the old bespoke migration covered - and a brand-new key can never be misread as
        // a legacy default. Map the legacy default "1" to "0" (inherit); keep explicit values >1.
        opt_key = "sparse_infill_filament_id";
        if (value == "1") value = "0";
    } else if (opt_key == "solid_infill_extruder" || opt_key == "solid_infill_filament") {
        opt_key = "internal_solid_filament_id";
        if (value == "1") value = "0";
    } else if (opt_key == "top_solid_infill_filament") {
        opt_key = "top_surface_filament_id";
        if (value == "1") value = "0";
    } else if (opt_key == "bottom_solid_infill_filament") {
        opt_key = "bottom_surface_filament_id";
        if (value == "1") value = "0";
    } else if (opt_key == "perimeter_extruder" || opt_key == "wall_filament" || opt_key == "wall_filament_id") {
        opt_key = "outer_wall_filament_id";
        if (value == "1") value = "0";
    } else if (opt_key == "inner_wall_filament") {
        opt_key = "inner_wall_filament_id";
        if (value == "1") value = "0";
    } else if (opt_key == "outer_wall_filament") {
        opt_key = "outer_wall_filament_id";
        if (value == "1") value = "0";
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
    } else if (opt_key == "tree_support_wall_count" && value == "-1") {
        value = "0";
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
    } else if (opt_key == "nozzle_volume_type"
        || opt_key == "default_nozzle_volume_type"
        || opt_key == "printer_extruder_variant"
        || opt_key == "print_extruder_variant"
        || opt_key == "filament_extruder_variant"
        || opt_key == "extruder_variant_list") {
        ReplaceString(value, "Normal", "Standard");
        ReplaceString(value, "Big Traffic", "High Flow");
    }
    else if (opt_key == "extruder_type") {
        ReplaceString(value, "DirectDrive", "Direct Drive");
    }
    else if (opt_key == "enable_power_loss_recovery") {
        if (value == "1" || boost::iequals(value, "true")) {
            value = "enable";
        } else if (value == "0" || boost::iequals(value, "false")) {
            value = "disable";
        }
    }
    else if(opt_key == "ensure_vertical_shell_thickness") {
        if(value == "1") {
            value = "ensure_all";
        }
        else if (value == "0"){
            value = "ensure_moderate";
        }
    } else if (opt_key == "rotate_solid_infill_direction") {
        opt_key = "solid_infill_rotate_template";
        if (value == "1") {
            value = "0,90";
        } else if (value == "0") {
            value = "0";
        }
    } else if (opt_key == "sparse_infill_anchor") {
        opt_key = "infill_anchor";
    } else if (opt_key == "sparse_infill_anchor_max") {
        opt_key = "infill_anchor_max";
    } else if (opt_key == "chamber_temperatures") {
        opt_key = "chamber_temperature";
    } else if (opt_key == "thumbnail_size") {
        opt_key = "thumbnails";
    } else if (opt_key == "top_one_wall_type" && value != "none") {
        opt_key = "only_one_wall_top";
        value = "1";
    } else if (opt_key == "initial_layer_flow_ratio") {
        opt_key = "bottom_solid_infill_flow_ratio";
    } else if (opt_key == "ironing_direction") {
        opt_key = "ironing_angle";
    } else if (opt_key == "ironing_angle" && boost::starts_with(value, "-")) {
        value = "0";
    } else if (opt_key == "counterbole_hole_bridging") {
        opt_key = "counterbore_hole_bridging";
    } else if (opt_key == "draft_shield" && value == "limited") {
        value = "disabled";
    } else if ((opt_key == "sparse_infill_pattern"         ||
                opt_key == "top_surface_pattern"           ||
                opt_key == "bottom_surface_pattern"        ||
                opt_key == "internal_solid_infill_pattern" ||
                opt_key == "ironing_pattern"               ||
                opt_key == "support_ironing_pattern") && value == "zig-zag") {
        value = "rectilinear";
    } else if (opt_key == "filament_map_mode") {
        if (value == "Auto") value = "Auto For Flush";
    }
    else if (opt_key == "filament_type"){
        std::vector<std::string> type_list;
        std::stringstream ss(value);
        std::string token;
        bool rebuild_value = false;
        while (std::getline(ss, token, ';')) {
            if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
                token = token.substr(1, token.size() - 2);
            if (token == "ASA-Aero") {
                token = "ASA-AERO";
                rebuild_value = true;
            }
            type_list.emplace_back(token);
        }
        if (rebuild_value) {
            value.clear();
            for (size_t idx = 0; idx < type_list.size(); ++idx) {
                if (idx != 0)
                    value += ';';
                value += "\"" + type_list[idx] + "\"";
            }
        }
    }
    // Orca: Rename wipe tower ribs related options
    else if (opt_key == "prime_tower_rib_wall") {
        if (value == "1") {
            opt_key = "wipe_tower_wall_type";
            value   = "rib";
        } else {
            opt_key = "";
        }
    } else if (opt_key == "prime_tower_extra_rib_length") {
        opt_key = "wipe_tower_extra_rib_length";
    } else if (opt_key == "prime_tower_rib_width") {
        opt_key = "wipe_tower_rib_width";
    } else if (opt_key == "prime_tower_fillet_wall") {
        opt_key = "wipe_tower_fillet_wall";
    } else if (opt_key == "extruder_clearance_max_radius") {
        opt_key = "extruder_clearance_radius";
    } else if (opt_key == "machine_switch_extruder_time") {
        opt_key = "machine_tool_change_time";
    }
    else if (opt_key == "wall_direction" && value == "auto") {
        value = "ccw";
    }
    // Orca: the IDEX/IQEX parallel printing keys shipped to testers as ixex_* before the feature
    // was renamed IMEX, and the two clearance keys were renamed again to say what they measure:
    // nozzle to carriage edge on the collision side, not the carriage's full width. Without this
    // an existing printer profile loses every one of these values silently.
    // is_ixex is the master gate: without it every other key below migrates into a feature that
    // stays switched off, which is worse than losing them all, because the settings then look
    // configured. ixex_primary_col/_row are the only era-1 keys with no modern counterpart (the
    // primary is a role in the mode's tools string now); they were never in an option list, so no
    // saved file carries them, and the has() check at the end of this function drops them anyway.
    else if (opt_key == "is_ixex") {
        opt_key = "is_imex";
    } else if (opt_key.compare(0, 5, "ixex_") == 0) {
        opt_key = "imex_" + opt_key.substr(5);
        if (opt_key == "imex_carriage_width_x")
            opt_key = "imex_nozzle_clearance_x";
        else if (opt_key == "imex_carriage_width_y")
            opt_key = "imex_nozzle_clearance_y";
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
        "internal_bridge_support_thickness", "top_area_threshold", "reduce_wall_solid_infill","filament_load_time","filament_unload_time",
        "smooth_coefficient", "overhang_totally_speed", "silent_mode",
        "overhang_speed_classic",
        "anisotropic_surfaces", // superseded by top_surface_fill_order / bottom_surface_fill_order
        // Belt printer keys retired before the first release: the global-mode and
        // back-transform switches are presumed on, and the pre-slice axis remap, the
        // support Z offset mode, the support floor mode (always on) and the first-layer
        // plane evaluator were removed.
        "belt_slice_rotation_global", "preslice_remap_x", "preslice_remap_y", "preslice_remap_z", "preslice_remap_global",
        "belt_support_z_offset_mode", "first_layer_plane", "first_layer_plane_offset",
        "belt_preslice_global", "gcode_back_transform", "belt_support_floor_mode", "first_layer_plane_thickness",
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
            // TRN %1% is a parameter name, %2% its value.
            std::string error_str = "\n" + Slic3r::format(_u8L("Invalid value provided for parameter %1%: %2%"), "thumbnails", thumbnails_str);
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

    // Orca: a config saved before a key joined filament_options_with_variant stores it once per filament
    // rather than once per filament variant, and one exported by an older CLI may store a single value.
    normalize_filament_values_to_variants(config);
}

// Read by PrintConfigDef's constructor, below, to generate the filament_*
// override options. A std::vector initialises dynamically, so this has to
// stay in this unit and above print_config_def: if the constructor runs
// first it sees an empty vector, and those options go undefined with no
// build or link error to notice.
const std::vector<std::string> filament_extruder_override_keys = {
    // floats
    "filament_retraction_length",
    "filament_z_hop",
    "filament_z_hop_types",
    "filament_retract_lift_above",  //not in filament_options_with_variant, not used?
    "filament_retract_lift_below",  //not in filament_options_with_variant, not used?
    "filament_retract_lift_enforce",
    "filament_retraction_speed",
    "filament_deretraction_speed",
    "filament_retract_restart_extra",  //not in filament_options_with_variant, added on 20250816
    "filament_retraction_minimum_travel",
    "filament_retract_length_toolchange",
    "filament_retract_restart_extra_toolchange",
    // BBS: floats
    "filament_wipe_distance",
    // bools
    "filament_retract_when_changing_layer",
    "filament_wipe",
    // percents
    "filament_retract_before_wipe",
    // Orca
    "filament_retract_after_wipe",
    // BBS
    "filament_long_retractions_when_cut",
    "filament_retraction_distances_when_cut"
};

// Some filament override parameters are generated from filament_extruder_override_keys,
// while filament_retract_length_nc is defined separately. Keep the generator list
// unchanged and use this helper for behavior checks that need the full override set.
bool is_filament_extruder_override_key(const std::string &opt_key)
{
    return std::find(filament_extruder_override_keys.begin(), filament_extruder_override_keys.end(), opt_key) != filament_extruder_override_keys.end() ||
           opt_key == "filament_retract_length_nc";
}

const PrintConfigDef print_config_def;

//todo
std::set<std::string> print_options_with_variant = {
    "initial_layer_speed",
    "initial_layer_infill_speed",
    "outer_wall_speed",
    "inner_wall_speed",
    "small_perimeter_speed",  //coFloatsOrPercents
    "small_perimeter_threshold",
    "sparse_infill_speed",
    "internal_solid_infill_speed",
    "top_surface_speed",
    "enable_overhang_speed", //coBools
    "overhang_1_4_speed",
    "overhang_2_4_speed",
    "overhang_3_4_speed",
    "overhang_4_4_speed",
    "slowdown_for_curled_perimeters",
    "bridge_speed",
    "internal_bridge_speed",
    "gap_infill_speed",
    "support_speed",
    "support_interface_speed",
    "travel_speed",
    "travel_speed_z",
    "initial_layer_travel_speed",
    "default_acceleration",
    "bridge_acceleration",
    "travel_acceleration",
    "initial_layer_travel_acceleration",
    "initial_layer_acceleration",
    "outer_wall_acceleration",
    "inner_wall_acceleration",
    "sparse_infill_acceleration", //coFloatsOrPercents
    "internal_solid_infill_acceleration", //coFloatsOrPercents
    "top_surface_acceleration",
    "default_jerk",
    "outer_wall_jerk",
    "inner_wall_jerk",
    "infill_jerk",
    "top_surface_jerk",
    "initial_layer_jerk",
    "travel_jerk",
    "initial_layer_travel_jerk",
    "default_junction_deviation",
    "print_extruder_id", //coInts
    "print_extruder_variant", //coStrings
    "top_solid_infill_flow_ratio"
};

std::set<std::string> filament_options_with_variant = {
    "filament_flow_ratio",
    "filament_max_volumetric_speed",
    // Per-variant ramming / pre-cooling / nozzle-change filament overrides
    "filament_ramming_volumetric_speed",
    "filament_pre_cooling_temperature",
    "filament_ramming_travel_time",
    "filament_ramming_volumetric_speed_nc",
    "filament_pre_cooling_temperature_nc",
    "filament_ramming_travel_time_nc",
    "filament_retract_length_nc",
    "filament_preheat_temperature_delta",
    //"filament_extruder_id",
    "filament_extruder_variant",
    "filament_retraction_length",
    "filament_z_hop",
    "filament_z_hop_types",
    "filament_retract_lift_above",
    "filament_retract_lift_below",
    "filament_retract_lift_enforce",
    "filament_retract_restart_extra",
    "filament_retract_length_toolchange",
    "filament_retract_restart_extra_toolchange",
    "filament_retraction_speed",
    "filament_deretraction_speed",
    "filament_retraction_minimum_travel",
    "filament_retract_when_changing_layer",
    "filament_wipe",
    //BBS
    "filament_wipe_distance",
    "filament_retract_before_wipe",
    // Orca
    "filament_retract_after_wipe",
    //BBS
    "filament_long_retractions_when_cut",
    "filament_retraction_distances_when_cut",
    "long_retractions_when_ec",
    "retraction_distances_when_ec",
    "nozzle_temperature_initial_layer",
    "nozzle_temperature",
    "filament_flush_volumetric_speed",
    "filament_flush_temp",
    "filament_cooling_before_tower",
    "volumetric_speed_coefficients",
    "filament_adaptive_volumetric_speed",
    "filament_ironing_flow",
    "filament_ironing_spacing",
    "filament_ironing_inset",
    "filament_ironing_speed",
    // Orca: pressure advance
    "enable_pressure_advance",
    "pressure_advance",
    "adaptive_pressure_advance",
    "adaptive_pressure_advance_model",
    "adaptive_pressure_advance_overhangs",
    "adaptive_pressure_advance_bridges",
    // Orca: cooling fans, multi-tool ramming and recommended nozzle temperature range
    "fan_min_speed",
    "fan_max_speed",
    "additional_cooling_fan_speed",
    "filament_minimal_purge_on_wipe_tower",
    "filament_multitool_ramming",
    "filament_multitool_ramming_volume",
    "filament_multitool_ramming_flow",
    "nozzle_temperature_range_low",
    "nozzle_temperature_range_high",
    "activate_air_filtration",
    "activate_air_filtration_during_print",
    "activate_air_filtration_on_completion",
    "during_print_exhaust_fan_speed",
    "complete_print_exhaust_fan_speed"
};

// Parameters that are the same as the number of extruders
std::set<std::string> printer_extruder_options = {
    "extruder_type",
    "nozzle_diameter",
    "default_nozzle_volume_type",
    "extruder_printable_area",
    "extruder_printable_height",
    "min_layer_height",
    "max_layer_height",
    "extruder_max_nozzle_count"     // Per-extruder max (sub-)nozzle count
};

std::set<std::string> printer_options_with_variant_1 = {
    "nozzle_volume",
    "retraction_length",
    "z_hop",
    "travel_slope",
    "retract_lift_above",
    "retract_lift_below",
    "retract_lift_enforce",
    "z_hop_types",
    "retraction_speed",
    "deretraction_speed",
    "retraction_minimum_travel",
    "retract_when_changing_layer",
    "wipe",
    "wipe_distance",
    "retract_before_wipe",
    // Orca:
    "retract_after_wipe",
    "retract_length_toolchange",
    "retract_restart_extra",
    "retract_restart_extra_toolchange",
    "long_retractions_when_cut",
    "retraction_distances_when_cut",
    "nozzle_volume",
    "nozzle_type",
    "printer_extruder_id",
    "printer_extruder_variant",
    // Per-variant hotend heat-up / cool-down rates (pre-heat time model)
    "hotend_cooling_rate",
    "hotend_heating_rate",
    "nozzle_flush_dataset"
};

//options with silient mode
std::set<std::string> printer_options_with_variant_2 = {
    "machine_max_acceleration_x",
    "machine_max_acceleration_y",
    "machine_max_acceleration_z",
    "machine_max_acceleration_e",
    "machine_max_acceleration_extruding",
    "machine_max_acceleration_retracting",
    "machine_max_acceleration_travel",
    "machine_max_speed_x",
    "machine_max_speed_y",
    "machine_max_speed_z",
    "machine_max_speed_e",
    "machine_max_jerk_x",
    "machine_max_jerk_y",
    "machine_max_jerk_z",
    "machine_max_jerk_e",
    "machine_max_junction_deviation",
};

std::set<std::string> empty_options;

std::set<std::string> filament_dev_options = {
    "filament_dev_ams_drying_ams_limitations",
    "filament_dev_ams_drying_temperature",
    "filament_dev_ams_drying_time",
    "filament_dev_ams_drying_heat_distortion_temperature",
    "filament_dev_chamber_drying_bed_temperature",
    "filament_dev_chamber_drying_time",
    "filament_dev_drying_softening_temperature",
    "filament_dev_drying_cooling_temperature"
};

} // namespace Slic3r
