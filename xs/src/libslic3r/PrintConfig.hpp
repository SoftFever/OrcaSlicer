// Configuration store of Slic3r.
//
// The configuration store is either static or dynamic.
// DynamicPrintConfig is used mainly at the user interface. while the StaticPrintConfig is used
// during the slicing and the g-code generation.
//
// The classes derived from StaticPrintConfig form a following hierarchy.
// Virtual inheritance is used for some of the parent objects.
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

#define OPT_PTR(KEY) if (opt_key == #KEY) return &this->KEY

namespace Slic3r {

enum GCodeFlavor {
    gcfRepRap, gcfTeacup, gcfMakerWare, gcfSailfish, gcfMach3, gcfMachinekit, gcfNoExtrusion, gcfSmoothie, gcfRepetier,
};

enum InfillPattern {
    ipRectilinear, ipGrid, ipTriangles, ipStars, ipCubic, ipLine, ipConcentric, ipHoneycomb, ip3DHoneycomb,
    ipHilbertCurve, ipArchimedeanChords, ipOctagramSpiral,
};

enum SupportMaterialPattern {
    smpRectilinear, smpRectilinearGrid, smpHoneycomb, smpPillars,
};

enum SeamPosition {
    spRandom, spNearest, spAligned, spRear
};

enum FilamentType {
    ftPLA, ftABS, ftPET, ftHIPS, ftFLEX, ftSCAFF, ftEDGE, ftNGEN, ftPVA
};

template<> inline t_config_enum_values ConfigOptionEnum<GCodeFlavor>::get_enum_values() {
    t_config_enum_values keys_map;
    keys_map["reprap"]          = gcfRepRap;
    keys_map["repetier"]        = gcfRepetier;
    keys_map["teacup"]          = gcfTeacup;
    keys_map["makerware"]       = gcfMakerWare;
    keys_map["sailfish"]        = gcfSailfish;
    keys_map["smoothie"]        = gcfSmoothie;
    keys_map["mach3"]           = gcfMach3;
    keys_map["machinekit"]      = gcfMachinekit;
    keys_map["no-extrusion"]    = gcfNoExtrusion;
    return keys_map;
}

template<> inline t_config_enum_values ConfigOptionEnum<InfillPattern>::get_enum_values() {
    t_config_enum_values keys_map;
    keys_map["rectilinear"]         = ipRectilinear;
    keys_map["grid"]                = ipGrid;
    keys_map["triangles"]           = ipTriangles;
    keys_map["stars"]               = ipStars;
    keys_map["cubic"]               = ipCubic;
    keys_map["line"]                = ipLine;
    keys_map["concentric"]          = ipConcentric;
    keys_map["honeycomb"]           = ipHoneycomb;
    keys_map["3dhoneycomb"]         = ip3DHoneycomb;
    keys_map["hilbertcurve"]        = ipHilbertCurve;
    keys_map["archimedeanchords"]   = ipArchimedeanChords;
    keys_map["octagramspiral"]      = ipOctagramSpiral;
    return keys_map;
}

template<> inline t_config_enum_values ConfigOptionEnum<SupportMaterialPattern>::get_enum_values() {
    t_config_enum_values keys_map;
    keys_map["rectilinear"]         = smpRectilinear;
    keys_map["rectilinear-grid"]    = smpRectilinearGrid;
    keys_map["honeycomb"]           = smpHoneycomb;
    keys_map["pillars"]             = smpPillars;
    return keys_map;
}

template<> inline t_config_enum_values ConfigOptionEnum<SeamPosition>::get_enum_values() {
    t_config_enum_values keys_map;
    keys_map["random"]              = spRandom;
    keys_map["nearest"]             = spNearest;
    keys_map["aligned"]             = spAligned;
    keys_map["rear"]                = spRear;
    return keys_map;
}

template<> inline t_config_enum_values ConfigOptionEnum<FilamentType>::get_enum_values() {
    t_config_enum_values keys_map;
    keys_map["PLA"]             = ftPLA;
    keys_map["ABS"]             = ftABS;
    keys_map["PET"]             = ftPET;
    keys_map["HIPS"]            = ftHIPS;
    keys_map["FLEX"]            = ftFLEX;
    keys_map["SCAFF"]           = ftSCAFF;
    keys_map["EDGE"]            = ftEDGE;
    keys_map["NGEN"]            = ftNGEN;
    keys_map["PVA"]             = ftPVA;
    return keys_map;
}

// Defines each and every confiuration option of Slic3r, including the properties of the GUI dialogs.
// Does not store the actual values, but defines default values.
class PrintConfigDef : public ConfigDef
{
    public:
    PrintConfigDef();
};

// The one and only global definition of SLic3r configuration options.
// This definition is constant.
extern PrintConfigDef print_config_def;

// Slic3r configuration storage with print_config_def assigned.
class PrintConfigBase : public virtual ConfigBase
{
public:
    PrintConfigBase() {
        this->def = &print_config_def;
    };
    
