#include "HMS.hpp"



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
    std::string url = (boost::format("https://%1%/GetVersion.php") % hms_host).str();
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

int HMSQuery::download_hms_info()
{
    AppConfig* config = wxGetApp().app_config;
    if (!config) return -1;

    std::string hms_host = wxGetApp().app_config->get_hms_host();
    std::string lang_code = wxGetApp().app_config->get_language_code();
    std::string url = (boost::format("https://%1%/query.php?lang=%2%") % hms_host % lang_code).str();

    Slic3r::Http http = Slic3r::Http::get(url);

    http.on_complete([this](std::string body, unsigned status) {
        try {
            json j = json::parse(body);
            if (j.contains("result")) {
                if (j["result"] == 0 && j.contains("data")) {
                    this->m_hms_json = j["data"];
                    if (j.contains("ver"))
                        m_hms_json["version"] = std::to_string(j["ver"].get<long long>());
                } else {
                    this->m_hms_json.clear();
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

    save_to_local();
    return 0;
}

int HMSQuery::load_from_local(std::string &version_info)
{
    if (data_dir().empty()) {
        version_info = "";
        BOOST_LOG_TRIVIAL(error) << "HMS: load_from_local, data_dir() is empty";
        return -1;
    }
    std::string filename = get_hms_file();
    std::string dir_str = (boost::filesystem::path(data_dir()) / filename).make_preferred().string();
    std::ifstream json_file(encode_path(dir_str.c_str()));
    try {
        if (json_file.is_open()) {
            json_file >> m_hms_json;
            if (m_hms_json.contains("version")) {
                version_info = m_hms_json["version"].get<std::string>();
                return 0;
            } else {
                BOOST_LOG_TRIVIAL(warning) << "HMS: load_from_local, no version info";
                return 0;
            }
        }
    } catch(...) {
        version_info = "";
        return -1;
    }
    version_info = "";
    return 0;
}

int HMSQuery::save_to_local()
{
    if (data_dir().empty()) {
        BOOST_LOG_TRIVIAL(error) << "HMS: save_to_local, data_dir() is empty";
        return -1;
    }
    std::string filename = get_hms_file();
    std::string dir_str = (boost::filesystem::path(data_dir()) / filename).make_preferred().string();
    std::ofstream json_file(encode_path(dir_str.c_str()));
    if (json_file.is_open()) {
        json_file << std::setw(4) << m_hms_json << std::endl;
        json_file.close();
        return 0;
    }
    return -1;
}

std::string HMSQuery::get_hms_file()
{
    AppConfig* config = wxGetApp().app_config;
    if (!config)
        return HMS_INFO_FILE;
    std::string lang_code = wxGetApp().app_config->get_language_code();
    return (boost::format("hms_%1%.json") % lang_code).str();
}

wxString HMSQuery::query_hms_msg(std::string long_error_code)
{
    if (long_error_code.empty())
        return wxEmptyString;
    AppConfig* config = wxGetApp().app_config;
    if (!config) return wxEmptyString;

    std::string hms_host = wxGetApp().app_config->get_hms_host();
    std::string lang_code = wxGetApp().app_config->get_language_code();

    if (m_hms_json.contains("device_hms")) {
        if (m_hms_json["device_hms"].contains(lang_code)) {
            for (auto item = m_hms_json["device_hms"][lang_code].begin(); item != m_hms_json["device_hms"][lang_code].end(); item++) {
                if (item->contains("ecode") && (*item)["ecode"].get<std::string>() == long_error_code) {
                    if (item->contains("intro")) {
                        return wxString::FromUTF8((*item)["intro"].get<std::string>());
                    }
                }
            }
            BOOST_LOG_TRIVIAL(info) << "hms: query_hms_msg, not found error_code = " << long_error_code;
        }
    } else {
        return wxEmptyString;
    }
    return wxEmptyString;
}

wxString HMSQuery::query_error_msg(std::string error_code)
{
    AppConfig* config = wxGetApp().app_config;
    if (!config) return wxEmptyString;

    std::string hms_host = wxGetApp().app_config->get_hms_host();
    std::string lang_code = wxGetApp().app_config->get_language_code();

    if (m_hms_json.contains("device_error")) {
        if (m_hms_json["device_error"].contains(lang_code)) {
            for (auto item = m_hms_json["device_error"][lang_code].begin(); item != m_hms_json["device_error"][lang_code].end(); item++) {
                if (item->contains("ecode") && (*item)["ecode"].get<std::string>() == error_code) {
                    if (item->contains("intro")) {
                        return wxString::FromUTF8((*item)["intro"].get<std::string>());
                    }
                }
            }
            BOOST_LOG_TRIVIAL(info) << "hms: query_error_msg, not found error_code = " << error_code;
        }
    }
    else {
        return wxEmptyString;
    }
    return wxEmptyString;
}

wxString HMSQuery::query_print_error_msg(int print_error)
{
    char buf[32];
    ::sprintf(buf, "%08X", print_error);
    return query_error_msg(std::string(buf));
}

int HMSQuery::check_hms_info()
{
    int result = 0;
    bool download_new_hms_info = true;

    // load local hms json file
    std::string version = "";
    if (load_from_local(version) == 0) {
        BOOST_LOG_TRIVIAL(info) << "HMS: check_hms_info current version = " << version;
        std::string new_version;
        get_hms_info_version(new_version);
        BOOST_LOG_TRIVIAL(info) << "HMS: check_hms_info latest version = " << new_version;
        if (!version.empty() && version == new_version) {
            download_new_hms_info = false;
        }
    }
    BOOST_LOG_TRIVIAL(info) << "HMS: check_hms_info need download new hms info = " << download_new_hms_info;
    // download if version is update
    if (download_new_hms_info) {
        result = download_hms_info();
    }
    return result;
}

std::string get_hms_wiki_url(int code)
{
    AppConfig* config = wxGetApp().app_config;
    if (!config) return "";

    char buf[32];
    ::sprintf(buf, "%08X", code);
    std::string error_code = std::string(buf);
    std::string hms_host = wxGetApp().app_config->get_hms_host();
    std::string lang_code = wxGetApp().app_config->get_language_code();
    std::string url = (boost::format("https://%1%/index.php?e=%2%&s=hms&lang=%3%")
                       % hms_host
                       % error_code
                       % lang_code).str();
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