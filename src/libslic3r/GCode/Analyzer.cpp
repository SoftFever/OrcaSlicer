#include <memory.h>
#include <string.h>
#include <float.h>

#include "../libslic3r.h"
#include "../PrintConfig.hpp"
#include "../Utils.hpp"
#include "Print.hpp"

#include <boost/log/trivial.hpp>
#if ENABLE_GCODE_VIEWER_DEBUG_OUTPUT
#include <boost/filesystem/path.hpp>
#endif // ENABLE_GCODE_VIEWER_DEBUG_OUTPUT

#include "Analyzer.hpp"
#include "PreviewData.hpp"

static const std::string AXIS_STR = "XYZE";
static const float MMMIN_TO_MMSEC = 1.0f / 60.0f;
static const float INCHES_TO_MM = 25.4f;
static const float DEFAULT_FEEDRATE = 0.0f;
static const unsigned int DEFAULT_EXTRUDER_ID = 0;
static const unsigned int DEFAULT_COLOR_PRINT_ID = 0;
static const Slic3r::Vec3f DEFAULT_START_POSITION = Slic3r::Vec3f::Zero();
static const float DEFAULT_START_EXTRUSION = 0.0f;
static const float DEFAULT_FAN_SPEED = 0.0f;

namespace Slic3r {

const std::string GCodeAnalyzer::Extrusion_Role_Tag = "_ANALYZER_EXTR_ROLE:";
const std::string GCodeAnalyzer::Mm3_Per_Mm_Tag = "_ANALYZER_MM3_PER_MM:";
const std::string GCodeAnalyzer::Width_Tag = "_ANALYZER_WIDTH:";
const std::string GCodeAnalyzer::Height_Tag = "_ANALYZER_HEIGHT:";
const std::string GCodeAnalyzer::Color_Change_Tag = "_ANALYZER_COLOR_CHANGE";
const std::string GCodeAnalyzer::Pause_Print_Tag = "_ANALYZER_PAUSE_PRINT";
const std::string GCodeAnalyzer::Custom_Code_Tag = "_ANALYZER_CUSTOM_CODE";
const std::string GCodeAnalyzer::End_Pause_Print_Or_Custom_Code_Tag = "_ANALYZER_END_PAUSE_PRINT_OR_CUSTOM_CODE";

const float GCodeAnalyzer::Default_mm3_per_mm = 0.0f;
const float GCodeAnalyzer::Default_Width = 0.0f;
const float GCodeAnalyzer::Default_Height = 0.0f;

GCodeAnalyzer::Metadata::Metadata()
    : extrusion_role(erNone)
    , extruder_id(DEFAULT_EXTRUDER_ID)
    , mm3_per_mm(GCodeAnalyzer::Default_mm3_per_mm)
    , width(GCodeAnalyzer::Default_Width)
    , height(GCodeAnalyzer::Default_Height)
    , feedrate(DEFAULT_FEEDRATE)
    , fan_speed(DEFAULT_FAN_SPEED)
    , cp_color_id(DEFAULT_COLOR_PRINT_ID)
{
}

GCodeAnalyzer::Metadata::Metadata(ExtrusionRole extrusion_role, unsigned int extruder_id, float mm3_per_mm, float width, float height, float feedrate, float fan_speed, unsigned int cp_color_id/* = 0*/)
    : extrusion_role(extrusion_role)
    , extruder_id(extruder_id)
    , mm3_per_mm(mm3_per_mm)
    , width(width)
    , height(height)
    , feedrate(feedrate)
    , fan_speed(fan_speed)
    , cp_color_id(cp_color_id)
{
}

bool GCodeAnalyzer::Metadata::operator != (const GCodeAnalyzer::Metadata& other) const
{
    if (extrusion_role != other.extrusion_role)
        return true;

    if (extruder_id != other.extruder_id)
        return true;

    if (mm3_per_mm != other.mm3_per_mm)
        return true;

    if (width != other.width)
        return true;

    if (height != other.height)
        return true;

    if (feedrate != other.feedrate)
        return true;

    if (fan_speed != other.fan_speed)
        return true;

    if (cp_color_id != other.cp_color_id)
        return true;

    return false;
}

GCodeAnalyzer::GCodeMove::GCodeMove(GCodeMove::EType type, ExtrusionRole extrusion_role, unsigned int extruder_id, float mm3_per_mm, float width, float height, float feedrate, const Vec3f& start_position, const Vec3f& end_position, float delta_extruder, float fan_speed, unsigned int cp_color_id/* = 0*/)
    : type(type)
    , data(extrusion_role, extruder_id, mm3_per_mm, width, height, feedrate, fan_speed, cp_color_id)
    , start_position(start_position)
    , end_position(end_position)
    , delta_extruder(delta_extruder)
{
}

GCodeAnalyzer::GCodeMove::GCodeMove(GCodeMove::EType type, const GCodeAnalyzer::Metadata& data, const Vec3f& start_position, const Vec3f& end_position, float delta_extruder)
    : type(type)
    , data(data)
    , start_position(start_position)
    , end_position(end_position)
    , delta_extruder(delta_extruder)
{
}

void GCodeAnalyzer::set_extruders_count(unsigned int count)
{
    m_extruders_count = count;
    for (unsigned int i=0; i<m_extruders_count; i++)
        m_extruder_color[i] = i;
}

void GCodeAnalyzer::reset()
{
    _set_units(Millimeters);
    _set_global_positioning_type(Absolute);
    _set_e_local_positioning_type(Absolute);
    _set_extrusion_role(erNone);
    _set_extruder_id(DEFAULT_EXTRUDER_ID);
    _set_cp_color_id(DEFAULT_COLOR_PRINT_ID);
    _set_mm3_per_mm(Default_mm3_per_mm);
    _set_width(Default_Width);
    _set_height(Default_Height);
    _set_feedrate(DEFAULT_FEEDRATE);
    _set_start_position(DEFAULT_START_POSITION);
    _set_start_extrusion(DEFAULT_START_EXTRUSION);
    _set_fan_speed(DEFAULT_FAN_SPEED);
    _reset_axes_position();
    _reset_axes_origin();
    _reset_cached_position();

    m_moves_map.clear();
    m_extruder_offsets.clear();
    m_extruders_count = 1;
    m_extruder_color.clear();
}

const std::string& GCodeAnalyzer::process_gcode(const std::string& gcode)
{
    m_process_output = "";

    m_parser.parse_buffer(gcode,
        [this](GCodeReader& reader, const GCodeReader::GCodeLine& line)
    { this->_process_gcode_line(reader, line); });

    return m_process_output;
}

void GCodeAnalyzer::calc_gcode_preview_data(GCodePreviewData& preview_data, std::function<void()> cancel_callback)
{
    // resets preview data
    preview_data.reset();

    // calculates extrusion layers
    _calc_gcode_preview_extrusion_layers(preview_data, cancel_callback);

    // calculates travel
    _calc_gcode_preview_travel(preview_data, cancel_callback);

    // calculates retractions
    _calc_gcode_preview_retractions(preview_data, cancel_callback);

    // calculates unretractions
    _calc_gcode_preview_unretractions(preview_data, cancel_callback);
}

bool GCodeAnalyzer::is_valid_extrusion_role(ExtrusionRole role)
{
    return ((erPerimeter <= role) && (role < erMixed));
}

#if ENABLE_GCODE_VIEWER_DEBUG_OUTPUT
void GCodeAnalyzer::open_debug_output_file()
{
    boost::filesystem::path path("d:/analyzer.output");
    m_debug_output.open(path.string());
}

void GCodeAnalyzer::close_debug_output_file()
{
    m_debug_output.close();
}
#endif // ENABLE_GCODE_VIEWER_DEBUG_OUTPUT

void GCodeAnalyzer::_process_gcode_line(GCodeReader&, const GCodeReader::GCodeLine& line)
{
    // processes 'special' comments contained in line
    if (_process_tags(line))
    {
#if 0
        // DEBUG ONLY: puts the line back into the gcode
        m_process_output += line.raw() + "\n";
#endif
        return;
    }

    // sets new start position/extrusion
    _set_start_position(_get_end_position());
    _set_start_extrusion(_get_axis_position(E));

    // processes 'normal' gcode lines
    std::string cmd = line.cmd();
    if (cmd.length() > 1)
    {
        switch (::toupper(cmd[0]))
        {
        case 'G':
            {
                switch (::atoi(&cmd[1]))
                {
                case 1: // Move
                    {
                        _processG1(line);
                        break;
                    }
                case 10: // Retract
                    {
                        _processG10(line);
                        break;
                    }
                case 11: // Unretract
                    {
                        _processG11(line);
                        break;
                    }
                case 22: // Firmware controlled Retract
                    {
                        _processG22(line);
                        break;
                    }
                case 23: // Firmware controlled Unretract
                    {
                        _processG23(line);
                        break;
                    }
                case 90: // Set to Absolute Positioning
                    {
                        _processG90(line);
                        break;
                    }
                case 91: // Set to Relative Positioning
                    {
                        _processG91(line);
                        break;
                    }
                case 92: // Set Position
                    {
                        _processG92(line);
                        break;
                    }
                }

                break;
            }
        case 'M':
            {
                switch (::atoi(&cmd[1]))
                {
                case 82: // Set extruder to absolute mode
                    {
                        _processM82(line);
                        break;
                    }
                case 83: // Set extruder to relative mode
                    {
                        _processM83(line);
                        break;
                    }
                case 106: // Set fan speed
                    {
                        _processM106(line);
                        break;
                    }
                case 107: // Disable fan
                    {
                        _processM107(line);
                        break;
                    }
                case 108:
                case 135:
                    {
                        // these are used by MakerWare and Sailfish firmwares
                        // for tool changing - we can process it in one place
                        _processM108orM135(line);
                        break;
                    }
                case 132: // Recall stored home offsets
                    {
                        _processM132(line);
                        break;
                    }
                case 401: // Repetier: Store x, y and z position
                    {
                        _processM401(line);
                        break;
                    }
                case 402: // Repetier: Go to stored position
                    {
                        _processM402(line);
                        break;
                    }
                }

                break;
            }
        case 'T': // Select Tools
            {
                _processT(line);
                break;
            }
        }
    }

    // puts the line back into the gcode
    m_process_output += line.raw() + "\n";
}

void GCodeAnalyzer::_processG1(const GCodeReader::GCodeLine& line)
{
    auto axis_absolute_position = [this](GCodeAnalyzer::EAxis axis, const GCodeReader::GCodeLine& lineG1) -> float
    {
        bool is_relative = (_get_global_positioning_type() == Relative);
        if (axis == E)
            is_relative |= (_get_e_local_positioning_type() == Relative);

        if (lineG1.has(Slic3r::Axis(axis)))
        {
            float lengthsScaleFactor = (_get_units() == GCodeAnalyzer::Inches) ? INCHES_TO_MM : 1.0f;
            float ret = lineG1.value(Slic3r::Axis(axis)) * lengthsScaleFactor;
            return is_relative ? _get_axis_position(axis) + ret : _get_axis_origin(axis) + ret;
        }
        else
            return _get_axis_position(axis);
    };

    // updates axes positions from line

    float new_pos[Num_Axis];
    for (unsigned char a = X; a < Num_Axis; ++a)
    {
        new_pos[a] = axis_absolute_position((EAxis)a, line);
    }

    // updates feedrate from line, if present
    if (line.has_f())
        _set_feedrate(line.f() * MMMIN_TO_MMSEC);

    // calculates movement deltas
    float delta_pos[Num_Axis];
    for (unsigned char a = X; a < Num_Axis; ++a)
    {
        delta_pos[a] = new_pos[a] - _get_axis_position((EAxis)a);
    }

    // Detects move type
    GCodeMove::EType type = GCodeMove::Noop;

    if (delta_pos[E] < 0.0f)
    {
        if ((delta_pos[X] != 0.0f) || (delta_pos[Y] != 0.0f) || (delta_pos[Z] != 0.0f))
            type = GCodeMove::Move;
        else
            type = GCodeMove::Retract;
    }
    else if (delta_pos[E] > 0.0f)
    {
        if ((delta_pos[X] == 0.0f) && (delta_pos[Y] == 0.0f) && (delta_pos[Z] == 0.0f))
            type = GCodeMove::Unretract;
        else if ((delta_pos[X] != 0.0f) || (delta_pos[Y] != 0.0f))
            type = GCodeMove::Extrude;
    }
    else if ((delta_pos[X] != 0.0f) || (delta_pos[Y] != 0.0f) || (delta_pos[Z] != 0.0f))
        type = GCodeMove::Move;

    ExtrusionRole role = _get_extrusion_role();
    if ((type == GCodeMove::Extrude) && ((_get_width() == 0.0f) || (_get_height() == 0.0f) || !is_valid_extrusion_role(role)))
        type = GCodeMove::Move;

    // updates axis positions
    for (unsigned char a = X; a < Num_Axis; ++a)
    {
        _set_axis_position((EAxis)a, new_pos[a]);
    }

    // stores the move
    if (type != GCodeMove::Noop)
        _store_move(type);
}

void GCodeAnalyzer::_processG10(const GCodeReader::GCodeLine& line)
{
    // stores retract move
    _store_move(GCodeMove::Retract);
}

void GCodeAnalyzer::_processG11(const GCodeReader::GCodeLine& line)
{
    // stores unretract move
    _store_move(GCodeMove::Unretract);
}

void GCodeAnalyzer::_processG22(const GCodeReader::GCodeLine& line)
{
    // stores retract move
    _store_move(GCodeMove::Retract);
}

void GCodeAnalyzer::_processG23(const GCodeReader::GCodeLine& line)
{
    // stores unretract move
    _store_move(GCodeMove::Unretract);
}

void GCodeAnalyzer::_processG90(const GCodeReader::GCodeLine& line)
{
    _set_global_positioning_type(Absolute);
}

void GCodeAnalyzer::_processG91(const GCodeReader::GCodeLine& line)
{
    _set_global_positioning_type(Relative);
}

void GCodeAnalyzer::_processG92(const GCodeReader::GCodeLine& line)
{
    float lengthsScaleFactor = (_get_units() == Inches) ? INCHES_TO_MM : 1.0f;
    bool anyFound = false;

    if (line.has_x())
    {
        _set_axis_origin(X, _get_axis_position(X) - line.x() * lengthsScaleFactor);
        anyFound = true;
    }

    if (line.has_y())
    {
        _set_axis_origin(Y, _get_axis_position(Y) - line.y() * lengthsScaleFactor);
        anyFound = true;
    }

    if (line.has_z())
    {
        _set_axis_origin(Z, _get_axis_position(Z) - line.z() * lengthsScaleFactor);
        anyFound = true;
    }

    if (line.has_e())
    {
        // extruder coordinate can grow to the point where its float representation does not allow for proper addition with small increments,
        // we set the value taken from the G92 line as the new current position for it
        _set_axis_position(E, line.e() * lengthsScaleFactor);
        anyFound = true;
    }

    if (!anyFound && ! line.has_unknown_axis())
    {
    	// The G92 may be called for axes that PrusaSlicer does not recognize, for example see GH issue #3510, 
    	// where G92 A0 B0 is called although the extruder axis is till E.
        for (unsigned char a = X; a < Num_Axis; ++a)
        {
            _set_axis_origin((EAxis)a, _get_axis_position((EAxis)a));
        }
    }
}

void GCodeAnalyzer::_processM82(const GCodeReader::GCodeLine& line)
{
    _set_e_local_positioning_type(Absolute);
}

void GCodeAnalyzer::_processM83(const GCodeReader::GCodeLine& line)
{
    _set_e_local_positioning_type(Relative);
}

void GCodeAnalyzer::_processM106(const GCodeReader::GCodeLine& line)
{
    if (!line.has('P'))
    {
        // The absence of P means the print cooling fan, so ignore anything else.
        float new_fan_speed;
        if (line.has_value('S', new_fan_speed))
            _set_fan_speed((100.0f / 255.0f) * new_fan_speed);
        else
            _set_fan_speed(100.0f);
    }
}

void GCodeAnalyzer::_processM107(const GCodeReader::GCodeLine& line)
{
    _set_fan_speed(0.0f);
}

void GCodeAnalyzer::_processM108orM135(const GCodeReader::GCodeLine& line)
{
    // These M-codes are used by MakerWare and Sailfish to change active tool.
    // They have to be processed otherwise toolchanges will be unrecognised
    // by the analyzer - see https://github.com/prusa3d/PrusaSlicer/issues/2566

    size_t code = ::atoi(&(line.cmd()[1]));
    if ((code == 108 && m_gcode_flavor == gcfSailfish)
        || (code == 135 && m_gcode_flavor == gcfMakerWare)) {

        std::string cmd = line.raw();
        size_t T_pos = cmd.find("T");
        if (T_pos != std::string::npos) {
            cmd = cmd.substr(T_pos);
            _processT(cmd);
        }
    }
}

void GCodeAnalyzer::_processM132(const GCodeReader::GCodeLine& line)
{
    // This command is used by Makerbot to load the current home position from EEPROM
    // see: https://github.com/makerbot/s3g/blob/master/doc/GCodeProtocol.md
    // Using this command to reset the axis origin to zero helps in fixing: https://github.com/prusa3d/PrusaSlicer/issues/3082

    if (line.has_x())
        _set_axis_origin(X, 0.0f);

    if (line.has_y())
        _set_axis_origin(Y, 0.0f);

    if (line.has_z())
        _set_axis_origin(Z, 0.0f);

    if (line.has_e())
        _set_axis_origin(E, 0.0f);
}

void GCodeAnalyzer::_processM401(const GCodeReader::GCodeLine& line)
{
    if (m_gcode_flavor != gcfRepetier)
        return;

    for (unsigned char a = 0; a <= 3; ++a)
    {
        _set_cached_position(a, _get_axis_position((EAxis)a));
    }
    _set_cached_position(4, _get_feedrate());
}

void GCodeAnalyzer::_processM402(const GCodeReader::GCodeLine& line)
{
    if (m_gcode_flavor != gcfRepetier)
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
            p = _get_cached_position(a);
            if (p != FLT_MAX)
                _set_axis_position((EAxis)a, p);
        }
    }

    p = _get_cached_position(E);
    if (p != FLT_MAX)
        _set_axis_position(E, p);

    p = FLT_MAX;
    if (!line.has_value(4, p))
        p = _get_cached_position(4);

    if (p != FLT_MAX)
        _set_feedrate(p);
}

