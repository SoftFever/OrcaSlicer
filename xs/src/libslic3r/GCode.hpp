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
#include "GCode/CoolingBuffer.hpp"
#include "GCode/PressureEqualizer.hpp"
#include "GCode/SpiralVase.hpp"
#include "EdgeGrid.hpp"

#include <memory>
#include <string>

namespace Slic3r {

// Forward declarations.
class GCode;

class AvoidCrossingPerimeters {
public:
    
    // this flag triggers the use of the external configuration space
    bool use_external_mp;
    bool use_external_mp_once;  // just for the next travel move
    
    // this flag disables avoid_crossing_perimeters just for the next travel move
    // we enable it by default for the first travel move in print
    bool disable_once;
    
    AvoidCrossingPerimeters() : use_external_mp(false), use_external_mp_once(false), disable_once(true) {}
    ~AvoidCrossingPerimeters() {}

    void init_external_mp(const ExPolygons &islands) { m_external_mp = Slic3r::make_unique<MotionPlanner>(islands); }
    void init_layer_mp(const ExPolygons &islands) { m_layer_mp = Slic3r::make_unique<MotionPlanner>(islands); }

    Polyline travel_to(GCode &gcodegen, Point point);

private:
    std::unique_ptr<MotionPlanner> m_external_mp;
    std::unique_ptr<MotionPlanner> m_layer_mp;
};

class OozePrevention {
public:
    bool enable;
    Points standby_points;
    
    OozePrevention() : enable(false) {}
    std::string pre_toolchange(GCode &gcodegen);
    std::string post_toolchange(GCode &gcodegen);
    
private:
    int _get_temp(GCode &gcodegen);
};

class Wipe {
public:
    bool enable;
    Polyline path;
    
    Wipe() : enable(false) {}
    bool has_path() const { return !this->path.points.empty(); }
    void reset_path() { this->path = Polyline(); }
    std::string wipe(GCode &gcodegen, bool toolchange = false);
};

class GCode {
public:        
    GCode() : 
        m_enable_loop_clipping(true), 
        m_enable_cooling_markers(false), 
        m_enable_extrusion_role_markers(false), 
        m_enable_analyzer_markers(false),
        m_layer_count(0),
        m_layer_index(-1), 
        m_layer(nullptr), 
        m_first_layer(false), 
        m_elapsed_time(0.0), 
        m_volumetric_speed(0),
        m_last_pos_defined(false),
        m_last_extrusion_role(erNone),
        m_brim_done(false),
        m_second_layer_things_done(false),
        m_last_obj_copy(Point(std::numeric_limits<coord_t>::max(), std::numeric_limits<coord_t>::max()))
        {}
    ~GCode() {}

    bool            do_export(FILE *file, Print &print);

    // Exported for the helper classes (OozePrevention, Wipe) and for the Perl binding for unit tests.
    const Pointf&   origin() const { return m_origin; }
    void            set_origin(const Pointf &pointf);
    void            set_origin(const coordf_t x, const coordf_t y) { this->set_origin(Pointf(x, y)); }
    const Point&    last_pos() const { return m_last_pos; }
    Pointf          point_to_gcode(const Point &point) const;
    const FullPrintConfig &config() const { return m_config; }
    const Layer*    layer() const { return m_layer; }
    GCodeWriter&    writer() { return m_writer; }
    bool            enable_cooling_markers() const { return m_enable_cooling_markers; }
    float           get_reset_elapsed_time() { float t = m_elapsed_time; m_elapsed_time = 0.f; return t; }

    // For Perl bindings, to be used exclusively by unit tests.
    unsigned int    layer_count() const { return m_layer_count; }
    void            set_layer_count(unsigned int value) { m_layer_count = value; }
    float           elapsed_time() const { return m_elapsed_time; }
    void            set_elapsed_time(float value) { m_elapsed_time = value; }
    void            apply_print_config(const PrintConfig &print_config);

private:
    void            process_layer(FILE *file, const Print &print, const Layer &layer, const Points &object_copies);

    void            set_last_pos(const Point &pos) { m_last_pos = pos; m_last_pos_defined = true; }
    bool            last_pos_defined() const { return m_last_pos_defined; }
    void            set_extruders(const std::vector<unsigned int> &extruder_ids);
    std::string     preamble();
    std::string     change_layer(const Layer &layer);
    std::string     extrude(const ExtrusionEntity &entity, std::string description = "", double speed = -1);
    std::string     extrude(ExtrusionLoop loop, std::string description = "", double speed = -1);
    std::string     extrude(ExtrusionMultiPath multipath, std::string description = "", double speed = -1);
    std::string     extrude(ExtrusionPath path, std::string description = "", double speed = -1);

