#ifndef slic3r_GCodeProcessor_hpp_
#define slic3r_GCodeProcessor_hpp_

#include "libslic3r/GCodeReader.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/ExtrusionEntity.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/CustomGCode.hpp"

#include <cstdint>
#include <array>
#include <vector>
#include <string>
#include <string_view>
#if ENABLE_SEAMS_VISUALIZATION
#include <optional>
#endif // ENABLE_SEAMS_VISUALIZATION

namespace Slic3r {

    enum class EMoveType : unsigned char
    {
        Noop,
        Retract,
        Unretract,
#if ENABLE_SEAMS_VISUALIZATION
        Seam,
#endif // ENABLE_SEAMS_VISUALIZATION
        Tool_change,
        Color_change,
        Pause_Print,
        Custom_GCode,
        Travel,
        Wipe,
        Extrude,
        Count
    };

    struct PrintEstimatedStatistics
    {
        enum class ETimeMode : unsigned char
        {
            Normal,
            Stealth,
            Count
        };

        struct Mode
        {
            float time;
            std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> custom_gcode_times;
            std::vector<std::pair<EMoveType, float>> moves_times;
            std::vector<std::pair<ExtrusionRole, float>> roles_times;
            std::vector<float> layers_times;

            void reset() {
                time = 0.0f;
                custom_gcode_times.clear();
                moves_times.clear();
                roles_times.clear();
                layers_times.clear();
            }
        };

        std::vector<double>                                 volumes_per_color_change;
        std::map<size_t, double>                            volumes_per_extruder;
        std::map<ExtrusionRole, std::pair<double, double>>  used_filaments_per_role;

        std::array<Mode, static_cast<size_t>(ETimeMode::Count)> modes;

        PrintEstimatedStatistics() { reset(); }

        void reset() {
            for (auto m : modes) {
                m.reset();
            }
            volumes_per_color_change.clear();
            volumes_per_extruder.clear();
            used_filaments_per_role.clear();
        }
    };

    class GCodeProcessor
    {
#if ENABLE_VALIDATE_CUSTOM_GCODE
        static const std::vector<std::string> Reserved_Tags;
#endif // ENABLE_VALIDATE_CUSTOM_GCODE
    public:
#if ENABLE_VALIDATE_CUSTOM_GCODE
        enum class ETags : unsigned char
        {
            Role,
            Wipe_Start,
            Wipe_End,
            Height,
            Width,
            Layer_Change,
            Color_Change,
            Pause_Print,
            Custom_Code,
            First_Line_M73_Placeholder,
            Last_Line_M73_Placeholder,
            Estimated_Printing_Time_Placeholder
        };

        static const std::string& reserved_tag(ETags tag) { return Reserved_Tags[static_cast<unsigned char>(tag)]; }
        // checks the given gcode for reserved tags and returns true when finding the 1st (which is returned into found_tag) 
        static bool contains_reserved_tag(const std::string& gcode, std::string& found_tag);
        // checks the given gcode for reserved tags and returns true when finding any
        // (the first max_count found tags are returned into found_tag)
        static bool contains_reserved_tags(const std::string& gcode, unsigned int max_count, std::vector<std::string>& found_tag);
#else
        static const std::string Extrusion_Role_Tag;
        static const std::string Wipe_Start_Tag;
        static const std::string Wipe_End_Tag;
        static const std::string Height_Tag;
        static const std::string Layer_Change_Tag;
        static const std::string Color_Change_Tag;
        static const std::string Pause_Print_Tag;
        static const std::string Custom_Code_Tag;
        static const std::string First_Line_M73_Placeholder_Tag;
        static const std::string Last_Line_M73_Placeholder_Tag;
        static const std::string Estimated_Printing_Time_Placeholder_Tag;
        static const std::string Width_Tag;
#endif // ENABLE_VALIDATE_CUSTOM_GCODE

        static const float Wipe_Width;
        static const float Wipe_Height;

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        static const std::string Mm3_Per_Mm_Tag;
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

    private:
        using AxisCoords = std::array<float, 4>;
        using ExtruderColors = std::vector<unsigned char>;
        using ExtruderTemps = std::vector<float>;

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
            unsigned int g1_line_id{ 0 };
            unsigned int layer_id{ 0 };
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

#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        struct MoveVertex
        {
            unsigned int gcode_id{ 0 };
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
            float temperature{ 0.0f }; // Celsius degrees
            float time{ 0.0f }; // s

            float volumetric_rate() const { return feedrate * mm3_per_mm; }
        };
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER

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

