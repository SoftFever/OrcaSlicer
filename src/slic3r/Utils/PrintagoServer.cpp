#include "PrintagoServer.hpp"
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <wx/tokenzr.h>
#include <wx/event.h>
#include <wx/url.h>

#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Tab.hpp"

namespace beefy = boost::beast;
using namespace Slic3r::GUI;

namespace Slic3r {

#define PRINTAGO_TEMP_THRESHOLD_ALLOW_E_CTRL 170.0f // Minimum temperature to allow extrusion control (per StatusPanel.cpp)

void printago_ws_error(beefy::error_code ec, char const* what) { BOOST_LOG_TRIVIAL(error) << what << ": " << ec.message(); }

//``````````````````````````````````````````````````
//------------------PrintagoSession------------------
//``````````````````````````````````````````````````
PrintagoSession::PrintagoSession(tcp::socket&& socket) : ws_(std::move(socket)) {}
void PrintagoSession::run() { on_run(); }
void PrintagoSession::on_run()
{
    ws_.async_accept([capture0 = shared_from_this()](auto&& PH1) { capture0->on_accept(std::forward<decltype(PH1)>(PH1)); });
}
void PrintagoSession::on_accept(beefy::error_code ec)
{
    if (ec)
        return printago_ws_error(ec, "accept");
    do_read();
}

void PrintagoSession::do_read()
{
    ws_.async_read(buffer_, [capture0 = shared_from_this()](auto&& PH1, auto&& PH2) {
        try {
            capture0->on_read(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2));
        } catch (std::exception e) {
            capture0->ws_.close(websocket::close_code::normal);
            wxGetApp().printago_director()->GetServer()->get_session()->set_authorized(false);
            wxGetApp().printago_director()->GetServer()->start();
        }
    });
}

void PrintagoSession::on_read(beefy::error_code ec, std::size_t bytes_transferred)
{
    if (ec) {
        if (ws_.is_open())
            ws_.close(websocket::close_code::normal);
        printago_ws_error(ec, "read");
        wxGetApp().printago_director()->GetServer()->get_session()->set_authorized(false);
        wxGetApp().printago_director()->GetServer()->start();
        return;
    } else {
        ws_.text(ws_.got_text());
        const auto msg = beefy::buffers_to_string(buffer_.data());

        try {
            auto json_msg = nlohmann::json::parse(msg);
            wxGetApp().printago_director()->ParseCommand(json_msg);
        } catch (const nlohmann::json::parse_error& e) {
            ws_.close(websocket::close_code::normal);
            printago_ws_error(ec, "parse");
            wxGetApp().printago_director()->GetServer()->get_session()->set_authorized(false);
            wxGetApp().printago_director()->GetServer()->start();
            return;    
        }

        buffer_.consume(buffer_.size());
        do_read();
    }
}

void PrintagoSession::async_send(const std::string& message)
{
    auto messageBuffer = std::make_shared<std::string>(message);
    net::post(ws_.get_executor(), [self = shared_from_this(), messageBuffer]() { self->do_write(messageBuffer); });
}

void PrintagoSession::do_write(std::shared_ptr<std::string> messageBuffer)
{
    ws_.async_write(net::buffer(*messageBuffer), [self = shared_from_this(), messageBuffer](beefy::error_code ec, std::size_t length) {
        if (ec) {
            if (self->ws_.is_open())
                self->ws_.close(websocket::close_code::normal);
            printago_ws_error(ec, "write");
            wxGetApp().printago_director()->GetServer()->get_session()->set_authorized(false);
            wxGetApp().printago_director()->GetServer()->start();
        }
    });
}

//``````````````````````````````````````````````````
//------------------PrintagoServer------------------
//``````````````````````````````````````````````````
PrintagoServer::PrintagoServer(net::io_context& ioc, tcp::endpoint endpoint) : ioc_(ioc), acceptor_(ioc), reconnection_delay_(1)
{
    beefy::error_code ec;
    acceptor_.open(endpoint.protocol(), ec);
    if (ec)
        printago_ws_error(ec, "open");

    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
    if (ec)
        printago_ws_error(ec, "set_option");

    acceptor_.bind(endpoint, ec);
    if (ec)
        printago_ws_error(ec, "bind");
    
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
    if (ec)
        printago_ws_error(ec, "listen");
}

void PrintagoServer::start() { do_accept(); }

void PrintagoServer::do_accept()
{
    acceptor_.async_accept(net::make_strand(ioc_), [capture0 = shared_from_this()](auto&& PH1, auto&& PH2) {
        capture0->on_accept(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2));
    });
}

void PrintagoServer::on_accept(beefy::error_code ec, tcp::socket socket)
{
    if (ec) {
        printago_ws_error(ec, "accept");
    } else {
        reconnection_delay_ = 1; // Reset delay on successful connection
        auto session        = std::make_shared<PrintagoSession>(std::move(socket));
        set_session(session);
        session->run();
    }
}

//``````````````````````````````````````````````````
//------------------PrintagoDirector------------------
//``````````````````````````````````````````````````
PrintagoDirector::PrintagoDirector()
{
    // Initialize and start the server
    _io_context   = std::make_shared<net::io_context>();
    auto endpoint = tcp::endpoint(net::ip::make_address("0.0.0.0"), PRINTAGO_PORT);
    server        = std::make_shared<PrintagoServer>(*_io_context, endpoint);
    server->start();

    // Start the server on a separate thread
    server_thread = std::thread([this] { _io_context->run(); });
    server_thread.detach(); // Detach the thread
}

PrintagoDirector::~PrintagoDirector()
{
    // Ensure proper cleanup
    if (_io_context) {
        _io_context->stop();
    }
    if (server_thread.joinable()) {
        server_thread.join();
    }

    delete m_select_machine_dlg;
}

void PrintagoDirector::PostErrorMessage(const wxString& printer_id,
                                        const wxString& localCommand,
                                        const json&     command,
                                        const wxString& errorDetail)
{
    if (!PBJob::CanProcessJob()) {
        PBJob::UnblockJobProcessing();
    }

    json errorResponse;
    errorResponse["local_command"] = localCommand.ToStdString();
    errorResponse["error_detail"]  = errorDetail.ToStdString();
    errorResponse["success"]       = false;

    auto resp = std::make_unique<PrintagoResponse>();
    resp->SetMessageType("error");
    resp->SetPrinterId(printer_id);
    resp->SetCommand(command);
    resp->SetData(errorResponse);

    _PostResponse(*resp);
}

