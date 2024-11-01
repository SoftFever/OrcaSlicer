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

namespace Slic3r { namespace GUI {

class JusPrinChatPanel : public wxPanel
{
public:
    JusPrinChatPanel(wxWindow* parent);
    virtual ~JusPrinChatPanel();
    void UpdateOAuthAccessToken();

private:
    void load_url();
    void OnClose(wxCloseEvent& evt);
    void OnError(wxWebViewEvent& evt);
    void OnLoaded(wxWebViewEvent& evt);
    void reload();
    void update_mode();

private:
    void SendMessage(wxString message);
    void OnScriptMessageReceived(wxWebViewEvent& event);
    nlohmann::json GetPresets(Preset::Type type);
    void UpdatePresets();

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