void GCodeAnalyzer::_reset_cached_position()
{
    for (unsigned char a = 0; a <= 4; ++a)
    {
        m_state.cached_position[a] = FLT_MAX;
    }
}

void GCodeAnalyzer::_processT(const std::string& cmd)
{
    if (cmd.length() > 1)
    {
        unsigned int id = (unsigned int)::strtol(cmd.substr(1).c_str(), nullptr, 10);
        if (_get_extruder_id() != id)
        {
            if (id >= m_extruders_count)
            {
                if (m_extruders_count > 1)
                    BOOST_LOG_TRIVIAL(error) << "GCodeAnalyzer encountered an invalid toolchange, maybe from a custom gcode.";
            }
            else
            {
                _set_extruder_id(id);
                if (_get_cp_color_id() != INT_MAX)
                    _set_cp_color_id(m_extruder_color[id]);
            }

            // stores tool change move
            _store_move(GCodeMove::Tool_change);
        }
    }
}

void GCodeAnalyzer::_processT(const GCodeReader::GCodeLine& line)
{
    _processT(line.cmd());
}

bool GCodeAnalyzer::_process_tags(const GCodeReader::GCodeLine& line)
{
    std::string comment = line.comment();

    // extrusion role tag
    size_t pos = comment.find(Extrusion_Role_Tag);
    if (pos != comment.npos)
    {
        _process_extrusion_role_tag(comment, pos);
        return true;
    }

    // mm3 per mm tag
    pos = comment.find(Mm3_Per_Mm_Tag);
    if (pos != comment.npos)
    {
        _process_mm3_per_mm_tag(comment, pos);
        return true;
    }

    // width tag
    pos = comment.find(Width_Tag);
    if (pos != comment.npos)
    {
        _process_width_tag(comment, pos);
        return true;
    }

    // height tag
    pos = comment.find(Height_Tag);
    if (pos != comment.npos)
    {
        _process_height_tag(comment, pos);
        return true;
    }

    // color change tag
    pos = comment.find(Color_Change_Tag);
    if (pos != comment.npos)
    {
        pos = comment.find_last_of(",T");
        unsigned extruder = pos == comment.npos ? 0 : std::stoi(comment.substr(pos + 1, comment.npos));
        _process_color_change_tag(extruder);
        return true;
    }

    // color change tag
    pos = comment.find(Pause_Print_Tag);
    if (pos != comment.npos)
    {
        _process_pause_print_or_custom_code_tag();
        return true;
    }

    // color change tag
    pos = comment.find(Custom_Code_Tag);
    if (pos != comment.npos)
    {
        _process_pause_print_or_custom_code_tag();
        return true;
    }

    // color change tag
    pos = comment.find(End_Pause_Print_Or_Custom_Code_Tag);
    if (pos != comment.npos)
    {
        _process_end_pause_print_or_custom_code_tag();
        return true;
    }

    return false;
}

