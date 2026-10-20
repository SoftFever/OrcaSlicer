#include "PrintConfig.hpp"
#include "CommonDefs.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "PrintConfigConstants.hpp"
#include "BeltTransform.hpp"
#include "ClipperUtils.hpp"
#include "Config.hpp"
#include "Geometry.hpp"
#include "FilamentMixer.hpp"
#include "MaterialType.hpp"
#include "I18N.hpp"
#include "enum_bitmask.hpp"
#include "format.hpp"

#include "GCode/Thumbnails.hpp"
#include <numeric>
#include <cstddef>
#include <algorithm>
#include <cassert>
#include <map>
#include <boost/algorithm/string/classification.hpp>
#include <iterator>
#include <cstdlib>
#include "libslic3r.h"
#include <boost/algorithm/string/predicate.hpp>
#include <cmath>
#include <boost/algorithm/string/constants.hpp>
#include <limits>
#include <memory>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>
#include <boost/preprocessor/tuple/to_seq.hpp>
#include <cstdint>
#include <set>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <boost/thread.hpp>
#include <float.h>
#include <string>
#include <vector>
#include <utility>
#include <sstream>
#include <unordered_map>


namespace Slic3r {


static t_config_enum_names enum_names_from_keys_map(const t_config_enum_values &enum_keys_map)
{
    t_config_enum_names names;
    int cnt = 0;
    for (const auto& kvp : enum_keys_map)
        cnt = std::max(cnt, kvp.second);
    cnt += 1;
    names.assign(cnt, "");
    for (const auto& kvp : enum_keys_map)
        names[kvp.second] = kvp.first;
    return names;
}

static const t_config_enum_values s_keys_map_ExtruderType = {
    { "Direct Drive",   etDirectDrive },
    { "Bowden",        etBowden }
};

static t_config_enum_names s_keys_names_ExtruderType = enum_names_from_keys_map(s_keys_map_ExtruderType);

static const t_config_enum_values s_keys_map_NozzleVolumeType = {
    { "Standard",  nvtStandard },
    { "High Flow", nvtHighFlow },
    { "TPU High Flow", nvtTPUHighFlow },
    { "Hybrid", nvtHybrid },
    { "E3D High Flow", nvtE3DHighFlow },
    { "Extra High Flow", nvtExtraHighFlow }
};

static t_config_enum_names s_keys_names_NozzleVolumeType = enum_names_from_keys_map(s_keys_map_NozzleVolumeType);


DynamicPrintConfig DynamicPrintConfig::full_print_config()
{
	return DynamicPrintConfig((const PrintRegionConfig&)FullPrintConfig::defaults());
}

DynamicPrintConfig::DynamicPrintConfig(const StaticPrintConfig& rhs) : DynamicConfig(rhs, rhs.keys_ref())
{
}

DynamicPrintConfig* DynamicPrintConfig::new_from_defaults_keys(const std::vector<std::string> &keys)
{
    auto *out = new DynamicPrintConfig();
    out->apply_only(FullPrintConfig::defaults(), keys);
    return out;
}

double min_object_distance(const ConfigBase &cfg)
{
    const ConfigOptionEnum<PrinterTechnology> *opt_printer_technology = cfg.option<ConfigOptionEnum<PrinterTechnology>>("printer_technology");
    auto printer_technology = opt_printer_technology ? opt_printer_technology->value : ptUnknown;

    double ret = 0.;

    if (printer_technology == ptSLA)
        ret = 6.;
    else {
        //BBS: duplicate_distance seam to be useless
        constexpr double duplicate_distance = 6.;
        auto ecr_opt = cfg.option<ConfigOptionFloat>("extruder_clearance_radius");
        auto co_opt  = cfg.option<ConfigOptionEnum<PrintSequence>>("print_sequence");

        if (!ecr_opt || !co_opt)
            ret = 0.;
        else {
            // min object distance is max(duplicate_distance, clearance_radius)
            ret = ((co_opt->value == PrintSequence::ByObject) && ecr_opt->value > duplicate_distance) ?
                      ecr_opt->value : duplicate_distance;
        }
    }

    return ret;
}

void DynamicPrintConfig::normalize_fdm(int used_filaments)
{
    if (this->has("extruder")) {
        int extruder = this->option("extruder")->getInt();
        this->erase("extruder");
        if (extruder != 0) {
            if (!this->has("sparse_infill_filament_id") || this->option("sparse_infill_filament_id")->getInt() == 0)
                this->option("sparse_infill_filament_id", true)->setInt(extruder);
            if (!this->has("outer_wall_filament_id") || this->option("outer_wall_filament_id")->getInt() == 0)
                this->option("outer_wall_filament_id", true)->setInt(extruder);
            if (!this->has("inner_wall_filament_id") || this->option("inner_wall_filament_id")->getInt() == 0)
                this->option("inner_wall_filament_id", true)->setInt(extruder);
            if (!this->has("internal_solid_filament_id") || this->option("internal_solid_filament_id")->getInt() == 0)
                this->option("internal_solid_filament_id", true)->setInt(extruder);
            if (!this->has("top_surface_filament_id") || this->option("top_surface_filament_id")->getInt() == 0)
                this->option("top_surface_filament_id", true)->setInt(extruder);
            if (!this->has("bottom_surface_filament_id") || this->option("bottom_surface_filament_id")->getInt() == 0)
                this->option("bottom_surface_filament_id", true)->setInt(extruder);
            // Don't propagate the current extruder to support.
            // For non-soluble supports, the default "0" extruder means to use the active extruder,
            // for soluble supports one certainly does not want to set the extruder to non-soluble.
            // if (!this->has("support_filament"))
            //     this->option("support_filament", true)->setInt(extruder);
            // if (!this->has("support_interface_filament"))
            //     this->option("support_interface_filament", true)->setInt(extruder);
        }
    }

    if (this->has("sparse_infill_filament_id")) {
        int sparse_infill_filament_id = this->option("sparse_infill_filament_id")->getInt();
        if (sparse_infill_filament_id > 0 && (!this->has("internal_solid_filament_id") || this->option("internal_solid_filament_id")->getInt() == 0))
            this->option("internal_solid_filament_id", true)->setInt(sparse_infill_filament_id);
    }

    const int internal_solid = this->has("internal_solid_filament_id") ? this->option("internal_solid_filament_id")->getInt() : 0;
    const int top_surface    = this->has("top_surface_filament_id") ? this->option("top_surface_filament_id")->getInt() : 0;
    const int bottom_surface = this->has("bottom_surface_filament_id") ? this->option("bottom_surface_filament_id")->getInt() : 0;

    if (internal_solid == 0 && top_surface > 0)
        this->option("internal_solid_filament_id", true)->setInt(top_surface);
    if (internal_solid == 0 && bottom_surface > 0)
        this->option("internal_solid_filament_id", true)->setInt(bottom_surface);
    if (top_surface == 0 && internal_solid > 0)
        this->option("top_surface_filament_id", true)->setInt(internal_solid);
    if (bottom_surface == 0 && internal_solid > 0)
        this->option("bottom_surface_filament_id", true)->setInt(internal_solid);

    if (this->has("spiral_mode") && this->opt<ConfigOptionBool>("spiral_mode", true)->value) {
        {
            // this should be actually done only on the spiral layers instead of all
            auto* opt = this->opt<ConfigOptionBools>("retract_when_changing_layer", true);
            opt->values.assign(opt->values.size(), false);  // set all values to false
            // Disable retract on layer change also for filament overrides.
            auto* opt_n = this->opt<ConfigOptionBoolsNullable>("filament_retract_when_changing_layer", true);
            opt_n->values.assign(opt_n->values.size(), false);  // Set all values to false.
        }
        {
            this->opt<ConfigOptionInt>("wall_loops", true)->value       = 1;
            this->opt<ConfigOptionBool>("alternate_extra_wall", true)->value = false;
            this->opt<ConfigOptionInt>("top_shell_layers", true)->value = 0;
            this->opt<ConfigOptionPercent>("sparse_infill_density", true)->value = 0;
        }
    }

    if (auto *opt_gcode_resolution = this->opt<ConfigOptionFloat>("resolution", false); opt_gcode_resolution)
        // Resolution will be above 1um.
        opt_gcode_resolution->value = std::max(opt_gcode_resolution->value, 0.001);

    // BBS
    ConfigOptionBool* ept_opt = this->option<ConfigOptionBool>("enable_prime_tower");
    if (used_filaments > 0 && ept_opt != nullptr) {
        ConfigOptionBool* islh_opt = this->option<ConfigOptionBool>("independent_support_layer_height", true);
        //ConfigOptionBool* alh_opt = this->option<ConfigOptionBool>("adaptive_layer_height");
        ConfigOptionEnum<PrintSequence>* ps_opt = this->option<ConfigOptionEnum<PrintSequence>>("print_sequence");

        ConfigOptionEnum<TimelapseType>* timelapse_opt = this->option<ConfigOptionEnum<TimelapseType>>("timelapse_type");
        bool is_smooth_timelapse = timelapse_opt != nullptr && timelapse_opt->value == TimelapseType::tlSmooth;
        if (!is_smooth_timelapse && (used_filaments == 1 || ps_opt->value == PrintSequence::ByObject)) {
            ept_opt->value = false;
        }

        if (ept_opt->value) {
            if (islh_opt)
                islh_opt->value = false;
            //if (alh_opt)
            //    alh_opt->value = false;
        }
        /* BBS: MusangKing - not sure if this is still valid, just comment it out cause "Independent support layer height" is re-opened.
        else {
            if (islh_opt)
                islh_opt->value = true;
        }
        */
    }
}

//BBS:divide normalize_fdm to 2 steps and call them one by one in Print::Apply

void DynamicPrintConfig::normalize_fdm_1()
{
    if (this->has("extruder")) {
        int extruder = this->option("extruder")->getInt();
        this->erase("extruder");
        if (extruder != 0) {
            if (!this->has("sparse_infill_filament_id") || this->option("sparse_infill_filament_id")->getInt() == 0)
                this->option("sparse_infill_filament_id", true)->setInt(extruder);
            if (!this->has("outer_wall_filament_id") || this->option("outer_wall_filament_id")->getInt() == 0)
                this->option("outer_wall_filament_id", true)->setInt(extruder);
            if (!this->has("inner_wall_filament_id") || this->option("inner_wall_filament_id")->getInt() == 0)
                this->option("inner_wall_filament_id", true)->setInt(extruder);
            if (!this->has("internal_solid_filament_id") || this->option("internal_solid_filament_id")->getInt() == 0)
                this->option("internal_solid_filament_id", true)->setInt(extruder);
            if (!this->has("top_surface_filament_id") || this->option("top_surface_filament_id")->getInt() == 0)
                this->option("top_surface_filament_id", true)->setInt(extruder);
            if (!this->has("bottom_surface_filament_id") || this->option("bottom_surface_filament_id")->getInt() == 0)
                this->option("bottom_surface_filament_id", true)->setInt(extruder);
            // Don't propagate the current extruder to support.
            // For non-soluble supports, the default "0" extruder means to use the active extruder,
            // for soluble supports one certainly does not want to set the extruder to non-soluble.
            // if (!this->has("support_filament"))
            //     this->option("support_filament", true)->setInt(extruder);
            // if (!this->has("support_interface_filament"))
            //     this->option("support_interface_filament", true)->setInt(extruder);
        }
    }

    if (this->has("sparse_infill_filament_id")) {
        int sparse_infill_filament_id = this->option("sparse_infill_filament_id")->getInt();
        if (sparse_infill_filament_id > 0 && (!this->has("internal_solid_filament_id") || this->option("internal_solid_filament_id")->getInt() == 0))
            this->option("internal_solid_filament_id", true)->setInt(sparse_infill_filament_id);
    }

    const int internal_solid = this->has("internal_solid_filament_id") ? this->option("internal_solid_filament_id")->getInt() : 0;
    const int top_surface    = this->has("top_surface_filament_id") ? this->option("top_surface_filament_id")->getInt() : 0;
    const int bottom_surface = this->has("bottom_surface_filament_id") ? this->option("bottom_surface_filament_id")->getInt() : 0;

    if (internal_solid == 0 && top_surface > 0)
        this->option("internal_solid_filament_id", true)->setInt(top_surface);
    if (internal_solid == 0 && bottom_surface > 0)
        this->option("internal_solid_filament_id", true)->setInt(bottom_surface);
    if (top_surface == 0 && internal_solid > 0)
        this->option("top_surface_filament_id", true)->setInt(internal_solid);
    if (bottom_surface == 0 && internal_solid > 0)
        this->option("bottom_surface_filament_id", true)->setInt(internal_solid);

    if (this->has("spiral_mode") && this->opt<ConfigOptionBool>("spiral_mode", true)->value) {
        {
            // this should be actually done only on the spiral layers instead of all
            auto* opt = this->opt<ConfigOptionBools>("retract_when_changing_layer", true);
            opt->values.assign(opt->values.size(), false);  // set all values to false
            // Disable retract on layer change also for filament overrides.
            auto* opt_n = this->opt<ConfigOptionBoolsNullable>("filament_retract_when_changing_layer", true);
            opt_n->values.assign(opt_n->values.size(), false);  // Set all values to false.
        }
        {
            this->opt<ConfigOptionInt>("wall_loops", true)->value       = 1;
            this->opt<ConfigOptionBool>("alternate_extra_wall", true)->value = false;
            this->opt<ConfigOptionInt>("top_shell_layers", true)->value = 0;
            this->opt<ConfigOptionPercent>("sparse_infill_density", true)->value = 0;
        }
    }

    if (auto *opt_gcode_resolution = this->opt<ConfigOptionFloat>("resolution", false); opt_gcode_resolution)
        // Resolution will be above 1um.
        opt_gcode_resolution->value = std::max(opt_gcode_resolution->value, 0.001);

    return;
}

t_config_option_keys DynamicPrintConfig::normalize_fdm_2(int num_objects, int used_filaments)
{
    t_config_option_keys changed_keys;
    ConfigOptionBool* ept_opt = this->option<ConfigOptionBool>("enable_prime_tower");
    if (used_filaments > 0 && ept_opt != nullptr) {
        ConfigOptionBool* islh_opt = this->option<ConfigOptionBool>("independent_support_layer_height", true);
        //ConfigOptionBool* alh_opt = this->option<ConfigOptionBool>("adaptive_layer_height");
        ConfigOptionEnum<PrintSequence>* ps_opt = this->option<ConfigOptionEnum<PrintSequence>>("print_sequence");

        ConfigOptionEnum<TimelapseType>* timelapse_opt = this->option<ConfigOptionEnum<TimelapseType>>("timelapse_type");
        bool is_smooth_timelapse = timelapse_opt != nullptr && timelapse_opt->value == TimelapseType::tlSmooth;

        ConfigOptionBool *enable_wrapping_opt = this->option<ConfigOptionBool>("enable_wrapping_detection");
        bool enable_wrapping = enable_wrapping_opt != nullptr && enable_wrapping_opt->value;

        bool has_mixed_filament = false;
        {
            auto *mixed_opt = this->option<ConfigOptionBools>("filament_is_mixed");
            if (mixed_opt)
                has_mixed_filament = has_any_mixed_filament(mixed_opt->values);
        }
        if (!is_smooth_timelapse && !enable_wrapping
            && (  (used_filaments == 1 && !has_mixed_filament)
                || (ps_opt->value == PrintSequence::ByObject && num_objects > 1))) {
            if (ept_opt->value) {
                ept_opt->value = false;
                changed_keys.push_back("enable_prime_tower");
            }
            //ept_opt->value = false;
        }

        if (ept_opt->value) {
            if (islh_opt) {
                if (islh_opt->value) {
                    islh_opt->value = false;
                    changed_keys.push_back("independent_support_layer_height");
                }
                //islh_opt->value = false;
            }
            //if (alh_opt) {
            //    if (alh_opt->value) {
            //        alh_opt->value = false;
            //        changed_keys.push_back("adaptive_layer_height");
            //    }
            //    //alh_opt->value = false;
            //}
        }
        /* BBS：MusangKing - use "global->support->Independent support layer height" widget to replace previous assignment
        else {
            if (islh_opt) {
                if (!islh_opt->value) {
                    islh_opt->value = true;
                    changed_keys.push_back("independent_support_layer_height");
                }
                //islh_opt->value = true;
            }
        }
        */
    }

    return changed_keys;
}

void  handle_legacy_sla(DynamicPrintConfig &config)
{
    for (std::string corr : {"relative_correction", "material_correction"}) {
        if (config.has(corr)) {
            if (std::string corr_x = corr + "_x"; !config.has(corr_x)) {
                auto* opt = config.opt<ConfigOptionFloat>(corr_x, true);
                opt->value = config.opt<ConfigOptionFloats>(corr)->values[0];
            }

            if (std::string corr_y = corr + "_y"; !config.has(corr_y)) {
                auto* opt = config.opt<ConfigOptionFloat>(corr_y, true);
                opt->value = config.opt<ConfigOptionFloats>(corr)->values[0];
            }

            if (std::string corr_z = corr + "_z"; !config.has(corr_z)) {
                auto* opt = config.opt<ConfigOptionFloat>(corr_z, true);
                opt->value = config.opt<ConfigOptionFloats>(corr)->values[1];
            }
        }
    }
}

size_t DynamicPrintConfig::get_parameter_size(const std::string& param_name, size_t extruder_nums)
{
    constexpr size_t default_param_length = 1;
    size_t filament_variant_length = default_param_length;
    size_t process_variant_length = default_param_length;
    size_t machine_variant_length = default_param_length;

    if (this->has("filament_extruder_variant"))
        filament_variant_length = this->option<ConfigOptionStrings>("filament_extruder_variant")->size();
    if (this->has("print_extruder_variant"))
        process_variant_length = this->option<ConfigOptionStrings>("print_extruder_variant")->size();
    if (this->has("printer_extruder_variant"))
        machine_variant_length = this->option<ConfigOptionStrings>("printer_extruder_variant")->size();

    if (printer_options_with_variant_1.count(param_name) > 0) {
        return machine_variant_length;
    }
    else if (printer_options_with_variant_2.count(param_name) > 0) {
        return machine_variant_length * 2;
    }
    else if (filament_options_with_variant.count(param_name) > 0) {
        return filament_variant_length;
    }
    else if (print_options_with_variant.count(param_name) > 0) {
        return process_variant_length;
    }
    return extruder_nums;
}

// Orca: Special handling for extruder variants
// BBL printers have extruder variants pre-defined in system profiles, however for customized multi-extruder profile,
// we need to set up these parameters automatically, otherwise per-extruder options won't work properly.
static void extend_extruder_variant(DynamicPrintConfig& config, const unsigned int num_extruders)
{
    // 1. Make sure the `extruder_variant_list` is the same length as extruder cnt
    if (!config.has("extruder_variant_list")) {
        config.set_key_value("extruder_variant_list",
                             new ConfigOptionStrings(std::vector<std::string>(num_extruders, "Direct Drive Standard")));
    }
    auto extruder_variant_opt = dynamic_cast<ConfigOptionStrings*>(config.option("extruder_variant_list"));
    assert(extruder_variant_opt != nullptr);
    extruder_variant_opt->resize(num_extruders, extruder_variant_opt); // Use the first option as the default value, so all extruders have the same variant

    // 2. Update `printer_extruder_variant` and `printer_extruder_id` based on `extruder_variant_list`
    auto printer_extruder_id_opt = dynamic_cast<ConfigOptionInts*>(config.option("printer_extruder_id"));
    assert(printer_extruder_id_opt != nullptr);
    printer_extruder_id_opt->values.clear();
    auto printer_extruder_variant_opt = dynamic_cast<ConfigOptionStrings*>(config.option("printer_extruder_variant"));
    assert(printer_extruder_variant_opt != nullptr);
    printer_extruder_variant_opt->values.clear();
    for (int i = 0; i < num_extruders; i++) {
        // `extruder_variant_list` specifies supported variant of each nozzle/extruder,
        // each item is a comma separated list of variants (extruder type + nozzle flow type) this extruder supported
        std::string variant = extruder_variant_opt->get_at(i);
        std::vector<std::string> variants_list;
        boost::split(variants_list, variant, boost::is_any_of(","), boost::token_compress_on);

        if (!variants_list.empty()) {
            printer_extruder_id_opt->values.insert(printer_extruder_id_opt->values.end(), variants_list.size(), i + 1);
            printer_extruder_variant_opt->values.insert(printer_extruder_variant_opt->values.end(), variants_list.begin(), variants_list.end());
        }
    }

    // 3. Size the machine limits to the rebuilt variants, padded with their first value like the other variant keys.
    // They are not extruder option keys, so the resize loop in set_num_extruders skips them.
    const auto &defaults = FullPrintConfig::defaults();
    for (const std::string &key : printer_options_with_variant_2)
        if (auto *opt = config.option<ConfigOptionFloats>(key))
            opt->resize(config.get_parameter_size(key, num_extruders), defaults.option(key));
}

void DynamicPrintConfig::set_num_extruders(unsigned int num_extruders)
{
    extend_extruder_variant(*this, num_extruders);

    const auto &defaults = FullPrintConfig::defaults();
    for (const std::string &key : print_config_def.extruder_option_keys()) {
        if (key == "default_filament_profile")
            // Don't resize this field, as it is presented to the user at the "Dependencies" page of the Printer profile and we don't want to present
            // empty fields there, if not defined by the system profile.
            continue;
        auto *opt = this->option(key, false);
        assert(opt != nullptr);
        assert(opt->is_vector());
        if (opt != nullptr && opt->is_vector()) {
            static_cast<ConfigOptionVectorBase*>(opt)->resize(get_parameter_size(key, num_extruders), defaults.option(key));
        }
    }
}

// BBS

void DynamicPrintConfig::set_num_filaments(unsigned int num_filaments)
{
    const auto& defaults = FullPrintConfig::defaults();
    for (const std::string& key : print_config_def.filament_option_keys()) {
        if (key == "default_filament_profile")
            // Don't resize this field, as it is presented to the user at the "Dependencies" page of the Printer profile and we don't want to present
            // empty fields there, if not defined by the system profile.
            continue;
        auto* opt = this->option(key, false);
        assert(opt != nullptr);
        assert(opt->is_vector());
        if (opt != nullptr && opt->is_vector())
            static_cast<ConfigOptionVectorBase*>(opt)->resize(num_filaments, defaults.option(key));
    }
}

//BBS: pass map to recording all invalid valies

std::map<std::string, std::string> DynamicPrintConfig::validate(bool under_cli)
{
    // Full print config is initialized from the defaults.
    const ConfigOption *opt = this->option("printer_technology", false);
    auto printer_technology = (opt == nullptr) ? ptFFF : static_cast<PrinterTechnology>(dynamic_cast<const ConfigOptionEnumGeneric*>(opt)->value);
    switch (printer_technology) {
    case ptFFF:
    {
        FullPrintConfig fpc;
        fpc.apply(*this, true);
        // Verify this print options through the FullPrintConfig.
        return Slic3r::validate(fpc, under_cli);
    }
    default:
        //FIXME no validation on SLA data?
        return std::map<std::string, std::string>();
    }
}

std::string DynamicPrintConfig::get_filament_type(std::string &displayed_filament_type, int id)
{
    auto* filament_id = dynamic_cast<const ConfigOptionStrings*>(this->option("filament_id"));
    auto* filament_type = dynamic_cast<const ConfigOptionStrings*>(this->option("filament_type"));
    auto* filament_is_support = dynamic_cast<const ConfigOptionBools*>(this->option("filament_is_support"));

    // get_at() on an empty vector option is undefined behavior (.front() of an empty vector),
    // and e.g. filament_id is never populated on a CLI from-scratch slice - treat an empty
    // option the same as a missing one.
    if (filament_id && filament_id->values.empty())
        filament_id = nullptr;
    if (filament_is_support && filament_is_support->values.empty())
        filament_is_support = nullptr;

    if (!filament_type || filament_type->values.empty())
        return "";

    if (!filament_is_support) {
        if (filament_type) {
            displayed_filament_type = filament_type->get_at(id);
            return filament_type->get_at(id);
        }
        else {
            displayed_filament_type = "";
            return "";
        }
    }
    else {
        bool is_support = filament_is_support ? filament_is_support->get_at(id) : false;
        if (is_support) {
            if (filament_id) {
                if (filament_id->get_at(id) == "GFS00") {
                    displayed_filament_type = "Sup.PLA";
                    return "PLA-S";
                }
                else if (filament_id->get_at(id) == "GFS01") {
                    displayed_filament_type = "Sup.PA";
                    return "PA-S";
                }
                else {
                    if (filament_type->get_at(id) == "PLA") {
                        displayed_filament_type = "Sup.PLA";
                        return "PLA-S";
                    }
                    else if (filament_type->get_at(id) == "PA") {
                        displayed_filament_type = "Sup.PA";
                        return "PA-S";
                    }
                    else {
                        displayed_filament_type = filament_type->get_at(id);
                        return filament_type->get_at(id);
                    }
                }
            }
            else {
                if (filament_type->get_at(id) == "PLA") {
                    displayed_filament_type = "Sup.PLA";
                    return "PLA-S";
                } else if (filament_type->get_at(id) == "PA") {
                    displayed_filament_type = "Sup.PA";
                    return "PA-S";
                } else {
                    displayed_filament_type = filament_type->get_at(id);
                    return filament_type->get_at(id);
                }
            }
        }
        else {
            displayed_filament_type = filament_type->get_at(id);
            return filament_type->get_at(id);
        }
    }
    return "PLA";
}

bool DynamicPrintConfig::is_using_different_extruders()
{
    bool ret = false;

    auto nozzle_diameters_opt = dynamic_cast<const ConfigOptionFloats*>(this->option("nozzle_diameter"));
    if (nozzle_diameters_opt != nullptr) {
        int size = nozzle_diameters_opt->size();
        if (size > 1) {
            auto extruder_type_opt = dynamic_cast<const ConfigOptionEnumsGeneric*>(this->option("extruder_type"));
            auto nozzle_volume_type_opt = dynamic_cast<const ConfigOptionEnumsGeneric*>(this->option("nozzle_volume_type"));
            if (extruder_type_opt && nozzle_volume_type_opt) {
                ExtruderType extruder_type = (ExtruderType)(extruder_type_opt->get_at(0));
                NozzleVolumeType nozzle_volume_type = (NozzleVolumeType)(nozzle_volume_type_opt->get_at(0));
                for (int index = 1; index < size; index++)
                {
                    ExtruderType extruder_type_1 = (ExtruderType)(extruder_type_opt->get_at(index));
                    NozzleVolumeType nozzle_volume_type_1 = (NozzleVolumeType)(nozzle_volume_type_opt->get_at(index));
                    if ((extruder_type_1 != extruder_type) || (nozzle_volume_type_1 != nozzle_volume_type)) {
                        ret = true;
                        break;
                    }
                }
            }
        }
    }

    return ret;
}

bool DynamicPrintConfig::has_multi_variant_filament() const
{
    auto variants  = dynamic_cast<const ConfigOptionStrings*>(this->option("filament_extruder_variant"));
    auto diameters = dynamic_cast<const ConfigOptionFloats*>(this->option("filament_diameter"));
    return variants && diameters && variants->size() > diameters->size();
}

bool DynamicPrintConfig::support_different_extruders(int& extruder_count) const
{
    std::set<std::string> variant_set;

    auto nozzle_diameters_opt = dynamic_cast<const ConfigOptionFloats*>(this->option("nozzle_diameter"));
    if (nozzle_diameters_opt != nullptr) {
        int size = nozzle_diameters_opt->size();
        extruder_count = size;
        auto extruder_variant_opt = dynamic_cast<const ConfigOptionStrings*>(this->option("extruder_variant_list"));
        if (extruder_variant_opt != nullptr) {
            for (int index = 0; index < size; index++) {
                std::string variant = extruder_variant_opt->get_at(index);
                std::vector<std::string> variants_list;
                boost::split(variants_list, variant, boost::is_any_of(","), boost::token_compress_on);
                if (!variants_list.empty())
                    variant_set.insert(variants_list.begin(), variants_list.end());
            }
        }
    }

    return (variant_set.size() > 1);
}

int DynamicPrintConfig::get_index_for_extruder(int extruder_or_filament_id, std::string id_name, ExtruderType extruder_type, NozzleVolumeType nozzle_volume_type, std::string variant_name, unsigned int stride) const
{
    int ret = -1;

    auto variant_opt = dynamic_cast<const ConfigOptionStrings*>(this->option(variant_name));
    const ConfigOptionInts* id_opt = id_name.empty()?nullptr: dynamic_cast<const ConfigOptionInts*>(this->option(id_name));
    const ConfigOptionStrings* extruder_variant_list_opt = dynamic_cast<const ConfigOptionStrings*>(this->option("extruder_variant_list"));
    auto generated_extruder_id = [extruder_variant_list_opt](int target_index) {
        if (!extruder_variant_list_opt)
            return 0;

        int variant_index = 0;
        for (int extruder_index = 0; extruder_index < int(extruder_variant_list_opt->values.size()); ++extruder_index) {
            std::vector<std::string> variants_list;
            boost::split(variants_list, extruder_variant_list_opt->get_at(extruder_index), boost::is_any_of(","), boost::token_compress_on);
            for (std::string variant : variants_list) {
                boost::trim(variant);
                if (variant.empty())
                    continue;
                if (variant_index == target_index)
                    return extruder_index + 1;
                ++variant_index;
            }
        }
        return 0;
    };

    if (variant_opt != nullptr) {
        int v_size = variant_opt->values.size();
        const bool has_complete_id_map = id_opt && int(id_opt->values.size()) >= v_size;
        // nvtHybrid not supported in presets, switch to nvtStandard to match the preset values
        if (nozzle_volume_type == nvtHybrid)
            nozzle_volume_type = nvtStandard;
        std::string extruder_variant = get_extruder_variant_string(extruder_type, nozzle_volume_type);
        for (int index = 0; index < v_size; index++)
        {
            const std::string variant = variant_opt->get_at(index);
            if (extruder_variant == variant) {
                if (id_opt) {
                    const int id = has_complete_id_map ? id_opt->get_at(index) : generated_extruder_id(index);
                    if (id == extruder_or_filament_id) {
                        ret = index * stride;
                        break;
                    }
                }
                else {
                    ret = index * stride;
                    break;
                }

            }
        }
    }
    return ret;
}

//only used for cli
//update values in single extruder process config to values in multi-extruder process
//limit the new values

int DynamicPrintConfig::update_values_from_single_to_multi(DynamicPrintConfig& multi_config, std::set<std::string>& key_set, std::string id_name, std::string variant_name)
{
    auto print_variant_opt = dynamic_cast<const ConfigOptionStrings*>(multi_config.option(variant_name));
    if (!print_variant_opt) {
        BOOST_LOG_TRIVIAL(error) << boost::format("%1%:%2%, can not get %3% from config")%__FUNCTION__ %__LINE__ % variant_name;
        return -1;
    }
    int variant_count = print_variant_opt->size();

    const ConfigDef  *config_def     = this->def();
    if (!config_def) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: can not find config define")%__LINE__;
        return -1;
    }
    for (auto& key: key_set)
    {
        const ConfigOptionDef *optdef  = config_def->get(key);
        if (!optdef) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: can not find opt define for %2%")%__LINE__%key;
            continue;
        }
        switch (optdef->type) {
            case coStrings:
            {
                ConfigOptionStrings* src_opt = multi_config.option<ConfigOptionStrings>(key);
                if (src_opt) {
                    ConfigOptionStrings* opt = this->option<ConfigOptionStrings>(key, true);

                    opt->values = src_opt->values;
                }
                break;
            }
            case coInts:
            {
                ConfigOptionInts* src_opt = multi_config.option<ConfigOptionInts>(key);
                if (src_opt) {
                    ConfigOptionInts* opt = this->option<ConfigOptionInts>(key, true);

                    opt->values = src_opt->values;
                }
                break;
            }
            case coFloats:
            {
                ConfigOptionFloats * src_opt = multi_config.option<ConfigOptionFloats>(key);
                if (src_opt) {
                    ConfigOptionFloats * opt = this->option<ConfigOptionFloats>(key, true);

                    assert(variant_count == src_opt->size());
                    opt->resize(variant_count, opt);

                    for (int index = 0; index < variant_count; index++)
                    {
                        //variant_count is the variant column width, src_opt the value array;
                        //they disagree when the source was authored at a different width
                        if (index >= (int)src_opt->values.size())
                            break;
                        if (opt->values[index] > src_opt->values[index])
                            opt->values[index] = src_opt->values[index];
                    }
                }
                break;
            }
            case coFloatsOrPercents:
            {
                ConfigOptionFloatsOrPercents * src_opt = multi_config.option<ConfigOptionFloatsOrPercents>(key);
                if (src_opt) {
                    ConfigOptionFloatsOrPercents * opt = this->option<ConfigOptionFloatsOrPercents>(key, true);

                    assert(variant_count == src_opt->size());
                    opt->resize(variant_count, opt);

                    for (int index = 0; index < variant_count; index++)
                    {
                        if (index >= (int)src_opt->values.size())
                            break;
                        if (opt->values[index].value > src_opt->values[index].value)
                            opt->values[index] = src_opt->values[index];
                    }
                }
                break;
            }
            case coBools:
            {
                ConfigOptionBools * src_opt = multi_config.option<ConfigOptionBools>(key);
                if (src_opt)
                {
                    ConfigOptionBools * opt = this->option<ConfigOptionBools>(key, true);

                    assert(variant_count == src_opt->size());
                    opt->resize(variant_count, opt);
                }

                break;
            }
            default:
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: unsupported option type for %2%")%__LINE__%key;
                break;
        }
    }

    return 0;
}

