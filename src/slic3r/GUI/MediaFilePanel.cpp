#include "MediaFilePanel.h"
#include "ImageGrid.h"
#include "I18N.hpp"
#include "GUI_App.hpp"

#include "Widgets/Button.hpp"
#include "Widgets/SwitchButton.hpp"
#include "Widgets/Label.hpp"
#include "Printer/PrinterFileSystem.h"

namespace Slic3r {
namespace GUI {

MediaFilePanel::MediaFilePanel(wxWindow * parent)
    : wxPanel(parent, wxID_ANY)
    , m_bmp_loading(this, "media_loading", 0)
    , m_bmp_failed(this, "media_failed", 0)
    , m_bmp_empty(this, "media_empty", 0)
{
    SetBackgroundColour(0xEEEEEE);
    Hide();

    wxBoxSizer * sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer * top_sizer = new wxBoxSizer(wxHORIZONTAL);
    top_sizer->SetMinSize({-1, 75 * em_unit(this) / 10});

    // Time group
    m_time_panel = new ::StaticBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_time_panel->SetBackgroundColor(StateColor());
    m_button_year = new ::Button(m_time_panel, _L("Year"), "", wxBORDER_NONE);
    m_button_month = new ::Button(m_time_panel, _L("Month"), "", wxBORDER_NONE);
    m_button_all = new ::Button(m_time_panel, _L("All Files"), "", wxBORDER_NONE);
    m_button_all->SetFont(Label::Head_14); // sync with m_last_mode
    for (auto b : {m_button_year, m_button_month, m_button_all}) {
        b->SetBackgroundColor(StateColor());
        b->SetTextColor(StateColor(
            std::make_pair(0x3B4446, (int) StateColor::Checked),
            std::make_pair(*wxLIGHT_GREY, (int) StateColor::Hovered),
            std::make_pair(0xACACAC, (int) StateColor::Normal)
        ));
    }

    wxBoxSizer *time_sizer = new wxBoxSizer(wxHORIZONTAL);
    time_sizer->Add(m_button_year, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 24);
    time_sizer->Add(m_button_month, 0, wxALIGN_CENTER_VERTICAL);
    time_sizer->Add(m_button_all, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 24);
    m_time_panel->SetSizer(time_sizer);
    top_sizer->Add(m_time_panel, 1, wxEXPAND);

    // File type
    m_type_panel = new ::StaticBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_type_panel->SetBackgroundColor(*wxWHITE);
    m_type_panel->SetCornerRadius(FromDIP(5));
    m_type_panel->SetMinSize({-1, 48 * em_unit(this) / 10});
    m_button_timelapse = new ::Button(m_type_panel, _L("Timelapse"), "", wxBORDER_NONE);
    m_button_timelapse->SetCanFocus(false);
    m_button_video = new ::Button(m_type_panel, _L("Video"), "", wxBORDER_NONE);
    m_button_video->SetCanFocus(false);

    wxBoxSizer *type_sizer = new wxBoxSizer(wxHORIZONTAL);
    type_sizer->Add(m_button_timelapse, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 24);
    type_sizer->Add(m_button_video, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 24);
    m_type_panel->SetSizer(type_sizer);
    //top_sizer->Add(m_type_panel, 0, wxALIGN_CENTER_VERTICAL);

    // File management
    m_manage_panel      = new ::StaticBox(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_manage_panel->SetBackgroundColor(StateColor());
    m_button_delete     = new ::Button(m_manage_panel, _L("Delete"));
    m_button_delete->SetBackgroundColor(StateColor());
    m_button_delete->SetCanFocus(false);
    m_button_download = new ::Button(m_manage_panel, _L("Download"));
    m_button_download->SetBackgroundColor(StateColor());
    m_button_download->SetCanFocus(false);
    m_button_management = new ::Button(m_manage_panel, _L("Management"));
    m_button_management->SetBackgroundColor(StateColor());

    wxBoxSizer *manage_sizer = new wxBoxSizer(wxHORIZONTAL);
    manage_sizer->AddStretchSpacer(1);
    manage_sizer->Add(m_button_delete, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 24)->Show(false);
    manage_sizer->Add(m_button_download, 0, wxALIGN_CENTER_VERTICAL)->Show(false);
    manage_sizer->Add(m_button_management, 0, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, 24);
    m_manage_panel->SetSizer(manage_sizer);
    top_sizer->Add(m_manage_panel, 1, wxEXPAND);

    sizer->Add(top_sizer, 0, wxEXPAND);

    m_image_grid = new ImageGrid(this);
    m_image_grid->SetStatus(m_bmp_failed.bmp(), _L("No printers."));
    sizer->Add(m_image_grid, 1, wxEXPAND);

    SetSizer(sizer);

    // Time group
    auto time_button_clicked = [this](wxEvent &e) {
        auto mode = PrinterFileSystem::G_NONE;
        if (e.GetEventObject() == m_button_year)
            mode = PrinterFileSystem::G_YEAR;
        else if (e.GetEventObject() == m_button_month)
            mode = PrinterFileSystem::G_MONTH;
        m_image_grid->SetGroupMode(mode);
    };
    m_button_year->Bind(wxEVT_COMMAND_BUTTON_CLICKED, time_button_clicked);
    m_button_month->Bind(wxEVT_COMMAND_BUTTON_CLICKED, time_button_clicked);
    m_button_all->Bind(wxEVT_COMMAND_BUTTON_CLICKED, time_button_clicked);

    {
        wxCommandEvent e(wxEVT_CHECKBOX);
        auto           b = m_button_all;
        e.SetEventObject(b);
        b->GetEventHandler()->ProcessEvent(e);
    }

    // File type
    auto type_button_clicked = [this](wxEvent &e) {
        auto type = PrinterFileSystem::F_TIMELAPSE;
        auto b    = dynamic_cast<Button *>(e.GetEventObject());
        if (b == m_button_video)
            type = PrinterFileSystem::F_VIDEO;
        if (m_last_type == type)
            return;
        m_image_grid->SetFileType(type);
        m_last_type = type;
        {
            wxCommandEvent e(wxEVT_CHECKBOX);
            e.SetEventObject(m_button_timelapse);
            m_button_timelapse->GetEventHandler()->ProcessEvent(e);
        }
        {
            wxCommandEvent e(wxEVT_CHECKBOX);
            e.SetEventObject(m_button_video);
            m_button_video->GetEventHandler()->ProcessEvent(e);
        }
    };
    m_button_video->Bind(wxEVT_COMMAND_BUTTON_CLICKED, type_button_clicked);
    m_button_timelapse->Bind(wxEVT_COMMAND_BUTTON_CLICKED, type_button_clicked);

    {
        wxCommandEvent e(wxEVT_CHECKBOX);
        auto           b = m_button_timelapse;
        e.SetEventObject(b);
        b->GetEventHandler()->ProcessEvent(e);
    }

    // File management
    m_button_management->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](auto &e) {
        e.Skip();
        bool selecting = !m_image_grid->IsSelecting();
        m_image_grid->SetSelecting(selecting);
        m_button_management->SetLabel(selecting ? _L("Finish") : _L("Management"));
        m_manage_panel->GetSizer()->Show(m_button_download, selecting);
        m_manage_panel->GetSizer()->Show(m_button_delete, selecting);
        m_manage_panel->Layout();
    });
    m_button_download->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](auto &e) { m_image_grid->DoActionOnSelection(1); });
    m_button_delete->Bind(wxEVT_COMMAND_BUTTON_CLICKED, [this](auto &e) { m_image_grid->DoActionOnSelection(0); });

    auto onShowHide = [this](auto &e) {
        e.Skip();
        if (m_isBeingDeleted) return;
        auto fs = m_image_grid ? m_image_grid->GetFileSystem() : nullptr;
        if (fs) IsShownOnScreen() ? fs->Start() : fs->Stop();
    };
    Bind(wxEVT_SHOW, onShowHide);
    parent->GetParent()->Bind(wxEVT_SHOW, onShowHide);
}

