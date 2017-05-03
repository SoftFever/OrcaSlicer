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
    CoolingBuffer(GCode &gcodegen) : m_gcodegen(gcodegen), m_elapsed_time(0.), m_layer_id(0) {}
    std::string append(const std::string &gcode, size_t object_id, size_t layer_id, bool is_support);
    std::string flush();
    GCode* 	    gcodegen() { return &m_gcodegen; };
    
private:
	CoolingBuffer& operator=(const CoolingBuffer&);

    GCode&              m_gcodegen;
    std::string         m_gcode;
    float               m_elapsed_time;
    size_t              m_layer_id;
    std::set<size_t>	m_object_ids_visited;
};

}

#endif
