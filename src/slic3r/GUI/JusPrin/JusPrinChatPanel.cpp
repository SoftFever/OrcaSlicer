#include "JusPrinChatPanel.hpp"

#include <iostream>
#include <wx/sizer.h>

#include "libslic3r/Config.hpp"
#include "libslic3r/Orient.hpp"
#include "libslic3r/Model.hpp"
#include "slic3r/GUI/PresetComboBoxes.hpp"
#include "slic3r/GUI/Jobs/OrientJob.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include "JusPrinPresetConfigUtils.hpp"
#include "JusPrinPlateUtils.hpp"
#include "JusPrinView3D.hpp"


namespace Slic3r { namespace GUI {

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
    void_action_handlers["init_server_url_and_redirect"] = &JusPrinChatPanel::handle_init_server_url_and_redirect;

    // Sync actions for the chat page (return json)
    json_action_handlers["get_presets"] = &JusPrinChatPanel::handle_get_presets;
    json_action_handlers["get_edited_presets"] = &JusPrinChatPanel::handle_get_edited_presets;
    json_action_handlers["render_plate"] = &JusPrinChatPanel::handle_render_plate;
    json_action_handlers["select_preset"] = &JusPrinChatPanel::handle_select_preset;
    json_action_handlers["apply_config"] = &JusPrinChatPanel::handle_apply_config;
    json_action_handlers["add_printers"] = &JusPrinChatPanel::handle_add_printers;
    json_action_handlers["add_filaments"] = &JusPrinChatPanel::handle_add_filaments;
    json_action_handlers["get_current_project"] = &JusPrinChatPanel::handle_get_current_project;
    json_action_handlers["change_chatpanel_display"] = &JusPrinChatPanel::handle_change_chatpanel_display;

    // Actions for the chat page (void return)
    void_action_handlers["show_login"] = &JusPrinChatPanel::handle_show_login;
    void_action_handlers["start_slicer_all"] = &JusPrinChatPanel::handle_start_slicer_all;
    void_action_handlers["export_gcode"] = &JusPrinChatPanel::handle_export_gcode;
    void_action_handlers["auto_orient_object"] = &JusPrinChatPanel::handle_auto_orient_object;
    void_action_handlers["plater_undo"] = &JusPrinChatPanel::handle_plater_undo;
    void_action_handlers["refresh_oauth_token"] = &JusPrinChatPanel::handle_refresh_oauth_token;
}


// Agent events that are processed by the chat panel

void JusPrinChatPanel::SendAutoOrientEvent(bool canceled) {
    nlohmann::json j = nlohmann::json::object();
    j["type"] = "autoOrient";
    j["data"] = nlohmann::json::object();
    j["data"]["status"] = canceled ? "canceled" : "completed";
    j["data"]["currentProject"] = JusPrinPlateUtils::GetCurrentProject(true);
    CallEmbeddedChatMethod("processAgentEvent", j.dump());
}

void JusPrinChatPanel::SendModelObjectsChangedEvent() {
    nlohmann::json j = nlohmann::json::object();
    j["type"] = "modelObjectsChanged";
    j["data"] = JusPrinPlateUtils::GetCurrentProject(true);

    CallEmbeddedChatMethod("processAgentEvent", j.dump());
}

void JusPrinChatPanel::SendNativeErrorOccurredEvent(const std::string& error_message) {
    nlohmann::json j = nlohmann::json::object();
    j["type"] = "nativeErrorOccurred";
    j["data"] = nlohmann::json::object();
    j["data"]["errorMessage"] = error_message;
    CallEmbeddedChatMethod("processAgentEvent", j.dump());
}

void JusPrinChatPanel::SendNotificationPushedEvent(
    const std::string& notification_text,
    const std::string& notification_type,
    const std::string& notification_level)
{
    nlohmann::json params = {
        {"type", "notificationPushed"},
        {"data", {
            {"text", notification_text}
        }}
    };

    if (!notification_type.empty()) {
        params["data"]["type"] = notification_type;
    }
    if (!notification_level.empty()) {
        params["data"]["level"] = notification_level;
    }

    CallEmbeddedChatMethod("processAgentEvent", params.dump());
}

void JusPrinChatPanel::SendChatPanelFocusEvent(const std::string& focus_event_type) {
    nlohmann::json j = nlohmann::json::object();
    j["type"] = "chatPanelFocus";
    j["data"] = nlohmann::json::object();
    j["data"]["focusEventType"] = focus_event_type;
    CallEmbeddedChatMethod("processAgentEvent", j.dump());
}

// End of Agent events that are processed by the chat panel

// Actions for preload.html only
void JusPrinChatPanel::handle_init_server_url_and_redirect(const nlohmann::json& params) {
    bool isDeveloperMode = wxGetApp().app_config->get_bool("developer_mode");
    wxString strJS = wxString::Format(
        "var CHAT_SERVER_URL = '%s'; checkAndRedirectToChatServer(developerMode =%s);",
        wxGetApp().app_config->get_with_default("jusprin_server", "server_url", "https://app.obico.io/jusprin"),
        isDeveloperMode ? "true" : "false");
    WebView::RunScript(m_browser, strJS);
}

void JusPrinChatPanel::handle_show_login(const nlohmann::json& params) {
    GUI::wxGetApp().CallAfter([this] {
        wxGetApp().show_jusprin_login();
    });
}

void JusPrinChatPanel::handle_refresh_oauth_token(const nlohmann::json& params) {
    UpdateOAuthAccessToken();
}

// Sync actions for the chat page
nlohmann::json JusPrinChatPanel::handle_get_presets(const nlohmann::json& params) {
    return JusPrinPresetConfigUtils::GetAllPresetJson();
}

nlohmann::json JusPrinChatPanel::handle_get_edited_presets(const nlohmann::json& params) {
    nlohmann::json j = JusPrinPresetConfigUtils::GetAllEditedPresetJson();
    return j;
}

nlohmann::json JusPrinChatPanel::handle_render_plate(const nlohmann::json& params) {
    return JusPrinPlateUtils::RenderPlateView(params);
}

nlohmann::json JusPrinChatPanel::handle_add_printers(const nlohmann::json& params) {
    wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS);
    return nlohmann::json::object();
}

