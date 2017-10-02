#ifndef slic3r_Preset_hpp_
#define slic3r_Preset_hpp_

#include "../../libslic3r/libslic3r.h"
#include "../../libslic3r/PrintConfig.hpp"

class wxBitmap;
class wxBitmapComboBox;

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

    // Set the is_dirty flag if the provided config is different from the active one.
    void                set_dirty(const DynamicPrintConfig &config) { this->is_dirty = ! this->config.diff(config).empty(); }
    void                reset_dirty() { this->is_dirty = false; }
    bool                enable_compatible(const std::string &active_printer);
};

// Collections of presets of the same type (one of the Print, Filament or Printer type).
class PresetCollection
{
public:
    // Initialize the PresetCollection with the "- default -" preset.
    PresetCollection(Preset::Type type, const std::vector<std::string> &keys);

    // Load ini files of the particular type from the provided directory path.
    void            load_presets(const std::string &dir_path, const std::string &subdir);

    // Compatible & incompatible marks, to be placed at the wxBitmapComboBox items.
    void            set_bitmap_compatible  (const wxBitmap *bmp) { m_bitmap_compatible   = bmp; }
    void            set_bitmap_incompatible(const wxBitmap *bmp) { m_bitmap_incompatible = bmp; }

    // Enable / disable the "- default -" preset.
    void            set_default_suppressed(bool default_suppressed);
    bool            is_default_suppressed() const { return m_default_suppressed; }

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
    Preset&         preset(size_t idx)          { return (int(idx) == m_idx_selected) ? m_edited_preset : m_presets[idx]; }
    const Preset&   preset(size_t idx) const    { return const_cast<PresetCollection*>(this)->preset(idx); }
    size_t          size() const                { return this->m_presets.size(); }

    // For Print / Filament presets, disable those, which are not compatible with the printer.
    void            enable_disable_compatible_to_printer(const std::string &active_printer);

    size_t          num_visible() const { return std::count_if(m_presets.begin(), m_presets.end(), [](const Preset &preset){return preset.is_visible;}); }
    void            delete_preset(const size_t idx);

    // Update the choice UI from the list of presets.
    void            update_editor_ui(wxBitmapComboBox *ui);
    void            update_platter_ui(wxBitmapComboBox *ui);

private:
    // Type of this PresetCollection: TYPE_PRINT, TYPE_FILAMENT or TYPE_PRINTER.
    Preset::Type            m_type;
    // List of presets, starting with the "- default -" preset.
    std::vector<Preset>     m_presets;
    Preset                  m_edited_preset;
    // Selected preset.
    int                     m_idx_selected;
    // Is the "- default -" preset suppressed?
    bool                    m_default_suppressed = true;
    // Compatible & incompatible marks, to be placed at the wxBitmapComboBox items.
    const wxBitmap         *m_bitmap_compatible = nullptr;
    const wxBitmap         *m_bitmap_incompatible = nullptr;
};

// Bundle of Print + Filament + Printer presets.
class PresetBundle
{
public:
    PresetBundle();
    ~PresetBundle();

    bool            load_bitmaps(const std::string &path_bitmap_compatible, const std::string &path_bitmap_incompatible);

    // Load ini files of all types (print, filament, printer) from the provided directory path.
    void            load_presets(const std::string &dir_path);

    PresetCollection        prints;
    PresetCollection        filaments;
    PresetCollection        printers;

    // Update the colors preview at the platter extruder combo box.
    void update_platter_filament_ui_colors(wxBitmapComboBox *ui, unsigned int idx_extruder, unsigned int idx_filament);

    static const std::vector<std::string>&  print_options();
    static const std::vector<std::string>&  filament_options();
    static const std::vector<std::string>&  printer_options();

private:
    // Indicator, that the preset is compatible with the selected printer.
    wxBitmap                            *m_bitmapCompatible;
    // Indicator, that the preset is NOT compatible with the selected printer.
    wxBitmap                            *m_bitmapIncompatible;
    // Caching color bitmaps for the 
    std::map<std::string, wxBitmap*>     m_mapColorToBitmap;
};

} // namespace Slic3r

#endif /* slic3r_Preset_hpp_ */
