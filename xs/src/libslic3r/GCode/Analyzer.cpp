#include <memory.h>
#include <string.h>
#include <float.h>

#include "../libslic3r.h"
#include "../PrintConfig.hpp"

#include "Analyzer.hpp"

namespace Slic3r {

void GCodeMovesDB::reset()
{
    for (size_t i = 0; i < m_layers.size(); ++ i)
        delete m_layers[i];
    m_layers.clear();
}

GCodeAnalyzer::GCodeAnalyzer(const Slic3r::GCodeConfig *config) : 
    m_config(config)
{
    reset();
    m_moves = new GCodeMovesDB();
}

GCodeAnalyzer::~GCodeAnalyzer()
{
    delete m_moves;
}

void GCodeAnalyzer::reset()
{
    output_buffer.clear();
    output_buffer_length = 0;

    m_current_extruder = 0;
    // Zero the position of the XYZE axes + the current feed
    memset(m_current_pos, 0, sizeof(float) * 5);
    m_current_extrusion_role = erNone;
    m_current_extrusion_width = 0;
    m_current_extrusion_height = 0;
    // Expect the first command to fill the nozzle (deretract).
    m_retracted = true;
    m_moves->reset();
}

const char* GCodeAnalyzer::process(const char *szGCode, bool flush)
{
    // Reset length of the output_buffer.
    output_buffer_length = 0;

    if (szGCode != 0) {
        const char *p = szGCode;
        while (*p != 0) {
            // Find end of the line.
            const char *endl = p;
            // Slic3r always generates end of lines in a Unix style.
            for (; *endl != 0 && *endl != '\n'; ++ endl) ;
            // Process a G-code line, store it into the provided GCodeLine object.
            bool should_output = process_line(p, endl - p);
            if (*endl == '\n') 
                ++ endl;
            if (should_output)
                push_to_output(p, endl - p);
            p = endl;
        }
    }

    return output_buffer.data();
}

// Is a white space?
static inline bool is_ws(const char c) { return c == ' ' || c == '\t'; }
// Is it an end of line? Consider a comment to be an end of line as well.
static inline bool is_eol(const char c) { return c == 0 || c == '\r' || c == '\n' || c == ';'; };
// Is it a white space or end of line?
static inline bool is_ws_or_eol(const char c) { return is_ws(c) || is_eol(c); };

// Eat whitespaces.
static void eatws(const char *&line)
{
    while (is_ws(*line)) 
        ++ line;
}

// Parse an int starting at the current position of a line.
// If succeeded, the line pointer is advanced.
static inline int parse_int(const char *&line)
{
    char *endptr = NULL;
    long result = strtol(line, &endptr, 10);
    if (endptr == NULL || !is_ws_or_eol(*endptr))
        throw std::runtime_error("GCodeAnalyzer: Error parsing an int");
    line = endptr;
    return int(result);
};

// Parse an int starting at the current position of a line.
// If succeeded, the line pointer is advanced.
static inline float parse_float(const char *&line)
{
    char *endptr = NULL;
    float result = strtof(line, &endptr);
    if (endptr == NULL || !is_ws_or_eol(*endptr))
        throw std::runtime_error("GCodeAnalyzer: Error parsing a float");
    line = endptr;
    return result;
};

#define EXTRUSION_ROLE_TAG ";_EXTRUSION_ROLE:"
bool GCodeAnalyzer::process_line(const char *line, const size_t len)
{
    if (strncmp(line, EXTRUSION_ROLE_TAG, strlen(EXTRUSION_ROLE_TAG)) == 0) {
        line += strlen(EXTRUSION_ROLE_TAG);
        int role = atoi(line);
        this->m_current_extrusion_role = ExtrusionRole(role);
        return false;
    }

/*
    // Set the type, copy the line to the buffer.
    buf.type = GCODE_MOVE_TYPE_OTHER;
    buf.modified = false;
    if (buf.raw.size() < len + 1)
        buf.raw.assign(line, line + len + 1);
    else
        memcpy(buf.raw.data(), line, len);
    buf.raw[len] = 0;
    buf.raw_length = len;

    memcpy(buf.pos_start, m_current_pos, sizeof(float)*5);
    memcpy(buf.pos_end, m_current_pos, sizeof(float)*5);
    memset(buf.pos_provided, 0, 5);

    buf.volumetric_extrusion_rate = 0.f;
    buf.volumetric_extrusion_rate_start = 0.f;
    buf.volumetric_extrusion_rate_end = 0.f;
    buf.max_volumetric_extrusion_rate_slope_positive = 0.f;
    buf.max_volumetric_extrusion_rate_slope_negative = 0.f;
	buf.extrusion_role = m_current_extrusion_role;

    // Parse the G-code line, store the result into the buf.
    switch (toupper(*line ++)) {
    case 'G': {
        int gcode = parse_int(line);
        eatws(line);
        switch (gcode) {
        case 0:
        case 1:
        {
            // G0, G1: A FFF 3D printer does not make a difference between the two.
            float new_pos[5];
            memcpy(new_pos, m_current_pos, sizeof(float)*5);
            bool  changed[5] = { false, false, false, false, false };
            while (!is_eol(*line)) {
                char axis = toupper(*line++);
                int  i = -1;
                switch (axis) {
                case 'X':
                case 'Y':
                case 'Z':
                    i = axis - 'X';
                    break;
                case 'E':
                    i = 3;
                    break;
                case 'F':
                    i = 4;
                    break;
                default:
                    assert(false);
                }
                if (i == -1)
                    throw std::runtime_error(std::string("GCodeAnalyzer: Invalid axis for G0/G1: ") + axis);
                buf.pos_provided[i] = true;
                new_pos[i] = parse_float(line);
                if (i == 3 && m_config->use_relative_e_distances.value)
                    new_pos[i] += m_current_pos[i];
                changed[i] = new_pos[i] != m_current_pos[i];
                eatws(line);
            }
            if (changed[3]) {
                // Extrusion, retract or unretract.
                float diff = new_pos[3] - m_current_pos[3];
                if (diff < 0) {
                    buf.type = GCODE_MOVE_TYPE_RETRACT;
                    m_retracted = true;
                } else if (! changed[0] && ! changed[1] && ! changed[2]) {
                    // assert(m_retracted);
                    buf.type = GCODE_MOVE_TYPE_UNRETRACT;
                    m_retracted = false;
                } else {
                    assert(changed[0] || changed[1]);
                    // Moving in XY plane.
                    buf.type = GCODE_MOVE_TYPE_EXTRUDE;
                    // Calculate the volumetric extrusion rate.
                    float diff[4];
                    for (size_t i = 0; i < 4; ++ i)
                        diff[i] = new_pos[i] - m_current_pos[i];
                    // volumetric extrusion rate = A_filament * F_xyz * L_e / L_xyz [mm^3/min]
                    float len2 = diff[0]*diff[0]+diff[1]*diff[1]+diff[2]*diff[2];
                    float rate = m_filament_crossections[m_current_extruder] * new_pos[4] * sqrt((diff[3]*diff[3])/len2);
                    buf.volumetric_extrusion_rate       = rate;
                    buf.volumetric_extrusion_rate_start = rate;
                    buf.volumetric_extrusion_rate_end   = rate;
                    m_stat.update(rate, sqrt(len2));
                    if (rate < 10.f) {
                    	printf("Extremely low flow rate: %f\n", rate);
                    }
                }
            } else if (changed[0] || changed[1] || changed[2]) {
                // Moving without extrusion.
                buf.type = GCODE_MOVE_TYPE_MOVE;
            }
            memcpy(m_current_pos, new_pos, sizeof(float) * 5);
            break;
        }
        case 92:
        {
            // G92 : Set Position
            // Set a logical coordinate position to a new value without actually moving the machine motors.
            // Which axes to set?
            bool set = false;
            while (!is_eol(*line)) {
                char axis = toupper(*line++);
                switch (axis) {
                case 'X':
                case 'Y':
                case 'Z':
                    m_current_pos[axis - 'X'] = (!is_ws_or_eol(*line)) ? parse_float(line) : 0.f;
                    set = true;
                    break;
                case 'E':
                    m_current_pos[3] = (!is_ws_or_eol(*line)) ? parse_float(line) : 0.f;
                    set = true;
                    break;
                default:
                    throw std::runtime_error(std::string("GCodeAnalyzer: Incorrect axis in a G92 G-code: ") + axis);
                }
                eatws(line);
            }
            assert(set);
            break;
        }
        case 10:
        case 22:
            // Firmware retract.
            buf.type = GCODE_MOVE_TYPE_RETRACT;
            m_retracted = true;
            break;
        case 11:
        case 23:
            // Firmware unretract.
            buf.type = GCODE_MOVE_TYPE_UNRETRACT;
            m_retracted = false;
            break;
        default:
            // Ignore the rest.
        break;
        }
        break;
    }
    case 'M': {
        int mcode = parse_int(line);
        eatws(line);
        switch (mcode) {
        default:
            // Ignore the rest of the M-codes.
        break;
        }
        break;
    }
    case 'T':
    {
        // Activate an extruder head.
        int new_extruder = parse_int(line);
        if (new_extruder != m_current_extruder) {
            m_current_extruder = new_extruder;
            m_retracted = true;
            buf.type = GCODE_MOVE_TYPE_TOOL_CHANGE;
        } else {
            buf.type = GCODE_MOVE_TYPE_NOOP;
        }
        break;
    }
    }

    buf.extruder_id = m_current_extruder;
    memcpy(buf.pos_end, m_current_pos, sizeof(float)*5);
*/
	return true;
}

void GCodeAnalyzer::push_to_output(const char *text, const size_t len, bool add_eol)
{
    // New length of the output buffer content.
    size_t len_new = output_buffer_length + len + 1;
    if (add_eol)
        ++ len_new;

    // Resize the output buffer to a power of 2 higher than the required memory.
    if (output_buffer.size() < len_new) {
        size_t v = len_new;
        // Compute the next highest power of 2 of 32-bit v
        // http://graphics.stanford.edu/~seander/bithacks.html
        v--;
        v |= v >> 1;
        v |= v >> 2;
        v |= v >> 4;
        v |= v >> 8;
        v |= v >> 16;
        v++;
        output_buffer.resize(v);
    }

    // Copy the text to the output.
    if (len != 0) {
        memcpy(output_buffer.data() + output_buffer_length, text, len);
        output_buffer_length += len;
    }
    if (add_eol)
        output_buffer[output_buffer_length ++] = '\n';
    output_buffer[output_buffer_length] = 0;
}

} // namespace Slic3r
