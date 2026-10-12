#ifndef slic3r_GUI_WebView_hpp_
#define slic3r_GUI_WebView_hpp_

#include <wx/string.h>
#include <wx/setup.h>
#include <wx/webview.h>
#include <wx/event.h>

class wxWebView;
class wxWindow;

wxDECLARE_EVENT(EVT_WEBVIEW_RECREATED, wxCommandEvent);

class WebView
{
public:
    // `profile` empty -> the shared default backend store used by all trusted UI.
    // `profile` non-empty -> a private store keyed by `profile`, so this view shares no
    // cookies/storage/session with the shared store or any other profile. Used for
    // plugin-supplied (untrusted) content.
    static wxWebView *CreateWebView(wxWindow *parent, wxString const &url, wxString const &profile = wxEmptyString);
#if wxUSE_WEBVIEW_EDGE
    static bool CheckWebViewRuntime();
    static bool DownloadAndInstallWebViewRuntime();
#endif
    static void LoadUrl(wxWebView * webView, wxString const &url);

    static bool RunScript(wxWebView * webView, wxString const & msg);

    // Marks "wx" as registered so CreateWebView's deferred add skips the duplicate.
    static void MarkScriptMessageHandlerAdded(wxWebView * webView);

    // On Windows, a WebView2 backend created during a GUI rebuild (language switch) can come up
    // ignoring every navigation. A panel that gets true here recreates its view on first Show().
    static bool NeedsRecreateOnShow();

    static void RecreateAll();
};

#endif // !slic3r_GUI_WebView_hpp_
