#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/LocalesUtils.hpp"
#include "GCodeProcessor.hpp"

#include <boost/log/trivial.hpp>
#if ENABLE_VALIDATE_CUSTOM_GCODE
#include <boost/algorithm/string/predicate.hpp>
#endif // ENABLE_VALIDATE_CUSTOM_GCODE
#include <boost/algorithm/string/split.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/nowide/cstdio.hpp>
#if ENABLE_GCODE_WINDOW
#include <boost/filesystem/path.hpp>
#endif // ENABLE_GCODE_WINDOW

#include <float.h>
#include <assert.h>

#if __has_include(<charconv>)
    #include <charconv>
    #include <utility>
#endif

#include <chrono>

static const float INCHES_TO_MM = 25.4f;
static const float MMMIN_TO_MMSEC = 1.0f / 60.0f;
static const float DEFAULT_ACCELERATION = 1500.0f; // Prusa Firmware 1_75mm_MK2
static const float DEFAULT_TRAVEL_ACCELERATION = 1250.0f;

static const size_t MIN_EXTRUDERS_COUNT = 5;
static const float DEFAULT_FILAMENT_DIAMETER = 1.75f;
static const float DEFAULT_FILAMENT_DENSITY = 1.245f;
static const Slic3r::Vec3f DEFAULT_EXTRUDER_OFFSET = Slic3r::Vec3f::Zero();

namespace Slic3r {

#if ENABLE_VALIDATE_CUSTOM_GCODE
const std::vector<std::string> GCodeProcessor::Reserved_Tags = {
    "TYPE:",
    "WIPE_START",
    "WIPE_END",
    "HEIGHT:",
    "WIDTH:",
    "LAYER_CHANGE",
    "COLOR_CHANGE",
    "PAUSE_PRINT",
    "CUSTOM_GCODE",
    "_GP_FIRST_LINE_M73_PLACEHOLDER",
    "_GP_LAST_LINE_M73_PLACEHOLDER",
    "_GP_ESTIMATED_PRINTING_TIME_PLACEHOLDER"
};
#else
const std::string GCodeProcessor::Extrusion_Role_Tag = "TYPE:";
const std::string GCodeProcessor::Wipe_Start_Tag     = "WIPE_START";
const std::string GCodeProcessor::Wipe_End_Tag       = "WIPE_END";
const std::string GCodeProcessor::Height_Tag         = "HEIGHT:";
const std::string GCodeProcessor::Width_Tag          = "WIDTH:";
const std::string GCodeProcessor::Layer_Change_Tag   = "LAYER_CHANGE";
const std::string GCodeProcessor::Color_Change_Tag   = "COLOR_CHANGE";
const std::string GCodeProcessor::Pause_Print_Tag    = "PAUSE_PRINT";
const std::string GCodeProcessor::Custom_Code_Tag    = "CUSTOM_GCODE";

const std::string GCodeProcessor::First_Line_M73_Placeholder_Tag          = "; _GP_FIRST_LINE_M73_PLACEHOLDER";
const std::string GCodeProcessor::Last_Line_M73_Placeholder_Tag           = "; _GP_LAST_LINE_M73_PLACEHOLDER";
const std::string GCodeProcessor::Estimated_Printing_Time_Placeholder_Tag = "; _GP_ESTIMATED_PRINTING_TIME_PLACEHOLDER";
#endif // ENABLE_VALIDATE_CUSTOM_GCODE

const float GCodeProcessor::Wipe_Width = 0.05f;
const float GCodeProcessor::Wipe_Height = 0.05f;

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
const std::string GCodeProcessor::Mm3_Per_Mm_Tag = "MM3_PER_MM:";
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

static void set_option_value(ConfigOptionFloats& option, size_t id, float value)
{
    if (id < option.values.size())
        option.values[id] = static_cast<double>(value);
};

static float get_option_value(const ConfigOptionFloats& option, size_t id)
{
    return option.values.empty() ? 0.0f :
        ((id < option.values.size()) ? static_cast<float>(option.values[id]) : static_cast<float>(option.values.back()));
}

static float estimated_acceleration_distance(float initial_rate, float target_rate, float acceleration)
{
    return (acceleration == 0.0f) ? 0.0f : (sqr(target_rate) - sqr(initial_rate)) / (2.0f * acceleration);
}

static float intersection_distance(float initial_rate, float final_rate, float acceleration, float distance)
{
    return (acceleration == 0.0f) ? 0.0f : (2.0f * acceleration * distance - sqr(initial_rate) + sqr(final_rate)) / (4.0f * acceleration);
}

static float speed_from_distance(float initial_feedrate, float distance, float acceleration)
{
    // to avoid invalid negative numbers due to numerical errors 
    float value = std::max(0.0f, sqr(initial_feedrate) + 2.0f * acceleration * distance);
    return ::sqrt(value);
}

// Calculates the maximum allowable speed at this point when you must be able to reach target_velocity using the 
// acceleration within the allotted distance.
static float max_allowable_speed(float acceleration, float target_velocity, float distance)
{
    // to avoid invalid negative numbers due to numerical errors 
    float value = std::max(0.0f, sqr(target_velocity) - 2.0f * acceleration * distance);
    return std::sqrt(value);
}

static float acceleration_time_from_distance(float initial_feedrate, float distance, float acceleration)
{
    return (acceleration != 0.0f) ? (speed_from_distance(initial_feedrate, distance, acceleration) - initial_feedrate) / acceleration : 0.0f;
}

void GCodeProcessor::CachedPosition::reset()
{
    std::fill(position.begin(), position.end(), FLT_MAX);
    feedrate = FLT_MAX;
}

void GCodeProcessor::CpColor::reset()
{
    counter = 0;
    current = 0;
}

float GCodeProcessor::Trapezoid::acceleration_time(float entry_feedrate, float acceleration) const
{
    return acceleration_time_from_distance(entry_feedrate, accelerate_until, acceleration);
}

float GCodeProcessor::Trapezoid::cruise_time() const
{
    return (cruise_feedrate != 0.0f) ? cruise_distance() / cruise_feedrate : 0.0f;
}

float GCodeProcessor::Trapezoid::deceleration_time(float distance, float acceleration) const
{
    return acceleration_time_from_distance(cruise_feedrate, (distance - decelerate_after), -acceleration);
}

float GCodeProcessor::Trapezoid::cruise_distance() const
{
    return decelerate_after - accelerate_until;
}

void GCodeProcessor::TimeBlock::calculate_trapezoid()
{
    trapezoid.cruise_feedrate = feedrate_profile.cruise;

    float accelerate_distance = std::max(0.0f, estimated_acceleration_distance(feedrate_profile.entry, feedrate_profile.cruise, acceleration));
    float decelerate_distance = std::max(0.0f, estimated_acceleration_distance(feedrate_profile.cruise, feedrate_profile.exit, -acceleration));
    float cruise_distance = distance - accelerate_distance - decelerate_distance;

    // Not enough space to reach the nominal feedrate.
    // This means no cruising, and we'll have to use intersection_distance() to calculate when to abort acceleration 
    // and start braking in order to reach the exit_feedrate exactly at the end of this block.
    if (cruise_distance < 0.0f) {
        accelerate_distance = std::clamp(intersection_distance(feedrate_profile.entry, feedrate_profile.exit, acceleration, distance), 0.0f, distance);
        cruise_distance = 0.0f;
        trapezoid.cruise_feedrate = speed_from_distance(feedrate_profile.entry, accelerate_distance, acceleration);
    }

    trapezoid.accelerate_until = accelerate_distance;
    trapezoid.decelerate_after = accelerate_distance + cruise_distance;
}

float GCodeProcessor::TimeBlock::time() const
{
    return trapezoid.acceleration_time(feedrate_profile.entry, acceleration)
        + trapezoid.cruise_time()
        + trapezoid.deceleration_time(distance, acceleration);
}

void GCodeProcessor::TimeMachine::State::reset()
{
    feedrate = 0.0f;
    safe_feedrate = 0.0f;
    axis_feedrate = { 0.0f, 0.0f, 0.0f, 0.0f };
    abs_axis_feedrate = { 0.0f, 0.0f, 0.0f, 0.0f };
}

void GCodeProcessor::TimeMachine::CustomGCodeTime::reset()
{
    needed = false;
    cache = 0.0f;
    times = std::vector<std::pair<CustomGCode::Type, float>>();
}

void GCodeProcessor::TimeMachine::reset()
{
    enabled = false;
    acceleration = 0.0f;
    max_acceleration = 0.0f;
    travel_acceleration = 0.0f;
    max_travel_acceleration = 0.0f;
    extrude_factor_override_percentage = 1.0f;
    time = 0.0f;
#if ENABLE_EXTENDED_M73_LINES
    stop_times = std::vector<StopTime>();
#endif // ENABLE_EXTENDED_M73_LINES
    curr.reset();
    prev.reset();
    gcode_time.reset();
    blocks = std::vector<TimeBlock>();
    g1_times_cache = std::vector<G1LinesCacheItem>();
    std::fill(moves_time.begin(), moves_time.end(), 0.0f);
    std::fill(roles_time.begin(), roles_time.end(), 0.0f);
    layers_time = std::vector<float>();
}

void GCodeProcessor::TimeMachine::simulate_st_synchronize(float additional_time)
{
    if (!enabled)
        return;

    time += additional_time;
    gcode_time.cache += additional_time;
    calculate_time();
}

static void planner_forward_pass_kernel(GCodeProcessor::TimeBlock& prev, GCodeProcessor::TimeBlock& curr)
{
    // If the previous block is an acceleration block, but it is not long enough to complete the
    // full speed change within the block, we need to adjust the entry speed accordingly. Entry
    // speeds have already been reset, maximized, and reverse planned by reverse planner.
    // If nominal length is true, max junction speed is guaranteed to be reached. No need to recheck.
    if (!prev.flags.nominal_length) {
        if (prev.feedrate_profile.entry < curr.feedrate_profile.entry) {
            float entry_speed = std::min(curr.feedrate_profile.entry, max_allowable_speed(-prev.acceleration, prev.feedrate_profile.entry, prev.distance));

            // Check for junction speed change
            if (curr.feedrate_profile.entry != entry_speed) {
                curr.feedrate_profile.entry = entry_speed;
                curr.flags.recalculate = true;
            }
        }
    }
}

void planner_reverse_pass_kernel(GCodeProcessor::TimeBlock& curr, GCodeProcessor::TimeBlock& next)
{
    // If entry speed is already at the maximum entry speed, no need to recheck. Block is cruising.
    // If not, block in state of acceleration or deceleration. Reset entry speed to maximum and
    // check for maximum allowable speed reductions to ensure maximum possible planned speed.
    if (curr.feedrate_profile.entry != curr.max_entry_speed) {
        // If nominal length true, max junction speed is guaranteed to be reached. Only compute
        // for max allowable speed if block is decelerating and nominal length is false.
        if (!curr.flags.nominal_length && curr.max_entry_speed > next.feedrate_profile.entry)
            curr.feedrate_profile.entry = std::min(curr.max_entry_speed, max_allowable_speed(-curr.acceleration, next.feedrate_profile.entry, curr.distance));
        else
            curr.feedrate_profile.entry = curr.max_entry_speed;

        curr.flags.recalculate = true;
    }
}

static void recalculate_trapezoids(std::vector<GCodeProcessor::TimeBlock>& blocks)
{
    GCodeProcessor::TimeBlock* curr = nullptr;
    GCodeProcessor::TimeBlock* next = nullptr;

    for (size_t i = 0; i < blocks.size(); ++i) {
        GCodeProcessor::TimeBlock& b = blocks[i];

        curr = next;
        next = &b;

        if (curr != nullptr) {
            // Recalculate if current block entry or exit junction speed has changed.
            if (curr->flags.recalculate || next->flags.recalculate) {
                // NOTE: Entry and exit factors always > 0 by all previous logic operations.
                GCodeProcessor::TimeBlock block = *curr;
                block.feedrate_profile.exit = next->feedrate_profile.entry;
                block.calculate_trapezoid();
                curr->trapezoid = block.trapezoid;
                curr->flags.recalculate = false; // Reset current only to ensure next trapezoid is computed
            }
        }
    }

    // Last/newest block in buffer. Always recalculated.
    if (next != nullptr) {
        GCodeProcessor::TimeBlock block = *next;
        block.feedrate_profile.exit = next->safe_feedrate;
        block.calculate_trapezoid();
        next->trapezoid = block.trapezoid;
        next->flags.recalculate = false;
    }
}

void GCodeProcessor::TimeMachine::calculate_time(size_t keep_last_n_blocks)
{
    if (!enabled || blocks.size() < 2)
        return;

    assert(keep_last_n_blocks <= blocks.size());

    // forward_pass
    for (size_t i = 0; i + 1 < blocks.size(); ++i) {
        planner_forward_pass_kernel(blocks[i], blocks[i + 1]);
    }

    // reverse_pass
    for (int i = static_cast<int>(blocks.size()) - 1; i > 0; --i)
        planner_reverse_pass_kernel(blocks[i - 1], blocks[i]);

    recalculate_trapezoids(blocks);

    size_t n_blocks_process = blocks.size() - keep_last_n_blocks;
    for (size_t i = 0; i < n_blocks_process; ++i) {
        const TimeBlock& block = blocks[i];
        float block_time = block.time();
        time += block_time;
        gcode_time.cache += block_time;
        moves_time[static_cast<size_t>(block.move_type)] += block_time;
        roles_time[static_cast<size_t>(block.role)] += block_time;
        if (block.layer_id > 0) {
            if (block.layer_id >= layers_time.size()) {
                size_t curr_size = layers_time.size();
                layers_time.resize(block.layer_id);
                for (size_t i = curr_size; i < layers_time.size(); ++i) {
                    layers_time[i] = 0.0f;
                }
            }
            layers_time[block.layer_id - 1] += block_time;
        }
        g1_times_cache.push_back({ block.g1_line_id, time });
#if ENABLE_EXTENDED_M73_LINES
        // update times for remaining time to printer stop placeholders
        auto it_stop_time = std::lower_bound(stop_times.begin(), stop_times.end(), block.g1_line_id,
            [](const StopTime& t, unsigned int value) { return t.g1_line_id < value; });
        if (it_stop_time != stop_times.end() && it_stop_time->g1_line_id == block.g1_line_id)
            it_stop_time->elapsed_time = time;
#endif // ENABLE_EXTENDED_M73_LINES
    }

    if (keep_last_n_blocks)
        blocks.erase(blocks.begin(), blocks.begin() + n_blocks_process);
    else
        blocks.clear();
}

void GCodeProcessor::TimeProcessor::reset()
{
    extruder_unloaded = true;
    export_remaining_time_enabled = false;
    machine_envelope_processing_enabled = false;
    machine_limits = MachineEnvelopeConfig();
    filament_load_times = std::vector<float>();
    filament_unload_times = std::vector<float>();
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        machines[i].reset();
    }
    machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].enabled = true;
}

