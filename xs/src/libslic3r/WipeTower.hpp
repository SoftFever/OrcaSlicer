#ifndef PrusaSingleExtruderMM_WipeTower_hpp_
#define PrusaSingleExtruderMM_WipeTower_hpp_

#include <algorithm>
#include <string>
#include <utility>

namespace PrusaSingleExtruderMM
{

class Writer;

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
		SHAPE_NORMAL   = 1,
		SHAPE_REVERSED = -1
	};

	struct xy
	{
		xy(float x = 0.f, float y = 0.f) : x(x), y(y) {}
		xy  operator+(const xy &rhs) const { xy out(*this); out.x += rhs.x; out.y += rhs.y; return out; }
		xy  operator-(const xy &rhs) const { xy out(*this); out.x -= rhs.x; out.y -= rhs.y; return out; }
		xy& operator+=(const xy &rhs) { x += rhs.x; y += rhs.y; return *this; }
		xy& operator-=(const xy &rhs) { x -= rhs.x; y -= rhs.y; return *this; }
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
	bool is_first_layer() const { return m_z_pos < 0.205f; }

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
	std::string FirstLayer(bool sideOnly = false, float y_offset = 0.f);

	// Returns gcode for toolchange 
	std::pair<std::string, WipeTower::xy> Toolchange(
		// extruder #   0 - 3
		const int 			tool, 
		// filament type currently used to print and loaded in nozzle -- see enum material_type
		const material_type current_material, 
		// filament type that will be loaded in to the nozzle  -- see enum material_type
		const material_type new_material, 
		// temperature in Celsius for new filament that will be loaded into the nozzle	
		const int 			temperature, 
		// orientation of purge / wipe shape (NORMAL / REVERSED)
		const wipe_shape 	shape, 
		// total toolchanges done counter ( comment in  header of toolchange only )
		const int 			count, 
		// space available for toolchange ( purge / load / wipe ) - in mm
		const float 		spaceAvailable, 
		// experimental, don't use, set to 0
		const float 		wipeStartY, 
		// for last toolchange in object set to true to unload filament into cooling tube, for all other set to false
		const bool  		lastInFile, 
		// experimental, set to false
		const bool 			colorInit = false);

	/*
		Returns gcode to draw empty pattern in place of a toolchange -> in case there are less toolchanges atm then what is required later 

		order				-- total toolchanges done for current layer
		total				-- total colors in current z layer including empty ones
		afterToolchange		-- true - ignore some not neccesary moves | false - do whole move from object to wipe tower
		firstLayerOffset	-- experimental , set to 0
	*/
	std::string Perimeter(int order, int total, int Layer, bool afterToolchange, int firstLayerOffset = 0);

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
		void expand(const float offset) {
			ld += xy(- offset, - offset);
			lu += xy(- offset,   offset);
			rd += xy(  offset, - offset);
			ru += xy(  offset,   offset);
		}
		xy ld;  // left down
		xy lu;	// left upper 
		xy ru;	// right upper
		xy rd;	// right lower
	};
	
	void toolchange_Unload(
		Writer				   &writer,
		const box_coordinates  &cleaning_box, 
		const material_type	 	material,
		const wipe_shape 	    shape,
		const int 				temperature);

	void toolchange_Change(
		Writer				   &writer,
		int 					tool,
		material_type 			current_material,
		material_type 			new_material);
	
	void toolchange_Load(
		Writer				   &writer,
		const box_coordinates  &cleaning_box,
		const material_type 	material,
		const wipe_shape 		shape,
		const bool 				colorInit);
	
	void toolchange_Wipe(
		Writer				   &writer,
		const box_coordinates  &cleaning_box, 
		const material_type 	material,
		const wipe_shape 	    shape);
	
	void toolchange_Done(
		Writer				   &writer,
		const box_coordinates  &cleaning_box, 
		const material_type 	material, 
		const wipe_shape 		shape);

	box_coordinates _boxForColor(int order) const;
};

}; // namespace PrusaSingleExtruderMM

#endif /* PrusaSingleExtruderMM_WipeTower_hpp_ */
