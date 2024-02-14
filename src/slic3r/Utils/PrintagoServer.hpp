#ifndef PRINTAGOSERVER_HPP
#define PRINTAGOSERVER_HPP

#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>

#include "nlohmann/json.hpp"
#include "slic3r/GUI/SelectMachine.hpp"
#include "slic3r/GUI/BackgroundSlicingProcess.hpp"

using namespace nlohmann;
namespace beefy     = boost::beast;
namespace websocket = beefy::websocket;
namespace net       = boost::asio;
using tcp           = net::ip::tcp;

namespace Slic3r {

static constexpr short PRINTAGO_PORT = 33647;
void                   printago_ws_error(beefy::error_code ec, char const* what);

class PrintagoDirector;

//``````````````````````````````````````````````````
//------------------PrintagoSession------------------
//``````````````````````````````````````````````````
class PrintagoSession : public std::enable_shared_from_this<PrintagoSession>
{
    friend class PrintagoDirector;

    websocket::stream<tcp::socket> ws_;
    beefy::flat_buffer             buffer_;
    bool                           is_authorized = false;

public:
    explicit PrintagoSession(tcp::socket&& socket);
    void run();

    void set_authorized(bool status) { is_authorized = status; }
    bool get_authorized() const { return is_authorized; }

private:
    void on_run();
    void on_accept(beefy::error_code ec);
    void do_read();
    void on_read(beefy::error_code ec, std::size_t bytes_transferred);
    void on_write(beefy::error_code ec, std::size_t bytes_transferred);
    void async_send(const std::string& message);
    void do_write(std::shared_ptr<std::string> messageBuffer);
};

//``````````````````````````````````````````````````
//------------------PrintagoServer------------------
//``````````````````````````````````````````````````
class PrintagoServer : public std::enable_shared_from_this<PrintagoServer>
{
    net::io_context& ioc_;
    tcp::acceptor    acceptor_;
    std::size_t      reconnection_delay_;

    std::shared_ptr<PrintagoSession> active_session;

public:
    PrintagoServer(net::io_context& ioc, tcp::endpoint endpoint);
    void start();

    void                             set_session(std::shared_ptr<PrintagoSession> session) { active_session = session; }
    void                             clear_session() { active_session = nullptr; }
    std::shared_ptr<PrintagoSession> get_session() const { return active_session; }

private:
    void do_accept();
    void on_accept(beefy::error_code ec, tcp::socket socket);
    void handle_reconnect();
};

//``````````````````````````````````````````````````
//------------------PrintagoCommand-----------------
//``````````````````````````````````````````````````
class PrintagoCommand
{
public:
    PrintagoCommand() = default;

    PrintagoCommand(const wxString& command_type, const wxString& action, json& parameters, json originalCommand)
        : m_command_type(command_type), m_action(action), m_parameters(std::move(parameters)), m_original_command(std::move(originalCommand))
    {}

    PrintagoCommand(const PrintagoCommand& other)
        : m_command_type(other.m_command_type)
        , m_action(other.m_action)
        , m_parameters(other.m_parameters)
        , m_original_command(other.m_original_command)
    {}

    virtual ~PrintagoCommand() {}

    void SetCommandType(const wxString& command) { m_command_type = command; }
    void SetAction(const wxString& action) { m_action = action; }
    void SetParameters(const json& parameters) { m_parameters = parameters; }
    void SetOriginalCommand(const json& originalCommandStr) { m_original_command = originalCommandStr; }

    wxString GetCommandType() const { return m_command_type; }
    wxString GetAction() const { return m_action; }
    json     GetParameters() const { return m_parameters; }
    json     GetOriginalCommand() const { return m_original_command; }

private:
    wxString                m_command_type;
    wxString                m_action;
    json                    m_parameters;
    json                    m_original_command;
};

//``````````````````````````````````````````````````
//------------------PrintagoResponse----------------
//``````````````````````````````````````````````````
class PrintagoResponse
{
public:
    PrintagoResponse() {}

    PrintagoResponse(const PrintagoResponse& response)
    {
        this->m_message_type = response.m_message_type;
        this->m_printer_id   = response.m_printer_id;
        this->m_command      = response.m_command;
        this->m_data         = response.m_data;
    }

    virtual ~PrintagoResponse() {}

    void SetMessageType(const wxString& message) { this->m_message_type = message; }
    void SetPrinterId(const wxString& printer_id) { this->m_printer_id = printer_id; }
    void SetCommand(const json& command) { this->m_command = command; }
    void SetData(const json& data) { this->m_data = data; }

    wxString GetMessageType() const { return this->m_message_type; }
    wxString GetPrinterId() const { return this->m_printer_id; }
    json     GetCommand() const { return this->m_command; }
    json     GetData() const { return this->m_data; }

private:
    wxString m_message_type;
    wxString m_printer_id;
    json     m_command;
    json     m_data;
};

//``````````````````````````````````````````````````
//------------------PrintagoDirector----------------
//``````````````````````````````````````````````````
class PrintagoDirector
{
public:
    PrintagoDirector();
    ~PrintagoDirector();

    bool ParseCommand(json command);
    void OnSlicingCompleted(SlicingProcessCompletedEvent::StatusType slicing_result);
    void OnPrintJobSent(wxString printerId, bool success);

    void PostJobUpdateMessage();

    void ResetMachineDialog()
    {
        delete m_select_machine_dlg;
        m_select_machine_dlg = nullptr;
    }

private:
    std::shared_ptr<net::io_context> _io_context;
    std::shared_ptr<PrintagoServer>  server;
    std::thread                      server_thread;

