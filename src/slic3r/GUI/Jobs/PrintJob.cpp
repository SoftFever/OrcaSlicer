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

void PrintJob::process()
{
    /* display info */
    wxString msg;
    int curr_percent = 10;

    if (this->connection_type == "lan") {
        msg = sending_over_lan_str;
    }
    else {
        msg = sending_over_cloud_str;
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

    std::string project_name = wxGetApp().plater()->get_project_name().ToUTF8().data();
    int curr_plate_idx = 0;
    if (job_data.plate_idx >= 0)
        curr_plate_idx = job_data.plate_idx + 1;
    else if (job_data.plate_idx == PLATE_CURRENT_IDX)
        curr_plate_idx = m_plater->get_partplate_list().get_curr_plate_index() + 1;

    BBL::PrintParams params;
    params.dev_id = m_dev_id;
    params.project_name = wxGetApp().plater()->get_project_name().ToUTF8().data();
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
    params.connection_type      = this->connection_type;

    // local print access
    params.dev_ip = m_dev_ip;
    params.username = "bblp";
    params.password = m_access_code;

    auto update_fn = [this, &msg, &curr_percent](int stage, int code, std::string info) {
                        if (stage == BBL::SendingPrintJobStage::PrintingStageCreate) {
                            if (this->connection_type == "lan") {
                                msg = _L("Sending print job over LAN");
                            } else {
                                msg = _L("Sending print job through cloud service");
                            }
                            curr_percent = 25;
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageUpload) {
                            curr_percent = 30;
                            if (code == 0 && !info.empty()) {
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
                            curr_percent = 50;
                        }
                        else  if (stage == BBL::SendingPrintJobStage::PrintingStageRecord) {
                            curr_percent = 70;
                            msg = _L("Sending print configuration");
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageSending) {
                            if (this->connection_type == "lan") {
                                msg = _L("Sending print job over LAN");
                            } else {
                                msg = _L("Sending print job through cloud service");
                            }
                            curr_percent = 90;
                        }
                        else if (stage == BBL::SendingPrintJobStage::PrintingStageFinished) {
                            curr_percent = 100;
                            msg = wxString::Format(_L("Successfully sent.Will automatically jump to the device page in %s s"), info);
                        } else {
                            if (this->connection_type == "lan") {
                                msg = _L("Sending print job over LAN");
                            } else {
                                msg = _L("Sending print job through cloud service");
                            }
                        }
                        this->update_status(curr_percent, msg);
                    };

    auto cancel_fn = [this]() {
            return was_canceled();
        };

    NetworkAgent* m_agent = wxGetApp().getAgent();

    if (params.connection_type != "lan") {
        if (!this->cloud_print_only
            && !params.password.empty() 
            && !params.dev_ip.empty()
            && this->has_sdcard) {
            // try to send local with record
            BOOST_LOG_TRIVIAL(info) << "print_job: try to start local print with record";
            this->update_status(curr_percent, _L("Sending print job over LAN"));
            result = m_agent->start_local_print_with_record(params, update_fn, cancel_fn);
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
            if (result < 0) {
                if (!params.password.empty() && !params.dev_ip.empty()) {
                    //try to send with local only
                    if (this->has_sdcard) {
                        this->update_status(curr_percent, _L("Sending print job over LAN"));
                        result = m_agent->start_local_print(params, update_fn, cancel_fn);
                    } else {
                        this->update_status(curr_percent, _L("Failed to connect to the cloud server connection. Please insert an SD card and resend the print job, which will transfer the print file via LAN. "));
                        BOOST_LOG_TRIVIAL(error) << "print_job: failed, need sdcard";
                        return;
                    }
                }
            }
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

    if (was_canceled()) {
        update_status(curr_percent, printjob_cancel_str);
        return;
    }

    if (result < 0) {
        if (result == BAMBU_NETWORK_ERR_FTP_LOGIN_DENIED) {
            update_status(curr_percent, upload_failed_str);
        } if (result == BAMBU_NETWORK_ERR_FILE_NOT_EXIST) {
            update_status(curr_percent, file_is_not_exists_str);
        } else if (result == BAMBU_NETWORK_ERR_FILE_OVER_SIZE) {
            update_status(curr_percent, file_over_size_str);
        } else if (result == BAMBU_NETWORK_ERR_CHECK_MD5_FAILED) {
            update_status(curr_percent, failed_in_cloud_service_str);
        } else if (result == BAMBU_NETWORK_ERR_INVALID_PARAMS) {
            update_status(curr_percent, upload_failed_str);
        } else if (result == BAMBU_NETWORK_ERR_CANCELED) {
            update_status(curr_percent, print_canceled_str);
        } else if (result == BAMBU_NETWORK_ERR_TIMEOUT) {
            update_status(curr_percent, timeout_to_upload_str);
        } else if (result == BAMBU_NETWORK_ERR_INVALID_RESULT) {
            update_status(curr_percent, upload_failed_str);
        } else if (result == BAMBU_NETWORK_ERR_FTP_UPLOAD_FAILED) {
            update_status(curr_percent, upload_failed_str);
        } else {
            update_status(curr_percent, failed_in_cloud_service_str);
        }
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

}} // namespace Slic3r::GUI
