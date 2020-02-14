#ifndef slic3r_GCodeProcessor_hpp_
#define slic3r_GCodeProcessor_hpp_

#if ENABLE_GCODE_VIEWER
#include "../GCodeReader.hpp"

namespace Slic3r {

    class GCodeProcessor
    {
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

        struct MoveStep
        {
            AxisCoords position; // mm
            float feedrate; // mm/s
            EMoveType type;
        };

        using MoveStepsList = std::vector<MoveStep>;

        GCodeReader m_parser;

        EUnits m_units;
        EPositioningType m_global_positioning_type;
        EPositioningType m_e_local_positioning_type;

        AxisCoords m_start_position; // mm
        AxisCoords m_end_position;   // mm
        AxisCoords m_origin;         // mm

        float m_feedrate; // mm/s

        MoveStepsList m_moves;


    public:
        GCodeProcessor() { reset(); }

        void apply_config(const PrintConfig& config);

        void reset();

        // Process the gcode contained in the file with the given filename
        // Return false if any error occourred
        void process_file(const std::string& filename);

    private:
        void process_gcode_line(const GCodeReader::GCodeLine& line);

        // Process tags embedded into comments
        void process_comment(const GCodeReader::GCodeLine& line);

        // Move
        void process_G1(const GCodeReader::GCodeLine& line);
    };

} /* namespace Slic3r */

#endif // ENABLE_GCODE_VIEWER

#endif /* slic3r_GCodeProcessor_hpp_ */


