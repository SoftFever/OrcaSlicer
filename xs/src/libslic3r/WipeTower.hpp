#ifndef PrusaSingleExtruderMM_WipeTower_hpp_
#define PrusaSingleExtruderMM_WipeTower_hpp_

#include <algorithm>
#include <string>
#include <utility>

namespace PrusaSingleExtruderMM
{

class WipeTower
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

	enum wipe_shape
	{
		NORMAL   = 1,
		REVERSED = -1
	};

	struct xy
	{
		xy(float x = 0.f, float y = 0.f) : x(x), y(y) {}
		float x;
		float y;
	};

	// Parse material name into material_type.
	static material_type parse_material(const char *name);

	// x			-- x coordinates of wipe tower in mm ( left bottom corner )
	// y			-- y coordinates of wipe tower in mm ( left bottom corner )
	// width		-- width of wipe tower in mm ( default 60 mm - leave as it is )
	// wipe_area	-- space available for one toolchange in mm
	// colors		-- maximum colors for object
	WipeTower(float x, float y, float width, float wipe_area, int color_changes) :
		m_wipe_tower_pos(x, y),
		m_wipe_tower_width(width),
		m_wipe_area(wipe_area),
		m_color_changes(color_changes),
		m_z_pos(0.f) {}

	// colors		-- maximum color changes for layer
	void setColors(int colors) { m_color_changes = colors; }

	// Z height		-- mm
	void setZ(float z) { m_z_pos = z; }

	// _retract - retract value in mm
	void setRetract(float _retract) { retract = _retract; }
	
	// _zHop - z hop value in mm
	void setZHop(float _zhop) { zHop = _zhop; }

	void setExtrusion(int layerHeight)
	{
		// set extrusion coefficient for layer height 
		// layerHeight		-- mm * 100

		switch (layerHeight)
		{
		case 15:
			extrusion_flow = (float)0.024;
			break;
		case 20:
			extrusion_flow = (float)0.029;
			break;
		default:
			break;
		}
	}

	/*
		Returns gcode for wipe tower brim
	
		sideOnly			-- set to false -- experimental, draw brim on sides of wipe tower 
		offset				-- set to 0		-- experimental, offset to replace brim in front / rear of wipe tower
	*/
	std::string FirstLayer(bool sideOnly, float offset);	

	/*
		Returns gcode for toolchange 
	
		tool				-- extruder #   0 - 3
		current_material	-- filament type currently used to print and loaded in nozzle -- see enum material_type
		new_material		-- filament type that will be loaded in to the nozzle  -- see enum material_type
		temperature			-- temperature in Celsius for new filament that will be loaded into the nozzle	
		shape				-- orientation of purge / wipe shape	-- 0 = normal, 1 = reversed -- enum wipe_shape
		count				-- total toolchanges done counter ( comment in  header of toolchange only )
		spaceAvailable		-- space available for toolchange ( purge / load / wipe ) - in mm
		wipeStartY			-- experimental, don't use, set to 0
		lastInFile			-- for last toolchange in object set to true to unload filament into cooling tube, for all other set to false
		colorInit			-- experimental, set to 0
	*/
	std::pair<std::string, WipeTower::xy> Toolchange(
		int tool, material_type current_material, material_type new_material, int temperature, wipe_shape shape, 
		int count, float spaceAvailable, float wipeStartY, bool lastInFile, bool colorInit);	

	/*
		Returns gcode to draw empty pattern in place of a toolchange -> in case there are less toolchanges atm then what is required later 

		order				-- total toolchanges done for current layer
		total				-- total colors in current z layer including empty ones
		layer				-- Z height in mm * 100  ( slows down print for first layer )
		afterToolchange		-- true - ignore some not neccesary moves | false - do whole move from object to wipe tower
		firstLayerOffset	-- experimental , set to 0
	*/
	std::string Perimeter(int order, int total, int layer, bool afterToolchange, int firstLayerOffset);

private:
	WipeTower();

	// Left front corner of the wipe tower in mm.
	xy    m_wipe_tower_pos;
	// Width of the wipe tower.
	float m_wipe_tower_width;
	// Per color Y span.
	float m_wipe_area;
	// Current Z position.
	float m_z_pos;
	// Maximum number of color changes per layer.
	int   m_color_changes;
	// Current y position at the wipe tower.
	float m_y_position;

	float zHop 				= 0.5f;
	float retract 			= 4.f;
	float perimeterWidth 	= 0.5f;
	float extrusion_flow 	= 0.029f;

	struct box_coordinates
	{
		box_coordinates(float left, float bottom, float width, float height) :
			ld(left        , bottom         ),
			lu(left        , bottom + height),
			rd(left + width, bottom         ),
			ru(left + width, bottom + height) {}
		box_coordinates(const xy &pos, float width, float height) : box_coordinates(pos.x, pos.y, width, height) {}
		xy ld;  // left down
		xy lu;	// left upper 
		xy ru;	// right upper
		xy rd;	// right lower
	};
	
	std::string toolchange_Unload(const box_coordinates &cleaning_box, material_type material, wipe_shape shape, int temperature);
	std::string toolchange_Change(int tool, material_type current_material, material_type new_material);
	std::string toolchange_Load(const box_coordinates &cleaning_box, material_type material, wipe_shape shape, bool colorInit);
	std::string toolchange_Wipe(const box_coordinates &cleaning_box, material_type material, wipe_shape shape);
	std::string toolchange_Done(const box_coordinates &cleaning_box, material_type material, wipe_shape shape);

	box_coordinates _boxForColor(int order) const;
};

}; // namespace PrusaSingleExtruderMM

#endif /* PrusaSingleExtruderMM_WipeTower_hpp_ */
