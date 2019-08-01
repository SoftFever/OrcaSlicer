#ifndef WipeTower_
#define WipeTower_

#include <cmath>
#include <string>
#include <sstream>
#include <utility>
#include <algorithm>

#include "libslic3r/PrintConfig.hpp"


namespace Slic3r
{

class WipeTowerWriter;



class WipeTower
{
public:
    struct Extrusion
	{
		Extrusion(const Vec2f &pos, float width, unsigned int tool) : pos(pos), width(width), tool(tool) {}
		// End position of this extrusion.
		Vec2f				pos;
		// Width of a squished extrusion, corrected for the roundings of the squished extrusions.
		// This is left zero if it is a travel move.
		float 			width;
		// Current extruder index.
		unsigned int    tool;
	};

	struct ToolChangeResult
	{
		// Print heigh of this tool change.
		float					print_z;
		float 					layer_height;
		// G-code section to be directly included into the output G-code.
		std::string				gcode;
		// For path preview.
		std::vector<Extrusion> 	extrusions;
		// Initial position, at which the wipe tower starts its action.
		// At this position the extruder is loaded and there is no Z-hop applied.
		Vec2f						start_pos;
		// Last point, at which the normal G-code generator of Slic3r shall continue.
		// At this position the extruder is loaded and there is no Z-hop applied.
		Vec2f						end_pos;
		// Time elapsed over this tool change.
		// This is useful not only for the print time estimation, but also for the control of layer cooling.
		float  				    elapsed_time;

        // Is this a priming extrusion? (If so, the wipe tower rotation & translation will not be applied later)
        bool                    priming;

        // Initial tool
        int initial_tool;

        // New tool
        int new_tool;

		// Sum the total length of the extrusion.
		float total_extrusion_length_in_plane() {
			float e_length = 0.f;
			for (size_t i = 1; i < this->extrusions.size(); ++ i) {
				const Extrusion &e = this->extrusions[i];
				if (e.width > 0) {
					Vec2f v = e.pos - (&e - 1)->pos;
					e_length += v.norm();
				}
			}
			return e_length;
		}
	};

	// x			-- x coordinates of wipe tower in mm ( left bottom corner )
	// y			-- y coordinates of wipe tower in mm ( left bottom corner )
	// width		-- width of wipe tower in mm ( default 60 mm - leave as it is )
	// wipe_area	-- space available for one toolchange in mm
	WipeTower(bool semm, float x, float y, float width, float rotation_angle, float cooling_tube_retraction,
              float cooling_tube_length, float parking_pos_retraction, float extra_loading_move, 
              float bridging, bool set_extruder_trimpot, GCodeFlavor flavor,
              const std::vector<std::vector<float>>& wiping_matrix, unsigned int initial_tool) :
        m_semm(semm),
        m_wipe_tower_pos(x, y),
		m_wipe_tower_width(width),
		m_wipe_tower_rotation_angle(rotation_angle),
		m_y_shift(0.f),
		m_z_pos(0.f),
		m_is_first_layer(false),
        m_gcode_flavor(flavor),
        m_bridging(bridging),
        m_current_tool(initial_tool),
        wipe_volumes(wiping_matrix)
        {
            // If this is a single extruder MM printer, we will use all the SE-specific config values.
            // Otherwise, the defaults will be used to turn off the SE stuff.
            if (m_semm) {
                m_cooling_tube_retraction = cooling_tube_retraction;
                m_cooling_tube_length = cooling_tube_length;
                m_parking_pos_retraction = parking_pos_retraction;
                m_extra_loading_move = extra_loading_move;
                m_set_extruder_trimpot = set_extruder_trimpot;
            }
        }

	virtual ~WipeTower() {}


