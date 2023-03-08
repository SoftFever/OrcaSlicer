#include "SideTools.hpp"
#include <wx/dcmemory.h>
#include <wx/dcgraph.h>
#include "Label.hpp"
#include "StateColor.hpp"
#include "../wxExtensions.hpp"
#include "../I18N.hpp"
#include "../GUI.hpp"

namespace Slic3r { namespace GUI {
	SideTools::SideTools(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
{
    wxPanel::Create(parent, id, pos, wxSize(0, FromDIP(50)));
    Bind(wxEVT_PAINT, &SideTools::OnPaint, this);

    SetBackgroundColour(wxColour("#FEFFFF"));

    m_printing_img = ScalableBitmap(this, "printer", 16);
    m_arrow_img    = ScalableBitmap(this, "monitor_arrow", 14);

    m_none_printing_img = ScalableBitmap(this, "tab_monitor_active", 24);
    m_none_arrow_img    = ScalableBitmap(this, "monitor_none_arrow", 14);
    m_none_add_img      = ScalableBitmap(this, "monitor_none_add", 14);

    m_wifi_none_img     = ScalableBitmap(this, "monitor_signal_no", 18);
    m_wifi_weak_img     = ScalableBitmap(this, "monitor_signal_weak", 18);
    m_wifi_middle_img   = ScalableBitmap(this, "monitor_signal_middle", 18);
    m_wifi_strong_img   = ScalableBitmap(this, "monitor_signal_strong", 18);

    m_intetval_timer = new wxTimer();
    m_intetval_timer->SetOwner(this);

    this->Bind(wxEVT_TIMER, &SideTools::stop_interval, this);
    this->Bind(wxEVT_ENTER_WINDOW, &SideTools::on_mouse_enter, this);
    this->Bind(wxEVT_LEAVE_WINDOW, &SideTools::on_mouse_leave, this);
    this->Bind(wxEVT_LEFT_DOWN, &SideTools::on_mouse_left_down, this);
    this->Bind(wxEVT_LEFT_UP, &SideTools::on_mouse_left_up, this);
}

SideTools::~SideTools() { delete m_intetval_timer; }

void SideTools::set_none_printer_mode() 
{ 
    m_none_printer = true;
    Refresh();
}

void SideTools::on_timer(wxTimerEvent &event)
{
}

void SideTools::set_current_printer_name(std::string dev_name) 
{
     m_none_printer = false;
     m_dev_name     = from_u8(dev_name);
     Refresh();
}

void SideTools::set_current_printer_signal(WifiSignal sign) 
{
     if (last_printer_signal == sign) return;
    
     last_printer_signal = sign;
     m_none_printer = false;
     m_wifi_type    = sign;
     Refresh();
}

void SideTools::start_interval() 
{ 
    m_intetval_timer->Start(SIDE_TOOL_CLICK_INTERVAL); 
    m_is_in_interval = true;
}

void SideTools::stop_interval(wxTimerEvent& event)
{
    m_is_in_interval = false;
    m_intetval_timer->Stop();
}


bool SideTools::is_in_interval() 
{
    return m_is_in_interval;
}

void SideTools::msw_rescale() 
{ 
    Refresh();
}

void SideTools::OnPaint(wxPaintEvent &event)
{
    wxPaintDC dc(this);
    doRender(dc);
}

void SideTools::render(wxDC &dc)
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

void SideTools::doRender(wxDC &dc)
{
    auto   left = FromDIP(15);
    wxSize size = GetSize();
    
    //if (m_none_printer) {
    //    dc.SetPen(SIDE_TOOLS_LIGHT_GREEN);
    //    dc.SetBrush(SIDE_TOOLS_LIGHT_GREEN);
    //    dc.DrawRectangle(0, 0, size.x, size.y);
    //}

    if (m_none_printer) {
        dc.SetPen(SIDE_TOOLS_BRAND);
        dc.SetBrush(SIDE_TOOLS_BRAND);
        dc.DrawRectangle(0, 0, size.x, size.y);

        dc.DrawBitmap(m_none_printing_img.bmp(), left, (size.y - m_none_printing_img.GetBmpSize().y) / 2);

        left += (m_none_printing_img.GetBmpSize().x + FromDIP(15));
        dc.DrawBitmap(m_none_arrow_img.bmp(), left, (size.y - m_none_arrow_img.GetBmpSize().y) / 2);

        left += (m_none_arrow_img.GetBmpSize().x + FromDIP(6));
        dc.SetFont(::Label::Body_14);
        dc.SetBackgroundMode(wxTRANSPARENT);
        dc.SetTextForeground(*wxWHITE);

        wxString no_printer_str = _L("No printer");
        auto sizet = dc.GetTextExtent(no_printer_str);
        auto left_add_bitmap = size.x - FromDIP(30) - m_wifi_none_img.GetBmpSize().x - m_none_add_img.GetBmpSize().x;
        auto size_width = left_add_bitmap - left;

        if (sizet.x > size_width) {
            wxString temp_str = wxEmptyString;
            for (auto i = 0; i < no_printer_str.Len(); i++) {
                if (dc.GetTextExtent(L("...") + temp_str).x < size_width) {
                    temp_str += no_printer_str[i];
                }
                else {
                    break;
                }
            }

            no_printer_str = temp_str + L("...");
        }

        dc.DrawText(no_printer_str, wxPoint(left, (size.y - sizet.y) / 2));

        left = size.x - FromDIP(30) - m_wifi_none_img.GetBmpSize().x;
        dc.DrawBitmap(m_none_add_img.bmp(), left, (size.y - m_none_add_img.GetBmpSize().y) / 2);
    } else {
        dc.DrawBitmap(m_printing_img.bmp(), left, (size.y - m_printing_img.GetBmpSize().y) / 2);

        left += (m_printing_img.GetBmpSize().x + FromDIP(5));
        dc.DrawBitmap(m_arrow_img.bmp(), left, (size.y - m_arrow_img.GetBmpSize().y) / 2);

        left += (m_arrow_img.GetBmpSize().x + FromDIP(6));
        dc.SetFont(::Label::Body_14);
        dc.SetBackgroundMode(wxTRANSPARENT);
        dc.SetTextForeground(StateColor::darkModeColorFor(SIDE_TOOLS_GREY900));

        auto sizet = dc.GetTextExtent(m_dev_name);
        auto text_end = size.x - m_wifi_none_img.GetBmpSize().x - 20;
        
        std::string finally_name = m_dev_name.ToStdString();
        if (sizet.x > (text_end - left)) {
            auto limit_width = text_end - left - dc.GetTextExtent("...").x - 20;
            for (auto i = 0; i < m_dev_name.length(); i++) {
                auto curr_width = dc.GetTextExtent(m_dev_name.substr(0, i));
                if (curr_width.x >= limit_width) {
                    finally_name = (m_dev_name.substr(0, i) + wxString("...")).ToStdString();
                    break;
                }
            }
        }


        dc.DrawText(finally_name, wxPoint(left, (size.y - sizet.y) / 2));

        left = size.x - FromDIP(18) - m_wifi_none_img.GetBmpSize().x;
        if (m_wifi_type == WifiSignal::NONE) dc.DrawBitmap(m_wifi_none_img.bmp(), left, (size.y - m_wifi_none_img.GetBmpSize().y) / 2);
        if (m_wifi_type == WifiSignal::WEAK) dc.DrawBitmap(m_wifi_weak_img.bmp(), left, (size.y - m_wifi_weak_img.GetBmpSize().y) / 2);
        if (m_wifi_type == WifiSignal::MIDDLE) dc.DrawBitmap(m_wifi_middle_img.bmp(), left, (size.y - m_wifi_middle_img.GetBmpSize().y) / 2);
        if (m_wifi_type == WifiSignal::STRONG) dc.DrawBitmap(m_wifi_strong_img.bmp(), left, (size.y - m_wifi_strong_img.GetBmpSize().y) / 2);
    }

    if (m_hover) {
        dc.SetPen(SIDE_TOOLS_BRAND);
        dc.SetBrush(*wxTRANSPARENT_BRUSH);
        dc.DrawRectangle(0, 0, size.x, size.y);
    }
}

void SideTools::on_mouse_left_down(wxMouseEvent &evt)
{
    m_click = true;
    Refresh();
}

void SideTools::on_mouse_left_up(wxMouseEvent &evt) 
{
     m_click = false;
     Refresh();
}

void SideTools::on_mouse_enter(wxMouseEvent &evt)
{
    m_hover = true;
    Refresh();
}

void SideTools::on_mouse_leave(wxMouseEvent &evt)
{
    m_hover = false;
    Refresh();
}
}}
