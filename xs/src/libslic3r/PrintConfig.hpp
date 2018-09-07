// Configuration store of Slic3r.
//
// The configuration store is either static or dynamic.
// DynamicPrintConfig is used mainly at the user interface. while the StaticPrintConfig is used
// during the slicing and the g-code generation.
//
// The classes derived from StaticPrintConfig form a following hierarchy.
//
// FullPrintConfig
//    PrintObjectConfig
//    PrintRegionConfig
//    PrintConfig
//        GCodeConfig
//    HostConfig
//

#ifndef slic3r_PrintConfig_hpp_
#define slic3r_PrintConfig_hpp_

#include "libslic3r.h"
#include "Config.hpp"

namespace Slic3r {

enum GCodeFlavor {
    gcfRepRap, gcfRepetier, gcfTeacup, gcfMakerWare, gcfMarlin, gcfSailfish, gcfMach3, gcfMachinekit, 
    gcfSmoothie, gcfNoExtrusion,
};

enum PrintHostType {
    htOctoPrint, htDuet,
};

enum InfillPattern {
    ipRectilinear, ipGrid, ipTriangles, ipStars, ipCubic, ipLine, ipConcentric, ipHoneycomb, ip3DHoneycomb,
    ipGyroid, ipHilbertCurve, ipArchimedeanChords, ipOctagramSpiral,
};

enum SupportMaterialPattern {
    smpRectilinear, smpRectilinearGrid, smpHoneycomb,
};

enum SeamPosition {
    spRandom, spNearest, spAligned, spRear
};

enum FilamentType {
    ftPLA, ftABS, ftPET, ftHIPS, ftFLEX, ftSCAFF, ftEDGE, ftNGEN, ftPVA
};

template<> inline t_config_enum_values& ConfigOptionEnum<GCodeFlavor>::get_enum_values() {
    static t_config_enum_values keys_map;
    if (keys_map.empty()) {
        keys_map["reprap"]          = gcfRepRap;
        keys_map["repetier"]        = gcfRepetier;
        keys_map["teacup"]          = gcfTeacup;
        keys_map["makerware"]       = gcfMakerWare;
        keys_map["marlin"]          = gcfMarlin;
        keys_map["sailfish"]        = gcfSailfish;
        keys_map["smoothie"]        = gcfSmoothie;
        keys_map["mach3"]           = gcfMach3;
        keys_map["machinekit"]      = gcfMachinekit;
        keys_map["no-extrusion"]    = gcfNoExtrusion;
    }
    return keys_map;
}

template<> inline t_config_enum_values& ConfigOptionEnum<PrintHostType>::get_enum_values() {
    static t_config_enum_values keys_map;
    if (keys_map.empty()) {
        keys_map["octoprint"]       = htOctoPrint;
        keys_map["duet"]            = htDuet;
    }
    return keys_map;
}

template<> inline t_config_enum_values& ConfigOptionEnum<InfillPattern>::get_enum_values() {
    static t_config_enum_values keys_map;
    if (keys_map.empty()) {
        keys_map["rectilinear"]         = ipRectilinear;
        keys_map["grid"]                = ipGrid;
        keys_map["triangles"]           = ipTriangles;
        keys_map["stars"]               = ipStars;
        keys_map["cubic"]               = ipCubic;
        keys_map["line"]                = ipLine;
        keys_map["concentric"]          = ipConcentric;
        keys_map["honeycomb"]           = ipHoneycomb;
        keys_map["3dhoneycomb"]         = ip3DHoneycomb;
        keys_map["gyroid"]              = ipGyroid;
        keys_map["hilbertcurve"]        = ipHilbertCurve;
        keys_map["archimedeanchords"]   = ipArchimedeanChords;
        keys_map["octagramspiral"]      = ipOctagramSpiral;
    }
    return keys_map;
}

template<> inline t_config_enum_values& ConfigOptionEnum<SupportMaterialPattern>::get_enum_values() {
    static t_config_enum_values keys_map;
    if (keys_map.empty()) {
        keys_map["rectilinear"]         = smpRectilinear;
        keys_map["rectilinear-grid"]    = smpRectilinearGrid;
        keys_map["honeycomb"]           = smpHoneycomb;
    }
    return keys_map;
}

template<> inline t_config_enum_values& ConfigOptionEnum<SeamPosition>::get_enum_values() {
    static t_config_enum_values keys_map;
    if (keys_map.empty()) {
        keys_map["random"]              = spRandom;
        keys_map["nearest"]             = spNearest;
        keys_map["aligned"]             = spAligned;
        keys_map["rear"]                = spRear;
    }
    return keys_map;
}

template<> inline t_config_enum_values& ConfigOptionEnum<FilamentType>::get_enum_values() {
    static t_config_enum_values keys_map;
    if (keys_map.empty()) {
        keys_map["PLA"]             = ftPLA;
        keys_map["ABS"]             = ftABS;
        keys_map["PET"]             = ftPET;
        keys_map["HIPS"]            = ftHIPS;
        keys_map["FLEX"]            = ftFLEX;
        keys_map["SCAFF"]           = ftSCAFF;
        keys_map["EDGE"]            = ftEDGE;
        keys_map["NGEN"]            = ftNGEN;
        keys_map["PVA"]             = ftPVA;
    }
    return keys_map;
}

// Defines each and every confiuration option of Slic3r, including the properties of the GUI dialogs.
// Does not store the actual values, but defines default values.
class PrintConfigDef : public ConfigDef
{
public:
    PrintConfigDef();

