#include "MediaPlayCtrl.h"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/Label.hpp"
#include "GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"
#include "I18N.hpp"
#include "MsgDialog.hpp"
#include "DownloadProgressDialog.hpp"

#include <boost/lexical_cast.hpp>
#include <boost/log/trivial.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/utf8_codecvt.hpp>
#undef pid_t
#include <boost/process.hpp>
#ifdef __WIN32__
#include <boost/process/windows.hpp>
#else
#include <sys/ipc.h>
#include <sys/shm.h>
#endif

#include <wx/clipbrd.h>

namespace Slic3r {
namespace GUI {

MediaPlayCtrl::MediaPlayCtrl(wxWindow *parent, wxMediaCtrl2 *media_ctrl, const wxPoint &pos, const wxSize &size)
    : wxPanel(parent, wxID_ANY, pos, size)
    , m_media_ctrl(media_ctrl)
{
    SetBackgroundColour(*wxWHITE);
    m_media_ctrl->Bind(wxEVT_MEDIA_STATECHANGED, &MediaPlayCtrl::onStateChanged, this);

    m_button_play = new Button(this, "", "media_play", wxBORDER_NONE);
    m_button_play->SetCanFocus(false);

    m_label_status = new Label(this, "");
    m_label_status->SetForegroundColour(wxColour("#323A3C"));

    m_button_play->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](auto &e) { TogglePlay(); });
    m_button_play->Bind(wxEVT_RIGHT_UP, [this](auto & e) { m_media_ctrl->Play(); });
    // m_label_status->Bind(wxEVT_LEFT_UP, [this](auto &e) {
    //     auto url = wxString::Format(L"https://wiki.bambulab.com/%s/software/bambu-studio/faq/live-view", L"en");
    //     wxLaunchDefaultBrowser(url);
    // });

    Bind(wxEVT_RIGHT_UP, [this](auto & e) {
        wxClipboard & c = *wxTheClipboard;
        if (c.Open()) {
            if (wxGetKeyState(WXK_SHIFT)) {
                if (c.IsSupported(wxDF_TEXT)) {
                    wxTextDataObject data;
                    c.GetData(data);
                    Stop();
                    m_url = data.GetText();
                    load();
                }
            } else {
                c.SetData(new wxTextDataObject(m_url));
            }
            c.Close();
        }
    });

    wxBoxSizer * sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_button_play, 0, wxEXPAND | wxALL, 0);
    sizer->AddStretchSpacer(1);
    sizer->Add(m_label_status, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(25));
    SetSizer(sizer);

    m_thread = boost::thread([this] {
        media_proc();
    });

//#if BBL_RELEASE_TO_PUBLIC
//    m_next_retry = wxDateTime::Now();
//#endif

    parent->Bind(wxEVT_SHOW, &MediaPlayCtrl::on_show_hide, this);
    parent->GetParent()->GetParent()->Bind(wxEVT_SHOW, &MediaPlayCtrl::on_show_hide, this);

    m_lan_user = "bblp";
    m_lan_passwd = "bblp";
}

MediaPlayCtrl::~MediaPlayCtrl()
{
    {
        boost::unique_lock lock(m_mutex);
        m_tasks.push_back("<exit>");
        m_cond.notify_all();
    }
    m_thread.join();
}

wxString hide_id_middle_string(wxString const &str, size_t offset = 0, size_t length = -1)
{
    if (length == size_t(-1))
        length = str.Length() - offset;
    if (length <= 8)
        return str;
    return str.Left(offset + 4) + wxString(length - 8, '*') + str.Mid(offset + length - 4);
}

