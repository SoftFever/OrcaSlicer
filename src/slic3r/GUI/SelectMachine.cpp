#include "SelectMachine.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Thread.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Preview.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "Widgets/RoundedRectangle.hpp"
#include "Widgets/StaticBox.hpp"
#include "ConnectPrinter.hpp"

#include <wx/progdlg.h>
#include <wx/clipbrd.h>
#include <wx/dcgraph.h>
#include <miniz.h>
#include <algorithm>
#include "Plater.hpp"
#include "BitmapCache.hpp"
#include "BindDialog.hpp"

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_UPDATE_WINDOWS_POSITION, wxCommandEvent);
wxDEFINE_EVENT(EVT_FINISHED_UPDATE_MACHINE_LIST, wxCommandEvent);
wxDEFINE_EVENT(EVT_UPDATE_USER_MACHINE_LIST, wxCommandEvent);
wxDEFINE_EVENT(EVT_PRINT_JOB_CANCEL, wxCommandEvent);
wxDEFINE_EVENT(EVT_BIND_MACHINE, wxCommandEvent);
wxDEFINE_EVENT(EVT_UNBIND_MACHINE, wxCommandEvent);
wxDEFINE_EVENT(EVT_DISSMISS_MACHINE_LIST, wxCommandEvent);
wxDEFINE_EVENT(EVT_CONNECT_LAN_PRINT, wxCommandEvent);
wxDEFINE_EVENT(EVT_EDIT_PRINT_NAME, wxCommandEvent);

#define INITIAL_NUMBER_OF_MACHINES 0
#define LIST_REFRESH_INTERVAL 200
#define MACHINE_LIST_REFRESH_INTERVAL 2000

#define WRAP_GAP FromDIP(10)

static wxString task_canceled_text = _L("Task canceled");


std::string get_print_status_info(PrintDialogStatus status)
{
    switch(status) {
    case PrintStatusInit:
        return "PrintStatusInit";
    case PrintStatusNoUserLogin:
        return "PrintStatusNoUserLogin";
    case PrintStatusInvalidPrinter:
        return "PrintStatusInvalidPrinter";
    case PrintStatusConnectingServer:
        return "PrintStatusConnectingServer";
    case PrintStatusReading:
        return "PrintStatusReading";
    case PrintStatusReadingFinished:
        return "PrintStatusReadingFinished";
    case PrintStatusReadingTimeout:
        return "PrintStatusReadingTimeout";
    case PrintStatusInUpgrading:
        return "PrintStatusInUpgrading";
    case PrintStatusNeedUpgradingAms:
        return "PrintStatusNeedUpgradingAms";
    case PrintStatusInSystemPrinting:
        return "PrintStatusInSystemPrinting";
    case PrintStatusInPrinting:
        return "PrintStatusInPrinting";
    case PrintStatusDisableAms:
        return "PrintStatusDisableAms";
    case PrintStatusAmsMappingSuccess:
        return "PrintStatusAmsMappingSuccess";
    case PrintStatusAmsMappingInvalid:
        return "PrintStatusAmsMappingInvalid";
    case PrintStatusAmsMappingU0Invalid:
        return "PrintStatusAmsMappingU0Invalid";
    case PrintStatusAmsMappingValid:
        return "PrintStatusAmsMappingValid";
    case PrintStatusAmsMappingByOrder:
        return "PrintStatusAmsMappingByOrder";
    case PrintStatusRefreshingMachineList:
        return "PrintStatusRefreshingMachineList";
    case PrintStatusSending:
        return "PrintStatusSending";
    case PrintStatusSendingCanceled:
        return "PrintStatusSendingCanceled";
    case PrintStatusLanModeNoSdcard:
        return "PrintStatusLanModeNoSdcard";
    case PrintStatusNoSdcard:
        return "PrintStatusNoSdcard";
    case PrintStatusTimelapseNoSdcard:
        return "PrintStatusTimelapseNoSdcard";
    }
    return "unknown";
}


MachineListModel::MachineListModel() : wxDataViewVirtualListModel(INITIAL_NUMBER_OF_MACHINES) { ; }

void MachineListModel::display_machines(std::map<std::string, MachineObject *> list)
{
    for (int i = 0; i < Col_Max; i++) { m_values[i].clear(); }

    std::vector<MachineObject *>                     list_array;
    std::map<std::string, MachineObject *>::iterator it;
    for (it = list.begin(); it != list.end(); it++) { list_array.push_back(it->second); }

    std::sort(list_array.begin(), list_array.end(), [](MachineObject *obj1, MachineObject *obj2) { return obj1->dev_name < obj2->dev_name; });

    std::vector<MachineObject *>::iterator iter;
    for (iter = list_array.begin(); iter != list_array.end(); iter++) { this->add_machine(*iter, false); }
    Reset(list_array.size());
}

void MachineListModel::add_machine(MachineObject *obj, bool reset)
{
    m_values[Col_MachineName].Add(from_u8(obj->dev_name));
    m_values[Col_MachineSN].Add(from_u8(obj->dev_id));
    m_values[Col_MachinePrintingStatus].Add(from_u8(obj->print_status));
    m_values[Col_MachineIPAddress].Add(from_u8(obj->dev_ip));
    m_values[Col_MachineConnection].Add(obj->is_online() ? _L("Online") : _L("Offline"));
    if (reset) Reset(m_values[Col_MachineName].GetCount());
}

int MachineListModel::find_row_by_sn(wxString sn)
{
    wxVariant val;
    for (int i = 0; i < this->GetCount(); i++) {
        GetValueByRow(val, i, Col_MachineSN);
        if (val == sn) { return i; }
    }

    return -1;
}

void MachineListModel::GetValueByRow(wxVariant &variant, unsigned int row, unsigned int col) const
{
    if (row > m_values[col].GetCount())
        variant = wxString::Format("virtual row %d", row);
    else
        variant = m_values[col][row];
}

bool MachineListModel::GetAttrByRow(unsigned int row, unsigned int col, wxDataViewItemAttr &attr) const { return true; }

bool MachineListModel::SetValueByRow(const wxVariant &variant, unsigned int row, unsigned int col)
{
    if (row >= m_values[col].GetCount()) return false;
    m_values[col][row] = variant.GetString();
    return true;
}

MachineObjectPanel::MachineObjectPanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style, const wxString &name)
{
    wxPanel::Create(parent, id, pos, SELECT_MACHINE_ITEM_SIZE, style, name);
    Bind(wxEVT_PAINT, &MachineObjectPanel::OnPaint, this);

    SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));

    m_unbind_img        = ScalableBitmap(this, "unbind", 18);
    m_edit_name_img     = ScalableBitmap(this, "edit_button", 18);
    m_select_unbind_img = ScalableBitmap(this, "unbind_selected", 18);

    m_printer_status_offline = ScalableBitmap(this, "printer_status_offline", 12);
    m_printer_status_busy    = ScalableBitmap(this, "printer_status_busy", 12);
    m_printer_status_idle    = ScalableBitmap(this, "printer_status_idle", 12);
    m_printer_status_lock    = ScalableBitmap(this, "printer_status_lock", 16);
    m_printer_in_lan         = ScalableBitmap(this, "printer_in_lan", 16);

    this->Bind(wxEVT_ENTER_WINDOW, &MachineObjectPanel::on_mouse_enter, this);
    this->Bind(wxEVT_LEAVE_WINDOW, &MachineObjectPanel::on_mouse_leave, this);
    this->Bind(wxEVT_LEFT_UP, &MachineObjectPanel::on_mouse_left_up, this);
}

MachineObjectPanel::~MachineObjectPanel() {}

void MachineObjectPanel::show_bind_dialog()
{
    if (wxGetApp().is_user_login()) {
        BindMachineDialog dlg;
        dlg.update_machine_info(m_info);
        dlg.ShowModal();
    }
}

void MachineObjectPanel::set_printer_state(PrinterState state)
{
    m_state = state;
    Refresh();
}

void MachineObjectPanel::show_edit_printer_name(bool show)
{
    m_show_edit = show;
    Refresh();
}

void MachineObjectPanel::show_printer_bind(bool show, PrinterBindState state)
{
    m_show_bind   = show;
    m_bind_state  = state;
    Refresh();
}

void MachineObjectPanel::OnPaint(wxPaintEvent &event)
{
    wxPaintDC dc(this);
    doRender(dc);
}

void MachineObjectPanel::render(wxDC &dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void MachineObjectPanel::doRender(wxDC &dc)
{
    auto   left = 10;
    wxSize size = GetSize();
    dc.SetPen(*wxTRANSPARENT_PEN);

    auto dwbitmap = m_printer_status_offline;
    if (m_state == PrinterState::IDLE) { dwbitmap = m_printer_status_idle; }
    if (m_state == PrinterState::BUSY) { dwbitmap = m_printer_status_busy; }
    if (m_state == PrinterState::OFFLINE) { dwbitmap = m_printer_status_offline; }
    if (m_state == PrinterState::LOCK) { dwbitmap = m_printer_status_lock; }
    if (m_state == PrinterState::IN_LAN) { dwbitmap = m_printer_in_lan; }

    // dc.DrawCircle(left, size.y / 2, 3);
    dc.DrawBitmap(dwbitmap.bmp(), wxPoint(left, (size.y - dwbitmap.GetBmpSize().y) / 2));

    left += dwbitmap.GetBmpSize().x + 8;
    dc.SetFont(Label::Body_13);
    dc.SetBackgroundMode(wxTRANSPARENT);
    dc.SetTextForeground(StateColor::darkModeColorFor(SELECT_MACHINE_GREY900));
    wxString dev_name = "";
    if (m_info) {
        dev_name = from_u8(m_info->dev_name);
    }
    auto        sizet        = dc.GetTextExtent(dev_name);
    auto        text_end     = size.x - m_unbind_img.GetBmpSize().x - 30;
    wxString finally_name =  dev_name;
    if (sizet.x > (text_end - left)) {
        auto limit_width = text_end - left - dc.GetTextExtent("...").x - 15;
        for (auto i = 0; i < dev_name.length(); i++) {
            auto curr_width = dc.GetTextExtent(dev_name.substr(0, i));
            if (curr_width.x >= limit_width) {
                finally_name = dev_name.substr(0, i) + "...";
                break;
            }
        }
    }

    dc.DrawText(finally_name, wxPoint(left, (size.y - sizet.y) / 2));

    if (m_hover) {
        dc.SetPen(SELECT_MACHINE_BRAND);
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(0, 0, size.x, size.y);

        if (m_show_bind) {
            if (m_bind_state == ALLOW_UNBIND) {
                left = size.x - m_unbind_img.GetBmpSize().x - 6;
                dc.DrawBitmap(m_unbind_img.bmp(), left, (size.y - m_unbind_img.GetBmpSize().y) / 2);
            }
        }

        if (m_show_edit) {
            left = size.x - m_unbind_img.GetBmpSize().x - 6 - m_edit_name_img.GetBmpSize().x - 6;
            dc.DrawBitmap(m_edit_name_img.bmp(), left, (size.y - m_edit_name_img.GetBmpSize().y) / 2);
        }
    }
}

void MachineObjectPanel::update_machine_info(MachineObject *info, bool is_my_devices)
{
    m_info = info;
    m_is_my_devices = is_my_devices;
    Refresh();
}

void MachineObjectPanel::on_mouse_enter(wxMouseEvent &evt)
{
    m_hover = true;
    Refresh();
}

void MachineObjectPanel::on_mouse_leave(wxMouseEvent &evt)
{
    m_hover = false;
    Refresh();
}

void MachineObjectPanel::on_mouse_left_up(wxMouseEvent &evt)
{
    if (m_is_my_devices) {
        // show edit
        if (m_show_edit) {
            auto edit_left   = GetSize().x - m_unbind_img.GetBmpSize().x - 6 - m_edit_name_img.GetBmpSize().x - 6;
            auto edit_right  = edit_left + m_edit_name_img.GetBmpSize().x;
            auto edit_top    = (GetSize().y - m_edit_name_img.GetBmpSize().y) / 2;
            auto edit_bottom = (GetSize().y - m_edit_name_img.GetBmpSize().y) / 2 + m_edit_name_img.GetBmpSize().y;
            if ((evt.GetPosition().x >= edit_left && evt.GetPosition().x <= edit_right) && evt.GetPosition().y >= edit_top && evt.GetPosition().y <= edit_bottom) {
                wxCommandEvent event(EVT_EDIT_PRINT_NAME);
                event.SetEventObject(this);
                wxPostEvent(this, event);
                return;
            }
        }
        if (m_show_bind) {
            auto left   = GetSize().x - m_unbind_img.GetBmpSize().x - 6;
            auto right  = left + m_unbind_img.GetBmpSize().x;
            auto top    = (GetSize().y - m_unbind_img.GetBmpSize().y) / 2;
            auto bottom = (GetSize().y - m_unbind_img.GetBmpSize().y) / 2 + m_unbind_img.GetBmpSize().y;

            if ((evt.GetPosition().x >= left && evt.GetPosition().x <= right) && evt.GetPosition().y >= top && evt.GetPosition().y <= bottom) {
                wxCommandEvent event(EVT_UNBIND_MACHINE, GetId());
                event.SetEventObject(this);
                GetEventHandler()->ProcessEvent(event);
            } else {
                if (m_info) {
                    wxGetApp().mainframe->jump_to_monitor(m_info->dev_id);
                }
                //wxGetApp().mainframe->SetFocus();
                wxCommandEvent event(EVT_DISSMISS_MACHINE_LIST);
                event.SetEventObject(this->GetParent());
                wxPostEvent(this->GetParent(), event);
            }
            return;
        }
        if (m_info && m_info->is_lan_mode_printer()) {
            if (m_info->has_access_right() && m_info->is_avaliable()) {
                wxGetApp().mainframe->jump_to_monitor(m_info->dev_id);
            } else {
                wxCommandEvent event(EVT_CONNECT_LAN_PRINT);
                event.SetEventObject(this);
                wxPostEvent(this, event);
            }
        } else {
            wxGetApp().mainframe->jump_to_monitor(m_info->dev_id);
        }
    } else {
        if (m_info && m_info->is_lan_mode_printer()) {
            wxCommandEvent event(EVT_CONNECT_LAN_PRINT);
            event.SetEventObject(this);
            wxPostEvent(this, event);
        } else {
            wxCommandEvent event(EVT_BIND_MACHINE);
            event.SetEventObject(this);
            wxPostEvent(this, event);
        }
    }

}

SelectMachinePopup::SelectMachinePopup(wxWindow *parent)
    : wxPopupTransientWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS), m_dismiss(false)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__


    SetSize(SELECT_MACHINE_POPUP_SIZE);
    SetMinSize(SELECT_MACHINE_POPUP_SIZE);
    SetMaxSize(SELECT_MACHINE_POPUP_SIZE);

    Freeze();
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    SetBackgroundColour(SELECT_MACHINE_GREY400);



    m_scrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, SELECT_MACHINE_LIST_SIZE, wxHSCROLL | wxVSCROLL);
    m_scrolledWindow->SetBackgroundColour(*wxWHITE);
    m_scrolledWindow->SetMinSize(SELECT_MACHINE_LIST_SIZE);
    m_scrolledWindow->SetScrollRate(0, 5);
    auto m_sizxer_scrolledWindow = new wxBoxSizer(wxVERTICAL);
    m_scrolledWindow->SetSizer(m_sizxer_scrolledWindow);
    m_scrolledWindow->Layout();
    m_sizxer_scrolledWindow->Fit(m_scrolledWindow);

