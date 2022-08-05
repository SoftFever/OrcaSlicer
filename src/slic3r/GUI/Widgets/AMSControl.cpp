#include "AMSControl.hpp"
#include "Label.hpp"
#include "../BitmapCache.hpp"
#include "../I18N.hpp"

#include <wx/simplebook.h>
#include <wx/dcgraph.h>

namespace Slic3r { namespace GUI {

static const int LOAD_STEP_COUNT   = 5;
static const int UNLOAD_STEP_COUNT = 3;

static const wxColour AMS_TRAY_DEFAULT_COL = wxColour(255, 255, 255);

static wxString FILAMENT_LOAD_STEP_STRING[LOAD_STEP_COUNT] = {
    _L("Heat the nozzle to target temperature"), 
    _L("Cut filament"), 
    _L("Pull back current filament"),
    _L("Push new filament into extruder"),
    _L("Purge old filament"),
};

static wxString FILAMENT_UNLOAD_STEP_STRING[UNLOAD_STEP_COUNT] = {_L("Heat the nozzle to target temperature"), _L("Cut filament"), _L("Pull back current filament")};

wxDEFINE_EVENT(EVT_AMS_LOAD, SimpleEvent);
wxDEFINE_EVENT(EVT_AMS_UNLOAD, SimpleEvent);
wxDEFINE_EVENT(EVT_AMS_SETTINGS, SimpleEvent);
wxDEFINE_EVENT(EVT_AMS_REFRESH_RFID, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_ON_SELECTED, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_ON_FILAMENT_EDIT, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_CLIBRATION_AGAIN, wxCommandEvent);
wxDEFINE_EVENT(EVT_AMS_CLIBRATION_CANCEL, wxCommandEvent);

inline int hex_digit_to_int(const char c)
{
    return (c >= '0' && c <= '9') ? int(c - '0') : (c >= 'A' && c <= 'F') ? int(c - 'A') + 10 : (c >= 'a' && c <= 'f') ? int(c - 'a') + 10 : -1;
}

static wxColour decode_color(const std::string &color)
{
    std::array<int, 3> ret = {0, 0, 0};
    const char *       c   = color.data() + 1;
    if (color.size() == 8) {
        for (size_t j = 0; j < 3; ++j) {
            int digit1 = hex_digit_to_int(*c++);
            int digit2 = hex_digit_to_int(*c++);
            if (digit1 == -1 || digit2 == -1) break;
            ret[j] = float(digit1 * 16 + digit2);
        }
    }
    return wxColour(ret[0], ret[1], ret[2]);
}

bool AMSinfo::parse_ams_info(Ams *ams)
{
    if (!ams) return false;
    this->ams_id = ams->id;
    cans.clear();
    for (int i = 0; i < 4; i++) {
        auto    it = ams->trayList.find(std::to_string(i));
        Caninfo info;
        // tray is exists
        if (it != ams->trayList.end() && it->second->is_exists) {
            if (it->second->is_tray_info_ready()) {
                info.can_id        = it->second->id;
                info.material_name = it->second->get_display_filament_type();
                if (!it->second->color.empty()) {
                    info.material_colour = AmsTray::decode_color(it->second->color);
                } else {
                    // set to white by default
                    info.material_colour = AMS_TRAY_DEFAULT_COL;
                }

                if (MachineObject::is_bbl_filament(it->second->tag_uid)) {
                    info.material_state = AMSCanType::AMS_CAN_TYPE_BRAND;
                } else {
                    info.material_state = AMSCanType::AMS_CAN_TYPE_THIRDBRAND;
                }
            } else {
                info.can_id = it->second->id;
                info.material_name = "";
                info.material_colour = AMS_TRAY_DEFAULT_COL;
                info.material_state = AMSCanType::AMS_CAN_TYPE_THIRDBRAND;
                wxColour(255, 255, 255);
            }
        } else {
            info.can_id         = i;
            info.material_state = AMSCanType::AMS_CAN_TYPE_EMPTY;
        }
        cans.push_back(info);
    }
    return true;
}

/*************************************************
Description:AMSrefresh
**************************************************/

AMSrefresh::AMSrefresh() { SetFont(Label::Body_10); }

AMSrefresh::AMSrefresh(wxWindow *parent, wxWindowID id, wxString number, Caninfo info, const wxPoint &pos, const wxSize &size) : AMSrefresh()
{
    m_info = info;
    m_text = number;
    create(parent, id, pos, size);
}

AMSrefresh::AMSrefresh(wxWindow *parent, wxWindowID id, int number, Caninfo info, const wxPoint &pos, const wxSize &size) : AMSrefresh()
{
    m_info = info;
    m_text = wxString::Format("%d", number);
    create(parent, id, pos, size);
}

 AMSrefresh::~AMSrefresh() 
 {
     if (m_playing_timer) {
         m_playing_timer->Stop();
         delete m_playing_timer;
         m_playing_timer = nullptr;
     }
 }

void AMSrefresh::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    wxWindow::Create(parent, id, pos, size, wxBORDER_NONE);
    SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
   
    Bind(wxEVT_TIMER, &AMSrefresh::on_timer, this);
    Bind(wxEVT_PAINT, &AMSrefresh::paintEvent, this);
    Bind(wxEVT_ENTER_WINDOW, &AMSrefresh::OnEnterWindow, this);
    Bind(wxEVT_LEAVE_WINDOW, &AMSrefresh::OnLeaveWindow, this);
    Bind(wxEVT_LEFT_DOWN, &AMSrefresh::OnClick, this);

    m_bitmap_normal   = ScalableBitmap(this, "ams_refresh_normal", 30);
    m_bitmap_selected = ScalableBitmap(this, "ams_refresh_selected", 30);

    m_bitmap_ams_rfid_0 = ScalableBitmap(this, "ams_rfid_0", 30);
    m_bitmap_ams_rfid_1 = ScalableBitmap(this, "ams_rfid_1", 30);
    m_bitmap_ams_rfid_2 = ScalableBitmap(this, "ams_rfid_2", 30);
    m_bitmap_ams_rfid_3 = ScalableBitmap(this, "ams_rfid_3", 30);
    m_bitmap_ams_rfid_4 = ScalableBitmap(this, "ams_rfid_4", 30);
    m_bitmap_ams_rfid_5 = ScalableBitmap(this, "ams_rfid_5", 30);
    m_bitmap_ams_rfid_6 = ScalableBitmap(this, "ams_rfid_6", 30);
    m_bitmap_ams_rfid_7 = ScalableBitmap(this, "ams_rfid_7", 30);

    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_0);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_1);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_2);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_3);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_4);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_5);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_6);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_7);

    m_playing_timer = new wxTimer();
    m_playing_timer->SetOwner(this);
    wxPostEvent(this, wxTimerEvent());

    SetSize(AMS_REFRESH_SIZE);
    SetMinSize(AMS_REFRESH_SIZE);
    SetMaxSize(AMS_REFRESH_SIZE);
}

void AMSrefresh::on_timer(wxTimerEvent &event) 
{
    //if (m_rotation_angle >= m_rfid_bitmap_list.size()) {
    //    m_rotation_angle = 0;
    //} else {
    //    m_rotation_angle++;
    //}
    Refresh();
}

void AMSrefresh::PlayLoading()
{
    if (m_play_loading)  return;
    m_play_loading = true;
    //m_rotation_angle = 0;
    m_playing_timer->Start(AMS_REFRESH_PLAY_LOADING_TIMER);
    Refresh();
}

void AMSrefresh::StopLoading()
{
    if (!m_play_loading) return;
    m_playing_timer->Stop();
    m_play_loading = false;
    Refresh();
}

void AMSrefresh::OnEnterWindow(wxMouseEvent &evt)
{
    m_selected = true;
    Refresh();
}

void AMSrefresh::OnLeaveWindow(wxMouseEvent &evt)
{
    m_selected = false;
    Refresh();
}

void AMSrefresh::OnClick(wxMouseEvent &evt) {
    post_event(wxCommandEvent(EVT_AMS_REFRESH_RFID)); 
}

void AMSrefresh::post_event(wxCommandEvent &&event)
{
    event.SetString(m_info.can_id);
    event.SetEventObject(m_parent);
    wxPostEvent(m_parent, event);
    event.Skip();
}

void AMSrefresh::paintEvent(wxPaintEvent &evt)
{
    wxSize    size = GetSize();
    wxPaintDC dc(this);

    auto colour = AMS_CONTROL_GRAY700;
    if (!wxWindow::IsEnabled()) { colour = AMS_CONTROL_GRAY500; }

    auto pot = wxPoint((size.x - m_bitmap_selected.GetBmpSize().x) / 2, (size.y - m_bitmap_selected.GetBmpSize().y) / 2);

    if (!m_play_loading) {
        dc.DrawBitmap(m_selected ? m_bitmap_selected.bmp() : m_bitmap_normal.bmp(), pot);
    } else {
        /* m_bitmap_rotation    = ScalableBitmap(this, "ams_refresh_normal", 30);
         auto           image = m_bitmap_rotation.bmp().ConvertToImage();
         wxPoint        offset;
         auto           loading_img = image.Rotate(m_rotation_angle, wxPoint(image.GetWidth() / 2, image.GetHeight() / 2), true, &offset);
         ScalableBitmap loading_bitmap;
         loading_bitmap.bmp() = wxBitmap(loading_img);
         dc.DrawBitmap(loading_bitmap.bmp(), offset.x , offset.y);*/
        m_rotation_angle++;
        if (m_rotation_angle >= m_rfid_bitmap_list.size()) {
            m_rotation_angle = 0;
        }
        if (m_rfid_bitmap_list.size() <= 0)return;
        dc.DrawBitmap(m_rfid_bitmap_list[m_rotation_angle].bmp(), pot);
    }

    dc.SetPen(wxPen(colour));
    dc.SetBrush(wxBrush(colour));
    dc.SetFont(Label::Body_12);
    dc.SetTextForeground(colour);
    auto tsize = dc.GetTextExtent(m_text);
    pot        = wxPoint((size.x - tsize.x) / 2, (size.y - tsize.y) / 2);
    dc.DrawText(m_text, pot);
}

