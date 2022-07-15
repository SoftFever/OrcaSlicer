#include "CheckBox.hpp"

#include "../wxExtensions.hpp"

CheckBox::CheckBox(wxWindow* parent)
	: wxBitmapToggleButton(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
	, m_on(this, "check_on", 16)
	, m_half(this, "check_half", 16)
	, m_off(this, "check_off", 16)
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
	SetBitmap((m_half_checked ? m_half : GetValue() ? m_on : m_off).bmp());
}