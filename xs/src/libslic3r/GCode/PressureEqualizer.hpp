#ifndef slic3r_GCode_PressureEqualizer_hpp_
#define slic3r_GCode_PressureEqualizer_hpp_

#include "../libslic3r.h"
#include "../PrintConfig.hpp"
#include "../ExtrusionEntity.hpp"

namespace Slic3r {

// Processes a G-code. Finds changes in the volumetric extrusion speed and adjusts the transitions
// between these paths to limit fast changes in the volumetric extrusion speed.
class PressureEqualizer
{
public:
    PressureEqualizer(const Slic3r::GCodeConfig *config);
    ~PressureEqualizer();

    void reset();

    // Process a next batch of G-code lines. Flush the internal buffers if asked for.
    const char* process(const char *szGCode, bool flush);

    size_t get_output_buffer_length() const { return output_buffer_length; }

private:
    struct Statistics
    {
        void reset() {
			volumetric_extrusion_rate_min = std::numeric_limits<float>::max();
            volumetric_extrusion_rate_max = 0.f;
            volumetric_extrusion_rate_avg = 0.f;
            extrusion_length = 0.f;
        }
        void update(float volumetric_extrusion_rate, float length) {
            volumetric_extrusion_rate_min = std::min(volumetric_extrusion_rate_min, volumetric_extrusion_rate);
            volumetric_extrusion_rate_max = std::max(volumetric_extrusion_rate_max, volumetric_extrusion_rate);
            volumetric_extrusion_rate_avg += volumetric_extrusion_rate * length;
            extrusion_length += length;          
        }
        float volumetric_extrusion_rate_min;
        float volumetric_extrusion_rate_max;
        float volumetric_extrusion_rate_avg;
        float extrusion_length;
    };

    struct Statistics m_stat;

    // Keeps the reference, does not own the config.
    const Slic3r::GCodeConfig *m_config;

    // Private configuration values
    // How fast could the volumetric extrusion rate increase / decrase? mm^3/sec^2
    struct ExtrusionRateSlope {
        float positive;
        float negative;
    };
    enum { numExtrusionRoles = erSupportMaterialInterface + 1 };
    ExtrusionRateSlope              m_max_volumetric_extrusion_rate_slopes[numExtrusionRoles];
    float                           m_max_volumetric_extrusion_rate_slope_positive;
    float                           m_max_volumetric_extrusion_rate_slope_negative;
    // Maximum segment length to split a long segment, if the initial and the final flow rate differ.
    float                           m_max_segment_length;

    // Configuration extracted from config.
    // Area of the crossestion of each filament. Necessary to calculate the volumetric flow rate.
    std::vector<float>              m_filament_crossections;

    // Internal data.
    // X,Y,Z,E,F
    float                           m_current_pos[5];
    size_t                          m_current_extruder;
    ExtrusionRole                   m_current_extrusion_role;
    bool                            m_retracted;

    enum GCodeLineType
    {
        GCODELINETYPE_INVALID,
        GCODELINETYPE_NOOP,
        GCODELINETYPE_OTHER,
        GCODELINETYPE_RETRACT,
        GCODELINETYPE_UNRETRACT,
        GCODELINETYPE_TOOL_CHANGE,
        GCODELINETYPE_MOVE,
        GCODELINETYPE_EXTRUDE,
    };

    struct GCodeLine
    {
        GCodeLine() : 
            type(GCODELINETYPE_INVALID),
            raw_length(0),
            modified(false),
            extruder_id(0), 
            volumetric_extrusion_rate(0.f), 
            volumetric_extrusion_rate_start(0.f), 
            volumetric_extrusion_rate_end(0.f) 
            {}

        bool        moving_xy()     const { return fabs(pos_end[0] - pos_start[0]) > 0.f || fabs(pos_end[1] - pos_start[1]) > 0.f; }
        bool        moving_z ()     const { return fabs(pos_end[2] - pos_start[2]) > 0.f; }
        bool        extruding()     const { return moving_xy() && pos_end[3] > pos_start[3]; }
        bool        retracting()    const { return pos_end[3] < pos_start[3]; }
        bool        deretracting()  const { return ! moving_xy() && pos_end[3] > pos_start[3]; }