void PrintagoDirector::PostJobUpdateMessage()
{
    json  responseData;
    auto resp = std::make_unique<PrintagoResponse>();
    resp->SetMessageType("status");
    resp->SetPrinterId(PBJob::printerId);
    resp->SetCommand("job_update");
    AddCurrentProcessJsonTo(responseData);
    resp->SetData(responseData);

    _PostResponse(*resp);
}

void PrintagoDirector::PostResponseMessage(const wxString& printer_id, const json& responseData, const json& command)
{
    auto resp = std::make_unique<PrintagoResponse>();
    resp->SetMessageType("status");
    resp->SetPrinterId(printer_id);
    resp->SetCommand(command);
    resp->SetData(responseData);

    _PostResponse(*resp);
}

void PrintagoDirector::PostSuccessMessage(const wxString& printer_id,
                                          const wxString& localCommand,
                                          const json&     command,
                                          const wxString& localCommandDetail)
{
    json responseData;
    responseData["local_command"]        = localCommand.ToStdString();
    responseData["local_command_detail"] = localCommandDetail.ToStdString();
    responseData["success"]              = true;

    auto resp = std::make_unique<PrintagoResponse>();
    resp->SetMessageType("success");
    resp->SetPrinterId(printer_id);
    resp->SetCommand(command);
    resp->SetData(responseData);
    
    _PostResponse(*resp);
}

void PrintagoDirector::PostStatusMessage(const wxString& printer_id, const json& statusData, const json& command)
{
    auto resp = std::make_unique<PrintagoResponse>();
    resp->SetMessageType("status");
    resp->SetPrinterId(printer_id);
    resp->SetCommand(command);
    resp->SetData(statusData);

    _PostResponse(*resp);
}

void PrintagoDirector::_PostResponse(const PrintagoResponse& response) const
{
    wxDateTime now = wxDateTime::Now();
    now.MakeUTC();
    const wxString timestamp = now.FormatISOCombined() + "Z";

    json message;
    message["type"]        = response.GetMessageType().ToStdString();
    message["timestamp"]   = timestamp.ToStdString();
    message["printer_id"]  = response.GetPrinterId().ToStdString();
    message["client_type"] = "bambu";
    message["command"]     = response.GetCommand();
    message["data"]        = response.GetData();

    const std::string messageStr = message.dump();

    auto session = server->get_session();
    if (session) {
        session->async_send(messageStr);
    }
}

bool PrintagoDirector::ParseCommand(json command)
{
    // Check for existence of "command" and "action" in the JSON object
    if (!command.contains("command") || !command.contains("action")) {
        PostErrorMessage("", "", "", "Missing 'command' or 'action' in command");
        return false;
    }

    wxString commandType = command["command"].get<std::string>();
    wxString action      = command["action"].get<std::string>();

    // Initialize an empty json object for parameters
    json parameters = json::object();

    // Safely parse parameters if they exist
    if (command.contains("parameters")) {
        if (command["parameters"].is_object()) {
            parameters = command["parameters"];
        } else {
            PostErrorMessage("", "", "", "'parameters' is not a JSON object");
            return false;
        }
    }

    PrintagoCommand printagoCommand;
    printagoCommand.SetCommandType(commandType);
    printagoCommand.SetAction(action);
    printagoCommand.SetParameters(parameters);   
    printagoCommand.SetOriginalCommand(command); 

    if (!ValidatePrintagoCommand(printagoCommand)) {
        return false;
    } else {
        ProcessPrintagoCommand(printagoCommand); 
    }

    return true;
}

bool PrintagoDirector::ValidatePrintagoCommand(const PrintagoCommand& cmd)
{
    wxString commandType = cmd.GetCommandType();
    wxString action      = cmd.GetAction();

    if (!server->get_session()->get_authorized() && !(commandType == "meta" && action == "init")) {
        PostErrorMessage("", "", cmd.GetOriginalCommand(), "Unauthorized");
        return false;
    }

    std::map<std::string, std::set<std::string>> validCommands = {{"meta", {"init"}},
                                                                  {"status", {"get_machine_list", "get_config", "switch_active"}},
                                                                  {"printer_control",
                                                                   {"pause_print", "resume_print", "stop_print", "get_status",
                                                                    "start_print_bbl"}},
                                                                  {"temperature_control", {"set_hotend", "set_bed"}},
                                                                  {"movement_control", {"jog", "home", "extrude"}}};

    auto commandIter = validCommands.find(commandType.ToStdString());
    if (commandIter != validCommands.end() && commandIter->second.find(action.ToStdString()) != commandIter->second.end()) {
        return true; 
    }

    PostErrorMessage("", "", cmd.GetOriginalCommand(), "Invalid Printago command");
    return false;
}

json PrintagoDirector::GetAllStatus()
{
    json statusObject = json::object();
    json machineList  = json::array();

    if (!wxGetApp().getDeviceManager())
        return json::object();

    AddCurrentProcessJsonTo(statusObject);

    const std::map<std::string, MachineObject*> machineMap = wxGetApp().getDeviceManager()->get_my_machine_list();

    for (auto& pair : machineMap) {
        machineList.push_back(MachineObjectToJson(pair.second));
    }
    statusObject["machines"] = machineList;
    return statusObject;
}

json PrintagoDirector::GetCompatOtherConfigsNames(Preset::Type preset_type, const Preset& printerPreset)
{
    PresetCollection* collection = nullptr;
    std::string       configType;

    if (preset_type == Preset::TYPE_FILAMENT) {
        collection = &wxGetApp().preset_bundle->filaments;
        configType = "filament";
    } else if (preset_type == Preset::TYPE_PRINT) {
        collection = &wxGetApp().preset_bundle->prints;
        configType = "print";
    } else {
        json noresult;
        return noresult;
    }

    json result;
    // Iterate through each preset
    for (Preset preset : collection->get_presets()) {
        if (!preset.is_visible)
            continue; // Skip hidden presets

        // Variable to hold the source of the preset
        wxString source = "";

        // Determine the source of the preset
        if (preset.is_system) {
            source = "system";
        } else if (preset.is_project_embedded) {
            source = "project";
        } else if (preset.is_user()) {
            source = "user";
        } else {
            continue; // Skip this preset
        }

        auto presetWithVendor = collection->get_preset_with_vendor_profile(preset);
        if (IsConfigCompatWithPrinter(presetWithVendor, printerPreset)) {
            result[configType][source.ToStdString()].push_back(preset.name);
        }
    }

    return result;
}

