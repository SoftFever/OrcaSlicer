#ifndef slic3r_GCodeProcessor_hpp_
#define slic3r_GCodeProcessor_hpp_

#if ENABLE_GCODE_VIEWER
#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/CustomGCode.hpp"

#include <array>
#include <vector>

namespace Slic3r {

    struct PrintStatistics;

    class GCodeProcessor
    {
    public:
        static const std::string Extrusion_Role_Tag;
        static const std::string Width_Tag;
        static const std::string Height_Tag;
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
            float feedrate; // mm/s

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

        struct FeedrateProfile
        {
            float entry{ 0.0f }; // mm/s
            float cruise{ 0.0f }; // mm/s
            float exit{ 0.0f }; // mm/s
        };

        struct Trapezoid
        {
            float accelerate_until{ 0.0f }; // mm
            float decelerate_after{ 0.0f }; // mm
            float cruise_feedrate{ 0.0f }; // mm/sec

            float acceleration_time(float entry_feedrate, float acceleration) const;
            float cruise_time() const;
            float deceleration_time(float distance, float acceleration) const;
            float cruise_distance() const;
        };

        struct TimeBlock
        {
            struct Flags
            {
                bool recalculate{ false };
                bool nominal_length{ false };
            };

            EMoveType move_type{ EMoveType::Noop };
            ExtrusionRole role{ erNone };
            float distance{ 0.0f }; // mm
            float acceleration{ 0.0f }; // mm/s^2
            float max_entry_speed{ 0.0f }; // mm/s
            float safe_feedrate{ 0.0f }; // mm/s
            Flags flags;
            FeedrateProfile feedrate_profile;
            Trapezoid trapezoid;

            // Calculates this block's trapezoid
            void calculate_trapezoid();

            float time() const;
        };

        enum class ETimeMode : unsigned char
        {
            Normal,
            Stealth,
            Count
        };

    private:
        struct TimeMachine
        {
            struct State
            {
                float feedrate; // mm/s
                float safe_feedrate; // mm/s
                AxisCoords axis_feedrate; // mm/s
                AxisCoords abs_axis_feedrate; // mm/s

                void reset();
            };

            struct CustomGCodeTime
            {
                bool needed;
                float cache;
                std::vector<std::pair<CustomGCode::Type, float>> times;

                void reset();
            };

            bool enabled;
            float acceleration; // mm/s^2
            float extrude_factor_override_percentage;
            float time; // s
            State curr;
            State prev;
            CustomGCodeTime gcode_time;
            std::vector<TimeBlock> blocks;
            std::array<float, static_cast<size_t>(EMoveType::Count)> moves_time;
            std::array<float, static_cast<size_t>(ExtrusionRole::erCount)> roles_time;

            void reset();

            // Simulates firmware st_synchronize() call
            void simulate_st_synchronize(float additional_time = 0.0f);
            void calculate_time(size_t keep_last_n_blocks = 0);
        };

        struct TimeProcessor
        {
            struct Planner
            {
                // Size of the firmware planner queue. The old 8-bit Marlins usually just managed 16 trapezoidal blocks.
                // Let's be conservative and plan for newer boards with more memory.
                static constexpr size_t queue_size = 64;
                // The firmware recalculates last planner_queue_size trapezoidal blocks each time a new block is added.
                // We are not simulating the firmware exactly, we calculate a sequence of blocks once a reasonable number of blocks accumulate.
                static constexpr size_t refresh_threshold = queue_size * 4;
            };

            // extruder_id is currently used to correctly calculate filament load / unload times into the total print time.
            // This is currently only really used by the MK3 MMU2:
            // extruder_unloaded = true means no filament is loaded yet, all the filaments are parked in the MK3 MMU2 unit.
            bool extruder_unloaded;
            MachineEnvelopeConfig machine_limits;
            // Additional load / unload times for a filament exchange sequence.
            std::vector<float> filament_load_times;
            std::vector<float> filament_unload_times;
            std::array<TimeMachine, static_cast<size_t>(ETimeMode::Count)> machines;

            void reset();
        };

    public:
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
        AxisCoords m_end_position; // mm
        AxisCoords m_origin; // mm
        CachedPosition m_cached_position;

        float m_feedrate; // mm/s
        float m_width; // mm
        float m_height; // mm
        float m_mm3_per_mm;
        float m_fan_speed; // percentage
        ExtrusionRole m_extrusion_role;
        unsigned char m_extruder_id;
        ExtrudersColor m_extruders_color;
        std::vector<float> m_filament_diameters;
        CpColor m_cp_color;

