#include <memory.h>
#include <string.h>
#include <float.h>

#include "../libslic3r.h"
#include "../PrintConfig.hpp"
#include "../LocalesUtils.hpp"

#include "PressureEqualizer.hpp"

namespace Slic3r {

PressureEqualizer::PressureEqualizer(const Slic3r::GCodeConfig *config) : 
    m_config(config)
{
    reset();
}

PressureEqualizer::~PressureEqualizer()
{
}

void PressureEqualizer::reset()
{
    circular_buffer_pos     = 0;
    circular_buffer_size    = 100;
    circular_buffer_items   = 0;
    circular_buffer.assign(circular_buffer_size, GCodeLine());

    // Preallocate some data, so that output_buffer.data() will return an empty string.
    output_buffer.assign(32, 0);
    output_buffer_length    = 0;

    m_current_extruder = 0;
    // Zero the position of the XYZE axes + the current feed
    memset(m_current_pos, 0, sizeof(float) * 5);
    m_current_extrusion_role = erNone;
    // Expect the first command to fill the nozzle (deretract).
    m_retracted = true;

    // Calculate filamet crossections for the multiple extruders.
    m_filament_crossections.clear();
    for (size_t i = 0; i < m_config->filament_diameter.values.size(); ++ i) {
        double r = m_config->filament_diameter.values[i];
        double a = 0.25f*M_PI*r*r;
        m_filament_crossections.push_back(float(a));
    }

    m_max_segment_length = 20.f;
    // Volumetric rate of a 0.45mm x 0.2mm extrusion at 60mm/s XY movement: 0.45*0.2*60*60=5.4*60 = 324 mm^3/min
    // Volumetric rate of a 0.45mm x 0.2mm extrusion at 20mm/s XY movement: 0.45*0.2*20*60=1.8*60 = 108 mm^3/min
    // Slope of the volumetric rate, changing from 20mm/s to 60mm/s over 2 seconds: (5.4-1.8)*60*60/2=60*60*1.8 = 6480 mm^3/min^2 = 1.8 mm^3/s^2
    m_max_volumetric_extrusion_rate_slope_positive = (m_config == NULL) ? 6480.f :
        m_config->max_volumetric_extrusion_rate_slope_positive.value * 60.f * 60.f;
    m_max_volumetric_extrusion_rate_slope_negative = (m_config == NULL) ? 6480.f :
        m_config->max_volumetric_extrusion_rate_slope_negative.value * 60.f * 60.f;

    for (size_t i = 0; i < numExtrusionRoles; ++ i) {
        m_max_volumetric_extrusion_rate_slopes[i].negative = m_max_volumetric_extrusion_rate_slope_negative;
        m_max_volumetric_extrusion_rate_slopes[i].positive = m_max_volumetric_extrusion_rate_slope_positive;
    }

    // Don't regulate the pressure in infill.
    m_max_volumetric_extrusion_rate_slopes[erBridgeInfill].negative = 0;
    m_max_volumetric_extrusion_rate_slopes[erBridgeInfill].positive = 0;
    // Don't regulate the pressure in gap fill.
    m_max_volumetric_extrusion_rate_slopes[erGapFill].negative = 0;
    m_max_volumetric_extrusion_rate_slopes[erGapFill].positive = 0;

    m_stat.reset();
    line_idx = 0;
}

const char* PressureEqualizer::process(const char *szGCode, bool flush)
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
            if (circular_buffer_items == circular_buffer_size)
                // Buffer is full. Push out the oldest line.
                output_gcode_line(circular_buffer[circular_buffer_pos]);
            else
                ++ circular_buffer_items;
            // Process a G-code line, store it into the provided GCodeLine object.
            size_t idx_tail = circular_buffer_pos;
            circular_buffer_pos = circular_buffer_idx_next(circular_buffer_pos);
            if (! process_line(p, endl - p, circular_buffer[idx_tail])) {
                // The line has to be forgotten. It contains comment marks, which shall be
                // filtered out of the target g-code.
                circular_buffer_pos = idx_tail;
                -- circular_buffer_items;
            }
            p = endl;
            if (*p == '\n') 
                ++ p;
        }
    }

    if (flush) {
        // Flush the remaining valid lines of the circular buffer.
        for (size_t idx = circular_buffer_idx_head(); circular_buffer_items > 0; -- circular_buffer_items) {
            output_gcode_line(circular_buffer[idx]);
            if (++ idx == circular_buffer_size)
                idx = 0;
        }
        // Reset the index pointer.
        assert(circular_buffer_items == 0);
        circular_buffer_pos = 0;

#if 1 
        printf("Statistics: \n"); 
        printf("Minimum volumetric extrusion rate: %f\n", m_stat.volumetric_extrusion_rate_min);
        printf("Maximum volumetric extrusion rate: %f\n", m_stat.volumetric_extrusion_rate_max);
        if (m_stat.extrusion_length > 0)
            m_stat.volumetric_extrusion_rate_avg /= m_stat.extrusion_length;
        printf("Average volumetric extrusion rate: %f\n", m_stat.volumetric_extrusion_rate_avg);
        m_stat.reset();
#endif
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
        throw Slic3r::RuntimeError("PressureEqualizer: Error parsing an int");
    line = endptr;
    return int(result);
};

