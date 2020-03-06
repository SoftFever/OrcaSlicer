#ifndef slic3r_Preset_hpp_
#define slic3r_Preset_hpp_

#include <deque>
#include <set>
#include <unordered_map>

#include <boost/filesystem/path.hpp>
#include <boost/property_tree/ptree_fwd.hpp>

#include "libslic3r/libslic3r.h"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/Semver.hpp"

class wxBitmap;
class wxBitmapComboBox;
class wxChoice;
class wxItemContainer;
class wxString;
class wxWindow;

namespace Slic3r {

class AppConfig;
class PresetBundle;

namespace GUI {
	class BitmapCache;
    class PresetComboBox;
}

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
    Semver                          config_version;
    std::string                     config_update_url;
    std::string                     changelog_url;

    struct PrinterVariant {
        PrinterVariant() {}
        PrinterVariant(const std::string &name) : name(name) {}
        std::string                 name;
    };

    struct PrinterModel {
        PrinterModel() {}
        std::string                 id;
        std::string                 name;
        PrinterTechnology           technology;
        std::string                 family;
        std::vector<PrinterVariant> variants;
        std::vector<std::string>	default_materials;
        // Vendor & Printer Model specific print bed model & texture.
        std::string 			 	bed_model;
        std::string 				bed_texture;

        PrinterVariant*       variant(const std::string &name) {
            for (auto &v : this->variants)
                if (v.name == name)
                    return &v;
            return nullptr;
        }

        const PrinterVariant* variant(const std::string &name) const { return const_cast<PrinterModel*>(this)->variant(name); }
    };
    std::vector<PrinterModel>          models;

    std::set<std::string>              default_filaments;
    std::set<std::string>              default_sla_materials;

    VendorProfile() {}
    VendorProfile(std::string id) : id(std::move(id)) {}

    bool 		valid() const { return ! name.empty() && ! id.empty() && config_version.valid(); }

    // Load VendorProfile from an ini file.
    // If `load_all` is false, only the header with basic info (name, version, URLs) is loaded.
    static VendorProfile from_ini(const boost::filesystem::path &path, bool load_all=true);
    static VendorProfile from_ini(const boost::property_tree::ptree &tree, const boost::filesystem::path &path, bool load_all=true);

    size_t      num_variants() const { size_t n = 0; for (auto &model : models) n += model.variants.size(); return n; }
    std::vector<std::string> families() const;

    bool        operator< (const VendorProfile &rhs) const { return this->id <  rhs.id; }
    bool        operator==(const VendorProfile &rhs) const { return this->id == rhs.id; }
};

class Preset;

// Helper to hold a profile with its vendor definition, where the vendor definition may have been extracted from a parent system preset.
// The parent preset is only accessible through PresetCollection, therefore to allow definition of the various is_compatible_with methods
// outside of the PresetCollection, this composite is returned by PresetCollection::get_preset_with_vendor_profile() when needed.
struct PresetWithVendorProfile {
	PresetWithVendorProfile(const Preset &preset, const VendorProfile *vendor) : preset(preset), vendor(vendor) {}
	const Preset 		&preset;
	const VendorProfile *vendor;
};

// Note: it is imporant that map is used here rather than unordered_map,
// because we need iterators to not be invalidated,
// because Preset and the ConfigWizard hold pointers to VendorProfiles.
// XXX: maybe set is enough (cf. changes in Wizard)
typedef std::map<std::string, VendorProfile> VendorMap;

class Preset
{
public:
    enum Type
    {
        TYPE_INVALID,
        TYPE_PRINT,
        TYPE_SLA_PRINT,
        TYPE_FILAMENT,
        TYPE_SLA_MATERIAL,
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
    // Preset is visible, if it is associated with a printer model / variant that is enabled in the AppConfig
    // or if it has no printer model / variant association.
    // Also the "default" preset is only visible, if it is the only preset in the list.
    bool                is_visible  = true;
    // Has this preset been modified?
    bool                is_dirty    = false;
    // Is this preset compatible with the currently active printer?
    bool                is_compatible = true;

    bool                is_user() const { return ! this->is_default && ! this->is_system; }

