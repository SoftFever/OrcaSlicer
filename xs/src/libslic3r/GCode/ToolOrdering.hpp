// Ordering of the tools to minimize tool switches.

#ifndef slic3r_ToolOrdering_hpp_
#define slic3r_ToolOrdering_hpp_

#include "libslic3r.h"
#include "Print.hpp"

namespace Slic3r {
namespace ToolOrdering {

struct LayerTools
{
    LayerTools(const coordf_t z) : print_z(z), wipe_tower_partitions(0) {}

    bool operator< (const LayerTools &rhs) const { return print_z <  rhs.print_z; }
    bool operator==(const LayerTools &rhs) const { return print_z == rhs.print_z; }

	coordf_t 					print_z;
	// Zero based extruder IDs, ordered to minimize tool switches.
	std::vector<unsigned int> 	extruders;
	// Number of wipe tower partitions to support the required number of tool switches
	// and to support the wipe tower partitions above this one.
    size_t                      wipe_tower_partitions;
};

// For the use case when each object is printed separately
// (print.config.complete_objects is true).
extern std::vector<LayerTools> tool_ordering(PrintObject &object);

// For the use case when all objects are printed at once.
// (print.config.complete_objects is false).
extern std::vector<LayerTools> tool_ordering(const Print &print);

} // namespace ToolOrdering
} // namespace SLic3r

#endif /* slic3r_ToolOrdering_hpp_ */