#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
void GCodeProcessor::TimeProcessor::post_process(const std::string& filename, std::vector<MoveVertex>& moves)
#else
void GCodeProcessor::TimeProcessor::post_process(const std::string& filename)
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
{
    boost::nowide::ifstream in(filename);
    if (!in.good())
        throw Slic3r::RuntimeError(std::string("Time estimator post process export failed.\nCannot open file for reading.\n"));

    // temporary file to contain modified gcode
    std::string out_path = filename + ".postprocess";
    FILE* out = boost::nowide::fopen(out_path.c_str(), "wb");
    if (out == nullptr)
        throw Slic3r::RuntimeError(std::string("Time estimator post process export failed.\nCannot open file for writing.\n"));

    auto time_in_minutes = [](float time_in_seconds) {
        return int(::roundf(time_in_seconds / 60.0f));
    };

#if ENABLE_EXTENDED_M73_LINES
    auto time_in_last_minute = [](float time_in_seconds) {
        assert(time_in_seconds <= 60.0f);
        return time_in_seconds / 60.0f;
    };

    auto format_line_M73_main = [](const std::string& mask, int percent, int time) {
#else
    auto format_line_M73 = [](const std::string& mask, int percent, int time) {
#endif // ENABLE_EXTENDED_M73_LINES
        char line_M73[64];
        sprintf(line_M73, mask.c_str(),
            std::to_string(percent).c_str(),
            std::to_string(time).c_str());
        return std::string(line_M73);
    };

#if ENABLE_EXTENDED_M73_LINES
    auto format_line_M73_stop_int = [](const std::string& mask, int time) {
        char line_M73[64];
        sprintf(line_M73, mask.c_str(), std::to_string(time).c_str());
        return std::string(line_M73);
    };

    auto format_time_float = [](float time) {
        return Slic3r::float_to_string_decimal_point(time, 2);
    };

    auto format_line_M73_stop_float = [format_time_float](const std::string& mask, float time) {
        char line_M73[64];
        sprintf(line_M73, mask.c_str(), format_time_float(time).c_str());
        return std::string(line_M73);
    };
#endif // ENABLE_EXTENDED_M73_LINES

    GCodeReader parser;
    std::string gcode_line;
    size_t g1_lines_counter = 0;
    // keeps track of last exported pair <percent, remaining time>
#if ENABLE_EXTENDED_M73_LINES
    std::array<std::pair<int, int>, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> last_exported_main;
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        last_exported_main[i] = { 0, time_in_minutes(machines[i].time) };
    }

    // keeps track of last exported remaining time to next printer stop
    std::array<int, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> last_exported_stop;
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        last_exported_stop[i] = time_in_minutes(machines[i].time);
    }
#else
    std::array<std::pair<int, int>, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> last_exported;
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        last_exported[i] = { 0, time_in_minutes(machines[i].time) };
    }
#endif // ENABLE_EXTENDED_M73_LINES

    // buffer line to export only when greater than 64K to reduce writing calls
    std::string export_line;

    // replace placeholder lines with the proper final value
    auto process_placeholders = [&](const std::string& gcode_line) {
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        unsigned int extra_lines_count = 0;
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER

        // remove trailing '\n'
        std::string line = gcode_line.substr(0, gcode_line.length() - 1);

        std::string ret;
#if ENABLE_VALIDATE_CUSTOM_GCODE
        if (line.length() > 1) {
            line = line.substr(1);
            if (export_remaining_time_enabled &&
                (line == reserved_tag(ETags::First_Line_M73_Placeholder) || line == reserved_tag(ETags::Last_Line_M73_Placeholder))) {
                for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
                    const TimeMachine& machine = machines[i];
                    if (machine.enabled) {
#if ENABLE_EXTENDED_M73_LINES
                        // export pair <percent, remaining time>
                        ret += format_line_M73_main(machine.line_m73_main_mask.c_str(),
                            (line == reserved_tag(ETags::First_Line_M73_Placeholder)) ? 0 : 100,
                            (line == reserved_tag(ETags::First_Line_M73_Placeholder)) ? time_in_minutes(machine.time) : 0);
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
                        ++extra_lines_count;
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER

                        // export remaining time to next printer stop
                        if (line == reserved_tag(ETags::First_Line_M73_Placeholder) && !machine.stop_times.empty()) {
                            int to_export_stop = time_in_minutes(machine.stop_times.front().elapsed_time);
                            ret += format_line_M73_stop_int(machine.line_m73_stop_mask.c_str(), to_export_stop);
                            last_exported_stop[i] = to_export_stop;
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
                            ++extra_lines_count;
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
                        }
#else
                        ret += format_line_M73(machine.line_m73_mask.c_str(),
                            (line == reserved_tag(ETags::First_Line_M73_Placeholder)) ? 0 : 100,
                            (line == reserved_tag(ETags::First_Line_M73_Placeholder)) ? time_in_minutes(machine.time) : 0);
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
                        ++extra_lines_count;
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
#endif // ENABLE_EXTENDED_M73_LINES
                    }
                }
            }
            else if (line == reserved_tag(ETags::Estimated_Printing_Time_Placeholder)) {
#else
        if (export_remaining_time_enabled && (line == First_Line_M73_Placeholder_Tag || line == Last_Line_M73_Placeholder_Tag)) {
            for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
                const TimeMachine& machine = machines[i];
                if (machine.enabled) {
                    ret += format_line_M73(machine.line_m73_mask.c_str(),
                        (line == First_Line_M73_Placeholder_Tag) ? 0 : 100,
                        (line == First_Line_M73_Placeholder_Tag) ? time_in_minutes(machines[i].time) : 0);
                }
            }
        }
        else if (line == Estimated_Printing_Time_Placeholder_Tag) {
#endif // ENABLE_VALIDATE_CUSTOM_GCODE
                for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
                    const TimeMachine& machine = machines[i];
                    PrintEstimatedStatistics::ETimeMode mode = static_cast<PrintEstimatedStatistics::ETimeMode>(i);
                    if (mode == PrintEstimatedStatistics::ETimeMode::Normal || machine.enabled) {
                        char buf[128];
                        sprintf(buf, "; estimated printing time (%s mode) = %s\n",
                            (mode == PrintEstimatedStatistics::ETimeMode::Normal) ? "normal" : "silent",
                            get_time_dhms(machine.time).c_str());
                        ret += buf;
                    }
                }
            }
#if ENABLE_VALIDATE_CUSTOM_GCODE
        }
#endif // ENABLE_VALIDATE_CUSTOM_GCODE

#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        return std::tuple(!ret.empty(), ret.empty() ? gcode_line : ret, (extra_lines_count == 0) ? extra_lines_count : extra_lines_count - 1);
#else
        return std::make_pair(!ret.empty(), ret.empty() ? gcode_line : ret);
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    };

    // check for temporary lines
    auto is_temporary_decoration = [](const std::string_view gcode_line) {
        // remove trailing '\n'
        assert(! gcode_line.empty());
        assert(gcode_line.back() == '\n');

        // return true for decorations which are used in processing the gcode but that should not be exported into the final gcode
        // i.e.:
        // bool ret = gcode_line.substr(0, gcode_line.length() - 1) == ";" + Layer_Change_Tag;
        // ...
        // return ret;
        return false;
    };

    // Iterators for the normal and silent cached time estimate entry recently processed, used by process_line_G1.
    auto g1_times_cache_it = Slic3r::reserve_vector<std::vector<TimeMachine::G1LinesCacheItem>::const_iterator>(machines.size());
    for (const auto& machine : machines)
        g1_times_cache_it.emplace_back(machine.g1_times_cache.begin());

    // add lines M73 to exported gcode
    auto process_line_G1 = [&]() {
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        unsigned int exported_lines_count = 0;
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        if (export_remaining_time_enabled) {
            for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
                const TimeMachine& machine = machines[i];
                if (machine.enabled) {
                    // export pair <percent, remaining time>
                    // Skip all machine.g1_times_cache below g1_lines_counter.
                    auto& it = g1_times_cache_it[i];
                    while (it != machine.g1_times_cache.end() && it->id < g1_lines_counter)
                        ++it;
                    if (it != machine.g1_times_cache.end() && it->id == g1_lines_counter) {
#if ENABLE_EXTENDED_M73_LINES
                        std::pair<int, int> to_export_main = { int(100.0f * it->elapsed_time / machine.time),
                                                               time_in_minutes(machine.time - it->elapsed_time) };
                        if (last_exported_main[i] != to_export_main) {
                            export_line += format_line_M73_main(machine.line_m73_main_mask.c_str(),
                                to_export_main.first, to_export_main.second);
                            last_exported_main[i] = to_export_main;
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
                            ++exported_lines_count;
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
#else
                        float elapsed_time = it->elapsed_time;
                        std::pair<int, int> to_export = { int(100.0f * elapsed_time / machine.time),
                                                          time_in_minutes(machine.time - elapsed_time) };
                        if (last_exported[i] != to_export) {
                            export_line += format_line_M73(machine.line_m73_mask.c_str(),
                                to_export.first, to_export.second);
                            last_exported[i] = to_export;
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
                            ++exported_lines_count;
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
#endif // ENABLE_EXTENDED_M73_LINES
                        }
#if ENABLE_EXTENDED_M73_LINES
                        // export remaining time to next printer stop
                        auto it_stop = std::upper_bound(machine.stop_times.begin(), machine.stop_times.end(), it->elapsed_time,
                            [](float value, const TimeMachine::StopTime& t) { return value < t.elapsed_time; });
                        if (it_stop != machine.stop_times.end()) {
                            int to_export_stop = time_in_minutes(it_stop->elapsed_time - it->elapsed_time);
                            if (last_exported_stop[i] != to_export_stop) {
                                if (to_export_stop > 0) {
                                    if (last_exported_stop[i] != to_export_stop) {
                                        export_line += format_line_M73_stop_int(machine.line_m73_stop_mask.c_str(), to_export_stop);
                                        last_exported_stop[i] = to_export_stop;
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
                                        ++exported_lines_count;
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
                                    }
                                }
                                else {
                                    bool is_last = false;
                                    auto next_it = it + 1;
                                    is_last |= (next_it == machine.g1_times_cache.end());

                                    if (next_it != machine.g1_times_cache.end()) {
                                        auto next_it_stop = std::upper_bound(machine.stop_times.begin(), machine.stop_times.end(), next_it->elapsed_time,
                                            [](float value, const TimeMachine::StopTime& t) { return value < t.elapsed_time; });
                                        is_last |= (next_it_stop != it_stop);

                                        std::string time_float_str = format_time_float(time_in_last_minute(it_stop->elapsed_time - it->elapsed_time));
                                        std::string next_time_float_str = format_time_float(time_in_last_minute(it_stop->elapsed_time - next_it->elapsed_time));
                                        is_last |= (string_to_double_decimal_point(time_float_str) > 0. && string_to_double_decimal_point(next_time_float_str) == 0.);
                                    }

                                    if (is_last) {
                                        if (std::distance(machine.stop_times.begin(), it_stop) == static_cast<ptrdiff_t>(machine.stop_times.size() - 1))
                                            export_line += format_line_M73_stop_int(machine.line_m73_stop_mask.c_str(), to_export_stop);
                                        else
                                            export_line += format_line_M73_stop_float(machine.line_m73_stop_mask.c_str(), time_in_last_minute(it_stop->elapsed_time - it->elapsed_time));

                                        last_exported_stop[i] = to_export_stop;
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
                                        ++exported_lines_count;
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
                                    }
                                }
                            }
                        }
#endif // ENABLE_EXTENDED_M73_LINES
                    }
                }
            }
        }
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        return exported_lines_count;
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    };

    // helper function to write to disk
    auto write_string = [&](const std::string& str) {
        fwrite((const void*)export_line.c_str(), 1, export_line.length(), out);
        if (ferror(out)) {
            in.close();
            fclose(out);
            boost::nowide::remove(out_path.c_str());
            throw Slic3r::RuntimeError(std::string("Time estimator post process export failed.\nIs the disk full?\n"));
        }
        export_line.clear();
    };

#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    unsigned int line_id = 0;
    std::vector<std::pair<unsigned int, unsigned int>> offsets;
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER

    while (std::getline(in, gcode_line)) {
        if (!in.good()) {
            fclose(out);
            throw Slic3r::RuntimeError(std::string("Time estimator post process export failed.\nError while reading from file.\n"));
        }

#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        ++line_id;
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER

        gcode_line += "\n";
        // replace placeholder lines
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        auto [processed, result, lines_added_count] = process_placeholders(gcode_line);
        if (processed && lines_added_count > 0)
            offsets.push_back({ line_id, lines_added_count });
#else
        auto [processed, result] = process_placeholders(gcode_line);
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        gcode_line = result;
        if (!processed) {
            // remove temporary lines
            if (is_temporary_decoration(gcode_line))
                continue;

            // add lines M73 where needed
            parser.parse_line(gcode_line,
                [&](GCodeReader& reader, const GCodeReader::GCodeLine& line) {
                    if (line.cmd_is("G1")) {
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
                        unsigned int extra_lines_count = process_line_G1();
                        ++g1_lines_counter;
                        if (extra_lines_count > 0)
                            offsets.push_back({ line_id, extra_lines_count });
#else
                        process_line_G1();
                        ++g1_lines_counter;
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
                    }
                });
        }

        export_line += gcode_line;
        if (export_line.length() > 65535)
            write_string(export_line);
    }

    if (!export_line.empty())
        write_string(export_line);

    fclose(out);
    in.close();

#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    // updates moves' gcode ids which have been modified by the insertion of the M73 lines
    unsigned int curr_offset_id = 0;
    unsigned int total_offset = 0;
    for (MoveVertex& move : moves) {
        while (curr_offset_id < static_cast<unsigned int>(offsets.size()) && offsets[curr_offset_id].first <= move.gcode_id) {
            total_offset += offsets[curr_offset_id].second;
            ++curr_offset_id;
        }
        move.gcode_id += total_offset;
    }
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER

    if (rename_file(out_path, filename))
        throw Slic3r::RuntimeError(std::string("Failed to rename the output G-code file from ") + out_path + " to " + filename + '\n' +
            "Is " + out_path + " locked?" + '\n');
}

void GCodeProcessor::UsedFilaments::reset()
{
    color_change_cache = 0.0f;
    volumes_per_color_change = std::vector<double>();

    tool_change_cache = 0.0f;
    volumes_per_extruder.clear();

    role_cache = 0.0f;
    filaments_per_role.clear();
}

void GCodeProcessor::UsedFilaments::increase_caches(double extruded_volume)
{
    color_change_cache += extruded_volume;
    tool_change_cache += extruded_volume;
    role_cache += extruded_volume;
}

void GCodeProcessor::UsedFilaments::process_color_change_cache()
{
    if (color_change_cache != 0.0f) {
        volumes_per_color_change.push_back(color_change_cache);
        color_change_cache = 0.0f;
    }
}

void GCodeProcessor::UsedFilaments::process_extruder_cache(GCodeProcessor* processor)
{
    size_t active_extruder_id = processor->m_extruder_id;
    if (tool_change_cache != 0.0f) {
        if (volumes_per_extruder.find(active_extruder_id) != volumes_per_extruder.end())
            volumes_per_extruder[active_extruder_id] += tool_change_cache;
        else
            volumes_per_extruder[active_extruder_id] = tool_change_cache;
        tool_change_cache = 0.0f;
    }
}

void GCodeProcessor::UsedFilaments::process_role_cache(GCodeProcessor* processor)
{
    if (role_cache != 0.0f) {
        std::pair<double, double> filament = { 0.0f, 0.0f };

        double s = PI * sqr(0.5 * processor->m_result.filament_diameters[processor->m_extruder_id]);
        filament.first = role_cache / s * 0.001;
        filament.second = role_cache * processor->m_result.filament_densities[processor->m_extruder_id] * 0.001;

        ExtrusionRole active_role = processor->m_extrusion_role;
        if (filaments_per_role.find(active_role) != filaments_per_role.end()) {
            filaments_per_role[active_role].first += filament.first;
            filaments_per_role[active_role].second += filament.second;
        }
        else
            filaments_per_role[active_role] = filament;
        role_cache = 0.0f;
    }
}

void GCodeProcessor::UsedFilaments::process_caches(GCodeProcessor* processor)
{
    process_color_change_cache();
    process_extruder_cache(processor);
    process_role_cache(processor);
}

