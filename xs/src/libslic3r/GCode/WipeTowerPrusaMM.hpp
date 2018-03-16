#ifndef WipeTowerPrusaMM_hpp_
#define WipeTowerPrusaMM_hpp_

#include <cmath>
#include <string>
#include <sstream>
#include <utility>

#include "WipeTower.hpp"

// Following is used to calculate extrusion flow - should be taken from config in future
const float Filament_Area = M_PI * 1.75f * 1.75f / 4.f; // filament area in mm^3
const float Nozzle_Diameter = 0.4f;	// nozzle diameter in mm
// desired line width (oval) in multiples of nozzle diameter - may not be actually neccessary to adjust
const float Width_To_Nozzle_Ratio = 1.25f;

// m_perimeter_width was hardcoded until now as 0.5 (for 0.4 nozzle and 0.2 layer height)
// FIXME m_perimeter_width is used in plan_toolchange - take care of proper initialization value when changing to variable
const float Konst = 1.f;
const float m_perimeter_width = Nozzle_Diameter * Width_To_Nozzle_Ratio * Konst;

const float WT_EPSILON = 1e-3f;


namespace Slic3r
{

namespace PrusaMultiMaterial {
	class Writer;
};



// Operator overload to output std::pairs
template <typename T>
std::ostream& operator<<(std::ostream& stream,const std::pair<T,T>& pair) {
    return stream << pair.first << " " << pair.second;
}

// Operator overload to output elements of a vector to std::ofstream easily:
template <typename T>
std::ostream& operator<<(std::ostream& stream,const std::vector<T>& vect) {
    for (const auto& element : vect)
        stream << element << " ";
    return stream;
}

// Operator overload to input elements of a vector from std::ifstream easily (reads until a fail)
template <typename T>
std::istream& operator>>(std::istream& stream, std::vector<T>& vect) {
    vect.clear();
    T value{};
    bool we_read_something = false;
    while (stream >> value) {
        vect.push_back(value);
        we_read_something = true;
    }
    if (!stream.eof() && we_read_something) { // if this is not eof, we might be at separator - let's get rid of it
        stream.clear();     // if we failed on very first line or reached eof, return stream in good() state
        stream.get();       // get() whatever we are stuck at
    }
    return stream;
}


// This struct is used to store parameters and to pass it to wipe tower generator
struct WipeTowerParameters {
    WipeTowerParameters() {  }           // create new empty object
    WipeTowerParameters(const std::string& init_data) { // create object and initialize from std::string
        set_defaults();
    }
    
    void set_defaults() {
        sampling = 0.25f;
        wipe_volumes = {{  0.f, 60.f, 60.f, 60.f},
                        { 60.f,  0.f, 60.f, 60.f},
                        { 60.f, 60.f,  0.f, 60.f},
                        { 60.f, 60.f, 60.f,  0.f}};
        filament_wipe_volumes = {{30.f,30.f},{30.f,30.f},{30.f,30.f},{30.f,30.f}};
    }
    
    float sampling = 0.25f; // this does not quite work yet, keep it fixed to 0.25f
    std::vector<std::vector<float>> wipe_volumes;
    std::vector<std::pair<int,int>> filament_wipe_volumes;
};


class WipeTowerPrusaMM : public WipeTower
{
public:
	enum material_type
	{
		INVALID = -1,
		PLA   = 0,		// E:210C	B:55C
		ABS   = 1,		// E:255C	B:100C
		PET   = 2,		// E:240C	B:90C
		HIPS  = 3,		// E:220C	B:100C
		FLEX  = 4,		// E:245C	B:80C
		SCAFF = 5,		// E:215C	B:55C
		EDGE  = 6,		// E:240C	B:80C
		NGEN  = 7,		// E:230C	B:80C
		PVA   = 8	    // E:210C	B:80C
	};

	// Parse material name into material_type.
	static material_type parse_material(const char *name);

