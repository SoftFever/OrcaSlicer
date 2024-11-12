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

    bool run();

private:
    wxWebView* m_browser {nullptr};
    wxString m_jusprint_url;
    bool m_networkOk {false};
    wxString m_javascript;
    wxString m_response_js;
    wxString m_user_agent;
    void OnNavigationRequest(wxWebViewEvent& evt);
    void OnNavigationComplete(wxWebViewEvent& evt);
    void OnDocumentLoaded(wxWebViewEvent& evt);
    void OnNewWindow(wxWebViewEvent& evt);
    void OnError(wxWebViewEvent& evt);
    void OnTitleChanged(wxWebViewEvent& evt);
    void OnFullScreenChanged(wxWebViewEvent& evt);
    void OnScriptMessage(wxWebViewEvent& evt);

    void load_url(wxString& url);
    void UpdateState();
    bool ShowErrorPage();
    void RunScript(const wxString& javascript);

};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_JusPrinLoginDialog_hpp_
