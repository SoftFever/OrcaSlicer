#include "FanControl.hpp"
#include "Label.hpp"
#include "../BitmapCache.hpp"
#include "../I18N.hpp"
#include "../GUI_App.hpp"

#include <wx/simplebook.h>
#include <wx/dcgraph.h>

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_FAN_SWITCH_ON, wxCommandEvent);
wxDEFINE_EVENT(EVT_FAN_SWITCH_OFF, wxCommandEvent);
wxDEFINE_EVENT(EVT_FAN_ADD, wxCommandEvent);
wxDEFINE_EVENT(EVT_FAN_DEC, wxCommandEvent);
wxDEFINE_EVENT(EVT_FAN_CHANGED, wxCommandEvent);

/*************************************************
Description:Fan
**************************************************/
Fan::Fan(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    create(parent, id, pos, size);
}

void Fan::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    m_current_speeds  = 0;

    wxWindow::Create(parent, id, pos, size, wxBORDER_NONE);
    SetBackgroundColour(*wxWHITE);

    m_rotate_offsets.push_back(RotateOffSet{ 2.5, wxPoint(-FromDIP(16), FromDIP(11)) });
    m_rotate_offsets.push_back(RotateOffSet{ 2.2, wxPoint(-FromDIP(20), FromDIP(11)) });
    m_rotate_offsets.push_back(RotateOffSet{ 1.7, wxPoint(-FromDIP(24), FromDIP(12)) });
    m_rotate_offsets.push_back(RotateOffSet{ 1.2, wxPoint(-FromDIP(22), FromDIP(4)) });
    m_rotate_offsets.push_back(RotateOffSet{ 0.7, wxPoint(-FromDIP(17), -FromDIP(6)) });
    m_rotate_offsets.push_back(RotateOffSet{ 0.3, wxPoint(-FromDIP(8), -FromDIP(11)) });
    m_rotate_offsets.push_back(RotateOffSet{ 6.1, wxPoint(-FromDIP(0), -FromDIP(9)) });
    m_rotate_offsets.push_back(RotateOffSet{ 5.5, wxPoint(-FromDIP(4), -FromDIP(2)) });
    m_rotate_offsets.push_back(RotateOffSet{ 5.1, wxPoint(-FromDIP(3), FromDIP(5)) });
    m_rotate_offsets.push_back(RotateOffSet{ 4.6, wxPoint(-FromDIP(3), FromDIP(14)) });
    m_rotate_offsets.push_back(RotateOffSet{ 4.0, wxPoint(-FromDIP(2), FromDIP(11)) });

    //auto m_bitmap_pointer  = ScalableBitmap(this, "fan_pointer", FromDIP(25));
    //m_img_pointer     = m_bitmap_pointer.bmp().ConvertToImage();

    m_bitmap_bk  = ScalableBitmap(this, "fan_dash_bk", FromDIP(80));

    for (auto i = 0; i <= 10; i++) {
#ifdef __APPLE__
        auto m_bitmap_scale  = ScalableBitmap(this, wxString::Format("fan_scale_%d", i).ToStdString(), FromDIP(60));
        m_bitmap_scales.push_back(m_bitmap_scale);
#else
        auto m_bitmap_scale  = ScalableBitmap(this, wxString::Format("fan_scale_%d", i).ToStdString(), FromDIP(46));
        m_bitmap_scales.push_back(m_bitmap_scale);
#endif
        
    }

//#ifdef __APPLE__
//    SetMinSize(wxSize(FromDIP(100), FromDIP(100) + FromDIP(6)));
//    SetMaxSize(wxSize(FromDIP(100), FromDIP(100) + FromDIP(6)));
//#else
    SetMinSize(wxSize(m_bitmap_bk.GetBmpSize().x, m_bitmap_bk.GetBmpSize().y + FromDIP(6)));
    SetMaxSize(wxSize(m_bitmap_bk.GetBmpSize().x, m_bitmap_bk.GetBmpSize().y + FromDIP(6)));
//#endif // __APPLE__
    
    Bind(wxEVT_PAINT, &Fan::paintEvent, this);
}

void Fan::set_fan_speeds(int g)
{
    m_current_speeds = g;
    Refresh();
}

void Fan::post_event(wxCommandEvent &&event)
{
    /*event.SetString(m_info.can_id);
    event.SetEventObject(m_parent);
    wxPostEvent(m_parent, event);
    event.Skip();*/
}

