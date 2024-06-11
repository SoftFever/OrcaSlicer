// AdaptivePAProcessor.cpp
// OrcaSlicer
//
// Implementation of the AdaptivePAProcessor class, responsible for processing G-code layers with adaptive pressure advance.

#include "../GCode.hpp"
#include "AdaptivePAProcessor.hpp"
#include <sstream>
#include <iostream>
#include <cmath>

namespace Slic3r {

/**
 * @brief Constructor for AdaptivePAProcessor.
 *
 * This constructor initializes the AdaptivePAProcessor with a reference to a GCode object.
 * It also initializes the configuration reference, pressure advance interpolation object,
 * and regular expression patterns used for processing the G-code.
 *
 * @param gcodegen A reference to the GCode object that generates the G-code.
 */
AdaptivePAProcessor::AdaptivePAProcessor(GCode &gcodegen)
    : m_gcodegen(gcodegen), m_config(gcodegen.config()), m_last_predicted_pa(0.0), m_last_feedrate(0.0), m_last_extruder_id(-1),
      m_pchipInterpolator(std::make_unique<PchipInterpolator>()),
      m_pa_change_pattern(R"(; PA_CHANGE:T(\d+) MM3MM:([0-9]*\.[0-9]+) ACCEL:(\d+))"),
      m_g1_f_pattern(R"(G1 F([0-9]+))")
{
    // Constructor body can be used for further initialization if necessary
}

/**
 * @brief Processes a layer of G-code and applies adaptive pressure advance.
 *
 * This method processes the G-code for a single layer, identifying the appropriate
 * pressure advance settings and applying them based on the current state and configurations.
 *
 * @param gcode A string containing the G-code for the layer.
 * @return A string containing the processed G-code with adaptive pressure advance applied.
 */
std::string AdaptivePAProcessor::process_layer(std::string &&gcode) {
    std::istringstream stream(gcode);
    std::string line;
    std::ostringstream output;
    int current_extruder_id = m_last_extruder_id; // Initialize with the last known extruder ID
    double mm3mm_value = 0.0;
    unsigned int accel_value = 0;
    std::string pa_change_line;

    // Iterate through each line of the G-code
    while (std::getline(stream, line)) {
        // Check for PA_CHANGE pattern in the line
        // We will only find this pattern for extruders where adaptive PA is enabled.
        // If there is mixed extruders in the layer (i.e. with adaptive PA on and off
        // this will only update the extruders where the adaptive PA is enabled
        // as these are the only ones where the PA pattern is output
        // For a mixed extruder layer with both adaptive PA enabled and disabled when the new tool is selected
        // the PA for that material is set. As no tag below will be found for this extruder, the original PA is retained.
        if (std::regex_search(line, m_match, m_pa_change_pattern)) {
            int extruder_id = std::stoi(m_match[1].str());
            mm3mm_value = std::stod(m_match[2].str());
            accel_value = std::stod(m_match[3].str());

            // Check if the extruder ID has changed
            bool extruder_changed = (extruder_id != m_last_extruder_id);
            m_last_extruder_id = extruder_id;

            // Save the PA_CHANGE line to output later after finding feedrate
            pa_change_line = line;

            // Look ahead for feedrate before any line containing both G and E commands
            std::streampos current_pos = stream.tellg();
            std::string next_line;
            bool feedrate_found = false;

            // Carry on searching on the layer gcode lines to find the print speed
            // If a G1 Fxxxx pattern is found, the new speed is identified
            // If any other Gx [...] Ex pattern (print move) is found before finding the feedrate, this means that the old one should
            // be used, so stop searching.
            while (std::getline(stream, next_line)) {
                if (std::regex_search(next_line, m_match, m_g1_f_pattern)) {
                    m_last_feedrate = std::stod(m_match[1].str()) / 60.0; // Convert from mm/min to mm/s
                    feedrate_found = true;
                    break;
                }
                if (next_line.find("G") != std::string::npos && next_line.find("E") != std::string::npos) {
                    break; // Stop searching if a line with both G and E is found
                }
            }

            // Restore stream position
            stream.clear();
            stream.seekg(current_pos);

            // Calculate the predicted PA using the current or last known feedrate
            std::string pa_calibration_values = m_config.adaptive_pressure_advance_model.get_at(m_last_extruder_id);
            int pchip_return_flag = m_pchipInterpolator->parseAndSetData(pa_calibration_values);

            double predicted_pa;
            if (pchip_return_flag == -1) {
                // Model failed, use fallback value from m_config
                predicted_pa = m_config.pressure_advance.get_at(m_last_extruder_id);
                output << "; PchipInterpolator setup failed, using fallback pressure advance value\n";
            } else {
                // Model succeeded, calculate predicted pressure advance
                predicted_pa = (*m_pchipInterpolator)(mm3mm_value * m_last_feedrate,accel_value);
                if (predicted_pa<0){
                    predicted_pa = m_config.pressure_advance.get_at(m_last_extruder_id);
                    output << "; PchipInterpolator interpolation failed, using fallback pressure advance value\n";
                }
            }

            // Output the PA_CHANGE line and set the pressure advance immediately after
            output << pa_change_line << '\n';
            output << "; Prev PA: " << std::to_string(m_last_predicted_pa) << "New PA: " << std::to_string(predicted_pa)<<"\n";
            output << "; Model flow speed: " << std::to_string(mm3mm_value * m_last_feedrate) << "\n";
            if (extruder_changed || std::fabs(predicted_pa - m_last_predicted_pa) > EPSILON) {
                output << m_gcodegen.writer().set_pressure_advance(predicted_pa); // Use m_writer to set pressure advance
                m_last_predicted_pa = predicted_pa; // Update the last predicted PA value
            }

        } else {
            // Output the current line if it isn't part of PA_CHANGE or feedrate search process
            output << line << '\n';
        }
    }

    return output.str();
}

} // namespace Slic3r
