#include "libslic3r/libslic3r.h"
#include "DeviceManager.hpp"
#include "libslic3r/Time.hpp"
#include "libslic3r/Thread.hpp"
#include "slic3r/Utils/ColorSpaceConvert.hpp"

#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "Plater.hpp"
#include "GUI_App.hpp"
#include <thread>
#include <mutex>
#include <codecvt>
#include <boost/foreach.hpp>
#include <boost/typeof/typeof.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>


namespace pt = boost::property_tree;

const int PRINTING_STAGE_COUNT = 20;
std::string PRINTING_STAGE_STR[PRINTING_STAGE_COUNT] = {
    "printing",
    "bed_leveling",
    "heatbed_preheating",
    "xy_mech_mode_sweep",
    "change_material",
    "m400_pause",
    "filament_runout_pause",
    "hotend_heating",
    "extrude_compensation_scan",
    "bed_scan",
    "first_layer_scan",
    "be_surface_typt_idetification",
    "scanner_extrinsic_para_cali",
    "toohead_homing",
    "nozzle_tip_cleaning",
    "extruder_temp_protect_cali",
    "user_pause",
    "toolhead_shell_off_pause",
    "scanner_laser_para_cali",
    "extruder_absolute_flow_cali"
    };





wxString get_stage_string(int stage)
{
    switch(stage) {
    case 0:
        //return _L("Printing");
        return "";
    case 1:
        return _L("Auto bed leveling");
    case 2:
        return _L("Heatbed preheating");
    case 3:
        return _L("Sweeping XY mech mode");
    case 4:
        return _L("Changing filament");
    case 5:
        return _L("M400 pause");
    case 6:
        return _L("Paused due to filament runout");
    case 7:
        return _L("Heating hotend");
    case 8:
        return _L("Calibrating extrusion");
    case 9:
        return _L("Scanning bed surface");
    case 10:
        return _L("Inspecting first layer");
    case 11:
        return _L("Identifying build plate type");
    case 12:
        return _L("Calibrating Micro Lidar");
    case 13:
        return _L("Homing toolhead");
    case 14:
        return _L("Cleaning nozzle tip");
    case 15:
        return _L("Checking extruder temperature");
    case 16:
        return _L("Printing was paused by the user");
    case 17:
        return _L("Pause of front cover falling");
    case 18:
        return _L("Calibrating the micro lida");
    case 19:
        return _L("Calibrating extrusion flow");
    case 20:
        return _L("Paused due to nozzle temperature malfunction");
    case 21:
        return _L("Paused due to heat bed temperature malfunction");
    default:
        ;
    }
    return "";
}

