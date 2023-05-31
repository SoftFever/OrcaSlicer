#ifndef slic3r_PrinterWebView_hpp_
#define slic3r_PrinterWebView_hpp_


#include "wx/artprov.h"
#include "wx/cmdline.h"
#include "wx/notifmsg.h"
#include "wx/settings.h"
#include "wx/webview.h"

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
#include <wx/panel.h>
#include <wx/tbarbase.h>
#include "wx/textctrl.h"
#include <wx/timer.h>


namespace Slic3r {
namespace GUI {


class PrinterWebView : public wxPanel {
public:
    PrinterWebView(wxWindow *parent);
    virtual ~PrinterWebView();

    void load_url(wxString& url);
    void UpdateState();
    void OnClose(wxCloseEvent& evt);
    void OnError(wxWebViewEvent& evt);

private:

    wxWebView* m_browser;
    long m_zoomFactor;

    // DECLARE_EVENT_TABLE()
};

} // GUI
} // Slic3r

#endif /* slic3r_Tab_hpp_ */
