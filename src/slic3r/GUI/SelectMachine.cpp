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
wxDEFINE_EVENT(EVT_UNBIND_MACHINE, wxCommandEvent);

#define INITIAL_NUMBER_OF_MACHINES 0
#define LIST_REFRESH_INTERVAL 200
#define MACHINE_LIST_REFRESH_INTERVAL 2000

static wxString task_canceled_text = _L("Task canceled");

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

    SetBackgroundColour(*wxWHITE);

    m_unbind_img        = create_scaled_bitmap("unbind", nullptr, 18);
    m_edit_name_img     = create_scaled_bitmap("edit_button", nullptr, 18);
    m_select_unbind_img = create_scaled_bitmap("unbind_selected", nullptr, 18);

    m_printer_status_offline = create_scaled_bitmap("printer_status_offline", nullptr, 12);
    m_printer_status_busy    = create_scaled_bitmap("printer_status_busy", nullptr, 12);
    m_printer_status_idle    = create_scaled_bitmap("printer_status_idle", nullptr, 12);
    m_printer_status_lock    = create_scaled_bitmap("printer_status_lock", nullptr, 16);
    m_printer_in_lan         = create_scaled_bitmap("printer_in_lan", nullptr, 16);

    this->Bind(wxEVT_ENTER_WINDOW, &MachineObjectPanel::on_mouse_enter, this);
    this->Bind(wxEVT_LEAVE_WINDOW, &MachineObjectPanel::on_mouse_leave, this);
    this->Bind(wxEVT_LEFT_DOWN, &MachineObjectPanel::on_mouse_left_down, this);
    this->Bind(wxEVT_LEFT_UP, &MachineObjectPanel::on_mouse_left_up, this);
}

MachineObjectPanel::~MachineObjectPanel() {}

void MachineObjectPanel::show_bind_dialog()
{
    if (wxGetApp().is_user_login()) {
        BindMachineDilaog dlg;
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
    dc.DrawBitmap(dwbitmap, wxPoint(left, (size.y - dwbitmap.GetSize().y) / 2));

    left += dwbitmap.GetSize().x + 8;
    dc.SetFont(Label::Body_13);
    dc.SetBackgroundMode(wxTRANSPARENT);
    dc.SetTextForeground(SELECT_MACHINE_GREY900);
    wxString dev_name;
    if (m_info) {
        dev_name = from_u8(m_info->dev_name);
    }
    auto        sizet        = dc.GetTextExtent(dev_name);
    auto        text_end     = size.x - m_unbind_img.GetSize().x - 30;
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
                left = size.x - m_unbind_img.GetSize().x - 6;
                dc.DrawBitmap(m_select_unbind_img, left, (size.y - m_unbind_img.GetSize().y) / 2); } 
        }

        if (m_show_edit) {
            left = size.x - m_unbind_img.GetSize().x - 6 - m_edit_name_img.GetSize().x - 6;
            dc.DrawBitmap(m_edit_name_img, left, (size.y - m_edit_name_img.GetSize().y) / 2);
        }
    }
}

void MachineObjectPanel::update_machine_info(/*std::string dev_id, wxString dev_name, int progress, wxString owner*/ MachineObject *info)
{
    m_info = info;
    // m_info->can_abort() ? set_printer_busy() : set_printer_idle();
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

void MachineObjectPanel::on_mouse_left_down(wxMouseEvent &evt)
{
    auto left   = GetSize().x - m_unbind_img.GetSize().x - 6;
    auto right  = left + m_unbind_img.GetSize().x;
    auto top    = (GetSize().y - m_unbind_img.GetSize().y) / 2;
    auto bottom = (GetSize().y - m_unbind_img.GetSize().y) / 2 + m_unbind_img.GetSize().y;

    if ((evt.GetPosition().x >= left && evt.GetPosition().x <= right) && evt.GetPosition().y >= top && evt.GetPosition().y <= bottom) {
    }

    Refresh();
    evt.Skip();
}

void MachineObjectPanel::on_mouse_left_up(wxMouseEvent &evt)
{
    if (m_show_edit) {
        auto edit_left   = GetSize().x - m_unbind_img.GetSize().x - 6 - m_edit_name_img.GetSize().x - 6;
        auto edit_right  = edit_left + m_edit_name_img.GetSize().x;
        auto edit_top    = (GetSize().y - m_edit_name_img.GetSize().y) / 2;
        auto edit_bottom = (GetSize().y - m_edit_name_img.GetSize().y) / 2 + m_edit_name_img.GetSize().y;
        if ((evt.GetPosition().x >= edit_left && evt.GetPosition().x <= edit_right) && evt.GetPosition().y >= edit_top && evt.GetPosition().y <= edit_bottom) {
            EditDevNameDialog dlg;
            dlg.set_machine_obj(m_info);
            dlg.ShowModal();
            return;
        }
    }

    if (m_show_bind) {
        auto left   = GetSize().x - m_unbind_img.GetSize().x - 6;
        auto right  = left + m_unbind_img.GetSize().x;
        auto top    = (GetSize().y - m_unbind_img.GetSize().y) / 2;
        auto bottom = (GetSize().y - m_unbind_img.GetSize().y) / 2 + m_unbind_img.GetSize().y;

        if ((evt.GetPosition().x >= left && evt.GetPosition().x <= right) && evt.GetPosition().y >= top && evt.GetPosition().y <= bottom) {
            wxCommandEvent event(EVT_UNBIND_MACHINE, GetId());
            event.SetEventObject(this);
            GetEventHandler()->ProcessEvent(event);
        } else {
            wxGetApp().mainframe->jump_to_monitor(m_info->dev_id);
            wxGetApp().mainframe->SetFocus();
        }
        return;
    }
}

wxIMPLEMENT_CLASS(SelectMachinePopup, wxPopupTransientWindow);

wxBEGIN_EVENT_TABLE(SelectMachinePopup, wxPopupTransientWindow) EVT_MOUSE_EVENTS(SelectMachinePopup::OnMouse) EVT_SIZE(SelectMachinePopup::OnSize)
    EVT_SET_FOCUS(SelectMachinePopup::OnSetFocus) EVT_KILL_FOCUS(SelectMachinePopup::OnKillFocus) wxEND_EVENT_TABLE()

        SelectMachinePopup::SelectMachinePopup(wxWindow *parent)
    : wxPopupTransientWindow(parent, wxBORDER_NONE | wxPU_CONTAINS_CONTROLS), m_dismiss(false)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    Freeze();
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    SetBackgroundColour(SELECT_MACHINE_GREY400);

    m_panel_body = new wxPanel(this, wxID_ANY, wxDefaultPosition, SELECT_MACHINE_POPUP_SIZE, wxTAB_TRAVERSAL);
    m_panel_body->SetBackgroundColour(*wxWHITE);

    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);

    m_scrolledWindow = new wxScrolledWindow(m_panel_body, wxID_ANY, wxDefaultPosition, SELECT_MACHINE_LIST_SIZE, wxHSCROLL | wxVSCROLL);
    m_scrolledWindow->SetScrollRate(0, 5);
    auto m_sizxer_scrolledWindow = new wxBoxSizer(wxVERTICAL);
    m_scrolledWindow->SetSizer(m_sizxer_scrolledWindow);
    m_scrolledWindow->Layout();
    m_sizxer_scrolledWindow->Fit(m_scrolledWindow);

    auto own_title        = create_title_panel(_L("My Device"));
    m_sizer_my_devices    = new wxBoxSizer(wxVERTICAL);
    auto other_title      = create_title_panel(_L("Other Device"));
    m_sizer_other_devices = new wxBoxSizer(wxVERTICAL);

    m_sizxer_scrolledWindow->Add(own_title, 0, wxEXPAND, 0);
    m_sizxer_scrolledWindow->Add(m_sizer_my_devices, 0, wxEXPAND, 0);
    m_sizxer_scrolledWindow->Add(other_title, 0, wxEXPAND, 0);
    m_sizxer_scrolledWindow->Add(m_sizer_other_devices, 0, wxEXPAND, 0);

    m_sizer_body->Add(m_scrolledWindow, 1, wxEXPAND | wxALIGN_CENTER_HORIZONTAL | wxALL, 5);
    m_panel_body->SetSizer(m_sizer_body);
    m_panel_body->Layout();
    m_sizer_main->Add(m_panel_body, 0, wxALL | wxEXPAND, 1);

    this->SetSizer(m_sizer_main);
    this->Layout();
    m_sizer_main->Fit(this);
    Thaw();

    //#ifdef __WXMAC__
    //    // On Mac, pop up window capture mouse events
    //    m_scrolledWindow->GetPanel()->Bind(wxEVT_LEFT_UP, &SelectMachinePopup::OnLeftUp, this);
    //#endif

    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
    Bind(EVT_UPDATE_USER_MACHINE_LIST, &SelectMachinePopup::update_machine_list, this);
    Bind(wxEVT_TIMER, &SelectMachinePopup::on_timer, this);
}

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

