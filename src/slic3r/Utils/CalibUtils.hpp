#pragma once
#include "libslic3r/Calib.hpp"

namespace Slic3r {

class ProgressIndicator;

namespace GUI {

class CalibUtils
{
public:
    CalibUtils(){};
    static void calib_flowrate(int pass, std::string dev_id, std::string select_ams, std::shared_ptr<ProgressIndicator> process_bar);
    static void calib_temptue(const Calib_Params &params, std::string dev_id, std::string select_ams, std::shared_ptr<ProgressIndicator> process_bar);
    static void calib_max_vol_speed(const Calib_Params &params, std::string dev_id, std::string select_ams, std::shared_ptr<ProgressIndicator> process_bar);
    static void calib_VFA(const Calib_Params &params);

private:
    static void process_and_store_3mf(Model *model, const DynamicPrintConfig &full_config, const Calib_Params &params);
    static void send_to_print(const std::string &dev_id, const std::string &select_ams, std::shared_ptr<ProgressIndicator> process_bar);
};

}
}