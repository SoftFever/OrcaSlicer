#include "GCode.hpp"
#include "ExtrusionEntity.hpp"
#include <algorithm>
#include <cstdlib>

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

std::string
OozePrevention::pre_toolchange(GCode &gcodegen)
{
    std::string gcode;
    
    // move to the nearest standby point
    if (!this->standby_points.empty()) {
        // get current position in print coordinates
        Pointf3 writer_pos = gcodegen.writer.get_position();
        Point pos = Point::new_scale(writer_pos.x, writer_pos.y);
        
        // find standby point
        Point standby_point;
        pos.nearest_point(this->standby_points, &standby_point);
        
        /*  We don't call gcodegen.travel_to() because we don't need retraction (it was already
            triggered by the caller) nor avoid_crossing_perimeters and also because the coordinates
            of the destination point must not be transformed by origin nor current extruder offset.  */
        gcode += gcodegen.writer.travel_to_xy(Pointf::new_unscale(standby_point), 
            "move to standby position");
    }
    
    if (gcodegen.config.standby_temperature_delta.value != 0) {
        // we assume that heating is always slower than cooling, so no need to block
        gcode += gcodegen.writer.set_temperature
            (this->_get_temp(gcodegen) + gcodegen.config.standby_temperature_delta.value, false);
    }
    
    return gcode;
}

std::string
OozePrevention::post_toolchange(GCode &gcodegen)
{
    std::string gcode;
    
    if (gcodegen.config.standby_temperature_delta.value != 0) {
        gcode += gcodegen.writer.set_temperature(this->_get_temp(gcodegen), true);
    }
    
    return gcode;
}

