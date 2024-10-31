#ifndef slic3r_ChatConfigPanel_hpp_
#define slic3r_ChatConfigPanel_hpp_

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

class ChatConfigPanel : public wxPanel
{
public:
    ChatConfigPanel(wxWindow* parent);
    virtual ~ChatConfigPanel();

private:
    void load_url();
    void UpdateState();
    void OnClose(wxCloseEvent& evt);
    void OnError(wxWebViewEvent& evt);
    void OnLoaded(wxWebViewEvent& evt);
    void reload();
    void update_mode();

private:
    void SendMessage(wxString  message);
    void OnScriptMessageReceived(wxWebViewEvent& event);

    void ConfigProperty(Preset::Type preset_type, const nlohmann::json& jsonObject);
    void FetchProperty(Preset::Type preset_type, const std::string& type);
    void FetchPresetBundle();
    void FetchFilaments();

    wxWebView* m_browser;
    long       m_zoomFactor;
    wxString   m_apikey; // todo
};

}} // namespace Slic3r::GUI

#endif /* slic3r_Tab_hpp_ */
