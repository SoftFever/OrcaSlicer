#ifndef slic3r_GCode_PressureEqualizer_hpp_
#define slic3r_GCode_PressureEqualizer_hpp_

#include "../libslic3r.h"
#include "../PrintConfig.hpp"
#include "../ExtrusionEntity.hpp"

namespace Slic3r {

enum GCodeMoveType
{
    GCODE_MOVE_TYPE_NOOP,
    GCODE_MOVE_TYPE_RETRACT,
    GCODE_MOVE_TYPE_UNRETRACT,
    GCODE_MOVE_TYPE_TOOL_CHANGE,
    GCODE_MOVE_TYPE_MOVE,
    GCODE_MOVE_TYPE_EXTRUDE,
};

// For visualization purposes, for the purposes of the G-code analysis and timing.
// The size of this structure is 56B.
// Keep the size of this structure as small as possible, because all moves of a complete print
// may be held in RAM.
struct GCodeMove
{
    bool        moving_xy(const float* pos_start)   const { return fabs(pos_end[0] - pos_start[0]) > 0.f || fabs(pos_end[1] - pos_start[1]) > 0.f; }
    bool        moving_xy()                         const { return moving_xy(get_pos_start()); }
    bool        moving_z (const float* pos_start)   const { return fabs(pos_end[2] - pos_start[2]) > 0.f; }
    bool        moving_z ()                         const { return moving_z(get_pos_start()); }
    bool        extruding(const float* pos_start)   const { return moving_xy() && pos_end[3] > pos_start[3]; }
    bool        extruding()                         const { return extruding(get_pos_start()); }
    bool        retracting(const float* pos_start)  const { return pos_end[3] < pos_start[3]; }
    bool        retracting()                        const { return retracting(get_pos_start()); }    
    bool        deretracting(const float* pos_start)  const { return ! moving_xy() && pos_end[3] > pos_start[3]; }
    bool        deretracting()                      const { return deretracting(get_pos_start()); }

    float       dist_xy2(const float* pos_start)    const { return (pos_end[0] - pos_start[0]) * (pos_end[0] - pos_start[0]) + (pos_end[1] - pos_start[1]) * (pos_end[1] - pos_start[1]); }
    float       dist_xy2()                          const { return dist_xy2(get_pos_start()); }
    float       dist_xyz2(const float* pos_start)   const { return (pos_end[0] - pos_start[0]) * (pos_end[0] - pos_start[0]) + (pos_end[1] - pos_start[1]) * (pos_end[1] - pos_start[1]) + (pos_end[2] - pos_start[2]) * (pos_end[2] - pos_start[2]); }
    float       dist_xyz2()                         const { return dist_xyz2(get_pos_start()); }

    float       dist_xy(const float* pos_start)     const { return sqrt(dist_xy2(pos_start)); }
    float       dist_xy()                           const { return dist_xy(get_pos_start()); }
    float       dist_xyz(const float* pos_start)    const { return sqrt(dist_xyz2(pos_start)); }
    float       dist_xyz()                          const { return dist_xyz(get_pos_start()); }

    float       dist_e(const float* pos_start)      const { return fabs(pos_end[3] - pos_start[3]); }
    float       dist_e()                            const { return dist_e(get_pos_start()); }

    float       feedrate()                          const { return pos_end[4]; }
    float       time(const float* pos_start)        const { return dist_xyz(pos_start) / feedrate(); }
    float       time()                              const { return time(get_pos_start()); }
    float       time_inv(const float* pos_start)    const { return feedrate() / dist_xyz(pos_start); }
    float       time_inv()                          const { return time_inv(get_pos_start()); }

    const float*    get_pos_start() const { assert(type != GCODE_MOVE_TYPE_NOOP); return this[-1].pos_end; }

    // Pack the enums to conserve space. With C++x11 the allocation size could be declared for enums, but for old C++ this is the only portable way.
    // GCodeLineType
    uint8_t         type;
    // Index of the active extruder.
    uint8_t         extruder_id;
    // ExtrusionRole
    uint8_t         extrusion_role;
    // For example, is it a bridge flow? Is the fan on?
    uint8_t         flags;
    // X,Y,Z,E,F. Storing the state of the currently active extruder only.
    float           pos_end[5];
    // Extrusion width, height for this segment in um.
    uint16_t        extrusion_width;
    uint16_t        extrusion_height;
};

typedef std::vector<GCodeMove> GCodeMoves;

struct GCodeLayer
{
    // Index of an object printed.
    size_t                  object_idx;
    // Index of an object instance printed.
    size_t                  object_instance_idx;
    // Index of the layer printed.
    size_t                  layer_idx;
    // Top z coordinate of the layer printed.
    float                   layer_z_top;

    // Moves over this layer. The 0th move is always of type GCODELINETYPE_NOOP and
    // it sets the initial position and tool for the layer.
    GCodeMoves              moves;

    // Indices into m_moves, where the tool changes happen.
    // This is useful, if one wants to display just only a piece of the path quickly.
    std::vector<size_t>     tool_changes;
};

typedef std::vector<GCodeLayer*> GCodeLayerPtrs;

class GCodeMovesDB
{
public:
    GCodeMovesDB() {};
    ~GCodeMovesDB() { reset(); }
    void reset();
    GCodeLayerPtrs      m_layers;
};

// Processes a G-code to extract moves and their types.
// This information is then used to render the print simulation colored by the extrusion type
// or various speeds.
// The GCodeAnalyzer is employed as a G-Code filter. It reads the G-code as it is generated,
// parses the comments generated by Slic3r just for the analyzer, and removes these comments.
class GCodeAnalyzer
{
public:
    GCodeAnalyzer(const Slic3r::GCodeConfig *config);
    ~GCodeAnalyzer();

    void reset();

    // Process a next batch of G-code lines. Flush the internal buffers if asked for.
    const char* process(const char *szGCode, bool flush);
    // Length of the buffer returned by process().
    size_t get_output_buffer_length() const { return output_buffer_length; }

private:
    // Keeps the reference, does not own the config.
    const Slic3r::GCodeConfig      *m_config;

    // Internal data.
    // X,Y,Z,E,F
    float                           m_current_pos[5];
    size_t                          m_current_extruder;
    ExtrusionRole                   m_current_extrusion_role;
    uint16_t                        m_current_extrusion_width;
    uint16_t                        m_current_extrusion_height;
    bool                            m_retracted;

    GCodeMovesDB                   *m_moves;

    // Output buffer will only grow. It will not be reallocated over and over.
    std::vector<char>               output_buffer;
    size_t                          output_buffer_length;

    bool process_line(const char *line, const size_t len);

    // Push the text to the end of the output_buffer.
    void push_to_output(const char *text, const size_t len, bool add_eol = true);
};

} // namespace Slic3r

#endif /* slic3r_GCode_PressureEqualizer_hpp_ */
