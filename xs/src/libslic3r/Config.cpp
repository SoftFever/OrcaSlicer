#include "Config.hpp"
#include <stdlib.h>  // for setenv()
#include <assert.h>
#include <string.h>

#if defined(_WIN32) && !defined(setenv) && defined(_putenv_s)
#define setenv(k, v, o) _putenv_s(k, v)
#endif

namespace Slic3r {

std::string escape_string_cstyle(const std::string &str)
{
    // Allocate a buffer twice the input string length,
    // so the output will fit even if all input characters get escaped.
    std::vector<char> out(str.size() * 2, 0);
    char *outptr = out.data();
    for (size_t i = 0; i < str.size(); ++ i) {
        char c = str[i];
        if (c == '\n' || c == '\r') {
            (*outptr ++) = '\\';
            (*outptr ++) = 'n';
        } else
            (*outptr ++) = c;
    }
    return std::string(out.data(), outptr - out.data());
}

std::string escape_strings_cstyle(const std::vector<std::string> &strs)
{
    // 1) Estimate the output buffer size to avoid buffer reallocation.
    size_t outbuflen = 0;
    for (size_t i = 0; i < strs.size(); ++ i)
        // Reserve space for every character escaped + quotes + semicolon.
        outbuflen += strs[i].size() * 2 + 3;
    // 2) Fill in the buffer.
    std::vector<char> out(outbuflen, 0);
    char *outptr = out.data();
    for (size_t j = 0; j < strs.size(); ++ j) {
        if (j > 0)
            // Separate the strings.
            (*outptr ++) = ';';
        const std::string &str = strs[j];
        // Is the string simple or complex? Complex string contains spaces, tabs, new lines and other
        // escapable characters. Empty string shall be quoted as well, if it is the only string in strs.
        bool should_quote = strs.size() == 1 && str.empty();
        for (size_t i = 0; i < str.size(); ++ i) {
            char c = str[i];
            if (c == ' ' || c == '\t' || c == '\\' || c == '"' || c == '\r' || c == '\n') {
                should_quote = true;
                break;
            }
        }
        if (should_quote) {
            (*outptr ++) = '"';
            for (size_t i = 0; i < str.size(); ++ i) {
                char c = str[i];
                if (c == '\\' || c == '"') {
                    (*outptr ++) = '\\';
                    (*outptr ++) = c;
                } else if (c == '\n' || c == '\r') {
                    (*outptr ++) = '\\';
                    (*outptr ++) = 'n';
                } else
                    (*outptr ++) = c;
            }
            (*outptr ++) = '"';
        } else {
            memcpy(outptr, str.data(), str.size());
            outptr += str.size();
        }
    }
    return std::string(out.data(), outptr - out.data());
}

bool unescape_string_cstyle(const std::string &str, std::string &str_out)
{
    std::vector<char> out(str.size(), 0);
    char *outptr = out.data();
    for (size_t i = 0; i < str.size(); ++ i) {
        char c = str[i];
        if (c == '\\') {
            if (++ i == str.size())
                return false;
            c = str[i];
            if (c == 'n')
                (*outptr ++) = '\n';
        } else
            (*outptr ++) = c;
    }
    str_out.assign(out.data(), outptr - out.data());
    return true;
}

bool unescape_strings_cstyle(const std::string &str, std::vector<std::string> &out)
{
    out.clear();
    if (str.empty())
        return true;

    size_t i = 0;
    for (;;) {
        // Skip white spaces.
        char c = str[i];
        while (c == ' ' || c == '\t') {
            if (++ i == str.size())
                return true;
            c = str[i];
        }
        // Start of a word.
        std::vector<char> buf;
        buf.reserve(16);
        // Is it enclosed in quotes?
        c = str[i];
        if (c == '"') {
            // Complex case, string is enclosed in quotes.
            for (++ i; i < str.size(); ++ i) {
                c = str[i];
                if (c == '"') {
                    // End of string.
                    break;
                }
                if (c == '\\') {
                    if (++ i == str.size())
                        return false;
                    c = str[i];
                    if (c == 'n')
                        c = '\n';
                }
                buf.push_back(c);
            }
            if (i == str.size())
                return false;
            ++ i;
        } else {
            for (; i < str.size(); ++ i) {
                c = str[i];
                if (c == ';')
                    break;
                buf.push_back(c);
            }
        }
        // Store the string into the output vector.
        out.push_back(std::string(buf.data(), buf.size()));
        if (i == str.size())
            return true;
        // Skip white spaces.
        c = str[i];
        while (c == ' ' || c == '\t') {
            if (++ i == str.size())
                // End of string. This is correct.
                return true;
            c = str[i];
        }
        if (c != ';')
            return false;
        if (++ i == str.size()) {
            // Emit one additional empty string.
            out.push_back(std::string());
            return true;
        }
    }
}

void ConfigBase::apply(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent)
{
    // loop through options and apply them
    for (const t_config_option_key &key : keys) {
        ConfigOption *my_opt = this->option(key, true);
        if (my_opt == nullptr) {
            if (! ignore_nonexistent)
                throw "Attempt to apply non-existent option";
            continue;
        }
        // not the most efficient way, but easier than casting pointers to subclasses
        if (! my_opt->deserialize(other.option(key)->serialize()))
            CONFESS((std::string("Unexpected failure when deserializing serialized value for ") + key).c_str());
    }
}

// this will *ignore* options not present in both configs
t_config_option_keys ConfigBase::diff(const ConfigBase &other) const
{
    t_config_option_keys diff;
    for (const t_config_option_key &opt_key : this->keys())
        if (other.has(opt_key) && other.serialize(opt_key) != this->serialize(opt_key))
            diff.push_back(opt_key);
    return diff;
}

std::string ConfigBase::serialize(const t_config_option_key &opt_key) const
{
    const ConfigOption* opt = this->option(opt_key);
    assert(opt != nullptr);
    return opt->serialize();
}

bool ConfigBase::set_deserialize(const t_config_option_key &opt_key, std::string str)
{
    const ConfigOptionDef* optdef = this->def->get(opt_key);
    if (optdef == NULL) throw "Calling set_deserialize() on unknown option";
    if (!optdef->shortcut.empty()) {
        for (std::vector<t_config_option_key>::const_iterator it = optdef->shortcut.begin(); it != optdef->shortcut.end(); ++it) {
            if (!this->set_deserialize(*it, str)) return false;
        }
        return true;
    }
    ConfigOption* opt = this->option(opt_key, true);
    assert(opt != nullptr);
    return opt->deserialize(str);
}

// Return an absolute value of a possibly relative config variable.
// For example, return absolute infill extrusion width, either from an absolute value, or relative to the layer height.
double ConfigBase::get_abs_value(const t_config_option_key &opt_key) const
{
    const ConfigOption* opt = this->option(opt_key);
    if (const ConfigOptionFloatOrPercent* optv = dynamic_cast<const ConfigOptionFloatOrPercent*>(opt)) {
        // get option definition
        const ConfigOptionDef* def = this->def->get(opt_key);
        assert(def != nullptr);
        // compute absolute value over the absolute value of the base option
        return optv->get_abs_value(this->get_abs_value(def->ratio_over));
    } else if (const ConfigOptionFloat* optv = dynamic_cast<const ConfigOptionFloat*>(opt)) {
        return optv->value;
    } else {
        throw "Not a valid option type for get_abs_value()";
    }
}

// Return an absolute value of a possibly relative config variable.
// For example, return absolute infill extrusion width, either from an absolute value, or relative to a provided value.
double ConfigBase::get_abs_value(const t_config_option_key &opt_key, double ratio_over) const 
{
    // get stored option value
    const ConfigOptionFloatOrPercent* opt = dynamic_cast<const ConfigOptionFloatOrPercent*>(this->option(opt_key));
    assert(opt != nullptr);
    // compute absolute value
    return opt->get_abs_value(ratio_over);
}

void ConfigBase::setenv_()
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

ConfigOption* DynamicConfig::optptr(const t_config_option_key &opt_key, bool create) {
    t_options_map::iterator it = options.find(opt_key);
    if (it == options.end()) {
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
            } else if (optdef->type == coPercents) {
                opt = new ConfigOptionPercents ();
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
    return it->second;
}

template<class T>
T* DynamicConfig::opt(const t_config_option_key &opt_key, bool create) {
    return dynamic_cast<T*>(this->option(opt_key, create));
}
template ConfigOptionInt* DynamicConfig::opt<ConfigOptionInt>(const t_config_option_key &opt_key, bool create);
template ConfigOptionBool* DynamicConfig::opt<ConfigOptionBool>(const t_config_option_key &opt_key, bool create);
template ConfigOptionBools* DynamicConfig::opt<ConfigOptionBools>(const t_config_option_key &opt_key, bool create);
template ConfigOptionPercent* DynamicConfig::opt<ConfigOptionPercent>(const t_config_option_key &opt_key, bool create);

t_config_option_keys DynamicConfig::keys() const
{
    t_config_option_keys keys;
    keys.reserve(this->options.size());
    for (const auto &opt : this->options)
        keys.emplace_back(opt.first);
    return keys;
}

void StaticConfig::set_defaults()
{
    // use defaults from definition
    if (this->def != nullptr) {
        for (const std::string &key : this->keys()) {
            const ConfigOptionDef* def = this->def->get(key);
            if (def->default_value != nullptr)
                this->option(key)->set(*def->default_value);
        }
    }
}

t_config_option_keys StaticConfig::keys() const 
{
    t_config_option_keys keys;
	assert(this->def != nullptr);
    for (const auto &opt_def : this->def->options)
        if (this->option(opt_def.first) != nullptr) 
            keys.push_back(opt_def.first);
    return keys;
}

}