    bool set_deserialize(t_config_option_key opt_key, std::string str, bool append = false);

    double min_object_distance() const;

protected:
    void _handle_legacy(t_config_option_key &opt_key, std::string &value) const;
};

// Slic3r dynamic configuration, used to override the configuration 
// per object, per modification volume or per printing material.
// The dynamic configuration is also used to store user modifications of the print global parameters,
// so the modified configuration values may be diffed against the active configuration
// to invalidate the proper slicing resp. g-code generation processing steps.
// This object is mapped to Perl as Slic3r::Config.
class DynamicPrintConfig : public PrintConfigBase, public DynamicConfig
{
    public:
    DynamicPrintConfig() : PrintConfigBase(), DynamicConfig() {};
    void normalize();
};

template<typename CONFIG>
void normalize_and_apply_config(CONFIG &dst, const DynamicPrintConfig &src)
{
    DynamicPrintConfig src_normalized = src;
    src_normalized.normalize();
    dst.apply(src_normalized, true);
}

class StaticPrintConfig : public PrintConfigBase, public StaticConfig
{
    public:
    StaticPrintConfig() : PrintConfigBase(), StaticConfig() {};
};

// This object is mapped to Perl as Slic3r::Config::PrintObject.
class PrintObjectConfig : public virtual StaticPrintConfig
{
public:
    ConfigOptionBool                clip_multipart_objects;
    ConfigOptionBool                dont_support_bridges;
    ConfigOptionFloat               elefant_foot_compensation;
    ConfigOptionFloatOrPercent      extrusion_width;
    ConfigOptionFloatOrPercent      first_layer_height;
    ConfigOptionBool                infill_only_where_needed;
    ConfigOptionBool                interface_shells;
    ConfigOptionFloat               layer_height;
    ConfigOptionInt                 raft_layers;
    ConfigOptionEnum<SeamPosition>  seam_position;
//    ConfigOptionFloat               seam_preferred_direction;
//    ConfigOptionFloat               seam_preferred_direction_jitter;
    ConfigOptionBool                support_material;
    ConfigOptionFloat               support_material_angle;
    ConfigOptionBool                support_material_buildplate_only;
    ConfigOptionFloat               support_material_contact_distance;
    ConfigOptionInt                 support_material_enforce_layers;
    ConfigOptionInt                 support_material_extruder;
    ConfigOptionFloatOrPercent      support_material_extrusion_width;
    ConfigOptionBool                support_material_interface_contact_loops;
    ConfigOptionInt                 support_material_interface_extruder;
    ConfigOptionInt                 support_material_interface_layers;
    ConfigOptionFloat               support_material_interface_spacing;
    ConfigOptionFloatOrPercent      support_material_interface_speed;
    ConfigOptionEnum<SupportMaterialPattern> support_material_pattern;
    ConfigOptionFloat               support_material_spacing;
    ConfigOptionFloat               support_material_speed;
    ConfigOptionBool                support_material_synchronize_layers;
    ConfigOptionInt                 support_material_threshold;
    ConfigOptionBool                support_material_with_sheath;
    ConfigOptionFloatOrPercent      support_material_xy_spacing;
    ConfigOptionFloat               xy_size_compensation;
    
    PrintObjectConfig(bool initialize = true) : StaticPrintConfig() {
        if (initialize)
            this->set_defaults();
    }

    virtual ConfigOption* optptr(const t_config_option_key &opt_key, bool create = false) {
        UNUSED(create);
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
        
        return NULL;
    };
};

// This object is mapped to Perl as Slic3r::Config::PrintRegion.
class PrintRegionConfig : public virtual StaticPrintConfig
{
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
    ConfigOptionBool                overhangs;
    ConfigOptionInt                 perimeter_extruder;
    ConfigOptionFloatOrPercent      perimeter_extrusion_width;
    ConfigOptionFloat               perimeter_speed;
    ConfigOptionInt                 perimeters;
    ConfigOptionFloatOrPercent      small_perimeter_speed;
    ConfigOptionFloat               solid_infill_below_area;
    ConfigOptionInt                 solid_infill_extruder;
    ConfigOptionFloatOrPercent      solid_infill_extrusion_width;
    ConfigOptionInt                 solid_infill_every_layers;
    ConfigOptionFloatOrPercent      solid_infill_speed;
    ConfigOptionBool                thin_walls;
    ConfigOptionFloatOrPercent      top_infill_extrusion_width;
    ConfigOptionInt                 top_solid_layers;
    ConfigOptionFloatOrPercent      top_solid_infill_speed;
    
    PrintRegionConfig(bool initialize = true) : StaticPrintConfig() {
        if (initialize)
            this->set_defaults();
    }

