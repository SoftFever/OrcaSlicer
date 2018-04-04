#include <memory.h>
#include <string.h>
#include <float.h>

#include "../libslic3r.h"
#include "../PrintConfig.hpp"
#include "Print.hpp"

#include "Analyzer.hpp"
#include "PreviewData.hpp"

static const std::string AXIS_STR = "XYZE";
static const float MMMIN_TO_MMSEC = 1.0f / 60.0f;
static const float INCHES_TO_MM = 25.4f;
static const float DEFAULT_FEEDRATE = 0.0f;
static const unsigned int DEFAULT_EXTRUDER_ID = 0;
static const Slic3r::Pointf3 DEFAULT_START_POSITION = Slic3r::Pointf3(0.0f, 0.0f, 0.0f);
static const float DEFAULT_START_EXTRUSION = 0.0f;

namespace Slic3r {

const std::string GCodeAnalyzer::Extrusion_Role_Tag = "_ANALYZER_EXTR_ROLE:";
const std::string GCodeAnalyzer::Mm3_Per_Mm_Tag = "_ANALYZER_MM3_PER_MM:";
const std::string GCodeAnalyzer::Width_Tag = "_ANALYZER_WIDTH:";
const std::string GCodeAnalyzer::Height_Tag = "_ANALYZER_HEIGHT:";

const double GCodeAnalyzer::Default_mm3_per_mm = 0.0;
const float GCodeAnalyzer::Default_Width = 0.0f;
const float GCodeAnalyzer::Default_Height = 0.0f;

GCodeAnalyzer::Metadata::Metadata()
    : extrusion_role(erNone)
    , extruder_id(DEFAULT_EXTRUDER_ID)
    , mm3_per_mm(GCodeAnalyzer::Default_mm3_per_mm)
    , width(GCodeAnalyzer::Default_Width)
    , height(GCodeAnalyzer::Default_Height)
    , feedrate(DEFAULT_FEEDRATE)
{
}

GCodeAnalyzer::Metadata::Metadata(ExtrusionRole extrusion_role, unsigned int extruder_id, double mm3_per_mm, float width, float height, float feedrate)
    : extrusion_role(extrusion_role)
    , extruder_id(extruder_id)
    , mm3_per_mm(mm3_per_mm)
    , width(width)
    , height(height)
    , feedrate(feedrate)
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

    return false;
}

GCodeAnalyzer::GCodeMove::GCodeMove(GCodeMove::EType type, ExtrusionRole extrusion_role, unsigned int extruder_id, double mm3_per_mm, float width, float height, float feedrate, const Pointf3& start_position, const Pointf3& end_position, float delta_extruder)
    : type(type)
    , data(extrusion_role, extruder_id, mm3_per_mm, width, height, feedrate)
    , start_position(start_position)
    , end_position(end_position)
    , delta_extruder(delta_extruder)
{
}

GCodeAnalyzer::GCodeMove::GCodeMove(GCodeMove::EType type, const GCodeAnalyzer::Metadata& data, const Pointf3& start_position, const Pointf3& end_position, float delta_extruder)
    : type(type)
    , data(data)
    , start_position(start_position)
    , end_position(end_position)
    , delta_extruder(delta_extruder)
{
}

GCodeAnalyzer::GCodeAnalyzer()
{
    reset();
}

void GCodeAnalyzer::reset()
{
    _set_units(Millimeters);
    _set_global_positioning_type(Absolute);
    _set_e_local_positioning_type(Absolute);
    _set_extrusion_role(erNone);
    _set_extruder_id(DEFAULT_EXTRUDER_ID);
    _set_mm3_per_mm(Default_mm3_per_mm);
    _set_width(Default_Width);
    _set_height(Default_Height);
    _set_feedrate(DEFAULT_FEEDRATE);
    _set_start_position(DEFAULT_START_POSITION);
    _set_start_extrusion(DEFAULT_START_EXTRUSION);
    _reset_axes_position();

    m_moves_map.clear();
}

const std::string& GCodeAnalyzer::process_gcode(const std::string& gcode)
{
    m_process_output = "";

    m_parser.parse_buffer(gcode,
        [this](GCodeReader& reader, const GCodeReader::GCodeLine& line)
    { this->_process_gcode_line(reader, line); });

    return m_process_output;
}

