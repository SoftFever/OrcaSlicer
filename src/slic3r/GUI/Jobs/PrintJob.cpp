#include "PrintJob.hpp"
#include "libslic3r/MTUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "bambu_networking.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r {
namespace GUI {

static wxString check_gcode_failed_str      = _L("Abnormal print file data. Please slice again");
static wxString printjob_cancel_str         = _L("Task canceled");
static wxString timeout_to_upload_str       = _L("Upload task timed out. Please check the network problem and try again");
static wxString failed_in_cloud_service_str = _L("Cloud service connection failed. Please try again.");
static wxString file_is_not_exists_str      = _L("Print file not found, please slice again");
static wxString file_over_size_str          = _L("The print file exceeds the maximum allowable size (1GB). Please simplify the model and slice again");
static wxString print_canceled_str          = _L("Task canceled");
static wxString upload_failed_str           = _L("Failed uploading print file");
static wxString upload_login_failed_str     = _L("Wrong Access code");


static wxString sending_over_lan_str = _L("Sending print job over LAN");
static wxString sending_over_cloud_str = _L("Sending print job through cloud service");

PrintJob::PrintJob(std::shared_ptr<ProgressIndicator> pri, Plater* plater, std::string dev_id)
: PlaterJob{ std::move(pri), plater },
    m_dev_id(dev_id)
{
    m_print_job_completed_id = plater->get_print_finished_event();
}

void PrintJob::prepare()
{
    m_plater->get_print_job_data(&job_data);
}

void PrintJob::on_exception(const std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
    } catch (std::exception &e) {
        PlaterJob::on_exception(eptr);
    }
}

void PrintJob::on_success(std::function<void()> success)
{
    m_success_fun = success;
}

wxString PrintJob::get_http_error_msg(unsigned int status, std::string body)
{
    int code = 0;
    std::string error;
    std::string message;
    wxString result;
    if (status >= 400 && status < 500)
        try {
        json j = json::parse(body);
        if (j.contains("code")) {
            if (!j["code"].is_null())
                code = j["code"].get<int>();
        }
        if (j.contains("error")) {
            if (!j["error"].is_null())
                error = j["error"].get<std::string>();
        }
        if (j.contains("message")) {
            if (!j["message"].is_null())
                message = j["message"].get<std::string>();
        }
        switch (status) {
            ;
        }
    }
    catch (...) {
        ;
    }
    else if (status == 503) {
        return _L("Service Unavailable");
    }
    else {
        wxString unkown_text = _L("Unkown Error.");
        unkown_text += wxString::Format("status=%u, body=%s", status, body);
        BOOST_LOG_TRIVIAL(error) << "http_error: status=" << status << ", code=" << code << ", error=" << error;
        return unkown_text;
    }

    BOOST_LOG_TRIVIAL(error) << "http_error: status=" << status << ", code=" << code << ", error=" << error;

    result = wxString::Format("code=%u, error=%s", code, from_u8(error));
    return result;
}

