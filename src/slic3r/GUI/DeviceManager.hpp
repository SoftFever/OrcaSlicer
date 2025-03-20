#ifndef slic3r_DeviceManager_hpp_
#define slic3r_DeviceManager_hpp_

#include <map>
#include <mutex>
#include <vector>
#include <string>
#include <memory>
#include <chrono>
#include <boost/thread.hpp>
#include <boost/nowide/fstream.hpp>
#include "nlohmann/json.hpp"
#include "libslic3r/ProjectTask.hpp"
#include "slic3r/Utils/json_diff.hpp"
#include "slic3r/Utils/NetworkAgent.hpp"
#include "boost/bimap/bimap.hpp"
#include "CameraPopup.hpp"
#include "libslic3r/calib.hpp"
#include "libslic3r/Utils.hpp"

#include <wx/object.h>

#define USE_LOCAL_SOCKET_BIND 0

#define DISCONNECT_TIMEOUT      30000.f     // milliseconds
#define PUSHINFO_TIMEOUT        15000.f     // milliseconds
#define TIMEOUT_FOR_STRAT       20000.f     // milliseconds
#define TIMEOUT_FOR_KEEPALIVE   5* 60 * 1000.f     // milliseconds
#define REQUEST_PUSH_MIN_TIME   3000.f     // milliseconds
#define REQUEST_START_MIN_TIME  15000.f     // milliseconds
#define EXTRUSION_OMIT_TIME     20000.f     // milliseconds
#define HOLD_TIMEOUT            10000.f     // milliseconds

#define FILAMENT_MAX_TEMP       300
#define FILAMENT_DEF_TEMP       220
#define FILAMENT_MIN_TEMP       120
#define BED_TEMP_LIMIT          120

#define HOLD_COUNT_MAX          3
#define HOLD_COUNT_CAMERA       6

#define HOLD_TIME_MAX           3 // 3 seconds
#define HOLD_TIME_SWITCHING     6 // 6 seconds

#define GET_VERSION_RETRYS      10
#define RETRY_INTERNAL          2000

#define MAIN_NOZZLE_ID          0
#define DEPUTY_NOZZLE_ID        1
#define UNIQUE_NOZZLE_ID        MAIN_NOZZLE_ID
#define INVALID_NOZZLE_ID       -1

#define VIRTUAL_TRAY_MAIN_ID    255
#define VIRTUAL_TRAY_DEPUTY_ID  254

#define START_SEQ_ID            20000
#define END_SEQ_ID              30000

inline int correct_filament_temperature(int filament_temp)
{
    int temp = std::min(filament_temp, FILAMENT_MAX_TEMP);
    temp     = std::max(temp, FILAMENT_MIN_TEMP);
    return temp;
}

wxString get_stage_string(int stage);

using namespace nlohmann;

namespace Slic3r {

struct BBLocalMachine;
class SecondaryCheckDialog;
enum PrinterArch {
    ARCH_CORE_XY,
    ARCH_I3,
};

enum PrinterSeries {
    SERIES_X1 = 0,
    SERIES_P1P,
    SERIES_UNKNOWN,
};

enum PrinterFunction {
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

enum ManualPaCaliMethod {
    PA_LINE = 0,
    PA_PATTERN,
};

enum ExtruderSwitchState {
    ES_IDLE = 0,
    ES_BUSY,
    ES_SWITCHING,
    ES_SWITCHING_FAILED
};

enum AirDuctType {
    AIR_FAN_TYPE,
    AIR_DOOR_TYPE
};

enum NozzleFlowType{
    NONE_FLOWTYPE,
    S_FLOW,
    H_FLOW
};

enum NozzleToolType {
    NONE_TOOLTYPE,
    H_TOOL,
    C_TOOL
};

struct AmsSlot
{
    std::string ams_id;
    std::string slot_id;
};

struct Nozzle
{
    int   id;
    NozzleToolType  tool_type;         // H nozzle or Cut
    NozzleFlowType  nozzle_flow;       // 0-common 1-high flow
    NozzleType      nozzle_type;       // 0-stainless_steel 1-hardened_steel
    float diameter = {0.4f}; // 0-0.2mm  1-0.4mm 2-0.6 mm3-0.8mm
    int   max_temp = 0;
    int   wear = 0;
};

struct NozzleData
{
    int extder_exist;  //0- none exist 1-exist
    int cut_exist;
    int state; //0-idle 1-checking
    std::vector<Nozzle> nozzles;
};

struct Extder
{
    int id; // 0-right 1-left

    int enable_change_nozzle{0};
    int ext_has_filament{0};
    int buffer_has_filament{0};
    int nozzle_exist{0};

    std::vector<int> filam_bak;// the refill filam

    int  temp{0};
    int target_temp{0};

    AmsSlot spre; // tray_pre
    AmsSlot snow; // tray_now
    AmsSlot star; // tray_tar
    int     ams_stat{0};

    int rfid_stat{0};

    int nozzle_id;        // nozzle id now
    int target_nozzle_id; // target nozzle id