        enum class EProducer
        {
            Unknown,
            PrusaSlicer,
            Cura,
            Simplify3D,
            CraftWare,
            ideaMaker
        };

        static const std::vector<std::pair<GCodeProcessor::EProducer, std::string>> Producers;
        EProducer m_producer;
        bool m_producers_enabled;

        TimeProcessor m_time_processor;

        Result m_result;
        static unsigned int s_result_id;

    public:
        GCodeProcessor() { reset(); }

        void apply_config(const PrintConfig& config);
        void enable_stealth_time_estimator(bool enabled);
        void enable_producers(bool enabled) { m_producers_enabled = enabled; }
        void reset();

        const Result& get_result() const { return m_result; }
        Result&& extract_result() { return std::move(m_result); }

        // Process the gcode contained in the file with the given filename
        void process_file(const std::string& filename);

        void update_print_stats_estimated_times(PrintStatistics& print_statistics);
        float get_time(ETimeMode mode) const;
        std::string get_time_dhm(ETimeMode mode) const;
        std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> get_custom_gcode_times(ETimeMode mode, bool include_remaining) const;

        std::vector<std::pair<EMoveType, float>> get_moves_time(ETimeMode mode) const;
        std::vector<std::pair<ExtrusionRole, float>> get_roles_time(ETimeMode mode) const;

    private:
        void process_gcode_line(const GCodeReader::GCodeLine& line);

        // Process tags embedded into comments
        void process_tags(const std::string& comment);
        bool process_producers_tags(const std::string& comment);
        bool process_prusaslicer_tags(const std::string& comment);
        bool process_cura_tags(const std::string& comment);
        bool process_simplify3d_tags(const std::string& comment);
        bool process_craftware_tags(const std::string& comment);
        bool process_ideamaker_tags(const std::string& comment);

        bool detect_producer(const std::string& comment);

        // Move
        void process_G0(const GCodeReader::GCodeLine& line);
        void process_G1(const GCodeReader::GCodeLine& line);

        // Retract
        void process_G10(const GCodeReader::GCodeLine& line);

        // Unretract
        void process_G11(const GCodeReader::GCodeLine& line);

        // Set Units to Inches
        void process_G20(const GCodeReader::GCodeLine& line);

        // Set Units to Millimeters
        void process_G21(const GCodeReader::GCodeLine& line);

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

        // Sleep or Conditional stop
        void process_M1(const GCodeReader::GCodeLine& line);

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

        // Set max printing acceleration
        void process_M201(const GCodeReader::GCodeLine& line);

        // Set maximum feedrate
        void process_M203(const GCodeReader::GCodeLine& line);

        // Set default acceleration
        void process_M204(const GCodeReader::GCodeLine& line);

        // Advanced settings
        void process_M205(const GCodeReader::GCodeLine& line);

        // Set extrude factor override percentage
        void process_M221(const GCodeReader::GCodeLine& line);

        // Repetier: Store x, y and z position
        void process_M401(const GCodeReader::GCodeLine& line);

        // Repetier: Go to stored position
        void process_M402(const GCodeReader::GCodeLine& line);

        // Set allowable instantaneous speed change
        void process_M566(const GCodeReader::GCodeLine& line);

        // Unload the current filament into the MK3 MMU2 unit at the end of print.
        void process_M702(const GCodeReader::GCodeLine& line);

        // Processes T line (Select Tool)
        void process_T(const GCodeReader::GCodeLine& line);
        void process_T(const std::string& command);

        void store_move_vertex(EMoveType type);

        float minimum_feedrate(ETimeMode mode, float feedrate) const;
        float minimum_travel_feedrate(ETimeMode mode, float feedrate) const;
        float get_axis_max_feedrate(ETimeMode mode, Axis axis) const;
        float get_axis_max_acceleration(ETimeMode mode, Axis axis) const;
        float get_axis_max_jerk(ETimeMode mode, Axis axis) const;
        float get_retract_acceleration(ETimeMode mode) const;
        float get_acceleration(ETimeMode mode) const;
        void set_acceleration(ETimeMode mode, float value);
        float get_filament_load_time(size_t extruder_id);
        float get_filament_unload_time(size_t extruder_id);

        void process_custom_gcode_time(CustomGCode::Type code);

        // Simulates firmware st_synchronize() call
        void simulate_st_synchronize(float additional_time = 0.0f);
   };

} /* namespace Slic3r */

#endif // ENABLE_GCODE_VIEWER

#endif /* slic3r_GCodeProcessor_hpp_ */


