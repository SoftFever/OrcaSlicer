#include "ChatConfigPanel.hpp"
#include <iostream>
#include <wx/sizer.h>


namespace Slic3r { namespace GUI {

std::string ConfigToJSON(const std::string& type,  const ConfigBase* config)
{
    nlohmann::json j;
    j["type"] = type;
    if (config == nullptr)
        return "";
    //record all the key-values
    for (const std::string &opt_key : config->keys())
    {
        const ConfigOption* opt = config->option(opt_key);
        if ( opt->is_scalar() ) {
            if (opt->type() == coString && (opt_key != "bed_custom_texture" && opt_key != "bed_custom_model"))
                //keep \n, \r, \t
                j[opt_key] = (dynamic_cast<const ConfigOptionString *>(opt))->value;
            else
                j[opt_key] = opt->serialize();
        }
        else {
            const ConfigOptionVectorBase *vec = static_cast<const ConfigOptionVectorBase*>(opt);
            //if (!vec->empty())
            std::vector<std::string> string_values = vec->vserialize();

            /*for (int i = 0; i < string_values.size(); i++)
            {
                std::string string_value = escape_string_cstyle(string_values[i]);
                j[opt_key][i] = string_value;
            }*/

            json j_array(string_values);
            j[opt_key] = j_array;
        }
    }
    return j.dump();
}

ChatConfigPanel::ChatConfigPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);

    // Create the webview
    m_browser = WebView::CreateWebView(this, "");
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }

    m_browser->Bind(wxEVT_WEBVIEW_ERROR, &ChatConfigPanel::OnError, this);
    m_browser->Bind(wxEVT_WEBVIEW_LOADED, &ChatConfigPanel::OnLoaded, this);
    m_browser->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &ChatConfigPanel::OnScriptMessageReceived, this);

    topsizer->Add(m_browser, 1, wxEXPAND);
    SetSizer(topsizer);

    update_mode();

    // Zoom
    m_zoomFactor = 100;

    // Connect the idle events
    Bind(wxEVT_CLOSE_WINDOW, &ChatConfigPanel::OnClose, this);

    load_url();
}

ChatConfigPanel::~ChatConfigPanel()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Start";
    SetEvtHandlerEnabled(false);

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " End";
}

void ChatConfigPanel::load_url()
{
    wxString url = wxString::Format("file://%s/web/jusprin/chat_config_test.html", from_u8(resources_dir()));
    if (m_browser == nullptr)
        return;


    m_browser->LoadURL(url);
    // m_browser->SetFocus();
    //UpdateState();
}

void ChatConfigPanel::reload() { m_browser->Reload(); }

void ChatConfigPanel::update_mode() { m_browser->EnableAccessToDevTools(wxGetApp().app_config->get_bool("developer_mode")); }

void ChatConfigPanel::UpdateState()
{
    // SetTitle(m_browser->GetCurrentTitle());
}

void ChatConfigPanel::OnClose(wxCloseEvent& evt) { this->Hide(); }

void ChatConfigPanel::SendMessage(wxString  message)
{
    wxString script = wxString::Format(R"(
    // Check if window.fetch exists before overriding
    if (window.onGUIMessage) {
        window.onGUIMessage('%s');
    }
)",
                                       message);
    WebView::RunScript(m_browser, script);
}