	// x			-- x coordinates of wipe tower in mm ( left bottom corner )
	// y			-- y coordinates of wipe tower in mm ( left bottom corner )
	// width		-- width of wipe tower in mm ( default 60 mm - leave as it is )
	// wipe_area	-- space available for one toolchange in mm
	WipeTowerPrusaMM(float x, float y, float width, float wipe_area, float rotation_angle, float cooling_tube_retraction,
                     float cooling_tube_length, float parking_pos_retraction, float bridging, bool adhesion, const std::string& parameters,
                     unsigned int initial_tool) :
		m_wipe_tower_pos(x, y),
		m_wipe_tower_width(width),
		m_wipe_tower_rotation_angle(rotation_angle),
		m_y_shift(0.f),
		m_z_pos(0.f),
		m_is_first_layer(false),
		m_is_last_layer(false),
        m_cooling_tube_retraction(cooling_tube_retraction),
        m_cooling_tube_length(cooling_tube_length),
        m_parking_pos_retraction(parking_pos_retraction),
		m_current_tool(initial_tool),
        m_par(parameters) 
 	{
        m_bridging = bridging;
        m_adhesion = adhesion;
        
		for (size_t i = 0; i < 4; ++ i) {
			// Extruder specific parameters.
			m_filpar[i].material = PLA;
			m_filpar[i].temperature = 0;
			m_filpar[i].first_layer_temperature = 0;
		}
	}

	virtual ~WipeTowerPrusaMM() {}

	// _retract - retract value in mm
	void set_retract(float retract) { m_retract = retract; }
	
	// _zHop - z hop value in mm
	void set_zhop(float zhop) { m_zhop = zhop; }


	// Set the extruder properties.
	void set_extruder(size_t idx, material_type material, int temp, int first_layer_temp, float loading_speed,
                      float unloading_speed, float delay, int cooling_time, std::string ramming_parameters)
	{
        m_filpar[idx].material = material;
        m_filpar[idx].temperature = temp;
        m_filpar[idx].first_layer_temperature = first_layer_temp;
        m_filpar[idx].loading_speed = loading_speed;
        m_filpar[idx].unloading_speed = unloading_speed;
        m_filpar[idx].delay = delay;
        m_filpar[idx].cooling_time = cooling_time;
        
        std::stringstream stream{ramming_parameters};
        float speed = 0.f;
        stream >> m_filpar[idx].ramming_line_width_multiplicator >> m_filpar[idx].ramming_step_multiplicator;
        m_filpar[idx].ramming_line_width_multiplicator /= 100;
        m_filpar[idx].ramming_step_multiplicator /= 100;
        while (stream >> speed)
            m_filpar[idx].ramming_speed.push_back(speed);
	}


	// Setter for internal structure m_plan containing info about the future wipe tower
	// to be used before building begins. The entries must be added ordered in z.
	void plan_toolchange(float z_par, float layer_height_par, unsigned int old_tool, unsigned int new_tool, bool brim);

	// Iterates through prepared m_plan, generates ToolChangeResults and appends them to "result"
	void generate(std::vector<std::vector<WipeTower::ToolChangeResult>> &result);

	// Calculates depth for all layers and propagates them downwards
	void plan_tower();

	// Goes through m_plan and recalculates depths and width of the WT to make it exactly square - experimental
	void make_wipe_tower_square();

    // Goes through m_plan, calculates border and finish_layer extrusions and subtracts them from last wipe
    void save_on_last_wipe();

	// Switch to a next layer.
	virtual void set_layer(
		// Print height of this layer.
		float print_z,
		// Layer height, used to calculate extrusion the rate.
		float layer_height,
		// Maximum number of tool changes on this layer or the layers below.
		size_t max_tool_changes,
		// Is this the first layer of the print? In that case print the brim first.
		bool is_first_layer,
		// Is this the last layer of the waste tower?
		bool is_last_layer)
	{
		m_z_pos 				= print_z;
		m_layer_height			= layer_height;
		m_is_first_layer 		= is_first_layer;
		m_print_brim = is_first_layer;
		m_depth_traversed  = 0.f; // to make room for perimeter line
		m_current_shape = (! is_first_layer && m_current_shape == SHAPE_NORMAL) ? SHAPE_REVERSED : SHAPE_NORMAL;
		
		++ m_num_layer_changes;
		
		// Calculates extrusion flow from desired line width, nozzle diameter, filament diameter and layer_height
		m_extrusion_flow = extrusion_flow(layer_height);

		while (!m_plan.empty() && m_layer_info->z < print_z - WT_EPSILON && m_layer_info+1!=m_plan.end())
			++m_layer_info;
	}

