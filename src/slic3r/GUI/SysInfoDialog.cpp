#include "SysInfoDialog.hpp"
#include "I18N.hpp"
#include "3DScene.hpp"
#include "GUI.hpp"
#include "../Utils/UndoRedo.hpp"

#include <string>

#include <wx/clipbrd.h>
#include <wx/platinfo.h>
#include "GUI_App.hpp"
#include "wxExtensions.hpp"

#ifdef _WIN32
	// The standard Windows includes.
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include <Windows.h>
	#include <psapi.h>
#endif /* _WIN32 */

namespace Slic3r { 
namespace GUI {

std::string get_main_info(bool format_as_html)
{
    std::stringstream out;

    std::string b_start  = format_as_html ? "<b>"  : "";
    std::string b_end    = format_as_html ? "</b>" : "";
    std::string line_end = format_as_html ? "<br>" : "\n";

    if (!format_as_html)
        out << b_start << SLIC3R_APP_NAME << b_end << line_end;
    out << b_start << "Version:   "             << b_end << SLIC3R_VERSION << line_end;
    out << b_start << "Build:     "             << b_end << SLIC3R_BUILD_ID << line_end;
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
    out << b_start << "Total RAM size [MB]: "  << b_end << Slic3r::format_memsize_MB(Slic3r::total_physical_memory());

    return out.str();
}

std::string get_mem_info(bool format_as_html)
{
    std::stringstream out;

    std::string b_start  = format_as_html ? "<b>"  : "";
    std::string b_end    = format_as_html ? "</b>" : "";
    std::string line_end = format_as_html ? "<br>" : "\n";

    const Slic3r::UndoRedo::Stack &stack = wxGetApp().plater()->undo_redo_stack();
    out << b_start << "RAM size reserved for the Undo / Redo stack [MB]: "  << b_end << Slic3r::format_memsize_MB(stack.get_memory_limit()) << line_end;
    out << b_start << "RAM size occupied by the Undo / Redo stack  [MB]: "  << b_end << Slic3r::format_memsize_MB(stack.memsize()) << line_end << line_end;

#ifdef _WIN32
   	HANDLE hProcess = ::OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, ::GetCurrentProcessId());
    if (hProcess != nullptr) {
        PROCESS_MEMORY_COUNTERS_EX pmc;
        if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc)))
			out << b_start << "WorkingSet     [MB]: "   << b_end << format_memsize_MB(pmc.WorkingSetSize) << line_end
				<< b_start << "PrivateBytes   [MB]: " << b_end << format_memsize_MB(pmc.PrivateUsage) << line_end
				<< b_start << "Pagefile(peak) [MB]: " << b_end << format_memsize_MB(pmc.PagefileUsage) << "(" << format_memsize_MB(pmc.PeakPagefileUsage) << ")" << line_end;
        CloseHandle(hProcess);
    }
#endif
    return out.str();
}

