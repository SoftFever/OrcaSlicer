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
    if (this->use_relative_e_distances()) {
        this->E = 0;
    }

    this->E += dE;
    this->absolute_E += dE;
    return this->E;
}

template <typename Val, class OptType> Val
Extruder::get_config(const char *name) const
{
    // TODO: figure out way to avoid static_cast to access hidden const method
    const ConfigOption *opt = static_cast<const ConfigBase*>(this->config)
        ->option(name);
    return dynamic_cast<const OptType *>(opt)->get_at(this->id);
}

bool
Extruder::use_relative_e_distances() const
{
    // not using get_config because use_relative_e_distances is global
    // for all extruders

    // TODO: figure out way to avoid static_cast to access hidden const method
    const ConfigOption *opt = static_cast<const ConfigBase*>(this->config)
        ->option("use_relative_e_distances");
    return *static_cast<const ConfigOptionBool*>(opt);
}

Pointf
Extruder::extruder_offset() const
{
    return get_config<Pointf, ConfigOptionPoints>("extruder_offset");
}

double
Extruder::nozzle_diameter() const
{
    return get_config<double, ConfigOptionFloats>("nozzle_diameter");
}

double
Extruder::filament_diameter() const
{
    return get_config<double, ConfigOptionFloats>("filament_diameter");
}

double
Extruder::extrusion_multiplier() const
{
    return get_config<double, ConfigOptionFloats>("extrusion_multiplier");
}

int
Extruder::temperature() const
{
    return get_config<int, ConfigOptionInts>("temperature");
}

int
Extruder::first_layer_temperature() const
{
    return get_config<int, ConfigOptionInts>("first_layer_temperature");
}

double
Extruder::retract_length() const
{
    return get_config<double, ConfigOptionFloats>("retract_length");
}

double
Extruder::retract_lift() const
{
    return get_config<double, ConfigOptionFloats>("retract_lift");
}

int
Extruder::retract_speed() const
{
    return get_config<int, ConfigOptionInts>("retract_speed");
}

double
Extruder::retract_restart_extra() const
{
    return get_config<double, ConfigOptionFloats>("retract_restart_extra");
}

double
Extruder::retract_before_travel() const
{
    return get_config<double, ConfigOptionFloats>("retract_before_travel");
}

bool
Extruder::retract_layer_change() const
{
    return get_config<bool, ConfigOptionBools>("retract_layer_change");
}

double
Extruder::retract_length_toolchange() const
{
    return get_config<double, ConfigOptionFloats>("retract_length_toolchange");
}

double
Extruder::retract_restart_extra_toolchange() const
{
    return get_config<double, ConfigOptionFloats>(
        "retract_restart_extra_toolchange");
}

bool
Extruder::wipe() const
{
    return get_config<bool, ConfigOptionBools>("wipe");
}


#ifdef SLIC3RXS
REGISTER_CLASS(Extruder, "Extruder");
#endif

}