// Parse an int starting at the current position of a line.
// If succeeded, the line pointer is advanced.
static inline float parse_float(const char *&line)
{
    char *endptr = NULL;
    float result = string_to_double_decimal_point(line, &endptr);
    if (endptr == NULL || !is_ws_or_eol(*endptr))
        throw Slic3r::RuntimeError("PressureEqualizer: Error parsing a float");
    line = endptr;
    return result;
};

bool PressureEqualizer::process_line(const char *line, const size_t len, GCodeLine &buf)
{
    static constexpr const char *EXTRUSION_ROLE_TAG = ";_EXTRUSION_ROLE:";

    if (strncmp(line, EXTRUSION_ROLE_TAG, strlen(EXTRUSION_ROLE_TAG)) == 0) {
        line += strlen(EXTRUSION_ROLE_TAG);
        int role = atoi(line);
        m_current_extrusion_role = ExtrusionRole(role);
        ++ line_idx;
        return false;
    }

    // Set the type, copy the line to the buffer.
    buf.type = GCODELINETYPE_OTHER;
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
                    throw Slic3r::RuntimeError(std::string("GCode::PressureEqualizer: Invalid axis for G0/G1: ") + axis);
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
                    buf.type = GCODELINETYPE_RETRACT;
                    m_retracted = true;
                } else if (! changed[0] && ! changed[1] && ! changed[2]) {
                    // assert(m_retracted);
                    buf.type = GCODELINETYPE_UNRETRACT;
                    m_retracted = false;
                } else {
                    assert(changed[0] || changed[1]);
                    // Moving in XY plane.
                    buf.type = GCODELINETYPE_EXTRUDE;
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
                    if (rate < 40.f) {
                    	printf("Extremely low flow rate: %f. Line %d, Length: %f, extrusion: %f Old position: (%f, %f, %f), new position: (%f, %f, %f)\n", 
                            rate, 
                            int(line_idx),
                            sqrt(len2), sqrt((diff[3]*diff[3])/len2),
                            m_current_pos[0], m_current_pos[1], m_current_pos[2],
                            new_pos[0], new_pos[1], new_pos[2]);
                    }
                }
            } else if (changed[0] || changed[1] || changed[2]) {
                // Moving without extrusion.
                buf.type = GCODELINETYPE_MOVE;
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
                    throw Slic3r::RuntimeError(std::string("GCode::PressureEqualizer: Incorrect axis in a G92 G-code: ") + axis);
                }
                eatws(line);
            }
            assert(set);
            break;
        }
        case 10:
        case 22:
            // Firmware retract.
            buf.type = GCODELINETYPE_RETRACT;
            m_retracted = true;
            break;
        case 11:
        case 23:
            // Firmware unretract.
            buf.type = GCODELINETYPE_UNRETRACT;
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
            buf.type = GCODELINETYPE_TOOL_CHANGE;
        } else {
            buf.type = GCODELINETYPE_NOOP;
        }
        break;
    }
    }

    buf.extruder_id = m_current_extruder;
    memcpy(buf.pos_end, m_current_pos, sizeof(float)*5);

    adjust_volumetric_rate();
    ++ line_idx;
	return true;
}

