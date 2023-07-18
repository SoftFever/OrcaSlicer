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
    Calib_VFA_Tower,
    Calib_Retraction_tower
};

enum class CalibState {
    Start = 0,
    Preset,
    Calibration,
    CoarseSave,
    FineCalibration,
    Save,
    Finish
};

struct Calib_Params
{
    Calib_Params() : mode(CalibMode::Calib_None){}
    double start, end, step;
    bool print_numbers;
    CalibMode mode;
};

class X1CCalibInfos
{
public:
    struct X1CCalibInfo
    {
        int         tray_id;
        int         bed_temp;
        int         nozzle_temp;
        float       nozzle_diameter;
        std::string filament_id;
        std::string setting_id;
        float       max_volumetric_speed;
        float       flow_rate = 0.98f; // for flow ratio
    };

    std::vector<X1CCalibInfo> calib_datas;
};

class CaliPresetInfo
{
public:
    int         tray_id;
    float       nozzle_diameter;
    std::string filament_id;
    std::string setting_id;
    std::string name;

    CaliPresetInfo &operator=(const CaliPresetInfo &other)
    {
        this->tray_id         = other.tray_id;
        this->nozzle_diameter = other.nozzle_diameter;
        this->filament_id     = other.filament_id;
        this->setting_id      = other.setting_id;
        this->name            = other.name;
        return *this;
    }
};

struct PrinterCaliInfo
{
    std::string                 dev_id;
    float                       cache_flow_ratio;
    std::vector<CaliPresetInfo> selected_presets;
};

class PACalibResult
{
public:
    enum CalibResult {
        CALI_RESULT_SUCCESS = 0,
        CALI_RESULT_PROBLEM = 1,
        CALI_RESULT_FAILED  = 2,
    };
    int         tray_id;
    int         cali_idx = -1;
    float       nozzle_diameter;
    std::string filament_id;
    std::string setting_id;
    std::string name;
    float       k_value = 0.0;
    float       n_coef = 0.0;
    int         confidence = -1; // 0: success  1: uncertain  2: failed
};

struct PACalibIndexInfo
{
    int         tray_id;
    int         cali_idx;
    float       nozzle_diameter;
    std::string filament_id;
};

class FlowRatioCalibResult
{
public:
    int         tray_id;
    float       nozzle_diameter;
    std::string filament_id;
    std::string setting_id;
    float       flow_ratio;
    int         confidence; // 0: success  1: uncertain  2: failed
};

class calib_pressure_advance
{
public:
    calib_pressure_advance(GCode *gcodegen);
    ~calib_pressure_advance() {}

    std::string generate_test(double start_pa = 0, double step_pa = 0.002, int count = 50);
    void        set_speed(double fast = 100.0, double slow = 20.0)
    {
        m_slow_speed = slow;
        m_fast_speed = fast;
    }
    double &line_width() { return m_line_width; };
    bool &  draw_numbers() { return m_draw_numbers; }

private:
    std::string move_to(Vec2d pt);
    std::string print_pa_lines(double start_x, double start_y, double start_pa, double step_pa, int num);
    std::string draw_digit(double startx, double starty, char c);
    std::string draw_number(double startx, double starty, double value);

private:
    GCode *mp_gcodegen;
    double m_length_short, m_length_long;
    double m_space_y;
    double m_slow_speed, m_fast_speed;
    double m_line_width;
    bool   m_draw_numbers;
};

} // namespace Slic3r