    // Name of the preset, usually derived form the file name.
    std::string         name;
    // File name of the preset. This could be a Print / Filament / Printer preset, 
    // or a Configuration file bundling the Print + Filament + Printer presets (in that case is_external and possibly is_system will be true),
    // or it could be a G-code (again, is_external will be true).
    std::string         file;
    // If this is a system profile, then there should be a vendor data available to display at the UI.
    const VendorProfile *vendor      = nullptr;

    // Has this profile been loaded?
    bool                loaded      = false;

    // Configuration data, loaded from a file, or set from the defaults.
    DynamicPrintConfig  config;

    // Alias of the preset
    std::string         alias;
    // List of profile names, from which this profile was renamed at some point of time.
    // This list is then used to match profiles by their names when loaded from .gcode, .3mf, .amf,
    // and to match the "inherits" field of user profiles with updated system profiles.
    std::vector<std::string> renamed_from;

    void                save();

    // Return a label of this preset, consisting of a name and a "(modified)" suffix, if this preset is dirty.
    std::string         label() const;

    // Set the is_dirty flag if the provided config is different from the active one.
    void                set_dirty(const DynamicPrintConfig &config) { this->is_dirty = ! this->config.diff(config).empty(); }
    void                set_dirty(bool dirty = true) { this->is_dirty = dirty; }
    void                reset_dirty() { this->is_dirty = false; }

    // Returns the name of the preset, from which this preset inherits.
    static std::string& inherits(DynamicPrintConfig &cfg) { return cfg.option<ConfigOptionString>("inherits", true)->value; }
    std::string&        inherits() { return Preset::inherits(this->config); }
    const std::string&  inherits() const { return Preset::inherits(const_cast<Preset*>(this)->config); }

    // Returns the "compatible_prints_condition".
    static std::string& compatible_prints_condition(DynamicPrintConfig &cfg) { return cfg.option<ConfigOptionString>("compatible_prints_condition", true)->value; }
    std::string&        compatible_prints_condition() { 
		assert(this->type == TYPE_FILAMENT || this->type == TYPE_SLA_MATERIAL);
        return Preset::compatible_prints_condition(this->config);
    }
    const std::string&  compatible_prints_condition() const { return const_cast<Preset*>(this)->compatible_prints_condition(); }

    // Returns the "compatible_printers_condition".
    static std::string& compatible_printers_condition(DynamicPrintConfig &cfg) { return cfg.option<ConfigOptionString>("compatible_printers_condition", true)->value; }
    std::string&        compatible_printers_condition() {
		assert(this->type == TYPE_PRINT || this->type == TYPE_SLA_PRINT || this->type == TYPE_FILAMENT || this->type == TYPE_SLA_MATERIAL);
        return Preset::compatible_printers_condition(this->config);
    }
    const std::string&  compatible_printers_condition() const { return const_cast<Preset*>(this)->compatible_printers_condition(); }

