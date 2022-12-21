#ifndef slic3r_DeviceManager_hpp_
#define slic3r_DeviceManager_hpp_

#include <map>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <boost/thread.hpp>
#include "nlohmann/json.hpp"
#include "libslic3r/ProjectTask.hpp"
#include "slic3r/Utils/json_diff.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"
#include "CameraPopup.hpp"

#define USE_LOCAL_SOCKET_BIND 0

#define DISCONNECT_TIMEOUT      30000.f     // milliseconds
#define PUSHINFO_TIMEOUT        15000.f     // milliseconds
#define TIMEOUT_FOR_STRAT       20000.f     // milliseconds
#define REQUEST_PUSH_MIN_TIME   15000.f     // milliseconds
#define REQUEST_START_MIN_TIME  15000.f     // milliseconds

#define FILAMENT_MAX_TEMP       300
#define FILAMENT_DEF_TEMP       220
#define FILAMENT_MIN_TEMP       120
#define BED_TEMP_LIMIT          120

#define HOLD_COUNT_MAX          3
#define HOLD_COUNT_CAMERA       6
#define GET_VERSION_RETRYS      10
#define RETRY_INTERNAL          2000

inline int correct_filament_temperature(int filament_temp)
{
    int temp = std::min(filament_temp, FILAMENT_MAX_TEMP);
    temp     = std::max(temp, FILAMENT_MIN_TEMP);
    return temp;
}

wxString get_stage_string(int stage);

using namespace nlohmann;

namespace Slic3r {

enum PRINTING_STAGE {
    PRINTING_STAGE_PRINTING = 0,
    PRINTING_STAGE_BED_LEVELING,
    PRINTING_STAGE_HEADBED,
    PRINTING_STAGE_XY_MECH_MODE,
    PRINTING_STAGE_CHANGE_MATERIAL,
    PRINTING_STAGE_M400_PAUSE,
    PRINTING_STAGE_FILAMENT_RUNOUT_PAUSE,
    PRINTING_STAGE_HOTEND_HEATING,
    PRINTING_STAGE_EXTRUDER_SCAN,
    PRINTING_STAGE_BED_SCAN,
    PRINTING_STAGE_FIRST_LAYER_SCAN,
    PRINTING_STAGE_SURFACE_TYPE_IDENT,
    PRINTING_STAGE_SCANNER_PARAM_CALI,
    PRINTING_STAGE_TOOHEAD_HOMING,
    PRINTING_STAGE_NOZZLE_TIP_CLEANING,
    PRINTING_STAGE_COUNT
};

enum PrinterFunction {
    FUNC_MONITORING = 0,
    FUNC_TIMELAPSE,
    FUNC_RECORDING,
    FUNC_FIRSTLAYER_INSPECT,
    FUNC_AI_MONITORING,
    FUNC_BUILDPLATE_MARKER_DETECT,
    FUNC_AUTO_RECOVERY_STEP_LOSS,
    FUNC_FLOW_CALIBRATION,
    FUNC_AUTO_LEVELING,
    FUNC_CHAMBER_TEMP,
    FUNC_CAMERA_VIDEO,
    FUNC_MEDIA_FILE,
    FUNC_REMOTE_TUNNEL,
    FUNC_LOCAL_TUNNEL,
    FUNC_PRINT_WITHOUT_SD,
    FUNC_VIRTUAL_CAMERA,
    FUNC_USE_AMS,
    FUNC_ALTER_RESOLUTION,
    FUNC_SEND_TO_SDCARD,
    FUNC_AUTO_SWITCH_FILAMENT,
    FUNC_CHAMBER_FAN,
    FUNC_MAX
};


enum PrintingSpeedLevel {
    SPEED_LEVEL_INVALID = 0,
    SPEED_LEVEL_SILENCE = 1,
    SPEED_LEVEL_NORMAL = 2,
    SPEED_LEVEL_RAPID = 3,
    SPEED_LEVEL_RAMPAGE = 4,
    SPEED_LEVEL_COUNT
};

class NetworkAgent;


enum AmsRfidState {
    AMS_RFID_INIT,
    AMS_RFID_LOADING,
    AMS_REID_DONE,
};

enum AmsStep {
    AMS_STEP_INIT,
    AMS_STEP_HEAT_EXTRUDER,
    AMS_STEP_LOADING,
    AMS_STEP_COMPLETED,
};

enum AmsRoadPosition {
    AMS_ROAD_POSITION_TRAY,     // filament at tray
    AMS_ROAD_POSITION_TUBE,     // filament at tube
    AMS_ROAD_POSITION_HOTEND,   // filament at hotend
};

enum AmsStatusMain {
    AMS_STATUS_MAIN_IDLE                = 0x00,
    AMS_STATUS_MAIN_FILAMENT_CHANGE     = 0x01,
    AMS_STATUS_MAIN_RFID_IDENTIFYING    = 0x02,
    AMS_STATUS_MAIN_ASSIST              = 0x03,
    AMS_STATUS_MAIN_CALIBRATION         = 0x04,
    AMS_STATUS_MAIN_SELF_CHECK          = 0x10,
    AMS_STATUS_MAIN_DEBUG               = 0x20,
    AMS_STATUS_MAIN_UNKNOWN             = 0xFF,
};

enum AmsRfidStatus {
    AMS_RFID_IDLE           = 0,
    AMS_RFID_READING        = 1,
    AMS_RFID_GCODE_TRANS    = 2,
    AMS_RFID_GCODE_RUNNING  = 3,
    AMS_RFID_ASSITANT       = 4,
    AMS_RFID_SWITCH_FILAMENT= 5,
    AMS_RFID_HAS_FILAMENT   = 6
};

enum AmsOptionType {
    AMS_OP_STARTUP_READ,
    AMS_OP_TRAY_READ,
    AMS_OP_CALIBRATE_REMAIN
};

class AmsTray {
public:
    AmsTray(std::string tray_id) {
        is_bbl          = false;
        id              = tray_id;
        road_position   = AMS_ROAD_POSITION_TRAY;
        step_state      = AMS_STEP_INIT;
        rfid_state      = AMS_RFID_INIT;
    }

