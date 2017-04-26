#ifndef slic3r_GCodeTimeEstimator_hpp_
#define slic3r_GCodeTimeEstimator_hpp_

#include "libslic3r.h"
#include "GCodeReader.hpp"

namespace Slic3r {

class GCodeTimeEstimator : public GCodeReader {
    public:
    float time = 0;  // in seconds
    
    void parse(const std::string &gcode);
    void parse_file(const std::string &file);
    
    protected:
    float acceleration = 9000;
    void _parser(GCodeReader&, const GCodeReader::GCodeLine &line);
    static float _accelerated_move(double length, double v, double acceleration);
};

} /* namespace Slic3r */

#endif /* slic3r_GCodeTimeEstimator_hpp_ */
