#include "HyperLink.hpp"

namespace Slic3r {
namespace GUI {

HyperLink::HyperLink(
    wxWindow* parent,
    const wxString& label,
    const wxString& url
)
    : wxStaticText(parent, wxID_ANY, label)
    , m_url(url)
    , m_normalColor(wxColour("#009687")) // used slightly different color otherwise automatically uses ColorForDark that not visible enough
    , m_hoverColor( wxColour("#26A69A"))
{
    // example usage new HyperLink(this, _L("Releases"), HyperLink::For(HyperLinkType::Releases));
    // or new HyperLink(this, _L("Releases")) and bind wxEVT_LEFT_DOWN

    SetForegroundColour(m_normalColor);
    HyperLink::SetFont(Label::Head_14);
    SetCursor(wxCursor(wxCURSOR_HAND));

    Bind(wxEVT_LEFT_DOWN   ,([this](wxMouseEvent e) {if (!m_url.IsEmpty()) wxLaunchDefaultBrowser(m_url);}));

    Bind(wxEVT_ENTER_WINDOW,([this](wxMouseEvent e) {SetForegroundColour(m_hoverColor ); Refresh();}));
    Bind(wxEVT_LEAVE_WINDOW,([this](wxMouseEvent e) {SetForegroundColour(m_normalColor); Refresh();}));
}

void HyperLink::SetFont(wxFont& font)
{   // make sure its underlined
    wxFont f = font;
    f.SetUnderlined(true);
    wxStaticText::SetFont(f);
}

}
}