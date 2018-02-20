#include "GCode.hpp"
#include "ExtrusionEntity.hpp"
#include "EdgeGrid.hpp"
#include "Geometry.hpp"
#include "GCode/PrintExtents.hpp"
#include "GCode/WipeTowerPrusaMM.hpp"
#include "Utils.hpp"

#include <algorithm>
#include <cstdlib>
#include <math.h>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/foreach.hpp>
#include <boost/log/trivial.hpp>

#include <boost/nowide/iostream.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/cstdlib.hpp>

#include "SVG.hpp"

#include <Shiny/Shiny.h>

#if 0
// Enable debugging and asserts, even in the release build.
#define DEBUG
#define _DEBUG
#undef NDEBUG
#endif

#include <assert.h>

namespace Slic3r {

// Only add a newline in case the current G-code does not end with a newline.
static inline void check_add_eol(std::string &gcode)
{
    if (! gcode.empty() && gcode.back() != '\n')
        gcode += '\n';    
}
    
// Plan a travel move while minimizing the number of perimeter crossings.
// point is in unscaled coordinates, in the coordinate system of the current active object
// (set by gcodegen.set_origin()).
Polyline AvoidCrossingPerimeters::travel_to(const GCode &gcodegen, const Point &point) 
{
    // If use_external, then perform the path planning in the world coordinate system (correcting for the gcodegen offset).
    // Otherwise perform the path planning in the coordinate system of the active object.
    bool  use_external  = this->use_external_mp || this->use_external_mp_once;
    Point scaled_origin = use_external ? Point::new_scale(gcodegen.origin().x, gcodegen.origin().y) : Point(0, 0);
    Polyline result = (use_external ? m_external_mp.get() : m_layer_mp.get())->
        shortest_path(gcodegen.last_pos() + scaled_origin, point + scaled_origin);
    if (use_external)
        result.translate(scaled_origin.negative());
    return result;
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
        ? gcodegen.config().first_layer_temperature.get_at(gcodegen.writer().extruder()->id())
        : gcodegen.config().temperature.get_at(gcodegen.writer().extruder()->id());
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
    // Shorten the retraction length by the amount already retracted before wipe.
    length *= (1. - gcodegen.writer().extruder()->retract_before_wipe());

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
        for (const Line &line : wipe_path.lines()) {
            double segment_length = line.length();
            /*  Reduce retraction length a bit to avoid effective retraction speed to be greater than the configured one
                due to rounding (TODO: test and/or better math for this)  */
            double dE = length * (segment_length / wipe_dist) * 0.95;
            //FIXME one shall not generate the unnecessary G1 Fxxx commands, here wipe_speed is a constant inside this cycle.
            // Is it here for the cooling markers? Or should it be outside of the cycle?
            gcode += gcodegen.writer().set_speed(wipe_speed*60, "", gcodegen.enable_cooling_markers() ? ";_WIPE" : "");
            gcode += gcodegen.writer().extrude_to_xy(
                gcodegen.point_to_gcode(line.b),
                -dE,
                "wipe and retract"
            );
        }
        
        // prevent wiping again on same path
        this->reset_path();
    }
    
    return gcode;
}

static inline Point wipe_tower_point_to_object_point(GCode &gcodegen, const WipeTower::xy &wipe_tower_pt)
{
    return Point(scale_(wipe_tower_pt.x - gcodegen.origin().x), scale_(wipe_tower_pt.y - gcodegen.origin().y));
}

std::string WipeTowerIntegration::append_tcr(GCode &gcodegen, const WipeTower::ToolChangeResult &tcr, int new_extruder_id) const
{
    std::string gcode;

    // Disable linear advance for the wipe tower operations.
    gcode += "M900 K0\n";
    // Move over the wipe tower.
    // Retract for a tool change, using the toolchange retract value and setting the priming extra length.
    gcode += gcodegen.retract(true);
    gcodegen.m_avoid_crossing_perimeters.use_external_mp_once = true;
    gcode += gcodegen.travel_to(
        wipe_tower_point_to_object_point(gcodegen, tcr.start_pos),
        erMixed,
        "Travel to a Wipe Tower");
    gcode += gcodegen.unretract();

    // Let the tool change be executed by the wipe tower class.
    // Inform the G-code writer about the changes done behind its back.
    gcode += tcr.gcode;
    // Let the m_writer know the current extruder_id, but ignore the generated G-code.
	if (new_extruder_id >= 0 && gcodegen.writer().need_toolchange(new_extruder_id))
        gcodegen.writer().toolchange(new_extruder_id);
    // Always append the filament start G-code even if the extruder did not switch,
    // because the wipe tower resets the linear advance and we want it to be re-enabled.
    const std::string &start_filament_gcode = gcodegen.config().start_filament_gcode.get_at(new_extruder_id);
    if (! start_filament_gcode.empty()) {
        // Process the start_filament_gcode for the active filament only.
        gcodegen.placeholder_parser().set("current_extruder", new_extruder_id);
        gcode += gcodegen.placeholder_parser_process("start_filament_gcode", start_filament_gcode, new_extruder_id);
        check_add_eol(gcode);
    }
    // A phony move to the end position at the wipe tower.
    gcodegen.writer().travel_to_xy(Pointf(tcr.end_pos.x, tcr.end_pos.y));
    gcodegen.set_last_pos(wipe_tower_point_to_object_point(gcodegen, tcr.end_pos));

    // Prepare a future wipe.
    gcodegen.m_wipe.path.points.clear();
    if (new_extruder_id >= 0) {
        // Start the wipe at the current position.
        gcodegen.m_wipe.path.points.emplace_back(wipe_tower_point_to_object_point(gcodegen, tcr.end_pos));
        // Wipe end point: Wipe direction away from the closer tower edge to the further tower edge.
        gcodegen.m_wipe.path.points.emplace_back(wipe_tower_point_to_object_point(gcodegen, 
            WipeTower::xy((std::abs(m_left - tcr.end_pos.x) < std::abs(m_right - tcr.end_pos.x)) ? m_right : m_left,
            tcr.end_pos.y)));
    }

    // Let the planner know we are traveling between objects.
    gcodegen.m_avoid_crossing_perimeters.use_external_mp_once = true;
    return gcode;
}

std::string WipeTowerIntegration::prime(GCode &gcodegen)
{
    assert(m_layer_idx == 0);
    std::string gcode;

    if (&m_priming != nullptr && ! m_priming.extrusions.empty()) {
        // Disable linear advance for the wipe tower operations.
        gcode += "M900 K0\n";
        // Let the tool change be executed by the wipe tower class.
        // Inform the G-code writer about the changes done behind its back.
        gcode += m_priming.gcode;
        // Let the m_writer know the current extruder_id, but ignore the generated G-code.
        unsigned int current_extruder_id = m_priming.extrusions.back().tool;
        gcodegen.writer().toolchange(current_extruder_id);
        gcodegen.placeholder_parser().set("current_extruder", current_extruder_id);
        // A phony move to the end position at the wipe tower.
        gcodegen.writer().travel_to_xy(Pointf(m_priming.end_pos.x, m_priming.end_pos.y));
        gcodegen.set_last_pos(wipe_tower_point_to_object_point(gcodegen, m_priming.end_pos));
        // Prepare a future wipe.
        gcodegen.m_wipe.path.points.clear();
        // Start the wipe at the current position.
        gcodegen.m_wipe.path.points.emplace_back(wipe_tower_point_to_object_point(gcodegen, m_priming.end_pos));
        // Wipe end point: Wipe direction away from the closer tower edge to the further tower edge.
        gcodegen.m_wipe.path.points.emplace_back(wipe_tower_point_to_object_point(gcodegen, 
            WipeTower::xy((std::abs(m_left - m_priming.end_pos.x) < std::abs(m_right - m_priming.end_pos.x)) ? m_right : m_left,
            m_priming.end_pos.y)));
    }
    return gcode;
}

std::string WipeTowerIntegration::tool_change(GCode &gcodegen, int extruder_id, bool finish_layer)
{
    std::string gcode;
	assert(m_layer_idx >= 0 && m_layer_idx <= m_tool_changes.size());
    if (! m_brim_done || gcodegen.writer().need_toolchange(extruder_id) || finish_layer) {
		if (m_layer_idx < m_tool_changes.size()) {
			assert(m_tool_change_idx < m_tool_changes[m_layer_idx].size());
			gcode += append_tcr(gcodegen, m_tool_changes[m_layer_idx][m_tool_change_idx++], extruder_id);
		}
        m_brim_done = true;
    }
    return gcode;
}

// Print is finished. Now it remains to unload the filament safely with ramming over the wipe tower.
std::string WipeTowerIntegration::finalize(GCode &gcodegen)
{
    std::string gcode;
    if (std::abs(gcodegen.writer().get_position().z - m_final_purge.print_z) > EPSILON)
        gcode += gcodegen.change_layer(m_final_purge.print_z);
    gcode += append_tcr(gcodegen, m_final_purge, -1);
    return gcode;
}

#define EXTRUDER_CONFIG(OPT) m_config.OPT.get_at(m_writer.extruder()->id())

