#include "Preset.hpp"

#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <wx/image.h>
#include <wx/bmpcbox.h>

#if 0
#define DEBUG
#define _DEBUG
#undef NDEBUG
#endif

#include <assert.h>

namespace Slic3r {

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
            if (boost::algorithm::iends_with(this->file, ".gcode") || boost::algorithm::iends_with(this->file, ".g"))
                this->config.load_from_gcode(this->file);
            else
                this->config.load(this->file);
        } catch (const std::ifstream::failure&) {
            throw std::runtime_error(std::string("The selected preset does not exist anymore: ") + this->file);
        } catch (const std::runtime_error&) {
            throw std::runtime_error(std::string("Failed loading the preset file: ") + this->file);
        }

        if (this->type == TYPE_PRINTER && std::find(keys.begin(), keys.end(), "nozzle_diameter") != keys.end()) {
            // Loaded the Printer settings. Verify, that all extruder dependent values have enough values.
            auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(this->config.option("nozzle_diameter"));
            size_t  num_extruders   = nozzle_diameter->values.size();
            auto   *deretract_speed = dynamic_cast<ConfigOptionFloats*>(this->config.option("deretract_speed"));
            deretract_speed->values.resize(num_extruders, deretract_speed->values.empty() ? 
                defaults.deretract_speed.values.front() : deretract_speed->values.front());
            auto   *extruder_colour = dynamic_cast<ConfigOptionStrings*>(this->config.option("extruder_colour"));
            extruder_colour->values.resize(num_extruders, extruder_colour->values.empty() ? 
                defaults.extruder_colour.values.front() : extruder_colour->values.front());
            auto   *retract_before_wipe = dynamic_cast<ConfigOptionPercents*>(this->config.option("retract_before_wipe"));
            retract_before_wipe->values.resize(num_extruders, retract_before_wipe->values.empty() ? 
                defaults.retract_before_wipe.values.front() : retract_before_wipe->values.front());
        }
    }

    this->loaded = true;
    return this->config;
}

bool Preset::enable_compatible(const std::string &active_printer)
{
    auto *compatible_printers = dynamic_cast<const ConfigOptionStrings*>(this->config.optptr("compatible_printers"));
    this->is_visible = compatible_printers && ! compatible_printers->values.empty() &&
        std::find(compatible_printers->values.begin(), compatible_printers->values.end(), active_printer) != 
            compatible_printers->values.end();
    return this->is_visible;
}

PresetCollection::PresetCollection(Preset::Type type, const std::vector<std::string> &keys) :
    m_type(type),
    m_edited_preset(type, "", false)
{
    // Insert just the default preset.
    m_presets.emplace_back(Preset(type, "- default -", true));
    m_presets.front().load(keys);
}

// Load all presets found in dir_path.
// Throws an exception on error.
void PresetCollection::load_presets(const std::string &dir_path, const std::string &subdir)
{
    m_presets.erase(m_presets.begin()+1, m_presets.end());
    t_config_option_keys keys = this->default_preset().config.keys();
	for (auto &file : boost::filesystem::directory_iterator(boost::filesystem::canonical(boost::filesystem::path(dir_path) / subdir).make_preferred()))
        if (boost::filesystem::is_regular_file(file.status()) && boost::algorithm::iends_with(file.path().filename().string(), ".ini")) {
            std::string name = file.path().filename().string();
            // Remove the .ini suffix.
            name.erase(name.size() - 4);
            try {
                Preset preset(m_type, name, false);
                preset.file = file.path().string();
                preset.load(keys);
                m_presets.emplace_back(preset);
            } catch (const boost::filesystem::filesystem_error &err) {

            }
        }
}

void PresetCollection::set_default_suppressed(bool default_suppressed)
{
    if (m_default_suppressed != default_suppressed) {
        m_default_suppressed = default_suppressed;
        m_presets.front().is_visible = ! default_suppressed || m_presets.size() > 1;
    }
}

void PresetCollection::enable_disable_compatible_to_printer(const std::string &active_printer)
{
    size_t num_visible = 0;
    for (size_t idx_preset = 1; idx_preset < m_presets.size(); ++ idx_preset)
        if (m_presets[idx_preset].enable_compatible(active_printer))
            ++ num_visible;
    if (num_visible == 0)
        // Show the "-- default --" preset.
        m_presets.front().is_visible = true;
}

static std::string g_suffix_modified = " (modified)";

