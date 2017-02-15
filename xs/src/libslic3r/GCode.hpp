#ifndef slic3r_GCode_hpp_
#define slic3r_GCode_hpp_

#include "libslic3r.h"
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

// Forward declarations.
class GCode;
namespace EdgeGrid { class Grid; }

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
    // If enabled, the G-code generator will put following comments at the ends
    // of the G-code lines: _EXTRUDE_SET_SPEED, _WIPE, _BRIDGE_FAN_START, _BRIDGE_FAN_END
    // Those comments are received and consumed (removed from the G-code) by the CoolingBuffer.pm Perl module.
    bool enable_cooling_markers;
    // Markers for the Pressure Equalizer to recognize the extrusion type.
    // The Pressure Equalizer removes the markers from the final G-code.
    bool enable_extrusion_role_markers;
    // Extended markers for the G-code Analyzer.
    // The G-code Analyzer will remove these comments from the final G-code.
    bool enable_analyzer_markers;
    // How many times will change_layer() be called?
    // change_layer() will update the progress bar.
    size_t layer_count;
    // Progress bar indicator. Increments from -1 up to layer_count.
    int layer_index;
    // Current layer processed. Insequential printing mode, only a single copy will be printed.
    // In non-sequential mode, all its copies will be printed.
    const Layer* layer;
    std::map<const PrintObject*,Point> _seam_position;
    // Distance Field structure to 
    EdgeGrid::Grid *_lower_layer_edge_grid;
    bool first_layer; // this flag triggers first layer speeds
    // Used by the CoolingBuffer.pm Perl module to calculate time spent per layer change.
    // This value is not quite precise. First it only accouts for extrusion moves and travel moves,
    // it does not account for wipe, retract / unretract moves.
    // second it does not account for the velocity profiles of the printer.
    float elapsed_time; // seconds
    double volumetric_speed;
    // Support for the extrusion role markers. Which marker is active?
    ExtrusionRole _last_extrusion_role;
    
    GCode();
    ~GCode();
    const Point& last_pos() const;
    void set_last_pos(const Point &pos);
    bool last_pos_defined() const;
    void apply_print_config(const PrintConfig &print_config);
    void set_extruders(const std::vector<unsigned int> &extruder_ids);
    void set_origin(const Pointf &pointf);
    std::string preamble();
    std::string change_layer(const Layer &layer);
    std::string extrude(const ExtrusionEntity &entity, std::string description = "", double speed = -1);
    std::string extrude(ExtrusionLoop loop, std::string description = "", double speed = -1);
    std::string extrude(ExtrusionMultiPath multipath, std::string description = "", double speed = -1);
    std::string extrude(ExtrusionPath path, std::string description = "", double speed = -1);
    std::string travel_to(const Point &point, ExtrusionRole role, std::string comment);
    bool needs_retraction(const Polyline &travel, ExtrusionRole role = erNone);
    std::string retract(bool toolchange = false);
    std::string unretract();
    std::string set_extruder(unsigned int extruder_id);
    Pointf point_to_gcode(const Point &point);
    
    private:
    Point _last_pos;
    bool _last_pos_defined;
    std::string _extrude(const ExtrusionPath &path, std::string description = "", double speed = -1);
};

}

#endif
