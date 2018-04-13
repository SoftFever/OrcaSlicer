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

enum ConfigFileType
{
    CONFIG_FILE_TYPE_UNKNOWN,
    CONFIG_FILE_TYPE_APP_CONFIG,
    CONFIG_FILE_TYPE_CONFIG,
    CONFIG_FILE_TYPE_CONFIG_BUNDLE,
};

extern ConfigFileType guess_config_file_type(const boost::property_tree::ptree &tree);

class VendorProfile
{
public:
    std::string                     name;
    std::string                     id;
    std::string                     config_version;
    std::string                     config_update_url;

    struct PrinterVariant {
        PrinterVariant() {}
        PrinterVariant(const std::string &name) : name(name) {}
        std::string                 name;
        bool                        enabled = true;
    };

    struct PrinterModel {
        PrinterModel() {}
        PrinterModel(const std::string &name) : name(name) {}
        std::string                 name;
        bool                        enabled = true;
        std::vector<PrinterVariant> variants;
        PrinterVariant*       variant(const std::string &name) {
            for (auto &v : this->variants)
                if (v.name == name)
                    return &v;
            return nullptr;
        }
        const PrinterVariant* variant(const std::string &name) const { return const_cast<PrinterModel*>(this)->variant(name); }

        bool        operator< (const PrinterModel &rhs) const { return this->name <  rhs.name; }
        bool        operator==(const PrinterModel &rhs) const { return this->name == rhs.name; }
    };
    std::set<PrinterModel>          models;

    size_t      num_variants() const { size_t n = 0; for (auto &model : models) n += model.variants.size(); return n; }

    bool        operator< (const VendorProfile &rhs) const { return this->id <  rhs.id; }
    bool        operator==(const VendorProfile &rhs) const { return this->id == rhs.id; }
};

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
    // System preset is read-only.
    bool                is_system   = false;
    // Preset is visible, if it is compatible with the active Printer.
    // Also the "default" preset is only visible, if it is the only preset in the list.
    bool                is_visible  = true;
    // Has this preset been modified?
    bool                is_dirty    = false;
    // Is this preset compatible with the currently active printer?
    bool                is_compatible = true;

    // Name of the preset, usually derived form the file name.
    std::string         name;
    // File name of the preset. This could be a Print / Filament / Printer preset, 
    // or a Configuration file bundling the Print + Filament + Printer presets (in that case is_external and possibly is_system will be true),
    // or it could be a G-code (again, is_external will be true).
    std::string         file;
    // A user profile may inherit its settings either from a system profile, or from a user profile.
    // A system profile shall never derive from any other profile, as the system profile hierarchy is being flattened during loading.
    std::string         inherits;
    // If this is a system profile, then there should be a vendor data available to display at the UI.
    const VendorProfile *vendor      = nullptr;

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

    bool                is_compatible_with_printer(const Preset &active_printer, const DynamicPrintConfig *extra_config) const;
    bool                is_compatible_with_printer(const Preset &active_printer) const;

    // Mark this preset as compatible if it is compatible with active_printer.
    bool                update_compatible_with_printer(const Preset &active_printer, const DynamicPrintConfig *extra_config);

    // Resize the extruder specific fields, initialize them with the content of the 1st extruder.
    void                set_num_extruders(unsigned int n) { set_num_extruders(this->config, n); }

    // Sort lexicographically by a preset name. The preset name shall be unique across a single PresetCollection.
    bool                operator<(const Preset &other) const { return this->name < other.name; }

    static const std::vector<std::string>&  print_options();
    static const std::vector<std::string>&  filament_options();
    // Printer options contain the nozzle options.
    static const std::vector<std::string>&  printer_options();
    // Nozzle options of the printer options.
    static const std::vector<std::string>&  nozzle_options();

protected:
    friend class        PresetCollection;
    friend class        PresetBundle;
    static void         normalize(DynamicPrintConfig &config);
    // Resize the extruder specific vectors ()
    static void         set_num_extruders(DynamicPrintConfig &config, unsigned int n);
    static const std::string& suffix_modified();
    static std::string  remove_suffix_modified(const std::string &name);
};

