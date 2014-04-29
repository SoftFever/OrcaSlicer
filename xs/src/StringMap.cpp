#include "StringMap.hpp"
#ifdef SLIC3RXS
#include "perlglue.hpp"
#endif


namespace Slic3r {

#ifdef SLIC3RXS
__REGISTER_CLASS(StringMap, "StringMap");
#endif

}