    struct ByExtruder
    {
        struct ToExtrude {
            ExtrusionEntityCollection perimeters;
            ExtrusionEntityCollection infills;
        };
        std::vector<ToExtrude> by_region;
    };
    std::string     extrude_perimeters(const Print &print, const std::vector<ByExtruder::ToExtrude> &by_region);
    std::string     extrude_infill(const Print &print, const std::vector<ByExtruder::ToExtrude> &by_region);
    std::string     extrude_support(const ExtrusionEntityCollection &support_fills, unsigned int extruder_id);

    std::string     travel_to(const Point &point, ExtrusionRole role, std::string comment);
    bool            needs_retraction(const Polyline &travel, ExtrusionRole role = erNone);
    std::string     retract(bool toolchange = false);
    std::string     unretract();
    std::string     set_extruder(unsigned int extruder_id);

    /* Origin of print coordinates expressed in unscaled G-code coordinates.
       This affects the input arguments supplied to the extrude*() and travel_to()
       methods. */
    Pointf                              m_origin;
    FullPrintConfig                     m_config;
    GCodeWriter                         m_writer;
    PlaceholderParser                   m_placeholder_parser;
    OozePrevention                      m_ooze_prevention;
    Wipe                                m_wipe;
    AvoidCrossingPerimeters             m_avoid_crossing_perimeters;
    bool                                m_enable_loop_clipping;
    // If enabled, the G-code generator will put following comments at the ends
    // of the G-code lines: _EXTRUDE_SET_SPEED, _WIPE, _BRIDGE_FAN_START, _BRIDGE_FAN_END
    // Those comments are received and consumed (removed from the G-code) by the CoolingBuffer.pm Perl module.
    bool                                m_enable_cooling_markers;
    // Markers for the Pressure Equalizer to recognize the extrusion type.
    // The Pressure Equalizer removes the markers from the final G-code.
    bool                                m_enable_extrusion_role_markers;
    // Extended markers for the G-code Analyzer.
    // The G-code Analyzer will remove these comments from the final G-code.
    bool                                m_enable_analyzer_markers;
    // How many times will change_layer() be called?
    // change_layer() will update the progress bar.
    unsigned int                        m_layer_count;
    // Progress bar indicator. Increments from -1 up to layer_count.
    int                                 m_layer_index;
    // Current layer processed. Insequential printing mode, only a single copy will be printed.
    // In non-sequential mode, all its copies will be printed.
    const Layer*                        m_layer;
    std::map<const PrintObject*,Point>  m_seam_position;
    // Distance Field structure to 
    std::unique_ptr<EdgeGrid::Grid>     m_lower_layer_edge_grid;
    // this flag triggers first layer speeds
    bool                                m_first_layer;
    // Used by the CoolingBuffer G-code filter to calculate time spent per layer change.
    // This value is not quite precise. First it only accouts for extrusion moves and travel moves,
    // it does not account for wipe, retract / unretract moves.
    // second it does not account for the velocity profiles of the printer.
    float                               m_elapsed_time; // seconds
    double                              m_volumetric_speed;
    // Support for the extrusion role markers. Which marker is active?
    ExtrusionRole                       m_last_extrusion_role;

    Point                               m_last_pos;
    bool                                m_last_pos_defined;

    std::unique_ptr<CoolingBuffer>      m_cooling_buffer;
    std::unique_ptr<SpiralVase>         m_spiral_vase;
    std::unique_ptr<PressureEqualizer>  m_pressure_equalizer;

    // Heights at which the skirt has already been extruded.
    std::set<coordf_t>                  m_skirt_done;
    // Has the brim been extruded already? Brim is being extruded only for the first object of a multi-object print.
    bool                                m_brim_done;
    // Flag indicating whether the nozzle temperature changes from 1st to 2nd layer were performed.
    bool                                m_second_layer_things_done;
    // Index of a last object copy extruded. -1 for not set yet.
    Point                               m_last_obj_copy;

    std::string _extrude(const ExtrusionPath &path, std::string description = "", double speed = -1);
    void _print_first_layer_extruder_temperatures(FILE *file, Print &print, bool wait);

    std::string filter(std::string &&gcode, bool flush);
};

}

#endif