    static int hex_digit_to_int(const char c)
    {
        return (c >= '0' && c <= '9') ? int(c - '0') : (c >= 'A' && c <= 'F') ? int(c - 'A') + 10 : (c >= 'a' && c <= 'f') ? int(c - 'a') + 10 : -1;
    }

    static wxColour decode_color(const std::string &color)
    {
        std::array<int, 3> ret = {0, 0, 0};
        const char *       c   = color.data();
        if (color.size() == 8) {
            for (size_t j = 0; j < 3; ++j) {
                int digit1 = hex_digit_to_int(*c++);
                int digit2 = hex_digit_to_int(*c++);
                if (digit1 == -1 || digit2 == -1) break;
                ret[j] = float(digit1 * 16 + digit2);
            }
        }
        return wxColour(ret[0], ret[1], ret[2]);
    }

    std::string     id;
    std::string     tag_uid;    // tag_uid
    std::string     setting_id; // tray_info_idx
    std::string     type;
    std::string     sub_brands;
    std::string     color;
    std::string     weight;
    std::string     diameter;
    std::string     temp;
    std::string     time;
    std::string     bed_temp_type;
    std::string     bed_temp;
    std::string     nozzle_temp_max;
    std::string     nozzle_temp_min;
    std::string     xcam_info;
    std::string     uuid;

    wxColour        wx_color;
    bool            is_bbl;
    bool            is_exists = false;
    int             hold_count = 0;
    int             remain = 0;         // filament remain: 0 ~ 100

    AmsRoadPosition road_position;
    AmsStep         step_state;
    AmsRfidState    rfid_state;

    void set_hold_count() { hold_count = HOLD_COUNT_MAX; }
    void update_color_from_str(std::string color);
    wxColour get_color();

