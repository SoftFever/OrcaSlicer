#include "AMSItem.hpp"
#include "Label.hpp"
#include "../BitmapCache.hpp"
#include "../I18N.hpp"
#include "../GUI_App.hpp"
#include "../Utils/WxFontUtils.hpp"

#include "slic3r/GUI/DeviceTab/uiAmsHumidityPopup.h"

#include <wx/simplebook.h>
#include <wx/dcgraph.h>

#include <boost/log/trivial.hpp>

#include "CalibUtils.hpp"



namespace Slic3r { namespace GUI {

    static const wxColour AMS_TRAY_DEFAULT_COL = wxColour(255, 255, 255);
    wxDEFINE_EVENT(EVT_AMS_EXTRUSION_CALI, wxCommandEvent);
    wxDEFINE_EVENT(EVT_AMS_LOAD, SimpleEvent);
    wxDEFINE_EVENT(EVT_AMS_UNLOAD, SimpleEvent);
    wxDEFINE_EVENT(EVT_AMS_SETTINGS, SimpleEvent);
    wxDEFINE_EVENT(EVT_AMS_FILAMENT_BACKUP, SimpleEvent);
    wxDEFINE_EVENT(EVT_AMS_REFRESH_RFID, wxCommandEvent);
    wxDEFINE_EVENT(EVT_AMS_ON_SELECTED, wxCommandEvent);
    wxDEFINE_EVENT(EVT_AMS_ON_FILAMENT_EDIT, wxCommandEvent);
    wxDEFINE_EVENT(EVT_VAMS_ON_FILAMENT_EDIT, wxCommandEvent);
    wxDEFINE_EVENT(EVT_AMS_CLIBRATION_AGAIN, wxCommandEvent);
    wxDEFINE_EVENT(EVT_AMS_CLIBRATION_CANCEL, wxCommandEvent);
    wxDEFINE_EVENT(EVT_AMS_GUIDE_WIKI, wxCommandEvent);
    wxDEFINE_EVENT(EVT_AMS_RETRY, wxCommandEvent);
    wxDEFINE_EVENT(EVT_AMS_SHOW_HUMIDITY_TIPS, wxCommandEvent);
    wxDEFINE_EVENT(EVT_AMS_UNSELETED_VAMS, wxCommandEvent);
    wxDEFINE_EVENT(EVT_CLEAR_SPEED_CONTROL, wxCommandEvent);


#define AMS_CANS_WINDOW_SIZE wxSize(FromDIP(264), FromDIP(196))
bool AMSinfo::parse_ams_info(MachineObject *obj, Ams *ams, bool remain_flag, bool humidity_flag)
{
    if (!ams) return false;
    this->ams_id = ams->id;

    if (ams->type == 1 || ams->type == 3 || ams->type == N3S_AMS) {
        this->ams_humidity = ams->humidity;
    }
    else{
        this->ams_humidity = -1;
    }

    this->humidity_raw = ams->humidity_raw;
    this->left_dray_time = ams->left_dry_time;
    this->current_temperature = ams->current_temperature;
    this->ams_type = AMSModel(ams->type);

    cans.clear();
    for (int i = 0; i < ams->trayList.size(); i++) {
        auto    it = ams->trayList.find(std::to_string(i));
        Caninfo info;
        // tray is exists
        if (it != ams->trayList.end() && it->second->is_exists) {
            if (it->second->is_tray_info_ready()) {
                info.can_id        = it->second->id;
                info.ctype         = it->second->ctype;
                info.material_name = it->second->get_display_filament_type();
                if (!it->second->color.empty()) {
                    info.material_colour = AmsTray::decode_color(it->second->color);
                } else {
                    // set to white by default
                    info.material_colour = AMS_TRAY_DEFAULT_COL;
                }

                for (std::string cols:it->second->cols) {
                    info.material_cols.push_back(AmsTray::decode_color(cols));
                }

                if (MachineObject::is_bbl_filament(it->second->tag_uid)) {
                    info.material_state = AMSCanType::AMS_CAN_TYPE_BRAND;
                } else {
                    info.material_state = AMSCanType::AMS_CAN_TYPE_THIRDBRAND;
                }

                if (!MachineObject::is_bbl_filament(it->second->tag_uid) || !remain_flag) {
                    info.material_remain = 100;
                } else {
                    if(it->second->remain < 0 || it->second->remain > 100) {
                        info.material_remain = 100;/*ignore the invalid data*/
                    } else {
                        info.material_remain = it->second->remain;
                    }
                }


            } else {
                info.can_id = it->second->id;
                info.material_name = "";
                info.ctype = 0;
                info.material_colour = AMS_TRAY_DEFAULT_COL;
                info.material_state = AMSCanType::AMS_CAN_TYPE_THIRDBRAND;
                wxColour(255, 255, 255);
            }

            if (it->second->is_tray_info_ready() && obj->cali_version >= 0) {
                CalibUtils::get_pa_k_n_value_by_cali_idx(obj, it->second->cali_idx, info.k, info.n);
            }
            else {
                info.k = it->second->k;
                info.n = it->second->n;
            }
        } else {
            //info.can_id = i;
            info.can_id         = std::to_string(i);
            info.material_state = AMSCanType::AMS_CAN_TYPE_EMPTY;
        }
        cans.push_back(info);
    }
    return true;
}

/*************************************************
Description:AMSrefresh
**************************************************/

AMSrefresh::AMSrefresh() { SetFont(Label::Body_10);}

AMSrefresh::AMSrefresh(wxWindow *parent, wxString number, Caninfo info, const wxPoint &pos, const wxSize &size) : AMSrefresh()
{
    m_info = info;
    m_can_id = number.ToStdString();
    create(parent, wxID_ANY, pos, size);
}

AMSrefresh::AMSrefresh(wxWindow *parent, int number, Caninfo info, const wxPoint &pos, const wxSize &size) : AMSrefresh()
{
    m_info = info;
    m_can_id = wxString::Format("%d", number).ToStdString();
    create(parent, wxID_ANY, pos, size);
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
    SetBackgroundColour(StateColor::darkModeColorFor(AMS_CONTROL_DEF_LIB_BK_COLOUR));

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
    if (m_play_loading || m_disable_mode)  return;

    m_play_loading = true;
    m_playing_timer->Start(AMS_REFRESH_PLAY_LOADING_TIMER);
    Refresh();
}

void AMSrefresh::StopLoading()
{
    if (!m_play_loading || m_disable_mode) return;

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
    if (m_disable_mode)
        return;
    event.SetString(m_info.can_id);
    event.SetEventObject(m_parent);
    wxPostEvent(m_parent, event);
    event.Skip();
}

void AMSrefresh::paintEvent(wxPaintEvent &evt)
{
    wxSize    size = GetSize();
    wxPaintDC dc(this);

    auto colour = StateColor::darkModeColorFor(AMS_CONTROL_GRAY700);
    if (!wxWindow::IsEnabled()) { colour = StateColor::darkModeColorFor(AMS_CONTROL_GRAY500); }

    auto pot = wxPoint((size.x - m_bitmap_selected.GetBmpSize().x) / 2, (size.y - m_bitmap_selected.GetBmpSize().y) / 2);

    if (!m_disable_mode) {
        if (!m_play_loading) {
            dc.DrawBitmap(m_selected ? m_bitmap_selected.bmp() : m_bitmap_normal.bmp(), pot);
        }
        else {
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
    }

    dc.SetPen(wxPen(colour));
    dc.SetBrush(wxBrush(colour));
    dc.SetFont(Label::Body_11);
    //dc.SetTextForeground(StateColor::darkModeColorFor(AMS_CONTROL_BLACK_COLOUR));
    dc.SetTextForeground(colour);
    auto tsize = dc.GetTextExtent(m_refresh_id);
    pot        = wxPoint((size.x - tsize.x) / 2, (size.y - tsize.y) / 2);
    dc.DrawText(m_refresh_id, pot);
}

void AMSrefresh::Update(std::string ams_id, Caninfo info)
{
    m_ams_id = ams_id;
    m_info   = info;

    if (!m_ams_id.empty() && !m_can_id.empty()) {
        auto aid = atoi(m_ams_id.c_str());
        auto tid = atoi(m_can_id.c_str());
        auto tray_id = aid * 4 + tid;
        m_refresh_id = wxGetApp().transition_tridid(tray_id);
    }
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
    dc.DrawRectangle(0, FromDIP(18), size.x, size.y - FromDIP(18) - FromDIP(5));
    dc.DrawBitmap(m_ams_extruder.bmp(), wxPoint((size.x - m_ams_extruder.GetBmpSize().x) / 2, (size.y - m_ams_extruder.GetBmpSize().y) / 2));
}


AMSextruderImage::AMSextruderImage(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size) 
{
    wxWindow::Create(parent, id, pos, AMS_EXTRUDER_BITMAP_SIZE); 
    SetBackgroundColour(StateColor::darkModeColorFor(*wxWHITE));

    m_ams_extruder = ScalableBitmap(this, "monitor_ams_extruder",55);
    SetSize(AMS_EXTRUDER_BITMAP_SIZE);
    SetMinSize(AMS_EXTRUDER_BITMAP_SIZE);
    SetMaxSize(AMS_EXTRUDER_BITMAP_SIZE);


    Bind(wxEVT_PAINT, &AMSextruderImage::paintEvent, this);
}

AMSextruderImage::~AMSextruderImage() {}




//Ams Extruder
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
    m_bitmap_panel->SetBackgroundColour(AMS_EXTRUDER_DEF_COLOUR);
    m_bitmap_panel->SetDoubleBuffered(true);
    m_bitmap_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_amsSextruder = new AMSextruderImage(m_bitmap_panel, wxID_ANY, wxDefaultPosition, AMS_EXTRUDER_BITMAP_SIZE);
    m_bitmap_sizer->Add(m_amsSextruder, 0, wxALIGN_CENTER, 0);

    m_bitmap_panel->SetSizer(m_bitmap_sizer);
    m_bitmap_panel->Layout();
    m_sizer_body->Add( 0, 0, 1, wxEXPAND, 0 );
    m_sizer_body->Add(m_bitmap_panel, 0, wxALIGN_CENTER, 0);

    SetSizer(m_sizer_body);

    Bind(wxEVT_PAINT, &AMSextruder::paintEvent, this);
    Layout();
}

void AMSextruder::OnVamsLoading(bool load, wxColour col)
{
    m_vams_loading = load;
    if (load)m_current_colur = col;
    Refresh();
}

void AMSextruder::OnAmsLoading(bool load, wxColour col /*= AMS_CONTROL_GRAY500*/)
{
    m_ams_loading = load;
    if (load)m_current_colur = col;
    Refresh();
}

void AMSextruder::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AMSextruder::render(wxDC& dc)
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

void AMSextruder::doRender(wxDC& dc)
{
    //m_current_colur = 
    wxSize size = GetSize();
    dc.SetPen(wxPen(AMS_CONTROL_GRAY500, 2, wxPENSTYLE_SOLID));
    dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));

