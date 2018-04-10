#include "ConfigSnapshotDialog.hpp"

#include "../Config/Snapshot.hpp"
#include "../Utils/Time.hpp"

#include "../../libslic3r/Utils.hpp"

namespace Slic3r { 
namespace GUI {

static std::string generate_html_row(const Config::Snapshot &snapshot, bool row_even)
{
    // Start by declaring a row with an alternating background color.
    std::string text = "<tr bgcolor=\"";
    text += row_even ? "#FFFFFF" : "#C0C0C0";
    text += "\">";
    text += "<td>";
//    text += _(L("ID:")) + " " + snapshot.id + "<br>";
    text += _(L("time captured:")) + " " + Utils::format_local_date_time(snapshot.time_captured) + "<br>";
    text += _(L("slic3r version:")) + " " + snapshot.slic3r_version_captured.to_string() + "<br>";
    if (! snapshot.comment.empty())
        text += _(L("user comment:")) + " " + snapshot.comment + "<br>";
//    text += "reason: " + snapshot.reason + "<br>";
    text += "print: " + snapshot.print + "<br>";
    text += "filaments: " + snapshot.filaments.front() + "<br>";
    text += "printer: " + snapshot.printer + "<br>";

    for (const Config::Snapshot::VendorConfig &vc : snapshot.vendor_configs) {
        text += "vendor: " + vc.name + ", ver: " + vc.version.to_string() + ", min slic3r ver: " + vc.min_slic3r_version.to_string() + ", max slic3r ver: " + vc.max_slic3r_version.to_string() + "<br>";
    }

    text += "<p align=\"right\"><a href=\"" + snapshot.id + "\">Activate</a></p>";
    text += "</td>";
	text += "</tr>";
    return text;
}

static std::string generate_html_page(const Config::SnapshotDB &snapshot_db)
{
    std::string text = 
        "<html>"
        "<body bgcolor=\"#ffffff\" cellspacing=\"2\" cellpadding=\"0\" border=\"0\" link=\"#800000\">"
        "<font color=\"#000000\">";
    text += "<table style=\"width:100%\">";
    for (size_t i_row = 0; i_row < snapshot_db.snapshots().size(); ++ i_row) {
        const Config::Snapshot &snapshot = snapshot_db.snapshots()[i_row];
        text += generate_html_row(snapshot, i_row & 1);
    }
    text +=
        "</table>"
        "</font>"
        "</body>"
        "</html>";
    return text;
}

ConfigSnapshotDialog::ConfigSnapshotDialog(const Config::SnapshotDB &snapshot_db)
    : wxDialog(NULL, wxID_ANY, _(L("Configuration Snapshots")), wxDefaultPosition, wxSize(600, 500), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX)
{
    this->SetBackgroundColour(*wxWHITE);
    
    wxBoxSizer* vsizer = new wxBoxSizer(wxVERTICAL);
    this->SetSizer(vsizer);

    // text
    wxHtmlWindow* html = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHW_SCROLLBAR_AUTO);
    {
        wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
        #ifdef __WXMSW__
            int size[] = {8,8,8,8,8,8,8};
        #else
            int size[] = {11,11,11,11,11,11,11};
        #endif
        html->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
        html->SetBorders(2);
        std::string text = generate_html_page(snapshot_db);
        FILE *file = ::fopen("d:\\temp\\configsnapshotdialog.html", "wt");
        fwrite(text.data(), 1, text.size(), file);
        fclose(file);
        html->SetPage(text.c_str());
        vsizer->Add(html, 1, wxEXPAND | wxALIGN_LEFT | wxRIGHT | wxBOTTOM, 0);
        html->Bind(wxEVT_HTML_LINK_CLICKED, &ConfigSnapshotDialog::onLinkClicked, this);
    }
    
    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxCLOSE);
    this->SetEscapeId(wxID_CLOSE);
    this->Bind(wxEVT_BUTTON, &ConfigSnapshotDialog::onCloseDialog, this, wxID_CLOSE);
    vsizer->Add(buttons, 0, wxEXPAND | wxRIGHT | wxBOTTOM, 3);

/*    
    this->Bind(wxEVT_LEFT_DOWN, &ConfigSnapshotDialog::onCloseDialog, this);
    logo->Bind(wxEVT_LEFT_DOWN, &ConfigSnapshotDialog::onCloseDialog, this);
    html->Bind(wxEVT_LEFT_DOWN, &ConfigSnapshotDialog::onCloseDialog, this);
*/
}

void ConfigSnapshotDialog::onLinkClicked(wxHtmlLinkEvent &event)
{
    wxLaunchDefaultBrowser(event.GetLinkInfo().GetHref());
    event.Skip(false);
}

void ConfigSnapshotDialog::onCloseDialog(wxEvent &)
{
    this->EndModal(wxID_CLOSE);
    this->Close();
}

} // namespace GUI
} // namespace Slic3r
