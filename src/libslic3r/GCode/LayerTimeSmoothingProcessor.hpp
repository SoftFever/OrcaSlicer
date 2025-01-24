//
//  LayerTimeSmoothingProcessor.hpp
//  OrcaSlicer
//

#ifndef LayerTimeSmoothingProcessor_h
#define LayerTimeSmoothingProcessor_h


#include <string>
#include <sstream>
#include <regex>
#include <memory>
#include <map>
#include <vector>

namespace Slic3r {

// Forward declaration of GCode class
class GCode;


class LayerTimeSmoothingProcessor {
public:

    LayerTimeSmoothingProcessor(GCode &gcodegen, const std::vector<unsigned int> &tools_used);
    float extract_layer_time(const std::string& gcode);
    std::string process_layers(const std::vector<std::string>& collected_layers, const std::vector<float>& layer_times);
    
private:
    GCode &m_gcodegen; // TODO: Probably unecessary. Remove if not needed in later code iterations
    
    float m_current_layer_time;

    constexpr static float MAX_RATIO = 1.20f; // Maximum layer time deviation between layers. TODO: Make this a user defined variable (in the filament profile?)
    
    // Helper function that calculates the layer time multiplication factors that will be used to multiply the G1 Fx statements with.
    std::vector<float> calculateLayerTimeFactors(const std::vector<float>& layerTimes);
    
    // Helper function to parse and scale an F-parameter in a G-code token.
    bool scaleFeedrate(std::string& token, float factor);
};


} // namespace Slic3r

#endif /* LayerTimeSmoothingProcessor_h */
