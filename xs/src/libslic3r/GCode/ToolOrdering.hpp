// Ordering of the tools to minimize tool switches.

#ifndef slic3r_ToolOrdering_hpp_
#define slic3r_ToolOrdering_hpp_

#include "libslic3r.h"

namespace Slic3r {

class Print;
class PrintObject;

class ToolOrdering 
{
public:
	struct LayerTools
	{
	    LayerTools(const coordf_t z) :
	    	print_z(z), 
	    	has_object(false),
			has_support(false),
			has_wipe_tower(false),
	    	wipe_tower_partitions(0),
	    	wipe_tower_layer_height(0.) {}

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
	    coordf_t 					wipe_tower_layer_height;
	};

	ToolOrdering() {}

	// For the use case when each object is printed separately
	// (print.config.complete_objects is true).
	ToolOrdering(const PrintObject &object, unsigned int first_extruder = (unsigned int)-1);

	// For the use case when all objects are printed at once.
	// (print.config.complete_objects is false).
	ToolOrdering(const Print &print, unsigned int first_extruder = (unsigned int)-1);

	void 				clear() { m_layer_tools.clear(); }

	// Get the first extruder printing the layer_tools, returns -1 if there is no layer printed.
	unsigned int   		first_extruder() const;

	// Get the first extruder printing the layer_tools, returns -1 if there is no layer printed.
	unsigned int   		last_extruder() const;

	// Find LayerTools with the closest print_z.
	LayerTools&			tools_for_layer(coordf_t print_z);
	const LayerTools&	tools_for_layer(coordf_t print_z) const 
		{ return *const_cast<const LayerTools*>(&const_cast<const ToolOrdering*>(this)->tools_for_layer(print_z)); }

	const LayerTools&   front()       const { return m_layer_tools.front(); }
	const LayerTools&   back()        const { return m_layer_tools.back(); }
	bool 				empty()       const { return m_layer_tools.empty(); }
	const std::vector<LayerTools>& layer_tools() const { return m_layer_tools; }

private:
	void				initialize_layers(std::vector<coordf_t> &zs);
	void 				collect_extruders(const PrintObject &object);
	void				reorder_extruders(unsigned int last_extruder_id);
	void 				fill_wipe_tower_partitions(const PrintConfig &config, coordf_t object_bottom_z);

	std::vector<LayerTools> m_layer_tools;
};

} // namespace SLic3r

#endif /* slic3r_ToolOrdering_hpp_ */
