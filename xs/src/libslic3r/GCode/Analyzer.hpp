#ifndef slic3r_GCode_Analyzer_hpp_
#define slic3r_GCode_Analyzer_hpp_

#include "../libslic3r.h"
#include "../PrintConfig.hpp"
#include "../ExtrusionEntity.hpp"

//############################################################################################################
#if ENRICO_GCODE_PREVIEW
#include "Point.hpp"
#include "GCodeReader.hpp"
#endif // ENRICO_GCODE_PREVIEW
//############################################################################################################

namespace Slic3r {

//############################################################################################################
#if ENRICO_GCODE_PREVIEW
    class Print;
#endif // ENRICO_GCODE_PREVIEW
//############################################################################################################

//############################################################################################################
#if !ENRICO_GCODE_PREVIEW
//############################################################################################################
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
//############################################################################################################
#endif // !ENRICO_GCODE_PREVIEW
//############################################################################################################

//############################################################################################################
#if ENRICO_GCODE_PREVIEW
class GCodeAnalyzer
{
public:
    static const std::string Extrusion_Role_Tag;
    static const std::string Mm3_Per_Mm_Tag;
    static const std::string Width_Tag;
    static const std::string Height_Tag;

    static const double Default_mm3_per_mm;
    static const float Default_Width;
    static const float Default_Height;

    enum EUnits : unsigned char
    {
        Millimeters,
        Inches
    };

    enum EAxis : unsigned char
    {
        X,
        Y,
        Z,
        E,
        Num_Axis
    };

    enum EPositioningType : unsigned char
    {
        Absolute,
        Relative
    };

    struct Metadata
    {
        ExtrusionRole extrusion_role;
        unsigned int extruder_id;
        double mm3_per_mm;
        float width;
        float height;
        float feedrate;

        Metadata();
        Metadata(ExtrusionRole extrusion_role, unsigned int extruder_id, double mm3_per_mm, float width, float height, float feedrate);

        bool operator != (const Metadata& other) const;
    };

    struct GCodeMove
    {
        enum EType : unsigned char
        {
            Noop,
            Retract,
            Unretract,
            Tool_change,
            Move,
            Extrude,
            Num_Types
        };

        EType type;
        Metadata data;
        Pointf3 start_position;
        Pointf3 end_position;
        float delta_extruder;

        GCodeMove(EType type, ExtrusionRole extrusion_role, unsigned int extruder_id, double mm3_per_mm, float width, float height, float feedrate, const Pointf3& start_position, const Pointf3& end_position, float delta_extruder);
        GCodeMove(EType type, const Metadata& data, const Pointf3& start_position, const Pointf3& end_position, float delta_extruder);
    };

    typedef std::vector<GCodeMove> GCodeMovesList;
    typedef std::map<GCodeMove::EType, GCodeMovesList> TypeToMovesMap;

private:
    struct State
    {
        EUnits units;
        EPositioningType positioning_xyz_type;
        EPositioningType positioning_e_type;
        Metadata data;
        Pointf3 start_position;
        float start_extrusion;
        float position[Num_Axis];
    };

public:
    struct PreviewData
    {
        struct Color
        {
            float rgba[4];

            Color();
            Color(float r, float g, float b, float a);

            static const Color Dummy;
        };

        struct Range
        {
            static const unsigned int Colors_Count = 10;
            static const Color Default_Colors[Colors_Count];

            Color colors[Colors_Count];
            float min;
            float max;

            Range();

            void reset();
            bool empty() const;
            void update_from(float value);
            void set_from(const Range& other);

            const Color& get_color_at(float value) const;
            const Color& get_color_at_max() const;

        private:
            float _step() const;
        };

        struct Extrusion
        {
            enum EViewType : unsigned char
            {
                FeatureType,
                Height,
                Width,
                Feedrate,
                Num_View_Types
            };

            static const unsigned int Num_Extrusion_Roles = (unsigned int)erMixed + 1;
            static const Color Default_Extrusion_Role_Colors[Num_Extrusion_Roles];
            static const EViewType Default_View_Type; 

            struct Ranges
            {
                Range height;
                Range width;
                Range feedrate;
            };

            struct Layer
            {
                float z;
                ExtrusionPaths paths;

                Layer(float z, const ExtrusionPaths& paths);
            };

            typedef std::vector<Layer> LayersList;

            EViewType view_type;
            Color role_colors[Num_Extrusion_Roles];
            Ranges ranges;
            LayersList layers;
            unsigned int role_flags;

            void set_default();
            bool is_role_flag_set(ExtrusionRole role) const;

            static bool is_role_flag_set(unsigned int flags, ExtrusionRole role);
        };

        struct Travel
        {
            enum EType : unsigned char
            {
                Move,
                Extrude,
                Retract,
                Num_Types
            };

            static const float Default_Width;
            static const float Default_Height;
            static const Color Default_Type_Colors[Num_Types];

            struct Polyline
            {
                enum EDirection
                {
                    Vertical,
                    Generic,
                    Num_Directions
                };

                EType type;
                EDirection direction;
                Polyline3 polyline;

                Polyline(EType type, EDirection direction, const Polyline3& polyline);
            };

