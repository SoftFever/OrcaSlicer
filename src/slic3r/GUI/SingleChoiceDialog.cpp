#include "SingleChoiceDialog.hpp"

#include "GUI_App.hpp"
#include "MainFrame.hpp"

namespace Slic3r { namespace GUI {

SingleChoiceDialog::SingleChoiceDialog(const wxString &message, const wxString &caption, const wxArrayString &choices, int initialSelection, wxWindow *parent)
    : DPIDialog(parent ? parent : static_cast<wxWindow *>(wxGetApp().mainframe), wxID_ANY, caption, wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    SetBackgroundColour(*wxWHITE);

    const int   dlg_width = 200;
    wxBoxSizer *bSizer    = new wxBoxSizer(wxVERTICAL);
    bSizer->SetMinSize(wxSize(FromDIP(dlg_width), -1));

    wxStaticText *message_text = new wxStaticText(this, wxID_ANY, message, wxDefaultPosition, wxDefaultSize, 0);
    message_text->Wrap(-1);
    bSizer->Add(message_text, 0, wxALL, FromDIP(5));

    type_comboBox = new ComboBox(this, wxID_ANY, choices[0], wxDefaultPosition, wxSize(FromDIP(dlg_width - 10), -1), 0, NULL, wxCB_READONLY);
    for (const wxString &type_name : choices) { type_comboBox->Append(type_name); }
    bSizer->Add(type_comboBox, 0, wxALL | wxALIGN_CENTER, FromDIP(5));
    bSizer->AddSpacer(FromDIP(5)); // ORCA add less vertical space after combo box
    type_comboBox->SetSelection(initialSelection);

    wxBoxSizer *bSizer_button = new wxBoxSizer(wxHORIZONTAL);

	bSizer_button->Add(0, 0, wxEXPAND, 0); // ORCA align buttons to right
    m_button_ok = new Button(this, _L("OK"));
    m_button_ok->SetStyleConfirm(Label::Body_12); // ORCA match style
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    bSizer_button->Add(m_button_ok, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(10)); // ORCA align buttons to right

    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { EndModal(wxID_OK); });

    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetStyleDefault(Label::Body_12); // ORCA match style
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    bSizer_button->Add(m_button_cancel, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(5)); // ORCA align buttons to right

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) { EndModal(wxID_CANCEL); });

    bSizer->Add(bSizer_button, 1, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM | wxEXPAND, FromDIP(5));

    this->SetSizer(bSizer);
    this->Layout();
    bSizer->Fit(this);
    wxGetApp().UpdateDlgDarkUI(this);
}
SingleChoiceDialog::~SingleChoiceDialog() {}
int SingleChoiceDialog::GetSingleChoiceIndex() { return this->ShowModal() == wxID_OK ? GetTypeComboBox()->GetSelection() : -1; }

void SingleChoiceDialog::on_dpi_changed(const wxRect &suggested_rect) {}
}} // namespace Slic3r::GUI