#ifndef slic3r_Extruder_hpp_
#define slic3r_Extruder_hpp_

#include "libslic3r.h"
#include "Point.hpp"

namespace Slic3r {

class GCodeConfig;

class Extruder
{
public:
    Extruder(unsigned int id, GCodeConfig *config);
    virtual ~Extruder() {}

    void   reset() {
        m_E             = 0;
        m_absolute_E    = 0;
        m_retracted     = 0;
        m_restart_extra = 0;
    }

    unsigned int id() const { return m_id; }

    double extrude(double dE);
    double retract(double length, double restart_extra);
    double unretract();
    double E() const { return m_E; }
    void   reset_E() { m_E = 0.; }
    double e_per_mm(double mm3_per_mm) const { return mm3_per_mm * m_e_per_mm3; }
    double e_per_mm3() const { return m_e_per_mm3; }
    // Used filament volume in mm^3.
    double extruded_volume() const;
    // Used filament length in mm.
    double used_filament() const;
    
    double filament_diameter() const;
    double filament_crossection() const { return this->filament_diameter() * this->filament_diameter() * 0.25 * PI; }
    double filament_density() const;
    double filament_cost() const;
    double extrusion_multiplier() const;
    double retract_before_wipe() const;
    double retract_length() const;
    double retract_lift() const;
    int    retract_speed() const;
    int    deretract_speed() const;
    double retract_restart_extra() const;
    double retract_length_toolchange() const;
    double retract_restart_extra_toolchange() const;

private:
    // Private constructor to create a key for a search in std::set.
    Extruder(unsigned int id) : m_id(id) {}

    // Reference to GCodeWriter instance owned by GCodeWriter.
    GCodeConfig *m_config;
    // Print-wide global ID of this extruder.
    unsigned int m_id;
    // Current state of the extruder axis, may be resetted if use_relative_e_distances.
    double       m_E;
    // Current state of the extruder tachometer, used to output the extruded_volume() and used_filament() statistics.
    double       m_absolute_E;
    // Current positive amount of retraction.
    double       m_retracted;
    // When retracted, this value stores the extra amount of priming on deretraction.
    double       m_restart_extra;
    double       m_e_per_mm3;
};

// Sort Extruder objects by the extruder id by default.
inline bool operator==(const Extruder &e1, const Extruder &e2) { return e1.id() == e2.id(); }
inline bool operator!=(const Extruder &e1, const Extruder &e2) { return e1.id() != e2.id(); }
inline bool operator< (const Extruder &e1, const Extruder &e2) { return e1.id() < e2.id(); }
inline bool operator> (const Extruder &e1, const Extruder &e2) { return e1.id() > e2.id(); }

}

#endif
