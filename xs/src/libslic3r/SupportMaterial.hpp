#ifndef slic3r_SupportMaterial_hpp_
#define slic3r_SupportMaterial_hpp_

namespace Slic3r {

// how much we extend support around the actual contact area
#define SUPPORT_MATERIAL_MARGIN 1.5	

// Instantiated by Slic3r::Print::Object->_support_material()
class PrintSupportMaterial
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
			bridging(false)
			{}

		~MyLayer() 
		{
			delete aux_polygons;
			aux_polygons = NULL;
		}

		bool operator==(const MyLayer &layer2) const {
			return print_z == layer2.printz && height == layer2.height && bridging == layer2.bridging;
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

		coordf_t	z() const { return is_top ? layer->print_z : layer->print_z - height; }

		bool operator<(const LayerExtreme &other) const { return z() < other.z(); }
	}

	struct LayerPrintZ_Hash {
		static size_t operator(const MyLayer &layer) { 
			return std::hash<double>(layer.print_z)^std::hash<double>(layer.height)^size_t(layer.bridging);
		}
	};

	typedef std::set<MyLayer, LayerPrintZ_Hash> MyLayersSet;
	typedef std::vector<Layer*> 				MyLayersPtr;
	typedef std::deque<Layer> 					MyLayersDeque;
	typedef std::deque<Layer> 					MyLayerStorage;

public:
	PrintSupportMaterial() :
		m_object(NULL),
		m_print_config(NULL),
		m_object_config(NULL),
		m_soluble_interface(false),
		m_support_layer_height_max(0.),
		m_support_interface_layer_height_max(0.)
	{}

	void setup(
		const PrintConfig 	*print_config;
		const ObjectConfig 	*object_config;
		Flow 			 	 flow;
		Flow 			 	 first_layer_flow;
		Flow 			 	 interface_flow;
		bool 			 	 soluble_interface)
	{
		this->m_object 				= object;
		this->m_print_config 		= print_config;
		this->m_object_config 		= object_config;
		this->m_flow 				= flow;
		this->m_first_layer_flow 	= first_layer_flow;
		this->m_interface_flow 		= interface_flow;
		this->m_soluble_interface 	= soluble_interface;
	}

	void generate(const PrintObject *object);

private:
	// Generate top contact layers supporting overhangs.
	// For a soluble interface material synchronize the layer heights with the object, otherwise leave the layer height undefined.
	// If supports over bed surface only are requested, don't generate contact layers over an object.
	MyLayersPtr top_contact_layers(const PrintObject &object, MyLayerStorage &layer_storage) const;

	// Generate bottom contact layers supporting the top contact layers.
	// For a soluble interface material synchronize the layer heights with the object, 
	// otherwise set the layer height to a bridging flow of a support interface nozzle.
	MyLayersPtr bottom_contact_layers(const PrintObject &object, const MyLayersPtr &top_contacts, MyLayerStorage &layer_storage) const;

	// Generate raft layers and the intermediate support layers between the bottom contact and top contact surfaces.
	MyLayersPtr raft_and_intermediate_support_layers(
	    const PrintObject   &object,
	    const MyLayersPtr   &bottom_contacts,
	    const MyLayersPtr   &top_contacts,
	    MyLayerStorage	 	&layer_storage,
	    const coordf_t       max_object_layer_height);

	void generate_base_layers(
	    const PrintObject   &object,
	    const MyLayersPtr   &bottom_contacts,
	    const MyLayersPtr   &top_contacts,
	    MyLayersPtr         &intermediate_layers);

	MyLayersPtr generate_interface_layers(
	    const PrintObject   &object,
	    const MyLayersPtr   &bottom_contacts,
	    const MyLayersPtr   &top_contacts,
	    MyLayersPtr         &intermediate_layers,
	    MyLayerStorage      &layer_storage);

/*
	void generate_pillars_shape();
	void clip_with_shape();
*/

	// Produce the actual G-code.
	void generate_toolpaths(
        const PrintObject   &object,
        const MyLayersPtr   &bottom_contacts,
        const MyLayersPtr   &top_contacts,
        const MyLayersPtr   &intermediate_layers,
        const MyLayersPtr   &interface_layers);

	const PrintConfig 	*m_print_config;
	const ObjectConfig 	*m_object_config;
	Flow 			 	 m_flow;
	Flow 			 	 m_first_layer_flow;
	Flow 			 	 m_interface_flow;
	bool 			 	 m_soluble_interface;

	coordf_t		 	 m_support_layer_height_max;
	coordf_t		 	 m_support_interface_layer_height_max;
};

#endif
