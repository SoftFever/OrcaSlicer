#include "Preset.hpp"

#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <boost/nowide/cenv.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include <wx/image.h>
#include <wx/choice.h>
#include <wx/bmpcbox.h>

#include "../../libslic3r/Utils.hpp"

#if 0
#define DEBUG
#define _DEBUG
#undef NDEBUG
#endif

#include <assert.h>

namespace Slic3r {

static std::string g_suffix_modified = " (modified)";

// Load keys from a config file or a G-code.
// Throw exceptions with reasonable messages if something goes wrong.
static void load_config_file(DynamicPrintConfig &config, const std::string &path)
{
    try {
        if (boost::algorithm::iends_with(path, ".gcode") || boost::algorithm::iends_with(path, ".g"))
            config.load_from_gcode(path);
        else
            config.load(path);
    } catch (const std::ifstream::failure&) {
        throw std::runtime_error(std::string("The selected preset does not exist anymore: ") + path);
    } catch (const std::runtime_error&) {
        throw std::runtime_error(std::string("Failed loading the preset file: ") + path);
    }

    // Update new extruder fields at the printer profile.
    auto keys = config.keys();
    const auto &defaults = FullPrintConfig::defaults();
    if (std::find(keys.begin(), keys.end(), "nozzle_diameter") != keys.end()) {
        // Loaded the Printer settings. Verify, that all extruder dependent values have enough values.
        auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(config.option("nozzle_diameter"));
        size_t  num_extruders   = nozzle_diameter->values.size();
        auto   *deretract_speed = dynamic_cast<ConfigOptionFloats*>(config.option("deretract_speed"));
        deretract_speed->values.resize(num_extruders, deretract_speed->values.empty() ? 
            defaults.deretract_speed.values.front() : deretract_speed->values.front());
        auto   *extruder_colour = dynamic_cast<ConfigOptionStrings*>(config.option("extruder_colour"));
        extruder_colour->values.resize(num_extruders, extruder_colour->values.empty() ? 
            defaults.extruder_colour.values.front() : extruder_colour->values.front());
        auto   *retract_before_wipe = dynamic_cast<ConfigOptionPercents*>(config.option("retract_before_wipe"));
        retract_before_wipe->values.resize(num_extruders, retract_before_wipe->values.empty() ? 
            defaults.retract_before_wipe.values.front() : retract_before_wipe->values.front());
    }
}

// Load a config file, return a C++ class Slic3r::DynamicPrintConfig with $keys initialized from the config file.
// In case of a "default" config item, return the default values.
DynamicPrintConfig& Preset::load(const std::vector<std::string> &keys)
{
    // Set the configuration from the defaults.
    Slic3r::FullPrintConfig defaults;
    this->config.apply_only(defaults, keys.empty() ? defaults.keys() : keys);
    if (! this->is_default)
        // Load the preset file, apply preset values on top of defaults.
        load_config_file(this->config, this->file);
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
    m_edited_preset(type, "", false),
    m_idx_selected(0),
    m_bitmap_main_frame(new wxBitmap)
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
    std::sort(m_presets.begin() + 1, m_presets.end(), [](const Preset &p1, const Preset &p2){ return p1.name < p2.name; });
}

// Load a preset from an already parsed config file, insert it into the sorted sequence of presets
// and select it, losing previous modifications.
Preset& PresetCollection::load_preset(const std::string &path, const std::string &name, const DynamicPrintConfig &config, bool select)
{
    Preset key(m_type, name);
    auto it = std::lower_bound(m_presets.begin(), m_presets.end(), key);
    if (it != m_presets.end() && it->name == name) {
        // The preset with the same name was found.
        it->is_dirty = false;
    } else {
        it = m_presets.emplace(it, Preset(m_type, name, false));
    }
    Preset &preset = *it;
    preset.file = path;
    preset.config = this->default_preset().config;
    preset.loaded = true;
    this->get_selected_preset().is_dirty = false;
    if (select)
        this->select_preset_by_name(name, true);
    return preset;
}

bool PresetCollection::load_bitmap_default(const std::string &file_name)
{
    return m_bitmap_main_frame->LoadFile(wxString::FromUTF8(Slic3r::var(file_name).c_str()), wxBITMAP_TYPE_PNG);
}

// Return a preset by its name. If the preset is active, a temporary copy is returned.
// If a preset is not found by its name, null is returned.
Preset* PresetCollection::find_preset(const std::string &name, bool first_visible_if_not_found)
{
    Preset key(m_type, name, false);
    auto it = std::lower_bound(m_presets.begin(), m_presets.end(), key, 
        [](const Preset &p1, const Preset &p2) { return p1.name < p2.name; } );
    // Ensure that a temporary copy is returned if the preset found is currently selected.
    return (it != m_presets.end() && it->name == key.name) ? &this->preset(it - m_presets.begin()) : 
        first_visible_if_not_found ? &this->first_visible() : nullptr;
}

// Return index of the first visible preset. Certainly at least the '- default -' preset shall be visible.
size_t PresetCollection::first_visible_idx() const
{
    size_t idx = 0;
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

// Save the preset under a new name. If the name is different from the old one,
// a new preset is stored into the list of presets.
// All presets are marked as not modified and the new preset is activated.
//void PresetCollection::save_current_preset(const std::string &new_name);

// Delete the current preset, activate the first visible preset.
//void PresetCollection::delete_current_preset();

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

void PresetCollection::update_platter_ui(wxChoice *ui)
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

    std::string name_selected = dynamic_cast<wxItemContainerImmutable*>(ui)->GetStringSelection().ToUTF8().data();
    if (boost::algorithm::iends_with(name_selected, g_suffix_modified))
        // Remove the g_suffix_modified.
        name_selected.erase(name_selected.end() - g_suffix_modified.size(), name_selected.end());

    ui->Clear();
    for (size_t i = this->m_presets.front().is_visible ? 0 : 1; i < this->m_presets.size(); ++ i) {
        const Preset &preset = this->m_presets[i];
        const wxBitmap *bmp = (i == 0 || preset.is_visible) ? m_bitmap_compatible : m_bitmap_incompatible;
        ui->Append(wxString::FromUTF8((preset.name + (preset.is_dirty ? g_suffix_modified : "")).c_str()), (void*)&preset);
        if (name_selected == preset.name)
            ui->SetSelection(ui->GetCount() - 1);
    }
}

// Update a dirty floag of the current preset, update the labels of the UI component accordingly.
// Return true if the dirty flag changed.
bool PresetCollection::update_dirty_ui(wxItemContainer *ui)
{
    // 1) Update the dirty flag of the current preset.
    bool was_dirty = this->get_selected_preset().is_dirty;
    bool is_dirty  = current_is_dirty();
    this->get_selected_preset().is_dirty = is_dirty;
    // 2) Update the labels.
    for (unsigned int ui_id = 0; ui_id < ui->GetCount(); ++ ui_id) {
        std::string   old_label    = ui->GetString(ui_id).utf8_str().data();
        std::string   preset_name  = boost::algorithm::ends_with(old_label, g_suffix_modified) ? 
                old_label.substr(0, g_suffix_modified.size()) :
                old_label;
        const Preset *preset       = this->find_preset(preset_name, false);
        assert(preset != nullptr);
        std::string new_label = preset->is_dirty ? preset->name + g_suffix_modified : preset->name;
        if (old_label != new_label)
            ui->SetString(ui_id, wxString::FromUTF8(new_label.c_str()));
    }
    return was_dirty != is_dirty;
}

bool PresetCollection::update_dirty_ui(wxChoice *ui)
{ 
    return update_dirty_ui(dynamic_cast<wxItemContainer*>(ui));
}

Preset& PresetCollection::select_preset(size_t idx)
{
    if (idx >= m_presets.size())
        idx = first_visible_idx();
    m_idx_selected = idx;
    m_edited_preset = m_presets[idx];
    return m_presets[idx];
}

bool PresetCollection::select_preset_by_name(const std::string &name, bool force)
{
    // 1) Try to find the preset by its name.
    Preset key(m_type, name, false);
    auto it = std::lower_bound(m_presets.begin(), m_presets.end(), key, 
        [](const Preset &p1, const Preset &p2) { return p1.name < p2.name; } );
    size_t idx = 0;
    if (it != m_presets.end() && it->name == key.name)
        // Preset found by its name.
        idx = it - m_presets.begin();
    else {
        // Find the first visible preset.
        for (size_t i = 0; i < m_presets.size(); ++ i)
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

bool PresetCollection::select_by_name_ui(char *name, wxItemContainer *ui)
{
    this->select_preset_by_name(name, true);
    //FIXME this is not finished yet.
    //this->update_platter_ui(wxChoice *ui)
    return true;
}

bool PresetCollection::select_by_name_ui(char *name, wxChoice *ui)
{
    return this->select_by_name_ui(name, dynamic_cast<wxItemContainer*>(ui));
}

PresetBundle::PresetBundle() :
    prints(Preset::TYPE_PRINT, print_options()), 
    filaments(Preset::TYPE_FILAMENT, filament_options()), 
    printers(Preset::TYPE_PRINTER, printer_options()),
    m_bitmapCompatible(new wxBitmap),
    m_bitmapIncompatible(new wxBitmap)
{
    ::wxInitAllImageHandlers();

    // Create the ID config keys, as they are not part of the Static print config classes.
    this->prints.preset(0).config.opt_string("print_settings_id", true);
    this->filaments.preset(0).config.opt_string("filament_settings_id", true);
    this->printers.preset(0).config.opt_string("print_settings_id", true);
    // Create the "compatible printers" keys, as they are not part of the Static print config classes.
    this->filaments.preset(0).config.optptr("compatible_printers", true);
    this->prints.preset(0).config.optptr("compatible_printers", true);

    this->prints   .load_bitmap_default("cog.png");
    this->filaments.load_bitmap_default("spool.png");
    this->printers .load_bitmap_default("printer_empty.png");

    // FIXME select some icons indicating compatibility.
    this->load_compatible_bitmaps("cog.png", "cog.png");
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
    this->prints   .load_presets(dir_path, "print");
    this->filaments.load_presets(dir_path, "filament");
    this->printers .load_presets(dir_path, "printer");
}

bool PresetBundle::load_compatible_bitmaps(const std::string &path_bitmap_compatible, const std::string &path_bitmap_incompatible)
{
    bool loaded_compatible   = m_bitmapCompatible  ->LoadFile(
        wxString::FromUTF8(Slic3r::var(path_bitmap_compatible).c_str()), wxBITMAP_TYPE_PNG);
    bool loaded_incompatible = m_bitmapIncompatible->LoadFile(
        wxString::FromUTF8(Slic3r::var(path_bitmap_incompatible).c_str()), wxBITMAP_TYPE_PNG);
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

DynamicPrintConfig PresetBundle::full_config() const
{    
    DynamicPrintConfig out;
    out.apply(FullPrintConfig());
    out.apply(this->prints.get_edited_preset().config);
    out.apply(this->printers.get_edited_preset().config);

    auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(out.option("nozzle_diameter"));
    size_t  num_extruders   = nozzle_diameter->values.size();

    if (num_extruders <= 1) {
        out.apply(this->filaments.get_edited_preset().config);
    } else {
        // Retrieve filament presets and build a single config object for them.
        // First collect the filament configurations based on the user selection of this->filament_presets.
        std::vector<const DynamicPrintConfig*> filament_configs;
        for (const std::string &filament_preset_name : this->filament_presets)
            filament_configs.emplace_back(&this->filaments.find_preset(filament_preset_name, true)->config);
		while (filament_configs.size() < num_extruders)
            filament_configs.emplace_back(&this->filaments.first_visible().config);
        // Option values to set a ConfigOptionVector from.
        std::vector<const ConfigOption*> filament_opts(num_extruders, nullptr);
        // loop through options and apply them to the resulting config.
        for (const t_config_option_key &key : this->filaments.default_preset().config.keys()) {
            // Get a destination option.
            ConfigOption *opt_dst = out.option(key, false);
            if (opt_dst->is_scalar()) {
                // Get an option, do not create if it does not exist.
                const ConfigOption *opt_src = filament_configs.front()->option(key);
                if (opt_src != nullptr)
                    opt_dst->set(opt_src);
            } else {
                // Setting a vector value from all filament_configs.
                for (size_t i = 0; i < filament_opts.size(); ++ i)
                    filament_opts[i] = filament_configs[i]->option(key);
                static_cast<ConfigOptionVectorBase*>(opt_dst)->set(filament_opts);
            }
        }
    }
    
    static const char *keys[] = { "perimeter", "infill", "solid_infill", "support_material", "support_material_interface" };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++ i) {
        std::string key = std::string(keys[i]) + "_extruder";
        auto *opt = dynamic_cast<ConfigOptionInt*>(out.option(key, false));
        assert(opt != nullptr);
        opt->value = std::min<int>(opt->value, std::min<int>(0, int(num_extruders) - 1));
    }

    return out;
}

// Load an external config file containing the print, filament and printer presets.
// Instead of a config file, a G-code may be loaded containing the full set of parameters.
// In the future the configuration will likely be read from an AMF file as well.
// If the file is loaded successfully, its print / filament / printer profiles will be activated.
void PresetBundle::load_config_file(const std::string &path)
{
    // 1) Initialize a config from full defaults.
    DynamicPrintConfig config;
    config.apply(FullPrintConfig());

    // 2) Try to load the config file.
    // Throw exceptions with reasonable messages if something goes wrong.
    Slic3r::load_config_file(config, path);

    // 3) Create a name from the file name.
    // Keep the suffix (.ini, .gcode, .amf, .3mf etc) to differentiate it from the normal profiles.
    std::string name = boost::filesystem::path(path).filename().string();

    // 3) If the loading succeeded, split and load the config into print / filament / printer settings.
    // First load the print and printer presets.
    for (size_t i_group = 0; i_group < 2; ++ i_group) {
        PresetCollection &presets = (i_group == 0) ? this->prints : this->printers;
        presets.load_preset(path, name, config).is_external = true;
    }

    // Now load the filaments. If there are multiple filament presets, split them and load them.
    auto   *nozzle_diameter   = dynamic_cast<const ConfigOptionFloats*>(config.option("nozzle_diameter"));
    auto   *filament_diameter = dynamic_cast<const ConfigOptionFloats*>(config.option("filament_diameter"));
    size_t  num_extruders     = std::min(nozzle_diameter->values.size(), filament_diameter->values.size());
    if (num_extruders <= 1) {
        this->filaments.load_preset(path, name, config).is_external = true;
        this->filament_presets.clear();
        this->filament_presets.emplace_back(name);
    } else {
        // Split the filament presets, load each of them separately.
        std::vector<DynamicPrintConfig> configs(num_extruders, this->filaments.default_preset().config);
        // loop through options and scatter them into configs.
        for (const t_config_option_key &key : this->filaments.default_preset().config.keys()) {
            const ConfigOption *other_opt = config.option(key, false);
            if (other_opt == nullptr)
                continue;
            if (other_opt->is_scalar()) {
                for (size_t i = 0; i < configs.size(); ++ i)
                    configs[i].option(key, false)->set(other_opt);
            } else {
                for (size_t i = 0; i < configs.size(); ++ i)
                    static_cast<ConfigOptionVectorBase*>(configs[i].option(key, false))->set_at(other_opt, 0, i);
            }
        }
        // Load the configs into this->filaments and make them active.
        filament_presets.clear();
        for (size_t i = 0; i < configs.size(); ++ i) {
            char suffix[64];
            if (i == 0)
                suffix[0] = 0;
            else
                sprintf(suffix, " (%d)", i);
            // Load all filament presets, but only select the first one in the preset dialog.
            this->filaments.load_preset(path, name + suffix, configs[i], i == 0).is_external = true;
            filament_presets.emplace_back(name + suffix);
        }
    }
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

// Load a config bundle file, into presets and store the loaded presets into separate files
// of the local configuration directory.
// Load settings into the provided settings instance.
void PresetBundle::load_configbundle(const std::string &path, const DynamicPrintConfig &settings)
{
    // 1) Read the complete config file into the boost::property_tree.
    namespace pt = boost::property_tree;
    pt::ptree tree;
    boost::nowide::ifstream ifs(path);
    pt::read_ini(ifs, tree);

    // 2) Parse the property_tree, extract the active preset names and the profiles, save them into local config files.
    std::vector<std::string> loaded_prints;
    std::vector<std::string> loaded_filaments;
    std::vector<std::string> loaded_printers;
    std::string              active_print;
    std::vector<std::string> active_filaments;
    std::string              active_printer;
    for (const auto &section : tree) {
        PresetCollection         *presets = nullptr;
        std::vector<std::string> *loaded  = nullptr;
        std::string               preset_name;
        if (boost::starts_with(section.first, "print:")) {
            presets = &prints;
            loaded  = &loaded_prints;
            preset_name = section.first.substr(6);
        } else if (boost::starts_with(section.first, "filament:")) {
            presets = &filaments;
            loaded  = &loaded_filaments;
            preset_name = section.first.substr(9);
        } else if (boost::starts_with(section.first, "printer:")) {
            presets = &printers;
            loaded  = &loaded_printers;
            preset_name = section.first.substr(8);
        } else if (section.first == "presets") {
            // Load the names of the active presets.
            for (auto &kvp : section.second) {
                if (kvp.first == "print") {
                    active_print = kvp.second.data();
                } else if (boost::starts_with(kvp.first, "filament")) {
                    int idx = 0;
                    if (kvp.first == "filament" || sscanf(kvp.first.c_str(), "filament_%d", &idx) == 1) {
                        if (int(active_filaments.size()) <= idx)
                            active_filaments.resize(idx + 1, std::string());
                        active_filaments[idx] = kvp.second.data();
                    }
                } else if (kvp.first == "printer") {
                    active_printer = kvp.second.data();
                }
            }
        } else if (section.first == "settings") {
            // Load the settings.
            for (auto &kvp : section.second) {
                if (kvp.first == "autocenter") {
                }
            }
        } else
            // Ignore an unknown section.
            continue;
        if (presets != nullptr) {
            // Load the print, filament or printer preset.
            DynamicPrintConfig config(presets->default_preset().config);
            for (auto &kvp : section.second)
                config.set_deserialize(kvp.first, kvp.second.data());
            // Load the preset into the list of presets, save it to disk.
            presets->load_preset(Slic3r::config_path(presets->name(), preset_name), preset_name, config, false).save();
        }
    }

    // 3) Activate the presets.
    if (! active_print.empty()) 
        prints.select_preset_by_name(active_print, true);
    if (! active_printer.empty())
        printers.select_preset_by_name(active_printer, true);
    // Activate the first filament preset.
    if (! active_filaments.empty() && ! active_filaments.front().empty())
        filaments.select_preset_by_name(active_filaments.front(), true);
    // Verify and select the filament presets.
    auto   *nozzle_diameter = static_cast<const ConfigOptionFloats*>(printers.get_selected_preset().config.option("nozzle_diameter"));
    size_t  num_extruders   = nozzle_diameter->values.size();
    if (this->filament_presets.size() < num_extruders)
        this->filament_presets.resize(num_extruders, filaments.get_selected_preset().name);
    for (size_t i = 0; i < num_extruders; ++ i)
        this->filament_presets[i] = (i < active_filaments.size()) ? 
            filaments.find_preset(active_filaments[i], true)->name :
            filaments.first_visible().name;
}

void PresetBundle::export_configbundle(const std::string &path, const DynamicPrintConfig &settings)
{
    boost::nowide::ofstream c;
    c.open(path, std::ios::out | std::ios::trunc);

    // Put a comment at the first line including the time stamp and Slic3r version.
    {
        std::time_t now;
        time(&now);
        char buf[sizeof "0000-00-00 00:00:00"];
        strftime(buf, sizeof(buf), "%F %T", gmtime(&now));
        c << "# generated by Slic3r " << SLIC3R_VERSION << " on " << buf << std::endl;
    }

    // Export the print, filament and printer profiles.
    for (size_t i_group = 0; i_group < 3; ++ i_group) {
        const PresetCollection &presets = (i_group == 0) ? this->prints : (i_group == 1) ? this->filaments : this->printers;
        for (const Preset &preset : presets()) {
            if (preset.is_default || preset.is_external)
                // Only export the common presets, not external files or the default preset.
                continue;
            c << "[" << presets.name() << ":" << preset.name << "]" << std::endl;
            for (const std::string &opt_key : preset.config.keys())
                c << opt_key << " = " << preset.config.serialize(opt_key) << std::endl;
        }
    }

    // Export the names of the active presets.
    c << "[presets]" << std::endl;
    c << "print = " << this->prints.get_selected_preset().name << std::endl;
    c << "printer = " << this->printers.get_selected_preset().name << std::endl;
    for (size_t i = 0; i < this->filament_presets.size(); ++ i) {
        char suffix[64];
        if (i > 0)
            sprintf(suffix, "_%d", i);
        else
            suffix[0] = 0;
        c << "filament" << suffix << " = " << this->filament_presets[i] << std::endl;
    }

    // Export the following setting values from the provided setting repository.
    static const char *settings_keys[] = { "autocenter" };
    c << "[presets]" << std::endl;
    c << "print = " << this->prints.get_selected_preset().name << std::endl;
    for (size_t i = 0; i < sizeof(settings_keys) / sizeof(settings_keys[0]); ++ i)
        c << settings_keys[i] << " = " << settings.serialize(settings_keys[i]) << std::endl;

    c.close();
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
        std::string   preset_name        = ui->GetString(ui_id).utf8_str().data();
        size_t        filament_preset_id = size_t(ui->GetClientData(ui_id));
        const Preset *filament_preset    = filaments.find_preset(preset_name, false);
        assert(filament_preset != nullptr);
        // Assign an extruder color to the selected item if the extruder color is defined.
        std::string   filament_rgb       = filament_preset->config.opt_string("filament_colour", 0);
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

void PresetBundle::set_default_suppressed(bool default_suppressed)
{
    prints.set_default_suppressed(default_suppressed);
    filaments.set_default_suppressed(default_suppressed);
    printers.set_default_suppressed(default_suppressed);
}

} // namespace Slic3r
