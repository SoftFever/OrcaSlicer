#include "JusPrinChatPanel.hpp"
#include "../PresetComboBoxes.hpp"
#include "libslic3r/Config.hpp"

#include <iostream>
#include <wx/sizer.h>
#include <libslic3r/Model.hpp>
#include <slic3r/GUI/PartPlate.hpp>
#include <slic3r/GUI/MainFrame.hpp>


namespace Slic3r { namespace GUI {


nlohmann::json PresetsToJSON(const std::vector<std::pair<const Preset*, bool>>& presets)
{
    nlohmann::json j_array = nlohmann::json::array();
    for (const auto& [preset, is_selected] : presets) {
        nlohmann::json j;
        j["name"] = preset->name;
        j["is_default"] = preset->is_default;
        j["is_selected"] = is_selected;
        j["config"] = preset->config.to_json(preset->name, "", preset->version.to_string(), preset->custom_defined);
        j_array.push_back(j);
    }
    return j_array;
}


JusPrinChatPanel::JusPrinChatPanel(wxWindow* parent) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    init_action_handlers();

    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);

    // Create the webview
    m_browser = WebView::CreateWebView(this, "");
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }

    m_browser->Bind(wxEVT_WEBVIEW_ERROR, &JusPrinChatPanel::OnError, this);
    m_browser->Bind(wxEVT_WEBVIEW_LOADED, &JusPrinChatPanel::OnLoaded, this);
    m_browser->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &JusPrinChatPanel::OnActionCallReceived, this);

    topsizer->Add(m_browser, 1, wxEXPAND);
    SetSizer(topsizer);

    update_mode();

    // Zoom
    m_zoomFactor = 100;

    // Connect the idle events
    Bind(wxEVT_CLOSE_WINDOW, &JusPrinChatPanel::OnClose, this);

    load_url();
}

JusPrinChatPanel::~JusPrinChatPanel()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Start";
    SetEvtHandlerEnabled(false);

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " End";
}

void JusPrinChatPanel::reload() {
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " reload() called";

    m_chat_page_loaded = false;
    m_browser->Reload();
}

void JusPrinChatPanel::update_mode() { m_browser->EnableAccessToDevTools(wxGetApp().app_config->get_bool("developer_mode")); }

void JusPrinChatPanel::OnClose(wxCloseEvent& evt) { this->Hide(); }

void JusPrinChatPanel::init_action_handlers() {
    // Actions for preload.html only
    action_handlers["init_server_url_and_redirect"] = &JusPrinChatPanel::handle_init_server_url_and_redirect;

    // Actions for the chat page
    action_handlers["switch_to_classic_mode"] = &JusPrinChatPanel::handle_switch_to_classic_mode;
    action_handlers["show_login"] = &JusPrinChatPanel::handle_show_login;
    action_handlers["select_preset"] = &JusPrinChatPanel::handle_select_preset;
    action_handlers["apply_config"] = &JusPrinChatPanel::handle_apply_config;
    action_handlers["add_printers"] = &JusPrinChatPanel::handle_add_printers;
    action_handlers["add_filaments"] = &JusPrinChatPanel::handle_add_filaments;
    action_handlers["start_slicer_all"] = &JusPrinChatPanel::handle_start_slicer_all;
    action_handlers["export_gcode"] = &JusPrinChatPanel::handle_export_gcode;

    action_handlers["refresh_oauth_token"] = &JusPrinChatPanel::handle_refresh_oauth_token;
    action_handlers["refresh_presets"] = &JusPrinChatPanel::handle_refresh_presets;
    action_handlers["refresh_plater_config"] = &JusPrinChatPanel::handle_refresh_plater_config;
}

// Actions for preload.html only
void JusPrinChatPanel::handle_init_server_url_and_redirect(const nlohmann::json& params) {
    wxString strJS = wxString::Format(
        "var CHAT_SERVER_URL = '%s'; checkAndRedirectToChatServer();",
        wxGetApp().app_config->get_with_default("jusprin_server", "server_url", "https://app.obico.io/jusprin"));
    WebView::RunScript(m_browser, strJS);
}

