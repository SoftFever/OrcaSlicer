#ifndef slic3r_Printago_hpp_
#define slic3r_Printago_hpp_

#include "nlohmann/json.hpp"

#include "slic3r/GUI/SelectMachine.hpp"
#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "wx/artprov.h"
#include "wx/cmdline.h"
#include "wx/notifmsg.h"
#include "wx/settings.h"
#include "wx/webview.h"
#include <wx/tokenzr.h>
#include <wx/event.h>

#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif

#include "wx/webviewarchivehandler.h"
#include "wx/webviewfshandler.h"
#include "wx/numdlg.h"
#include "wx/infobar.h"
#include "wx/filesys.h"
#include "wx/filename.h"
#include "wx/fs_arc.h"
#include "wx/fs_mem.h"
#include "wx/stdpaths.h"
#include <wx/panel.h>
#include <wx/tbarbase.h>
#include "wx/textctrl.h"
#include <wx/timer.h>
#include <wx/string.h>
#include <wx/hashmap.h>
#include <wx/uri.h>
#include <wx/url.h>
#include <map>
#include <curl/curl.h>

namespace Slic3r {

class NetworkAgent;

namespace GUI {

wxDECLARE_EVENT(MY_WEBVIEW_MESSAGE_EVENT, wxCommandEvent);

class PrintagoPanel : public wxPanel
{
public:
    PrintagoPanel(wxWindow *parent, wxString *url);
    virtual ~PrintagoPanel();
    void OnProcessCompleted(SlicingProcessCompletedEvent &event);
    void load_url(wxString &url);
    bool m_can_process_job = true; // let's us know if we can clear/add files/slice/send.

    void OnNavigationRequest(wxWebViewEvent &evt);
    void OnNavigationComplete(wxWebViewEvent &evt);
    void OnDocumentLoaded(wxWebViewEvent &evt);
    void OnNewWindow(wxWebViewEvent &evt);
    void OnError(wxWebViewEvent &evt);
    void RunScript(const wxString &javascript);

private:
    Slic3r::DeviceManager *devManager;
    wxWebView  *m_browser;
    wxBoxSizer *bSizer_toolbar;

    SelectMachineDialog *m_select_machine_dlg = nullptr;

    wxInfoBar    *m_info;
    wxStaticText *m_info_text;

    long m_zoomFactor;

    // we set this to true when we need to issue a
    // command that must block (e.g slicing/sending a print to a printer)
    // no need to send this for commands like home/jog.
    wxString jobPrinterId;

    void HandlePrintagoCommand(const wxString          &commandType,
                               const wxString          &action,
                               wxStringToStringHashMap &parameters,
                               const wxString          &originalCommandStr);

    void SendJsonMessage(
        const wxString &msg_type, const wxString &printer_id, const json &data, wxWebView *webView, const wxString &command = "");
    void SendStatusMessage(const wxString &printer_id, const json &statusData, wxWebView *webView, const wxString &command = "");
    void SendResponseMessage(const wxString &printer_id, const json &responseData, wxWebView *webView, const wxString &command = "");
    void SendErrorMessage(const wxString &printer_id, const json &errorData, wxWebView *webView, const wxString &command = "");
    void SendSuccessMessage(const wxString &printer_id, const wxString localCommand, wxWebView *webView, const wxString &command = "");

    // wraps sending and error response, and unblocks the server for job processing.
    void SendErrorAndUnblock(const wxString &printer_id, const json &errorData, wxWebView *webView, const wxString &command);

    wxStringToStringHashMap      ParseQueryString(const wxString &queryString);
    std::map<wxString, wxString> ExtractPrefixedParams(const wxStringToStringHashMap &params, const wxString &prefix);

    json GetAllStatus();
    json GetMachineStatus(const wxString &printerId);
    json GetMachineStatus(MachineObject *machine);

    bool            SavePrintagoFile(const wxString url, wxString &localPath);
    static wxString wxURLErrorToString(wxURLError error);

    bool DownloadFileFromURL(const wxString url, const wxFileName &localPath);

    static json MachineObjectToJson(MachineObject *machine);

    void set_can_process_job(bool can_process_job);
    bool can_process_job() const { return m_can_process_job; };

};

} // namespace GUI
} // namespace Slic3r

#endif /* printago_Tab_hpp_ */