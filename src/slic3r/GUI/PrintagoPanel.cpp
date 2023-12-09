#include "WebViewDialog.hpp"

#include "I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "libslic3r_version.h"
#include "../Utils/Http.hpp"
#include "nlohmann/json.hpp"

#include <wx/sizer.h>
#include <wx/toolbar.h>
#include <wx/textdlg.h>
#include <wx/datetime.h>
#include <wx/url.h>
#include <slic3r/GUI/Widgets/WebView.hpp>

#include <wx/wx.h>
#include <wx/sstream.h>
#include <wx/wfstream.h>
#include <wx/stdpaths.h>

using namespace nlohmann;

namespace pt = boost::property_tree;
    
namespace Slic3r {
namespace GUI {
    wxDECLARE_EVENT(EVT_PRINTAGO_RESPONSE_MESSAGE, wxCommandEvent);
    wxDEFINE_EVENT(EVT_PRINTAGO_RESPONSE_MESSAGE, wxCommandEvent);
    wxDEFINE_EVENT(EVT_PRINTAGO_PRINT, wxCommandEvent);

    #define PRINTAGO_LOGIN_INFO_UPDATE_TIMER_ID 17653
    #define PRINTAGO_TEMP_THRESHOLD_ALLOW_E_CTRL 170.0f  // Minimum temperature to allow extrusion control (per StatusPanel.cpp)

    BEGIN_EVENT_TABLE(PrintagoPanel, wxPanel)
    EVT_TIMER(PRINTAGO_LOGIN_INFO_UPDATE_TIMER_ID, PrintagoPanel::OnFreshLoginStatus)
    END_EVENT_TABLE()

PrintagoPanel::PrintagoPanel(wxWindow *parent, wxString* url)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
 {
    devManager = Slic3r::GUI::wxGetApp().getDeviceManager();
    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);
    
    // printagoPlater = new Plater(this, wxGetApp().mainframe);
    // printagoPlater->Hide();
    
#if !PRINTAGO_RELEASE
    // Create the button
    bSizer_toolbar = new wxBoxSizer(wxHORIZONTAL);

    m_button_back = new wxButton(this, wxID_ANY, wxT("Back"), wxDefaultPosition, wxDefaultSize, 0);
    m_button_back->Enable(false);
    bSizer_toolbar->Add(m_button_back, 0, wxALL, 5);

    m_button_forward = new wxButton(this, wxID_ANY, wxT("Forward"), wxDefaultPosition, wxDefaultSize, 0);
    m_button_forward->Enable(false);
    bSizer_toolbar->Add(m_button_forward, 0, wxALL, 5);

    m_button_stop = new wxButton(this, wxID_ANY, wxT("Stop"), wxDefaultPosition, wxDefaultSize, 0);

    bSizer_toolbar->Add(m_button_stop, 0, wxALL, 5);

    m_button_reload = new wxButton(this, wxID_ANY, wxT("Reload"), wxDefaultPosition, wxDefaultSize, 0);
    bSizer_toolbar->Add(m_button_reload, 0, wxALL, 5);

    m_url = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    bSizer_toolbar->Add(m_url, 1, wxALL | wxEXPAND, 5);

    m_button_tools = new wxButton(this, wxID_ANY, wxT("Tools"), wxDefaultPosition, wxDefaultSize, 0);
    bSizer_toolbar->Add(m_button_tools, 0, wxALL, 5);

    topsizer->Add(bSizer_toolbar, 0, wxEXPAND, 0);
    bSizer_toolbar->Show(false);

    // Create panel for find toolbar.
    wxPanel* panel = new wxPanel(this);
    topsizer->Add(panel, wxSizerFlags().Expand());

    // Create sizer for panel.
    wxBoxSizer* panel_sizer = new wxBoxSizer(wxVERTICAL);
    panel->SetSizer(panel_sizer);
#endif //PRINTAGO_RELEASE

    // Create the info panel
    m_info = new wxInfoBar(this);
    topsizer->Add(m_info, wxSizerFlags().Expand());
    // Create the webview
    m_browser = WebView::CreateWebView(this, *url);
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }
    m_browser->Hide();
    SetSizer(topsizer);

    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    // Log backend information
    if (wxGetApp().get_mode() == comDevelop) {
        wxLogMessage(wxWebView::GetBackendVersionInfo().ToString());
        wxLogMessage("Backend: %s Version: %s", m_browser->GetClassInfo()->GetClassName(),
            wxWebView::GetBackendVersionInfo().ToString());
        wxLogMessage("User Agent: %s", m_browser->GetUserAgent());
    }
    
    // Create the Tools menu
    m_tools_menu = new wxMenu();
    wxMenuItem* viewSource = m_tools_menu->Append(wxID_ANY, _L("View Source"));
    wxMenuItem* viewText = m_tools_menu->Append(wxID_ANY, _L("View Text"));
    m_tools_menu->AppendSeparator();
    m_tools_handle_navigation = m_tools_menu->AppendCheckItem(wxID_ANY, _L("Handle Navigation"));
    m_tools_handle_new_window = m_tools_menu->AppendCheckItem(wxID_ANY, _L("Handle New Windows"));
    m_tools_menu->AppendSeparator();

    //Create an editing menu
    wxMenu* editmenu = new wxMenu();
    m_edit_cut = editmenu->Append(wxID_ANY, _L("Cut"));
    m_edit_copy = editmenu->Append(wxID_ANY, _L("Copy"));
    m_edit_paste = editmenu->Append(wxID_ANY, _L("Paste"));
    editmenu->AppendSeparator();
    m_edit_undo = editmenu->Append(wxID_ANY, _L("Undo"));
    m_edit_redo = editmenu->Append(wxID_ANY, _L("Redo"));
    editmenu->AppendSeparator();
    m_edit_mode = editmenu->AppendCheckItem(wxID_ANY, _L("Edit Mode"));
    m_tools_menu->AppendSubMenu(editmenu, "Edit");