    if (!m_none_ams_mode) {
        dc.DrawLine(size.x / 2, -1, size.x / 2, size.y * 0.6 - 1);
    }

    if (m_has_vams) {
        dc.DrawRoundedRectangle(-size.x / 2, size.y * 0.1, size.x, size.y, 4);

        if (m_vams_loading) {

            if (m_current_colur.Alpha() == 0) { dc.SetPen(wxPen(*wxWHITE, 6, wxPENSTYLE_SOLID)); }
            else { dc.SetPen(wxPen(m_current_colur, 6, wxPENSTYLE_SOLID)); }
            dc.DrawRoundedRectangle(-size.x / 2, size.y * 0.1, size.x, size.y, 4);

            if ((m_current_colur == *wxWHITE || m_current_colur.Alpha() == 0) && !wxGetApp().dark_mode()) {
                dc.SetPen(wxPen(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, 1, wxPENSTYLE_SOLID));
                dc.DrawRoundedRectangle(-size.x / 2 - FromDIP(3), size.y * 0.1 + FromDIP(3), size.x, size.y, 3);
                dc.DrawRoundedRectangle(-size.x / 2 + FromDIP(3), size.y * 0.1 - FromDIP(3), size.x, size.y, 5);
            }
        }

        if (m_ams_loading && !m_none_ams_mode) {
            if (m_current_colur.Alpha() == 0) {dc.SetPen(wxPen(*wxWHITE, 6, wxPENSTYLE_SOLID));}
            else {dc.SetPen(wxPen(m_current_colur, 6, wxPENSTYLE_SOLID));}
            dc.DrawLine(size.x / 2, -1, size.x / 2, size.y * 0.6 - 1);

            if ((m_current_colur == *wxWHITE || m_current_colur.Alpha() == 0) && !wxGetApp().dark_mode()) {
                dc.SetPen(wxPen(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, 1, wxPENSTYLE_SOLID));
                dc.DrawLine(size.x / 2 - FromDIP(4), -1, size.x / 2 - FromDIP(3), size.y * 0.6 - 1);
                dc.DrawLine(size.x / 2 + FromDIP(3), -1, size.x / 2 + FromDIP(3), size.y * 0.6 - 1);
            }
        }
    }
    else {
        if (m_ams_loading) {
            if (m_current_colur.Alpha() == 0) { dc.SetPen(wxPen(*wxWHITE, 6, wxPENSTYLE_SOLID)); }
            else { dc.SetPen(wxPen(m_current_colur, 6, wxPENSTYLE_SOLID)); }
            dc.DrawLine(size.x / 2, -1, size.x / 2, size.y * 0.6 - 1);

            if ((m_current_colur == *wxWHITE || m_current_colur.Alpha() == 0) && !wxGetApp().dark_mode()) {
                dc.SetPen(wxPen(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, 1, wxPENSTYLE_SOLID));
                dc.DrawLine(size.x / 2 - FromDIP(4), -1, size.x / 2 - FromDIP(3), size.y * 0.6 - 1);
                dc.DrawLine(size.x / 2 + FromDIP(3), -1, size.x / 2 + FromDIP(3), size.y * 0.6 - 1);
            }
        }
    }

}

void AMSextruder::msw_rescale()
{
    m_amsSextruder->msw_rescale();
    Layout();
    Update();
    Refresh();
}

/*************************************************
Description:AMSVirtualRoad
**************************************************/

AMSVirtualRoad::AMSVirtualRoad(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size) { create(parent, id, pos, size); }

AMSVirtualRoad::~AMSVirtualRoad() {}

void AMSVirtualRoad::OnVamsLoading(bool load, wxColour col)
{
    m_vams_loading = load;
    if (load)m_current_color = col;
    Refresh();
}

void AMSVirtualRoad::create(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size)
{
    wxWindow::Create(parent, id, pos, wxDefaultSize, wxBORDER_NONE);
    SetBackgroundColour(StateColor::darkModeColorFor(AMS_CONTROL_WHITE_COLOUR));
    Layout();
    Bind(wxEVT_PAINT, &AMSVirtualRoad::paintEvent, this);
}

void AMSVirtualRoad::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AMSVirtualRoad::render(wxDC& dc)
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

void AMSVirtualRoad::doRender(wxDC& dc)
{
    if (!m_has_vams) return;

    wxSize size = GetSize();
    if (m_vams_loading) {
        if (m_current_color.Alpha() == 0) { dc.SetPen(wxPen(*wxWHITE, 6, wxPENSTYLE_SOLID)); }
        else { dc.SetPen(wxPen(m_current_color, 6, wxPENSTYLE_SOLID)); }
    }
    else {
        dc.SetPen(wxPen(AMS_CONTROL_GRAY500, 2, wxPENSTYLE_SOLID));
    }

    dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
    dc.DrawRoundedRectangle(size.x / 2, -size.y / 1.1 + FromDIP(1), size.x, size.y, 4);

    if ((m_current_color == *wxWHITE || m_current_color.Alpha() == 0) && !wxGetApp().dark_mode()) {
        dc.SetPen(wxPen(AMS_CONTROL_DEF_LIB_BK_COLOUR, 1, wxPENSTYLE_SOLID));
        dc.DrawRoundedRectangle(size.x / 2 - FromDIP(3), -size.y / 1.1 + FromDIP(4), size.x, size.y, 5);
        dc.DrawRoundedRectangle(size.x / 2 + FromDIP(3), -size.y / 1.1 - FromDIP(2), size.x, size.y, 3);
    }
}


void AMSVirtualRoad::msw_rescale()
{
    Layout();
    Update();
    Refresh();
}

/*************************************************
Description:AMSLib
**************************************************/
AMSLib::AMSLib(wxWindow *parent, std::string ams_idx, Caninfo info)
{
    m_border_color   = (wxColour(130, 130, 128));
    m_road_def_color = AMS_CONTROL_GRAY500;
    wxWindow::SetBackgroundColour(StateColor::darkModeColorFor(AMS_CONTROL_DEF_LIB_BK_COLOUR));
    create(parent);

    Bind(wxEVT_PAINT, &AMSLib::paintEvent, this);
    Bind(wxEVT_ENTER_WINDOW, &AMSLib::on_enter_window, this);
    Bind(wxEVT_LEAVE_WINDOW, &AMSLib::on_leave_window, this);
    Bind(wxEVT_LEFT_DOWN, &AMSLib::on_left_down, this);

    Update(info, ams_idx, false);
}

AMSLib::~AMSLib()
{
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
    m_bitmap_transparent    = ScalableBitmap(this, "transparent_ams_lib", 68);

    m_bitmap_extra_tray_left    = ScalableBitmap(this, "extra_ams_tray_left", 80);
    m_bitmap_extra_tray_right    = ScalableBitmap(this, "extra_ams_tray_right", 80);

    m_bitmap_extra_tray_left_hover = ScalableBitmap(this, "extra_ams_tray_left_hover", 80);
    m_bitmap_extra_tray_right_hover = ScalableBitmap(this, "extra_ams_tray_right_hover", 80);

    m_bitmap_extra_tray_left_selected = ScalableBitmap(this, "extra_ams_tray_left_selected", 80);
    m_bitmap_extra_tray_right_selected = ScalableBitmap(this, "extra_ams_tray_right_selected", 80);


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
    if (m_info.material_state != AMSCanType::AMS_CAN_TYPE_EMPTY && m_info.material_state != AMSCanType::AMS_CAN_TYPE_NONE) {
        auto size = GetSize();
        auto pos  = evt.GetPosition();
        if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_THIRDBRAND || m_info.material_state == AMSCanType::AMS_CAN_TYPE_BRAND ||
            m_info.material_state == AMSCanType::AMS_CAN_TYPE_VIRTUAL) {

            auto left = FromDIP(10);
            auto right = size.x - FromDIP(10);
            auto top = 0;
            auto bottom = 0;

            if (m_ams_model == AMSModel::GENERIC_AMS || m_ams_model == AMSModel::N3F_AMS || m_ams_model == AMSModel::N3S_AMS || m_ams_model == AMSModel::EXT_AMS) {
                top = (size.y - FromDIP(15) - m_bitmap_editable_light.GetBmpSize().y);
                bottom = size.y - FromDIP(15);
            }
            else if (m_ams_model == AMSModel::AMS_LITE) {
                top = (size.y - FromDIP(20) - m_bitmap_editable_light.GetBmpSize().y);
                bottom = size.y - FromDIP(20);
            }

            if (pos.x >= left && pos.x <= right && pos.y >= top && top <= bottom) {
                if (m_selected) {
                    if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_VIRTUAL) {
                        post_event(wxCommandEvent(EVT_VAMS_ON_FILAMENT_EDIT));
                    }
                    else {
                        post_event(wxCommandEvent(EVT_AMS_ON_FILAMENT_EDIT));
                    }
                } else {
                    BOOST_LOG_TRIVIAL(trace) << "current amslib is not selected";
                }
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
    if (m_ams_model == AMSModel::AMS_LITE) {
        render_lite_text(dc);
    }
    else{
        render_generic_text(dc);
    }
}

void AMSLib::render_lite_text(wxDC& dc)
{
    auto tmp_lib_colour = m_info.material_colour;

    change_the_opacity(tmp_lib_colour);
    auto temp_text_colour = AMS_CONTROL_GRAY800;

    if (tmp_lib_colour.GetLuminance() < 0.6) {
        temp_text_colour = AMS_CONTROL_WHITE_COLOUR;
    }
    else {
        temp_text_colour = AMS_CONTROL_GRAY800;
    }

    if (m_info.material_remain < 50) {
        temp_text_colour = AMS_CONTROL_GRAY800;
    }

    if (tmp_lib_colour.Alpha() == 0) {
        temp_text_colour = AMS_CONTROL_GRAY800;
    }

    dc.SetFont(::Label::Body_13);
    dc.SetTextForeground(temp_text_colour);

    auto libsize = GetSize();
    if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_THIRDBRAND
        || m_info.material_state == AMSCanType::AMS_CAN_TYPE_BRAND
        || m_info.material_state == AMSCanType::AMS_CAN_TYPE_VIRTUAL) {

        if (m_info.material_name.empty()) {
            auto tsize = dc.GetMultiLineTextExtent("?");
            auto pot = wxPoint(0, 0);
            pot = wxPoint((libsize.x - tsize.x) / 2 + FromDIP(2), (libsize.y - tsize.y) / 2 - FromDIP(5));
            dc.DrawText("?", pot);
        }
        else {
            auto tsize = dc.GetMultiLineTextExtent(m_info.material_name);
            std::vector<std::string> split_char_arr = { " ", "-" };
            bool has_split = false;
            std::string has_split_char = " ";

            for (std::string split_char : split_char_arr) {
                if (m_info.material_name.find(split_char) != std::string::npos) {
                    has_split = true;
                    has_split_char = split_char;
                }
            }

            if (has_split) {
                dc.SetFont(::Label::Body_10);
                auto line_top = m_info.material_name.substr(0, m_info.material_name.find(has_split_char));
                auto line_bottom = m_info.material_name.substr(m_info.material_name.find(has_split_char));

                auto line_top_tsize = dc.GetMultiLineTextExtent(line_top);
                auto line_bottom_tsize = dc.GetMultiLineTextExtent(line_bottom);

                auto pot_top = wxPoint((libsize.x - line_top_tsize.x) / 2 + FromDIP(3), (libsize.y - line_top_tsize.y) / 2 - line_top_tsize.y);
                dc.DrawText(line_top, pot_top);

                auto pot_bottom = wxPoint((libsize.x - line_bottom_tsize.x) / 2 + FromDIP(3), (libsize.y - line_bottom_tsize.y) / 2);
                dc.DrawText(line_bottom, pot_bottom);


            }
            else {
                dc.SetFont(::Label::Body_10);
                auto pot = wxPoint(0, 0);
                if (m_obj ) {
                    pot = wxPoint((libsize.x - tsize.x) / 2 + FromDIP(6), (libsize.y - tsize.y) / 2 - FromDIP(5));
                }
                dc.DrawText(m_info.material_name, pot);
            }
        }
    }

    if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_EMPTY) {
        auto tsize = dc.GetMultiLineTextExtent("/");
        auto pot = wxPoint((libsize.x - tsize.x) / 2 + FromDIP(2), (libsize.y - tsize.y) / 2 + FromDIP(3));
        dc.DrawText("/", pot);
    }
}

