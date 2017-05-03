#ifndef slic3r_Extruder_hpp_
#define slic3r_Extruder_hpp_

#include "libslic3r.h"
#include "Point.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

class Extruder
{
public:
    unsigned int id;
    double E;
    double absolute_E;
    double retracted;
    double restart_extra;
    double e_per_mm3;
    double retract_speed_mm_min;

    Extruder(unsigned int id, GCodeConfig *config);
    virtual ~Extruder() {}

    void   reset();
    double extrude(double dE);
    double retract(double length, double restart_extra);
    double unretract();
    double e_per_mm(double mm3_per_mm) const;
    double extruded_volume() const;
    double used_filament() const;
    
    double filament_diameter() const;
    double filament_density() const;
    double filament_cost() const;
    double extrusion_multiplier() const;
    double retract_length() const;
    double retract_lift() const;
    int    retract_speed() const;
    double retract_restart_extra() const;
    double retract_length_toolchange() const;
    double retract_restart_extra_toolchange() const;

    static Extruder key(unsigned int id) { return Extruder(id); }

private:
    // Private constructor to create a key for a search in std::set.
    Extruder(unsigned int id) : id(id) {}

    GCodeConfig *m_config;
};

// Sort Extruder objects by the extruder id by default.
inline bool operator==(const Extruder &e1, const Extruder &e2) { return e1.id == e2.id; }
inline bool operator!=(const Extruder &e1, const Extruder &e2) { return e1.id != e2.id; }
inline bool operator< (const Extruder &e1, const Extruder &e2) { return e1.id < e2.id; }
inline bool operator> (const Extruder &e1, const Extruder &e2) { return e1.id > e2.id; }

}

#endif