#if !BBL_RELEASE_TO_PUBLIC && defined(__WINDOWS__)
	m_sizer_search_bar = new wxBoxSizer(wxVERTICAL);
	m_search_bar = new wxSearchCtrl( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0 );
	m_search_bar->ShowSearchButton( true );
	m_search_bar->ShowCancelButton( false );
	m_sizer_search_bar->Add( m_search_bar, 1, wxALL| wxEXPAND, 1 );
	m_sizer_main->Add(m_sizer_search_bar, 0, wxALL | wxEXPAND, FromDIP(2));
	m_search_bar->Bind( wxEVT_COMMAND_TEXT_UPDATED, &SelectMachinePopup::update_machine_list, this );
#endif
    auto own_title        = create_title_panel(_L("My Device"));
    m_sizer_my_devices    = new wxBoxSizer(wxVERTICAL);
    auto other_title      = create_title_panel(_L("Other Device"));
    m_sizer_other_devices = new wxBoxSizer(wxVERTICAL);

    m_sizxer_scrolledWindow->Add(own_title, 0, wxEXPAND | wxLEFT, FromDIP(15));
    m_sizxer_scrolledWindow->Add(m_sizer_my_devices, 0, wxEXPAND, 0);
    m_sizxer_scrolledWindow->Add(other_title, 0, wxEXPAND | wxLEFT, FromDIP(15));
    m_sizxer_scrolledWindow->Add(m_sizer_other_devices, 0, wxEXPAND, 0);


    m_sizer_main->Add(m_scrolledWindow, 0, wxALL | wxEXPAND, FromDIP(2));

    SetSizer(m_sizer_main);
    Layout();
    Thaw();

    #ifdef __APPLE__
    m_scrolledWindow->Bind(wxEVT_LEFT_UP, &SelectMachinePopup::OnLeftUp, this);
    #endif // __APPLE__

    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
    Bind(EVT_UPDATE_USER_MACHINE_LIST, &SelectMachinePopup::update_machine_list, this);
    Bind(wxEVT_TIMER, &SelectMachinePopup::on_timer, this);
    Bind(EVT_DISSMISS_MACHINE_LIST, &SelectMachinePopup::on_dissmiss_win, this);
}

SelectMachinePopup::~SelectMachinePopup() { delete m_refresh_timer;}

void SelectMachinePopup::Popup(wxWindow *WXUNUSED(focus))
{
    BOOST_LOG_TRIVIAL(trace) << "get_print_info: start";
    start_ssdp(true);
    if (m_refresh_timer) {
        m_refresh_timer->Stop();
        m_refresh_timer->Start(MACHINE_LIST_REFRESH_INTERVAL);
    }

    if (wxGetApp().is_user_login()) {
        if (!get_print_info_thread) {
            get_print_info_thread = new boost::thread(Slic3r::create_thread([&] {
                NetworkAgent* agent = wxGetApp().getAgent();
                unsigned int http_code;
                std::string body;
                int result = agent->get_user_print_info(&http_code, &body);
                if (result == 0) {
                    m_print_info = body;
                } else {
                    m_print_info = "";
                }
                wxCommandEvent event(EVT_UPDATE_USER_MACHINE_LIST);
                event.SetEventObject(this);
                wxPostEvent(this, event);
            }));
        }
    }

    wxPostEvent(this, wxTimerEvent());
    wxPopupTransientWindow::Popup();
}

void SelectMachinePopup::OnDismiss()
{
    BOOST_LOG_TRIVIAL(trace) << "get_print_info: dismiss";
    start_ssdp(false);
    m_dismiss = true;

    if (m_refresh_timer) {
        m_refresh_timer->Stop();
    }
    if (get_print_info_thread) {
        if (get_print_info_thread->joinable()) {
            get_print_info_thread->join();
            delete get_print_info_thread;
            get_print_info_thread = nullptr;
        }
    }

    wxCommandEvent event(EVT_FINISHED_UPDATE_MACHINE_LIST);
    event.SetEventObject(this);
    wxPostEvent(this, event);
}

bool SelectMachinePopup::ProcessLeftDown(wxMouseEvent &event) {
    return wxPopupTransientWindow::ProcessLeftDown(event);
}

bool SelectMachinePopup::Show(bool show) {
    if (show) {
        for (int i = 0; i < m_user_list_machine_panel.size(); i++) {
            m_user_list_machine_panel[i]->mPanel->update_machine_info(nullptr);
            m_user_list_machine_panel[i]->mPanel->Hide();
        }

         for (int j = 0; j < m_other_list_machine_panel.size(); j++) {
            m_other_list_machine_panel[j]->mPanel->update_machine_info(nullptr);
            m_other_list_machine_panel[j]->mPanel->Hide();
        }
    }
    return wxPopupTransientWindow::Show(show);
}

wxWindow *SelectMachinePopup::create_title_panel(wxString text)
{
    auto m_panel_title_own = new wxWindow(m_scrolledWindow, wxID_ANY, wxDefaultPosition, SELECT_MACHINE_ITEM_SIZE, wxTAB_TRAVERSAL);
    m_panel_title_own->SetBackgroundColour(*wxWHITE);

    wxBoxSizer *m_sizer_title_own = new wxBoxSizer(wxHORIZONTAL);

    auto m_title_own = new wxStaticText(m_panel_title_own, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, 0);
    m_title_own->Wrap(-1);
    m_sizer_title_own->Add(m_title_own, 0, wxALIGN_CENTER, 0);

    wxBoxSizer *m_sizer_line_own = new wxBoxSizer(wxHORIZONTAL);

    auto m_panel_line_own = new wxPanel(m_panel_title_own, wxID_ANY, wxDefaultPosition, wxSize(SELECT_MACHINE_ITEM_SIZE.x, FromDIP(1)), wxTAB_TRAVERSAL);
    m_panel_line_own->SetBackgroundColour(SELECT_MACHINE_GREY400);

    m_sizer_line_own->Add(m_panel_line_own, 0, wxALIGN_CENTER, 0);
    m_sizer_title_own->Add(0, 0, 0, wxLEFT, FromDIP(10));
    m_sizer_title_own->Add(m_sizer_line_own, 1, wxEXPAND | wxRIGHT, FromDIP(10));

    m_panel_title_own->SetSizer(m_sizer_title_own);
    m_panel_title_own->Layout();
    return m_panel_title_own;
}

void SelectMachinePopup::on_timer(wxTimerEvent &event)
{
    BOOST_LOG_TRIVIAL(trace) << "SelectMachinePopup on_timer";
    wxGetApp().reset_to_active();
    wxCommandEvent user_event(EVT_UPDATE_USER_MACHINE_LIST);
    user_event.SetEventObject(this);
    wxPostEvent(this, user_event);
}

void SelectMachinePopup::update_other_devices()
{
    DeviceManager* dev = wxGetApp().getDeviceManager();
    if (!dev) return;
    m_free_machine_list = dev->get_local_machine_list();

    BOOST_LOG_TRIVIAL(trace) << "SelectMachinePopup update_other_devices start";
    this->Freeze();
    m_scrolledWindow->Freeze();
    int i = 0;

    for (auto &elem : m_free_machine_list) {
        MachineObject *     mobj = elem.second;
        /* do not show printer bind state is empty */
        if (!mobj->is_avaliable()) continue;

        if (!wxGetApp().is_user_login() && !mobj->is_lan_mode_printer())
            continue;

        /* do not show printer in my list */
        auto it = m_bind_machine_list.find(mobj->dev_id);
        if (it != m_bind_machine_list.end())
            continue;

        MachineObjectPanel* op = nullptr;
        if (i < m_other_list_machine_panel.size()) {
            op = m_other_list_machine_panel[i]->mPanel;
            op->Show();
#if !BBL_RELEASE_TO_PUBLIC && defined(__WINDOWS__)
			if (!search_for_printer(mobj)) {
				op->Hide();
			}
#endif
        } else {
            op = new MachineObjectPanel(m_scrolledWindow, wxID_ANY);
            MachinePanel* mpanel = new MachinePanel();
            mpanel->mIndex = wxString::Format("%d", i);
            mpanel->mPanel = op;
            m_other_list_machine_panel.push_back(mpanel);
            m_sizer_other_devices->Add(op, 0, wxEXPAND, 0);
        }
        i++;

        op->update_machine_info(mobj);

        if (mobj->is_lan_mode_printer()) {
            if (mobj->has_access_right()) {
                op->set_printer_state(PrinterState::IN_LAN);
            } else {
                op->set_printer_state(PrinterState::LOCK);
            }
        } else {
            op->show_edit_printer_name(false);
            op->show_printer_bind(true, PrinterBindState::ALLOW_BIND);
            if (mobj->is_in_printing()) {
                op->set_printer_state(PrinterState::BUSY);
            } else {
                op->SetToolTip(_L("Online"));
                op->set_printer_state(IDLE);
            }
        }

        op->Bind(EVT_CONNECT_LAN_PRINT, [this, mobj](wxCommandEvent &e) {
            if (mobj) {
                if (mobj->is_lan_mode_printer()) {
                    ConnectPrinterDialog dlg(wxGetApp().mainframe, wxID_ANY, _L("Input access code"));
                    dlg.set_machine_object(mobj);
                    if (dlg.ShowModal() == wxID_OK) {
                        wxGetApp().mainframe->jump_to_monitor(mobj->dev_id);
                    }
                }
            }
        });

        op->Bind(EVT_BIND_MACHINE, [this, mobj](wxCommandEvent &e) {
            BindMachineDialog dlg;
            dlg.update_machine_info(mobj);
            int dlg_result = wxID_CANCEL;
            dlg_result     = dlg.ShowModal();
            if (dlg_result == wxID_OK) { wxGetApp().mainframe->jump_to_monitor(mobj->dev_id); }
        });
    }

    for (int j = i; j < m_other_list_machine_panel.size(); j++) {
        m_other_list_machine_panel[j]->mPanel->update_machine_info(nullptr);
        m_other_list_machine_panel[j]->mPanel->Hide();
    }

    if (m_placeholder_panel != nullptr) {
        m_scrolledWindow->RemoveChild(m_placeholder_panel);
        m_placeholder_panel->Destroy();
        m_placeholder_panel = nullptr;
    }

    m_placeholder_panel = new wxWindow(m_scrolledWindow, wxID_ANY, wxDefaultPosition, wxSize(-1,FromDIP(26)));
    wxBoxSizer* placeholder_sizer = new wxBoxSizer(wxVERTICAL);

    m_hyperlink = new wxHyperlinkCtrl(m_placeholder_panel, wxID_ANY, _L("Can't find my devices?"), wxT("https://wiki.bambulab.com/en/software/bambu-studio/failed-to-connect-printer"), wxDefaultPosition, wxDefaultSize, wxHL_DEFAULT_STYLE);
    placeholder_sizer->Add(m_hyperlink, 0, wxALIGN_CENTER | wxALL, 5);


    m_placeholder_panel->SetSizer(placeholder_sizer);
    m_placeholder_panel->Layout();
    placeholder_sizer->Fit(m_placeholder_panel);

    m_placeholder_panel->SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));
    m_sizer_other_devices->Add(m_placeholder_panel, 0, wxEXPAND, 0);

    //m_sizer_other_devices->Layout();
    if(m_other_devices_count != i) {
		m_scrolledWindow->Fit();
    }
    m_scrolledWindow->Layout();
	m_scrolledWindow->Thaw();
	Layout();
	Fit();
	this->Thaw();
    m_other_devices_count = i;
    BOOST_LOG_TRIVIAL(trace) << "SelectMachinePopup update_other_devices end";
}

void SelectMachinePopup::update_user_devices()
{
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    if (!m_print_info.empty()) {
        dev->parse_user_print_info(m_print_info);
        m_print_info = "";
    }

    m_bind_machine_list.clear();
    m_bind_machine_list = dev->get_my_machine_list();

    //sort list
    std::vector<std::pair<std::string, MachineObject*>> user_machine_list;
    for (auto& it: m_bind_machine_list) {
        user_machine_list.push_back(it);
    }

    std::sort(user_machine_list.begin(), user_machine_list.end(), [&](auto& a, auto&b) {
            if (a.second && b.second) {
                return a.second->dev_name.compare(b.second->dev_name) < 0;
            }
            return false;
        });

    BOOST_LOG_TRIVIAL(trace) << "SelectMachinePopup update_machine_list start";
    this->Freeze();
    m_scrolledWindow->Freeze();
    int i = 0;

    for (auto& elem : user_machine_list) {
        MachineObject* mobj = elem.second;
        MachineObjectPanel* op = nullptr;
        if (i < m_user_list_machine_panel.size()) {
            op = m_user_list_machine_panel[i]->mPanel;
            op->Show();
#if !BBL_RELEASE_TO_PUBLIC && defined(__WINDOWS__)
			if (!search_for_printer(mobj)) {
				op->Hide();
			}
#endif
        } else {
            op = new MachineObjectPanel(m_scrolledWindow, wxID_ANY);
            MachinePanel* mpanel = new MachinePanel();
            mpanel->mIndex = wxString::Format("%d", i);
            mpanel->mPanel = op;
            m_user_list_machine_panel.push_back(mpanel);
            m_sizer_my_devices->Add(op, 0, wxEXPAND, 0);
        }
        i++;
        op->update_machine_info(mobj, true);
        //set in lan
        if (mobj->is_lan_mode_printer()) {
            if (!mobj->is_online()) {
                continue;
            }
            else {
                op->show_printer_bind(false, PrinterBindState::NONE);
                op->show_edit_printer_name(false);
                if (mobj->has_access_right() && mobj->is_avaliable()) {
                    op->set_printer_state(PrinterState::IN_LAN);
                    op->show_printer_bind(true, PrinterBindState::ALLOW_UNBIND);
                    op->SetToolTip(_L("Online"));
                }
                else {
                    op->set_printer_state(PrinterState::LOCK);
                }
            }
            op->Bind(EVT_UNBIND_MACHINE, [this, dev, mobj](wxCommandEvent& e) {
                dev->set_selected_machine("");
                if (mobj)
                    mobj->set_access_code("");
                MessageDialog msg_wingow(nullptr, _L("Log out successful."), "", wxAPPLY | wxOK);
                if (msg_wingow.ShowModal() == wxOK) { return; }
                });
        }
        else {
            op->show_printer_bind(true, PrinterBindState::ALLOW_UNBIND);
            op->Bind(EVT_UNBIND_MACHINE, [this, mobj, dev](wxCommandEvent& e) {
                // show_unbind_dialog
                UnBindMachineDialog dlg;
                dlg.update_machine_info(mobj);
                if (dlg.ShowModal() == wxID_OK) {
                    dev->set_selected_machine("");
                }
                });

            if (!mobj->is_online()) {
                op->SetToolTip(_L("Offline"));
                op->set_printer_state(PrinterState::OFFLINE);
            }
            else {
                op->show_edit_printer_name(true);
                op->show_printer_bind(true, PrinterBindState::ALLOW_UNBIND);
                if (mobj->is_in_printing()) {
                    op->SetToolTip(_L("Busy"));
                    op->set_printer_state(PrinterState::BUSY);
                }
                else {
                    op->SetToolTip(_L("Online"));
                    op->set_printer_state(PrinterState::IDLE);
                }
            }
        }

        op->Bind(EVT_CONNECT_LAN_PRINT, [this, mobj](wxCommandEvent &e) {
            if (mobj) {
                if (mobj->is_lan_mode_printer()) {
                    ConnectPrinterDialog dlg(wxGetApp().mainframe, wxID_ANY, _L("Input access code"));
                    dlg.set_machine_object(mobj);
                    if (dlg.ShowModal() == wxID_OK) {
                        wxGetApp().mainframe->jump_to_monitor(mobj->dev_id);
                    }
                }
            }
        });

         op->Bind(EVT_EDIT_PRINT_NAME, [this, mobj](wxCommandEvent &e) {
            EditDevNameDialog dlg;
            dlg.set_machine_obj(mobj);
            dlg.ShowModal();
         });
    }

    for (int j = i; j < m_user_list_machine_panel.size(); j++) {
        m_user_list_machine_panel[j]->mPanel->update_machine_info(nullptr);
        m_user_list_machine_panel[j]->mPanel->Hide();
    }
    //m_sizer_my_devices->Layout();

    if (m_my_devices_count != i) {
		m_scrolledWindow->Fit();
    }
    m_scrolledWindow->Layout();
    m_scrolledWindow->Thaw();
	Layout();
	Fit();
	this->Thaw();
    m_my_devices_count = i;
}