void AMSrefresh::Update(Caninfo info)
{
    m_info = info;
    StopLoading();
}

void AMSrefresh::msw_rescale() {
    m_bitmap_normal     = ScalableBitmap(this, "ams_refresh_normal", 30);
    m_bitmap_selected   = ScalableBitmap(this, "ams_refresh_selected", 30);
    m_bitmap_ams_rfid_0 = ScalableBitmap(this, "ams_rfid_0", 30);
    m_bitmap_ams_rfid_1 = ScalableBitmap(this, "ams_rfid_1", 30);
    m_bitmap_ams_rfid_2 = ScalableBitmap(this, "ams_rfid_2", 30);
    m_bitmap_ams_rfid_3 = ScalableBitmap(this, "ams_rfid_3", 30);
    m_bitmap_ams_rfid_4 = ScalableBitmap(this, "ams_rfid_4", 30);
    m_bitmap_ams_rfid_5 = ScalableBitmap(this, "ams_rfid_5", 30);
    m_bitmap_ams_rfid_6 = ScalableBitmap(this, "ams_rfid_6", 30);
    m_bitmap_ams_rfid_7 = ScalableBitmap(this, "ams_rfid_7", 30);

    m_rfid_bitmap_list.clear();
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_0);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_1);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_2);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_3);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_4);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_5);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_6);
    m_rfid_bitmap_list.push_back(m_bitmap_ams_rfid_7);
}

void AMSrefresh::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
}

/*************************************************
Description:AMSextruder
**************************************************/
void AMSextruderImage::TurnOn(wxColour col) 
{
    m_colour  = col;
    Refresh();
}

void AMSextruderImage::TurnOff() 
{
    m_colour = AMS_EXTRUDER_DEF_COLOUR;
    Refresh();
}

void AMSextruderImage::msw_rescale() 
{
    //m_ams_extruder.SetSize(AMS_EXTRUDER_BITMAP_SIZE);
    //auto image     = m_ams_extruder.ConvertToImage();
    m_ams_extruder = ScalableBitmap(this, "monitor_ams_extruder", 55);
    Refresh();
}

void AMSextruderImage::paintEvent(wxPaintEvent &evt) 
{
    wxPaintDC dc(this);
    render(dc);
}

void AMSextruderImage::render(wxDC &dc)
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

void AMSextruderImage::doRender(wxDC &dc) 
{ 
    auto size = GetSize();
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(m_colour);
    dc.DrawRectangle(0, 0, size.x, size.y - FromDIP(5));
    dc.DrawBitmap(m_ams_extruder.bmp(), wxPoint((size.x - m_ams_extruder.GetBmpSize().x) / 2, (size.y - m_ams_extruder.GetBmpSize().y) / 2));
}


AMSextruderImage::AMSextruderImage(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size) 
{
    wxWindow::Create(parent, id, pos, AMS_EXTRUDER_BITMAP_SIZE); 
    SetBackgroundColour(*wxWHITE);

    m_ams_extruder = ScalableBitmap(this, "monitor_ams_extruder",55);

    SetSize(AMS_EXTRUDER_BITMAP_SIZE);
    SetMinSize(AMS_EXTRUDER_BITMAP_SIZE);
    SetMaxSize(AMS_EXTRUDER_BITMAP_SIZE);


    Bind(wxEVT_PAINT, &AMSextruderImage::paintEvent, this);
}

AMSextruderImage::~AMSextruderImage() {}



AMSextruder::AMSextruder(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size) { create(parent, id, pos, size); }

 AMSextruder::~AMSextruder() {}

void AMSextruder::TurnOn(wxColour col)
{ 
    m_amsSextruder->TurnOn(col);
}

void AMSextruder::TurnOff()
{
    m_amsSextruder->TurnOff();
}

void AMSextruder::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    wxWindow::Create(parent, id, pos, AMS_EXTRUDER_SIZE, wxBORDER_NONE);
    SetBackgroundColour(AMS_CONTROL_WHITE_COLOUR);

    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);

    m_bitmap_panel = new wxPanel(this, wxID_ANY, wxDefaultPosition, AMS_EXTRUDER_BITMAP_SIZE, wxTAB_TRAVERSAL);
    m_bitmap_panel->SetBackgroundColour(wxColour(AMS_EXTRUDER_DEF_COLOUR));
    m_bitmap_panel->SetDoubleBuffered(true);
    m_bitmap_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_amsSextruder = new AMSextruderImage(m_bitmap_panel, wxID_ANY, wxDefaultPosition, AMS_EXTRUDER_BITMAP_SIZE);
    m_bitmap_sizer->Add(m_amsSextruder, 0, wxALIGN_CENTER, 0);

    m_bitmap_panel->SetSizer(m_bitmap_sizer);
    m_bitmap_panel->Layout();
    m_sizer_body->Add(m_bitmap_panel, 0, wxALIGN_CENTER, 0);

    SetSizer(m_sizer_body);
    Layout();
}

void AMSextruder::msw_rescale()
{
    m_amsSextruder->msw_rescale();
    Layout();
    Update();
    Refresh();
}

/*************************************************
Description:AMSLib
**************************************************/
AMSLib::AMSLib(wxWindow *parent, wxWindowID id, Caninfo info, const wxPoint &pos, const wxSize &size)
{
    m_border_color   = (wxColour(130, 130, 128));
    m_road_def_color = AMS_CONTROL_GRAY500;
    wxWindow::SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    create(parent, id, pos, size);

    Bind(wxEVT_PAINT, &AMSLib::paintEvent, this);
    Bind(wxEVT_ENTER_WINDOW, &AMSLib::on_enter_window, this);
    Bind(wxEVT_LEAVE_WINDOW, &AMSLib::on_leave_window, this);
    Bind(wxEVT_LEFT_DOWN, &AMSLib::on_left_down, this);

    Update(info, false);
}

void AMSLib::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    wxWindow::Create(parent, id, pos, size);

    SetSize(AMS_CAN_LIB_SIZE);
    SetMinSize(AMS_CAN_LIB_SIZE);
    SetMaxSize(AMS_CAN_LIB_SIZE);

    auto m_sizer_body = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *m_sizer_edit = new wxBoxSizer(wxHORIZONTAL);

    m_bitmap_editable       = ScalableBitmap(this, "ams_editable", 14);
    m_bitmap_editable_light = ScalableBitmap(this, "ams_editable_light", 14);
    m_bitmap_readonly       = ScalableBitmap(this, "ams_readonly", 14);
    m_bitmap_readonly_light = ScalableBitmap(this, "ams_readonly_light", 14);

    m_sizer_body->Add(0, 0, 1, wxEXPAND, 0);
    m_sizer_body->Add(m_sizer_edit, 0, wxALIGN_CENTER, 0);
    m_sizer_body->Add(0, 0, 0, wxBOTTOM, GetSize().y * 0.12);
    SetSizer(m_sizer_body);
    Layout();
}

void AMSLib::on_enter_window(wxMouseEvent &evt)
{ 
    m_hover = true;
    Refresh();
}

void AMSLib::on_leave_window(wxMouseEvent &evt)
{ 
    m_hover = false;
    Refresh();
}

void AMSLib::on_left_down(wxMouseEvent &evt)
{
    //dc.DrawBitmap(temp_bitmap, (size.x - m_bitmap_editable.GetSize().x) / 2, ( size.y - FromDIP(10) - temp_bitmap.GetSize().y) );
    if (m_info.material_state != AMSCanType::AMS_CAN_TYPE_EMPTY && m_info.material_state != AMSCanType::AMS_CAN_TYPE_NONE) {
        auto size = GetSize();
        auto pos  = evt.GetPosition();
        if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_THIRDBRAND || m_info.material_state == AMSCanType::AMS_CAN_TYPE_BRAND) {
            auto left   = FromDIP(20);
            auto top    = (size.y - FromDIP(10) - m_bitmap_editable_light.GetBmpSize().y);
            auto right  = size.x - FromDIP(20);
            auto bottom = size.y - FromDIP(10);

            if (pos.x >= left && pos.x <= right && pos.y >= top && top <= bottom) {
                post_event(wxCommandEvent(EVT_AMS_ON_FILAMENT_EDIT)); 
            }
        }
    }
}


void AMSLib::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this); 
    render(dc);
}

void AMSLib::render(wxDC &dc)
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

    // text
    auto tmp_lib_colour  = m_info.material_colour;
    auto temp_text_colour = AMS_CONTROL_GRAY800;

    if (tmp_lib_colour.GetLuminance() < 0.5) {
        temp_text_colour = AMS_CONTROL_WHITE_COLOUR;
    } else {
        temp_text_colour = AMS_CONTROL_GRAY800;
    }

    //if (!wxWindow::IsEnabled()) {
        //temp_text_colour = AMS_CONTROL_DISABLE_TEXT_COLOUR;
    //}

    dc.SetFont(::Label::Body_13);
    dc.SetTextForeground(temp_text_colour);

    auto libsize = GetSize();
    if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_THIRDBRAND || m_info.material_state == AMSCanType::AMS_CAN_TYPE_BRAND) {
        if (m_info.material_name.empty()) {
            auto tsize = dc.GetMultiLineTextExtent("?");
            auto pot   = wxPoint((libsize.x - tsize.x) / 2, (libsize.y - tsize.y) / 2 + FromDIP(3));
            dc.DrawText(L("?"), pot);
        } else {
            auto tsize = dc.GetMultiLineTextExtent(m_info.material_name);

            if (m_info.material_name.find(' ') != std::string::npos) {
                dc.SetFont(::Label::Body_12);

                auto line_top    = m_info.material_name.substr(0, m_info.material_name.find(' '));
                auto line_bottom = m_info.material_name.substr(m_info.material_name.find(' '));

                auto line_top_tsize    = dc.GetMultiLineTextExtent(line_top);
                auto line_bottom_tsize = dc.GetMultiLineTextExtent(line_bottom);

                auto pot_top = wxPoint((libsize.x - line_top_tsize.x) / 2, (libsize.y - line_top_tsize.y) / 2 - line_top_tsize.y + FromDIP(6));
                dc.DrawText(line_top, pot_top);

                auto pot_bottom = wxPoint((libsize.x - line_bottom_tsize.x) / 2, (libsize.y - line_bottom_tsize.y) / 2 + FromDIP(6));
                dc.DrawText(line_bottom, pot_bottom);

            } else {
                auto pot = wxPoint((libsize.x - tsize.x) / 2, (libsize.y - tsize.y) / 2 + FromDIP(3));
                dc.DrawText(m_info.material_name, pot);
            }
        }
    }

    if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_EMPTY) {
        auto tsize = dc.GetMultiLineTextExtent(_L("Empty"));
        auto pot   = wxPoint((libsize.x - tsize.x) / 2, (libsize.y - tsize.y) / 2 + FromDIP(3));
        dc.DrawText(_L("Empty"), pot);
    }
}

