#ifndef slic3r_Config_hpp_
#define slic3r_Config_hpp_

#include <myinit.h>
#include <map>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
#include "Point.hpp"

namespace Slic3r {

typedef std::string t_config_option_key;
typedef std::vector<std::string> t_config_option_keys;

class ConfigOption {
    public:
    virtual ~ConfigOption() {};
    virtual std::string serialize() = 0;
    virtual void deserialize(std::string str) = 0;
};

class ConfigOptionFloat : public ConfigOption
{
    public:
    float value;
    ConfigOptionFloat() : value(0) {};
    
    operator float() const { return this->value; };
    
    std::string serialize() {
        std::ostringstream ss;
        ss << this->value;
        return ss.str();
    };
    
    void deserialize(std::string str) {
        this->value = ::atof(str.c_str());
    };
};

class ConfigOptionFloats : public ConfigOption
{
    public:
    std::vector<float> values;
    
    std::string serialize() {
        std::ostringstream ss;
        for (std::vector<float>::const_iterator it = this->values.begin(); it != this->values.end(); ++it) {
            if (it - this->values.begin() != 0) ss << ",";
            ss << *it;
        }
        return ss.str();
    };
    
    void deserialize(std::string str) {
        this->values.clear();
        std::istringstream is(str);
        std::string item_str;
        while (std::getline(is, item_str, ',')) {
            this->values.push_back(::atof(item_str.c_str()));
        }
    };
};

class ConfigOptionInt : public ConfigOption
{
    public:
    int value;
    ConfigOptionInt() : value(0) {};
    
    operator int() const { return this->value; };
    
    std::string serialize() {
        std::ostringstream ss;
        ss << this->value;
        return ss.str();
    };
    
    void deserialize(std::string str) {
        this->value = ::atoi(str.c_str());
    };
};

class ConfigOptionInts : public ConfigOption
{
    public:
    std::vector<int> values;
    
    std::string serialize() {
        std::ostringstream ss;
        for (std::vector<int>::const_iterator it = this->values.begin(); it != this->values.end(); ++it) {
            if (it - this->values.begin() != 0) ss << ",";
            ss << *it;
        }
        return ss.str();
    };
    
    void deserialize(std::string str) {
        this->values.clear();
        std::istringstream is(str);
        std::string item_str;
        while (std::getline(is, item_str, ',')) {
            this->values.push_back(::atoi(item_str.c_str()));
        }
    };
};

class ConfigOptionString : public ConfigOption
{
    public:
    std::string value;
    ConfigOptionString() : value("") {};
    
    operator std::string() const { return this->value; };
    
    std::string serialize() {
        std::string str = this->value;
        
        // s/\R/\\n/g
        size_t pos = 0;
        while ((pos = str.find("\n", pos)) != std::string::npos || (pos = str.find("\r", pos)) != std::string::npos) {
            str.replace(pos, 1, "\\n");
            pos += 2; // length of "\\n"
        }
        
        return str; 
    };
    
    void deserialize(std::string str) {
        // s/\\n/\n/g
        size_t pos = 0;
        while ((pos = str.find("\\n", pos)) != std::string::npos) {
            str.replace(pos, 2, "\n");
            pos += 1; // length of "\n"
        }
        
        this->value = str;
    };
};

class ConfigOptionFloatOrPercent : public ConfigOption
{
    public:
    float value;
    bool percent;
    ConfigOptionFloatOrPercent() : value(0), percent(false) {};
    
    std::string serialize() {
        std::ostringstream ss;
        ss << this->value;
        std::string s(ss.str());
        if (this->percent) s += "%";
        return s;
    };
    
    void deserialize(std::string str) {
        if (str.find_first_of("%") != std::string::npos) {
            sscanf(str.c_str(), "%f%%", &this->value);
            this->percent = true;
        } else {
            this->value = ::atof(str.c_str());
            this->percent = false;
        }
    };
};

class ConfigOptionPoint : public ConfigOption
{
    public:
    Pointf point;
    ConfigOptionPoint() : point(Pointf(0,0)) {};
    
