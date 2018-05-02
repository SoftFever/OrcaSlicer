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
    : wxDialog(NULL, wxID_ANY, _(L("About Slic3r")), wxDefaultPosition, wxSize(600, 340), wxCAPTION)
{
	SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)/**wxWHITE*/);
    wxBoxSizer* hsizer = new wxBoxSizer(wxHORIZONTAL);
    this->SetSizer(hsizer);

    // logo
//     AboutDialogLogo* logo = new AboutDialogLogo(this);
	wxBitmap logo_bmp = wxBitmap(from_u8(Slic3r::var("Slic3r_192px.png")), wxBITMAP_TYPE_PNG);
	auto *logo = new wxStaticBitmap(this, wxID_ANY, std::move(logo_bmp));
    hsizer->Add(logo, 0, wxEXPAND | wxLEFT | wxRIGHT, 30);
    
    wxBoxSizer* vsizer = new wxBoxSizer(wxVERTICAL);
    hsizer->Add(vsizer, 1, wxEXPAND, 0);

    // title
    {
        wxStaticText* title = new wxStaticText(this, wxID_ANY, "Slic3r Prusa Edition", wxDefaultPosition, wxDefaultSize);
        wxFont title_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        title_font.SetWeight(wxFONTWEIGHT_BOLD);
        title_font.SetFamily(wxFONTFAMILY_ROMAN);
        title_font.SetPointSize(24);
        title->SetFont(title_font);
        vsizer->Add(title, 0, wxALIGN_LEFT | wxTOP, 30);
    }
    
    // version
    {
        auto version_string = _(L("Version ")) + std::string(SLIC3R_VERSION);
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
    wxHtmlWindow* html = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_NEVER);
    {
        wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        #ifdef __WXMSW__
            int size[] = {8,8,8,8,8,8,8};
        #else
            int size[] = {11,11,11,11,11,11,11};
        #endif
        html->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
		html->SetHTMLBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
        html->SetBorders(2);
        const char* text =
            "<html>"
            "<body bgcolor=\"#ffffff\" link=\"#808080\">"
            "<font color=\"#808080\">"
            "Copyright &copy; 2016-2018 Prusa Research. <br />"
            "Copyright &copy; 2011-2017 Alessandro Ranellucci. <br />"
            "<a href=\"http://slic3r.org/\">Slic3r</a> is licensed under the "
            "<a href=\"http://www.gnu.org/licenses/agpl-3.0.html\">GNU Affero General Public License, version 3</a>."
            "<br /><br /><br />"
            "Contributions by Henrik Brix Andersen, Nicolas Dandrimont, Mark Hindess, Petr Ledvina, Joseph Lenox, Y. Sapir, Mike Sheldrake, Vojtech Bubnik and numerous others. "
            "Manual by Gary Hodgson. Inspired by the RepRap community. <br />"
            "Slic3r logo designed by Corey Daniels, <a href=\"http://www.famfamfam.com/lab/icons/silk/\">Silk Icon Set</a> designed by Mark James. "
            "</font>"
            "</body>"
            "</html>";
        html->SetPage(text);
        vsizer->Add(html, 1, wxEXPAND | wxALIGN_LEFT | wxRIGHT | wxBOTTOM, 20);
        html->Bind(wxEVT_HTML_LINK_CLICKED, &AboutDialog::onLinkClicked, this);
    }
    
    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxCLOSE);
    this->SetEscapeId(wxID_CLOSE);
    this->Bind(wxEVT_BUTTON, &AboutDialog::onCloseDialog, this, wxID_CLOSE);
    vsizer->Add(buttons, 0, wxEXPAND | wxRIGHT | wxBOTTOM, 3);
    
    this->Bind(wxEVT_LEFT_DOWN, &AboutDialog::onCloseDialog, this);
    logo->Bind(wxEVT_LEFT_DOWN, &AboutDialog::onCloseDialog, this);
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
