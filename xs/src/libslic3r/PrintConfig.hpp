#ifndef slic3r_PrintConfig_hpp_
#define slic3r_PrintConfig_hpp_

#include "Config.hpp"

namespace Slic3r {

enum GCodeFlavor {
    gcfRepRap, gcfTeacup, gcfMakerWare, gcfSailfish, gcfMach3, gcfMachinekit, gcfNoExtrusion,
};

enum InfillPattern {
    ipRectilinear, ipLine, ipConcentric, ipHoneycomb, ip3DHoneycomb,
    ipHilbertCurve, ipArchimedeanChords, ipOctagramSpiral,
};

enum SupportMaterialPattern {
    smpRectilinear, smpRectilinearGrid, smpHoneycomb, smpPillars,
};

enum SeamPosition {
    spRandom, spNearest, spAligned
};

template<> inline t_config_enum_values ConfigOptionEnum<GCodeFlavor>::get_enum_values() {
    t_config_enum_values keys_map;
    keys_map["reprap"]          = gcfRepRap;
    keys_map["teacup"]          = gcfTeacup;
    keys_map["makerware"]       = gcfMakerWare;
    keys_map["sailfish"]        = gcfSailfish;
    keys_map["mach3"]           = gcfMach3;
    keys_map["machinekit"]      = gcfMachinekit;
    keys_map["no-extrusion"]    = gcfNoExtrusion;
    return keys_map;
}

template<> inline t_config_enum_values ConfigOptionEnum<InfillPattern>::get_enum_values() {
    t_config_enum_values keys_map;
    keys_map["rectilinear"]         = ipRectilinear;
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
    return keys_map;
}

class PrintConfigDef
{
    public:
    static t_optiondef_map def;
    
    static t_optiondef_map build_def();
};

class DynamicPrintConfig : public DynamicConfig
{
    public:
    DynamicPrintConfig() {
        this->def = &PrintConfigDef::def;
    };
    
    void normalize();
};

class StaticPrintConfig : public virtual StaticConfig
{
    public:
    StaticPrintConfig() {
        this->def = &PrintConfigDef::def;
    };
};

class PrintObjectConfig : public virtual StaticPrintConfig
{
    public:
    ConfigOptionBool                dont_support_bridges;
    ConfigOptionFloatOrPercent      extrusion_width;
    ConfigOptionFloatOrPercent      first_layer_height;
    ConfigOptionBool                infill_only_where_needed;
    ConfigOptionBool                interface_shells;
    ConfigOptionFloat               layer_height;
    ConfigOptionInt                 raft_layers;
    ConfigOptionEnum<SeamPosition>  seam_position;
    ConfigOptionBool                support_material;
    ConfigOptionInt                 support_material_angle;
    ConfigOptionFloat               support_material_contact_distance;
    ConfigOptionInt                 support_material_enforce_layers;
    ConfigOptionInt                 support_material_extruder;
    ConfigOptionFloatOrPercent      support_material_extrusion_width;
    ConfigOptionInt                 support_material_interface_extruder;
    ConfigOptionInt                 support_material_interface_layers;
    ConfigOptionFloat               support_material_interface_spacing;
    ConfigOptionFloatOrPercent      support_material_interface_speed;
    ConfigOptionEnum<SupportMaterialPattern> support_material_pattern;
    ConfigOptionFloat               support_material_spacing;
    ConfigOptionFloat               support_material_speed;
    ConfigOptionInt                 support_material_threshold;
    ConfigOptionFloat               xy_size_compensation;
    
