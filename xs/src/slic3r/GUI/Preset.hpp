#ifndef slic3r_Preset_hpp_
#define slic3r_Preset_hpp_

#include <deque>

#include "../../libslic3r/libslic3r.h"
#include "../../libslic3r/PrintConfig.hpp"

class wxBitmap;
class wxChoice;
class wxBitmapComboBox;
class wxItemContainer;

namespace Slic3r {

class Preset
{
public:
    enum Type
    {
        TYPE_INVALID,
        TYPE_PRINT,
        TYPE_FILAMENT,
        TYPE_PRINTER,
    };

    Preset(Type type, const std::string &name, bool is_default = false) : type(type), is_default(is_default), name(name) {}

    Type                type        = TYPE_INVALID;

    // The preset represents a "default" set of properties,
    // pulled from the default values of the PrintConfig (see PrintConfigDef for their definitions).
    bool                is_default;
    // External preset points to a configuration, which has been loaded but not imported
    // into the Slic3r default configuration location.
    bool                is_external = false;
    // Preset is visible, if it is compatible with the active Printer.
    // Also the "default" preset is only visible, if it is the only preset in the list.
    bool                is_visible  = true;
    // Has this preset been modified?
    bool                is_dirty    = false;

    // Name of the preset, usually derived form the file name.
    std::string         name;
    // File name of the preset. This could be a Print / Filament / Printer preset, 
    // or a Configuration file bundling the Print + Filament + Printer presets (in that case is_external will be true),
    // or it could be a G-code (again, is_external will be true).
    std::string         file;

    // Has this profile been loaded?
    bool                loaded      = false;

    // Configuration data, loaded from a file, or set from the defaults.
    DynamicPrintConfig  config;

    // Load this profile for the following keys only.
    // Throws std::runtime_error in case the file cannot be read.
    DynamicPrintConfig& load(const std::vector<std::string> &keys);

    void                save();

    // Return a label of this preset, consisting of a name and a "(modified)" suffix, if this preset is dirty.
    std::string         label() const;

    // Set the is_dirty flag if the provided config is different from the active one.
    void                set_dirty(const DynamicPrintConfig &config) { this->is_dirty = ! this->config.diff(config).empty(); }
    void                set_dirty(bool dirty = true) { this->is_dirty = dirty; }
    void                reset_dirty() { this->is_dirty = false; }

    // Mark this preset as visible if it is compatible with active_printer.
    bool                enable_compatible(const std::string &active_printer);

    // Sort lexicographically by a preset name. The preset name shall be unique across a single PresetCollection.
    bool                operator<(const Preset &other) const { return this->name < other.name; }
};

// Collections of presets of the same type (one of the Print, Filament or Printer type).
class PresetCollection
{
public:
    // Initialize the PresetCollection with the "- default -" preset.
    PresetCollection(Preset::Type type, const std::vector<std::string> &keys);
    ~PresetCollection();

    Preset::Type    type() const { return m_type; }
    std::string     name() const;
    const std::deque<Preset>& operator()() const { return m_presets; }

    // Load ini files of the particular type from the provided directory path.
    void            load_presets(const std::string &dir_path, const std::string &subdir);

    // Load a preset from an already parsed config file, insert it into the sorted sequence of presets
    // and select it, losing previous modifications.
    Preset&         load_preset(const std::string &path, const std::string &name, const DynamicPrintConfig &config, bool select = true);

    // Load default bitmap to be placed at the wxBitmapComboBox of a MainFrame.
    bool            load_bitmap_default(const std::string &file_name);

    // Compatible & incompatible marks, to be placed at the wxBitmapComboBox items.
    void            set_bitmap_compatible  (const wxBitmap *bmp) { m_bitmap_compatible   = bmp; }
    void            set_bitmap_incompatible(const wxBitmap *bmp) { m_bitmap_incompatible = bmp; }

    // Enable / disable the "- default -" preset.
    void            set_default_suppressed(bool default_suppressed);
    bool            is_default_suppressed() const { return m_default_suppressed || m_presets.size() <= 1; }

    // Select a preset. If an invalid index is provided, the first visible preset is selected.
    Preset&         select_preset(size_t idx);
    // Return the selected preset, without the user modifications applied.
    Preset&         get_selected_preset()       { return m_presets[m_idx_selected]; }
    const Preset&   get_selected_preset() const { return m_presets[m_idx_selected]; }
    // Return the selected preset including the user modifications.
    Preset&         get_edited_preset()         { return m_edited_preset; }
    const Preset&   get_edited_preset() const   { return m_edited_preset; }
    // Return a preset possibly with modifications.
    const Preset&   default_preset() const      { return m_presets.front(); }
    // Return a preset by an index. If the preset is active, a temporary copy is returned.
    Preset&         preset(size_t idx)          { return (int(idx) == m_idx_selected) ? m_edited_preset : m_presets[idx]; }
    const Preset&   preset(size_t idx) const    { return const_cast<PresetCollection*>(this)->preset(idx); }
    
    // Return a preset by its name. If the preset is active, a temporary copy is returned.
    // If a preset is not found by its name, null is returned.
    Preset*         find_preset(const std::string &name, bool first_visible_if_not_found = false);
    const Preset*   find_preset(const std::string &name, bool first_visible_if_not_found = false) const 
        { return const_cast<PresetCollection*>(this)->find_preset(name, first_visible_if_not_found); }

    size_t          first_visible_idx() const;
    // Return index of the first visible preset. Certainly at least the '- default -' preset shall be visible.
    // Return the first visible preset. Certainly at least the '- default -' preset shall be visible.
    Preset&         first_visible()         { return this->preset(this->first_visible_idx()); }
    const Preset&   first_visible() const   { return this->preset(this->first_visible_idx()); }