void AMSLib::render_generic_text(wxDC &dc)
{
    bool show_k_value = true;
    if (m_obj && (m_obj->cali_version >= 0) && (abs(m_info.k - 0) < 1e-3)) {
        show_k_value = false;
    }

    auto tmp_lib_colour = m_info.material_colour;
    change_the_opacity(tmp_lib_colour);

    auto temp_text_colour = AMS_CONTROL_GRAY800;

    if (tmp_lib_colour.GetLuminance() < 0.6) {
        temp_text_colour = AMS_CONTROL_WHITE_COLOUR;
    }
    else {
        temp_text_colour = AMS_CONTROL_GRAY800;
    }

    if (m_info.material_remain < 50) {
        temp_text_colour = AMS_CONTROL_GRAY800;
    }

    if (tmp_lib_colour.Alpha() == 0) {
        temp_text_colour = AMS_CONTROL_GRAY800;
    }

    dc.SetFont(::Label::Body_13);
    dc.SetTextForeground(temp_text_colour);
    auto alpha = m_info.material_colour.Alpha();
    if (alpha != 0 && alpha != 255 && alpha != 254) {
        dc.SetTextForeground(*wxBLACK);
    }

    auto libsize = GetSize();
    if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_THIRDBRAND
        || m_info.material_state == AMSCanType::AMS_CAN_TYPE_BRAND
        || m_info.material_state == AMSCanType::AMS_CAN_TYPE_VIRTUAL) {

        if (m_info.material_name.empty() /*&&  m_info.material_state != AMSCanType::AMS_CAN_TYPE_VIRTUAL*/) {
            auto tsize = dc.GetMultiLineTextExtent("?");
            auto pot = wxPoint(0, 0);

            if (m_obj && show_k_value) {
                pot = wxPoint((libsize.x - tsize.x) / 2, (libsize.y - tsize.y) / 2 - FromDIP(9));
            }
            else {
                pot = wxPoint((libsize.x - tsize.x) / 2, (libsize.y - tsize.y) / 2 + FromDIP(3));
            }
            dc.DrawText("?", pot);

        }
        else {
            auto tsize = dc.GetMultiLineTextExtent(m_info.material_name);
            std::vector<std::string> split_char_arr = { " ", "-" };
            bool has_split = false;
            std::string has_split_char = " ";

            for (std::string split_char : split_char_arr) {
                if (m_info.material_name.find(split_char) != std::string::npos) {
                    has_split = true;
                    has_split_char = split_char;
                }
            }


            if (has_split) {
                dc.SetFont(::Label::Body_12);

                auto line_top = m_info.material_name.substr(0, m_info.material_name.find(has_split_char));
                auto line_bottom = m_info.material_name.substr(m_info.material_name.find(has_split_char));

                auto line_top_tsize = dc.GetMultiLineTextExtent(line_top);
                auto line_bottom_tsize = dc.GetMultiLineTextExtent(line_bottom);

                if (!m_show_kn) {
                    auto pot_top = wxPoint((libsize.x - line_top_tsize.x) / 2, (libsize.y - line_top_tsize.y) / 2 - line_top_tsize.y + FromDIP(6));
                    dc.DrawText(line_top, pot_top);


                    auto pot_bottom = wxPoint((libsize.x - line_bottom_tsize.x) / 2, (libsize.y - line_bottom_tsize.y) / 2 + FromDIP(4));
                    dc.DrawText(line_bottom, pot_bottom);
                }
                else {
                    auto pot_top = wxPoint((libsize.x - line_top_tsize.x) / 2, (libsize.y - line_top_tsize.y) / 2 - line_top_tsize.y - FromDIP(6));
                    dc.DrawText(line_top, pot_top);

                    auto pot_bottom = wxPoint((libsize.x - line_bottom_tsize.x) / 2, (libsize.y - line_bottom_tsize.y) / 2 - FromDIP(8));
                    dc.DrawText(line_bottom, pot_bottom);
                }


            }
            else {
                auto pot = wxPoint(0, 0);
                if (m_obj && show_k_value) {
                    pot = wxPoint((libsize.x - tsize.x) / 2, (libsize.y - tsize.y) / 2 - FromDIP(9));
                } else {
                    pot = wxPoint((libsize.x - tsize.x) / 2, (libsize.y - tsize.y) / 2 + FromDIP(3));
                }
                dc.DrawText(m_info.material_name, pot);
            }
        }

        //draw k&n
        if (m_obj && show_k_value) {
            if (m_show_kn) {
                wxString str_k = wxString::Format("K %1.3f", m_info.k);
                wxString str_n = wxString::Format("N %1.3f", m_info.n);
                dc.SetFont(::Label::Body_11);
                auto tsize = dc.GetMultiLineTextExtent(str_k);
                auto pot_k = wxPoint((libsize.x - tsize.x) / 2, (libsize.y - tsize.y) / 2 - FromDIP(9) + tsize.y);
                dc.DrawText(str_k, pot_k);
            }
        }
    }

    if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_EMPTY) {
        auto tsize = dc.GetMultiLineTextExtent(_L("Empty"));
        auto pot = wxPoint((libsize.x - tsize.x) / 2, (libsize.y - tsize.y) / 2 + FromDIP(3));
        dc.DrawText(_L("Empty"), pot);
    }
}

void AMSLib::doRender(wxDC &dc)
{
    if (m_ams_model == AMSModel::AMS_LITE) {
        render_lite_lib(dc);
    }
    else {
        render_generic_lib(dc);
    }
}

