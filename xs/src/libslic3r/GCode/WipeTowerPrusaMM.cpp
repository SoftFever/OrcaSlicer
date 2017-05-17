#include "WipeTowerPrusaMM.hpp"

#include <assert.h>
#include <math.h>
#include <fstream>
#include <iostream>

#ifdef __linux
#include <strings.h>
#endif /* __linux */

#ifdef _MSC_VER 
#define strcasecmp _stricmp
#endif

namespace Slic3r
{

namespace PrusaMultiMaterial {

class Writer
{
public:
	Writer() : 
		m_current_pos(std::numeric_limits<float>::max(), std::numeric_limits<float>::max()),
		m_current_z(0.f),
		m_current_feedrate(0.f),
		m_extrusion_flow(0.f) {}

	Writer&				 set_z(float z) 
		{ m_current_z = z; return *this; }

	Writer& 			 set_extrusion_flow(float flow)
		{ m_extrusion_flow = flow; return *this; }

	Writer& 			 feedrate(float f)
	{
		if (f != m_current_feedrate)
			m_gcode += "G1" + set_format_F(f) + "\n";
		return *this;
	}

	const std::string&   gcode() const { return m_gcode; }
	float                x()     const { return m_current_pos.x; }
	float                y()     const { return m_current_pos.y; }
	const WipeTower::xy& pos()   const { return m_current_pos; }

	// Extrude with an explicitely provided amount of extrusion.
	Writer& extrude_explicit(float x, float y, float e, float f = 0.f) 
	{
		if (x == m_current_pos.x && y == m_current_pos.y && e == 0.f && (f == 0.f || f == m_current_feedrate))
			return *this;
		m_gcode += "G1";
		if (x != m_current_pos.x)
			m_gcode += set_format_X(x);
		if (y != m_current_pos.y)
			m_gcode += set_format_Y(y);
		if (e != 0.f)
			m_gcode += set_format_E(e);
		if (f != 0.f && f != m_current_feedrate)
			m_gcode += set_format_F(f);
		m_gcode += "\n";
		return *this;
	}

	Writer& extrude_explicit(const WipeTower::xy &dest, float e, float f = 0.f) 
		{ return extrude_explicit(dest.x, dest.y, e, f); }

	// Travel to a new XY position. f=0 means use the current value.
	Writer& travel(float x, float y, float f = 0.f)
		{ return extrude_explicit(x, y, 0.f, f); }

	Writer& travel(const WipeTower::xy &dest, float f = 0.f) 
		{ return extrude_explicit(dest.x, dest.y, 0.f, f); }

	// Extrude a line from current position to x, y with the extrusion amount given by m_extrusion_flow.
	Writer& extrude(float x, float y, float f = 0.f)
	{
		float dx = x - m_current_pos.x;
		float dy = y - m_current_pos.y;
		return extrude_explicit(x, y, sqrt(dx*dx+dy*dy) * m_extrusion_flow, f);
	}

	Writer& extrude(const WipeTower::xy &dest, const float f = 0.f) 
		{ return extrude(dest.x, dest.y, f); }

	Writer& deretract(float e, float f = 0.f)
	{
		if (e == 0.f && (f == 0.f || f == m_current_feedrate))
			return *this;
		m_gcode += "G1";
		if (e != 0.f)
			m_gcode += set_format_E(e);
		if (f != 0.f && f != m_current_feedrate)
			m_gcode += set_format_F(f);
		m_gcode += "\n";
		return *this;
	}

	// Derectract while moving in the X direction.
	// If |x| > 0, the feed rate relates to the x distance,
	// otherwise the feed rate relates to the e distance.
	Writer& deretract_move_x(float x, float e, float f = 0.f)
		{ return extrude_explicit(x, m_current_pos.y, e, f); }

	Writer& retract(float e, float f = 0.f)
		{ return deretract(-e, f); }