namespace Slic3r {

/* Common Functions */
void split_string(std::string s, std::vector<std::string>& v) {

    std::string t = "";
    for (int i = 0; i < s.length(); ++i) {
        if (s[i] == ',') {
            v.push_back(t);
            t = "";
        }
        else {
            t.push_back(s[i]);
        }
    }
    v.push_back(t);
}

void AmsTray::update_color_from_str(std::string color)
{
    if (color.empty()) return;

    if (this->color.compare(color) == 0)
        return;

    wx_color = "#" + wxString::FromUTF8(color);
    this->color = color;
}

wxColour AmsTray::get_color()
{
    return AmsTray::decode_color(color);
}

bool AmsTray::is_tray_info_ready()
{
    if (color.empty())
        return false;
    if (type.empty())
        return false;
    //if (setting_id.empty())
        //return false;
    return true;
}

bool AmsTray::is_unset_third_filament()
{
    if (this->is_bbl)
        return false;

    if (color.empty() || type.empty())
        return true;
    return false;
}

std::string AmsTray::get_display_filament_type()
{
    if (type == "PLA-S")
        return "Support W";
    else if (type == "PA-S")
        return "Support G";
    else
        return type;
    return type;
}

std::string AmsTray::get_filament_type()
{
    if (type == "Support W") {
        return "PLA-S";
    } else if (type == "Support G") {
        return "PA-S";
    } else if (type == "Support") {
        if (setting_id == "GFS00") {
            type = "PLA-S";
        } else if (setting_id == "GFS01") {
            type = "PA-S";
        } else {
            return "PLA-S";
        }
    } else {
        return type;
    }
    return type;
}

bool HMSItem::parse_hms_info(unsigned attr, unsigned code)
{
    bool result = true;
    unsigned int model_id_int = (attr >> 24) & 0xFF;
    if (model_id_int < (unsigned) MODULE_MAX)
        this->module_id = (ModuleID)model_id_int;
    else
        this->module_id = MODULE_UKNOWN;
    this->module_num = (attr >> 16) & 0xFF;
    this->part_id    = (attr >> 8) & 0xFF;
    this->reserved   = (attr >> 0) & 0xFF;
    unsigned msg_level_int = code >> 16;
    if (msg_level_int < (unsigned)HMS_MSG_LEVEL_MAX)
        this->msg_level = (HMSMessageLevel)msg_level_int;
    else
        this->msg_level = HMS_UNKNOWN;
    this->msg_code = code & 0xFFFF;
    return result;
}

std::string HMSItem::get_long_error_code()
{
    char buf[64];
    ::sprintf(buf, "%02X%02X%02X00000%1X%04X",
        this->module_id,
        this->module_num,
        this->part_id,
        (int)this->msg_level,
        this->msg_code);
    return std::string(buf);
}

wxString HMSItem::get_module_name(ModuleID module_id)
{
    switch (module_id)
    {
    case MODULE_MC:
        return _L("MC");
    case MODULE_MAINBOARD:
        return _L("MainBoard");
    case MODULE_AMS:
        return _L("AMS");
    case MODULE_TH:
        return _L("TH");
    case MODULE_XCAM:
        return _L("XCam");
    default:
        wxString text = _L("Unknown") + wxString::Format("0x%x", (unsigned)module_id);
        return text;
    }
    return "";
}

wxString HMSItem::get_hms_msg_level_str(HMSMessageLevel level)
{
    switch(level) {
    case HMS_FATAL:
        return _L("Fatal");
    case HMS_SERIOUS:
        return _L("Serious");
    case HMS_COMMON:
        return _L("Common");
    case HMS_INFO:
        return _L("Info");
    default:
        return _L("Unknown");
    }
    return "";
}

std::string MachineObject::parse_printer_type(std::string type_str)
{
    if (type_str.compare("3DPrinter-X1") == 0) {
        return "BL-P002";
    } else if (type_str.compare("3DPrinter-X1-Carbon") == 0) {
        return "BL-P001";
    } else if (type_str.compare("BL-P001") == 0) {
        return type_str;
    } else if (type_str.compare("BL-P003") == 0) {
        return type_str;
    } else {
        return DeviceManager::parse_printer_type(type_str);
    }
    return "";
}

std::string MachineObject::get_preset_printer_model_name(std::string printer_type)
{
    return DeviceManager::get_printer_display_name(printer_type);
}

std::string MachineObject::get_preset_printer_thumbnail_img(std::string printer_type)
{
    return DeviceManager::get_printer_thumbnail_img(printer_type);
}

wxString MachineObject::get_printer_type_display_str()
{
    std::string display_name = get_preset_printer_model_name(printer_type);
    if (!display_name.empty())
        return display_name;
    else
        return _L("Unknown");
}

std::string MachineObject::get_printer_thumbnail_img_str()
{
    std::string img_str = get_preset_printer_thumbnail_img(printer_type);
    if (!img_str.empty())
        return img_str;
    else
        return "printer_thumbnail";
}

void MachineObject::set_access_code(std::string code)
{
    this->access_code = code;
    AppConfig *config = GUI::wxGetApp().app_config;
    if (config) {
        GUI::wxGetApp().app_config->set_str("access_code", dev_id, code);
    }
}

bool MachineObject::is_lan_mode_printer()
{
    bool result = false;
    if (!dev_connection_type.empty() && dev_connection_type == "lan")
        return true;
    return result;
}

MachineObject::MachineObject(NetworkAgent* agent, std::string name, std::string id, std::string ip)
    :dev_name(name),
    dev_id(id),
    dev_ip(ip),
    subtask_(nullptr),
    slice_info(nullptr),
    m_is_online(false)
{
    m_agent = agent;

    reset();

    /* temprature fields */
    nozzle_temp         = 0.0f;
    nozzle_temp_target  = 0.0f;
    bed_temp            = 0.0f;
    bed_temp_target     = 0.0f;
    chamber_temp        = 0.0f;
    frame_temp          = 0.0f;

    /* ams fileds */
    ams_exist_bits = 0;
    tray_exist_bits = 0;
    tray_is_bbl_bits = 0;
    ams_rfid_status = 0;
    is_ams_need_update = false;
    ams_insert_flag = false;
    ams_power_on_flag = false;
    ams_support_use_ams = false;
    ams_calibrate_remain_flag = false;
    ams_humidity = 5;

    /* signals */
    wifi_signal = "";

    /* upgrade */
    upgrade_force_upgrade = false;
    upgrade_new_version = false;
    upgrade_consistency_request = false;

    /* cooling */
    heatbreak_fan_speed = 0;
    cooling_fan_speed = 0;
    big_fan1_speed = 0;
    big_fan2_speed = 0;
    fan_gear = 0;

    /* printing */
    mc_print_stage = 0;
    mc_print_error_code = 0;
    print_error = 0;
    mc_print_line_number = 0;
    mc_print_percent = 0;
    mc_print_sub_stage = 0;
    mc_left_time = 0;
    home_flag = -1;
    hw_switch_state = 0;
    printing_speed_lvl   = PrintingSpeedLevel::SPEED_LEVEL_INVALID;

    has_ipcam = true; // default true
}

MachineObject::~MachineObject()
{
    if (subtask_) {
        delete subtask_;
        subtask_ = nullptr;
    }

    if (get_slice_info_thread) {
        if (get_slice_info_thread->joinable()) {
            get_slice_info_thread->join();
            get_slice_info_thread = nullptr;
        }
    }

    if (slice_info) {
        delete slice_info;
        slice_info = nullptr;
    }

    for (auto it = amsList.begin(); it != amsList.end(); it++) {
        for (auto tray_it = it->second->trayList.begin(); tray_it != it->second->trayList.end(); tray_it++) {
            if (tray_it->second) {
                delete tray_it->second;
                tray_it->second = nullptr;
            }
        }
        it->second->trayList.clear();
    }
    amsList.clear();
}

bool MachineObject::check_valid_ip()
{
    if (dev_ip.empty()) {
        return false;
    }

    return true;
}

void MachineObject::_parse_print_option_ack(int option)
{
    xcam_auto_recovery_step_loss = ((option >> (int)PRINT_OP_AUTO_RECOVERY) & 0x01) != 0;
}

void MachineObject::_parse_tray_now(std::string tray_now)
{
    m_tray_now = tray_now;
    if (tray_now.empty()) {
        return;
    } else {
        try {
            int tray_now_int = atoi(tray_now.c_str());
            if (tray_now_int >= 0 && tray_now_int < 16) {
                m_ams_id = std::to_string(tray_now_int >> 2);
                m_tray_id = std::to_string(tray_now_int & 0x3);
            }
            else if (tray_now_int == 255) {
                m_ams_id = "0";
                m_tray_id = "0";
            }
        }
        catch(...) {
        }
    }
}

Ams *MachineObject::get_curr_Ams()
{
    auto it = amsList.find(m_ams_id);
    if (it != amsList.end())
        return it->second;
    return nullptr;
}

AmsTray *MachineObject::get_curr_tray()
{
    Ams* curr_ams = get_curr_Ams();
    if (!curr_ams) return nullptr;

    auto it = curr_ams->trayList.find(m_tray_now);
    if (it != curr_ams->trayList.end())
        return it->second;
    return nullptr;
}

AmsTray *MachineObject::get_ams_tray(std::string ams_id, std::string tray_id)
{
    auto it = amsList.find(ams_id);
    if (it == amsList.end()) return nullptr;
    if (!it->second) return nullptr;

    auto iter = it->second->trayList.find(tray_id);
    if (iter != it->second->trayList.end())
        return iter->second;
    else
        return nullptr;
}

void MachineObject::_parse_ams_status(int ams_status)
{
    ams_status_sub = ams_status & 0xFF;
    int ams_status_main_int = (ams_status & 0xFF00) >> 8;
    if (ams_status_main_int == (int)AmsStatusMain::AMS_STATUS_MAIN_IDLE) {
        ams_status_main = AmsStatusMain::AMS_STATUS_MAIN_IDLE;
    } else if (ams_status_main_int == (int) AmsStatusMain::AMS_STATUS_MAIN_FILAMENT_CHANGE) {
        ams_status_main = AmsStatusMain::AMS_STATUS_MAIN_FILAMENT_CHANGE;
    } else if (ams_status_main_int == (int) AmsStatusMain::AMS_STATUS_MAIN_RFID_IDENTIFYING) {
        ams_status_main = AmsStatusMain::AMS_STATUS_MAIN_RFID_IDENTIFYING;
    } else if (ams_status_main_int == (int) AmsStatusMain::AMS_STATUS_MAIN_ASSIST) {
        ams_status_main = AmsStatusMain::AMS_STATUS_MAIN_ASSIST;
    } else if (ams_status_main_int == (int) AmsStatusMain::AMS_STATUS_MAIN_CALIBRATION) {
        ams_status_main = AmsStatusMain::AMS_STATUS_MAIN_CALIBRATION;
    } else if (ams_status_main_int == (int) AmsStatusMain::AMS_STATUS_MAIN_SELF_CHECK) {
        ams_status_main = AmsStatusMain::AMS_STATUS_MAIN_SELF_CHECK;
    } else if (ams_status_main_int == (int) AmsStatusMain::AMS_STATUS_MAIN_DEBUG) {
        ams_status_main = AmsStatusMain::AMS_STATUS_MAIN_DEBUG;
    } else {
        ams_status_main = AmsStatusMain::AMS_STATUS_MAIN_UNKNOWN;
    }

    BOOST_LOG_TRIVIAL(trace) << "ams_debug: main = " << ams_status_main_int << ", sub = " << ams_status_sub;
}

bool MachineObject::can_unload_filament()
{
    bool result = false;
    if (!has_ams())
        return true;

    if (ams_status_main == AMS_STATUS_MAIN_IDLE && hw_switch_state == 1 && m_tray_now == "255") {
        return true;
    }
    return result;
}

bool MachineObject::is_U0_firmware()
{
    auto ota_ver_it = module_vers.find("ota");
    if (ota_ver_it != module_vers.end()) {
        if (ota_ver_it->second.sw_ver.compare("00.01.04.00") < 0)
            return true;
    }
    return false;
}

bool MachineObject::is_support_ams_mapping()
{
    if (printer_type == "BL-P001" || printer_type == "BL-P002") {
        AppConfig* config = Slic3r::GUI::wxGetApp().app_config;
        if (config) {
            if (config->get("check_ams_version") == "0")
                return true;
        }
        bool need_upgrade = false;
        if (has_ams()) {
            // compare ota version and ams version
            auto ota_ver_it = module_vers.find("ota");
            if (ota_ver_it != module_vers.end()) {
                if (!MachineObject::is_support_ams_mapping_version("ota", ota_ver_it->second.sw_ver)) {
                    need_upgrade = true;
                }
            }
            for (int i = 0; i < 4; i++) {
                std::string ams_id = (boost::format("ams/%1%") % i).str();
                auto ams_ver_it = module_vers.find(ams_id);
                if (ams_ver_it != module_vers.end()) {
                    if (!MachineObject::is_support_ams_mapping_version("ams", ams_ver_it->second.sw_ver)) {
                        need_upgrade = true;
                    }
                }
            }
        }
        return !need_upgrade;
    }
    else {
        return true;
    }
}

bool MachineObject::is_support_ams_mapping_version(std::string module, std::string version)
{
    bool result = true;

    if (module == "ota") {
        if (version.compare("00.01.04.03") < 0)
            return false;
    }
    else if (module == "ams") {
        // omit ams version is empty
        if (version.empty())
            return true;
        if (version.compare("00.00.04.10") < 0)
            return false;
    }
    return result;
}

bool MachineObject::is_only_support_cloud_print()
{
    auto ap_ver_it = module_vers.find("rv1126");
    if (ap_ver_it != module_vers.end()) {
        if (ap_ver_it->second.sw_ver > "00.00.12.61") {
            return false;
        }
    }
    return true;
}

static float calc_color_distance(wxColour c1, wxColour c2)
{
    float lab[2][3];
    RGB2Lab(c1.Red(), c1.Green(), c1.Blue(), &lab[0][0], &lab[0][1], &lab[0][2]);
    RGB2Lab(c2.Red(), c2.Green(), c2.Blue(), &lab[1][0], &lab[1][1], &lab[1][2]);

    return DeltaE76(lab[0][0], lab[0][1], lab[0][2], lab[1][0], lab[1][1], lab[1][2]);
}

/* use common colors to calc a threshold */
static float calc_threshold()
{
    //common colors from https://www.ebaomonthly.com/window/photo/lesson/colorList.htm

    const int common_color_num = 32;
    wxColour colors[common_color_num] = {
        wxColour(255, 0, 0),
        wxColour(255, 36, 0),
        wxColour(255, 77, 0),
        wxColour(255, 165, 0),
        wxColour(255, 191, 0),
        wxColour(255, 215, 0),
        wxColour(255, 255, 0),
        wxColour(204, 255, 0),
        wxColour(102, 255, 0),
        wxColour(0, 255, 0),

        wxColour(0, 255, 255),
        wxColour(0, 127, 255),
        wxColour(0, 0, 255),
        wxColour(127, 255, 212),
        wxColour(224, 255, 255),
        wxColour(240, 248, 245),
        wxColour(48, 213, 200),
        wxColour(100, 149, 237),
        wxColour(0, 51, 153),
        wxColour(65, 105, 225),

        wxColour(0, 51, 102),
        wxColour(42, 82, 190),
        wxColour(0, 71, 171),
        wxColour(30, 144, 255),
        wxColour(0, 47, 167),
        wxColour(0, 0, 128),
        wxColour(94, 134, 193),
        wxColour(204, 204, 255),
        wxColour(8, 37, 103),
        wxColour(139, 0, 255),

        wxColour(227, 38, 54),
        wxColour(255, 0, 255)
    };

    float min_val = INT_MAX;
    int a = -1;
    int b = -1;
    for (int i = 0; i < common_color_num; i++) {
        for (int j = i+1; j < common_color_num; j++) {
            float distance = calc_color_distance(colors[i], colors[j]);
            if (min_val > distance) {
                min_val = distance;
                a = i;
                b = j;
            }
        }
    }
    BOOST_LOG_TRIVIAL(trace) << "min_distance = " << min_val << ", a = " << a << ", b = " << b;

    return min_val;
}

int MachineObject::ams_filament_mapping(std::vector<FilamentInfo> filaments, std::vector<FilamentInfo>& result, std::vector<int> exclude_id)
{
    if (filaments.empty())
        return -1;

    // tray_index : tray_color
    std::map<int, FilamentInfo> tray_filaments;
    for (auto ams = amsList.begin(); ams != amsList.end(); ams++) {
        for (auto tray = ams->second->trayList.begin(); tray != ams->second->trayList.end(); tray++) {
            int ams_id = atoi(ams->first.c_str());
            int tray_id = atoi(tray->first.c_str());
            int tray_index = ams_id * 4 + tray_id;
            // skip exclude id
            for (int i = 0; i < exclude_id.size(); i++) {
                if (tray_index == exclude_id[i])
                    continue;
            }
            // push
            if (tray->second->is_tray_info_ready()) {
                FilamentInfo info;
                info.color = tray->second->color;
                info.type = tray->second->get_filament_type();
                info.id = tray_index;
                tray_filaments.emplace(std::make_pair(tray_index, info));
            }
        }
    }

    // tray info list
    std::vector<FilamentInfo> tray_info_list;
    for (auto it = amsList.begin(); it != amsList.end(); it++) {
        for (int i = 0; i < 4; i++) {
            FilamentInfo info;
            auto tray_it = it->second->trayList.find(std::to_string(i));
            if (tray_it != it->second->trayList.end()) {
                info.id = atoi(tray_it->first.c_str()) + atoi(it->first.c_str()) * 4;
                info.tray_id = atoi(tray_it->first.c_str()) + atoi(it->first.c_str()) * 4;
                info.color = tray_it->second->color;
                info.type = tray_it->second->get_filament_type();
            }
            else {
                info.id = -1;
                info.tray_id = -1;
            }
            tray_info_list.push_back(info);
        }
    }

    
    // is_support_ams_mapping
    if (!is_support_ams_mapping()) {
        BOOST_LOG_TRIVIAL(info) << "ams_mapping: do not support, use order mapping";
        result.clear();
        for (int i = 0; i < filaments.size(); i++) {
            FilamentInfo info;
            info.id = filaments[i].id;
            int ams_id = filaments[i].id / 4;
            auto ams_it = amsList.find(std::to_string(ams_id));
            if (ams_it == amsList.end()) {
                info.tray_id = -1;
                info.mapping_result = (int)MappingResult::MAPPING_RESULT_EXCEED;
            } else {
                info.tray_id = filaments[i].id;
                int tray_id = filaments[i].id % 4;
                auto tray_it = ams_it->second->trayList.find(std::to_string(tray_id));
                if (tray_it != ams_it->second->trayList.end()) {
                    if (!tray_it->second->is_exists || tray_it->second->is_unset_third_filament()) {
                        ;
                    } else {
                        if (filaments[i].type == tray_it->second->get_filament_type()) {
                            info.color = tray_it->second->color;
                            info.type = tray_it->second->get_filament_type();
                        } else {
                            info.tray_id = -1;
                            info.mapping_result = (int)MappingResult::MAPPING_RESULT_TYPE_MISMATCH;
                        }
                    }
                }
            }
            result.push_back(info);
        }
        return 1;
    }

    char buffer[256];
    std::vector<std::vector<DisValue>> distance_map;

    // print title
    ::sprintf(buffer, "F(id)");
    std::string line = std::string(buffer);
    for (auto tray = tray_filaments.begin(); tray != tray_filaments.end(); tray++) {
        ::sprintf(buffer, "   AMS%02d", tray->second.id+1);
        line += std::string(buffer);
    }
    BOOST_LOG_TRIVIAL(info) << "ams_mapping_distance:" << line;

    for (int i = 0; i < filaments.size(); i++) {
        std::vector<DisValue> rol;
        ::sprintf(buffer, "F(%02d)", filaments[i].id+1);
        line = std::string(buffer);
        for (auto tray = tray_filaments.begin(); tray != tray_filaments.end(); tray++) {
            DisValue val;
            val.tray_id = tray->second.id;
            wxColour c = wxColour(filaments[i].color);
            val.distance = calc_color_distance(c, AmsTray::decode_color(tray->second.color));
            if (filaments[i].type != tray->second.type) {
                val.distance = 999999;
                val.is_type_match = false;
            } else {
                val.is_type_match = true;
            }
            ::sprintf(buffer, "  %6.0f", val.distance);
            line += std::string(buffer);
            rol.push_back(val);
        }
        BOOST_LOG_TRIVIAL(info) << "ams_mapping_distance:" << line;
        distance_map.push_back(rol);
    }

    // mapping algorithm
    for (int i = 0; i < filaments.size(); i++) {
        FilamentInfo info;
        info.id = filaments[i].id;
        info.tray_id = -1;
        result.push_back(info);
    }

    std::set<int> picked_src;
    std::set<int> picked_tar;
    for (int k = 0; k < distance_map.size(); k++) {
        float min_val = INT_MAX;
        int picked_src_idx = -1;
        int picked_tar_idx = -1;
        for (int i = 0; i < distance_map.size(); i++) {
            if (picked_src.find(i) != picked_src.end())
                continue;
            for (int j = 0; j < distance_map[i].size(); j++) {
                if (picked_tar.find(j) != picked_tar.end())
                    continue;
                if (distance_map[i][j].is_same_color
                    && distance_map[i][j].is_type_match) {
                    if (min_val > distance_map[i][j].distance) {
                        min_val = distance_map[i][j].distance;
                        picked_src_idx = i;
                        picked_tar_idx = j;
                    }
                }
            }
        }
        if (picked_src_idx >= 0 && picked_tar_idx >= 0) {
            auto tray = tray_filaments.find(distance_map[k][picked_tar_idx].tray_id);
            if (tray != tray_filaments.end()) {
                result[picked_src_idx].tray_id  = tray->first;
                result[picked_src_idx].color    = tray->second.color;
                result[picked_src_idx].type     = tray->second.type;
                result[picked_src_idx].distance = tray->second.distance;
            }
            else {
                FilamentInfo info;
                info.tray_id = -1;
            }
            ::sprintf(buffer, "ams_mapping, picked F(%02d) AMS(%02d), distance=%6.0f", picked_src_idx+1, picked_tar_idx+1,
                distance_map[picked_src_idx][picked_tar_idx].distance);
            BOOST_LOG_TRIVIAL(info) << std::string(buffer);
            picked_src.insert(picked_src_idx);
            picked_tar.insert(picked_tar_idx);
        }
    }

    std::vector<FilamentInfo> cache_map_result = result;

    //check ams mapping result
    if (is_valid_mapping_result(result, true)) {
        return 0;
    }

    reset_mapping_result(result);
    try {
        // try to use ordering ams mapping
        bool order_mapping_result = true;
        for (int i = 0; i < filaments.size(); i++) {
            if (i >= tray_info_list.size()) {
                order_mapping_result = false;
                break;
            }
            if (tray_info_list[i].tray_id == -1) {
                result[i].tray_id = tray_info_list[i].tray_id;
            } else {
                if (!tray_info_list[i].type.empty() && tray_info_list[i].type != filaments[i].type) {
                    order_mapping_result = false;
                    break;
                } else {
                    result[i].tray_id = tray_info_list[i].tray_id;
                    result[i].color = tray_info_list[i].color;
                    result[i].type = tray_info_list[i].type;
                }
            }
        }

        //check order mapping result
        if (is_valid_mapping_result(result, true)) {
            return 0;
        }
    } catch(...) {
        reset_mapping_result(result);
        return -1;
    }

    // try to match some color
    reset_mapping_result(result);
    result = cache_map_result;
    for (auto it = result.begin(); it != result.end(); it++) {
        if (it->distance >= 6000) {
            it->tray_id = -1;
        }
    }

    return 0;
}

bool MachineObject::is_valid_mapping_result(std::vector<FilamentInfo>& result, bool check_empty_slot)
{
    bool valid_ams_mapping_result = true;
    if (result.empty()) return false;

    for (int i = 0; i < result.size(); i++) {
        // invalid mapping result
        if (result[i].tray_id < 0)
            valid_ams_mapping_result = false;
        else {
            int ams_id = result[i].tray_id / 4;
            auto ams_item = amsList.find(std::to_string(ams_id));
            if (ams_item == amsList.end()) {
                result[i].tray_id = -1;
                valid_ams_mapping_result = false;
            } else {
                if (check_empty_slot) {
                    int  tray_id   = result[i].tray_id % 4;
                    auto tray_item = ams_item->second->trayList.find(std::to_string(tray_id));
                    if (tray_item == ams_item->second->trayList.end()) {
                        result[i].tray_id        = -1;
                        valid_ams_mapping_result = false;
                    } else {
                        if (!tray_item->second->is_exists) {
                            result[i].tray_id        = -1;
                            valid_ams_mapping_result = false;
                        }
                    }
                }
            }
        }
    }
    return valid_ams_mapping_result;
}

bool MachineObject::is_mapping_exceed_filament(std::vector<FilamentInfo> & result, int &exceed_index)
{
    bool is_exceed = false;
    for (int i = 0; i < result.size(); i++) {
        int ams_id = result[i].tray_id / 4;
        if (amsList.find(std::to_string(ams_id)) == amsList.end()) {
            exceed_index = result[i].tray_id;
            result[i].tray_id = -1;
            is_exceed = true;
            break;
        }
        if (result[i].mapping_result == MappingResult::MAPPING_RESULT_EXCEED) {
            exceed_index = result[i].id;
            is_exceed = true;
            break;
        }
    }
    return is_exceed;
}

void MachineObject::reset_mapping_result(std::vector<FilamentInfo>& result)
{
    for (int i = 0; i < result.size(); i++) {
        result[i].tray_id = -1;
        result[i].distance = 99999;
        result[i].mapping_result = 0;
    }
}

bool MachineObject::is_bbl_filament(std::string tag_uid)
{
    if (tag_uid.empty())
        return false;

    for (int i = 0; i < tag_uid.length(); i++) {
        if (tag_uid[i] != '0')
            return true;
    }

    return false;
}

std::string MachineObject::light_effect_str(LIGHT_EFFECT effect)
{
    switch (effect)
    {
    case LIGHT_EFFECT::LIGHT_EFFECT_ON:
        return "on";
    case LIGHT_EFFECT::LIGHT_EFFECT_OFF:
        return "off";
    case LIGHT_EFFECT::LIGHT_EFFECT_FLASHING:
        return "flashing";
    default:
        return "unknown";
    }
    return "unknown";
}

MachineObject::LIGHT_EFFECT MachineObject::light_effect_parse(std::string effect_str)
{
    if (effect_str.compare("on") == 0)
        return LIGHT_EFFECT::LIGHT_EFFECT_ON;
    else if (effect_str.compare("off") == 0)
        return LIGHT_EFFECT::LIGHT_EFFECT_OFF;
    else if (effect_str.compare("flashing") == 0)
        return LIGHT_EFFECT::LIGHT_EFFECT_FLASHING;
    else
        return LIGHT_EFFECT::LIGHT_EFFECT_UNKOWN;
    return LIGHT_EFFECT::LIGHT_EFFECT_UNKOWN;
}


std::string MachineObject::get_firmware_type_str()
{
    if (firmware_type == PrinterFirmwareType::FIRMWARE_TYPE_ENGINEER)
        return "engineer";
    else if (firmware_type == PrinterFirmwareType::FIRMWARE_TYPE_PRODUCTION)
        return "product";

    // return engineer by default;
    return "engineer";
}

bool MachineObject::is_in_upgrading()
{
    return upgrade_display_state == (int)UpgradingInProgress;
}

bool MachineObject::is_upgrading_avalable()
{
    return upgrade_display_state == (int)UpgradingAvaliable;
}

int MachineObject::get_upgrade_percent()
{
    if (upgrade_progress.empty())
        return 0;
    try {
        int result = atoi(upgrade_progress.c_str());
        return result;
    } catch(...) {
        ;
    }
    return 0;
}

std::string MachineObject::get_ota_version()
{
    auto it = module_vers.find("ota");
    if (it != module_vers.end()) {
        //double check name
        if (it->second.name == "ota") {
            return it->second.sw_ver;
        }
    }
    return "";
}

bool MachineObject::check_version_valid()
{
    bool valid = true;
    for (auto module : module_vers) {
        if (module.second.sn.empty()
            && module.first != "ota"
            && module.first != "xm")
            return false;
        if (module.second.sw_ver.empty())
            return false;
    }
    get_version_retry = 0;
    return valid;
}

wxString MachineObject::get_upgrade_result_str(int err_code)
{
    switch(err_code) {
    case UpgradeNoError:
        return _L("Update successful.");
    case UpgradeDownloadFailed:
        return _L("Downloading failed.");
    case UpgradeVerfifyFailed:
        return _L("Verification failed.");
    case UpgradeFlashFailed:
        return _L("Update failed.");
    case UpgradePrinting:
        return _L("Update failed.");
    default:
        return _L("Update failed.");
    }
    return "";
}

std::map<int, MachineObject::ModuleVersionInfo> MachineObject::get_ams_version()
{
    std::map<int, ModuleVersionInfo> result;
    for (int i = 0; i < 4; i++) {
        std::string ams_id = "ams/" + std::to_string(i);
        auto it = module_vers.find(ams_id);
        if (it != module_vers.end()) {
            result.emplace(std::pair(i, it->second));
        }
    }
    return result;
}

bool MachineObject::is_system_printing()
{
    if (is_in_calibration() && is_in_printing_status(print_status))
        return true;
    //FIXME
    //if (print_type == "system" && is_in_printing_status(print_status))
        //return true;
    return false;
}

bool MachineObject::is_axis_at_home(std::string axis)
{
    if (home_flag < 0)
        return true;

    if (axis == "X") {
        return home_flag & 1 == 1;
    } else if (axis == "Y") {
        return home_flag >> 1 & 1 == 1;
    } else if (axis == "Z") {
        return home_flag >> 2 & 1 == 1;
    } else {
        return true;
    }
}

bool MachineObject::is_filament_at_extruder()
{
    if (hw_switch_state == 1)
        return true;
    else if (hw_switch_state == 0)
        return false;
    else {
       //default
        return true;
    }
}

wxString MachineObject::get_curr_stage()
{
    if (stage_list_info.empty()) {
        return "";
    }
    return get_stage_string(stage_curr);
}

int MachineObject::get_curr_stage_idx()
{
    int result = -1;
    for (int i = 0; i < stage_list_info.size(); i++) {
        if (stage_list_info[i] == stage_curr) {
            return i;
        }
    }
    return -1;
}

bool MachineObject::is_in_calibration()
{
    // gcode file: auto_cali_for_user.gcode or auto_cali_for_user_param
    if (boost::contains(m_gcode_file, "auto_cali_for_user")
        && stage_curr != 0) {
        return true;
    } else {
        // reset
        if (stage_curr != 0) {
            calibration_done = false;
        }
    }
    return false;
}

bool MachineObject::is_calibration_done()
{
    return calibration_done;
}

bool MachineObject::is_calibration_running()
{
    if (is_in_calibration() && is_in_printing_status(print_status))
        return true;
    return false;
}

void MachineObject::parse_state_changed_event()
{
    // parse calibration done
    if (last_mc_print_stage != mc_print_stage) {
        if (mc_print_stage == 1 && boost::contains(m_gcode_file, "auto_cali_for_user.gcode")) {
            calibration_done = true;
        } else {
            calibration_done = false;
        }
    }
    last_mc_print_stage = mc_print_stage;
}

void MachineObject::parse_status(int flag)
{
    is_220V_voltage             = ((flag >> 3) & 0x1) != 0;
    if (xcam_auto_recovery_hold_count > 0)
        xcam_auto_recovery_hold_count--;
    else {
        xcam_auto_recovery_step_loss = ((flag >> 4) & 0x1) != 0;
    }

    camera_recording            = ((flag >> 5) & 0x1) != 0;
    ams_calibrate_remain_flag   = ((flag >> 7) & 0x1) != 0;

    if (ams_print_option_count > 0)
        ams_print_option_count--;
    else {
        ams_auto_switch_filament_flag = ((flag >> 10) & 0x1) != 0;
    }

    sdcard_state = MachineObject::SdcardState((flag >> 8) & 0x11);
}

PrintingSpeedLevel MachineObject::_parse_printing_speed_lvl(int lvl)
{
    if (lvl < (int)SPEED_LEVEL_COUNT)
        return PrintingSpeedLevel(lvl);

    return PrintingSpeedLevel::SPEED_LEVEL_INVALID;
}

int MachineObject::get_bed_temperature_limit()
{
    if (printer_type == "BL-P001" || printer_type == "BL-P002") {
        if (is_220V_voltage)
            return 110;
        else {
            return 120;
        }
    } else {
        int limit = BED_TEMP_LIMIT;
        DeviceManager::get_bed_temperature_limit(printer_type, limit);
        return limit;
    }
    return BED_TEMP_LIMIT;
}

bool MachineObject::is_sdcard_printing()
{
    if (can_abort()
        && (obj_subtask_id.compare("0") == 0 || obj_subtask_id.empty())
        && (profile_id_ == "0" || profile_id_.empty())
        && (project_id_ == "0" || project_id_.empty()))
        return true;
    else
        return false;
}

bool MachineObject::has_sdcard()
{
    return (sdcard_state == MachineObject::SdcardState::HAS_SDCARD_NORMAL);
}

MachineObject::SdcardState MachineObject::get_sdcard_state()
{
    return sdcard_state;
}

bool MachineObject::is_timelapse()
{
    return camera_timelapse;
}

bool MachineObject::is_recording_enable()
{
    return camera_recording_when_printing;
}

bool MachineObject::is_recording()
{
    return camera_recording;
}

void MachineObject::parse_version_func()
{
    auto ota_version = module_vers.find("ota");
    if (printer_type == "BL-P001" ||
        printer_type == "BL-P002") {
        if (ota_version != module_vers.end()) {
            if (ota_version->second.sw_ver.compare("01.01.01.00") <= 0) {
                ams_support_remain                      = false;
                ams_support_auto_switch_filament_flag   = false;
                is_xcam_buildplate_supported            = false;
                xcam_support_recovery_step_loss         = false;
                is_support_send_to_sdcard               = false;
            } else {
                ams_support_remain                      = true;
                ams_support_auto_switch_filament_flag   = true;
                is_xcam_buildplate_supported            = true;
                xcam_support_recovery_step_loss         = true;
                is_support_send_to_sdcard               = true;
            }
        }
    }
}

int MachineObject::command_get_version(bool with_retry)
{
    BOOST_LOG_TRIVIAL(info) << "command_get_version";
    json j;
    j["info"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["info"]["command"] = "get_version";
    if (with_retry)
        get_version_retry = GET_VERSION_RETRYS;
    return this->publish_json(j.dump(), 1);
}

int MachineObject::command_request_push_all()
{
    auto curr_time = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - last_request_push);
    if (diff.count() < REQUEST_PUSH_MIN_TIME) {
        BOOST_LOG_TRIVIAL(trace) << "static: command_request_push_all: send request too fast, dev_id=" << dev_id;
        return -1;
    } else {
        BOOST_LOG_TRIVIAL(trace) << "static: command_request_push_all, dev_id=" << dev_id;
        last_request_push = std::chrono::system_clock::now();
    }
    json j;
    j["pushing"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["pushing"]["command"]     = "pushall";
    return this->publish_json(j.dump());
}

int MachineObject::command_pushing(std::string cmd)
{
    auto curr_time = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - last_request_start);
    if (diff.count() < REQUEST_START_MIN_TIME) {
        BOOST_LOG_TRIVIAL(trace) << "static: command_request_start: send request too fast, dev_id=" << dev_id;
        return -1;
    }
    else {
        BOOST_LOG_TRIVIAL(trace) << "static: command_request_start, dev_id=" << dev_id;
        last_request_start = std::chrono::system_clock::now();
    }

    if (cmd == "start" || cmd == "stop") {
        json j;
        j["pushing"]["command"] = cmd;
        j["pushing"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
        return this->publish_json(j.dump());
    }
    return -1;
}

int MachineObject::command_upgrade_confirm()
{
    BOOST_LOG_TRIVIAL(info) << "command_upgrade_confirm";
    json j;
    j["upgrade"]["command"] = "upgrade_confirm";
    j["upgrade"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["upgrade"]["src_id"] = 1; // 1 for slicer
    return this->publish_json(j.dump());
}

int MachineObject::command_consistency_upgrade_confirm()
{
    BOOST_LOG_TRIVIAL(info) << "command_consistency_upgrade_confirm";
    json j;
    j["upgrade"]["command"] = "consistency_confirm";
    j["upgrade"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["upgrade"]["src_id"] = 1; // 1 for slicer
    return this->publish_json(j.dump());
}

int MachineObject::command_upgrade_firmware(FirmwareInfo info)
{
    std::string version     = info.version;
    std::string dst_url     = info.url;
    std::string module_name = info.module_type;

    json j;
    j["upgrade"]["command"]     = "start";
    j["upgrade"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["upgrade"]["url"]         = info.url;
    j["upgrade"]["module"]      = info.module_type;
    j["upgrade"]["version"]     = info.version;
    j["upgrade"]["src_id"]      = 1;

    return this->publish_json(j.dump());
}

int MachineObject::command_upgrade_module(std::string url, std::string module_type, std::string version)
{
    json j;
    j["upgrade"]["command"] = "start";
    j["upgrade"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["upgrade"]["url"] = url;
    j["upgrade"]["module"] = module_type;
    j["upgrade"]["version"] = version;
    j["upgrade"]["src_id"] = 1;

    return this->publish_json(j.dump());
}

int MachineObject::command_xyz_abs()
{
    return this->publish_gcode("G90 \n");
}

int MachineObject::command_auto_leveling()
{
    return this->publish_gcode("G29 \n");
}

int MachineObject::command_go_home()
{
    return this->publish_gcode("G28 \n");
}

int MachineObject::command_control_fan(FanType fan_type, bool on_off)
{
    std::string gcode = (boost::format("M106 P%1% S%2% \n") % (int)fan_type % (on_off ? 255 : 0)).str();
    return this->publish_gcode(gcode);
}

int MachineObject::command_control_fan_val(FanType fan_type, int val)
{
    std::string gcode = (boost::format("M106 P%1% S%2% \n") % (int)fan_type % (val)).str();
    return this->publish_gcode(gcode);
}


int MachineObject::command_task_abort()
{
    json j;
    j["print"]["command"] = "stop";
    j["print"]["param"] = "";
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);

    return this->publish_json(j.dump(), 1);
}

int MachineObject::command_task_pause()
{
    json j;
    j["print"]["command"] = "pause";
    j["print"]["param"] = "";
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);

    return this->publish_json(j.dump(), 1);
}

int MachineObject::command_task_resume()
{
    json j;
    j["print"]["command"] = "resume";
    j["print"]["param"] = "";
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);

    return this->publish_json(j.dump(), 1);
}

int MachineObject::command_set_bed(int temp)
{
    std::string gcode_str = (boost::format("M140 S%1%\n") % temp).str();
    return this->publish_gcode(gcode_str);
}

int MachineObject::command_set_nozzle(int temp)
{
    std::string gcode_str = (boost::format("M104 S%1%\n") % temp).str();
    return this->publish_gcode(gcode_str);
}

int MachineObject::command_ams_switch(int tray_index, int old_temp, int new_temp)
{
    BOOST_LOG_TRIVIAL(trace) << "ams_switch to " << tray_index << " with temp: " << old_temp << ", " << new_temp;
    if (old_temp < 0) old_temp = FILAMENT_DEF_TEMP;
    if (new_temp < 0) new_temp = FILAMENT_DEF_TEMP;
    int tray_id_int = tray_index;

    std::string gcode = "";
    if (tray_index == 255) {
        // unload gcode
        gcode = "M620 S255\nM104 S250\nG28 X\nG91\nG1 Z3.0 F1200\nG90\n"
                "G1 X70 F12000\nG1 Y245\nG1 Y265 F3000\nM109 S250\nG1 X120 F12000\n"
                "G1 X20 Y50 F12000\nG1 Y-3\nT255\nM104 S25\nG1 X165 F5000\nG1 Y245\n"
                "G91\nG1 Z-3.0 F1200\nG90\nM621 S255\n";
    } else {
        // load gcode
        gcode = "M620 S[next_extruder]\nM104 S250\nG28 X\nG91\n\nG1 Z3.0 F1200\nG90\n"
                "G1 X70 F12000\nG1 Y245\nG1 Y265 F3000\nM109 S250\nG1 X120 F12000\nG1 X20 Y50 F12000\nG1 Y-3"
                "\nT[next_extruder]\nG1 X54  F12000\nG1 Y265\nM400\nM106 P1 S0\nG92 E0\nG1 E40 F200\nM400"
                "\nM109 S[new_filament_temp]\nM400\nM106 P1 S255\nG92 E0\nG1 E5 F300\nM400\nM106 P1 S0\nG1 X70  F9000"
                "\nG1 X76 F15000\nG1 X65 F15000\nG1 X76 F15000\nG1 X65 F15000\nG1 X70 F6000\nG1 X100 F5000\nG1 X70 F15000"
                "\nG1 X100 F5000\nG1 X70 F15000\nG1 X165 F5000\nG1 Y245\nG91\nG1 Z-3.0 F1200\nG90\nM621 S[next_extruder]\n";

        boost::replace_all(gcode, "[next_extruder]", std::to_string(tray_index));
        boost::replace_all(gcode, "[new_filament_temp]", std::to_string(new_temp));
    }

    return this->publish_gcode(gcode);
}

int MachineObject::command_ams_change_filament(int tray_id, int old_temp, int new_temp)
{
    json j;
    j["print"]["command"] = "ams_change_filament";
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["print"]["target"] = tray_id;
    j["print"]["curr_temp"] = old_temp;
    j["print"]["tar_temp"] = new_temp;
    return this->publish_json(j.dump());
}

int MachineObject::command_ams_user_settings(int ams_id, bool start_read_opt, bool tray_read_opt, bool remain_flag)
{
    json j;
    j["print"]["command"] = "ams_user_setting";
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["print"]["ams_id"] = ams_id;
    j["print"]["startup_read_option"]   = start_read_opt;
    j["print"]["tray_read_option"]      = tray_read_opt;
    j["print"]["calibrate_remain_flag"] = remain_flag;

    ams_insert_flag = tray_read_opt;
    ams_power_on_flag = start_read_opt;
    ams_calibrate_remain_flag = remain_flag;
    ams_user_setting_hold_count = HOLD_COUNT_MAX;

    return this->publish_json(j.dump());
}

int MachineObject::command_ams_user_settings(int ams_id, AmsOptionType op, bool value)
{
    json j;
    j["print"]["command"]               = "ams_user_setting";
    j["print"]["sequence_id"]           = std::to_string(MachineObject::m_sequence_id++);
    j["print"]["ams_id"]                = ams_id;
    if (op == AmsOptionType::AMS_OP_STARTUP_READ) {
        j["print"]["startup_read_option"] = value;
        ams_power_on_flag                 = value;
    } else if (op == AmsOptionType::AMS_OP_TRAY_READ) {
        j["print"]["tray_read_option"] = value;
        ams_insert_flag                = value;
    } else if (op == AmsOptionType::AMS_OP_CALIBRATE_REMAIN) {
        j["print"]["calibrate_remain_flag"] = value;
        ams_calibrate_remain_flag = value;
    } else {
        return -1;
    }
    ams_user_setting_hold_count = HOLD_COUNT_MAX;
    return this->publish_json(j.dump());
}

int MachineObject::command_ams_calibrate(int ams_id)
{
    std::string gcode_cmd = (boost::format("M620 C%1% \n") % ams_id).str();
    BOOST_LOG_TRIVIAL(trace) << "ams_debug: gcode_cmd" << gcode_cmd;
    return this->publish_gcode(gcode_cmd);
}

int MachineObject::command_ams_filament_settings(int ams_id, int tray_id, std::string setting_id, std::string tray_color, std::string tray_type, int nozzle_temp_min, int nozzle_temp_max)
{
    BOOST_LOG_TRIVIAL(info) << "command_ams_filament_settings, ams_id = " << ams_id << ", tray_id = " << tray_id << ", tray_color = " << tray_color
                            << ", tray_type = " << tray_type;
    json j;
    j["print"]["command"] = "ams_filament_setting";
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["print"]["ams_id"]      = ams_id;
    j["print"]["tray_id"]     = tray_id;
    j["print"]["tray_info_idx"] = setting_id;
    // format "FFFFFFFF"   RGBA
    j["print"]["tray_color"]    = tray_color;
    j["print"]["nozzle_temp_min"]   = nozzle_temp_min;
    j["print"]["nozzle_temp_max"] = nozzle_temp_max;
    j["print"]["tray_type"] = tray_type;

    return this->publish_json(j.dump());
}

int MachineObject::command_ams_refresh_rfid(std::string tray_id)
{
    std::string gcode_cmd = (boost::format("M620 R%1% \n") % tray_id).str();
    BOOST_LOG_TRIVIAL(trace) << "ams_debug: gcode_cmd" << gcode_cmd;
    return this->publish_gcode(gcode_cmd);
}

int MachineObject::command_ams_select_tray(std::string tray_id)
{
    std::string gcode_cmd = (boost::format("M620 P%1% \n") % tray_id).str();
    BOOST_LOG_TRIVIAL(trace) << "ams_debug: gcode_cmd" << gcode_cmd;
    return this->publish_gcode(gcode_cmd);
}

int MachineObject::command_ams_control(std::string action)
{
    //valid actions
    if (action == "resume" || action == "reset" || action == "pause") {
        json j;
        j["print"]["command"] = "ams_control";
        j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
        j["print"]["param"] = action;
        return this->publish_json(j.dump());
    }
    return -1;
}


int MachineObject::command_set_chamber_light(LIGHT_EFFECT effect, int on_time, int off_time, int loops, int interval)
{
    json j;
    j["system"]["command"] = "ledctrl";
    j["system"]["led_node"] = "chamber_light";
    j["system"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["system"]["led_mode"] = light_effect_str(effect);
    j["system"]["led_on_time"] = on_time;
    j["system"]["led_off_time"] = off_time;
    j["system"]["loop_times"] = loops;
    j["system"]["interval_time"] = interval;

    return this->publish_json(j.dump());
}

int MachineObject::command_set_work_light(LIGHT_EFFECT effect, int on_time, int off_time, int loops, int interval)
{
    json j;
    j["system"]["command"] = "ledctrl";
    j["system"]["led_node"] = "work_light";
    j["system"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["system"]["led_mode"] = light_effect_str(effect);
    j["system"]["led_on_time"] = on_time;
    j["system"]["led_off_time"] = off_time;
    j["system"]["loop_times"] = loops;
    j["system"]["interval_time"] = interval;

    return this->publish_json(j.dump());
}

int MachineObject::command_set_printing_speed(PrintingSpeedLevel lvl)
{
    json j;
    j["print"]["command"] = "print_speed";
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["print"]["param"] = std::to_string((int)lvl);

    return this->publish_json(j.dump());
}

int MachineObject::command_set_printing_option(bool auto_recovery)
{
    int print_option = (int)auto_recovery << (int)PRINT_OP_AUTO_RECOVERY;
    json j;
    j["print"]["command"]       = "print_option";
    j["print"]["sequence_id"]   = std::to_string(MachineObject::m_sequence_id++);
    j["print"]["option"]        = print_option;
    j["print"]["auto_recovery"] = auto_recovery;

    return this->publish_json(j.dump());
}

int MachineObject::command_ams_switch_filament(bool switch_filament)
{
    json j;
    j["print"]["command"] = "print_option";
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["print"]["auto_switch_filament"] = switch_filament;

    ams_auto_switch_filament_flag = switch_filament;
    BOOST_LOG_TRIVIAL(trace) << "command_ams_filament_settings:" << switch_filament;
    ams_print_option_count = HOLD_COUNT_MAX;

    return this->publish_json(j.dump());
}

int MachineObject::command_axis_control(std::string axis, double unit, double value, int speed)
{
    char cmd[256];
    if (axis.compare("X") == 0
        || axis.compare("Y") == 0
        || axis.compare("Z") == 0) {
        sprintf(cmd, "M211 S \nM211 X1 Y1 Z1\nM1002 push_ref_mode\nG91 \nG1 %s%0.1f F%d\nM1002 pop_ref_mode\nM211 R\n", axis.c_str(), value * unit, speed);
    }
    else if (axis.compare("E") == 0) {
        sprintf(cmd, "M83 \nG0 %s%0.1f F%d\n", axis.c_str(), value * unit, speed);
        extruder_axis_status = (value >= 0.0f)? LOAD : UNLOAD;
    }
    else {
        return -1;
    }
    return this->publish_gcode(cmd);
}


bool MachineObject::is_support_command_calibration()
{
    if (printer_type == "BL-P001"
        || printer_type == "BL-P002") {
        auto ap_ver_it = module_vers.find("rv1126");
        if (ap_ver_it != module_vers.end()) {
            if (ap_ver_it->second.sw_ver.compare("00.00.15.79") < 0)
                return false;
        }
    }
    return true;
}

int MachineObject::command_start_calibration(bool vibration, bool bed_leveling, bool xcam_cali)
{
    if (!is_support_command_calibration()) {
        // fixed gcode file
        json j;
        j["print"]["command"] = "gcode_file";
        j["print"]["param"] = "/usr/etc/print/auto_cali_for_user.gcode";
        j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
        return this->publish_json(j.dump());
    } else {
        json j;
        j["print"]["command"] = "calibration";
        j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
        j["print"]["option"] =      (vibration    ? 1 << 2 : 0)
                                +   (bed_leveling ? 1 << 1 : 0)
                                +   (xcam_cali    ? 1 << 0 : 0);
        return this->publish_json(j.dump());
    }
}

int MachineObject::command_unload_filament()
{
    if (printer_type == "BL-P001"
        || printer_type == "BL-P002") {
        // fixed gcode file
        json j;
        j["print"]["command"] = "gcode_file";
        j["print"]["param"] = "/usr/etc/print/filament_unload.gcode";
        j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
        return this->publish_json(j.dump());
    }
    else {
        json j;
        j["print"]["command"] = "unload_filament";
        j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
        return this->publish_json(j.dump());
    }
}


int MachineObject::command_ipcam_record(bool on_off)
{
    BOOST_LOG_TRIVIAL(info) << "command_ipcam_record = " << on_off;
    json j;
    j["camera"]["command"] = "ipcam_record_set";
    j["camera"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["camera"]["control"] = on_off ? "enable" : "disable";
    camera_recording_hold_count = HOLD_COUNT_CAMERA;
    this->camera_recording_when_printing = on_off;
    return this->publish_json(j.dump());
}

int MachineObject::command_ipcam_timelapse(bool on_off)
{
    BOOST_LOG_TRIVIAL(info) << "command_ipcam_timelapse " << on_off;
    json j;
    j["camera"]["command"] = "ipcam_timelapse";
    j["camera"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["camera"]["control"] = on_off ? "enable" : "disable";
    camera_timelapse_hold_count = HOLD_COUNT_CAMERA;
    this->camera_timelapse = on_off;
    return this->publish_json(j.dump());
}

int MachineObject::command_ipcam_resolution_set(std::string resolution)
{
    BOOST_LOG_TRIVIAL(info) << "command:ipcam_resolution_set" << ", resolution:" << resolution;
    json j;
    j["camera"]["command"] = "ipcam_resolution_set";
    j["camera"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["camera"]["resolution"] = resolution;
    camera_resolution_hold_count = HOLD_COUNT_CAMERA;
    camera_recording_hold_count = HOLD_COUNT_CAMERA;
    this->camera_resolution = resolution;
    return this->publish_json(j.dump());
}

int MachineObject::command_xcam_control(std::string module_name, bool on_off, std::string lvl)
{
    json j;
    j["xcam"]["command"] = "xcam_control_set";
    j["xcam"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);
    j["xcam"]["module_name"] = module_name;
    j["xcam"]["control"] = on_off;
    j["xcam"]["enable"] = on_off;       //old protocol
    j["xcam"]["print_halt"]  = true;    //old protocol
    if (!lvl.empty()) {
        j["xcam"]["halt_print_sensitivity"] = lvl;
    }
    BOOST_LOG_TRIVIAL(info) << "command:xcam_control_set" << ", module_name:" << module_name << ", control:" << on_off << ", halt_print_sensitivity:" << lvl;
    return this->publish_json(j.dump());
}

int MachineObject::command_xcam_control_ai_monitoring(bool on_off, std::string lvl)
{
    bool print_halt = (lvl == "never_halt") ? false:true;

    xcam_ai_monitoring = on_off;
    xcam_ai_monitoring_hold_count = HOLD_COUNT_MAX;
    xcam_ai_monitoring_sensitivity = lvl;
    return command_xcam_control("printing_monitor", on_off, lvl);
}

int MachineObject::command_xcam_control_buildplate_marker_detector(bool on_off)
{
    xcam_buildplate_marker_detector = on_off;
    xcam_buildplate_marker_hold_count = HOLD_COUNT_MAX;
    return command_xcam_control("buildplate_marker_detector", on_off);
}

int MachineObject::command_xcam_control_first_layer_inspector(bool on_off, bool print_halt)
{
    xcam_first_layer_inspector = on_off;
    xcam_first_layer_hold_count = HOLD_COUNT_MAX;
    return command_xcam_control("first_layer_inspector", on_off);
}

int MachineObject::command_xcam_control_auto_recovery_step_loss(bool on_off)
{
    xcam_auto_recovery_step_loss = on_off;
    xcam_auto_recovery_hold_count = HOLD_COUNT_MAX;
    return command_set_printing_option(on_off);
}

void MachineObject::set_bind_status(std::string status)
{
    bind_user_name = status;
}

std::string MachineObject::get_bind_str()
{
    std::string default_result = "N/A";
    if (bind_user_name.compare("null") == 0) {
        return "Free";
    }
    else if (!bind_user_name.empty()) {
        return bind_user_name;
    }
    return default_result;
}

bool MachineObject::can_print()
{
    if (print_status.compare("RUNNING") == 0) {
        return false;
    }
    if (print_status.compare("IDLE") == 0 || print_status.compare("FINISH") == 0) {
        return true;
    }
    return true;
}

bool MachineObject::can_resume()
{
    if (print_status.compare("PAUSE") == 0)
        return true;
    return false;
}

bool MachineObject::can_pause()
{
    if (print_status.compare("RUNNING") == 0)
        return true;
    return false;
}

bool MachineObject::can_abort()
{
    return MachineObject::is_in_printing_status(print_status);
}

bool MachineObject::is_in_printing_status(std::string status)
{
    if (status.compare("PAUSE") == 0
        || status.compare("RUNNING") == 0
        || status.compare("SLICING") == 0
        || status.compare("PREPARE") == 0) {
        return true;
    }
    return false;
}

bool MachineObject::is_in_printing()
{
    /* use print_status if print_status is valid */
    if (!print_status.empty())
        return MachineObject::is_in_printing_status(print_status);
    else {
        return MachineObject::is_in_printing_status(iot_print_status);
    }
    return false;
}

bool MachineObject::is_in_prepare()
{
    return print_status == "PREPARE";
}

bool MachineObject::is_printing_finished()
{
    if (print_status.compare("FINISH") == 0
        || print_status.compare("FAILED") == 0) {
        return true;
    }
    return false;
}

void MachineObject::reset_update_time()
{
    BOOST_LOG_TRIVIAL(trace) << "reset reset_update_time, dev_id =" << dev_id;
    last_update_time = std::chrono::system_clock::now();
}

void MachineObject::reset()
{
    BOOST_LOG_TRIVIAL(trace) << "reset dev_id=" << dev_id;
    last_update_time = std::chrono::system_clock::now();
    m_push_count = 0;
    is_220V_voltage = false;
    get_version_retry = 0;
    camera_recording = false;
    camera_recording_when_printing = false;
    camera_timelapse = false;
    camera_resolution = "";
    printing_speed_mag = 100;
    gcode_file_prepare_percent = 0;
    iot_print_status = "";
    print_status = "";
    last_mc_print_stage = -1;
    m_new_ver_list_exist = false;
    extruder_axis_status = LOAD;

    subtask_ = nullptr;

}

void MachineObject::set_print_state(std::string status)
{
    print_status = status;
}

int MachineObject::connect(bool is_anonymous)
{
    std::string username;
    std::string password;
    if (!is_anonymous) {
        username = "bblp";
        password = access_code;
    }
    if (m_agent) {
        try {
            return m_agent->connect_printer(dev_id, dev_ip, username, password);
        } catch (...) {
            ;
        }
    }
    return -1;
}

int MachineObject::disconnect()
{
    if (m_agent) {
        return m_agent->disconnect_printer();
    }
    return -1;
}

bool MachineObject::is_connected()
{
    std::chrono::system_clock::time_point curr_time = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(curr_time - last_update_time);
    if (diff.count() > DISCONNECT_TIMEOUT) {
        BOOST_LOG_TRIVIAL(trace) << "machine_object: dev_id=" << dev_id <<", diff count = " << diff.count();
        return false;
    }

    if (!is_lan_mode_printer()) {
        NetworkAgent* m_agent = Slic3r::GUI::wxGetApp().getAgent();
        if (m_agent) {
            return m_agent->is_server_connected();
        }
    }
    return true;
}

bool MachineObject::is_connecting()
{
    return is_connected() && m_push_count == 0;
}

void MachineObject::set_online_state(bool on_off)
{
    m_is_online = on_off;
}

bool MachineObject::is_info_ready()
{
    if (module_vers.empty())
        return false;

    std::chrono::system_clock::time_point curr_time = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::microseconds>(last_push_time - curr_time);
    if (m_push_count > 0 && diff.count() < PUSHINFO_TIMEOUT) {
        return true;
    }
    return false;
}

bool MachineObject::is_function_supported(PrinterFunction func)
{
    std::string func_name;
    switch (func) {
    case FUNC_MONITORING:
        func_name = "FUNC_MONITORING";
        break;
    case FUNC_TIMELAPSE:
        func_name = "FUNC_TIMELAPSE";
        break;
    case FUNC_RECORDING:
        func_name = "FUNC_RECORDING";
        break;
    case FUNC_FIRSTLAYER_INSPECT:
        func_name = "FUNC_FIRSTLAYER_INSPECT";
        break;
    case FUNC_AI_MONITORING:
        func_name = "FUNC_AI_MONITORING";
        break;
    case FUNC_BUILDPLATE_MARKER_DETECT:
        parse_version_func();
        if (!is_xcam_buildplate_supported)
            return false;
        func_name = "FUNC_BUILDPLATE_MARKER_DETECT";
        break;
    case FUNC_AUTO_RECOVERY_STEP_LOSS:
        parse_version_func();
        if (!xcam_support_recovery_step_loss)
            return false;
        func_name = "FUNC_AUTO_RECOVERY_STEP_LOSS";
        break;
    case FUNC_FLOW_CALIBRATION:
        func_name = "FUNC_FLOW_CALIBRATION";
        break;
    case FUNC_AUTO_LEVELING:
        func_name = "FUNC_AUTO_LEVELING";
        break;
    case FUNC_CHAMBER_TEMP:
        func_name = "FUNC_CHAMBER_TEMP";
        break;
    case FUNC_CAMERA_VIDEO:
        func_name = "FUNC_CAMERA_VIDEO";
        break;
    case FUNC_MEDIA_FILE:
        func_name = "FUNC_MEDIA_FILE";
        break;
    case FUNC_REMOTE_TUNNEL:
        func_name = "FUNC_REMOTE_TUNNEL";
        break;
    case FUNC_LOCAL_TUNNEL:
        func_name = "FUNC_LOCAL_TUNNEL";
        break;
    case FUNC_PRINT_WITHOUT_SD:
        func_name = "FUNC_PRINT_WITHOUT_SD";
        break;
    case FUNC_USE_AMS:
        func_name = "FUNC_USE_AMS";
        break;
    case FUNC_ALTER_RESOLUTION:
        func_name = "FUNC_ALTER_RESOLUTION";
        break;
    case FUNC_SEND_TO_SDCARD:
        parse_version_func();
        if (!is_support_send_to_sdcard)
            return false;
        func_name = "FUNC_SEND_TO_SDCARD";
        break;
    case FUNC_AUTO_SWITCH_FILAMENT:
        parse_version_func();
        if (!ams_support_auto_switch_filament_flag)
            return false;
        func_name = "FUNC_AUTO_SWITCH_FILAMENT";
        break;
    case FUNC_VIRTUAL_CAMERA:
        func_name = "FUNC_VIRTUAL_CAMERA";
        break;
    case FUNC_CHAMBER_FAN:
        func_name = "FUNC_CHAMBER_FAN";
        break;
    default:
        return true;
    }
    return DeviceManager::is_function_supported(printer_type, func_name);
}

std::vector<std::string> MachineObject::get_resolution_supported()
{
    return DeviceManager::get_resolution_supported(printer_type);
}

bool MachineObject::is_support_print_with_timelapse()
{
    //TODO version check, set true by default
    return true;
}

int MachineObject::publish_json(std::string json_str, int qos)
{
    if (is_lan_mode_printer()) {
        return local_publish_json(json_str, qos);
    } else {
        return cloud_publish_json(json_str, qos);
    }
}

int MachineObject::cloud_publish_json(std::string json_str, int qos)
{
    int result = -1;
    if (m_agent)
        result = m_agent->send_message(dev_id, json_str, qos);

    return result;
}

int MachineObject::local_publish_json(std::string json_str, int qos)
{
    int result = -1;
    if (m_agent) {
        result = m_agent->send_message_to_printer(dev_id, json_str, qos);
    }
    return result;
}

int MachineObject::parse_json(std::string payload)
{
    parse_msg_count++;
    std::chrono::system_clock::time_point clock_start = std::chrono::system_clock::now();
    this->set_online_state(true);

    /* update last received time */
    last_update_time = std::chrono::system_clock::now();

    try {
        bool restored_json = false;
        json j;
        json j_pre = json::parse(payload);
        if (j_pre.empty()) {
            return 0;
        }

        if (j_pre.contains("print")) {
            if (j_pre["print"].contains("command")) {
                if (j_pre["print"]["command"].get<std::string>() == "push_status") {
                    if (j_pre["print"].contains("msg")) {
                        if (j_pre["print"]["msg"].get<int>() == 0) {           //all message
                            BOOST_LOG_TRIVIAL(trace) << "static: get push_all msg, dev_id=" << dev_id;
                            m_push_count++;
                            print_json.diff2all_base_reset(j_pre);
                        } else if (j_pre["print"]["msg"].get<int>() == 1) {    //diff message
                            if (print_json.diff2all(j_pre, j) == 0) {
                                restored_json = true;
                            } else {
                                BOOST_LOG_TRIVIAL(trace) << "parse_json: restore failed! count = " << parse_msg_count;
                                if (print_json.is_need_request()) {
                                    BOOST_LOG_TRIVIAL(trace) << "parse_json: need request pushall, count = " << parse_msg_count;
                                    // request new push
                                    GUI::wxGetApp().CallAfter([this]{
                                        this->command_request_push_all();
                                    });
                                    return -1;
                                }
                                return -1;
                            }
                        } else {
                            BOOST_LOG_TRIVIAL(warning) << "unsupported msg_type=" << j_pre["print"]["msg"].get<std::string>();
                        }
                    }
                }
            }
        }

        if (!restored_json) {
            j = json::parse(payload);
        }

        BOOST_LOG_TRIVIAL(trace) << "parse_json: dev_id=" << dev_id << ", playload=" << j.dump(4);

        if (j.contains("print")) {
            json jj = j["print"];
            if (jj.contains("command")) {
                if (jj["command"].get<std::string>() == "push_status") {
                    m_push_count++;
                    last_push_time = std::chrono::system_clock::now();
#pragma region printing
                    // U0 firmware
                    if (jj.contains("print_type")) {
                        print_type = jj["print_type"].get<std::string>();
                    }

                    if (jj.contains("home_flag")) {
                        home_flag = jj["home_flag"].get<int>();
                        parse_status(home_flag);
                    }
                    if (jj.contains("hw_switch_state")) {
                        hw_switch_state = jj["hw_switch_state"].get<int>();
                    }

                    if (jj.contains("mc_remaining_time")) {
                        if (jj["mc_remaining_time"].is_string())
                            mc_left_time = stoi(j["print"]["mc_remaining_time"].get<std::string>()) * 60;
                        else if (jj["mc_remaining_time"].is_number_integer())
                            mc_left_time = j["print"]["mc_remaining_time"].get<int>() * 60;
                    }
                    if (jj.contains("mc_percent")) {
                        if (jj["mc_percent"].is_string())
                            mc_print_percent = stoi(j["print"]["mc_percent"].get<std::string>());
                        else if (jj["mc_percent"].is_number_integer())
                            mc_print_percent = j["print"]["mc_percent"].get<int>();
                    }
                    if (jj.contains("mc_print_sub_stage")) {
                        if (jj["mc_print_sub_stage"].is_number_integer())
                            mc_print_sub_stage = j["print"]["mc_print_sub_stage"].get<int>();
                    }
                    /* printing */
                    if (jj.contains("mc_print_stage")) {
                        if (jj["mc_print_stage"].is_string())
                            mc_print_stage = atoi(jj["mc_print_stage"].get<std::string>().c_str());
                        if (jj["mc_print_stage"].is_number())
                            mc_print_stage = jj["mc_print_stage"].get<int>();
                    }
                    if (jj.contains("mc_print_error_code")) {
                        if (jj["mc_print_error_code"].is_number())
                            mc_print_error_code = jj["mc_print_error_code"].get<int>();

                    }
                    if (jj.contains("mc_print_line_number")) {
                        if (jj["mc_print_line_number"].is_string() && !jj["mc_print_line_number"].is_null())
                            mc_print_line_number = atoi(jj["mc_print_line_number"].get<std::string>().c_str());
                    }
                    if (jj.contains("print_error")) {
                        if (jj["print_error"].is_number())
                            print_error = jj["print_error"].get<int>();
                    }

#pragma endregion

#pragma region online
                    // parse online info
                    try {
                        if (jj.contains("online")) {
                            if (jj["online"].contains("ahb")) {
                                if (jj["online"]["ahb"].get<bool>()) {
                                    online_ahb = true;
                                } else {
                                    online_ahb = false;
                                }
                            }
                            if (jj["online"].contains("rfid")) {
                                if (jj["online"]["rfid"].get<bool>()) {
                                    online_rfid = true;
                                } else {
                                    online_rfid = false;
                                }
                            }
                            if (jj["online"].contains("version")) {
                                online_version = jj["online"]["version"].get<int>();
                            }
                            if (last_online_version != online_version) {
                                last_online_version = online_version;
                                GUI::wxGetApp().CallAfter([this] {
                                    this->command_get_version();
                                    });
                            }
                        }
                    } catch (...) {
                        ;
                    }
#pragma endregion

#pragma region print_task
                    if (jj.contains("printer_type")) {
                        printer_type = parse_printer_type(jj["printer_type"].get<std::string>());
                    }

                    if (jj.contains("subtask_name")) {
                        subtask_name = jj["subtask_name"].get<std::string>();
                    }
                    if (jj.contains("gcode_state")) {
                        this->set_print_state(jj["gcode_state"].get<std::string>());
                    }

                    if (jj.contains("task_id")) {
                        this->task_id_ = jj["task_id"].get<std::string>();
                    }

                    if (jj.contains("gcode_file"))
                        this->m_gcode_file = jj["gcode_file"].get<std::string>();
                    if (jj.contains("gcode_file_prepare_percent")) {
                        std::string percent_str = jj["gcode_file_prepare_percent"].get<std::string>();
                        if (!percent_str.empty()) {
                            try{
                                this->gcode_file_prepare_percent = atoi(percent_str.c_str());
                            } catch(...) {}
                        }
                    }
                    if (jj.contains("project_id")
                        && jj.contains("profile_id")
                        && jj.contains("subtask_id")
                        ){
                        obj_subtask_id = jj["subtask_id"].get<std::string>();

                        int plate_index = -1;
                        /* parse local plate_index from task */
                        if (obj_subtask_id.compare("0") == 0 && jj["profile_id"].get<std::string>() != "0") {
                            if (jj.contains("gcode_file")) {
                                m_gcode_file = jj["gcode_file"].get<std::string>();
                                int idx_start = m_gcode_file.find_last_of("_") + 1;
                                int idx_end = m_gcode_file.find_last_of(".");
                                if (idx_start > 0 && idx_end > idx_start) {
                                    try {
                                        plate_index = atoi(m_gcode_file.substr(idx_start, idx_end - idx_start).c_str());
                                    }
                                    catch (...) {
                                        ;
                                    }
                                }
                            }
                        }
                        update_slice_info(jj["project_id"].get<std::string>(), jj["profile_id"].get<std::string>(), jj["subtask_id"].get<std::string>(), plate_index);
                        BBLSubTask* curr_task = get_subtask();
                        if (curr_task) {
                            curr_task->task_progress = mc_print_percent;
                            curr_task->printing_status = print_status;
                            curr_task->task_id = jj["subtask_id"].get<std::string>();

                        }
                    }


#pragma endregion

#pragma region status
                    /* temperature */
                    if (jj.contains("bed_temper")) {
                        if (jj["bed_temper"].is_number()) {
                            bed_temp = jj["bed_temper"].get<float>();
                        }
                    }
                    if (jj.contains("bed_target_temper")) {
                        if (jj["bed_target_temper"].is_number()) {
                            bed_temp_target = jj["bed_target_temper"].get<float>();
                        }
                    }
                    if (jj.contains("frame_temper")) {
                        if (jj["frame_temper"].is_number()) {
                            frame_temp = jj["frame_temper"].get<float>();
                        }
                    }
                    if (jj.contains("nozzle_temper")) {
                        if (jj["nozzle_temper"].is_number()) {
                            nozzle_temp = jj["nozzle_temper"].get<float>();
                        }
                    }
                    if (jj.contains("nozzle_target_temper")) {
                        if (jj["nozzle_target_temper"].is_number()) {
                            nozzle_temp_target = jj["nozzle_target_temper"].get<float>();
                        }
                    }
                    if (jj.contains("chamber_temper")) {
                        if (jj["chamber_temper"].is_number()) {
                            chamber_temp = jj["chamber_temper"].get<float>();
                        }
                    }
                    /* signals */
                    if (jj.contains("link_th_state"))
                        link_th = jj["link_th_state"].get<std::string>();
                    if (jj.contains("link_ams_state"))
                        link_ams = jj["link_ams_state"].get<std::string>();
                    if (jj.contains("wifi_signal"))
                        wifi_signal = jj["wifi_signal"].get<std::string>();

                    /* cooling */
                    if (jj.contains("fan_gear")) {
                        fan_gear = jj["fan_gear"].get<std::uint32_t>();
                        big_fan2_speed = (int)((fan_gear & 0x00FF0000) >> 16);
                        big_fan1_speed = (int)((fan_gear & 0x0000FF00) >> 8);
                        cooling_fan_speed= (int)((fan_gear & 0x000000FF) >> 0);
                    }
                    else {
                        if (jj.contains("cooling_fan_speed")) {
                            cooling_fan_speed = stoi(jj["cooling_fan_speed"].get<std::string>());
                            cooling_fan_speed = round( floor(cooling_fan_speed / float(1.5)) * float(25.5) );
                        }
                        else {
                            cooling_fan_speed = 0;
                        }

                        if (jj.contains("big_fan1_speed")) {
                            big_fan1_speed = stoi(jj["big_fan1_speed"].get<std::string>());
                            big_fan1_speed = round( floor(big_fan1_speed / float(1.5)) * float(25.5) );
                        }
                        else {
                            big_fan1_speed = 0;
                        }

                        if (jj.contains("big_fan2_speed")) {
                            big_fan2_speed = stoi(jj["big_fan2_speed"].get<std::string>());
                            big_fan2_speed = round( floor(big_fan2_speed / float(1.5)) * float(25.5) );
                        }
                        else {
                            big_fan2_speed = 0;
                        }
                    }

                    if (jj.contains("heatbreak_fan_speed")) {
                        heatbreak_fan_speed = stoi(jj["heatbreak_fan_speed"].get<std::string>());
                    }
                    
                    /* parse speed */
                    try {
                        if (jj.contains("spd_lvl")) {
                            printing_speed_lvl = (PrintingSpeedLevel)jj["spd_lvl"].get<int>();
                        }
                        if (jj.contains("spd_mag")) {
                            printing_speed_mag = jj["spd_mag"].get<int>();
                        }
                    }
                    catch (...) {
                        ;
                    }

                    try {
                        if (jj.contains("stg")) {
                            stage_list_info.clear();
                            if (jj["stg"].is_array()) {
                                for (auto it = jj["stg"].begin(); it != jj["stg"].end(); it++) {
                                    for (auto kv = (*it).begin(); kv != (*it).end(); kv++) {
                                        stage_list_info.push_back(kv.value().get<int>());
                                    }
                                }
                            }
                        }
                        if (jj.contains("stg_cur")) {
                            stage_curr = jj["stg_cur"].get<int>();
                        }
                    }
                    catch (...) {
                        ;
                    }

                    /* get fimware type */
                    try {
                        if (jj.contains("lifecycle")) {
                            if (jj["lifecycle"].get<std::string>() == "engineer")
                                firmware_type = PrinterFirmwareType::FIRMWARE_TYPE_ENGINEER;
                            else if (jj["lifecycle"].get<std::string>() == "product")
                                firmware_type = PrinterFirmwareType::FIRMWARE_TYPE_PRODUCTION;
                        }
                    }
                    catch (...) {
                        ;
                    }

                    try {
                        if (jj.contains("lights_report") && jj["lights_report"].is_array()) {
                            for (auto it = jj["lights_report"].begin(); it != jj["lights_report"].end(); it++) {
                                if ((*it)["node"].get<std::string>().compare("chamber_light") == 0)
                                    chamber_light = light_effect_parse((*it)["mode"].get<std::string>());
                                if ((*it)["node"].get<std::string>().compare("work_light") == 0)
                                    work_light = light_effect_parse((*it)["mode"].get<std::string>());
                            }
                        }
                    }
                    catch (...) {
                        ;
                    }
                    // media
                    try {
                        if (jj.contains("sdcard")) {
                            if (jj["sdcard"].get<bool>())
                                sdcard_state = MachineObject::SdcardState::HAS_SDCARD_NORMAL;
                            else
                                sdcard_state = MachineObject::SdcardState::NO_SDCARD;
                        } else {
                            //do not check sdcard if no sdcard field
                            sdcard_state = MachineObject::SdcardState::NO_SDCARD;
                        }
                    }
                    catch (...) {
                        ;
                    }
#pragma endregion

#pragma region upgrade
                    try {
                        if (jj.contains("upgrade_state")) {
                            if (jj["upgrade_state"].contains("status"))
                                upgrade_status = jj["upgrade_state"]["status"].get<std::string>();
                            if (jj["upgrade_state"].contains("progress")) {
                                upgrade_progress = jj["upgrade_state"]["progress"].get<std::string>();
                            } if (jj["upgrade_state"].contains("new_version_state"))
                                upgrade_new_version = jj["upgrade_state"]["new_version_state"].get<int>() == 1 ? true : false;
                            if (jj["upgrade_state"].contains("ams_new_version_number"))
                                ams_new_version_number = jj["upgrade_state"]["ams_new_version_number"].get<std::string>();
                            if (jj["upgrade_state"].contains("ota_new_version_number"))
                                ota_new_version_number = jj["upgrade_state"]["ota_new_version_number"].get<std::string>();
                            if (jj["upgrade_state"].contains("ahb_new_version_number"))
                                ahb_new_version_number = jj["upgrade_state"]["ahb_new_version_number"].get<std::string>();
                            if (jj["upgrade_state"].contains("module"))
                                upgrade_module = jj["upgrade_state"]["module"].get<std::string>();
                            if (jj["upgrade_state"].contains("message"))
                                upgrade_message = jj["upgrade_state"]["message"].get<std::string>();
                            if (jj["upgrade_state"].contains("consistency_request"))
                                upgrade_consistency_request = jj["upgrade_state"]["consistency_request"].get<bool>();
                            if (jj["upgrade_state"].contains("force_upgrade"))
                                upgrade_force_upgrade = jj["upgrade_state"]["force_upgrade"].get<bool>();
                            if (jj["upgrade_state"].contains("err_code"))
                                upgrade_err_code = jj["upgrade_state"]["err_code"].get<int>();
                            if (jj["upgrade_state"].contains("dis_state")) {
                                if (upgrade_display_state != jj["upgrade_state"]["dis_state"].get<int>()
                                    && jj["upgrade_state"]["dis_state"].get<int>() == 3) {
                                    GUI::wxGetApp().CallAfter([this] {
                                        this->command_get_version();
                                    });
                                }
                                upgrade_display_state = jj["upgrade_state"]["dis_state"].get<int>();
                            } else {
                                //BBS compatibility with old version
                                if (upgrade_status == "DOWNLOADING"
                                    || upgrade_status == "FLASHING"
                                    || upgrade_status == "UPGRADE_REQUEST"
                                    || upgrade_status == "PRE_FLASH_START"
                                    || upgrade_status == "PRE_FLASH_SUCCESS") {
                                    upgrade_display_state = (int)UpgradingDisplayState::UpgradingInProgress;
                                }
                                else if (upgrade_status == "UPGRADE_SUCCESS"
                                    || upgrade_status == "DOWNLOAD_FAIL"
                                    || upgrade_status == "FLASH_FAIL"
                                    || upgrade_status == "PRE_FLASH_FAIL"
                                    || upgrade_status == "UPGRADE_FAIL") {
                                    upgrade_display_state = (int)UpgradingDisplayState::UpgradingFinished;
                                }
                                else {
                                    if (upgrade_new_version) {
                                        upgrade_display_state = (int)UpgradingDisplayState::UpgradingAvaliable;
                                    }
                                    else {
                                        upgrade_display_state = (int)UpgradingDisplayState::UpgradingUnavaliable;
                                    }
                                }
                            }
                            // new ver list
                            if (jj["upgrade_state"].contains("new_ver_list")) {
                                m_new_ver_list_exist = true;
                                new_ver_list.clear();
                                for (auto ver_item = jj["upgrade_state"]["new_ver_list"].begin(); ver_item != jj["upgrade_state"]["new_ver_list"].end(); ver_item++) {
                                    ModuleVersionInfo ver_info;
                                    if (ver_item->contains("name"))
                                        ver_info.name = (*ver_item)["name"].get<std::string>();
                                    else
                                        continue;

                                    if (ver_item->contains("cur_ver"))
                                        ver_info.sw_ver = (*ver_item)["cur_ver"].get<std::string>();
                                    if (ver_item->contains("new_ver"))
                                        ver_info.sw_new_ver = (*ver_item)["new_ver"].get<std::string>();

                                    if (ver_info.name == "ota") {
                                        ota_new_version_number = ver_info.sw_new_ver;
                                    }

                                    new_ver_list.insert(std::make_pair(ver_info.name, ver_info));
                                }
                            } else {
                                new_ver_list.clear();
                            }
                        }
                    }
                    catch (...) {
                        ;
                    }
#pragma endregion

#pragma region  camera
                    // parse camera info
                    try {
                        if (jj.contains("ipcam")) {
                            if (jj["ipcam"].contains("ipcam_record")) {
                                if (camera_recording_hold_count > 0)
                                    camera_recording_hold_count--;
                                else {
                                    if (jj["ipcam"]["ipcam_record"].get<std::string>() == "enable") {
                                        camera_recording_when_printing = true;
                                    }
                                    else {
                                        camera_recording_when_printing = false;
                                    }
                                }
                            }
                            if (jj["ipcam"].contains("timelapse")) {
                                if (camera_timelapse_hold_count > 0)
                                    camera_timelapse_hold_count--;
                                else {
                                    if (jj["ipcam"]["timelapse"].get<std::string>() == "enable") {
                                        camera_timelapse = true;
                                    }
                                    else {
                                        camera_timelapse = false;
                                    }
                                }
                            }
                            if (jj["ipcam"].contains("ipcam_dev")) {
                                if (jj["ipcam"]["ipcam_dev"].get<std::string>() == "1") {
                                    has_ipcam = true;
                                } else {
                                    has_ipcam = false;
                                }
                            }
                            if (jj["ipcam"].contains("resolution")) {
                                if (camera_resolution_hold_count > 0)
                                    camera_resolution_hold_count--;
                                else {
                                    camera_resolution = jj["ipcam"]["resolution"].get<std::string>();
                                }
                            }
                        }
                    }
                    catch (...) {
                        ;
                    }

                    try {
                        if (jj.contains("xcam")) {
                            if (xcam_ai_monitoring_hold_count > 0)
                                xcam_ai_monitoring_hold_count--;
                            else {
                                if (jj["xcam"].contains("printing_monitor")) {
                                    // new protocol
                                    xcam_ai_monitoring = jj["xcam"]["printing_monitor"].get<bool>();
                                } else {
                                    // old version protocol
                                    if (jj["xcam"].contains("spaghetti_detector")) {
                                        xcam_ai_monitoring = jj["xcam"]["spaghetti_detector"].get<bool>();
                                        if (jj["xcam"].contains("print_halt")) {
                                            bool print_halt = jj["xcam"]["print_halt"].get<bool>();
                                            if (print_halt) { xcam_ai_monitoring_sensitivity = "medium"; }
                                        }
                                    }
                                }
                                if (jj["xcam"].contains("halt_print_sensitivity")) {
                                    xcam_ai_monitoring_sensitivity = jj["xcam"]["halt_print_sensitivity"].get<std::string>();
                                }
                            }

                            if (xcam_first_layer_hold_count > 0)
                                xcam_first_layer_hold_count--;
                            else {
                                if (jj["xcam"].contains("first_layer_inspector")) {
                                    xcam_first_layer_inspector = jj["xcam"]["first_layer_inspector"].get<bool>();
                                }
                            }

                            if (xcam_buildplate_marker_hold_count > 0)
                                xcam_buildplate_marker_hold_count--;
                            else {
                                if (jj["xcam"].contains("buildplate_marker_detector")) {
                                    xcam_buildplate_marker_detector = jj["xcam"]["buildplate_marker_detector"].get<bool>();
                                    is_xcam_buildplate_supported = true;
                                } else {
                                    is_xcam_buildplate_supported = false;
                                }
                            }
                        }
                    }
                    catch (...) {
                        ;
                    }
#pragma endregion

#pragma region hms
                    // parse hms msg
                    try {
                        hms_list.clear();
                        if (jj.contains("hms")) {
                            if (jj["hms"].is_array()) {
                                for (auto it = jj["hms"].begin(); it != jj["hms"].end(); it++) {
                                    HMSItem item;
                                    if ((*it).contains("attr") && (*it).contains("code")) {
                                        unsigned attr = (*it)["attr"].get<unsigned>();
                                        unsigned code = (*it)["code"].get<unsigned>();
                                        item.parse_hms_info(attr, code);
                                    }
                                    hms_list.push_back(item);
                                }
                            }
                        }
                    }
                    catch (...) {
                        ;
                    }
#pragma endregion


#pragma region push_ams
                    /* ams status */
                    try {
                        if (jj.contains("ams_status")) {
                            int ams_status = jj["ams_status"].get<int>();
                            this->_parse_ams_status(ams_status);
                        }
                    }
                    catch (...) {
                        ;
                    }

                    if (jj.contains("ams")) {
                        if (jj["ams"].contains("ams")) {
                            long int last_ams_exist_bits = ams_exist_bits;
                            long int last_tray_exist_bits = tray_exist_bits;
                            long int last_is_bbl_bits = tray_is_bbl_bits;
                            long int last_read_done_bits = tray_read_done_bits;
                            long int last_ams_version = ams_version;
                            if (jj["ams"].contains("ams_exist_bits")) {
                                ams_exist_bits = stol(jj["ams"]["ams_exist_bits"].get<std::string>(), nullptr, 16);
                            }
                            if (jj["ams"].contains("tray_exist_bits")) {
                                tray_exist_bits = stol(jj["ams"]["tray_exist_bits"].get<std::string>(), nullptr, 16);
                            }
                            if (jj["ams"].contains("tray_read_done_bits")) {
                                tray_read_done_bits = stol(jj["ams"]["tray_read_done_bits"].get<std::string>(), nullptr, 16);
                            }
                            if (jj["ams"].contains("tray_reading_bits")) {
                                tray_reading_bits = stol(jj["ams"]["tray_reading_bits"].get<std::string>(), nullptr, 16);
                                ams_support_use_ams = true;
                            }
                            if (jj["ams"].contains("tray_is_bbl_bits")) {
                                tray_is_bbl_bits = stol(jj["ams"]["tray_is_bbl_bits"].get<std::string>(), nullptr, 16);
                            }
                            if (jj["ams"].contains("version")) {
                                if (jj["ams"]["version"].is_number())
                                    ams_version = jj["ams"]["version"].get<int>();
                            }
                            if (jj["ams"].contains("tray_now")) {
                                this->_parse_tray_now(jj["ams"]["tray_now"].get<std::string>());
                            }
                            if (jj["ams"].contains("tray_tar")) {
                                m_tray_tar = jj["ams"]["tray_tar"].get<std::string>();
                            }
                            if (jj["ams"].contains("ams_rfid_status"))
                                ams_rfid_status = jj["ams"]["ams_rfid_status"].get<int>();
                            if (jj["ams"].contains("humidity")) {
                                if (jj["ams"]["humidity"].is_string()) {
                                    std::string humidity_str = jj["ams"]["humidity"].get<std::string>();
                                    try {
                                        ams_humidity = atoi(humidity_str.c_str());
                                    } catch (...) {
                                        ;
                                    }
                                }
                            }

                            if (jj["ams"].contains("insert_flag") || jj["ams"].contains("power_on_flag")
                                || jj["ams"].contains("calibrate_remain_flag")) {
                                if (ams_user_setting_hold_count > 0) {
                                    ams_user_setting_hold_count--;
                                } else {
                                    if (jj["ams"].contains("insert_flag")) {
                                        ams_insert_flag = jj["ams"]["insert_flag"].get<bool>();
                                    }
                                    if (jj["ams"].contains("power_on_flag")) {
                                        ams_power_on_flag = jj["ams"]["power_on_flag"].get<bool>();
                                    }
                                    if (jj["ams"].contains("calibrate_remain_flag")) {
                                        ams_calibrate_remain_flag = jj["ams"]["calibrate_remain_flag"].get<bool>();
                                    }
                                }
                            }
                            if (ams_exist_bits != last_ams_exist_bits
                                || last_tray_exist_bits != last_tray_exist_bits
                                || tray_is_bbl_bits != last_is_bbl_bits
                                || tray_read_done_bits != last_read_done_bits
                                || last_ams_version != ams_version) {
                                is_ams_need_update = true;
                            }
                            else {
                                is_ams_need_update = false;
                            }

                            json j_ams = jj["ams"]["ams"];
                            std::set<std::string> ams_id_set;
                            for (auto it = amsList.begin(); it != amsList.end(); it++) {
                                ams_id_set.insert(it->first);
                            }
                            for (auto it = j_ams.begin(); it != j_ams.end(); it++) {
                                if (!it->contains("id")) continue;
                                std::string ams_id = (*it)["id"].get<std::string>();
                                ams_id_set.erase(ams_id);
                                Ams* curr_ams = nullptr;
                                auto ams_it = amsList.find(ams_id);
                                if (ams_it == amsList.end()) {
                                    Ams* new_ams = new Ams(ams_id);
                                    try {
                                        if (!ams_id.empty()) {
                                            int ams_id_int = atoi(ams_id.c_str());
                                            new_ams->is_exists = (ams_exist_bits & (1 << ams_id_int)) != 0 ? true : false;
                                        }
                                    }
                                    catch (...) {
                                        ;
                                    }
                                    amsList.insert(std::make_pair(ams_id, new_ams));
                                    // new ams added event
                                    curr_ams = new_ams;
                                } else {
                                    curr_ams = ams_it->second;
                                }
                                if (!curr_ams) continue;

                                if (it->contains("humidity")) {
                                    std::string humidity = (*it)["humidity"].get<std::string>();

                                    try {
                                        curr_ams->humidity = atoi(humidity.c_str());
                                    }
                                    catch (...) {
                                        ;
                                    }
                                }
                                

                                if (it->contains("tray")) {
                                    std::set<std::string> tray_id_set;
                                    for (auto it = curr_ams->trayList.begin(); it != curr_ams->trayList.end(); it++) {
                                        tray_id_set.insert(it->first);
                                    }
                                    for (auto tray_it = (*it)["tray"].begin(); tray_it != (*it)["tray"].end(); tray_it++) {
                                        if (!tray_it->contains("id")) continue;
                                        std::string tray_id = (*tray_it)["id"].get<std::string>();
                                        tray_id_set.erase(tray_id);
                                        // compare tray_list
                                        AmsTray* curr_tray = nullptr;
                                        auto tray_iter = curr_ams->trayList.find(tray_id);
                                        if (tray_iter == curr_ams->trayList.end()) {
                                            AmsTray* new_tray = new AmsTray(tray_id);
                                            curr_ams->trayList.insert(std::make_pair(tray_id, new_tray));
                                            curr_tray = new_tray;
                                        }
                                        else {
                                            curr_tray = tray_iter->second;
                                        }
                                        if (!curr_tray) continue;

                                        if (curr_tray->hold_count > 0) {
                                            curr_tray->hold_count--;
                                            continue;
                                        }

                                        curr_tray->id = (*tray_it)["id"].get<std::string>();
                                        if (tray_it->contains("tag_uid"))
                                            curr_tray->tag_uid          = (*tray_it)["tag_uid"].get<std::string>();
                                        else
                                            curr_tray->tag_uid = "0";
                                        if (tray_it->contains("tray_info_idx") && tray_it->contains("tray_type")) {
                                            curr_tray->setting_id       = (*tray_it)["tray_info_idx"].get<std::string>();
                                            std::string type            = (*tray_it)["tray_type"].get<std::string>();
                                            if (curr_tray->setting_id == "GFS00") {
                                                curr_tray->type = "PLA-S";
                                            }
                                            else if (curr_tray->setting_id == "GFS01") {
                                                curr_tray->type = "PA-S";
                                            } else {
                                                curr_tray->type = type;
                                            }
                                        } else {
                                            curr_tray->setting_id = "";
                                            curr_tray->type       = "";
                                        }
                                        if (tray_it->contains("tray_sub_brands"))
                                            curr_tray->sub_brands       = (*tray_it)["tray_sub_brands"].get<std::string>();
                                        else
                                            curr_tray->sub_brands = "";
                                        if (tray_it->contains("tray_weight"))
                                            curr_tray->weight           = (*tray_it)["tray_weight"].get<std::string>();
                                        else
                                            curr_tray->weight = "";
                                        if (tray_it->contains("tray_diameter"))
                                            curr_tray->diameter         = (*tray_it)["tray_diameter"].get<std::string>();
                                        else
                                            curr_tray->diameter = "";
                                        if (tray_it->contains("tray_temp"))
                                            curr_tray->temp             = (*tray_it)["tray_temp"].get<std::string>();
                                        else
                                            curr_tray->temp = "";
                                        if (tray_it->contains("tray_time"))
                                            curr_tray->time             = (*tray_it)["tray_time"].get<std::string>();
                                        else
                                            curr_tray->time = "";
                                        if (tray_it->contains("bed_temp_type"))
                                            curr_tray->bed_temp_type    = (*tray_it)["bed_temp_type"].get<std::string>();
                                        else
                                            curr_tray->bed_temp_type = "";
                                        if (tray_it->contains("bed_temp"))
                                            curr_tray->bed_temp         = (*tray_it)["bed_temp"].get<std::string>();
                                        else
                                            curr_tray->bed_temp = "";
                                        if (tray_it->contains("nozzle_temp_max"))
                                            curr_tray->nozzle_temp_max = (*tray_it)["nozzle_temp_max"].get<std::string>();
                                        else
                                            curr_tray->nozzle_temp_max = "";
                                        if (tray_it->contains("nozzle_temp_min"))
                                            curr_tray->nozzle_temp_min = (*tray_it)["nozzle_temp_min"].get<std::string>();
                                        else
                                            curr_tray->nozzle_temp_min = "";
                                        if (tray_it->contains("xcam_info"))
                                            curr_tray->xcam_info = (*tray_it)["xcam_info"].get<std::string>();
                                        else
                                            curr_tray->xcam_info = "";
                                        if (tray_it->contains("tray_uuid"))
                                            curr_tray->uuid = (*tray_it)["tray_uuid"].get<std::string>();
                                        else
                                            curr_tray->uuid = "0";
                                        if (tray_it->contains("tray_color")) {
                                            auto color = (*tray_it)["tray_color"].get<std::string>();
                                            curr_tray->update_color_from_str(color);
                                        } else {
                                            curr_tray->color = "";
                                        }
                                        if (tray_it->contains("remain")) {
                                            curr_tray->remain = (*tray_it)["remain"].get<int>();
                                        } else {
                                            curr_tray->remain = -1;
                                        }
                                        try {
                                            if (!ams_id.empty() && !curr_tray->id.empty()) {
                                                int ams_id_int = atoi(ams_id.c_str());
                                                int tray_id_int = atoi(curr_tray->id.c_str());
                                                curr_tray->is_exists = (tray_exist_bits & (1 << (ams_id_int * 4 + tray_id_int))) != 0 ? true : false;
                                            }
                                        }
                                        catch (...) {
                                        }
                                    }
                                    // remove not in trayList
                                    for (auto tray_it = tray_id_set.begin(); tray_it != tray_id_set.end(); tray_it++) {
                                        std::string tray_id = *tray_it;
                                        auto tray = curr_ams->trayList.find(tray_id);
                                        if (tray != curr_ams->trayList.end()) {
                                            curr_ams->trayList.erase(tray_id);
                                            BOOST_LOG_TRIVIAL(trace) << "parse_json: remove ams_id=" << ams_id << ", tray_id=" << tray_id;
                                        }
                                    }
                                }
                            }
                            // remove not in amsList
                            for (auto it = ams_id_set.begin(); it != ams_id_set.end(); it++) {
                                std::string ams_id = *it;
                                auto ams = amsList.find(ams_id);
                                if (ams != amsList.end()) {
                                    BOOST_LOG_TRIVIAL(trace) << "parse_json: remove ams_id=" << ams_id;
                                    amsList.erase(ams_id);
                                }
                            }
                        }
                    }
#pragma endregion

                } else if (jj["command"].get<std::string>() == "gcode_line") {
                    //ack of gcode_line
                    BOOST_LOG_TRIVIAL(debug) << "parse_json, ack of gcode_line = " << j.dump(4);
                } else if (jj["command"].get<std::string>() == "project_file") {
                    //ack of project file
                    BOOST_LOG_TRIVIAL(debug) << "parse_json, ack of project_file = " << j.dump(4);
                    std::string result;
                    if (jj.contains("result")) {
                        result = jj["result"].get<std::string>();
                        if (result == "FAIL") {
                            wxString text = _L("Failed to start printing job");
                            GUI::wxGetApp().show_dialog(text);
                        }
                    }
                } else if (jj["command"].get<std::string>() == "ams_filament_setting") {
                    // BBS trigger ams UI update
                    ams_version = -1;

                    if (jj["ams_id"].is_number()) {
                        int ams_id = jj["ams_id"].get<int>();
                        auto ams_it = amsList.find(std::to_string(ams_id));
                        if (ams_it != amsList.end()) {
                            int tray_id = jj["tray_id"].get<int>();
                            auto tray_it = ams_it->second->trayList.find(std::to_string(tray_id));
                            if (tray_it != ams_it->second->trayList.end()) {
                                BOOST_LOG_TRIVIAL(trace) << "ams_filament_setting, parse tray info";
                                tray_it->second->nozzle_temp_max = std::to_string(jj["nozzle_temp_max"].get<int>());
                                tray_it->second->nozzle_temp_min = std::to_string(jj["nozzle_temp_min"].get<int>());
                                tray_it->second->type = jj["tray_type"].get<std::string>();
                                tray_it->second->color = jj["tray_color"].get<std::string>();
                                tray_it->second->setting_id = jj["tray_info_idx"].get<std::string>();
                                // delay update
                                tray_it->second->set_hold_count();
                            } else {
                                BOOST_LOG_TRIVIAL(warning) << "ams_filament_setting, can not find in trayList, tray_id=" << tray_id;
                            }
                        } else {
                            BOOST_LOG_TRIVIAL(warning) << "ams_filament_setting, can not find in amsList, ams_id=" << ams_id;
                        }
                    }
                } else if (jj["command"].get<std::string>() == "xcam_control_set") {
                    if (jj.contains("module_name")) {
                        if (jj.contains("enable") || jj.contains("control")) {
                            bool enable = false;
                            if (jj.contains("enable"))
                                enable = jj["enable"].get<bool>();
                            else if (jj.contains("control"))
                                enable = jj["control"].get<bool>();
                            else {
                                ;
                            }

                            if (jj["module_name"].get<std::string>() == "first_layer_inspector") {
                                xcam_first_layer_inspector = enable;
                                xcam_first_layer_hold_count = HOLD_COUNT_MAX;
                            }
                            else if (jj["module_name"].get<std::string>() == "buildplate_marker_detector") {
                                xcam_buildplate_marker_detector = enable;
                                xcam_buildplate_marker_hold_count = HOLD_COUNT_MAX;
                            }
                            else if (jj["module_name"].get<std::string>() == "printing_monitor") {
                                xcam_ai_monitoring = enable;
                                xcam_ai_monitoring_hold_count = HOLD_COUNT_MAX;
                                if (jj.contains("halt_print_sensitivity")) {
                                    xcam_ai_monitoring_sensitivity = jj["halt_print_sensitivity"].get<std::string>();
                                }
                            }
                            else if (jj["module_name"].get<std::string>() == "spaghetti_detector") {
                                // old protocol
                                xcam_ai_monitoring = enable;
                                xcam_ai_monitoring_hold_count = HOLD_COUNT_MAX;
                                if (jj.contains("print_halt")) {
                                    if (jj["print_halt"].get<bool>())
                                        xcam_ai_monitoring_sensitivity = "medium";
                                }
                            }
                        }
                    }
                }else if(jj["command"].get<std::string>() == "print_option") {
                     try {
                          if (jj.contains("option")) {
                              if (jj["option"].is_number()) {
                                  int option = jj["option"].get<int>();
                                  _parse_print_option_ack(option);
                              }
                          }
                          if (jj.contains("auto_recovery")) {
                              xcam_auto_recovery_step_loss = jj["auto_recovery"].get<bool>();
                          }
                     }
                     catch(...) {
                     }
                }
            }
        }

        try {
            if (j.contains("info")) {
                if (j["info"].contains("command") && j["info"]["command"].get<std::string>() == "get_version") {
                    json j_module = j["info"]["module"];
                    module_vers.clear();
                    for (auto it = j_module.begin(); it != j_module.end(); it++) {
                        ModuleVersionInfo ver_info;
                        ver_info.name = (*it)["name"].get<std::string>();
                        if ((*it).contains("sw_ver"))
                            ver_info.sw_ver = (*it)["sw_ver"].get<std::string>();
                        if ((*it).contains("sn"))
                            ver_info.sn = (*it)["sn"].get<std::string>();
                        if ((*it).contains("hw_ver"))
                            ver_info.hw_ver = (*it)["hw_ver"].get<std::string>();
                        module_vers.emplace(ver_info.name, ver_info);
                    }
                    parse_version_func();
                    bool get_version_result = true;
                    if (j["info"].contains("result"))
                        if (j["info"]["result"].get<std::string>() == "fail")
                            get_version_result = false;
                    if ((!check_version_valid() && get_version_retry-- >= 0)
                        && get_version_result) {
                            BOOST_LOG_TRIVIAL(info) << "get_version_retry = " << get_version_retry;
                            boost::thread retry = boost::thread([this] {
                                boost::this_thread::sleep_for(boost::chrono::milliseconds(RETRY_INTERNAL));
                                GUI::wxGetApp().CallAfter([this] {
                                    this->command_get_version(false);
                            });
                        });
                    }
                }
            }
        } catch (...) {}

        try {
            if (j.contains("camera")) {
                if (j["camera"].contains("command")) {
                    if (j["camera"]["command"].get<std::string>() == "ipcam_timelapse") {
                        if (j["camera"]["control"].get<std::string>() == "enable")
                            this->camera_timelapse = true;
                        if (j["camera"]["control"].get<std::string>() == "disable")
                            this->camera_timelapse = false;
                        BOOST_LOG_TRIVIAL(info) << "ack of timelapse = " << camera_timelapse;
                    } else if (j["camera"]["command"].get<std::string>() == "ipcam_record_set") {
                        if (j["camera"]["control"].get<std::string>() == "enable")
                            this->camera_recording_when_printing = true;
                        if (j["camera"]["control"].get<std::string>() == "disable")
                            this->camera_recording_when_printing = false;
                        BOOST_LOG_TRIVIAL(info) << "ack of ipcam_record_set " << camera_recording_when_printing;
                    } else if (j["camera"]["command"].get<std::string>() == "ipcam_resolution_set") {
                        this->camera_resolution = j["camera"]["resolution"].get<std::string>();
                        BOOST_LOG_TRIVIAL(info) << "ack of resolution = " << camera_resolution;
                    }
                }
            }
        } catch (...) {}


        // event info
        try {
            if (j.contains("event")) {
                if (j["event"].contains("event")) {
                    if (j["event"]["event"].get<std::string>() == "client.disconnected")
                        set_online_state(false);
                    else if (j["event"]["event"].get<std::string>() == "client.connected")
                        set_online_state(true);
                }
            }
        }
        catch (...)  {}

        parse_state_changed_event();
    }
    catch (...) {
        BOOST_LOG_TRIVIAL(trace) << "parse_json failed! dev_id=" << this->dev_id <<", payload = " << payload;
    }

    std::chrono::system_clock::time_point clock_stop = std::chrono::system_clock::now();
    auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(clock_stop - clock_start);
    if (diff.count() > 10.0f) {
        BOOST_LOG_TRIVIAL(trace) << "parse_json timeout = " << diff.count();
    }
    return 0;
}

int MachineObject::publish_gcode(std::string gcode_str)
{
    json j;
    j["print"]["command"] = "gcode_line";
    j["print"]["param"] = gcode_str;
    j["print"]["sequence_id"] = std::to_string(MachineObject::m_sequence_id++);

    if (m_agent)
        j["print"]["user_id"] = m_agent->get_user_id();
    return publish_json(j.dump());
}

BBLSubTask* MachineObject::get_subtask()
{
    if (!subtask_)
        subtask_ = new BBLSubTask(nullptr);
    return subtask_;
}

void MachineObject::update_slice_info(std::string project_id, std::string profile_id, std::string subtask_id, int plate_idx)
{
    if (!m_agent) return;

    if (project_id_ != project_id || profile_id_ != profile_id || slice_info == nullptr || subtask_id_ != subtask_id) {
        project_id_ = project_id;
        profile_id_ = profile_id;
        subtask_id_ = subtask_id;

        if (project_id.empty()
            || profile_id.empty()
            || subtask_id.empty()) {
            return;
        }

        if (project_id.compare("0") == 0
            || profile_id.compare("0") == 0) return;

        BOOST_LOG_TRIVIAL(trace) << "slice_info: start";
        slice_info = new BBLSliceInfo();
        get_slice_info_thread = new boost::thread([this, project_id, profile_id, subtask_id, plate_idx] {
                int plate_index = -1;

                if (!m_agent) return;

                if (plate_idx >= 0) {
                    plate_index = plate_idx;
                } else {
                    if (subtask_id.compare("0") == 0)
                        return;
                    m_agent->get_task_plate_index(subtask_id, &plate_index);
                }

                if (plate_index >= 0) {
                    std::string slice_json;
                    m_agent->get_slice_info(project_id, profile_id, plate_index, &slice_json);
                    if (slice_json.empty()) return;
                    //parse json
                    try {
                        json j = json::parse(slice_json);
                        if (!j["prediction"].is_null())
                            slice_info->prediction = j["prediction"].get<int>();
                        if (!j["weight"].is_null())
                            slice_info->weight = j["weight"].get<float>();
                        if (!j["thumbnail"].is_null()) {
                            slice_info->thumbnail_url = j["thumbnail"]["url"].get<std::string>();
                            BOOST_LOG_TRIVIAL(trace) << "slice_info: thumbnail url=" << slice_info->thumbnail_url;
                        }
                        if (!j["filaments"].is_null()) {
                            for (auto filament : j["filaments"]) {
                                FilamentInfo f;
                                f.color = filament["color"].get<std::string>();
                                f.type = filament["type"].get<std::string>();
                                f.used_g = stof(filament["used_g"].get<std::string>());
                                f.used_m = stof(filament["used_m"].get<std::string>());
                                slice_info->filaments_info.push_back(f);
                            }
                        }
                    } catch(...) {
                        ;
                    }
                }
            });
    }
}

void MachineObject::get_firmware_info()
{
    m_firmware_valid = false;
    if (m_firmware_thread_started)
        return;

    boost::thread update_info_thread = Slic3r::create_thread(
        [&] {
            m_firmware_thread_started = true;
            int          result = 0;
            unsigned int http_code;
            std::string  http_body;
            if (!m_agent) return;
            result = m_agent->get_printer_firmware(dev_id, &http_code, &http_body);
            if (result < 0) {
                // get upgrade list failed
                return;
            }
            try {
                json j = json::parse(http_body);
                if (j.contains("devices") && !j["devices"].is_null()) {
                    firmware_list.clear();
                    for (json::iterator it = j["devices"].begin(); it != j["devices"].end(); it++) {
                        if ((*it)["dev_id"].get<std::string>() == this->dev_id) {
                            try {
                                json firmware = (*it)["firmware"];
                                for (json::iterator firmware_it = firmware.begin(); firmware_it != firmware.end(); firmware_it++) {
                                    FirmwareInfo item;
                                    item.version = (*firmware_it)["version"].get<std::string>();
                                    item.url = (*firmware_it)["url"].get<std::string>();
                                    if ((*firmware_it).contains("description"))
                                        item.description = (*firmware_it)["description"].get<std::string>();
                                    item.module_type = "ota";
                                    int name_start = item.url.find_last_of('/') + 1;
                                    if (name_start > 0) {
                                        item.name = item.url.substr(name_start, item.url.length() - name_start);
                                        firmware_list.push_back(item);
                                    }
                                    else {
                                        BOOST_LOG_TRIVIAL(trace) << "skip";
                                    }
                                }
                            }
                            catch (...) {}
                            try {
                                if ((*it).contains("ams")) {
                                    json ams_list = (*it)["ams"];
                                    if (ams_list.size() > 0) {
                                        auto ams_front = ams_list.front();
                                        json firmware_ams = (ams_front)["firmware"];
                                        for (json::iterator ams_it = firmware_ams.begin(); ams_it != firmware_ams.end(); ams_it++) {
                                            FirmwareInfo item;
                                            item.version = (*ams_it)["version"].get<std::string>();
                                            item.url = (*ams_it)["url"].get<std::string>();
                                            if ((*ams_it).contains("description"))
                                                item.description = (*ams_it)["description"].get<std::string>();
                                            item.module_type = "ams";
                                            int name_start = item.url.find_last_of('/') + 1;
                                            if (name_start > 0) {
                                                item.name = item.url.substr(name_start, item.url.length() - name_start);
                                                firmware_list.push_back(item);
                                            }
                                            else {
                                                BOOST_LOG_TRIVIAL(trace) << "skip";
                                            }
                                        }
                                    }
                                }
                            }
                            catch (...) {
                                ;
                            }
                        }
                    }
                }
            }
            catch (...) {
                return;
            }
            m_firmware_thread_started = false;
            m_firmware_valid = true;
        }
    );
    return;
}

bool MachineObject::is_firmware_info_valid()
{
    return m_firmware_valid;
}

DeviceManager::DeviceManager(NetworkAgent* agent)
{
    m_agent = agent;
}

DeviceManager::~DeviceManager()
{
    for (auto it = localMachineList.begin(); it != localMachineList.end(); it++) {
        if (it->second) {
            delete it->second;
            it->second = nullptr;
        }
    }
    localMachineList.clear();

    for (auto it = userMachineList.begin(); it != userMachineList.end(); it++) {
        if (it->second) {
            delete it->second;
            it->second = nullptr;
        }
    }
    userMachineList.clear();
}

void DeviceManager::set_agent(NetworkAgent* agent)
{
    m_agent = agent;
}

void DeviceManager::check_pushing()
{
    MachineObject* obj = this->get_selected_machine();
    if (obj) {
        std::chrono::system_clock::time_point start = std::chrono::system_clock::now();
        auto internal = std::chrono::duration_cast<std::chrono::milliseconds>(start - obj->last_update_time);
        if (internal.count() > TIMEOUT_FOR_STRAT && internal.count() < 1000 * 60 * 60 * 300) {
            BOOST_LOG_TRIVIAL(info) << "command_pushing: diff = " << internal.count();
            obj->command_pushing("start");
        }
    }
}

void DeviceManager::on_machine_alive(std::string json_str)
{
    try {
        BOOST_LOG_TRIVIAL(trace) << "DeviceManager::SsdpDiscovery, json" << json_str;
        json j = json::parse(json_str);
        std::string dev_name        = j["dev_name"].get<std::string>();
        std::string dev_id          = j["dev_id"].get<std::string>();
        std::string dev_ip          = j["dev_ip"].get<std::string>();
        std::string printer_type_str= j["dev_type"].get<std::string>();
        std::string printer_signal  = j["dev_signal"].get<std::string>();
        std::string connect_type    = j["connect_type"].get<std::string>();
        std::string bind_state      = j["bind_state"].get<std::string>();

        MachineObject* obj;

        /* update userMachineList info */
        auto it = userMachineList.find(dev_id);
        if (it != userMachineList.end()) {
            it->second->dev_ip = dev_ip;
            it->second->bind_state = bind_state;
        }

        /* update localMachineList */
        it = localMachineList.find(dev_id);
        if (it != localMachineList.end()) {
            // update properties
            /* ip changed */
            obj = it->second;
            if (obj->dev_ip.compare(dev_ip) != 0 && !obj->dev_ip.empty()) {
                BOOST_LOG_TRIVIAL(info) << "MachineObject IP changed from " << obj->dev_ip << " to " << dev_ip;
                obj->dev_ip = dev_ip;
                /* ip changed reconnect mqtt */
            }
            obj->wifi_signal = printer_signal;
            obj->dev_connection_type = connect_type;
            obj->bind_state = bind_state;
            obj->printer_type = MachineObject::parse_printer_type(printer_type_str);

            // U0 firmware
            if (obj->dev_connection_type.empty() && obj->bind_state.empty())
                obj->bind_state = "free";

            BOOST_LOG_TRIVIAL(debug) << "SsdpDiscovery:: Update Machine Info, printer_sn = " << dev_id << ", signal = " << printer_signal;
            obj->last_alive = Slic3r::Utils::get_current_time_utc();
            obj->m_is_online = true;
        }
        else {
            /* insert a new machine */
            obj = new MachineObject(m_agent, dev_name, dev_id, dev_ip);
            obj->printer_type = MachineObject::parse_printer_type(printer_type_str);
            obj->wifi_signal = printer_signal;
            obj->dev_connection_type = connect_type;
            obj->bind_state     = bind_state;

            //load access code
            AppConfig* config = Slic3r::GUI::wxGetApp().app_config;
            if (config) {
                obj->access_code = Slic3r::GUI::wxGetApp().app_config->get("access_code", dev_id);
            }
            localMachineList.insert(std::make_pair(dev_id, obj));


            BOOST_LOG_TRIVIAL(debug) << "SsdpDiscovery::New Machine, ip = " << dev_ip << ", printer_name= " << dev_name << ", printer_type = " << printer_type_str << ", signal = " << printer_signal;
        }
    }
    catch (...) {
        ;
    }
}

void DeviceManager::disconnect_all()
{

}

int DeviceManager::query_bind_status(std::string &msg)
{
    if (!m_agent) {
        msg = "";
        return -1;
    }

    BOOST_LOG_TRIVIAL(trace) << "DeviceManager::query_bind_status";
    std::map<std::string, MachineObject*>::iterator it;
    std::vector<std::string> query_list;
    for (it = localMachineList.begin(); it != localMachineList.end(); it++) {
        query_list.push_back(it->first);
    }

    unsigned int http_code;
    std::string http_body;
    int result = m_agent->query_bind_status(query_list, &http_code, &http_body);

    if (result < 0) {
        msg = (boost::format("code=%1%,body=%2") % http_code % http_body).str();
    } else {
        msg = "";
        try {
            json j = json::parse(http_body);
            if (j.contains("bind_list")) {

                for (auto& item : j["bind_list"]) {
                    auto it = localMachineList.find(item["dev_id"].get<std::string>());
                    if (it != localMachineList.end()) {
                        if (!item["user_id"].is_null())
                            it->second->bind_user_id = item["user_id"].get<std::string>();
                        if (!item["user_name"].is_null())
                            it->second->bind_user_name = item["user_name"].get<std::string>();
                        else
                            it->second->bind_user_name = "Free";
                    }
                }
            }
        } catch(...) {
            ;
        }
    }
    return result;
}

MachineObject* DeviceManager::get_local_selected_machine()
{
    return get_local_machine(local_selected_machine);
}

MachineObject* DeviceManager::get_default_machine() {

    std::string dev_id;
    if (m_agent) {
        m_agent->get_user_selected_machine();
    }
    if (dev_id.empty()) return nullptr;

    auto it = userMachineList.find(dev_id);
    if (it == userMachineList.end()) return nullptr;
    return it->second;
}

MachineObject* DeviceManager::get_local_machine(std::string dev_id)
{
    if (dev_id.empty()) return nullptr;
    auto it = localMachineList.find(dev_id);
    if (it == localMachineList.end()) return nullptr;
    return it->second;
}

void DeviceManager::erase_user_machine(std::string dev_id)
{
    userMachineList.erase(dev_id);
}

MachineObject* DeviceManager::get_user_machine(std::string dev_id)
{
    if (!Slic3r::GUI::wxGetApp().is_user_login())
        return nullptr;

    std::map<std::string, MachineObject*>::iterator it = userMachineList.find(dev_id);
    if (it == userMachineList.end()) return nullptr;
    return it->second;
}

MachineObject* DeviceManager::get_my_machine(std::string dev_id)
{
    auto list = get_my_machine_list();
    auto it = list.find(dev_id);
    if (it != list.end()) {
        return it->second;
    }
    return nullptr;
}

void DeviceManager::clean_user_info()
{
    BOOST_LOG_TRIVIAL(trace) << "DeviceManager::clean_user_info";
    // reset selected_machine
    selected_machine = "";
    local_selected_machine = "";

    // clean access code
    for (auto it = userMachineList.begin(); it != userMachineList.end(); it++) {
        it->second->set_access_code("");
    }
    // clean user list
    for (auto it = userMachineList.begin(); it != userMachineList.end(); it++) {
        if (it->second) {
            delete it->second;
            it->second = nullptr;
        }
    }
    userMachineList.clear();
}

bool DeviceManager::set_selected_machine(std::string dev_id)
{
    BOOST_LOG_TRIVIAL(info) << "set_selected_machine=" << dev_id;
    auto my_machine_list = get_my_machine_list();
    auto it = my_machine_list.find(dev_id);
    if (it != my_machine_list.end()) {
        if (selected_machine == dev_id) {
            if (it->second->connection_type() != "lan") {
                // only reset update time
                it->second->reset_update_time();
                return true;
            } else {
                // lan mode printer reconnect printer
                if (m_agent) {
                    m_agent->disconnect_printer();
                    it->second->reset();
                    it->second->connect();
                    it->second->set_lan_mode_connection_state(true);
                }
            }
        } else {
            if (m_agent) {
                if (it->second->connection_type() != "lan" || it->second->connection_type().empty()) {
                    if (m_agent->get_user_selected_machine() != dev_id) {
                        BOOST_LOG_TRIVIAL(info) << "static: set_selected_machine: same dev_id = " << dev_id;
                        m_agent->set_user_selected_machine(dev_id);
                        it->second->reset();
                    } else {
                        it->second->reset_update_time();
                    }
                } else {
                    m_agent->disconnect_printer();
                    it->second->reset();
                    it->second->connect();
                    it->second->set_lan_mode_connection_state(true);
                }
            }
        }
    }
    selected_machine = dev_id;
    return true;
}

MachineObject* DeviceManager::get_selected_machine()
{
    if (selected_machine.empty()) return nullptr;

    MachineObject* obj = get_user_machine(selected_machine);
    if (obj)
        return obj;

    // return local machine has access code
    auto it = localMachineList.find(selected_machine);
    if (it != localMachineList.end()) {
        if (it->second->has_access_right())
            return it->second;
    }
    return nullptr;
}

std::map<std::string, MachineObject*> DeviceManager::get_my_machine_list()
{
    std::map<std::string, MachineObject*> result;

    for (auto it = userMachineList.begin(); it != userMachineList.end(); it++) {
        if (!it->second)
            continue;
        if (!it->second->is_lan_mode_printer())
            result.insert(std::make_pair(it->first, it->second));
    }

    for (auto it = localMachineList.begin(); it != localMachineList.end(); it++) {
        if (!it->second)
            continue;
        if (it->second->has_access_right() && it->second->is_avaliable() && it->second->is_lan_mode_printer()) {
            // remove redundant in userMachineList
            if (result.find(it->first) == result.end()) {
                result.emplace(std::make_pair(it->first, it->second));
            }
        }
    }
    return result;
}

std::string DeviceManager::get_first_online_user_machine() {
    for (auto it = userMachineList.begin(); it != userMachineList.end(); it++) {
        if (it->second && it->second->is_online()) {
            return it->second->dev_id;
        }
    }
    return "";
}

void DeviceManager::modify_device_name(std::string dev_id, std::string dev_name)
{
    BOOST_LOG_TRIVIAL(trace) << "modify_device_name";
    if (m_agent) {
        int result = m_agent->modify_printer_name(dev_id, dev_name);
        if (result == 0) {
            update_user_machine_list_info();
        }
    }
}

void DeviceManager::parse_user_print_info(std::string body)
{
    BOOST_LOG_TRIVIAL(trace) << "DeviceManager::parse_user_print_info";
    std::lock_guard<std::mutex> lock(listMutex);
    std::set<std::string> new_list;
    try {
        json j = json::parse(body);
        if (j.contains("devices") && !j["devices"].is_null()) {
            for (auto& elem : j["devices"]) {
                MachineObject* obj = nullptr;
                std::string dev_id;
                if (!elem["dev_id"].is_null()) {
                    dev_id = elem["dev_id"].get<std::string>();
                    new_list.insert(dev_id);
                }
                std::map<std::string, MachineObject*>::iterator iter = userMachineList.find(dev_id);
                if (iter != userMachineList.end()) {
                    /* update field */
                    obj = iter->second;
                    obj->dev_id = dev_id;
                }
                else {
                    obj = new MachineObject(m_agent, "", "", "");
                    if (m_agent) {
                        obj->set_bind_status(m_agent->get_user_name());
                    }
                    userMachineList.insert(std::make_pair(dev_id, obj));
                }

                if (!obj) continue;

                if (!elem["dev_id"].is_null())
                    obj->dev_id = elem["dev_id"].get<std::string>();
                if (!elem["dev_name"].is_null())
                    obj->dev_name = elem["dev_name"].get<std::string>();
                if (!elem["dev_online"].is_null())
                    obj->m_is_online = elem["dev_online"].get<bool>();
                if (elem.contains("dev_model_name") && !elem["dev_model_name"].is_null())
                    obj->printer_type = elem["dev_model_name"].get<std::string>();
                if (!elem["task_status"].is_null())
                    obj->iot_print_status = elem["task_status"].get<std::string>();
                if (elem.contains("dev_product_name") && !elem["dev_product_name"].is_null())
                    obj->product_name = elem["dev_product_name"].get<std::string>();
                if (elem.contains("dev_access_code") && !elem["dev_access_code"].is_null()) {
                    std::string acc_code = elem["dev_access_code"].get<std::string>();
                    acc_code.erase(std::remove(acc_code.begin(), acc_code.end(), '\n'), acc_code.end());
                    obj->set_access_code(acc_code);
                }
            }

            //remove MachineObject from userMachineList
            std::map<std::string, MachineObject*>::iterator iterat;
            for (iterat = userMachineList.begin(); iterat != userMachineList.end(); ) {
                if (new_list.find(iterat->first) == new_list.end()) {
                    iterat = userMachineList.erase(iterat);
                }
                else {
                    iterat++;
                }
            }
        }
    }
    catch (std::exception& e) {
        ;
    }
}

void DeviceManager::update_user_machine_list_info()
{
    if (!m_agent) return;

    BOOST_LOG_TRIVIAL(debug) << "update_user_machine_list_info";
    unsigned int http_code;
    std::string body;
    int result = m_agent->get_user_print_info(&http_code, &body);
    if (result == 0) {
        parse_user_print_info(body);
    }
}


std::map<std::string ,MachineObject*> DeviceManager::get_local_machine_list()
{
    std::map<std::string, MachineObject*> result;
    std::map<std::string, MachineObject*>::iterator it;

    for (it = localMachineList.begin(); it != localMachineList.end(); it++) {
        if (it->second->m_is_online) {
            result.insert(std::make_pair(it->first, it->second));
        }
    }

    return result;
}

void DeviceManager::load_last_machine()
{
    if (userMachineList.empty()) return;

    else if (userMachineList.size() == 1) {
        this->set_selected_machine(userMachineList.begin()->second->dev_id);
    } else {
        if (m_agent) {
            std::string last_monitor_machine = m_agent->get_user_selected_machine();
            bool found = false;
            for (auto it = userMachineList.begin(); it != userMachineList.end(); it++) {
                if (last_monitor_machine == it->first) {
                    this->set_selected_machine(last_monitor_machine);
                    found = true;
                }
            }
            if (!found)
                this->set_selected_machine(userMachineList.begin()->second->dev_id);
        }
    }
}

json DeviceManager::function_table = json::object();

std::string DeviceManager::parse_printer_type(std::string type_str)
{
    if (DeviceManager::function_table.contains("printers")) {
        for (auto printer : DeviceManager::function_table["printers"]) {
            if (printer.contains("model_id") && printer["model_id"].get<std::string>() == type_str) {
                if (printer.contains("printer_type")) {
                    return printer["printer_type"].get<std::string>();
                }
            }
        }
    }
    return "";
}

std::string DeviceManager::get_printer_display_name(std::string type_str)
{
    if (DeviceManager::function_table.contains("printers")) {
        for (auto printer : DeviceManager::function_table["printers"]) {
            if (printer.contains("model_id") && printer["model_id"].get<std::string>() == type_str) {
                if (printer.contains("display_name")) {
                    return printer["display_name"].get<std::string>();
                }
            }
        }
    }
    return "";
}

std::string DeviceManager::get_printer_thumbnail_img(std::string type_str)
{
    if (DeviceManager::function_table.contains("printers")) {
        for (auto printer : DeviceManager::function_table["printers"]) {
            if (printer.contains("model_id") && printer["model_id"].get<std::string>() == type_str) {
                if (printer.contains("printer_thumbnail_image")) {
                    return printer["printer_thumbnail_image"].get<std::string>();
                }
            }
        }
    }
    return "";
}

bool DeviceManager::is_function_supported(std::string type_str, std::string function_name)
{
    if (DeviceManager::function_table.contains("printers")) {
        for (auto printer : DeviceManager::function_table["printers"]) {
            if (printer.contains("model_id") && printer["model_id"].get<std::string>() == type_str) {
                if (printer.contains("func")) {
                    if (printer["func"].contains(function_name))
                        return printer["func"][function_name].get<bool>();
                }
            }
        }
    }
    return true;
}

std::vector<std::string> DeviceManager::get_resolution_supported(std::string type_str)
{
    std::vector<std::string> resolution_supported;
    if (DeviceManager::function_table.contains("printers")) {
        for (auto printer : DeviceManager::function_table["printers"]) {
            if (printer.contains("model_id") && printer["model_id"].get<std::string>() == type_str) {
                if (printer.contains("camera_resolution")) {
                    for (auto res : printer["camera_resolution"])
                        resolution_supported.emplace_back(res.get<std::string>());
                }
            }
        }
    }
    return resolution_supported;
}

bool DeviceManager::get_bed_temperature_limit(std::string type_str, int &limit)
{
    bool result = false;
    if (DeviceManager::function_table.contains("printers")) {
        for (auto printer : DeviceManager::function_table["printers"]) {
            if (printer.contains("model_id") && printer["model_id"].get<std::string>() == type_str) {
                if (printer.contains("bed_temperature_limit")) {
                    limit = printer["bed_temperature_limit"].get<int>();
                    return true;
                }
            }
        }
    }
    return result;
}

bool DeviceManager::load_functional_config(std::string config_file)
{
    std::ifstream json_file(config_file.c_str());
    try {
        if (json_file.is_open()) {
            json_file >> DeviceManager::function_table;
            return true;
        } else {
            BOOST_LOG_TRIVIAL(error) << "load functional config failed, file = " << config_file;
        }
    }
    catch(...) {
        BOOST_LOG_TRIVIAL(error) << "load functional config failed, file = " << config_file;
        return false;
    }
    return true;
}

} // namespace Slic3r
