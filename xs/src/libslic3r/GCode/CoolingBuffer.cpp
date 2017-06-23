#include "../GCode.hpp"
#include "CoolingBuffer.hpp"
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <iostream>

namespace Slic3r {

void apply_speed_factor(std::string &line, float speed_factor, float min_print_speed)
{
    // find pos of F
    size_t pos = line.find_first_of('F');
    size_t last_pos = line.find_first_of(' ', pos+1);
    
    // extract current speed
    float speed;
    {
        std::istringstream iss(line.substr(pos+1, last_pos));
        iss >> speed;
    }
    
    // change speed
    speed *= speed_factor;
    speed = std::max(speed, min_print_speed);
    
    // replace speed in string
    {
        std::ostringstream oss;
        oss << speed;
        line.replace(pos+1, last_pos-pos, oss.str());
    }
}

std::string CoolingBuffer::process_layer(const std::string &gcode_src, size_t layer_id)
{
    const FullPrintConfig &config = m_gcodegen.config();

    std::string gcode               = gcode_src;
    int         fan_speed           = config.fan_always_on.values.front() ? config.min_fan_speed.values.front() : 0;
    float       speed_factor        = 1.0;
    bool        slowdown_external   = true;

    const std::vector<ElapsedTime> &elapsed_times = m_gcodegen.writer().elapsed_times();
    ElapsedTime elapsed_time;
    for (const ElapsedTime &et : elapsed_times)
        elapsed_time += et;

    if (config.cooling.values.front()) {
        #ifdef SLIC3R_DEBUG
        printf("Layer %zu estimated printing time: %f seconds\n", layer_id, elapsed_time.total);
        #endif
        if (elapsed_time.total < (float)config.slowdown_below_layer_time.values.front()) {
            // Layer time very short. Enable the fan to a full throttle and slow down the print
            // (stretch the layer print time to slowdown_below_layer_time).
            fan_speed = config.max_fan_speed.values.front();
            
            // We are not altering speed of bridges.
            float time_to_stretch = elapsed_time.stretchable();
            float target_time = (float)config.slowdown_below_layer_time.values.front() - elapsed_time.non_stretchable();
            
            // If we spend most of our time on external perimeters include them in the slowdown,
            // otherwise only alter other extrusions.
            if (elapsed_time.external_perimeters < 0.5f * time_to_stretch) {
                time_to_stretch -= elapsed_time.external_perimeters;
                target_time     -= elapsed_time.external_perimeters;
                slowdown_external = false;
            }
            
            speed_factor = time_to_stretch / target_time;
        } else if (elapsed_time.total < (float)config.fan_below_layer_time.values.front()) {
            // Layer time quite short. Enable the fan proportionally according to the current layer time.
            fan_speed = config.max_fan_speed.values.front()
                - (config.max_fan_speed.values.front() - config.min_fan_speed.values.front())
                * (elapsed_time.total - (float)config.slowdown_below_layer_time.values.front())
                / (config.fan_below_layer_time.values.front() - config.slowdown_below_layer_time.values.front());
        }
        
        #ifdef SLIC3R_DEBUG
        printf("  fan = %d%%, speed = %f%%\n", fan_speed, speed_factor * 100);
        #endif
        
        if (speed_factor < 1.0) {
            // Adjust feed rate of G1 commands marked with an _EXTRUDE_SET_SPEED
            // as long as they are not _WIPE moves (they cannot if they are _EXTRUDE_SET_SPEED)
            // and they are not preceded directly by _BRIDGE_FAN_START (do not adjust bridging speed).
            std::string new_gcode;
            std::istringstream ss(gcode);
            std::string line;
            bool  bridge_fan_start = false;
            float min_print_speed  = float(config.min_print_speed.values.front() * 60.);
            while (std::getline(ss, line)) {
                if (boost::starts_with(line, "G1")
                    && boost::contains(line, ";_EXTRUDE_SET_SPEED")
                    && !boost::contains(line, ";_WIPE")
                    && !bridge_fan_start
                    && (slowdown_external || !boost::contains(line, ";_EXTERNAL_PERIMETER"))) {
                    apply_speed_factor(line, speed_factor, min_print_speed);
                    boost::replace_first(line, ";_EXTRUDE_SET_SPEED", "");
                }
                bridge_fan_start = boost::starts_with(line, ";_BRIDGE_FAN_START");
                new_gcode += line + '\n';
            }
            gcode = new_gcode;
        }
    }
    if (layer_id < config.disable_fan_first_layers.values.front())
        fan_speed = 0;
    
    gcode = m_gcodegen.writer().set_fan(fan_speed) + gcode;
    
    // bridge fan speed
    if (!config.cooling.values.front() || config.bridge_fan_speed.values.front() == 0 || layer_id < config.disable_fan_first_layers.values.front()) {
        boost::replace_all(gcode, ";_BRIDGE_FAN_START", "");
        boost::replace_all(gcode, ";_BRIDGE_FAN_END", "");
    } else {
        boost::replace_all(gcode, ";_BRIDGE_FAN_START", m_gcodegen.writer().set_fan(config.bridge_fan_speed.values.front(), true));
        boost::replace_all(gcode, ";_BRIDGE_FAN_END",   m_gcodegen.writer().set_fan(fan_speed, true));
    }
    boost::replace_all(gcode, ";_WIPE", "");
    boost::replace_all(gcode, ";_EXTRUDE_SET_SPEED", "");
    boost::replace_all(gcode, ";_EXTERNAL_PERIMETER", "");
    
    m_object_ids_visited.clear();
    m_gcodegen.writer().reset_elapsed_times();
    return gcode;
}

}