// Actions for the chat page
void JusPrinChatPanel::handle_switch_to_classic_mode(const nlohmann::json& params) {
    wxGetApp().set_classic_mode(true);
}

void JusPrinChatPanel::handle_show_login(const nlohmann::json& params) {
    GUI::wxGetApp().CallAfter([this] {
        wxGetApp().show_jusprin_login();
    });
}

void JusPrinChatPanel::handle_refresh_oauth_token(const nlohmann::json& params) {
    UpdateOAuthAccessToken();
}

void JusPrinChatPanel::handle_refresh_presets(const nlohmann::json& params) {
    RefreshPresets();
}

void JusPrinChatPanel::handle_refresh_plater_config(const nlohmann::json& params) {
    RefreshPlaterConfig();
}

void JusPrinChatPanel::handle_add_printers(const nlohmann::json& params) {
    GUI::wxGetApp().CallAfter([this] {
        wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS);
        reload();
    });
}

void JusPrinChatPanel::handle_add_filaments(const nlohmann::json& params) {
    GUI::wxGetApp().CallAfter([this] {
    wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_FILAMENTS);
    reload();
    });

}

void JusPrinChatPanel::handle_select_preset(const nlohmann::json& params)
{
    nlohmann::json payload = params.value("payload", nlohmann::json::object());
    if (payload.is_null()) {
        BOOST_LOG_TRIVIAL(error) << "handle_select_preset: missing payload parameter";
        return;
    }
    Preset::Type preset_type;
    std::string  type = payload.value("type", "");
    if (type == "print") {
        preset_type = Preset::Type::TYPE_PRINT;
    } else if (type == "filament") {
        preset_type = Preset::Type::TYPE_FILAMENT;
    } else if (type == "printer") {
        preset_type = Preset::Type::TYPE_PRINTER;
    } else {
        BOOST_LOG_TRIVIAL(error) << "handle_select_preset: invalid type parameter";
        return;
    }

    try {
        std::string  name = payload.value("name", "");
        Tab* tab = Slic3r::GUI::wxGetApp().get_tab(preset_type);
        if (tab != nullptr) {
            tab->m_presets->discard_current_changes();
            tab->select_preset(name, false, std::string(), false);
        }
    } catch (const std::exception& e) {
        // TODO: propogate the error to the web page
        BOOST_LOG_TRIVIAL(error) << "handle_select_preset: error selecting preset " << e.what();
    }

    RefreshPresets(); // JusPrin is the source of truth for presets. Update the web page whenever a preset changes

    // Start a few chat session when printer or filament preset changes to make things simpler for now
    if (preset_type == Preset::Type::TYPE_PRINTER || preset_type == Preset::Type::TYPE_FILAMENT) {
        reload();
    }
}

void JusPrinChatPanel::handle_apply_config(const nlohmann::json& params) {
    nlohmann::json param_item = params.value("payload", nlohmann::json::object());
    if (param_item.is_null()) {
        BOOST_LOG_TRIVIAL(error) << "handle_apply_config: missing payload parameter";
        return;
    }

    if (!param_item.is_array()) {
        BOOST_LOG_TRIVIAL(error) << "handle_apply_config: payload is not an array";
        return;
    }

    if (param_item.empty()) {
        return;
    }


    std::array<Preset::Type, 2> preset_types = {Preset::Type::TYPE_PRINT, Preset::Type::TYPE_FILAMENT};

    for (const auto& preset_type : preset_types) {
        if (Tab* tab = Slic3r::GUI::wxGetApp().get_tab(preset_type)) {
            tab->m_presets->discard_current_changes();
        }
    }

    for (const auto& item : param_item) {
        ApplyConfig(item);
    }

    for (const auto& preset_type : preset_types) {
        if (Tab* tab = Slic3r::GUI::wxGetApp().get_tab(preset_type)) {
            tab->reload_config();
            tab->update();
            tab->update_dirty();
        }
    }
}

