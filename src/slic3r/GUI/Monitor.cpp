#include "Tab.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/Model.hpp"

#include <wx/app.h>
#include <wx/button.h>
#include <wx/scrolwin.h>
#include <wx/sizer.h>

#include <wx/bmpcbox.h>
#include <wx/bmpbuttn.h>
#include <wx/treectrl.h>
#include <wx/imaglist.h>
#include <wx/settings.h>
#include <wx/filedlg.h>
#include <wx/wupdlock.h>
#include <wx/dataview.h>
#include <wx/tglbtn.h>

#include "wxExtensions.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "Widgets/Label.hpp"
#include "format.hpp"
#include "MediaPlayCtrl.h"
#include "MediaFilePanel.h"
#include "Plater.hpp"
#include "BindDialog.hpp"

namespace Slic3r {
namespace GUI {

#define REFRESH_INTERVAL       1000

AddMachinePanel::AddMachinePanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
    : wxPanel(parent, id, pos, size, style)
{
    this->SetBackgroundColour(0xEEEEEE);

    wxBoxSizer* topsizer = new wxBoxSizer(wxVERTICAL);

    topsizer->AddStretchSpacer();

    m_bitmap_empty = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    m_bitmap_empty->SetBitmap(create_scaled_bitmap("monitor_status_empty", nullptr, 250));
    topsizer->Add(m_bitmap_empty, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);
    topsizer->AddSpacer(46);

    wxBoxSizer* horiz_sizer = new wxBoxSizer(wxHORIZONTAL);
    horiz_sizer->Add(0, 0, 538, 0, 0);

    wxBoxSizer* btn_sizer = new wxBoxSizer(wxVERTICAL);
    m_button_add_machine = new Button(this, "", "monitor_add_machine", FromDIP(24));
    m_button_add_machine->SetCornerRadius(FromDIP(12));
    StateColor button_bg(
        std::pair<wxColour, int>(0xCECECE, StateColor::Pressed),
        std::pair<wxColour, int>(0xCECECE, StateColor::Hovered),
        std::pair<wxColour, int>(this->GetBackgroundColour(), StateColor::Normal)
    );
    m_button_add_machine->SetBackgroundColor(button_bg);
    m_button_add_machine->SetBorderColor(0x909090);
    m_button_add_machine->SetMinSize(wxSize(96, 39));
    btn_sizer->Add(m_button_add_machine, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);
    m_staticText_add_machine = new wxStaticText(this, wxID_ANY, wxT("click to add machine"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_add_machine->Wrap(-1);
    m_staticText_add_machine->SetForegroundColour(0x909090);
    btn_sizer->Add(m_staticText_add_machine, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 5);

    horiz_sizer->Add(btn_sizer);
    horiz_sizer->Add(0, 0, 624, 0, 0);

    topsizer->Add(horiz_sizer, 0, wxEXPAND, 0);

    topsizer->AddStretchSpacer();

    this->SetSizer(topsizer);
    this->Layout();

    // Connect Events
    m_button_add_machine->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AddMachinePanel::on_add_machine), NULL, this);
}

void AddMachinePanel::msw_rescale() {

}

void AddMachinePanel::on_add_machine(wxCommandEvent& event) {
    // load a url
}

AddMachinePanel::~AddMachinePanel() {
    m_button_add_machine->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AddMachinePanel::on_add_machine), NULL, this);
}

 MonitorPanel::MonitorPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style),
     m_select_machine(SelectMachinePopup(this))
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    init_bitmap();

    init_tabpanel();

    m_main_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_main_sizer->Add(m_tabpanel, 1, wxEXPAND | wxLEFT, 0);
    SetSizerAndFit(m_main_sizer);

    init_timer();

    m_side_tools->Connect(wxEVT_LEFT_DOWN, wxMouseEventHandler(MonitorPanel::on_printer_clicked), NULL, this);

    Bind(wxEVT_TIMER, &MonitorPanel::on_timer, this);
    Bind(wxEVT_SIZE, &MonitorPanel::on_size, this);
    Bind(wxEVT_COMMAND_CHOICE_SELECTED, &MonitorPanel::on_select_printer, this);

    m_select_machine.Bind(EVT_FINISHED_UPDATE_MACHINE_LIST, [this](wxCommandEvent& e) {
        m_side_tools->start_interval();
    });
}