bool PrintagoDirector::IsConfigCompatWithPrinter(const PresetWithVendorProfile& preset, const Preset& printerPreset)
{
    auto active_printer = wxGetApp().preset_bundle->printers.get_preset_with_vendor_profile(printerPreset);

    DynamicPrintConfig extra_config;
    extra_config.set_key_value("printer_preset", new ConfigOptionString(printerPreset.name));

    const ConfigOption* opt = printerPreset.config.option("nozzle_diameter");
    if (opt)
        extra_config.set_key_value("num_extruders", new ConfigOptionInt((int) static_cast<const ConfigOptionFloats*>(opt)->values.size()));

    if (preset.vendor != nullptr && preset.vendor != active_printer.vendor)
        // The current profile has a vendor assigned and it is different from the active print's vendor.
        return false;
    auto& condition               = preset.preset.compatible_printers_condition();
    auto* compatible_printers     = dynamic_cast<const ConfigOptionStrings*>(preset.preset.config.option("compatible_printers"));
    bool  has_compatible_printers = compatible_printers != nullptr && !compatible_printers->values.empty();
    if (!has_compatible_printers && !condition.empty()) {
        try {
            return PlaceholderParser::evaluate_boolean_expression(condition, active_printer.preset.config, &extra_config);
        } catch (const std::runtime_error& err) {
            // FIXME in case of an error, return "compatible with everything".
            printf("Preset::is_compatible_with_printer - parsing error of compatible_printers_condition %s:\n%s\n",
                   active_printer.preset.name.c_str(), err.what());
            return true;
        }
    }
    return preset.preset.is_default || active_printer.preset.name.empty() || !has_compatible_printers ||
           std::find(compatible_printers->values.begin(), compatible_printers->values.end(), active_printer.preset.name) !=
               compatible_printers->values.end()
           // BBS
           || (!active_printer.preset.is_system && IsConfigCompatWithParent(preset, active_printer));
}

bool PrintagoDirector::IsConfigCompatWithParent(const PresetWithVendorProfile& preset, const PresetWithVendorProfile& active_printer)
{
    auto* compatible_printers     = dynamic_cast<const ConfigOptionStrings*>(preset.preset.config.option("compatible_printers"));
    bool  has_compatible_printers = compatible_printers != nullptr && !compatible_printers->values.empty();
    // BBS_carryover: FIXME only check the parent now, but should check grand-parent as well.
    return has_compatible_printers && std::find(compatible_printers->values.begin(), compatible_printers->values.end(),
                                                active_printer.preset.inherits()) != compatible_printers->values.end();
}

