#include "LegacyPrinterMigrationDialog.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "GUI_Utils.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "format.hpp"
#include "libslic3r/Preset.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/DialogButtons.hpp"
#include "Widgets/Label.hpp"

#include <wx/event.h>
#include <wx/gdicmn.h>
#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/textctrl.h>
#include <wx/toplevel.h>

#include <string>
#include <vector>

namespace Slic3r { namespace GUI {

LegacyPrinterMigrationDialog::LegacyPrinterMigrationDialog(wxWindow *parent, const std::vector<LegacyPrinterMigration> &migrations,
                                                           bool offer_dont_remind)
    : DPIDialog(parent ? parent : static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, _L("Migrate Filament Presets"),
                wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    SetBackgroundColour(*wxWHITE);
    SetFont(Label::Body_14);

    auto *sizer = new wxBoxSizer(wxVERTICAL);

    auto *message = new Label(this,
        _L("Since OrcaSlicer 2.5.0, filament presets have been restructured to support more features. Some of your "
           "presets need to be migrated to the new structure, or they will no longer be available for some of your "
           "printers.\n\n"
           "Would you like to migrate them now? Your presets will be backed up first, and this only needs to be done "
           "once. You can also do it later from Help > Migrate Filament Presets."),
        0);
    message->Wrap(FromDIP(520));
    sizer->Add(message, 0, wxEXPAND | wxALL, FromDIP(15));

    sizer->Add(new Label(this, _L("Affected presets:")), 0, wxLEFT | wxRIGHT, FromDIP(15));
    sizer->AddSpacer(FromDIP(5));

    wxString details;
    for (const LegacyPrinterMigration &migration : migrations) {
        details += format_wxstr(_L("%1% (based on %2%)"), migration.preset, migration.parent) + "\n";
        for (const std::string &printer : migration.printers)
            details += "    " + from_u8(printer) + "\n";
    }
    auto *list = new wxTextCtrl(this, wxID_ANY, details, wxDefaultPosition, wxSize(-1, FromDIP(220)),
                                wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
    sizer->Add(list, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(15));

    if (offer_dont_remind) {
        auto *dont_remind_sizer = new wxBoxSizer(wxHORIZONTAL);
        m_dont_remind           = new ::CheckBox(this);
        auto *dont_remind_label = new Label(this, _L("Don't remind me again"));
        dont_remind_label->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
            m_dont_remind->SetValue(!m_dont_remind->GetValue());
            e.Skip();
        });
        dont_remind_sizer->Add(m_dont_remind, 0, wxALIGN_CENTER_VERTICAL);
        dont_remind_sizer->Add(dont_remind_label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
        sizer->Add(dont_remind_sizer, 0, wxLEFT | wxRIGHT | wxTOP, FromDIP(15));
    }

    auto *buttons = new DialogButtons(this, {"Yes", "No"}, "Yes");
    buttons->GetYES()->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_YES); });
    buttons->GetNO()->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_NO); });
    sizer->Add(buttons, 0, wxEXPAND);

    SetSizerAndFit(sizer);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

bool LegacyPrinterMigrationDialog::dont_remind() const
{
    return m_dont_remind != nullptr && m_dont_remind->GetValue();
}

void LegacyPrinterMigrationDialog::on_dpi_changed(const wxRect &)
{
    if (m_dont_remind != nullptr)
        m_dont_remind->Rescale();
    GetSizer()->SetSizeHints(this);
    Refresh();
}

}} // namespace Slic3r::GUI
