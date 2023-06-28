#include "EditCalibrationHistoryDialog.hpp"
#include "MsgDialog.hpp"
#include "GUI_App.hpp"

namespace Slic3r { namespace GUI {

#define EDIT_HISTORY_DIALOG_INPUT_SIZE     wxSize(FromDIP(160), FromDIP(24))

static bool validate_input_k_value(wxString k_text, float* output_value)
{
    float default_k = 0.0f;
    if (k_text.IsEmpty()) {
        *output_value = default_k;
        MessageDialog msg_dlg(nullptr, _L("Please input a valid value (K in 0~0.5)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    double k_value = 0.0;
    try {
        k_text.ToDouble(&k_value);
    }
    catch (...) {
        ;
    }

    if (k_value < 0 || k_value > 0.5) {
        *output_value = default_k;
        MessageDialog msg_dlg(nullptr, _L("Please input a valid value (K in 0~0.5)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    *output_value = k_value;
    return true;
};

static bool validate_input_n_value(wxString n_text, float* output_value) {
    float default_n = 1.0f;
    if (n_text.IsEmpty()) {
        *output_value = default_n;
        MessageDialog msg_dlg(nullptr, _L("Please input a valid value (N in 0.6~2.0)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    double n_value = 0.0;
    try {
        n_text.ToDouble(&n_value);
    }
    catch (...) {
        ;
    }

    if (n_value < 0.6 || n_value > 2.0) {
        *output_value = default_n;
        MessageDialog msg_dlg(nullptr, _L("Please input a valid value (N in 0.6~2.0)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    *output_value = n_value;
    return true;
}

EditCalibrationHistoryDialog::EditCalibrationHistoryDialog(wxWindow* parent, wxString k, wxString n, wxString material_name, wxString nozzle_dia)
    : DPIDialog(parent, wxID_ANY, _L("Edit Pressure Advance"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    create(k, n, material_name, nozzle_dia);
    wxGetApp().UpdateDlgDarkUI(this);
}

EditCalibrationHistoryDialog::~EditCalibrationHistoryDialog() {
}

void EditCalibrationHistoryDialog::create(const wxString& k, const wxString& n, const wxString& material_name, const wxString& nozzle_dia)
{
    this->SetBackgroundColour(*wxWHITE);
    auto main_sizer = new wxBoxSizer(wxVERTICAL);

    auto top_panel = new wxPanel(this);
    auto panel_sizer = new wxBoxSizer(wxVERTICAL);
    top_panel->SetSizer(panel_sizer);

    auto flex_sizer = new wxFlexGridSizer(0, 2, FromDIP(15), FromDIP(30));
    flex_sizer->SetFlexibleDirection(wxBOTH);
    flex_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    wxStaticText* nozzle_title = new wxStaticText(top_panel, wxID_ANY, _L("Nozzle Diameter"));
    wxStaticText* nozzle_value = new wxStaticText(top_panel, wxID_ANY, nozzle_dia);
    flex_sizer->Add(nozzle_title);
    flex_sizer->Add(nozzle_value);

    wxStaticText* material_name_title = new wxStaticText(top_panel, wxID_ANY, _L("Material"));
    TextInput* material_name_value = new TextInput(top_panel, material_name, "", "", wxDefaultPosition, EDIT_HISTORY_DIALOG_INPUT_SIZE, wxTE_PROCESS_ENTER);
    m_material_name = material_name.ToStdString();
    material_name_value->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this, material_name_value](auto& e) {
        if (!material_name_value->GetTextCtrl()->GetValue().IsEmpty())
            m_material_name = material_name_value->GetTextCtrl()->GetValue().ToStdString();
        });
    material_name_value->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this, material_name_value](auto& e) {
        if (!material_name_value->GetTextCtrl()->GetValue().IsEmpty())
            m_material_name = material_name_value->GetTextCtrl()->GetValue().ToStdString();
        e.Skip();
        });
    flex_sizer->Add(material_name_title);
    flex_sizer->Add(material_name_value);

    wxStaticText* k_title = new wxStaticText(top_panel, wxID_ANY, _L("K Factor"));
    TextInput* k_value = new TextInput(top_panel, k, "", "", wxDefaultPosition, EDIT_HISTORY_DIALOG_INPUT_SIZE, wxTE_PROCESS_ENTER);
    double double_k = 0.0;
    k_value->GetTextCtrl()->GetValue().ToDouble(&double_k);
    m_k_value = double_k;
    k_value->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this, k_value](auto& e) {
        float k = 0.0f;
        validate_input_k_value(k_value->GetTextCtrl()->GetValue(), &k);
        wxString k_str = wxString::Format("%.3f", k);
        k_value->GetTextCtrl()->SetValue(k_str);
        m_k_value = k;
        });
    k_value->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this, k_value](auto& e) {
        float k = 0.0f;
        validate_input_k_value(k_value->GetTextCtrl()->GetValue(), &k);
        wxString k_str = wxString::Format("%.3f", k);
        k_value->GetTextCtrl()->SetValue(k_str);
        m_k_value = k;
        e.Skip();
        });
    flex_sizer->Add(k_title);
    flex_sizer->Add(k_value);

    // Hide:
    //wxStaticText* n_title = new wxStaticText(top_panel, wxID_ANY, _L("N Factor"));
    //TextInput* n_value = new TextInput(top_panel, n, "", "", wxDefaultPosition, EDIT_HISTORY_DIALOG_INPUT_SIZE, wxTE_PROCESS_ENTER);
    //flex_sizer->Add(n_title);
    //flex_sizer->Add(n_value);
    
    panel_sizer->Add(flex_sizer);

    panel_sizer->AddSpacer(FromDIP(25));

    auto btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    Button* save_btn = new Button(top_panel, _L("Save"));
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    save_btn->SetBackgroundColour(*wxWHITE);
    save_btn->SetBackgroundColor(btn_bg_green);
    save_btn->SetBorderColor(wxColour(0, 174, 66));
    save_btn->SetTextColor(wxColour("#FFFFFE"));
    save_btn->SetMinSize(wxSize(-1, FromDIP(24)));
    save_btn->SetCornerRadius(FromDIP(12));
    Button* cancel_btn = new Button(top_panel, _L("Cancel"));
    cancel_btn->SetBackgroundColour(*wxWHITE);
    cancel_btn->SetMinSize(wxSize(-1, FromDIP(24)));
    cancel_btn->SetCornerRadius(FromDIP(12));
    save_btn->Bind(wxEVT_BUTTON, &EditCalibrationHistoryDialog::on_save, this);
    cancel_btn->Bind(wxEVT_BUTTON, &EditCalibrationHistoryDialog::on_cancel, this);
    btn_sizer->AddStretchSpacer();
    btn_sizer->Add(save_btn);
    btn_sizer->AddSpacer(FromDIP(20));
    btn_sizer->Add(cancel_btn);
    panel_sizer->Add(btn_sizer, 0, wxEXPAND, 0);


    main_sizer->Add(top_panel, 1, wxEXPAND | wxALL, FromDIP(20));

    SetSizer(main_sizer);
    Layout();
    Fit();
    CenterOnParent();
}

float EditCalibrationHistoryDialog::get_k_value(){
    return m_k_value;
}

float EditCalibrationHistoryDialog::get_n_value(){
    return m_n_value;
}

wxString EditCalibrationHistoryDialog::get_material_name_value() {
    return m_material_name;
}

void EditCalibrationHistoryDialog::on_save(wxCommandEvent& event) {
    EndModal(wxID_OK);
}

void EditCalibrationHistoryDialog::on_cancel(wxCommandEvent& event) {
    EndModal(wxID_CANCEL);
}

void EditCalibrationHistoryDialog::on_dpi_changed(const wxRect& suggested_rect) 
{
}

}} // namespace Slic3r::GUI