    bool is_tray_info_ready();
    bool is_unset_third_filament();
    std::string get_display_filament_type();
    std::string get_filament_type();
};


class Ams {
public:
    Ams(std::string ams_id) {
        id = ams_id;
    }
    std::string   id;
    int           humidity = 5;
    bool          startup_read_opt{true};
    bool          tray_read_opt{false};
    bool          is_exists{false};
    std::map<std::string, AmsTray*> trayList;
};

enum PrinterFirmwareType {
    FIRMWARE_TYPE_ENGINEER = 0,
    FIRMWARE_TYPE_PRODUCTION,
    FIRMEARE_TYPE_UKNOWN,
};


class FirmwareInfo
{
public:
    std::string module_type;    // ota or ams
    std::string version;
    std::string url;
    std::string name;
    std::string description;
};

enum ModuleID {
    MODULE_UKNOWN       = 0x00,
    MODULE_01           = 0x01,
    MODULE_02           = 0x02,
    MODULE_MC           = 0x03,
    MODULE_04           = 0x04,
    MODULE_MAINBOARD    = 0x05,
    MODULE_06           = 0x06,
    MODULE_AMS          = 0x07,
    MODULE_TH           = 0x08,
    MODULE_09           = 0x09,
    MODULE_10           = 0x0A,
    MODULE_11           = 0x0B,
    MODULE_XCAM         = 0x0C,
    MODULE_13           = 0x0D,
    MODULE_14           = 0x0E,
    MODULE_15           = 0x0F,
    MODULE_MAX          = 0x10
};

enum HMSMessageLevel {
    HMS_UNKNOWN = 0,
    HMS_FATAL   = 1,
    HMS_SERIOUS = 2,
    HMS_COMMON  = 3,
    HMS_INFO    = 4,
    HMS_MSG_LEVEL_MAX,
};

class HMSItem
{
public:
    ModuleID        module_id;
    unsigned        module_num;
    unsigned        part_id;
    unsigned        reserved;
    HMSMessageLevel msg_level = HMS_UNKNOWN;
    int             msg_code = 0;
    bool parse_hms_info(unsigned attr, unsigned code);
    std::string get_long_error_code();

    static wxString get_module_name(ModuleID module_id);
    static wxString get_hms_msg_level_str(HMSMessageLevel level);
};


#define UpgradeNoError          0
#define UpgradeDownloadFailed   -1
#define UpgradeVerfifyFailed    -2
#define UpgradeFlashFailed      -3
#define UpgradePrinting         -4

// calc distance map
struct DisValue {
    int  tray_id;
    float distance;
    bool  is_same_color = true;
    bool  is_type_match = true;
};

class MachineObject
{
private:
    NetworkAgent* m_agent { nullptr };

    bool check_valid_ip();
    void _parse_print_option_ack(int option);

public:

    enum LIGHT_EFFECT {
        LIGHT_EFFECT_ON,
        LIGHT_EFFECT_OFF,
        LIGHT_EFFECT_FLASHING,
        LIGHT_EFFECT_UNKOWN,
    };

    enum FanType {
        COOLING_FAN = 1,
        BIG_COOLING_FAN = 2,
        CHAMBER_FAN = 3,
    };

    enum UpgradingDisplayState {
        UpgradingUnavaliable = 0,
        UpgradingAvaliable = 1,
        UpgradingInProgress = 2,
        UpgradingFinished = 3
    };

    enum ExtruderAxisStatus {
        LOAD = 0,
        UNLOAD =1,
        STATUS_NUMS = 2
    };
    enum ExtruderAxisStatus extruder_axis_status = LOAD;

    enum PrintOption {
        PRINT_OP_AUTO_RECOVERY = 0,
        PRINT_OP_MAX,
    };

    class ModuleVersionInfo
    {
    public:
        std::string name;
        std::string sn;
        std::string hw_ver;
        std::string sw_ver;
        std::string sw_new_ver;
    };

    enum SdcardState {
        NO_SDCARD = 0,
        HAS_SDCARD_NORMAL = 1,
        HAS_SDCARD_ABNORMAL = 2,
        SDCARD_STATE_NUM = 3
    };

    /* static members and functions */
    static inline int m_sequence_id = 20000;
    static std::string parse_printer_type(std::string type_str);
    static std::string get_preset_printer_model_name(std::string printer_type);
    static std::string get_preset_printer_thumbnail_img(std::string printer_type);
    static bool is_bbl_filament(std::string tag_uid);

