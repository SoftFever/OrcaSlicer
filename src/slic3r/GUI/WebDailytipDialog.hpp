#ifndef slic3r_WebDialytipDialog_hpp_
#define slic3r_WebDialytipDialog_hpp_

#include "wx/artprov.h"
#include "wx/cmdline.h"
#include "wx/notifmsg.h"
#include "wx/settings.h"
#include "wx/webview.h"

#if wxUSE_WEBVIEW_IE
#include "wx/msw/webview_ie.h"
#endif
#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif

#include "wx/webviewarchivehandler.h"
#include "wx/webviewfshandler.h"
#include "wx/numdlg.h"
#include "wx/infobar.h"
#include "wx/filesys.h"
#include "wx/fs_arc.h"
#include "wx/fs_mem.h"
#include "wx/stdpaths.h"
#include <wx/frame.h>
#include <wx/tbarbase.h>
#include "wx/textctrl.h"

#include "GUI_App.hpp"

namespace Slic3r { namespace GUI {

enum { SKIP_CHECKBOX };

class DailytipFrame : public wxDialog
{
public:
    DailytipFrame(GUI_App *pGUI);
    virtual ~DailytipFrame();

    void load_url(wxString &url);

    void UpdateState();
    void OnIdle(wxIdleEvent &evt);
    // void OnClose(wxCloseEvent &evt);

    void OnNavigationRequest(wxWebViewEvent &evt);
    void OnNavigationComplete(wxWebViewEvent &evt);
    void OnDocumentLoaded(wxWebViewEvent &evt);
    void OnNewWindow(wxWebViewEvent &evt);
    void OnError(wxWebViewEvent &evt);
    void OnTitleChanged(wxWebViewEvent &evt);
    void OnFullScreenChanged(wxWebViewEvent &evt);
    void OnScriptMessage(wxWebViewEvent &evt);

    void OnScriptResponseMessage(wxCommandEvent &evt);
    void RunScript(const wxString &javascript);

    bool IsTodayUsed();
    bool IsSkip();
    bool IsWhetherShow();
    void UpdateShowTime();

    void OnSkipClick(wxCommandEvent &evt);

private:
    GUI_App *m_MainPtr;

    wxWebView * m_browser;
    wxCheckBox *m_SkipBtn;

    wxString m_SectionName;

    // User Config
    bool PrivacyUse;

#if wxUSE_WEBVIEW_IE
    wxMenuItem *m_script_object_el;
    wxMenuItem *m_script_date_el;
    wxMenuItem *m_script_array_el;
#endif
    // Last executed JavaScript snippet, for convenience.
    wxString m_javascript;
    wxString m_response_js;

    // DECLARE_EVENT_TABLE()
};

}} // namespace Slic3r::GUI

#endif /* slic3r_Tab_hpp_ */
