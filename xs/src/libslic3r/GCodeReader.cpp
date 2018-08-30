#include "GCodeReader.hpp"
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <fstream>
#include <iostream>

#include <Shiny/Shiny.h>

namespace Slic3r {

void GCodeReader::apply_config(const GCodeConfig &config)
{
    m_config = config;
    m_extrusion_axis = m_config.get_extrusion_axis()[0];
}

void GCodeReader::apply_config(const DynamicPrintConfig &config)
{
    m_config.apply(config, true);
    m_extrusion_axis = m_config.get_extrusion_axis()[0];
}

const char* GCodeReader::parse_line_internal(const char *ptr, GCodeLine &gline, std::pair<const char*, const char*> &command)
{
    PROFILE_FUNC();
    
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
            Axis axis = NUM_AXES;
            switch (*c) {
            case 'X': axis = X; break;
            case 'Y': axis = Y; break;
            case 'Z': axis = Z; break;
            case 'F': axis = F; break;
            default:
                if (*c == m_extrusion_axis)
                    axis = E;
                break;
            }
            if (axis != NUM_AXES) {
                // Try to parse the numeric value.
                char   *pend = nullptr;
                double  v = strtod(++ c, &pend);
                if (pend != nullptr && is_end_of_word(*pend)) {
                    // The axis value has been parsed correctly.
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
                    this->m_position[i] = gline.value(Axis(i));
        }
    }
}

void GCodeReader::parse_file(const std::string &file, callback_t callback)
{
    std::ifstream f(file);
    std::string line;
    while (std::getline(f, line))
        this->parse_line(line, callback);
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