void AMSLib::doRender(wxDC &dc)
{
    wxSize size             = GetSize();
    auto   tmp_lib_colour   = m_info.material_colour;
    auto   temp_bitmap_third      = m_bitmap_editable_light;
    auto   temp_bitmap_brand      = m_bitmap_readonly_light;

    if (tmp_lib_colour.GetLuminance() < 0.5) {
        temp_bitmap_third = m_bitmap_editable_light;
        temp_bitmap_brand = m_bitmap_readonly_light;
    } else {
        temp_bitmap_third = m_bitmap_editable;
        temp_bitmap_brand = m_bitmap_readonly;
    }

    //if (!wxWindow::IsEnabled()) {
        //tmp_lib_colour   = AMS_CONTROL_DISABLE_COLOUR;
    //}

    // selected
    if (m_selected) {
        // lib
        dc.SetPen(wxPen(tmp_lib_colour, 2, wxSOLID));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, size.x, size.y);
        } else {
            dc.DrawRoundedRectangle(FromDIP(1), FromDIP(1), size.x - FromDIP(1), size.y - FromDIP(1), m_radius);
        }

        dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
        dc.SetBrush(wxBrush(tmp_lib_colour));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, size.x, size.y);
        } else {
            dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4), size.x - FromDIP(8), size.y - FromDIP(8), m_radius);
        }
    }
    
    if (!m_selected && m_hover) {
        dc.SetPen(wxPen(AMS_CONTROL_BRAND_COLOUR, 2, wxSOLID));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, size.x, size.y);
        } else {
            dc.DrawRoundedRectangle(FromDIP(1), FromDIP(1), size.x - FromDIP(1), size.y - FromDIP(1), m_radius);
        }

        dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
        dc.SetBrush(wxBrush(tmp_lib_colour));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, size.x, size.y);
        } else {
            dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4), size.x - FromDIP(8), size.y - FromDIP(8), m_radius);
        }
    } else {
        dc.SetPen(wxPen(tmp_lib_colour, 1, wxSOLID));
        dc.SetBrush(wxBrush(tmp_lib_colour));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, size.x, size.y);
        } else {
            dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4), size.x - FromDIP(8), size.y - FromDIP(8), m_radius);
        }
    }
    
    // edit icon
    if (m_info.material_state != AMSCanType::AMS_CAN_TYPE_EMPTY && m_info.material_state != AMSCanType::AMS_CAN_TYPE_NONE)
    {
        if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_THIRDBRAND)
            dc.DrawBitmap(temp_bitmap_third.bmp(), (size.x - temp_bitmap_third.GetBmpSize().x) / 2, (size.y - FromDIP(10) - temp_bitmap_third.GetBmpSize().y));
        if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_BRAND)
            dc.DrawBitmap(temp_bitmap_brand.bmp(), (size.x - temp_bitmap_brand.GetBmpSize().x) / 2, (size.y - FromDIP(10) - temp_bitmap_brand.GetBmpSize().y));
    }
}

void AMSLib::Update(Caninfo info, bool refresh)
{
    m_info = info;
    Layout();
    if (refresh) Refresh();
}

wxColour AMSLib::GetLibColour() { return m_info.material_colour; }

void AMSLib::OnSelected()
{
    if (!wxWindow::IsEnabled()) return;
    if (m_unable_selected) return;

    post_event(wxCommandEvent(EVT_AMS_ON_SELECTED));
    m_selected = true;
    Refresh();
}

void AMSLib::post_event(wxCommandEvent &&event)
{
    event.SetString(m_info.can_id);
    event.SetEventObject(m_parent);
    wxPostEvent(m_parent, event);
    event.Skip();
}

void AMSLib::UnSelected()
{
    m_selected = false;
    Refresh();
}

bool AMSLib::Enable(bool enable) { return wxWindow::Enable(enable); }

/*************************************************
Description:AMSRoad
**************************************************/
AMSRoad::AMSRoad() : m_road_def_color(AMS_CONTROL_GRAY500), m_road_color(AMS_CONTROL_GRAY500) {}
AMSRoad::AMSRoad(wxWindow *parent, wxWindowID id, Caninfo info, int canindex, int maxcan, const wxPoint &pos, const wxSize &size) : AMSRoad()
{
    m_info     = info;
    m_canindex = canindex;
    // road type
    auto mode = AMSRoadMode::AMS_ROAD_MODE_END;
    if (m_canindex == 0 && maxcan == 1) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_NONE;
    } else if (m_canindex == 0 && maxcan > 1) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_END;
    } else if (m_canindex < (maxcan - 1)) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_LEFT_RIGHT;
    } else if (m_canindex == (maxcan - 1)) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_LEFT;
    }

    create(parent, id, pos, size);
    Bind(wxEVT_PAINT, &AMSRoad::paintEvent, this);
    wxWindow::SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
}

void AMSRoad::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size) { wxWindow::Create(parent, id, pos, size); }

void AMSRoad::Update(Caninfo info, int canindex, int maxcan)
{
    m_info     = info;
    m_canindex = canindex;
    if (m_canindex == 0 && maxcan == 1) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_END_ONLY;
    } else if (m_canindex == 0 && maxcan > 1) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_END;
    } else if (m_canindex < (maxcan - 1)) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_LEFT_RIGHT;
    } else if (m_canindex == (maxcan - 1)) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_LEFT;
    }
    m_pass_rode_mode.push_back(AMSPassRoadMode::AMS_ROAD_MODE_NONE);
    Refresh();
}

void AMSRoad::SetPassRoadColour(wxColour col) { m_road_color = col; }

void AMSRoad::SetMode(AMSRoadMode mode)
{
    m_rode_mode = mode;
    Refresh();
}

void AMSRoad::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AMSRoad::render(wxDC &dc)
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

void AMSRoad::doRender(wxDC &dc)
{
    wxSize size = GetSize();

    dc.SetPen(wxPen(m_road_def_color, 2, wxSOLID));
    dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
    // left mode
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_LEFT) { dc.DrawRoundedRectangle(-10, -10, size.x / 2 + 10, size.y * 0.6 + 10, 4); }

    // left right mode
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_LEFT_RIGHT) {
        dc.DrawLine(size.x / 2, -1, size.x / 2, size.y * 0.6 - 1);
        dc.DrawLine(0, size.y * 0.6 - 1, size.x, size.y * 0.6 - 1);
    }

    // end mode
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END) {
        dc.SetBrush(wxBrush(m_road_def_color));
        dc.DrawLine(size.x / 2, -1, size.x / 2, size.y * 0.6 - 1);
        dc.DrawLine(size.x / 2, size.y * 0.6, size.x / 2, size.y);
        dc.DrawLine(size.x / 2, size.y * 0.6 - 1, size.x, size.y * 0.6 - 1);
    }

    // end mode only
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END_ONLY) {
        dc.SetBrush(wxBrush(m_road_def_color));
        dc.DrawLine(size.x / 2, -1, size.x / 2, size.y * 0.6 - 1);
        dc.DrawLine(size.x / 2, size.y * 0.6, size.x / 2, size.y);
    }

    // end none
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_NONE) {
        dc.SetBrush(wxBrush(m_road_def_color));
        dc.DrawLine(size.x / 2, -1, size.x / 2, size.y * 0.6 - 1);
        dc.DrawLine(size.x / 2, size.y * 0.6, size.x / 2, size.y);
        // dc.DrawLine(size.x / 2, size.y * 0.6 - 1, size.x, size.y * 0.6 - 1);
    }

    // mode none
    // if (m_pass_rode_mode.size() == 1 && m_pass_rode_mode[0] == AMSPassRoadMode::AMS_ROAD_MODE_NONE) return;

    dc.SetPen(wxPen(m_road_color, m_passroad_width, wxSOLID));
    dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));

    // left pass mode
    for (auto pass_mode : m_pass_rode_mode) {
        switch (pass_mode) {
        case AMSPassRoadMode::AMS_ROAD_MODE_LEFT: dc.DrawRoundedRectangle(-10, -10, size.x / 2 + 10, size.y * 0.6 + 10, 4); break;

        case AMSPassRoadMode::AMS_ROAD_MODE_LEFT_RIGHT: dc.DrawLine(0, size.y * 0.6 - 1, size.x, size.y * 0.6 - 1); break;

        case AMSPassRoadMode::AMS_ROAD_MODE_END_TOP: dc.DrawLine(size.x / 2, -1, size.x / 2, size.y * 0.6 - 1); break;

        case AMSPassRoadMode::AMS_ROAD_MODE_END_BOTTOM: dc.DrawLine(size.x / 2, size.y * 0.6, size.x / 2, size.y); break;

        case AMSPassRoadMode::AMS_ROAD_MODE_END_RIGHT: dc.DrawLine(size.x / 2, size.y * 0.6 - 1, size.x, size.y * 0.6 - 1); break;

        default: break;
        }
    }

    // end mode
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END || m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END_ONLY) {
        dc.SetPen(wxPen(m_road_def_color, 2, wxSOLID));
        dc.SetBrush(wxBrush(m_road_def_color));
        dc.DrawRoundedRectangle(size.x * 0.37 / 2, size.y * 0.6 - size.y / 6, size.x * 0.63, size.y / 3, m_radius);
    }
}

void AMSRoad::UpdatePassRoad(int tag_index, AMSPassRoadType type, AMSPassRoadSTEP step) {}

