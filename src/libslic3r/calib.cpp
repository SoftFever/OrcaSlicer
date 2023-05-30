#include "calib.hpp"
#include "Point.hpp"
#include "PrintConfig.hpp"
#include "GCodeWriter.hpp"
#include "GCode.hpp"
#include <map>

namespace Slic3r {
    std::string calib_pressure_advance::move_to(Vec2d pt, std::string comment = std::string()) {
        std::stringstream gcode;

        gcode << mp_gcodegen->retract();
        if (comment.empty()) {
            gcode << mp_gcodegen->writer().travel_to_xy(pt);
        } else {
            gcode << mp_gcodegen->writer().travel_to_xy(pt, comment);
        }
        gcode << mp_gcodegen->unretract();

        return gcode.str();
    }

    std::string convert_number_to_string(double num) {
        auto sNumber = std::to_string(value);
        sNumber.erase(sNumber.find_last_not_of('0') + 1, std::string::npos);
        sNumber.erase(sNumber.find_last_not_of('.') + 1, std::string::npos);

        return sNumber;
    }

    std::string calib_pressure_advance::draw_digit(double startx, double starty, char c, calib_pressure_advance::DrawDigitMode mode) {
        auto& writer = mp_gcodegen->writer();
        std::stringstream gcode;
        const double lw = 0.48;
        Flow line_flow = Flow(lw, 0.2, m_nozzle_diameter);
        const double len = m_digit_len;
        const double gap = lw / 2.0;

        /* 
        filament diameter = 1.75
        area of a circle = PI * radius^2
        (1.75 / 2)^2 * PI = 2.40528
        */
        
        const double e = line_flow.mm3_per_mm() / 2.40528; // filament_mm/extrusion_mm
        const auto dE = e * len;
        const auto two_dE = dE * 2;

        Vec2d p0, p1, p2, p3, p4, p5;
        Vec2d p0_5, p4_5;
        Vec2d gap_p0_toward_p3, gap_p2_toward_p3;
        Vec2d dot_direction;

        if (mode == calib_pressure_advance::DrawDigitMode::Vertical) {
            //  1-------2-------5
            //  |       |       |
            //  |       |       |
            //  0-------3-------4
            p0 = Vec2d(startx, starty);
            p0_5 = Vec2d(startx, starty - len / 2);
            p1 = Vec2d(startx, starty - len);
            p2 = Vec2d(startx + len, starty - len);
            p3 = Vec2d(startx + len, starty);
            p4 = Vec2d(startx + len * 2, starty);
            p4_5 = Vec2d(startx + len * 2, starty - len / 2);
            p5 = Vec2d(startx + len * 2, starty - len);

            gap_p0_toward_p3 = p0 + Vec2d(gap, 0);
            gap_p2_toward_p3 = p2 + Vec2d(0, gap);

            dot_direction = Vec2d(-len / 2, 0);
        } else {
            //  0-------1 
            //  |       |
            //  3-------2
            //  |       |
            //  4-------5
            p0 = Vec2d(startx, starty);
            p0_5 = Vec2d(startx + len / 2, starty);
            p1 = Vec2d(startx + len, starty);
            p2 = Vec2d(startx + len, starty - len);
            p3 = Vec2d(startx, starty - len);
            p4 = Vec2d(startx, starty - len * 2);
            p4_5 = Vec2d(startx + len / 2, starty - len * 2);
            p5 = Vec2d(startx + len, starty - len * 2);

            gap_p0_toward_p3 = p0 - Vec2d(0, gap);
            gap_p2_toward_p3 = p2 - Vec2d(gap, 0);

            dot_direction = Vec2d(0, -len / 2);
        }

        switch (c) {
        case '0':
            gcode << move_to(p0);
            gcode << writer.extrude_to_xy(p1, dE);
            gcode << writer.extrude_to_xy(p5, two_dE);
            gcode << writer.extrude_to_xy(p4, dE);
            gcode << writer.extrude_to_xy(gap_p0_toward_p3, two_dE);
            break;
        case '1':
            gcode << move_to(p0_5);
            gcode << writer.extrude_to_xy(p4_5, two_dE);
            break;
        case '2':
            gcode << move_to(p0);
            gcode << writer.extrude_to_xy(p1, dE);
            gcode << writer.extrude_to_xy(p2, dE);
            gcode << writer.extrude_to_xy(p3, dE);
            gcode << writer.extrude_to_xy(p4, dE);
            gcode << writer.extrude_to_xy(p5, dE);
            break;
        case '3':
            gcode << move_to(p0);
            gcode << writer.extrude_to_xy(p1, dE);
            gcode << writer.extrude_to_xy(p5, two_dE);
            gcode << writer.extrude_to_xy(p4, dE);
            gcode << move_to(gap_p2_toward_p3);
            gcode << writer.extrude_to_xy(p3, dE);
            break;
        case '4':
            gcode << move_to(p0);
            gcode << writer.extrude_to_xy(p3, dE);
            gcode << writer.extrude_to_xy(p2, dE);
            gcode << move_to(p1);
            gcode << writer.extrude_to_xy(p5, two_dE);
            break;
        case '5':
            gcode << move_to(p1);
            gcode << writer.extrude_to_xy(p0, dE);
            gcode << writer.extrude_to_xy(p3, dE);
            gcode << writer.extrude_to_xy(p2, dE);
            gcode << writer.extrude_to_xy(p5, dE);
            gcode << writer.extrude_to_xy(p4, dE);
            break;
        case '6':
            gcode << move_to(p1);
            gcode << writer.extrude_to_xy(p0, dE);
            gcode << writer.extrude_to_xy(p4, two_dE);
            gcode << writer.extrude_to_xy(p5, dE);
            gcode << writer.extrude_to_xy(p2, dE);
            gcode << writer.extrude_to_xy(p3, dE);
            break;
        case '7':
            gcode << move_to(p0);
            gcode << writer.extrude_to_xy(p1, dE);
            gcode << writer.extrude_to_xy(p5, two_dE);
            break;
        case '8':
            gcode << move_to(p2);
            gcode << writer.extrude_to_xy(p3, dE);
            gcode << writer.extrude_to_xy(p4, dE);
            gcode << writer.extrude_to_xy(p5, dE);
            gcode << writer.extrude_to_xy(p1, two_dE);
            gcode << writer.extrude_to_xy(p0, dE);
            gcode << writer.extrude_to_xy(p3, dE);
            break;
        case '9':
            gcode << move_to(p5);
            gcode << writer.extrude_to_xy(p1, two_dE);
            gcode << writer.extrude_to_xy(p0, dE);
            gcode << writer.extrude_to_xy(p3, dE);
            gcode << writer.extrude_to_xy(p2, dE);
            break;
        case '.':
            gcode << move_to(p4_5);
            gcode << writer.extrude_to_xy(p4_5 + dot_direction, dE);
            break;
        default:
            break;
    }

        return gcode.str();
    }

