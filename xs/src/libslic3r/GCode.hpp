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

class GCode;

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
    Polyline travel_to(GCode &gcodegen, Point point);
    
    private:
    MotionPlanner* _external_mp;
    MotionPlanner* _layer_mp;
};

class OozePrevention {
    public:
    bool enable;
    Points standby_points;
    
    OozePrevention();
    std::string pre_toolchange(GCode &gcodegen);
    std::string post_toolchange(GCode &gcodegen);
    
    private:
    int _get_temp(GCode &gcodegen);
};

class Wipe {
    public:
    bool enable;
    Polyline path;
    
    Wipe();
    bool has_path();
    void reset_path();
    std::string wipe(GCode &gcodegen, bool toolchange = false);
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
    const Layer* layer;
    std::map<const PrintObject*,Point> _seam_position;
    bool first_layer; // this flag triggers first layer speeds
    unsigned int elapsed_time; // seconds
    double volumetric_speed;
    
    GCode();
    Point& last_pos();
    void set_last_pos(const Point &pos);
    bool last_pos_defined() const;
    void apply_print_config(const PrintConfig &print_config);
    void set_extruders(const std::vector<unsigned int> &extruder_ids);
    void set_origin(const Pointf &pointf);
    std::string preamble();
    std::string change_layer(const Layer &layer);
    std::string extrude(const ExtrusionEntity &entity, std::string description = "", double speed = -1);
    std::string extrude(ExtrusionLoop loop, std::string description = "", double speed = -1);
    std::string extrude(const ExtrusionPath &path, std::string description = "", double speed = -1);
    std::string travel_to(const Point &point, ExtrusionRole role, std::string comment);
    bool needs_retraction(const Polyline &travel, ExtrusionRole role = erNone);
    std::string retract(bool toolchange = false);
    std::string unretract();
    std::string set_extruder(unsigned int extruder_id);
    Pointf point_to_gcode(const Point &point);
    
    private:
    Point _last_pos;
    bool _last_pos_defined;
    std::string _extrude(ExtrusionPath path, std::string description = "", double speed = -1);
};

}

#endif
