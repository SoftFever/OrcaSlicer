#pragma once
#define calib_pressure_advance_dd

#include "GCode.hpp"
#include "GCodeWriter.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

enum class CalibMode : int {
    Calib_None = 0,
    Calib_PA_Line,
    Calib_PA_Pattern,
    Calib_PA_Tower,
    Calib_Temp_Tower,
    Calib_Vol_speed_Tower,
    Calib_VFA_Tower,
    Calib_Retraction_tower
};

struct Calib_Params {
    Calib_Params() : mode(CalibMode::Calib_None) { };
    double start, end, step;
    bool print_numbers;
    CalibMode mode;
};

class CalibPressureAdvance {
protected:
    CalibPressureAdvance() { };
    CalibPressureAdvance(GCode* gcodegen);
    ~CalibPressureAdvance() { };

    enum class DrawDigitMode {
        Left_To_Right,
        Bottom_To_Top
    };

    void delta_scale_bed_ext(BoundingBoxf& bed_ext) const { bed_ext.scale(1.0f / 1.41421f); }

    std::string move_to(Vec2d pt, GCodeWriter writer, std::string comment = std::string());
    double      e_per_mm(
        double line_width,
        double layer_height,
        float filament_diameter,
        float print_flow_ratio
    ) const;
    double      speed_adjust(int speed) const { return speed * 60; };
    
    std::string convert_number_to_string(double num) const;
    double      number_spacing() const { return m_digit_segment_len + m_digit_gap_len; };
    std::string draw_digit(
        double startx,
        double starty,
        char c,
        CalibPressureAdvance::DrawDigitMode mode,
        double line_width,
        double e_per_mm,
        GCodeWriter& writer
    );
    std::string draw_number(
        double startx,
        double starty,
        double value,
        CalibPressureAdvance::DrawDigitMode mode,
        double line_width,
        double layer_height,
        GCodeWriter& writer
    );

    GCode* mp_gcodegen {nullptr};
    
    Vec3d m_last_pos;

    DrawDigitMode m_draw_digit_mode {DrawDigitMode::Left_To_Right};
    const double m_digit_segment_len {2};
    const double m_digit_gap_len {1};
    const std::string::size_type m_max_number_len {5};

    double m_nozzle_diameter {-1};
    double m_line_width {-1};
    double m_height_layer {-1};
};

class CalibPressureAdvanceLine : public CalibPressureAdvance {
public:
    CalibPressureAdvanceLine(GCode* gcodegen);
    ~CalibPressureAdvanceLine() { };

    std::string generate_test(double start_pa = 0, double step_pa = 0.002, int count = 50);

    void set_speed(double fast = 100.0, double slow = 20.0) {
        m_slow_speed = slow;
        m_fast_speed = fast;
    }
    
    double& line_width() { return m_line_width; };
    bool is_delta() const;
    bool& draw_numbers() { return m_draw_numbers; }

private:
    std::string print_pa_lines(double start_x, double start_y, double start_pa, double step_pa, int num);
    
    void delta_modify_start(double& startx, double& starty, int count);

    double m_thin_line_width;
    double m_number_line_width;
    double m_length_short, m_length_long;
    double m_space_y;
    double m_slow_speed, m_fast_speed;
    bool   m_draw_numbers;
};

class CalibPressureAdvancePattern;

struct PatternSettings {
friend class CalibPressureAdvancePattern;
friend struct DrawLineOptArgs;
friend struct DrawBoxOptArgs;
private:
    PatternSettings() { };
    PatternSettings(const CalibPressureAdvancePattern& cpap);

    double anchor_line_width;
    int anchor_perimeters;
    double encroachment;
    double first_layer_height;
    int first_layer_speed;
    double layer_height;
    double line_width;
    int perim_speed;
};

struct DrawLineOptArgs {
friend class CalibPressureAdvancePattern;
private:
    DrawLineOptArgs(const PatternSettings& ps) {
        height = ps.layer_height;
        line_width = ps.line_width;
        speed = ps.perim_speed;
    };

    double height;
    double line_width;
    int speed;
    std::string comment {"Print line"};
};

