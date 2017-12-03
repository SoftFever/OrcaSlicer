#ifndef WipeTowerPrusaMM_hpp_
#define WipeTowerPrusaMM_hpp_

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

#include "WipeTower.hpp"

namespace Slic3r
{

namespace PrusaMultiMaterial {
	class Writer;
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
	WipeTowerPrusaMM(float x, float y, float width, float wipe_area, unsigned int initial_tool) :
		m_wipe_tower_pos(x, y),
		m_wipe_tower_width(width),
		m_wipe_area(wipe_area),
		m_z_pos(0.f),
		m_current_tool(initial_tool)
 	{
		for (size_t i = 0; i < 4; ++ i) {
			// Extruder specific parameters.
			m_material[i] = PLA;
			m_temperature[i] = 0;
			m_first_layer_temperature[i] = 0;
		}
	}

	virtual ~WipeTowerPrusaMM() {}

	// _retract - retract value in mm
	void set_retract(float retract) { m_retract = retract; }
	
	// _zHop - z hop value in mm
	void set_zhop(float zhop) { m_zhop = zhop; }

	// Set the extruder properties.
	void set_extruder(size_t idx, material_type material, int temp, int first_layer_temp)
	{
		m_material[idx] = material;
		m_temperature[idx] = temp;
		m_first_layer_temperature[idx] = first_layer_temp;
	}

	// Switch to a next layer.
	virtual void set_layer(
		// Print height of this layer.
		float  print_z,
		// Layer height, used to calculate extrusion the rate. 
		float  layer_height, 
		// Maximum number of tool changes on this layer or the layers below.
		size_t max_tool_changes, 
		// Is this the first layer of the print? In that case print the brim first.
		bool   is_first_layer,
		// Is this the last layer of the waste tower?
		bool   is_last_layer)
	{
		m_z_pos 				= print_z;
		m_layer_height			= layer_height;
		m_max_color_changes 	= max_tool_changes;
		m_is_first_layer 		= is_first_layer;
		m_is_last_layer			= is_last_layer;
		// Start counting the color changes from zero. Special case: -1 - extrude a brim first.
		m_idx_tool_change_in_layer = is_first_layer ? (unsigned int)(-1) : 0;
		m_current_wipe_start_y  = 0.f;
		m_current_shape = (! is_first_layer && m_current_shape == SHAPE_NORMAL) ? SHAPE_REVERSED : SHAPE_NORMAL;
		++ m_num_layer_changes;
		// Extrusion rate for an extrusion aka perimeter width 0.35mm.
		// Clamp the extrusion height to a 0.2mm layer height, independent of the nozzle diameter.
//		m_extrusion_flow = std::min(0.2f, layer_height) * 0.145f;
		// Use a strictly
		m_extrusion_flow = layer_height * 0.145f;
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
		bool 						last_wipe_inside_wipe_tower, 
		// May be used by a stand alone post processor.
		Purpose 					purpose = PURPOSE_MOVE_TO_TOWER_AND_EXTRUDE);

	// Returns gcode for a toolchange and a final print head position.
	// On the first layer, extrude a brim around the future wipe tower first.
	virtual ToolChangeResult tool_change(unsigned int new_tool, bool last_in_layer, Purpose purpose);

	// Close the current wipe tower layer with a perimeter and possibly fill the unfilled space with a zig-zag.
	// Call this method only if layer_finished() is false.
	virtual ToolChangeResult finish_layer(Purpose purpose);

	// Is the current layer finished? A layer is finished if either the wipe tower is finished, or
	// the wipe tower has been completely covered by the tool change extrusions,
	// or the rest of the tower has been filled by a sparse infill with the finish_layer() method.
	virtual bool 			 layer_finished() const
		{ return m_idx_tool_change_in_layer == m_max_color_changes; }

private:
	WipeTowerPrusaMM();

	// A fill-in direction (positive Y, negative Y) alternates with each layer.
	enum wipe_shape
	{
		SHAPE_NORMAL   = 1,
		SHAPE_REVERSED = -1
	};

	// Left front corner of the wipe tower in mm.
	xy     			m_wipe_tower_pos;
	// Width of the wipe tower.
	float  			m_wipe_tower_width;
	// Per color Y span.
	float  			m_wipe_area;
	// Current Z position.
	float  			m_z_pos 			= 0.f;
	// Current layer height.
	float           m_layer_height      = 0.f;
	// Maximum number of color changes per layer.
	size_t 			m_max_color_changes = 0;
	// Is this the 1st layer of the print? If so, print the brim around the waste tower.
	bool   			m_is_first_layer = false;
	// Is this the last layer of this waste tower?
	bool   			m_is_last_layer  = false;

	// G-code generator parameters.
	float  			m_zhop 			 = 0.5f;
	float  			m_retract		 = 4.f;
	// Width of an extrusion line, also a perimeter spacing for 100% infill.
	float  			m_perimeter_width = 0.5f;
	// Extrusion flow is derived from m_perimeter_width, layer height and filament diameter.
	float  			m_extrusion_flow  = 0.029f;

	// Extruder specific parameters.
	material_type 	m_material[4];
	int  			m_temperature[4];
	int  			m_first_layer_temperature[4];

	// State of the wiper tower generator.
	// Layer change counter for the output statistics.
	unsigned int 	m_num_layer_changes = 0;
	// Tool change change counter for the output statistics.
	unsigned int 	m_num_tool_changes = 0;
	// Layer change counter in this layer. Counting up to m_max_color_changes.
	unsigned int 	m_idx_tool_change_in_layer = 0;
	// A fill-in direction (positive Y, negative Y) alternates with each layer.
	wipe_shape   	m_current_shape = SHAPE_NORMAL;
	unsigned int 	m_current_tool  = 0;
	// Current y position at the wipe tower.
	float 		 	m_current_wipe_start_y = 0.f;
	// How much to wipe the 1st extruder over the wipe tower at the 1st layer
	// after the wipe tower brim has been extruded?
	float  			m_initial_extra_wipe = 0.f;

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
		xy ru;	// right upper
		xy rd;	// right lower
	};

	// Returns gcode for wipe tower brim
	// sideOnly			-- set to false -- experimental, draw brim on sides of wipe tower 
	// offset			-- set to 0		-- experimental, offset to replace brim in front / rear of wipe tower
	ToolChangeResult toolchange_Brim(Purpose purpose, bool sideOnly = false, float y_offset = 0.f);

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
		bool skip_initial_y_move);
	
	void toolchange_Perimeter();
};

}; // namespace Slic3r

#endif /* WipeTowerPrusaMM_hpp_ */
