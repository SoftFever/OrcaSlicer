#ifndef slic3r_CoolingBuffer_hpp_
#define slic3r_CoolingBuffer_hpp_

#include "libslic3r.h"
#include <map>
#include <string>

namespace Slic3r {

class GCode;
class Layer;

struct ElapsedTime
{
    ElapsedTime(unsigned int extruder_id = 0) : extruder_id(extruder_id) { this->reset(); }
    void reset() { total = bridges = external_perimeters = travel = other = 0.f; }

    ElapsedTime& operator+=(const ElapsedTime &rhs) {
        this->total                 += rhs.total;
        this->bridges               += rhs.bridges;
        this->external_perimeters   += rhs.external_perimeters;
        this->travel                += rhs.travel;
        this->other                 += rhs.other;
        return *this;
    }

    // Potion of the total time, which cannot be stretched to heed the minimum layer print time.
    float   non_stretchable() const { return this->bridges + this->travel + this->other; }
    // Potion of the total time, which could be stretched to heed the minimum layer print time.
    float   stretchable() const { return this->total - this->non_stretchable(); }

    // For which extruder ID has this statistics been collected?
    unsigned int    extruder_id;
    // Total time.
    float           total;
    // Per feature time slices.
    float           bridges;
    float           external_perimeters;
    float           travel;
    float           other;
};

// Sort ElapsedTime objects by the extruder id by default.
inline bool operator==(const ElapsedTime &e1, const ElapsedTime &e2) { return e1.extruder_id == e2.extruder_id; }
inline bool operator!=(const ElapsedTime &e1, const ElapsedTime &e2) { return e1.extruder_id != e2.extruder_id; }
inline bool operator< (const ElapsedTime &e1, const ElapsedTime &e2) { return e1.extruder_id < e2.extruder_id; }
inline bool operator> (const ElapsedTime &e1, const ElapsedTime &e2) { return e1.extruder_id > e2.extruder_id; }

/*
A standalone G-code filter, to control cooling of the print.
The G-code is processed per layer. Once a layer is collected, fan start / stop commands are edited
and the print is modified to stretch over a minimum layer time.
*/

class CoolingBuffer {
public:
    CoolingBuffer(GCode &gcodegen) : m_gcodegen(gcodegen) {}
    void        set_current_extruder(unsigned int extruder_id) { m_current_extruder = extruder_id; }
    std::string process_layer(const std::string &gcode, size_t layer_id);
    GCode* 	    gcodegen() { return &m_gcodegen; }

private:
	CoolingBuffer& operator=(const CoolingBuffer&);

    GCode&              m_gcodegen;
    std::string         m_gcode;
    unsigned int        m_current_extruder;
    std::set<size_t>	m_object_ids_visited;
};

}

#endif