bool SelectMachinePopup::search_for_printer(MachineObject* obj)
{
	std::string search_text = std::string((m_search_bar->GetValue()).mb_str());
	if (search_text.empty()) {
		return true;
	}
	auto name = obj->dev_name;
	auto ip = obj->dev_ip;
	auto name_it = name.find(search_text);
	auto ip_it = ip.find(search_text);
	if ((name_it != std::string::npos)||(ip_it != std::string::npos)) {
		return true;
	}

    return false;
}

void SelectMachinePopup::on_dissmiss_win(wxCommandEvent &event)
{
    Dismiss();
}

void SelectMachinePopup::update_machine_list(wxCommandEvent &event)
{
    update_user_devices();
    update_other_devices();
    BOOST_LOG_TRIVIAL(trace) << "SelectMachinePopup update_machine_list end";
}

void SelectMachinePopup::start_ssdp(bool start)
{
    return;
    //if (wxGetApp().getAgent()) { wxGetApp().getAgent()->start_discovery(true, start); }
}

void SelectMachinePopup::OnLeftUp(wxMouseEvent &event)
{
    auto mouse_pos = ClientToScreen(event.GetPosition());
    auto wxscroll_win_pos = m_scrolledWindow->ClientToScreen(wxPoint(0, 0));

    if (mouse_pos.x > wxscroll_win_pos.x && mouse_pos.y > wxscroll_win_pos.y && mouse_pos.x < (wxscroll_win_pos.x + m_scrolledWindow->GetSize().x) &&
        mouse_pos.y < (wxscroll_win_pos.y + m_scrolledWindow->GetSize().y)) {

        for (MachinePanel* p : m_user_list_machine_panel) {
            auto p_rect = p->mPanel->ClientToScreen(wxPoint(0, 0));
            if (mouse_pos.x > p_rect.x && mouse_pos.y > p_rect.y && mouse_pos.x < (p_rect.x + p->mPanel->GetSize().x) && mouse_pos.y < (p_rect.y + p->mPanel->GetSize().y)) {
                wxMouseEvent event(wxEVT_LEFT_UP);
                auto         tag_pos = p->mPanel->ScreenToClient(mouse_pos);
                event.SetPosition(tag_pos);
                event.SetEventObject(p->mPanel);
                wxPostEvent(p->mPanel, event);
            }
        }

        for (MachinePanel* p : m_other_list_machine_panel) {
            auto p_rect = p->mPanel->ClientToScreen(wxPoint(0, 0));
            if (mouse_pos.x > p_rect.x && mouse_pos.y > p_rect.y && mouse_pos.x < (p_rect.x + p->mPanel->GetSize().x) && mouse_pos.y < (p_rect.y + p->mPanel->GetSize().y)) {
                wxMouseEvent event(wxEVT_LEFT_UP);
                auto         tag_pos = p->mPanel->ScreenToClient(mouse_pos);
                event.SetPosition(tag_pos);
                event.SetEventObject(p->mPanel);
                wxPostEvent(p->mPanel, event);
            }
        }

        //hyper link
        auto h_rect = m_hyperlink->ClientToScreen(wxPoint(0, 0));
        if (mouse_pos.x > h_rect.x && mouse_pos.y > h_rect.y && mouse_pos.x < (h_rect.x + m_hyperlink->GetSize().x) && mouse_pos.y < (h_rect.y + m_hyperlink->GetSize().y)) {
          wxLaunchDefaultBrowser(wxT("https://wiki.bambulab.com/en/software/bambu-studio/failed-to-connect-printer"));
        }
    }
}

static wxString MACHINE_BED_TYPE_STRING[BED_TYPE_COUNT] = {
    //_L("Auto"),
    _L("Bambu Cool Plate"), _L("Bamabu Engineering Plate"), _L("Bamabu High Temperature Plate")};

static std::string MachineBedTypeString[BED_TYPE_COUNT] = {
    //"auto",
    "pc",
    "pei",
    "pe",
};


void SelectMachineDialog::stripWhiteSpace(std::string& str)
{
    if (str == "") { return; }

    string::iterator cur_it;
    cur_it = str.begin();

    while (cur_it != str.end()) {
        if ((*cur_it) == '\n' || (*cur_it) == ' ') {
            cur_it = str.erase(cur_it);
        }
        else {
            cur_it++;
        }
    }
}

wxString SelectMachineDialog::format_text(wxString &m_msg)
{
    if (wxGetApp().app_config->get("language") != "zh_CN") {return m_msg; }

    wxString out_txt      = m_msg;
    wxString count_txt    = "";
    int      new_line_pos = 0;

    for (int i = 0; i < m_msg.length(); i++) {
        auto text_size = m_statictext_ams_msg->GetTextExtent(count_txt);
        if (text_size.x < (FromDIP(400))) {
            count_txt += m_msg[i];
        } else {
            out_txt.insert(i - 1, '\n');
            count_txt = "";
        }
    }
    return out_txt;
}

SelectMachineDialog::SelectMachineDialog(Plater *plater)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Send print job to"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_plater(plater), m_export_3mf_cancel(false)
    , m_mapping_popup(AmsMapingPopup(this))
    , m_mapping_tip_popup(AmsMapingTipPopup(this))
    , m_mapping_tutorial_popup(AmsTutorialPopup(this))
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    // bind
    Bind(wxEVT_CLOSE_WINDOW, &SelectMachineDialog::on_cancel, this);

    for (int i = 0; i < BED_TYPE_COUNT; i++) { m_bedtype_list.push_back(MACHINE_BED_TYPE_STRING[i]); }

    // font
    SetFont(wxGetApp().normal_font());

    // icon
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    Freeze();
    SetBackgroundColour(m_colour_def_color);

    m_sizer_main = new wxBoxSizer(wxVERTICAL);

    m_sizer_main->SetMinSize(wxSize(0, -1));
    m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));

    m_scrollable_view   = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_sizer_scrollable_view = new wxBoxSizer(wxVERTICAL);

    m_scrollable_region       = new wxPanel(m_scrollable_view, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_sizer_scrollable_region = new wxBoxSizer(wxVERTICAL);


    //rename normal
    m_rename_switch_panel = new wxSimplebook(m_scrollable_region);
    m_rename_switch_panel->SetSize(wxSize(FromDIP(420), FromDIP(25)));
    m_rename_switch_panel->SetMinSize(wxSize(FromDIP(420), FromDIP(25)));
    m_rename_switch_panel->SetMaxSize(wxSize(FromDIP(420), FromDIP(25)));

    m_rename_normal_panel = new wxPanel(m_rename_switch_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_rename_normal_panel->SetBackgroundColour(*wxWHITE);
    rename_sizer_v = new wxBoxSizer(wxVERTICAL);
    rename_sizer_h = new wxBoxSizer(wxHORIZONTAL);

    m_rename_text = new wxStaticText(m_rename_normal_panel, wxID_ANY, wxT("MyLabel"), wxDefaultPosition, wxDefaultSize, 0);
    m_rename_text->SetFont(::Label::Body_13);
    m_rename_text->SetMaxSize(wxSize(FromDIP(390), -1));
    m_rename_button = new Button(m_rename_normal_panel, "", "ams_editable", wxBORDER_NONE, FromDIP(10));
    m_rename_button->SetBackgroundColor(*wxWHITE);
    m_rename_button->SetBackgroundColour(*wxWHITE);

    rename_sizer_h->Add(m_rename_text, 0, wxALIGN_CENTER, 0);
    rename_sizer_h->Add(m_rename_button, 0, wxALIGN_CENTER, 0);
    rename_sizer_v->Add(rename_sizer_h, 1, wxALIGN_CENTER, 0);
    m_rename_normal_panel->SetSizer(rename_sizer_v);
    m_rename_normal_panel->Layout();
    rename_sizer_v->Fit(m_rename_normal_panel);

    //rename edit
    auto m_rename_edit_panel = new wxPanel(m_rename_switch_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_rename_edit_panel->SetBackgroundColour(*wxWHITE);
    auto rename_edit_sizer_v = new wxBoxSizer(wxVERTICAL);

    m_rename_input = new ::TextInput(m_rename_edit_panel, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_PROCESS_ENTER);
    m_rename_input->GetTextCtrl()->SetFont(::Label::Body_13);
    m_rename_input->SetSize(wxSize(FromDIP(380), FromDIP(24)));
    m_rename_input->SetMinSize(wxSize(FromDIP(380), FromDIP(24)));
    m_rename_input->SetMaxSize(wxSize(FromDIP(380), FromDIP(24)));
    m_rename_input->Bind(wxEVT_TEXT_ENTER, [this](auto& e) {on_rename_enter();});
    rename_edit_sizer_v->Add(m_rename_input, 1, wxALIGN_CENTER, 0);


    m_rename_edit_panel->SetSizer(rename_edit_sizer_v);
    m_rename_edit_panel->Layout();
    rename_edit_sizer_v->Fit(m_rename_edit_panel);

    m_rename_button->Bind(wxEVT_BUTTON, &SelectMachineDialog::on_rename_click, this);
    m_rename_switch_panel->AddPage(m_rename_normal_panel, wxEmptyString, true);
    m_rename_switch_panel->AddPage(m_rename_edit_panel, wxEmptyString, false);

    Bind(wxEVT_CHAR_HOOK, [this](wxKeyEvent& e) {
        if (e.GetKeyCode() == WXK_ESCAPE) {
            if (m_rename_switch_panel->GetSelection() == 0) {
                e.Skip();
            }
            else {
                m_rename_switch_panel->SetSelection(0);
                m_rename_text->SetLabel(m_current_project_name);
                m_rename_normal_panel->Layout();
            }
        }
        else {
            e.Skip();
        }
    });


    m_panel_image = new wxPanel(m_scrollable_region, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_image->SetBackgroundColour(m_colour_def_color);

    sizer_thumbnail = new wxBoxSizer(wxVERTICAL);
    m_thumbnailPanel = new ThumbnailPanel(m_panel_image);
    m_thumbnailPanel->SetSize(wxSize(FromDIP(256), FromDIP(256)));
    m_thumbnailPanel->SetMinSize(wxSize(FromDIP(256), FromDIP(256)));
    m_thumbnailPanel->SetMaxSize(wxSize(FromDIP(256), FromDIP(256)));
    sizer_thumbnail->Add(m_thumbnailPanel, 0, wxEXPAND, 0);
    m_panel_image->SetSizer(sizer_thumbnail);
    m_panel_image->Layout();

    wxBoxSizer *m_sizer_basic        = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_basic_weight = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_basic_time   = new wxBoxSizer(wxHORIZONTAL);

    auto timeimg = new wxStaticBitmap(m_scrollable_region, wxID_ANY, create_scaled_bitmap("print-time", this, 18), wxDefaultPosition, wxSize(FromDIP(18), FromDIP(18)), 0);
    m_sizer_basic_weight->Add(timeimg, 1, wxEXPAND | wxALL, FromDIP(5));
    m_stext_time = new wxStaticText(m_scrollable_region, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    m_sizer_basic_weight->Add(m_stext_time, 0, wxALL, FromDIP(5));
    m_sizer_basic->Add(m_sizer_basic_weight, 0, wxALIGN_CENTER, 0);
    m_sizer_basic->Add(0, 0, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));

    auto weightimg = new wxStaticBitmap(m_scrollable_region, wxID_ANY, create_scaled_bitmap("print-weight", this, 18), wxDefaultPosition, wxSize(FromDIP(18), FromDIP(18)), 0);
    m_sizer_basic_time->Add(weightimg, 1, wxEXPAND | wxALL, FromDIP(5));
    m_stext_weight = new wxStaticText(m_scrollable_region, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    m_sizer_basic_time->Add(m_stext_weight, 0, wxALL, FromDIP(5));
    m_sizer_basic->Add(m_sizer_basic_time, 0, wxALIGN_CENTER, 0);

    auto m_sizer_material_area = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer* m_sizer_material_tips = new wxBoxSizer(wxHORIZONTAL);


    auto img_amsmapping_tip = new wxStaticBitmap(m_scrollable_region, wxID_ANY, create_scaled_bitmap("enable_ams", this, 16), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), 0);
    m_sizer_material_tips->Add(img_amsmapping_tip, 0, wxALIGN_CENTER | wxLEFT, FromDIP(5));

    img_amsmapping_tip->Bind(wxEVT_ENTER_WINDOW, [this, img_amsmapping_tip](auto& e) {
        wxPoint img_pos = img_amsmapping_tip->ClientToScreen(wxPoint(0, 0));
        wxPoint popup_pos(img_pos.x, img_pos.y + img_amsmapping_tip->GetRect().height);
        m_mapping_tutorial_popup.Position(popup_pos, wxSize(0, 0));
        m_mapping_tutorial_popup.Popup();

        if (m_mapping_tutorial_popup.ClientToScreen(wxPoint(0, 0)).y < img_pos.y) {
            m_mapping_tutorial_popup.Dismiss();
            popup_pos = wxPoint(img_pos.x, img_pos.y - m_mapping_tutorial_popup.GetRect().height);
            m_mapping_tutorial_popup.Position(popup_pos, wxSize(0, 0));
            m_mapping_tutorial_popup.Popup();
        }
        });

    img_amsmapping_tip->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) {
        m_mapping_tutorial_popup.Dismiss();
        });


    m_sizer_material = new wxGridSizer(0, 4, 0, FromDIP(5));


    m_sizer_material_area->Add(m_sizer_material_tips, 0, wxALIGN_CENTER|wxLEFT, FromDIP(8));
    m_sizer_material_area->Add(m_sizer_material, 0, wxLEFT, FromDIP(15));

    m_statictext_ams_msg = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
    m_statictext_ams_msg->SetFont(::Label::Body_13);
    m_statictext_ams_msg->Hide();

    m_line_materia = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_materia->SetForegroundColour(wxColour(238, 238, 238));
    m_line_materia->SetBackgroundColour(wxColour(238, 238, 238));

    wxBoxSizer *m_sizer_printer = new wxBoxSizer(wxHORIZONTAL);

    m_stext_printer_title = new wxStaticText(this, wxID_ANY, _L("Printer"), wxDefaultPosition, wxSize(-1, -1), 0);
    m_stext_printer_title->SetFont(::Label::Head_14);
    m_stext_printer_title->Wrap(-1);
    m_stext_printer_title->SetForegroundColour(m_colour_bold_color);
    m_stext_printer_title->SetBackgroundColour(m_colour_def_color);

    m_sizer_printer->Add(m_stext_printer_title, 0, wxALL | wxLEFT, FromDIP(5));
    m_sizer_printer->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(12));

    m_comboBox_printer = new ::ComboBox(this, wxID_ANY, "", wxDefaultPosition, wxSize(FromDIP(250), -1), 0, nullptr, wxCB_READONLY);
    m_comboBox_printer->Bind(wxEVT_COMBOBOX, &SelectMachineDialog::on_selection_changed, this);

    m_sizer_printer->Add(m_comboBox_printer, 1, wxEXPAND | wxRIGHT, FromDIP(5));
    btn_bg_enable = StateColor(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                               std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    m_button_refresh = new Button(this, _L("Refresh"));
    m_button_refresh->SetBackgroundColor(btn_bg_enable);
    m_button_refresh->SetBorderColor(btn_bg_enable);
    m_button_refresh->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
    m_button_refresh->SetSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_refresh->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_refresh->SetCornerRadius(FromDIP(10));
    m_button_refresh->Bind(wxEVT_BUTTON, &SelectMachineDialog::on_refresh, this);
    m_sizer_printer->Add(m_button_refresh, 0, wxALL | wxLEFT, FromDIP(5));

    m_statictext_printer_msg = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
    m_statictext_printer_msg->SetFont(::Label::Body_13);
    m_statictext_printer_msg->Hide();

    //m_sizer_select = new wxGridSizer(0, 2, 0, 0);
    m_sizer_select = new wxWrapSizer();
    select_bed     = create_item_checkbox(_L("Bed Leveling"), this, _L("Bed Leveling"), "bed_leveling");
    select_flow    = create_item_checkbox(_L("Flow Calibration"), this, _L("Flow Calibration"), "flow_cali");
    select_timelapse = create_item_checkbox(_L("Timelapse"), this, _L("Timelapse"), "timelapse");
    select_use_ams = create_ams_checkbox(_L("Enable AMS"), this, _L("Enable AMS"));

    m_sizer_select->Add(select_bed, 0, wxLEFT | wxRIGHT, WRAP_GAP);
    m_sizer_select->Add(select_flow, 0, wxLEFT | wxRIGHT, WRAP_GAP);
    m_sizer_select->Add(select_timelapse, 0, wxLEFT | wxRIGHT, WRAP_GAP);
    m_sizer_select->Add(select_use_ams, 0, wxLEFT | wxRIGHT, WRAP_GAP);

    select_bed->Show(true);
    select_flow->Show(true);
    select_timelapse->Show(false);
    select_use_ams->Show(true);

    // line schedule
    m_line_schedule = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_schedule->SetBackgroundColour(wxColour(238, 238, 238));

    m_sizer_bottom = new wxBoxSizer(wxVERTICAL);
    m_simplebook   = new wxSimplebook(this, wxID_ANY, wxDefaultPosition, SELECT_MACHINE_DIALOG_SIMBOOK_SIZE, 0);

    // perpare mode
    m_panel_prepare = new wxPanel(m_simplebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_prepare->SetBackgroundColour(m_colour_def_color);
    //m_panel_prepare->SetBackgroundColour(wxColour(135,206,250));
    wxBoxSizer *m_sizer_prepare = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_pcont   = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_prepare->Add(0, 0, 1, wxTOP, FromDIP(12));

    auto hyperlink_sizer = new wxBoxSizer( wxHORIZONTAL );
    auto m_hyperlink = new wxHyperlinkCtrl(m_panel_prepare, wxID_ANY, _L("Can't find my devices?"), wxT("https://wiki.bambulab.com/en/software/bambu-studio/failed-to-connect-printer"), wxDefaultPosition, wxDefaultSize, wxHL_DEFAULT_STYLE);

    //auto linkimg = new wxStaticBitmap(m_panel_prepare, wxID_ANY, create_scaled_bitmap("link_wiki_img", this, 18), wxDefaultPosition, wxSize(FromDIP(18), FromDIP(18)), 0);

    hyperlink_sizer->Add(m_hyperlink, 0, wxALIGN_CENTER | wxALL, 5);
    //hyperlink_sizer->Add(linkimg, 0, wxALIGN_CENTER | wxALL, FromDIP(5));

    m_sizer_prepare->Add(hyperlink_sizer, 0, wxALIGN_CENTER | wxALL, 5);


    m_sizer_pcont->Add(0, 0, 1, wxEXPAND, 0);
    m_button_ensure = new Button(m_panel_prepare, _L("Send"));
    m_button_ensure->SetBackgroundColor(btn_bg_enable);
    m_button_ensure->SetBorderColor(btn_bg_enable);
    m_button_ensure->SetTextColor(StateColor::darkModeColorFor("#FFFFFE"));
    m_button_ensure->SetSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_ensure->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_ensure->SetCornerRadius(FromDIP(12));

    m_button_ensure->Bind(wxEVT_BUTTON, &SelectMachineDialog::on_ok_btn, this);
    m_sizer_pcont->Add(m_button_ensure, 0, wxEXPAND | wxBOTTOM, FromDIP(10));
    m_sizer_prepare->Add(m_sizer_pcont, 0, wxEXPAND, 0);
    m_panel_prepare->SetSizer(m_sizer_prepare);
    m_panel_prepare->Layout();
    m_simplebook->AddPage(m_panel_prepare, wxEmptyString, true);

    // sending mode
    m_status_bar    = std::make_shared<BBLStatusBarSend>(m_simplebook);
    m_panel_sending = m_status_bar->get_panel();
    m_simplebook->AddPage(m_panel_sending, wxEmptyString, false);

    // finish mode
    m_panel_finish = new wxPanel(m_simplebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_finish->SetBackgroundColour(wxColour(135, 206, 250));
    wxBoxSizer *m_sizer_finish   = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_finish_v = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_finish_h = new wxBoxSizer(wxHORIZONTAL);

    auto imgsize      = FromDIP(25);
    auto completedimg = new wxStaticBitmap(m_panel_finish, wxID_ANY, create_scaled_bitmap("completed", m_panel_finish, 25), wxDefaultPosition, wxSize(imgsize, imgsize), 0);
    m_sizer_finish_h->Add(completedimg, 0, wxALIGN_CENTER | wxALL, FromDIP(5));

    m_statictext_finish = new wxStaticText(m_panel_finish, wxID_ANY, L("send completed"), wxDefaultPosition, wxDefaultSize, 0);
    m_statictext_finish->Wrap(-1);
    m_statictext_finish->SetForegroundColour(wxColour(0, 174, 66));
    m_sizer_finish_h->Add(m_statictext_finish, 0, wxALIGN_CENTER | wxALL, FromDIP(5));

    m_sizer_finish_v->Add(m_sizer_finish_h, 1, wxALIGN_CENTER, 0);

    m_sizer_finish->Add(m_sizer_finish_v, 1, wxALIGN_CENTER, 0);

    m_panel_finish->SetSizer(m_sizer_finish);
    m_panel_finish->Layout();
    m_sizer_finish->Fit(m_panel_finish);
    m_simplebook->AddPage(m_panel_finish, wxEmptyString, false);

    // bind
    Bind(EVT_UPDATE_USER_MACHINE_LIST, &SelectMachineDialog::update_printer_combobox, this);
    Bind(EVT_PRINT_JOB_CANCEL, &SelectMachineDialog::on_print_job_cancel, this);
    Bind(EVT_SET_FINISH_MAPPING, &SelectMachineDialog::on_set_finish_mapping, this);
    wxGetApp().Bind(EVT_CONNECT_LAN_MODE_PRINT, [this](wxCommandEvent& e) {
        if (e.GetInt() == 1) {
            DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
            if (!dev) return;
            m_comboBox_printer->SetValue(dev->get_selected_machine()->dev_name);
        }
    });

    m_panel_prepare->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        check_fcous_state(this);
        e.Skip();
    });

    m_scrollable_region->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        check_fcous_state(this);
        e.Skip();
    });

    Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        check_fcous_state(this);
        e.Skip();
    });

    m_sizer_scrollable_region->Add(m_rename_switch_panel, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_scrollable_region->Add(0, 0, 0, wxTOP, FromDIP(8));
    m_sizer_scrollable_region->Add(m_panel_image, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_scrollable_region->Add(0, 0, 0, wxTOP, FromDIP(10));
    m_sizer_scrollable_region->Add(m_sizer_basic, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    //m_sizer_scrollable_region->Add(m_sizer_material, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_scrollable_region->Add(m_sizer_material_area, 0, wxLEFT, FromDIP(10));

    m_scrollable_region->SetSizer(m_sizer_scrollable_region);
    m_scrollable_region->Layout();

    m_scrollable_view->SetSizer(m_sizer_scrollable_view);
    m_scrollable_view->Layout();
    m_sizer_scrollable_view->Add(m_scrollable_region, 0, wxEXPAND, 0);

    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(22));
    m_sizer_main->Add(m_scrollable_view, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(8));
    m_sizer_main->Add(m_statictext_ams_msg, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(8));
    m_sizer_main->Add(m_line_materia, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));
    m_sizer_main->Add(m_sizer_printer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(5));
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(8));
    m_sizer_main->Add(m_statictext_printer_msg, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_main->Add(0, 1, 0, wxTOP, FromDIP(20));
    m_sizer_main->Add(m_sizer_select, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(40));
    m_sizer_main->Add(0, 1, 0, wxTOP, FromDIP(12));
    m_sizer_main->Add(m_line_schedule, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));
    m_sizer_main->Add(m_simplebook, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_main->Add(m_sizer_bottom, 0, wxALIGN_CENTER_HORIZONTAL);
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(15));

    SetSizer(m_sizer_main);
    Layout();
    Fit();
    Thaw();

    init_bind();
    init_timer();
    // CenterOnParent();
    Centre(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);
}
void SelectMachineDialog::check_focus(wxWindow* window)
{
    if (window == m_rename_input || window == m_rename_input->GetTextCtrl()) {
        on_rename_enter();
    }
}