//used for object/region config
//duplicate single to multiple
/*int DynamicPrintConfig::update_values_from_single_to_multi_2(DynamicPrintConfig& multi_config, std::set<std::string>& key_set)
{
    const ConfigDef  *config_def     = this->def();
    if (!config_def) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: can not find config define")%__LINE__;
        return -1;
    }

    t_config_option_keys keys = this->keys();
    for (auto& key: keys)
    {
        if (key_set.find(key) == key_set.end())
            continue;

        const ConfigOptionDef *optdef  = config_def->get(key);
        if (!optdef) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: can not find opt define for %2%")%__LINE__%key;
            continue;
        }
        switch (optdef->type) {
            case coFloats:
            {
                ConfigOptionFloatsNullable * opt = this->option<ConfigOptionFloatsNullable>(key);
                ConfigOptionFloatsNullable* src_opt = multi_config.option<ConfigOptionFloatsNullable>(key);

                if (src_opt && !opt->is_nil(0))
                    opt->values.resize(src_opt->size(), opt->values[0]);
                break;
            }
            case coFloatsOrPercents:
            {
                ConfigOptionFloatsOrPercentsNullable* opt = this->option<ConfigOptionFloatsOrPercentsNullable>(key);
                ConfigOptionFloatsOrPercentsNullable* src_opt = multi_config.option<ConfigOptionFloatsOrPercentsNullable>(key);

                if (src_opt &&!opt->is_nil(0))
                    opt->values.resize(src_opt->size(), opt->values[0]);
                break;
            }
            case coBools:
            {
                ConfigOptionBoolsNullable* opt = this->option<ConfigOptionBoolsNullable>(key);
                ConfigOptionBoolsNullable* src_opt = multi_config.option<ConfigOptionBoolsNullable>(key);

                if (src_opt &&!opt->is_nil(0))
                    opt->values.resize(src_opt->size(), opt->values[0]);

                break;
            }
            default:
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: unsupported option type for %2%")%__LINE__%key;
                break;
        }
    }

    return 0;
}*/

