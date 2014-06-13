#ifndef slic3r_PlaceholderParser_hpp_
#define slic3r_PlaceholderParser_hpp_


#include <myinit.h>
#include <map>
#include <string>
#include "PrintConfig.hpp"


namespace Slic3r {

class PlaceholderParser
{
    public:
    std::map<std::string, std::string> _single;
    std::map<std::string, std::string> _multiple;

    PlaceholderParser();
    ~PlaceholderParser();

    void apply_config(DynamicPrintConfig &config);

    private:
    template<class T>
    void set_multiple_from_vector(
        const std::string &key, ConfigOptionVector<T> &opt);
};

}

#endif