void SelectMachineDialog::check_fcous_state(wxWindow* window)
{
    check_focus(window);
    auto children = window->GetChildren();
    for (auto child : children) {
        check_fcous_state(child);
    }
}

wxWindow *SelectMachineDialog::create_ams_checkbox(wxString title, wxWindow *parent, wxString tooltip)
{
    auto checkbox = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    checkbox->SetBackgroundColour(m_colour_def_color);

    wxBoxSizer *sizer_checkbox = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *sizer_check    = new wxBoxSizer(wxVERTICAL);

    ams_check = new ::CheckBox(checkbox);

    sizer_check->Add(ams_check, 0, wxBOTTOM | wxEXPAND | wxTOP, FromDIP(5));

    sizer_checkbox->Add(sizer_check, 0, wxEXPAND, FromDIP(5));
    sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(11));

    auto text = new wxStaticText(checkbox, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    text->SetFont(::Label::Body_13);
    text->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3C")));
    text->Wrap(-1);
    sizer_checkbox->Add(text, 0, wxALIGN_CENTER, FromDIP(5));

    auto img_ams_tip = new wxStaticBitmap(checkbox, wxID_ANY, create_scaled_bitmap("enable_ams", this, 16), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), 0);
    sizer_checkbox->Add(img_ams_tip, 0, wxALIGN_CENTER | wxLEFT, FromDIP(5));

    img_ams_tip->Bind(wxEVT_ENTER_WINDOW, [this, img_ams_tip](auto& e) {
        wxPoint img_pos = img_ams_tip->ClientToScreen(wxPoint(0, 0));
        wxPoint popup_pos(img_pos.x, img_pos.y + img_ams_tip->GetRect().height);
        m_mapping_tip_popup.Position(popup_pos, wxSize(0, 0));
        m_mapping_tip_popup.Popup();

        if (m_mapping_tip_popup.ClientToScreen(wxPoint(0, 0)).y < img_pos.y) {
            m_mapping_tip_popup.Dismiss();
            popup_pos = wxPoint(img_pos.x, img_pos.y - m_mapping_tip_popup.GetRect().height);
            m_mapping_tip_popup.Position(popup_pos, wxSize(0, 0));
            m_mapping_tip_popup.Popup();
        }
    });

    img_ams_tip->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) {
        m_mapping_tip_popup.Dismiss();
        });

    checkbox->SetSizer(sizer_checkbox);
    checkbox->Layout();
    sizer_checkbox->Fit(checkbox);

    checkbox->SetToolTip(tooltip);
    text->SetToolTip(tooltip);

    text->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent & event) {
            ams_check->SetValue(ams_check->GetValue() ? false : true);
        });
    return checkbox;
}

wxWindow *SelectMachineDialog::create_item_checkbox(wxString title, wxWindow *parent, wxString tooltip, std::string param)
{
    auto checkbox = new wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    checkbox->SetBackgroundColour(m_colour_def_color);

    wxBoxSizer *sizer_checkbox = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *sizer_check    = new wxBoxSizer(wxVERTICAL);

    auto check = new ::CheckBox(checkbox);

    sizer_check->Add(check, 0, wxBOTTOM | wxEXPAND | wxTOP, FromDIP(5));

    sizer_checkbox->Add(sizer_check, 0, wxEXPAND, FromDIP(5));
    sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(11));

    auto text = new wxStaticText(checkbox, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    text->SetFont(::Label::Body_13);
    text->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#323A3C")));
    text->Wrap(-1);
    text->SetMinSize(wxSize(FromDIP(120), -1));
    text->SetMaxSize(wxSize(FromDIP(120), -1));
    sizer_checkbox->Add(text, 0, wxBOTTOM | wxEXPAND | wxTOP, FromDIP(5));

    checkbox->SetSizer(sizer_checkbox);
    checkbox->Layout();
    sizer_checkbox->Fit(checkbox);

    checkbox->SetToolTip(tooltip);
    text->SetToolTip(tooltip);

    text->Bind(wxEVT_LEFT_DOWN, [this, check](wxMouseEvent &) { check->SetValue(check->GetValue() ? false : true); });
    m_checkbox_list[param] = check;
    return checkbox;
}

void SelectMachineDialog::update_select_layout(MachineObject *obj)
{
    if (obj && obj->is_function_supported(PrinterFunction::FUNC_FLOW_CALIBRATION)) {
        select_flow->Show();
    } else {
        select_flow->Hide();
    }

    if (obj && obj->is_function_supported(PrinterFunction::FUNC_AUTO_LEVELING)) {
        select_bed->Show();
    } else {
        select_bed->Hide();
    }

    if (obj && obj->is_function_supported(PrinterFunction::FUNC_TIMELAPSE)
        && obj->is_support_print_with_timelapse()
        && is_show_timelapse()) {
        select_timelapse->Show();
    } else {
        select_timelapse->Hide();
    }
    Fit();
}

void SelectMachineDialog::prepare_mode()
{
    m_is_in_sending_mode = false;
    if (m_print_job) {
        m_print_job->join();
    }

    if (wxIsBusy())
        wxEndBusyCursor();

    Enable_Send_Button(true);

    m_status_bar->reset();
    if (m_simplebook->GetSelection() != 0) {
        m_simplebook->SetSelection(0);
        Layout();
        Fit();
    }
}

void SelectMachineDialog::sending_mode()
{
    m_is_in_sending_mode = true;
    if (m_simplebook->GetSelection() != 1){
        m_simplebook->SetSelection(1);
        Layout();
        Fit();
    }
}

void SelectMachineDialog::finish_mode()
{
    m_is_in_sending_mode = false;
    m_simplebook->SetSelection(2);
    Layout();
    Fit();
}