    PrintObjectConfig() : StaticPrintConfig() {
        this->dont_support_bridges.value                         = true;
        this->extrusion_width.value                              = 0;
        this->extrusion_width.percent                            = false;
        this->first_layer_height.value                           = 0.35;
        this->first_layer_height.percent                         = false;
        this->infill_only_where_needed.value                     = false;
        this->interface_shells.value                             = false;
        this->layer_height.value                                 = 0.3;
        this->raft_layers.value                                  = 0;
        this->seam_position.value                                = spAligned;
        this->support_material.value                             = false;
        this->support_material_angle.value                       = 0;
        this->support_material_contact_distance.value            = 0.2;
        this->support_material_enforce_layers.value              = 0;
        this->support_material_extruder.value                    = 1;
        this->support_material_extrusion_width.value             = 0;
        this->support_material_extrusion_width.percent           = false;
        this->support_material_interface_extruder.value          = 1;
        this->support_material_interface_layers.value            = 3;
        this->support_material_interface_spacing.value           = 0;
        this->support_material_interface_speed.value             = 100;
        this->support_material_interface_speed.percent           = true;
        this->support_material_pattern.value                     = smpPillars;
        this->support_material_spacing.value                     = 2.5;
        this->support_material_speed.value                       = 60;
        this->support_material_threshold.value                   = 0;
        this->xy_size_compensation.value                         = 0;
    };
    
    ConfigOption* option(const t_config_option_key opt_key, bool create = false) {
        if (opt_key == "dont_support_bridges")                       return &this->dont_support_bridges;
        if (opt_key == "extrusion_width")                            return &this->extrusion_width;
        if (opt_key == "first_layer_height")                         return &this->first_layer_height;
        if (opt_key == "infill_only_where_needed")                   return &this->infill_only_where_needed;
        if (opt_key == "interface_shells")                           return &this->interface_shells;
        if (opt_key == "layer_height")                               return &this->layer_height;
        if (opt_key == "raft_layers")                                return &this->raft_layers;
        if (opt_key == "seam_position")                              return &this->seam_position;
        if (opt_key == "support_material")                           return &this->support_material;
        if (opt_key == "support_material_angle")                     return &this->support_material_angle;
        if (opt_key == "support_material_contact_distance")          return &this->support_material_contact_distance;
        if (opt_key == "support_material_enforce_layers")            return &this->support_material_enforce_layers;
        if (opt_key == "support_material_extruder")                  return &this->support_material_extruder;
        if (opt_key == "support_material_extrusion_width")           return &this->support_material_extrusion_width;
        if (opt_key == "support_material_interface_extruder")        return &this->support_material_interface_extruder;
        if (opt_key == "support_material_interface_layers")          return &this->support_material_interface_layers;
        if (opt_key == "support_material_interface_spacing")         return &this->support_material_interface_spacing;
        if (opt_key == "support_material_interface_speed")           return &this->support_material_interface_speed;
        if (opt_key == "support_material_pattern")                   return &this->support_material_pattern;
        if (opt_key == "support_material_spacing")                   return &this->support_material_spacing;
        if (opt_key == "support_material_speed")                     return &this->support_material_speed;
        if (opt_key == "support_material_threshold")                 return &this->support_material_threshold;
        if (opt_key == "xy_size_compensation")                       return &this->xy_size_compensation;
        
        return NULL;
    };
};

class PrintRegionConfig : public virtual StaticPrintConfig
{
    public:
    ConfigOptionInt                 bottom_solid_layers;
    ConfigOptionFloat               bridge_flow_ratio;
    ConfigOptionFloat               bridge_speed;
    ConfigOptionEnum<InfillPattern> external_fill_pattern;
    ConfigOptionFloatOrPercent      external_perimeter_extrusion_width;
    ConfigOptionFloatOrPercent      external_perimeter_speed;
    ConfigOptionBool                external_perimeters_first;
    ConfigOptionBool                extra_perimeters;
    ConfigOptionInt                 fill_angle;
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
    
