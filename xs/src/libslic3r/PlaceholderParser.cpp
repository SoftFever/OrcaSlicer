#include "PlaceholderParser.hpp"
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>
#ifdef _MSC_VER
    #include <stdlib.h>  // provides **_environ
#else
    #include <unistd.h>  // provides **environ
#endif

#ifdef __APPLE__
#include <crt_externs.h>
#undef environ
#define environ (*_NSGetEnviron())
#else
    #ifdef _MSC_VER
       #define environ _environ
    #else
     	extern char **environ;
    #endif
#endif

namespace Slic3r {

PlaceholderParser::PlaceholderParser()
{
    this->set("version", SLIC3R_VERSION);
    this->apply_env_variables();
    this->update_timestamp();
}

void PlaceholderParser::update_timestamp()
{
    time_t rawtime;
    time(&rawtime);
    struct tm* timeinfo = localtime(&rawtime);
    
    {
        std::ostringstream ss;
        ss << (1900 + timeinfo->tm_year);
        ss << std::setw(2) << std::setfill('0') << (1 + timeinfo->tm_mon);
        ss << std::setw(2) << std::setfill('0') << timeinfo->tm_mday;
        ss << "-";
        ss << std::setw(2) << std::setfill('0') << timeinfo->tm_hour;
        ss << std::setw(2) << std::setfill('0') << timeinfo->tm_min;
        ss << std::setw(2) << std::setfill('0') << timeinfo->tm_sec;
        this->set("timestamp", ss.str());
    }
    this->set("year",   1900 + timeinfo->tm_year);
    this->set("month",  1 + timeinfo->tm_mon);
    this->set("day",    timeinfo->tm_mday);
    this->set("hour",   timeinfo->tm_hour);
    this->set("minute", timeinfo->tm_min);
    this->set("second", timeinfo->tm_sec);
}

// Scalar configuration values are stored into m_single,
// vector configuration values are stored into m_multiple.
// All vector configuration values stored into the PlaceholderParser
// are expected to be addressed by the extruder ID, therefore
// if a vector configuration value is addressed without an index,
// a current extruder ID is used.
void PlaceholderParser::apply_config(const DynamicPrintConfig &config)
{
    for (const t_config_option_key &opt_key : config.keys()) {
        const ConfigOptionDef* def = config.def()->get(opt_key);
        if (def->multiline || opt_key == "post_process")
            continue;

        const ConfigOption* opt = config.option(opt_key);
        const ConfigOptionVectorBase* optv = dynamic_cast<const ConfigOptionVectorBase*>(opt);
        if (optv != nullptr && opt_key != "bed_shape") {
            // set placeholders for options with multiple values
            this->set(opt_key, optv->vserialize());
        } else if (const ConfigOptionPoint* optp = dynamic_cast<const ConfigOptionPoint*>(opt)) {
            this->set(opt_key, optp->serialize());
            Pointf val = *optp;
            this->set(opt_key + "_X", val.x);
            this->set(opt_key + "_Y", val.y);
        } else {
            // set single-value placeholders
            this->set(opt_key, opt->serialize());
        }
    }
}

void PlaceholderParser::apply_env_variables()
{
    for (char** env = environ; *env; env++) {
        if (strncmp(*env, "SLIC3R_", 7) == 0) {
            std::stringstream ss(*env);
            std::string key, value;
            std::getline(ss, key, '=');
            ss >> value;
            
            this->set(key, value);
        }
    }
}

void PlaceholderParser::set(const std::string &key, const std::string &value)
{
    m_single[key] = value;
    m_multiple.erase(key);
}

void PlaceholderParser::set(const std::string &key, int value)
{
    std::ostringstream ss;
    ss << value;
    this->set(key, ss.str());
}

void PlaceholderParser::set(const std::string &key, unsigned int value)
{
    std::ostringstream ss;
    ss << value;
    this->set(key, ss.str());
}

void PlaceholderParser::set(const std::string &key, double value)
{
    std::ostringstream ss;
    ss << value;
    this->set(key, ss.str());
}

void PlaceholderParser::set(const std::string &key, std::vector<std::string> values)
{
    m_single.erase(key);
    if (values.empty())
        m_multiple.erase(key);
    else
        m_multiple[key] = values;
}

std::string PlaceholderParser::process(std::string str, unsigned int current_extruder_id) const
{
    char key[2048];

    // Replace extruder independent single options, like [foo].
    for (const auto &key_value : m_single) {
        sprintf(key, "[%s]", key_value.first.c_str());
        const std::string &replace = key_value.second;
        for (size_t i = 0; (i = str.find(key, i)) != std::string::npos;) {
            str.replace(i, key_value.first.size() + 2, replace);
            i += replace.size();
        }
    }

    // Replace extruder dependent single options with the value for the active extruder.
    // For example, [temperature] will be replaced with the current extruder temperature.
    for (const auto &key_value : m_multiple) {
		sprintf(key, "[%s]", key_value.first.c_str());
        const std::string &replace = key_value.second[(current_extruder_id < key_value.second.size()) ? current_extruder_id : 0];
        for (size_t i = 0; (i = str.find(key, i)) != std::string::npos;) {
            str.replace(i, key_value.first.size() + 2, replace);
            i += replace.size();
        }
    }

    // Replace multiple options like [foo_0].
    for (const auto &key_value : m_multiple) {
        sprintf(key, "[%s_", key_value.first.c_str());
        const std::vector<std::string> &values = key_value.second;
        for (size_t i = 0; (i = str.find(key, i)) != std::string::npos;) {
            size_t k = str.find(']', i + key_value.first.size() + 2);
            if (k != std::string::npos) {
                // Parse the key index and the closing bracket.
                ++ k;
                int idx = 0;
                if (sscanf(str.c_str() + i + key_value.first.size() + 2, "%d]", &idx) == 1 && idx >= 0) {
                    if (idx >= int(values.size()))
                        idx = 0;
                    str.replace(i, k - i, values[idx]);
                    i += values[idx].size();
                    continue;
                }
            }
            // The key does not match the pattern [foo_%d]. Skip just [foo_.] with the hope that there was a missing ']',
            // so an opening '[' may be found somewhere before the position k.
            i += key_value.first.size() + 3;
        }
    }
    
    return str;
}

}
