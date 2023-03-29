#include "libslic3r.h"
#include "I18N.hpp"
#include "GCode.hpp"
#include "Exception.hpp"
#include "ExtrusionEntity.hpp"
#include "EdgeGrid.hpp"
#include "Geometry/ConvexHull.hpp"
#include "GCode/PrintExtents.hpp"
#include "GCode/WipeTower.hpp"
#include "ShortestPath.hpp"
#include "Print.hpp"
#include "Utils.hpp"
#include "ClipperUtils.hpp"
#include "libslic3r.h"
#include "LocalesUtils.hpp"
#include "libslic3r/format.hpp"
#include "Time.hpp"
#include "GCode/ExtrusionProcessor.hpp"
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <math.h>
#include <utility>
#include <string_view>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/find.hpp>
#include <boost/foreach.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/trivial.hpp>
#include <boost/beast/core/detail/base64.hpp>

#include <boost/nowide/iostream.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/cstdlib.hpp>

#include "SVG.hpp"

#include <tbb/parallel_for.h>
#include "calib.hpp"
// Intel redesigned some TBB interface considerably when merging TBB with their oneAPI set of libraries, see GH #7332.
// We are using quite an old TBB 2017 U7. Before we update our build servers, let's use the old API, which is deprecated in up to date TBB.
#if ! defined(TBB_VERSION_MAJOR)
    #include <tbb/version.h>
#endif
#if ! defined(TBB_VERSION_MAJOR)
    static_assert(false, "TBB_VERSION_MAJOR not defined");
#endif
#if TBB_VERSION_MAJOR >= 2021
    #include <tbb/parallel_pipeline.h>
    using slic3r_tbb_filtermode = tbb::filter_mode;
#else
    #include <tbb/pipeline.h>
    using slic3r_tbb_filtermode = tbb::filter;
#endif

#include <Shiny/Shiny.h>

#include "miniz_extension.hpp"

using namespace std::literals::string_view_literals;

#if 0
// Enable debugging and asserts, even in the release build.
#define DEBUG
#define _DEBUG
#undef NDEBUG
#endif

#include <assert.h>

namespace Slic3r {

    //! macro used to mark string used at localization,
    //! return same string
#define L(s) (s)
#define _(s) Slic3r::I18N::translate(s)

static const float g_min_purge_volume = 100.f;
static const float g_purge_volume_one_time = 135.f;
static const int g_max_flush_count = 4;

Vec2d travel_point_1;
Vec2d travel_point_2;
Vec2d travel_point_3;

static std::vector<Vec2d> get_path_of_change_filament(const Print& print)
{
    // give safe value in case there is no start_end_points in config
    std::vector<Vec2d> out_points;
    out_points.emplace_back(Vec2d(54, 0));
    out_points.emplace_back(Vec2d(54, 0));
    out_points.emplace_back(Vec2d(54, 245));

    // get the start_end_points from config (20, -3) (54, 245)
    Pointfs points = print.config().start_end_points.values;
    if (points.size() != 2)
        return out_points;

    Vec2d start_point  = points[0];
    Vec2d end_point    = points[1];

    // the cutter area size(18, 28)
    Pointfs excluse_area = print.config().bed_exclude_area.values;
    if (excluse_area.size() != 4)
        return out_points;

    double cutter_area_x = excluse_area[2].x() + 2;
    double cutter_area_y = excluse_area[2].y() + 2;

    double start_x_position = start_point.x();
    double end_x_position   = end_point.x();
    double end_y_position   = end_point.y();

    bool can_travel_form_left = true;

    // step 1: get the x-range intervals of all objects
    std::vector<std::pair<double, double>> object_intervals;
    for (PrintObject *print_object : print.objects()) {
        const PrintInstances &print_instances = print_object->instances();
        BoundingBoxf3 bounding_box = print_instances[0].model_instance->get_object()->bounding_box();

        if (bounding_box.min.x() < start_x_position && bounding_box.min.y() < cutter_area_y)
            can_travel_form_left = false;

        std::pair<double, double> object_scope = std::make_pair(bounding_box.min.x() - 2, bounding_box.max.x() + 2);
        if (object_intervals.empty())
            object_intervals.push_back(object_scope);
        else {
            std::vector<std::pair<double, double>> new_object_intervals;
            bool intervals_intersect = false;
            std::pair<double, double>              new_merged_scope;
            for (auto object_interval : object_intervals) {
                if (object_interval.second >= object_scope.first && object_interval.first <= object_scope.second) {
                    if (intervals_intersect) {
                        new_merged_scope = std::make_pair(std::min(object_interval.first, new_merged_scope.first), std::max(object_interval.second, new_merged_scope.second));
                    } else { // it is the first intersection
                        new_merged_scope = std::make_pair(std::min(object_interval.first, object_scope.first), std::max(object_interval.second, object_scope.second));
                    }
                    intervals_intersect = true;
                } else {
                    new_object_intervals.push_back(object_interval);
                }
            }

            if (intervals_intersect) {
                new_object_intervals.push_back(new_merged_scope);
                object_intervals = new_object_intervals;
            } else
                object_intervals.push_back(object_scope);
        }
    }

    // step 2: get the available x-range
    std::sort(object_intervals.begin(), object_intervals.end(),
              [](const std::pair<double, double> &left, const std::pair<double, double> &right) {
            return left.first < right.first;
    });
    std::vector<std::pair<double, double>> available_intervals;
    double                                 start_position = 0;
    for (auto object_interval : object_intervals) {
        if (object_interval.first > start_position)
            available_intervals.push_back(std::make_pair(start_position, object_interval.first));
        start_position = object_interval.second;
    }
    available_intervals.push_back(std::make_pair(start_position, 255));

    // step 3: get the nearest path
    double new_path     = 255;
    for (auto available_interval : available_intervals) {
        if (available_interval.first > end_x_position) {
            double distance = available_interval.first - end_x_position;
            new_path        = abs(end_x_position - new_path) < distance ? new_path : available_interval.first;
            break;
        } else {
            if (available_interval.second >= end_x_position) {
                new_path = end_x_position;
                break;
            } else if (!can_travel_form_left && available_interval.second < start_x_position) {
                continue;
            } else {
                new_path     = available_interval.second;
            }
        }
    }

    // step 4: generate path points  (new_path == start_x_position means not need to change path)
    Vec2d out_point_1;
    Vec2d out_point_2;
    Vec2d out_point_3;
    if (new_path < start_x_position) {
        out_point_1 = Vec2d(start_x_position, cutter_area_y);
        out_point_2 = Vec2d(new_path, cutter_area_y);
        out_point_3 = Vec2d(new_path, end_y_position);
    } else {
        out_point_1 = Vec2d(new_path, 0);
        out_point_2 = Vec2d(new_path, 0);
        out_point_3 = Vec2d(new_path, end_y_position);
    }

    out_points.clear();
    out_points.emplace_back(out_point_1);
    out_points.emplace_back(out_point_2);
    out_points.emplace_back(out_point_3);

    return out_points;
}

// Only add a newline in case the current G-code does not end with a newline.
    static inline void check_add_eol(std::string& gcode)
    {
        if (!gcode.empty() && gcode.back() != '\n')
            gcode += '\n';
    }


    // Return true if tch_prefix is found in custom_gcode
    static bool custom_gcode_changes_tool(const std::string& custom_gcode, const std::string& tch_prefix, unsigned next_extruder)
    {
        bool ok = false;
        size_t from_pos = 0;
        size_t pos = 0;
        while ((pos = custom_gcode.find(tch_prefix, from_pos)) != std::string::npos) {
            if (pos + 1 == custom_gcode.size())
                break;
            from_pos = pos + 1;
            // only whitespace is allowed before the command
            while (--pos < custom_gcode.size() && custom_gcode[pos] != '\n') {
                if (!std::isspace(custom_gcode[pos]))
                    goto NEXT;
            }
            {
                // we should also check that the extruder changes to what was expected
                std::istringstream ss(custom_gcode.substr(from_pos, std::string::npos));
                unsigned num = 0;
                if (ss >> num)
                    ok = (num == next_extruder);
            }
        NEXT:;
        }
        return ok;
    }

    std::string OozePrevention::pre_toolchange(GCode& gcodegen)
    {
        std::string gcode;

        // move to the nearest standby point
        if (!this->standby_points.empty()) {
            // get current position in print coordinates
            Vec3d writer_pos = gcodegen.writer().get_position();
            Point pos = Point::new_scale(writer_pos(0), writer_pos(1));

            // find standby point
            Point standby_point;
            pos.nearest_point(this->standby_points, &standby_point);

            /*  We don't call gcodegen.travel_to() because we don't need retraction (it was already
                triggered by the caller) nor reduce_crossing_wall and also because the coordinates
                of the destination point must not be transformed by origin nor current extruder offset.  */
            gcode += gcodegen.writer().travel_to_xy(unscale(standby_point),
                "move to standby position");
        }

        if (gcodegen.config().standby_temperature_delta.value != 0) {
            // we assume that heating is always slower than cooling, so no need to block
            gcode += gcodegen.writer().set_temperature
            (this->_get_temp(gcodegen) + gcodegen.config().standby_temperature_delta.value, false, gcodegen.writer().extruder()->id());
        }

        return gcode;
    }

    std::string OozePrevention::post_toolchange(GCode& gcodegen)
    {
        return (gcodegen.config().standby_temperature_delta.value != 0) ?
            gcodegen.writer().set_temperature(this->_get_temp(gcodegen), true, gcodegen.writer().extruder()->id()) :
            std::string();
    }

    int
        OozePrevention::_get_temp(GCode& gcodegen)
    {
        return (gcodegen.layer() != NULL && gcodegen.layer()->id() == 0)
            ? gcodegen.config().nozzle_temperature_initial_layer.get_at(gcodegen.writer().extruder()->id())
            : gcodegen.config().nozzle_temperature.get_at(gcodegen.writer().extruder()->id());
    }

    std::string Wipe::wipe(GCode& gcodegen, bool toolchange, bool is_last)
    {
        std::string gcode;

        /*  Reduce feedrate a bit; travel speed is often too high to move on existing material.
            Too fast = ripping of existing material; too slow = short wipe path, thus more blob.  */
        double _wipe_speed = gcodegen.config().get_abs_value("wipe_speed");// gcodegen.writer().config.travel_speed.value * 0.8;
        if(gcodegen.config().role_based_wipe_speed)
            _wipe_speed = gcodegen.writer().get_current_speed() / 60.0;
        if(_wipe_speed < 10)
            _wipe_speed = 10;

        // get the retraction length
        double length = toolchange
            ? gcodegen.writer().extruder()->retract_length_toolchange()
            : gcodegen.writer().extruder()->retraction_length();
        // Shorten the retraction length by the amount already retracted before wipe.
        length *= (1. - gcodegen.writer().extruder()->retract_before_wipe());

        //SoftFever: allow 100% retract before wipe
        if (length >= 0)
        {
            /*  Calculate how long we need to travel in order to consume the required
                amount of retraction. In other words, how far do we move in XY at wipe_speed
                for the time needed to consume retraction_length at retraction_speed?  */
            // BBS
            double wipe_dist = scale_(gcodegen.config().wipe_distance.get_at(gcodegen.writer().extruder()->id()));

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
            if (!wipe_path.empty()) {
                // BBS. Handle short path case.
                if (wipe_path.length() < wipe_dist) {
                    wipe_dist = wipe_path.length();
                    //BBS: avoid to divide 0
                    wipe_dist = wipe_dist < EPSILON ? EPSILON : wipe_dist;
                }

                // add tag for processor
                gcode += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_Start) + "\n";
                //BBS: don't need to enable cooling makers when this is the last wipe. Because no more cooling layer will clean this "_WIPE"
                //Softfever: 
                std::string cooling_mark = "";
                if (gcodegen.enable_cooling_markers() && !is_last)
                    cooling_mark = /*gcodegen.config().role_based_wipe_speed ? ";_EXTERNAL_PERIMETER" : */";_WIPE";

                gcode += gcodegen.writer().set_speed(_wipe_speed * 60, "", cooling_mark);
                for (const Line& line : wipe_path.lines()) {
                    double segment_length = line.length();
                    /*  Reduce retraction length a bit to avoid effective retraction speed to be greater than the configured one
                        due to rounding (TODO: test and/or better math for this)  */
                    double dE = length * (segment_length / wipe_dist) * 0.95;
                    //BBS: fix this FIXME
                    //FIXME one shall not generate the unnecessary G1 Fxxx commands, here wipe_speed is a constant inside this cycle.
                    // Is it here for the cooling markers? Or should it be outside of the cycle?
                    //gcode += gcodegen.writer().set_speed(wipe_speed * 60, "", gcodegen.enable_cooling_markers() ? ";_WIPE" : "");
                    gcode += gcodegen.writer().extrude_to_xy(
                        gcodegen.point_to_gcode(line.b),
                        -dE,
                        "wipe and retract"
                    );
                }
                // add tag for processor
                gcode += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Wipe_End) + "\n";
                gcodegen.set_last_pos(wipe_path.points.back());
            }

            // prevent wiping again on same path
            this->reset_path();
        }

        return gcode;
    }

    static inline Point wipe_tower_point_to_object_point(GCode& gcodegen, const Vec2f& wipe_tower_pt)
    {
        return Point(scale_(wipe_tower_pt.x() - gcodegen.origin()(0)), scale_(wipe_tower_pt.y() - gcodegen.origin()(1)));
    }

    std::string WipeTowerIntegration::append_tcr(GCode& gcodegen, const WipeTower::ToolChangeResult& tcr, int new_extruder_id, double z) const
    {
        if (new_extruder_id != -1 && new_extruder_id != tcr.new_tool)
            throw Slic3r::InvalidArgument("Error: WipeTowerIntegration::append_tcr was asked to do a toolchange it didn't expect.");

        std::string gcode;

        // Toolchangeresult.gcode assumes the wipe tower corner is at the origin (except for priming lines)
        // We want to rotate and shift all extrusions (gcode postprocessing) and starting and ending position
        float alpha = m_wipe_tower_rotation / 180.f * float(M_PI);

        auto transform_wt_pt = [&alpha, this](const Vec2f& pt) -> Vec2f {
            Vec2f out = Eigen::Rotation2Df(alpha) * pt;
            out += m_wipe_tower_pos;
            return out;
        };

        Vec2f start_pos = tcr.start_pos;
        Vec2f end_pos = tcr.end_pos;
        if (! tcr.priming) {
            start_pos = transform_wt_pt(start_pos);
            end_pos = transform_wt_pt(end_pos);
        }

        Vec2f wipe_tower_offset = tcr.priming ? Vec2f::Zero() : m_wipe_tower_pos;
        float wipe_tower_rotation = tcr.priming ? 0.f : alpha;

        std::string tcr_rotated_gcode = post_process_wipe_tower_moves(tcr, wipe_tower_offset, wipe_tower_rotation);

        // BBS: add partplate logic
        Vec2f plate_origin_2d(m_plate_origin(0), m_plate_origin(1));

        // BBS: toolchange gcode will move to start_pos,
        // so only perform movement when printing sparse partition to support upper layer.
        // start_pos is the position in plate coordinate.
        if (! tcr.priming && tcr.is_finish_first) {
            // Move over the wipe tower.
            gcode += gcodegen.retract();
            gcodegen.m_avoid_crossing_perimeters.use_external_mp_once();
            gcode += gcodegen.travel_to(
                wipe_tower_point_to_object_point(gcodegen, start_pos + plate_origin_2d),
                erMixed,
                "Travel to a Wipe Tower");
            gcode += gcodegen.unretract();
        }

        double current_z = gcodegen.writer().get_position().z();
        if (z == -1.) // in case no specific z was provided, print at current_z pos
            z = current_z;
        if (! is_approx(z, current_z)) {
            gcode += gcodegen.writer().retract();
            gcode += gcodegen.writer().travel_to_z(z, "Travel down to the last wipe tower layer.");
            gcode += gcodegen.writer().unretract();
        }


        // Process the end filament gcode.
        std::string end_filament_gcode_str;
        if (gcodegen.writer().extruder() != nullptr) {
            // Process the custom filament_end_gcode in case of single_extruder_multi_material.
            unsigned int        old_extruder_id = gcodegen.writer().extruder()->id();
            const std::string& filament_end_gcode = gcodegen.config().filament_end_gcode.get_at(old_extruder_id);
            if (gcodegen.writer().extruder() != nullptr && !filament_end_gcode.empty()) {
                end_filament_gcode_str = gcodegen.placeholder_parser_process("filament_end_gcode", filament_end_gcode, old_extruder_id);
                check_add_eol(end_filament_gcode_str);
            }
        }
        //BBS: increase toolchange count
        gcodegen.m_toolchange_count++;

        // BBS: should be placed before toolchange parsing
        std::string toolchange_retract_str = gcodegen.retract(true, false);
        check_add_eol(toolchange_retract_str);

        // Process the custom change_filament_gcode. If it is empty, provide a simple Tn command to change the filament.
        // Otherwise, leave control to the user completely.
        std::string toolchange_gcode_str;
        const std::string& change_filament_gcode = gcodegen.config().change_filament_gcode.value;
//        m_max_layer_z = std::max(m_max_layer_z, tcr.print_z);
        if (! change_filament_gcode.empty()) {
            DynamicConfig config;
            int previous_extruder_id = gcodegen.writer().extruder() ? (int)gcodegen.writer().extruder()->id() : -1;
            config.set_key_value("previous_extruder", new ConfigOptionInt(previous_extruder_id));
            config.set_key_value("next_extruder", new ConfigOptionInt((int)new_extruder_id));
            config.set_key_value("layer_num", new ConfigOptionInt(gcodegen.m_layer_index));
            config.set_key_value("layer_z", new ConfigOptionFloat(tcr.print_z));
            config.set_key_value("toolchange_z", new ConfigOptionFloat(z));
//            config.set_key_value("max_layer_z", new ConfigOptionFloat(m_max_layer_z));
            // BBS
            {
                GCodeWriter& gcode_writer = gcodegen.m_writer;
                FullPrintConfig& full_config = gcodegen.m_config;
                float old_retract_length = gcode_writer.extruder() != nullptr ? full_config.retraction_length.get_at(previous_extruder_id) : 0;
                float new_retract_length = full_config.retraction_length.get_at(new_extruder_id);
                float old_retract_length_toolchange = gcode_writer.extruder() != nullptr ? full_config.retract_length_toolchange.get_at(previous_extruder_id) : 0;
                float new_retract_length_toolchange = full_config.retract_length_toolchange.get_at(new_extruder_id);
                int old_filament_temp = gcode_writer.extruder() != nullptr ? (gcodegen.on_first_layer()? full_config.nozzle_temperature_initial_layer.get_at(previous_extruder_id) : full_config.nozzle_temperature.get_at(previous_extruder_id)) : 210;
                int new_filament_temp = gcodegen.on_first_layer() ? full_config.nozzle_temperature_initial_layer.get_at(new_extruder_id) : full_config.nozzle_temperature.get_at(new_extruder_id);
                Vec3d nozzle_pos = gcode_writer.get_position();

                float purge_volume = tcr.purge_volume < EPSILON ? 0 : std::max(tcr.purge_volume, g_min_purge_volume);
                float filament_area = float((M_PI / 4.f) * pow(full_config.filament_diameter.get_at(new_extruder_id), 2));
                float purge_length = purge_volume / filament_area;

                int old_filament_e_feedrate = gcode_writer.extruder() != nullptr ? (int)(60.0 * full_config.filament_max_volumetric_speed.get_at(previous_extruder_id) / filament_area) : 200;
                old_filament_e_feedrate = old_filament_e_feedrate == 0 ? 100 : old_filament_e_feedrate;
                int new_filament_e_feedrate = (int)(60.0 * full_config.filament_max_volumetric_speed.get_at(new_extruder_id) / filament_area);
                new_filament_e_feedrate = new_filament_e_feedrate == 0 ? 100 : new_filament_e_feedrate;

                config.set_key_value("max_layer_z", new ConfigOptionFloat(gcodegen.m_max_layer_z));
                config.set_key_value("toolchange_count", new ConfigOptionInt((int)gcodegen.m_toolchange_count));
                //BBS: fan speed is useless placeholer now, but we don't remove it to avoid
                //slicing error in old change_filament_gcode in old 3MF
                config.set_key_value("fan_speed", new ConfigOptionInt((int)0));
                config.set_key_value("old_retract_length", new ConfigOptionFloat(old_retract_length));
                config.set_key_value("new_retract_length", new ConfigOptionFloat(new_retract_length));
                config.set_key_value("old_retract_length_toolchange", new ConfigOptionFloat(old_retract_length_toolchange));
                config.set_key_value("new_retract_length_toolchange", new ConfigOptionFloat(new_retract_length_toolchange));
                config.set_key_value("old_filament_temp", new ConfigOptionInt(old_filament_temp));
                config.set_key_value("new_filament_temp", new ConfigOptionInt(new_filament_temp));
                config.set_key_value("x_after_toolchange", new ConfigOptionFloat(start_pos(0)));
                config.set_key_value("y_after_toolchange", new ConfigOptionFloat(start_pos(1)));
                config.set_key_value("z_after_toolchange", new ConfigOptionFloat(nozzle_pos(2)));
                config.set_key_value("first_flush_volume", new ConfigOptionFloat(purge_length / 2.f));
                config.set_key_value("second_flush_volume", new ConfigOptionFloat(purge_length / 2.f));
                config.set_key_value("old_filament_e_feedrate", new ConfigOptionInt(old_filament_e_feedrate));
                config.set_key_value("new_filament_e_feedrate", new ConfigOptionInt(new_filament_e_feedrate));
                config.set_key_value("travel_point_1_x", new ConfigOptionFloat(float(travel_point_1.x())));
                config.set_key_value("travel_point_1_y", new ConfigOptionFloat(float(travel_point_1.y())));
                config.set_key_value("travel_point_2_x", new ConfigOptionFloat(float(travel_point_2.x())));
                config.set_key_value("travel_point_2_y", new ConfigOptionFloat(float(travel_point_2.y())));
                config.set_key_value("travel_point_3_x", new ConfigOptionFloat(float(travel_point_3.x())));
                config.set_key_value("travel_point_3_y", new ConfigOptionFloat(float(travel_point_3.y())));

                int flush_count = std::min(g_max_flush_count, (int)std::round(purge_volume / g_purge_volume_one_time));
                float flush_unit = purge_length / flush_count;
                int flush_idx = 0;
                for (; flush_idx < flush_count; flush_idx++) {
                    char key_value[64] = { 0 };
                    snprintf(key_value, sizeof(key_value), "flush_length_%d", flush_idx + 1);
                    config.set_key_value(key_value, new ConfigOptionFloat(flush_unit));
                }

                for (; flush_idx < g_max_flush_count; flush_idx++) {
                    char key_value[64] = { 0 };
                    snprintf(key_value, sizeof(key_value), "flush_length_%d", flush_idx + 1);
                    config.set_key_value(key_value, new ConfigOptionFloat(0.f));
                }
            }
            toolchange_gcode_str = gcodegen.placeholder_parser_process("change_filament_gcode", change_filament_gcode, new_extruder_id, &config);
            check_add_eol(toolchange_gcode_str);

            // retract before toolchange
            toolchange_gcode_str = toolchange_retract_str + toolchange_gcode_str;
            //BBS: current position and fan_speed is unclear after interting change_filament_gcode
            toolchange_gcode_str += ";_FORCE_RESUME_FAN_SPEED\n";
            gcodegen.writer().set_current_position_clear(false);

            // move to start_pos for wiping after toolchange
            std::string start_pos_str;
            start_pos_str = gcodegen.travel_to(wipe_tower_point_to_object_point(gcodegen, start_pos + plate_origin_2d), erMixed, "Move to start pos");
            check_add_eol(start_pos_str);
            toolchange_gcode_str += start_pos_str;

            // unretract before wiping
            toolchange_gcode_str += gcodegen.unretract();
            check_add_eol(toolchange_gcode_str);
        }

        std::string toolchange_command;
        if (tcr.priming || (new_extruder_id >= 0 && gcodegen.writer().need_toolchange(new_extruder_id)))
            toolchange_command = gcodegen.writer().toolchange(new_extruder_id);
        if (!custom_gcode_changes_tool(toolchange_gcode_str, gcodegen.writer().toolchange_prefix(), new_extruder_id))
            toolchange_gcode_str += toolchange_command;
        else {
            // We have informed the m_writer about the current extruder_id, we can ignore the generated G-code.
        }

        gcodegen.placeholder_parser().set("current_extruder", new_extruder_id);

        // Process the start filament gcode.
        std::string start_filament_gcode_str;
        const std::string& filament_start_gcode = gcodegen.config().filament_start_gcode.get_at(new_extruder_id);
        if (!filament_start_gcode.empty()) {
            // Process the filament_start_gcode for the active filament only.
            DynamicConfig config;
            config.set_key_value("filament_extruder_id", new ConfigOptionInt(new_extruder_id));
            start_filament_gcode_str = gcodegen.placeholder_parser_process("filament_start_gcode", filament_start_gcode, new_extruder_id, &config);
            check_add_eol(start_filament_gcode_str);
        }

        // Insert the end filament, toolchange, and start filament gcode into the generated gcode.
        DynamicConfig config;
        config.set_key_value("filament_end_gcode", new ConfigOptionString(end_filament_gcode_str));
        config.set_key_value("change_filament_gcode", new ConfigOptionString(toolchange_gcode_str));
        config.set_key_value("filament_start_gcode", new ConfigOptionString(start_filament_gcode_str));
        std::string tcr_gcode, tcr_escaped_gcode = gcodegen.placeholder_parser_process("tcr_rotated_gcode", tcr_rotated_gcode, new_extruder_id, &config);
        unescape_string_cstyle(tcr_escaped_gcode, tcr_gcode);
        gcode += tcr_gcode;
        check_add_eol(toolchange_gcode_str);

        //SoftFever: set new PA for new filament
        if (gcodegen.config().enable_pressure_advance.get_at(new_extruder_id)) {
            gcode += gcodegen.writer().set_pressure_advance(gcodegen.config().pressure_advance.get_at(new_extruder_id));
        }

        // A phony move to the end position at the wipe tower.
        gcodegen.writer().travel_to_xy(end_pos.cast<double>());
        gcodegen.set_last_pos(wipe_tower_point_to_object_point(gcodegen, end_pos + plate_origin_2d));
        if (!is_approx(z, current_z)) {
            gcode += gcodegen.writer().retract();
            gcode += gcodegen.writer().travel_to_z(current_z, "Travel back up to the topmost object layer.");
            gcode += gcodegen.writer().unretract();
        }

        else {
            // Prepare a future wipe.
            gcodegen.m_wipe.reset_path();
            for (const Vec2f& wipe_pt : tcr.wipe_path)
                gcodegen.m_wipe.path.points.emplace_back(wipe_tower_point_to_object_point(gcodegen, transform_wt_pt(wipe_pt)));
        }

        // Let the planner know we are traveling between objects.
        gcodegen.m_avoid_crossing_perimeters.use_external_mp_once();
        return gcode;
    }

    // This function postprocesses gcode_original, rotates and moves all G1 extrusions and returns resulting gcode
    // Starting position has to be supplied explicitely (otherwise it would fail in case first G1 command only contained one coordinate)
    std::string WipeTowerIntegration::post_process_wipe_tower_moves(const WipeTower::ToolChangeResult& tcr, const Vec2f& translation, float angle) const
    {
        Vec2f extruder_offset;
        if (m_single_extruder_multi_material)
            extruder_offset = m_extruder_offsets[0].cast<float>();
        else
            extruder_offset = m_extruder_offsets[tcr.initial_tool].cast<float>();

        std::istringstream gcode_str(tcr.gcode);
        std::string gcode_out;
        std::string line;
        Vec2f pos = tcr.start_pos;
        Vec2f transformed_pos = pos;
        Vec2f old_pos(-1000.1f, -1000.1f);

        while (gcode_str) {
            std::getline(gcode_str, line);  // we read the gcode line by line

            // All G1 commands should be translated and rotated. X and Y coords are
            // only pushed to the output when they differ from last time.
            // WT generator can override this by appending the never_skip_tag
            if (line.find("G1 ") == 0) {
                bool never_skip = false;
                auto it = line.find(WipeTower::never_skip_tag());
                if (it != std::string::npos) {
                    // remove the tag and remember we saw it
                    never_skip = true;
                    line.erase(it, it + WipeTower::never_skip_tag().size());
                }
                std::ostringstream line_out;
                std::istringstream line_str(line);
                line_str >> std::noskipws;  // don't skip whitespace
                char ch = 0;
                while (line_str >> ch) {
                    if (ch == 'X' || ch == 'Y')
                        line_str >> (ch == 'X' ? pos.x() : pos.y());
                    else
                        line_out << ch;
                }

                transformed_pos = Eigen::Rotation2Df(angle) * pos + translation;

                if (transformed_pos != old_pos || never_skip) {
                    line = line_out.str();
                    std::ostringstream oss;
                    oss << std::fixed << std::setprecision(3) << "G1 ";
                    if (transformed_pos.x() != old_pos.x() || never_skip)
                        oss << " X" << transformed_pos.x() - extruder_offset.x();
                    if (transformed_pos.y() != old_pos.y() || never_skip)
                        oss << " Y" << transformed_pos.y() - extruder_offset.y();
                    oss << " ";
                    line.replace(line.find("G1 "), 3, oss.str());
                    old_pos = transformed_pos;
                }
            }

            gcode_out += line + "\n";

            // If this was a toolchange command, we should change current extruder offset
            if (line == "[change_filament_gcode]") {
                // BBS
                if (!m_single_extruder_multi_material) {
                    extruder_offset = m_extruder_offsets[tcr.new_tool].cast<float>();

                    // If the extruder offset changed, add an extra move so everything is continuous
                    if (extruder_offset != m_extruder_offsets[tcr.initial_tool].cast<float>()) {
                        std::ostringstream oss;
                        oss << std::fixed << std::setprecision(3)
                            << "G1 X" << transformed_pos.x() - extruder_offset.x()
                            << " Y" << transformed_pos.y() - extruder_offset.y()
                            << "\n";
                        gcode_out += oss.str();
                    }
                }
            }
        }
        return gcode_out;
    }


    std::string WipeTowerIntegration::prime(GCode& gcodegen)
    {
        std::string gcode;
#if 0
        for (const WipeTower::ToolChangeResult& tcr : m_priming) {
            if (! tcr.extrusions.empty())
                gcode += append_tcr(gcodegen, tcr, tcr.new_tool);
        }
#endif
        return gcode;
    }

    std::string WipeTowerIntegration::tool_change(GCode& gcodegen, int extruder_id, bool finish_layer)
    {
        std::string gcode;

        assert(m_layer_idx >= 0);
        if (m_layer_idx >= (int) m_tool_changes.size()) return gcode;

        // Calculate where the wipe tower layer will be printed. -1 means that print z will not change,
        // resulting in a wipe tower with sparse layers.
        double wipe_tower_z  = -1;
        bool   ignore_sparse = false;
        if (gcodegen.config().wipe_tower_no_sparse_layers.value) {
            wipe_tower_z  = m_last_wipe_tower_print_z;
            ignore_sparse = (m_tool_changes[m_layer_idx].size() == 1 && m_tool_changes[m_layer_idx].front().initial_tool == m_tool_changes[m_layer_idx].front().new_tool);
            if (m_tool_change_idx == 0 && !ignore_sparse)
                wipe_tower_z = m_last_wipe_tower_print_z + m_tool_changes[m_layer_idx].front().layer_height;
        }

        if (m_enable_timelapse_print && m_is_first_print) {
            gcode += append_tcr(gcodegen, m_tool_changes[m_layer_idx][0], m_tool_changes[m_layer_idx][0].new_tool, wipe_tower_z);
            m_tool_change_idx++;
            m_is_first_print = false;
        }

        if (gcodegen.writer().need_toolchange(extruder_id) || finish_layer) {
            if (!(size_t(m_tool_change_idx) < m_tool_changes[m_layer_idx].size())) throw Slic3r::RuntimeError("Wipe tower generation failed, possibly due to empty first layer.");

            if (!ignore_sparse) {
                gcode += append_tcr(gcodegen, m_tool_changes[m_layer_idx][m_tool_change_idx++], extruder_id, wipe_tower_z);
                m_last_wipe_tower_print_z = wipe_tower_z;
            }
        }

        return gcode;
    }

    // Print is finished. Now it remains to unload the filament safely with ramming over the wipe tower.
    std::string WipeTowerIntegration::finalize(GCode& gcodegen)
    {
        std::string gcode;
        // BBS
#if 0
        if (std::abs(gcodegen.writer().get_position()(2) - m_final_purge.print_z) > EPSILON)
            gcode += gcodegen.change_layer(m_final_purge.print_z);
        gcode += append_tcr(gcodegen, m_final_purge, -1);
#endif
        return gcode;
    }

    const std::vector<std::string> ColorPrintColors::Colors = { "#C0392B", "#E67E22", "#F1C40F", "#27AE60", "#1ABC9C", "#2980B9", "#9B59B6" };