    typedef std::function<void()> UploadedFn;
    typedef std::function<void(int progress)> UploadProgressFn;
    typedef std::function<void(std::string error)> ErrorFn;
    typedef std::function<void(int result, std::string info)> ResultFn;

    /* properties */
    std::string dev_name;
    std::string dev_ip;
    std::string dev_id;
    std::string access_code;
    std::string dev_connection_type;    /* lan | cloud */
    std::string connection_type() { return dev_connection_type; }
    bool has_access_right() { return !access_code.empty(); }
    void set_access_code(std::string code);
    bool is_lan_mode_printer();
    //PRINTER_TYPE printer_type = PRINTER_3DPrinter_UKNOWN;
    std::string printer_type;       /* model_id */

    std::string printer_thumbnail_img;
    std::string monitor_upgrade_printer_img;

    wxString get_printer_type_display_str();

    std::string get_printer_thumbnail_img_str();
    std::string product_name;       // set by iot service, get /user/print

    std::string bind_user_name;
    std::string bind_user_id;
    std::string bind_state;     /* free | occupied */
    bool is_avaliable() { return bind_state == "free"; }
    time_t last_alive;
    bool m_is_online;
    bool m_lan_mode_connection_state{false};
    void set_lan_mode_connection_state(bool state) {m_lan_mode_connection_state = state;};
    bool get_lan_mode_connection_state() {return m_lan_mode_connection_state;};
    int  parse_msg_count = 0;
    std::chrono::system_clock::time_point   last_update_time;   /* last received print data from machine */
    std::chrono::system_clock::time_point   last_push_time;     /* last received print push from machine */
    std::chrono::system_clock::time_point   last_request_push;  /* last received print push from machine */
    std::chrono::system_clock::time_point   last_request_start; /* last received print push from machine */

    /* ams properties */
    std::map<std::string, Ams*> amsList;    // key: ams[id], start with 0
    long  ams_exist_bits = 0;
    long  tray_exist_bits = 0;
    long  tray_is_bbl_bits = 0;
    long  tray_read_done_bits = 0;
    long  tray_reading_bits = 0;
    int   ams_rfid_status = 0;
    bool  ams_insert_flag { false };
    bool  ams_power_on_flag { false };
    bool  ams_calibrate_remain_flag { false };
    bool  ams_support_auto_switch_filament_flag { true };
    bool  ams_auto_switch_filament_flag  { false };
    bool  ams_support_use_ams { false };
    bool  ams_support_remain { true };
    int   ams_humidity;
    int   ams_user_setting_hold_count = 0;
    AmsStatusMain ams_status_main;
    int   ams_status_sub;
    int   ams_version = 0;
    std::string m_ams_id;           // local ams  : "0" ~ "3"
    std::string m_tray_id;          // local tray id : "0" ~ "3"
    std::string m_tray_now;         // tray_now : "0" ~ "15" or "255"
    std::string m_tray_tar;         // tray_tar : "0" ~ "15" or "255"
    void _parse_tray_now(std::string tray_now);
    bool is_filament_move() { return atoi(m_tray_now.c_str()) == 255 ? false : true; };
    bool    is_ams_need_update;

    inline bool is_ams_unload() { return m_tray_tar.compare("255") == 0; }
    Ams*     get_curr_Ams();
    AmsTray* get_curr_tray();
    AmsTray *get_ams_tray(std::string ams_id, std::string tray_id);
    // parse amsStatusMain and ams_status_sub
    void _parse_ams_status(int ams_status);
    bool has_ams() { return ams_exist_bits != 0; }
    bool can_unload_filament();
    bool is_U0_firmware();
    bool is_support_ams_mapping();
    bool is_only_support_cloud_print();
    static bool is_support_ams_mapping_version(std::string module, std::string version);

    int ams_filament_mapping(std::vector<FilamentInfo> filaments, std::vector<FilamentInfo> &result, std::vector<int> exclude_id = std::vector<int>());
    bool is_valid_mapping_result(std::vector<FilamentInfo>& result, bool check_empty_slot = false);
    // exceed index start with 0
    bool is_mapping_exceed_filament(std::vector<FilamentInfo>& result, int &exceed_index);
    void reset_mapping_result(std::vector<FilamentInfo>& result);