void MediaPlayCtrl::SetMachineObject(MachineObject* obj)
{
    std::string machine = obj ? obj->dev_id : "";
    if (obj) {
        m_camera_exists  = obj->has_ipcam;
        m_dev_ver        = obj->get_ota_version();
        m_lan_mode       = obj->is_lan_mode_printer();
        m_lan_proto      = obj->liveview_local;
        m_remote_support = obj->liveview_remote;
        m_lan_ip         = obj->dev_ip;
        m_lan_passwd     = obj->get_access_code();
        m_device_busy    = obj->is_camera_busy_off();
        m_tutk_state     = obj->tutk_state;
    } else {
        m_camera_exists = false;
        m_lan_mode = false;
        m_lan_proto = MachineObject::LVL_None;
        m_lan_ip.clear();
        m_lan_passwd.clear();
        m_dev_ver.clear();
        m_tutk_state.clear();
        m_remote_support = true;
        m_device_busy = false;
    }
    if (machine == m_machine) {
        if (m_last_state == MEDIASTATE_IDLE && IsEnabled())
            Play();
        return;
    }
    m_machine = machine;
    BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl switch machine: " << hide_id_middle_string(m_machine);
    m_disable_lan = false;
    m_failed_retry = 0;
    m_last_failed_codes.clear();
    m_last_user_play = wxDateTime::Now();
    std::string stream_url;
    if (get_stream_url(&stream_url)) {
        m_streaming = boost::algorithm::contains(stream_url, "device=" + m_machine);
    } else {
        m_streaming = false;
    }
    if (m_last_state != MEDIASTATE_IDLE)
        Stop(" ");
    if (m_next_retry.IsValid()) // Try open 2 seconds later, to avoid state conflict
        m_next_retry = wxDateTime::Now() + wxTimeSpan::Seconds(2);
    else
        SetStatus("", false);
}

wxString hide_passwd(wxString url, std::vector<wxString> const &passwords)
{
    for (auto &p : passwords) {
        auto i = url.find(p);
        if (i == wxString::npos)
            continue;
        auto j = i + p.length();
        if (p[p.length() - 1] == '=') {
            i = j;
            j = url.find('&', i);
            if (j == wxString::npos) j = url.length();
        }
        auto l = size_t(j - i);
        if (j == url.length() || url[j] == '@' || url[j] == '&')
            url.replace(i, l, l, wxUniChar('*'));
    }
    return url;
}

void MediaPlayCtrl::Play()
{
    if (!m_next_retry.IsValid() || wxDateTime::Now() < m_next_retry)
        return;
    if (!IsShownOnScreen())
        return;
    if (m_last_state != MEDIASTATE_IDLE) {
        return;
    }
    m_failed_code = 0;
    if (m_machine.empty()) {
        Stop(_L("Initialize failed (No Device)!"));
        return;
    }
    if (!IsEnabled()) {
        Stop(_L("Initialize failed (Device connection not ready)!"));
        return;
    }
    if (!m_camera_exists) {
        Stop(_L("Initialize failed (No Camera Device)!"));
        return;
    }
    if (m_device_busy) {
        Stop(_L("Printer is busy downloading, Please wait for the downloading to finish."));
        m_failed_retry = 0;
        return;
    }

    m_last_state = MEDIASTATE_INITIALIZING;
    m_button_play->SetIcon("media_stop");
    NetworkAgent *agent = wxGetApp().getAgent();
    std::string  agent_version = agent ? agent->get_version() : "";
    if (m_lan_proto > MachineObject::LVL_None && (m_lan_mode || !m_remote_support) && !m_disable_lan && !m_lan_ip.empty()) {
        m_disable_lan = m_remote_support && !m_lan_mode; // try remote next time
        if (m_lan_proto == MachineObject::LVL_Local)
            m_url = "bambu:///local/" + m_lan_ip + ".?port=6000&user=" + m_lan_user + "&passwd=" + m_lan_passwd;
        else if (m_lan_proto == MachineObject::LVL_Rtsps)
            m_url = "bambu:///rtsps___" + m_lan_user + ":" + m_lan_passwd + "@" + m_lan_ip + "/streaming/live/1?proto=rtsps";
        else if (m_lan_proto == MachineObject::LVL_Rtsp)
            m_url = "bambu:///rtsp___" + m_lan_user + ":" + m_lan_passwd + "@" + m_lan_ip + "/streaming/live/1?proto=rtsp";
        m_url += "&device=" + hide_id_middle_string(m_machine);
        m_url += "&version=" + agent_version;
        m_url += "&dev_ver=" + m_dev_ver;
        BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl: " << hide_passwd(m_url, {m_lan_passwd} );
        load();
        return;
    }

    m_disable_lan = false;
    if (m_lan_ip.empty())
        m_failed_code = -1;

    if (m_lan_mode) {
        Stop(m_lan_proto < 0
                ? _L("Initialize failed (Not supported on the current printer version)!")
                : _L("Initialize failed (Not accessible in LAN-only mode)!"));
        return;
    }
    
    if (!m_remote_support) { // not support tutk
        Stop(m_lan_ip.empty() 
            ? _L("Initialize failed (Missing LAN ip of printer)!") 
            : _L("Initialize failed (Not supported on the current printer version)!"));
        return;
    }

    m_failed_code = 0;
    SetStatus(_L("Initializing..."));

    if (agent) {
        agent->get_camera_url(m_machine, [this, m = m_machine, v = agent_version, dv = m_dev_ver](std::string url) {
            if (boost::algorithm::starts_with(url, "bambu:///")) {
                url += "&device=" + into_u8(hide_id_middle_string(m));
                url += "&version=" + v;
                url += "&dev_ver=" + dv;
            }
            BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl: " << hide_id_middle_string(hide_passwd(url, {"authkey=", "passwd="}), 9, 20);
            CallAfter([this, m, url] {
                if (m != m_machine) {
                    BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl drop late ttcode for machine: " << m;
                    return;
                }
                m_url = url;
                if (m_last_state == MEDIASTATE_INITIALIZING) {
                    if (url.empty() || !boost::algorithm::starts_with(url, "bambu:///")) {
                        m_failed_code = 3;
                        Stop(wxString::Format(_L("Initialize failed (%s)!"), url.empty() ? _L("Network unreachable") : from_u8(url)));
                    } else {
                        load();
                    }
                } else {
                    BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl drop late ttcode for state: " << m_last_state;
                }
            });
        });
    }
}