            struct G1LinesCacheItem
            {
                unsigned int id;
                float elapsed_time;
            };

            bool enabled;
            float acceleration; // mm/s^2
            // hard limit for the acceleration, to which the firmware will clamp.
            float max_acceleration; // mm/s^2
            float travel_acceleration; // mm/s^2
            // hard limit for the travel acceleration, to which the firmware will clamp.
            float max_travel_acceleration; // mm/s^2
            float extrude_factor_override_percentage;
            float time; // s
#if ENABLE_EXTENDED_M73_LINES
            struct StopTime
            {
                unsigned int g1_line_id;
                float elapsed_time;
            };
            std::vector<StopTime> stop_times;
            std::string line_m73_main_mask;
            std::string line_m73_stop_mask;
#else
            std::string line_m73_mask;
#endif // ENABLE_EXTENDED_M73_LINES
            State curr;
            State prev;
            CustomGCodeTime gcode_time;
            std::vector<TimeBlock> blocks;
            std::vector<G1LinesCacheItem> g1_times_cache;
            std::array<float, static_cast<size_t>(EMoveType::Count)> moves_time;
            std::array<float, static_cast<size_t>(ExtrusionRole::erCount)> roles_time;
            std::vector<float> layers_time;

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
            // whether or not to export post-process the gcode to export lines M73 in it
            bool export_remaining_time_enabled;
            // allow to skip the lines M201/M203/M204/M205 generated by GCode::print_machine_envelope() for non-Normal time estimate mode
            bool machine_envelope_processing_enabled;
            MachineEnvelopeConfig machine_limits;
            // Additional load / unload times for a filament exchange sequence.
            std::vector<float> filament_load_times;
            std::vector<float> filament_unload_times;
            std::array<TimeMachine, static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Count)> machines;

            void reset();

            // post process the file with the given filename to add remaining time lines M73
#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
            // and updates moves' gcode ids accordingly
            void post_process(const std::string& filename, std::vector<MoveVertex>& moves);
#else
            void post_process(const std::string& filename);
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        };

        struct UsedFilaments  // filaments per ColorChange
        {
            double color_change_cache;
            std::vector<double> volumes_per_color_change;

            double tool_change_cache;
            std::map<size_t, double> volumes_per_extruder;

            double role_cache;
            std::map<ExtrusionRole, std::pair<double, double>> filaments_per_role;

            void reset();

            void increase_caches(double extruded_volume);

            void process_color_change_cache();
            void process_extruder_cache(GCodeProcessor* processor);
            void process_role_cache(GCodeProcessor* processor);
            void process_caches(GCodeProcessor* processor);

            friend class GCodeProcessor;
        };

    public:
#if !ENABLE_GCODE_LINES_ID_IN_H_SLIDER
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
            float temperature{ 0.0f }; // Celsius degrees
            float time{ 0.0f }; // s

            float volumetric_rate() const { return feedrate * mm3_per_mm; }
        };
#endif // !ENABLE_GCODE_LINES_ID_IN_H_SLIDER

        struct Result
        {
            struct SettingsIds
            {
                std::string print;
                std::vector<std::string> filament;
                std::string printer;

                void reset() {
                    print = "";
                    filament = std::vector<std::string>();
                    printer = "";
                }
            };
#if ENABLE_GCODE_WINDOW
            std::string filename;
#endif // ENABLE_GCODE_WINDOW
            unsigned int id;
            std::vector<MoveVertex> moves;
            Pointfs bed_shape;
            SettingsIds settings_ids;
            size_t extruders_count;
            std::vector<std::string> extruder_colors;
            std::vector<float> filament_diameters;
            std::vector<float> filament_densities;
            PrintEstimatedStatistics print_statistics;

#if ENABLE_GCODE_VIEWER_STATISTICS
            int64_t time{ 0 };
#endif // ENABLE_GCODE_VIEWER_STATISTICS
            void reset();
        };