// Update the wxChoice UI component from this list of presets.
// Hide the 
void PresetCollection::update_editor_ui(wxBitmapComboBox *ui)
{
    if (ui == nullptr)
        return;

    size_t      n_visible       = this->num_visible();
    size_t      n_choice        = size_t(ui->GetCount());
    std::string name_selected   = dynamic_cast<wxItemContainerImmutable*>(ui)->GetStringSelection().ToUTF8().data();
    if (boost::algorithm::iends_with(name_selected, g_suffix_modified))
        // Remove the g_suffix_modified.
        name_selected.erase(name_selected.end() - g_suffix_modified.size(), name_selected.end());
#if 0
    if (std::abs(int(n_visible) - int(n_choice)) <= 1) {
        // The number of items differs by at most one, update the choice.
        size_t i_preset = 0;
        size_t i_ui     = 0;
        while (i_preset < presets.size()) {
            std::string name_ui = ui->GetString(i_ui).ToUTF8();
            if (boost::algorithm::iends_with(name_ui, g_suffix_modified))
                // Remove the g_suffix_modified.
                name_ui.erase(name_ui.end() - g_suffix_modified.size(), name_ui.end());
            while (this->presets[i_preset].name )
            const Preset &preset = this->presets[i_preset];
            if (preset)
        }
    } else 
#endif
    {
        // Otherwise fill in the list from scratch.
        ui->Clear();
        for (size_t i = this->m_presets.front().is_visible ? 0 : 1; i < this->m_presets.size(); ++ i) {
            const Preset &preset = this->m_presets[i];
            const wxBitmap *bmp = (i == 0 || preset.is_visible) ? m_bitmap_compatible : m_bitmap_incompatible;
            ui->Append(wxString::FromUTF8((preset.name + (preset.is_dirty ? g_suffix_modified : "")).c_str()), (bmp == 0) ? wxNullBitmap : *bmp, (void*)i);
            if (name_selected == preset.name)
                ui->SetSelection(ui->GetCount() - 1);
        }
    }
}

void PresetCollection::update_platter_ui(wxBitmapComboBox *ui)
{
    if (ui == nullptr)
        return;

    size_t n_visible = this->num_visible();
    size_t n_choice  = size_t(ui->GetCount());
    if (std::abs(int(n_visible) - int(n_choice)) <= 1) {
        // The number of items differs by at most one, update the choice.
    } else {
        // Otherwise fill in the list from scratch.
    }
}

PresetBundle::PresetBundle() :
    prints(Preset::TYPE_PRINT, print_options()), 
    filaments(Preset::TYPE_FILAMENT, filament_options()), 
    printers(Preset::TYPE_PRINTER, printer_options()),
    m_bitmapCompatible(new wxBitmap),
    m_bitmapIncompatible(new wxBitmap)
{
    // Create the ID config keys, as they are not part of the Static print config classes.
    this->prints.preset(0).config.opt_string("print_settings_id", true);
    this->filaments.preset(0).config.opt_string("filament_settings_id", true);
    this->printers.preset(0).config.opt_string("print_settings_id", true);
    // Create the "compatible printers" keys, as they are not part of the Static print config classes.
    this->filaments.preset(0).config.optptr("compatible_printers", true);
    this->prints.preset(0).config.optptr("compatible_printers", true);
}

PresetBundle::~PresetBundle()
{
	assert(m_bitmapCompatible != nullptr);
	assert(m_bitmapIncompatible != nullptr);
	delete m_bitmapCompatible;
	m_bitmapCompatible = nullptr;
    delete m_bitmapIncompatible;
	m_bitmapIncompatible = nullptr;
    for (std::pair<const std::string, wxBitmap*> &bitmap : m_mapColorToBitmap)
        delete bitmap.second;
}

void PresetBundle::load_presets(const std::string &dir_path)
{
    this->prints.load_presets(dir_path, "print");
    this->prints.load_presets(dir_path, "filament");
    this->prints.load_presets(dir_path, "printer");
}

bool PresetBundle::load_bitmaps(const std::string &path_bitmap_compatible, const std::string &path_bitmap_incompatible)
{
    bool loaded_compatible   = m_bitmapCompatible  ->LoadFile(wxString::FromUTF8(path_bitmap_compatible.c_str()));
    bool loaded_incompatible = m_bitmapIncompatible->LoadFile(wxString::FromUTF8(path_bitmap_incompatible.c_str()));
    if (loaded_compatible) {
        prints   .set_bitmap_compatible(m_bitmapCompatible);
        filaments.set_bitmap_compatible(m_bitmapCompatible);
        printers .set_bitmap_compatible(m_bitmapCompatible);
    }
    if (loaded_incompatible) {
        prints   .set_bitmap_compatible(m_bitmapIncompatible);
        filaments.set_bitmap_compatible(m_bitmapIncompatible);
        printers .set_bitmap_compatible(m_bitmapIncompatible);        
    }
    return loaded_compatible && loaded_incompatible;
}

static inline int hex_digit_to_int(const char c)
{
    return 
        (c >= '0' && c <= '9') ? int(c - '0') : 
        (c >= 'A' && c <= 'F') ? int(c - 'A') + 10 :
        (c >= 'a' && c <= 'f') ? int(c - 'a') + 10 : -1;
}

static inline bool parse_color(const std::string &scolor, unsigned char *rgb_out)
{
    rgb_out[0] = rgb_out[1] = rgb_out[2] = 0;
    const char        *c      = scolor.data() + 1;
    if (scolor.size() != 7 || scolor.front() != '#')
        return false;
    for (size_t i = 0; i < 3; ++ i) {
        int digit1 = hex_digit_to_int(*c ++);
        int digit2 = hex_digit_to_int(*c ++);
        if (digit1 == -1 || digit2 == -1)
            return false;
        rgb_out[i] = (unsigned char)(digit1 * 16 + digit2);
    }
    return true;
}

