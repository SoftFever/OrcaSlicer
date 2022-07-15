#include "ImageSwitchButton.hpp"
#include "Label.hpp"
#include "StaticBox.hpp"
#include "../wxExtensions.hpp"

#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(ImageSwitchButton, StaticBox)

EVT_LEFT_DOWN(ImageSwitchButton::mouseDown)
EVT_ENTER_WINDOW(ImageSwitchButton::mouseEnterWindow)
EVT_LEAVE_WINDOW(ImageSwitchButton::mouseLeaveWindow)
EVT_LEFT_UP(ImageSwitchButton::mouseReleased)
EVT_PAINT(ImageSwitchButton::paintEvent)

END_EVENT_TABLE()

static const wxColour DEFAULT_HOVER_COL = wxColour(0, 174, 66);
static const wxColour DEFAULT_PRESS_COL = wxColour(238, 238, 238);

ImageSwitchButton::ImageSwitchButton(wxWindow *parent, wxBitmap &img_on, wxBitmap &img_off, long style)
    : text_color(std::make_pair(0x6B6B6B, (int) StateColor::Disabled), std::make_pair(*wxBLACK, (int) StateColor::Normal))
    , state_handler(this)
{
    m_padding = 0;
    m_on  = img_on.GetSubBitmap(wxRect(0, 0, img_on.GetWidth(), img_on.GetHeight()));
    m_off = img_off.GetSubBitmap(wxRect(0, 0, img_off.GetWidth(), img_off.GetHeight()));
    bg_color     = StateColor(std::make_pair(DEFAULT_PRESS_COL, (int) StateColor::Pressed),
                              std::make_pair(*wxWHITE, (int) StateColor::Normal));
    border_color = StateColor(std::make_pair(DEFAULT_HOVER_COL, (int) StateColor::Hovered));

    StaticBox::Create(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, style);

    state_handler.attach({&bg_color});
    state_handler.attach({&border_color});
    state_handler.update_binds();
    messureSize();
    Refresh();
}

void ImageSwitchButton::SetLabels(wxString const &lbl_on, wxString const &lbl_off)
{
	labels[0] = lbl_on;
	labels[1] = lbl_off;
    messureSize();
    Refresh();
}

void ImageSwitchButton::SetImages(wxBitmap &img_on, wxBitmap &img_off)
{
	m_on = img_on;
	m_off = img_off;
    messureSize();
    Refresh();
}

void ImageSwitchButton::SetTextColor(StateColor const &color)
{
	text_color = color;
    state_handler.update_binds();
    messureSize();
    Refresh();
}

void ImageSwitchButton::SetBorderColor(StateColor const &color)
{
	border_color = color;
    messureSize();
    Refresh();
}

void ImageSwitchButton::SetBgColor(StateColor const &color) {
	bg_color = color;
    messureSize();
    Refresh();
}

void ImageSwitchButton::SetValue(bool value)
{
    m_on_off = value;
    messureSize();
    Refresh();
}

void ImageSwitchButton::SetPadding(int padding)
{
    m_padding = padding;
    messureSize();
    Refresh();
}

void ImageSwitchButton::messureSize()
{
	wxClientDC dc(this);
    dc.SetFont(GetFont());
	textSize = dc.GetTextExtent(GetValue() ? labels[0] : labels[1]);
}

void ImageSwitchButton::paintEvent(wxPaintEvent &evt)
{
	wxPaintDC dc(this);
	render(dc);
}

void ImageSwitchButton::render(wxDC& dc)
{
	StaticBox::render(dc);
    int states = state_handler.states();
	wxSize size = GetSize();

	if (pressedDown) {
        dc.SetBrush(bg_color.colorForStates(StateColor::Pressed));
        dc.DrawRectangle(wxRect(0, 0, size.x, size.y));
    }

    if (hover) {
        dc.SetPen(border_color.colorForStates(StateColor::Hovered));
        dc.DrawRectangle(wxRect(0, 0, size.x, size.y));
    }

	wxSize szIcon;
	wxSize szContent = textSize;
	wxBitmap &icon = GetValue() ? m_on: m_off;
	
	int content_height = icon.GetHeight() + textSize.y + m_padding;

	wxPoint pt = wxPoint((size.x - icon.GetWidth()) / 2, (size.y - content_height) / 2);
	if (icon.IsOk()) {
		dc.DrawBitmap(icon, pt);
		pt.y += m_padding + icon.GetHeight();
	}
	pt.x = (size.x - textSize.x) / 2;
	dc.SetFont(GetFont());
    if (!IsEnabled())
        dc.SetTextForeground(text_color.colorForStates(StateColor::Disabled));
    else
        dc.SetTextForeground(text_color.colorForStates(states));
	dc.DrawText(GetValue() ? labels[0] : labels[1], pt);
}

void ImageSwitchButton::Rescale()
{
	messureSize();
}

void ImageSwitchButton::mouseDown(wxMouseEvent &event)
{
    event.Skip();
    pressedDown = true;
    SetFocus();
    CaptureMouse();
}

void ImageSwitchButton::mouseReleased(wxMouseEvent &event)
{
    event.Skip();
    if (pressedDown) {
        pressedDown = false;
        ReleaseMouse();
        m_on_off = !m_on_off;
        Refresh();
        sendButtonEvent();
    }
}

void ImageSwitchButton::mouseEnterWindow(wxMouseEvent &event)
{
    if (!hover) {
        hover = true;
        Refresh();
    }
}

void ImageSwitchButton::mouseLeaveWindow(wxMouseEvent &event)
{
    if (hover) {
        hover = false;
        Refresh();
    }
}

void ImageSwitchButton::sendButtonEvent()
{
    wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
}