SysInfoDialog::SysInfoDialog()
    : DPIDialog(NULL, wxID_ANY, wxString(SLIC3R_APP_NAME) + " - " + _(L("System Information")), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE|wxRESIZE_BORDER)
{
	wxColour bgr_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
	SetBackgroundColour(bgr_clr);
    SetFont(wxGetApp().normal_font());

    wxBoxSizer* hsizer = new wxBoxSizer(wxHORIZONTAL);
    hsizer->SetMinSize(wxSize(50 * wxGetApp().em_unit(), -1));

	auto main_sizer = new wxBoxSizer(wxVERTICAL);
	main_sizer->Add(hsizer, 1, wxEXPAND | wxALL, 10);

    // logo
    m_logo_bmp = ScalableBitmap(this, "PrusaSlicer_192px.png", 192);
    m_logo = new wxStaticBitmap(this, wxID_ANY, m_logo_bmp.bmp());
	hsizer->Add(m_logo, 0, wxALIGN_CENTER_VERTICAL);
    
    wxBoxSizer* vsizer = new wxBoxSizer(wxVERTICAL);
    hsizer->Add(vsizer, 1, wxEXPAND|wxLEFT, 20);

    // title
    {
        wxStaticText* title = new wxStaticText(this, wxID_ANY, SLIC3R_APP_NAME, wxDefaultPosition, wxDefaultSize);
        wxFont title_font = wxGetApp().bold_font();
        title_font.SetFamily(wxFONTFAMILY_ROMAN);
        title_font.SetPointSize(22);
        title->SetFont(title_font);
        vsizer->Add(title, 0, wxEXPAND | wxALIGN_LEFT | wxTOP, wxGetApp().em_unit()/*50*/);
    }

    // main_info_text
    wxFont font = wxGetApp().normal_font();
    const auto text_clr = wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT);
    auto text_clr_str = wxString::Format(wxT("#%02X%02X%02X"), text_clr.Red(), text_clr.Green(), text_clr.Blue());
    auto bgr_clr_str = wxString::Format(wxT("#%02X%02X%02X"), bgr_clr.Red(), bgr_clr.Green(), bgr_clr.Blue());

    const int fs = font.GetPointSize() - 1;
    int size[] = { static_cast<int>(fs*1.5), static_cast<int>(fs*1.4), static_cast<int>(fs*1.3), fs, fs, fs, fs };

    m_html = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_NEVER);
    {
        m_html->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
        m_html->SetBorders(2);
		const auto text = wxString::Format(
            "<html>"
            "<body bgcolor= %s link= %s>"
            "<font color=%s>"
            "%s"
            "</font>"
            "</body>"
            "</html>", bgr_clr_str, text_clr_str, text_clr_str,
            get_main_info(true));
        m_html->SetPage(text);
        vsizer->Add(m_html, 1, wxEXPAND | wxBOTTOM, wxGetApp().em_unit());
    }

    // opengl_info
    m_opengl_info_html = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO);
    {
        m_opengl_info_html->SetMinSize(wxSize(-1, 16 * wxGetApp().em_unit()));
        m_opengl_info_html->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
        m_opengl_info_html->SetBorders(10);
        const auto text = wxString::Format(
            "<html>"
            "<body bgcolor= %s link= %s>"
            "<font color=%s>"
            "%s"
            "</font>"
            "</body>"
            "</html>", bgr_clr_str, text_clr_str, text_clr_str,
            get_mem_info(true) + "<br>" + _3DScene::get_gl_info(true, true));
        m_opengl_info_html->SetPage(text);
        main_sizer->Add(m_opengl_info_html, 1, wxEXPAND | wxBOTTOM, 15);
    }
    
    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxOK);
    m_btn_copy_to_clipboard = new wxButton(this, wxID_ANY, _(L("Copy to Clipboard")), wxDefaultPosition, wxDefaultSize);

    buttons->Insert(0, m_btn_copy_to_clipboard, 0, wxLEFT, 5);
    m_btn_copy_to_clipboard->Bind(wxEVT_BUTTON, &SysInfoDialog::onCopyToClipboard, this);

    this->SetEscapeId(wxID_OK);
    this->Bind(wxEVT_BUTTON, &SysInfoDialog::onCloseDialog, this, wxID_OK);
    main_sizer->Add(buttons, 0, wxEXPAND | wxRIGHT | wxBOTTOM, 3);
    
//     this->Bind(wxEVT_LEFT_DOWN, &SysInfoDialog::onCloseDialog, this);
//     logo->Bind(wxEVT_LEFT_DOWN, &SysInfoDialog::onCloseDialog, this);

	SetSizer(main_sizer);
	main_sizer->SetSizeHints(this);
}

void SysInfoDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_logo_bmp.msw_rescale();
    m_logo->SetBitmap(m_logo_bmp.bmp());

    wxFont font = GetFont();
    const int fs = font.GetPointSize() - 1;
    int font_size[] = { static_cast<int>(fs*1.5), static_cast<int>(fs*1.4), static_cast<int>(fs*1.3), fs, fs, fs, fs };

    m_html->SetFonts(font.GetFaceName(), font.GetFaceName(), font_size);
    m_html->Refresh();

    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_OK, m_btn_copy_to_clipboard->GetId() });

    m_opengl_info_html->SetMinSize(wxSize(-1, 16 * em));
    m_opengl_info_html->SetFonts(font.GetFaceName(), font.GetFaceName(), font_size);
    m_opengl_info_html->Refresh();

    const wxSize& size = wxSize(65 * em, 55 * em);

    SetMinSize(size);
    Fit();

    Refresh();
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
}

} // namespace GUI
} // namespace Slic3r