// Collections of presets of the same type (one of the Print, Filament or Printer type).
class PresetCollection
{
public:
    // Initialize the PresetCollection with the "- default -" preset.
    PresetCollection(Preset::Type type, const std::vector<std::string> &keys);
    ~PresetCollection();

    void            reset(bool delete_files);

    Preset::Type    type() const { return m_type; }
    std::string     name() const;
    const std::deque<Preset>& operator()() const { return m_presets; }

    // Load ini files of the particular type from the provided directory path.
    void            load_presets(const std::string &dir_path, const std::string &subdir);

    // Load a preset from an already parsed config file, insert it into the sorted sequence of presets
    // and select it, losing previous modifications.
    Preset&         load_preset(const std::string &path, const std::string &name, const DynamicPrintConfig &config, bool select = true);
    Preset&         load_preset(const std::string &path, const std::string &name, DynamicPrintConfig &&config, bool select = true);

    // Save the preset under a new name. If the name is different from the old one,
    // a new preset is stored into the list of presets.
    // All presets are marked as not modified and the new preset is activated.
    void            save_current_preset(const std::string &new_name);

    // Delete the current preset, activate the first visible preset.
    void            delete_current_preset();

    // Load default bitmap to be placed at the wxBitmapComboBox of a MainFrame.
    bool            load_bitmap_default(const std::string &file_name);

    // Compatible & incompatible marks, to be placed at the wxBitmapComboBox items.
    void            set_bitmap_compatible  (const wxBitmap *bmp) { m_bitmap_compatible   = bmp; }
    void            set_bitmap_incompatible(const wxBitmap *bmp) { m_bitmap_incompatible = bmp; }
    void            set_bitmap_lock        (const wxBitmap *bmp) { m_bitmap_lock         = bmp; }
    void            set_bitmap_lock_open   (const wxBitmap *bmp) { m_bitmap_lock_open    = bmp; }

    // Enable / disable the "- default -" preset.
    void            set_default_suppressed(bool default_suppressed);
    bool            is_default_suppressed() const { return m_default_suppressed; }

    // Select a preset. If an invalid index is provided, the first visible preset is selected.
    Preset&         select_preset(size_t idx);
    // Return the selected preset, without the user modifications applied.
    Preset&         get_selected_preset()       { return m_presets[m_idx_selected]; }
    const Preset&   get_selected_preset() const { return m_presets[m_idx_selected]; }
    int             get_selected_idx()    const { return m_idx_selected; }
    // For the current edited preset, return the parent preset if there is one.
    // If there is no parent preset, nullptr is returned.
    // The parent preset may be a system preset or a user preset, which will be
    // reflected by the UI.
    const Preset*   get_selected_preset_parent() const;
	// get parent preset for some child preset
	const Preset*	get_preset_parent(const Preset& child) const;
	// Return the selected preset including the user modifications.
    Preset&         get_edited_preset()         { return m_edited_preset; }
    const Preset&   get_edited_preset() const   { return m_edited_preset; }

	// used to update preset_choice from Tab
	const std::deque<Preset>&	get_presets()	{ return m_presets; }
	int						get_idx_selected()	{ return m_idx_selected; }
	const std::string&		get_suffix_modified();

    // Return a preset possibly with modifications.
    const Preset&   default_preset() const      { return m_presets.front(); }
    // Return a preset by an index. If the preset is active, a temporary copy is returned.
    Preset&         preset(size_t idx)          { return (int(idx) == m_idx_selected) ? m_edited_preset : m_presets[idx]; }
    const Preset&   preset(size_t idx) const    { return const_cast<PresetCollection*>(this)->preset(idx); }
    void            discard_current_changes()   { m_presets[m_idx_selected].reset_dirty(); m_edited_preset = m_presets[m_idx_selected]; }
    
    // Return a preset by its name. If the preset is active, a temporary copy is returned.
    // If a preset is not found by its name, null is returned.
    Preset*         find_preset(const std::string &name, bool first_visible_if_not_found = false);
    const Preset*   find_preset(const std::string &name, bool first_visible_if_not_found = false) const 
        { return const_cast<PresetCollection*>(this)->find_preset(name, first_visible_if_not_found); }