void AMSLib::render_lite_lib(wxDC& dc)
{
    wxSize size = GetSize();

    ScalableBitmap tray_bitmap = m_can_index <= 1 ? m_bitmap_extra_tray_left : m_bitmap_extra_tray_right;
    ScalableBitmap tray_bitmap_hover = m_can_index <= 1 ? m_bitmap_extra_tray_left_hover : m_bitmap_extra_tray_right_hover;
    ScalableBitmap tray_bitmap_selected = m_can_index <= 1 ? m_bitmap_extra_tray_left_selected : m_bitmap_extra_tray_right_selected;


    auto   tmp_lib_colour    = m_info.material_colour;
    change_the_opacity(tmp_lib_colour);

    auto   temp_bitmap_third = m_bitmap_editable_light;
    auto   temp_bitmap_brand = m_bitmap_readonly_light;

    //draw road


    dc.SetPen(wxPen(AMS_CONTROL_GRAY500, 2, wxPENSTYLE_SOLID));
    dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));

    if (m_pass_road) {
        dc.SetPen(wxPen(m_info.material_colour, 6, wxPENSTYLE_SOLID));
    }

    if (m_can_index == 0 || m_can_index == 3) {
        dc.DrawLine(size.x / 2, size.y / 2, size.x / 2, size.y);
    }
    else {
        dc.DrawLine(size.x / 2, size.y / 2, size.x / 2, 0);
    }

    //draw def background
    dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
    dc.SetBrush(wxBrush(StateColor::darkModeColorFor(AMS_CONTROL_DEF_LIB_BK_COLOUR)));
    dc.DrawRoundedRectangle(FromDIP(10), FromDIP(10), size.x - FromDIP(20), size.y - FromDIP(20), 0);

    if (tmp_lib_colour.GetLuminance() < 0.6) {
        temp_bitmap_third = m_bitmap_editable_light;
        temp_bitmap_brand = m_bitmap_readonly_light;
    }
    else {
        temp_bitmap_third = m_bitmap_editable;
        temp_bitmap_brand = m_bitmap_readonly;
    }

    if (m_info.material_remain < 50) {
        temp_bitmap_third = m_bitmap_editable;
        temp_bitmap_brand = m_bitmap_readonly;
    }

    if (tmp_lib_colour.Alpha() == 0) {
        temp_bitmap_third = m_bitmap_editable;
        temp_bitmap_brand = m_bitmap_readonly;
    }

    dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
    if (m_info.material_cols.size() > 1) {
        int left = FromDIP(10);
        int gwidth = std::round(size.x / (m_info.material_cols.size() - 1));
        //gradient
        if (m_info.ctype == 0) {
            for (int i = 0; i < m_info.material_cols.size() - 1; i++) {
                auto rect = wxRect(left, FromDIP(10), size.x - FromDIP(20), size.y - FromDIP(20));
                dc.GradientFillLinear(rect, m_info.material_cols[i], m_info.material_cols[i + 1], wxEAST);
                left += gwidth;
            }
        }
        else {
            int cols_size = m_info.material_cols.size();
            for (int i = 0; i < cols_size; i++) {
                dc.SetBrush(wxBrush(m_info.material_cols[i]));
                float x = FromDIP(10) + ((float)size.x - FromDIP(20)) * i / cols_size;
                dc.DrawRoundedRectangle(x, FromDIP(10), ((float)size.x - FromDIP(20)) / cols_size, size.y - FromDIP(20), 0);
            }
            dc.SetBrush(wxBrush(tmp_lib_colour));
        }
    }
    else  {
        dc.SetBrush(wxBrush(tmp_lib_colour));
        dc.DrawRoundedRectangle(FromDIP(10), FromDIP(10), size.x - FromDIP(20), size.y - FromDIP(20), 0);
    }
    dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
    dc.SetBrush(wxBrush(tmp_lib_colour));
    if (!m_disable_mode) {
        // edit icon
        if (m_info.material_state != AMSCanType::AMS_CAN_TYPE_EMPTY && m_info.material_state != AMSCanType::AMS_CAN_TYPE_NONE)
        {
            if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_THIRDBRAND || m_info.material_state == AMSCanType::AMS_CAN_TYPE_VIRTUAL)
                dc.DrawBitmap(temp_bitmap_third.bmp(), (size.x - temp_bitmap_third.GetBmpSize().x) / 2 + FromDIP(2), (size.y - FromDIP(18) - temp_bitmap_third.GetBmpSize().y));
            if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_BRAND)
                dc.DrawBitmap(temp_bitmap_brand.bmp(), (size.x - temp_bitmap_brand.GetBmpSize().x) / 2 + FromDIP(2), (size.y - FromDIP(18) - temp_bitmap_brand.GetBmpSize().y));
        }
    }

    // selected & hover
    if (m_selected) {
        dc.DrawBitmap(tray_bitmap_selected.bmp(), (size.x - tray_bitmap_selected.GetBmpSize().x) / 2, (size.y - tray_bitmap_selected.GetBmpSize().y) / 2);
    }
    else if (!m_selected && m_hover) {
        dc.DrawBitmap(tray_bitmap_hover.bmp(), (size.x - tray_bitmap_hover.GetBmpSize().x) / 2, (size.y - tray_bitmap_hover.GetBmpSize().y) / 2);
    }
    else {
        dc.DrawBitmap(tray_bitmap.bmp(), (size.x - tray_bitmap.GetBmpSize().x) / 2, (size.y - tray_bitmap.GetBmpSize().y) / 2);
    }
}


void AMSLib::render_generic_lib(wxDC &dc)
{
    wxSize size = GetSize();
    auto   tmp_lib_colour = m_info.material_colour;
    change_the_opacity(tmp_lib_colour);

    auto   temp_bitmap_third = m_bitmap_editable_light;
    auto   temp_bitmap_brand = m_bitmap_readonly_light;

    //draw def background
    dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
    dc.SetBrush(wxBrush(AMS_CONTROL_DEF_LIB_BK_COLOUR));
    dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4), size.x - FromDIP(8), size.y - FromDIP(8), m_radius);

    if (tmp_lib_colour.GetLuminance() < 0.6) {
        temp_bitmap_third = m_bitmap_editable_light;
        temp_bitmap_brand = m_bitmap_readonly_light;
    }
    else {
        temp_bitmap_third = m_bitmap_editable;
        temp_bitmap_brand = m_bitmap_readonly;
    }

    if (m_info.material_remain < 50) {
        temp_bitmap_third = m_bitmap_editable;
        temp_bitmap_brand = m_bitmap_readonly;
    }

    if (tmp_lib_colour.Alpha() == 0) {
        temp_bitmap_third = m_bitmap_editable;
        temp_bitmap_brand = m_bitmap_readonly;
    }

    // selected
    if (m_selected) {
        dc.SetPen(wxPen(tmp_lib_colour, 2, wxPENSTYLE_SOLID));
        if (tmp_lib_colour.Alpha() == 0) {
            dc.SetPen(wxPen(wxColour(tmp_lib_colour.Red(), tmp_lib_colour.Green(),tmp_lib_colour.Blue(),128), 2, wxPENSTYLE_SOLID));
        }
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, size.x, size.y);
        }
        else {
            dc.DrawRoundedRectangle(FromDIP(1), FromDIP(1), size.x - FromDIP(1), size.y - FromDIP(1), m_radius);
        }

        dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
        dc.SetBrush(wxBrush(tmp_lib_colour));
    }

    if (!m_selected && m_hover) {
        dc.SetPen(wxPen(AMS_CONTROL_BRAND_COLOUR, 2, wxPENSTYLE_SOLID));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        if (m_radius == 0) {
            dc.DrawRectangle(0, 0, size.x, size.y);
        }
        else {
            dc.DrawRoundedRectangle(FromDIP(1), FromDIP(1), size.x - FromDIP(1), size.y - FromDIP(1), m_radius);
        }

        dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
        dc.SetBrush(wxBrush(tmp_lib_colour));
    }
    else {
        dc.SetPen(wxPen(tmp_lib_colour, 1, wxPENSTYLE_SOLID));
        dc.SetBrush(wxBrush(tmp_lib_colour));
    }

    //draw remain
    auto alpha = m_info.material_colour.Alpha();
    int height = size.y - FromDIP(8);
    int curr_height = height * float(m_info.material_remain * 1.0 / 100.0); 
    dc.SetFont(::Label::Body_13);

    int top = height - curr_height;

    if (curr_height >= FromDIP(6)) {

        //transparent
        
        if (alpha == 0) {
            dc.DrawBitmap(m_bitmap_transparent.bmp(), FromDIP(4), FromDIP(4));
        }
        else if (alpha != 255 && alpha != 254) {
            if (transparent_changed) {
                std::string rgb = (tmp_lib_colour.GetAsString(wxC2S_HTML_SYNTAX)).ToStdString();
                if (rgb.size() == 9) {
                    //delete alpha value
                    rgb = rgb.substr(0, rgb.size() - 2);
                }
                float alpha_f = 0.7 * tmp_lib_colour.Alpha() / 255.0;
                std::vector<std::string> replace;
                replace.push_back(rgb);
                std::string fill_replace = "fill-opacity=\"" + std::to_string(alpha_f);
                replace.push_back(fill_replace);
                m_bitmap_transparent = ScalableBitmap(this, "transparent_ams_lib", 68, false, false, true, replace);
                transparent_changed = false;
                
            }
            dc.DrawBitmap(m_bitmap_transparent.bmp(), FromDIP(4), FromDIP(4));
        }
        //gradient
        if (m_info.material_cols.size() > 1) {
            int left = FromDIP(4);
            float total_width = size.x - FromDIP(8);
            int gwidth = std::round(total_width / (m_info.material_cols.size() - 1));
            //gradient
            if (m_info.ctype == 0) {
                for (int i = 0; i < m_info.material_cols.size() - 1; i++) {

                    if ((left + gwidth) > (size.x - FromDIP(8))) {
                        gwidth = (size.x - FromDIP(4)) - left;
                    }

                    auto rect = wxRect(left, height - curr_height + FromDIP(4), gwidth, curr_height);
                    dc.GradientFillLinear(rect, m_info.material_cols[i], m_info.material_cols[i + 1], wxEAST);
                    left += gwidth;
                }
            }
            else {
                //multicolour
                gwidth = std::round(total_width / m_info.material_cols.size());
                for (int i = 0; i < m_info.material_cols.size(); i++) {
                    dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
                    dc.SetBrush(wxBrush(m_info.material_cols[i]));
                    if (i == 0 || i == m_info.material_cols.size() - 1) {
#ifdef __APPLE__
                        dc.DrawRoundedRectangle(left + gwidth * i, height - curr_height + FromDIP(4), gwidth, curr_height, m_radius);
#else
                        dc.DrawRoundedRectangle(left + gwidth * i, height - curr_height + FromDIP(4), gwidth, curr_height, m_radius - 1);
#endif
                        //add rectangle
                        int dr_gwidth = std::round(gwidth * 0.6);
                        if (i == 0) {
                            dc.DrawRectangle(left + gwidth - dr_gwidth, height - curr_height + FromDIP(4), dr_gwidth, curr_height);
                        }
                        else {
                            dc.DrawRectangle(left + gwidth*i, height - curr_height + FromDIP(4), dr_gwidth, curr_height);
                        }
                    }
                    else {
                        dc.DrawRectangle(left + gwidth * i, height - curr_height + FromDIP(4), gwidth, curr_height);
                    }
                }
                //reset pen and brush
                if (m_selected || m_hover) {
                    dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
                    dc.SetBrush(wxBrush(tmp_lib_colour));
                }
                else {
                    dc.SetPen(wxPen(tmp_lib_colour, 1, wxPENSTYLE_SOLID));
                    dc.SetBrush(wxBrush(tmp_lib_colour));
                }
            }
        }
        else {
            auto brush = dc.GetBrush();
            if (alpha != 0 && alpha != 255 && alpha != 254) dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
#ifdef __APPLE__
            dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4) + top, size.x - FromDIP(8), curr_height, m_radius);