int
OozePrevention::_get_temp(GCode &gcodegen)
{
    return (gcodegen.layer != NULL && gcodegen.layer->id() == 0)
        ? gcodegen.config.first_layer_temperature.get_at(gcodegen.writer.extruder()->id)
        : gcodegen.config.temperature.get_at(gcodegen.writer.extruder()->id);
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

#define EXTRUDER_CONFIG(OPT) this->config.OPT.get_at(this->writer.extruder()->id)

GCode::GCode()
    : enable_loop_clipping(true), enable_cooling_markers(false), layer_count(0),
        layer_index(-1), first_layer(false), elapsed_time(0), volumetric_speed(0),
        _last_pos_defined(false), layer(NULL), placeholder_parser(NULL)
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
GCode::set_extruders(const std::vector<unsigned int> &extruder_ids)
{
    this->writer.set_extruders(extruder_ids);
    
    // enable wipe path generation if any extruder has wipe enabled
    this->wipe.enable = false;
    for (std::vector<unsigned int>::const_iterator it = extruder_ids.begin();
        it != extruder_ids.end(); ++it) {
        if (this->config.wipe.get_at(*it)) {
            this->wipe.enable = true;
            break;
        }
    }
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

std::string
GCode::change_layer(const Layer &layer)
{
    this->layer = &layer;
    this->layer_index++;
    this->first_layer = (layer.id() == 0);
    
    // avoid computing islands and overhangs if they're not needed
    if (this->config.avoid_crossing_perimeters) {
        ExPolygons islands;
        union_(layer.slices, &islands, true);
        this->avoid_crossing_perimeters.init_layer_mp(islands);
    }
    
    std::string gcode;
    if (this->layer_count > 0) {
        gcode += this->writer.update_progress(this->layer_index, this->layer_count);
    }
    
    coordf_t z = layer.print_z + this->config.z_offset.value;  // in unscaled coordinates
    if (EXTRUDER_CONFIG(retract_layer_change) && this->writer.will_move_z(z)) {
        gcode += this->retract();
    }
    {
        std::ostringstream comment;
        comment << "move to next layer (" << this->layer_index << ")";
        gcode += this->writer.travel_to_z(z, comment.str());
    }
    
    // forget last wiping path as wiping after raising Z is pointless
    this->wipe.reset_path();
    
    return gcode;
}

std::string
GCode::extrude(ExtrusionLoop loop, std::string description, double speed)
{
    // get a copy; don't modify the orientation of the original loop object otherwise
    // next copies (if any) would not detect the correct orientation
    
    // extrude all loops ccw
    bool was_clockwise = loop.make_counter_clockwise();
    
    // find the point of the loop that is closest to the current extruder position
    // or randomize if requested
    Point last_pos = this->last_pos();
    if (this->config.spiral_vase) {
        loop.split_at(last_pos);
    } else if (this->config.seam_position == spNearest || this->config.seam_position == spAligned) {
        Polygon polygon = loop.polygon();
        
        // simplify polygon in order to skip false positives in concave/convex detection
        // (loop is always ccw as polygon.simplify() only works on ccw polygons)
        Polygons simplified = polygon.simplify(scale_(EXTRUDER_CONFIG(nozzle_diameter))/2);
        
        // restore original winding order so that concave and convex detection always happens
        // on the right/outer side of the polygon
        if (was_clockwise) {
            for (Polygons::iterator p = simplified.begin(); p != simplified.end(); ++p)
                p->reverse();
        }
        
        // concave vertices have priority
        Points candidates;
        for (Polygons::const_iterator p = simplified.begin(); p != simplified.end(); ++p) {
            Points concave = p->concave_points(PI*4/3);
            candidates.insert(candidates.end(), concave.begin(), concave.end());
        }
        
        // if no concave points were found, look for convex vertices
        if (candidates.empty()) {
            for (Polygons::const_iterator p = simplified.begin(); p != simplified.end(); ++p) {
                Points convex = p->convex_points(PI*2/3);
                candidates.insert(candidates.end(), convex.begin(), convex.end());
            }
        }
        
        // retrieve the last start position for this object
        if (this->layer != NULL && this->_seam_position.count(this->layer->object()) > 0) {
            last_pos = this->_seam_position[this->layer->object()];
        }
        
        Point point;
        if (this->config.seam_position == spNearest) {
            if (candidates.empty()) candidates = polygon.points;
            last_pos.nearest_point(candidates, &point);
            
            // On 32-bit Linux, Clipper will change some point coordinates by 1 unit
            // while performing simplify_polygons(), thus split_at_vertex() won't 
            // find them anymore.
            if (!loop.split_at_vertex(point)) loop.split_at(point);
        } else if (!candidates.empty()) {
            Points non_overhang;
            for (Points::const_iterator p = candidates.begin(); p != candidates.end(); ++p) {
                if (!loop.has_overhang_point(*p))
                    non_overhang.push_back(*p);
            }
            
            if (!non_overhang.empty())
                candidates = non_overhang;
            
            last_pos.nearest_point(candidates, &point);
            if (!loop.split_at_vertex(point)) loop.split_at(point);  // see note above
        } else {
            point = last_pos.projection_onto(polygon);
            loop.split_at(point);
        }
        if (this->layer != NULL)
            this->_seam_position[this->layer->object()] = point;
    } else if (this->config.seam_position == spRandom) {
        if (loop.role == elrContourInternalPerimeter) {
            Polygon polygon = loop.polygon();
            Point centroid = polygon.centroid();
            last_pos = Point(polygon.bounding_box().max.x, centroid.y);
            last_pos.rotate(rand() % 2*PI, centroid);
        }
        loop.split_at(last_pos);
    }
    
    // clip the path to avoid the extruder to get exactly on the first point of the loop;
    // if polyline was shorter than the clipping distance we'd get a null polyline, so
    // we discard it in that case
    double clip_length = this->enable_loop_clipping
        ? scale_(EXTRUDER_CONFIG(nozzle_diameter)) * LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER
        : 0;
    
    // get paths
    ExtrusionPaths paths;
    loop.clip_end(clip_length, &paths);
    if (paths.empty()) return "";
    
    // apply the small perimeter speed
    if (paths.front().is_perimeter() && loop.length() <= SMALL_PERIMETER_LENGTH) {
        if (speed == -1) speed = this->config.get_abs_value("small_perimeter_speed");
    }
    
    // extrude along the path
    std::string gcode;
    for (ExtrusionPaths::const_iterator path = paths.begin(); path != paths.end(); ++path)
        gcode += this->_extrude(*path, description, speed);
    
    // reset acceleration
    gcode += this->writer.set_acceleration(this->config.default_acceleration.value);
    
    if (this->wipe.enable)
        this->wipe.path = paths.front().polyline;  // TODO: don't limit wipe to last path
    
    // make a little move inwards before leaving loop
    if (paths.back().role == erExternalPerimeter && this->layer != NULL && this->config.perimeters > 1) {
        Polyline &last_path_polyline = paths.back().polyline;
        // detect angle between last and first segment
        // the side depends on the original winding order of the polygon (left for contours, right for holes)
        Point a = paths.front().polyline.points[1];  // second point
        Point b = *(paths.back().polyline.points.end()-3);       // second to last point
        if (was_clockwise) {
            // swap points
            Point c = a; a = b; b = c;
        }
        
        double angle = paths.front().first_point().ccw_angle(a, b) / 3;
        
        // turn left if contour, turn right if hole
        if (was_clockwise) angle *= -1;
        
        // create the destination point along the first segment and rotate it
        // we make sure we don't exceed the segment length because we don't know
        // the rotation of the second segment so we might cross the object boundary
        Line first_segment(
            paths.front().polyline.points[0],
            paths.front().polyline.points[1]
        );
        double distance = std::min(
            scale_(EXTRUDER_CONFIG(nozzle_diameter)),
            first_segment.length()
        );
        Point point = first_segment.point_at(distance);
        point.rotate(angle, first_segment.a);
        
        // generate the travel move
        gcode += this->writer.travel_to_xy(this->point_to_gcode(point), "move inwards before travel");
    }
    
    return gcode;
}

std::string
GCode::extrude(const ExtrusionEntity &entity, std::string description, double speed)
{
    if (const ExtrusionPath* path = dynamic_cast<const ExtrusionPath*>(&entity)) {
        return this->extrude(*path, description, speed);
    } else if (const ExtrusionLoop* loop = dynamic_cast<const ExtrusionLoop*>(&entity)) {
        return this->extrude(*loop, description, speed);
    } else {
        CONFESS("Invalid argument supplied to extrude()");
        return "";
    }
}

std::string
GCode::extrude(const ExtrusionPath &path, std::string description, double speed)
{
    std::string gcode = this->_extrude(path, description, speed);
    
    // reset acceleration
    gcode += this->writer.set_acceleration(this->config.default_acceleration.value);
    
    return gcode;
}

std::string
GCode::_extrude(ExtrusionPath path, std::string description, double speed)
{
    path.simplify(SCALED_RESOLUTION);
    
    std::string gcode;
    
    // go to first point of extrusion path
    if (!this->_last_pos_defined || !this->_last_pos.coincides_with(path.first_point())) {
        gcode += this->travel_to(
            path.first_point(),
            path.role,
            "move to first " + description + " point"
        );
    }
    
    // compensate retraction
    gcode += this->unretract();
    
    // adjust acceleration
    {
        double acceleration;
        if (this->config.first_layer_acceleration.value > 0 && this->first_layer) {
            acceleration = this->config.first_layer_acceleration.value;
        } else if (this->config.perimeter_acceleration.value > 0 && path.is_perimeter()) {
            acceleration = this->config.perimeter_acceleration.value;
        } else if (this->config.bridge_acceleration.value > 0 && path.is_bridge()) {
            acceleration = this->config.bridge_acceleration.value;
        } else if (this->config.infill_acceleration.value > 0 && path.is_infill()) {
            acceleration = this->config.infill_acceleration.value;
        } else {
            acceleration = this->config.default_acceleration.value;
        }
        gcode += this->writer.set_acceleration(acceleration);
    }
    
    // calculate extrusion length per distance unit
    double e_per_mm = this->writer.extruder()->e_per_mm3 * path.mm3_per_mm;
    if (this->writer.extrusion_axis().empty()) e_per_mm = 0;
    
    // set speed
    if (speed == -1) {
        if (path.role == erPerimeter) {
            speed = this->config.get_abs_value("perimeter_speed");
        } else if (path.role == erExternalPerimeter) {
            speed = this->config.get_abs_value("external_perimeter_speed");
        } else if (path.role == erOverhangPerimeter || path.role == erBridgeInfill) {
            speed = this->config.get_abs_value("bridge_speed");
        } else if (path.role == erInternalInfill) {
            speed = this->config.get_abs_value("infill_speed");
        } else if (path.role == erSolidInfill) {
            speed = this->config.get_abs_value("solid_infill_speed");
        } else if (path.role == erTopSolidInfill) {
            speed = this->config.get_abs_value("top_solid_infill_speed");
        } else if (path.role == erGapFill) {
            speed = this->config.get_abs_value("gap_fill_speed");
        } else {
            CONFESS("Invalid speed");
        }
    }
    if (this->first_layer) {
        speed = this->config.get_abs_value("first_layer_speed", speed);
    }
    if (this->volumetric_speed != 0 && speed == 0) {
        speed = this->volumetric_speed / path.mm3_per_mm;
    }
    if (this->config.max_volumetric_speed.value > 0) {
        // cap speed with max_volumetric_speed anyway (even if user is not using autospeed)
        speed = std::min(
            speed,
            this->config.max_volumetric_speed.value / path.mm3_per_mm
        );
    }
    double F = speed * 60;  // convert mm/sec to mm/min
    
    // extrude arc or line
    if (path.is_bridge() && this->enable_cooling_markers)
        gcode += ";_BRIDGE_FAN_START\n";
    gcode += this->writer.set_speed(F);
    double path_length = 0;
    {
        std::string comment = this->config.gcode_comments ? (" ; " + description) : "";
        Lines lines = path.polyline.lines();
        for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line) {
            const double line_length = line->length() * SCALING_FACTOR;
            path_length += line_length;
            
            gcode += this->writer.extrude_to_xy(
                this->point_to_gcode(line->b),
                e_per_mm * line_length,
                comment
            );
        }
    }
    if (this->wipe.enable) {
        this->wipe.path = path.polyline;
        this->wipe.path.reverse();
    }
    if (path.is_bridge() && this->enable_cooling_markers)
        gcode += ";_BRIDGE_FAN_END\n";
    
    this->set_last_pos(path.last_point());
    
    if (this->config.cooling)
        this->elapsed_time += path_length / F * 60;
    
    return gcode;
}

// This method accepts &point in print coordinates.
std::string
GCode::travel_to(const Point &point, ExtrusionRole role, std::string comment)
{    
    /*  Define the travel move as a line between current position and the taget point.
        This is expressed in print coordinates, so it will need to be translated by
        this->origin in order to get G-code coordinates.  */
    Polyline travel;
    travel.append(this->last_pos());
    travel.append(point);
    
    // check whether a straight travel move would need retraction
    bool needs_retraction = this->needs_retraction(travel, role);
    
    // if a retraction would be needed, try to use avoid_crossing_perimeters to plan a
    // multi-hop travel path inside the configuration space
    if (needs_retraction
        && this->config.avoid_crossing_perimeters
        && !this->avoid_crossing_perimeters.disable_once) {
        travel = this->avoid_crossing_perimeters.travel_to(*this, point);
        
        // check again whether the new travel path still needs a retraction
        needs_retraction = this->needs_retraction(travel, role);
    }
    
    // Re-allow avoid_crossing_perimeters for the next travel moves
    this->avoid_crossing_perimeters.disable_once = false;
    this->avoid_crossing_perimeters.use_external_mp_once = false;
    
    // generate G-code for the travel move
    std::string gcode;
    if (needs_retraction) gcode += this->retract();
    
    // use G1 because we rely on paths being straight (G0 may make round paths)
    Lines lines = travel.lines();
    for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line)
        gcode += this->writer.travel_to_xy(this->point_to_gcode(line->b), comment);
    
    return gcode;
}

