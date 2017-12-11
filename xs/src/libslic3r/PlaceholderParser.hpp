#ifndef slic3r_PlaceholderParser_hpp_
#define slic3r_PlaceholderParser_hpp_

#include "libslic3r.h"
#include <map>
#include <string>
#include <vector>
#include "PrintConfig.hpp"

namespace Slic3r {

class PlaceholderParser
{
public:    
    PlaceholderParser();
    
    void update_timestamp();
    void apply_config(const DynamicPrintConfig &config);
    void apply_env_variables();

    // Add new ConfigOption values to m_config.
    void set(const std::string &key, const std::string &value)  { this->set(key, new ConfigOptionString(value)); }
    void set(const std::string &key, int value)                 { this->set(key, new ConfigOptionInt(value)); }
    void set(const std::string &key, unsigned int value)        { this->set(key, int(value)); }
    void set(const std::string &key, bool value)                { this->set(key, new ConfigOptionBool(value)); }
    void set(const std::string &key, double value)              { this->set(key, new ConfigOptionFloat(value)); }
    void set(const std::string &key, const std::vector<std::string> &values) { this->set(key, new ConfigOptionStrings(values)); }
    void set(const std::string &key, ConfigOption *opt)         { m_config.set_key_value(key, opt); }
    const ConfigOption* option(const std::string &key) const    { return m_config.option(key); }

    // Fill in the template.
    std::string process(const std::string &templ, unsigned int current_extruder_id, const DynamicConfig *config_override = nullptr) const;
    
private:
    DynamicConfig m_config;
};

}

#endif
