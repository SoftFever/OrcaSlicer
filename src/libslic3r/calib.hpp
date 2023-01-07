#pragma once

#include <string>
#include "Point.hpp"
namespace Slic3r {

class GCode;
class calib_pressure_advance
{
public:
    calib_pressure_advance(GCode* gcodegen);
    ~calib_pressure_advance() {}

    std::string generate_test(double start_pa = 0, double step_pa = 0.002, int count = 50);
    void set_speed(double fast = 100.0,double slow = 20.0) {
        m_slow_speed = slow;
        m_fast_speed = fast;
    }
    double& line_width() { return m_line_width; };

private:
    std::string move_to(Vec2d pt);
    std::string print_pa_lines(double start_x, double start_y, double start_pa, double step_pa, int num);
    std::string draw_digit(double startx, double starty, char c);
    std::string draw_number(double startx, double starty, double value);
private:
    GCode* mp_gcodegen;
    double m_length_short, m_length_long;
    double m_space_y;
    double m_slow_speed, m_fast_speed;
    double m_line_width;
};
} // namespace Slic3r