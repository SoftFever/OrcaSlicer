#include "SideMenuPopup.hpp"
#include "Label.hpp"

#include <wx/dcgraph.h>



wxBEGIN_EVENT_TABLE(SidePopup,wxPopupTransientWindow)
EVT_PAINT(SidePopup::paintEvent)
wxEND_EVENT_TABLE()

SidePopup::SidePopup(wxWindow* parent)
    :wxPopupTransientWindow(parent,
    wxBORDER_NONE |
    wxPU_CONTAINS_CONTROLS)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

}

SidePopup::~SidePopup()
{
    ;
}

void SidePopup::OnDismiss()
{
    wxPopupTransientWindow::OnDismiss();
}

bool SidePopup::ProcessLeftDown(wxMouseEvent& event)
{
    return wxPopupTransientWindow::ProcessLeftDown(event);
}
bool SidePopup::Show( bool show )
{
    return wxPopupTransientWindow::Show(show);
}

void SidePopup::Popup(wxWindow* focus)
{
    Create();
     int screenwidth = wxSystemSettings::GetMetric(wxSYS_SCREEN_X,NULL);
    int max_width = 0;
    for (auto btn : btn_list)
    {
        max_width = std::max(btn->GetMinSize().x, max_width);
    }
    if (focus) {
        wxPoint pos = focus->ClientToScreen(wxPoint(0, -6));

#ifdef __APPLE__
         pos.x = pos.x - FromDIP(20);
#endif // __APPLE__
       
        if (pos.x + max_width > screenwidth)
            Position({pos.x - (pos.x + max_width - screenwidth),pos.y}, {0, focus->GetSize().y + 12});
        else
            Position(pos, {0, focus->GetSize().y + 12});
    }
    wxPopupTransientWindow::Popup();
}

void SidePopup::Create()
{
    wxSizer* sizer = new wxBoxSizer(wxVERTICAL);

    int max_width = 0;
    int height = 0;
    for (auto btn : btn_list)
    {
        max_width = std::max(btn->GetMinSize().x, max_width);
    }

    for (auto btn : btn_list)
    {
        wxSize size = btn->GetMinSize();
        height += size.y;
        size.x = max_width;
        btn->SetMinSize(size);
        btn->SetSize(size);
        sizer->Add(btn, 0, 0, 0);        
    }

    SetSize(wxSize(max_width, height));

    SetSizer(sizer, true);

    Layout();
    Refresh();
}

void SidePopup::paintEvent(wxPaintEvent& evt)
{
    wxPaintDC dc(this);
    wxSize size = GetSize();
    dc.SetBrush(wxTransparentColour);
    dc.DrawRectangle(0, 0, size.x, size.y);
}

void SidePopup::append_button(SideButton* btn)
{
    btn_list.push_back(btn);
}