MonitorPanel::~MonitorPanel()
{
    m_side_tools->Disconnect(wxEVT_LEFT_DOWN, wxMouseEventHandler(MonitorPanel::on_printer_clicked), NULL, this);

    if (m_refresh_timer)
        m_refresh_timer->Stop();
    delete m_refresh_timer;
}

 void MonitorPanel::init_bitmap()
{
    m_signal_strong_img = create_scaled_bitmap("monitor_signal_strong", nullptr, 24);
    m_signal_middle_img = create_scaled_bitmap("monitor_signal_middle", nullptr, 24);
    m_signal_weak_img = create_scaled_bitmap("monitor_signal_weak", nullptr, 24);
    m_signal_no_img   = create_scaled_bitmap("monitor_signal_no", nullptr, 24);
    m_printer_img = create_scaled_bitmap("monitor_printer", nullptr, 26);
    m_arrow_img = create_scaled_bitmap("monitor_arrow",nullptr, 14);
}

 void MonitorPanel::init_timer()
{
    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
    m_refresh_timer->Start(REFRESH_INTERVAL);
    wxPostEvent(this, wxTimerEvent());

    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;
    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_)
        GUI::wxGetApp().sidebar().load_ams_list(obj_->amsList);
}

 void MonitorPanel::init_tabpanel()
{
    m_side_tools = new SideTools(this, wxID_ANY);
    wxBoxSizer* sizer_side_tools = new wxBoxSizer(wxVERTICAL);

   /* auto warning_panel = new wxPanel(this, wxID_ANY);
    warning_panel->SetBackgroundColour(wxColour(255, 111, 0));
    warning_panel->SetSize(wxSize(FromDIP(220), FromDIP(25)));
    warning_panel->SetMinSize(wxSize(FromDIP(220), FromDIP(25)));
    warning_panel->SetMaxSize(wxSize(FromDIP(220), FromDIP(25)));
    sizer_side_tools->Add(warning_panel, 0, wxEXPAND, 0);

    wxBoxSizer *sizer_boxh = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *sizer_boxv = new wxBoxSizer(wxHORIZONTAL);*/

    m_connection_info = new Button(this, "Failed to connect to the printer");
    m_connection_info->SetBackgroundColor(wxColour(255, 111, 0));
    m_connection_info->SetBorderColor(wxColour(255, 111, 0));
    m_connection_info->SetTextColor(*wxWHITE);
    m_connection_info->SetFont(::Label::Body_13);
    m_connection_info->SetCornerRadius(0);
    m_connection_info->SetSize(wxSize(FromDIP(-1), FromDIP(25)));
    m_connection_info->SetMinSize(wxSize(FromDIP(-1), FromDIP(25)));
    m_connection_info->SetMaxSize(wxSize(FromDIP(-1), FromDIP(25)));

    wxBoxSizer* connection_sizer = new wxBoxSizer(wxVERTICAL);
    m_hyperlink = new wxHyperlinkCtrl(m_connection_info, wxID_ANY, _L("Failed to connect to the server"), wxT("https://wiki.bambulab.com/en/software/bambu-studio/failed-to-connect-printer"), wxDefaultPosition, wxDefaultSize, wxHL_DEFAULT_STYLE);
    connection_sizer->Add(m_hyperlink, 0, wxALIGN_CENTER | wxALL, 5);
    m_hyperlink->SetBackgroundColour(wxColour(255, 111, 0));
    m_connection_info->SetSizer(connection_sizer);
    m_connection_info->Layout();
    connection_sizer->Fit(m_connection_info);

    m_connection_info->Hide();


    sizer_side_tools->Add(m_connection_info, 0, wxEXPAND, 0);
    sizer_side_tools->Add(m_side_tools, 1, wxEXPAND, 0);
    m_tabpanel             = new Tabbook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, sizer_side_tools, wxNB_LEFT | wxTAB_TRAVERSAL | wxNB_NOPAGETHEME);
    m_tabpanel->SetBackgroundColour(*wxWHITE);
    m_tabpanel->Bind(wxEVT_BOOKCTRL_PAGE_CHANGED, [this](wxBookCtrlEvent& e) {
        ;
    });

    //m_status_add_machine_panel = new AddMachinePanel(m_tabpanel);
    m_status_info_panel        = new StatusPanel(m_tabpanel);
    m_tabpanel->AddPage(m_status_info_panel, _L("Status"), "", true);

    m_media_file_panel = new MediaFilePanel(m_tabpanel);
    m_tabpanel->AddPage(m_media_file_panel, _L("Media"), "", false);

    m_upgrade_panel = new UpgradePanel(m_tabpanel);
    m_tabpanel->AddPage(m_upgrade_panel, _L("Update"), "", false);

    m_hms_panel = new HMSPanel(m_tabpanel);
    m_tabpanel->AddPage(m_hms_panel, _L("HMS"),"", false);

    m_initialized = true;
    show_status((int)MonitorStatus::MONITOR_NO_PRINTER);
}