void MediaPlayCtrl::Stop(wxString const &msg)
{
    int last_state = m_last_state;

    if (m_last_state != MEDIASTATE_IDLE) {
        m_media_ctrl->InvalidateBestSize();
        m_button_play->SetIcon("media_play");
        boost::unique_lock lock(m_mutex);
        m_tasks.push_back("<stop>");
        m_cond.notify_all();
        m_last_state = MEDIASTATE_IDLE;
        if (!msg.IsEmpty())
            SetStatus(msg, false);
        else if (m_failed_code)
            SetStatus(_L("Stopped [%d]!"), true);
        else
            SetStatus(_L("Stopped."), false);
        if (m_failed_code >= 100) // not keep retry on local error
            m_next_retry = wxDateTime();
    } else if (!msg.IsEmpty()) {
        SetStatus(msg, false);
    } else {
        m_failed_code = 0;
    }


    bool remote = m_url.find("/local/") == wxString::npos && m_url.find("/rtsp") == wxString::npos;
    if (last_state != wxMEDIASTATE_PLAYING && m_failed_code != 0 
            && m_last_failed_codes.find(m_failed_code) == m_last_failed_codes.end()
            && (m_user_triggered || m_failed_retry > 3)) {
        m_last_failed_codes.insert(m_failed_code);
    }

    m_url.clear();
    ++m_failed_retry;
    if (m_failed_code < 0 && last_state != wxMEDIASTATE_PLAYING && !remote && (m_failed_retry > 1 || m_user_triggered)) {
        m_next_retry = wxDateTime(); // stop retry
        if (wxGetApp().show_modal_ip_address_enter_dialog(_L("LAN Connection Failed (Failed to start liveview)"))) {
            m_failed_retry = 0;
            m_user_triggered = true;
            if (m_last_user_play + wxTimeSpan::Minutes(5) < wxDateTime::Now()) {
                m_last_failed_codes.clear();
                m_last_user_play = wxDateTime::Now();
            }
            m_next_retry   = wxDateTime::Now();
            return;
        }
    }
    m_user_triggered = false;
    if (m_next_retry.IsValid())
        m_next_retry = wxDateTime::Now() + wxTimeSpan::Seconds(5 * m_failed_retry);
}