void AMSRoad::OnPassRoad(std::vector<AMSPassRoadMode> prord_list)
{
    // AMS_ROAD_MODE_NONE, AMS_ROAD_MODE_LEFT, AMS_ROAD_MODE_LEFT_RIGHT, AMS_ROAD_MODE_END_TOP, AMS_ROAD_MODE_END_BOTTOM, AMS_ROAD_MODE_END_RIGHT,
    // AMS_ROAD_MODE_LEFT, AMS_ROAD_MODE_LEFT_RIGHT, AMS_ROAD_MODE_END,

    m_pass_rode_mode.clear();
    auto left_types       = std::vector<AMSPassRoadMode>{AMSPassRoadMode::AMS_ROAD_MODE_NONE, AMSPassRoadMode::AMS_ROAD_MODE_LEFT};
    auto left_right_types = std::vector<AMSPassRoadMode>{AMSPassRoadMode::AMS_ROAD_MODE_NONE, AMSPassRoadMode::AMS_ROAD_MODE_LEFT, AMSPassRoadMode::AMS_ROAD_MODE_LEFT_RIGHT};
    auto end_types        = std::vector<AMSPassRoadMode>{AMSPassRoadMode::AMS_ROAD_MODE_NONE, AMSPassRoadMode::AMS_ROAD_MODE_END_TOP, AMSPassRoadMode::AMS_ROAD_MODE_END_BOTTOM,
                                                  AMSPassRoadMode::AMS_ROAD_MODE_END_RIGHT};

    // left
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_LEFT) {
        for (auto i = 0; i < prord_list.size(); i++) {
            std::vector<AMSPassRoadMode>::iterator iter = std::find(left_types.begin(), left_types.end(), prord_list[i]);
            if (iter != left_types.end()) m_pass_rode_mode.push_back(prord_list[i]);

            if (prord_list[i] == AMSPassRoadMode::AMS_ROAD_MODE_NONE) {
                m_pass_rode_mode = std::vector<AMSPassRoadMode>{AMSPassRoadMode::AMS_ROAD_MODE_NONE};
                break;
            }
        }
    }

    // left right
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_LEFT_RIGHT) {
        for (auto i = 0; i < prord_list.size(); i++) {
            std::vector<AMSPassRoadMode>::iterator iter = std::find(left_right_types.begin(), left_right_types.end(), prord_list[i]);
            if (iter != left_right_types.end()) m_pass_rode_mode.push_back(prord_list[i]);

            if (prord_list[i] == AMSPassRoadMode::AMS_ROAD_MODE_NONE) {
                m_pass_rode_mode = std::vector<AMSPassRoadMode>{AMSPassRoadMode::AMS_ROAD_MODE_NONE};
                break;
            }
        }
    }

    // left end
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END || m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END_ONLY) {
        for (auto i = 0; i < prord_list.size(); i++) {
            std::vector<AMSPassRoadMode>::iterator iter = std::find(end_types.begin(), end_types.end(), prord_list[i]);
            if (iter != end_types.end()) m_pass_rode_mode.push_back(prord_list[i]);

            if (prord_list[i] == AMSPassRoadMode::AMS_ROAD_MODE_NONE) {
                m_pass_rode_mode = std::vector<AMSPassRoadMode>{AMSPassRoadMode::AMS_ROAD_MODE_NONE};
                break;
            }
        }
    }

    //Refresh();
}

/*************************************************
Description:AMSControl
**************************************************/
AMSItem::AMSItem() {}

AMSItem::AMSItem(wxWindow *parent, wxWindowID id, AMSinfo amsinfo, const wxSize cube_size, const wxPoint &pos, const wxSize &size) : AMSItem()
{
    m_amsinfo   = amsinfo;
    m_cube_size = cube_size;
    create(parent, id, pos, size);
    Bind(wxEVT_PAINT, &AMSItem::paintEvent, this);
    Bind(wxEVT_ENTER_WINDOW, &AMSItem::OnEnterWindow, this);
    Bind(wxEVT_LEAVE_WINDOW, &AMSItem::OnLeaveWindow, this);
    // Bind(wxEVT_LEFT_DOWN, &AMSItem::OnSelected, this);
}

void AMSItem::Open()
{
    m_open = true;
    Show();
}

void AMSItem::Close()
{
    m_open = false;
    Hide();
}

void AMSItem::Update(AMSinfo amsinfo)
{
    m_amsinfo = amsinfo;
}

void AMSItem::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    wxWindow::Create(parent, id, pos, size);
    SetBackgroundColour(AMS_CONTROL_WHITE_COLOUR);
    HideHumidity();
    Refresh();
}

void AMSItem::OnEnterWindow(wxMouseEvent &evt)
{
    // m_hover = true;
    // Refresh();
}

void AMSItem::OnLeaveWindow(wxMouseEvent &evt)
{
    // m_hover = false;
    // Refresh();
}

void AMSItem::OnSelected()
{
    if (!wxWindow::IsEnabled()) { return; }
    m_selected = true;
    Refresh();
}

void AMSItem::UnSelected()
{
    m_selected = false;
    Refresh();
}

void AMSItem::ShowHumidity()
{
    m_show_humidity = true;
    SetSize(AMS_ITEM_HUMIDITY_SIZE);
    SetMinSize(AMS_ITEM_HUMIDITY_SIZE);
    Refresh();
}

void AMSItem::HideHumidity()
{
    m_show_humidity = false;
    SetSize(AMS_ITEM_SIZE);
    SetMinSize(AMS_ITEM_SIZE);
    Refresh();
}

void AMSItem::SetHumidity(int humidity)
{
    m_humidity = humidity;
    Refresh();
}

bool AMSItem::Enable(bool enable) { return wxWindow::Enable(enable); }

void AMSItem::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AMSItem::render(wxDC &dc)
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

void AMSItem::doRender(wxDC &dc)
{
    wxSize size = GetSize();
    dc.SetPen(wxPen(m_background_colour));
    dc.SetBrush(wxBrush(m_background_colour));
    dc.DrawRoundedRectangle(0, 0, size.x, size.y, 3);

    auto left = m_padding;
    for (std::vector<Caninfo>::iterator iter = m_amsinfo.cans.begin(); iter != m_amsinfo.cans.end(); iter++) {
        dc.SetPen(wxPen(*wxTRANSPARENT_PEN));

        if (wxWindow::IsEnabled()) {
            dc.SetBrush(wxBrush(iter->material_colour));
        } else {
            dc.SetBrush(AMS_CONTROL_DISABLE_COLOUR);
        }

        dc.DrawRoundedRectangle(left, (size.y - AMS_ITEM_CUBE_SIZE.y) / 2, AMS_ITEM_CUBE_SIZE.x, AMS_ITEM_CUBE_SIZE.y, 2);
        left += AMS_ITEM_CUBE_SIZE.x;
        left += m_space;
    }

    m_show_humidity = false;
    if (m_show_humidity) {
        left = 4 * AMS_ITEM_CUBE_SIZE.x + 6 * m_space;
        dc.SetPen(wxPen(AMS_CONTROL_GRAY500, 1));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        dc.DrawLine(left, (size.y - AMS_ITEM_CUBE_SIZE.y) / 2, left, ((size.y - AMS_ITEM_CUBE_SIZE.y) / 2) + AMS_ITEM_CUBE_SIZE.y);

        left += m_space;
        dc.SetFont(::Label::Body_13);
        dc.SetTextForeground(AMS_CONTROL_GRAY800);
        auto tsize = dc.GetTextExtent("00% RH");
        auto text  = wxString::Format("%d%% RH", m_humidity);
        dc.DrawText(text, wxPoint(left, (size.y - tsize.y) / 2));
    }

    auto border_colour = AMS_CONTROL_BRAND_COLOUR;
    if (!wxWindow::IsEnabled()) { border_colour = AMS_CONTROL_DISABLE_COLOUR; }

    if (m_hover) {
        dc.SetPen(wxPen(border_colour, 1));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, 3);
    }

    if (m_selected) {
        dc.SetPen(wxPen(border_colour, 1));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        dc.DrawRoundedRectangle(0, 0, size.x, size.y, 3);
    }
}

void AMSItem::DoSetSize(int x, int y, int width, int height, int sizeFlags /*= wxSIZE_AUTO*/) { wxWindow::DoSetSize(x, y, width, height, sizeFlags); }

/*************************************************
Description:AmsCan
**************************************************/

AmsCans::AmsCans() {}

AmsCans::AmsCans(wxWindow *parent, wxWindowID id, AMSinfo info, const wxPoint &pos, const wxSize &size) : AmsCans()
{
    wxWindow::Create(parent, wxID_ANY, pos, AMS_CANS_WINDOW_SIZE);
    create(parent, id, info, pos, size);
}

void AmsCans::create(wxWindow *parent, wxWindowID id, AMSinfo info, const wxPoint &pos, const wxSize &size)
{
    sizer_can = new wxBoxSizer(wxHORIZONTAL);
    m_info    = info;

    Freeze();
    for (auto it = m_info.cans.begin(); it != m_info.cans.end(); it++) {
        AddCan(*it, m_can_count, m_info.cans.size());
        m_can_count++;
    }

    SetSizer(sizer_can);
    Layout();
    Fit();
    Thaw();
}

void AmsCans::Update(AMSinfo info)
{
    m_info      = info;
    m_can_count = info.cans.size();

    for (auto i = 0; i < m_can_refresh_list.GetCount(); i++) {
        Canrefreshs *refresh = m_can_refresh_list[i];
        if (i < m_can_count) {
            refresh->canrefresh->Update(info.cans[i]);
            refresh->canrefresh->Show();
        } else {
            refresh->canrefresh->Hide();
        }
    }
   
    for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
        CanLibs *lib = m_can_lib_list[i];
        if (i < m_can_count) {
            lib->canLib->Update(info.cans[i]);
            lib->canLib->Show();
        } else {
            lib->canLib->Hide();
        }
    }
    for (auto i = 0; i < m_can_road_list.GetCount(); i++) {
        CanRoads *road = m_can_road_list[i];
        if (i < m_can_count) {
            road->canRoad->Update(info.cans[i], i, m_can_count);
            road->canRoad->Show();
        } else {
            road->canRoad->Hide();
        }
    }

    Layout();
}