    std::string calib_pressure_advance::draw_number(double startx, double starty, double value, calib_pressure_advance::DrawDigitMode mode) {
        auto sNumber = convert_number_to_string(value);
        std::stringstream gcode;
        gcode << mp_gcodegen->writer().set_speed(3600);

        for (int i = 0; i < sNumber.length(); ++i) {
            if (i > m_max_number_length)
                break;
            gcode << draw_digit(startx + i * m_number_spacing, starty, sNumber[i], mode);
        }

        return gcode.str();
    }

    bool calib_pressure_advance::is_delta() {
        return mp_gcodegen->config().printable_area.values.size() > 4;
    }

    void calib_pressure_advance::delta_scale_bed_ext(BoundingBoxf& bed_ext) {
        bed_ext.scale(1.0f / 1.41421f);
    }

    void calib_pressure_advance::delta_modify_start(double& start_x, double& start_y, int count) {
        startx = -startx;
        starty = -(count * m_space_y) / 2;
    }

    calib_pressure_advance_line::calib_pressure_advance_line(GCode* gcodegen) :mp_gcodegen(gcodegen), m_length_short(20.0), m_length_long(40.0), m_space_y(3.5), m_line_width(0.6), m_draw_numbers(true) {}

    std::string calib_pressure_advance_line::generate_test(double start_pa /*= 0*/, double step_pa /*= 0.002*/,
                                                      int count /*= 10*/) {
      BoundingBoxf bed_ext = get_extents(mp_gcodegen->config().printable_area.values);
      const bool is_delta = is_delta();
      if (is_delta) {
        delta_scale_bed_ext(bed_ext);
      }

      auto bed_sizes = mp_gcodegen->config().printable_area.values;
      const auto &w = bed_ext.size().x();
      const auto &h = bed_ext.size().y();
      count = std::min(count, int((h - 10) / m_space_y));

      m_length_long = 40 + std::min(w - 120.0, 0.0);

      auto startx = (w - m_length_short * 2 - m_length_long - 20) / 2;
      auto starty = (h - count * m_space_y) / 2;
      if (is_delta) {
        delta_modify_start(startx, starty, count);
      }

      return print_pa_lines(startx, starty, start_pa, step_pa, count);
    }

