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
    action_handlers["auto_orient_object"] = &JusPrinChatPanel::handle_auto_orient_object;

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

    try {
        std::string  name = payload.value("name", "");
        Tab* tab = Slic3r::GUI::wxGetApp().get_tab(preset_type);
        if (tab != nullptr) {
            tab->m_preset_bundle->prints.discard_current_changes();
            tab->m_preset_bundle->filaments.discard_current_changes();
            tab->select_preset(name, false, std::string(), false);
        }
    } catch (const std::exception& e) {
        // TODO: propogate the error to the web page
        BOOST_LOG_TRIVIAL(error) << "handle_select_preset: error selecting preset " << e.what();
    }

    RefreshPresets(); // JusPrin is the source of truth for presets. Update the web page whenever a preset changes

    // Start a few chat session when printer or filament preset changes to make things simpler for now
    if (preset_type == Preset::Type::TYPE_PRINTER || preset_type == Preset::Type::TYPE_FILAMENT) {
        RefreshPresets();
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

void JusPrinChatPanel::handle_auto_orient_object(const nlohmann::json& params) {
    GUI::wxGetApp().CallAfter([this] {
        Slic3r::GUI::Plater* plater = Slic3r::GUI::wxGetApp().plater();
        plater->set_prepare_state(Job::PREPARE_STATE_MENU);
        plater->orient();
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
        AdvertiseSupportedAction();
    }

}

void JusPrinChatPanel::AdvertiseSupportedAction() {
    nlohmann::json action_handlers_json = nlohmann::json::array();
    for (const auto& [action, handler] : action_handlers) {
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

    auto it = action_handlers.find(action);
    if (it != action_handlers.end()) {
        (this->*(it->second))(jsonObject);
        return;
    }
}

void JusPrinChatPanel::RunScriptInBrowser(const wxString& script) {
    if (wxGetApp().app_config->get_bool("use_classic_mode")) {
        return;
    }

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << " " << script;
    WebView::RunScript(m_browser, script);
}

}} // namespace Slic3r::GUI