struct DrawBoxOptArgs {
friend class CalibPressureAdvancePattern;
private:
    DrawBoxOptArgs(const PatternSettings& ps) {
        num_perimeters = ps.anchor_perimeters;
        height = ps.first_layer_height;
        line_width = ps.anchor_line_width;
        speed = ps.first_layer_speed;
    };

    bool is_filled {false};
    int num_perimeters;
    double height;
    double line_width;
    double speed;
};

class CalibPressureAdvancePattern : public CalibPressureAdvance {
friend struct PatternSettings;
public:
    CalibPressureAdvancePattern(
        const Calib_Params& params,
        DynamicPrintConfig& config,
        const bool& is_bbl_machine,
        const Vec3d& origin
    );

    double handle_xy_size() const { return m_handle_xy_size; };
    double height_first_layer() const { return m_height_first_layer; };
    double height_layer() const { return m_height_layer; };
    double max_layer_z() const { return m_height_first_layer + ((m_num_layers - 1) * m_height_layer); };

    void starting_point(const Model& model);

    void generate_gcodes(Model& model);
protected:
    double line_width() const { return m_nozzle_diameter * m_line_ratio / 100; };
    double line_width_anchor() const { return m_nozzle_diameter * m_anchor_layer_line_ratio / 100; };

    double speed_first_layer() const { return m_speed_first_layer; };
    double speed_perimeter() const { return m_speed_perimeter; };
    int anchor_perimeters() const { return m_anchor_perimeters; };
    double encroachment() const { return m_encroachment; };
private:
    DynamicPrintConfig pattern_config(const Model& model);
    GCodeWriter pattern_writer(const Model& model); // travel_to and extrude_to require a non-const GCodeWriter

    const int get_num_patterns() const
    {
        return std::ceil((m_params.end - m_params.start) / m_params.step + 1);
    }

    std::string draw_line(Vec2d to_pt, DrawLineOptArgs opt_args, Model& model);
    std::string draw_box(
        double min_x,
        double min_y,
        double size_x,
        double size_y,
        DrawBoxOptArgs opt_args,
        Model& model
    );

    double to_radians(double degrees) const { return degrees * M_PI / 180; };
    double get_distance(Vec2d from, Vec2d to) const;
    
    /* 
    from slic3r documentation: spacing = extrusion_width - layer_height * (1 - PI/4)
    "spacing" = center-to-center distance of adjacent extrusions, which partially overlap
        https://manual.slic3r.org/advanced/flow-math
        https://ellis3dp.com/Print-Tuning-Guide/articles/misconceptions.html#two-04mm-perimeters--08mm
    */
    double line_spacing() const { return line_width() - m_height_layer * (1 - M_PI / 4); };
    double line_spacing_anchor() const { return line_width_anchor() - m_height_first_layer * (1 - M_PI / 4); };
    double line_spacing_angle() const { return line_spacing() / std::sin(to_radians(m_corner_angle) / 2); };

    double object_size_x() const;
    double object_size_y() const;
    double frame_size_y() const { return std::sin(to_radians(double(m_corner_angle) / 2)) * m_wall_side_length * 2; };

    double glyph_start_x(int pattern_i = 0) const;
    double glyph_length_x() const;
    double glyph_tab_max_x() const;
    double max_numbering_height() const;

    double pattern_shift() const;
    double print_size_x() const { return object_size_x() + pattern_shift(); };
    double print_size_y() const { return object_size_y(); };

    const Calib_Params& m_params;
    const DynamicPrintConfig m_initial_config;
    const bool& m_is_bbl_machine;
    const Vec3d& m_origin;

    bool m_is_delta;
    Vec3d m_starting_point;
    
    PatternSettings m_pattern_settings;

    const double m_handle_xy_size {5};

    const int m_num_layers {4};
    const double m_height_first_layer {0.25};

    const double m_line_ratio {112.5};
    const int m_anchor_layer_line_ratio {140};
    const int m_anchor_perimeters {4};
    
    const double m_speed_first_layer {30};
    const double m_speed_perimeter {100};
    
    const double m_prime_zone_buffer {10};
    const int m_wall_count {3};
    const double m_wall_side_length {30.0};
    const int m_corner_angle {90};
    const int m_pattern_spacing {2};
    const double m_encroachment {1. / 3.};

    const double m_glyph_padding_horizontal {1};
    const double m_glyph_padding_vertical {1};
};
} // namespace Slic3r
