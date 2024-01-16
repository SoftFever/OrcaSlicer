#include "WebViewDialog.hpp"

#include "I18N.hpp"
#include "../Utils/Http.hpp"
#include "nlohmann/json.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/wxExtensions.hpp"

#include <slic3r/GUI/Widgets/WebView.hpp>
#include <wx/datetime.h>
#include <wx/sizer.h>
#include <wx/url.h>

#include "Tab.hpp"

#include <thread>

using namespace nlohmann;

namespace pt = boost::property_tree;

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(PRINTAGO_SEND_WEBVIEW_MESSAGE_EVENT, PrintagoMessageEvent);
wxDEFINE_EVENT(PRINTAGO_SLICING_PROCESS_COMPLETED_EVENT, SlicingProcessCompletedEvent);
wxDEFINE_EVENT(PRINTAGO_PRINT_SENT_EVENT, wxCommandEvent);


#define PRINTAGO_TEMP_THRESHOLD_ALLOW_E_CTRL 170.0f // Minimum temperature to allow extrusion control (per StatusPanel.cpp)

PrintagoPanel::PrintagoPanel(wxWindow *parent, wxString *url) : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    devManager           = Slic3r::GUI::wxGetApp().getDeviceManager();
    
    wxBoxSizer *topsizer = new wxBoxSizer(wxVERTICAL);

    m_browser = WebView::CreateWebView(this, *url);
    if (m_browser == nullptr) {
        wxLogError("Could not init m_browser");
        return;
    }
    m_browser->EnableContextMenu(false);
    m_browser->Hide();
    SetSizer(topsizer);
    
    topsizer->Add(m_browser, wxSizerFlags().Expand().Proportion(1));

    Bind(wxEVT_WEBVIEW_NAVIGATING, &PrintagoPanel::OnWebNavigationRequest, this);
    Bind(wxEVT_WEBVIEW_NAVIGATED, &PrintagoPanel::OnWebNavigationComplete, this);
    Bind(wxEVT_WEBVIEW_ERROR, &PrintagoPanel::OnWebError, this);
    Bind(wxEVT_WEBVIEW_NEWWINDOW, &PrintagoPanel::OnNewWindow, this);
    Bind(PRINTAGO_SEND_WEBVIEW_MESSAGE_EVENT, &PrintagoPanel::SendWebViewMessage, this);
    Bind(EVT_PROCESS_COMPLETED, &PrintagoPanel::OnSlicingProcessCompleted, this);
    Bind(PRINTAGO_PRINT_SENT_EVENT, &PrintagoPanel::OnPrintJobSent, this);
    // Bind(wxEVT_WEBVIEW_ERROR, &PrintagoPanel::OnWebError, this);
    // Bind(wxEVT_WEBVIEW_NAVIGATING, &PrintagoPanel::OnWebNavigating, this);

    // wxGetApp().mainframe->m_plater->Disable();
    // wxGetApp().mainframe->m_tabpanel->Disable();
}

PrintagoPanel::~PrintagoPanel()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " Start";
    SetEvtHandlerEnabled(false);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " End";
}

void PrintagoPanel::load_url(wxString &url)
{
    this->Show();
    this->Raise();

    m_browser->LoadURL(url);
    m_browser->SetFocus();
}

void PrintagoPanel::SetCanProcessJob(const bool can_process_job)
{
    if (can_process_job) {
        jobPrinterId.Clear();
        jobCommand.Clear();
        jobLocalModelFile.Clear();
        jobServerState = "idle";
        jobConfigFiles.clear();
        jobProgress          = 0;
        jobPrintagoId        = "ptgo_default";
        m_select_machine_dlg = nullptr;
        // wxGetApp().mainframe->m_tabpanel->Enable();
        // wxGetApp().mainframe->m_topbar->Enable();
    } else {
        // wxGetApp().mainframe->m_tabpanel->Disable();
        // wxGetApp().mainframe->m_topbar->Disable();
    }
    m_can_process_job = can_process_job;
}

