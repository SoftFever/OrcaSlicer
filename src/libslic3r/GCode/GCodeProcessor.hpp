#ifndef slic3r_GCodeProcessor_hpp_
#define slic3r_GCodeProcessor_hpp_

#if ENABLE_GCODE_VIEWER
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/ExtrusionEntity.hpp"

#include <array>
#include <vector>

namespace Slic3r {

    class GCodeProcessor
    {
    public:
        static const std::string Extrusion_Role_Tag;
        static const std::string Width_Tag;
        static const std::string Height_Tag;
        static const std::string Mm3_Per_Mm_Tag;
        static const std::string Color_Change_Tag;
        static const std::string Pause_Print_Tag;
        static const std::string Custom_Code_Tag;

    private:
        using AxisCoords = std::array<float, 4>;
        using ExtrudersColor = std::vector<unsigned char>;

        enum class EUnits : unsigned char
        {
            Millimeters,
            Inches
        };

        enum class EPositioningType : unsigned char
        {
            Absolute,
            Relative
        };

        struct CachedPosition
        {
            AxisCoords position; // mm
            float feedrate;  // mm/s

            void reset();
        };

        struct CpColor
        {
            unsigned char counter;
            unsigned char current;

            void reset();
        };

    public:
        enum class EMoveType : unsigned char
        {
            Noop,
            Retract,
            Unretract,
            Tool_change,
            Color_change,
            Pause_Print,
            Custom_GCode,
            Travel,
            Extrude,
            Count
        };

        struct MoveVertex
        {
            EMoveType type{ EMoveType::Noop };
            ExtrusionRole extrusion_role{ erNone };
            unsigned char extruder_id{ 0 };
            unsigned char cp_color_id{ 0 };
            Vec3f position{ Vec3f::Zero() }; // mm
            float delta_extruder{ 0.0f }; // mm
            float feedrate{ 0.0f }; // mm/s
            float width{ 0.0f }; // mm
            float height{ 0.0f }; // mm
            float mm3_per_mm{ 0.0f };
            float fan_speed{ 0.0f }; // percentage
            float time{ 0.0f }; // s

            float volumetric_rate() const { return feedrate * mm3_per_mm; }

            std::string to_string() const
            {
                std::string str = std::to_string((int)type);
                str += ", " + std::to_string((int)extrusion_role);
                str += ", " + Slic3r::to_string((Vec3d)position.cast<double>());
                str += ", " + std::to_string(extruder_id);
                str += ", " + std::to_string(cp_color_id);
                str += ", " + std::to_string(feedrate);
                str += ", " + std::to_string(width);
                str += ", " + std::to_string(height);
                str += ", " + std::to_string(mm3_per_mm);
                str += ", " + std::to_string(fan_speed);
                return str;
            }
        };

        struct Result
        {
            unsigned int id;
            std::vector<MoveVertex> moves;
#if ENABLE_GCODE_VIEWER_STATISTICS
            long long time{ 0 };
            void reset() { time = 0; moves = std::vector<MoveVertex>(); }
#else
            void reset() { moves = std::vector<MoveVertex>(); }
#endif // ENABLE_GCODE_VIEWER_STATISTICS
        };

    private:
        GCodeReader m_parser;

        EUnits m_units;
        EPositioningType m_global_positioning_type;
        EPositioningType m_e_local_positioning_type;
        std::vector<Vec3f> m_extruder_offsets;
        GCodeFlavor m_flavor;

        AxisCoords m_start_position; // mm
        AxisCoords m_end_position;   // mm
        AxisCoords m_origin;         // mm
        CachedPosition m_cached_position;

        float m_feedrate;  // mm/s
        float m_width;     // mm
        float m_height;    // mm
        float m_mm3_per_mm;
        float m_fan_speed; // percentage
        ExtrusionRole m_extrusion_role;
        unsigned char m_extruder_id;
        ExtrudersColor m_extruders_color;
        CpColor m_cp_color;

        Result m_result;
        static unsigned int s_result_id;

    public:
        GCodeProcessor() { reset(); }

        void apply_config(const PrintConfig& config);
        void reset();

        const Result& get_result() const { return m_result; }
        Result&& extract_result() { return std::move(m_result); }

        // Process the gcode contained in the file with the given filename
        void process_file(const std::string& filename);

    private:
        void process_gcode_line(const GCodeReader::GCodeLine& line);

        // Process tags embedded into comments
        void process_tags(const std::string& comment);

        // Move
        void process_G0(const GCodeReader::GCodeLine& line);
        void process_G1(const GCodeReader::GCodeLine& line);

        // Retract
        void process_G10(const GCodeReader::GCodeLine& line);

        // Unretract
        void process_G11(const GCodeReader::GCodeLine& line);

        // Firmware controlled Retract
        void process_G22(const GCodeReader::GCodeLine& line);

        // Firmware controlled Unretract
        void process_G23(const GCodeReader::GCodeLine& line);

        // Set to Absolute Positioning
        void process_G90(const GCodeReader::GCodeLine& line);

        // Set to Relative Positioning
        void process_G91(const GCodeReader::GCodeLine& line);

        // Set Position
        void process_G92(const GCodeReader::GCodeLine& line);

        // Set extruder to absolute mode
        void process_M82(const GCodeReader::GCodeLine& line);

        // Set extruder to relative mode
        void process_M83(const GCodeReader::GCodeLine& line);

        // Set fan speed
        void process_M106(const GCodeReader::GCodeLine& line);

        // Disable fan
        void process_M107(const GCodeReader::GCodeLine& line);

        // Set tool (Sailfish)
        void process_M108(const GCodeReader::GCodeLine& line);

        // Recall stored home offsets
        void process_M132(const GCodeReader::GCodeLine& line);

        // Set tool (MakerWare)
        void process_M135(const GCodeReader::GCodeLine& line);

        // Repetier: Store x, y and z position
        void process_M401(const GCodeReader::GCodeLine& line);

        // Repetier: Go to stored position
        void process_M402(const GCodeReader::GCodeLine& line);

        // Processes T line (Select Tool)
        void process_T(const GCodeReader::GCodeLine& line);
        void process_T(const std::string& command);

        void store_move_vertex(EMoveType type);
   };

} /* namespace Slic3r */

#endif // ENABLE_GCODE_VIEWER

#endif /* slic3r_GCodeProcessor_hpp_ */


