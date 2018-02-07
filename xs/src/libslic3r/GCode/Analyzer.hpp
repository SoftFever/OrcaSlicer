#ifndef slic3r_GCode_Analyzer_hpp_
#define slic3r_GCode_Analyzer_hpp_

#include "../libslic3r.h"
#include "../PrintConfig.hpp"
#include "../ExtrusionEntity.hpp"

#include "Point.hpp"
#include "GCodeReader.hpp"

namespace Slic3r {

class Print;

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
        float width;     // mm
        float height;    // mm
        float feedrate;  // mm/s

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

            std::vector<unsigned char> as_bytes() const;

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
            float step_size() const;

            const Color& get_color_at(float value) const;
            const Color& get_color_at_max() const;
        };

        struct LegendItem
        {
            std::string text;
            Color color;

            LegendItem(const std::string& text, const Color& color);
        };

        typedef std::vector<LegendItem> LegendItemsList;

        struct Extrusion
        {
            enum EViewType : unsigned char
            {
                FeatureType,
                Height,
                Width,
                Feedrate,
                Tool,
                Num_View_Types
            };

            static const unsigned int Num_Extrusion_Roles = (unsigned int)erMixed + 1;
            static const Color Default_Extrusion_Role_Colors[Num_Extrusion_Roles];
            static const std::string Default_Extrusion_Role_Names[Num_Extrusion_Roles];
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
            std::string role_names[Num_Extrusion_Roles];
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
                float feedrate;
                unsigned int extruder_id;
                Polyline3 polyline;

                Polyline(EType type, EDirection direction, float feedrate, unsigned int extruder_id, const Polyline3& polyline);
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

        std::string get_legend_title() const;
        LegendItemsList get_legend_items(const std::vector<float>& tool_colors) const;
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

    static bool is_valid_extrusion_role(ExtrusionRole role);

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

} // namespace Slic3r

#endif /* slic3r_GCode_Analyzer_hpp_ */