//update global process config for multi variant to multi variant case
//1. skip the key-values not in key_set
//2. update the key-value to the new one, then check whether the old one with the same variant can be used or not

int DynamicPrintConfig::update_values_from_multi_to_multi(DynamicPrintConfig& new_config, std::set<std::string>& key_set, std::string id_name, std::string variant_name, std::vector<std::string>& new_extruder_variants)
{
    int new_extruder_count = new_extruder_variants.size();
    std::vector<int> new_variant_indices(new_extruder_count, -1);

    auto print_variant_opt = dynamic_cast<const ConfigOptionStrings*>(this->option(variant_name));
    auto new_variant_opt = dynamic_cast<const ConfigOptionStrings*>(new_config.option(variant_name));
    auto new_print_id_opt = dynamic_cast<const ConfigOptionInts*>(new_config.option(id_name));
    if (!print_variant_opt || !new_variant_opt || !new_print_id_opt) {
        BOOST_LOG_TRIVIAL(error) << boost::format("%1%:%2%, can not get variant %3%, id %4% from config")%__FUNCTION__ %__LINE__ % variant_name  % id_name;
        return -1;
    }
    int variant_count = print_variant_opt->size(), new_variant_count = new_variant_opt->size();

    std::vector<std::vector<int>> extruder_variant_indices;
    for (int i = 0; i < new_extruder_count; i++)
    {
        std::vector<int> variant_indices;
        for (int j = 0; j < variant_count; j++)
        {
            if (new_extruder_variants[i] == print_variant_opt->values[j]) {
                variant_indices.push_back(j);
            }
        }

        if (variant_indices.empty())
        {
            //can not find any
            variant_indices.resize(variant_count, 0);
            for (int j = 0; j < variant_count; j++)
                variant_indices[j] = j;
        }
        extruder_variant_indices.emplace_back(variant_indices);
    }

    for (int i = 0; i < new_extruder_count; i++)
    {
        for (int j = 0; j < new_variant_count; j++)
        {
            if ((i+1 == new_print_id_opt->values[j]) && (new_extruder_variants[i] == new_variant_opt->values[j])) {
                new_variant_indices[i] = j;
                break;
            }
        }
    }

    const ConfigDef* config_def = this->def();
    if (!config_def) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: can not find config define") % __LINE__;
        return -1;
    }
    for (auto& key : key_set)
    {
        const ConfigOptionDef* optdef = config_def->get(key);
        if (!optdef) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: can not find opt define for %2%") % __LINE__ % key;
            continue;
        }
        switch (optdef->type) {
        case coStrings:
        {
            ConfigOptionStrings* src_opt = new_config.option<ConfigOptionStrings>(key);
            if (src_opt) {
                ConfigOptionStrings* opt = this->option<ConfigOptionStrings>(key, true);

                //assert(variant_count == opt->size());
                opt->values = src_opt->values;
            }
            break;
        }
        case coInts:
        {
            ConfigOptionInts* src_opt = new_config.option<ConfigOptionInts>(key);
            if (src_opt) {
                ConfigOptionInts* opt = this->option<ConfigOptionInts>(key, true);

                //assert(variant_count == opt->size());
                opt->values = src_opt->values;
            }
            break;
        }
        case coFloats:
        {
            ConfigOptionFloats* src_opt = new_config.option<ConfigOptionFloats>(key);
            if (src_opt) {
                ConfigOptionFloats* opt = this->option<ConfigOptionFloats>(key, true);

                std::vector<double> old_values = opt->values;
                int old_count = old_values.size();
                int new_count = src_opt->values.size();

                assert(variant_count == old_count);
                assert(new_variant_count == new_count);
                opt->values = src_opt->values;

                for (int i = 0; i < new_extruder_count; i++)
                {
                    std::vector<int>& variant_indices = extruder_variant_indices[i];
                    int new_variant_index = new_variant_indices[i];
                    if ((new_variant_index == -1) || variant_indices.empty())
                        continue;

                    for(auto idx : variant_indices){
                        assert(idx < old_count);
                        //the counts come from the variant columns, the arrays from the options;
                        //they disagree when a config was authored at a different variant width
                        if (idx >= old_count || new_variant_index >= (int)opt->values.size())
                            continue;
                        if (old_values[idx] < opt->values[new_variant_index])
                            opt->values[new_variant_index] = old_values[idx];
                    }
                }
            }
            break;
        }
        case coFloatsOrPercents:
        {
            ConfigOptionFloatsOrPercents* src_opt = new_config.option<ConfigOptionFloatsOrPercents>(key);
            if (src_opt) {
                ConfigOptionFloatsOrPercents* opt = this->option<ConfigOptionFloatsOrPercents>(key, true);

                std::vector<FloatOrPercent> old_values = opt->values;
                int old_count = old_values.size();
                int new_count = src_opt->values.size();

                assert(variant_count == old_count);
                assert(new_variant_count == new_count);
                opt->values = src_opt->values;

                for (int i = 0; i < new_extruder_count; i++)
                {
                    std::vector<int>& variant_indices = extruder_variant_indices[i];
                    int new_variant_index = new_variant_indices[i];
                    if ((new_variant_index == -1) || variant_indices.empty())
                        continue;

                    for(auto idx : variant_indices){
                        assert(idx < old_count);
                        //the counts come from the variant columns, the arrays from the options;
                        //they disagree when a config was authored at a different variant width
                        if (idx >= old_count || new_variant_index >= (int)opt->values.size())
                            continue;
                        if (old_values[idx] < opt->values[new_variant_index])
                            opt->values[new_variant_index] = old_values[idx];
                    }
                }
            }
            break;
        }
        case coBools:
        {
            ConfigOptionBools* src_opt = new_config.option<ConfigOptionBools>(key);
            if (src_opt) {
                ConfigOptionBools* opt = this->option<ConfigOptionBools>(key, true);

                std::vector<unsigned char> old_values = opt->values;
                int old_count = old_values.size();
                int new_count = src_opt->values.size();

                assert(variant_count == old_count);
                assert(new_variant_count == new_count);
                opt->values = src_opt->values;

                for (int i = 0; i < new_extruder_count; i++)
                {
                    std::vector<int>& variant_indices = extruder_variant_indices[i];
                    int new_variant_index = new_variant_indices[i];
                    if ((new_variant_index == -1) || variant_indices.empty())
                        continue;

                    for(auto idx : variant_indices){
                        assert(idx < old_count);
                        if (idx >= old_count || new_variant_index >= (int)opt->values.size())
                            continue;
                        if (old_values[idx]) //enabled
                            opt->values[new_variant_index] = old_values[idx];
                    }
                }
            }

            break;
        }
        default:
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: unsupported option type for %2%") % __LINE__ % key;
            break;
        }
    }

    return 0;
}