    // Return a printer technology, return ptFFF if the printer technology is not set.
    static PrinterTechnology printer_technology(const DynamicPrintConfig &cfg) {
        auto *opt = cfg.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
        // The following assert may trigger when importing some legacy profile, 
        // but it is safer to keep it here to capture the cases where the "printer_technology" key is queried, where it should not.
//        assert(opt != nullptr);
        return (opt == nullptr) ? ptFFF : opt->value;
    }
    PrinterTechnology   printer_technology() const { return Preset::printer_technology(this->config); }
    // This call returns a reference, it may add a new entry into the DynamicPrintConfig.
    PrinterTechnology&  printer_technology_ref() { return this->config.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology", true)->value; }

    // Set is_visible according to application config
    void                set_visible_from_appconfig(const AppConfig &app_config);

    // Resize the extruder specific fields, initialize them with the content of the 1st extruder.
    void                set_num_extruders(unsigned int n) { this->config.set_num_extruders(n); }

    // Sort lexicographically by a preset name. The preset name shall be unique across a single PresetCollection.
    bool                operator<(const Preset &other) const { return this->name < other.name; }

    static const std::vector<std::string>&  print_options();
    static const std::vector<std::string>&  filament_options();
    // Printer options contain the nozzle options.
    static const std::vector<std::string>&  printer_options();
    // Nozzle options of the printer options.
    static const std::vector<std::string>&  nozzle_options();

    static const std::vector<std::string>&  sla_printer_options();
    static const std::vector<std::string>&  sla_material_options();
    static const std::vector<std::string>&  sla_print_options();

	static void                             update_suffix_modified();
    static const std::string&               suffix_modified();
    static void                             normalize(DynamicPrintConfig &config);
    // Report configuration fields, which are misplaced into a wrong group, remove them from the config.
    static std::string                      remove_invalid_keys(DynamicPrintConfig &config, const DynamicPrintConfig &default_config);

protected:
    friend class        PresetCollection;
    friend class        PresetBundle;
    static std::string  remove_suffix_modified(const std::string &name);
};

bool is_compatible_with_print  (const PresetWithVendorProfile &preset, const PresetWithVendorProfile &active_print, const PresetWithVendorProfile &active_printer);
bool is_compatible_with_printer(const PresetWithVendorProfile &preset, const PresetWithVendorProfile &active_printer, const DynamicPrintConfig *extra_config);
bool is_compatible_with_printer(const PresetWithVendorProfile &preset, const PresetWithVendorProfile &active_printer);

enum class PresetSelectCompatibleType {
	// Never select a compatible preset if the newly selected profile is not compatible.
	Never,
	// Only select a compatible preset if the active profile used to be compatible, but it is no more.
	OnlyIfWasCompatible,
	// Always select a compatible preset if the active profile is no more compatible.
	Always
};

// Collections of presets of the same type (one of the Print, Filament or Printer type).
class PresetCollection
{
public:
    // Initialize the PresetCollection with the "- default -" preset.
    PresetCollection(Preset::Type type, const std::vector<std::string> &keys, const Slic3r::StaticPrintConfig &defaults, const std::string &default_name = "- default -");
    ~PresetCollection();

    typedef std::deque<Preset>::iterator Iterator;
    typedef std::deque<Preset>::const_iterator ConstIterator;
    Iterator        begin() { return m_presets.begin() + m_num_default_presets; }
    ConstIterator   begin() const { return m_presets.cbegin() + m_num_default_presets; }
    ConstIterator   cbegin() const { return m_presets.cbegin() + m_num_default_presets; }
    Iterator        end() { return m_presets.end(); }
    ConstIterator   end() const { return m_presets.cend(); }
    ConstIterator   cend() const { return m_presets.cend(); }

    void            reset(bool delete_files);

    Preset::Type    type() const { return m_type; }
    // Name, to be used on the screen and in error messages. Not localized.
    std::string     name() const;
    // Name, to be used as a section name in config bundle, and as a folder name for presets.
    std::string     section_name() const;
    const std::deque<Preset>& operator()() const { return m_presets; }

    // Add default preset at the start of the collection, increment the m_default_preset counter.
    void            add_default_preset(const std::vector<std::string> &keys, const Slic3r::StaticPrintConfig &defaults, const std::string &preset_name);

    // Load ini files of the particular type from the provided directory path.
    void            load_presets(const std::string &dir_path, const std::string &subdir);

    // Load a preset from an already parsed config file, insert it into the sorted sequence of presets
    // and select it, losing previous modifications.
    Preset&         load_preset(const std::string &path, const std::string &name, const DynamicPrintConfig &config, bool select = true);
    Preset&         load_preset(const std::string &path, const std::string &name, DynamicPrintConfig &&config, bool select = true);

    Preset&         load_external_preset(
        // Path to the profile source file (a G-code, an AMF or 3MF file, a config file)
        const std::string           &path,
        // Name of the profile, derived from the source file name.
        const std::string           &name,
        // Original name of the profile, extracted from the loaded config. Empty, if the name has not been stored.
        const std::string           &original_name,
        // Config to initialize the preset from.
        const DynamicPrintConfig    &config,
        // Select the preset after loading?
        bool                         select = true);

    // Save the preset under a new name. If the name is different from the old one,
    // a new preset is stored into the list of presets.
    // All presets are marked as not modified and the new preset is activated.
    void            save_current_preset(const std::string &new_name, bool detach = false);

    // Delete the current preset, activate the first visible preset.
    // returns true if the preset was deleted successfully.
    bool            delete_current_preset();
    // Delete the current preset, activate the first visible preset.
    // returns true if the preset was deleted successfully.
    bool            delete_preset(const std::string& name);

