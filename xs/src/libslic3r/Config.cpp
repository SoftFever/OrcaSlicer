#include "Config.hpp"
#include <stdlib.h>  // for setenv()
#include <assert.h>

#if defined(_WIN32) && !defined(setenv) && defined(_putenv_s)
#define setenv(k, v, o) _putenv_s(k, v)
#endif

namespace Slic3r {

bool
operator== (const ConfigOption &a, const ConfigOption &b)
{
    return a.serialize().compare(b.serialize()) == 0;
}

bool
operator!= (const ConfigOption &a, const ConfigOption &b)
{
    return !(a == b);
}

ConfigDef::~ConfigDef()
{
    for (t_optiondef_map::iterator it = this->options.begin(); it != this->options.end(); ++it) {
        if (it->second.default_value != NULL)
            delete it->second.default_value;
    }
}

ConfigOptionDef*
ConfigDef::add(const t_config_option_key &opt_key, ConfigOptionType type)
{
    ConfigOptionDef* opt = &this->options[opt_key];
    opt->type = type;
    return opt;
}

const ConfigOptionDef*
ConfigDef::get(const t_config_option_key &opt_key) const
{
    if (this->options.count(opt_key) == 0) return NULL;
    return &const_cast<ConfigDef*>(this)->options[opt_key];
}

bool
ConfigBase::has(const t_config_option_key &opt_key) {
    return (this->option(opt_key, false) != NULL);
}

void
ConfigBase::apply(const ConfigBase &other, bool ignore_nonexistent) {
    // get list of option keys to apply
    t_config_option_keys opt_keys = other.keys();
    
    // loop through options and apply them
    for (t_config_option_keys::const_iterator it = opt_keys.begin(); it != opt_keys.end(); ++it) {
        ConfigOption* my_opt = this->option(*it, true);
        if (my_opt == NULL) {
            if (ignore_nonexistent == false) throw "Attempt to apply non-existent option";
            continue;
        }
        
        // not the most efficient way, but easier than casting pointers to subclasses
        bool res = my_opt->deserialize( other.option(*it)->serialize() );
        if (!res) {
            std::string error = "Unexpected failure when deserializing serialized value for " + *it;
            CONFESS(error.c_str());
        }
    }
}

bool
ConfigBase::equals(ConfigBase &other) {
    return this->diff(other).empty();
}

// this will *ignore* options not present in both configs
t_config_option_keys
ConfigBase::diff(ConfigBase &other) {
    t_config_option_keys diff;
    
    t_config_option_keys my_keys = this->keys();
    for (t_config_option_keys::const_iterator opt_key = my_keys.begin(); opt_key != my_keys.end(); ++opt_key) {
        if (other.has(*opt_key) && other.serialize(*opt_key) != this->serialize(*opt_key)) {
            diff.push_back(*opt_key);
        }
    }
    
    return diff;
}

std::string
ConfigBase::serialize(const t_config_option_key &opt_key) const {
    const ConfigOption* opt = this->option(opt_key);
    assert(opt != NULL);
    return opt->serialize();
}

bool
ConfigBase::set_deserialize(const t_config_option_key &opt_key, std::string str) {
    const ConfigOptionDef* optdef = this->def->get(opt_key);
    if (optdef == NULL) throw "Calling set_deserialize() on unknown option";
    if (!optdef->shortcut.empty()) {
        for (std::vector<t_config_option_key>::const_iterator it = optdef->shortcut.begin(); it != optdef->shortcut.end(); ++it) {
            if (!this->set_deserialize(*it, str)) return false;
        }
        return true;
    }
    
    ConfigOption* opt = this->option(opt_key, true);
    assert(opt != NULL);
    return opt->deserialize(str);
}

double
ConfigBase::get_abs_value(const t_config_option_key &opt_key) {
    ConfigOption* opt = this->option(opt_key, false);
    if (ConfigOptionFloatOrPercent* optv = dynamic_cast<ConfigOptionFloatOrPercent*>(opt)) {
        // get option definition
        const ConfigOptionDef* def = this->def->get(opt_key);
        assert(def != NULL);
        
        // compute absolute value over the absolute value of the base option
        return optv->get_abs_value(this->get_abs_value(def->ratio_over));
    } else if (ConfigOptionFloat* optv = dynamic_cast<ConfigOptionFloat*>(opt)) {
        return optv->value;
    } else {
        throw "Not a valid option type for get_abs_value()";
    }
}

double
ConfigBase::get_abs_value(const t_config_option_key &opt_key, double ratio_over) {
    // get stored option value
    ConfigOptionFloatOrPercent* opt = dynamic_cast<ConfigOptionFloatOrPercent*>(this->option(opt_key));
    assert(opt != NULL);
    
    // compute absolute value
    return opt->get_abs_value(ratio_over);
}

void
ConfigBase::setenv_()
{
#ifdef setenv
    t_config_option_keys opt_keys = this->keys();
    for (t_config_option_keys::const_iterator it = opt_keys.begin(); it != opt_keys.end(); ++it) {
        // prepend the SLIC3R_ prefix
        std::ostringstream ss;
        ss << "SLIC3R_";
        ss << *it;
        std::string envname = ss.str();
        
        // capitalize environment variable name
        for (size_t i = 0; i < envname.size(); ++i)
            envname[i] = (envname[i] <= 'z' && envname[i] >= 'a') ? envname[i]-('a'-'A') : envname[i];
        
        setenv(envname.c_str(), this->serialize(*it).c_str(), 1);
    }
#endif
}

const ConfigOption*
ConfigBase::option(const t_config_option_key &opt_key) const {
    return const_cast<ConfigBase*>(this)->option(opt_key, false);
}

ConfigOption*
ConfigBase::option(const t_config_option_key &opt_key, bool create) {
    return this->optptr(opt_key, create);
}

DynamicConfig& DynamicConfig::operator= (DynamicConfig other)
{
    this->swap(other);
    return *this;
}

void
DynamicConfig::swap(DynamicConfig &other)
{
    std::swap(this->options, other.options);
}

DynamicConfig::~DynamicConfig () {
    for (t_options_map::iterator it = this->options.begin(); it != this->options.end(); ++it) {
        ConfigOption* opt = it->second;
        if (opt != NULL) delete opt;
    }
}

DynamicConfig::DynamicConfig (const DynamicConfig& other) {
    this->def = other.def;
    this->apply(other, false);
}

ConfigOption*
DynamicConfig::optptr(const t_config_option_key &opt_key, bool create) {
    if (this->options.count(opt_key) == 0) {
        if (create) {
            const ConfigOptionDef* optdef = this->def->get(opt_key);
            assert(optdef != NULL);
            ConfigOption* opt;
            if (optdef->type == coFloat) {
                opt = new ConfigOptionFloat ();
            } else if (optdef->type == coFloats) {
                opt = new ConfigOptionFloats ();
            } else if (optdef->type == coInt) {
                opt = new ConfigOptionInt ();
            } else if (optdef->type == coInts) {
                opt = new ConfigOptionInts ();
            } else if (optdef->type == coString) {
                opt = new ConfigOptionString ();
            } else if (optdef->type == coStrings) {
                opt = new ConfigOptionStrings ();
            } else if (optdef->type == coPercent) {
                opt = new ConfigOptionPercent ();
            } else if (optdef->type == coFloatOrPercent) {
                opt = new ConfigOptionFloatOrPercent ();
            } else if (optdef->type == coPoint) {
                opt = new ConfigOptionPoint ();
            } else if (optdef->type == coPoints) {
                opt = new ConfigOptionPoints ();
            } else if (optdef->type == coBool) {
                opt = new ConfigOptionBool ();
            } else if (optdef->type == coBools) {
                opt = new ConfigOptionBools ();
            } else if (optdef->type == coEnum) {
                ConfigOptionEnumGeneric* optv = new ConfigOptionEnumGeneric ();
                optv->keys_map = &optdef->enum_keys_map;
                opt = static_cast<ConfigOption*>(optv);
            } else {
                throw "Unknown option type";
            }
            this->options[opt_key] = opt;
            return opt;
        } else {
            return NULL;
        }
    }
    return this->options[opt_key];
}

template<class T>
T*
DynamicConfig::opt(const t_config_option_key &opt_key, bool create) {
    return dynamic_cast<T*>(this->option(opt_key, create));
}
template ConfigOptionInt* DynamicConfig::opt<ConfigOptionInt>(const t_config_option_key &opt_key, bool create);
template ConfigOptionBool* DynamicConfig::opt<ConfigOptionBool>(const t_config_option_key &opt_key, bool create);
template ConfigOptionBools* DynamicConfig::opt<ConfigOptionBools>(const t_config_option_key &opt_key, bool create);
template ConfigOptionPercent* DynamicConfig::opt<ConfigOptionPercent>(const t_config_option_key &opt_key, bool create);

t_config_option_keys
DynamicConfig::keys() const {
    t_config_option_keys keys;
    for (t_options_map::const_iterator it = this->options.begin(); it != this->options.end(); ++it)
        keys.push_back(it->first);
    return keys;
}

void
DynamicConfig::erase(const t_config_option_key &opt_key) {
    this->options.erase(opt_key);
}

void
StaticConfig::set_defaults()
{
    // use defaults from definition
    if (this->def == NULL) return;
    t_config_option_keys keys = this->keys();
    for (t_config_option_keys::const_iterator it = keys.begin(); it != keys.end(); ++it) {
        const ConfigOptionDef* def = this->def->get(*it);
        
        if (def->default_value != NULL)
            this->option(*it)->set(*def->default_value);
    }
}

t_config_option_keys
StaticConfig::keys() const {
    t_config_option_keys keys;
    for (t_optiondef_map::const_iterator it = this->def->options.begin(); it != this->def->options.end(); ++it) {
        const ConfigOption* opt = this->option(it->first);
        if (opt != NULL) keys.push_back(it->first);
    }
    return keys;
}

}
