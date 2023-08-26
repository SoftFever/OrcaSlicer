#include "ConfigSnapshotDialog.hpp"
#include "I18N.hpp"

#include "../Config/Snapshot.hpp"

#include "libslic3r/Utils.hpp"
#include "libslic3r/Time.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "wxExtensions.hpp"

namespace Slic3r { 
namespace GUI {

static wxString format_reason(const Config::Snapshot::Reason reason) 
{
    switch (reason) {
    case Config::Snapshot::SNAPSHOT_UPGRADE:
        return wxString(_(L("Upgrade")));
    case Config::Snapshot::SNAPSHOT_DOWNGRADE:
        return wxString(_(L("Downgrade")));
    case Config::Snapshot::SNAPSHOT_BEFORE_ROLLBACK:
        return wxString(_(L("Before roll back")));
    case Config::Snapshot::SNAPSHOT_USER:
        return wxString(_(L("User")));
    case Config::Snapshot::SNAPSHOT_UNKNOWN:
    default:
        return wxString(_(L("Unknown")));
    }
}

static std::string get_color(wxColour colour) 
{
    wxString clr_str = wxString::Format(wxT("#%02X%02X%02X"), colour.Red(), colour.Green(), colour.Blue());
    return clr_str.ToStdString();
};


static wxString generate_html_row(const Config::Snapshot &snapshot, bool row_even, bool snapshot_active, bool dark_mode)
{    
    // Start by declaring a row with an alternating background color.
    wxString text = "<tr bgcolor=\"";
    text += snapshot_active ? 
            dark_mode ? "#208a20"  : "#B3FFCB" : 
            (row_even ? get_color(wxGetApp().get_window_default_clr()/*wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)*/) : dark_mode ? "#656565" : "#D5D5D5" );
    text += "\">";
    text += "<td>";
    
    static const constexpr char *LOCALE_TIME_FMT = "%x %X";
    wxString datetime = wxDateTime(snapshot.time_captured).Format(LOCALE_TIME_FMT);
    
    // Format the row header.
    text += wxString("<font size=\"5\"><b>") + (snapshot_active ? _(L("Active")) + ": " : "") +
            datetime + ": " + format_reason(snapshot.reason);
    
    if (! snapshot.comment.empty())
        text += " (" + wxString::FromUTF8(snapshot.comment.data()) + ")";
    text += "</b></font><br>";
    // End of row header.
    text += _(L("PrusaSlicer version")) + ": " + snapshot.slic3r_version_captured.to_string() + "<br>";
    bool has_fff = ! snapshot.print.empty() || ! snapshot.filaments.empty();
    bool has_sla = ! snapshot.sla_print.empty() || ! snapshot.sla_material.empty();
    if (has_fff || ! has_sla) {
        text += _(L("print")) + ": " + snapshot.print + "<br>";
        text += _(L("filaments")) + ": " + snapshot.filaments.front() + "<br>";
    }
    if (has_sla) {
        text += _(L("SLA print")) + ": " + snapshot.sla_print + "<br>";
        text += _(L("SLA material")) + ": " + snapshot.sla_material + "<br>";
    }
    text += _(L("printer")) + ": " + (snapshot.physical_printer.empty() ? snapshot.printer : snapshot.physical_printer) + "<br>";

    bool compatible = true;
    for (const Config::Snapshot::VendorConfig &vc : snapshot.vendor_configs) {
        text += _(L("vendor")) + ": " + vc.name +", " + _(L("version")) + ": " + vc.version.config_version.to_string() + 
				", " + _(L("min PrusaSlicer version")) + ": " + vc.version.min_slic3r_version.to_string();
        if (vc.version.max_slic3r_version != Semver::inf())
            text += ", " + _(L("max PrusaSlicer version")) + ": " + vc.version.max_slic3r_version.to_string();
        text += "<br>";
        for (const auto& model : vc.models_variants_installed) {
            text += _(L("model")) + ": " + model.first + ", " + _(L("variants")) + ": ";
            for (const std::string &variant : model.second) {
                if (&variant != &*model.second.begin())
                    text += ", ";
                text += variant;
            }
            text += "<br>";
        }
        if (! vc.version.is_current_slic3r_supported()) { compatible = false; }
    }

    if (! compatible) {
        text += "<p align=\"right\">" + from_u8((boost::format(_utf8(L("Incompatible with this %s"))) % SLIC3R_APP_NAME).str()) + "</p>";
    }
    else if (! snapshot_active)
        text += "<p align=\"right\"><a href=\"" + snapshot.id + "\">" + _(L("Activate")) + "</a></p>";
    text += "</td>";
	text += "</tr>";
    return text;
}

static wxString generate_html_page(const Config::SnapshotDB &snapshot_db, const wxString &on_snapshot)
{
    bool dark_mode = wxGetApp().dark_mode();
    wxString text = 
        "<html>"
        "<body bgcolor=\"" + get_color(wxGetApp().get_window_default_clr()/*wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW)*/) + "\" cellspacing=\"2\" cellpadding=\"0\" border=\"0\" link=\"#800000\">"
        "<font color=\"" + get_color(wxGetApp().get_label_clr_default()/*wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOWTEXT)*/) + "\">";
    text += "<table style=\"width:100%\">";
    for (size_t i_row = 0; i_row < snapshot_db.snapshots().size(); ++ i_row) {
        const Config::Snapshot &snapshot = snapshot_db.snapshots()[snapshot_db.snapshots().size() - i_row - 1];
        text += generate_html_row(snapshot, i_row & 1, snapshot.id == on_snapshot, dark_mode);
    }
    text +=
        "</table>"
        "</font>"
        "</body>"
        "</html>";
    return text;
}

ConfigSnapshotDialog::ConfigSnapshotDialog(const Config::SnapshotDB &snapshot_db, const wxString &on_snapshot)
    : DPIDialog(static_cast<wxWindow*>(wxGetApp().mainframe), wxID_ANY, _(L("Configuration Snapshots")), wxDefaultPosition,
               wxSize(45 * wxGetApp().em_unit(), 40 * wxGetApp().em_unit()), 
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX)
{
    this->SetFont(wxGetApp().normal_font());
#ifdef _WIN32
    wxGetApp().UpdateDarkUI(this);
#else
    this->SetBackgroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW));
#endif