	// Set the extruder properties.
	void set_extruder(size_t idx, std::string material, int temp, int first_layer_temp, float loading_speed, float loading_speed_start,
                      float unloading_speed, float unloading_speed_start, float delay, int cooling_moves,
                      float cooling_initial_speed, float cooling_final_speed, std::string ramming_parameters, float max_volumetric_speed,
                      float nozzle_diameter, float filament_diameter)
	{
        //while (m_filpar.size() < idx+1)   // makes sure the required element is in the vector
        m_filpar.push_back(FilamentParameters());

        m_filpar[idx].material = material;
        m_filpar[idx].temperature = temp;
        m_filpar[idx].first_layer_temperature = first_layer_temp;

        // If this is a single extruder MM printer, we will use all the SE-specific config values.
        // Otherwise, the defaults will be used to turn off the SE stuff.
        if (m_semm) {
            m_filpar[idx].loading_speed           = loading_speed;
            m_filpar[idx].loading_speed_start     = loading_speed_start;
            m_filpar[idx].unloading_speed         = unloading_speed;
            m_filpar[idx].unloading_speed_start   = unloading_speed_start;
            m_filpar[idx].delay                   = delay;
            m_filpar[idx].cooling_moves           = cooling_moves;
            m_filpar[idx].cooling_initial_speed   = cooling_initial_speed;
            m_filpar[idx].cooling_final_speed     = cooling_final_speed;
        }

        m_filpar[idx].filament_area = float((M_PI/4.f) * pow(filament_diameter, 2)); // all extruders are assumed to have the same filament diameter at this point
        m_filpar[idx].nozzle_diameter = nozzle_diameter; // to be used in future with (non-single) multiextruder MM

        if (max_volumetric_speed != 0.f)
            m_filpar[idx].max_e_speed = (max_volumetric_speed / filament_area());

        m_perimeter_width = nozzle_diameter * Width_To_Nozzle_Ratio; // all extruders are now assumed to have the same diameter

        if (m_semm) {
            std::stringstream stream{ramming_parameters};
            float speed = 0.f;
            stream >> m_filpar[idx].ramming_line_width_multiplicator >> m_filpar[idx].ramming_step_multiplicator;
            m_filpar[idx].ramming_line_width_multiplicator /= 100;
            m_filpar[idx].ramming_step_multiplicator /= 100;
            while (stream >> speed)
                m_filpar[idx].ramming_speed.push_back(speed);
        }

        m_used_filament_length.resize(std::max(m_used_filament_length.size(), idx + 1)); // makes sure that the vector is big enough so we don't have to check later
	}


	// Appends into internal structure m_plan containing info about the future wipe tower
	// to be used before building begins. The entries must be added ordered in z.
	void plan_toolchange(float z_par, float layer_height_par, unsigned int old_tool, unsigned int new_tool, bool brim, float wipe_volume = 0.f);

	// Iterates through prepared m_plan, generates ToolChangeResults and appends them to "result"
	void generate(std::vector<std::vector<ToolChangeResult>> &result);

    float get_depth() const { return m_wipe_tower_depth; }



	// Switch to a next layer.
	void set_layer(
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
		m_depth_traversed  = 0.f;
		m_current_shape = (! is_first_layer && m_current_shape == SHAPE_NORMAL) ? SHAPE_REVERSED : SHAPE_NORMAL;
		if (is_first_layer) {
            this->m_num_layer_changes 	= 0;
            this->m_num_tool_changes 	= 0;
        }
        else
            ++ m_num_layer_changes;
		
		// Calculate extrusion flow from desired line width, nozzle diameter, filament diameter and layer_height:
		m_extrusion_flow = extrusion_flow(layer_height);

        // Advance m_layer_info iterator, making sure we got it right
		while (!m_plan.empty() && m_layer_info->z < print_z - WT_EPSILON && m_layer_info+1 != m_plan.end())
			++m_layer_info;
	}

	// Return the wipe tower position.
	const Vec2f& 		 position() const { return m_wipe_tower_pos; }
	// Return the wipe tower width.
	float     		 width()    const { return m_wipe_tower_width; }
	// The wipe tower is finished, there should be no more tool changes or wipe tower prints.
	bool 	  		 finished() const { return m_max_color_changes == 0; }

	// Returns gcode to prime the nozzles at the front edge of the print bed.
	std::vector<ToolChangeResult> prime(
		// print_z of the first layer.
		float 						first_layer_height, 
		// Extruder indices, in the order to be primed. The last extruder will later print the wipe tower brim, print brim and the object.
		const std::vector<unsigned int> &tools,
		// If true, the last priming are will be the same as the other priming areas, and the rest of the wipe will be performed inside the wipe tower.
		// If false, the last priming are will be large enough to wipe the last extruder sufficiently.
		bool 						last_wipe_inside_wipe_tower);

