#ifndef slic3r_PlaceholderParser_hpp_
#define slic3r_PlaceholderParser_hpp_


#include <myinit.h>
#include <map>
#include <string>


namespace Slic3r {

class PlaceholderParser
{
    public:
    std::map<std::string, std::string> _single;
    std::map<std::string, std::string> _multiple;

    PlaceholderParser();
    ~PlaceholderParser();
};

}

#endif
