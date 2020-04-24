#include "libslic3r/libslic3r.h"
#include "GCodeProcessor.hpp"

#include <boost/log/trivial.hpp>

#include <float.h>

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
const std::string GCodeProcessor::Mm3_Per_Mm_Tag     = "_PROCESSOR_MM3_PER_MM:";
const std::string GCodeProcessor::Color_Change_Tag   = "_PROCESSOR_COLOR_CHANGE";
const std::string GCodeProcessor::Pause_Print_Tag    = "_PROCESSOR_PAUSE_PRINT";
const std::string GCodeProcessor::Custom_Code_Tag    = "_PROCESSOR_CUSTOM_CODE";

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

unsigned int GCodeProcessor::Result::id = 0;

void GCodeProcessor::apply_config(const PrintConfig& config)
{
    m_parser.apply_config(config);

    m_flavor = config.gcode_flavor;

    size_t extruders_count = config.nozzle_diameter.values.size();

    m_extruder_offsets.resize(extruders_count);
    for (size_t id = 0; id < extruders_count; ++id)
    {
        Vec2f offset = config.extruder_offset.get_at(id).cast<float>();
        m_extruder_offsets[id] = Vec3f(offset(0), offset(1), 0.0f);
    }

    m_extruders_color.resize(extruders_count);
    for (size_t id = 0; id < extruders_count; ++id)
    {
        m_extruders_color[id] = static_cast<unsigned int>(id);
    }
}

void GCodeProcessor::reset()
{
    m_units = EUnits::Millimeters;
    m_global_positioning_type = EPositioningType::Absolute;
    m_e_local_positioning_type = EPositioningType::Absolute;
    m_extruder_offsets = std::vector<Vec3f>(1, Vec3f::Zero());
    m_flavor = gcfRepRap;

    std::fill(m_start_position.begin(), m_start_position.end(), 0.0f);
    std::fill(m_end_position.begin(), m_end_position.end(), 0.0f);
    std::fill(m_origin.begin(), m_origin.end(), 0.0f);
    m_cached_position.reset();

    m_feedrate = 0.0f;
    m_width = 0.0f;
    m_height = 0.0f;
    m_mm3_per_mm = 0.0f;
    m_fan_speed = 0.0f;

    m_extrusion_role = erNone;
    m_extruder_id = 0;
    m_extruders_color = ExtrudersColor();
    m_cp_color.reset();

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
                case 1:  { process_G1(line); break; }  // Move
                case 10: { process_G10(line); break; } // Retract
                case 11: { process_G11(line); break; } // Unretract
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
                case 82:  { process_M82(line); break; }  // Set extruder to absolute mode
                case 83:  { process_M83(line); break; }  // Set extruder to relative mode
                case 106: { process_M106(line); break; } // Set fan speed
                case 107: { process_M107(line); break; } // Disable fan
                case 108: { process_M108(line); break; } // Set tool (Sailfish)
                case 132: { process_M132(line); break; } // Recall stored home offsets
                case 135: { process_M135(line); break; } // Set tool (MakerWare)
                case 401: { process_M401(line); break; } // Repetier: Store x, y and z position
                case 402: { process_M402(line); break; } // Repetier: Go to stored position
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
        try
        {
            int role = std::stoi(comment.substr(pos + Extrusion_Role_Tag.length()));
            if (is_valid_extrusion_role(role))
                m_extrusion_role = static_cast<ExtrusionRole>(role);
            else
            {
                // todo: show some error ?
            }
        }
        catch (...)
        {
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Extrusion Role (" << comment << ").";
        }

        return;
    }

    // width tag
    pos = comment.find(Width_Tag);
    if (pos != comment.npos)
    {
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
    if (pos != comment.npos)
    {
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

    // mm3 per mm tag
    pos = comment.find(Mm3_Per_Mm_Tag);
    if (pos != comment.npos)
    {
        try
        {
            m_mm3_per_mm = std::stof(comment.substr(pos + Mm3_Per_Mm_Tag.length()));
        }
        catch (...)
        {
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Mm3_Per_Mm (" << comment << ").";
        }
        return;
    }

    // color change tag
    pos = comment.find(Color_Change_Tag);
    if (pos != comment.npos)
    {
        pos = comment.find_last_of(",T");
        try
        {
            unsigned char extruder_id = (pos == comment.npos) ? 0 : static_cast<unsigned char>(std::stoi(comment.substr(pos + 1)));

            m_extruders_color[extruder_id] = static_cast<unsigned char>(m_extruder_offsets.size()) + m_cp_color.counter; // color_change position in list of color for preview
            ++m_cp_color.counter;
            if (m_cp_color.counter == UCHAR_MAX)
                m_cp_color.counter = 0;

            if (m_extruder_id == extruder_id)
            {
                m_cp_color.current = m_extruders_color[extruder_id];
                store_move_vertex(EMoveType::Color_change);
            }
        }
        catch (...)
        {
            BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid value for Color_Change (" << comment << ").";
        }

        return;
    }

    // pause print tag
    pos = comment.find(Pause_Print_Tag);
    if (pos != comment.npos)
    {
        store_move_vertex(EMoveType::Pause_Print);
        return;
    }

    // custom code tag
    pos = comment.find(Custom_Code_Tag);
    if (pos != comment.npos)
    {
        store_move_vertex(EMoveType::Custom_GCode);
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
    if (!line.has('P'))
    {
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

void GCodeProcessor::process_M401(const GCodeReader::GCodeLine& line)
{
    if (m_flavor != gcfRepetier)
        return;

    for (unsigned char a = 0; a <= 3; ++a)
    {
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
    for (unsigned char a = X; a <= Z; ++a)
    {
        if (has_xyz || line.has(a))
        {
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

void GCodeProcessor::process_T(const GCodeReader::GCodeLine& line)
{
    process_T(line.cmd());
}

void GCodeProcessor::process_T(const std::string& command)
{
    if (command.length() > 1)
    {
        try
        {
            unsigned char id = static_cast<unsigned char>(std::stoi(command.substr(1)));
            if (m_extruder_id != id)
            {
                unsigned char extruders_count = static_cast<unsigned char>(m_extruder_offsets.size());
                if (id >= extruders_count)
                    BOOST_LOG_TRIVIAL(error) << "GCodeProcessor encountered an invalid toolchange, maybe from a custom gcode.";
                else
                {
                    m_extruder_id = id;
                    m_cp_color.current = m_extruders_color[id];
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
    m_result.moves.emplace_back(vertex);
}

} /* namespace Slic3r */

#endif // ENABLE_GCODE_VIEWER
