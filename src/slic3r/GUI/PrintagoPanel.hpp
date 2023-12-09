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

class PrintagoPanel : public wxPanel
{
public:
    PrintagoPanel(wxWindow *parent, wxString* url);
    virtual ~PrintagoPanel();
    void OnProcessCompleted(SlicingProcessCompletedEvent &event);
    void load_url(wxString& url);
    bool m_can_process_job = true; //let's us know if we can clear/add files/slice/send.

    void UpdateState();
    void OnIdle(wxIdleEvent& evt);
    void OnUrl(wxCommandEvent& evt);
    void OnBack(wxCommandEvent& evt);
    void OnForward(wxCommandEvent& evt);
    void OnStop(wxCommandEvent& evt);
    void OnReload(wxCommandEvent& evt);
    void OnNavigationRequest(wxWebViewEvent& evt);
    void OnNavigationComplete(wxWebViewEvent& evt);
    void OnDocumentLoaded(wxWebViewEvent& evt);
    void OnTitleChanged(wxWebViewEvent &evt);
    void OnNewWindow(wxWebViewEvent& evt);
    void OnScriptMessage(wxWebViewEvent& evt);
    void OnScriptResponseMessage(wxCommandEvent& evt);
    void OnViewSourceRequest(wxCommandEvent& evt);
    void OnViewTextRequest(wxCommandEvent& evt);
    void OnToolsClicked(wxCommandEvent& evt);
    void OnError(wxWebViewEvent& evt);
    void OnCut(wxCommandEvent& evt);
    void OnCopy(wxCommandEvent& evt);
    void OnPaste(wxCommandEvent& evt);
    void OnUndo(wxCommandEvent& evt);
    void OnRedo(wxCommandEvent& evt);
    void OnMode(wxCommandEvent& evt);
    void RunScript(const wxString& javascript);
    void OnRunScriptString(wxCommandEvent& evt);
    void OnRunScriptInteger(wxCommandEvent& evt);
    void OnRunScriptDouble(wxCommandEvent& evt);
    void OnRunScriptBool(wxCommandEvent& evt);
    void OnRunScriptObject(wxCommandEvent& evt);
    void OnRunScriptArray(wxCommandEvent& evt);
    void OnRunScriptDOM(wxCommandEvent& evt);
    void OnRunScriptUndefined(wxCommandEvent& evt);
    void OnRunScriptNull(wxCommandEvent& evt);
    void OnRunScriptDate(wxCommandEvent& evt);
    void OnRunScriptMessage(wxCommandEvent& evt);
    void OnRunScriptCustom(wxCommandEvent& evt);
    void OnAddUserScript(wxCommandEvent& evt);
    void OnSetCustomUserAgent(wxCommandEvent& evt);
    void OnClearSelection(wxCommandEvent& evt);
    void OnDeleteSelection(wxCommandEvent& evt);
    void OnSelectAll(wxCommandEvent& evt);
    void OnLoadScheme(wxCommandEvent& evt);
    void OnUseMemoryFS(wxCommandEvent& evt);
    void OnEnableContextMenu(wxCommandEvent& evt);
    void OnEnableDevTools(wxCommandEvent& evt);
    void OnClose(wxCloseEvent& evt);

    wxTimer * m_LoginUpdateTimer{nullptr};
    void OnFreshLoginStatus(wxTimerEvent &event);

    void SendRecentList(int images);
    void OpenModelDetail(std::string id, NetworkAgent *agent);
    void SendLoginInfo();
    void ShowNetpluginTip();

    void update_mode();
private:
    wxWebView* m_browser;
    wxBoxSizer *bSizer_toolbar;
    wxButton *  m_button_back;
    wxButton *  m_button_forward;
    wxButton *  m_button_stop;
    wxButton *  m_button_reload;
    wxTextCtrl *m_url;
    wxButton *  m_button_tools;

    wxMenu* m_tools_menu;
    wxMenuItem* m_tools_handle_navigation;
    wxMenuItem* m_tools_handle_new_window;
    wxMenuItem* m_edit_cut;
    wxMenuItem* m_edit_copy;
    wxMenuItem* m_edit_paste;
    wxMenuItem* m_edit_undo;
    wxMenuItem* m_edit_redo;
    wxMenuItem* m_edit_mode;
    wxMenuItem* m_scroll_line_up;
    wxMenuItem* m_scroll_line_down;
    wxMenuItem* m_scroll_page_up;
    wxMenuItem* m_scroll_page_down;
    wxMenuItem* m_script_string;
    wxMenuItem* m_script_integer;
    wxMenuItem* m_script_double;
    wxMenuItem* m_script_bool;
    wxMenuItem* m_script_object;
    wxMenuItem* m_script_array;
    wxMenuItem* m_script_dom;
    wxMenuItem* m_script_undefined;
    wxMenuItem* m_script_null;
    wxMenuItem* m_script_date;
    wxMenuItem* m_script_message;
    wxMenuItem* m_script_custom;
    wxMenuItem* m_selection_clear;
    wxMenuItem* m_selection_delete;
    wxMenuItem* m_context_menu;
    wxMenuItem* m_dev_tools;

    SelectMachineDialog* m_select_machine_dlg = nullptr;

    wxInfoBar *m_info;
    wxStaticText* m_info_text;

    long m_zoomFactor;

    // Last executed JavaScript snippet, for convenience.
    wxString m_javascript;
    wxString m_response_js;

    // Printago 

    Slic3r::DeviceManager* devManager;

    //we set this to true when we need to issue a 
    //command that must block (e.g slicing/sending a print to a printer)
    //no need to send this for commands like home/jog.
    wxString jobPrinterId;

    void HandlePrintagoCommand(const wxString& commandType, const wxString& action, 
                                wxStringToStringHashMap& parameters, const wxString& originalCommandStr);

    void SendJsonMessage(const wxString& msg_type, const wxString& printer_id, const json& data, wxWebView* webView, const wxString& command = "");
    void SendStatusMessage(const wxString& printer_id, const json& statusData, wxWebView* webView, const wxString& command = "");
    void SendResponseMessage(const wxString& printer_id, const json& responseData, wxWebView* webView, const wxString& command = "");
    void SendErrorMessage(const wxString& printer_id, const json& errorData, wxWebView* webView,const wxString& command = "");
    void SendSuccessMessage(const wxString& printer_id, const wxString localCommand, wxWebView* webView, const wxString& command = "");
    
    //wraps sending and error response, and unblocks the server for job processing.
    void SendErrorAndUnblock(const wxString& printer_id, const json& errorData, wxWebView* webView, const wxString& command);
    
    wxStringToStringHashMap ParseQueryString(const wxString& queryString);
    std::map<wxString, wxString> ExtractPrefixedParams(const wxStringToStringHashMap& params, const wxString& prefix);
    
    json GetAllStatus();
    json GetMachineStatus(const wxString &printerId);
    json GetMachineStatus(MachineObject* machine);

    wxString SavePrintagoFile(const wxString& url);
    wxString wxURLErrorToString(wxURLError error);
    static size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream);
    bool DownloadFileFromURL(const wxString& url, const wxString& localPath);

    json MachineObjectToJson(MachineObject* machine);

    void set_can_process_job(bool can_process_job);
    bool can_process_job() { return m_can_process_job; };

    DECLARE_EVENT_TABLE()
};

} // GUI
} // Slic3r

#endif /* printago_Tab_hpp_ */