void PressureEqualizer::output_gcode_line(GCodeLine &line)
{
    if (! line.modified) {
        push_to_output(line.raw.data(), line.raw_length, true);
        return;
    }

    // The line was modified.
    // Find the comment.
    const char *comment = line.raw.data();
    while (*comment != ';' && *comment != 0) ++comment;
    if (*comment != ';')
        comment = NULL;
    
    // Emit the line with lowered extrusion rates.
    float l2 = line.dist_xyz2();
    float l = sqrt(l2);
    size_t nSegments = size_t(ceil(l / m_max_segment_length));
    if (nSegments == 1) {
        // Just update this segment.
        push_line_to_output(line, line.feedrate() * line.volumetric_correction_avg(), comment);
    } else {
        bool accelerating = line.volumetric_extrusion_rate_start < line.volumetric_extrusion_rate_end;
        // Update the initial and final feed rate values.
        line.pos_start[4] = line.volumetric_extrusion_rate_start * line.pos_end[4] / line.volumetric_extrusion_rate;
        line.pos_end  [4] = line.volumetric_extrusion_rate_end   * line.pos_end[4] / line.volumetric_extrusion_rate;
        float feed_avg = 0.5f * (line.pos_start[4] + line.pos_end[4]);
        // Limiting volumetric extrusion rate slope for this segment.
        float max_volumetric_extrusion_rate_slope = accelerating ?
            line.max_volumetric_extrusion_rate_slope_positive : line.max_volumetric_extrusion_rate_slope_negative; 
        // Total time for the segment, corrected for the possibly lowered volumetric feed rate,
        // if accelerating / decelerating over the complete segment.
        float t_total = line.dist_xyz() / feed_avg;
        // Time of the acceleration / deceleration part of the segment, if accelerating / decelerating
        // with the maximum volumetric extrusion rate slope.
        float t_acc    = 0.5f * (line.volumetric_extrusion_rate_start + line.volumetric_extrusion_rate_end) / max_volumetric_extrusion_rate_slope;
        float l_acc    = l;
        float l_steady = 0.f;
        if (t_acc < t_total) {
            // One may achieve higher print speeds if part of the segment is not speed limited.
            float l_acc    = t_acc * feed_avg;
            float l_steady = l - l_acc;
            if (l_steady < 0.5f * m_max_segment_length) {
                l_acc    = l;
                l_steady = 0.f;
            } else
                nSegments = size_t(ceil(l_acc / m_max_segment_length));
        }
        float pos_start[5];
        float pos_end  [5];
        float pos_end2 [4];
        memcpy(pos_start, line.pos_start, sizeof(float)*5);
        memcpy(pos_end  , line.pos_end  , sizeof(float)*5);
        if (l_steady > 0.f) {
            // There will be a steady feed segment emitted.
            if (accelerating) {
                // Prepare the final steady feed rate segment.
                memcpy(pos_end2, pos_end, sizeof(float)*4);
                float t = l_acc / l;
                for (int i = 0; i < 4; ++ i) {
                    pos_end[i] = pos_start[i] + (pos_end[i] - pos_start[i]) * t;
                    line.pos_provided[i] = true;
                }
            } else {
                // Emit the steady feed rate segment.
                float t = l_steady / l;
                for (int i = 0; i < 4; ++ i) {
                    line.pos_end[i] = pos_start[i] + (pos_end[i] - pos_start[i]) * t;
                    line.pos_provided[i] = true;
                }
                push_line_to_output(line, pos_start[4], comment);
                comment = NULL;
                memcpy(line.pos_start, line.pos_end, sizeof(float)*5);
                memcpy(pos_start, line.pos_end, sizeof(float)*5);
            }
        }
        // Split the segment into pieces.
        for (size_t i = 1; i < nSegments; ++ i) {
            float t = float(i) / float(nSegments);
            for (size_t j = 0; j < 4; ++ j) {
                line.pos_end[j] = pos_start[j] + (pos_end[j] - pos_start[j]) * t;
                line.pos_provided[j] = true;
            } 
            // Interpolate the feed rate at the center of the segment.
            push_line_to_output(line, pos_start[4] + (pos_end[4] - pos_start[4]) * (float(i) - 0.5f) / float(nSegments), comment);
            comment = NULL;
            memcpy(line.pos_start, line.pos_end, sizeof(float)*5);
        }
		if (l_steady > 0.f && accelerating) {
            for (int i = 0; i < 4; ++ i) {
                line.pos_end[i] = pos_end2[i];
                line.pos_provided[i] = true;
            }
            push_line_to_output(line, pos_end[4], comment);
        }
    }
}