void Fan::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void Fan::render(wxDC& dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({ 0, 0 }, size, &dc, { 0, 0 });

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

void Fan::doRender(wxDC& dc)
{
    auto rpm = wxT("rpm");

    wxSize size = GetSize();
    dc.DrawBitmap(m_bitmap_bk.bmp(), wxPoint(0,0));

    //fan scale
    /*auto central_point = wxPoint(size.x / 2, size.y / 2 + FromDIP(15));
    dc.DrawBitmap(m_bitmap_scale_0.bmp(), central_point.x - FromDIP(38), central_point.y);
    dc.DrawBitmap(m_bitmap_scale_1.bmp(), central_point.x - FromDIP(40), central_point.y - FromDIP(17));
    dc.DrawBitmap(m_bitmap_scale_2.bmp(), central_point.x - FromDIP(40), central_point.y - FromDIP(36));
    dc.DrawBitmap(m_bitmap_scale_3.bmp(), central_point.x - FromDIP(32), central_point.y - FromDIP(48));
    dc.DrawBitmap(m_bitmap_scale_4.bmp(), central_point.x - FromDIP(18), central_point.y - FromDIP(53));
    dc.DrawBitmap(m_bitmap_scale_5.bmp(), central_point.x - FromDIP(0),  central_point.y  - FromDIP(53));
    dc.DrawBitmap(m_bitmap_scale_6.bmp(), central_point.x + FromDIP(18), central_point.y - FromDIP(48));
    dc.DrawBitmap(m_bitmap_scale_7.bmp(), central_point.x + FromDIP(31), central_point.y - FromDIP(36));
    dc.DrawBitmap(m_bitmap_scale_8.bmp(), central_point.x + FromDIP(36), central_point.y - FromDIP(17));
    dc.DrawBitmap(m_bitmap_scale_9.bmp(), central_point.x + FromDIP(28), central_point.y);*/

    //fan pointer
    //auto pointer_central_point = wxPoint((size.x - m_img_pointer.GetSize().x) / 2, (size.y - m_img_pointer.GetSize().y) / 2);
    //auto bmp = m_img_pointer.Rotate(m_rotate_offsets[m_current_speeds].rotate, wxPoint(size.x / 2,size.y / 2));
    auto central_point = wxPoint((size.x  - m_bitmap_scales[m_current_speeds].GetBmpSize().x) / 2, (size.y  - m_bitmap_scales[m_current_speeds].GetBmpSize().y) / 2 - FromDIP(4));
    dc.DrawBitmap(m_bitmap_scales[m_current_speeds].bmp(), central_point.x, central_point.y);

    //fan val
    dc.SetTextForeground(DRAW_TEXT_COLOUR);
    dc.SetFont(::Label::Head_13);
    auto speeds = wxString::Format("%d%%", m_current_speeds * 10);
    dc.DrawText(speeds, (size.x - dc.GetTextExtent(speeds).x) / 2 + FromDIP(2), size.y - dc.GetTextExtent(speeds).y - FromDIP(5));

    //rpm
    //dc.SetFont(::Label::Body_13);
    //dc.DrawText(rpm, (size.x - dc.GetTextExtent(rpm).x) / 2, size.y - dc.GetTextExtent(rpm).y);
}

void Fan::msw_rescale() {
   m_bitmap_bk.msw_rescale();
}

void Fan::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
}


/*************************************************
Description:FanOperate
**************************************************/
FanOperate::FanOperate(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    m_current_speeds = 0;
    m_min_speeds     = 1;
    m_max_speeds     = 10;
    create(parent, id, pos, size);
}

void FanOperate::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    
    wxWindow::Create(parent, id, pos, size, wxBORDER_NONE);
    SetBackgroundColour(*wxWHITE);

    m_bitmap_add        = ScalableBitmap(this, "fan_control_add", FromDIP(11));
    m_bitmap_decrease   = ScalableBitmap(this, "fan_control_decrease", FromDIP(11));

    SetMinSize(wxSize(FromDIP(SIZE_OF_FAN_OPERATE.x), FromDIP(SIZE_OF_FAN_OPERATE.y)));
    Bind(wxEVT_PAINT, &FanOperate::paintEvent, this);
    Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND);});
    Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_ARROW);});
    Bind(wxEVT_LEFT_DOWN, &FanOperate::on_left_down, this);
}

