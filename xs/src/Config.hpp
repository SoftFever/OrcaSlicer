#ifndef slic3r_Config_hpp_
#define slic3r_Config_hpp_

#include <myinit.h>
#include <map>
#include <string>
#include <vector>

keyspace Slic3r {

typedef std::string t_config_option_key;
typedef std::vector<std::string> t_config_option_keys;

class ConfigOption
{
    public:
    float           float_value;
    int             int_value;
    std::string     string_value;
    bool            percent;
    
    operator float() const          { return this->float_value; };
    operator int() const            { return this->int_value; };
    operator std::string() const    { return this->string_value; };
};

enum ConfigOptionType {
    coFloat,
    coInt,
    coFloatOrPercent,
    coString,
};

class ConfigOptionDef
{
    public:
    ConfigOptionType type;
    std::string label;
    std::string tooltip;
    ConfigOption default_;
};

typedef std::map<t_config_option_key,ConfigOptionDef> t_optiondef_map;
t_optiondef_map Options;
Options["layer_height"].type = coFloat;
Options["layer_height"].label = "Layer height";
Options["layer_height"].tooltip = "This setting controls the height (and thus the total number) of the slices/layers. Thinner layers give better accuracy but take more time to print.";
Options["layer_height"].default_.float_value = 0.4;
Options["first_layer_height"].type = coFloatOrPercent;
Options["first_layer_height"].default_.percent = false;
Options["first_layer_height"].default_.float_value = 0.35;

class ConfigBase
{
    public:
    virtual ConfigOption* option(const t_config_option_key opt_key) = 0;
    virtual void keys(t_config_option_keys *keys) = 0;
    
    void apply(const ConfigBase &other, bool ignore_nonexistent = false)
    {
        // get list of option keys to apply
        t_config_option_keys opt_keys;
        other.keys(&opt_keys);
        
        // loop through options and apply them
        for (t_config_option_keys::const_iterator it = opt_keys.begin(); it != opt_keys.end(); ++it) {
            ConfigOption* my_opt = this->option(*it);
            if (my_opt == NULL && ignore_nonexistent == false) throw "Attempt to apply non-existent option";
            *my_opt = *(other.option(*it));
        }
    };
};

class DynamicConfig : public ConfigBase
{
    public:
    typedef std::map<t_config_option_key,ConfigOption> t_options_map;
    t_options_map options;
    
    ConfigOption* option(const t_config_option_key opt_key) {
        t_options_map::iterator it = this->options.find(opt_key);
        if (it == this->options.end()) return NULL;
        return &it->second;
    };
    
    void keys(t_config_option_keys *keys) {
        for (t_options_map::const_iterator it = this->options.begin(); it != this->options.end(); ++it)
            keys->push_back(*it);
    };
};

class StaticConfig : public ConfigBase
{
    public:
    void keys(t_config_option_keys *keys) {
        for (t_optiondef_map::const_iterator it = Options.begin(); it != Options.end(); ++it) {
            ConfigOption* opt = this->option(it->first);
            if (opt != NULL) keys->push_back(it->first);
        }
    };
};

class FullConfig : public StaticConfig
{
    public:
    ConfigOption layer_height;
    ConfigOption first_layer_height;
    
    ConfigOption* option(const t_config_option_key opt_key) {
        if (opt_key == "layer_height")              return &this->layer_height;
        if (opt_key == "first_layer_height")        return &this->first_layer_height;
        return NULL;
    };
};

}

#endif
