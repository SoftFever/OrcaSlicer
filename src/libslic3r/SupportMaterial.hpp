#ifndef slic3r_SupportMaterial_hpp_
#define slic3r_SupportMaterial_hpp_

#include "Flow.hpp"
#include "PrintConfig.hpp"
#include "Slicing.hpp"

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
	// Support layer type to be used by MyLayer. This type carries a much more detailed information
	// about the support layer type than the final support layers stored in a PrintObject.
	enum SupporLayerType {
		sltUnknown = 0,
		// Ratft base layer, to be printed with the support material.
		sltRaftBase,
		// Raft interface layer, to be printed with the support interface material. 
		sltRaftInterface,
		// Bottom contact layer placed over a top surface of an object. To be printed with a support interface material.
		sltBottomContact,
		// Dense interface layer, to be printed with the support interface material.
		// This layer is separated from an object by an sltBottomContact layer.
		sltBottomInterface,
		// Sparse base support layer, to be printed with a support material.
		sltBase,
		// Dense interface layer, to be printed with the support interface material.
		// This layer is separated from an object with sltTopContact layer.
		sltTopInterface,
		// Top contact layer directly supporting an overhang. To be printed with a support interface material.
		sltTopContact,
		// Some undecided type yet. It will turn into sltBase first, then it may turn into sltBottomInterface or sltTopInterface.
		sltIntermediate,
	};

	// A support layer type used internally by the SupportMaterial class. This class carries a much more detailed
	// information about the support layer than the layers stored in the PrintObject, mainly
	// the MyLayer is aware of the bridging flow and the interface gaps between the object and the support.
	class MyLayer
	{
	public:
		void reset() {
			*this = MyLayer();
		}

		bool operator==(const MyLayer &layer2) const {
			return print_z == layer2.print_z && height == layer2.height && bridging == layer2.bridging;
		}

		// Order the layers by lexicographically by an increasing print_z and a decreasing layer height.
		bool operator<(const MyLayer &layer2) const {
			if (print_z < layer2.print_z) {
				return true;
			} else if (print_z == layer2.print_z) {
			 	if (height > layer2.height)
			 		return true;
			 	else if (height == layer2.height) {
			 		// Bridging layers first.
			 	 	return bridging && ! layer2.bridging;
			 	} else
			 		return false;
			} else
				return false;
		}

		void merge(MyLayer &&rhs) {
            // The union_() does not support move semantic yet, but maybe one day it will.
            this->polygons = union_(this->polygons, std::move(rhs.polygons));
            auto merge = [](std::unique_ptr<Polygons> &dst, std::unique_ptr<Polygons> &src) {
            	if (! dst || dst->empty())
            		dst = std::move(src);
            	else if (src && ! src->empty())
        			*dst = union_(*dst, std::move(*src));
            };
            merge(this->contact_polygons,  rhs.contact_polygons);
            merge(this->overhang_polygons, rhs.overhang_polygons);
            merge(this->enforcer_polygons, rhs.enforcer_polygons);
            rhs.reset();
        }

		// For the bridging flow, bottom_print_z will be above bottom_z to account for the vertical separation.
		// For the non-bridging flow, bottom_print_z will be equal to bottom_z.
		coordf_t bottom_print_z() const { return print_z - height; }

		// To sort the extremes of top / bottom interface layers.
		coordf_t extreme_z() const { return (this->layer_type == sltTopContact) ? this->bottom_z : this->print_z; }

		SupporLayerType layer_type { sltUnknown };
		// Z used for printing, in unscaled coordinates.
		coordf_t print_z { 0 };
		// Bottom Z of this layer. For soluble layers, bottom_z + height = print_z,
		// otherwise bottom_z + gap + height = print_z.
		coordf_t bottom_z { 0 };
		// Layer height in unscaled coordinates.
    	coordf_t height { 0 };
    	// Index of a PrintObject layer_id supported by this layer. This will be set for top contact layers.
    	// If this is not a contact layer, it will be set to size_t(-1).
    	size_t 	 idx_object_layer_above { size_t(-1) };
    	// Index of a PrintObject layer_id, which supports this layer. This will be set for bottom contact layers.
    	// If this is not a contact layer, it will be set to size_t(-1).
    	size_t 	 idx_object_layer_below { size_t(-1) };
    	// Use a bridging flow when printing this support layer.
    	bool 	 bridging { false };

    	// Polygons to be filled by the support pattern.
    	Polygons polygons;
    	// Currently for the contact layers only.
    	std::unique_ptr<Polygons> contact_polygons;
    	std::unique_ptr<Polygons> overhang_polygons;
    	// Enforcers need to be propagated independently in case the "support on build plate only" option is enabled.
    	std::unique_ptr<Polygons> enforcer_polygons;
	};

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
	};

	// Layers are allocated and owned by a deque. Once a layer is allocated, it is maintained
	// up to the end of a generate() method. The layer storage may be replaced by an allocator class in the future, 
	// which would allocate layers by multiple chunks.
	typedef std::deque<MyLayer> 				MyLayerStorage;
	typedef std::vector<MyLayer*> 				MyLayersPtr;

