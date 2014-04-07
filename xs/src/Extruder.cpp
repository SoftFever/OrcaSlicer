#include "Extruder.hpp"

namespace Slic3r {

Extruder::Extruder(int id, bool use_relative_e_distances,
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
    double wipe)
:   id(id),
    use_relative_e_distances(use_relative_e_distances),
    extruder_offset(*extruder_offset),
    nozzle_diameter(nozzle_diameter),
    filament_diameter(filament_diameter),
    extrusion_multiplier(extrusion_multiplier),
    temperature(temperature),
    first_layer_temperature(first_layer_temperature),
    retract_length(retract_length),
    retract_lift(retract_lift),
    retract_speed(retract_speed),
    retract_restart_extra(retract_restart_extra),
    retract_before_travel(retract_before_travel),
    retract_layer_change(retract_layer_change),
    retract_length_toolchange(retract_length_toolchange),
    retract_restart_extra_toolchange(retract_restart_extra_toolchange),
    wipe(wipe)
{
    reset();
}

void
Extruder::reset()
{
    this->E = 0;
    this->absolute_E = 0;
    this->retracted = 0;
    this->restart_extra = 0;
}

double
Extruder::extrude(double dE)
{
    if (use_relative_e_distances) {
        this->E = 0;
    }

    this->E += dE;
    this->absolute_E += dE;
    return this->E;
}

}
