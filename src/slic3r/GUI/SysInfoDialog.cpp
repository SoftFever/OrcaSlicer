#include "SysInfoDialog.hpp"
#include "I18N.hpp"
#include "3DScene.hpp"
#include "GUI.hpp"

#include <wx/clipbrd.h>
#include <wx/platinfo.h>
#include "GUI_App.hpp"

namespace Slic3r { 
namespace GUI {

std::string get_main_info(bool format_as_html)
{
    std::stringstream out;

    std::string b_start  = format_as_html ? "<b>"  : "";
    std::string b_end    = format_as_html ? "</b>" : "";
    std::string line_end = format_as_html ? "<br>" : "\n";

    if (!format_as_html)
        out << b_start << SLIC3R_FORK_NAME << b_end << line_end;
    out << b_start << "Version:   "             << b_end << SLIC3R_VERSION << line_end;
    out << b_start << "Build:     "             << b_end << SLIC3R_BUILD << line_end;
    out << line_end;
    out << b_start << "Operating System:    "   << b_end << wxPlatformInfo::Get().GetOperatingSystemFamilyName() << line_end;
    out << b_start << "System Architecture: "   << b_end << wxPlatformInfo::Get().GetArchName() << line_end;
    out << b_start << 
#if defined _WIN32
        "Windows Version:     "
#else
        // Hopefully some kind of unix / linux.
        "System Version:      "
#endif
        << b_end << wxPlatformInfo::Get().GetOperatingSystemDescription() << line_end;

    return out.str();
}

SysInfoDialog::SysInfoDialog()
    : wxDialog(NULL, wxID_ANY, _(L("Slic3r Prusa Edition - System Information")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER)
{
	wxColour bgr_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
	SetBackgroundColour(bgr_clr);
    wxBoxSizer* hsizer = new wxBoxSizer(wxHORIZONTAL);
    hsizer->SetMinSize(wxSize(50 * wxGetApp().em_unit(), -1));

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(hsizer, 1, wxEXPAND | wxALL, 10);

    // logo
	wxBitmap logo_bmp = wxBitmap(from_u8(Slic3r::var("Slic3r_192px.png")), wxBITMAP_TYPE_PNG);
    auto *logo = new wxStaticBitmap(this, wxID_ANY, std::move(logo_bmp));
	hsizer->Add(logo, 0, wxALIGN_CENTER_VERTICAL);
    
    wxBoxSizer* vsizer = new wxBoxSizer(wxVERTICAL);
    hsizer->Add(vsizer, 1, wxEXPAND|wxLEFT, 20);

    // title
    {
        wxStaticText* title = new wxStaticText(this, wxID_ANY, SLIC3R_FORK_NAME, wxDefaultPosition, wxDefaultSize);
        wxFont title_font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        title_font.SetWeight(wxFONTWEIGHT_BOLD);
        title_font.SetFamily(wxFONTFAMILY_ROMAN);
        title_font.SetPointSize(22);
        title->SetFont(title_font);
        vsizer->Add(title, 0, wxEXPAND | wxALIGN_LEFT | wxTOP, wxGetApp().em_unit()/*50*/);
    }

    // main_info_text
    wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    const auto text_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    auto text_clr_str = wxString::Format(wxT("#%02X%02X%02X"), text_clr.Red(), text_clr.Green(), text_clr.Blue());
    auto bgr_clr_str = wxString::Format(wxT("#%02X%02X%02X"), bgr_clr.Red(), bgr_clr.Green(), bgr_clr.Blue());

    const int fs = font.GetPointSize() - 1;
    int size[] = { static_cast<int>(fs*1.5), static_cast<int>(fs*1.4), static_cast<int>(fs*1.3), fs, fs, fs, fs };

    wxHtmlWindow* html = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_NEVER);
    {
        html->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
        html->SetBorders(2);
		const auto text = wxString::Format(
            "<html>"
            "<body bgcolor= %s link= %s>"
            "<font color=%s>"
            "%s"
            "</font>"
            "</body>"
            "</html>", bgr_clr_str, text_clr_str, text_clr_str,
            get_main_info(true));
        html->SetPage(text);
        vsizer->Add(html, 1, wxEXPAND | wxBOTTOM, wxGetApp().em_unit());
    }

    // opengl_info
    wxHtmlWindow* opengl_info_html = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO);
    {
        opengl_info_html->SetMinSize(wxSize(-1, 16 * wxGetApp().em_unit()));
        opengl_info_html->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
        opengl_info_html->SetBorders(10);
        const auto text = wxString::Format(
            "<html>"
            "<body bgcolor= %s link= %s>"
            "<font color=%s>"
            "%s"
            "</font>"
            "</body>"
            "</html>", bgr_clr_str, text_clr_str, text_clr_str,
            _3DScene::get_gl_info(true, true));
        opengl_info_html->SetPage(text);
        main_sizer->Add(opengl_info_html, 1, wxEXPAND | wxBOTTOM, 15);
    }
    
    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxOK);
    auto btn_copy_to_clipboard = new wxButton(this, wxID_ANY, "Copy to Clipboard", wxDefaultPosition, wxDefaultSize);
    buttons->Insert(0, btn_copy_to_clipboard, 0, wxLEFT, 5);
    btn_copy_to_clipboard->Bind(wxEVT_BUTTON, &SysInfoDialog::onCopyToClipboard, this);

    this->SetEscapeId(wxID_OK);
    this->Bind(wxEVT_BUTTON, &SysInfoDialog::onCloseDialog, this, wxID_OK);
    main_sizer->Add(buttons, 0, wxEXPAND | wxRIGHT | wxBOTTOM, 3);
    
//     this->Bind(wxEVT_LEFT_DOWN, &SysInfoDialog::onCloseDialog, this);
//     logo->Bind(wxEVT_LEFT_DOWN, &SysInfoDialog::onCloseDialog, this);

	SetSizer(main_sizer);
	main_sizer->SetSizeHints(this);
}

void SysInfoDialog::onCopyToClipboard(wxEvent &)
{
    wxTheClipboard->Open();
    const auto text = get_main_info(false)+"\n"+_3DScene::get_gl_info(false, true);
    wxTheClipboard->SetData(new wxTextDataObject(text));
    wxTheClipboard->Close();
}

void SysInfoDialog::onCloseDialog(wxEvent &)
{
    this->EndModal(wxID_CLOSE);
    this->Close();
}

} // namespace GUI
} // namespace Slic3r
