#include "libslic3r/libslic3r.h"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Print.hpp"
#include "GCodeProcessor.hpp"

#include <boost/log/trivial.hpp>

#include <float.h>
#include <assert.h>

#if ENABLE_GCODE_VIEWER

#if ENABLE_GCODE_VIEWER_STATISTICS
#include <chrono>
#endif // ENABLE_GCODE_VIEWER_STATISTICS

static const float INCHES_TO_MM = 25.4f;
static const float MMMIN_TO_MMSEC = 1.0f / 60.0f;

static const float DEFAULT_ACCELERATION = 1500.0f; // Prusa Firmware 1_75mm_MK2

namespace Slic3r {

const std::string GCodeProcessor::Extrusion_Role_Tag = "ExtrType:";
const std::string GCodeProcessor::Width_Tag          = "PrusaSlicer__WIDTH:";
const std::string GCodeProcessor::Height_Tag         = "PrusaSlicer__HEIGHT:";
const std::string GCodeProcessor::Color_Change_Tag   = "COLOR_CHANGE";
const std::string GCodeProcessor::Pause_Print_Tag    = "PAUSE_PRINT";
const std::string GCodeProcessor::Custom_Code_Tag    = "CUSTOM_CODE";

static bool is_valid_extrusion_role(int value)
{
    return (static_cast<int>(erNone) <= value) && (value <= static_cast<int>(erMixed));
}

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
    extrude_factor_override_percentage = 1.0f;
    time = 0.0f;
    curr.reset();
    prev.reset();
    gcode_time.reset();
    blocks = std::vector<TimeBlock>();
    std::fill(moves_time.begin(), moves_time.end(), 0.0f);
    std::fill(roles_time.begin(), roles_time.end(), 0.0f);
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
//    m_g1_times.reserve(m_g1_times.size() + n_blocks_process);
    for (size_t i = 0; i < n_blocks_process; ++i) {
        const TimeBlock& block = blocks[i];
        float block_time = block.time();
        time += block_time;
        gcode_time.cache += block_time;
        moves_time[static_cast<size_t>(block.move_type)] += block_time;
        roles_time[static_cast<size_t>(block.role)] += block_time;

//        if (block.g1_line_id >= 0)
//            m_g1_times.emplace_back(block.g1_line_id, time);
    }

    if (keep_last_n_blocks)
        blocks.erase(blocks.begin(), blocks.begin() + n_blocks_process);
    else
        blocks.clear();
}

void GCodeProcessor::TimeProcessor::reset()
{
    extruder_unloaded = true;
    machine_limits = MachineEnvelopeConfig();
    filament_load_times = std::vector<float>();
    filament_unload_times = std::vector<float>();
    for (size_t i = 0; i < static_cast<size_t>(ETimeMode::Count); ++i) {
        machines[i].reset();
    }
    machines[static_cast<size_t>(ETimeMode::Normal)].enabled = true;
}

const std::vector<std::pair<GCodeProcessor::EProducer, std::string>> GCodeProcessor::Producers = {
    { EProducer::PrusaSlicer, "PrusaSlicer" },
    { EProducer::Cura,        "Cura" },
    { EProducer::Simplify3D,  "Simplify3D" },
    { EProducer::CraftWare,   "CraftWare" },
    { EProducer::ideaMaker,   "ideaMaker" }
};

unsigned int GCodeProcessor::s_result_id = 0;

void GCodeProcessor::apply_config(const PrintConfig& config)
{
    m_parser.apply_config(config);

    m_flavor = config.gcode_flavor;

    size_t extruders_count = config.nozzle_diameter.values.size();

    m_extruder_offsets.resize(extruders_count);
    for (size_t id = 0; id < extruders_count; ++id) {
        Vec2f offset = config.extruder_offset.get_at(id).cast<float>();
        m_extruder_offsets[id] = Vec3f(offset(0), offset(1), 0.0f);
    }

    m_extruders_color.resize(extruders_count);
    for (size_t id = 0; id < extruders_count; ++id) {
        m_extruders_color[id] = static_cast<unsigned int>(id);
    }

    for (double diam : config.filament_diameter.values) {
        m_filament_diameters.push_back(static_cast<float>(diam));
    }

    m_time_processor.machine_limits = reinterpret_cast<const MachineEnvelopeConfig&>(config);
    // Filament load / unload times are not specific to a firmware flavor. Let anybody use it if they find it useful.
    // As of now the fields are shown at the UI dialog in the same combo box as the ramming values, so they
    // are considered to be active for the single extruder multi-material printers only.
    m_time_processor.filament_load_times.clear();
    for (double d : config.filament_load_time.values) {
        m_time_processor.filament_load_times.push_back(static_cast<float>(d));
    }
    m_time_processor.filament_unload_times.clear();
    for (double d : config.filament_unload_time.values) {
        m_time_processor.filament_unload_times.push_back(static_cast<float>(d));
    }
    for (size_t i = 0; i < static_cast<size_t>(ETimeMode::Count); ++i) {
        float max_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_extruding, i);
        m_time_processor.machines[i].acceleration = (max_acceleration > 0.0f) ? max_acceleration : DEFAULT_ACCELERATION;
    }
}

