#ifndef slic3r_PlaceholderParser_hpp_
#define slic3r_PlaceholderParser_hpp_


#include <myinit.h>
#include <map>
#include <string>
#include <vector>
#include "PrintConfig.hpp"


namespace Slic3r {

typedef std::map<std::string, std::string> t_strstr_map;
typedef std::map<std::string, std::vector<std::string> > t_strstrs_map;

class PlaceholderParser
{
    public:
    t_strstr_map _single;
    t_strstrs_map _multiple;
    
    PlaceholderParser();
    void update_timestamp();
    void apply_config(DynamicPrintConfig &config);
    void apply_env_variables();
    void set(const std::string &key, const std::string &value);
    void set(const std::string &key, int value);
    void set(const std::string &key, std::vector<std::string> values);
    std::string process(std::string str) const;
    
    private:
    bool find_and_replace(std::string &source, std::string const &find, std::string const &replace) const;
};

}

#endif
