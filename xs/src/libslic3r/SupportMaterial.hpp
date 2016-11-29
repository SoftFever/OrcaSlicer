#ifndef slic3r_SupportMaterial_hpp_
#define slic3r_SupportMaterial_hpp_

#include "Flow.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

class PrintObject;
class PrintConfig;
class PrintObjectConfig;

// how much we extend support around the actual contact area
#define SUPPORT_MATERIAL_MARGIN 1.5	

// This class manages raft and supports for a single PrintObject.
// Instantiated by Slic3r::Print::Object->_support_material()
// This class is instantiated before the slicing starts as Object.pm will query
// the parameters of the raft to determine the 1st layer height and thickness.
class PrintObjectSupportMaterial
{
public:
	enum SupporLayerType {
		sltUnknown = 0,
		sltRaft,
		stlFirstLayer,
		sltBottomContact,
		sltBottomInterface,
		sltBase,
		sltTopInterface,
		sltTopContact,
		// Some undecided type yet. It will turn into stlBase first, then it may turn into stlBottomInterface or stlTopInterface.
		stlIntermediate,
	};

	class MyLayer
	{
	public:
		MyLayer() :
			layer_type(sltUnknown),
			print_z(0.),
			bottom_z(0.),
			height(0.),
			idx_object_layer_above(size_t(-1)),
			idx_object_layer_below(size_t(-1)),
			bridging(false),
			aux_polygons(NULL)
			{}

		~MyLayer() 
		{
			delete aux_polygons;
			aux_polygons = NULL;
		}

		bool operator==(const MyLayer &layer2) const {
			return print_z == layer2.print_z && height == layer2.height && bridging == layer2.bridging;
		}

		bool operator<(const MyLayer &layer2) const {
			if (print_z < layer2.print_z) {
				return true;
			} else if (print_z == layer2.print_z) {
			 	if (height > layer2.height)
			 		return true;
			 	else if (height == layer2.height) {
			 	 	return bridging < layer2.bridging;
			 	} else
			 		return false;
			} else
				return false;
		}

		SupporLayerType layer_type;
		// Z used for printing in unscaled coordinates
		coordf_t print_z;
		// Bottom height of this layer. For soluble layers, bottom_z + height = print_z,
		// otherwise bottom_z + gap + height = print_z.
		coordf_t bottom_z;
		// layer height in unscaled coordinates
    	coordf_t height;
    	// Index of a PrintObject layer_id supported by this layer. This will be set for top contact layers.
    	// If this is not a contact layer, it will be set to size_t(-1).
    	size_t 	 idx_object_layer_above;
    	// Index of a PrintObject layer_id, which supports this layer. This will be set for bottom contact layers.
    	// If this is not a contact layer, it will be set to size_t(-1).
    	size_t 	 idx_object_layer_below;
    	// Use a bridging flow when printing this support layer.
    	bool 	 bridging;

    	// Polygons to be filled by the support pattern.
    	Polygons polygons;
    	// Currently for the contact layers only: Overhangs are stored here.
    	Polygons *aux_polygons;
	};

	struct LayerExtreme
	{
		LayerExtreme(MyLayer *alayer, bool ais_top) : layer(alayer), is_top(ais_top) {}
		MyLayer 	*layer;
		// top or bottom extreme
		bool   		 is_top;

		coordf_t	z() const { return is_top ? layer->print_z : layer->print_z - layer->height; }

		bool operator<(const LayerExtreme &other) const { return z() < other.z(); }
	};

/*
	struct LayerPrintZ_Hash {
		size_t operator()(const MyLayer &layer) const { 
			return std::hash<double>()(layer.print_z)^std::hash<double>()(layer.height)^size_t(layer.bridging);
		}
	};
*/

	typedef std::vector<MyLayer*> 				MyLayersPtr;
	typedef std::deque<MyLayer> 				MyLayerStorage;

public:
	PrintObjectSupportMaterial(const PrintObject *object);

	// Height of the 1st layer is user configured as it is important for the print
	// to stick to he print bed.
	coordf_t	first_layer_height() 		const { return m_object_config->first_layer_height.value; }

	// Is raft enabled?
	bool 		has_raft() 					const { return m_has_raft; }
	// Has any support?
	bool 		has_support()				const { return m_object_config->support_material.value; }

	// How many raft layers are there below the 1st object layer?
	// The 1st object layer_id will be offsetted by this number.
	size_t 		num_raft_layers() 			const { return m_object_config->raft_layers.value; }
	// num_raft_layers() == num_raft_base_layers() + num_raft_interface_layers() + num_raft_contact_layers().
	size_t 		num_raft_base_layers() 		const { return m_num_base_raft_layers; }
	size_t 		num_raft_interface_layers() const { return m_num_interface_raft_layers; }
	size_t 		num_raft_contact_layers() 	const { return m_num_contact_raft_layers; }

	coordf_t 	raft_height() 			    const { return m_raft_height; }
	coordf_t    raft_base_height() 			const { return m_raft_base_height; }
	coordf_t	raft_interface_height() 	const { return m_raft_interface_height; }
	coordf_t	raft_contact_height() 		const { return m_raft_contact_height; }
	bool 		raft_bridging() 			const { return m_raft_contact_layer_bridging; }