void GCodeProcessor::apply_config(const DynamicPrintConfig& config)
{
    m_parser.apply_config(config);

    const ConfigOptionFloats* filament_diameters = config.option<ConfigOptionFloats>("filament_diameter");
    if (filament_diameters != nullptr) {
        for (double diam : filament_diameters->values) {
            m_filament_diameters.push_back(static_cast<float>(diam));
        }
    }

    const ConfigOptionPoints* bed_shape = config.option<ConfigOptionPoints>("bed_shape");
    if (bed_shape != nullptr) 
        m_result.bed_shape = bed_shape->values;
}

void GCodeProcessor::enable_stealth_time_estimator(bool enabled)
{
    m_time_processor.machines[static_cast<size_t>(ETimeMode::Stealth)].enabled = enabled;
}

void GCodeProcessor::reset()
{
    m_units = EUnits::Millimeters;
    m_global_positioning_type = EPositioningType::Absolute;
    m_e_local_positioning_type = EPositioningType::Absolute;
    m_extruder_offsets = std::vector<Vec3f>(1, Vec3f::Zero());
    m_flavor = gcfRepRap;

    m_start_position = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_end_position = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_origin = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_cached_position.reset();

    m_feedrate = 0.0f;
    m_width = 0.0f;
    m_height = 0.0f;
    m_mm3_per_mm = 0.0f;
    m_fan_speed = 0.0f;

    m_extrusion_role = erNone;
    m_extruder_id = 0;
    m_extruders_color = ExtrudersColor();
    m_filament_diameters = std::vector<float>();
    m_cp_color.reset();

    m_producer = EProducer::Unknown;
    m_producers_enabled = false;

    m_time_processor.reset();

    m_result.reset();
    m_result.id = ++s_result_id;
}

void GCodeProcessor::process_file(const std::string& filename)
{
#if ENABLE_GCODE_VIEWER_STATISTICS
    auto start_time = std::chrono::high_resolution_clock::now();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // pre-processing
    // parse the gcode file to detect its producer
    if (m_producers_enabled) {
        m_parser.parse_file(filename, [this](GCodeReader& reader, const GCodeReader::GCodeLine& line) {
            std::string cmd = line.cmd();
            if (cmd.length() == 0) {
                std::string comment = line.comment();
                if (comment.length() > 1 && detect_producer(comment))
                    m_parser.quit_parsing_file();
            }
            });

        // if the gcode was produced by PrusaSlicer,
        // extract the config from it
        if (m_producer == EProducer::PrusaSlicer) {
            DynamicPrintConfig config;
            config.apply(FullPrintConfig::defaults());
            config.load_from_gcode_file(filename);
            apply_config(config);
        }
    }

    m_result.id = ++s_result_id;
    m_result.moves.emplace_back(MoveVertex());
    m_parser.parse_file(filename, [this](GCodeReader& reader, const GCodeReader::GCodeLine& line) { process_gcode_line(line); });

    // process the remaining time blocks
    for (size_t i = 0; i < static_cast<size_t>(ETimeMode::Count); ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        TimeMachine::CustomGCodeTime& gcode_time = machine.gcode_time;
        machine.calculate_time();
        if (gcode_time.needed && gcode_time.cache != 0.0f)
            gcode_time.times.push_back({ CustomGCode::ColorChange, gcode_time.cache });
    }

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_result.time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
}

