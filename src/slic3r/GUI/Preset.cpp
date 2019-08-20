#include <cassert>

#include "Preset.hpp"
#include "AppConfig.hpp"
#include "BitmapCache.hpp"
#include "I18N.hpp"
#include "wxExtensions.hpp"

#ifdef _MSC_VER
    #define WIN32_LEAN_AND_MEAN
    #define NOMINMAX
    #include <Windows.h>
#endif /* _MSC_VER */

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <unordered_map>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <boost/nowide/cenv.hpp>
#include <boost/nowide/convert.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/locale.hpp>
#include <boost/log/trivial.hpp>

#include <wx/image.h>
#include <wx/choice.h>
#include <wx/bmpcbox.h>
#include <wx/wupdlock.h>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "libslic3r/PlaceholderParser.hpp"
#include "Plater.hpp"

using boost::property_tree::ptree;

namespace Slic3r {

ConfigFileType guess_config_file_type(const ptree &tree)
{
    size_t app_config   = 0;
    size_t bundle       = 0;
    size_t config       = 0;
    for (const ptree::value_type &v : tree) {
        if (v.second.empty()) {
            if (v.first == "background_processing" ||
                v.first == "last_output_path" ||
                v.first == "no_controller" ||
                v.first == "no_defaults")
                ++ app_config;
            else if (v.first == "nozzle_diameter" ||
                v.first == "filament_diameter")
                ++ config;
        } else if (boost::algorithm::starts_with(v.first, "print:") ||
            boost::algorithm::starts_with(v.first, "filament:") ||
            boost::algorithm::starts_with(v.first, "printer:") ||
            v.first == "settings")
            ++ bundle;
        else if (v.first == "presets") {
            ++ app_config;
            ++ bundle;
        } else if (v.first == "recent") {
            for (auto &kvp : v.second)
                if (kvp.first == "config_directory" || kvp.first == "skein_directory")
                    ++ app_config;
        }
    }
    return (app_config > bundle && app_config > config) ? CONFIG_FILE_TYPE_APP_CONFIG :
           (bundle > config) ? CONFIG_FILE_TYPE_CONFIG_BUNDLE : CONFIG_FILE_TYPE_CONFIG;
}


VendorProfile VendorProfile::from_ini(const boost::filesystem::path &path, bool load_all)
{
    ptree tree;
    boost::filesystem::ifstream ifs(path);
    boost::property_tree::read_ini(ifs, tree);
    return VendorProfile::from_ini(tree, path, load_all);
}

static const std::unordered_map<std::string, std::string> pre_family_model_map {{
    { "MK3",        "MK3" },
    { "MK3MMU2",    "MK3" },
    { "MK2.5",      "MK2.5" },
    { "MK2.5MMU2",  "MK2.5" },
    { "MK2S",       "MK2" },
    { "MK2SMM",     "MK2" },
    { "SL1",        "SL1" },
}};

VendorProfile VendorProfile::from_ini(const ptree &tree, const boost::filesystem::path &path, bool load_all)
{
    static const std::string printer_model_key = "printer_model:";
    const std::string id = path.stem().string();

    if (! boost::filesystem::exists(path)) {
        throw std::runtime_error((boost::format("Cannot load Vendor Config Bundle `%1%`: File not found: `%2%`.") % id % path).str());
    }

    VendorProfile res(id);

    auto get_or_throw = [&](const ptree &tree, const std::string &key) -> ptree::const_assoc_iterator
    {
        auto res = tree.find(key);
        if (res == tree.not_found()) {
            throw std::runtime_error((boost::format("Vendor Config Bundle `%1%` is not valid: Missing secion or key: `%2%`.") % id % key).str());
        }
        return res;
    };

    const auto &vendor_section = get_or_throw(tree, "vendor")->second;
    res.name = get_or_throw(vendor_section, "name")->second.data();

    auto config_version_str = get_or_throw(vendor_section, "config_version")->second.data();
    auto config_version = Semver::parse(config_version_str);
    if (! config_version) {
        throw std::runtime_error((boost::format("Vendor Config Bundle `%1%` is not valid: Cannot parse config_version: `%2%`.") % id % config_version_str).str());
    } else {
        res.config_version = std::move(*config_version);
    }

    const auto config_update_url = vendor_section.find("config_update_url");
    if (config_update_url != vendor_section.not_found()) {
        res.config_update_url = config_update_url->second.data();
    }

    const auto changelog_url = vendor_section.find("changelog_url");
    if (changelog_url != vendor_section.not_found()) {
        res.changelog_url = changelog_url->second.data();
    }

    if (! load_all) {
        return res;
    }

    for (auto &section : tree) {
        if (boost::starts_with(section.first, printer_model_key)) {
            VendorProfile::PrinterModel model;
            model.id = section.first.substr(printer_model_key.size());
            model.name = section.second.get<std::string>("name", model.id);

            const char *technology_fallback = boost::algorithm::starts_with(model.id, "SL") ? "SLA" : "FFF";

            auto technology_field = section.second.get<std::string>("technology", technology_fallback);
            if (! ConfigOptionEnum<PrinterTechnology>::from_string(technology_field, model.technology)) {
                BOOST_LOG_TRIVIAL(error) << boost::format("Vendor bundle: `%1%`: Invalid printer technology field: `%2%`") % id % technology_field;
                model.technology = ptFFF;
            }

            model.family = section.second.get<std::string>("family", std::string());
            if (model.family.empty() && res.name == "Prusa Research") {
                // If no family is specified, it can be inferred for known printers
                const auto from_pre_map = pre_family_model_map.find(model.id);
                if (from_pre_map != pre_family_model_map.end()) { model.family = from_pre_map->second; }
            }
#if 0
            // Remove SLA printers from the initial alpha.
            if (model.technology == ptSLA)
                continue;
#endif
            section.second.get<std::string>("variants", "");
            const auto variants_field = section.second.get<std::string>("variants", "");
            std::vector<std::string> variants;
            if (Slic3r::unescape_strings_cstyle(variants_field, variants)) {
                for (const std::string &variant_name : variants) {
                    if (model.variant(variant_name) == nullptr)
                        model.variants.emplace_back(VendorProfile::PrinterVariant(variant_name));
                }
            } else {
                BOOST_LOG_TRIVIAL(error) << boost::format("Vendor bundle: `%1%`: Malformed variants field: `%2%`") % id % variants_field;
            }
            if (! model.id.empty() && ! model.variants.empty())
                res.models.push_back(std::move(model));
        }
    }

    return res;
}

std::vector<std::string> VendorProfile::families() const
{
    std::vector<std::string> res;
    unsigned num_familiies = 0;

    for (auto &model : models) {
        if (std::find(res.begin(), res.end(), model.family) == res.end()) {
            res.push_back(model.family);
            num_familiies++;
        }
    }

    return res;
}

// Suffix to be added to a modified preset name in the combo box.
static std::string g_suffix_modified = " (modified)";
const std::string& Preset::suffix_modified()
{
    return g_suffix_modified;
}

void Preset::update_suffix_modified()
{
    g_suffix_modified = (" (" + _(L("modified")) + ")").ToUTF8().data();
}
// Remove an optional "(modified)" suffix from a name.
// This converts a UI name to a unique preset identifier.
std::string Preset::remove_suffix_modified(const std::string &name)
{
    return boost::algorithm::ends_with(name, g_suffix_modified) ?
        name.substr(0, name.size() - g_suffix_modified.size()) :
        name;
}

void Preset::set_num_extruders(DynamicPrintConfig &config, unsigned int num_extruders)
{
    const auto &defaults = FullPrintConfig::defaults();
    for (const std::string &key : Preset::nozzle_options()) {
        if (key == "default_filament_profile")
            continue;
        auto *opt = config.option(key, false);
        assert(opt != nullptr);
        assert(opt->is_vector());
        if (opt != nullptr && opt->is_vector())
            static_cast<ConfigOptionVectorBase*>(opt)->resize(num_extruders, defaults.option(key));
    }
}

// Update new extruder fields at the printer profile.
void Preset::normalize(DynamicPrintConfig &config)
{
    auto *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(config.option("nozzle_diameter"));
    if (nozzle_diameter != nullptr)
        // Loaded the FFF Printer settings. Verify, that all extruder dependent values have enough values.
        set_num_extruders(config, (unsigned int)nozzle_diameter->values.size());
    if (config.option("filament_diameter") != nullptr) {
        // This config contains single or multiple filament presets.
        // Ensure that the filament preset vector options contain the correct number of values.
        size_t n = (nozzle_diameter == nullptr) ? 1 : nozzle_diameter->values.size();
        const auto &defaults = FullPrintConfig::defaults();
        for (const std::string &key : Preset::filament_options()) {
            if (key == "compatible_prints" || key == "compatible_printers")
                continue;
            auto *opt = config.option(key, false);
            /*assert(opt != nullptr);
            assert(opt->is_vector());*/
            if (opt != nullptr && opt->is_vector())
                static_cast<ConfigOptionVectorBase*>(opt)->resize(n, defaults.option(key));
        }
        // The following keys are mandatory for the UI, but they are not part of FullPrintConfig, therefore they are handled separately.
        for (const std::string &key : { "filament_settings_id" }) {
            auto *opt = config.option(key, false);
            assert(opt == nullptr || opt->type() == coStrings);
            if (opt != nullptr && opt->type() == coStrings)
                static_cast<ConfigOptionStrings*>(opt)->values.resize(n, std::string());
        }
    }
}

std::string Preset::remove_invalid_keys(DynamicPrintConfig &config, const DynamicPrintConfig &default_config)
{
    std::string incorrect_keys;
    for (const std::string &key : config.keys())
        if (! default_config.has(key)) {
            if (incorrect_keys.empty())
                incorrect_keys = key;
            else {
                incorrect_keys += ", ";
                incorrect_keys += key;
            }
            config.erase(key);
        }
    return incorrect_keys;
}

void Preset::save()
{
    this->config.save(this->file);
}

// Return a label of this preset, consisting of a name and a "(modified)" suffix, if this preset is dirty.
std::string Preset::label() const
{
    return this->name + (this->is_dirty ? g_suffix_modified : "");
}

bool Preset::is_compatible_with_print(const Preset &active_print) const
{
    auto &condition             = this->compatible_prints_condition();
    auto *compatible_prints     = dynamic_cast<const ConfigOptionStrings*>(this->config.option("compatible_prints"));
    bool  has_compatible_prints = compatible_prints != nullptr && ! compatible_prints->values.empty();
    if (! has_compatible_prints && ! condition.empty()) {
        try {
            return PlaceholderParser::evaluate_boolean_expression(condition, active_print.config);
        } catch (const std::runtime_error &err) {
            //FIXME in case of an error, return "compatible with everything".
            printf("Preset::is_compatible_with_print - parsing error of compatible_prints_condition %s:\n%s\n", active_print.name.c_str(), err.what());
            return true;
        }
    }
    return this->is_default || active_print.name.empty() || ! has_compatible_prints ||
        std::find(compatible_prints->values.begin(), compatible_prints->values.end(), active_print.name) !=
            compatible_prints->values.end();
}

bool Preset::is_compatible_with_printer(const Preset &active_printer, const DynamicPrintConfig *extra_config) const
{
    auto &condition               = this->compatible_printers_condition();
    auto *compatible_printers     = dynamic_cast<const ConfigOptionStrings*>(this->config.option("compatible_printers"));
    bool  has_compatible_printers = compatible_printers != nullptr && ! compatible_printers->values.empty();
    if (! has_compatible_printers && ! condition.empty()) {
        try {
            return PlaceholderParser::evaluate_boolean_expression(condition, active_printer.config, extra_config);
        } catch (const std::runtime_error &err) {
            //FIXME in case of an error, return "compatible with everything".
            printf("Preset::is_compatible_with_printer - parsing error of compatible_printers_condition %s:\n%s\n", active_printer.name.c_str(), err.what());
            return true;
        }
    }
    return this->is_default || active_printer.name.empty() || ! has_compatible_printers ||
        std::find(compatible_printers->values.begin(), compatible_printers->values.end(), active_printer.name) !=
            compatible_printers->values.end();
}

bool Preset::is_compatible_with_printer(const Preset &active_printer) const
{
    DynamicPrintConfig config;
    config.set_key_value("printer_preset", new ConfigOptionString(active_printer.name));
    const ConfigOption *opt = active_printer.config.option("nozzle_diameter");
    if (opt)
        config.set_key_value("num_extruders", new ConfigOptionInt((int)static_cast<const ConfigOptionFloats*>(opt)->values.size()));
    return this->is_compatible_with_printer(active_printer, &config);
}

bool Preset::update_compatible(const Preset &active_printer, const DynamicPrintConfig *extra_config, const Preset *active_print)
{
    this->is_compatible  = is_compatible_with_printer(active_printer, extra_config);
    if (active_print != nullptr)
        this->is_compatible &= is_compatible_with_print(*active_print);
    return this->is_compatible;
}

void Preset::set_visible_from_appconfig(const AppConfig &app_config)
{
    if (vendor == nullptr) { return; }
    const std::string &model = config.opt_string("printer_model");
    const std::string &variant = config.opt_string("printer_variant");
    if (model.empty() || variant.empty()) { return; }
    is_visible = app_config.get_variant(vendor->id, model, variant);
}

const std::vector<std::string>& Preset::print_options()
{
    static std::vector<std::string> s_opts {
        "layer_height", "first_layer_height", "perimeters", "spiral_vase", "slice_closing_radius", "top_solid_layers", "bottom_solid_layers",
        "extra_perimeters", "ensure_vertical_shell_thickness", "avoid_crossing_perimeters", "thin_walls", "overhangs",
        "seam_position", "external_perimeters_first", "fill_density", "fill_pattern", "top_fill_pattern", "bottom_fill_pattern",
        "infill_every_layers", "infill_only_where_needed", "solid_infill_every_layers", "fill_angle", "bridge_angle",
        "solid_infill_below_area", "only_retract_when_crossing_perimeters", "infill_first", "max_print_speed",
        "max_volumetric_speed",
#ifdef HAS_PRESSURE_EQUALIZER
        "max_volumetric_extrusion_rate_slope_positive", "max_volumetric_extrusion_rate_slope_negative",
#endif /* HAS_PRESSURE_EQUALIZER */
        "perimeter_speed", "small_perimeter_speed", "external_perimeter_speed", "infill_speed", "solid_infill_speed",
        "top_solid_infill_speed", "support_material_speed", "support_material_xy_spacing", "support_material_interface_speed",
        "bridge_speed", "gap_fill_speed", "travel_speed", "first_layer_speed", "perimeter_acceleration", "infill_acceleration",
        "bridge_acceleration", "first_layer_acceleration", "default_acceleration", "skirts", "skirt_distance", "skirt_height",
        "min_skirt_length", "brim_width", "support_material", "support_material_auto", "support_material_threshold", "support_material_enforce_layers",
        "raft_layers", "support_material_pattern", "support_material_with_sheath", "support_material_spacing",
        "support_material_synchronize_layers", "support_material_angle", "support_material_interface_layers",
        "support_material_interface_spacing", "support_material_interface_contact_loops", "support_material_contact_distance",
        "support_material_buildplate_only", "dont_support_bridges", "notes", "complete_objects", "extruder_clearance_radius",
        "extruder_clearance_height", "gcode_comments", "gcode_label_objects", "output_filename_format", "post_process", "perimeter_extruder",
        "infill_extruder", "solid_infill_extruder", "support_material_extruder", "support_material_interface_extruder",
        "ooze_prevention", "standby_temperature_delta", "interface_shells", "extrusion_width", "first_layer_extrusion_width",
        "perimeter_extrusion_width", "external_perimeter_extrusion_width", "infill_extrusion_width", "solid_infill_extrusion_width",
        "top_infill_extrusion_width", "support_material_extrusion_width", "infill_overlap", "bridge_flow_ratio", "clip_multipart_objects",
        "elefant_foot_compensation", "xy_size_compensation", "threads", "resolution", "wipe_tower", "wipe_tower_x", "wipe_tower_y",
        "wipe_tower_width", "wipe_tower_rotation_angle", "wipe_tower_bridging", "single_extruder_multi_material_priming",
        "compatible_printers", "compatible_printers_condition", "inherits"
    };
    return s_opts;
}

const std::vector<std::string>& Preset::filament_options()
{
    static std::vector<std::string> s_opts {
        "filament_colour", "filament_diameter", "filament_type", "filament_soluble", "filament_notes", "filament_max_volumetric_speed",
        "extrusion_multiplier", "filament_density", "filament_cost", "filament_loading_speed", "filament_loading_speed_start", "filament_load_time",
        "filament_unloading_speed", "filament_unloading_speed_start", "filament_unload_time", "filament_toolchange_delay", "filament_cooling_moves",
        "filament_cooling_initial_speed", "filament_cooling_final_speed", "filament_ramming_parameters", "filament_minimal_purge_on_wipe_tower",
        "temperature", "first_layer_temperature", "bed_temperature", "first_layer_bed_temperature", "fan_always_on", "cooling", "min_fan_speed",
        "max_fan_speed", "bridge_fan_speed", "disable_fan_first_layers", "fan_below_layer_time", "slowdown_below_layer_time", "min_print_speed",
        "start_filament_gcode", "end_filament_gcode",
        // Retract overrides
        "filament_retract_length", "filament_retract_lift", "filament_retract_lift_above", "filament_retract_lift_below", "filament_retract_speed", "filament_deretract_speed", "filament_retract_restart_extra", "filament_retract_before_travel",
        "filament_retract_layer_change", "filament_wipe", "filament_retract_before_wipe",
        // Profile compatibility
        "compatible_prints", "compatible_prints_condition", "compatible_printers", "compatible_printers_condition", "inherits"
    };
    return s_opts;
}

const std::vector<std::string>& Preset::printer_options()
{
    static std::vector<std::string> s_opts;
    if (s_opts.empty()) {
        s_opts = {
            "printer_technology",
            "bed_shape", "bed_custom_texture", "bed_custom_model", "z_offset", "gcode_flavor", "use_relative_e_distances", "serial_port", "serial_speed",
            "use_firmware_retraction", "use_volumetric_e", "variable_layer_height",
            "host_type", "print_host", "printhost_apikey", "printhost_cafile",
            "single_extruder_multi_material", "start_gcode", "end_gcode", "before_layer_gcode", "layer_gcode", "toolchange_gcode",
            "between_objects_gcode", "printer_vendor", "printer_model", "printer_variant", "printer_notes", "cooling_tube_retraction",
            "cooling_tube_length", "high_current_on_filament_swap", "parking_pos_retraction", "extra_loading_move", "max_print_height",
            "default_print_profile", "inherits",
            "remaining_times", "silent_mode", "machine_max_acceleration_extruding", "machine_max_acceleration_retracting",
            "machine_max_acceleration_x", "machine_max_acceleration_y", "machine_max_acceleration_z", "machine_max_acceleration_e",
            "machine_max_feedrate_x", "machine_max_feedrate_y", "machine_max_feedrate_z", "machine_max_feedrate_e",
            "machine_min_extruding_rate", "machine_min_travel_rate",
            "machine_max_jerk_x", "machine_max_jerk_y", "machine_max_jerk_z", "machine_max_jerk_e"
        };
        s_opts.insert(s_opts.end(), Preset::nozzle_options().begin(), Preset::nozzle_options().end());
    }
    return s_opts;
}

// The following nozzle options of a printer profile will be adjusted to match the size
// of the nozzle_diameter vector.
const std::vector<std::string>& Preset::nozzle_options()
{
    // ConfigOptionFloats, ConfigOptionPercents, ConfigOptionBools, ConfigOptionStrings
    static std::vector<std::string> s_opts {
        "nozzle_diameter", "min_layer_height", "max_layer_height", "extruder_offset",
        "retract_length", "retract_lift", "retract_lift_above", "retract_lift_below", "retract_speed", "deretract_speed",
        "retract_before_wipe", "retract_restart_extra", "retract_before_travel", "wipe",
        "retract_layer_change", "retract_length_toolchange", "retract_restart_extra_toolchange", "extruder_colour",
        "default_filament_profile"
    };
    return s_opts;
}

const std::vector<std::string>& Preset::sla_print_options()
{
    static std::vector<std::string> s_opts;
    if (s_opts.empty()) {
        s_opts = {
            "layer_height",
            "faded_layers",
            "supports_enable",
            "support_head_front_diameter",
            "support_head_penetration",
            "support_head_width",
            "support_pillar_diameter",
            "support_pillar_connection_mode",
            "support_buildplate_only",
            "support_pillar_widening_factor",
            "support_base_diameter",
            "support_base_height",
            "support_base_safety_distance",
            "support_critical_angle",
            "support_max_bridge_length",
            "support_max_pillar_link_distance",
            "support_object_elevation",
            "support_points_density_relative",
            "support_points_minimal_distance",
            "slice_closing_radius",
            "pad_enable",
            "pad_wall_thickness",
            "pad_wall_height",
            "pad_max_merge_distance",
            // "pad_edge_radius",
            "pad_wall_slope",
            "pad_object_gap",
            "pad_zero_elevation",
            "pad_object_connector_stride",
            "pad_object_connector_width",
            "pad_object_connector_penetration",
            "output_filename_format",
            "default_sla_print_profile",
            "compatible_printers",
            "compatible_printers_condition",
            "inherits"
        };
    }
    return s_opts;
}

const std::vector<std::string>& Preset::sla_material_options()
{
    static std::vector<std::string> s_opts;
    if (s_opts.empty()) {
        s_opts = {
            "initial_layer_height",
            "exposure_time_min", "exposure_time_max", "exposure_time",
            "initial_exposure_time_min", "initial_exposure_time_max", "initial_exposure_time",
            "material_correction",
            "material_notes",
            "default_sla_material_profile",
            "compatible_prints", "compatible_prints_condition",
            "compatible_printers", "compatible_printers_condition", "inherits"
        };
    }
    return s_opts;
}

const std::vector<std::string>& Preset::sla_printer_options()
{
    static std::vector<std::string> s_opts;
    if (s_opts.empty()) {
        s_opts = {
            "printer_technology",
            "bed_shape", "bed_custom_texture", "bed_custom_model", "max_print_height",
            "bed_shape", "max_print_height",
            "display_width", "display_height", "display_pixels_x", "display_pixels_y",
            "display_mirror_x", "display_mirror_y",
            "display_orientation",
            "fast_tilt_time", "slow_tilt_time", "area_fill",
            "relative_correction",
            "absolute_correction",
            "gamma_correction",
            "print_host", "printhost_apikey", "printhost_cafile",
            "printer_notes",
            "inherits"
        };
    }
    return s_opts;
}

PresetCollection::PresetCollection(Preset::Type type, const std::vector<std::string> &keys, const Slic3r::StaticPrintConfig &defaults, const std::string &default_name) :
    m_type(type),
    m_edited_preset(type, "", false),
    m_idx_selected(0),
    m_bitmap_main_frame(new wxBitmap),
    m_bitmap_add(new wxBitmap),
    m_bitmap_cache(new GUI::BitmapCache)
{
    // Insert just the default preset.
    this->add_default_preset(keys, defaults, default_name);
    m_edited_preset.config.apply(m_presets.front().config);
}

PresetCollection::~PresetCollection()
{
    delete m_bitmap_main_frame;
    m_bitmap_main_frame = nullptr;
    delete m_bitmap_add;
    m_bitmap_add = nullptr;
    delete m_bitmap_cache;
    m_bitmap_cache = nullptr;
}

void PresetCollection::reset(bool delete_files)
{
    if (m_presets.size() > m_num_default_presets) {
        if (delete_files) {
            // Erase the preset files.
            for (Preset &preset : m_presets)
                if (! preset.is_default && ! preset.is_external && ! preset.is_system)
                    boost::nowide::remove(preset.file.c_str());
        }
        // Don't use m_presets.resize() here as it requires a default constructor for Preset.
        m_presets.erase(m_presets.begin() + m_num_default_presets, m_presets.end());
        this->select_preset(0);
    }
}

void PresetCollection::add_default_preset(const std::vector<std::string> &keys, const Slic3r::StaticPrintConfig &defaults, const std::string &preset_name)
{
    // Insert just the default preset.
    m_presets.emplace_back(Preset(this->type(), preset_name, true));
    m_presets.back().config.apply_only(defaults, keys.empty() ? defaults.keys() : keys);
    m_presets.back().loaded = true;
    ++ m_num_default_presets;
}

// Load all presets found in dir_path.
// Throws an exception on error.
void PresetCollection::load_presets(const std::string &dir_path, const std::string &subdir)
{
    boost::filesystem::path dir = boost::filesystem::canonical(boost::filesystem::path(dir_path) / subdir).make_preferred();
    m_dir_path = dir.string();
    std::string errors_cummulative;
    // Store the loaded presets into a new vector, otherwise the binary search for already existing presets would be broken.
    // (see the "Preset already present, not loading" message).
    std::deque<Preset> presets_loaded;
    for (auto &dir_entry : boost::filesystem::directory_iterator(dir))
        if (Slic3r::is_ini_file(dir_entry)) {
            std::string name = dir_entry.path().filename().string();
            // Remove the .ini suffix.
            name.erase(name.size() - 4);
            if (this->find_preset(name, false)) {
                // This happens when there's is a preset (most likely legacy one) with the same name as a system preset
                // that's already been loaded from a bundle.
                BOOST_LOG_TRIVIAL(warning) << "Preset already present, not loading: " << name;
                continue;
            }
            try {
                Preset preset(m_type, name, false);
                preset.file = dir_entry.path().string();
                // Load the preset file, apply preset values on top of defaults.
                try {
                    DynamicPrintConfig config;
                    config.load_from_ini(preset.file);
                    // Find a default preset for the config. The PrintPresetCollection provides different default preset based on the "printer_technology" field.
                    const Preset &default_preset = this->default_preset_for(config);
                    preset.config = default_preset.config;
                    preset.config.apply(std::move(config));
                    Preset::normalize(preset.config);
                    // Report configuration fields, which are misplaced into a wrong group.
                    std::string incorrect_keys = Preset::remove_invalid_keys(config, default_preset.config);
                    if (! incorrect_keys.empty())
                        BOOST_LOG_TRIVIAL(error) << "Error in a preset file: The preset \"" <<
                            preset.file << "\" contains the following incorrect keys: " << incorrect_keys << ", which were removed";
                    preset.loaded = true;
                } catch (const std::ifstream::failure &err) {
                    throw std::runtime_error(std::string("The selected preset cannot be loaded: ") + preset.file + "\n\tReason: " + err.what());
                } catch (const std::runtime_error &err) {
                    throw std::runtime_error(std::string("Failed loading the preset file: ") + preset.file + "\n\tReason: " + err.what());
                }
                presets_loaded.emplace_back(preset);
            } catch (const std::runtime_error &err) {
                errors_cummulative += err.what();
                errors_cummulative += "\n";
            }
        }
    m_presets.insert(m_presets.end(), std::make_move_iterator(presets_loaded.begin()), std::make_move_iterator(presets_loaded.end()));
    std::sort(m_presets.begin() + m_num_default_presets, m_presets.end());
    this->select_preset(first_visible_idx());
    if (! errors_cummulative.empty())
        throw std::runtime_error(errors_cummulative);
}

// Load a preset from an already parsed config file, insert it into the sorted sequence of presets
// and select it, losing previous modifications.
Preset& PresetCollection::load_preset(const std::string &path, const std::string &name, const DynamicPrintConfig &config, bool select)
{
    DynamicPrintConfig cfg(this->default_preset().config);
    cfg.apply_only(config, cfg.keys(), true);
    return this->load_preset(path, name, std::move(cfg), select);
}

static bool profile_print_params_same(const DynamicPrintConfig &cfg1, const DynamicPrintConfig &cfg2)
{
    t_config_option_keys diff = cfg1.diff(cfg2);
    // Following keys are used by the UI, not by the slicing core, therefore they are not important
    // when comparing profiles for equality. Ignore them.
    for (const char *key : { "compatible_prints", "compatible_prints_condition",
                             "compatible_printers", "compatible_printers_condition", "inherits",
                             "print_settings_id", "filament_settings_id", "sla_print_settings_id", "sla_material_settings_id", "printer_settings_id",
                             "printer_model", "printer_variant", "default_print_profile", "default_filament_profile", "default_sla_print_profile", "default_sla_material_profile",
                             "printhost_apikey", "printhost_cafile" })
        diff.erase(std::remove(diff.begin(), diff.end(), key), diff.end());
    // Preset with the same name as stored inside the config exists.
    return diff.empty();
}

// Load a preset from an already parsed config file, insert it into the sorted sequence of presets
// and select it, losing previous modifications.
// In case
Preset& PresetCollection::load_external_preset(
    // Path to the profile source file (a G-code, an AMF or 3MF file, a config file)
    const std::string           &path,
    // Name of the profile, derived from the source file name.
    const std::string           &name,
    // Original name of the profile, extracted from the loaded config. Empty, if the name has not been stored.
    const std::string           &original_name,
    // Config to initialize the preset from.
    const DynamicPrintConfig    &config,
    // Select the preset after loading?
    bool                         select)
{
    // Load the preset over a default preset, so that the missing fields are filled in from the default preset.
    DynamicPrintConfig cfg(this->default_preset_for(config).config);
    cfg.apply_only(config, cfg.keys(), true);
    // Is there a preset already loaded with the name stored inside the config?
    std::deque<Preset>::iterator it = this->find_preset_internal(original_name);
    bool                         found = it != m_presets.end() && it->name == original_name;
    if (found && profile_print_params_same(it->config, cfg)) {
        // The preset exists and it matches the values stored inside config.
        if (select)
            this->select_preset(it - m_presets.begin());
        return *it;
    }
    // Update the "inherits" field.
    std::string &inherits = Preset::inherits(cfg);
    if (found && inherits.empty()) {
        // There is a profile with the same name already loaded. Should we update the "inherits" field?
        if (it->vendor == nullptr)
            inherits = it->inherits();
        else
            inherits = it->name;
    }
    // The external preset does not match an internal preset, load the external preset.
    std::string new_name;
    for (size_t idx = 0;; ++ idx) {
        std::string suffix;
        if (original_name.empty()) {
            if (idx > 0)
                suffix = " (" + std::to_string(idx) + ")";
        } else {
            if (idx == 0)
                suffix = " (" + original_name + ")";
            else
                suffix = " (" + original_name + "-" + std::to_string(idx) + ")";
        }
        new_name = name + suffix;
        it = this->find_preset_internal(new_name);
        if (it == m_presets.end() || it->name != new_name)
            // Unique profile name. Insert a new profile.
            break;
        if (profile_print_params_same(it->config, cfg)) {
            // The preset exists and it matches the values stored inside config.
            if (select)
                this->select_preset(it - m_presets.begin());
            return *it;
        }
        // Form another profile name.
    }
    // Insert a new profile.
    Preset &preset = this->load_preset(path, new_name, std::move(cfg), select);
    preset.is_external = true;
    if (&this->get_selected_preset() == &preset)
        this->get_edited_preset().is_external = true;

    return preset;
}

Preset& PresetCollection::load_preset(const std::string &path, const std::string &name, DynamicPrintConfig &&config, bool select)
{
    auto it = this->find_preset_internal(name);
    if (it == m_presets.end() || it->name != name) {
        // The preset was not found. Create a new preset.
        it = m_presets.emplace(it, Preset(m_type, name, false));
    }
    Preset &preset = *it;
    preset.file = path;
    preset.config = std::move(config);
    preset.loaded = true;
    preset.is_dirty = false;
    if (select)
        this->select_preset_by_name(name, true);
    return preset;
}

void PresetCollection::save_current_preset(const std::string &new_name)
{
    // 1) Find the preset with a new_name or create a new one,
    // initialize it with the edited config.
    auto it = this->find_preset_internal(new_name);
    if (it != m_presets.end() && it->name == new_name) {
        // Preset with the same name found.
        Preset &preset = *it;
        if (preset.is_default || preset.is_external || preset.is_system)
            // Cannot overwrite the default preset.
            return;
        // Overwriting an existing preset.
        preset.config = std::move(m_edited_preset.config);
        // The newly saved preset will be activated -> make it visible.
        preset.is_visible = true;
    } else {
        // Creating a new preset.
        Preset       &preset   = *m_presets.insert(it, m_edited_preset);
        std::string  &inherits = preset.inherits();
        std::string   old_name = preset.name;
        preset.name = new_name;
        preset.file = this->path_from_name(new_name);
        preset.vendor = nullptr;
        if (preset.is_system) {
            // Inheriting from a system preset.
            inherits = /* preset.vendor->name + "/" + */ old_name;
        } else if (inherits.empty()) {
            // Inheriting from a user preset. Link the new preset to the old preset.
            // inherits = old_name;
        } else {
            // Inherited from a user preset. Just maintain the "inherited" flag,
            // meaning it will inherit from either the system preset, or the inherited user preset.
        }
        preset.is_default  = false;
        preset.is_system   = false;
        preset.is_external = false;
        // The newly saved preset will be activated -> make it visible.
        preset.is_visible  = true;
    }
    // 2) Activate the saved preset.
    this->select_preset_by_name(new_name, true);
    // 2) Store the active preset to disk.
    this->get_selected_preset().save();
}

bool PresetCollection::delete_current_preset()
{
    const Preset &selected = this->get_selected_preset();
    if (selected.is_default)
        return false;
    if (! selected.is_external && ! selected.is_system) {
        // Erase the preset file.
        boost::nowide::remove(selected.file.c_str());
    }
    // Remove the preset from the list.
    m_presets.erase(m_presets.begin() + m_idx_selected);
    // Find the next visible preset.
    size_t new_selected_idx = m_idx_selected;
    if (new_selected_idx < m_presets.size())
        for (; new_selected_idx < m_presets.size() && ! m_presets[new_selected_idx].is_visible; ++ new_selected_idx) ;
    if (new_selected_idx == m_presets.size())
        for (--new_selected_idx; new_selected_idx > 0 && !m_presets[new_selected_idx].is_visible; --new_selected_idx);
    this->select_preset(new_selected_idx);
    return true;
}

void PresetCollection::load_bitmap_default(wxWindow *window, const std::string &file_name)
{
    // XXX: See note in PresetBundle::load_compatible_bitmaps()
    (void)window;
    *m_bitmap_main_frame = create_scaled_bitmap(nullptr, file_name);
}

void PresetCollection::load_bitmap_add(wxWindow *window, const std::string &file_name)
{
    // XXX: See note in PresetBundle::load_compatible_bitmaps()
    (void)window;
    *m_bitmap_add = create_scaled_bitmap(nullptr, file_name);
}

const Preset* PresetCollection::get_selected_preset_parent() const
{
    if (this->get_selected_idx() == -1)
        // This preset collection has no preset activated yet. Only the get_edited_preset() is valid.
        return nullptr;
//    const std::string &inherits = this->get_edited_preset().inherits();
//    if (inherits.empty())
//		return this->get_selected_preset().is_system ? &this->get_selected_preset() : nullptr;

    std::string inherits = this->get_edited_preset().inherits();
    if (inherits.empty())
    {
        if (this->get_selected_preset().is_system || this->get_selected_preset().is_default)
            return &this->get_selected_preset();
        if (this->get_selected_preset().is_external)
            return nullptr;

        inherits = m_type != Preset::Type::TYPE_PRINTER ? "- default -" :
                   this->get_edited_preset().printer_technology() == ptFFF ?
                   "- default FFF -" : "- default SLA -" ;
    }

    const Preset* preset = this->find_preset(inherits, false);
    return (preset == nullptr/* || preset->is_default*/ || preset->is_external) ? nullptr : preset;
}

const Preset* PresetCollection::get_preset_parent(const Preset& child) const
{
    const std::string &inherits = child.inherits();
    if (inherits.empty())
// 		return this->get_selected_preset().is_system ? &this->get_selected_preset() : nullptr;
        return nullptr;
    const Preset* preset = this->find_preset(inherits, false);
    return (preset == nullptr/* || preset->is_default */|| preset->is_external) ? nullptr : preset;
}

const std::string& PresetCollection::get_suffix_modified() {
    return g_suffix_modified;
}

// Return a preset by its name. If the preset is active, a temporary copy is returned.
// If a preset is not found by its name, null is returned.
Preset* PresetCollection::find_preset(const std::string &name, bool first_visible_if_not_found)
{
    Preset key(m_type, name, false);
    auto it = this->find_preset_internal(name);
    // Ensure that a temporary copy is returned if the preset found is currently selected.
    return (it != m_presets.end() && it->name == key.name) ? &this->preset(it - m_presets.begin()) :
        first_visible_if_not_found ? &this->first_visible() : nullptr;
}

// Return index of the first visible preset. Certainly at least the '- default -' preset shall be visible.
size_t PresetCollection::first_visible_idx() const
{
    size_t idx = m_default_suppressed ? m_num_default_presets : 0;
    for (; idx < this->m_presets.size(); ++ idx)
        if (m_presets[idx].is_visible)
            break;
    if (idx == m_presets.size())
        idx = 0;
    return idx;
}

void PresetCollection::set_default_suppressed(bool default_suppressed)
{
    if (m_default_suppressed != default_suppressed) {
        m_default_suppressed = default_suppressed;
        bool default_visible = ! default_suppressed || m_idx_selected < m_num_default_presets;
        for (size_t i = 0; i < m_num_default_presets; ++ i)
            m_presets[i].is_visible = default_visible;
    }
}

size_t PresetCollection::update_compatible_internal(const Preset &active_printer, const Preset *active_print, bool unselect_if_incompatible)
{
    DynamicPrintConfig config;
    config.set_key_value("printer_preset", new ConfigOptionString(active_printer.name));
    const ConfigOption *opt = active_printer.config.option("nozzle_diameter");
    if (opt)
        config.set_key_value("num_extruders", new ConfigOptionInt((int)static_cast<const ConfigOptionFloats*>(opt)->values.size()));
    for (size_t idx_preset = m_num_default_presets; idx_preset < m_presets.size(); ++ idx_preset) {
        bool    selected        = idx_preset == m_idx_selected;
        Preset &preset_selected = m_presets[idx_preset];
        Preset &preset_edited   = selected ? m_edited_preset : preset_selected;
        if (! preset_edited.update_compatible(active_printer, &config, active_print) &&
            selected && unselect_if_incompatible)
            m_idx_selected = -1;
        if (selected)
            preset_selected.is_compatible = preset_edited.is_compatible;
    }
    return m_idx_selected;
}

// Save the preset under a new name. If the name is different from the old one,
// a new preset is stored into the list of presets.
// All presets are marked as not modified and the new preset is activated.
//void PresetCollection::save_current_preset(const std::string &new_name);

// Delete the current preset, activate the first visible preset.
//void PresetCollection::delete_current_preset();

// Update the wxChoice UI component from this list of presets.
// Hide the
void PresetCollection::update_platter_ui(GUI::PresetComboBox *ui)
{
    if (ui == nullptr)
        return;

    // Otherwise fill in the list from scratch.
    ui->Freeze();
    ui->Clear();
    size_t selected_preset_item = 0;

    const Preset &selected_preset = this->get_selected_preset();
    // Show wide icons if the currently selected preset is not compatible with the current printer,
    // and draw a red flag in front of the selected preset.
    bool wide_icons = ! selected_preset.is_compatible && m_bitmap_incompatible != nullptr;

    /* It's supposed that standard size of an icon is 16px*16px for 100% scaled display.
    * So set sizes for solid_colored icons used for filament preset
    * and scale them in respect to em_unit value
    */
    const float scale_f = ui->em_unit() * 0.1f;
    const int icon_height           = 16 * scale_f + 0.5f;
    const int icon_width            = 16 * scale_f + 0.5f;
    const int thin_space_icon_width = 4 * scale_f + 0.5f;
    const int wide_space_icon_width = 6 * scale_f + 0.5f;

    std::map<wxString, wxBitmap*> nonsys_presets;
    wxString selected = "";
    if (!this->m_presets.front().is_visible)
        ui->set_label_marker(ui->Append(PresetCollection::separator(L("System presets")), wxNullBitmap));
    for (size_t i = this->m_presets.front().is_visible ? 0 : m_num_default_presets; i < this->m_presets.size(); ++ i) {
        const Preset &preset = this->m_presets[i];
        if (! preset.is_visible || (! preset.is_compatible && i != m_idx_selected))
            continue;
        std::string   bitmap_key = "";
        // If the filament preset is not compatible and there is a "red flag" icon loaded, show it left
        // to the filament color image.
        if (wide_icons)
            bitmap_key += preset.is_compatible ? ",cmpt" : ",ncmpt";
        bitmap_key += (preset.is_system || preset.is_default) ? ",syst" : ",nsyst";
        wxBitmap     *bmp = m_bitmap_cache->find(bitmap_key);
        if (bmp == nullptr) {
            // Create the bitmap with color bars.
            std::vector<wxBitmap> bmps;
            if (wide_icons)
                // Paint a red flag for incompatible presets.
                bmps.emplace_back(preset.is_compatible ? m_bitmap_cache->mkclear(icon_width, icon_height) : *m_bitmap_incompatible);
            // Paint the color bars.
            bmps.emplace_back(m_bitmap_cache->mkclear(thin_space_icon_width, icon_height));
            bmps.emplace_back(*m_bitmap_main_frame);
            // Paint a lock at the system presets.
            bmps.emplace_back(m_bitmap_cache->mkclear(wide_space_icon_width, icon_height));
            bmps.emplace_back((preset.is_system || preset.is_default) ? *m_bitmap_lock : m_bitmap_cache->mkclear(icon_width, icon_height));
            bmp = m_bitmap_cache->insert(bitmap_key, bmps);
        }

        if (preset.is_default || preset.is_system) {
            ui->Append(wxString::FromUTF8((preset.name + (preset.is_dirty ? g_suffix_modified : "")).c_str()),
                (bmp == 0) ? (m_bitmap_main_frame ? *m_bitmap_main_frame : wxNullBitmap) : *bmp);
            if (i == m_idx_selected)
                selected_preset_item = ui->GetCount() - 1;
        }
        else
        {
            nonsys_presets.emplace(wxString::FromUTF8((preset.name + (preset.is_dirty ? g_suffix_modified : "")).c_str()), bmp/*preset.is_compatible*/);
            if (i == m_idx_selected)
                selected = wxString::FromUTF8((preset.name + (preset.is_dirty ? g_suffix_modified : "")).c_str());
        }
        if (i + 1 == m_num_default_presets)
            ui->set_label_marker(ui->Append(PresetCollection::separator(L("System presets")), wxNullBitmap));
    }
    if (!nonsys_presets.empty())
    {
        ui->set_label_marker(ui->Append(PresetCollection::separator(L("User presets")), wxNullBitmap));
        for (std::map<wxString, wxBitmap*>::iterator it = nonsys_presets.begin(); it != nonsys_presets.end(); ++it) {
            ui->Append(it->first, *it->second);
            if (it->first == selected)
                selected_preset_item = ui->GetCount() - 1;
        }
    }
    if (m_type == Preset::TYPE_PRINTER) {
        std::string   bitmap_key = "";
        // If the filament preset is not compatible and there is a "red flag" icon loaded, show it left
        // to the filament color image.
        if (wide_icons)
            bitmap_key += "wide,";
        bitmap_key += "add_printer";
        wxBitmap     *bmp = m_bitmap_cache->find(bitmap_key);
        if (bmp == nullptr) {
            // Create the bitmap with color bars.
            std::vector<wxBitmap> bmps;
            if (wide_icons)
                // Paint a red flag for incompatible presets.
                bmps.emplace_back(m_bitmap_cache->mkclear(icon_width, icon_height));
            // Paint the color bars.
            bmps.emplace_back(m_bitmap_cache->mkclear(thin_space_icon_width, icon_height));
            bmps.emplace_back(*m_bitmap_main_frame);
            // Paint a lock at the system presets.
            bmps.emplace_back(m_bitmap_cache->mkclear(wide_space_icon_width, icon_height));
            bmps.emplace_back(m_bitmap_add ? *m_bitmap_add : wxNullBitmap);
            bmp = m_bitmap_cache->insert(bitmap_key, bmps);
        }
        ui->set_label_marker(ui->Append(PresetCollection::separator(L("Add a new printer")), *bmp), GUI::PresetComboBox::LABEL_ITEM_CONFIG_WIZARD);
    }

    ui->SetSelection(selected_preset_item);
    ui->SetToolTip(ui->GetString(selected_preset_item));
    ui->check_selection();
    ui->Thaw();

    // Update control min size after rescale (changed Display DPI under MSW)
    if (ui->GetMinWidth() != 20 * ui->em_unit())
        ui->SetMinSize(wxSize(20 * ui->em_unit(), ui->GetSize().GetHeight()));
}

size_t PresetCollection::update_tab_ui(wxBitmapComboBox *ui, bool show_incompatible, const int em/* = 10*/)
{
    if (ui == nullptr)
        return 0;
    ui->Freeze();
    ui->Clear();
    size_t selected_preset_item = 0;

    /* It's supposed that standard size of an icon is 16px*16px for 100% scaled display.
    * So set sizes for solid_colored(empty) icons used for preset
    * and scale them in respect to em_unit value
    */
    const float scale_f = em * 0.1f;
    const int icon_height = 16 * scale_f + 0.5f;
    const int icon_width  = 16 * scale_f + 0.5f;

    std::map<wxString, wxBitmap*> nonsys_presets;
    wxString selected = "";
    if (!this->m_presets.front().is_visible)
        ui->Append(PresetCollection::separator(L("System presets")), wxNullBitmap);
    for (size_t i = this->m_presets.front().is_visible ? 0 : m_num_default_presets; i < this->m_presets.size(); ++i) {
        const Preset &preset = this->m_presets[i];
        if (! preset.is_visible || (! show_incompatible && ! preset.is_compatible && i != m_idx_selected))
            continue;
        std::string   bitmap_key = "tab";
        bitmap_key += preset.is_compatible ? ",cmpt" : ",ncmpt";
        bitmap_key += (preset.is_system || preset.is_default) ? ",syst" : ",nsyst";
        wxBitmap     *bmp = m_bitmap_cache->find(bitmap_key);
        if (bmp == nullptr) {
            // Create the bitmap with color bars.
            std::vector<wxBitmap> bmps;
            const wxBitmap* tmp_bmp = preset.is_compatible ? m_bitmap_compatible : m_bitmap_incompatible;
            bmps.emplace_back((tmp_bmp == 0) ? (m_bitmap_main_frame ? *m_bitmap_main_frame : wxNullBitmap) : *tmp_bmp);
            // Paint a lock at the system presets.
            bmps.emplace_back((preset.is_system || preset.is_default) ? *m_bitmap_lock : m_bitmap_cache->mkclear(icon_width, icon_height));
            bmp = m_bitmap_cache->insert(bitmap_key, bmps);
        }

        if (preset.is_default || preset.is_system) {
            ui->Append(wxString::FromUTF8((preset.name + (preset.is_dirty ? g_suffix_modified : "")).c_str()),
                (bmp == 0) ? (m_bitmap_main_frame ? *m_bitmap_main_frame : wxNullBitmap) : *bmp);
            if (i == m_idx_selected)
                selected_preset_item = ui->GetCount() - 1;
        }
        else
        {
            nonsys_presets.emplace(wxString::FromUTF8((preset.name + (preset.is_dirty ? g_suffix_modified : "")).c_str()), bmp/*preset.is_compatible*/);
            if (i == m_idx_selected)
                selected = wxString::FromUTF8((preset.name + (preset.is_dirty ? g_suffix_modified : "")).c_str());
        }
        if (i + 1 == m_num_default_presets)
            ui->Append(PresetCollection::separator(L("System presets")), wxNullBitmap);
    }
    if (!nonsys_presets.empty())
    {
        ui->Append(PresetCollection::separator(L("User presets")), wxNullBitmap);
        for (std::map<wxString, wxBitmap*>::iterator it = nonsys_presets.begin(); it != nonsys_presets.end(); ++it) {
            ui->Append(it->first, *it->second);
            if (it->first == selected)
                selected_preset_item = ui->GetCount() - 1;
        }
    }
    if (m_type == Preset::TYPE_PRINTER) {
        wxBitmap *bmp = m_bitmap_cache->find("add_printer_tab");
        if (bmp == nullptr) {
            // Create the bitmap with color bars.
            std::vector<wxBitmap> bmps;
            bmps.emplace_back(*m_bitmap_main_frame);
            bmps.emplace_back(m_bitmap_add ? *m_bitmap_add : wxNullBitmap);
            bmp = m_bitmap_cache->insert("add_printer_tab", bmps);
        }
        ui->Append(PresetCollection::separator("Add a new printer"), *bmp);
    }
    ui->SetSelection(selected_preset_item);
    ui->SetToolTip(ui->GetString(selected_preset_item));
    ui->Thaw();
    return selected_preset_item;
}

// Update a dirty floag of the current preset, update the labels of the UI component accordingly.
// Return true if the dirty flag changed.
bool PresetCollection::update_dirty_ui(wxBitmapComboBox *ui)
{
    wxWindowUpdateLocker noUpdates(ui);
    // 1) Update the dirty flag of the current preset.
    bool was_dirty = this->get_selected_preset().is_dirty;
    bool is_dirty  = current_is_dirty();
    this->get_selected_preset().is_dirty = is_dirty;
    this->get_edited_preset().is_dirty = is_dirty;
    // 2) Update the labels.
    for (unsigned int ui_id = 0; ui_id < ui->GetCount(); ++ ui_id) {
        std::string   old_label    = ui->GetString(ui_id).utf8_str().data();
        std::string   preset_name  = Preset::remove_suffix_modified(old_label);
        const Preset *preset       = this->find_preset(preset_name, false);
//		The old_label could be the "----- system presets ------" or the "------- user presets --------" separator.
//      assert(preset != nullptr);
        if (preset != nullptr) {
            std::string new_label = preset->is_dirty ? preset->name + g_suffix_modified : preset->name;
            if (old_label != new_label)
                ui->SetString(ui_id, wxString::FromUTF8(new_label.c_str()));
        }
    }
#ifdef __APPLE__
    // wxWidgets on OSX do not upload the text of the combo box line automatically.
    // Force it to update by re-selecting.
    ui->SetSelection(ui->GetSelection());
#endif /* __APPLE __ */
    return was_dirty != is_dirty;
}

template<class T>
void add_correct_opts_to_diff(const std::string &opt_key, t_config_option_keys& vec, const ConfigBase &other, const ConfigBase &this_c)
{
    const T* opt_init = static_cast<const T*>(other.option(opt_key));
    const T* opt_cur = static_cast<const T*>(this_c.option(opt_key));
    int opt_init_max_id = opt_init->values.size() - 1;
    for (int i = 0; i < opt_cur->values.size(); i++)
    {
        int init_id = i <= opt_init_max_id ? i : 0;
        if (opt_cur->values[i] != opt_init->values[init_id])
            vec.emplace_back(opt_key + "#" + std::to_string(i));
    }
}

// Use deep_diff to correct return of changed options, considering individual options for each extruder.
inline t_config_option_keys deep_diff(const ConfigBase &config_this, const ConfigBase &config_other)
{
    t_config_option_keys diff;
    for (const t_config_option_key &opt_key : config_this.keys()) {
        const ConfigOption *this_opt  = config_this.option(opt_key);
        const ConfigOption *other_opt = config_other.option(opt_key);
        if (this_opt != nullptr && other_opt != nullptr && *this_opt != *other_opt)
        {
            if (opt_key == "bed_shape" || opt_key == "compatible_prints" || opt_key == "compatible_printers") {
                diff.emplace_back(opt_key);
                continue;
            }
            switch (other_opt->type())
            {
            case coInts:    add_correct_opts_to_diff<ConfigOptionInts       >(opt_key, diff, config_other, config_this);  break;
            case coBools:   add_correct_opts_to_diff<ConfigOptionBools      >(opt_key, diff, config_other, config_this);  break;
            case coFloats:  add_correct_opts_to_diff<ConfigOptionFloats     >(opt_key, diff, config_other, config_this);  break;
            case coStrings: add_correct_opts_to_diff<ConfigOptionStrings    >(opt_key, diff, config_other, config_this);  break;
            case coPercents:add_correct_opts_to_diff<ConfigOptionPercents   >(opt_key, diff, config_other, config_this);  break;
            case coPoints:  add_correct_opts_to_diff<ConfigOptionPoints     >(opt_key, diff, config_other, config_this);  break;
            default:        diff.emplace_back(opt_key);     break;
            }
        }
    }
    return diff;
}

std::vector<std::string> PresetCollection::dirty_options(const Preset *edited, const Preset *reference, const bool deep_compare /*= false*/)
{
    std::vector<std::string> changed;
    if (edited != nullptr && reference != nullptr) {
        changed = deep_compare ?
                deep_diff(edited->config, reference->config) :
                reference->config.diff(edited->config);
        // The "compatible_printers" option key is handled differently from the others:
        // It is not mandatory. If the key is missing, it means it is compatible with any printer.
        // If the key exists and it is empty, it means it is compatible with no printer.
        std::initializer_list<const char*> optional_keys { "compatible_prints", "compatible_printers" };
        for (auto &opt_key : optional_keys) {
            if (reference->config.has(opt_key) != edited->config.has(opt_key))
                changed.emplace_back(opt_key);
        }
    }
    return changed;
}

// Select a new preset. This resets all the edits done to the currently selected preset.
// If the preset with index idx does not exist, a first visible preset is selected.
Preset& PresetCollection::select_preset(size_t idx)
{
    for (Preset &preset : m_presets)
        preset.is_dirty = false;
    if (idx >= m_presets.size())
        idx = first_visible_idx();
    m_idx_selected = idx;
    m_edited_preset = m_presets[idx];
    bool default_visible = ! m_default_suppressed || m_idx_selected < m_num_default_presets;
    for (size_t i = 0; i < m_num_default_presets; ++i)
        m_presets[i].is_visible = default_visible;
    return m_presets[idx];
}

bool PresetCollection::select_preset_by_name(const std::string &name_w_suffix, bool force)
{
    std::string name = Preset::remove_suffix_modified(name_w_suffix);
    // 1) Try to find the preset by its name.
    auto it = this->find_preset_internal(name);
    size_t idx = 0;
    if (it != m_presets.end() && it->name == name && it->is_visible)
        // Preset found by its name and it is visible.
        idx = it - m_presets.begin();
    else {
        // Find the first visible preset.
        for (size_t i = m_default_suppressed ? m_num_default_presets : 0; i < m_presets.size(); ++ i)
            if (m_presets[i].is_visible) {
                idx = i;
                break;
            }
        // If the first visible preset was not found, return the 0th element, which is the default preset.
    }

    // 2) Select the new preset.
    if (m_idx_selected != idx || force) {
        this->select_preset(idx);
        return true;
    }

    return false;
}

bool PresetCollection::select_preset_by_name_strict(const std::string &name)
{
    // 1) Try to find the preset by its name.
    auto it = this->find_preset_internal(name);
    size_t idx = (size_t)-1;
    if (it != m_presets.end() && it->name == name && it->is_visible)
        // Preset found by its name.
        idx = it - m_presets.begin();
    // 2) Select the new preset.
    if (idx != (size_t)-1) {
        this->select_preset(idx);
        return true;
    }
    m_idx_selected = idx;
    return false;
}

// Merge one vendor's presets with the other vendor's presets, report duplicates.
std::vector<std::string> PresetCollection::merge_presets(PresetCollection &&other, const std::set<VendorProfile> &new_vendors)
{
    std::vector<std::string> duplicates;
    for (Preset &preset : other.m_presets) {
        if (preset.is_default || preset.is_external)
            continue;
        Preset key(m_type, preset.name);
        auto it = std::lower_bound(m_presets.begin() + m_num_default_presets, m_presets.end(), key);
        if (it == m_presets.end() || it->name != preset.name) {
            if (preset.vendor != nullptr) {
                // Re-assign a pointer to the vendor structure in the new PresetBundle.
                auto it = new_vendors.find(*preset.vendor);
                assert(it != new_vendors.end());
                preset.vendor = &(*it);
            }
            this->m_presets.emplace(it, std::move(preset));
        } else
            duplicates.emplace_back(std::move(preset.name));
    }
    return duplicates;
}

std::string PresetCollection::name() const
{
    switch (this->type()) {
    case Preset::TYPE_PRINT:        return L("print");
    case Preset::TYPE_FILAMENT:     return L("filament");
    case Preset::TYPE_SLA_PRINT:    return L("SLA print");
    case Preset::TYPE_SLA_MATERIAL: return L("SLA material");
    case Preset::TYPE_PRINTER:      return L("printer");
    default:                        return "invalid";
    }
}

std::string PresetCollection::section_name() const
{
    switch (this->type()) {
    case Preset::TYPE_PRINT:        return "print";
    case Preset::TYPE_FILAMENT:     return "filament";
    case Preset::TYPE_SLA_PRINT:    return "sla_print";
    case Preset::TYPE_SLA_MATERIAL: return "sla_material";
    case Preset::TYPE_PRINTER:      return "printer";
    default:                        return "invalid";
    }
}

std::vector<std::string> PresetCollection::system_preset_names() const
{
    size_t num = 0;
    for (const Preset &preset : m_presets)
        if (preset.is_system)
            ++ num;
    std::vector<std::string> out;
    out.reserve(num);
    for (const Preset &preset : m_presets)
        if (preset.is_system)
            out.emplace_back(preset.name);
    std::sort(out.begin(), out.end());
    return out;
}

// Generate a file path from a profile name. Add the ".ini" suffix if it is missing.
std::string PresetCollection::path_from_name(const std::string &new_name) const
{
    std::string file_name = boost::iends_with(new_name, ".ini") ? new_name : (new_name + ".ini");
    return (boost::filesystem::path(m_dir_path) / file_name).make_preferred().string();
}

void PresetCollection::clear_bitmap_cache()
{
    m_bitmap_cache->clear();
}

wxString PresetCollection::separator(const std::string &label)
{
    return wxString::FromUTF8(PresetCollection::separator_head()) + _(label) + wxString::FromUTF8(PresetCollection::separator_tail());
}

const Preset& PrinterPresetCollection::default_preset_for(const DynamicPrintConfig &config) const
{
    const ConfigOptionEnumGeneric *opt_printer_technology = config.opt<ConfigOptionEnumGeneric>("printer_technology");
    return this->default_preset((opt_printer_technology == nullptr || opt_printer_technology->value == ptFFF) ? 0 : 1);
}

const Preset* PrinterPresetCollection::find_by_model_id(const std::string &model_id) const
{
    if (model_id.empty()) { return nullptr; }

    const auto it = std::find_if(cbegin(), cend(), [&](const Preset &preset) {
        return preset.config.opt_string("printer_model") == model_id;
    });

    return it != cend() ? &*it : nullptr;
}

} // namespace Slic3r