    wxMenu* script_menu = new wxMenu;
    m_script_string = script_menu->Append(wxID_ANY, "Return String");
    m_script_integer = script_menu->Append(wxID_ANY, "Return integer");
    m_script_double = script_menu->Append(wxID_ANY, "Return double");
    m_script_bool = script_menu->Append(wxID_ANY, "Return bool");
    m_script_object = script_menu->Append(wxID_ANY, "Return JSON object");
    m_script_array = script_menu->Append(wxID_ANY, "Return array");
    m_script_dom = script_menu->Append(wxID_ANY, "Modify DOM");
    m_script_undefined = script_menu->Append(wxID_ANY, "Return undefined");
    m_script_null = script_menu->Append(wxID_ANY, "Return null");
    m_script_date = script_menu->Append(wxID_ANY, "Return Date");
    m_script_message = script_menu->Append(wxID_ANY, "Send script message");
    m_script_custom = script_menu->Append(wxID_ANY, "Custom script");
    m_tools_menu->AppendSubMenu(script_menu, _L("Run Script"));
    wxMenuItem* addUserScript = m_tools_menu->Append(wxID_ANY, _L("Add user script"));
    wxMenuItem* setCustomUserAgent = m_tools_menu->Append(wxID_ANY, _L("Set custom user agent"));

    //Selection menu
    wxMenu* selection = new wxMenu();
    m_selection_clear = selection->Append(wxID_ANY, _L("Clear Selection"));
    m_selection_delete = selection->Append(wxID_ANY, _L("Delete Selection"));
    wxMenuItem* selectall = selection->Append(wxID_ANY, _L("Select All"));

    editmenu->AppendSubMenu(selection, "Selection");

    wxMenuItem* loadscheme = m_tools_menu->Append(wxID_ANY, _L("Custom Scheme Example"));
    wxMenuItem* usememoryfs = m_tools_menu->Append(wxID_ANY, _L("Memory File System Example"));

    m_context_menu = m_tools_menu->AppendCheckItem(wxID_ANY, _L("Enable Context Menu"));
    m_dev_tools = m_tools_menu->AppendCheckItem(wxID_ANY, _L("Enable Dev Tools"));

    //By default we want to handle navigation and new windows
    m_tools_handle_navigation->Check();
    m_tools_handle_new_window->Check();

    //Zoom
    m_zoomFactor = 100;

    // Connect the button events
#if !PRINTAGO_RELEASE
    Bind(wxEVT_BUTTON, &PrintagoPanel::OnBack, this, m_button_back->GetId());
    Bind(wxEVT_BUTTON, &PrintagoPanel::OnForward, this, m_button_forward->GetId());
    Bind(wxEVT_BUTTON, &PrintagoPanel::OnStop, this, m_button_stop->GetId());
    Bind(wxEVT_BUTTON, &PrintagoPanel::OnReload, this, m_button_reload->GetId());
    Bind(wxEVT_BUTTON, &PrintagoPanel::OnToolsClicked, this, m_button_tools->GetId());
    Bind(wxEVT_TEXT_ENTER, &PrintagoPanel::OnUrl, this, m_url->GetId());

#endif //PRINTAGO_RELEASE

    // Connect the webview events
    Bind(wxEVT_WEBVIEW_NAVIGATING, &PrintagoPanel::OnNavigationRequest, this);
    Bind(wxEVT_WEBVIEW_NAVIGATED, &PrintagoPanel::OnNavigationComplete, this);
    Bind(wxEVT_WEBVIEW_LOADED, &PrintagoPanel::OnDocumentLoaded, this);
    Bind(wxEVT_WEBVIEW_TITLE_CHANGED, &PrintagoPanel::OnTitleChanged, this);
    Bind(wxEVT_WEBVIEW_ERROR, &PrintagoPanel::OnError, this);
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &PrintagoPanel::OnNewWindow, this);
    Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &PrintagoPanel::OnScriptMessage, this);
    Bind(EVT_PRINTAGO_RESPONSE_MESSAGE, &PrintagoPanel::OnScriptResponseMessage, this);

    // Connect the menu events
    Bind(wxEVT_MENU, &PrintagoPanel::OnViewSourceRequest, this, viewSource->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnViewTextRequest, this, viewText->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnCut, this, m_edit_cut->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnCopy, this, m_edit_copy->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnPaste, this, m_edit_paste->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnUndo, this, m_edit_undo->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnRedo, this, m_edit_redo->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnMode, this, m_edit_mode->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnRunScriptString, this, m_script_string->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnRunScriptInteger, this, m_script_integer->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnRunScriptDouble, this, m_script_double->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnRunScriptBool, this, m_script_bool->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnRunScriptObject, this, m_script_object->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnRunScriptArray, this, m_script_array->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnRunScriptDOM, this, m_script_dom->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnRunScriptUndefined, this, m_script_undefined->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnRunScriptNull, this, m_script_null->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnRunScriptDate, this, m_script_date->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnRunScriptMessage, this, m_script_message->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnRunScriptCustom, this, m_script_custom->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnAddUserScript, this, addUserScript->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnSetCustomUserAgent, this, setCustomUserAgent->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnClearSelection, this, m_selection_clear->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnDeleteSelection, this, m_selection_delete->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnSelectAll, this, selectall->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnLoadScheme, this, loadscheme->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnUseMemoryFS, this, usememoryfs->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnEnableContextMenu, this, m_context_menu->GetId());
    Bind(wxEVT_MENU, &PrintagoPanel::OnEnableDevTools, this, m_dev_tools->GetId());

    //Connect the idle events
    Bind(wxEVT_IDLE, &PrintagoPanel::OnIdle, this);
    Bind(wxEVT_CLOSE_WINDOW, &PrintagoPanel::OnClose, this);

    m_LoginUpdateTimer = nullptr;
 }

PrintagoPanel::~PrintagoPanel()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Start";
    SetEvtHandlerEnabled(false);
    
    delete m_tools_menu;

    if (m_LoginUpdateTimer != nullptr) {
        m_LoginUpdateTimer->Stop();
        delete m_LoginUpdateTimer;
        m_LoginUpdateTimer = NULL;
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " End";
}

void PrintagoPanel::load_url(wxString& url)
{
    this->Show();
    this->Raise();
    m_url->SetLabelText(url);

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage(m_url->GetValue());
    m_browser->LoadURL(url);
    m_browser->SetFocus();
    UpdateState();
}

/**
    * Method that retrieves the current state from the web control and updates the GUI
    * the reflect this current state.
    */