#if ENABLE_GCODE_VIEWER_STATISTICS
void GCodeProcessor::Result::reset() {
    moves = std::vector<GCodeProcessor::MoveVertex>();
    bed_shape = Pointfs();
    settings_ids.reset();
    extruders_count = 0;
    extruder_colors = std::vector<std::string>();
    filament_diameters = std::vector<float>(MIN_EXTRUDERS_COUNT, DEFAULT_FILAMENT_DIAMETER);
    filament_densities = std::vector<float>(MIN_EXTRUDERS_COUNT, DEFAULT_FILAMENT_DENSITY);
    time = 0;
}
#else
void GCodeProcessor::Result::reset() {
    moves = std::vector<GCodeProcessor::MoveVertex>();
    bed_shape = Pointfs();
    settings_ids.reset();
    extruders_count = 0;
    extruder_colors = std::vector<std::string>();
    filament_diameters = std::vector<float>(MIN_EXTRUDERS_COUNT, DEFAULT_FILAMENT_DIAMETER);
    filament_densities = std::vector<float>(MIN_EXTRUDERS_COUNT, DEFAULT_FILAMENT_DENSITY);
}
#endif // ENABLE_GCODE_VIEWER_STATISTICS

const std::vector<std::pair<GCodeProcessor::EProducer, std::string>> GCodeProcessor::Producers = {
    { EProducer::PrusaSlicer, "PrusaSlicer" },
    { EProducer::Slic3rPE,    "Slic3r Prusa Edition" },
    { EProducer::Slic3r,      "Slic3r" },
    { EProducer::Cura,        "Cura_SteamEngine" },
    { EProducer::Simplify3D,  "Simplify3D" },
    { EProducer::CraftWare,   "CraftWare" },
    { EProducer::ideaMaker,   "ideaMaker" },
    { EProducer::KissSlicer,  "KISSlicer" }
};

unsigned int GCodeProcessor::s_result_id = 0;

#if ENABLE_VALIDATE_CUSTOM_GCODE
bool GCodeProcessor::contains_reserved_tag(const std::string& gcode, std::string& found_tag)
{
    bool ret = false;

    GCodeReader parser;
    parser.parse_buffer(gcode, [&ret, &found_tag](GCodeReader& parser, const GCodeReader::GCodeLine& line) {
        std::string comment = line.raw();
        if (comment.length() > 2 && comment.front() == ';') {
            comment = comment.substr(1);
            for (const std::string& s : Reserved_Tags) {
                if (boost::starts_with(comment, s)) {
                    ret = true;
                    found_tag = comment;
                    parser.quit_parsing();
                    return;
                }
            }
        }
        });

    return ret;
}

bool GCodeProcessor::contains_reserved_tags(const std::string& gcode, unsigned int max_count, std::vector<std::string>& found_tag)
{
    max_count = std::max(max_count, 1U);

    bool ret = false;

    GCodeReader parser;
    parser.parse_buffer(gcode, [&ret, &found_tag, max_count](GCodeReader& parser, const GCodeReader::GCodeLine& line) {
        std::string comment = line.raw();
        if (comment.length() > 2 && comment.front() == ';') {
            comment = comment.substr(1);
            for (const std::string& s : Reserved_Tags) {
                if (boost::starts_with(comment, s)) {
                    ret = true;
                    found_tag.push_back(comment);
                    if (found_tag.size() == max_count) {
                        parser.quit_parsing();
                        return;
                    }
                }
            }
        }
        });

    return ret;
}
#endif // ENABLE_VALIDATE_CUSTOM_GCODE

GCodeProcessor::GCodeProcessor()
{
    reset();
#if ENABLE_EXTENDED_M73_LINES
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].line_m73_main_mask = "M73 P%s R%s\n";
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].line_m73_stop_mask = "M73 C%s\n";
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].line_m73_main_mask = "M73 Q%s S%s\n";
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].line_m73_stop_mask = "M73 D%s\n";
#else
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].line_m73_mask = "M73 P%s R%s\n";
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].line_m73_mask = "M73 Q%s S%s\n";
#endif // ENABLE_EXTENDED_M73_LINES
}

void GCodeProcessor::apply_config(const PrintConfig& config)
{
    m_parser.apply_config(config);

    m_flavor = config.gcode_flavor;

    size_t extruders_count = config.nozzle_diameter.values.size();
    m_result.extruders_count = extruders_count;

    m_extruder_offsets.resize(extruders_count);
    for (size_t i = 0; i < extruders_count; ++i) {
        Vec2f offset = config.extruder_offset.get_at(i).cast<float>();
        m_extruder_offsets[i] = { offset(0), offset(1), 0.0f };
    }

    m_extruder_colors.resize(extruders_count);
    for (size_t i = 0; i < extruders_count; ++i) {
        m_extruder_colors[i] = static_cast<unsigned char>(i);
    }

    m_extruder_temps.resize(extruders_count);

    m_result.filament_diameters.resize(config.filament_diameter.values.size());
    for (size_t i = 0; i < config.filament_diameter.values.size(); ++i) {
        m_result.filament_diameters[i] = static_cast<float>(config.filament_diameter.values[i]);
    }

    m_result.filament_densities.resize(config.filament_density.values.size());
    for (size_t i = 0; i < config.filament_density.values.size(); ++i) {
        m_result.filament_densities[i] = static_cast<float>(config.filament_density.values[i]);
    }

    if ((m_flavor == gcfMarlinLegacy || m_flavor == gcfMarlinFirmware) && config.machine_limits_usage.value != MachineLimitsUsage::Ignore) {
        m_time_processor.machine_limits = reinterpret_cast<const MachineEnvelopeConfig&>(config);
        if (m_flavor == gcfMarlinLegacy) {
            // Legacy Marlin does not have separate travel acceleration, it uses the 'extruding' value instead.
            m_time_processor.machine_limits.machine_max_acceleration_travel = m_time_processor.machine_limits.machine_max_acceleration_extruding;
        }
    }

    // Filament load / unload times are not specific to a firmware flavor. Let anybody use it if they find it useful.
    // As of now the fields are shown at the UI dialog in the same combo box as the ramming values, so they
    // are considered to be active for the single extruder multi-material printers only.
    m_time_processor.filament_load_times.resize(config.filament_load_time.values.size());
    for (size_t i = 0; i < config.filament_load_time.values.size(); ++i) {
        m_time_processor.filament_load_times[i] = static_cast<float>(config.filament_load_time.values[i]);
    }
    m_time_processor.filament_unload_times.resize(config.filament_unload_time.values.size());
    for (size_t i = 0; i < config.filament_unload_time.values.size(); ++i) {
        m_time_processor.filament_unload_times[i] = static_cast<float>(config.filament_unload_time.values[i]);
    }

    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        float max_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_extruding, i);
        m_time_processor.machines[i].max_acceleration = max_acceleration;
        m_time_processor.machines[i].acceleration = (max_acceleration > 0.0f) ? max_acceleration : DEFAULT_ACCELERATION;
        float max_travel_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_travel, i);
        m_time_processor.machines[i].max_travel_acceleration = max_travel_acceleration;
        m_time_processor.machines[i].travel_acceleration = (max_travel_acceleration > 0.0f) ? max_travel_acceleration : DEFAULT_TRAVEL_ACCELERATION;
    }

    m_time_processor.export_remaining_time_enabled = config.remaining_times.value;
    m_use_volumetric_e = config.use_volumetric_e;

#if ENABLE_START_GCODE_VISUALIZATION
    const ConfigOptionFloatOrPercent* first_layer_height = config.option<ConfigOptionFloatOrPercent>("first_layer_height");
    if (first_layer_height != nullptr)
        m_first_layer_height = std::abs(first_layer_height->value);
#endif // ENABLE_START_GCODE_VISUALIZATION
}