void MonitorPanel::set_default()
{
    obj = nullptr;
    last_conn_type = "undefined";

    /* reset status panel*/
    m_status_info_panel->set_default();

    /* reset side tool*/
    //m_bitmap_wifi_signal->SetBitmap(wxNullBitmap);

    wxGetApp().sidebar().load_ams_list({});
}

wxWindow* MonitorPanel::create_side_tools()
{
    //TEST function
    //m_bitmap_wifi_signal->Connect(wxEVT_LEFT_DCLICK, wxMouseEventHandler(MonitorPanel::on_update_all), NULL, this);

    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    auto        panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(0, FromDIP(50)));
    panel->SetBackgroundColour(wxColour(135,206,250));
    panel->SetSizer(sizer);
    sizer->Layout();
    panel->Fit();
    return panel;
}

void MonitorPanel::msw_rescale()
{
    init_bitmap();

    /* side_tool rescale */
    m_side_tools->msw_rescale();

    m_tabpanel->Rescale();
    //m_status_add_machine_panel->msw_rescale();
    m_status_info_panel->msw_rescale();
    m_media_file_panel->Rescale();
    m_upgrade_panel->msw_rescale();
    m_hms_panel->msw_rescale();

    m_connection_info->SetCornerRadius(0);
    m_connection_info->SetSize(wxSize(FromDIP(220), FromDIP(25)));
    m_connection_info->SetMinSize(wxSize(FromDIP(220), FromDIP(25)));
    m_connection_info->SetMaxSize(wxSize(FromDIP(220), FromDIP(25)));

    Layout();
    Refresh();
}

void MonitorPanel::select_machine(std::string machine_sn)
{
    wxCommandEvent *event = new wxCommandEvent(wxEVT_COMMAND_CHOICE_SELECTED);
    event->SetString(machine_sn);
    wxQueueEvent(this, event);
}

void MonitorPanel::on_update_all(wxMouseEvent &event)
{
    update_all();
    Layout();
    Refresh();
}

 void MonitorPanel::on_timer(wxTimerEvent& event)
{
    update_all();

    Layout();
    Refresh();
}

 void MonitorPanel::on_select_printer(wxCommandEvent& event)
{
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    if (!dev->set_selected_machine(event.GetString().ToStdString()))
        return;

    set_default();
    update_all();

    MachineObject *obj_ = dev->get_selected_machine();
    if (obj_)
        GUI::wxGetApp().sidebar().load_ams_list(obj_->amsList);

    Layout();
    Refresh();
}

void MonitorPanel::on_printer_clicked(wxMouseEvent &event)
{
    auto mouse_pos = ClientToScreen(event.GetPosition());
    wxPoint rect = m_side_tools->ClientToScreen(wxPoint(0, 0));

    if (!m_side_tools->is_in_interval()) {
        wxPoint             pos              = m_side_tools->ClientToScreen(wxPoint(0, 0));
        pos.y += m_side_tools->GetRect().height;
        pos.x = pos.x < 0? 0:pos.x;
        m_select_machine.Position(pos, wxSize(0, 0));

#ifdef __linux__
        m_select_machine.SetSize(wxSize(m_side_tools->GetSize().x, -1));
        m_select_machine.SetMaxSize(wxSize(m_side_tools->GetSize().x, -1));
        m_select_machine.SetMinSize(wxSize(m_side_tools->GetSize().x, -1));
#endif

        m_select_machine.Popup();
    }
}