    operator Pointf() const { return this->point; };
    
    std::string serialize() {
        std::ostringstream ss;
        ss << this->point.x;
        ss << ",";
        ss << this->point.y;
        return ss.str();
    };
    
    void deserialize(std::string str) {
        sscanf(str.c_str(), "%f%*1[,x]%f", &this->point.x, &this->point.y);
    };
};

class ConfigOptionPoints : public ConfigOption
{
    public:
    Pointfs points;
    
    std::string serialize() {
        std::ostringstream ss;
        for (Pointfs::const_iterator it = this->points.begin(); it != this->points.end(); ++it) {
            if (it - this->points.begin() != 0) ss << ",";
            ss << it->x;
            ss << "x";
            ss << it->y;
        }
        return ss.str();
    };
    
    void deserialize(std::string str) {
        this->points.clear();
        std::istringstream is(str);
        std::string point_str;
        while (std::getline(is, point_str, ',')) {
            Pointf point;
            sscanf(point_str.c_str(), "%fx%f", &point.x, &point.y);
            this->points.push_back(point);
        }
    };
};

class ConfigOptionBool : public ConfigOption
{
    public:
    bool value;
    ConfigOptionBool() : value(false) {};
    
    operator bool() const { return this->value; };
    
    std::string serialize() {
        return std::string(this->value ? "1" : "0");
    };
    
    void deserialize(std::string str) {
        this->value = (str.compare("1") == 0);
    };
};

class ConfigOptionBools : public ConfigOption
{
    public:
    std::vector<bool> values;
    
    std::string serialize() {
        std::ostringstream ss;
        for (std::vector<bool>::const_iterator it = this->values.begin(); it != this->values.end(); ++it) {
            if (it - this->values.begin() != 0) ss << ",";
            ss << (*it ? "1" : "0");
        }
        return ss.str();
    };
    
    void deserialize(std::string str) {
        this->values.clear();
        std::istringstream is(str);
        std::string item_str;
        while (std::getline(is, item_str, ',')) {
            this->values.push_back(item_str.compare("1") == 0);
        }
    };
};

template <class T>
class ConfigOptionEnum : public ConfigOption
{
    public:
    T value;
    
    operator T() const { return this->value; };
    
    std::string serialize();
    void deserialize(std::string str);
    static std::map<std::string,T> get_enum_values();
};

template <class T>
std::string ConfigOptionEnum<T>::serialize() {
    typename std::map<std::string,T> enum_keys_map = ConfigOptionEnum<T>::get_enum_values();
    for (typename std::map<std::string,T>::iterator it = enum_keys_map.begin(); it != enum_keys_map.end(); ++it) {
        if (it->second == this->value) return it->first;
    }
    return "";
};

template <class T>
void ConfigOptionEnum<T>::deserialize(std::string str) {
    typename std::map<std::string,T> enum_keys_map = ConfigOptionEnum<T>::get_enum_values();
    assert(enum_keys_map.count(str) > 0);
    this->value = enum_keys_map[str];
};

enum GCodeFlavor {
    gcfRepRap, gcfTeacup, gcfMakerWare, gcfSailfish, gcfMach3, gcfNoExtrusion,
};
typedef ConfigOptionEnum<GCodeFlavor> ConfigOptionEnumGCodeFlavor;

// we declare this as inline to keep it in this file along with all other option definitions
template<> inline std::map<std::string,GCodeFlavor> ConfigOptionEnum<GCodeFlavor>::get_enum_values() {
    std::map<std::string,GCodeFlavor> keys_map;
    keys_map["reprap"]          = gcfRepRap;
    keys_map["teacup"]          = gcfTeacup;
    keys_map["makerware"]       = gcfMakerWare;
    keys_map["sailfish"]        = gcfSailfish;
    keys_map["mach3"]           = gcfMach3;
    keys_map["no-extrusion"]    = gcfNoExtrusion;
    return keys_map;
}

enum ConfigOptionType {
    coFloat,
    coFloats,
    coInt,
    coInts,
    coString,
    coFloatOrPercent,
    coPoint,
    coPoints,
    coBool,
    coBools,
    coEnumGCodeFlavor,
};

class ConfigOptionDef
{
    public:
    ConfigOptionType type;
    std::string label;
    std::string tooltip;
    std::string ratio_over;
};

typedef std::map<t_config_option_key,ConfigOptionDef> t_optiondef_map;

ConfigOptionDef* get_config_option_def(const t_config_option_key opt_key);

class ConfigBase
{
    public:
    virtual ConfigOption* option(const t_config_option_key opt_key, bool create = false) = 0;
    virtual void keys(t_config_option_keys *keys) = 0;
    void apply(ConfigBase &other, bool ignore_nonexistent = false);
    std::string serialize(const t_config_option_key opt_key);
    void set_deserialize(const t_config_option_key opt_key, std::string str);
    float get_abs_value(const t_config_option_key opt_key);
    