void FanOperate::on_left_down(wxMouseEvent& event)
{
     auto mouse_pos = ClientToScreen(event.GetPosition());
     auto win_pos = ClientToScreen(wxPoint(0, 0));

     auto decrease_fir = GetSize().x / 3 + win_pos.x;
     auto add_fir = GetSize().x / 3 * 2 + win_pos.x;

     if (mouse_pos.x > win_pos.x && mouse_pos.x < decrease_fir && mouse_pos.y > win_pos.y && mouse_pos.y < (win_pos.y + GetSize().y)) {
         decrease_fan_speeds();
         return;
     }

     if (mouse_pos.x > add_fir && mouse_pos.x < (win_pos.x + GetSize().x) && mouse_pos.y > win_pos.y && mouse_pos.y < (win_pos.y + GetSize().y)) {        
         add_fan_speeds();
         return;
     }
}

void FanOperate::set_fan_speeds(int g)
{
    m_current_speeds = g;
    Refresh();
}

void FanOperate::add_fan_speeds()
{
    if (m_current_speeds + 1 > m_max_speeds) return;
    set_fan_speeds(++m_current_speeds);
    post_event(wxCommandEvent(EVT_FAN_ADD)); 
    post_event(wxCommandEvent(EVT_FAN_SWITCH_ON)); 
}

void FanOperate::decrease_fan_speeds()
{
    //turn off
    if (m_current_speeds - 1 < m_min_speeds) {
        m_current_speeds = 0;
        set_fan_speeds(m_current_speeds);
        post_event(wxCommandEvent(EVT_FAN_SWITCH_OFF));
    }
    else {
        set_fan_speeds(--m_current_speeds);
    }
     post_event(wxCommandEvent(EVT_FAN_DEC));
    
}

void FanOperate::post_event(wxCommandEvent &&event)
{
    event.SetInt(m_current_speeds);
    event.SetEventObject(this);
    wxPostEvent(this, event);
    event.Skip();
}

void FanOperate::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void FanOperate::render(wxDC& dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({ 0, 0 }, size, &dc, { 0, 0 });

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

void FanOperate::doRender(wxDC& dc)
{
    wxSize size = GetSize();
    dc.SetPen(wxPen(DRAW_OPERATE_LINE_COLOUR));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0,0,size.x,size.y,5);

    //splt
    auto left_fir = size.x / 3;

    dc.DrawLine(left_fir, FromDIP(4), left_fir, size.y - FromDIP(4));
    dc.DrawLine(left_fir * 2, FromDIP(4), left_fir * 2, size.y - FromDIP(4));

    dc.DrawBitmap(m_bitmap_decrease.bmp(), (left_fir - m_bitmap_decrease.GetBmpSize().x) / 2, (size.y - m_bitmap_decrease.GetBmpSize().y) / 2);
    dc.DrawBitmap(m_bitmap_add.bmp(), (left_fir * 2 + (left_fir - m_bitmap_decrease.GetBmpSize().x) / 2), (size.y - m_bitmap_add.GetBmpSize().y) / 2);

    //txt
    dc.SetFont(::Label::Body_12);
    dc.SetTextForeground(StateColor::darkModeColorFor(wxColour(0x898989)));
    wxString text = wxString::Format("%d%%", 10);
    wxSize text_size = dc.GetTextExtent(text);
    dc.DrawText(text, wxPoint(left_fir + (left_fir- text_size.x) / 2, (size.y- text_size.y) / 2));
}

void FanOperate::msw_rescale() {
}