void MonitorPanel::on_size(wxSizeEvent &event)
{
    Layout();
    Refresh();
}

 void MonitorPanel::update_status(MachineObject* obj)
{
    if (!obj) return;

    /* Update Device Info */
    m_side_tools->set_current_printer_name(obj->dev_name);

    // update wifi signal image
    int wifi_signal_val = 0;
    if (!obj->is_connected() || obj->is_connecting()) {
        m_side_tools->set_current_printer_signal(WifiSignal::NONE);
    } else {
        if (!obj->wifi_signal.empty() && boost::ends_with(obj->wifi_signal, "dBm")) {
            try {
                wifi_signal_val = std::stoi(obj->wifi_signal.substr(0, obj->wifi_signal.size() - 3));
            }
            catch (...) {
                ;
            }
            if (wifi_signal_val > -45) {
                m_side_tools->set_current_printer_signal(WifiSignal::STRONG);
            }
            else if (wifi_signal_val <= -45 && wifi_signal_val >= -60) {
                m_side_tools->set_current_printer_signal(WifiSignal::MIDDLE);
            }
            else {
                m_side_tools->set_current_printer_signal(WifiSignal::WEAK);
            }
        }
        else {
            m_side_tools->set_current_printer_signal(WifiSignal::MIDDLE);
        }
    }
}

void MonitorPanel::update_all()
{
    NetworkAgent* m_agent = wxGetApp().getAgent();
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;
    obj = dev->get_selected_machine();

    // check valid machine
    if (obj && dev->get_my_machine(obj->dev_id) == nullptr) {
        dev->set_selected_machine("");
        if (m_agent)
            m_agent->set_user_selected_machine("");
        show_status((int)MONITOR_NO_PRINTER);
        return;
    }

    //BBS check mqtt connections if user is login
    if (wxGetApp().is_user_login()) {
        // check mqtt connection and reconnect if disconnected
        try {
            m_agent->refresh_connection();
        }
        catch (...) {
            ;
        }
    }

    if (obj) {
        wxGetApp().reset_to_active();
        if (obj->connection_type() != last_conn_type) {
            last_conn_type = obj->connection_type();
        }
    }

    m_status_info_panel->obj = obj;
    m_upgrade_panel->update(obj);

    
    m_status_info_panel->m_media_play_ctrl->SetMachineObject(obj);
    m_media_file_panel->SetMachineObject(obj);

    update_status(obj);
    
    if (!obj) {
        show_status((int)MONITOR_NO_PRINTER);
        return;
    }

    if (obj->is_connecting()) {
        show_status(MONITOR_CONNECTING);
        return;
    } else if (!obj->is_connected()) {
        int server_status = 0;
        // only disconnected server in cloud mode
        if (obj->connection_type() != "lan") {
            if (m_agent) {
                server_status = m_agent->is_server_connected() ? 0 : (int)MONITOR_DISCONNECTED_SERVER;
            }
        }
        show_status((int) MONITOR_DISCONNECTED + server_status);
        return;
    }

    show_status(MONITOR_NORMAL);


    if (m_status_info_panel->IsShown()) {
        m_status_info_panel->update(obj);
    }

    if (m_hms_panel->IsShown()) {
        m_hms_panel->update(obj);
    }
#if !BBL_RELEASE_TO_PUBLIC
    if (m_upgrade_panel->IsShown()) {
        m_upgrade_panel->update(obj);
    }
#endif
}