    size_t          first_visible_idx() const;
    size_t          first_compatible_idx() const;
    // Return index of the first visible preset. Certainly at least the '- default -' preset shall be visible.
    // Return the first visible preset. Certainly at least the '- default -' preset shall be visible.
    Preset&         first_visible()             { return this->preset(this->first_visible_idx()); }
    const Preset&   first_visible() const       { return this->preset(this->first_visible_idx()); }
    Preset&         first_compatible()          { return this->preset(this->first_compatible_idx()); }
    const Preset&   first_compatible() const    { return this->preset(this->first_compatible_idx()); }

    // Return number of presets including the "- default -" preset.
    size_t          size() const                { return this->m_presets.size(); }

    // For Print / Filament presets, disable those, which are not compatible with the printer.
    void            update_compatible_with_printer(const Preset &active_printer, bool select_other_if_incompatible);

    size_t          num_visible() const { return std::count_if(m_presets.begin(), m_presets.end(), [](const Preset &preset){return preset.is_visible;}); }

    // Compare the content of get_selected_preset() with get_edited_preset() configs, return true if they differ.
    bool                        current_is_dirty() const { return ! this->current_dirty_options().empty(); }
    // Compare the content of get_selected_preset() with get_edited_preset() configs, return the list of keys where they differ.
    std::vector<std::string>    current_dirty_options() const 
        { return dirty_options(&this->get_edited_preset(), &this->get_selected_preset()); }
    // Compare the content of get_selected_preset() with get_edited_preset() configs, return the list of keys where they differ.
    std::vector<std::string>    current_different_from_parent_options() const
        { return dirty_options(&this->get_edited_preset(), this->get_selected_preset_parent()); }
    // Compare the content of get_selected_preset() with get_selected_preset_parent() configs, return the list of keys where they equal.
	std::vector<std::string>    system_equal_options() const;

    // Update the choice UI from the list of presets.
    // If show_incompatible, all presets are shown, otherwise only the compatible presets are shown.
    // If an incompatible preset is selected, it is shown as well.
    void            update_tab_ui(wxBitmapComboBox *ui, bool show_incompatible);
    // Update the choice UI from the list of presets.
    // Only the compatible presets are shown.
    // If an incompatible preset is selected, it is shown as well.
    void            update_platter_ui(wxBitmapComboBox *ui);

    // Update a dirty floag of the current preset, update the labels of the UI component accordingly.
    // Return true if the dirty flag changed.
    bool            update_dirty_ui(wxBitmapComboBox *ui);
    
    // Select a profile by its name. Return true if the selection changed.
    // Without force, the selection is only updated if the index changes.
    // With force, the changes are reverted if the new index is the same as the old index.
    bool            select_preset_by_name(const std::string &name, bool force);

    // Generate a file path from a profile name. Add the ".ini" suffix if it is missing.
    std::string     path_from_name(const std::string &new_name) const;

private:
    PresetCollection();
    PresetCollection(const PresetCollection &other);
    PresetCollection& operator=(const PresetCollection &other);

    // Find a preset in the sorted list of presets.
    // The "-- default -- " preset is always the first, so it needs
    // to be handled differently.
    std::deque<Preset>::iterator find_preset_internal(const std::string &name)
    {
        Preset key(m_type, name);
        auto it = std::lower_bound(m_presets.begin() + 1, m_presets.end(), key);
        return ((it == m_presets.end() || it->name != name) && m_presets.front().name == name) ? m_presets.begin() : it;
    }
    std::deque<Preset>::const_iterator find_preset_internal(const std::string &name) const
        { return const_cast<PresetCollection*>(this)->find_preset_internal(name); }

    static std::vector<std::string> dirty_options(const Preset *edited, const Preset *reference);

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
    const wxBitmap         *m_bitmap_compatible   = nullptr;
    const wxBitmap         *m_bitmap_incompatible = nullptr;
    const wxBitmap         *m_bitmap_lock         = nullptr;
    const wxBitmap         *m_bitmap_lock_open    = nullptr;
    // Marks placed at the wxBitmapComboBox of a MainFrame.
    // These bitmaps are owned by PresetCollection.
    wxBitmap               *m_bitmap_main_frame;
    // Path to the directory to store the config files into.
    std::string             m_dir_path;
};

} // namespace Slic3r

#endif /* slic3r_Preset_hpp_ */