            typedef std::vector<Polyline> PolylinesList;

            PolylinesList polylines;
            float width;
            float height;
            Color type_colors[Num_Types];
            bool is_visible;

            void set_default();
        };

        struct Retraction
        {
            static const Color Default_Color;

            struct Position
            {
                Point3 position;
                float width;
                float height;

                Position(const Point3& position, float width, float height);
            };

            typedef std::vector<Position> PositionsList;

            PositionsList positions;
            Color color;
            bool is_visible;

            void set_default();
        };

        Extrusion extrusion;
        Travel travel;
        Retraction retraction;
        Retraction unretraction;

        PreviewData();

        void set_default();
        void reset();

        const Color& get_extrusion_role_color(ExtrusionRole role) const;
        const Color& get_extrusion_height_color(float height) const;
        const Color& get_extrusion_width_color(float width) const;
        const Color& get_extrusion_feedrate_color(float feedrate) const;
    };

private:
    State m_state;
    GCodeReader m_parser;
    TypeToMovesMap m_moves_map;

    // The output of process_layer()
    std::string m_process_output;

public:
    GCodeAnalyzer();

    // Reinitialize the analyzer
    void reset();

    // Adds the gcode contained in the given string to the analysis and returns it after removing the workcodes
    const std::string& process_gcode(const std::string& gcode);

    // Calculates all data needed for gcode visualization
    void calc_gcode_preview_data(Print& print);

private:
    // Processes the given gcode line
    void _process_gcode_line(GCodeReader& reader, const GCodeReader::GCodeLine& line);

    // Move
    void _processG1(const GCodeReader::GCodeLine& line);

    // Firmware controlled Retract
    void _processG22(const GCodeReader::GCodeLine& line);

    // Firmware controlled Unretract
    void _processG23(const GCodeReader::GCodeLine& line);

    // Set to Absolute Positioning
    void _processG90(const GCodeReader::GCodeLine& line);

    // Set to Relative Positioning
    void _processG91(const GCodeReader::GCodeLine& line);

    // Set Position
    void _processG92(const GCodeReader::GCodeLine& line);

    // Set extruder to absolute mode
    void _processM82(const GCodeReader::GCodeLine& line);

    // Set extruder to relative mode
    void _processM83(const GCodeReader::GCodeLine& line);

    // Processes T line (Select Tool)
    void _processT(const GCodeReader::GCodeLine& line);

    // Processes the tags
    // Returns true if any tag has been processed
    bool _process_tags(const GCodeReader::GCodeLine& line);

    // Processes extrusion role tag
    void _process_extrusion_role_tag(const std::string& comment, size_t pos);

    // Processes mm3_per_mm tag
    void _process_mm3_per_mm_tag(const std::string& comment, size_t pos);

    // Processes width tag
    void _process_width_tag(const std::string& comment, size_t pos);

    // Processes height tag
    void _process_height_tag(const std::string& comment, size_t pos);

    void _set_units(EUnits units);
    EUnits _get_units() const;

    void _set_positioning_xyz_type(EPositioningType type);
    EPositioningType _get_positioning_xyz_type() const;

    void _set_positioning_e_type(EPositioningType type);
    EPositioningType _get_positioning_e_type() const;

    void _set_extrusion_role(ExtrusionRole extrusion_role);
    ExtrusionRole _get_extrusion_role() const;

    void _set_extruder_id(unsigned int id);
    unsigned int _get_extruder_id() const;

    void _set_mm3_per_mm(double value);
    double _get_mm3_per_mm() const;

    void _set_width(float width);
    float _get_width() const;

    void _set_height(float height);
    float _get_height() const;

    void _set_feedrate(float feedrate_mm_sec);
    float _get_feedrate() const;

    void _set_axis_position(EAxis axis, float position);
    float _get_axis_position(EAxis axis) const;

    // Sets axes position to zero
    void _reset_axes_position();

    void _set_start_position(const Pointf3& position);
    const Pointf3& _get_start_position() const;

    void _set_start_extrusion(float extrusion);
    float _get_start_extrusion() const;
    float _get_delta_extrusion() const;

    // Returns current xyz position (from m_state.position[])
    Pointf3 _get_end_position() const;

    // Adds a new move with the given data
    void _store_move(GCodeMove::EType type);

    // Checks if the given int is a valid extrusion role (contained into enum ExtrusionRole)
    bool _is_valid_extrusion_role(int value) const;

    void _calc_gcode_preview_extrusion_layers(Print& print);
    void _calc_gcode_preview_travel(Print& print);
    void _calc_gcode_preview_retractions(Print& print);
    void _calc_gcode_preview_unretractions(Print& print);
};

GCodeAnalyzer::PreviewData::Color operator + (const GCodeAnalyzer::PreviewData::Color& c1, const GCodeAnalyzer::PreviewData::Color& c2);
GCodeAnalyzer::PreviewData::Color operator * (float f, const GCodeAnalyzer::PreviewData::Color& color);

#endif // ENRICO_GCODE_PREVIEW
//############################################################################################################

} // namespace Slic3r

#endif /* slic3r_GCode_Analyzer_hpp_ */
