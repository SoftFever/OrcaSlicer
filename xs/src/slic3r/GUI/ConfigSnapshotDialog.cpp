#include "ConfigSnapshotDialog.hpp"

#include "../Config/Snapshot.hpp"
#include "../Utils/Time.hpp"

#include "../../libslic3r/Utils.hpp"

namespace Slic3r { 
namespace GUI {

static std::string format_reason(const Config::Snapshot::Reason reason) 
{
    switch (reason) {
    case Config::Snapshot::SNAPSHOT_UPGRADE:
        return std::string(_(L("Upgrade")));
    case Config::Snapshot::SNAPSHOT_DOWNGRADE:
        return std::string(_(L("Downgrade")));
    case Config::Snapshot::SNAPSHOT_BEFORE_ROLLBACK:
        return std::string(_(L("Before roll back")));
    case Config::Snapshot::SNAPSHOT_USER:
        return std::string(_(L("User")));
    case Config::Snapshot::SNAPSHOT_UNKNOWN:
    default:
        return std::string(_(L("Unknown")));
    }
}

static std::string generate_html_row(const Config::Snapshot &snapshot, bool row_even, bool snapshot_active)
{
    // Start by declaring a row with an alternating background color.
    std::string text = "<tr bgcolor=\"";
    text += snapshot_active ? "#B3FFCB" : (row_even ? "#FFFFFF" : "#D5D5D5");
    text += "\">";
    text += "<td>";
    // Format the row header.
    text += std::string("<font size=\"5\"><b>") + (snapshot_active ? _(L("Active: ")) : "") + 
        Utils::format_local_date_time(snapshot.time_captured) + ": " + format_reason(snapshot.reason);
    if (! snapshot.comment.empty())
        text += " (" + snapshot.comment + ")";
    text += "</b></font><br>";
    // End of row header.
//    text += _(L("ID:")) + " " + snapshot.id + "<br>";
    // text += _(L("time captured:")) + " " + Utils::format_local_date_time(snapshot.time_captured) + "<br>";
    text += _(L("slic3r version")) + ": " + snapshot.slic3r_version_captured.to_string() + "<br>";
//    text += "reason: " + snapshot.reason + "<br>";
    text += _(L("print")) + ": " + snapshot.print + "<br>";
    text += _(L("filaments")) + ": " + snapshot.filaments.front() + "<br>";
    text += _(L("printer")) + ": " + snapshot.printer + "<br>";

    for (const Config::Snapshot::VendorConfig &vc : snapshot.vendor_configs) {
        text += _(L("vendor")) + ": " + vc.name + ", ver: " + vc.version.to_string() + ", min slic3r ver: " + vc.min_slic3r_version.to_string();
        if (vc.max_slic3r_version != Semver::inf())
            text += ", max slic3r ver: " + vc.max_slic3r_version.to_string();
        text += "<br>";
        for (const std::pair<std::string, std::set<std::string>> &model : vc.models_variants_installed) {
            text += _(L("model")) + ": " + model.first + ", " + _(L("variants")) + ": ";
            for (const std::string &variant : model.second) {
                if (&variant != &*model.second.begin())
                    text += ", ";
                text += variant;
            }
            text += "<br>";
        }
    }

    if (! snapshot_active)
        text += "<p align=\"right\"><a href=\"" + snapshot.id + "\">" + _(L("Activate")) + "</a></p>";
    text += "</td>";
	text += "</tr>";
    return text;
}

static std::string generate_html_page(const Config::SnapshotDB &snapshot_db, const std::string &on_snapshot)
{
    std::string text = 
        "<html>"
        "<body bgcolor=\"#ffffff\" cellspacing=\"2\" cellpadding=\"0\" border=\"0\" link=\"#800000\">"
        "<font color=\"#000000\">";
    text += "<table style=\"width:100%\">";
    for (size_t i_row = 0; i_row < snapshot_db.snapshots().size(); ++ i_row) {
        const Config::Snapshot &snapshot = snapshot_db.snapshots()[snapshot_db.snapshots().size() - i_row - 1];
        text += generate_html_row(snapshot, i_row & 1, snapshot.id == on_snapshot);
    }
    text +=
        "</table>"
        "</font>"
        "</body>"
        "</html>";
    return text;
}

ConfigSnapshotDialog::ConfigSnapshotDialog(const Config::SnapshotDB &snapshot_db, const std::string &on_snapshot)
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
            int size[] = {8,8,8,8,11,11,11};
        #else
            int size[] = {11,11,11,11,14,14,14};
        #endif
        html->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
        html->SetBorders(2);
        std::string text = generate_html_page(snapshot_db, on_snapshot);
        html->SetPage(text.c_str());
        vsizer->Add(html, 1, wxEXPAND | wxALIGN_LEFT | wxRIGHT | wxBOTTOM, 0);
        html->Bind(wxEVT_HTML_LINK_CLICKED, &ConfigSnapshotDialog::onLinkClicked, this);
    }
    
    wxStdDialogButtonSizer* buttons = this->CreateStdDialogButtonSizer(wxCLOSE);
    this->SetEscapeId(wxID_CLOSE);
    this->Bind(wxEVT_BUTTON, &ConfigSnapshotDialog::onCloseDialog, this, wxID_CLOSE);
    vsizer->Add(buttons, 0, wxEXPAND | wxRIGHT | wxBOTTOM, 3);
}

void ConfigSnapshotDialog::onLinkClicked(wxHtmlLinkEvent &event)
{
    m_snapshot_to_activate = event.GetLinkInfo().GetHref();
    this->EndModal(wxID_CLOSE);
    this->Close();
}

void ConfigSnapshotDialog::onCloseDialog(wxEvent &)
{
    this->EndModal(wxID_CLOSE);
    this->Close();
}

} // namespace GUI
} // namespace Slic3r