json PrintagoPanel::MachineObjectToJson(MachineObject *machine)
{
    json j;
    if (machine) {
        j["hardware"]["dev_model"]        = machine->printer_type;
        j["hardware"]["dev_display_name"] = machine->get_printer_type_display_str().ToStdString();
        j["hardware"]["dev_name"]         = machine->dev_name;
        j["hardware"]["nozzle_diameter"]  = machine->nozzle_diameter;

        j["hardware"]["compat_printer_profiles"] = GetCompatPrinterConfigNames(machine->get_preset_printer_model_name(machine->printer_type));  

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

json PrintagoPanel::GetMachineStatus(MachineObject *machine)
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

json PrintagoPanel::GetMachineStatus(const wxString &printerId)
{
    if (!devManager)
        return json::object();
    return GetMachineStatus(devManager->get_my_machine(printerId.ToStdString()));
}

json PrintagoPanel::GetAllStatus()
{
    json statusObject = json::object();
    json machineList  = json::array();

    if (!devManager)
        return json::object();

    AddCurrentProcessJsonTo(statusObject);

    const std::map<std::string, MachineObject *> machineMap = devManager->get_my_machine_list();

    for (auto &pair : machineMap) {
        machineList.push_back(MachineObjectToJson(pair.second));
    }
    statusObject["machines"] = machineList;
    return statusObject;
}

void PrintagoPanel::AddCurrentProcessJsonTo(json &statusObject)
{
    statusObject["process"]["can_process_job"] = CanProcessJob();
    statusObject["process"]["job_id"]         = ""; // add later from command.
    statusObject["process"]["job_state"]      = jobServerState.ToStdString();
    statusObject["process"]["job_machine"]    = jobPrinterId.ToStdString();
    statusObject["process"]["job_local_file"] = jobLocalModelFile.GetFullPath().ToStdString();
    statusObject["process"]["job_progress"]   = jobProgress;

    statusObject["software"]["is_dark_mode"] = wxGetApp().dark_mode();
}

bool PrintagoPanel::DownloadFileFromURL(const wxString url, const wxFileName &localFilename)
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

            if (fs::exists(target_path)) fs::remove(target_path);
            if (fs::exists(tmp_path)) fs::remove(tmp_path);
            
            auto http = Http::get(url.ToStdString());

            while (cont && retry_count < max_retries) {
                retry_count++;
                http.on_progress([&percent, &cont, &msg](Http::Progress progress, bool &cancel) {
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

bool PrintagoPanel::SavePrintagoFile(const wxString url, wxFileName &localPath)
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
    tempDir /= jobPrintagoId.ToStdString();

    boost::system::error_code ec; 
    if (!fs::create_directories(tempDir, ec)) {
        if (ec) {
            //there was an error creating the directory
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

json PrintagoPanel::GetConfigByName(wxString configType, wxString configName) 
{
    json result;
    PresetBundle     *presetBundle = wxGetApp().preset_bundle;
    PresetCollection *collection   = nullptr;

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
        configJson = Config2Json(preset->config, preset->name, "", preset->version.to_string());
    } else {  //preset not found
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
        case Preset::TYPE_PRINT:
            collectionName = "print";
            break;
        case Preset::TYPE_PRINTER:
            collectionName = "printer";
            break;
        case Preset::TYPE_FILAMENT:
            collectionName = "filament";
            break;
        default:
            break;

    }
    result["config_type"] = collectionName; 
    result["config_source"] = source.ToStdString();
    result["config_name"] = preset->name;
    result["config_content"] = configJson;

    if (collectionName == "printer") {
        result["compat_filament_profiles"] = GetCompatFilamentConfigNames(*preset);
        result["compat_print_profiles"] = GetCompatPrintConfigNames(*preset);
    }

    return result;
}

bool PrintagoPanel::IsConfigCompatWithPrinter(const PresetWithVendorProfile &preset, const Preset &printerPreset)
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
            printf("Preset::is_compatible_with_printer - parsing error of compatible_printers_condition %s:\n%s\n", active_printer.preset.name.c_str(), err.what());
            return true;
        }
    }
    return preset.preset.is_default || active_printer.preset.name.empty() || !has_compatible_printers ||
        std::find(compatible_printers->values.begin(), compatible_printers->values.end(), active_printer.preset.name) !=
        compatible_printers->values.end()
        //BBS
           || (!active_printer.preset.is_system && IsConfigCompatWithParent(preset, active_printer));
}

bool PrintagoPanel::IsConfigCompatWithParent(const PresetWithVendorProfile &preset, const PresetWithVendorProfile &active_printer)
{
    auto *compatible_printers     = dynamic_cast<const ConfigOptionStrings *>(preset.preset.config.option("compatible_printers"));
    bool  has_compatible_printers = compatible_printers != nullptr && !compatible_printers->values.empty();
    // BBS: FIXME only check the parent now, but should check grand-parent as well.
    return has_compatible_printers && std::find(compatible_printers->values.begin(), compatible_printers->values.end(),
                                                active_printer.preset.inherits()) != compatible_printers->values.end();
}

json PrintagoPanel::GetCompatPrinterConfigNames(std::string printer_type)
{
    // Create a JSON object to hold arrays of config names for each source
    json result;

    // Iterate through each preset
    for (Preset preset : wxGetApp().preset_bundle->printers.get_presets()) {

        if (!preset.is_visible) continue; // Skip hidden presets

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
        for (const auto &vendor_profile : wxGetApp().preset_bundle->vendors) {
            for (const auto &vendor_model : vendor_profile.second.models) {
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

json PrintagoPanel::GetCompatOtherConfigsNames(Preset::Type preset_type, const Preset &printerPreset)
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

json PrintagoPanel::Config2Json(const DynamicPrintConfig &config,
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

wxString PrintagoPanel::wxURLErrorToString(wxURLError error)
{
    switch (error) {
        case wxURL_NOERR: return {"No Error"};
        case wxURL_SNTXERR: return {"Syntax Error"};
        case wxURL_NOPROTO: return {"No Protocol"};
        case wxURL_NOHOST: return {"No Host"};
        case wxURL_NOPATH: return {"No Path"};
        case wxURL_CONNERR: return {"Connection Error"};
        case wxURL_PROTOERR: return {"Protocol Error"};
        default: return {"Unknown Error"};
    }
}

bool PrintagoPanel::SwitchSelectedPrinter(const wxString &printerId)
{
    //don't switch the UI if CanProcessJob() is false; we're slicing and stuff in the UI.
    if (!devManager || !CanProcessJob()) return false;

    auto machine = devManager->get_my_machine(printerId.ToStdString());
    if (!machine) return false;

    //set the selected printer in the monitor UI.
    try {
        wxGetApp().mainframe->m_monitor->select_machine(printerId.ToStdString());
    } catch (...) {
        return false;
    }

    return true;
}

void PrintagoPanel::HandlePrintagoCommand(const PrintagoCommand &event)
{
    wxString                commandType        = event.GetCommandType();
    wxString                action             = event.GetAction();
    wxStringToStringHashMap parameters         = event.GetParameters();
    wxString                originalCommandStr = event.GetOriginalCommandStr();
    wxString                actionDetail;

    wxLogMessage("HandlePrintagoCommand: {command: " + commandType + ", action: " + action + "}");
    MachineObject *printer     = {nullptr};

    wxString printerId = parameters.count("printer_id") ? parameters["printer_id"] : "Unspecified"; 
    bool     hasPrinterId = printerId.compare("Unspecified");

    if (!commandType.compare("status")) {
        std::string username = wxGetApp().getAgent()->is_user_login() ? wxGetApp().getAgent()->get_user_name() : "nouser@bab";
        if (!action.compare("get_machine_list")) {
            SendResponseMessage(username, GetAllStatus(), originalCommandStr);
        }
        else if (!action.compare("get_config")) {
            wxString config_type = parameters["config_type"]; // printer, filament, print
            wxString config_name = Http::url_decode(parameters["config_name"].ToStdString()); // name of the config
            json     configJson  = GetConfigByName(config_type, config_name);
            if (!configJson.empty()) {
                SendResponseMessage(username, configJson, originalCommandStr);   
            } else {
                SendErrorMessage(username, action, originalCommandStr, "config not found; valid types are: print, printer, or filament");
                return;
            }
        } else if (!action.compare("switch_active")) {
            if (!CanProcessJob()) {
                SendErrorMessage("", action, originalCommandStr, "unable, UI blocked");
            }
            if (hasPrinterId) {
                if (!SwitchSelectedPrinter(printerId)) {
                    SendErrorMessage("", action, originalCommandStr, "unable, unknown");
                } else {
                    actionDetail = wxString::Format("connecting to %s", printerId);
                    SendSuccessMessage(printerId, action, originalCommandStr, actionDetail);
                }
            } else {
                SendErrorMessage("", action, originalCommandStr, "no printer_id specified");
            }
        }
        return;
    }

    if (!hasPrinterId) {
        SendErrorMessage("", action, originalCommandStr, "no printer_id specified");
        wxLogMessage("PrintagoCommandError: No printer_id specified");
        return;
    }
    // Find the printer in the machine list
    auto machineList = devManager->get_my_machine_list();
    auto it = std::find_if(machineList.begin(), machineList.end(),
                           [&printerId](const std::pair<std::string, MachineObject *> &pair) { return pair.second->dev_id == printerId; });

    if (it != machineList.end()) {
        // Printer found
        printer = it->second;
    } else {
        SendErrorMessage(printerId, action, originalCommandStr, "no printer not found with ID: " + printerId);
        wxLogMessage("PrintagoCommandError: No printer found with ID: " + printerId);
        return;
    }

    // select the printer for updates in the monitor for updates.
    SwitchSelectedPrinter(printerId);
    
    if (!commandType.compare("printer_control")) {
        if (!action.compare("pause_print")) {
            try {
                 if (!printer->can_pause()) {
                    SendErrorMessage(printerId, action, originalCommandStr, "cannot pause printer");
                    return;
                 }
                printer->command_task_pause();
            } catch (...) {
                SendErrorMessage(printerId, action, originalCommandStr, "an error occurred issuing pause_print");
                return;
            }
        }
        else if (!action.compare("resume_print")) {
            try {
                if (!printer->can_resume()) {
                    SendErrorMessage(printerId, action, originalCommandStr, "cannot resume printer");
                    return;
                }
                printer->command_task_resume();
            } catch (...) {
                SendErrorMessage(printerId, action, originalCommandStr, "an error occurred issuing resume_print");
                return;
            }
        }
        else if (!action.compare("stop_print")) {
            try {
                if (!printer->can_abort()) {
                    SendErrorMessage(printerId, action, originalCommandStr, "cannot abort printer");
                    return;
                }
                printer->command_task_abort();
            } catch (...) {
                SendErrorMessage(printerId, action, originalCommandStr, "an error occurred issuing stop_print");
                return;
            }
        }
        else if (!action.compare("get_status")) {
            SendStatusMessage(printerId, GetMachineStatus(printer), originalCommandStr);
            return;
        }
        else if (!action.compare("start_print_bbl")) {
            if (!printer->can_print() && jobPrinterId.compare(printerId)) { //printer can print, and we're not already prepping for it.
                SendErrorMessage(printerId, action, originalCommandStr, "cannot start print");
                return;
            }
            
            wxString printagoModelUrl = parameters["model"];
            wxString printerConfUrl  = parameters["printer_conf"];
            wxString printConfUrl    = parameters["print_conf"];
            wxString filamentConfUrl = parameters["filament_conf"];

            wxString printagoId      = parameters["printago_job"];
            if (!printagoId.empty()) {
                jobPrintagoId = printagoId;
            }

            jobPrinterId             = printerId;
            jobCommand               = originalCommandStr;
            jobLocalModelFile        = "";

            if (!m_select_machine_dlg)
                m_select_machine_dlg = new SelectMachineDialog(wxGetApp().plater());

            if (!CanProcessJob()) {
                SendErrorMessage(printerId, action, originalCommandStr, "busy with current job - check status");
                return;
            }
            SetCanProcessJob(false);

            if (printagoModelUrl.empty()) {
                SendErrorAndUnblock(printerId, action, originalCommandStr, "no url specified");
                return;
            } else if (printConfUrl.empty() || printerConfUrl.empty() || filamentConfUrl.empty()) {
                SendErrorAndUnblock(printerId, action, originalCommandStr, "must specify printer, filament, and print configurations");
                return;
            } else {
                printagoModelUrl = Http::url_decode(printagoModelUrl.ToStdString());
                printerConfUrl = Http::url_decode(printerConfUrl.ToStdString());
                printConfUrl = Http::url_decode(printConfUrl.ToStdString());
                filamentConfUrl = Http::url_decode(filamentConfUrl.ToStdString());
            }

            jobServerState = "download";
            jobProgress    = 10;

            // Second param is reference and modified inside SavePrintagoFile.
            if (SavePrintagoFile(printagoModelUrl, jobLocalModelFile)) {
                wxLogMessage("Downloaded file to: " + jobLocalModelFile.GetFullPath());
            } else {
                SendErrorAndUnblock(printerId, wxString::Format("%s:%s", action, jobServerState), originalCommandStr, "model download failed");
                return;
            }

            // Do the configuring here: this allows 3MF files to load, then we can configure the slicer and override the 3MF conf settings from what Printago sent.
            wxFileName localPrinterConf, localFilamentConf, localPrintConf;
            if (SavePrintagoFile(printerConfUrl, localPrinterConf) && SavePrintagoFile(filamentConfUrl, localFilamentConf) &&
                SavePrintagoFile(printConfUrl, localPrintConf)) {
                wxLogMessage("Downloaded config files");
            } else {
                SendErrorAndUnblock(printerId, wxString::Format("%s:%s", action, jobServerState), originalCommandStr,
                                    "config download failed");
                return;
            }

            jobConfigFiles["printer"] = localPrinterConf;
            jobConfigFiles["filament"] = localFilamentConf;
            jobConfigFiles["print"] = localPrintConf;

            jobServerState = "configure";
            jobProgress    = 30;

            wxGetApp().mainframe->select_tab(1);
            wxGetApp().plater()->reset();

            actionDetail = wxString::Format("slice_config: %s", jobLocalModelFile.GetFullPath());

            // Loads the configs into the UI, if able; selects them in the dropdowns.
            ImportPrintagoConfigs();
            SetPrintagoConfigs();

            try {
                if (!jobLocalModelFile.GetExt().MakeUpper().compare("3MF")) {
                    // The last 'true' tells the function to not ask the user to confirm the load; save any existing work.
                    wxGetApp().plater()->load_project(jobLocalModelFile.GetFullPath(), "-", true);
                    SetPrintagoConfigs(); //since the 3MF may have it's own configs that get set on load.
                } else {
                    std::vector<std::string> filePathArray;
                    filePathArray.push_back(jobLocalModelFile.GetFullPath().ToStdString());
                    LoadStrategy strategy = LoadStrategy::LoadModel |
                                            LoadStrategy::Silence; // LoadStrategy::LoadConfig | LoadStrategy::LoadAuxiliary
                    wxGetApp().plater()->load_files(filePathArray, strategy, false);
                }
            } catch (...) {
                SendErrorAndUnblock(jobPrinterId, wxString::Format("%s:%s", "start_print_bbl", jobServerState), jobCommand,
                                    "and error occurred loading the model and config");
                return;
            }

            jobServerState = "slice";
            jobProgress    = 45;
            devManager->get_my_machine_list();
            wxGetApp().plater()->select_plate(0, true);
            wxGetApp().plater()->reslice();
            actionDetail = wxString::Format("slice_start: %s", jobLocalModelFile.GetFullPath());
        }
    }
    else if (!commandType.compare("temperature_control")) {
        if (!printer->can_print() && jobPrinterId.compare(printerId)) {
            SendErrorMessage(printerId, action, originalCommandStr, "cannot control temperature; printer busy");
            return;
        }
        wxString tempStr = parameters["temperature"];
        long     targetTemp;
        if (!tempStr.ToLong(&targetTemp)) {
            SendErrorMessage(printerId, action, originalCommandStr, "invalid temperature value");
            return;
        }

        if (!action.compare("set_hotend")) {
            try {
                printer->command_set_nozzle(targetTemp);
                actionDetail = wxString::Format("%d", targetTemp);
            } catch (...) {
                SendErrorMessage(printerId, action, originalCommandStr, "an error occurred setting nozzle temperature");
                return;
            }
        }
        else if (!action.compare("set_bed")) {
            try {
                int limit = printer->get_bed_temperature_limit();
                if (targetTemp >= limit) {
                    targetTemp = limit;
                }
                printer->command_set_bed(targetTemp);
                actionDetail = wxString::Format("%d", targetTemp);
            } catch (...) {
                SendErrorMessage(printerId, action, originalCommandStr, "an error occurred setting bed temperature");
                return;
            }
        } 
    }
    else if (!commandType.compare("movement_control")) {
        if (!printer->can_print() && jobPrinterId.compare(printerId)) {
            SendErrorMessage(printerId, action, originalCommandStr, "cannot control movement; printer busy");
            return;
        }
        if (!action.compare("jog")) {
            auto axes = ExtractPrefixedParams(parameters, "axes");
            if (axes.empty()) {
                SendErrorMessage(printerId, action, originalCommandStr, "no axes specified");
                wxLogMessage("PrintagoCommandError: No axes specified");
                return;
            }

            if (!printer->is_axis_at_home("X") || !printer->is_axis_at_home("Y") || !printer->is_axis_at_home("Z")) {
                SendErrorMessage(printerId, action, originalCommandStr, "must home axes before moving");
                wxLogMessage("PrintagoCommandError: Axes not at home");
                return;
            }
            // Iterate through each axis and its value; we do this loop twice to ensure the input in clean.
            // this ensures we do not move the head unless all input moves are valid.
            for (const auto &axis : axes) {
                wxString axisName = axis.first;
                axisName.MakeUpper();
                if (axisName != "X" && axisName != "Y" && axisName != "Z") {
                    SendErrorMessage(printerId, action, originalCommandStr, "invalid axis name: " + axisName);
                    wxLogMessage("PrintagoCommandError: Invalid axis name " + axisName);
                    return;
                }
                wxString axisValueStr = axis.second;
                double   axisValue;
                if (!axisValueStr.ToDouble(&axisValue)) {
                    SendErrorMessage(printerId, action, originalCommandStr, "invalid value for axis " + axisName);
                    wxLogMessage("PrintagoCommandError: Invalid value for axis " + axisName);
                    return;
                }
            }

            for (const auto &axis : axes) {
                wxString axisName = axis.first;
                axisName.MakeUpper();
                wxString axisValueStr = axis.second;
                double   axisValue;
                axisValueStr.ToDouble(&axisValue);
                try {
                    printer->command_axis_control(axisName.ToStdString(), 1.0, axisValue, 3000);
                } catch (...) {
                    SendErrorMessage(printerId, action, originalCommandStr, "an error occurred moving axis " + axisName);
                    wxLogMessage("PrintagoCommandError: An error occurred moving axis " + axisName);
                    return;
                }
            }

        }
        else if (!action.compare("home")) {
            try {
                printer->command_go_home();
            } catch (...) {
                SendErrorMessage(printerId, action, originalCommandStr, "an error occurred homing axes");
                wxLogMessage("PrintagoCommandError: An error occurred homing axes");
                return;
            }

        }
        else if (!action.compare("extrude")) {
            wxString amtStr = parameters["amount"];
            long     extrudeAmt;
            if (!amtStr.ToLong(&extrudeAmt)) {
                wxLogMessage("Invalid extrude amount value: " + amtStr);
                SendErrorMessage(printerId, action, originalCommandStr, "invalid extrude amount value");
                return;
            }

            if (printer->nozzle_temp >= PRINTAGO_TEMP_THRESHOLD_ALLOW_E_CTRL) {
                try {
                    printer->command_axis_control("E", 1.0, extrudeAmt, 900);
                    actionDetail = wxString::Format("%d", extrudeAmt);
                } catch (...) {
                    SendErrorMessage(printerId, action, originalCommandStr, "an error occurred extruding filament");
                    wxLogMessage("PrintagoCommandError: An error occurred extruding filament");
                    return;
                }
            } else {
                SendErrorMessage(printerId, action, originalCommandStr,
                                 wxString::Format("nozzle temperature too low to extrude (min: %.1f)",
                                                  PRINTAGO_TEMP_THRESHOLD_ALLOW_E_CTRL));
                wxLogMessage("PrintagoCommandError: Nozzle temperature too low to extrude");
                return;
            }

        } 
    }

    //only send this response if it's *not* a start_print_bbl command.
    if (action.compare("start_print_bbl")) {
        SendSuccessMessage(printerId, action, originalCommandStr, actionDetail);
    }
    return;
}

void PrintagoPanel::ImportPrintagoConfigs()
{
    std::vector<std::string> cfiles;
    cfiles.push_back(into_u8(jobConfigFiles["printer"].GetFullPath()));
    cfiles.push_back(into_u8(jobConfigFiles["filament"].GetFullPath()));
    cfiles.push_back(into_u8(jobConfigFiles["print"].GetFullPath()));
    
    wxGetApp().preset_bundle->import_presets(
        cfiles,
        [this](std::string const &name) {
            return wxID_YESTOALL;
        },
        ForwardCompatibilitySubstitutionRule::Enable);
    if (!cfiles.empty()) {
        //reloads the UI presets in the dropdowns.
        wxGetApp().load_current_presets(true);
    }
}

void PrintagoPanel::SetPrintagoConfigs()
{
    std::string printerProfileName  = GetConfigNameFromJsonFile(jobConfigFiles["printer"].GetFullPath());
    std::string filamentProfileName = GetConfigNameFromJsonFile(jobConfigFiles["filament"].GetFullPath());
    std::string printProfileName    = GetConfigNameFromJsonFile(jobConfigFiles["print"].GetFullPath());

    wxGetApp().get_tab(Preset::TYPE_PRINTER)->select_preset(printerProfileName);
    wxGetApp().get_tab(Preset::TYPE_PRINT)->select_preset(printProfileName);

    int numFilaments = wxGetApp().filaments_cnt();
    for (int i = 0; i < numFilaments; ++i) {
        wxGetApp().preset_bundle->set_filament_preset(i, filamentProfileName);
        wxGetApp().plater()->sidebar().combos_filament()[i]->update();
    }
}

std::string PrintagoPanel::GetConfigNameFromJsonFile(const wxString &FilePath)
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

void PrintagoPanel::OnSlicingProcessCompleted(SlicingProcessCompletedEvent &evt)
{
    // in case we got here by mistake and there's nothing we're trying to process; return silently.
    if (jobPrinterId.IsEmpty() || !m_select_machine_dlg || CanProcessJob()) {
        SetCanProcessJob(true);
        return;
    }
    const wxString action = "start_print_bbl";
    wxString       actionDetail;

    if (!evt.success()) {
        actionDetail = "slicing Unknown Error: " + jobLocalModelFile.GetFullPath();
        if (evt.cancelled())
            actionDetail = "slicing cancelled: " + jobLocalModelFile.GetFullPath();
        else if (evt.error())
            actionDetail = "slicing error: " + jobLocalModelFile.GetFullPath();
        SendErrorAndUnblock(jobPrinterId, action, jobCommand, actionDetail);
        return;
    }

    // Slicing Success -> Send to the Printer

    jobServerState = "send";
    jobProgress    = 75;
    actionDetail   = wxString::Format("send_to_printer: %s", jobLocalModelFile.GetFullName());

    m_select_machine_dlg->set_print_type(PrintFromType::FROM_NORMAL);
    m_select_machine_dlg->prepare(0);

    m_select_machine_dlg->setPrinterLastSelect(jobPrinterId.ToStdString());
    auto selectedPrinter = devManager->get_selected_machine();
    if (selectedPrinter->dev_id != jobPrinterId.ToStdString() && !selectedPrinter->is_connected()) {
        devManager->set_selected_machine(jobPrinterId.ToStdString(), false);
    }

    wxCommandEvent btnEvt(GetId());
    m_select_machine_dlg->on_ok_btn(btnEvt);
}

void PrintagoPanel::OnPrintJobSent(wxCommandEvent &evt)
{
    // in case we got here by mistake and there's nothing we're trying to process; return silently.
    if (jobPrinterId.IsEmpty() || !m_select_machine_dlg || CanProcessJob()) {
        SetCanProcessJob(true);
        return;
    }
    const wxString printSentTo = evt.GetString();
    if (!printSentTo.compare("ERROR")) {
        SendErrorAndUnblock(jobPrinterId, "start_print_bbl", jobCommand, "an error occurred sending the print job.");
        return;
    }

    //Hack so SendSuccessMessage is the last thing we do before unblocking.
    const wxString pid(jobPrinterId);
    const wxString cmd (jobCommand);

    SetCanProcessJob(true);
    SendSuccessMessage(pid, "start_print_bbl", cmd, wxString::Format("print sent to: %s", printSentTo));
}

wxStringToStringHashMap PrintagoPanel::ParseQueryString(const wxString &queryString)
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

std::map<wxString, wxString> PrintagoPanel::ExtractPrefixedParams(const wxStringToStringHashMap &params, const wxString &prefix)
{
    std::map<wxString, wxString> extractedParams;
    for (const auto &kv : params) {
        if (kv.first.StartsWith(prefix + ".")) {
            wxString parmName         = kv.first.Mid(prefix.length() + 1); // +1 for the dot
            extractedParams[parmName] = kv.second;
        }
    }
    return extractedParams;
}

void PrintagoPanel::SendStatusMessage(const wxString printer_id, const json statusData, const wxString command)
{
    auto *event = new PrintagoMessageEvent(PRINTAGO_SEND_WEBVIEW_MESSAGE_EVENT);
    event->SetMessageType("status");
    event->SetPrinterId(printer_id);
    const wxURL url(command);
    wxString path = url.GetPath();
    event->SetCommand((!path.empty() && path[0] == '/') ? path.Remove(0, 1).ToStdString() : path.ToStdString());
    event->SetData(statusData);

    wxQueueEvent(this, event);
}

void PrintagoPanel::SendResponseMessage(const wxString printer_id, const json responseData, const wxString command)
{
    auto *event = new PrintagoMessageEvent(PRINTAGO_SEND_WEBVIEW_MESSAGE_EVENT);
    event->SetMessageType("status");
    event->SetPrinterId(printer_id);
    const wxURL url(command);
    event->SetCommand(url.GetPath().ToStdString());
    event->SetData(responseData);

    wxQueueEvent(this, event);
}

void PrintagoPanel::SendSuccessMessage(const wxString printer_id,
                                       const wxString localCommand,
                                       const wxString command,
                                       const wxString localCommandDetail)
{
    json responseData;
    responseData["local_command"]        = localCommand.ToStdString();
    responseData["local_command_detail"] = localCommandDetail.ToStdString();
    responseData["success"]              = true;

    auto *event = new PrintagoMessageEvent(PRINTAGO_SEND_WEBVIEW_MESSAGE_EVENT);
    event->SetMessageType("success");
    event->SetPrinterId(printer_id);
    const wxURL url(command);
    wxString path = url.GetPath();
    event->SetCommand((!path.empty() && path[0] == '/') ? path.Remove(0, 1).ToStdString() : path.ToStdString());
    event->SetData(responseData);

    wxQueueEvent(this, event);
}

void PrintagoPanel::SendErrorMessage(const wxString printer_id,
                                     const wxString localCommand,
                                     const wxString command,
                                     const wxString errorDetail)
{
    json errorResponse;
    errorResponse["local_command"] = localCommand.ToStdString();
    errorResponse["error_detail"]  = errorDetail.ToStdString();
    errorResponse["success"]       = false;

    auto *event = new PrintagoMessageEvent(PRINTAGO_SEND_WEBVIEW_MESSAGE_EVENT);
    event->SetMessageType("error");
    event->SetPrinterId(printer_id);
    const wxURL url(command);
    wxString    path = url.GetPath();
    event->SetCommand((!path.empty() && path[0] == '/') ? path.Remove(0, 1).ToStdString() : path.ToStdString());
    event->SetData(errorResponse);

    wxQueueEvent(this, event);
}

void PrintagoPanel::SendJsonErrorMessage(const wxString printer_id,
                                     const wxString localCommand,
                                     const wxString command,
                                     const json errorDetail)
{
    json errorResponse;
    errorResponse["local_command"] = localCommand.ToStdString();
    errorResponse["error_detail"]  = errorDetail;
    errorResponse["success"]       = false;

    auto *event = new PrintagoMessageEvent(PRINTAGO_SEND_WEBVIEW_MESSAGE_EVENT);
    event->SetMessageType("error");
    event->SetPrinterId(printer_id);
    const wxURL url(command);
    wxString    path = url.GetPath();
    event->SetCommand((!path.empty() && path[0] == '/') ? path.Remove(0, 1).ToStdString() : path.ToStdString());
    event->SetData(errorResponse);

    wxQueueEvent(this, event);
}

void PrintagoPanel::SendErrorAndUnblock(const wxString printer_id,
                                        const wxString localCommand,
                                        const wxString command,
                                        const wxString errorDetail)
{
    SetCanProcessJob(true);
    SendErrorMessage(printer_id, localCommand, command, errorDetail);
}

void PrintagoPanel::SendWebViewMessage(PrintagoMessageEvent &evt)
{
    wxDateTime now = wxDateTime::Now();
    now.MakeUTC();
    const wxString timestamp = now.FormatISOCombined() + "Z";

    json message;
    message["type"]        = evt.GetMessageType().ToStdString();
    message["timestamp"]   = timestamp.ToStdString();
    message["printer_id"]  = evt.GetPrinterId().ToStdString();
    message["client_type"] = "bambu";
    message["command"]     = evt.GetCommand().ToStdString();
    message["data"]        = evt.GetData();

    const wxString messageStr = wxString(message.dump().c_str(), wxConvUTF8);
    CallAfter([this, messageStr]() {
        m_browser->RunScript(wxString::Format("window.postMessage(%s, '*');", messageStr));
    });
}

void PrintagoPanel::OnWebNavigationRequest(wxWebViewEvent &evt)
{
    if (!jobServerState.compare("configure")) 
         return;

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();

    const wxString &url = evt.GetURL();
    if (url.StartsWith("printago://")) {
        evt.Veto(); // Prevent the web view from navigating to this URL

        wxURI         uri(url);
        wxString      path           = uri.GetPath();
        wxArrayString pathComponents = wxStringTokenize(path, "/");
        wxString      commandType, action;

        // Extract commandType and action from the path

        if (pathComponents.GetCount() != 3) {
            SendErrorMessage("", "", "", "invalid printago command");
            return;
        }

        commandType = pathComponents.Item(1); // The first actual component after the leading empty one
        action      = pathComponents.Item(2); // The second actual component

        wxString                query      = uri.GetQuery();          // Get the query part of the URI
        wxStringToStringHashMap parameters = ParseQueryString(query); // Use ParseQueryString to get parameters

        PrintagoCommand event;
        event.SetCommandType(commandType);
        event.SetAction(action);
        event.SetParameters(parameters);
        event.SetOriginalCommandStr(url.ToStdString());

        if (!ValidatePrintagoCommand(event)) {
            SendErrorMessage("", "", url, "invalid printago command");
            return;
        } else {
            CallAfter([=]() { HandlePrintagoCommand(event); });
        }
    }
}

bool PrintagoPanel::ValidatePrintagoCommand(const PrintagoCommand &event)
{
    wxString commandType = event.GetCommandType();
    wxString action      = event.GetAction();

    // Map of valid command types to their corresponding valid actions
    std::map<std::string, std::set<std::string>> validCommands = {
        {"status", {"get_machine_list", "get_config", "switch_active" }},
        {"printer_control", {"pause_print", "resume_print", "stop_print", "get_status","start_print_bbl"}},
        {"temperature_control", {"set_hotend", "set_bed"}},
        {"movement_control", {"jog", "home", "extrude"}}
    };

    auto commandIter = validCommands.find(commandType.ToStdString());
    if (commandIter != validCommands.end() && commandIter->second.find(action.ToStdString()) != commandIter->second.end()) {
        return true;
    }
    return false; 
}

void PrintagoPanel::OnWebNavigationComplete(wxWebViewEvent &evt)
{
    m_browser->Show();
    Layout();
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": " << evt.GetTarget().ToUTF8().data();
}

void PrintagoPanel::OnNewWindow(wxWebViewEvent &evt) { evt.Veto(); }

void PrintagoPanel::RunScript(const wxString &javascript)
{
    if (!m_browser)
        return;

    WebView::RunScript(m_browser, javascript);
}

void PrintagoPanel::OnWebError(wxWebViewEvent &evt)
{
#define WX_ERROR_CASE(type) \
    case type: category = #type; break;

    wxString category;
    switch (evt.GetInt()) {
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
}

void PrintagoPanel::OnWebNavigating(wxWebViewEvent& evt)
{

}

}} // namespace Slic3r::GUI
