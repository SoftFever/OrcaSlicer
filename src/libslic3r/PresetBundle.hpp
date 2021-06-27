#ifndef slic3r_PresetBundle_hpp_
#define slic3r_PresetBundle_hpp_

#include "Preset.hpp"
#include "AppConfig.hpp"
#include "enum_bitmask.hpp"

#include <memory>
#include <unordered_map>
#include <boost/filesystem/path.hpp>

namespace Slic3r {

// Bundle of Print + Filament + Printer presets.
class PresetBundle
{
public:
    PresetBundle();
    PresetBundle(const PresetBundle &rhs);
    PresetBundle& operator=(const PresetBundle &rhs);

    // Remove all the presets but the "-- default --".
    // Optionally remove all the files referenced by the presets from the user profile directory.
    void            reset(bool delete_files);

    void            setup_directories();

    // Load ini files of all types (print, filament, printer) from Slic3r::data_dir() / presets.
    // Load selections (current print, current filaments, current printer) from config.ini
    PresetsConfigSubstitutions load_presets(AppConfig &config, ForwardCompatibilitySubstitutionRule rule, const std::string &preferred_model_id = std::string());

    // Export selections (current print, current filaments, current printer) into config.ini
    void            export_selections(AppConfig &config);

    PresetCollection            prints;
    PresetCollection            sla_prints;
    PresetCollection            filaments;
    PresetCollection            sla_materials;
	PresetCollection& 			materials(PrinterTechnology pt)       { return pt == ptFFF ? this->filaments : this->sla_materials; }
	const PresetCollection& 	materials(PrinterTechnology pt) const { return pt == ptFFF ? this->filaments : this->sla_materials; }
    PrinterPresetCollection     printers;
    PhysicalPrinterCollection   physical_printers;
    // Filament preset names for a multi-extruder or multi-material print.
    // extruders.size() should be the same as printers.get_edited_preset().config.nozzle_diameter.size()
    std::vector<std::string>    filament_presets;

    // The project configuration values are kept separated from the print/filament/printer preset,
    // they are being serialized / deserialized from / to the .amf, .3mf, .config, .gcode, 
    // and they are being used by slicing core.
    DynamicPrintConfig          project_config;

    // There will be an entry for each system profile loaded, 
    // and the system profiles will point to the VendorProfile instances owned by PresetBundle::vendors.
    VendorMap                   vendors;

    struct ObsoletePresets {
        std::vector<std::string> prints;
        std::vector<std::string> sla_prints;
        std::vector<std::string> filaments;
        std::vector<std::string> sla_materials;
        std::vector<std::string> printers;
    };
    ObsoletePresets             obsolete_presets;

    bool                        has_defauls_only() const 
        { return prints.has_defaults_only() && filaments.has_defaults_only() && printers.has_defaults_only(); }

    DynamicPrintConfig          full_config() const;
    // full_config() with the "printhost_apikey" and "printhost_cafile" removed.
    DynamicPrintConfig          full_config_secure() const;

    // Load user configuration and store it into the user profiles.
    // This method is called by the configuration wizard.
    void                        load_config_from_wizard(const std::string &name, DynamicPrintConfig config)
        { this->load_config_file_config(name, false, std::move(config)); }

    // Load configuration that comes from a model file containing configuration, such as 3MF et al.
    // This method is called by the Plater.
    void                        load_config_model(const std::string &name, DynamicPrintConfig config)
        { this->load_config_file_config(name, true, std::move(config)); }

    // Load an external config file containing the print, filament and printer presets.
    // Instead of a config file, a G-code may be loaded containing the full set of parameters.
    // In the future the configuration will likely be read from an AMF file as well.
    // If the file is loaded successfully, its print / filament / printer profiles will be activated.
    ConfigSubstitutions         load_config_file(const std::string &path, ForwardCompatibilitySubstitutionRule compatibility_rule);

