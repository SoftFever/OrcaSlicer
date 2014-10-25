#ifndef slic3r_Extruder_hpp_
#define slic3r_Extruder_hpp_

#include <myinit.h>
#include "Point.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

class Extruder
{
    public:
    int id;
    double E;
    double absolute_E;
    double retracted;
    double restart_extra;
    double e_per_mm3;
    double retract_speed_mm_min;
    
    Extruder(int id, GCodeConfig *config);
    virtual ~Extruder() {}
    void reset();
    double extrude(double dE);
    double retract(double length, double restart_extra);
    double unretract();
    double e_per_mm(double mm3_per_mm) const;
    double extruded_volume() const;
    double used_filament() const;
    
    double filament_diameter() const;
    double extrusion_multiplier() const;
    double retract_length() const;
    double retract_lift() const;
    int retract_speed() const;
    double retract_restart_extra() const;
    double retract_length_toolchange() const;
    double retract_restart_extra_toolchange() const;
    
    private:
    GCodeConfig *config;
};

}

#endif
