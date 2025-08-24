//**********************************************************/
/* File: uiAmsHumidityPopup.cpp
*  Description: The popup with Ams Humidity
*
* \n class uiAmsHumidityPopup
//**********************************************************/

#include "uiAmsHumidityPopup.h"

#include "slic3r/Utils/WxFontUtils.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Widgets/StateColor.hpp"


#include <wx/dcgraph.h>

namespace Slic3r { namespace GUI {

uiAmsPercentHumidityDryPopup::uiAmsPercentHumidityDryPopup(wxWindow *parent)
    : PopupWindow(parent, wxBORDER_NONE)
{
    SetSize(wxSize(FromDIP(400), FromDIP(270)));
    SetMinSize(wxSize(FromDIP(400), FromDIP(270)));
    SetMaxSize(wxSize(FromDIP(400), FromDIP(270)));

    idle_img   = ScalableBitmap(this, "ams_drying", 16);
    drying_img = ScalableBitmap(this, "ams_is_drying", 16);
    close_img  = ScalableBitmap(this, "hum_popup_close", 24);

    Bind(wxEVT_PAINT, &uiAmsPercentHumidityDryPopup::paintEvent, this);
    Bind(wxEVT_LEFT_UP, [this](auto &e) {
        auto rect = ClientToScreen(wxPoint(0, 0));

        auto close_left   = rect.x + GetSize().x - close_img.GetBmpWidth() - FromDIP(38);
        auto close_right  = close_left + close_img.GetBmpWidth();
        auto close_top    = rect.y + FromDIP(24);
        auto close_bottom = close_top + close_img.GetBmpHeight();

        auto mouse_pos = ClientToScreen(e.GetPosition());
        if (mouse_pos.x > close_left && mouse_pos.y > close_top && mouse_pos.x < close_right && mouse_pos.y < close_bottom) { Dismiss(); }
    });
}

void uiAmsPercentHumidityDryPopup::Update(int humidiy_level, int humidity_percent, int left_dry_time, float current_temperature)
{
    if (m_humidity_level != humidiy_level || m_humidity_percent != humidity_percent ||
        m_left_dry_time != left_dry_time || m_current_temperature != current_temperature)
    {
        m_humidity_level   = humidiy_level;
        m_humidity_percent = humidity_percent;
        m_left_dry_time    = left_dry_time;
        m_current_temperature = current_temperature;

        Refresh();
    }
}

void uiAmsPercentHumidityDryPopup::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void uiAmsPercentHumidityDryPopup::render(wxDC &dc)
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

void uiAmsPercentHumidityDryPopup::doRender(wxDC &dc)
{
    // background
    {
        dc.SetBrush(StateColor::darkModeColorFor(*wxWHITE));
        dc.DrawRoundedRectangle(0, 0, GetSize().GetWidth(), GetSize().GetHeight(), 0);
    }
    dc.SetBrush(*wxTRANSPARENT_BRUSH);

    wxPoint p;

    // Header
    {
        dc.SetFont(::Label::Head_24);
        dc.SetTextForeground(StateColor::darkModeColorFor(*wxBLACK));
        //WxFontUtils::get_suitable_font_size(FromDIP(24), dc);

        auto extent = dc.GetTextExtent(_L("Current AMS humidity"));
        dc.DrawText(_L("Current AMS humidity"), (GetSize().GetWidth() - extent.GetWidth()) / 2, FromDIP(24));
    }

    // close icon
    p.y += FromDIP(24);
    dc.DrawBitmap(close_img.bmp(), GetSize().x - close_img.GetBmpWidth() - FromDIP(38), p.y);

    // humitidy image
    if (0 < m_humidity_level && m_humidity_level < 6)
    {
        ScalableBitmap humitidy_image;
        if (wxGetApp().dark_mode())
        {
            humitidy_image = ScalableBitmap(this, "hum_level" + std::to_string(m_humidity_level) + "_no_num_light", 64);
        }
        else
        {
            humitidy_image = ScalableBitmap(this, "hum_level" + std::to_string(m_humidity_level) + "_no_num_light", 64);
        }

        p.y += 2 * FromDIP(24);
        dc.DrawBitmap(humitidy_image.bmp(), (GetSize().GetWidth() - humitidy_image.GetBmpWidth()) / 2, p.y);
        p.y += humitidy_image.GetBmpHeight();
    }

    // dry state
    int spacing = FromDIP(5);
    {
        p.y += spacing;
        if (m_left_dry_time > 0)
        {
            dc.DrawBitmap(drying_img.bmp(), GetSize().GetWidth() / 2 - drying_img.GetBmpWidth() - spacing, p.y);
        }
        else
        {
            dc.DrawBitmap(idle_img.bmp(), GetSize().GetWidth() / 2 - idle_img.GetBmpWidth() - spacing, p.y);
        }

        dc.SetFont(::Label::Body_14);
        //WxFontUtils::get_suitable_font_size(idle_img.GetBmpHeight(), dc);

        const wxString &dry_state        = (m_left_dry_time > 0) ? _L("Drying") : _L("Idle");
        auto dry_state_extent = dc.GetTextExtent(dry_state);

        p.y += (idle_img.GetBmpHeight() - dry_state_extent.GetHeight());//align bottom
        dc.DrawText(dry_state, GetSize().GetWidth() / 2 + spacing, p.y);
        p.y += dry_state_extent.GetHeight();
    }

    // Grid area
    {
        p.y += 2 * spacing;
        DrawGridArea(dc, p);
    }
}


static vector<wxString> grid_header{ L("Humidity"), L("Temperature"), L("Left Time")};
void uiAmsPercentHumidityDryPopup::DrawGridArea(wxDC &dc, wxPoint start_p)
{
    const wxColour& gray_clr = StateColor::darkModeColorFor(wxColour(194, 194, 194));
    const wxColour& black_clr = StateColor::darkModeColorFor(*wxBLACK);

    // Horizontal line
    dc.SetPen(gray_clr);
    int h_margin = FromDIP(20);
    dc.DrawLine(h_margin, start_p.y, GetSize().GetWidth() - h_margin, start_p.y);
    start_p.x = h_margin;
    start_p.y += h_margin;

    // Draw grid area
    int toltal_col;
    if (m_left_dry_time > 0)
    {
        toltal_col = 3;
    }
    else
    {
        toltal_col = 2;
    }

    int row_height = FromDIP(30);
    int text_height = FromDIP(20);
    int distance = (GetSize().GetWidth() - 2 * h_margin)/ toltal_col;
    for (int col = 0; col < toltal_col; ++col)
    {
        const wxString& header = _L(grid_header[col]);
        dc.SetFont(::Label::Body_14);
        //WxFontUtils::get_suitable_font_size(text_height, dc);
        const auto &header_extent = dc.GetTextExtent(header);

        int left = start_p.x + (distance - header_extent.GetWidth()) / 2;
        dc.SetPen(gray_clr);
        dc.DrawText(header, left, start_p.y);

        // row content
        dc.SetPen(black_clr);
        if (header == _L("Humidity"))
        {
            const wxString &humidity_str = wxString::Format("%d%%", m_humidity_percent);
            dc.DrawText(humidity_str, left, start_p.y + row_height);
        }
        else if (header == _L("Temperature"))
        {
            const wxString &temp_str = wxString::Format(wxString::FromUTF8(u8"%.1f \u2103"), m_current_temperature);
            dc.DrawText(temp_str, left, start_p.y + row_height);
        }
        else if (header == _L("Left Time"))
        {
            const wxString &time_str = wxString::Format("%d : %d", m_left_dry_time / 60, m_left_dry_time % 60);
            dc.DrawText(time_str, left, start_p.y + row_height);
        }

        start_p.x += distance;
        if (col < toltal_col - 1) /*draw splitter*/
        {
            dc.SetPen(gray_clr);
            dc.DrawLine(start_p.x, start_p.y, start_p.x, start_p.y + 2 * row_height);
        }
    }
}

void uiAmsPercentHumidityDryPopup::msw_rescale()
{
    idle_img.msw_rescale();
    drying_img.msw_rescale();
    close_img.msw_rescale();

    Refresh();
}

} // namespace GUI

} // namespace Slic3r