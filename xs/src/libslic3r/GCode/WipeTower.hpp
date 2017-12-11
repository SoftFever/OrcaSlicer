#ifndef slic3r_WipeTower_hpp_
#define slic3r_WipeTower_hpp_

#include <utility>
#include <string>
#include <vector>

namespace Slic3r
{

// A pure virtual WipeTower definition.
class WipeTower
{
public:
	// Internal point class, to make the wipe tower independent from other slic3r modules.
	// This is important for Prusa Research as we want to build the wipe tower post-processor independently from slic3r.
	struct xy
	{
		xy(float x = 0.f, float y = 0.f) : x(x), y(y) {}
		xy  operator+(const xy &rhs) const { xy out(*this); out.x += rhs.x; out.y += rhs.y; return out; }
		xy  operator-(const xy &rhs) const { xy out(*this); out.x -= rhs.x; out.y -= rhs.y; return out; }
		xy& operator+=(const xy &rhs) { x += rhs.x; y += rhs.y; return *this; }
		xy& operator-=(const xy &rhs) { x -= rhs.x; y -= rhs.y; return *this; }
		bool operator==(const xy &rhs) const { return x == rhs.x && y == rhs.y; }
		bool operator!=(const xy &rhs) const { return x != rhs.x || y != rhs.y; }
		float x;
		float y;
	};

	WipeTower() {}
	virtual ~WipeTower() {}

	// Return the wipe tower position.
	virtual const xy& position() const = 0;

	// Return the wipe tower width.
	virtual float     width() const = 0;

	// The wipe tower is finished, there should be no more tool changes or wipe tower prints.
	virtual bool 	  finished() const = 0;

	// Switch to a next layer.
	virtual void 	  set_layer(
		// Print height of this layer.
		float  print_z,
		// Layer height, used to calculate extrusion the rate. 
		float  layer_height, 
		// Maximum number of tool changes on this layer or the layers below.
		size_t max_tool_changes, 
		// Is this the first layer of the print? In that case print the brim first.
		bool   is_first_layer,
		// Is this the last layer of the wipe tower?
		bool   is_last_layer) = 0;

	enum Purpose {
		PURPOSE_MOVE_TO_TOWER,
		PURPOSE_EXTRUDE,
		PURPOSE_MOVE_TO_TOWER_AND_EXTRUDE,
	};

	// Extrusion path of the wipe tower, for 3D preview of the generated tool paths.
	struct Extrusion
	{
		Extrusion(const xy &pos, float width, unsigned int tool) : pos(pos), width(width), tool(tool) {}
		// End position of this extrusion.
		xy				pos;
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
		xy						start_pos;
		// Last point, at which the normal G-code generator of Slic3r shall continue.
		// At this position the extruder is loaded and there is no Z-hop applied.
		xy						end_pos;
		// Time elapsed over this tool change.
		// This is useful not only for the print time estimation, but also for the control of layer cooling.
		float  				    elapsed_time;

		// Sum the total length of the extrusion.
		float total_extrusion_length_in_plane() {
			float e_length = 0.f;
			for (size_t i = 1; i < this->extrusions.size(); ++ i) {
				const Extrusion &e = this->extrusions[i];
				if (e.width > 0) {
					xy v = e.pos - (&e - 1)->pos;
					e_length += sqrt(v.x*v.x+v.y*v.y);
				}
			}
			return e_length;
		}
	};

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
		Purpose 					purpose = PURPOSE_MOVE_TO_TOWER_AND_EXTRUDE) = 0;

	// Returns gcode for toolchange and the end position.
	// if new_tool == -1, just unload the current filament over the wipe tower.
	virtual ToolChangeResult tool_change(unsigned int new_tool, bool last_in_layer, Purpose purpose = PURPOSE_MOVE_TO_TOWER_AND_EXTRUDE) = 0;

	// Close the current wipe tower layer with a perimeter and possibly fill the unfilled space with a zig-zag.
	// Call this method only if layer_finished() is false.
	virtual ToolChangeResult finish_layer(Purpose purpose = PURPOSE_MOVE_TO_TOWER_AND_EXTRUDE) = 0;

	// Is the current layer finished? A layer is finished if either the wipe tower is finished, or
	// the wipe tower has been completely covered by the tool change extrusions,
	// or the rest of the tower has been filled by a sparse infill with the finish_layer() method.
	virtual bool 		     layer_finished() const = 0;
};

}; // namespace Slic3r

#endif /* slic3r_WipeTower_hpp_ */