// Update the colors preview at the platter extruder combo box.
void PresetBundle::update_platter_filament_ui_colors(wxBitmapComboBox *ui, unsigned int idx_extruder, unsigned int idx_filament)
{
    unsigned char rgb[3];
    std::string extruder_color = this->printers.get_edited_preset().config.opt_string("extruder_colour", idx_extruder);
    if (! parse_color(extruder_color, rgb))
        // Extruder color is not defined.
        extruder_color.clear();

    for (unsigned int ui_id = 0; ui_id < ui->GetCount(); ++ ui_id) {
        if (! ui->HasClientUntypedData())
            continue;
        size_t        filament_preset_id = size_t(ui->GetClientData(ui_id));
        const Preset &filament_preset    = filaments.preset(filament_preset_id);
        // Assign an extruder color to the selected item if the extruder color is defined.
        std::string   filament_rgb       = filament_preset.config.opt_string("filament_colour", 0);
        std::string   extruder_rgb       = (int(ui_id) == ui->GetSelection() && ! extruder_color.empty()) ? extruder_color : filament_rgb;
        wxBitmap     *bitmap             = nullptr;
        if (filament_rgb == extruder_rgb) {
            auto it = m_mapColorToBitmap.find(filament_rgb);
            if (it == m_mapColorToBitmap.end()) {
                // Create the bitmap.
                parse_color(filament_rgb, rgb);
                wxImage image(24, 16);
                image.SetRGB(wxRect(0, 0, 24, 16), rgb[0], rgb[1], rgb[2]);
                m_mapColorToBitmap[filament_rgb] = new wxBitmap(image);
            } else {
                bitmap = it->second;
            }
        } else {
            std::string bitmap_key = filament_rgb + extruder_rgb;
            auto it = m_mapColorToBitmap.find(bitmap_key);
            if (it == m_mapColorToBitmap.end()) {
                // Create the bitmap.
                wxImage image(24, 16);
                parse_color(extruder_rgb, rgb);
                image.SetRGB(wxRect(0, 0, 16, 16), rgb[0], rgb[1], rgb[2]);
                parse_color(filament_rgb, rgb);
                image.SetRGB(wxRect(16, 0, 8, 16), rgb[0], rgb[1], rgb[2]);
                m_mapColorToBitmap[filament_rgb] = new wxBitmap(image);
            } else {
                bitmap = it->second;
            }
        }
        ui->SetItemBitmap(ui_id, *bitmap);
    }
}

const std::vector<std::string>& PresetBundle::print_options()
{    
    const char *opts[] = { 
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
        "wipe_tower_width", "wipe_tower_per_color_wipe"
    };
    static std::vector<std::string> s_opts;
    if (s_opts.empty())
        s_opts.assign(opts, opts + (sizeof(opts) / sizeof(opts[0])));
    return s_opts;
}

const std::vector<std::string>& PresetBundle::filament_options()
{    
    const char *opts[] = {
        "filament_colour", "filament_diameter", "filament_type", "filament_soluble", "filament_notes", "filament_max_volumetric_speed", 
        "extrusion_multiplier", "filament_density", "filament_cost", "temperature", "first_layer_temperature", "bed_temperature", 
        "first_layer_bed_temperature", "fan_always_on", "cooling", "min_fan_speed", "max_fan_speed", "bridge_fan_speed", 
        "disable_fan_first_layers", "fan_below_layer_time", "slowdown_below_layer_time", "min_print_speed", "start_filament_gcode", 
        "end_filament_gcode"
    };
    static std::vector<std::string> s_opts;
    if (s_opts.empty())
        s_opts.assign(opts, opts + (sizeof(opts) / sizeof(opts[0])));
    return s_opts;
}

const std::vector<std::string>& PresetBundle::printer_options()
{    
    const char *opts[] = {
        "bed_shape", "z_offset", "gcode_flavor", "use_relative_e_distances", "serial_port", "serial_speed", 
        "octoprint_host", "octoprint_apikey", "use_firmware_retraction", "use_volumetric_e", "variable_layer_height", 
        "single_extruder_multi_material", "start_gcode", "end_gcode", "before_layer_gcode", "layer_gcode", "toolchange_gcode", 
        "nozzle_diameter", "extruder_offset", "retract_length", "retract_lift", "retract_speed", "deretract_speed", 
        "retract_before_wipe", "retract_restart_extra", "retract_before_travel", "retract_layer_change", "wipe", 
        "retract_length_toolchange", "retract_restart_extra_toolchange", "extruder_colour", "printer_notes"
    };
    static std::vector<std::string> s_opts;
    if (s_opts.empty())
        s_opts.assign(opts, opts + (sizeof(opts) / sizeof(opts[0])));
    return s_opts;
}

} // namespace Slic3r
