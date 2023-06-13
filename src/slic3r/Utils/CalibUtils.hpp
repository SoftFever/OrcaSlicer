#pragma once
#include "libslic3r/Calib.hpp"
#include "../GUI/DeviceManager.hpp"
#include "../GUI/Jobs/PrintJob.hpp"

namespace Slic3r {

class ProgressIndicator;
class Preset;

namespace GUI {
class CalibInfo
{
public:
    Calib_Params                       params;
    Preset*                            printer_prest;
    Preset*                            filament_prest;
    Preset*                            print_prest;
    BedType                            bed_type;
    std::string                        dev_id;
    std::string                        select_ams;
    std::shared_ptr<ProgressIndicator> process_bar;
};


class CalibUtils
{
public:
    CalibUtils(){};
    static std::shared_ptr<PrintJob> print_job;
    static void calib_PA(const X1CCalibInfos& calib_infos, std::string& error_message);
    static void emit_get_PA_calib_results();
    static bool get_PA_calib_results(std::vector<PACalibResult> &pa_calib_results);
    static void emit_get_PA_calib_infos();
    static bool get_PA_calib_tab(std::vector<PACalibResult> &pa_calib_infos);
    static void set_PA_calib_result(const std::vector<PACalibResult>& pa_calib_values);
    static void select_PA_calib_result(const PACalibIndexInfo &pa_calib_info);
    static void delete_PA_calib_result(const PACalibIndexInfo &pa_calib_info);

    static void calib_flowrate_X1C(const X1CCalibInfos& calib_infos, std::string& error_message);
    static void emit_get_flow_ratio_calib_results();
    static bool get_flow_ratio_calib_results(std::vector<FlowRatioCalibResult> &flow_ratio_calib_results);
    static void calib_flowrate(int pass, const CalibInfo& calib_info, std::string& error_message);

    static void calib_generic_PA(const CalibInfo& calib_info, std::string &error_message);
    static void calib_temptue(const CalibInfo& calib_info, std::string& error_message);
    static void calib_max_vol_speed(const CalibInfo& calib_info, std::string& error_message);
    static void calib_VFA(const CalibInfo& calib_info, std::string& error_message);
    static void calib_retraction(const CalibInfo &calib_info, std::string &error_message);

private:
    static void process_and_store_3mf(Model* model, const DynamicPrintConfig& full_config, const Calib_Params& params, std::string& error_message);
    static void send_to_print(const std::string& dev_id, const std::string& select_ams, std::shared_ptr<ProgressIndicator> process_bar, BedType bed_type, std::string& error_message);
};

}
}