void MediaPlayCtrl::TogglePlay()
{
    BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl::TogglePlay";
    if (m_last_state != MEDIASTATE_IDLE) {
        m_next_retry = wxDateTime();
        Stop();
    } else {
        m_failed_retry = 0;
        m_user_triggered = true;
        if (m_last_user_play + wxTimeSpan::Minutes(5) < wxDateTime::Now()) {
            m_last_failed_codes.clear();
            m_last_user_play = wxDateTime::Now();
        }
        m_next_retry = wxDateTime::Now();
        Play();
    }
}

void MediaPlayCtrl::ToggleStream()
{
    std::string file_url = data_dir() + "/cameratools/url.txt";
    if (m_streaming) {
        boost::nowide::ofstream file(file_url);
        file.close();
        m_streaming = false;
        return;
    } else if (!boost::filesystem::exists(file_url)) {
        boost::nowide::ofstream file(file_url);
        file.close();
    }
    std::string url;
    if (!get_stream_url(&url)) {
        // create stream pipeline
        bool need_install = false;
        if (!start_stream_service(&need_install)) {
            if (!need_install) return;
            auto res = MessageDialog(this->GetParent(), _L("Virtual Camera Tools is required for this task!\nDo you want to install them?"), _L("Info"),
                                    wxOK | wxCANCEL).ShowModal();
            if (res == wxID_OK) {
                // download tools
                struct DownloadProgressDialog2 : DownloadProgressDialog
                {
                    MediaPlayCtrl *ctrl;
                    DownloadProgressDialog2(MediaPlayCtrl *ctrl) : DownloadProgressDialog(_L("Downloading Virtual Camera Tools")), ctrl(ctrl) {}
                    struct UpgradeNetworkJob2 : UpgradeNetworkJob
                    {
                        UpgradeNetworkJob2() {
                            name         = "cameratools";
                            package_name = "camera_tools.zip";
                        }
                    };
                    std::unique_ptr<UpgradeNetworkJob> make_job() override
                    { return std::make_unique<UpgradeNetworkJob2>(); }
                    void                               on_finish() override
                    {
                        ctrl->CallAfter([ctrl = this->ctrl] { ctrl->ToggleStream(); });
                        EndModal(wxID_CLOSE);
                    }
                };
                DownloadProgressDialog2 dlg(this);
                dlg.ShowModal();
            }
            return;
        }
    }
    if (!url.empty() && wxGetApp().app_config->get("not_show_vcamera_stop_prev") != "1") {
        MessageDialog dlg(this->GetParent(), _L("Another virtual camera is running.\nOrca Slicer supports only a single virtual camera.\nDo you want to stop this virtual camera?"), _L("Warning"),
                                 wxYES | wxCANCEL | wxICON_INFORMATION);
        dlg.show_dsa_button();
        auto          res = dlg.ShowModal();
        if (dlg.get_checkbox_state())
            wxGetApp().app_config->set("not_show_vcamera_stop_prev", "1");
        if (res == wxID_CANCEL) return;
    }
    if (m_lan_proto > MachineObject::LVL_None && (m_lan_mode || !m_remote_support) && !m_disable_lan && !m_lan_ip.empty()) {
        std::string url;
        if (m_lan_proto == MachineObject::LVL_Local)
            url = "bambu:///local/" + m_lan_ip + ".?port=6000&user=" + m_lan_user + "&passwd=" + m_lan_passwd;
        else if (m_lan_proto == MachineObject::LVL_Rtsps)
            url = "bambu:///rtsps___" + m_lan_user + ":" + m_lan_passwd + "@" + m_lan_ip + "/streaming/live/1?proto=rtsps";
        else if (m_lan_proto == MachineObject::LVL_Rtsp)
            url = "bambu:///rtsp___" + m_lan_user + ":" + m_lan_passwd + "@" + m_lan_ip + "/streaming/live/1?proto=rtsp";
        url += "&device=" + into_u8(hide_id_middle_string(m_machine));
        url += "&dev_ver=" + m_dev_ver;
        BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl::ToggleStream: " << hide_passwd(url, {m_lan_passwd});
        std::string             file_url = data_dir() + "/cameratools/url.txt";
        boost::nowide::ofstream file(file_url);
        auto                    url2 = encode_path(url.c_str());
        file.write(url2.c_str(), url2.size());
        file.close();
        m_streaming = true;
        return;
    }
    NetworkAgent *agent = wxGetApp().getAgent();
    if (!agent) return;
    agent->get_camera_url(m_machine, [this, m = m_machine, v = agent->get_version(), dv = m_dev_ver](std::string url) {
        if (boost::algorithm::starts_with(url, "bambu:///")) {
            url += "&device=" + m;
            url += "&version=" + v;
            url += "&dev_ver=" + dv;
        }
        BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl::ToggleStream: " << hide_id_middle_string(hide_passwd(url, {"authkey=", "passwd="}), 9, 20);
        CallAfter([this, m, url] {
            if (m != m_machine) return;
            if (url.empty() || !boost::algorithm::starts_with(url, "bambu:///")) {
                MessageDialog(this->GetParent(), wxString::Format(_L("Virtual camera initialize failed (%s)!"), url.empty() ? _L("Network unreachable") : from_u8(url)), _L("Information"),
                              wxICON_INFORMATION)
                    .ShowModal();
                return;
            }
            std::string             file_url = data_dir() + "/cameratools/url.txt";
            boost::nowide::ofstream file(file_url);
            auto                    url2 = encode_path(url.c_str());
            file.write(url2.c_str(), url2.size());
            file.close();
            m_streaming = true;
        });
    });
}

