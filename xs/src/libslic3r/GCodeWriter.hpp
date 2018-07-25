#ifndef slic3r_GCodeWriter_hpp_
#define slic3r_GCodeWriter_hpp_

#include "libslic3r.h"
#include <string>
#include "Extruder.hpp"
#include "Point.hpp"
#include "PrintConfig.hpp"
#include "GCode/CoolingBuffer.hpp"

namespace Slic3r {

class GCodeWriter {
public:
    GCodeConfig config;
    bool multiple_extruders;
    
    GCodeWriter() : 
        multiple_extruders(false), m_extrusion_axis("E"), m_extruder(nullptr),
        m_single_extruder_multi_material(false),
        m_last_acceleration(0), m_max_acceleration(0), m_last_fan_speed(0), 
        m_last_bed_temperature(0), m_last_bed_temperature_reached(true), 
        m_lifted(0)
        {}
    Extruder*            extruder()             { return m_extruder; }
    const Extruder*      extruder()     const   { return m_extruder; }

    std::string          extrusion_axis() const { return m_extrusion_axis; }
    void                 apply_print_config(const PrintConfig &print_config);
    // Extruders are expected to be sorted in an increasing order.
    void                 set_extruders(const std::vector<unsigned int> &extruder_ids);
    const std::vector<Extruder>& extruders() const { return m_extruders; }
    std::vector<unsigned int> extruder_ids() const { 
        std::vector<unsigned int> out; 
        out.reserve(m_extruders.size()); 
        for (const Extruder &e : m_extruders) 
            out.push_back(e.id()); 
        return out;
    }
    std::string preamble();
    std::string postamble() const;
    std::string set_temperature(unsigned int temperature, bool wait = false, int tool = -1) const;
    std::string set_bed_temperature(unsigned int temperature, bool wait = false);
    std::string set_fan(unsigned int speed, bool dont_save = false);
    std::string set_acceleration(unsigned int acceleration);
    std::string reset_e(bool force = false);
    std::string update_progress(unsigned int num, unsigned int tot, bool allow_100 = false) const;
    // return false if this extruder was already selected
    bool        need_toolchange(unsigned int extruder_id) const 
        { return m_extruder == nullptr || m_extruder->id() != extruder_id; }
    std::string set_extruder(unsigned int extruder_id)
        { return this->need_toolchange(extruder_id) ? this->toolchange(extruder_id) : ""; }
    // Prefix of the toolchange G-code line, to be used by the CoolingBuffer to separate sections of the G-code
    // printed with the same extruder.
    std::string toolchange_prefix() const;
    std::string toolchange(unsigned int extruder_id);
    std::string set_speed(double F, const std::string &comment = std::string(), const std::string &cooling_marker = std::string()) const;
    std::string travel_to_xy(const Pointf &point, const std::string &comment = std::string());
    std::string travel_to_xyz(const Pointf3 &point, const std::string &comment = std::string());
    std::string travel_to_z(double z, const std::string &comment = std::string());
    bool        will_move_z(double z) const;
    std::string extrude_to_xy(const Pointf &point, double dE, const std::string &comment = std::string());
    std::string extrude_to_xyz(const Pointf3 &point, double dE, const std::string &comment = std::string());
    std::string retract(bool before_wipe = false);
    std::string retract_for_toolchange(bool before_wipe = false);
    std::string unretract();
    std::string lift();
    std::string unlift();
    Pointf3     get_position() const { return m_pos; }

private:
    std::vector<Extruder>    m_extruders;
    std::string     m_extrusion_axis;
    bool            m_single_extruder_multi_material;
    Extruder*       m_extruder;
    unsigned int    m_last_acceleration;
    // Limit for setting the acceleration, to respect the machine limits set for the Marlin firmware.
    // If set to zero, the limit is not in action.
    unsigned int    m_max_acceleration;
    unsigned int    m_last_fan_speed;
    unsigned int    m_last_bed_temperature;
    bool            m_last_bed_temperature_reached;
    double          m_lifted;
    Pointf3         m_pos;

    std::string _travel_to_z(double z, const std::string &comment);
    std::string _retract(double length, double restart_extra, const std::string &comment);
};

} /* namespace Slic3r */

#endif /* slic3r_GCodeWriter_hpp_ */