    //current nozzle
    NozzleType     current_nozzle_type{NozzleType::ntUndefine};            // 0-hardened_steel 1-stainless_steel
    float current_nozzle_diameter = {0.4f}; // 0-0.2mm  1-0.4mm 2-0.6 mm3-0.8mm
    NozzleFlowType current_nozzle_flow_type{NozzleFlowType::NONE_FLOWTYPE};//0-common 1-high flow
};

struct ExtderData
{
    int current_extder_id{0};
    int target_extder_id{0};
    int total_extder_count {0};
    int current_loading_extder_id {-1};
    bool current_busy_for_loading{false};
    ExtruderSwitchState switch_extder_state;
    std::vector<Extder> extders;
};

struct AirMode
{
    int              id{-1};
    std::vector<int> ctrl;
    // If the fan is off, it cannot be controlled and is displayed as off
    std::vector<int> off;
    // If the fan is not off or ctrl, it will be displayed as auto
};

struct AirParts
{
    int         type{ 0 };
    int         id{ 0 };
    int         func{ 0 };
    int         state{ 0 };// 100%
    int         range_start{ 0 };// 100%
    int         range_end{ 0 };// 100%
};

struct AirDuctData
{
    int curren_mode{ 0 };
    std::vector<AirMode> modes;
    std::vector<AirParts> parts;
};

struct RatingInfo {
    bool        request_successful;
    int         http_code;
    int         rating_id;
    int         start_count;
    bool        success_printed;
    std::string content;
    std::vector<std::string>  image_url_paths;
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
        std::array<int, 4> ret = {0, 0, 0, 0};
        const char *       c   = color.data();
        if (color.size() == 8) {
            for (size_t j = 0; j < 4; ++j) {
                int digit1 = hex_digit_to_int(*c++);
                int digit2 = hex_digit_to_int(*c++);
                if (digit1 == -1 || digit2 == -1) break;
                ret[j] = float(digit1 * 16 + digit2);
            }
        } else {
            return wxColour(255, 255, 255, 255);
        }
        return wxColour(ret[0], ret[1], ret[2], ret[3]);
    }

    bool operator==(AmsTray const &o) const
    {
        return id == o.id && type == o.type && filament_setting_id == o.filament_setting_id && color == o.color;
    }
    bool operator!=(AmsTray const &o) const { return !operator==(o); }

    std::string     id;
    std::string     tag_uid;     // tag_uid
    std::string     setting_id;  // tray_info_idx
    std::string     filament_setting_id;    // setting_id
    std::string     type;
    std::string     sub_brands;
    std::string     color;
    std::vector<std::string> cols;
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
    int             ctype = 0;
    float           k = 0.0f;       // k range: 0 ~ 0.5
    float           n = 0.0f;       // k range: 0.6 ~ 2.0
    int             cali_idx = 0;

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

    void reset();

    bool is_tray_info_ready();
    bool is_unset_third_filament();
    std::string get_display_filament_type();
    std::string get_filament_type();
};

#define INVALID_AMS_TEMPERATURE std::numeric_limits<float>::min()

class Ams
{
public:
    Ams(std::string ams_id, int nozzle_id, int type_id)
    {
        id     = ams_id;
        nozzle = nozzle_id;
        type   = type_id;
    }
    std::string                      id;
    int                              left_dry_time       = 0;
    int                              humidity            = 5;
    int                              humidity_raw        = -1;                      // the percentage, -1 means invalid. eg. 100 means 100%
    float                            current_temperature = INVALID_AMS_TEMPERATURE; // the temperature
    bool                             startup_read_opt{true};
    bool                             tray_read_opt{false};
    bool                             is_exists{false};
    std::map<std::string, AmsTray *> trayList;

    int nozzle;
    int type{1}; // 0:dummy 1:ams 2:ams-lite 3:n3f 4:n3s

public:
    wxString get_ams_device_name() const
    {
        wxString ams_device_name;
        if (type == 1) {
            ams_device_name = "AMS-%d";
        } else if (type == 2) {
            ams_device_name = "AMS Lite-%d";
        } else if (type == 3) {
            ams_device_name = "AMS 2 PRO-%d";
        } else if (type == 4) {
            ams_device_name = "AMS HT-%d";
        } else {
            assert(0);
            ams_device_name = "AMS-%d";
        }

        int num_id;
        try {
            num_id = std::stoi(id);
        } catch (...) {}

        int loc = (num_id > 127) ? (num_id - 127) : (num_id + 1);
        return wxString::Format(ams_device_name, loc);
    };
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


enum AIR_FUN {
    FAN_HEAT_BREAK_0_IDX      = 0,
    FAN_COOLING_0_AIRDOOR     = 1,
    FAN_REMOTE_COOLING_0_IDX  = 2,
    FAN_CHAMBER_0_IDX         = 3,
    FAN_HEAT_BREAK_1_IDX      = 4,
    FAN_MC_BOARD_0_IDX        = 5,
    FAN_INNNER_LOOP_FAN_0_IDX = 6,
    FAN_TOTAL_COUNT           = 7
};

enum AIR_DOOR {
    AIR_DOOR_FUNC_CHAMBER = 0,
    AIR_DOOR_FUNC_INNERLOOP,
    AIR_DOOR_FUNC_TOP
};

enum AIR_DUCT {
    AIR_DUCT_NONE = -1,
    AIR_DUCT_COOLING_FILT = 0,
    AIR_DUCT_HEATING_INTERNAL_FILT,
    AIR_DUCT_EXHAUST,
    AIR_DUCT_FULL_COOLING,
    AIR_DUCT_NUM,
    AIR_DUCT_INIT = 0xFF    //Initial mode, only used within mc
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
    bool            already_read = false;
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

class Preset;
class MachineObject
{
private:
    NetworkAgent *       m_agent{nullptr};
    std::shared_ptr<int> m_token = std::make_shared<int>(1);

    bool check_valid_ip();
    void _parse_print_option_ack(int option);

    std::string access_code;
    std::string user_access_code;

    // type, time stamp, delay
    std::vector<std::tuple<std::string, uint64_t, uint64_t>> message_delay;

public:

    typedef std::function<void(const json &)> CommandCallBack;

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
        EXHAUST_FAN,
        FILTER_FAN,
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
        wxString    product_name;
        std::string sn;
        std::string hw_ver;
        std::string sw_ver;
        std::string sw_new_ver;
        int         firmware_status;
        ModuleVersionInfo() :firmware_status(0) {

        };

    public:
        bool isValid() const { return !sn.empty(); }
        bool isAirPump() const { return product_name.Contains("Air Pump"); }
        bool isLaszer() const { return product_name.Contains("Laser"); }
        bool isCuttingModule() const { return product_name.Contains("Cutting Module"); }
    };

