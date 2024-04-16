#pragma once
#include "libslic3r/calib.hpp"
#include "../GUI/DeviceManager.hpp"
#include "../GUI/Jobs/PrintJob.hpp"
#include "slic3r/GUI/Jobs/Worker.hpp"

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
    static std::unique_ptr<Worker> print_worker;

    static CalibMode get_calib_mode_by_name(const std::string name, int &cali_stage);

    static void calib_PA(const X1CCalibInfos& calib_infos, int mode, wxString& error_message);
    
    static void emit_get_PA_calib_results(float nozzle_diameter);
    static bool get_PA_calib_results(std::vector<PACalibResult> &pa_calib_results);
    
    static void emit_get_PA_calib_infos(float nozzle_diameter);
    static bool get_PA_calib_tab(std::vector<PACalibResult> &pa_calib_infos);

    static void emit_get_PA_calib_info(float nozzle_diameter, const std::string &filament_id);
    static bool get_PA_calib_info(PACalibResult &pa_calib_info);

    static void set_PA_calib_result(const std::vector<PACalibResult>& pa_calib_values, bool is_auto_cali);
    static void select_PA_calib_result(const PACalibIndexInfo &pa_calib_info);
    static void delete_PA_calib_result(const PACalibIndexInfo &pa_calib_info);

    static void calib_flowrate_X1C(const X1CCalibInfos& calib_infos, std::string& error_message);
    static void emit_get_flow_ratio_calib_results(float nozzle_diameter);
    static bool get_flow_ratio_calib_results(std::vector<FlowRatioCalibResult> &flow_ratio_calib_results);
    static void calib_flowrate(int pass, const CalibInfo &calib_info, wxString &error_message);

    static void calib_pa_pattern(const CalibInfo &calib_info, Model &model);

    static void calib_generic_PA(const CalibInfo &calib_info, wxString &error_message);
    static void calib_temptue(const CalibInfo &calib_info, wxString &error_message);
    static void calib_max_vol_speed(const CalibInfo &calib_info, wxString &error_message);
    static void calib_VFA(const CalibInfo &calib_info, wxString &error_message);
    static void calib_retraction(const CalibInfo &calib_info, wxString &error_message);

    //help function
    static int get_selected_calib_idx(const std::vector<PACalibResult> &pa_calib_values, int cali_idx);
    static bool get_pa_k_n_value_by_cali_idx(const MachineObject* obj, int cali_idx, float& out_k, float& out_n);

    static bool validate_input_name(wxString name);
    static bool validate_input_k_value(wxString k_text, float* output_value);
    static bool validate_input_flow_ratio(wxString flow_ratio, float* output_value);

private:
    static void process_and_store_3mf(Model* model, const DynamicPrintConfig& full_config, const Calib_Params& params, wxString& error_message);
    static void send_to_print(const CalibInfo &calib_info, wxString& error_message, int flow_ratio_mode = 0); // 0: none  1: coarse  2: fine
};

}
}
