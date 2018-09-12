#include "AboutDialog.hpp"

#include "../../libslic3r/Utils.hpp"

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
    : wxDialog(NULL, wxID_ANY, _(L("About Slic3r")), wxDefaultPosition, wxDefaultSize, wxCAPTION)
{
	wxColour bgr_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
	SetBackgroundColour(bgr_clr);
    wxBoxSizer* hsizer = new wxBoxSizer(wxHORIZONTAL);

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(hsizer, 0, wxEXPAND | wxALL, 20);

    // logo
	wxBitmap logo_bmp = wxBitmap(from_u8(Slic3r::var("Slic3r_192px.png")), wxBITMAP_TYPE_PNG);
	auto *logo = new wxStaticBitmap(this, wxID_ANY, std::move(logo_bmp));
	hsizer->Add(logo, 1, wxALIGN_CENTRE_VERTICAL | wxEXPAND | wxTOP | wxBOTTOM, 35);
    
    wxBoxSizer* vsizer = new wxBoxSizer(wxVERTICAL);
#ifdef __WXMSW__
	int proportion = 2;
#else
	int proportion = 3;
#endif
    hsizer->Add(vsizer, proportion, wxEXPAND|wxLEFT, 20);

    // title
    {
        wxStaticText* title = new wxStaticText(this, wxID_ANY, "Slic3r Prusa Edition", wxDefaultPosition, wxDefaultSize);
        wxFont title_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        title_font.SetWeight(wxFONTWEIGHT_BOLD);
        title_font.SetFamily(wxFONTFAMILY_ROMAN);
        title_font.SetPointSize(24);
        title->SetFont(title_font);
        vsizer->Add(title, 0, wxALIGN_LEFT | wxTOP, 10);
    }
    
    // version
    {
        auto version_string = _(L("Version"))+ " " + std::string(SLIC3R_VERSION);
        wxStaticText* version = new wxStaticText(this, wxID_ANY, version_string.c_str(), wxDefaultPosition, wxDefaultSize);
        wxFont version_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        #ifdef __WXMSW__
            version_font.SetPointSize(9);
        #else
            version_font.SetPointSize(11);
        #endif
        version->SetFont(version_font);
        vsizer->Add(version, 0, wxALIGN_LEFT | wxBOTTOM, 10);
    }
    
    // text
    wxHtmlWindow* html = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO/*NEVER*/);
    {
        wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        const auto text_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
		auto text_clr_str = wxString::Format(wxT("#%02X%02X%02X"), text_clr.Red(), text_clr.Green(), text_clr.Blue());
		auto bgr_clr_str = wxString::Format(wxT("#%02X%02X%02X"), bgr_clr.Red(), bgr_clr.Green(), bgr_clr.Blue());

		const int fs = font.GetPointSize()-1;
        int size[] = {fs,fs,fs,fs,fs,fs,fs};
        html->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
        html->SetBorders(2);
		const auto text = wxString::Format(
            "<html>"
            "<body bgcolor= %s link= %s>"
            "<font color=%s>"
            "Copyright &copy; 2016-2018 Prusa Research. <br />"
            "Copyright &copy; 2011-2017 Alessandro Ranellucci. <br />"
            "<a href=\"http://slic3r.org/\">Slic3r</a> is licensed under the "
            "<a href=\"http://www.gnu.org/licenses/agpl-3.0.html\">GNU Affero General Public License, version 3</a>."
            "<br /><br />"
            "Contributions by Henrik Brix Andersen, Nicolas Dandrimont, Mark Hindess, Petr Ledvina, Joseph Lenox, Y. Sapir, Mike Sheldrake, Vojtech Bubnik and numerous others. "
            "Manual by Gary Hodgson. Inspired by the RepRap community. <br />"
            "Slic3r logo designed by Corey Daniels, <a href=\"http://www.famfamfam.com/lab/icons/silk/\">Silk Icon Set</a> designed by Mark James. "
            "</font>"
            "</body>"
            "</html>", bgr_clr_str, text_clr_str, text_clr_str);
        html->SetPage(text);
        vsizer->Add(html, 1, wxEXPAND | wxBOTTOM, 10);
        html->Bind(wxEVT_HTML_LINK_CLICKED, &AboutDialog::onLinkClicked, this);
    }
    
    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxCLOSE);
    this->SetEscapeId(wxID_CLOSE);
    this->Bind(wxEVT_BUTTON, &AboutDialog::onCloseDialog, this, wxID_CLOSE);
    vsizer->Add(buttons, 0, wxEXPAND | wxRIGHT | wxBOTTOM, 3);
    
    this->Bind(wxEVT_LEFT_DOWN, &AboutDialog::onCloseDialog, this);
    logo->Bind(wxEVT_LEFT_DOWN, &AboutDialog::onCloseDialog, this);

	SetSizer(main_sizer);
	main_sizer->SetSizeHints(this);
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
