#ifndef slic3r_Layer_hpp_
#define slic3r_Layer_hpp_

#include <myinit.h>

namespace Slic3r {

typedef std::pair<coordf_t,coordf_t> t_layer_height_range;
typedef std::map<t_layer_height_range,coordf_t> t_layer_height_ranges;

}

#endif
