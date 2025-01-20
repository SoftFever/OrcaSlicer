#include "JusPrinChatPanel.hpp"
#include "../PresetComboBoxes.hpp"
#include "libslic3r/Config.hpp"
#include "slic3r/GUI/Jobs/OrientJob.hpp"
#include "libslic3r/Orient.hpp"
#include "JusPrinConfigUtils.hpp"

#include <iostream>
#include <wx/sizer.h>
#include <libslic3r/Model.hpp>
#include <slic3r/GUI/PartPlate.hpp>
#include <slic3r/GUI/MainFrame.hpp>


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
    // Actions for the chat page (void return)
    void_action_handlers["switch_to_classic_mode"] = &JusPrinChatPanel::handle_switch_to_classic_mode;
    void_action_handlers["show_login"] = &JusPrinChatPanel::handle_show_login;
    void_action_handlers["select_preset"] = &JusPrinChatPanel::handle_select_preset;
    void_action_handlers["discard_current_changes"] = &JusPrinChatPanel::handle_discard_current_changes;
    void_action_handlers["apply_config"] = &JusPrinChatPanel::handle_apply_config;
    void_action_handlers["add_printers"] = &JusPrinChatPanel::handle_add_printers;
    void_action_handlers["add_filaments"] = &JusPrinChatPanel::handle_add_filaments;
    void_action_handlers["start_slicer_all"] = &JusPrinChatPanel::handle_start_slicer_all;
    void_action_handlers["export_gcode"] = &JusPrinChatPanel::handle_export_gcode;
    void_action_handlers["auto_orient_object"] = &JusPrinChatPanel::handle_auto_orient_object;
    void_action_handlers["plater_undo"] = &JusPrinChatPanel::handle_plater_undo;
    void_action_handlers["refresh_oauth_token"] = &JusPrinChatPanel::handle_refresh_oauth_token;
    void_action_handlers["refresh_presets"] = &JusPrinChatPanel::handle_refresh_presets;
    void_action_handlers["refresh_plater_config"] = &JusPrinChatPanel::handle_refresh_plater_config;
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

// Sync actions for the chat page
nlohmann::json JusPrinChatPanel::handle_get_presets(const nlohmann::json& params) {
    return GetAllPresetJson();
}

nlohmann::json JusPrinChatPanel::handle_get_edited_presets(const nlohmann::json& params) {
    nlohmann::json j = JusPrinConfigUtils::GetAllEditedPresetJson();
    return j;
}

// TODO: identify the actions obsolete by v0.3 and flag them as deprecated

void JusPrinChatPanel::handle_refresh_presets(const nlohmann::json& params) {
    RefreshPresets();
}

void JusPrinChatPanel::handle_refresh_plater_config(const nlohmann::json& params) {
    RefreshPlaterConfig();
}

void JusPrinChatPanel::handle_add_printers(const nlohmann::json& params) {
    GUI::wxGetApp().CallAfter([this] {
        wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS);
        RefreshPresets();
    });
}

void JusPrinChatPanel::handle_add_filaments(const nlohmann::json& params) {
    GUI::wxGetApp().CallAfter([this] {
        wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_FILAMENTS);
        RefreshPresets();
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

    DiscardCurrentPresetChanges(); // Selecting a printer will result in selecting a filament or print preset. So we need to discard changes for all presets in order not to have the "transfer or discard" dialog pop up

    try {
        std::string  name = payload.value("name", "");
        Tab* tab = Slic3r::GUI::wxGetApp().get_tab(preset_type);
        if (tab != nullptr) {
            tab->select_preset(name, false, std::string(), false);
        }
    } catch (const std::exception& e) {
        // TODO: propogate the error to the web page
        BOOST_LOG_TRIVIAL(error) << "handle_select_preset: error selecting preset " << e.what();
    }

    RefreshPresets(); // JusPrin is the source of truth for presets. Update the web page whenever a preset changes
}

void JusPrinChatPanel::handle_discard_current_changes(const nlohmann::json& params) {
    JusPrinConfigUtils::DiscardCurrentPresetChanges();
    JusPrinConfigUtils::UpdatePresetTabs();
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

    for (const auto& item : param_item) {
        JusPrinConfigUtils::ApplyConfig(item);
    }

    JusPrinConfigUtils::UpdatePresetTabs();
}

void JusPrinChatPanel::UpdatePresetTabs() {
    std::array<Preset::Type, 2> preset_types = {Preset::Type::TYPE_PRINT, Preset::Type::TYPE_FILAMENT};

    for (const auto& preset_type : preset_types) {
        if (Tab* tab = Slic3r::GUI::wxGetApp().get_tab(preset_type)) {
            tab->reload_config();
            tab->update();
            tab->update_dirty();
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

void JusPrinChatPanel::RefreshPresets() {
    nlohmann::json allPresetsJson = GetAllPresetJson();
    UpdateEmbeddedChatState("presets", allPresetsJson.dump());
}

void JusPrinChatPanel::RefreshPlaterConfig() {
    nlohmann::json platerJson = JusPrinConfigUtils::GetPlaterConfigJson();
    UpdateEmbeddedChatState("platerConfig", platerJson.dump());
}

void JusPrinChatPanel::RefreshPlaterStatus() {
    nlohmann::json j = nlohmann::json::object();
    Slic3r::GUI::Plater* plater = Slic3r::GUI::wxGetApp().plater();

    j["currentPlate"] = nlohmann::json::object();
    j["currentPlate"]["gCodeCanExport"] = plater->get_partplate_list().get_curr_plate()->is_slice_result_ready_for_export();

    UpdateEmbeddedChatState("platerStatus", j.dump());
}

nlohmann::json JusPrinChatPanel::GetAllPresetJson() {
    return JusPrinConfigUtils::GetAllPresetJson();
}

void JusPrinChatPanel::SendAutoOrientEvent(bool canceled) {
    nlohmann::json j = nlohmann::json::object();
    j["type"] = "autoOrient";
    j["data"] = nlohmann::json::object();
    j["data"]["status"] = canceled ? "canceled" : "completed";
    CallEmbeddedChatMethod("eventReceived", j.dump());
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
    std::string action = jsonObject["action"];

    // Determine the appropriate handler based on the presence of "refId"
    if (jsonObject.contains("refId")) {
        auto json_it = json_action_handlers.find(action);
        if (json_it != json_action_handlers.end()) {
            auto retVal = (this->*(json_it->second))(jsonObject);
            std::string refId = jsonObject["refId"];
            nlohmann::json responseJson = {
                {"refId", refId},
                {"retVal", retVal}
            };
            CallEmbeddedChatMethod("setAgentActionRetVal", responseJson.dump());
        }
    } else {
        auto void_it = void_action_handlers.find(action);
        if (void_it != void_action_handlers.end()) {
            (this->*(void_it->second))(jsonObject);
        }
    }
}

void JusPrinChatPanel::RunScriptInBrowser(const wxString& script) {
    if (wxGetApp().app_config->get_bool("use_classic_mode")) {
        return;
    }

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " " << script;
    WebView::RunScript(m_browser, script);
}

void JusPrinChatPanel::DiscardCurrentPresetChanges() {
    JusPrinConfigUtils::DiscardCurrentPresetChanges();
}

}} // namespace Slic3r::GUI