void GCodeAnalyzer::_process_extrusion_role_tag(const std::string& comment, size_t pos)
{
    int role = (int)::strtol(comment.substr(pos + Extrusion_Role_Tag.length()).c_str(), nullptr, 10);
    if (_is_valid_extrusion_role(role))
        _set_extrusion_role((ExtrusionRole)role);
    else
    {
        // todo: show some error ?
    }
}

void GCodeAnalyzer::_process_mm3_per_mm_tag(const std::string& comment, size_t pos)
{
    _set_mm3_per_mm((float)::strtod(comment.substr(pos + Mm3_Per_Mm_Tag.length()).c_str(), nullptr));
}

void GCodeAnalyzer::_process_width_tag(const std::string& comment, size_t pos)
{
    _set_width((float)::strtod(comment.substr(pos + Width_Tag.length()).c_str(), nullptr));
}

void GCodeAnalyzer::_process_height_tag(const std::string& comment, size_t pos)
{
    _set_height((float)::strtod(comment.substr(pos + Height_Tag.length()).c_str(), nullptr));
}

void GCodeAnalyzer::_process_color_change_tag(unsigned extruder)
{
    m_extruder_color[extruder] = m_extruders_count + m_state.cp_color_counter; // color_change position in list of color for preview
    m_state.cp_color_counter++;

    if (_get_extruder_id() == extruder)
        _set_cp_color_id(m_extruder_color[extruder]);
}