void GCodeProcessor::update_print_stats_estimated_times(PrintStatistics& print_statistics)
{
    print_statistics.estimated_normal_print_time = get_time(GCodeProcessor::ETimeMode::Normal);
    print_statistics.estimated_normal_custom_gcode_print_times = get_custom_gcode_times(GCodeProcessor::ETimeMode::Normal, true);
    print_statistics.estimated_normal_moves_times = get_moves_time(GCodeProcessor::ETimeMode::Normal);
    print_statistics.estimated_normal_roles_times = get_roles_time(GCodeProcessor::ETimeMode::Normal);
    if (m_time_processor.machines[static_cast<size_t>(GCodeProcessor::ETimeMode::Stealth)].enabled) {
        print_statistics.estimated_silent_print_time = get_time(GCodeProcessor::ETimeMode::Stealth);
        print_statistics.estimated_silent_custom_gcode_print_times = get_custom_gcode_times(GCodeProcessor::ETimeMode::Stealth, true);
        print_statistics.estimated_silent_moves_times = get_moves_time(GCodeProcessor::ETimeMode::Stealth);
        print_statistics.estimated_silent_roles_times = get_roles_time(GCodeProcessor::ETimeMode::Stealth);
    }
    else {
        print_statistics.estimated_silent_print_time = 0.0f;
        print_statistics.estimated_silent_custom_gcode_print_times.clear();
        print_statistics.estimated_silent_moves_times.clear();
        print_statistics.estimated_silent_roles_times.clear();
    }
}

float GCodeProcessor::get_time(ETimeMode mode) const
{
    return (mode < ETimeMode::Count) ? m_time_processor.machines[static_cast<size_t>(mode)].time : 0.0f;
}

std::string GCodeProcessor::get_time_dhm(ETimeMode mode) const
{
    return (mode < ETimeMode::Count) ? short_time(get_time_dhms(m_time_processor.machines[static_cast<size_t>(mode)].time)) : std::string("N/A");
}