void JusPrinChatPanel::ApplyConfig(const nlohmann::json& item) {
    std::string  type = item.value("type", "");
    Preset::Type preset_type;
    if (type == "print") {
        preset_type = Preset::Type::TYPE_PRINT;
    } else if (type == "filament") {
        preset_type = Preset::Type::TYPE_FILAMENT;
    } else {
        BOOST_LOG_TRIVIAL(error) << "handle_apply_config: invalid type parameter";
        return;
    }

    Tab* tab = Slic3r::GUI::wxGetApp().get_tab(preset_type);
    if (tab != nullptr) {
        try {
            DynamicPrintConfig* config = tab->get_config();
            if (!config) return;

        ConfigSubstitutionContext context(ForwardCompatibilitySubstitutionRule::Enable);
            config->set_deserialize(item.value("key", ""), item["value"], context);
        } catch (const std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "handle_apply_config: error applying config " << e.what();
        }
    }
}

void JusPrinChatPanel::handle_start_slicer_all(const nlohmann::json& params) {
    GUI::wxGetApp().CallAfter([this] {
        wxGetApp().mainframe->start_slicer_all();
    });
}

void JusPrinChatPanel::handle_export_gcode(const nlohmann::json& params) {
    GUI::wxGetApp().CallAfter([this] {
        Slic3r::GUI::Plater* plater = Slic3r::GUI::wxGetApp().plater();
        plater->export_gcode(false);
    });
}

void JusPrinChatPanel::load_url()
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " load_url() called";

    m_chat_page_loaded = false;
    wxString url = wxString::Format("file://%s/web/jusprin/jusprin_chat_preload.html", from_u8(resources_dir()));
    if (m_browser == nullptr)
        return;
    m_browser->LoadURL(url);
}

void JusPrinChatPanel::UpdateOAuthAccessToken() {
    wxString strJS = wxString::Format(
        "if (typeof window.setJusPrinEmbeddedChatOauthAccessToken === 'function') {"
        "    window.setJusPrinEmbeddedChatOauthAccessToken('%s');"
        "}",
        wxGetApp().app_config->get_with_default("jusprin_server", "access_token", ""));

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " " << strJS;

    WebView::RunScript(m_browser, strJS);
}

void JusPrinChatPanel::UpdateEmbeddedChatState(const wxString& state_key, const wxString& state_value) {
    if (!m_chat_page_loaded) {
        return;
    }

    wxString strJS = wxString::Format(
        "if (typeof window.updateJusPrinEmbeddedChatState === 'function') {"
        "    window.updateJusPrinEmbeddedChatState('%s', %s);"
        "}",
        state_key, state_value);

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " " << strJS;

    WebView::RunScript(m_browser, strJS);
}

void JusPrinChatPanel::RefreshPresets() {
    nlohmann::json printerPresetsJson = GetPresetsJson(Preset::Type::TYPE_PRINTER);
    nlohmann::json filamentPresetsJson = GetPresetsJson(Preset::Type::TYPE_FILAMENT);
    nlohmann::json printPresetsJson = GetPresetsJson(Preset::Type::TYPE_PRINT);

    nlohmann::json allPresetsJson = {
        {"printerPresets", printerPresetsJson},
        {"filamentPresets", filamentPresetsJson},
        {"printProcessPresets", printPresetsJson}
    };
    wxString allPresetsStr = allPresetsJson.dump();
    UpdateEmbeddedChatState("presets", allPresetsStr);
}

void JusPrinChatPanel::RefreshPlaterConfig() {
    nlohmann::json platerJson = GetPlaterConfigJson();
    UpdateEmbeddedChatState("platerConfig", platerJson.dump());
}