#if ENABLE_SEAMS_VISUALIZATION
        class SeamsDetector
        {
            bool m_active{ false };
            std::optional<Vec3f> m_first_vertex;

        public:
            void activate(bool active) {
                if (m_active != active) {
                    m_active = active;
                    if (m_active)
                        m_first_vertex.reset();
                }
            }

            std::optional<Vec3f> get_first_vertex() const { return m_first_vertex; }
            void set_first_vertex(const Vec3f& vertex) { m_first_vertex = vertex; }

            bool is_active() const { return m_active; }
            bool has_first_vertex() const { return m_first_vertex.has_value(); }
        };
#endif // ENABLE_SEAMS_VISUALIZATION

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        struct DataChecker
        {
            struct Error
            {
                float value;
                float tag_value;
                ExtrusionRole role;
            };

            std::string type;
            float threshold{ 0.01f };
            float last_tag_value{ 0.0f };
            unsigned int count{ 0 };
            std::vector<Error> errors;

            DataChecker(const std::string& type, float threshold)
                : type(type), threshold(threshold)
            {}

            void update(float value, ExtrusionRole role) {
                if (role != erCustom) {
                    ++count;
                    if (last_tag_value != 0.0f) {
                        if (std::abs(value - last_tag_value) / last_tag_value > threshold)
                            errors.push_back({ value, last_tag_value, role });
                    }
                }
            }

            void reset() { last_tag_value = 0.0f; errors.clear(); count = 0; }

            std::pair<float, float> get_min() const {
                float delta_min = FLT_MAX;
                float perc_min = 0.0f;
                for (const Error& e : errors) {
                    if (delta_min > e.value - e.tag_value) {
                        delta_min = e.value - e.tag_value;
                        perc_min = 100.0f * delta_min / e.tag_value;
                    }
                }
                return { delta_min, perc_min };
            }

            std::pair<float, float> get_max() const {
                float delta_max = -FLT_MAX;
                float perc_max = 0.0f;
                for (const Error& e : errors) {
                    if (delta_max < e.value - e.tag_value) {
                        delta_max = e.value - e.tag_value;
                        perc_max = 100.0f * delta_max / e.tag_value;
                    }
                }
                return { delta_max, perc_max };
            }

            void output() const {
                if (!errors.empty()) {
                    std::cout << type << ":\n";
                    std::cout << "Errors: " << errors.size() << " (" << 100.0f * float(errors.size()) / float(count) << "%)\n";
                    auto [min, perc_min] = get_min();
                    auto [max, perc_max] = get_max();
                    std::cout << "min: " << min << "(" << perc_min << "%) - max: " << max << "(" << perc_max << "%)\n";
                }
            }
        };
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

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
        bool m_wiping;

#if ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        unsigned int m_line_id;
#if ENABLE_SEAMS_VISUALIZATION
        unsigned int m_last_line_id;
#endif // ENABLE_SEAMS_VISUALIZATION
#endif // ENABLE_GCODE_LINES_ID_IN_H_SLIDER
        float m_feedrate; // mm/s
        float m_width; // mm
        float m_height; // mm
        float m_forced_width; // mm
        float m_forced_height; // mm
        float m_mm3_per_mm;
        float m_fan_speed; // percentage
        ExtrusionRole m_extrusion_role;
        unsigned char m_extruder_id;
        ExtruderColors m_extruder_colors;
        ExtruderTemps m_extruder_temps;
        float m_extruded_last_z;
#if ENABLE_START_GCODE_VISUALIZATION
        float m_first_layer_height; // mm
        bool m_processing_start_custom_gcode;
#endif // ENABLE_START_GCODE_VISUALIZATION
        unsigned int m_g1_line_id;
        unsigned int m_layer_id;
        CpColor m_cp_color;
        bool m_use_volumetric_e;
#if ENABLE_SEAMS_VISUALIZATION
        SeamsDetector m_seams_detector;
#endif // ENABLE_SEAMS_VISUALIZATION

        enum class EProducer
        {
            Unknown,
            PrusaSlicer,
            Slic3rPE,
            Slic3r,
            Cura,
            Simplify3D,
            CraftWare,
            ideaMaker,
            KissSlicer
        };

        static const std::vector<std::pair<GCodeProcessor::EProducer, std::string>> Producers;
        EProducer m_producer;
        bool m_producers_enabled;

        TimeProcessor m_time_processor;
        UsedFilaments m_used_filaments;

        Result m_result;
        static unsigned int s_result_id;

#if ENABLE_GCODE_VIEWER_DATA_CHECKING
        DataChecker m_mm3_per_mm_compare{ "mm3_per_mm", 0.01f };
        DataChecker m_height_compare{ "height", 0.01f };
        DataChecker m_width_compare{ "width", 0.01f };
