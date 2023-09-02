///|/ Copyright (c) Prusa Research 2023 Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_SupportParameters_hpp_
#define slic3r_SupportParameters_hpp_

#include "../libslic3r.h"
#include "../Flow.hpp"

namespace Slic3r {

class PrintObject;
enum InfillPattern : int;

namespace FFFSupport {

struct SupportParameters {
	SupportParameters(const PrintObject &object);

    // Both top / bottom contacts and interfaces are soluble.
    bool                    soluble_interface;
    // Support contact & interface are soluble, but support base is non-soluble.
    bool                    soluble_interface_non_soluble_base;

    // Is there at least a top contact layer extruded above support base?
    bool                    has_top_contacts;
    // Is there at least a bottom contact layer extruded below support base?
    bool                    has_bottom_contacts;
    // Number of top interface layers without counting the contact layer.
    size_t                  num_top_interface_layers;
    // Number of bottom interface layers without counting the contact layer.
    size_t                  num_bottom_interface_layers;
    // Number of top base interface layers. Zero if not soluble_interface_non_soluble_base.
    size_t                  num_top_base_interface_layers;
    // Number of bottom base interface layers. Zero if not soluble_interface_non_soluble_base.
    size_t                  num_bottom_base_interface_layers;

    bool                    has_contacts() const { return this->has_top_contacts || this->has_bottom_contacts; }
    bool                    has_interfaces() const { return this->num_top_interface_layers + this->num_bottom_interface_layers > 0; }
    bool                    has_base_interfaces() const { return this->num_top_base_interface_layers + this->num_bottom_base_interface_layers > 0; }
    size_t                  num_top_interface_layers_only() const { return this->num_top_interface_layers - this->num_top_base_interface_layers; }
    size_t                  num_bottom_interface_layers_only() const { return this->num_bottom_interface_layers - this->num_bottom_base_interface_layers; }

	// Flow at the 1st print layer.
	Flow 					first_layer_flow;
	// Flow at the support base (neither top, nor bottom interface).
	// Also flow at the raft base with the exception of raft interface and contact layers.
	Flow 					support_material_flow;
	// Flow at the top interface and contact layers.
	Flow 					support_material_interface_flow;
	// Flow at the bottom interfaces and contacts.
	Flow 					support_material_bottom_interface_flow;
	// Flow at raft inteface & contact layers.
	Flow    				raft_interface_flow;
	// Is merging of regions allowed? Could the interface & base support regions be printed with the same extruder?
	bool 					can_merge_support_regions;

    coordf_t 				support_layer_height_min;
//	coordf_t				support_layer_height_max;

	coordf_t				gap_xy;

    float    				base_angle;
    float    				interface_angle;

    // Density of the top / bottom interface and contact layers.
    coordf_t 				interface_density;
    // Density of the raft interface and contact layers.
    coordf_t 				raft_interface_density;
    // Density of the base support layers.
    coordf_t 				support_density;

    // Pattern of the sparse infill including sparse raft layers.
    InfillPattern           base_fill_pattern;
    // Pattern of the top / bottom interface and contact layers.
    InfillPattern           interface_fill_pattern;
    // Pattern of the raft interface and contact layers.
    InfillPattern           raft_interface_fill_pattern;
    // Pattern of the contact layers.
    InfillPattern 			contact_fill_pattern;
    // Shall the sparse (base) layers be printed with a single perimeter line (sheath) for robustness?
    bool                    with_sheath;
    // Branches of organic supports with area larger than this threshold will be extruded with double lines.
    double                  tree_branch_diameter_double_wall_area_scaled;

    float 					raft_angle_1st_layer;
    float 					raft_angle_base;
    float 					raft_angle_interface;

    // Produce a raft interface angle for a given SupportLayer::interface_id()
    float 					raft_interface_angle(size_t interface_id) const 
    	{ return this->raft_angle_interface + ((interface_id & 1) ? float(- M_PI / 4.) : float(+ M_PI / 4.)); }
};

} // namespace FFFSupport

} // namespace Slic3r

#endif /* slic3r_SupportParameters_hpp_ */
