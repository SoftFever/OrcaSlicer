#include "MediaPlayCtrl.h"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/Label.hpp"
#include "GUI_App.hpp"
#include "libslic3r/AppConfig.hpp"
#include "I18N.hpp"

namespace Slic3r {
namespace GUI {

MediaPlayCtrl::MediaPlayCtrl(wxWindow *parent, wxMediaCtrl2 *media_ctrl, const wxPoint &pos, const wxSize &size)
    : wxPanel(parent, wxID_ANY, pos, size)
    , m_media_ctrl(media_ctrl)
{
    SetBackgroundColour(*wxWHITE);
    m_media_ctrl->Bind(wxEVT_MEDIA_STATECHANGED, &MediaPlayCtrl::onStateChanged, this);

    m_button_play = new Button(this, "", "media_play", wxBORDER_NONE);

    m_label_status = new Label(Label::Body_14, this);

    m_button_play->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](auto & e) { TogglePlay(); });

    m_button_play->Bind(wxEVT_RIGHT_UP, [this](auto & e) { m_media_ctrl->Play(); });

    Bind(wxEVT_RIGHT_UP, [this](auto & e) { wxClipboard & c = *wxTheClipboard; if (c.Open()) { c.SetData(new wxTextDataObject(m_url)); c.Close(); } });

    wxBoxSizer * sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(m_button_play, 0, wxEXPAND | wxALIGN_CENTER_VERTICAL | wxALL, 0);
    sizer->AddStretchSpacer(1);
    sizer->Add(m_label_status, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(25));
    SetSizer(sizer);

    m_thread = boost::thread([this] {
        media_proc();
    });
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

void MediaPlayCtrl::SetMachineObject(MachineObject* obj)
{
    std::string machine = obj ? obj->dev_id : "";
    if (machine == m_machine) {
        if (m_last_state == MEDIASTATE_IDLE && m_next_retry.IsValid() && wxDateTime::Now() >= m_next_retry)
            Play();
        return;
    }
    m_machine = machine;
    m_failed_retry = 0;
    if (m_last_state != MEDIASTATE_IDLE)
        Stop();
    //Play();
    SetStatus("");
}

void MediaPlayCtrl::Play()
{
    if (m_machine.empty()) {
        SetStatus(_L("Initialize failed (No Device)!"));
        return;
    }
    if (m_last_state != MEDIASTATE_IDLE) {
        return;
    }
    m_last_state = MEDIASTATE_INITIALIZING;
    m_button_play->SetIcon("media_stop");
    SetStatus(_L("Initializing..."));


    NetworkAgent* agent = wxGetApp().getAgent();
    if (agent) {
            agent->get_camera_url(m_machine, [this](std::string url) {
            BOOST_LOG_TRIVIAL(info) << "camera_url: " << url;
            CallAfter([this, url] {
                m_url = url;
                if (m_last_state == MEDIASTATE_INITIALIZING) {
                    if (url.empty()) {
                        Stop();
                        m_failed_code = 1;
                        SetStatus(_L("Initialize failed [%d]!"));
                    } else {
                        m_last_state = MEDIASTATE_LOADING;
                        SetStatus(_L("Loading..."));
                        if (wxGetApp().app_config->get("dump_video") == "true") {
                            BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl dump video to " << boost::filesystem::current_path();
                            m_url = m_url + "&dump=video.h264";
                        }
                        boost::unique_lock lock(m_mutex);
                        m_tasks.push_back(m_url);
                        m_cond.notify_all();
                    }
                }
            });
        });
    }
}

void MediaPlayCtrl::Stop()
{
    if (m_last_state != MEDIASTATE_IDLE) {
        m_media_ctrl->InvalidateBestSize();
        m_button_play->SetIcon("media_play");
        boost::unique_lock lock(m_mutex);
        m_tasks.push_back("<stop>");
        m_cond.notify_all();
    }
    m_last_state = MEDIASTATE_IDLE;
    SetStatus(_L("Stopped."));
    ++m_failed_retry;
    //m_next_retry = wxDateTime::Now() + wxTimeSpan::Seconds(5 * m_failed_retry);
}

void MediaPlayCtrl::TogglePlay()
{
    if (m_last_state != MEDIASTATE_IDLE)
        Stop();
    else
        Play();
}

void MediaPlayCtrl::SetStatus(wxString const& msg2)
{
    auto msg = wxString::Format(msg2, m_failed_code);
    BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl::SetStatus: " << msg.ToUTF8().data();
#ifdef __WXMSW__
    OutputDebugStringA("MediaPlayCtrl::SetStatus: ");
    OutputDebugStringA(msg.ToUTF8().data());
    OutputDebugStringA("\n");
#endif // __WXMSW__
    m_label_status->SetLabel(msg);
    //m_label_status->SetForegroundColour(!msg.EndsWith("!") ? 0x42AE00 : 0x3B65E9);
    Layout();
}

void MediaPlayCtrl::media_proc()
{
    boost::unique_lock lock(m_mutex);
    while (true) {
        while (m_tasks.empty()) {
            m_cond.wait(lock);
        }
        wxString url = m_tasks.front();
        lock.unlock();
        if (url.IsEmpty()) {
            break;
        }
        else if (url == "<stop>") {
            m_media_ctrl->Stop();
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

void MediaPlayCtrl::onStateChanged(wxMediaEvent& event)
{
    auto last_state = m_last_state;
    auto state = m_media_ctrl->GetState();
    BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl::onStateChanged: " << state << ", last_state: " << last_state;
    if ((int) state < 0)
        return;
    {
        boost::unique_lock lock(m_mutex);
        if (!m_tasks.empty()) {
            BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl::onStateChanged: skip when task not finished";
            return;
        }
    }
    if (last_state == MEDIASTATE_IDLE && state == wxMEDIASTATE_STOPPED) {
        return;
    }
    m_last_state = state;
    if ((last_state == wxMEDIASTATE_PAUSED || last_state == wxMEDIASTATE_PLAYING)  &&
        state == wxMEDIASTATE_STOPPED) {
        Stop();
        return;
    }
    if (last_state == MEDIASTATE_LOADING && state == wxMEDIASTATE_STOPPED) {
        wxSize size = m_media_ctrl->GetVideoSize();
        BOOST_LOG_TRIVIAL(info) << "MediaPlayCtrl::onStateChanged: size: " << size.x << "x" << size.y;
        m_failed_code = m_media_ctrl->GetLastError();
        if (size.GetWidth() > 1000) {
            SetStatus(_L("Playing..."));
            m_failed_retry = 0;
            boost::unique_lock lock(m_mutex);
            m_tasks.push_back("<play>");
            m_cond.notify_all();
        }
        else if (event.GetId()) {
            Stop();
            SetStatus(_L("Load failed [%d]!"));
        } else {
            m_last_state = last_state;
        }
    }
}

}}

void wxMediaCtrl2::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
    if (sizeFlags & wxSIZE_USE_EXISTING) return;
    wxSize size = GetVideoSize();
    if (size.GetWidth() <= 0)
        size = wxSize{16, 9};
    int maxHeight = (width * size.GetHeight() + size.GetHeight() - 1) / size.GetWidth();
    if (maxHeight != GetMaxHeight()) {
        // BOOST_LOG_TRIVIAL(info) << "wxMediaCtrl2::DoSetSize: width: " << width << ", height: " << height << ", maxHeight: " << maxHeight;
        SetMaxSize({-1, maxHeight});
        Slic3r::GUI::wxGetApp().CallAfter([this] {
            if (auto p = GetParent()) {
                p->Layout();
                p->Refresh();
            }
        });
    }
}