bool
GCode::needs_retraction(const Polyline &travel, ExtrusionRole role)
{
    if (travel.length() < scale_(EXTRUDER_CONFIG(retract_before_travel))) {
        // skip retraction if the move is shorter than the configured threshold
        return false;
    }
    
    if (role == erSupportMaterial) {
        const SupportLayer* support_layer = dynamic_cast<const SupportLayer*>(this->layer);
        if (support_layer != NULL && support_layer->support_islands.contains(travel)) {
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
    if (EXTRUDER_CONFIG(wipe) && this->wipe.has_path()) {
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

std::string
GCode::set_extruder(unsigned int extruder_id)
{
    this->placeholder_parser->set("current_extruder", extruder_id);
    if (!this->writer.need_toolchange(extruder_id))
        return "";
    
    // if we are running a single-extruder setup, just set the extruder and return nothing
    if (!this->writer.multiple_extruders) {
        return this->writer.toolchange(extruder_id);
    }
    
    // prepend retraction on the current extruder
    std::string gcode = this->retract(true);
    
    // append custom toolchange G-code
    if (this->writer.extruder() != NULL && !this->config.toolchange_gcode.value.empty()) {
        PlaceholderParser pp = *this->placeholder_parser;
        pp.set("previous_extruder", this->writer.extruder()->id);
        pp.set("next_extruder",     extruder_id);
        gcode += pp.process(this->config.toolchange_gcode.value) + '\n';
    }
    
    // if ooze prevention is enabled, park current extruder in the nearest
    // standby point and set it to the standby temperature
    if (this->ooze_prevention.enable && this->writer.extruder() != NULL)
        gcode += this->ooze_prevention.pre_toolchange(*this);
    
    // append the toolchange command
    gcode += this->writer.toolchange(extruder_id);
    
    // set the new extruder to the operating temperature
    if (this->ooze_prevention.enable)
        gcode += this->ooze_prevention.post_toolchange(*this);
    
    return gcode;
}

// convert a model-space scaled point into G-code coordinates
Pointf
GCode::point_to_gcode(const Point &point)
{
    Pointf extruder_offset = EXTRUDER_CONFIG(extruder_offset);
    return Pointf(
        unscale(point.x) + this->origin.x - extruder_offset.x,
        unscale(point.y) + this->origin.y - extruder_offset.y
    );
}

#ifdef SLIC3RXS
REGISTER_CLASS(GCode, "GCode");
#endif

}
