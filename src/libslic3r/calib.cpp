#include "calib.hpp"
#include "PrintConfig.hpp"
#include "GCodeWriter.hpp"
#include "GCode.hpp"
#include <map>

namespace Slic3r {
CalibPressureAdvance::CalibPressureAdvance(GCode* gcodegen) :
    mp_gcodegen(gcodegen)
{
    if (gcodegen != nullptr) {
        m_nozzle_diameter = gcodegen->config().nozzle_diameter.get_at(0);
    }
};

bool CalibPressureAdvance::is_delta() const
{
    return mp_gcodegen->config().printable_area.values.size() > 4;
}

std::string CalibPressureAdvance::move_to(Vec2d pt, std::string comment)
{
    std::stringstream gcode;
    GCodeWriter& writer = mp_gcodegen->writer();

    gcode << writer.retract();
    gcode << writer.travel_to_xy(pt, comment);
    gcode << writer.unretract();

    return gcode.str(); 
}

double CalibPressureAdvance::e_per_mm(
    double line_width,
    double layer_height,
    float filament_diameter,
    float print_flow_ratio
) {
    const Flow line_flow = Flow(line_width, layer_height, m_nozzle_diameter);
    const double filament_area = M_PI * std::pow(filament_diameter / 2, 2);

    return line_flow.mm3_per_mm() / filament_area * print_flow_ratio;
}

std::string CalibPressureAdvance::convert_number_to_string(double num)
{
    auto sNumber = std::to_string(num);
    sNumber.erase(sNumber.find_last_not_of('0') + 1, std::string::npos);
    sNumber.erase(sNumber.find_last_not_of('.') + 1, std::string::npos);

    return sNumber;
}

std::string CalibPressureAdvance::draw_digit(
    double startx,
    double starty,
    char c,
    CalibPressureAdvance::DrawDigitMode mode,
    double line_width,
    double e_per_mm    
)
{
    const double len = m_digit_segment_len;
    const double gap = line_width / 2.0;

    const auto dE = e_per_mm * len;
    const auto two_dE = dE * 2;

    Vec2d p0, p1, p2, p3, p4, p5;
    Vec2d p0_5, p4_5;
    Vec2d gap_p0_toward_p3, gap_p2_toward_p3;
    Vec2d dot_direction;

    if (mode == CalibPressureAdvance::DrawDigitMode::Bottom_To_Top) {
        //  1-------2-------5
        //  |       |       |
        //  |       |       |
        //  0-------3-------4
        p0 = Vec2d(startx, starty);
        p0_5 = Vec2d(startx, starty + len / 2);
        p1 = Vec2d(startx, starty + len);
        p2 = Vec2d(startx + len, starty + len);
        p3 = Vec2d(startx + len, starty);
        p4 = Vec2d(startx + len * 2, starty);
        p4_5 = Vec2d(startx + len * 2, starty + len / 2);
        p5 = Vec2d(startx + len * 2, starty + len);

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

        dot_direction = Vec2d(0, len / 2);
    }

    std::stringstream gcode;
    GCodeWriter& writer = mp_gcodegen->writer();

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

std::string CalibPressureAdvance::draw_number(
    double startx,
    double starty,
    double value,
    CalibPressureAdvance::DrawDigitMode mode,
    double line_width,
    double e_per_mm
)
{
    auto sNumber = convert_number_to_string(value);
    std::stringstream gcode;
    gcode << mp_gcodegen->writer().set_speed(3600);

    for (std::string::size_type i = 0; i < sNumber.length(); ++i) {
        if (i > m_max_number_len) {
            break;
        }
        switch (mode) {
            case DrawDigitMode::Bottom_To_Top:
                gcode << draw_digit(startx, starty + i * number_spacing(), sNumber[i], mode, line_width, e_per_mm);
                break;
            default:
                gcode << draw_digit(startx + i * number_spacing(), starty, sNumber[i], mode, line_width, e_per_mm);
        }
    }

    return gcode.str();
}

CalibPressureAdvanceLine::CalibPressureAdvanceLine(GCode* gcodegen) :
    CalibPressureAdvance(gcodegen),
    m_thin_line_width(0.44),
    m_length_short(20.0),
    m_length_long(40.0),
    m_space_y(3.5),
    m_draw_numbers(true)
    {
        this->m_line_width = 0.6;
        this->m_number_line_width = 0.48;
        this->m_height_layer = 0.2;
    }
;

std::string CalibPressureAdvanceLine::generate_test(double start_pa /*= 0*/, double step_pa /*= 0.002*/, int count /*= 10*/)
{
    BoundingBoxf bed_ext = get_extents(mp_gcodegen->config().printable_area.values);
    if (is_delta()) {
        CalibPressureAdvanceLine::delta_scale_bed_ext(bed_ext);
    }

    auto bed_sizes = mp_gcodegen->config().printable_area.values;
    const auto &w = bed_ext.size().x();
    const auto &h = bed_ext.size().y();
    count = std::min(count, int((h - 10) / m_space_y));

    m_length_long = 40 + std::min(w - 120.0, 0.0);

    auto startx = (w - m_length_short * 2 - m_length_long - 20) / 2;
    auto starty = (h - count * m_space_y) / 2;
    if (is_delta()) {
        CalibPressureAdvanceLine::delta_modify_start(startx, starty, count);
    }

    return print_pa_lines(startx, starty, start_pa, step_pa, count);
}

std::string CalibPressureAdvanceLine::print_pa_lines(double start_x, double start_y, double start_pa, double step_pa, int num)
{
    auto& writer = mp_gcodegen->writer();
    const auto& config = mp_gcodegen->config();

    const auto filament_diameter = config.filament_diameter.get_at(0);
    const auto print_flow_ratio = config.print_flow_ratio;

    const double e_per_mm = CalibPressureAdvance::e_per_mm(
        m_line_width,
        m_height_layer,
        filament_diameter,
        print_flow_ratio
    );
    const double thin_e_per_mm = CalibPressureAdvance::e_per_mm(
        m_thin_line_width,
        m_height_layer,
        filament_diameter,
        print_flow_ratio
    );

    const double fast = CalibPressureAdvance::speed_adjust(m_fast_speed);
    const double slow = CalibPressureAdvance::speed_adjust(m_slow_speed);
    std::stringstream gcode;
    gcode << mp_gcodegen->writer().travel_to_z(m_height_layer);
    double y_pos = start_y;

    // prime line
    auto prime_x = start_x - 2;
    gcode << move_to(Vec2d(prime_x, y_pos + (num - 4) * m_space_y));
    gcode << writer.set_speed(slow);
    gcode << writer.extrude_to_xy(Vec2d(prime_x, y_pos + 3 * m_space_y), e_per_mm * m_space_y * num * 1.1);

    for (int i = 0; i < num; ++i) {
        gcode << writer.set_pressure_advance(start_pa + i * step_pa);
        gcode << move_to(Vec2d(start_x, y_pos + i * m_space_y));
        gcode << writer.set_speed(slow);
        gcode << writer.extrude_to_xy(Vec2d(start_x + m_length_short, y_pos + i * m_space_y), e_per_mm * m_length_short);
        gcode << writer.set_speed(fast);
        gcode << writer.extrude_to_xy(Vec2d(start_x + m_length_short + m_length_long, y_pos + i * m_space_y), e_per_mm * m_length_long);
        gcode << writer.set_speed(slow);
        gcode << writer.extrude_to_xy(Vec2d(start_x + m_length_short + m_length_long + m_length_short, y_pos + i * m_space_y), e_per_mm * m_length_short);
    }
    gcode << writer.set_pressure_advance(0.0);

    if (m_draw_numbers) {
        // draw indicator lines
        gcode << writer.set_speed(fast);
        gcode << move_to(Vec2d(start_x + m_length_short, y_pos + (num - 1) * m_space_y + 2));
        gcode << writer.extrude_to_xy(Vec2d(start_x + m_length_short, y_pos + (num - 1) * m_space_y + 7), thin_e_per_mm * 7);
        gcode << move_to(Vec2d(start_x + m_length_short + m_length_long, y_pos + (num - 1) * m_space_y + 7));
        gcode << writer.extrude_to_xy(Vec2d(start_x + m_length_short + m_length_long, y_pos + (num - 1) * m_space_y + 2), thin_e_per_mm * 7);

        for (int i = 0; i < num; i += 2) {
            gcode << draw_number(
                start_x + m_length_short + m_length_long + m_length_short + 3,
                y_pos + i * m_space_y + m_space_y / 2,
                start_pa + i * step_pa,
                DrawDigitMode::Left_To_Right,
                m_number_line_width,
                m_height_layer
            );
        }
    }
    return gcode.str();
}

void CalibPressureAdvanceLine::delta_modify_start(double& startx, double& starty, int count)
{
    startx = -startx;
    starty = -(count * m_space_y) / 2;
}

PatternSettings::PatternSettings(const CalibPressureAdvancePattern& cpap) {    
    anchor_line_width = cpap.line_width_anchor();
    anchor_perimeters = cpap.anchor_perimeters();
    encroachment = cpap.encroachment();
    first_layer_height = cpap.height_first_layer();
    first_layer_speed = cpap.speed_adjust(cpap.speed_first_layer());
    layer_height = cpap.height_layer();
    line_width = cpap.line_width();
    perim_speed = cpap.speed_adjust(cpap.speed_perimeter());
}

CalibPressureAdvancePattern::CalibPressureAdvancePattern(const Calib_Params& params) :
    CalibPressureAdvancePattern(params, nullptr)
{ };

CalibPressureAdvancePattern::CalibPressureAdvancePattern(const Calib_Params& params, GCode* gcodegen) :
    CalibPressureAdvance(gcodegen),
    m_start_pa(params.start),
    m_end_pa(params.end),
    m_step_pa(params.step)
{    
    this->m_draw_digit_mode = DrawDigitMode::Bottom_To_Top;
    this->m_line_width = line_width();
    this->m_height_layer = 0.2;
}

CustomGCode::Info CalibPressureAdvancePattern::generate_gcodes()
{
    assert(mp_gcodegen != nullptr);

    std::stringstream gcode;
    GCodeWriter& writer = mp_gcodegen->writer();

    gcode << writer.travel_to_xyz(
        Vec3d(pattern_start_x(), pattern_start_y(), m_height_first_layer),
        "Move to start position"
    );

    gcode << writer.set_pressure_advance(m_start_pa);

    const PatternSettings pattern_settings(*this);
    const DrawBoxOptArgs default_box_opt_args(pattern_settings);

    // create anchor frame
    gcode << draw_box(
        pattern_start_x(),
        pattern_start_y(),
        print_size_x(),
        frame_size_y(),
        default_box_opt_args
    );

    // create tab for numbers
    DrawBoxOptArgs draw_box_opt_args = default_box_opt_args;
    draw_box_opt_args.is_filled = true;
    draw_box_opt_args.num_perimeters = m_anchor_perimeters;
    gcode << draw_box(
        pattern_start_x(),
        pattern_start_y() + frame_size_y() + line_spacing_anchor(),
        glyph_tab_max_x() - pattern_start_x(),
        max_numbering_height() + line_spacing_anchor() + m_glyph_padding_vertical * 2,
        draw_box_opt_args
    );

    std::vector<CustomGCode::Item> gcode_items;

    const DrawLineOptArgs default_line_opt_args(pattern_settings);
    const int num_patterns = get_num_patterns(); // "cache" for use in loops

    // draw pressure advance pattern
    for (int i = 0; i < m_num_layers; ++i) {
        if (i > 0) {
            CustomGCode::Item item;
            item.print_z = i == 1 ? m_height_first_layer : m_height_layer;
            item.type = CustomGCode::Type::Custom;
            item.extra = gcode.str();
            gcode_items.push_back(item);
            gcode = std::stringstream(); // reset for next layer contents
        }

        // // line numbering
        if (i == 1) {
            gcode << writer.set_pressure_advance(m_start_pa);

            // glyph on every other line
            for (int j = 0; j < num_patterns; j += 2) {
                double current_glyph_start_x =
                    pattern_start_x() +
                    (j * (m_pattern_spacing + line_width())) +
                    (j * ((m_wall_count - 1) * line_spacing_angle())) // this aligns glyph starts with first pattern perim
                ;
                // shift glyph center to middle of pattern walls. m_digit_segment_len = half of x width of glyph
                current_glyph_start_x +=
                    (((m_wall_count - 1) / 2) * line_spacing_angle()) - m_digit_segment_len
                ;
                current_glyph_start_x += pattern_shift();

                gcode << draw_number(
                    current_glyph_start_x,
                    pattern_start_y() + frame_size_y() + m_glyph_padding_vertical + line_width(),
                    m_start_pa + (j * m_step_pa),
                    DrawDigitMode::Bottom_To_Top,
                    m_line_width,
                    m_height_layer
                );
            }
        }

        DrawLineOptArgs draw_line_opt_args = default_line_opt_args;

        double to_x = pattern_start_x() + pattern_shift();
        double to_y = pattern_start_y();
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
            to_x += shrink * std::sin(to_radians(90) - to_radians(m_corner_angle) / 2);
            to_y +=
                line_spacing_anchor() * (m_anchor_perimeters - 1) +
                (line_width_anchor() * (1 - m_encroachment))
            ;
        }

        double initial_x = to_x;
        double initial_y = to_y;

        gcode << move_to(Vec2d(to_x, to_y), "Move to pattern start");

        for (int j = 0; j < num_patterns; ++j) {
            // increment pressure advance
            gcode << writer.set_pressure_advance(m_start_pa + (j * m_step_pa));

            for (int k = 0; k < m_wall_count; ++k) {
                to_x += std::cos(to_radians(m_corner_angle) / 2) * side_length;
                to_y += std::sin(to_radians(m_corner_angle) / 2) * side_length;
                
                draw_line_opt_args = default_line_opt_args;
                draw_line_opt_args.height = i == 0 ? m_height_first_layer : m_height_layer;
                draw_line_opt_args.speed = i == 0 ? m_speed_first_layer : m_speed_perimeter;
                draw_line_opt_args.comment = "Print pattern wall";
                gcode << draw_line(to_x, to_y, draw_line_opt_args);

                to_x -= std::cos(to_radians(m_corner_angle) / 2) * side_length;
                to_y += std::sin(to_radians(m_corner_angle) / 2) * side_length;

                gcode << draw_line(to_x, to_y, draw_line_opt_args);

                to_y = initial_y;
                if (k != m_wall_count - 1) {
                    // perimeters not done yet. move to next perimeter
                    to_x += line_spacing_angle();
                    gcode << move_to(Vec2d(to_x, to_y), "Move to start next pattern wall");
                } else if (j != num_patterns - 1) {
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
    gcode << writer.set_pressure_advance(m_start_pa);

    CustomGCode::Item item;
    item.print_z = m_height_layer;
    item.type = CustomGCode::Type::Custom;
    item.extra = gcode.str();

    gcode_items.push_back(item);

    CustomGCode::Info info;
    info.mode = CustomGCode::Mode::SingleExtruder;
    info.gcodes = gcode_items;

    return info;
}

std::string CalibPressureAdvancePattern::draw_line(double to_x, double to_y, DrawLineOptArgs opt_args)
{
    std::stringstream gcode;

    const double e_per_mm = CalibPressureAdvance::e_per_mm(
        opt_args.line_width,
        opt_args.height,
        mp_gcodegen->config().filament_diameter.get_at(0),
        mp_gcodegen->config().filament_flow_ratio.get_at(0)
    );

    const Point last_pos = mp_gcodegen->last_pos();
    const double length = get_distance(last_pos.x(), last_pos.y(), to_x, to_y);
    auto dE = e_per_mm * length;

    gcode << mp_gcodegen->writer().extrude_to_xy(Vec2d(to_x, to_y), dE, opt_args.comment);

    return gcode.str();
}

std::string CalibPressureAdvancePattern::draw_box(double min_x, double min_y, double size_x, double size_y, DrawBoxOptArgs opt_args)
{
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

    PatternSettings ps(*this);
    DrawLineOptArgs line_opt_args(ps);
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
    const double spacing_45 = spacing / std::sin(to_radians(45));

    const double bound_modifier =
        (spacing * (opt_args.num_perimeters - 1)) +
        (opt_args.line_width * (1 - m_encroachment))
    ;
    const double x_min_bound = min_x + bound_modifier;
    const double x_max_bound = max_x - bound_modifier;
    const double y_min_bound = min_y + bound_modifier;
    const double y_max_bound = max_y - bound_modifier;
    const int x_count = std::floor((x_max_bound - x_min_bound) / spacing_45);
    const int y_count = std::floor((y_max_bound - y_min_bound) / spacing_45);

    double x_remainder = std::fmod((x_max_bound - x_min_bound), spacing_45);
    double y_remainder = std::fmod((y_max_bound - y_min_bound), spacing_45);

    x = x_min_bound;
    y = y_min_bound;

    gcode << move_to(Vec2d(x, y), "Move to fill start");

    for (int i = 0; i < x_count + y_count + (x_remainder + y_remainder >= spacing_45 ? 1 : 0); ++i) { // this isn't the most robust way, but less expensive than finding line intersections
        if (i < std::min(x_count, y_count)) {
            if (i % 2 == 0) {
                x += spacing_45;
                y = y_min_bound;
                gcode << move_to(Vec2d(x, y), "Fill: Step right");

                y += x - x_min_bound;
                x = x_min_bound;
                line_opt_args.comment = "Fill: Print up/left";
                gcode << draw_line(x, y, line_opt_args);
            } else {
                y += spacing_45;
                x = x_min_bound;
                gcode << move_to(Vec2d(x, y), "Fill: Step up");

                x += y - y_min_bound;
                y = y_min_bound;
                line_opt_args.comment = "Fill: Print down/right";
                gcode << draw_line(x, y, line_opt_args);
            }
        } else if (i < std::max(x_count, y_count)) {
            if (x_count > y_count) {
                // box is wider than tall
                if (i % 2 == 0) {
                    x += spacing_45;
                    y = y_min_bound;
                    gcode << move_to(Vec2d(x, y), "Fill: Step right");

                    x -= y_max_bound - y_min_bound;
                    y = y_max_bound;
                    line_opt_args.comment = "Fill: Print up/left";
                    gcode << draw_line(x, y, line_opt_args);
                } else {
                    if (i == y_count) {
                        x += spacing_45 - y_remainder;
                        y_remainder = 0;
                    } else {
                        x += spacing_45;
                    }
                    y = y_max_bound;
                    gcode << move_to(Vec2d(x, y), "Fill: Step right");
                    
                    x += y_max_bound - y_min_bound;
                    y = y_min_bound;
                    line_opt_args.comment = "Fill: Print down/right";
                    gcode << draw_line(x, y, line_opt_args);
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
                    gcode << move_to(Vec2d(x, y), "Fill: Step up");

                    x = x_min_bound;
                    y += x_max_bound - x_min_bound;
                    line_opt_args.comment = "Fill: Print up/left";
                    gcode << draw_line(x, y, line_opt_args);
                } else {
                    x = x_min_bound;
                    y += spacing_45;
                    gcode << move_to(Vec2d(x, y), "Fill: Step up");

                    x = x_max_bound;
                    y -= x_max_bound - x_min_bound;
                    line_opt_args.comment = "Fill: Print down/right";
                    gcode << draw_line(x, y, line_opt_args);
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
                gcode << move_to(Vec2d(x, y), "Fill: Step up");

                x -= y_max_bound - y;
                y = y_max_bound;
                line_opt_args.comment = "Fill: Print up/left";
                gcode << draw_line(x, y, line_opt_args);
            } else {
                if (i == y_count) {
                    x += spacing_45 - y_remainder;
                } else {
                    x += spacing_45;
                }
                y = y_max_bound;
                gcode << move_to(Vec2d(x, y), "Fill: Step right");

                y -= x_max_bound - x;
                x = x_max_bound;
                line_opt_args.comment = "Fill: Print down/right";
                gcode << draw_line(x, y, line_opt_args);
            }
        }
    }

    return gcode.str();
}

double CalibPressureAdvancePattern::get_distance(double cur_x, double cur_y, double to_x, double to_y)
{
    return std::hypot((to_x - cur_x), (to_y - cur_y));
}

Point CalibPressureAdvancePattern::bed_center()
{
    BoundingBoxf bed_ext = get_extents(mp_gcodegen->config().printable_area.values);
    
    if (is_delta()) {
        delta_scale_bed_ext(bed_ext);
    }

    double center_x = bed_ext.size().x() / 2;
    double center_y = bed_ext.size().y() / 2;

    return Point(center_x, center_y);
}

double CalibPressureAdvancePattern::object_size_x()
{
    return get_num_patterns() * ((m_wall_count - 1) * line_spacing_angle()) +
        (get_num_patterns() - 1) * (m_pattern_spacing + line_width()) +
        std::cos(to_radians(m_corner_angle) / 2) * m_wall_side_length +
        line_spacing_anchor() * m_anchor_perimeters
    ;
}

double CalibPressureAdvancePattern::object_size_y()
{
    return 2 * (std::sin(to_radians(m_corner_angle) / 2) * m_wall_side_length) +
        max_numbering_height() +
        m_glyph_padding_vertical * 2 +
        line_width_anchor();
}

double CalibPressureAdvancePattern::glyph_start_x()
{
    return
        bed_center().x() -
        object_size_x() / 2 +
        (((m_wall_count - 1) / 2) * line_spacing_angle() - 2)
    ;
}

double CalibPressureAdvancePattern::glyph_end_x()
{
    return
        bed_center().x() -
        object_size_x() / 2 +
        (get_num_patterns() - 1) * (m_pattern_spacing + line_width()) +
        (get_num_patterns() - 1) * ((m_wall_count - 1) * line_spacing_angle()) +
        4
    ;
}

double CalibPressureAdvancePattern::glyph_tab_max_x()
{
    return
        glyph_end_x() +
        m_glyph_padding_horizontal +
        line_width_anchor() / 2
    ;
}

double CalibPressureAdvancePattern::max_numbering_height()
{
    std::string::size_type most_characters = 0;
    const int num_patterns = get_num_patterns();

    // note: only every other number is printed
    for (std::string::size_type i = 0; i < num_patterns; i += 2) {
        std::string sNumber = convert_number_to_string(m_start_pa + (i * m_step_pa));

        if (sNumber.length() > most_characters) {
            most_characters = sNumber.length();
        }
    }

    most_characters = std::min(most_characters, m_max_number_len);

    return (most_characters * m_digit_segment_len) + ((most_characters - 1) * m_digit_gap_len);
}

double CalibPressureAdvancePattern::pattern_shift()
{
    auto shift =
        bed_center().x() -
        object_size_x() / 2 -
        glyph_start_x() +
        m_glyph_padding_horizontal
    ;

    if (shift > 0) {
        return shift + line_width_anchor() / 2;
    }
    return 0;
}

double CalibPressureAdvancePattern::pattern_start_x()
{
    double pattern_start_x =
        bed_center().x() -
        (object_size_x() + pattern_shift()) / 2
    ;

    if (is_delta()) {
        pattern_start_x = -pattern_start_x;
    }

    return pattern_start_x;
};

double CalibPressureAdvancePattern::pattern_start_y()
{
    double pattern_start_y = bed_center().y() - object_size_y() / 2;

    if (is_delta()) {
        pattern_start_y = -(frame_size_y() / 2);
    }

    return pattern_start_y;
};
} // namespace Slic3r