int DynamicPrintConfig::update_values_from_multi_to_multi_2(const std::vector<std::string>& src_extruder_variants, const std::vector<std::string>& dst_extruder_variants, const DynamicPrintConfig& dst_config, const std::set<std::string>& key_sets)
{
    const ConfigDef  *config_def     = this->def();
    if (!config_def) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: can not find config define")%__LINE__;
        return -1;
    }

    auto get_same_variant_indices = [](const std::vector<std::string>& extruder_variants, const std::string& variant){
        std::vector<int> indices;
        for(int i=0;i<extruder_variants.size();++i)
            if(extruder_variants[i] == variant)
                indices.push_back(i);
        return indices;
    };

    std::vector<std::vector<int>> same_variant_indices;
    for(size_t dst_idx =0 ;dst_idx < dst_extruder_variants.size(); ++dst_idx){
        auto& dst_variant = dst_extruder_variants[dst_idx];
        auto indices =get_same_variant_indices(src_extruder_variants, dst_variant);
        same_variant_indices.emplace_back(indices);
    }

    //dst_values below is the destination PRINT preset's per-variant row, sized to its own
    //print_extruder_variant; dst_extruder_variants is the PRINTER's list. They disagree until
    //the print preset is re-selected, so size the row to the variant count before indexing it.
    const size_t dst_variant_count = dst_extruder_variants.size();
    if (dst_variant_count == 0) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: empty destination variant list")%__LINE__;
        return -1;
    }

    t_config_option_keys keys = this->keys();
    for(auto& key : keys){
        if(key_sets.find(key) == key_sets.end())
            continue;
        const ConfigOptionDef* optdef = config_def->get(key);
        if(!optdef){
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: can not find opt define for %2%")%__LINE__%key;
            continue;
        }

        switch (optdef->type){
            case coFloats:
            {
                ConfigOptionFloatsNullable* opt = this->option<ConfigOptionFloatsNullable>(key);
                auto src_values = opt->values;
                const auto* dst_opt = dst_config.option<ConfigOptionFloatsNullable>(key);
                if(!dst_opt){
                    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: %2% missing from destination config")%__LINE__%key;
                    break;
                }
                auto dst_values = dst_opt->values;
                dst_values.resize(dst_variant_count, ConfigOptionFloatsNullable::nil_value());
                for(size_t dst_idx =0; dst_idx < same_variant_indices.size(); ++dst_idx){
                    auto& indices = same_variant_indices[dst_idx];
                    if(indices.empty())
                        continue;
                    bool has_value = false;
                    double target_value = std::numeric_limits<double>::max();
                    for(auto idx : indices){
                        if(idx < (int)opt->values.size() && !opt->is_nil(idx)){
                            has_value = true;
                            target_value = std::min(target_value, src_values[idx]);
                        }
                    }

                    if(has_value)
                        dst_values[dst_idx] = target_value;
                }
                opt->values = dst_values;
                break;
            }
            case coFloatsOrPercents:
            {
                ConfigOptionFloatsOrPercentsNullable* opt = this->option<ConfigOptionFloatsOrPercentsNullable>(key);
                auto src_values = opt->values;
                const auto* dst_opt = dst_config.option<ConfigOptionFloatsOrPercentsNullable>(key);
                if(!dst_opt){
                    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: %2% missing from destination config")%__LINE__%key;
                    break;
                }
                auto dst_values = dst_opt->values;
                dst_values.resize(dst_variant_count, ConfigOptionFloatsOrPercentsNullable::nil_value());
                for(size_t dst_idx =0; dst_idx < same_variant_indices.size(); ++dst_idx){
                    auto& indices = same_variant_indices[dst_idx];
                    if(indices.empty())
                        continue;
                    bool has_value = false;
                    FloatOrPercent target_value{9999.f, true};
                    for(auto idx : indices){
                        if(idx < (int)opt->values.size() && !opt->is_nil(idx)){
                            has_value = true;
                            target_value = src_values[idx].value < target_value.value ? src_values[idx] : target_value;
                        }
                    }

                    if(has_value)
                        dst_values[dst_idx] = target_value;
                }
                opt->values = dst_values;
                break;
            }
            case coBools:
            {
                ConfigOptionBoolsNullable* opt = this->option<ConfigOptionBoolsNullable>(key);
                auto src_values = opt->values;
                const auto* dst_opt = dst_config.option<ConfigOptionBoolsNullable>(key);
                if(!dst_opt){
                    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: %2% missing from destination config")%__LINE__%key;
                    break;
                }
                auto dst_values = dst_opt->values;
                dst_values.resize(dst_variant_count, ConfigOptionBoolsNullable::nil_value());
                for(size_t dst_idx =0; dst_idx < same_variant_indices.size(); ++dst_idx){
                    auto indices = same_variant_indices[dst_idx];
                    if(indices.empty())
                        continue;
                    bool has_value = false;
                    bool target_value = false;
                    for(auto idx : indices){
                        if(idx < (int)opt->values.size() && !opt->is_nil(idx)){
                            has_value = true;
                            target_value = src_values[idx];
                            break;
                        }
                    }

                    if(has_value)
                        dst_values[dst_idx] = target_value;
                }

                opt->values = dst_values;
                break;
            }
            default:
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: unsupported option type for %2%")%__LINE__%key;
                break;
        }

    }

    return 0;

}

void set_variant_override(ConfigOptionVectorBase &target, const ConfigOptionVectorBase &source,
                          const std::vector<int> &variant_index, int stride)
{
    // A single-value object or region override applies to every nozzle variant.
    std::vector<int> indices = variant_index;
    if (source.size() == 1 && !source.is_nil(0))
        std::fill(indices.begin(), indices.end(), 0);
    target.set_to_index(&source, indices, stride);
}

void normalize_filament_values_to_variants(DynamicPrintConfig &config)
{
    const auto *self_index = config.option<ConfigOptionInts>("filament_self_index");
    if (self_index == nullptr || self_index->empty())
        return;
    const int filament_count = *std::max_element(self_index->values.begin(), self_index->values.end());
    if (filament_count <= 0 || size_t(filament_count) >= self_index->size())
        return;
    // The values are one per filament, without variant strings, or a single value for all of them. The
    // variant strings do not change today's mapping; they are passed so a rule that reads them applies here too.
    const auto *variants = config.option<ConfigOptionStrings>("filament_extruder_variant");
    const std::vector<std::string> variant_list = variants && variants->size() == self_index->size() ? variants->values : std::vector<std::string>();
    std::vector<int> filament_ids(filament_count);
    std::iota(filament_ids.begin(), filament_ids.end(), 1);
    const std::vector<int> from_filaments = map_variant_indices(variant_list, self_index->values, {}, filament_ids);
    const std::vector<int> from_single    = map_variant_indices(variant_list, self_index->values, {}, {});
    for (const std::string &key : filament_options_with_variant) {
        auto *opt = dynamic_cast<ConfigOptionVectorBase *>(config.option(key));
        if (opt == nullptr || (opt->size() != size_t(filament_count) && opt->size() != 1))
            continue;
        const std::vector<int> &variant_index = opt->size() == size_t(filament_count) ? from_filaments : from_single;
        std::unique_ptr<ConfigOption> source(opt->clone());
        // -1 and a single-value source both resolve to the first value through get_at()
        for (size_t variant = 0; variant < self_index->size(); ++variant)
            opt->set_at(source.get(), variant, variant_index[variant]);
    }
}

void set_filament_dev_options(DynamicPrintConfig &config, const std::vector<const DynamicPrintConfig *> &filament_configs)
{
    for (const std::string &key : filament_dev_options) {
        if (std::none_of(filament_configs.begin(), filament_configs.end(), [&key](const DynamicPrintConfig *filament) { return filament->has(key); }))
            continue;
        const ConfigOption *default_value = print_config_def.get(key)->default_value.get();
        auto *dst = static_cast<ConfigOptionVectorBase *>(config.option(key, true));
        dst->clear();
        for (const DynamicPrintConfig *filament : filament_configs) {
            const auto *src = static_cast<const ConfigOptionVectorBase *>(filament->has(key) ? filament->option(key) : default_value);
            if (!src->empty())
                dst->append(src);
        }
    }
}

void resize_mixed_filament_metadata(DynamicPrintConfig &config, size_t old_slot_count, size_t new_slot_count)
{
    auto resize = [old_slot_count, new_slot_count](auto *opt) {
        opt->values.resize(std::min(old_slot_count, opt->values.size()));
        opt->values.resize(new_slot_count);
    };
    resize(config.option<ConfigOptionBools>("filament_is_mixed", true));
    resize(config.option<ConfigOptionStrings>("filament_mixed_components", true));
    resize(config.option<ConfigOptionStrings>("filament_mixed_sublayer_ratios", true));
    resize(config.option<ConfigOptionBools>("filament_mixed_gradient", true));
    resize(config.option<ConfigOptionStrings>("filament_mixed_gradient_range", true));
    resize(config.option<ConfigOptionStrings>("filament_mixed_gradient_curve", true));
    resize(config.option<ConfigOptionBools>("filament_mixed_gradient_per_part", true));
}


//used for object/region config
//use the smallest of multiple to single
/*int DynamicPrintConfig::update_values_from_multi_to_single_2(std::set<std::string>& key_set)
{
    const ConfigDef  *config_def     = this->def();
    if (!config_def) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: can not find config define")%__LINE__;
        return -1;
    }

    t_config_option_keys keys = this->keys();
    for (auto& key: keys)
    {
        if (key_set.find(key) == key_set.end())
            continue;

        const ConfigOptionDef *optdef  = config_def->get(key);
        if (!optdef) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: can not find opt define for %2%")%__LINE__%key;
            continue;
        }
        switch (optdef->type) {
            case coFloats:
            {
                ConfigOptionFloatsNullable* opt = this->option<ConfigOptionFloatsNullable>(key);
                double min = 9999.0;
                bool has_value = false;

                for (int index = 0; index < opt->values.size(); index++)
                {
                    if (!opt->is_nil(index) && (opt->values[index] < min)) {
                        min = opt->values[index];
                        has_value = true;
                    }
                }

                opt->values.erase(opt->values.begin() + 1, opt->values.end());
                if (has_value)
                    opt->values[0] = min;
                break;
            }
            case coFloatsOrPercents:
            {
                ConfigOptionFloatsOrPercentsNullable * opt = this->option<ConfigOptionFloatsOrPercentsNullable>(key);
                FloatOrPercent min{9999.f, true};
                bool has_value = false;

                for (int index = 0; index < opt->values.size(); index++)
                {
                    if (!opt->is_nil(index) && (opt->values[index].value < min.value)) {
                        min = opt->values[index];
                        has_value = true;
                    }
                }

                opt->values.erase(opt->values.begin() + 1, opt->values.end());
                if (has_value)
                    opt->values[0] = min;
                break;
            }
            case coBools:
            {
                ConfigOptionBoolsNullable* opt = this->option<ConfigOptionBoolsNullable>(key);

                bool min, has_value = false;
                for (int index = 0; index < opt->values.size(); index++)
                {
                    if (!opt->is_nil(index)) {
                        min = opt->values[index];
                        has_value = true;
                        break;
                    }
                }

                opt->values.erase(opt->values.begin() + 1, opt->values.end());
                if (has_value)
                    opt->values[0] = min;
                break;
            }
            default:
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: unsupported option type for %2%")%__LINE__%key;
                break;
        }
    }

    return 0;
}*/

std::string

DynamicPrintConfig::get_filament_vendor() const
{
    const ConfigOptionStrings* opt = dynamic_cast<const ConfigOptionStrings*> (option("filament_vendor"));
    if (opt && !opt->values.empty())
    {
        return opt->values[0];
    }

    return std::string();
}


std::string

DynamicPrintConfig::get_filament_type() const
{
    const ConfigOptionStrings* opt = dynamic_cast<const ConfigOptionStrings*> (option("filament_type"));
    if (opt && !opt->values.empty())
    {
        return opt->values[0];
    }

    return std::string();
}

int DynamicPrintConfig::get_extruder_nozzle_volume_count(int extruder_count, std::vector<std::vector<NozzleVolumeType>>& nozzle_volume_types) const
{
    int count = extruder_count;
    auto opt_extruder_nozzle_stats = dynamic_cast<const ConfigOptionStrings*>(this->option("extruder_nozzle_stats"));
    nozzle_volume_types.resize(extruder_count, std::vector<NozzleVolumeType>{});
    if (opt_extruder_nozzle_stats && (int(opt_extruder_nozzle_stats->values.size()) == extruder_count)) {
        std::vector<std::map<NozzleVolumeType, int>> extruder_nozzle_counts = get_extruder_nozzle_stats(opt_extruder_nozzle_stats->values);
        count = 0;
        for (int i = 0; i < extruder_count; i++)
        {
            count += extruder_nozzle_counts[i].size();
            // std::map iteration order gives the canonical slot order: ascending NozzleVolumeType
            for (auto& iter : extruder_nozzle_counts[i])
                nozzle_volume_types[i].push_back(iter.first);
        }
    }
    return count;
}