void GCodeProcessor::apply_config(const DynamicPrintConfig& config)
{
    m_parser.apply_config(config);

    const ConfigOptionEnum<GCodeFlavor>* gcode_flavor = config.option<ConfigOptionEnum<GCodeFlavor>>("gcode_flavor");
    if (gcode_flavor != nullptr)
        m_flavor = gcode_flavor->value;

    const ConfigOptionPoints* bed_shape = config.option<ConfigOptionPoints>("bed_shape");
    if (bed_shape != nullptr)
        m_result.bed_shape = bed_shape->values;

    const ConfigOptionString* print_settings_id = config.option<ConfigOptionString>("print_settings_id");
    if (print_settings_id != nullptr)
        m_result.settings_ids.print = print_settings_id->value;

    const ConfigOptionStrings* filament_settings_id = config.option<ConfigOptionStrings>("filament_settings_id");
    if (filament_settings_id != nullptr)
        m_result.settings_ids.filament = filament_settings_id->values;

    const ConfigOptionString* printer_settings_id = config.option<ConfigOptionString>("printer_settings_id");
    if (printer_settings_id != nullptr)
        m_result.settings_ids.printer = printer_settings_id->value;

    m_result.extruders_count = config.option<ConfigOptionFloats>("nozzle_diameter")->values.size();

    const ConfigOptionFloats* filament_diameters = config.option<ConfigOptionFloats>("filament_diameter");
    if (filament_diameters != nullptr) {
        m_result.filament_diameters.clear();
        m_result.filament_diameters.resize(filament_diameters->values.size());
        for (size_t i = 0; i < filament_diameters->values.size(); ++i) {
            m_result.filament_diameters[i] = static_cast<float>(filament_diameters->values[i]);
        }
    }

    if (m_result.filament_diameters.size() < m_result.extruders_count) {
        for (size_t i = m_result.filament_diameters.size(); i < m_result.extruders_count; ++i) {
            m_result.filament_diameters.emplace_back(DEFAULT_FILAMENT_DIAMETER);
        }
    }

    const ConfigOptionFloats* filament_densities = config.option<ConfigOptionFloats>("filament_density");
    if (filament_densities != nullptr) {
        m_result.filament_densities.clear();
        m_result.filament_densities.resize(filament_densities->values.size());
        for (size_t i = 0; i < filament_densities->values.size(); ++i) {
            m_result.filament_densities[i] = static_cast<float>(filament_densities->values[i]);
        }
    }

    if (m_result.filament_densities.size() < m_result.extruders_count) {
        for (size_t i = m_result.filament_densities.size(); i < m_result.extruders_count; ++i) {
            m_result.filament_densities.emplace_back(DEFAULT_FILAMENT_DENSITY);
        }
    }

    const ConfigOptionPoints* extruder_offset = config.option<ConfigOptionPoints>("extruder_offset");
    if (extruder_offset != nullptr) {
        m_extruder_offsets.resize(extruder_offset->values.size());
        for (size_t i = 0; i < extruder_offset->values.size(); ++i) {
            Vec2f offset = extruder_offset->values[i].cast<float>();
            m_extruder_offsets[i] = { offset(0), offset(1), 0.0f };
        }
    }
    
    if (m_extruder_offsets.size() < m_result.extruders_count) {
        for (size_t i = m_extruder_offsets.size(); i < m_result.extruders_count; ++i) {
            m_extruder_offsets.emplace_back(DEFAULT_EXTRUDER_OFFSET);
        }
    }

    const ConfigOptionStrings* extruder_colour = config.option<ConfigOptionStrings>("extruder_colour");
    if (extruder_colour != nullptr) {
        // takes colors from config
        m_result.extruder_colors = extruder_colour->values;
        // try to replace missing values with filament colors
        const ConfigOptionStrings* filament_colour = config.option<ConfigOptionStrings>("filament_colour");
        if (filament_colour != nullptr && filament_colour->values.size() == m_result.extruder_colors.size()) {
            for (size_t i = 0; i < m_result.extruder_colors.size(); ++i) {
                if (m_result.extruder_colors[i].empty())
                    m_result.extruder_colors[i] = filament_colour->values[i];
            }
        }
    }

    if (m_result.extruder_colors.size() < m_result.extruders_count) {
        for (size_t i = m_result.extruder_colors.size(); i < m_result.extruders_count; ++i) {
            m_result.extruder_colors.emplace_back(std::string());
        }
    }

    // replace missing values with default
    std::string default_color = "#FF8000";
    for (size_t i = 0; i < m_result.extruder_colors.size(); ++i) {
        if (m_result.extruder_colors[i].empty())
            m_result.extruder_colors[i] = default_color;
    }

    m_extruder_colors.resize(m_result.extruder_colors.size());
    for (size_t i = 0; i < m_result.extruder_colors.size(); ++i) {
        m_extruder_colors[i] = static_cast<unsigned char>(i);
    }

    m_extruder_temps.resize(m_result.extruders_count);

    const ConfigOptionFloats* filament_load_time = config.option<ConfigOptionFloats>("filament_load_time");
    if (filament_load_time != nullptr) {
        m_time_processor.filament_load_times.resize(filament_load_time->values.size());
        for (size_t i = 0; i < filament_load_time->values.size(); ++i) {
            m_time_processor.filament_load_times[i] = static_cast<float>(filament_load_time->values[i]);
        }
    }

    const ConfigOptionFloats* filament_unload_time = config.option<ConfigOptionFloats>("filament_unload_time");
    if (filament_unload_time != nullptr) {
        m_time_processor.filament_unload_times.resize(filament_unload_time->values.size());
        for (size_t i = 0; i < filament_unload_time->values.size(); ++i) {
            m_time_processor.filament_unload_times[i] = static_cast<float>(filament_unload_time->values[i]);
        }
    }

    if (m_flavor == gcfMarlinLegacy || m_flavor == gcfMarlinFirmware) {
        const ConfigOptionFloats* machine_max_acceleration_x = config.option<ConfigOptionFloats>("machine_max_acceleration_x");
        if (machine_max_acceleration_x != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_x.values = machine_max_acceleration_x->values;

        const ConfigOptionFloats* machine_max_acceleration_y = config.option<ConfigOptionFloats>("machine_max_acceleration_y");
        if (machine_max_acceleration_y != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_y.values = machine_max_acceleration_y->values;

        const ConfigOptionFloats* machine_max_acceleration_z = config.option<ConfigOptionFloats>("machine_max_acceleration_z");
        if (machine_max_acceleration_z != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_z.values = machine_max_acceleration_z->values;

        const ConfigOptionFloats* machine_max_acceleration_e = config.option<ConfigOptionFloats>("machine_max_acceleration_e");
        if (machine_max_acceleration_e != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_e.values = machine_max_acceleration_e->values;

        const ConfigOptionFloats* machine_max_feedrate_x = config.option<ConfigOptionFloats>("machine_max_feedrate_x");
        if (machine_max_feedrate_x != nullptr)
            m_time_processor.machine_limits.machine_max_feedrate_x.values = machine_max_feedrate_x->values;

        const ConfigOptionFloats* machine_max_feedrate_y = config.option<ConfigOptionFloats>("machine_max_feedrate_y");
        if (machine_max_feedrate_y != nullptr)
            m_time_processor.machine_limits.machine_max_feedrate_y.values = machine_max_feedrate_y->values;

        const ConfigOptionFloats* machine_max_feedrate_z = config.option<ConfigOptionFloats>("machine_max_feedrate_z");
        if (machine_max_feedrate_z != nullptr)
            m_time_processor.machine_limits.machine_max_feedrate_z.values = machine_max_feedrate_z->values;

        const ConfigOptionFloats* machine_max_feedrate_e = config.option<ConfigOptionFloats>("machine_max_feedrate_e");
        if (machine_max_feedrate_e != nullptr)
            m_time_processor.machine_limits.machine_max_feedrate_e.values = machine_max_feedrate_e->values;

        const ConfigOptionFloats* machine_max_jerk_x = config.option<ConfigOptionFloats>("machine_max_jerk_x");
        if (machine_max_jerk_x != nullptr)
            m_time_processor.machine_limits.machine_max_jerk_x.values = machine_max_jerk_x->values;

        const ConfigOptionFloats* machine_max_jerk_y = config.option<ConfigOptionFloats>("machine_max_jerk_y");
        if (machine_max_jerk_y != nullptr)
            m_time_processor.machine_limits.machine_max_jerk_y.values = machine_max_jerk_y->values;

        const ConfigOptionFloats* machine_max_jerk_z = config.option<ConfigOptionFloats>("machine_max_jerkz");
        if (machine_max_jerk_z != nullptr)
            m_time_processor.machine_limits.machine_max_jerk_z.values = machine_max_jerk_z->values;

        const ConfigOptionFloats* machine_max_jerk_e = config.option<ConfigOptionFloats>("machine_max_jerk_e");
        if (machine_max_jerk_e != nullptr)
            m_time_processor.machine_limits.machine_max_jerk_e.values = machine_max_jerk_e->values;

        const ConfigOptionFloats* machine_max_acceleration_extruding = config.option<ConfigOptionFloats>("machine_max_acceleration_extruding");
        if (machine_max_acceleration_extruding != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_extruding.values = machine_max_acceleration_extruding->values;

        const ConfigOptionFloats* machine_max_acceleration_retracting = config.option<ConfigOptionFloats>("machine_max_acceleration_retracting");
        if (machine_max_acceleration_retracting != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_retracting.values = machine_max_acceleration_retracting->values;


        // Legacy Marlin does not have separate travel acceleration, it uses the 'extruding' value instead.
        const ConfigOptionFloats* machine_max_acceleration_travel = config.option<ConfigOptionFloats>(m_flavor == gcfMarlinLegacy
                                                                                                    ? "machine_max_acceleration_extruding"
                                                                                                    : "machine_max_acceleration_travel");
        if (machine_max_acceleration_travel != nullptr)
            m_time_processor.machine_limits.machine_max_acceleration_travel.values = machine_max_acceleration_travel->values;


        const ConfigOptionFloats* machine_min_extruding_rate = config.option<ConfigOptionFloats>("machine_min_extruding_rate");
        if (machine_min_extruding_rate != nullptr)
            m_time_processor.machine_limits.machine_min_extruding_rate.values = machine_min_extruding_rate->values;

        const ConfigOptionFloats* machine_min_travel_rate = config.option<ConfigOptionFloats>("machine_min_travel_rate");
        if (machine_min_travel_rate != nullptr)
            m_time_processor.machine_limits.machine_min_travel_rate.values = machine_min_travel_rate->values;
    }

    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        float max_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_extruding, i);
        m_time_processor.machines[i].max_acceleration = max_acceleration;
        m_time_processor.machines[i].acceleration = (max_acceleration > 0.0f) ? max_acceleration : DEFAULT_ACCELERATION;
        float max_travel_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_travel, i);
        m_time_processor.machines[i].max_travel_acceleration = max_travel_acceleration;
        m_time_processor.machines[i].travel_acceleration = (max_travel_acceleration > 0.0f) ? max_travel_acceleration : DEFAULT_TRAVEL_ACCELERATION;
    }

    if (m_time_processor.machine_limits.machine_max_acceleration_x.values.size() > 1)
        enable_stealth_time_estimator(true);

    const ConfigOptionBool* use_volumetric_e = config.option<ConfigOptionBool>("use_volumetric_e");
    if (use_volumetric_e != nullptr)
        m_use_volumetric_e = use_volumetric_e->value;

#if ENABLE_START_GCODE_VISUALIZATION
    const ConfigOptionFloatOrPercent* first_layer_height = config.option<ConfigOptionFloatOrPercent>("first_layer_height");
    if (first_layer_height != nullptr)
        m_first_layer_height = std::abs(first_layer_height->value);
#endif // ENABLE_START_GCODE_VISUALIZATION
}

void GCodeProcessor::enable_stealth_time_estimator(bool enabled)
{
    m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].enabled = enabled;
}

void GCodeProcessor::reset()
{
    m_units = EUnits::Millimeters;
    m_global_positioning_type = EPositioningType::Absolute;
    m_e_local_positioning_type = EPositioningType::Absolute;
    m_extruder_offsets = std::vector<Vec3f>(MIN_EXTRUDERS_COUNT, Vec3f::Zero());
    m_flavor = gcfRepRapSprinter;

    m_start_position = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_end_position = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_origin = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_cached_position.reset();
    m_wiping = false;

#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    m_line_id = 0;
#if ENABLE_SEAMS_VISUALIZATION
    m_last_line_id = 0;
#endif // ENABLE_SEAMS_VISUALIZATION
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    m_feedrate = 0.0f;
    m_width = 0.0f;
    m_height = 0.0f;
    m_forced_width = 0.0f;
    m_forced_height = 0.0f;
    m_mm3_per_mm = 0.0f;
    m_fan_speed = 0.0f;

    m_extrusion_role = erNone;
    m_extruder_id = 0;
    m_extruder_colors.resize(MIN_EXTRUDERS_COUNT);
    for (size_t i = 0; i < MIN_EXTRUDERS_COUNT; ++i) {
        m_extruder_colors[i] = static_cast<unsigned char>(i);
    }
    m_extruder_temps.resize(MIN_EXTRUDERS_COUNT);
    for (size_t i = 0; i < MIN_EXTRUDERS_COUNT; ++i) {
        m_extruder_temps[i] = 0.0f;
    }

    m_extruded_last_z = 0.0f;
#if ENABLE_START_GCODE_VISUALIZATION
    m_first_layer_height = 0.0f;
    m_processing_start_custom_gcode = false;
#endif // ENABLE_START_GCODE_VISUALIZATION
    m_g1_line_id = 0;
    m_layer_id = 0;
    m_cp_color.reset();

    m_producer = EProducer::Unknown;
    m_producers_enabled = false;

    m_time_processor.reset();
    m_used_filaments.reset();

    m_result.reset();
    m_result.id = ++s_result_id;

    m_use_volumetric_e = false;

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    m_mm3_per_mm_compare.reset();
    m_height_compare.reset();
    m_width_compare.reset();
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
}

void GCodeProcessor::process_file(const std::string& filename, bool apply_postprocess, std::function<void()> cancel_callback)
{
    auto last_cancel_callback_time = std::chrono::high_resolution_clock::now();

    CNumericLocalesSetter locales_setter;

#if ENABLE_GCODE_VIEWER_STATISTICS
    auto start_time = std::chrono::high_resolution_clock::now();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // pre-processing
    // parse the gcode file to detect its producer
    if (m_producers_enabled) {
        m_parser.parse_file(filename, [this](GCodeReader& reader, const GCodeReader::GCodeLine& line) {
            const std::string_view cmd = line.cmd();
            if (cmd.length() == 0) {
                const std::string_view comment = line.comment();
                if (comment.length() > 1 && detect_producer(comment))
#if ENABLE_VALIDATE_CUSTOM_GCODE
                    m_parser.quit_parsing();
#else
                    m_parser.quit_parsing_file();
#endif // ENABLE_VALIDATE_CUSTOM_GCODE
            }
            });

        // if the gcode was produced by PrusaSlicer,
        // extract the config from it
        if (m_producer == EProducer::PrusaSlicer || m_producer == EProducer::Slic3rPE || m_producer == EProducer::Slic3r) {
            DynamicPrintConfig config;
            config.apply(FullPrintConfig::defaults());
            // Silently substitute unknown values by new ones for loading configurations from PrusaSlicer's own G-code.
            // Showing substitution log or errors may make sense, but we are not really reading many values from the G-code config,
            // thus a probability of incorrect substitution is low and the G-code viewer is a consumer-only anyways.
            config.load_from_gcode_file(filename, false, ForwardCompatibilitySubstitutionRule::EnableSilent);
            apply_config(config);
        }
        else if (m_producer == EProducer::Simplify3D)
            apply_config_simplify3d(filename);
    }

    // process gcode
#if ENABLE_GCODE_WINDOW
    m_result.filename = filename;
#endif // ENABLE_GCODE_WINDOW
    m_result.id = ++s_result_id;
    // 1st move must be a dummy move
    m_result.moves.emplace_back(MoveVertex());
    m_parser.parse_file(filename, [this, cancel_callback, &last_cancel_callback_time](GCodeReader& reader, const GCodeReader::GCodeLine& line) {
        if (cancel_callback != nullptr) {
            // call the cancel callback every 100 ms
            auto curr_time = std::chrono::high_resolution_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - last_cancel_callback_time).count() > 100) {
                cancel_callback();
                last_cancel_callback_time = curr_time;
            }
        }
        process_gcode_line(line);
        });

    // update width/height of wipe moves
    for (MoveVertex& move : m_result.moves) {
        if (move.type == EMoveType::Wipe) {
            move.width = Wipe_Width;
            move.height = Wipe_Height;
        }
    }

    // process the time blocks
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        TimeMachine::CustomGCodeTime& gcode_time = machine.gcode_time;
        machine.calculate_time();
        if (gcode_time.needed && gcode_time.cache != 0.0f)
            gcode_time.times.push_back({ CustomGCode::ColorChange, gcode_time.cache });
    }

    m_used_filaments.process_caches(this);

    update_estimated_times_stats();

    // post-process to add M73 lines into the gcode
    if (apply_postprocess)
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        m_time_processor.post_process(filename, m_result.moves);
#else
        m_time_processor.post_process(filename);
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    std::cout << "\n";
    m_mm3_per_mm_compare.output();
    m_height_compare.output();
    m_width_compare.output();
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_result.time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
}

float GCodeProcessor::get_time(PrintEstimatedStatistics::ETimeMode mode) const
{
    return (mode < PrintEstimatedStatistics::ETimeMode::Count) ? m_time_processor.machines[static_cast<size_t>(mode)].time : 0.0f;
}

std::string GCodeProcessor::get_time_dhm(PrintEstimatedStatistics::ETimeMode mode) const
{
    return (mode < PrintEstimatedStatistics::ETimeMode::Count) ? short_time(get_time_dhms(m_time_processor.machines[static_cast<size_t>(mode)].time)) : std::string("N/A");
}

std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> GCodeProcessor::get_custom_gcode_times(PrintEstimatedStatistics::ETimeMode mode, bool include_remaining) const
{
    std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> ret;
    if (mode < PrintEstimatedStatistics::ETimeMode::Count) {
        const TimeMachine& machine = m_time_processor.machines[static_cast<size_t>(mode)];
        float total_time = 0.0f;
        for (const auto& [type, time] : machine.gcode_time.times) {
            float remaining = include_remaining ? machine.time - total_time : 0.0f;
            ret.push_back({ type, { time, remaining } });
            total_time += time;
        }
    }
    return ret;
}

std::vector<std::pair<EMoveType, float>> GCodeProcessor::get_moves_time(PrintEstimatedStatistics::ETimeMode mode) const
{
    std::vector<std::pair<EMoveType, float>> ret;
    if (mode < PrintEstimatedStatistics::ETimeMode::Count) {
        for (size_t i = 0; i < m_time_processor.machines[static_cast<size_t>(mode)].moves_time.size(); ++i) {
            float time = m_time_processor.machines[static_cast<size_t>(mode)].moves_time[i];
            if (time > 0.0f)
                ret.push_back({ static_cast<EMoveType>(i), time });
        }
    }
    return ret;
}

std::vector<std::pair<ExtrusionRole, float>> GCodeProcessor::get_roles_time(PrintEstimatedStatistics::ETimeMode mode) const
{
    std::vector<std::pair<ExtrusionRole, float>> ret;
    if (mode < PrintEstimatedStatistics::ETimeMode::Count) {
        for (size_t i = 0; i < m_time_processor.machines[static_cast<size_t>(mode)].roles_time.size(); ++i) {
            float time = m_time_processor.machines[static_cast<size_t>(mode)].roles_time[i];
            if (time > 0.0f)
                ret.push_back({ static_cast<ExtrusionRole>(i), time });
        }
    }
    return ret;
}

std::vector<float> GCodeProcessor::get_layers_time(PrintEstimatedStatistics::ETimeMode mode) const
{
    return (mode < PrintEstimatedStatistics::ETimeMode::Count) ?
        m_time_processor.machines[static_cast<size_t>(mode)].layers_time :
        std::vector<float>();
}

void GCodeProcessor::apply_config_simplify3d(const std::string& filename)
{
    struct BedSize
    {
        double x{ 0.0 };
        double y{ 0.0 };

        bool is_defined() const { return x > 0.0 && y > 0.0; }
    };

    BedSize bed_size;

    m_parser.parse_file(filename, [this, &bed_size](GCodeReader& reader, const GCodeReader::GCodeLine& line) {
        auto extract_double = [](const std::string& cmt, const std::string& key, double& out) {
            size_t pos = cmt.find(key);
            if (pos != cmt.npos) {
                pos = cmt.find(',', pos);
                if (pos != cmt.npos) {
                    out = string_to_double_decimal_point(cmt.substr(pos+1));
                    return true;
                }
            }
            return false;
        };

        auto extract_floats = [](const std::string& cmt, const std::string& key, std::vector<float>& out) {
            size_t pos = cmt.find(key);
            if (pos != cmt.npos) {
                pos = cmt.find(',', pos);
                if (pos != cmt.npos) {
                    std::string data_str = cmt.substr(pos + 1);
                    std::vector<std::string> values_str;
                    boost::split(values_str, data_str, boost::is_any_of("|"), boost::token_compress_on);
                    for (const std::string& s : values_str) {
                        out.emplace_back(static_cast<float>(string_to_double_decimal_point(s)));
                    }
                    return true;
                }
            }
            return false;
        };

        const std::string& comment = line.raw();
        if (comment.length() > 2 && comment.front() == ';') {
            if (bed_size.x == 0.0 && comment.find("strokeXoverride") != comment.npos)
                extract_double(comment, "strokeXoverride", bed_size.x);
            else if (bed_size.y == 0.0 && comment.find("strokeYoverride") != comment.npos)
                extract_double(comment, "strokeYoverride", bed_size.y);
            else if (comment.find("filamentDiameters") != comment.npos) {
                m_result.filament_diameters.clear();
                extract_floats(comment, "filamentDiameters", m_result.filament_diameters);
            }
            else if (comment.find("filamentDensities") != comment.npos) {
                m_result.filament_densities.clear();
                extract_floats(comment, "filamentDensities", m_result.filament_densities);
            }
        }
        });

    m_result.extruders_count = std::max<size_t>(1, std::min(m_result.filament_diameters.size(), m_result.filament_densities.size()));

    if (bed_size.is_defined()) {
        m_result.bed_shape = {
            { 0.0, 0.0 },
            { bed_size.x, 0.0 },
            { bed_size.x, bed_size.y },
            { 0.0, bed_size.y }
        };
    }
}