std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> GCodeProcessor::get_custom_gcode_times(ETimeMode mode, bool include_remaining) const
{
    std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> ret;
    if (mode < ETimeMode::Count) {
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

std::vector<std::pair<GCodeProcessor::EMoveType, float>> GCodeProcessor::get_moves_time(ETimeMode mode) const
{
    std::vector<std::pair<EMoveType, float>> ret;
    if (mode < ETimeMode::Count) {
        for (size_t i = 0; i < m_time_processor.machines[static_cast<size_t>(mode)].moves_time.size(); ++i) {
            float time = m_time_processor.machines[static_cast<size_t>(mode)].moves_time[i];
            if (time > 0.0f)
                ret.push_back({ static_cast<EMoveType>(i), time });
        }
    }
    return ret;
}

std::vector<std::pair<ExtrusionRole, float>> GCodeProcessor::get_roles_time(ETimeMode mode) const
{
    std::vector<std::pair<ExtrusionRole, float>> ret;
    if (mode < ETimeMode::Count) {
        for (size_t i = 0; i < m_time_processor.machines[static_cast<size_t>(mode)].roles_time.size(); ++i) {
            float time = m_time_processor.machines[static_cast<size_t>(mode)].roles_time[i];
            if (time > 0.0f)
                ret.push_back({ static_cast<ExtrusionRole>(i), time });
        }
    }
    return ret;
}

void GCodeProcessor::process_gcode_line(const GCodeReader::GCodeLine& line)
{
/* std::cout << line.raw() << std::endl; */

    // update start position
    m_start_position = m_end_position;

    std::string cmd = line.cmd();
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
                case 106: { process_M106(line); break; } // Set fan speed
                case 107: { process_M107(line); break; } // Disable fan
                case 108: { process_M108(line); break; } // Set tool (Sailfish)
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
        std::string comment = line.comment();
        if (comment.length() > 1)
            // process tags embedded into comments
            process_tags(comment);
    }
}

void GCodeProcessor::process_tags(const std::string& comment)
{
    // producers tags
    if (m_producers_enabled) {
        if (m_producer != EProducer::Unknown) {
            if (process_producers_tags(comment))
                return;
        }
    }

    // extrusion role tag
    size_t pos = comment.find(Extrusion_Role_Tag);
    if (pos != comment.npos) {
        m_extrusion_role = ExtrusionEntity::string_to_role(comment.substr(pos + Extrusion_Role_Tag.length()));
        return;
    }

    // width tag
    pos = comment.find(Width_Tag);
    if (pos != comment.npos) {
        try
        {
            m_width = std::stof(comment.substr(pos + Width_Tag.length()));
        }
        catch (...)
        {
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Width (" << comment << ").";
        }
        return;
    }

    // height tag
    pos = comment.find(Height_Tag);
    if (pos != comment.npos) {
        try
        {
            m_height = std::stof(comment.substr(pos + Height_Tag.length()));
        }
        catch (...)
        {
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Height (" << comment << ").";
        }
        return;
    }

    // color change tag
    pos = comment.find(Color_Change_Tag);
    if (pos != comment.npos) {
        pos = comment.find_last_of(",T");
        try
        {
            unsigned char extruder_id = (pos == comment.npos) ? 0 : static_cast<unsigned char>(std::stoi(comment.substr(pos + 1)));

            m_extruders_color[extruder_id] = static_cast<unsigned char>(m_extruder_offsets.size()) + m_cp_color.counter; // color_change position in list of color for preview
            ++m_cp_color.counter;
            if (m_cp_color.counter == UCHAR_MAX)
                m_cp_color.counter = 0;

            if (m_extruder_id == extruder_id) {
                m_cp_color.current = m_extruders_color[extruder_id];
                store_move_vertex(EMoveType::Color_change);
            }

            process_custom_gcode_time(CustomGCode::ColorChange);
        }
        catch (...)
        {
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Color_Change (" << comment << ").";
        }

        return;
    }

    // pause print tag
    pos = comment.find(Pause_Print_Tag);
    if (pos != comment.npos) {
        store_move_vertex(EMoveType::Pause_Print);
        process_custom_gcode_time(CustomGCode::PausePrint);
        return;
    }

    // custom code tag
    pos = comment.find(Custom_Code_Tag);
    if (pos != comment.npos) {
        store_move_vertex(EMoveType::Custom_GCode);
        return;
    }
}

bool GCodeProcessor::process_producers_tags(const std::string& comment)
{
    switch (m_producer)
    {
    case EProducer::PrusaSlicer: { return process_prusaslicer_tags(comment); }
    case EProducer::Cura:        { return process_cura_tags(comment); }
    case EProducer::Simplify3D:  { return process_simplify3d_tags(comment); }
    case EProducer::CraftWare:   { return process_craftware_tags(comment); }
    case EProducer::ideaMaker:   { return process_ideamaker_tags(comment); }
    default:                     { return false; }
    }
}

bool GCodeProcessor::process_prusaslicer_tags(const std::string& comment)
{
    std::cout << comment << "\n";
    return false;
}

bool GCodeProcessor::process_cura_tags(const std::string& comment)
{
    // TYPE -> extrusion role
    std::string tag = "TYPE:";
    size_t pos = comment.find(tag);
    if (pos != comment.npos) {
        std::string type = comment.substr(pos + tag.length());
        if (type == "SKIRT")
            m_extrusion_role = erSkirt;
        else if (type == "WALL-OUTER")
            m_extrusion_role = erExternalPerimeter;
        else if (type == "WALL-INNER")
            m_extrusion_role = erPerimeter;
        else if (type == "SKIN")
            m_extrusion_role = erSolidInfill;
        else if (type == "FILL")
            m_extrusion_role = erInternalInfill;
        else if (type == "SUPPORT")
            m_extrusion_role = erSupportMaterial;
        else if (type == "SUPPORT-INTERFACE")
            m_extrusion_role = erSupportMaterialInterface;
        else if (type == "PRIME-TOWER")
            m_extrusion_role = erWipeTower;
        else {
            m_extrusion_role = erNone;
            BOOST_LOG_TRIVIAL(warning) << "GCodeProcessor found unknown extrusion role: " << type;
        }

        return true;
    }

    return false;
}

bool GCodeProcessor::process_simplify3d_tags(const std::string& comment)
{
    // extrusion roles

    // ; skirt
    size_t pos = comment.find(" skirt");
    if (pos == 0) {
        m_extrusion_role = erSkirt;
        return true;
    }
    
    // ; outer perimeter
    pos = comment.find(" outer perimeter");
    if (pos == 0) {
        m_extrusion_role = erExternalPerimeter;
        return true;
    }

    // ; inner perimeter
    pos = comment.find(" inner perimeter");
    if (pos == 0) {
        m_extrusion_role = erPerimeter;
        return true;
    }

    // ; gap fill
    pos = comment.find(" gap fill");
    if (pos == 0) {
        m_extrusion_role = erGapFill;
        return true;
    }

    // ; infill
    pos = comment.find(" infill");
    if (pos == 0) {
        m_extrusion_role = erInternalInfill;
        return true;
    }

    // ; solid layer
    pos = comment.find(" solid layer");
    if (pos == 0) {
        m_extrusion_role = erNone; // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; bridge
    pos = comment.find(" bridge");
    if (pos == 0) {
        m_extrusion_role = erBridgeInfill;
        return true;
    }

    // ; support
    pos = comment.find(" support");
    if (pos == 0) {
        m_extrusion_role = erSupportMaterial;
        return true;
    }

    // ; prime pillar
    pos = comment.find(" prime pillar");
    if (pos == 0) {
        m_extrusion_role = erWipeTower;
        return true;
    }

    // ; ooze shield
    pos = comment.find(" ooze shield");
    if (pos == 0) {
        m_extrusion_role = erNone; // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        return true;
    }

    // ; raft
    pos = comment.find(" raft");
    if (pos == 0) {
        m_extrusion_role = erSkirt;
        return true;
    }

    // geometry

    // ; tool
    std::string tag = " tool";
    pos = comment.find(tag);
    if (pos == 0) {
        std::string data = comment.substr(pos + tag.length());
        std::string h_tag = "H";
        size_t h_start = data.find(h_tag);
        size_t h_end = data.find_first_of(' ', h_start);
        std::string w_tag = "W";
        size_t w_start = data.find(w_tag);
        size_t w_end = data.find_first_of(' ', w_start);
        if (h_start != data.npos) {
            try
            {
                std::string test = data.substr(h_start + 1, (h_end != data.npos) ? h_end - h_start - 1 : h_end);
                m_height = std::stof(data.substr(h_start + 1, (h_end != data.npos) ? h_end - h_start - 1 : h_end));
            }
            catch (...)
            {
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Height (" << comment << ").";
            }
        }
        if (w_start != data.npos) {
            try
            {
                std::string test = data.substr(w_start + 1, (w_end != data.npos) ? w_end - w_start - 1 : w_end);
                m_width = std::stof(data.substr(w_start + 1, (w_end != data.npos) ? w_end - w_start - 1 : w_end));
            }
            catch (...)
            {
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Width (" << comment << ").";
            }
        }

        return true;
    }

    std::cout << comment << "\n";
    return false;
}

bool GCodeProcessor::process_craftware_tags(const std::string& comment)
{
    // segType -> extrusion role
    std::string tag = "segType:";
    size_t pos = comment.find(tag);
    if (pos != comment.npos) {
        std::string type = comment.substr(pos + tag.length());
        if (type == "Skirt")
            m_extrusion_role = erSkirt;
        else if (type == "Perimeter")
            m_extrusion_role = erExternalPerimeter;
        else if (type == "HShell")
            m_extrusion_role = erNone; // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        else if (type == "InnerHair")
            m_extrusion_role = erNone; // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        else if (type == "Loop")
            m_extrusion_role = erNone; // <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<
        else if (type == "Infill")
            m_extrusion_role = erInternalInfill;
        else if (type == "Raft")
            m_extrusion_role = erSkirt;
        else if (type == "Support")
            m_extrusion_role = erSupportMaterial;
        else if (type == "SupportTouch")
            m_extrusion_role = erSupportMaterial;
        else if (type == "SoftSupport")
            m_extrusion_role = erSupportMaterialInterface;
        else if (type == "Pillar")
            m_extrusion_role = erWipeTower;
        else {
            m_extrusion_role = erNone;
            BOOST_LOG_TRIVIAL(warning) << "GCodeProcessor found unknown extrusion role: " << type;
        }

        return true;
    }

    return false;
}

bool GCodeProcessor::process_ideamaker_tags(const std::string& comment)
{
    // TYPE -> extrusion role
    std::string tag = "TYPE:";
    size_t pos = comment.find(tag);
    if (pos != comment.npos) {
        std::string type = comment.substr(pos + tag.length());
        if (type == "RAFT")
            m_extrusion_role = erSkirt;
        else if (type == "WALL-OUTER")
            m_extrusion_role = erExternalPerimeter;
        else if (type == "WALL-INNER")
            m_extrusion_role = erPerimeter;
        else if (type == "SOLID-FILL")
            m_extrusion_role = erSolidInfill;
        else if (type == "FILL")
            m_extrusion_role = erInternalInfill;
        else if (type == "BRIDGE")
            m_extrusion_role = erBridgeInfill;
        else if (type == "SUPPORT")
            m_extrusion_role = erSupportMaterial;
        else {
            m_extrusion_role = erNone;
            BOOST_LOG_TRIVIAL(warning) << "GCodeProcessor found unknown extrusion role: " << type;
        }
        return true;
    }

    // geometry

    // width
    tag = "WIDTH:";
    pos = comment.find(tag);
    if (pos != comment.npos) {
        try
        {
            m_width = std::stof(comment.substr(pos + tag.length()));
        }
        catch (...)
        {
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Width (" << comment << ").";
        }
        return true;
    }

    // height
    tag = "HEIGHT:";
    pos = comment.find(tag);
    if (pos != comment.npos) {
        try
        {
            m_height = std::stof(comment.substr(pos + tag.length()));
        }
        catch (...)
        {
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Height (" << comment << ").";
        }
        return true;
    }

    return false;
}

bool GCodeProcessor::detect_producer(const std::string& comment)
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
    auto absolute_position = [this](Axis axis, const GCodeReader::GCodeLine& lineG1)
    {
        bool is_relative = (m_global_positioning_type == EPositioningType::Relative);
        if (axis == E)
            is_relative |= (m_e_local_positioning_type == EPositioningType::Relative);

        if (lineG1.has(Slic3r::Axis(axis))) {
            float lengthsScaleFactor = (m_units == EUnits::Inches) ? INCHES_TO_MM : 1.0f;
            float ret = lineG1.value(Slic3r::Axis(axis)) * lengthsScaleFactor;
            return is_relative ? m_start_position[axis] + ret : m_origin[axis] + ret;
        }
        else
            return m_start_position[axis];
    };

    auto move_type = [this](const AxisCoords& delta_pos) {
        EMoveType type = EMoveType::Noop;

        if (delta_pos[E] < 0.0f) {
            type = (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f || delta_pos[Z] != 0.0f) ? EMoveType::Travel : EMoveType::Retract;
        }
        else if (delta_pos[E] > 0.0f) {
            if (delta_pos[X] == 0.0f && delta_pos[Y] == 0.0f && delta_pos[Z] == 0.0f)
                type = EMoveType::Unretract;
            else if (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f)
                type = EMoveType::Extrude;
        } 
        else if (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f || delta_pos[Z] != 0.0f)
            type = EMoveType::Travel;

#if ENABLE_GCODE_VIEWER_AS_STATE
        if (type == EMoveType::Extrude && (m_width == 0.0f || m_height == 0.0f)) {
            if (m_extrusion_role != erCustom) {
                m_width = 0.5f;
                m_height = 0.5f;
            }
            type = EMoveType::Travel;
        }
#else
        if (type == EMoveType::Extrude && (m_width == 0.0f || m_height == 0.0f || !is_valid_extrusion_role(m_extrusion_role)))
            type = EMoveType::Travel;
#endif // ENABLE_GCODE_VIEWER_AS_STATE

        return type;
    };

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

    if (type == EMoveType::Extrude) {
        if (delta_pos[E] > 0.0f) {
            float ds = std::sqrt(sqr(delta_pos[X]) + sqr(delta_pos[Y]) + sqr(delta_pos[Z]));
            if (ds > 0.0f && static_cast<size_t>(m_extruder_id) < m_filament_diameters.size())
                m_mm3_per_mm = round_nearest(delta_pos[E] * static_cast<float>(M_PI) * sqr(static_cast<float>(m_filament_diameters[m_extruder_id])) / (4.0f * ds), 3);
        }
    }

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

    for (size_t i = 0; i < static_cast<size_t>(ETimeMode::Count); ++i) {
        TimeMachine& machine = m_time_processor.machines[i];
        if (!machine.enabled)
            continue;

        TimeMachine::State& curr = machine.curr;
        TimeMachine::State& prev = machine.prev;
        std::vector<TimeBlock>& blocks = machine.blocks;

        curr.feedrate = (delta_pos[E] == 0.0f) ?
            minimum_travel_feedrate(static_cast<ETimeMode>(i), m_feedrate) :
            minimum_feedrate(static_cast<ETimeMode>(i), m_feedrate);

        TimeBlock block;
        block.move_type = type;
        block.role = m_extrusion_role;
        block.distance = distance;

        // calculates block cruise feedrate
        float min_feedrate_factor = 1.0f;
        for (unsigned char a = X; a <= E; ++a) {
            curr.axis_feedrate[a] = curr.feedrate * delta_pos[a] * inv_distance;
            if (a == E)
                curr.axis_feedrate[a] *= machine.extrude_factor_override_percentage;

            curr.abs_axis_feedrate[a] = std::abs(curr.axis_feedrate[a]);
            if (curr.abs_axis_feedrate[a] != 0.0f) {
                float axis_max_feedrate = get_axis_max_feedrate(static_cast<ETimeMode>(i), static_cast<Axis>(a));
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
        float acceleration = is_extrusion_only_move(delta_pos) ?
            get_retract_acceleration(static_cast<ETimeMode>(i)) :
            get_acceleration(static_cast<ETimeMode>(i));

        for (unsigned char a = X; a <= E; ++a) {
            float axis_max_acceleration = get_axis_max_acceleration(static_cast<ETimeMode>(i), static_cast<Axis>(a));
            if (acceleration * std::abs(delta_pos[a]) * inv_distance > axis_max_acceleration)
                acceleration = axis_max_acceleration;
        }

        block.acceleration = acceleration;

        // calculates block exit feedrate
        curr.safe_feedrate = block.feedrate_profile.cruise;

        for (unsigned char a = X; a <= E; ++a) {
            float axis_max_jerk = get_axis_max_jerk(static_cast<ETimeMode>(i), static_cast<Axis>(a));
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
                    (((v_entry > 0.0f) || (v_exit < 0.0f)) ?
                        // coasting
                        (v_exit - v_entry) :
                        // axis reversal
                        std::max(v_exit, -v_entry)) :
                    // v_exit <= v_entry
                    (((v_entry < 0.0f) || (v_exit > 0.0f)) ?
                        // coasting
                        (v_entry - v_exit) :
                        // axis reversal
                        std::max(-v_exit, v_entry));

                float axis_max_jerk = get_axis_max_jerk(static_cast<ETimeMode>(i), static_cast<Axis>(a));
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
            if ((prev.safe_feedrate > vmax_junction_threshold) && (curr.safe_feedrate > vmax_junction_threshold))
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
    float factor = (m_flavor != gcfRepRap && m_units == EUnits::Inches) ? INCHES_TO_MM : 1.0f;

    for (size_t i = 0; i < static_cast<size_t>(ETimeMode::Count); ++i) {
        if (line.has_x())
            set_option_value(m_time_processor.machine_limits.machine_max_acceleration_x, i, line.x() * factor);

        if (line.has_y() && i < m_time_processor.machine_limits.machine_max_acceleration_y.values.size())
            set_option_value(m_time_processor.machine_limits.machine_max_acceleration_y, i, line.y() * factor);

        if (line.has_z() && i < m_time_processor.machine_limits.machine_max_acceleration_z.values.size())
            set_option_value(m_time_processor.machine_limits.machine_max_acceleration_z, i, line.z() * factor);

        if (line.has_e() && i < m_time_processor.machine_limits.machine_max_acceleration_e.values.size())
            set_option_value(m_time_processor.machine_limits.machine_max_acceleration_e, i, line.e() * factor);
    }
}

void GCodeProcessor::process_M203(const GCodeReader::GCodeLine& line)
{
    // see http://reprap.org/wiki/G-code#M203:_Set_maximum_feedrate
    if (m_flavor == gcfRepetier)
        return;

    // see http://reprap.org/wiki/G-code#M203:_Set_maximum_feedrate
    // http://smoothieware.org/supported-g-codes
    float factor = (m_flavor == gcfMarlin || m_flavor == gcfSmoothie) ? 1.0f : MMMIN_TO_MMSEC;

    for (size_t i = 0; i < static_cast<size_t>(ETimeMode::Count); ++i) {
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

void GCodeProcessor::process_M204(const GCodeReader::GCodeLine& line)
{
    float value;
    for (size_t i = 0; i < static_cast<size_t>(ETimeMode::Count); ++i) {
        if (line.has_value('S', value)) {
            // Legacy acceleration format. This format is used by the legacy Marlin, MK2 or MK3 firmware,
            // and it is also generated by Slic3r to control acceleration per extrusion type
            // (there is a separate acceleration settings in Slicer for perimeter, first layer etc).
            set_acceleration(static_cast<ETimeMode>(i), value);
            if (line.has_value('T', value))
                set_option_value(m_time_processor.machine_limits.machine_max_acceleration_retracting, i, value);
        }
        else {
            // New acceleration format, compatible with the upstream Marlin.
            if (line.has_value('P', value))
                set_acceleration(static_cast<ETimeMode>(i), value);
            if (line.has_value('R', value))
                set_option_value(m_time_processor.machine_limits.machine_max_acceleration_retracting, i, value);
            if (line.has_value('T', value)) {
                // Interpret the T value as the travel acceleration in the new Marlin format.
                //FIXME Prusa3D firmware currently does not support travel acceleration value independent from the extruding acceleration value.
                // set_travel_acceleration(value);
            }
        }
    }
}

void GCodeProcessor::process_M205(const GCodeReader::GCodeLine& line)
{
    for (size_t i = 0; i < static_cast<size_t>(ETimeMode::Count); ++i) {
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

void GCodeProcessor::process_M221(const GCodeReader::GCodeLine& line)
{
    float value_s;
    float value_t;
    if (line.has_value('S', value_s) && !line.has_value('T', value_t)) {
        value_s *= 0.01f;
        for (size_t i = 0; i < static_cast<size_t>(ETimeMode::Count); ++i) {
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
    for (size_t i = 0; i < static_cast<size_t>(ETimeMode::Count); ++i) {
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

void GCodeProcessor::process_T(const std::string& command)
{
    if (command.length() > 1) {
        try
        {
            unsigned char id = static_cast<unsigned char>(std::stoi(command.substr(1)));
            if (m_extruder_id != id) {
                unsigned char extruders_count = static_cast<unsigned char>(m_extruder_offsets.size());
                if (id >= extruders_count)
                    BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid toolchange, maybe from a custom gcode.";
                else {
                    unsigned char old_extruder_id = m_extruder_id;
                    m_extruder_id = id;
                    m_cp_color.current = m_extruders_color[id];
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
        catch (...)
        {
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid toolchange (" << command << ").";
        }
    }
}

void GCodeProcessor::store_move_vertex(EMoveType type)
{
    MoveVertex vertex;
    vertex.type = type;
    vertex.extrusion_role = m_extrusion_role;
    vertex.position = Vec3f(m_end_position[X], m_end_position[Y], m_end_position[Z]) + m_extruder_offsets[m_extruder_id];
    vertex.delta_extruder = m_end_position[E] - m_start_position[E];
    vertex.feedrate = m_feedrate;
    vertex.width = m_width;
    vertex.height = m_height;
    vertex.mm3_per_mm = m_mm3_per_mm;
    vertex.fan_speed = m_fan_speed;
    vertex.extruder_id = m_extruder_id;
    vertex.cp_color_id = m_cp_color.current;
    vertex.time = static_cast<float>(m_result.moves.size());
    m_result.moves.emplace_back(vertex);
}

float GCodeProcessor::minimum_feedrate(ETimeMode mode, float feedrate) const
{
    if (m_time_processor.machine_limits.machine_min_extruding_rate.empty())
        return feedrate;

    return std::max(feedrate, get_option_value(m_time_processor.machine_limits.machine_min_extruding_rate, static_cast<size_t>(mode)));
}

float GCodeProcessor::minimum_travel_feedrate(ETimeMode mode, float feedrate) const
{
    if (m_time_processor.machine_limits.machine_min_travel_rate.empty())
        return feedrate;

    return std::max(feedrate, get_option_value(m_time_processor.machine_limits.machine_min_travel_rate, static_cast<size_t>(mode)));
}

float GCodeProcessor::get_axis_max_feedrate(ETimeMode mode, Axis axis) const
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

float GCodeProcessor::get_axis_max_acceleration(ETimeMode mode, Axis axis) const
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

float GCodeProcessor::get_axis_max_jerk(ETimeMode mode, Axis axis) const
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

float GCodeProcessor::get_retract_acceleration(ETimeMode mode) const
{
    return get_option_value(m_time_processor.machine_limits.machine_max_acceleration_retracting, static_cast<size_t>(mode));
}

float GCodeProcessor::get_acceleration(ETimeMode mode) const
{
    size_t id = static_cast<size_t>(mode);
    return (id < m_time_processor.machines.size()) ? m_time_processor.machines[id].acceleration : DEFAULT_ACCELERATION;
}

void GCodeProcessor::set_acceleration(ETimeMode mode, float value)
{
    size_t id = static_cast<size_t>(mode);
    if (id < m_time_processor.machines.size()) {
        float max_acceleration = get_option_value(m_time_processor.machine_limits.machine_max_acceleration_extruding, id);
        m_time_processor.machines[id].acceleration = (max_acceleration == 0.0f) ? value : std::min(value, max_acceleration);
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
    for (size_t i = 0; i < static_cast<size_t>(ETimeMode::Count); ++i) {
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

void GCodeProcessor::simulate_st_synchronize(float additional_time)
{
    for (size_t i = 0; i < static_cast<size_t>(ETimeMode::Count); ++i) {
        m_time_processor.machines[i].simulate_st_synchronize(additional_time);
    }
}

} /* namespace Slic3r */

#endif // ENABLE_GCODE_VIEWER
