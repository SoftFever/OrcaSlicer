#ifndef slic3r_GCode_hpp_
#define slic3r_GCode_hpp_

#include <myinit.h>
#include "ExPolygon.hpp"
#include "MotionPlanner.hpp"
#include <string>

namespace Slic3r {

// draft for a binary representation of a G-code line

class AvoidCrossingPerimeters {
    public:
    
    // this flag triggers the use of the external configuration space
    bool use_external_mp;
    bool use_external_mp_once;  // just for the next travel move
    
    // this flag disables avoid_crossing_perimeters just for the next travel move
    // we enable it by default for the first travel move in print
    bool disable_once;
    
    AvoidCrossingPerimeters();
    ~AvoidCrossingPerimeters();
    void init_external_mp(const ExPolygons &islands);
    void init_layer_mp(const ExPolygons &islands);
    
    //Polyline travel_to(GCode &gcodegen, const Point &point);
    Polyline travel_to(Point point, const Pointf &gcodegen_origin,
        const Point &gcodegen_last_pos);
    
    private:
    MotionPlanner* _external_mp;
    MotionPlanner* _layer_mp;
};

}

#endif