void MediaPlayCtrl::msw_rescale() { 
    m_button_play->Rescale(); 
}

void MediaPlayCtrl::onStateChanged(wxMediaEvent &event)
{
    auto last_state = m_last_state;
    auto state      = m_media_ctrl->GetState();
    BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl::onStateChanged: " << state << ", last_state: " << last_state;
    if ((int) state < 0) return;
    {
        boost::unique_lock lock(m_mutex);
        if (!m_tasks.empty()) {
            BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl::onStateChanged: skip when task not finished";
            return;
        }
    }
    if ((last_state == MEDIASTATE_IDLE || last_state == MEDIASTATE_INITIALIZING) && state == wxMEDIASTATE_STOPPED) { return; }
    if ((last_state == wxMEDIASTATE_PAUSED || last_state == wxMEDIASTATE_PLAYING) && state == wxMEDIASTATE_STOPPED) {
        m_failed_code = m_media_ctrl->GetLastError();
        Stop();
        return;
    }
    if (last_state == MEDIASTATE_LOADING && (state == wxMEDIASTATE_STOPPED || state == wxMEDIASTATE_PAUSED)) {
        wxSize size = m_media_ctrl->GetVideoSize();
        BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl::onStateChanged: size: " << size.x << "x" << size.y;
        m_failed_code = m_media_ctrl->GetLastError();
        if (size.GetWidth() >= 320) {
            m_last_state = state;
            SetStatus(_L("Playing..."), false);


            m_failed_retry = 0;
            m_failed_code  = 0;
            m_disable_lan = false;
            boost::unique_lock lock(m_mutex);
            m_tasks.push_back("<play>");
            m_cond.notify_all();
        } else if (event.GetId()) {
            if (m_failed_code == 0)
                m_failed_code = 2;
            Stop(_L("Load failed [%d]!"));
        }
    } else {
        m_last_state = state;
    }
}

void MediaPlayCtrl::SetStatus(wxString const &msg2, bool hyperlink)
{
    auto msg = wxString::Format(msg2, m_failed_code);
    BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl::SetStatus: " << msg.ToUTF8().data();
#ifdef __WXMSW__
    OutputDebugStringA("MediaPlayCtrl::SetStatus: ");
    OutputDebugStringA(msg.ToUTF8().data());
    OutputDebugStringA("\n");
#endif // __WXMSW__
    m_label_status->SetLabel(msg);
    long style = m_label_status->GetWindowStyle() & ~LB_HYPERLINK;
    if (hyperlink) {
        style |= LB_HYPERLINK;
    }
    m_label_status->SetWindowStyle(style);
    m_label_status->InvalidateBestSize();
    Layout();
}

bool MediaPlayCtrl::IsStreaming() const { return m_streaming; }