	// Returns gcode for a toolchange and a final print head position.
	// On the first layer, extrude a brim around the future wipe tower first.
	ToolChangeResult tool_change(unsigned int new_tool, bool last_in_layer);

	// Fill the unfilled space with a sparse infill.
	// Call this method only if layer_finished() is false.
	ToolChangeResult finish_layer();

	// Is the current layer finished?
	bool 			 layer_finished() const {
		return ( (m_is_first_layer ? m_wipe_tower_depth - m_perimeter_width : m_layer_info->depth) - WT_EPSILON < m_depth_traversed);
	}

    std::vector<float> get_used_filament() const { return m_used_filament_length; }
    int get_number_of_toolchanges() const { return m_num_tool_changes; }

    struct FilamentParameters {
        std::string 	    material = "PLA";
        int  			    temperature = 0;
        int  			    first_layer_temperature = 0;
        float               loading_speed = 0.f;
        float               loading_speed_start = 0.f;
        float               unloading_speed = 0.f;
        float               unloading_speed_start = 0.f;
        float               delay = 0.f ;
        int                 cooling_moves = 0;
        float               cooling_initial_speed = 0.f;
        float               cooling_final_speed = 0.f;
        float               ramming_line_width_multiplicator = 1.f;
        float               ramming_step_multiplicator = 1.f;
        float               max_e_speed = std::numeric_limits<float>::max();
        std::vector<float>  ramming_speed;
        float               nozzle_diameter;
        float               filament_area;
    };

private:
	WipeTower();

	enum wipe_shape // A fill-in direction
	{
		SHAPE_NORMAL = 1,
		SHAPE_REVERSED = -1
	};


    const bool  m_peters_wipe_tower   = false; // sparse wipe tower inspired by Peter's post processor - not finished yet
    const float Width_To_Nozzle_Ratio = 1.25f; // desired line width (oval) in multiples of nozzle diameter - may not be actually neccessary to adjust
    const float WT_EPSILON            = 1e-3f;
    const float filament_area() const {
        return m_filpar[0].filament_area; // all extruders are assumed to have the same filament diameter at this point
    }


	bool   m_semm               = true; // Are we using a single extruder multimaterial printer?
    Vec2f  m_wipe_tower_pos; 			// Left front corner of the wipe tower in mm.
	float  m_wipe_tower_width; 			// Width of the wipe tower.
	float  m_wipe_tower_depth 	= 0.f; 	// Depth of the wipe tower
	float  m_wipe_tower_rotation_angle = 0.f; // Wipe tower rotation angle in degrees (with respect to x axis)
    float  m_internal_rotation  = 0.f;
	float  m_y_shift			= 0.f;  // y shift passed to writer
	float  m_z_pos 				= 0.f;  // Current Z position.
	float  m_layer_height 		= 0.f; 	// Current layer height.
	size_t m_max_color_changes 	= 0; 	// Maximum number of color changes per layer.
	bool   m_is_first_layer 	= false;// Is this the 1st layer of the print? If so, print the brim around the waste tower.
    int    m_old_temperature    = -1;   // To keep track of what was the last temp that we set (so we don't issue the command when not neccessary)

	// G-code generator parameters.
    float           m_cooling_tube_retraction   = 0.f;
    float           m_cooling_tube_length       = 0.f;
    float           m_parking_pos_retraction    = 0.f;
    float           m_extra_loading_move        = 0.f;
    float           m_bridging                  = 0.f;
    bool            m_set_extruder_trimpot      = false;
    bool            m_adhesion                  = true;
    GCodeFlavor     m_gcode_flavor;

	float m_perimeter_width = 0.4f * Width_To_Nozzle_Ratio; // Width of an extrusion line, also a perimeter spacing for 100% infill.
	float m_extrusion_flow = 0.038f; //0.029f;// Extrusion flow is derived from m_perimeter_width, layer height and filament diameter.

	// Extruder specific parameters.
    std::vector<FilamentParameters> m_filpar;


	// State of the wipe tower generator.
	unsigned int m_num_layer_changes = 0; // Layer change counter for the output statistics.
	unsigned int m_num_tool_changes  = 0; // Tool change change counter for the output statistics.
	///unsigned int 	m_idx_tool_change_in_layer = 0; // Layer change counter in this layer. Counting up to m_max_color_changes.
	bool m_print_brim = true;
	// A fill-in direction (positive Y, negative Y) alternates with each layer.
	wipe_shape   	m_current_shape = SHAPE_NORMAL;
	unsigned int 	m_current_tool  = 0;
    const std::vector<std::vector<float>> wipe_volumes;

