#include "Extruder.hpp"

namespace Slic3r {

Extruder::Extruder(int id, PrintConfig *config)
:   id(id),
    config(config)
{
    reset();
    
    // cache values that are going to be called often
    this->e_per_mm3 = this->extrusion_multiplier()
        * (4 / ((this->filament_diameter() * this->filament_diameter()) * PI));
    this->retract_speed_mm_min = this->retract_speed() * 60;
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
    // in case of relative E distances we always reset to 0 before any output
    if (this->config->use_relative_e_distances)
        this->E = 0;

    this->E += dE;
    this->absolute_E += dE;
    return dE;
}

/* This method makes sure the extruder is retracted by the specified amount
   of filament and returns the amount of filament retracted.
   If the extruder is already retracted by the same or a greater amount, 
   this method is a no-op.
   The restart_extra argument sets the extra length to be used for
   unretraction. If we're actually performing a retraction, any restart_extra
   value supplied will overwrite the previous one if any. */
double
Extruder::retract(double length, double restart_extra)
{
    // in case of relative E distances we always reset to 0 before any output
    if (this->config->use_relative_e_distances)
        this->E = 0;
    
    double to_retract = length - this->retracted;
    if (to_retract > 0) {
        this->E -= to_retract;
        this->absolute_E -= to_retract;
        this->retracted += to_retract;
        this->restart_extra = restart_extra;
        return to_retract;
    } else {
        return 0;
    }
}

double
Extruder::unretract()
{
    double dE = this->retracted + this->restart_extra;
    this->extrude(dE);
    this->retracted = 0;
    this->restart_extra = 0;
    return dE;
}

double
Extruder::e_per_mm(double mm3_per_mm) const
{
    return mm3_per_mm * this->e_per_mm3;
}

double
Extruder::extruded_volume() const
{
    return this->used_filament() * (this->filament_diameter() * this->filament_diameter()) * PI/4;
}

double
Extruder::used_filament() const
{
    // Any current amount of retraction should not affect used filament, since
    // it represents empty volume in the nozzle. We add it back to E.
    return this->absolute_E + this->retracted;
}

Pointf
Extruder::extruder_offset() const
{
    return this->config->extruder_offset.get_at(this->id);
}

double
Extruder::nozzle_diameter() const
{
    return this->config->nozzle_diameter.get_at(this->id);
}

double
Extruder::filament_diameter() const
{
    return this->config->filament_diameter.get_at(this->id);
}

double
Extruder::extrusion_multiplier() const
{
    return this->config->extrusion_multiplier.get_at(this->id);
}

int
Extruder::temperature() const
{
    return this->config->temperature.get_at(this->id);
}

int
Extruder::first_layer_temperature() const
{
    return this->config->first_layer_temperature.get_at(this->id);
}

double
Extruder::retract_length() const
{
    return this->config->retract_length.get_at(this->id);
}

double
Extruder::retract_lift() const
{
    return this->config->retract_lift.get_at(this->id);
}

int
Extruder::retract_speed() const
{
    return this->config->retract_speed.get_at(this->id);
}

double
Extruder::retract_restart_extra() const
{
    return this->config->retract_restart_extra.get_at(this->id);
}

double
Extruder::retract_before_travel() const
{
    return this->config->retract_before_travel.get_at(this->id);
}

bool
Extruder::retract_layer_change() const
{
    return this->config->retract_layer_change.get_at(this->id);
}

double
Extruder::retract_length_toolchange() const
{
    return this->config->retract_length_toolchange.get_at(this->id);
}

double
Extruder::retract_restart_extra_toolchange() const
{
    return this->config->retract_restart_extra_toolchange.get_at(this->id);
}

bool
Extruder::wipe() const
{
    return this->config->wipe.get_at(this->id);
}


#ifdef SLIC3RXS
REGISTER_CLASS(Extruder, "Extruder");
#endif

}
