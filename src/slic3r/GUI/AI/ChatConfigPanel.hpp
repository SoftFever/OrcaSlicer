#ifndef slic3r_ChatConfigPanel_hpp_
#define slic3r_ChatConfigPanel_hpp_

#include <wx/panel.h>
#include <wx/webview.h>

#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif
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

    wxWebView* m_browser;
    long       m_zoomFactor;
    wxString   m_apikey; // todo
};

}} // namespace Slic3r::GUI

#endif /* slic3r_Tab_hpp_ */