    // Load default bitmap to be placed at the wxBitmapComboBox of a MainFrame.
    void            load_bitmap_default(const std::string &file_name);

    // Load "add new printer" bitmap to be placed at the wxBitmapComboBox of a MainFrame.
    void            load_bitmap_add(const std::string &file_name);

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
    // Returns the name of the selected preset, or an empty string if no preset is selected.
    std::string     get_selected_preset_name() const { return (m_idx_selected == -1) ? std::string() : this->get_selected_preset().name; }
    // For the current edited preset, return the parent preset if there is one.
    // If there is no parent preset, nullptr is returned.
    // The parent preset may be a system preset or a user preset, which will be
    // reflected by the UI.
    const Preset*   get_selected_preset_parent() const;
	// Get parent preset for a child preset, based on the "inherits" field of a child,
	// where the "inherits" profile name is searched for in both m_presets and m_map_system_profile_renamed.
	const Preset*	get_preset_parent(const Preset& child) const;
	// Return the selected preset including the user modifications.
    Preset&         get_edited_preset()         { return m_edited_preset; }
    const Preset&   get_edited_preset() const   { return m_edited_preset; }

    // Return vendor of the first parent profile, for which the vendor is defined, or null if such profile does not exist.
    PresetWithVendorProfile get_preset_with_vendor_profile(const Preset &preset) const;
    PresetWithVendorProfile get_edited_preset_with_vendor_profile() const { return this->get_preset_with_vendor_profile(this->get_edited_preset()); }

    const std::string& get_preset_name_by_alias(const std::string& alias) const;

	// used to update preset_choice from Tab
	const std::deque<Preset>&	get_presets() const	{ return m_presets; }
	int							get_idx_selected()	{ return m_idx_selected; }
	static const std::string&	get_suffix_modified();

    // Return a preset possibly with modifications.
	Preset&			default_preset(size_t idx = 0)		 { assert(idx < m_num_default_presets); return m_presets[idx]; }
	const Preset&   default_preset(size_t idx = 0) const { assert(idx < m_num_default_presets); return m_presets[idx]; }
	virtual const Preset& default_preset_for(const DynamicPrintConfig & /* config */) const { return this->default_preset(); }
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
    // Return index of the first compatible preset. Certainly at least the '- default -' preset shall be compatible.
    // If one of the prefered_alternates is compatible, select it.
    template<typename PreferedCondition>
    size_t          first_compatible_idx(PreferedCondition prefered_condition) const
    {
        size_t i = m_default_suppressed ? m_num_default_presets : 0;
        size_t n = this->m_presets.size();
        size_t i_compatible = n;
        for (; i < n; ++ i)
            // Since we use the filament selection from Wizard, it's needed to control the preset visibility too 
            if (m_presets[i].is_compatible && m_presets[i].is_visible) {
                if (prefered_condition(m_presets[i].name))
                    return i;
                if (i_compatible == n)
                    // Store the first compatible profile into i_compatible.
                    i_compatible = i;
            }
        return (i_compatible == n) ? 0 : i_compatible;
    }
    // Return index of the first compatible preset. Certainly at least the '- default -' preset shall be compatible.
    size_t          first_compatible_idx() const { return this->first_compatible_idx([](const std::string&){return true;}); }

    // Return index of the first visible preset. Certainly at least the '- default -' preset shall be visible.
    // Return the first visible preset. Certainly at least the '- default -' preset shall be visible.
    Preset&         first_visible()             { return this->preset(this->first_visible_idx()); }
    const Preset&   first_visible() const       { return this->preset(this->first_visible_idx()); }
    Preset&         first_compatible()          { return this->preset(this->first_compatible_idx()); }
    template<typename PreferedCondition>
    Preset&         first_compatible(PreferedCondition prefered_condition) { return this->preset(this->first_compatible_idx(prefered_condition)); }
    const Preset&   first_compatible() const    { return this->preset(this->first_compatible_idx()); }

    // Return number of presets including the "- default -" preset.
    size_t          size() const                { return m_presets.size(); }
    bool            has_defaults_only() const   { return m_presets.size() <= m_num_default_presets; }

