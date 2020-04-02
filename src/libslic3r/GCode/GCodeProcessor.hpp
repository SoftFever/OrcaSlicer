#ifndef slic3r_GCodeProcessor_hpp_
#define slic3r_GCodeProcessor_hpp_

#if ENABLE_GCODE_VIEWER
#include "../GCodeReader.hpp"
#include "../Point.hpp"
#include "../ExtrusionEntity.hpp"

namespace Slic3r {

    class GCodeProcessor
    {
    public:
        static const std::string Extrusion_Role_Tag;
        static const std::string Width_Tag;
        static const std::string Height_Tag;

    private:
        using AxisCoords = std::array<float, 4>;

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

        enum class EMoveType : unsigned char
        {
            Noop,
            Retract,
            Unretract,
            Tool_change,
            Travel,
            Extrude,
            Num_Types
        };

    public:
        struct MoveVertex
        {
            EMoveType type{ EMoveType::Noop };
            ExtrusionRole extrusion_role{ erNone };
            Vec3f position{ Vec3f::Zero() }; // mm
            float feedrate{ 0.0f }; // mm/s
            float width{ 0.0f }; // mm
            float height{ 0.0f }; // mm
            unsigned int extruder_id{ 0 };

            std::string to_string() const
            {
                std::string str = std::to_string((int)type);
                str += ", " + std::to_string((int)extrusion_role);
                str += ", " + Slic3r::to_string((Vec3d)position.cast<double>());
                str += ", " + std::to_string(extruder_id);
                str += ", " + std::to_string(feedrate);
                str += ", " + std::to_string(width);
                str += ", " + std::to_string(height);
                return str;
            }
        };

        struct Result
        {
            std::vector<MoveVertex> moves;
            void reset() { moves = std::vector<MoveVertex>(); }
        };

    private:
        GCodeReader m_parser;

        EUnits m_units;
        EPositioningType m_global_positioning_type;
        EPositioningType m_e_local_positioning_type;
        std::vector<Vec3f> m_extruder_offsets;

        AxisCoords m_start_position; // mm
        AxisCoords m_end_position;   // mm
        AxisCoords m_origin;         // mm

        float m_feedrate; // mm/s
        float m_width;    // mm
        float m_height;   // mm
        ExtrusionRole m_extrusion_role;
        unsigned int m_extruder_id;

        Result m_result;

    public:
        GCodeProcessor() { reset(); }

        void apply_config(const PrintConfig& config);
        void reset();

        const Result& get_result() const { return m_result; }
        Result&& extract_result() { return std::move(m_result); }

        // Process the gcode contained in the file with the given filename
        // Return false if any error occourred
        void process_file(const std::string& filename);

    private:
        void process_gcode_line(const GCodeReader::GCodeLine& line);

        // Process tags embedded into comments
        void process_tags(const std::string& comment);

        // Move
        void process_G1(const GCodeReader::GCodeLine& line);

        // Set to Absolute Positioning
        void processG90(const GCodeReader::GCodeLine& line);

        // Set to Relative Positioning
        void processG91(const GCodeReader::GCodeLine& line);

        // Set Position
        void processG92(const GCodeReader::GCodeLine& line);

        // Set extruder to absolute mode
        void processM82(const GCodeReader::GCodeLine& line);

        // Set extruder to relative mode
        void processM83(const GCodeReader::GCodeLine& line);

        // Processes T line (Select Tool)
        void processT(const GCodeReader::GCodeLine& line);

        void store_move_vertex(EMoveType type);
   };

} /* namespace Slic3r */

#endif // ENABLE_GCODE_VIEWER

#endif /* slic3r_GCodeProcessor_hpp_ */