bool SelectMachinePopup::Show(bool show) { return wxPopupTransientWindow::Show(show); }

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
        if (i < m_list_Machine_panel.size()) {
            op = m_list_Machine_panel[i]->mPanel;
        } else {
            op = new MachineObjectPanel(m_scrolledWindow, wxID_ANY);
            MachinePanel* mpanel = new MachinePanel();
            mpanel->mIndex = wxString::Format("%d", i);
            mpanel->mPanel = op;
            m_list_Machine_panel.push_back(mpanel);
            m_sizer_other_devices->Add(op, 0, wxEXPAND, 0);
        }
        i++;
        
        op->update_machine_info(mobj);

        op->Bind(wxEVT_LEFT_DOWN, [this, op, mobj](auto &e){
                int dlg_result = wxID_CANCEL;
                if (mobj->is_lan_mode_printer()) {
                    if (!mobj->has_access_right()) {
                        ConnectPrinterDialog dlg(this, wxID_ANY, _L("Input access code"));
                        dlg.set_machine_object(mobj);
                        dlg_result = dlg.ShowModal();
                    } else {
                        ;
                    }
                } else {
                    if (wxGetApp().is_user_login()) {
                        BindMachineDilaog dlg;
                        dlg.update_machine_info(mobj);
                        dlg_result = dlg.ShowModal();
                    }
                }
                if (dlg_result == wxID_OK) {
                    wxGetApp().mainframe->jump_to_monitor(mobj->dev_id);
                }
            }
        );

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
    }

    for (int j = i; j < m_list_Machine_panel.size(); j++) {
        m_list_Machine_panel[j]->mPanel->Hide();
    }
    m_sizer_other_devices->Layout();
    m_scrolledWindow->Layout();
    m_scrolledWindow->Fit();
    m_scrolledWindow->Thaw();
    Layout();
    Fit();
    this->Thaw();
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

    BOOST_LOG_TRIVIAL(trace) << "SelectMachinePopup update_machine_list start";
    this->Freeze();
    m_scrolledWindow->Freeze();
    int i = 0;
    for (auto& elem : m_bind_machine_list) {
        MachineObject* mobj = elem.second;
        MachineObjectPanel* op = nullptr;
        if (i < m_user_list_machine_panel.size()) {
            op = m_user_list_machine_panel[i]->mPanel;
            op->Show();
        }
        else {
            op = new MachineObjectPanel(m_scrolledWindow, wxID_ANY);
            MachinePanel* mpanel = new MachinePanel();
            mpanel->mIndex = wxString::Format("%d", i);
            mpanel->mPanel = op;
            m_user_list_machine_panel.push_back(mpanel);
            m_sizer_my_devices->Add(op, 0, wxEXPAND, 0);
        }
        i++;
        op->update_machine_info(mobj);
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
            op->Bind(EVT_UNBIND_MACHINE, [this, mobj](wxCommandEvent& e) {
                mobj->set_access_code("");
                });
        }
        else {
            op->show_printer_bind(true, PrinterBindState::ALLOW_UNBIND);
            op->Bind(EVT_UNBIND_MACHINE, [this, mobj, dev](wxCommandEvent& e) {
                // show_unbind_dialog
                UnBindMachineDilaog dlg;
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

        op->Bind(wxEVT_LEFT_DOWN, [this, op, mobj](auto& e) {
            if (mobj->is_lan_mode_printer()) {
                if (mobj->has_access_right() && mobj->is_avaliable()) {
                    //Connect printer
                    mobj->connect();
                }
                else {
                    ConnectPrinterDialog dlg(this, wxID_ANY, _L("Input access code"));
                    dlg.set_machine_object(mobj);
                    dlg.ShowModal();
                }
            }
            //wxGetApp().mainframe->jump_to_monitor(mobj->dev_id);
            }
        );
    }

    for (int j = i; j < m_user_list_machine_panel.size(); j++) {
        m_user_list_machine_panel[j]->mPanel->Hide();
    }
    //m_sizer_my_devices->Layout();
    m_scrolledWindow->Layout();
    m_scrolledWindow->Fit();
    m_scrolledWindow->Thaw();
    Layout();
    Fit();
    this->Thaw();
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

void SelectMachinePopup::OnSize(wxSizeEvent &event) { event.Skip(); }

void SelectMachinePopup::OnSetFocus(wxFocusEvent &event) { event.Skip(); }

void SelectMachinePopup::OnKillFocus(wxFocusEvent &event) { event.Skip(); }

void SelectMachinePopup::OnMouse(wxMouseEvent &event) { event.Skip(); }

void SelectMachinePopup::OnLeftUp(wxMouseEvent &event)
{
    this->GetParent()->SetFocus();
    event.Skip();
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

void SelectMachineDialog::update_info_msg(wxString msg)
{
    if (msg.empty()) {
        if (!m_text_load_ams_data->GetLabel().empty()) {
            m_text_load_ams_data->SetLabel(wxEmptyString);
            m_text_load_ams_data->Hide();
            Layout();
            Fit();
        }
    }
    else {
        auto str_new = msg.ToStdString();
        stripWhiteSpace(str_new);

        auto str_old = m_text_load_ams_data->GetLabel().ToStdString();
        stripWhiteSpace(str_old);

        if (str_new != str_old) {
            if (m_text_load_ams_data->GetLabel() != msg) {
                m_text_load_ams_data->SetLabel(msg);
                m_text_load_ams_data->SetMaxSize(wxSize(FromDIP(400), -1));
                m_text_load_ams_data->SetMinSize(wxSize(FromDIP(400), -1));
                m_text_load_ams_data->Wrap(FromDIP(400));
                m_text_load_ams_data->Show();
                Layout();
                Fit();
            }
        }
    }
}

void SelectMachineDialog::update_warn_msg(wxString msg)
{
    if (msg.empty()) {
        if (!m_error_load_ams_data->GetLabel().empty()) {
            m_error_load_ams_data->SetLabel(wxEmptyString);
            m_error_load_ams_data->Hide();
            Layout();
            Fit();
        }
    }
    else {
        auto str_new = msg.ToStdString();
        stripWhiteSpace(str_new);

        auto str_old = m_error_load_ams_data->GetLabel().ToStdString();
        stripWhiteSpace(str_old);

        if (str_new != str_old) {
            if (m_error_load_ams_data->GetLabel() != msg) {
                m_error_load_ams_data->SetLabel(msg);
                m_error_load_ams_data->SetMinSize(wxSize(FromDIP(400), -1));
                m_error_load_ams_data->SetMaxSize(wxSize(FromDIP(400), -1));
                m_error_load_ams_data->Wrap(FromDIP(400));
                m_error_load_ams_data->Show();
                Layout();
                Fit();
            }
        }
    }
}

SelectMachineDialog::SelectMachineDialog(Plater *plater)
    : DPIDialog(static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Send print job to"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
    , m_plater(plater)
    , m_export_3mf_cancel(false)
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

    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);

    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(22));

    m_image = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_image->SetBackgroundColour(m_colour_def_color);

    sizer_thumbnail = new wxBoxSizer(wxVERTICAL);
    m_staticbitmap = new wxStaticBitmap(m_image, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize);
    sizer_thumbnail->Add(m_staticbitmap, 0, wxEXPAND, 0);
    m_image->SetSizer(sizer_thumbnail);
    m_image->Layout();

    m_sizer_main->Add(m_image, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT);

    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(10));

    wxBoxSizer *m_sizer_basic        = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_basic_weight = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_basic_time   = new wxBoxSizer(wxHORIZONTAL);

    auto timeimg = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("print-time", this, 18), wxDefaultPosition, wxSize(FromDIP(18), FromDIP(18)), 0);
    m_sizer_basic_weight->Add(timeimg, 1, wxEXPAND | wxALL, FromDIP(5));
    m_stext_time = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_RIGHT);
    m_sizer_basic_weight->Add(m_stext_time, 0, wxALL, FromDIP(5));
    m_sizer_basic->Add(m_sizer_basic_weight, 0, wxALIGN_CENTER, 0);
    m_sizer_basic->Add(0, 0, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));

    auto weightimg = new wxStaticBitmap(this, wxID_ANY, create_scaled_bitmap("print-weight", this, 18), wxDefaultPosition, wxSize(FromDIP(18), FromDIP(18)), 0);
    m_sizer_basic_time->Add(weightimg, 1, wxEXPAND | wxALL, FromDIP(5));
    m_stext_weight = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    m_sizer_basic_time->Add(m_stext_weight, 0, wxALL, FromDIP(5));
    m_sizer_basic->Add(m_sizer_basic_time, 0, wxALIGN_CENTER, 0);
    m_sizer_main->Add(m_sizer_basic, 0, wxALIGN_CENTER, 0);

    m_sizer_material = new wxGridSizer(0, 6, 0, 0);
    m_sizer_main->Add(m_sizer_material, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(80));

    m_text_load_ams_data = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
    m_text_load_ams_data->SetFont(::Label::Body_13);
    m_text_load_ams_data->SetForegroundColour(wxColour(0x6B, 0x6B, 0x6B));

    m_error_load_ams_data = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
    m_error_load_ams_data->SetFont(::Label::Body_13);
    m_error_load_ams_data->SetForegroundColour(wxColour(0xFF, 0x6F, 0x00));

   
    m_text_load_ams_data->Hide();
    m_error_load_ams_data->Hide();

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(8));
    m_sizer_main->Add(m_text_load_ams_data, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_main->Add(m_error_load_ams_data, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(8));

    m__line_materia = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m__line_materia->SetForegroundColour(wxColour(238, 238, 238));
    m__line_materia->SetBackgroundColour(wxColour(238, 238, 238));

    m_sizer_main->Add(m__line_materia, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));

    wxBoxSizer *m_sizer_printer = new wxBoxSizer(wxHORIZONTAL);

    m_stext_printer_title = new wxStaticText(this, wxID_ANY, L("Printer"), wxDefaultPosition, wxSize(-1, -1), 0);
    m_stext_printer_title->SetFont(::Label::Head_14);
    m_stext_printer_title->Wrap(-1);
    m_stext_printer_title->SetForegroundColour(m_colour_bold_color);
    m_stext_printer_title->SetBackgroundColour(m_colour_def_color);

    m_sizer_printer->Add(m_stext_printer_title, 0, wxALL | wxLEFT, FromDIP(5));
    m_sizer_printer->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(12));

    m_comboBox_printer = new ::ComboBox(this, wxID_ANY, L(""), wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY);
    m_comboBox_printer->Bind(wxEVT_COMBOBOX, &SelectMachineDialog::on_selection_changed, this);

    m_sizer_printer->Add(m_comboBox_printer, 1, wxEXPAND | wxRIGHT, FromDIP(5));
    btn_bg_enable = StateColor(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    m_button_refresh = new Button(this, _L("Refresh"));
    m_button_refresh->SetBackgroundColor(btn_bg_enable);
    m_button_refresh->SetBorderColor(btn_bg_enable);
    m_button_refresh->SetTextColor(*wxWHITE);
    m_button_refresh->SetSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_refresh->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_refresh->SetCornerRadius(FromDIP(12));
    m_button_refresh->Bind(wxEVT_BUTTON, &SelectMachineDialog::on_refresh, this);
    m_sizer_printer->Add(m_button_refresh, 0, wxALL | wxLEFT, FromDIP(5));

    m_sizer_main->Add(m_sizer_printer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));

    m_panel_warn             = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    wxBoxSizer *m_sizer_warn = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_warn->Add(0, 0, 1, wxEXPAND, FromDIP(5));

    auto warimg = new wxStaticBitmap(m_panel_warn, wxID_ANY, create_scaled_bitmap("obj_warning", m_panel_warn, 15), wxDefaultPosition, wxSize(FromDIP(15), FromDIP(15)), 0);
    m_sizer_warn->Add(warimg, 0, wxEXPAND, 0);

    m_statictext_warn = new wxStaticText(m_panel_warn, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_statictext_warn->Wrap(-1);
    m_statictext_warn->SetFont(::Label::Body_13);
    m_statictext_warn->SetForegroundColour(wxColour(255, 111, 0));

    m_sizer_warn->Add(m_statictext_warn, 0, wxALL | wxEXPAND, FromDIP(5));

    m_sizer_warn->Add(0, 0, 1, wxEXPAND, FromDIP(5));

    m_panel_warn->SetSizer(m_sizer_warn);
    m_panel_warn->Layout();
    m_sizer_warn->Fit(m_panel_warn);
    m_sizer_main->Add(m_panel_warn, 0, wxEXPAND | wxTOP, FromDIP(2));

    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));

    m_line_bed = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line_bed->SetForegroundColour(wxColour(238, 238, 238));
    m_line_bed->SetBackgroundColour(wxColour(238, 238, 238));

    m_sizer_main->Add(m_line_bed, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));

    m_sizer_main->Add(0, 1, 0, wxTOP, FromDIP(14));

    // BBS hide bed choice
    // wxBoxSizer *m_sizer_bed = new wxBoxSizer(wxHORIZONTAL);
    // m_staticText_bed_title = new wxStaticText(this, wxID_ANY, L("Bed style"), wxDefaultPosition, wxSize(-1, -1), 0);
    // m_staticText_bed_title->SetFont(::Label::Head_14);
    // m_staticText_bed_title->Wrap(-1);
    // m_staticText_bed_title->SetForegroundColour(m_colour_bold_color);
    // m_staticText_bed_title->SetBackgroundColour(m_colour_def_color);
    // m_sizer_bed->Add(m_staticText_bed_title, 0, wxALL | wxEXPAND, 5);
    // m_sizer_bed->Add(0, 0, 0, wxEXPAND | wxLEFT, 12);
    // m_comboBox_bed = new ::ComboBox(this, wxID_ANY, L(""), wxDefaultPosition, wxDefaultSize, 0, nullptr, wxCB_READONLY);
    // for (auto i = 0; i < m_bedtype_list.size(); i++) { m_comboBox_bed->Append(m_bedtype_list[i]); }
    // m_sizer_bed->Add(m_comboBox_bed, 1, wxEXPAND | wxRIGHT, 30);
    // m_sizer_main->Add(m_sizer_bed, 0, wxEXPAND | wxLEFT | wxRIGHT, 30);

    m_sizer_select = new wxGridSizer(2, 2, 0, 0);

    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));
    select_bed           = create_item_checkbox(_L("Bed Leveling"), this, _L("Bed Leveling"), "bed_leveling");
    select_flow          = create_item_checkbox(_L("Flow Calibration"), this, _L("Flow Calibration"), "flow_cali");

    //select_vibration = create_item_checkbox(_L("Vibration Calibration"), this, _L("Vibration Calibration"), "vibration_cali");
    //select_layer_inspect = create_item_checkbox(_L("First Layer Inspection"), this, _L("First Layer Inspection"), "layer_inspect");

    select_bed->Show(false);
    select_flow->Show(false);
    //select_vibration->Show(false);
    //select_layer_inspect->Show(false);
    // select_record->Show(false);

    m_sizer_select->Add(select_bed);
    m_sizer_select->Add(select_flow);
    //m_sizer_select->Add(select_vibration);
    //m_sizer_select->Add(select_layer_inspect);
    // m_sizer_select->Add(select_record);
    m_sizer_main->Add(m_sizer_select, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(40));
    m_sizer_main->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(11));

    // error msg
    m_panel_err             = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    wxBoxSizer *m_sizer_err = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_err->Add(0, 0, 0, wxEXPAND, FromDIP(5));
    m_panel_err->Hide();

    auto errimg = new wxStaticBitmap(m_panel_err, wxID_ANY, create_scaled_bitmap("obj_warning", m_panel_warn, 30), wxDefaultPosition,
                                     wxSize(wxGetApp().em_unit() * 3, wxGetApp().em_unit() * 3), 0);
    m_sizer_err->Add(errimg, 0, wxEXPAND, 0);

    m_statictext_err = new wxStaticText(m_panel_err, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_statictext_err->Wrap(-1);
    m_statictext_err->SetForegroundColour(wxColour(255, 111, 0));

    m_sizer_err->Add(m_statictext_err, 0, wxALL | wxEXPAND, FromDIP(5));
    m_sizer_err->Add(0, 0, 1, wxEXPAND, FromDIP(5));

    m_panel_err->SetSizer(m_sizer_err);
    m_panel_err->Layout();
    m_sizer_err->Fit(m_panel_err);
    m_sizer_main->Add(m_panel_err, 0, wxEXPAND | wxLEFT, FromDIP(40));

    // bottom  area
    /*  m_panel_bottom = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, wxGetApp().em_unit() * 7), wxTAB_TRAVERSAL);
      m_panel_bottom->SetMinSize(wxSize(-1, wxGetApp().em_unit() * 7));
      m_panel_bottom->SetMaxSize(wxSize(-1, wxGetApp().em_unit() * 7));
      m_panel_bottom->SetBackgroundColour(m_colour_def_color);*/

    // line schedule
    m_line_schedule = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    m_line_schedule->SetBackgroundColour(wxColour(238, 238, 238));
    m_sizer_main->Add(m_line_schedule, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(25));

    m_sizer_bottom = new wxBoxSizer(wxVERTICAL);
    m_simplebook   = new wxSimplebook(this, wxID_ANY, wxDefaultPosition, SELECT_MACHINE_DIALOG_SIMBOOK_SIZE, 0);
    m_sizer_main->Add(m_simplebook, 0, wxALIGN_CENTER_HORIZONTAL, 0);

    // perpare mode
    m_panel_prepare = new wxPanel(m_simplebook, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    m_panel_prepare->SetBackgroundColour(m_colour_def_color);
    // m_panel_prepare->SetBackgroundColour(wxColour(135,206,250));
    wxBoxSizer *m_sizer_prepare = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_pcont   = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_prepare->Add(0, 0, 1, wxTOP, FromDIP(22));
    m_sizer_pcont->Add(0, 0, 1, wxEXPAND, 0);
    m_button_ensure = new Button(m_panel_prepare, _L("Send"));
    m_button_ensure->SetBackgroundColor(btn_bg_enable);
    m_button_ensure->SetBorderColor(btn_bg_enable);
    m_button_ensure->SetTextColor(*wxWHITE);
    m_button_ensure->SetSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_ensure->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
    m_button_ensure->SetCornerRadius(FromDIP(12));

    m_button_ensure->Bind(wxEVT_BUTTON, &SelectMachineDialog::on_ok, this);
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
    auto completedimg = new wxStaticBitmap(m_panel_finish, wxID_ANY, create_scaled_bitmap("completed", m_panel_warn, 25), wxDefaultPosition, wxSize(imgsize, imgsize), 0);
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

    m_sizer_main->Add(m_sizer_bottom, 0, wxALIGN_CENTER_HORIZONTAL);
    m_sizer_main->Add(0, 0, 0, wxEXPAND|wxTOP, FromDIP(15));

    // bind
    Bind(EVT_UPDATE_USER_MACHINE_LIST, &SelectMachineDialog::update_printer_combobox, this);
    Bind(EVT_PRINT_JOB_CANCEL, &SelectMachineDialog::on_print_job_cancel, this);
    Bind(EVT_SET_FINISH_MAPPING, &SelectMachineDialog::on_set_finish_mapping, this);

    SetSizer(m_sizer_main);
    Layout();
    Fit();
    Thaw();

    init_bind();
    init_timer();
     //CenterOnParent();
    Centre(wxBOTH);
}

wxWindow *SelectMachineDialog::create_item_checkbox(wxString title, wxWindow *parent, wxString tooltip, std::string param)
{
    auto checkbox = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    checkbox->SetBackgroundColour(m_colour_def_color);

    wxBoxSizer *sizer_checkbox = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *sizer_check    = new wxBoxSizer(wxVERTICAL);

    auto check = new ::CheckBox(checkbox);

    sizer_check->Add(check, 0, wxBOTTOM | wxEXPAND | wxTOP, FromDIP(5));

    sizer_checkbox->Add(sizer_check, 0, wxEXPAND, FromDIP(5));
    sizer_checkbox->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(11));

    auto text = new wxStaticText(checkbox, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    text->SetFont(::Label::Body_13);
    text->SetForegroundColour(wxColour(107, 107, 107));
    text->Wrap(-1);
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

void SelectMachineDialog::update_select_layout(PRINTER_TYPE type)
{
    if (type == PRINTER_TYPE::PRINTER_3DPrinter_UKNOWN) {
        select_bed->Show();
        select_flow->Show();
    } else if (type == PRINTER_TYPE::PRINTER_3DPrinter_X1) {
        select_bed->Show();
        select_flow->Show();
    } else if (type == PRINTER_TYPE::PRINTER_3DPrinter_X1_Carbon) {
        select_bed->Show();
        select_flow->Show();
    } else if (type == PRINTER_TYPE::PRINTER_3DPrinter_P1) {
        select_bed->Show();
        select_flow->Show(false);
    } else if (type == PRINTER_TYPE::PRINTER_3DPrinter_NONE) {
        select_bed->Hide();
        select_flow->Hide();
    } else {
        select_bed->Show();
        select_flow->Show();
    }

    Fit();
}

void SelectMachineDialog::prepare_mode()
{
    m_panel_warn->Hide();
    m_simplebook->SetSelection(0);
    Layout();
    Fit();
}

void SelectMachineDialog::sending_mode()
{
    m_panel_warn->Hide();
    m_simplebook->SetSelection(1);
    Layout();
    Fit();
}

void SelectMachineDialog::finish_mode()
{
    m_panel_warn->Hide();
    m_simplebook->SetSelection(2);
    Layout();
    Fit();
}

bool SelectMachineDialog::do_ams_mapping(MachineObject *obj_)
{
    if (!obj_) return false;

    // try color and type mapping
    int result = obj_->ams_filament_mapping(m_filaments, m_ams_mapping_result);
    if (result == 0) {
        for (auto f = m_ams_mapping_result.begin(); f != m_ams_mapping_result.end(); f++) {
            BOOST_LOG_TRIVIAL(trace) << "ams_mapping f id = " << f->id << ", tray_id = " << f->tray_id << ", color = " << f->color << ", type = " << f->type;

            MaterialHash::iterator iter = m_materialList.begin();
            while (iter != m_materialList.end()) {
                int           id = iter->second->id;
                Material* item = iter->second;
                MaterialItem* m = item->item;

                if (f->id == id) {
                    wxString ams_id;
                    wxColour ams_col;

                    if (f->tray_id >= 0) {
                        ams_id = wxString::Format("%02d", f->tray_id + 1);
                    } else {
                        ams_id = "-";
                    }

                    if (!f->color.empty()) {
                        ams_col = AmsTray::decode_color(f->color);
                    } else {
                        // default color
                        ams_col = wxColour(0x6B, 0x6B, 0x6B);
                    }

                    m->set_ams_info(ams_col, ams_id);
                    break;
                }
                iter++;
            }
        }
    }

    return obj_->is_valid_mapping_result(m_ams_mapping_result);
}

bool SelectMachineDialog::get_ams_mapping_result(std::string &mapping_array_str)
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
        if (!valid_mapping_result) {
            return false;
        } else {
            json          j = json::array();
            for (int i = 0; i < wxGetApp().preset_bundle->filament_presets.size(); i++) {
                int tray_id = -1;
                for (int k = 0; k < m_ams_mapping_result.size(); k++) {
                    if (m_ams_mapping_result[k].id == i) {
                        tray_id = m_ams_mapping_result[k].tray_id;
                    }
                }
                j.push_back(tray_id);
            }
            mapping_array_str = j.dump();
            return true;
        }
    }
    return true;
}

