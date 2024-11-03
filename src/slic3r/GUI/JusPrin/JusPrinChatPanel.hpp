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

namespace Slic3r { namespace GUI {

class JusPrinChatPanel : public wxPanel
{
public:
    JusPrinChatPanel(wxWindow* parent);
    virtual ~JusPrinChatPanel();
    void reload();
    void UpdateOAuthAccessToken();

private:
    void load_url();
    void OnClose(wxCloseEvent& evt);
    void OnError(wxWebViewEvent& evt);
    void OnLoaded(wxWebViewEvent& evt);
    void update_mode();

    using MemberFunctionPtr = void (JusPrinChatPanel::*)(const nlohmann::json&);
    std::map<std::string, MemberFunctionPtr> action_handlers;

    void init_action_handlers();

    void handle_switch_to_classic_mode(const nlohmann::json& params);
    void handle_show_login(const nlohmann::json& params);
    void handle_update_presets(const nlohmann::json& params);
    void handle_select_preset(const nlohmann::json& params);
    void handle_add_printers(const nlohmann::json& params);
    void handle_add_filaments(const nlohmann::json& params);
    void start_slice_all(const nlohmann::json& params);

private:
    void SendMessage(wxString message);
    void OnActionCallReceived(wxWebViewEvent& event);
    nlohmann::json GetPresetsJson(Preset::Type type);
    nlohmann::json GetPlaterStateJson();
    void UpdatePresets();
    void UpdatePlaterState();

    void ConfigProperty(Preset::Type preset_type, const nlohmann::json& jsonObject);
    void FetchProperty(Preset::Type preset_type);
    void FetchPresetBundle();
    void FetchFilaments();
    void FetchUsedFilamentIds();

    wxWebView* m_browser;
    long     m_zoomFactor;
    wxString m_apikey; // todo

};

}} // namespace Slic3r::GUI

#endif /* slic3r_Tab_hpp_ */