void PrintagoPanel::UpdateState() {}
void PrintagoPanel::OnIdle(wxIdleEvent& WXUNUSED(evt)) {}

/**
x    */
void PrintagoPanel::OnUrl(wxCommandEvent& WXUNUSED(evt))
{
    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage(m_url->GetValue());
    m_browser->LoadURL(m_url->GetValue());
    m_browser->SetFocus();
    UpdateState();
}

/**
    * Callback invoked when user pressed the "back" button
    */
void PrintagoPanel::OnBack(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->GoBack();
    UpdateState();
}

/**
    * Callback invoked when user pressed the "forward" button
    */
void PrintagoPanel::OnForward(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->GoForward();
    UpdateState();
}

/**
    * Callback invoked when user pressed the "stop" button
    */
void PrintagoPanel::OnStop(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->Stop();
    UpdateState();
}

/**
    * Callback invoked when user pressed the "reload" button
    */
void PrintagoPanel::OnReload(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->Reload();
    UpdateState();
}

void PrintagoPanel::OnCut(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->Cut();
}

void PrintagoPanel::OnCopy(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->Copy();
}

void PrintagoPanel::OnPaste(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->Paste();
}

void PrintagoPanel::OnUndo(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->Undo();
}

void PrintagoPanel::OnRedo(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->Redo();
}

void PrintagoPanel::OnMode(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->SetEditable(m_edit_mode->IsChecked());
}

void PrintagoPanel::OnClose(wxCloseEvent& evt)
{
    this->Hide();
}

void PrintagoPanel::OnLoadScheme(wxCommandEvent& WXUNUSED(evt)) {}
void PrintagoPanel::OnUseMemoryFS(wxCommandEvent& WXUNUSED(evt)) {}
void PrintagoPanel::OnEnableContextMenu(wxCommandEvent& evt) {}
void PrintagoPanel::OnEnableDevTools(wxCommandEvent& evt) {}
void PrintagoPanel::OnFreshLoginStatus(wxTimerEvent &event) {}
void PrintagoPanel::SendRecentList(int images) {}
void PrintagoPanel::OpenModelDetail(std::string id, NetworkAgent *agent) {}
void PrintagoPanel::SendLoginInfo() {}
void PrintagoPanel::ShowNetpluginTip() {}
void PrintagoPanel::update_mode() {}

void PrintagoPanel::set_can_process_job(bool can_process_job) {
    if (can_process_job) 
        jobPrinterId = "";
    m_can_process_job = can_process_job;
}

json PrintagoPanel::MachineObjectToJson(MachineObject* machine) {
    json j;
    if (machine) {
        j["hardware"]["dev_model"] = machine->printer_type;
        j["hardware"]["dev_display_name"] = machine->get_printer_type_display_str().ToStdString();
        j["hardware"]["dev_name"] = machine->dev_name;
        j["hardware"]["nozzle_diameter"] = machine->nozzle_diameter;

        j["connection_info"]["dev_ip"] = machine->dev_ip;
        j["connection_info"]["dev_id"] = machine->dev_id;
        j["connection_info"]["dev_name"] = machine->dev_name;
        j["connection_info"]["dev_connection_type"] = machine->dev_connection_type;
        j["connection_info"]["is_local"] = machine->is_local();
        j["connection_info"]["is_connected"] = machine->is_connected();
        j["connection_info"]["is_connecting"] = machine->is_connecting();
        j["connection_info"]["is_online"] = machine->is_online();
        j["connection_info"]["has_access_right"] = machine->has_access_right();
        j["connection_info"]["ftp_folder"] = machine->get_ftp_folder();
        j["connection_info"]["access_code"] = machine->get_access_code();

        // MachineObject State Info
        j["state"]["can_print"] = machine->can_print();
        j["state"]["can_resume"] = machine->can_resume();
        j["state"]["can_pause"] = machine->can_pause();  
        j["state"]["can_abort"] = machine->can_abort();
        j["state"]["is_in_printing"] = machine->is_in_printing();
        j["state"]["is_in_prepare"] = machine->is_in_prepare();
        j["state"]["is_printing_finished"] = machine->is_printing_finished();
        j["state"]["is_in_printing_status"] = machine->is_in_printing_status(machine->print_status);
        j["state"]["is_in_extrusion_cali"] = machine->is_in_extrusion_cali();
        j["state"]["is_extrusion_cali_finished"] = machine->is_extrusion_cali_finished();

        //Current Job/Print Info
        j["current"]["print_status"] = machine->print_status;
        j["current"]["m_gcode_file"] = machine->m_gcode_file;
        j["current"]["print_time_left"] = machine->mc_left_time;
        j["current"]["print_percent"] = machine->mc_print_percent;
        j["current"]["print_stage"] = machine->mc_print_stage;
        j["current"]["print_sub_stage"] = machine->mc_print_sub_stage;
        j["current"]["curr_layer"] = machine->curr_layer;
        j["current"]["total_layers"] = machine->total_layers;  

        //Temperatures
        j["current"]["temperatures"]["nozzle_temp"] = machine->nozzle_temp;
        j["current"]["temperatures"]["nozzle_temp_target"] = machine->nozzle_temp_target;
        j["current"]["temperatures"]["bed_temp"] = machine->bed_temp;
        j["current"]["temperatures"]["bed_temp_target"] = machine->bed_temp_target;
        j["current"]["temperatures"]["chamber_temp"] = machine->chamber_temp;
        j["current"]["temperatures"]["chamber_temp_target"] = machine->chamber_temp_target;
        j["current"]["temperatures"]["frame_temp"] = machine->frame_temp;

        //Cooling
        j["current"]["cooling"]["heatbreak_fan_speed"] = machine->heatbreak_fan_speed;
        j["current"]["cooling"]["cooling_fan_speed"] = machine->cooling_fan_speed;
        j["current"]["cooling"]["big_fan1_speed"] = machine->big_fan1_speed;
        j["current"]["cooling"]["big_fan2_speed"] = machine->big_fan2_speed;
        j["current"]["cooling"]["fan_gear"] = machine->fan_gear;
    }
    return j;
}

