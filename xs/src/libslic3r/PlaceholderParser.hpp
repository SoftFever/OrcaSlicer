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
    std::map<std::string, std::string>                  m_single;
    std::map<std::string, std::vector<std::string>>     m_multiple;
    
    PlaceholderParser();
    void update_timestamp();
    void apply_config(const DynamicPrintConfig &config);
    void apply_env_variables();
    void set(const std::string &key, const std::string &value);
    void set(const std::string &key, int value);
    void set(const std::string &key, unsigned int value);
    void set(const std::string &key, double value);
    void set(const std::string &key, std::vector<std::string> values);
    std::string process(std::string str, unsigned int current_extruder_id) const;
    
private:
    bool find_and_replace(std::string &source, std::string const &find, std::string const &replace) const;
};

}

#endif