    enum SdcardState {
        NO_SDCARD = 0,
        HAS_SDCARD_NORMAL = 1,
        HAS_SDCARD_ABNORMAL = 2,
        HAS_SDCARD_READONLY = 3,
        SDCARD_STATE_NUM = 4
    };

    enum ActiveState {
        NotActive,
        Active,
        UpdateToDate
    };

    enum MappingOption {
        USE_LEFT_AMS = 0,
        USE_RIGHT_AMS,
        USE_LEFT_EXT,
        USE_RIGHT_EXT
    };

    class ExtrusionRatioInfo
    {
    public:
        std::string name;
        std::string setting_id;
        float       k = 0.0;
        float       n = 0.0;
    };

    /* static members and functions */
    static inline int m_sequence_id = START_SEQ_ID;
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
    bool        local_use_ssl_for_mqtt { true };
    bool        local_use_ssl_for_ftp { true };
    bool        m_busy_for_select_extruder {false};
    int         subscribe_counter{3};
    std::string dev_connection_type;    /* lan | cloud */
    std::string connection_type() { return dev_connection_type; }
    std::string dev_connection_name;    /* lan | eth */
    void set_dev_ip(std::string ip) {dev_ip = ip;}
    std::string get_ftp_folder();
    bool has_access_right() const { return !get_access_code().empty(); }
    std::string get_access_code() const;

    void set_access_code(std::string code, bool only_refresh = true);
    void set_user_access_code(std::string code, bool only_refresh = true);
    void erase_user_access_code();
    std::string get_user_access_code() const;
    bool is_lan_mode_printer() const;

    //PRINTER_TYPE printer_type = PRINTER_3DPrinter_UKNOWN;
    std::string printer_type;       /* model_id */
    PrinterSeries get_printer_series() const;
    PrinterArch get_printer_arch() const;
    std::string get_printer_ams_type() const;
    bool        get_printer_is_enclosed() const;

    // check printer device series
    static bool is_series_n(const std::string& series_str);
    static bool is_series_p(const std::string& series_str);
    static bool is_series_x(const std::string& series_str);
    static bool is_series_o(const std::string& series_str);

    bool is_series_n() const;
    bool is_series_p() const;
    bool is_series_x() const;
    bool is_series_o() const;

    std::string get_printer_series_str() const;

    void reload_printer_settings();

    std::string printer_thumbnail_img;
    std::string monitor_upgrade_printer_img;

    wxString get_printer_type_display_str();
    std::string get_printer_thumbnail_img_str();

    std::string product_name;       // set by iot service, get /user/print

    std::string bind_user_name;
    std::string bind_user_id;
    std::string bind_state;     /* free | occupied */
    std::string bind_sec_link;
    std::string bind_ssdp_version;
    bool is_avaliable() { return bind_state == "free"; }
    time_t last_alive;
    bool m_is_online;
    bool m_lan_mode_connection_state{false};
    bool m_set_ctt_dlg{ false };
    void set_lan_mode_connection_state(bool state) {m_lan_mode_connection_state = state;};
    bool get_lan_mode_connection_state() {return m_lan_mode_connection_state;};
    void set_ctt_dlg( wxString text);
    int  parse_msg_count = 0;
    int  keep_alive_count = 0;
    std::chrono::system_clock::time_point   last_update_time;   /* last received print data from machine */
    std::chrono::system_clock::time_point   last_utc_time;   /* last received print data from machine */
    std::chrono::system_clock::time_point   last_keep_alive;    /* last received print data from machine */
    std::chrono::system_clock::time_point   last_push_time;     /* last received print push from machine */
    std::chrono::system_clock::time_point   last_request_push;  /* last received print push from machine */
    std::chrono::system_clock::time_point   last_request_start; /* last received print push from machine */

    int m_active_state = 0; // 0 - not active, 1 - active, 2 - update-to-date
    bool is_tunnel_mqtt = false;

    /* ams properties */
    std::map<std::string, Ams*> amsList;    // key: ams[id], start with 0

    int   ams_cali_id = 0;
    int   ams_cali_stat = 0;

    //AmsTray vt_tray;                        // virtual tray
    long  ams_exist_bits = 0;
    long  tray_exist_bits = 0;
    long  tray_is_bbl_bits = 0;
    long  tray_read_done_bits = 0;
    long  tray_reading_bits = 0;
    int   ams_rfid_status = 0;
    bool  ams_insert_flag { false };
    bool  ams_power_on_flag { false };
    bool  ams_calibrate_remain_flag { false };
    bool  ams_auto_switch_filament_flag  { false };
    bool  ams_air_print_status { false };
    bool  ams_support_virtual_tray { true };
    time_t ams_user_setting_start = 0;
    time_t ams_switch_filament_start = 0;
    AmsStatusMain ams_status_main;
    int   ams_status_sub;
    int   ams_version = 0;
    std::string m_ams_id;           // local ams  : "0" ~ "3"
    std::string m_tray_id;          // local tray id : "0" ~ "3"
    std::string m_tray_now;         // tray_now : "0" ~ "15" or "254", "255"
    std::string m_tray_tar;         // tray_tar : "0" ~ "15" or "255"

    int extrusion_cali_hold_count = 0;
    std::chrono::system_clock::time_point last_extrusion_cali_start_time;
    int extrusion_cali_set_tray_id = -1;
    std::chrono::system_clock::time_point extrusion_cali_set_hold_start;
    std::string  extrusion_cali_filament_name;

    bool only_exist_ext_spool() { return amsList.empty(); }
    bool is_in_extrusion_cali();
    bool is_extrusion_cali_finished();
    void _parse_tray_now(std::string tray_now);
    bool is_filament_move() { return atoi(m_tray_now.c_str()) == 255 ? false : true; };
    bool    is_ams_need_update;