MediaFilePanel::~MediaFilePanel()
{
    SetMachineObject(nullptr);
}

void MediaFilePanel::SetMachineObject(MachineObject* obj)
{
    std::string machine = obj ? obj->dev_id : "";
    if (obj && obj->is_function_supported(PrinterFunction::FUNC_MEDIA_FILE)) {
        m_lan_mode     = obj->is_lan_mode_printer();
        m_lan_ip       = obj->is_function_supported(PrinterFunction::FUNC_LOCAL_TUNNEL) ? obj->dev_ip : "";
        m_lan_passwd   = obj->access_code;
        m_tutk_support = obj->is_function_supported(PrinterFunction::FUNC_REMOTE_TUNNEL);
    } else {
        m_lan_mode = false;
        m_lan_ip.clear();
        m_lan_passwd.clear();
        m_tutk_support = true;
    }
    if (machine == m_machine)
        return;
    m_machine = machine;
    auto fs = m_image_grid->GetFileSystem();
    if (fs) {
        m_image_grid->SetFileSystem(nullptr);
        fs->Unbind(EVT_MODE_CHANGED, &MediaFilePanel::modeChanged, this);
        fs->Stop(true);
    }
    if (m_machine.empty()) {
        m_image_grid->SetStatus(m_bmp_failed.bmp(), _L("No printers."));    
    } else if (m_lan_ip.empty() && (m_lan_mode && !m_tutk_support)) {
        m_image_grid->SetStatus(m_bmp_failed.bmp(), _L("Not supported."));
    } else {
        boost::shared_ptr<PrinterFileSystem> fs(new PrinterFileSystem);
        m_image_grid->SetFileType(m_last_type);
        m_image_grid->SetFileSystem(fs);
        fs->Bind(EVT_MODE_CHANGED, &MediaFilePanel::modeChanged, this);
        fs->Bind(EVT_STATUS_CHANGED, [this, wfs = boost::weak_ptr(fs)](auto &e) {
            boost::shared_ptr fs(wfs.lock());
            if (m_image_grid->GetFileSystem() != fs) // canceled
                return;
            wxBitmap icon;
            wxString msg;
            switch (e.GetInt()) {
            case PrinterFileSystem::Initializing: icon = m_bmp_loading.bmp(); msg = _L("Initializing..."); fetchUrl(boost::weak_ptr(fs)); break;
            case PrinterFileSystem::Connecting: icon = m_bmp_loading.bmp(); msg = _L("Connecting..."); break;
            case PrinterFileSystem::Failed: icon = m_bmp_failed.bmp(); msg = _L("Connect failed [%d]!"); break;
            case PrinterFileSystem::ListSyncing: icon = m_bmp_loading.bmp(); msg = _L("Loading file list..."); break;
            case PrinterFileSystem::ListReady: icon = m_bmp_empty.bmp(); msg = _L("No files"); break;
            }
            if (fs->GetCount() == 0)
                m_image_grid->SetStatus(icon, msg);
            else
                (void) 0; // TODO: show dialog
        });
        if (IsShown()) fs->Start();
    }
    wxCommandEvent e(EVT_MODE_CHANGED);
    modeChanged(e);
}

