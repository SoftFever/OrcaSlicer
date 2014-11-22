#include "Config.hpp"

namespace Slic3r {

bool
ConfigBase::has(const t_config_option_key opt_key) {
    return (this->option(opt_key, false) != NULL);
}

void
ConfigBase::apply(const ConfigBase &other, bool ignore_nonexistent) {
    // get list of option keys to apply
    t_config_option_keys opt_keys;
    other.keys(&opt_keys);
    
    // loop through options and apply them
    for (t_config_option_keys::const_iterator it = opt_keys.begin(); it != opt_keys.end(); ++it) {
        ConfigOption* my_opt = this->option(*it, true);
        if (my_opt == NULL) {
            if (ignore_nonexistent == false) throw "Attempt to apply non-existent option";
            continue;
        }
        
        // not the most efficient way, but easier than casting pointers to subclasses
        bool res = my_opt->deserialize( other.option(*it)->serialize() );
        if (!res) CONFESS("Unexpected failure when deserializing serialized value");
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
    
    t_config_option_keys my_keys;
    this->keys(&my_keys);
    for (t_config_option_keys::const_iterator opt_key = my_keys.begin(); opt_key != my_keys.end(); ++opt_key) {
        if (other.has(*opt_key) && other.serialize(*opt_key) != this->serialize(*opt_key)) {
            diff.push_back(*opt_key);
        }
    }
    
    return diff;
}

std::string
ConfigBase::serialize(const t_config_option_key opt_key) {
    ConfigOption* opt = this->option(opt_key);
    assert(opt != NULL);
    return opt->serialize();
}

bool
ConfigBase::set_deserialize(const t_config_option_key opt_key, std::string str) {
    if (this->def->count(opt_key) == 0) throw "Calling set_deserialize() on unknown option";
    ConfigOptionDef* optdef = &(*this->def)[opt_key];
    if (!optdef->shortcut.empty()) {
        for (std::vector<t_config_option_key>::iterator it = optdef->shortcut.begin(); it != optdef->shortcut.end(); ++it) {
            if (!this->set_deserialize(*it, str)) return false;
        }
        return true;
    }
    
    ConfigOption* opt = this->option(opt_key, true);
    assert(opt != NULL);
    return opt->deserialize(str);
}

double
ConfigBase::get_abs_value(const t_config_option_key opt_key) {
    ConfigOption* opt = this->option(opt_key, false);
    if (ConfigOptionFloatOrPercent* optv = dynamic_cast<ConfigOptionFloatOrPercent*>(opt)) {
        // get option definition
        assert(this->def->count(opt_key) != 0);
        ConfigOptionDef* def = &(*this->def)[opt_key];
        
        // compute absolute value over the absolute value of the base option
        return optv->get_abs_value(this->get_abs_value(def->ratio_over));
    } else if (ConfigOptionFloat* optv = dynamic_cast<ConfigOptionFloat*>(opt)) {
        return optv->value;
    } else {
        throw "Not a valid option type for get_abs_value()";
    }
}

double
ConfigBase::get_abs_value(const t_config_option_key opt_key, double ratio_over) {
    // get stored option value
    ConfigOptionFloatOrPercent* opt = dynamic_cast<ConfigOptionFloatOrPercent*>(this->option(opt_key));
    assert(opt != NULL);
    
    // compute absolute value
    return opt->get_abs_value(ratio_over);
}

#ifdef SLIC3RXS
SV*
ConfigBase::as_hash() {
    HV* hv = newHV();
    
    t_config_option_keys opt_keys;
    this->keys(&opt_keys);
    
    for (t_config_option_keys::const_iterator it = opt_keys.begin(); it != opt_keys.end(); ++it)
        (void)hv_store( hv, it->c_str(), it->length(), this->get(*it), 0 );
    
    return newRV_noinc((SV*)hv);
}

SV*
ConfigBase::get(t_config_option_key opt_key) {
    ConfigOption* opt = this->option(opt_key);
    if (opt == NULL) return &PL_sv_undef;
    if (ConfigOptionFloat* optv = dynamic_cast<ConfigOptionFloat*>(opt)) {
        return newSVnv(optv->value);
    } else if (ConfigOptionPercent* optv = dynamic_cast<ConfigOptionPercent*>(opt)) {
        return newSVnv(optv->value);
    } else if (ConfigOptionFloats* optv = dynamic_cast<ConfigOptionFloats*>(opt)) {
        AV* av = newAV();
        av_fill(av, optv->values.size()-1);
        for (std::vector<double>::iterator it = optv->values.begin(); it != optv->values.end(); ++it)
            av_store(av, it - optv->values.begin(), newSVnv(*it));
        return newRV_noinc((SV*)av);
    } else if (ConfigOptionInt* optv = dynamic_cast<ConfigOptionInt*>(opt)) {
        return newSViv(optv->value);
    } else if (ConfigOptionInts* optv = dynamic_cast<ConfigOptionInts*>(opt)) {
        AV* av = newAV();
        av_fill(av, optv->values.size()-1);
        for (std::vector<int>::iterator it = optv->values.begin(); it != optv->values.end(); ++it)
            av_store(av, it - optv->values.begin(), newSViv(*it));
        return newRV_noinc((SV*)av);
    } else if (ConfigOptionString* optv = dynamic_cast<ConfigOptionString*>(opt)) {
        // we don't serialize() because that would escape newlines
        return newSVpvn_utf8(optv->value.c_str(), optv->value.length(), true);
    } else if (ConfigOptionStrings* optv = dynamic_cast<ConfigOptionStrings*>(opt)) {
        AV* av = newAV();
        av_fill(av, optv->values.size()-1);
        for (std::vector<std::string>::iterator it = optv->values.begin(); it != optv->values.end(); ++it)
            av_store(av, it - optv->values.begin(), newSVpvn_utf8(it->c_str(), it->length(), true));
        return newRV_noinc((SV*)av);
    } else if (ConfigOptionPoint* optv = dynamic_cast<ConfigOptionPoint*>(opt)) {
        return perl_to_SV_clone_ref(optv->point);
    } else if (ConfigOptionPoints* optv = dynamic_cast<ConfigOptionPoints*>(opt)) {
        AV* av = newAV();
        av_fill(av, optv->values.size()-1);
        for (Pointfs::iterator it = optv->values.begin(); it != optv->values.end(); ++it)
            av_store(av, it - optv->values.begin(), perl_to_SV_clone_ref(*it));
        return newRV_noinc((SV*)av);
    } else if (ConfigOptionBool* optv = dynamic_cast<ConfigOptionBool*>(opt)) {
        return newSViv(optv->value ? 1 : 0);
    } else if (ConfigOptionBools* optv = dynamic_cast<ConfigOptionBools*>(opt)) {
        AV* av = newAV();
        av_fill(av, optv->values.size()-1);
        for (std::vector<bool>::iterator it = optv->values.begin(); it != optv->values.end(); ++it)
            av_store(av, it - optv->values.begin(), newSViv(*it ? 1 : 0));
        return newRV_noinc((SV*)av);
    } else {
        std::string serialized = opt->serialize();
        return newSVpvn_utf8(serialized.c_str(), serialized.length(), true);
    }
}

SV*
ConfigBase::get_at(t_config_option_key opt_key, size_t i) {
    ConfigOption* opt = this->option(opt_key);
    if (opt == NULL) return &PL_sv_undef;
    
    if (ConfigOptionFloats* optv = dynamic_cast<ConfigOptionFloats*>(opt)) {
        return newSVnv(optv->get_at(i));
    } else if (ConfigOptionInts* optv = dynamic_cast<ConfigOptionInts*>(opt)) {
        return newSViv(optv->get_at(i));
    } else if (ConfigOptionStrings* optv = dynamic_cast<ConfigOptionStrings*>(opt)) {
        // we don't serialize() because that would escape newlines
        std::string val = optv->get_at(i);
        return newSVpvn_utf8(val.c_str(), val.length(), true);
    } else if (ConfigOptionPoints* optv = dynamic_cast<ConfigOptionPoints*>(opt)) {
        return perl_to_SV_clone_ref(optv->get_at(i));
    } else if (ConfigOptionBools* optv = dynamic_cast<ConfigOptionBools*>(opt)) {
        return newSViv(optv->get_at(i) ? 1 : 0);
    } else {
        return &PL_sv_undef;
    }
}

bool
ConfigBase::set(t_config_option_key opt_key, SV* value) {
    ConfigOption* opt = this->option(opt_key, true);
    if (opt == NULL) CONFESS("Trying to set non-existing option");
    
    if (ConfigOptionFloat* optv = dynamic_cast<ConfigOptionFloat*>(opt)) {
        if (!looks_like_number(value)) return false;
        optv->value = SvNV(value);
    } else if (ConfigOptionFloats* optv = dynamic_cast<ConfigOptionFloats*>(opt)) {
        std::vector<double> values;
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        for (size_t i = 0; i < len; i++) {
            SV** elem = av_fetch(av, i, 0);
            if (elem == NULL || !looks_like_number(*elem)) return false;
            values.push_back(SvNV(*elem));
        }
        optv->values = values;
    } else if (ConfigOptionInt* optv = dynamic_cast<ConfigOptionInt*>(opt)) {
        if (!looks_like_number(value)) return false;
        optv->value = SvIV(value);
    } else if (ConfigOptionInts* optv = dynamic_cast<ConfigOptionInts*>(opt)) {
        std::vector<int> values;
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        for (size_t i = 0; i < len; i++) {
            SV** elem = av_fetch(av, i, 0);
            if (elem == NULL || !looks_like_number(*elem)) return false;
            values.push_back(SvIV(*elem));
        }
        optv->values = values;
    } else if (ConfigOptionString* optv = dynamic_cast<ConfigOptionString*>(opt)) {
        optv->value = std::string(SvPV_nolen(value), SvCUR(value));
    } else if (ConfigOptionStrings* optv = dynamic_cast<ConfigOptionStrings*>(opt)) {
        optv->values.clear();
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        for (size_t i = 0; i < len; i++) {
            SV** elem = av_fetch(av, i, 0);
            if (elem == NULL) return false;
            optv->values.push_back(std::string(SvPV_nolen(*elem), SvCUR(*elem)));
        }
    } else if (ConfigOptionPoint* optv = dynamic_cast<ConfigOptionPoint*>(opt)) {
        return optv->point.from_SV_check(value);
    } else if (ConfigOptionPoints* optv = dynamic_cast<ConfigOptionPoints*>(opt)) {
        std::vector<Pointf> values;
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        for (size_t i = 0; i < len; i++) {
            SV** elem = av_fetch(av, i, 0);
            Pointf point;
            if (elem == NULL || !point.from_SV_check(*elem)) return false;
            values.push_back(point);
        }
        optv->values = values;
    } else if (ConfigOptionBool* optv = dynamic_cast<ConfigOptionBool*>(opt)) {
        optv->value = SvTRUE(value);
    } else if (ConfigOptionBools* optv = dynamic_cast<ConfigOptionBools*>(opt)) {
        optv->values.clear();
        AV* av = (AV*)SvRV(value);
        const size_t len = av_len(av)+1;
        for (size_t i = 0; i < len; i++) {
            SV** elem = av_fetch(av, i, 0);
            if (elem == NULL) return false;
            optv->values.push_back(SvTRUE(*elem));
        }
    } else {
        if (!opt->deserialize( std::string(SvPV_nolen(value)) )) return false;
    }
    return true;
}

/* This method is implemented as a workaround for this typemap bug:
   https://rt.cpan.org/Public/Bug/Display.html?id=94110 */
bool
ConfigBase::set_deserialize(const t_config_option_key opt_key, SV* str) {
    size_t len;
    const char * c = SvPV(str, len);
    std::string value(c, len);
    
    return this->set_deserialize(opt_key, value);
}

void
ConfigBase::set_ifndef(t_config_option_key opt_key, SV* value, bool deserialize)
{
    if (!this->has(opt_key)) {
        if (deserialize) {
            this->set_deserialize(opt_key, value);
        } else {
            this->set(opt_key, value);
        }
    }
}
#endif

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
DynamicConfig::option(const t_config_option_key opt_key, bool create) {
    if (this->options.count(opt_key) == 0) {
        if (create) {
            ConfigOptionDef* optdef = &(*this->def)[opt_key];
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
DynamicConfig::opt(const t_config_option_key opt_key, bool create) {
    return dynamic_cast<T*>(this->option(opt_key, create));
}
template ConfigOptionInt* DynamicConfig::opt<ConfigOptionInt>(const t_config_option_key opt_key, bool create);
template ConfigOptionBool* DynamicConfig::opt<ConfigOptionBool>(const t_config_option_key opt_key, bool create);
template ConfigOptionBools* DynamicConfig::opt<ConfigOptionBools>(const t_config_option_key opt_key, bool create);
template ConfigOptionPercent* DynamicConfig::opt<ConfigOptionPercent>(const t_config_option_key opt_key, bool create);

const ConfigOption*
DynamicConfig::option(const t_config_option_key opt_key) const {
    return const_cast<DynamicConfig*>(this)->option(opt_key, false);
}

void
DynamicConfig::keys(t_config_option_keys *keys) const {
    for (t_options_map::const_iterator it = this->options.begin(); it != this->options.end(); ++it)
        keys->push_back(it->first);
}

void
DynamicConfig::erase(const t_config_option_key opt_key) {
    this->options.erase(opt_key);
}

void
StaticConfig::keys(t_config_option_keys *keys) const {
    for (t_optiondef_map::const_iterator it = this->def->begin(); it != this->def->end(); ++it) {
        const ConfigOption* opt = this->option(it->first);
        if (opt != NULL) keys->push_back(it->first);
    }
}

const ConfigOption*
StaticConfig::option(const t_config_option_key opt_key) const
{
    return const_cast<StaticConfig*>(this)->option(opt_key, false);
}

#ifdef SLIC3RXS
bool
StaticConfig::set(t_config_option_key opt_key, SV* value) {
    ConfigOptionDef* optdef = &(*this->def)[opt_key];
    if (!optdef->shortcut.empty()) {
        for (std::vector<t_config_option_key>::iterator it = optdef->shortcut.begin(); it != optdef->shortcut.end(); ++it) {
            if (!this->set(*it, value)) return false;
        }
        return true;
    }
    
    return static_cast<ConfigBase*>(this)->set(opt_key, value);
}
#endif

}
