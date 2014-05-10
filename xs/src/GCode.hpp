#ifndef slic3r_GCode_hpp_
#define slic3r_GCode_hpp_

#include <myinit.h>
#include <string>

namespace Slic3r {

// draft for a binary representation of a G-code line

enum GCodeCmdType {
    gcctSyncMotion,
    gcctExtrude,
    gcctResetE,
    gcctSetTemp,
    gcctSetTempWait,
    gcctToolchange,
    gcctCustom
};

class GCodeCmd {
    public:
    GCodeCmdType type;
    float X, Y, Z, E, F;
    unsigned short T, S;
    std::string custom, comment;
    float xy_dist; // cache
    
    GCodeCmd(GCodeCmdType type)
        : type(type), X(0), Y(0), Z(0), E(0), F(0), T(-1), S(0), xy_dist(-1) {};
};

}

#endif