    inline bool is_ams_unload() { return m_tray_tar.compare("255") == 0; }
    [[nodiscard]] bool is_ams_on_settingup() const;

    Ams*     get_curr_Ams();
    AmsTray* get_curr_tray();
    AmsTray *get_ams_tray(std::string ams_id, std::string tray_id);
    // parse amsStatusMain and ams_status_sub
    void _parse_ams_status(int ams_status);
    bool has_ams() { return ams_exist_bits != 0; }
    bool can_unload_filament();
    bool is_support_ams_mapping();
    bool is_support_amx_ext_mix_mapping();

    void get_ams_colors(std::vector<wxColour>& ams_colors);
    void parse_tray_info(int ams_id, int sot_id, AmsTray tray, FilamentInfo& result);
    int ams_filament_mapping(std::vector<FilamentInfo> filaments, std::vector<FilamentInfo> &result, std::vector<bool> map_opt, std::vector<int> exclude_id = std::vector<int>(),bool nozzle_has_ams_then_ignore_ext =false);
    bool is_valid_mapping_result(std::vector<FilamentInfo>& result, bool check_empty_slot = false);
    // exceed index start with 0
    bool is_mapping_exceed_filament(std::vector<FilamentInfo>& result, int &exceed_index);
    void reset_mapping_result(std::vector<FilamentInfo>& result);
    bool is_main_extruder_on_left() const;
    bool is_multi_extruders() const;
    bool need_SD_card() const;
    int  get_extruder_id_by_ams_id(const std::string &ams_id);

    /*online*/
    bool   online_rfid;
    bool   online_ahb;
    int    online_version = -1;
    int    last_online_version = -1;

    /* temperature */
    //float  nozzle_temp;
    //float  nozzle_temp_target;
    float  bed_temp;
    float  bed_temp_target;
    float  chamber_temp;
    float  chamber_temp_target;
    float  chamber_temp_edit_min = 0;
    float  chamber_temp_edit_max = 60;
    long   chamber_temp_switch_heat = LONG_MAX;/*default to LONG_MAX*/
    float  frame_temp;

    /* cooling */
    int     heatbreak_fan_speed = 0;
    int     cooling_fan_speed = 0;
    int     big_fan1_speed = 0;
    int     big_fan2_speed = 0;
    uint32_t fan_gear       = 0;

    /*extruder*/
    [[nodiscard]] bool is_nozzle_flow_type_supported() const { return is_enable_np; };
    [[nodiscard]] NozzleFlowType get_nozzle_flow_type(int extruder_id) const;
    [[nodiscard]] const Extder& get_current_extruder() const;

    //new fan data
    AirDuctData m_air_duct_data;
    void converse_to_duct(bool is_suppt_part_fun, bool is_suppt_aux_fun, bool is_suppt_cham_fun); // Convert the data to duct type to make the newand old protocols consistent

    /* signals */
    std::string wifi_signal;
    std::string link_th;
    std::string link_ams;
    bool        network_wired { false };

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
    int upgrade_display_hold_count = 0;
    PrinterFirmwareType       firmware_type; // engineer|production
    PrinterFirmwareType       lifecycle { PrinterFirmwareType::FIRMWARE_TYPE_PRODUCTION };
    std::string upgrade_progress;
    std::string upgrade_message;
    std::string upgrade_status;
    std::string upgrade_module;
    std::string ams_new_version_number;
    std::string ota_new_version_number;
    std::string ahb_new_version_number;
    int get_version_retry = 0;

    ModuleVersionInfo air_pump_version_info;
    ModuleVersionInfo laser_version_info;
    ModuleVersionInfo cutting_module_version_info;
    std::map<std::string, ModuleVersionInfo> module_vers;
    std::map<std::string, ModuleVersionInfo> new_ver_list;
    std::map<std::string, ExtrusionRatioInfo> extrusion_ratio_map;
    bool    m_new_ver_list_exist = false;
    int upgrade_err_code = 0;
    std::vector<FirmwareInfo> firmware_list;

    std::string get_firmware_type_str();
    std::string get_lifecycle_type_str();
    bool is_in_upgrading();
    bool is_upgrading_avalable();
    int get_upgrade_percent();
    std::string get_ota_version();
    bool check_version_valid();
    wxString get_upgrade_result_str(int upgrade_err_code);
    // key: ams_id start as 0,1,2,3
    std::map<int, ModuleVersionInfo> get_ams_version();

    void clear_version_info();
    void store_version_info(const ModuleVersionInfo& info);

    /* printing */
    std::string print_type;
    float   nozzle { 0.0f };        // default is 0.0f as initial value
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
    int     curr_layer = 0;
    int     total_layers = 0;
    bool    is_support_layer_num { false };
    bool    nozzle_blob_detection_enabled{ false };

    int last_cali_version = -1;
    int cali_version = -1;
    float                      cali_selected_nozzle_dia { 0.0 };
    // 1: record when start calibration in preset page
    // 2: reset when start calibration in start page
    // 3: save tray_id, filament_id, setting_id, and name, nozzle_dia
    std::vector<CaliPresetInfo> selected_cali_preset;
    float                      cache_flow_ratio { 0.0 };
    bool                       cali_finished = true;
    FlowRatioCalibrationType   flow_ratio_calibration_type = FlowRatioCalibrationType::COMPLETE_CALIBRATION;

    ManualPaCaliMethod         manual_pa_cali_method = ManualPaCaliMethod::PA_LINE;
    bool                       has_get_pa_calib_tab{ false };
    bool                       request_tab_from_bbs { false };
    std::vector<PACalibResult> pa_calib_tab;
    bool                       get_pa_calib_result { false };
    std::vector<PACalibResult> pa_calib_results;
    bool                       get_flow_calib_result { false };
    std::vector<FlowRatioCalibResult> flow_ratio_results;
    void reset_pa_cali_history_result()
    {
        has_get_pa_calib_tab = false;
        pa_calib_tab.clear();
    }