nlohmann::json JusPrinChatPanel::handle_add_filaments(const nlohmann::json& params) {
    wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_FILAMENTS);
    return nlohmann::json::object();
}

nlohmann::json JusPrinChatPanel::handle_get_current_project(const nlohmann::json& params) {
    nlohmann::json payload = params.value("payload", nlohmann::json::object());
    bool with_model_object_features = payload.value("with_model_object_features", false);
    return JusPrinPlateUtils::GetCurrentProject(with_model_object_features);
}

nlohmann::json JusPrinChatPanel::handle_select_preset(const nlohmann::json& params)
{
    nlohmann::json payload = params.value("payload", nlohmann::json::object());
    if (payload.is_null()) {
        BOOST_LOG_TRIVIAL(error) << "handle_select_preset: missing payload parameter";
        throw std::runtime_error("Missing payload parameter");
    }

    std::string type = payload.value("type", "");
    std::string name = payload.value("name", "");

    JusPrinPresetConfigUtils::SelectPreset(type, name);
    return nlohmann::json::object();
}

nlohmann::json JusPrinChatPanel::handle_apply_config(const nlohmann::json& params) {
    nlohmann::json param_item = params.value("payload", nlohmann::json::object());
    if (param_item.is_null()) {
        BOOST_LOG_TRIVIAL(error) << "handle_apply_config: missing payload parameter";
        throw std::runtime_error("Missing payload parameter");
    }

    if (!param_item.is_array()) {
        BOOST_LOG_TRIVIAL(error) << "handle_apply_config: payload is not an array";
        throw std::runtime_error("Payload is not an array");
    }

    if (param_item.empty()) {
        return nlohmann::json::object();
    }

    for (const auto& item : param_item) {
        JusPrinPresetConfigUtils::ApplyConfig(item);
    }

    JusPrinPresetConfigUtils::UpdatePresetTabs();

    return nlohmann::json::object();
}

