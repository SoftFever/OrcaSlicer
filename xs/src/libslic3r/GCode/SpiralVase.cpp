#include "SpiralVase.hpp"
#include <sstream>

namespace Slic3r {

std::string
_format_z(float z)
{
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(3)
       << z;
    return ss.str();
}

std::string
SpiralVase::process_layer(const std::string &gcode)
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
    if (!this->enable) {
        this->_reader.parse(gcode, {});
        return gcode;
    }
    
    // Get total XY length for this layer by summing all extrusion moves.
    float total_layer_length = 0;
    float layer_height = 0;
    float z;
    bool set_z = false;
    
    {
        GCodeReader r = this->_reader;  // clone
        r.parse(gcode, [&total_layer_length, &layer_height, &z, &set_z]
            (GCodeReader &, const GCodeReader::GCodeLine &line) {
            if (line.cmd == "G1") {
                if (line.extruding()) {
                    total_layer_length += line.dist_XY();
                } else if (line.has('Z')) {
                    layer_height += line.dist_Z();
                    if (!set_z) {
                        z = line.new_Z();
                        set_z = true;
                    }
                }
            }
        });
    }
    
    // Remove layer height from initial Z.
    z -= layer_height;
    
    std::string new_gcode;
    this->_reader.parse(gcode, [&new_gcode, &z, &layer_height, &total_layer_length]
        (GCodeReader &, GCodeReader::GCodeLine line) {
        if (line.cmd == "G1") {
            if (line.has('Z')) {
                // If this is the initial Z move of the layer, replace it with a
                // (redundant) move to the last Z of previous layer.
                line.set('Z', _format_z(z));
                new_gcode += line.raw + '\n';
                return;
            } else {
                float dist_XY = line.dist_XY();
                if (dist_XY > 0) {
                    // horizontal move
                    if (line.extruding()) {
                        z += dist_XY * layer_height / total_layer_length;
                        line.set('Z', _format_z(z));
                        new_gcode += line.raw + '\n';
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
        new_gcode += line.raw + '\n';
    });
    
    return new_gcode;
}

}
