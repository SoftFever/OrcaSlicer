#include "Config.hpp"
#include <assert.h>
#include <ctime>
#include <fstream>
#include <iostream>
#include <exception> // std::runtime_error
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/erase.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/config.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/nowide/cenv.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
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
		const ConfigOption *other_opt = other.option(key);
        if (other_opt != nullptr && ! my_opt->deserialize(other_opt->serialize()))
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

bool ConfigBase::set_deserialize(t_config_option_key opt_key, const std::string &str, bool append)
{
    const ConfigOptionDef* optdef = this->def->get(opt_key);
    if (optdef == nullptr) {
        // If we didn't find an option, look for any other option having this as an alias.
        for (const auto &opt : this->def->options) {
            for (const t_config_option_key &opt_key2 : opt.second.aliases) {
                if (opt_key2 == opt_key) {
                    opt_key = opt_key2;
                    optdef = &opt.second;
                    break;
                }
            }
            if (optdef != nullptr)
                break;
        }
        if (optdef == nullptr)
            throw UnknownOptionException();
    }
    
    if (! optdef->shortcut.empty()) {
        for (const t_config_option_key &shortcut : optdef->shortcut)
            if (! this->set_deserialize(shortcut, str))
                return false;
        return true;
    }
    
    ConfigOption *opt = this->option(opt_key, true);
    assert(opt != nullptr);
    return opt->deserialize(str, append);
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

void ConfigBase::load(const std::string &file)
{
    namespace pt = boost::property_tree;
    pt::ptree tree;
    boost::nowide::ifstream ifs(file);
    pt::read_ini(ifs, tree);
    for (const pt::ptree::value_type &v : tree) {
        try {
            t_config_option_key opt_key = v.first;
            std::string value = v.second.get_value<std::string>();
            this->set_deserialize(opt_key, value);
        } catch (UnknownOptionException & /* e */) {
            // ignore
        }
    }
}

// Load the config keys from the tail of a G-code.
void ConfigBase::load_from_gcode(const std::string &file)
{
    // 1) Read a 64k block from the end of the G-code.
	boost::nowide::ifstream ifs(file);
	{
		const char slic3r_gcode_header[] = "; generated by Slic3r ";
		std::string firstline;
		std::getline(ifs, firstline);
		if (strncmp(slic3r_gcode_header, firstline.c_str(), strlen(slic3r_gcode_header)) != 0)
			throw std::runtime_error("Not a Slic3r generated g-code.");
	}
    ifs.seekg(0, ifs.end);
	auto file_length = ifs.tellg();
	auto data_length = std::min<std::fstream::streampos>(65535, file_length);
	ifs.seekg(file_length - data_length, ifs.beg);
	std::vector<char> data(size_t(data_length) + 1, 0);
	ifs.read(data.data(), data_length);
    ifs.close();

    // 2) Walk line by line in reverse until a non-configuration key appears.
    char *data_start = data.data();
    // boost::nowide::ifstream seems to cook the text data somehow, so less then the 64k of characters may be retrieved.
	char *end = data_start + strlen(data.data());
    size_t num_key_value_pairs = 0;
    for (;;) {
        // Extract next line.
        for (-- end; end > data_start && (*end == '\r' || *end == '\n'); -- end);
        if (end == data_start)
            break;
        char *start = end;
        *(++ end) = 0;
        for (; start > data_start && *start != '\r' && *start != '\n'; -- start);
        if (start == data_start)
            break;
        // Extracted a line from start to end. Extract the key = value pair.
        if (end - (++ start) < 10 || start[0] != ';' || start[1] != ' ')
            break;
        char *key = start + 2;
        if (! (*key >= 'a' && *key <= 'z') || (*key >= 'A' && *key <= 'Z'))
            // A key must start with a letter.
            break;
        char *sep = strchr(key, '=');
        if (sep == nullptr || sep[-1] != ' ' || sep[1] != ' ')
            break;
        char *value = sep + 2;
        if (value > end)
            break;
        char *key_end = sep - 1;
        if (key_end - key < 3)
            break;
        *key_end = 0;
        // The key may contain letters, digits and underscores.
        for (char *c = key; c != key_end; ++ c)
            if (! ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_')) {
                key = nullptr;
                break;
            }
        if (key == nullptr)
            break;
        try {
            this->set_deserialize(key, value);
            ++ num_key_value_pairs;
        } catch (UnknownOptionException & /* e */) {
            // ignore
        }
        end = start;
    }
    if (num_key_value_pairs < 90) {
        char msg[80];
        sprintf(msg, "Suspiciously low number of configuration values extracted: %d", num_key_value_pairs);
        throw std::runtime_error(msg);
    }
}

void ConfigBase::save(const std::string &file) const
{
    boost::nowide::ofstream c;
    c.open(file, std::ios::out | std::ios::trunc);
    {
        std::time_t now;
        time(&now);
        char buf[sizeof "0000-00-00 00:00:00"];
        strftime(buf, sizeof(buf), "%F %T", gmtime(&now));
        c << "# generated by Slic3r " << SLIC3R_VERSION << " on " << buf << std::endl;
    }
    for (const std::string &opt_key : this->keys())
        c << opt_key << " = " << this->serialize(opt_key) << std::endl;
    c.close();
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
