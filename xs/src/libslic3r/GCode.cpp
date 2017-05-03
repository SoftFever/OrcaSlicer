#include "GCode.hpp"
#include "ExtrusionEntity.hpp"
#include "EdgeGrid.hpp"
#include "Geometry.hpp"

#include <algorithm>
#include <cstdlib>
#include <math.h>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/date_time/local_time/local_time.hpp>
#include <boost/foreach.hpp>

#include "SVG.hpp"

#if 0
// Enable debugging and asserts, even in the release build.
#define DEBUG
#define _DEBUG
#undef NDEBUG
#endif

#include <assert.h>

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
        Point scaled_origin = Point::new_scale(gcodegen.origin().x, gcodegen.origin().y);
        
        // represent last_pos in absolute G-code coordinates
        Point last_pos = gcodegen.last_pos();
        last_pos.translate(scaled_origin);
        
        // represent point in absolute G-code coordinates
        point.translate(scaled_origin);
        
        // calculate path
        Polyline travel = this->_external_mp->shortest_path(last_pos, point);
        //exit(0);
        // translate the path back into the shifted coordinate system that gcodegen
        // is currently using for writing coordinates
        travel.translate(scaled_origin.negative());
        return travel;
    } else {
        return this->_layer_mp->shortest_path(gcodegen.last_pos(), point);
    }
}

std::string OozePrevention::pre_toolchange(GCode &gcodegen)
{
    std::string gcode;
    
    // move to the nearest standby point
    if (!this->standby_points.empty()) {
        // get current position in print coordinates
        Pointf3 writer_pos = gcodegen.writer().get_position();
        Point pos = Point::new_scale(writer_pos.x, writer_pos.y);
        
        // find standby point
        Point standby_point;
        pos.nearest_point(this->standby_points, &standby_point);
        
        /*  We don't call gcodegen.travel_to() because we don't need retraction (it was already
            triggered by the caller) nor avoid_crossing_perimeters and also because the coordinates
            of the destination point must not be transformed by origin nor current extruder offset.  */
        gcode += gcodegen.writer().travel_to_xy(Pointf::new_unscale(standby_point), 
            "move to standby position");
    }
    
    if (gcodegen.config().standby_temperature_delta.value != 0) {
        // we assume that heating is always slower than cooling, so no need to block
        gcode += gcodegen.writer().set_temperature
            (this->_get_temp(gcodegen) + gcodegen.config().standby_temperature_delta.value, false);
    }
    
    return gcode;
}

std::string OozePrevention::post_toolchange(GCode &gcodegen)
{
    return (gcodegen.config().standby_temperature_delta.value != 0) ?
        gcodegen.writer().set_temperature(this->_get_temp(gcodegen), true) :
        std::string();
}

int
OozePrevention::_get_temp(GCode &gcodegen)
{
    return (gcodegen.layer() != NULL && gcodegen.layer()->id() == 0)
        ? gcodegen.config().first_layer_temperature.get_at(gcodegen.writer().extruder()->id)
        : gcodegen.config().temperature.get_at(gcodegen.writer().extruder()->id);
}

std::string
Wipe::wipe(GCode &gcodegen, bool toolchange)
{
    std::string gcode;
    
    /*  Reduce feedrate a bit; travel speed is often too high to move on existing material.
        Too fast = ripping of existing material; too slow = short wipe path, thus more blob.  */
    double wipe_speed = gcodegen.writer().config.travel_speed.value * 0.8;
    
    // get the retraction length
    double length = toolchange
        ? gcodegen.writer().extruder()->retract_length_toolchange()
        : gcodegen.writer().extruder()->retract_length();
    
    if (length > 0) {
        /*  Calculate how long we need to travel in order to consume the required
            amount of retraction. In other words, how far do we move in XY at wipe_speed
            for the time needed to consume retract_length at retract_speed?  */
        double wipe_dist = scale_(length / gcodegen.writer().extruder()->retract_speed() * wipe_speed);
    
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
            //FIXME one shall not generate the unnecessary G1 Fxxx commands, here wipe_speed is a constant inside this cycle.
            // Is it here for the cooling markers? Or should it be outside of the cycle?
            gcode += gcodegen.writer().set_speed(wipe_speed*60, "", gcodegen.enable_cooling_markers() ? ";_WIPE" : "");
            gcode += gcodegen.writer().extrude_to_xy(
                gcodegen.point_to_gcode(line->b),
                -dE,
                "wipe and retract"
            );
            retracted += dE;
        }
        gcodegen.writer().extruder()->retracted += retracted;
        
        // prevent wiping again on same path
        this->reset_path();
    }
    
    return gcode;
}

#define EXTRUDER_CONFIG(OPT) m_config.OPT.get_at(m_writer.extruder()->id)

inline void write(FILE *file, const std::string &what)
{
    fwrite(what.data(), 1, what.size(), file);
}

inline void writeln(FILE *file, const std::string &what)
{
    write(file, what);
    fprintf(file, "\n");
}