void PrintJob::process()
{
    /* display info */
    wxString msg;
    int curr_percent = 10;

    if (this->connection_type == "lan") {
        msg = _L("Sending print job over LAN");
    }
    else {
        msg = _L("Sending print job through cloud service");
    }

    int result = -1;
    unsigned int http_code;
    std::string http_body;

    int total_plate_num = m_plater->get_partplate_list().get_plate_count();

    PartPlate* plate = m_plater->get_partplate_list().get_plate(job_data.plate_idx);
    if (plate == nullptr) {
        plate = m_plater->get_partplate_list().get_curr_plate();
        if (plate == nullptr)
        return;
    }

    /* check gcode is valid */
    if (!plate->is_valid_gcode_file()) {
        update_status(curr_percent, check_gcode_failed_str);
        return;
    }

    if (was_canceled()) {
        update_status(curr_percent, printjob_cancel_str);
        return;
    }

    // task name
    std::string project_name = wxGetApp().plater()->get_project_name().ToUTF8().data();
    int curr_plate_idx = 0;
    if (job_data.plate_idx >= 0)
        curr_plate_idx = job_data.plate_idx + 1;
    else if (job_data.plate_idx == PLATE_CURRENT_IDX)
        curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;
    else if (job_data.plate_idx == PLATE_ALL_IDX)
        curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;
    else
        curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;

    BBL::PrintParams params;
    params.dev_id = m_dev_id;
    //params.project_name = project_name;
    params.project_name = m_project_name;
    params.preset_name = wxGetApp().preset_bundle->prints.get_selected_preset_name();
    params.filename = job_data._3mf_path.string();
    params.config_filename = job_data._3mf_config_path.string();
    params.plate_index = curr_plate_idx;
    params.task_bed_leveling    = this->task_bed_leveling;
    params.task_flow_cali       = this->task_flow_cali;
    params.task_vibration_cali  = this->task_vibration_cali;
    params.task_layer_inspect   = this->task_layer_inspect;
    params.task_record_timelapse= this->task_record_timelapse;
    params.ams_mapping          = this->task_ams_mapping;
    params.ams_mapping_info     = this->task_ams_mapping_info;
    params.connection_type      = this->connection_type;
    params.task_use_ams         = this->task_use_ams;

    // local print access
    params.dev_ip = m_dev_ip;
    params.use_ssl  = m_local_use_ssl;
    params.username = "bblp";
    params.password = m_access_code;
    wxString error_text;
    wxString msg_text;


    const int StagePercentPoint[(int)PrintingStageFinished + 1] = {
        20,     // PrintingStageCreate
        30,     // PrintingStageUpload
        70,     // PrintingStageWaiting
        75,     // PrintingStageRecord
        99,     // PrintingStageSending
        100     // PrintingStageFinished
    };

    auto update_fn = [this, &msg, &curr_percent, &error_text, StagePercentPoint](int stage, int code, std::string info) {
                        if (stage == BBL::SendingPrintJobStage::PrintingStageCreate) {
                            if (this->connection_type == "lan") {
                                msg = _L("Sending print job over LAN");
                            } else {
                                msg = _L("Sending print job through cloud service");
                            }
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageUpload) {
                            if (code >= 0 && code <= 100 && !info.empty()) {
                                if (this->connection_type == "lan") {
                                    msg = _L("Sending print job over LAN");
                                } else {
                                    msg = _L("Sending print job through cloud service");
                                }
                                msg += wxString::Format("(%s)", info);
                            }
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageWaiting) {
                            if (this->connection_type == "lan") {
                                msg = _L("Sending print job over LAN");
                            } else {
                                msg = _L("Sending print job through cloud service");
                            }
                        }
                        else  if (stage == BBL::SendingPrintJobStage::PrintingStageRecord) {
                            msg = _L("Sending print configuration");
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageSending) {
                            if (this->connection_type == "lan") {
                                msg = _L("Sending print job over LAN");
                            } else {
                                msg = _L("Sending print job through cloud service");
                            }
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageFinished) {
                            msg = wxString::Format(_L("Successfully sent. Will automatically jump to the device page in %ss"), info);
                            this->update_percent_finish();
                        } else {
                            if (this->connection_type == "lan") {
                                msg = _L("Sending print job over LAN");
                            } else {
                                msg = _L("Sending print job through cloud service");
                            }
                        }

                        // update current percnet
                        if (stage >= 0 && stage <= (int) PrintingStageFinished) {
                            curr_percent = StagePercentPoint[stage];
                            if ((stage == BBL::SendingPrintJobStage::PrintingStageUpload
                                || stage == BBL::SendingPrintJobStage::PrintingStageRecord)
                                && (code > 0 && code <= 100)) {
                                curr_percent = (StagePercentPoint[stage + 1] - StagePercentPoint[stage]) * code / 100 + StagePercentPoint[stage];
                            }
                        }

                        if (code > 100 || code < 0) {
                            error_text = this->get_http_error_msg(code, info);
                            msg += wxString::Format("[%s]", error_text);
                        }
                        this->update_status(curr_percent, msg);
                    };

    auto cancel_fn = [this]() {
            return was_canceled();
        };


    NetworkAgent* m_agent = wxGetApp().getAgent();

    if (params.connection_type != "lan") {
        if (params.dev_ip.empty())
            params.comments = "no_ip";
        else if (this->cloud_print_only)
            params.comments = "low_version";
        else if (!this->has_sdcard)
            params.comments = "no_sdcard";
        else if (params.password.empty())
            params.comments = "no_password";

        if (!this->cloud_print_only
            && !params.password.empty() 
            && !params.dev_ip.empty()
            && this->has_sdcard) {
            // try to send local with record
            BOOST_LOG_TRIVIAL(info) << "print_job: try to start local print with record";
            this->update_status(curr_percent, _L("Sending print job over LAN"));
            result = m_agent->start_local_print_with_record(params, update_fn, cancel_fn);
            if (result == BAMBU_NETWORK_ERR_FTP_LOGIN_DENIED) {
                params.comments = "wrong_code";
            } else if (result == BAMBU_NETWORK_ERR_FTP_UPLOAD_FAILED) {
                params.comments = "upload_failed";
            } else {
                params.comments = (boost::format("failed(%1%)") % result).str();
            }
            if (result < 0) {
                // try to send with cloud
                BOOST_LOG_TRIVIAL(warning) << "print_job: try to send with cloud";
                this->update_status(curr_percent, _L("Sending print job through cloud service"));
                result = m_agent->start_print(params, update_fn, cancel_fn);
            }
        } else {
            BOOST_LOG_TRIVIAL(info) << "print_job: send with cloud";
            this->update_status(curr_percent, _L("Sending print job through cloud service"));
            result = m_agent->start_print(params, update_fn, cancel_fn);
        }
    } else {
        if (this->has_sdcard) {
            this->update_status(curr_percent, _L("Sending print job over LAN"));
            result = m_agent->start_local_print(params, update_fn, cancel_fn);
        } else {
            this->update_status(curr_percent, _L("An SD card needs to be inserted before printing via LAN."));
            return;
        }
    }

    if (result < 0) {
        if (result == BAMBU_NETWORK_ERR_FTP_LOGIN_DENIED) {
            msg_text = _L("Failed to send the print job. Please try again.");
        } if (result == BAMBU_NETWORK_ERR_FILE_NOT_EXIST) {
            msg_text = file_is_not_exists_str;
        } else if (result == BAMBU_NETWORK_ERR_FILE_OVER_SIZE) {
            msg_text = file_over_size_str;
        } else if (result == BAMBU_NETWORK_ERR_CHECK_MD5_FAILED) {
            msg_text = failed_in_cloud_service_str;
        } else if (result == BAMBU_NETWORK_ERR_INVALID_PARAMS) {
            msg_text = _L("Failed to send the print job. Please try again.");
        } else if (result == BAMBU_NETWORK_ERR_CANCELED) {
            msg_text = print_canceled_str;
        } else if (result == BAMBU_NETWORK_ERR_TIMEOUT) {
            msg_text = timeout_to_upload_str;
        } else if (result == BAMBU_NETWORK_ERR_INVALID_RESULT) {
            msg_text = _L("Failed to send the print job. Please try again.");
        } else if (result == BAMBU_NETWORK_ERR_FTP_UPLOAD_FAILED) {
            msg_text = _L("Failed to send the print job. Please try again.");
        } else {
            update_status(curr_percent, failed_in_cloud_service_str);
        }
        if (!error_text.IsEmpty()) {
            curr_percent = 0;
            msg_text += wxString::Format("[%d][%s]", result, error_text);
        }
        update_status(curr_percent, msg_text);
        BOOST_LOG_TRIVIAL(error) << "print_job: failed, result = " << result;
    } else {
        BOOST_LOG_TRIVIAL(error) << "print_job: send ok.";
        wxCommandEvent* evt = new wxCommandEvent(m_print_job_completed_id);
        evt->SetString(m_dev_id);
        wxQueueEvent(m_plater, evt);
        m_job_finished = true;
    }
}

void PrintJob::finalize() {
    if (was_canceled()) return;

    Job::finalize();
}

void PrintJob::set_project_name(std::string name)
{
    m_project_name = name;
}

}} // namespace Slic3r::GUI
