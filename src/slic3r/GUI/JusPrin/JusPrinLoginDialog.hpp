#ifndef slic3r_GUI_JusPrinLoginDialog_hpp_
#define slic3r_GUI_JusPrinLoginDialog_hpp_

#include <wx/dialog.h>
#include <wx/timer.h>
#include <wx/webview.h>

namespace Slic3r { namespace GUI {

class JusPrinLoginDialog : public wxDialog
{
public:
    JusPrinLoginDialog();
    virtual ~JusPrinLoginDialog();

    bool run();

private:
    wxWebView* m_browser {nullptr};
    wxTimer* m_timer {nullptr};
    wxString m_jusprint_url;
    bool m_networkOk {false};
    wxString m_javascript;
    wxString m_response_js;
    wxString m_user_agent;
    wxString m_oauth_token;
    void OnNavigationRequest(wxWebViewEvent& evt);
    void OnNavigationComplete(wxWebViewEvent& evt);
    void OnDocumentLoaded(wxWebViewEvent& evt);
    void OnNewWindow(wxWebViewEvent& evt);
    void OnError(wxWebViewEvent& evt);
    void OnTitleChanged(wxWebViewEvent& evt);
    void OnFullScreenChanged(wxWebViewEvent& evt);
    void OnScriptMessage(wxWebViewEvent& evt);
    void OnTimer(wxTimerEvent& event);

    void load_url(wxString& url);
    void UpdateState();
    bool ShowErrorPage();
    void RunScript(const wxString& javascript);

    DECLARE_EVENT_TABLE()
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_JusPrinLoginDialog_hpp_
