#include "Config.hpp"
#include "Utils.hpp"
#include <assert.h>
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
#include <string.h>

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

void ConfigBase::apply_only(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent)
{
    // loop through options and apply them
    for (const t_config_option_key &opt_key : keys) {
        // Create a new option with default value for the key.
        // If the key is not in the parameter definition, or this ConfigBase is a static type and it does not support the parameter,
        // an exception is thrown if not ignore_nonexistent.
        ConfigOption *my_opt = this->option(opt_key, true);
        if (my_opt == nullptr) {
            // opt_key does not exist in this ConfigBase and it cannot be created, because it is not defined by this->def().
            // This is only possible if other is of DynamicConfig type.
            if (ignore_nonexistent)
                continue;
            throw UnknownOptionException(opt_key);
        }
		const ConfigOption *other_opt = other.option(opt_key);
        if (other_opt != nullptr)
            my_opt->set(other_opt);
    }
}

// this will *ignore* options not present in both configs
t_config_option_keys ConfigBase::diff(const ConfigBase &other) const
{
    t_config_option_keys diff;
    for (const t_config_option_key &opt_key : this->keys()) {
        const ConfigOption *this_opt  = this->option(opt_key);
        const ConfigOption *other_opt = other.option(opt_key);
        if (this_opt != nullptr && other_opt != nullptr && *this_opt != *other_opt)
            diff.emplace_back(opt_key);
    }
    return diff;
}

t_config_option_keys ConfigBase::equal(const ConfigBase &other) const
{
    t_config_option_keys equal;
    for (const t_config_option_key &opt_key : this->keys()) {
        const ConfigOption *this_opt  = this->option(opt_key);
        const ConfigOption *other_opt = other.option(opt_key);
        if (this_opt != nullptr && other_opt != nullptr && *this_opt == *other_opt)
            equal.emplace_back(opt_key);
    }
    return equal;
}

std::string ConfigBase::serialize(const t_config_option_key &opt_key) const
{
    const ConfigOption* opt = this->option(opt_key);
    assert(opt != nullptr);
    return opt->serialize();
}

bool ConfigBase::set_deserialize(const t_config_option_key &opt_key_src, const std::string &value_src, bool append)
{
    t_config_option_key opt_key = opt_key_src;
    std::string         value   = value_src;
    // Both opt_key and value may be modified by _handle_legacy().
    // If the opt_key is no more valid in this version of Slic3r, opt_key is cleared by _handle_legacy().
    this->handle_legacy(opt_key, value);
    if (opt_key.empty())
        // Ignore the option.
        return true;
    return this->set_deserialize_raw(opt_key, value, append);
}

bool ConfigBase::set_deserialize_raw(const t_config_option_key &opt_key_src, const std::string &value, bool append)
{
    t_config_option_key opt_key = opt_key_src;
    // Try to deserialize the option by its name.
    const ConfigDef       *def    = this->def();
    if (def == nullptr)
        throw NoDefinitionException(opt_key);
    const ConfigOptionDef *optdef = def->get(opt_key);
    if (optdef == nullptr) {
        // If we didn't find an option, look for any other option having this as an alias.
        for (const auto &opt : def->options) {
            for (const t_config_option_key &opt_key2 : opt.second.aliases) {
                if (opt_key2 == opt_key) {
                    opt_key = opt.first;
                    optdef = &opt.second;
                    break;
                }
            }
            if (optdef != nullptr)
                break;
        }
        if (optdef == nullptr)
            throw UnknownOptionException(opt_key);
    }
    
    if (! optdef->shortcut.empty()) {
        // Aliasing for example "solid_layers" to "top_solid_layers" and "bottom_solid_layers".
        for (const t_config_option_key &shortcut : optdef->shortcut)
            // Recursive call.
            if (! this->set_deserialize_raw(shortcut, value, append))
                return false;
        return true;
    }
    
    ConfigOption *opt = this->option(opt_key, true);
    assert(opt != nullptr);
    return opt->deserialize(value, append);
}

