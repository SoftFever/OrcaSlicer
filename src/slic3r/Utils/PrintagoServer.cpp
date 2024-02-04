#include "PrintagoServer.hpp"
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <wx/tokenzr.h>
#include <wx/event.h>
#include <wx/url.h>

#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Tab.hpp"

namespace beef = boost::beast;
using namespace Slic3r::GUI;

namespace Slic3r {


#define PRINTAGO_TEMP_THRESHOLD_ALLOW_E_CTRL 170.0f // Minimum temperature to allow extrusion control (per StatusPanel.cpp)

void printago_ws_error(beef::error_code ec, char const* what) { BOOST_LOG_TRIVIAL(error) << what << ": " << ec.message(); }

//``````````````````````````````````````````````````
//------------------PrintagoSession------------------
//``````````````````````````````````````````````````
PrintagoSession::PrintagoSession(tcp::socket&& socket) : ws_(std::move(socket)) {}

void PrintagoSession::run() { on_run(); }

void PrintagoSession::on_run()
{
    // Set suggested timeout settings for the websocket
    // ... other setup ...
    ws_.async_accept([capture0 = shared_from_this()](auto&& PH1) { capture0->on_accept(std::forward<decltype(PH1)>(PH1)); });
}

void PrintagoSession::on_accept(beef::error_code ec)
{
    if (ec)
        return printago_ws_error(ec, "accept");

    do_read();
}

void PrintagoSession::do_read()
{
    ws_.async_read(buffer_, [capture0 = shared_from_this()](auto&& PH1, auto&& PH2) {
        capture0->on_read(std::forward<decltype(PH1)>(PH1), std::forward<decltype(PH2)>(PH2));
    });
}

void PrintagoSession::on_read(beef::error_code ec, std::size_t bytes_transferred)
{
    if (ec) {
        printago_ws_error(ec, "read");
    } else {
        ws_.text(ws_.got_text());
        const auto msg = beef::buffers_to_string(buffer_.data());
        wxGetApp().printago_director()->ParseCommand(msg);
        buffer_.consume(buffer_.size());
        do_read();
    }
}

void PrintagoSession::on_write(beef::error_code ec, std::size_t bytes_transferred)
{
    if (ec)
        return printago_ws_error(ec, "write");

    buffer_.consume(buffer_.size());
    do_read();
}

void PrintagoSession::async_send(const std::string& message)
{
    net::post(ws_.get_executor(), [self = shared_from_this(), message]() { self->do_write(message); });
}

void PrintagoSession::do_write(const std::string& message)
{
    ws_.async_write(net::buffer(message), [self = shared_from_this()](beef::error_code ec, std::size_t length) {
        if (ec) {
            printago_ws_error(ec, "write");
        }
    });
}

//``````````````````````````````````````````````````
//------------------PrintagoServer------------------
//``````````````````````````````````````````````````
PrintagoServer::PrintagoServer(net::io_context& ioc, tcp::endpoint endpoint) : ioc_(ioc), acceptor_(ioc), reconnection_delay_(1)
{
    beef::error_code ec;
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

void PrintagoServer::on_accept(beef::error_code ec, tcp::socket socket)
{
    if (ec) {
        printago_ws_error(ec, "accept");
        handle_reconnect();
    } else {
        reconnection_delay_ = 1; // Reset delay on successful connection
        auto session = std::make_shared<PrintagoSession>(std::move(socket));
        set_session(session);
        session->run();
        do_accept(); 
    }
}

void PrintagoServer::handle_reconnect()
{
    if (reconnection_delay_ < 120) {
        reconnection_delay_ *= 2; // Exponential back-off
    }
    auto timer = std::make_shared<net::steady_timer>(ioc_, std::chrono::seconds(reconnection_delay_));
    timer->async_wait([capture0 = shared_from_this(), timer](const beef::error_code&) { capture0->do_accept(); });
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

    // Clean up other resources
    delete m_select_machine_dlg;
}

void PrintagoDirector::PostErrorMessage(const wxString printer_id, const wxString localCommand, const wxString command, const wxString errorDetail)
{
    if (!PBJob::CanProcessJob()) {
        PBJob::UnblockJobProcessing();
    }

    json errorResponse;
    errorResponse["local_command"] = localCommand.ToStdString();
    errorResponse["error_detail"]  = errorDetail.ToStdString();
    errorResponse["success"]       = false;

    auto* resp = new PrintagoResponse();
    resp->SetMessageType("error");
    resp->SetPrinterId(printer_id);
    const wxURL url(command);
    wxString    path = url.GetPath();
    resp->SetCommand((!path.empty() && path[0] == '/') ? path.Remove(0, 1).ToStdString() : path.ToStdString());
    resp->SetData(errorResponse);

    _PostResponse(*resp);
    
}

void PrintagoDirector::PostJobUpdateMessage()
{
    json responseData;
    auto* resp = new PrintagoResponse();
    resp->SetMessageType("status");
    resp->SetPrinterId(PBJob::printerId);
    resp->SetCommand("job_update");
    AddCurrentProcessJsonTo(responseData);
    resp->SetData(responseData);

    _PostResponse(*resp);
}

void PrintagoDirector::PostResponseMessage(const wxString printer_id, const json responseData, const wxString command)
{
    auto* resp = new PrintagoResponse();
    resp->SetMessageType("status");
    resp->SetPrinterId(printer_id);
    const wxURL url(command);
    resp->SetCommand(url.GetPath().ToStdString());
    resp->SetData(responseData);

    _PostResponse(*resp);
}

void PrintagoDirector::PostSuccessMessage(const wxString printer_id,
                                          const wxString localCommand,
                                          const wxString command,
                                          const wxString localCommandDetail)
{
    json responseData;
    responseData["local_command"]        = localCommand.ToStdString();
    responseData["local_command_detail"] = localCommandDetail.ToStdString();
    responseData["success"]              = true;

    auto* resp = new PrintagoResponse();
    resp->SetMessageType("success");
    resp->SetPrinterId(printer_id);
    const wxURL url(command);
    wxString    path = url.GetPath();
    resp->SetCommand((!path.empty() && path[0] == '/') ? path.Remove(0, 1).ToStdString() : path.ToStdString());
    resp->SetData(responseData);

    _PostResponse(*resp);
}

void PrintagoDirector::PostStatusMessage(const wxString printer_id, const json statusData, const wxString command)
{
    auto* resp = new PrintagoResponse(); // PrintagoMessageEvent(PRINTAGO_SEND_WEBVIEW_MESSAGE_EVENT);
    resp->SetMessageType("status");
    resp->SetPrinterId(printer_id);
    const wxURL url(command);
    wxString path = url.GetPath();
    resp->SetCommand((!path.empty() && path[0] == '/') ? path.Remove(0, 1).ToStdString() : path.ToStdString());
    resp->SetData(statusData);

    _PostResponse(*resp);
}

void PrintagoDirector::_PostResponse(const PrintagoResponse response)
{
    wxDateTime now = wxDateTime::Now();
    now.MakeUTC();
    const wxString timestamp = now.FormatISOCombined() + "Z";

    json message;
    message["type"]        = response.GetMessageType().ToStdString();
    message["timestamp"]   = timestamp.ToStdString();
    message["printer_id"]  = response.GetPrinterId().ToStdString();
    message["client_type"] = "bambu";
    message["command"]     = response.GetCommand().ToStdString();
    message["data"]        = response.GetData();

    const std::string messageStr = message.dump();

    auto session = server->get_session();
    if (session) {
        session->async_send(messageStr);
    }
}

bool PrintagoDirector::ParseCommand(const std::string& command)
{
    const wxString command_str(command);
    if (!command_str.StartsWith("printago://")) {
        return false;
    }

    wxURI         uri(command_str);
    wxString      path           = uri.GetPath();
    wxArrayString pathComponents = wxStringTokenize(path, "/");
    wxString      commandType, action;

    if (pathComponents.GetCount() != 3) {
        wxGetApp().printago_director()->PostErrorMessage("", "", "", "invalid printago command");
        return false;
    }

    commandType = pathComponents.Item(1); 
    action      = pathComponents.Item(2); 

    wxString                query      = uri.GetQuery();          
    wxStringToStringHashMap parameters = _ParseQueryString(query); 

    PrintagoCommand printagoCommand;
    printagoCommand.SetCommandType(commandType);
    printagoCommand.SetAction(action);
    printagoCommand.SetParameters(parameters);
    printagoCommand.SetOriginalCommandStr(command_str.ToStdString());

    if (!ValidatePrintagoCommand(printagoCommand)) {
        PostErrorMessage("", "", command_str, "invalid printago command");
        return false;
    } else {
        wxGetApp().CallAfter([=]() { ProcessPrintagoCommand(printagoCommand); });
    }

    return true;
}

wxStringToStringHashMap PrintagoDirector::_ParseQueryString(const wxString& queryString)
{
    wxStringToStringHashMap params;

    // Split the query string on '&' to get key-value pairs
    wxStringTokenizer tokenizer(queryString, "&");
    while (tokenizer.HasMoreTokens()) {
        wxString token = tokenizer.GetNextToken();

        // Split each key-value pair on '='
        wxString key   = token.BeforeFirst('=');
        wxString value = token.AfterFirst('=');

        // URL-decode the key and value
        wxString decodedKey   = wxURI::Unescape(key);
        wxString decodedValue = wxURI::Unescape(value);

        params[decodedKey] = decodedValue;
    }
    return params;
}

bool PrintagoDirector::ValidatePrintagoCommand(const PrintagoCommand& cmd)
{
    wxString commandType = cmd.GetCommandType();
    wxString action      = cmd.GetAction();

    // Map of valid command types to their corresponding valid actions
    std::map<std::string, std::set<std::string>> validCommands = {{"status", {"get_machine_list", "get_config", "switch_active"}},
                                                                  {"printer_control",
                                                                   {"pause_print", "resume_print", "stop_print", "get_status",
                                                                    "start_print_bbl"}},
                                                                  {"temperature_control", {"set_hotend", "set_bed"}},
                                                                  {"movement_control", {"jog", "home", "extrude"}}};

    auto commandIter = validCommands.find(commandType.ToStdString());
    if (commandIter != validCommands.end() && commandIter->second.find(action.ToStdString()) != commandIter->second.end()) {
        return true;
    }
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

json PrintagoDirector::GetCompatOtherConfigsNames(Preset::Type preset_type, const Preset &printerPreset)
{
    PresetCollection *collection = nullptr;
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

bool PrintagoDirector::IsConfigCompatWithPrinter(const PresetWithVendorProfile &preset, const Preset &printerPreset)
{
    auto active_printer = wxGetApp().preset_bundle->printers.get_preset_with_vendor_profile(printerPreset);

    DynamicPrintConfig extra_config;
    extra_config.set_key_value("printer_preset", new ConfigOptionString(printerPreset.name));
    const ConfigOption *opt = printerPreset.config.option("nozzle_diameter");
    if (opt)
        extra_config.set_key_value("num_extruders", new ConfigOptionInt((int) static_cast<const ConfigOptionFloats *>(opt)->values.size()));

    if (preset.vendor != nullptr && preset.vendor != active_printer.vendor)
		// The current profile has a vendor assigned and it is different from the active print's vendor.
		return false;
    auto &condition               = preset.preset.compatible_printers_condition();
    auto *compatible_printers     = dynamic_cast<const ConfigOptionStrings*>(preset.preset.config.option("compatible_printers"));
    bool  has_compatible_printers = compatible_printers != nullptr && ! compatible_printers->values.empty();
    if (! has_compatible_printers && ! condition.empty()) {
        try {
            return PlaceholderParser::evaluate_boolean_expression(condition, active_printer.preset.config, &extra_config);
        } catch (const std::runtime_error &err) {
            //FIXME in case of an error, return "compatible with everything".
            printf("Preset::is_compatible_with_printer - parsing error of compatible_printers_condition %s:\n%s\n",
            active_printer.preset.name.c_str(), err.what()); return true;
        }
    }
    return preset.preset.is_default || active_printer.preset.name.empty() || !has_compatible_printers ||
        std::find(compatible_printers->values.begin(), compatible_printers->values.end(), active_printer.preset.name) !=
        compatible_printers->values.end()
        //BBS
           || (!active_printer.preset.is_system && IsConfigCompatWithParent(preset, active_printer));
}

bool PrintagoDirector::IsConfigCompatWithParent(const PresetWithVendorProfile &preset, const PresetWithVendorProfile &active_printer)
{
    auto *compatible_printers     = dynamic_cast<const ConfigOptionStrings *>(preset.preset.config.option("compatible_printers"));
    bool  has_compatible_printers = compatible_printers != nullptr && !compatible_printers->values.empty();
    // BBS: FIXME only check the parent now, but should check grand-parent as well.
    return has_compatible_printers && std::find(compatible_printers->values.begin(), compatible_printers->values.end(),
                                                active_printer.preset.inherits()) != compatible_printers->values.end();
}

bool PrintagoDirector::ProcessPrintagoCommand(const PrintagoCommand& cmd)
{
    wxString                commandType        = cmd.GetCommandType();
    wxString                action             = cmd.GetAction();
    wxStringToStringHashMap parameters         = cmd.GetParameters();
    wxString                originalCommandStr = cmd.GetOriginalCommandStr();
    wxString                actionDetail;

    MachineObject* printer = {nullptr};

    wxString printerId    = parameters.count("printer_id") ? parameters["printer_id"] : "Unspecified";
    bool     hasPrinterId = printerId.compare("Unspecified");

    if (!commandType.compare("status")) {
        std::string username = wxGetApp().getAgent()->is_user_login() ? wxGetApp().getAgent()->get_user_name() : "nouser@bab";
        if (!action.compare("get_machine_list")) {
            PostResponseMessage(username, GetAllStatus(), originalCommandStr);
        } else if (!action.compare("get_config")) {
            wxString config_type = parameters["config_type"];                                 // printer, filament, print
            wxString config_name = Http::url_decode(parameters["config_name"].ToStdString()); // name of the config
            json     configJson  = GetConfigByName(config_type, config_name);
            if (!configJson.empty()) {
                PostResponseMessage(username, configJson, originalCommandStr);
            } else {
                PostErrorMessage(username, action, originalCommandStr, "config not found; valid types are: print, printer, or filament");
                return false;
            }
        } else if (!action.compare("switch_active")) {
            if (!PBJob::CanProcessJob()) {
                PostErrorMessage("", action, originalCommandStr, "unable, UI blocked");
            }
            if (hasPrinterId) {
                if (!SwitchSelectedPrinter(printerId)) {
                    PostErrorMessage("", action, originalCommandStr, "unable, unknown");
                } else {
                    actionDetail = wxString::Format("connecting to %s", printerId);
                    PostSuccessMessage(printerId, action, originalCommandStr, actionDetail);
                }
            } else {
                PostErrorMessage("", action, originalCommandStr, "no printer_id specified");
            }
        }
        return true;
    }

    if (!hasPrinterId) {
        PostErrorMessage("", action, originalCommandStr, "no printer_id specified");
        return false;
    }
    // Find the printer in the machine list
    auto machineList = wxGetApp().getDeviceManager()->get_my_machine_list();
    auto it          = std::find_if(machineList.begin(), machineList.end(),
                                    [&printerId](const std::pair<std::string, MachineObject*>& pair) { return pair.second->dev_id == printerId; });

    if (it != machineList.end()) {
        printer = it->second;
    } else {
        PostErrorMessage(printerId, action, originalCommandStr, "no printer not found with ID: " + printerId);
        return false;
    }

    // select the printer for updates in the monitor for updates.
    SwitchSelectedPrinter(printerId);

    if (!commandType.compare("printer_control")) {
        if (!action.compare("pause_print")) {
            try {
                if (!printer->can_pause()) {
                    PostErrorMessage(printerId, action, originalCommandStr, "cannot pause printer");
                    return false;
                }
                printer->command_task_pause();
            } catch (...) {
                PostErrorMessage(printerId, action, originalCommandStr, "an error occurred issuing pause_print");
                return false;
            }
        } else if (!action.compare("resume_print")) {
            try {
                if (!printer->can_resume()) {
                    PostErrorMessage(printerId, action, originalCommandStr, "cannot resume printer");
                    return false;
                }
                printer->command_task_resume();
            } catch (...) {
                PostErrorMessage(printerId, action, originalCommandStr, "an error occurred issuing resume_print");
                return false;
            }
        } else if (!action.compare("stop_print")) {
            try {
                if (!printer->can_abort()) {
                    PostErrorMessage(printerId, action, originalCommandStr, "cannot abort printer");
                    return false;
                }
                printer->command_task_abort();
            } catch (...) {
                PostErrorMessage(printerId, action, originalCommandStr, "an error occurred issuing stop_print");
                return false;
            }
        } else if (!action.compare("get_status")) {
            PostStatusMessage(printerId, GetMachineStatus(printer), originalCommandStr);
            return true;
        } else if (!action.compare("start_print_bbl")) {
            if (!printer->can_print() && PBJob::printerId.compare(printerId)) { // printer can print, and we're not already prepping for it.
                PostErrorMessage(printerId, action, originalCommandStr, "cannot start print");
                return false;
            }

            wxString printagoModelUrl = parameters["model"];
            wxString printerConfUrl   = parameters["printer_conf"];
            wxString printConfUrl     = parameters["print_conf"];
            wxString filamentConfUrl  = parameters["filament_conf"];

            wxString printagoId = parameters["printago_job"];
            if (!printagoId.empty()) {
                PBJob::jobId = printagoId;
            }
            
            PBJob::printerId = printerId;
            PBJob::command   = originalCommandStr;
            PBJob::localFile = "";

            PBJob::use_ams = printer->has_ams() && parameters.count("use_ams") && parameters["use_ams"].ToStdString() == "true";
            PBJob::bbl_do_bed_leveling = parameters.count("do_bed_leveling") && parameters["do_bed_leveling"].ToStdString() == "true";
            PBJob::bbl_do_flow_cali = parameters.count("do_flow_cali") && parameters["do_flow_cali"].ToStdString() == "true";
            
            if (parameters.count("bed_type")) {
                PBJob::bed_type = PBJob::StringToBedType(parameters["bed_type"].ToStdString());   
            } else {
                PostErrorMessage(printerId, action, originalCommandStr, "missing bed_type (cool_plate, eng_plate, warm_plate, textured_pei)");
                return false;
            }

            if (!m_select_machine_dlg)
                m_select_machine_dlg = new SelectMachineDialog(wxGetApp().plater());

            if (!PBJob::CanProcessJob()) {
                PostErrorMessage(printerId, action, originalCommandStr, "busy with current job - check status");
                return false;
            }

            PBJob::BlockJobProcessing();

            if (printagoModelUrl.empty()) {
                PostErrorMessage(printerId, action, originalCommandStr, "no url specified");
                return false;
            } else if (printConfUrl.empty() || printerConfUrl.empty() || filamentConfUrl.empty()) {
                PostErrorMessage(printerId, action, originalCommandStr, "must specify printer, filament, and print configurations");
                return false;
            } else {
                printagoModelUrl = Http::url_decode(printagoModelUrl.ToStdString());
                printerConfUrl   = Http::url_decode(printerConfUrl.ToStdString());
                printConfUrl     = Http::url_decode(printConfUrl.ToStdString());
                filamentConfUrl  = Http::url_decode(filamentConfUrl.ToStdString());
            }

            PBJob::SetServerState(JobServerState::Download, true);

            // Second param is reference and modified inside SavePrintagoFile.
            if (SavePrintagoFile(printagoModelUrl, PBJob::localFile)) {
            } else {
                PostErrorMessage(printerId, wxString::Format("%s:%s", action, PBJob::serverStateStr()), originalCommandStr,
                                    "model download failed");
                return false;
            }

            // Do the configuring here: this allows 3MF files to load, then we can configure the slicer and override the 3MF conf settings
            // from what Printago sent.
            wxFileName localPrinterConf, localFilamentConf, localPrintConf;
            if (SavePrintagoFile(printerConfUrl, localPrinterConf) && SavePrintagoFile(filamentConfUrl, localFilamentConf) &&
                SavePrintagoFile(printConfUrl, localPrintConf)) {
            } else {
                PostErrorMessage(printerId, wxString::Format("%s:%s", action, PBJob::serverStateStr()), originalCommandStr,
                                    "config download failed");
                return false;
            }

            PBJob::configFiles["printer"] = localPrinterConf;
            PBJob::configFiles["filament"] = localFilamentConf;
            PBJob::configFiles["print"]    = localPrintConf;

            PBJob::SetServerState(JobServerState::Configure, true);

            wxGetApp().mainframe->select_tab(1);
            wxGetApp().plater()->reset();

            actionDetail = wxString::Format("slice_config: %s", PBJob::localFile.GetFullPath());

            // Loads the configs into the UI, if able; selects them in the dropdowns.
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
            } catch (...) {
                PostErrorMessage(PBJob::printerId, wxString::Format("%s:%s", "start_print_bbl", PBJob::serverStateStr()), PBJob::command,
                                    "and error occurred loading the model and config");
                return false;
            }

            PBJob::SetServerState(JobServerState::Slicing, true);
            
            wxGetApp().plater()->select_plate(0, true);
            wxGetApp().plater()->reslice();
            actionDetail = wxString::Format("slice_start: %s", PBJob::localFile.GetFullPath());
        }
    } else if (!commandType.compare("temperature_control")) {
        if (!printer->can_print() && PBJob::printerId.compare(printerId)) {
            PostErrorMessage(printerId, action, originalCommandStr, "cannot control temperature; printer busy");
            return false;
        }
        wxString tempStr = parameters["temperature"];
        long     targetTemp;
        if (!tempStr.ToLong(&targetTemp)) {
            PostErrorMessage(printerId, action, originalCommandStr, "invalid temperature value");
            return false;
        }

        if (!action.compare("set_hotend")) {
            try {
                printer->command_set_nozzle(targetTemp);
                actionDetail = wxString::Format("%d", targetTemp);
            } catch (...) {
                PostErrorMessage(printerId, action, originalCommandStr, "an error occurred setting nozzle temperature");
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
                PostErrorMessage(printerId, action, originalCommandStr, "an error occurred setting bed temperature");
                return false;
            }
        }
    } else if (!commandType.compare("movement_control")) {
        if (!printer->can_print() && PBJob::printerId.compare(printerId)) {
            PostErrorMessage(printerId, action, originalCommandStr, "cannot control movement; printer busy");
            return false;
        }
        if (!action.compare("jog")) {
            auto axes = ExtractPrefixedParams(parameters, "axes");
            if (axes.empty()) {
                PostErrorMessage(printerId, action, originalCommandStr, "no axes specified");
                return false;
            }

            if (!printer->is_axis_at_home("X") || !printer->is_axis_at_home("Y") || !printer->is_axis_at_home("Z")) {
                PostErrorMessage(printerId, action, originalCommandStr, "must home axes before moving");
                return false;
            }
            // Iterate through each axis and its value; we do this loop twice to ensure the input in clean.
            // this ensures we do not move the head unless all input moves are valid.
            for (const auto& axis : axes) {
                wxString axisName = axis.first;
                axisName.MakeUpper();
                if (axisName != "X" && axisName != "Y" && axisName != "Z") {
                    PostErrorMessage(printerId, action, originalCommandStr, "invalid axis name: " + axisName);
                    return false;
                }
                wxString axisValueStr = axis.second;
                double   axisValue;
                if (!axisValueStr.ToDouble(&axisValue)) {
                    PostErrorMessage(printerId, action, originalCommandStr, "invalid value for axis " + axisName);
                    return false;
                }
            }

            for (const auto& axis : axes) {
                wxString axisName = axis.first;
                axisName.MakeUpper();
                wxString axisValueStr = axis.second;
                double   axisValue;
                axisValueStr.ToDouble(&axisValue);
                try {
                    printer->command_axis_control(axisName.ToStdString(), 1.0, axisValue, 3000);
                } catch (...) {
                    PostErrorMessage(printerId, action, originalCommandStr, "an error occurred moving axis " + axisName);
                    return false;
                }
            }

        } else if (!action.compare("home")) {
            try {
                printer->command_go_home();
            } catch (...) {
                PostErrorMessage(printerId, action, originalCommandStr, "an error occurred homing axes");
                return false;
            }

        } else if (!action.compare("extrude")) {
            wxString amtStr = parameters["amount"];
            long     extrudeAmt;
            if (!amtStr.ToLong(&extrudeAmt)) {
                PostErrorMessage(printerId, action, originalCommandStr, "invalid extrude amount value");
                return false;
            }

            if (printer->nozzle_temp >= PRINTAGO_TEMP_THRESHOLD_ALLOW_E_CTRL) {
                try {
                    printer->command_axis_control("E", 1.0, extrudeAmt, 900);
                    actionDetail = wxString::Format("%d", extrudeAmt);
                } catch (...) {
                    PostErrorMessage(printerId, action, originalCommandStr, "an error occurred extruding filament");
                    return false;
                }
            } else {
                PostErrorMessage(printerId, action, originalCommandStr,
                                 wxString::Format("nozzle temperature too low to extrude (min: %.1f)",
                                                  PRINTAGO_TEMP_THRESHOLD_ALLOW_E_CTRL));
                return false;
            }
        }
    }

    // only send this response if it's *not* a start_print_bbl command.
    if (action.compare("start_print_bbl")) {
        PostSuccessMessage(printerId, action, originalCommandStr, actionDetail);
    }
    return true;
}

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

std::map<wxString, wxString> PrintagoDirector::ExtractPrefixedParams(const wxStringToStringHashMap& params, const wxString& prefix)
{
    std::map<wxString, wxString> extractedParams;
    for (const auto& kv : params) {
        if (kv.first.StartsWith(prefix + ".")) {
            wxString parmName         = kv.first.Mid(prefix.length() + 1); // +1 for the dot
            extractedParams[parmName] = kv.second;
        }
    }
    return extractedParams;
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
    statusObject["process"]["job_id"]         = PBJob::jobId.ToStdString();
    statusObject["process"]["job_state"]      = PBJob::serverStateStr();
    statusObject["process"]["job_machine"]    = PBJob::printerId.ToStdString();
    statusObject["process"]["job_local_file"] = PBJob::localFile.GetFullPath().ToStdString();
    statusObject["process"]["job_progress"]   = PBJob::progress;

    statusObject["process"]["bed_level"]      = PBJob::bbl_do_bed_leveling;
    statusObject["process"]["flow_calibr"]    = PBJob::bbl_do_flow_cali;
    statusObject["process"]["bed_type"]       = PBJob::bed_type;
    statusObject["process"]["use_ams"]        = PBJob::use_ams;

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

json PrintagoDirector::ConfigToJson(const DynamicPrintConfig &config,
                              const std::string &name,
                              const std::string &from,
                              const std::string &version,
                              const std::string  is_custom)
{
    json j;
    // record the headers
    j[BBL_JSON_KEY_VERSION] = version;
    j[BBL_JSON_KEY_NAME]    = name;
    j[BBL_JSON_KEY_FROM]    = from;
    if (!is_custom.empty())
        j[BBL_JSON_KEY_IS_CUSTOM] = is_custom;

    // record all the key-values
    for (const std::string &opt_key : config.keys()) {
        const ConfigOption *opt = config.option(opt_key);
        if (opt->is_scalar()) {
            if (opt->type() == coString && (opt_key != "bed_custom_texture" && opt_key != "bed_custom_model"))
                // keep \n, \r, \t
                j[opt_key] = (dynamic_cast<const ConfigOptionString *>(opt))->value;
            else
                j[opt_key] = opt->serialize();
        } else {
            const ConfigOptionVectorBase *vec = static_cast<const ConfigOptionVectorBase *>(opt);
            std::vector<std::string> string_values = vec->vserialize();
            json j_array(string_values);
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

    // set the selected printer in the monitor UI.
    try {
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
            int x = 5 + 1;
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
    if (PBJob::printerId.IsEmpty() || !m_select_machine_dlg || PBJob::CanProcessJob()) {
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

    actionDetail   = wxString::Format("send_to_printer: %s", PBJob::localFile.GetFullName());
    
    m_select_machine_dlg->set_print_type(PrintFromType::FROM_NORMAL);
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
    if (PBJob::printerId.IsEmpty() || !m_select_machine_dlg || PBJob::CanProcessJob()) {
        PBJob::UnblockJobProcessing();
        return;
    }
    if (!success) {
        PostErrorMessage(PBJob::printerId, "start_print_bbl", PBJob::command, "an error occurred sending the print job.");
        return;
    }

    PBJob::progress = 99;
    PostJobUpdateMessage();

    const wxString pid(PBJob::printerId);
    const wxString cmd(PBJob::command);

    PBJob::UnblockJobProcessing();

    PostSuccessMessage(pid, "start_print_bbl", cmd, wxString::Format("print sent to: %s", printerId));
}


} // namespace Slic3r