    std::string calib_pressure_advance_line::print_pa_lines(double start_x, double start_y, double start_pa, double step_pa, int num) {
        auto& writer = mp_gcodegen->writer();
        Flow line_flow = Flow(m_line_width, 0.2, m_nozzle_diameter);
        Flow thin_line_flow = Flow(0.44, 0.2, m_nozzle_diameter);
        const double e_calib = line_flow.mm3_per_mm() / 2.40528; // filament_mm/extrusion_mm
        const double e = thin_line_flow.mm3_per_mm() / 2.40528; // filament_mm/extrusion_mm


        const double fast = m_fast_speed * 60.0;
        const double slow = m_slow_speed * 60.0;
        std::stringstream gcode;
        gcode << mp_gcodegen->writer().travel_to_z(0.2);
        double y_pos = start_y;

        // prime line
        auto prime_x = start_x - 2;
        gcode << move_to(Vec2d(prime_x, y_pos + (num - 4) * m_space_y));
        gcode << writer.set_speed(slow);
        gcode << writer.extrude_to_xy(Vec2d(prime_x, y_pos + 3 * m_space_y), e_calib * m_space_y * num * 1.1);

        for (int i = 0; i < num; ++i) {

            gcode << writer.set_pressure_advance(start_pa + i * step_pa);
            gcode << move_to(Vec2d(start_x, y_pos + i * m_space_y));
            gcode << writer.set_speed(slow);
            gcode << writer.extrude_to_xy(Vec2d(start_x + m_length_short, y_pos + i * m_space_y), e_calib * m_length_short);
            gcode << writer.set_speed(fast);
            gcode << writer.extrude_to_xy(Vec2d(start_x + m_length_short + m_length_long, y_pos + i * m_space_y), e_calib * m_length_long);
            gcode << writer.set_speed(slow);
            gcode << writer.extrude_to_xy(Vec2d(start_x + m_length_short + m_length_long + m_length_short, y_pos + i * m_space_y), e_calib * m_length_short);

        }
        gcode << writer.set_pressure_advance(0.0);

        if (m_draw_numbers) {
            // draw indicator lines
            gcode << writer.set_speed(fast);
            gcode << move_to(Vec2d(start_x + m_length_short, y_pos + (num - 1) * m_space_y + 2));
            gcode << writer.extrude_to_xy(Vec2d(start_x + m_length_short, y_pos + (num - 1) * m_space_y + 7), e * 7);
            gcode << move_to(Vec2d(start_x + m_length_short + m_length_long, y_pos + (num - 1) * m_space_y + 7));
            gcode << writer.extrude_to_xy(Vec2d(start_x + m_length_short + m_length_long, y_pos + (num - 1) * m_space_y + 2), e * 7);

            for (int i = 0; i < num; i += 2) {
                gcode << draw_number(start_x + m_length_short + m_length_long + m_length_short + 3, y_pos + i * m_space_y + m_space_y / 2, start_pa + i * step_pa);
            }
        }
        return gcode.str();
    }

    calib_pressure_advance_pattern::calib_pressure_advance_pattern(GCode* gcodegen) : mp_gcodegen(gcodegen) { }