nlohmann::json JusPrinChatPanel::handle_change_chatpanel_display(const nlohmann::json& params) {
    nlohmann::json payload = params.value("payload", nlohmann::json::object());

    std::string display = payload.value("display", "");
    if (auto* view3d = dynamic_cast<JusPrinView3D*>(GetParent())) {
        display = view3d->changeChatPanelDisplay(display);
    } else {
        display = "none";
    }

    return nlohmann::json::object({
        {"display", display}
    });
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

void JusPrinChatPanel::handle_auto_orient_object(const nlohmann::json& params) {
    GUI::wxGetApp().CallAfter([this] {
        Slic3r::GUI::Plater* plater = Slic3r::GUI::wxGetApp().plater();
        plater->set_prepare_state(Job::PREPARE_STATE_MENU);
        plater->orient();
    });
}

void JusPrinChatPanel::handle_plater_undo(const nlohmann::json& params) {
    GUI::wxGetApp().CallAfter([this] {
        Slic3r::GUI::Plater* plater = Slic3r::GUI::wxGetApp().plater();
        plater->undo();
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

    RunScriptInBrowser(strJS);
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

    RunScriptInBrowser(strJS);
}

void JusPrinChatPanel::CallEmbeddedChatMethod(const wxString& method, const wxString& params) {
    wxString strJS = wxString::Format(
        "if (typeof window.callJusPrinEmbeddedChatMethod === 'function') {"
        "    window.callJusPrinEmbeddedChatMethod('%s', %s);"
        "}",
        method, params);
    RunScriptInBrowser(strJS);
}

void JusPrinChatPanel::RefreshPlaterStatus() {
    nlohmann::json j = nlohmann::json::object();
    Slic3r::GUI::Plater* plater = Slic3r::GUI::wxGetApp().plater();

    j["currentPlate"] = nlohmann::json::object();
    j["currentPlate"]["gCodeCanExport"] = plater->get_partplate_list().get_curr_plate()->is_slice_result_ready_for_export();

    UpdateEmbeddedChatState("platerStatus", j.dump());
}

void JusPrinChatPanel::OnLoaded(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": page loaded: %1% %2% %3%") % evt.GetURL() % evt.GetTarget() % evt.GetString();

    wxString chat_server_url = wxGetApp().app_config->get_with_default("jusprin_server", "server_url", "https://app.obico.io/jusprin");
    if (evt.GetURL().Contains(chat_server_url)) {
        AdvertiseSupportedAction();
    }

}

void JusPrinChatPanel::AdvertiseSupportedAction() {
    nlohmann::json action_handlers_json = nlohmann::json::array();
    // Add void action handlers
    for (const auto& [action, handler] : void_action_handlers) {
        action_handlers_json.push_back(action);
    }
    // Add json action handlers
    for (const auto& [action, handler] : json_action_handlers) {
        action_handlers_json.push_back(action);
    }
    UpdateEmbeddedChatState("supportedActions", action_handlers_json.dump());
}

void JusPrinChatPanel::OnActionCallReceived(wxWebViewEvent& event)
{
    m_chat_page_loaded = true;  // If we received an action call, the chat page is loaded and javascript is ready

    wxString message = event.GetString();

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": action call received: %1%") % message;

    std::string jsonString = std::string(message.mb_str());
    nlohmann::json jsonObject = nlohmann::json::parse(jsonString);

    // Determine the appropriate handler based on the presence of "refId"
    if (jsonObject.contains("refId") &&
        !jsonObject["refId"].is_null() &&
        !jsonObject["refId"].get<std::string>().empty()) {
        GUI::wxGetApp().CallAfter([this, jsonObject] {
            auto json_it = json_action_handlers.find(jsonObject["action"]);
            if (json_it != json_action_handlers.end()) {
                try {
                    auto retVal = (this->*(json_it->second))(jsonObject);
                    std::string refId = jsonObject["refId"];
                    nlohmann::json responseJson = {
                        {"refId", refId},
                        {"retVal", retVal}
                    };
                    CallEmbeddedChatMethod("setAgentActionRetVal", responseJson.dump());
                } catch (const std::exception& e) {
                    std::string refId = jsonObject["refId"];
                    nlohmann::json responseJson = {
                        {"refId", refId},
                        {"error", e.what()}
                    };
                    CallEmbeddedChatMethod("setAgentActionRetVal", responseJson.dump());
                }
            }
        });
    } else {
        std::string action = jsonObject["action"];
        auto void_it = void_action_handlers.find(action);
        if (void_it != void_action_handlers.end()) {
            (this->*(void_it->second))(jsonObject);
        }
        auto json_it = json_action_handlers.find(action);
        if (json_it != json_action_handlers.end()) {
            (this->*(json_it->second))(jsonObject);
        }
    }
}

void JusPrinChatPanel::RunScriptInBrowser(const wxString& script) {
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " " << script;
    WebView::RunScript(m_browser, script);
}

}} // namespace Slic3r::GUI
