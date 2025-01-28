#ifndef slic3r_SupportMaterial_hpp_
#define slic3r_SupportMaterial_hpp_

#include "Flow.hpp"
#include "PrintConfig.hpp"
#include "Slicing.hpp"
#include "Fill/FillBase.hpp"
#include "SupportLayer.hpp"
namespace Slic3r {

class PrintObject;
class PrintConfig;
class PrintObjectConfig;

// This class manages raft and supports for a single PrintObject.
// Instantiated by Slic3r::Print::Object->_support_material()
// This class is instantiated before the slicing starts as Object.pm will query
// the parameters of the raft to determine the 1st layer height and thickness.
class PrintObjectSupportMaterial
{
public:

	struct SupportParams {
		Flow 		first_layer_flow;
		Flow 		support_material_flow;
		Flow 		support_material_interface_flow;
		Flow 		support_material_bottom_interface_flow;
		// Is merging of regions allowed? Could the interface & base support regions be printed with the same extruder?
		bool 		can_merge_support_regions;

	    coordf_t 	support_layer_height_min;
	//	coordf_t	support_layer_height_max;

		coordf_t	gap_xy;

	    float    				base_angle;
	    float    				interface_angle;
	    coordf_t 				interface_spacing;
	    coordf_t				support_expansion;
	    coordf_t 				interface_density;
	    coordf_t 				support_spacing;
	    coordf_t 				support_density;

	    InfillPattern           base_fill_pattern;
	    InfillPattern           interface_fill_pattern;
	    InfillPattern 			contact_fill_pattern;
	    bool                    with_sheath;
	};

public:
	PrintObjectSupportMaterial(const PrintObject *object, const SlicingParameters &slicing_params);

	// Is raft enabled?
	bool 		has_raft() 					const { return m_slicing_params.has_raft(); }
	// Has any support?
	bool 		has_support()				const { return m_object_config->enable_support.value || m_object_config->enforce_support_layers; }
	bool 		build_plate_only() 			const { return this->has_support() && m_object_config->support_on_build_plate_only.value; }
	// BBS
	bool 		synchronize_layers()		const { return /*m_slicing_params.soluble_interface && */!m_print_config->independent_support_layer_height.value; }
	bool 		has_contact_loops() 		const { return m_object_config->support_interface_loop_pattern.value; }

	// Generate support material for the object.
	// New support layers will be added to the object,
	// with extrusion paths and islands filled in for each support layer.
	void 		generate(PrintObject &object);

private:
	std::vector<Polygons> buildplate_covered(const PrintObject &object) const;

	// Generate top contact layers supporting overhangs.
	// For a soluble interface material synchronize the layer heights with the object, otherwise leave the layer height undefined.
	// If supports over bed surface only are requested, don't generate contact layers over an object.
	SupportGeneratorLayersPtr top_contact_layers(const PrintObject &object, const std::vector<Polygons> &buildplate_covered, SupportGeneratorLayerStorage &layer_storage) const;

	// Generate bottom contact layers supporting the top contact layers.
	// For a soluble interface material synchronize the layer heights with the object, 
	// otherwise set the layer height to a bridging flow of a support interface nozzle.
	SupportGeneratorLayersPtr bottom_contact_layers_and_layer_support_areas(
		const PrintObject &object, const SupportGeneratorLayersPtr &top_contacts, std::vector<Polygons> &buildplate_covered, 
		SupportGeneratorLayerStorage &layer_storage, std::vector<Polygons> &layer_support_areas) const;

	// Trim the top_contacts layers with the bottom_contacts layers if they overlap, so there would not be enough vertical space for both of them.
	void trim_top_contacts_by_bottom_contacts(const PrintObject &object, const SupportGeneratorLayersPtr &bottom_contacts, SupportGeneratorLayersPtr &top_contacts) const;

	// Generate raft layers and the intermediate support layers between the bottom contact and top contact surfaces.
	SupportGeneratorLayersPtr raft_and_intermediate_support_layers(
	    const PrintObject   &object,
	    const SupportGeneratorLayersPtr   &bottom_contacts,
	    const SupportGeneratorLayersPtr   &top_contacts,
	    SupportGeneratorLayerStorage	  &layer_storage) const;

	// Fill in the base layers with polygons.
	void generate_base_layers(
	    const PrintObject   &object,
	    const SupportGeneratorLayersPtr   &bottom_contacts,
	    const SupportGeneratorLayersPtr   &top_contacts,
	    SupportGeneratorLayersPtr         &intermediate_layers,
	    const std::vector<Polygons> &layer_support_areas) const;

	// Generate raft layers, also expand the 1st support layer
	// in case there is no raft layer to improve support adhesion.
    SupportGeneratorLayersPtr generate_raft_base(
    	const PrintObject   &object,
	    const SupportGeneratorLayersPtr   &top_contacts,
	    const SupportGeneratorLayersPtr   &interface_layers,
	    const SupportGeneratorLayersPtr   &base_interface_layers,
	    const SupportGeneratorLayersPtr   &base_layers,
	    SupportGeneratorLayerStorage      &layer_storage) const;

	// Turn some of the base layers into base interface layers.
	// For soluble interfaces with non-soluble bases, print maximum two first interface layers with the base
	// extruder to improve adhesion of the soluble filament to the base.
	std::pair<SupportGeneratorLayersPtr, SupportGeneratorLayersPtr> generate_interface_layers(
	    const SupportGeneratorLayersPtr   &bottom_contacts,
	    const SupportGeneratorLayersPtr   &top_contacts,
	    SupportGeneratorLayersPtr         &intermediate_layers,
	    SupportGeneratorLayerStorage      &layer_storage) const;
	

	// Trim support layers by an object to leave a defined gap between
	// the support volume and the object.
	void trim_support_layers_by_object(
	    const PrintObject   &object,
	    SupportGeneratorLayersPtr         &support_layers,
	    const coordf_t       gap_extra_above,
	    const coordf_t       gap_extra_below,
	    const coordf_t       gap_xy) const;

/*
	void generate_pillars_shape();
	void clip_with_shape();
*/

	// Produce the actual G-code.
	void generate_toolpaths(
		SupportLayerPtrs    &support_layers,
        const SupportGeneratorLayersPtr 	&raft_layers,
        const SupportGeneratorLayersPtr   &bottom_contacts,
        const SupportGeneratorLayersPtr   &top_contacts,
        const SupportGeneratorLayersPtr   &intermediate_layers,
		const SupportGeneratorLayersPtr   &interface_layers,
        const SupportGeneratorLayersPtr   &base_interface_layers) const;

	// Following objects are not owned by SupportMaterial class.
	const PrintObject 		*m_object;
	const PrintConfig 		*m_print_config;
	const PrintObjectConfig *m_object_config;
	// Pre-calculated parameters shared between the object slicer and the support generator,
	// carrying information on a raft, 1st layer height, 1st object layer height, gap between the raft and object etc.
	SlicingParameters	     m_slicing_params;
	// Various precomputed support parameters to be shared with external functions.
	SupportParams 			 m_support_params;
};

} // namespace Slic3r

#endif /* slic3r_SupportMaterial_hpp_ */