// Orca: BBL system profiles ship full-width print_extruder_id/print_extruder_variant columns, but
// custom multi-extruder printers only ever get the machine-scope columns synthesized for them (see
// extend_extruder_variant); the process scope keeps the length-1 defaults, both in presets and in
// 3mf project configs. Expanding with that degenerate map makes every per-extruder lookup fail, and
// because both keys are themselves in print_options_with_variant, the expansion then latches a
// full-width-but-wrong [1,1,...] map that also defeats the generated_extruder_id fallback in
// get_index_for_extruder. Synthesize the process columns from the printer's extruder_variant_list
// (same token walk as extend_extruder_variant) before expanding.
static void ensure_process_variant_columns(DynamicPrintConfig &config, const DynamicPrintConfig &printer_config)
{
    auto id_opt      = dynamic_cast<ConfigOptionInts *>(config.option("print_extruder_id"));
    auto variant_opt = dynamic_cast<ConfigOptionStrings *>(config.option("print_extruder_variant"));
    auto list_opt    = dynamic_cast<const ConfigOptionStrings *>(printer_config.option("extruder_variant_list"));
    if (!id_opt || !variant_opt || !list_opt)
        return;
    if (id_opt->values.size() != 1 || variant_opt->values.size() != 1)
        return;

    std::vector<int>         ids;
    std::vector<std::string> variants;
    for (int i = 0; i < int(list_opt->values.size()); ++i) {
        std::vector<std::string> tokens;
        boost::split(tokens, list_opt->get_at(i), boost::is_any_of(","), boost::token_compress_on);
        for (std::string &token : tokens) {
            boost::trim(token);
            if (token.empty())
                continue;
            ids.push_back(i + 1);
            variants.push_back(token);
        }
    }
    // A single column is the legitimate single-extruder layout, not a degenerate one.
    if (ids.size() <= 1)
        return;
    id_opt->values      = std::move(ids);
    variant_opt->values = std::move(variants);
}

std::vector<int> DynamicPrintConfig::update_values_to_printer_extruders(DynamicPrintConfig& printer_config, int extruder_count, int extruder_nozzle_volume_count, std::vector<std::vector<NozzleVolumeType>>& nv_types,
    std::set<std::string>& key_set, std::string id_name, std::string variant_name, unsigned int stride, unsigned int extruder_id, NozzleVolumeType filament_nvt)
{
    std::vector<int> variant_index;
    int variant_count = extruder_count;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: extruder_count %2%, extruder_nozzle_volume_count %3%")%__LINE__ %extruder_count %extruder_nozzle_volume_count;

    auto opt_extruder_type = dynamic_cast<const ConfigOptionEnumsGeneric*>(printer_config.option("extruder_type"));
    auto opt_nozzle_volume_type = dynamic_cast<const ConfigOptionEnumsGeneric*>(printer_config.option("nozzle_volume_type"));
    if (!opt_extruder_type || !opt_nozzle_volume_type) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: extruder_type or nozzle_volume_type option not found, skipping")%__LINE__;
        return variant_index;
    }

    if (extruder_id > 0 && extruder_id <= static_cast<unsigned> (extruder_count)) {
        variant_index.resize(1);
        ExtruderType extruder_type = (ExtruderType)(opt_extruder_type->get_at(extruder_id - 1));
        NozzleVolumeType nozzle_volume_type = (NozzleVolumeType)(opt_nozzle_volume_type->get_at(extruder_id - 1));

        if (nozzle_volume_type == nvtHybrid) {
            // a mixed-nozzle extruder has no preset column of its own: the filament's concrete
            // volume type selects the slot
            nozzle_volume_type = filament_nvt;
        }
        else if (nozzle_volume_type != filament_nvt) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                                    << boost::format(", Line %1%: nozzle_volume_type is %2%,  not equal to filament_nvt %3%") % __LINE__ % nozzle_volume_type % filament_nvt;
        }

        //variant index
        variant_index[0] = get_index_for_extruder(extruder_id, id_name, extruder_type, nozzle_volume_type, variant_name);

        if (variant_index[0] < 0) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: could not found extruder_type %2%, nozzle_volume_type %3%, for filament")
                % __LINE__ % s_keys_names_ExtruderType[extruder_type] % s_keys_names_NozzleVolumeType[nozzle_volume_type];
        }

        variant_count = 1;
    }
    else {
        if (id_name == "print_extruder_id")
            ensure_process_variant_columns(*this, printer_config);
        // Orca: emit the slots first, then size variant_count from what was actually
        // emitted. extruder_nozzle_volume_count only equals the emitted total when every
        // extruder carries per-type stats; an extruder with an empty stats entry combined
        // with a Hybrid extruder would otherwise overrun a table pre-sized from that count.
        for (int e_index = 0; e_index < extruder_count; e_index++)
        {
            ExtruderType extruder_type = (ExtruderType)(opt_extruder_type->get_at(e_index));
            NozzleVolumeType nozzle_volume_type = (NozzleVolumeType)(opt_nozzle_volume_type->get_at(e_index));

            // Orca: latch the decision before the inner loop reassigns nozzle_volume_type;
            // re-testing the reassigned value would make every slot after the first reuse
            // the first slot's volume type.
            const bool per_type_slots = extruder_nozzle_volume_count > extruder_count || nozzle_volume_type == nvtHybrid;
            const int nvt_count = per_type_slots ? int(nv_types[e_index].size()) : 1;
            for (int nvt_index = 0; nvt_index < nvt_count; nvt_index++)
            {
                if (per_type_slots)
                    nozzle_volume_type = nv_types[e_index][nvt_index];
                //variant index
                int slot_index = get_index_for_extruder(e_index+1, id_name, extruder_type, nozzle_volume_type, variant_name);
                if (slot_index < 0) {
                    // Orca: This is expected during transient UI states (e.g. popup windows),
                    // fall back to 0 silently.
                    slot_index = 0;
                }
                variant_index.push_back(slot_index);
            }
        }
        variant_count = int(variant_index.size());
    }

    const ConfigDef       *config_def     = this->def();
    if (!config_def) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: can not find config define")%__LINE__;
        return variant_index;
    }
    for (auto& key: key_set)
    {
        const ConfigOptionDef *optdef  = config_def->get(key);
        if (!optdef) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: can not find opt define for %2%")%__LINE__%key;
            continue;
        }
        switch (optdef->type) {
            case coStrings:
            {
                ConfigOptionStrings * opt = this->option<ConfigOptionStrings>(key);
                if (!opt) continue;
                std::vector<std::string> new_values;

                new_values.resize(variant_count * stride);
                for (int e_index = 0; e_index < variant_count; e_index++)
                {
                    for (unsigned int i = 0; i < stride; i++)
                        new_values[e_index*stride + i] = opt->get_at(variant_index[e_index]*stride + i);
                }
                opt->values = new_values;
                break;
            }
            case coInts:
            {
                ConfigOptionInts * opt = this->option<ConfigOptionInts>(key);
                if (!opt) continue;
                std::vector<int> new_values;

                new_values.resize(variant_count * stride);
                for (int e_index = 0; e_index < variant_count; e_index++)
                {
                    for (unsigned int i = 0; i < stride; i++)
                        new_values[e_index*stride + i] = opt->get_at(variant_index[e_index]*stride + i);
                }
                opt->values = new_values;
                break;
            }
            case coFloats:
            {
                ConfigOptionFloats * opt = this->option<ConfigOptionFloats>(key);
                if (!opt) continue;
                std::vector<double> new_values;

                new_values.resize(variant_count * stride);
                for (int e_index = 0; e_index < variant_count; e_index++)
                {
                    for (unsigned int i = 0; i < stride; i++)
                        new_values[e_index*stride + i] = opt->get_at(variant_index[e_index]*stride + i);
                }
                opt->values = new_values;
                break;
            }
            case coPercents:
            {
                ConfigOptionPercents * opt = this->option<ConfigOptionPercents>(key);
                if (!opt) continue;
                std::vector<double> new_values;

                new_values.resize(variant_count * stride);
                for (int e_index = 0; e_index < variant_count; e_index++)
                {
                    for (unsigned int i = 0; i < stride; i++)
                        new_values[e_index*stride + i] = opt->get_at(variant_index[e_index]*stride + i);
                }
                opt->values = new_values;
                break;
            }
            case coFloatsOrPercents:
            {
                ConfigOptionFloatsOrPercents * opt = this->option<ConfigOptionFloatsOrPercents>(key);
                if (!opt) continue;
                std::vector<FloatOrPercent> new_values;

                new_values.resize(variant_count * stride);
                for (int e_index = 0; e_index < variant_count; e_index++)
                {
                    for (unsigned int i = 0; i < stride; i++)
                        new_values[e_index*stride + i] = opt->get_at(variant_index[e_index]*stride + i);
                }
                opt->values = new_values;
                break;
            }
            case coBools:
            {
                ConfigOptionBools * opt = this->option<ConfigOptionBools>(key);
                if (!opt) continue;
                std::vector<unsigned char> new_values;

                new_values.resize(variant_count * stride);
                for (int e_index = 0; e_index < variant_count; e_index++)
                {
                    for (unsigned int i = 0; i < stride; i++)
                        new_values[e_index*stride + i] = opt->get_at(variant_index[e_index]*stride + i);
                }
                opt->values = new_values;
                break;
            }
            case coEnums:
            {
                ConfigOptionEnumsGeneric * opt = this->option<ConfigOptionEnumsGeneric>(key);
                if (!opt) continue;
                std::vector<int> new_values;

                new_values.resize(variant_count * stride);
                for (int e_index = 0; e_index < variant_count; e_index++)
                {
                    for (unsigned int i = 0; i < stride; i++)
                        new_values[e_index*stride + i] = opt->get_at(variant_index[e_index]*stride + i);
                }
                opt->values = new_values;
                break;
            }
            default:
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: unsupported option type for %2%")%__LINE__%key;
                break;
        }
    }

    return variant_index;
}

// Regathers a vector option's values through per-slot source indices (one input index per
// output slot). Out-of-range indices keep the first value, matching get_at's fallback.
template<typename OptType, typename ValueType>
static void gather_option_values(const char *caller, const std::string &key, OptType *opt, const std::vector<int> &slot_param_indices)
{
    if (!opt || opt->values.empty()) {
        BOOST_LOG_TRIVIAL(warning) << caller << boost::format(", Line %1%: option %2% not found or empty, skipping")%__LINE__%key;
        return;
    }
    std::vector<ValueType> new_values;
    new_values.reserve(slot_param_indices.size());
    for (int idx : slot_param_indices) {
        if (idx < 0 || static_cast<size_t>(idx) >= opt->values.size()) {
            BOOST_LOG_TRIVIAL(warning) << caller << boost::format(", Line %1%: option %2% slot index %3% out of range, keeping first value")%__LINE__%key%idx;
            new_values.emplace_back(opt->values.front());
        }
        else
            new_values.emplace_back(opt->values[idx]);
    }
    opt->values = std::move(new_values);
}