void AmsCans::AddCan(Caninfo caninfo, int canindex, int maxcan)
{
    auto        amscan      = new wxWindow(this, wxID_ANY);
    wxBoxSizer *m_sizer_ams = new wxBoxSizer(wxVERTICAL);
    m_sizer_ams->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));
    auto m_panel_refresh = new AMSrefresh(amscan, wxID_ANY, m_can_count + 1, caninfo);
    m_sizer_ams->Add(m_panel_refresh, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_sizer_ams->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(2));
    auto m_panel_lib = new AMSLib(amscan, wxID_ANY, caninfo);
    m_panel_lib->Bind(wxEVT_LEFT_DOWN, [this, canindex](wxMouseEvent &ev) {
        m_canlib_selection = canindex;
        // m_canlib_id        = caninfo.can_id;

        for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
            CanLibs *lib = m_can_lib_list[i];
            if (lib->canLib->m_can_index == m_canlib_selection) {
                lib->canLib->OnSelected();
            } else {
                lib->canLib->UnSelected();
            }
        }
        ev.Skip();
    });

    m_panel_lib->m_info.can_id = caninfo.can_id;
    m_panel_lib->m_can_index   = canindex;
    auto m_panel_road          = new AMSRoad(amscan, wxID_ANY, caninfo, canindex, maxcan, wxDefaultPosition, AMS_CAN_ROAD_SIZE);
    m_sizer_ams->Add(m_panel_lib, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, FromDIP(4));
    m_sizer_ams->Add(m_panel_road, 0, wxALL, 0);

    amscan->SetSizer(m_sizer_ams);
    amscan->Layout();
    amscan->Fit();
    sizer_can->Add(amscan, 0, wxALL, 0);

    Canrefreshs *canrefresh = new Canrefreshs;
    canrefresh->canID       = caninfo.can_id;
    canrefresh->canrefresh  = m_panel_refresh;
    m_can_refresh_list.Add(canrefresh);

    CanLibs *canlib = new CanLibs;
    canlib->canID   = caninfo.can_id;
    canlib->canLib  = m_panel_lib;
    m_can_lib_list.Add(canlib);

    CanRoads *canroad = new CanRoads;
    canroad->canID    = caninfo.can_id;
    canroad->canRoad  = m_panel_road;
    m_can_road_list.Add(canroad);
}

void AmsCans::SelectCan(std::string canid)
{
    for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
        CanLibs *lib = m_can_lib_list[i];
        if (lib->canLib->m_info.can_id == canid) { m_canlib_selection = lib->canLib->m_can_index; }
    }

    m_canlib_id = canid;

    for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
        CanLibs *lib = m_can_lib_list[i];
        if (lib->canLib->m_info.can_id == m_canlib_id) {
            lib->canLib->OnSelected();
        } else {
            lib->canLib->UnSelected();
        }
    }
}

void AmsCans::SetAmsStep(wxString canid, AMSPassRoadType type, AMSPassRoadSTEP step)
{

    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
        for (auto i = 0; i < m_can_road_list.GetCount(); i++) {
            CanRoads *road = m_can_road_list[i];
            auto      pr   = std::vector<AMSPassRoadMode>{};
            pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_NONE);
            road->canRoad->OnPassRoad(pr);
        }

        return;
    }

    
    auto tag_can_index = -1;
    for (auto i = 0; i < m_can_road_list.GetCount(); i++) {
        CanRoads *road = m_can_road_list[i];
        if (canid == road->canRoad->m_info.can_id) { tag_can_index = road->canRoad->m_canindex; }
    }
    if (tag_can_index == -1) return;

    // get colour
    auto tag_colour = *wxWHITE;
    for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
        CanLibs *lib = m_can_lib_list[i];
        if (canid == lib->canLib->m_info.can_id) tag_colour = lib->canLib->GetLibColour();
    }

    // unload
    if (type == AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD) {
        for (auto i = 0; i < m_can_road_list.GetCount(); i++) {
            CanRoads *road = m_can_road_list[i];

            auto index = road->canRoad->m_canindex;
            auto pr    = std::vector<AMSPassRoadMode>{};

            pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_BOTTOM);
            if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_2) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_BOTTOM); }

            if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_3) {
                if (index == tag_can_index && index > 0) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_LEFT); }
                if (index < tag_can_index && index > 0) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_LEFT_RIGHT); }
                if (index == 0 && tag_can_index == index) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_TOP); }
                if (index == 0 && tag_can_index > index) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_RIGHT); }
            }

            road->canRoad->SetPassRoadColour(tag_colour);
            road->canRoad->OnPassRoad(pr);
        }
    }

    // load
    if (type == AMSPassRoadType::AMS_ROAD_TYPE_LOAD) {
        for (auto i = 0; i < m_can_road_list.GetCount(); i++) {
            CanRoads *road = m_can_road_list[i];

            auto index = road->canRoad->m_canindex;
            auto pr    = std::vector<AMSPassRoadMode>{};

            if (index == tag_can_index && index > 0) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_LEFT); }
            if (index < tag_can_index && index > 0) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_LEFT_RIGHT); }
            if (index == 0 && tag_can_index == index) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_TOP); }
            if (index == 0 && tag_can_index > index) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_RIGHT); }

            if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_2) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_BOTTOM); }

            road->canRoad->SetPassRoadColour(tag_colour);
            road->canRoad->OnPassRoad(pr);
        }
    }
}

void AmsCans::PlayRridLoading(wxString canid)
{
    for (auto i = 0; i < m_can_refresh_list.GetCount(); i++) {
        Canrefreshs *refresh = m_can_refresh_list[i];
        if (refresh->canrefresh->m_info.can_id == canid) { refresh->canrefresh->PlayLoading(); }
    }
}

std::string AmsCans::GetCurrentCan()
{
    if (m_canlib_selection > -1 && m_canlib_selection < m_can_lib_list.size()) {
        CanLibs *lib = m_can_lib_list[m_canlib_selection];
        return lib->canLib->m_info.can_id;
    }
    return "";
}

void AmsCans::StopRridLoading(wxString canid)
{
    for (auto i = 0; i < m_can_refresh_list.GetCount(); i++) {
        Canrefreshs *refresh = m_can_refresh_list[i];
        if (refresh->canrefresh->m_info.can_id == canid) { refresh->canrefresh->StopLoading(); }
    }
}

void AmsCans::msw_rescale() 
{
    for (auto i = 0; i < m_can_refresh_list.GetCount(); i++) {
        Canrefreshs *refresh = m_can_refresh_list[i];
        refresh->canrefresh->msw_rescale(); 
    }
}

//wxColour AmsCans::GetCanColour(wxString canid)
//{
//    wxColour col = *wxWHITE;
//    for (auto i = 0; i < m_can_lib_list.GetCount(); i++) {
//        CanLibs *lib = m_can_lib_list[i];
//        if (lib->canLib->m_info.can_id == canid) { col = lib->canLib->m_info.material_colour; }
//    }
//    return col;
//}