void GCodeAnalyzer::_process_pause_print_or_custom_code_tag()
{
    _set_cp_color_id(INT_MAX);
}

void GCodeAnalyzer::_process_end_pause_print_or_custom_code_tag()
{
    if (_get_cp_color_id() == INT_MAX)
        _set_cp_color_id(m_extruder_color[_get_extruder_id()]);
}

void GCodeAnalyzer::_set_units(GCodeAnalyzer::EUnits units)
{
    m_state.units = units;
}

GCodeAnalyzer::EUnits GCodeAnalyzer::_get_units() const
{
    return m_state.units;
}

void GCodeAnalyzer::_set_global_positioning_type(GCodeAnalyzer::EPositioningType type)
{
    m_state.global_positioning_type = type;
}

GCodeAnalyzer::EPositioningType GCodeAnalyzer::_get_global_positioning_type() const
{
    return m_state.global_positioning_type;
}

void GCodeAnalyzer::_set_e_local_positioning_type(GCodeAnalyzer::EPositioningType type)
{
    m_state.e_local_positioning_type = type;
}

GCodeAnalyzer::EPositioningType GCodeAnalyzer::_get_e_local_positioning_type() const
{
    return m_state.e_local_positioning_type;
}

void GCodeAnalyzer::_set_extrusion_role(ExtrusionRole extrusion_role)
{
    m_state.data.extrusion_role = extrusion_role;
}