// Older compilers do not provide a std::make_unique template. Provide a simple one.
template<typename T, typename... Args>
std::unique_ptr<T> make_unique(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

bool GCode::do_export(FILE *file, Print &print)
{
    // How many times will be change_layer() called?
    // change_layer() in turn increments the progress bar status.
    m_layer_count = 0;
    for (auto object : print.objects)
        // if sequential printing is not enable, all copies of the same object share the same layer change command(s)
        m_layer_count += (unsigned int)((print.config.complete_objects.value ? object->copies().size() : 1) * object->total_layer_count());

    m_enable_cooling_markers = true;
    this->apply_print_config(print.config);
    this->set_extruders(print.extruders());
    
    // Initialize autospeed.
    {
        // get the minimum cross-section used in the print
        std::vector<double> mm3_per_mm;
        for (auto object : print.objects) {
            for (size_t region_id = 0; region_id < print.regions.size(); ++ region_id) {
                auto region = print.regions[region_id];
                for (auto layer : object->layers) {
                    auto layerm = layer->regions[region_id];
                    if (region->config.get_abs_value("perimeter_speed"          ) == 0 || 
                        region->config.get_abs_value("small_perimeter_speed"    ) == 0 || 
                        region->config.get_abs_value("external_perimeter_speed" ) == 0 || 
                        region->config.get_abs_value("bridge_speed"             ) == 0)
                        mm3_per_mm.push_back(layerm->perimeters.min_mm3_per_mm());
                    if (region->config.get_abs_value("infill_speed"             ) == 0 || 
                        region->config.get_abs_value("solid_infill_speed"       ) == 0 || 
                        region->config.get_abs_value("top_solid_infill_speed"   ) == 0 || 
                        region->config.get_abs_value("bridge_speed"             ) == 0)
                        mm3_per_mm.push_back(layerm->fills.min_mm3_per_mm());
                }
            }
            if (object->config.get_abs_value("support_material_speed"           ) == 0 || 
                object->config.get_abs_value("support_material_interface_speed" ) == 0)
                for (auto layer : object->support_layers)
                    mm3_per_mm.push_back(layer->support_fills.min_mm3_per_mm());
        }
        // filter out 0-width segments
        mm3_per_mm.erase(std::remove_if(mm3_per_mm.begin(), mm3_per_mm.end(), [](double v) { return v < 0.000001; }), mm3_per_mm.end());
        if (! mm3_per_mm.empty()) {
            // In order to honor max_print_speed we need to find a target volumetric
            // speed that we can use throughout the print. So we define this target 
            // volumetric speed as the volumetric speed produced by printing the 
            // smallest cross-section at the maximum speed: any larger cross-section
            // will need slower feedrates.
            m_volumetric_speed = *std::min_element(mm3_per_mm.begin(), mm3_per_mm.end()) * print.config.max_print_speed.value;
            // limit such volumetric speed with max_volumetric_speed if set
            if (print.config.max_volumetric_speed.value > 0)
                m_volumetric_speed = std::min(m_volumetric_speed, print.config.max_volumetric_speed.value);
        }
    }
    
    m_cooling_buffer = make_unique<CoolingBuffer>(*this);
    if (print.config.spiral_vase.value)
        m_spiral_vase = make_unique<SpiralVase>(print.config);
    if (print.config.max_volumetric_extrusion_rate_slope_positive.value > 0 ||
        print.config.max_volumetric_extrusion_rate_slope_negative.value > 0)
        m_pressure_equalizer = make_unique<PressureEqualizer>(&print.config);
    m_enable_extrusion_role_markers = (bool)m_pressure_equalizer;

    // Write information on the generator.
    {
        const auto now = boost::posix_time::second_clock::local_time();
        const auto date = now.date();
        fprintf(file, "; generated by Slic3r %s on %04d-%02d-%02d at %02d:%02d:%02d\n\n",
            SLIC3R_VERSION,
            // Local date in an ANSII format.
            int(now.date().year()), int(now.date().month()), int(now.date().day()),
            int(now.time_of_day().hours()), int(now.time_of_day().minutes()), int(now.time_of_day().seconds()));
    }
    // Write notes (content of the Print Settings tab -> Notes)
    {
        std::list<std::string> lines;
        boost::split(lines, print.config.notes.value, boost::is_any_of("\n"), boost::token_compress_off);
        for (auto line : lines) {
            // Remove the trailing '\r' from the '\r\n' sequence.
            if (! line.empty() && line.back() == '\r')
                line.pop_back();
            fprintf(file, "; %s\n", line.c_str());
        }
        if (! lines.empty())
            fprintf(file, "\n");
    }
    // Write some terse information on the slicing parameters.
    {
        const PrintObject *first_object = print.objects.front();
        const double       layer_height = first_object->config.layer_height.value;
        for (size_t region_id = 0; region_id < print.regions.size(); ++ region_id) {
            auto region = print.regions[region_id];
            fprintf(file, "; external perimeters extrusion width = %.2fmm\n", region->flow(frExternalPerimeter, layer_height, false, false, -1., *first_object).width);
            fprintf(file, "; perimeters extrusion width = %.2fmm\n",          region->flow(frPerimeter,         layer_height, false, false, -1., *first_object).width);
            fprintf(file, "; infill extrusion width = %.2fmm\n",              region->flow(frInfill,            layer_height, false, false, -1., *first_object).width);
            fprintf(file, "; solid infill extrusion width = %.2fmm\n",        region->flow(frSolidInfill,       layer_height, false, false, -1., *first_object).width);
            fprintf(file, "; top infill extrusion width = %.2fmm\n",          region->flow(frTopSolidInfill,    layer_height, false, false, -1., *first_object).width);
            if (print.has_support_material())
                fprintf(file, "; support material extrusion width = %.2fmm\n", support_material_flow(first_object).width);
            if (print.config.first_layer_extrusion_width.value > 0)
                fprintf(file, "; first layer extrusion width = %.2fmm\n",   region->flow(frPerimeter, layer_height, false, true, -1., *first_object).width);
            fprintf(file, "\n");
        }
    }
    
    // Prepare the helper object for replacing placeholders in custom G-code and output filename.
    m_placeholder_parser = print.placeholder_parser;
    m_placeholder_parser.update_timestamp();
    
    // Disable fan.
    if (print.config.cooling.value && print.config.disable_fan_first_layers.value)
        write(file, m_writer.set_fan(0, true));
    
    // Set bed temperature if the start G-code does not contain any bed temp control G-codes.
    if (print.config.first_layer_bed_temperature.value > 0 &&
        boost::ifind_first(print.config.start_gcode.value, std::string("M140")).empty() &&
        boost::ifind_first(print.config.start_gcode.value, std::string("M190")).empty())
        write(file, m_writer.set_bed_temperature(print.config.first_layer_bed_temperature.value, true));
    
    // Set extruder(s) temperature before and after start G-code.
    this->_print_first_layer_extruder_temperatures(file, print, false);
    fprintf(file, "%s\n", m_placeholder_parser.process(print.config.start_gcode.value).c_str());
    this->_print_first_layer_extruder_temperatures(file, print, true);
    
    // Set other general things.
    write(file, this->preamble());
    
    // Initialize a motion planner for object-to-object travel moves.
    if (print.config.avoid_crossing_perimeters.value) {
        //coord_t distance_from_objects = coord_t(scale_(1.)); 
        // Compute the offsetted convex hull for each object and repeat it for each copy.
        Polygons islands_p;
        for (const PrintObject *object : print.objects) {
            // Discard objects only containing thin walls (offset would fail on an empty polygon).
            Polygons polygons;
            for (const Layer *layer : object->layers)
                for (const ExPolygon &expoly : layer->slices.expolygons)
                    polygons.push_back(expoly.contour);
            if (! polygons.empty()) {
                // Translate convex hull for each object copy and append it to the islands array.
                for (const Point &copy : object->_shifted_copies)
                    for (Polygon poly : polygons) {
                        poly.translate(copy);
                        islands_p.emplace_back(std::move(poly));
                    }
            }
        }
        m_avoid_crossing_perimeters.init_external_mp(union_ex(islands_p));
    }
    
    // Calculate wiping points if needed
    if (print.config.ooze_prevention.value) {
        Points skirt_points;
        for (const ExtrusionEntity *ee : print.skirt.entities)
            for (const ExtrusionPath &path : dynamic_cast<const ExtrusionLoop*>(ee)->paths)
                append(skirt_points, path.polyline.points);
        if (! skirt_points.empty()) {
            Polygon outer_skirt = Slic3r::Geometry::convex_hull(skirt_points);
            Polygons skirts;
            for (unsigned int extruder_id : print.extruders()) {
                const Pointf &extruder_offset = print.config.extruder_offset.get_at(extruder_id);
                Polygon s(outer_skirt);
                s.translate(-scale_(extruder_offset.x), -scale_(extruder_offset.y));
                skirts.emplace_back(std::move(s));
            }
            m_ooze_prevention.enable = true;
            m_ooze_prevention.standby_points =
                offset(Slic3r::Geometry::convex_hull(skirts), scale_(3.f)).front().equally_spaced_points(scale_(10.));
#if 0
                require "Slic3r/SVG.pm";
                Slic3r::SVG::output(
                    "ooze_prevention.svg",
                    red_polygons    => \@skirts,
                    polygons        => [$outer_skirt],
                    points          => $gcodegen->ooze_prevention->standby_points,
                );
#endif
        }
    }
    
    // Set initial extruder only after custom start G-code.
    write(file, this->set_extruder(print.extruders().front()));
    
    // Do all objects for each layer.
    if (print.config.complete_objects.value) {
        // Print objects from the smallest to the tallest to avoid collisions
        // when moving onto next object starting point.
        std::vector<PrintObject*> objects(print.objects);
        std::sort(objects.begin(), objects.end(), [](const PrintObject* po1, const PrintObject* po2) { return po1->size.z < po2->size.z; });        
        size_t finished_objects = 0;
        for (PrintObject *object : objects) {
            for (const Point &copy : object->_shifted_copies) {
                Points copies;
                copies.push_back(copy);
                // Move to the origin position for the copy we're going to print.
                // This happens before Z goes down to layer 0 again, so that no collision happens hopefully.
                if (finished_objects > 0) {
                    this->set_origin(unscale(copy.x), unscale(copy.y));
                    m_enable_cooling_markers = false; // we're not filtering these moves through CoolingBuffer
                    m_avoid_crossing_perimeters.use_external_mp_once = true;
                    write(file, this->retract());
                    write(file, this->travel_to(Point(0, 0), erNone, "move to origin position for next object"));
                    m_enable_cooling_markers = true;
                    // Disable motion planner when traveling to first object point.
                    m_avoid_crossing_perimeters.disable_once = true;
                }
                
                // Order layers by print_z, support layers preceding the object layers.
                std::vector<Layer*> layers(object->layers);
                layers.insert(layers.end(), object->support_layers.begin(), object->support_layers.end());
                std::sort(layers.begin(), layers.end(), [](const Layer *l1, const Layer *l2) 
                    { return (l1->print_z == l2->print_z) ? dynamic_cast<const SupportLayer*>(l1) != nullptr : l1->print_z < l2->print_z; });
                for (Layer *layer : layers) {
                    // Ff we are printing the bottom layer of an object, and we have already finished
                    // another one, set first layer temperatures. This happens before the Z move
                    // is triggered, so machine has more time to reach such temperatures.
                    if (layer->id() == 0 && finished_objects > 0) {
                        if (print.config.first_layer_bed_temperature.value > 0)
                            write(file, m_writer.set_bed_temperature(print.config.first_layer_bed_temperature));
                        // Set first layer extruder.
                        this->_print_first_layer_extruder_temperatures(file, print, false);
                    }
                    this->process_layer(file, print, *layer, copies);
                }
                write(file, this->filter(m_cooling_buffer->flush(), true));
                ++ finished_objects;
                // Flag indicating whether the nozzle temperature changes from 1st to 2nd layer were performed.
                // Reset it when starting another object from 1st layer.
                m_second_layer_things_done = false;
            }
        }
    } else {
        // Order objects using a nearest neighbor search.
        std::vector<size_t> object_indices;
        Points object_reference_points;
        for (PrintObject *object : print.objects)
            object_reference_points.push_back(object->_shifted_copies.front());
        Slic3r::Geometry::chained_path(object_reference_points, object_indices);        
        // Sort layers by Z.
        // All extrusion moves with the same top layer height are extruded uninterrupted,
        // object extrusion moves are performed first, then the support.
        std::map<coordf_t, std::vector<LayerPtrs>> layers; // print_z => [ [layers], [layers], [layers] ]  by obj_idx
        for (size_t obj_idx = 0; obj_idx < print.objects.size(); ++ obj_idx) {
            PrintObject *object = print.objects[obj_idx];
            // Collect the object layers by z, support layers first, object layers second.
            LayerPtrs object_layers(object->support_layers.begin(), object->support_layers.end());
            append(object_layers, object->layers);
            for (Layer *layer : object_layers) {
                std::vector<LayerPtrs> &object_layers_at_printz = layers[layer->print_z];
                if (object_layers_at_printz.empty())
                    object_layers_at_printz.resize(print.objects.size(), LayerPtrs());
                object_layers_at_printz[obj_idx].push_back(layer);
            }
        }
        for (auto &layer : layers)
            for (size_t obj_idx : object_indices)
                for (Layer *l : layer.second[obj_idx])
                    this->process_layer(file, print, *l, l->object()->_shifted_copies);
        write(file, this->filter(m_cooling_buffer->flush(), true));
    }

    // write end commands to file
    write(file, this->retract());   // TODO: process this retract through PressureRegulator in order to discharge fully
    write(file, m_writer.set_fan(false));
    writeln(file, m_placeholder_parser.process(print.config.end_gcode));
    write(file, m_writer.update_progress(m_layer_count, m_layer_count, true)); // 100%
    write(file, m_writer.postamble());
    
    // get filament stats
    print.filament_stats.clear();
    print.total_used_filament    = 0.;
    print.total_extruded_volume  = 0.;
    print.total_weight           = 0.;
    print.total_cost             = 0.;
    for (const Extruder &extruder : m_writer.extruders) {
        double used_filament   = extruder.used_filament();
        double extruded_volume = extruder.extruded_volume();
        double filament_weight = extruded_volume * extruder.filament_density() * 0.001;
        double filament_cost   = filament_weight * extruder.filament_cost()    * 0.001;
        print.filament_stats.insert(std::pair<size_t,float>(extruder.id, used_filament));
        fprintf(file, "; filament used = %.1lfmm (%.1lfcm3)\n", used_filament, extruded_volume * 0.001);
        if (filament_weight > 0.) {
            print.total_weight = print.total_weight + filament_weight;
            fprintf(file, "; filament used = %.1lf\n", filament_weight);
            if (filament_cost > 0.) {
                print.total_cost = print.total_cost + filament_cost;
                fprintf(file, "; filament cost = %.1lf\n", filament_cost);
            }
        }
        print.total_used_filament   = print.total_used_filament + used_filament;
        print.total_extruded_volume = print.total_extruded_volume + extruded_volume;
    }
    fprintf(file, "; total filament cost = %.1lf\n", print.total_cost);

    // Append full config.
    fprintf(file, "\n");
    for (const std::string &key : print.config.keys())
        fprintf(file, "; %s = %s\n", key.c_str(), print.config.serialize(key).c_str());
    for (const std::string &key : print.default_object_config.keys())
        fprintf(file, "; %s = %s\n", key.c_str(), print.default_object_config.serialize(key).c_str());

    return true;
}

// Write 1st layer extruder temperatures into the G-code.
// Only do that if the start G-code does not already contain any M-code controlling an extruder temperature.
// FIXME this does not work correctly for multi-extruder, single heater configuration as it emits multiple preheat commands for the same heater.
// M104 - Set Extruder Temperature
// M109 - Set Extruder Temperature and Wait
void GCode::_print_first_layer_extruder_temperatures(FILE *file, Print &print, bool wait)
{
    if (boost::ifind_first(print.config.start_gcode.value, std::string("M104")).empty() &&
        boost::ifind_first(print.config.start_gcode.value, std::string("M109")).empty()) {
        for (unsigned int tool_id : print.extruders()) {
            int temp = print.config.first_layer_temperature.get_at(tool_id);
            if (print.config.ooze_prevention.value)
                temp += print.config.standby_temperature_delta.value;
            if (temp > 0)
                write(file, m_writer.set_temperature(temp, wait, tool_id));
        }
    }
}

// Called per object's layer.
// First a $gcode string is collected,
// then filtered and finally written to a file $fh.
//FIXME If printing multiple objects at once, this incorrectly applies cooling logic to a single object's layer instead
// of all the objects printed.
void GCode::process_layer(FILE *file, const Print &print, const Layer &layer, const Points &object_copies)
{
    std::string gcode;
    
    const PrintObject &object = *layer.object();
    m_config.apply(object.config, true);

    const SupportLayer *support_layer = dynamic_cast<const SupportLayer*>(&layer);
    
    // Check whether it is possible to apply the spiral vase logic for this layer.
    if (m_spiral_vase) {
        bool enable = (layer.id() > 0 || print.config.brim_width.value == 0.) && (layer.id() >= print.config.skirt_height.value && ! print.has_infinite_skirt());
        if (enable) {
            for (const LayerRegion *layer_region : layer.regions)
                if (layer_region->region()->config.bottom_solid_layers.value > layer.id() ||
                    layer_region->perimeters.items_count() > 1 ||
                    layer_region->fills.items_count() > 0) {
                    enable = false;
                    break;
                }
        }
        m_spiral_vase->enable = enable;
    }

    // If we're going to apply spiralvase to this layer, disable loop clipping
    m_enable_loop_clipping = (! m_spiral_vase || ! m_spiral_vase->enable);
    
    if (! m_second_layer_things_done && layer.id() == 1) {
        // Transition from 1st to 2nd layer. Adjust nozzle temperatures as prescribed by the nozzle dependent
        // first_layer_temperature vs. temperature settings.
        for (const Extruder &extruder : m_writer.extruders) {
            int temperature = print.config.temperature.get_at(extruder.id);
            if (temperature > 0 && temperature != print.config.first_layer_temperature.get_at(extruder.id))
                gcode += m_writer.set_temperature(temperature, false, extruder.id);
        }
        if (print.config.bed_temperature.value > 0 && print.config.bed_temperature != print.config.first_layer_bed_temperature.value)
            gcode += m_writer.set_bed_temperature(print.config.bed_temperature);
        // Mark the temperature transition from 1st to 2nd layer to be finished.
        m_second_layer_things_done = true;
    }
    
    // Set new layer - this will change Z and force a retraction if retract_layer_change is enabled.
    if (! print.config.before_layer_gcode.value.empty()) {
        PlaceholderParser pp(m_placeholder_parser);
        pp.set("layer_num", m_layer_index + 1);
        pp.set("layer_z",   layer.print_z);
        gcode += pp.process(print.config.before_layer_gcode.value) + "\n";
    }
    gcode += this->change_layer(layer);  // this will increase m_layer_index
    if (! print.config.layer_gcode.value.empty()) {
        PlaceholderParser pp(m_placeholder_parser);
        pp.set("layer_num", m_layer_index);
        pp.set("layer_z",   layer.print_z);
        gcode += pp.process(print.config.layer_gcode.value) + "\n";
    }
    
    // Extrude skirt at the print_z of the raft layers and normal object layers
    // not at the print_z of the interlaced support material layers.
    //FIXME this will print the support 1st, skirt 2nd and an object 3rd 
    // if they are at the same print_z, it is not the 1st print layer and the support is printed before object.
    if (// Not enough skirt layers printed yer
        (m_skirt_done.size() < print.config.skirt_height.value || print.has_infinite_skirt()) &&
        // This print_z has not been extruded yet
        m_skirt_done.find(layer.print_z) == m_skirt_done.end() &&
        // and this layer is the 1st layer, or it is an object layer, or it is a raft layer.
        (layer.id() == 0 || support_layer == nullptr || layer.id() < object.config.raft_layers.value)) {
        this->set_origin(0.,0.);
        m_avoid_crossing_perimeters.use_external_mp = true;
        std::vector<unsigned int> extruder_ids = m_writer.extruder_ids();
        gcode += this->set_extruder(extruder_ids.front());
        // Skip skirt if we have a large brim.
        if (layer.id() < print.config.skirt_height.value || print.has_infinite_skirt()) {
            Flow skirt_flow = print.skirt_flow();
            // Distribute skirt loops across all extruders.
            for (size_t i = 0; i < print.skirt.entities.size(); ++ i) {
                // When printing layers > 0 ignore 'min_skirt_length' and 
                // just use the 'skirts' setting; also just use the current extruder.
                if (layer.id() > 0 && i >= print.config.skirts)
                    break;
                unsigned int extruder_id = extruder_ids[(i / extruder_ids.size()) % extruder_ids.size()];
                if (layer.id() == 0)
                    gcode += this->set_extruder(extruder_id);
                // Adjust flow according to this layer's layer height.
                ExtrusionLoop loop = *dynamic_cast<const ExtrusionLoop*>(print.skirt.entities[i]);
                Flow layer_skirt_flow(skirt_flow);
                layer_skirt_flow.height = (float)layer.height;
                double mm3_per_mm = layer_skirt_flow.mm3_per_mm();
                for (ExtrusionPath &path : loop.paths) {
                    path.height     = (float)layer.height;
                    path.mm3_per_mm = mm3_per_mm;
                }                
                gcode += this->extrude(loop, "skirt", object.config.support_material_speed.value);
            }
        }
        m_skirt_done.insert(layer.print_z);
        m_avoid_crossing_perimeters.use_external_mp = false;
        // Allow a straight travel move to the first object point if this is the first layer (but don't in next layers).
        if (layer.id() == 0)
            m_avoid_crossing_perimeters.disable_once = true;
    }
    
    // extrude brim
    if (! m_brim_done) {
        gcode += this->set_extruder(print.regions.front()->config.perimeter_extruder.value - 1);
        this->set_origin(0.f, 0.f);
        m_avoid_crossing_perimeters.use_external_mp = true;
        for (const ExtrusionEntity *ee : print.brim.entities)
            gcode += this->extrude(*dynamic_cast<const ExtrusionLoop*>(ee), "brim", object.config.support_material_speed.value);
        m_brim_done = true;
        m_avoid_crossing_perimeters.use_external_mp = false;
        // Allow a straight travel move to the first object point.
        m_avoid_crossing_perimeters.disable_once = true;
    }
    
    for (const Point &copy : object_copies) {
        // When starting a new object, use the external motion planner for the first travel move.
        if (m_last_obj_copy != copy)
            m_avoid_crossing_perimeters.use_external_mp_once = true;
        m_last_obj_copy = copy;
        this->set_origin(unscale(copy.x), unscale(copy.y));        
        // Extrude support material before other things because it might use a lower Z
        // and also because we avoid travelling on other things when printing it.
        if (support_layer != nullptr) {
			if (support_layer->support_fills.entities.size() > 0) {
				if (object.config.support_material_extruder.value == object.config.support_material_interface_extruder.value) {
					// Both the support and the support interface are printed with the same extruder, therefore
					// the interface may be interleaved with the support base.
					// Don't change extruder if the extruder is set to 0. Use the current extruder instead.
					gcode += this->extrude_support(
						support_layer->support_fills.chained_path_from(m_last_pos, false),
						object.config.support_material_extruder);
				} else {
					// Extrude the support base before support interface for two reasons.
					// 1) Support base may be extruded with the current extruder (extruder ID 0)
					//    and the support interface may be printed with the solube material,
					//    then one wants to avoid the base being printed with the soluble material.
					// 2) It is likely better to print the interface after the base as the interface is
					//    often printed by bridges and it is convenient to have the base printed already,
					//    so the bridges may stick to it.
					gcode += this->extrude_support(
						support_layer->support_fills.chained_path_from(m_last_pos, false, erSupportMaterial),
						object.config.support_material_extruder);
					// Extrude the support interface.
					gcode += this->extrude_support(
						support_layer->support_fills.chained_path_from(m_last_pos, false, erSupportMaterialInterface),
						object.config.support_material_interface_extruder);
				}
			}
			continue;
        }
        
        // We now define a strategy for building perimeters and fills. The separation 
        // between regions doesn't matter in terms of printing order, as we follow 
        // another logic instead:
        // - we group all extrusions by extruder so that we minimize toolchanges
        // - we start from the last used extruder
        // - for each extruder, we group extrusions by island
        // - for each island, we extrude perimeters first, unless user set the infill_first
        //   option
        // (Still, we have to keep track of regions because we need to apply their config)
        
        // group extrusions by extruder and then by island
        std::map<unsigned int, std::vector<ByExtruder>> by_extruder;

        size_t n_slices = layer.slices.expolygons.size();
        std::vector<BoundingBox> layer_surface_bboxes;
        layer_surface_bboxes.reserve(n_slices);
        for (const ExPolygon &expoly : layer.slices.expolygons)
            layer_surface_bboxes.push_back(get_extents(expoly.contour));
        auto point_inside_surface = [&layer, &layer_surface_bboxes](const size_t i, const Point &point) { 
            const BoundingBox &bbox = layer_surface_bboxes[i];
            return point.x >= bbox.min.x && point.x < bbox.max.x &&
                   point.y >= bbox.min.y && point.y < bbox.max.y &&
                   layer.slices.expolygons[i].contour.contains(point);
        };

        for (size_t region_id = 0; region_id < print.regions.size(); ++ region_id) {
            const LayerRegion *layerm = layer.regions[region_id];
            if (layerm == nullptr)
                continue;
            const PrintRegion &region = *print.regions[region_id];
            
            // process perimeters
            for (const ExtrusionEntity *ee : layerm->perimeters.entities) {
                // perimeter_coll represents perimeter extrusions of a single island.
                const auto *perimeter_coll = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                if (perimeter_coll->entities.empty())
                    // This shouldn't happen but first_point() would fail.
                    continue;
                // Init by_extruder item only if we actually use the extruder.
                std::vector<ByExtruder> &byex = by_extruder[std::max<int>(region.config.perimeter_extruder.value - 1, 0)];
                if (byex.empty())
                    byex.assign(n_slices, ByExtruder());
                for (size_t i = 0; i <= n_slices; ++ i)
                    if (// perimeter_coll->first_point does not fit inside any slice
                        i == n_slices ||
                        // perimeter_coll->first_point fits inside ith slice
                        point_inside_surface(i, perimeter_coll->first_point())) {
                        if (byex[i].by_region.empty())
                            byex[i].by_region.assign(print.regions.size(), ByExtruder::ToExtrude());
                        byex[i].by_region[region_id].perimeters.append(perimeter_coll->entities);
                        break;
                    }
            }
            
            // process infill
            // layerm->fills is a collection of Slic3r::ExtrusionPath::Collection objects (C++ class ExtrusionEntityCollection), 
            // each one containing the ExtrusionPath objects of a certain infill "group" (also called "surface"
            // throughout the code). We can redefine the order of such Collections but we have to 
            // do each one completely at once.
            for (const ExtrusionEntity *ee : layerm->fills.entities) {
                // fill represents infill extrusions of a single island.
                const auto *fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                if (fill->entities.empty())
                    // This shouldn't happen but first_point() would fail.
                    continue;
                // init by_extruder item only if we actually use the extruder
                int extruder_id = std::max<int>(0, (is_solid_infill(fill->entities.front()->role()) ? region.config.solid_infill_extruder : region.config.infill_extruder) - 1);
                // Init by_extruder item only if we actually use the extruder.
                std::vector<ByExtruder> &byex = by_extruder[extruder_id];
                if (byex.empty())
                    byex.assign(n_slices, ByExtruder());
                for (size_t i = 0; i <= n_slices; ++i)
                    if (// fill->first_point does not fit inside any slice
                        i == n_slices ||
                        // fill->first_point fits inside ith slice
                        point_inside_surface(i, fill->first_point())) {
                        if (byex[i].by_region.empty())
                            byex[i].by_region.assign(print.regions.size(), ByExtruder::ToExtrude());
                        byex[i].by_region[region_id].infills.append(fill->entities);
                        break;
                    }
            }
        } // for regions

        // Tweak extruder ordering to save toolchanges.
        std::vector<unsigned int> extruders;
        extruders.reserve(by_extruder.size());
        for (const auto &ex : by_extruder)
            extruders.push_back(ex.first);
        // Reorder the extruders, so that the last used extruder is at the front.
        for (size_t i = 1; i < extruders.size(); ++ i)
            if (extruders[i] == m_writer.extruder()->id) {
                // Move the last extruder to the front.
                memmove(extruders.data() + 1, extruders.data(), i);
                extruders.front() = m_writer.extruder()->id;
                break;
            }
        // Extrude the perimeters & infill ordered by the extruders.
        for (unsigned int extruder_id : extruders) {
            gcode += this->set_extruder(extruder_id);
            for (const ByExtruder &island : by_extruder[extruder_id]) {
                if (print.config.infill_first) {
                    gcode += this->extrude_infill(print, island.by_region);
                    gcode += this->extrude_perimeters(print, island.by_region);
                } else {
                    gcode += this->extrude_perimeters(print, island.by_region);
                    gcode += this->extrude_infill(print, island.by_region);
                }
            }
        }
    } // for object copies

    // Apply spiral vase post-processing if this layer contains suitable geometry
    // (we must feed all the G-code into the post-processor, including the first 
    // bottom non-spiral layers otherwise it will mess with positions)
    // we apply spiral vase at this stage because it requires a full layer.
    if (m_spiral_vase)
        gcode = m_spiral_vase->process_layer(gcode);
    // Apply cooling logic; this may alter speeds.
    if (m_cooling_buffer)
        gcode = m_cooling_buffer->append(
            gcode, 
            // Index of the current layer's object
            //FIXME add an index into the objects?
            std::find(print.objects.begin(), print.objects.end(), layer.object()) - print.objects.begin(),
            layer.id(),
            // Differentiate normal layers from the support layers for the purpose of layer cooling time.
            support_layer != nullptr);
    write(file, this->filter(std::move(gcode), false));
}

std::string GCode::filter(std::string &&gcode, bool flush)
{
    // apply pressure equalization if enabled;
    // printf("G-code before filter:\n%s\n", gcode.c_str());
    std::string out = m_pressure_equalizer ? 
        m_pressure_equalizer->process(gcode.c_str(), flush) :
        std::move(gcode);
    // printf("G-code after filter:\n%s\n", out.c_str());
    return out;
}

void GCode::apply_print_config(const PrintConfig &print_config)
{
    m_writer.apply_print_config(print_config);
    m_config.apply(print_config);
}

void GCode::set_extruders(const std::vector<unsigned int> &extruder_ids)
{
    m_writer.set_extruders(extruder_ids);
    
    // enable wipe path generation if any extruder has wipe enabled
    m_wipe.enable = false;
    for (auto id : extruder_ids)
        if (m_config.wipe.get_at(id)) {
            m_wipe.enable = true;
            break;
        }
}

void GCode::set_origin(const Pointf &pointf)
{    
    // if origin increases (goes towards right), last_pos decreases because it goes towards left
    const Point translate(
        scale_(m_origin.x - pointf.x),
        scale_(m_origin.y - pointf.y)
    );
    m_last_pos.translate(translate);
    m_wipe.path.translate(translate);
    m_origin = pointf;
}

std::string GCode::preamble()
{
    std::string gcode = m_writer.preamble();
    
    /*  Perform a *silent* move to z_offset: we need this to initialize the Z
        position of our writer object so that any initial lift taking place
        before the first layer change will raise the extruder from the correct
        initial Z instead of 0.  */
    m_writer.travel_to_z(m_config.z_offset.value);
    
    return gcode;
}

std::string GCode::change_layer(const Layer &layer)
{
    m_layer = &layer;
    m_layer_index++;
    m_first_layer = (layer.id() == 0);
    m_lower_layer_edge_grid.release();

    std::string gcode;

    if (m_enable_analyzer_markers) {
        // Store the binary pointer to the layer object directly into the G-code to be accessed by the GCodeAnalyzer.
        char buf[64];
        sprintf(buf, ";_LAYEROBJ:%p\n", m_layer);
        gcode += buf;
    }
    
    // avoid computing islands and overhangs if they're not needed
    if (m_config.avoid_crossing_perimeters) {
        ExPolygons islands = union_ex(layer.slices, true);
        m_avoid_crossing_perimeters.init_layer_mp(islands);
    }
    
    if (m_layer_count > 0)
        gcode += m_writer.update_progress(m_layer_index, m_layer_count);
    
    coordf_t z = layer.print_z + m_config.z_offset.value;  // in unscaled coordinates
    if (EXTRUDER_CONFIG(retract_layer_change) && m_writer.will_move_z(z)) {
        gcode += this->retract();
    }
    {
        std::ostringstream comment;
        comment << "move to next layer (" << m_layer_index << ")";
        gcode += m_writer.travel_to_z(z, comment.str());
    }
    
    // forget last wiping path as wiping after raising Z is pointless
    m_wipe.reset_path();
    
    return gcode;
}

static inline const char* ExtrusionRole2String(const ExtrusionRole role)
{
    switch (role) {
    case erNone:                        return "erNone";
    case erPerimeter:                   return "erPerimeter";
    case erExternalPerimeter:           return "erExternalPerimeter";
    case erOverhangPerimeter:           return "erOverhangPerimeter";
    case erInternalInfill:              return "erInternalInfill";
    case erSolidInfill:                 return "erSolidInfill";
    case erTopSolidInfill:              return "erTopSolidInfill";
    case erBridgeInfill:                return "erBridgeInfill";
    case erGapFill:                     return "erGapFill";
    case erSkirt:                       return "erSkirt";
    case erSupportMaterial:             return "erSupportMaterial";
    case erSupportMaterialInterface:    return "erSupportMaterialInterface";
    case erMixed:                       return "erMixed";
    default:                            return "erInvalid";
    };
}

static inline const char* ExtrusionLoopRole2String(const ExtrusionLoopRole role)
{
    switch (role) {
    case elrDefault:                    return "elrDefault";
    case elrContourInternalPerimeter:   return "elrContourInternalPerimeter";
    case elrSkirt:                      return "elrSkirt";
    default:                            return "elrInvalid";
    }
};

// Return a value in <0, 1> of a cubic B-spline kernel centered around zero.
// The B-spline is re-scaled so it has value 1 at zero.
static inline float bspline_kernel(float x)
{
    x = std::abs(x);
	if (x < 1.f) {
		return 1.f - (3.f / 2.f) * x * x + (3.f / 4.f) * x * x * x;
	}
	else if (x < 2.f) {
		x -= 1.f;
		float x2 = x * x;
		float x3 = x2 * x;
		return (1.f / 4.f) - (3.f / 4.f) * x + (3.f / 4.f) * x2 - (1.f / 4.f) * x3;
	}
	else
        return 0;
}

static float extrudate_overlap_penalty(float nozzle_r, float weight_zero, float overlap_distance)
{
    // The extrudate is not fully supported by the lower layer. Fit a polynomial penalty curve.
    // Solved by sympy package:
/*
from sympy import *
(x,a,b,c,d,r,z)=symbols('x a b c d r z')
p = a + b*x + c*x*x + d*x*x*x
p2 = p.subs(solve([p.subs(x, -r), p.diff(x).subs(x, -r), p.diff(x,x).subs(x, -r), p.subs(x, 0)-z], [a, b, c, d]))
from sympy.plotting import plot
plot(p2.subs(r,0.2).subs(z,1.), (x, -1, 3), adaptive=False, nb_of_points=400)
*/
    if (overlap_distance < - nozzle_r) {
        // The extrudate is fully supported by the lower layer. This is the ideal case, therefore zero penalty.
        return 0.f;
    } else {
        float x  = overlap_distance / nozzle_r;
        float x2 = x * x;
        float x3 = x2 * x;
        return weight_zero * (1.f + 3.f * x + 3.f * x2 + x3);
    }
}

static Points::iterator project_point_to_polygon_and_insert(Polygon &polygon, const Point &pt, double eps)
{
    assert(polygon.points.size() >= 2);
    if (polygon.points.size() <= 1)
    if (polygon.points.size() == 1)
        return polygon.points.begin();

    Point  pt_min;
    double d_min = std::numeric_limits<double>::max();
    size_t i_min = size_t(-1);

    for (size_t i = 0; i < polygon.points.size(); ++ i) {
        size_t j = i + 1;
        if (j == polygon.points.size())
            j = 0;
        const Point &p1 = polygon.points[i];
        const Point &p2 = polygon.points[j];
        const Slic3r::Point v_seg = p1.vector_to(p2);
        const Slic3r::Point v_pt  = p1.vector_to(pt);
        const int64_t l2_seg = int64_t(v_seg.x) * int64_t(v_seg.x) + int64_t(v_seg.y) * int64_t(v_seg.y);
        int64_t t_pt = int64_t(v_seg.x) * int64_t(v_pt.x) + int64_t(v_seg.y) * int64_t(v_pt.y);
        if (t_pt < 0) {
            // Closest to p1.
            double dabs = sqrt(int64_t(v_pt.x) * int64_t(v_pt.x) + int64_t(v_pt.y) * int64_t(v_pt.y));
            if (dabs < d_min) {
                d_min  = dabs;
                i_min  = i;
                pt_min = p1;
            }
        }
        else if (t_pt > l2_seg) {
            // Closest to p2. Then p2 is the starting point of another segment, which shall be discovered in the next step.
            continue;
        } else {
            // Closest to the segment.
            assert(t_pt >= 0 && t_pt <= l2_seg);
            int64_t d_seg = int64_t(v_seg.y) * int64_t(v_pt.x) - int64_t(v_seg.x) * int64_t(v_pt.y);
            double d = double(d_seg) / sqrt(double(l2_seg));
            double dabs = std::abs(d);
            if (dabs < d_min) {
                d_min  = dabs;
                i_min  = i;
                // Evaluate the foot point.
                pt_min = p1;
                double linv = double(d_seg) / double(l2_seg);
                pt_min.x = pt.x - coord_t(floor(double(v_seg.y) * linv + 0.5));
				pt_min.y = pt.y + coord_t(floor(double(v_seg.x) * linv + 0.5));
				assert(Line(p1, p2).distance_to(pt_min) < scale_(1e-5));
            }
        }
    }

	assert(i_min != size_t(-1));
    if (pt_min.distance_to(polygon.points[i_min]) > eps) {
        // Insert a new point on the segment i_min, i_min+1.
        return polygon.points.insert(polygon.points.begin() + (i_min + 1), pt_min);
    }
    return polygon.points.begin() + i_min;
}

std::vector<float> polygon_parameter_by_length(const Polygon &polygon)
{
    // Parametrize the polygon by its length.
    std::vector<float> lengths(polygon.points.size()+1, 0.);
    for (size_t i = 1; i < polygon.points.size(); ++ i)
        lengths[i] = lengths[i-1] + float(polygon.points[i].distance_to(polygon.points[i-1]));
    lengths.back() = lengths[lengths.size()-2] + float(polygon.points.front().distance_to(polygon.points.back()));
    return lengths;
}

std::vector<float> polygon_angles_at_vertices(const Polygon &polygon, const std::vector<float> &lengths, float min_arm_length)
{
    assert(polygon.points.size() + 1 == lengths.size());
    if (min_arm_length > 0.25f * lengths.back())
        min_arm_length = 0.25f * lengths.back();

    // Find the initial prev / next point span.
    size_t idx_prev = polygon.points.size();
    size_t idx_curr = 0;
    size_t idx_next = 1;
    while (idx_prev > idx_curr && lengths.back() - lengths[idx_prev] < min_arm_length)
        -- idx_prev;
    while (idx_next < idx_prev && lengths[idx_next] < min_arm_length)
        ++ idx_next;

    std::vector<float> angles(polygon.points.size(), 0.f);
    for (; idx_curr < polygon.points.size(); ++ idx_curr) {
        // Move idx_prev up until the distance between idx_prev and idx_curr is lower than min_arm_length.
        if (idx_prev >= idx_curr) {
            while (idx_prev < polygon.points.size() && lengths.back() - lengths[idx_prev] + lengths[idx_curr] > min_arm_length)
                ++ idx_prev;
            if (idx_prev == polygon.points.size())
                idx_prev = 0;
        }
        while (idx_prev < idx_curr && lengths[idx_curr] - lengths[idx_prev] > min_arm_length)
            ++ idx_prev;
        // Move idx_prev one step back.
        if (idx_prev == 0)
            idx_prev = polygon.points.size() - 1;
        else
            -- idx_prev;
        // Move idx_next up until the distance between idx_curr and idx_next is greater than min_arm_length.
        if (idx_curr <= idx_next) {
            while (idx_next < polygon.points.size() && lengths[idx_next] - lengths[idx_curr] < min_arm_length)
                ++ idx_next;
            if (idx_next == polygon.points.size())
                idx_next = 0;
        }
        while (idx_next < idx_curr && lengths.back() - lengths[idx_curr] + lengths[idx_next] < min_arm_length)
            ++ idx_next;
        // Calculate angle between idx_prev, idx_curr, idx_next.
        const Point &p0 = polygon.points[idx_prev];
        const Point &p1 = polygon.points[idx_curr];
        const Point &p2 = polygon.points[idx_next];
        const Point  v1 = p0.vector_to(p1);
        const Point  v2 = p1.vector_to(p2);
		int64_t dot   = int64_t(v1.x)*int64_t(v2.x) + int64_t(v1.y)*int64_t(v2.y);
		int64_t cross = int64_t(v1.x)*int64_t(v2.y) - int64_t(v1.y)*int64_t(v2.x);
		float angle = float(atan2(double(cross), double(dot)));
        angles[idx_curr] = angle;
    }

    return angles;
}

std::string GCode::extrude(ExtrusionLoop loop, std::string description, double speed)
{
    // get a copy; don't modify the orientation of the original loop object otherwise
    // next copies (if any) would not detect the correct orientation

    if (m_layer->lower_layer != NULL) {
        if (! this->m_lower_layer_edge_grid) {
            // Create the distance field for a layer below.
            const coord_t distance_field_resolution = scale_(1.f);
            this->m_lower_layer_edge_grid = make_unique<EdgeGrid::Grid>();
            this->m_lower_layer_edge_grid->create(m_layer->lower_layer->slices, distance_field_resolution);
            this->m_lower_layer_edge_grid->calculate_sdf();
            #if 0
            {
                static int iRun = 0;
                BoundingBox bbox = this->m_lower_layer_edge_grid->bbox();
                bbox.min.x -= scale_(5.f);
                bbox.min.y -= scale_(5.f);
                bbox.max.x += scale_(5.f);
                bbox.max.y += scale_(5.f);
                EdgeGrid::save_png(*this->m_lower_layer_edge_grid, bbox, scale_(0.1f), debug_out_path("GCode_extrude_loop_edge_grid-%d.png", iRun++));
            }
            #endif
        }
    }
  
    // extrude all loops ccw
    bool was_clockwise = loop.make_counter_clockwise();
    
    SeamPosition seam_position = m_config.seam_position;
    if (loop.loop_role() == elrSkirt) 
        seam_position = spNearest;
    
    // find the point of the loop that is closest to the current extruder position
    // or randomize if requested
    Point last_pos = this->last_pos();
    if (m_config.spiral_vase) {
        loop.split_at(last_pos, false);
    } else if (seam_position == spNearest || seam_position == spAligned || seam_position == spRear) {
        Polygon        polygon    = loop.polygon();
        const coordf_t nozzle_dmr = EXTRUDER_CONFIG(nozzle_diameter);
        const coord_t  nozzle_r   = scale_(0.5*nozzle_dmr);

        // Retrieve the last start position for this object.
        float last_pos_weight = 1.f;
        switch (seam_position) {
        case spAligned:
            // Seam is aligned to the seam at the preceding layer.
            if (m_layer != NULL && m_seam_position.count(m_layer->object()) > 0) {
                last_pos = m_seam_position[m_layer->object()];
                last_pos_weight = 1.f;
            }
            break;
        case spRear:
            last_pos = m_layer->object()->bounding_box().center();
            last_pos.y += coord_t(3. * m_layer->object()->bounding_box().radius());
            last_pos_weight = 5.f;
            break;
        }

        // Insert a projection of last_pos into the polygon.
        size_t last_pos_proj_idx;
        {
            Points::iterator it = project_point_to_polygon_and_insert(polygon, last_pos, 0.1 * nozzle_r);
            last_pos_proj_idx = it - polygon.points.begin();
        }
        Point last_pos_proj = polygon.points[last_pos_proj_idx];
        // Parametrize the polygon by its length.
        std::vector<float> lengths = polygon_parameter_by_length(polygon);

        // For each polygon point, store a penalty.
        // First calculate the angles, store them as penalties. The angles are caluculated over a minimum arm length of nozzle_r.
        std::vector<float> penalties = polygon_angles_at_vertices(polygon, lengths, nozzle_r);
        // No penalty for reflex points, slight penalty for convex points, high penalty for flat surfaces.
        const float penaltyConvexVertex = 1.f;
        const float penaltyFlatSurface  = 5.f;
        const float penaltySeam         = 1.3f;
        const float penaltyOverhangHalf = 10.f;
        // Penalty for visible seams.
        for (size_t i = 0; i < polygon.points.size(); ++ i) {
            float ccwAngle = penalties[i];
            if (was_clockwise)
                ccwAngle = - ccwAngle;
            float penalty = 0;
//            if (ccwAngle <- float(PI/3.))
            if (ccwAngle <- float(0.6 * PI))
                // Sharp reflex vertex. We love that, it hides the seam perfectly.
                penalty = 0.f;
//            else if (ccwAngle > float(PI/3.))
            else if (ccwAngle > float(0.6 * PI))
                // Seams on sharp convex vertices are more visible than on reflex vertices.
                penalty = penaltyConvexVertex;
            else if (ccwAngle < 0.f) {
                // Interpolate penalty between maximum and zero.
                penalty = penaltyFlatSurface * bspline_kernel(ccwAngle * (PI * 2. / 3.));
            } else {
                assert(ccwAngle >= 0.f);
                // Interpolate penalty between maximum and the penalty for a convex vertex.
                penalty = penaltyConvexVertex + (penaltyFlatSurface - penaltyConvexVertex) * bspline_kernel(ccwAngle * (PI * 2. / 3.));
            }
            // Give a negative penalty for points close to the last point or the prefered seam location.
            //float dist_to_last_pos_proj = last_pos_proj.distance_to(polygon.points[i]);
            float dist_to_last_pos_proj = (i < last_pos_proj_idx) ? 
                std::min(lengths[last_pos_proj_idx] - lengths[i], lengths.back() - lengths[last_pos_proj_idx] + lengths[i]) : 
                std::min(lengths[i] - lengths[last_pos_proj_idx], lengths.back() - lengths[i] + lengths[last_pos_proj_idx]);
            float dist_max = 0.1f * lengths.back(); // 5.f * nozzle_dmr
            penalty -= last_pos_weight * bspline_kernel(dist_to_last_pos_proj / dist_max);
            penalties[i] = std::max(0.f, penalty);
        }

        // Penalty for overhangs.
        if (m_lower_layer_edge_grid) {
            // Use the edge grid distance field structure over the lower layer to calculate overhangs.
            coord_t nozzle_r = scale_(0.5*nozzle_dmr);
            coord_t search_r = scale_(0.8*nozzle_dmr);
            for (size_t i = 0; i < polygon.points.size(); ++ i) {
                const Point &p = polygon.points[i];
                coordf_t dist;
                // Signed distance is positive outside the object, negative inside the object.
                // The point is considered at an overhang, if it is more than nozzle radius
                // outside of the lower layer contour.
                bool found = m_lower_layer_edge_grid->signed_distance(p, search_r, dist);
                // If the approximate Signed Distance Field was initialized over m_lower_layer_edge_grid,
                // then the signed distnace shall always be known.
                assert(found);
                penalties[i] += extrudate_overlap_penalty(nozzle_r, penaltyOverhangHalf, dist);
            }
        }

        // Find a point with a minimum penalty.
        size_t idx_min = std::min_element(penalties.begin(), penalties.end()) - penalties.begin();

        // if (seam_position == spAligned)
        // For all (aligned, nearest, rear) seams:
        {
            // Very likely the weight of idx_min is very close to the weight of last_pos_proj_idx.
            // In that case use last_pos_proj_idx instead.
            float penalty_aligned  = penalties[last_pos_proj_idx];
            float penalty_min      = penalties[idx_min];
            float penalty_diff_abs = std::abs(penalty_min - penalty_aligned);
            float penalty_max      = std::max(penalty_min, penalty_aligned);
            float penalty_diff_rel = (penalty_max == 0.f) ? 0.f : penalty_diff_abs / penalty_max;
            // printf("Align seams, penalty aligned: %f, min: %f, diff abs: %f, diff rel: %f\n", penalty_aligned, penalty_min, penalty_diff_abs, penalty_diff_rel);
            if (penalty_diff_rel < 0.05) {
                // Penalty of the aligned point is very close to the minimum penalty.
                // Align the seams as accurately as possible.
                idx_min = last_pos_proj_idx;
            }
            m_seam_position[m_layer->object()] = polygon.points[idx_min];
        }

        // Export the contour into a SVG file.
        #if 0
        {
            static int iRun = 0;
            SVG svg(debug_out_path("GCode_extrude_loop-%d.svg", iRun ++));
            if (m_layer->lower_layer != NULL)
                svg.draw(m_layer->lower_layer->slices.expolygons);
            for (size_t i = 0; i < loop.paths.size(); ++ i)
                svg.draw(loop.paths[i].as_polyline(), "red");
            Polylines polylines;
            for (size_t i = 0; i < loop.paths.size(); ++ i)
                polylines.push_back(loop.paths[i].as_polyline());
            Slic3r::Polygons polygons;
            coordf_t nozzle_dmr = EXTRUDER_CONFIG(nozzle_diameter);
            coord_t delta = scale_(0.5*nozzle_dmr);
            Slic3r::offset(polylines, &polygons, delta);
//            for (size_t i = 0; i < polygons.size(); ++ i) svg.draw((Polyline)polygons[i], "blue");
            svg.draw(last_pos, "green", 3);
            svg.draw(polygon.points[idx_min], "yellow", 3);
            svg.Close();
        }
        #endif

        // Split the loop at the point with a minium penalty.
        if (!loop.split_at_vertex(polygon.points[idx_min]))
            // The point is not in the original loop. Insert it.
            loop.split_at(polygon.points[idx_min], true);

    } else if (seam_position == spRandom) {
        if (loop.loop_role() == elrContourInternalPerimeter) {
            // This loop does not contain any other loop. Set a random position.
            // The other loops will get a seam close to the random point chosen
            // on the inner most contour.
            //FIXME This works correctly for inner contours first only.
            //FIXME Better parametrize the loop by its length.
            Polygon polygon = loop.polygon();
            Point centroid = polygon.centroid();
            last_pos = Point(polygon.bounding_box().max.x, centroid.y);
            last_pos.rotate(fmod((float)rand()/16.0, 2.0*PI), centroid);
        }
        // Find the closest point, avoid overhangs.
        loop.split_at(last_pos, true);
    }
    
    // clip the path to avoid the extruder to get exactly on the first point of the loop;
    // if polyline was shorter than the clipping distance we'd get a null polyline, so
    // we discard it in that case
    double clip_length = m_enable_loop_clipping ? 
        scale_(EXTRUDER_CONFIG(nozzle_diameter)) * LOOP_CLIPPING_LENGTH_OVER_NOZZLE_DIAMETER : 
        0;

    // get paths
    ExtrusionPaths paths;
    loop.clip_end(clip_length, &paths);
    if (paths.empty()) return "";
    
    // apply the small perimeter speed
    if (is_perimeter(paths.front().role()) && loop.length() <= SMALL_PERIMETER_LENGTH && speed == -1)
        speed = m_config.small_perimeter_speed.get_abs_value(m_config.perimeter_speed);
    
    // extrude along the path
    std::string gcode;
    for (ExtrusionPaths::iterator path = paths.begin(); path != paths.end(); ++path) {
//    description += ExtrusionLoopRole2String(loop.loop_role());
//    description += ExtrusionRole2String(path->role);
        path->simplify(SCALED_RESOLUTION);
        gcode += this->_extrude(*path, description, speed);
    }
    
    // reset acceleration
    gcode += m_writer.set_acceleration(m_config.default_acceleration.value);
    
    if (m_wipe.enable)
        m_wipe.path = paths.front().polyline;  // TODO: don't limit wipe to last path
    
    // make a little move inwards before leaving loop
    if (paths.back().role() == erExternalPerimeter && m_layer != NULL && m_config.perimeters.value > 1) {
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
        gcode += m_writer.travel_to_xy(this->point_to_gcode(point), "move inwards before travel");
    }
    
    return gcode;
}

std::string GCode::extrude(ExtrusionMultiPath multipath, std::string description, double speed)
{
    // extrude along the path
    std::string gcode;
    for (ExtrusionPaths::iterator path = multipath.paths.begin(); path != multipath.paths.end(); ++path) {
//    description += ExtrusionLoopRole2String(loop.loop_role());
//    description += ExtrusionRole2String(path->role);
        path->simplify(SCALED_RESOLUTION);
        gcode += this->_extrude(*path, description, speed);
    }
    if (m_wipe.enable) {
        m_wipe.path = std::move(multipath.paths.back().polyline);  // TODO: don't limit wipe to last path
        m_wipe.path.reverse();
    }
    // reset acceleration
    gcode += m_writer.set_acceleration(m_config.default_acceleration.value);
    return gcode;
}

std::string GCode::extrude(const ExtrusionEntity &entity, std::string description, double speed)
{
    if (const ExtrusionPath* path = dynamic_cast<const ExtrusionPath*>(&entity)) {
        return this->extrude(*path, description, speed);
    } else if (const ExtrusionMultiPath* multipath = dynamic_cast<const ExtrusionMultiPath*>(&entity)) {
        return this->extrude(*multipath, description, speed);
    } else if (const ExtrusionLoop* loop = dynamic_cast<const ExtrusionLoop*>(&entity)) {
        return this->extrude(*loop, description, speed);
    } else {
        CONFESS("Invalid argument supplied to extrude()");
        return "";
    }
}

std::string GCode::extrude(ExtrusionPath path, std::string description, double speed)
{
//    description += ExtrusionRole2String(path.role());
    path.simplify(SCALED_RESOLUTION);
    std::string gcode = this->_extrude(path, description, speed);
    if (m_wipe.enable) {
        m_wipe.path = std::move(path.polyline);
        m_wipe.path.reverse();
    }    
    // reset acceleration
    gcode += m_writer.set_acceleration(m_config.default_acceleration.value);
    return gcode;
}

// Extrude perimeters: Decide where to put seams (hide or align seams).
std::string GCode::extrude_perimeters(const Print &print, const std::vector<ByExtruder::ToExtrude> &by_region)
{
    std::string gcode;
    for (const ByExtruder::ToExtrude &region : by_region) {
        m_config.apply(print.regions[&region - &by_region.front()]->config);
        for (ExtrusionEntity *ee : region.perimeters.entities)
            gcode += this->extrude(*ee, "perimeter");
    }
    return gcode;
}

// Chain the paths hierarchically by a greedy algorithm to minimize a travel distance.
std::string GCode::extrude_infill(const Print &print, const std::vector<ByExtruder::ToExtrude> &by_region)
{
    std::string gcode;
    for (const ByExtruder::ToExtrude &region : by_region) {
        m_config.apply(print.regions[&region - &by_region.front()]->config);
		ExtrusionEntityCollection chained = region.infills.chained_path_from(m_last_pos, false);
        for (ExtrusionEntity *fill : chained.entities) {
            auto *eec = dynamic_cast<ExtrusionEntityCollection*>(fill);
            if (eec) {
				ExtrusionEntityCollection chained2 = eec->chained_path_from(m_last_pos, false);
				for (ExtrusionEntity *ee : chained2.entities)
                    gcode += this->extrude(*ee, "infill");
            } else
                gcode += this->extrude(*fill, "infill");
        }
    }
    return gcode;
}

std::string GCode::extrude_support(const ExtrusionEntityCollection &support_fills, unsigned int extruder_id)
{
    std::string gcode;
    if (! support_fills.entities.empty()) {
        const char   *support_label            = "support material";
        const char   *support_interface_label  = "support material interface";
        const double  support_speed            = m_config.support_material_speed.value;
        const double  support_interface_speed  = m_config.support_material_interface_speed.get_abs_value(support_speed);
        // Only trigger extruder change if the extruder is not set to zero,
		// but make sure the extruder is initialized.
        // Extruder ID zero means "does not matter", extrude with the current extruder.
		if (m_writer.extruder() == nullptr && extruder_id == 0)
			extruder_id = 1;
        if (extruder_id > 0)
            gcode += this->set_extruder(extruder_id - 1);
        for (const ExtrusionEntity *ee : support_fills.entities) {
            ExtrusionRole role = ee->role();
            assert(role == erSupportMaterial || role == erSupportMaterialInterface);
            const char  *label = (role == erSupportMaterial) ? support_label : support_interface_label;
            const double speed = (role == erSupportMaterial) ? support_speed : support_interface_speed;
            const ExtrusionPath *path = dynamic_cast<const ExtrusionPath*>(ee);
            if (path)
                gcode += this->extrude(*path, label, speed);
            else {
                const ExtrusionMultiPath *multipath = dynamic_cast<const ExtrusionMultiPath*>(ee);
                assert(multipath != nullptr);
                if (multipath)
                    gcode += this->extrude(*multipath, label, speed);
            }
        }
    }
    return gcode;
}

std::string
GCode::_extrude(const ExtrusionPath &path, std::string description, double speed)
{
    std::string gcode;
    
    // go to first point of extrusion path
    if (!m_last_pos_defined || !m_last_pos.coincides_with(path.first_point())) {
        gcode += this->travel_to(
            path.first_point(),
            path.role(),
            "move to first " + description + " point"
        );
    }
    
    // compensate retraction
    gcode += this->unretract();
    
    // adjust acceleration
    {
        double acceleration;
        if (m_config.first_layer_acceleration.value > 0 && m_first_layer) {
            acceleration = m_config.first_layer_acceleration.value;
        } else if (m_config.perimeter_acceleration.value > 0 && is_perimeter(path.role())) {
            acceleration = m_config.perimeter_acceleration.value;
        } else if (m_config.bridge_acceleration.value > 0 && is_bridge(path.role())) {
            acceleration = m_config.bridge_acceleration.value;
        } else if (m_config.infill_acceleration.value > 0 && is_infill(path.role())) {
            acceleration = m_config.infill_acceleration.value;
        } else {
            acceleration = m_config.default_acceleration.value;
        }
        gcode += m_writer.set_acceleration(acceleration);
    }
    
    // calculate extrusion length per distance unit
    double e_per_mm = m_writer.extruder()->e_per_mm3 * path.mm3_per_mm;
    if (m_writer.extrusion_axis().empty()) e_per_mm = 0;
    
    // set speed
    if (speed == -1) {
        if (path.role() == erPerimeter) {
            speed = m_config.get_abs_value("perimeter_speed");
        } else if (path.role() == erExternalPerimeter) {
            speed = m_config.get_abs_value("external_perimeter_speed");
        } else if (path.role() == erOverhangPerimeter || path.role() == erBridgeInfill) {
            speed = m_config.get_abs_value("bridge_speed");
        } else if (path.role() == erInternalInfill) {
            speed = m_config.get_abs_value("infill_speed");
        } else if (path.role() == erSolidInfill) {
            speed = m_config.get_abs_value("solid_infill_speed");
        } else if (path.role() == erTopSolidInfill) {
            speed = m_config.get_abs_value("top_solid_infill_speed");
        } else if (path.role() == erGapFill) {
            speed = m_config.get_abs_value("gap_fill_speed");
        } else {
            CONFESS("Invalid speed");
        }
    }
    if (m_first_layer) {
        speed = m_config.get_abs_value("first_layer_speed", speed);
    }
    if (m_volumetric_speed != 0. && speed == 0) {
        speed = m_volumetric_speed / path.mm3_per_mm;
    }
    if (m_config.max_volumetric_speed.value > 0) {
        // cap speed with max_volumetric_speed anyway (even if user is not using autospeed)
        speed = std::min(
            speed,
            m_config.max_volumetric_speed.value / path.mm3_per_mm
        );
    }
    if (EXTRUDER_CONFIG(filament_max_volumetric_speed) > 0) {
        // cap speed with max_volumetric_speed anyway (even if user is not using autospeed)
        speed = std::min(
            speed,
            EXTRUDER_CONFIG(filament_max_volumetric_speed) / path.mm3_per_mm
        );
    }
    double F = speed * 60;  // convert mm/sec to mm/min
    
    // extrude arc or line
    if (m_enable_extrusion_role_markers || m_enable_analyzer_markers) {
        if (path.role() != m_last_extrusion_role) {
            m_last_extrusion_role = path.role();
            char buf[32];
            sprintf(buf, ";_EXTRUSION_ROLE:%d\n", int(path.role()));
            gcode += buf;
        }
    }
    if (is_bridge(path.role()) && m_enable_cooling_markers)
        gcode += ";_BRIDGE_FAN_START\n";
    gcode += m_writer.set_speed(F, "", m_enable_cooling_markers ? ";_EXTRUDE_SET_SPEED" : "");
    double path_length = 0;
    {
        std::string comment = m_config.gcode_comments ? description : "";
        Lines lines = path.polyline.lines();
        for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line) {
            const double line_length = line->length() * SCALING_FACTOR;
            path_length += line_length;
            
            gcode += m_writer.extrude_to_xy(
                this->point_to_gcode(line->b),
                e_per_mm * line_length,
                comment
            );
        }
    }
    if (is_bridge(path.role()) && m_enable_cooling_markers)
        gcode += ";_BRIDGE_FAN_END\n";
    
    this->set_last_pos(path.last_point());
    
    if (m_config.cooling)
        m_elapsed_time += path_length / F * 60;
    
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
        && m_config.avoid_crossing_perimeters
        && ! m_avoid_crossing_perimeters.disable_once) {
        travel = m_avoid_crossing_perimeters.travel_to(*this, point);
        
        // check again whether the new travel path still needs a retraction
        needs_retraction = this->needs_retraction(travel, role);
        //if (needs_retraction && m_layer_index > 1) exit(0);
    }
    
    // Re-allow avoid_crossing_perimeters for the next travel moves
    m_avoid_crossing_perimeters.disable_once = false;
    m_avoid_crossing_perimeters.use_external_mp_once = false;
    
    // generate G-code for the travel move
    std::string gcode;
    if (needs_retraction)
        gcode += this->retract();
    else
        // Reset the wipe path when traveling, so one would not wipe along an old path.
        m_wipe.reset_path();
    
    // use G1 because we rely on paths being straight (G0 may make round paths)
    Lines lines = travel.lines();
    for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line)
	    gcode += m_writer.travel_to_xy(this->point_to_gcode(line->b), comment);
    
    /*  While this makes the estimate more accurate, CoolingBuffer calculates the slowdown
        factor on the whole elapsed time but only alters non-travel moves, thus the resulting
        time is still shorter than the configured threshold. We could create a new 
        elapsed_travel_time but we would still need to account for bridges, retractions, wipe etc.
    if (m_config.cooling)
        m_elapsed_time += unscale(travel.length()) / m_config.get_abs_value("travel_speed");
    */

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
        const SupportLayer* support_layer = dynamic_cast<const SupportLayer*>(m_layer);
        //FIXME support_layer->support_islands.contains should use some search structure!
        if (support_layer != NULL && support_layer->support_islands.contains(travel)) {
            // skip retraction if this is a travel move inside a support material island
            return false;
        }
    }
    
    if (m_config.only_retract_when_crossing_perimeters && m_layer != nullptr) {
        if (m_config.fill_density.value > 0
            && m_layer->any_internal_region_slice_contains(travel)) {
            /*  skip retraction if travel is contained in an internal slice *and*
                internal infill is enabled (so that stringing is entirely not visible)  */
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
    
    if (m_writer.extruder() == NULL)
        return gcode;
    
    // wipe (if it's enabled for this extruder and we have a stored wipe path)
    if (EXTRUDER_CONFIG(wipe) && m_wipe.has_path())
        gcode += m_wipe.wipe(*this, toolchange);
    
    /*  The parent class will decide whether we need to perform an actual retraction
        (the extruder might be already retracted fully or partially). We call these 
        methods even if we performed wipe, since this will ensure the entire retraction
        length is honored in case wipe path was too short.  */
    gcode += toolchange ? m_writer.retract_for_toolchange() : m_writer.retract();
    
    gcode += m_writer.reset_e();
    if (m_writer.extruder()->retract_length() > 0 || m_config.use_firmware_retraction)
        gcode += m_writer.lift();
    
    return gcode;
}

std::string
GCode::unretract()
{
    std::string gcode;
    gcode += m_writer.unlift();
    gcode += m_writer.unretract();
    return gcode;
}

std::string
GCode::set_extruder(unsigned int extruder_id)
{
    m_placeholder_parser.set("current_extruder", extruder_id);
    if (!m_writer.need_toolchange(extruder_id))
        return "";
    
    // if we are running a single-extruder setup, just set the extruder and return nothing
    if (!m_writer.multiple_extruders) {
        return m_writer.toolchange(extruder_id);
    }
    
    // prepend retraction on the current extruder
    std::string gcode = this->retract(true);

    // Always reset the extrusion path, even if the tool change retract is set to zero.
    m_wipe.reset_path();
    
    // append custom toolchange G-code
    if (m_writer.extruder() != NULL && !m_config.toolchange_gcode.value.empty()) {
        PlaceholderParser pp = m_placeholder_parser;
        pp.set("previous_extruder", m_writer.extruder()->id);
        pp.set("next_extruder",     extruder_id);
        gcode += pp.process(m_config.toolchange_gcode.value) + '\n';
    }
    
    // if ooze prevention is enabled, park current extruder in the nearest
    // standby point and set it to the standby temperature
    if (m_ooze_prevention.enable && m_writer.extruder() != NULL)
        gcode += m_ooze_prevention.pre_toolchange(*this);
    
    // append the toolchange command
    gcode += m_writer.toolchange(extruder_id);
    
    // set the new extruder to the operating temperature
    if (m_ooze_prevention.enable)
        gcode += m_ooze_prevention.post_toolchange(*this);
    
    return gcode;
}

// convert a model-space scaled point into G-code coordinates
Pointf GCode::point_to_gcode(const Point &point) const
{
    Pointf extruder_offset = EXTRUDER_CONFIG(extruder_offset);
    return Pointf(
        unscale(point.x) + m_origin.x - extruder_offset.x,
        unscale(point.y) + m_origin.y - extruder_offset.y
    );
}

}