    // For Print / Filament presets, disable those, which are not compatible with the printer.
    template<typename PreferedCondition>
    void            update_compatible(const PresetWithVendorProfile &active_printer, const PresetWithVendorProfile *active_print, PresetSelectCompatibleType select_other_if_incompatible, PreferedCondition prefered_condition)
    {
        if (this->update_compatible_internal(active_printer, active_print, select_other_if_incompatible) == (size_t)-1)
            // Find some other compatible preset, or the "-- default --" preset.
            this->select_preset(this->first_compatible_idx(prefered_condition));        
    }
    void            update_compatible(const PresetWithVendorProfile &active_printer, const PresetWithVendorProfile *active_print, PresetSelectCompatibleType select_other_if_incompatible)
        { this->update_compatible(active_printer, active_print, select_other_if_incompatible, [](const std::string&){return true;}); }

    size_t          num_visible() const { return std::count_if(m_presets.begin(), m_presets.end(), [](const Preset &preset){return preset.is_visible;}); }

    // Compare the content of get_selected_preset() with get_edited_preset() configs, return true if they differ.
    bool                        current_is_dirty() const { return ! this->current_dirty_options().empty(); }
    // Compare the content of get_selected_preset() with get_edited_preset() configs, return the list of keys where they differ.
    std::vector<std::string>    current_dirty_options(const bool deep_compare = false) const
        { return dirty_options(&this->get_edited_preset(), &this->get_selected_preset(), deep_compare); }
    // Compare the content of get_selected_preset() with get_edited_preset() configs, return the list of keys where they differ.
    std::vector<std::string>    current_different_from_parent_options(const bool deep_compare = false) const
        { return dirty_options(&this->get_edited_preset(), this->get_selected_preset_parent(), deep_compare); }

    // Return a sorted list of system preset names.
    std::vector<std::string>    system_preset_names() const;

    // Update the choice UI from the list of presets.
    // If show_incompatible, all presets are shown, otherwise only the compatible presets are shown.
    // If an incompatible preset is selected, it is shown as well.
    size_t          update_tab_ui(wxBitmapComboBox *ui, bool show_incompatible, const int em = 10);
    // Update the choice UI from the list of presets.
    // Only the compatible presets are shown.
    // If an incompatible preset is selected, it is shown as well.
    void            update_plater_ui(GUI::PresetComboBox *ui);

    // Update a dirty floag of the current preset, update the labels of the UI component accordingly.
    // Return true if the dirty flag changed.
    bool            update_dirty_ui(wxBitmapComboBox *ui);
    
    // Select a profile by its name. Return true if the selection changed.
    // Without force, the selection is only updated if the index changes.
    // With force, the changes are reverted if the new index is the same as the old index.
    bool            select_preset_by_name(const std::string &name, bool force);

    // Generate a file path from a profile name. Add the ".ini" suffix if it is missing.
    std::string     path_from_name(const std::string &new_name) const;

    void            clear_bitmap_cache();

#ifdef __linux__
	static const char* separator_head() { return "------- "; }
	static const char* separator_tail() { return " -------"; }
#else /* __linux__ */
    static const char* separator_head() { return "————— "; }
    static const char* separator_tail() { return " —————"; }
#endif /* __linux__ */
	static wxString    separator(const std::string &label);

protected:
    // Select a preset, if it exists. If it does not exist, select an invalid (-1) index.
    // This is a temporary state, which shall be fixed immediately by the following step.
    bool            select_preset_by_name_strict(const std::string &name);

    // Merge one vendor's presets with the other vendor's presets, report duplicates.
    std::vector<std::string> merge_presets(PresetCollection &&other, const VendorMap &new_vendors);

    // Update m_map_alias_to_profile_name from loaded system profiles.
	void 			update_map_alias_to_profile_name();

    // Update m_map_system_profile_renamed from loaded system profiles.
    void 			update_map_system_profile_renamed();

private:
    PresetCollection();
    PresetCollection(const PresetCollection &other);
    PresetCollection& operator=(const PresetCollection &other);

