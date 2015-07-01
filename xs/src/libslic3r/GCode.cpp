#include "GCode.hpp"
#include "ExtrusionEntity.hpp"

namespace Slic3r {

AvoidCrossingPerimeters::AvoidCrossingPerimeters()
    : use_external_mp(false), use_external_mp_once(false), disable_once(true),
        _external_mp(NULL), _layer_mp(NULL)
{
}

AvoidCrossingPerimeters::~AvoidCrossingPerimeters()
{
    if (this->_external_mp != NULL)
        delete this->_external_mp;
    
    if (this->_layer_mp != NULL)
        delete this->_layer_mp;
}

void
AvoidCrossingPerimeters::init_external_mp(const ExPolygons &islands)
{
    if (this->_external_mp != NULL)
        delete this->_external_mp;
    
    this->_external_mp = new MotionPlanner(islands);
}

void
AvoidCrossingPerimeters::init_layer_mp(const ExPolygons &islands)
{
    if (this->_layer_mp != NULL)
        delete this->_layer_mp;
    
    this->_layer_mp = new MotionPlanner(islands);
}

Polyline
AvoidCrossingPerimeters::travel_to(GCode &gcodegen, Point point)
{
    if (this->use_external_mp || this->use_external_mp_once) {
        // get current origin set in gcodegen
        // (the one that will be used to translate the G-code coordinates by)
        Point scaled_origin = Point::new_scale(gcodegen.origin.x, gcodegen.origin.y);
        
        // represent last_pos in absolute G-code coordinates
        Point last_pos = gcodegen.last_pos();
        last_pos.translate(scaled_origin);
        
        // represent point in absolute G-code coordinates
        point.translate(scaled_origin);
        
        // calculate path
        Polyline travel = this->_external_mp->shortest_path(last_pos, point);
        
        // translate the path back into the shifted coordinate system that gcodegen
        // is currently using for writing coordinates
        travel.translate(scaled_origin.negative());
        return travel;
    } else {
        return this->_layer_mp->shortest_path(gcodegen.last_pos(), point);
    }
}

#ifdef SLIC3RXS
REGISTER_CLASS(AvoidCrossingPerimeters, "GCode::AvoidCrossingPerimeters");
#endif

OozePrevention::OozePrevention()
    : enable(false)
{
}

#ifdef SLIC3RXS
REGISTER_CLASS(OozePrevention, "GCode::OozePrevention");
#endif

Wipe::Wipe()
    : enable(false)
{
}

bool
Wipe::has_path()
{
    return !this->path.points.empty();
}

void
Wipe::reset_path()
{
    this->path = Polyline();
}

std::string
Wipe::wipe(GCode &gcodegen, bool toolchange)
{
    std::string gcode;
    
    /*  Reduce feedrate a bit; travel speed is often too high to move on existing material.
        Too fast = ripping of existing material; too slow = short wipe path, thus more blob.  */
    double wipe_speed = gcodegen.writer.config.travel_speed.value * 0.8;
    
    // get the retraction length
    double length = toolchange
        ? gcodegen.writer.extruder()->retract_length_toolchange()
        : gcodegen.writer.extruder()->retract_length();
    
    if (length > 0) {
        /*  Calculate how long we need to travel in order to consume the required
            amount of retraction. In other words, how far do we move in XY at wipe_speed
            for the time needed to consume retract_length at retract_speed?  */
        double wipe_dist = scale_(length / gcodegen.writer.extruder()->retract_speed() * wipe_speed);
    
        /*  Take the stored wipe path and replace first point with the current actual position
            (they might be different, for example, in case of loop clipping).  */
        Polyline wipe_path;
        wipe_path.append(gcodegen.last_pos());
        wipe_path.append(
            this->path.points.begin() + 1,
            this->path.points.end()
        );
        
        wipe_path.clip_end(wipe_path.length() - wipe_dist);
    
        // subdivide the retraction in segments
        double retracted = 0;
        Lines lines = wipe_path.lines();
        for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line) {
            double segment_length = line->length();
            /*  Reduce retraction length a bit to avoid effective retraction speed to be greater than the configured one
                due to rounding (TODO: test and/or better math for this)  */
            double dE = length * (segment_length / wipe_dist) * 0.95;
            gcode += gcodegen.writer.set_speed(wipe_speed*60);
            gcode += gcodegen.writer.extrude_to_xy(
                gcodegen.point_to_gcode(line->b),
                -dE,
                (std::string)"wipe and retract" + (gcodegen.enable_cooling_markers ? ";_WIPE" : "")
            );
            retracted += dE;
        }
        gcodegen.writer.extruder()->retracted += retracted;
        
        // prevent wiping again on same path
        this->reset_path();
    }
    