void MediaPlayCtrl::load()
{
    m_last_state = MEDIASTATE_LOADING;
    SetStatus(_L("Loading..."));
    if (wxGetApp().app_config->get("internal_developer_mode") == "true") {
        std::string file_h264 = data_dir() + "/video.h264";
        std::string file_info = data_dir() + "/video.info";
        BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl dump video to " << file_h264;
        // closed by BambuSource
        FILE *dump_h264_file = boost::nowide::fopen(file_h264.c_str(), "wb");
        FILE *dump_info_file = boost::nowide::fopen(file_info.c_str(), "wb");
        m_url                = m_url + "&dump_h264=" + boost::lexical_cast<std::string>(dump_h264_file);
        m_url                = m_url + "&dump_info=" + boost::lexical_cast<std::string>(dump_info_file);
    }
    boost::unique_lock lock(m_mutex);
    m_tasks.push_back(m_url);
    m_cond.notify_all();
}

void MediaPlayCtrl::on_show_hide(wxShowEvent &evt)
{
    evt.Skip();
    if (m_isBeingDeleted) return;
    m_failed_retry = 0;
    if (m_next_retry.IsValid()) // Try open 2 seconds later, to avoid quick play/stop
        m_next_retry = wxDateTime::Now() + wxTimeSpan::Seconds(2);
    IsShownOnScreen() ? Play() : Stop();
}

void MediaPlayCtrl::media_proc()
{
    boost::unique_lock lock(m_mutex);
    while (true) {
        while (m_tasks.empty()) {
            m_cond.wait(lock);
        }
        wxString url = m_tasks.front();
        if (m_tasks.size() >= 2 && !url.IsEmpty() && url[0] != '<' && m_tasks[1] == "<stop>") {
            BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl: busy skip url: " << hide_id_middle_string(hide_passwd(url, {"authkey=", "passwd=", m_lan_passwd}), 9, 20);
            m_tasks.pop_front();
            m_tasks.pop_front();
            continue;
        }
        lock.unlock();
        if (url.IsEmpty()) {
            break;
        }
        else if (url == "<stop>") {
            BOOST_LOG_TRIVIAL(info) <<  "MediaPlayCtrl: start stop";
            m_media_ctrl->Stop();
            BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl: end stop";
        }
        else if (url == "<exit>") {
            break;
        }
        else if (url == "<play>") {
            m_media_ctrl->Play();
        }
        else {
            BOOST_LOG_TRIVIAL(info) <<  "MediaPlayCtrl: start load";
            m_media_ctrl->Load(wxURI(url));
            BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl: end load";
        }
        lock.lock();
        m_tasks.pop_front();
        wxMediaEvent theEvent(wxEVT_MEDIA_STATECHANGED, m_media_ctrl->GetId());
        theEvent.SetId(0);
        m_media_ctrl->GetEventHandler()->AddPendingEvent(theEvent);
    }
}