ExtrusionRole GCodeAnalyzer::_get_extrusion_role() const
{
    return m_state.data.extrusion_role;
}

void GCodeAnalyzer::_set_extruder_id(unsigned int id)
{
    m_state.data.extruder_id = id;
}

unsigned int GCodeAnalyzer::_get_extruder_id() const
{
    return m_state.data.extruder_id;
}

void GCodeAnalyzer::_set_cp_color_id(unsigned int id)
{
    m_state.data.cp_color_id = id;
}

unsigned int GCodeAnalyzer::_get_cp_color_id() const
{
    return m_state.data.cp_color_id;
}

void GCodeAnalyzer::_set_mm3_per_mm(float value)
{
    m_state.data.mm3_per_mm = value;
}

float GCodeAnalyzer::_get_mm3_per_mm() const
{
    return m_state.data.mm3_per_mm;
}

void GCodeAnalyzer::_set_width(float width)
{
    m_state.data.width = width;
}

float GCodeAnalyzer::_get_width() const
{
    return m_state.data.width;
}

void GCodeAnalyzer::_set_height(float height)
{
    m_state.data.height = height;
}

float GCodeAnalyzer::_get_height() const
{
    return m_state.data.height;
}

void GCodeAnalyzer::_set_feedrate(float feedrate_mm_sec)
{
    m_state.data.feedrate = feedrate_mm_sec;
}

float GCodeAnalyzer::_get_feedrate() const
{
    return m_state.data.feedrate;
}

void GCodeAnalyzer::_set_fan_speed(float fan_speed_percentage)
{
    m_state.data.fan_speed = fan_speed_percentage;
}

float GCodeAnalyzer::_get_fan_speed() const
{
    return m_state.data.fan_speed;
}

void GCodeAnalyzer::_set_axis_position(EAxis axis, float position)
{
    m_state.position[axis] = position;
}

float GCodeAnalyzer::_get_axis_position(EAxis axis) const
{
    return m_state.position[axis];
}

void GCodeAnalyzer::_set_axis_origin(EAxis axis, float position)
{
    m_state.origin[axis] = position;
}

