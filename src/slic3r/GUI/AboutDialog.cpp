#include "AboutDialog.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "GUI_App.hpp"
#include "wxExtensions.hpp"

namespace Slic3r { 
namespace GUI {

AboutDialogLogo::AboutDialogLogo(wxWindow* parent)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
{
    this->SetBackgroundColour(*wxWHITE);
    this->logo = wxBitmap(from_u8(Slic3r::var("Slic3r_192px.png")), wxBITMAP_TYPE_PNG);
    this->SetMinSize(this->logo.GetSize());
    
    this->Bind(wxEVT_PAINT, &AboutDialogLogo::onRepaint, this);
}

void AboutDialogLogo::onRepaint(wxEvent &event)
{
    wxPaintDC dc(this);
    dc.SetBackgroundMode(wxTRANSPARENT);

    wxSize size = this->GetSize();
    int logo_w = this->logo.GetWidth();
    int logo_h = this->logo.GetHeight();
    dc.DrawBitmap(this->logo, (size.GetWidth() - logo_w)/2, (size.GetHeight() - logo_h)/2, true);

    event.Skip();
}

AboutDialog::AboutDialog()
    : DPIDialog(NULL, wxID_ANY, wxString::Format(_(L("About %s")), SLIC3R_APP_NAME), wxDefaultPosition, 
                wxDefaultSize, /*wxCAPTION*/wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
{
    SetFont(wxGetApp().normal_font());

	wxColour bgr_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
	SetBackgroundColour(bgr_clr);
    wxBoxSizer* hsizer = new wxBoxSizer(wxHORIZONTAL);

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(hsizer, 0, wxEXPAND | wxALL, 20);

    // logo
    m_logo_bitmap = ScalableBitmap(this, "Slic3r_192px.png", 192);
    m_logo = new wxStaticBitmap(this, wxID_ANY, m_logo_bitmap.bmp());
	hsizer->Add(m_logo, 1, wxALIGN_CENTER_VERTICAL);
    
    wxBoxSizer* vsizer = new wxBoxSizer(wxVERTICAL); 	
    hsizer->Add(vsizer, 2, wxEXPAND|wxLEFT, 20);

    // title
    {
        wxStaticText* title = new wxStaticText(this, wxID_ANY, SLIC3R_APP_NAME, wxDefaultPosition, wxDefaultSize);
        wxFont title_font = GUI::wxGetApp().bold_font();
        title_font.SetFamily(wxFONTFAMILY_ROMAN);
        title_font.SetPointSize(24);
        title->SetFont(title_font);
        vsizer->Add(title, 0, wxALIGN_LEFT | wxTOP, 10);
    }
    
    // version
    {
        auto version_string = _(L("Version"))+ " " + std::string(SLIC3R_VERSION);
        wxStaticText* version = new wxStaticText(this, wxID_ANY, version_string.c_str(), wxDefaultPosition, wxDefaultSize);
        wxFont version_font = GetFont();
        #ifdef __WXMSW__
        version_font.SetPointSize(version_font.GetPointSize()-1);
        #else
            version_font.SetPointSize(11);
        #endif
        version->SetFont(version_font);
        vsizer->Add(version, 0, wxALIGN_LEFT | wxBOTTOM, 10);
    }
    
    // text
    m_html = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO/*NEVER*/);
    {
        m_html->SetMinSize(wxSize(-1, 16 * wxGetApp().em_unit()));
        wxFont font = GetFont();
        const auto text_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
		auto text_clr_str = wxString::Format(wxT("#%02X%02X%02X"), text_clr.Red(), text_clr.Green(), text_clr.Blue());
		auto bgr_clr_str = wxString::Format(wxT("#%02X%02X%02X"), bgr_clr.Red(), bgr_clr.Green(), bgr_clr.Blue());

		const int fs = font.GetPointSize()-1;
        int size[] = {fs,fs,fs,fs,fs,fs,fs};
        m_html->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
        m_html->SetBorders(2);
        const wxString copyright_str = _(L("Copyright"));
        // TRN "Slic3r _is licensed under the_ License"
        const wxString is_lecensed_str = _(L("is licensed under the"));
        const wxString license_str = _(L("GNU Affero General Public License, version 3"));
        const wxString contributors_str = _(L("Contributions by Henrik Brix Andersen, Nicolas Dandrimont, Mark Hindess, Petr Ledvina, Joseph Lenox, Y. Sapir, Mike Sheldrake, Vojtech Bubnik and numerous others."));
		const auto text = wxString::Format(
            "<html>"
            "<body bgcolor= %s link= %s>"
            "<font color=%s>"
            "%s &copy; 2016-2019 Prusa Research. <br />"
            "%s &copy; 2011-2018 Alessandro Ranellucci. <br />"
            "<a href=\"http://slic3r.org/\">Slic3r</a> %s "
            "<a href=\"http://www.gnu.org/licenses/agpl-3.0.html\">%s</a>."
            "<br /><br />"
            "%s"
            "</font>"
            "</body>"
            "</html>", bgr_clr_str, text_clr_str, text_clr_str,
            copyright_str, copyright_str,
            is_lecensed_str,
            license_str,
            contributors_str);
        m_html->SetPage(text);
        vsizer->Add(m_html, 1, wxEXPAND | wxBOTTOM, 10);
        m_html->Bind(wxEVT_HTML_LINK_CLICKED, &AboutDialog::onLinkClicked, this);
    }
    
    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxCLOSE);
    this->SetEscapeId(wxID_CLOSE);
    this->Bind(wxEVT_BUTTON, &AboutDialog::onCloseDialog, this, wxID_CLOSE);
    vsizer->Add(buttons, 0, wxEXPAND | wxRIGHT | wxBOTTOM, 3);

	SetSizer(main_sizer);
	main_sizer->SetSizeHints(this);
}

void AboutDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_logo_bitmap.msw_rescale();
    m_logo->SetBitmap(m_logo_bitmap.bmp());

    const wxFont& font = GetFont();
    const int fs = font.GetPointSize() - 1;
    int font_size[] = { fs, fs, fs, fs, fs, fs, fs };
    m_html->SetFonts(font.GetFaceName(), font.GetFaceName(), font_size);

    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_CLOSE });

    m_html->SetMinSize(wxSize(-1, 16 * em));
    m_html->Refresh();

    const wxSize& size = wxSize(65 * em, 30 * em);

    SetMinSize(size);
    Fit();

    Refresh();
}

void AboutDialog::onLinkClicked(wxHtmlLinkEvent &event)
{
    wxLaunchDefaultBrowser(event.GetLinkInfo().GetHref());
    event.Skip(false);
}

void AboutDialog::onCloseDialog(wxEvent &)
{
    this->EndModal(wxID_CLOSE);
    this->Close();
}

} // namespace GUI
} // namespace Slic3r
