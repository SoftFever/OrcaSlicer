#pragma once
#define calib_pressure_advance_dd

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

class CalibPressureAdvance {
protected:
    CalibPressureAdvance(GCode* gcodegen) :
        mp_gcodegen(gcodegen),
        m_digit_segment_len(2),
        m_max_number_length(5),
        m_number_spacing(3.0)
        { 
            m_nozzle_diameter = gcodegen->config().nozzle_diameter.get_at(0);
        }
    ;
    ~CalibPressureAdvance() { };

    enum class DrawDigitMode {
        Horizontal = 0,
        Vertical
    };

    std::string move_to(Vec2d pt, std::string comment = std::string());
    
    std::string convert_number_to_string(double num);
    std::string draw_digit(double startx, double starty, char c, CalibPressureAdvance::DrawDigitMode mode);
    std::string draw_number(double startx, double starty, double value, CalibPressureAdvance::DrawDigitMode mode);
    
    bool is_delta();
    void delta_scale_bed_ext(BoundingBoxf& bed_ext);
    void delta_modify_start(double start_x, double start_y, int count);

    GCode* mp_gcodegen;
    double m_nozzle_diameter;
    double m_digit_segment_len;
    int m_max_number_length;
    double m_number_spacing;
};

class CalibPressureAdvanceLine : public CalibPressureAdvance {
public:
    CalibPressureAdvanceLine(GCode* gcodegen) :
        CalibPressureAdvance(gcodegen),
        m_length_short(20.0),
        m_length_long(40.0),
        m_space_y(3.5),
        m_line_width(0.6),
        m_draw_numbers(true)
        { }
    ;
    ~CalibPressureAdvanceLine() { };

    std::string generate_test(double start_pa = 0, double step_pa = 0.002, int count = 50);
    
    void set_speed(double fast = 100.0, double slow = 20.0) {
        m_slow_speed = slow;
        m_fast_speed = fast;
    }
    
    double& line_width() { return m_line_width; };
    bool& draw_numbers() { return m_draw_numbers; }

private:
    std::string print_pa_lines(double start_x, double start_y, double start_pa, double step_pa, int num);

    double m_length_short, m_length_long;
    double m_space_y;
    double m_slow_speed, m_fast_speed;
    double m_line_width;
    bool   m_draw_numbers;
};

class CalibPressureAdvancePattern : public CalibPressureAdvance {
public:
    CalibPressureAdvancePattern(GCode* gcodegen) :
        CalibPressureAdvance(gcodegen),

        m_line_ratio(112.5),
        m_extrusion_multiplier(0.98),
        m_height_layer(0.2),
        m_height_first_layer(0.25),

        m_anchor_perimeters(4),
        m_anchor_layer_line_ratio(140),

        m_wall_count(3),
        m_wall_side_length(30.0),

        m_corner_angle(90),
        m_pattern_spacing(2),
        
        m_glyph_padding_horizontal(1),
        m_glyph_padding_vertical(1)
        { }
    ;
    ~CalibPressureAdvancePattern() { };

    std::string generate_test(
        double start_pa = 0,
        double end_pa = 0.08,
        double step_pa = 0.005
    );

private:
    double to_radians(double degrees) { return degrees * M_PI / 180; }
    double get_distance(double cur_x, double cur_y, double to_x, double to_y);
    
    double line_width() { return m_nozzle_diameter * m_line_ratio / 100; };
    double line_width_anchor() { return m_nozzle_diameter * m_anchor_layer_line_ratio / 100; };
    
    // from slic3r documentation: spacing = extrusion_width - layer_height * (1 - PI/4)
    double line_spacing() { return line_width() - m_height_layer * (1 - M_PI / 4); };
    double line_spacing_anchor() { return line_width_anchor() - m_height_first_layer * (1 - M_PI / 4); };
    double line_spacing_angle() { return line_spacing() / std::sin(to_radians(m_corner_angle) / 2); };

    double max_numbering_height(double start_pa, double step_pa, int num_patterns);

    double object_size_x(int num_patterns);
    double object_size_y(double start_pa, double step_pa, int num_patterns);

    std::string draw_line(double to_x, double to_y, std::string comment = std::string());
    std::string draw_box(double min_x, double min_y, double size_x, double size_y);

    std::string print_pa_pattern(double start_x, double start_y, double start_pa, double step_pa, int num_patterns);

    double m_line_ratio;
    double m_extrusion_multiplier;
    double m_height_layer;
    double m_height_first_layer;
    
    int m_anchor_perimeters;
    int m_anchor_layer_line_ratio;
    
    int m_wall_count;
    double m_wall_side_length;
    
    int m_corner_angle;
    int m_pattern_spacing;

    double m_glyph_padding_horizontal;
    double m_glyph_padding_vertical;
};
} // namespace Slic3r