json PrintagoPanel::GetMachineStatus(const wxString &printerId) {
    json statusObject = json::object();
    json machineList = json::array();

    if (!devManager) return json::object();

    statusObject["can_process_job"] = can_process_job();
    statusObject["current_job_id"] = "";//add later from command.
    statusObject["current_job_machine"] = jobPrinterId.ToStdString();

    machineList.push_back(MachineObjectToJson(devManager->get_my_machine(printerId.ToStdString())));
    statusObject["machines"] = machineList;
    return statusObject;
}

json PrintagoPanel::GetAllStatus() {
    std::map<std::string, MachineObject*> machineMap;
    json statusObject = json::object();
    json machineList = json::array();

    if (!devManager) return json::object();
    
    statusObject["can_process_job"] = can_process_job();
    statusObject["current_job_id"] = "";//add later from command.
    statusObject["current_job_machine"] = jobPrinterId.ToStdString();

    machineMap = devManager->get_my_machine_list();
    for (auto& pair : machineMap) {
        machineList.push_back(MachineObjectToJson(pair.second));
    }
    statusObject["machines"] = machineList;
    return statusObject;
}

size_t PrintagoPanel::write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    return fwrite(ptr, size, nmemb, stream);
}

bool PrintagoPanel::DownloadFileFromURL(const wxString& url, const wxString& localPath) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        wxLogError("Curl initialization failed");
        SendErrorMessage("", {{"error", "Curl initialization failed"}}, m_browser);
        return false;
    }

    FILE *fp = fopen(localPath.c_str(), "wb");
    if (!fp) {
        wxLogError("Failed to open file for writing");
        SendErrorMessage("", {{"error", "Failed to open file for writing"}}, m_browser);
        curl_easy_cleanup(curl);
        return false;
    }

    std::string urlStr = url.ToStdString();
    curl_easy_setopt(curl, CURLOPT_URL, urlStr.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);

    CURLcode res = curl_easy_perform(curl);
    double downloadedBytes = 0;
    curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &downloadedBytes);
    fclose(fp);

    if (res != CURLE_OK) {
        std::string errMsg = "Curl download failed: " + std::string(curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        return false;
    }

    curl_easy_cleanup(curl);
    return true;
}

wxString PrintagoPanel::SavePrintagoFile(const wxString& url) {
    wxURI uri(url);
    wxString path = uri.GetPath();

    wxArrayString pathComponents = wxStringTokenize(path, "/");
    wxString filename;

    if (!pathComponents.IsEmpty()) {
        filename = pathComponents.Last();
    } else {
        wxLogError("URL path is empty, cannot extract filename");
        return "URL path is empty, cannot extract filename";
    }
    // Remove any query string from the filename
    size_t queryPos = filename.find('?');
    if (queryPos != wxString::npos) {
        filename = filename.substr(0, queryPos);
    }
    // Construct the full path for the temporary file
    wxString tempDir = wxFileName::GetTempDir();
    wxString tempFile = tempDir + "/" + filename;
    
    if (DownloadFileFromURL(url, tempFile)) {
        wxLogMessage("File downloaded to: %s", tempFile);
        return tempFile;
    } else {
        wxLogError("Download failed");
        return "Download Failed";
    }
}

wxString PrintagoPanel::wxURLErrorToString(wxURLError error) {
    switch (error) {
        case wxURL_NOERR:
            return wxString("No Error");
        case wxURL_SNTXERR:
            return wxString("Syntax Error");
        case wxURL_NOPROTO:
            return wxString("No Protocol");
        case wxURL_NOHOST:
            return wxString("No Host");
        case wxURL_NOPATH:
            return wxString("No Path");
        case wxURL_CONNERR:
            return wxString("Connection Error");
        case wxURL_PROTOERR:
            return wxString("Protocol Error");
        default:
            return wxString("Unknown Error");
    }
}