void DynamicPrintConfig::update_values_to_printer_extruders_for_multiple_filaments(DynamicPrintConfig& printer_config, int extruder_count, int extruder_nozzle_volume_count, std::set<std::string>& key_set, std::string id_name, std::string variant_name)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: extruder_count %2%, extruder_nozzle_volume_count %3%")%__LINE__ %extruder_count %extruder_nozzle_volume_count;

    {
        auto opt_filament_map = printer_config.option<ConfigOptionInts>("filament_map");
        if (!opt_filament_map) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: filament_map option not found, skipping")%__LINE__;
            return;
        }
        std::vector<int> filament_maps = opt_filament_map->values;
        auto opt_ids = id_name.empty()? nullptr: dynamic_cast<const ConfigOptionInts*>(this->option(id_name));
        // Orca: a map shorter than the filament count must not drop the filaments past its end;
        // they take the first extruder.
        if (opt_ids && !opt_ids->values.empty())
            filament_maps.resize(std::max<size_t>(filament_maps.size(), *std::max_element(opt_ids->values.begin(), opt_ids->values.end())), 1);
        size_t filament_count = filament_maps.size();
        //apply process settings
        auto opt_extruder_type = dynamic_cast<const ConfigOptionEnumsGeneric*>(printer_config.option("extruder_type"));
        auto opt_nozzle_volume_type = dynamic_cast<const ConfigOptionEnumsGeneric*>(printer_config.option("nozzle_volume_type"));
        if (!opt_extruder_type || !opt_nozzle_volume_type) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: extruder_type or nozzle_volume_type option not found, skipping")%__LINE__;
            return;
        }

        auto opt_filament_volume_maps = dynamic_cast<const ConfigOptionInts*>(printer_config.option("filament_volume_map"));
        std::vector<int> filament_volume_maps;
        // Orca: honour the per-filament volume map only when a producer sized it to the
        // filament count. The full-config producers (PresetBundle injection, engine
        // write-back) always size it; mis-sized maps (stale project values, CLI runs until
        // the per-filament synthesis lands there) must not distort slot resolution nor be
        // indexed out of bounds.
        if (opt_filament_volume_maps && opt_filament_volume_maps->values.size() == filament_count)
            filament_volume_maps = opt_filament_volume_maps->values;
        std::vector<int> variant_index;

        variant_index.resize(filament_count, -1);

        for (int f_index = 0; f_index < filament_count; f_index++)
        {
            ExtruderType extruder_type = (ExtruderType)(opt_extruder_type->get_at(filament_maps[f_index] - 1));
            NozzleVolumeType nozzle_volume_type = (NozzleVolumeType)(opt_nozzle_volume_type->get_at(filament_maps[f_index] - 1));

            if ((extruder_nozzle_volume_count > extruder_count || nozzle_volume_type == nvtHybrid) && (!filament_volume_maps.empty())) {
                nozzle_volume_type = (NozzleVolumeType)(filament_volume_maps[f_index]);
            }

            //variant index
            variant_index[f_index] = get_index_for_extruder(f_index+1, id_name, extruder_type, nozzle_volume_type, variant_name);
            if (variant_index[f_index] < 0) {
                // Orca: a filament need not define every extruder variant (a Direct Drive filament on a
                // Bowden printer), so this is not an invalid state: the filament's first variant is used.
                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: could not found extruder_type %2%, nozzle_volume_type %3%, filament_index %4%, extruder index %5%")
                    %__LINE__ %s_keys_names_ExtruderType[extruder_type] % s_keys_names_NozzleVolumeType[nozzle_volume_type] % (f_index+1) %filament_maps[f_index];
                //for some updates happens in a invalid state(caused by popup window)
                //we need to avoid crash
                variant_index[f_index] = 0;
                if (opt_ids) {
                    for (int i = 0; i < opt_ids->values.size(); i++)
                        if (opt_ids->values[i] == (f_index+1)) {
                            variant_index[f_index] = i;
                            break;
                        }
                }
            }
        }

        const ConfigDef       *config_def     = this->def();
        if (!config_def) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: can not find config define")%__LINE__;
            return;
        }
        bool has_id_key = !id_name.empty() && key_set.count(id_name) > 0;
        for (auto& key: key_set)
        {
            if (has_id_key && key == id_name)
                continue;
            const ConfigOptionDef *optdef  = config_def->get(key);
            if (!optdef) {
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: can not find opt define for %2%")%__LINE__%key;
                continue;
            }
            // An empty option has no first value to fall back on; give it one registered default per filament.
            if (auto *vec = dynamic_cast<ConfigOptionVectorBase*>(this->option(key)); vec && vec->empty() && optdef->default_value)
                vec->resize(filament_count, optdef->default_value.get());

            switch (optdef->type) {
                case coStrings:          gather_option_values<ConfigOptionStrings, std::string>(__FUNCTION__, key, this->option<ConfigOptionStrings>(key), variant_index); break;
                case coInts:             gather_option_values<ConfigOptionInts, int>(__FUNCTION__, key, this->option<ConfigOptionInts>(key), variant_index); break;
                case coFloats:           gather_option_values<ConfigOptionFloats, double>(__FUNCTION__, key, this->option<ConfigOptionFloats>(key), variant_index); break;
                case coPercents:         gather_option_values<ConfigOptionPercents, double>(__FUNCTION__, key, this->option<ConfigOptionPercents>(key), variant_index); break;
                case coFloatsOrPercents: gather_option_values<ConfigOptionFloatsOrPercents, FloatOrPercent>(__FUNCTION__, key, this->option<ConfigOptionFloatsOrPercents>(key), variant_index); break;
                case coBools:            gather_option_values<ConfigOptionBools, unsigned char>(__FUNCTION__, key, this->option<ConfigOptionBools>(key), variant_index); break;
                case coEnums:            gather_option_values<ConfigOptionEnumsGeneric, int>(__FUNCTION__, key, this->option<ConfigOptionEnumsGeneric>(key), variant_index); break;
                default:
                    BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: unsupported option type for %2%")%__LINE__%key;
                    break;
            }
        }
        // Orca: also require a non-empty id list; get_at on an empty vector is undefined.
        if (has_id_key && opt_ids && !opt_ids->values.empty()) {
            // remap the id list itself last: the lookups above still need the original ids
            std::vector<int> new_values;
            new_values.resize(filament_count);
            for (int f_index = 0; f_index < filament_count; f_index++) {
                new_values[f_index] = opt_ids->get_at(variant_index[f_index]);
            }
            const_cast<ConfigOptionInts*>(opt_ids)->values = new_values;
        }
    }
}

void DynamicPrintConfig::update_filament_config_values_for_multiple_extruders(DynamicPrintConfig& printer_config,
    const std::unordered_map<int, std::vector<FilamentVariantUse>>& filament_variant_uses,
    int extruder_count, int extruder_nozzle_volume_count,
    std::set<std::string>& key_set, std::string id_name, std::string variant_name,
    std::vector<int>* slot_machine_indices)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(", Line %1%: extruder_count %2%, extruder_nozzle_volume_count %3%")%__LINE__ %extruder_count %extruder_nozzle_volume_count;

    auto opt_filament_map = printer_config.option<ConfigOptionInts>("filament_map");
    if (!opt_filament_map) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: filament_map option not found, skipping")%__LINE__;
        return;
    }
    std::vector<int> filament_maps = opt_filament_map->values;
    size_t filament_count = filament_maps.size();
    auto opt_extruder_type = dynamic_cast<const ConfigOptionEnumsGeneric*>(printer_config.option("extruder_type"));
    auto opt_nozzle_volume_type = dynamic_cast<const ConfigOptionEnumsGeneric*>(printer_config.option("nozzle_volume_type"));
    if (!opt_extruder_type || !opt_nozzle_volume_type) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: extruder_type or nozzle_volume_type option not found, skipping")%__LINE__;
        return;
    }

    auto opt_filament_volume_maps = dynamic_cast<const ConfigOptionInts*>(printer_config.option("filament_volume_map"));
    std::vector<int> filament_volume_maps;
    // Orca: same sizing guard as the single-slot rebuild above — honour the per-filament volume
    // map only when a producer sized it to the filament count.
    if (opt_filament_volume_maps && opt_filament_volume_maps->values.size() == filament_count)
        filament_volume_maps = opt_filament_volume_maps->values;
    auto opt_ids = id_name.empty() ? nullptr : dynamic_cast<const ConfigOptionInts*>(this->option(id_name));

    std::vector<int> slot_param_indices;
    slot_param_indices.reserve(filament_count * 2);
    if (slot_machine_indices) {
        slot_machine_indices->clear();
        slot_machine_indices->reserve(filament_count * 2);
    }

    auto resolve_slot = [&](int f_index, ExtruderType extruder_type, NozzleVolumeType nozzle_volume_type, int extruder_id_1based) {
        int param_index = get_index_for_extruder(f_index + 1, id_name, extruder_type, nozzle_volume_type, variant_name);
        if (param_index < 0) {
            BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: could not found extruder_type %2%, nozzle_volume_type %3%, filament_index %4%, extruder index %5%")
                %__LINE__ %s_keys_names_ExtruderType[extruder_type] % s_keys_names_NozzleVolumeType[nozzle_volume_type] % (f_index+1) %extruder_id_1based;
            assert(false);
            //for some updates happens in a invalid state(caused by popup window)
            //we need to avoid crash
            param_index = 0;
            if (opt_ids) {
                for (int i = 0; i < opt_ids->values.size(); i++)
                    if (opt_ids->values[i] == (f_index + 1)) {
                        param_index = i;
                        break;
                    }
            }
        }
        slot_param_indices.push_back(param_index);
        if (slot_machine_indices) {
            // Orca: key each output slot to the machine slot of its own variant (the same
            // derivation the per-filament slot map uses), so the retract-override nil fallback
            // stays aligned when a filament occupies more than one slot.
            int machine_index = printer_config.get_index_for_extruder(extruder_id_1based, "print_extruder_id", extruder_type, nozzle_volume_type, "print_extruder_variant");
            slot_machine_indices->push_back(std::max(machine_index, 0));
        }
    };

    for (int f_index = 0; f_index < static_cast<int>(filament_count); f_index++) {
        auto uses_it = filament_variant_uses.find(f_index);
        if (uses_it != filament_variant_uses.end() && !uses_it->second.empty()) {
            for (const FilamentVariantUse &use : uses_it->second)
                resolve_slot(f_index, use.extruder_type, use.nozzle_volume_type, use.extruder_id + 1);
        }
        else {
            ExtruderType extruder_type = (ExtruderType)(opt_extruder_type->get_at(filament_maps[f_index] - 1));
            NozzleVolumeType nozzle_volume_type = (NozzleVolumeType)(opt_nozzle_volume_type->get_at(filament_maps[f_index] - 1));
            if ((extruder_nozzle_volume_count > extruder_count || nozzle_volume_type == nvtHybrid) && !filament_volume_maps.empty())
                nozzle_volume_type = (NozzleVolumeType)(filament_volume_maps[f_index]);
            resolve_slot(f_index, extruder_type, nozzle_volume_type, filament_maps[f_index]);
        }
    }

    const ConfigDef *config_def = this->def();
    if (!config_def) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(", Line %1%: can not find config define")%__LINE__;
        return;
    }
    bool has_id_key = !id_name.empty() && key_set.count(id_name) > 0;
    for (auto &key : key_set) {
        if (has_id_key && key == id_name)
            continue;
        const ConfigOptionDef *optdef = config_def->get(key);
        if (!optdef) {
            BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: can not find opt define for %2%")%__LINE__%key;
            continue;
        }
        switch (optdef->type) {
            case coStrings:          gather_option_values<ConfigOptionStrings, std::string>(__FUNCTION__, key, this->option<ConfigOptionStrings>(key), slot_param_indices); break;
            case coInts:             gather_option_values<ConfigOptionInts, int>(__FUNCTION__, key, this->option<ConfigOptionInts>(key), slot_param_indices); break;
            case coFloats:           gather_option_values<ConfigOptionFloats, double>(__FUNCTION__, key, this->option<ConfigOptionFloats>(key), slot_param_indices); break;
            case coPercents:         gather_option_values<ConfigOptionPercents, double>(__FUNCTION__, key, this->option<ConfigOptionPercents>(key), slot_param_indices); break;
            case coFloatsOrPercents: gather_option_values<ConfigOptionFloatsOrPercents, FloatOrPercent>(__FUNCTION__, key, this->option<ConfigOptionFloatsOrPercents>(key), slot_param_indices); break;
            case coBools:            gather_option_values<ConfigOptionBools, unsigned char>(__FUNCTION__, key, this->option<ConfigOptionBools>(key), slot_param_indices); break;
            case coEnums:            gather_option_values<ConfigOptionEnumsGeneric, int>(__FUNCTION__, key, this->option<ConfigOptionEnumsGeneric>(key), slot_param_indices); break;
            default:
                BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(", Line %1%: unsupported option type for %2%")%__LINE__%key;
                break;
        }
    }
    // Orca: also require a non-empty id list; get_at on an empty vector is undefined.
    if (has_id_key && opt_ids && !opt_ids->values.empty()) {
        // remap the id list itself last: the slot resolution above still needs the original ids
        std::vector<int> new_values;
        new_values.reserve(slot_param_indices.size());
        for (int idx : slot_param_indices)
            new_values.push_back(opt_ids->get_at(idx));
        const_cast<ConfigOptionInts*>(opt_ids)->values = std::move(new_values);
    }
}

namespace {
// Options in printer_options_with_variant_2 are stored as (normal,silent) pairs per printer variant.
// Some legacy presets/projects carry a variant list but still store only one pair; normalize to avoid crashes.
static void normalize_stride2_floats(ConfigOptionFloats &opt, size_t expected_size)
{
    auto &v = opt.values;
    if (expected_size == 0) {
        v.clear();
        return;
    }
    if (v.empty()) {
        // Fallback: keep behavior predictable instead of crashing. This should be rare.
        v.resize(expected_size, 0.0);
        return;
    }

    const double first  = v[0];
    const double second = (v.size() >= 2) ? v[1] : first;

    // Ensure we have at least one (normal,silent) pair to replicate.
    if (v.size() < 2) {
        v.resize(2, first);
        v[1] = second;
    }
    // Keep pair alignment if some legacy preset produced odd length.
    if (v.size() % 2 != 0)
        v.push_back(second);

    if (v.size() > expected_size) {
        v.resize(expected_size);
        return;
    }

    const size_t have_variants = v.size() / 2;
    const size_t want_variants = expected_size / 2;
    v.resize(expected_size);
    for (size_t vi = have_variants; vi < want_variants; ++vi) {
        v[vi * 2] = first;
        if (vi * 2 + 1 < v.size())
            v[vi * 2 + 1] = second;
    }
}

static void log_normalize_legacy_vector_size(const char *fn, const std::string &key, int stride, size_t src_size, size_t dest_size, size_t expected_size,
                                            size_t restore_n, int cur_variant_count, int target_variant_count, size_t cur_ids, size_t target_ids,
                                            const ConfigOption *opt_src, const ConfigOption *opt_target)
{
    BOOST_LOG_TRIVIAL(debug) << fn << ": normalizing legacy vector size for key '" << key << "'"
                             << " stride=" << stride << " src_size=" << src_size << " dest_size=" << dest_size << " expected=" << expected_size
                             << " restore_index.size=" << restore_n << " cur_variants=" << cur_variant_count << " target_variants=" << target_variant_count
                             << " cur_ids=" << cur_ids << " target_ids=" << target_ids << " cur_value=" << opt_src->serialize()
                             << " target_value=" << opt_target->serialize();
}
} // namespace