void GCodeProcessor::process_gcode_line(const GCodeReader::GCodeLine& line)
{
/* std::cout << line.raw() << std::endl; */

#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
    ++m_line_id;
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER

    // update start position
    m_start_position = m_end_position;

    const std::string_view cmd = line.cmd();
    if (cmd.length() > 1) {
        // process command lines
        switch (::toupper(cmd[0]))
        {
        case 'G':
            {
                switch (::atoi(&cmd[1]))
                {
                case 0:  { process_G0(line); break; }  // Move
                case 1:  { process_G1(line); break; }  // Move
                case 10: { process_G10(line); break; } // Retract
                case 11: { process_G11(line); break; } // Unretract
                case 20: { process_G20(line); break; } // Set Units to Inches
                case 21: { process_G21(line); break; } // Set Units to Millimeters
                case 22: { process_G22(line); break; } // Firmware controlled retract
                case 23: { process_G23(line); break; } // Firmware controlled unretract
                case 28: { process_G28(line); break; } // Move to origin
                case 90: { process_G90(line); break; } // Set to Absolute Positioning
                case 91: { process_G91(line); break; } // Set to Relative Positioning
                case 92: { process_G92(line); break; } // Set Position
                default: { break; }
                }
                break;
            }
        case 'M':
            {
                switch (::atoi(&cmd[1]))
                {
                case 1:   { process_M1(line); break; }   // Sleep or Conditional stop
                case 82:  { process_M82(line); break; }  // Set extruder to absolute mode
                case 83:  { process_M83(line); break; }  // Set extruder to relative mode
                case 104: { process_M104(line); break; } // Set extruder temperature
                case 106: { process_M106(line); break; } // Set fan speed
                case 107: { process_M107(line); break; } // Disable fan
                case 108: { process_M108(line); break; } // Set tool (Sailfish)
                case 109: { process_M109(line); break; } // Set extruder temperature and wait
                case 132: { process_M132(line); break; } // Recall stored home offsets
                case 135: { process_M135(line); break; } // Set tool (MakerWare)
                case 201: { process_M201(line); break; } // Set max printing acceleration
                case 203: { process_M203(line); break; } // Set maximum feedrate
                case 204: { process_M204(line); break; } // Set default acceleration
                case 205: { process_M205(line); break; } // Advanced settings
                case 221: { process_M221(line); break; } // Set extrude factor override percentage
                case 401: { process_M401(line); break; } // Repetier: Store x, y and z position
                case 402: { process_M402(line); break; } // Repetier: Go to stored position
                case 566: { process_M566(line); break; } // Set allowable instantaneous speed change
                case 702: { process_M702(line); break; } // Unload the current filament into the MK3 MMU2 unit at the end of print.
                default: { break; }
                }
                break;
            }
        case 'T':
            {
                process_T(line); // Select Tool
                break;
            }
        default: { break; }
        }
    }
    else {
        const std::string &comment = line.raw();
        if (comment.length() > 2 && comment.front() == ';')
            // Process tags embedded into comments. Tag comments always start at the start of a line
            // with a comment and continue with a tag without any whitespace separator.
            process_tags(comment.substr(1));
    }
}

#if __has_include(<charconv>)
    template <typename T, typename = void>
    struct is_from_chars_convertible : std::false_type {};
    template <typename T>
    struct is_from_chars_convertible<T, std::void_t<decltype(std::from_chars(std::declval<const char*>(), std::declval<const char*>(), std::declval<T&>()))>> : std::true_type {};
#endif

// Returns true if the number was parsed correctly into out and the number spanned the whole input string.
template<typename T>
[[nodiscard]] static inline bool parse_number(const std::string_view sv, T &out)
{
    // https://www.bfilipek.com/2019/07/detect-overload-from-chars.html#example-stdfromchars
#if __has_include(<charconv>)
    // Visual Studio 19 supports from_chars all right.
    // OSX compiler that we use only implements std::from_chars just for ints.
    // GCC that we compile on does not provide <charconv> at all.
    if constexpr (is_from_chars_convertible<T>::value) {
        auto str_end = sv.data() + sv.size();
        auto [end_ptr, error_code] = std::from_chars(sv.data(), str_end, out);
        return error_code == std::errc() && end_ptr == str_end;
    } 
    else
#endif
    {
        // Legacy conversion, which is costly due to having to make a copy of the string before conversion.
        try {
            assert(sv.size() < 1024);
	    assert(sv.data() != nullptr);
            std::string str { sv };
            size_t read = 0;
            if constexpr (std::is_same_v<T, int>)
                out = std::stoi(str, &read);
            else if constexpr (std::is_same_v<T, long>)
                out = std::stol(str, &read);
            else if constexpr (std::is_same_v<T, float>)
                out = string_to_double_decimal_point(str, &read);
            else if constexpr (std::is_same_v<T, double>)
                out = string_to_double_decimal_point(str, &read);
            return str.size() == read;
        } catch (...) {
            return false;
        }
    }
}

void GCodeProcessor::process_tags(const std::string_view comment)
{
    // producers tags
    if (m_producers_enabled && process_producers_tags(comment))
        return;

#if ENABLE_VALIDATE_CUSTOM_GCODE
    // extrusion role tag
    if (boost::starts_with(comment, reserved_tag(ETags::Role))) {
        set_extrusion_role(ExtrusionEntity::string_to_role(comment.substr(reserved_tag(ETags::Role).length())));
#if ENABLE_SEAMS_VISUALIZATION
        if (m_extrusion_role == erExternalPerimeter)
            m_seams_detector.activate(true);
#endif // ENABLE_SEAMS_VISUALIZATION
#if ENABLE_START_GCODE_VISUALIZATION
        m_processing_start_custom_gcode = (m_extrusion_role == erCustom && m_g1_line_id == 0);
#endif // ENABLE_START_GCODE_VISUALIZATION
        return;
    }

    // wipe start tag
    if (boost::starts_with(comment, reserved_tag(ETags::Wipe_Start))) {
        m_wiping = true;
        return;
    }

    // wipe end tag
    if (boost::starts_with(comment, reserved_tag(ETags::Wipe_End))) {
        m_wiping = false;
        return;
    }
#else
    // extrusion role tag
    if (boost::starts_with(comment, Extrusion_Role_Tag)) {
        set_extrusion_role(ExtrusionEntity::string_to_role(comment.substr(Extrusion_Role_Tag.length())));
        return;
    }

    // wipe start tag
    if (boost::starts_with(comment, Wipe_Start_Tag)) {
        m_wiping = true;
        return;
    }

    // wipe end tag
    if (boost::starts_with(comment, Wipe_End_Tag)) {
        m_wiping = false;
        return;
    }
#endif // ENABLE_VALIDATE_CUSTOM_GCODE

    if (!m_producers_enabled || m_producer == EProducer::PrusaSlicer) {
#if ENABLE_VALIDATE_CUSTOM_GCODE
        // height tag
        if (boost::starts_with(comment, reserved_tag(ETags::Height))) {
            if (!parse_number(comment.substr(reserved_tag(ETags::Height).size()), m_forced_height))
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Height (" << comment << ").";
            return;
        }
        // width tag
        if (boost::starts_with(comment, reserved_tag(ETags::Width))) {
            if (!parse_number(comment.substr(reserved_tag(ETags::Width).size()), m_forced_width))
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Width (" << comment << ").";
            return;
        }
#else
        // height tag
        if (boost::starts_with(comment, Height_Tag)) {
            if (!parse_number(comment.substr(Height_Tag.size()), m_forced_height))
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Height (" << comment << ").";
            return;
        }
        // width tag
        if (boost::starts_with(comment, Width_Tag)) {
            if (!parse_number(comment.substr(Width_Tag.size()), m_forced_width))
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Width (" << comment << ").";
            return;
        }
#endif // ENABLE_VALIDATE_CUSTOM_GCODE
    }

#if ENABLE_VALIDATE_CUSTOM_GCODE
    // color change tag
    if (boost::starts_with(comment, reserved_tag(ETags::Color_Change))) {
        unsigned char extruder_id = 0;
        if (boost::starts_with(comment.substr(reserved_tag(ETags::Color_Change).size()), ",T")) {
            int eid;
            if (!parse_number(comment.substr(reserved_tag(ETags::Color_Change).size() + 2), eid) || eid < 0 || eid > 255) {
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Color_Change (" << comment << ").";
                return;
            }
            extruder_id = static_cast<unsigned char>(eid);
        }

        if (extruder_id < m_extruder_colors.size())
            m_extruder_colors[extruder_id] = static_cast<unsigned char>(m_extruder_offsets.size()) + m_cp_color.counter; // color_change position in list of color for preview
        ++m_cp_color.counter;
        if (m_cp_color.counter == UCHAR_MAX)
            m_cp_color.counter = 0;

        if (m_extruder_id == extruder_id) {
            m_cp_color.current = m_extruder_colors[extruder_id];
            store_move_vertex(EMoveType::Color_change);
        }

        process_custom_gcode_time(CustomGCode::ColorChange);
        process_filaments(CustomGCode::ColorChange);

        return;
    }

    // pause print tag
    if (comment == reserved_tag(ETags::Pause_Print)) {
        store_move_vertex(EMoveType::Pause_Print);
        process_custom_gcode_time(CustomGCode::PausePrint);
        return;
    }

    // custom code tag
    if (comment == reserved_tag(ETags::Custom_Code)) {
        store_move_vertex(EMoveType::Custom_GCode);
        return;
    }

    // layer change tag
    if (comment == reserved_tag(ETags::Layer_Change)) {
        ++m_layer_id;
        return;
    }
#else
    // color change tag
    if (boost::starts_with(comment, Color_Change_Tag)) {
        unsigned char extruder_id = 0;
        if (boost::starts_with(comment.substr(Color_Change_Tag.size()), ",T")) {
            int eid;
            if (! parse_number(comment.substr(Color_Change_Tag.size() + 2), eid) || eid < 0 || eid > 255) {
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Color_Change (" << comment << ").";
                return;
            }
            extruder_id = static_cast<unsigned char>(eid);
        }

        m_extruder_colors[extruder_id] = static_cast<unsigned char>(m_extruder_offsets.size()) + m_cp_color.counter; // color_change position in list of color for preview
        ++m_cp_color.counter;
        if (m_cp_color.counter == UCHAR_MAX)
            m_cp_color.counter = 0;

        if (m_extruder_id == extruder_id) {
            m_cp_color.current = m_extruder_colors[extruder_id];
            store_move_vertex(EMoveType::Color_change);
        }

        process_custom_gcode_time(CustomGCode::ColorChange);

        return;
    }

    // pause print tag
    if (comment == Pause_Print_Tag) {
        store_move_vertex(EMoveType::Pause_Print);
        process_custom_gcode_time(CustomGCode::PausePrint);
        return;
    }

    // custom code tag
    if (comment == Custom_Code_Tag) {
        store_move_vertex(EMoveType::Custom_GCode);
        return;
    }

    // layer change tag
    if (comment == Layer_Change_Tag) {
        ++m_layer_id;
        return;
    }
#endif // ENABLE_VALIDATE_CUSTOM_GCODE

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
    // mm3_per_mm print tag
    if (boost::starts_with(comment, Mm3_Per_Mm_Tag)) {
        if (! parse_number(comment.substr(Mm3_Per_Mm_Tag.size()), m_mm3_per_mm_compare.last_tag_value))
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Mm3_Per_Mm (" << comment << ").";
        return;
    }
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
}

bool GCodeProcessor::process_producers_tags(const std::string_view comment)
{
    switch (m_producer)
    {
    case EProducer::Slic3rPE:
    case EProducer::Slic3r: 
    case EProducer::PrusaSlicer: { return process_prusaslicer_tags(comment); }
    case EProducer::Cura:        { return process_cura_tags(comment); }
    case EProducer::Simplify3D:  { return process_simplify3d_tags(comment); }
    case EProducer::CraftWare:   { return process_craftware_tags(comment); }
    case EProducer::ideaMaker:   { return process_ideamaker_tags(comment); }
    case EProducer::KissSlicer:  { return process_kissslicer_tags(comment); }
    default:                     { return false; }
    }
}

bool GCodeProcessor::process_prusaslicer_tags(const std::string_view comment)
{
    return false;
}

bool GCodeProcessor::process_cura_tags(const std::string_view comment)
{
    // TYPE -> extrusion role
    std::string tag = "TYPE:";
    size_t pos = comment.find(tag);
    if (pos != comment.npos) {
        const std::string_view type = comment.substr(pos + tag.length());
        if (type == "SKIRT")
            set_extrusion_role(erSkirt);
        else if (type == "WALL-OUTER")
            set_extrusion_role(erExternalPerimeter);
        else if (type == "WALL-INNER")
            set_extrusion_role(erPerimeter);
        else if (type == "SKIN")
            set_extrusion_role(erSolidInfill);
        else if (type == "FILL")
            set_extrusion_role(erInternalInfill);
        else if (type == "SUPPORT")
            set_extrusion_role(erSupportMaterial);
        else if (type == "SUPPORT-INTERFACE")
            set_extrusion_role(erSupportMaterialInterface);
        else if (type == "PRIME-TOWER")
            set_extrusion_role(erWipeTower);
        else {
            set_extrusion_role(erNone);
            BOOST_LOG_TRIVIAL(warning) << "GCodeProcessor found unknown extrusion role: " << type;
        }

#if ENABLE_SEAMS_VISUALIZATION
        if (m_extrusion_role == erExternalPerimeter)
            m_seams_detector.activate(true);
#endif // ENABLE_SEAMS_VISUALIZATION
        return true;
    }

    // flavor
    tag = "FLAVOR:";
    pos = comment.find(tag);
    if (pos != comment.npos) {
        const std::string_view flavor = comment.substr(pos + tag.length());
        if (flavor == "BFB")
            m_flavor = gcfMarlinLegacy; // is this correct ?
        else if (flavor == "Mach3")
            m_flavor = gcfMach3;
        else if (flavor == "Makerbot")
            m_flavor = gcfMakerWare;
        else if (flavor == "UltiGCode")
            m_flavor = gcfMarlinLegacy; // is this correct ?
        else if (flavor == "Marlin(Volumetric)")
            m_flavor = gcfMarlinLegacy; // is this correct ?
        else if (flavor == "Griffin")
            m_flavor = gcfMarlinLegacy; // is this correct ?
        else if (flavor == "Repetier")
            m_flavor = gcfRepetier;
        else if (flavor == "RepRap")
            m_flavor = gcfRepRapFirmware;
        else if (flavor == "Marlin")
            m_flavor = gcfMarlinLegacy;
        else
            BOOST_LOG_TRIVIAL(warning) << "GCodeProcessor found unknown flavor: " << flavor;

        return true;
    }

    // layer
    tag = "LAYER:";
    pos = comment.find(tag);
    if (pos != comment.npos) {
        ++m_layer_id;
        return true;
    }

    return false;
}

bool GCodeProcessor::process_simplify3d_tags(const std::string_view comment)
{
    // extrusion roles

    // in older versions the comments did not contain the key 'feature'
    std::string_view cmt = comment;
    size_t pos = cmt.find(" feature");
    if (pos == 0)
        cmt.remove_prefix(8);

    // ; skirt
    pos = cmt.find(" skirt");
    if (pos == 0) {
        set_extrusion_role(erSkirt);
        return true;
    }
    
    // ; outer perimeter
    pos = cmt.find(" outer perimeter");
    if (pos == 0) {
        set_extrusion_role(erExternalPerimeter);
#if ENABLE_SEAMS_VISUALIZATION
        m_seams_detector.activate(true);
#endif // ENABLE_SEAMS_VISUALIZATION
        return true;
    }

    // ; inner perimeter
    pos = cmt.find(" inner perimeter");
    if (pos == 0) {
        set_extrusion_role(erPerimeter);
        return true;
    }

    // ; gap fill
    pos = cmt.find(" gap fill");
    if (pos == 0) {
        set_extrusion_role(erGapFill);
        return true;
    }

    // ; infill
    pos = cmt.find(" infill");
    if (pos == 0) {
        set_extrusion_role(erInternalInfill);
        return true;
    }

    // ; solid layer
    pos = cmt.find(" solid layer");
    if (pos == 0) {
        set_extrusion_role(erSolidInfill);
        return true;
    }

    // ; bridge
    pos = cmt.find(" bridge");
    if (pos == 0) {
        set_extrusion_role(erBridgeInfill);
        return true;
    }

    // ; support
    pos = cmt.find(" support");
    if (pos == 0) {
        set_extrusion_role(erSupportMaterial);
        return true;
    }

    // ; dense support
    pos = cmt.find(" dense support");
    if (pos == 0) {
        set_extrusion_role(erSupportMaterialInterface);
        return true;
    }

    // ; prime pillar
    pos = cmt.find(" prime pillar");
    if (pos == 0) {
        set_extrusion_role(erWipeTower);
        return true;
    }

    // ; ooze shield
    pos = cmt.find(" ooze shield");
    if (pos == 0) {
        set_extrusion_role(erNone); // Missing mapping
        return true;
    }

    // ; raft
    pos = cmt.find(" raft");
    if (pos == 0) {
        set_extrusion_role(erSupportMaterial);
        return true;
    }

    // ; internal single extrusion
    pos = cmt.find(" internal single extrusion");
    if (pos == 0) {
        set_extrusion_role(erNone); // Missing mapping
        return true;
    }

    // geometry
    // ; tool
    std::string tag = " tool";
    pos = cmt.find(tag);
    if (pos == 0) {
        const std::string_view data = cmt.substr(pos + tag.length());
        std::string h_tag = "H";
        size_t h_start = data.find(h_tag);
        size_t h_end = data.find_first_of(' ', h_start);
        std::string w_tag = "W";
        size_t w_start = data.find(w_tag);
        size_t w_end = data.find_first_of(' ', w_start);
        if (h_start != data.npos) {
            if (!parse_number(data.substr(h_start + 1, (h_end != data.npos) ? h_end - h_start - 1 : h_end), m_forced_height))
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Height (" << comment << ").";
        }
        if (w_start != data.npos) {
            if (!parse_number(data.substr(w_start + 1, (w_end != data.npos) ? w_end - w_start - 1 : w_end), m_forced_width))
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Width (" << comment << ").";
        }

        return true;
    }

    // ; layer
    tag = " layer";
    pos = cmt.find(tag);
    if (pos == 0) {
        // skip lines "; layer end"
        const std::string_view data = cmt.substr(pos + tag.length());
        size_t end_start = data.find("end");
        if (end_start == data.npos)
            ++m_layer_id;

        return true;
    }

    return false;
}