void PrintagoPanel::HandlePrintagoCommand(const wxString& commandType, const wxString& action, 
                                          wxStringToStringHashMap& parameters, const wxString& originalCommandStr) {
    wxString actionDetail;
    wxLogMessage("HandlePrintagoCommand: {command: " + commandType + ", action: " + action + "}");
    MachineObject* printer = { nullptr };
    auto machineList = devManager->get_my_machine_list();

    if (!commandType.compare("status")) {
        if (!action.compare("get_machine_list")) {
            std::string username = wxGetApp().getAgent()->is_user_login() ? wxGetApp().getAgent()->get_user_name() : "[printago_slicer_id?]";
            SendResponseMessage(username, GetAllStatus(), m_browser, originalCommandStr);
            return;
        } else {
            SendErrorMessage("", {{"error", "invalid status action"}}, m_browser, originalCommandStr);
            wxLogMessage("PrintagoCommandError: Invalid status action: " + action);
            return;
        }
    } 

    wxString printerId = parameters.count("printer_id") ? parameters["printer_id"] : "Unspecified";
    if (!printerId.compare("Unspecified")) {
        SendErrorMessage("", {{"error", "no printer_id specified"}}, m_browser, originalCommandStr);
        wxLogMessage("PrintagoCommandError: No printer_id specified");
        return;
    }
    // Find the printer in the machine list
    auto it = std::find_if(machineList.begin(), machineList.end(),
        [&printerId](const std::pair<std::string, MachineObject*>& pair) {
            return pair.second->dev_id == printerId;
        });
    
    if (it != machineList.end()) {  
        // Printer found
        printer = it->second;
    } else {
        SendErrorMessage(printerId, {{"error", "no printer not found with ID: " + printerId.ToStdString()}}, m_browser, originalCommandStr);
        wxLogMessage("PrintagoCommandError: No printer found with ID: " + printerId);
        return;
    }

    if (!commandType.compare("printer_control")) {
        if (!action.compare("pause_print")) {
            try {
                printer->command_task_pause();
            } catch (...) {
                SendErrorMessage(printerId, {{"error", "an error occurred issuing pause_print"}}, m_browser, originalCommandStr);
                return;
            } 
        } else if (!action.compare("resume_print")) {
            try {
                if (printer->can_resume()) {
                    printer->command_task_resume();
                } else {
                    SendErrorMessage(printerId, {{"error", "cannot resume print"}}, m_browser, originalCommandStr);
                    return;
                }
            } catch (...) {
                SendErrorMessage(printerId, {{"error", "an error occurred issuing resume_print"}}, m_browser, originalCommandStr);
                return;
            }
            return;
        } else if (!action.compare("stop_print")) {
            try {
                printer->command_task_abort();
            } catch (...) {
                SendErrorMessage(printerId, {{"error", "an error occurred issuing stop_print"}}, m_browser, originalCommandStr);
                return;
            }   
        } else if (!action.compare("get_status")) {
            SendStatusMessage(printerId, MachineObjectToJson(printer), m_browser, originalCommandStr);
            return;
        } else if (!action.compare("start_print_bbl")) {
            wxString printagoFileUrl = parameters["url"];
            wxString decodedUrl = { "" };
            jobPrinterId = printerId;
            set_can_process_job(false);
            if (!m_select_machine_dlg) m_select_machine_dlg = new SelectMachineDialog(wxGetApp().plater());
            
            if(!can_process_job()) {
                SendErrorMessage(printerId, {{"error", "busy with current job - check status"}}, m_browser, originalCommandStr);
                return;
            }

            if (printagoFileUrl.empty()) {
                SendErrorAndUnblock(printerId, {{"error", "no url specified"}}, m_browser, originalCommandStr);
                return;
            } else {
                decodedUrl = wxURI::Unescape(printagoFileUrl);
                wxURL url(decodedUrl);
                if (url.GetError() != wxURL_NOERR) {   
                    SendErrorAndUnblock(printerId, {{"error", "cannot access url; " + wxURLErrorToString(url.GetError()).ToStdString()}}, m_browser, originalCommandStr);
                    return;
                }
                if (!url.IsOk()) {
                    SendErrorAndUnblock(printerId, {{"error", "invalid url specified: " + decodedUrl.ToStdString()}}, m_browser, originalCommandStr);
                    return;
                }
            }
            
            //TODO: handle this in a better way instead of checking "Download Failed"
            wxString localFilePath = SavePrintagoFile(decodedUrl);
            if (!localFilePath.compare("Download Failed")) {
                SendErrorAndUnblock(printerId, {{"error", "download failed"}}, m_browser, originalCommandStr);
                return;
            } else {
                wxLogMessage("Downloaded file to: " + localFilePath);
                actionDetail = "Slicing Started: " + localFilePath;
            }

            //TODO: clear plate[0], remove any other plates.
            //TODO: IF localFilePath is .3MF file; use plater.load_project() instead of load_files().
            std::vector<std::string> filePathArray;
            filePathArray.push_back(localFilePath.ToStdString());
            std::vector<size_t> loadedFilesIndices = wxGetApp().plater()->load_files(filePathArray, LoadStrategy::Restore, false);   
            wxGetApp().plater()->select_plate(0, true);
            wxGetApp().plater()->reslice();

        } else {
                SendErrorMessage(printerId, {{"error", "invalid printer_control action"}}, m_browser, originalCommandStr);
                return;
        }

        wxString actionString;
        if(actionDetail.IsEmpty()) {
            actionString = action;
        } else {
            actionString = wxString::Format("%s: %s", action, actionDetail); 
        }
        SendSuccessMessage(printerId, actionString, m_browser, originalCommandStr);
    
    } else if (!commandType.compare("temperature_control")) {
        wxString tempStr = parameters["temperature"];
        long targetTemp;
        if (!tempStr.ToLong(&targetTemp)) {
            SendErrorMessage(printerId, {{"error", "invalid temperature value"}}, m_browser, originalCommandStr);
            return;
        }

        if (!action.compare("set_hotend")) {
            try {
                printer->command_set_nozzle(targetTemp);
                actionDetail = wxString::Format("%d", targetTemp);
            } catch (...) {
                SendErrorMessage(printerId, {{"error", "an error occurred setting nozzle temperature"}}, m_browser, originalCommandStr);
                return;
            }
        } else if (!action.compare("set_bed")) {
            try {
                int limit = printer->get_bed_temperature_limit();
                if (targetTemp >= limit) {
                    targetTemp = limit;
                }
                printer->command_set_bed(targetTemp);
                actionDetail = wxString::Format("%d", targetTemp);
            } catch (...) {
                SendErrorMessage(printerId, {{"error", "an error occurred setting bed temperature"}}, m_browser, originalCommandStr);
                return;
            }
        } else {
            SendErrorMessage(printerId, {{"error", "invalid temperature_control action"}}, m_browser, originalCommandStr);
            wxLogMessage("PrintagoCommandError: Invalid temperature_control action: " + action);
            return;
        }

        wxString actionString;
        if(actionDetail.IsEmpty()) {
            actionString = action;
        } else {
            actionString = wxString::Format("%s: %s", action, actionDetail); 
        }
        SendSuccessMessage(printerId, actionString, m_browser, originalCommandStr);

    } else if (!commandType.compare("movement_control")) {
        if (!action.compare("jog")) {
            auto axes = ExtractPrefixedParams(parameters, "axes");
            if (axes.empty()) {
                SendErrorMessage(printerId, {{"error", "no axes specified"}}, m_browser, originalCommandStr);
                wxLogMessage("PrintagoCommandError: No axes specified");
                return;
            } 

            if (!printer->is_axis_at_home("X")
                || !printer->is_axis_at_home("Y")
                || !printer->is_axis_at_home("Z")) {
                SendErrorMessage(printerId, {{"error", "must home axes before moving"}}, m_browser, originalCommandStr);
                wxLogMessage("PrintagoCommandError: Axes not at home");
                return;
            }
            // Iterate through each axis and its value; we do this loop twice to ensure the input in clean.
            // this ensures we do not move the head unless all input moves are valid.
            for (const auto& axis : axes) {
                wxString axisName = axis.first; 
                axisName.MakeUpper(); 
                if (axisName != "X" && axisName != "Y" && axisName != "Z") {
                    SendErrorMessage(printerId, {{"error", "Invalid axis name: " + axisName.ToStdString()}}, m_browser, originalCommandStr);
                    wxLogMessage("PrintagoCommandError: Invalid axis name " + axisName);
                    return;
                }
                wxString axisValueStr = axis.second;
                double axisValue;
                if (!axisValueStr.ToDouble(&axisValue)) {
                    SendErrorMessage(printerId, {{"error", "Invalid value for axis " + axisName.ToStdString()}}, m_browser, originalCommandStr);
                    wxLogMessage("PrintagoCommandError: Invalid value for axis " + axisName);
                    return;
                }
            }

            for (const auto& axis : axes) {
                wxString axisName = axis.first;
                axisName.MakeUpper(); 
                wxString axisValueStr = axis.second;
                double axisValue;
                axisValueStr.ToDouble(&axisValue);
                try {
                    printer->command_axis_control(axisName.ToStdString(), 1.0, axisValue, 3000);
                } catch (...) {
                    SendErrorMessage(printerId, {{"error", "an error occurred moving axis " + axisName.ToStdString()}}, m_browser, originalCommandStr);
                    wxLogMessage("PrintagoCommandError: An error occurred moving axis " + axisName);
                    return;
                }
            }

        } else if(!action.compare("home")) {
            try{
                printer->command_go_home();
            } catch (...) {
                SendErrorMessage(printerId, {{"error", "an error occurred homing axes"}}, m_browser, originalCommandStr);
                wxLogMessage("PrintagoCommandError: An error occurred homing axes");
                return;
            }

        } else if(!action.compare("extrude")) {
            wxString amtStr = parameters["amount"];
            long extrudeAmt;
            if (!amtStr.ToLong(&extrudeAmt)) {
                wxLogMessage("Invalid extrude amount value: " + amtStr);
                SendErrorMessage(printerId, {{"error", "invalid extrude amount value"}}, m_browser, originalCommandStr);
                return;
            }

            if (printer->nozzle_temp >= PRINTAGO_TEMP_THRESHOLD_ALLOW_E_CTRL) {
                try {
                    printer->command_axis_control("E", 1.0, extrudeAmt, 900);
                    actionDetail = wxString::Format("%d", extrudeAmt);
                } catch (...) {
                    SendErrorMessage(printerId, {{"error", "an error occurred extruding filament"}}, m_browser, originalCommandStr);
                    wxLogMessage("PrintagoCommandError: An error occurred extruding filament");
                    return;
                }
            } else {
                SendErrorMessage(printerId, {{"error", wxString::Format("nozzle temperature too low to extrude (min: %.1f)", PRINTAGO_TEMP_THRESHOLD_ALLOW_E_CTRL).ToStdString()}}, m_browser, originalCommandStr);
                wxLogMessage("PrintagoCommandError: Nozzle temperature too low to extrude");
                return;
            }
        
        } else {
            SendErrorMessage(printerId, {{"error", "invalid movement_control action"}}, m_browser, originalCommandStr);
            wxLogMessage("PrintagoCommandError: Invalid movement_control action");
            return;
        }
       
        wxString actionString;
        if(actionDetail.IsEmpty()) {
            actionString = action;
        } else {
            actionString = wxString::Format("%s: %s", action, actionDetail); 
        }
        SendSuccessMessage(printerId, actionString, m_browser, originalCommandStr);
    }
}