    // Return number of presets including the "- default -" preset.
    size_t          size() const                { return this->m_presets.size(); }

    // For Print / Filament presets, disable those, which are not compatible with the printer.
    void            enable_disable_compatible_to_printer(const std::string &active_printer);

    size_t          num_visible() const { return std::count_if(m_presets.begin(), m_presets.end(), [](const Preset &preset){return preset.is_visible;}); }

    // Compare the content of get_selected_preset() with get_edited_preset() configs, return true if they differ.
    bool                        current_is_dirty() { return ! this->current_dirty_options().empty(); }
    // Compare the content of get_selected_preset() with get_edited_preset() configs, return the list of keys where they differ.
    std::vector<std::string>    current_dirty_options() { return this->get_selected_preset().config.diff(this->get_edited_preset().config); }

    // Save the preset under a new name. If the name is different from the old one,
    // a new preset is stored into the list of presets.
    // All presets are marked as not modified and the new preset is activated.
    void            save_current_preset(const std::string &new_name);

    // Delete the current preset, activate the first visible preset.
    void            delete_current_preset();

    // Update the choice UI from the list of presets.
    void            update_editor_ui(wxBitmapComboBox *ui);
    void            update_platter_ui(wxChoice *ui);
    void            update_platter_ui(wxBitmapComboBox *ui);

    // Update a dirty floag of the current preset, update the labels of the UI component accordingly.
    // Return true if the dirty flag changed.
    bool            update_dirty_ui(wxItemContainer *ui);
    bool            update_dirty_ui(wxChoice *ui);
    
    // Select a profile by its name. Return true if the selection changed.
    // Without force, the selection is only updated if the index changes.
    // With force, the changes are reverted if the new index is the same as the old index.
    bool            select_preset_by_name(const std::string &name, bool force);
    // Select a profile by its name, update selection at the UI component.
    // Return true if the selection changed.
    bool            select_by_name_ui(char *name, wxItemContainer *ui);
    bool            select_by_name_ui(char *name, wxChoice *ui);

private:
    PresetCollection();
    PresetCollection(const PresetCollection &other);
    PresetCollection& operator=(const PresetCollection &other);

    // Type of this PresetCollection: TYPE_PRINT, TYPE_FILAMENT or TYPE_PRINTER.
    Preset::Type            m_type;
    // List of presets, starting with the "- default -" preset.
    // Use deque to force the container to allocate an object per each entry, 
    // so that the addresses of the presets don't change during resizing of the container.
    std::deque<Preset>      m_presets;
    // Initially this preset contains a copy of the selected preset. Later on, this copy may be modified by the user.
    Preset                  m_edited_preset;
    // Selected preset.
    int                     m_idx_selected;
    // Is the "- default -" preset suppressed?
    bool                    m_default_suppressed = true;
    // Compatible & incompatible marks, to be placed at the wxBitmapComboBox items of a Platter.
    // These bitmaps are not owned by PresetCollection, but by a PresetBundle.
    const wxBitmap         *m_bitmap_compatible = nullptr;
    const wxBitmap         *m_bitmap_incompatible = nullptr;
    // Marks placed at the wxBitmapComboBox of a MainFrame.
    // These bitmaps are owned by PresetCollection.
    wxBitmap               *m_bitmap_main_frame;
};

// Bundle of Print + Filament + Printer presets.
class PresetBundle
{
public:
    PresetBundle();
    ~PresetBundle();

    // Load ini files of all types (print, filament, printer) from the provided directory path.
    void            load_presets(const std::string &dir_path);

    PresetCollection            prints;
    PresetCollection            filaments;
    PresetCollection            printers;
    // Filament preset names for a multi-extruder or multi-material print.
    // extruders.size() should be the same as printers.get_edited_preset().config.nozzle_diameter.size()
    std::vector<std::string>    filament_presets;

    DynamicPrintConfig          full_config() const;

    // Load an external config file containing the print, filament and printer presets.
    // Instead of a config file, a G-code may be loaded containing the full set of parameters.
    // In the future the configuration will likely be read from an AMF file as well.
    // If the file is loaded successfully, its print / filament / printer profiles will be activated.
    void                        load_config_file(const std::string &path);

    // Load a config bundle file, into presets and store the loaded presets into separate files
    // of the local configuration directory.
    // Load settings into the provided settings instance.
    // Activate the presets stored in the 
    void                        load_configbundle(const std::string &path, const DynamicPrintConfig &settings);

    // Export a config bundle file containing all the presets and the names of the active presets.
    void                        export_configbundle(const std::string &path, const DynamicPrintConfig &settings);

    // Update the colors preview at the platter extruder combo box.
    void update_platter_filament_ui_colors(wxBitmapComboBox *ui, unsigned int idx_extruder, unsigned int idx_filament);

    static const std::vector<std::string>&  print_options();
    static const std::vector<std::string>&  filament_options();
    static const std::vector<std::string>&  printer_options();

    // Enable / disable the "- default -" preset.
    void            set_default_suppressed(bool default_suppressed);

private:
    bool            load_compatible_bitmaps(const std::string &path_bitmap_compatible, const std::string &path_bitmap_incompatible);

    // Indicator, that the preset is compatible with the selected printer.
    wxBitmap                            *m_bitmapCompatible;
    // Indicator, that the preset is NOT compatible with the selected printer.
    wxBitmap                            *m_bitmapIncompatible;
    // Caching color bitmaps for the 
    std::map<std::string, wxBitmap*>     m_mapColorToBitmap;
};

} // namespace Slic3r

#endif /* slic3r_Preset_hpp_ */