    /*online*/
    bool   online_rfid;
    bool   online_ahb;
    int    online_version = -1;
    int    last_online_version = -1;

    /* temperature */
    float  nozzle_temp;
    float  nozzle_temp_target;
    float  bed_temp;
    float  bed_temp_target;
    float  chamber_temp;
    float  frame_temp;

    /* cooling */
    int     heatbreak_fan_speed = 0;
    int     cooling_fan_speed = 0;
    int     big_fan1_speed = 0;
    int     big_fan2_speed = 0;
    uint32_t fan_gear       = 0;

    /* signals */
    std::string wifi_signal;
    std::string link_th;
    std::string link_ams;

    /* lights */
    LIGHT_EFFECT chamber_light;
    LIGHT_EFFECT work_light;
    std::string light_effect_str(LIGHT_EFFECT effect);
    LIGHT_EFFECT light_effect_parse(std::string effect_str);

    /* upgrade */
    bool upgrade_force_upgrade { false };
    bool upgrade_new_version { false };
    bool upgrade_consistency_request { false };
    int upgrade_display_state = 0;           // 0 : upgrade unavailable, 1: upgrade idle, 2: upgrading, 3: upgrade_finished
    PrinterFirmwareType       firmware_type; // engineer|production
    std::string upgrade_progress;
    std::string upgrade_message;
    std::string upgrade_status;
    std::string upgrade_module;
    std::string ams_new_version_number;
    std::string ota_new_version_number;
    std::string ahb_new_version_number;
    int get_version_retry = 0;
    std::map<std::string, ModuleVersionInfo> module_vers;
    std::map<std::string, ModuleVersionInfo> new_ver_list;
    bool    m_new_ver_list_exist = false;
    int upgrade_err_code = 0;
    std::vector<FirmwareInfo> firmware_list;

    std::string get_firmware_type_str();
    bool is_in_upgrading();
    bool is_upgrading_avalable();
    int get_upgrade_percent();
    std::string get_ota_version();
    bool check_version_valid();
    wxString get_upgrade_result_str(int upgrade_err_code);
    // key: ams_id start as 0,1,2,3
    std::map<int, ModuleVersionInfo> get_ams_version();

    /* printing */
    std::string print_type;
    float   nozzle { 0.0f };
    bool    is_220V_voltage { false };


    int     mc_print_stage;
    int     mc_print_sub_stage;
    int     mc_print_error_code;
    int     mc_print_line_number;
    int     mc_print_percent;       /* left print progess in percent */
    int     mc_left_time;           /* left time in seconds */
    int     last_mc_print_stage;
    int     home_flag;
    int     hw_switch_state;
    bool    is_system_printing();
    int     print_error;

    std::vector<int> stage_list_info;
    int stage_curr = 0;
    int m_push_count = 0;
    bool calibration_done { false };

    bool is_axis_at_home(std::string axis);

    bool is_filament_at_extruder();

    wxString get_curr_stage();
    // return curr stage index of stage list
    int get_curr_stage_idx();
    bool is_in_calibration();
    bool is_calibration_running();
    bool is_calibration_done();

    void parse_state_changed_event();
    void parse_status(int flag);

    /* printing status */
    std::string print_status;      /* enum string: FINISH, RUNNING, PAUSE, INIT, FAILED */
    std::string iot_print_status;  /* iot */
    PrintingSpeedLevel printing_speed_lvl;
    int                printing_speed_mag = 100;
    PrintingSpeedLevel _parse_printing_speed_lvl(int lvl);
    int get_bed_temperature_limit();

    /* camera */
    bool has_ipcam { false };
    bool camera_recording { false };
    bool camera_recording_when_printing { false };
    bool camera_timelapse { false };
    int  camera_recording_hold_count = 0;
    int  camera_timelapse_hold_count = 0;
    int  camera_resolution_hold_count = 0;
    std::string camera_resolution = "";
    bool xcam_first_layer_inspector { false };
    int  xcam_first_layer_hold_count = 0;

