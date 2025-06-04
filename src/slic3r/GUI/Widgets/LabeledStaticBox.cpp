#include "LabeledStaticBox.hpp"
#include "libslic3r/Utils.hpp"
#include "../GUI.hpp"
#include "Label.hpp"

BEGIN_EVENT_TABLE(LabeledStaticBox, wxStaticBox)

EVT_PAINT(LabeledStaticBox::paintEvent)

END_EVENT_TABLE()

/*
Fix label overflowing to inner frame
Fix use elypsis if text too long
setmin size
*/

LabeledStaticBox::LabeledStaticBox()
    : state_handler(this)
{
    radius       = 4;
    border_width = 1;
    text_color = StateColor(
        std::make_pair(0x363636, (int) StateColor::Normal),
        std::make_pair(0x6B6B6B, (int) StateColor::Disabled)
    );
    background_color = StateColor(
        std::make_pair(0xFFFFFF, (int) StateColor::Normal),
        std::make_pair(0xF0F0F1, (int) StateColor::Disabled)
    );
    border_color = StateColor(
        std::make_pair(0xDBDBDB, (int) StateColor::Normal),
        std::make_pair(0xDBDBDB, (int) StateColor::Disabled)
    );
    font        = Label::Head_14;
}

LabeledStaticBox::LabeledStaticBox(
    wxWindow*       parent,
    const wxString& label,
    const wxPoint&  pos,
    const wxSize&   size,
    long            style
)
    : LabeledStaticBox()
{
    Create(parent, label, pos, size, style);
}

bool LabeledStaticBox::Create(
    wxWindow*       parent,
    const wxString& label,
    const wxPoint&  pos,
    const wxSize&   size,
    long            style
)
{
    if (style & wxBORDER_NONE)
        border_width = 0;
    wxStaticBox::Create(parent, wxID_ANY, label, pos, size, style);

    Bind(wxEVT_PAINT, &LabeledStaticBox::paintEvent, this);
    state_handler.attach({&text_color, &background_color, &border_color});
    state_handler.update_binds();
    #ifndef __WXOSX__
        SetBackgroundStyle(wxBG_STYLE_PAINT);
    #endif
    SetBackgroundColour(background_color.colorForStates(state_handler.states()));
    SetForegroundColour(      text_color.colorForStates(state_handler.states()));
    SetBorderColor(         border_color.colorForStates(state_handler.states()));
    return true;
}

void LabeledStaticBox::SetCornerRadius(int radius)
{
    this->radius = radius;
    Refresh();
}

void LabeledStaticBox::SetBorderWidth(int width)
{
    this->border_width = width;
    Refresh();
}

void LabeledStaticBox::SetBorderColor(StateColor const &color)
{
    border_color = color;
    state_handler.update_binds();
    Refresh();
}

void LabeledStaticBox::SetFont(wxFont set_font)
{
    font = set_font;
    Refresh();
}

bool LabeledStaticBox::Enable(bool enable)
{
    bool result = this->Enable(enable);
    if (result) {
        wxCommandEvent e(EVT_ENABLE_CHANGED);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(e);
        this->SetBackgroundColour(background_color.colorForStates(state_handler.states()));
        this->SetForegroundColour(      text_color.colorForStates(state_handler.states()));
        this->SetBorderColor(         border_color.colorForStates(state_handler.states()));
    }
    return result;
}

void LabeledStaticBox::paintEvent(wxPaintEvent& evt)
{
    wxAutoBufferedPaintDC dc(this);

    wxString  label     = this->GetLabel();
    wxRect    client_rc = this->GetClientRect();
    double    scale     = dc.GetContentScaleFactor();
    wxCoord   tW, tH;

    // fill full background
    dc.SetBackground(wxBrush(background_color.colorForStates(0)));
    dc.Clear();

    if (!label.IsEmpty()) {
        dc.SetFont(font);
        dc.SetTextForeground(text_color.colorForStates(state_handler.states()));
        dc.GetTextExtent(label, &tW, &tH);
        client_rc.y      += tH / 2;
        client_rc.height -= tH / 2;
    }

    dc.SetBrush(wxBrush(background_color.colorForStates(state_handler.states())));
    dc.SetPen(wxPen(border_color.colorForStates(state_handler.states()), border_width, wxSOLID));
    dc.DrawRoundedRectangle(client_rc, radius * scale); // add border

    if (!label.IsEmpty()) {
        dc.SetPen(wxPen(background_color.colorForStates(state_handler.states())));
        dc.DrawRectangle(wxRect(6 * scale,0, tW + 8 * scale, client_rc.y + border_width)); // text background
        // if text lenght > client size 
        dc.DrawText(label, wxPoint(10 * scale, 2 * scale));
    }
}