public:
	PrintObjectSupportMaterial(const PrintObject *object, const SlicingParameters &slicing_params);

	// Is raft enabled?
	bool 		has_raft() 					const { return m_slicing_params.has_raft(); }
	// Has any support?
	bool 		has_support()				const { return m_object_config->support_material.value || m_object_config->support_material_enforce_layers; }
	bool 		build_plate_only() 			const { return this->has_support() && m_object_config->support_material_buildplate_only.value; }

	bool 		synchronize_layers()		const { return m_slicing_params.soluble_interface && m_object_config->support_material_synchronize_layers.value; }
	bool 		has_contact_loops() 		const { return m_object_config->support_material_interface_contact_loops.value; }

	// Generate support material for the object.
	// New support layers will be added to the object,
	// with extrusion paths and islands filled in for each support layer.
	void 		generate(PrintObject &object);

private:
	std::vector<Polygons> buildplate_covered(const PrintObject &object) const;

	// Generate top contact layers supporting overhangs.
	// For a soluble interface material synchronize the layer heights with the object, otherwise leave the layer height undefined.
	// If supports over bed surface only are requested, don't generate contact layers over an object.
	MyLayersPtr top_contact_layers(const PrintObject &object, const std::vector<Polygons> &buildplate_covered, MyLayerStorage &layer_storage) const;

	// Generate bottom contact layers supporting the top contact layers.
	// For a soluble interface material synchronize the layer heights with the object, 
	// otherwise set the layer height to a bridging flow of a support interface nozzle.
	MyLayersPtr bottom_contact_layers_and_layer_support_areas(
		const PrintObject &object, const MyLayersPtr &top_contacts, std::vector<Polygons> &buildplate_covered, 
		MyLayerStorage &layer_storage, std::vector<Polygons> &layer_support_areas) const;

	// Trim the top_contacts layers with the bottom_contacts layers if they overlap, so there would not be enough vertical space for both of them.
	void trim_top_contacts_by_bottom_contacts(const PrintObject &object, const MyLayersPtr &bottom_contacts, MyLayersPtr &top_contacts) const;

	// Generate raft layers and the intermediate support layers between the bottom contact and top contact surfaces.
	MyLayersPtr raft_and_intermediate_support_layers(
	    const PrintObject   &object,
	    const MyLayersPtr   &bottom_contacts,
	    const MyLayersPtr   &top_contacts,
	    MyLayerStorage	 	&layer_storage) const;

	// Fill in the base layers with polygons.
	void generate_base_layers(
	    const PrintObject   &object,
	    const MyLayersPtr   &bottom_contacts,
	    const MyLayersPtr   &top_contacts,
	    MyLayersPtr         &intermediate_layers,
	    const std::vector<Polygons> &layer_support_areas) const;

	// Generate raft layers, also expand the 1st support layer
	// in case there is no raft layer to improve support adhesion.
    MyLayersPtr generate_raft_base(
    	const PrintObject   &object,
	    const MyLayersPtr   &top_contacts,
	    const MyLayersPtr   &interface_layers,
	    const MyLayersPtr   &base_interface_layers,
	    const MyLayersPtr   &base_layers,
	    MyLayerStorage      &layer_storage) const;

	// Turn some of the base layers into base interface layers.
	// For soluble interfaces with non-soluble bases, print maximum two first interface layers with the base
	// extruder to improve adhesion of the soluble filament to the base.
	std::pair<MyLayersPtr, MyLayersPtr> generate_interface_layers(
	    const MyLayersPtr   &bottom_contacts,
	    const MyLayersPtr   &top_contacts,
	    MyLayersPtr         &intermediate_layers,
	    MyLayerStorage      &layer_storage) const;
	

	// Trim support layers by an object to leave a defined gap between
	// the support volume and the object.
	void trim_support_layers_by_object(
	    const PrintObject   &object,
	    MyLayersPtr         &support_layers,
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
        const MyLayersPtr 	&raft_layers,
        const MyLayersPtr   &bottom_contacts,
        const MyLayersPtr   &top_contacts,
        const MyLayersPtr   &intermediate_layers,
		const MyLayersPtr   &interface_layers,
        const MyLayersPtr   &base_interface_layers) const;

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
