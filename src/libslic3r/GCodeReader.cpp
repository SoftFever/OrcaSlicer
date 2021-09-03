#include "GCodeReader.hpp"
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/nowide/fstream.hpp>
#include <charconv>
#include <fstream>
#include <iostream>
#include <iomanip>

#include "LocalesUtils.hpp"

#include <Shiny/Shiny.h>

namespace Slic3r {

static inline char get_extrusion_axis_char(const GCodeConfig &config)
{
    std::string axis = get_extrusion_axis(config);
    assert(axis.size() <= 1);
    // Return 0 for gcfNoExtrusion
    return axis.empty() ? 0 : axis[0];
}

void GCodeReader::apply_config(const GCodeConfig &config)
{
    m_config = config;
    m_extrusion_axis = get_extrusion_axis_char(m_config);
}

void GCodeReader::apply_config(const DynamicPrintConfig &config)
{
    m_config.apply(config, true);
    m_extrusion_axis = get_extrusion_axis_char(m_config);
}

const char* GCodeReader::parse_line_internal(const char *ptr, const char *end, GCodeLine &gline, std::pair<const char*, const char*> &command)
{
    PROFILE_FUNC();

    assert(is_decimal_separator_point());
    
    // command and args
    const char *c = ptr;
    {
        PROFILE_BLOCK(command_and_args);
        // Skip the whitespaces.
        command.first = skip_whitespaces(c);
        // Skip the command.
        c = command.second = skip_word(command.first);
        // Up to the end of line or comment.
		while (! is_end_of_gcode_line(*c)) {
            // Skip whitespaces.
            c = skip_whitespaces(c);
			if (is_end_of_gcode_line(*c))
				break;
            // Check the name of the axis.
            Axis axis = NUM_AXES_WITH_UNKNOWN;
            switch (*c) {
            case 'X': axis = X; break;
            case 'Y': axis = Y; break;
            case 'Z': axis = Z; break;
            case 'F': axis = F; break;
            default:
                if (*c == m_extrusion_axis) {
                    if (m_extrusion_axis != 0)
                        axis = E;
                } else if (*c >= 'A' && *c <= 'Z')
                	// Unknown axis, but we still want to remember that such a axis was seen.
                	axis = UNKNOWN_AXIS;
                break;
            }
            if (axis != NUM_AXES_WITH_UNKNOWN) {
                // Try to parse the numeric value.
                double v;
                auto [pend, ec] = std::from_chars(++ c, end, v);
                if (pend != c && is_end_of_word(*pend)) {
                    // The axis value has been parsed correctly.
                    if (axis != UNKNOWN_AXIS)
	                    gline.m_axis[int(axis)] = float(v);
                    gline.m_mask |= 1 << int(axis);
                    c = pend;
                } else
                    // Skip the rest of the word.
                    c = skip_word(c);
            } else
                // Skip the rest of the word.
                c = skip_word(c);
        }
    }
    
    if (gline.has(E) && m_config.use_relative_e_distances)
        m_position[E] = 0;

    // Skip the rest of the line.
    for (; ! is_end_of_line(*c); ++ c);

    // Copy the raw string including the comment, without the trailing newlines.
    if (c > ptr) {
        PROFILE_BLOCK(copy_raw_string);
        gline.m_raw.assign(ptr, c);
    }

    // Skip the trailing newlines.
	if (*c == '\r')
		++ c;
	if (*c == '\n')
		++ c;

    if (m_verbose)
        std::cout << gline.m_raw << std::endl;

    return c;
}

void GCodeReader::update_coordinates(GCodeLine &gline, std::pair<const char*, const char*> &command)
{
    PROFILE_FUNC();
    if (*command.first == 'G') {
        int cmd_len = int(command.second - command.first);
        if ((cmd_len == 2 && (command.first[1] == '0' || command.first[1] == '1')) ||
            (cmd_len == 3 &&  command.first[1] == '9' && command.first[2] == '2')) {
            for (size_t i = 0; i < NUM_AXES; ++ i)
                if (gline.has(Axis(i)))
                    m_position[i] = gline.value(Axis(i));
        }
    }
}

void GCodeReader::parse_file(const std::string &file, callback_t callback)
{
    boost::nowide::ifstream f(file);
    f.sync_with_stdio(false);
    std::vector<char> buffer(65536 * 10, 0);
    std::string line;
    m_parsing = true;
    GCodeLine gline;
    bool eof = false;
    while (m_parsing && ! eof) {
        f.read(buffer.data(), buffer.size());
        auto it        = buffer.begin();
        auto it_bufend = buffer.begin() + f.gcount();
        eof = ! f.good();
        while (it != it_bufend) {
            bool eol = false;
            auto it_end = it;
            for (; it_end != it_bufend && ! (eol = *it_end == '\r' || *it_end == '\n'); ++ it_end) ;
            eol |= eof && it_end == it_bufend;
            if (eol) {
                gline.reset();
                if (line.empty())
                    this->parse_line(&(*it), &(*it_end), gline, callback);
                else {
                    line.insert(line.end(), it, it_end);
                    this->parse_line(line.c_str(), line.c_str() + line.size(), gline, callback);
                    line.clear();
                }
            } else
                line.insert(line.end(), it, it_end);
            // Skip all the empty lines.
            for (it = it_end; it != it_bufend && (*it == '\r' || *it == '\n'); ++ it) ;
        }
    }
}

bool GCodeReader::GCodeLine::has(char axis) const
{
    const char *c = m_raw.c_str();
    // Skip the whitespaces.
    c = skip_whitespaces(c);
    // Skip the command.
    c = skip_word(c);
    // Up to the end of line or comment.
    while (! is_end_of_gcode_line(*c)) {
        // Skip whitespaces.
        c = skip_whitespaces(c);
        if (is_end_of_gcode_line(*c))
            break;
        // Check the name of the axis.
        if (*c == axis)
            return true;
        // Skip the rest of the word.
        c = skip_word(c);
    }
    return false;
}

bool GCodeReader::GCodeLine::has_value(char axis, float &value) const
{
    assert(is_decimal_separator_point());
    const char *c = m_raw.c_str();
    // Skip the whitespaces.
    c = skip_whitespaces(c);
    // Skip the command.
    c = skip_word(c);
    // Up to the end of line or comment.
    while (! is_end_of_gcode_line(*c)) {
        // Skip whitespaces.
        c = skip_whitespaces(c);
        if (is_end_of_gcode_line(*c))
            break;
        // Check the name of the axis.
        if (*c == axis) {
            // Try to parse the numeric value.
            char   *pend = nullptr;
            double  v = strtod(++ c, &pend);
            if (pend != nullptr && is_end_of_word(*pend)) {
                // The axis value has been parsed correctly.
                value = float(v);
                return true;
            }
        }
        // Skip the rest of the word.
        c = skip_word(c);
    }
    return false;
}

void GCodeReader::GCodeLine::set(const GCodeReader &reader, const Axis axis, const float new_value, const int decimal_digits)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimal_digits) << new_value;

    char match[3] = " X";
    if (int(axis) < 3)
        match[1] += int(axis);
    else if (axis == F)
        match[1] = 'F';
    else {
        assert(axis == E);
        // Extruder axis is set.
        assert(reader.extrusion_axis() != 0);
        match[1] = reader.extrusion_axis();
    }

    if (this->has(axis)) {
        size_t pos = m_raw.find(match)+2;
        size_t end = m_raw.find(' ', pos+1);
        m_raw = m_raw.replace(pos, end-pos, ss.str());
    } else {
        size_t pos = m_raw.find(' ');
        if (pos == std::string::npos)
            m_raw += std::string(match) + ss.str();
        else
            m_raw = m_raw.replace(pos, 0, std::string(match) + ss.str());
    }
    m_axis[axis] = new_value;
    m_mask |= 1 << int(axis);
}

}