void SelectMachineDialog::sync_ams_mapping_result(std::vector<FilamentInfo> &result)
{
    if (result.empty()) {
        BOOST_LOG_TRIVIAL(trace) << "ams_mapping result is empty";
        for (auto it = m_materialList.begin(); it != m_materialList.end(); it++) {
            wxString ams_id = "-";
            wxColour ams_col = wxColour(0xCE, 0xCE, 0xCE);
            it->second->item->set_ams_info(ams_col, ams_id);
        }
        return;
    }

    for (auto f = result.begin(); f != result.end(); f++) {
        BOOST_LOG_TRIVIAL(trace) << "ams_mapping f id = " << f->id << ", tray_id = " << f->tray_id << ", color = " << f->color << ", type = " << f->type;

        MaterialHash::iterator iter = m_materialList.begin();
        while (iter != m_materialList.end()) {
            int           id   = iter->second->id;
            Material *    item = iter->second;
            MaterialItem *m    = item->item;

            if (f->id == id) {
                wxString ams_id;
                wxColour ams_col;

                if (f->tray_id >= 0) {
                    ams_id = wxGetApp().transition_tridid(f->tray_id);
                    //ams_id = wxString::Format("%02d", f->tray_id + 1);
                } else {
                    ams_id = "-";
                }

                if (!f->color.empty()) {
                    ams_col = AmsTray::decode_color(f->color);
                } else {
                    // default color
                    ams_col = wxColour(0xCE, 0xCE, 0xCE);
                }

                m->set_ams_info(ams_col, ams_id);
                break;
            }
            iter++;
        }
    }
}

void print_ams_mapping_result(std::vector<FilamentInfo>& result)
{
    if (result.empty()) {
        BOOST_LOG_TRIVIAL(info) << "print_ams_mapping_result: empty";
    }

    char buffer[256];
    for (int i = 0; i < result.size(); i++) {
        ::sprintf(buffer, "print_ams_mapping: F(%02d) -> A(%02d)", result[i].id+1, result[i].tray_id+1);
        BOOST_LOG_TRIVIAL(info) << std::string(buffer);
    }
}

bool SelectMachineDialog::do_ams_mapping(MachineObject *obj_)
{
    if (!obj_) return false;

    // try color and type mapping
    int result = obj_->ams_filament_mapping(m_filaments, m_ams_mapping_result);
    if (result == 0) {
        print_ams_mapping_result(m_ams_mapping_result);
        std::string ams_array;
        std::string mapping_info;
        get_ams_mapping_result(ams_array, mapping_info);
        if (ams_array.empty()) {
            reset_ams_material();
            BOOST_LOG_TRIVIAL(info) << "ams_mapping_array=[]";
        } else {
            sync_ams_mapping_result(m_ams_mapping_result);
            BOOST_LOG_TRIVIAL(info) << "ams_mapping_array=" << ams_array;
        }
        return obj_->is_valid_mapping_result(m_ams_mapping_result);
    } else {
        // do not support ams mapping try to use order mapping
        bool is_valid = obj_->is_valid_mapping_result(m_ams_mapping_result);
        if (result != 1 && !is_valid) {
            //reset invalid result
            for (int i = 0; i < m_ams_mapping_result.size(); i++) {
                m_ams_mapping_result[i].tray_id = -1;
                m_ams_mapping_result[i].distance = 99999;
            }
        }
        sync_ams_mapping_result(m_ams_mapping_result);
           return is_valid;
    }

    return true;
}

bool SelectMachineDialog::get_ams_mapping_result(std::string &mapping_array_str, std::string &ams_mapping_info)
{
    if (m_ams_mapping_result.empty())
        return false;

    bool valid_mapping_result = true;
    int invalid_count = 0;
    for (int i = 0; i < m_ams_mapping_result.size(); i++) {
        if (m_ams_mapping_result[i].tray_id == -1) {
            valid_mapping_result = false;
            invalid_count++;
        }
    }

    if (invalid_count == m_ams_mapping_result.size()) {
        return false;
    } else {
        json          j = json::array();
        json mapping_info_json = json::array();

        for (int i = 0; i < wxGetApp().preset_bundle->filament_presets.size(); i++) {
            int tray_id = -1;
            json mapping_item;
            mapping_item["ams"] = tray_id;
            mapping_item["targetColor"] = "";
            mapping_item["filamentId"] = "";
            mapping_item["filamentType"] = "";

            for (int k = 0; k < m_ams_mapping_result.size(); k++) {
                if (m_ams_mapping_result[k].id == i) {
                    tray_id = m_ams_mapping_result[k].tray_id;
                    mapping_item["ams"]             = tray_id;
                    mapping_item["filamentType"]    = m_filaments[k].type;
                    auto it = wxGetApp().preset_bundle->filaments.find_preset(wxGetApp().preset_bundle->filament_presets[i]);
                    if (it != nullptr) {
                        mapping_item["filamentId"] = it->filament_id;
                    }
                    //convert #RRGGBB to RRGGBBAA
                    if (m_filaments[k].color.size() > 6) {
                        mapping_item["sourceColor"] = m_filaments[k].color.substr(1, 6) + "FF";
                    } else {
                        mapping_item["sourceColor"]     = m_filaments[k].color;
                    }
                    mapping_item["targetColor"]     = m_ams_mapping_result[k].color;
                }
            }
            j.push_back(tray_id);
            mapping_info_json.push_back(mapping_item);
        }
        mapping_array_str = j.dump();
        ams_mapping_info = mapping_info_json.dump();
        return valid_mapping_result;
    }
    return true;
}

void SelectMachineDialog::prepare(int print_plate_idx)
{
    m_print_plate_idx = print_plate_idx;
}

void SelectMachineDialog::update_ams_status_msg(wxString msg, bool is_warning)
{
    auto colour = is_warning ? wxColour(0xFF, 0x6F, 0x00):wxColour(0x6B, 0x6B, 0x6B);
    m_statictext_ams_msg->SetForegroundColour(colour);

    if (msg.empty()) {
        if (!m_statictext_ams_msg->GetLabel().empty()) {
            m_statictext_ams_msg->SetLabel(wxEmptyString);
            m_statictext_ams_msg->Hide();
            Layout();
            Fit();
        }
    } else {
        msg = format_text(msg);

        auto str_new = msg.ToStdString();
        stripWhiteSpace(str_new);

        auto str_old = m_statictext_ams_msg->GetLabel().ToStdString();
        stripWhiteSpace(str_old);

        if (str_new != str_old) {
            if (m_statictext_ams_msg->GetLabel() != msg) {
                m_statictext_ams_msg->SetLabel(msg);
                m_statictext_ams_msg->SetMinSize(wxSize(FromDIP(400), -1));
                m_statictext_ams_msg->SetMaxSize(wxSize(FromDIP(400), -1));
                m_statictext_ams_msg->Wrap(FromDIP(400));
                m_statictext_ams_msg->Show();
                Layout();
                Fit();
            }
        }
    }
}

void SelectMachineDialog::update_priner_status_msg(wxString msg, bool is_warning)
{
    auto colour = is_warning ? wxColour(0xFF, 0x6F, 0x00) : wxColour(0x6B, 0x6B, 0x6B);
    m_statictext_printer_msg->SetForegroundColour(colour);

    if (msg.empty()) {
        if (!m_statictext_printer_msg->GetLabel().empty()) {
            m_statictext_printer_msg->SetLabel(wxEmptyString);
            m_statictext_printer_msg->Hide();
            Layout();
            Fit();
        }
    } else {
        msg          = format_text(msg);

        auto str_new = msg.ToStdString();
        stripWhiteSpace(str_new);

        auto str_old = m_statictext_printer_msg->GetLabel().ToStdString();
        stripWhiteSpace(str_old);

        if (str_new != str_old) {
            if (m_statictext_printer_msg->GetLabel() != msg) {
                m_statictext_printer_msg->SetLabel(msg);
                m_statictext_printer_msg->SetMinSize(wxSize(FromDIP(400), -1));
                m_statictext_printer_msg->SetMaxSize(wxSize(FromDIP(400), -1));
                m_statictext_printer_msg->Wrap(FromDIP(400));
                m_statictext_printer_msg->Show();
                Layout();
                Fit();
            }
        }
    }
}

void SelectMachineDialog::update_print_status_msg(wxString msg, bool is_warning, bool is_printer_msg)
{
    if (is_printer_msg) {
        update_ams_status_msg(wxEmptyString, false);
        update_priner_status_msg(msg, is_warning);
    } else {
        update_ams_status_msg(msg, is_warning);
        update_priner_status_msg(wxEmptyString, false);
    }
}

bool SelectMachineDialog::has_tips(MachineObject* obj)
{
    if (!obj) return false;

    // must set to a status if return true
    if (select_timelapse->IsShown() &&
        m_checkbox_list["timelapse"]->GetValue()) {
        if (obj->get_sdcard_state() == MachineObject::SdcardState::NO_SDCARD) {
            show_status(PrintDialogStatus::PrintStatusTimelapseNoSdcard);
            return true;
        }
    }

    return false;
}