    wxBoxSizer* vsizer = new wxBoxSizer(wxVERTICAL);
    this->SetSizer(vsizer);

    // text
    html = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO);
    {
        wxFont font = get_default_font(this);
        #ifdef __WXMSW__
            const int fs = font.GetPointSize();
            const int fs1 = static_cast<int>(0.8f*fs);
            const int fs2 = static_cast<int>(1.1f*fs);
            int size[] = {fs1, fs1, fs1, fs1, fs2, fs2, fs2};
//             int size[] = {8,8,8,8,11,11,11};
        #else
            int size[] = {11,11,11,11,14,14,14};
        #endif
        html->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
        html->SetBorders(2);
        wxString text = generate_html_page(snapshot_db, on_snapshot);
        html->SetPage(text);
        vsizer->Add(html, 1, wxEXPAND | wxALIGN_LEFT | wxRIGHT | wxBOTTOM, 0);
        html->Bind(wxEVT_HTML_LINK_CLICKED, &ConfigSnapshotDialog::onLinkClicked, this);
    }
    
    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxCLOSE);
    wxGetApp().UpdateDarkUI(static_cast<wxButton*>(this->FindWindowById(wxID_CLOSE, this)));
    this->SetEscapeId(wxID_CLOSE);
    this->Bind(wxEVT_BUTTON, &ConfigSnapshotDialog::onCloseDialog, this, wxID_CLOSE);
    vsizer->Add(buttons, 0, wxEXPAND | wxRIGHT | wxBOTTOM, 3);
}

void ConfigSnapshotDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    wxFont font = get_default_font(this);
    const int fs = font.GetPointSize();
    const int fs1 = static_cast<int>(0.8f*fs);
    const int fs2 = static_cast<int>(1.1f*fs);
    int font_size[] = { fs1, fs1, fs1, fs1, fs2, fs2, fs2 };

    html->SetFonts(font.GetFaceName(), font.GetFaceName(), font_size);
    html->Refresh();

    const int& em = em_unit();

    msw_buttons_rescale(this, em, { wxID_CLOSE});

    const wxSize& size = wxSize(45 * em, 40 * em);
    SetMinSize(size);
    Fit();

    Refresh();
}

void ConfigSnapshotDialog::onLinkClicked(wxHtmlLinkEvent &event)
{
    m_snapshot_to_activate = event.GetLinkInfo().GetHref().ToUTF8();
    this->EndModal(wxID_CLOSE);
}

void ConfigSnapshotDialog::onCloseDialog(wxEvent &)
{
    this->EndModal(wxID_CLOSE);
}

} // namespace GUI
} // namespace Slic3r
