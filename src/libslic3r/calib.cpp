#include "calib.hpp"
#include "Point.hpp"
#include "PrintConfig.hpp"
#include "GCodeWriter.hpp"
#include "GCode.hpp"
#include <map>

namespace Slic3r {
Calib_Params::Calib_Params() : mode(CalibMode::Calib_None) { }

CalibPressureAdvance::CalibPressureAdvance(GCode* gcodegen) :
    mp_gcodegen(gcodegen),
    m_digit_segment_len(2),
    m_max_number_length(5),
    m_number_spacing(3.0)
    { 
        m_nozzle_diameter = gcodegen->config().nozzle_diameter.get_at(0);
    }
;

std::string CalibPressureAdvance::move_to(Vec2d pt, std::string comment = std::string())
{
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

std::string CalibPressureAdvance::convert_number_to_string(double num)
{
    auto sNumber = std::to_string(num);
    sNumber.erase(sNumber.find_last_not_of('0') + 1, std::string::npos);
    sNumber.erase(sNumber.find_last_not_of('.') + 1, std::string::npos);

    return sNumber;
}

std::string CalibPressureAdvance::draw_digit(double startx, double starty, char c, CalibPressureAdvance::DrawDigitMode mode)
{
    auto& writer = mp_gcodegen->writer();
    std::stringstream gcode;
    const double lw = 0.48;
    Flow line_flow = Flow(lw, 0.2, m_nozzle_diameter);
    const double len = m_digit_segment_len;
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

    if (mode == CalibPressureAdvance::DrawDigitMode::Vertical) {
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

std::string CalibPressureAdvance::draw_number(double startx, double starty, double value, calib_pressure_advance::DrawDigitMode mode)
{
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

bool CalibPressureAdvance::is_delta()
{
    return mp_gcodegen->config().printable_area.values.size() > 4;
}

void CalibPressureAdvance::delta_scale_bed_ext(BoundingBoxf& bed_ext)
{
    bed_ext.scale(1.0f / 1.41421f);
}

void CalibPressureAdvance::delta_modify_start(double& start_x, double& start_y, int count)
{
    startx = -startx;
    starty = -(count * m_space_y) / 2; // TODO fix for pattern
}

std::string CalibPressureAdvanceLine::generate_test(double start_pa /*= 0*/, double step_pa /*= 0.002*/, int count /*= 10*/)
{
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

std::string CalibPressureAdvanceLine::print_pa_lines(double start_x, double start_y, double start_pa, double step_pa, int num)
{
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

std::string CalibPressureAdvancePattern::generate_test(double start_pa, double end_pa, double step_pa)
{
    BoundingBoxf bed_ext = get_extents(mp_gcodegen->config().printable_area.values);
    
    if (is_delta()) {
        delta_scale_bed_ext(bed_ext);
    }

    const auto &w = bed_ext.size().x();
    const auto &h = bed_ext.size().y();
    const auto center_x = w / 2;
    const auto center_y = h / 2;

    const int num_patterns = std::ceil((end_pa - start_pa) / step_pa + 1);

    auto start_x = pattern_start_x(num_patterns, center_x);
    auto start_y = pattern_start_y(start_pa, step_pa, num_patterns, center_y);

    if (is_delta()) {
        delta_modify_start(start_x, start_y, num_patterns);
    }

    CalibPressureAdvancePattern::PatternCalc pattern_calc(
        start_pa,
        step_pa,
        num_patterns,

        center_x,
        center_y,
        start_x,
        start_y,

        print_size_x(num_patterns, center_x),
        frame_size_y(),

        glyph_end_x(num_patterns, center_x),
        glyph_tab_max_x(num_patterns, center_x)
    );

    return print_pa_pattern(pattern_calc);
}

CalibPressureAdvancePattern::PatternSettings() {
    const CalibPressureAdvancePattern cpap;

    anchor_line_width = cpap.line_width_anchor();
    anchor_perimeters = cpap.m_anchor_perimeters;
    encroachment = cpap.m_encroachment;
    extrusion_multiplier = cpap.m_extrusion_multiplier;
    first_layer_height = cpap.m_height_first_layer;
    first_layer_speed = cpap.speed_adjust(cpap.m_speed_first_layer);
    layer_height = cpap.m_height_layer;
    line_width = cpap.line_width();
    perim_speed = cpap.speed_adjust(cpap.m_speed_perimeter;
}

CalibPressureAdvancePattern::DrawLineOptArgs() {
    PatternSettings ps;

    extrusion_multiplier = ps.extrusion_multiplier;
    height = ps.layer_height;
    line_width = ps.line_width;
    speed = ps.perim_speed;
    comment = "Print line";
}

CalibPressureAdvancePattern::DrawBoxOptArgs() {
    PatternSettings ps;

    is_filled = false;
    num_perimeters = ps.anchor_perimeters;
    height = ps.first_layer_height;
    line_width = ps.anchor_line_width;
    speed = ps.first_layer_spee;
}

double CalibPressureAdvancePattern::get_distance(double cur_x, double cur_y, double to_x, double to_y)
{
    return std::hypot((to_x - cur_x), (to_y - cur_y));
}

double CalibPressureAdvancePattern::max_numbering_height(double start_pa, double step_pa, int num_patterns)
{
    int most_characters = 0;

    // note: only every other number is printed
    for (int i = 0; i < num_patterns; i += 2) {
        std::string sNumber = convert_number_to_string(start_pa + (i * step_pa));

        if (sNumber.length > most_characters) { most_characters = sNumber.length; }
    }

    most_characters = std::min(most_characters, m_max_number_length);

    return (most_characters * m_digit_segment_len) + ((most_characters - 1) * m_number_spacing);
}

double CalibPressureAdvancePattern::object_size_x(int num_patterns)
{
    return num_patterns * ((m_wall_count - 1) * line_spacing_angle()) +
        (num_patterns - 1) * (m_pattern_spacing + line_width()) +
        std::cos(to_radians(m_corner_angle) / 2) * m_wall_side_length +
        line_spacing_anchor() * m_anchor_perimeters
    ;
}

double CalibPressureAdvancePattern::object_size_y(double start_pa, double step_pa, int num_patterns)
{
    return 2 * std::sin(to_radians(m_corner_angle) / 2) * m_wall_side_length +
        max_numbering_height(start_pa, step_pa, num_patterns) +
        m_glyph_padding_vertical * 2 +
        line_width_anchor()
}

double CalibPressureAdvancePattern::glyph_start_x(int num_patterns, double center_x)
{
    return
        center_x -
        object_size_x(num_patterns) / 2 +
        (((m_wall_count - 1) / 2) * line_spacing_angle() - 2)
    ;
}

double CalibPressureAdvancePattern::glyph_end_x(int num_patterns, double center_x)
{
    return
        center_x -
        object_size_x(num_patterns) / 2 +
        (num_patterns - 1) * (m_pattern_spacing + line_width()) +
        (num_patterns - 1) * ((m_wall_count - 1) * line_spacing_angle()) +
        4
    ;
}

double CalibPressureAdvancePattern::glyph_tab_max_x(int num_patterns, double center_x)
{
    return
        glyph_end_x(num_patterns, center_x) +
        m_glyph_padding_horizontal +
        line_width_anchor() / 2
    ;
}

double CalibPressureAdvancePattern::pattern_shift(int num_patterns, double center_x)
{
    auto shift =
        center_x -
        object_size_x(num_patterns) / 2 -
        glyph_start_x(num_patterns, center_x) +
        m_glyph_padding_horizontal
    ;

    if (shift > 0) {
        return shift + line_width_anchor() / 2;
    }
    return 0;
}

std::string CalibPressureAdvancePattern::draw_line(double to_x, double to_y, DrawLineOptArgs opt_args = DrawLineOptArgs())
{
    std::stringstream gcode;
    auto& config = mp_gcodegen.config();
    auto& writer = mp_gcodegen.writer();

    Flow line_flow = Flow(opt_args.line_width, opt_args.height, m_nozzle_diameter);
    const double filament_area = M_PI * std::pow(config.filament_diameter.value / 2, 2);
    const double e_per_mm = line_flow.mm3_per_mm() / filament_area * opt_args.extrusion_multiplier;

    Point last_pos = mp_gcodegen.last_pos();
    const double length = get_distance(last_pos.x(), last_pos.y(), to_x, to_y);
    auto dE = e_per_mm * length;

    // TODO: set speed? save current and reset after line?
    gcode << writer.extrude_to_xy(Vec2d(to_x, to_y), dE, opt_args.comment);

    return gcode.str();
}

std::string CalibPressureAdvancePattern::draw_box(double min_x, double min_y, double size_x, double size_y, DrawBoxOptArgs opt_args = DrawBoxOptArgs())
{
    const auto& writer = mp_gcodegen->writer();
    std::stringstream gcode;

    double x = min_x;
    double y = min_y;
    const double max_x = min_x + size_x;
    const double max_y = min_y + size_y;

    const double spacing = opt_args.line_width - opt_args.height * (1 - M_PI / 4);

    // if number of perims exceeds size of box, reduce it to max
    const int max_perimeters =
        std::min(
            // this is the equivalent of number of perims for concentric fill
            std::floor(size_x * std::sin(to_radians(45))) / (spacing / std::sin(to_radians(45))),
            std::floor(size_y * std::sin(to_radians(45))) / (spacing / std::sin(to_radians(45)))
        )
    ;

    opt_args.num_perimeters = std::min(opt_args.num_perimeters, max_perimeters);

    gcode << move_to(Vec2d(min_x, min_y), "Move to box start");

    DrawLineOptArgs line_opt_args();
    line_opt_args.height = opt_args.height;
    line_opt_args.line_width = opt_args.line_width;
    line_opt_args.speed = opt_args.speed;

    for (int i = 0; i < opt_args.num_perimeters; ++i) {
        if (i != 0) { // after first perimeter, step inwards to start next perimeter
            x += spacing;
            y += spacing;
            gcode << move_to(Vec2d(x, y), "Step inwards to print next perimeter");
        }

        y += size_y - i * spacing * 2;
        line_opt_args.comment = "Draw perimeter (up)";
        gcode << draw_line(x, y, line_opt_args);

        x += size_x - i * spacing * 2;
        line_opt_args.comment = "Draw perimeter (right)";
        gcode << draw_line(x, y, line_opt_args);

        y -= size_y - i * spacing * 2;
        line_opt_args.comment = "Draw perimeter (down)";
        gcode << draw_line(x, y, line_opt_args);

        x -= size_x - i * spacing * 2;
        line_opt_args.comment = "Draw perimeter (left)";
        gcode << draw_line(x, y, line_opt_args);
    }

    if (!opt_args.is_filled) {
        return gcode.str();
    }

    // create box infill
    const spacing_45 = spacing / std::sin(to_radians(45));
    const PatternSettings basic_settings;

    const bound_modifier =
        (spacing * (opt_args.num_perimeters - 1)) +
        (opt_args.line_width * (1 - basic_settings.en))
    ;
    const double x_min_bound = min_x + bound_modifier;
    const double x_max_bound = max_x - bound_modifier;
    const double y_min_bound = min_y + bound_modifier;
    const double y_max_bound = max_y - bound_modifier;
    const int x_count = std::floor((x_max_bound - x_min_bound) / spacing_45);
    const int y_count = std::floor((y_max_bound - y_min_bound) / spacing_45);

    double x_remainder = (x_max_bound - x_min_bound) % spacing_45;
    double y_remainder = (y_max_bound - y_min_bound) % spacing_45;

    double x = x_min_bound;
    double y = y_min_bound;

    line_opt_args.comment = "Fill";

    gcode << move_to(Vec2d(x, y), "Move to fill start");

    // this isn't the most robust way, but less expensive than finding line intersections
    for (int i = 0; i < x_count + y_count + (x_remainder + y_remainder >= spacing_45 ? 1 : 0); ++i) {
        if (i < std::min(x_count, y_count)) {
            if (i % 2 == 0) {
                x += spacing_45;
                y = y_min_bound;
                gcode << move_to(Vec2d(x, y)); // step right

                y += x - x_min_bound;
                x = x_min_bound;
                gcode << draw_line(x, y, line_opt_args); // print up/left
            } else {
                y += spacing_45;
                x = x_min_bound;
                gcode << move_to(Vec2d(x, y)); // step up

                x += y - y_min_bound;
                y = y_min_bound;
                gcode << draw_line(x, y, line_opt_args); // print down/right
            }
        } else if (i < std::max(x_count, y_count)) {
            if (x_count > y_count) {
                // box is wider than tall
                if (i % 2 == 0) {
                    x += spacing_45;
                    y = y_min_bound;
                    gcode << move_to(Vec2d(x, y)); // step right

                    x -= y_max_bound - y_min_bound;
                    y = y_max_bound;
                    gcode << draw_line(x, y, line_opt_args); // print up/left
                } else {
                    if (i == y_count) {
                        x += spacing_45 - y_remainder;
                        y_remainder = 0;
                    } else {
                        x += spacing_45;
                    }
                    y = y_max_bound;
                    gcode << move_to(Vec2d(x, y)); // step right
                    
                    x += y_max_bound - y_min_bound;
                    y = y_min_bound;
                    gcode << draw_line(x, y, line_opt_args); // print down/right
                }
            } else {
                // box is taller than wide
                if (i % 2 == 0) {
                    x = x_max_bound;
                    if (i == x_count) {
                        y += spacing_45 - x_remainder;
                        x_remainder = 0;
                    } else {
                        y += spacing_45;
                    }
                    gcode << move_to(Vec2d(x, y)); // step up

                    x = x_min_bound;
                    y += x_max_bound - x_min_bound;
                    gcode << draw_line(x, y, line_opt_args); // print up/left
                } else {
                    x = x_min_bound;
                    y += spacing_45;
                    gcode << move_to(Vec2d(x, y)); // step up

                    x = x_max_bound;
                    y -= x_max_bound - x_min_bound;
                    gcode << draw_line(x, y, line_opt_args); // print down/right
                }
            }
        } else {
            if (i % 2 == 0) {
                x = x_max_bound;
                if (i == x_count) {
                    y += spacing_45 - x_remainder;
                } else {
                    y += spacing_45;
                }
                gcode << move_to(Vec2d(x, y)); // step up

                x -= y_max_bound - y;
                y = y_max_bound;
                gcode << draw_line(x, y, line_opt_args); // print up/left
            } else {
                if (i == y_count) {
                    x += spacing_45 - y_remainder;
                } else {
                    x += spacing_45;
                }
                y = y_max_bound;
                gcode << move_to(Vec2d(x, y)); // step right

                y -= x_max_bound - x;
                x = x_max_bound;
                gcode << draw_line(x, y, line_opt_args); // print down/right
            }
        }
    }

    return gcode.str();
}

std::string CalibPressureAdvancePattern::print_pa_pattern(PatternCalc& calc)
{   
    const auto& writer = mp_gcodegen->writer();
    std::stringstream gcode;

    const DrawLineOptArgs draw_line_basic_settings;
    DrawLineOptArgs draw_line_opt_args;
    const DrawBoxOptArgs draw_box_basic_settings;
    DrawBoxOptArgs draw_box_opt_args;

    gcode << writer.travel_to_z(m_height_first_layer, "Move to start layer height");
    gcode << move_to(Vec2d(calc.pattern_start_x, calc.pattern_start_y), "Move to start position");

    writer.set_pressure_advance(calc.start_pa);

    // create anchor and line numbering frame
    gcode << draw_box(
        calc.pattern_start_x,
        calc.pattern_start_y,
        calc.print_size_x,
        calc.frame_size_y,
        draw_box_basic_settings
    );

    // create tab for numbers
    draw_box_opt_args = draw_box_basic_settings;
    draw_box_opt_args.is_filled = true;
    gcode << draw_box(
        calc.pattern_start_x,
        calc.pattern_start_y + calc.frame_size_y + line_spacing_anchor(),
        glyph_tab_max_x(calc.num_patterns, calc.center_x) - calc.pattern_start_x,
        max_numbering_height(calc.start_pa, calc.step_pa, calc.num_patterns) + line_spacing_anchor() + m_glyph_padding_vertical * 2,
        draw_box_opt_args
    );

    // draw pressure advance pattern
    for (int i = 0; i < m_num_layers; ++i) {
        if (i == 1) {
            // TODO?
            // set new fan speed after first layer
        }

        gcode << writer().travel_to_z(m_height_first_layer + i * m_height_layer, "Move to layer height");

        // line numbering
        if (i == 1) {
            gcode << writer.set_pressure_advance(calc.start_pa);

            // glyph on every other line
            for (int j = 0; j < calc.num_patterns; j += 2) {
                double current_glyph_start_x =
                    calc.pattern_start_x +
                    (j * (m_pattern_spacing + line_width())) +
                    (j * ((m_wall_count - 1) * line_spacing_angle())) // this aligns glyph starts with first pattern perim
                ;
                // shift glyph center to middle of pattern walls. m_digit_segment_len = half of x width of glyph
                current_glyph_start_x +=
                    (((m_wall_count - 1) / 2) * line_spacing_angle()) - m_digit_segment_len
                ;
                current_glyph_start_x += pattern_shift(calc.num_patterns, calc.center_x);

                gcode << draw_number(
                    current_glyph_start_x,
                    calc.pattern_start_y + calc.frame_size_y + m_glyph_padding_vertical + line_width(),
                    calc.start_pa + (j * calc.step_pa),
                    DrawDigitMode::Vertical
                );
            }
        }

        double to_x = calc.pattern_start_x + pattern_shift(calc.num_patterns, calc.center_x);
        double to_y = calc.pattern_start_y;
        double side_length = m_wall_side_length;

        if (i == 0) {
            // shrink first layer to fit inside frame
            double shrink =
                (
                    line_spacing_anchor() * (m_anchor_perimeters - 1) +
                    (line_width_anchor() * (1 - m_encroachment))
                ) / std::sin(to_radians(m_corner_angle) / 2)
            ;
            side_length = m_wall_side_length - shrink;
            to_x += shrink + std::sin(to_radians(90) - to_radians(m_corner_angle) / 2);
            to_y +=
                line_spacing_anchor() * (m_anchor_perimeters - 1) +
                (line_width_anchor() * (1 - m_encroachment))
            ;
        }

        double initial_x = to_x;
        double initial_y = to_y;

        gcode << move_to(Vec2d(to_x, to_y), "Move to pattern start");

        for (int j = 0; j < calc.num_patterns; ++j) {
            // increment pressure advance
            writer.set_pressure_advance(calc.start_pa + (j * calc.step_pa));

            for (int k = 0; k < m_wall_count; ++k) {
                to_x += std::cos(to_radians(m_corner_angle) / 2) * side_length;
                to_y += std::sin(to_radians(m_corner_angle) / 2) * side_length;
                
                draw_line_opt_args = draw_line_basic_settings;
                draw_line_opt_args.height = i == 0 ? m_height_first_layer : m_height_layer;
                draw_line_opt_args.speed = i == 0 ? m_speed_first_layer : m_speed_perimeter;
                draw_line_opt_args.comment = "Print pattern wall";
                
                gcode << draw_line(to_x, to_y, draw_line_opt_args);

                to_y = initial_y;
                if (k != m_wall_count - 1) {
                    // perimeters not done yet. move to next perimeter
                    to_x += line_spacing_angle();
                    gcode << move_to(Vec2d(to_x, to_y), "Move to start next pattern wall");
                } else if (j != calc.num_patterns - 1) {
                    // patterns not done yet. move to next pattern
                    to_x += m_pattern_spacing + line_width();
                    gcode << move_to(Vec2d(to_x, to_y), "Move to next pattern");
                } else if (i != m_num_layers - 1) {
                    // layers not done yet. move back to start
                    to_x = initial_x;
                    gcode << move_to(Vec2d(to_x, to_y), "Move back to start position");
                } else {
                    // everything done
                }
            }
        }
    }

    // set pressure advance back to start value
    writer.set_pressure_advance(calc.start_pa);

    return gcode.str();
}
} // namespace Slic3r