void JusPrinChatPanel::RefreshPlaterStatus() {
    nlohmann::json j = nlohmann::json::object();
    Slic3r::GUI::Plater* plater = Slic3r::GUI::wxGetApp().plater();

    j["currentPlate"] = nlohmann::json::object();
    j["currentPlate"]["gCodeCanExport"] = plater->get_partplate_list().get_curr_plate()->is_slice_result_ready_for_export();

    UpdateEmbeddedChatState("platerStatus", j.dump());
}


nlohmann::json JusPrinChatPanel::GetPresetsJson(Preset::Type type) {
    Tab* tab = Slic3r::GUI::wxGetApp().get_tab(type);
    if (!tab) {
        return nlohmann::json::array();
    }

    TabPresetComboBox* combo = tab->get_combo_box();
    std::vector<std::pair<const Preset*, bool>> presets;

    // It doesn't seem that PresetComboBox keeps a list of available presets. So we will have to go by the combo box text then look up the preset
    for (unsigned int i = 0; i < combo->GetCount(); i++) {
        std::string preset_name = combo->GetString(i).ToUTF8().data();

        if (preset_name.substr(0, 5) == "-----") continue;   // Skip separator

        // Orca Slicer adds "* " to the preset name to indicate that it has been modified. But the underlying preset name is without the "* " prefix
        if (preset_name.substr(0, 2) == "* ") {
            preset_name = preset_name.substr(2);
        }

        const Preset* preset = tab->m_presets->find_preset(preset_name, false);
        if (preset) {
            presets.push_back({preset, combo->GetSelection() == i});
        }
    }

    return PresetsToJSON(presets);
}

nlohmann::json JusPrinChatPanel::GetPlaterConfigJson()
{
    nlohmann::json j = nlohmann::json::object();
    Slic3r::GUI::Plater* plater = Slic3r::GUI::wxGetApp().plater();

    j["plateCount"] = plater->get_partplate_list().get_plate_list().size();

    j["modelObjects"] = nlohmann::json::array();

    for (const ModelObject* object :  plater->model().objects) {
        auto object_grid_config = &(object->config);

        nlohmann::json obj;
        obj["name"] = object->name;

        int extruder_id = -1;  // Default extruder ID
        auto extruder_id_ptr = static_cast<const ConfigOptionInt*>(object_grid_config->option("extruder"));
        if (extruder_id_ptr) {
            extruder_id = *extruder_id_ptr;
        }
        obj["extruderId"] = extruder_id;

        j["modelObjects"].push_back(obj);
    }

    return j;
}

void JusPrinChatPanel::OnLoaded(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": page loaded: %1% %2% %3%") % evt.GetURL() % evt.GetTarget() % evt.GetString();

    wxString chat_server_url = wxGetApp().app_config->get_with_default("jusprin_server", "server_url", "https://app.obico.io/jusprin");
    if (evt.GetURL().Contains(chat_server_url)) {
        // TODO: This callback is not triggered when a plate is added or removed
        // TODO: This callback is triggered when an object is removed, but not when an object is cloned
        wxGetApp().plater()->add_model_changed([this]() { OnPlaterChanged(); });

        AdvertiseSupportedAction();
    }

}

void JusPrinChatPanel::OnPlaterChanged() {
    reload();
}


void JusPrinChatPanel::AdvertiseSupportedAction() {
    nlohmann::json action_handlers_json = nlohmann::json::array();
    for (const auto& [action, handler] : action_handlers) {
        action_handlers_json.push_back(action);
    }
    UpdateEmbeddedChatState("supportedActions", action_handlers_json.dump());
}

// TODO: Clean up the code below this line

void JusPrinChatPanel::SendMessage(wxString  message)
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

void JusPrinChatPanel::OnError(wxWebViewEvent& evt)
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
    BOOST_LOG_TRIVIAL(error) << __FUNCTION__
                            << boost::format(": error loading page %1% %2% %3% %4%") % evt.GetURL() % evt.GetTarget() % e % evt.GetString();
}

