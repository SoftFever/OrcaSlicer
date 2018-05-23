#ifndef slic3r_PresetBundle_hpp_
#define slic3r_PresetBundle_hpp_

#include "AppConfig.hpp"
#include "Preset.hpp"

#include <set>
#include <boost/filesystem/path.hpp>

namespace Slic3r {

namespace GUI {
    class BitmapCache;
};

class PlaceholderParser;

// Bundle of Print + Filament + Printer presets.
class PresetBundle
{
public:
    PresetBundle();
    ~PresetBundle();

    // Remove all the presets but the "-- default --".
    // Optionally remove all the files referenced by the presets from the user profile directory.
    void            reset(bool delete_files);

    void            setup_directories();

    // Load ini files of all types (print, filament, printer) from Slic3r::data_dir() / presets.
    // Load selections (current print, current filaments, current printer) from config.ini
    // This is done just once on application start up.
    void            load_presets(const AppConfig &config);

    // Export selections (current print, current filaments, current printer) into config.ini
    void            export_selections(AppConfig &config);
    // Export selections (current print, current filaments, current printer) into a placeholder parser.
    void            export_selections(PlaceholderParser &pp);

    PresetCollection            prints;
    PresetCollection            filaments;
    PresetCollection            printers;
    // Filament preset names for a multi-extruder or multi-material print.
    // extruders.size() should be the same as printers.get_edited_preset().config.nozzle_diameter.size()
    std::vector<std::string>    filament_presets;

    // The project configuration values are kept separated from the print/filament/printer preset,
    // they are being serialized / deserialized from / to the .amf, .3mf, .config, .gcode, 
    // and they are being used by slicing core.
    DynamicPrintConfig          project_config;

    // There will be an entry for each system profile loaded, 
    // and the system profiles will point to the VendorProfile instances owned by PresetBundle::vendors.
    std::set<VendorProfile>     vendors;

    struct ObsoletePresets {
        std::vector<std::string> prints;
        std::vector<std::string> filaments;
        std::vector<std::string> printers;
    };
    ObsoletePresets             obsolete_presets;

    bool                        has_defauls_only() const 
        { return prints.size() <= 1 && filaments.size() <= 1 && printers.size() <= 1; }

    DynamicPrintConfig          full_config() const;

    // Load user configuration and store it into the user profiles.
    // This method is called by the configuration wizard.
    void                        load_config(const std::string &name, DynamicPrintConfig config)
        { this->load_config_file_config(name, false, std::move(config)); }

    // Load an external config file containing the print, filament and printer presets.
    // Instead of a config file, a G-code may be loaded containing the full set of parameters.
    // In the future the configuration will likely be read from an AMF file as well.
    // If the file is loaded successfully, its print / filament / printer profiles will be activated.
    void                        load_config_file(const std::string &path);

    // Load an external config source containing the print, filament and printer presets.
    // The given string must contain the full set of parameters (same as those exported to gcode).
    // If the string is parsed successfully, its print / filament / printer profiles will be activated.
    void                        load_config_string(const char* str, const char* source_filename = nullptr);

    // Load a config bundle file, into presets and store the loaded presets into separate files
    // of the local configuration directory.
    // Load settings into the provided settings instance.
    // Activate the presets stored in the config bundle.
    // Returns the number of presets loaded successfully.
    enum { 
        // Save the profiles, which have been loaded.
        LOAD_CFGBNDLE_SAVE = 1, 
        // Delete all old config profiles before loading.
        LOAD_CFGBNDLE_RESET_USER_PROFILE = 2,
        // Load a system config bundle.
        LOAD_CFGBNDLE_SYSTEM = 4,
        LOAD_CFGBUNDLE_VENDOR_ONLY = 8,
    };
    // Load the config bundle, store it to the user profile directory by default.
    size_t                      load_configbundle(const std::string &path, unsigned int flags = LOAD_CFGBNDLE_SAVE);

    // Export a config bundle file containing all the presets and the names of the active presets.
    void                        export_configbundle(const std::string &path); // , const DynamicPrintConfig &settings);

    // Update a filament selection combo box on the platter for an idx_extruder.
    void                        update_platter_filament_ui(unsigned int idx_extruder, wxBitmapComboBox *ui);

    // Enable / disable the "- default -" preset.
    void                        set_default_suppressed(bool default_suppressed);

    // Set the filament preset name. As the name could come from the UI selection box, 
    // an optional "(modified)" suffix will be removed from the filament name.
    void                        set_filament_preset(size_t idx, const std::string &name);

    // Read out the number of extruders from an active printer preset,
    // update size and content of filament_presets.
    void                        update_multi_material_filament_presets();

    // Update the is_compatible flag of all print and filament presets depending on whether they are marked
    // as compatible with the currently selected printer.
    // Also updates the is_visible flag of each preset.
    // If select_other_if_incompatible is true, then the print or filament preset is switched to some compatible
    // preset if the current print or filament preset is not compatible.
    void                        update_compatible_with_printer(bool select_other_if_incompatible);

    static bool                 parse_color(const std::string &scolor, unsigned char *rgb_out);

private:
    std::string                 load_system_presets();
    // Merge one vendor's presets with the other vendor's presets, report duplicates.
    std::vector<std::string>    merge_presets(PresetBundle &&other);

    // Set the "enabled" flag for printer vendors, printer models and printer variants
    // based on the user configuration.
    // If the "vendor" section is missing, enable all models and variants of the particular vendor.
    void                        load_installed_printers(const AppConfig &config);

    // Load selections (current print, current filaments, current printer) from config.ini
    // This is done just once on application start up.
    void                        load_selections(const AppConfig &config);

    // Load print, filament & printer presets from a config. If it is an external config, then the name is extracted from the external path.
    // and the external config is just referenced, not stored into user profile directory.
    // If it is not an external config, then the config will be stored into the user profile directory.
    void                        load_config_file_config(const std::string &name_or_path, bool is_external, DynamicPrintConfig &&config);
    void                        load_config_file_config_bundle(const std::string &path, const boost::property_tree::ptree &tree);
    bool                        load_compatible_bitmaps();

    // Indicator, that the preset is compatible with the selected printer.
    wxBitmap                            *m_bitmapCompatible;
    // Indicator, that the preset is NOT compatible with the selected printer.
    wxBitmap                            *m_bitmapIncompatible;
    // Indicator, that the preset is system and not modified.
    wxBitmap                            *m_bitmapLock;
    // Indicator, that the preset is system and user modified.
    wxBitmap                            *m_bitmapLockOpen;
    // Caching color bitmaps for the filament combo box.
    GUI::BitmapCache                    *m_bitmapCache;
};

} // namespace Slic3r

#endif /* slic3r_PresetBundle_hpp_ */
