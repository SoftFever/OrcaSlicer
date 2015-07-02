#ifndef slic3r_PrintConfig_hpp_
#define slic3r_PrintConfig_hpp_

#include "Config.hpp"

#define OPT_PTR(KEY) if (opt_key == #KEY) return &this->KEY

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
        OPT_PTR(dont_support_bridges);
        OPT_PTR(extrusion_width);
        OPT_PTR(first_layer_height);
        OPT_PTR(infill_only_where_needed);
        OPT_PTR(interface_shells);
        OPT_PTR(layer_height);
        OPT_PTR(raft_layers);
        OPT_PTR(seam_position);
        OPT_PTR(support_material);
        OPT_PTR(support_material_angle);
        OPT_PTR(support_material_contact_distance);
        OPT_PTR(support_material_enforce_layers);
        OPT_PTR(support_material_extruder);
        OPT_PTR(support_material_extrusion_width);
        OPT_PTR(support_material_interface_extruder);
        OPT_PTR(support_material_interface_layers);
        OPT_PTR(support_material_interface_spacing);
        OPT_PTR(support_material_interface_speed);
        OPT_PTR(support_material_pattern);
        OPT_PTR(support_material_spacing);
        OPT_PTR(support_material_speed);
        OPT_PTR(support_material_threshold);
        OPT_PTR(xy_size_compensation);
        
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
        OPT_PTR(bottom_solid_layers);
        OPT_PTR(bridge_flow_ratio);
        OPT_PTR(bridge_speed);
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
    ConfigOptionFloat               max_print_speed;
    ConfigOptionFloat               max_volumetric_speed;
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
        this->max_print_speed.value                              = 80;
        this->max_volumetric_speed.value                         = 0;
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
        OPT_PTR(before_layer_gcode);
        OPT_PTR(end_gcode);
        OPT_PTR(extrusion_axis);
        OPT_PTR(extrusion_multiplier);
        OPT_PTR(filament_diameter);
        OPT_PTR(gcode_comments);
        OPT_PTR(gcode_flavor);
        OPT_PTR(layer_gcode);
        OPT_PTR(max_print_speed);
        OPT_PTR(max_volumetric_speed);
        OPT_PTR(pressure_advance);
        OPT_PTR(retract_length);
        OPT_PTR(retract_length_toolchange);
        OPT_PTR(retract_lift);
        OPT_PTR(retract_restart_extra);
        OPT_PTR(retract_restart_extra_toolchange);
        OPT_PTR(retract_speed);
        OPT_PTR(start_gcode);
        OPT_PTR(toolchange_gcode);
        OPT_PTR(travel_speed);
        OPT_PTR(use_firmware_retraction);
        OPT_PTR(use_relative_e_distances);
        OPT_PTR(use_volumetric_e);
        
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
        OPT_PTR(extruder_offset);
        OPT_PTR(fan_always_on);
        OPT_PTR(fan_below_layer_time);
        OPT_PTR(filament_colour);
        OPT_PTR(first_layer_acceleration);
        OPT_PTR(first_layer_bed_temperature);
        OPT_PTR(first_layer_extrusion_width);
        OPT_PTR(first_layer_speed);
        OPT_PTR(first_layer_temperature);
        OPT_PTR(gcode_arcs);
        OPT_PTR(infill_acceleration);
        OPT_PTR(infill_first);
        OPT_PTR(max_fan_speed);
        OPT_PTR(min_fan_speed);
        OPT_PTR(min_print_speed);
        OPT_PTR(min_skirt_length);
        OPT_PTR(notes);
        OPT_PTR(nozzle_diameter);
        OPT_PTR(only_retract_when_crossing_perimeters);
        OPT_PTR(ooze_prevention);
        OPT_PTR(output_filename_format);
        OPT_PTR(perimeter_acceleration);
        OPT_PTR(post_process);
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
        OPT_PTR(vibration_limit);
        OPT_PTR(wipe);
        OPT_PTR(z_offset);
        
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
        OPT_PTR(octoprint_host);
        OPT_PTR(octoprint_apikey);
        
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