void DynamicPrintConfig::update_non_diff_values_to_base_config(DynamicPrintConfig& new_config, const t_config_option_keys& keys, const std::set<std::string>& different_keys,
    std::string extruder_id_name, std::string extruder_variant_name, std::set<std::string>& key_set1, std::set<std::string>& key_set2)
{
    std::vector<int> cur_extruder_ids, target_extruder_ids, variant_index;
    std::vector<std::string> cur_extruder_variants, target_extruder_variants;

    if (!extruder_id_name.empty()) {
        if (this->option(extruder_id_name))
            cur_extruder_ids = this->option<ConfigOptionInts>(extruder_id_name)->values;
        if (new_config.option(extruder_id_name))
            target_extruder_ids = new_config.option<ConfigOptionInts>(extruder_id_name)->values;
    }
    if (this->option(extruder_variant_name))
        cur_extruder_variants = this->option<ConfigOptionStrings>(extruder_variant_name, true)->values;
    if (new_config.option(extruder_variant_name))
        target_extruder_variants = new_config.option<ConfigOptionStrings>(extruder_variant_name, true)->values;

    int cur_variant_count = cur_extruder_variants.size();
    int target_variant_count = target_extruder_variants.size();

    // A base variant this config does not list (the base gained it after the config was saved, or the
    // config lists none) takes this config's first variant of the same extruder, as a user preset's
    // values do in update_diff_values_to_child_config. Left unmatched, the base's value would silently
    // replace the user's.
    variant_index.resize(target_variant_count, -1);
    if (cur_variant_count == 0) {
        // Defensive: target_variant_count may be 0 if the preset doesn't carry extruder_variant_name.
        // In that case keep variant_index empty and let the downstream size checks produce a useful error.
        if (!variant_index.empty())
            // This config's one value belongs to the extruder of the base's first variant.
            variant_index = map_variant_indices(target_extruder_variants, target_extruder_ids, {},
                                                target_extruder_ids.empty() ? std::vector<int>() : std::vector<int>{target_extruder_ids[0]});
    }
    else if ((cur_extruder_ids.size() > 0) && cur_variant_count != cur_extruder_ids.size()){
        //should not happen
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" size of %1% = %2%, not equal to size of %3% = %4%")
             %extruder_variant_name %cur_variant_count %extruder_id_name %cur_extruder_ids.size();
    }
    else if ((target_extruder_ids.size() > 0) && target_variant_count != target_extruder_ids.size()){
        //should not happen
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" size of %1% = %2%, not equal to size of %3% = %4%")
             %extruder_variant_name %target_variant_count %extruder_id_name %target_extruder_ids.size();
    }
    else {
        variant_index = map_variant_indices(target_extruder_variants, target_extruder_ids, cur_extruder_variants, cur_extruder_ids);
    }

    for (auto& opt : keys) {
        ConfigOption *opt_src = this->option(opt);
        const ConfigOption *opt_target = new_config.option(opt);
        if (opt_src && opt_target && (*opt_src != *opt_target)) {
            BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" change key %1% from old_value %2% to inherit's value %3%")
                    %opt %(opt_src->serialize()) %(opt_target->serialize());
            if (different_keys.find(opt) == different_keys.end()) {
                opt_src->set(opt_target);
            }
            else {
                if (opt_target->is_scalar()
                    || ((key_set1.find(opt) == key_set1.end()) && (key_set2.empty() || (key_set2.find(opt) == key_set2.end())))) {
                    //nothing to do, keep the original one
                }
                else {
                    // Guard: set_with_restore is parent-shaped and would truncate the child's
                    // vector when the child has more extruders than the parent (e.g. an IDEX
                    // preset inheriting from a single-nozzle base). The child's saved value is
                    // authoritative for its own extruder count, so skip the merge for this key.
                    if (cur_variant_count > target_variant_count)
                        continue;

                    // The variant lists are the base's layout itself, which every other value is
                    // carried onto: a variant this config lacks keeps its own name and id.
                    if (opt == extruder_id_name || opt == extruder_variant_name) {
                        opt_src->set(opt_target);
                        continue;
                    }

                    int stride = 1;
                    if (key_set2.find(opt) != key_set2.end())
                        stride = 2;

                    const size_t restore_n     = variant_index.size();
                    const size_t expected_size = restore_n * size_t(stride);

                    if (stride == 2) {
                        // Options in key_set2 are machine limits stored as (normal,silent) pairs per printer variant.
                        if (opt_src->type() != coFloats || opt_target->type() != coFloats)
                            throw ConfigurationError((boost::format("%1%: key '%2%' is expected to be ConfigOptionFloats for stride=2.") % __FUNCTION__ % opt).str());

                        auto *src_f = static_cast<ConfigOptionFloats*>(opt_src);
                        ConfigOptionFloats rhs_tmp(*static_cast<const ConfigOptionFloats*>(opt_target));

                        const size_t src_size  = src_f->values.size();
                        const size_t dest_size = rhs_tmp.values.size();
                        if (src_size != expected_size || dest_size != expected_size)
                            log_normalize_legacy_vector_size(__FUNCTION__, opt, stride, src_size, dest_size, expected_size, restore_n, cur_variant_count,
                                                             target_variant_count, cur_extruder_ids.size(), target_extruder_ids.size(), opt_src, opt_target);

                        // Normalize src in-place so backup_values indexing is safe, normalize rhs via a temporary copy.
                        normalize_stride2_floats(*src_f, expected_size);
                        normalize_stride2_floats(rhs_tmp, expected_size);
                        src_f->set_with_restore(&rhs_tmp, variant_index, stride);
                    } else {
                        ConfigOptionVectorBase* opt_vec_src = static_cast<ConfigOptionVectorBase*>(opt_src);

                        const size_t src_size  = opt_vec_src->size();
                        const size_t dest_size = static_cast<const ConfigOptionVectorBase*>(opt_target)->size();
                        if (src_size != expected_size || dest_size != expected_size)
                            log_normalize_legacy_vector_size(__FUNCTION__, opt, stride, src_size, dest_size, expected_size, restore_n, cur_variant_count,
                                                             target_variant_count, cur_extruder_ids.size(), target_extruder_ids.size(), opt_src, opt_target);

                        if (opt_vec_src->size() != expected_size)
                            opt_vec_src->resize(expected_size, opt_target);

                        // Normalize rhs via a cloned temporary (rhs itself is const).
                        ConfigOptionUniquePtr rhs_owner(opt_target->clone());
                        ConfigOptionVectorBase *rhs_vec = dynamic_cast<ConfigOptionVectorBase*>(rhs_owner.get());
                        if (rhs_vec == nullptr)
                            throw ConfigurationError((boost::format("%1%: key '%2%' is expected to be a vector option.") % __FUNCTION__ % opt).str());
                        if (rhs_vec->size() != expected_size)
                            rhs_vec->resize(expected_size, opt_target);

                        opt_vec_src->set_with_restore(rhs_vec, variant_index, stride);
                    }
                }
            }
        }
    }
    return;
}

void DynamicPrintConfig::update_diff_values_to_child_config(DynamicPrintConfig& new_config, std::string extruder_id_name, std::string extruder_variant_name, std::set<std::string>& key_set1, std::set<std::string>& key_set2)
{
    std::vector<int> cur_extruder_ids, target_extruder_ids, variant_index;
    std::vector<std::string> cur_extruder_variants, target_extruder_variants;

    if (!extruder_id_name.empty()) {
        if (this->option(extruder_id_name))
            cur_extruder_ids = this->option<ConfigOptionInts>(extruder_id_name)->values;
        if (new_config.option(extruder_id_name))
            target_extruder_ids = new_config.option<ConfigOptionInts>(extruder_id_name)->values;
    }
    if (this->option(extruder_variant_name))
        cur_extruder_variants = this->option<ConfigOptionStrings>(extruder_variant_name, true)->values;
    if (new_config.option(extruder_variant_name))
        target_extruder_variants = new_config.option<ConfigOptionStrings>(extruder_variant_name, true)->values;

    int cur_variant_count = cur_extruder_variants.size();
    int target_variant_count = target_extruder_variants.size();

    if (cur_variant_count > 0)
        variant_index.resize(cur_variant_count, -1);
    else
        variant_index.resize(1, 0);

    // A parent variant the child does not list (the parent gained it after the child was saved, or the
    // child lists none) takes the child's first variant of the same extruder, as slicing does.
    // Left unmatched, the parent's value would silently replace the user's.
    if (target_variant_count == 0) {
        // The child's one value belongs to the extruder of the parent's first variant.
        if (cur_variant_count > 0)
            variant_index = map_variant_indices(cur_extruder_variants, cur_extruder_ids, {},
                                                cur_extruder_ids.empty() ? std::vector<int>() : std::vector<int>{cur_extruder_ids[0]});
    }
    else if ((cur_extruder_ids.size() > 0) && cur_variant_count != cur_extruder_ids.size()){
        //should not happen
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" size of %1% = %2%, not equal to size of %3% = %4%")
             %extruder_variant_name %cur_variant_count %extruder_id_name %cur_extruder_ids.size();
    }
    else if ((target_extruder_ids.size() > 0) && target_variant_count != target_extruder_ids.size()){
        //should not happen
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << boost::format(" size of %1% = %2%, not equal to size of %3% = %4%")
             %extruder_variant_name %target_variant_count %extruder_id_name %target_extruder_ids.size();
    }
    else if (cur_variant_count > 0) {
        variant_index = map_variant_indices(cur_extruder_variants, cur_extruder_ids, target_extruder_variants, target_extruder_ids);
    }

    const t_config_option_keys &keys = new_config.keys();
    for (auto& opt : keys) {
        if ((opt == extruder_id_name) || (opt == extruder_variant_name))
            continue;
        ConfigOption *opt_src = this->option(opt);
        const ConfigOption *opt_target = new_config.option(opt);
        if (opt_src && opt_target && (*opt_src != *opt_target)) {
            BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(" change key %1% from base_value %2% to child's value %3%")
                    %opt %(opt_src->serialize()) %(opt_target->serialize());
            if (opt_target->is_scalar()
                || ((key_set1.find(opt) == key_set1.end()) && (key_set2.empty() || (key_set2.find(opt) == key_set2.end())))) {
                //nothing to do, keep the original one
                opt_src->set(opt_target);
            }
            else {
                ConfigOptionVectorBase* opt_vec_src = static_cast<ConfigOptionVectorBase*>(opt_src);
                const ConfigOptionVectorBase* opt_vec_dest = static_cast<const ConfigOptionVectorBase*>(opt_target);
                int stride = 1;
                if (key_set2.find(opt) != key_set2.end())
                    stride = 2;
                // set_only_diff() requires the base vector length to equal variant_index.size()*stride, where
                // variant_index is sized from the parent's printer_extruder_variant list (or 1 when it has none).
                // Legacy / multi-extruder / multi-variant presets can violate this in either direction when a key's
                // stored length and the variant-list length were sized inconsistently - e.g. a Snapmaker U1 base
                // (4 nozzles) whose stride-2 machine limits were length-extended to nozzles*2 while its
                // printer_extruder_variant list is empty (variant_index == 1), so base length != variant_index*stride.
                // Rather than throw (which is caught upstream and DELETES the user preset file), fall back to the
                // child's explicit value, which is authoritative for its own extruder/variant layout.
                if (opt_vec_src->size() != variant_index.size() * size_t(stride)) {
                    opt_src->set(opt_target);
                }
                else
                    opt_vec_src->set_only_diff(opt_vec_dest, variant_index, stride);
            }
        }
    }
    return;
}

void compute_filament_override_value(const std::string& opt_key, const ConfigOption *opt_old_machine, const ConfigOption *opt_new_machine, const ConfigOption *opt_new_filament, const DynamicPrintConfig& new_full_config,
    t_config_option_keys& diff_keys, DynamicPrintConfig& filament_overrides, std::vector<int>& f_map_indices)
{
    bool is_nil = opt_new_filament->is_nil();

    // ugly code, for these params, we should ignore the value in filament params
    ConfigOptionBoolsNullable opt_long_retraction_default;
    if (opt_key == "long_retractions_when_cut" && new_full_config.option<ConfigOptionInt>("enable_long_retraction_when_cut")->value != LongRectrationLevel::EnableFilament) {
        auto ptr = dynamic_cast<const ConfigOptionBoolsNullable*>(opt_new_filament);
        for (size_t idx = 0; idx < ptr->values.size(); ++idx)
            opt_long_retraction_default.values.push_back(ptr->nil_value());
        opt_new_filament = &opt_long_retraction_default;
    }

    ConfigOptionFloatsNullable opt_retraction_distance_default;
    if (opt_key == "retraction_distances_when_cut" && new_full_config.option<ConfigOptionInt>("enable_long_retraction_when_cut")->value != LongRectrationLevel::EnableFilament) {
        auto ptr = dynamic_cast<const ConfigOptionFloatsNullable*>(opt_new_filament);
        for (size_t idx = 0; idx < ptr->values.size(); ++idx)
            opt_long_retraction_default.values.push_back(ptr->nil_value());
        opt_new_filament = &opt_retraction_distance_default;
    }

    auto opt_copy = opt_new_machine->clone();
    opt_copy->apply_override(opt_new_filament, f_map_indices);
    bool changed = *opt_old_machine != *opt_copy;

    if (changed) {
        diff_keys.emplace_back(opt_key);
        filament_overrides.set_key_value(opt_key, opt_copy);
    }
    else
        delete opt_copy;
}


void update_static_print_config_from_dynamic(ConfigBase& config, const DynamicPrintConfig& dest_config, std::vector<int> variant_index, std::set<std::string>& key_set1, int stride)
{
    if (variant_index.size() > 0) {
        const t_config_option_keys &keys = dest_config.keys();
        for (auto& opt : keys) {
            ConfigOption *opt_src = config.option(opt);
            const ConfigOption *opt_dest = dest_config.option(opt);
            if (opt_src && opt_dest && (*opt_src != *opt_dest)) {
                if (opt_dest->is_scalar() || (key_set1.find(opt) == key_set1.end()))
                    opt_src->set(opt_dest);
                else {
                    ConfigOptionVectorBase* opt_vec_src = static_cast<ConfigOptionVectorBase*>(opt_src);
                    const ConfigOptionVectorBase* opt_vec_dest = static_cast<const ConfigOptionVectorBase*>(opt_dest);
                    set_variant_override(*opt_vec_src, *opt_vec_dest, variant_index, stride);
                }
            }
        }
    }
    else
        config.apply(dest_config, true);
}