void PrintagoPanel::OnProcessCompleted(SlicingProcessCompletedEvent &event) {
    if (!m_select_machine_dlg || jobPrinterId.IsEmpty() || !event.success())
        return;

    
    wxString actionString = wxString::Format("%s: %s", "start_print_bbl", "Slicing Complete");
    SendSuccessMessage(jobPrinterId, actionString, m_browser);
    PrintFromType print_type = PrintFromType::FROM_NORMAL;
    m_select_machine_dlg->set_print_type(print_type);

    m_select_machine_dlg->prepare(0); // partplate_list.get_curr_plate_index());
    devManager->set_selected_machine(jobPrinterId.ToStdString(), false);
    m_select_machine_dlg->setPrinterLastSelect(jobPrinterId.ToStdString());
    
    wxCommandEvent evt(EVT_PRINTAGO_PRINT);
    m_select_machine_dlg->on_ok_btn(evt);
    actionString = wxString::Format("%s: %s", "start_print_bbl", "Job Sending to Printer");
    SendSuccessMessage(jobPrinterId, actionString, m_browser);
    m_select_machine_dlg = nullptr;
    set_can_process_job(true);
}

wxStringToStringHashMap PrintagoPanel::ParseQueryString(const wxString& queryString) {
    wxStringToStringHashMap params;

    // Split the query string on '&' to get key-value pairs
    wxStringTokenizer tokenizer(queryString, "&");
    while (tokenizer.HasMoreTokens()) {
        wxString token = tokenizer.GetNextToken();

        // Split each key-value pair on '='
        wxString key = token.BeforeFirst('=');
        wxString value = token.AfterFirst('=');

        // URL-decode the key and value
        wxString decodedKey = wxURI::Unescape(key);
        wxString decodedValue = wxURI::Unescape(value);

        params[decodedKey] = decodedValue;
    }
    return params;
}

std::map<wxString, wxString> PrintagoPanel::ExtractPrefixedParams(const wxStringToStringHashMap& params, const wxString& prefix) {
    std::map<wxString, wxString> extractedParams;
    for (const auto& kv : params) {
        if (kv.first.StartsWith(prefix + ".")) {
            wxString parmName = kv.first.Mid(prefix.length() + 1); // +1 for the dot
            extractedParams[parmName] = kv.second;
        }
    }
    return extractedParams;
}

void PrintagoPanel::SendJsonMessage(const wxString& msg_type, const wxString& printer_id, const json& data, 
                                    wxWebView* webView, const wxString& command) {
    wxDateTime now = wxDateTime::Now();
    now.MakeUTC();
    wxString timestamp = now.FormatISOCombined() + "Z";

    json message;
    message["type"] = msg_type.ToStdString();
    message["timestamp"] = timestamp.ToStdString();
    message["printer_id"] = printer_id.ToStdString();
    message["client_type"] = "bambu";
    message["command"] = command.ToStdString();
    message["data"] = data;

    wxString messageStr = wxString(message.dump().c_str(), wxConvUTF8);
    CallAfter([this, webView, messageStr] {
        webView->RunScript(wxString::Format("window.postMessage(%s, '*');", messageStr));
    });
    // webView->RunScript(wxString::Format("window.postMessage(%s, '*');", messageStr));
}

void PrintagoPanel::SendStatusMessage(const wxString& printer_id, const json& statusData, 
                                      wxWebView* webView, const wxString& command) {

    SendJsonMessage("status", printer_id, statusData, webView, command);
}

