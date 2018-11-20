#ifndef slic3r_CoolingBuffer_hpp_
#define slic3r_CoolingBuffer_hpp_

#include "libslic3r.h"
#include <map>
#include <string>

namespace Slic3r {

class GCode;
class Layer;
class PerExtruderAdjustments;

// A standalone G-code filter, to control cooling of the print.
// The G-code is processed per layer. Once a layer is collected, fan start / stop commands are edited
// and the print is modified to stretch over a minimum layer time.
//
// The simple it sounds, the actual implementation is significantly more complex.
// Namely, for a multi-extruder print, each material may require a different cooling logic.
// For example, some materials may not like to print too slowly, while with some materials 
// we may slow down significantly.
//
class CoolingBuffer {
public:
    CoolingBuffer(GCode &gcodegen);
    void        reset();
    void        set_current_extruder(unsigned int extruder_id) { m_current_extruder = extruder_id; }
    std::string process_layer(const std::string &gcode, size_t layer_id);
    GCode* 	    gcodegen() { return &m_gcodegen; }

private:
	CoolingBuffer& operator=(const CoolingBuffer&) = delete;
    std::vector<PerExtruderAdjustments> parse_layer_gcode(const std::string &gcode, std::vector<float> &current_pos) const;
    float       calculate_layer_slowdown(std::vector<PerExtruderAdjustments> &per_extruder_adjustments);
    // Apply slow down over G-code lines stored in per_extruder_adjustments, enable fan if needed.
    // Returns the adjusted G-code.
    std::string apply_layer_cooldown(const std::string &gcode, size_t layer_id, float layer_time, std::vector<PerExtruderAdjustments> &per_extruder_adjustments);

    GCode&              m_gcodegen;
    std::string         m_gcode;
    // Internal data.
    // X,Y,Z,E,F
    std::vector<char>   m_axis;
    std::vector<float>  m_current_pos;
    unsigned int        m_current_extruder;

    // Old logic: proportional.
    bool                m_cooling_logic_proportional = false;
};

}

#endif