/*************************************************
Description:FanControl
**************************************************/
FanControl::FanControl(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size)
    :wxWindow(parent, id, pos, size)
{
    auto m_bitmap_fan = new ScalableBitmap(this, "fan_icon", 18);
    m_bitmap_toggle_off = new ScalableBitmap(this, "toggle_off", 14);
    m_bitmap_toggle_on = new ScalableBitmap(this, "toggle_on", 14);



    SetBackgroundColour(*wxWHITE);

    wxBoxSizer* m_sizer_main = new wxBoxSizer(wxHORIZONTAL);

    m_fan = new Fan(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_sizer_main->Add(m_fan, 1, wxEXPAND | wxALL, 0);


    m_sizer_main->Add(0, 0, 0, wxLEFT, FromDIP(18));

    wxBoxSizer* sizer_control = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* sizer_control_top = new wxBoxSizer(wxHORIZONTAL);

    
    auto m_static_bitmap_fan = new wxStaticBitmap(this, wxID_ANY, m_bitmap_fan->bmp(), wxDefaultPosition, wxDefaultSize);
    

    m_static_name = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END|wxALIGN_CENTER_HORIZONTAL);
    m_static_name->SetForegroundColour(wxColour(DRAW_TEXT_COLOUR));
    m_static_name->SetBackgroundColour(*wxWHITE);
    m_static_name->SetMinSize(wxSize(FromDIP(50), -1));
    m_static_name->SetMaxSize(wxSize(FromDIP(50), -1));

    
    m_switch_button = new wxStaticBitmap(this, wxID_ANY, m_bitmap_toggle_off->bmp(), wxDefaultPosition, wxDefaultSize, 0);
    m_switch_button->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_HAND); });
    m_switch_button->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) { SetCursor(wxCURSOR_RIGHT_ARROW); });
    m_switch_button->Bind(wxEVT_LEFT_DOWN, &FanControl::on_swith_fan, this);


    sizer_control_top->Add(m_static_bitmap_fan, 0, wxALIGN_CENTER, 5);
    sizer_control_top->Add(m_static_name, 0, wxALIGN_CENTER, 0);
    sizer_control_top->Add( 0, 0, 1, wxEXPAND, 0 );
    sizer_control_top->Add(m_switch_button, 0, wxALIGN_CENTER, 0);


    sizer_control->Add(sizer_control_top, 0, wxALIGN_CENTER, 0);
    sizer_control->Add(0, 0, 0, wxTOP, FromDIP(15));

    m_fan_operate = new FanOperate(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    sizer_control->Add(m_fan_operate, 0, wxALIGN_CENTER, 0);


    m_sizer_main->Add(sizer_control, 0, wxALIGN_CENTER, 0);


    this->SetSizer(m_sizer_main);
    this->Layout();
    m_sizer_main->Fit(this);

    m_fan_operate->Bind(EVT_FAN_SWITCH_ON, [this](wxCommandEvent& e) {
        m_current_speed = e.GetInt();
        m_switch_button->SetBitmap(m_bitmap_toggle_on->bmp());
        m_switch_fan = true;
        m_fan->set_fan_speeds(m_current_speed);
    });
    m_fan_operate->Bind(EVT_FAN_SWITCH_OFF, [this](wxCommandEvent& e) {
        m_current_speed = e.GetInt();
        m_switch_button->SetBitmap(m_bitmap_toggle_off->bmp());
        m_switch_fan = false;
        m_fan->set_fan_speeds(m_current_speed);
    });

    m_fan_operate->Bind(EVT_FAN_ADD, [this](wxCommandEvent& e) {
        m_current_speed = e.GetInt();
        m_fan->set_fan_speeds(m_current_speed);
        command_control_fan();
    });

    m_fan_operate->Bind(EVT_FAN_DEC, [this](wxCommandEvent& e) {
        m_current_speed = e.GetInt();
        m_fan->set_fan_speeds(m_current_speed);
        command_control_fan();
    });
}

void FanControl::on_left_down(wxMouseEvent& evt)
{
    auto mouse_pos = ClientToScreen(evt.GetPosition());
    auto tag_pos = m_fan_operate->ScreenToClient(mouse_pos);
    evt.SetPosition(tag_pos);
    m_fan_operate->on_left_down(evt);
}

void FanControl::command_control_fan()
{
    if (m_current_speed < 0 || m_current_speed > 10) { return; }
    int speed = floor(m_current_speed * float(25.5));
    if (m_update_already && m_obj) {
        m_obj->command_control_fan_val(m_type, speed);
        post_event(wxCommandEvent(EVT_FAN_CHANGED));
    }
}

void FanControl::on_swith_fan(wxMouseEvent& evt)
{
    int speed = 0;
    if (m_switch_fan) {
        m_switch_button->SetBitmap(m_bitmap_toggle_off->bmp());
        m_switch_fan = false;
    }
    else {
        speed = 255;
        m_switch_button->SetBitmap(m_bitmap_toggle_on->bmp());
        m_switch_fan = true;
    }

    set_fan_speed(speed);
    command_control_fan();
}

void FanControl::on_swith_fan(bool on)
{
    m_switch_fan = on;
    if (m_switch_fan) {
        m_switch_button->SetBitmap(m_bitmap_toggle_on->bmp());
    }
    else {
        m_switch_button->SetBitmap(m_bitmap_toggle_off->bmp());
    }
}

