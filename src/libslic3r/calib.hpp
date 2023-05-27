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

class calib_pressure_advance {
private:
    std::string move_to(Ved3d pt);
    std::string move_to(Vec2d pt);
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
    std::string draw_digit(double startx, double starty, char c);
    std::string draw_number(double startx, double starty, double value);
private:
    GCode* mp_gcodegen;
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

        double to_radians(degrees) { return degrees * (M_PI / 180); }
        
        double line_width() { return mp_gcodegen->config().nozzle_diameter.get_at(0) * m_line_ratio / 100; };
        double line_width_anchor() { return mp_gcodegen->config().nozzle_diameter.get_at(0) * m_anchor_layer_line_ratio / 100; };
        
        // from slic3r documentation: spacing = extrusion_width - layer_height * (1 - PI/4)
        double line_spacing() { return line_width() - mp_gcodegen->config().layer_height.value * (1 - M_PI / 4); };
        double line_spacing_anchor() { return line_width_anchor() - mp_gcodegen->config().initial_layer_print_height.value * (1 - M_PI / 4); };
        double line_spacing_angle() { return line_spacing() / sin(to_radians(m_corner_angle) / 2); };
    private:
        std::string move_to(Vec2d pt);
    private:
        Gcode* mp_gcodegen;
        int m_anchor_layer_line_ratio;
        double m_line_ratio;
        int m_pattern_spacing;
        int m_wall_count;
        double m_wall_side_length;
};
} // namespace Slic3r
