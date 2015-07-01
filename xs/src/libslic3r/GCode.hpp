#ifndef slic3r_GCode_hpp_
#define slic3r_GCode_hpp_

#include <myinit.h>
#include "ExPolygon.hpp"
#include "GCodeWriter.hpp"
#include "Layer.hpp"
#include "MotionPlanner.hpp"
#include "Point.hpp"
#include "PlaceholderParser.hpp"
#include "Print.hpp"
#include "PrintConfig.hpp"
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

class OozePrevention {
    public:
    bool enable;
    Points standby_points;
    
    OozePrevention();
};

class Wipe {
    public:
    bool enable;
    Polyline path;
    
    Wipe();
    bool has_path();
    void reset_path();
    //std::string wipe(GCode &gcodegen, bool toolchange = false);
};

class GCode {
    public:
    
    /* Origin of print coordinates expressed in unscaled G-code coordinates.
       This affects the input arguments supplied to the extrude*() and travel_to()
       methods. */
    Pointf origin;
    FullPrintConfig config;
    GCodeWriter writer;
    PlaceholderParser* placeholder_parser;
    OozePrevention ooze_prevention;
    Wipe wipe;
    AvoidCrossingPerimeters avoid_crossing_perimeters;
    bool enable_loop_clipping;
    bool enable_cooling_markers;
    size_t layer_count;
    int layer_index; // just a counter
    Layer* layer;
    std::map<PrintObject*,Point> _seam_position;
    bool first_layer; // this flag triggers first layer speeds
    unsigned int elapsed_time; // seconds
    Point last_pos;
    bool last_pos_defined;
    double volumetric_speed;
    
    GCode();
};

}

#endif
