// Ordering of the tools to minimize tool switches.

#ifndef slic3r_ToolOrdering_hpp_
#define slic3r_ToolOrdering_hpp_

#include "libslic3r.h"
#include "Print.hpp"

namespace Slic3r {
namespace ToolOrdering {

struct LayerTools
{
    LayerTools(const coordf_t z) : 
    	print_z(z), 
    	has_object(false),
		has_support(false),
		has_wipe_tower(false),
    	wipe_tower_partitions(0) {}

    bool operator< (const LayerTools &rhs) const { return print_z <  rhs.print_z; }
    bool operator==(const LayerTools &rhs) const { return print_z == rhs.print_z; }

	coordf_t 					print_z;
	bool 						has_object;
	bool						has_support;
	// Zero based extruder IDs, ordered to minimize tool switches.
	std::vector<unsigned int> 	extruders;
	// Will there be anything extruded on this layer for the wipe tower?
	// Due to the support layers possibly interleaving the object layers,
	// wipe tower will be disabled for some support only layers.
	bool 						has_wipe_tower;
	// Number of wipe tower partitions to support the required number of tool switches
	// and to support the wipe tower partitions above this one.
    size_t                      wipe_tower_partitions;
};

// For the use case when each object is printed separately
// (print.config.complete_objects is true).
extern std::vector<LayerTools> tool_ordering(const PrintObject &object, unsigned int first_extruder = (unsigned int)-1);

// For the use case when all objects are printed at once.
// (print.config.complete_objects is false).
extern std::vector<LayerTools> tool_ordering(const Print &print, unsigned int first_extruder = (unsigned int)-1);

// Get the first extruder printing the layer_tools, returns -1 if there is no layer printed.
extern unsigned int			   first_extruder(const std::vector<LayerTools> &layer_tools);

// Get the first extruder printing the layer_tools, returns -1 if there is no layer printed.
extern unsigned int			   last_extruder(const std::vector<LayerTools> &layer_tools);

} // namespace ToolOrdering
} // namespace SLic3r

#endif /* slic3r_ToolOrdering_hpp_ */
