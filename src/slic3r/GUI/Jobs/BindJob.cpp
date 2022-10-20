#include "BindJob.hpp"

#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"

namespace Slic3r {
namespace GUI {

wxDEFINE_EVENT(EVT_BIND_UPDATE_MESSAGE, wxCommandEvent);
wxDEFINE_EVENT(EVT_BIND_MACHINE_SUCCESS, wxCommandEvent);
wxDEFINE_EVENT(EVT_BIND_MACHINE_FAIL, wxCommandEvent);


static wxString waiting_auth_str = _L("Logging in");
static wxString login_failed_str = _L("Login failed");


wxString get_login_fail_reason(std::string fail_reason)
{
    if (fail_reason == "NO Regions")
        return _L("The region parameter is incorrrect");
    else if (fail_reason == "Cloud Timeout")
        return _L("Failure of printer login");
    else if (fail_reason == "Ticket Failed")
        return _L("Failed to get ticket");
    else if (fail_reason == "Wait Auth Timeout")
        return _L("User authorization timeout");
    else if (fail_reason == "Bind Failure")
        return _L("Failure of bind");
    else
        return _L("Unknown Failure");
}

BindJob::BindJob(std::shared_ptr<ProgressIndicator> pri, Plater *plater, std::string dev_id, std::string dev_ip)
    : PlaterJob{std::move(pri), plater},
    m_dev_id(dev_id),
    m_dev_ip(dev_ip)
{
    ;
}

void BindJob::on_exception(const std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
    } catch (std::exception &e) {
        PlaterJob::on_exception(eptr);
    }
}

void BindJob::on_success(std::function<void()> success)
{
    m_success_fun = success;
}

void BindJob::update_status(int st, const wxString &msg)
{
    GUI::Job::update_status(st, msg);
    //post_event(wxCommandEvent(EVT_BIND_UPDATE_MESSAGE), msg);
    wxCommandEvent event(EVT_BIND_UPDATE_MESSAGE);
    event.SetString(msg);
    event.SetEventObject(m_event_handle);
    wxPostEvent(m_event_handle, event);
}

void BindJob::process()
{
    /* display info */
    wxString msg = waiting_auth_str;
    int curr_percent = 0;

    NetworkAgent* m_agent = wxGetApp().getAgent();
    if (!m_agent) { return; }

    // get timezone
    wxDateTime::TimeZone tz(wxDateTime::Local);
    long offset = tz.GetOffset();
    std::string timezone = get_timezone_utc_hm(offset);

    int result = m_agent->bind(m_dev_ip, timezone,
        [this, &curr_percent, &msg](int stage, int code, std::string info) {
            if (stage == BBL::BindJobStage::LoginStageConnect) {
                curr_percent = 15;
                msg = _L("Logging in");
            } else if (stage == BBL::BindJobStage::LoginStageLogin) {
                curr_percent = 30;
                msg = _L("Logging in");
            } else if (stage == BBL::BindJobStage::LoginStageWaitForLogin) {
                curr_percent = 45;
                msg = _L("Logging in");
            } else if (stage == BBL::BindJobStage::LoginStageGetIdentify) {
                curr_percent = 60;
                msg = _L("Logging in");
            } else if (stage == BBL::BindJobStage::LoginStageWaitAuth) {
                curr_percent = 80;
                msg = _L("Logging in");
            } else if (stage == BBL::BindJobStage::LoginStageFinished) {
                curr_percent = 100;
                msg = _L("Logging in");
            } else {
                msg = _L("Logging in");
            }
            if (code != 0) {
                msg = _L("Login failed") + wxString::Format("(code=%d,info=%s). ", code, info);
                if (code == BAMBU_NETWORK_ERR_TIMEOUT) {
                    msg += _L("Please check the printer network connection.");
                }
            }
            update_status(curr_percent, msg);
        }
    );

    if (result < 0) {
        BOOST_LOG_TRIVIAL(trace) << "login: result = " << result;
        post_fail_event();
        return;
    }

    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) {
        BOOST_LOG_TRIVIAL(trace) << "login: dev is null";
        post_fail_event();
        return;
    }
    dev->update_user_machine_list_info();

    wxCommandEvent event(EVT_BIND_MACHINE_SUCCESS);
    event.SetEventObject(m_event_handle);
    wxPostEvent(m_event_handle, event);
    return;
}

void BindJob::finalize()
{
    if (was_canceled()) return;

    Job::finalize();
}

void BindJob::set_event_handle(wxWindow *hanle)
{
    m_event_handle = hanle;
}

void BindJob::post_fail_event()
{
    wxCommandEvent event(EVT_BIND_MACHINE_FAIL);
    event.SetEventObject(m_event_handle);
    wxPostEvent(m_event_handle, event);
}

}} // namespace Slic3r::GUI
