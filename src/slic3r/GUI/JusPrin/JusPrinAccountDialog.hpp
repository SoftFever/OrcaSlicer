#ifndef slic3r_GUI_JusPrinAccountDialog_hpp_
#define slic3r_GUI_JusPrinAccountDialog_hpp_

#include <wx/dialog.h>
#include <wx/webview.h>

namespace Slic3r { namespace GUI {

class JusPrinAccountDialog : public wxDialog
{
public:
    JusPrinAccountDialog(const wxString& url);
    bool run();

private:
    wxWebView* m_browser {nullptr};
    wxString m_url;
    void OnNavigationRequest(wxWebViewEvent& evt);
    void OnNavigationComplete(wxWebViewEvent& evt);
    void OnDocumentLoaded(wxWebViewEvent& evt);
    void OnNewWindow(wxWebViewEvent& evt);
    void OnError(wxWebViewEvent& evt);
    void OnTitleChanged(wxWebViewEvent& evt);
    void OnFullScreenChanged(wxWebViewEvent& evt);

    void load_url(wxString& url);
    void UpdateState();
};

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_JusPrinAccountDialog_hpp_