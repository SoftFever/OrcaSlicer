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

bool
Extruder::use_relative_e_distances() const
{
    // TODO: figure out way to avoid static_cast to access hidden const method
    const ConfigOption *opt = static_cast<const ConfigBase*>(this->config)
        ->option("use_relative_e_distances");
    return *static_cast<const ConfigOptionBool*>(opt);
}

#ifdef SLIC3RXS
REGISTER_CLASS(Extruder, "Extruder");
#endif

}