// Collect pairs of object_layer + support_layer sorted by print_z.
// object_layer & support_layer are considered to be on the same print_z, if they are not further than EPSILON.
std::vector<GCode::LayerToPrint> GCode::collect_layers_to_print(const PrintObject &object)
{
    std::vector<GCode::LayerToPrint> layers_to_print;
    layers_to_print.reserve(object.layers.size() + object.support_layers.size());

    // Pair the object layers with the support layers by z.
    size_t idx_object_layer  = 0;
    size_t idx_support_layer = 0;
    while (idx_object_layer < object.layers.size() || idx_support_layer < object.support_layers.size()) {
        LayerToPrint layer_to_print;
        layer_to_print.object_layer  = (idx_object_layer < object.layers.size()) ? object.layers[idx_object_layer ++] : nullptr;
        layer_to_print.support_layer = (idx_support_layer < object.support_layers.size()) ? object.support_layers[idx_support_layer ++] : nullptr;
        if (layer_to_print.object_layer && layer_to_print.support_layer) {
            if (layer_to_print.object_layer->print_z < layer_to_print.support_layer->print_z - EPSILON) {
                layer_to_print.support_layer = nullptr;
                -- idx_support_layer;
            } else if (layer_to_print.support_layer->print_z < layer_to_print.object_layer->print_z - EPSILON) {
                layer_to_print.object_layer = nullptr;
                -- idx_object_layer;
            }
        }
        layers_to_print.emplace_back(layer_to_print);
    }

    return layers_to_print;
}

// Prepare for non-sequential printing of multiple objects: Support resp. object layers with nearly identical print_z 
// will be printed for  all objects at once.
// Return a list of <print_z, per object LayerToPrint> items.
std::vector<std::pair<coordf_t, std::vector<GCode::LayerToPrint>>> GCode::collect_layers_to_print(const Print &print)
{
    struct OrderingItem {
        coordf_t    print_z;
        size_t      object_idx;
        size_t      layer_idx;
    };
    std::vector<std::vector<LayerToPrint>>  per_object(print.objects.size(), std::vector<LayerToPrint>());
    std::vector<OrderingItem>               ordering;
    for (size_t i = 0; i < print.objects.size(); ++ i) {
        per_object[i] = collect_layers_to_print(*print.objects[i]);
        OrderingItem ordering_item;
        ordering_item.object_idx = i;
        ordering.reserve(ordering.size() + per_object[i].size());
        const LayerToPrint &front = per_object[i].front();
        for (const LayerToPrint &ltp : per_object[i]) {
            ordering_item.print_z   = ltp.print_z();
            ordering_item.layer_idx = &ltp - &front;
            ordering.emplace_back(ordering_item);
        }
    }

    std::sort(ordering.begin(), ordering.end(), [](const OrderingItem &oi1, const OrderingItem &oi2) { return oi1.print_z < oi2.print_z; });

    std::vector<std::pair<coordf_t, std::vector<LayerToPrint>>> layers_to_print;
    // Merge numerically very close Z values.
    for (size_t i = 0; i < ordering.size();) {
        // Find the last layer with roughly the same print_z.
        size_t j = i + 1;
        coordf_t zmax = ordering[i].print_z + EPSILON;
        for (; j < ordering.size() && ordering[j].print_z <= zmax; ++ j) ;
        // Merge into layers_to_print.
        std::pair<coordf_t, std::vector<LayerToPrint>> merged;
        // Assign an average print_z to the set of layers with nearly equal print_z.
        merged.first = 0.5 * (ordering[i].print_z + ordering[j-1].print_z);
        merged.second.assign(print.objects.size(), LayerToPrint());
        for (; i < j; ++ i) {
            const OrderingItem &oi = ordering[i];
            assert(merged.second[oi.object_idx].layer() == nullptr);
            merged.second[oi.object_idx] = std::move(per_object[oi.object_idx][oi.layer_idx]);
        }
        layers_to_print.emplace_back(std::move(merged));
    }

    return layers_to_print;
}

void GCode::do_export(Print *print, const char *path, GCodePreviewData *preview_data)
{
    PROFILE_CLEAR();

    BOOST_LOG_TRIVIAL(info) << "Exporting G-code...";

    // Remove the old g-code if it exists.
    boost::nowide::remove(path);

    std::string path_tmp(path);
    path_tmp += ".tmp";

    FILE *file = boost::nowide::fopen(path_tmp.c_str(), "wb");
    if (file == nullptr)
        throw std::runtime_error(std::string("G-code export to ") + path + " failed.\nCannot open the file for writing.\n");

    this->m_placeholder_parser_failed_templates.clear();
    this->_do_export(*print, file, preview_data);
    fflush(file);
    if (ferror(file)) {
        fclose(file);
        boost::nowide::remove(path_tmp.c_str());
        throw std::runtime_error(std::string("G-code export to ") + path + " failed\nIs the disk full?\n");
    }
    fclose(file);
    if (! this->m_placeholder_parser_failed_templates.empty()) {
        // G-code export proceeded, but some of the PlaceholderParser substitutions failed.
        std::string msg = std::string("G-code export to ") + path + " failed due to invalid custom G-code sections:\n\n";
        for (const std::string &name : this->m_placeholder_parser_failed_templates)
            msg += std::string("\t") + name + "\n";
        msg += "\nPlease inspect the file ";
        msg += path_tmp + " for error messages enclosed between\n";
        msg += "        !!!!! Failed to process the custom G-code template ...\n";
        msg += "and\n";
        msg += "        !!!!! End of an error report for the custom G-code template ...\n";
        throw std::runtime_error(msg);
    }

    if (boost::nowide::rename(path_tmp.c_str(), path) != 0)
        throw std::runtime_error(
            std::string("Failed to rename the output G-code file from ") + path_tmp + " to " + path + '\n' +
            "Is " + path_tmp + " locked?" + '\n');

    BOOST_LOG_TRIVIAL(info) << "Exporting G-code finished";

    // Write the profiler measurements to file
    PROFILE_UPDATE();
    PROFILE_OUTPUT(debug_out_path("gcode-export-profile.txt").c_str());
}