float GCodeAnalyzer::_get_axis_origin(EAxis axis) const
{
    return m_state.origin[axis];
}

void GCodeAnalyzer::_reset_axes_position()
{
    ::memset((void*)m_state.position, 0, Num_Axis * sizeof(float));
}

void GCodeAnalyzer::_reset_axes_origin()
{
    ::memset((void*)m_state.origin, 0, Num_Axis * sizeof(float));
}

void GCodeAnalyzer::_set_start_position(const Vec3f& position)
{
    m_state.start_position = position;
}

const Vec3f& GCodeAnalyzer::_get_start_position() const
{
    return m_state.start_position;
}

void GCodeAnalyzer::_set_cached_position(unsigned char axis, float position)
{
    if ((0 <= axis) || (axis <= 4))
        m_state.cached_position[axis] = position;
}

float GCodeAnalyzer::_get_cached_position(unsigned char axis) const
{
    return ((0 <= axis) || (axis <= 4)) ? m_state.cached_position[axis] : FLT_MAX;
}

void GCodeAnalyzer::_set_start_extrusion(float extrusion)
{
    m_state.start_extrusion = extrusion;
}

float GCodeAnalyzer::_get_start_extrusion() const
{
    return m_state.start_extrusion;
}

float GCodeAnalyzer::_get_delta_extrusion() const
{
    return _get_axis_position(E) - m_state.start_extrusion;
}

Vec3f GCodeAnalyzer::_get_end_position() const
{
    return Vec3f(m_state.position[X], m_state.position[Y], m_state.position[Z]);
}

void GCodeAnalyzer::_store_move(GCodeAnalyzer::GCodeMove::EType type)
{
    // if type non mapped yet, map it
    TypeToMovesMap::iterator it = m_moves_map.find(type);
    if (it == m_moves_map.end())
        it = m_moves_map.insert(TypeToMovesMap::value_type(type, GCodeMovesList())).first;

    // store move
    Vec3f extruder_offset = Vec3f::Zero();
    unsigned int extruder_id = _get_extruder_id();
    ExtruderOffsetsMap::iterator extr_it = m_extruder_offsets.find(extruder_id);
    if (extr_it != m_extruder_offsets.end())
        extruder_offset = Vec3f((float)extr_it->second(0), (float)extr_it->second(1), 0.0f);

    Vec3f start_position = _get_start_position() + extruder_offset;
    Vec3f end_position = _get_end_position() + extruder_offset;
    it->second.emplace_back(type, _get_extrusion_role(), extruder_id, _get_mm3_per_mm(), _get_width(), _get_height(), _get_feedrate(), start_position, end_position, _get_delta_extrusion(), _get_fan_speed(), _get_cp_color_id());

#if ENABLE_GCODE_VIEWER_DEBUG_OUTPUT
    if (m_debug_output.good())
    {
        m_debug_output << std::to_string((int)type);
        m_debug_output << ", " << std::to_string((int)_get_extrusion_role());
        m_debug_output << ", " << Slic3r::to_string((Vec3d)_get_end_position().cast<double>());
        m_debug_output << ", " << std::to_string(extruder_id);
        m_debug_output << ", " << std::to_string(_get_feedrate());
        m_debug_output << ", " << std::to_string(_get_width());
        m_debug_output << ", " << std::to_string(_get_height());
        m_debug_output << "\n";
    }
#endif // ENABLE_GCODE_VIEWER_DEBUG_OUTPUT
}

bool GCodeAnalyzer::_is_valid_extrusion_role(int value) const
{
    return ((int)erNone <= value) && (value <= (int)erMixed);
}