    PrintRegionConfig() : StaticPrintConfig() {
        this->bottom_solid_layers.value                          = 3;
        this->bridge_flow_ratio.value                            = 1;
        this->bridge_speed.value                                 = 60;
        this->external_fill_pattern.value                        = ipRectilinear;
        this->external_perimeter_extrusion_width.value           = 0;
        this->external_perimeter_extrusion_width.percent         = false;
        this->external_perimeter_speed.value                     = 50;
        this->external_perimeter_speed.percent                   = true;
        this->external_perimeters_first.value                    = false;
        this->extra_perimeters.value                             = true;
        this->fill_angle.value                                   = 45;
        this->fill_density.value                                 = 20;
        this->fill_pattern.value                                 = ipHoneycomb;
        this->gap_fill_speed.value                               = 20;
        this->infill_extruder.value                              = 1;
        this->infill_extrusion_width.value                       = 0;
        this->infill_extrusion_width.percent                     = false;
        this->infill_every_layers.value                          = 1;
        this->infill_overlap.value                               = 15;
        this->infill_overlap.percent                             = true;
        this->infill_speed.value                                 = 80;
        this->overhangs.value                                    = true;
        this->perimeter_extruder.value                           = 1;
        this->perimeter_extrusion_width.value                    = 0;
        this->perimeter_extrusion_width.percent                  = false;
        this->perimeter_speed.value                              = 60;
        this->perimeters.value                                   = 3;
        this->solid_infill_extruder.value                        = 1;
        this->small_perimeter_speed.value                        = 15;
        this->small_perimeter_speed.percent                      = false;
        this->solid_infill_below_area.value                      = 70;
        this->solid_infill_extrusion_width.value                 = 0;
        this->solid_infill_extrusion_width.percent               = false;
        this->solid_infill_every_layers.value                    = 0;
        this->solid_infill_speed.value                           = 20;
        this->solid_infill_speed.percent                         = false;
        this->thin_walls.value                                   = true;
        this->top_infill_extrusion_width.value                   = 0;
        this->top_infill_extrusion_width.percent                 = false;
        this->top_solid_infill_speed.value                       = 15;
        this->top_solid_infill_speed.percent                     = false;
        this->top_solid_layers.value                             = 3;
    };
    
    ConfigOption* option(const t_config_option_key opt_key, bool create = false) {
        if (opt_key == "bottom_solid_layers")                        return &this->bottom_solid_layers;
        if (opt_key == "bridge_flow_ratio")                          return &this->bridge_flow_ratio;
        if (opt_key == "bridge_speed")                               return &this->bridge_speed;
        if (opt_key == "external_fill_pattern")                      return &this->external_fill_pattern;
        if (opt_key == "external_perimeter_extrusion_width")         return &this->external_perimeter_extrusion_width;
        if (opt_key == "external_perimeter_speed")                   return &this->external_perimeter_speed;
        if (opt_key == "external_perimeters_first")                  return &this->external_perimeters_first;
        if (opt_key == "extra_perimeters")                           return &this->extra_perimeters;
        if (opt_key == "fill_angle")                                 return &this->fill_angle;
        if (opt_key == "fill_density")                               return &this->fill_density;
        if (opt_key == "fill_pattern")                               return &this->fill_pattern;
        if (opt_key == "gap_fill_speed")                             return &this->gap_fill_speed;
        if (opt_key == "infill_extruder")                            return &this->infill_extruder;
        if (opt_key == "infill_extrusion_width")                     return &this->infill_extrusion_width;
        if (opt_key == "infill_every_layers")                        return &this->infill_every_layers;
        if (opt_key == "infill_overlap")                             return &this->infill_overlap;
        if (opt_key == "infill_speed")                               return &this->infill_speed;
        if (opt_key == "overhangs")                                  return &this->overhangs;
        if (opt_key == "perimeter_extruder")                         return &this->perimeter_extruder;
        if (opt_key == "perimeter_extrusion_width")                  return &this->perimeter_extrusion_width;
        if (opt_key == "perimeter_speed")                            return &this->perimeter_speed;
        if (opt_key == "perimeters")                                 return &this->perimeters;
        if (opt_key == "small_perimeter_speed")                      return &this->small_perimeter_speed;
        if (opt_key == "solid_infill_below_area")                    return &this->solid_infill_below_area;
        if (opt_key == "solid_infill_extruder")                      return &this->solid_infill_extruder;
        if (opt_key == "solid_infill_extrusion_width")               return &this->solid_infill_extrusion_width;
        if (opt_key == "solid_infill_every_layers")                  return &this->solid_infill_every_layers;
        if (opt_key == "solid_infill_speed")                         return &this->solid_infill_speed;
        if (opt_key == "thin_walls")                                 return &this->thin_walls;
        if (opt_key == "top_infill_extrusion_width")                 return &this->top_infill_extrusion_width;
        if (opt_key == "top_solid_infill_speed")                     return &this->top_solid_infill_speed;
        if (opt_key == "top_solid_layers")                           return &this->top_solid_layers;
        
        return NULL;
    };
};

class GCodeConfig : public virtual StaticPrintConfig
{
    public:
    ConfigOptionString              before_layer_gcode;
    ConfigOptionString              end_gcode;
    ConfigOptionString              extrusion_axis;
    ConfigOptionFloats              extrusion_multiplier;
    ConfigOptionFloats              filament_diameter;
    ConfigOptionBool                gcode_comments;
    ConfigOptionEnum<GCodeFlavor>   gcode_flavor;
    ConfigOptionString              layer_gcode;
    ConfigOptionFloat               pressure_advance;
    ConfigOptionFloats              retract_length;
    ConfigOptionFloats              retract_length_toolchange;
    ConfigOptionFloats              retract_lift;
    ConfigOptionFloats              retract_restart_extra;
    ConfigOptionFloats              retract_restart_extra_toolchange;
    ConfigOptionInts                retract_speed;
    ConfigOptionString              start_gcode;
    ConfigOptionString              toolchange_gcode;
    ConfigOptionFloat               travel_speed;
    ConfigOptionBool                use_firmware_retraction;
    ConfigOptionBool                use_relative_e_distances;
    ConfigOptionBool                use_volumetric_e;
    