    void reset_pa_cali_result() {
        get_pa_calib_result = false;
        pa_calib_results.clear();
    }

    void reset_flow_rate_cali_result() {
        get_flow_calib_result = false;
        flow_ratio_results.clear();
    }

    bool check_pa_result_validation(PACalibResult& result);

    std::vector<int> stage_list_info;
    int stage_curr = 0;
    int m_push_count = 0;
    int m_full_msg_count = 0; /*the full message count, there are full or diff messages from network*/
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
    std::string print_status;      /* enum string: FINISH, SLICING, RUNNING, PAUSE, INIT, FAILED */
    int queue_number = 0;
    std::string iot_print_status;  /* iot */
    PrintingSpeedLevel printing_speed_lvl;
    int                printing_speed_mag = 100;
    PrintingSpeedLevel _parse_printing_speed_lvl(int lvl);
    int get_bed_temperature_limit();
    bool is_filament_installed();
    /* camera */
    bool has_ipcam { false };
    bool camera_recording { false };
    bool camera_recording_when_printing { false };
    bool camera_timelapse { false };
    int  camera_recording_hold_count = 0;
    int  camera_timelapse_hold_count = 0;
    int  camera_resolution_hold_count = 0;
    std::string camera_resolution            = "";
    std::vector<std::string> camera_resolution_supported;
    bool xcam_first_layer_inspector { false };
    int  xcam_first_layer_hold_count = 0;
    std::string local_rtsp_url;
    std::string tutk_state;
    enum LiveviewLocal {
        LVL_None,
        LVL_Disable,
        LVL_Local,
        LVL_Rtsps,
        LVL_Rtsp
    } liveview_local{ LVL_None };
    enum LiveviewRemote {
        LVR_None,
        LVR_Tutk,
        LVR_Agora,
        LVR_TutkAgora
    } liveview_remote{ LVR_None };
    enum FileLocal {
        FL_None,
        FL_Local
    } file_local{ FL_None };
    enum FileRemote {
        FR_None,
        FR_Tutk,
        FR_Agora,
        FR_TutkAgora
    } file_remote{ FR_None };

    enum PlateMakerDectect : int
    {
        POS_CHECK      = 1,
        TYPE_POS_CHECK = 2,
    };

    enum DoorOpenCheckState : int
    {
        DOOR_OPEN_CHECK_DISABLE            = 0,/*do nothing*/
        DOOR_OPEN_CHECK_ENABLE_WARNING     = 1,/*warning*/
        DOOR_OPEN_CHECK_ENABLE_PAUSE_PRINT = 2,/*pause print*/
    };

    enum DeviceMode : unsigned int
    {
        DEVICE_MODE_FDM   = 0x00000001,
        DEVICE_MODE_LASER = 0x00000010,
        DEVICE_MODE_CUT   = 0x00000100,
    };

    bool        file_model_download{false};
    bool        virtual_camera{false};

    int nozzle_setting_hold_count = 0;

    bool xcam_ai_monitoring{ false };
    time_t xcam_ai_monitoring_hold_start = 0;
    std::string xcam_ai_monitoring_sensitivity;
    bool xcam_buildplate_marker_detector{ false };
    int  xcam_buildplate_marker_hold_count = 0;
    bool xcam_auto_recovery_step_loss{ false };
    bool xcam_allow_prompt_sound{ false };
    bool xcam_filament_tangle_detect{ false };
    int  xcam_auto_recovery_hold_count = 0;
    int  xcam_prompt_sound_hold_count = 0;
    int  xcam_filament_tangle_detect_count = 0;
    int  nozzle_selected_count = 0;
    bool flag_update_nozzle = {true};

    /*target from Studio-SwitchBoard, default to INVALID_NOZZLE_ID if no switching control from PC*/
    int targ_nozzle_id_from_pc = INVALID_NOZZLE_ID;

    //supported features
    bool is_support_chamber_edit{false};
    bool is_support_extrusion_cali{false};
    bool is_support_first_layer_inspect{false};
    bool is_support_ai_monitoring {false};
    bool is_support_lidar_calibration {false};

    bool is_support_build_plate_marker_detect{false};
    PlateMakerDectect m_plate_maker_detect_type{ POS_CHECK };

    bool is_support_pa_calibration{false};
    bool is_support_flow_calibration{false};
    bool is_support_auto_flow_calibration{false};/*The flag is used to distinguish auto/on/off or on/off */
    bool is_support_print_without_sd{false};
    bool is_support_print_all{false};
    bool is_support_send_to_sdcard {false};
    bool is_support_aux_fan {false};
    bool is_support_chamber_fan{false};
    bool is_support_filament_backup{false};
    bool is_support_show_filament_backup{false};/*the filament is not supported shown in some previous hardware*/
    bool is_support_timelapse{false};
    bool is_support_update_remain{false};
    bool is_support_auto_leveling{false};
    bool is_support_auto_recovery_step_loss{false};
    bool is_support_ams_humidity {false};
    bool is_support_prompt_sound{false};
    bool is_support_filament_tangle_detect{false};
    bool is_support_1080dpi {false};
    bool is_support_cloud_print_only {false};
    bool is_support_command_ams_switch{false};
    bool is_support_mqtt_alive {false};
    bool is_support_tunnel_mqtt{false};
    bool is_support_motor_noise_cali{false};
    bool is_support_nozzle_offset_cali{ false };
    bool is_support_high_tempbed_cali{ false };
    bool is_support_wait_sending_finish{false};
    bool is_support_user_preset{false};
    //bool is_support_p1s_plus{false};
    bool is_support_nozzle_blob_detection{false};
    bool is_support_air_print_detection{false};
    bool is_support_agora{false};
    bool is_support_upgrade_kit{false};
    bool is_support_filament_setting_inprinting{false};
    bool is_support_internal_timelapse { false };// fun[28], support timelapse without SD card
    bool is_support_command_homing { false };// fun[32]

