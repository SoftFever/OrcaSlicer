#include "PlateSettingsDialog.hpp"


namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_SET_BED_TYPE_CONFIRM, wxCommandEvent);

PlateSettingsDialog::PlateSettingsDialog(wxWindow* parent, const wxString& title, bool only_first_layer_seq, const wxPoint& pos, const wxSize& size, long style)
:DPIDialog(parent, wxID_ANY, title, pos, size, style)
{
    std::string icon_path = (boost::format("%1%/images/OrcaSlicerTitle.ico") % resources_dir()).str();
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

    auto plate_name_txt = new wxStaticText(this, wxID_ANY, _L("Plate name"));
    plate_name_txt->SetFont(Label::Body_14);
    m_ti_plate_name = new TextInput(this, wxString::FromDouble(0.0), "", "", wxDefaultPosition, wxSize(FromDIP(240),-1), wxTE_PROCESS_ENTER);
    top_sizer->Add(plate_name_txt, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT |wxALL, FromDIP(5));
    top_sizer->Add(m_ti_plate_name, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT |wxALL, FromDIP(5));

    m_bed_type_choice = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(240), -1), 0,
                                     NULL, wxCB_READONLY);
    for (BedType i = btDefault; i < btCount; i = BedType(int(i) + 1)) {
      m_bed_type_choice->Append(to_bed_type_name(i));
    }

    if (!wxGetApp().preset_bundle->is_bbl_vendor())
      m_bed_type_choice->Disable();

    wxStaticText* m_bed_type_txt = new wxStaticText(this, wxID_ANY, _L("Bed type"));
    m_bed_type_txt->SetFont(Label::Body_14);
    top_sizer->Add(m_bed_type_txt, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxALL, FromDIP(5));
    top_sizer->Add(m_bed_type_choice, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT |wxALL, FromDIP(5));

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

    m_first_layer_print_seq_choice = new ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(240), -1), 0, NULL, wxCB_READONLY);
    m_first_layer_print_seq_choice->Append(_L("Auto"));
    m_first_layer_print_seq_choice->Append(_L("Customize"));
    m_first_layer_print_seq_choice->SetSelection(0);
    m_first_layer_print_seq_choice->Bind(wxEVT_COMBOBOX, [this](auto& e) {
        if (e.GetSelection() == 0) {
            m_drag_canvas->Hide();
        }
        else if (e.GetSelection() == 1) {
            m_drag_canvas->Show();
        }
        Layout();
        Fit();
        });
    wxStaticText* first_layer_txt = new wxStaticText(this, wxID_ANY, _L("First layer filament sequence"));
    first_layer_txt->SetFont(Label::Body_14);
    top_sizer->Add(first_layer_txt, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxALL, FromDIP(5));
    top_sizer->Add(m_first_layer_print_seq_choice, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, FromDIP(5));

    const std::vector<std::string> extruder_colours = wxGetApp().plater()->get_extruder_colors_from_plater_config();
    std::vector<int> order;
    if (order.empty()) {
        for (int i = 1; i <= extruder_colours.size(); i++) {
            order.push_back(i);
        }
    }
    m_drag_canvas = new DragCanvas(this, extruder_colours, order);
    m_drag_canvas->Hide();
    top_sizer->Add(0, 0, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxALL, FromDIP(5));
    top_sizer->Add(m_drag_canvas, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxALL, FromDIP(5));

    m_sizer_main->Add(top_sizer, 0, wxEXPAND | wxALL, FromDIP(30));

    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
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

    if (only_first_layer_seq) {
        for (auto item : top_sizer->GetChildren()) {
            if (item->GetWindow())
                item->GetWindow()->Show(false);
        }
        first_layer_txt->Show();
        m_first_layer_print_seq_choice->Show();
        m_drag_canvas->Show();
        Layout();
        Fit();
    }
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

void PlateSettingsDialog::sync_first_layer_print_seq(int selection, const std::vector<int>& seq)
{
    if (m_first_layer_print_seq_choice != nullptr) {
        if (selection == 1) {
            const std::vector<std::string> extruder_colours = wxGetApp().plater()->get_extruder_colors_from_plater_config();
            m_drag_canvas->set_shape_list(extruder_colours, seq);
        }
        m_first_layer_print_seq_choice->SetSelection(selection);

        wxCommandEvent event(wxEVT_COMBOBOX);
        event.SetInt(selection);
        event.SetEventObject(m_first_layer_print_seq_choice);
        wxPostEvent(m_first_layer_print_seq_choice, event);
    }
}