void ChatConfigPanel::OnError(wxWebViewEvent& evt)
{
    auto e = "unknown error";
    switch (evt.GetInt()) {
    case wxWEBVIEW_NAV_ERR_CONNECTION: e = "wxWEBVIEW_NAV_ERR_CONNECTION"; break;
    case wxWEBVIEW_NAV_ERR_CERTIFICATE: e = "wxWEBVIEW_NAV_ERR_CERTIFICATE"; break;
    case wxWEBVIEW_NAV_ERR_AUTH: e = "wxWEBVIEW_NAV_ERR_AUTH"; break;
    case wxWEBVIEW_NAV_ERR_SECURITY: e = "wxWEBVIEW_NAV_ERR_SECURITY"; break;
    case wxWEBVIEW_NAV_ERR_NOT_FOUND: e = "wxWEBVIEW_NAV_ERR_NOT_FOUND"; break;
    case wxWEBVIEW_NAV_ERR_REQUEST: e = "wxWEBVIEW_NAV_ERR_REQUEST"; break;
    case wxWEBVIEW_NAV_ERR_USER_CANCELLED: e = "wxWEBVIEW_NAV_ERR_USER_CANCELLED"; break;
    case wxWEBVIEW_NAV_ERR_OTHER: e = "wxWEBVIEW_NAV_ERR_OTHER"; break;
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                            << boost::format(": error loading page %1% %2% %3% %4%") % evt.GetURL() % evt.GetTarget() % e % evt.GetString();
}

void ChatConfigPanel::OnLoaded(wxWebViewEvent& evt)
{
    if (evt.GetURL().IsEmpty())
        return;

}

void ChatConfigPanel::OnScriptMessageReceived(wxWebViewEvent& event)
{
    wxString message = event.GetString();
    std::string  jsonString = std::string(message.mb_str());
    nlohmann::json jsonObject = nlohmann::json::parse(jsonString);
    std::string action = jsonObject["action"];

    if (action == "fetch_preset_bundle")
    {
        FetchPresetBundle();
        return;
    }

    Preset::Type   preset_type;
    std::string    type = jsonObject["type"];
    if (type == "TYPE_PRINT") {
        preset_type = Preset::Type::TYPE_PRINT;
    } else if (type == "TYPE_PRINTER") {
        preset_type = Preset::Type::TYPE_PRINTER;
    } else if (type == "TYPE_FILAMENT") {
        preset_type = Preset::Type::TYPE_FILAMENT;
    } else if (type == "TYPE_SLA_MATERIAL") {
        preset_type = Preset::Type::TYPE_SLA_MATERIAL;
    } else if (type == "TYPE_PRINTER") {
        preset_type = Preset::Type::TYPE_PRINTER;
    } else if (type == "TYPE_COUNT") {
        preset_type = Preset::Type::TYPE_COUNT;
    } else if (type == "TYPE_PHYSICAL_PRINTER") {
        preset_type = Preset::Type::TYPE_PHYSICAL_PRINTER;
    } else if (type == "TYPE_PLATE") {
        preset_type = Preset::Type::TYPE_PLATE;
    }

    if(action == "config_property"){
        ConfigProperty(preset_type, jsonObject);
    } else if (action == "fetch_property") {
        FetchProperty(preset_type, "FETCH_"+type);
    }
}
void ChatConfigPanel::ConfigProperty(Preset::Type preset_type, const nlohmann::json& jsonObject) {
    std::string key  = jsonObject["key"];
    Tab* tab = Slic3r::GUI::wxGetApp().get_tab(preset_type);
    if (tab) {
        if (jsonObject["value"].is_string()) {
            std::string value = jsonObject["value"].get<std::string>();
            tab->ApplyConfig(key, value);
        } else if (jsonObject["value"].is_number_integer()) {
            auto value = jsonObject["value"].get<int>();
            tab->ApplyConfig(key, value);
        } else if (jsonObject["value"].is_number_float()) {
            auto value = jsonObject["value"].get<float>();
            tab->ApplyConfig(key, value);
        } else if (jsonObject["value"].is_boolean()) {
            auto value = jsonObject["value"].get<bool>();
            tab->ApplyConfig(key, value);
        } else  if (jsonObject["value"].is_array()) {
            auto values = jsonObject["value"];
            if(values.size() == 0) return;
            if (values[0].is_string()) {   
                std::vector<std::string> vec_value;
                for (const auto& item : jsonObject["value"]) {
                    vec_value.push_back(item.get<std::string>());
                }
                tab->ApplyConfig(key, vec_value);
            }
            else if(values[0].is_number_integer()) {
                std::vector<int> vec_value;
                for (const auto& item : jsonObject["value"]) {
                    vec_value.push_back(item.get<int>());
                }
                tab->ApplyConfig(key, vec_value);
            }else if(values[0].is_number_float()) {
                std::vector<double> vec_value;
                for (const auto& item : jsonObject["value"]) {
                    vec_value.push_back(item.get<float>());
                }
                tab->ApplyConfig(key, vec_value);
            }else if(values[0].is_boolean()) {
                std::vector<unsigned char> vec_value;
                for (const auto& item : jsonObject["value"]) {
                    vec_value.push_back((unsigned char)item.get<bool>());
                }
                tab->ApplyConfig(key, vec_value);
            }
        }
        
        else {
            // 处理其他类型或抛出异常
            //throw std::runtime_error("Unsupported JSON value type");
            return;
        }
    }
}

void ChatConfigPanel::FetchProperty(Preset::Type preset_type, const std::string& type)
{
    Tab* tab = Slic3r::GUI::wxGetApp().get_tab(preset_type);
    if (tab) {
        auto config = tab->m_config;
        SendMessage(ConfigToJSON(type, config));
    }

     Slic3r::GUI::wxGetApp().preset_bundle->full_config();
}

void ChatConfigPanel::FetchPresetBundle() { 
    SendMessage(ConfigToJSON("FETCH_PresetBundle", dynamic_cast<ConfigBase*>(&Slic3r::GUI::wxGetApp().preset_bundle->full_config())));
}

}} // namespace Slic3r::GUI