void GCodeAnalyzer::calc_gcode_preview_data(GCodePreviewData& preview_data)
{
    // resets preview data
    preview_data.reset();

    // calculates extrusion layers
    _calc_gcode_preview_extrusion_layers(preview_data);

    // calculates travel
    _calc_gcode_preview_travel(preview_data);

    // calculates retractions
    _calc_gcode_preview_retractions(preview_data);

    // calculates unretractions
    _calc_gcode_preview_unretractions(preview_data);
}

bool GCodeAnalyzer::is_valid_extrusion_role(ExtrusionRole role)
{
    return ((erPerimeter <= role) && (role < erMixed));
}

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

// Returns the new absolute position on the given axis in dependence of the given parameters
float axis_absolute_position_from_G1_line(GCodeAnalyzer::EAxis axis, const GCodeReader::GCodeLine& lineG1, GCodeAnalyzer::EUnits units, bool is_relative, float current_absolute_position)
{
    float lengthsScaleFactor = (units == GCodeAnalyzer::Inches) ? INCHES_TO_MM : 1.0f;
    if (lineG1.has(Slic3r::Axis(axis)))
    {
        float ret = lineG1.value(Slic3r::Axis(axis)) * lengthsScaleFactor;
        return is_relative ? current_absolute_position + ret : ret;
    }
    else
        return current_absolute_position;
}

