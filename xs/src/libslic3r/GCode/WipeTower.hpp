#ifndef slic3r_WipeTower_hpp_
#define slic3r_WipeTower_hpp_

#include <utility>
#include <string>

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
		float x;
		float y;
	};

	WipeTower() {}
	virtual ~WipeTower() {}

	// Return the wipe tower position.
	virtual const xy& position() const = 0;

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

	// Returns gcode for toolchange and the end position.
	// if new_tool == -1, just unload the current filament over the wipe tower.
	virtual std::pair<std::string, xy> tool_change(int new_tool) = 0;

	// Close the current wipe tower layer with a perimeter and possibly fill the unfilled space with a zig-zag.
	// Call this method only if layer_finished() is false.
	virtual std::pair<std::string, xy> finish_layer() = 0;

	// Is the current layer finished? A layer is finished if either the wipe tower is finished, or
	// the wipe tower has been completely covered by the tool change extrusions,
	// or the rest of the tower has been filled by a sparse infill with the finish_layer() method.
	virtual bool 					   layer_finished() const = 0;
};

}; // namespace Slic3r

#endif /* slic3r_WipeTower_hpp_ */
