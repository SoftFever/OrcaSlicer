#include "Config.hpp"

namespace Slic3r {

t_optiondef_map Options = _build_optiondef_map();
FullConfig DefaultConfig = _build_default_config();

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

#ifdef SLIC3RXS
SV*
ConfigBase::get(t_config_option_key opt_key) {
    ConfigOption* opt = this->option(opt_key);
    if (opt == NULL) return &PL_sv_undef;
    if (ConfigOptionFloat* v = dynamic_cast<ConfigOptionFloat*>(opt)) {
        return newSVnv(v->value);
    } else if (ConfigOptionInt* v = dynamic_cast<ConfigOptionInt*>(opt)) {
        return newSViv(v->value);
    } else {
        throw "Unknown option value type";
    }
}
#endif

ConfigOption*
DynamicConfig::option(const t_config_option_key opt_key) {
    t_options_map::iterator it = this->options.find(opt_key);
    if (it == this->options.end()) return NULL;
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