    virtual ConfigOption* optptr(const t_config_option_key &opt_key, bool create = false) {
        UNUSED(create);
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
        
        return NULL;
    };
};

// This object is mapped to Perl as Slic3r::Config::GCode.
class GCodeConfig : public virtual StaticPrintConfig
{
public:
    ConfigOptionString              before_layer_gcode;
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
    ConfigOptionString              toolchange_gcode;
    ConfigOptionFloat               travel_speed;
    ConfigOptionBool                use_firmware_retraction;
    ConfigOptionBool                use_relative_e_distances;
    ConfigOptionBool                use_volumetric_e;
    ConfigOptionBool                variable_layer_height;
    
    GCodeConfig(bool initialize = true) : StaticPrintConfig() {
        if (initialize)
            this->set_defaults();
    }
    
    virtual ConfigOption* optptr(const t_config_option_key &opt_key, bool create = false) {
        UNUSED(create);
        OPT_PTR(before_layer_gcode);
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
        OPT_PTR(start_gcode);
        OPT_PTR(start_filament_gcode);
        OPT_PTR(toolchange_gcode);
        OPT_PTR(travel_speed);
        OPT_PTR(use_firmware_retraction);
        OPT_PTR(use_relative_e_distances);
        OPT_PTR(use_volumetric_e);
        OPT_PTR(variable_layer_height);
        return NULL;
    };
    
    std::string get_extrusion_axis() const
    {
        if ((this->gcode_flavor.value == gcfMach3) || (this->gcode_flavor.value == gcfMachinekit)) {
            return "A";
        } else if (this->gcode_flavor.value == gcfNoExtrusion) {
            return "";
        } else {
            return this->extrusion_axis.value;
        }
    };
};

// This object is mapped to Perl as Slic3r::Config::Print.
class PrintConfig : public GCodeConfig
{
public:
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
    ConfigOptionFloats              min_print_speed;
    ConfigOptionFloat               min_skirt_length;
    ConfigOptionString              notes;
    ConfigOptionFloats              nozzle_diameter;
    ConfigOptionBool                only_retract_when_crossing_perimeters;
    ConfigOptionBool                ooze_prevention;
    ConfigOptionString              output_filename_format;
    ConfigOptionFloat               perimeter_acceleration;
    ConfigOptionStrings             post_process;
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
    ConfigOptionFloat               z_offset;
    
    PrintConfig(bool initialize = true) : GCodeConfig(false) {
        if (initialize)
            this->set_defaults();
    }

    virtual ConfigOption* optptr(const t_config_option_key &opt_key, bool create = false) {
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
        OPT_PTR(min_print_speed);
        OPT_PTR(min_skirt_length);
        OPT_PTR(notes);
        OPT_PTR(nozzle_diameter);
        OPT_PTR(only_retract_when_crossing_perimeters);
        OPT_PTR(ooze_prevention);
        OPT_PTR(output_filename_format);
        OPT_PTR(perimeter_acceleration);
        OPT_PTR(post_process);
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
        OPT_PTR(z_offset);
        
        // look in parent class
        ConfigOption* opt;
        if ((opt = GCodeConfig::optptr(opt_key, create)) != NULL) return opt;
        
        return NULL;
    };
};

class HostConfig : public virtual StaticPrintConfig
{
public:
    ConfigOptionString              octoprint_host;
    ConfigOptionString              octoprint_apikey;
    ConfigOptionString              serial_port;
    ConfigOptionInt                 serial_speed;
    
    HostConfig(bool initialize = true) : StaticPrintConfig() {
        if (initialize)
            this->set_defaults();
    }

    virtual ConfigOption* optptr(const t_config_option_key &opt_key, bool create = false) {
        UNUSED(create);
        OPT_PTR(octoprint_host);
        OPT_PTR(octoprint_apikey);
        OPT_PTR(serial_port);
        OPT_PTR(serial_speed);
        
        return NULL;
    };
};

// This object is mapped to Perl as Slic3r::Config::Full.
class FullPrintConfig
    : public PrintObjectConfig, public PrintRegionConfig, public PrintConfig, public HostConfig
{
    public:
    FullPrintConfig(bool initialize = true) :
        PrintObjectConfig(false),
        PrintRegionConfig(false), 
        PrintConfig(false), 
        HostConfig(false)
    {
        if (initialize)
            this->set_defaults();
    }

    virtual ConfigOption* optptr(const t_config_option_key &opt_key, bool create = false) {
        ConfigOption* opt;
        if ((opt = PrintObjectConfig::optptr(opt_key, create)) != NULL) return opt;
        if ((opt = PrintRegionConfig::optptr(opt_key, create)) != NULL) return opt;
        if ((opt = PrintConfig::optptr(opt_key, create)) != NULL) return opt;
        if ((opt = HostConfig::optptr(opt_key, create)) != NULL) return opt;
        return NULL;
    };
};

}

#endif
