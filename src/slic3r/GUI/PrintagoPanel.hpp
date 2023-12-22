#ifndef slic3r_Printago_hpp_
#define slic3r_Printago_hpp_

#include "nlohmann/json.hpp"

#include <wx/event.h>
#include <wx/tokenzr.h>
#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "slic3r/GUI/SelectMachine.hpp"
#include "wx/webview.h"

#if wxUSE_WEBVIEW_EDGE
#include "wx/msw/webview_edge.h"
#endif

#include <map>
#include <curl/curl.h>
#include <wx/hashmap.h>
#include <wx/panel.h>
#include <wx/string.h>
#include <wx/url.h>
#include "wx/filename.h"
#include "wx/infobar.h"

namespace Slic3r {

class NetworkAgent;

namespace GUI {

class PrintagoMessageEvent; // forward declaration

wxDECLARE_EVENT(PRINTAGO_SEND_WEBVIEW_MESSAGE_EVENT, PrintagoMessageEvent);
wxDECLARE_EVENT(PRINTAGO_SLICING_PROCESS_COMPLETED_EVENT, SlicingProcessCompletedEvent);
wxDECLARE_EVENT(PRINTAGO_PRINT_SENT_EVENT, wxCommandEvent);

class PrintagoCommand
{
public:
    PrintagoCommand() = default;

    PrintagoCommand(const PrintagoCommand &event)
    {
        this->m_command_type         = event.m_command_type;
        this->m_action               = event.m_action;
        this->m_parameters           = event.m_parameters;
        this->m_original_command_str = event.m_original_command_str;
    }

    virtual ~PrintagoCommand() {}

    void SetCommandType(const wxString &command) { this->m_command_type = command; }
    void SetAction(const wxString &action) { this->m_action = action; }
    void SetParameters(const wxStringToStringHashMap &parameters) { this->m_parameters = parameters; }
    void SetOriginalCommandStr(const wxString &originalCommandStr) { this->m_original_command_str = originalCommandStr; }

    wxString                GetCommandType() const { return this->m_command_type; }
    wxString                GetAction() const { return this->m_action; }
    wxStringToStringHashMap GetParameters() const { return this->m_parameters; }
    wxString                GetOriginalCommandStr() const { return this->m_original_command_str; }

private:
    wxString                m_command_type;
    wxString                m_action;
    wxStringToStringHashMap m_parameters;
    wxString                m_original_command_str;
};

class PrintagoMessageEvent : public wxEvent
{
public:
    PrintagoMessageEvent(wxEventType eventType = wxEVT_NULL) : wxEvent(0, eventType) {}

    PrintagoMessageEvent(const PrintagoMessageEvent &event)
    {
        this->m_message_type = event.m_message_type;
        this->m_printer_id   = event.m_printer_id;
        this->m_command      = event.m_command;
        this->m_data         = event.m_data;
    }

    virtual ~PrintagoMessageEvent() {}

    wxEvent *Clone() const override { return new PrintagoMessageEvent(*this); }

    void SetMessageType(const wxString &message) { this->m_message_type = message; }
    void SetPrinterId(const wxString &printer_id) { this->m_printer_id = printer_id; }
    void SetCommand(const wxString &command) { this->m_command = command; }
    void SetData(const json &data) { this->m_data = data; }

    wxString GetMessageType() const { return this->m_message_type; }
    wxString GetPrinterId() const { return this->m_printer_id; }
    wxString GetCommand() const { return this->m_command; }
    json     GetData() const { return this->m_data; }

private:
    wxString m_message_type;
    wxString m_printer_id;
    wxString m_command;
    json     m_data;
};

class PrintagoPanel : public wxPanel
{
public:
    PrintagoPanel(wxWindow *parent, wxString *url);
    virtual ~PrintagoPanel();

    void load_url(wxString &url);
    bool CanProcessJob() { return m_can_process_job; }

private:
    Slic3r::DeviceManager *devManager;
    wxWebView             *m_browser;
    wxBoxSizer            *bSizer_toolbar;

    // we set this to true when we need to issue a
    // command that must block (e.g slicing/sending a print to a printer)
    // no need to send this for commands like home/jog.
    wxString jobPrinterId;
    wxString jobCommand;
    wxString jobLocalFilePath;
    wxString jobServerState = "idle"; // TODO : use enum
    int      jobProgress    = 0;

    SelectMachineDialog *m_select_machine_dlg = nullptr;
    inline static bool   m_can_process_job    = true; // let's us know if we can clear/add files/slice/send.

    void HandlePrintagoCommand(const PrintagoCommand &event);

    void SendStatusMessage   (const wxString printer_id, const json     statusData,   const wxString command = "");
    void SendResponseMessage (const wxString printer_id, const json     responseData, const wxString command = "");
    void SendSuccessMessage  (const wxString printer_id, const wxString localCommand, const wxString command = "", const wxString localCommandDetail = "");
    void SendErrorMessage    (const wxString printer_id, const wxString localCommand, const wxString command = "", const wxString errorDetail = "");
    void SendJsonErrorMessage(const wxString printer_id, const wxString localCommand, const wxString command = "", const json     errorDetail = "");

    // wraps SendErrorMessage and SetCanProcessJob(true)
    void SendErrorAndUnblock(const wxString printer_id, const wxString localCommand, const wxString command, const wxString errorDetail);

    wxStringToStringHashMap      ParseQueryString(const wxString &queryString);
    std::map<wxString, wxString> ExtractPrefixedParams(const wxStringToStringHashMap &params, const wxString &prefix);

    bool ValidatePrintagoCommand(const PrintagoCommand &event);

    json GetAllStatus();
    void AddCurrentProcessJsonTo(json &statusObject);
    json GetMachineStatus(const wxString &printerId);
    json GetMachineStatus(MachineObject *machine);

    bool SavePrintagoPrintFile(const wxString url, wxString &localPath);
    json export_all_configs_to_json();
    bool DownloadFileFromURL(const wxString url, const wxFileName &localPath);

    static wxString wxURLErrorToString(wxURLError error);
    static json     MachineObjectToJson(MachineObject *machine);

    void OnNavigationRequest(wxWebViewEvent &evt);
    void OnNavigationComplete(wxWebViewEvent &evt);
    void OnNewWindow(wxWebViewEvent &evt);
    void OnError(wxWebViewEvent &evt);
    void RunScript(const wxString &javascript);

    void OnSlicingProcessCompleted(SlicingProcessCompletedEvent &evt);
    void OnPrintJobSent(wxCommandEvent &evt);
    void SendWebViewMessage(PrintagoMessageEvent &evt);

    void SetCanProcessJob(bool can_process_job);
};

} // namespace GUI
} // namespace Slic3r

#endif /* printago_Tab_hpp_ */