    return gcode;
}

#ifdef SLIC3RXS
REGISTER_CLASS(Wipe, "GCode::Wipe");
#endif

GCode::GCode()
    : enable_loop_clipping(true), enable_cooling_markers(false), layer_count(0),
        layer_index(-1), first_layer(false), elapsed_time(0), volumetric_speed(0),
        _last_pos_defined(false)
{
}

Point&
GCode::last_pos()
{
    return this->_last_pos;
}

void
GCode::set_last_pos(const Point &pos)
{
    this->_last_pos = pos;
    this->_last_pos_defined = true;
}

bool
GCode::last_pos_defined() const
{
    return this->_last_pos_defined;
}

void
GCode::apply_print_config(const PrintConfig &print_config)
{
    this->writer.apply_print_config(print_config);
    this->config.apply(print_config);
}

void
GCode::set_origin(const Pointf &pointf)
{    
    // if origin increases (goes towards right), last_pos decreases because it goes towards left
    Point translate(
        scale_(this->origin.x - pointf.x),
        scale_(this->origin.y - pointf.y)
    );
    this->_last_pos.translate(translate);
    this->wipe.path.translate(translate);
    
    this->origin = pointf;
}

std::string
GCode::preamble()
{
    std::string gcode = this->writer.preamble();
    
    /*  Perform a *silent* move to z_offset: we need this to initialize the Z
        position of our writer object so that any initial lift taking place
        before the first layer change will raise the extruder from the correct
        initial Z instead of 0.  */
    this->writer.travel_to_z(this->config.z_offset.value);
    
    return gcode;
}

bool
GCode::needs_retraction(const Polyline &travel, ExtrusionRole role)
{
    if (travel.length() < scale_(this->config.retract_before_travel.get_at(this->writer.extruder()->id))) {
        // skip retraction if the move is shorter than the configured threshold
        return false;
    }
    
    if (role == erSupportMaterial) {
        SupportLayer* support_layer = dynamic_cast<SupportLayer*>(this->layer);
        if (support_layer->support_islands.contains(travel)) {
            // skip retraction if this is a travel move inside a support material island
            return false;
        }
    }
    
    if (this->config.only_retract_when_crossing_perimeters && this->layer != NULL) {
        if (this->config.fill_density.value > 0
            && this->layer->any_internal_region_slice_contains(travel)) {
            /*  skip retraction if travel is contained in an internal slice *and*
                internal infill is enabled (so that stringing is entirely not visible)  */
            return false;
        } else if (this->layer->any_bottom_region_slice_contains(travel)
            && this->layer->upper_layer != NULL
            && this->layer->upper_layer->slices.contains(travel)
            && (this->config.bottom_solid_layers.value >= 2 || this->config.fill_density.value > 0)) {
            /*  skip retraction if travel is contained in an *infilled* bottom slice
                but only if it's also covered by an *infilled* upper layer's slice
                so that it's not visible from above (a bottom surface might not have an
                upper slice in case of a thin membrane)  */
            return false;
        }
    }
    
    // retract if only_retract_when_crossing_perimeters is disabled or doesn't apply
    return true;
}

std::string
GCode::retract(bool toolchange)
{
    std::string gcode;
    
    if (this->writer.extruder() == NULL)
        return gcode;
    
    // wipe (if it's enabled for this extruder and we have a stored wipe path)
    if (this->config.wipe.get_at(this->writer.extruder()->id) && this->wipe.has_path()) {
        gcode += this->wipe.wipe(*this, toolchange);
    }
    
    /*  The parent class will decide whether we need to perform an actual retraction
        (the extruder might be already retracted fully or partially). We call these 
        methods even if we performed wipe, since this will ensure the entire retraction
        length is honored in case wipe path was too short.  */
    gcode += toolchange ? this->writer.retract_for_toolchange() : this->writer.retract();
    
    gcode += this->writer.reset_e();
    if (this->writer.extruder()->retract_length() > 0 || this->config.use_firmware_retraction)
        gcode += this->writer.lift();
    
    return gcode;
}

std::string
GCode::unretract()
{
    std::string gcode;
    gcode += this->writer.unlift();
    gcode += this->writer.unretract();
    return gcode;
}

// convert a model-space scaled point into G-code coordinates
Pointf
GCode::point_to_gcode(const Point &point)
{
    Pointf extruder_offset = this->config.extruder_offset.get_at(this->writer.extruder()->id);
    return Pointf(
        unscale(point.x) + this->origin.x - extruder_offset.x,
        unscale(point.y) + this->origin.y - extruder_offset.y
    );
}

#ifdef SLIC3RXS
REGISTER_CLASS(GCode, "GCode");
#endif

}
