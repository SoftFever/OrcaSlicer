#include "calib.hpp"
#include "Point.hpp"
#include "PrintConfig.hpp"
#include "GCodeWriter.hpp"
#include "GCode.hpp"
#include <map>

namespace Slic3r {

    calib_pressure_advance::calib_pressure_advance(GCode* gcodegen) :mp_gcodegen(gcodegen), m_length_short(20.0), m_length_long(40.0) {}

    std::string calib_pressure_advance::generate_test(double start_pa/*= 0*/, double step_pa /*= 0.005*/, int count/*= 10*/) {
        auto bed_sizes = mp_gcodegen->config().printable_area.values;
        auto w = bed_sizes[2].x() - bed_sizes[0].x();
        auto h = bed_sizes[2].y() - bed_sizes[0].y();
        auto startx = (w - 100) / 2;
        auto starty = (h - count * 5) / 2;
        return print_pa_lines(startx, starty, start_pa, step_pa, count);
    }

    std::string calib_pressure_advance::move_to(Vec2d pt) {
        std::stringstream gcode;
        gcode << mp_gcodegen->retract();
        gcode << mp_gcodegen->writer().travel_to_xy(pt);
        gcode << mp_gcodegen->unretract();
        return gcode.str();
    }

    std::string calib_pressure_advance::print_pa_lines(double start_x, double start_y, double start_pa, double step_pa, int num) {

        auto& writer = mp_gcodegen->writer();
        const double e = 0.04; // filament_mm/extrusion_mm
        const double y_offset = 5;

        const double fast = mp_gcodegen->config().get_abs_value("outer_wall_speed") * 60.0;
        const double slow = std::max(1200.0, fast * 0.1);
        std::stringstream gcode;
        gcode << mp_gcodegen->writer().travel_to_z(0.2);
        double y_pos = start_y;

        for (int i = 0; i < num; ++i) {

            gcode << writer.set_pressure_advance(start_pa + i * step_pa);
            gcode << move_to(Vec2d(start_x, y_pos + i * y_offset));
            gcode << writer.set_speed(slow);
            gcode << writer.extrude_to_xy(Vec2d(start_x + m_length_short, y_pos + i * y_offset), e * m_length_short);
            gcode << writer.set_speed(fast);
            gcode << writer.extrude_to_xy(Vec2d(start_x + m_length_short + m_length_long, y_pos + i * y_offset), e * m_length_long);
            gcode << writer.set_speed(slow);
            gcode << writer.extrude_to_xy(Vec2d(start_x + m_length_short + m_length_long + m_length_short, y_pos + i * y_offset), e * m_length_short);

        }
        // draw indicator lines
        gcode << writer.set_speed(fast);
        gcode << move_to(Vec2d(start_x + m_length_short, y_pos + (num - 1) * y_offset + 2));
        gcode << writer.extrude_to_xy(Vec2d(start_x + m_length_short, y_pos + (num - 1) * y_offset + 7), e * 7);
        gcode << move_to(Vec2d(start_x + m_length_short + m_length_long, y_pos + (num - 1) * y_offset + 7));
        gcode << writer.extrude_to_xy(Vec2d(start_x + m_length_short + m_length_long, y_pos + (num - 1) * y_offset + 2), e * 7);


        for (int i = 0; i < num; i += 2) {
            gcode << draw_number(start_x + m_length_short + m_length_long + m_length_short + 3, y_pos + i * y_offset + y_offset / 2, start_pa + i * step_pa);

        }
        return gcode.str();
    }