void SelectMachineDialog::show_status(PrintDialogStatus status, std::vector<wxString> params)
{
    if (m_print_status != status)
        BOOST_LOG_TRIVIAL(info) << "select_machine_dialog: show_status = " << status << "(" << get_print_status_info(status) << ")";
    m_print_status = status;

    // m_comboBox_printer
    if (status == PrintDialogStatus::PrintStatusRefreshingMachineList)
        m_comboBox_printer->Disable();
    else
        m_comboBox_printer->Enable();

    // m_panel_warn m_simplebook
    if (status == PrintDialogStatus::PrintStatusSending) {
        sending_mode();
    } else {
        prepare_mode();
    }

    // other
    if (status == PrintDialogStatus::PrintStatusInit) {
        update_print_status_msg(wxEmptyString, false, false);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusNoUserLogin) {
        wxString msg_text = _L("No login account, only printers in LAN mode are displayed");
        update_print_status_msg(msg_text, false, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    }else if (status == PrintDialogStatus::PrintStatusInvalidPrinter) {
        update_print_status_msg(wxEmptyString, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusConnectingServer) {
        wxString msg_text = _L("Connecting to server");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusReading) {
        wxString msg_text = _L("Synchronizing device information");
        update_print_status_msg(msg_text, false, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusReadingFinished) {
        update_print_status_msg(wxEmptyString, false, true);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusReadingTimeout) {
        wxString msg_text = _L("Synchronizing device information time out");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusInUpgrading) {
        wxString msg_text = _L("Cannot send the print job when the printer is updating firmware");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusInSystemPrinting) {
        wxString msg_text = _L("The printer is executing instructions. Please restart printing after it ends");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusInPrinting) {
        wxString msg_text = _L("The printer is busy on other print job");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusDisableAms) {
        update_print_status_msg(wxEmptyString, false, false);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusNeedUpgradingAms) {
        wxString msg_text;
        if (params.size() > 0)
            msg_text = wxString::Format(_L("Filament %s exceeds the number of AMS slots. Please update the printer firmware to support AMS slot assignment."), params[0]);
        else
            msg_text = _L("Filament exceeds the number of AMS slots. Please update the printer firmware to support AMS slot assignment.");
        update_print_status_msg(msg_text, true, false);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusAmsMappingSuccess){
        wxString msg_text = _L("Filaments to AMS slots mappings have been established. You can click a filament above to change its mapping AMS slot");
        update_print_status_msg(msg_text, false, false);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusAmsMappingInvalid) {
        wxString msg_text = _L("Please click each filament above to specify its mapping AMS slot before sending the print job");
        update_print_status_msg(msg_text, true, false);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusAmsMappingU0Invalid) {
        wxString msg_text;
        if (params.size() > 1)
            msg_text = wxString::Format(_L("Filament %s does not match the filament in AMS slot %s. Please update the printer firmware to support AMS slot assignment."), params[0], params[1]);
        else
            msg_text = _L("Filament does not match the filament in AMS slot. Please update the printer firmware to support AMS slot assignment.");
        update_print_status_msg(msg_text, true, false);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusAmsMappingValid) {
        wxString msg_text = _L("Filaments to AMS slots mappings have been established. You can click a filament above to change its mapping AMS slot");
        update_print_status_msg(msg_text, false, false);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusRefreshingMachineList) {
        update_print_status_msg(wxEmptyString, false, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(false);
    } else if (status == PrintDialogStatus::PrintStatusSending) {
        Enable_Send_Button(false);
        Enable_Refresh_Button(false);
    } else if (status == PrintDialogStatus::PrintStatusSendingCanceled) {
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusLanModeNoSdcard) {
        wxString msg_text = _L("An SD card needs to be inserted before printing via LAN.");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusAmsMappingByOrder) {
        wxString msg_text = _L("The printer firmware only supports sequential mapping of filament => AMS slot.");
        update_print_status_msg(msg_text, false, false);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusNoSdcard) {
        wxString msg_text = _L("An SD card needs to be inserted before printing.");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusTimelapseNoSdcard) {
        wxString msg_text = _L("An SD card needs to be inserted to record timelapse.");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusNeedForceUpgrading) {
        wxString msg_text = _L("Cannot send the print job to a printer whose firmware is required to get updated.");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusNeedConsistencyUpgrading) {
        wxString msg_text = _L("Cannot send the print job to a printer whose firmware is required to get updated.");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusBlankPlate) {
        wxString msg_text = _L("Cannot send the print job for empty plate");
        update_print_status_msg(msg_text, true, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    }
}


void SelectMachineDialog::init_model()
{
    machine_model = new MachineListModel;
    m_dataViewListCtrl_machines->AssociateModel(machine_model.get());
    m_dataViewListCtrl_machines->AppendTextColumn("Printer Name", MachineListModel::Col_MachineName, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE, wxALIGN_NOT,
                                                  wxDATAVIEW_COL_SORTABLE);

    m_dataViewListCtrl_machines->AppendTextColumn("SN(dev_id)", MachineListModel::Col_MachineSN, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE, wxALIGN_NOT,
                                                  wxDATAVIEW_COL_RESIZABLE);

    m_dataViewListCtrl_machines->AppendTextColumn("Status", MachineListModel::Col_MachinePrintingStatus, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE, wxALIGN_NOT,
                                                  wxDATAVIEW_COL_RESIZABLE);

    m_dataViewListCtrl_machines->AppendTextColumn("TaskName", MachineListModel::Col_MachineTaskName, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE, wxALIGN_NOT,
                                                  wxDATAVIEW_COL_RESIZABLE);

    m_dataViewListCtrl_machines->AppendTextColumn("Connection", MachineListModel::Col_MachineConnection, wxDATAVIEW_CELL_INERT, wxCOL_WIDTH_AUTOSIZE, wxALIGN_NOT,
                                                  wxDATAVIEW_COL_RESIZABLE);
}

void SelectMachineDialog::init_bind()
{
    Bind(wxEVT_TIMER, &SelectMachineDialog::on_timer, this);
}

void SelectMachineDialog::init_timer()
{
    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
}

void SelectMachineDialog::on_cancel(wxCloseEvent &event)
{
    if (m_print_job) {
        if (m_print_job->is_running()) {
            m_print_job->cancel();
            m_print_job->join();
        }
    }
    this->EndModal(wxID_CANCEL);
}

bool SelectMachineDialog::is_same_printer_model()
{
    bool result = true;
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return result;

    MachineObject* obj_ = dev->get_selected_machine();
    assert(obj_->dev_id == m_printer_last_select);
    if (obj_ == nullptr) {
        return result;
    }

    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle && preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle) != obj_->printer_type) {
        BOOST_LOG_TRIVIAL(info) << "printer_model: source = " << preset_bundle->printers.get_edited_preset().get_printer_type(preset_bundle);
        BOOST_LOG_TRIVIAL(info) << "printer_model: target = " << obj_->printer_type;
        return false;
    }

    return true;
}

void SelectMachineDialog::show_errors(wxString &info)
{
    ConfirmBeforeSendDialog confirm_dlg(this, wxID_ANY, _L("Errors"));
    confirm_dlg.update_text(info);
    confirm_dlg.on_show();
}

void SelectMachineDialog::on_ok_btn(wxCommandEvent &event)
{
    std::vector<wxString> confirm_text;
    confirm_text.push_back(_L("Please check the following infomation and click Confirm to continue sending print:\n"));

#if 0
    //Check Printer Model Id
    bool is_same_printer_type = is_same_printer_model();
    if (!is_same_printer_type)
        confirm_text.push_back(_L("The printer type used to generate G-code is not the same type as the currently selected physical printer. It is recommend to re-slice by selecting the same printer type.\n"));
#else
    bool is_same_printer_type = true;
#endif

    //Check slice warnings
    bool has_slice_warnings = false;
    PartPlate* plate = m_plater->get_partplate_list().get_curr_plate();
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();

    if(!dev) return;

    MachineObject* obj_ = dev->get_selected_machine();
    for (auto warning : plate->get_slice_result()->warnings) {
        if (warning.msg == BED_TEMP_TOO_HIGH_THAN_FILAMENT) {
            if ((obj_->printer_type == "BL-P001" || obj_->printer_type == "BL-P002")) {
                confirm_text.push_back(Plater::get_slice_warning_string(warning) + "\n");
                has_slice_warnings = true;
            }
        }
        else {
            has_slice_warnings = true;
        }
    }


    //check for unidentified material
    auto mapping_result = m_mapping_popup.parse_ams_mapping(obj_->amsList);
    auto has_unknown_filament = false;
    // check if ams mapping is has errors, tpu
    bool has_tpu_filament = false;

    for (auto i = 0; i < m_ams_mapping_result.size(); i++) {
        auto tid = m_ams_mapping_result[i].tray_id;
        std::string filament_type = boost::to_upper_copy(m_ams_mapping_result[i].type);
        if (filament_type == "TPU") {
            has_tpu_filament = true;
        }
        for (auto miter : mapping_result) {
            //matching
            if (miter.id == tid) {
                if (miter.type == TrayType::THIRD || miter.type == TrayType::EMPTY) {
                    has_unknown_filament = true;
                    break;
                }
            }
        }
    }

    if (has_tpu_filament && obj_->has_ams() && ams_check->GetValue()) {
        wxString tpu_tips = wxString::Format(_L("The %s filament is too soft to be used with the AMS"), "TPU");
        show_errors(tpu_tips);
        return;
    }

    if (has_unknown_filament) {
        has_slice_warnings = true;
        confirm_text.push_back(_L("There are some unknown filaments in the AMS mappings. Please check whether they are the required filaments. If they are okay, press \"Confirm\" to start printing.") + "\n");
    }

    if (!is_same_printer_type || has_slice_warnings) {
        wxString confirm_title = _L("Warning");
        ConfirmBeforeSendDialog confirm_dlg(this, wxID_ANY, confirm_title);
        confirm_dlg.Bind(EVT_SECONDARY_CHECK_CONFIRM, [this, &confirm_dlg](wxCommandEvent& e) {
            confirm_dlg.on_hide();
            this->on_ok();
        });
        wxString info_msg = wxEmptyString;

        for (auto i = 0; i < confirm_text.size(); i++) {
            if (i == 0) {
                info_msg += confirm_text[i];
            }
            else {
                info_msg += wxString::Format("%d:%s\n",i, confirm_text[i]);
            }

        }
        confirm_dlg.update_text(info_msg);
        confirm_dlg.on_show();

    } else {
        this->on_ok();
    }
}

void SelectMachineDialog::on_ok()
{
    BOOST_LOG_TRIVIAL(info) << "print_job: on_ok to send";
    m_is_canceled = false;
    Enable_Send_Button(false);
    if (m_is_in_sending_mode)
        return;

    int result = 0;
    if (m_printer_last_select.empty()) {
        return;
    }

    DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    MachineObject *obj_ = dev->get_selected_machine();
    assert(obj_->dev_id == m_printer_last_select);
    if (obj_ == nullptr) {
        return;
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", print_job: for send task, current printer id =  " << m_printer_last_select << std::endl;
    show_status(PrintDialogStatus::PrintStatusSending);

    m_status_bar->reset();
    m_status_bar->set_prog_block();
    m_status_bar->set_cancel_callback_fina([this]() {
        BOOST_LOG_TRIVIAL(info) << "print_job: enter canceled";
        if (m_print_job) {
            if (m_print_job->is_running()) {
                BOOST_LOG_TRIVIAL(info) << "print_job: canceled";
                m_print_job->cancel();
            }
            m_print_job->join();
        }
        m_is_canceled = true;
        wxCommandEvent* event = new wxCommandEvent(EVT_PRINT_JOB_CANCEL);
        wxQueueEvent(this, event);
    });

    if (m_is_canceled) {
        BOOST_LOG_TRIVIAL(info) << "print_job: m_is_canceled";
        m_status_bar->set_status_text(task_canceled_text);
        return;
    }

    // enter sending mode
    sending_mode();

    // get ams_mapping_result
    std::string ams_mapping_array;
    std::string ams_mapping_info;
    get_ams_mapping_result(ams_mapping_array, ams_mapping_info);

    result = m_plater->send_gcode(m_print_plate_idx, [this](int export_stage, int current, int total, bool &cancel) {
        if (this->m_is_canceled) return;
        bool     cancelled = false;
        wxString msg       = _L("Preparing print job");
        m_status_bar->update_status(msg, cancelled, 10, true);
        m_export_3mf_cancel = cancel = cancelled;
    });

    if (m_is_canceled || m_export_3mf_cancel) {
        BOOST_LOG_TRIVIAL(info) << "print_job: m_export_3mf_cancel or m_is_canceled";
        m_status_bar->set_status_text(task_canceled_text);
        return;
    }

    if (result < 0) {
        wxString msg = _L("Abnormal print file data. Please slice again");
        m_status_bar->set_status_text(msg);
        return;
    }

    // export config 3mf if needed
    if (!obj_->is_lan_mode_printer()) {
        result = m_plater->export_config_3mf(m_print_plate_idx);
        if (result < 0) {
            BOOST_LOG_TRIVIAL(trace) << "export_config_3mf failed, result = " << result;
            return;
        }
    }
    if (m_is_canceled || m_export_3mf_cancel) {
        BOOST_LOG_TRIVIAL(info) << "print_job: m_export_3mf_cancel or m_is_canceled";
        m_status_bar->set_status_text(task_canceled_text);
        return;
    }

    m_print_job                = std::make_shared<PrintJob>(m_status_bar, m_plater, m_printer_last_select);
    m_print_job->m_dev_ip      = obj_->dev_ip;
    m_print_job->m_access_code = obj_->access_code;
    m_print_job->connection_type = obj_->connection_type();
    m_print_job->set_project_name(m_current_project_name.utf8_string());

    if (obj_->is_support_ams_mapping()) {
        m_print_job->task_ams_mapping = ams_mapping_array;
        m_print_job->task_ams_mapping_info = ams_mapping_info;
    } else {
        m_print_job->task_ams_mapping = "";
        m_print_job->task_ams_mapping_info = "";
    }

    m_print_job->has_sdcard = obj_->has_sdcard();

    if (obj_->is_only_support_cloud_print()) {
        m_print_job->cloud_print_only = true;
    }


    bool timelapse_option = select_timelapse->IsShown() ? m_checkbox_list["timelapse"]->GetValue() : true;

    m_print_job->set_print_config(
        MachineBedTypeString[0],
        m_checkbox_list["bed_leveling"]->GetValue(),
        m_checkbox_list["flow_cali"]->GetValue(),
        false,
        timelapse_option,
        true);

    if (obj_->has_ams()) {
        m_print_job->task_use_ams = ams_check->GetValue();
    } else {
        m_print_job->task_use_ams = false;
    }

    BOOST_LOG_TRIVIAL(info) << "print_job: timelapse_option = " << timelapse_option;
    BOOST_LOG_TRIVIAL(info) << "print_job: use_ams = " << m_print_job->task_use_ams;

    m_print_job->on_success([this]() { finish_mode(); });

    wxCommandEvent evt(m_plater->get_print_finished_event());
    m_print_job->start();
    BOOST_LOG_TRIVIAL(info) << "print_job: start print job";
}

void SelectMachineDialog::update_user_machine_list()
{
    NetworkAgent* m_agent = wxGetApp().getAgent();
    if (m_agent && m_agent->is_user_login()) {
        boost::thread get_print_info_thread = Slic3r::create_thread([&] {
            NetworkAgent* agent = wxGetApp().getAgent();
            unsigned int http_code;
            std::string body;
            int result = agent->get_user_print_info(&http_code, &body);
            if (result == 0) {
                m_print_info = body;
            }
            else {
                m_print_info = "";
            }
            wxCommandEvent event(EVT_UPDATE_USER_MACHINE_LIST);
            event.SetEventObject(this);
            wxPostEvent(this, event);
        });
    } else {
        wxCommandEvent event(EVT_UPDATE_USER_MACHINE_LIST);
        event.SetEventObject(this);
        wxPostEvent(this, event);
    }
}

void SelectMachineDialog::on_refresh(wxCommandEvent &event)
{
    BOOST_LOG_TRIVIAL(info) << "m_printer_last_select: on_refresh";
    show_status(PrintDialogStatus::PrintStatusRefreshingMachineList);

    update_user_machine_list();
}

void SelectMachineDialog::on_set_finish_mapping(wxCommandEvent &evt)
{
    auto selection_data = evt.GetString();
    auto selection_data_arr = wxSplit(selection_data.ToStdString(), '|');

    BOOST_LOG_TRIVIAL(info) << "The ams mapping selection result: data is " << selection_data;

    if (selection_data_arr.size() == 5) {
         for (auto i = 0; i < m_ams_mapping_result.size(); i++) {
            if (m_ams_mapping_result[i].id == wxAtoi(selection_data_arr[4])) {
                m_ams_mapping_result[i].tray_id = evt.GetInt();
				auto ams_colour = wxColour(wxAtoi(selection_data_arr[0]), wxAtoi(selection_data_arr[1]), wxAtoi(selection_data_arr[2]));
				auto color = wxString::Format("%sFF", ams_colour.GetAsString(wxC2S_HTML_SYNTAX).substr(1, ams_colour.GetAsString(wxC2S_HTML_SYNTAX).size()-1));
			   m_ams_mapping_result[i].color = color.ToStdString();
            }
            BOOST_LOG_TRIVIAL(trace) << "The ams mapping result: id is " << m_ams_mapping_result[i].id << "tray_id is " << m_ams_mapping_result[i].tray_id;
         }

        MaterialHash::iterator iter = m_materialList.begin();
        while (iter != m_materialList.end()) {
            Material*        item = iter->second;
            MaterialItem *m  = item->item;
            if (item->id == m_current_filament_id) {
                auto ams_colour = wxColour(wxAtoi(selection_data_arr[0]), wxAtoi(selection_data_arr[1]), wxAtoi(selection_data_arr[2]));
                m->set_ams_info(ams_colour, selection_data_arr[3]);
            }
            iter++;
        }
    }
}

void SelectMachineDialog::on_print_job_cancel(wxCommandEvent &evt)
{
    BOOST_LOG_TRIVIAL(info) << "print_job: canceled";
    show_status(PrintDialogStatus::PrintStatusSendingCanceled);
    // enter prepare mode
    prepare_mode();
}

std::vector<std::string> SelectMachineDialog::sort_string(std::vector<std::string> strArray)
{
    std::vector<std::string> outputArray;
    std::sort(strArray.begin(), strArray.end());
    std::vector<std::string>::iterator st;
    for (st = strArray.begin(); st != strArray.end(); st++) { outputArray.push_back(*st); }

    return outputArray;
}

bool  SelectMachineDialog::is_timeout()
{
    if (timeout_count > 15 * 1000 / LIST_REFRESH_INTERVAL) {
        return true;
    }
    return false;
}

void  SelectMachineDialog::reset_timeout()
{
    timeout_count = 0;
}

void SelectMachineDialog::update_user_printer()
{
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    // update user print info
    if (!m_print_info.empty()) {
        dev->parse_user_print_info(m_print_info);
        m_print_info = "";
    }

    // clear machine list
    m_list.clear();
    m_comboBox_printer->Clear();
    std::vector<std::string>              machine_list;
    wxArrayString                         machine_list_name;
    std::map<std::string, MachineObject*> option_list;

    //user machine list
    option_list = dev->get_my_machine_list();

    // same machine only appear once
    for (auto it = option_list.begin(); it != option_list.end(); it++) {
        if (it->second && (it->second->is_online() || it->second->is_connected())) {
            machine_list.push_back(it->second->dev_name);
        }
    }



    //lan machine list
    auto lan_option_list = dev->get_local_machine_list();

    for (auto elem : lan_option_list) {
        MachineObject* mobj = elem.second;

        /* do not show printer bind state is empty */
        if (!mobj->is_avaliable()) continue;
        if (!mobj->is_online()) continue;
        if (!mobj->is_lan_mode_printer()) continue;
        /*if (mobj->is_in_printing()) {op->set_printer_state(PrinterState::BUSY);}*/

        if (!mobj->has_access_right()) {
            option_list[mobj->dev_name] = mobj;
            machine_list.push_back(mobj->dev_name);
        }
    }

    machine_list = sort_string(machine_list);
    for (auto tt = machine_list.begin(); tt != machine_list.end(); tt++) {
        for (auto it = option_list.begin(); it != option_list.end(); it++) {
            if (it->second->dev_name == *tt) {
                m_list.push_back(it->second);
                wxString dev_name_text = from_u8(it->second->dev_name);
                if (it->second->is_lan_mode_printer()) {
                    dev_name_text += "(LAN)";
                }
                machine_list_name.Add(dev_name_text);
                break;
            }
        }
    }

    m_comboBox_printer->Set(machine_list_name);

    MachineObject* obj = dev->get_selected_machine();

    if (obj) {
        if (obj->is_lan_mode_printer() && !obj->has_access_right()) {
            m_printer_last_select = "";
        }
        else {
           m_printer_last_select = obj->dev_id;
        }

    } else {
        m_printer_last_select = "";
    }

    if (m_list.size() > 0) {
        // select a default machine
        if (m_printer_last_select.empty()) {
            int def_selection = -1;
            for (int i = 0; i < m_list.size(); i++) {
                if (m_list[i]->is_lan_mode_printer() && !m_list[i]->has_access_right()) {
                    continue;
                }
                else {
                    def_selection = i;
                }
            }

            if (def_selection >= 0) {
                m_printer_last_select = m_list[def_selection]->dev_id;
                m_comboBox_printer->SetSelection(def_selection);
                wxCommandEvent event(wxEVT_COMBOBOX);
                event.SetEventObject(m_comboBox_printer);
                wxPostEvent(m_comboBox_printer, event);
            }
        }

        for (auto i = 0; i < m_list.size(); i++) {
            if (m_list[i]->dev_id == m_printer_last_select) {

                if (obj && !obj->get_lan_mode_connection_state()) {
                    m_comboBox_printer->SetSelection(i);
                    wxCommandEvent event(wxEVT_COMBOBOX);
                    event.SetEventObject(m_comboBox_printer);
                    wxPostEvent(m_comboBox_printer, event);
                }
            }
        }
    }
    else {
        m_printer_last_select = "";
        update_select_layout(nullptr);
        m_comboBox_printer->SetTextLabel("");
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "for send task, current printer id =  " << m_printer_last_select << std::endl;
}

void SelectMachineDialog::on_rename_click(wxCommandEvent& event)
{
    m_is_rename_mode = true;
    m_rename_input->GetTextCtrl()->SetValue(m_current_project_name);
    m_rename_switch_panel->SetSelection(1);
    m_rename_input->GetTextCtrl()->SetFocus();
    m_rename_input->GetTextCtrl()->SetInsertionPointEnd();
}

void SelectMachineDialog::on_rename_enter()
{
    if (m_is_rename_mode == false){
        return;
    }
    else {
        m_is_rename_mode = false;
    }

    auto     new_file_name = m_rename_input->GetTextCtrl()->GetValue();
    auto     m_valid_type = Valid;
    wxString info_line;

    const char* unusable_symbols = "<>[]:/\\|?*\"";

    const std::string unusable_suffix = PresetCollection::get_suffix_modified(); //"(modified)";
    for (size_t i = 0; i < std::strlen(unusable_symbols); i++) {
        if (new_file_name.find_first_of(unusable_symbols[i]) != std::string::npos) {
            info_line = _L("Name is invalid;") + "\n" + _L("illegal characters:") + " " + unusable_symbols;
            m_valid_type = NoValid;
            break;
        }
    }

    if (m_valid_type == Valid && new_file_name.find(unusable_suffix) != std::string::npos) {
        info_line = _L("Name is invalid;") + "\n" + _L("illegal suffix:") + "\n\t" + from_u8(PresetCollection::get_suffix_modified());
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && new_file_name.empty()) {
        info_line = _L("The name is not allowed to be empty.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && new_file_name.find_first_of(' ') == 0) {
        info_line = _L("The name is not allowed to start with space character.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && new_file_name.find_last_of(' ') == new_file_name.length() - 1) {
        info_line = _L("The name is not allowed to end with space character.");
        m_valid_type = NoValid;
    }

    if (m_valid_type != Valid) {
        MessageDialog msg_wingow(nullptr, info_line, "", wxICON_WARNING | wxOK);
        if (msg_wingow.ShowModal() == wxID_OK) {
             m_rename_switch_panel->SetSelection(0);
             m_rename_text->SetLabel(m_current_project_name);
             m_rename_normal_panel->Layout();
             return;
        }
    }

    m_current_project_name = new_file_name;
    m_rename_switch_panel->SetSelection(0);
    m_rename_text->SetLabel(m_current_project_name);
    m_rename_normal_panel->Layout();
}

void SelectMachineDialog::update_printer_combobox(wxCommandEvent &event)
{
    show_status(PrintDialogStatus::PrintStatusInit);
    update_user_printer();
}

void SelectMachineDialog::on_timer(wxTimerEvent &event)
{
    wxGetApp().reset_to_active();
    update_show_status();
}

void SelectMachineDialog::on_selection_changed(wxCommandEvent &event)
{
    /* reset timeout and reading printer info */
    m_status_bar->reset();
    timeout_count      = 0;
    m_ams_mapping_res  = false;
    ams_mapping_valid  = false;
    m_ams_mapping_result.clear();

    auto selection = m_comboBox_printer->GetSelection();
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    MachineObject* obj = nullptr;
    for (int i = 0; i < m_list.size(); i++) {
        if (i == selection) {

            //check lan mode machine
            if (m_list[i]->is_lan_mode_printer() && !m_list[i]->has_access_right()) {
                ConnectPrinterDialog dlg(wxGetApp().mainframe, wxID_ANY, _L("Input access code"));
                dlg.set_machine_object(m_list[i]);
                auto res = dlg.ShowModal();
                m_printer_last_select = "";
                m_comboBox_printer->SetSelection(-1);
                m_comboBox_printer->Refresh();
                m_comboBox_printer->Update();
            }

            m_printer_last_select = m_list[i]->dev_id;
            obj = m_list[i];

            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "for send task, current printer id =  " << m_printer_last_select << std::endl;
            break;
        }
    }

    if (obj && !obj->get_lan_mode_connection_state()) {
        obj->command_get_version();
        obj->command_request_push_all();
        dev->set_selected_machine(m_printer_last_select);
        // Has changed machine unrecoverably
        GUI::wxGetApp().sidebar().load_ams_list(obj->amsList);
        update_select_layout(obj);
    } else {
        BOOST_LOG_TRIVIAL(error) << "on_selection_changed dev_id not found";
        return;
    }

    reset_ams_material();

    update_show_status();
}

void SelectMachineDialog::update_ams_check(MachineObject* obj)
{
    if (obj && obj->is_function_supported(FUNC_USE_AMS)
        && obj->ams_support_use_ams
        && obj->has_ams()) {
        select_use_ams->Show();
    } else {
        select_use_ams->Hide();
    }
}

void SelectMachineDialog::update_show_status()
{
    // refreshing return
    if (get_status() == PrintDialogStatus::PrintStatusRefreshingMachineList)
        return;

    if (get_status() == PrintDialogStatus::PrintStatusSending)
        return;

    if (get_status() == PrintDialogStatus::PrintStatusSendingCanceled)
        return;

    NetworkAgent* agent = Slic3r::GUI::wxGetApp().getAgent();
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!agent) {
        update_ams_check(nullptr);
        return;
    }
    if (!dev) return;
    dev->check_pushing();

    PartPlate* plate = m_plater->get_partplate_list().get_curr_plate();
    // blank plate has no valid gcode file
    if (plate&& !plate->is_valid_gcode_file()) {
        show_status(PrintDialogStatus::PrintStatusBlankPlate);
        return;
    }

    MachineObject* obj_ = dev->get_my_machine(m_printer_last_select);
    if (!obj_) {
        update_ams_check(nullptr);
        if (agent) {
            if (agent->is_user_login()) {
                show_status(PrintDialogStatus::PrintStatusInvalidPrinter);
            }
            else {
                show_status(PrintDialogStatus::PrintStatusNoUserLogin);
            }
        }
        return;
    }

    /* check cloud machine connections */
    if (!obj_->is_lan_mode_printer()) {
        if (!agent->is_server_connected()) {
            agent->refresh_connection();
            show_status(PrintDialogStatus::PrintStatusConnectingServer);
            reset_timeout();
            return;
        }
    }

    if (!obj_->is_info_ready()) {
        if (is_timeout()) {
            m_ams_mapping_result.clear();
            sync_ams_mapping_result(m_ams_mapping_result);
            show_status(PrintDialogStatus::PrintStatusReadingTimeout);
            return;
        }
        else {
            timeout_count++;
            show_status(PrintDialogStatus::PrintStatusReading);
            return;
        }
        return;
    }

    reset_timeout();
    update_ams_check(obj_);

    // do ams mapping if no ams result
    if (obj_->has_ams() && m_ams_mapping_result.empty()) {
        if (obj_->ams_support_use_ams) {
            if (ams_check->GetValue()) {
                do_ams_mapping(obj_);
            } else {
                m_ams_mapping_result.clear();
                sync_ams_mapping_result(m_ams_mapping_result);
            }
        }
    }

    // reading done
    if (wxGetApp().app_config && wxGetApp().app_config->get("internal_debug").empty()) {
        if (obj_->upgrade_force_upgrade) {
            show_status(PrintDialogStatus::PrintStatusNeedForceUpgrading);
            return;
        }

        if (obj_->upgrade_consistency_request) {
            show_status(PrintStatusNeedConsistencyUpgrading);
            return;
        }
    }

    if (obj_->is_in_upgrading()) {
        show_status(PrintDialogStatus::PrintStatusInUpgrading);
        return;
    }
    else if (obj_->is_system_printing()) {
        show_status(PrintDialogStatus::PrintStatusInSystemPrinting);
        return;
    }
    else if (obj_->is_in_printing()) {
        show_status(PrintDialogStatus::PrintStatusInPrinting);
        return;
    }
    else if (!obj_->is_function_supported(PrinterFunction::FUNC_PRINT_WITHOUT_SD) && (obj_->get_sdcard_state() == MachineObject::SdcardState::NO_SDCARD)) {
        show_status(PrintDialogStatus::PrintStatusNoSdcard);
        return;
    }

    // check sdcard when if lan mode printer
    if (obj_->is_lan_mode_printer()) {
        if (obj_->get_sdcard_state() == MachineObject::SdcardState::NO_SDCARD) {
            show_status(PrintDialogStatus::PrintStatusLanModeNoSdcard);
            return;
        }
    }

    // no ams
    if (!obj_->has_ams()) {
        if (!has_tips(obj_))
            show_status(PrintDialogStatus::PrintStatusReadingFinished);
        return;
    }

    if (obj_->ams_support_use_ams) {
        if (!ams_check->GetValue()) {
            m_ams_mapping_result.clear();
            sync_ams_mapping_result(m_ams_mapping_result);
            show_status(PrintDialogStatus::PrintStatusDisableAms);
            return;
        }
    }

    // do ams mapping if no ams result
    if (m_ams_mapping_result.empty()) {
        do_ams_mapping(obj_);
    }

    if (!obj_->is_support_ams_mapping()) {
        int exceed_index = -1;
        if (obj_->is_mapping_exceed_filament(m_ams_mapping_result, exceed_index)) {
            std::vector<wxString> params;
            params.push_back(wxString::Format("%02d", exceed_index+1));
            show_status(PrintDialogStatus::PrintStatusNeedUpgradingAms, params);
        } else {
            if (obj_->is_valid_mapping_result(m_ams_mapping_result)) {
                show_status(PrintDialogStatus::PrintStatusAmsMappingByOrder);
            } else {
                int mismatch_index = -1;
                for (int i = 0; i < m_ams_mapping_result.size(); i++) {
                    if (m_ams_mapping_result[i].mapping_result == MappingResult::MAPPING_RESULT_TYPE_MISMATCH) {
                        mismatch_index = m_ams_mapping_result[i].id;
                        break;
                    }
                }
                std::vector<wxString> params;
                if (mismatch_index >= 0) {
                    params.push_back(wxString::Format("%02d", mismatch_index+1));
                    params.push_back(wxString::Format("%02d", mismatch_index+1));
                }
                show_status(PrintDialogStatus::PrintStatusAmsMappingU0Invalid, params);
            }
        }
        return;
    }

    if (m_ams_mapping_res) {
        show_status(PrintDialogStatus::PrintStatusAmsMappingSuccess);
        return;
    }
    else {
        if (obj_->is_valid_mapping_result(m_ams_mapping_result)) {
            if (!has_tips(obj_))
                show_status(PrintDialogStatus::PrintStatusAmsMappingValid);
            return;
        }
        else {
            show_status(PrintDialogStatus::PrintStatusAmsMappingInvalid);
            return;
        }
    }
}

bool SelectMachineDialog::is_show_timelapse()
{
    auto compare_version = [](const std::string &version1, const std::string &version2) -> bool {
        int i = 0, j = 0;
        int max_size = std::max(version1.size(), version2.size());
        while (i < max_size || j < max_size) {
            int v1 = 0, v2 = 0;
            while (i < version1.size() && version1[i] != '.') v1 = 10 * v1 + (version1[i++] - '0');
            while (j < version2.size() && version2[j] != '.') v2 = 10 * v2 + (version2[j++] - '0');
            if (v1 > v2) return true;
            if (v1 < v2) return false;
            ++i;
            ++j;
        }
        return false;
    };

    std::string standard_version = "01.04.00.00";
    PartPlate *plate      = m_plater->get_partplate_list().get_curr_plate();
    fs::path   gcode_path = plate->get_tmp_gcode_path();

    std::string   line;
    std::ifstream gcode_file;
    gcode_file.open(gcode_path.string());
    if (gcode_file.fail()) {
    } else {
        bool is_version = false;
        while (gcode_file >> line) {
            if (is_version) {
                if (compare_version(standard_version, line)) {
                    gcode_file.close();
                    return false;
                }
                break;
            }
            if (line == "BambuStudio")
                is_version = true;
        }
    }
    gcode_file.close();
    return true;
}

void SelectMachineDialog::reset_ams_material()
{
    MaterialHash::iterator iter = m_materialList.begin();
    while (iter != m_materialList.end()) {
        int           id = iter->first;
        Material* item = iter->second;
        MaterialItem* m = item->item;
        wxString ams_id = "-";
        wxColour ams_col = wxColour(0xEE, 0xEE, 0xEE);
        m->set_ams_info(ams_col, ams_id);
        iter++;
    }
}

void SelectMachineDialog::Enable_Refresh_Button(bool en)
{
    if (!en) {
        if (m_button_refresh->IsEnabled()) {
            m_button_refresh->Disable();
            m_button_refresh->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
            m_button_refresh->SetBorderColor(wxColour(0x90, 0x90, 0x90));
        }
    } else {
        if (!m_button_refresh->IsEnabled()) {
            m_button_refresh->Enable();
            m_button_refresh->SetBackgroundColor(btn_bg_enable);
            m_button_refresh->SetBorderColor(btn_bg_enable);
        }
    }
}

void SelectMachineDialog::Enable_Send_Button(bool en)
{
    if (!en) {
        if (m_button_ensure->IsEnabled()) {
            m_button_ensure->Disable();
            m_button_ensure->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
            m_button_ensure->SetBorderColor(wxColour(0x90, 0x90, 0x90));
        }
    } else {
        if (!m_button_ensure->IsEnabled()) {
            m_button_ensure->Enable();
            m_button_ensure->SetBackgroundColor(btn_bg_enable);
            m_button_ensure->SetBorderColor(btn_bg_enable);
        }
    }
}

void SelectMachineDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_button_refresh->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_refresh->SetCornerRadius(FromDIP(12));
    m_button_ensure->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_ensure->SetCornerRadius(FromDIP(12));
    m_status_bar->msw_rescale();
    Fit();
    Refresh();
}

wxImage *SelectMachineDialog::LoadImageFromBlob(const unsigned char *data, int size)
{
    if (data != NULL) {
        wxMemoryInputStream mi(data, size);
        wxImage *           img = new wxImage(mi, wxBITMAP_TYPE_ANY);
        if (img != NULL && img->IsOk()) return img;
        // wxLogDebug( wxT("DB::LoadImageFromBlob error: data=%p size=%d"), data, size);
        // caller is responsible for deleting the pointer
        delete img;
    }
    return NULL;
}

void SelectMachineDialog::set_default()
{
    //project name
    m_rename_switch_panel->SetSelection(0);

    wxString filename = m_plater->get_export_gcode_filename("", false,
        m_print_plate_idx == PLATE_ALL_IDX ?true:false);

    if (m_print_plate_idx == PLATE_ALL_IDX && filename.empty()) {
        filename = _L("Untitled");
    }

    if (filename.empty()) {
        filename = m_plater->get_export_gcode_filename("", true);
        if (std::strstr(filename.c_str(), _L("Untitled").c_str()) == NULL) {
            filename = wxString::Format("Untitled%s",filename);
        }
    }

    fs::path filename_path(filename.c_str());
    m_current_project_name = wxString::FromUTF8(filename_path.filename().string());
    m_rename_text->SetLabelText(m_current_project_name);
    m_rename_normal_panel->Layout();


    //clear combobox
    m_list.clear();
    m_comboBox_printer->Clear();
    m_printer_last_select = "";
    m_print_info = "";
    m_comboBox_printer->SetValue(wxEmptyString);
    m_comboBox_printer->Enable();
    // rset status bar
    m_status_bar->reset();

    NetworkAgent* agent = wxGetApp().getAgent();
    if (agent) {
        if (agent->is_user_login()) {
            show_status(PrintDialogStatus::PrintStatusInit);
        } else {
            show_status(PrintDialogStatus::PrintStatusNoUserLogin);
        }
    }
    select_bed->Show();
    select_flow->Show();

    // checkbox default values
    m_checkbox_list["bed_leveling"]->SetValue(true);
    m_checkbox_list["flow_cali"]->SetValue(true);
    m_checkbox_list["timelapse"]->SetValue(true);
    ams_check->SetValue(true);

    // thumbmail
    //wxBitmap bitmap;
    ThumbnailData &data   = m_plater->get_partplate_list().get_curr_plate()->thumbnail_data;
    if (data.is_valid()) {
        wxImage image(data.width, data.height);
        image.InitAlpha();
        for (unsigned int r = 0; r < data.height; ++r) {
            unsigned int rr = (data.height - 1 - r) * data.width;
            for (unsigned int c = 0; c < data.width; ++c) {
                unsigned char *px = (unsigned char *) data.pixels.data() + 4 * (rr + c);
                image.SetRGB((int) c, (int) r, px[0], px[1], px[2]);
                image.SetAlpha((int) c, (int) r, px[3]);
            }
        }
        image  = image.Rescale(FromDIP(256), FromDIP(256));
        m_thumbnailPanel->set_thumbnail(image);
        //bitmap = wxBitmap(image);
    }

    //m_staticbitmap->SetBitmap(bitmap);
    //sizer_thumbnail->Layout();

    std::vector<std::string> materials;
    std::vector<std::string> display_materials;
    {
        auto preset_bundle = wxGetApp().preset_bundle;
        for (auto filament_name : preset_bundle->filament_presets) {
            for (auto iter = preset_bundle->filaments.lbegin(); iter != preset_bundle->filaments.end(); iter++) {
                if (filament_name.compare(iter->name) == 0) {
                    std::string display_filament_type;
                    std::string filament_type = iter->config.get_filament_type(display_filament_type);
                    display_materials.push_back(display_filament_type);
                    materials.push_back(filament_type);
                }
            }
        }
    }

    // material info
    auto        extruders = wxGetApp().plater()->get_partplate_list().get_curr_plate()->get_used_extruders();
    BitmapCache bmcache;

    MaterialHash::iterator iter = m_materialList.begin();
    while (iter != m_materialList.end()) {
        int       id   = iter->first;
        Material *item = iter->second;
        item->item->Destroy();
        delete item;
        iter++;
    }

    m_sizer_material->Clear();
    m_materialList.clear();
    m_filaments.clear();

    for (auto i = 0; i < extruders.size(); i++) {
        auto          extruder = extruders[i] - 1;
        auto          colour   = wxGetApp().preset_bundle->project_config.opt_string("filament_colour", (unsigned int) extruder);
        unsigned char rgb[3];
        bmcache.parse_color(colour, rgb);

        auto          colour_rgb = wxColour((int) rgb[0], (int) rgb[1], (int) rgb[2]);
        if (extruder >= materials.size() || extruder < 0 || extruder >= display_materials.size())
            continue;

       /* if (m_materialList.size() == 0) {
            auto tips_panel = new wxPanel(m_scrollable_region, wxID_ANY);
            tips_panel->SetSize(wxSize(60,40));
            tips_panel->SetMinSize(wxSize(60,40));
            tips_panel->SetMaxSize(wxSize(60,40));
            tips_panel->SetBackgroundColour(*wxRED);
            m_sizer_material->Add(tips_panel, 0, wxALL, FromDIP(4));
        }*/

        MaterialItem *item = new MaterialItem(m_scrollable_region, colour_rgb, _L(display_materials[extruder]));
        m_sizer_material->Add(item, 0, wxALL, FromDIP(4));

        item->Bind(wxEVT_LEFT_UP, [this, item, materials, extruder](wxMouseEvent &e) {

        });

        item->Bind(wxEVT_LEFT_DOWN, [this, item, materials, extruder](wxMouseEvent &e) {
            MaterialHash::iterator iter = m_materialList.begin();
            while (iter != m_materialList.end()) {
                int           id   = iter->first;
                Material *    item = iter->second;
                MaterialItem *m    = item->item;
                m->on_normal();
                iter++;
            }

            m_current_filament_id = extruder;
            item->on_selected();


            auto    mouse_pos = ClientToScreen(e.GetPosition());
            wxPoint rect      = item->ClientToScreen(wxPoint(0, 0));
            // update ams data
            DeviceManager *dev_manager = Slic3r::GUI::wxGetApp().getDeviceManager();
            if (!dev_manager) return;
            MachineObject *obj_ = dev_manager->get_selected_machine();

            if (obj_ && obj_->is_support_ams_mapping()) {
                if (m_mapping_popup.IsShown()) return;
                wxPoint pos = item->ClientToScreen(wxPoint(0, 0));
                pos.y += item->GetRect().height;
                m_mapping_popup.Position(pos, wxSize(0, 0));

                if (obj_ && obj_->has_ams() && ams_check->GetValue()) {
                    m_mapping_popup.set_current_filament_id(extruder);
                    m_mapping_popup.set_tag_texture(materials[extruder]);
                    m_mapping_popup.update_ams_data(obj_->amsList);
                    m_mapping_popup.Popup();
                }
            }
        });

        Material *material_item = new Material();
        material_item->id       = extruder;
        material_item->item     = item;
        m_materialList[i]       = material_item;

        // build for ams mapping
        if (extruder < materials.size() && extruder >= 0) {
            FilamentInfo info;
            info.id    = extruder;
            info.type  = materials[extruder];
            info.color = colour_rgb.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
            m_filaments.push_back(info);
        }
    }

    if (extruders.size() <= 4) {
        m_sizer_material->SetCols(extruders.size());
    } else {
        m_sizer_material->SetCols(4);
    }

    m_scrollable_region->Layout();
    m_scrollable_region->Fit();

    //m_scrollable_view->Layout();
    //m_scrollable_view->Fit();

    m_scrollable_view->SetSize(m_scrollable_region->GetSize());
    m_scrollable_view->SetMinSize(m_scrollable_region->GetSize());
    m_scrollable_view->SetMaxSize(m_scrollable_region->GetSize());

    Layout();
    Fit();


    wxSize screenSize = wxGetDisplaySize();
    auto dialogSize = this->GetSize();

    #ifdef __WINDOWS__
    if (screenSize.y < dialogSize.y) {
        m_need_adaptation_screen = true;
        m_scrollable_view->SetScrollRate(0, 5);
        m_scrollable_view->SetSize(wxSize(-1, FromDIP(220)));
        m_scrollable_view->SetMinSize(wxSize(-1, FromDIP(220)));
        m_scrollable_view->SetMaxSize(wxSize(-1, FromDIP(220)));
    } else {
        /* m_scrollable_view->SetSize(m_scrollable_region->GetSize());
         m_scrollable_view->SetMinSize(m_scrollable_region->GetSize());
         m_scrollable_view->SetMaxSize(m_scrollable_region->GetSize());*/
        m_scrollable_view->SetScrollRate(0, 0);
    }
    #endif // __WXOSX_MAC__


    reset_ams_material();

    // basic info
    auto       aprint_stats = m_plater->get_partplate_list().get_current_fff_print().print_statistics();
    wxString   time;
    PartPlate *plate = m_plater->get_partplate_list().get_curr_plate();
    if (plate) {
        if (plate->get_slice_result()) { time = wxString::Format("%s", get_bbl_monitor_time_dhm(plate->get_slice_result()->print_statistics.modes[0].time)); }
    }

    char weight[64];
    ::sprintf(weight, "  %.2f g", aprint_stats.total_weight);

    m_stext_time->SetLabel(time);
    m_stext_weight->SetLabel(weight);
}

bool SelectMachineDialog::Show(bool show)
{
    show_status(PrintDialogStatus::PrintStatusInit);

    // set default value when show this dialog
    if (show) {
        wxGetApp().UpdateDlgDarkUI(this);
        wxGetApp().reset_to_active();
        set_default();
        update_user_machine_list();
        //update_lan_machine_list();
    }

    if (show) {
        m_refresh_timer->Start(LIST_REFRESH_INTERVAL);
    } else {
        m_refresh_timer->Stop();
    }

    Layout();
    Fit();
    if (show) { CenterOnParent(); }
    return DPIDialog::Show(show);
}

SelectMachineDialog::~SelectMachineDialog()
{
    delete m_refresh_timer;
}

void SelectMachineDialog::update_lan_machine_list()
{
    DeviceManager* dev = wxGetApp().getDeviceManager();
    if (!dev) return;
   auto  m_free_machine_list = dev->get_local_machine_list();

    BOOST_LOG_TRIVIAL(trace) << "SelectMachinePopup update_other_devices start";

    for (auto& elem : m_free_machine_list) {
        MachineObject* mobj = elem.second;

        /* do not show printer bind state is empty */
        if (!mobj->is_avaliable()) continue;
        if (!mobj->is_online()) continue;
        if (!mobj->is_lan_mode_printer()) continue;
        /*if (mobj->is_in_printing()) {op->set_printer_state(PrinterState::BUSY);}*/

        if (mobj->has_access_right()) {
                auto b = mobj->dev_name;

                // clear machine list

                //m_comboBox_printer->Clear();
                std::vector<std::string>              machine_list;
                wxArrayString                         machine_list_name;
                std::map<std::string, MachineObject*> option_list;

                // same machine only appear once

               /* machine_list = sort_string(machine_list);
                for (auto tt = machine_list.begin(); tt != machine_list.end(); tt++) {
                    for (auto it = option_list.begin(); it != option_list.end(); it++) {
                        if (it->second->dev_name == *tt) {
                            m_list.push_back(it->second);
                            wxString dev_name_text = from_u8(it->second->dev_name);
                            if (it->second->is_lan_mode_printer()) {
                                dev_name_text += "(LAN)";
                            }
                            machine_list_name.Add(dev_name_text);
                            break;
                        }
                    }
                }

                m_comboBox_printer->Set(machine_list_name);

                MachineObject* obj = dev->get_selected_machine();
                if (obj) {
                    m_printer_last_select = obj->dev_id;
                }
                else {
                    m_printer_last_select = "";
                }*/
                //op->set_printer_state(PrinterState::LOCK);
            }

    }



    BOOST_LOG_TRIVIAL(trace) << "SelectMachineDialog update_lan_devices end";
}


EditDevNameDialog::EditDevNameDialog(Plater *plater /*= nullptr*/)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Modifying the device name"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top   = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(38));
    m_textCtr = new ::TextInput(this, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(260), FromDIP(40)), wxTE_PROCESS_ENTER);
    m_textCtr->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(22)));
    m_textCtr->SetMinSize(wxSize(FromDIP(260), FromDIP(40)));
    m_sizer_main->Add(m_textCtr, 0, wxALIGN_CENTER_HORIZONTAL | wxLEFT | wxRIGHT, FromDIP(40));

    m_static_valid = new wxStaticText(this, wxID_ANY, wxT(""), wxDefaultPosition, wxDefaultSize, 0);
    m_static_valid->Wrap(-1);
    m_static_valid->SetFont(::Label::Body_13);
    m_static_valid->SetForegroundColour(wxColour(255, 111, 0));
    m_sizer_main->Add(m_static_valid, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxLEFT | wxRIGHT, FromDIP(10));


    m_button_confirm = new Button(this, _L("Confirm"));
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    m_button_confirm->SetBackgroundColor(btn_bg_green);
    m_button_confirm->SetBorderColor(wxColour(0, 174, 66));
    m_button_confirm->SetTextColor(wxColour(255, 255, 255));
    m_button_confirm->SetSize(wxSize(FromDIP(72), FromDIP(24)));
    m_button_confirm->SetMinSize(wxSize(FromDIP(72), FromDIP(24)));
    m_button_confirm->SetCornerRadius(FromDIP(12));
    m_button_confirm->Bind(wxEVT_BUTTON, &EditDevNameDialog::on_edit_name, this);

    m_sizer_main->Add(m_button_confirm, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(10));
    m_sizer_main->Add(0, 0, 0, wxBOTTOM, FromDIP(38));

    SetSizer(m_sizer_main);
    Layout();
    Fit();
    Centre(wxBOTH);
    wxGetApp().UpdateDlgDarkUI(this);
}

EditDevNameDialog::~EditDevNameDialog() {}

void EditDevNameDialog::set_machine_obj(MachineObject *obj)
{
    m_info = obj;
    if (m_info)
        m_textCtr->GetTextCtrl()->SetValue(from_u8(m_info->dev_name));
}

void EditDevNameDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_button_confirm->SetSize(wxSize(FromDIP(72), FromDIP(24)));
    m_button_confirm->SetMinSize(wxSize(FromDIP(72), FromDIP(24)));
}

