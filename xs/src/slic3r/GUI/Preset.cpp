//#undef NDEBUG
#include <cassert>

#include "Preset.hpp"
#include "AppConfig.hpp"
#include "BitmapCache.hpp"

#include <fstream>
#include <stdexcept>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <boost/nowide/cenv.hpp>
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

#include "../../libslic3r/libslic3r.h"
#include "../../libslic3r/Utils.hpp"
#include "../../libslic3r/PlaceholderParser.hpp"

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

    auto config_update_url = vendor_section.find("config_update_url");
    if (config_update_url != vendor_section.not_found()) {
        res.config_update_url = config_update_url->second.data();
    }

    if (! load_all) {
        return res;
    }

    for (auto &section : tree) {
        if (boost::starts_with(section.first, printer_model_key)) {
            VendorProfile::PrinterModel model;
            model.id = section.first.substr(printer_model_key.size());
            model.name = section.second.get<std::string>("name", model.id);
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
        auto *opt = config.option(key, false);
        assert(opt != nullptr);
        assert(opt->is_vector());
        if (opt != nullptr && opt->is_vector() && key != "default_filament_profile")
            static_cast<ConfigOptionVectorBase*>(opt)->resize(num_extruders, defaults.option(key));
    }
}

// Update new extruder fields at the printer profile.
void Preset::normalize(DynamicPrintConfig &config)
{
    auto *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(config.option("nozzle_diameter"));
    if (nozzle_diameter != nullptr)
        // Loaded the Printer settings. Verify, that all extruder dependent values have enough values.
        set_num_extruders(config, (unsigned int)nozzle_diameter->values.size());
    if (config.option("filament_diameter") != nullptr) {
        // This config contains single or multiple filament presets.
        // Ensure that the filament preset vector options contain the correct number of values.
        size_t n = (nozzle_diameter == nullptr) ? 1 : nozzle_diameter->values.size();
        const auto &defaults = FullPrintConfig::defaults();
        for (const std::string &key : Preset::filament_options()) {
			if (key == "compatible_printers")
				continue;
            auto *opt = config.option(key, false);
            assert(opt != nullptr);
            assert(opt->is_vector());
            if (opt != nullptr && opt->is_vector())
                static_cast<ConfigOptionVectorBase*>(opt)->resize(n, defaults.option(key));
        }
        // The following keys are mandatory for the UI, but they are not part of FullPrintConfig, therefore they are handled separately.
        for (const std::string &key : { "filament_settings_id" }) {
            auto *opt = config.option(key, false);
            assert(opt != nullptr);
            assert(opt->type() == coStrings);
            if (opt != nullptr && opt->type() == coStrings)
                static_cast<ConfigOptionStrings*>(opt)->values.resize(n, std::string());
        }
    }
}

// Load a config file, return a C++ class Slic3r::DynamicPrintConfig with $keys initialized from the config file.
// In case of a "default" config item, return the default values.
DynamicPrintConfig& Preset::load(const std::vector<std::string> &keys)
{
    // Set the configuration from the defaults.
    Slic3r::FullPrintConfig defaults;
    this->config.apply_only(defaults, keys.empty() ? defaults.keys() : keys);
    if (! this->is_default) {
        // Load the preset file, apply preset values on top of defaults.
        try {
            this->config.load_from_ini(this->file);
            Preset::normalize(this->config);
        } catch (const std::ifstream::failure &err) {
            throw std::runtime_error(std::string("The selected preset cannot be loaded: ") + this->file + "\n\tReason: " + err.what());
        } catch (const std::runtime_error &err) {
            throw std::runtime_error(std::string("Failed loading the preset file: ") + this->file + "\n\tReason: " + err.what());
        }
    }
    this->loaded = true;
    return this->config;
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
    config.set_key_value("num_extruders", new ConfigOptionInt(
        (int)static_cast<const ConfigOptionFloats*>(active_printer.config.option("nozzle_diameter"))->values.size()));
    return this->is_compatible_with_printer(active_printer, &config);
}

