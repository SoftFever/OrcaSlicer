#pragma once

#include <string>
#include "Point.hpp"
namespace Slic3r {

class GCode;

enum class CalibMode : int {
    Calib_None = 0,
    Calib_PA_Line,
    Calib_PA_Tower,
    Calib_Flow_Rate,
    Calib_Temp_Tower,
    Calib_Vol_speed_Tower,
    Calib_VFA_Tower

};
struct Calib_Params
{
    Calib_Params() : mode(CalibMode::Calib_None){}
    double start, end, step;
    bool print_numbers;
    CalibMode mode;
};
} // namespace Slic3r
