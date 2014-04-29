#ifndef slic3r_StringMap_hpp_
#define slic3r_StringMap_hpp_

#include <myinit.h>
#include <map>
#include <string>

namespace Slic3r {

// this is just for XSPP, because it chokes on the template typename
typedef std::map<std::string, std::string> StringMap;

}


#endif
