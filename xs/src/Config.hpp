#ifndef slic3r_Config_hpp_
#define slic3r_Config_hpp_

#include <myinit.h>
#include <map>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <iostream>;
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

enum GCodeFlavor {
    gcfRepRap, gcfTeacup, gcfMakerWare, gcfSailfish, gcfMach3, gcfNoExtrusion,
};

class ConfigOptionGCodeFlavor : public ConfigOption
{
    public:
    GCodeFlavor value;
    ConfigOptionGCodeFlavor() : value(gcfRepRap) {};
    
    operator GCodeFlavor() const { return this->value; };
    
    std::string serialize() {
             if (this->value == gfcRepRap)      { return std::string("reprap"); }
        else if (this->value == gcfTeacup)      { return std::string("teacup"); }
        else if (this->value == gcfMakerWare)   { return std::string("makerware"); }
        else if (this->value == gcfSailfish)    { return std::string("sailfish"); }
        else if (this->value == gcfMach3)       { return std::string("mach3"); }
        else if (this->value == gcfNoExtrusion) { return std::string("no-extrusion"); }
    };
    
    void deserialize(std::string str) {
             if (str.compare("reprap") == 0)        { this->value = gfcRepRap; }
        else if (str.compare("teacup") == 0)        { this->value = gcfTeacup; }
        else if (str.compare("makerware") == 0)     { this->value = gcfMakerWare; }
        else if (str.compare("sailfish") == 0)      { this->value = gcfSailfish; }
        else if (str.compare("mach3") == 0)         { this->value = gcfMach3; }
        else if (str.compare("no-extrusion") == 0)  { this->value = gcfNoExtrusion; }
    };
};

enum ConfigOptionType {
    coFloat,
    coInt,
    coString,
    coFloatOrPercent,
    coPoint,
    coBool,
    coGCodeFlavor,
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
    ConfigOptionFloat           layer_height;
    ConfigOptionFloatOrPercent  first_layer_height;
    ConfigOptionInt             perimeters;
    ConfigOptionString          extrusion_axis;
    ConfigOptionPoint           print_center;
    ConfigOptionString          notes;
    ConfigOptionBool            use_relative_e_distances;
    
    ConfigOption* option(const t_config_option_key opt_key, bool create = false) {
        assert(!create);  // can't create options in StaticConfig
        if (opt_key == "layer_height")              return &this->layer_height;
        if (opt_key == "first_layer_height")        return &this->first_layer_height;
        if (opt_key == "perimeters")                return &this->perimeters;
        if (opt_key == "extrusion_axis")            return &this->extrusion_axis;
        if (opt_key == "print_center")              return &this->print_center;
        if (opt_key == "notes")                     return &this->notes;
        if (opt_key == "use_relative_e_distances")  return &this->use_relative_e_distances;
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
    
    Options["notes"].type = coString;
    
    Options["use_relative_e_distances"].type = coBool;
    
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
    defconf.notes.value                     = "";
    defconf.use_relative_e_distances.value  = false;
    
    return defconf;
}

}

#endif