	float           m_depth_traversed = 0.f; // Current y position at the wipe tower.
	bool 			m_left_to_right   = true;
	float			m_extra_spacing   = 1.f;

	// Calculates extrusion flow needed to produce required line width for given layer height
	float extrusion_flow(float layer_height = -1.f) const	// negative layer_height - return current m_extrusion_flow
	{
		if ( layer_height < 0 )
			return m_extrusion_flow;
		return layer_height * ( m_perimeter_width - layer_height * (1.f-float(M_PI)/4.f)) / filament_area();
	}

	// Calculates length of extrusion line to extrude given volume
	float volume_to_length(float volume, float line_width, float layer_height) const {
		return std::max(0.f, volume / (layer_height * (line_width - layer_height * (1.f - float(M_PI) / 4.f))));
	}

	// Calculates depth for all layers and propagates them downwards
	void plan_tower();

	// Goes through m_plan and recalculates depths and width of the WT to make it exactly square - experimental
	void make_wipe_tower_square();

    // Goes through m_plan, calculates border and finish_layer extrusions and subtracts them from last wipe
    void save_on_last_wipe();


	struct box_coordinates
	{
		box_coordinates(float left, float bottom, float width, float height) :
			ld(left        , bottom         ),
			lu(left        , bottom + height),
			rd(left + width, bottom         ),
			ru(left + width, bottom + height) {}
		box_coordinates(const Vec2f &pos, float width, float height) : box_coordinates(pos(0), pos(1), width, height) {}
		void translate(const Vec2f &shift) {
			ld += shift; lu += shift;
			rd += shift; ru += shift;
		}
		void translate(const float dx, const float dy) { translate(Vec2f(dx, dy)); }
		void expand(const float offset) {
			ld += Vec2f(- offset, - offset);
			lu += Vec2f(- offset,   offset);
			rd += Vec2f(  offset, - offset);
			ru += Vec2f(  offset,   offset);
		}
		void expand(const float offset_x, const float offset_y) {
			ld += Vec2f(- offset_x, - offset_y);
			lu += Vec2f(- offset_x,   offset_y);
			rd += Vec2f(  offset_x, - offset_y);
			ru += Vec2f(  offset_x,   offset_y);
		}
		Vec2f ld;  // left down
		Vec2f lu;	// left upper 
		Vec2f rd;	// right lower
		Vec2f ru;  // right upper
	};


	// to store information about tool changes for a given layer
	struct WipeTowerInfo{
		struct ToolChange {
			unsigned int old_tool;
			unsigned int new_tool;
			float required_depth;
            float ramming_depth;
            float first_wipe_line;
            float wipe_volume;
			ToolChange(unsigned int old, unsigned int newtool, float depth=0.f, float ramming_depth=0.f, float fwl=0.f, float wv=0.f)
            : old_tool{old}, new_tool{newtool}, required_depth{depth}, ramming_depth{ramming_depth}, first_wipe_line{fwl}, wipe_volume{wv} {}
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

    // Stores information about used filament length per extruder:
    std::vector<float> m_used_filament_length;


	// Returns gcode for wipe tower brim
	// sideOnly			-- set to false -- experimental, draw brim on sides of wipe tower
	// offset			-- set to 0		-- experimental, offset to replace brim in front / rear of wipe tower
	ToolChangeResult toolchange_Brim(bool sideOnly = false, float y_offset = 0.f);

	void toolchange_Unload(
		WipeTowerWriter &writer,
		const box_coordinates  &cleaning_box, 
		const std::string&	 	current_material,
		const int 				new_temperature);

	void toolchange_Change(
		WipeTowerWriter &writer,
		const unsigned int		new_tool,
		const std::string& 		new_material);
	
	void toolchange_Load(
		WipeTowerWriter &writer,
		const box_coordinates  &cleaning_box);
	
	void toolchange_Wipe(
		WipeTowerWriter &writer,
		const box_coordinates  &cleaning_box,
		float wipe_volume);
};




}; // namespace Slic3r

#endif // WipeTowerPrusaMM_hpp_ 