void GCodeAnalyzer::_calc_gcode_preview_extrusion_layers(GCodePreviewData& preview_data, std::function<void()> cancel_callback)
{
    struct Helper
    {
        static GCodePreviewData::Extrusion::Layer& get_layer_at_z(GCodePreviewData::Extrusion::LayersList& layers, float z)
        {
            //FIXME this has a terrible time complexity
            for (GCodePreviewData::Extrusion::Layer& layer : layers)
            {
                // if layer found, return it
                if (layer.z == z)
                    return layer;
            }

            // if layer not found, create and return it
            layers.emplace_back(z, GCodePreviewData::Extrusion::Paths());
            return layers.back();
        }

        static void store_polyline(const Polyline& polyline, const Metadata& data, float z, GCodePreviewData& preview_data)
        {
            // if the polyline is valid, create the extrusion path from it and store it
            if (polyline.is_valid())
            {
				auto& paths = get_layer_at_z(preview_data.extrusion.layers, z).paths;
				paths.emplace_back(GCodePreviewData::Extrusion::Path());
				GCodePreviewData::Extrusion::Path &path = paths.back();
                path.polyline = polyline;
				path.extrusion_role = data.extrusion_role;
                path.mm3_per_mm = data.mm3_per_mm;
                path.width = data.width;
				path.height = data.height;
                path.feedrate = data.feedrate;
                path.extruder_id = data.extruder_id;
                path.cp_color_id = data.cp_color_id;
                path.fan_speed = data.fan_speed;
            }
        }
    };

    TypeToMovesMap::iterator extrude_moves = m_moves_map.find(GCodeMove::Extrude);
    if (extrude_moves == m_moves_map.end())
        return;

    Metadata data;
    float z = FLT_MAX;
    Polyline polyline;
    Vec3f position(FLT_MAX, FLT_MAX, FLT_MAX);
    float volumetric_rate = FLT_MAX;
    GCodePreviewData::Range height_range;
    GCodePreviewData::Range width_range;
    GCodePreviewData::MultiRange<GCodePreviewData::FeedrateKind> feedrate_range;
    GCodePreviewData::Range volumetric_rate_range;
    GCodePreviewData::Range fan_speed_range;

    // to avoid to call the callback too often
    unsigned int cancel_callback_threshold = (unsigned int)std::max((int)extrude_moves->second.size() / 25, 1);
    unsigned int cancel_callback_curr = 0;

    // constructs the polylines while traversing the moves
    for (const GCodeMove& move : extrude_moves->second)
    {
        // to avoid to call the callback too often
        cancel_callback_curr = (cancel_callback_curr + 1) % cancel_callback_threshold;
        if (cancel_callback_curr == 0)
            cancel_callback();

        if ((data != move.data) || (z != move.start_position.z()) || (position != move.start_position) || (volumetric_rate != move.data.feedrate * move.data.mm3_per_mm))
        {
            // store current polyline
            polyline.remove_duplicate_points();
            Helper::store_polyline(polyline, data, z, preview_data);

            // reset current polyline
            polyline = Polyline();

            // add both vertices of the move
            polyline.append(Point(scale_(move.start_position.x()), scale_(move.start_position.y())));
            polyline.append(Point(scale_(move.end_position.x()), scale_(move.end_position.y())));

            // update current values
            data = move.data;
            z = (float)move.start_position.z();
            volumetric_rate = move.data.feedrate * move.data.mm3_per_mm;
            height_range.update_from(move.data.height);
            width_range.update_from(move.data.width);
            feedrate_range.update_from(move.data.feedrate, GCodePreviewData::FeedrateKind::EXTRUSION);
            volumetric_rate_range.update_from(volumetric_rate);
            fan_speed_range.update_from(move.data.fan_speed);
        }
        else
            // append end vertex of the move to current polyline
            polyline.append(Point(scale_(move.end_position.x()), scale_(move.end_position.y())));

        // update current values
        position = move.end_position;
    }

    // store last polyline
    polyline.remove_duplicate_points();
    Helper::store_polyline(polyline, data, z, preview_data);

    // updates preview ranges data
    preview_data.ranges.height.update_from(height_range);
    preview_data.ranges.width.update_from(width_range);
    preview_data.ranges.feedrate.update_from(feedrate_range);
    preview_data.ranges.volumetric_rate.update_from(volumetric_rate_range);
    preview_data.ranges.fan_speed.update_from(fan_speed_range);

    // we need to sort the layers by their z as they can be shuffled in case of sequential prints
    std::sort(preview_data.extrusion.layers.begin(), preview_data.extrusion.layers.end(), [](const GCodePreviewData::Extrusion::Layer& l1, const GCodePreviewData::Extrusion::Layer& l2)->bool { return l1.z < l2.z; });
}

void GCodeAnalyzer::_calc_gcode_preview_travel(GCodePreviewData& preview_data, std::function<void()> cancel_callback)
{
    struct Helper
    {
        static void store_polyline(const Polyline3& polyline, GCodePreviewData::Travel::EType type, GCodePreviewData::Travel::Polyline::EDirection direction, 
            float feedrate, unsigned int extruder_id, GCodePreviewData& preview_data)
        {
            // if the polyline is valid, store it
            if (polyline.is_valid())
                preview_data.travel.polylines.emplace_back(type, direction, feedrate, extruder_id, polyline);
        }
    };

    TypeToMovesMap::iterator travel_moves = m_moves_map.find(GCodeMove::Move);
    if (travel_moves == m_moves_map.end())
        return;

    Polyline3 polyline;
    Vec3f position(FLT_MAX, FLT_MAX, FLT_MAX);
    GCodePreviewData::Travel::EType type = GCodePreviewData::Travel::Num_Types;
    GCodePreviewData::Travel::Polyline::EDirection direction = GCodePreviewData::Travel::Polyline::Num_Directions;
    float feedrate = FLT_MAX;
    unsigned int extruder_id = -1;

    GCodePreviewData::Range height_range;
    GCodePreviewData::Range width_range;
    GCodePreviewData::MultiRange<GCodePreviewData::FeedrateKind> feedrate_range;

    // to avoid to call the callback too often
    unsigned int cancel_callback_threshold = (unsigned int)std::max((int)travel_moves->second.size() / 25, 1);
    unsigned int cancel_callback_curr = 0;

    // constructs the polylines while traversing the moves
    for (const GCodeMove& move : travel_moves->second)
    {
        cancel_callback_curr = (cancel_callback_curr + 1) % cancel_callback_threshold;
        if (cancel_callback_curr == 0)
            cancel_callback();

        GCodePreviewData::Travel::EType move_type = (move.delta_extruder < 0.0f) ? GCodePreviewData::Travel::Retract : ((move.delta_extruder > 0.0f) ? GCodePreviewData::Travel::Extrude : GCodePreviewData::Travel::Move);
        GCodePreviewData::Travel::Polyline::EDirection move_direction = ((move.start_position.x() != move.end_position.x()) || (move.start_position.y() != move.end_position.y())) ? GCodePreviewData::Travel::Polyline::Generic : GCodePreviewData::Travel::Polyline::Vertical;

        if ((type != move_type) || (direction != move_direction) || (feedrate != move.data.feedrate) || (position != move.start_position) || (extruder_id != move.data.extruder_id))
        {
            // store current polyline
            polyline.remove_duplicate_points();
            Helper::store_polyline(polyline, type, direction, feedrate, extruder_id, preview_data);

            // reset current polyline
            polyline = Polyline3();

            // add both vertices of the move
            polyline.append(Vec3crd((int)scale_(move.start_position.x()), (int)scale_(move.start_position.y()), (int)scale_(move.start_position.z())));
            polyline.append(Vec3crd((int)scale_(move.end_position.x()), (int)scale_(move.end_position.y()), (int)scale_(move.end_position.z())));
        }
        else
            // append end vertex of the move to current polyline
            polyline.append(Vec3crd((int)scale_(move.end_position.x()), (int)scale_(move.end_position.y()), (int)scale_(move.end_position.z())));

        // update current values
        position = move.end_position;
        type = move_type;
        feedrate = move.data.feedrate;
        extruder_id = move.data.extruder_id;
        height_range.update_from(move.data.height);
        width_range.update_from(move.data.width);
        feedrate_range.update_from(move.data.feedrate, GCodePreviewData::FeedrateKind::TRAVEL);
    }

    // store last polyline
    polyline.remove_duplicate_points();
    Helper::store_polyline(polyline, type, direction, feedrate, extruder_id, preview_data);

    // updates preview ranges data
    preview_data.ranges.height.update_from(height_range);
    preview_data.ranges.width.update_from(width_range);
    preview_data.ranges.feedrate.update_from(feedrate_range);

    // we need to sort the polylines by their min z as they can be shuffled in case of sequential prints
    std::sort(preview_data.travel.polylines.begin(), preview_data.travel.polylines.end(),
        [](const GCodePreviewData::Travel::Polyline& p1, const GCodePreviewData::Travel::Polyline& p2)->bool
    { return unscale<double>(p1.polyline.bounding_box().min(2)) < unscale<double>(p2.polyline.bounding_box().min(2)); });
}