        float       dist_xy2()      const { return (pos_end[0] - pos_start[0]) * (pos_end[0] - pos_start[0]) + (pos_end[1] - pos_start[1]) * (pos_end[1] - pos_start[1]); }
        float       dist_xyz2()     const { return (pos_end[0] - pos_start[0]) * (pos_end[0] - pos_start[0]) + (pos_end[1] - pos_start[1]) * (pos_end[1] - pos_start[1]) + (pos_end[2] - pos_start[2]) * (pos_end[2] - pos_start[2]); }
        float       dist_xy()       const { return sqrt(dist_xy2()); }
        float       dist_xyz()      const { return sqrt(dist_xyz2()); }
        float       dist_e()        const { return fabs(pos_end[3] - pos_start[3]); }

        float       feedrate()      const { return pos_end[4]; }
        float       time()          const { return dist_xyz() / feedrate(); }
        float       time_inv()      const { return feedrate() / dist_xyz(); }
        float       volumetric_correction_avg() const { 
            float avg_correction = 0.5f * (volumetric_extrusion_rate_start + volumetric_extrusion_rate_end) / volumetric_extrusion_rate; 
            assert(avg_correction > 0.f);
            assert(avg_correction <= 1.00000001f);
            return avg_correction;
        }
        float       time_corrected()  const { return time() * volumetric_correction_avg(); }

        GCodeLineType type;

        // We try to keep the string buffer once it has been allocated, so it will not be reallocated over and over.
        std::vector<char>   raw;
        size_t              raw_length;
        // If modified, the raw text has to be adapted by the new extrusion rate,
        // or maybe the line needs to be split into multiple lines.
        bool                modified;

        // float       timeStart;
        // float       timeEnd;
        // X,Y,Z,E,F. Storing the state of the currently active extruder only.
        float       pos_start[5];
        float       pos_end[5];
        // Was the axis found on the G-code line? X,Y,Z,F
        bool        pos_provided[5];

        // Index of the active extruder.
        size_t      extruder_id;
        // Extrusion role of this segment.
        ExtrusionRole extrusion_role;

        // Current volumetric extrusion rate.
        float       volumetric_extrusion_rate;
        // Volumetric extrusion rate at the start of this segment.
        float       volumetric_extrusion_rate_start;
        // Volumetric extrusion rate at the end of this segment.
        float       volumetric_extrusion_rate_end;

        // Volumetric extrusion rate slope limiting this segment.
        // If set to zero, the slope is unlimited.
        float       max_volumetric_extrusion_rate_slope_positive;
        float       max_volumetric_extrusion_rate_slope_negative;
    };

    // Circular buffer of GCode lines. The circular buffer size will be limited to circular_buffer_size.
    std::vector<GCodeLine>          circular_buffer;
    // Current position of the circular buffer (index, where to write the next line to, the line has to be pushed out before it is overwritten).
    size_t                          circular_buffer_pos;
    // Circular buffer size, configuration value.
    size_t                          circular_buffer_size;
    // Number of valid lines in the circular buffer. Lower or equal to circular_buffer_size.
    size_t                          circular_buffer_items;

    // Output buffer will only grow. It will not be reallocated over and over.
    std::vector<char>               output_buffer;
    size_t                          output_buffer_length;

    // For debugging purposes. Index of the G-code line processed.
    size_t                          line_idx;

    bool process_line(const char *line, const size_t len, GCodeLine &buf);
    void output_gcode_line(GCodeLine &buf);

    // Go back from the current circular_buffer_pos and lower the feedtrate to decrease the slope of the extrusion rate changes.
    // Then go forward and adjust the feedrate to decrease the slope of the extrusion rate changes.
    void adjust_volumetric_rate();

    // Push the text to the end of the output_buffer.
    void push_to_output(const char *text, const size_t len, bool add_eol = true);
    // Push an axis assignment to the end of the output buffer.
    void push_axis_to_output(const char axis, const float value, bool add_eol = false);
    // Push a G-code line to the output, 
    void push_line_to_output(const GCodeLine &line, const float new_feedrate, const char *comment);

    size_t circular_buffer_idx_head() const {
        size_t idx = circular_buffer_pos + circular_buffer_size - circular_buffer_items;
        if (idx >= circular_buffer_size)
            idx -= circular_buffer_size;
        return idx;
    }

    size_t circular_buffer_idx_tail() const { return circular_buffer_pos; }

    size_t circular_buffer_idx_prev(size_t idx) const {
        idx += circular_buffer_size - 1;
        if (idx >= circular_buffer_size)
            idx -= circular_buffer_size;
        return idx;
    }

    size_t circular_buffer_idx_next(size_t idx) const {
        if (++ idx >= circular_buffer_size)
            idx -= circular_buffer_size;
        return idx;
    }
};

} // namespace Slic3r

#endif /* slic3r_GCode_PressureEqualizer_hpp_ */