/*************************************************
Description:AMSControl
**************************************************/
// WX_DEFINE_OBJARRAY(AmsItemsHash);
AMSControl::AMSControl(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size) : wxSimplebook(parent, wxID_ANY, pos, size)
{
    SetBackgroundColour(*wxWHITE);
    // normal mode
    //Freeze();
    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);
    auto        amswin       = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(-1, AMS_CAN_ITEM_HEIGHT_SIZE));
    amswin->SetBackgroundColour(*wxWHITE);
    // top - ams tag
    m_simplebook_amsitems = new wxSimplebook(amswin, wxID_ANY);
    m_simplebook_amsitems->SetSize(wxSize(-1, AMS_CAN_ITEM_HEIGHT_SIZE));
    m_simplebook_amsitems->SetMinSize(wxSize(-1, AMS_CAN_ITEM_HEIGHT_SIZE));
    auto m_sizer_amsitems = new wxBoxSizer(wxHORIZONTAL);
    m_simplebook_amsitems->SetSizer(m_sizer_amsitems);
    m_simplebook_amsitems->Layout();
    m_sizer_amsitems->Fit(m_simplebook_amsitems);
    m_sizer_body->Add(m_simplebook_amsitems, 0, wxEXPAND, 0);

    m_panel_top = new wxPanel(m_simplebook_amsitems, wxID_ANY, wxDefaultPosition, wxSize(-1, AMS_CAN_ITEM_HEIGHT_SIZE));
    m_sizer_top = new wxBoxSizer(wxHORIZONTAL);
    m_panel_top->SetSizer(m_sizer_top);
    m_panel_top->Layout();
    m_sizer_top->Fit(m_panel_top);

    auto m_panel_top_empty = new wxPanel(m_simplebook_amsitems, wxID_ANY, wxDefaultPosition, wxSize(-1, AMS_CAN_ITEM_HEIGHT_SIZE));
    auto m_sizer_top_empty = new wxBoxSizer(wxHORIZONTAL);
    m_panel_top_empty->SetSizer(m_sizer_top_empty);
    m_panel_top_empty->Layout();
    m_sizer_top_empty->Fit(m_panel_top_empty);

    m_simplebook_amsitems->AddPage(m_panel_top, wxEmptyString, false);
    m_simplebook_amsitems->AddPage(m_panel_top_empty, wxEmptyString, false);

    m_sizer_body->Add(0, 0, 1, wxEXPAND | wxTOP, 18);

    wxBoxSizer *m_sizer_bottom = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_left   = new wxBoxSizer(wxVERTICAL);

    m_panel_can = new StaticBox(amswin, wxID_ANY, wxDefaultPosition, AMS_CANS_SIZE, wxBORDER_NONE);
    m_panel_can->SetMinSize(AMS_CANS_SIZE);
    m_panel_can->SetCornerRadius(10);
    m_panel_can->SetBackgroundColor(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    m_sizer_cans = new wxBoxSizer(wxHORIZONTAL);

    m_simplebook_ams = new wxSimplebook(m_panel_can, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_simplebook_ams->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_sizer_cans->Add(m_simplebook_ams, 0, wxLEFT | wxLEFT, FromDIP(10));

    // ams mode
    m_simplebook_cans = new wxSimplebook(m_simplebook_ams, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_simplebook_cans->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);

    // none ams mode
    m_none_ams_panel = new wxPanel(m_simplebook_ams, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_none_ams_panel->SetBackgroundColour(AMS_CONTROL_DEF_BLOCK_BK_COLOUR);
    m_none_ams_panel->SetDoubleBuffered(true);

    wxBoxSizer *sizer_ams_panel = new wxBoxSizer(wxHORIZONTAL);
    AMSinfo     none_ams        = AMSinfo{"0", std::vector<Caninfo>{Caninfo{"0", wxEmptyString, *wxWHITE, AMSCanType::AMS_CAN_TYPE_EMPTY}}};
    auto        amscans         = new AmsCans(m_none_ams_panel, wxID_ANY, none_ams);
    sizer_ams_panel->Add(amscans, 0, wxALL, 0);
    sizer_ams_panel->Add(0, 0, 0, wxLEFT, 20);
    auto m_tip_none_ams = new wxStaticText(m_none_ams_panel, wxID_ANY, _L("Click the pencil icon to edit the filament."), wxDefaultPosition, wxDefaultSize, 0);
    m_tip_none_ams->Wrap(150);
    m_tip_none_ams->SetFont(::Label::Body_13);
    m_tip_none_ams->SetForegroundColour(AMS_CONTROL_GRAY500);
    m_tip_none_ams->SetMinSize({150, -1});
    sizer_ams_panel->Add(m_tip_none_ams, 0, wxALIGN_CENTER, 0);
    m_none_ams_panel->SetSizer(sizer_ams_panel);
    m_none_ams_panel->Layout();

    m_simplebook_ams->AddPage(m_simplebook_cans, wxEmptyString, true);
    m_simplebook_ams->AddPage(m_none_ams_panel, wxEmptyString, false);

    m_panel_can->SetSizer(m_sizer_cans);
    m_panel_can->Layout();
    m_sizer_cans->Fit(m_panel_can);

    m_sizer_left->Add(m_panel_can, 1, wxEXPAND, 0);

    wxBoxSizer *m_sizer_left_bottom = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *sizer_sextruder     = new wxBoxSizer(wxVERTICAL);

    auto extruder_pane = new wxPanel(amswin, wxID_ANY, wxDefaultPosition, AMS_EXTRUDER_SIZE);

    extruder_pane->SetSizer(sizer_sextruder);
    extruder_pane->Layout();

    m_extruder = new AMSextruder(extruder_pane, wxID_ANY, wxDefaultPosition, AMS_EXTRUDER_SIZE);
    sizer_sextruder->Add(m_extruder, 0, wxALIGN_CENTER, 0);

    m_sizer_left_bottom->Add(extruder_pane, 0, wxLEFT, FromDIP(10));

    m_sizer_left_bottom->Add(0, 0, 0, wxEXPAND, 0);
    m_sizer_left_bottom->Add(0, 0, 0, wxALL | wxLEFT, FromDIP(26));

    StateColor btn_bg_green(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled),std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Pressed),
                           std::pair<wxColour, int>(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, StateColor::Hovered),
                           std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Normal));

    StateColor btn_bd_green(std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Enabled));
    StateColor btn_bd_white(std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Disabled), std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));
    StateColor btn_text_green(std::pair<wxColour, int>(*wxBLACK, StateColor::Disabled), std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Enabled));
    m_sizer_left_bottom->AddStretchSpacer();
    m_button_extruder_feed = new Button(amswin, _L("Load Filament"));
    m_button_extruder_feed->SetBackgroundColor(btn_bg_green);
    m_button_extruder_feed->SetBorderColor(btn_bd_green);
    m_button_extruder_feed->SetTextColor(btn_text_green);
    m_button_extruder_feed->SetFont(Label::Body_13);
    m_sizer_left_bottom->Add(m_button_extruder_feed, 0, wxTOP, FromDIP(0));
    m_sizer_left_bottom->Add(0, 0, 0, wxALL | wxLEFT, FromDIP(10));

    m_button_extruder_back = new Button(amswin, _L("Unload Filament"));
    m_button_extruder_back->SetBackgroundColor(btn_bg_white);
    m_button_extruder_back->SetBorderColor(btn_bd_white);
    m_button_extruder_back->SetFont(Label::Body_13);
    m_sizer_left_bottom->Add(m_button_extruder_back, 0, wxTOP, FromDIP(20));

    m_sizer_left->Add(m_sizer_left_bottom, 0, wxEXPAND, 0);
    m_sizer_bottom->Add(m_sizer_left, 0, wxEXPAND, 0);
    m_sizer_bottom->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(43));

    wxBoxSizer *m_sizer_right = new wxBoxSizer(wxVERTICAL);
    m_simplebook_right        = new wxSimplebook(amswin, wxID_ANY);
    m_simplebook_right->SetMinSize(AMS_STEP_SIZE);
    m_simplebook_right->SetSize(AMS_STEP_SIZE);
    m_simplebook_right->SetBackgroundColour(*wxWHITE);
    m_sizer_right->Add(m_simplebook_right, 0, wxALL, 0);

    auto tip_right    = new wxPanel(m_simplebook_right, wxID_ANY, wxDefaultPosition, AMS_STEP_SIZE, wxTAB_TRAVERSAL);
    m_sizer_right_tip = new wxBoxSizer(wxVERTICAL);
    m_tip_right_top   = new wxStaticText(tip_right, wxID_ANY, _L("Tips"), wxDefaultPosition, wxDefaultSize, 0);
    m_tip_right_top->SetFont(::Label::Head_13);
    m_tip_right_top->SetForegroundColour(AMS_CONTROL_BRAND_COLOUR);
    m_tip_right_top->Wrap(AMS_STEP_SIZE.x);
    m_sizer_right_tip->Add(m_tip_right_top, 0, 0, 0);
    m_sizer_right_tip->Add(0, 0, 0, wxTOP, 10);
    m_tip_load_info = new wxStaticText(tip_right, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, 0);
    m_tip_load_info->SetFont(::Label::Body_13);
    m_tip_load_info->SetForegroundColour(AMS_CONTROL_GRAY700);
    m_sizer_right_tip->Add(m_tip_load_info, 0, 0, 0);
    tip_right->SetSizer(m_sizer_right_tip);
    tip_right->Layout();

    m_filament_load_step = new ::StepIndicator(m_simplebook_right, wxID_ANY);
    m_filament_load_step->SetMinSize(AMS_STEP_SIZE);
    m_filament_load_step->SetSize(AMS_STEP_SIZE);
    m_filament_load_step->SetBackgroundColour(*wxWHITE);

    m_filament_unload_step = new ::StepIndicator(m_simplebook_right, wxID_ANY);
    m_filament_unload_step->SetMinSize(AMS_STEP_SIZE);
    m_filament_unload_step->SetSize(AMS_STEP_SIZE);
    m_filament_unload_step->SetBackgroundColour(*wxWHITE);

    m_simplebook_right->AddPage(tip_right, wxEmptyString, false);
    m_simplebook_right->AddPage(m_filament_load_step, wxEmptyString, false);
    m_simplebook_right->AddPage(m_filament_unload_step, wxEmptyString, false);

    wxBoxSizer *m_sizer_right_bottom = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_right_bottom->Add(0, 0, 1, wxEXPAND, 5);
    m_button_ams_setting = new Button(amswin, _L("AMS Settings"));
    m_button_ams_setting->SetBackgroundColor(btn_bg_white);
    m_button_ams_setting->SetBorderColor(btn_bd_white);
    m_button_ams_setting->SetFont(Label::Body_13);
    m_button_ams_setting->Hide();
    m_sizer_right_bottom->Add(m_button_ams_setting, 0, wxTOP, 20);
    m_sizer_right->Add(m_sizer_right_bottom, 0, wxEXPAND, 5);
    m_sizer_bottom->Add(m_sizer_right, 0, wxEXPAND, 5);
    m_sizer_body->Add(m_sizer_bottom, 0, wxEXPAND | wxLEFT, 11);

    init_scaled_buttons();
    amswin->SetSizer(m_sizer_body);
    amswin->Layout();
    amswin->Fit();
    
    //Thaw();

    SetSize(amswin->GetSize());
    SetMinSize(amswin->GetSize());

    // calibration mode
    m_simplebook_calibration = new wxSimplebook(this, wxID_ANY, wxDefaultPosition, amswin->GetSize(), wxTAB_TRAVERSAL);

    auto m_in_calibration_panel = new wxWindow(m_simplebook_calibration, wxID_ANY, wxDefaultPosition, amswin->GetSize(), wxTAB_TRAVERSAL);
    m_in_calibration_panel->SetBackgroundColour(AMS_CONTROL_WHITE_COLOUR);
    wxBoxSizer *sizer_calibration_h = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *sizer_calibration_v = new wxBoxSizer(wxVERTICAL);
    auto        thumbnail           = new wxStaticBitmap(m_in_calibration_panel, wxID_ANY, create_scaled_bitmap("ams_icon", nullptr, 126), wxDefaultPosition, wxDefaultSize);
    m_text_calibration_percent      = new wxStaticText(m_in_calibration_panel, wxID_ANY, wxT("0%"), wxDefaultPosition, wxDefaultSize, 0);
    m_text_calibration_percent->SetFont(::Label::Head_16);
    m_text_calibration_percent->SetForegroundColour(AMS_CONTROL_BRAND_COLOUR);
    auto m_text_calibration_tip = new wxStaticText(m_in_calibration_panel, wxID_ANY, _L("Calibrating AMS..."), wxDefaultPosition, wxDefaultSize, 0);
    m_text_calibration_tip->SetFont(::Label::Body_14);
    m_text_calibration_tip->SetForegroundColour(AMS_CONTROL_GRAY700);
    sizer_calibration_v->Add(thumbnail, 0, wxALIGN_CENTER, 0);
    sizer_calibration_v->Add(0, 0, 0, wxTOP, 16);
    sizer_calibration_v->Add(m_text_calibration_percent, 0, wxALIGN_CENTER, 0);
    sizer_calibration_v->Add(0, 0, 0, wxTOP, 8);
    sizer_calibration_v->Add(m_text_calibration_tip, 0, wxALIGN_CENTER, 0);
    sizer_calibration_h->Add(sizer_calibration_v, 1, wxALIGN_CENTER, 0);
    m_in_calibration_panel->SetSizer(sizer_calibration_h);
    m_in_calibration_panel->Layout();

    auto m_calibration_err_panel = new wxWindow(m_simplebook_calibration, wxID_ANY, wxDefaultPosition, amswin->GetSize(), wxTAB_TRAVERSAL);
    m_calibration_err_panel->SetBackgroundColour(AMS_CONTROL_WHITE_COLOUR);
    wxBoxSizer *sizer_err_calibration_h = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *sizer_err_calibration_v = new wxBoxSizer(wxVERTICAL);
    m_hyperlink = new wxHyperlinkCtrl(m_calibration_err_panel, wxID_ANY, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxHL_DEFAULT_STYLE);
    m_hyperlink->SetVisitedColour(wxColour(31, 142, 234));
    auto m_tip_calibration_err = new wxStaticText(m_calibration_err_panel, wxID_ANY, _L("A problem occured during calibration. Click to view the solution."), wxDefaultPosition,
                                                  wxDefaultSize, 0);
    m_tip_calibration_err->SetFont(::Label::Body_14);
    m_tip_calibration_err->SetForegroundColour(AMS_CONTROL_GRAY700);

    wxBoxSizer *sizer_button = new wxBoxSizer(wxHORIZONTAL);

    auto m_button_calibration_again = new Button(m_calibration_err_panel, _L("Calibrate again"));
    m_button_calibration_again->SetBackgroundColor(btn_bg_green);
    m_button_calibration_again->SetBorderColor(AMS_CONTROL_BRAND_COLOUR);
    m_button_calibration_again->SetTextColor(AMS_CONTROL_WHITE_COLOUR);
    m_button_calibration_again->SetMinSize(AMS_CONTRO_CALIBRATION_BUTTON_SIZE);
    m_button_calibration_again->SetCornerRadius(12);
    m_button_calibration_again->Bind(wxEVT_LEFT_DOWN, &AMSControl::on_clibration_again_click, this);

    sizer_button->Add(m_button_calibration_again, 0, wxALL, 5);

    auto       m_button_calibration_cancel = new Button(m_calibration_err_panel, _L("Cancel calibration"));
    m_button_calibration_cancel->SetBackgroundColor(btn_bg_white);
    m_button_calibration_cancel->SetBorderColor(AMS_CONTROL_GRAY700);
    m_button_calibration_cancel->SetTextColor(AMS_CONTROL_GRAY800);
    m_button_calibration_cancel->SetMinSize(AMS_CONTRO_CALIBRATION_BUTTON_SIZE);
    m_button_calibration_cancel->SetCornerRadius(12);
    m_button_calibration_cancel->Bind(wxEVT_LEFT_DOWN, &AMSControl::on_clibration_cancel_click, this);
    sizer_button->Add(m_button_calibration_cancel, 0, wxALL, 5);

    sizer_err_calibration_v->Add(m_hyperlink, 0, wxALIGN_CENTER, 0);
    sizer_err_calibration_v->Add(0, 0, 0, wxTOP, 6);
    sizer_err_calibration_v->Add(m_tip_calibration_err, 0, wxALIGN_CENTER, 0);
    sizer_err_calibration_v->Add(0, 0, 0, wxTOP, 8);
    sizer_err_calibration_v->Add(sizer_button, 0, wxALIGN_CENTER | wxTOP, 18);
    sizer_err_calibration_h->Add(sizer_err_calibration_v, 1, wxALIGN_CENTER, 0);
    m_calibration_err_panel->SetSizer(sizer_err_calibration_h);
    m_calibration_err_panel->Layout();

    m_simplebook_calibration->AddPage(m_in_calibration_panel, wxEmptyString, false);
    m_simplebook_calibration->AddPage(m_calibration_err_panel, wxEmptyString, false);

    AddPage(amswin, wxEmptyString, false);
    AddPage(m_simplebook_calibration, wxEmptyString, false);

    UpdateStepCtrl();

    m_button_extruder_feed->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AMSControl::on_filament_load), NULL, this);
    m_button_extruder_back->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AMSControl::on_filament_unload), NULL, this);
    m_button_ams_setting->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AMSControl::on_ams_setting_click), NULL, this);

    CreateAms();
    SetSelection(0);
    EnterNoneAMSMode();
}

