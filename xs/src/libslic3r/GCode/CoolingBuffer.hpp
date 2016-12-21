#ifndef slic3r_CoolingBuffer_hpp_
#define slic3r_CoolingBuffer_hpp_

#include "libslic3r.h"
#include "GCode.hpp"
#include <map>
#include <string>

namespace Slic3r {

/*
A standalone G-code filter, to control cooling of the print.
The G-code is processed per layer. Once a layer is collected, fan start / stop commands are edited
and the print is modified to stretch over a minimum layer time.
*/

class CoolingBuffer {
    public:
    CoolingBuffer(GCode &gcodegen)
        : _gcodegen(&gcodegen), _elapsed_time(0.), _layer_id(0)
    {
        this->_min_print_speed = this->_gcodegen->config.min_print_speed * 60;
    };
    std::string append(const std::string &gcode, std::string obj_id, size_t layer_id, float print_z);
    std::string flush();
    GCode* gcodegen() { return this->_gcodegen; };
    
    private:
    GCode*                      _gcodegen;
    std::string                 _gcode;
    float                       _elapsed_time;
    size_t                      _layer_id;
    std::map<std::string,float> _last_z;
    float                       _min_print_speed;
};

}

#endif