#else
            dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4) + top, size.x - FromDIP(8), curr_height, m_radius - 1);
#endif
            dc.SetBrush(brush);
        }
    }

    if (top > 2) {
        if (curr_height >= FromDIP(6)) {
            dc.DrawRectangle(FromDIP(4), FromDIP(4) + top, size.x - FromDIP(8), FromDIP(2));
            if (alpha != 255 && alpha != 254) {
                dc.SetPen(wxPen(*wxWHITE));
                dc.SetBrush(wxBrush(*wxWHITE));
#ifdef __APPLE__
                dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4) , size.x - FromDIP(8), top, m_radius);
#else
                dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4) , size.x - FromDIP(8), top, m_radius - 1);
#endif
            }
            if (tmp_lib_colour.Red() > 238 && tmp_lib_colour.Green() > 238 && tmp_lib_colour.Blue() > 238) {
                dc.SetPen(wxPen(wxColour(130, 129, 128), 1, wxPENSTYLE_SOLID));
                dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
                dc.DrawLine(FromDIP(4), FromDIP(4) + top, size.x - FromDIP(4), FromDIP(4) + top);
            }
        }
        else {
            dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
            if (tmp_lib_colour.Red() > 238 && tmp_lib_colour.Green() > 238 && tmp_lib_colour.Blue() > 238) {
                dc.SetPen(wxPen(wxColour(130, 129, 128), 2, wxPENSTYLE_SOLID));
            }
            else {
                dc.SetPen(wxPen(tmp_lib_colour, 2, wxPENSTYLE_SOLID));
            }

#ifdef __APPLE__
            dc.DrawLine(FromDIP(5), FromDIP(4) + height - FromDIP(2), size.x - FromDIP(5), FromDIP(4) + height - FromDIP(2));
            dc.DrawLine(FromDIP(6), FromDIP(4) + height - FromDIP(1), size.x - FromDIP(6), FromDIP(4) + height - FromDIP(1));
#else
            dc.DrawLine(FromDIP(4), FromDIP(4) + height - FromDIP(2), size.x - FromDIP(4), FromDIP(4) + height - FromDIP(2));
            dc.DrawLine(FromDIP(5), FromDIP(4) + height - FromDIP(1), size.x - FromDIP(5), FromDIP(4) + height - FromDIP(1));
#endif
        }
    }

    //border
    dc.SetPen(wxPen(wxColour(130, 130, 128), 1, wxPENSTYLE_SOLID));
    dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
#ifdef __APPLE__
    dc.DrawRoundedRectangle(FromDIP(4), FromDIP(4), size.x - FromDIP(7), size.y - FromDIP(7), m_radius);
#else
    dc.DrawRoundedRectangle(FromDIP(3), FromDIP(3), size.x - FromDIP(6), size.y - FromDIP(6), m_radius);
#endif

    if (!m_disable_mode) {
        // edit icon
        if (m_info.material_state != AMSCanType::AMS_CAN_TYPE_EMPTY && m_info.material_state != AMSCanType::AMS_CAN_TYPE_NONE)
        {
            if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_THIRDBRAND || m_info.material_state == AMSCanType::AMS_CAN_TYPE_VIRTUAL)
                dc.DrawBitmap(temp_bitmap_third.bmp(), (size.x - temp_bitmap_third.GetBmpSize().x) / 2, (size.y - FromDIP(10) - temp_bitmap_third.GetBmpSize().y));
            if (m_info.material_state == AMSCanType::AMS_CAN_TYPE_BRAND)
                dc.DrawBitmap(temp_bitmap_brand.bmp(), (size.x - temp_bitmap_brand.GetBmpSize().x) / 2, (size.y - FromDIP(10) - temp_bitmap_brand.GetBmpSize().y));
        }
    }
}

void AMSLib::on_pass_road(bool pass)
{
    if (m_pass_road != pass) {
        m_pass_road = pass;
        Refresh();
    }
}

void AMSLib::Update(Caninfo info, std::string ams_idx, bool refresh)
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;
    if (dev->get_selected_machine() && dev->get_selected_machine() != m_obj) {
        m_obj = dev->get_selected_machine();
    }
    if (info.material_colour.Alpha() != 0 && info.material_colour.Alpha() != 255 && info.material_colour.Alpha() != 254 && m_info.material_colour != info.material_colour) {
        transparent_changed = true;
    }

    m_info = info;
    m_ams_id = ams_idx;
    m_slot_id = info.can_id;
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
    //int tray_id = atoi(m_ams_id.c_str()) * 4 + atoi(m_info.can_id.c_str());
    //event.SetString(m_info.can_id);
    event.SetString(m_slot_id);
    event.SetInt(std::stoi(m_ams_id));
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

void AMSLib::msw_rescale()
{
    m_bitmap_transparent.msw_rescale();
}

/*************************************************
Description:AMSRoad
**************************************************/
AMSRoad::AMSRoad() : m_road_def_color(AMS_CONTROL_GRAY500), m_road_color(AMS_CONTROL_GRAY500) {}
AMSRoad::AMSRoad(wxWindow *parent, wxWindowID id, Caninfo info, int canindex, int maxcan, const wxPoint &pos, const wxSize &size)
    : AMSRoad()
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
    } else if (m_canindex == -1 && maxcan == -1) {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_VIRTUAL_TRAY;
    }
    else {
        m_rode_mode = AMSRoadMode::AMS_ROAD_MODE_NONE_ANY_ROAD;
    }

    if (m_rode_mode != AMSRoadMode::AMS_ROAD_MODE_VIRTUAL_TRAY) {
        create(parent, id, pos, size);
    }
    else {
        wxSize virtual_size(size.x - 1, size.y + 2);
        create(parent, id, pos, virtual_size);

    }

    Bind(wxEVT_PAINT, &AMSRoad::paintEvent, this);
    wxWindow::SetBackgroundColour(StateColor::darkModeColorFor(AMS_CONTROL_DEF_LIB_BK_COLOUR));
}

void AMSRoad::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size) { wxWindow::Create(parent, id, pos, size); }

void AMSRoad::Update(AMSinfo amsinfo, Caninfo info, int canindex, int maxcan)
{
    m_amsinfo = amsinfo;
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

void AMSRoad::OnVamsLoading(bool load, wxColour col /*= AMS_CONTROL_GRAY500*/)
{
    m_vams_loading = load;
    if(load)m_road_color = col;
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

    dc.SetPen(wxPen(m_road_def_color, 2, wxPENSTYLE_SOLID));
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

    //virtual road
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_VIRTUAL_TRAY) {
        dc.SetBrush(wxBrush(m_road_def_color));
        dc.DrawLine(size.x / 2, -1, size.x / 2, size.y - 1);
    }

    // mode none
    // if (m_pass_rode_mode.size() == 1 && m_pass_rode_mode[0] == AMSPassRoadMode::AMS_ROAD_MODE_NONE) return;

    if (m_road_color.Alpha() == 0) {dc.SetPen(wxPen(*wxWHITE, m_passroad_width, wxPENSTYLE_SOLID));}
    else {dc.SetPen(wxPen(m_road_color, m_passroad_width, wxPENSTYLE_SOLID));}

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

    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_VIRTUAL_TRAY && m_vams_loading) {
        dc.DrawLine(size.x / 2, -1, size.x / 2, size.y - 1);
    }

    // end mode
    if (m_rode_mode == AMSRoadMode::AMS_ROAD_MODE_END) {
        dc.SetPen(wxPen(m_road_def_color, 2, wxPENSTYLE_SOLID));
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
}


/*************************************************
Description:AMSPreview
**************************************************/
AMSPreview::AMSPreview() {}

AMSPreview::AMSPreview(wxWindow *parent, wxWindowID id, AMSinfo amsinfo, const wxSize cube_size, const wxPoint &pos, const wxSize &size) : AMSPreview()
{
    create(parent, id, pos, AMS_ITEM_SIZE);
    m_amsinfo = amsinfo;
    m_cube_size = cube_size;
    Bind(wxEVT_PAINT, &AMSPreview::paintEvent, this);
    Bind(wxEVT_ENTER_WINDOW, &AMSPreview::OnEnterWindow, this);
    Bind(wxEVT_LEAVE_WINDOW, &AMSPreview::OnLeaveWindow, this);
}

void AMSPreview::Open()
{
    m_open = true;
    Show();
}

void AMSPreview::Close()
{
    m_open = false;
    Hide();
}

void AMSPreview::Update(AMSinfo amsinfo)
{
    m_amsinfo = amsinfo;
}

void AMSPreview::create(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    m_ts_bitmap_cube = new ScalableBitmap(this, "ts_bitmap_cube", 14);
    wxWindow::Create(parent, id, pos, size);
    SetMinSize(AMS_ITEM_SIZE);
    SetMaxSize(AMS_ITEM_SIZE);
    SetBackgroundColour(StateColor::darkModeColorFor(AMS_CONTROL_WHITE_COLOUR));
    Refresh();
}

void AMSPreview::OnEnterWindow(wxMouseEvent &evt)
{
    // m_hover = true;
    // Refresh();
}

void AMSPreview::OnLeaveWindow(wxMouseEvent &evt)
{
    // m_hover = false;
    // Refresh();
}

void AMSPreview::OnSelected()
{
    if (!wxWindow::IsEnabled()) { return; }
    m_selected = true;
    Refresh();
}

void AMSPreview::UnSelected()
{
    m_selected = false;
    Refresh();
}

bool AMSPreview::Enable(bool enable) { return wxWindow::Enable(enable); }

void AMSPreview::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AMSPreview::render(wxDC &dc)
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

