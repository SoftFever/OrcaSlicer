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
    void update_timestamp();
    void apply_config(DynamicPrintConfig &config);
    void set(const std::string &key, const std::string &value);

    private:
    template<class T>
    void set_multiple_from_vector(
        const std::string &key, ConfigOptionVector<T> &opt);
    std::string _int_to_string(int value) const;
};

}

#endif
