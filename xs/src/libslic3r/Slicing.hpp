// Based on implementation by @platsch

#ifndef slic3r_Slicing_hpp_
#define slic3r_Slicing_hpp_

#include "libslic3r.h"

namespace Slic3r
{

struct SlicingParameters
{
	SlicingParameters() { memset(this, 0, sizeof(SlicingParameters)); }

	// The regular layer height, applied for all but the first layer, if not overridden by layer ranges
	// or by the variable layer thickness table.
    coordf_t    layer_height;

    // Thickness of the first layer. This is either the first print layer thickness if printed without a raft,
    // or a bridging flow thickness if printed over a non-soluble raft,
    // or a normal layer height if printed over a soluble raft.
    coordf_t    first_layer_height;

    // If the object is printed over a non-soluble raft, the first layer may be printed with a briding flow.
    bool 		first_layer_bridging;

    // Minimum / maximum layer height, to be used for the automatic adaptive layer height algorithm,
    // or by an interactive layer height editor.
    coordf_t    min_layer_height;
    coordf_t    max_layer_height;

    // Bottom and top of the printed object.
    // If printed without a raft, object_print_z_min = 0 and object_print_z_max = object height.
    // Otherwise object_print_z_min is equal to the raft height.
    coordf_t 	object_print_z_min;
    coordf_t 	object_print_z_max;
};

typedef std::pair<coordf_t,coordf_t> t_layer_height_range;
typedef std::map<t_layer_height_range,coordf_t> t_layer_height_ranges;

}; // namespace Slic3r

#endif /* slic3r_Slicing_hpp_ */