bool PrintagoDirector::ProcessPrintagoCommand(const PrintagoCommand& cmd)
{
    wxString                commandType     = cmd.GetCommandType();
    wxString                action          = cmd.GetAction();
    json                    parameters      = cmd.GetParameters();
    json                    originalCommand = cmd.GetOriginalCommand();
    wxString                actionDetail;

    MachineObject* printer = {nullptr};

    std::string printerId = parameters.contains("printer_id") ? parameters["printer_id"].get<std::string>() : "Unspecified";
    bool        hasPrinterId = printerId.compare("Unspecified");

    if (!commandType.compare("meta")) {
        if (!action.compare("init")) {
            std::string token = parameters["token"];
            if (token.empty()) {
                PostErrorMessage("", "", cmd.GetOriginalCommand(), "Unauthorized: No Token");
                return false;
            }

            if (ValidateToken(token)) {
                server->get_session()->set_authorized(true);
                actionDetail = "Authorized";
            } else {
                server->get_session()->set_authorized(false);
                PostErrorMessage("", "", cmd.GetOriginalCommand(), "Unauthorized: Invalid Token");
                return false;
            }            
        }

        PostSuccessMessage(printerId, action, originalCommand, actionDetail);
        return true;
    }

    if (!commandType.compare("status")) {
        std::string username = wxGetApp().getAgent()->is_user_login() ? wxGetApp().getAgent()->get_user_name() : "nouser@bbl";
        if (!action.compare("get_machine_list")) {
            PostResponseMessage(username, GetAllStatus(), originalCommand);
        } else if (!action.compare("get_config")) {
            std::string config_type = parameters["config_type"];                    // printer, filament, print
            std::string config_name = Http::url_decode(parameters["config_name"]);  // name of the config
            json     configJson  = GetConfigByName(config_type, config_name);
            if (!configJson.empty()) {
                PostResponseMessage(username, configJson, originalCommand);
            } else {
                PostErrorMessage(username, action, originalCommand, "config not found; valid types are: print, printer, or filament");
                return false;
            }
        } else if (!action.compare("switch_active")) {
            if (!PBJob::CanProcessJob()) {
                PostErrorMessage("", action, originalCommand, "unable, UI blocked");
            }
            if (hasPrinterId) {
                if (!SwitchSelectedPrinter(printerId)) {
                    if (PBJob::CanProcessJob()) {
                        PostErrorMessage("", action, originalCommand, "unable, printer_id not found.");
                    } else {
                        PostErrorMessage("", action, originalCommand, "unable, UI blocked");
                    }
                } else {
                    actionDetail = wxString::Format("connecting to %s", printerId);
                    PostSuccessMessage(printerId, action, originalCommand, actionDetail);
                }
            } else {
                PostErrorMessage("", action, originalCommand, "no printer_id specified");
            }
        }
        return true;
    }

    if (!hasPrinterId) {
        PostErrorMessage("", action, originalCommand, "no printer_id specified");
        return false;
    }
    // Find the printer in the machine list
    auto machineList = wxGetApp().getDeviceManager()->get_my_machine_list();
    auto it          = std::find_if(machineList.begin(), machineList.end(),
                                    [&printerId](const std::pair<std::string, MachineObject*>& pair) { return pair.second->dev_id == printerId; });

    if (it != machineList.end()) {
        printer = it->second;
    } else {
        PostErrorMessage(printerId, action, originalCommand, "no printer not found with ID: " + printerId);
        return false;
    }

    // select the printer for updates in the monitor for updates.
    // this gets eventually queued in the UI thread.  Don't wait for it here if we dont need to.
    SwitchSelectedPrinter(printerId);

    if (!commandType.compare("printer_control")) {
        if (!action.compare("pause_print")) {
            try {
                if (!printer->can_pause()) {
                    PostErrorMessage(printerId, action, originalCommand, "cannot pause printer");
                    return false;
                }
                printer->command_task_pause();
            } catch (...) {
                PostErrorMessage(printerId, action, originalCommand, "an error occurred issuing pause_print");
                return false;
            }
        } else if (!action.compare("resume_print")) {
            try {
                if (!printer->can_resume()) {
                    PostErrorMessage(printerId, action, originalCommand, "cannot resume printer");
                    return false;
                }
                printer->command_task_resume();
            } catch (...) {
                PostErrorMessage(printerId, action, originalCommand, "an error occurred issuing resume_print");
                return false;
            }
        } else if (!action.compare("stop_print")) {
            try {
                if (!printer->can_abort()) {
                    PostErrorMessage(printerId, action, originalCommand, "cannot abort printer");
                    return false;
                }
                printer->command_task_abort();
            } catch (...) {
                PostErrorMessage(printerId, action, originalCommand, "an error occurred issuing stop_print");
                return false;
            }
        } else if (!action.compare("get_status")) {
            PostStatusMessage(printerId, GetMachineStatus(printer), originalCommand);
            return true;
        } else if (!action.compare("start_print_bbl")) {
            if (!printer->can_print() && PBJob::printerId.compare(printerId)) { // printer can print, and we're not already prepping for it.
                PostErrorMessage(printerId, action, originalCommand, "cannot start print");
                return false;
            }

            std::string printagoModelUrl = parameters["model"];
            std::string printerConfUrl   = parameters["printer_conf"];
            std::string printConfUrl     = parameters["print_conf"];
            std::string filamentConfUrl  = parameters["filament_conf"];

            wxString printagoId = parameters["printago_job"];
            if (!printagoId.empty()) {
                PBJob::jobId = printagoId;
            }

            PBJob::printerId = printerId;
            PBJob::command   = originalCommand;
            PBJob::localFile = "";

            PBJob::use_ams             = printer->has_ams() && parameters.contains("use_ams") && parameters["use_ams"].get<bool>();
            PBJob::bbl_do_bed_leveling = parameters.contains("do_bed_leveling") && parameters["do_bed_leveling"].get<bool>();
            PBJob::bbl_do_flow_cali    = parameters.contains("do_flow_cali") && parameters["do_flow_cali"].get<bool>();

            if (parameters.count("bed_type")) {
                PBJob::bed_type = PBJob::StringToBedType(parameters["bed_type"]);
            } else {
                PostErrorMessage(printerId, action, originalCommand, "missing bed_type (cool_plate, eng_plate, warm_plate, textured_pei)");
                return false;
            }

            if (!PBJob::CanProcessJob()) {
                PostErrorMessage(printerId, action, originalCommand, "busy with current job - check status");
                return false;
            }

            PBJob::BlockJobProcessing();

            if (printagoModelUrl.empty()) {
                PostErrorMessage(printerId, action, originalCommand, "no url specified");
                return false;
            } else if (printConfUrl.empty() || printerConfUrl.empty() || filamentConfUrl.empty()) {
                PostErrorMessage(printerId, action, originalCommand, "must specify printer, filament, and print configurations");
                return false;
            } else {
                printagoModelUrl = Http::url_decode(printagoModelUrl);
                printerConfUrl   = Http::url_decode(printerConfUrl);
                printConfUrl     = Http::url_decode(printConfUrl);
                filamentConfUrl  = Http::url_decode(filamentConfUrl);
            }

            PBJob::SetServerState(JobServerState::Download, true);

            // Second param is reference and modified inside SavePrintagoFile.
            if (SavePrintagoFile(printagoModelUrl, PBJob::localFile)) {
            } else {
                PostErrorMessage(printerId, wxString::Format("%s:%s", action, PBJob::serverStateStr()), originalCommand,
                                 "model download failed");
                return false;
            }

            // Do the configuring here: this allows 3MF files to load, then we can configure the slicer and override the 3MF conf settings
            // from what Printago sent.
            wxFileName localPrinterConf, localFilamentConf, localPrintConf;
            if (SavePrintagoFile(printerConfUrl, localPrinterConf) && SavePrintagoFile(filamentConfUrl, localFilamentConf) &&
                SavePrintagoFile(printConfUrl, localPrintConf)) {
            } else {
                PostErrorMessage(printerId, wxString::Format("%s:%s", action, PBJob::serverStateStr()), originalCommand,
                                 "config download failed");
                return false;
            }

            PBJob::configFiles["printer"]  = localPrinterConf;
            PBJob::configFiles["filament"] = localFilamentConf;
            PBJob::configFiles["print"]    = localPrintConf;

            PBJob::SetServerState(JobServerState::Configure, true);

            std::promise<bool> uiLoadFilesPromise;
            std::future<bool>  uiLoadFilesFuture = uiLoadFilesPromise.get_future();

            wxGetApp().CallAfter([&]() {
                wxGetApp().mainframe->select_tab(1);
                wxGetApp().plater()->reset();

                actionDetail = wxString::Format("slice_config: %s", PBJob::localFile.GetFullPath());

                ImportPrintagoConfigs();
                SetPrintagoConfigs();
                wxGetApp().plater()->sidebar().on_bed_type_change(PBJob::bed_type);

                try {
                    if (!PBJob::localFile.GetExt().MakeUpper().compare("3MF")) {
                        // The last 'true' tells the function to not ask the user to confirm the load; save any existing work.
                        wxGetApp().plater()->load_project(PBJob::localFile.GetFullPath(), "-", true);
                        SetPrintagoConfigs(); // since the 3MF may have it's own configs that get set on load.
                    } else {
                        std::vector<std::string> filePathArray;
                        filePathArray.push_back(PBJob::localFile.GetFullPath().ToStdString());
                        LoadStrategy strategy = LoadStrategy::LoadModel |
                                                LoadStrategy::Silence; // LoadStrategy::LoadConfig | LoadStrategy::LoadAuxiliary
                        wxGetApp().plater()->load_files(filePathArray, strategy, false);
                    }
                    wxGetApp().plater()->select_plate(0, true);
                    uiLoadFilesPromise.set_value(true);
                } catch (...) {
                    uiLoadFilesPromise.set_value(false);
                }
            });

            bool uiLoadFilesSuccess = uiLoadFilesFuture.get();
            if (uiLoadFilesSuccess) {
                PBJob::SetServerState(JobServerState::Slicing, true);
                wxGetApp().plater()->reslice();    
            } else {
                PostErrorMessage(PBJob::printerId, wxString::Format("%s:%s", "start_print_bbl", PBJob::serverStateStr()), PBJob::command,
                                 "and error occurred loading the model and config");
                return false;
            }
            
            actionDetail = wxString::Format("slice_start: %s", PBJob::localFile.GetFullPath());
        }
    } else if (!commandType.compare("temperature_control")) {
        if (!printer->can_print() && PBJob::printerId.compare(printerId)) {
            PostErrorMessage(printerId, action, originalCommand, "cannot control temperature; printer busy");
            return false;
        }

        if (!parameters.contains("temperature") || !parameters["temperature"].is_number_integer()) {
            PostErrorMessage(printerId, action, originalCommand, "invalid temperature value");
            return false;
        }
        int targetTemp = parameters["temperature"].get<int>();

        if (!action.compare("set_hotend")) {
            try {
                printer->command_set_nozzle(targetTemp);
                actionDetail = wxString::Format("%d", targetTemp);
            } catch (...) {
                PostErrorMessage(printerId, action, originalCommand, "an error occurred setting nozzle temperature");
                return false;
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
                PostErrorMessage(printerId, action, originalCommand, "an error occurred setting bed temperature");
                return false;
            }
        }
    } else if (!commandType.compare("movement_control")) {
        if (!printer->can_print() && PBJob::printerId.compare(printerId)) {
            PostErrorMessage(printerId, action, originalCommand, "cannot control movement; printer busy");
            return false;
        }
        if (!action.compare("jog")) {
            if (!parameters.contains("axes") || !parameters["axes"].is_object()) {
                PostErrorMessage(printerId, action, originalCommand, "no axes specified");
                return false;
            }

            auto axes = parameters["axes"];

            if (!printer->is_axis_at_home("X") || !printer->is_axis_at_home("Y") || !printer->is_axis_at_home("Z")) {
                PostErrorMessage(printerId, action, originalCommand, "must home axes before moving");
                return false;
            }

            // Validate axes and values
            for (auto& [axisName, axisValue] : axes.items()) {
                wxString axisNameUpper(axisName);
                axisNameUpper.MakeUpper();

                if (axisNameUpper != "X" && axisNameUpper != "Y" && axisNameUpper != "Z") {
                    PostErrorMessage(printerId, action, originalCommand, "invalid axis name: " + axisNameUpper);
                    return false;
                }

                if (!axisValue.is_number()) {
                    PostErrorMessage(printerId, action, originalCommand, "invalid value for axis " + axisNameUpper);
                    return false;
                }
                double value = axisValue.get<double>(); 
                try {
                 
                    printer->command_axis_control(axisNameUpper.ToStdString(), 1.0, value, 3000);
                } catch (...) {
                    PostErrorMessage(printerId, action, originalCommand, "an error occurred moving axis " + axisNameUpper);
                    return false;
                }
            }
        } else if (!action.compare("home")) {
            try {
                printer->command_go_home();
            } catch (...) {
                PostErrorMessage(printerId, action, originalCommand, "an error occurred homing axes");
                return false;
            }

        } else if (!action.compare("extrude")) {

            if (!parameters.contains("amount") || !parameters["amount"].is_number_integer()) {
                PostErrorMessage(printerId, action, originalCommand, "invalid extrude amount value");
                return false;
            }
            long extrudeAmt = parameters["amount"].get<long>();

            if (printer->nozzle_temp >= PRINTAGO_TEMP_THRESHOLD_ALLOW_E_CTRL) {
                try {
                    printer->command_axis_control("E", 1.0, extrudeAmt, 900);
                    actionDetail = wxString::Format("%d", extrudeAmt);
                } catch (...) {
                    PostErrorMessage(printerId, action, originalCommand, "an error occurred extruding filament");
                    return false;
                }
            } else {
                PostErrorMessage(printerId, action, originalCommand,
                                 wxString::Format("nozzle temperature too low to extrude (min: %.1f)",
                                                  PRINTAGO_TEMP_THRESHOLD_ALLOW_E_CTRL));
                return false;
            }
        }
    }

    // only send this response if it's *not* a start_print_bbl command. (it has it's own response)
    if (action.compare("start_print_bbl")) {
        PostSuccessMessage(printerId, action, originalCommand, actionDetail);
    }
    return true;
}

