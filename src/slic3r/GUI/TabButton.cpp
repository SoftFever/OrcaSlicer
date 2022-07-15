#include "TabButton.hpp"
#include "Widgets/Label.hpp"

#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(TabButton, StaticBox)

EVT_LEFT_DOWN(TabButton::mouseDown)
EVT_LEFT_UP(TabButton::mouseReleased)

// catch paint events
EVT_PAINT(TabButton::paintEvent)

END_EVENT_TABLE()

static wxColour BORDER_HOVER_COL = wxColour(0, 174, 66);

const static wxColour TAB_BUTTON_BG    = wxColour(255, 255, 255, 255);
const static wxColour TAB_BUTTON_SEL   = wxColour(219, 253, 213, 255);

TabButton::TabButton()
    : paddingSize(43, 16)
    , text_color(*wxBLACK)
{
    background_color = StateColor(
        std::make_pair(TAB_BUTTON_SEL, (int) StateColor::Checked),
        std::make_pair(*wxWHITE, (int) StateColor::Hovered),
        std::make_pair(*wxWHITE, (int) StateColor::Normal));

    border_color = StateColor(
        std::make_pair(*wxWHITE, (int) StateColor::Checked),
        std::make_pair(BORDER_HOVER_COL, (int) StateColor::Hovered),
        std::make_pair(*wxWHITE, (int) StateColor::Normal));
}

TabButton::TabButton(wxWindow *parent, wxString text, wxBitmap &bmp, long style, int iconSize)
    : TabButton()
{
    Create(parent, text, bmp, style, iconSize);
}

bool TabButton::Create(wxWindow *parent, wxString text, wxBitmap &bmp, long style, int iconSize)
{
    StaticBox::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, style);
    state_handler.attach({&text_color, &border_color});
    state_handler.update_binds();
    //BBS set default font
    SetFont(Label::Body_14);
    wxWindow::SetLabel(text);
    this->icon = bmp.GetSubBitmap(wxRect(0, 0, bmp.GetWidth(), bmp.GetHeight()));
    messureSize();
    return true;
}

void TabButton::SetLabel(const wxString &label)
{
    wxWindow::SetLabel(label);
    messureSize();
    Refresh();
}

void TabButton::SetMinSize(const wxSize &size)
{
    minSize = size;
    messureSize();
}

void TabButton::SetPaddingSize(const wxSize &size)
{
    paddingSize = size;
    messureSize();
}

void TabButton::SetTextColor(StateColor const &color)
{
    text_color = color;
    state_handler.update_binds();
    Refresh();
}

void TabButton::SetBorderColor(StateColor const &color)
{
    border_color = color;
    state_handler.update_binds();
    Refresh();
}

void TabButton::SetBGColor(StateColor const &color)
{
    background_color = color;
    state_handler.update_binds();
    Refresh();
}

void TabButton::SetBitmap(wxBitmap &bitmap)
{
    this->icon = bitmap.GetSubBitmap(wxRect(0, 0, bitmap.GetWidth(), bitmap.GetHeight()));
}

bool TabButton::Enable(bool enable)
{
    bool result = wxWindow::Enable(enable);
    if (result) {
        wxCommandEvent e(EVT_ENABLE_CHANGED);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(e);
    }
    return result;
}

void TabButton::Rescale()
{
    messureSize();
}

void TabButton::paintEvent(wxPaintEvent &evt)
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
void TabButton::render(wxDC &dc)
{
    StaticBox::render(dc);
    int    states = state_handler.states();
    wxSize size   = GetSize();

    dc.SetPen(wxPen(border_color.colorForStates(states)));
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    dc.DrawRectangle(0, 0, size.x, size.y);

    // calc content size
    wxSize szIcon;
    wxSize szContent = textSize;
    if (icon.IsOk()) {
        if (szContent.y > 0) {
            // BBS norrow size between text and icon
            szContent.x += 5;
        }
        szIcon = icon.GetSize();
        szContent.x += szIcon.x;
        if (szIcon.y > szContent.y) szContent.y = szIcon.y;
    }
    // move to center
    wxRect rcContent = {{0, 0}, size};
    wxSize offset    = (size - szContent) / 2;
    rcContent.Deflate(offset.x, offset.y);
    // start draw
    wxPoint pt = rcContent.GetLeftTop();

    auto text = GetLabel();
    if (!text.IsEmpty()) {
        pt.x = paddingSize.x;
        pt.y = rcContent.y + (rcContent.height - textSize.y) / 2;
        dc.SetFont(GetFont());
        dc.SetTextForeground(text_color.colorForStates(states));
        dc.DrawText(text, pt);
    }

    if (icon.IsOk()) {
        pt.x = size.x - icon.GetWidth() - paddingSize.y;
        pt.y = (size.y - icon.GetHeight()) / 2;
        dc.DrawBitmap(icon, pt);
    }
}

void TabButton::messureSize()
{
    wxClientDC dc(this);
    textSize = dc.GetTextExtent(GetLabel());
    if (minSize.GetWidth() > 0) {
        wxWindow::SetMinSize(minSize);
        return;
    }
    wxSize szContent = textSize;
    if (this->icon.IsOk()) {
        if (szContent.y > 0) {
            // BBS norrow size between text and icon
            szContent.x += 5;
        }
        wxSize szIcon = this->icon.GetSize();
        szContent.x += szIcon.x;
        if (szIcon.y > szContent.y) szContent.y = szIcon.y;
    }
    wxWindow::SetMinSize(szContent + paddingSize);
}

void TabButton::mouseDown(wxMouseEvent &event)
{
    event.Skip();
    pressedDown = true;
    SetFocus();
    CaptureMouse();
}

void TabButton::mouseReleased(wxMouseEvent &event)
{
    event.Skip();
    if (pressedDown) {
        pressedDown = false;
        ReleaseMouse();
        if (wxRect({0, 0}, GetSize()).Contains(event.GetPosition()))
            sendButtonEvent();
    }
}

void TabButton::sendButtonEvent()
{
    wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
}