void AMSControl::init_scaled_buttons()
{
    m_button_extruder_feed->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_extruder_feed->SetCornerRadius(FromDIP(11));
    m_button_extruder_back->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_extruder_back->SetCornerRadius(FromDIP(11));
    m_button_ams_setting->SetMinSize(wxSize(-1, FromDIP(33)));
    m_button_ams_setting->SetCornerRadius(FromDIP(12));
}

std::string AMSControl::GetCurentAms() { return m_current_ams; }

std::string AMSControl::GetCurrentCan(std::string amsid)
{
    std::string current_can;
    for (auto i = 0; i < m_ams_cans_list.GetCount(); i++) {
        AmsCansWindow *ams = m_ams_cans_list[i];
        if (ams->amsCans->m_info.ams_id == amsid) {
            current_can = ams->amsCans->GetCurrentCan();
            return current_can;
        }
    }
    return current_can;
    /*std::string current_can_id = "";
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams) {
            current_can_id =  m_ams_info[i].current_can_id;
        }
    }
    return current_can_id;*/
}

wxColour AMSControl::GetCanColour(std::string amsid, std::string canid) 
{
    wxColour col = *wxWHITE;
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == amsid) { 
            for (auto o = 0; o < m_ams_info[i].cans.size(); o++) {
                if (m_ams_info[i].cans[o].can_id == canid) { 
                    col = m_ams_info[i].cans[o].material_colour;
                }
            }
        }
    }
    return col;
}

void AMSControl::SetActionState(AMSAction action) 
{
    switch (action) {
    case Slic3r::GUI::AMSAction::AMS_ACTION_NONE: break;
    case Slic3r::GUI::AMSAction::AMS_ACTION_LOAD: 
        m_button_extruder_feed->Enable();
        m_button_extruder_back->Disable();
        break;
    case Slic3r::GUI::AMSAction::AMS_ACTION_UNLOAD: 
        m_button_extruder_feed->Disable();
        m_button_extruder_back->Enable();
        break;
    case Slic3r::GUI::AMSAction::AMS_ACTION_PRINTING: 
        m_button_extruder_feed->Disable();
        m_button_extruder_back->Disable();
        break;
    case Slic3r::GUI::AMSAction::AMS_ACTION_NORMAL:
        m_button_extruder_feed->Enable();
        m_button_extruder_back->Enable();
        break;
    default: break;
    }
}

void AMSControl::EnterNoneAMSMode()
{
    m_simplebook_amsitems->SetSelection(1);
    m_simplebook_ams->SetSelection(1);
    m_button_extruder_feed->Hide();
    ShowFilamentTip(false);
}

void AMSControl::ExitNoneAMSMode()
{
    m_simplebook_ams->SetSelection(0);
    m_simplebook_amsitems->SetSelection(0);
    m_button_extruder_feed->Show();
    ShowFilamentTip(true);
}

void AMSControl::EnterCalibrationMode(bool read_to_calibration)
{
    SetSelection(1);
    if (read_to_calibration)
        m_simplebook_calibration->SetSelection(0);
    else
        m_simplebook_calibration->SetSelection(1);
}

void AMSControl::ExitcClibrationMode() { SetSelection(0); }

void AMSControl::SetClibrationpercent(int percent) { m_text_calibration_percent->SetLabelText(wxString::Format("%d%%", percent)); }

void AMSControl::SetClibrationLink(wxString link)
{
    m_hyperlink->SetLabel(link);
    m_hyperlink->SetURL(link);
    m_hyperlink->Refresh();
    m_hyperlink->Update();
}

void AMSControl::PlayRridLoading(wxString amsid, wxString canid)
{
    AmsCansHash::iterator iter             = m_ams_cans_list.begin();
    auto                  count_item_index = 0;

    for (auto i = 0; i < m_ams_cans_list.GetCount(); i++) {
        AmsCansWindow *cans = m_ams_cans_list[i];
        if (cans->amsCans->m_info.ams_id == amsid) { cans->amsCans->PlayRridLoading(canid); }
        iter++;
    }
}

void AMSControl::StopRridLoading(wxString amsid, wxString canid)
{
    AmsCansHash::iterator iter             = m_ams_cans_list.begin();
    auto                  count_item_index = 0;

    for (auto i = 0; i < m_ams_cans_list.GetCount(); i++) {
        AmsCansWindow *cans = m_ams_cans_list[i];
        if (cans->amsCans->m_info.ams_id == amsid) { cans->amsCans->StopRridLoading(canid); }
        iter++;
    }
}

void AMSControl::msw_rescale()
{
    m_extruder->msw_rescale();
    m_button_extruder_back->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_extruder_feed->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_ams_setting->SetMinSize(wxSize(-1, FromDIP(33)));

    for (auto i = 0; i < m_ams_cans_list.GetCount(); i++) {
        AmsCansWindow *cans = m_ams_cans_list[i];
        cans->amsCans->msw_rescale();
    }

    Layout();
    Refresh();
}

void AMSControl::UpdateStepCtrl()
{
    for (int i = 0; i < LOAD_STEP_COUNT; i++) { m_filament_load_step->AppendItem(FILAMENT_LOAD_STEP_STRING[i]); }
    for (int i = 0; i < UNLOAD_STEP_COUNT; i++) { m_filament_unload_step->AppendItem(FILAMENT_UNLOAD_STEP_STRING[i]); }
}