void FanControl::set_machine_obj(MachineObject* obj)
{
    m_update_already = true;
    m_obj = obj;
}

void FanControl::set_type(MachineObject::FanType type)
{
    m_type = type;
}

void FanControl::set_name(wxString name) 
{
    m_static_name->SetLabelText(name);
}

void FanControl::set_fan_speed(int g)
{
    if (g < 0 || g > 255) return;
    int speed = round(float(g) / float(25.5));

    if (m_current_speed != speed) {
        m_current_speed = speed;
        m_fan->set_fan_speeds(speed);
        m_fan_operate->set_fan_speeds(m_current_speed);

        if (m_current_speed <= 0) {
            on_swith_fan(false);
        }
        else {
            on_swith_fan(true);
        }
    }
   
}

void FanControl::set_fan_switch(bool s)
{

}

void FanControl::post_event(wxCommandEvent&& event)
{
    event.SetInt(m_type);
    event.SetString(wxString::Format("%d", m_current_speed));
    event.SetEventObject(GetParent());
    wxPostEvent(GetParent(), event);
    event.Skip();
}


/*************************************************
Description:FanControlPopup
**************************************************/
FanControlPopup::FanControlPopup(wxWindow* parent)
    :PopupWindow(parent, wxBORDER_NONE)
{
    this->SetSizeHints(wxDefaultSize, wxDefaultSize);

    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    m_part_fan = new FanControl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_part_fan->set_type(MachineObject::FanType::COOLING_FAN);
    m_part_fan->set_name(_L("Part"));

    m_aux_fan = new FanControl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_aux_fan->set_type(MachineObject::FanType::BIG_COOLING_FAN);
    m_aux_fan->set_name(_L("Aux"));

    m_cham_fan = new FanControl(this, wxID_ANY, wxDefaultPosition, wxDefaultSize);
    m_cham_fan->set_type(MachineObject::FanType::CHAMBER_FAN);
    m_cham_fan->set_name(_L("Cham"));

    m_line_top = new wxWindow(this, wxID_ANY);
    m_line_top->SetSize(wxSize(-1, 1));
    m_line_top->SetBackgroundColour(0xF1F1F1);

    m_line_bottom = new wxWindow(this, wxID_ANY);
    m_line_bottom->SetSize(wxSize(-1, 1));
    m_line_bottom->SetBackgroundColour(0xF1F1F1);


    m_sizer_main->Add(m_part_fan, 0, wxALL, FromDIP(14));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(8));
    m_sizer_main->Add(m_aux_fan, 0, wxALL, FromDIP(14));
    m_sizer_main->Add(m_line_bottom, 0, wxEXPAND|wxLEFT|wxRIGHT, FromDIP(8));
    m_sizer_main->Add(m_cham_fan, 0, wxALL, FromDIP(14));


    this->SetSizer(m_sizer_main);
    this->Layout();
    m_sizer_main->Fit(this);

    this->Centre(wxBOTH);
    Bind(wxEVT_PAINT, &FanControlPopup::paintEvent, this);

#if __APPLE__
    Bind(wxEVT_LEFT_DOWN, &FanControlPopup::on_left_down, this);
#endif

#ifdef __WXOSX__
    Bind(wxEVT_IDLE, [](wxIdleEvent& evt) {});
#endif

    Bind(wxEVT_SHOW, &FanControlPopup::on_show, this);
    SetBackgroundColour(*wxWHITE);
}

void FanControlPopup::show_cham_fan(bool support_cham_fun)
{
    
    if (support_cham_fun && !m_is_suppt_cham_fun) {
        m_cham_fan->Show();
        m_line_bottom->Show();
        Layout();
        Fit();
    }
    
    if (!support_cham_fun && m_is_suppt_cham_fun) {
        m_cham_fan->Hide();
        m_line_bottom->Hide();
        Layout();
        Fit();
    }
    m_is_suppt_cham_fun = support_cham_fun;
}

void FanControlPopup::show_aux_fan(bool support_aux_fun)
{

    if (support_aux_fun && !m_is_suppt_aux_fun) {
        m_aux_fan->Show();
        m_line_bottom->Show();
        Layout();
        Fit();
    }

    if (!support_aux_fun && m_is_suppt_aux_fun) {
        m_aux_fan->Hide();
        m_line_bottom->Hide();
        Layout();
        Fit();
    }
    m_is_suppt_aux_fun = support_aux_fun;
}