void SelectMachineDialog::prepare(int print_plate_idx)
{
    m_print_plate_idx = print_plate_idx;
}

void SelectMachineDialog::update_print_status_msg(wxString msg, bool is_warning)
{
    //set style
    if (is_warning) {
        update_warn_msg(msg);
        update_info_msg(wxEmptyString);
    } else {
        update_warn_msg(wxEmptyString);
        update_info_msg(msg);
    }
}

void SelectMachineDialog::show_status(PrintDialogStatus status)
{
    BOOST_LOG_TRIVIAL(trace) << "select_machine_dialog: show_status = " << status;
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
        update_print_status_msg(wxEmptyString, false);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusNoUserLogin) {
        wxString msg_text = _L("No login account, only printers in LAN mode are displayed");
        update_print_status_msg(msg_text, false);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    }else if (status == PrintDialogStatus::PrintStatusInvalidPrinter) {
        update_print_status_msg(wxEmptyString, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusConnectingServer) {
        wxString msg_text = _L("Connecting to server");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusReading) {
        wxString msg_text = _L("Synchronizing device information");
        update_print_status_msg(msg_text, false);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusReadingFinished) {
        update_print_status_msg(wxEmptyString, false);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusReadingTimeout) {
        wxString msg_text = _L("Synchronizing device information time out");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusInUpgrading) {
        wxString msg_text = _L("Cannot send the print task when the upgrade is in progress");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusInSystemPrinting) {
        wxString msg_text = _L("The printer is executing instructions. Please restart printing after it ends");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusInPrinting) {
        wxString msg_text = _L("The printer is busy on other print job");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusNeedUpgradingAms) {
        wxString msg_text = _L("The firmware versions of printer and AMS are too low.Please update to the latest version before sending the print job");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusAmsMappingSuccess){
        wxString msg_text = _L("Filaments to AMS slots mappings have been established. You can click a filament above to change its mapping AMS slot");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusAmsMappingInvalid) {
        wxString msg_text = _L("Please click each filament above to specify its mapping AMS slot before sending the print job");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(false);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusAmsMappingValid) {
        update_print_status_msg(wxEmptyString, false);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusRefreshingMachineList) {
        update_print_status_msg(wxEmptyString, false);
        Enable_Send_Button(false);
        Enable_Refresh_Button(false);
    } else if (status == PrintDialogStatus::PrintStatusSending) {
        Enable_Send_Button(false);
        Enable_Refresh_Button(false);
    } else if (status == PrintDialogStatus::PrintStatusSendingCanceled) {
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusNoSdcard) {
        wxString msg_text = _L("An SD card needs to be inserted before printing via LAN.");
        update_print_status_msg(msg_text, true);
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    } else if (status == PrintDialogStatus::PrintStatusAmsMappingByOrder) {
        Enable_Send_Button(true);
        Enable_Refresh_Button(true);
    }
}

