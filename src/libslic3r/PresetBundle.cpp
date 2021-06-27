#include <cassert>

#include "PresetBundle.hpp"
#include "libslic3r.h"
#include "Utils.hpp"
#include "Model.hpp"
#include "format.hpp"

#include <algorithm>
#include <set>
#include <fstream>
#include <unordered_set>
#include <boost/filesystem.hpp>
#include <boost/algorithm/clamp.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <boost/nowide/cenv.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/locale.hpp>
#include <boost/log/trivial.hpp>


// Store the print/filament/printer presets into a "presets" subdirectory of the Slic3rPE config dir.
// This breaks compatibility with the upstream Slic3r if the --datadir is used to switch between the two versions.
// #define SLIC3R_PROFILE_USE_PRESETS_SUBDIR

namespace Slic3r {

static std::vector<std::string> s_project_options {
    "colorprint_heights",
    "wiping_volumes_extruders",
    "wiping_volumes_matrix"
};

const char *PresetBundle::PRUSA_BUNDLE = "PrusaResearch";

PresetBundle::PresetBundle() :
    prints(Preset::TYPE_PRINT, Preset::print_options(), static_cast<const PrintRegionConfig&>(FullPrintConfig::defaults())),
    filaments(Preset::TYPE_FILAMENT, Preset::filament_options(), static_cast<const PrintRegionConfig&>(FullPrintConfig::defaults())),
    sla_materials(Preset::TYPE_SLA_MATERIAL, Preset::sla_material_options(), static_cast<const SLAMaterialConfig&>(SLAFullPrintConfig::defaults())), 
    sla_prints(Preset::TYPE_SLA_PRINT, Preset::sla_print_options(), static_cast<const SLAPrintObjectConfig&>(SLAFullPrintConfig::defaults())),
    printers(Preset::TYPE_PRINTER, Preset::printer_options(), static_cast<const PrintRegionConfig&>(FullPrintConfig::defaults()), "- default FFF -"),
    physical_printers(PhysicalPrinter::printer_options())
{
    // The following keys are handled by the UI, they do not have a counterpart in any StaticPrintConfig derived classes,
    // therefore they need to be handled differently. As they have no counterpart in StaticPrintConfig, they are not being
    // initialized based on PrintConfigDef(), but to empty values (zeros, empty vectors, empty strings).
    //
    // "compatible_printers", "compatible_printers_condition", "inherits",
    // "print_settings_id", "filament_settings_id", "printer_settings_id", "printer_settings_id"
    // "printer_vendor", "printer_model", "printer_variant", "default_print_profile", "default_filament_profile"

    // Create the ID config keys, as they are not part of the Static print config classes.
    this->prints.default_preset().config.optptr("print_settings_id", true);
    this->prints.default_preset().compatible_printers_condition();
    this->prints.default_preset().inherits();

    this->filaments.default_preset().config.option<ConfigOptionStrings>("filament_settings_id", true)->values = { "" };
    this->filaments.default_preset().compatible_printers_condition();
    this->filaments.default_preset().inherits();
	// Set all the nullable values to nils.
	this->filaments.default_preset().config.null_nullables();

    this->sla_materials.default_preset().config.optptr("sla_material_settings_id", true);
    this->sla_materials.default_preset().compatible_printers_condition();
    this->sla_materials.default_preset().inherits();

    this->sla_prints.default_preset().config.optptr("sla_print_settings_id", true);
    this->sla_prints.default_preset().config.opt_string("output_filename_format", true) = "[input_filename_base].sl1";
    this->sla_prints.default_preset().compatible_printers_condition();
    this->sla_prints.default_preset().inherits();

    this->printers.add_default_preset(Preset::sla_printer_options(), static_cast<const SLAMaterialConfig&>(SLAFullPrintConfig::defaults()), "- default SLA -");
    this->printers.preset(1).printer_technology_ref() = ptSLA;
    for (size_t i = 0; i < 2; ++ i) {
		// The following ugly switch is to avoid printers.preset(0) to return the edited instance, as the 0th default is the current one.
		Preset &preset = this->printers.default_preset(i);
        for (const char *key : { 
            "printer_settings_id", "printer_vendor", "printer_model", "printer_variant", "thumbnails",
            //FIXME the following keys are only created here for compatibility to be able to parse legacy Printer profiles.
            // These keys are converted to Physical Printer profile. After the conversion, they shall be removed.
            "host_type", "print_host", "printhost_apikey", "printhost_cafile"})
            preset.config.optptr(key, true);
        if (i == 0) {
            preset.config.optptr("default_print_profile", true);
            preset.config.option<ConfigOptionStrings>("default_filament_profile", true);
        } else {
            preset.config.optptr("default_sla_print_profile", true);
            preset.config.optptr("default_sla_material_profile", true);
        }
        // default_sla_material_profile
        preset.inherits();
    }

    // Re-activate the default presets, so their "edited" preset copies will be updated with the additional configuration values above.
    this->prints       .select_preset(0);
    this->sla_prints   .select_preset(0);
    this->filaments    .select_preset(0);
    this->sla_materials.select_preset(0);
    this->printers     .select_preset(0);

    this->project_config.apply_only(FullPrintConfig::defaults(), s_project_options);
}

PresetBundle::PresetBundle(const PresetBundle &rhs)
{
    *this = rhs;
}

PresetBundle& PresetBundle::operator=(const PresetBundle &rhs)
{
    prints              = rhs.prints;
    sla_prints          = rhs.sla_prints;
    filaments           = rhs.filaments;
    sla_materials       = rhs.sla_materials;
    printers            = rhs.printers;
    physical_printers   = rhs.physical_printers;

    filament_presets    = rhs.filament_presets;
    project_config      = rhs.project_config;
    vendors             = rhs.vendors;
    obsolete_presets    = rhs.obsolete_presets;

    // Adjust Preset::vendor pointers to point to the copied vendors map.
    prints       .update_vendor_ptrs_after_copy(this->vendors);
    sla_prints   .update_vendor_ptrs_after_copy(this->vendors);
    filaments    .update_vendor_ptrs_after_copy(this->vendors);
    sla_materials.update_vendor_ptrs_after_copy(this->vendors);
    printers     .update_vendor_ptrs_after_copy(this->vendors);

    return *this;
}

void PresetBundle::reset(bool delete_files)
{
    // Clear the existing presets, delete their respective files.
    this->vendors.clear();
    this->prints       .reset(delete_files);
    this->sla_prints   .reset(delete_files);
    this->filaments    .reset(delete_files);
    this->sla_materials.reset(delete_files);
    this->printers     .reset(delete_files);
    this->filament_presets.clear();
    this->filament_presets.emplace_back(this->filaments.get_selected_preset_name());
    this->obsolete_presets.prints.clear();
    this->obsolete_presets.sla_prints.clear();
    this->obsolete_presets.filaments.clear();
    this->obsolete_presets.sla_materials.clear();
    this->obsolete_presets.printers.clear();
}

void PresetBundle::setup_directories()
{
    boost::filesystem::path data_dir = boost::filesystem::path(Slic3r::data_dir());
    std::initializer_list<boost::filesystem::path> paths = { 
        data_dir,
		data_dir / "vendor",
        data_dir / "cache",
#ifdef SLIC3R_PROFILE_USE_PRESETS_SUBDIR
        // Store the print/filament/printer presets into a "presets" directory.
        data_dir / "presets", 
        data_dir / "presets" / "print", 
        data_dir / "presets" / "filament", 
        data_dir / "presets" / "sla_print",  
        data_dir / "presets" / "sla_material", 
        data_dir / "presets" / "printer", 
        data_dir / "presets" / "physical_printer" 
#else
        // Store the print/filament/printer presets at the same location as the upstream Slic3r.
        data_dir / "print", 
        data_dir / "filament", 
        data_dir / "sla_print", 
        data_dir / "sla_material", 
        data_dir / "printer", 
        data_dir / "physical_printer" 
#endif
    };
    for (const boost::filesystem::path &path : paths) {
		boost::filesystem::path subdir = path;
        subdir.make_preferred();
        if (! boost::filesystem::is_directory(subdir) && 
            ! boost::filesystem::create_directory(subdir))
            throw Slic3r::RuntimeError(std::string("Slic3r was unable to create its data directory at ") + subdir.string());
    }
}

PresetsConfigSubstitutions PresetBundle::load_presets(AppConfig &config, ForwardCompatibilitySubstitutionRule substitution_rule, const std::string &preferred_model_id)
{
    // First load the vendor specific system presets.
    std::string errors_cummulative = this->load_system_presets();

    const std::string dir_user_presets = data_dir()
#ifdef SLIC3R_PROFILE_USE_PRESETS_SUBDIR
        // Store the print/filament/printer presets into a "presets" directory.
        + "/presets"
#else
        // Store the print/filament/printer presets at the same location as the upstream Slic3r.
#endif
        ;

    PresetsConfigSubstitutions substitutions;
    try {
        this->prints.load_presets(dir_user_presets, "print", substitutions, substitution_rule);
    } catch (const std::runtime_error &err) {
        errors_cummulative += err.what();
    }
    try {
        this->sla_prints.load_presets(dir_user_presets, "sla_print", substitutions, substitution_rule);
    } catch (const std::runtime_error &err) {
        errors_cummulative += err.what();
    }
    try {
        this->filaments.load_presets(dir_user_presets, "filament", substitutions, substitution_rule);
    } catch (const std::runtime_error &err) {
        errors_cummulative += err.what();
    }
    try {
        this->sla_materials.load_presets(dir_user_presets, "sla_material", substitutions, substitution_rule);
    } catch (const std::runtime_error &err) {
        errors_cummulative += err.what();
    }
    try {
        this->printers.load_presets(dir_user_presets, "printer", substitutions, substitution_rule);
    } catch (const std::runtime_error &err) {
        errors_cummulative += err.what();
    }
    try {
        this->physical_printers.load_printers(dir_user_presets, "physical_printer", substitutions, substitution_rule);
    } catch (const std::runtime_error &err) {
        errors_cummulative += err.what();
    }
    this->update_multi_material_filament_presets();
    this->update_compatible(PresetSelectCompatibleType::Never);
    if (! errors_cummulative.empty())
        throw Slic3r::RuntimeError(errors_cummulative);

    this->load_selections(config, preferred_model_id);

    return substitutions;
}

// Load system presets into this PresetBundle.
// For each vendor, there will be a single PresetBundle loaded.
std::string PresetBundle::load_system_presets()
{
    // Here the vendor specific read only Config Bundles are stored.
    boost::filesystem::path dir = (boost::filesystem::path(data_dir()) / "vendor").make_preferred();
    std::string errors_cummulative;
    bool        first = true;
    for (auto &dir_entry : boost::filesystem::directory_iterator(dir))
        if (Slic3r::is_ini_file(dir_entry)) {
            std::string name = dir_entry.path().filename().string();
            // Remove the .ini suffix.
            name.erase(name.size() - 4);
            try {
                // Load the config bundle, flatten it.
                if (first) {
                    // Reset this PresetBundle and load the first vendor config.
                    this->load_configbundle(dir_entry.path().string(), PresetBundle::LoadSystem);
                    first = false;
                } else {
                    // Load the other vendor configs, merge them with this PresetBundle.
                    // Report duplicate profiles.
                    PresetBundle other;
                    other.load_configbundle(dir_entry.path().string(), PresetBundle::LoadSystem);
                    std::vector<std::string> duplicates = this->merge_presets(std::move(other));
                    if (! duplicates.empty()) {
                        errors_cummulative += "Vendor configuration file " + name + " contains the following presets with names used by other vendors: ";
                        for (size_t i = 0; i < duplicates.size(); ++ i) {
                            if (i > 0)
                                errors_cummulative += ", ";
                            errors_cummulative += duplicates[i];
                        }
                    }
                }
            } catch (const std::runtime_error &err) {
                errors_cummulative += err.what();
                errors_cummulative += "\n";
            }
        }
    if (first) {
		// No config bundle loaded, reset.
		this->reset(false);
	}

	this->update_system_maps();
    return errors_cummulative;
}

// Merge one vendor's presets with the other vendor's presets, report duplicates.
std::vector<std::string> PresetBundle::merge_presets(PresetBundle &&other)
{
    this->vendors.insert(other.vendors.begin(), other.vendors.end());
    std::vector<std::string> duplicate_prints        = this->prints       .merge_presets(std::move(other.prints),        this->vendors);
    std::vector<std::string> duplicate_sla_prints    = this->sla_prints   .merge_presets(std::move(other.sla_prints),    this->vendors);
    std::vector<std::string> duplicate_filaments     = this->filaments    .merge_presets(std::move(other.filaments),     this->vendors);
    std::vector<std::string> duplicate_sla_materials = this->sla_materials.merge_presets(std::move(other.sla_materials), this->vendors);
    std::vector<std::string> duplicate_printers      = this->printers     .merge_presets(std::move(other.printers),      this->vendors);
	append(this->obsolete_presets.prints,        std::move(other.obsolete_presets.prints));
	append(this->obsolete_presets.sla_prints,    std::move(other.obsolete_presets.sla_prints));
	append(this->obsolete_presets.filaments,     std::move(other.obsolete_presets.filaments));
    append(this->obsolete_presets.sla_materials, std::move(other.obsolete_presets.sla_materials));
	append(this->obsolete_presets.printers,      std::move(other.obsolete_presets.printers));
	append(duplicate_prints, std::move(duplicate_sla_prints));
	append(duplicate_prints, std::move(duplicate_filaments));
    append(duplicate_prints, std::move(duplicate_sla_materials));
    append(duplicate_prints, std::move(duplicate_printers));
    return duplicate_prints;
}

void PresetBundle::update_system_maps()
{
    this->prints 	   .update_map_system_profile_renamed();
    this->sla_prints   .update_map_system_profile_renamed();
    this->filaments    .update_map_system_profile_renamed();
    this->sla_materials.update_map_system_profile_renamed();
    this->printers     .update_map_system_profile_renamed();

    this->prints       .update_map_alias_to_profile_name();
    this->sla_prints   .update_map_alias_to_profile_name();
    this->filaments    .update_map_alias_to_profile_name();
    this->sla_materials.update_map_alias_to_profile_name();
}

static inline std::string remove_ini_suffix(const std::string &name)
{
    std::string out = name;
    if (boost::iends_with(out, ".ini"))
        out.erase(out.end() - 4, out.end());
    return out;
}

// Set the "enabled" flag for printer vendors, printer models and printer variants
// based on the user configuration.
// If the "vendor" section is missing, enable all models and variants of the particular vendor.
void PresetBundle::load_installed_printers(const AppConfig &config)
{
	this->update_system_maps();
    for (auto &preset : printers)
        preset.set_visible_from_appconfig(config);
}

const std::string& PresetBundle::get_preset_name_by_alias( const Preset::Type& preset_type, const std::string& alias) const
{
    // there are not aliases for Printers profiles
    if (preset_type == Preset::TYPE_PRINTER || preset_type == Preset::TYPE_INVALID)
        return alias;

    const PresetCollection& presets = preset_type == Preset::TYPE_PRINT     ? prints :
                                      preset_type == Preset::TYPE_SLA_PRINT ? sla_prints :
                                      preset_type == Preset::TYPE_FILAMENT  ? filaments :
                                      sla_materials;

    return presets.get_preset_name_by_alias(alias);
}

void PresetBundle::save_changes_for_preset(const std::string& new_name, Preset::Type type,
                                           const std::vector<std::string>& unselected_options)
{
    PresetCollection& presets = type == Preset::TYPE_PRINT          ? prints :
                                type == Preset::TYPE_SLA_PRINT      ? sla_prints :
                                type == Preset::TYPE_FILAMENT       ? filaments :
                                type == Preset::TYPE_SLA_MATERIAL   ? sla_materials : printers;

    // if we want to save just some from selected options
    if (!unselected_options.empty()) {
        // revert unselected options to the old values
        presets.get_edited_preset().config.apply_only(presets.get_selected_preset().config, unselected_options);
    }

    // Save the preset into Slic3r::data_dir / presets / section_name / preset_name.ini
    presets.save_current_preset(new_name);
    // Mark the print & filament enabled if they are compatible with the currently selected preset.
    // If saving the preset changes compatibility with other presets, keep the now incompatible dependent presets selected, however with a "red flag" icon showing that they are no more compatible.
    update_compatible(PresetSelectCompatibleType::Never);

    if (type == Preset::TYPE_FILAMENT) {
        // synchronize the first filament presets.
        set_filament_preset(0, filaments.get_selected_preset_name());
    }
}

void PresetBundle::load_installed_filaments(AppConfig &config)
{
    if (! config.has_section(AppConfig::SECTION_FILAMENTS)) {
		// Compatibility with the PrusaSlicer 2.1.1 and older, where the filament profiles were not installable yet.
		// Find all filament profiles, which are compatible with installed printers, and act as if these filament profiles
		// were installed.
        std::unordered_set<const Preset*> compatible_filaments;
        for (const Preset &printer : printers)
            if (printer.is_visible && printer.printer_technology() == ptFFF) {
				const PresetWithVendorProfile printer_with_vendor_profile = printers.get_preset_with_vendor_profile(printer);
				for (const Preset &filament : filaments)
					if (filament.is_system && is_compatible_with_printer(filaments.get_preset_with_vendor_profile(filament), printer_with_vendor_profile))
						compatible_filaments.insert(&filament);
			}
		// and mark these filaments as installed, therefore this code will not be executed at the next start of the application.
        for (const auto &filament: compatible_filaments)
            config.set(AppConfig::SECTION_FILAMENTS, filament->name, "1");
    }

    for (auto &preset : filaments)
        preset.set_visible_from_appconfig(config);
}

void PresetBundle::load_installed_sla_materials(AppConfig &config)
{
    if (! config.has_section(AppConfig::SECTION_MATERIALS)) {
        std::unordered_set<const Preset*> comp_sla_materials;
		// Compatibility with the PrusaSlicer 2.1.1 and older, where the SLA material profiles were not installable yet.
		// Find all SLA material profiles, which are compatible with installed printers, and act as if these SLA material profiles
		// were installed.
        for (const Preset &printer : printers)
            if (printer.is_visible && printer.printer_technology() == ptSLA) {
				const PresetWithVendorProfile printer_with_vendor_profile = printers.get_preset_with_vendor_profile(printer);
				for (const Preset &material : sla_materials)
					if (material.is_system && is_compatible_with_printer(sla_materials.get_preset_with_vendor_profile(material), printer_with_vendor_profile))
						comp_sla_materials.insert(&material);
			}
		// and mark these SLA materials as installed, therefore this code will not be executed at the next start of the application.
		for (const auto &material: comp_sla_materials)
            config.set(AppConfig::SECTION_MATERIALS, material->name, "1");
    }

    for (auto &preset : sla_materials)
        preset.set_visible_from_appconfig(config);
}

// Load selections (current print, current filaments, current printer) from config.ini
// This is done on application start up or after updates are applied.
void PresetBundle::load_selections(AppConfig &config, const std::string &preferred_model_id)
{
	// Update visibility of presets based on application vendor / model / variant configuration.
	this->load_installed_printers(config);

    // Update visibility of filament and sla material presets
    this->load_installed_filaments(config);
    this->load_installed_sla_materials(config);

    // Parse the initial print / filament / printer profile names.
    std::string initial_print_profile_name        = remove_ini_suffix(config.get("presets", "print"));
    std::string initial_sla_print_profile_name    = remove_ini_suffix(config.get("presets", "sla_print"));
    std::string initial_filament_profile_name     = remove_ini_suffix(config.get("presets", "filament"));
    std::string initial_sla_material_profile_name = remove_ini_suffix(config.get("presets", "sla_material"));
	std::string initial_printer_profile_name      = remove_ini_suffix(config.get("presets", "printer"));

    // Activate print / filament / printer profiles from either the config,
    // or from the preferred_model_id suggestion passed in by ConfigWizard.
    // If the printer profile enumerated by the config are not visible, select an alternate preset.
    // Do not select alternate profiles for the print / filament profiles as those presets
    // will be selected by the following call of this->update_compatible(PresetSelectCompatibleType::Always).

    const Preset *initial_printer = printers.find_preset(initial_printer_profile_name);
    const Preset *preferred_printer = printers.find_by_model_id(preferred_model_id);
    printers.select_preset_by_name(
        (preferred_printer != nullptr && (initial_printer == nullptr || !initial_printer->is_visible)) ? 
            preferred_printer->name : 
            initial_printer_profile_name,
        true);

    // Selects the profile, leaves it to -1 if the initial profile name is empty or if it was not found.
    prints.select_preset_by_name_strict(initial_print_profile_name);
    filaments.select_preset_by_name_strict(initial_filament_profile_name);
	sla_prints.select_preset_by_name_strict(initial_sla_print_profile_name);
    sla_materials.select_preset_by_name_strict(initial_sla_material_profile_name);

    // Load the names of the other filament profiles selected for a multi-material printer.
    // Load it even if the current printer technology is SLA.
    // The possibly excessive filament names will be later removed with this->update_multi_material_filament_presets()
    // once the FFF technology gets selected.
    this->filament_presets = { filaments.get_selected_preset_name() };
    for (unsigned int i = 1; i < 1000; ++ i) {
        char name[64];
        sprintf(name, "filament_%u", i);
        if (! config.has("presets", name))
            break;
        this->filament_presets.emplace_back(remove_ini_suffix(config.get("presets", name)));
    }

    // Update visibility of presets based on their compatibility with the active printer.
    // Always try to select a compatible print and filament preset to the current printer preset,
    // as the application may have been closed with an active "external" preset, which does not
    // exist.
    this->update_compatible(PresetSelectCompatibleType::Always);
    this->update_multi_material_filament_presets();

    // Parse the initial physical printer name.
    std::string initial_physical_printer_name = remove_ini_suffix(config.get("presets", "physical_printer"));

    // Activate physical printer from the config
    if (!initial_physical_printer_name.empty())
        physical_printers.select_printer(initial_physical_printer_name);
}

// Export selections (current print, current filaments, current printer) into config.ini
void PresetBundle::export_selections(AppConfig &config)
{
	assert(this->printers.get_edited_preset().printer_technology() != ptFFF || filament_presets.size() >= 1);
	assert(this->printers.get_edited_preset().printer_technology() != ptFFF || filament_presets.size() > 1 || filaments.get_selected_preset_name() == filament_presets.front());
    config.clear_section("presets");
    config.set("presets", "print",        prints.get_selected_preset_name());
    config.set("presets", "filament",     filament_presets.front());
    for (unsigned i = 1; i < filament_presets.size(); ++i) {
        char name[64];
        sprintf(name, "filament_%u", i);
        config.set("presets", name, filament_presets[i]);
    }

    config.set("presets", "sla_print",    sla_prints.get_selected_preset_name());
    config.set("presets", "sla_material", sla_materials.get_selected_preset_name());
    config.set("presets", "printer",      printers.get_selected_preset_name());
    config.set("presets", "physical_printer", physical_printers.get_selected_full_printer_name());
}

DynamicPrintConfig PresetBundle::full_config() const
{
    return (this->printers.get_edited_preset().printer_technology() == ptFFF) ?
        this->full_fff_config() :
        this->full_sla_config();
}

DynamicPrintConfig PresetBundle::full_config_secure() const
{
    DynamicPrintConfig config = this->full_config();
    //FIXME legacy, the keys should not be there after conversion to a Physical Printer profile.
    config.erase("print_host");
    config.erase("printhost_apikey");
    config.erase("printhost_cafile");
    return config;
}

DynamicPrintConfig PresetBundle::full_fff_config() const
{    
    DynamicPrintConfig out;
    out.apply(FullPrintConfig::defaults());
    out.apply(this->prints.get_edited_preset().config);
    // Add the default filament preset to have the "filament_preset_id" defined.
	out.apply(this->filaments.default_preset().config);
	out.apply(this->printers.get_edited_preset().config);
    out.apply(this->project_config);

    auto   *nozzle_diameter = dynamic_cast<const ConfigOptionFloats*>(out.option("nozzle_diameter"));
    size_t  num_extruders   = nozzle_diameter->values.size();
    // Collect the "compatible_printers_condition" and "inherits" values over all presets (print, filaments, printers) into a single vector.
    std::vector<std::string> compatible_printers_condition;
    std::vector<std::string> compatible_prints_condition;
    std::vector<std::string> inherits;
    compatible_printers_condition.emplace_back(this->prints.get_edited_preset().compatible_printers_condition());
    inherits                     .emplace_back(this->prints.get_edited_preset().inherits());

    if (num_extruders <= 1) {
        out.apply(this->filaments.get_edited_preset().config);
        compatible_printers_condition.emplace_back(this->filaments.get_edited_preset().compatible_printers_condition());
        compatible_prints_condition  .emplace_back(this->filaments.get_edited_preset().compatible_prints_condition());
        inherits                     .emplace_back(this->filaments.get_edited_preset().inherits());
    } else {
        // Retrieve filament presets and build a single config object for them.
        // First collect the filament configurations based on the user selection of this->filament_presets.
        // Here this->filaments.find_preset() and this->filaments.first_visible() return the edited copy of the preset if active.
        std::vector<const DynamicPrintConfig*> filament_configs;
        for (const std::string &filament_preset_name : this->filament_presets)
            filament_configs.emplace_back(&this->filaments.find_preset(filament_preset_name, true)->config);
		while (filament_configs.size() < num_extruders)
            filament_configs.emplace_back(&this->filaments.first_visible().config);
        for (const DynamicPrintConfig *cfg : filament_configs) {
            // The compatible_prints/printers_condition() returns a reference to configuration key, which may not yet exist.
            DynamicPrintConfig &cfg_rw = *const_cast<DynamicPrintConfig*>(cfg);
            compatible_printers_condition.emplace_back(Preset::compatible_printers_condition(cfg_rw));
            compatible_prints_condition  .emplace_back(Preset::compatible_prints_condition(cfg_rw));
            inherits                     .emplace_back(Preset::inherits(cfg_rw));
        }
        // Option values to set a ConfigOptionVector from.
        std::vector<const ConfigOption*> filament_opts(num_extruders, nullptr);
        // loop through options and apply them to the resulting config.
        for (const t_config_option_key &key : this->filaments.default_preset().config.keys()) {
			if (key == "compatible_prints" || key == "compatible_printers")
				continue;
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

	// Don't store the "compatible_printers_condition" for the printer profile, there is none.
    inherits.emplace_back(this->printers.get_edited_preset().inherits());

    // These value types clash between the print and filament profiles. They should be renamed.
    out.erase("compatible_prints");
    out.erase("compatible_prints_condition");
    out.erase("compatible_printers");
    out.erase("compatible_printers_condition");
    out.erase("inherits");
    
    static const char *keys[] = { "perimeter", "infill", "solid_infill", "support_material", "support_material_interface" };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++ i) {
        std::string key = std::string(keys[i]) + "_extruder";
        auto *opt = dynamic_cast<ConfigOptionInt*>(out.option(key, false));
        assert(opt != nullptr);
        opt->value = boost::algorithm::clamp<int>(opt->value, 0, int(num_extruders));
    }

    out.option<ConfigOptionString >("print_settings_id",    true)->value  = this->prints.get_selected_preset_name();
    out.option<ConfigOptionStrings>("filament_settings_id", true)->values = this->filament_presets;
    out.option<ConfigOptionString >("printer_settings_id",  true)->value  = this->printers.get_selected_preset_name();
    out.option<ConfigOptionString >("physical_printer_settings_id", true)->value = this->physical_printers.get_selected_printer_name();

    // Serialize the collected "compatible_printers_condition" and "inherits" fields.
    // There will be 1 + num_exturders fields for "inherits" and 2 + num_extruders for "compatible_printers_condition" stored.
    // The vector will not be stored if all fields are empty strings.
    auto add_if_some_non_empty = [&out](std::vector<std::string> &&values, const std::string &key) {
        bool nonempty = false;
        for (const std::string &v : values)
            if (! v.empty()) {
                nonempty = true;
                break;
            }
        if (nonempty)
            out.set_key_value(key, new ConfigOptionStrings(std::move(values)));
    };
    add_if_some_non_empty(std::move(compatible_printers_condition), "compatible_printers_condition_cummulative");
    add_if_some_non_empty(std::move(compatible_prints_condition),   "compatible_prints_condition_cummulative");
    add_if_some_non_empty(std::move(inherits),                      "inherits_cummulative");

	out.option<ConfigOptionEnumGeneric>("printer_technology", true)->value = ptFFF;
    return out;
}

DynamicPrintConfig PresetBundle::full_sla_config() const
{    
    DynamicPrintConfig out;
    out.apply(SLAFullPrintConfig::defaults());
    out.apply(this->sla_prints.get_edited_preset().config);
    out.apply(this->sla_materials.get_edited_preset().config);
    out.apply(this->printers.get_edited_preset().config);
    // There are no project configuration values as of now, the project_config is reserved for FFF printers.
//    out.apply(this->project_config);

    // Collect the "compatible_printers_condition" and "inherits" values over all presets (sla_prints, sla_materials, printers) into a single vector.
    std::vector<std::string> compatible_printers_condition;
	std::vector<std::string> compatible_prints_condition;
    std::vector<std::string> inherits;
    compatible_printers_condition.emplace_back(this->sla_prints.get_edited_preset().compatible_printers_condition());
	inherits					 .emplace_back(this->sla_prints.get_edited_preset().inherits());
    compatible_printers_condition.emplace_back(this->sla_materials.get_edited_preset().compatible_printers_condition());
	compatible_prints_condition  .emplace_back(this->sla_materials.get_edited_preset().compatible_prints_condition());
    inherits                     .emplace_back(this->sla_materials.get_edited_preset().inherits());
    inherits                     .emplace_back(this->printers.get_edited_preset().inherits());

    // These two value types clash between the print and filament profiles. They should be renamed.
    out.erase("compatible_printers");
    out.erase("compatible_printers_condition");
    out.erase("inherits");
    
    out.option<ConfigOptionString >("sla_print_settings_id",    true)->value  = this->sla_prints.get_selected_preset_name();
    out.option<ConfigOptionString >("sla_material_settings_id", true)->value  = this->sla_materials.get_selected_preset_name();
    out.option<ConfigOptionString >("printer_settings_id",      true)->value  = this->printers.get_selected_preset_name();
    out.option<ConfigOptionString >("physical_printer_settings_id", true)->value = this->physical_printers.get_selected_printer_name();

    // Serialize the collected "compatible_printers_condition" and "inherits" fields.
    // There will be 1 + num_exturders fields for "inherits" and 2 + num_extruders for "compatible_printers_condition" stored.
    // The vector will not be stored if all fields are empty strings.
    auto add_if_some_non_empty = [&out](std::vector<std::string> &&values, const std::string &key) {
        bool nonempty = false;
        for (const std::string &v : values)
            if (! v.empty()) {
                nonempty = true;
                break;
            }
        if (nonempty)
            out.set_key_value(key, new ConfigOptionStrings(std::move(values)));
    };
    add_if_some_non_empty(std::move(compatible_printers_condition), "compatible_printers_condition_cummulative");
    add_if_some_non_empty(std::move(compatible_prints_condition),   "compatible_prints_condition_cummulative");
    add_if_some_non_empty(std::move(inherits),                      "inherits_cummulative");

	out.option<ConfigOptionEnumGeneric>("printer_technology", true)->value = ptSLA;
	return out;
}

// Load an external config file containing the print, filament and printer presets.
// Instead of a config file, a G-code may be loaded containing the full set of parameters.
// In the future the configuration will likely be read from an AMF file as well.
// If the file is loaded successfully, its print / filament / printer profiles will be activated.
ConfigSubstitutions PresetBundle::load_config_file(const std::string &path, ForwardCompatibilitySubstitutionRule compatibility_rule)
{
	if (is_gcode_file(path)) {
		DynamicPrintConfig config;
		config.apply(FullPrintConfig::defaults());
        ConfigSubstitutions config_substitutions = config.load_from_gcode_file(path, true /* check_header */, compatibility_rule);
        Preset::normalize(config);
		load_config_file_config(path, true, std::move(config));
		return config_substitutions;
	}

    // 1) Try to load the config file into a boost property tree.
    boost::property_tree::ptree tree;
    try {
        boost::nowide::ifstream ifs(path);
        boost::property_tree::read_ini(ifs, tree);
    } catch (const std::ifstream::failure &err) {
        throw Slic3r::RuntimeError(std::string("The Config Bundle cannot be loaded: ") + path + "\n\tReason: " + err.what());
    } catch (const boost::property_tree::file_parser_error &err) {
        throw Slic3r::RuntimeError((boost::format("Failed loading the Config Bundle \"%1%\": %2% at line %3%")
        	% err.filename() % err.message() % err.line()).str());
    } catch (const std::runtime_error &err) {
        throw Slic3r::RuntimeError(std::string("Failed loading the preset file: ") + path + "\n\tReason: " + err.what());
    }

    // 2) Continue based on the type of the configuration file.
    ConfigFileType config_file_type = guess_config_file_type(tree);
    ConfigSubstitutions config_substitutions;
    switch (config_file_type) {
    case CONFIG_FILE_TYPE_UNKNOWN:
        throw Slic3r::RuntimeError(std::string("Unknown configuration file type: ") + path);   
    case CONFIG_FILE_TYPE_APP_CONFIG:
        throw Slic3r::RuntimeError(std::string("Invalid configuration file: ") + path + ". This is an application config file.");
	case CONFIG_FILE_TYPE_CONFIG:
	{
		// Initialize a config from full defaults.
		DynamicPrintConfig config;
		config.apply(FullPrintConfig::defaults());
        config_substitutions = config.load(tree, compatibility_rule);
		Preset::normalize(config);
		load_config_file_config(path, true, std::move(config));
        return config_substitutions;
    }
    case CONFIG_FILE_TYPE_CONFIG_BUNDLE:
        return load_config_file_config_bundle(path, tree);
    }

    // This shall never happen. Suppres compiler warnings.
    assert(false);
    return ConfigSubstitutions{};
}

// Load a config file from a boost property_tree. This is a private method called from load_config_file.
// is_external == false on if called from ConfigWizard
void PresetBundle::load_config_file_config(const std::string &name_or_path, bool is_external, DynamicPrintConfig &&config)
{
    PrinterTechnology printer_technology = Preset::printer_technology(config);

    // The "compatible_printers" field should not have been exported into a config.ini or a G-code anyway, 
    // but some of the alpha versions of Slic3r did.
    {
        ConfigOption *opt_compatible = config.optptr("compatible_printers");
        if (opt_compatible != nullptr) {
            assert(opt_compatible->type() == coStrings);
            if (opt_compatible->type() == coStrings)
                static_cast<ConfigOptionStrings*>(opt_compatible)->values.clear();
        }
    }

    size_t num_extruders = (printer_technology == ptFFF) ?
        std::min(config.option<ConfigOptionFloats>("nozzle_diameter"  )->values.size(), 
                 config.option<ConfigOptionFloats>("filament_diameter")->values.size()) :
		// 1 SLA material
        1;
    // Make a copy of the "compatible_printers_condition_cummulative" and "inherits_cummulative" vectors, which 
    // accumulate values over all presets (print, filaments, printers).
    // These values will be distributed into their particular presets when loading.
    std::vector<std::string> compatible_printers_condition_values   = std::move(config.option<ConfigOptionStrings>("compatible_printers_condition_cummulative", true)->values);
    std::vector<std::string> compatible_prints_condition_values     = std::move(config.option<ConfigOptionStrings>("compatible_prints_condition_cummulative",   true)->values);
    std::vector<std::string> inherits_values                        = std::move(config.option<ConfigOptionStrings>("inherits_cummulative", true)->values);
    std::string &compatible_printers_condition  = Preset::compatible_printers_condition(config);
    std::string &compatible_prints_condition    = Preset::compatible_prints_condition(config);
    std::string &inherits                       = Preset::inherits(config);
    compatible_printers_condition_values.resize(num_extruders + 2, std::string());
    compatible_prints_condition_values.resize(num_extruders, std::string());
    inherits_values.resize(num_extruders + 2, std::string());
    // The "default_filament_profile" will be later extracted into the printer profile.
	switch (printer_technology) {
	case ptFFF:
		config.option<ConfigOptionString>("default_print_profile", true);
        config.option<ConfigOptionStrings>("default_filament_profile", true);
		break;
	case ptSLA:
		config.option<ConfigOptionString>("default_sla_print_profile", true);
		config.option<ConfigOptionString>("default_sla_material_profile", true);
		break;
    default: break;
	}

    // 1) Create a name from the file name.
    // Keep the suffix (.ini, .gcode, .amf, .3mf etc) to differentiate it from the normal profiles.
    std::string name = is_external ? boost::filesystem::path(name_or_path).filename().string() : name_or_path;

    // 2) If the loading succeeded, split and load the config into print / filament / printer settings.
    // First load the print and printer presets.

	auto load_preset = 
		[&config, &inherits, &inherits_values, 
         &compatible_printers_condition, &compatible_printers_condition_values, 
         &compatible_prints_condition, &compatible_prints_condition_values, 
         is_external, &name, &name_or_path]
		(PresetCollection &presets, size_t idx, const std::string &key) {
		// Split the "compatible_printers_condition" and "inherits" values one by one from a single vector to the print & printer profiles.
		inherits = inherits_values[idx];
		compatible_printers_condition = compatible_printers_condition_values[idx];
        if (idx > 0 && idx - 1 < compatible_prints_condition_values.size())
            compatible_prints_condition = compatible_prints_condition_values[idx - 1];
		if (is_external)
			presets.load_external_preset(name_or_path, name, config.opt_string(key, true), config);
		else
			presets.load_preset(presets.path_from_name(name), name, config).save();
	};

    switch (Preset::printer_technology(config)) {
    case ptFFF:
    {
        load_preset(this->prints, 0, "print_settings_id");
        load_preset(this->printers, num_extruders + 1, "printer_settings_id");

        // 3) Now load the filaments. If there are multiple filament presets, split them and load them.
        auto old_filament_profile_names = config.option<ConfigOptionStrings>("filament_settings_id", true);
    	old_filament_profile_names->values.resize(num_extruders, std::string());

        if (num_extruders <= 1) {
            // Split the "compatible_printers_condition" and "inherits" from the cummulative vectors to separate filament presets.
            inherits                      = inherits_values[1];
            compatible_printers_condition = compatible_printers_condition_values[1];
			compatible_prints_condition   = compatible_prints_condition_values.front();
			Preset                *loaded = nullptr;
            if (is_external)
                loaded = this->filaments.load_external_preset(name_or_path, name, old_filament_profile_names->values.front(), config).first;
            else {
                // called from Config Wizard.
				loaded= &this->filaments.load_preset(this->filaments.path_from_name(name), name, config);
				loaded->save();
			}
            this->filament_presets.clear();
			this->filament_presets.emplace_back(loaded->name);
        } else {
            assert(is_external);
            // Split the filament presets, load each of them separately.
            std::vector<DynamicPrintConfig> configs(num_extruders, this->filaments.default_preset().config);
            // loop through options and scatter them into configs.
            for (const t_config_option_key &key : this->filaments.default_preset().config.keys()) {
                const ConfigOption *other_opt = config.option(key);
                if (other_opt == nullptr)
                    continue;
                if (other_opt->is_scalar()) {
                    for (size_t i = 0; i < configs.size(); ++ i)
                        configs[i].option(key, false)->set(other_opt);
                } else if (key != "compatible_printers" && key != "compatible_prints") {
                    for (size_t i = 0; i < configs.size(); ++ i)
                        static_cast<ConfigOptionVectorBase*>(configs[i].option(key, false))->set_at(other_opt, 0, i);
                }
            }
            // Load the configs into this->filaments and make them active.
            this->filament_presets = std::vector<std::string>(configs.size());
            // To avoid incorrect selection of the first filament preset (means a value of Preset->m_idx_selected) 
            // in a case when next added preset take a place of previosly selected preset,
            // we should add presets from last to first
            bool any_modified = false;
            for (int i = (int)configs.size()-1; i >= 0; i--) {
                DynamicPrintConfig &cfg = configs[i];
                // Split the "compatible_printers_condition" and "inherits" from the cummulative vectors to separate filament presets.
                cfg.opt_string("compatible_printers_condition", true) = compatible_printers_condition_values[i + 1];
                cfg.opt_string("compatible_prints_condition",   true) = compatible_prints_condition_values[i];
                cfg.opt_string("inherits", true)                      = inherits_values[i + 1];
                // Load all filament presets, but only select the first one in the preset dialog.
                auto [loaded, modified] = this->filaments.load_external_preset(name_or_path, name,
                    (i < int(old_filament_profile_names->values.size())) ? old_filament_profile_names->values[i] : "",
                    std::move(cfg), 
                    i == 0 ? 
                        PresetCollection::LoadAndSelect::Always : 
                    any_modified ?
                        PresetCollection::LoadAndSelect::Never :
                        PresetCollection::LoadAndSelect::OnlyIfModified);
                any_modified |= modified;
                this->filament_presets[i] = loaded->name;
            }
        }

        // 4) Load the project config values (the per extruder wipe matrix etc).
        this->project_config.apply_only(config, s_project_options);

        break;
    }
    case ptSLA:
        load_preset(this->sla_prints,    0, "sla_print_settings_id");
        load_preset(this->sla_materials, 1, "sla_material_settings_id");
        load_preset(this->printers,      2, "printer_settings_id");
        break;
    default:
        break;
    }

	this->update_compatible(PresetSelectCompatibleType::Never);

    const std::string &physical_printer = config.option<ConfigOptionString>("physical_printer_settings_id", true)->value;
    if (this->printers.get_edited_preset().is_external || physical_printer.empty()) {
        this->physical_printers.unselect_printer();
    } else {
        // Activate the physical printer profile if possible.
        PhysicalPrinter *pp = this->physical_printers.find_printer(physical_printer, true);
        if (pp != nullptr && std::find(pp->preset_names.begin(), pp->preset_names.end(), this->printers.get_edited_preset().name) != pp->preset_names.end())
            this->physical_printers.select_printer(pp->name, this->printers.get_edited_preset().name);
        else
            this->physical_printers.unselect_printer();
    }
}

// Load the active configuration of a config bundle from a boost property_tree. This is a private method called from load_config_file.
ConfigSubstitutions PresetBundle::load_config_file_config_bundle(const std::string &path, const boost::property_tree::ptree &tree)
{
    // 1) Load the config bundle into a temp data.
    PresetBundle tmp_bundle;
    // Load the config bundle, but don't save the loaded presets to user profile directory, as only the presets marked as active in the loaded preset bundle
    // will be loaded into the master PresetBundle and activated.
    auto [presets_substitutions, presets_imported] = tmp_bundle.load_configbundle(path, {});
    UNUSED(presets_imported);

    std::string bundle_name = std::string(" - ") + boost::filesystem::path(path).filename().string();

    // 2) Extract active configs from the config bundle, copy them and activate them in this bundle.
    ConfigSubstitutions config_substitutions;
    auto load_one = [&path, &bundle_name, &presets_substitutions = presets_substitutions, &config_substitutions](
            PresetCollection &collection_dst, PresetCollection &collection_src, const std::string &preset_name_src, bool activate) -> std::string {
        // If there are substitutions reported for this preset, move them to config_substitutions.
        if (auto it = std::find_if(presets_substitutions.begin(), presets_substitutions.end(), [&preset_name_src](const PresetConfigSubstitutions& subs){ return subs.preset_name == preset_name_src; });
            it != presets_substitutions.end() && ! it->substitutions.empty())
            append(config_substitutions, std::move(it->substitutions));
        Preset *preset_src = collection_src.find_preset(preset_name_src, false);
        Preset *preset_dst = collection_dst.find_preset(preset_name_src, false);
        assert(preset_src != nullptr);
        std::string preset_name_dst;
        if (preset_dst != nullptr && preset_dst->is_default) {
            // No need to copy a default preset, it always exists in collection_dst.
            if (activate)
                collection_dst.select_preset(0);
            return preset_name_src;
        } else if (preset_dst != nullptr && preset_src->config == preset_dst->config) {
            // Don't save as the config exists in the current bundle and its content is the same.
            return preset_name_src;
        } else {
            // Generate a new unique name.
            preset_name_dst = preset_name_src + bundle_name;
            Preset *preset_dup = nullptr;
            for (size_t i = 1; (preset_dup = collection_dst.find_preset(preset_name_dst, false)) != nullptr; ++ i) {
                if (preset_src->config == preset_dup->config)
                    // The preset has been already copied into collection_dst.
                    return preset_name_dst;
                // Try to generate another name.
                char buf[64];
                sprintf(buf, " (%d)", (int)i);
                preset_name_dst = preset_name_src + buf + bundle_name;
            }
        }
        assert(! preset_name_dst.empty());
        // Save preset_src->config into collection_dst under preset_name_dst.
        // The "compatible_printers" field should not have been exported into a config.ini or a G-code anyway, 
        // but some of the alpha versions of Slic3r did.
        ConfigOption *opt_compatible = preset_src->config.optptr("compatible_printers");
        if (opt_compatible != nullptr) {
            assert(opt_compatible->type() == coStrings);
            if (opt_compatible->type() == coStrings)
                static_cast<ConfigOptionStrings*>(opt_compatible)->values.clear();
        }
        (collection_dst.type() == Preset::TYPE_FILAMENT ? 
            collection_dst.load_preset(path, preset_name_dst, preset_src->config, activate) :
            // Only move the source config for non filament profiles, as single filament profile may be referenced multiple times.
            collection_dst.load_preset(path, preset_name_dst, std::move(preset_src->config), activate))
            .is_external = true;
        return preset_name_dst;
    };
    load_one(this->prints,        tmp_bundle.prints,        tmp_bundle.prints       .get_selected_preset_name(), true);
    load_one(this->sla_prints,    tmp_bundle.sla_prints,    tmp_bundle.sla_prints   .get_selected_preset_name(), true);
    load_one(this->filaments,     tmp_bundle.filaments,     tmp_bundle.filaments    .get_selected_preset_name(), true);
    load_one(this->sla_materials, tmp_bundle.sla_materials, tmp_bundle.sla_materials.get_selected_preset_name(), true);
    load_one(this->printers,      tmp_bundle.printers,      tmp_bundle.printers     .get_selected_preset_name(), true);
    this->update_multi_material_filament_presets();
    for (size_t i = 1; i < std::min(tmp_bundle.filament_presets.size(), this->filament_presets.size()); ++ i)
        this->filament_presets[i] = load_one(this->filaments, tmp_bundle.filaments, tmp_bundle.filament_presets[i], false);

    this->update_compatible(PresetSelectCompatibleType::Never);

    sort_remove_duplicates(config_substitutions);
    return config_substitutions;
}

// Process the Config Bundle loaded as a Boost property tree.
// For each print, filament and printer preset (group defined by group_name), apply the inherited presets.
// The presets starting with '*' are considered non-terminal and they are
// removed through the flattening process by this function.
// This function will never fail, but it will produce error messages through boost::log.
// system_profiles will not be flattened, and they will be kept inside the "inherits" field
static void flatten_configbundle_hierarchy(boost::property_tree::ptree &tree, const std::string &group_name, const std::vector<std::string> &system_profiles)
{
    namespace pt = boost::property_tree;

    // 1) For the group given by group_name, initialize the presets.
    struct Prst {
        Prst(const std::string &name, pt::ptree *node) : name(name), node(node) {}
        // Name of this preset. If the name starts with '*', it is an intermediate preset,
        // which will not make it into the result.
        const std::string           name;
        // Link to the source boost property tree node, owned by tree.
        pt::ptree                  *node;
        // Link to the presets, from which this preset inherits.
        std::vector<Prst*>          inherits;
        // Link to the presets, for which this preset is a direct parent.
        std::vector<Prst*>          parent_of;
        // When running the Kahn's Topological sorting algorithm, this counter is decreased from inherits.size() to zero.
        // A cycle is indicated, if the number does not drop to zero after the Kahn's algorithm finishes.
        size_t                      num_incoming_edges_left = 0;
        // Sorting by the name, to be used when inserted into std::set.
        bool operator==(const Prst &rhs) const { return this->name == rhs.name; }
        bool operator< (const Prst &rhs) const { return this->name < rhs.name; }
    };
    // Find the presets, store them into a std::map, addressed by their names.
    std::set<Prst> presets;
    std::string group_name_preset = group_name + ":";
    for (auto &section : tree)
        if (boost::starts_with(section.first, group_name_preset) && section.first.size() > group_name_preset.size())
            presets.emplace(section.first.substr(group_name_preset.size()), &section.second);
    // Fill in the "inherits" and "parent_of" members, report invalid inheritance fields.
    for (const Prst &prst : presets) {
        // Parse the list of comma separated values, possibly enclosed in quotes.
        std::vector<std::string> inherits_names;
        std::vector<std::string> inherits_system;
        if (Slic3r::unescape_strings_cstyle(prst.node->get<std::string>("inherits", ""), inherits_names)) {
            // Resolve the inheritance by name.
            std::vector<Prst*> &inherits_nodes = const_cast<Prst&>(prst).inherits;
            for (const std::string &node_name : inherits_names) {
                auto it_system = std::lower_bound(system_profiles.begin(), system_profiles.end(), node_name);
                if (it_system != system_profiles.end() && *it_system == node_name) {
                    // Loading a user config budnle, this preset is derived from a system profile.
                    inherits_system.emplace_back(node_name);
                } else {
                    auto it = presets.find(Prst(node_name, nullptr));
                    if (it == presets.end())
                        BOOST_LOG_TRIVIAL(error) << "flatten_configbundle_hierarchy: The preset " << prst.name << " inherits an unknown preset \"" << node_name << "\"";
                    else {
                        inherits_nodes.emplace_back(const_cast<Prst*>(&(*it)));
                        inherits_nodes.back()->parent_of.emplace_back(const_cast<Prst*>(&prst));
                    }
                }
            }
        } else {
            BOOST_LOG_TRIVIAL(error) << "flatten_configbundle_hierarchy: The preset " << prst.name << " has an invalid \"inherits\" field";
        }
        // Remove the "inherits" key, it has no meaning outside of the config bundle.
        const_cast<pt::ptree*>(prst.node)->erase("inherits");
        if (! inherits_system.empty()) {
            // Loaded a user config bundle, where a profile inherits a system profile.
			// User profile should be derived from a single system profile only.
			assert(inherits_system.size() == 1);
			if (inherits_system.size() > 1)
				BOOST_LOG_TRIVIAL(error) << "flatten_configbundle_hierarchy: The preset " << prst.name << " inherits from more than single system preset";
			prst.node->put("inherits", Slic3r::escape_string_cstyle(inherits_system.front()));
        }
    }

    // 2) Create a linear ordering for the directed acyclic graph of preset inheritance.
    // https://en.wikipedia.org/wiki/Topological_sorting
    // Kahn's algorithm.
    std::vector<Prst*> sorted;
    {
        // Initialize S with the set of all nodes with no incoming edge.
        std::deque<Prst*> S;
        for (const Prst &prst : presets)
            if (prst.inherits.empty())
                S.emplace_back(const_cast<Prst*>(&prst));
            else
                const_cast<Prst*>(&prst)->num_incoming_edges_left = prst.inherits.size();
        while (! S.empty()) {
            Prst *n = S.front();
            S.pop_front();
            sorted.emplace_back(n);
            for (Prst *m : n->parent_of) {
                assert(m->num_incoming_edges_left > 0);
                if (-- m->num_incoming_edges_left == 0) {
                    // We have visited all parents of m.
                    S.emplace_back(m);
                }
            }
        }
        if (sorted.size() < presets.size()) {
            for (const Prst &prst : presets)
                if (prst.num_incoming_edges_left)
                    BOOST_LOG_TRIVIAL(error) << "flatten_configbundle_hierarchy: The preset " << prst.name << " has cyclic dependencies";
        }
    }

    // Apply the dependencies in their topological ordering.
    for (Prst *prst : sorted) {
        // Merge the preset nodes in their order of application.
        // Iterate in a reverse order, so the last change will be placed first in merged.
        for (auto it_inherits = prst->inherits.rbegin(); it_inherits != prst->inherits.rend(); ++ it_inherits)
            for (auto it = (*it_inherits)->node->begin(); it != (*it_inherits)->node->end(); ++ it)
				if (it->first == "renamed_from") {
            		// Don't inherit "renamed_from" flag, it does not make sense. The "renamed_from" flag only makes sense for a concrete preset.
            		if (boost::starts_with((*it_inherits)->name, "*"))
			            BOOST_LOG_TRIVIAL(error) << boost::format("Nonpublic intermediate preset %1% contains a \"renamed_from\" field, which is ignored") % (*it_inherits)->name;
				} else if (prst->node->find(it->first) == prst->node->not_found())
                    prst->node->add_child(it->first, it->second);
    }

    // Remove the "internal" presets from the ptree. These presets are marked with '*'.
    group_name_preset += '*';
    for (auto it_section = tree.begin(); it_section != tree.end(); ) {
        if (boost::starts_with(it_section->first, group_name_preset) && it_section->first.size() > group_name_preset.size())
            // Remove the "internal" preset from the ptree.
            it_section = tree.erase(it_section);
        else
            // Keep the preset.
            ++ it_section;
    }
}

// preset_bundle is set when loading user config bundles, which must not overwrite the system profiles.
static void flatten_configbundle_hierarchy(boost::property_tree::ptree &tree, const PresetBundle *preset_bundle)
{
    flatten_configbundle_hierarchy(tree, "print",           preset_bundle ? preset_bundle->prints.system_preset_names()        : std::vector<std::string>());
    flatten_configbundle_hierarchy(tree, "filament",        preset_bundle ? preset_bundle->filaments.system_preset_names()     : std::vector<std::string>());
    flatten_configbundle_hierarchy(tree, "sla_print",       preset_bundle ? preset_bundle->sla_prints.system_preset_names()    : std::vector<std::string>());
    flatten_configbundle_hierarchy(tree, "sla_material",    preset_bundle ? preset_bundle->sla_materials.system_preset_names() : std::vector<std::string>());
    flatten_configbundle_hierarchy(tree, "printer",         preset_bundle ? preset_bundle->printers.system_preset_names()      : std::vector<std::string>());
}

// Load a config bundle file, into presets and store the loaded presets into separate files
// of the local configuration directory.
std::pair<PresetsConfigSubstitutions, size_t> PresetBundle::load_configbundle(const std::string &path, LoadConfigBundleAttributes flags)
{
    // Enable substitutions for user config bundle, throw an exception when loading a system profile.
    ConfigSubstitutionContext substitution_context {
        flags.has(LoadConfigBundleAttribute::LoadSystem) ?
            ForwardCompatibilitySubstitutionRule::Disable :
            ForwardCompatibilitySubstitutionRule::Enable 
        };

    PresetsConfigSubstitutions substitutions;

    if (flags.has(LoadConfigBundleAttribute::ResetUserProfile) || flags.has(LoadConfigBundleAttribute::LoadSystem))
        // Reset this bundle, delete user profile files if SaveImported.
        this->reset(flags.has(LoadConfigBundleAttribute::SaveImported));

    // 1) Read the complete config file into a boost::property_tree.
    namespace pt = boost::property_tree;
    pt::ptree tree;
    boost::nowide::ifstream ifs(path);
    try {
        pt::read_ini(ifs, tree);
    } catch (const boost::property_tree::ini_parser::ini_parser_error &err) {
        throw Slic3r::RuntimeError(format("Failed loading config bundle \"%1%\"\nError: \"%2%\" at line %3%", path, err.message(), err.line()).c_str());
    }

    const VendorProfile *vendor_profile = nullptr;
    if (flags.has(LoadConfigBundleAttribute::LoadSystem) || flags.has(LoadConfigBundleAttribute::LoadVendorOnly)) {
        auto vp = VendorProfile::from_ini(tree, path);
        if (vp.models.size() == 0) {
            BOOST_LOG_TRIVIAL(error) << boost::format("Vendor bundle: `%1%`: No printer model defined.") % path;
            return std::make_pair(PresetsConfigSubstitutions{}, 0);
        } else if (vp.num_variants() == 0) {
            BOOST_LOG_TRIVIAL(error) << boost::format("Vendor bundle: `%1%`: No printer variant defined") % path;
            return std::make_pair(PresetsConfigSubstitutions{}, 0);
        }
        vendor_profile = &this->vendors.insert({vp.id, vp}).first->second;
    }

    if (flags.has(LoadConfigBundleAttribute::LoadVendorOnly))
        return std::make_pair(PresetsConfigSubstitutions{}, 0);

    // 1.5) Flatten the config bundle by applying the inheritance rules. Internal profiles (with names starting with '*') are removed.
    // If loading a user config bundle, do not flatten with the system profiles, but keep the "inherits" flag intact.
    flatten_configbundle_hierarchy(tree, flags.has(LoadConfigBundleAttribute::LoadSystem) ? nullptr : this);

    // 2) Parse the property_tree, extract the active preset names and the profiles, save them into local config files.
    // Parse the obsolete preset names, to be deleted when upgrading from the old configuration structure.
    std::vector<std::string> loaded_prints;
    std::vector<std::string> loaded_filaments;
    std::vector<std::string> loaded_sla_prints;
    std::vector<std::string> loaded_sla_materials;
    std::vector<std::string> loaded_printers;
    std::vector<std::string> loaded_physical_printers;
    std::string              active_print;
    std::vector<std::string> active_filaments;
    std::string              active_sla_print;
    std::string              active_sla_material;
    std::string              active_printer;
    std::string              active_physical_printer;
    size_t                   presets_loaded = 0;
    size_t                   ph_printers_loaded = 0;

    for (const auto &section : tree) {
        PresetCollection         *presets = nullptr;
        std::string               preset_name;
        PhysicalPrinterCollection *ph_printers = nullptr;
        std::string               ph_printer_name;
        if (boost::starts_with(section.first, "print:")) {
            presets = &this->prints;
            preset_name = section.first.substr(6);
        } else if (boost::starts_with(section.first, "filament:")) {
            presets = &this->filaments;
            preset_name = section.first.substr(9);
        } else if (boost::starts_with(section.first, "sla_print:")) {
            presets = &this->sla_prints;
            preset_name = section.first.substr(10);
        } else if (boost::starts_with(section.first, "sla_material:")) {
            presets = &this->sla_materials;
            preset_name = section.first.substr(13);
        } else if (boost::starts_with(section.first, "printer:")) {
            presets = &this->printers;
            preset_name = section.first.substr(8);
        } else if (boost::starts_with(section.first, "physical_printer:")) {
            ph_printers = &this->physical_printers;
            ph_printer_name = section.first.substr(17);
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
                } else if (kvp.first == "sla_print") {
                    active_sla_print = kvp.second.data();
                } else if (kvp.first == "sla_material") {
                    active_sla_material = kvp.second.data();
                } else if (kvp.first == "printer") {
                    active_printer = kvp.second.data();
                }else if (kvp.first == "physical_printer") {
                    active_physical_printer = kvp.second.data();
                }
            }
        } else if (section.first == "obsolete_presets") {
            // Parse the names of obsolete presets. These presets will be deleted from user's
            // profile directory on installation of this vendor preset.
            for (auto &kvp : section.second) {
                std::vector<std::string> *dst = nullptr;
                if (kvp.first == "print")
                    dst = &this->obsolete_presets.prints;
                else if (kvp.first == "filament")
                    dst = &this->obsolete_presets.filaments;
                else if (kvp.first == "sla_print")
                    dst = &this->obsolete_presets.sla_prints;
                else if (kvp.first == "sla_material")
                    dst = &this->obsolete_presets.sla_materials;
                else if (kvp.first == "printer")
                    dst = &this->obsolete_presets.printers;
                if (dst)
                    unescape_strings_cstyle(kvp.second.data(), *dst);
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
            const DynamicPrintConfig *default_config = nullptr;
            DynamicPrintConfig        config;
            std::string 			  alias_name;
            std::vector<std::string>  renamed_from;
            auto parse_config_section = [&section, &alias_name, &renamed_from, &substitution_context, &path](DynamicPrintConfig &config) {
                substitution_context.substitutions.clear();
                for (auto &kvp : section.second) {
                	if (kvp.first == "alias")
                		alias_name = kvp.second.data();
                	else if (kvp.first == "renamed_from") {
                		if (! unescape_strings_cstyle(kvp.second.data(), renamed_from)) {
			                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The preset \"" << 
			                    section.first << "\" contains invalid \"renamed_from\" key, which is being ignored.";
                   		}
                	}
                    // Throws on parsing error. For system presets, no substituion is being done, but an exception is thrown.
                    config.set_deserialize(kvp.first, kvp.second.data(), substitution_context);
                }
            };
            if (presets == &this->printers) {
                // Select the default config based on the printer_technology field extracted from kvp.
                DynamicPrintConfig config_src;
                parse_config_section(config_src);
                default_config = &presets->default_preset_for(config_src).config;
                config = *default_config;
                config.apply(config_src);
            } else {
                default_config = &presets->default_preset().config;
                config = *default_config;
                parse_config_section(config);
            }
            Preset::normalize(config);
            // Report configuration fields, which are misplaced into a wrong group.
            std::string incorrect_keys = Preset::remove_invalid_keys(config, *default_config);
            if (! incorrect_keys.empty())
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
                    section.first << "\" contains the following incorrect keys: " << incorrect_keys << ", which were removed";
            if (flags.has(LoadConfigBundleAttribute::LoadSystem) && presets == &printers) {
                // Filter out printer presets, which are not mentioned in the vendor profile.
                // These presets are considered not installed.
                auto printer_model   = config.opt_string("printer_model");
                if (printer_model.empty()) {
                    BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
                        section.first << "\" defines no printer model, it will be ignored.";
                    continue;
                }
                auto printer_variant = config.opt_string("printer_variant");
                if (printer_variant.empty()) {
                    BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
                        section.first << "\" defines no printer variant, it will be ignored.";
                    continue;
                }
                auto it_model = std::find_if(vendor_profile->models.cbegin(), vendor_profile->models.cend(),
                    [&](const VendorProfile::PrinterModel &m) { return m.id == printer_model; }
                );
                if (it_model == vendor_profile->models.end()) {
                    BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
                        section.first << "\" defines invalid printer model \"" << printer_model << "\", it will be ignored.";
                    continue;
                }
                auto it_variant = it_model->variant(printer_variant);
                if (it_variant == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
                        section.first << "\" defines invalid printer variant \"" << printer_variant << "\", it will be ignored.";
                    continue;
                }
                const Preset *preset_existing = presets->find_preset(section.first, false);
                if (preset_existing != nullptr) {
                    BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The printer preset \"" << 
                        section.first << "\" has already been loaded from another Confing Bundle.";
                    continue;
                }
            } else if (! flags.has(LoadConfigBundleAttribute::LoadSystem)) {
                // This is a user config bundle.
                const Preset *existing = presets->find_preset(preset_name, false);
                if (existing != nullptr) {
                    if (existing->is_system) {
    					assert(existing->vendor != nullptr);
                        BOOST_LOG_TRIVIAL(error) << "Error in a user provided Config Bundle \"" << path << "\": The " << presets->name() << " preset \"" << 
    						existing->name << "\" is a system preset of vendor " << existing->vendor->name << " and it will be ignored.";
                        continue;
                    } else {
                        assert(existing->vendor == nullptr);
                        BOOST_LOG_TRIVIAL(trace) << "A " << presets->name() << " preset \"" << existing->name << "\" was overwritten with a preset from user Config Bundle \"" << path << "\"";
                    }
                } else {
					BOOST_LOG_TRIVIAL(trace) << "A new " << presets->name() << " preset \"" << preset_name << "\" was imported from user Config Bundle \"" << path << "\"";
                }
            }
            // Decide a full path to this .ini file.
            auto file_name = boost::algorithm::iends_with(preset_name, ".ini") ? preset_name : preset_name + ".ini";
            auto file_path = (boost::filesystem::path(data_dir()) 
#ifdef SLIC3R_PROFILE_USE_PRESETS_SUBDIR
                // Store the print/filament/printer presets into a "presets" directory.
                / "presets" 
#else
                // Store the print/filament/printer presets at the same location as the upstream Slic3r.
#endif
                / presets->section_name() / file_name).make_preferred();
            // Load the preset into the list of presets, save it to disk.
            Preset &loaded = presets->load_preset(file_path.string(), preset_name, std::move(config), false);
            if (flags.has(LoadConfigBundleAttribute::SaveImported))
                loaded.save();
            if (flags.has(LoadConfigBundleAttribute::LoadSystem)) {
                loaded.is_system = true;
                loaded.vendor = vendor_profile;
            }

            // Derive the profile logical name aka alias from the preset name if the alias was not stated explicitely.
            if (alias_name.empty()) {
                size_t end_pos = preset_name.find_first_of("@");
	            if (end_pos != std::string::npos) {
	                alias_name = preset_name.substr(0, end_pos);
	                if (renamed_from.empty())
	                	// Add the preset name with the '@' character removed into the "renamed_from" list.
	                	renamed_from.emplace_back(alias_name + preset_name.substr(end_pos + 1));
                    boost::trim_right(alias_name);
	            }
	        }
	        if (alias_name.empty())
	        	loaded.alias = preset_name;
	        else 
	         	loaded.alias = std::move(alias_name);
	        loaded.renamed_from = std::move(renamed_from);
            if (! substitution_context.empty())
                substitutions.push_back({ 
                    preset_name, presets->type(), PresetConfigSubstitutions::Source::ConfigBundle, 
                    std::string(), std::move(substitution_context.substitutions) });
            ++ presets_loaded;
        }

        if (ph_printers != nullptr) {
            // Load the physical printer
            const DynamicPrintConfig& default_config = ph_printers->default_config();
            DynamicPrintConfig        config = default_config;

            substitution_context.substitutions.clear();
            for (auto& kvp : section.second)
                config.set_deserialize(kvp.first, kvp.second.data(), substitution_context);

            // Report configuration fields, which are misplaced into a wrong group.
            std::string incorrect_keys = Preset::remove_invalid_keys(config, default_config);
            if (!incorrect_keys.empty())
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The physical printer \"" <<
                section.first << "\" contains the following incorrect keys: " << incorrect_keys << ", which were removed";

            const PhysicalPrinter* ph_printer_existing = ph_printers->find_printer(ph_printer_name, false);
            if (ph_printer_existing != nullptr) {
                BOOST_LOG_TRIVIAL(error) << "Error in a Vendor Config Bundle \"" << path << "\": The physical printer \"" <<
                    section.first << "\" has already been loaded from another Confing Bundle.";
                continue;
            }

            // Decide a full path to this .ini file.
            auto file_name = boost::algorithm::iends_with(ph_printer_name, ".ini") ? ph_printer_name : ph_printer_name + ".ini";
            auto file_path = (boost::filesystem::path(data_dir())
#ifdef SLIC3R_PROFILE_USE_PRESETS_SUBDIR
                // Store the physical printers into a "presets" directory.
                / "presets"
#else
                // Store the physical printers at the same location as the upstream Slic3r.
#endif
                / "physical_printer" / file_name).make_preferred();
            // Load the preset into the list of presets, save it to disk.
            ph_printers->load_printer(file_path.string(), ph_printer_name, std::move(config), false, flags.has(LoadConfigBundleAttribute::SaveImported));
            if (! substitution_context.empty())
                substitutions.push_back({
                    ph_printer_name, Preset::TYPE_PHYSICAL_PRINTER, PresetConfigSubstitutions::Source::ConfigBundle, 
                    std::string(), std::move(substitution_context.substitutions) });
            ++ ph_printers_loaded;
        }
    }

    // 3) Activate the presets and physical printer if any exists.
    if (! flags.has(LoadConfigBundleAttribute::LoadSystem)) {
        if (! active_print.empty()) 
            prints.select_preset_by_name(active_print, true);
        if (! active_sla_print.empty()) 
            sla_materials.select_preset_by_name(active_sla_print, true);
        if (! active_sla_material.empty()) 
            sla_materials.select_preset_by_name(active_sla_material, true);
        if (! active_printer.empty())
            printers.select_preset_by_name(active_printer, true);
        if (! active_physical_printer.empty())
            physical_printers.select_printer(active_physical_printer, active_printer);
        // Activate the first filament preset.
        if (! active_filaments.empty() && ! active_filaments.front().empty())
            filaments.select_preset_by_name(active_filaments.front(), true);
        this->update_multi_material_filament_presets();
        for (size_t i = 0; i < std::min(this->filament_presets.size(), active_filaments.size()); ++ i)
            this->filament_presets[i] = filaments.find_preset(active_filaments[i], true)->name;
        this->update_compatible(PresetSelectCompatibleType::Never);
    }

    return std::make_pair(std::move(substitutions), presets_loaded + ph_printers_loaded);
}

void PresetBundle::update_multi_material_filament_presets()
{
    if (printers.get_edited_preset().printer_technology() != ptFFF)
        return;

    // Verify and select the filament presets.
    auto   *nozzle_diameter = static_cast<const ConfigOptionFloats*>(printers.get_edited_preset().config.option("nozzle_diameter"));
    size_t  num_extruders   = nozzle_diameter->values.size();
    // Verify validity of the current filament presets.
    for (size_t i = 0; i < std::min(this->filament_presets.size(), num_extruders); ++ i)
        this->filament_presets[i] = this->filaments.find_preset(this->filament_presets[i], true)->name;
    // Append the rest of filament presets.
    this->filament_presets.resize(num_extruders, this->filament_presets.empty() ? this->filaments.first_visible().name : this->filament_presets.back());

    // Now verify if wiping_volumes_matrix has proper size (it is used to deduce number of extruders in wipe tower generator):
    std::vector<double> old_matrix = this->project_config.option<ConfigOptionFloats>("wiping_volumes_matrix")->values;
    size_t old_number_of_extruders = size_t(sqrt(old_matrix.size())+EPSILON);
    if (num_extruders != old_number_of_extruders) {
            // First verify if purging volumes presets for each extruder matches number of extruders
            std::vector<double>& extruders = this->project_config.option<ConfigOptionFloats>("wiping_volumes_extruders")->values;
            while (extruders.size() < 2*num_extruders) {
                extruders.push_back(extruders.size()>1 ? extruders[0] : 50.);  // copy the values from the first extruder
                extruders.push_back(extruders.size()>1 ? extruders[1] : 50.);
            }
            while (extruders.size() > 2*num_extruders) {
                extruders.pop_back();
                extruders.pop_back();
            }

        std::vector<double> new_matrix;
        for (unsigned int i=0;i<num_extruders;++i)
            for (unsigned int j=0;j<num_extruders;++j) {
                // append the value for this pair from the old matrix (if it's there):
                if (i<old_number_of_extruders && j<old_number_of_extruders)
                    new_matrix.push_back(old_matrix[i*old_number_of_extruders + j]);
                else
                    new_matrix.push_back( i==j ? 0. : extruders[2*i]+extruders[2*j+1]); // so it matches new extruder volumes
            }
		this->project_config.option<ConfigOptionFloats>("wiping_volumes_matrix")->values = new_matrix;
    }
}

void PresetBundle::update_compatible(PresetSelectCompatibleType select_other_print_if_incompatible, PresetSelectCompatibleType select_other_filament_if_incompatible)
{
    const Preset					&printer_preset					    = this->printers.get_edited_preset();
	const PresetWithVendorProfile    printer_preset_with_vendor_profile = this->printers.get_preset_with_vendor_profile(printer_preset);

    class PreferedProfileMatch
    {
    public:
        PreferedProfileMatch(const std::string &prefered_alias, const std::string &prefered_name) :
            m_prefered_alias(prefered_alias), m_prefered_name(prefered_name) {}

        int operator()(const Preset &preset) const
        {
            return 
                preset.is_default || preset.is_external ?
                    // Don't match any properties of the "-- default --" profile or the external profiles when switching printer profile.
                    0 :
                    ! m_prefered_alias.empty() && m_prefered_alias == preset.alias ?
                        // Matching an alias, always take this preset with priority.
                        std::numeric_limits<int>::max() :
                        // Otherwise take the prefered profile, or the first compatible.
                        preset.name == m_prefered_name;
        }

    private:
        const std::string  m_prefered_alias;
        const std::string &m_prefered_name;
    };

    // Matching by the layer height in addition.
    class PreferedPrintProfileMatch : public PreferedProfileMatch
    {
    public:
        PreferedPrintProfileMatch(const Preset *preset, const std::string &prefered_name) :
            PreferedProfileMatch(preset ? preset->alias : std::string(), prefered_name), m_prefered_layer_height(preset ? preset->config.opt_float("layer_height") : 0) {}

        int operator()(const Preset &preset) const
        {
            // Don't match any properties of the "-- default --" profile or the external profiles when switching printer profile.
            if (preset.is_default || preset.is_external)
                return 0;
            int match_quality = PreferedProfileMatch::operator()(preset);
            if (match_quality < std::numeric_limits<int>::max()) {
                match_quality += 1;
                if (m_prefered_layer_height > 0. && std::abs(preset.config.opt_float("layer_height") - m_prefered_layer_height) < 0.0005)
                    match_quality *= 10;
            }
            return match_quality;
        }

    private:
        const double m_prefered_layer_height;
    };

    // Matching by the layer height in addition.
    class PreferedFilamentProfileMatch : public PreferedProfileMatch
    {
    public:
        PreferedFilamentProfileMatch(const Preset *preset, const std::string &prefered_name) :
            PreferedProfileMatch(preset ? preset->alias : std::string(), prefered_name), 
            m_prefered_filament_type(preset ? preset->config.opt_string("filament_type", 0) : std::string()) {}

        int operator()(const Preset &preset) const
        {
            // Don't match any properties of the "-- default --" profile or the external profiles when switching printer profile.
            if (preset.is_default || preset.is_external)
                return 0;
            int match_quality = PreferedProfileMatch::operator()(preset);
            if (match_quality < std::numeric_limits<int>::max()) {
                match_quality += 1;
                if (! m_prefered_filament_type.empty() && m_prefered_filament_type == preset.config.opt_string("filament_type", 0))
                    match_quality *= 10;
            }
            return match_quality;
        }

    private:
        const std::string m_prefered_filament_type;
    };

    // Matching by the layer height in addition.
    class PreferedFilamentsProfileMatch
    {
    public:
        PreferedFilamentsProfileMatch(const Preset *preset, const std::vector<std::string> &prefered_names) :
            m_prefered_alias(preset ? preset->alias : std::string()),
            m_prefered_filament_type(preset ? preset->config.opt_string("filament_type", 0) : std::string()),
            m_prefered_names(prefered_names)
            {}

        int operator()(const Preset &preset) const
        {
            // Don't match any properties of the "-- default --" profile or the external profiles when switching printer profile.
            if (preset.is_default || preset.is_external)
                return 0;
            if (! m_prefered_alias.empty() && m_prefered_alias == preset.alias)
                // Matching an alias, always take this preset with priority.
                return std::numeric_limits<int>::max();
            int match_quality = (std::find(m_prefered_names.begin(), m_prefered_names.end(), preset.name) != m_prefered_names.end()) + 1;
            if (! m_prefered_filament_type.empty() && m_prefered_filament_type == preset.config.opt_string("filament_type", 0))
                match_quality *= 10;
            return match_quality;
        }

    private:
        const std::string               m_prefered_alias;
        const std::string               m_prefered_filament_type;
        const std::vector<std::string> &m_prefered_names;
    };

	switch (printer_preset.printer_technology()) {
    case ptFFF:
    {
		assert(printer_preset.config.has("default_print_profile"));
		assert(printer_preset.config.has("default_filament_profile"));
        const std::vector<std::string> &prefered_filament_profiles = printer_preset.config.option<ConfigOptionStrings>("default_filament_profile")->values;
        this->prints.update_compatible(printer_preset_with_vendor_profile, nullptr, select_other_print_if_incompatible,
            PreferedPrintProfileMatch(this->prints.get_selected_idx() == size_t(-1) ? nullptr : &this->prints.get_edited_preset(), printer_preset.config.opt_string("default_print_profile")));
        const PresetWithVendorProfile   print_preset_with_vendor_profile = this->prints.get_edited_preset_with_vendor_profile();
        // Remember whether the filament profiles were compatible before updating the filament compatibility.
        std::vector<char> 				filament_preset_was_compatible(this->filament_presets.size(), false);
        for (size_t idx = 0; idx < this->filament_presets.size(); ++ idx) {
            Preset *preset = this->filaments.find_preset(this->filament_presets[idx], false);
            filament_preset_was_compatible[idx] = preset != nullptr && preset->is_compatible;
        }
        // First select a first compatible profile for the preset editor.
        this->filaments.update_compatible(printer_preset_with_vendor_profile, &print_preset_with_vendor_profile, select_other_filament_if_incompatible,
            PreferedFilamentsProfileMatch(this->filaments.get_selected_idx() == size_t(-1) ? nullptr : &this->filaments.get_edited_preset(), prefered_filament_profiles));
        if (select_other_filament_if_incompatible != PresetSelectCompatibleType::Never) {
            // Verify validity of the current filament presets.
            const std::string prefered_filament_profile = prefered_filament_profiles.empty() ? std::string() : prefered_filament_profiles.front();
            if (this->filament_presets.size() == 1) {
                // The compatible profile should have been already selected for the preset editor. Just use it.
            	if (select_other_filament_if_incompatible == PresetSelectCompatibleType::Always || filament_preset_was_compatible.front())
                	this->filament_presets.front() = this->filaments.get_edited_preset().name;
            } else {
                for (size_t idx = 0; idx < this->filament_presets.size(); ++ idx) {
                    std::string &filament_name = this->filament_presets[idx];
                    Preset      *preset = this->filaments.find_preset(filament_name, false);
                    if (preset == nullptr || (! preset->is_compatible && (select_other_filament_if_incompatible == PresetSelectCompatibleType::Always || filament_preset_was_compatible[idx])))
                        // Pick a compatible profile. If there are prefered_filament_profiles, use them.
                        filament_name = this->filaments.first_compatible(
                            PreferedFilamentProfileMatch(preset,
                                (idx < prefered_filament_profiles.size()) ? prefered_filament_profiles[idx] : prefered_filament_profile)).name;
                }
            }
        }
		break;
    }
    case ptSLA:
    {
		assert(printer_preset.config.has("default_sla_print_profile"));
		assert(printer_preset.config.has("default_sla_material_profile"));
		this->sla_prints.update_compatible(printer_preset_with_vendor_profile, nullptr, select_other_print_if_incompatible,
            PreferedPrintProfileMatch(this->sla_prints.get_selected_idx() == size_t(-1) ? nullptr : &this->sla_prints.get_edited_preset(), printer_preset.config.opt_string("default_sla_print_profile")));
        const PresetWithVendorProfile sla_print_preset_with_vendor_profile = this->sla_prints.get_edited_preset_with_vendor_profile();
		this->sla_materials.update_compatible(printer_preset_with_vendor_profile, &sla_print_preset_with_vendor_profile, select_other_filament_if_incompatible,
            PreferedProfileMatch(this->sla_materials.get_selected_idx() == size_t(-1) ? std::string() : this->sla_materials.get_edited_preset().alias, printer_preset.config.opt_string("default_sla_material_profile")));
		break;
	}
    default: break;
    }
}

void PresetBundle::export_configbundle(const std::string &path, bool export_system_settings, bool export_physical_printers/* = false*/)
{
    boost::nowide::ofstream c;
    c.open(path, std::ios::out | std::ios::trunc);

    // Put a comment at the first line including the time stamp and Slic3r version.
    c << "# " << Slic3r::header_slic3r_generated() << std::endl;

    // Export the print, filament and printer profiles.

	for (const PresetCollection *presets : { 
		(const PresetCollection*)&this->prints, (const PresetCollection*)&this->filaments, 
		(const PresetCollection*)&this->sla_prints, (const PresetCollection*)&this->sla_materials, 
		(const PresetCollection*)&this->printers }) {
        for (const Preset &preset : (*presets)()) {
            if (preset.is_default || preset.is_external || (preset.is_system && ! export_system_settings))
                // Only export the common presets, not external files or the default preset.
                continue;
            c << std::endl << "[" << presets->section_name() << ":" << preset.name << "]" << std::endl;
            for (const std::string &opt_key : preset.config.keys())
                c << opt_key << " = " << preset.config.opt_serialize(opt_key) << std::endl;
        }
    }

    if (export_physical_printers) {
        for (const PhysicalPrinter& ph_printer : this->physical_printers) {
            c << std::endl << "[physical_printer:" << ph_printer.name << "]" << std::endl;
            for (const std::string& opt_key : ph_printer.config.keys())
                c << opt_key << " = " << ph_printer.config.opt_serialize(opt_key) << std::endl;
        }
    }

    // Export the names of the active presets.
    c << std::endl << "[presets]" << std::endl;
    c << "print = " << this->prints.get_selected_preset_name() << std::endl;
    c << "sla_print = " << this->sla_prints.get_selected_preset_name() << std::endl;
    c << "sla_material = " << this->sla_materials.get_selected_preset_name() << std::endl;
    c << "printer = " << this->printers.get_selected_preset_name() << std::endl;
    for (size_t i = 0; i < this->filament_presets.size(); ++ i) {
        char suffix[64];
        if (i > 0)
            sprintf(suffix, "_%d", (int)i);
        else
            suffix[0] = 0;
        c << "filament" << suffix << " = " << this->filament_presets[i] << std::endl;
    }

    if (export_physical_printers && this->physical_printers.get_selected_idx() >= 0)
        c << "physical_printer = " << this->physical_printers.get_selected_printer_name() << std::endl;
#if 0
    // Export the following setting values from the provided setting repository.
    static const char *settings_keys[] = { "autocenter" };
    c << "[settings]" << std::endl;
    for (size_t i = 0; i < sizeof(settings_keys) / sizeof(settings_keys[0]); ++ i)
        c << settings_keys[i] << " = " << settings.serialize(settings_keys[i]) << std::endl;
#endif

    c.close();
}

// Set the filament preset name. As the name could come from the UI selection box, 
// an optional "(modified)" suffix will be removed from the filament name.
void PresetBundle::set_filament_preset(size_t idx, const std::string &name)
{
	if (idx >= filament_presets.size())
        filament_presets.resize(idx + 1, filaments.default_preset().name);
    filament_presets[idx] = Preset::remove_suffix_modified(name);
}

void PresetBundle::set_default_suppressed(bool default_suppressed)
{
    prints.set_default_suppressed(default_suppressed);
    filaments.set_default_suppressed(default_suppressed);
    sla_prints.set_default_suppressed(default_suppressed);
    sla_materials.set_default_suppressed(default_suppressed);
    printers.set_default_suppressed(default_suppressed);
}

} // namespace Slic3r