#define EXTRUDER_CONFIG(OPT) m_config.OPT.get_at(m_writer.extruder()->id())

// Collect pairs of object_layer + support_layer sorted by print_z.
// object_layer & support_layer are considered to be on the same print_z, if they are not further than EPSILON.
std::vector<GCode::LayerToPrint> GCode::collect_layers_to_print(const PrintObject& object)
{
    std::vector<GCode::LayerToPrint> layers_to_print;
    layers_to_print.reserve(object.layers().size() + object.support_layers().size());

    /*
    // Calculate a minimum support layer height as a minimum over all extruders, but not smaller than 10um.
    // This is the same logic as in support generator.
    //FIXME should we use the printing extruders instead?
    double gap_over_supports = object.config().support_top_z_distance;
    // FIXME should we test object.config().support_material_synchronize_layers ? Currently the support layers are synchronized with object layers iff soluble supports.
    assert(!object.has_support() || gap_over_supports != 0. || object.config().support_material_synchronize_layers);
    if (gap_over_supports != 0.) {
        gap_over_supports = std::max(0., gap_over_supports);
        // Not a soluble support,
        double support_layer_height_min = 1000000.;
        for (auto lh : object.print()->config().min_layer_height.values)
            support_layer_height_min = std::min(support_layer_height_min, std::max(0.01, lh));
        gap_over_supports += support_layer_height_min;
    }*/

    std::vector<std::pair<double, double>> warning_ranges;

    // Pair the object layers with the support layers by z.
    size_t idx_object_layer = 0;
    size_t idx_support_layer = 0;
    const LayerToPrint* last_extrusion_layer = nullptr;
    while (idx_object_layer < object.layers().size() || idx_support_layer < object.support_layers().size()) {
        LayerToPrint layer_to_print;
        double print_z_min = std::numeric_limits<double>::max();
        if (idx_object_layer < object.layers().size()) {
            layer_to_print.object_layer = object.layers()[idx_object_layer++];
            print_z_min = std::min(print_z_min, layer_to_print.object_layer->print_z);
        }

        if (idx_support_layer < object.support_layers().size()) {
            layer_to_print.support_layer = object.support_layers()[idx_support_layer++];
            print_z_min = std::min(print_z_min, layer_to_print.support_layer->print_z);
        }

        if (layer_to_print.object_layer && layer_to_print.object_layer->print_z > print_z_min + EPSILON) {
            layer_to_print.object_layer = nullptr;
            --idx_object_layer;
        }

        if (layer_to_print.support_layer && layer_to_print.support_layer->print_z > print_z_min + EPSILON) {
            layer_to_print.support_layer = nullptr;
            --idx_support_layer;
        }

        layer_to_print.original_object = &object;
        layers_to_print.push_back(layer_to_print);

        bool has_extrusions = (layer_to_print.object_layer && layer_to_print.object_layer->has_extrusions())
            || (layer_to_print.support_layer && layer_to_print.support_layer->has_extrusions());

        // Check that there are extrusions on the very first layer. The case with empty
        // first layer may result in skirt/brim in the air and maybe other issues.
        if (layers_to_print.size() == 1u) {
            if (!has_extrusions)
                throw Slic3r::SlicingError(_(L("One object has empty initial layer and can't be printed. Please Cut the bottom or enable supports.")), object.id().id);
        }

        // In case there are extrusions on this layer, check there is a layer to lay it on.
        if ((layer_to_print.object_layer && layer_to_print.object_layer->has_extrusions())
            // Allow empty support layers, as the support generator may produce no extrusions for non-empty support regions.
            || (layer_to_print.support_layer /* && layer_to_print.support_layer->has_extrusions() */)) {
            double top_cd = object.config().support_top_z_distance;
            //double bottom_cd = object.config().support_bottom_z_distance == 0. ? top_cd : object.config().support_bottom_z_distance;
            double bottom_cd = top_cd;

            double extra_gap = (layer_to_print.support_layer ? bottom_cd : top_cd);

            double maximal_print_z = (last_extrusion_layer ? last_extrusion_layer->print_z() : 0.)
                + layer_to_print.layer()->height
                + std::max(0., extra_gap);
            // Negative support_contact_z is not taken into account, it can result in false positives in cases

            if (has_extrusions && layer_to_print.print_z() > maximal_print_z + 2. * EPSILON)
                warning_ranges.emplace_back(std::make_pair((last_extrusion_layer ? last_extrusion_layer->print_z() : 0.), layers_to_print.back().print_z()));
        }
        // Remember last layer with extrusions.
        if (has_extrusions)
            last_extrusion_layer = &layers_to_print.back();
    }

    if (! warning_ranges.empty()) {
        std::string warning;
        size_t i = 0;
        for (i = 0; i < std::min(warning_ranges.size(), size_t(5)); ++i)
            warning += Slic3r::format(_(L("Object can't be printed for empty layer between %1% and %2%.")),
                                      warning_ranges[i].first, warning_ranges[i].second) + "\n";
        warning += Slic3r::format(_(L("Object: %1%")), object.model_object()->name) + "\n"
            + _(L("Maybe parts of the object at these height are too thin, or the object has faulty mesh"));

        const_cast<Print*>(object.print())->active_step_add_warning(
            PrintStateBase::WarningLevel::CRITICAL, warning, PrintStateBase::SlicingEmptyGcodeLayers);
    }

    return layers_to_print;
}

// Prepare for non-sequential printing of multiple objects: Support resp. object layers with nearly identical print_z
// will be printed for  all objects at once.
// Return a list of <print_z, per object LayerToPrint> items.
std::vector<std::pair<coordf_t, std::vector<GCode::LayerToPrint>>> GCode::collect_layers_to_print(const Print& print)
{
    struct OrderingItem {
        coordf_t    print_z;
        size_t      object_idx;
        size_t      layer_idx;
    };

    std::vector<std::vector<LayerToPrint>>  per_object(print.objects().size(), std::vector<LayerToPrint>());
    std::vector<OrderingItem>               ordering;
    for (size_t i = 0; i < print.objects().size(); ++i) {
        per_object[i] = collect_layers_to_print(*print.objects()[i]);
        OrderingItem ordering_item;
        ordering_item.object_idx = i;
        ordering.reserve(ordering.size() + per_object[i].size());
        const LayerToPrint& front = per_object[i].front();
        for (const LayerToPrint& ltp : per_object[i]) {
            ordering_item.print_z = ltp.print_z();
            ordering_item.layer_idx = &ltp - &front;
            ordering.emplace_back(ordering_item);
        }
    }

    std::sort(ordering.begin(), ordering.end(), [](const OrderingItem& oi1, const OrderingItem& oi2) { return oi1.print_z < oi2.print_z; });

    std::vector<std::pair<coordf_t, std::vector<LayerToPrint>>> layers_to_print;

    // Merge numerically very close Z values.
    for (size_t i = 0; i < ordering.size();) {
        // Find the last layer with roughly the same print_z.
        size_t j = i + 1;
        coordf_t zmax = ordering[i].print_z + EPSILON;
        for (; j < ordering.size() && ordering[j].print_z <= zmax; ++j);
        // Merge into layers_to_print.
        std::pair<coordf_t, std::vector<LayerToPrint>> merged;
        // Assign an average print_z to the set of layers with nearly equal print_z.
        merged.first = 0.5 * (ordering[i].print_z + ordering[j - 1].print_z);
        merged.second.assign(print.objects().size(), LayerToPrint());
        for (; i < j; ++i) {
            const OrderingItem& oi = ordering[i];
            assert(merged.second[oi.object_idx].layer() == nullptr);
            merged.second[oi.object_idx] = std::move(per_object[oi.object_idx][oi.layer_idx]);
        }
        layers_to_print.emplace_back(std::move(merged));
    }

    return layers_to_print;
}

// free functions called by GCode::do_export()
namespace DoExport {
//    static void update_print_estimated_times_stats(const GCodeProcessor& processor, PrintStatistics& print_statistics)
//    {
//        const GCodeProcessorResult& result = processor.get_result();
//        print_statistics.estimated_normal_print_time = get_time_dhms(result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time);
//        print_statistics.estimated_silent_print_time = processor.is_stealth_time_estimator_enabled() ?
//            get_time_dhms(result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].time) : "N/A";
//    }

    static void update_print_estimated_stats(const GCodeProcessor& processor, const std::vector<Extruder>& extruders, PrintStatistics& print_statistics)
    {
        const GCodeProcessorResult& result = processor.get_result();
        print_statistics.estimated_normal_print_time = get_time_dhms(result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time);
        print_statistics.estimated_silent_print_time = processor.is_stealth_time_estimator_enabled() ?
            get_time_dhms(result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].time) : "N/A";

        // update filament statictics
        double total_extruded_volume = 0.0;
        double total_used_filament   = 0.0;
        double total_weight          = 0.0;
        double total_cost            = 0.0;
        for (auto volume : result.print_statistics.volumes_per_extruder) {
            total_extruded_volume += volume.second;

            size_t extruder_id = volume.first;
            auto extruder = std::find_if(extruders.begin(), extruders.end(), [extruder_id](const Extruder& extr) { return extr.id() == extruder_id; });
            if (extruder == extruders.end())
                continue;

            double s = PI * sqr(0.5* extruder->filament_diameter());
            double weight = volume.second * extruder->filament_density() * 0.001;
            total_used_filament += volume.second/s;
            total_weight        += weight;
            total_cost          += weight * extruder->filament_cost() * 0.001;
        }
        //BBS: add flush volume
        for (auto volume : result.print_statistics.flush_per_filament) {
            total_extruded_volume += volume.second;

            size_t extruder_id = volume.first;
            auto extruder = std::find_if(extruders.begin(), extruders.end(), [extruder_id](const Extruder& extr) { return extr.id() == extruder_id; });
            if (extruder == extruders.end())
                continue;

            double s = PI * sqr(0.5* extruder->filament_diameter());
            double weight = volume.second * extruder->filament_density() * 0.001;
            total_used_filament += volume.second/s;
            total_weight        += weight;
            total_cost          += weight * extruder->filament_cost() * 0.001;
        }

        print_statistics.total_extruded_volume = total_extruded_volume;
        print_statistics.total_used_filament   = total_used_filament;
        print_statistics.total_weight          = total_weight;
        print_statistics.total_cost            = total_cost;

        print_statistics.filament_stats = result.print_statistics.volumes_per_extruder;
    }

    // if any reserved keyword is found, returns a std::vector containing the first MAX_COUNT keywords found
    // into pairs containing:
    // first: source
    // second: keyword
    // to be shown in the warning notification
    // The returned vector is empty if no keyword has been found
    static std::vector<std::pair<std::string, std::string>> validate_custom_gcode(const Print& print) {
        static const unsigned int MAX_TAGS_COUNT = 5;
        std::vector<std::pair<std::string, std::string>> ret;

        auto check = [&ret](const std::string& source, const std::string& gcode) {
            std::vector<std::string> tags;
            if (GCodeProcessor::contains_reserved_tags(gcode, MAX_TAGS_COUNT, tags)) {
                if (!tags.empty()) {
                    size_t i = 0;
                    while (ret.size() < MAX_TAGS_COUNT && i < tags.size()) {
                        ret.push_back({ source, tags[i] });
                        ++i;
                    }
                }
            }
        };

        const GCodeConfig& config = print.config();
        check(_(L("Machine start G-code")), config.machine_start_gcode.value);
        if (ret.size() < MAX_TAGS_COUNT) check(_(L("Machine end G-code")), config.machine_end_gcode.value);
        if (ret.size() < MAX_TAGS_COUNT) check(_(L("Before layer change G-code")), config.before_layer_change_gcode.value);
        if (ret.size() < MAX_TAGS_COUNT) check(_(L("Layer change G-code")), config.layer_change_gcode.value);
        if (ret.size() < MAX_TAGS_COUNT) check(_(L("Change filament G-code")), config.change_filament_gcode.value);
        //BBS
        //if (ret.size() < MAX_TAGS_COUNT) check(_(L("Printing by object G-code")), config.printing_by_object_gcode.value);
        //if (ret.size() < MAX_TAGS_COUNT) check(_(L("Color Change G-code")), config.color_change_gcode.value);
        if (ret.size() < MAX_TAGS_COUNT) check(_(L("Pause G-code")), config.machine_pause_gcode.value);
        if (ret.size() < MAX_TAGS_COUNT) check(_(L("Template Custom G-code")), config.template_custom_gcode.value);
        if (ret.size() < MAX_TAGS_COUNT) {
            for (const std::string& value : config.filament_start_gcode.values) {
                check(_(L("Filament start G-code")), value);
                if (ret.size() == MAX_TAGS_COUNT)
                    break;
            }
        }
        if (ret.size() < MAX_TAGS_COUNT) {
            for (const std::string& value : config.filament_end_gcode.values) {
                check(_(L("Filament end G-code")), value);
                if (ret.size() == MAX_TAGS_COUNT)
                    break;
            }
        }
        //BBS: no custom_gcode_per_print_z, don't need to check
        //if (ret.size() < MAX_TAGS_COUNT) {
        //    const CustomGCode::Info& custom_gcode_per_print_z = print.model().custom_gcode_per_print_z;
        //    for (const auto& gcode : custom_gcode_per_print_z.gcodes) {
        //        check(_(L("Custom G-code")), gcode.extra);
        //        if (ret.size() == MAX_TAGS_COUNT)
        //            break;
        //    }
        //}

        return ret;
    }
} // namespace DoExport

void GCode::do_export(Print* print, const char* path, GCodeProcessorResult* result, ThumbnailsGeneratorCallback thumbnail_cb)
{
    PROFILE_CLEAR();

    // BBS
    m_curr_print = print;

    GCodeWriter::full_gcode_comment = print->config().gcode_comments;
    CNumericLocalesSetter locales_setter;

    // Does the file exist? If so, we hope that it is still valid.
    if (print->is_step_done(psGCodeExport) && boost::filesystem::exists(boost::filesystem::path(path)))
        return;

    BOOST_LOG_TRIVIAL(info) << boost::format("Will export G-code to %1% soon")%path;
    GCodeProcessor::s_IsBBLPrinter = print->is_BBL_printer();
    print->set_started(psGCodeExport);


    // check if any custom gcode contains keywords used by the gcode processor to
    // produce time estimation and gcode toolpaths
    std::vector<std::pair<std::string, std::string>> validation_res = DoExport::validate_custom_gcode(*print);
    if (!validation_res.empty()) {
        std::string reports;
        for (const auto& [source, keyword] : validation_res) {
            reports += source + ": \"" + keyword + "\"\n";
        }
        //print->active_step_add_warning(PrintStateBase::WarningLevel::NON_CRITICAL,
        //    _(L("In the custom G-code were found reserved keywords:")) + "\n" +
        //    reports +
        //    _(L("This may cause problems in g-code visualization and printing time estimation.")));
        std::string temp = "Dangerous keywords in custom Gcode: " + reports + "\nThis may cause problems in g-code visualization and printing time estimation.";
        BOOST_LOG_TRIVIAL(warning) << temp;
    }

    BOOST_LOG_TRIVIAL(info) << "Exporting G-code..." << log_memory_info();

    // Remove the old g-code if it exists.
    boost::nowide::remove(path);

    fs::path file_path(path);
    fs::path folder = file_path.parent_path();
    if (!fs::exists(folder)) {
        fs::create_directory(folder);
        BOOST_LOG_TRIVIAL(error) << "[WARNING]: the parent path " + folder.string() +" is not there, create it!" << std::endl;
    }

    std::string path_tmp(path);
    path_tmp += ".tmp";

    m_processor.initialize(path_tmp);
    GCodeOutputStream file(boost::nowide::fopen(path_tmp.c_str(), "wb"), m_processor);
    if (! file.is_open()) {
        BOOST_LOG_TRIVIAL(error) << std::string("G-code export to ") + path + " failed.\nCannot open the file for writing.\n" << std::endl;
        if (!fs::exists(folder)) {
            //fs::create_directory(folder);
            BOOST_LOG_TRIVIAL(error) << "the parent path " + folder.string() +" is not there!!!" << std::endl;
        }
        throw Slic3r::RuntimeError(std::string("G-code export to ") + path + " failed.\nCannot open the file for writing.\n");
    }

    try {
        m_placeholder_parser_failed_templates.clear();
        this->_do_export(*print, file, thumbnail_cb);
        file.flush();
        if (file.is_error()) {
            file.close();
            boost::nowide::remove(path_tmp.c_str());
            throw Slic3r::RuntimeError(std::string("G-code export to ") + path + " failed\nIs the disk full?\n");
        }
    } catch (std::exception & /* ex */) {
        // Rethrow on any exception. std::runtime_exception and CanceledException are expected to be thrown.
        // Close and remove the file.
        file.close();
        boost::nowide::remove(path_tmp.c_str());
        throw;
    }
    file.close();

    check_placeholder_parser_failed();

    BOOST_LOG_TRIVIAL(debug) << "Start processing gcode, " << log_memory_info();
    // Post-process the G-code to update time stamps.
    m_processor.finalize(true);
//    DoExport::update_print_estimated_times_stats(m_processor, print->m_print_statistics);
    DoExport::update_print_estimated_stats(m_processor, m_writer.extruders(), print->m_print_statistics);
    if (result != nullptr) {
        *result = std::move(m_processor.extract_result());
        // set the filename to the correct value
        result->filename = path;
    }

    //BBS: add some log for error output
    BOOST_LOG_TRIVIAL(debug) << boost::format("Finished processing gcode to %1% ") % path_tmp;

    std::error_code ret = rename_file(path_tmp, path);
    if (ret) {
        throw Slic3r::RuntimeError(
            std::string("Failed to rename the output G-code file from ") + path_tmp + " to " + path + '\n' + "error code " + ret.message() + '\n' +
            "Is " + path_tmp + " locked?" + '\n');
    }
    else {
        BOOST_LOG_TRIVIAL(info) << boost::format("rename_file from %1% to %2% successfully")% path_tmp % path;
    }

    BOOST_LOG_TRIVIAL(info) << "Exporting G-code finished" << log_memory_info();
    print->set_done(psGCodeExport);

    // Write the profiler measurements to file
    PROFILE_UPDATE();
    PROFILE_OUTPUT(debug_out_path("gcode-export-profile.txt").c_str());
}

// free functions called by GCode::_do_export()
namespace DoExport {
    static void init_gcode_processor(const PrintConfig& config, GCodeProcessor& processor, bool& silent_time_estimator_enabled)
    {
        silent_time_estimator_enabled = (config.gcode_flavor == gcfMarlinLegacy || config.gcode_flavor == gcfMarlinFirmware)
                                        && config.silent_mode;
        processor.reset();
        processor.apply_config(config);
        processor.enable_stealth_time_estimator(silent_time_estimator_enabled);
    }

#if 0
	static double autospeed_volumetric_limit(const Print &print)
	{
	    // get the minimum cross-section used in the print
	    std::vector<double> mm3_per_mm;
	    for (auto object : print.objects()) {
	        for (size_t region_id = 0; region_id < object->num_printing_regions(); ++ region_id) {
	            const PrintRegion &region = object->printing_region(region_id);
	            for (auto layer : object->layers()) {
	                const LayerRegion* layerm = layer->regions()[region_id];
	                if (region.config().get_abs_value("inner_wall_speed") == 0 ||
                        // BBS: remove small small_perimeter_speed config, and will absolutely
                        // remove related code if no other issue in the coming release.
	                    //region.config().get_abs_value("small_perimeter_speed") == 0 ||
	                    region.config().outer_wall_speed.value == 0 ||
	                    region.config().get_abs_value("bridge_speed") == 0)
	                    mm3_per_mm.push_back(layerm->perimeters.min_mm3_per_mm());
	                if (region.config().get_abs_value("sparse_infill_speed") == 0 ||
	                    region.config().get_abs_value("internal_solid_infill_speed") == 0 ||
	                    region.config().get_abs_value("top_surface_speed") == 0 ||
                        region.config().get_abs_value("bridge_speed") == 0)
                    {
                        // Minimal volumetric flow should not be calculated over ironing extrusions.
                        // Use following lambda instead of the built-it method.
                        auto min_mm3_per_mm_no_ironing = [](const ExtrusionEntityCollection& eec) -> double {
                            double min = std::numeric_limits<double>::max();
                            for (const ExtrusionEntity* ee : eec.entities)
                                if (ee->role() != erIroning)
                                    min = std::min(min, ee->min_mm3_per_mm());
                            return min;
                        };

                        mm3_per_mm.push_back(min_mm3_per_mm_no_ironing(layerm->fills));
                    }
	            }
	        }
	        if (object->config().get_abs_value("support_speed") == 0 ||
	            object->config().get_abs_value("support_interface_speed") == 0)
	            for (auto layer : object->support_layers())
	                mm3_per_mm.push_back(layer->support_fills.min_mm3_per_mm());
	    }
	    // filter out 0-width segments
	    mm3_per_mm.erase(std::remove_if(mm3_per_mm.begin(), mm3_per_mm.end(), [](double v) { return v < 0.000001; }), mm3_per_mm.end());
	    double volumetric_speed = 0.;
	    if (! mm3_per_mm.empty()) {
	        // In order to honor max_print_speed we need to find a target volumetric
	        // speed that we can use throughout the print. So we define this target
	        // volumetric speed as the volumetric speed produced by printing the
	        // smallest cross-section at the maximum speed: any larger cross-section
	        // will need slower feedrates.
	        volumetric_speed = *std::min_element(mm3_per_mm.begin(), mm3_per_mm.end()) * print.config().max_print_speed.value;
	        // limit such volumetric speed with max_volumetric_speed if set
            //BBS
	        //if (print.config().max_volumetric_speed.value > 0)
	        //    volumetric_speed = std::min(volumetric_speed, print.config().max_volumetric_speed.value);
	    }
	    return volumetric_speed;
	}
#endif

