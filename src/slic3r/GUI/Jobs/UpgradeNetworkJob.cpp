#include "UpgradeNetworkJob.hpp"

#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/Http.hpp"

namespace Slic3r {
namespace GUI {

wxDEFINE_EVENT(EVT_UPGRADE_UPDATE_MESSAGE, wxCommandEvent);
wxDEFINE_EVENT(EVT_UPGRADE_NETWORK_SUCCESS, wxCommandEvent);
wxDEFINE_EVENT(EVT_DOWNLOAD_NETWORK_FAILED, wxCommandEvent);
wxDEFINE_EVENT(EVT_INSTALL_NETWORK_FAILED, wxCommandEvent);


UpgradeNetworkJob::UpgradeNetworkJob(std::shared_ptr<ProgressIndicator> pri)
    : Job{std::move(pri)}
{
    name         = "plugins";
    package_name = "networking_plugins.zip";
}

void UpgradeNetworkJob::on_exception(const std::exception_ptr &eptr)
{
    try {
        if (eptr)
            std::rethrow_exception(eptr);
    } catch (std::exception &e) {
        UpgradeNetworkJob::on_exception(eptr);
    }
}

void UpgradeNetworkJob::on_success(std::function<void()> success)
{
    m_success_fun = success;
}

void UpgradeNetworkJob::update_status(int st, const wxString &msg)
{
    BOOST_LOG_TRIVIAL(info) << "UpgradeNetworkJob: percent = " << st << "msg = " << msg;
    GUI::Job::update_status(st, msg);
    wxCommandEvent event(EVT_UPGRADE_UPDATE_MESSAGE);
    event.SetString(msg);
    event.SetEventObject(m_event_handle);
    wxPostEvent(m_event_handle, event);
}

void UpgradeNetworkJob::process()
{
    // downloading
    int result = 0;

    AppConfig* app_config = wxGetApp().app_config;
    if (!app_config)
        return;

    BOOST_LOG_TRIVIAL(info) << "[UpgradeNetworkJob process]: enter";

    // get temp path
    fs::path target_file_path = (fs::temp_directory_path() / package_name);
    fs::path tmp_path = target_file_path;
    auto path_str = tmp_path.string() + wxString::Format(".%d%s", get_current_pid(), ".tmp").ToStdString();
    tmp_path = fs::path(path_str);

    BOOST_LOG_TRIVIAL(info) << "UpgradeNetworkJob: save netowrk_plugin to " << tmp_path.string();

    auto cancel_fn = [this]() {
        return was_canceled();
    };
    int curr_percent = 0;
    result = wxGetApp().download_plugin(name, package_name, 
        [this, &curr_percent](int state, int percent, bool &cancel) {
            if (state == InstallStatusNormal) {
                update_status(percent, _L("Downloading"));
            } else if (state == InstallStatusDownloadFailed) {
                update_status(percent, _L("Download failed"));
            } else {
                update_status(percent, _L("Downloading"));
            }
            curr_percent = percent;
        }, cancel_fn);

    if (was_canceled()) {
        update_status(0, _L("Cancelled"));
        wxCommandEvent event(wxEVT_CLOSE_WINDOW);
        event.SetEventObject(m_event_handle);
        wxPostEvent(m_event_handle, event);
        return;
    }

    if (result < 0) {
        update_status(0, _L("Download failed"));
        wxCommandEvent event(EVT_DOWNLOAD_NETWORK_FAILED);
        event.SetEventObject(m_event_handle);
        wxPostEvent(m_event_handle, event);
        return;
    }

    result = wxGetApp().install_plugin(
        name, package_name,
        [this](int state, int percent, bool &cancel) {
        if (state == InstallStatusInstallCompleted) {
            update_status(percent, _L("Install successfully."));
        } else {
            update_status(percent, _L("Installing"));
        }
        }, cancel_fn);

    if (was_canceled()) {
        update_status(0, _L("Cancelled"));
        wxCommandEvent event(wxEVT_CLOSE_WINDOW);
        event.SetEventObject(m_event_handle);
        wxPostEvent(m_event_handle, event);
        return;
    }

    if (result != 0) {
        update_status(0, _L("Install failed"));
        wxCommandEvent event(EVT_INSTALL_NETWORK_FAILED);
        event.SetEventObject(m_event_handle);
        wxPostEvent(m_event_handle, event);
        return;
    }

    wxCommandEvent event(EVT_UPGRADE_NETWORK_SUCCESS);
    event.SetEventObject(m_event_handle);
    wxPostEvent(m_event_handle, event);
    BOOST_LOG_TRIVIAL(info) << "[UpgradeNetworkJob process]: exit";
    return;
}

void UpgradeNetworkJob::finalize()
{
    if (was_canceled()) return;

    Job::finalize();
}

void UpgradeNetworkJob::set_event_handle(wxWindow *hanle)
{
    m_event_handle = hanle;
}

}} // namespace Slic3r::GUI