    bool installed_upgrade_kit{false};
    int  bed_temperature_limit = -1;

    /*nozzle temp range*/
    std::vector<int>    nozzle_temp_range;

    /*temp temp range*/
    std::vector<int>    bed_temp_range;

    /* sdcard */
    MachineObject::SdcardState sdcard_state { NO_SDCARD };
    MachineObject::SdcardState get_sdcard_state();

    /* HMS */
    std::vector<HMSItem>    hms_list;

    /* machine mqtt apis */
    int connect(bool is_anonymous = false, bool use_openssl = true);
    int disconnect();

    json_diff print_json;

    /* Project Task and Sub Task */
    std::string  project_id_;
    std::string  profile_id_;
    std::string  task_id_;
    std::string  subtask_id_;
    std::string  job_id_;
    std::string  last_subtask_id_;
    BBLSliceInfo* slice_info {nullptr};
    boost::thread* get_slice_info_thread { nullptr };
    boost::thread* get_model_task_thread { nullptr };

    /* key: sequence id, value: callback */
    std::map<std::string, CommandCallBack> m_callback_list;

    bool is_makeworld_subtask();

    /* device type */
    DeviceMode  m_device_mode{ DEVICE_MODE_FDM };
    inline bool is_fdm_type() const { return m_device_mode == DEVICE_MODE_FDM; }

    int m_plate_index { -1 };
    std::string m_gcode_file;
    int gcode_file_prepare_percent = 0;
    BBLSubTask* subtask_;
    BBLModelTask *model_task { nullptr };
    RatingInfo*  rating_info { nullptr };
    int           request_model_result             = 0;
    bool          get_model_mall_result_need_retry = false;

    std::string obj_subtask_id;     // subtask_id == 0 for sdcard
    std::string subtask_name;
    bool is_sdcard_printing();
    bool is_timelapse();
    bool is_recording_enable();
    bool is_recording();


    int get_liveview_remote();
    int get_file_remote();

    MachineObject(NetworkAgent* agent, std::string name, std::string id, std::string ip);
    ~MachineObject();

    std::string parse_version();
    void parse_version_func();
    bool is_studio_cmd(int seq);

    /* quick check*/
    bool canEnableTimelapse() const;

    /* command commands */
    int command_get_version(bool with_retry = true);
    int command_request_push_all(bool request_now = false);
    int command_pushing(std::string cmd);
    int command_clean_print_error(std::string task_id, int print_error);
    int command_set_printer_nozzle(std::string nozzle_type, float diameter);
    int command_set_printer_nozzle2(int id, std::string nozzle_type, float diameter);
    int command_get_access_code();
    int command_select_extruder(int id);

    /* command upgrade */
    int command_upgrade_confirm();
    int command_consistency_upgrade_confirm();
    int command_upgrade_firmware(FirmwareInfo info);
    int command_upgrade_module(std::string url, std::string module_type, std::string version);

    /* control apis */
    int command_xyz_abs();
    int command_auto_leveling();
    int command_go_home();
    int command_go_home2();
    int command_control_fan(int fan_type, int val);   // Old protocol
    int command_control_fan_new(int fan_id, int val, const CommandCallBack &cb); // New protocol
    int command_control_air_duct(int mode_id, const CommandCallBack& cb);
    int command_task_abort();
    /* cancelled the job_id */
    int command_task_cancel(std::string job_id);
    int command_task_pause();
    int command_task_resume();
    int command_hms_idle_ignore(const std::string &error_str, int type);
    int command_hms_resume(const std::string& error_str, const std::string& job_id);
    int command_hms_ignore(const std::string& error_str, const std::string& job_id);
    /* buzzer*/
    int command_stop_buzzer();

    /* temp*/
    int command_set_bed(int temp);
    int command_set_nozzle(int temp);
    int command_set_nozzle_new(int nozzle_id, int temp);
    int command_set_chamber(int temp);
    // ams controls
    //int command_ams_switch(int tray_index, int old_temp = 210, int new_temp = 210);
    int command_ams_change_filament(bool load, std::string ams_id, std::string slot_id, int old_temp = 210, int new_temp = 210);
    int command_ams_user_settings(int ams_id, bool start_read_opt, bool tray_read_opt, bool remain_flag = false);
    int command_ams_switch_filament(bool switch_filament);
    int command_ams_air_print_detect(bool air_print_detect);
    int command_ams_calibrate(int ams_id);
    int command_ams_filament_settings(int ams_id, int slot_id, std::string filament_id, std::string setting_id, std::string tray_color, std::string tray_type, int nozzle_temp_min, int nozzle_temp_max);
    int command_ams_select_tray(std::string tray_id);
    int command_ams_refresh_rfid(std::string tray_id);
    int command_ams_refresh_rfid2(int ams_id, int slot_id);
    int command_ams_control(std::string action);
    int command_set_chamber_light(LIGHT_EFFECT effect, int on_time = 500, int off_time = 500, int loops = 1, int interval = 1000);
    int command_set_chamber_light2(LIGHT_EFFECT effect, int on_time = 500, int off_time = 500, int loops = 1, int interval = 1000);
    int command_set_work_light(LIGHT_EFFECT effect, int on_time = 500, int off_time = 500, int loops = 1, int interval = 1000);
    int command_start_extrusion_cali(int tray_index, int nozzle_temp, int bed_temp, float max_volumetric_speed, std::string setting_id = "");
    int command_stop_extrusion_cali();
    int command_extrusion_cali_set(int tray_index, std::string setting_id, std::string name, float k, float n, int bed_temp = -1, int nozzle_temp = -1, float max_volumetric_speed = -1);

    // set printing speed
    int command_set_printing_speed(PrintingSpeedLevel lvl);

    //set prompt sound
    int command_set_prompt_sound(bool prompt_sound);