    std::string calib_pressure_advance_pattern::generate_test(double start_pa, double end_pa, double step_pa) {
        BoundingBoxf bed_ext = get_extents(mp_gcodegen->config().printable_area.values);
        
        bool is_delta = is_delta();
        if (is_delta) {
            delta_scale_bed_ext(bed_ext);
        }

        const auto &w = bed_ext.size().x();
        const auto &h = bed_ext.size().y();
        const auto center_x = w / 2;
        const auto center_y = h / 2;

        int num_patterns = std::ceil((end_pa - start_pa) / step_pa + 1);
        num_patterns = std::min(num_patterns, int((h - 10) / m_space_y));

        auto object_size_x = object_size_x(num_patterns);
        auto object_size_y = object_size_y(start_pa, step_pa, num_patterns);

        auto glyph_start_x = 
            center_x -
            object_size_x / 2 +
            (((m_wall_count - 1) / 2) * line_spacing_angle() - 2)
        ;

        auto pattern_shift =
            center_x -
            object_size_x / 2 -
            glyph_start_x +
            m_glyph_padding_horizontal
        ;
        if (pattern_shift > 0) {
            pattern_shift += (line_width_anchor() / 2);
        } else {
            pattern_shift = 0;
        }

        auto start_x = center_x - (object_size_x + pattern_shift) / 2;
        auto start_y = center_y - object_size_y / 2;

        if (is_delta) {
            delta_modify_start(start_x, start_y, num_patterns);
        }

        return print_pa_pattern(start_x, start_y, start_pa, step_pa, num_patterns);
    }

    std::string calib_pressure_advance_pattern::print_pa_pattern(double start_x, double start_y, double start_pa, double step_pa, int num) {
        auto& writer = mp_gcodegen->writer();
        // TODO set test parameters

        std::stringstream gcode;

        gcode << move_to(Vec2d(start_x, start_y, mp_gcodegen->config().initial_layer_print_height.value));
    }

    double object_size_x(int num_patterns) {
        return num_patterns * ((m_wall_count - 1) * line_spacing_angle()) +
            (num_patterns - 1) * (m_pattern_spacing + line_width()) +
            std::cos(to_radians(m_corner_angle) / 2) * m_wall_side_length +
            line_spacing_anchor() * m_anchor_perimeters
        ;
    }

    double object_size_y(double start_pa, double step_pa, int num_patterns) {
        return 2 * std::sin(to_radians(m_corner_angle) / 2) * m_wall_side_length +
            max_numbering_height(start_pa, step_pa, num_patterns) +
            m_glyph_padding_vertical * 2 +
            line_width_anchor()
    }

    double max_numbering_height(double start_pa, double step_pa, int count) {
        int max_length = 0;

        for (int i = 0; i < count; i += 2) {
            std::string sNumber = convert_number_to_string(start_pa + (i * step_pa));

            if (sNumber.length > max_length) { max_length = sNumber.length; }
        }

        max_length = std::min(max_characters, m_max_number_length);

        return (max_length * m_digit_len) + ((max_length - 1) * m_number_spacing);
    }

    double get_distance(double cur_x, double cur_y, double to_x, double to_y) {
        return std::hypot((to_x - cur_x), (to_y - cur_y));
    }

    std::string draw_line(double to_x, double to_y, double line_width, double layer_height, std::string comment = std::string()) {
        std::stringstream gcode;
        auto& config = mp_gcodegen.config();
        auto& writer = mp_gcodegen.writer();

        Flow line_flow = Flow(line_width, layer_height, m_nozzle_diameter);
        const double filament_area = M_PI * std::pow(config.filament_diameter.value / 2, 2);
        const double e_per_mm = line_flow.mm3_per_mm() / filament_area * config.print_flow_ratio;

        Point last_pos = mp_gcodegen.last_pos();
        const double length = get_distance(last_pos.x(), last_pos.y(), to_x, to_y);
        
        auto dE = e_per_mm * length;

        if (comment.empty()) {
            gcode << writer.extrude_to_xy(Vec2d(to_x, to_y), dE);
        } else {
            gcode << writer.extrude_to_xy(Vec2d(to_x, to_y), dE, comment);
        }

        return gcode.str();
    }

    Calib_Params::Calib_Params() : mode(CalibMode::Calib_None) {}
} // namespace Slic3r
