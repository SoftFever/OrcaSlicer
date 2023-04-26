#ifndef __BAMBU_NETWORKING_HPP__
#define __BAMBU_NETWORKING_HPP__

#include <string>
#include <functional>

namespace BBL {

#define BAMBU_NETWORK_SUCCESS                           0
#define BAMBU_NETWORK_ERR_INVALID_HANDLE                -1
#define BAMBU_NETWORK_ERR_CONNECT_FAILED                -2
#define BAMBU_NETWORK_ERR_DISCONNECT_FAILED             -3
#define BAMBU_NETWORK_ERR_SEND_MSG_FAILED               -4
#define BAMBU_NETWORK_ERR_BIND_FAILED                   -5
#define BAMBU_NETWORK_ERR_UNBIND_FAILED                 -6
#define BAMBU_NETWORK_ERR_PRINT_FAILED                  -7
#define BAMBU_NETWORK_ERR_LOCAL_PRINT_FAILED            -8
#define BAMBU_NETWORK_ERR_REQUEST_SETTING_FAILED        -9
#define BAMBU_NETWORK_ERR_PUT_SETTING_FAILED            -10
#define BAMBU_NETWORK_ERR_GET_SETTING_LIST_FAILED       -11
#define BAMBU_NETWORK_ERR_DEL_SETTING_FAILED            -12
#define BAMBU_NETWORK_ERR_GET_USER_PRINTINFO_FAILED     -13
#define BAMBU_NETWORK_ERR_GET_PRINTER_FIRMWARE_FAILED   -14
#define BAMBU_NETWORK_ERR_QUERY_BIND_INFO_FAILED        -15
#define BAMBU_NETWORK_ERR_MODIFY_PRINTER_NAME_FAILED    -16
#define BAMBU_NETWORK_ERR_FILE_NOT_EXIST                -17
#define BAMBU_NETWORK_ERR_FILE_OVER_SIZE                -18
#define BAMBU_NETWORK_ERR_CHECK_MD5_FAILED              -19
#define BAMBU_NETWORK_ERR_TIMEOUT                       -20
#define BAMBU_NETWORK_ERR_CANCELED                      -21
#define BAMBU_NETWORK_ERR_INVALID_PARAMS                -22
#define BAMBU_NETWORK_ERR_INVALID_RESULT                -23
#define BAMBU_NETWORK_ERR_FTP_UPLOAD_FAILED             -24
#define BAMBU_NETWORK_ERR_FTP_LOGIN_DENIED              -25
#define BAMBU_NETWORK_ERR_GET_MODEL_PUBLISH_PAGE        -26
#define BAMBU_NETWORK_ERR_GET_MODEL_MALL_HOME_PAGE      -27
#define BAMBU_NETWORK_ERR_GET_USER_INFO                 -28
#define BAMBU_NETWORK_ERR_WRONG_IP_ADDRESS              -29
#define BAMBU_NETWORK_ERR_NO_SPACE_LEFT_ON_DEVICE       -30


#define BAMBU_NETWORK_LIBRARY               "bambu_networking"
#define BAMBU_NETWORK_AGENT_NAME            "bambu_network_agent"
#define BAMBU_NETWORK_AGENT_VERSION         "01.06.02.01"

//iot preset type strings
#define IOT_PRINTER_TYPE_STRING     "printer"
#define IOT_FILAMENT_STRING         "filament"
#define IOT_PRINT_TYPE_STRING       "print"

#define IOT_JSON_KEY_VERSION            "version"
#define IOT_JSON_KEY_NAME               "name"
#define IOT_JSON_KEY_TYPE               "type"
#define IOT_JSON_KEY_UPDATE_TIME        "updated_time"
#define IOT_JSON_KEY_BASE_ID            "base_id"
#define IOT_JSON_KEY_SETTING_ID         "setting_id"
#define IOT_JSON_KEY_FILAMENT_ID        "filament_id"
#define IOT_JSON_KEY_USER_ID            "user_id"


// user callbacks
typedef std::function<void(int online_login, bool login)> OnUserLoginFn;
// printer callbacks
typedef std::function<void(std::string topic_str)>  OnPrinterConnectedFn;
typedef std::function<void(int status, std::string dev_id, std::string msg)> OnLocalConnectedFn;
typedef std::function<void()>                       OnServerConnectedFn;
typedef std::function<void(std::string dev_id, std::string msg)> OnMessageFn;
// http callbacks
typedef std::function<void(unsigned http_code, std::string http_body)> OnHttpErrorFn;
typedef std::function<std::string()>                GetCountryCodeFn;
// print callbacks
typedef std::function<void(int status, int code, std::string msg)> OnUpdateStatusFn;
typedef std::function<bool()>                       WasCancelledFn;
// local callbacks
typedef std::function<void(std::string dev_info_json_str)> OnMsgArrivedFn;

typedef std::function<void(int progress)> ProgressFn;
typedef std::function<void(int retcode, std::string info)> LoginFn;
typedef std::function<void(int result, std::string info)> ResultFn;
typedef std::function<bool()> CancelFn;

enum SendingPrintJobStage {
    PrintingStageCreate = 0,
    PrintingStageUpload = 1,
    PrintingStageWaiting = 2,
    PrintingStageSending = 3,
    PrintingStageRecord  = 4,
    PrintingStageFinished = 5,
};

enum PublishingStage {
    PublishingCreate    = 0,
    PublishingUpload    = 1,
    PublishingWaiting   = 2,
    PublishingJumpUrl   = 3,
};

enum BindJobStage {
    LoginStageConnect = 0,
    LoginStageLogin = 1,
    LoginStageWaitForLogin = 2,
    LoginStageGetIdentify = 3,
    LoginStageWaitAuth = 4,
    LoginStageFinished = 5,
};

enum ConnectStatus {
    ConnectStatusOk = 0,
    ConnectStatusFailed = 1,
    ConnectStatusLost = 2,
};

/* print job*/
struct PrintParams {
    /* basic info */
    std::string     dev_id;
    std::string     task_name;
    std::string     project_name;
    std::string     preset_name;
    std::string     filename;
    std::string     config_filename;
    int             plate_index;
    std::string     ftp_folder;
    std::string     ftp_file;
    std::string     ftp_file_md5;
    std::string     ams_mapping;
    std::string     ams_mapping_info;
    std::string     connection_type;
    std::string     comments;
    int             origin_profile_id = 0;
    std::string     origin_model_id;

    /* access options */
    std::string     dev_ip;
    bool            use_ssl;
    std::string     username;
    std::string     password;

    /*user options */
    bool            task_bed_leveling;      /* bed leveling of task */
    bool            task_flow_cali;         /* flow calibration of task */
    bool            task_vibration_cali;    /* vibration calibration of task */
    bool            task_layer_inspect;     /* first layer inspection of task */
    bool            task_record_timelapse;  /* record timelapse of task */
    bool            task_use_ams;
    std::string     task_bed_type;
    std::string     extra_options;
};

struct PublishParams {
    std::string     project_name;
    std::string     project_3mf_file;
    std::string     preset_name;
    std::string     project_model_id;
    std::string     design_id;
    std::string     config_filename;
};

}

#endif