bool MediaPlayCtrl::start_stream_service(bool *need_install)
{
#ifdef __WIN32__
    std::string file_source = data_dir() + "\\cameratools\\bambu_source.exe";
    std::string file_ffmpeg = data_dir() + "\\cameratools\\ffmpeg.exe";
    std::string file_ff_cfg = data_dir() + "\\cameratools\\ffmpeg.cfg";
#else
    std::string file_source = data_dir() + "/cameratools/bambu_source";
    std::string file_ffmpeg = data_dir() + "/cameratools/ffmpeg";
    std::string file_ff_cfg = data_dir() + "/cameratools/ffmpeg.cfg";
#endif
    if (!boost::filesystem::exists(file_source) || !boost::filesystem::exists(file_ffmpeg) || !boost::filesystem::exists(file_ff_cfg)) {
        if (need_install) *need_install = true;
        return false;
    }
    std::string file_url  = data_dir() + "/cameratools/url.txt";
    if (!boost::filesystem::exists(file_url)) {
        boost::nowide::ofstream file(file_url);
        file.close();
    }
    wxString file_url2 = L"bambu:///camera/" + from_u8(file_url);
    file_url2.Replace("\\", "/");
    file_url2 = wxURI(file_url2).BuildURI();
    try {
        std::string configs;
        load_string_file(file_ff_cfg, configs);
        std::vector<std::string> configss;
        boost::algorithm::split(configss, configs, boost::algorithm::is_any_of("\r\n"));
        configss.erase(std::remove(configss.begin(), configss.end(), std::string()), configss.end());
        boost::process::pipe intermediate;
        boost::filesystem::path start_dir(boost::filesystem::path(data_dir()) / "plugins");
#ifdef __WXMSW__
        start_dir = boost::filesystem::path(data_dir()) / "cameratools";
        std::string file_dll = data_dir() + "/cameratools/BambuSource.dll";
        std::string file_dll2 = data_dir() + "/plugins/BambuSource.dll";
        if (!boost::filesystem::exists(file_dll) || boost::filesystem::last_write_time(file_dll) != boost::filesystem::last_write_time(file_dll2))
            boost::filesystem::copy_file(file_dll2, file_dll, boost::filesystem::copy_option::overwrite_if_exists);
        static std::locale tmp = std::locale(std::locale(), new boost::nowide::utf8_codecvt<wchar_t>());
        boost::process::imbue(tmp);
        boost::process::child process_source(file_source, into_u8(file_url2), boost::process::start_dir(start_dir), boost::process::windows::create_no_window, 
                                             boost::process::std_out > intermediate, boost::process::limit_handles);
        boost::process::child process_ffmpeg(file_ffmpeg, configss, boost::process::windows::create_no_window, 
                                             boost::process::std_in < intermediate, boost::process::limit_handles);
#else
        boost::filesystem::permissions(file_source, boost::filesystem::owner_exe | boost::filesystem::add_perms);
        boost::filesystem::permissions(file_ffmpeg, boost::filesystem::owner_exe | boost::filesystem::add_perms);
        boost::process::child process_source(file_source, file_url2.data().AsInternal(), boost::process::start_dir(start_dir), 
                                             boost::process::std_out > intermediate, boost::process::limit_handles);
        boost::process::child process_ffmpeg(file_ffmpeg, configss, boost::process::std_in < intermediate, boost::process::limit_handles);
#endif
        process_source.detach();
        process_ffmpeg.detach();
    } catch (std::exception &e) {
        BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl failed to start camera stream: " << e.what();
        return false;
    }
    return true;
}

bool MediaPlayCtrl::get_stream_url(std::string *url)
{
#ifdef __WIN32__
    HANDLE shm = ::OpenFileMapping(FILE_MAP_READ, FALSE, L"bambu_stream_url");
    if (shm == NULL) return false;
    if (url) {
        char *addr = (char *) MapViewOfFile(shm, FILE_MAP_READ, 0, 0, 0);
        if (addr) {
            *url = addr;
            UnmapViewOfFile(addr);
            url = nullptr;
        }
    }
    CloseHandle(shm);
#else
    std::string file_url = data_dir() + "/cameratools/url.txt";
    key_t key = ::ftok(file_url.c_str(), 1000);
    int shm = ::shmget(key, 1024, 0);
    if (shm == -1) return false;
    struct shmid_ds ds;
    ::shmctl(shm, IPC_STAT, &ds);
    if (ds.shm_nattch == 0) {
        return false;
    }
    if (url) {
        char *addr = (char *) ::shmat(shm, nullptr, 0);
        if (addr != (void*) -1) {
            *url = addr;
            ::shmdt(addr);
            url = nullptr;
        }
    }
#endif
    return url == nullptr;
}

}}

void wxMediaCtrl2::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
#ifdef __WXMAC__
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
#else
    wxMediaCtrl::DoSetSize(x, y, width, height, sizeFlags);
#endif
    //if (sizeFlags & wxSIZE_USE_EXISTING) return;
    //wxSize size = GetVideoSize();
    //if (size.GetWidth() <= 0)
    //    size = wxSize{16, 9};
    //int maxHeight = (width * size.GetHeight() + size.GetHeight() - 1) / size.GetWidth();
    //if (maxHeight != GetMaxHeight()) {
    //    // BOOST_LOG_TRIVIAL(info) << "wxMediaCtrl2::DoSetSize: width: " << width << ", height: " << height << ", maxHeight: " << maxHeight;
    //    SetMaxSize({-1, maxHeight});
    //    CallAfter([this] {
    //        if (auto p = GetParent()) {
    //            p->Layout();
    //            p->Refresh();
    //        }
    //    });
    //}
}