// Return an absolute value of a possibly relative config variable.
// For example, return absolute infill extrusion width, either from an absolute value, or relative to the layer height.
double ConfigBase::get_abs_value(const t_config_option_key &opt_key) const
{
    // Get stored option value.
    const ConfigOption *raw_opt = this->option(opt_key);
    assert(raw_opt != nullptr);
    if (raw_opt->type() == coFloat)
        return static_cast<const ConfigOptionFloat*>(raw_opt)->value;
    if (raw_opt->type() == coFloatOrPercent) {
        // Get option definition.
        const ConfigDef *def = this->def();
        if (def == nullptr)
            throw NoDefinitionException(opt_key);
        const ConfigOptionDef *opt_def = def->get(opt_key);
        assert(opt_def != nullptr);
        // Compute absolute value over the absolute value of the base option.
        //FIXME there are some ratio_over chains, which end with empty ratio_with.
        // For example, XXX_extrusion_width parameters are not handled by get_abs_value correctly.
        return opt_def->ratio_over.empty() ? 0. : 
            static_cast<const ConfigOptionFloatOrPercent*>(raw_opt)->get_abs_value(this->get_abs_value(opt_def->ratio_over));
    }
    throw std::runtime_error("ConfigBase::get_abs_value(): Not a valid option type for get_abs_value()");
}

// Return an absolute value of a possibly relative config variable.
// For example, return absolute infill extrusion width, either from an absolute value, or relative to a provided value.
double ConfigBase::get_abs_value(const t_config_option_key &opt_key, double ratio_over) const 
{
    // Get stored option value.
    const ConfigOption *raw_opt = this->option(opt_key);
    assert(raw_opt != nullptr);
    if (raw_opt->type() != coFloatOrPercent)
        throw std::runtime_error("ConfigBase::get_abs_value(): opt_key is not of coFloatOrPercent");
    // Compute absolute value.
    return static_cast<const ConfigOptionFloatOrPercent*>(raw_opt)->get_abs_value(ratio_over);
}

void ConfigBase::setenv_()
{
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
        
        boost::nowide::setenv(envname.c_str(), this->serialize(*it).c_str(), 1);
    }
}

void ConfigBase::load(const std::string &file)
{
    if (boost::iends_with(file, ".gcode") || boost::iends_with(file, ".g"))
        this->load_from_gcode_file(file);
    else
        this->load_from_ini(file);
}

void ConfigBase::load_from_ini(const std::string &file)
{
    boost::property_tree::ptree tree;
    boost::nowide::ifstream ifs(file);
    boost::property_tree::read_ini(ifs, tree);
    this->load(tree);
}

void ConfigBase::load(const boost::property_tree::ptree &tree)
{
    for (const boost::property_tree::ptree::value_type &v : tree) {
        try {
            t_config_option_key opt_key = v.first;
            this->set_deserialize(opt_key, v.second.get_value<std::string>());
        } catch (UnknownOptionException & /* e */) {
            // ignore
        }
    }
}

// Load the config keys from the tail of a G-code file.
void ConfigBase::load_from_gcode_file(const std::string &file)
{
    // Read a 64k block from the end of the G-code.
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

    load_from_gcode_string(data.data());
}