    bool xcam_ai_monitoring{ false };
    int  xcam_ai_monitoring_hold_count = 0;
    std::string xcam_ai_monitoring_sensitivity;
    bool is_xcam_buildplate_supported { true };
    bool xcam_buildplate_marker_detector{ false };
    int  xcam_buildplate_marker_hold_count = 0;
    bool xcam_support_recovery_step_loss { true };
    bool xcam_auto_recovery_step_loss{ false };
    int  xcam_auto_recovery_hold_count = 0;
    int  ams_print_option_count = 0;

    /* sdcard */
    MachineObject::SdcardState sdcard_state { NO_SDCARD };
    MachineObject::SdcardState get_sdcard_state();
    bool is_support_send_to_sdcard { true };

    /* HMS */
    std::vector<HMSItem>    hms_list;

    /* machine mqtt apis */
    int connect(bool is_anonymous = false);
    int disconnect();

    json_diff print_json;

    /* Project Task and Sub Task */
    std::string  project_id_;
    std::string  profile_id_;
    std::string  task_id_;
    std::string  subtask_id_;
    BBLSliceInfo* slice_info {nullptr};
    boost::thread* get_slice_info_thread { nullptr };


    int plate_index { -1 };
    std::string m_gcode_file;
    int gcode_file_prepare_percent = 0;
    BBLSubTask* subtask_;
    std::string obj_subtask_id;     // subtask_id == 0 for sdcard
    std::string subtask_name;
    bool is_sdcard_printing();
    bool has_sdcard();
    bool is_timelapse();
    bool is_recording_enable();
    bool is_recording();


    MachineObject(NetworkAgent* agent, std::string name, std::string id, std::string ip);
    ~MachineObject();

    void parse_version_func();
    /* command commands */
    int command_get_version(bool with_retry = true);
    int command_request_push_all();
    int command_pushing(std::string cmd);

    /* command upgrade */
    int command_upgrade_confirm();
    int command_consistency_upgrade_confirm();
    int command_upgrade_firmware(FirmwareInfo info);
    int command_upgrade_module(std::string url, std::string module_type, std::string version);

    /* control apis */
    int command_xyz_abs();
    int command_auto_leveling();
    int command_go_home();
    int command_control_fan(FanType fan_type, bool on_off);
    int command_control_fan_val(FanType fan_type, int val);
    int command_task_abort();
    int command_task_pause();
    int command_task_resume();
    int command_set_bed(int temp);
    int command_set_nozzle(int temp);
    // ams controls
    int command_ams_switch(int tray_index, int old_temp = 210, int new_temp = 210);
    int command_ams_change_filament(int tray_id, int old_temp = 210, int new_temp = 210);
    int command_ams_user_settings(int ams_id, bool start_read_opt, bool tray_read_opt, bool remain_flag = false);
    int command_ams_user_settings(int ams_id, AmsOptionType op, bool value);
    int command_ams_switch_filament(bool switch_filament);
    int command_ams_calibrate(int ams_id);
    int command_ams_filament_settings(int ams_id, int tray_id, std::string setting_id, std::string tray_color, std::string tray_type, int nozzle_temp_min, int nozzle_temp_max);
    int command_ams_select_tray(std::string tray_id);
    int command_ams_refresh_rfid(std::string tray_id);
    int command_ams_control(std::string action);
    int command_set_chamber_light(LIGHT_EFFECT effect, int on_time = 500, int off_time = 500, int loops = 1, int interval = 1000);
    int command_set_work_light(LIGHT_EFFECT effect, int on_time = 500, int off_time = 500, int loops = 1, int interval = 1000);

    // set printing speed
    int command_set_printing_speed(PrintingSpeedLevel lvl);

    // set print option
    int command_set_printing_option(bool auto_recovery);

    // axis string is X, Y, Z, E
    int command_axis_control(std::string axis, double unit = 1.0f, double value = 1.0f, int speed = 3000);

    // calibration printer
    bool is_support_command_calibration();
    int command_start_calibration(bool vibration, bool bed_leveling, bool xcam_cali);