    std::string calib_pressure_advance::draw_digit(double startx, double starty, char c) {
        auto& writer = mp_gcodegen->writer();
        std::stringstream gcode;
        const double lw = 0.48;
        const double len = 2;
        const double gap = lw / 2.0;
        const double e = 0.04; // filament_mm/extrusion_mm

        //  0-------1 
        //  |       |
        //  3-------2
        //  |       |
        //  4-------5
        const Vec2d p0(startx, starty);
        const Vec2d p1(startx + len, starty);
        const Vec2d p2(startx + len, starty - len);
        const Vec2d p3(startx, starty - len);
        const Vec2d p4(startx, starty - len * 2);
        const Vec2d p5(startx + len, starty - len * 2);

        switch (c)
        {
        case '0':
            gcode << move_to(p0);
            gcode << writer.extrude_to_xy(p1, e * len);
            gcode << writer.extrude_to_xy(p5, e * len * 2);
            gcode << writer.extrude_to_xy(p4, e * len);
            gcode << writer.extrude_to_xy(p0 - Vec2d(0, gap), e * len * 2);
            break;
        case '1':
            gcode << move_to(p0 + Vec2d(len / 2, 0));
            gcode << writer.extrude_to_xy(p4 + Vec2d(len / 2, 0), e * len * 2);
            break;
        case '2':
            gcode << move_to(p0);
            gcode << writer.extrude_to_xy(p1, e * len);
            gcode << writer.extrude_to_xy(p2, e * len);
            gcode << writer.extrude_to_xy(p3, e * len);
            gcode << writer.extrude_to_xy(p4, e * len);
            gcode << writer.extrude_to_xy(p5, e * len);
            break;
        case '3':
            gcode << move_to(p0);
            gcode << writer.extrude_to_xy(p1, e * len);
            gcode << writer.extrude_to_xy(p5, e * len * 2);
            gcode << writer.extrude_to_xy(p4, e * len);
            gcode << move_to(p2 - Vec2d(gap, 0));
            gcode << writer.extrude_to_xy(p3, e * len);
            break;
        case '4':
            gcode << move_to(p0);
            gcode << writer.extrude_to_xy(p3, e * len);
            gcode << writer.extrude_to_xy(p2, e * len);
            gcode << move_to(p1);
            gcode << writer.extrude_to_xy(p5, e * len * 2);
            break;
        case '5':
            gcode << move_to(p1);
            gcode << writer.extrude_to_xy(p0, e * len);
            gcode << writer.extrude_to_xy(p3, e * len);
            gcode << writer.extrude_to_xy(p2, e * len);
            gcode << writer.extrude_to_xy(p5, e * len);
            gcode << writer.extrude_to_xy(p4, e * len);
            break;
        case '6':
            gcode << move_to(p1);
            gcode << writer.extrude_to_xy(p0, e * len);
            gcode << writer.extrude_to_xy(p4, e * len * 2);
            gcode << writer.extrude_to_xy(p5, e * len);
            gcode << writer.extrude_to_xy(p2, e * len);
            gcode << writer.extrude_to_xy(p3, e * len);
            break;
        case '7':
            gcode << move_to(p0);
            gcode << writer.extrude_to_xy(p1, e * len);
            gcode << writer.extrude_to_xy(p5, e * len * 2);
            break;
        case '8':
            gcode << move_to(p2);
            gcode << writer.extrude_to_xy(p3, e * len);
            gcode << writer.extrude_to_xy(p4, e * len);
            gcode << writer.extrude_to_xy(p5, e * len);
            gcode << writer.extrude_to_xy(p1, e * len * 2);
            gcode << writer.extrude_to_xy(p0, e * len);
            gcode << writer.extrude_to_xy(p3, e * len);
            break;
        case '9':
            gcode << move_to(p5);
            gcode << writer.extrude_to_xy(p1, e * len * 2);
            gcode << writer.extrude_to_xy(p0, e * len);
            gcode << writer.extrude_to_xy(p3, e * len);
            gcode << writer.extrude_to_xy(p2, e * len);
            break;
        case '.':
            gcode << move_to(p4 + Vec2d(len / 2, 0));
            gcode << writer.extrude_to_xy(p4 + Vec2d(len / 2, len / 2), e * len);
            break;
        default:
            break;
        }

        return gcode.str();
    }
    // draw number
    std::string calib_pressure_advance::draw_number(double startx, double starty, double value) {
        double spacing = 3.0;
        auto sNumber = std::to_string(value);
        sNumber.erase(sNumber.find_last_not_of('0') + 1, std::string::npos);
        sNumber.erase(sNumber.find_last_not_of('.') + 1, std::string::npos);
        std::stringstream gcode;
        gcode << mp_gcodegen->writer().set_speed(3600);


        for (int i = 0; i < sNumber.length(); ++i) {
            if (i > 5)
                break;
            gcode << draw_digit(startx + i * spacing, starty, sNumber[i]);

        }

        return gcode.str();
    }
} // namespace Slic3r