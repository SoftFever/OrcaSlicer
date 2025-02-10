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
    json_action_handlers["get_plates"] = &JusPrinChatPanel::handle_get_plates;
    json_action_handlers["get_plate_2d_images"] = &JusPrinChatPanel::handle_get_plate_2d_images;
    json_action_handlers["select_preset"] = &JusPrinChatPanel::handle_select_preset;
    json_action_handlers["apply_config"] = &JusPrinChatPanel::handle_apply_config;
    json_action_handlers["add_printers"] = &JusPrinChatPanel::handle_add_printers;
    json_action_handlers["add_filaments"] = &JusPrinChatPanel::handle_add_filaments;

    // Actions for the chat page (void return)
    void_action_handlers["switch_to_classic_mode"] = &JusPrinChatPanel::handle_switch_to_classic_mode;
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
    j["data"]["modelObjects"] = GetAllModelObjectsJson();
    CallEmbeddedChatMethod("processAgentEvent", j.dump());
}

void JusPrinChatPanel::SendModelObjectsChangedEvent() {
    nlohmann::json j = nlohmann::json::object();
    j["type"] = "modelObjectsChanged";
    j["data"] = GetAllModelObjectsJson();

    CallEmbeddedChatMethod("processAgentEvent", j.dump());
}

void JusPrinChatPanel::SendClassicModeChangedEvent(bool use_classic_mode) {
    nlohmann::json j = nlohmann::json::object();
    j["type"] = "classicModeChanged";
    j["data"] = nlohmann::json::object();
    j["data"]["useClassicMode"] = use_classic_mode;
    CallEmbeddedChatMethod("processAgentEvent", j.dump());
}

void JusPrinChatPanel::SendNativeErrorOccurredEvent(const std::string& error_message) {
    nlohmann::json j = nlohmann::json::object();
    j["type"] = "nativeErrorOccurred";
    j["data"] = nlohmann::json::object();
    j["data"]["errorMessage"] = error_message;
    CallEmbeddedChatMethod("processAgentEvent", j.dump());
}

// End of Agent events that are processed by the chat panel


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
    return JusPrinPresetConfigUtils::GetAllPresetJson();
}

nlohmann::json JusPrinChatPanel::handle_get_edited_presets(const nlohmann::json& params) {
    nlohmann::json j = JusPrinPresetConfigUtils::GetAllEditedPresetJson();
    return j;
}

nlohmann::json JusPrinChatPanel::handle_get_plates(const nlohmann::json& params) {
    return JusPrinPlateUtils::GetPlates(params);
}

nlohmann::json JusPrinChatPanel::handle_get_plate_2d_images(const nlohmann::json& params) {
    return JusPrinPlateUtils::GetPlate2DImages(params);
}

nlohmann::json JusPrinChatPanel::handle_add_printers(const nlohmann::json& params) {
    wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS);
    return nlohmann::json::object();
}

nlohmann::json JusPrinChatPanel::handle_add_filaments(const nlohmann::json& params) {
    wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_FILAMENTS);
    return nlohmann::json::object();
}

nlohmann::json JusPrinChatPanel::handle_select_preset(const nlohmann::json& params)
{
    nlohmann::json payload = params.value("payload", nlohmann::json::object());
    if (payload.is_null()) {
        BOOST_LOG_TRIVIAL(error) << "handle_select_preset: missing payload parameter";
        throw std::runtime_error("Missing payload parameter");
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
        throw std::runtime_error("Invalid type parameter");
    }

    JusPrinPresetConfigUtils::DiscardCurrentPresetChanges(); // Selecting a printer will result in selecting a filament or print preset. So we need to discard changes for all presets in order not to have the "transfer or discard" dialog pop up

    std::string  name = payload.value("name", "");
    Tab* tab = Slic3r::GUI::wxGetApp().get_tab(preset_type);
    if (tab != nullptr) {
        tab->select_preset(name, false, std::string(), false);
    }

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
    if (wxGetApp().app_config->get_bool("use_classic_mode")) {
        return;
    }

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " " << script;
    WebView::RunScript(m_browser, script);
}

nlohmann::json JusPrinChatPanel::CostItemsToJson(const Slic3r::orientation::CostItems& cost_items) {
    nlohmann::json j;
    j["overhang"] = cost_items.overhang;
    j["bottom"] = cost_items.bottom;
    j["bottom_hull"] = cost_items.bottom_hull;
    j["contour"] = cost_items.contour;
    j["area_laf"] = cost_items.area_laf;
    j["area_projected"] = cost_items.area_projected;
    j["volume"] = cost_items.volume;
    j["area_total"] = cost_items.area_total;
    j["radius"] = cost_items.radius;
    j["height_to_bottom_hull_ratio"] = cost_items.height_to_bottom_hull_ratio;
    j["unprintability"] = cost_items.unprintability;
    return j;
}

nlohmann::json JusPrinChatPanel::GetModelObjectFeaturesJson(const ModelObject* obj) {
    if (!obj || obj->instances.size() != 1) {
        std::string error_message = "GetModelObjectFeaturesJson: Not sure why there will be more than one instance of a model object. Skipping for now.";
        wxGetApp().sidebar().jusprin_chat_panel()->SendNativeErrorOccurredEvent(error_message);
        BOOST_LOG_TRIVIAL(error) << error_message;
        return nlohmann::json::object();
    }

    Slic3r::orientation::OrientMesh om = OrientJob::get_orient_mesh(obj->instances[0]);
    Slic3r::orientation::OrientParams params;
    params.min_volume = false;

    Slic3r::orientation::AutoOrienterDelegate orienter(&om, params, {}, {});
    Slic3r::orientation::CostItems features = orienter.get_features(om.orientation.cast<float>(), true);
    return CostItemsToJson(features);
}

nlohmann::json JusPrinChatPanel::GetAllModelObjectsJson() {
    nlohmann::json j = nlohmann::json::array();
    Plater* plater = wxGetApp().plater();

    for (const ModelObject* object : plater->model().objects) {
        auto object_grid_config = &(object->config);

        nlohmann::json obj;
        obj["id"] = std::to_string(object->id().id);
        obj["name"] = object->name;
        obj["features"] = GetModelObjectFeaturesJson(object);

        int extruder_id = -1;  // Default extruder ID
        auto extruder_id_ptr = static_cast<const ConfigOptionInt*>(object_grid_config->option("extruder"));
        if (extruder_id_ptr) {
            extruder_id = *extruder_id_ptr;
        }
        obj["extruderId"] = extruder_id;

        j.push_back(obj);
    }

    return j;
}

}} // namespace Slic3r::GUI
