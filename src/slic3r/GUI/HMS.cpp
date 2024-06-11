#include "HMS.hpp"

#include <boost/log/trivial.hpp>


namespace Slic3r {
namespace GUI {

int get_hms_info_version(std::string& version)
{
    AppConfig* config = wxGetApp().app_config;
    if (!config)
        return -1;
    std::string hms_host = config->get_hms_host();
    if(hms_host.empty()) {
        BOOST_LOG_TRIVIAL(error) << "hms_host is empty";
        return -1;
    }
    int result = -1;
    version = "";
    std::string lang;
    std::string query_params = HMSQuery::build_query_params(lang);
    std::string url = (boost::format("https://%1%/GetVersion.php?%2%") % hms_host % query_params).str();
    Slic3r::Http http = Slic3r::Http::get(url);
    http.timeout_max(10)
        .on_complete([&result, &version](std::string body, unsigned status){
            try {
                json j = json::parse(body);
                if (j.contains("ver")) {
                    version = std::to_string(j["ver"].get<long long>());
                }
            } catch (...) {
                ;
            }
        })
        .on_error([&result](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "get_hms_info_version: body = " << body << ", status = " << status << ", error = " << error;
            result = -1;
            })
        .perform_sync();
    return result;
}

int HMSQuery::download_hms_related(std::string hms_type, json* receive_json)
{
    std::string local_version = "0";
    load_from_local(local_version, hms_type, receive_json);
    AppConfig* config = wxGetApp().app_config;
    if (!config) return -1;

    std::string hms_host = wxGetApp().app_config->get_hms_host();
    std::string lang;
    std::string query_params = HMSQuery::build_query_params(lang);
    std::string url;
    if (hms_type.compare(QUERY_HMS_INFO) == 0) {
        url = (boost::format("https://%1%/query.php?%2%&v=%3%") % hms_host % query_params % local_version).str();
    }
    else if (hms_type.compare(QUERY_HMS_ACTION) == 0) {
        url = (boost::format("https://%1%/hms/GetActionImage.php?v=%2%") % hms_host % local_version).str();
    }

    BOOST_LOG_TRIVIAL(info) << "hms: download url = " << url;
    Slic3r::Http http = Slic3r::Http::get(url);
    http.on_complete([this, receive_json, hms_type](std::string body, unsigned status) {
        try {
            json j = json::parse(body);
            if (j.contains("result")) {
                if (j["result"] == 0 && j.contains("data")) {
                    if (hms_type.compare(QUERY_HMS_INFO) == 0) {
                        (*receive_json) = j["data"];
                        this->save_local = true;
                    }
                    else if (hms_type.compare(QUERY_HMS_ACTION) == 0) {
                        (*receive_json)["data"] = j["data"];
                        this->save_local = true;
                    }
                    if (j.contains("ver"))
                        (*receive_json)["version"] = std::to_string(j["ver"].get<long long>());
                } else if (j["result"] == 201){
                    BOOST_LOG_TRIVIAL(info) << "HMSQuery: HMS info is the latest version";
                }else{
                    receive_json->clear();
                    BOOST_LOG_TRIVIAL(info) << "HMSQuery: update hms info error = " << j["result"].get<int>();
                }
            }
        } catch (...) {
            ;
        }
        })
        .timeout_max(20)
        .on_error([](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "HMSQuery: update hms info error = " << error << ", body = " << body << ", status = " << status;
        }).perform_sync();

        if (!receive_json->empty() && save_local == true) {
            save_to_local(lang, hms_type, *receive_json);
            save_local = false;
        }
    return 0;
}

int HMSQuery::load_from_local(std::string& version_info, std::string hms_type, json* load_json)
{
    if (data_dir().empty()) {
        version_info = "0";
        BOOST_LOG_TRIVIAL(error) << "HMS: load_from_local, data_dir() is empty";
        return -1;
    }
    std::string filename = get_hms_file(hms_type, HMSQuery::hms_language_code());
    auto hms_folder = (boost::filesystem::path(data_dir()) / "hms");
    if (!fs::exists(hms_folder))
        fs::create_directory(hms_folder);

    std::string dir_str = (hms_folder / filename).make_preferred().string();
    std::ifstream json_file(encode_path(dir_str.c_str()));
    try {
        if (json_file.is_open()) {
            json_file >> (*load_json);
            if ((*load_json).contains("version")) {
                version_info = (*load_json)["version"].get<std::string>();
                return 0;
            } else {
                BOOST_LOG_TRIVIAL(warning) << "HMS: load_from_local, no version info";
                return 0;
            }
        }
    } catch(...) {
        version_info = "0";
        BOOST_LOG_TRIVIAL(error) << "HMS: load_from_local failed";
        return -1;
    }
    version_info = "0";
    return 0;
}

int HMSQuery::save_to_local(std::string lang, std::string hms_type, json save_json)
{
    if (data_dir().empty()) {
        BOOST_LOG_TRIVIAL(error) << "HMS: save_to_local, data_dir() is empty";
        return -1;
    }
    std::string filename = get_hms_file(hms_type,lang);
    auto hms_folder = (boost::filesystem::path(data_dir()) / "hms");
    if (!fs::exists(hms_folder))
        fs::create_directory(hms_folder);
    std::string dir_str = (hms_folder / filename).make_preferred().string();
    std::ofstream json_file(encode_path(dir_str.c_str()));
    if (json_file.is_open()) {
        json_file << std::setw(4) << save_json << std::endl;
        json_file.close();
        return 0;
    }
    BOOST_LOG_TRIVIAL(error) << "HMS: save_to_local failed";
    return -1;
}

std::string HMSQuery::hms_language_code()
{
    AppConfig* config = wxGetApp().app_config;
    if (!config)
        // set language code to en by default
        return "en";
    std::string lang_code = wxGetApp().app_config->get_language_code();
    if (lang_code.compare("uk") == 0
        || lang_code.compare("cs") == 0
        || lang_code.compare("ru") == 0) {
        BOOST_LOG_TRIVIAL(info) << "HMS: using english for lang_code = " << lang_code;
        return "en";
    }
    else if (lang_code.empty()) {
        // set language code to en by default
        return "en";
    }
    return lang_code;
}

std::string HMSQuery::build_query_params(std::string& lang)
{
    std::string lang_code = HMSQuery::hms_language_code();
    lang = lang_code;
    std::string query_params = (boost::format("lang=%1%") % lang_code).str();
    return query_params;
}

std::string HMSQuery::get_hms_file(std::string hms_type, std::string lang)
{
    //return hms action filename
    if (hms_type.compare(QUERY_HMS_ACTION) == 0) {
        return (boost::format("hms_action.json")).str();
    }
    //return hms filename
    return (boost::format("hms_%1%.json") % lang).str();
}

wxString HMSQuery::query_hms_msg(std::string long_error_code)
{
    AppConfig* config = wxGetApp().app_config;
    if (!config) return wxEmptyString;
    std::string lang_code = HMSQuery::hms_language_code();
    return _query_hms_msg(long_error_code, lang_code);
}

wxString HMSQuery::_query_hms_msg(std::string long_error_code, std::string lang_code)
{
    if (long_error_code.empty())
        return wxEmptyString;

    if (m_hms_info_json.contains("device_hms")) {
        if (m_hms_info_json["device_hms"].contains(lang_code)) {
            for (auto item = m_hms_info_json["device_hms"][lang_code].begin(); item != m_hms_info_json["device_hms"][lang_code].end(); item++) {
                if (item->contains("ecode")) {
                    std::string temp_string =  (*item)["ecode"].get<std::string>();
                    if (boost::to_upper_copy(temp_string) == long_error_code) {
                        if (item->contains("intro")) {
                            return wxString::FromUTF8((*item)["intro"].get<std::string>());
                        }
                    }
                }
            }
            BOOST_LOG_TRIVIAL(info) << "hms: query_hms_msg, not found error_code = " << long_error_code;
        } else {
            BOOST_LOG_TRIVIAL(error) << "hms: query_hms_msg, do not contains lang_code = " << lang_code;
            // return first language
            if (!m_hms_info_json["device_hms"].empty()) {
                for (auto lang : m_hms_info_json["device_hms"]) {
                    for (auto item = lang.begin(); item != lang.end(); item++) {
                        if (item->contains("ecode")) {
                            std::string temp_string = (*item)["ecode"].get<std::string>();
                            if (boost::to_upper_copy(temp_string) == long_error_code) {
                                if (item->contains("intro")) {
                                    return wxString::FromUTF8((*item)["intro"].get<std::string>());
                                }
                            }
                        }
                    }
                }
            }
        }
    } else {
        BOOST_LOG_TRIVIAL(info) << "device_hms is not exists";
        return wxEmptyString;
    }
    return wxEmptyString;
}

wxString HMSQuery::_query_error_msg(std::string error_code, std::string lang_code)
{
    if (m_hms_info_json.contains("device_error")) {
        if (m_hms_info_json["device_error"].contains(lang_code)) {
            for (auto item = m_hms_info_json["device_error"][lang_code].begin(); item != m_hms_info_json["device_error"][lang_code].end(); item++) {
                if (item->contains("ecode") && boost::to_upper_copy((*item)["ecode"].get<std::string>()) == error_code) {
                    if (item->contains("intro")) {
                        return wxString::FromUTF8((*item)["intro"].get<std::string>());
                    }
                }
            }
            BOOST_LOG_TRIVIAL(info) << "hms: query_error_msg, not found error_code = " << error_code;
        } else {
            BOOST_LOG_TRIVIAL(error) << "hms: query_error_msg, do not contains lang_code = " << lang_code;
            // return first language
            if (!m_hms_info_json["device_error"].empty()) {
                for (auto lang : m_hms_info_json["device_error"]) {
                    for (auto item = lang.begin(); item != lang.end(); item++) {
                        if (item->contains("ecode") && boost::to_upper_copy((*item)["ecode"].get<std::string>()) == error_code) {
                            if (item->contains("intro")) {
                                return wxString::FromUTF8((*item)["intro"].get<std::string>());
                            }
                        }
                    }
                }
            }
        }
    }
    else {
        BOOST_LOG_TRIVIAL(info) << "device_error is not exists";
        return wxEmptyString;
    }
    return wxEmptyString;
}

wxString HMSQuery::_query_error_url_action(std::string long_error_code, std::string dev_id, std::vector<int>& button_action)
{
    if (m_hms_action_json.contains("data")) {
        for (auto item = m_hms_action_json["data"].begin(); item != m_hms_action_json["data"].end(); item++) {
            if (item->contains("ecode") && boost::to_upper_copy((*item)["ecode"].get<std::string>()) == long_error_code) {
                if (item->contains("device") && (boost::to_upper_copy((*item)["device"].get<std::string>()) == dev_id ||
                    (*item)["device"].get<std::string>() == "default")) {
                    if (item->contains("actions")) {
                        for (auto item_actions = (*item)["actions"].begin(); item_actions != (*item)["actions"].end(); item_actions++) {
                            button_action.emplace_back(item_actions->get<int>());
                        }
                    }
                    if (item->contains("image")) {
                        return wxString::FromUTF8((*item)["image"].get<std::string>());
                    }
                }
            }
        }
    }
    else {
        BOOST_LOG_TRIVIAL(info) << "data is not exists";
        return wxEmptyString;
    }
    return wxEmptyString;
}


wxString HMSQuery::query_print_error_msg(int print_error)
{
    char buf[32];
    ::sprintf(buf, "%08X", print_error);
    std::string lang_code = HMSQuery::hms_language_code();
    return _query_error_msg(std::string(buf), lang_code);
}

wxString HMSQuery::query_print_error_url_action(int print_error, std::string dev_id, std::vector<int>& button_action)
{
    char buf[32];
    ::sprintf(buf, "%08X", print_error);
    //The first three digits of SN number
    dev_id = dev_id.substr(0, 3);
    return _query_error_url_action(std::string(buf), dev_id, button_action);
}


int HMSQuery::check_hms_info()
{
    boost::thread check_thread = boost::thread([this] {

        download_hms_related(QUERY_HMS_INFO, &m_hms_info_json);
        download_hms_related(QUERY_HMS_ACTION, &m_hms_action_json);
        return 0;
    });
    return 0;
}

std::string get_hms_wiki_url(std::string error_code)
{
    AppConfig* config = wxGetApp().app_config;
    if (!config) return "";

    std::string hms_host = wxGetApp().app_config->get_hms_host();
    std::string lang_code = HMSQuery::hms_language_code();
    std::string url = (boost::format("https://%1%/index.php?e=%2%&s=device_hms&lang=%3%")
        % hms_host
        % error_code
        % lang_code).str();

    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return url;
    MachineObject* obj = dev->get_selected_machine();
    if (!obj) return url;

    if (!obj->dev_id.empty()) {
        url = (boost::format("https://%1%/index.php?e=%2%&d=%3%&s=device_hms&lang=%4%")
                       % hms_host
                       % error_code
                       % obj->dev_id
                       % lang_code).str();
    }
    return url;
}

std::string get_error_message(int error_code)
{
	char buf[64];
    std::string result_str = "";
    std::sprintf(buf,"%08X",error_code);
    std::string hms_host = wxGetApp().app_config->get_hms_host();
    std::string get_lang = wxGetApp().app_config->get_language_code();

    std::string url = (boost::format("https://%1%/query.php?lang=%2%&e=%3%")
                        %hms_host
                        %get_lang
                        %buf).str();

    Slic3r::Http http = Slic3r::Http::get(url);
    http.header("accept", "application/json")
        .timeout_max(10)
        .on_complete([get_lang, &result_str](std::string body, unsigned status) {
            try {
                json j = json::parse(body);
                if (j.contains("result")) {
                    if (j["result"].get<int>() == 0) {
                        if (j.contains("data")) {
                            json jj = j["data"];
                            if (jj.contains("device_error")) {
                                if (jj["device_error"].contains(get_lang)) {
                                    if (jj["device_error"][get_lang].size() > 0) {
                                        if (!jj["device_error"][get_lang][0]["intro"].empty() || !jj["device_error"][get_lang][0]["ecode"].empty()) {
                                            std::string error_info = jj["device_error"][get_lang][0]["intro"].get<std::string>();
                                            std::string error_code = jj["device_error"][get_lang][0]["ecode"].get<std::string>();
                                            error_code.insert(4, " ");
                                            result_str = from_u8(error_info).ToStdString() + "[" + error_code + "]";
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            } catch (...) {
             ;
        }
        })
        .on_error([](std::string body, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(trace) << boost::format("[BBL ErrorMessage]: status=%1%, error=%2%, body=%3%") % status % error % body;
        }).perform_sync();

        return result_str;
}

}
}