void PlateSettingsDialog::sync_spiral_mode(bool spiral_mode, bool as_global)
{
    if (m_spiral_mode_choice) {
        if (as_global) {
            m_spiral_mode_choice->SetSelection(0);
        }
        else {
            if (spiral_mode)
                m_spiral_mode_choice->SetSelection(1);
            else
                m_spiral_mode_choice->SetSelection(2);
        }
    }
}

wxString PlateSettingsDialog::to_bed_type_name(BedType bed_type) {
    switch (bed_type) {
    case btDefault:
        return _L("Same as Global Plate Type");
    default: {
        const ConfigOptionDef *bed_type_def = print_config_def.get("curr_bed_type");
        return _(bed_type_def->enum_labels[size_t(bed_type) - 1]);
        }
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

std::vector<int> PlateSettingsDialog::get_first_layer_print_seq()
{
    return m_drag_canvas->get_shape_list_order();
}


//PlateNameEditDialog
PlateNameEditDialog::PlateNameEditDialog(wxWindow *parent, wxWindowID id, const wxString &title, const wxPoint &pos, const wxSize &size, long style)
    : DPIDialog(parent, id, title, pos, size, style)
{
    std::string icon_path = (boost::format("%1%/images/OrcaSlicerTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top   = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(400), -1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));

    wxFlexGridSizer *top_sizer = new wxFlexGridSizer(0, 2, FromDIP(5), 0);
    top_sizer->AddGrowableCol(0, 1);
    top_sizer->SetFlexibleDirection(wxBOTH);
    top_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    auto plate_name_txt = new wxStaticText(this, wxID_ANY, _L("Plate name"));
    plate_name_txt->SetFont(Label::Body_14);
    m_ti_plate_name = new TextInput(this, wxString::FromDouble(0.0), "", "", wxDefaultPosition, wxSize(FromDIP(240), -1), wxTE_PROCESS_ENTER);
    m_ti_plate_name->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &e) {
        if (this->IsModal())
            EndModal(wxID_YES);
        else
            this->Close();
    });
    top_sizer->Add(plate_name_txt, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_LEFT | wxALL, FromDIP(5));
    top_sizer->Add(m_ti_plate_name, 0, wxALIGN_CENTER_VERTICAL | wxALIGN_RIGHT | wxALL, FromDIP(5));
    m_ti_plate_name->GetTextCtrl()->SetMaxLength(250);

    m_sizer_main->Add(top_sizer, 0, wxEXPAND | wxALL, FromDIP(30));

    auto       sizer_button = new wxBoxSizer(wxHORIZONTAL);
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
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
    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
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
    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &e) {
        if (this->IsModal())
            EndModal(wxID_NO);
        else
            this->Close();
    });

    sizer_button->AddStretchSpacer();
    sizer_button->Add(m_button_ok, 0, wxALL, FromDIP(5));
    sizer_button->Add(m_button_cancel, 0, wxALL, FromDIP(5));
    sizer_button->Add(FromDIP(30), 0, 0, 0);

    m_sizer_main->Add(sizer_button, 0, wxEXPAND, FromDIP(20));

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    CenterOnParent();

    wxGetApp().UpdateDlgDarkUI(this);
}

PlateNameEditDialog::~PlateNameEditDialog() {}

void PlateNameEditDialog::on_dpi_changed(const wxRect &suggested_rect)
{
    m_button_ok->Rescale();
    m_button_cancel->Rescale();
}


wxString PlateNameEditDialog::get_plate_name() const { return m_ti_plate_name->GetTextCtrl()->GetValue(); }

void PlateNameEditDialog::set_plate_name(const wxString &name) {
    m_ti_plate_name->GetTextCtrl()->SetValue(name);
    m_ti_plate_name->GetTextCtrl()->SetFocus();
    m_ti_plate_name->GetTextCtrl()->SetInsertionPointEnd();
}

}} // namespace Slic3r::GUI