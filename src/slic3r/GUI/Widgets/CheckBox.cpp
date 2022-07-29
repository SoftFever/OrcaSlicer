#include "CheckBox.hpp"

#include "../wxExtensions.hpp"

CheckBox::CheckBox(wxWindow* parent)
	: wxBitmapToggleButton(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
    , m_on(this, "check_on", 18)
    , m_half(this, "check_half", 18)
    , m_off(this, "check_off", 18)
    , m_on_disabled(this, "check_on_disabled", 18)
    , m_half_disabled(this, "check_half_disabled", 18)
    , m_off_disabled(this, "check_off_disabled", 18)
    , m_on_focused(this, "check_on_focused", 18)
    , m_half_focused(this, "check_half_focused", 18)
    , m_off_focused(this, "check_off_focused", 18)
{
	//SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
	if (parent)
		SetBackgroundColour(parent->GetBackgroundColour());
	Bind(wxEVT_TOGGLEBUTTON, [this](auto& e) { m_half_checked = false; update(); e.Skip(); });
	SetSize(m_on.GetBmpSize());
	SetMinSize(m_on.GetBmpSize());
	update();
}

void CheckBox::SetValue(bool value)
{
	wxBitmapToggleButton::SetValue(value);
	update();
}

void CheckBox::SetHalfChecked(bool value)
{
	m_half_checked = value;
	update();
}

void CheckBox::Rescale()
{
	m_on.msw_rescale();
	m_off.msw_rescale();
	SetSize(m_on.GetBmpSize());
	update();
}

void CheckBox::update()
{
	SetBitmapLabel((m_half_checked ? m_half : GetValue() ? m_on : m_off).bmp());
    SetBitmapDisabled((m_half_checked ? m_half_disabled : GetValue() ? m_on_disabled : m_off_disabled).bmp());
    SetBitmapFocus((m_half_checked ? m_half_focused : GetValue() ? m_on_focused : m_off_focused).bmp());
    SetBitmapCurrent((m_half_checked ? m_half_focused : GetValue() ? m_on_focused : m_off_focused).bmp());
}

CheckBox::State CheckBox::GetNormalState() const { return State_Normal; }