//needs UI thread.
void PrintagoDirector::ImportPrintagoConfigs()
{
    std::vector<std::string> cfiles;
    cfiles.push_back(into_u8(PBJob::configFiles["printer"].GetFullPath()));
    cfiles.push_back(into_u8(PBJob::configFiles["filament"].GetFullPath()));
    cfiles.push_back(into_u8(PBJob::configFiles["print"].GetFullPath()));

    wxGetApp().preset_bundle->import_presets(
        cfiles, [this](std::string const& name) { return wxID_YESTOALL; }, ForwardCompatibilitySubstitutionRule::Enable);
    if (!cfiles.empty()) {
        // reloads the UI presets in the dropdowns.
        wxGetApp().load_current_presets(true);
    }
}

//needs UI thread.
void PrintagoDirector::SetPrintagoConfigs()
{
    std::string printerProfileName  = GetConfigNameFromJsonFile(PBJob::configFiles["printer"].GetFullPath());
    std::string filamentProfileName = GetConfigNameFromJsonFile(PBJob::configFiles["filament"].GetFullPath());
    std::string printProfileName    = GetConfigNameFromJsonFile(PBJob::configFiles["print"].GetFullPath());

    wxGetApp().get_tab(Preset::TYPE_PRINTER)->select_preset(printerProfileName);
    wxGetApp().get_tab(Preset::TYPE_PRINT)->select_preset(printProfileName);

    int numFilaments = wxGetApp().filaments_cnt();
    for (int i = 0; i < numFilaments; ++i) {
        wxGetApp().preset_bundle->set_filament_preset(i, filamentProfileName);
        wxGetApp().plater()->sidebar().combos_filament()[i]->update();
    }
}