bool MonitorPanel::Show(bool show)
{
#ifdef __APPLE__
    wxGetApp().mainframe->SetMinSize(wxGetApp().plater()->GetMinSize());
#endif

    NetworkAgent* m_agent = wxGetApp().getAgent();
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (show) {
        m_refresh_timer->Stop();
        m_refresh_timer->SetOwner(this);
        m_refresh_timer->Start(REFRESH_INTERVAL);
        wxPostEvent(this, wxTimerEvent());

        if (dev) {
            //set a default machine when obj is null
            obj = dev->get_selected_machine();
            if (obj == nullptr) {
                dev->load_last_machine();
            } else {
                obj->reset_update_time();
            }
        }
    } else {
        m_refresh_timer->Stop();
    }
    return wxPanel::Show(show);
}

void MonitorPanel::update_side_panel()
{
    Slic3r::DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    auto is_next_machine = false;
    if (!dev->get_first_online_user_machine().empty()) {
        wxCommandEvent* event = new wxCommandEvent(wxEVT_COMMAND_CHOICE_SELECTED);
        event->SetString(dev->get_first_online_user_machine());
        wxQueueEvent(this, event);
        is_next_machine = true;
        return;
    }

    if (!is_next_machine) { m_side_tools->set_none_printer_mode(); }
}

void MonitorPanel::show_status(int status)
{
    if (!m_initialized) return;

    if (last_status == status)
        return;
    last_status = status;

    BOOST_LOG_TRIVIAL(info) << "monitor: show_status = " << status;

    if (((status & (int) MonitorStatus::MONITOR_DISCONNECTED) != 0) || ((status & (int) MonitorStatus::MONITOR_DISCONNECTED_SERVER) != 0)) {
        if ((status & (int) MonitorStatus::MONITOR_DISCONNECTED_SERVER))
            m_hyperlink->SetLabel(_L("Failed to connect to the server"));
            //m_connection_info->SetLabel(_L("Failed to connect to the server"));
        else
            m_hyperlink->SetLabel(_L("Failed to connect to the printer"));
            //m_connection_info->SetLabel(_L("Failed to connect to the printer"));

        m_hyperlink->Show();
        m_connection_info->SetLabel(wxEmptyString);
        m_connection_info->Show();
        m_connection_info->SetBackgroundColor(wxColour(255, 111, 0));
        m_connection_info->SetBorderColor(wxColour(255, 111, 0));
#if !BBL_RELEASE_TO_PUBLIC
        m_upgrade_panel->update(nullptr);
#endif
    } else if ((status & (int) MonitorStatus::MONITOR_NORMAL) != 0) {
        m_connection_info->Hide();
    } else if ((status & (int) MonitorStatus::MONITOR_CONNECTING) != 0) {
        m_hyperlink->Hide();
        m_connection_info->SetLabel(_L("Connecting..."));
        m_connection_info->SetBackgroundColor(wxColour(0, 174, 66));
        m_connection_info->SetBorderColor(wxColour(0, 174, 66));
        m_connection_info->Show();
    }

    Freeze();
    if ((status & (int)MonitorStatus::MONITOR_NO_PRINTER) != 0) {
        set_default();
        m_side_tools->set_none_printer_mode();
        m_connection_info->Hide();
        m_status_info_panel->show_status(status);
        m_tabpanel->Refresh();
        m_tabpanel->Layout();
#if !BBL_RELEASE_TO_PUBLIC
        m_upgrade_panel->update(nullptr);
#endif
    } else if (((status & (int)MonitorStatus::MONITOR_NORMAL) != 0)
        || ((status & (int)MonitorStatus::MONITOR_DISCONNECTED) != 0)
        || ((status & (int) MonitorStatus::MONITOR_DISCONNECTED_SERVER) != 0)
        || ((status & (int)MonitorStatus::MONITOR_CONNECTING) != 0)
        ) {
        if (((status & (int) MonitorStatus::MONITOR_DISCONNECTED) != 0)
            || ((status & (int) MonitorStatus::MONITOR_DISCONNECTED_SERVER) != 0)
            || ((status & (int)MonitorStatus::MONITOR_CONNECTING) != 0)) {
            m_side_tools->set_current_printer_signal(WifiSignal::NONE);
            set_default();
        }

        m_status_info_panel->show_status(status);
        m_tabpanel->Refresh();
        m_tabpanel->Layout();
    }
    Layout();
    Thaw();
}

} // GUI
} // Slic3r