void GCodeAnalyzer::_processG1(const GCodeReader::GCodeLine& line)
{
    // updates axes positions from line
    EUnits units = _get_units();
    float new_pos[Num_Axis];
    for (unsigned char a = X; a < Num_Axis; ++a)
    {
        bool is_relative = (_get_global_positioning_type() == Relative);
        if (a == E)
            is_relative |= (_get_e_local_positioning_type() == Relative);

        new_pos[a] = axis_absolute_position_from_G1_line((EAxis)a, line, units, is_relative, _get_axis_position((EAxis)a));
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
        _set_axis_position(X, line.x() * lengthsScaleFactor);
        anyFound = true;
    }

    if (line.has_y())
    {
        _set_axis_position(Y, line.y() * lengthsScaleFactor);
        anyFound = true;
    }

    if (line.has_z())
    {
        _set_axis_position(Z, line.z() * lengthsScaleFactor);
        anyFound = true;
    }

    if (line.has_e())
    {
        _set_axis_position(E, line.e() * lengthsScaleFactor);
        anyFound = true;
    }

    if (!anyFound)
    {
        for (unsigned char a = X; a < Num_Axis; ++a)
        {
            _set_axis_position((EAxis)a, 0.0f);
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

void GCodeAnalyzer::_processT(const GCodeReader::GCodeLine& line)
{
    std::string cmd = line.cmd();
    if (cmd.length() > 1)
    {
        unsigned int id = (unsigned int)::strtol(cmd.substr(1).c_str(), nullptr, 10);
        if (_get_extruder_id() != id)
        {
            _set_extruder_id(id);

            // stores tool change move
            _store_move(GCodeMove::Tool_change);
        }
    }
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
    _set_mm3_per_mm(::strtod(comment.substr(pos + Mm3_Per_Mm_Tag.length()).c_str(), nullptr));
}

void GCodeAnalyzer::_process_width_tag(const std::string& comment, size_t pos)
{
    _set_width((float)::strtod(comment.substr(pos + Width_Tag.length()).c_str(), nullptr));
}

void GCodeAnalyzer::_process_height_tag(const std::string& comment, size_t pos)
{
    _set_height((float)::strtod(comment.substr(pos + Height_Tag.length()).c_str(), nullptr));
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

void GCodeAnalyzer::_set_mm3_per_mm(double value)
{
    m_state.data.mm3_per_mm = value;
}

double GCodeAnalyzer::_get_mm3_per_mm() const
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

void GCodeAnalyzer::_set_axis_position(EAxis axis, float position)
{
    m_state.position[axis] = position;
}

float GCodeAnalyzer::_get_axis_position(EAxis axis) const
{
    return m_state.position[axis];
}

void GCodeAnalyzer::_reset_axes_position()
{
    ::memset((void*)m_state.position, 0, Num_Axis * sizeof(float));
}

void GCodeAnalyzer::_set_start_position(const Pointf3& position)
{
    m_state.start_position = position;
}

const Pointf3& GCodeAnalyzer::_get_start_position() const
{
    return m_state.start_position;
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

Pointf3 GCodeAnalyzer::_get_end_position() const
{
    return Pointf3(m_state.position[X], m_state.position[Y], m_state.position[Z]);
}

void GCodeAnalyzer::_store_move(GCodeAnalyzer::GCodeMove::EType type)
{
    // if type non mapped yet, map it
    TypeToMovesMap::iterator it = m_moves_map.find(type);
    if (it == m_moves_map.end())
        it = m_moves_map.insert(TypeToMovesMap::value_type(type, GCodeMovesList())).first;

    // store move
    it->second.emplace_back(type, _get_extrusion_role(), _get_extruder_id(), _get_mm3_per_mm(), _get_width(), _get_height(), _get_feedrate(), _get_start_position(), _get_end_position(), _get_delta_extrusion());
}

bool GCodeAnalyzer::_is_valid_extrusion_role(int value) const
{
    return ((int)erNone <= value) && (value <= (int)erMixed);
}

void GCodeAnalyzer::_calc_gcode_preview_extrusion_layers(GCodePreviewData& preview_data)
{
    struct Helper
    {
        static GCodePreviewData::Extrusion::Layer& get_layer_at_z(GCodePreviewData::Extrusion::LayersList& layers, float z)
        {
            for (GCodePreviewData::Extrusion::Layer& layer : layers)
            {
                // if layer found, return it
                if (layer.z == z)
                    return layer;
            }

            // if layer not found, create and return it
            layers.emplace_back(z, ExtrusionPaths());
            return layers.back();
        }

        static void store_polyline(const Polyline& polyline, const Metadata& data, float z, GCodePreviewData& preview_data)
        {
            // if the polyline is valid, create the extrusion path from it and store it
            if (polyline.is_valid())
            {
                ExtrusionPath path(data.extrusion_role, data.mm3_per_mm, data.width, data.height);
                path.polyline = polyline;
                path.feedrate = data.feedrate;
                path.extruder_id = data.extruder_id;

                get_layer_at_z(preview_data.extrusion.layers, z).paths.push_back(path);
            }
        }
    };

    TypeToMovesMap::iterator extrude_moves = m_moves_map.find(GCodeMove::Extrude);
    if (extrude_moves == m_moves_map.end())
        return;

    Metadata data;
    float z = FLT_MAX;
    Polyline polyline;
    Pointf3 position(FLT_MAX, FLT_MAX, FLT_MAX);
    float volumetric_rate = FLT_MAX;
    GCodePreviewData::Range height_range;
    GCodePreviewData::Range width_range;
    GCodePreviewData::Range feedrate_range;
    GCodePreviewData::Range volumetric_rate_range;

    // constructs the polylines while traversing the moves
    for (const GCodeMove& move : extrude_moves->second)
    {
        if ((data != move.data) || (z != move.start_position.z) || (position != move.start_position) || (volumetric_rate != move.data.feedrate * (float)move.data.mm3_per_mm))
        {
            // store current polyline
            polyline.remove_duplicate_points();
            Helper::store_polyline(polyline, data, z, preview_data);

            // reset current polyline
            polyline = Polyline();

            // add both vertices of the move
            polyline.append(Point(scale_(move.start_position.x), scale_(move.start_position.y)));
            polyline.append(Point(scale_(move.end_position.x), scale_(move.end_position.y)));

            // update current values
            data = move.data;
            z = move.start_position.z;
            volumetric_rate = move.data.feedrate * (float)move.data.mm3_per_mm;
            height_range.update_from(move.data.height);
            width_range.update_from(move.data.width);
            feedrate_range.update_from(move.data.feedrate);
            volumetric_rate_range.update_from(volumetric_rate);
        }
        else
            // append end vertex of the move to current polyline
            polyline.append(Point(scale_(move.end_position.x), scale_(move.end_position.y)));

        // update current values
        position = move.end_position;
    }

    // store last polyline
    polyline.remove_duplicate_points();
    Helper::store_polyline(polyline, data, z, preview_data);

    // updates preview ranges data
    preview_data.ranges.height.set_from(height_range);
    preview_data.ranges.width.set_from(width_range);
    preview_data.ranges.feedrate.set_from(feedrate_range);
    preview_data.ranges.volumetric_rate.set_from(volumetric_rate_range);
}

void GCodeAnalyzer::_calc_gcode_preview_travel(GCodePreviewData& preview_data)
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
    Pointf3 position(FLT_MAX, FLT_MAX, FLT_MAX);
    GCodePreviewData::Travel::EType type = GCodePreviewData::Travel::Num_Types;
    GCodePreviewData::Travel::Polyline::EDirection direction = GCodePreviewData::Travel::Polyline::Num_Directions;
    float feedrate = FLT_MAX;
    unsigned int extruder_id = -1;

    GCodePreviewData::Range height_range;
    GCodePreviewData::Range width_range;
    GCodePreviewData::Range feedrate_range;

    // constructs the polylines while traversing the moves
    for (const GCodeMove& move : travel_moves->second)
    {
        GCodePreviewData::Travel::EType move_type = (move.delta_extruder < 0.0f) ? GCodePreviewData::Travel::Retract : ((move.delta_extruder > 0.0f) ? GCodePreviewData::Travel::Extrude : GCodePreviewData::Travel::Move);
        GCodePreviewData::Travel::Polyline::EDirection move_direction = ((move.start_position.x != move.end_position.x) || (move.start_position.y != move.end_position.y)) ? GCodePreviewData::Travel::Polyline::Generic : GCodePreviewData::Travel::Polyline::Vertical;

        if ((type != move_type) || (direction != move_direction) || (feedrate != move.data.feedrate) || (position != move.start_position) || (extruder_id != move.data.extruder_id))
        {
            // store current polyline
            polyline.remove_duplicate_points();
            Helper::store_polyline(polyline, type, direction, feedrate, extruder_id, preview_data);

            // reset current polyline
            polyline = Polyline3();

            // add both vertices of the move
            polyline.append(Point3(scale_(move.start_position.x), scale_(move.start_position.y), scale_(move.start_position.z)));
            polyline.append(Point3(scale_(move.end_position.x), scale_(move.end_position.y), scale_(move.end_position.z)));
        }
        else
            // append end vertex of the move to current polyline
            polyline.append(Point3(scale_(move.end_position.x), scale_(move.end_position.y), scale_(move.end_position.z)));

        // update current values
        position = move.end_position;
        type = move_type;
        feedrate = move.data.feedrate;
        extruder_id = move.data.extruder_id;
        height_range.update_from(move.data.height);
        width_range.update_from(move.data.width);
        feedrate_range.update_from(move.data.feedrate);
    }

    // store last polyline
    polyline.remove_duplicate_points();
    Helper::store_polyline(polyline, type, direction, feedrate, extruder_id, preview_data);

    // updates preview ranges data
    preview_data.ranges.height.set_from(height_range);
    preview_data.ranges.width.set_from(width_range);
    preview_data.ranges.feedrate.set_from(feedrate_range);
}

void GCodeAnalyzer::_calc_gcode_preview_retractions(GCodePreviewData& preview_data)
{
    TypeToMovesMap::iterator retraction_moves = m_moves_map.find(GCodeMove::Retract);
    if (retraction_moves == m_moves_map.end())
        return;

    for (const GCodeMove& move : retraction_moves->second)
    {
        // store position
        Point3 position(scale_(move.start_position.x), scale_(move.start_position.y), scale_(move.start_position.z));
        preview_data.retraction.positions.emplace_back(position, move.data.width, move.data.height);
    }
}

void GCodeAnalyzer::_calc_gcode_preview_unretractions(GCodePreviewData& preview_data)
{
    TypeToMovesMap::iterator unretraction_moves = m_moves_map.find(GCodeMove::Unretract);
    if (unretraction_moves == m_moves_map.end())
        return;

    for (const GCodeMove& move : unretraction_moves->second)
    {
        // store position
        Point3 position(scale_(move.start_position.x), scale_(move.start_position.y), scale_(move.start_position.z));
        preview_data.unretraction.positions.emplace_back(position, move.data.width, move.data.height);
    }
}

GCodePreviewData::Color operator + (const GCodePreviewData::Color& c1, const GCodePreviewData::Color& c2)
{
    return GCodePreviewData::Color(clamp(0.0f, 1.0f, c1.rgba[0] + c2.rgba[0]),
        clamp(0.0f, 1.0f, c1.rgba[1] + c2.rgba[1]),
        clamp(0.0f, 1.0f, c1.rgba[2] + c2.rgba[2]),
        clamp(0.0f, 1.0f, c1.rgba[3] + c2.rgba[3]));
}

GCodePreviewData::Color operator * (float f, const GCodePreviewData::Color& color)
{
    return GCodePreviewData::Color(clamp(0.0f, 1.0f, f * color.rgba[0]),
        clamp(0.0f, 1.0f, f * color.rgba[1]),
        clamp(0.0f, 1.0f, f * color.rgba[2]),
        clamp(0.0f, 1.0f, f * color.rgba[3]));
}

} // namespace Slic3r