void EditDevNameDialog::on_edit_name(wxCommandEvent &e)
{
    m_static_valid->SetLabel(wxEmptyString);
    auto     m_valid_type = Valid;
    wxString info_line;
    auto     new_dev_name = m_textCtr->GetTextCtrl()->GetValue();

    const char *      unusable_symbols = "@~.<>[]:/\\|?*\"";
    const std::string unusable_suffix  = PresetCollection::get_suffix_modified();

    for (size_t i = 0; i < std::strlen(unusable_symbols); i++) {
        if (new_dev_name.find_first_of(unusable_symbols[i]) != std::string::npos) {
            info_line    = _L("Name is invalid;") + _L("illegal characters:") + " " + unusable_symbols;
            m_valid_type = NoValid;
            break;
        }
    }

    if (m_valid_type == Valid && new_dev_name.find(unusable_suffix) != std::string::npos) {
        info_line    = _L("Name is invalid;") + _L("illegal suffix:") + "\n\t" + from_u8(PresetCollection::get_suffix_modified());
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && new_dev_name.empty()) {
        info_line    = _L("The name is not allowed to be empty.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && new_dev_name.find_first_of(' ') == 0) {
        info_line    = _L("The name is not allowed to start with space character.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == Valid && new_dev_name.find_last_of(' ') == new_dev_name.length() - 1) {
        info_line    = _L("The name is not allowed to end with space character.");
        m_valid_type = NoValid;
    }

    if (m_valid_type == NoValid) {
        m_static_valid->SetLabel(info_line);
        Layout();
    }

    if (m_valid_type == Valid) {
        m_static_valid->SetLabel(wxEmptyString);
        DeviceManager *dev      = Slic3r::GUI::wxGetApp().getDeviceManager();
        if (dev) {
            auto           utf8_str = new_dev_name.ToUTF8();
            auto           name     = std::string(utf8_str.data(), utf8_str.length());
            if (m_info)
                dev->modify_device_name(m_info->dev_id, name);
        }
        DPIDialog::EndModal(wxID_CLOSE);
    }
}

 ThumbnailPanel::ThumbnailPanel(wxWindow *parent, wxWindowID winid, const wxPoint &pos, const wxSize &size)
     : wxPanel(parent, winid, pos, size)
 {
#ifdef __WINDOWS__
     SetDoubleBuffered(true);
#endif //__WINDOWS__

     SetBackgroundStyle(wxBG_STYLE_CUSTOM);
     wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);
     m_staticbitmap    = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize);
     sizer->Add(m_staticbitmap, 1, wxEXPAND|wxALL, 0);
     SetSizer(sizer);
     Layout();
     Fit();
 }

 void ThumbnailPanel::set_thumbnail(wxImage img)
 {
     wxBitmap bitmap(img);
     m_staticbitmap->SetBitmap(bitmap);
 }

 ThumbnailPanel::~ThumbnailPanel() {}

 }} // namespace Slic3r::GUI