	// Return the wipe tower position.
	virtual const xy& 		 position() const { return m_wipe_tower_pos; }
	// Return the wipe tower width.
	virtual float     		 width()    const { return m_wipe_tower_width; }
	// The wipe tower is finished, there should be no more tool changes or wipe tower prints.
	virtual bool 	  		 finished() const { return m_max_color_changes == 0; }

	// Returns gcode to prime the nozzles at the front edge of the print bed.
	virtual ToolChangeResult prime(
		// print_z of the first layer.
		float 						first_layer_height, 
		// Extruder indices, in the order to be primed. The last extruder will later print the wipe tower brim, print brim and the object.
		const std::vector<unsigned int> &tools,
		// If true, the last priming are will be the same as the other priming areas, and the rest of the wipe will be performed inside the wipe tower.
		// If false, the last priming are will be large enough to wipe the last extruder sufficiently.
		bool 						last_wipe_inside_wipe_tower);

	// Returns gcode for a toolchange and a final print head position.
	// On the first layer, extrude a brim around the future wipe tower first.
	virtual ToolChangeResult tool_change(unsigned int new_tool, bool last_in_layer);

	// Fill the unfilled space with a zig-zag.
	// Call this method only if layer_finished() is false.
	virtual ToolChangeResult finish_layer();

	// Is the current layer finished?
	virtual bool 			 layer_finished() const {
		return ( (m_is_first_layer ? m_wipe_tower_depth - m_perimeter_width : m_layer_info->depth) - WT_EPSILON < m_depth_traversed);
	}


private:
	WipeTowerPrusaMM();

	enum wipe_shape // A fill-in direction (positive Y, negative Y) alternates with each layer.
	{
		SHAPE_NORMAL = 1,
		SHAPE_REVERSED = -1
	};

	xy 	   m_wipe_tower_pos; 			// Left front corner of the wipe tower in mm.
	float  m_wipe_tower_width; 			// Width of the wipe tower.
	float  m_wipe_tower_depth 	= 0.f; 	// Depth of the wipe tower
	float  m_wipe_tower_rotation_angle = 0.f; // Wipe tower rotation angle in degrees (with respect to x axis
	float  m_y_shift			= 0.f;  // y shift passed to writer
	float  m_z_pos 				= 0.f;  // Current Z position.
	float  m_layer_height 		= 0.f; 	// Current layer height.
	size_t m_max_color_changes 	= 0; 	// Maximum number of color changes per layer.
	bool   m_is_first_layer 	= false;// Is this the 1st layer of the print? If so, print the brim around the waste tower.
	bool   m_is_last_layer 		= false;// Is this the last layer of this waste tower?
    bool   m_layer_parity       = false;

	// G-code generator parameters.
	float  			m_zhop 			 = 0.5f;
	float  			m_retract		 = 4.f;
    float           m_cooling_tube_retraction   = 0.f;
    float           m_cooling_tube_length       = 0.f;
    float           m_parking_pos_retraction    = 0.f;
    float           m_bridging                  = 0.f;
    bool            m_adhesion                  = true;

	float m_line_width = Nozzle_Diameter * Width_To_Nozzle_Ratio; // Width of an extrusion line, also a perimeter spacing for 100% infill.
	float m_extrusion_flow = 0.038; //0.029f;// Extrusion flow is derived from m_perimeter_width, layer height and filament diameter.


    struct FilamentParameters {
        material_type 	    material;
        int  			    temperature;
        int  			    first_layer_temperature;
        float               loading_speed;
        float               unloading_speed;
        float               delay;
        int                 cooling_time;
        float               ramming_line_width_multiplicator;
        float               ramming_step_multiplicator;
        std::vector<float>  ramming_speed;
    };

	// Extruder specific parameters.
    FilamentParameters m_filpar[4];


	// State of the wiper tower generator.
	unsigned int m_num_layer_changes = 0; // Layer change counter for the output statistics.
	unsigned int m_num_tool_changes  = 0; // Tool change change counter for the output statistics.
	///unsigned int 	m_idx_tool_change_in_layer = 0; // Layer change counter in this layer. Counting up to m_max_color_changes.
	bool m_print_brim = true;
	// A fill-in direction (positive Y, negative Y) alternates with each layer.
	wipe_shape   	m_current_shape = SHAPE_NORMAL;
	unsigned int 	m_current_tool  = 0;
    WipeTowerParameters m_par;

