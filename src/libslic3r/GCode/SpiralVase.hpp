#ifndef slic3r_SpiralVase_hpp_
#define slic3r_SpiralVase_hpp_

#include "../libslic3r.h"
#include "../GCodeReader.hpp"

namespace Slic3r {

class SpiralVase {
public:
    SpiralVase(const PrintConfig &config) : m_config(&config)
    {
        m_reader.z() = (float)m_config->z_offset;
        m_reader.apply_config(*m_config);
    };

    void 		enable(bool en) {
   		m_transition_layer = en && ! m_enabled;
    	m_enabled 		   = en;
    }

    std::string process_layer(const std::string &gcode);
    
private:
    const PrintConfig  *m_config;
    GCodeReader 		m_reader;

    bool 				m_enabled = false;
    // First spiral vase layer. Layer height has to be ramped up from zero to the target layer height.
    bool 				m_transition_layer = false;
};

}

#endif