    GCodeConfig() : StaticPrintConfig() {
        this->before_layer_gcode.value                           = "";
        this->end_gcode.value                                    = "M104 S0 ; turn off temperature\nG28 X0  ; home X axis\nM84     ; disable motors\n";
        this->extrusion_axis.value                               = "E";
        this->extrusion_multiplier.values.resize(1);
        this->extrusion_multiplier.values[0]                     = 1;
        this->filament_diameter.values.resize(1);
        this->filament_diameter.values[0]                        = 3;
        this->gcode_comments.value                               = false;
        this->gcode_flavor.value                                 = gcfRepRap;
        this->layer_gcode.value                                  = "";
        this->pressure_advance.value                             = 0;
        this->retract_length.values.resize(1);
        this->retract_length.values[0]                           = 2;
        this->retract_length_toolchange.values.resize(1);
        this->retract_length_toolchange.values[0]                = 10;
        this->retract_lift.values.resize(1);
        this->retract_lift.values[0]                             = 0;
        this->retract_restart_extra.values.resize(1);
        this->retract_restart_extra.values[0]                    = 0;
        this->retract_restart_extra_toolchange.values.resize(1);
        this->retract_restart_extra_toolchange.values[0]         = 0;
        this->retract_speed.values.resize(1);
        this->retract_speed.values[0]                            = 40;
        this->start_gcode.value                                  = "G28 ; home all axes\nG1 Z5 F5000 ; lift nozzle\n";
        this->toolchange_gcode.value                             = "";
        this->travel_speed.value                                 = 130;
        this->use_firmware_retraction.value                      = false;
        this->use_relative_e_distances.value                     = false;
        this->use_volumetric_e.value                             = false;
    };
    
