#include "Config.hpp"

namespace Slic3r {

t_optiondef_map Options = _build_optiondef_map();
FullConfig DefaultConfig = _build_default_config();

ConfigOptionDef*
get_config_option_def(const t_config_option_key opt_key) {
    t_optiondef_map::iterator it = Options.find(opt_key);
    if (it == Options.end()) return NULL;
    return &it->second;
}

void
ConfigBase::apply(ConfigBase &other, bool ignore_nonexistent) {
    // get list of option keys to apply
    t_config_option_keys opt_keys;
    other.keys(&opt_keys);
    
    // loop through options and apply them
    for (t_config_option_keys::const_iterator it = opt_keys.begin(); it != opt_keys.end(); ++it) {
        ConfigOption* my_opt = this->option(*it);
        if (my_opt == NULL && ignore_nonexistent == false) throw "Attempt to apply non-existent option";
        *my_opt = *(other.option(*it));
    }
}

std::string
ConfigBase::serialize(const t_config_option_key opt_key) {
    ConfigOption* opt = this->option(opt_key);
    assert(opt != NULL);
    return opt->serialize();
}

void
ConfigBase::set_deserialize(const t_config_option_key opt_key, std::string str) {
    ConfigOption* opt = this->option(opt_key);
    assert(opt != NULL);
    opt->deserialize(str);
}

float
ConfigBase::get_abs_value(const t_config_option_key opt_key) {
    // get option definition
    ConfigOptionDef* def = get_config_option_def(opt_key);
    assert(def != NULL);
    assert(def->type == coFloatOrPercent);
    
    // get stored option value
    ConfigOptionFloatOrPercent* opt = dynamic_cast<ConfigOptionFloatOrPercent*>(this->option(opt_key));
    assert(opt != NULL);
    
    // compute absolute value
    if (opt->percent) {
        ConfigOptionFloat* optbase = dynamic_cast<ConfigOptionFloat*>(this->option(def->ratio_over));
        assert(optbase != NULL);
        return optbase->value * opt->value / 100;
    } else {
        return opt->value;
    }
}

#ifdef SLIC3RXS
SV*
ConfigBase::get(t_config_option_key opt_key) {
    ConfigOption* opt = this->option(opt_key);
    if (opt == NULL) return &PL_sv_undef;
    if (ConfigOptionFloat* optv = dynamic_cast<ConfigOptionFloat*>(opt)) {
        return newSVnv(optv->value);
    } else if (ConfigOptionInt* optv = dynamic_cast<ConfigOptionInt*>(opt)) {
        return newSViv(optv->value);
    } else if (ConfigOptionString* optv = dynamic_cast<ConfigOptionString*>(opt)) {
        // we don't serialize() because that would escape newlines
        return newSVpvn(optv->value.c_str(), optv->value.length());
    } else if (ConfigOptionPoint* optv = dynamic_cast<ConfigOptionPoint*>(opt)) {
        return optv->point.to_SV_pureperl();
    } else {
        std::string serialized = opt->serialize();
        return newSVpvn(serialized.c_str(), serialized.length());
    }
}

void
ConfigBase::set(t_config_option_key opt_key, SV* value) {
    ConfigOption* opt = this->option(opt_key, true);
    assert(opt != NULL);
    
    if (ConfigOptionFloat* optv = dynamic_cast<ConfigOptionFloat*>(opt)) {
        optv->value = SvNV(value);
    } else if (ConfigOptionInt* optv = dynamic_cast<ConfigOptionInt*>(opt)) {
        optv->value = SvIV(value);
    } else if (ConfigOptionString* optv = dynamic_cast<ConfigOptionString*>(opt)) {
        optv->value = std::string(SvPV_nolen(value), SvCUR(value));
    } else if (ConfigOptionPoint* optv = dynamic_cast<ConfigOptionPoint*>(opt)) {
        optv->point.from_SV(value);
    } else {
        opt->deserialize( std::string(SvPV_nolen(value)) );
    }
}
#endif

DynamicConfig::~DynamicConfig () {
    for (t_options_map::iterator it = this->options.begin(); it != this->options.end(); ++it) {
        ConfigOption* opt = it->second;
        if (opt != NULL) delete opt;
    }
}

ConfigOption*
DynamicConfig::option(const t_config_option_key opt_key, bool create) {
    t_options_map::iterator it = this->options.find(opt_key);
    if (it == this->options.end()) {
        if (create) {
            ConfigOption* opt;
            if (Options[opt_key].type == coFloat) {
                opt = new ConfigOptionFloat ();
            } else if (Options[opt_key].type == coInt) {
                opt = new ConfigOptionInt ();
            } else if (Options[opt_key].type == coString) {
                opt = new ConfigOptionString ();
            } else if (Options[opt_key].type == coFloatOrPercent) {
                opt = new ConfigOptionFloatOrPercent ();
            } else if (Options[opt_key].type == coPoint) {
                opt = new ConfigOptionPoint ();
            } else {
                throw "Unknown option type";
            }
            this->options[opt_key] = opt;
            return opt;
        } else {
            return NULL;
        }
    }
    return it->second;
}

void
DynamicConfig::keys(t_config_option_keys *keys) {
    for (t_options_map::const_iterator it = this->options.begin(); it != this->options.end(); ++it)
        keys->push_back(it->first);
}

bool
DynamicConfig::has(const t_config_option_key opt_key) const {
    t_options_map::const_iterator it = this->options.find(opt_key);
    return (it != this->options.end());
}

void
StaticConfig::keys(t_config_option_keys *keys) {
    for (t_optiondef_map::const_iterator it = Options.begin(); it != Options.end(); ++it) {
        ConfigOption* opt = this->option(it->first);
        if (opt != NULL) keys->push_back(it->first);
    }
}

}
