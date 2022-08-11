#include "StaticLine.hpp"
#include "Label.hpp"

#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(StaticLine, wxWindow)

// catch paint events
EVT_PAINT(StaticLine::paintEvent)

END_EVENT_TABLE()

StaticLine::StaticLine(wxWindow* parent, bool vertical, const wxString& label)
    : wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , vertical(vertical)
{
    wxWindow::SetBackgroundColour(parent->GetBackgroundColour());
    this->pen = wxPen(wxColour("#EEEEEE"));
    DisableFocusFromKeyboard();
    SetFont(Label::Body_14);
    SetLabel(label);
}

void StaticLine::SetLabel(const wxString& label)
{
    wxWindow::SetLabel(label);
    int s = 1;
    if (!label.IsEmpty()) {
        wxClientDC dc(this);
        auto size = dc.GetTextExtent(label);
        s = vertical ? size.x : size.y;
    }
    if (vertical)
        SetMinSize({s, -1});
    else
        SetMinSize({-1, s});
    Refresh();
}

void StaticLine::SetLineColour(wxColour color)
{
    this->pen = wxPen(color);
}

void StaticLine::paintEvent(wxPaintEvent& evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxPaintDC dc(this);
    render(dc);
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void StaticLine::render(wxDC& dc)
{
    wxSize size = GetSize();
    wxSize size2 {0, 0};
    auto label = GetLabel();
    if (!label.IsEmpty()) {
        size2 = dc.GetTextExtent(label);
        dc.DrawText(label, 0, 0);
        if (vertical)
            size2.y += 5;
        else
            size2.x += 5;
    }
    dc.SetPen(pen);
    if (vertical) {
        size.x /= 2;
        dc.DrawLine(size.x, size2.y, size.x, size.y);
    } else {
        size.y /= 2;
        dc.DrawLine(size2.x, size.y, size.x, size.y);
    }
}
