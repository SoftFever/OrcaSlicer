#ifndef slic3r_GCodeWriter_hpp_
#define slic3r_GCodeWriter_hpp_

#include <myinit.h>
#include <string>
#include "Extruder.hpp"
#include "Point.hpp"
#include "PrintConfig.hpp"

namespace Slic3r {

class GCodeWriter {
    public:
    GCodeConfig config;
    std::map<unsigned int,Extruder> extruders;
    bool multiple_extruders;
    
    GCodeWriter()
        : multiple_extruders(false), _extrusion_axis("E"), _extruder(NULL),
            _last_acceleration(0), _last_fan_speed(0), _lifted(0)
        {};
    Extruder* extruder();
    std::string extrusion_axis() const;
    void apply_print_config(const PrintConfig &print_config);
    void set_extruders(const std::vector<unsigned int> &extruder_ids);
    std::string preamble();
    std::string postamble();
    std::string set_temperature(unsigned int temperature, bool wait = false, int tool = -1);
    std::string set_bed_temperature(unsigned int temperature, bool wait = false);
    std::string set_fan(unsigned int speed, bool dont_save = false);
    std::string set_acceleration(unsigned int acceleration);
    std::string reset_e(bool force = false);
    std::string update_progress(unsigned int num, unsigned int tot, bool allow_100 = false);
    bool need_toolchange(unsigned int extruder_id) const;
    std::string set_extruder(unsigned int extruder_id);
    std::string toolchange(unsigned int extruder_id);
    std::string set_speed(double F, const std::string &comment = std::string());
    std::string travel_to_xy(const Pointf &point, const std::string &comment = std::string());
    std::string travel_to_xyz(const Pointf3 &point, const std::string &comment = std::string());
    std::string travel_to_z(double z, const std::string &comment = std::string());
    bool will_move_z(double z) const;
    std::string extrude_to_xy(const Pointf &point, double dE, const std::string &comment = std::string());
    std::string extrude_to_xyz(const Pointf3 &point, double dE, const std::string &comment = std::string());
    std::string retract();
    std::string retract_for_toolchange();
    std::string unretract();
    std::string lift();
    std::string unlift();
    Pointf3 get_position() const;
    
    private:
    std::string _extrusion_axis;
    Extruder* _extruder;
    unsigned int _last_acceleration;
    unsigned int _last_fan_speed;
    double _lifted;
    Pointf3 _pos;
    
    std::string _travel_to_z(double z, const std::string &comment);
    std::string _retract(double length, double restart_extra, const std::string &comment);
};

}

#endif