std::string PrintagoDirector::GetConfigNameFromJsonFile(const wxString& FilePath)
{
    std::ifstream file(FilePath.ToStdString());
    if (!file.is_open()) {
        return "";
    }
    json j;
    file >> j;
    file.close();
    return j.at("name").get<std::string>();
}

void PrintagoDirector::AddCurrentProcessJsonTo(json& statusObject)
{
    statusObject["process"]["can_process_job"] = PBJob::CanProcessJob();
    statusObject["process"]["job_id"]          = PBJob::jobId.ToStdString();
    statusObject["process"]["job_state"]       = PBJob::serverStateStr();
    statusObject["process"]["job_machine"]     = PBJob::printerId.ToStdString();
    statusObject["process"]["job_local_file"]  = PBJob::localFile.GetFullPath().ToStdString();
    statusObject["process"]["job_progress"]    = PBJob::progress;

    statusObject["process"]["bed_level"]   = PBJob::bbl_do_bed_leveling;
    statusObject["process"]["flow_calibr"] = PBJob::bbl_do_flow_cali;
    statusObject["process"]["bed_type"]    = PBJob::bed_type;
    statusObject["process"]["use_ams"]     = PBJob::use_ams;

    statusObject["software"]["is_dark_mode"] = wxGetApp().dark_mode();
}

json PrintagoDirector::GetMachineStatus(MachineObject* machine)
{
    json statusObject = json::object();
    json machineList  = json::array();

    if (!machine)
        return json::object();

    AddCurrentProcessJsonTo(statusObject);

    machineList.push_back(MachineObjectToJson(machine));
    statusObject["machines"] = machineList;
    return statusObject;
}

json PrintagoDirector::GetMachineStatus(const wxString& printerId)
{
    if (!wxGetApp().getDeviceManager())
        return json::object();
    return GetMachineStatus(wxGetApp().getDeviceManager()->get_my_machine(printerId.ToStdString()));
}

json PrintagoDirector::MachineObjectToJson(MachineObject* machine)
{
    json j;
    if (machine) {
        j["hardware"]["dev_model"]        = machine->printer_type;
        j["hardware"]["dev_display_name"] = machine->get_printer_type_display_str().ToStdString();
        j["hardware"]["dev_name"]         = machine->dev_name;
        j["hardware"]["nozzle_diameter"]  = machine->nozzle_diameter;

        j["hardware"]["compat_printer_profiles"] = GetCompatPrinterConfigNames(
            machine->get_preset_printer_model_name(machine->printer_type));

        j["connection_info"]["dev_ip"]              = machine->dev_ip;
        j["connection_info"]["dev_id"]              = machine->dev_id;
        j["connection_info"]["dev_name"]            = machine->dev_name;
        j["connection_info"]["dev_connection_type"] = machine->dev_connection_type;
        j["connection_info"]["is_local"]            = machine->is_local();
        j["connection_info"]["is_connected"]        = machine->is_connected();
        j["connection_info"]["is_connecting"]       = machine->is_connecting();
        j["connection_info"]["is_online"]           = machine->is_online();
        j["connection_info"]["has_access_right"]    = machine->has_access_right();
        j["connection_info"]["ftp_folder"]          = machine->get_ftp_folder();
        j["connection_info"]["access_code"]         = machine->get_access_code();

        // MachineObject State Info
        j["state"]["can_print"]                  = machine->can_print();
        j["state"]["can_resume"]                 = machine->can_resume();
        j["state"]["can_pause"]                  = machine->can_pause();
        j["state"]["can_abort"]                  = machine->can_abort();
        j["state"]["is_in_printing"]             = machine->is_in_printing();
        j["state"]["is_in_prepare"]              = machine->is_in_prepare();
        j["state"]["is_printing_finished"]       = machine->is_printing_finished();
        j["state"]["is_in_extrusion_cali"]       = machine->is_in_extrusion_cali();
        j["state"]["is_extrusion_cali_finished"] = machine->is_extrusion_cali_finished();

        // Current Job/Print Info
        j["current"]["print_status"]    = machine->print_status;
        j["current"]["print_time_left"] = machine->mc_left_time;
        j["current"]["print_percent"]   = machine->mc_print_percent;
        j["current"]["print_stage"]     = machine->mc_print_stage;
        j["current"]["print_sub_stage"] = machine->mc_print_sub_stage;
        j["current"]["curr_layer"]      = machine->curr_layer;
        j["current"]["total_layers"]    = machine->total_layers;

        // Temperatures
        j["current"]["temperatures"]["nozzle_temp"]         = static_cast<int>(std::round(machine->nozzle_temp));
        j["current"]["temperatures"]["nozzle_temp_target"]  = static_cast<int>(std::round(machine->nozzle_temp_target));
        j["current"]["temperatures"]["bed_temp"]            = static_cast<int>(std::round(machine->bed_temp));
        j["current"]["temperatures"]["bed_temp_target"]     = static_cast<int>(std::round(machine->bed_temp_target));
        j["current"]["temperatures"]["chamber_temp"]        = static_cast<int>(std::round(machine->chamber_temp));
        j["current"]["temperatures"]["chamber_temp_target"] = static_cast<int>(std::round(machine->chamber_temp_target));
        j["current"]["temperatures"]["frame_temp"]          = static_cast<int>(std::round(machine->frame_temp));

        // Cooling
        j["current"]["cooling"]["heatbreak_fan_speed"] = machine->heatbreak_fan_speed;
        j["current"]["cooling"]["cooling_fan_speed"]   = machine->cooling_fan_speed;
        j["current"]["cooling"]["big_fan1_speed"]      = machine->big_fan1_speed;
        j["current"]["cooling"]["big_fan2_speed"]      = machine->big_fan2_speed;
        j["current"]["cooling"]["fan_gear"]            = machine->fan_gear;
    }

    return j;
}

