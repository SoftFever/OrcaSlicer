#ifndef slic3r_PresetBundle_hpp_
#define slic3r_PresetBundle_hpp_

#include "AppConfig.hpp"
#include "Preset.hpp"

namespace Slic3r {

// Bundle of Print + Filament + Printer presets.
class PresetBundle
{
public:
    PresetBundle();
    ~PresetBundle();

    void            setup_directories();

    // Load ini files of all types (print, filament, printer) from the provided directory path.
    void            load_presets(const std::string &dir_path);

    // Load selections (current print, current filaments, current printer) from config.ini
    // This is done just once on application start up.
    void            load_selections(const AppConfig &config);
    // Export selections (current print, current filaments, current printer) into config.ini
    void            export_selections(AppConfig &config);

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
    // Activate the presets stored in the config bundle.
    // Returns the number of presets loaded successfully.
    size_t                      load_configbundle(const std::string &path);

    // Export a config bundle file containing all the presets and the names of the active presets.
    void                        export_configbundle(const std::string &path); // , const DynamicPrintConfig &settings);

    // Update a filament selection combo box on the platter for an idx_extruder.
    void                        update_platter_filament_ui(unsigned int idx_extruder, wxBitmapComboBox *ui);
    // Update the colors preview at the platter extruder combo box.
    void                        update_platter_filament_ui_colors(unsigned int idx_extruder, wxBitmapComboBox *ui);

    // Enable / disable the "- default -" preset.
    void                        set_default_suppressed(bool default_suppressed);

    // Set the filament preset name. As the name could come from the UI selection box, 
    // an optional "(modified)" suffix will be removed from the filament name.
    void                        set_filament_preset(size_t idx, const std::string &name);

    // Read out the number of extruders from an active printer preset,
    // update size and content of filament_presets.
    void                        update_multi_material_filament_presets();

private:
    void                        load_config_file_config(const std::string &path, const boost::property_tree::ptree &tree);
    void                        load_config_file_config_bundle(const std::string &path, const boost::property_tree::ptree &tree);
    bool                        load_compatible_bitmaps(const std::string &path_bitmap_compatible, const std::string &path_bitmap_incompatible);

    // Indicator, that the preset is compatible with the selected printer.
    wxBitmap                            *m_bitmapCompatible;
    // Indicator, that the preset is NOT compatible with the selected printer.
    wxBitmap                            *m_bitmapIncompatible;
    // Caching color bitmaps for the 
    std::map<std::string, wxBitmap*>     m_mapColorToBitmap;
};

} // namespace Slic3r

#endif /* slic3r_PresetBundle_hpp_ */
