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

void
PlaceholderParser::update_timestamp()
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

void PlaceholderParser::apply_config(const DynamicPrintConfig &config)
{
    t_config_option_keys opt_keys = config.keys();
    for (t_config_option_keys::const_iterator i = opt_keys.begin(); i != opt_keys.end(); ++i) {
        const t_config_option_key &opt_key = *i;
        const ConfigOptionDef* def = config.def->get(opt_key);
        if (def->multiline) continue;
        
        const ConfigOption* opt = config.option(opt_key);
        if (const ConfigOptionVectorBase* optv = dynamic_cast<const ConfigOptionVectorBase*>(opt)) {
            // set placeholders for options with multiple values
            // TODO: treat [bed_shape] as single, not multiple
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

void
PlaceholderParser::apply_env_variables()
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

void
PlaceholderParser::set(const std::string &key, const std::string &value)
{
    this->_single[key] = value;
    this->_multiple.erase(key);
}

void
PlaceholderParser::set(const std::string &key, int value)
{
    std::ostringstream ss;
    ss << value;
    this->set(key, ss.str());
}

void
PlaceholderParser::set(const std::string &key, std::vector<std::string> values)
{
    if (values.empty()) {
        this->_multiple.erase(key);
        this->_single.erase(key);
    } else {
        this->_multiple[key] = values;
        this->_single[key] = values.front();
    }
}

std::string
PlaceholderParser::process(std::string str) const
{
    // replace single options, like [foo]
    for (t_strstr_map::const_iterator it = this->_single.begin(); it != this->_single.end(); ++it) {
        std::stringstream ss;
        ss << '[' << it->first << ']';
        this->find_and_replace(str, ss.str(), it->second);
    }
    
    // replace multiple options like [foo_0] by looping until we have enough values
    // or until a previous match was found (this handles non-existing indices reasonably
    // without a regex)
    for (t_strstrs_map::const_iterator it = this->_multiple.begin(); it != this->_multiple.end(); ++it) {
        const std::vector<std::string> &values = it->second;
        bool found = false;
        for (size_t i = 0; (i < values.size()) || found; ++i) {
            std::stringstream ss;
            ss << '[' << it->first << '_' << i << ']';
            if (i < values.size()) {
                found = this->find_and_replace(str, ss.str(), values[i]);
            } else {
                found = this->find_and_replace(str, ss.str(), values.front());
            }
        }
    }
    
    return str;
}

bool
PlaceholderParser::find_and_replace(std::string &source, std::string const &find, std::string const &replace) const
{
    bool found = false;
    for (std::string::size_type i = 0; (i = source.find(find, i)) != std::string::npos; ) {
        source.replace(i, find.length(), replace);
        i += replace.length();
        found = true;
    }
    return found;
}

}
