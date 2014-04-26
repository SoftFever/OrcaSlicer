#ifndef slic3r_Extruder_hpp_
#define slic3r_Extruder_hpp_

#include <myinit.h>
#include "Point.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

class Extruder
{
    public:
    Extruder(int id, PrintConfig *config);
    virtual ~Extruder() {}
    void reset();
    double extrude(double dE);

    bool use_relative_e_distances() const;

    int id;
    double E;
    double absolute_E;
    double retracted;
    double restart_extra;

    // TODO: maybe better to keep a reference to an existing object than copy it
    PrintConfig config;
};

}

#endif
