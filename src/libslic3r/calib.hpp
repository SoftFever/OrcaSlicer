#pragma once
#define calib_pressure_advance_dd

#include <math.h>
#include <string>
#include "Point.hpp"
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

struct Calib_Params
{
    Calib_Params();
    double start, end, step;
    bool print_numbers;
    CalibMode mode;
};

class calib_pressure_advance
{
public:
    enum class DrawDigitMode {
        Horizontal = 0,
        Vertical
    };
private:
    std::string move_to(Ved3d pt);
    std::string move_to(Vec2d pt);
    std::string convert_number_to_string(double num);
    std::string draw_digit(double startx, double starty, char c, calib_pressure_advance::DrawDigitMode mode);
    std::string draw_number(double startx, double starty, double value, calib_pressure_advance::DrawDigitMode mode);
    bool is_delta();
    void delta_scale_bed_ext(BoundingBoxf& bed_ext);
    void delta_modify_start(double start_x, double start_y, int count);
private:
    GCode* mp_gcodegen;
    double m_digit_len {2};
    int m_max_number_length {5};
    double m_number_spacing {3.0};
}

class calib_pressure_advance_line: public calib_pressure_advance
{
public:
    calib_pressure_advance_line(GCode* gcodegen);
    ~calib_pressure_advance_line() {}

    std::string generate_test(double start_pa = 0, double step_pa = 0.002, int count = 50);
    void set_speed(double fast = 100.0,double slow = 20.0) {
        m_slow_speed = slow;
        m_fast_speed = fast;
    }
    double& line_width() { return m_line_width; };
    bool&    draw_numbers() { return m_draw_numbers; }

private:
    std::string print_pa_lines(double start_x, double start_y, double start_pa, double step_pa, int num);
private:
    double m_length_short, m_length_long;
    double m_space_y;
    double m_slow_speed, m_fast_speed;
    double m_line_width;
    bool   m_draw_numbers;
};

class calib_pressure_advance_pattern: public calib_pressure_advance
{
    public:
        calib_pressure_advance_pattern(GCode* gcodegen);
        ~calib_pressure_advance_pattern() {}

        std::string generate_test(double start_pa = 0, double end_pa = 0.08, double step_pa = 0.005);

        double to_radians(double degrees) { return degrees * (M_PI / 180); }
        
        double line_width() { return mp_gcodegen->config().nozzle_diameter.get_at(0) * m_line_ratio / 100; };
        double line_width_anchor() { return mp_gcodegen->config().nozzle_diameter.get_at(0) * m_anchor_layer_line_ratio / 100; };
        
        // from slic3r documentation: spacing = extrusion_width - layer_height * (1 - PI/4)
        double line_spacing() { return line_width() - mp_gcodegen->config().layer_height.value * (1 - M_PI / 4); };
        double line_spacing_anchor() { return line_width_anchor() - mp_gcodegen->config().initial_layer_print_height.value * (1 - M_PI / 4); };
        double line_spacing_angle() { return line_spacing() / sin(to_radians(m_corner_angle) / 2); };

        double object_size_x(int num_patterns);
        double object_size_y(double start_pa, double step_pa, int num_patterns);

        double max_numbering_height();
        double pattern_shift();
    private:
        int m_anchor_layer_line_ratio {140};
        int m_anchor_perimeters {4};
        int m_corner_angle {90};
        double m_glyph_padding_horizontal {1};
        double m_glyph_padding_vertical {1};
        double m_line_ratio {112.5};
        int m_pattern_spacing {2};
        int m_wall_count {3};
        double m_wall_side_length {30.0};
};
} // namespace Slic3r