void AMSPreview::doRender(wxDC &dc)
{
    wxSize size = GetSize();
    dc.SetPen(wxPen(StateColor::darkModeColorFor(m_background_colour)));
    dc.SetBrush(wxBrush(StateColor::darkModeColorFor(m_background_colour)));
    dc.DrawRoundedRectangle(0, 0, size.x, size.y, 3);

    auto left = m_padding;
    for (std::vector<Caninfo>::iterator iter = m_amsinfo.cans.begin(); iter != m_amsinfo.cans.end(); iter++) {
        dc.SetPen(wxPen(*wxTRANSPARENT_PEN));

        if (wxWindow::IsEnabled()) {
            wxColour color = iter->material_colour;
            change_the_opacity(color);
            dc.SetBrush(wxBrush(color));
        } else {
            dc.SetBrush(AMS_CONTROL_DISABLE_COLOUR);
        }

        if (iter->material_cols.size() > 1) {
            int fleft = left;
            float total_width = AMS_ITEM_CUBE_SIZE.x;
            int gwidth = std::round(total_width / (iter->material_cols.size() - 1));
            if (iter->ctype == 0) {
                for (int i = 0; i < iter->material_cols.size() - 1; i++) {

                    if ((fleft + gwidth) > (AMS_ITEM_CUBE_SIZE.x)) {
                        gwidth = (fleft + AMS_ITEM_CUBE_SIZE.x) - fleft;
                    }

                    auto rect = wxRect(fleft, (size.y - AMS_ITEM_CUBE_SIZE.y) / 2, gwidth, AMS_ITEM_CUBE_SIZE.y);
                    dc.GradientFillLinear(rect, iter->material_cols[i], iter->material_cols[i + 1], wxEAST);
                    fleft += gwidth;
                }
            } else {
                int cols_size = iter->material_cols.size();
                for (int i = 0; i < cols_size; i++) {
                    dc.SetBrush(wxBrush(iter->material_cols[i]));
                    float x = left + total_width * i / cols_size;
                    dc.DrawRoundedRectangle(x, (size.y - AMS_ITEM_CUBE_SIZE.y) / 2, total_width / cols_size, AMS_ITEM_CUBE_SIZE.y , 0);
                }
            }

            dc.SetPen(wxPen(StateColor::darkModeColorFor(m_background_colour)));
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.DrawRoundedRectangle(left - 1, (size.y - AMS_ITEM_CUBE_SIZE.y) / 2 - 1, AMS_ITEM_CUBE_SIZE.x + 2, AMS_ITEM_CUBE_SIZE.y + 2, 2);

        }else {
            if (iter->material_colour.Alpha() == 0) {
                dc.DrawBitmap(m_ts_bitmap_cube->bmp(),left,(size.y - AMS_ITEM_CUBE_SIZE.y) / 2);
            }
            else {
                wxRect rect(left, (size.y - AMS_ITEM_CUBE_SIZE.y) / 2, AMS_ITEM_CUBE_SIZE.x, AMS_ITEM_CUBE_SIZE.y);
                if(iter->material_state==AMSCanType::AMS_CAN_TYPE_EMPTY){
                    dc.SetPen(wxPen(wxColor(0, 0, 0)));
                    dc.DrawRoundedRectangle(rect, 2);
                    
                    dc.DrawLine(rect.GetRight()-1, rect.GetTop()+1, rect.GetLeft()+1, rect.GetBottom()-1); 
                } 
                else {
                    dc.DrawRoundedRectangle(rect, 2);
                }
            }
            
        }

        
        left += AMS_ITEM_CUBE_SIZE.x;
        left += m_space;
    }

    auto border_colour = AMS_CONTROL_BRAND_COLOUR;
    if (!wxWindow::IsEnabled()) { border_colour = AMS_CONTROL_DISABLE_COLOUR; }

    if (m_hover) {
        dc.SetPen(wxPen(border_colour, 2));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        dc.DrawRoundedRectangle(1, 1, size.x - 1, size.y - 1, 3);

    }

    if (m_selected) {
        dc.SetPen(wxPen(border_colour, 2));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        dc.DrawRoundedRectangle(1, 1, size.x-1, size.y-1, 3);
    }
}

void AMSPreview::DoSetSize(int x, int y, int width, int height, int sizeFlags /*= wxSIZE_AUTO*/) { wxWindow::DoSetSize(x, y, width, height, sizeFlags); }

/*************************************************
Description:AMSHumidity
**************************************************/

AMSHumidity::AMSHumidity() {}
AMSHumidity::AMSHumidity(wxWindow* parent, wxWindowID id, AMSinfo info, const wxPoint& pos, const wxSize& size)
    : AMSHumidity()
{
    create(parent, id, pos, wxDefaultSize);

    for (int i = 1; i <= 5; i++) { ams_humidity_imgs.push_back(ScalableBitmap(this, "hum_level" + std::to_string(i) + "_light", 16));}
    for (int i = 1; i <= 5; i++) { ams_humidity_dark_imgs.push_back(ScalableBitmap(this, "hum_level" + std::to_string(i) + "_dark", 16));}
    for (int i = 1; i <= 5; i++) { ams_humidity_no_num_imgs.push_back(ScalableBitmap(this, "hum_level" + std::to_string(i) + "_no_num_light", 16)); }
    for (int i = 1; i <= 5; i++) { ams_humidity_no_num_dark_imgs.push_back(ScalableBitmap(this, "hum_level" + std::to_string(i) + "_no_num_dark", 16)); }

    ams_sun_img = ScalableBitmap(this, "ams_drying", 16);
    ams_drying_img = ScalableBitmap(this, "ams_is_drying", 16);

    Bind(wxEVT_PAINT, &AMSHumidity::paintEvent, this);
    //wxWindow::SetBackgroundColour(AMS_CONTROL_DEF_HUMIDITY_BK_COLOUR);

    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
        if (m_show_humidity) {
            if (m_amsinfo.ams_type == AMSModel::GENERIC_AMS) {
                return;/*STUDIO-12083*/
            }

            auto mouse_pos = ClientToScreen(e.GetPosition());
            auto rect = ClientToScreen(wxPoint(0, 0));

            if (mouse_pos.x > rect.x &&
                mouse_pos.y > rect.y) {
                wxCommandEvent show_event(EVT_AMS_SHOW_HUMIDITY_TIPS);

                uiAmsHumidityInfo *info = new uiAmsHumidityInfo;
                info->ams_id            = m_amsinfo.ams_id;
                info->humidity_level    = m_amsinfo.ams_humidity;
                info->humidity_percent  = m_amsinfo.humidity_raw;
                info->left_dry_time     = m_amsinfo.left_dray_time;
                info->current_temperature = m_amsinfo.current_temperature;
                show_event.SetClientData(info);
                wxPostEvent(GetParent()->GetParent(), show_event);

#ifdef __WXMSW__
                wxCommandEvent close_event(EVT_CLEAR_SPEED_CONTROL);
                wxPostEvent(GetParent()->GetParent(), close_event);
#endif // __WXMSW__

            }
        }
        });

    Update(info);
}

void AMSHumidity::create(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size) {
    wxWindow::Create(parent, id, pos, size);
    SetBackgroundColour(StateColor::darkModeColorFor(AMS_CONTROL_DEF_LIB_BK_COLOUR));
}


void AMSHumidity::Update(AMSinfo amsinfo)
{
    if (m_amsinfo != amsinfo)
    {
        m_amsinfo = amsinfo;
        update_size();
        Refresh();
    }
}

void AMSHumidity::update_size()
{
    wxSize size;
    if (m_amsinfo.humidity_raw != -1) {
        size = AMS_HUMIDITY_SIZE;
    } else {
        size = AMS_HUMIDITY_NO_PERCENT_SIZE;
    }

    if (!m_amsinfo.support_drying()) { size.x -= AMS_HUMIDITY_DRY_WIDTH; }

    SetMaxSize(size);
    SetMinSize(size);
    SetSize(size);
}


void AMSHumidity::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AMSHumidity::render(wxDC& dc)
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

void AMSHumidity::doRender(wxDC& dc)
{
    wxSize size = GetSize();

    dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
    dc.SetBrush(wxBrush(StateColor::darkModeColorFor(AMS_CONTROL_DEF_BLOCK_BK_COLOUR)));
    // left mode
    if (m_amsinfo.ams_humidity >= 1 && m_amsinfo.ams_humidity <= 5) { m_show_humidity = true; }
    else { m_show_humidity = false; }

    if (m_show_humidity) {
        //background
        dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
        dc.SetBrush(wxBrush(StateColor::darkModeColorFor(AMS_CONTROL_DEF_BLOCK_BK_COLOUR)));
        dc.DrawRoundedRectangle(0, 0, (size.x), (size.y), (size.y / 2));

        wxPoint pot;
        if (m_amsinfo.humidity_raw != -1) /*image with no number + percentage*/
        {
            // hum image
            ScalableBitmap hum_img;
            if (!wxGetApp().dark_mode()) {
                hum_img = ams_humidity_no_num_imgs[m_amsinfo.ams_humidity - 1];
            } else {
                hum_img = ams_humidity_no_num_dark_imgs[m_amsinfo.ams_humidity - 1];
            }

            pot = wxPoint(FromDIP(5), ((size.y - hum_img.GetBmpSize().y) / 2));
            dc.DrawBitmap(hum_img.bmp(), pot);
            pot.x += hum_img.GetBmpSize().x + FromDIP(3);

            // percentage
            wxString hum_percentage(std::to_string(m_amsinfo.humidity_raw));
            dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
            dc.SetFont(Label::Body_14);
            dc.SetTextForeground(StateColor::darkModeColorFor(AMS_CONTROL_BLACK_COLOUR));

            //WxFontUtils::get_suitable_font_size(0.7 * size.GetHeight(), dc);
            auto tsize1 = dc.GetMultiLineTextExtent(hum_percentage);
            pot.y = (size.y - tsize1.y) / 2;
            dc.DrawText(hum_percentage, pot);
            pot.x += (tsize1.x + FromDIP(3));

            // percentage sign
            dc.SetFont(Label::Body_12);
            //WxFontUtils::get_suitable_font_size(0.5 * size.GetHeight(), dc);
            auto tsize2 = dc.GetMultiLineTextExtent(_L("%"));
            pot.y = pot.y + ((tsize1.y - tsize2.y) / 2) + FromDIP(2);
            dc.DrawText(_L("%"), pot);

            pot.x += tsize2.x;
        }
        else /*image with number*/
        {
            // hum image
            ScalableBitmap hum_img;
            if (!wxGetApp().dark_mode()) {
                hum_img = ams_humidity_imgs[m_amsinfo.ams_humidity - 1];
            } else {
                hum_img = ams_humidity_dark_imgs[m_amsinfo.ams_humidity - 1];
            }

            pot = wxPoint(FromDIP(5), ((size.y - hum_img.GetBmpSize().y) / 2));
            dc.DrawBitmap(hum_img.bmp(), pot);
            pot.x = pot.x + hum_img.GetBmpSize().x;
        }

        if (m_amsinfo.support_drying())
        {
            pot.x += FromDIP(2);// spacing

            // vertical line
            dc.SetPen(wxPen(wxColour(194, 194, 194)));
            dc.SetBrush(wxBrush(wxColour(194, 194, 194)));
            dc.DrawLine(pot.x, GetSize().y / 2 - FromDIP(10), pot.x, GetSize().y / 2 + FromDIP(10));

            // sun image
            dc.SetPen(wxPen(*wxTRANSPARENT_PEN));
            pot.x += ((size.GetWidth() - pot.x) - ams_drying_img.GetBmpWidth()) / 2;// spacing
            if (m_amsinfo.left_dray_time > 0) {
                pot.y = (size.y - ams_drying_img.GetBmpHeight()) / 2;
                dc.DrawBitmap(ams_drying_img.bmp(), pot);
            } else {
                pot.y = (size.y - ams_sun_img.GetBmpHeight()) / 2;
                dc.DrawBitmap(ams_sun_img.bmp(), pot);
            }
        }
    }
    else {
        //to do ...
    }
}

