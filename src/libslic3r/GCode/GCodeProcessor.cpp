#include "../libslic3r.h"
#include "GCodeProcessor.hpp"

#if ENABLE_GCODE_VIEWER

static const float INCHES_TO_MM = 25.4f;
static const float MMMIN_TO_MMSEC = 1.0f / 60.0f;

namespace Slic3r {

void GCodeProcessor::apply_config(const PrintConfig& config)
{
    m_parser.apply_config(config);
}

void GCodeProcessor::reset()
{
    m_units = EUnits::Millimeters;
    m_global_positioning_type = EPositioningType::Absolute;
    m_e_local_positioning_type = EPositioningType::Absolute;
    
    ::memset(m_start_position.data(), 0, sizeof(AxisCoords));
    ::memset(m_end_position.data(), 0, sizeof(AxisCoords));
    ::memset(m_origin.data(), 0, sizeof(AxisCoords));

    m_feedrate = 0.0f;

    m_moves.clear();
}

void GCodeProcessor::process_file(const std::string& filename)
{
    MoveStep start_step {};
    m_moves.emplace_back(start_step);
    m_parser.parse_file(filename, [this](GCodeReader& reader, const GCodeReader::GCodeLine& line) { process_gcode_line(line); });
    int a = 0;
}

void GCodeProcessor::process_gcode_line(const GCodeReader::GCodeLine& line)
{
/*
    std::cout << line.raw() << std::endl;
*/

    // update start position
    m_start_position = m_end_position;

    std::string cmd = line.cmd();
    std::string comment = line.comment();

    if (cmd.length() > 1)
    {
        // process command lines
        switch (::toupper(cmd[0]))
        {
        case 'G':
            {
                switch (::atoi(&cmd[1]))
                {
                // Move
                case 1: { process_G1(line); }
                default: { break; }
                }
                break;
            }
        case 'M':
            {
                switch (::atoi(&cmd[1]))
                {
                default: { break; }
                }
                break;
            }
        case 'T':
            {
                break;
            }
        default: { break; }
        }
    }
    else if (comment.length() > 1)
    {
        // process tags embedded into comments
        process_comment(line);
    }
}

void GCodeProcessor::process_comment(const GCodeReader::GCodeLine& line)
{
}

void GCodeProcessor::process_G1(const GCodeReader::GCodeLine& line)
{
    auto axis_absolute_position = [this](Axis axis, const GCodeReader::GCodeLine& lineG1)
    {
        bool is_relative = (m_global_positioning_type == EPositioningType::Relative);
        if (axis == E)
            is_relative |= (m_e_local_positioning_type == EPositioningType::Relative);

        if (lineG1.has(Slic3r::Axis(axis)))
        {
            float lengthsScaleFactor = (m_units == EUnits::Inches) ? INCHES_TO_MM : 1.0f;
            float ret = lineG1.value(Slic3r::Axis(axis)) * lengthsScaleFactor;
            return is_relative ? m_start_position[axis] + ret : m_origin[axis] + ret;
        }
        else
            return m_start_position[axis];
    };

    // updates axes positions from line
    for (unsigned char a = X; a <= E; ++a)
    {
        m_end_position[a] = axis_absolute_position((Axis)a, line);
    }

    // updates feedrate from line, if present
    if (line.has_f())
        m_feedrate = line.f() * MMMIN_TO_MMSEC;

    // calculates movement deltas
    float max_abs_delta = 0.0f;
    AxisCoords delta_pos;
    for (unsigned char a = X; a <= E; ++a)
    {
        delta_pos[a] = m_end_position[a] - m_start_position[a];
        max_abs_delta = std::max(max_abs_delta, std::abs(delta_pos[a]));
    }

    // no displacement, return
    if (max_abs_delta == 0.0f)
        return; // <<<<<<<<<<<<<<<<< is this correct for time estimate ?

    // detect move type
    EMoveType move_type = EMoveType::Noop;
    if (delta_pos[E] < 0.0f)
    {
        if ((delta_pos[X] != 0.0f) || (delta_pos[Y] != 0.0f) || (delta_pos[Z] != 0.0f))
            move_type = EMoveType::Travel;
        else
            move_type = EMoveType::Retract;
    }
    else if (delta_pos[E] > 0.0f)
    {
        if ((delta_pos[X] == 0.0f) && (delta_pos[Y] == 0.0f) && (delta_pos[Z] == 0.0f))
            move_type = EMoveType::Unretract;
        else if ((delta_pos[X] != 0.0f) || (delta_pos[Y] != 0.0f))
            move_type = EMoveType::Extrude;
    }
    else if ((delta_pos[X] != 0.0f) || (delta_pos[Y] != 0.0f) || (delta_pos[Z] != 0.0f))
        move_type = EMoveType::Travel;


    MoveStep move_step { m_end_position, m_feedrate, move_type };
    m_moves.emplace_back(move_step);

/*
    std::cout << "start: ";
    for (unsigned char a = X; a <= E; ++a)
    {
        std::cout << m_start_position[a];
        if (a != E)
            std::cout << ", ";
    }
    std::cout << " - end: ";
    for (unsigned char a = X; a <= E; ++a)
    {
        std::cout << m_end_position[a];
        if (a != E)
            std::cout << ", ";
    }
    std::cout << "\n";
*/
}

} /* namespace Slic3r */

#endif // ENABLE_GCODE_VIEWER