json PrintagoDirector::GetCompatPrinterConfigNames(std::string printer_type)
{
    // Create a JSON object to hold arrays of config names for each source
    json result;

    // Iterate through each preset
    for (Preset preset : wxGetApp().preset_bundle->printers.get_presets()) {
        if (!preset.is_visible)
            continue; // Skip hidden presets

        // Variable to hold the source of the preset
        wxString source = "";

        // Determine the source of the preset
        if (preset.is_system) {
            source = "system";
        } else if (preset.is_project_embedded) {
            source = "project";
        } else if (preset.is_user()) {
            source = "user";
        } else {
            continue; // Skip this preset
        }

        // Check for matching printer models in vendor profiles
        for (const auto& vendor_profile : wxGetApp().preset_bundle->vendors) {
            for (const auto& vendor_model : vendor_profile.second.models) {
                if (vendor_model.name == preset.config.opt_string("printer_model") && vendor_model.name == printer_type) {
                    // If the JSON object doesn't have this source key yet, create it
                    if (result.find(source.ToStdString()) == result.end()) {
                        result[source.ToStdString()] = json::array();
                    }
                    // Add the preset name to the array corresponding to this source
                    result[source.ToStdString()].push_back(preset.name);
                }
            }
        }
    }

    return result;
}

json PrintagoDirector::GetConfigByName(wxString configType, wxString configName)
{
    json              result;
    PresetBundle*     presetBundle = wxGetApp().preset_bundle;
    PresetCollection* collection   = nullptr;

    if (configType == "print") {
        collection = &presetBundle->prints;
    } else if (configType == "filament") {
        collection = &presetBundle->filaments;
    } else if (configType == "printer") {
        collection = &presetBundle->printers;
    } else {
        json noresult;
        return noresult;
    }

    const auto preset = collection->find_preset(configName.ToStdString());

    json configJson = "";
    if (preset) {
        configJson = ConfigToJson(preset->config, preset->name, "", preset->version.to_string());
    } else { // preset not found
        json noresult;
        return noresult;
    }

    wxString source = "default";
    if (preset->is_system) {
        source = "system";
    } else if (preset->is_project_embedded) {
        source = "project";
    } else if (preset->is_user()) {
        source = "user";
    }

    std::string collectionName = "none";
    switch (collection->type()) {
    case Preset::TYPE_PRINT: collectionName = "print"; break;
    case Preset::TYPE_PRINTER: collectionName = "printer"; break;
    case Preset::TYPE_FILAMENT: collectionName = "filament"; break;
    default: break;
    }
    result["config_type"]    = collectionName;
    result["config_source"]  = source.ToStdString();
    result["config_name"]    = preset->name;
    result["config_content"] = configJson;

    if (collectionName == "printer") {
        result["compat_filament_profiles"] = GetCompatFilamentConfigNames(*preset);
        result["compat_print_profiles"]    = GetCompatPrintConfigNames(*preset);
    }

    return result;
}

json PrintagoDirector::ConfigToJson(const DynamicPrintConfig& config,
                                    const std::string&        name,
                                    const std::string&        from,
                                    const std::string&        version,
                                    const std::string         is_custom)
{
    json j;
    // record the headers
    j[BBL_JSON_KEY_VERSION] = version;
    j[BBL_JSON_KEY_NAME]    = name;
    j[BBL_JSON_KEY_FROM]    = from;
    if (!is_custom.empty())
        j[BBL_JSON_KEY_IS_CUSTOM] = is_custom;

    // record all the key-values
    for (const std::string& opt_key : config.keys()) {
        const ConfigOption* opt = config.option(opt_key);
        if (opt->is_scalar()) {
            if (opt->type() == coString && (opt_key != "bed_custom_texture" && opt_key != "bed_custom_model"))
                // keep \n, \r, \t
                j[opt_key] = (dynamic_cast<const ConfigOptionString*>(opt))->value;
            else
                j[opt_key] = opt->serialize();
        } else {
            const ConfigOptionVectorBase* vec           = static_cast<const ConfigOptionVectorBase*>(opt);
            std::vector<std::string>      string_values = vec->vserialize();
            json                          j_array(string_values);
            j[opt_key] = j_array;
        }
    }

    return j;
}

bool PrintagoDirector::SwitchSelectedPrinter(const wxString& printerId)
{
    // don't switch the UI if CanProcessJob() is false; we're slicing and stuff in the UI.
    if (!PBJob::CanProcessJob())
        return false;

    auto machine = wxGetApp().getDeviceManager()->get_my_machine(printerId.ToStdString());
    if (!machine)
        return false;

    try {
        //this function queues the update on UI thread.
        wxGetApp().mainframe->m_monitor->select_machine(printerId.ToStdString());
    } catch (...) {
        return false;
    }

    return true;
}

bool PrintagoDirector::SavePrintagoFile(const wxString url, wxFileName& localPath)
{
    wxURI    uri(url);
    wxString path = uri.GetPath();

    wxArrayString pathComponents = wxStringTokenize(path, "/");
    wxString      uriFileName;
    if (!pathComponents.IsEmpty()) {
        uriFileName = pathComponents.Last();
    } else {
        return false;
    }
    // Remove any query string from the filename
    size_t queryPos = uriFileName.find('?');
    if (queryPos != wxString::npos) {
        uriFileName = uriFileName.substr(0, queryPos);
    }
    // Construct the full path for the temporary file

    fs::path tempDir = (fs::temp_directory_path());
    tempDir /= PBJob::jobId.ToStdString();

    boost::system::error_code ec;
    if (!fs::create_directories(tempDir, ec)) {
        if (ec) {
            // there was an error creating the directory
        }
    }

    wxString wxTempDir(tempDir.string());

    wxFileName fullFileName(wxTempDir, uriFileName);

    if (DownloadFileFromURL(url, fullFileName)) {
        wxLogMessage("File downloaded to: %s", fullFileName.GetFullPath());
        localPath = fullFileName;
        return true;
    } else {
        localPath = "";
        return false;
    }
}