    static void init_ooze_prevention(const Print &print, OozePrevention &ooze_prevention)
	{
	    // Calculate wiping points if needed
	    if (print.config().ooze_prevention.value && ! print.config().single_extruder_multi_material) {
	        Points skirt_points;
	        for (const ExtrusionEntity *ee : print.skirt().entities)
	            for (const ExtrusionPath &path : dynamic_cast<const ExtrusionLoop*>(ee)->paths)
	                append(skirt_points, path.polyline.points);
	        if (! skirt_points.empty()) {
	            Polygon outer_skirt = Slic3r::Geometry::convex_hull(skirt_points);
	            Polygons skirts;
	            for (unsigned int extruder_id : print.extruders()) {
	                const Vec2d &extruder_offset = print.config().extruder_offset.get_at(extruder_id);
	                Polygon s(outer_skirt);
	                s.translate(Point::new_scale(-extruder_offset(0), -extruder_offset(1)));
	                skirts.emplace_back(std::move(s));
	            }
	            ooze_prevention.enable = true;
	            ooze_prevention.standby_points = offset(Slic3r::Geometry::convex_hull(skirts), float(scale_(3.))).front().equally_spaced_points(float(scale_(10.)));
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
	}

    //BBS: add plate id for thumbnail generate param
	template<typename WriteToOutput, typename ThrowIfCanceledCallback>
	static void export_thumbnails_to_file(ThumbnailsGeneratorCallback &thumbnail_cb, int plate_id, const std::vector<Vec2d> &sizes, WriteToOutput output, ThrowIfCanceledCallback throw_if_canceled)
	{
	    // Write thumbnails using base64 encoding
	    if (thumbnail_cb != nullptr)
	    {
	        const size_t max_row_length = 78;
            //BBS: add plate id for thumbnail generate param
            ThumbnailsList thumbnails = thumbnail_cb(ThumbnailsParams{ sizes, true, true, true, true, plate_id });
	        for (const ThumbnailData& data : thumbnails)
	        {
	            if (data.is_valid())
	            {
	                size_t png_size = 0;
	                void* png_data = tdefl_write_image_to_png_file_in_memory_ex((const void*)data.pixels.data(), data.width, data.height, 4, &png_size, MZ_DEFAULT_LEVEL, 1);
	                if (png_data != nullptr)
	                {
	                    std::string encoded;
	                    encoded.resize(boost::beast::detail::base64::encoded_size(png_size));
	                    encoded.resize(boost::beast::detail::base64::encode((void*)&encoded[0], (const void*)png_data, png_size));

                        output("; THUMBNAIL_BLOCK_START\n");
	                    output((boost::format("; thumbnail begin %dx%d %d\n") % data.width % data.height % encoded.size()).str().c_str());

	                    unsigned int row_count = 0;
	                    while (encoded.size() > max_row_length)
	                    {
	                        output((boost::format("; %s\n") % encoded.substr(0, max_row_length)).str().c_str());
	                        encoded = encoded.substr(max_row_length);
	                        ++row_count;
	                    }

	                    if (encoded.size() > 0)
	                    	output((boost::format("; %s\n") % encoded).str().c_str());

	                    output("; thumbnail end\n");
                        output("; THUMBNAIL_BLOCK_END\n\n");

	                    mz_free(png_data);
	                }
	            }
	            throw_if_canceled();
	        }
	    }
	}

	// Fill in print_statistics and return formatted string containing filament statistics to be inserted into G-code comment section.
    static std::string update_print_stats_and_format_filament_stats(
        const bool                   has_wipe_tower,
	    const WipeTowerData         &wipe_tower_data,
	    const std::vector<Extruder> &extruders,
		PrintStatistics 		    &print_statistics)
    {
		std::string filament_stats_string_out;

	    print_statistics.clear();
        print_statistics.total_toolchanges = std::max(0, wipe_tower_data.number_of_toolchanges);
	    if (! extruders.empty()) {
	        std::pair<std::string, unsigned int> out_filament_used_mm ("; filament used [mm] = ", 0);
	        std::pair<std::string, unsigned int> out_filament_used_cm3("; filament used [cm3] = ", 0);
	        std::pair<std::string, unsigned int> out_filament_used_g  ("; filament used [g] = ", 0);
	        std::pair<std::string, unsigned int> out_filament_cost    ("; filament cost = ", 0);
	        for (const Extruder &extruder : extruders) {
	            double used_filament   = extruder.used_filament() + (has_wipe_tower ? wipe_tower_data.used_filament[extruder.id()] : 0.f);
	            double extruded_volume = extruder.extruded_volume() + (has_wipe_tower ? wipe_tower_data.used_filament[extruder.id()] * 2.4052f : 0.f); // assumes 1.75mm filament diameter
	            double filament_weight = extruded_volume * extruder.filament_density() * 0.001;
	            double filament_cost   = filament_weight * extruder.filament_cost()    * 0.001;
                auto append = [&extruder](std::pair<std::string, unsigned int> &dst, const char *tmpl, double value) {
                    assert(is_decimal_separator_point());
	                while (dst.second < extruder.id()) {
	                    // Fill in the non-printing extruders with zeros.
	                    dst.first += (dst.second > 0) ? ", 0" : "0";
	                    ++ dst.second;
	                }
	                if (dst.second > 0)
	                    dst.first += ", ";
	                char buf[64];
					sprintf(buf, tmpl, value);
	                dst.first += buf;
	                ++ dst.second;
	            };
	            append(out_filament_used_mm,  "%.2lf", used_filament);
	            append(out_filament_used_cm3, "%.2lf", extruded_volume * 0.001);
	            if (filament_weight > 0.) {
	                print_statistics.total_weight = print_statistics.total_weight + filament_weight;
	                append(out_filament_used_g, "%.2lf", filament_weight);
	                if (filament_cost > 0.) {
	                    print_statistics.total_cost = print_statistics.total_cost + filament_cost;
	                    append(out_filament_cost, "%.2lf", filament_cost);
	                }
	            }
	            print_statistics.total_used_filament += used_filament;
	            print_statistics.total_extruded_volume += extruded_volume;
	            print_statistics.total_wipe_tower_filament += has_wipe_tower ? used_filament - extruder.used_filament() : 0.;
	            print_statistics.total_wipe_tower_cost += has_wipe_tower ? (extruded_volume - extruder.extruded_volume())* extruder.filament_density() * 0.001 * extruder.filament_cost() * 0.001 : 0.;
	        }
	        filament_stats_string_out += out_filament_used_mm.first;
            filament_stats_string_out += "\n" + out_filament_used_cm3.first;
            if (out_filament_used_g.second)
                filament_stats_string_out += "\n" + out_filament_used_g.first;
            if (out_filament_cost.second)
               filament_stats_string_out += "\n" + out_filament_cost.first;
        }
        return filament_stats_string_out;
    }
}

#if 0
// Sort the PrintObjects by their increasing Z, likely useful for avoiding colisions on Deltas during sequential prints.
static inline std::vector<const PrintInstance*> sort_object_instances_by_max_z(const Print &print)
{
    std::vector<const PrintObject*> objects(print.objects().begin(), print.objects().end());
    std::sort(objects.begin(), objects.end(), [](const PrintObject *po1, const PrintObject *po2) { return po1->height() < po2->height(); });
    std::vector<const PrintInstance*> instances;
    instances.reserve(objects.size());
    for (const PrintObject *object : objects)
        for (size_t i = 0; i < object->instances().size(); ++ i)
            instances.emplace_back(&object->instances()[i]);
    return instances;
}
#endif

// Produce a vector of PrintObjects in the order of their respective ModelObjects in print.model().
//BBS: add sort logic for seq-print
std::vector<const PrintInstance*> sort_object_instances_by_model_order(const Print& print, bool init_order)
{
    // Build up map from ModelInstance* to PrintInstance*
    std::vector<std::pair<const ModelInstance*, const PrintInstance*>> model_instance_to_print_instance;
    model_instance_to_print_instance.reserve(print.num_object_instances());
    for (const PrintObject *print_object : print.objects())
        for (const PrintInstance &print_instance : print_object->instances())
        {
            if (init_order)
                const_cast<ModelInstance*>(print_instance.model_instance)->arrange_order = print_instance.model_instance->id().id;
            model_instance_to_print_instance.emplace_back(print_instance.model_instance, &print_instance);
        }
    std::sort(model_instance_to_print_instance.begin(), model_instance_to_print_instance.end(), [](auto &l, auto &r) { return l.first->arrange_order < r.first->arrange_order; });

    std::vector<const PrintInstance*> instances;
    instances.reserve(model_instance_to_print_instance.size());
    for (const ModelObject *model_object : print.model().objects)
        for (const ModelInstance *model_instance : model_object->instances) {
            auto it = std::lower_bound(model_instance_to_print_instance.begin(), model_instance_to_print_instance.end(), std::make_pair(model_instance, nullptr), [](auto &l, auto &r) { return l.first->arrange_order < r.first->arrange_order; });
            if (it != model_instance_to_print_instance.end() && it->first == model_instance)
                instances.emplace_back(it->second);
        }
    std::sort(instances.begin(), instances.end(), [](auto& l, auto& r) { return l->model_instance->arrange_order < r->model_instance->arrange_order; });
    return instances;
}

enum BambuBedType {
    bbtUnknown = 0,
    bbtCoolPlate = 1,
    bbtEngineeringPlate = 2,
    bbtHighTemperaturePlate = 3,
    bbtTexturedPEIPlate         = 4,
};

static BambuBedType to_bambu_bed_type(BedType type)
{
    BambuBedType bambu_bed_type = bbtUnknown;
    if (type == btPC)
        bambu_bed_type = bbtCoolPlate;
    else if (type == btEP)
        bambu_bed_type = bbtEngineeringPlate;
    else if (type == btPEI)
        bambu_bed_type = bbtHighTemperaturePlate;
    else if (type == btPTE)
        bambu_bed_type = bbtTexturedPEIPlate;

    return bambu_bed_type;
}

void GCode::_do_export(Print& print, GCodeOutputStream &file, ThumbnailsGeneratorCallback thumbnail_cb)
{
    PROFILE_FUNC();

    // modifies m_silent_time_estimator_enabled
    DoExport::init_gcode_processor(print.config(), m_processor, m_silent_time_estimator_enabled);
    const bool is_bbl_printers = print.is_BBL_printer();
    // resets analyzer's tracking data
    m_last_height  = 0.f;
    m_last_layer_z = 0.f;
    m_max_layer_z  = 0.f;
    m_last_width = 0.f;
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    m_last_mm3_per_mm = 0.;
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
    
    m_writer.set_is_bbl_machine(is_bbl_printers);

    // How many times will be change_layer() called?
    // change_layer() in turn increments the progress bar status.
    m_layer_count = 0;
    if (print.config().print_sequence == PrintSequence::ByObject) {
        // Add each of the object's layers separately.
        for (auto object : print.objects()) {
            //BBS: fix the issue that total layer is not right
            std::vector<coord_t> zs;
            zs.reserve(object->layers().size() + object->support_layers().size());
            for (auto layer : object->layers())
                zs.push_back((coord_t)(layer->print_z / EPSILON));
            for (auto layer : object->support_layers())
                zs.push_back((coord_t)(layer->print_z / EPSILON));
            std::sort(zs.begin(), zs.end());
            //BBS: merge numerically very close Z values.
            auto end_it = std::unique(zs.begin(), zs.end());
            unsigned int temp_layer_count = (unsigned int)(end_it - zs.begin());
            for (auto it = zs.begin(); it != end_it - 1; it++) {
                if (abs(*it - *(it + 1)) < EPSILON)
                    temp_layer_count--;
            }
            m_layer_count += (unsigned int)(object->instances().size() * temp_layer_count);
        }
    } else {
        // Print all objects with the same print_z together.
        //BBS: fix the issue that total layer is not right
        std::vector<coord_t> zs;
        for (auto object : print.objects()) {
            zs.reserve(zs.size() + object->layers().size() + object->support_layers().size());
            for (auto layer : object->layers())
                zs.push_back((coord_t)(layer->print_z / EPSILON));
            for (auto layer : object->support_layers())
                zs.push_back((coord_t)(layer->print_z / EPSILON));
        }
        std::sort(zs.begin(), zs.end());
        //BBS: merge numerically very close Z values.
        auto end_it = std::unique(zs.begin(), zs.end());
        m_layer_count = (unsigned int)(end_it - zs.begin());
        for (auto it = zs.begin(); it != end_it - 1; it++) {
            if (abs(*it - *(it + 1)) < EPSILON)
                m_layer_count--;
        }
    }
    print.throw_if_canceled();

    m_enable_cooling_markers = true;
    this->apply_print_config(print.config());

    //m_volumetric_speed = DoExport::autospeed_volumetric_limit(print);
    print.throw_if_canceled();

    if (print.config().spiral_mode.value)
        m_spiral_vase = make_unique<SpiralVase>(print.config());
#ifdef HAS_PRESSURE_EQUALIZER
    if (print.config().max_volumetric_extrusion_rate_slope_positive.value > 0 ||
        print.config().max_volumetric_extrusion_rate_slope_negative.value > 0)
        m_pressure_equalizer = make_unique<PressureEqualizer>(&print.config());
    m_enable_extrusion_role_markers = (bool)m_pressure_equalizer;
#else /* HAS_PRESSURE_EQUALIZER */
    m_enable_extrusion_role_markers = false;
#endif /* HAS_PRESSURE_EQUALIZER */

    file.write_format("; HEADER_BLOCK_START\n");
    // Write information on the generator.
    file.write_format("; generated by %s on %s\n", Slic3r::header_slic3r_generated().c_str(), Slic3r::Utils::local_timestamp().c_str());
    //BBS: total estimated printing time
    file.write_format(";%s\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Estimated_Printing_Time_Placeholder).c_str());
    //BBS: total layer number
    file.write_format(";%s\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Total_Layer_Number_Placeholder).c_str());
    file.write_format("; HEADER_BLOCK_END\n\n");

    
      // BBS: write global config at the beginning of gcode file because printer
      // need these config information
      // Append full config, delimited by two 'phony' configuration keys
      // CONFIG_BLOCK_START and CONFIG_BLOCK_END. The delimiters are structured
      // as configuration key / value pairs to be parsable by older versions of
      // PrusaSlicer G-code viewer.
    {
      if (is_bbl_printers) {
        file.write("; CONFIG_BLOCK_START\n");
        std::string full_config;
        append_full_config(print, full_config);
        if (!full_config.empty())
          file.write(full_config);

        // SoftFever: write compatiple image
        int first_layer_bed_temperature = get_bed_temperature(0, true, print.config().curr_bed_type);
        file.write_format("; first_layer_bed_temperature = %d\n",
                          first_layer_bed_temperature);
        file.write_format(
            "; first_layer_temperature = %d\n",
            print.config().nozzle_temperature_initial_layer.get_at(0));
        file.write("; CONFIG_BLOCK_END\n\n");
      } else {
        file.write_format("; hack-fix: write fake slicer info here so that "
                          "preprocess_cancellation can process.\n");
        file.write_format("; %s\n\n", std::string(std::string("generated by SuperSlicer " SoftFever_VERSION " on ") +
                                                  Slic3r::Utils::local_timestamp())
                                          .c_str());

        DoExport::export_thumbnails_to_file(
            thumbnail_cb, print.get_plate_index(), print.full_print_config().option<ConfigOptionPoints>("thumbnails")->values,
            [&file](const char *sz) { file.write(sz); },
            [&print]() { print.throw_if_canceled(); });
      }
    }
    

    // Write some terse information on the slicing parameters.
    const PrintObject *first_object         = print.objects().front();
    const double       layer_height         = first_object->config().layer_height.value;
    const double       initial_layer_print_height   = print.config().initial_layer_print_height.value;
    for (size_t region_id = 0; region_id < print.num_print_regions(); ++ region_id) {
        const PrintRegion &region = print.get_print_region(region_id);
        file.write_format("; external perimeters extrusion width = %.2fmm\n", region.flow(*first_object, frExternalPerimeter, layer_height).width());
        file.write_format("; perimeters extrusion width = %.2fmm\n",          region.flow(*first_object, frPerimeter,         layer_height).width());
        file.write_format("; infill extrusion width = %.2fmm\n",              region.flow(*first_object, frInfill,            layer_height).width());
        file.write_format("; solid infill extrusion width = %.2fmm\n",        region.flow(*first_object, frSolidInfill,       layer_height).width());
        file.write_format("; top infill extrusion width = %.2fmm\n",          region.flow(*first_object, frTopSolidInfill,    layer_height).width());
        if (print.has_support_material())
            file.write_format("; support material extrusion width = %.2fmm\n", support_material_flow(first_object).width());
        if (print.config().initial_layer_line_width.value > 0)
            file.write_format("; first layer extrusion width = %.2fmm\n",   region.flow(*first_object, frPerimeter, initial_layer_print_height, true).width());
        file.write_format("\n");
    }

    file.write_format("; EXECUTABLE_BLOCK_START\n");
    // adds tags for time estimators
    file.write_format(";%s\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::First_Line_M73_Placeholder).c_str());

    // Prepare the helper object for replacing placeholders in custom G-code and output filename.
    m_placeholder_parser = print.placeholder_parser();
    m_placeholder_parser.update_timestamp();
    m_placeholder_parser_context.rng = std::mt19937(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    print.update_object_placeholders(m_placeholder_parser.config_writable(), ".gcode");

    // Get optimal tool ordering to minimize tool switches of a multi-exruder print.
    // For a print by objects, find the 1st printing object.
    ToolOrdering tool_ordering;
    unsigned int initial_extruder_id = (unsigned int)-1;
    //BBS: first non-support filament extruder
    unsigned int initial_non_support_extruder_id;
    unsigned int final_extruder_id   = (unsigned int)-1;
    bool         has_wipe_tower      = false;
    std::vector<const PrintInstance*> 					print_object_instances_ordering;
    std::vector<const PrintInstance*>::const_iterator 	print_object_instance_sequential_active;
    if (print.config().print_sequence == PrintSequence::ByObject) {
        // Order object instances for sequential print.
        print_object_instances_ordering = sort_object_instances_by_model_order(print);
//        print_object_instances_ordering = sort_object_instances_by_max_z(print);
        // Find the 1st printing object, find its tool ordering and the initial extruder ID.
        print_object_instance_sequential_active = print_object_instances_ordering.begin();
        for (; print_object_instance_sequential_active != print_object_instances_ordering.end(); ++ print_object_instance_sequential_active) {
            tool_ordering = ToolOrdering(*(*print_object_instance_sequential_active)->print_object, initial_extruder_id);
            if ((initial_extruder_id = tool_ordering.first_extruder()) != static_cast<unsigned int>(-1)) {
                //BBS: try to find the non-support filament extruder if is multi color and initial_extruder is support filament
                initial_non_support_extruder_id = initial_extruder_id;
                if (tool_ordering.all_extruders().size() > 1 && print.config().filament_is_support.get_at(initial_extruder_id)) {
                    bool has_non_support_filament = false;
                    for (unsigned int extruder : tool_ordering.all_extruders()) {
                        if (!print.config().filament_is_support.get_at(extruder)) {
                            has_non_support_filament = true;
                            break;
                        }
                    }
                    //BBS: find the non-support filament extruder of object
                    if (has_non_support_filament)
                        for (LayerTools layer_tools : tool_ordering.layer_tools()) {
                            if (!layer_tools.has_object)
                                continue;
                            for (unsigned int extruder : layer_tools.extruders) {
                                if (print.config().filament_is_support.get_at(extruder))
                                    continue;
                                initial_non_support_extruder_id = extruder;
                                break;
                            }
                        }
                }

                break;
            }
        }
        if (initial_extruder_id == static_cast<unsigned int>(-1))
            // No object to print was found, cancel the G-code export.
            throw Slic3r::SlicingError(_(L("No object can be printed. Maybe too small")));
        // We don't allow switching of extruders per layer by Model::custom_gcode_per_print_z in sequential mode.
        // Use the extruder IDs collected from Regions.
        this->set_extruders(print.extruders());

        has_wipe_tower = print.has_wipe_tower() && tool_ordering.has_wipe_tower();
    } else {
        // Find tool ordering for all the objects at once, and the initial extruder ID.
        // If the tool ordering has been pre-calculated by Print class for wipe tower already, reuse it.
        tool_ordering = print.tool_ordering();
        tool_ordering.assign_custom_gcodes(print);
        if (tool_ordering.all_extruders().empty())
            // No object to print was found, cancel the G-code export.
            throw Slic3r::SlicingError(_(L("No object can be printed. Maybe too small")));
        has_wipe_tower = print.has_wipe_tower() && tool_ordering.has_wipe_tower();
        // BBS: priming logic is removed, so 1st layer tool_ordering also respect the object tool sequence
#if 0
        initial_extruder_id = (has_wipe_tower && !print.config().single_extruder_multi_material_priming) ?
            // The priming towers will be skipped.
            tool_ordering.all_extruders().back() :
            // Don't skip the priming towers.
            tool_ordering.first_extruder();
#else
        initial_extruder_id = tool_ordering.first_extruder();
#endif
        //BBS: try to find the non-support filament extruder if is multi color and initial_extruder is support filament
        if (initial_extruder_id != static_cast<unsigned int>(-1)) {
            initial_non_support_extruder_id = initial_extruder_id;
            if (tool_ordering.all_extruders().size() > 1 && print.config().filament_is_support.get_at(initial_extruder_id)) {
                bool has_non_support_filament = false;
                for (unsigned int extruder : tool_ordering.all_extruders()) {
                    if (!print.config().filament_is_support.get_at(extruder)) {
                        has_non_support_filament = true;
                        break;
                    }
                }
                //BBS: find the non-support filament extruder of object
                if (has_non_support_filament)
                    for (LayerTools layer_tools : tool_ordering.layer_tools()) {
                        if (!layer_tools.has_object)
                            continue;
                        for (unsigned int extruder : layer_tools.extruders) {
                            if (print.config().filament_is_support.get_at(extruder))
                                continue;
                            initial_non_support_extruder_id = extruder;
                            break;
                        }
                    }
            }
        }

        // In non-sequential print, the printing extruders may have been modified by the extruder switches stored in Model::custom_gcode_per_print_z.
        // Therefore initialize the printing extruders from there.
        this->set_extruders(tool_ordering.all_extruders());
        // Order object instances using a nearest neighbor search.
        print_object_instances_ordering = chain_print_object_instances(print);
    }
    if (initial_extruder_id == (unsigned int)-1) {
        // Nothing to print!
        initial_extruder_id = 0;
        initial_non_support_extruder_id = 0;
        final_extruder_id   = 0;
    } else {
        final_extruder_id = tool_ordering.last_extruder();
        assert(final_extruder_id != (unsigned int)-1);
    }
    print.throw_if_canceled();

    m_cooling_buffer = make_unique<CoolingBuffer>(*this);
    m_cooling_buffer->set_current_extruder(initial_extruder_id);

    // Emit machine envelope limits for the Marlin firmware.
    this->print_machine_envelope(file, print);

    // Disable fan.
    if (m_config.auxiliary_fan.value && print.config().close_fan_the_first_x_layers.get_at(initial_extruder_id)) {
        file.write(m_writer.set_fan(0));
        //BBS: disable additional fan
        file.write(m_writer.set_additional_fan(0));
    }

    // Let the start-up script prime the 1st printing tool.
    m_placeholder_parser.set("initial_tool", initial_extruder_id);
    m_placeholder_parser.set("initial_extruder", initial_extruder_id);
    //BBS
    m_placeholder_parser.set("initial_no_support_tool", initial_non_support_extruder_id);
    m_placeholder_parser.set("initial_no_support_extruder", initial_non_support_extruder_id);
    m_placeholder_parser.set("current_extruder", initial_extruder_id);
    //Set variable for total layer count so it can be used in custom gcode.
    m_placeholder_parser.set("total_layer_count", m_layer_count);
    // Useful for sequential prints.
    m_placeholder_parser.set("current_object_idx", 0);
    // For the start / end G-code to do the priming and final filament pull in case there is no wipe tower provided.
    m_placeholder_parser.set("has_wipe_tower", has_wipe_tower);
    //m_placeholder_parser.set("has_single_extruder_multi_material_priming", has_wipe_tower && print.config().single_extruder_multi_material_priming);
    m_placeholder_parser.set("total_toolchanges", std::max(0, print.wipe_tower_data().number_of_toolchanges)); // Check for negative toolchanges (single extruder mode) and set to 0 (no tool change).
    Vec2f plate_offset = m_writer.get_xy_offset();
    {
        BoundingBoxf bbox(print.config().printable_area.values);
        m_placeholder_parser.set("print_bed_min", new ConfigOptionFloats({ bbox.min.x() - plate_offset.x(), bbox.min.y() - plate_offset.y() }));
        m_placeholder_parser.set("print_bed_max", new ConfigOptionFloats({ bbox.max.x() - plate_offset.x(), bbox.max.y() - plate_offset.y() }));
        m_placeholder_parser.set("print_bed_size", new ConfigOptionFloats({ bbox.size().x(), bbox.size().y() }));
    }
    {
        // Convex hull of the 1st layer extrusions, for bed leveling and placing the initial purge line.
        // It encompasses the object extrusions, support extrusions, skirt, brim, wipe tower.
        // It does NOT encompass user extrusions generated by custom G-code,
        // therefore it does NOT encompass the initial purge line.
        // It does NOT encompass MMU/MMU2 starting (wipe) areas.
        auto pts = std::make_unique<ConfigOptionPoints>();
        pts->values.reserve(print.first_layer_convex_hull().size());
        for (const Point& pt : print.first_layer_convex_hull().points)
            pts->values.emplace_back(unscale(pt) - Vec2d(print.get_plate_origin().x(), print.get_plate_origin().y()));
        BoundingBoxf bbox(pts->values);
        m_placeholder_parser.set("first_layer_print_convex_hull", pts.release());
        m_placeholder_parser.set("first_layer_print_min", new ConfigOptionFloats({bbox.min.x(), bbox.min.y()}));
        m_placeholder_parser.set("first_layer_print_max", new ConfigOptionFloats({bbox.max.x(), bbox.max.y()}));
        m_placeholder_parser.set("first_layer_print_size", new ConfigOptionFloats({ bbox.size().x(), bbox.size().y() }));
    }
    float outer_wall_volumetric_speed = 0.0f;
    {
        int curr_bed_type = m_config.curr_bed_type.getInt();

        std::string first_layer_bed_temp_str;
        const ConfigOptionInts* first_bed_temp_opt = m_config.option<ConfigOptionInts>(get_bed_temp_1st_layer_key((BedType)curr_bed_type));
        const ConfigOptionInts* bed_temp_opt = m_config.option<ConfigOptionInts>(get_bed_temp_key((BedType)curr_bed_type));
        m_placeholder_parser.set("bbl_bed_temperature_gcode", new ConfigOptionBool(false));
        m_placeholder_parser.set("bed_temperature_initial_layer", new ConfigOptionInts(*first_bed_temp_opt));
        m_placeholder_parser.set("bed_temperature", new ConfigOptionInts(*bed_temp_opt));
        m_placeholder_parser.set("bed_temperature_initial_layer_single", new ConfigOptionInt(first_bed_temp_opt->get_at(initial_extruder_id)));
        m_placeholder_parser.set("bed_temperature_initial_layer_vector", new ConfigOptionString(""));
        m_placeholder_parser.set("chamber_temperature",new ConfigOptionInt(m_config.chamber_temperature));

        // SoftFever: support variables `first_layer_temperature` and `first_layer_bed_temperature`
        m_placeholder_parser.set("first_layer_bed_temperature", new ConfigOptionInts(*first_bed_temp_opt));
        m_placeholder_parser.set("first_layer_temperature", new ConfigOptionInts(m_config.nozzle_temperature_initial_layer));
        m_placeholder_parser.set("max_print_height",new ConfigOptionInt(m_config.printable_height));
        m_placeholder_parser.set("z_offset", new ConfigOptionFloat(0.0f));
        m_placeholder_parser.set("plate_name", new ConfigOptionString(print.get_plate_name()));

        //BBS: calculate the volumetric speed of outer wall. Ignore pre-object setting and multi-filament, and just use the default setting
        {
            float filament_max_volumetric_speed = m_config.option<ConfigOptionFloats>("filament_max_volumetric_speed")->get_at(initial_non_support_extruder_id);
            float outer_wall_line_width = print.default_region_config().outer_wall_line_width.value;
            if (outer_wall_line_width == 0.0) {
                float default_line_width = print.default_object_config().line_width.value;
                outer_wall_line_width = default_line_width == 0.0 ? m_config.nozzle_diameter.get_at(initial_non_support_extruder_id) : default_line_width;
            }
            Flow outer_wall_flow = Flow(outer_wall_line_width, m_config.layer_height, m_config.nozzle_diameter.get_at(initial_non_support_extruder_id));
            float outer_wall_speed = print.default_region_config().outer_wall_speed.value;
            outer_wall_volumetric_speed = outer_wall_speed * outer_wall_flow.mm3_per_mm();
            if (outer_wall_volumetric_speed > filament_max_volumetric_speed)
                outer_wall_volumetric_speed = filament_max_volumetric_speed;
            m_placeholder_parser.set("outer_wall_volumetric_speed", new ConfigOptionFloat(outer_wall_volumetric_speed));
        }

    }
    std::string machine_start_gcode = this->placeholder_parser_process("machine_start_gcode", print.config().machine_start_gcode.value, initial_extruder_id);
    if (print.config().gcode_flavor != gcfKlipper) {
        // Set bed temperature if the start G-code does not contain any bed temp control G-codes.
        this->_print_first_layer_bed_temperature(file, print, machine_start_gcode, initial_extruder_id, true);
        // Set extruder(s) temperature before and after start G-code.
        this->_print_first_layer_extruder_temperatures(file, print, machine_start_gcode, initial_extruder_id, false);
    }

    // adds tag for processor
    file.write_format(";%s%s\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Role).c_str(), ExtrusionEntity::role_to_string(erCustom).c_str());

    // Write the custom start G-code
    file.writeln(machine_start_gcode);

    if (print.config().gcode_flavor == gcfKlipper) {

      this->_print_first_layer_bed_temperature(file, print, "",
                                               initial_extruder_id, true);
      this->_print_first_layer_extruder_temperatures(
          file, print, "", initial_extruder_id, false);
    }
    //BBS: gcode writer doesn't know where the real position of extruder is after inserting custom gcode
    m_writer.set_current_position_clear(false);

    // Process filament-specific gcode.
   /* if (has_wipe_tower) {
        // Wipe tower will control the extruder switching, it will call the filament_start_gcode.
    } else {
            DynamicConfig config;
            config.set_key_value("filament_extruder_id", new ConfigOptionInt(int(initial_extruder_id)));
            file.writeln(this->placeholder_parser_process("filament_start_gcode", print.config().filament_start_gcode.values[initial_extruder_id], initial_extruder_id, &config));
    }
*/
    this->_print_first_layer_extruder_temperatures(file, print, machine_start_gcode, initial_extruder_id, true);
    print.throw_if_canceled();

    // Set other general things.
    file.write(this->preamble());

    // Calculate wiping points if needed
    DoExport::init_ooze_prevention(print, m_ooze_prevention);
    print.throw_if_canceled();

    // Collect custom seam data from all objects.
    std::function<void(void)> throw_if_canceled_func = [&print]() { print.throw_if_canceled(); };
    m_seam_placer.init(print, throw_if_canceled_func);

    // BBS: get path for change filament
    if (m_writer.multiple_extruders) {
        std::vector<Vec2d> points = get_path_of_change_filament(print);
        if (points.size() == 3) {
            travel_point_1 = points[0];
            travel_point_2 = points[1];
            travel_point_3 = points[2];
        }
    }

    // BBS: priming logic is removed, always set first extruer here.
    //if (! (has_wipe_tower && print.config().single_extruder_multi_material_priming))
    {
        // Set initial extruder only after custom start G-code.
        // Ugly hack: Do not set the initial extruder if the extruder is primed using the MMU priming towers at the edge of the print bed.
        file.write(this->set_extruder(initial_extruder_id, 0.));
    }
    // BBS: set that indicates objs with brim
    for (auto iter = print.m_brimMap.begin(); iter != print.m_brimMap.end(); ++iter) {
        if (!iter->second.empty())
            this->m_objsWithBrim.insert(iter->first);
    }
    for (auto iter = print.m_supportBrimMap.begin(); iter != print.m_supportBrimMap.end(); ++iter) {
        if (!iter->second.empty())
            this->m_objSupportsWithBrim.insert(iter->first);
    }
    if (this->m_objsWithBrim.empty() && this->m_objSupportsWithBrim.empty()) m_brim_done = true;

    // SoftFever: calib
    if (print.calib_params().mode == CalibMode::Calib_PA_Line) {
        std::string gcode;
        gcode += m_writer.set_acceleration((unsigned int)floor(m_config.outer_wall_acceleration.value + 0.5));

        if (m_config.default_jerk.value > 0) {
            double jerk = m_config.outer_wall_jerk.value;
            gcode += m_writer.set_jerk_xy(jerk);
        }

        calib_pressure_advance pa_test(this);
        double filament_max_volumetric_speed = m_config.option<ConfigOptionFloats>("filament_max_volumetric_speed")->get_at(initial_extruder_id);
        Flow pattern_line = Flow(pa_test.line_width(), 0.2, m_config.nozzle_diameter.get_at(0));
        auto fast_speed = std::min(print.default_region_config().outer_wall_speed.value, filament_max_volumetric_speed / pattern_line.mm3_per_mm());
        auto slow_speed = std::max(20.0, fast_speed / 10.0);
        pa_test.set_speed(fast_speed, slow_speed);
        pa_test.draw_numbers() = print.calib_params().print_numbers;
        auto params = print.calib_params();
        gcode += pa_test.generate_test(params.start, params.step, std::llround(std::ceil((params.end - params.start) / params.step)));

        file.write(gcode);
    } else {
        //BBS: open spaghetti detector
        if (is_bbl_printers) {
            // if (print.config().spaghetti_detector.value)
            file.write("M981 S1 P20000 ;open spaghetti detector\n");
        }

        // Do all objects for each layer.
        if (print.config().print_sequence == PrintSequence::ByObject && !has_wipe_tower) {
            size_t finished_objects = 0;
            const PrintObject *prev_object = (*print_object_instance_sequential_active)->print_object;
            for (; print_object_instance_sequential_active != print_object_instances_ordering.end(); ++ print_object_instance_sequential_active) {
                const PrintObject &object = *(*print_object_instance_sequential_active)->print_object;
                if (&object != prev_object || tool_ordering.first_extruder() != final_extruder_id) {
                    tool_ordering = ToolOrdering(object, final_extruder_id);
                    unsigned int new_extruder_id = tool_ordering.first_extruder();
                    if (new_extruder_id == (unsigned int)-1)
                        // Skip this object.
                        continue;
                    initial_extruder_id = new_extruder_id;
                    final_extruder_id   = tool_ordering.last_extruder();
                    assert(final_extruder_id != (unsigned int)-1);
                }
                print.throw_if_canceled();
                this->set_origin(unscale((*print_object_instance_sequential_active)->shift));

                // BBS: prime extruder if extruder change happens before this object instance
                bool prime_extruder = false;
                if (finished_objects > 0) {
                    // Move to the origin position for the copy we're going to print.
                    // This happens before Z goes down to layer 0 again, so that no collision happens hopefully.
                    m_enable_cooling_markers = false; // we're not filtering these moves through CoolingBuffer
                    m_avoid_crossing_perimeters.use_external_mp_once();
                    // BBS. change tool before moving to origin point.
                    if (m_writer.need_toolchange(initial_extruder_id)) {
                        const PrintObjectConfig& object_config = object.config();
                        coordf_t initial_layer_print_height = print.config().initial_layer_print_height.value;
                        file.write(this->set_extruder(initial_extruder_id, initial_layer_print_height));
                        prime_extruder = true;
                    }
                    else {
                        file.write(this->retract());
                    }
                    file.write(m_writer.travel_to_z(m_max_layer_z));
                    file.write(this->travel_to(Point(0, 0), erNone, "move to origin position for next object"));
                    m_enable_cooling_markers = true;
                    // Disable motion planner when traveling to first object point.
                    m_avoid_crossing_perimeters.disable_once();
                    // Ff we are printing the bottom layer of an object, and we have already finished
                    // another one, set first layer temperatures. This happens before the Z move
                    // is triggered, so machine has more time to reach such temperatures.
                    m_placeholder_parser.set("current_object_idx", int(finished_objects));
                    //BBS: remove printing_by_object_gcode
                    //std::string printing_by_object_gcode = this->placeholder_parser_process("printing_by_object_gcode", print.config().printing_by_object_gcode.value, initial_extruder_id);
                    std::string printing_by_object_gcode;
                    // Set first layer bed and extruder temperatures, don't wait for it to reach the temperature.
                    this->_print_first_layer_bed_temperature(file, print, printing_by_object_gcode, initial_extruder_id, false);
                    this->_print_first_layer_extruder_temperatures(file, print, printing_by_object_gcode, initial_extruder_id, false);
                    file.writeln(printing_by_object_gcode);
                }
                // Reset the cooling buffer internal state (the current position, feed rate, accelerations).
                m_cooling_buffer->reset(this->writer().get_position());
                m_cooling_buffer->set_current_extruder(initial_extruder_id);
                // Process all layers of a single object instance (sequential mode) with a parallel pipeline:
                // Generate G-code, run the filters (vase mode, cooling buffer), run the G-code analyser
                // and export G-code into file.
                this->process_layers(print, tool_ordering, collect_layers_to_print(object), *print_object_instance_sequential_active - object.instances().data(), file, prime_extruder);
                //BBS: close powerlost recovery
                {
                    if (is_bbl_printers && m_second_layer_things_done) {
                        file.write("; close powerlost recovery\n");
                        file.write("M1003 S0\n");
                    }
                }
    #ifdef HAS_PRESSURE_EQUALIZER
                if (m_pressure_equalizer)
                    file.write(m_pressure_equalizer->process("", true));
    #endif /* HAS_PRESSURE_EQUALIZER */
                ++ finished_objects;
                // Flag indicating whether the nozzle temperature changes from 1st to 2nd layer were performed.
                // Reset it when starting another object from 1st layer.
                m_second_layer_things_done = false;
                prev_object = &object;
            }
        } else {
            // Sort layers by Z.
            // All extrusion moves with the same top layer height are extruded uninterrupted.
            std::vector<std::pair<coordf_t, std::vector<LayerToPrint>>> layers_to_print = collect_layers_to_print(print);
            // Prusa Multi-Material wipe tower.
            if (has_wipe_tower && ! layers_to_print.empty()) {
                m_wipe_tower.reset(new WipeTowerIntegration(print.config(), print.get_plate_index(), print.get_plate_origin(), * print.wipe_tower_data().priming.get(), print.wipe_tower_data().tool_changes, *print.wipe_tower_data().final_purge.get()));
                //BBS
                //file.write(m_writer.travel_to_z(initial_layer_print_height + m_config.z_offset.value, "Move to the first layer height"));
                file.write(m_writer.travel_to_z(initial_layer_print_height, "Move to the first layer height"));
    #if 0
                if (print.config().single_extruder_multi_material_priming) {
                    file.write(m_wipe_tower->prime(*this));
                    // Verify, whether the print overaps the priming extrusions.
                    BoundingBoxf bbox_print(get_print_extrusions_extents(print));
                    coordf_t twolayers_printz = ((layers_to_print.size() == 1) ? layers_to_print.front() : layers_to_print[1]).first + EPSILON;
                    for (const PrintObject *print_object : print.objects())
                        bbox_print.merge(get_print_object_extrusions_extents(*print_object, twolayers_printz));
                    bbox_print.merge(get_wipe_tower_extrusions_extents(print, twolayers_printz));
                    BoundingBoxf bbox_prime(get_wipe_tower_priming_extrusions_extents(print));
                    bbox_prime.offset(0.5f);
                    bool overlap = bbox_prime.overlap(bbox_print);

                    if (print.config().gcode_flavor == gcfMarlinLegacy || print.config().gcode_flavor == gcfMarlinFirmware) {
                        file.write(this->retract());
                        file.write("M300 S800 P500\n"); // Beep for 500ms, tone 800Hz.
                        if (overlap) {
                            // Wait for the user to remove the priming extrusions.
                            file.write("M1 Remove priming towers and click button.\n");
                        } else {
                            // Just wait for a bit to let the user check, that the priming succeeded.
                            //TODO Add a message explaining what the printer is waiting for. This needs a firmware fix.
                            file.write("M1 S10\n");
                        }
                    }
                    //BBS: only support Marlin
                    //else {
                        // This is not Marlin, M1 command is probably not supported.
                        //if (overlap) {
                        //    print.active_step_add_warning(PrintStateBase::WarningLevel::CRITICAL,
                        //        _(L("Your print is very close to the priming regions. "
                        //          "Make sure there is no collision.")));
                        //} else {
                        //    // Just continue printing, no action necessary.
                        //}
                    //}
                }
    #endif
                print.throw_if_canceled();
            }
            // Process all layers of all objects (non-sequential mode) with a parallel pipeline:
            // Generate G-code, run the filters (vase mode, cooling buffer), run the G-code analyser
            // and export G-code into file.
            this->process_layers(print, tool_ordering, print_object_instances_ordering, layers_to_print, file);
            //BBS: close powerlost recovery
            {
                if (is_bbl_printers && m_second_layer_things_done) {
                    file.write("; close powerlost recovery\n");
                    file.write("M1003 S0\n");
                }
            }
    #ifdef HAS_PRESSURE_EQUALIZER
            if (m_pressure_equalizer)
                file.write(m_pressure_equalizer->process("", true));
    #endif /* HAS_PRESSURE_EQUALIZER */
            if (m_wipe_tower)
                // Purge the extruder, pull out the active filament.
                file.write(m_wipe_tower->finalize(*this));
        }
    }
    //BBS: the last retraction
    // Write end commands to file.
    file.write(this->retract(false, true));
    file.write(m_writer.set_fan(0));
    //BBS: make sure the additional fan is closed when end
    if(m_config.auxiliary_fan.value)
        file.write(m_writer.set_additional_fan(0));
    if (is_bbl_printers) {
        //BBS: close spaghetti detector
        //Note: M981 is also used to tell xcam the last layer is finished, so we need always send it even if spaghetti option is disabled.
        //if (print.config().spaghetti_detector.value)
        file.write("M981 S0 P20000 ; close spaghetti detector\n");
    }

    // adds tag for processor
    file.write_format(";%s%s\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Role).c_str(), ExtrusionEntity::role_to_string(erCustom).c_str());

    // Process filament-specific gcode in extruder order.
    {
        DynamicConfig config;
        config.set_key_value("layer_num", new ConfigOptionInt(m_layer_index));
        //BBS
        //config.set_key_value("layer_z",   new ConfigOptionFloat(m_writer.get_position()(2) - m_config.z_offset.value));
        config.set_key_value("layer_z",   new ConfigOptionFloat(m_writer.get_position()(2)));
        config.set_key_value("max_layer_z", new ConfigOptionFloat(m_max_layer_z));
        if (print.config().single_extruder_multi_material) {
            // Process the filament_end_gcode for the active filament only.
            int extruder_id = m_writer.extruder()->id();
            config.set_key_value("filament_extruder_id", new ConfigOptionInt(extruder_id));
            file.writeln(this->placeholder_parser_process("filament_end_gcode", print.config().filament_end_gcode.get_at(extruder_id), extruder_id, &config));
        } else {
            for (const std::string &end_gcode : print.config().filament_end_gcode.values) {
                int extruder_id = (unsigned int)(&end_gcode - &print.config().filament_end_gcode.values.front());
                config.set_key_value("filament_extruder_id", new ConfigOptionInt(extruder_id));
                file.writeln(this->placeholder_parser_process("filament_end_gcode", end_gcode, extruder_id, &config));
            }
        }
        file.writeln(this->placeholder_parser_process("machine_end_gcode", print.config().machine_end_gcode, m_writer.extruder()->id(), &config));
    }
    file.write(m_writer.update_progress(m_layer_count, m_layer_count, true)); // 100%
    file.write(m_writer.postamble());

    // adds tags for time estimators
    file.write_format(";%s\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Last_Line_M73_Placeholder).c_str());
    file.write_format("; EXECUTABLE_BLOCK_END\n\n");

    print.throw_if_canceled();

    // Get filament stats.
    file.write(DoExport::update_print_stats_and_format_filament_stats(
    	// Const inputs
        has_wipe_tower, print.wipe_tower_data(),
        m_writer.extruders(),
        // Modifies
        print.m_print_statistics));
    if (!is_bbl_printers) {
        file.write_format("; total filament used [g] = %.2lf\n",
            print.m_print_statistics.total_weight);
        file.write_format("; total filament cost = %.2lf\n",
            print.m_print_statistics.total_cost);
        if (print.m_print_statistics.total_toolchanges > 0)
            file.write_format("; total filament change = %i\n",
                print.m_print_statistics.total_toolchanges);
        file.write_format("; total layers count = %i\n", m_layer_count);
        file.write_format(
            ";%s\n",
            GCodeProcessor::reserved_tag(
                GCodeProcessor::ETags::Estimated_Printing_Time_Placeholder)
            .c_str());
      file.write("\n");
      file.write("; CONFIG_BLOCK_START\n");
      std::string full_config;
      append_full_config(print, full_config);
      if (!full_config.empty())
        file.write(full_config);

      // SoftFever: write compatiple info
      int first_layer_bed_temperature = get_bed_temperature(0, true, print.config().curr_bed_type);
      file.write_format("; first_layer_bed_temperature = %d\n", first_layer_bed_temperature);
      file.write_format("; bed_shape = %s\n", print.full_print_config().opt_serialize("printable_area").c_str());
      file.write_format("; first_layer_temperature = %d\n", print.config().nozzle_temperature_initial_layer.get_at(0));
      file.write_format("; first_layer_height = %.3f\n", print.config().initial_layer_print_height.value);
        
        //SF TODO
//      file.write_format("; variable_layer_height = %d\n", print.ad.adaptive_layer_height ? 1 : 0);
   
      file.write("; CONFIG_BLOCK_END\n\n");

    }
    file.write("\n");

    print.throw_if_canceled();
}

//BBS
void GCode::check_placeholder_parser_failed()
{
    if (! m_placeholder_parser_failed_templates.empty()) {
        // G-code export proceeded, but some of the PlaceholderParser substitutions failed.
        std::string msg = Slic3r::format(_(L("Failed to generate gcode for invalid custom G-code.\n\n")));
        for (const auto &name_and_error : m_placeholder_parser_failed_templates)
            msg += name_and_error.first + " " + name_and_error.second + "\n";
        msg += Slic3r::format(_(L("Please check the custom G-code or use the default custom G-code.")));
        throw Slic3r::PlaceholderParserError(msg);
    }
}

// Process all layers of all objects (non-sequential mode) with a parallel pipeline:
// Generate G-code, run the filters (vase mode, cooling buffer), run the G-code analyser
// and export G-code into file.
void GCode::process_layers(
    const Print                                                         &print,
    const ToolOrdering                                                  &tool_ordering,
    const std::vector<const PrintInstance*>                             &print_object_instances_ordering,
    const std::vector<std::pair<coordf_t, std::vector<LayerToPrint>>>   &layers_to_print,
    GCodeOutputStream                                                   &output_stream)
{
    // The pipeline is variable: The vase mode filter is optional.
    size_t layer_to_print_idx = 0;
    const auto generator = tbb::make_filter<void, GCode::LayerResult>(slic3r_tbb_filtermode::serial_in_order,
        [this, &print, &tool_ordering, &print_object_instances_ordering, &layers_to_print, &layer_to_print_idx](tbb::flow_control& fc) -> GCode::LayerResult {
            if (layer_to_print_idx == layers_to_print.size()) {
                fc.stop();
                return {};
            } else {
                const std::pair<coordf_t, std::vector<LayerToPrint>>& layer = layers_to_print[layer_to_print_idx++];
                const LayerTools& layer_tools = tool_ordering.tools_for_layer(layer.first);
                print.set_status(80, Slic3r::format(_(L("Generating G-code: layer %1%")), std::to_string(layer_to_print_idx)));
                if (m_wipe_tower && layer_tools.has_wipe_tower)
                    m_wipe_tower->next_layer();
                //BBS
                check_placeholder_parser_failed();
                print.throw_if_canceled();
                return this->process_layer(print, layer.second, layer_tools, &layer == &layers_to_print.back(), &print_object_instances_ordering, size_t(-1));
            }
        });
    const auto spiral_mode = tbb::make_filter<GCode::LayerResult, GCode::LayerResult>(slic3r_tbb_filtermode::serial_in_order,
        [&spiral_mode = *this->m_spiral_vase.get()](GCode::LayerResult in) -> GCode::LayerResult {
            spiral_mode.enable(in.spiral_vase_enable);
            return { spiral_mode.process_layer(std::move(in.gcode)), in.layer_id, in.spiral_vase_enable, in.cooling_buffer_flush };
        });
    const auto cooling = tbb::make_filter<GCode::LayerResult, std::string>(slic3r_tbb_filtermode::serial_in_order,
        [&cooling_buffer = *this->m_cooling_buffer.get()](GCode::LayerResult in) -> std::string {
            return cooling_buffer.process_layer(std::move(in.gcode), in.layer_id, in.cooling_buffer_flush);
        });
    const auto output = tbb::make_filter<std::string, void>(slic3r_tbb_filtermode::serial_in_order,
        [&output_stream](std::string s) { output_stream.write(s); }
    );

    // The pipeline elements are joined using const references, thus no copying is performed.
    if (m_spiral_vase)
        tbb::parallel_pipeline(12, generator & spiral_mode & cooling & output);
    else
        tbb::parallel_pipeline(12, generator & cooling & output);
}

// Process all layers of a single object instance (sequential mode) with a parallel pipeline:
// Generate G-code, run the filters (vase mode, cooling buffer), run the G-code analyser
// and export G-code into file.
void GCode::process_layers(
    const Print                             &print,
    const ToolOrdering                      &tool_ordering,
    std::vector<LayerToPrint>                layers_to_print,
    const size_t                             single_object_idx,
    GCodeOutputStream                       &output_stream,
    // BBS
    const bool                               prime_extruder)
{
    // The pipeline is variable: The vase mode filter is optional.
    size_t layer_to_print_idx = 0;
    const auto generator = tbb::make_filter<void, GCode::LayerResult>(slic3r_tbb_filtermode::serial_in_order,
        [this, &print, &tool_ordering, &layers_to_print, &layer_to_print_idx, single_object_idx, prime_extruder](tbb::flow_control& fc) -> GCode::LayerResult {
            if (layer_to_print_idx == layers_to_print.size()) {
                fc.stop();
                return {};
            } else {
                LayerToPrint &layer = layers_to_print[layer_to_print_idx ++];
                print.set_status(80, Slic3r::format(_(L("Generating G-code: layer %1%")), std::to_string(layer_to_print_idx)));
                //BBS
                check_placeholder_parser_failed();
                print.throw_if_canceled();
                return this->process_layer(print, { std::move(layer) }, tool_ordering.tools_for_layer(layer.print_z()), &layer == &layers_to_print.back(), nullptr, single_object_idx, prime_extruder);
            }
        });
    const auto spiral_mode = tbb::make_filter<GCode::LayerResult, GCode::LayerResult>(slic3r_tbb_filtermode::serial_in_order,
        [&spiral_mode = *this->m_spiral_vase.get()](GCode::LayerResult in)->GCode::LayerResult {
            spiral_mode.enable(in.spiral_vase_enable);
            return { spiral_mode.process_layer(std::move(in.gcode)), in.layer_id, in.spiral_vase_enable, in.cooling_buffer_flush };
        });
    const auto cooling = tbb::make_filter<GCode::LayerResult, std::string>(slic3r_tbb_filtermode::serial_in_order,
        [&cooling_buffer = *this->m_cooling_buffer.get()](GCode::LayerResult in)->std::string {
            return cooling_buffer.process_layer(std::move(in.gcode), in.layer_id, in.cooling_buffer_flush);
        });
    const auto output = tbb::make_filter<std::string, void>(slic3r_tbb_filtermode::serial_in_order,
        [&output_stream](std::string s) { output_stream.write(s); }
    );

    // The pipeline elements are joined using const references, thus no copying is performed.
    if (m_spiral_vase)
        tbb::parallel_pipeline(12, generator & spiral_mode & cooling & output);
    else
        tbb::parallel_pipeline(12, generator & cooling & output);
}

std::string GCode::placeholder_parser_process(const std::string &name, const std::string &templ, unsigned int current_extruder_id, const DynamicConfig *config_override)
{
    try {
        return m_placeholder_parser.process(templ, current_extruder_id, config_override, &m_placeholder_parser_context);
    } catch (std::runtime_error &err) {
        // Collect the names of failed template substitutions for error reporting.
        auto it = m_placeholder_parser_failed_templates.find(name);
        if (it == m_placeholder_parser_failed_templates.end())
            // Only if there was no error reported for this template, store the first error message into the map to be reported.
            // We don't want to collect error message for each and every occurence of a single custom G-code section.
            m_placeholder_parser_failed_templates.insert(it, std::make_pair(name, std::string(err.what())));
        // Insert the macro error message into the G-code.
        return
            std::string("\n!!!!! Failed to process the custom G-code template ") + name + "\n" +
            err.what() +
            "!!!!! End of an error report for the custom G-code template " + name + "\n\n";
    }
}

// Parse the custom G-code, try to find mcode_set_temp_dont_wait and mcode_set_temp_and_wait or optionally G10 with temperature inside the custom G-code.
// Returns true if one of the temp commands are found, and try to parse the target temperature value into temp_out.
static bool custom_gcode_sets_temperature(const std::string &gcode, const int mcode_set_temp_dont_wait, const int mcode_set_temp_and_wait, const bool include_g10, int &temp_out)
{
    temp_out = -1;
    if (gcode.empty())
        return false;

    const char *ptr = gcode.data();
    bool temp_set_by_gcode = false;
    while (*ptr != 0) {
        // Skip whitespaces.
        for (; *ptr == ' ' || *ptr == '\t'; ++ ptr);
        if (*ptr == 'M' || // Line starts with 'M'. It is a machine command.
            (*ptr == 'G' && include_g10)) { // Only check for G10 if requested
            bool is_gcode = *ptr == 'G';
            ++ ptr;
            // Parse the M or G code value.
            char *endptr = nullptr;
            int mgcode = int(strtol(ptr, &endptr, 10));
            if (endptr != nullptr && endptr != ptr &&
                is_gcode ?
                    // G10 found
                    mgcode == 10 :
                    // M104/M109 or M140/M190 found.
                    (mgcode == mcode_set_temp_dont_wait || mgcode == mcode_set_temp_and_wait)) {
                ptr = endptr;
                if (! is_gcode)
                    // Let the caller know that the custom M-code sets the temperature.
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
                            // Let the caller know that the custom G-code sets the temperature
                            // Only do this after successfully parsing temperature since G10
                            // can be used for other reasons
                            temp_set_by_gcode = true;
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

// Print the machine envelope G-code for the Marlin firmware based on the "machine_max_xxx" parameters.
// Do not process this piece of G-code by the time estimator, it already knows the values through another sources.
void GCode::print_machine_envelope(GCodeOutputStream &file, Print &print)
{
    const auto flavor = print.config().gcode_flavor.value;
    if (flavor == gcfMarlinLegacy || flavor == gcfMarlinFirmware) {
        int factor = flavor == gcfRepRapFirmware ? 60 : 1; // RRF M203 and M566 are in mm/min
        file.write_format("M201 X%d Y%d Z%d E%d\n",
            int(print.config().machine_max_acceleration_x.values.front() + 0.5),
            int(print.config().machine_max_acceleration_y.values.front() + 0.5),
            int(print.config().machine_max_acceleration_z.values.front() + 0.5),
            int(print.config().machine_max_acceleration_e.values.front() + 0.5));
        file.write_format("M203 X%d Y%d Z%d E%d\n",
            int(print.config().machine_max_speed_x.values.front() * factor + 0.5),
            int(print.config().machine_max_speed_y.values.front() * factor + 0.5),
            int(print.config().machine_max_speed_z.values.front() * factor + 0.5),
            int(print.config().machine_max_speed_e.values.front() * factor + 0.5));

        // Now M204 - acceleration. This one is quite hairy thanks to how Marlin guys care about
        // Legacy Marlin should export travel acceleration the same as printing acceleration.
        // MarlinFirmware has the two separated.
        int travel_acc = flavor == gcfMarlinLegacy
                       ? int(print.config().machine_max_acceleration_extruding.values.front() + 0.5)
                       : int(print.config().machine_max_acceleration_travel.values.front() + 0.5);
        if (flavor == gcfRepRapFirmware)
            file.write_format("M204 P%d T%d ; sets acceleration (P, T), mm/sec^2\n",
                int(print.config().machine_max_acceleration_extruding.values.front() + 0.5),
                travel_acc);
        else
            file.write_format("M204 P%d R%d T%d\n",
                int(print.config().machine_max_acceleration_extruding.values.front() + 0.5),
                int(print.config().machine_max_acceleration_retracting.values.front() + 0.5),
                travel_acc);

        assert(is_decimal_separator_point());
        file.write_format(flavor == gcfRepRapFirmware
            ? "M566 X%.2lf Y%.2lf Z%.2lf E%.2lf ; sets the jerk limits, mm/min\n"
            : "M205 X%.2lf Y%.2lf Z%.2lf E%.2lf ; sets the jerk limits, mm/sec\n",
            print.config().machine_max_jerk_x.values.front() * factor,
            print.config().machine_max_jerk_y.values.front() * factor,
            print.config().machine_max_jerk_z.values.front() * factor,
            print.config().machine_max_jerk_e.values.front() * factor);

    }
}

// BBS
int GCode::get_bed_temperature(const int extruder_id, const bool is_first_layer, const BedType bed_type) const
{
    std::string bed_temp_key = is_first_layer ? get_bed_temp_1st_layer_key(bed_type) : get_bed_temp_key(bed_type);
    const ConfigOptionInts* bed_temp_opt = m_config.option<ConfigOptionInts>(bed_temp_key);
    return bed_temp_opt->get_at(extruder_id);
}

// Write 1st layer bed temperatures into the G-code.
// Only do that if the start G-code does not already contain any M-code controlling an extruder temperature.
// M140 - Set Extruder Temperature
// M190 - Set Extruder Temperature and Wait
void GCode::_print_first_layer_bed_temperature(GCodeOutputStream &file, Print &print, const std::string &gcode, unsigned int first_printing_extruder_id, bool wait)
{
    // Initial bed temperature based on the first extruder.
    // BBS
    std::vector<int> temps_per_bed;
    int bed_temp = get_bed_temperature(first_printing_extruder_id, true, print.config().curr_bed_type);

    // Is the bed temperature set by the provided custom G-code?
    int  temp_by_gcode     = -1;
    bool temp_set_by_gcode = custom_gcode_sets_temperature(gcode, 140, 190, false, temp_by_gcode);
    // BBS
#if 0
    if (temp_set_by_gcode && temp_by_gcode >= 0 && temp_by_gcode < 1000)
        temp = temp_by_gcode;
#endif

    // Always call m_writer.set_bed_temperature() so it will set the internal "current" state of the bed temp as if
    // the custom start G-code emited these.
    std::string set_temp_gcode = m_writer.set_bed_temperature(bed_temp, wait);
    if (! temp_set_by_gcode)
        file.write(set_temp_gcode);
}

// Write 1st layer extruder temperatures into the G-code.
// Only do that if the start G-code does not already contain any M-code controlling an extruder temperature.
// M104 - Set Extruder Temperature
// M109 - Set Extruder Temperature and Wait
// RepRapFirmware: G10 Sxx
void GCode::_print_first_layer_extruder_temperatures(GCodeOutputStream &file, Print &print, const std::string &gcode, unsigned int first_printing_extruder_id, bool wait)
{
    // Is the bed temperature set by the provided custom G-code?
    int  temp_by_gcode = -1;
    bool include_g10   = print.config().gcode_flavor == gcfRepRapFirmware;
    if (custom_gcode_sets_temperature(gcode, 104, 109, include_g10, temp_by_gcode)) {
        // Set the extruder temperature at m_writer, but throw away the generated G-code as it will be written with the custom G-code.
        int temp = print.config().nozzle_temperature_initial_layer.get_at(first_printing_extruder_id);
        if (temp_by_gcode >= 0 && temp_by_gcode < 1000)
            temp = temp_by_gcode;
        m_writer.set_temperature(temp, wait, first_printing_extruder_id);
    } else {
        // Custom G-code does not set the extruder temperature. Do it now.
        if (print.config().single_extruder_multi_material.value) {
            // Set temperature of the first printing extruder only.
            int temp = print.config().nozzle_temperature_initial_layer.get_at(first_printing_extruder_id);
            if (temp > 0)
                file.write(m_writer.set_temperature(temp, wait, first_printing_extruder_id));
        } else {
            // Set temperatures of all the printing extruders.
            for (unsigned int tool_id : print.extruders()) {
                int temp = print.config().nozzle_temperature_initial_layer.get_at(tool_id);
                if (print.config().ooze_prevention.value)
                    temp += print.config().standby_temperature_delta.value;
                if (temp > 0)
                    file.write(m_writer.set_temperature(temp, wait, tool_id));
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

std::vector<GCode::InstanceToPrint> GCode::sort_print_object_instances(
    std::vector<GCode::ObjectByExtruder> 		&objects_by_extruder,
    const std::vector<LayerToPrint> 			&layers,
    // Ordering must be defined for normal (non-sequential print).
    const std::vector<const PrintInstance*> 	*ordering,
    // For sequential print, the instance of the object to be printing has to be defined.
    const size_t                     		 	 single_object_instance_idx)
{
    std::vector<InstanceToPrint> out;

    if (ordering == nullptr) {
        // Sequential print, single object is being printed.
        for (ObjectByExtruder &object_by_extruder : objects_by_extruder) {
            const size_t       layer_id     = &object_by_extruder - objects_by_extruder.data();
            //BBS:add the support of shared print object
            const PrintObject *print_object = layers[layer_id].original_object;
            //const PrintObject *print_object = layers[layer_id].object();
            if (print_object)
                out.emplace_back(object_by_extruder, layer_id, *print_object, single_object_instance_idx);
        }
    } else {
        // Create mapping from PrintObject* to ObjectByExtruder*.
        std::vector<std::pair<const PrintObject*, ObjectByExtruder*>> sorted;
        sorted.reserve(objects_by_extruder.size());
        for (ObjectByExtruder &object_by_extruder : objects_by_extruder) {
            const size_t       layer_id     = &object_by_extruder - objects_by_extruder.data();
            //BBS:add the support of shared print object
            const PrintObject *print_object = layers[layer_id].original_object;
            //const PrintObject *print_object = layers[layer_id].object();
            if (print_object)
                sorted.emplace_back(print_object, &object_by_extruder);
        }
        std::sort(sorted.begin(), sorted.end());

        if (! sorted.empty()) {
            out.reserve(sorted.size());
            for (const PrintInstance *instance : *ordering) {
                const PrintObject &print_object = *instance->print_object;
                //BBS:add the support of shared print object
                //const PrintObject* print_obj_ptr = &print_object;
                //if (print_object.get_shared_object())
                //    print_obj_ptr = print_object.get_shared_object();
                std::pair<const PrintObject*, ObjectByExtruder*> key(&print_object, nullptr);
                auto it = std::lower_bound(sorted.begin(), sorted.end(), key);
                if (it != sorted.end() && it->first == &print_object)
                    // ObjectByExtruder for this PrintObject was found.
                    out.emplace_back(*it->second, it->second - objects_by_extruder.data(), print_object, instance - print_object.instances().data());
            }
        }
    }
    return out;
}

namespace ProcessLayer
{

    static std::string emit_custom_gcode_per_print_z(
        GCode                                                   &gcodegen,
        const CustomGCode::Item 								*custom_gcode,
        unsigned int                                             current_extruder_id,
        // ID of the first extruder printing this layer.
        unsigned int                                             first_extruder_id,
        const PrintConfig                                       &config)
    {
        std::string gcode;
        // BBS
        bool single_filament_print = config.filament_diameter.size() == 1;

        if (custom_gcode != nullptr) {
            // Extruder switches are processed by LayerTools, they should be filtered out.
            assert(custom_gcode->type != CustomGCode::ToolChange);

            CustomGCode::Type   gcode_type = custom_gcode->type;
            bool  				color_change = gcode_type == CustomGCode::ColorChange;
            bool 				tool_change = gcode_type == CustomGCode::ToolChange;
            // Tool Change is applied as Color Change for a single extruder printer only.
            assert(!tool_change || single_filament_print);

            std::string pause_print_msg;
            int m600_extruder_before_layer = -1;
            if (color_change && custom_gcode->extruder > 0)
                m600_extruder_before_layer = custom_gcode->extruder - 1;
            else if (gcode_type == CustomGCode::PausePrint)
                pause_print_msg = custom_gcode->extra;
            //BBS: inserting color gcode is removed
#if 0
            // we should add or not colorprint_change in respect to nozzle_diameter count instead of really used extruders count
            if (color_change || tool_change)
            {
                assert(m600_extruder_before_layer >= 0);
                // Color Change or Tool Change as Color Change.
                // add tag for processor
                gcode += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Color_Change) + ",T" + std::to_string(m600_extruder_before_layer) + "," + custom_gcode->color + "\n";

                if (!single_filament_print && m600_extruder_before_layer >= 0 && first_extruder_id != (unsigned)m600_extruder_before_layer
                    // && !MMU1
                    ) {
                    //! FIXME_in_fw show message during print pause
                    DynamicConfig cfg;
                    cfg.set_key_value("color_change_extruder", new ConfigOptionInt(m600_extruder_before_layer));
                    gcode += gcodegen.placeholder_parser_process("machine_pause_gcode", config.machine_pause_gcode, current_extruder_id, &cfg);
                    gcode += "\n";
                    gcode += "M117 Change filament for Extruder " + std::to_string(m600_extruder_before_layer) + "\n";
                }
                else {
                    gcode += gcodegen.placeholder_parser_process("color_change_gcode", config.color_change_gcode, current_extruder_id);
                    gcode += "\n";
                    //FIXME Tell G-code writer that M600 filled the extruder, thus the G-code writer shall reset the extruder to unretracted state after
                    // return from M600. Thus the G-code generated by the following line is ignored.
                    // see GH issue #6362
                    gcodegen.writer().unretract();
                }
            }
            else {
#endif
                if (gcode_type == CustomGCode::PausePrint) // Pause print
                {
                    // add tag for processor
                    gcode += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Pause_Print) + "\n";
                    //! FIXME_in_fw show message during print pause
                    //if (!pause_print_msg.empty())
                    //    gcode += "M117 " + pause_print_msg + "\n";
                    gcode += gcodegen.placeholder_parser_process("machine_pause_gcode", config.machine_pause_gcode, current_extruder_id) + "\n";
                }
                else {
                    // add tag for processor
                    gcode += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Custom_Code) + "\n";
                    if (gcode_type == CustomGCode::Template)    // Template Custom Gcode
                        gcode += gcodegen.placeholder_parser_process("template_custom_gcode", config.template_custom_gcode, current_extruder_id);
                    else                                        // custom Gcode
                        gcode += custom_gcode->extra;

                }
                gcode += "\n";
#if 0
            }
#endif
        }

        return gcode;
    }
} // namespace ProcessLayer

namespace Skirt {
    static void skirt_loops_per_extruder_all_printing(const Print &print, const LayerTools &layer_tools, std::map<unsigned int, std::pair<size_t, size_t>> &skirt_loops_per_extruder_out)
    {
        // Prime all extruders printing over the 1st layer over the skirt lines.
        size_t n_loops = print.skirt().entities.size();
        size_t n_tools = layer_tools.extruders.size();
        size_t lines_per_extruder = (n_loops + n_tools - 1) / n_tools;

        // BBS. Extrude skirt with first extruder if min_skirt_length is zero
        const PrintConfig &config = print.config();
        if (Print::min_skirt_length < EPSILON) {
            skirt_loops_per_extruder_out[layer_tools.extruders.front()] = std::pair<size_t, size_t>(0, n_loops);
        } else {
            for (size_t i = 0; i < n_loops; i += lines_per_extruder)
                skirt_loops_per_extruder_out[layer_tools.extruders[i / lines_per_extruder]] = std::pair<size_t, size_t>(i, std::min(i + lines_per_extruder, n_loops));
        }
    }

    static std::map<unsigned int, std::pair<size_t, size_t>> make_skirt_loops_per_extruder_1st_layer(
        const Print             				&print,
        const LayerTools                		&layer_tools,
        // Heights (print_z) at which the skirt has already been extruded.
        std::vector<coordf_t>  			    	&skirt_done)
    {
        // Extrude skirt at the print_z of the raft layers and normal object layers
        // not at the print_z of the interlaced support material layers.
        std::map<unsigned int, std::pair<size_t, size_t>> skirt_loops_per_extruder_out;
        //For sequential print, the following test may fail when extruding the 2nd and other objects.
        // assert(skirt_done.empty());
        if (skirt_done.empty() && print.has_skirt() && ! print.skirt().entities.empty() && layer_tools.has_skirt) {
            skirt_loops_per_extruder_all_printing(print, layer_tools, skirt_loops_per_extruder_out);
            skirt_done.emplace_back(layer_tools.print_z);
        }
        return skirt_loops_per_extruder_out;
    }

    static std::map<unsigned int, std::pair<size_t, size_t>> make_skirt_loops_per_extruder_other_layers(
        const Print 							&print,
        const LayerTools                		&layer_tools,
        // Heights (print_z) at which the skirt has already been extruded.
        std::vector<coordf_t>			    	&skirt_done)
    {
        // Extrude skirt at the print_z of the raft layers and normal object layers
        // not at the print_z of the interlaced support material layers.
        std::map<unsigned int, std::pair<size_t, size_t>> skirt_loops_per_extruder_out;
        if (print.has_skirt() && ! print.skirt().entities.empty() && layer_tools.has_skirt &&
            // Not enough skirt layers printed yet.
            //FIXME infinite or high skirt does not make sense for sequential print!
            (skirt_done.size() < (size_t)print.config().skirt_height.value || print.has_infinite_skirt())) {
            bool valid = ! skirt_done.empty() && skirt_done.back() < layer_tools.print_z - EPSILON;
            assert(valid);
            // This print_z has not been extruded yet (sequential print)
            // FIXME: The skirt_done should not be empty at this point. The check is a workaround
            if (valid) {
#if 0
                // Prime just the first printing extruder. This is original Slic3r's implementation.
                skirt_loops_per_extruder_out[layer_tools.extruders.front()] = std::pair<size_t, size_t>(0, print.config().skirt_loops.value);
                skirt_loops_per_extruder_out[layer_tools.extruders.front()] = std::pair<size_t, size_t>(0, print.config().skirt_loops.value);
#else
                // Prime all extruders planned for this layer, see
                skirt_loops_per_extruder_all_printing(print, layer_tools, skirt_loops_per_extruder_out);
#endif
                assert(!skirt_done.empty());
                skirt_done.emplace_back(layer_tools.print_z);
            }
        }
        return skirt_loops_per_extruder_out;
    }

} // namespace Skirt

// In sequential mode, process_layer is called once per each object and its copy,
// therefore layers will contain a single entry and single_object_instance_idx will point to the copy of the object.
// In non-sequential mode, process_layer is called per each print_z height with all object and support layers accumulated.
// For multi-material prints, this routine minimizes extruder switches by gathering extruder specific extrusion paths
// and performing the extruder specific extrusions together.
GCode::LayerResult GCode::process_layer(
    const Print                    			&print,
    // Set of object & print layers of the same PrintObject and with the same print_z.
    const std::vector<LayerToPrint> 		&layers,
    const LayerTools        		        &layer_tools,
    const bool                               last_layer,
    // Pairs of PrintObject index and its instance index.
    const std::vector<const PrintInstance*> *ordering,
    // If set to size_t(-1), then print all copies of all objects.
    // Otherwise print a single copy of a single object.
    const size_t                     		 single_object_instance_idx,
    // BBS
    const bool                               prime_extruder)
{
    assert(! layers.empty());
    // Either printing all copies of all objects, or just a single copy of a single object.
    assert(single_object_instance_idx == size_t(-1) || layers.size() == 1);

    // First object, support and raft layer, if available.
    const Layer         *object_layer  = nullptr;
    const SupportLayer  *support_layer = nullptr;
    const SupportLayer  *raft_layer    = nullptr;
    for (const LayerToPrint &l : layers) {
        if (l.object_layer && ! object_layer)
            object_layer = l.object_layer;
        if (l.support_layer) {
            if (! support_layer)
                support_layer = l.support_layer;
            if (! raft_layer && support_layer->id() < support_layer->object()->slicing_parameters().raft_layers())
                raft_layer = support_layer;
        }
    }

    const Layer* layer_ptr = nullptr;
    if (object_layer != nullptr)
        layer_ptr = object_layer;
    else if (support_layer != nullptr)
        layer_ptr = support_layer;
    const Layer& layer = *layer_ptr;
    GCode::LayerResult   result { {}, layer.id(), false, last_layer };
    if (layer_tools.extruders.empty())
        // Nothing to extrude.
        return result;

    // Extract 1st object_layer and support_layer of this set of layers with an equal print_z.
    coordf_t             print_z       = layer.print_z;
    //BBS: using layer id to judge whether the layer is first layer is wrong. Because if the normal
    //support is attached above the object, and support layers has independent layer height, then the lowest support
    //interface layer id is 0.
    bool                 first_layer   = (layer.id() == 0 && abs(layer.bottom_z()) < EPSILON);
    m_writer.set_is_first_layer(first_layer);
    unsigned int         first_extruder_id = layer_tools.extruders.front();

    // Initialize config with the 1st object to be printed at this layer.
    m_config.apply(layer.object()->config(), true);

    // Check whether it is possible to apply the spiral vase logic for this layer.
    // Just a reminder: A spiral vase mode is allowed for a single object, single material print only.
    m_enable_loop_clipping = true;
    if (m_spiral_vase && layers.size() == 1 && support_layer == nullptr) {
        bool enable = (layer.id() > 0 || !print.has_brim()) && (layer.id() >= (size_t)print.config().skirt_height.value && ! print.has_infinite_skirt());
        if (enable) {
            for (const LayerRegion *layer_region : layer.regions())
                if (size_t(layer_region->region().config().bottom_shell_layers.value) > layer.id() ||
                    layer_region->perimeters.items_count() > 1u ||
                    layer_region->fills.items_count() > 0) {
                    enable = false;
                    break;
                }
        }
        result.spiral_vase_enable = enable;
        // If we're going to apply spiralvase to this layer, disable loop clipping.
        m_enable_loop_clipping = !enable;
    }

    std::string gcode;
    assert(is_decimal_separator_point()); // for the sprintfs

    // add tag for processor
    gcode += ";" + GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Layer_Change) + "\n";
    // export layer z
    char buf[64];
    sprintf(buf, print.is_BBL_printer() ? "; Z_HEIGHT: %g\n" : ";Z:%g\n", print_z);
    gcode += buf;
    // export layer height
    float height = first_layer ? static_cast<float>(print_z) : static_cast<float>(print_z) - m_last_layer_z;
    sprintf(buf, ";%s%g\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height).c_str(), height);
    gcode += buf;
    // update caches
    m_last_layer_z = static_cast<float>(print_z);
    m_max_layer_z  = std::max(m_max_layer_z, m_last_layer_z);
    m_last_height = height;

    // Set new layer - this will change Z and force a retraction if retract_when_changing_layer is enabled.
    if (! print.config().before_layer_change_gcode.value.empty()) {
        DynamicConfig config;
        config.set_key_value("layer_num",   new ConfigOptionInt(m_layer_index + 1));
        config.set_key_value("layer_z",     new ConfigOptionFloat(print_z));
        config.set_key_value("max_layer_z", new ConfigOptionFloat(m_max_layer_z));
        gcode += this->placeholder_parser_process("before_layer_change_gcode",
            print.config().before_layer_change_gcode.value, m_writer.extruder()->id(), &config)
            + "\n";
    }

    // BBS: don't use lazy_raise when enable spiral vase
    gcode += this->change_layer(print_z, !m_spiral_vase);  // this will increase m_layer_index
    m_layer = &layer;
    m_object_layer_over_raft = false;
    if (! print.config().layer_change_gcode.value.empty()) {
        DynamicConfig config;
        config.set_key_value("layer_num", new ConfigOptionInt(m_layer_index));
        config.set_key_value("layer_z",   new ConfigOptionFloat(print_z));
        gcode += this->placeholder_parser_process("layer_change_gcode",
            print.config().layer_change_gcode.value, m_writer.extruder()->id(), &config)
            + "\n";
        config.set_key_value("max_layer_z", new ConfigOptionFloat(m_max_layer_z));
    }

    if (print.calib_mode() == CalibMode::Calib_PA_Tower) {
        gcode += writer().set_pressure_advance(print.calib_params().start + static_cast<int>(print_z) * print.calib_params().step);
    } else if (print.calib_mode() == CalibMode::Calib_Temp_Tower) {
        auto offset = static_cast<unsigned int>(print_z / 10.001) * 5;
        gcode += writer().set_temperature(print.calib_params().start - offset);
    } else if (print.calib_mode() == CalibMode::Calib_VFA_Tower) {
        auto _speed = print.calib_params().start + std::floor(print_z / 5.0) * print.calib_params().step;
        m_calib_config.set_key_value("outer_wall_speed", new ConfigOptionFloat(std::round(_speed)));
    } else if (print.calib_mode() == CalibMode::Calib_Vol_speed_Tower) {
        auto _speed = print.calib_params().start + print_z * print.calib_params().step;
        m_calib_config.set_key_value("outer_wall_speed", new ConfigOptionFloat(std::round(_speed)));
    }

    //BBS
    if (first_layer) {
        //BBS: set first layer global acceleration
        if (m_config.default_acceleration.value > 0 && m_config.initial_layer_acceleration.value > 0) {
            double acceleration = m_config.initial_layer_acceleration.value;
            gcode += m_writer.set_acceleration((unsigned int)floor(acceleration + 0.5));
        }

        if (m_config.default_jerk.value > 0 && m_config.initial_layer_jerk.value > 0) {
            double jerk = m_config.initial_layer_jerk.value;
            gcode += m_writer.set_jerk_xy(jerk);
        }

    }

    if (! first_layer && ! m_second_layer_things_done) {
      if (print.is_BBL_printer()) {
        // BBS: open powerlost recovery
        {
          gcode += "; open powerlost recovery\n";
          gcode += "M1003 S1\n";
        }
        // BBS: open first layer inspection at second layer
        if (print.config().scan_first_layer.value) {
          // BBS: retract first to avoid droping when scan model
          gcode += this->retract();
          gcode += "M976 S1 P1 ; scan model before printing 2nd layer\n";
          gcode += "M400 P100\n";
          gcode += this->unretract();
        }
      }
        //BBS:  reset acceleration at sencond layer
        if (m_config.default_acceleration.value > 0 && m_config.initial_layer_acceleration.value > 0) {
            double acceleration = m_config.default_acceleration.value;
            gcode += m_writer.set_acceleration((unsigned int)floor(acceleration + 0.5));
        }

        if (m_config.default_jerk.value > 0 && m_config.initial_layer_jerk.value > 0) {
            double jerk = m_config.default_jerk.value;
            gcode += m_writer.set_jerk_xy(jerk);
        }

        // Transition from 1st to 2nd layer. Adjust nozzle temperatures as prescribed by the nozzle dependent
        // nozzle_temperature_initial_layer vs. temperature settings.
        for (const Extruder &extruder : m_writer.extruders()) {
            if (print.config().single_extruder_multi_material.value && extruder.id() != m_writer.extruder()->id())
                // In single extruder multi material mode, set the temperature for the current extruder only.
                continue;
            int temperature = print.config().nozzle_temperature.get_at(extruder.id());
            if (temperature > 0 && temperature != print.config().nozzle_temperature_initial_layer.get_at(extruder.id()))
                gcode += m_writer.set_temperature(temperature, false, extruder.id());
        }

        // BBS
        int bed_temp = get_bed_temperature(first_extruder_id, false, print.config().curr_bed_type);
        gcode += m_writer.set_bed_temperature(bed_temp);
        // Mark the temperature transition from 1st to 2nd layer to be finished.
        m_second_layer_things_done = true;
    }

    // Map from extruder ID to <begin, end> index of skirt loops to be extruded with that extruder.
    std::map<unsigned int, std::pair<size_t, size_t>> skirt_loops_per_extruder;

    if (single_object_instance_idx == size_t(-1)) {
        // Normal (non-sequential) print.
        gcode += ProcessLayer::emit_custom_gcode_per_print_z(*this, layer_tools.custom_gcode, m_writer.extruder()->id(), first_extruder_id, print.config());
    }
    // Extrude skirt at the print_z of the raft layers and normal object layers
    // not at the print_z of the interlaced support material layers.
    skirt_loops_per_extruder = first_layer ?
        Skirt::make_skirt_loops_per_extruder_1st_layer(print, layer_tools, m_skirt_done) :
        Skirt::make_skirt_loops_per_extruder_other_layers(print, layer_tools, m_skirt_done);

    for (const auto& layer_to_print : layers) {
      m_extrusion_quality_estimator.prepare_for_new_layer(layer_to_print.original_object, layer_to_print.object_layer);
    }


    // Group extrusions by an extruder, then by an object, an island and a region.
    std::map<unsigned int, std::vector<ObjectByExtruder>> by_extruder;
    bool is_anything_overridden = const_cast<LayerTools&>(layer_tools).wiping_extrusions().is_anything_overridden();
    for (const LayerToPrint &layer_to_print : layers) {
        if (layer_to_print.support_layer != nullptr) {
            const SupportLayer &support_layer = *layer_to_print.support_layer;
            const PrintObject& object = *layer_to_print.original_object;
            if (! support_layer.support_fills.entities.empty()) {
                ExtrusionRole   role               = support_layer.support_fills.role();
                bool            has_support        = role == erMixed || role == erSupportMaterial || role == erSupportTransition;
                bool            has_interface      = role == erMixed || role == erSupportMaterialInterface;
                // Extruder ID of the support base. -1 if "don't care".
                unsigned int    support_extruder   = object.config().support_filament.value - 1;
                // Shall the support be printed with the active extruder, preferably with non-soluble, to avoid tool changes?
                bool            support_dontcare   = object.config().support_filament.value == 0;
                // Extruder ID of the support interface. -1 if "don't care".
                unsigned int    interface_extruder = object.config().support_interface_filament.value - 1;
                // Shall the support interface be printed with the active extruder, preferably with non-soluble, to avoid tool changes?
                bool            interface_dontcare = object.config().support_interface_filament.value == 0;

                // BBS: apply wiping overridden extruders
                WipingExtrusions& wiping_extrusions = const_cast<LayerTools&>(layer_tools).wiping_extrusions();
                if (support_dontcare) {
                    int extruder_override = wiping_extrusions.get_support_extruder_overrides(&object);
                    if (extruder_override >= 0) {
                        support_extruder = extruder_override;
                        support_dontcare = false;
                    }
                }

                if (interface_dontcare) {
                    int extruder_override = wiping_extrusions.get_support_interface_extruder_overrides(&object);
                    if (extruder_override >= 0) {
                        interface_extruder = extruder_override;
                        interface_dontcare = false;
                    }
                }

                // BBS: try to print support base with a filament other than interface filament
                if (support_dontcare && !interface_dontcare) {
                    unsigned int dontcare_extruder = first_extruder_id;
                    for (unsigned int extruder_id : layer_tools.extruders) {
                        if (print.config().filament_soluble.get_at(extruder_id))
                            continue;

                        if (extruder_id == interface_extruder)
                            continue;

                        dontcare_extruder = extruder_id;
                        break;
                    }

                    if (support_dontcare)
                        support_extruder = dontcare_extruder;
                }
                else if (support_dontcare || interface_dontcare) {
                    // Some support will be printed with "don't care" material, preferably non-soluble.
                    // Is the current extruder assigned a soluble filament?
                    unsigned int dontcare_extruder = first_extruder_id;
                    if (print.config().filament_soluble.get_at(dontcare_extruder)) {
                        // The last extruder printed on the previous layer extrudes soluble filament.
                        // Try to find a non-soluble extruder on the same layer.
                        for (unsigned int extruder_id : layer_tools.extruders)
                            if (! print.config().filament_soluble.get_at(extruder_id)) {
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
            size_t n_slices = layer.lslices.size();
            const std::vector<BoundingBox> &layer_surface_bboxes = layer.lslices_bboxes;
            // Traverse the slices in an increasing order of bounding box size, so that the islands inside another islands are tested first,
            // so we can just test a point inside ExPolygon::contour and we may skip testing the holes.
            std::vector<size_t> slices_test_order;
            slices_test_order.reserve(n_slices);
            for (size_t i = 0; i < n_slices; ++ i)
                slices_test_order.emplace_back(i);
            std::sort(slices_test_order.begin(), slices_test_order.end(), [&layer_surface_bboxes](size_t i, size_t j) {
                const Vec2d s1 = layer_surface_bboxes[i].size().cast<double>();
                const Vec2d s2 = layer_surface_bboxes[j].size().cast<double>();
                return s1.x() * s1.y() < s2.x() * s2.y();
            });
            auto point_inside_surface = [&layer, &layer_surface_bboxes](const size_t i, const Point &point) {
                const BoundingBox &bbox = layer_surface_bboxes[i];
                return point(0) >= bbox.min(0) && point(0) < bbox.max(0) &&
                       point(1) >= bbox.min(1) && point(1) < bbox.max(1) &&
                       layer.lslices[i].contour.contains(point);
            };

            for (size_t region_id = 0; region_id < layer.regions().size(); ++ region_id) {
                const LayerRegion *layerm = layer.regions()[region_id];
                if (layerm == nullptr)
                    continue;
                // PrintObjects own the PrintRegions, thus the pointer to PrintRegion would be unique to a PrintObject, they would not
                // identify the content of PrintRegion accross the whole print uniquely. Translate to a Print specific PrintRegion.
                const PrintRegion &region = print.get_print_region(layerm->region().print_region_id());

                // Now we must process perimeters and infills and create islands of extrusions in by_region std::map.
                // It is also necessary to save which extrusions are part of MM wiping and which are not.
                // The process is almost the same for perimeters and infills - we will do it in a cycle that repeats twice:
                std::vector<unsigned int> printing_extruders;
                for (const ObjectByExtruder::Island::Region::Type entity_type : { ObjectByExtruder::Island::Region::INFILL, ObjectByExtruder::Island::Region::PERIMETERS }) {
                    for (const ExtrusionEntity *ee : (entity_type == ObjectByExtruder::Island::Region::INFILL) ? layerm->fills.entities : layerm->perimeters.entities) {
                        // extrusions represents infill or perimeter extrusions of a single island.
                        assert(dynamic_cast<const ExtrusionEntityCollection*>(ee) != nullptr);
                        const auto *extrusions = static_cast<const ExtrusionEntityCollection*>(ee);
                        if (extrusions->entities.empty()) // This shouldn't happen but first_point() would fail.
                            continue;

                        // This extrusion is part of certain Region, which tells us which extruder should be used for it:
                        int correct_extruder_id = layer_tools.extruder(*extrusions, region);

                        // Let's recover vector of extruder overrides:
                        const WipingExtrusions::ExtruderPerCopy *entity_overrides = nullptr;
                        if (! layer_tools.has_extruder(correct_extruder_id)) {
                            // this entity is not overridden, but its extruder is not in layer_tools - we'll print it
                            // by last extruder on this layer (could happen e.g. when a wiping object is taller than others - dontcare extruders are eradicated from layer_tools)
                            correct_extruder_id = layer_tools.extruders.back();
                        }
                        printing_extruders.clear();
                        if (is_anything_overridden) {
                            entity_overrides = const_cast<LayerTools&>(layer_tools).wiping_extrusions().get_extruder_overrides(extrusions, layer_to_print.original_object, correct_extruder_id, layer_to_print.object()->instances().size());
                            if (entity_overrides == nullptr) {
                                printing_extruders.emplace_back(correct_extruder_id);
                            } else {
                                printing_extruders.reserve(entity_overrides->size());
                                for (int extruder : *entity_overrides)
                                    printing_extruders.emplace_back(extruder >= 0 ?
                                        // at least one copy is overridden to use this extruder
                                        extruder :
                                        // at least one copy would normally be printed with this extruder (see get_extruder_overrides function for explanation)
                                        static_cast<unsigned int>(- extruder - 1));
                                Slic3r::sort_remove_duplicates(printing_extruders);
                            }
                        } else
                            printing_extruders.emplace_back(correct_extruder_id);

                        // Now we must add this extrusion into the by_extruder map, once for each extruder that will print it:
                        for (unsigned int extruder : printing_extruders)
                        {
                            std::vector<ObjectByExtruder::Island> &islands = object_islands_by_extruder(
                                by_extruder,
                                extruder,
                                &layer_to_print - layers.data(),
                                layers.size(), n_slices+1);
                            for (size_t i = 0; i <= n_slices; ++ i) {
                                bool   last = i == n_slices;
                                size_t island_idx = last ? n_slices : slices_test_order[i];
                                if (// extrusions->first_point does not fit inside any slice
                                    last ||
                                    // extrusions->first_point fits inside ith slice
                                    point_inside_surface(island_idx, extrusions->first_point())) {
                                    if (islands[island_idx].by_region.empty())
                                        islands[island_idx].by_region.assign(print.num_print_regions(), ObjectByExtruder::Island::Region());
                                    islands[island_idx].by_region[region.print_region_id()].append(entity_type, extrusions, entity_overrides);
                                    break;
                                }
                            }
                        }
                    }
                }
            } // for regions
        }
    } // for objects

    if (m_wipe_tower)
        m_wipe_tower->set_is_first_print(true);

    // Extrude the skirt, brim, support, perimeters, infill ordered by the extruders.
    for (unsigned int extruder_id : layer_tools.extruders)
    {
        gcode += (layer_tools.has_wipe_tower && m_wipe_tower) ?
            m_wipe_tower->tool_change(*this, extruder_id, extruder_id == layer_tools.extruders.back()) :
            this->set_extruder(extruder_id, print_z);

        // let analyzer tag generator aware of a role type change
        if (layer_tools.has_wipe_tower && m_wipe_tower)
            m_last_processor_extrusion_role = erWipeTower;

        if (auto loops_it = skirt_loops_per_extruder.find(extruder_id); loops_it != skirt_loops_per_extruder.end()) {
            const std::pair<size_t, size_t> loops = loops_it->second;
            this->set_origin(0., 0.);
            m_avoid_crossing_perimeters.use_external_mp();
            Flow layer_skirt_flow = print.skirt_flow().with_height(float(m_skirt_done.back() - (m_skirt_done.size() == 1 ? 0. : m_skirt_done[m_skirt_done.size() - 2])));
            double mm3_per_mm = layer_skirt_flow.mm3_per_mm();
            for (size_t i = loops.first; i < loops.second; ++i) {
                // Adjust flow according to this layer's layer height.
                ExtrusionLoop loop = *dynamic_cast<const ExtrusionLoop*>(print.skirt().entities[i]);
                for (ExtrusionPath &path : loop.paths) {
                    path.height = layer_skirt_flow.height();
                    path.mm3_per_mm = mm3_per_mm;
                }
                //FIXME using the support_speed of the 1st object printed.
                gcode += this->extrude_loop(loop, "skirt", m_config.support_speed.value);
            }
            m_avoid_crossing_perimeters.use_external_mp(false);
            // Allow a straight travel move to the first object point if this is the first layer (but don't in next layers).
            if (first_layer && loops.first == 0)
                m_avoid_crossing_perimeters.disable_once();
        }

        auto objects_by_extruder_it = by_extruder.find(extruder_id);
        if (objects_by_extruder_it == by_extruder.end())
            continue;

        // BBS: ordering instances by extruder
        std::vector<InstanceToPrint> instances_to_print;
        bool has_prime_tower = print.config().enable_prime_tower
            && print.extruders().size() > 1
            && (print.config().print_sequence == PrintSequence::ByLayer
                || (print.config().print_sequence == PrintSequence::ByObject && print.objects().size() == 1));
        if (has_prime_tower) {
            int plate_idx = print.get_plate_index();
            Point wt_pos(print.config().wipe_tower_x.get_at(plate_idx), print.config().wipe_tower_y.get_at(plate_idx));

            std::vector<GCode::ObjectByExtruder>& objects_by_extruder = objects_by_extruder_it->second;
            std::vector<const PrintObject*> print_objects;
            for (int obj_idx = 0; obj_idx < objects_by_extruder.size(); obj_idx++) {
                auto& object_by_extruder = objects_by_extruder[obj_idx];
                if (object_by_extruder.islands.empty() && (object_by_extruder.support == nullptr || object_by_extruder.support->empty()))
                    continue;

                print_objects.push_back(print.get_object(obj_idx));
            }

            std::vector<const PrintInstance*> new_ordering = chain_print_object_instances(print_objects, &wt_pos);
            std::reverse(new_ordering.begin(), new_ordering.end());
            instances_to_print = sort_print_object_instances(objects_by_extruder_it->second, layers, &new_ordering, single_object_instance_idx);
        }
        else {
            instances_to_print = sort_print_object_instances(objects_by_extruder_it->second, layers, ordering, single_object_instance_idx);
        }

        // BBS
        if (print.config().print_sequence == PrintSequence::ByObject && prime_extruder && first_layer && extruder_id == first_extruder_id) {
            for (InstanceToPrint& instance_to_print : instances_to_print) {
                if (this->m_objSupportsWithBrim.find(instance_to_print.print_object.id()) != this->m_objSupportsWithBrim.end() &&
                    print.m_supportBrimMap.at(instance_to_print.print_object.id()).entities.size() > 0)
                    continue;

                if (this->m_objsWithBrim.find(instance_to_print.print_object.id()) != this->m_objsWithBrim.end() &&
                    print.m_brimMap.at(instance_to_print.print_object.id()).entities.size() > 0)
                    continue;

                const Point& offset = instance_to_print.print_object.instances()[instance_to_print.instance_id].shift;
                set_origin(unscaled(offset));
                for (ExtrusionEntity* ee : layer.object()->object_skirt().entities)
                    //FIXME using the support_speed of the 1st object printed.
                    gcode += this->extrude_entity(*ee, "skirt", m_config.support_speed.value);
            }
        }

        // We are almost ready to print. However, we must go through all the objects twice to print the the overridden extrusions first (infill/perimeter wiping feature):
        std::vector<ObjectByExtruder::Island::Region> by_region_per_copy_cache;
        for (int print_wipe_extrusions = is_anything_overridden; print_wipe_extrusions>=0; --print_wipe_extrusions) {
            if (is_anything_overridden && print_wipe_extrusions == 0)
                gcode+="; PURGING FINISHED\n";
            for (InstanceToPrint &instance_to_print : instances_to_print) {
                const LayerToPrint &layer_to_print = layers[instance_to_print.layer_id];
                // To control print speed of the 1st object layer printed over raft interface.
                bool object_layer_over_raft = layer_to_print.object_layer && layer_to_print.object_layer->id() > 0 &&
                    instance_to_print.print_object.slicing_parameters().raft_layers() == layer_to_print.object_layer->id();
                m_config.apply(instance_to_print.print_object.config(), true);
                m_layer = layer_to_print.layer();
                m_object_layer_over_raft = object_layer_over_raft;
                if (m_config.reduce_crossing_wall)
                    m_avoid_crossing_perimeters.init_layer(*m_layer);
                if (this->config().gcode_label_objects) {
                    gcode += std::string("; printing object ") + instance_to_print.print_object.model_object()->name + " id:" + std::to_string(instance_to_print.layer_id) + " copy " + std::to_string(instance_to_print.instance_id) + "\n";
                    if (!m_config.use_relative_e_distances)
                        gcode += m_writer.reset_e(true);
                }
                m_extrusion_quality_estimator.set_current_object(&instance_to_print.print_object);

                // When starting a new object, use the external motion planner for the first travel move.
                const Point &offset = instance_to_print.print_object.instances()[instance_to_print.instance_id].shift;
                std::pair<const PrintObject*, Point> this_object_copy(&instance_to_print.print_object, offset);
                if (m_last_obj_copy != this_object_copy)
                    m_avoid_crossing_perimeters.use_external_mp_once();
                m_last_obj_copy = this_object_copy;
                this->set_origin(unscale(offset));
                if (instance_to_print.object_by_extruder.support != nullptr) {
                    m_layer = layers[instance_to_print.layer_id].support_layer;
                    m_object_layer_over_raft = false;

                    //BBS: print supports' brims first
                    if (this->m_objSupportsWithBrim.find(instance_to_print.print_object.id()) != this->m_objSupportsWithBrim.end() && !print_wipe_extrusions) {
                        this->set_origin(0., 0.);
                        m_avoid_crossing_perimeters.use_external_mp();
                        for (const ExtrusionEntity* ee : print.m_supportBrimMap.at(instance_to_print.print_object.id()).entities) {
                            gcode += this->extrude_entity(*ee, "brim", m_config.support_speed.value);
                        }
                        m_avoid_crossing_perimeters.use_external_mp(false);
                        // Allow a straight travel move to the first object point.
                        m_avoid_crossing_perimeters.disable_once();
                        this->m_objSupportsWithBrim.erase(instance_to_print.print_object.id());
                    }
                    // When starting a new object, use the external motion planner for the first travel move.
                    const Point& offset = instance_to_print.print_object.instances()[instance_to_print.instance_id].shift;
                    std::pair<const PrintObject*, Point> this_object_copy(&instance_to_print.print_object, offset);
                    if (m_last_obj_copy != this_object_copy)
                        m_avoid_crossing_perimeters.use_external_mp_once();
                    m_last_obj_copy = this_object_copy;
                    this->set_origin(unscale(offset));
                    ExtrusionEntityCollection support_eec;

                    // BBS
                    WipingExtrusions& wiping_extrusions = const_cast<LayerTools&>(layer_tools).wiping_extrusions();
                    bool support_overridden = wiping_extrusions.is_support_overridden(layer_to_print.original_object);
                    bool support_intf_overridden = wiping_extrusions.is_support_interface_overridden(layer_to_print.original_object);

                    ExtrusionRole support_extrusion_role = instance_to_print.object_by_extruder.support_extrusion_role;
                    bool is_overridden = support_extrusion_role == erSupportMaterialInterface ? support_intf_overridden : support_overridden;
                    if (is_overridden == (print_wipe_extrusions != 0))
                        gcode += this->extrude_support(
                            // support_extrusion_role is erSupportMaterial, erSupportTransition, erSupportMaterialInterface or erMixed for all extrusion paths.
                            instance_to_print.object_by_extruder.support->chained_path_from(m_last_pos, support_extrusion_role));

                    m_layer = layer_to_print.layer();
                    m_object_layer_over_raft = object_layer_over_raft;
                }
                //FIXME order islands?
                // Sequential tool path ordering of multiple parts within the same object, aka. perimeter tracking (#5511)
                for (ObjectByExtruder::Island &island : instance_to_print.object_by_extruder.islands) {
                    const auto& by_region_specific = is_anything_overridden ? island.by_region_per_copy(by_region_per_copy_cache, static_cast<unsigned int>(instance_to_print.instance_id), extruder_id, print_wipe_extrusions != 0) : island.by_region;
                    //BBS: add brim by obj by extruder
                    if (this->m_objsWithBrim.find(instance_to_print.print_object.id()) != this->m_objsWithBrim.end() && !print_wipe_extrusions) {
                        this->set_origin(0., 0.);
                        m_avoid_crossing_perimeters.use_external_mp();
                        for (const ExtrusionEntity* ee : print.m_brimMap.at(instance_to_print.print_object.id()).entities) {
                            gcode += this->extrude_entity(*ee, "brim", m_config.support_speed.value);
                        }
                        m_avoid_crossing_perimeters.use_external_mp(false);
                        // Allow a straight travel move to the first object point.
                        m_avoid_crossing_perimeters.disable_once();
                        this->m_objsWithBrim.erase(instance_to_print.print_object.id());
                    }
                    // When starting a new object, use the external motion planner for the first travel move.
                    const Point& offset = instance_to_print.print_object.instances()[instance_to_print.instance_id].shift;
                    std::pair<const PrintObject*, Point> this_object_copy(&instance_to_print.print_object, offset);
                    if (m_last_obj_copy != this_object_copy)
                        m_avoid_crossing_perimeters.use_external_mp_once();
                    m_last_obj_copy = this_object_copy;
                    this->set_origin(unscale(offset));
                    //FIXME the following code prints regions in the order they are defined, the path is not optimized in any way.
                    bool is_infill_first = print.default_region_config().wall_infill_order == WallInfillOrder::InfillInnerOuter ||
                                           print.default_region_config().wall_infill_order == WallInfillOrder::InfillOuterInner;
                    //BBS: for first layer, we always print wall firstly to get better bed adhesive force
                    //This behaviour is same with cura
                    if (is_infill_first && !first_layer) {
                        gcode += this->extrude_infill(print, by_region_specific, false);
                        gcode += this->extrude_perimeters(print, by_region_specific);
                    } else {
                        gcode += this->extrude_perimeters(print, by_region_specific);
                        gcode += this->extrude_infill(print,by_region_specific, false);
                    }
                    // ironing
                    gcode += this->extrude_infill(print,by_region_specific, true);
                }
                if (this->config().gcode_label_objects) {
                    gcode += std::string("; stop printing object ") + instance_to_print.print_object.model_object()->name + " id:" + std::to_string(instance_to_print.layer_id) + " copy " + std::to_string(instance_to_print.instance_id) + "\n";
                    if (!m_config.use_relative_e_distances)
                        gcode += m_writer.reset_e(true);
                }
            }
        }
    }

#if 0
    // Apply spiral vase post-processing if this layer contains suitable geometry
    // (we must feed all the G-code into the post-processor, including the first
    // bottom non-spiral layers otherwise it will mess with positions)
    // we apply spiral vase at this stage because it requires a full layer.
    // Just a reminder: A spiral vase mode is allowed for a single object per layer, single material print only.
    if (m_spiral_vase)
        gcode = m_spiral_vase->process_layer(std::move(gcode));

    // Apply cooling logic; this may alter speeds.
    if (m_cooling_buffer)
        gcode = m_cooling_buffer->process_layer(std::move(gcode), layer.id(),
            // Flush the cooling buffer at each object layer or possibly at the last layer, even if it contains just supports (This should not happen).
            object_layer || last_layer);

#ifdef HAS_PRESSURE_EQUALIZER
    // Apply pressure equalization if enabled;
    // printf("G-code before filter:\n%s\n", gcode.c_str());
    if (m_pressure_equalizer)
        gcode = m_pressure_equalizer->process(gcode.c_str(), false);
    // printf("G-code after filter:\n%s\n", out.c_str());
#endif /* HAS_PRESSURE_EQUALIZER */

    file.write(gcode);
#endif

    BOOST_LOG_TRIVIAL(trace) << "Exported layer " << layer.id() << " print_z " << print_z <<
    log_memory_info();

    result.gcode = std::move(gcode);
    result.cooling_buffer_flush = object_layer || raft_layer || last_layer;
    return result;
}

void GCode::apply_print_config(const PrintConfig &print_config)
{
    m_writer.apply_print_config(print_config);
    m_config.apply(print_config);
    m_scaled_resolution = scaled<double>(print_config.resolution.value);
}

void GCode::append_full_config(const Print &print, std::string &str)
{
    const DynamicPrintConfig &cfg = print.full_print_config();
    // Sorted list of config keys, which shall not be stored into the G-code. Initializer list.
    static constexpr auto banned_keys = {
        "compatible_printers"sv,
        "compatible_prints"sv
    };
    assert(std::is_sorted(banned_keys.begin(), banned_keys.end()));
    auto is_banned = [](const std::string &key) {
        return std::binary_search(banned_keys.begin(), banned_keys.end(), key);
    };
    std::ostringstream ss;
    for (const std::string& key : cfg.keys()) {
        if (!is_banned(key) && !cfg.option(key)->is_nil()) {
            if (key == "wipe_tower_x" || key == "wipe_tower_y") {
                ss << std::fixed << std::setprecision(3) << "; " << key << " = " << dynamic_cast<const ConfigOptionFloats*>(cfg.option(key))->get_at(print.get_plate_index()) << "\n";
            }
            else
                ss << "; " << key << " = " << cfg.opt_serialize(key) << "\n";
        }
    }
    str += ss.str();
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

void GCode::set_origin(const Vec2d &pointf)
{
    // if origin increases (goes towards right), last_pos decreases because it goes towards left
    const Point translate(
        scale_(m_origin(0) - pointf(0)),
        scale_(m_origin(1) - pointf(1))
    );
    m_last_pos += translate;
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
    //m_writer.travel_to_z(m_config.z_offset.value);
    m_writer.travel_to_z(0.0);

    return gcode;
}

// called by GCode::process_layer()
std::string GCode::change_layer(coordf_t print_z, bool lazy_raise)
{
    std::string gcode;
    if (m_layer_count > 0)
        // Increment a progress bar indicator.
        gcode += m_writer.update_progress(++ m_layer_index, m_layer_count);
    //BBS
    //coordf_t z = print_z + m_config.z_offset.value;  // in unscaled coordinates
    coordf_t z = print_z;  // in unscaled coordinates
    if (EXTRUDER_CONFIG(retract_when_changing_layer) && m_writer.will_move_z(z)) {
        LiftType lift_type = this->to_lift_type(ZHopType(EXTRUDER_CONFIG(z_hop_types)));
        //BBS: force to use SpiralLift when change layer if lift type is auto
        gcode += this->retract(false, false, ZHopType(EXTRUDER_CONFIG(z_hop_types)) == ZHopType::zhtAuto ? LiftType::SpiralLift : lift_type);
    }

    if (!lazy_raise) {
        std::ostringstream comment;
        comment << "move to next layer (" << m_layer_index << ")";
        gcode += m_writer.travel_to_z(z, comment.str());
    } else {
        //BBS: set m_need_change_layer_lift_z to be true so that z lift can be done in travel_to() function
        m_need_change_layer_lift_z = true;
    }

    // BBS
    m_nominal_z = print_z;

    // forget last wiping path as wiping after raising Z is pointless
    // BBS. Dont forget wiping path to reduce stringing.
    //m_wipe.reset_path();

    return gcode;
}



static std::unique_ptr<EdgeGrid::Grid> calculate_layer_edge_grid(const Layer& layer)
{
    auto out = make_unique<EdgeGrid::Grid>();

    // Create the distance field for a layer below.
    const coord_t distance_field_resolution = coord_t(scale_(1.) + 0.5);
    out->create(layer.lslices, distance_field_resolution);
    out->calculate_sdf();
#if 0
        {
            static int iRun = 0;
            BoundingBox bbox = (*lower_layer_edge_grid)->bbox();
            bbox.min(0) -= scale_(5.f);
            bbox.min(1) -= scale_(5.f);
            bbox.max(0) += scale_(5.f);
            bbox.max(1) += scale_(5.f);
            EdgeGrid::save_png(*(*lower_layer_edge_grid), bbox, scale_(0.1f), debug_out_path("GCode_extrude_loop_edge_grid-%d.png", iRun++));
        }
#endif
    return out;
}


std::string GCode::extrude_loop(ExtrusionLoop loop, std::string description, double speed)
{
    // get a copy; don't modify the orientation of the original loop object otherwise
    // next copies (if any) would not detect the correct orientation

    // extrude all loops ccw
    bool was_clockwise = loop.make_counter_clockwise();

    // find the point of the loop that is closest to the current extruder position
    // or randomize if requested
    Point last_pos = this->last_pos();
    if (!m_config.spiral_mode && description == "perimeter") {
        assert(m_layer != nullptr);
        bool is_outer_wall_first = m_config.wall_infill_order == WallInfillOrder::OuterInnerInfill
                                || m_config.wall_infill_order == WallInfillOrder::InfillOuterInner
                                || m_config.wall_infill_order == WallInfillOrder::InnerOuterInnerInfill;
        m_seam_placer.place_seam(m_layer, loop, is_outer_wall_first, this->last_pos());
    } else
        loop.split_at(last_pos, false);

    // clip the path to avoid the extruder to get exactly on the first point of the loop;
    // if polyline was shorter than the clipping distance we'd get a null polyline, so
    // we discard it in that case
    double clip_length = m_enable_loop_clipping ?
    scale_(m_config.seam_gap.get_abs_value(EXTRUDER_CONFIG(nozzle_diameter))) : 0;

    // get paths
    ExtrusionPaths paths;
    loop.clip_end(clip_length, &paths);
    if (paths.empty()) return "";

    // SoftFever: check loop lenght for small perimeter. 
    double small_peri_speed = -1;
    if (speed == -1 && loop.length() <= SMALL_PERIMETER_LENGTH(m_config.small_perimeter_threshold.value))
        small_peri_speed = m_config.small_perimeter_speed.get_abs_value(m_config.outer_wall_speed);

    // extrude along the path
    std::string gcode;
    bool is_small_peri = false;
    for (ExtrusionPaths::iterator path = paths.begin(); path != paths.end(); ++path) {
//    description += ExtrusionLoop::role_to_string(loop.loop_role());
//    description += ExtrusionEntity::role_to_string(path->role);
        // don't apply small perimeter setting for overhangs/bridges/non-perimeters
        is_small_peri = is_perimeter(path->role()) && !is_bridge(path->role()) && small_peri_speed > 0 && (path->get_overhang_degree() == 0 || path->get_overhang_degree() > 5);
        gcode += this->_extrude(*path, description, is_small_peri ? small_peri_speed : speed);
    }

    // BBS
    if (m_wipe.enable) {
        m_wipe.path = Polyline();
        for (ExtrusionPath &path : paths) {
            //BBS: Don't need to save duplicated point into wipe path
            if (!m_wipe.path.empty() && !path.empty() &&
                m_wipe.path.last_point() == path.first_point())
                m_wipe.path.append(path.polyline.points.begin() + 1, path.polyline.points.end());
            else
                m_wipe.path.append(path.polyline);  // TODO: don't limit wipe to last path
        }
    }

    // make a little move inwards before leaving loop
    if (m_config.wipe_on_loops.value && paths.back().role() == erExternalPerimeter && m_layer != NULL && m_config.wall_loops.value > 1 && paths.front().size() >= 2 && paths.back().polyline.points.size() >= 3) {
        // detect angle between last and first segment
        // the side depends on the original winding order of the polygon (left for contours, right for holes)
        //FIXME improve the algorithm in case the loop is tiny.
        //FIXME improve the algorithm in case the loop is split into segments with a low number of points (see the Point b query).
        Point a = paths.front().polyline.points[1];  // second point
        Point b = *(paths.back().polyline.points.end()-3);       // second to last point
        if (was_clockwise) {
            // swap points
            Point c = a; a = b; b = c;

    //    double angle = paths.front().first_point().ccw_angle(a, b) / 3;

    //    // turn left if contour, turn right if hole
    //    if (was_clockwise) angle *= -1;

        }

        double angle = paths.front().first_point().ccw_angle(a, b) / 3;

        // turn left if contour, turn right if hole
        if (was_clockwise) angle *= -1;

        // create the destination point along the first segment and rotate it
        // we make sure we don't exceed the segment length because we don't know
        // the rotation of the second segment so we might cross the object boundary
        Vec2d  p1 = paths.front().polyline.points.front().cast<double>();
        Vec2d  p2 = paths.front().polyline.points[1].cast<double>();
        Vec2d  v  = p2 - p1;
        double nd = scale_(EXTRUDER_CONFIG(nozzle_diameter));
        double l2 = v.squaredNorm();
        // Shift by no more than a nozzle diameter.
        //FIXME Hiding the seams will not work nicely for very densely discretized contours!
        //BBS. shorten the travel distant before the wipe path
        double threshold = 0.2;
        Point  pt = (p1 + v * threshold).cast<coord_t>();
        if (nd * nd < l2)
            pt = (p1 + threshold * v * (nd / sqrt(l2))).cast<coord_t>();
        //Point pt = ((nd * nd >= l2) ? (p1+v*0.4): (p1 + 0.2 * v * (nd / sqrt(l2)))).cast<coord_t>();
        pt.rotate(angle, paths.front().polyline.points.front());
        // generate the travel move
        gcode += m_writer.extrude_to_xy(this->point_to_gcode(pt), 0,"move inwards before travel",true);
    }

    //BBS: don't reset acceleration when printing first layer. During first layer, acceleration is always same value.
    if (!this->on_first_layer()) {
        // reset acceleration
        if (m_config.default_acceleration.value > 0)
            gcode += m_writer.set_acceleration((unsigned int)(m_config.default_acceleration.value + 0.5));
        if (m_config.default_jerk.value > 0)
            gcode += m_writer.set_jerk_xy(m_config.default_jerk.value);
    }
    return gcode;
}

std::string GCode::extrude_multi_path(ExtrusionMultiPath multipath, std::string description, double speed)
{
    // extrude along the path
    std::string gcode;
    for (ExtrusionPath path : multipath.paths)
        gcode += this->_extrude(path, description, speed);

    // BBS
    if (m_wipe.enable) {
        m_wipe.path = Polyline();
        for (ExtrusionPath &path : multipath.paths) {
            //BBS: Don't need to save duplicated point into wipe path
            if (!m_wipe.path.empty() && !path.empty() &&
                m_wipe.path.last_point() == path.first_point())
                m_wipe.path.append(path.polyline.points.begin() + 1, path.polyline.points.end());
            else
                m_wipe.path.append(path.polyline); // TODO: don't limit wipe to last path
        }
        m_wipe.path.reverse();
    }
    //BBS: don't reset acceleration when printing first layer. During first layer, acceleration is always same value.
    if (!this->on_first_layer()) {
        // reset acceleration
        gcode += m_writer.set_acceleration((unsigned int)floor(m_config.default_acceleration.value + 0.5));
        if(m_config.default_jerk.value > 0)
            gcode += m_writer.set_jerk_xy(m_config.default_jerk.value);
        }
    return gcode;
}

std::string GCode::extrude_entity(const ExtrusionEntity &entity, std::string description, double speed)
{
    if (const ExtrusionPath* path = dynamic_cast<const ExtrusionPath*>(&entity))
        return this->extrude_path(*path, description, speed);
    else if (const ExtrusionMultiPath* multipath = dynamic_cast<const ExtrusionMultiPath*>(&entity))
        return this->extrude_multi_path(*multipath, description, speed);
    else if (const ExtrusionLoop* loop = dynamic_cast<const ExtrusionLoop*>(&entity))
        return this->extrude_loop(*loop, description, speed);
    else
        throw Slic3r::InvalidArgument("Invalid argument supplied to extrude()");
    return "";
}

std::string GCode::extrude_path(ExtrusionPath path, std::string description, double speed)
{
//    description += ExtrusionEntity::role_to_string(path.role());
    std::string gcode = this->_extrude(path, description, speed);
    if (m_wipe.enable) {
        m_wipe.path = std::move(path.polyline);
        m_wipe.path.reverse();
    }
    //BBS: don't reset acceleration when printing first layer. During first layer, acceleration is always same value.
    if (!this->on_first_layer()){
        // reset acceleration
        gcode += m_writer.set_acceleration((unsigned int)floor(m_config.default_acceleration.value + 0.5));
        if(m_config.default_jerk.value > 0)
            gcode += m_writer.set_jerk_xy(m_config.default_jerk.value);

        }
    return gcode;
}

// Extrude perimeters: Decide where to put seams (hide or align seams).
std::string GCode::extrude_perimeters(const Print &print, const std::vector<ObjectByExtruder::Island::Region> &by_region)
{
    std::string gcode;
    for (const ObjectByExtruder::Island::Region &region : by_region)
        if (! region.perimeters.empty()) {
            m_config.apply(print.get_print_region(&region - &by_region.front()).config());

            for (const ExtrusionEntity* ee : region.perimeters)
                gcode += this->extrude_entity(*ee, "perimeter", -1.);
        }
    return gcode;
}

// Chain the paths hierarchically by a greedy algorithm to minimize a travel distance.
std::string GCode::extrude_infill(const Print &print, const std::vector<ObjectByExtruder::Island::Region> &by_region, bool ironing)
{
    std::string 		 gcode;
    ExtrusionEntitiesPtr extrusions;
    const char*          extrusion_name = ironing ? "ironing" : "infill";
    for (const ObjectByExtruder::Island::Region &region : by_region)
        if (! region.infills.empty()) {
            extrusions.clear();
            extrusions.reserve(region.infills.size());
            for (ExtrusionEntity *ee : region.infills)
                if ((ee->role() == erIroning) == ironing)
                    extrusions.emplace_back(ee);
            if (! extrusions.empty()) {
                m_config.apply(print.get_print_region(&region - &by_region.front()).config());
                chain_and_reorder_extrusion_entities(extrusions, &m_last_pos);
                for (const ExtrusionEntity *fill : extrusions) {
                    auto *eec = dynamic_cast<const ExtrusionEntityCollection*>(fill);
                    if (eec) {
                        for (ExtrusionEntity *ee : eec->chained_path_from(m_last_pos).entities)
                            gcode += this->extrude_entity(*ee, extrusion_name);
                    } else
                        gcode += this->extrude_entity(*fill, extrusion_name);
                }
            }
        }
    return gcode;
}

std::string GCode::extrude_support(const ExtrusionEntityCollection &support_fills)
{
    static constexpr const char *support_label            = "support material";
    static constexpr const char *support_interface_label  = "support material interface";
    const char* support_transition_label = "support transition";

    std::string gcode;
    if (! support_fills.entities.empty()) {
        const double  support_speed            = m_config.support_speed.value;
        const double  support_interface_speed  = m_config.get_abs_value("support_interface_speed");
        for (const ExtrusionEntity *ee : support_fills.entities) {
            ExtrusionRole role = ee->role();
            assert(role == erSupportMaterial || role == erSupportMaterialInterface || role == erSupportTransition);
            const char* label = (role == erSupportMaterial) ? support_label :
                ((role == erSupportMaterialInterface) ? support_interface_label : support_transition_label);
            // BBS
            //const double speed = (role == erSupportMaterial) ? support_speed : support_interface_speed;
            const double speed = -1.0;
            const ExtrusionPath* path = dynamic_cast<const ExtrusionPath*>(ee);
            const ExtrusionMultiPath* multipath = dynamic_cast<const ExtrusionMultiPath*>(ee);
            const ExtrusionLoop* loop = dynamic_cast<const ExtrusionLoop*>(ee);
            const ExtrusionEntityCollection* collection = dynamic_cast<const ExtrusionEntityCollection*>(ee);
            if (path)
                gcode += this->extrude_path(*path, label, speed);
            else if (multipath) {
                gcode += this->extrude_multi_path(*multipath, label, speed);
            }
            else if (loop) {
                gcode += this->extrude_loop(*loop, label, speed);
            }
            else if (collection) {
                gcode += extrude_support(*collection);
            }
            else {
                throw Slic3r::InvalidArgument("Unknown extrusion type");
            }
        }
    }
    return gcode;
}

bool GCode::GCodeOutputStream::is_error() const
{
    return ::ferror(this->f);
}

void GCode::GCodeOutputStream::flush()
{
    ::fflush(this->f);
}

void GCode::GCodeOutputStream::close()
{
    if (this->f) {
        ::fclose(this->f);
        this->f = nullptr;
    }
}

void GCode::GCodeOutputStream::write(const char *what)
{
    if (what != nullptr) {
        const char* gcode = what;
        // writes string to file
        fwrite(gcode, 1, ::strlen(gcode), this->f);
        //FIXME don't allocate a string, maybe process a batch of lines?
        m_processor.process_buffer(std::string(gcode));
    }
}

void GCode::GCodeOutputStream::writeln(const std::string &what)
{
    if (! what.empty())
        this->write(what.back() == '\n' ? what : what + '\n');
}

void GCode::GCodeOutputStream::write_format(const char* format, ...)
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
        this->write(bufptr);

    if (buffer_dynamic)
        free(bufptr);

    va_end(args);
}

static std::map<int, std::string> overhang_speed_key_map =
{
    {1, "overhang_1_4_speed"},
    {2, "overhang_2_4_speed"},
    {3, "overhang_3_4_speed"},
    {4, "overhang_4_4_speed"},
    {5, "bridge_speed"},
};

std::string GCode::_extrude(const ExtrusionPath &path, std::string description, double speed)
{
    std::string gcode;

    if (is_bridge(path.role()))
        description += " (bridge)";

    // go to first point of extrusion path
    //BBS: path.first_point is 2D point. But in lazy raise case, lift z is done in travel_to function.
    //Add m_need_change_layer_lift_z when change_layer in case of no lift if m_last_pos is equal to path.first_point() by chance
    if (!m_last_pos_defined || m_last_pos != path.first_point() || m_need_change_layer_lift_z) {
        gcode += this->travel_to(
            path.first_point(),
            path.role(),
            "move to first " + description + " point"
        );
        m_need_change_layer_lift_z = false;
    }

    // compensate retraction
    gcode += this->unretract();
    m_config.apply(m_calib_config);
    // adjust acceleration
    if (m_config.default_acceleration.value > 0) {
        double acceleration;
        if (this->on_first_layer() && m_config.initial_layer_acceleration.value > 0) {
            acceleration = m_config.initial_layer_acceleration.value;
#if 0
        } else if (this->object_layer_over_raft() && m_config.first_layer_acceleration_over_raft.value > 0) {
            acceleration = m_config.first_layer_acceleration_over_raft.value;
#endif
        } else if (m_config.get_abs_value("bridge_acceleration") > 0 && is_bridge(path.role())) {
            acceleration = m_config.get_abs_value("bridge_acceleration");
        } else if (m_config.get_abs_value("sparse_infill_acceleration") > 0 && (path.role() == erInternalInfill)) {
            acceleration = m_config.get_abs_value("sparse_infill_acceleration");
        } else if (m_config.get_abs_value("internal_solid_infill_acceleration") > 0 && (path.role() == erSolidInfill)) {
            acceleration = m_config.get_abs_value("internal_solid_infill_acceleration");
        } else if (m_config.outer_wall_acceleration.value > 0 && is_external_perimeter(path.role())) {
            acceleration = m_config.outer_wall_acceleration.value;
        } else if (m_config.inner_wall_acceleration.value > 0 && is_internal_perimeter(path.role())) {
            acceleration = m_config.inner_wall_acceleration.value;
        } else if (m_config.top_surface_acceleration.value > 0 && is_top_surface(path.role())) {
            acceleration = m_config.top_surface_acceleration.value;
        } else {
            acceleration = m_config.default_acceleration.value;
        }
        gcode += m_writer.set_acceleration((unsigned int)floor(acceleration + 0.5));
    }

    // adjust X Y jerk
    if (m_config.default_jerk.value > 0) {
        double jerk;
        if (this->on_first_layer() && m_config.initial_layer_jerk.value > 0) {
            jerk = m_config.initial_layer_jerk.value;
        } else if (m_config.outer_wall_jerk.value > 0 && is_external_perimeter(path.role())) {
             jerk = m_config.outer_wall_jerk.value;
        } else if (m_config.inner_wall_jerk.value > 0 && is_internal_perimeter(path.role())) {
            jerk = m_config.inner_wall_jerk.value;
        } else if (m_config.top_surface_jerk.value > 0 && is_top_surface(path.role())) {
            jerk = m_config.top_surface_jerk.value;
        } else if (m_config.infill_jerk.value > 0 && is_infill(path.role())) {
            jerk = m_config.infill_jerk.value;
        }
        else {
            jerk = m_config.default_jerk.value;
        }
        gcode += m_writer.set_jerk_xy(jerk);
    }

    // calculate extrusion length per distance unit
    const auto _mm3_per_mm = path.mm3_per_mm * this->config().print_flow_ratio;
    double e_per_mm = m_writer.extruder()->e_per_mm3() * _mm3_per_mm;

    double min_speed = double(m_config.slow_down_min_speed.get_at(m_writer.extruder()->id()));
    // set speed
    if (speed == -1) {
        int overhang_degree = path.get_overhang_degree();
        if (path.role() == erPerimeter) {
            speed = m_config.get_abs_value("inner_wall_speed");
            if (m_config.overhang_speed_classic.value && m_config.enable_overhang_speed.value && overhang_degree > 0 &&
                overhang_degree <= 5) {
                double new_speed = m_config.get_abs_value(overhang_speed_key_map[overhang_degree].c_str());
                speed = new_speed == 0.0 ? speed : new_speed;
            }
        } else if (path.role() == erExternalPerimeter) {
            speed = m_config.get_abs_value("outer_wall_speed");
            if (m_config.overhang_speed_classic.value && m_config.enable_overhang_speed.value &&
                overhang_degree > 0 && overhang_degree <= 5) {
                double new_speed = m_config.get_abs_value(overhang_speed_key_map[overhang_degree].c_str());
                speed = new_speed == 0.0 ? speed : new_speed;
            }
        } else if (path.role() == erOverhangPerimeter || path.role() == erBridgeInfill || path.role() == erSupportTransition) {
            speed = m_config.get_abs_value("bridge_speed");
        } else if (path.role() == erInternalInfill) {
            speed = m_config.get_abs_value("sparse_infill_speed");
        } else if (path.role() == erSolidInfill) {
            speed = m_config.get_abs_value("internal_solid_infill_speed");
        } else if (path.role() == erTopSolidInfill) {
            speed = m_config.get_abs_value("top_surface_speed");
        } else if (path.role() == erIroning) {
            speed = m_config.get_abs_value("ironing_speed");
        } else if (path.role() == erBottomSurface) {
            speed = m_config.get_abs_value("initial_layer_infill_speed");
        } else if (path.role() == erGapFill) {
            speed = m_config.get_abs_value("gap_infill_speed");
        }
        else if (path.role() == erSupportMaterial ||
                 path.role() == erSupportMaterialInterface) {
            const double  support_speed = m_config.support_speed.value;
            const double  support_interface_speed = m_config.get_abs_value("support_interface_speed");
            speed = (path.role() == erSupportMaterial) ? support_speed : support_interface_speed;
        } else {
            throw Slic3r::InvalidArgument("Invalid speed");
        }
    }
    //BBS: if not set the speed, then use the filament_max_volumetric_speed directly
    if (speed == 0)
        speed = EXTRUDER_CONFIG(filament_max_volumetric_speed) / _mm3_per_mm;
    if (this->on_first_layer()) {
        //BBS: for solid infill of initial layer, speed can be higher as long as
        //wall lines have be attached
        if (path.role() != erBottomSurface)
            speed = m_config.get_abs_value("initial_layer_speed");
    }
    //BBS: remove this config
    //else if (this->object_layer_over_raft())
    //    speed = m_config.get_abs_value("first_layer_speed_over_raft", speed);
    //if (m_config.max_volumetric_speed.value > 0) {
    //    // cap speed with max_volumetric_speed anyway (even if user is not using autospeed)
    //    speed = std::min(
    //        speed,
    //        m_config.max_volumetric_speed.value / _mm3_per_mm
    //    );
    //}
    if (EXTRUDER_CONFIG(filament_max_volumetric_speed) > 0) {
        // cap speed with max_volumetric_speed anyway (even if user is not using autospeed)
        speed = std::min(
            speed,
            EXTRUDER_CONFIG(filament_max_volumetric_speed) / _mm3_per_mm
        );
    }

    
    bool variable_speed = false;
    std::vector<ProcessedPoint> new_points {};

    if (m_config.enable_overhang_speed && !m_config.overhang_speed_classic && !this->on_first_layer() &&
        (is_bridge(path.role()) || is_perimeter(path.role()))) {
        ConfigOptionPercents overhang_overlap_levels({75, 50, 25, 5});
        ConfigOptionFloatsOrPercents dynamic_overhang_speeds(
            {(m_config.get_abs_value("overhang_1_4_speed") < 0.5)
                 ? FloatOrPercent{100, true}
                 : FloatOrPercent{m_config.get_abs_value("overhang_1_4_speed"), false},
             (m_config.get_abs_value("overhang_2_4_speed") < 0.5)
                 ? FloatOrPercent{100, true}
                 : FloatOrPercent{m_config.get_abs_value("overhang_2_4_speed"), false},
             (m_config.get_abs_value("overhang_3_4_speed") < 0.5)
                 ? FloatOrPercent{100, true}
                 : FloatOrPercent{m_config.get_abs_value("overhang_3_4_speed"), false},
             (m_config.get_abs_value("overhang_4_4_speed") < 0.5)
                 ? FloatOrPercent{100, true}
                 : FloatOrPercent{m_config.get_abs_value("overhang_4_4_speed"), false}});

        new_points = m_extrusion_quality_estimator.estimate_extrusion_quality(
            path, overhang_overlap_levels, dynamic_overhang_speeds, m_config.get_abs_value("outer_wall_speed"), speed);

        variable_speed = std::any_of(new_points.begin(), new_points.end(),
                                     [speed](const ProcessedPoint &p) { return p.speed != speed; });
    }

    double F = speed * 60;  // convert mm/sec to mm/min

    // extrude arc or line
    if (m_enable_extrusion_role_markers)
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
        }
    }

    // adds processor tags and updates processor tracking data
    // PrusaMultiMaterial::Writer may generate GCodeProcessor::Height_Tag lines without updating m_last_height
    // so, if the last role was erWipeTower we force export of GCodeProcessor::Height_Tag lines
    bool last_was_wipe_tower = (m_last_processor_extrusion_role == erWipeTower);
    char buf[64];
    assert(is_decimal_separator_point());

    if (path.role() != m_last_processor_extrusion_role) {
        m_last_processor_extrusion_role = path.role();
        sprintf(buf, ";%s%s\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Role).c_str(), ExtrusionEntity::role_to_string(m_last_processor_extrusion_role).c_str());
        gcode += buf;
    }

    if (last_was_wipe_tower || m_last_width != path.width) {
        m_last_width = path.width;
        sprintf(buf, ";%s%g\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Width).c_str(), m_last_width);
        gcode += buf;
    }

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    if (last_was_wipe_tower || (m_last_mm3_per_mm != path.mm3_per_mm)) {
        m_last_mm3_per_mm = path.mm3_per_mm;
        sprintf(buf, ";%s%f\n", GCodeProcessor::Mm3_Per_Mm_Tag.c_str(), m_last_mm3_per_mm);
        gcode += buf;
    }
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

    if (last_was_wipe_tower || std::abs(m_last_height - path.height) > EPSILON) {
        m_last_height = path.height;
        sprintf(buf, ";%s%g\n", GCodeProcessor::reserved_tag(GCodeProcessor::ETags::Height).c_str(), m_last_height);
        gcode += buf;
    }

    auto overhang_fan_threshold = EXTRUDER_CONFIG(overhang_fan_threshold);
    auto enable_overhang_bridge_fan = EXTRUDER_CONFIG(enable_overhang_bridge_fan);


    //    { "0%", Overhang_threshold_none },
    //    { "5%", Overhang_threshold_1_4 },
    //    { "25%", Overhang_threshold_2_4 },
    //    { "50%", Overhang_threshold_3_4 },
    //    { "75%", Overhang_threshold_4_4 },
    //    { "95%", Overhang_threshold_bridge }
    auto check_overhang_fan = [&overhang_fan_threshold](float overlap) {
      switch (overhang_fan_threshold) {
      case (int)Overhang_threshold_1_4:
        return overlap <= 0.95f;
        break;
      case (int)Overhang_threshold_2_4:
        return overlap <= 0.75f;
        break;
      case (int)Overhang_threshold_3_4:
        return overlap <= 0.5f;
        break;
      case (int)Overhang_threshold_4_4:
        return overlap <= 0.25f;
        break;
      case (int)Overhang_threshold_bridge:
        return overlap <= 0.05f;
        break;
      case (int)Overhang_threshold_none:
      default:
        return true;
      }
    };

    std::string comment;
    if (m_enable_cooling_markers) {
        comment = ";_EXTRUDE_SET_SPEED";
        if (path.role() == erExternalPerimeter)
            comment += ";_EXTERNAL_PERIMETER";
    }
    bool is_overhang_fan_on = false;
    if (!variable_speed) {
        // F is mm per minute.
        gcode += m_writer.set_speed(F, "", comment);
        double path_length = 0.;
        {
            if (m_enable_cooling_markers && enable_overhang_bridge_fan) {
                // BBS: Overhang_threshold_none means Overhang_threshold_1_4 and forcing cooling for all external
                // perimeter
                int overhang_threshold = overhang_fan_threshold == Overhang_threshold_none ? Overhang_threshold_none
                                                                                           : overhang_fan_threshold - 1;
                if ((overhang_fan_threshold == Overhang_threshold_none && is_perimeter(path.role())) ||
                    (path.get_overhang_degree() > overhang_threshold || is_bridge(path.role()))) {
                    gcode += ";_OVERHANG_FAN_START\n";
                    is_overhang_fan_on = true;
                }
            } 
            // BBS: use G1 if not enable arc fitting or has no arc fitting result or in spiral_mode mode
            // Attention: G2 and G3 is not supported in spiral_mode mode
            if (!m_config.enable_arc_fitting || path.polyline.fitting_result.empty() || m_config.spiral_mode) {
                for (const Line& line : path.polyline.lines()) {
                    const double line_length = line.length() * SCALING_FACTOR;
                    path_length += line_length;
                    gcode += m_writer.extrude_to_xy(
                        this->point_to_gcode(line.b),
                        e_per_mm * line_length,
                        GCodeWriter::full_gcode_comment ? description : "", path.is_force_no_extrusion());
                }
            } else {
                // BBS: start to generate gcode from arc fitting data which includes line and arc
                const std::vector<PathFittingData>& fitting_result = path.polyline.fitting_result;
                for (size_t fitting_index = 0; fitting_index < fitting_result.size(); fitting_index++) {
                    switch (fitting_result[fitting_index].path_type) {
                    case EMovePathType::Linear_move: {
                        size_t start_index = fitting_result[fitting_index].start_point_index;
                        size_t end_index = fitting_result[fitting_index].end_point_index;
                        for (size_t point_index = start_index + 1; point_index < end_index + 1; point_index++) {
                            const Line line = Line(path.polyline.points[point_index - 1], path.polyline.points[point_index]);
                            const double line_length = line.length() * SCALING_FACTOR;
                            path_length += line_length;
                            gcode += m_writer.extrude_to_xy(
                                this->point_to_gcode(line.b),
                                e_per_mm * line_length,
                                GCodeWriter::full_gcode_comment ? description : "", path.is_force_no_extrusion());
                        }
                        break;
                    }
                    case EMovePathType::Arc_move_cw:
                    case EMovePathType::Arc_move_ccw: {
                        const ArcSegment& arc = fitting_result[fitting_index].arc_data;
                        const double arc_length = fitting_result[fitting_index].arc_data.length * SCALING_FACTOR;
                        const Vec2d center_offset = this->point_to_gcode(arc.center) - this->point_to_gcode(arc.start_point);
                        path_length += arc_length;
                        gcode += m_writer.extrude_arc_to_xy(
                            this->point_to_gcode(arc.end_point),
                            center_offset,
                            e_per_mm * arc_length,
                            arc.direction == ArcDirection::Arc_Dir_CCW,
                            GCodeWriter::full_gcode_comment ? description : "", path.is_force_no_extrusion());
                        break;
                    }
                    default:
                        // BBS: should never happen that a empty path_type has been stored
                        assert(0);
                        break;
                    }
                }
            }
            if (is_overhang_fan_on) {
                is_overhang_fan_on = false;
                gcode += ";_OVERHANG_FAN_END\n";
            }
        }
    } else {
        double last_set_speed = std::max((float)EXTRUDER_CONFIG(slow_down_min_speed), new_points[0].speed) * 60.0;

        gcode += m_writer.set_speed(last_set_speed, "", comment);
        Vec2d prev = this->point_to_gcode_quantized(new_points[0].p);
        for (size_t i = 1; i < new_points.size(); i++) {
            const ProcessedPoint &processed_point = new_points[i];
            Vec2d p = this->point_to_gcode_quantized(processed_point.p);
            const double line_length = (p - prev).norm();
            if (m_enable_cooling_markers && enable_overhang_bridge_fan) {
                if (is_bridge(path.role()) || check_overhang_fan(new_points[i - 1].overlap) ) {
                    if(!is_overhang_fan_on)
                        gcode += ";_OVERHANG_FAN_START\n";
                    is_overhang_fan_on = true;
                }else {
                    if (is_overhang_fan_on) {
                        gcode += ";_OVERHANG_FAN_END\n";
                        is_overhang_fan_on = false;
                    }
                    
        }
            }
            gcode +=
                m_writer.extrude_to_xy(p, e_per_mm * line_length, GCodeWriter::full_gcode_comment ? description : "");

            prev = p;
            double new_speed = std::max((float)EXTRUDER_CONFIG(slow_down_min_speed), processed_point.speed) * 60.0;
            if (last_set_speed != new_speed) {
                gcode += m_writer.set_speed(new_speed, "", comment);
                last_set_speed = new_speed;
            }
        }
        if (is_overhang_fan_on) {
            is_overhang_fan_on = false;
            gcode += ";_OVERHANG_FAN_END\n";
        }
    }
    if (m_enable_cooling_markers) {
        gcode += ";_EXTRUDE_END\n";
    }
    this->set_last_pos(path.last_point());
    return gcode;
}

// This method accepts &point in print coordinates.
std::string GCode::travel_to(const Point &point, ExtrusionRole role, std::string comment)
{
    /*  Define the travel move as a line between current position and the taget point.
        This is expressed in print coordinates, so it will need to be translated by
        this->origin in order to get G-code coordinates.  */
    Polyline travel { this->last_pos(), point };

    // check whether a straight travel move would need retraction
    LiftType lift_type = LiftType::SpiralLift;
    bool needs_retraction = this->needs_retraction(travel, role, lift_type);
    // check whether wipe could be disabled without causing visible stringing
    bool could_be_wipe_disabled       = false;
    // Save state of use_external_mp_once for the case that will be needed to call twice m_avoid_crossing_perimeters.travel_to.
    const bool used_external_mp_once  = m_avoid_crossing_perimeters.used_external_mp_once();
    std::string gcode;

    // SoftFever
    if (this->on_first_layer()) {
        if (m_config.default_acceleration.value > 0) {
            auto accel = (unsigned int)floor(m_config.initial_layer_acceleration.value + 0.5);
            if (accel > 0)
                gcode += m_writer.set_acceleration(accel);
        }
        if (m_config.default_jerk.value > 0) {
            auto jerk = m_config.initial_layer_jerk.value;
            if (jerk > 0)
                gcode += m_writer.set_jerk_xy(jerk);
        }
    }
    else
    {
        if(m_config.default_jerk.value > 0)
        {
            auto jerk = m_config.travel_jerk.value;
            auto accel = (unsigned int)floor(m_config.travel_acceleration.value + 0.5);
            if(jerk > 0)
                gcode += m_writer.set_jerk_xy(jerk);
            
            if(accel > 0)
                gcode += m_writer.set_acceleration(accel);
        }
    }
    // if a retraction would be needed, try to use reduce_crossing_wall to plan a
    // multi-hop travel path inside the configuration space
    if (needs_retraction
        && m_config.reduce_crossing_wall
        && ! m_avoid_crossing_perimeters.disabled_once()
        //BBS: don't generate detour travel paths when current position is unclear
        && m_writer.is_current_position_clear()) {
        travel = m_avoid_crossing_perimeters.travel_to(*this, point, &could_be_wipe_disabled);
        // check again whether the new travel path still needs a retraction
        needs_retraction = this->needs_retraction(travel, role, lift_type);
        //if (needs_retraction && m_layer_index > 1) exit(0);
    }

    // Re-allow reduce_crossing_wall for the next travel moves
    m_avoid_crossing_perimeters.reset_once_modifiers();

    // generate G-code for the travel move
    if (needs_retraction) {
        if (m_config.reduce_crossing_wall && could_be_wipe_disabled)
            m_wipe.reset_path();

        Point last_post_before_retract = this->last_pos();
        gcode += this->retract(false, false, lift_type);
        // When "Wipe while retracting" is enabled, then extruder moves to another position, and travel from this position can cross perimeters.
        // Because of it, it is necessary to call avoid crossing perimeters again with new starting point after calling retraction()
        // FIXME Lukas H.: Try to predict if this second calling of avoid crossing perimeters will be needed or not. It could save computations.
        if (last_post_before_retract != this->last_pos() && m_config.reduce_crossing_wall) {
            // If in the previous call of m_avoid_crossing_perimeters.travel_to was use_external_mp_once set to true restore this value for next call.
            if (used_external_mp_once)
                m_avoid_crossing_perimeters.use_external_mp_once();
            travel = m_avoid_crossing_perimeters.travel_to(*this, point);
            // If state of use_external_mp_once was changed reset it to right value.
            if (used_external_mp_once)
                m_avoid_crossing_perimeters.reset_once_modifiers();
        }
    } else
        // Reset the wipe path when traveling, so one would not wipe along an old path.
        m_wipe.reset_path();

    // use G1 because we rely on paths being straight (G0 may make round paths)
    if (travel.size() >= 2) {
        for (size_t i = 1; i < travel.size(); ++ i) {
            // BBS. Process lazy layer change, but don't do lazy layer change when enable spiral vase
            Vec3d curr_pos = m_writer.get_position();
            if (i == 1 && !m_spiral_vase) {
                Vec2d dest2d = this->point_to_gcode(travel.points[i]);
                Vec3d dest3d(dest2d(0), dest2d(1), m_nominal_z);
                gcode += m_writer.travel_to_xyz(dest3d, comment+" travel_to_xyz");
            } else {
                gcode += m_writer.travel_to_xy(this->point_to_gcode(travel.points[i]), comment+" travel_to_xy");
            }
        }
        this->set_last_pos(travel.points.back());
    }
    return gcode;
}

//BBS
LiftType GCode::to_lift_type(ZHopType z_hop_types) {
    switch (z_hop_types)
    {
    case ZHopType::zhtNormal:
        return LiftType::NormalLift;
    case ZHopType::zhtSlope:
        return LiftType::LazyLift;
    case ZHopType::zhtSpiral:
        return LiftType::SpiralLift;
    default:
        // if no corresponding lift type, use normal lift
        return LiftType::NormalLift;
    }
};

bool GCode::needs_retraction(const Polyline &travel, ExtrusionRole role, LiftType& lift_type)
{
    if (travel.length() < scale_(EXTRUDER_CONFIG(retraction_minimum_travel))) {
        // skip retraction if the move is shorter than the configured threshold
        return false;
    }

    auto is_through_overhang = [this](const Polyline& travel) {
        const float protect_z_scaled = scale_(0.4);
        std::pair<float, float> z_range;
        z_range.second = m_layer ? m_layer->print_z : 0.f;
        z_range.first = std::max(0.f, z_range.second - protect_z_scaled);
        for (auto object : m_curr_print->objects()) {
            BoundingBox obj_bbox = object->bounding_box();
            BoundingBox travel_bbox = get_extents(travel);
            obj_bbox.offset(scale_(EPSILON));
            if (!obj_bbox.overlap(travel_bbox))
                continue;

            for (auto layer : object->layers()) {
                if (layer->print_z < z_range.first)
                    continue;

                if (layer->print_z > z_range.second + EPSILON)
                    break;

                for (ExPolygon& overhang : layer->loverhangs) {
                    if (overhang.contains(travel))
                        return true;
                }
            }
        }

        return false;
    };

    float max_z_hop = 0.f;
    for (int i = 0; i < m_config.z_hop.size(); i++)
        max_z_hop = std::max(max_z_hop, (float)m_config.z_hop.get_at(i));
    float travel_len_thresh = max_z_hop / tan(GCodeWriter::slope_threshold);
    float accum_len = 0.f;
    Polyline clipped_travel;
    for (auto line : travel.lines()) {
        if (accum_len + line.length() > travel_len_thresh + EPSILON) {
            Point end_pnt = line.a + line.normal() * (travel_len_thresh - accum_len);
            clipped_travel.append(Polyline(line.a, end_pnt));
            break;
        }
        else {
            clipped_travel.append(Polyline(line.a, line.b));
            accum_len += line.length();
        }
    }

    //BBS: force to retract when leave from external perimeter for a long travel
    //Better way is judging whether the travel move direction is same with last extrusion move.
    if (is_perimeter(m_last_processor_extrusion_role) && m_last_processor_extrusion_role != erPerimeter) {
        if (ZHopType(EXTRUDER_CONFIG(z_hop_types)) == ZHopType::zhtAuto) {
            lift_type = is_through_overhang(clipped_travel) ? LiftType::SpiralLift : LiftType::LazyLift;
        }
        else {
            lift_type = to_lift_type(ZHopType(EXTRUDER_CONFIG(z_hop_types)));
        }
        return true;
    }

    if (role == erSupportMaterial || role == erSupportTransition) {
        const SupportLayer* support_layer = dynamic_cast<const SupportLayer*>(m_layer);
        //FIXME support_layer->support_islands.contains should use some search structure!
        if (support_layer != NULL && support_layer->support_islands.contains(travel))
            // skip retraction if this is a travel move inside a support material island
            //FIXME not retracting over a long path may cause oozing, which in turn may result in missing material
            // at the end of the extrusion path!
            return false;
        //reduce the retractions in lightning infills for tree support
        if (support_layer != NULL && support_layer->support_type==stInnerTree)
            for (auto &area : support_layer->base_areas)
                if (area.contains(travel))
                    return false;
    }
    //BBS: need retract when long moving to print perimeter to avoid dropping of material
    if (!is_perimeter(role) && m_config.reduce_infill_retraction && m_layer != nullptr &&
        m_config.sparse_infill_density.value > 0 && m_layer->any_internal_region_slice_contains(travel))
        // Skip retraction if travel is contained in an internal slice *and*
        // internal infill is enabled (so that stringing is entirely not visible).
        //FIXME any_internal_region_slice_contains() is potentionally very slow, it shall test for the bounding boxes first.
        return false;

    // retract if reduce_infill_retraction is disabled or doesn't apply when role is perimeter
    if (ZHopType(EXTRUDER_CONFIG(z_hop_types)) == ZHopType::zhtAuto) {
        lift_type = is_through_overhang(clipped_travel) ? LiftType::SpiralLift : LiftType::LazyLift;
    }
    else {
        lift_type = to_lift_type(ZHopType(EXTRUDER_CONFIG(z_hop_types)));
    }
    return true;
}

std::string GCode::retract(bool toolchange, bool is_last_retraction, LiftType lift_type)
{
    std::string gcode;

    if (m_writer.extruder() == nullptr)
        return gcode;

    // wipe (if it's enabled for this extruder and we have a stored wipe path and no-zero wipe distance)
    if (EXTRUDER_CONFIG(wipe) && m_wipe.has_path() && scale_(EXTRUDER_CONFIG(wipe_distance)) > SCALED_EPSILON) {
        gcode += toolchange ? m_writer.retract_for_toolchange(true) : m_writer.retract(true);
        gcode += m_wipe.wipe(*this, toolchange, is_last_retraction);
    }

    /*  The parent class will decide whether we need to perform an actual retraction
        (the extruder might be already retracted fully or partially). We call these
        methods even if we performed wipe, since this will ensure the entire retraction
        length is honored in case wipe path was too short.  */
    gcode += toolchange ? m_writer.retract_for_toolchange() : m_writer.retract();

    gcode += m_writer.reset_e();
    //BBS
    if (m_writer.extruder()->retraction_length() > 0 || m_config.use_firmware_retraction) {
        // BBS: don't do lazy_lift when enable spiral vase
        size_t extruder_id = m_writer.extruder()->id();
        gcode += m_writer.lift(!m_spiral_vase ? lift_type : LiftType::NormalLift);
    }

    return gcode;
}

std::string GCode::set_extruder(unsigned int extruder_id, double print_z)
{
    if (!m_writer.need_toolchange(extruder_id))
        return "";

    // if we are running a single-extruder setup, just set the extruder and return nothing
    if (!m_writer.multiple_extruders) {
        m_placeholder_parser.set("current_extruder", extruder_id);

        std::string gcode;
        // Append the filament start G-code.
        const std::string &filament_start_gcode = m_config.filament_start_gcode.get_at(extruder_id);
        if (! filament_start_gcode.empty()) {
            // Process the filament_start_gcode for the filament.
            gcode += this->placeholder_parser_process("filament_start_gcode", filament_start_gcode, extruder_id);
            check_add_eol(gcode);
        }
        if (m_config.enable_pressure_advance.get_at(extruder_id)) {
            gcode += m_writer.set_pressure_advance(m_config.pressure_advance.get_at(extruder_id));
        }

        gcode += m_writer.toolchange(extruder_id);
        return gcode;
    }

    // BBS. Should be placed before retract.
    m_toolchange_count++;

    // prepend retraction on the current extruder
    std::string gcode = this->retract(true, false);

    // Always reset the extrusion path, even if the tool change retract is set to zero.
    m_wipe.reset_path();

    if (m_writer.extruder() != nullptr) {
        // Process the custom filament_end_gcode. set_extruder() is only called if there is no wipe tower
        // so it should not be injected twice.
        unsigned int        old_extruder_id     = m_writer.extruder()->id();
        const std::string  &filament_end_gcode  = m_config.filament_end_gcode.get_at(old_extruder_id);
        if (! filament_end_gcode.empty()) {
            gcode += placeholder_parser_process("filament_end_gcode", filament_end_gcode, old_extruder_id);
            check_add_eol(gcode);
        }
    }


    // If ooze prevention is enabled, park current extruder in the nearest
    // standby point and set it to the standby temperature.
    if (m_ooze_prevention.enable && m_writer.extruder() != nullptr)
        gcode += m_ooze_prevention.pre_toolchange(*this);

    // BBS
    float new_retract_length = m_config.retraction_length.get_at(extruder_id);
    float new_retract_length_toolchange = m_config.retract_length_toolchange.get_at(extruder_id);
    int new_filament_temp = this->on_first_layer() ? m_config.nozzle_temperature_initial_layer.get_at(extruder_id): m_config.nozzle_temperature.get_at(extruder_id);
    Vec3d nozzle_pos = m_writer.get_position();
    float old_retract_length, old_retract_length_toolchange, wipe_volume;
    int old_filament_temp, old_filament_e_feedrate;

    float filament_area = float((M_PI / 4.f) * pow(m_config.filament_diameter.get_at(extruder_id), 2));

    if (m_writer.extruder() != nullptr) {
        std::vector<float> flush_matrix(cast<float>(m_config.flush_volumes_matrix.values));
        const unsigned int number_of_extruders = (unsigned int)(sqrt(flush_matrix.size()) + EPSILON);
        assert(m_writer.extruder()->id() < number_of_extruders);

        int previous_extruder_id = m_writer.extruder()->id();
        old_retract_length = m_config.retraction_length.get_at(previous_extruder_id);
        old_retract_length_toolchange = m_config.retract_length_toolchange.get_at(previous_extruder_id);
        old_filament_temp = this->on_first_layer()? m_config.nozzle_temperature_initial_layer.get_at(previous_extruder_id) : m_config.nozzle_temperature.get_at(previous_extruder_id);
        wipe_volume = flush_matrix[previous_extruder_id * number_of_extruders + extruder_id];
        wipe_volume *= m_config.flush_multiplier;
        old_filament_e_feedrate = (int)(60.0 * m_config.filament_max_volumetric_speed.get_at(previous_extruder_id) / filament_area);
        old_filament_e_feedrate = old_filament_e_feedrate == 0 ? 100 : old_filament_e_feedrate;
    }
    else {
        old_retract_length = 0.f;
        old_retract_length_toolchange = 0.f;
        old_filament_temp = 0;
        wipe_volume = 0.f;
        old_filament_e_feedrate = 200;
    }
    float wipe_length = wipe_volume / filament_area;
    int new_filament_e_feedrate = (int)(60.0 * m_config.filament_max_volumetric_speed.get_at(extruder_id) / filament_area);
    new_filament_e_feedrate = new_filament_e_feedrate == 0 ? 100 : new_filament_e_feedrate;

    DynamicConfig dyn_config;
    dyn_config.set_key_value("previous_extruder", new ConfigOptionInt((int)(m_writer.extruder() != nullptr ? m_writer.extruder()->id() : -1)));
    dyn_config.set_key_value("next_extruder", new ConfigOptionInt((int)extruder_id));
    dyn_config.set_key_value("layer_num", new ConfigOptionInt(m_layer_index));
    dyn_config.set_key_value("layer_z", new ConfigOptionFloat(print_z));
    dyn_config.set_key_value("max_layer_z", new ConfigOptionFloat(m_max_layer_z));
    dyn_config.set_key_value("toolchange_count", new ConfigOptionInt((int)m_toolchange_count));
    //BBS: fan speed is useless placeholer now, but we don't remove it to avoid
    //slicing error in old change_filament_gcode in old 3MF
    dyn_config.set_key_value("fan_speed", new ConfigOptionInt((int)0));
    dyn_config.set_key_value("old_retract_length", new ConfigOptionFloat(old_retract_length));
    dyn_config.set_key_value("new_retract_length", new ConfigOptionFloat(new_retract_length));
    dyn_config.set_key_value("old_retract_length_toolchange", new ConfigOptionFloat(old_retract_length_toolchange));
    dyn_config.set_key_value("new_retract_length_toolchange", new ConfigOptionFloat(new_retract_length_toolchange));
    dyn_config.set_key_value("old_filament_temp", new ConfigOptionInt(old_filament_temp));
    dyn_config.set_key_value("new_filament_temp", new ConfigOptionInt(new_filament_temp));
    dyn_config.set_key_value("x_after_toolchange", new ConfigOptionFloat(nozzle_pos(0)));
    dyn_config.set_key_value("y_after_toolchange", new ConfigOptionFloat(nozzle_pos(1)));
    dyn_config.set_key_value("z_after_toolchange", new ConfigOptionFloat(nozzle_pos(2)));
    dyn_config.set_key_value("first_flush_volume", new ConfigOptionFloat(wipe_length / 2.f));
    dyn_config.set_key_value("second_flush_volume", new ConfigOptionFloat(wipe_length / 2.f));
    dyn_config.set_key_value("old_filament_e_feedrate", new ConfigOptionInt(old_filament_e_feedrate));
    dyn_config.set_key_value("new_filament_e_feedrate", new ConfigOptionInt(new_filament_e_feedrate));
    dyn_config.set_key_value("travel_point_1_x", new ConfigOptionFloat(float(travel_point_1.x())));
    dyn_config.set_key_value("travel_point_1_y", new ConfigOptionFloat(float(travel_point_1.y())));
    dyn_config.set_key_value("travel_point_2_x", new ConfigOptionFloat(float(travel_point_2.x())));
    dyn_config.set_key_value("travel_point_2_y", new ConfigOptionFloat(float(travel_point_2.y())));
    dyn_config.set_key_value("travel_point_3_x", new ConfigOptionFloat(float(travel_point_3.x())));
    dyn_config.set_key_value("travel_point_3_y", new ConfigOptionFloat(float(travel_point_3.y())));

    int flush_count = std::min(g_max_flush_count, (int)std::round(wipe_volume / g_purge_volume_one_time));
    float flush_unit = wipe_length / flush_count;
    int flush_idx = 0;
    for (; flush_idx < flush_count; flush_idx++) {
        char key_value[64] = { 0 };
        snprintf(key_value, sizeof(key_value), "flush_length_%d", flush_idx + 1);
        dyn_config.set_key_value(key_value, new ConfigOptionFloat(flush_unit));
    }

    for (; flush_idx < g_max_flush_count; flush_idx++) {
        char key_value[64] = { 0 };
        snprintf(key_value, sizeof(key_value), "flush_length_%d", flush_idx + 1);
        dyn_config.set_key_value(key_value, new ConfigOptionFloat(0.f));
    }

    // Process the custom change_filament_gcode.
    const std::string& change_filament_gcode = m_config.change_filament_gcode.value;
    std::string toolchange_gcode_parsed;
    if (!change_filament_gcode.empty()) {
        toolchange_gcode_parsed = placeholder_parser_process("change_filament_gcode", change_filament_gcode, extruder_id, &dyn_config);
        gcode += toolchange_gcode_parsed;
        check_add_eol(gcode);
        //BBS: gcode writer doesn't know where the extruder is and whether fan speed is changed after inserting tool change gcode
        //Set this flag so that normal lift will be used the first time after tool change.
        gcode += ";_FORCE_RESUME_FAN_SPEED\n";
        m_writer.set_current_position_clear(false);
    }

    // BBS. Reset old extruder E-value.
    // Keep retract length because Custom GCode will guarantee retract length be the same as toolchange
    if (m_config.single_extruder_multi_material) {
        m_writer.reset_e();
    }

    // We inform the writer about what is happening, but we may not use the resulting gcode.
    std::string toolchange_command = m_writer.toolchange(extruder_id);
    if (! custom_gcode_changes_tool(toolchange_gcode_parsed, m_writer.toolchange_prefix(), extruder_id))
        gcode += toolchange_command;
    else {
        // user provided his own toolchange gcode, no need to do anything
    }

    // Set the temperature if the wipe tower didn't (not needed for non-single extruder MM)
    if (m_config.single_extruder_multi_material && !m_config.enable_prime_tower) {
        int temp = (m_layer_index <= 0 ? m_config.nozzle_temperature_initial_layer.get_at(extruder_id) :
                                         m_config.nozzle_temperature.get_at(extruder_id));

        gcode += m_writer.set_temperature(temp, false);
    }

    m_placeholder_parser.set("current_extruder", extruder_id);

    // Append the filament start G-code.
    const std::string &filament_start_gcode = m_config.filament_start_gcode.get_at(extruder_id);
    if (! filament_start_gcode.empty()) {
        // Process the filament_start_gcode for the new filament.
        gcode += this->placeholder_parser_process("filament_start_gcode", filament_start_gcode, extruder_id);
        check_add_eol(gcode);
    }
    // Set the new extruder to the operating temperature.
    if (m_ooze_prevention.enable)
        gcode += m_ooze_prevention.post_toolchange(*this);

    if (m_config.enable_pressure_advance.get_at(extruder_id)) {
        gcode += m_writer.set_pressure_advance(m_config.pressure_advance.get_at(extruder_id));
    }

    return gcode;
}

// convert a model-space scaled point into G-code coordinates
Vec2d GCode::point_to_gcode(const Point &point) const
{
    Vec2d extruder_offset = EXTRUDER_CONFIG(extruder_offset);
    return unscale(point) + m_origin - extruder_offset;
}

// convert a model-space scaled point into G-code coordinates
Point GCode::gcode_to_point(const Vec2d &point) const
{
    Vec2d extruder_offset = EXTRUDER_CONFIG(extruder_offset);
    return Point(
        scale_(point(0) - m_origin(0) + extruder_offset(0)),
        scale_(point(1) - m_origin(1) + extruder_offset(1)));
}

Vec2d GCode::point_to_gcode_quantized(const Point& point) const
{
    Vec2d p = this->point_to_gcode(point);
    return { GCodeFormatter::quantize_xyzf(p.x()), GCodeFormatter::quantize_xyzf(p.y()) };
}


// Goes through by_region std::vector and returns reference to a subvector of entities, that are to be printed
// during infill/perimeter wiping, or normally (depends on wiping_entities parameter)
// Fills in by_region_per_copy_cache and returns its reference.
const std::vector<GCode::ObjectByExtruder::Island::Region>& GCode::ObjectByExtruder::Island::by_region_per_copy(std::vector<Region> &by_region_per_copy_cache, unsigned int copy, unsigned int extruder, bool wiping_entities) const
{
    bool has_overrides = false;
    for (const auto& reg : by_region)
        if (! reg.infills_overrides.empty() || ! reg.perimeters_overrides.empty()) {
            has_overrides = true;
            break;
        }

    // Data is cleared, but the memory is not.
    by_region_per_copy_cache.clear();

    if (! has_overrides)
        // Simple case. No need to copy the regions.
        return wiping_entities ? by_region_per_copy_cache : this->by_region;

    // Complex case. Some of the extrusions of some object instances are to be printed first - those are the wiping extrusions.
    // Some of the extrusions of some object instances are printed later - those are the clean print extrusions.
    // Filter out the extrusions based on the infill_overrides / perimeter_overrides:

    for (const auto& reg : by_region) {
        by_region_per_copy_cache.emplace_back(); // creates a region in the newly created Island

        // Now we are going to iterate through perimeters and infills and pick ones that are supposed to be printed
        // References are used so that we don't have to repeat the same code
        for (int iter = 0; iter < 2; ++iter) {
            const ExtrusionEntitiesPtr&										entities    = (iter ? reg.infills : reg.perimeters);
            ExtrusionEntitiesPtr&   										target_eec  = (iter ? by_region_per_copy_cache.back().infills : by_region_per_copy_cache.back().perimeters);
            const std::vector<const WipingExtrusions::ExtruderPerCopy*>& 	overrides   = (iter ? reg.infills_overrides : reg.perimeters_overrides);

            // Now the most important thing - which extrusion should we print.
            // See function ToolOrdering::get_extruder_overrides for details about the negative numbers hack.
            if (wiping_entities) {
                // Apply overrides for this region.
                for (unsigned int i = 0; i < overrides.size(); ++ i) {
                    const WipingExtrusions::ExtruderPerCopy *this_override = overrides[i];
                    // This copy (aka object instance) should be printed with this extruder, which overrides the default one.
                    if (this_override != nullptr && (*this_override)[copy] == int(extruder))
                        target_eec.emplace_back(entities[i]);
                }
            } else {
                // Apply normal extrusions (non-overrides) for this region.
                unsigned int i = 0;
                for (; i < overrides.size(); ++ i) {
                    const WipingExtrusions::ExtruderPerCopy *this_override = overrides[i];
                    // This copy (aka object instance) should be printed with this extruder, which shall be equal to the default one.
                    if (this_override == nullptr || (*this_override)[copy] == -int(extruder)-1)
                        target_eec.emplace_back(entities[i]);
                }
                for (; i < entities.size(); ++ i)
                    target_eec.emplace_back(entities[i]);
            }
        }
    }
    return by_region_per_copy_cache;
}

// This function takes the eec and appends its entities to either perimeters or infills of this Region (depending on the first parameter)
// It also saves pointer to ExtruderPerCopy struct (for each entity), that holds information about which extruders should be used for which copy.
void GCode::ObjectByExtruder::Island::Region::append(const Type type, const ExtrusionEntityCollection* eec, const WipingExtrusions::ExtruderPerCopy* copies_extruder)
{
    // We are going to manipulate either perimeters or infills, exactly in the same way. Let's create pointers to the proper structure to not repeat ourselves:
    ExtrusionEntitiesPtr*									perimeters_or_infills;
    std::vector<const WipingExtrusions::ExtruderPerCopy*>* 	perimeters_or_infills_overrides;

    switch (type) {
    case PERIMETERS:
        perimeters_or_infills 			= &perimeters;
        perimeters_or_infills_overrides = &perimeters_overrides;
        break;
    case INFILL:
        perimeters_or_infills 			= &infills;
        perimeters_or_infills_overrides = &infills_overrides;
        break;
    default:
    	throw Slic3r::InvalidArgument("Unknown parameter!");
    }

    // First we append the entities, there are eec->entities.size() of them:
    size_t old_size = perimeters_or_infills->size();
    size_t new_size = old_size + (eec->can_reverse() ? eec->entities.size() : 1);
    perimeters_or_infills->reserve(new_size);
    if (eec->can_reverse()) {
        for (auto* ee : eec->entities)
            perimeters_or_infills->emplace_back(ee);
    } else
        perimeters_or_infills->emplace_back(const_cast<ExtrusionEntityCollection*>(eec));

    if (copies_extruder != nullptr) {
        // Don't reallocate overrides if not needed.
        // Missing overrides are implicitely considered non-overridden.
        perimeters_or_infills_overrides->reserve(new_size);
        perimeters_or_infills_overrides->resize(old_size, nullptr);
        perimeters_or_infills_overrides->resize(new_size, copies_extruder);
    }
}

}   // namespace Slic3r