    #ifdef SLIC3RXS
    SV* get(t_config_option_key opt_key);
    void set(t_config_option_key opt_key, SV* value);
    #endif
};

class DynamicConfig : public ConfigBase
{
    public:
    DynamicConfig() {};
    ~DynamicConfig();
    ConfigOption* option(const t_config_option_key opt_key, bool create = false);
    void keys(t_config_option_keys *keys);
    bool has(const t_config_option_key opt_key) const;
    
    private:
    DynamicConfig(const DynamicConfig& other);              // we disable this by making it private and unimplemented
    DynamicConfig& operator= (const DynamicConfig& other);  // we disable this by making it private and unimplemented
    typedef std::map<t_config_option_key,ConfigOption*> t_options_map;
    t_options_map options;
};

class StaticConfig : public ConfigBase
{
    public:
    void keys(t_config_option_keys *keys);
};

class FullConfig : public StaticConfig
{
    public:
    ConfigOptionFloat               layer_height;
    ConfigOptionFloatOrPercent      first_layer_height;
    ConfigOptionInt                 perimeters;
    ConfigOptionString              extrusion_axis;
    ConfigOptionPoint               print_center;
    ConfigOptionPoints              extruder_offset;
    ConfigOptionString              notes;
    ConfigOptionBool                use_relative_e_distances;
    ConfigOptionEnumGCodeFlavor     gcode_flavor;
    ConfigOptionFloats              nozzle_diameter;
    ConfigOptionInts                temperature;
    ConfigOptionBools               wipe;
    
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
};

static t_optiondef_map _build_optiondef_map () {
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
    
    Options["gcode_flavor"].type = coEnumGCodeFlavor;
    
    Options["nozzle_diameter"].type = coFloats;
    
    Options["temperature"].type = coInts;
    
    Options["wipe"].type = coBools;
    
    return Options;
}


static FullConfig _build_default_config () {
    FullConfig defconf;
    
    defconf.layer_height.value              = 0.4;
    defconf.first_layer_height.value        = 0.35;
    defconf.first_layer_height.percent      = false;
    defconf.perimeters.value                = 3;
    defconf.extrusion_axis.value            = "E";
    defconf.print_center.point              = Pointf(100,100);
    defconf.extruder_offset.points.push_back(Pointf(0,0));
    defconf.notes.value                     = "";
    defconf.use_relative_e_distances.value  = false;
    defconf.gcode_flavor.value              = gcfRepRap;
    defconf.nozzle_diameter.values.push_back(0.5);
    defconf.temperature.values.push_back(200);
    defconf.wipe.values.push_back(true);
    
    return defconf;
}

}

#endif
