#ifndef slic3r_GCodeReader_hpp_
#define slic3r_GCodeReader_hpp_

#include "libslic3r.h"
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string>
#include "PrintConfig.hpp"

namespace Slic3r {

class GCodeReader {
public:    
    class GCodeLine {
    public:
        GCodeReader* reader;
        std::string raw;
        std::string cmd;
        std::string comment;
        std::map<char,std::string> args;
        
        GCodeLine(GCodeReader* _reader) : reader(_reader) {};
        
        bool  has(char arg) const { return this->args.count(arg) > 0; };
        float get_float(char arg) const { return float(atof(this->args.at(arg).c_str())); };
        float new_X() const { return this->has('X') ? float(atof(this->args.at('X').c_str())) : this->reader->X; };
        float new_Y() const { return this->has('Y') ? float(atof(this->args.at('Y').c_str())) : this->reader->Y; };
        float new_Z() const { return this->has('Z') ? float(atof(this->args.at('Z').c_str())) : this->reader->Z; };
        float new_E() const { return this->has('E') ? float(atof(this->args.at('E').c_str())) : this->reader->E; };
        float new_F() const { return this->has('F') ? float(atof(this->args.at('F').c_str())) : this->reader->F; };
        float dist_X() const { return this->new_X() - this->reader->X; };
        float dist_Y() const { return this->new_Y() - this->reader->Y; };
        float dist_Z() const { return this->new_Z() - this->reader->Z; };
        float dist_E() const { return this->new_E() - this->reader->E; };
        float dist_XY() const {
            float x = this->dist_X();
            float y = this->dist_Y();
            return sqrt(x*x + y*y);
        };
        bool extruding()  const { return this->cmd == "G1" && this->dist_E() > 0; };
        bool retracting() const { return this->cmd == "G1" && this->dist_E() < 0; };
        bool travel()     const { return this->cmd == "G1" && ! this->has('E'); };
        void set(char arg, std::string value);
    };
    typedef std::function<void(GCodeReader&, const GCodeLine&)> callback_t;
    
    float X, Y, Z, E, F;
    bool verbose;
    callback_t callback; 
    
    GCodeReader() : X(0), Y(0), Z(0), E(0), F(0), verbose(false), m_extrusion_axis('E') {};
    void apply_config(const GCodeConfig &config);
    void apply_config(const DynamicPrintConfig &config);
    void parse(const std::string &gcode, callback_t callback);
    void parse_line(std::string line, callback_t callback);
    void parse_file(const std::string &file, callback_t callback);
    
private:
    GCodeConfig m_config;
    char m_extrusion_axis;
};

} /* namespace Slic3r */

#endif /* slic3r_GCodeReader_hpp_ */
