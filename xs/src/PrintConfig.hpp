#ifndef slic3r_PrintConfig_hpp_
#define slic3r_PrintConfig_hpp_

#include "Config.hpp"

namespace Slic3r {

enum GCodeFlavor {
    gcfRepRap, gcfTeacup, gcfMakerWare, gcfSailfish, gcfMach3, gcfNoExtrusion,
};

template<> inline t_config_enum_values ConfigOptionEnum<GCodeFlavor>::get_enum_values() {
    t_config_enum_values keys_map;
    keys_map["reprap"]          = gcfRepRap;
    keys_map["teacup"]          = gcfTeacup;
    keys_map["makerware"]       = gcfMakerWare;
    keys_map["sailfish"]        = gcfSailfish;
    keys_map["mach3"]           = gcfMach3;
    keys_map["no-extrusion"]    = gcfNoExtrusion;
    return keys_map;
}

class PrintConfig : public StaticConfig
{
    public:
    static t_optiondef_map PrintConfigDef;
    
    ConfigOptionFloat               layer_height;
    ConfigOptionFloatOrPercent      first_layer_height;
    ConfigOptionInt                 perimeters;
    ConfigOptionString              extrusion_axis;
    ConfigOptionPoint               print_center;
    ConfigOptionPoints              extruder_offset;
    ConfigOptionString              notes;
    ConfigOptionBool                use_relative_e_distances;
    ConfigOptionEnum<GCodeFlavor>   gcode_flavor;
    ConfigOptionFloats              nozzle_diameter;
    ConfigOptionInts                temperature;
    ConfigOptionBools               wipe;
    
    PrintConfig() {
        this->def = &PrintConfig::PrintConfigDef;
        
        this->layer_height.value              = 0.4;
        this->first_layer_height.value        = 0.35;
        this->first_layer_height.percent      = false;
        this->perimeters.value                = 3;
        this->extrusion_axis.value            = "E";
        this->print_center.point              = Pointf(100,100);
        this->extruder_offset.points.push_back(Pointf(0,0));
        this->notes.value                     = "";
        this->use_relative_e_distances.value  = false;
        this->gcode_flavor.value              = gcfRepRap;
        this->nozzle_diameter.values.push_back(0.5);
        this->temperature.values.push_back(200);
        this->wipe.values.push_back(true);
    };
    
    ConfigOption* option(const t_config_option_key opt_key, bool create = false) {
        assert(!create);  // can't create options in StaticConfig
        if (opt_key == "layer_height")              return &this->layer_height;
        if (opt_key == "first_layer_height")        return &this->first_layer_height;
        if (opt_key == "perimeters")                return &this->perimeters;
        if (opt_key == "extrusion_axis")            return &this->extrusion_axis;
        if (opt_key == "print_center")              return &this->print_center;
        if (opt_key == "extruder_offset")           return &this->extruder_offset;
        if (opt_key == "notes")                     return &this->notes;
        if (opt_key == "use_relative_e_distances")  return &this->use_relative_e_distances;
        if (opt_key == "gcode_flavor")              return &this->gcode_flavor;
        if (opt_key == "nozzle_diameter")           return &this->nozzle_diameter;
        if (opt_key == "temperature")               return &this->temperature;
        if (opt_key == "wipe")                      return &this->wipe;
        return NULL;
    };
    
    static t_optiondef_map build_def () {
        t_optiondef_map Options;
        Options["layer_height"].type = coFloat;
        Options["layer_height"].label = "Layer height";
        Options["layer_height"].tooltip = "This setting controls the height (and thus the total number) of the slices/layers. Thinner layers give better accuracy but take more time to print.";

        Options["first_layer_height"].type = coFloatOrPercent;
        Options["first_layer_height"].ratio_over = "layer_height";
    
        Options["perimeters"].type = coInt;
        Options["perimeters"].label = "Perimeters (minimum)";
        Options["perimeters"].tooltip = "This option sets the number of perimeters to generate for each layer. Note that Slic3r may increase this number automatically when it detects sloping surfaces which benefit from a higher number of perimeters if the Extra Perimeters option is enabled.";
    
        Options["extrusion_axis"].type = coString;
    
        Options["print_center"].type = coPoint;
    
        Options["extruder_offset"].type = coPoints;
    
        Options["notes"].type = coString;
    
        Options["use_relative_e_distances"].type = coBool;
    
        Options["gcode_flavor"].type = coEnum;
        Options["gcode_flavor"].enum_keys_map = ConfigOptionEnum<GCodeFlavor>::get_enum_values();
    
        Options["nozzle_diameter"].type = coFloats;
    
        Options["temperature"].type = coInts;
    
        Options["wipe"].type = coBools;
    
        return Options;
    };
};

class DynamicPrintConfig : public DynamicConfig
{
    public:
    DynamicPrintConfig() {
        this->def = &PrintConfig::PrintConfigDef;
    };

};

}

#endif