void PressureEqualizer::adjust_volumetric_rate()
{
    if (circular_buffer_items < 2)
        return;

    // Go back from the current circular_buffer_pos and lower the feedtrate to decrease the slope of the extrusion rate changes.
    const size_t idx_head = circular_buffer_idx_head();
    const size_t idx_tail = circular_buffer_idx_prev(circular_buffer_idx_tail());
    size_t idx = idx_tail;
    if (idx == idx_head || ! circular_buffer[idx].extruding())
        // Nothing to do, the last move is not extruding.
        return;

    float feedrate_per_extrusion_role[numExtrusionRoles];
    for (size_t i = 0; i < numExtrusionRoles; ++ i)
        feedrate_per_extrusion_role[i] = FLT_MAX;
    feedrate_per_extrusion_role[circular_buffer[idx].extrusion_role] = circular_buffer[idx].volumetric_extrusion_rate_start;

    bool modified = true;
    while (modified && idx != idx_head) {
        size_t idx_prev = circular_buffer_idx_prev(idx);
        for (; ! circular_buffer[idx_prev].extruding() && idx_prev != idx_head; idx_prev = circular_buffer_idx_prev(idx_prev)) ;
        if (! circular_buffer[idx_prev].extruding())
        	break;
        // Volumetric extrusion rate at the start of the succeding segment.
        float rate_succ = circular_buffer[idx].volumetric_extrusion_rate_start;
        // What is the gradient of the extrusion rate between idx_prev and idx?
        idx = idx_prev;
        GCodeLine &line = circular_buffer[idx];
        for (size_t iRole = 1; iRole < numExtrusionRoles; ++ iRole) {
            float rate_slope = m_max_volumetric_extrusion_rate_slopes[iRole].negative;
            if (rate_slope == 0)
                // The negative rate is unlimited.
                continue;
            float rate_end = feedrate_per_extrusion_role[iRole];
            if (iRole == line.extrusion_role && rate_succ < rate_end)
                // Limit by the succeeding volumetric flow rate.
                rate_end = rate_succ;
            if (line.volumetric_extrusion_rate_end > rate_end) {
                line.volumetric_extrusion_rate_end = rate_end;
                line.modified = true;
            } else if (iRole == line.extrusion_role) {
                rate_end = line.volumetric_extrusion_rate_end;
            } else if (rate_end == FLT_MAX) {
                // The rate for ExtrusionRole iRole is unlimited.
                continue;
            } else {
                // Use the original, 'floating' extrusion rate as a starting point for the limiter.
            }
//            modified = false;
            float rate_start = rate_end + rate_slope * line.time_corrected();
            if (rate_start < line.volumetric_extrusion_rate_start) {
                // Limit the volumetric extrusion rate at the start of this segment due to a segment 
                // of ExtrusionType iRole, which will be extruded in the future.
                line.volumetric_extrusion_rate_start = rate_start;
                line.max_volumetric_extrusion_rate_slope_negative = rate_slope;
                line.modified = true;
//              modified = true;
            }
            feedrate_per_extrusion_role[iRole] = (iRole == line.extrusion_role) ? line.volumetric_extrusion_rate_start : rate_start;
        }
    }

    // Go forward and adjust the feedrate to decrease the slope of the extrusion rate changes.
    for (size_t i = 0; i < numExtrusionRoles; ++ i)
        feedrate_per_extrusion_role[i] = FLT_MAX;
    feedrate_per_extrusion_role[circular_buffer[idx].extrusion_role] = circular_buffer[idx].volumetric_extrusion_rate_end;

    assert(circular_buffer[idx].extruding());
    while (idx != idx_tail) {
        size_t idx_next = circular_buffer_idx_next(idx);
        for (; ! circular_buffer[idx_next].extruding() && idx_next != idx_tail; idx_next = circular_buffer_idx_next(idx_next)) ;
        if (! circular_buffer[idx_next].extruding())
        	break;
        float rate_prec = circular_buffer[idx].volumetric_extrusion_rate_end;
        // What is the gradient of the extrusion rate between idx_prev and idx?
        idx = idx_next;
        GCodeLine &line = circular_buffer[idx];
        for (size_t iRole = 1; iRole < numExtrusionRoles; ++ iRole) {
            float rate_slope = m_max_volumetric_extrusion_rate_slopes[iRole].positive;
            if (rate_slope == 0)
                // The positive rate is unlimited.
                continue;
            float rate_start = feedrate_per_extrusion_role[iRole];
            if (iRole == line.extrusion_role && rate_prec < rate_start)
                rate_start = rate_prec;
            if (line.volumetric_extrusion_rate_start > rate_start) {
                line.volumetric_extrusion_rate_start = rate_start;
                line.modified = true;
            } else if (iRole == line.extrusion_role) {
                rate_start = line.volumetric_extrusion_rate_start;
            } else if (rate_start == FLT_MAX) {
                // The rate for ExtrusionRole iRole is unlimited.
                continue;
            } else {
                // Use the original, 'floating' extrusion rate as a starting point for the limiter.
            }
            float rate_end = (rate_slope == 0) ? FLT_MAX : rate_start + rate_slope * line.time_corrected();
            if (rate_end < line.volumetric_extrusion_rate_end) {
                // Limit the volumetric extrusion rate at the start of this segment due to a segment 
                // of ExtrusionType iRole, which was extruded before.
                line.volumetric_extrusion_rate_end = rate_end;
                line.max_volumetric_extrusion_rate_slope_positive = rate_slope;
                line.modified = true;
            }
            feedrate_per_extrusion_role[iRole] = (iRole == line.extrusion_role) ? line.volumetric_extrusion_rate_end : rate_end;
        }
    }
}

void PressureEqualizer::push_axis_to_output(const char axis, const float value, bool add_eol)
{
    char buf[2048];
    int len = sprintf(buf, 
        (axis == 'E') ? " %c%.3f" : " %c%.5f",
        axis, value);
    push_to_output(buf, len, add_eol);
}

void PressureEqualizer::push_to_output(const char *text, const size_t len, bool add_eol)
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

void PressureEqualizer::push_line_to_output(const GCodeLine &line, const float new_feedrate, const char *comment)
{
    push_to_output("G1", 2, false);
    for (char i = 0; i < 3; ++ i)
        if (line.pos_provided[i])
            push_axis_to_output('X'+i, line.pos_end[i]);
    push_axis_to_output('E', m_config->use_relative_e_distances.value ? (line.pos_end[3] - line.pos_start[3]) : line.pos_end[3]);
//    if (line.pos_provided[4] || fabs(line.feedrate() - new_feedrate) > 1e-5)
        push_axis_to_output('F', new_feedrate);
    // output comment and EOL
    push_to_output(comment, (comment == NULL) ? 0 : strlen(comment), true);
} 

} // namespace Slic3r