	float m_depth_traversed = 0.f; // Current y position at the wipe tower.
	// How much to wipe the 1st extruder over the wipe tower at the 1st layer
	// after the wipe tower brim has been extruded?
	float  			m_initial_extra_wipe = 0.f;
	bool 			m_left_to_right = true;
	float			m_extra_spacing = 1.f;

	// Calculates extrusion flow needed to produce required line width for given layer height
	float extrusion_flow(float layer_height = -1.f) const	// negative layer_height - return current m_extrusion_flow
	{
		if ( layer_height < 0 )
			return m_extrusion_flow;
		return layer_height * ( Width_To_Nozzle_Ratio * Nozzle_Diameter - layer_height * (1-M_PI/4.f)) / (Filament_Area);
	}

	// Calculates length of extrusion line to extrude given volume
	float volume_to_length(float volume, float line_width, float layer_height) const {
		return volume / (layer_height * (line_width - layer_height * (1. - M_PI / 4.)));
	}


	struct box_coordinates
	{
		box_coordinates(float left, float bottom, float width, float height) :
			ld(left        , bottom         ),
			lu(left        , bottom + height),
			rd(left + width, bottom         ),
			ru(left + width, bottom + height) {}
		box_coordinates(const xy &pos, float width, float height) : box_coordinates(pos.x, pos.y, width, height) {}
		void translate(const xy &shift) {
			ld += shift; lu += shift;
			rd += shift; ru += shift;
		}
		void translate(const float dx, const float dy) { translate(xy(dx, dy)); }
		void expand(const float offset) {
			ld += xy(- offset, - offset);
			lu += xy(- offset,   offset);
			rd += xy(  offset, - offset);
			ru += xy(  offset,   offset);
		}
		void expand(const float offset_x, const float offset_y) {
			ld += xy(- offset_x, - offset_y);
			lu += xy(- offset_x,   offset_y);
			rd += xy(  offset_x, - offset_y);
			ru += xy(  offset_x,   offset_y);
		}
		xy ld;  // left down
		xy lu;	// left upper 
		xy rd;	// right lower
		xy ru;  // right upper
	};


	// to store information about tool changes for a given layer
	struct WipeTowerInfo{
		struct ToolChange {
			unsigned int old_tool;
			unsigned int new_tool;
			float required_depth;
            float ramming_depth;
            float first_wipe_line;
			ToolChange(unsigned int old,unsigned int newtool,float depth=0.f,float ramming_depth=0.f,float fwl=0.f)
            : old_tool{old}, new_tool{newtool}, required_depth{depth}, ramming_depth{ramming_depth},first_wipe_line{fwl} {}
		};
		float z;		// z position of the layer
		float height;	// layer height
		float depth;	// depth of the layer based on all layers above
		float extra_spacing;
		float toolchanges_depth() const { float sum = 0.f; for (const auto &a : tool_changes) sum += a.required_depth; return sum; }

		std::vector<ToolChange> tool_changes;

		WipeTowerInfo(float z_par, float layer_height_par)
			: z{z_par}, height{layer_height_par}, depth{0}, extra_spacing{1.f} {}
	};

	std::vector<WipeTowerInfo> m_plan; 	// Stores information about all layers and toolchanges for the future wipe tower (filled by plan_toolchange(...))
	std::vector<WipeTowerInfo>::iterator m_layer_info = m_plan.end();


	// Returns gcode for wipe tower brim
	// sideOnly			-- set to false -- experimental, draw brim on sides of wipe tower
	// offset			-- set to 0		-- experimental, offset to replace brim in front / rear of wipe tower
	ToolChangeResult toolchange_Brim(bool sideOnly = false, float y_offset = 0.f);

	void toolchange_Unload(
		PrusaMultiMaterial::Writer &writer,
		const box_coordinates  &cleaning_box, 
		const material_type	 	current_material,
		const int 				new_temperature);

	void toolchange_Change(
		PrusaMultiMaterial::Writer &writer,
		const unsigned int		new_tool,
		material_type 			new_material);
	
	void toolchange_Load(
		PrusaMultiMaterial::Writer &writer,
		const box_coordinates  &cleaning_box);
	
	void toolchange_Wipe(
		PrusaMultiMaterial::Writer &writer,
		const box_coordinates  &cleaning_box,
		float wipe_volume);
};




}; // namespace Slic3r

#endif /* WipeTowerPrusaMM_hpp_ */