bool GCodeProcessor::process_craftware_tags(const std::string_view comment)
{
    // segType -> extrusion role
    std::string tag = "segType:";
    size_t pos = comment.find(tag);
    if (pos != comment.npos) {
        const std::string_view type = comment.substr(pos + tag.length());
        if (type == "Skirt")
            set_extrusion_role(erSkirt);
        else if (type == "Perimeter")
            set_extrusion_role(erExternalPerimeter);
        else if (type == "HShell")
            set_extrusion_role(erNone); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        else if (type == "InnerHair")
            set_extrusion_role(erNone); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        else if (type == "Loop")
            set_extrusion_role(erNone); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        else if (type == "Infill")
            set_extrusion_role(erInternalInfill);
        else if (type == "Raft")
            set_extrusion_role(erSkirt);
        else if (type == "Support")
            set_extrusion_role(erSupportMaterial);
        else if (type == "SupportTouch")
            set_extrusion_role(erSupportMaterial);
        else if (type == "SoftSupport")
            set_extrusion_role(erSupportMaterialInterface);
        else if (type == "Pillar")
            set_extrusion_role(erWipeTower);
        else {
            set_extrusion_role(erNone);
            BOOST_LOG_TRIVIAL(warning) << "GCodeProcessor found unknown extrusion role: " << type;
        }

#if ENABLE_SEAMS_VISUALIZATION
        if (m_extrusion_role == erExternalPerimeter)
            m_seams_detector.activate(true);
#endif // ENABLE_SEAMS_VISUALIZATION

        return true;
    }

    // layer
    pos = comment.find(" Layer #");
    if (pos == 0) {
        ++m_layer_id;
        return true;
    }

    return false;
}

bool GCodeProcessor::process_ideamaker_tags(const std::string_view comment)
{
    // TYPE -> extrusion role
    std::string tag = "TYPE:";
    size_t pos = comment.find(tag);
    if (pos != comment.npos) {
        const std::string_view type = comment.substr(pos + tag.length());
        if (type == "RAFT")
            set_extrusion_role(erSkirt);
        else if (type == "WALL-OUTER")
            set_extrusion_role(erExternalPerimeter);
        else if (type == "WALL-INNER")
            set_extrusion_role(erPerimeter);
        else if (type == "SOLID-FILL")
            set_extrusion_role(erSolidInfill);
        else if (type == "FILL")
            set_extrusion_role(erInternalInfill);
        else if (type == "BRIDGE")
            set_extrusion_role(erBridgeInfill);
        else if (type == "SUPPORT")
            set_extrusion_role(erSupportMaterial);
        else {
            set_extrusion_role(erNone);
            BOOST_LOG_TRIVIAL(warning) << "GCodeProcessor found unknown extrusion role: " << type;
        }

#if ENABLE_SEAMS_VISUALIZATION
        if (m_extrusion_role == erExternalPerimeter)
            m_seams_detector.activate(true);
#endif // ENABLE_SEAMS_VISUALIZATION
        return true;
    }

    // geometry
    // width
    tag = "WIDTH:";
    pos = comment.find(tag);
    if (pos != comment.npos) {
        if (!parse_number(comment.substr(pos + tag.length()), m_forced_width))
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Width (" << comment << ").";
        return true;
    }

    // height
    tag = "HEIGHT:";
    pos = comment.find(tag);
    if (pos != comment.npos) {
        if (!parse_number(comment.substr(pos + tag.length()), m_forced_height))
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Height (" << comment << ").";
        return true;
    }

    // layer
    pos = comment.find("LAYER:");
    if (pos == 0) {
        ++m_layer_id;
        return true;
    }

    return false;
}

bool GCodeProcessor::process_kissslicer_tags(const std::string_view comment)
{
    // extrusion roles

    // ; 'Raft Path'
    size_t pos = comment.find(" 'Raft Path'");
    if (pos == 0) {
        set_extrusion_role(erSkirt);
        return true;
    }

    // ; 'Support Interface Path'
    pos = comment.find(" 'Support Interface Path'");
    if (pos == 0) {
        set_extrusion_role(erSupportMaterialInterface);
        return true;
    }

    // ; 'Travel/Ironing Path'
    pos = comment.find(" 'Travel/Ironing Path'");
    if (pos == 0) {
        set_extrusion_role(erIroning);
        return true;
    }

    // ; 'Support (may Stack) Path'
    pos = comment.find(" 'Support (may Stack) Path'");
    if (pos == 0) {
        set_extrusion_role(erSupportMaterial);
        return true;
    }

    // ; 'Perimeter Path'
    pos = comment.find(" 'Perimeter Path'");
    if (pos == 0) {
        set_extrusion_role(erExternalPerimeter);
#if ENABLE_SEAMS_VISUALIZATION
        m_seams_detector.activate(true);
#endif // ENABLE_SEAMS_VISUALIZATION
        return true;
    }

    // ; 'Pillar Path'
    pos = comment.find(" 'Pillar Path'");
    if (pos == 0) {
        set_extrusion_role(erNone); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; 'Destring/Wipe/Jump Path'
    pos = comment.find(" 'Destring/Wipe/Jump Path'");
    if (pos == 0) {
        set_extrusion_role(erNone); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; 'Prime Pillar Path'
    pos = comment.find(" 'Prime Pillar Path'");
    if (pos == 0) {
        set_extrusion_role(erNone); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; 'Loop Path'
    pos = comment.find(" 'Loop Path'");
    if (pos == 0) {
        set_extrusion_role(erNone); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; 'Crown Path'
    pos = comment.find(" 'Crown Path'");
    if (pos == 0) {
        set_extrusion_role(erNone); // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; 'Solid Path'
    pos = comment.find(" 'Solid Path'");
    if (pos == 0) {
        set_extrusion_role(erNone);
        return true;
    }

    // ; 'Stacked Sparse Infill Path'
    pos = comment.find(" 'Stacked Sparse Infill Path'");
    if (pos == 0) {
        set_extrusion_role(erInternalInfill);
        return true;
    }

    // ; 'Sparse Infill Path'
    pos = comment.find(" 'Sparse Infill Path'");
    if (pos == 0) {
        set_extrusion_role(erSolidInfill);
        return true;
    }

    // geometry

    // layer
    pos = comment.find(" BEGIN_LAYER_");
    if (pos == 0) {
        ++m_layer_id;
        return true;
    }

    return false;
}

bool GCodeProcessor::detect_producer(const std::string_view comment)
{
    for (const auto& [id, search_string] : Producers) {
        size_t pos = comment.find(search_string);
        if (pos != comment.npos) {
            m_producer = id;
            BOOST_LOG_TRIVIAL(info) << "Detected gcode producer: " << search_string;
            return true;
        }
    }
    return false;
}

void GCodeProcessor::process_G0(const GCodeReader::GCodeLine& line)
{
    process_G1(line);
}

void GCodeProcessor::process_G1(const GCodeReader::GCodeLine& line)
{
    float filament_diameter = (static_cast<size_t>(m_extruder_id) < m_result.filament_diameters.size()) ? m_result.filament_diameters[m_extruder_id] : m_result.filament_diameters.back();
    float filament_radius = 0.5f * filament_diameter;
    float area_filament_cross_section = static_cast<float>(M_PI) * sqr(filament_radius);
    auto absolute_position = [this, area_filament_cross_section](Axis axis, const GCodeReader::GCodeLine& lineG1) {
        bool is_relative = (m_global_positioning_type == EPositioningType::Relative);
        if (axis == E)
            is_relative |= (m_e_local_positioning_type == EPositioningType::Relative);

        if (lineG1.has(Slic3r::Axis(axis))) {
            float lengthsScaleFactor = (m_units == EUnits::Inches) ? INCHES_TO_MM : 1.0f;
            float ret = lineG1.value(Slic3r::Axis(axis)) * lengthsScaleFactor;
            if (axis == E && m_use_volumetric_e)
                ret /= area_filament_cross_section;
            return is_relative ? m_start_position[axis] + ret : m_origin[axis] + ret;
        }
        else
            return m_start_position[axis];
    };

    auto move_type = [this](const AxisCoords& delta_pos) {
        EMoveType type = EMoveType::Noop;

        if (m_wiping)
            type = EMoveType::Wipe;
        else if (delta_pos[E] < 0.0f)
            type = (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f || delta_pos[Z] != 0.0f) ? EMoveType::Travel : EMoveType::Retract;
        else if (delta_pos[E] > 0.0f) {
            if (delta_pos[X] == 0.0f && delta_pos[Y] == 0.0f)
                type = (delta_pos[Z] == 0.0f) ? EMoveType::Unretract : EMoveType::Travel;
            else if (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f)
                type = EMoveType::Extrude;
        } 
        else if (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f || delta_pos[Z] != 0.0f)
            type = EMoveType::Travel;

        return type;
    };

    ++m_g1_line_id;

    // enable processing of lines M201/M203/M204/M205
    m_time_processor.machine_envelope_processing_enabled = true;

    // updates axes positions from line
    for (unsigned char a = X; a <= E; ++a) {
        m_end_position[a] = absolute_position((Axis)a, line);
    }

    // updates feedrate from line, if present
    if (line.has_f())
        m_feedrate = line.f() * MMMIN_TO_MMSEC;

    // calculates movement deltas
    float max_abs_delta = 0.0f;
    AxisCoords delta_pos;
    for (unsigned char a = X; a <= E; ++a) {
        delta_pos[a] = m_end_position[a] - m_start_position[a];
        max_abs_delta = std::max(max_abs_delta, std::abs(delta_pos[a]));
    }

    // no displacement, return
    if (max_abs_delta == 0.0f)
        return;

    EMoveType type = move_type(delta_pos);
    if (type == EMoveType::Extrude && m_end_position[Z] == 0.0f)
        type = EMoveType::Travel;

    if (type == EMoveType::Extrude) {
        float delta_xyz = std::sqrt(sqr(delta_pos[X]) + sqr(delta_pos[Y]) + sqr(delta_pos[Z]));
        float volume_extruded_filament = area_filament_cross_section * delta_pos[E];
        float area_toolpath_cross_section = volume_extruded_filament / delta_xyz;

        // save extruded volume to the cache
        m_used_filaments.increase_caches(volume_extruded_filament);

        // volume extruded filament / tool displacement = area toolpath cross section
        m_mm3_per_mm = area_toolpath_cross_section;
#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_mm3_per_mm_compare.update(area_toolpath_cross_section, m_extrusion_role);
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

        if (m_forced_height > 0.0f)
            m_height = m_forced_height;
        else {
            if (m_end_position[Z] > m_extruded_last_z + EPSILON) {
                m_height = m_end_position[Z] - m_extruded_last_z;
                m_extruded_last_z = m_end_position[Z];
            }
        }

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_height_compare.update(m_height, m_extrusion_role);
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

        if (m_forced_width > 0.0f)
            m_width = m_forced_width;
        else if (m_extrusion_role == erExternalPerimeter)
            // cross section: rectangle
            m_width = delta_pos[E] * static_cast<float>(M_PI * sqr(1.05f * filament_radius)) / (delta_xyz * m_height);
        else if (m_extrusion_role == erBridgeInfill || m_extrusion_role == erNone)
            // cross section: circle
            m_width = static_cast<float>(m_result.filament_diameters[m_extruder_id]) * std::sqrt(delta_pos[E] / delta_xyz);
        else
            // cross section: rectangle + 2 semicircles
            m_width = delta_pos[E] * static_cast<float>(M_PI * sqr(filament_radius)) / (delta_xyz * m_height) + static_cast<float>(1.0 - 0.25 * M_PI) * m_height;

        // clamp width to avoid artifacts which may arise from wrong values of m_height
        m_width = std::min(m_width, std::max(1.0f, 4.0f * m_height));

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        m_width_compare.update(m_width, m_extrusion_role);
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING
    }

#if ENABLE_START_GCODE_VISUALIZATION
    if (type == EMoveType::Extrude && (m_width == 0.0f || m_height == 0.0f))
#else
    if (type == EMoveType::Extrude && (m_extrusion_role == erCustom || m_width == 0.0f || m_height == 0.0f))
#endif // ENABLE_START_GCODE_VISUALIZATION
        type = EMoveType::Travel;

    // time estimate section
    auto move_length = [](const AxisCoords& delta_pos) {
        float sq_xyz_length = sqr(delta_pos[X]) + sqr(delta_pos[Y]) + sqr(delta_pos[Z]);
        return (sq_xyz_length > 0.0f) ? std::sqrt(sq_xyz_length) : std::abs(delta_pos[E]);
    };

    auto is_extrusion_only_move = [](const AxisCoords& delta_pos) {
        return delta_pos[X] == 0.0f && delta_pos[Y] == 0.0f && delta_pos[Z] == 0.0f && delta_pos[E] != 0.0f;
    };

    float distance = move_length(delta_pos);
    assert(distance != 0.0f);
    float inv_distance = 1.0f / distance;

    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        if (!machine.enabled)
            continue;

        TimeMachine::State& curr = machine.curr;
        TimeMachine::State& prev = machine.prev;
        std::vector<TimeBlock>& blocks = machine.blocks;

        curr.feedrate = (delta_pos[E] == 0.0f) ?
            minimum_travel_feedrate(static_cast<PrintEstimatedStatistics::ETimeMode>(i), m_feedrate) :
            minimum_feedrate(static_cast<PrintEstimatedStatistics::ETimeMode>(i), m_feedrate);

        TimeBlock block;
        block.move_type = type;
        block.role = m_extrusion_role;
        block.distance = distance;
        block.g1_line_id = m_g1_line_id;
        block.layer_id = m_layer_id;

        // calculates block cruise feedrate
        float min_feedrate_factor = 1.0f;
        for (unsigned char a = X; a <= E; ++a) {
            curr.axis_feedrate[a] = curr.feedrate * delta_pos[a] * inv_distance;
            if (a == E)
                curr.axis_feedrate[a] *= machine.extrude_factor_override_percentage;

            curr.abs_axis_feedrate[a] = std::abs(curr.axis_feedrate[a]);
            if (curr.abs_axis_feedrate[a] != 0.0f) {
                float axis_max_feedrate = get_axis_max_feedrate(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a));
                if (axis_max_feedrate != 0.0f)
                    min_feedrate_factor = std::min(min_feedrate_factor, axis_max_feedrate / curr.abs_axis_feedrate[a]);
            }
        }

        block.feedrate_profile.cruise = min_feedrate_factor * curr.feedrate;

        if (min_feedrate_factor < 1.0f) {
            for (unsigned char a = X; a <= E; ++a) {
                curr.axis_feedrate[a] *= min_feedrate_factor;
                curr.abs_axis_feedrate[a] *= min_feedrate_factor;
            }
        }

        // calculates block acceleration
        float acceleration = 
            (type == EMoveType::Travel) ? get_travel_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i)) :
            (is_extrusion_only_move(delta_pos) ?
                get_retract_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i)) :
                get_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i)));

        for (unsigned char a = X; a <= E; ++a) {
            float axis_max_acceleration = get_axis_max_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a));
            if (acceleration * std::abs(delta_pos[a]) * inv_distance > axis_max_acceleration)
                acceleration = axis_max_acceleration;
        }

        block.acceleration = acceleration;

        // calculates block exit feedrate
        curr.safe_feedrate = block.feedrate_profile.cruise;

        for (unsigned char a = X; a <= E; ++a) {
            float axis_max_jerk = get_axis_max_jerk(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a));
            if (curr.abs_axis_feedrate[a] > axis_max_jerk)
                curr.safe_feedrate = std::min(curr.safe_feedrate, axis_max_jerk);
        }

        block.feedrate_profile.exit = curr.safe_feedrate;

        static const float PREVIOUS_FEEDRATE_THRESHOLD = 0.0001f;

        // calculates block entry feedrate
        float vmax_junction = curr.safe_feedrate;
        if (!blocks.empty() && prev.feedrate > PREVIOUS_FEEDRATE_THRESHOLD) {
            bool prev_speed_larger = prev.feedrate > block.feedrate_profile.cruise;
            float smaller_speed_factor = prev_speed_larger ? (block.feedrate_profile.cruise / prev.feedrate) : (prev.feedrate / block.feedrate_profile.cruise);
            // Pick the smaller of the nominal speeds. Higher speed shall not be achieved at the junction during coasting.
            vmax_junction = prev_speed_larger ? block.feedrate_profile.cruise : prev.feedrate;

            float v_factor = 1.0f;
            bool limited = false;

            for (unsigned char a = X; a <= E; ++a) {
                // Limit an axis. We have to differentiate coasting from the reversal of an axis movement, or a full stop.
                float v_exit = prev.axis_feedrate[a];
                float v_entry = curr.axis_feedrate[a];

                if (prev_speed_larger)
                    v_exit *= smaller_speed_factor;

                if (limited) {
                    v_exit *= v_factor;
                    v_entry *= v_factor;
                }

                // Calculate the jerk depending on whether the axis is coasting in the same direction or reversing a direction.
                float jerk =
                    (v_exit > v_entry) ?
                    ((v_entry > 0.0f || v_exit < 0.0f) ?
                        // coasting
                        (v_exit - v_entry) :
                        // axis reversal
                        std::max(v_exit, -v_entry)) :
                    // v_exit <= v_entry
                    ((v_entry < 0.0f || v_exit > 0.0f) ?
                        // coasting
                        (v_entry - v_exit) :
                        // axis reversal
                        std::max(-v_exit, v_entry));

                float axis_max_jerk = get_axis_max_jerk(static_cast<PrintEstimatedStatistics::ETimeMode>(i), static_cast<Axis>(a));
                if (jerk > axis_max_jerk) {
                    v_factor *= axis_max_jerk / jerk;
                    limited = true;
                }
            }

            if (limited)
                vmax_junction *= v_factor;

            // Now the transition velocity is known, which maximizes the shared exit / entry velocity while
            // respecting the jerk factors, it may be possible, that applying separate safe exit / entry velocities will achieve faster prints.
            float vmax_junction_threshold = vmax_junction * 0.99f;

            // Not coasting. The machine will stop and start the movements anyway, better to start the segment from start.
            if (prev.safe_feedrate > vmax_junction_threshold && curr.safe_feedrate > vmax_junction_threshold)
                vmax_junction = curr.safe_feedrate;
        }

        float v_allowable = max_allowable_speed(-acceleration, curr.safe_feedrate, block.distance);
        block.feedrate_profile.entry = std::min(vmax_junction, v_allowable);

        block.max_entry_speed = vmax_junction;
        block.flags.nominal_length = (block.feedrate_profile.cruise <= v_allowable);
        block.flags.recalculate = true;
        block.safe_feedrate = curr.safe_feedrate;

        // calculates block trapezoid
        block.calculate_trapezoid();

        // updates previous
        prev = curr;

        blocks.push_back(block);

        if (blocks.size() > TimeProcessor::Planner::refresh_threshold)
            machine.calculate_time(TimeProcessor::Planner::queue_size);
    }