    int command_unload_filament();

    // camera control
    int command_ipcam_record(bool on_off);
    int command_ipcam_timelapse(bool on_off);
    int command_ipcam_resolution_set(std::string resolution);
    int command_xcam_control(std::string module_name, bool on_off, std::string lvl = "");
    int command_xcam_control_ai_monitoring(bool on_off, std::string lvl);
    int command_xcam_control_first_layer_inspector(bool on_off, bool print_halt);
    int command_xcam_control_buildplate_marker_detector(bool on_off);
    int command_xcam_control_auto_recovery_step_loss(bool on_off);

    /* common apis */
    inline bool is_local() { return !dev_ip.empty(); }
    void set_bind_status(std::string status);
    std::string get_bind_str();
    bool can_print();
    bool can_resume();
    bool can_pause();
    bool can_abort();
    bool is_in_printing();
    bool is_in_prepare();
    bool is_printing_finished();
    void reset_update_time();
    void reset();
    static bool is_in_printing_status(std::string status);

    void set_print_state(std::string status);

    bool is_connected();
    bool is_connecting();
    void set_online_state(bool on_off);
    bool is_online() { return m_is_online; }
    bool is_info_ready();
    bool is_function_supported(PrinterFunction func);
    std::vector<std::string> get_resolution_supported();
    bool is_support_print_with_timelapse();


    /* Msg for display MsgFn */
    typedef std::function<void(std::string topic, std::string payload)> MsgFn;
    int publish_json(std::string json_str, int qos = 0);
    int cloud_publish_json(std::string json_str, int qos = 0);
    int local_publish_json(std::string json_str, int qos = 0);
    int parse_json(std::string payload);
    int publish_gcode(std::string gcode_str);

    BBLSubTask* get_subtask();
    void update_slice_info(std::string project_id, std::string profile_id, std::string subtask_id, int plate_idx);

    bool m_firmware_valid { false };
    bool m_firmware_thread_started { false };
    void get_firmware_info();
    bool is_firmware_info_valid();
};

class DeviceManager
{
private:
    NetworkAgent* m_agent { nullptr };

public:
    DeviceManager(NetworkAgent* agent = nullptr);
    ~DeviceManager();
    void set_agent(NetworkAgent* agent);

    std::mutex listMutex;
    std::string selected_machine;                               /* dev_id */
    std::string local_selected_machine;                         /* dev_id */
    std::map<std::string, MachineObject*> localMachineList;     /* dev_id -> MachineObject*, localMachine SSDP   */
    std::map<std::string, MachineObject*> userMachineList;      /* dev_id -> MachineObject*  cloudMachine of User */

    void check_pushing();

    MachineObject* get_default_machine();
    MachineObject* get_local_selected_machine();
    MachineObject* get_local_machine(std::string dev_id);
    MachineObject* get_user_machine(std::string dev_id);
    MachineObject* get_my_machine(std::string dev_id);
    void erase_user_machine(std::string dev_id);
    void clean_user_info();

    bool set_selected_machine(std::string dev_id);
    MachineObject* get_selected_machine();

    /* return machine has access code and user machine if login*/
    std::map<std::string, MachineObject*> get_my_machine_list();
    std::string get_first_online_user_machine();
    void modify_device_name(std::string dev_id, std::string dev_name);
    void update_user_machine_list_info();
    void parse_user_print_info(std::string body);

    /* create machine or update machine properties */
    void on_machine_alive(std::string json_str);

    /* disconnect all machine connections */
    void disconnect_all();
    int query_bind_status(std::string &msg);

    // get alive machine
    std::map<std::string, MachineObject*> get_local_machine_list();
    void load_last_machine();

    static json function_table;
    static std::string parse_printer_type(std::string type_str);
    static std::string get_printer_display_name(std::string type_str);
    static std::string get_printer_thumbnail_img(std::string type_str);
    static bool is_function_supported(std::string type_str, std::string function_name);
    static std::vector<std::string> get_resolution_supported(std::string type_str);

    static bool get_bed_temperature_limit(std::string type_str, int& limit);
    static bool load_functional_config(std::string config_file);
};

} // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
