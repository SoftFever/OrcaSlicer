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
      m_AdaptivePAInterpolator(std::make_unique<AdaptivePAInterpolator>()),
      m_pa_change_pattern(R"(; PA_CHANGE:T(\d+) MM3MM:([0-9]*\.[0-9]+) ACCEL:(\d+) EXT:(\d+) RC:(\d+))"),
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
    double mm3mm_value = 0.0;
    unsigned int accel_value = 0;
    std::string pa_change_line;

    // Iterate through each line of the layer G-code
    while (std::getline(stream, line)) {
        // Check for PA_CHANGE pattern in the line
        // We will only find this pattern for extruders where adaptive PA is enabled.
        // If there is mixed extruders in the layer (i.e. with adaptive PA on and off
        // this will only update the extruders where the adaptive PA is enabled
        // as these are the only ones where the PA pattern is output
        // For a mixed extruder layer with both adaptive PA enabled and disabled when the new tool is selected
        // the PA for that material is set. As no tag below will be found for this extruder, the original PA is retained.
        if (line.find("; PA_CHANGE") == 0) { // prune lines quickly before running regex check as regex is more expensive to run
            if (std::regex_search(line, m_match, m_pa_change_pattern)) {
                int extruder_id = std::stoi(m_match[1].str());
                mm3mm_value = std::stod(m_match[2].str());
                accel_value = std::stod(m_match[3].str());
                //int isExternal = std::stoi(m_match[4].str());
                int roleChange = std::stoi(m_match[5].str());
                
                // Check if the extruder ID has changed
                bool extruder_changed = (extruder_id != m_last_extruder_id);
                m_last_extruder_id = extruder_id;
                
                // Save the PA_CHANGE line to output later after finding feedrate
                pa_change_line = line;
                
                // Look ahead for feedrate before any line containing both G and E commands
                std::streampos current_pos = stream.tellg();
                std::string next_line;
                double temp_feed_rate = 0;
                bool extrude_move_found = false;
                
                // Carry on searching on the layer gcode lines to find the print speed
                // If a G1 Fxxxx pattern is found, the new speed is identified
                // Carry on searching for feedrates to find the maximum print speed
                // until a feature change pattern or a wipe command is detected
                while (std::getline(stream, next_line)) {
                    if (next_line.find("G1 F") == 0) { // prune lines quickly before running pattern matching
                        std::size_t pos = next_line.find('F');
                        if (pos != std::string::npos) {
                            double feedrate = std::stod(next_line.substr(pos + 1)) / 60.0; // Convert from mm/min to mm/s
                            if (temp_feed_rate < feedrate) {
                                temp_feed_rate = feedrate;
                            }
                        }
                        continue;
                    }
                    
                    // Found an extrude move
                    if ((!extrude_move_found) && next_line.find("G1 ") == 0 &&
                        next_line.find('X') != std::string::npos &&
                        next_line.find('Y') != std::string::npos &&
                        next_line.find('E') != std::string::npos) {
                        // Pattern matched, break the loop
                        extrude_move_found = true;
                        continue;
                    }
                    
                    // Search for a travel move after we've found at least one speed and at least one extrude move
                    // Most likely we now need to stop searching for speeds as we're done with this island
                    if (next_line.find("G1 ") == 0 &&
                        next_line.find('X') != std::string::npos &&
                        next_line.find('Y') != std::string::npos &&
                        next_line.find('E') == std::string::npos &&
                        (temp_feed_rate > 0) &&
                        extrude_move_found) {
                        // First travel move after extrude move found. Stop searching
                        break;
                    }
                    
                    // Check for WIPE command
                    // If we have a wipe command, usually the wipe speed is different (larger) than the max print speed
                    // for that feature. So stop searching if a wipe command is found so as not to overwrite the
                    // speed used for PA calculation by the Wipe speed.
                    if (next_line.find("WIPE") != std::string::npos) {
                        break; // Stop searching if wipe command is found
                    }
                    
                    // Check for PA_CHANGE pattern and RC value
                    // If RC = 1, it means we have a role change so stop trying to find the max speed for the feature.
                    // Check for PA_CHANGE pattern and RC value
                    // This is possibly redundant as a new feature would always have a travel move preceding it
                    // but check anyway. However check last so to not invoke it without reason...
                    if (next_line.find("; PA_CHANGE") == 0) { // prune lines quickly before running pattern matching
                        std::size_t rc_pos = next_line.rfind("RC:");
                        if (rc_pos != std::string::npos) {
                            int rc_value = std::stoi(next_line.substr(rc_pos + 3));
                            if (rc_value == 1) {
                                break; // Role change found, stop searching
                            }
                        }
                    }
                }
                
                // Found new feedrate
                if (temp_feed_rate > 0) {
                    m_last_feedrate = temp_feed_rate;
                }
                
                // Restore stream position
                stream.clear();
                stream.seekg(current_pos);
                
                // Calculate the predicted PA using the current or last known feedrate
                std::string pa_calibration_values = m_config.adaptive_pressure_advance_model.get_at(m_last_extruder_id);
                int pchip_return_flag = m_AdaptivePAInterpolator->parseAndSetData(pa_calibration_values);
                
                double predicted_pa;
                if (pchip_return_flag == -1) {
                    // Model failed, use fallback value from m_config
                    predicted_pa = m_config.pressure_advance.get_at(m_last_extruder_id);
                    output << "; PchipInterpolator setup failed, using fallback pressure advance value\n";
                } else {
                    // Model succeeded, calculate predicted pressure advance
                    predicted_pa = (*m_AdaptivePAInterpolator)(mm3mm_value * m_last_feedrate, accel_value);
                    if (predicted_pa < 0) {
                        predicted_pa = m_config.pressure_advance.get_at(m_last_extruder_id);
                        output << "; PchipInterpolator interpolation failed, using fallback pressure advance value\n";
                    }
                }
                
                // Output the PA_CHANGE line and set the pressure advance immediately after
                // TODO: reduce debug logging when prepping for release (remove the first three outputs)
                output << "; APA Print Speed: " << std::to_string(m_last_feedrate) << "\n";
                output << "; APA Model flow speed: " << std::to_string(mm3mm_value * m_last_feedrate) << "\n";
                output << "; APA Prev PA: " << std::to_string(m_last_predicted_pa) << " New PA: " << std::to_string(predicted_pa) << "\n";
                output << pa_change_line << '\n';
                if (extruder_changed || std::fabs(predicted_pa - m_last_predicted_pa) > EPSILON) {
                    output << m_gcodegen.writer().set_pressure_advance(predicted_pa); // Use m_writer to set pressure advance
                    m_last_predicted_pa = predicted_pa; // Update the last predicted PA value
                }
                
            }
        }else {
            // Output the current line if it isn't part of PA_CHANGE process
            output << line << '\n';
        }
    }

    return output.str();
}

} // namespace Slic3r