	// Elevate the extruder head above the current print_z position.
	Writer& z_hop(float hop, float f = 0.f)
	{ 
		m_gcode += std::string("G1") + set_format_Z(m_current_z + hop);
		if (f != 0 && f != m_current_feedrate)
			m_gcode += set_format_F(f);
		m_gcode += "\n";
		return *this;
	}

	// Lower the extruder head back to the current print_z position.
	Writer& z_hop_reset(float hop, float f = 0.f) 
		{ return z_hop(0, f); }

	// Move to x1, +y_increment,
	// extrude quickly amount e to x2 with feed f.
	Writer& ram(float x1, float x2, float dy, float e, float f)
	{
		return  travel(x1, m_current_pos.y + dy, f)
			   .extrude_explicit(x2, m_current_pos.y, e);
	}

	Writer& cool(float x1, float x2, float e1, float e2, float f)
	{
		return  extrude_explicit(x1, m_current_pos.y, e1, f)
			   .extrude_explicit(x2, m_current_pos.y, e2);
	}

	Writer& set_tool(int tool) 
	{
		char buf[64];
		sprintf(buf, "T%d\n", tool);
		m_gcode += buf;
		return *this;
	}

	// Set extruder temperature, don't wait by default.
	Writer& set_extruder_temp(int temperature, bool wait = false)
	{
		char buf[128];
		sprintf(buf, "M%d S%d\n", wait ? 109 : 104, temperature);
		m_gcode += buf;
		return *this;
	};

	// Set speed factor override percentage.
	Writer& speed_override(int speed) 
	{
		char buf[128];
		sprintf(buf, "M220 S%d\n", speed);
		m_gcode += buf;
		return *this;
	};

	// Set digital trimpot motor
	Writer& set_extruder_trimpot(int current) 
	{
		char buf[128];
		sprintf(buf, "M907 E%d\n", current);
		m_gcode += buf;
		return *this;
	};

	Writer& flush_planner_queue() 
	{ 
		m_gcode += "G4 S0\n"; 
		return *this;
	}

	// Reset internal extruder counter.
	Writer& reset_extruder()
	{ 
		m_gcode += "G92 E0\n";
		return *this;
	}

	Writer& comment_with_value(const char *comment, int value)
	{
		char strvalue[64];
		sprintf(strvalue, "%d", value);
		m_gcode += std::string(";") + comment + strvalue + "\n";
		return *this;
	};

	Writer& comment_material(WipeTowerPrusaMM::material_type material)
	{
		m_gcode += "; material : ";
		switch (material)
		{
		case WipeTowerPrusaMM::PVA:
			m_gcode += "#8 (PVA)";
			break;
		case WipeTowerPrusaMM::SCAFF:
			m_gcode += "#5 (Scaffold)";
			break;
		case WipeTowerPrusaMM::FLEX:
			m_gcode += "#4 (Flex)";
			break;
		default:
			m_gcode += "DEFAULT (PLA)";
			break;
		}
		m_gcode += "\n";
		return *this;
	};

	Writer& append(const char *text) { m_gcode += text; return *this; }

private:
	WipeTower::xy m_current_pos;
	float    	  m_current_z;
	float 	  	  m_current_feedrate;
	float 	  	  m_extrusion_flow;
	std::string   m_gcode;

	std::string   set_format_X(float x) {
		char buf[64];
		sprintf(buf, " X%.3f", x);
		m_current_pos.x = x;
		return buf;
	}

	std::string   set_format_Y(float y) {
		char buf[64];
		sprintf(buf, " Y%.3f", y);
		m_current_pos.y = y;
		return buf;
	}

	std::string   set_format_Z(float z) {
		char buf[64];
		sprintf(buf, " Z%.3f", z);
		return buf;
	}

	std::string   set_format_E(float e) {
		char buf[64];
		sprintf(buf, " E%.4f", e);
		return buf;
	}