    static void handle_legacy(t_config_option_key &opt_key, std::string &value);
};

// The one and only global definition of SLic3r configuration options.
// This definition is constant.
extern PrintConfigDef print_config_def;

// Slic3r dynamic configuration, used to override the configuration 
// per object, per modification volume or per printing material.
// The dynamic configuration is also used to store user modifications of the print global parameters,
// so the modified configuration values may be diffed against the active configuration
// to invalidate the proper slicing resp. g-code generation processing steps.
// This object is mapped to Perl as Slic3r::Config.
class DynamicPrintConfig : public DynamicConfig
{
public:
    DynamicPrintConfig() {}
    DynamicPrintConfig(const DynamicPrintConfig &other) : DynamicConfig(other) {}

    static DynamicPrintConfig* new_from_defaults();
    static DynamicPrintConfig* new_from_defaults_keys(const std::vector<std::string> &keys);

    // Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here.
    const ConfigDef*    def() const override { return &print_config_def; }

    void                normalize();

    // Validate the PrintConfig. Returns an empty string on success, otherwise an error message is returned.
    std::string         validate();

    // Verify whether the opt_key has not been obsoleted or renamed.
    // Both opt_key and value may be modified by handle_legacy().
    // If the opt_key is no more valid in this version of Slic3r, opt_key is cleared by handle_legacy().
    // handle_legacy() is called internally by set_deserialize().
    void                handle_legacy(t_config_option_key &opt_key, std::string &value) const override
        { PrintConfigDef::handle_legacy(opt_key, value); }
};

template<typename CONFIG>
void normalize_and_apply_config(CONFIG &dst, const DynamicPrintConfig &src)
{
    DynamicPrintConfig src_normalized(src);
    src_normalized.normalize();
    dst.apply(src_normalized, true);
}

class StaticPrintConfig : public StaticConfig
{
public:
    StaticPrintConfig() {}

    // Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here.
    const ConfigDef*    def() const override { return &print_config_def; }

protected:
    // Verify whether the opt_key has not been obsoleted or renamed.
    // Both opt_key and value may be modified by handle_legacy().
    // If the opt_key is no more valid in this version of Slic3r, opt_key is cleared by handle_legacy().
    // handle_legacy() is called internally by set_deserialize().
    void                handle_legacy(t_config_option_key &opt_key, std::string &value) const override
        { PrintConfigDef::handle_legacy(opt_key, value); }

    // Internal class for keeping a dynamic map to static options.
    class StaticCacheBase
    {
    public:
        // To be called during the StaticCache setup.
        // Add one ConfigOption into m_map_name_to_offset.
        template<typename T>
        void                opt_add(const std::string &name, const char *base_ptr, const T &opt)
        {
            assert(m_map_name_to_offset.find(name) == m_map_name_to_offset.end());
            m_map_name_to_offset[name] = (const char*)&opt - base_ptr;
        }

    protected:
        std::map<std::string, ptrdiff_t>    m_map_name_to_offset;
    };

    // Parametrized by the type of the topmost class owning the options.
    template<typename T>
    class StaticCache : public StaticCacheBase
    {
    public:
        // Calling the constructor of m_defaults with 0 forces m_defaults to not run the initialization.
        StaticCache() : m_defaults(nullptr) {}
        ~StaticCache() { delete m_defaults; m_defaults = nullptr; }

        bool                initialized() const { return ! m_keys.empty(); }

        ConfigOption*       optptr(const std::string &name, T *owner) const
        {
            const auto it = m_map_name_to_offset.find(name);
            return (it == m_map_name_to_offset.end()) ? nullptr : reinterpret_cast<ConfigOption*>((char*)owner + it->second);
        }

        const ConfigOption* optptr(const std::string &name, const T *owner) const
        {
            const auto it = m_map_name_to_offset.find(name);
            return (it == m_map_name_to_offset.end()) ? nullptr : reinterpret_cast<const ConfigOption*>((const char*)owner + it->second);
        }

        const std::vector<std::string>& keys()      const { return m_keys; }
        const T&                        defaults()  const { return *m_defaults; }

