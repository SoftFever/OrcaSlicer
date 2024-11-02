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
    m_browser->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &JusPrinChatPanel::OnScriptMessageReceived, this);

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

void JusPrinChatPanel::init_action_handlers() {
    action_handlers["update_presets"] = &JusPrinChatPanel::handle_update_presets;
    action_handlers["select_preset"] = &JusPrinChatPanel::handle_select_preset;
    action_handlers["add_printers"] = &JusPrinChatPanel::handle_add_printers;
    action_handlers["add_filaments"] = &JusPrinChatPanel::handle_add_filaments;
    action_handlers["start_slice_all"] = &JusPrinChatPanel::start_slice_all;
}

void JusPrinChatPanel::handle_update_presets(const nlohmann::json& params) {
    UpdatePresets();
}

void JusPrinChatPanel::handle_add_printers(const nlohmann::json& params) {
    wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_PRINTERS);
    load_url();
}

void JusPrinChatPanel::handle_add_filaments(const nlohmann::json& params) {
    wxGetApp().run_wizard(ConfigWizard::RR_USER, ConfigWizard::SP_FILAMENTS);
    load_url();
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

    std::string  name = payload.value("name", "");
    Tab* tab = Slic3r::GUI::wxGetApp().get_tab(preset_type);
    if (tab != nullptr) {
        tab->select_preset(name, false, std::string(), false);
    }

    load_url();
}
void JusPrinChatPanel::load_url()
{
    wxString url = wxString::Format("file://%s/web/jusprin/chat_config_test.html", from_u8(resources_dir()));
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
    WebView::RunScript(m_browser, strJS);
}

void JusPrinChatPanel::UpdatePresets() {
    nlohmann::json printerPresetsJson = GetPresets(Preset::Type::TYPE_PRINTER);
    nlohmann::json filamentPresetsJson = GetPresets(Preset::Type::TYPE_FILAMENT);
    if (printerPresetsJson == nullptr || filamentPresetsJson == nullptr) {
        return;
    }

    nlohmann::json allPresetsJson = {
        {"printerPresets", printerPresetsJson},
        {"filamentPresets", filamentPresetsJson}
    };
    wxString allPresetsStr = allPresetsJson.dump();
    wxString strJS = wxString::Format("updateJusPrinEmbeddedChatState('%s', %s)", "presets", allPresetsStr);
    WebView::RunScript(m_browser, strJS);
    wxString strJS1 = wxString::Format("console.log(JSON.stringify(%s))", allPresetsStr);
    WebView::RunScript(m_browser, strJS1);
}

void JusPrinChatPanel::reload() { m_browser->Reload(); }

void JusPrinChatPanel::update_mode() { m_browser->EnableAccessToDevTools(wxGetApp().app_config->get_bool("developer_mode")); }


void JusPrinChatPanel::OnClose(wxCloseEvent& evt) { this->Hide(); }

nlohmann::json JusPrinChatPanel::GetPresets(Preset::Type type) {
    Tab* tab = Slic3r::GUI::wxGetApp().get_tab(type);
    if (!tab) {
        return nullptr;
    }

    TabPresetComboBox* combo = tab->get_combo_box();
    std::vector<std::pair<const Preset*, bool>> presets;

    for (unsigned int i = 0; i < combo->GetCount(); i++) {
        std::string preset_name = combo->GetString(i).ToUTF8().data();

        if (preset_name.substr(0, 5) == "-----") continue;   // Skip separator

        const Preset* preset = tab->m_presets->find_preset(preset_name, false);
        if (preset) {
            presets.push_back({preset, combo->GetSelection() == i});
        }
    }

    return PresetsToJSON(presets);
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
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__
                            << boost::format(": error loading page %1% %2% %3% %4%") % evt.GetURL() % evt.GetTarget() % e % evt.GetString();
}

void JusPrinChatPanel::OnLoaded(wxWebViewEvent& evt)
{
    if (evt.GetURL().IsEmpty())
        return;

    wxString strJS = wxString::Format(
        "if (typeof checkAndRedirectToChatServer === 'function') {"
        "    checkAndRedirectToChatServer('%s');"
        "}",
        wxGetApp().app_config->get_with_default("jusprin_server", "server_url", "https://app.obico.io/jusprin"));
    WebView::RunScript(m_browser, strJS);
    if (wxGetApp().plater() != nullptr)
        wxGetApp().plater()->add_model_changed([this]() { SendMessage("model_changed"); });

    Slic3r::GUI::wxGetApp().mainframe->update_classic();
    UpdateOAuthAccessToken();
    UpdatePresets();
}

void JusPrinChatPanel::OnScriptMessageReceived(wxWebViewEvent& event)
{
    wxString message = event.GetString();
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

void JusPrinChatPanel::start_slice_all(const nlohmann::json& params) {
    Slic3r::GUI::wxGetApp().mainframe->start_slicer_all();
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

void JusPrinChatPanel::FetchUsedFilamentIds()
{
    Slic3r::Model&              model           = Slic3r::GUI::wxGetApp().model();
    int                         object_count    = model.objects.size();
    Slic3r::GUI::PartPlateList& partplate_list  = Slic3r::GUI::wxGetApp().plater()->get_partplate_list();
    const DynamicPrintConfig*   plater_config   = Slic3r::GUI::wxGetApp().plater()->config();
    const DynamicPrintConfig&   filament_config = *plater_config;

    nlohmann::json j;
    std::set<int>  ids;
    for (int i = 0; i < object_count; i++) {
        ModelObject* object             = model.objects[i];
        auto         object_grid_config = &(object->config);
        auto         plate_index        = partplate_list.find_instance_belongs(i, 0);
        if (plate_index == -1) {
            continue;
        }

        auto extruder_id_ptr = static_cast<const ConfigOptionInt*>(object_grid_config->option("extruder"));
        if (extruder_id_ptr) {
            ids.insert(*extruder_id_ptr);
        }
    }
    j["used_filiment_ids"] = ids;
    SendMessage(j.dump());
}

}} // namespace Slic3r::GUI
