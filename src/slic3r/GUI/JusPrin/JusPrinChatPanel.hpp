#ifndef slic3r_JusPrinChatPanel_hpp_
#define slic3r_JusPrinChatPanel_hpp_

#include <wx/panel.h>
#include <wx/webview.h>

#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif
#include "slic3r/GUI/GUI_App.hpp"
#include <slic3r/GUI/Widgets/WebView.hpp>
#include "libslic3r/Utils.hpp"
#include "slic3r/GUI/Tab.hpp"
#include "nlohmann/json.hpp"
#include <map>
#include "JusPrinPresetConfigUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Orient.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/PartPlate.hpp"

namespace Slic3r { namespace GUI {

class JusPrinChatPanel : public wxPanel
{
public:
    JusPrinChatPanel(wxWindow* parent);
    virtual ~JusPrinChatPanel();
    void reload();

    // Agent events that are processed by the chat panel
    void SendAutoOrientEvent(bool canceled);
    void SendModelObjectsChangedEvent();
    void SendClassicModeChangedEvent(bool use_classic_mode);
    void SendNativeErrorOccurredEvent(const std::string& error_message);

    // End of Agent events that are processed by the chat panel

    void UpdateOAuthAccessToken();
    void RefreshPlaterStatus();


    static nlohmann::json GetModelObjectFeaturesJson(const ModelObject* obj);
    static nlohmann::json CostItemsToJson(const Slic3r::orientation::CostItems& cost_items);
    static nlohmann::json GetAllModelObjectsJson();

private:
    void load_url();
    void update_mode();
    void OnClose(wxCloseEvent& evt);
    void OnError(wxWebViewEvent& evt);
    void OnLoaded(wxWebViewEvent& evt);

    using VoidMemberFunctionPtr = void (JusPrinChatPanel::*)(const nlohmann::json&);
    using JsonMemberFunctionPtr = nlohmann::json (JusPrinChatPanel::*)(const nlohmann::json&);

    std::map<std::string, VoidMemberFunctionPtr> void_action_handlers;
    std::map<std::string, JsonMemberFunctionPtr> json_action_handlers;

    // actions for preload.html only
    void handle_init_server_url_and_redirect(const nlohmann::json& params);

    // Actions for the chat page
    void init_action_handlers();

    // Sync actions for the chat page
    nlohmann::json handle_get_presets(const nlohmann::json& params);
    nlohmann::json handle_get_edited_presets(const nlohmann::json& params);
    nlohmann::json handle_get_plates(const nlohmann::json& params);
    nlohmann::json handle_render_plate(const nlohmann::json& params);
    nlohmann::json handle_select_preset(const nlohmann::json& params);
    nlohmann::json handle_apply_config(const nlohmann::json& params);
    nlohmann::json handle_add_printers(const nlohmann::json& params);
    nlohmann::json handle_add_filaments(const nlohmann::json& params);

    // Actions to trigger events in JusPrin
    void handle_switch_to_classic_mode(const nlohmann::json& params);
    void handle_show_login(const nlohmann::json& params);
    void handle_start_slicer_all(const nlohmann::json& params);
    void handle_export_gcode(const nlohmann::json& params);
    void handle_auto_orient_object(const nlohmann::json& params);
    void handle_plater_undo(const nlohmann::json& params);

    // Actions to fetch info to be sent to the web page
    void handle_refresh_oauth_token(const nlohmann::json& params);

    void OnActionCallReceived(wxWebViewEvent& event);

    void AdvertiseSupportedAction();

    wxWebView* m_browser;
    long     m_zoomFactor;
    bool m_chat_page_loaded{false};

    void UpdateEmbeddedChatState(const wxString& state_key, const wxString& state_value);
    void CallEmbeddedChatMethod(const wxString& method, const wxString& params);

    void RunScriptInBrowser(const wxString& script);

private:
    int m_radis{12};
    void OnPaint(wxPaintEvent& event);
};

}} // namespace Slic3r::GUI

#endif /* slic3r_Tab_hpp_ */