void PrintagoPanel::SendResponseMessage(const wxString& printer_id, const json& responseData, 
                                        wxWebView* webView, const wxString& command) {
    SendJsonMessage("response", printer_id, responseData, webView, command);
}

void PrintagoPanel::SendSuccessMessage(const wxString& printer_id, const wxString localCommand, 
                                      wxWebView* webView, const wxString& command) {
    json responseData;
    responseData["local_command"] = localCommand.ToStdString();
    responseData["success"] = true;
    SendJsonMessage("success", printer_id, responseData, webView, command);
}

void PrintagoPanel::SendErrorMessage(const wxString& printer_id, const json& errorData, 
                                    wxWebView* webView, const wxString& command) {
    SendJsonMessage("error", printer_id, errorData, webView, command);
}

//wraps sending and error response, and unblocks the server for job processing.
void PrintagoPanel::SendErrorAndUnblock(const wxString& printer_id, const json& errorData, 
                                    wxWebView* webView, const wxString& command) {
    SendErrorMessage(printer_id, errorData, webView, command);
    std::string str = errorData.dump();
    wxString wxStr = "PrintagoCommandError: " + wxString::FromUTF8(str.c_str()); 
    wxLogMessage(wxStr);
    set_can_process_job(true);
}

/**
    * Callback invoked when there is a request to load a new page (for instance
    * when the user clicks a link)
    */
void PrintagoPanel::OnNavigationRequest(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
    
    const wxString &url = evt.GetURL();

    if (url.StartsWith("printago://")) {
        evt.Veto(); // Prevent the web view from navigating to this URL

        wxURI uri(url);
        wxString path = uri.GetPath();
        wxArrayString pathComponents = wxStringTokenize(path, "/");
        wxString commandType, action;

        // Extract commandType and action from the path
        if (pathComponents.GetCount() >= 2) {
            commandType = pathComponents.Item(1); // The first actual component after the leading empty one
            action = pathComponents.Item(2); // The second actual component
        } else {
            // Handle error: insufficient components in the path
            return;
        }

        wxString query = uri.GetQuery(); // Get the query part of the URI
        wxStringToStringHashMap parameters = ParseQueryString(query); // Use ParseQueryString to get parameters

        HandlePrintagoCommand(commandType, action, parameters, url.ToStdString());
    }

    if (m_info->IsShown())
    {
        m_info->Dismiss();
    }

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("%s", "Navigation request to '" + evt.GetURL() + "' (target='" +
            evt.GetTarget() + "')");

    //If we don't want to handle navigation then veto the event and navigation
    //will not take place, we also need to stop the loading animation

//    if (!m_tools_handle_navigation->IsChecked()) {
//       evt.Veto();
//        m_button_stop->Enable(false);
//    } else {
        UpdateState();
//    }
}

/**
    * Callback invoked when a navigation request was accepted
    */
void PrintagoPanel::OnNavigationComplete(wxWebViewEvent& evt)
{
    m_browser->Show();
    Layout();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("%s", "Navigation complete; url='" + evt.GetURL() + "'");
    UpdateState();
}

/**
    * Callback invoked when a page is finished loading
    */
void PrintagoPanel::OnDocumentLoaded(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
    // Only notify if the document is the main frame, not a subframe
    if (evt.GetURL() == m_browser->GetCurrentURL())
    {
        if (wxGetApp().get_mode() == comDevelop)
            wxLogMessage("%s", "Document loaded; url='" + evt.GetURL() + "'");
    }
    UpdateState();
    ShowNetpluginTip();
}

void PrintagoPanel::OnTitleChanged(wxWebViewEvent &evt)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": " << evt.GetString().ToUTF8().data();
    // wxGetApp().CallAfter([this] { SendRecentList(); });
}

/**
    * On new window, we veto to stop extra windows appearing
    */
void PrintagoPanel::OnNewWindow(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": " << evt.GetURL().ToUTF8().data();
    wxString flag = " (other)";

    if (evt.GetNavigationAction() == wxWEBVIEW_NAV_ACTION_USER)
    {
        flag = " (user)";
    }

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("%s", "New window; url='" + evt.GetURL() + "'" + flag);

    //If we handle new window events then just load them in this window as we
    //are a single window browser
    if (m_tools_handle_new_window->IsChecked())
        m_browser->LoadURL(evt.GetURL());

    UpdateState();
}

void PrintagoPanel::OnScriptMessage(wxWebViewEvent& evt)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": " << evt.GetString().ToUTF8().data();
    // update login status
    if (m_LoginUpdateTimer == nullptr) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Create Timer";
        m_LoginUpdateTimer = new wxTimer(this, PRINTAGO_LOGIN_INFO_UPDATE_TIMER_ID);
        m_LoginUpdateTimer->Start(2000);
    }

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("Script message received; value = %s, handler = %s", evt.GetString(), evt.GetMessageHandler());
    std::string response = wxGetApp().handle_web_request(evt.GetString().ToUTF8().data());
    if (response.empty()) return;

    /* remove \n in response string */
    response.erase(std::remove(response.begin(), response.end(), '\n'), response.end());
    if (!response.empty()) {
        m_response_js = wxString::Format("window.postMessage('%s')", response);
        wxCommandEvent* event = new wxCommandEvent(EVT_PRINTAGO_RESPONSE_MESSAGE, this->GetId());
        wxQueueEvent(this, event);
    }
    else {
        m_response_js.clear();
    }
}

void PrintagoPanel::OnScriptResponseMessage(wxCommandEvent& WXUNUSED(evt))
{
    if (!m_response_js.empty()) {
        RunScript(m_response_js);
    }
}

/**
    * Invoked when user selects the "View Source" menu item
    */
void PrintagoPanel::OnViewSourceRequest(wxCommandEvent& WXUNUSED(evt))
{
    // SourceViewDialog dlg(this, m_browser->GetPageSource());
    // dlg.ShowModal();
}

/**
    * Invoked when user selects the "View Text" menu item
    */