void GCode::_do_export(Print &print, FILE *file, GCodePreviewData *preview_data)
{
    PROFILE_FUNC();

    // resets time estimator
    m_time_estimator.reset();
    m_time_estimator.set_dialect(print.config.gcode_flavor);

    // resets analyzer
    m_analyzer.reset();
    m_enable_analyzer = preview_data != nullptr;

    // resets analyzer's tracking data
    m_last_mm3_per_mm = GCodeAnalyzer::Default_mm3_per_mm;
    m_last_width = GCodeAnalyzer::Default_Width;
    m_last_height = GCodeAnalyzer::Default_Height;

    // How many times will be change_layer() called?
    // change_layer() in turn increments the progress bar status.
    m_layer_count = 0;
    if (print.config.complete_objects.value) {
        // Add each of the object's layers separately.
        for (auto object : print.objects) {
            std::vector<coordf_t> zs;
            zs.reserve(object->layers.size() + object->support_layers.size());
            for (auto layer : object->layers)
                zs.push_back(layer->print_z);
            for (auto layer : object->support_layers)
                zs.push_back(layer->print_z);
            std::sort(zs.begin(), zs.end());
            m_layer_count += (unsigned int)(object->copies().size() * (std::unique(zs.begin(), zs.end()) - zs.begin()));
        }
    } else {
        // Print all objects with the same print_z together.
        std::vector<coordf_t> zs;
        for (auto object : print.objects) {
            zs.reserve(zs.size() + object->layers.size() + object->support_layers.size());
            for (auto layer : object->layers)
                zs.push_back(layer->print_z);
            for (auto layer : object->support_layers)
                zs.push_back(layer->print_z);
        }
        std::sort(zs.begin(), zs.end());
        m_layer_count = (unsigned int)(std::unique(zs.begin(), zs.end()) - zs.begin());
    }

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
    _write_format(file, "; %s\n\n", Slic3r::header_slic3r_generated().c_str());
    // Write notes (content of the Print Settings tab -> Notes)
    {
        std::list<std::string> lines;
        boost::split(lines, print.config.notes.value, boost::is_any_of("\n"), boost::token_compress_off);
        for (auto line : lines) {
            // Remove the trailing '\r' from the '\r\n' sequence.
            if (! line.empty() && line.back() == '\r')
                line.pop_back();
            _write_format(file, "; %s\n", line.c_str());
        }
        if (! lines.empty())
            _write(file, "\n");
    }
    // Write some terse information on the slicing parameters.
    const PrintObject *first_object         = print.objects.front();
    const double       layer_height         = first_object->config.layer_height.value;
    const double       first_layer_height   = first_object->config.first_layer_height.get_abs_value(layer_height);
    for (size_t region_id = 0; region_id < print.regions.size(); ++ region_id) {
        auto region = print.regions[region_id];
        _write_format(file, "; external perimeters extrusion width = %.2fmm\n", region->flow(frExternalPerimeter, layer_height, false, false, -1., *first_object).width);
        _write_format(file, "; perimeters extrusion width = %.2fmm\n",          region->flow(frPerimeter,         layer_height, false, false, -1., *first_object).width);
        _write_format(file, "; infill extrusion width = %.2fmm\n",              region->flow(frInfill,            layer_height, false, false, -1., *first_object).width);
        _write_format(file, "; solid infill extrusion width = %.2fmm\n",        region->flow(frSolidInfill,       layer_height, false, false, -1., *first_object).width);
        _write_format(file, "; top infill extrusion width = %.2fmm\n",          region->flow(frTopSolidInfill,    layer_height, false, false, -1., *first_object).width);
        if (print.has_support_material())
            _write_format(file, "; support material extrusion width = %.2fmm\n", support_material_flow(first_object).width);
        if (print.config.first_layer_extrusion_width.value > 0)
            _write_format(file, "; first layer extrusion width = %.2fmm\n",   region->flow(frPerimeter, first_layer_height, false, true, -1., *first_object).width);
        _write_format(file, "\n");
    }
    
    // Prepare the helper object for replacing placeholders in custom G-code and output filename.
    m_placeholder_parser = print.placeholder_parser;
    m_placeholder_parser.update_timestamp();

    // Get optimal tool ordering to minimize tool switches of a multi-exruder print.
    // For a print by objects, find the 1st printing object.
    ToolOrdering tool_ordering;
    unsigned int initial_extruder_id = (unsigned int)-1;
    unsigned int final_extruder_id   = (unsigned int)-1;
    size_t       initial_print_object_id = 0;
    bool         has_wipe_tower      = false;
    if (print.config.complete_objects.value) {
		// Find the 1st printing object, find its tool ordering and the initial extruder ID.
		for (; initial_print_object_id < print.objects.size(); ++initial_print_object_id) {
			tool_ordering = ToolOrdering(*print.objects[initial_print_object_id], initial_extruder_id);
			if ((initial_extruder_id = tool_ordering.first_extruder()) != (unsigned int)-1)
				break;
		}
	} else {
		// Find tool ordering for all the objects at once, and the initial extruder ID.
        // If the tool ordering has been pre-calculated by Print class for wipe tower already, reuse it.
		tool_ordering = print.m_tool_ordering.empty() ?
            ToolOrdering(print, initial_extruder_id) :
            print.m_tool_ordering;
		initial_extruder_id = tool_ordering.first_extruder();
        has_wipe_tower = print.has_wipe_tower() && tool_ordering.has_wipe_tower();
    }
    if (initial_extruder_id == (unsigned int)-1) {
        // Nothing to print!
        initial_extruder_id = 0;
        final_extruder_id   = 0;
    } else {
        final_extruder_id = tool_ordering.last_extruder();
        assert(final_extruder_id != (unsigned int)-1);
    }

    m_cooling_buffer->set_current_extruder(initial_extruder_id);

    // Disable fan.
    if (! print.config.cooling.get_at(initial_extruder_id) || print.config.disable_fan_first_layers.get_at(initial_extruder_id))
        _write(file, m_writer.set_fan(0, true));

    // Let the start-up script prime the 1st printing tool.
    m_placeholder_parser.set("initial_tool", initial_extruder_id);
    m_placeholder_parser.set("initial_extruder", initial_extruder_id);
    m_placeholder_parser.set("current_extruder", initial_extruder_id);
    // Useful for sequential prints.
    m_placeholder_parser.set("current_object_idx", 0);
    // For the start / end G-code to do the priming and final filament pull in case there is no wipe tower provided.
    m_placeholder_parser.set("has_wipe_tower", has_wipe_tower);
    std::string start_gcode = this->placeholder_parser_process("start_gcode", print.config.start_gcode.value, initial_extruder_id);
    
    // Set bed temperature if the start G-code does not contain any bed temp control G-codes.
    this->_print_first_layer_bed_temperature(file, print, start_gcode, initial_extruder_id, true);
    // Set extruder(s) temperature before and after start G-code.
    this->_print_first_layer_extruder_temperatures(file, print, start_gcode, initial_extruder_id, false);
    // Write the custom start G-code
    _writeln(file, start_gcode);
    // Process filament-specific gcode in extruder order.
    if (print.config.single_extruder_multi_material) {
        if (has_wipe_tower) {
            // Wipe tower will control the extruder switching, it will call the start_filament_gcode.
        } else {
            // Only initialize the initial extruder.
            _writeln(file, this->placeholder_parser_process("start_filament_gcode", print.config.start_filament_gcode.values[initial_extruder_id], initial_extruder_id));
        }
    } else {
        for (const std::string &start_gcode : print.config.start_filament_gcode.values)
            _writeln(file, this->placeholder_parser_process("start_gcode", start_gcode, (unsigned int)(&start_gcode - &print.config.start_filament_gcode.values.front())));
    }
    this->_print_first_layer_extruder_temperatures(file, print, start_gcode, initial_extruder_id, true);
    
    // Set other general things.
    _write(file, this->preamble());

    // Initialize a motion planner for object-to-object travel moves.
    if (print.config.avoid_crossing_perimeters.value) {
        // Collect outer contours of all objects over all layers.
        // Discard objects only containing thin walls (offset would fail on an empty polygon).
        Polygons islands;
        for (const PrintObject *object : print.objects)
            for (const Layer *layer : object->layers)
                for (const ExPolygon &expoly : layer->slices.expolygons)
                    for (const Point &copy : object->_shifted_copies) {
                        islands.emplace_back(expoly.contour);
                        islands.back().translate(copy);
                    }
        //FIXME Mege the islands in parallel.
        m_avoid_crossing_perimeters.init_external_mp(union_ex(islands));
    }
    
    // Calculate wiping points if needed
    if (print.config.ooze_prevention.value && ! print.config.single_extruder_multi_material) {
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
    _write(file, this->set_extruder(initial_extruder_id));

    // Do all objects for each layer.
    if (print.config.complete_objects.value) {
        // Print objects from the smallest to the tallest to avoid collisions
        // when moving onto next object starting point.
        std::vector<PrintObject*> objects(print.objects);
        std::sort(objects.begin(), objects.end(), [](const PrintObject* po1, const PrintObject* po2) { return po1->size.z < po2->size.z; });        
        size_t finished_objects = 0;
        for (size_t object_id = initial_print_object_id; object_id < objects.size(); ++ object_id) {
            const PrintObject &object = *objects[object_id];
            for (const Point &copy : object._shifted_copies) {
                // Get optimal tool ordering to minimize tool switches of a multi-exruder print.
                if (object_id != initial_print_object_id || &copy != object._shifted_copies.data()) {
                    // Don't initialize for the first object and first copy.
                    tool_ordering = ToolOrdering(object, final_extruder_id);
                    unsigned int new_extruder_id = tool_ordering.first_extruder();
                    if (new_extruder_id == (unsigned int)-1)
                        // Skip this object.
                        continue;
                    initial_extruder_id = new_extruder_id;
                    final_extruder_id   = tool_ordering.last_extruder();
                    assert(final_extruder_id != (unsigned int)-1);
                }
                this->set_origin(unscale(copy.x), unscale(copy.y));
                if (finished_objects > 0) {
                    // Move to the origin position for the copy we're going to print.
                    // This happens before Z goes down to layer 0 again, so that no collision happens hopefully.
                    m_enable_cooling_markers = false; // we're not filtering these moves through CoolingBuffer
                    m_avoid_crossing_perimeters.use_external_mp_once = true;
                    _write(file, this->retract());
                    _write(file, this->travel_to(Point(0, 0), erNone, "move to origin position for next object"));
                    m_enable_cooling_markers = true;
                    // Disable motion planner when traveling to first object point.
                    m_avoid_crossing_perimeters.disable_once = true;
                    // Ff we are printing the bottom layer of an object, and we have already finished
                    // another one, set first layer temperatures. This happens before the Z move
                    // is triggered, so machine has more time to reach such temperatures.
                    m_placeholder_parser.set("current_object_idx", int(finished_objects));
                    std::string between_objects_gcode = this->placeholder_parser_process("between_objects_gcode", print.config.between_objects_gcode.value, initial_extruder_id);
                    // Set first layer bed and extruder temperatures, don't wait for it to reach the temperature.
                    this->_print_first_layer_bed_temperature(file, print, between_objects_gcode, initial_extruder_id, false);
                    this->_print_first_layer_extruder_temperatures(file, print, between_objects_gcode, initial_extruder_id, false);
                    _writeln(file, between_objects_gcode);
                }
                // Reset the cooling buffer internal state (the current position, feed rate, accelerations).
                m_cooling_buffer->reset();
                m_cooling_buffer->set_current_extruder(initial_extruder_id);
                // Pair the object layers with the support layers by z, extrude them.
                std::vector<LayerToPrint> layers_to_print = collect_layers_to_print(object);
                for (const LayerToPrint &ltp : layers_to_print) {
                    std::vector<LayerToPrint> lrs;
                    lrs.emplace_back(std::move(ltp));
                    this->process_layer(file, print, lrs, tool_ordering.tools_for_layer(ltp.print_z()), &copy - object._shifted_copies.data());
                }
                if (m_pressure_equalizer)
                    _write(file, m_pressure_equalizer->process("", true));
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
        // All extrusion moves with the same top layer height are extruded uninterrupted.
        std::vector<std::pair<coordf_t, std::vector<LayerToPrint>>> layers_to_print = collect_layers_to_print(print);
        // Prusa Multi-Material wipe tower.
        if (has_wipe_tower && ! layers_to_print.empty()) {
            m_wipe_tower.reset(new WipeTowerIntegration(print.config, *print.m_wipe_tower_priming.get(), print.m_wipe_tower_tool_changes, *print.m_wipe_tower_final_purge.get()));
            _write(file, m_writer.travel_to_z(first_layer_height + m_config.z_offset.value, "Move to the first layer height"));
		    _write(file, m_wipe_tower->prime(*this));
            // Verify, whether the print overaps the priming extrusions.
            BoundingBoxf bbox_print(get_print_extrusions_extents(print));
            coordf_t twolayers_printz = ((layers_to_print.size() == 1) ? layers_to_print.front() : layers_to_print[1]).first + EPSILON;
            for (const PrintObject *print_object : print.objects)
                bbox_print.merge(get_print_object_extrusions_extents(*print_object, twolayers_printz));
            bbox_print.merge(get_wipe_tower_extrusions_extents(print, twolayers_printz));
            BoundingBoxf bbox_prime(get_wipe_tower_priming_extrusions_extents(print));
            bbox_prime.offset(0.5f);
            // Beep for 500ms, tone 800Hz. Yet better, play some Morse.
            _write(file, this->retract());
            _write(file, "M300 S800 P500\n");
            if (bbox_prime.overlap(bbox_print)) {
                // Wait for the user to remove the priming extrusions, otherwise they would
                // get covered by the print.
                _write(file, "M1 Remove priming towers and click button.\n");
            }
            else {
                // Just wait for a bit to let the user check, that the priming succeeded.
                //TODO Add a message explaining what the printer is waiting for. This needs a firmware fix.
                _write(file, "M1 S10\n");
            }
        }
        // Extrude the layers.
        for (auto &layer : layers_to_print) {
            const ToolOrdering::LayerTools &layer_tools = tool_ordering.tools_for_layer(layer.first);
            if (m_wipe_tower && layer_tools.has_wipe_tower)
                m_wipe_tower->next_layer();
            this->process_layer(file, print, layer.second, layer_tools, size_t(-1));
        }
        if (m_pressure_equalizer)
            _write(file, m_pressure_equalizer->process("", true));
        if (m_wipe_tower)
            // Purge the extruder, pull out the active filament.
            _write(file, m_wipe_tower->finalize(*this));
    }

    // Write end commands to file.
    _write(file, this->retract());
    _write(file, m_writer.set_fan(false));
    // Process filament-specific gcode in extruder order.
    if (print.config.single_extruder_multi_material) {
        // Process the end_filament_gcode for the active filament only.
        _writeln(file, this->placeholder_parser_process("end_filament_gcode", print.config.end_filament_gcode.get_at(m_writer.extruder()->id()), m_writer.extruder()->id()));
    } else {
        for (const std::string &end_gcode : print.config.end_filament_gcode.values)
            _writeln(file, this->placeholder_parser_process("end_filament_gcode", end_gcode, (unsigned int)(&end_gcode - &print.config.end_filament_gcode.values.front())));
    }
    _writeln(file, this->placeholder_parser_process("end_gcode", print.config.end_gcode, m_writer.extruder()->id()));
    _write(file, m_writer.update_progress(m_layer_count, m_layer_count, true)); // 100%
    _write(file, m_writer.postamble());

    // calculates estimated printing time
    m_time_estimator.calculate_time();

    // Get filament stats.
    print.filament_stats.clear();
    print.total_used_filament    = 0.;
    print.total_extruded_volume  = 0.;
    print.total_weight           = 0.;
    print.total_cost             = 0.;
    print.estimated_print_time   = m_time_estimator.get_time_hms();
    for (const Extruder &extruder : m_writer.extruders()) {
        double used_filament   = extruder.used_filament();
        double extruded_volume = extruder.extruded_volume();
        double filament_weight = extruded_volume * extruder.filament_density() * 0.001;
        double filament_cost   = filament_weight * extruder.filament_cost()    * 0.001;
        print.filament_stats.insert(std::pair<size_t,float>(extruder.id(), used_filament));
        _write_format(file, "; filament used = %.1lfmm (%.1lfcm3)\n", used_filament, extruded_volume * 0.001);
        if (filament_weight > 0.) {
            print.total_weight = print.total_weight + filament_weight;
            _write_format(file, "; filament used = %.1lf\n", filament_weight);
            if (filament_cost > 0.) {
                print.total_cost = print.total_cost + filament_cost;
                _write_format(file, "; filament cost = %.1lf\n", filament_cost);
            }
        }
        print.total_used_filament = print.total_used_filament + used_filament;
        print.total_extruded_volume = print.total_extruded_volume + extruded_volume;
    }
    _write_format(file, "; total filament cost = %.1lf\n", print.total_cost);
    _write_format(file, "; estimated printing time = %s\n", m_time_estimator.get_time_hms().c_str());

    // Append full config.
    _write(file, "\n");
    {
        std::string full_config = "";
        append_full_config(print, full_config);
        if (!full_config.empty())
            _write(file, full_config);
    }

    // starts analizer calculations
    if (preview_data != nullptr)
        m_analyzer.calc_gcode_preview_data(*preview_data);
}

std::string GCode::placeholder_parser_process(const std::string &name, const std::string &templ, unsigned int current_extruder_id, const DynamicConfig *config_override)
{
    try {
        return m_placeholder_parser.process(templ, current_extruder_id, config_override);
    } catch (std::runtime_error &err) {
        // Collect the names of failed template substitutions for error reporting.
        this->m_placeholder_parser_failed_templates.insert(name);
        // Insert the macro error message into the G-code.
        return
            std::string("\n!!!!! Failed to process the custom G-code template ") + name + "\n" + 
            err.what() + 
            "!!!!! End of an error report for the custom G-code template " + name + "\n\n";
    }
}

// Parse the custom G-code, try to find mcode_set_temp_dont_wait and mcode_set_temp_and_wait inside the custom G-code.
// Returns true if one of the temp commands are found, and try to parse the target temperature value into temp_out.
static bool custom_gcode_sets_temperature(const std::string &gcode, const int mcode_set_temp_dont_wait, const int mcode_set_temp_and_wait, int &temp_out)
{
    temp_out = -1;
    if (gcode.empty())
        return false;

    const char *ptr = gcode.data();
    bool temp_set_by_gcode = false;
    while (*ptr != 0) {
        // Skip whitespaces.
        for (; *ptr == ' ' || *ptr == '\t'; ++ ptr);
        if (*ptr == 'M') {
            // Line starts with 'M'. It is a machine command.
            ++ ptr;
            // Parse the M code value.
            char *endptr = nullptr;
            int mcode = int(strtol(ptr, &endptr, 10));
            if (endptr != nullptr && endptr != ptr && (mcode == mcode_set_temp_dont_wait || mcode == mcode_set_temp_and_wait)) {
                // M104/M109 or M140/M190 found.
				ptr = endptr;
                // Let the caller know that the custom G-code sets the temperature.
                temp_set_by_gcode = true;
                // Now try to parse the temperature value.
				// While not at the end of the line:
				while (strchr(";\r\n\0", *ptr) == nullptr) {
                    // Skip whitespaces.
                    for (; *ptr == ' ' || *ptr == '\t'; ++ ptr);
                    if (*ptr == 'S') {
                        // Skip whitespaces.
                        for (++ ptr; *ptr == ' ' || *ptr == '\t'; ++ ptr);
                        // Parse an int.
                        endptr = nullptr;
                        long temp_parsed = strtol(ptr, &endptr, 10);
						if (endptr > ptr) {
							ptr = endptr;
							temp_out = temp_parsed;
						}
                    } else {
                        // Skip this word.
						for (; strchr(" \t;\r\n\0", *ptr) == nullptr; ++ ptr);
                    }
                }
            }
        }
        // Skip the rest of the line.
        for (; *ptr != 0 && *ptr != '\r' && *ptr != '\n'; ++ ptr);
		// Skip the end of line indicators.
		for (; *ptr == '\r' || *ptr == '\n'; ++ ptr);
	}
    return temp_set_by_gcode;
}

// Write 1st layer bed temperatures into the G-code.
// Only do that if the start G-code does not already contain any M-code controlling an extruder temperature.
// M140 - Set Extruder Temperature
// M190 - Set Extruder Temperature and Wait
void GCode::_print_first_layer_bed_temperature(FILE *file, Print &print, const std::string &gcode, unsigned int first_printing_extruder_id, bool wait)
{
    // Initial bed temperature based on the first extruder.
    int  temp = print.config.first_layer_bed_temperature.get_at(first_printing_extruder_id);
    // Is the bed temperature set by the provided custom G-code?
    int  temp_by_gcode     = -1;
    bool temp_set_by_gcode = custom_gcode_sets_temperature(gcode, 140, 190, temp_by_gcode);
    if (temp_set_by_gcode && temp_by_gcode >= 0 && temp_by_gcode < 1000)
        temp = temp_by_gcode;
    // Always call m_writer.set_bed_temperature() so it will set the internal "current" state of the bed temp as if
    // the custom start G-code emited these.
    std::string set_temp_gcode = m_writer.set_bed_temperature(temp, wait);
    if (! temp_set_by_gcode)
        _write(file, set_temp_gcode);
}

// Write 1st layer extruder temperatures into the G-code.
// Only do that if the start G-code does not already contain any M-code controlling an extruder temperature.
// M104 - Set Extruder Temperature
// M109 - Set Extruder Temperature and Wait
void GCode::_print_first_layer_extruder_temperatures(FILE *file, Print &print, const std::string &gcode, unsigned int first_printing_extruder_id, bool wait)
{
    // Is the bed temperature set by the provided custom G-code?
    int  temp_by_gcode     = -1;
    if (custom_gcode_sets_temperature(gcode, 104, 109, temp_by_gcode)) {
        // Set the extruder temperature at m_writer, but throw away the generated G-code as it will be written with the custom G-code.
        int temp = print.config.first_layer_temperature.get_at(first_printing_extruder_id);
        if (temp_by_gcode >= 0 && temp_by_gcode < 1000)
            temp = temp_by_gcode;
        m_writer.set_temperature(temp_by_gcode, wait, first_printing_extruder_id);
    } else {
        // Custom G-code does not set the extruder temperature. Do it now.
        if (print.config.single_extruder_multi_material.value) {
            // Set temperature of the first printing extruder only.
            int temp = print.config.first_layer_temperature.get_at(first_printing_extruder_id);
            if (temp > 0)
                _write(file, m_writer.set_temperature(temp, wait, first_printing_extruder_id));
        } else {
            // Set temperatures of all the printing extruders.
            for (unsigned int tool_id : print.extruders()) {
                int temp = print.config.first_layer_temperature.get_at(tool_id);
                if (print.config.ooze_prevention.value)
                    temp += print.config.standby_temperature_delta.value;
                if (temp > 0)
                    _write(file, m_writer.set_temperature(temp, wait, tool_id));
            }
        }
    }
}

inline GCode::ObjectByExtruder& object_by_extruder(
    std::map<unsigned int, std::vector<GCode::ObjectByExtruder>> &by_extruder, 
    unsigned int                                                  extruder_id, 
    size_t                                                        object_idx, 
    size_t                                                        num_objects)
{
    std::vector<GCode::ObjectByExtruder> &objects_by_extruder = by_extruder[extruder_id];
    if (objects_by_extruder.empty())
        objects_by_extruder.assign(num_objects, GCode::ObjectByExtruder());
    return objects_by_extruder[object_idx];
}

inline std::vector<GCode::ObjectByExtruder::Island>& object_islands_by_extruder(
    std::map<unsigned int, std::vector<GCode::ObjectByExtruder>>  &by_extruder, 
    unsigned int                                                   extruder_id, 
    size_t                                                         object_idx, 
    size_t                                                         num_objects,
    size_t                                                         num_islands)
{
    std::vector<GCode::ObjectByExtruder::Island> &islands = object_by_extruder(by_extruder, extruder_id, object_idx, num_objects).islands;
    if (islands.empty())
        islands.assign(num_islands, GCode::ObjectByExtruder::Island());
    return islands;
}

// In sequential mode, process_layer is called once per each object and its copy, 
// therefore layers will contain a single entry and single_object_idx will point to the copy of the object.
// In non-sequential mode, process_layer is called per each print_z height with all object and support layers accumulated.
// For multi-material prints, this routine minimizes extruder switches by gathering extruder specific extrusion paths
// and performing the extruder specific extrusions together.
void GCode::process_layer(
    // Write into the output file.
    FILE                            *file,
    const Print                     &print,
    // Set of object & print layers of the same PrintObject and with the same print_z.
    const std::vector<LayerToPrint> &layers,
    const ToolOrdering::LayerTools  &layer_tools,
    // If set to size_t(-1), then print all copies of all objects.
    // Otherwise print a single copy of a single object.
    const size_t                     single_object_idx)
{
    assert(! layers.empty());
    assert(! layer_tools.extruders.empty());
    // Either printing all copies of all objects, or just a single copy of a single object.
    assert(single_object_idx == size_t(-1) || layers.size() == 1);

    if (layer_tools.extruders.empty())
        // Nothing to extrude.
        return;

    // Extract 1st object_layer and support_layer of this set of layers with an equal print_z.
    const Layer         *object_layer  = nullptr;
    const SupportLayer  *support_layer = nullptr;
    for (const LayerToPrint &l : layers) {
        if (l.object_layer != nullptr && object_layer == nullptr)
            object_layer = l.object_layer;
        if (l.support_layer != nullptr && support_layer == nullptr)
            support_layer = l.support_layer;
    }
    const Layer         &layer         = (object_layer != nullptr) ? *object_layer : *support_layer;    
    coordf_t             print_z       = layer.print_z;
    bool                 first_layer   = layer.id() == 0;
    unsigned int         first_extruder_id = layer_tools.extruders.front();

    // Initialize config with the 1st object to be printed at this layer.
    m_config.apply(layer.object()->config, true);

    // Check whether it is possible to apply the spiral vase logic for this layer.
    // Just a reminder: A spiral vase mode is allowed for a single object, single material print only.
    if (m_spiral_vase && layers.size() == 1 && support_layer == nullptr) {
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
    m_enable_loop_clipping = ! m_spiral_vase || ! m_spiral_vase->enable;
    
    std::string gcode;

    // Set new layer - this will change Z and force a retraction if retract_layer_change is enabled.
    if (! print.config.before_layer_gcode.value.empty()) {
        DynamicConfig config;
        config.set_key_value("layer_num", new ConfigOptionInt(m_layer_index + 1));
        config.set_key_value("layer_z",   new ConfigOptionFloat(print_z));
        gcode += this->placeholder_parser_process("before_layer_gcode",
            print.config.before_layer_gcode.value, m_writer.extruder()->id(), &config)
            + "\n";
    }
    gcode += this->change_layer(print_z);  // this will increase m_layer_index
	m_layer = &layer;
    if (! print.config.layer_gcode.value.empty()) {
        DynamicConfig config;
        config.set_key_value("layer_num", new ConfigOptionInt(m_layer_index));
        config.set_key_value("layer_z",   new ConfigOptionFloat(print_z));
        gcode += this->placeholder_parser_process("layer_gcode",
            print.config.layer_gcode.value, m_writer.extruder()->id(), &config)
            + "\n";
    }

    if (! first_layer && ! m_second_layer_things_done) {
        // Transition from 1st to 2nd layer. Adjust nozzle temperatures as prescribed by the nozzle dependent
        // first_layer_temperature vs. temperature settings.
        for (const Extruder &extruder : m_writer.extruders()) {
            if (print.config.single_extruder_multi_material.value && extruder.id() != m_writer.extruder()->id())
                // In single extruder multi material mode, set the temperature for the current extruder only.
                continue;
            int temperature = print.config.temperature.get_at(extruder.id());
            if (temperature > 0 && temperature != print.config.first_layer_temperature.get_at(extruder.id()))
                gcode += m_writer.set_temperature(temperature, false, extruder.id());
        }
        gcode += m_writer.set_bed_temperature(print.config.bed_temperature.get_at(first_extruder_id));
        // Mark the temperature transition from 1st to 2nd layer to be finished.
        m_second_layer_things_done = true;
    }

    // Extrude skirt at the print_z of the raft layers and normal object layers
    // not at the print_z of the interlaced support material layers.
    bool extrude_skirt = 
		! print.skirt.entities.empty() &&
        // Not enough skirt layers printed yet.
        (m_skirt_done.size() < print.config.skirt_height.value || print.has_infinite_skirt()) &&
        // This print_z has not been extruded yet
		(m_skirt_done.empty() ? 0. : m_skirt_done.back()) < print_z - EPSILON &&
        // and this layer is the 1st layer, or it is an object layer, or it is a raft layer.
        (first_layer || object_layer != nullptr || support_layer->id() < m_config.raft_layers.value);
    std::map<unsigned int, std::pair<size_t, size_t>> skirt_loops_per_extruder;
    coordf_t                                          skirt_height = 0.;
    if (extrude_skirt) {
        // Fill in skirt_loops_per_extruder.
		skirt_height = print_z - (m_skirt_done.empty() ? 0. : m_skirt_done.back());
        m_skirt_done.push_back(print_z);
        if (first_layer) {
            // Prime the extruders over the skirt lines.
            std::vector<unsigned int> extruder_ids = m_writer.extruder_ids();
            // Reorder the extruders, so that the last used extruder is at the front.
            for (size_t i = 1; i < extruder_ids.size(); ++ i)
                if (extruder_ids[i] == first_extruder_id) {
                    // Move the last extruder to the front.
                    memmove(extruder_ids.data() + 1, extruder_ids.data(), i * sizeof(unsigned int));
                    extruder_ids.front() = first_extruder_id;
                    break;
                }
            size_t n_loops = print.skirt.entities.size();
			if (n_loops <= extruder_ids.size()) {
				for (size_t i = 0; i < n_loops; ++i)
                    skirt_loops_per_extruder[extruder_ids[i]] = std::pair<size_t, size_t>(i, i + 1);
            } else {
                // Assign skirt loops to the extruders.
                std::vector<unsigned int> extruder_loops(extruder_ids.size(), 1);
                n_loops -= extruder_loops.size();
                while (n_loops > 0) {
                    for (size_t i = 0; i < extruder_ids.size() && n_loops > 0; ++ i, -- n_loops)
                        ++ extruder_loops[i];
                }
                for (size_t i = 0; i < extruder_ids.size(); ++ i)
                    skirt_loops_per_extruder[extruder_ids[i]] = std::make_pair<size_t, size_t>(
                        (i == 0) ? 0 : extruder_loops[i - 1], 
                        ((i == 0) ? 0 : extruder_loops[i - 1]) + extruder_loops[i]);
            }
        } else
            // Extrude all skirts with the current extruder.
            skirt_loops_per_extruder[first_extruder_id] = std::pair<size_t, size_t>(0, print.config.skirts.value);
    }

    // Group extrusions by an extruder, then by an object, an island and a region.
    std::map<unsigned int, std::vector<ObjectByExtruder>> by_extruder;
    
    for (const LayerToPrint &layer_to_print : layers) {
        if (layer_to_print.support_layer != nullptr) {
            const SupportLayer &support_layer = *layer_to_print.support_layer;
            const PrintObject  &object = *support_layer.object();
            if (! support_layer.support_fills.entities.empty()) {
                ExtrusionRole   role               = support_layer.support_fills.role();
                bool            has_support        = role == erMixed || role == erSupportMaterial;
                bool            has_interface      = role == erMixed || role == erSupportMaterialInterface;
                // Extruder ID of the support base. -1 if "don't care".
                unsigned int    support_extruder   = object.config.support_material_extruder.value - 1;
                // Shall the support be printed with the active extruder, preferably with non-soluble, to avoid tool changes?
                bool            support_dontcare   = object.config.support_material_extruder.value == 0;
                // Extruder ID of the support interface. -1 if "don't care".
                unsigned int    interface_extruder = object.config.support_material_interface_extruder.value - 1;
                // Shall the support interface be printed with the active extruder, preferably with non-soluble, to avoid tool changes?
                bool            interface_dontcare = object.config.support_material_interface_extruder.value == 0;
                if (support_dontcare || interface_dontcare) {
                    // Some support will be printed with "don't care" material, preferably non-soluble.
                    // Is the current extruder assigned a soluble filament?
                    unsigned int dontcare_extruder = first_extruder_id;
                    if (print.config.filament_soluble.get_at(dontcare_extruder)) {
                        // The last extruder printed on the previous layer extrudes soluble filament.
                        // Try to find a non-soluble extruder on the same layer.
                        for (unsigned int extruder_id : layer_tools.extruders)
                            if (! print.config.filament_soluble.get_at(extruder_id)) {
                                dontcare_extruder = extruder_id;
                                break;
                            }
                    }
                    if (support_dontcare)
                        support_extruder = dontcare_extruder;
                    if (interface_dontcare)
                        interface_extruder = dontcare_extruder;
                }
                // Both the support and the support interface are printed with the same extruder, therefore
                // the interface may be interleaved with the support base.
                bool single_extruder = ! has_support || support_extruder == interface_extruder;
                // Assign an extruder to the base.
                ObjectByExtruder &obj = object_by_extruder(by_extruder, has_support ? support_extruder : interface_extruder, &layer_to_print - layers.data(), layers.size());
                obj.support = &support_layer.support_fills;
                obj.support_extrusion_role = single_extruder ? erMixed : erSupportMaterial;
                if (! single_extruder && has_interface) {
                    ObjectByExtruder &obj_interface = object_by_extruder(by_extruder, interface_extruder, &layer_to_print - layers.data(), layers.size());
                    obj_interface.support = &support_layer.support_fills;
                    obj_interface.support_extrusion_role = erSupportMaterialInterface;
                }
            }
        }
        if (layer_to_print.object_layer != nullptr) {
            const Layer &layer = *layer_to_print.object_layer;
            // We now define a strategy for building perimeters and fills. The separation 
            // between regions doesn't matter in terms of printing order, as we follow 
            // another logic instead:
            // - we group all extrusions by extruder so that we minimize toolchanges
            // - we start from the last used extruder
            // - for each extruder, we group extrusions by island
            // - for each island, we extrude perimeters first, unless user set the infill_first
            //   option
            // (Still, we have to keep track of regions because we need to apply their config)
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
                    std::vector<ObjectByExtruder::Island> &islands = object_islands_by_extruder(
                        by_extruder,
                        std::max<int>(region.config.perimeter_extruder.value - 1, 0),
                        &layer_to_print - layers.data(),
                        layers.size(), n_slices+1);
                    for (size_t i = 0; i <= n_slices; ++ i)
                        if (// perimeter_coll->first_point does not fit inside any slice
                            i == n_slices ||
                            // perimeter_coll->first_point fits inside ith slice
                            point_inside_surface(i, perimeter_coll->first_point())) {
                            if (islands[i].by_region.empty())
                                islands[i].by_region.assign(print.regions.size(), ObjectByExtruder::Island::Region());
                            islands[i].by_region[region_id].perimeters.append(perimeter_coll->entities);
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
                    std::vector<ObjectByExtruder::Island> &islands = object_islands_by_extruder(
                        by_extruder,
                        extruder_id,
                        &layer_to_print - layers.data(),
                        layers.size(), n_slices+1);
                    for (size_t i = 0; i <= n_slices; ++i)
                        if (// fill->first_point does not fit inside any slice
                            i == n_slices ||
                            // fill->first_point fits inside ith slice
                            point_inside_surface(i, fill->first_point())) {
                            if (islands[i].by_region.empty())
                                islands[i].by_region.assign(print.regions.size(), ObjectByExtruder::Island::Region());
                            islands[i].by_region[region_id].infills.append(fill->entities);
                            break;
                        }
                }
            } // for regions
        }
    } // for objects

    // Extrude the skirt, brim, support, perimeters, infill ordered by the extruders.
    std::vector<std::unique_ptr<EdgeGrid::Grid>> lower_layer_edge_grids(layers.size());
    for (unsigned int extruder_id : layer_tools.extruders)
    {   
        gcode += (layer_tools.has_wipe_tower && m_wipe_tower) ?
            m_wipe_tower->tool_change(*this, extruder_id, extruder_id == layer_tools.extruders.back()) :
            this->set_extruder(extruder_id);

        if (extrude_skirt) {
            auto loops_it = skirt_loops_per_extruder.find(extruder_id);
            if (loops_it != skirt_loops_per_extruder.end()) {
                const std::pair<size_t, size_t> loops = loops_it->second;
                this->set_origin(0.,0.);
                m_avoid_crossing_perimeters.use_external_mp = true;
                Flow skirt_flow = print.skirt_flow();
                for (size_t i = loops.first; i < loops.second; ++ i) {
                    // Adjust flow according to this layer's layer height.
                    ExtrusionLoop loop = *dynamic_cast<const ExtrusionLoop*>(print.skirt.entities[i]);
                    Flow layer_skirt_flow(skirt_flow);
                    layer_skirt_flow.height = (float)skirt_height;
                    double mm3_per_mm = layer_skirt_flow.mm3_per_mm();
                    for (ExtrusionPath &path : loop.paths) {
                        path.height     = (float)layer.height;
                        path.mm3_per_mm = mm3_per_mm;
                    }                
                    gcode += this->extrude_loop(loop, "skirt", m_config.support_material_speed.value);
                }
                m_avoid_crossing_perimeters.use_external_mp = false;
                // Allow a straight travel move to the first object point if this is the first layer (but don't in next layers).
                if (first_layer && loops.first == 0)
                    m_avoid_crossing_perimeters.disable_once = true;
            }
        }
        
        // Extrude brim with the extruder of the 1st region.
        if (! m_brim_done) {
            this->set_origin(0., 0.);
            m_avoid_crossing_perimeters.use_external_mp = true;
            for (const ExtrusionEntity *ee : print.brim.entities)
                gcode += this->extrude_loop(*dynamic_cast<const ExtrusionLoop*>(ee), "brim", m_config.support_material_speed.value);
            m_brim_done = true;
            m_avoid_crossing_perimeters.use_external_mp = false;
            // Allow a straight travel move to the first object point.
            m_avoid_crossing_perimeters.disable_once = true;
        }

        auto objects_by_extruder_it = by_extruder.find(extruder_id);
        if (objects_by_extruder_it == by_extruder.end())
            continue;
        for (const ObjectByExtruder &object_by_extruder : objects_by_extruder_it->second) {
            const size_t       layer_id     = &object_by_extruder - objects_by_extruder_it->second.data();
            const PrintObject *print_object = layers[layer_id].object();
			if (print_object == nullptr)
				// This layer is empty for this particular object, it has neither object extrusions nor support extrusions at this print_z.
				continue;

            m_config.apply(print_object->config, true);
            m_layer = layers[layer_id].layer();
            if (m_config.avoid_crossing_perimeters)
                m_avoid_crossing_perimeters.init_layer_mp(union_ex(m_layer->slices, true));
            Points copies;
            if (single_object_idx == size_t(-1)) 
                copies = print_object->_shifted_copies;
            else
                copies.push_back(print_object->_shifted_copies[single_object_idx]);
            // Sort the copies by the closest point starting with the current print position.
            
            for (const Point &copy : copies) {
                // When starting a new object, use the external motion planner for the first travel move.
                std::pair<const PrintObject*, Point> this_object_copy(print_object, copy);
                if (m_last_obj_copy != this_object_copy)
                    m_avoid_crossing_perimeters.use_external_mp_once = true;
                m_last_obj_copy = this_object_copy;
                this->set_origin(unscale(copy.x), unscale(copy.y));
                if (object_by_extruder.support != nullptr) {
                    m_layer = layers[layer_id].support_layer;
                    gcode += this->extrude_support(
                        // support_extrusion_role is erSupportMaterial, erSupportMaterialInterface or erMixed for all extrusion paths.
                        object_by_extruder.support->chained_path_from(m_last_pos, false, object_by_extruder.support_extrusion_role));
                    m_layer = layers[layer_id].layer();
                }
                for (const ObjectByExtruder::Island &island : object_by_extruder.islands) {
                    if (print.config.infill_first) {
                        gcode += this->extrude_infill(print, island.by_region);
                        gcode += this->extrude_perimeters(print, island.by_region, lower_layer_edge_grids[layer_id]);
                    } else {
                        gcode += this->extrude_perimeters(print, island.by_region, lower_layer_edge_grids[layer_id]);
                        gcode += this->extrude_infill(print, island.by_region);
                    }
                }
            }
        }
    }

    // Apply spiral vase post-processing if this layer contains suitable geometry
    // (we must feed all the G-code into the post-processor, including the first 
    // bottom non-spiral layers otherwise it will mess with positions)
    // we apply spiral vase at this stage because it requires a full layer.
    // Just a reminder: A spiral vase mode is allowed for a single object per layer, single material print only.
    if (m_spiral_vase)
        gcode = m_spiral_vase->process_layer(gcode);

    // Apply cooling logic; this may alter speeds.
    if (m_cooling_buffer)
        gcode = m_cooling_buffer->process_layer(gcode, layer.id());

    // Apply pressure equalization if enabled;
    // printf("G-code before filter:\n%s\n", gcode.c_str());
    if (m_pressure_equalizer)
        gcode = m_pressure_equalizer->process(gcode.c_str(), false);
    // printf("G-code after filter:\n%s\n", out.c_str());

    _write(file, gcode);
}

void GCode::apply_print_config(const PrintConfig &print_config)
{
    m_writer.apply_print_config(print_config);
    m_config.apply(print_config);
}

void GCode::append_full_config(const Print& print, std::string& str)
{
    char buff[4096];

    const StaticPrintConfig *configs[] = { &print.config, &print.default_object_config, &print.default_region_config };
    for (size_t i = 0; i < sizeof(configs) / sizeof(configs[0]); ++i) {
        const StaticPrintConfig *cfg = configs[i];
        for (const std::string &key : cfg->keys())
        {
            if (key != "compatible_printers")
            {
                sprintf(buff, "; %s = %s\n", key.c_str(), cfg->serialize(key).c_str());
                str += buff;
            }
        }
    }
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

// called by GCode::process_layer()
std::string GCode::change_layer(coordf_t print_z)
{
    std::string gcode;
    if (m_layer_count > 0)
        // Increment a progress bar indicator.
        gcode += m_writer.update_progress(++ m_layer_index, m_layer_count);
    coordf_t z = print_z + m_config.z_offset.value;  // in unscaled coordinates
    if (EXTRUDER_CONFIG(retract_layer_change) && m_writer.will_move_z(z))
        gcode += this->retract();

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
    case erWipeTower:                   return "erWipeTower";
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

std::string GCode::extrude_loop(ExtrusionLoop loop, std::string description, double speed, std::unique_ptr<EdgeGrid::Grid> *lower_layer_edge_grid)
{
    // get a copy; don't modify the orientation of the original loop object otherwise
    // next copies (if any) would not detect the correct orientation

    if (m_layer->lower_layer != nullptr && lower_layer_edge_grid != nullptr) {
        if (! *lower_layer_edge_grid) {
            // Create the distance field for a layer below.
            const coord_t distance_field_resolution = coord_t(scale_(1.) + 0.5);
            *lower_layer_edge_grid = make_unique<EdgeGrid::Grid>();
            (*lower_layer_edge_grid)->create(m_layer->lower_layer->slices, distance_field_resolution);
            (*lower_layer_edge_grid)->calculate_sdf();
            #if 0
            {
                static int iRun = 0;
                BoundingBox bbox = (*lower_layer_edge_grid)->bbox();
                bbox.min.x -= scale_(5.f);
                bbox.min.y -= scale_(5.f);
                bbox.max.x += scale_(5.f);
                bbox.max.y += scale_(5.f);
                EdgeGrid::save_png(*(*lower_layer_edge_grid), bbox, scale_(0.1f), debug_out_path("GCode_extrude_loop_edge_grid-%d.png", iRun++));
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
        const coord_t  nozzle_r   = coord_t(scale_(0.5 * nozzle_dmr) + 0.5);

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
        std::vector<float> penalties = polygon_angles_at_vertices(polygon, lengths, float(nozzle_r));
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
                penalty = penaltyFlatSurface * bspline_kernel(ccwAngle * float(PI * 2. / 3.));
            } else {
                assert(ccwAngle >= 0.f);
                // Interpolate penalty between maximum and the penalty for a convex vertex.
                penalty = penaltyConvexVertex + (penaltyFlatSurface - penaltyConvexVertex) * bspline_kernel(ccwAngle * float(PI * 2. / 3.));
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
        if (lower_layer_edge_grid && (*lower_layer_edge_grid)) {
            // Use the edge grid distance field structure over the lower layer to calculate overhangs.
            coord_t nozzle_r = coord_t(floor(scale_(0.5 * nozzle_dmr) + 0.5));
            coord_t search_r = coord_t(floor(scale_(0.8 * nozzle_dmr) + 0.5));
            for (size_t i = 0; i < polygon.points.size(); ++ i) {
                const Point &p = polygon.points[i];
                coordf_t dist;
                // Signed distance is positive outside the object, negative inside the object.
                // The point is considered at an overhang, if it is more than nozzle radius
                // outside of the lower layer contour.
                bool found = (*lower_layer_edge_grid)->signed_distance(p, search_r, dist);
                // If the approximate Signed Distance Field was initialized over lower_layer_edge_grid,
                // then the signed distnace shall always be known.
                assert(found);
                penalties[i] += extrudate_overlap_penalty(float(nozzle_r), penaltyOverhangHalf, float(dist));
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
    gcode += m_writer.set_acceleration((unsigned int)(m_config.default_acceleration.value + 0.5));
    
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
        double distance = std::min<double>(
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

std::string GCode::extrude_multi_path(ExtrusionMultiPath multipath, std::string description, double speed)
{
    // extrude along the path
    std::string gcode;
    for (ExtrusionPath path : multipath.paths) {
//    description += ExtrusionLoopRole2String(loop.loop_role());
//    description += ExtrusionRole2String(path->role);
        path.simplify(SCALED_RESOLUTION);
        gcode += this->_extrude(path, description, speed);
    }
    if (m_wipe.enable) {
        m_wipe.path = std::move(multipath.paths.back().polyline);  // TODO: don't limit wipe to last path
        m_wipe.path.reverse();
    }
    // reset acceleration
    gcode += m_writer.set_acceleration((unsigned int)floor(m_config.default_acceleration.value + 0.5));
    return gcode;
}

std::string GCode::extrude_entity(const ExtrusionEntity &entity, std::string description, double speed, std::unique_ptr<EdgeGrid::Grid> *lower_layer_edge_grid)
{
    if (const ExtrusionPath* path = dynamic_cast<const ExtrusionPath*>(&entity))
        return this->extrude_path(*path, description, speed);
    else if (const ExtrusionMultiPath* multipath = dynamic_cast<const ExtrusionMultiPath*>(&entity))
        return this->extrude_multi_path(*multipath, description, speed);
    else if (const ExtrusionLoop* loop = dynamic_cast<const ExtrusionLoop*>(&entity))
        return this->extrude_loop(*loop, description, speed, lower_layer_edge_grid);
    else {
        CONFESS("Invalid argument supplied to extrude()");
        return "";
    }
}

std::string GCode::extrude_path(ExtrusionPath path, std::string description, double speed)
{
//    description += ExtrusionRole2String(path.role());
    path.simplify(SCALED_RESOLUTION);
    std::string gcode = this->_extrude(path, description, speed);
    if (m_wipe.enable) {
        m_wipe.path = std::move(path.polyline);
        m_wipe.path.reverse();
    }
    // reset acceleration
    gcode += m_writer.set_acceleration((unsigned int)floor(m_config.default_acceleration.value + 0.5));
    return gcode;
}

// Extrude perimeters: Decide where to put seams (hide or align seams).
std::string GCode::extrude_perimeters(const Print &print, const std::vector<ObjectByExtruder::Island::Region> &by_region, std::unique_ptr<EdgeGrid::Grid> &lower_layer_edge_grid)
{
    std::string gcode;
    for (const ObjectByExtruder::Island::Region &region : by_region) {
        m_config.apply(print.regions[&region - &by_region.front()]->config);
        for (ExtrusionEntity *ee : region.perimeters.entities)
            gcode += this->extrude_entity(*ee, "perimeter", -1., &lower_layer_edge_grid);
    }
    return gcode;
}

// Chain the paths hierarchically by a greedy algorithm to minimize a travel distance.
std::string GCode::extrude_infill(const Print &print, const std::vector<ObjectByExtruder::Island::Region> &by_region)
{
    std::string gcode;
    for (const ObjectByExtruder::Island::Region &region : by_region) {
        m_config.apply(print.regions[&region - &by_region.front()]->config);
		ExtrusionEntityCollection chained = region.infills.chained_path_from(m_last_pos, false);
        for (ExtrusionEntity *fill : chained.entities) {
            auto *eec = dynamic_cast<ExtrusionEntityCollection*>(fill);
            if (eec) {
				ExtrusionEntityCollection chained2 = eec->chained_path_from(m_last_pos, false);
				for (ExtrusionEntity *ee : chained2.entities)
                    gcode += this->extrude_entity(*ee, "infill");
            } else
                gcode += this->extrude_entity(*fill, "infill");
        }
    }
    return gcode;
}

std::string GCode::extrude_support(const ExtrusionEntityCollection &support_fills)
{
    std::string gcode;
    if (! support_fills.entities.empty()) {
        const char   *support_label            = "support material";
        const char   *support_interface_label  = "support material interface";
        const double  support_speed            = m_config.support_material_speed.value;
        const double  support_interface_speed  = m_config.support_material_interface_speed.get_abs_value(support_speed);
        for (const ExtrusionEntity *ee : support_fills.entities) {
            ExtrusionRole role = ee->role();
            assert(role == erSupportMaterial || role == erSupportMaterialInterface);
            const char  *label = (role == erSupportMaterial) ? support_label : support_interface_label;
            const double speed = (role == erSupportMaterial) ? support_speed : support_interface_speed;
            const ExtrusionPath *path = dynamic_cast<const ExtrusionPath*>(ee);
            if (path)
                gcode += this->extrude_path(*path, label, speed);
            else {
                const ExtrusionMultiPath *multipath = dynamic_cast<const ExtrusionMultiPath*>(ee);
                assert(multipath != nullptr);
                if (multipath)
                    gcode += this->extrude_multi_path(*multipath, label, speed);
            }
        }
    }
    return gcode;
}

void GCode::_write(FILE* file, const char *what)
{
    if (what != nullptr) {
        // apply analyzer, if enabled
        const char* gcode = m_enable_analyzer ? m_analyzer.process_gcode(what).c_str() : what;

        // writes string to file
        fwrite(gcode, 1, ::strlen(gcode), file);
        // updates time estimator and gcode lines vector
        m_time_estimator.add_gcode_block(gcode);
    }
}

void GCode::_writeln(FILE* file, const std::string &what)
{
    if (! what.empty())
        _write(file, (what.back() == '\n') ? what : (what + '\n'));
}

void GCode::_write_format(FILE* file, const char* format, ...)
{
    va_list args;
    va_start(args, format);

    int buflen;
    {
        va_list args2;
        va_copy(args2, args);
        buflen =
    #ifdef _MSC_VER
            ::_vscprintf(format, args2)
    #else
            ::vsnprintf(nullptr, 0, format, args2)
    #endif
            + 1;
        va_end(args2);
    }

    char buffer[1024];
    bool buffer_dynamic = buflen > 1024;
    char *bufptr = buffer_dynamic ? (char*)malloc(buflen) : buffer;
    int res = ::vsnprintf(bufptr, buflen, format, args);
    if (res > 0)
        _write(file, bufptr);

    if (buffer_dynamic)
        free(bufptr);

    va_end(args);
}

std::string GCode::_extrude(const ExtrusionPath &path, std::string description, double speed)
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
        if (this->on_first_layer() && m_config.first_layer_acceleration.value > 0) {
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
        gcode += m_writer.set_acceleration((unsigned int)floor(acceleration + 0.5));
    }
    
    // calculate extrusion length per distance unit
    double e_per_mm = m_writer.extruder()->e_per_mm3() * path.mm3_per_mm;
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
    if (this->on_first_layer())
        speed = m_config.get_abs_value("first_layer_speed", speed);
    if (m_volumetric_speed != 0. && speed == 0)
        speed = m_volumetric_speed / path.mm3_per_mm;
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
    if (m_enable_extrusion_role_markers || m_enable_analyzer)
    {
        if (path.role() != m_last_extrusion_role)
        {
            m_last_extrusion_role = path.role();
            if (m_enable_extrusion_role_markers)
            {
                char buf[32];
                sprintf(buf, ";_EXTRUSION_ROLE:%d\n", int(m_last_extrusion_role));
                gcode += buf;
            }
            if (m_enable_analyzer)
            {
                char buf[32];
                sprintf(buf, ";%s%d\n", GCodeAnalyzer::Extrusion_Role_Tag.c_str(), int(m_last_extrusion_role));
                gcode += buf;
            }
        }
    }

    // adds analyzer tags and updates analyzer's tracking data
    if (m_enable_analyzer)
    {
        if (m_last_mm3_per_mm != path.mm3_per_mm)
        {
            m_last_mm3_per_mm = path.mm3_per_mm;

            char buf[32];
            sprintf(buf, ";%s%f\n", GCodeAnalyzer::Mm3_Per_Mm_Tag.c_str(), m_last_mm3_per_mm);
            gcode += buf;
        }

        if (m_last_width != path.width)
        {
            m_last_width = path.width;

            char buf[32];
            sprintf(buf, ";%s%f\n", GCodeAnalyzer::Width_Tag.c_str(), m_last_width);
            gcode += buf;
        }

        if (m_last_height != path.height)
        {
            m_last_height = path.height;

            char buf[32];
            sprintf(buf, ";%s%f\n", GCodeAnalyzer::Height_Tag.c_str(), m_last_height);
            gcode += buf;
        }
    }

    std::string comment;
    if (m_enable_cooling_markers) {
        if (is_bridge(path.role()))
            gcode += ";_BRIDGE_FAN_START\n";
        else
            comment = ";_EXTRUDE_SET_SPEED";
        if (path.role() == erExternalPerimeter)
            comment += ";_EXTERNAL_PERIMETER";
    }
    // F is mm per minute.
    gcode += m_writer.set_speed(F, "", comment);
    double path_length = 0.;
    {
        std::string comment = m_config.gcode_comments ? description : "";
        for (const Line &line : path.polyline.lines()) {
            const double line_length = line.length() * SCALING_FACTOR;
            path_length += line_length;
            gcode += m_writer.extrude_to_xy(
                this->point_to_gcode(line.b),
                e_per_mm * line_length,
                comment);
        }
    }
    if (m_enable_cooling_markers)
        gcode += is_bridge(path.role()) ? ";_BRIDGE_FAN_END\n" : ";_EXTRUDE_END\n";
    
    this->set_last_pos(path.last_point());
    return gcode;
}

// This method accepts &point in print coordinates.
std::string GCode::travel_to(const Point &point, ExtrusionRole role, std::string comment)
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
    
    return gcode;
}

bool GCode::needs_retraction(const Polyline &travel, ExtrusionRole role)
{
    if (travel.length() < scale_(EXTRUDER_CONFIG(retract_before_travel))) {
        // skip retraction if the move is shorter than the configured threshold
        return false;
    }
    
    if (role == erSupportMaterial) {
        const SupportLayer* support_layer = dynamic_cast<const SupportLayer*>(m_layer);
        //FIXME support_layer->support_islands.contains should use some search structure!
        if (support_layer != NULL && support_layer->support_islands.contains(travel))
            // skip retraction if this is a travel move inside a support material island
            //FIXME not retracting over a long path may cause oozing, which in turn may result in missing material
            // at the end of the extrusion path!
            return false;
    }

    if (m_config.only_retract_when_crossing_perimeters && m_layer != nullptr &&
        m_config.fill_density.value > 0 && m_layer->any_internal_region_slice_contains(travel))
        // Skip retraction if travel is contained in an internal slice *and*
        // internal infill is enabled (so that stringing is entirely not visible).
        //FIXME any_internal_region_slice_contains() is potentionally very slow, it shall test for the bounding boxes first.
        return false;
    
    // retract if only_retract_when_crossing_perimeters is disabled or doesn't apply
    return true;
}

std::string GCode::retract(bool toolchange)
{
    std::string gcode;
    
    if (m_writer.extruder() == nullptr)
        return gcode;
    
    // wipe (if it's enabled for this extruder and we have a stored wipe path)
    if (EXTRUDER_CONFIG(wipe) && m_wipe.has_path()) {
        gcode += toolchange ? m_writer.retract_for_toolchange(true) : m_writer.retract(true);
        gcode += m_wipe.wipe(*this, toolchange);
    }
    
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

std::string GCode::set_extruder(unsigned int extruder_id)
{
    if (!m_writer.need_toolchange(extruder_id))
        return "";
    
    // if we are running a single-extruder setup, just set the extruder and return nothing
    if (!m_writer.multiple_extruders) {
        m_placeholder_parser.set("current_extruder", extruder_id);
        return m_writer.toolchange(extruder_id);
    }
    
    // prepend retraction on the current extruder
    std::string gcode = this->retract(true);

    // Always reset the extrusion path, even if the tool change retract is set to zero.
    m_wipe.reset_path();
    
    if (m_writer.extruder() != nullptr) {
        // Process the custom end_filament_gcode in case of single_extruder_multi_material.
        unsigned int        old_extruder_id     = m_writer.extruder()->id();
        const std::string  &end_filament_gcode  = m_config.end_filament_gcode.get_at(old_extruder_id);
        if (m_config.single_extruder_multi_material && ! end_filament_gcode.empty()) {
            gcode += placeholder_parser_process("end_filament_gcode", end_filament_gcode, old_extruder_id);
            check_add_eol(gcode);
        }
    }

    m_placeholder_parser.set("current_extruder", extruder_id);

    if (m_writer.extruder() != nullptr && ! m_config.toolchange_gcode.value.empty()) {
        // Process the custom toolchange_gcode.
        DynamicConfig config;
        config.set_key_value("previous_extruder", new ConfigOptionInt((int)m_writer.extruder()->id()));
        config.set_key_value("next_extruder",     new ConfigOptionInt((int)extruder_id));
        gcode += placeholder_parser_process("toolchange_gcode", m_config.toolchange_gcode.value, extruder_id, &config);
        check_add_eol(gcode);
    }
    
    // If ooze prevention is enabled, park current extruder in the nearest
    // standby point and set it to the standby temperature.
    if (m_ooze_prevention.enable && m_writer.extruder() != nullptr)
        gcode += m_ooze_prevention.pre_toolchange(*this);
    // Append the toolchange command.
    gcode += m_writer.toolchange(extruder_id);
    // Append the filament start G-code for single_extruder_multi_material.
    const std::string &start_filament_gcode = m_config.start_filament_gcode.get_at(extruder_id);
    if (m_config.single_extruder_multi_material && ! start_filament_gcode.empty()) {
        // Process the start_filament_gcode for the active filament only.
        gcode += this->placeholder_parser_process("start_filament_gcode", start_filament_gcode, extruder_id);
        check_add_eol(gcode);
    }
    // Set the new extruder to the operating temperature.
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
        unscale(point.y) + m_origin.y - extruder_offset.y);
}

// convert a model-space scaled point into G-code coordinates
Point GCode::gcode_to_point(const Pointf &point) const
{
    Pointf extruder_offset = EXTRUDER_CONFIG(extruder_offset);
    return Point(
        scale_(point.x - m_origin.x + extruder_offset.x),
        scale_(point.y - m_origin.y + extruder_offset.y));
}

}