bool Preset::update_compatible_with_printer(const Preset &active_printer, const DynamicPrintConfig *extra_config)
{
    return this->is_compatible = is_compatible_with_printer(active_printer, extra_config);
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
        "layer_height", "first_layer_height", "perimeters", "spiral_vase", "top_solid_layers", "bottom_solid_layers", 
        "extra_perimeters", "ensure_vertical_shell_thickness", "avoid_crossing_perimeters", "thin_walls", "overhangs", 
        "seam_position", "external_perimeters_first", "fill_density", "fill_pattern", "external_fill_pattern", 
        "infill_every_layers", "infill_only_where_needed", "solid_infill_every_layers", "fill_angle", "bridge_angle", 
        "solid_infill_below_area", "only_retract_when_crossing_perimeters", "infill_first", "max_print_speed", 
        "max_volumetric_speed", "max_volumetric_extrusion_rate_slope_positive", "max_volumetric_extrusion_rate_slope_negative", 
        "perimeter_speed", "small_perimeter_speed", "external_perimeter_speed", "infill_speed", "solid_infill_speed", 
        "top_solid_infill_speed", "support_material_speed", "support_material_xy_spacing", "support_material_interface_speed",
        "bridge_speed", "gap_fill_speed", "travel_speed", "first_layer_speed", "perimeter_acceleration", "infill_acceleration", 
        "bridge_acceleration", "first_layer_acceleration", "default_acceleration", "skirts", "skirt_distance", "skirt_height",
        "min_skirt_length", "brim_width", "support_material", "support_material_threshold", "support_material_enforce_layers", 
        "raft_layers", "support_material_pattern", "support_material_with_sheath", "support_material_spacing", 
        "support_material_synchronize_layers", "support_material_angle", "support_material_interface_layers", 
        "support_material_interface_spacing", "support_material_interface_contact_loops", "support_material_contact_distance", 
        "support_material_buildplate_only", "dont_support_bridges", "notes", "complete_objects", "extruder_clearance_radius", 
        "extruder_clearance_height", "gcode_comments", "output_filename_format", "post_process", "perimeter_extruder", 
        "infill_extruder", "solid_infill_extruder", "support_material_extruder", "support_material_interface_extruder", 
        "ooze_prevention", "standby_temperature_delta", "interface_shells", "extrusion_width", "first_layer_extrusion_width", 
        "perimeter_extrusion_width", "external_perimeter_extrusion_width", "infill_extrusion_width", "solid_infill_extrusion_width", 
        "top_infill_extrusion_width", "support_material_extrusion_width", "infill_overlap", "bridge_flow_ratio", "clip_multipart_objects", 
        "elefant_foot_compensation", "xy_size_compensation", "threads", "resolution", "wipe_tower", "wipe_tower_x", "wipe_tower_y",
        "wipe_tower_width", "wipe_tower_rotation_angle", "wipe_tower_bridging", "single_extruder_multi_material_priming", 
        "compatible_printers", "compatible_printers_condition","inherits"
    };
    return s_opts;
}

const std::vector<std::string>& Preset::filament_options()
{    
    static std::vector<std::string> s_opts {
        "filament_colour", "filament_diameter", "filament_type", "filament_soluble", "filament_notes", "filament_max_volumetric_speed",
        "extrusion_multiplier", "filament_density", "filament_cost", 
        "filament_loading_speed", "filament_load_time", "filament_unloading_speed", "filament_unload_time", "filament_toolchange_delay",
        "filament_cooling_moves", "filament_cooling_initial_speed", "filament_cooling_final_speed", "filament_ramming_parameters",
        "filament_minimal_purge_on_wipe_tower", "temperature", "first_layer_temperature", "bed_temperature", "first_layer_bed_temperature",
        "fan_always_on", "cooling", "min_fan_speed", "max_fan_speed", "bridge_fan_speed", "disable_fan_first_layers", "fan_below_layer_time",
        "slowdown_below_layer_time", "min_print_speed", "start_filament_gcode", "end_filament_gcode","compatible_printers", "compatible_printers_condition",
        "inherits"
    };
    return s_opts;
}