void GCodeAnalyzer::_calc_gcode_preview_retractions(GCodePreviewData& preview_data, std::function<void()> cancel_callback)
{
    TypeToMovesMap::iterator retraction_moves = m_moves_map.find(GCodeMove::Retract);
    if (retraction_moves == m_moves_map.end())
        return;

    // to avoid to call the callback too often
    unsigned int cancel_callback_threshold = (unsigned int)std::max((int)retraction_moves->second.size() / 25, 1);
    unsigned int cancel_callback_curr = 0;

    for (const GCodeMove& move : retraction_moves->second)
    {
        cancel_callback_curr = (cancel_callback_curr + 1) % cancel_callback_threshold;
        if (cancel_callback_curr == 0)
            cancel_callback();

        // store position
        Vec3crd position((int)scale_(move.start_position.x()), (int)scale_(move.start_position.y()), (int)scale_(move.start_position.z()));
        preview_data.retraction.positions.emplace_back(position, move.data.width, move.data.height);
    }

    // we need to sort the positions by their z as they can be shuffled in case of sequential prints
    std::sort(preview_data.retraction.positions.begin(), preview_data.retraction.positions.end(),
        [](const GCodePreviewData::Retraction::Position& p1, const GCodePreviewData::Retraction::Position& p2)->bool
    { return unscale<double>(p1.position(2)) < unscale<double>(p2.position(2)); });
}

void GCodeAnalyzer::_calc_gcode_preview_unretractions(GCodePreviewData& preview_data, std::function<void()> cancel_callback)
{
    TypeToMovesMap::iterator unretraction_moves = m_moves_map.find(GCodeMove::Unretract);
    if (unretraction_moves == m_moves_map.end())
        return;

    // to avoid to call the callback too often
    unsigned int cancel_callback_threshold = (unsigned int)std::max((int)unretraction_moves->second.size() / 25, 1);
    unsigned int cancel_callback_curr = 0;

    for (const GCodeMove& move : unretraction_moves->second)
    {
        cancel_callback_curr = (cancel_callback_curr + 1) % cancel_callback_threshold;
        if (cancel_callback_curr == 0)
            cancel_callback();

        // store position
        Vec3crd position((int)scale_(move.start_position.x()), (int)scale_(move.start_position.y()), (int)scale_(move.start_position.z()));
        preview_data.unretraction.positions.emplace_back(position, move.data.width, move.data.height);
    }

    // we need to sort the positions by their z as they can be shuffled in case of sequential prints
    std::sort(preview_data.unretraction.positions.begin(), preview_data.unretraction.positions.end(),
        [](const GCodePreviewData::Retraction::Position& p1, const GCodePreviewData::Retraction::Position& p2)->bool
    { return unscale<double>(p1.position(2)) < unscale<double>(p2.position(2)); });
}

// Return an estimate of the memory consumed by the time estimator.
size_t GCodeAnalyzer::memory_used() const
{
    size_t out = sizeof(*this);
    for (const std::pair<GCodeMove::EType, GCodeMovesList> &kvp : m_moves_map)
        out += sizeof(kvp) + SLIC3R_STDVEC_MEMSIZE(kvp.second, GCodeMove);
    out += m_process_output.size();
    return out;
}

} // namespace Slic3r
