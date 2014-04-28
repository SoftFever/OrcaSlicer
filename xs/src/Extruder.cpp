#include "Extruder.hpp"
#ifdef SLIC3RXS
#include "perlglue.hpp"
#endif

namespace Slic3r {

Extruder::Extruder(int id, PrintConfig *config)
:   id(id),
    config(config)
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
    if (this->config->use_relative_e_distances) {
        this->E = 0;
    }

    this->E += dE;
    this->absolute_E += dE;
    return this->E;
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