void AMSHumidity::msw_rescale() {
    for (auto& img : ams_humidity_imgs) { img.msw_rescale();}
    for (auto& img : ams_humidity_dark_imgs) { img.msw_rescale(); }
    for (auto &img : ams_humidity_no_num_imgs) { img.msw_rescale(); }
    for (auto &img : ams_humidity_no_num_dark_imgs) { img.msw_rescale(); }
    ams_sun_img.msw_rescale();
    ams_drying_img.msw_rescale();

    Layout();
    Refresh();
}


/*************************************************
Description:AmsItem
**************************************************/

AmsItem::AmsItem() {}

AmsItem::AmsItem(wxWindow *parent,AMSinfo info,  AMSModel model) : AmsItem()
{
    m_bitmap_extra_framework = ScalableBitmap(this, "ams_extra_framework_mid", 140);

    SetDoubleBuffered(true);
    m_ams_model = model;
    m_info      = info;

    wxWindow::Create(parent, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE);

    create(parent);

    SetBackgroundColour(StateColor::darkModeColorFor(AMS_CONTROL_DEF_LIB_BK_COLOUR));
    Bind(wxEVT_PAINT, &AmsItem::paintEvent, this);
}

void AmsItem::create(wxWindow *parent)
{
    Freeze();
    if (m_ams_model == AMSModel::GENERIC_AMS || m_ams_model == AMSModel::N3F_AMS || m_ams_model == AMSModel::N3S_AMS) {
        sizer_can = new wxBoxSizer(wxHORIZONTAL);
        sizer_item = new wxBoxSizer(wxVERTICAL);
        for (auto it = m_info.cans.begin(); it != m_info.cans.end(); it++) {
            AddCan(*it, m_can_count, m_info.cans.size(), sizer_can);
            m_can_count++;
        }
        m_humidity = new AMSHumidity(this, wxID_ANY, m_info);
        sizer_item->Add(m_humidity, 0, wxALIGN_CENTER_HORIZONTAL, 0);
        sizer_item->Add(sizer_can, 0, wxALIGN_LEFT, 0);
        SetSizer(sizer_item);
    }
    else if(m_ams_model == AMSModel::AMS_LITE) {
        sizer_can = new wxBoxSizer(wxVERTICAL);
        sizer_can_middle = new wxBoxSizer(wxHORIZONTAL);
        sizer_can_left = new wxBoxSizer(wxVERTICAL);
        sizer_can_right = new wxBoxSizer(wxVERTICAL);

        sizer_can_left->Add(0,0,0,wxTOP,FromDIP(8));

        for (auto it = m_info.cans.begin(); it != m_info.cans.end(); it++) {
            if (m_can_count <= 1) {
                AddCan(*it, m_can_count, m_info.cans.size(), sizer_can_left);
                if (m_can_count == 0) {
                    sizer_can_left->Add(0,0,0,wxTOP,FromDIP(20));
                }
            }
            else {
                AddCan(*it, m_can_count, m_info.cans.size(), sizer_can_right);
                if (m_can_count == 2) {
                   sizer_can_right->Prepend(0, 0, 0, wxTOP, FromDIP(20));
                }
            }

            m_can_count++;
        }

        sizer_can_right->Prepend(0,0,0,wxTOP,FromDIP(8));
        sizer_can_middle->Add(0, 0, 0, wxLEFT, FromDIP(8));
        sizer_can_middle->Add(sizer_can_left, 0, wxALL, 0);
        sizer_can_middle->Add( 0, 0, 0, wxLEFT, FromDIP(20) );
        sizer_can_middle->Add(sizer_can_right, 0, wxALL, 0);
        sizer_can->Add(sizer_can_middle, 1, wxALIGN_CENTER, 0);
        SetSizer(sizer_can);
    }


    Layout();
    Fit();
    Thaw();
}

void AmsItem::AddCan(Caninfo caninfo, int canindex, int maxcan, wxBoxSizer* sizer)
{
    auto        amscan = new wxWindow(this, wxID_ANY);

    amscan->SetBackgroundColour(StateColor::darkModeColorFor(AMS_CONTROL_DEF_LIB_BK_COLOUR));

    wxBoxSizer* m_sizer_ams = new wxBoxSizer(wxVERTICAL);
   

    auto m_panel_refresh = new AMSrefresh(amscan, m_can_count, caninfo);
    m_can_refresh_list[caninfo.can_id] = m_panel_refresh;

    auto m_panel_lib = new AMSLib(amscan, m_info.ams_id, caninfo);

    m_panel_lib->Bind(wxEVT_LEFT_DOWN, [this, canindex](wxMouseEvent& ev) {
        m_canlib_selection = canindex;
        // m_canlib_id        = caninfo.can_id;

        for (auto lib_it : m_can_lib_list) {
            AMSLib* lib = lib_it.second;
            if (lib->m_can_index == m_canlib_selection) {
                wxCommandEvent evt(EVT_AMS_UNSELETED_VAMS);
                evt.SetString(m_info.ams_id);
                wxPostEvent(GetParent()->GetParent(), evt);
                lib->OnSelected();
            }
            else {
                lib->UnSelected();
            }
        }
        ev.Skip();
        });

    m_panel_lib->m_ams_model = m_ams_model;
    m_panel_lib->m_ams_id = m_info.ams_id;
    m_panel_lib->m_slot_id = caninfo.can_id;
    m_panel_lib->m_info.can_id = caninfo.can_id;
    m_panel_lib->m_can_index = canindex;

    auto m_panel_road = new AMSRoad(amscan, wxID_ANY, caninfo, canindex, maxcan, wxDefaultPosition, AMS_CAN_ROAD_SIZE);

    if (m_ams_model != AMSModel::AMS_LITE && m_ams_model != AMSModel::EXT_AMS) {
        //m_sizer_ams->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(2));
        m_sizer_ams->Add(m_panel_refresh, 0, wxALIGN_CENTER_HORIZONTAL, 0);
        //m_sizer_ams->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(2));
        m_sizer_ams->Add(m_panel_lib, 1, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, FromDIP(3));
        m_sizer_ams->Add(m_panel_road, 0, wxALL, 0);
    }
    else if (m_ams_model == AMSModel::AMS_LITE)
    {
        m_sizer_ams = new wxBoxSizer(wxHORIZONTAL);
        m_panel_road->Hide();

        if (canindex <= 1) {
            m_sizer_ams->Add(m_panel_refresh, 0, wxALIGN_CENTER, 0);
            m_sizer_ams->Add(m_panel_lib, 0, wxALIGN_CENTER, 0);
        }
        else {
            m_sizer_ams->Add(m_panel_lib, 0, wxALIGN_CENTER, 0);
            m_sizer_ams->Add(m_panel_refresh, 0, wxALIGN_CENTER, 0);
        }
    }
    amscan->SetSizer(m_sizer_ams);
    amscan->Layout();
    amscan->Fit();

    if (m_ams_model != AMSModel::AMS_LITE && m_ams_model != AMSModel::EXT_AMS) {
         sizer->Add(amscan, 0, wxALL, 0);
    }
    else if (m_ams_model == AMSModel::AMS_LITE)
    {
        if (canindex > 1) {
            sizer->Prepend(amscan, 0, wxALL, 0);
        }
        else {
            sizer->Add(amscan, 0, wxALL, 0);
        }
    }

    m_can_lib_list[caninfo.can_id] = m_panel_lib;
    m_can_road_list[caninfo.can_id] = m_panel_road;
}

void AmsItem::Update(AMSinfo info)
{
    m_info      = info;
    m_can_count = info.cans.size();

    if (m_humidity)
    {
        m_humidity->Update(m_info);
    }

    for (int i = 0; i < m_can_count; i++) {
        auto it = m_can_refresh_list.find(std::to_string(i));
        if (it == m_can_refresh_list.end()) break;

        auto refresh = it->second;
        if (refresh != nullptr){
            refresh->Update(info.ams_id, info.cans[i]);
            refresh->Show();
        }
    }

    for (int i = 0; i < m_can_lib_list.size(); i++) {
        AMSLib* lib = m_can_lib_list[std::to_string(i)];
        if (lib != nullptr){
            if (i < m_can_count){
                lib->Update(info.cans[i], info.ams_id);
                lib->Show();
            }
            else{
                lib->Hide();
            }
        }
    }

    if (m_ams_model != AMSModel::AMS_LITE) {
        for (auto i = 0; i < m_can_road_list.size(); i++) {
            AMSRoad* road = m_can_road_list[std::to_string(i)];
            if (road != nullptr) {
                if (i < m_can_count) {
                    road->Update(m_info, info.cans[i], i, m_can_count);
                    road->Show();
                } else {
                    road->Hide();
                }
            }
        }
    }

    Layout();
}

void AmsItem::SetDefSelectCan()
{
    for (auto lib_it : m_can_lib_list) {
        AMSLib* lib = lib_it.second;
        m_canlib_selection =lib->m_can_index;
        m_canlib_id = lib->m_info.can_id;
        SelectCan(m_canlib_id);
        break;
    }
}