// Load the config keys from the given string.
void ConfigBase::load_from_gcode_string(const char* str)
{
    if (str == nullptr)
        return;

    // Walk line by line in reverse until a non-configuration key appears.
    char *data_start = const_cast<char*>(str);
    // boost::nowide::ifstream seems to cook the text data somehow, so less then the 64k of characters may be retrieved.
    char *end = data_start + strlen(str);
    size_t num_key_value_pairs = 0;
    for (;;) {
        // Extract next line.
        for (--end; end > data_start && (*end == '\r' || *end == '\n'); --end);
        if (end == data_start)
            break;
        char *start = end;
        *(++end) = 0;
        for (; start > data_start && *start != '\r' && *start != '\n'; --start);
        if (start == data_start)
            break;
        // Extracted a line from start to end. Extract the key = value pair.
        if (end - (++start) < 10 || start[0] != ';' || start[1] != ' ')
            break;
        char *key = start + 2;
        if (!(*key >= 'a' && *key <= 'z') || (*key >= 'A' && *key <= 'Z'))
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
        for (char *c = key; c != key_end; ++c)
            if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_')) {
                key = nullptr;
                break;
            }
        if (key == nullptr)
            break;
        try {
            this->set_deserialize(key, value);
            ++num_key_value_pairs;
        }
        catch (UnknownOptionException & /* e */) {
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
    c << "# " << Slic3r::header_slic3r_generated() << std::endl;
    for (const std::string &opt_key : this->keys())
        c << opt_key << " = " << this->serialize(opt_key) << std::endl;
    c.close();
}

bool DynamicConfig::operator==(const DynamicConfig &rhs) const
{
    t_options_map::const_iterator it1     = this->options.begin();
    t_options_map::const_iterator it1_end = this->options.end();
    t_options_map::const_iterator it2     = rhs.options.begin();
    t_options_map::const_iterator it2_end = rhs.options.end();
    for (; it1 != it1_end && it2 != it2_end; ++ it1, ++ it2)
        if (*it1->second != *it2->second)
            return false;
    return it1 == it1_end && it2 == it2_end;
}

ConfigOption* DynamicConfig::optptr(const t_config_option_key &opt_key, bool create)
{
    t_options_map::iterator it = options.find(opt_key);
    if (it != options.end())
        // Option was found.
        return it->second;
    if (! create)
        // Option was not found and a new option shall not be created.
        return nullptr;
    // Try to create a new ConfigOption.
    const ConfigDef       *def    = this->def();
    if (def == nullptr)
        throw NoDefinitionException(opt_key);
    const ConfigOptionDef *optdef = def->get(opt_key);
    if (optdef == nullptr)
//        throw std::runtime_error(std::string("Invalid option name: ") + opt_key);
        // Let the parent decide what to do if the opt_key is not defined by this->def().
        return nullptr;
    ConfigOption *opt = nullptr;
    switch (optdef->type) {
    case coFloat:           opt = new ConfigOptionFloat();          break;
    case coFloats:          opt = new ConfigOptionFloats();         break;
    case coInt:             opt = new ConfigOptionInt();            break;
    case coInts:            opt = new ConfigOptionInts();           break;
    case coString:          opt = new ConfigOptionString();         break;
    case coStrings:         opt = new ConfigOptionStrings();        break;
    case coPercent:         opt = new ConfigOptionPercent();        break;
    case coPercents:        opt = new ConfigOptionPercents();       break;
    case coFloatOrPercent:  opt = new ConfigOptionFloatOrPercent(); break;
    case coPoint:           opt = new ConfigOptionPoint();          break;
    case coPoints:          opt = new ConfigOptionPoints();         break;
    case coBool:            opt = new ConfigOptionBool();           break;
    case coBools:           opt = new ConfigOptionBools();          break;
    case coEnum:            opt = new ConfigOptionEnumGeneric(optdef->enum_keys_map); break;
    default:                throw std::runtime_error(std::string("Unknown option type for option ") + opt_key);
    }
    this->options[opt_key] = opt;
    return opt;
}

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
    auto *defs = this->def();
    if (defs != nullptr) {
        for (const std::string &key : this->keys()) {
            const ConfigOptionDef   *def = defs->get(key);
            ConfigOption            *opt = this->option(key);
            if (def != nullptr && opt != nullptr && def->default_value != nullptr)
                opt->set(def->default_value);
        }
    }
}

t_config_option_keys StaticConfig::keys() const 
{
    t_config_option_keys keys;
	assert(this->def != nullptr);
    for (const auto &opt_def : this->def()->options)
        if (this->option(opt_def.first) != nullptr) 
            keys.push_back(opt_def.first);
    return keys;
}

}