#if ENABLE_SEAMS_VISUALIZATION
    if (m_seams_detector.is_active()) {
        // check for seam starting vertex
        if (type == EMoveType::Extrude && m_extrusion_role == erExternalPerimeter && !m_seams_detector.has_first_vertex())
            m_seams_detector.set_first_vertex(m_result.moves.back().position - m_extruder_offsets[m_extruder_id]);
        // check for seam ending vertex and store the resulting move
        else if ((type != EMoveType::Extrude || m_extrusion_role != erExternalPerimeter) && m_seams_detector.has_first_vertex()) {
            auto set_end_position = [this](const Vec3f& pos) {
                m_end_position[X] = pos.x(); m_end_position[Y] = pos.y(); m_end_position[Z] = pos.z();
            };

            const Vec3f curr_pos(m_end_position[X], m_end_position[Y], m_end_position[Z]);
            const Vec3f new_pos = m_result.moves.back().position - m_extruder_offsets[m_extruder_id];
            const std::optional<Vec3f> first_vertex = m_seams_detector.get_first_vertex();
            // the threshold value = 0.0625f == 0.25 * 0.25 is arbitrary, we may find some smarter condition later
            if ((new_pos - *first_vertex).squaredNorm() < 0.0625f) {
                set_end_position(0.5f * (new_pos + *first_vertex));
                store_move_vertex(EMoveType::Seam);
                set_end_position(curr_pos);
            }

            m_seams_detector.activate(false);
        }
    }
#endif // ENABLE_SEAMS_VISUALIZATION

    // store move
    store_move_vertex(type);
}

void GCodeProcessor::process_G10(const GCodeReader::GCodeLine& line)
{
    // stores retract move
    store_move_vertex(EMoveType::Retract);
}

void GCodeProcessor::process_G11(const GCodeReader::GCodeLine& line)
{
    // stores unretract move
    store_move_vertex(EMoveType::Unretract);
}

void GCodeProcessor::process_G20(const GCodeReader::GCodeLine& line)
{
    m_units = EUnits::Inches;
}

void GCodeProcessor::process_G21(const GCodeReader::GCodeLine& line)
{
    m_units = EUnits::Millimeters;
}

void GCodeProcessor::process_G22(const GCodeReader::GCodeLine& line)
{
    // stores retract move
    store_move_vertex(EMoveType::Retract);
}

void GCodeProcessor::process_G23(const GCodeReader::GCodeLine& line)
{
    // stores unretract move
    store_move_vertex(EMoveType::Unretract);
}

void GCodeProcessor::process_G28(const GCodeReader::GCodeLine& line)
{
    std::string_view cmd = line.cmd();
    std::string new_line_raw = { cmd.data(), cmd.size() };
    bool found = false;
    if (line.has_x()) {
        new_line_raw += " X0";
        found = true;
    }
    if (line.has_y()) {
        new_line_raw += " Y0";
        found = true;
    }
    if (line.has_z()) {
        new_line_raw += " Z0";
        found = true;
    }
    if (!found)
        new_line_raw += " X0  Y0  Z0";

    GCodeReader::GCodeLine new_gline;
    GCodeReader reader;
    reader.parse_line(new_line_raw, [&](GCodeReader& reader, const GCodeReader::GCodeLine& gline) { new_gline = gline; });
    process_G1(new_gline);
}

void GCodeProcessor::process_G90(const GCodeReader::GCodeLine& line)
{
    m_global_positioning_type = EPositioningType::Absolute;
}

void GCodeProcessor::process_G91(const GCodeReader::GCodeLine& line)
{
    m_global_positioning_type = EPositioningType::Relative;
}

void GCodeProcessor::process_G92(const GCodeReader::GCodeLine& line)
{
    float lengths_scale_factor = (m_units == EUnits::Inches) ? INCHES_TO_MM : 1.0f;
    bool any_found = false;

    if (line.has_x()) {
        m_origin[X] = m_end_position[X] - line.x() * lengths_scale_factor;
        any_found = true;
    }

    if (line.has_y()) {
        m_origin[Y] = m_end_position[Y] - line.y() * lengths_scale_factor;
        any_found = true;
    }

    if (line.has_z()) {
        m_origin[Z] = m_end_position[Z] - line.z() * lengths_scale_factor;
        any_found = true;
    }

    if (line.has_e()) {
        // extruder coordinate can grow to the point where its float representation does not allow for proper addition with small increments,
        // we set the value taken from the G92 line as the new current position for it
        m_end_position[E] = line.e() * lengths_scale_factor;
        any_found = true;
    }
    else
        simulate_st_synchronize();

    if (!any_found && !line.has_unknown_axis()) {
        // The G92 may be called for axes that PrusaSlicer does not recognize, for example see GH issue #3510, 
        // where G92 A0 B0 is called although the extruder axis is till E.
        for (unsigned char a = X; a <= E; ++a) {
            m_origin[a] = m_end_position[a];
        }
    }
}

void GCodeProcessor::process_M1(const GCodeReader::GCodeLine& line)
{
    simulate_st_synchronize();
}

void GCodeProcessor::process_M82(const GCodeReader::GCodeLine& line)
{
    m_e_local_positioning_type = EPositioningType::Absolute;
}

void GCodeProcessor::process_M83(const GCodeReader::GCodeLine& line)
{
    m_e_local_positioning_type = EPositioningType::Relative;
}

void GCodeProcessor::process_M104(const GCodeReader::GCodeLine& line)
{
    float new_temp;
    if (line.has_value('S', new_temp))
        m_extruder_temps[m_extruder_id] = new_temp;
}

void GCodeProcessor::process_M106(const GCodeReader::GCodeLine& line)
{
    if (!line.has('P')) {
        // The absence of P means the print cooling fan, so ignore anything else.
        float new_fan_speed;
        if (line.has_value('S', new_fan_speed))
            m_fan_speed = (100.0f / 255.0f) * new_fan_speed;
        else
            m_fan_speed = 100.0f;
    }
}

void GCodeProcessor::process_M107(const GCodeReader::GCodeLine& line)
{
    m_fan_speed = 0.0f;
}

void GCodeProcessor::process_M108(const GCodeReader::GCodeLine& line)
{
    // These M-codes are used by Sailfish to change active tool.
    // They have to be processed otherwise toolchanges will be unrecognised
    // by the analyzer - see https://github.com/prusa3d/PrusaSlicer/issues/2566

    if (m_flavor != gcfSailfish)
        return;

    std::string cmd = line.raw();
    size_t pos = cmd.find("T");
    if (pos != std::string::npos)
        process_T(cmd.substr(pos));
}

void GCodeProcessor::process_M109(const GCodeReader::GCodeLine& line)
{
    float new_temp;
    if (line.has_value('R', new_temp)) {
        float val;
        if (line.has_value('T', val)) {
            size_t eid = static_cast<size_t>(val);
            if (eid < m_extruder_temps.size())
                m_extruder_temps[eid] = new_temp;
        }
        else
            m_extruder_temps[m_extruder_id] = new_temp;
    }
}

void GCodeProcessor::process_M132(const GCodeReader::GCodeLine& line)
{
    // This command is used by Makerbot to load the current home position from EEPROM
    // see: https://github.com/makerbot/s3g/blob/master/doc/GCodeProtocol.md
    // Using this command to reset the axis origin to zero helps in fixing: https://github.com/prusa3d/PrusaSlicer/issues/3082

    if (line.has_x())
        m_origin[X] = 0.0f;

    if (line.has_y())
        m_origin[Y] = 0.0f;

    if (line.has_z())
        m_origin[Z] = 0.0f;

    if (line.has_e())
        m_origin[E] = 0.0f;
}

void GCodeProcessor::process_M135(const GCodeReader::GCodeLine& line)
{
    // These M-codes are used by MakerWare to change active tool.
    // They have to be processed otherwise toolchanges will be unrecognised
    // by the analyzer - see https://github.com/prusa3d/PrusaSlicer/issues/2566

    if (m_flavor != gcfMakerWare)
        return;

    std::string cmd = line.raw();
    size_t pos = cmd.find("T");
    if (pos != std::string::npos)
        process_T(cmd.substr(pos));
}

void GCodeProcessor::process_M201(const GCodeReader::GCodeLine& line)
{
    // see http://reprap.org/wiki/G-code#M201:_Set_max_printing_acceleration
    float factor = ((m_flavor != gcfRepRapSprinter && m_flavor != gcfRepRapFirmware) && m_units == EUnits::Inches) ? INCHES_TO_MM : 1.0f;

    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        if (static_cast<PrintEstimatedStatistics::ETimeMode>(i) == PrintEstimatedStatistics::ETimeMode::Normal ||
            m_time_processor.machine_envelope_processing_enabled) {
            if (line.has_x())
                set_option_value(m_time_processor.machine_limits.machine_max_acceleration_x, i, line.x() * factor);

            if (line.has_y())
                set_option_value(m_time_processor.machine_limits.machine_max_acceleration_y, i, line.y() * factor);

            if (line.has_z())
                set_option_value(m_time_processor.machine_limits.machine_max_acceleration_z, i, line.z() * factor);

            if (line.has_e())
                set_option_value(m_time_processor.machine_limits.machine_max_acceleration_e, i, line.e() * factor);
        }
    }
}

void GCodeProcessor::process_M203(const GCodeReader::GCodeLine& line)
{
    // see http://reprap.org/wiki/G-code#M203:_Set_maximum_feedrate
    if (m_flavor == gcfRepetier)
        return;

    // see http://reprap.org/wiki/G-code#M203:_Set_maximum_feedrate
    // http://smoothieware.org/supported-g-codes
    float factor = (m_flavor == gcfMarlinLegacy || m_flavor == gcfMarlinFirmware || m_flavor == gcfSmoothie) ? 1.0f : MMMIN_TO_MMSEC;

    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        if (static_cast<PrintEstimatedStatistics::ETimeMode>(i) == PrintEstimatedStatistics::ETimeMode::Normal ||
            m_time_processor.machine_envelope_processing_enabled) {
            if (line.has_x())
                set_option_value(m_time_processor.machine_limits.machine_max_feedrate_x, i, line.x() * factor);

            if (line.has_y())
                set_option_value(m_time_processor.machine_limits.machine_max_feedrate_y, i, line.y() * factor);

            if (line.has_z())
                set_option_value(m_time_processor.machine_limits.machine_max_feedrate_z, i, line.z() * factor);

            if (line.has_e())
                set_option_value(m_time_processor.machine_limits.machine_max_feedrate_e, i, line.e() * factor);
        }
    }
}

void GCodeProcessor::process_M204(const GCodeReader::GCodeLine& line)
{
    float value;
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        if (static_cast<PrintEstimatedStatistics::ETimeMode>(i) == PrintEstimatedStatistics::ETimeMode::Normal ||
            m_time_processor.machine_envelope_processing_enabled) {
            if (line.has_value('S', value)) {
                // Legacy acceleration format. This format is used by the legacy Marlin, MK2 or MK3 firmware
                // It is also generated by PrusaSlicer to control acceleration per extrusion type
                // (perimeters, first layer etc) when 'Marlin (legacy)' flavor is used.
                set_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), value);
                set_travel_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), value);
                if (line.has_value('T', value))
                    set_option_value(m_time_processor.machine_limits.machine_max_acceleration_retracting, i, value);
            }
            else {
                // New acceleration format, compatible with the upstream Marlin.
                if (line.has_value('P', value))
                    set_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), value);
                if (line.has_value('R', value))
                    set_option_value(m_time_processor.machine_limits.machine_max_acceleration_retracting, i, value);
                if (line.has_value('T', value))
                    // Interpret the T value as the travel acceleration in the new Marlin format.
                    set_travel_acceleration(static_cast<PrintEstimatedStatistics::ETimeMode>(i), value);
            }
        }
    }
}