    GUI::SelectMachineDialog* m_select_machine_dlg = nullptr;

    void PostStatusMessage(const wxString& printer_id, const json& statusData, const json& command = {});
    void PostResponseMessage(const wxString& printer_id, const json& responseData, const json& command = {});
    void PostSuccessMessage(const wxString& printer_id,
                            const wxString& localCommand,
                            const json&     command            = {},
                            const wxString& localCommandDetail = "");
    void PostErrorMessage(const wxString& printer_id, const wxString& localCommand, const json& command = {}, const wxString& errorDetail = "");

    void _PostResponse(const PrintagoResponse& response) const;

    bool                         ValidatePrintagoCommand(const PrintagoCommand& cmd);
    bool                         ProcessPrintagoCommand(const PrintagoCommand& command);

    json GetAllStatus();
    void AddCurrentProcessJsonTo(json& statusObject);
    json GetMachineStatus(const wxString& printerId);
    json GetMachineStatus(MachineObject* machine);
    json MachineObjectToJson(MachineObject* machine);
    json ConfigToJson(const DynamicPrintConfig& config,
                      const std::string&        name,
                      const std::string&        from,
                      const std::string&        version,
                      const std::string         is_custom = "");

    json GetCompatOtherConfigsNames(Preset::Type preset_type, const Preset& printerPreset);
    json GetCompatFilamentConfigNames(const Preset& printerPreset)
    {
        return GetCompatOtherConfigsNames(Preset::TYPE_FILAMENT, printerPreset);
    }
    json GetCompatPrintConfigNames(const Preset& printerPreset) { return GetCompatOtherConfigsNames(Preset::TYPE_PRINT, printerPreset); }

    bool IsConfigCompatWithPrinter(const PresetWithVendorProfile& preset, const Preset& printerPreset);
    bool IsConfigCompatWithParent(const PresetWithVendorProfile& preset, const PresetWithVendorProfile& active_printer);

    std::string GetConfigNameFromJsonFile(const wxString& FilePath);
    json        GetConfigByName(wxString configType, wxString configName);
    json        GetCompatPrinterConfigNames(std::string printer_type);
    void        ImportPrintagoConfigs();
    void        SetPrintagoConfigs();

    bool SwitchSelectedPrinter(const wxString& printerId);

    bool SavePrintagoFile(const wxString url, wxFileName& localPath);
    bool DownloadFileFromURL(const wxString url, const wxFileName& localPath);

    bool ValidateToken(const std::string& token);
};

//``````````````````````````````````````````````````
//------------------PBJob (Printago Blocking Job)---
//``````````````````````````````````````````````````
enum class JobServerState { Idle, Download, Configure, Slicing, Sending };

class PBJob
{
private:
    static bool SetCanProcessJob(const bool can_process_job)
    {
        if (can_process_job) {
            printerId.Clear();
            command = {};
            localFile.Clear();
            m_serverState = JobServerState::Idle;
            configFiles.clear();
            progress = 0;
            jobId    = "ptgo_default";

            use_ams             = false;
            bbl_do_bed_leveling = false;
            bbl_do_flow_cali    = false;

            GUI::wxGetApp().printago_director()->ResetMachineDialog();

        }

        m_can_process_job = can_process_job;
        return can_process_job;
    }
    inline static bool           m_can_process_job = true;
    inline static JobServerState m_serverState     = JobServerState::Idle;

public:
    static std::string serverStateStr()
    {
        switch (m_serverState) {
        case JobServerState::Idle: return "idle";
        case JobServerState::Download: return "download";
        case JobServerState::Configure: return "configure";
        case JobServerState::Slicing: return "slicing";
        case JobServerState::Sending: return "sending";
        default: return "null";
        }
    }

    const inline static std::map<JobServerState, int> serverStateProgress = {{JobServerState::Idle, 0},
                                                                             {JobServerState::Download, 7},
                                                                             {JobServerState::Configure, 15},
                                                                             {JobServerState::Slicing, 25},
                                                                             {JobServerState::Sending, 90}};

    inline static wxString                                    jobId = "ptgo_default";
    inline static wxString                                    printerId;
    inline static json                                        command;
    inline static wxFileName                                  localFile;
    inline static std::unordered_map<std::string, wxFileName> configFiles;
    inline static int                                         progress = 0;

    inline static bool    use_ams             = false;
    inline static bool    bbl_do_bed_leveling = false;
    inline static bool    bbl_do_flow_cali    = false;
    inline static BedType bed_type            = BedType::btDefault;

    static BedType StringToBedType(const std::string& bedType)
    {
        if (bedType == "cool_plate")
            return BedType::btPC;
        else if (bedType == "eng_plate")
            return BedType::btEP;
        else if (bedType == "warm_plate")
            return BedType::btPEI;
        else if (bedType == "textured_pei")
            return BedType::btPTE;
        else
            return BedType::btDefault;
    }

    static void SetServerState(JobServerState new_state, bool postMessage = false)
    {
        if (new_state != m_serverState)
            progress = serverStateProgress.at(new_state);
        m_serverState = new_state;

        if (postMessage)
            GUI::wxGetApp().printago_director()->PostJobUpdateMessage();
    }

    static JobServerState GetServerState() { return m_serverState; }
    static bool           CanProcessJob() { return m_can_process_job; }
    static bool           BlockJobProcessing() { return SetCanProcessJob(false); }
    static bool           UnblockJobProcessing() { return SetCanProcessJob(true); }
};

} // namespace Slic3r

#endif // PRINTAGOSERVER_HPP