        // To be called during the StaticCache setup.
        // Collect option keys from m_map_name_to_offset,
        // assign default values to m_defaults.
        void                finalize(T *defaults, const ConfigDef *defs)
        {
            assert(defs != nullptr);
            m_defaults = defaults;
            m_keys.clear();
            m_keys.reserve(m_map_name_to_offset.size());
			for (const auto &kvp : defs->options) {
				// Find the option given the option name kvp.first by an offset from (char*)m_defaults.
				ConfigOption *opt = this->optptr(kvp.first, m_defaults);
				if (opt == nullptr)
					// This option is not defined by the ConfigBase of type T.
					continue;
                m_keys.emplace_back(kvp.first);
                const ConfigOptionDef *def = defs->get(kvp.first);
                assert(def != nullptr);
                if (def->default_value != nullptr)
                    opt->set(def->default_value);
            }
        }

    private:
        T                                  *m_defaults;
        std::vector<std::string>            m_keys;
    };
};

#define STATIC_PRINT_CONFIG_CACHE_BASE(CLASS_NAME) \
public: \
    /* Overrides ConfigBase::optptr(). Find ando/or create a ConfigOption instance for a given name. */ \
    ConfigOption*            optptr(const t_config_option_key &opt_key, bool create = false) override \
        { return s_cache_##CLASS_NAME.optptr(opt_key, this); } \
    /* Overrides ConfigBase::keys(). Collect names of all configuration values maintained by this configuration store. */ \
    t_config_option_keys     keys() const override { return s_cache_##CLASS_NAME.keys(); } \
    static const CLASS_NAME& defaults() { initialize_cache(); return s_cache_##CLASS_NAME.defaults(); } \
private: \
    static void initialize_cache() \
    { \
        if (! s_cache_##CLASS_NAME.initialized()) { \
            CLASS_NAME *inst = new CLASS_NAME(1); \
            inst->initialize(s_cache_##CLASS_NAME, (const char*)inst); \
            s_cache_##CLASS_NAME.finalize(inst, inst->def()); \
        } \
    } \
    /* Cache object holding a key/option map, a list of option keys and a copy of this static config initialized with the defaults. */ \
    static StaticPrintConfig::StaticCache<CLASS_NAME> s_cache_##CLASS_NAME;

#define STATIC_PRINT_CONFIG_CACHE(CLASS_NAME) \
    STATIC_PRINT_CONFIG_CACHE_BASE(CLASS_NAME) \
public: \
    /* Public default constructor will initialize the key/option cache and the default object copy if needed. */ \
    CLASS_NAME() { initialize_cache(); *this = s_cache_##CLASS_NAME.defaults(); } \
protected: \
    /* Protected constructor to be called when compounded. */ \
    CLASS_NAME(int) {}

#define STATIC_PRINT_CONFIG_CACHE_DERIVED(CLASS_NAME) \
    STATIC_PRINT_CONFIG_CACHE_BASE(CLASS_NAME) \
public: \
    /* Overrides ConfigBase::def(). Static configuration definition. Any value stored into this ConfigBase shall have its definition here. */ \
    const ConfigDef*    def() const override { return &print_config_def; } \
    /* Handle legacy and obsoleted config keys */ \
    void                handle_legacy(t_config_option_key &opt_key, std::string &value) const override \
        { PrintConfigDef::handle_legacy(opt_key, value); }

#define OPT_PTR(KEY) cache.opt_add(#KEY, base_ptr, this->KEY)

// This object is mapped to Perl as Slic3r::Config::PrintObject.
class PrintObjectConfig : public StaticPrintConfig
{
    STATIC_PRINT_CONFIG_CACHE(PrintObjectConfig)
public:
    ConfigOptionBool                clip_multipart_objects;
    ConfigOptionBool                dont_support_bridges;
    ConfigOptionFloat               elefant_foot_compensation;
    ConfigOptionFloatOrPercent      extrusion_width;
    ConfigOptionFloatOrPercent      first_layer_height;
    ConfigOptionBool                infill_only_where_needed;
    // Force the generation of solid shells between adjacent materials/volumes.
    ConfigOptionBool                interface_shells;
    ConfigOptionFloat               layer_height;
    ConfigOptionInt                 raft_layers;
    ConfigOptionEnum<SeamPosition>  seam_position;
//    ConfigOptionFloat               seam_preferred_direction;
//    ConfigOptionFloat               seam_preferred_direction_jitter;
    ConfigOptionBool                support_material;
    // Automatic supports (generated based on support_material_threshold).
    ConfigOptionBool                support_material_auto;
    // Direction of the support pattern (in XY plane).
    ConfigOptionFloat               support_material_angle;
    ConfigOptionBool                support_material_buildplate_only;
    ConfigOptionFloat               support_material_contact_distance;
    ConfigOptionInt                 support_material_enforce_layers;
    ConfigOptionInt                 support_material_extruder;
    ConfigOptionFloatOrPercent      support_material_extrusion_width;
    ConfigOptionBool                support_material_interface_contact_loops;
    ConfigOptionInt                 support_material_interface_extruder;
    ConfigOptionInt                 support_material_interface_layers;
    // Spacing between interface lines (the hatching distance). Set zero to get a solid interface.
    ConfigOptionFloat               support_material_interface_spacing;
    ConfigOptionFloatOrPercent      support_material_interface_speed;
    ConfigOptionEnum<SupportMaterialPattern> support_material_pattern;
    // Spacing between support material lines (the hatching distance).
    ConfigOptionFloat               support_material_spacing;
    ConfigOptionFloat               support_material_speed;
    ConfigOptionBool                support_material_synchronize_layers;
    // Overhang angle threshold.
    ConfigOptionInt                 support_material_threshold;
    ConfigOptionBool                support_material_with_sheath;
    ConfigOptionFloatOrPercent      support_material_xy_spacing;
    ConfigOptionFloat               xy_size_compensation;
    ConfigOptionBool                wipe_into_objects;

protected:
    void initialize(StaticCacheBase &cache, const char *base_ptr)
    {
        OPT_PTR(clip_multipart_objects);
        OPT_PTR(dont_support_bridges);
        OPT_PTR(elefant_foot_compensation);
        OPT_PTR(extrusion_width);
        OPT_PTR(first_layer_height);
        OPT_PTR(infill_only_where_needed);
        OPT_PTR(interface_shells);
        OPT_PTR(layer_height);
        OPT_PTR(raft_layers);
        OPT_PTR(seam_position);
//        OPT_PTR(seam_preferred_direction);
//        OPT_PTR(seam_preferred_direction_jitter);
        OPT_PTR(support_material);
        OPT_PTR(support_material_auto);
        OPT_PTR(support_material_angle);
        OPT_PTR(support_material_buildplate_only);
        OPT_PTR(support_material_contact_distance);
        OPT_PTR(support_material_enforce_layers);
        OPT_PTR(support_material_interface_contact_loops);
        OPT_PTR(support_material_extruder);
        OPT_PTR(support_material_extrusion_width);
        OPT_PTR(support_material_interface_extruder);
        OPT_PTR(support_material_interface_layers);
        OPT_PTR(support_material_interface_spacing);
        OPT_PTR(support_material_interface_speed);
        OPT_PTR(support_material_pattern);
        OPT_PTR(support_material_spacing);
        OPT_PTR(support_material_speed);
        OPT_PTR(support_material_synchronize_layers);
        OPT_PTR(support_material_xy_spacing);
        OPT_PTR(support_material_threshold);
        OPT_PTR(support_material_with_sheath);
        OPT_PTR(xy_size_compensation);
        OPT_PTR(wipe_into_objects);
    }
};

// This object is mapped to Perl as Slic3r::Config::PrintRegion.
class PrintRegionConfig : public StaticPrintConfig
{
    STATIC_PRINT_CONFIG_CACHE(PrintRegionConfig)
public:
    ConfigOptionFloat               bridge_angle;
    ConfigOptionInt                 bottom_solid_layers;
    ConfigOptionFloat               bridge_flow_ratio;
    ConfigOptionFloat               bridge_speed;
    ConfigOptionBool                ensure_vertical_shell_thickness;
    ConfigOptionEnum<InfillPattern> external_fill_pattern;
    ConfigOptionFloatOrPercent      external_perimeter_extrusion_width;
    ConfigOptionFloatOrPercent      external_perimeter_speed;
    ConfigOptionBool                external_perimeters_first;
    ConfigOptionBool                extra_perimeters;
    ConfigOptionFloat               fill_angle;
    ConfigOptionPercent             fill_density;
    ConfigOptionEnum<InfillPattern> fill_pattern;
    ConfigOptionFloat               gap_fill_speed;
    ConfigOptionInt                 infill_extruder;
    ConfigOptionFloatOrPercent      infill_extrusion_width;
    ConfigOptionInt                 infill_every_layers;
    ConfigOptionFloatOrPercent      infill_overlap;
    ConfigOptionFloat               infill_speed;
    // Detect bridging perimeters
    ConfigOptionBool                overhangs;
    ConfigOptionInt                 perimeter_extruder;
    ConfigOptionFloatOrPercent      perimeter_extrusion_width;
    ConfigOptionFloat               perimeter_speed;
    // Total number of perimeters.
    ConfigOptionInt                 perimeters;
    ConfigOptionFloatOrPercent      small_perimeter_speed;
    ConfigOptionFloat               solid_infill_below_area;
    ConfigOptionInt                 solid_infill_extruder;
    ConfigOptionFloatOrPercent      solid_infill_extrusion_width;
    ConfigOptionInt                 solid_infill_every_layers;
    ConfigOptionFloatOrPercent      solid_infill_speed;
    // Detect thin walls.
    ConfigOptionBool                thin_walls;
    ConfigOptionFloatOrPercent      top_infill_extrusion_width;
    ConfigOptionInt                 top_solid_layers;
    ConfigOptionFloatOrPercent      top_solid_infill_speed;
    ConfigOptionBool                wipe_into_infill;
    
protected:
    void initialize(StaticCacheBase &cache, const char *base_ptr)
    {
        OPT_PTR(bridge_angle);
        OPT_PTR(bottom_solid_layers);
        OPT_PTR(bridge_flow_ratio);
        OPT_PTR(bridge_speed);
        OPT_PTR(ensure_vertical_shell_thickness);
        OPT_PTR(external_fill_pattern);
        OPT_PTR(external_perimeter_extrusion_width);
        OPT_PTR(external_perimeter_speed);
        OPT_PTR(external_perimeters_first);
        OPT_PTR(extra_perimeters);
        OPT_PTR(fill_angle);
        OPT_PTR(fill_density);
        OPT_PTR(fill_pattern);
        OPT_PTR(gap_fill_speed);
        OPT_PTR(infill_extruder);
        OPT_PTR(infill_extrusion_width);
        OPT_PTR(infill_every_layers);
        OPT_PTR(infill_overlap);
        OPT_PTR(infill_speed);
        OPT_PTR(overhangs);
        OPT_PTR(perimeter_extruder);
        OPT_PTR(perimeter_extrusion_width);
        OPT_PTR(perimeter_speed);
        OPT_PTR(perimeters);
        OPT_PTR(small_perimeter_speed);
        OPT_PTR(solid_infill_below_area);
        OPT_PTR(solid_infill_extruder);
        OPT_PTR(solid_infill_extrusion_width);
        OPT_PTR(solid_infill_every_layers);
        OPT_PTR(solid_infill_speed);
        OPT_PTR(thin_walls);
        OPT_PTR(top_infill_extrusion_width);
        OPT_PTR(top_solid_infill_speed);
        OPT_PTR(top_solid_layers);
        OPT_PTR(wipe_into_infill);
    }
};

class MachineEnvelopeConfig : public StaticPrintConfig
{
    STATIC_PRINT_CONFIG_CACHE(MachineEnvelopeConfig)
public:
    // M201 X... Y... Z... E... [mm/sec^2]
    ConfigOptionFloats              machine_max_acceleration_x;
    ConfigOptionFloats              machine_max_acceleration_y;
    ConfigOptionFloats              machine_max_acceleration_z;
    ConfigOptionFloats              machine_max_acceleration_e;
    // M203 X... Y... Z... E... [mm/sec]
    ConfigOptionFloats              machine_max_feedrate_x;
    ConfigOptionFloats              machine_max_feedrate_y;
    ConfigOptionFloats              machine_max_feedrate_z;
    ConfigOptionFloats              machine_max_feedrate_e;
    // M204 S... [mm/sec^2]
    ConfigOptionFloats              machine_max_acceleration_extruding;
    // M204 T... [mm/sec^2]
    ConfigOptionFloats              machine_max_acceleration_retracting;
    // M205 X... Y... Z... E... [mm/sec]
    ConfigOptionFloats              machine_max_jerk_x;
    ConfigOptionFloats              machine_max_jerk_y;
    ConfigOptionFloats              machine_max_jerk_z;
    ConfigOptionFloats              machine_max_jerk_e;
    // M205 T... [mm/sec]
    ConfigOptionFloats              machine_min_travel_rate;
    // M205 S... [mm/sec]
    ConfigOptionFloats              machine_min_extruding_rate;

protected:
    void initialize(StaticCacheBase &cache, const char *base_ptr)
    {
        OPT_PTR(machine_max_acceleration_x);
        OPT_PTR(machine_max_acceleration_y);
        OPT_PTR(machine_max_acceleration_z);
        OPT_PTR(machine_max_acceleration_e);
        OPT_PTR(machine_max_feedrate_x);
        OPT_PTR(machine_max_feedrate_y);
        OPT_PTR(machine_max_feedrate_z);
        OPT_PTR(machine_max_feedrate_e);
        OPT_PTR(machine_max_acceleration_extruding);
        OPT_PTR(machine_max_acceleration_retracting);
        OPT_PTR(machine_max_jerk_x);
        OPT_PTR(machine_max_jerk_y);
        OPT_PTR(machine_max_jerk_z);
        OPT_PTR(machine_max_jerk_e);
        OPT_PTR(machine_min_travel_rate);
        OPT_PTR(machine_min_extruding_rate);
    }
};

// This object is mapped to Perl as Slic3r::Config::GCode.
class GCodeConfig : public StaticPrintConfig
{
    STATIC_PRINT_CONFIG_CACHE(GCodeConfig)
public:
    ConfigOptionString              before_layer_gcode;
    ConfigOptionString              between_objects_gcode;
    ConfigOptionFloats              deretract_speed;
    ConfigOptionString              end_gcode;
    ConfigOptionStrings             end_filament_gcode;
    ConfigOptionString              extrusion_axis;
    ConfigOptionFloats              extrusion_multiplier;
    ConfigOptionFloats              filament_diameter;
    ConfigOptionFloats              filament_density;
    ConfigOptionStrings             filament_type;
    ConfigOptionBools               filament_soluble;
    ConfigOptionFloats              filament_cost;
    ConfigOptionFloats              filament_max_volumetric_speed;
    ConfigOptionFloats              filament_loading_speed;
    ConfigOptionFloats              filament_loading_speed_start;
    ConfigOptionFloats              filament_load_time;
    ConfigOptionFloats              filament_unloading_speed;
    ConfigOptionFloats              filament_unloading_speed_start;
    ConfigOptionFloats              filament_toolchange_delay;
    ConfigOptionFloats              filament_unload_time;
    ConfigOptionInts                filament_cooling_moves;
    ConfigOptionFloats              filament_cooling_initial_speed;
    ConfigOptionFloats              filament_minimal_purge_on_wipe_tower;
    ConfigOptionFloats              filament_cooling_final_speed;
    ConfigOptionStrings             filament_ramming_parameters;
    ConfigOptionBool                gcode_comments;
    ConfigOptionEnum<GCodeFlavor>   gcode_flavor;
    ConfigOptionString              layer_gcode;
    ConfigOptionFloat               max_print_speed;
    ConfigOptionFloat               max_volumetric_speed;
    ConfigOptionFloat               max_volumetric_extrusion_rate_slope_positive;
    ConfigOptionFloat               max_volumetric_extrusion_rate_slope_negative;
    ConfigOptionPercents            retract_before_wipe;
    ConfigOptionFloats              retract_length;
    ConfigOptionFloats              retract_length_toolchange;
    ConfigOptionFloats              retract_lift;
    ConfigOptionFloats              retract_lift_above;
    ConfigOptionFloats              retract_lift_below;
    ConfigOptionFloats              retract_restart_extra;
    ConfigOptionFloats              retract_restart_extra_toolchange;
    ConfigOptionFloats              retract_speed;
    ConfigOptionString              start_gcode;
    ConfigOptionStrings             start_filament_gcode;
    ConfigOptionBool                single_extruder_multi_material;
    ConfigOptionBool                single_extruder_multi_material_priming;
    ConfigOptionString              toolchange_gcode;
    ConfigOptionFloat               travel_speed;
    ConfigOptionBool                use_firmware_retraction;
    ConfigOptionBool                use_relative_e_distances;
    ConfigOptionBool                use_volumetric_e;
    ConfigOptionBool                variable_layer_height;
    ConfigOptionFloat               cooling_tube_retraction;
    ConfigOptionFloat               cooling_tube_length;
    ConfigOptionFloat               parking_pos_retraction;
    ConfigOptionBool                remaining_times;
    ConfigOptionBool                silent_mode;
    ConfigOptionFloat               extra_loading_move;

    std::string get_extrusion_axis() const
    {
        return
            ((this->gcode_flavor.value == gcfMach3) || (this->gcode_flavor.value == gcfMachinekit)) ? "A" :
            (this->gcode_flavor.value == gcfNoExtrusion) ? "" : this->extrusion_axis.value;
    }

protected:
    void initialize(StaticCacheBase &cache, const char *base_ptr)
    {
        OPT_PTR(before_layer_gcode);
        OPT_PTR(between_objects_gcode);
        OPT_PTR(deretract_speed);
        OPT_PTR(end_gcode);
        OPT_PTR(end_filament_gcode);
        OPT_PTR(extrusion_axis);
        OPT_PTR(extrusion_multiplier);
        OPT_PTR(filament_diameter);
        OPT_PTR(filament_density);
        OPT_PTR(filament_type);
        OPT_PTR(filament_soluble);
        OPT_PTR(filament_cost);
        OPT_PTR(filament_max_volumetric_speed);
        OPT_PTR(filament_loading_speed);
        OPT_PTR(filament_loading_speed_start);
        OPT_PTR(filament_load_time);
        OPT_PTR(filament_unloading_speed);
        OPT_PTR(filament_unloading_speed_start);
        OPT_PTR(filament_unload_time);
        OPT_PTR(filament_toolchange_delay);
        OPT_PTR(filament_cooling_moves);
        OPT_PTR(filament_cooling_initial_speed);
        OPT_PTR(filament_minimal_purge_on_wipe_tower);
        OPT_PTR(filament_cooling_final_speed);
        OPT_PTR(filament_ramming_parameters);
        OPT_PTR(gcode_comments);
        OPT_PTR(gcode_flavor);
        OPT_PTR(layer_gcode);
        OPT_PTR(max_print_speed);
        OPT_PTR(max_volumetric_speed);
        OPT_PTR(max_volumetric_extrusion_rate_slope_positive);
        OPT_PTR(max_volumetric_extrusion_rate_slope_negative);
        OPT_PTR(retract_before_wipe);
        OPT_PTR(retract_length);
        OPT_PTR(retract_length_toolchange);
        OPT_PTR(retract_lift);
        OPT_PTR(retract_lift_above);
        OPT_PTR(retract_lift_below);
        OPT_PTR(retract_restart_extra);
        OPT_PTR(retract_restart_extra_toolchange);
        OPT_PTR(retract_speed);
        OPT_PTR(single_extruder_multi_material);
        OPT_PTR(single_extruder_multi_material_priming);
        OPT_PTR(start_gcode);
        OPT_PTR(start_filament_gcode);
        OPT_PTR(toolchange_gcode);
        OPT_PTR(travel_speed);
        OPT_PTR(use_firmware_retraction);
        OPT_PTR(use_relative_e_distances);
        OPT_PTR(use_volumetric_e);
        OPT_PTR(variable_layer_height);
        OPT_PTR(cooling_tube_retraction);
        OPT_PTR(cooling_tube_length);
        OPT_PTR(parking_pos_retraction);
        OPT_PTR(remaining_times);
        OPT_PTR(silent_mode);
        OPT_PTR(extra_loading_move);
    }
};

// This object is mapped to Perl as Slic3r::Config::Print.
class PrintConfig : public MachineEnvelopeConfig, public GCodeConfig
{
    STATIC_PRINT_CONFIG_CACHE_DERIVED(PrintConfig)
    PrintConfig() : GCodeConfig(0) { initialize_cache(); *this = s_cache_PrintConfig.defaults(); }
public:
    double                          min_object_distance() const;
    static double                   min_object_distance(const ConfigBase *config);

    ConfigOptionBool                avoid_crossing_perimeters;
    ConfigOptionPoints              bed_shape;
    ConfigOptionInts                bed_temperature;
    ConfigOptionFloat               bridge_acceleration;
    ConfigOptionInts                bridge_fan_speed;
    ConfigOptionFloat               brim_width;
    ConfigOptionBool                complete_objects;
    ConfigOptionBools               cooling;
    ConfigOptionFloat               default_acceleration;
    ConfigOptionInts                disable_fan_first_layers;
    ConfigOptionFloat               duplicate_distance;
    ConfigOptionFloat               extruder_clearance_height;
    ConfigOptionFloat               extruder_clearance_radius;
    ConfigOptionStrings             extruder_colour;
    ConfigOptionPoints              extruder_offset;
    ConfigOptionBools               fan_always_on;
    ConfigOptionInts                fan_below_layer_time;
    ConfigOptionStrings             filament_colour;
    ConfigOptionStrings             filament_notes;
    ConfigOptionFloat               first_layer_acceleration;
    ConfigOptionInts                first_layer_bed_temperature;
    ConfigOptionFloatOrPercent      first_layer_extrusion_width;
    ConfigOptionFloatOrPercent      first_layer_speed;
    ConfigOptionInts                first_layer_temperature;
    ConfigOptionFloat               infill_acceleration;
    ConfigOptionBool                infill_first;
    ConfigOptionInts                max_fan_speed;
    ConfigOptionFloats              max_layer_height;
    ConfigOptionInts                min_fan_speed;
    ConfigOptionFloats              min_layer_height;
    ConfigOptionFloat               max_print_height;
    ConfigOptionFloats              min_print_speed;
    ConfigOptionFloat               min_skirt_length;
    ConfigOptionString              notes;
    ConfigOptionFloats              nozzle_diameter;
    ConfigOptionBool                only_retract_when_crossing_perimeters;
    ConfigOptionBool                ooze_prevention;
    ConfigOptionString              output_filename_format;
    ConfigOptionFloat               perimeter_acceleration;
    ConfigOptionStrings             post_process;
    ConfigOptionString              printer_model;
    ConfigOptionString              printer_notes;
    ConfigOptionFloat               resolution;
    ConfigOptionFloats              retract_before_travel;
    ConfigOptionBools               retract_layer_change;
    ConfigOptionFloat               skirt_distance;
    ConfigOptionInt                 skirt_height;
    ConfigOptionInt                 skirts;
    ConfigOptionInts                slowdown_below_layer_time;
    ConfigOptionBool                spiral_vase;
    ConfigOptionInt                 standby_temperature_delta;
    ConfigOptionInts                temperature;
    ConfigOptionInt                 threads;
    ConfigOptionBools               wipe;
    ConfigOptionBool                wipe_tower;
    ConfigOptionFloat               wipe_tower_x;
    ConfigOptionFloat               wipe_tower_y;
    ConfigOptionFloat               wipe_tower_width;
    ConfigOptionFloat               wipe_tower_per_color_wipe;
    ConfigOptionFloat               wipe_tower_rotation_angle;
    ConfigOptionFloat               wipe_tower_bridging;
    ConfigOptionFloats              wiping_volumes_matrix;
    ConfigOptionFloats              wiping_volumes_extruders;
    ConfigOptionFloat               z_offset;
    
protected:
    PrintConfig(int) : GCodeConfig(1) {}
    void initialize(StaticCacheBase &cache, const char *base_ptr)
    {
        this->MachineEnvelopeConfig::initialize(cache, base_ptr);
        this->GCodeConfig::initialize(cache, base_ptr);
        OPT_PTR(avoid_crossing_perimeters);
        OPT_PTR(bed_shape);
        OPT_PTR(bed_temperature);
        OPT_PTR(bridge_acceleration);
        OPT_PTR(bridge_fan_speed);
        OPT_PTR(brim_width);
        OPT_PTR(complete_objects);
        OPT_PTR(cooling);
        OPT_PTR(default_acceleration);
        OPT_PTR(disable_fan_first_layers);
        OPT_PTR(duplicate_distance);
        OPT_PTR(extruder_clearance_height);
        OPT_PTR(extruder_clearance_radius);
        OPT_PTR(extruder_colour);
        OPT_PTR(extruder_offset);
        OPT_PTR(fan_always_on);
        OPT_PTR(fan_below_layer_time);
        OPT_PTR(filament_colour);
        OPT_PTR(filament_notes);
        OPT_PTR(first_layer_acceleration);
        OPT_PTR(first_layer_bed_temperature);
        OPT_PTR(first_layer_extrusion_width);
        OPT_PTR(first_layer_speed);
        OPT_PTR(first_layer_temperature);
        OPT_PTR(infill_acceleration);
        OPT_PTR(infill_first);
        OPT_PTR(max_fan_speed);
        OPT_PTR(max_layer_height);
        OPT_PTR(min_fan_speed);
        OPT_PTR(min_layer_height);
        OPT_PTR(max_print_height);
        OPT_PTR(min_print_speed);
        OPT_PTR(min_skirt_length);
        OPT_PTR(notes);
        OPT_PTR(nozzle_diameter);
        OPT_PTR(only_retract_when_crossing_perimeters);
        OPT_PTR(ooze_prevention);
        OPT_PTR(output_filename_format);
        OPT_PTR(perimeter_acceleration);
        OPT_PTR(post_process);
        OPT_PTR(printer_model);
        OPT_PTR(printer_notes);
        OPT_PTR(resolution);
        OPT_PTR(retract_before_travel);
        OPT_PTR(retract_layer_change);
        OPT_PTR(skirt_distance);
        OPT_PTR(skirt_height);
        OPT_PTR(skirts);
        OPT_PTR(slowdown_below_layer_time);
        OPT_PTR(spiral_vase);
        OPT_PTR(standby_temperature_delta);
        OPT_PTR(temperature);
        OPT_PTR(threads);
        OPT_PTR(wipe);
        OPT_PTR(wipe_tower);
        OPT_PTR(wipe_tower_x);
        OPT_PTR(wipe_tower_y);
        OPT_PTR(wipe_tower_width);
        OPT_PTR(wipe_tower_per_color_wipe);
        OPT_PTR(wipe_tower_rotation_angle);
        OPT_PTR(wipe_tower_bridging);
        OPT_PTR(wiping_volumes_matrix);
        OPT_PTR(wiping_volumes_extruders);
        OPT_PTR(z_offset);
    }
};

class HostConfig : public StaticPrintConfig
{
    STATIC_PRINT_CONFIG_CACHE(HostConfig)
public:
    ConfigOptionEnum<PrintHostType> host_type;
    ConfigOptionString              print_host;
    ConfigOptionString              printhost_apikey;
    ConfigOptionString              printhost_cafile;
    ConfigOptionString              serial_port;
    ConfigOptionInt                 serial_speed;
    
protected:
    void initialize(StaticCacheBase &cache, const char *base_ptr)
    {
        OPT_PTR(host_type);
        OPT_PTR(print_host);
        OPT_PTR(printhost_apikey);
        OPT_PTR(printhost_cafile);
        OPT_PTR(serial_port);
        OPT_PTR(serial_speed);
    }
};

// This object is mapped to Perl as Slic3r::Config::Full.
class FullPrintConfig : 
    public PrintObjectConfig, 
    public PrintRegionConfig,
    public PrintConfig,
    public HostConfig
{
    STATIC_PRINT_CONFIG_CACHE_DERIVED(FullPrintConfig)
	FullPrintConfig() : PrintObjectConfig(0), PrintRegionConfig(0), PrintConfig(0), HostConfig(0) { initialize_cache(); *this = s_cache_FullPrintConfig.defaults(); }

public:
    // Validate the FullPrintConfig. Returns an empty string on success, otherwise an error message is returned.
    std::string                 validate();

protected:
    // Protected constructor to be called to initialize ConfigCache::m_default.
    FullPrintConfig(int) : PrintObjectConfig(0), PrintRegionConfig(0), PrintConfig(0), HostConfig(0) {}
    void initialize(StaticCacheBase &cache, const char *base_ptr)
    {
        this->PrintObjectConfig::initialize(cache, base_ptr);
        this->PrintRegionConfig::initialize(cache, base_ptr);
        this->PrintConfig      ::initialize(cache, base_ptr);
        this->HostConfig       ::initialize(cache, base_ptr);
    }
};

#undef STATIC_PRINT_CONFIG_CACHE
#undef STATIC_PRINT_CONFIG_CACHE_BASE
#undef STATIC_PRINT_CONFIG_CACHE_DERIVED
#undef OPT_PTR

}

#endif