void FanControlPopup::update_fan_data(MachineObject::FanType type, MachineObject* obj)
{
    m_is_suppt_cham_fun = obj->is_support_chamber_fan;
    show_cham_fan(m_is_suppt_cham_fun);

    if (type ==  MachineObject::FanType::COOLING_FAN && obj->cooling_fan_speed >= 0) {
        m_part_fan->set_fan_speed(obj->cooling_fan_speed);
    }

    if (type ==  MachineObject::FanType::BIG_COOLING_FAN && obj->big_fan1_speed >= 0) {
        m_aux_fan->set_fan_speed(obj->big_fan1_speed);
    }

    if (type ==  MachineObject::FanType::CHAMBER_FAN && obj->big_fan2_speed >= 0) {
        m_cham_fan->set_fan_speed(obj->big_fan2_speed);
    }

    m_part_fan->set_machine_obj(obj);
    m_aux_fan->set_machine_obj(obj);
    m_cham_fan->set_machine_obj(obj);

    Bind(EVT_FAN_CHANGED, [this](wxCommandEvent& e) {
        post_event(e.GetInt(), e.GetString());
    });
}

void FanControlPopup::on_left_down(wxMouseEvent& evt)
{
    auto mouse_pos = ClientToScreen(evt.GetPosition());
    
    auto win_pos =  m_part_fan->m_switch_button->ClientToScreen(wxPoint(0, 0));
    auto size =  m_part_fan->m_switch_button->GetSize();
    if (mouse_pos.x > win_pos.x && mouse_pos.x < (win_pos.x +  m_part_fan->m_switch_button->GetSize().x) && mouse_pos.y > win_pos.y && mouse_pos.y < (win_pos.y +  m_part_fan->m_switch_button->GetSize().y)) {
        m_part_fan->on_swith_fan(evt);
    }
    
    win_pos =  m_aux_fan->m_switch_button->ClientToScreen(wxPoint(0, 0));
    size =  m_aux_fan->m_switch_button->GetSize();
    if (mouse_pos.x > win_pos.x && mouse_pos.x < (win_pos.x +  m_aux_fan->m_switch_button->GetSize().x) && mouse_pos.y > win_pos.y && mouse_pos.y < (win_pos.y +  m_aux_fan->m_switch_button->GetSize().y)) {
        m_aux_fan->on_swith_fan(evt);
    }
    
    win_pos =  m_cham_fan->m_switch_button->ClientToScreen(wxPoint(0, 0));
    size =  m_cham_fan->m_switch_button->GetSize();
    if (mouse_pos.x > win_pos.x && mouse_pos.x < (win_pos.x +  m_cham_fan->m_switch_button->GetSize().x) && mouse_pos.y > win_pos.y && mouse_pos.y < (win_pos.y +  m_cham_fan->m_switch_button->GetSize().y)) {
        m_cham_fan->on_swith_fan(evt);
    }
    
    auto part_tag_pos = m_part_fan->ScreenToClient(mouse_pos);
    evt.SetPosition(part_tag_pos);
    m_part_fan->on_left_down(evt);

    auto aux_tag_pos = m_aux_fan->ScreenToClient(mouse_pos);
    evt.SetPosition(aux_tag_pos);
    m_aux_fan->on_left_down(evt);

    auto cham_tag_pos = m_cham_fan->ScreenToClient(mouse_pos);
    evt.SetPosition(cham_tag_pos);
    m_cham_fan->on_left_down(evt);
    evt.Skip();
}

void FanControlPopup::OnDismiss()
{
    m_part_fan->update_obj_state(false);
    m_aux_fan->update_obj_state(false);
    m_cham_fan->update_obj_state(false);
}

void FanControlPopup::post_event(int fan_type, wxString speed)
{
    wxCommandEvent event(EVT_FAN_CHANGED); 
    event.SetInt(fan_type);
    event.SetString(speed);
    event.SetEventObject(GetParent());
    wxPostEvent(GetParent(), event);
    event.Skip();
}

bool FanControlPopup::ProcessLeftDown(wxMouseEvent& event)
{
    return PopupWindow::ProcessLeftDown(event);
}

void FanControlPopup::on_show(wxShowEvent& evt)
{
    wxGetApp().UpdateDarkUIWin(this);
}

void FanControlPopup::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    dc.SetPen(wxColour(0xAC, 0xAC, 0xAC));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 0);
}
}} // namespace Slic3r::GUI