void JusPrinChatPanel::OnActionCallReceived(wxWebViewEvent& event)
{
    m_chat_page_loaded = true;  // If we received an action call, the chat page is loaded and javascript is ready

    wxString message = event.GetString();

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": action call received: %1%") % message;

    std::string jsonString = std::string(message.mb_str());
    nlohmann::json jsonObject = nlohmann::json::parse(jsonString);
    std::string action = jsonObject["action"];

    auto it = action_handlers.find(action);
    if (it != action_handlers.end()) {
        (this->*(it->second))(jsonObject);
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
    }
}


void JusPrinChatPanel::ConfigProperty(Preset::Type preset_type, const nlohmann::json& jsonObject) {
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

void JusPrinChatPanel::FetchProperty(Preset::Type preset_type)
{
    Tab* printer_tab = Slic3r::GUI::wxGetApp().get_tab(Preset::Type::TYPE_PRINTER);
    Tab* filament_tab = Slic3r::GUI::wxGetApp().get_tab(Preset::Type::TYPE_FILAMENT);
    Tab* process_tab = Slic3r::GUI::wxGetApp().get_tab(Preset::Type::TYPE_PRINT);
    PresetBundle* preset_bundle = Slic3r::GUI::wxGetApp().preset_bundle;
    PresetCollection& printer_presets = preset_bundle->printers;
    PresetCollection& filament_presets = preset_bundle->filaments;
    PresetCollection& process_presets = preset_bundle->prints;

    PresetWithVendorProfile printer_profile = printer_presets.get_edited_preset_with_vendor_profile();
    PresetWithVendorProfile filament_profile = filament_presets.get_edited_preset_with_vendor_profile();
    PresetWithVendorProfile process_profile = process_presets.get_edited_preset_with_vendor_profile();

    // process_tab->m_presets->get_selected_preset().name;
    // process_tab->m_presets->get_selected_preset().config;

    TabPresetComboBox* combo = process_tab->get_combo_box();
    // int selection = combo->GetSelection();
    for (unsigned int i = 0; i < combo->GetCount(); i++) {
        std::string preset_name = combo->GetString(i).ToUTF8().data();
        const Preset* process_preset = process_presets.find_preset(preset_name, false);
        if (process_preset) {
            PresetWithVendorProfile process_profile1 = process_presets.get_preset_with_vendor_profile(*process_preset);
            // bool is_compatible = is_compatible_with_print(process_profile1, filament_profile, printer_profile);
        }
    }
    // std::string preset_name1 = "0.15mm Quality @MK3S 0.4";
    // process_tab->select_preset(preset_name1, false, std::string(), false);
    Tab* tab = Slic3r::GUI::wxGetApp().get_tab(preset_type);
    if (tab) {
        auto config = tab->m_config;
        const Preset& preset = tab->m_presets->get_selected_preset();
        // Convert the config to JSON string using the correct parameter types
        std::string json_str = PresetsToJSON({{&preset, true}});
        SendMessage(json_str);
    }
}

void JusPrinChatPanel::FetchPresetBundle() {
    // const DynamicPrintConfig& full_config = Slic3r::GUI::wxGetApp().preset_bundle->full_config();
    // wxString strJS = wxString::Format("updateJusPrinEmbeddedChatState('selectedFilament', %s)", ConfigToJSON(&full_config));
    // WebView::RunScript(m_browser, strJS);
}

void JusPrinChatPanel::FetchFilaments() {
    PresetBundle* preset_bundle = Slic3r::GUI::wxGetApp().preset_bundle;
    PresetCollection& filament_presets = preset_bundle->filaments;
    PresetWithVendorProfile filament_profile = filament_presets.get_edited_preset_with_vendor_profile();

    // Fix: Take the address of the config with &
    std::string json_str = PresetsToJSON({{&filament_profile.preset, true}});
    wxString strJS = wxString::Format("updateJusPrinEmbeddedChatState('selectedFilament', %s)", json_str);
    WebView::RunScript(m_browser, strJS);
}

}} // namespace Slic3r::GUI
