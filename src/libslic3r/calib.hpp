#pragma once
#define calib_pressure_advance_dd

#include <string>
#include "CustomGCode.hpp"
#include "GCodeWriter.hpp"
#include "Point.hpp"
#include "PrintConfig.hpp"
namespace Slic3r {

class GCode;

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

    bool        is_delta() const;
    void        delta_scale_bed_ext(BoundingBoxf& bed_ext) { bed_ext.scale(1.0f / 1.41421f); }

    std::string move_to(Vec2d pt, std::string comment = std::string());
    double      e_per_mm(
        double line_width,
        double layer_height,
        float filament_diameter,
        float print_flow_ratio
    );
    double      speed_adjust(int speed) const { return speed * 60; };
    
    std::string convert_number_to_string(double num);
    double      number_spacing() { return m_digit_segment_len + m_digit_gap_len; };
    std::string draw_digit(
        double startx,
        double starty,
        char c,
        CalibPressureAdvance::DrawDigitMode mode,
        double line_width,
        double e_per_mm
    );
    std::string draw_number(
        double startx,
        double starty,
        double value,
        CalibPressureAdvance::DrawDigitMode mode,
        double line_width,
        double layer_height
    );

    GCode* mp_gcodegen {nullptr};

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
    bool&   draw_numbers() { return m_draw_numbers; }

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
    PatternSettings(const CalibPressureAdvancePattern* cpap);

    PatternSettings& operator= (const PatternSettings& rhs) =default;

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

// the bare minimum needed to plate this calibration test
class CalibPressureAdvancePatternPlate : public CalibPressureAdvance {
public:
    CalibPressureAdvancePatternPlate(const Calib_Params& params) :
        CalibPressureAdvance(),
        m_start_pa(params.start),
        m_end_pa(params.end),
        m_step_pa(params.step)
    {
        this->m_height_layer = 0.2;
    };
    CalibPressureAdvancePatternPlate(
        const Calib_Params& params,
        GCode* gcodegen
    ) :
        CalibPressureAdvance(gcodegen),
        m_start_pa(params.start),
        m_end_pa(params.end),
        m_step_pa(params.step)
    {
        this->m_height_layer = 0.2;
    };

    double height_first_layer() const { return m_height_first_layer; };
    double height_layer() const { return m_height_layer; };
    double max_layer_z() { return m_height_first_layer + ((m_num_layers - 1) * m_height_layer); };

    double handle_xy_size() { return m_handle_xy_size; };
protected:
    const double m_start_pa;
    const double m_end_pa;
    const double m_step_pa;

    const double m_handle_xy_size {5};

    const int m_num_layers {4};
    const double m_height_first_layer {0.25};
};

/* Remaining definition. Separated because it requires fully setup GCode object, 
which is not available at the time of plating */
class CalibPressureAdvancePattern : public CalibPressureAdvancePatternPlate {
friend struct PatternSettings;
public:
    CalibPressureAdvancePattern(
        const Calib_Params& params,
        GCode* gcodegen,
        const Vec2d starting_point
    ) :
        CalibPressureAdvancePatternPlate(params, gcodegen),
        m_starting_point(starting_point)
    {
        this->m_draw_digit_mode = DrawDigitMode::Bottom_To_Top;
        this->m_line_width = line_width();

        this->m_pattern_settings = PatternSettings(this);
    };
    ~CalibPressureAdvancePattern() { };

    CustomGCode::Info generate_gcodes();
protected:
    double line_width() const { return m_nozzle_diameter * m_line_ratio / 100; };
    double line_width_anchor() const { return m_nozzle_diameter * m_anchor_layer_line_ratio / 100; };

    double speed_first_layer() const { return m_speed_first_layer; };
    double speed_perimeter() const { return m_speed_perimeter; };
    int anchor_perimeters() const { return m_anchor_perimeters; };
    double encroachment() const { return m_encroachment; };
private:
    int get_num_patterns() const
    {
        return std::ceil((m_end_pa - m_start_pa) / m_step_pa + 1);
    }

    std::string draw_line(double to_x, double to_y, DrawLineOptArgs opt_args);
    std::string draw_box(double min_x, double min_y, double size_x, double size_y, DrawBoxOptArgs opt_args);

    double to_radians(double degrees) const { return degrees * M_PI / 180; };
    double get_distance(double cur_x, double cur_y, double to_x, double to_y);
    
    // from slic3r documentation: spacing = extrusion_width - layer_height * (1 - PI/4)
    double line_spacing() { return line_width() - m_height_layer * (1 - M_PI / 4); };
    double line_spacing_anchor() { return line_width_anchor() - m_height_first_layer * (1 - M_PI / 4); };
    double line_spacing_angle() { return line_spacing() / std::sin(to_radians(m_corner_angle) / 2); };

    Point bed_center();
    double object_size_x();
    double object_size_y();
    double frame_size_y() { return std::sin(to_radians(double(m_corner_angle) / 2)) * m_wall_side_length * 2; };

    double glyph_start_x();
    double glyph_end_x();
    double glyph_tab_max_x();
    double max_numbering_height();

    double pattern_shift();
    double print_size_x() { return object_size_x() + pattern_shift(); };
    double print_size_y() { return object_size_y(); };

    double pattern_start_x();
    double pattern_start_y();

    PatternSettings m_pattern_settings;
    const Vec2d m_starting_point;

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