void AmsItem::SelectCan(std::string canid)
{
    for (auto lib_it : m_can_lib_list) {
        AMSLib* lib = lib_it.second;
        if (lib->m_info.can_id == canid) {
            m_canlib_selection = lib->m_can_index;
        }
    }

    m_canlib_id = canid;

    for (auto lib_it : m_can_lib_list) {
        AMSLib* lib = lib_it.second;
        if (lib->m_info.can_id == m_canlib_id) {
            wxCommandEvent evt(EVT_AMS_UNSELETED_VAMS);
            evt.SetString(m_info.ams_id);
            wxPostEvent(GetParent()->GetParent(), evt);
            lib->OnSelected();
        } else {
            lib->UnSelected();
        }
    }
}

wxColour AmsItem::GetTagColr(wxString canid)
{
    auto tag_colour = *wxWHITE;
    for (auto lib_it : m_can_lib_list) {
        AMSLib* lib = lib_it.second;
        if (canid == lib->m_info.can_id) tag_colour = lib->GetLibColour();
    }
    return tag_colour;
}

void AmsItem::SetAmsStepExtra(wxString canid, AMSPassRoadType type, AMSPassRoadSTEP step)
{
    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1) {
        SetAmsStep(canid.ToStdString());
    }else if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2) {
        SetAmsStep(canid.ToStdString());
    }else if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3) {
        SetAmsStep(canid.ToStdString());
    }else if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
        SetAmsStep("");
    }
}

void AmsItem::SetAmsStep(wxString canid, AMSPassRoadType type, AMSPassRoadSTEP step)
{
    if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
        for (auto road_it : m_can_road_list) {
            AMSRoad* road = road_it.second;
            auto      pr   = std::vector<AMSPassRoadMode>{};
            pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_NONE);
            road->OnPassRoad(pr);
        }

        return;
    }

    
    auto tag_can_index = -1;
    for (auto road_it : m_can_road_list) {
        AMSRoad* road = road_it.second;
        if (canid == road->m_info.can_id) { tag_can_index = road->m_canindex; }
    }
    if (tag_can_index == -1) return;

    // get colour
    auto tag_colour = *wxWHITE;
    for (auto lib_it : m_can_lib_list) {
        AMSLib* lib = lib_it.second;
        if (canid == lib->m_info.can_id) tag_colour = lib->GetLibColour();
    }

    // unload
    if (type == AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD) {
        for (auto road_it : m_can_road_list) {
            AMSRoad* road = road_it.second;

            auto index = road->m_canindex;
            auto pr    = std::vector<AMSPassRoadMode>{};

            pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_BOTTOM);
            if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_2) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_BOTTOM); }

            if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_3) {
                if (index == tag_can_index && index > 0) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_LEFT); }
                if (index < tag_can_index && index > 0) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_LEFT_RIGHT); }
                if (index == 0 && tag_can_index == index) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_TOP); }
                if (index == 0 && tag_can_index > index) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_RIGHT); }
            }

            road->SetPassRoadColour(tag_colour);
            road->OnPassRoad(pr);
        }
    }

    // load
    if (type == AMSPassRoadType::AMS_ROAD_TYPE_LOAD) {
        for (auto road_it : m_can_road_list) {
            AMSRoad* road = road_it.second;

            auto index = road->m_canindex;
            auto pr    = std::vector<AMSPassRoadMode>{};

            if (index == tag_can_index && index > 0) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_LEFT); }
            if (index < tag_can_index && index > 0) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_LEFT_RIGHT); }
            if (index == 0 && tag_can_index == index) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_TOP); }
            if (index == 0 && tag_can_index > index) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_RIGHT); }

            if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_2) { pr.push_back(AMSPassRoadMode::AMS_ROAD_MODE_END_BOTTOM); }

            road->SetPassRoadColour(tag_colour);
            road->OnPassRoad(pr);
        }
    }
}

void AmsItem::SetAmsStep(std::string can_id)
{
    if (m_road_canid != can_id) {
        m_road_canid = can_id;
        Refresh();
    }
    for (auto lib : m_can_lib_list){
        if (lib.second->m_info.can_id == can_id){
            lib.second->on_pass_road(true);
        }
        else{
            lib.second->on_pass_road(false);
        }
    }
}

void AmsItem::PlayRridLoading(wxString canid)
{
    for (auto refresh_it : m_can_refresh_list) {
        AMSrefresh* refresh = refresh_it.second;
        if (refresh->GetCanId() == canid) { refresh->PlayLoading(); }
    }
}

std::string AmsItem::GetCurrentCan()
{
    if (m_canlib_selection < 0)
        return "";

    return wxString::Format("%d", m_canlib_selection).ToStdString();
}

void AmsItem::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void AmsItem::render(wxDC& dc)
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

void AmsItem::doRender(wxDC& dc)
{
    wxSize     size = GetSize();

    //road for extra
    if (m_ams_model == AMSModel::AMS_LITE) {
        dc.DrawBitmap(m_bitmap_extra_framework.bmp(), (size.x - m_bitmap_extra_framework.GetBmpSize().x) / 2, (size.y - m_bitmap_extra_framework.GetBmpSize().y) / 2);

        // A1
        dc.SetPen(wxPen(AMS_CONTROL_GRAY500, 2, wxPENSTYLE_SOLID));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        RenderLiteRoad(dc, size);
    }
}

void AmsItem::RenderLiteRoad(wxDC& dc, wxSize size) {
    auto end_top = size.x / 2 - FromDIP(99);
    auto passroad_width = 6;
    auto a1_top = size.y / 2 - FromDIP(4);
    auto a2_top = size.y / 2 + FromDIP(8);
    auto a3_top = size.y / 2 + FromDIP(4);
    auto a4_top = size.y / 2;

    try
    {
        auto a1_left = m_can_lib_list["0"]->GetScreenPosition().x + m_can_lib_list["0"]->GetSize().x / 2;
        auto local_pos1 = GetScreenPosition().x + GetSize().x / 2;
        a1_left = size.x / 2 + (a1_left - local_pos1);
        dc.DrawLine(a1_left, FromDIP(30), a1_left, a1_top);
        dc.DrawLine(a1_left, a1_top, end_top, a1_top);

        // A2
        auto a2_left = m_can_lib_list["1"]->GetScreenPosition().x + m_can_lib_list["1"]->GetSize().x / 2;
        auto local_pos2 = GetScreenPosition().x + GetSize().x / 2;
        a2_left = size.x / 2 + (a2_left - local_pos2);
        dc.DrawLine(a2_left, FromDIP(160), a2_left, a2_top);
        dc.DrawLine(a2_left, a2_top, end_top, a2_top);

        // A3
        auto a3_left = m_can_lib_list["2"]->GetScreenPosition().x + m_can_lib_list["2"]->GetSize().x / 2;
        auto local_pos3 = GetScreenPosition().x + GetSize().x / 2;
        a3_left = size.x / 2 + (a3_left - local_pos3);
        dc.DrawLine(a3_left, FromDIP(160), a3_left, a3_top);
        dc.DrawLine(a3_left, a3_top, end_top, a3_top);


        //// A4
        auto a4_left = m_can_lib_list["3"]->GetScreenPosition().x + m_can_lib_list["3"]->GetSize().x / 2;
        auto local_pos4 = GetScreenPosition().x + GetSize().x / 2;
        a4_left = size.x / 2 + (a4_left - local_pos4);
        dc.DrawLine(a4_left, FromDIP(30), a4_left, a4_top);
        dc.DrawLine(a4_left, a4_top, end_top, a4_top);

        //to Extruder
        dc.SetPen(wxPen(AMS_CONTROL_GRAY500, 2, wxPENSTYLE_SOLID));
        dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
        dc.DrawLine(end_top, a1_top, end_top, size.y);


        if (!m_road_canid.empty()) {
            int can_idx = atoi(m_road_canid.c_str());
            if (can_idx < 0 || can_idx >= m_info.cans.size()) {
                return;
            }
            m_road_colour = m_info.cans[can_idx].material_colour;
            dc.SetPen(wxPen(m_road_colour, passroad_width, wxPENSTYLE_SOLID));
            if (m_road_canid == "0") {
                dc.DrawLine(a1_left, FromDIP(30), a1_left, a1_top);
                dc.DrawLine(a1_left, a1_top, end_top, a1_top);
                dc.DrawLine(end_top, a1_top, end_top, size.y);
            }

            if (m_road_canid == "1") {
                dc.DrawLine(a2_left, FromDIP(160), a2_left, a2_top);
                dc.DrawLine(a2_left, a2_top, end_top, a2_top);
                dc.DrawLine(end_top, a2_top, end_top, size.y);
            }

            if (m_road_canid == "2") {
                dc.DrawLine(a3_left, FromDIP(160), a3_left, a3_top);
                dc.DrawLine(a3_left, a3_top, end_top, a3_top);
                dc.DrawLine(end_top, a3_top, end_top, size.y);
            }

            if (m_road_canid == "3") {
                dc.DrawLine(a4_left, FromDIP(30), a4_left, a4_top);
                dc.DrawLine(a4_left, a4_top, end_top, a4_top);
                dc.DrawLine(end_top, a4_top, end_top, size.y);
            }
        }
    }
    catch (...) {}
}

void AmsItem::StopRridLoading(wxString canid)
{
    for (auto refresh_it : m_can_refresh_list) {
        AMSrefresh* refresh = refresh_it.second;
        if (refresh->GetCanId() == canid) { refresh->StopLoading(); }
    }
}

void AmsItem::msw_rescale()
{
    for (auto refresh_it : m_can_refresh_list) {
        AMSrefresh* refresh = refresh_it.second;
        if(refresh) refresh->msw_rescale();
    }

    for (auto lib_it : m_can_lib_list) {
        AMSLib* lib = lib_it.second;
        if (lib) lib->msw_rescale();
    }
    if (m_humidity != nullptr) m_humidity->msw_rescale();
}

void AmsItem::show_sn_value(bool show)
{
    for (auto lib_it : m_can_lib_list) {
        AMSLib* lib = lib_it.second;
        if (lib) lib->show_kn_value(show);
    }
}

}} // namespace Slic3r::GUI
