#include "RadioBox.hpp"

#include "../wxExtensions.hpp"

namespace Slic3r { 
namespace GUI {
RadioBox::RadioBox(wxWindow *parent)
    : wxBitmapToggleButton(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), m_on(this, "radio_on", 18), m_off(this, "radio_off", 18)
{
    // SetBackgroundStyle(wxBG_STYLE_TRANSPARENT);
    if (parent) SetBackgroundColour(parent->GetBackgroundColour());
    // Bind(wxEVT_TOGGLEBUTTON, [this](auto& e) { update(); e.Skip(); });
    SetSize(m_on.GetSize());
    SetMinSize(m_on.GetSize());
    update();
}

void RadioBox::SetValue(bool value)
{
    wxBitmapToggleButton::SetValue(value);
    update();
}

bool RadioBox::GetValue()
{
    return wxBitmapToggleButton::GetValue();
}


void RadioBox::Rescale()
{
    m_on.sys_color_changed();
    m_off.sys_color_changed();
    SetSize(m_on.GetSize());
    update();
}

void RadioBox::update() { SetBitmap((GetValue() ? m_on : m_off).bmp()); }

}
}