void AMSControl::CreateAms()
{
    auto caninfo0_0 = Caninfo{"def_can_0", L(""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};
    auto caninfo0_1 = Caninfo{"def_can_1", L(""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};
    auto caninfo0_2 = Caninfo{"def_can_2", L(""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};
    auto caninfo0_3 = Caninfo{"def_can_3", L(""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};

    AMSinfo                        ams1 = AMSinfo{"def_ams_0", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    AMSinfo                        ams2 = AMSinfo{"def_ams_1", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    AMSinfo                        ams3 = AMSinfo{"def_ams_2", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    AMSinfo                        ams4 = AMSinfo{"def_ams_3", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    std::vector<AMSinfo>           ams_info{ams1, ams2, ams3, ams4};
    std::vector<AMSinfo>::iterator it;
    Freeze();
    for (it = ams_info.begin(); it != ams_info.end(); it++) { AddAms(*it, true); }
    m_sizer_top->Layout();
    Thaw();
}

void AMSControl::Reset() 
{
    m_current_ams = ""; 
    m_current_senect = "";
}


void AMSControl::UpdateAms(std::vector<AMSinfo> info, bool keep_selection)
{
    std::string curr_ams_id = GetCurentAms();
    std::string curr_can_id = GetCurrentCan(curr_ams_id);
    if (info.size() > 0) ExitNoneAMSMode();

    // update item
    m_ams_info = info;
    for (auto i = 0; i < m_ams_item_list.GetCount(); i++) {
        AmsItems *item = m_ams_item_list[i];
        if (i < info.size() && info.size() > 1) {
            item->amsItem->Update(m_ams_info[i]);
            item->amsItem->Open();
        } else {
            item->amsItem->Close();
        }
    }

    // update cans
    for (auto i = 0; i < m_ams_cans_list.GetCount(); i++) {
        AmsCansWindow *cans = m_ams_cans_list[i];
        if (i < info.size()) {
            cans->amsCans->m_info = m_ams_info[i];
            cans->amsCans->Update(m_ams_info[i]);
        }
    }

    if (m_current_senect.empty() && info.size() > 0) {
        if (curr_ams_id.empty()) {
            SwitchAms(info[0].ams_id);
            return;
        }

        if (keep_selection) {
            SwitchAms(curr_ams_id);
            for (auto i = 0; i < m_ams_cans_list.GetCount(); i++) {
                AmsCansWindow *cans = m_ams_cans_list[i];
                if (i < info.size()) { cans->amsCans->SelectCan(curr_can_id); }
            }
            return;
        }
    }
}

void AMSControl::AddAms(AMSinfo info, bool refresh)
{
    if (m_ams_count >= AMS_CONTROL_MAX_COUNT) return;

    // item
    auto amsitem = new AMSItem(m_panel_top, wxID_ANY, info);
    amsitem->Bind(wxEVT_LEFT_DOWN, [this, amsitem](wxMouseEvent &e) {
        SwitchAms(amsitem->m_amsinfo.ams_id);
        e.Skip();
    });

    AmsItems *item = new AmsItems();
    item->amsIndex = info.ams_id;
    item->amsItem  = amsitem;

    m_ams_item_list.Add(item);
    m_sizer_top->Add(amsitem, 0, wxALIGN_CENTER|wxRIGHT, 6);

    AmsCansWindow *canswin = new AmsCansWindow();
    auto           amscans = new AmsCans(m_simplebook_cans, wxID_ANY, info);

    canswin->amsIndex = info.ams_id;
    canswin->amsCans  = amscans;
    m_ams_cans_list.Add(canswin);

    m_simplebook_cans->AddPage(amscans, wxEmptyString, false);
    amscans->m_selection = m_simplebook_cans->GetPageCount() - 1;

    if (refresh) { m_sizer_top->Layout(); }
    m_ams_count++;
    m_ams_info.push_back(info);
}

void AMSControl::SwitchAms(std::string ams_id)
{
    for (auto i = 0; i < m_ams_item_list.GetCount(); i++) {
        AmsItems *item = m_ams_item_list[i];
        if (item->amsItem->m_amsinfo.ams_id == ams_id) {
            item->amsItem->OnSelected();
            //item->amsItem->ShowHumidity();
            m_current_senect = ams_id;
        } else {
            item->amsItem->UnSelected();
            //item->amsItem->HideHumidity();
        }
        m_sizer_top->Layout();
        // m_panel_top->Fit();
    }

    for (auto i = 0; i < m_ams_cans_list.GetCount(); i++) {
        AmsCansWindow *cans = m_ams_cans_list[i];
        if (cans->amsCans->m_info.ams_id == ams_id) { m_simplebook_cans->SetSelection(cans->amsCans->m_selection); }
    }
    m_current_ams = ams_id;

     // update extruder
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams) {
            switch (m_ams_info[i].current_step) {
            case AMSPassRoadSTEP::AMS_ROAD_STEP_NONE: m_extruder->TurnOff(); break;

            case AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1: m_extruder->TurnOff(); break;

            case AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2: m_extruder->TurnOn(GetCanColour(m_current_ams, m_ams_info[i].current_can_id)); break;

            case AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3: m_extruder->TurnOn(GetCanColour(m_current_ams, m_ams_info[i].current_can_id)); break;
            }
        }
    }

    // update buttons
}

void AMSControl::SetFilamentStep(int item_idx, bool isload)
{
    if (item_idx == FilamentStep::STEP_IDLE && isload) {
        m_filament_load_step->Idle();
        return;
    }

    if (item_idx == FilamentStep::STEP_IDLE && !isload) {
        m_filament_unload_step->Idle();
        return;
    }

    if (item_idx >= 0 && isload && item_idx < FilamentStep::STEP_COUNT) {
        m_simplebook_right->SetSelection(1);
        m_filament_load_step->SelectItem(item_idx - 1);
    }

    if (item_idx >= 0 && !isload && item_idx < FilamentStep::STEP_COUNT) {
        m_simplebook_right->SetSelection(2);
        m_filament_unload_step->SelectItem(item_idx - 1);
    }
}

void AMSControl::ShowFilamentTip(bool hasams)
{
    m_simplebook_right->SetSelection(0);
    if (hasams) {
        m_tip_right_top->Show();
        m_tip_load_info->SetLabelText(_L("Choose an AMS slot then press \"Load\" or \"Unload\" button to automatically load or unload filiament."));
    } else {
        // m_tip_load_info->SetLabelText(_L("Before loading, please make sure the filament is pushed into toolhead."));
        m_tip_right_top->Hide();
        m_tip_load_info->SetLabelText(wxEmptyString);
    }
    m_sizer_right_tip->Layout();
    m_tip_load_info->Wrap(AMS_STEP_SIZE.x);
    m_tip_load_info->SetMinSize(AMS_STEP_SIZE);
}

void AMSControl::SetHumidity(std::string amsid, int humidity)
{
    for (auto i = 0; i < m_ams_item_list.GetCount(); i++) {
        AmsItems *item = m_ams_item_list[i];
        if (amsid == item->amsItem->m_amsinfo.ams_id) { item->amsItem->SetHumidity(humidity); }
    }
}

bool AMSControl::Enable(bool enable)
{
    for (auto i = 0; i < m_ams_item_list.GetCount(); i++) {
        AmsItems *item = m_ams_item_list[i];
        item->amsItem->Enable(enable);
    }

    for (auto i = 0; i < m_ams_cans_list.GetCount(); i++) {
        AmsCansWindow *cans = m_ams_cans_list[i];
        cans->amsCans->Enable(enable);
    }
    m_button_extruder_back->Enable(enable);
    m_button_extruder_feed->Enable(enable);
    m_button_ams_setting->Enable(enable);

    m_filament_load_step->Enable(enable);
    return wxWindow::Enable(enable);
}

void AMSControl::SetAmsStep(std::string ams_id, std::string canid, AMSPassRoadType type, AMSPassRoadSTEP step)
{
    AmsCansWindow *cans     = nullptr;
    bool           notfound = true;

    for (auto i = 0; i < m_ams_cans_list.GetCount(); i++) {
        cans = m_ams_cans_list[i];
        if (cans->amsCans->m_info.ams_id == ams_id) {
            notfound = false;
            break;
        }
    }

    if (notfound) return;
    if (cans == nullptr) return;

    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
        if (ams_id == m_current_ams) { m_extruder->TurnOff(); }
        cans->amsCans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    }

    type = AMSPassRoadType::AMS_ROAD_TYPE_LOAD;
    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1) {
        if (ams_id == m_current_ams) { m_extruder->TurnOff(); }
        cans->amsCans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
        cans->amsCans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
    }

    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2) {
        if (ams_id == m_current_ams) { m_extruder->TurnOn(GetCanColour(ams_id, canid)); }
        cans->amsCans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    }

    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3) {
        if (ams_id == m_current_ams) { m_extruder->TurnOn(GetCanColour(ams_id, canid)); }
        cans->amsCans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
        cans->amsCans->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
    }

    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == ams_id) {
            m_ams_info[i].current_step   = step;
            m_ams_info[i].current_can_id = canid;
        }
    }

    if (type == AMSPassRoadType::AMS_ROAD_TYPE_LOAD) { 
        SetActionState(AMSAction::AMS_ACTION_LOAD); 
    }

    if (type == AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD) { 
        SetActionState(AMSAction::AMS_ACTION_UNLOAD); 
    }
}

void AMSControl::on_filament_load(wxCommandEvent &event)
{
    m_button_extruder_back->Disable();
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_LOAD; }
    }
    post_event(SimpleEvent(EVT_AMS_LOAD));
}

void AMSControl::on_filament_unload(wxCommandEvent &event)
{
    m_button_extruder_feed->Disable();
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_UNLOAD; }
    }
    post_event(SimpleEvent(EVT_AMS_UNLOAD));
}

void AMSControl::on_ams_setting_click(wxCommandEvent &event)
{
    post_event(SimpleEvent(EVT_AMS_SETTINGS));
}

void AMSControl::on_clibration_again_click(wxMouseEvent &event) { post_event(SimpleEvent(EVT_AMS_CLIBRATION_AGAIN)); }

void AMSControl::on_clibration_cancel_click(wxMouseEvent &event) { post_event(SimpleEvent(EVT_AMS_CLIBRATION_CANCEL)); }

void AMSControl::post_event(wxEvent &&event)
{
    event.SetEventObject(m_parent);
    wxPostEvent(m_parent, event);
}

}} // namespace Slic3r::GUI