void GCodeProcessor::process_M205(const GCodeReader::GCodeLine& line)
{
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        if (static_cast<PrintEstimatedStatistics::ETimeMode>(i) == PrintEstimatedStatistics::ETimeMode::Normal ||
            m_time_processor.machine_envelope_processing_enabled) {
            if (line.has_x()) {
                float max_jerk = line.x();
                set_option_value(m_time_processor.machine_limits.machine_max_jerk_x, i, max_jerk);
                set_option_value(m_time_processor.machine_limits.machine_max_jerk_y, i, max_jerk);
            }

            if (line.has_y())
                set_option_value(m_time_processor.machine_limits.machine_max_jerk_y, i, line.y());

            if (line.has_z())
                set_option_value(m_time_processor.machine_limits.machine_max_jerk_z, i, line.z());

            if (line.has_e())
                set_option_value(m_time_processor.machine_limits.machine_max_jerk_e, i, line.e());

            float value;
            if (line.has_value('S', value))
                set_option_value(m_time_processor.machine_limits.machine_min_extruding_rate, i, value);

            if (line.has_value('T', value))
                set_option_value(m_time_processor.machine_limits.machine_min_travel_rate, i, value);
        }
    }
}

void GCodeProcessor::process_M221(const GCodeReader::GCodeLine& line)
{
    float value_s;
    float value_t;
    if (line.has_value('S', value_s) && !line.has_value('T', value_t)) {
        value_s *= 0.01f;
        for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
            m_time_processor.machines[i].extrude_factor_override_percentage = value_s;
        }
    }
}

void GCodeProcessor::process_M401(const GCodeReader::GCodeLine& line)
{
    if (m_flavor != gcfRepetier)
        return;

    for (unsigned char a = 0; a <= 3; ++a) {
        m_cached_position.position[a] = m_start_position[a];
    }
    m_cached_position.feedrate = m_feedrate;
}

void GCodeProcessor::process_M402(const GCodeReader::GCodeLine& line)
{
    if (m_flavor != gcfRepetier)
        return;

    // see for reference:
    // https://github.com/repetier/Repetier-Firmware/blob/master/src/ArduinoAVR/Repetier/Printer.cpp
    // void Printer::GoToMemoryPosition(bool x, bool y, bool z, bool e, float feed)

    bool has_xyz = !(line.has_x() || line.has_y() || line.has_z());

    float p = FLT_MAX;
    for (unsigned char a = X; a <= Z; ++a) {
        if (has_xyz || line.has(a)) {
            p = m_cached_position.position[a];
            if (p != FLT_MAX)
                m_start_position[a] = p;
        }
    }

    p = m_cached_position.position[E];
    if (p != FLT_MAX)
        m_start_position[E] = p;

    p = FLT_MAX;
    if (!line.has_value(4, p))
        p = m_cached_position.feedrate;

    if (p != FLT_MAX)
        m_feedrate = p;
}

void GCodeProcessor::process_M566(const GCodeReader::GCodeLine& line)
{
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        if (line.has_x())
            set_option_value(m_time_processor.machine_limits.machine_max_jerk_x, i, line.x() * MMMIN_TO_MMSEC);

        if (line.has_y())
            set_option_value(m_time_processor.machine_limits.machine_max_jerk_y, i, line.y() * MMMIN_TO_MMSEC);

        if (line.has_z())
            set_option_value(m_time_processor.machine_limits.machine_max_jerk_z, i, line.z() * MMMIN_TO_MMSEC);

        if (line.has_e())
            set_option_value(m_time_processor.machine_limits.machine_max_jerk_e, i, line.e() * MMMIN_TO_MMSEC);
    }
}

void GCodeProcessor::process_M702(const GCodeReader::GCodeLine& line)
{
    if (line.has('C')) {
        // MK3 MMU2 specific M code:
        // M702 C is expected to be sent by the custom end G-code when finalizing a print.
        // The MK3 unit shall unload and park the active filament into the MMU2 unit.
        m_time_processor.extruder_unloaded = true;
        simulate_st_synchronize(get_filament_unload_time(m_extruder_id));
    }
}

void GCodeProcessor::process_T(const GCodeReader::GCodeLine& line)
{
    process_T(line.cmd());
}

void GCodeProcessor::process_T(const std::string_view command)
{
    if (command.length() > 1) {
        int eid = 0;
        if (! parse_number(command.substr(1), eid) || eid < 0 || eid > 255) {
            // Specific to the MMU2 V2 (see https://www.help.prusa3d.com/en/article/prusa-specific-g-codes_112173):
            if ((m_flavor == gcfMarlinLegacy || m_flavor == gcfMarlinFirmware) && (command == "Tx" || command == "Tc" || command == "T?"))
                return;

            // T-1 is a valid gcode line for RepRap Firmwares (used to deselects all tools) see https://github.com/prusa3d/PrusaSlicer/issues/5677
            if ((m_flavor != gcfRepRapFirmware && m_flavor != gcfRepRapSprinter) || eid != -1)
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid toolchange (" << command << ").";
        } else {
            unsigned char id = static_cast<unsigned char>(eid);
            if (m_extruder_id != id) {
                if (id >= m_result.extruders_count)
                    BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid toolchange, maybe from a custom gcode.";
                else {
                    unsigned char old_extruder_id = m_extruder_id;
                    process_filaments(CustomGCode::ToolChange);
                    m_extruder_id = id;
                    m_cp_color.current = m_extruder_colors[id];
                    // Specific to the MK3 MMU2:
                    // The initial value of extruder_unloaded is set to true indicating
                    // that the filament is parked in the MMU2 unit and there is nothing to be unloaded yet.
                    float extra_time = get_filament_unload_time(static_cast<size_t>(old_extruder_id));
                    m_time_processor.extruder_unloaded = false;
                    extra_time += get_filament_load_time(static_cast<size_t>(m_extruder_id));
                    simulate_st_synchronize(extra_time);
                }

                // store tool change move
                store_move_vertex(EMoveType::Tool_change);
            }
        }
    }
}

void GCodeProcessor::store_move_vertex(EMoveType type)
{
#if ENABLE_SEAMS_VISUALIZATION
    m_last_line_id = (type == EMoveType::Color_change || type == EMoveType::Pause_Print || type == EMoveType::Custom_GCode) ?
        m_line_id + 1 :
        ((type == EMoveType::Seam) ? m_last_line_id : m_line_id);
#endif // ENABLE_SEAMS_VISUALIZATION

    MoveVertex vertex = {
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
#if ENABLE_SEAMS_VISUALIZATION
        m_last_line_id,
#else
        (type == EMoveType::Color_change || type == EMoveType::Pause_Print || type == EMoveType::Custom_GCode) ? m_line_id + 1 : m_line_id,
#endif // ENABLE_SEAMS_VISUALIZATION
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        type,
        m_extrusion_role,
        m_extruder_id,
        m_cp_color.current,
#if ENABLE_START_GCODE_VISUALIZATION
        Vec3f(m_end_position[X], m_end_position[Y], m_processing_start_custom_gcode ? m_first_layer_height : m_end_position[Z]) + m_extruder_offsets[m_extruder_id],
#else
        Vec3f(m_end_position[X], m_end_position[Y], m_end_position[Z]) + m_extruder_offsets[m_extruder_id],
#endif // ENABLE_START_GCODE_VISUALIZATION
        m_end_position[E] - m_start_position[E],
        m_feedrate,
        m_width,
        m_height,
        m_mm3_per_mm,
        m_fan_speed,
        m_extruder_temps[m_extruder_id],
        static_cast<float>(m_result.moves.size())
    };
    m_result.moves.emplace_back(vertex);

#if ENABLE_EXTENDED_M73_LINES
    // stores stop time placeholders for later use
    if (type == EMoveType::Color_change || type == EMoveType::Pause_Print) {
        for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
            TimeMachine& machine = m_time_processor.machines[i];
            if (!machine.enabled)
                continue;

            machine.stop_times.push_back({ m_g1_line_id, 0.0f });
        }
    }
#endif // ENABLE_EXTENDED_M73_LINES
}

void GCodeProcessor::set_extrusion_role(ExtrusionRole role)
{
    m_used_filaments.process_role_cache(this);
    m_extrusion_role = role;
}

float GCodeProcessor::minimum_feedrate(PrintEstimatedStatistics::ETimeMode mode, float feedrate) const
{
    if (m_time_processor.machine_limits.machine_min_extruding_rate.empty())
        return feedrate;

    return std::max(feedrate, get_option_value(m_time_processor.machine_limits.machine_min_extruding_rate, static_cast<size_t>(mode)));
}

float GCodeProcessor::minimum_travel_feedrate(PrintEstimatedStatistics::ETimeMode mode, float feedrate) const
{
    if (m_time_processor.machine_limits.machine_min_travel_rate.empty())
        return feedrate;

    return std::max(feedrate, get_option_value(m_time_processor.machine_limits.machine_min_travel_rate, static_cast<size_t>(mode)));
}

float GCodeProcessor::get_axis_max_feedrate(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const
{
    switch (axis)
    {
    case X: { return get_option_value(m_time_processor.machine_limits.machine_max_feedrate_x, static_cast<size_t>(mode)); }
    case Y: { return get_option_value(m_time_processor.machine_limits.machine_max_feedrate_y, static_cast<size_t>(mode)); }
    case Z: { return get_option_value(m_time_processor.machine_limits.machine_max_feedrate_z, static_cast<size_t>(mode)); }
    case E: { return get_option_value(m_time_processor.machine_limits.machine_max_feedrate_e, static_cast<size_t>(mode)); }
    default: { return 0.0f; }
    }
}

float GCodeProcessor::get_axis_max_acceleration(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const
{
    switch (axis)
    {
    case X: { return get_option_value(m_time_processor.machine_limits.machine_max_acceleration_x, static_cast<size_t>(mode)); }
    case Y: { return get_option_value(m_time_processor.machine_limits.machine_max_acceleration_y, static_cast<size_t>(mode)); }
    case Z: { return get_option_value(m_time_processor.machine_limits.machine_max_acceleration_z, static_cast<size_t>(mode)); }
    case E: { return get_option_value(m_time_processor.machine_limits.machine_max_acceleration_e, static_cast<size_t>(mode)); }
    default: { return 0.0f; }
    }
}

float GCodeProcessor::get_axis_max_jerk(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const
{
    switch (axis)
    {
    case X: { return get_option_value(m_time_processor.machine_limits.machine_max_jerk_x, static_cast<size_t>(mode)); }
    case Y: { return get_option_value(m_time_processor.machine_limits.machine_max_jerk_y, static_cast<size_t>(mode)); }
    case Z: { return get_option_value(m_time_processor.machine_limits.machine_max_jerk_z, static_cast<size_t>(mode)); }
    case E: { return get_option_value(m_time_processor.machine_limits.machine_max_jerk_e, static_cast<size_t>(mode)); }
    default: { return 0.0f; }
    }
}

float GCodeProcessor::get_retract_acceleration(PrintEstimatedStatistics::ETimeMode mode) const
{
    return get_option_value(m_time_processor.machine_limits.machine_max_acceleration_retracting, static_cast<size_t>(mode));
}

float GCodeProcessor::get_acceleration(PrintEstimatedStatistics::ETimeMode mode) const
{
    size_t id = static_cast<size_t>(mode);
    return (id < m_time_processor.machines.size()) ? m_time_processor.machines[id].acceleration : DEFAULT_ACCELERATION;
}

void GCodeProcessor::set_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value)
{
    size_t id = static_cast<size_t>(mode);
    if (id < m_time_processor.machines.size()) {
        m_time_processor.machines[id].acceleration = (m_time_processor.machines[id].max_acceleration == 0.0f) ? value :
            // Clamp the acceleration with the maximum.
            std::min(value, m_time_processor.machines[id].max_acceleration);
    }
}

float GCodeProcessor::get_travel_acceleration(PrintEstimatedStatistics::ETimeMode mode) const
{
    size_t id = static_cast<size_t>(mode);
    return (id < m_time_processor.machines.size()) ? m_time_processor.machines[id].travel_acceleration : DEFAULT_TRAVEL_ACCELERATION;
}

void GCodeProcessor::set_travel_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value)
{
    size_t id = static_cast<size_t>(mode);
    if (id < m_time_processor.machines.size()) {
        m_time_processor.machines[id].travel_acceleration = (m_time_processor.machines[id].max_travel_acceleration == 0.0f) ? value :
            // Clamp the acceleration with the maximum.
            std::min(value, m_time_processor.machines[id].max_travel_acceleration);
    }
}

float GCodeProcessor::get_filament_load_time(size_t extruder_id)
{
    return (m_time_processor.filament_load_times.empty() || m_time_processor.extruder_unloaded) ?
        0.0f :
        ((extruder_id < m_time_processor.filament_load_times.size()) ?
            m_time_processor.filament_load_times[extruder_id] : m_time_processor.filament_load_times.front());
}

float GCodeProcessor::get_filament_unload_time(size_t extruder_id)
{
    return (m_time_processor.filament_unload_times.empty() || m_time_processor.extruder_unloaded) ?
        0.0f :
        ((extruder_id < m_time_processor.filament_unload_times.size()) ?
            m_time_processor.filament_unload_times[extruder_id] : m_time_processor.filament_unload_times.front());
}

void GCodeProcessor::process_custom_gcode_time(CustomGCode::Type code)
{
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        if (!machine.enabled)
            continue;

        TimeMachine::CustomGCodeTime& gcode_time = machine.gcode_time;
        gcode_time.needed = true;
        //FIXME this simulates st_synchronize! is it correct?
        // The estimated time may be longer than the real print time.
        machine.simulate_st_synchronize();
        if (gcode_time.cache != 0.0f) {
            gcode_time.times.push_back({ code, gcode_time.cache });
            gcode_time.cache = 0.0f;
        }
    }
}

void GCodeProcessor::process_filaments(CustomGCode::Type code)
{
    if (code == CustomGCode::ColorChange)
        m_used_filaments.process_color_change_cache();

    if (code == CustomGCode::ToolChange)
        m_used_filaments.process_extruder_cache(this);
}

void GCodeProcessor::simulate_st_synchronize(float additional_time)
{
    for (size_t i = 0; i < static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count); ++i) {
        m_time_processor.machines[i].simulate_st_synchronize(additional_time);
    }
}

void GCodeProcessor::update_estimated_times_stats()
{
    auto update_mode = [this](PrintEstimatedStatistics::ETimeMode mode) {
        PrintEstimatedStatistics::Mode& data = m_result.print_statistics.modes[static_cast<size_t>(mode)];
        data.time = get_time(mode);
        data.custom_gcode_times = get_custom_gcode_times(mode, true);
        data.moves_times = get_moves_time(mode);
        data.roles_times = get_roles_time(mode);
        data.layers_times = get_layers_time(mode);
    };

    update_mode(PrintEstimatedStatistics::ETimeMode::Normal);
    if (m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].enabled)
        update_mode(PrintEstimatedStatistics::ETimeMode::Stealth);
    else
        m_result.print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].reset();

    m_result.print_statistics.volumes_per_color_change  = m_used_filaments.volumes_per_color_change;
    m_result.print_statistics.volumes_per_extruder      = m_used_filaments.volumes_per_extruder;
    m_result.print_statistics.used_filaments_per_role   = m_used_filaments.filaments_per_role;
}

} /* namespace Slic3r */