    // Find a preset position in the sorted list of presets.
    // The "-- default -- " preset is always the first, so it needs
    // to be handled differently.
    // If a preset does not exist, an iterator is returned indicating where to insert a preset with the same name.
    std::deque<Preset>::iterator find_preset_internal(const std::string &name)
    {
        Preset key(m_type, name);
        auto it = std::lower_bound(m_presets.begin() + m_num_default_presets, m_presets.end(), key);
        if (it == m_presets.end() || it->name != name) {
            // Preset has not been not found in the sorted list of non-default presets. Try the defaults.
            for (size_t i = 0; i < m_num_default_presets; ++ i)
                if (m_presets[i].name == name) {
                    it = m_presets.begin() + i;
                    break;
                }
        }
        return it;
    }
    std::deque<Preset>::const_iterator find_preset_internal(const std::string &name) const
        { return const_cast<PresetCollection*>(this)->find_preset_internal(name); }
    std::deque<Preset>::iterator 	   find_preset_renamed(const std::string &name) {
    	auto it_renamed = m_map_system_profile_renamed.find(name);
    	auto it = (it_renamed == m_map_system_profile_renamed.end()) ? m_presets.end() : this->find_preset_internal(it_renamed->second);
    	assert((it_renamed == m_map_system_profile_renamed.end()) || (it != m_presets.end() && it->name == it_renamed->second));
    	return it;
    }
    std::deque<Preset>::const_iterator find_preset_renamed(const std::string &name) const
        { return const_cast<PresetCollection*>(this)->find_preset_renamed(name); }

    size_t update_compatible_internal(const PresetWithVendorProfile &active_printer, const PresetWithVendorProfile *active_print, PresetSelectCompatibleType unselect_if_incompatible);

    static std::vector<std::string> dirty_options(const Preset *edited, const Preset *reference, const bool is_printer_type = false);

    // Type of this PresetCollection: TYPE_PRINT, TYPE_FILAMENT or TYPE_PRINTER.
    Preset::Type            m_type;
    // List of presets, starting with the "- default -" preset.
    // Use deque to force the container to allocate an object per each entry, 
    // so that the addresses of the presets don't change during resizing of the container.
    std::deque<Preset>      m_presets;
    // System profiles may have aliases. Map to the full profile name.
    std::vector<std::pair<std::string, std::string>> m_map_alias_to_profile_name;
    // Map from old system profile name to a current system profile name.
    std::map<std::string, std::string> m_map_system_profile_renamed;
    // Initially this preset contains a copy of the selected preset. Later on, this copy may be modified by the user.
    Preset                  m_edited_preset;
    // Selected preset.
    int                     m_idx_selected;
    // Is the "- default -" preset suppressed?
    bool                    m_default_suppressed  = true;
    size_t                  m_num_default_presets = 0;
    // Compatible & incompatible marks, to be placed at the wxBitmapComboBox items of a Plater.
    // These bitmaps are not owned by PresetCollection, but by a PresetBundle.
    const wxBitmap         *m_bitmap_compatible   = nullptr;
    const wxBitmap         *m_bitmap_incompatible = nullptr;
    const wxBitmap         *m_bitmap_lock         = nullptr;
    const wxBitmap         *m_bitmap_lock_open    = nullptr;
    // Marks placed at the wxBitmapComboBox of a MainFrame.
    // These bitmaps are owned by PresetCollection.
    wxBitmap               *m_bitmap_main_frame;
    // "Add printer profile" icon, owned by PresetCollection.
    wxBitmap               *m_bitmap_add;
    // Path to the directory to store the config files into.
    std::string             m_dir_path;

	// Caching color bitmaps for the filament combo box.
	GUI::BitmapCache       *m_bitmap_cache = nullptr;

    // to access select_preset_by_name_strict()
    friend class PresetBundle;
};

// Printer supports the FFF and SLA technologies, with different set of configuration values,
// therefore this PresetCollection needs to handle two defaults.
class PrinterPresetCollection : public PresetCollection
{
public:
    PrinterPresetCollection(Preset::Type type, const std::vector<std::string> &keys, const Slic3r::StaticPrintConfig &defaults, const std::string &default_name = "- default -") :
		PresetCollection(type, keys, defaults, default_name) {}
    const Preset&   default_preset_for(const DynamicPrintConfig &config) const override;

    const Preset*   find_by_model_id(const std::string &model_id) const;
};

namespace PresetUtils {
	// PrinterModel of a system profile, from which this preset is derived, or null if it is not derived from a system profile.
	const VendorProfile::PrinterModel* system_printer_model(const Preset &preset);
} // namespace PresetUtils

} // namespace Slic3r

#endif /* slic3r_Preset_hpp_ */