	std::string   set_format_F(float f) {
		char buf[64];
		sprintf(buf, " F%.0f", f);
		m_current_feedrate = f;
		return buf;
	}
};

} // namespace PrusaMultiMaterial

WipeTowerPrusaMM::material_type WipeTowerPrusaMM::parse_material(const char *name)
{
	if (strcasecmp(name, "PLA") == 0)
		return PLA;
	if (strcasecmp(name, "ABS") == 0)
		return ABS;
	if (strcasecmp(name, "PET") == 0)
		return PET;
	if (strcasecmp(name, "HIPS") == 0)
		return HIPS;
	if (strcasecmp(name, "FLEX") == 0)
		return FLEX;
	if (strcasecmp(name, "SCAFF") == 0)
		return SCAFF;
	if (strcasecmp(name, "EDGE") == 0)
		return EDGE;
	if (strcasecmp(name, "NGEN") == 0)
		return NGEN;
	if (strcasecmp(name, "PVA") == 0)
		return PVA;
	return INVALID;
}

std::pair<std::string, WipeTower::xy> WipeTowerPrusaMM::tool_change(int tool)
{
	// Either it is the last tool unload,
	// or there must be a nonzero wipe tower partitions available.
	assert(tool < 0 || it_layer_tools->wipe_tower_partitions > 0);

	if (m_layer_change_in_layer == (unsigned int)(-1)) {
		// Mark the brim as extruded.
		m_layer_change_in_layer = 0;
		// First layer, prime the extruder.
		return toolchange_Brim(tool);
	}

	box_coordinates cleaning_box(
		m_wipe_tower_pos.x,
		m_wipe_tower_pos.y + m_current_wipe_start_y,
		m_wipe_tower_width, 
		m_wipe_area - m_perimeter_width / 2);

	PrusaMultiMaterial::Writer writer;
	writer.set_extrusion_flow(m_extrusion_flow)
		  .set_z(m_z_pos)
		  .append(";--------------------\n"
			 	  "; CP TOOLCHANGE START\n")
		  .comment_with_value(" toolchange #", m_layer_change_total)
		  .comment_material(m_current_material)
		  .append(";--------------------\n")
		  .speed_override(100)
		  // Lift for a Z hop.
		  .z_hop(m_zhop, 7200)
		  // additional retract on move to tower
		  .retract(m_retract/2, 3600)
		  .travel(((m_current_shape == SHAPE_NORMAL) ? cleaning_box.ld : cleaning_box.rd) + xy(m_perimeter_width, m_current_shape * m_perimeter_width), 7200)
		  // Unlift for a Z hop.
		  .z_hop_reset(7200)
		  // Additional retract on move to tower.
		  .deretract(m_retract/2, 3600)
		  .deretract(m_retract, 1500)
		  // Increase extruder current for ramming.
		  .set_extruder_trimpot(750)
		  .flush_planner_queue();

	// Ram the hot material out of the melt zone, retract the filament into the cooling tubes and let it cool.
	toolchange_Unload(writer, cleaning_box, m_current_material, m_current_shape, 
		m_is_first_layer ? m_first_layer_temperature[tool]  : m_temperature[tool]);

	if (tool >= 0) {
		// This is not the last change.
		// Change the tool, set a speed override for solube and flex materials.
		toolchange_Change(writer, tool, m_current_material, m_material[tool]);
		toolchange_Load(writer, cleaning_box);
		// Wipe the newly loaded filament until the end of the assigned wipe area.
		toolchange_Wipe(writer, cleaning_box, m_current_material);
		// Draw a perimeter around cleaning_box and wipe.
		toolchange_Done(writer, cleaning_box);
	}

	// Reset the extruder current to a normal value.
	writer.set_extruder_trimpot(550)
		  .flush_planner_queue()
		  .reset_extruder()
		  .append("; CP TOOLCHANGE END\n"
	 		      ";------------------\n"
				  "\n\n");

    ++ m_layer_change_in_layer;
    m_current_wipe_start_y += m_wipe_area;
  	m_current_material = m_material[tool];
	return std::pair<std::string, xy>(writer.gcode(), writer.pos());
}

std::pair<std::string, WipeTower::xy> WipeTowerPrusaMM::toolchange_Brim(size_t tool, bool sideOnly, float y_offset)
{
	const box_coordinates wipeTower_box(
		m_wipe_tower_pos,
		m_wipe_tower_width,
		m_wipe_area * float(m_max_color_changes) - m_perimeter_width / 2);

	PrusaMultiMaterial::Writer writer;
	writer.set_extrusion_flow(m_extrusion_flow * 1.1f)
		  // Let the writer know the current Z position as a base for Z-hop.
		  .set_z(m_z_pos)
		  .append(
			";-------------------------------------\n"
			"; CP WIPE TOWER FIRST LAYER BRIM START\n");

	// Move with Z hop and prime the extruder 10*m_perimeter_width left along the vertical edge of the wipe tower.
	writer.z_hop(m_zhop, 7200)
		  .travel(wipeTower_box.lu - xy(m_perimeter_width * 10.f, 0), 6000)
		  .z_hop_reset(7200)
		  .extrude_explicit(wipeTower_box.ld - xy(m_perimeter_width * 10.f, 0), m_retract, 2400)
		  .feedrate(2100);

	toolchange_Change(writer, tool, m_current_material, m_material[tool]);

	if (sideOnly) {
		float x_offset = 0.f;
		for (size_t i = 0; i < 4; ++ i, x_offset += m_perimeter_width)
			writer.travel (wipeTower_box.ld + xy(- x_offset,   y_offset))
				  .extrude(wipeTower_box.lu + xy(- x_offset, - y_offset));
		writer.travel(wipeTower_box.rd + xy(x_offset, y_offset), 7000)
			  .feedrate(2100);
		x_offset = 0.f;
		for (size_t i = 0; i < 4; ++ i, x_offset += m_perimeter_width)
			writer.travel (wipeTower_box.rd + xy(x_offset,   y_offset))
				  .extrude(wipeTower_box.ru + xy(x_offset, - y_offset));
	} else {
		// Extrude 4 rounds of a brim around the future wipe tower.
		box_coordinates box(wipeTower_box);
		box.ld += xy(- m_perimeter_width / 2, 0);
		box.lu += xy(- m_perimeter_width / 2, m_perimeter_width);
		box.rd += xy(  m_perimeter_width / 2, 0);
		box.ru += xy(  m_perimeter_width / 2, m_perimeter_width);
		for (size_t i = 0; i < 4; ++ i) {
			writer.travel(box.ld)
				  .extrude(box.lu) .extrude(box.ru)
				  .extrude(box.rd) .extrude(box.ld);
			box.expand(m_perimeter_width);
		}
	}

	// Move to the front left corner and wipe along the front edge.
	writer.travel(wipeTower_box.ld, 7000)
		  .travel(wipeTower_box.rd)
		  .travel(wipeTower_box.ld)
		  .append("; CP WIPE TOWER FIRST LAYER BRIM END\n"
			      ";-----------------------------------\n");

	return std::pair<std::string, xy>(writer.gcode(), writer.pos());
}

// Ram the hot material out of the melt zone, retract the filament into the cooling tubes and let it cool.
void WipeTowerPrusaMM::toolchange_Unload(
	PrusaMultiMaterial::Writer &writer,
	const box_coordinates 	&cleaning_box,
	const material_type		 material,
	const wipe_shape 	     shape,
	const int 				 temperature)
{
	float xl = cleaning_box.ld.x + (m_perimeter_width / 2);
	float xr = cleaning_box.rd.x - (m_perimeter_width / 2);
	float y_step = shape * m_perimeter_width;

	writer.append("; CP TOOLCHANGE UNLOAD");

	// Ram the hot material out of the extruder melt zone.
	switch (material)
	{
	case PVA:
   		// ramming          start                    end                  y increment     amount feedrate
		writer.ram(xl + m_perimeter_width * 2, xr - m_perimeter_width,     y_step * 1.2f, 3,     4000)
			  .ram(xr - m_perimeter_width,     xl + m_perimeter_width,     y_step * 1.5f, 3,     4500)
			  .ram(xl + m_perimeter_width * 2, xr - m_perimeter_width * 2, y_step * 1.5f, 3,     4800)
			  .ram(xr - m_perimeter_width,     xl + m_perimeter_width,     y_step * 1.5f, 3,     5000);
		break;
	case SCAFF:
		writer.ram(xl + m_perimeter_width * 2, xr - m_perimeter_width,     y_step * 3.f,  3,     4000)
			  .ram(xr - m_perimeter_width,     xl + m_perimeter_width,     y_step * 3.f,  4,     4600)
			  .ram(xl + m_perimeter_width * 2, xr - m_perimeter_width * 2, y_step * 3.f,  4.5,   5200);
		break;
	default:
		writer.ram(xl + m_perimeter_width * 2, xr - m_perimeter_width,     y_step * 1.2f, 1.6f,  4000)
			  .ram(xr - m_perimeter_width,     xl + m_perimeter_width,     y_step * 1.2f, 1.65f, 4600)
			  .ram(xl + m_perimeter_width * 2, xr - m_perimeter_width * 2, y_step * 1.2f, 1.74f, 5200);
	}

	// Pull the filament end into a cooling tube.
	writer.retract(15, 5000).retract(50, 5400).retract(15, 3000).deretract(12, 2000);

	if (temperature != 0)
		// Set the extruder temperature, but don't wait.
		writer.set_extruder_temp(temperature, false);

	// Horizontal cooling moves at the following y coordinate:
	writer.travel(writer.x(), writer.y() + y_step * 0.8f, 1600);
	switch (material)
	{
	case PVA:
		writer.cool(xl, xr, 3, -5, 1600)
			  .cool(xl, xr, 5, -5, 2000)
			  .cool(xl, xr, 5, -5, 2200)
			  .cool(xl, xr, 5, -5, 2400)
			  .cool(xl, xr, 5, -5, 2400)
			  .cool(xl, xr, 5, -5, 2400);
		break;
	case SCAFF:
		writer.cool(xl, xr, 3, -5, 1600)
			  .cool(xl, xr, 5, -5, 2000)
			  .cool(xl, xr, 5, -5, 2200)
			  .cool(xl, xr, 5, -5, 2200)
			  .cool(xl, xr, 5, -5, 2400);
		break;
	default:
		writer.cool(xl, xr, 3, -5, 1600)
			  .cool(xl, xr, 5, -5, 2000)
			  .cool(xl, xr, 5, -5, 2400)
			  .cool(xl, xr, 5, -3, 2400);
	}

	writer.flush_planner_queue();
}

// Change the tool, set a speed override for solube and flex materials.
void WipeTowerPrusaMM::toolchange_Change(
	PrusaMultiMaterial::Writer &writer,
	const int 		tool, 
	material_type  /* current_material */, 
	material_type 	new_material)
{
	// Speed override for the material. Go slow for flex and soluble materials.
	int speed_override;
	switch (new_material) {
	case PVA:   speed_override = 80; break;
	case SCAFF: speed_override = 35; break;
	case FLEX:  speed_override = 35; break;
	default:    speed_override = 100;
	}
	writer.set_tool(tool)
	      .speed_override(speed_override)
	      .flush_planner_queue();
}

void WipeTowerPrusaMM::toolchange_Load(
	PrusaMultiMaterial::Writer &writer,
	const box_coordinates  &cleaning_box)
{
	float xl = cleaning_box.ld.x + m_perimeter_width;
	float xr = cleaning_box.rd.x - m_perimeter_width;

	writer.append("; CP TOOLCHANGE LOAD\n")
	// Load the filament while moving left / right,
	// so the excess material will not create a blob at a single position.
		  .deretract_move_x(xr, 20, 1400)
		  .deretract_move_x(xl, 40, 3000)
		  .deretract_move_x(xr, 20, 1600)
		  .deretract_move_x(xl, 10, 1000);

	// Extrude first five lines (just three lines if colorInit is set).
	writer.extrude(xr, writer.y(), 1600);
	bool colorInit = false;
	size_t pass = colorInit ? 1 : 2;
	for (int i = 0; i < pass; ++ i)
		writer.travel (xr, writer.y() + m_current_shape * m_perimeter_width * 0.85f, 2200)
			  .extrude(xl, writer.y())
			  .travel (xl, writer.y() + m_current_shape * m_perimeter_width * 0.85f)
			  .extrude(xr, writer.y());

	// Reset the extruder current to the normal value.
	writer.set_extruder_trimpot(550);
}

// Wipe the newly loaded filament until the end of the assigned wipe area.
void WipeTowerPrusaMM::toolchange_Wipe(
	PrusaMultiMaterial::Writer &writer,
	const box_coordinates  &cleaning_box,
	const material_type 	material)
{
	// Increase flow on first layer, slow down print.
	writer.set_extrusion_flow(m_extrusion_flow * (m_is_first_layer ? 1.18f : 1.f))
		  .append("; CP TOOLCHANGE WIPE\n");
	float wipe_coeff = m_is_first_layer ? 0.5f : 1.f;
	float xl = cleaning_box.ld.x + 2.f * m_perimeter_width;
	float xr = cleaning_box.rd.x - 2.f * m_perimeter_width;
	// Wipe speed will increase up to 4800.
	float wipe_speed = 4200;
	// Y increment per wipe line.
	float dy = m_current_shape * m_perimeter_width * 0.7f;
	for (bool p = true; ; p = ! p) {
		writer.feedrate((wipe_speed = std::min(4800.f, wipe_speed + 50.f)) * wipe_coeff);
		if (p) {
			writer.extrude(xl - m_perimeter_width / 2, writer.y() + dy);
			writer.extrude(xr + m_perimeter_width, writer.y());
		} else {
			writer.extrude(xl - m_perimeter_width, writer.y() + dy);
			writer.extrude(xr + m_perimeter_width*2, writer.y());
		}
		writer.feedrate((wipe_speed = std::min(4800.f, wipe_speed + 50.f)) * wipe_coeff)
			  .extrude(xr + m_perimeter_width, writer.y() + dy)
			  .extrude(xl - m_perimeter_width, writer.y());
		if ((m_current_shape == SHAPE_NORMAL) ?
			(writer.y() > cleaning_box.lu.y - m_perimeter_width) :
			(writer.y() < cleaning_box.ld.y + m_perimeter_width))
			// Next wipe line does not fit the cleaning box.
			break;
	}
	// Reset the extrusion flow.
	writer.set_extrusion_flow(m_extrusion_flow);
}

// Draw a perimeter around cleaning_box and wipe.
void WipeTowerPrusaMM::toolchange_Done(
	PrusaMultiMaterial::Writer &writer,
	const box_coordinates 	&cleaning_box)
{
	box_coordinates box = cleaning_box;
	if (m_current_shape == SHAPE_REVERSED) {
		std::swap(box.lu, box.ld);
		std::swap(box.ru, box.rd);
	}
	// Draw a perimeter around cleaning_box.
	writer.travel(box.lu, 7000)
		  .extrude(box.ld, 3200).extrude(box.rd)
		  .extrude(box.ru).extrude(box.lu)
		  // Wipe the nozzle.
		  .travel(box.ru, 7200)
		  .travel(box.lu)
		  .feedrate(6000);
}

std::pair<std::string, WipeTower::xy> WipeTowerPrusaMM::close_layer()
{
	PrusaMultiMaterial::Writer writer;
	writer.set_extrusion_flow(m_extrusion_flow)
		  .set_z(m_z_pos)
		  .append(";--------------------\n"
				  "; CP EMPTY GRID START\n")
		  .comment_with_value(" layer #", m_layer_change_total ++);

	// Slow down on the 1st layer.
	float speed_factor = m_is_first_layer ? 0.5f : 1.f;

	box_coordinates _p  = _boxForColor(m_layer_change_in_layer);
	{
		box_coordinates _to = _boxForColor(m_max_color_changes);
		float firstLayerOffset = 0.f;
		_p.ld.y += firstLayerOffset;
		_p.rd.y += firstLayerOffset;
		_p.lu = _to.lu; _p.ru = _to.ru;
	}

	if (m_layer_change_in_layer == 0)
		// There were no tool changes at all in this layer.
		// Jump with retract to _p.ld + a random shift in +x.
		writer.retract(m_retract * 1.5f, 3600)
			  .z_hop(m_zhop, 7200)
			  .travel(_p.ld.x + 5.f + 15.f * float(rand()) / RAND_MAX, _p.ld.y, 7000)
			  .z_hop_reset(7200)
			  .extrude_explicit(_p.ld, m_retract * 1.5f, 3600);

	box_coordinates box = _p;
	writer.extrude(box.lu, 2400 * speed_factor)
		  .extrude(box.ru)
		  .extrude(box.rd)
		  .extrude(box.ld + xy(m_perimeter_width / 2, 0));

	box.expand(- m_perimeter_width / 2);
	writer.extrude(box.lu, 3200 * speed_factor)
		  .extrude(box.ru)
		  .extrude(box.rd)
		  .extrude(box.ld + xy(m_perimeter_width / 2, 0))
		  .extrude(box.ld + xy(m_perimeter_width / 2, m_perimeter_width / 2));

	writer.extrude(_p.ld + xy(m_perimeter_width * 3,   m_perimeter_width), 2900 * speed_factor)
	      .extrude(_p.lu + xy(m_perimeter_width * 3, - m_perimeter_width))
		  .extrude(_p.lu + xy(m_perimeter_width * 6, - m_perimeter_width))
		  .extrude(_p.ld + xy(m_perimeter_width * 6,   m_perimeter_width));

	if (_p.lu.y - _p.ld.y > 4) {
		// Extrude three zig-zags.
		writer.feedrate(3200 * speed_factor);
		float step = (m_wipe_tower_width - m_perimeter_width * 12.f) / 12.f;
		for (size_t i = 0; i < 3; ++ i) {
			writer.extrude(writer.x() + step, _p.ld.y + m_perimeter_width * 8);
			writer.extrude(writer.x()       , _p.lu.y - m_perimeter_width * 8);
			writer.extrude(writer.x() + step, _p.lu.y - m_perimeter_width    );
			writer.extrude(writer.x() + step, _p.lu.y - m_perimeter_width * 8);
			writer.extrude(writer.x()       , _p.ld.y + m_perimeter_width * 8);
			writer.extrude(writer.x() + step, _p.ld.y + m_perimeter_width    );
		}
	}

	// Extrude the perimeter.
	writer.extrude(_p.ru + xy(- m_perimeter_width * 6, - m_perimeter_width), 2900 * speed_factor)
		  .extrude(_p.ru + xy(- m_perimeter_width * 3, - m_perimeter_width))
		  .extrude(_p.rd + xy(- m_perimeter_width * 3,   m_perimeter_width))
		  .extrude(_p.rd + xy(- m_perimeter_width,       m_perimeter_width))
       	  // Wipe along the front side of the current wiping box.
		  .travel(_p.ld + xy(  m_perimeter_width, m_perimeter_width / 2), 7200)
		  .travel(_p.rd + xy(- m_perimeter_width, m_perimeter_width / 2))
		  .append("; CP EMPTY GRID END\n"
			      ";------------------\n\n\n\n\n\n\n");

	m_current_shape = wipe_shape(- m_current_shape);
	return std::pair<std::string, xy>(writer.gcode(), writer.pos());
}

WipeTowerPrusaMM::box_coordinates WipeTowerPrusaMM::_boxForColor(int order) const
{
	return box_coordinates(m_wipe_tower_pos.x, m_wipe_tower_pos.y + m_wipe_area * order - m_perimeter_width / 2, m_wipe_tower_width, m_perimeter_width);
}

}; // namespace Slic3r
