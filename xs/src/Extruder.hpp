#ifndef slic3r_Extruder_hpp_
#define slic3r_Extruder_hpp_

#include <myinit.h>
#include "Point.hpp"

namespace Slic3r {

class Extruder
{
    public:
    Extruder(int id, bool use_relative_e_distances,
        const Point *extruder_offset,
        double nozzle_diameter,
        double filament_diameter,
        double extrusion_multiplier,
        int temperature,
        int first_layer_temperature,
        double retract_length,
        double retract_lift,
        int retract_speed,
        double retract_restart_extra,
        double retract_before_travel,
        bool retract_layer_change,
        double retract_length_toolchange,
        double retract_restart_extra_toolchange,
        double wipe);
    virtual ~Extruder() {}
    void reset();
    double extrude(double dE);

    int id;
    bool use_relative_e_distances;
    double E;
    double absolute_E;
    double retracted;
    double restart_extra;

    // options:
    Point extruder_offset;
    double nozzle_diameter;
    double filament_diameter;
    double extrusion_multiplier;
    int temperature;
    int first_layer_temperature;
    double retract_length;
    double retract_lift;
    int retract_speed;
    double retract_restart_extra;
    double retract_before_travel;
    bool retract_layer_change;
    double retract_length_toolchange;
    double retract_restart_extra_toolchange;
    double wipe;
};

}

#endif
