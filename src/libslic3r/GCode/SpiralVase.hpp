#ifndef slic3r_SpiralVase_hpp_
#define slic3r_SpiralVase_hpp_

#include "../libslic3r.h"
#include "../GCodeReader.hpp"

namespace Slic3r {

class SpiralVase {
public:
    bool enable = false;
    
    SpiralVase(const PrintConfig &config) : m_config(&config)
    {
        m_reader.z() = (float)m_config->z_offset;
        m_reader.apply_config(*m_config);
    };
    std::string process_layer(const std::string &gcode);
    
private:
    const PrintConfig  *m_config;
    GCodeReader 		m_reader;
};

}

#endif
