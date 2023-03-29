#include "PlateSettingsDialog.hpp"


namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_SET_BED_TYPE_CONFIRM, wxCommandEvent);

PlateSettingsDialog::PlateSettingsDialog(wxWindow* parent, wxWindowID id, const wxString& title, const wxPoint& pos, const wxSize& size, long style)
:DPIDialog(parent, id, title, pos, size, style)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    wxBoxSizer* m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(400), -1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));

    wxFlexGridSizer* top_sizer = new wxFlexGridSizer(0, 2, FromDIP(5), 0);
    top_sizer->AddGrowableCol(0,1);
    top_sizer->SetFlexibleDirection(wxBOTH);
    top_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    bool is_bbl = wxGetApp().preset_bundle->printers.get_edited_preset().is_bbl_vendor_preset(wxGetApp().preset_bundle);
    if (is_bbl) {
      m_bed_type_choice = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(240), -1), 0,
                                       NULL, wxCB_READONLY);
      for (BedType i = btDefault; i < btCount; i = BedType(int(i) + 1)) {
        m_bed_type_choice->Append(to_bed_type_name(i));
      }
      wxStaticText *m_bed_type_txt = new wxStaticText(this, wxID_ANY, _L("Bed type"));
      m_bed_type_txt->SetFont(Label::Body_14);
      top_sizer->Add(m_bed_type_txt, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxALL, FromDIP(5));
      top_sizer->Add(m_bed_type_choice, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, FromDIP(5));
    }


    wxBoxSizer* m_sizer_selectbox = new wxBoxSizer(wxHORIZONTAL);
    m_print_seq_choice = new ComboBox( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(240),-1), 0, NULL, wxCB_READONLY );
    m_print_seq_choice->Append(_L("Same as Global Print Sequence"));
    for (auto i = PrintSequence::ByLayer; i < PrintSequence::ByDefault; i = PrintSequence(int(i) + 1)) {
        m_print_seq_choice->Append(to_print_sequence_name(i));
    }
    wxStaticText* m_print_seq_txt = new wxStaticText(this, wxID_ANY, _L("Print sequence"));
    m_print_seq_txt->SetFont(Label::Body_14);
    top_sizer->Add(m_print_seq_txt, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT |wxALL, FromDIP(5));
    top_sizer->Add(m_print_seq_choice, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT |wxALL, FromDIP(5));

    auto plate_name_txt = new wxStaticText(this, wxID_ANY, _L("Plate name"));
    plate_name_txt->SetFont(Label::Body_14);
    m_ti_plate_name = new TextInput(this, wxString::FromDouble(0.0), "", "", wxDefaultPosition, wxSize(FromDIP(240),-1), wxTE_PROCESS_ENTER);
    top_sizer->Add(plate_name_txt, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT |wxALL, FromDIP(5));
    top_sizer->Add(m_ti_plate_name, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT |wxALL, FromDIP(5));

    m_sizer_main->Add(top_sizer, 0, wxEXPAND | wxALL, FromDIP(30));

    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 150, 136), StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_button_ok = new Button(this, _L("OK"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour("#FFFFFE"));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_ok->SetCornerRadius(FromDIP(12));
    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxCommandEvent evt(EVT_SET_BED_TYPE_CONFIRM, GetId());
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        if (this->IsModal())
            EndModal(wxID_YES);
        else
            this->Close();
        });

    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_button_cancel->SetCornerRadius(FromDIP(12));
    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        if (this->IsModal())
            EndModal(wxID_NO);
        else
            this->Close();
        });

    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));
    sizer_button->Add(FromDIP(30),0, 0, 0);

    m_sizer_main->Add(sizer_button, 0, wxEXPAND, FromDIP(20));

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    CenterOnParent();

    wxGetApp().UpdateDlgDarkUI(this);
}

PlateSettingsDialog::~PlateSettingsDialog()
{

}

void PlateSettingsDialog::sync_bed_type(BedType type)
{
    if (m_bed_type_choice != nullptr) {
        m_bed_type_choice->SetSelection(int(type));
    }
}

void PlateSettingsDialog::sync_print_seq(int print_seq)
{
    if (m_print_seq_choice != nullptr) {
        m_print_seq_choice->SetSelection(print_seq);
    }
}

wxString PlateSettingsDialog::to_bed_type_name(BedType bed_type) {
    switch (bed_type) {
    case btDefault:
        return _L("Same as Global Bed Type");
    case btPC:
        return _L("Cool Plate");
    case btEP:
        return _L("Engineering Plate");
    case btPEI:
        return _L("High Temp Plate");
    case btPTE:
        return _L("Textured PEI Plate");
    default:
        return _L("Same as Global Bed Type");
    }
    return _L("Same as Global Bed Type");
}

wxString PlateSettingsDialog::to_print_sequence_name(PrintSequence print_seq) {
    switch (print_seq) {
    case PrintSequence::ByLayer:
        return _L("By Layer");
    case PrintSequence::ByObject:
        return _L("By Object");
    default:
        return _L("By Layer");
    }
    return _L("By Layer");
}

void PlateSettingsDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    m_button_ok->Rescale();
    m_button_cancel->Rescale();
}

wxString PlateSettingsDialog::get_plate_name() const {
    return m_ti_plate_name->GetTextCtrl()->GetValue(); 
}

void PlateSettingsDialog::set_plate_name(const wxString &name) { m_ti_plate_name->GetTextCtrl()->SetValue(name); }

}} // namespace Slic3r::GUI