void SelectMachineDialog::update_err_msg(wxString msg)
{
    /*
    if (msg.empty()) {
        m_statictext_err->SetLabel(wxEmptyString);
        m_panel_err->Hide();
    } else {
        m_statictext_err->SetLabel(msg);
        m_panel_err->Show();
    }
    Layout();
    Fit();
    */
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

void SelectMachineDialog::on_ok(wxCommandEvent &event)
{
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

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "for send task, current printer id =  " << m_printer_last_select << std::endl;
    show_status(PrintDialogStatus::PrintStatusSending);

    m_status_bar->reset();
    m_status_bar->set_prog_block();

    // get ams_mapping_result
    std::string ams_mapping_array;
    get_ams_mapping_result(ams_mapping_array);

    result = m_plater->send_gcode(m_print_plate_idx, [this](int export_stage, int current, int total, bool &cancel) {
        bool     cancelled = false;
        wxString msg       = _L("Preparing print job");
        m_status_bar->update_status(msg, cancelled, 10, true);
        m_export_3mf_cancel = cancel = cancelled;
    });

    if (result < 0) {
        wxString msg = _L("Abnormal print file data. Please slice again");
        m_status_bar->set_status_text(msg);
        return;
    }

    if (m_export_3mf_cancel) {
        m_status_bar->set_status_text(task_canceled_text);
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

    m_print_job                = std::make_shared<PrintJob>(m_status_bar, m_plater, m_printer_last_select);
    m_print_job->m_dev_ip      = obj_->dev_ip;
    m_print_job->m_access_code = obj_->access_code;
    m_print_job->connection_type = obj_->connection_type();
    if (obj_->is_support_ams_mapping())
        m_print_job->task_ams_mapping = ams_mapping_array;
    else
        m_print_job->task_ams_mapping = "";
    
    if (obj_->has_sdcard()) {
        m_print_job->has_sdcard = obj_->has_sdcard();
    }
    
    if (obj_->is_only_support_cloud_print()) {
        m_print_job->cloud_print_only = true;
    }

    m_print_job->set_print_config(
        MachineBedTypeString[0],
        m_checkbox_list["bed_leveling"]->GetValue(),
        m_checkbox_list["flow_cali"]->GetValue(),
        false,
        false,
        true);

    m_print_job->on_success([this]() { finish_mode(); });

    m_status_bar->set_cancel_callback_fina([this]() {
        m_print_job->cancel();
        wxCommandEvent *event = new wxCommandEvent(EVT_PRINT_JOB_CANCEL);
        wxQueueEvent(this, event);
    });

    wxCommandEvent evt(m_plater->get_print_finished_event());
    m_print_job->start();
}

void SelectMachineDialog::on_refresh(wxCommandEvent &event)
{
    show_status(PrintDialogStatus::PrintStatusRefreshingMachineList);

    if (wxGetApp().is_user_login()) {
        if (this == NULL || this == nullptr) { return; }
        boost::thread get_print_info_thread = Slic3r::create_thread([this] {
            DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
            if (!dev) return;
            dev->update_user_machine_list_info();

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

void SelectMachineDialog::on_set_finish_mapping(wxCommandEvent &evt)
{
    for (auto i = 0; i < m_ams_mapping_result.size(); i++) {
        if (m_ams_mapping_result[i].id == m_current_filament_id) {
            m_ams_mapping_result[i].tray_id = evt.GetInt();
        }
    }

    auto colours = evt.GetString();
    auto colours_arr = wxSplit(colours.ToStdString(), '|');
    auto c = colours_arr[3];

    if (colours_arr.size() == 4) {
        MaterialHash::iterator iter = m_materialList.begin();
        while (iter != m_materialList.end()) {
            Material*        item = iter->second;
            MaterialItem *m  = item->item;
            if (item->id == m_current_filament_id) {
                auto ams_colour = wxColour(wxAtoi(colours_arr[0]),wxAtoi(colours_arr[1]),wxAtoi(colours_arr[2]));
                m->set_ams_info(ams_colour, colours_arr[3]);
            }
            iter++;
        }
    }
}

void SelectMachineDialog::on_print_job_cancel(wxCommandEvent &evt)
{
    if (m_print_job->is_running()) { m_print_job->join(5 * 1000); }
    show_status(PrintDialogStatus::PrintStatusSendingCanceled);
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

void SelectMachineDialog::update_printer_combobox(wxCommandEvent &event)
{
    Slic3r::DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    // clear machine list
    m_list.clear();
    m_comboBox_printer->Clear();

    std::vector<std::string>               machine_list;
    std::map<std::string, MachineObject *> option_list;

    option_list = dev->get_my_machine_list();

    // local lan machine
    /*std::map<std::string, MachineObject*> local_machine_list = dev->get_local_machine_list();
    for (auto it = local_machine_list.begin(); it != local_machine_list.end(); it++) {
        if (it->second->is_lan_mode_printer()) {
            if (option_list.find(it->first) == option_list.end()) {
                option_list.insert(std::make_pair(it->first, it->second));
            }
        }
    }*/

    // same machine only appear once
    for (auto it = option_list.begin(); it != option_list.end(); it++) {
        if (it->second && it->second->is_online()) {
            machine_list.push_back(it->second->dev_name);
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
                m_comboBox_printer->Append(dev_name_text);
                break;
            }
        }
    }

    MachineObject* obj = dev->get_selected_machine();
    if (obj) {
        m_printer_last_select = obj->dev_id;
    }
    if (m_list.size() > 0) {
        // select a default machine
        if (m_printer_last_select.empty()) {
            update_select_layout(m_list[0]->printer_type);
            m_printer_last_select = m_list[0]->dev_id;
            m_comboBox_printer->SetSelection(0);
            wxCommandEvent event(wxEVT_COMBOBOX);
            event.SetEventObject(m_comboBox_printer);
            wxPostEvent(m_comboBox_printer, event);
        }
        for (auto i = 0; i < m_list.size(); i++) {
            if (m_list[i]->dev_id == m_printer_last_select) {
                update_select_layout(m_list[i]->printer_type);
                m_comboBox_printer->SetSelection(i);
                wxCommandEvent event(wxEVT_COMBOBOX);
                event.SetEventObject(m_comboBox_printer);
                wxPostEvent(m_comboBox_printer, event);
            }
        }
    } else {
        m_printer_last_select = "";
        update_select_layout(PRINTER_TYPE::PRINTER_3DPrinter_NONE);
        m_comboBox_printer->SetTextLabel("");
    }

    dev->set_selected_machine(m_printer_last_select);

    MachineObject* obj_ = dev->get_selected_machine();
    NetworkAgent* agent = wxGetApp().getAgent();
    if (!obj_) {
        if (agent) {
            if (agent->is_user_login()) {
                show_status(PrintDialogStatus::PrintStatusInit);
            }
            else {
                show_status(PrintDialogStatus::PrintStatusNoUserLogin);
            }
        }
    } else {
        /* check cloud machine connections */
        if (!obj_->is_lan_mode_printer()) {
            if (!obj_->is_info_ready()) {
                if (!agent->is_server_connected()) {
                    agent->refresh_connection();
                    show_status(PrintDialogStatus::PrintStatusConnectingServer);
                } else {
                    show_status(PrintDialogStatus::PrintStatusReading);
                }
            }
        } else {
            if (obj_->is_info_ready()) {
                if (obj_->has_sdcard()) {
                    show_status(PrintDialogStatus::PrintStatusReading);
                } else {
                    show_status(PrintDialogStatus::PrintStatusNoSdcard);
                }
            }
        }
    }

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "for send task, current printer id =  " << m_printer_last_select << std::endl;
}

void SelectMachineDialog::on_timer(wxTimerEvent &event)
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
    if (!agent) return;
    if (!dev) return;
    MachineObject* obj_ = dev->get_selected_machine();
    if (!obj_) {
        if (agent) {
            if (agent->is_user_login()) {
                show_status(PrintDialogStatus::PrintStatusInvalidPrinter);
            } else {
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
            show_status(PrintDialogStatus::PrintStatusReadingTimeout);
            return;
        } else {
            timeout_count++;
            show_status(PrintDialogStatus::PrintStatusReading);
            return;
        }
        return;
    }
    reset_timeout();
    
    // reading done
    if (obj_->is_in_upgrading()) {
        show_status(PrintDialogStatus::PrintStatusInUpgrading);
        return;
    } else if (obj_->is_system_printing()) {
        show_status(PrintDialogStatus::PrintStatusInSystemPrinting);
        return;
    } else if (obj_->is_in_printing()) {
        show_status(PrintDialogStatus::PrintStatusInPrinting);
        return;
    } 

    // no ams
    if (!obj_->has_ams()) {
        show_status(PrintDialogStatus::PrintStatusReadingFinished);
        return;
    }

    if (!obj_->is_support_ams_mapping()) {
        do_ams_mapping(obj_);
        show_status(PrintDialogStatus::PrintStatusAmsMappingByOrder);
        return;
    }

    // do ams mapping if no ams result
    if (m_ams_mapping_result.empty()) {
        do_ams_mapping(obj_);
    }

    if (m_ams_mapping_res) {
        show_status(PrintDialogStatus::PrintStatusAmsMappingSuccess);
        return;
    } else {
        if (obj_->is_valid_mapping_result(m_ams_mapping_result)) {
            show_status(PrintDialogStatus::PrintStatusAmsMappingValid);
        } else {
            show_status(PrintDialogStatus::PrintStatusAmsMappingInvalid);
        }
    }
}

void SelectMachineDialog::on_selection_changed(wxCommandEvent &event)
{
    /* reset timeout and reading printer info */
    timeout_count      = 0;
    m_ams_mapping_res  = false;
    ams_mapping_valid  = false;
    m_ams_mapping_result.clear();

    // reading printer info
    show_status(PrintDialogStatus::PrintStatusReading);

    if (event.GetString().empty()) { return; }

    auto dev_name  = event.GetString().ToStdString();
    auto selection = event.GetSelection();

    
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;
    MachineObject* obj = nullptr;
    for (int i = 0; i < m_list.size(); i++) {
        if (i == selection) {
            m_printer_last_select = m_list[i]->dev_id;
            obj = m_list[i];
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "for send task, current printer id =  " << m_printer_last_select << std::endl;
            break;
        }
    }

    if (obj) {
        dev->set_selected_machine(m_printer_last_select);
        update_select_layout(obj->printer_type);
    } else {
        BOOST_LOG_TRIVIAL(error) << "on_selection_changed dev_id not found";
        return;
    }

    MaterialHash::iterator iter = m_materialList.begin();
    while (iter != m_materialList.end()) {
        int           id   = iter->first;
        Material *    item = iter->second;
        MaterialItem *m    = item->item;
        wxString ams_id  = "-";
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
    m_button_ensure->SetMinSize(SELECT_MACHINE_DIALOG_BUTTON_SIZE);
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
    //clear combobox
    m_list.clear();
    m_comboBox_printer->Clear();
    m_printer_last_select = "";
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

    // checkbox default values
    m_checkbox_list["bed_leveling"]->SetValue(true);
    m_checkbox_list["flow_cali"]->SetValue(true);

    // thumbmail
    Freeze();
    wxBitmap bitmap;
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
        bitmap = wxBitmap(image);
    } else {
        bitmap = wxBitmap(wxSize(FromDIP(256), FromDIP(256)), 0);
    }
    m_staticbitmap->SetBitmap(bitmap);
    sizer_thumbnail->Layout();

    std::vector<std::string> materials;
    {
        auto preset_bundle = wxGetApp().preset_bundle;
        for (auto filament_name : preset_bundle->filament_presets) {
            for (auto iter = preset_bundle->filaments.lbegin(); iter != preset_bundle->filaments.end(); iter++) {
                if (filament_name.compare(iter->name) == 0) {
                    materials.push_back(iter->config.get_filament_type());
                }
            }
        }
    }

    // material info
    auto        extruders = m_plater->get_partplate_list().get_curr_plate()->get_extruders();
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
        MaterialItem *item       = new MaterialItem(this, colour_rgb, _L(materials[extruder]));
        m_sizer_material->Add(item, 0, wxLEFT | wxRIGHT, FromDIP(5));

        // item->Layout();

        //item->Bind(wxEVT_LEFT_UP, [this, item, materials, extruder](wxMouseEvent &e) {
        //    auto    mouse_pos = ClientToScreen(e.GetPosition());
        //    wxPoint rect      = item->ClientToScreen(wxPoint(0, 0));

        //    auto    mapping = new AmsMapingPopup(this);
        //    wxPoint pos     = item->ClientToScreen(wxPoint(0, 0));
        //    pos.y += item->GetRect().height;
        //    mapping->Position(pos, wxSize(0, 0));

        //    // update ams data
        //    DeviceManager *dev_manager = Slic3r::GUI::wxGetApp().getDeviceManager();
        //    if (!dev_manager) return;
        //    MachineObject *obj_        = dev_manager->get_selected_machine();

        //    if (obj_ && obj_->has_ams()) {
        //        mapping->update_ams_data(obj_->amsList);
        //        mapping->set_tag_texture(materials[extruder]);
        //        mapping->Popup();
        //    }
        //    e.Skip();
        //});

        item->Bind(wxEVT_LEFT_DOWN, [this, item, extruder](wxMouseEvent &e) {
            Freeze();
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
            Thaw();
            e.Skip();
        });

        Material *material_item = new Material();
        material_item->id       = extruder;
        material_item->item     = item;
        m_materialList[i]       = material_item;

        // build for ams mapping
        if (extruder < materials.size()) {
            FilamentInfo info;
            info.id    = extruder;
            info.type  = materials[extruder];
            info.color = colour_rgb.GetAsString(wxC2S_HTML_SYNTAX).ToStdString();
            m_filaments.push_back(info);
        }
    }

    // basic info
    auto       aprint_stats = m_plater->get_partplate_list().get_current_fff_print().print_statistics();
    wxString   time;
    PartPlate *plate = m_plater->get_partplate_list().get_curr_plate();
    if (plate) {
        if (plate->get_slice_result()) { time = wxString::Format("%s", get_bbl_remain_time_dhms(plate->get_slice_result()->print_statistics.modes[0].time)); }
    }

    char weight[64];
    ::sprintf(weight, "  %.2f g", aprint_stats.total_weight);

    m_stext_time->SetLabel(time);
    m_stext_weight->SetLabel(weight);
}

bool SelectMachineDialog::Show(bool show)
{
    // set default value when show this dialog
    if (show) {
        set_default();
    }

    NetworkAgent *agent = Slic3r::GUI::wxGetApp().getAgent();
    if (agent) {
        if (show)
            agent->start_subscribe("send_print");
        else
            agent->stop_subscribe("send_print");

        if (agent->is_user_login()) {
            boost::thread get_print_info_thread = Slic3r::create_thread([this] {
                DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
                if (dev) {
                    dev->update_user_machine_list_info();
                }

                wxCommandEvent event(EVT_UPDATE_USER_MACHINE_LIST);
                event.SetEventObject(this);
                wxPostEvent(this, event);
            });
        }

        /*if (show) {
            agent->start_discovery(true, true);
        } else {
            agent->start_discovery(true, false);
        }*/
    }

    if (show) {
        m_refresh_timer->Start(LIST_REFRESH_INTERVAL);
    } else {
        m_refresh_timer->Stop();
    }
    Thaw();
    if (show) { CenterOnParent(); }

    Layout();
    return DPIDialog::Show(show);
}

SelectMachineDialog::~SelectMachineDialog()
{
    ;
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
    m_button_confirm->SetCornerRadius(12);
    m_button_confirm->Bind(wxEVT_BUTTON, &EditDevNameDialog::on_edit_name, this);

    m_sizer_main->Add(m_button_confirm, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP, FromDIP(10));
    m_sizer_main->Add(0, 0, 0, wxBOTTOM, FromDIP(38));

    SetSizer(m_sizer_main);
    Layout();
    Fit();
    Centre(wxBOTH);
}

EditDevNameDialog::~EditDevNameDialog() {}

void EditDevNameDialog::set_machine_obj(MachineObject *obj)
{
    m_info = obj;
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
            dev->modify_device_name(m_info->dev_id, name);
        }
        DPIDialog::EndModal(wxID_CLOSE);
    }
}

 }} // namespace Slic3r::GUI
