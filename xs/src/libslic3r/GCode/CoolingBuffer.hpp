#ifndef slic3r_CoolingBuffer_hpp_
#define slic3r_CoolingBuffer_hpp_

#include "libslic3r.h"
#include <map>
#include <string>

namespace Slic3r {

class GCode;
class Layer;

/*
A standalone G-code filter, to control cooling of the print.
The G-code is processed per layer. Once a layer is collected, fan start / stop commands are edited
and the print is modified to stretch over a minimum layer time.
*/

class CoolingBuffer {
public:
    CoolingBuffer(GCode &gcodegen);
    void        reset();
    void        set_current_extruder(unsigned int extruder_id) { m_current_extruder = extruder_id; }
    std::string process_layer(const std::string &gcode, size_t layer_id);
    GCode* 	    gcodegen() { return &m_gcodegen; }

private:
	CoolingBuffer& operator=(const CoolingBuffer&);

    GCode&              m_gcodegen;
    std::string         m_gcode;
    // Internal data.
    // X,Y,Z,E,F
    std::vector<char>   m_axis;
    std::vector<float>  m_current_pos;
    unsigned int        m_current_extruder;
};

}

#endif