    // Load a config bundle file, into presets and store the loaded presets into separate files
    // of the local configuration directory.
    // Load settings into the provided settings instance.
    // Activate the presets stored in the config bundle.
    // Returns the number of presets loaded successfully.
    enum LoadConfigBundleAttribute { 
        // Save the profiles, which have been loaded.
        SaveImported,
        // Delete all old config profiles before loading.
        ResetUserProfile,
        // Load a system config bundle.
        LoadSystem,
        LoadVendorOnly,
    };
    using LoadConfigBundleAttributes = enum_bitmask<LoadConfigBundleAttribute>;
    // Load the config bundle based on the flags.
    // Don't do any config substitutions when loading a system profile, perform and report substitutions otherwise.
    std::pair<PresetsConfigSubstitutions, size_t> load_configbundle(const std::string &path, LoadConfigBundleAttributes flags);

    // Export a config bundle file containing all the presets and the names of the active presets.
    void                        export_configbundle(const std::string &path, bool export_system_settings = false, bool export_physical_printers = false);

    // Enable / disable the "- default -" preset.
    void                        set_default_suppressed(bool default_suppressed);

    // Set the filament preset name. As the name could come from the UI selection box, 
    // an optional "(modified)" suffix will be removed from the filament name.
    void                        set_filament_preset(size_t idx, const std::string &name);

    // Read out the number of extruders from an active printer preset,
    // update size and content of filament_presets.
    void                        update_multi_material_filament_presets();

    // Update the is_compatible flag of all print and filament presets depending on whether they are marked
    // as compatible with the currently selected printer (and print in case of filament presets).
    // Also updates the is_visible flag of each preset.
    // If select_other_if_incompatible is true, then the print or filament preset is switched to some compatible
    // preset if the current print or filament preset is not compatible.
    void                        update_compatible(PresetSelectCompatibleType select_other_print_if_incompatible, PresetSelectCompatibleType select_other_filament_if_incompatible);
    void                        update_compatible(PresetSelectCompatibleType select_other_if_incompatible) { this->update_compatible(select_other_if_incompatible, select_other_if_incompatible); }

    // Set the is_visible flag for printer vendors, printer models and printer variants
    // based on the user configuration.
    // If the "vendor" section is missing, enable all models and variants of the particular vendor.
    void                        load_installed_printers(const AppConfig &config);

    const std::string&          get_preset_name_by_alias(const Preset::Type& preset_type, const std::string& alias) const;

    // Save current preset of a required type under a new name. If the name is different from the old one,
    // Unselected option would be reverted to the beginning values
    void                        save_changes_for_preset(const std::string& new_name, Preset::Type type, const std::vector<std::string>& unselected_options);

    static const char *PRUSA_BUNDLE;
private:
    std::string                 load_system_presets();
    // Merge one vendor's presets with the other vendor's presets, report duplicates.
    std::vector<std::string>    merge_presets(PresetBundle &&other);
    // Update renamed_from and alias maps of system profiles.
    void 						update_system_maps();

    // Set the is_visible flag for filaments and sla materials,
    // apply defaults based on enabled printers when no filaments/materials are installed.
    void                        load_installed_filaments(AppConfig &config);
    void                        load_installed_sla_materials(AppConfig &config);

    // Load selections (current print, current filaments, current printer) from config.ini
    // This is done just once on application start up.
    void                        load_selections(AppConfig &config, const std::string &preferred_model_id = "");

    // Load print, filament & printer presets from a config. If it is an external config, then the name is extracted from the external path.
    // and the external config is just referenced, not stored into user profile directory.
    // If it is not an external config, then the config will be stored into the user profile directory.
    void                        load_config_file_config(const std::string &name_or_path, bool is_external, DynamicPrintConfig &&config);
    ConfigSubstitutions         load_config_file_config_bundle(const std::string &path, const boost::property_tree::ptree &tree);

    DynamicPrintConfig          full_fff_config() const;
    DynamicPrintConfig          full_sla_config() const;
};

ENABLE_ENUM_BITMASK_OPERATORS(PresetBundle::LoadConfigBundleAttribute)

} // namespace Slic3r

#endif /* slic3r_PresetBundle_hpp_ */