const std::vector<std::string>& Preset::printer_options()
{    
    static std::vector<std::string> s_opts;
    if (s_opts.empty()) {
        s_opts = {
            "bed_shape", "z_offset", "gcode_flavor", "use_relative_e_distances", "serial_port", "serial_speed", 
            "octoprint_host", "octoprint_apikey", "octoprint_cafile", "use_firmware_retraction", "use_volumetric_e", "variable_layer_height",
            "single_extruder_multi_material", "start_gcode", "end_gcode", "before_layer_gcode", "layer_gcode", "toolchange_gcode",
            "between_objects_gcode", "printer_vendor", "printer_model", "printer_variant", "printer_notes", "cooling_tube_retraction",
            "cooling_tube_length", "parking_pos_retraction", "extra_loading_move", "max_print_height", "default_print_profile", "inherits",
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

PresetCollection::PresetCollection(Preset::Type type, const std::vector<std::string> &keys) :
    m_type(type),
    m_edited_preset(type, "", false),
    m_idx_selected(0),
    m_bitmap_main_frame(new wxBitmap),
	m_bitmap_cache(new GUI::BitmapCache)
{
    // Insert just the default preset.
    m_presets.emplace_back(Preset(type, "- default -", true));
    m_presets.front().load(keys);
    m_edited_preset.config.apply(m_presets.front().config);
}

PresetCollection::~PresetCollection()
{
    delete m_bitmap_main_frame;
    m_bitmap_main_frame = nullptr;
	delete m_bitmap_cache;
	m_bitmap_cache = nullptr;
}

void PresetCollection::reset(bool delete_files)
{
    if (m_presets.size() > 1) {
        if (delete_files) {
            // Erase the preset files.
            for (Preset &preset : m_presets)
                if (! preset.is_default && ! preset.is_external && ! preset.is_system)
                    boost::nowide::remove(preset.file.c_str());
        }
        // Don't use m_presets.resize() here as it requires a default constructor for Preset.
        m_presets.erase(m_presets.begin() + 1, m_presets.end());
        this->select_preset(0);
    }
}

// Load all presets found in dir_path.
// Throws an exception on error.
void PresetCollection::load_presets(const std::string &dir_path, const std::string &subdir)
{
	boost::filesystem::path dir = boost::filesystem::canonical(boost::filesystem::path(dir_path) / subdir).make_preferred();
	m_dir_path = dir.string();
    t_config_option_keys keys = this->default_preset().config.keys();
    std::string errors_cummulative;
	for (auto &dir_entry : boost::filesystem::directory_iterator(dir))
        if (boost::filesystem::is_regular_file(dir_entry.status()) && boost::algorithm::iends_with(dir_entry.path().filename().string(), ".ini")) {
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
                preset.load(keys);
                m_presets.emplace_back(preset);
            } catch (const std::runtime_error &err) {
                errors_cummulative += err.what();
                errors_cummulative += "\n";
			}
        }
    std::sort(m_presets.begin() + 1, m_presets.end());
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
    for (const char *key : { "compatible_printers", "compatible_printers_condition", "inherits", 
                             "print_settings_id", "filament_settings_id", "printer_settings_id",
                             "printer_model", "printer_variant", "default_print_profile", "default_filament_profile" })
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
    DynamicPrintConfig cfg(this->default_preset().config);
    cfg.apply_only(config, cfg.keys(), true);
    // Is there a preset already loaded with the name stored inside the config?
    std::deque<Preset>::iterator it = this->find_preset_internal(original_name);
    if (it != m_presets.end() && it->name == original_name && profile_print_params_same(it->config, cfg)) {
        // The preset exists and it matches the values stored inside config.
        if (select)
            this->select_preset(it - m_presets.begin());
        return *it;
    }
    // Update the "inherits" field.
    std::string &inherits = Preset::inherits(cfg);
    if (it != m_presets.end() && inherits.empty()) {
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
    }
	// 2) Activate the saved preset.
	this->select_preset_by_name(new_name, true);
	// 2) Store the active preset to disk.
	this->get_selected_preset().save();
}

void PresetCollection::delete_current_preset()
{
    const Preset &selected = this->get_selected_preset();
    if (selected.is_default)
        return;
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
}

bool PresetCollection::load_bitmap_default(const std::string &file_name)
{
    return m_bitmap_main_frame->LoadFile(wxString::FromUTF8(Slic3r::var(file_name).c_str()), wxBITMAP_TYPE_PNG);
}

const Preset* PresetCollection::get_selected_preset_parent() const
{
    const std::string &inherits = this->get_edited_preset().inherits();
    if (inherits.empty())
		return this->get_selected_preset().is_system ? &this->get_selected_preset() : nullptr; 
    const Preset* preset = this->find_preset(inherits, false);
    return (preset == nullptr || preset->is_default || preset->is_external) ? nullptr : preset;
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
    size_t idx = m_default_suppressed ? 1 : 0;
    for (; idx < this->m_presets.size(); ++ idx)
        if (m_presets[idx].is_visible)
            break;
    if (idx == this->m_presets.size())
        idx = 0;
    return idx;
}

void PresetCollection::set_default_suppressed(bool default_suppressed)
{
    if (m_default_suppressed != default_suppressed) {
        m_default_suppressed = default_suppressed;
        m_presets.front().is_visible = ! default_suppressed || (m_presets.size() > 1 && m_idx_selected > 0);
    }
}

size_t PresetCollection::update_compatible_with_printer_internal(const Preset &active_printer, bool unselect_if_incompatible)
{
    DynamicPrintConfig config;
    config.set_key_value("printer_preset", new ConfigOptionString(active_printer.name));
    config.set_key_value("num_extruders", new ConfigOptionInt(
        (int)static_cast<const ConfigOptionFloats*>(active_printer.config.option("nozzle_diameter"))->values.size()));
    for (size_t idx_preset = 1; idx_preset < m_presets.size(); ++ idx_preset) {
        bool    selected        = idx_preset == m_idx_selected;
        Preset &preset_selected = m_presets[idx_preset];
        Preset &preset_edited   = selected ? m_edited_preset : preset_selected;
        if (! preset_edited.update_compatible_with_printer(active_printer, &config) &&
            selected && unselect_if_incompatible)
            m_idx_selected = (size_t)-1;
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
void PresetCollection::update_platter_ui(wxBitmapComboBox *ui)
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
	bool wide_icons = !selected_preset.is_compatible && m_bitmap_incompatible != nullptr;

	std::map<wxString, wxBitmap*> nonsys_presets;
	wxString selected = "";
	if (!this->m_presets.front().is_visible)
		ui->Append("------- " +_(L("System presets")) + " -------", wxNullBitmap);
	for (size_t i = this->m_presets.front().is_visible ? 0 : 1; i < this->m_presets.size(); ++i) {
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
				bmps.emplace_back(preset.is_compatible ? m_bitmap_cache->mkclear(16, 16) : *m_bitmap_incompatible);
			// Paint the color bars.
			bmps.emplace_back(m_bitmap_cache->mkclear(4, 16));
			bmps.emplace_back(*m_bitmap_main_frame);
			// Paint a lock at the system presets.
 			bmps.emplace_back(m_bitmap_cache->mkclear(6, 16));
			bmps.emplace_back((preset.is_system || preset.is_default) ? *m_bitmap_lock : m_bitmap_cache->mkclear(16, 16));
			bmp = m_bitmap_cache->insert(bitmap_key, bmps);
		}

		if (preset.is_default || preset.is_system){
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
		if (preset.is_default)
			ui->Append("------- " + _(L("System presets")) + " -------", wxNullBitmap);
	}
	if (!nonsys_presets.empty())
	{
		ui->Append("-------  " + _(L("User presets")) + "  -------", wxNullBitmap);
		for (std::map<wxString, wxBitmap*>::iterator it = nonsys_presets.begin(); it != nonsys_presets.end(); ++it) {
			ui->Append(it->first, *it->second);
			if (it->first == selected)
				selected_preset_item = ui->GetCount() - 1;
		}
	}

	ui->SetSelection(selected_preset_item);
	ui->SetToolTip(ui->GetString(selected_preset_item));
	ui->Thaw();
}

size_t PresetCollection::update_tab_ui(wxBitmapComboBox *ui, bool show_incompatible)
{
    if (ui == nullptr)
        return 0;
    ui->Freeze();
    ui->Clear();
	size_t selected_preset_item = 0;

	std::map<wxString, wxBitmap*> nonsys_presets;
	wxString selected = "";
	if (!this->m_presets.front().is_visible)
		ui->Append("------- " + _(L("System presets")) + " -------", wxNullBitmap);
	for (size_t i = this->m_presets.front().is_visible ? 0 : 1; i < this->m_presets.size(); ++i) {
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
			bmps.emplace_back((preset.is_system || preset.is_default) ? *m_bitmap_lock : m_bitmap_cache->mkclear(16, 16));
			bmp = m_bitmap_cache->insert(bitmap_key, bmps);
		}

		if (preset.is_default || preset.is_system){
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
		if (preset.is_default)
			ui->Append("------- " + _(L("System presets")) + " -------", wxNullBitmap);
    }
	if (!nonsys_presets.empty())
	{
		ui->Append("-------  " + _(L("User presets")) + "  -------", wxNullBitmap);
		for (std::map<wxString, wxBitmap*>::iterator it = nonsys_presets.begin(); it != nonsys_presets.end(); ++it) {
			ui->Append(it->first, *it->second);
			if (it->first == selected)
				selected_preset_item = ui->GetCount() - 1;
		}
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
        assert(preset != nullptr);
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

std::vector<std::string> PresetCollection::dirty_options(const Preset *edited, const Preset *reference, const bool is_printer_type /*= false*/)
{
    std::vector<std::string> changed;
	if (edited != nullptr && reference != nullptr) {
        changed = is_printer_type  ? 
				reference->config.deep_diff(edited->config) :
				reference->config.diff(edited->config);
        // The "compatible_printers" option key is handled differently from the others:
        // It is not mandatory. If the key is missing, it means it is compatible with any printer.
        // If the key exists and it is empty, it means it is compatible with no printer.
        std::initializer_list<const char*> optional_keys { "compatible_printers" };
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
    m_presets.front().is_visible = ! m_default_suppressed || m_idx_selected == 0;
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
        for (size_t i = m_default_suppressed ? 1 : 0; i < m_presets.size(); ++ i)
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
        auto it = std::lower_bound(m_presets.begin() + 1, m_presets.end(), key);
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
    case Preset::TYPE_PRINT:    return "print";
    case Preset::TYPE_FILAMENT: return "filament";
    case Preset::TYPE_PRINTER:  return "printer";
    default:                    return "invalid";
    }
}

// Generate a file path from a profile name. Add the ".ini" suffix if it is missing.
std::string PresetCollection::path_from_name(const std::string &new_name) const
{
	std::string file_name = boost::iends_with(new_name, ".ini") ? new_name : (new_name + ".ini");
    return (boost::filesystem::path(m_dir_path) / file_name).make_preferred().string();
}

} // namespace Slic3r
