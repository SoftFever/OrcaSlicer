#ifndef slic3r_Config_hpp_
#define slic3r_Config_hpp_

#include <myinit.h>
#include <map>
#include <string>
#include <vector>

namespace Slic3r {

typedef std::string t_config_option_key;
typedef std::vector<std::string> t_config_option_keys;

class ConfigOption {
    public:
    virtual ~ConfigOption() {};
};

class ConfigOptionFloat : public ConfigOption
{
    public:
    float value;
    operator float() const { return this->value; };
};

class ConfigOptionInt : public ConfigOption
{
    public:
    int value;
    operator int() const { return this->value; };
};

class ConfigOptionString : public ConfigOption
{
    public:
    std::string value;
    operator std::string() const { return this->value; };
};

class ConfigOptionFloatOrPercent : public ConfigOption
{
    public:
    float value;
    bool percent;
};

enum ConfigOptionType {
    coFloat,
    coInt,
    coString,
    coFloatOrPercent,
};

class ConfigOptionDef
{
    public:
    ConfigOptionType type;
    std::string label;
    std::string tooltip;
};

typedef std::map<t_config_option_key,ConfigOptionDef> t_optiondef_map;

class ConfigBase
{
    public:
    virtual ConfigOption* option(const t_config_option_key opt_key) = 0;
    virtual void keys(t_config_option_keys *keys) = 0;
    void apply(ConfigBase &other, bool ignore_nonexistent = false);
    
    #ifdef SLIC3RXS
    SV* get(t_config_option_key opt_key);
    #endif
};

class DynamicConfig : public ConfigBase
{
    public:
    ConfigOption* option(const t_config_option_key opt_key);
    void keys(t_config_option_keys *keys);
    bool has(const t_config_option_key opt_key) const;
    
    private:
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
    ConfigOptionFloat layer_height;
    ConfigOptionFloatOrPercent first_layer_height;
    
    ConfigOption* option(const t_config_option_key opt_key) {
        if (opt_key == "layer_height")              return &this->layer_height;
        if (opt_key == "first_layer_height")        return &this->first_layer_height;
        return NULL;
    };
};

static t_optiondef_map _build_optiondef_map () {
    t_optiondef_map Options;
    Options["layer_height"].type = coFloat;
    Options["layer_height"].label = "Layer height";
    Options["layer_height"].tooltip = "This setting controls the height (and thus the total number) of the slices/layers. Thinner layers give better accuracy but take more time to print.";

    Options["first_layer_height"].type = coFloatOrPercent;
    return Options;
}

static FullConfig _build_default_config () {
    FullConfig defconf;
    
    defconf.layer_height.value = 0.4;
    defconf.first_layer_height.value = 0.35;
    defconf.first_layer_height.percent = false;
    
    return defconf;
}

}

#endif