void MediaFilePanel::Rescale()
{
    m_bmp_loading.msw_rescale();
    m_bmp_failed.msw_rescale();
    m_bmp_empty.msw_rescale();

    auto top_sizer = GetSizer()->GetItem((size_t) 0)->GetSizer();
    top_sizer->SetMinSize({-1, 75 * em_unit(this) / 10});
    m_button_year->Rescale();
    m_button_month->Rescale();
    m_button_all->Rescale();

    m_button_video->Rescale();
    m_button_timelapse->Rescale();
    m_type_panel->SetMinSize({-1, 48 * em_unit(this) / 10});

    m_button_video->Rescale();
    m_button_timelapse->Rescale();
    m_button_management->Rescale();

    m_image_grid->Rescale();
}

void MediaFilePanel::modeChanged(wxCommandEvent& e1)
{
    e1.Skip();
    auto fs = m_image_grid->GetFileSystem();
    auto mode = fs ? fs->GetGroupMode() : 0;
    if (m_last_mode == mode)
        return;
    ::Button* buttons[] = {m_button_all, m_button_month, m_button_year};
    wxCommandEvent e(wxEVT_CHECKBOX);
    auto b = buttons[m_last_mode];
    b->SetFont(Label::Body_14);
    e.SetEventObject(b);
    b->GetEventHandler()->ProcessEvent(e);
    b = buttons[mode];
    b->SetFont(Label::Head_14);
    e.SetEventObject(b);
    b->GetEventHandler()->ProcessEvent(e);
    m_last_mode = mode;
}

void MediaFilePanel::fetchUrl(boost::weak_ptr<PrinterFileSystem> wfs)
{
    if (!m_lan_ip.empty()) {
       std::string url = "bambu:///local/" + m_lan_ip + ".?port=6000&user=" + m_lan_user + "&passwd=" + m_lan_passwd;
        boost::shared_ptr fs(wfs.lock());
        if (!fs || fs != m_image_grid->GetFileSystem()) return;
        fs->SetUrl(url);
        return;
    }
    if (m_lan_mode && !m_tutk_support) { // not support tutk
        return;
    }
    NetworkAgent *agent = wxGetApp().getAgent();
    if (agent) {
        agent->get_camera_url(m_machine,
            [this, wfs](std::string url) {
            BOOST_LOG_TRIVIAL(info) << "MediaFilePanel::fetchUrl: camera_url: " << url;
            CallAfter([this, url, wfs] {
                boost::shared_ptr fs(wfs.lock());
                if (!fs || fs != m_image_grid->GetFileSystem()) return;
                fs->SetUrl(url);
            });
        });
    }
}

MediaFileFrame::MediaFileFrame(wxWindow* parent)
    : DPIFrame(parent, wxID_ANY, "Media Files", wxDefaultPosition, { 1600, 900 })
{
    m_panel = new MediaFilePanel(this);
    wxBoxSizer * sizer = new wxBoxSizer(wxVERTICAL);
    sizer->Add(m_panel, 1, wxEXPAND);
    SetSizer(sizer);

    Bind(wxEVT_CLOSE_WINDOW, [this](auto & e){
        Hide();
        e.Veto();
    });
}

void MediaFileFrame::on_dpi_changed(const wxRect& suggested_rect) { m_panel->Rescale(); Refresh(); }

}}