	// 1st layer of the object will be printed depeding on the raft settings.
	coordf_t 	first_object_layer_print_z() 	const { return m_object_1st_layer_print_z; }
	coordf_t 	first_object_layer_height() 	const { return m_object_1st_layer_height; }
	coordf_t 	first_object_layer_gap() 		const { return m_object_1st_layer_gap; }
	bool 		first_object_layer_bridging() 	const { return m_object_1st_layer_bridging; }

	// Generate support material for the object.
	// New support layers will be added to the object,
	// with extrusion paths and islands filled in for each support layer.
	void 		generate(PrintObject &object);

private:
	// Generate top contact layers supporting overhangs.
	// For a soluble interface material synchronize the layer heights with the object, otherwise leave the layer height undefined.
	// If supports over bed surface only are requested, don't generate contact layers over an object.
	MyLayersPtr top_contact_layers(const PrintObject &object, MyLayerStorage &layer_storage) const;

	// Generate bottom contact layers supporting the top contact layers.
	// For a soluble interface material synchronize the layer heights with the object, 
	// otherwise set the layer height to a bridging flow of a support interface nozzle.
	MyLayersPtr bottom_contact_layers_and_layer_support_areas(
		const PrintObject &object, const MyLayersPtr &top_contacts, MyLayerStorage &layer_storage,
		std::vector<Polygons> &layer_support_areas) const;

	// Trim the top_contacts layers with the bottom_contacts layers if they overlap, so there would not be enough vertical space for both of them.
	void trim_top_contacts_by_bottom_contacts(const PrintObject &object, const MyLayersPtr &bottom_contacts, MyLayersPtr &top_contacts) const;

	// Generate raft layers and the intermediate support layers between the bottom contact and top contact surfaces.
	MyLayersPtr raft_and_intermediate_support_layers(
	    const PrintObject   &object,
	    const MyLayersPtr   &bottom_contacts,
	    const MyLayersPtr   &top_contacts,
	    MyLayerStorage	 	&layer_storage,
	    const coordf_t       max_object_layer_height) const;

	void generate_base_layers(
	    const PrintObject   &object,
	    const MyLayersPtr   &bottom_contacts,
	    const MyLayersPtr   &top_contacts,
	    MyLayersPtr         &intermediate_layers,
	    std::vector<Polygons> &layer_support_areas) const;

    Polygons generate_raft_base(
	    const PrintObject   &object,
	    const MyLayersPtr   &bottom_contacts,
	    MyLayersPtr         &intermediate_layers) const;

	MyLayersPtr generate_interface_layers(
	    const PrintObject   &object,
	    const MyLayersPtr   &bottom_contacts,
	    const MyLayersPtr   &top_contacts,
	    MyLayersPtr         &intermediate_layers,
	    MyLayerStorage      &layer_storage) const;

/*
	void generate_pillars_shape();
	void clip_with_shape();
*/

	// Produce the actual G-code.
	void generate_toolpaths(
        const PrintObject   &object,
        const Polygons 		&raft,
        const MyLayersPtr   &bottom_contacts,
        const MyLayersPtr   &top_contacts,
        const MyLayersPtr   &intermediate_layers,
        const MyLayersPtr   &interface_layers) const;

	const PrintObject 		*m_object;
	const PrintConfig 		*m_print_config;
	const PrintObjectConfig *m_object_config;

	Flow 			 	 m_first_layer_flow;
	Flow 			 	 m_support_material_flow;
	Flow 			 	 m_support_material_interface_flow;
	bool 			 	 m_soluble_interface;

	Flow 				 m_support_material_raft_base_flow;
	Flow 				 m_support_material_raft_interface_flow;
	Flow 				 m_support_material_raft_contact_flow;

	bool 				 m_has_raft;
	size_t 				 m_num_base_raft_layers;
	size_t 				 m_num_interface_raft_layers;
	size_t 				 m_num_contact_raft_layers;
	// If set, the raft contact layer is laid with round strings, which are easily detachable
	// from both the below and above layes.
	// Otherwise a normal flow is used and the strings are squashed against the layer below, 
	// creating a firm bond with the layer below and making the interface top surface flat.
	coordf_t 			 m_raft_height;
	coordf_t 			 m_raft_base_height;
	coordf_t			 m_raft_interface_height;
	coordf_t			 m_raft_contact_height;
	bool 				 m_raft_contact_layer_bridging;

	coordf_t 			 m_object_1st_layer_print_z;
	coordf_t			 m_object_1st_layer_height;
	coordf_t 			 m_object_1st_layer_gap;
	bool 				 m_object_1st_layer_bridging;

    coordf_t 			 m_object_layer_height_max;
    coordf_t 			 m_support_layer_height_min;
	coordf_t		 	 m_support_layer_height_max;
	coordf_t		 	 m_support_interface_layer_height_max;

	coordf_t  			 m_gap_extra_above;
	coordf_t 			 m_gap_extra_below;
	coordf_t 			 m_gap_xy;

	// If enabled, the support layers will be synchronized with object layers.
	// This does not prevent the support layers to be combined.
	bool 				 m_synchronize_support_layers_with_object;
	// If disabled and m_synchronize_support_layers_with_object,
	// the support layers will be synchronized with the object layers exactly, no layer will be combined.
	bool 				 m_combine_support_layers;
};

} // namespace Slic3r

#endif /* slic3r_SupportMaterial_hpp_ */