bool PrintagoDirector::DownloadFileFromURL(const wxString url, const wxFileName& localFilename)
{
    boost::filesystem::path target_path = fs::path(localFilename.GetFullPath().ToStdString());
    wxString                filename    = localFilename.GetFullName(); // just filename and extension
    bool                    cont        = true;
    bool                    download_ok = false;
    int                     retry_count = 0;
    int                     percent     = 1;
    const int               max_retries = 3;
    wxString                msg;

    /* prepare project and profile */
    boost::thread download_thread = Slic3r::create_thread(
        [&percent, &cont, &retry_count, max_retries, &msg, &target_path, &download_ok, url, &filename] {
            int          res = 0;
            unsigned int http_code;
            std::string  http_body;

            fs::path tmp_path = target_path;
            tmp_path += "." + std::to_string(get_current_pid()) + ".download";

            if (fs::exists(target_path))
                fs::remove(target_path);
            if (fs::exists(tmp_path))
                fs::remove(tmp_path);

            auto http = Http::get(url.ToStdString());

            while (cont && retry_count < max_retries) {
                retry_count++;
                http.on_progress([&percent, &cont, &msg](Http::Progress progress, bool& cancel) {
                        if (!cont)
                            cancel = true;
                        if (progress.dltotal != 0) {
                            percent = progress.dlnow * 100 / progress.dltotal;
                        }
                        msg = wxString::Format("Printago part file Downloaded %d%%", percent);
                    })
                    .on_error([&msg, &cont, &retry_count, max_retries](std::string body, std::string error, unsigned http_status) {
                        (void) body;
                        BOOST_LOG_TRIVIAL(error)
                            << boost::str(boost::format("Error getting: `%1%`: HTTP %2%, %3%") % body % http_status % error);

                        if (retry_count == max_retries) {
                            msg  = boost::str(boost::format("Error getting: `%1%`: HTTP %2%, %3%") % body % http_status % error);
                            cont = false;
                        }
                    })
                    .on_complete([&cont, &download_ok, tmp_path, target_path](std::string body, unsigned /* http_status */) {
                        fs::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
                        file.write(body.c_str(), body.size());
                        file.close();
                        fs::rename(tmp_path, target_path);
                        cont        = false;
                        download_ok = true;
                    })
                    .perform_sync();
            }
        });

    while (cont) {
        wxMilliSleep(50);
        if (download_ok)
            break;
    }

    if (download_thread.joinable())
        download_thread.join();

    return download_ok;
}

void PrintagoDirector::OnSlicingCompleted(SlicingProcessCompletedEvent::StatusType slicing_result)
{
    // in case we got here by mistake and there's nothing we're trying to process; return silently.
    if (PBJob::printerId.IsEmpty() ||  PBJob::CanProcessJob()) {
        PBJob::UnblockJobProcessing();
        return;
    }
    const wxString action = "start_print_bbl";
    wxString       actionDetail;



    if (slicing_result != SlicingProcessCompletedEvent::StatusType::Finished) {
        actionDetail = "slicing Unknown Error: " + PBJob::localFile.GetFullPath();
        if (slicing_result == SlicingProcessCompletedEvent::StatusType::Cancelled)
            actionDetail = "slicing cancelled: " + PBJob::localFile.GetFullPath();
        else if (slicing_result == SlicingProcessCompletedEvent::StatusType::Error)
            actionDetail = "slicing error: " + PBJob::localFile.GetFullPath();
        PostErrorMessage(PBJob::printerId, action, PBJob::command, actionDetail);
        return;
    }

    // Slicing Success -> Send to the Printer
    PBJob::SetServerState(JobServerState::Sending, true);

    actionDetail = wxString::Format("send_to_printer: %s", PBJob::localFile.GetFullName());

     if (!m_select_machine_dlg)
        m_select_machine_dlg = new SelectMachineDialog(wxGetApp().plater());
    
    m_select_machine_dlg->set_print_type(FROM_NORMAL);
     m_select_machine_dlg->prepare(0);

    m_select_machine_dlg->SetPrinter(PBJob::printerId.ToStdString());
    auto selectedPrinter = wxGetApp().getDeviceManager()->get_selected_machine();
    if (selectedPrinter->dev_id != PBJob::printerId.ToStdString() && !selectedPrinter->is_connected()) {
        wxGetApp().getDeviceManager()->set_selected_machine(PBJob::printerId.ToStdString(), false);
    }

    if (selectedPrinter->has_ams()) {
        m_select_machine_dlg->SetCheckboxOption("use_ams", PBJob::use_ams);
    } else {
        m_select_machine_dlg->SetCheckboxOption("use_ams", false);
    }
    m_select_machine_dlg->SetCheckboxOption("timelapse", false);
    m_select_machine_dlg->SetCheckboxOption("bed_leveling", PBJob::bbl_do_bed_leveling);
    m_select_machine_dlg->SetCheckboxOption("flow_cali", PBJob::bbl_do_flow_cali);

    wxGetApp().CallAfter([=] {
        wxCommandEvent btnEvt(wxGetApp().mainframe->GetId());
        m_select_machine_dlg->on_ok_btn(btnEvt);
    });
}

void PrintagoDirector::OnPrintJobSent(wxString printerId, bool success)
{
    // in case we got here by mistake and there's nothing we're trying to process; return silently.
    if (PBJob::printerId.IsEmpty() || PBJob::CanProcessJob()) {
        PBJob::UnblockJobProcessing();
        return;
    }
    if (!success) {
        PostErrorMessage(PBJob::printerId, "start_print_bbl", PBJob::command, "an error occurred sending the print job.");
        return;
    }

    PBJob::progress = 99;
    PostJobUpdateMessage();

    json command = PBJob::command; // put here before we clear it in Unblock.

    PBJob::UnblockJobProcessing(); // unblock before notifying the client of the success.

    PostSuccessMessage(PBJob::printerId, "start_print_bbl", PBJob::command, wxString::Format("print sent to: %s", printerId));
}

bool PrintagoDirector::ValidateToken(const std::string& token)
{
    std::string url     = "http://localhost:3000/api/slicer-tokens/" + token;
    bool        isValid = false;

    // Perform the HTTP GET request synchronously
    Slic3r::Http::get(url)
        .on_complete([&isValid](const std::string& body, unsigned /*status*/) {
            // Parse the response body to check for success
            try {
                auto response = nlohmann::json::parse(body);
                if (response.contains("success") && response["success"].get<bool>()) {
                    isValid = true;
                }
            } catch (const nlohmann::json::exception& e) {
                BOOST_LOG_TRIVIAL(error) << "JSON parsing error: " << e.what();
                isValid = false;
            }
        })
        .on_error([](const std::string& /*body*/, const std::string& /*error*/, unsigned /*status*/) { })
        .perform_sync(); 
    return isValid;
}

} // namespace Slic3r