//BBS: pass map to recording all invalid valies
//FIXME localize this function.
std::map<std::string, std::string> validate(const FullPrintConfig &cfg, bool under_cli)
{
    std::map<std::string, std::string> error_message;
    // --layer-height
    if (cfg.get_abs_value("layer_height") <= 0) {
        error_message.emplace("layer_height", _u8L("invalid value ") + std::to_string(cfg.get_abs_value("layer_height")));
    }
    else if (fabs(fmod(cfg.get_abs_value("layer_height"), SCALING_FACTOR)) > 1e-4) {
        error_message.emplace("layer_height", _u8L("invalid value ") + std::to_string(cfg.get_abs_value("layer_height")));
    }

    // --first-layer-height
    if (cfg.initial_layer_print_height.value <= 0) {
        error_message.emplace("initial_layer_print_height", _u8L("invalid value ") + std::to_string(cfg.initial_layer_print_height.value));
    }

    // --filament-diameter
    for (double fd : cfg.filament_diameter.values)
        if (fd < 1) {
            error_message.emplace("filament_diameter", _u8L("invalid value ") + cfg.filament_diameter.serialize());
            break;
        }

    // --nozzle-diameter
    for (double nd : cfg.nozzle_diameter.values)
        if (nd < 0.005) {
            error_message.emplace("nozzle_diameter", _u8L("invalid value ") + cfg.nozzle_diameter.serialize());
            break;
        }

    // --perimeters
    if (cfg.wall_loops.value < 0) {
        error_message.emplace("wall_loops", _u8L("invalid value ") + std::to_string(cfg.wall_loops.value));
    }

    // --solid-layers
    if (cfg.top_shell_layers < 0) {
        error_message.emplace("top_shell_layers", _u8L("invalid value ") + std::to_string(cfg.top_shell_layers));
    }
    if (cfg.bottom_shell_layers < 0) {
        error_message.emplace("bottom_shell_layers", _u8L("invalid value ") + std::to_string(cfg.bottom_shell_layers));
    }

    if (cfg.use_firmware_retraction.value &&
        cfg.gcode_flavor.value != gcfKlipper &&
        cfg.gcode_flavor.value != gcfSmoothie &&
        cfg.gcode_flavor.value != gcfRepRapSprinter &&
        cfg.gcode_flavor.value != gcfRepRapFirmware &&
        cfg.gcode_flavor.value != gcfMarlinLegacy &&
        cfg.gcode_flavor.value != gcfMarlinFirmware &&
        cfg.gcode_flavor.value != gcfMachinekit &&
        cfg.gcode_flavor.value != gcfRepetier)
        error_message.emplace("use_firmware_retraction","--use-firmware-retraction is only supported by Klipper, Marlin, Smoothie, RepRapFirmware, Repetier and Machinekit firmware");

    if (cfg.use_firmware_retraction.value)
        for (unsigned char wipe : cfg.wipe.values)
             if (wipe)
                error_message.emplace("use_firmware_retraction", "--use-firmware-retraction is not compatible with --wipe");
                
    // --gcode-flavor
    if (! print_config_def.get("gcode_flavor")->has_enum_value(cfg.gcode_flavor.serialize())) {
        error_message.emplace("gcode_flavor", _u8L("invalid value ") + cfg.gcode_flavor.serialize());
    }

    // --fill-pattern
    if (! print_config_def.get("sparse_infill_pattern")->has_enum_value(cfg.sparse_infill_pattern.serialize())) {
        error_message.emplace("sparse_infill_pattern", _u8L("invalid value ") + cfg.sparse_infill_pattern.serialize());
    }

    // --top-fill-pattern
    if (! print_config_def.get("top_surface_pattern")->has_enum_value(cfg.top_surface_pattern.serialize())) {
        error_message.emplace("top_surface_pattern", _u8L("invalid value ") + cfg.top_surface_pattern.serialize());
    }

    // --bottom-fill-pattern
    if (! print_config_def.get("bottom_surface_pattern")->has_enum_value(cfg.bottom_surface_pattern.serialize())) {
        error_message.emplace("bottom_surface_pattern", _u8L("invalid value ") + cfg.bottom_surface_pattern.serialize());
    }

    // --soild-fill-pattern
    if (!print_config_def.get("internal_solid_infill_pattern")->has_enum_value(cfg.internal_solid_infill_pattern.serialize())) {
        error_message.emplace("internal_solid_infill_pattern", _u8L("invalid value ") + cfg.internal_solid_infill_pattern.serialize());
    }

    // --skirt-height
    if (cfg.skirt_height < 0) {
        error_message.emplace("skirt_height", _u8L("invalid value ") + std::to_string(cfg.skirt_height));
    }

    // --bridge-flow-ratio
    if (cfg.bridge_flow <= 0) {
        error_message.emplace("bridge_flow", _u8L("invalid value ") + std::to_string(cfg.bridge_flow));
    }
    
    // --internal-bridge-flow-ratio
    if (cfg.internal_bridge_flow <= 0) {
        error_message.emplace("internal_bridge_flow", _u8L("invalid value ") + std::to_string(cfg.internal_bridge_flow));
    }

    // extruder clearance
    if (cfg.extruder_clearance_radius <= 0) {
        error_message.emplace("extruder_clearance_radius", _u8L("invalid value ") + std::to_string(cfg.extruder_clearance_radius));
    }
    if (cfg.extruder_clearance_height_to_rod <= 0) {
        error_message.emplace("extruder_clearance_height_to_rod", _u8L("invalid value ") + std::to_string(cfg.extruder_clearance_height_to_rod));
    }
    if (cfg.extruder_clearance_height_to_lid <= 0) {
        error_message.emplace("extruder_clearance_height_to_lid", _u8L("invalid value ") + std::to_string(cfg.extruder_clearance_height_to_lid));
    }
    if (cfg.nozzle_height <= 0)
        error_message.emplace("nozzle_height", _u8L("invalid value ") + std::to_string(cfg.nozzle_height));

    // --extrusion-multiplier
    for (double em : cfg.filament_flow_ratio.values)
        if (em <= 0) {
            error_message.emplace("filament_flow_ratio", _u8L("invalid value ") + cfg.filament_flow_ratio.serialize());
            break;
        }

    // --spiral-vase
    //for non-cli case, we will popup dialog for spiral mode correction
    if (cfg.spiral_mode && under_cli) {
        // Note that we might want to have more than one perimeter on the bottom
        // solid layers.
        if (cfg.wall_loops != 1) {
            error_message.emplace("wall_loops", _u8L("Invalid value when spiral vase mode is enabled: ") + std::to_string(cfg.wall_loops));
            //return "Can't make more than one perimeter when spiral vase mode is enabled";
            //return "Can't make less than one perimeter when spiral vase mode is enabled";
        }

        if (cfg.sparse_infill_density > 0) {
            error_message.emplace("sparse_infill_density", _u8L("Invalid value when spiral vase mode is enabled: ") + std::to_string(cfg.sparse_infill_density));
            //return "Spiral vase mode can only print hollow objects, so you need to set Fill density to 0";
        }

        if (cfg.top_shell_layers > 0) {
            error_message.emplace("top_shell_layers", _u8L("Invalid value when spiral vase mode is enabled: ") + std::to_string(cfg.top_shell_layers));
            //return "Spiral vase mode is not compatible with top solid layers";
        }

        if (cfg.enable_support ) {
            error_message.emplace("enable_support", _u8L("Invalid value when spiral vase mode is enabled: ") + std::to_string(cfg.enable_support));
            //return "Spiral vase mode is not compatible with support";
        }
        if (cfg.enforce_support_layers > 0) {
            error_message.emplace("enforce_support_layers", _u8L("Invalid value when spiral vase mode is enabled: ") + std::to_string(cfg.enforce_support_layers));
            //return "Spiral vase mode is not compatible with support";
        }
    }

    // extrusion widths
    {
        double max_nozzle_diameter = 0.;
        double min_nozzle_diameter = std::numeric_limits<double>::max();
        for (double dmr : cfg.nozzle_diameter.values)
        {
            max_nozzle_diameter = std::max(max_nozzle_diameter, dmr);
            min_nozzle_diameter = std::min(min_nozzle_diameter, dmr);
        }
        const char *widths[] = {
            "outer_wall_line_width",
            "inner_wall_line_width",
            "sparse_infill_line_width",
            "internal_solid_infill_line_width",
            "bridge_line_width",
            "top_surface_line_width",
            "support_line_width",
            "initial_layer_line_width",
            "skin_infill_line_width",
            "skeleton_infill_line_width"};
        for (size_t i = 0; i < sizeof(widths) / sizeof(widths[i]); ++ i) {
            std::string key(widths[i]);
            double abs_width = cfg.get_abs_value(key, max_nozzle_diameter);
            double allowed_max = (key == "bridge_line_width") ? min_nozzle_diameter : MAX_LINE_WIDTH_MULTIPLIER * max_nozzle_diameter;
            if (abs_width > allowed_max) {
                if (key == "bridge_line_width")
                    error_message.emplace(key, _u8L("Bridge line width must not exceed nozzle diameter: ") + std::to_string(abs_width));
                else
                    error_message.emplace(key, _u8L("too large line width ") + std::to_string(abs_width));
                //return std::string("Too Large line width: ") + key;
            }
        }
    }

    // Out of range validation of numeric values.
    for (const std::string &opt_key : cfg.keys()) {
        const ConfigOption      *opt    = cfg.optptr(opt_key);
        assert(opt != nullptr);
        const ConfigOptionDef   *optdef = print_config_def.get(opt_key);
        assert(optdef != nullptr);
        bool out_of_range = false;
        switch (opt->type()) {
        case coFloat:
        case coPercent:
        case coFloatOrPercent:
        {
            auto *fopt = static_cast<const ConfigOptionFloat*>(opt);
            out_of_range = !optdef->is_value_valid(fopt->value);
            break;
        }
        case coFloats:
        case coPercents:
            for (double v : static_cast<const ConfigOptionVector<double>*>(opt)->values)
                if (!optdef->is_value_valid(v)) {
                    out_of_range = true;
                    break;
                }
            break;
        case coInt:
        {
            auto *iopt = static_cast<const ConfigOptionInt*>(opt);
            out_of_range = !optdef->is_value_valid(iopt->value);
            break;
        }
        case coInts:
            for (int v : static_cast<const ConfigOptionVector<int>*>(opt)->values)
                if (!optdef->is_value_valid(v)) {
                    out_of_range = true;
                    break;
                }
            break;
        default:;
        }
        if (out_of_range) {
            if (error_message.find(opt_key) == error_message.end())
                // TRN %1% is the current value, %2% and %3% the lowest and highest allowed values.
                error_message.emplace(opt_key, Slic3r::format(_u8L("%1% not in range [%2%, %3%]"), opt->serialize(), optdef->min, optdef->max));
            //return std::string("Value out of range: " + opt_key);
        }
    }

    // Mixed-color (混色) parameter validation.
    {
        const auto &is_mixed       = cfg.filament_is_mixed.values;
        const auto &comp_strs      = cfg.filament_mixed_components.values;
        const auto &ratio_strs     = cfg.filament_mixed_sublayer_ratios.values;
        const auto &gradient_flags = cfg.filament_mixed_gradient.values;
        const auto &range_strs     = cfg.filament_mixed_gradient_range.values;
        const auto &curve_strs     = cfg.filament_mixed_gradient_curve.values;

        std::map<std::string, std::string> mixed_errors = validate_mixed_filament_params(
            is_mixed, comp_strs, ratio_strs, gradient_flags,
            range_strs, curve_strs);
        for (const auto &kv : mixed_errors)
            if (error_message.find(kv.first) == error_message.end())
                error_message.emplace(kv.first, kv.second);
    }

    // The configuration is valid.
    return error_message;
}

// Declare and initialize static caches of StaticPrintConfig derived classes.
#define PRINT_CONFIG_CACHE_ELEMENT_DEFINITION(r, data, CLASS_NAME) StaticPrintConfig::StaticCache<class Slic3r::CLASS_NAME> BOOST_PP_CAT(CLASS_NAME::s_cache_, CLASS_NAME);
#define PRINT_CONFIG_CACHE_ELEMENT_INITIALIZATION(r, data, CLASS_NAME) Slic3r::CLASS_NAME::initialize_cache();
#define PRINT_CONFIG_CACHE_INITIALIZE(CLASSES_SEQ)     BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CACHE_ELEMENT_DEFINITION, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_SEQ))     int print_config_static_initializer() {         /* For some reason it's important this function doesn't get optimized out, so this should work. */         static volatile int ret = 1;         BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_CACHE_ELEMENT_INITIALIZATION, _, BOOST_PP_TUPLE_TO_SEQ(CLASSES_SEQ))         return ret;     }
PRINT_CONFIG_CACHE_INITIALIZE((
    PrintObjectConfig, PrintRegionConfig, MachineEnvelopeConfig, GCodeConfig, PrintConfig, FullPrintConfig,
    SLAMaterialConfig, SLAPrintConfig, SLAPrintObjectConfig, SLAPrinterConfig, SLAFullPrintConfig))
static int print_config_static_initialized = print_config_static_initializer();

// The same set() calls ConfigBase::apply_only() makes, without looking every key up by name. Out of line so the
// option list is expanded for this once, not in every file that includes PrintConfig.hpp.
#define PRINT_CONFIG_APPLY_TO_DEFINITION(r, data, CLASS_NAME) \
    bool CLASS_NAME::apply_to(ConfigBase &target) const \
    { \
        auto *dst = dynamic_cast<CLASS_NAME*>(&target); \
        if (dst == nullptr) \
            return false; \
        visit_option_pairs(*dst, *this, [](const char*, ConfigOption &a, const ConfigOption &b) { a.set(&b); return true; }); \
        return true; \
    }
BOOST_PP_SEQ_FOR_EACH(PRINT_CONFIG_APPLY_TO_DEFINITION, _, (PrintObjectConfig)(PrintRegionConfig)(MachineEnvelopeConfig)(GCodeConfig)
    (SLAMaterialConfig)(SLAPrintConfig)(SLAPrintObjectConfig)(SLAPrinterConfig))
#undef PRINT_CONFIG_APPLY_TO_DEFINITION

//BBS: remove unused command currently

} // namespace Slic3r