void PrintagoPanel::OnViewTextRequest(wxCommandEvent& WXUNUSED(evt))
{
    wxDialog textViewDialog(this, wxID_ANY, "Page Text",
        wxDefaultPosition, wxSize(700, 500),
        wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);

    wxTextCtrl* text = new wxTextCtrl(this, wxID_ANY, m_browser->GetPageText(),
        wxDefaultPosition, wxDefaultSize,
        wxTE_MULTILINE |
        wxTE_RICH |
        wxTE_READONLY);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(text, 1, wxEXPAND);
    SetSizer(sizer);
    textViewDialog.ShowModal();
}

/**
    * Invoked when user selects the "Menu" item
    */
void PrintagoPanel::OnToolsClicked(wxCommandEvent& WXUNUSED(evt))
{
    if (m_browser->GetCurrentURL() == "")
        return;

    m_edit_cut->Enable(m_browser->CanCut());
    m_edit_copy->Enable(m_browser->CanCopy());
    m_edit_paste->Enable(m_browser->CanPaste());

    m_edit_undo->Enable(m_browser->CanUndo());
    m_edit_redo->Enable(m_browser->CanRedo());

    m_selection_clear->Enable(m_browser->HasSelection());
    m_selection_delete->Enable(m_browser->HasSelection());

    m_context_menu->Check(m_browser->IsContextMenuEnabled());
    m_dev_tools->Check(m_browser->IsAccessToDevToolsEnabled());

    wxPoint position = ScreenToClient(wxGetMousePosition());
    PopupMenu(m_tools_menu, position.x, position.y);
}

void PrintagoPanel::RunScript(const wxString& javascript)
{
    // Remember the script we run in any case, so the next time the user opens
    // the "Run Script" dialog box, it is shown there for convenient updating.
    m_javascript = javascript;

    if (!m_browser) return;

    WebView::RunScript(m_browser, javascript);
}

void PrintagoPanel::OnRunScriptString(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("setCount(345);");
}

void PrintagoPanel::OnRunScriptInteger(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(a){return a;}f(123);");
}

void PrintagoPanel::OnRunScriptDouble(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(a){return a;}f(2.34);");
}

void PrintagoPanel::OnRunScriptBool(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(a){return a;}f(false);");
}

void PrintagoPanel::OnRunScriptObject(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(){var person = new Object();person.name = 'Foo'; \
    person.lastName = 'Bar';return person;}f();");
}

void PrintagoPanel::OnRunScriptArray(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(){ return [\"foo\", \"bar\"]; }f();");
}

void PrintagoPanel::OnRunScriptDOM(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("document.write(\"Hello World!\");");
}

void PrintagoPanel::OnRunScriptUndefined(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(){var person = new Object();}f();");
}

void PrintagoPanel::OnRunScriptNull(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(){return null;}f();");
}

void PrintagoPanel::OnRunScriptDate(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("function f(){var d = new Date('10/08/2017 21:30:40'); \
    var tzoffset = d.getTimezoneOffset() * 60000; \
    return new Date(d.getTime() - tzoffset);}f();");
}

void PrintagoPanel::OnRunScriptMessage(wxCommandEvent& WXUNUSED(evt))
{
    RunScript("window.wx.postMessage('This is a web message');");
}

void PrintagoPanel::OnRunScriptCustom(wxCommandEvent& WXUNUSED(evt))
{
    wxTextEntryDialog dialog
    (
        this,
        "Please enter JavaScript code to execute",
        wxGetTextFromUserPromptStr,
        m_javascript,
        wxOK | wxCANCEL | wxCENTRE | wxTE_MULTILINE
    );
    if (dialog.ShowModal() != wxID_OK)
        return;

    RunScript(dialog.GetValue());
}

void PrintagoPanel::OnAddUserScript(wxCommandEvent& WXUNUSED(evt))
{
    wxString userScript = "window.wx_test_var = 'wxWidgets webview sample';";
    wxTextEntryDialog dialog
    (
        this,
        "Enter the JavaScript code to run as the initialization script that runs before any script in the HTML document.",
        wxGetTextFromUserPromptStr,
        userScript,
        wxOK | wxCANCEL | wxCENTRE | wxTE_MULTILINE
    );
    if (dialog.ShowModal() != wxID_OK)
        return;

    if (!m_browser->AddUserScript(dialog.GetValue()))
        wxLogError("Could not add user script");
}

void PrintagoPanel::OnSetCustomUserAgent(wxCommandEvent& WXUNUSED(evt))
{
    wxString customUserAgent = "Mozilla/5.0 (iPhone; CPU iPhone OS 13_1_3 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/13.0.1 Mobile/15E148 Safari/604.1";
    wxTextEntryDialog dialog
    (
        this,
        "Enter the custom user agent string you would like to use.",
        wxGetTextFromUserPromptStr,
        customUserAgent,
        wxOK | wxCANCEL | wxCENTRE
    );
    if (dialog.ShowModal() != wxID_OK)
        return;

    if (!m_browser->SetUserAgent(customUserAgent))
        wxLogError("Could not set custom user agent");
}

void PrintagoPanel::OnClearSelection(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->ClearSelection();
}

void PrintagoPanel::OnDeleteSelection(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->DeleteSelection();
}

void PrintagoPanel::OnSelectAll(wxCommandEvent& WXUNUSED(evt))
{
    m_browser->SelectAll();
}

/**
    * Callback invoked when a loading error occurs
    */
void PrintagoPanel::OnError(wxWebViewEvent& evt)
{
#define WX_ERROR_CASE(type) \
    case type: \
    category = #type; \
    break;

    wxString category;
    switch (evt.GetInt())
    {
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CONNECTION);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_CERTIFICATE);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_AUTH);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_SECURITY);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_NOT_FOUND);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_REQUEST);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_USER_CANCELLED);
        WX_ERROR_CASE(wxWEBVIEW_NAV_ERR_OTHER);
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": [" << category << "] " << evt.GetString().ToUTF8().data();

    if (wxGetApp().get_mode() == comDevelop)
        wxLogMessage("%s", "Error; url='" + evt.GetURL() + "', error='" + category + " (" + evt.GetString() + ")'");

    //Show the info bar with an error
    m_info->ShowMessage(_L("An error occurred loading ") + evt.GetURL() + "\n" +
        "'" + category + "'", wxICON_ERROR);

    UpdateState();
}


} // GUI
} // Slic3r
