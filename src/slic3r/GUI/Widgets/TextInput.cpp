#include "TextInput.hpp"
#include "Label.hpp"
#include "TextCtrl.h"
#include "slic3r/GUI/Widgets/Label.hpp"

#include <wx/dcclient.h>
#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(TextInput, wxPanel)

EVT_PAINT(TextInput::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

TextInput::TextInput()
    : label_color(
		std::make_pair(wxColour("#ACACAC"), (int) StateColor::Disabled),
        std::make_pair(wxColour("#6B6B6B"), (int) StateColor::Normal)
	)
    , text_color(
		std::make_pair(wxColour("#ACACAC"), (int) StateColor::Disabled),
		std::make_pair(wxColour("#262E30"), (int) StateColor::Normal)
	)
{
    radius = 0;
    border_width = 1;
    border_color = StateColor(
		std::make_pair(wxColour("#DBDBDB"), (int) StateColor::Disabled),
		std::make_pair(wxColour("#009688"), (int) StateColor::Hovered),
		std::make_pair(wxColour("#DBDBDB"), (int) StateColor::Normal)
	);
    background_color = StateColor(
		std::make_pair(wxColour("#F0F0F1"), (int) StateColor::Disabled),
		std::make_pair(wxColour("#FFFFFF"), (int) StateColor::Normal)
	);
    SetFont(Label::Body_12);
}

TextInput::TextInput(wxWindow *     parent,
                     wxString       text,
                     wxString       label,
                     wxString       icon,
                     const wxPoint &pos,
                     const wxSize & size,
                     long           style)
    : TextInput()
{
    Create(parent, text, label, icon, pos, size, style);
}

void TextInput::Create(wxWindow *     parent,
                       wxString       text,
                       wxString       label,
                       wxString       icon,
                       const wxPoint &pos,
                       const wxSize & size,
                       long           style)
{
        text_ctrl = nullptr;
    StaticBox::Create(parent, wxID_ANY, pos, size, style);
    wxWindow::SetLabel(label);
    style &= ~wxRIGHT;
    state_handler.attach({&label_color, & text_color});
    state_handler.update_binds();
    text_ctrl = new TextCtrl(this, wxID_ANY, text, {4, 4}, wxDefaultSize, style | wxBORDER_NONE | wxTE_PROCESS_ENTER);
    text_ctrl->SetFont(Label::Body_14);
    text_ctrl->SetInitialSize(text_ctrl->GetBestSize());
    text_ctrl->SetBackgroundColour(background_color.colorForStates(state_handler.states()));
    text_ctrl->SetForegroundColour(text_color.colorForStates(state_handler.states()));
    state_handler.attach_child(text_ctrl);
    text_ctrl->Bind(wxEVT_KILL_FOCUS, [this](auto &e) {
        OnEdit();
        e.SetId(GetId());
        ProcessEventLocally(e);
        e.Skip();
    });
    text_ctrl->Bind(wxEVT_TEXT_ENTER, [this](auto &e) {
        OnEdit();
        e.SetId(GetId());
        ProcessEventLocally(e);
    });
    text_ctrl->Bind(wxEVT_RIGHT_DOWN, [this](auto &e) {}); // disable context menu
    if (!icon.IsEmpty()) {
        this->icon = ScalableBitmap(this, icon.ToStdString(), 0); // ORCA: 0 gets icon size from file
    }
    messureSize();
}

void TextInput::SetCornerRadius(double radius)
{
    this->radius = radius;
    Refresh();
}

void TextInput::SetLabel(const wxString& label)
{
    wxWindow::SetLabel(label);
    messureSize();
    Refresh();
}

void TextInput::SetIcon(const wxBitmap &icon)
{
    this->icon = ScalableBitmap();
    this->icon.bmp() = icon;
    Rescale();
}

void TextInput::SetIcon(const wxString &icon)
{
    if (this->icon.name() == icon.ToStdString())
        return;
    this->icon = ScalableBitmap(this, icon.ToStdString(), 16);
    Rescale();
}

void TextInput::SetLabelColor(StateColor const &color)
{
    label_color = color;
    state_handler.update_binds();
}

void TextInput::SetTextColor(StateColor const& color)
{
    text_color= color;
    state_handler.update_binds();
}

void TextInput::Rescale()
{
    if (!this->icon.name().empty())
        this->icon.msw_rescale();
    messureSize();
    Refresh();
}

bool TextInput::Enable(bool enable)
{
    bool result = text_ctrl->Enable(enable) && wxWindow::Enable(enable);
    if (result) {
        wxCommandEvent e(EVT_ENABLE_CHANGED);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(e);
        text_ctrl->SetBackgroundColour(background_color.colorForStates(state_handler.states()));
        text_ctrl->SetForegroundColour(text_color.colorForStates(state_handler.states()));
    }
    return result;
}

void TextInput::SetMinSize(const wxSize& size)
{
    wxSize size2 = size;
    if (size2.y < 0) {
#ifdef __WXMAC__
        if (GetPeer()) // peer is not ready in Create on mac
#endif
        size2.y = GetSize().y;
    }
    wxWindow::SetMinSize(size2);
}

void TextInput::DoSetSize(int x, int y, int width, int height, int sizeFlags)
{
    wxWindow::DoSetSize(x, y, width, height, sizeFlags);
    if (sizeFlags & wxSIZE_USE_EXISTING) return;
    wxSize size = GetSize();
    wxPoint textPos = {5, 0};
    if (this->icon.bmp().IsOk()) {
        wxSize szIcon = this->icon.GetBmpSize();
        textPos.x += szIcon.x;
    }
    bool align_right = GetWindowStyle() & wxRIGHT;
    if (align_right)
        textPos.x += labelSize.x;
    if (text_ctrl) {
        wxSize textSize = text_ctrl->GetSize();
        textSize.x = size.x - textPos.x - labelSize.x - 10;
        text_ctrl->SetSize(textSize);
        text_ctrl->SetPosition({textPos.x, (size.y - textSize.y) / 2});
    }
}

void TextInput::DoSetToolTipText(wxString const &tip)
{
    wxWindow::DoSetToolTipText(tip);
    text_ctrl->SetToolTip(tip);
}

void TextInput::paintEvent(wxPaintEvent &evt)
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
void TextInput::render(wxDC& dc)
{
    StaticBox::render(dc);
    int states = state_handler.states();
    wxSize size = GetSize();
    bool   align_right = GetWindowStyle() & wxRIGHT;
    // start draw
    wxPoint pt = {1, 0}; // ORCA: Start drawing at 0 for icons
    if (icon.bmp().IsOk()) {
        wxSize szIcon = icon.GetBmpSize();
        pt.y = (size.y - szIcon.y) / 2;
        dc.DrawBitmap(icon.bmp(), pt);
        pt.x += szIcon.x + 0;
    } else {
        pt.x += 5; // ORCA: Add left margin to text if there is no icon
    }
    auto text = wxWindow::GetLabel();
    if (!text.IsEmpty()) {
        if (icon.bmp().IsOk()) {
            pt.x += 3; // ORCA: Add left margin to text if there is an icon
		}
        wxSize textSize = text_ctrl->GetSize();
        if (align_right) {
            if (pt.x + labelSize.x > size.x)
                text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, size.x - pt.x);
            pt.y = (size.y - labelSize.y) / 2;
        } else {
            pt.x += textSize.x;
            pt.y = (size.y + textSize.y) / 2 - labelSize.y;
        }
        dc.SetTextForeground(label_color.colorForStates(states));
        if(align_right)
            dc.SetFont(GetFont());
        else
            dc.SetFont(Label::Body_12);
        dc.DrawText(text, pt);
    }
}

void TextInput::messureSize()
{
    wxSize size = GetSize();
    wxClientDC dc(this);
    bool   align_right = GetWindowStyle() & wxRIGHT;
    if (align_right)
        dc.SetFont(GetFont());
    else
        dc.SetFont(Label::Body_12);
    labelSize = dc.GetTextExtent(wxWindow::GetLabel());
    wxSize textSize = text_ctrl->GetSize();
    int h = textSize.y + 8;
    if (size.y < h) {
        size.y = h;
    }
    wxSize minSize = size;
    minSize.x = GetMinWidth();
    SetMinSize(minSize);
    SetSize(size);
}