    ConfigOption* option(const t_config_option_key opt_key, bool create = false) {
        if (opt_key == "before_layer_gcode")                         return &this->before_layer_gcode;
        if (opt_key == "end_gcode")                                  return &this->end_gcode;
        if (opt_key == "extrusion_axis")                             return &this->extrusion_axis;
        if (opt_key == "extrusion_multiplier")                       return &this->extrusion_multiplier;
        if (opt_key == "filament_diameter")                          return &this->filament_diameter;
        if (opt_key == "gcode_comments")                             return &this->gcode_comments;
        if (opt_key == "gcode_flavor")                               return &this->gcode_flavor;
        if (opt_key == "layer_gcode")                                return &this->layer_gcode;
        if (opt_key == "pressure_advance")                           return &this->pressure_advance;
        if (opt_key == "retract_length")                             return &this->retract_length;
        if (opt_key == "retract_length_toolchange")                  return &this->retract_length_toolchange;
        if (opt_key == "retract_lift")                               return &this->retract_lift;
        if (opt_key == "retract_restart_extra")                      return &this->retract_restart_extra;
        if (opt_key == "retract_restart_extra_toolchange")           return &this->retract_restart_extra_toolchange;
        if (opt_key == "retract_speed")                              return &this->retract_speed;
        if (opt_key == "start_gcode")                                return &this->start_gcode;
        if (opt_key == "toolchange_gcode")                           return &this->toolchange_gcode;
        if (opt_key == "travel_speed")                               return &this->travel_speed;
        if (opt_key == "use_firmware_retraction")                    return &this->use_firmware_retraction;
        if (opt_key == "use_relative_e_distances")                   return &this->use_relative_e_distances;
        if (opt_key == "use_volumetric_e")                           return &this->use_volumetric_e;
        
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

class PrintConfig : public GCodeConfig
{
    public:
    ConfigOptionBool                avoid_crossing_perimeters;
    ConfigOptionPoints              bed_shape;
    ConfigOptionInt                 bed_temperature;
    ConfigOptionFloat               bridge_acceleration;
    ConfigOptionInt                 bridge_fan_speed;
    ConfigOptionFloat               brim_width;
    ConfigOptionBool                complete_objects;
    ConfigOptionBool                cooling;
    ConfigOptionFloat               default_acceleration;
    ConfigOptionInt                 disable_fan_first_layers;
    ConfigOptionFloat               duplicate_distance;
    ConfigOptionFloat               extruder_clearance_height;
    ConfigOptionFloat               extruder_clearance_radius;
    ConfigOptionPoints              extruder_offset;
    ConfigOptionBool                fan_always_on;
    ConfigOptionInt                 fan_below_layer_time;
    ConfigOptionStrings             filament_colour;
    ConfigOptionFloat               first_layer_acceleration;
    ConfigOptionInt                 first_layer_bed_temperature;
    ConfigOptionFloatOrPercent      first_layer_extrusion_width;
    ConfigOptionFloatOrPercent      first_layer_speed;
    ConfigOptionInts                first_layer_temperature;
    ConfigOptionBool                gcode_arcs;
    ConfigOptionFloat               infill_acceleration;
    ConfigOptionBool                infill_first;
    ConfigOptionInt                 max_fan_speed;
    ConfigOptionInt                 min_fan_speed;
    ConfigOptionInt                 min_print_speed;
    ConfigOptionFloat               min_skirt_length;
    ConfigOptionString              notes;
    ConfigOptionFloats              nozzle_diameter;
    ConfigOptionBool                only_retract_when_crossing_perimeters;
    ConfigOptionBool                ooze_prevention;
    ConfigOptionString              output_filename_format;
    ConfigOptionFloat               perimeter_acceleration;
    ConfigOptionStrings             post_process;
    ConfigOptionFloat               resolution;
    ConfigOptionFloats              retract_before_travel;
    ConfigOptionBools               retract_layer_change;
    ConfigOptionFloat               skirt_distance;
    ConfigOptionInt                 skirt_height;
    ConfigOptionInt                 skirts;
    ConfigOptionInt                 slowdown_below_layer_time;
    ConfigOptionBool                spiral_vase;
    ConfigOptionInt                 standby_temperature_delta;
    ConfigOptionInts                temperature;
    ConfigOptionInt                 threads;
    ConfigOptionFloat               vibration_limit;
    ConfigOptionBools               wipe;
    ConfigOptionFloat               z_offset;
    
    PrintConfig() : GCodeConfig() {
        this->avoid_crossing_perimeters.value                    = false;
        this->bed_shape.values.push_back(Pointf(0,0));
        this->bed_shape.values.push_back(Pointf(200,0));
        this->bed_shape.values.push_back(Pointf(200,200));
        this->bed_shape.values.push_back(Pointf(0,200));
        this->bed_temperature.value                              = 0;
        this->bridge_acceleration.value                          = 0;
        this->bridge_fan_speed.value                             = 100;
        this->brim_width.value                                   = 0;
        this->complete_objects.value                             = false;
        this->cooling.value                                      = true;
        this->default_acceleration.value                         = 0;
        this->disable_fan_first_layers.value                     = 3;
        this->duplicate_distance.value                           = 6;
        this->extruder_clearance_height.value                    = 20;
        this->extruder_clearance_radius.value                    = 20;
        this->extruder_offset.values.resize(1);
        this->extruder_offset.values[0]                          = Pointf(0,0);
        this->fan_always_on.value                                = false;
        this->fan_below_layer_time.value                         = 60;
        this->filament_colour.values.resize(1);
        this->filament_colour.values[0]                          = "#FFFFFF";
        this->first_layer_acceleration.value                     = 0;
        this->first_layer_bed_temperature.value                  = 0;
        this->first_layer_extrusion_width.value                  = 200;
        this->first_layer_extrusion_width.percent                = true;
        this->first_layer_speed.value                            = 30;
        this->first_layer_speed.percent                          = false;
        this->first_layer_temperature.values.resize(1);
        this->first_layer_temperature.values[0]                  = 200;
        this->gcode_arcs.value                                   = false;
        this->infill_acceleration.value                          = 0;
        this->infill_first.value                                 = false;
        this->max_fan_speed.value                                = 100;
        this->min_fan_speed.value                                = 35;
        this->min_print_speed.value                              = 10;
        this->min_skirt_length.value                             = 0;
        this->notes.value                                        = "";
        this->nozzle_diameter.values.resize(1);
        this->nozzle_diameter.values[0]                          = 0.5;
        this->only_retract_when_crossing_perimeters.value        = true;
        this->ooze_prevention.value                              = false;
        this->output_filename_format.value                       = "[input_filename_base].gcode";
        this->perimeter_acceleration.value                       = 0;
        this->resolution.value                                   = 0;
        this->retract_before_travel.values.resize(1);
        this->retract_before_travel.values[0]                    = 2;
        this->retract_layer_change.values.resize(1);
        this->retract_layer_change.values[0]                     = false;
        this->skirt_distance.value                               = 6;
        this->skirt_height.value                                 = 1;
        this->skirts.value                                       = 1;
        this->slowdown_below_layer_time.value                    = 5;
        this->spiral_vase.value                                  = false;
        this->standby_temperature_delta.value                    = -5;
        this->temperature.values.resize(1);
        this->temperature.values[0]                              = 200;
        this->threads.value                                      = 2;
        this->vibration_limit.value                              = 0;
        this->wipe.values.resize(1);
        this->wipe.values[0]                                     = false;
        this->z_offset.value                                     = 0;
    };
    
    ConfigOption* option(const t_config_option_key opt_key, bool create = false) {
        if (opt_key == "avoid_crossing_perimeters")                  return &this->avoid_crossing_perimeters;
        if (opt_key == "bed_shape")                                  return &this->bed_shape;
        if (opt_key == "bed_temperature")                            return &this->bed_temperature;
        if (opt_key == "bridge_acceleration")                        return &this->bridge_acceleration;
        if (opt_key == "bridge_fan_speed")                           return &this->bridge_fan_speed;
        if (opt_key == "brim_width")                                 return &this->brim_width;
        if (opt_key == "complete_objects")                           return &this->complete_objects;
        if (opt_key == "cooling")                                    return &this->cooling;
        if (opt_key == "default_acceleration")                       return &this->default_acceleration;
        if (opt_key == "disable_fan_first_layers")                   return &this->disable_fan_first_layers;
        if (opt_key == "duplicate_distance")                         return &this->duplicate_distance;
        if (opt_key == "extruder_clearance_height")                  return &this->extruder_clearance_height;
        if (opt_key == "extruder_clearance_radius")                  return &this->extruder_clearance_radius;
        if (opt_key == "extruder_offset")                            return &this->extruder_offset;
        if (opt_key == "fan_always_on")                              return &this->fan_always_on;
        if (opt_key == "fan_below_layer_time")                       return &this->fan_below_layer_time;
        if (opt_key == "filament_colour")                             return &this->filament_colour;
        if (opt_key == "first_layer_acceleration")                   return &this->first_layer_acceleration;
        if (opt_key == "first_layer_bed_temperature")                return &this->first_layer_bed_temperature;
        if (opt_key == "first_layer_extrusion_width")                return &this->first_layer_extrusion_width;
        if (opt_key == "first_layer_speed")                          return &this->first_layer_speed;
        if (opt_key == "first_layer_temperature")                    return &this->first_layer_temperature;
        if (opt_key == "gcode_arcs")                                 return &this->gcode_arcs;
        if (opt_key == "infill_acceleration")                        return &this->infill_acceleration;
        if (opt_key == "infill_first")                               return &this->infill_first;
        if (opt_key == "max_fan_speed")                              return &this->max_fan_speed;
        if (opt_key == "min_fan_speed")                              return &this->min_fan_speed;
        if (opt_key == "min_print_speed")                            return &this->min_print_speed;
        if (opt_key == "min_skirt_length")                           return &this->min_skirt_length;
        if (opt_key == "notes")                                      return &this->notes;
        if (opt_key == "nozzle_diameter")                            return &this->nozzle_diameter;
        if (opt_key == "only_retract_when_crossing_perimeters")      return &this->only_retract_when_crossing_perimeters;
        if (opt_key == "ooze_prevention")                            return &this->ooze_prevention;
        if (opt_key == "output_filename_format")                     return &this->output_filename_format;
        if (opt_key == "perimeter_acceleration")                     return &this->perimeter_acceleration;
        if (opt_key == "post_process")                               return &this->post_process;
        if (opt_key == "resolution")                                 return &this->resolution;
        if (opt_key == "retract_before_travel")                      return &this->retract_before_travel;
        if (opt_key == "retract_layer_change")                       return &this->retract_layer_change;
        if (opt_key == "skirt_distance")                             return &this->skirt_distance;
        if (opt_key == "skirt_height")                               return &this->skirt_height;
        if (opt_key == "skirts")                                     return &this->skirts;
        if (opt_key == "slowdown_below_layer_time")                  return &this->slowdown_below_layer_time;
        if (opt_key == "spiral_vase")                                return &this->spiral_vase;
        if (opt_key == "standby_temperature_delta")                  return &this->standby_temperature_delta;
        if (opt_key == "temperature")                                return &this->temperature;
        if (opt_key == "threads")                                    return &this->threads;
        if (opt_key == "vibration_limit")                            return &this->vibration_limit;
        if (opt_key == "wipe")                                       return &this->wipe;
        if (opt_key == "z_offset")                                   return &this->z_offset;
        
        // look in parent class
        ConfigOption* opt;
        if ((opt = GCodeConfig::option(opt_key, create)) != NULL) return opt;
        
        return NULL;
    };
};

class HostConfig : public virtual StaticPrintConfig
{
    public:
    ConfigOptionString              octoprint_host;
    ConfigOptionString              octoprint_apikey;
    
    HostConfig() : StaticPrintConfig() {
        this->octoprint_host.value                              = "";
        this->octoprint_apikey.value                            = "";
    };
    
    ConfigOption* option(const t_config_option_key opt_key, bool create = false) {
        if (opt_key == "octoprint_host")                        return &this->octoprint_host;
        if (opt_key == "octoprint_apikey")                      return &this->octoprint_apikey;
        
        return NULL;
    };
};

class FullPrintConfig
    : public PrintObjectConfig, public PrintRegionConfig, public PrintConfig, public HostConfig
{
    public:
    ConfigOption* option(const t_config_option_key opt_key, bool create = false) {
        ConfigOption* opt;
        if ((opt = PrintObjectConfig::option(opt_key, create)) != NULL) return opt;
        if ((opt = PrintRegionConfig::option(opt_key, create)) != NULL) return opt;
        if ((opt = PrintConfig::option(opt_key, create)) != NULL) return opt;
        if ((opt = HostConfig::option(opt_key, create)) != NULL) return opt;
        return NULL;
    };
};

}

#endif
