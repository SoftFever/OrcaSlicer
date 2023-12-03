#ifndef slic3r_SpiralVase_hpp_
#define slic3r_SpiralVase_hpp_

#include "../libslic3r.h"
#include "../GCodeReader.hpp"

namespace Slic3r {

class SpiralPoint
{
    public:
        SpiralPoint(float paramx, float paramy) : x(paramx), y(paramy) {}
    public:
        float x, y;
    
};

class SpiralVase {
    
public:
    SpiralVase(const PrintConfig &config) : m_config(config)
    {
        m_reader.z() = (float)m_config.z_offset;
        m_reader.apply_config(m_config);
        m_previous_layer = NULL;
        m_smooth_spiral = config.spiral_mode_smooth;
    };

    void 		enable(bool en) {
   		m_transition_layer = en && ! m_enabled;
    	m_enabled 		   = en;
    }

    std::string process_layer(const std::string &gcode, bool last_layer);
    
private:
    const PrintConfig  &m_config;
    GCodeReader 		m_reader;

    bool 				m_enabled = false;
    // First spiral vase layer. Layer height has to be ramped up from zero to the target layer height.
    bool 				m_transition_layer = false;
    // Whether to interpolate XY coordinates with the previous layer. Results in no seam at layer changes
    bool                m_smooth_spiral = false;
    std::vector<SpiralPoint> * m_previous_layer;
};



}

#endif // slic3r_SpiralVase_hpp_
