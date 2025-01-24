// LayerTimeSmoothingProcessor.cpp
// OrcaSlicer
//
// Implementation of the Layer Time Smoothing class, responsible for containing layer times accross layers within an acceptable tolerance

#include "../GCode.hpp"
#include "LayerTimeSmoothingProcessor.hpp"
#include <sstream>
#include <iostream>
#include <cmath>
#include <vector>

namespace Slic3r {


LayerTimeSmoothingProcessor::LayerTimeSmoothingProcessor(GCode &gcodegen, const std::vector<unsigned int> &tools_used)
: m_gcodegen(gcodegen)
{
    // Constructor body used for tools initialisation
    // TODO: filter by tool option
    for (unsigned int tool : tools_used) {
    }
}

float LayerTimeSmoothingProcessor::extract_layer_time(const std::string& gcode) {
    // Regular expression to match the pattern ;LAYER_TIME_ESTIMATE:<float>
    const std::regex layer_time_pattern(R"(;LAYER_TIME_ESTIMATE:([0-9]*\.?[0-9]+))");
    std::smatch match;
    
    // Search for the pattern in the input gcode
    if (std::regex_search(gcode, match, layer_time_pattern)) {
        try{
            // TODO: add guard against too short layer times, i.e. 0 to avoid infinite speed slow downs...
            m_current_layer_time = std::stof(match[1].str());
        }catch (const std::exception& e){
            return m_current_layer_time;
        }
    }
    // Return the updated layer time or the last known good value if no match was found
    return m_current_layer_time;
}

// --------------------------------------------------------------
// calculateLayerTimeFactors
// --------------------------------------------------------------
/**
 * \brief  Given an array of layer times in seconds, returns an array of
 *         "speed factors" each in (0, 1], enforcing that consecutive
 *         layers' times differ by at most MAX_RATIO, using only slowdowns.
 *
 * \param[in]  layerTimes   Original times for each layer (index = layer #).
 * \return                  Vector of multipliers: factor[i] = original_time[i]
 *                                        / adjusted_time[i], always <= 1.0.
 *
 * \details
 *  - We use an "immediate local backtracking" approach:
 *    1) Start from the first layer and move forward.
 *    2) For each pair (i, i+1), if times are out of ratio bounds, we raise
 *       (slow down) the smaller one to bring the ratio to MAX_RATIO exactly.
 *    3) If we had to raise time[i], we step back one layer to re-check (i-1, i).
 *    4) Continue until we reach the last layer.  At that point, the entire
 *       sequence is guaranteed to have no consecutive ratio > MAX_RATIO.
 *  - The final speed multiplier for layer i is original_time[i] / adjusted_time[i].
 *    Because adjusted_time[i] >= original_time[i], this ratio is in (0, 1].
 *
 * \note  If layerTimes is empty or contains only one layer, we simply return
 *        a factors vector of the same size, filled with 1.0f (no change).
 */
std::vector<float> LayerTimeSmoothingProcessor::calculateLayerTimeFactors(const std::vector<float>& layerTimes)
{
    // Retrieve amount of layers to be processed
    const std::size_t n = layerTimes.size();
    if (n <= 2)
    {
        // If 2 or fewer layers, there's no "middle" layer to adjust.
        return std::vector<float>(n, 1.0f);
    }
    
    // Make a copy of the times to allow the algorithm to modify in place and iterate
    std::vector<float> adjustedTimes = layerTimes;

    // We apply ratio checks only from layer = 1 up to layer = (n - 2).
    // That means the pairs we consider are:
    //   (1,2), (2,3), ..., (n-3, n-2).
    // We *skip* (0,1) and (n-2, n-1).
    //
    // We'll implement "immediate local backtracking":
    //   - if we raise times[i], we step back to i-1 (but never go below i=1).

    // Start from the first "inner" layer
    std::size_t i = 1;

    // While (i+1) is at most (n-2)
    // so the largest pair we check is (n-3, n-2).
    while (i + 1 <= n - 2)
    {
        float tCurr = adjustedTimes[i];
        float tNext = adjustedTimes[i + 1];
        
        // Protect against division by zero (shouldn't happen with valid input).
        if (tCurr < EPSILON || tNext < EPSILON)
        {
            // Move on safely to the next layer without making any adjustments
            ++i;
            continue;
        }
        
        // Check the ratio in both directions
        if (tNext > tCurr * MAX_RATIO)
        {
            // tNext is too large relative to tCurr,
            // so we raise tCurr to (tNext / MAX_RATIO).
            // That ensures tNext / tCurr <= MAX_RATIO.
            const float newTCurr = tNext / MAX_RATIO;
            if (newTCurr > tCurr)
            {
                adjustedTimes[i] = newTCurr;

                // Backtrack to re-check (i-1, i) if i > 1.
                // We must not go below i=1, because layer 0 is excluded.
                if (i > 1)
                {
                    --i;
                    continue;
                }
            }
        }
        // If tCurr is more than 25% bigger than tNext, raise tNext
        else if (tCurr > tNext * MAX_RATIO)
        {
            // tCurr is too large relative to tNext,
            // so we raise tNext to (tCurr / MAX_RATIO).
            const float newTNext = tCurr / MAX_RATIO;
            if (newTNext > tNext)
            {
                adjustedTimes[i + 1] = newTNext;
                // We changed times[i+1], so the ratio with (i+1, i+2) might
                // need adjusting. We'll get there in the next loop.
            }
        }
        // Advance to the next pair
        ++i;
    }
    
    // Finally, compute the factors. factor[i] = original / adjusted.
    // This is <= 1.0 because adjustedTimes[i] >= layerTimes[i].
    std::vector<float> factors(n, 1.0f); // initialize vector with 1.0, i.e. no adjustments
    for (std::size_t idx = 0; idx < n; ++idx)
    {
        const float origT = layerTimes[idx];
        const float adjT  = adjustedTimes[idx];
        
        if (adjT > EPSILON)
        {
            factors[idx] = origT / adjT;
        }
        else
        {
            // If something is off (very small or zero), fallback to 1.0
            factors[idx] = 1.0f;
        }
    }
    
    return factors;
}


// A helper function to parse and scale an F-parameter in a G-code token.
// Example token: "F1800" -> multiply speed -> "F1350" (if factor=0.75).
// Returns true if 'token' was modified (it started with 'F' and contained a valid float),
// false otherwise.
bool LayerTimeSmoothingProcessor::scaleFeedrate(std::string& token, float factor)
{
    if (token.size() < 2) return false;  // Must at least be 'F' + one more character
    if (token[0] != 'F') return false;   // Must start with 'F'
    
    //The remainder after 'F' should be the numeric portion.
    const std::string numericPart = token.substr(1);
    
    try {
        // Parse as integer (any non-integer input will throw std::invalid_argument). This exception should never happen.
        int originalFeedrate = std::stoi(numericPart);
    
        // Scale by factor and convert to int
        int newFeedrate = static_cast<int>(std::lround(originalFeedrate * factor));
        
        // Ensure we don't end up with less than 20mm/sec feedrate, which is a reasonable print speed floor.
        // TODO: read this minimum value from the filament profile
        if (newFeedrate < 1200)
            newFeedrate = 1200; // minimal safe feedrate
        
        // Rebuild the token with the updated integer feedrate
        std::ostringstream oss;
        oss << 'F' << newFeedrate;  // e.g., "F900"
        token = oss.str();
        
        return true;
    }
    catch (...) {
        // If parsing fails, do nothing. This should never happen, just included for safety.
        printf("Exception!\n");
        return false;
    }
}

std::string LayerTimeSmoothingProcessor::process_layers(const std::vector<std::string>& collected_layers,
                                                        const std::vector<float>& layer_times)
{
    // 1) Calculate adjustment factors.
    //    factor[i] = original_time[i] / adjusted_time[i],
    //    always <= 1.0 as we only slow down layers.
    auto factors = calculateLayerTimeFactors(layer_times);
    
    // --- Debug output ---
    for (std::size_t i = 0; i < factors.size(); ++i){
        const float origTime  = layer_times[i];
        const float finalTime = (factors[i] > 0.0f) ? (origTime / factors[i]) : origTime;
        std::cout << "Layer " << i
        << ": Original Time = " << origTime
        << " s, Factor = " << factors[i]
        << " => Final Time = " << finalTime
        << " s\n";
    }
    // ------------------------------
    
    
    std::string result;
    result.reserve(collected_layers.size() * 80); // optional reserve to reduce allocations
    
    // 2) Process each layer’s G-code
    // "collected_layers[i]" contains all lines for layer i
    // in one string, separated by newlines.
    // Multiply any F-values in G1 lines by "factors[i]".
    for (std::size_t layerIndex = 0; layerIndex < collected_layers.size(); ++layerIndex)
    {
        // Safety check: if out-of-range, default factor = 1.0
        float factor = 1.0f;
        if (layerIndex < factors.size()) {
            factor = factors[layerIndex];
        }
        
        // Parse the G-code lines from the layer’s combined string.
        // Typically, each line is separated by '\n'.
        // So let's split on '\n' to handle them individually.
        std::istringstream layerStream(collected_layers[layerIndex]);
        std::string line;
        
        bool external_perimeter = false;
        
        while (std::getline(layerStream, line))
        {
            // Check if this line is a G1 command that might contain an F parameter.
            //   1) Split on spaces
            //   2) If we see "G1" among the tokens, attempt to scale any "F..." token that follows.
            
            std::istringstream iss(line);
            std::vector<std::string> tokens;
            {
                std::string tk;
                while (iss >> tk) {
                    tokens.push_back(tk);
                }
            }
            
            // Detect outer wall
            if (tokens.size() >= 1) {
                if (tokens[0].rfind(";TYPE:Outer", 0) == 0) { // Outer wall
                    external_perimeter = true;
                } else if (tokens[0].rfind(";TYPE:", 0) == 0) { // any other feature
                    external_perimeter = false;
                }
            }
            
            // Case 1: G1 F<number> (2 tokens)
            if (tokens.size() == 2)
            {
                // tokens[0] == "G1" && tokens[1] == "FNNNN"
                if (tokens[0] == "G1" && !external_perimeter) // dont amend the print speed if its an outer wall.
                {
                    bool feedrateAdjusted = scaleFeedrate(tokens[1], factor);
                    if (feedrateAdjusted)
                    {
                        std::ostringstream reassembled;
                        reassembled << tokens[0] << ' ' << tokens[1];
                        line = reassembled.str();
                    }
                }
            }
            // Case 2: G1 X<number> Y<number> F<number> (4 tokens)
            else if (tokens.size() == 4)
            {
                if (tokens[0] == "G1" && tokens[1].rfind("X", 0) == 0 && tokens[2].rfind("Y", 0) == 0 && tokens[3].rfind("F", 0) == 0)
                {
                    // We only want to scale the feedrate if the last token is F...
                    bool feedrateAdjusted = scaleFeedrate(tokens[3], factor);
                    
                    if (feedrateAdjusted)
                    {
                        // Reassemble the line with updated feedrate
                        std::ostringstream reassembled;
                        reassembled << tokens[0] << ' ' << tokens[1] << ' '
                                    << tokens[2] << ' ' << tokens[3];
                        line = reassembled.str();
                    }
                }
            }
            // Case 3: G1 X<number> Y<number> Z<number> F<number> (5 tokens)
            else if (tokens.size() == 5)
            {
                if (tokens[0] == "G1" && tokens[1].rfind("X", 0) == 0 && tokens[2].rfind("Y", 0) == 0 && tokens[3].rfind("Z", 0) == 0 && tokens[4].rfind("F", 0) == 0)
                {
                    // We only want to scale the feedrate if the last token is F...
                    bool feedrateAdjusted = scaleFeedrate(tokens[4], factor);
                    
                    if (feedrateAdjusted)
                    {
                        // Reassemble the line with updated feedrate
                        std::ostringstream reassembled;
                        reassembled << tokens[0] << ' ' << tokens[1] << ' '
                                    << tokens[2] << ' ' << tokens[3] << ' ' << tokens[4];
                        line = reassembled.str();
                    }
                }
            }
            
            // Output potentially modified line for final G-code.
            result += line + '\n';
        }
    }
    
    return result;
}

}

