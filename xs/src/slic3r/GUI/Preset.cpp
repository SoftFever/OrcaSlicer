#include "Preset.hpp"

#include <fstream>
#include <boost/filesystem.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <boost/nowide/cenv.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/locale.hpp>

#include <wx/image.h>
#include <wx/choice.h>
#include <wx/bmpcbox.h>
#include <wx/wupdlock.h>

#include "../../libslic3r/Utils.hpp"

#if 0
#define DEBUG
#define _DEBUG
#undef NDEBUG
#endif

#include <assert.h>

namespace Slic3r {

// Suffix to be added to a modified preset name in the combo box.
static std::string g_suffix_modified = " (modified)";
const std::string& Preset::suffix_modified()
{
    return g_suffix_modified;
}
// Remove an optional "(modified)" suffix from a name.
// This converts a UI name to a unique preset identifier.
std::string Preset::remove_suffix_modified(const std::string &name)
{
    return boost::algorithm::ends_with(name, g_suffix_modified) ?
        name.substr(0, name.size() - g_suffix_modified.size()) :
        name;
}

// Load keys from a config file or a G-code.
// Throw exceptions with reasonable messages if something goes wrong.
void Preset::load_config_file(DynamicPrintConfig &config, const std::string &path)
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
	boost::filesystem::path dir = boost::filesystem::canonical(boost::filesystem::path(dir_path) / subdir).make_preferred();
	m_dir_path = dir.string();
    m_presets.erase(m_presets.begin()+1, m_presets.end());
    t_config_option_keys keys = this->default_preset().config.keys();
	for (auto &file : boost::filesystem::directory_iterator(dir))
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
    std::sort(m_presets.begin() + 1, m_presets.end());
    m_presets.front().is_visible = ! m_default_suppressed || m_presets.size() > 1;
    this->select_preset(first_visible_idx());
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

void PresetCollection::save_current_preset(const std::string &new_name)
{
    Preset key(m_type, new_name, false);
    auto it = std::lower_bound(m_presets.begin(), m_presets.end(), key);
    if (it != m_presets.end() && it->name == key.name) {
        // Preset with the same name found.
        Preset &preset = *it;
        if (preset.is_default)
            // Cannot overwrite the default preset.
            return;
        // Overwriting an existing preset.
        preset.config = std::move(m_edited_preset.config);
        m_idx_selected = it - m_presets.begin();
    } else {
        // Creating a new preset.
		m_idx_selected = m_presets.insert(it, m_edited_preset) - m_presets.begin();
        Preset &preset = m_presets[m_idx_selected];
        std::string file_name = new_name;
        if (! boost::iends_with(file_name, ".ini"))
            file_name += ".ini";
        preset.name = new_name;
		preset.file = (boost::filesystem::path(m_dir_path) / file_name).make_preferred().string();
    }
    m_edited_preset = m_presets[m_idx_selected];
    m_presets[m_idx_selected].save();
}

void PresetCollection::delete_current_preset()
{
    const Preset &selected = this->get_selected_preset();
    if (selected.is_default || selected.is_external)
        return;
    // Erase the preset file.
    boost::nowide::remove(selected.file.c_str());
    // Remove the preset from the list.
    m_presets.erase(m_presets.begin() + m_idx_selected);
    // Find the next visible preset.
    m_presets.front().is_visible = ! m_default_suppressed || m_presets.size() > 1;    
    for (; m_idx_selected < m_presets.size() && ! m_presets[m_idx_selected].is_visible; ++ m_idx_selected) ;
    if (m_idx_selected == m_presets.size())
        m_idx_selected = this->first_visible_idx();
    m_edited_preset = m_presets[m_idx_selected];
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
    auto it = std::lower_bound(m_presets.begin(), m_presets.end(), key);
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
void PresetCollection::update_platter_ui(wxBitmapComboBox *ui)
{
    if (ui == nullptr)
        return;
    // Otherwise fill in the list from scratch.
    ui->Freeze();
    ui->Clear();
    for (size_t i = this->m_presets.front().is_visible ? 0 : 1; i < this->m_presets.size(); ++ i) {
        const Preset &preset = this->m_presets[i];
        const wxBitmap *bmp = (i == 0 || preset.is_visible) ? m_bitmap_compatible : m_bitmap_incompatible;
        ui->Append(wxString::FromUTF8((preset.name + (preset.is_dirty ? g_suffix_modified : "")).c_str()), (bmp == 0) ? wxNullBitmap : *bmp, (void*)i);
		if (i == m_idx_selected)
            ui->SetSelection(ui->GetCount() - 1);
    }
    ui->Thaw();
}

void PresetCollection::update_tab_ui(wxChoice *ui)
{
    if (ui == nullptr)
        return;
    ui->Freeze();
    ui->Clear();
    for (size_t i = this->m_presets.front().is_visible ? 0 : 1; i < this->m_presets.size(); ++ i) {
        const Preset &preset = this->m_presets[i];
        const wxBitmap *bmp = (i == 0 || preset.is_visible) ? m_bitmap_compatible : m_bitmap_incompatible;
        ui->Append(wxString::FromUTF8((preset.name + (preset.is_dirty ? g_suffix_modified : "")).c_str()), (void*)&preset);
		if (i == m_idx_selected)
            ui->SetSelection(ui->GetCount() - 1);
    }
    ui->Thaw();
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
        std::string   preset_name  = Preset::remove_suffix_modified(old_label);
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
    wxWindowUpdateLocker noUpdates(ui);
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

bool PresetCollection::select_preset_by_name(const std::string &name_w_suffix, bool force)
{   
    std::string name = Preset::remove_suffix_modified(name_w_suffix);
    // 1) Try to find the preset by its name.
    Preset key(m_type, name, false);
    auto it = std::lower_bound(m_presets.begin(), m_presets.end(), key);
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

std::string PresetCollection::name() const
{
    switch (this->type()) {
    case Preset::TYPE_PRINT:    return "print";
    case Preset::TYPE_FILAMENT: return "filament";
    case Preset::TYPE_PRINTER:  return "printer";
    default:                    return "invalid";
    }
}

} // namespace Slic3r