    //set fliament tangle detect
    int command_set_filament_tangle_detect(bool fliament_tangle_detect);


    // set print option
    int command_set_printing_option(bool auto_recovery);

    int command_nozzle_blob_detect(bool nozzle_blob_detect);

    // axis string is X, Y, Z, E
    int command_axis_control(std::string axis, double unit = 1.0f, double input_val = 1.0f, int speed = 3000);

    int command_extruder_control(int nozzle_id, double val);
    // calibration printer
    bool is_support_command_calibration();
    int command_start_calibration(bool vibration, bool bed_leveling, bool xcam_cali, bool motor_noise, bool nozzle_cali, bool bed_cali);

    // PA calibration
    int command_start_pa_calibration(const X1CCalibInfos& pa_data, int mode = 0);  // 0: automatic mode; 1: manual mode. default: automatic mode
    int command_set_pa_calibration(const std::vector<PACalibResult>& pa_calib_values, bool is_auto_cali);
    int command_delete_pa_calibration(const PACalibIndexInfo& pa_calib);
    int command_get_pa_calibration_tab(const PACalibExtruderInfo& calib_info);
    int command_get_pa_calibration_result(float nozzle_diameter);
    int commnad_select_pa_calibration(const PACalibIndexInfo& pa_calib_info);

    // flow ratio calibration
    int command_start_flow_ratio_calibration(const X1CCalibInfos& calib_data);
    int command_get_flow_ratio_calibration_result(float nozzle_diameter);

    // camera control
    int command_ipcam_record(bool on_off);
    int command_ipcam_timelapse(bool on_off);
    int command_ipcam_resolution_set(std::string resolution);
    int command_xcam_control(std::string module_name, bool on_off, std::string lvl = "");
    int command_xcam_control_ai_monitoring(bool on_off, std::string lvl);
    int command_xcam_control_first_layer_inspector(bool on_off, bool print_halt);
    int command_xcam_control_buildplate_marker_detector(bool on_off);
    int command_xcam_control_auto_recovery_step_loss(bool on_off);
    int command_xcam_control_allow_prompt_sound(bool on_off);
    int command_xcam_control_filament_tangle_detect(bool on_off);

    /* common apis */
    inline bool is_local() { return !dev_ip.empty(); }
    void set_bind_status(std::string status);
    std::string get_bind_str();
    bool can_print();
    bool can_resume();
    bool can_pause();
    bool can_abort();
    bool is_in_printing();
    bool is_in_printing_pause() const;
    bool is_in_prepare();
    bool is_printing_finished();
    bool is_core_xy();
    void reset_update_time();
    void reset();
    static bool is_in_printing_status(std::string status);

    void set_print_state(std::string status);

    bool is_connected();
    bool is_connecting();
    void set_online_state(bool on_off);
    bool is_online() { return m_is_online; }
    bool is_info_ready(bool check_version = true) const;
    bool is_camera_busy_off();

    std::vector<std::string> get_resolution_supported();
    std::vector<std::string> get_compatible_machine();

    /* Msg for display MsgFn */
    typedef std::function<void(std::string topic, std::string payload)> MsgFn;
    int publish_json(std::string json_str, int qos = 0, int flag = 0);
    int cloud_publish_json(std::string json_str, int qos = 0, int flag = 0);
    int local_publish_json(std::string json_str, int qos = 0, int flag = 0);
    int parse_json(std::string payload, bool key_filed_only = false);
    int publish_gcode(std::string gcode_str);

    std::string setting_id_to_type(std::string setting_id, std::string tray_type);
    BBLSubTask* get_subtask();
    BBLModelTask* get_modeltask();
    void set_modeltask(BBLModelTask* task);
    void update_model_task();
    void update_slice_info(std::string project_id, std::string profile_id, std::string subtask_id, int plate_idx);

    bool m_firmware_valid { false };
    bool m_firmware_thread_started { false };
    void get_firmware_info();
    bool is_firmware_info_valid();
    std::string get_string_from_fantype(int type);

    /*for more extruder*/
    bool                        is_enable_np{ false };
    bool                        is_enable_ams_np{ false };

    ExtderData                  m_extder_data;
    NozzleData                  m_nozzle_data;

    /*vi slot data*/
    std::vector<AmsTray> vt_slot;
    AmsTray parse_vt_tray(json vtray);
    /*for parse new info*/
    bool check_enable_np(const json& print) const;
    void parse_new_info(json print);
    bool is_nozzle_data_invalid();
    int  get_flag_bits(std::string str, int start, int count = 1) const;
    int get_flag_bits(int num, int start, int count = 1, int base = 10) const;

    /* Device Filament Check */
    struct FilamentData
    {
        std::set<std::string>                      checked_filament;
        std::string                                printer_preset_name;
        std::map<std::string, std::pair<int, int>> filament_list; // filament_id, pair<min temp, max temp>
    };
    std::map<std::string, FilamentData> m_nozzle_filament_data;
    void update_filament_list();
    void update_printer_preset_name();
    void check_ams_filament_valid();

    int command_handle_response(const json &response);

    /* xcam door open check*/
    bool               support_door_open_check() const { return is_support_door_open_check;};
    DoorOpenCheckState get_door_open_check_state() const { return xcam_door_open_check;};
    void               command_set_door_open_check(DoorOpenCheckState state);

    /* xcam save remove print file to local*/
    bool support_save_remote_print_file_to_storage() const { return is_support_save_remote_print_file_to_storage; };
    bool get_save_remote_print_file_to_storage() const { return xcam__save_remote_print_file_to_storage; };
    void command_set_save_remote_print_file_to_storage(bool save);

private:

    /* xcam door open check*/
    bool is_support_door_open_check = false;
    DoorOpenCheckState xcam_door_open_check  = DoorOpenCheckState::DOOR_OPEN_CHECK_DISABLE;
    time_t xcam_door_open_check_start_time   = 0;

