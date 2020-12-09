#include "SpiralVase.hpp"
#include "GCode.hpp"
#include <sstream>

namespace Slic3r {

std::string SpiralVase::process_layer(const std::string &gcode)
{
    /*  This post-processor relies on several assumptions:
        - all layers are processed through it, including those that are not supposed
          to be transformed, in order to update the reader with the XY positions
        - each call to this method includes a full layer, with a single Z move
          at the beginning
        - each layer is composed by suitable geometry (i.e. a single complete loop)
        - loops were not clipped before calling this method  */
    
    // If we're not going to modify G-code, just feed it to the reader
    // in order to update positions.
    if (! m_enabled) {
        m_reader.parse_buffer(gcode);
        return gcode;
    }
    
    // Get total XY length for this layer by summing all extrusion moves.
    float total_layer_length = 0;
    float layer_height = 0;
    float z = 0.f;
    
    {
        //FIXME Performance warning: This copies the GCodeConfig of the reader.
        GCodeReader r = m_reader;  // clone
        bool set_z = false;
        r.parse_buffer(gcode, [&total_layer_length, &layer_height, &z, &set_z]
            (GCodeReader &reader, const GCodeReader::GCodeLine &line) {
            if (line.cmd_is("G1")) {
                if (line.extruding(reader)) {
                    total_layer_length += line.dist_XY(reader);
                } else if (line.has(Z)) {
                    layer_height += line.dist_Z(reader);
                    if (!set_z) {
                        z = line.new_Z(reader);
                        set_z = true;
                    }
                }
            }
        });
    }
    
    // Remove layer height from initial Z.
    z -= layer_height;
    
    std::string new_gcode;
    //FIXME Tapering of the transition layer only works reliably with relative extruder distances.
    // For absolute extruder distances it will be switched off.
    // Tapering the absolute extruder distances requires to process every extrusion value after the first transition
    // layer.
    bool  transition = m_transition_layer && m_config->use_relative_e_distances.value;
    float layer_height_factor = layer_height / total_layer_length;
    float len = 0.f;
    m_reader.parse_buffer(gcode, [&new_gcode, &z, total_layer_length, layer_height_factor, transition, &len]
        (GCodeReader &reader, GCodeReader::GCodeLine line) {
        if (line.cmd_is("G1")) {
            if (line.has_z()) {
                // If this is the initial Z move of the layer, replace it with a
                // (redundant) move to the last Z of previous layer.
                line.set(reader, Z, z);
                new_gcode += line.raw() + '\n';
                return;
            } else {
                float dist_XY = line.dist_XY(reader);
                if (dist_XY > 0) {
                    // horizontal move
                    if (line.extruding(reader)) {
                        len += dist_XY;
                        line.set(reader, Z, z + len * layer_height_factor);
                        if (transition && line.has(E))
                            // Transition layer, modulate the amount of extrusion from zero to the final value.
                            line.set(reader, E, line.value(E) * len / total_layer_length);
                        new_gcode += line.raw() + '\n';
                    }
                    return;
                
                    /*  Skip travel moves: the move to first perimeter point will
                        cause a visible seam when loops are not aligned in XY; by skipping
                        it we blend the first loop move in the XY plane (although the smoothness
                        of such blend depend on how long the first segment is; maybe we should
                        enforce some minimum length?).  */
                }
            }
        }
        new_gcode += line.raw() + '\n';
    });
    
    return new_gcode;
}

}