#endif // ENABLE_GCODE_VIEWER_DATA_CHECKING

    public:
        GCodeProcessor();

        void apply_config(const PrintConfig& config);
        void enable_stealth_time_estimator(bool enabled);
        bool is_stealth_time_estimator_enabled() const {
            return m_time_processor.machines[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Stealth)].enabled;
        }
        void enable_machine_envelope_processing(bool enabled) { m_time_processor.machine_envelope_processing_enabled = enabled; }
        void enable_producers(bool enabled) { m_producers_enabled = enabled; }
        void reset();

        const Result& get_result() const { return m_result; }
        Result&& extract_result() { return std::move(m_result); }

        // Process the gcode contained in the file with the given filename
        // throws CanceledException through print->throw_if_canceled() (sent by the caller as callback).
        void process_file(const std::string& filename, bool apply_postprocess, std::function<void()> cancel_callback = nullptr);

        float get_time(PrintEstimatedStatistics::ETimeMode mode) const;
        std::string get_time_dhm(PrintEstimatedStatistics::ETimeMode mode) const;
        std::vector<std::pair<CustomGCode::Type, std::pair<float, float>>> get_custom_gcode_times(PrintEstimatedStatistics::ETimeMode mode, bool include_remaining) const;

        std::vector<std::pair<EMoveType, float>> get_moves_time(PrintEstimatedStatistics::ETimeMode mode) const;
        std::vector<std::pair<ExtrusionRole, float>> get_roles_time(PrintEstimatedStatistics::ETimeMode mode) const;
        std::vector<float> get_layers_time(PrintEstimatedStatistics::ETimeMode mode) const;

    private:
        void apply_config(const DynamicPrintConfig& config);
        void apply_config_simplify3d(const std::string& filename);
        void process_gcode_line(const GCodeReader::GCodeLine& line);

        // Process tags embedded into comments
        void process_tags(const std::string_view comment);
        bool process_producers_tags(const std::string_view comment);
        bool process_prusaslicer_tags(const std::string_view comment);
        bool process_cura_tags(const std::string_view comment);
        bool process_simplify3d_tags(const std::string_view comment);
        bool process_craftware_tags(const std::string_view comment);
        bool process_ideamaker_tags(const std::string_view comment);
        bool process_kissslicer_tags(const std::string_view comment);

        bool detect_producer(const std::string_view comment);

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

        // Move to origin
        void process_G28(const GCodeReader::GCodeLine& line);

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

        // Set extruder temperature
        void process_M104(const GCodeReader::GCodeLine& line);

        // Set fan speed
        void process_M106(const GCodeReader::GCodeLine& line);

        // Disable fan
        void process_M107(const GCodeReader::GCodeLine& line);

        // Set tool (Sailfish)
        void process_M108(const GCodeReader::GCodeLine& line);

        // Set extruder temperature and wait
        void process_M109(const GCodeReader::GCodeLine& line);

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
        void process_T(const std::string_view command);

        void store_move_vertex(EMoveType type);

        float minimum_feedrate(PrintEstimatedStatistics::ETimeMode mode, float feedrate) const;
        float minimum_travel_feedrate(PrintEstimatedStatistics::ETimeMode mode, float feedrate) const;
        float get_axis_max_feedrate(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const;
        float get_axis_max_acceleration(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const;
        float get_axis_max_jerk(PrintEstimatedStatistics::ETimeMode mode, Axis axis) const;
        float get_retract_acceleration(PrintEstimatedStatistics::ETimeMode mode) const;
        float get_acceleration(PrintEstimatedStatistics::ETimeMode mode) const;
        void  set_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value);
        float get_travel_acceleration(PrintEstimatedStatistics::ETimeMode mode) const;
        void  set_travel_acceleration(PrintEstimatedStatistics::ETimeMode mode, float value);
        float get_filament_load_time(size_t extruder_id);
        float get_filament_unload_time(size_t extruder_id);

        void process_custom_gcode_time(CustomGCode::Type code);
        void process_filaments(CustomGCode::Type code);

        // Simulates firmware st_synchronize() call
        void simulate_st_synchronize(float additional_time = 0.0f);

        void update_estimated_times_stats();
   };

} /* namespace Slic3r */

#endif /* slic3r_GCodeProcessor_hpp_ */