    /* xcam save remove print file to local*/
    bool is_support_save_remote_print_file_to_storage = false;
    bool xcam__save_remote_print_file_to_storage      = false;
    time_t xcam__save_remote_print_file_to_storage_start_time = 0;
};

class DeviceManagerRefresher;
class DeviceManager
{
private:
    NetworkAgent*           m_agent { nullptr };
    DeviceManagerRefresher* m_refresher{nullptr};

public:
    static bool   EnableMultiMachine;

    DeviceManager(NetworkAgent* agent = nullptr);
    ~DeviceManager();
    NetworkAgent *get_agent() const{ return m_agent; }
    void set_agent(NetworkAgent* agent);

    std::mutex listMutex;
    std::string selected_machine;                               /* dev_id */
    std::string local_selected_machine;                         /* dev_id */
    std::map<std::string, MachineObject*> localMachineList;     /* dev_id -> MachineObject*, localMachine SSDP   */
    std::map<std::string, MachineObject*> userMachineList;      /* dev_id -> MachineObject*  cloudMachine of User */

    void start_refresher();
    void stop_refresher();

    void keep_alive();
    void check_pushing();

    static float nozzle_diameter_conver(int diame);
    static int nozzle_diameter_conver(float diame);
    static std::string nozzle_type_conver(int type);
    static int nozzle_type_conver(std::string& type);

    MachineObject* get_default_machine();
    MachineObject* get_local_selected_machine();
    MachineObject* get_local_machine(std::string dev_id);
    MachineObject* get_user_machine(std::string dev_id);
    MachineObject* get_my_machine(std::string dev_id);
    void erase_user_machine(std::string dev_id);
    void clean_user_info();
    void reload_printer_settings();

    bool set_selected_machine(std::string dev_id,  bool need_disconnect = false);
    MachineObject* get_selected_machine();
    void add_user_subscribe();
    void del_user_subscribe();

    void subscribe_device_list(std::vector<std::string> dev_list);

    /* return machine has access code and user machine if login*/
    std::map<std::string, MachineObject*> get_my_machine_list();
    std::map<std::string, MachineObject*> get_my_cloud_machine_list();
    std::string get_first_online_user_machine();
    void modify_device_name(std::string dev_id, std::string dev_name);
    void update_user_machine_list_info();
    void parse_user_print_info(std::string body);

    /* create machine or update machine properties */
    void on_machine_alive(std::string json_str);
    MachineObject* insert_local_device(const BBLocalMachine& machine, std::string connection_type, std::string bind_state, std::string version, std::string access_code);
    /* disconnect all machine connections */
    void disconnect_all();
    int query_bind_status(std::string &msg);

    // get alive machine
    std::map<std::string, MachineObject*> get_local_machine_list();
    void load_last_machine();

    std::vector<std::string> subscribe_list_cache;

    static void set_key_field_parsing(bool enable) { DeviceManager::key_field_only = enable; }

    static bool key_field_only;
    static json function_table;
    static json filaments_blacklist;
    static json filaments_printable_blacklist;

    template<typename T>
    static T get_value_from_config(std::string type_str, std::string item){
        std::string config_file = Slic3r::resources_dir() + "/printers/" + type_str + ".json";
        boost::nowide::ifstream json_file(config_file.c_str());
        try {
            json jj;
            if (json_file.is_open()) {
                json_file >> jj;
                if (jj.contains("00.00.00.00")) {
                    json const& printer = jj["00.00.00.00"];
                    if (printer.contains(item)) {
                        return printer[item].get<T>();
                    }
                }
            }
        }
        catch (...) {}
        return "";
    }

    static std::string parse_printer_type(std::string type_str);
    static std::string get_printer_display_name(std::string type_str);
    static std::string get_printer_thumbnail_img(std::string type_str);
    static std::string get_printer_ams_type(std::string type_str);
    static std::string get_printer_series(std::string type_str);
    static std::string get_printer_diagram_img(std::string type_str);
    static std::string get_printer_ams_img(std::string type_str);
    static PrinterArch get_printer_arch(std::string type_str);
    static std::string get_ftp_folder(std::string type_str);
    static bool get_printer_is_enclosed(std::string type_str);
    static bool load_filaments_blacklist_config();
    static std::vector<std::string> get_resolution_supported(std::string type_str);
    static std::vector<std::string> get_compatible_machine(std::string type_str);
    static std::vector<std::string> get_unsupport_auto_cali_filaments(std::string type_str);
    static void check_filaments_in_blacklist(std::string model_id, std::string tag_vendor, std::string tag_type, int ams_id, int slot_id, std::string tag_name, bool &in_blacklist, std::string &ac, std::string &info);
    static bool check_filaments_printable(const std::string &tag_vendor, const std::string &tag_type, int ams_id, bool &in_blacklist, std::string &ac, std::string &info);
    static boost::bimaps::bimap<std::string, std::string> get_all_model_id_with_name();
    static std::string load_gcode(std::string type_str, std::string gcode_file);
    static bool is_virtual_slot(int ams_id);
    static std::string get_filament_name_from_ams(int ams_id, int slot_id);
    static void update_local_machine(const MachineObject& m);
};

class DeviceManagerRefresher : public wxObject
{
    wxTimer       *m_timer{nullptr};
    int            m_timer_interval_msec = 1000;

    DeviceManager *m_manager{nullptr};

public:
    DeviceManagerRefresher(DeviceManager* manger);
    ~DeviceManagerRefresher();

public:
    void Start() { m_timer->Start(m_timer_interval_msec); }
    void Stop() { m_timer->Stop(); }

protected:
    virtual void on_timer(wxTimerEvent &event);
};


// change the opacity
void change_the_opacity(wxColour& colour);


class JsonValParser
{
public:
    static std::string get_longlong_val(const json& j);
};

}; // namespace Slic3r

#endif //  slic3r_DeviceManager_hpp_
