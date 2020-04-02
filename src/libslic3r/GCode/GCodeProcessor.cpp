#include "../libslic3r.h"
#include "GCodeProcessor.hpp"

#include <boost/log/trivial.hpp>

#if ENABLE_GCODE_VIEWER

static const float INCHES_TO_MM = 25.4f;
static const float MMMIN_TO_MMSEC = 1.0f / 60.0f;

static bool is_valid_extrusion_role(int value)
{
    return ((int)Slic3r::erNone <= value) && (value <= (int)Slic3r::erMixed);
}

namespace Slic3r {

const std::string GCodeProcessor::Extrusion_Role_Tag = "_PROCESSOR_EXTRUSION_ROLE:";
const std::string GCodeProcessor::Width_Tag          = "_PROCESSOR_WIDTH:";
const std::string GCodeProcessor::Height_Tag         = "_PROCESSOR_HEIGHT:";

void GCodeProcessor::apply_config(const PrintConfig& config)
{
    m_parser.apply_config(config);

    size_t extruders_count = config.nozzle_diameter.values.size();
    if (m_extruder_offsets.size() != extruders_count)
        m_extruder_offsets.resize(extruders_count);

    for (size_t id = 0; id < extruders_count; ++id)
    {
        Vec2f offset = config.extruder_offset.get_at(id).cast<float>();
        m_extruder_offsets[id] = Vec3f(offset(0), offset(1), 0.0f);
    }
}

void GCodeProcessor::reset()
{
    m_units = EUnits::Millimeters;
    m_global_positioning_type = EPositioningType::Absolute;
    m_e_local_positioning_type = EPositioningType::Absolute;
    m_extruder_offsets = std::vector<Vec3f>(1, Vec3f::Zero());

    std::fill(m_start_position.begin(), m_start_position.end(), 0.0f);
    std::fill(m_end_position.begin(), m_end_position.end(), 0.0f);
    std::fill(m_origin.begin(), m_origin.end(), 0.0f);

    m_feedrate = 0.0f;
    m_width = 0.0f;
    m_height = 0.0f;
    m_extrusion_role = erNone;
    m_extruder_id = 0;

    m_result.reset();
}

void GCodeProcessor::process_file(const std::string& filename)
{
    m_result.moves.emplace_back(MoveVertex());
    m_parser.parse_file(filename, [this](GCodeReader& reader, const GCodeReader::GCodeLine& line) { process_gcode_line(line); });
}

void GCodeProcessor::process_gcode_line(const GCodeReader::GCodeLine& line)
{
/*
    std::cout << line.raw() << std::endl;
*/

    // update start position
    m_start_position = m_end_position;

    std::string cmd = line.cmd();
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
                case 1: { process_G1(line); break; }
                // Set to Absolute Positioning
                case 90: { processG90(line); break; }
                // Set to Relative Positioning
                case 91: { processG91(line); break; }
                // Set Position
                case 92: { processG92(line); break; }
                default: { break; }
                }
                break;
            }
        case 'M':
            {
                switch (::atoi(&cmd[1]))
                {
                // Set extruder to absolute mode
                case 82: { processM82(line); break; }
                // Set extruder to relative mode
                case 83: { processM83(line); break; }
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
    else
    {
        std::string comment = line.comment();
        if (comment.length() > 1)
            // process tags embedded into comments
            process_tags(comment);
    }
}

void GCodeProcessor::process_tags(const std::string& comment)
{
    // extrusion role tag
    size_t pos = comment.find(Extrusion_Role_Tag);
    if (pos != comment.npos)
    {
        int role = std::stoi(comment.substr(pos + Extrusion_Role_Tag.length()));
        if (is_valid_extrusion_role(role))
            m_extrusion_role = static_cast<ExtrusionRole>(role);
        else
        {
            // todo: show some error ?
        }

        return;
    }

    // width tag
    pos = comment.find(Width_Tag);
    if (pos != comment.npos)
    {
        m_width = std::stof(comment.substr(pos + Width_Tag.length()));
        return;
    }

    // height tag
    pos = comment.find(Height_Tag);
    if (pos != comment.npos)
    {
        m_height = std::stof(comment.substr(pos + Height_Tag.length()));
        return;
    }
}

void GCodeProcessor::process_G1(const GCodeReader::GCodeLine& line)
{
    auto absolute_position = [this](Axis axis, const GCodeReader::GCodeLine& lineG1)
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

    auto move_type = [this](const AxisCoords& delta_pos) {
        EMoveType type = EMoveType::Noop;

        if (delta_pos[E] < 0.0f)
        {
            if (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f || delta_pos[Z] != 0.0f)
                type = EMoveType::Travel;
            else
                type = EMoveType::Retract;
        }
        else if (delta_pos[E] > 0.0f)
        {
            if (delta_pos[X] == 0.0f && delta_pos[Y] == 0.0f && delta_pos[Z] == 0.0f)
                type = EMoveType::Unretract;
            else if ((delta_pos[X] != 0.0f) || (delta_pos[Y] != 0.0f))
                type = EMoveType::Extrude;
        }
        else if (delta_pos[X] != 0.0f || delta_pos[Y] != 0.0f || delta_pos[Z] != 0.0f)
            type = EMoveType::Travel;

        if (type == EMoveType::Extrude && (m_width == 0.0f || m_height == 0.0f || !is_valid_extrusion_role(m_extrusion_role)))
            type = EMoveType::Travel;

        return type;
    };

    // updates axes positions from line
    for (unsigned char a = X; a <= E; ++a)
    {
        m_end_position[a] = absolute_position((Axis)a, line);
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
        return;

    // store g1 move
    store_move_vertex(move_type(delta_pos));
}

void GCodeProcessor::processG90(const GCodeReader::GCodeLine& line)
{
    m_global_positioning_type = EPositioningType::Absolute;
}

void GCodeProcessor::processG91(const GCodeReader::GCodeLine& line)
{
    m_global_positioning_type = EPositioningType::Relative;
}

void GCodeProcessor::processG92(const GCodeReader::GCodeLine& line)
{
    float lengthsScaleFactor = (m_units == EUnits::Inches) ? INCHES_TO_MM : 1.0f;
    bool anyFound = false;

    if (line.has_x())
    {
        m_origin[X] = m_end_position[X] - line.x() * lengthsScaleFactor;
        anyFound = true;
    }

    if (line.has_y())
    {
        m_origin[Y] = m_end_position[Y] - line.y() * lengthsScaleFactor;
        anyFound = true;
    }

    if (line.has_z())
    {
        m_origin[Z] = m_end_position[Z] - line.z() * lengthsScaleFactor;
        anyFound = true;
    }

    if (line.has_e())
    {
        // extruder coordinate can grow to the point where its float representation does not allow for proper addition with small increments,
        // we set the value taken from the G92 line as the new current position for it
        m_end_position[E] = line.e() * lengthsScaleFactor;
        anyFound = true;
    }

    if (!anyFound && !line.has_unknown_axis())
    {
        // The G92 may be called for axes that PrusaSlicer does not recognize, for example see GH issue #3510, 
        // where G92 A0 B0 is called although the extruder axis is till E.
        for (unsigned char a = X; a <= E; ++a)
        {
            m_origin[a] = m_end_position[a];
        }
    }
}

void GCodeProcessor::processM82(const GCodeReader::GCodeLine& line)
{
    m_e_local_positioning_type = EPositioningType::Absolute;
}

void GCodeProcessor::processM83(const GCodeReader::GCodeLine& line)
{
    m_e_local_positioning_type = EPositioningType::Relative;
}

void GCodeProcessor::processT(const GCodeReader::GCodeLine& line)
{
    const std::string& cmd = line.cmd();
    if (cmd.length() > 1)
    {
        unsigned int id = (unsigned int)std::stoi(cmd.substr(1));
        if (m_extruder_id != id)
        {
            unsigned int extruders_count = (unsigned int)m_extruder_offsets.size();
            if (id >= extruders_count)
                BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid toolchange, maybe from a custom gcode.";
            else
            {
                m_extruder_id = id;
//                if (_get_cp_color_id() != INT_MAX) <<<<<<<<<<<<<<<<<<< TODO
//                    _set_cp_color_id(m_extruder_color[id]);
            }

            // store tool change move
            store_move_vertex(EMoveType::Tool_change);
        }
    }
}

void GCodeProcessor::store_move_vertex(EMoveType type)
{
    MoveVertex vertex;
    vertex.type = type;
    vertex.extrusion_role = m_extrusion_role;
    vertex.position = Vec3f(m_end_position[0], m_end_position[1], m_end_position[2]) + m_extruder_offsets[m_extruder_id];
    vertex.feedrate = m_feedrate;
    vertex.width = m_width;
    vertex.height = m_height;
    vertex.extruder_id = m_extruder_id;
    m_result.moves.emplace_back(vertex);
}

} /* namespace Slic3r */

#endif // ENABLE_GCODE_VIEWER
