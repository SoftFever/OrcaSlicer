#ifndef slic3r_PlaceholderParser_hpp_
#define slic3r_PlaceholderParser_hpp_


#include <myinit.h>
#include <map>
#include <string>
#include <vector>
#include "PrintConfig.hpp"


namespace Slic3r {

class PlaceholderParser
{
    public:
    std::map<std::string, std::string> _single;
    std::map<std::string, std::string> _multiple;

    PlaceholderParser();
    void update_timestamp();
    void apply_config(DynamicPrintConfig &config);
    void set(const std::string &key, const std::string &value);
    void set(const std::string &key, int value);
    void set(const std::string &key, const std::vector<std::string> &values);
};

}

#endif
