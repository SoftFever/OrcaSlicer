#include "CalibrationWizard.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "../../libslic3r/Calib.hpp"

#define CALIBRATION_COMBOX_SIZE            wxSize(FromDIP(500), FromDIP(24))
#define CALIBRATION_AMS_COMBOX_SIZE        wxSize(FromDIP(250), FromDIP(24))
#define CALIBRATION_OPTIMAL_INPUT_SIZE     wxSize(FromDIP(300), FromDIP(24))
#define CALIBRATION_FROM_TO_INPUT_SIZE     wxSize(FromDIP(160), FromDIP(24))
#define CALIBRATION_FGSIZER_HGAP           FromDIP(50)
#define CALIBRATION_TEXT_MAX_LENGTH        FromDIP(90) + CALIBRATION_FGSIZER_HGAP + 2 * CALIBRATION_AMS_COMBOX_SIZE.x
static const wxString NA_STR = _L("N/A");

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_CALIBRATION_TRAY_SELECTION_CHANGED, SimpleEvent);

FilamentComboBox::FilamentComboBox(wxWindow* parent, const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, wxID_ANY, pos, size, wxTAB_TRAVERSAL)
{
    wxBoxSizer* main_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_comboBox = new CalibrateFilamentComboBox(this);
    m_comboBox->SetSize(CALIBRATION_AMS_COMBOX_SIZE);
    m_comboBox->SetMinSize(CALIBRATION_AMS_COMBOX_SIZE);
    main_sizer->Add(m_comboBox->clr_picker, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(8));
    main_sizer->Add(m_comboBox, 0, wxALIGN_CENTER);

    this->SetSizer(main_sizer);
    this->Layout();
    main_sizer->Fit(this);
}

void FilamentComboBox::set_select_mode(FilamentSelectMode mode)
{
    m_mode = mode;
    if (m_checkBox)
        m_checkBox->Show(m_mode == FSMCheckBoxMode);
    if (m_radioBox)
        m_radioBox->Show(m_mode == FSMRadioMode);

    Layout();
}

void FilamentComboBox::load_tray_from_ams(int id, DynamicPrintConfig& tray)
{
    m_tray_id = id;
    m_comboBox->load_tray(tray);
    Enable(m_comboBox->is_tray_exist());
}

void FilamentComboBox::update_from_preset() { m_comboBox->update(); }

bool FilamentComboBox::Show(bool show)
{
    bool result = wxPanel::Show(show);
    if (m_radioBox && m_mode == FSMRadioMode)
        result = result && m_radioBox->Show(show);
    if (m_checkBox && m_mode == FSMCheckBoxMode)
        result = result && m_checkBox->Show(show);
    return result;
}

bool FilamentComboBox::Enable(bool enable) {
    bool result = wxPanel::Enable(enable);
    if (m_radioBox)
        result = result && m_radioBox->Enable(enable);
    if (m_checkBox) {
        if (!enable)
            m_checkBox->SetValue(false);
        result = result && m_checkBox->Enable(enable);
    }
    return result;
}

void FilamentComboBox::SetValue(bool value) {
    if (m_radioBox)
        m_radioBox->SetValue(value);
    if (m_checkBox)
        m_checkBox->SetValue(value);
    SimpleEvent e(EVT_CALIBRATION_TRAY_SELECTION_CHANGED);
    e.ResumePropagation(wxEVENT_PROPAGATE_MAX);
    e.SetEventObject(this);
    wxPostEvent(this, e);
}

CalibrationWizard::CalibrationWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style) 
{
    SetBackgroundColour(wxColour(0xEEEEEE));

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    m_scrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxVSCROLL);
    m_scrolledWindow->SetScrollRate(5, 5);
    m_scrolledWindow->SetBackgroundColour(*wxWHITE);

    m_all_pages_sizer = new wxBoxSizer(wxVERTICAL);

    m_scrolledWindow->SetSizer(m_all_pages_sizer);
    m_scrolledWindow->Layout();
    m_all_pages_sizer->Fit(m_scrolledWindow);

    create_presets_panel();
    init_presets_selections();

    create_send_progress_bar();

    create_print_panel();

    main_sizer->Add(m_scrolledWindow, 1, wxEXPAND | wxALL, FromDIP(10));

    this->SetSizer(main_sizer);
    this->Layout();
    main_sizer->Fit(this);

    m_comboBox_printer->Bind(wxEVT_COMBOBOX, &CalibrationWizard::on_select_printer, this);
    m_comboBox_nozzle_dia->Bind(wxEVT_COMBOBOX, &CalibrationWizard::on_select_nozzle, this);
    m_comboBox_bed_type->Bind(wxEVT_COMBOBOX, &CalibrationWizard::on_select_bed_type, this);
    m_button_pause_resume->Bind(wxEVT_BUTTON, &CalibrationWizard::on_subtask_pause_resume, this);
    m_button_abort->Bind(wxEVT_BUTTON, &CalibrationWizard::on_subtask_abort, this);
    m_ams_sync_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
        on_update_ams_filament();
        });
    m_ams_radiobox->Bind(wxEVT_RADIOBUTTON, &CalibrationWizard::on_choose_ams, this);
    m_ext_spool_radiobox->Bind(wxEVT_RADIOBUTTON, &CalibrationWizard::on_choose_ext_spool, this);
    Bind(EVT_CALIBRATIONPAGE_PREV, &CalibrationWizard::on_click_btn_prev, this);
    Bind(EVT_CALIBRATIONPAGE_NEXT, &CalibrationWizard::on_click_btn_next, this);
    Bind(EVT_CALIBRATION_TRAY_SELECTION_CHANGED, &CalibrationWizard::on_select_tray, this);
#if 1 /*debug*/
    this->Bind(wxEVT_CHAR_HOOK, [this](auto& evt) {
        const int keyCode = evt.GetKeyCode();
        switch (keyCode)
        {
        case WXK_NUMPAD_PAGEUP:   case WXK_PAGEUP:
            show_page(get_curr_page()->get_prev_page());
            break;
        case WXK_NUMPAD_PAGEDOWN: case WXK_PAGEDOWN:
        {
            show_page(get_curr_page()->get_next_page());
            break;
        }
        default:
            evt.Skip();
            break;
        }
        });
#endif
}

void CalibrationWizard::create_presets_panel()
{
    const int PRESET_GAP = FromDIP(25);

    m_presets_panel = new wxPanel(m_scrolledWindow);
    auto panel_sizer = new wxBoxSizer(wxVERTICAL);

    auto printer_combo_text = new wxStaticText(m_presets_panel, wxID_ANY, _L("Printer"), wxDefaultPosition, wxDefaultSize, 0);
    printer_combo_text->Wrap(-1);
    printer_combo_text->SetFont(Label::Head_14);
    panel_sizer->Add(printer_combo_text, 0, wxALL, 0);
    m_comboBox_printer = new ComboBox(m_presets_panel, wxID_ANY, "", wxDefaultPosition, CALIBRATION_COMBOX_SIZE, 0, nullptr, wxCB_READONLY);
    panel_sizer->Add(m_comboBox_printer, 0, wxALL, 0);

    panel_sizer->AddSpacer(PRESET_GAP);

    auto nozzle_combo_text = new wxStaticText(m_presets_panel, wxID_ANY, _L("Nozzle"), wxDefaultPosition, wxDefaultSize, 0);
    nozzle_combo_text->Wrap(-1);
    nozzle_combo_text->SetFont(Label::Head_14);
    panel_sizer->Add(nozzle_combo_text, 0, wxALL, 0);
    m_comboBox_nozzle_dia = new ComboBox(m_presets_panel, wxID_ANY, "", wxDefaultPosition, CALIBRATION_COMBOX_SIZE, 0, nullptr, wxCB_READONLY);
    panel_sizer->Add(m_comboBox_nozzle_dia, 0, wxALL, 0);

    panel_sizer->AddSpacer(PRESET_GAP);

    auto plate_type_combo_text = new wxStaticText(m_presets_panel, wxID_ANY, _L("Plate Type"), wxDefaultPosition, wxDefaultSize, 0);
    plate_type_combo_text->Wrap(-1);
    plate_type_combo_text->SetFont(Label::Head_14);
    panel_sizer->Add(plate_type_combo_text, 0, wxALL, 0);
    m_comboBox_bed_type = new ComboBox(m_presets_panel, wxID_ANY, "", wxDefaultPosition, CALIBRATION_COMBOX_SIZE, 0, nullptr, wxCB_READONLY);
    panel_sizer->Add(m_comboBox_bed_type, 0, wxALL, 0);

    panel_sizer->AddSpacer(PRESET_GAP);

    {// Hide
        auto process_combo_text = new wxStaticText(m_presets_panel, wxID_ANY, _L("Process"), wxDefaultPosition, wxDefaultSize, 0);
        process_combo_text->Hide();
        process_combo_text->Wrap(-1);
        panel_sizer->Add(process_combo_text, 0, wxALL, 0);
        m_comboBox_process = new ComboBox(m_presets_panel, wxID_ANY, "", wxDefaultPosition, CALIBRATION_COMBOX_SIZE, 0, nullptr, wxCB_READONLY);
        m_comboBox_process->Hide();
        panel_sizer->Add(m_comboBox_process, 0, wxALL, 0);
    }// Hide

    m_select_ams_mode_panel = new wxPanel(m_presets_panel);
    auto choose_ams_sizer = new wxBoxSizer(wxVERTICAL);
    auto filament_from_text = new wxStaticText(m_select_ams_mode_panel, wxID_ANY, _L("Filament From"), wxDefaultPosition, wxDefaultSize, 0);
    filament_from_text->SetFont(Label::Head_14);
    choose_ams_sizer->Add(filament_from_text, 0);
    auto raioBox_sizer = new wxFlexGridSizer(2, 1, 0, FromDIP(10));
    m_ams_radiobox = new wxRadioButton(m_select_ams_mode_panel, wxID_ANY, _L("AMS"));
    m_ams_radiobox->SetValue(true);
    raioBox_sizer->Add(m_ams_radiobox, 0);
    //auto ams_text = new wxStaticText(m_choose_ams_panel, wxID_ANY, _L("AMS"), wxDefaultPosition, wxDefaultSize, 0);
    //raioBox_sizer->Add(ams_text);
    m_ext_spool_radiobox = new wxRadioButton(m_select_ams_mode_panel, wxID_ANY, _L("External Spool"));
    raioBox_sizer->Add(m_ext_spool_radiobox, 0);
    //auto ext_spool_text = new wxStaticText(m_choose_ams_panel, wxID_ANY, _L("External Spool"), wxDefaultPosition, wxDefaultSize, 0);
    //raioBox_sizer->Add(ext_spool_text, 0);
    choose_ams_sizer->Add(raioBox_sizer, 0);
    m_select_ams_mode_panel->SetSizer(choose_ams_sizer);
    panel_sizer->Add(m_select_ams_mode_panel, 0);

    panel_sizer->AddSpacer(PRESET_GAP);

    auto filament_for_title_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto filament_for_text = new wxStaticText(m_presets_panel, wxID_ANY, _L("Filament For Calibration"), wxDefaultPosition, wxDefaultSize, 0);
    filament_for_text->SetFont(Label::Head_14);
    filament_for_title_sizer->Add(filament_for_text, 0, wxALIGN_CENTER);
    filament_for_title_sizer->AddSpacer(FromDIP(25));
    m_ams_sync_button = new ScalableButton(m_presets_panel, wxID_ANY, "ams_fila_sync", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, false, 18);
    m_ams_sync_button->SetBackgroundColour(*wxWHITE);
    m_ams_sync_button->SetToolTip(_L("Synchronize filament list from AMS"));
    filament_for_title_sizer->Add(m_ams_sync_button, 0, wxALIGN_CENTER, 0);
    panel_sizer->Add(filament_for_title_sizer);
    m_filament_list_panel = new wxPanel(m_presets_panel);
    auto filament_list_sizer = new wxBoxSizer(wxVERTICAL);
    auto filament_list_tips = new wxStaticText(m_filament_list_panel, wxID_ANY, _L("Please select same type of material, because plate temperature might not be compatible with different type of material"), wxDefaultPosition, wxDefaultSize, 0);
    filament_list_tips->SetFont(Label::Body_13);
    filament_list_tips->SetForegroundColour(wxColour(145, 145, 145));
    filament_list_tips->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    filament_list_sizer->Add(filament_list_tips);
    filament_list_sizer->AddSpacer(FromDIP(10));
    m_muilti_ams_panel = new wxPanel(m_filament_list_panel);
    auto multi_ams_sizer = new wxBoxSizer(wxVERTICAL);
    auto ams_items_sizer = new wxBoxSizer(wxHORIZONTAL);
    for (int i = 0; i < 4; i++) {
        AMSinfo temp_info = AMSinfo{ std::to_string(i), std::vector<Caninfo>{} };
        auto amsitem = new AMSItem(m_muilti_ams_panel, wxID_ANY, temp_info);
        amsitem->Bind(wxEVT_LEFT_DOWN, [this, amsitem](wxMouseEvent& e) {
            on_switch_ams(amsitem->m_amsinfo.ams_id);
            e.Skip();
            });
        m_ams_item_list.push_back(amsitem);
        ams_items_sizer->Add(amsitem, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(6));
    }
    multi_ams_sizer->Add(ams_items_sizer, 0);
    multi_ams_sizer->AddSpacer(FromDIP(10));
    m_muilti_ams_panel->SetSizer(multi_ams_sizer);
    filament_list_sizer->Add(m_muilti_ams_panel);
    m_muilti_ams_panel->Hide();
    auto filament_fgSizer = new wxFlexGridSizer(2, 2, FromDIP(10), CALIBRATION_FGSIZER_HGAP);
    for (int i = 0; i < 16; i++) {
        auto filament_comboBox_sizer = new wxBoxSizer(wxHORIZONTAL);
        wxRadioButton* radio_btn = new wxRadioButton(m_filament_list_panel, wxID_ANY, "");
        CheckBox* check_box = new CheckBox(m_filament_list_panel);
        check_box->SetBackgroundColour(*wxWHITE);
        FilamentComboBox* fcb = new FilamentComboBox(m_filament_list_panel);
        fcb->SetRadioBox(radio_btn);
        fcb->SetCheckBox(check_box);
        fcb->set_select_mode(FilamentSelectMode::FSMRadioMode);
        filament_comboBox_sizer->Add(radio_btn, 0, wxALIGN_CENTER);
        filament_comboBox_sizer->Add(check_box, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(8));
        filament_comboBox_sizer->Add(fcb, 0, wxALIGN_CENTER);
        filament_fgSizer->Add(filament_comboBox_sizer, 0);
        radio_btn->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent& event) {
            SimpleEvent e(EVT_CALIBRATION_TRAY_SELECTION_CHANGED);
            e.SetEventObject(this);
            wxPostEvent(this, e);
            event.Skip();
            });
        check_box->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& event) {
            SimpleEvent e(EVT_CALIBRATION_TRAY_SELECTION_CHANGED);
            e.SetEventObject(this);
            wxPostEvent(this, e);
            event.Skip();
            });
        m_filament_comboBox_list.push_back(fcb);

        if (i >= 4)
            fcb->Show(false);
    }
    filament_list_sizer->Add(filament_fgSizer, 0);
    m_filament_list_panel->SetSizer(filament_list_sizer);
    panel_sizer->Add(m_filament_list_panel, 0);

    {// Hide
        m_virtual_panel = new wxPanel(m_presets_panel);
        auto virtual_sizer = new wxBoxSizer(wxHORIZONTAL);
        virtual_sizer->AddSpacer(FromDIP(10));
        wxRadioButton* radio_btn = new wxRadioButton(m_virtual_panel, wxID_ANY, "");
        CheckBox* check_box = new CheckBox(m_virtual_panel);
        m_virtual_tray_comboBox = new FilamentComboBox(m_virtual_panel);
        m_virtual_tray_comboBox->SetRadioBox(radio_btn);
        m_virtual_tray_comboBox->SetCheckBox(check_box);
        m_virtual_tray_comboBox->set_select_mode(FilamentSelectMode::FSMRadioMode);
        radio_btn->Bind(wxEVT_RADIOBUTTON, [this](wxCommandEvent& event) {
            SimpleEvent e(EVT_CALIBRATION_TRAY_SELECTION_CHANGED);
            e.SetEventObject(this);
            wxPostEvent(this, e);
            event.Skip();
            });
        virtual_sizer->Add(radio_btn, 0, wxALIGN_CENTER);
        virtual_sizer->Add(check_box, 0, wxALIGN_CENTER);
        virtual_sizer->Add(m_virtual_tray_comboBox, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(8));
        m_virtual_panel->SetSizer(virtual_sizer);
        m_virtual_panel->Hide();
        panel_sizer->Add(m_virtual_panel, 0);
    }// Hide

    panel_sizer->AddSpacer(PRESET_GAP);


    wxBoxSizer* horiz_sizer;
    horiz_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer* from_sizer;
    from_sizer = new wxBoxSizer(wxVERTICAL);
    m_from_text = new wxStaticText(m_presets_panel, wxID_ANY, _L("From"), wxDefaultPosition, wxDefaultSize, 0);
    m_from_text->Wrap(-1);
    m_from_text->SetFont(::Label::Body_14);
    from_sizer->Add(m_from_text, 0, wxALL, 0);
    m_from_value = new TextInput(m_presets_panel, wxEmptyString, _L("\u2103"), "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, 0);
    m_from_value->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    from_sizer->Add(m_from_value, 0, wxALL, 0);
    horiz_sizer->Add(from_sizer, 0, wxEXPAND, 0);

    horiz_sizer->Add(FromDIP(10), 0, 0, wxEXPAND, 0);

    wxBoxSizer* to_sizer;
    to_sizer = new wxBoxSizer(wxVERTICAL);
    m_to_text = new wxStaticText(m_presets_panel, wxID_ANY, _L("To"), wxDefaultPosition, wxDefaultSize, 0);
    m_to_text->Wrap(-1);
    m_to_text->SetFont(::Label::Body_14);
    to_sizer->Add(m_to_text, 0, wxALL, 0);
    m_to_value = new TextInput(m_presets_panel, wxEmptyString, _L("\u2103"), "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, 0);
    m_to_value->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    to_sizer->Add(m_to_value, 0, wxALL, 0);
    horiz_sizer->Add(to_sizer, 0, wxEXPAND, 0);

    horiz_sizer->Add(FromDIP(10), 0, 0, wxEXPAND, 0);

    wxBoxSizer* step_sizer;
    step_sizer = new wxBoxSizer(wxVERTICAL);
    m_step_text = new wxStaticText(m_presets_panel, wxID_ANY, _L("Step"), wxDefaultPosition, wxDefaultSize, 0);
    m_step_text->Wrap(-1);
    m_step_text->SetFont(::Label::Body_14);
    step_sizer->Add(m_step_text, 0, wxALL, 0);
    m_step = new TextInput(m_presets_panel, "5", _L("\u2103"), "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, 0);
    m_step->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    step_sizer->Add(m_step, 0, wxALL, 0);
    horiz_sizer->Add(step_sizer, 0, wxEXPAND, 0);
    panel_sizer->Add(horiz_sizer, 0, wxEXPAND, 0);

    panel_sizer->AddSpacer(PRESET_GAP * 6);

    auto info_sizer = new wxFlexGridSizer(0, 3, 0, FromDIP(10));
    info_sizer->SetFlexibleDirection(wxBOTH);
    info_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    auto nozzle_temp_sizer = new wxBoxSizer(wxVERTICAL);
    auto nozzle_temp_text = new wxStaticText(m_presets_panel, wxID_ANY, _L("Nozzle temperature"));
    m_nozzle_temp = new TextInput(m_presets_panel, wxEmptyString, _L("\u2103"), "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, wxTE_READONLY);
    nozzle_temp_sizer->Add(nozzle_temp_text, 0, wxALIGN_LEFT);
    nozzle_temp_sizer->Add(m_nozzle_temp, 0, wxEXPAND);

    auto bed_temp_sizer = new wxBoxSizer(wxVERTICAL);
    auto bed_temp_text = new wxStaticText(m_presets_panel, wxID_ANY, _L("Bed temperature"));
    m_bed_temp = new TextInput(m_presets_panel, wxEmptyString, _L("\u2103"), "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, wxTE_READONLY);
    bed_temp_sizer->Add(bed_temp_text, 0, wxALIGN_LEFT);
    bed_temp_sizer->Add(m_bed_temp, 0, wxEXPAND);

    auto max_flow_sizer = new wxBoxSizer(wxVERTICAL);
    auto max_flow_text = new wxStaticText(m_presets_panel, wxID_ANY, _L("Max volumetric speed"));
    m_max_volumetric_speed = new TextInput(m_presets_panel, wxEmptyString, _L("mm\u00B3"), "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, wxTE_READONLY);
    max_flow_sizer->Add(max_flow_text, 0, wxALIGN_LEFT);
    max_flow_sizer->Add(m_max_volumetric_speed, 0, wxEXPAND);

    m_nozzle_temp->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [](auto&) {});
    m_bed_temp->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [](auto&) {});
    m_max_volumetric_speed->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [](auto&) {});

    info_sizer->Add(nozzle_temp_sizer);
    info_sizer->Add(bed_temp_sizer);
    info_sizer->Add(max_flow_sizer);
    panel_sizer->Add(info_sizer, 0, wxEXPAND);

    m_filaments_incompatible_tips = new wxStaticText(m_presets_panel, wxID_ANY, _L("filaments incompatible, please select same type of material"));
    m_filaments_incompatible_tips->SetFont(Label::Body_13);
    m_filaments_incompatible_tips->SetForegroundColour(wxColour(230, 92, 92));
    m_filaments_incompatible_tips->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    m_filaments_incompatible_tips->Hide();
    panel_sizer->Add(m_filaments_incompatible_tips, 0, wxEXPAND);

    m_bed_type_incompatible_tips = new wxStaticText(m_presets_panel, wxID_ANY, _L("filaments incompatible, please select same type of material"));
    m_bed_type_incompatible_tips->SetFont(Label::Body_13);
    m_bed_type_incompatible_tips->SetForegroundColour(wxColour(230, 92, 92));
    m_bed_type_incompatible_tips->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    m_bed_type_incompatible_tips->Hide();
    panel_sizer->Add(m_bed_type_incompatible_tips, 0, wxEXPAND);

    m_presets_panel->SetSizer(panel_sizer);
    m_presets_panel->Layout();
    panel_sizer->Fit(m_presets_panel);
}

void CalibrationWizard::create_print_panel() {
    m_print_panel = new wxPanel(m_scrolledWindow);
    m_print_panel->SetSize({ FromDIP(600),-1 });
    m_print_panel->SetMinSize({ FromDIP(600),-1 });
    auto panel_sizer = new wxBoxSizer(wxVERTICAL);

    wxPanel* panel_text = new wxPanel(m_print_panel);
    panel_text->SetBackgroundColour(*wxWHITE);

    wxBoxSizer* text_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_staticText_progress_percent = new wxStaticText(panel_text, wxID_ANY, "0%", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_progress_percent->SetFont(::Label::Head_18);
    m_staticText_progress_percent->SetForegroundColour(wxColour(0, 174, 66));

    m_staticText_layers = new wxStaticText(panel_text, wxID_ANY, _L("Layer: N/A"));
    m_staticText_layers->SetFont(::Label::Body_15);
    m_staticText_layers->SetForegroundColour(wxColour(146, 146, 146));

    m_staticText_progress_left_time = new wxStaticText(panel_text, wxID_ANY, NA_STR, wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_progress_left_time->Wrap(-1);
    m_staticText_progress_left_time->SetFont(::Label::Body_15);
    m_staticText_progress_left_time->SetForegroundColour(wxColour(146, 146, 146));

    text_sizer->Add(m_staticText_progress_percent, 0, wxALIGN_CENTER, 0);
    text_sizer->Add(0, 0, 1, wxEXPAND, 0);
    text_sizer->Add(m_staticText_layers, 0, wxALIGN_CENTER | wxALL, 0);
    text_sizer->Add(0, 0, 0, wxLEFT, FromDIP(20));
    text_sizer->Add(m_staticText_progress_left_time, 0, wxALIGN_CENTER | wxALL, 0);
    text_sizer->AddSpacer(FromDIP(90));

    panel_text->SetSizer(text_sizer);

    auto panel_progressbar = new wxPanel(m_print_panel, wxID_ANY);
    panel_progressbar->SetBackgroundColour(*wxWHITE);
    auto progressbar_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_print_gauge_progress = new ProgressBar(panel_progressbar, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize);
    m_print_gauge_progress->SetValue(0);
    m_print_gauge_progress->SetHeight(FromDIP(8));
    m_print_gauge_progress->SetSize(wxSize(FromDIP(400), -1));
    m_print_gauge_progress->SetMinSize(wxSize(FromDIP(400), -1));

    m_button_pause_resume = new ScalableButton(panel_progressbar, wxID_ANY, "print_control_pause", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true);
    m_button_pause_resume->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        if (m_button_pause_resume->GetToolTipText() == _L("Pause")) {
            m_button_pause_resume->SetBitmap_("print_control_pause_hover");
        }

        if (m_button_pause_resume->GetToolTipText() == _L("Resume")) {
            m_button_pause_resume->SetBitmap_("print_control_resume_hover");
        }
        });
    m_button_pause_resume->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {
        if (m_button_pause_resume->GetToolTipText() == _L("Pause")) {
            m_button_pause_resume->SetBitmap_("print_control_pause");
        }

        if (m_button_pause_resume->GetToolTipText() == _L("Resume")) {
            m_button_pause_resume->SetBitmap_("print_control_resume");
        }
        });

    m_button_abort = new ScalableButton(panel_progressbar, wxID_ANY, "print_control_stop", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true);
    m_button_abort->SetToolTip(_L("Stop"));
    m_button_abort->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        m_button_abort->SetBitmap_("print_control_stop_hover");
        });
    m_button_abort->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {
        m_button_abort->SetBitmap_("print_control_stop"); }
    );

    progressbar_sizer->Add(m_print_gauge_progress, 1, wxALIGN_CENTER_VERTICAL, 0);
    progressbar_sizer->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(18));
    progressbar_sizer->Add(m_button_pause_resume, 0, wxALL, FromDIP(5));
    progressbar_sizer->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(18));
    progressbar_sizer->Add(m_button_abort, 0, wxALL, FromDIP(5));

    panel_progressbar->SetSizer(progressbar_sizer);

    panel_sizer->AddSpacer(FromDIP(15));
    panel_sizer->Add(panel_text, 0, wxEXPAND, 0);
    panel_sizer->Add(panel_progressbar, 0, wxEXPAND, 0);
    panel_sizer->AddSpacer(FromDIP(15));

    m_print_panel->SetSizer(panel_sizer);

    m_print_panel->SetSizer(panel_sizer);
    m_print_panel->Layout();
    panel_sizer->Fit(m_print_panel);
}

void CalibrationWizard::create_send_progress_bar()
{
    m_send_progress_panel = new wxPanel(m_scrolledWindow);
    auto panel_sizer = new wxBoxSizer(wxVERTICAL);
    m_send_progress_bar = std::shared_ptr<BBLStatusBarSend>(new BBLStatusBarSend(m_send_progress_panel));
    panel_sizer->Add(m_send_progress_bar->get_panel(), 0, wxEXPAND, 0);
    m_send_progress_panel->Hide();

    m_send_progress_panel->SetSizer(panel_sizer);
    m_send_progress_panel->Layout();
    panel_sizer->Fit(m_send_progress_panel);
}

void CalibrationWizard::add_presets_panel_to_page(CalibrationWizardPage* page, wxBoxSizer* sizer)
{
    m_presets_panel->Reparent(page);
    sizer->Add(m_presets_panel, 0, wxEXPAND, 0);
}

void CalibrationWizard::add_print_panel_to_page(CalibrationWizardPage* page, wxBoxSizer* sizer)
{
    m_btn_recali = page->get_prev_btn();
    m_btn_next = page->get_next_btn();
    m_print_panel->Reparent(page);
    sizer->Add(m_print_panel, 0, wxALIGN_CENTER, 0);
}

void CalibrationWizard::add_send_progress_to_page(CalibrationWizardPage* page, wxBoxSizer* sizer)
{
    m_send_progress_panel->Reparent(page);
    sizer->Add(m_send_progress_panel, 0, wxEXPAND, 0);
}

void CalibrationWizard::show_send_progress_bar(bool show)
{
    m_send_progress_panel->Show(show);

    if (get_curr_page()->get_next_btn()->GetButtonType() == Calibrate)
        get_curr_page()->get_next_btn()->Show(!show);

    Layout();
}

void CalibrationWizard::on_choose_ams(wxCommandEvent& event) {
    m_filament_list_panel->Show();
    m_virtual_panel->Hide();
    Layout();

    m_virtual_tray_comboBox->SetValue(false);

    event.Skip();
}

void CalibrationWizard::on_choose_ext_spool(wxCommandEvent& event) {
    m_virtual_panel->Show();
    m_filament_list_panel->Hide();
    Layout();

    for (int i = 0; i < m_filament_comboBox_list.size(); i++) {
        m_filament_comboBox_list[i]->SetValue(false);
    }

    event.Skip();
}

void CalibrationWizard::show_page(CalibrationWizardPage* page) {
    if (!page)
        return;

    auto page_node = m_first_page;
    while (page_node)
    {
        page_node->Hide();
        page_node = page_node->get_next_page();
    }
    m_curr_page = page;
    m_curr_page->Show();

    Layout();
}

void CalibrationWizard::on_click_btn_prev(IntEvent& event)
{
    ButtonType button_type = static_cast<ButtonType>(event.get_data());
    switch (button_type)
    {
    case Slic3r::GUI::Back:
        show_page(get_curr_page()->get_prev_page());
        break;
    case Slic3r::GUI::Recalibrate:
        if (!curr_obj ||
            curr_obj->is_system_printing() ||
            curr_obj->is_in_calibration() ||// vs obj->is_in_extrusion_cali() ?
            curr_obj->is_in_printing()) {
            MessageDialog msg_dlg(nullptr, _L("Is in printing. Please wait for printing to complete"), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }
        show_page(get_frist_page());
        break;
    }
}

void CalibrationWizard::on_click_btn_next(IntEvent& event)
{
    ButtonType button_type = static_cast<ButtonType>(event.get_data());
    switch (button_type)
    {
    case Slic3r::GUI::Start:
    case Slic3r::GUI::Next:
        show_page(get_curr_page()->get_next_page());
        break;
    case Slic3r::GUI::Calibrate: {
        if(!curr_obj){
            MessageDialog msg_dlg(nullptr, _L("No Printer Connected!"), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }

        if (!m_printer_preset || !m_filament_preset || !m_print_preset) {
            wxString tips;
            if (!m_printer_preset || !m_print_preset) {
                tips = _L("Please select a printer and nozzle for calibration.");
            }
            else {
                tips = _L("Please select filament for calibration.");
            }
            MessageDialog msg_dlg(nullptr, tips, wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }

        if (curr_obj->is_system_printing() ||
            curr_obj->is_in_calibration() ||
            curr_obj->is_in_printing()) {
            MessageDialog msg_dlg(nullptr, _L("Is in printing. Please wait for printing to complete"), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }

        std::vector<int> tray_ids = get_selected_tray();
        if (start_calibration(tray_ids)) {
            m_send_progress_bar->set_cancel_callback_fina([this]() {
                BOOST_LOG_TRIVIAL(info) << "CalibrationWizard::print_job: enter canceled";
                // todo cancel send print job related logic (PACalibration doesn't send a print job)
                //if (Slic3r::GUI::print_job) {
                //    if (Slic3r::GUI::print_job->is_running()) {
                //        BOOST_LOG_TRIVIAL(info) << "calibration_print_job: canceled";
                //        Slic3r::GUI::print_job->cancel();
                //    }
                //    Slic3r::GUI::print_job->join();
                //}
                show_send_progress_bar(false);
                });
        }
        break;
    }
    case Slic3r::GUI::Save:
        if (!curr_obj ||
            curr_obj->is_system_printing() ||
            curr_obj->is_in_calibration() ||// vs obj->is_in_extrusion_cali() ?
            curr_obj->is_in_printing()) {
            MessageDialog msg_dlg(nullptr, _L("Is in printing. Please wait for printing to complete"), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }
        if(save_calibration_result())
            show_page(get_frist_page());

        break;
    default:
        break;
    }
}

void CalibrationWizard::update_print_progress()
{
    if (!curr_obj) {
        reset_printing_values();
        return;
    }

    m_print_panel->Freeze();

    static bool print_finish = false;

    if (curr_obj->is_support_layer_num) {
        m_staticText_layers->Show();
    }
    else {
        m_staticText_layers->Hide();
    }

    //update_model_info(); //set model_task_id to subtask_id

    if (curr_obj->is_system_printing()
        || curr_obj->is_in_calibration()) {// vs obj->is_in_extrusion_cali() ?
        reset_printing_values();
    }
    else if (curr_obj->is_in_printing() || curr_obj->print_status == "FINISH") {
        if (curr_obj->is_in_prepare() || curr_obj->print_status == "SLICING") {
            reset_printing_values();
            m_btn_recali->Hide();
        }
        else {
            if (curr_obj->can_resume()) {
                m_button_pause_resume->SetBitmap_("print_control_resume");
                if (m_button_pause_resume->GetToolTipText() != _L("Resume")) { m_button_pause_resume->SetToolTip(_L("Resume")); }
            }
            else {
                m_button_pause_resume->SetBitmap_("print_control_pause");
                if (m_button_pause_resume->GetToolTipText() != _L("Pause")) { m_button_pause_resume->SetToolTip(_L("Pause")); }
            }

            if (curr_obj->print_status == "FINISH") {// obj->is_extrusion_cali_finished() also can get in
                m_button_abort->Enable(false);
                m_button_abort->SetBitmap_("print_control_stop_disable");
                m_button_pause_resume->Enable(false);
                m_button_pause_resume->SetBitmap_("print_control_resume_disable");
                m_btn_next->Enable(true);
                if (!print_finish && curr_obj->get_modeltask() && curr_obj->get_modeltask()->design_id > 0) {
                    print_finish = true;
                    //show_print_progress_bar(false);
                    //show_page(get_curr_page()->get_next_page());
                }
            }
            else {
                m_button_abort->Enable(true);
                m_button_abort->SetBitmap_("print_control_stop");
                m_button_pause_resume->Enable(true);
                m_btn_next->Enable(false);

                if (print_finish) {
                    print_finish = false;
                }
            }

            // update left time 
            std::string left_time;
            try {
                left_time = get_bbl_monitor_time_dhm(curr_obj->mc_left_time);
            }
            catch (...) {
                ;
            }
            wxString left_time_text = left_time.empty() ? NA_STR : wxString::Format("-%s", left_time);
            m_staticText_progress_left_time->SetLabelText(left_time_text);

            if (curr_obj->subtask_) {
                m_print_gauge_progress->SetValue(curr_obj->subtask_->task_progress);
                m_staticText_progress_percent->SetLabelText(wxString::Format("%d%%", curr_obj->subtask_->task_progress));
                m_staticText_layers->SetLabelText(wxString::Format(_L("Layer: %d/%d"), curr_obj->curr_layer, curr_obj->total_layers));

            }
            else {
                m_print_gauge_progress->SetValue(0);
                m_staticText_progress_percent->SetLabelText(NA_STR);
                m_staticText_layers->SetLabelText(wxString::Format(_L("Layer: %s"), NA_STR));
            }
        }
    }
    else {
        reset_printing_values();
    }
    m_print_panel->Layout();

    m_print_panel->Thaw();
}

void CalibrationWizard::reset_printing_values()
{
    m_button_pause_resume->Enable(false);
    m_button_pause_resume->SetBitmap_("print_control_pause_disable");

    m_button_abort->Enable(false);
    m_button_abort->SetBitmap_("print_control_stop_disable");

    m_btn_next->Enable(false);
    m_btn_recali->Show();

    m_print_gauge_progress->SetValue(0);
    m_staticText_progress_left_time->SetLabelText(NA_STR);
    m_staticText_layers->SetLabelText(wxString::Format(_L("Layer: %s"), NA_STR));
    m_staticText_progress_percent->SetLabelText(NA_STR);
    m_print_panel->Layout();
}

void CalibrationWizard::on_subtask_pause_resume(wxCommandEvent& event)
{
    if (curr_obj) {
        if (curr_obj->can_resume())
            curr_obj->command_task_resume();
        else
            curr_obj->command_task_pause();
    }
}

void CalibrationWizard::on_subtask_abort(wxCommandEvent& event)
{
    MessageDialog msg_dlg(nullptr, _L("Are you sure you want to cancel this print?"), wxEmptyString, wxICON_WARNING | wxOK | wxCANCEL);
    if (msg_dlg.ShowModal() == wxID_OK) {
        if (curr_obj) curr_obj->command_task_abort();
        m_btn_recali->Show();
    }
    //if (abort_dlg == nullptr) {
    //    abort_dlg = new SecondaryCheckDialog(this->GetParent(), wxID_ANY, _L("Cancel print"));
    //    abort_dlg->Bind(EVT_SECONDARY_CHECK_CONFIRM, [this](wxCommandEvent& e) {
    //        if (obj) obj->command_task_abort();
    //        });
    //}
    //abort_dlg->update_text(_L("Are you sure you want to cancel this print?"));
    //abort_dlg->on_show();
}

std::vector<int> CalibrationWizard::get_selected_tray()
{
    std::vector<int> tray_ids;

    if (m_filament_from_ext_spool) {
        if(m_virtual_tray_comboBox->GetRadioBox()->GetValue())
            tray_ids.push_back(m_virtual_tray_comboBox->get_tray_id());
    }
    else {
        if (get_ams_select_mode() == FilamentSelectMode::FSMCheckBoxMode) {
            for (auto fcb : m_filament_comboBox_list) {
                if (fcb->GetCheckBox()->GetValue()) {
                    tray_ids.push_back(fcb->get_tray_id());
                }
            }
        }
        else if (get_ams_select_mode() == FilamentSelectMode::FSMRadioMode) {
            for (auto fcb : m_filament_comboBox_list) {
                if (fcb->GetRadioBox()->GetValue()) {
                    tray_ids.push_back(fcb->get_tray_id());
                }
            }
        }
    }
    return tray_ids;
}

FilamentComboBoxList CalibrationWizard::get_selected_filament_comboBox()
{
    FilamentComboBoxList fcb_list;

    if (m_filament_from_ext_spool) {
        if (m_virtual_tray_comboBox->GetRadioBox()->GetValue())
            fcb_list.push_back(m_virtual_tray_comboBox);
    }
    else {
        if (get_ams_select_mode() == FilamentSelectMode::FSMCheckBoxMode) {
            for (auto fcb : m_filament_comboBox_list) {
                if (fcb->GetCheckBox()->GetValue()) {
                    fcb_list.push_back(fcb);
                }
            }
        }
        else if (get_ams_select_mode() == FilamentSelectMode::FSMRadioMode) {
            for (auto fcb : m_filament_comboBox_list) {
                if (fcb->GetRadioBox()->GetValue()) {
                    fcb_list.push_back(fcb);
                }
            }
        }
    }
    return fcb_list;
}

void CalibrationWizard::update_filaments_from_preset()
{
    for (auto& fcb : m_filament_comboBox_list)
        fcb->update_from_preset();
    m_virtual_tray_comboBox->update_from_preset();

    Layout();
}

void CalibrationWizard::init_presets_selections() {
    init_nozzle_selections();
    init_bed_type_selections();
    init_process_selections();
}

void CalibrationWizard::update_printer_selections()
{
    Slic3r::DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    // clear machine list
    obj_list.clear();
    m_comboBox_printer->Clear();
    wxArrayString                         machine_list_name;
    std::map<std::string, MachineObject*> option_list;

    // user machine list
    option_list = dev->get_my_machine_list();
    for (auto it = option_list.begin(); it != option_list.end(); it++) {
        if (it->second && (it->second->is_online() || it->second->is_connected())) {
            obj_list.push_back(it->second);
            wxString dev_name_text = from_u8(it->second->dev_name);
            if (it->second->is_lan_mode_printer()) {
                dev_name_text += "(LAN)";
            }
            machine_list_name.Add(dev_name_text);
        }
    }
    m_comboBox_printer->Set(machine_list_name);

    // comboBox_printer : set a default value
    MachineObject* obj = dev->get_selected_machine();
    if (obj) {
        for (auto i = 0; i < obj_list.size(); i++) {
            if (obj_list[i]->dev_id == obj->dev_id) {
                m_comboBox_printer->SetSelection(i);
                wxCommandEvent event(wxEVT_COMBOBOX);
                event.SetEventObject(m_comboBox_printer);
                wxPostEvent(m_comboBox_printer, event);
            }
        }
    }
    else {
        int def_selection = obj_list.size() - 1;
        m_comboBox_printer->SetSelection(def_selection);
        wxCommandEvent event(wxEVT_COMBOBOX);
        event.SetEventObject(m_comboBox_printer);
        wxPostEvent(m_comboBox_printer, event);
    }
}

void CalibrationWizard::init_nozzle_selections()
{
    m_comboBox_nozzle_dia->Clear();
    m_printer_preset = nullptr;
    if (curr_obj) {
        m_comboBox_nozzle_dia->AppendString(wxString::Format("%1.1f", 0.2));
        m_comboBox_nozzle_dia->AppendString(wxString::Format("%1.1f", 0.4));
        m_comboBox_nozzle_dia->AppendString(wxString::Format("%1.1f", 0.6));
        m_comboBox_nozzle_dia->AppendString(wxString::Format("%1.1f", 0.8));
        m_comboBox_nozzle_dia->SetSelection(-1);
    }
}

void CalibrationWizard::init_bed_type_selections()
{
    m_comboBox_bed_type->Clear();
    int curr_selection = 0;
    if (curr_obj) {
        const ConfigOptionDef* bed_type_def = print_config_def.get("curr_bed_type");
        if (bed_type_def && bed_type_def->enum_keys_map) {
            for (auto item : *bed_type_def->enum_keys_map) {
                if (item.first == "Default Plate")
                    continue;
                m_comboBox_bed_type->AppendString(_L(item.first));
            }
            m_comboBox_bed_type->SetSelection(curr_selection);
        }
    }
}

void CalibrationWizard::init_process_selections()
{
    m_comboBox_process->Clear();
    m_print_preset = nullptr;
    wxArrayString print_items;
    PresetBundle* preset_bundle = wxGetApp().preset_bundle;

    double nozzle_value = 0.4;
    wxString nozzle_value_str = m_comboBox_nozzle_dia->GetValue();
    try {
        nozzle_value_str.ToDouble(&nozzle_value);
    }
    catch (...) {
        ;
    }

    if (preset_bundle && curr_obj) {
        for (auto print_it = preset_bundle->prints.begin(); print_it != preset_bundle->prints.end(); print_it++) {
            ConfigOption* printer_opt = print_it->config.option("compatible_printers");
            ConfigOptionStrings* printer_strs = dynamic_cast<ConfigOptionStrings*>(printer_opt);
            for (auto printer_str : printer_strs->values) {
                if (m_printer_preset && m_printer_preset->name == printer_str) {
                    wxString process_name = wxString::FromUTF8(print_it->name);
                    print_items.Add(process_name);
                    break;
                }
            }
        }
        m_comboBox_process->Set(print_items);
        m_comboBox_process->SetSelection((print_items.size() + 1) / 2 - 1);

        for (auto print_it = preset_bundle->prints.begin(); print_it != preset_bundle->prints.end(); print_it++) {
            wxString print_name = wxString::FromUTF8(print_it->name);
            if (print_name.compare(m_comboBox_process->GetValue()) == 0) {
                m_print_preset = &*print_it;
            }
        }
    }
}

void CalibrationWizard::on_select_printer(wxCommandEvent& evt) {
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;

    auto selection = m_comboBox_printer->GetSelection();
    MachineObject* new_obj = nullptr;
    for (int i = 0; i < obj_list.size(); i++) {
        if (i == selection) {
            // check lan mode machine
            //if (obj_list[i]->is_lan_mode_printer() && !obj_list[i]->has_access_right()) {
            //    ConnectPrinterDialog dlg(wxGetApp().mainframe, wxID_ANY, _L("Input access code"));
            //    dlg.set_machine_object(obj_list[i]);
            //    auto res = dlg.ShowModal();
            //    m_printer_last_select = "";
            //    m_comboBox_printer->SetSelection(-1);
            //    m_comboBox_printer->Refresh();
            //    m_comboBox_printer->Update();
            //}
            new_obj = obj_list[i];
            break;
        }
    }

    if (new_obj && !new_obj->get_lan_mode_connection_state()) {
        new_obj->command_get_version();
        new_obj->command_request_push_all();
        dev->set_selected_machine(new_obj->dev_id, true);

        wxGetApp().sidebar().load_ams_list(new_obj->dev_id, new_obj);

        if (new_obj != curr_obj) {
            curr_obj = new_obj;
            init_presets_selections();
            wxGetApp().preset_bundle->set_calibrate_printer("");
            update_filaments_from_preset();
            on_update_ams_filament(false);
        }
    }
    else {
        BOOST_LOG_TRIVIAL(error) << "CalibrationWizard::on_select_printer dev_id not found";
        return;
    }
}

void CalibrationWizard::on_select_nozzle(wxCommandEvent& evt) {
    if (curr_obj) {
        double nozzle_value = 0.4;
        wxString nozzle_value_str = m_comboBox_nozzle_dia->GetValue();
        try {
            nozzle_value_str.ToDouble(&nozzle_value);
        }
        catch (...) {
            ;
        }
        PresetBundle* preset_bundle = wxGetApp().preset_bundle;
        for (auto printer_it = preset_bundle->printers.begin(); printer_it != preset_bundle->printers.end(); printer_it++) {
            // only use system printer preset
            if (!printer_it->is_system) continue;

            ConfigOption* printer_nozzle_opt = printer_it->config.option("nozzle_diameter");
            ConfigOptionFloats* printer_nozzle_vals = nullptr;
            if (printer_nozzle_opt)
                printer_nozzle_vals = dynamic_cast<ConfigOptionFloats*>(printer_nozzle_opt);
            std::string model_id = printer_it->get_current_printer_type(preset_bundle);
            if (model_id.compare(curr_obj->printer_type) == 0
                && printer_nozzle_vals
                && abs(printer_nozzle_vals->get_at(0) - nozzle_value) < 1e-3) {
                m_printer_preset = &*printer_it;
            }
        }

        if (m_printer_preset) {
            preset_bundle->set_calibrate_printer(m_printer_preset->name);
            update_filaments_from_preset();
            on_update_ams_filament(false);

            init_process_selections();
        }
    }
}

void CalibrationWizard::on_select_bed_type(wxCommandEvent& evt) {
    recommend_input_value();
}

void CalibrationWizard::on_switch_ams(std::string ams_id)
{
    for (auto i = 0; i < m_ams_item_list.size(); i++) {
        AMSItem* item = m_ams_item_list[i];
        if (item->m_amsinfo.ams_id == ams_id) {
            item->OnSelected();
        }
        else {
            item->UnSelected();
        }
    }

    for (int i = 0; i < m_filament_comboBox_list.size(); i++) {
        if (stoi(ams_id) * 4 <= i && i < stoi(ams_id) * 4 + 4)
            m_filament_comboBox_list[i]->Show(true);
        else {
            m_filament_comboBox_list[i]->SetValue(false);
            m_filament_comboBox_list[i]->Show(false);
        }
    }

    Layout();
}

void CalibrationWizard::on_select_tray(SimpleEvent& evt) {
    // when comboBox set selection or value of checkbox/radio changed will enter,
    // check if preset names are same

    FilamentComboBoxList fcb_list = get_selected_filament_comboBox();
    if (fcb_list.empty()) {
        m_filament_preset = nullptr;
        m_filaments_incompatible_tips->Hide();
        recommend_input_value();
        return;
    }

    auto first_preset = fcb_list[0]->GetComboBox()->get_selected_preset();
    if(!first_preset) {
        m_filament_preset = nullptr;
        recommend_input_value();
        return;
    }
    bool all_preset_same = true;
    for (auto fcb : fcb_list) {
        auto selected_preset = fcb->GetComboBox()->get_selected_preset();
        if (selected_preset && selected_preset->filament_id != first_preset->filament_id)
            all_preset_same = false;
    }

    if (!all_preset_same) {
        m_filament_preset = nullptr;
        m_filaments_incompatible_tips->Show();
        Layout();
    }
    else {
        m_filament_preset = const_cast<Preset*>(first_preset);
        m_filaments_incompatible_tips->Hide();
        Layout();
    }

    recommend_input_value();
}

void CalibrationWizard::on_update_ams_filament(bool dialog)
{
    auto& list = wxGetApp().preset_bundle->filament_ams_list;
    if (list.empty() && dialog) {
        MessageDialog dlg(this, _L("No AMS filaments. Please select a printer in 'Device' page to load AMS info."), _L("Sync filaments with AMS"), wxOK);
        dlg.ShowModal();
        return;
    }

    for (auto& entry : list) {
        if (entry.first < m_filament_comboBox_list.size()) {
            m_filament_comboBox_list[entry.first]->load_tray_from_ams(entry.first, entry.second);
        }
        if (entry.first == VIRTUAL_TRAY_ID)
            m_virtual_tray_comboBox->load_tray_from_ams(entry.first, entry.second);
    }

    std::vector<AMSinfo> ams_info;
    for (auto ams = curr_obj->amsList.begin(); ams != curr_obj->amsList.end(); ams++) {
        AMSinfo info;
        info.ams_id = ams->first;
        if (ams->second->is_exists && info.parse_ams_info(ams->second, curr_obj->ams_calibrate_remain_flag, curr_obj->is_support_ams_humidity)) ams_info.push_back(info);
    }
    for (auto i = 0; i < m_ams_item_list.size(); i++) {
        AMSItem* item = m_ams_item_list[i];
        if(ams_info.size() > 1)
            m_muilti_ams_panel->Show();
        else
            m_muilti_ams_panel->Hide();

        if (i < ams_info.size() && ams_info.size() > 1) {
            item->Update(ams_info[i]);
            item->Open();
            if (i == 0)
                item->OnSelected();
        }
        else {
            item->Close();
        }
    }
    Layout();
}

bool CalibrationWizard::recommend_input_value() {
    if (!m_filament_preset){
        m_nozzle_temp->GetTextCtrl()->SetValue(wxEmptyString);
        m_bed_temp->GetTextCtrl()->SetValue(wxEmptyString);
        m_max_volumetric_speed->GetTextCtrl()->SetValue(wxEmptyString);
        m_bed_type_incompatible_tips->SetLabel("");
        m_bed_type_incompatible_tips->Hide();
        return false;
    }

    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    int bed_temp_int = -1;
    if (preset_bundle) {
        // update nozzle temperature
        ConfigOption* opt_nozzle_temp = m_filament_preset->config.option("nozzle_temperature");
        if (opt_nozzle_temp) {
            ConfigOptionInts* opt_min_ints = dynamic_cast<ConfigOptionInts*>(opt_nozzle_temp);
            if (opt_min_ints) {
                wxString text_nozzle_temp = wxString::Format("%d", opt_min_ints->get_at(0));
                m_nozzle_temp->GetTextCtrl()->SetValue(text_nozzle_temp);
            }
        }
        // update bed temperature
        bed_temp_int = get_bed_temp(&m_filament_preset->config);
        wxString bed_temp_text = wxString::Format("%d", bed_temp_int);
        m_bed_temp->GetTextCtrl()->SetValue(bed_temp_text);
        // update max flow speed
        ConfigOption* opt_flow_speed = m_filament_preset->config.option("filament_max_volumetric_speed");
        if (opt_flow_speed) {
            ConfigOptionFloats* opt_flow_floats = dynamic_cast<ConfigOptionFloats*>(opt_flow_speed);
            if (opt_flow_floats) {
                wxString flow_val_text = wxString::Format("%0.2f", opt_flow_floats->get_at(0));
                m_max_volumetric_speed->GetTextCtrl()->SetValue(flow_val_text);
            }
        }

        // check compatibility
        if (m_bed_temp->GetTextCtrl()->GetValue().compare("0") == 0) {
            m_nozzle_temp->GetTextCtrl()->SetValue(wxEmptyString);
            m_bed_temp->GetTextCtrl()->SetValue(wxEmptyString);
            m_max_volumetric_speed->GetTextCtrl()->SetValue(wxEmptyString);
            wxString tips = wxString::Format(_L("%s does not support %s"), m_comboBox_bed_type->GetValue(), m_filament_preset->alias);
            m_bed_type_incompatible_tips->SetLabel(tips);
            m_bed_type_incompatible_tips->Show();
            Layout();
            return false;
        }
        else {
            m_bed_type_incompatible_tips->SetLabel("");
            m_bed_type_incompatible_tips->Hide();
        }
    }
    return true;
}

int CalibrationWizard::get_bed_temp(DynamicPrintConfig* config)
{
    BedType curr_bed_type = BedType(m_comboBox_bed_type->GetSelection() + btDefault + 1);
    const ConfigOptionInts* opt_bed_temp_ints = config->option<ConfigOptionInts>(get_bed_temp_key(curr_bed_type));
    if (opt_bed_temp_ints) {
        return opt_bed_temp_ints->get_at(0);
    }
    return -1;
}

bool CalibrationWizard::save_presets(const std::string& config_key, ConfigOption* config_value)
{
    auto filament_presets = &wxGetApp().preset_bundle->filaments;
    DynamicPrintConfig* filament_config = &m_filament_preset->config;

    SavePresetDialog dlg(this, Preset::Type::TYPE_FILAMENT, "Calibrated");
    if (dlg.ShowModal() != wxID_OK)
        return false;
    std::string name = dlg.get_name();
    bool save_to_project = dlg.get_save_to_project_selection(Preset::TYPE_FILAMENT);

    filament_config->set_key_value(config_key, config_value);
    // Save the preset into Slic3r::data_dir / presets / section_name / preset_name.ini
    filament_presets->save_current_preset(name, false, save_to_project, m_filament_preset);

    Preset* new_preset = filament_presets->find_preset(name, false, true);
    if (!new_preset) {
        BOOST_LOG_TRIVIAL(info) << "create new preset failed";
        return false;
    }

    new_preset->sync_info = "create";
    if (wxGetApp().is_user_login())
        new_preset->user_id = wxGetApp().getAgent()->get_user_id();
    BOOST_LOG_TRIVIAL(info) << "sync_preset: create preset = " << new_preset->name;

    new_preset->save_info();

    // Mark the print & filament enabled if they are compatible with the currently selected preset.
    // If saving the preset changes compatibility with other presets, keep the now incompatible dependent presets selected, however with a "red flag" icon showing that they are no more compatible.
    wxGetApp().preset_bundle->update_compatible(PresetSelectCompatibleType::Never);

    // update current comboBox selected preset
    std::string curr_preset_name = filament_presets->get_edited_preset().name;
    wxGetApp().plater()->sidebar().update_presets_from_to(Preset::TYPE_FILAMENT, curr_preset_name, new_preset->name);

    return true;
}

PressureAdvanceWizard::PressureAdvanceWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, id, pos, size, style)
{
    for (int i = 0; i < 4/*m_filament_comboBox_list.size()*/; i++) {
        m_filament_comboBox_list[i]->set_select_mode(FSMCheckBoxMode);
    }
    // todo hide some comboBox

    create_pages();
}

void PressureAdvanceWizard::create_pages()
{
    // page 0 : start page
    //m_page0 = new CalibrationWizardPage(m_scrolledWindow, false);
    //m_page0->set_page_title(_L("Pressure Advance"));
    //m_page0->set_page_index(_L("1/3"));

    //auto page0_top_sizer = m_page0->get_top_vsizer();
    //auto page0_top_description = new wxStaticText(m_page0, wxID_ANY, _L("Pressure advance is a technique used to compensate for the delay between the extruder motor and the actual extrusion of the filament. It works by increasing the pressure of the filament in the nozzle just before a rapid movement occurs, improving the accuracy of the printed object. It can be adjusted through the printer's firmware or slicer software, and is useful for objects with sharp corners or intricate details."), wxDefaultPosition, wxDefaultSize, 0);
    //page0_top_description->SetFont(::Label::Body_14);
    //page0_top_description->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    //page0_top_description->SetMinSize(wxSize(CALIBRATION_TEXT_MAX_LENGTH, -1));
    //page0_top_sizer->Add(page1_top_description, 0, wxALL, 0);
    //page0_top_sizer->AddSpacer(FromDIP(20));

    //auto page0_content_sizer = m_page0->get_content_vsizer();
    //auto bitmap_sizer = new wxBoxSizer(wxHORIZONTAL);
    //auto page0_bitmap1 = new wxStaticBitmap(m_page0, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    //page0_bitmap1->SetMinSize(wxSize(560, 450)); // todo modify
    //page0_bitmap1->SetBackgroundColour(*wxBLACK); // todo modify
    //bitmap_sizer->Add(page1_bitmap1, 0, wxALL, 0);

    //auto page0_bitmap2 = new wxStaticBitmap(m_page0, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    //page0_bitmap2->SetMinSize(wxSize(560, 450)); // todo modify
    //page0_bitmap2->SetBackgroundColour(*wxBLACK); // todo modify
    //bitmap_sizer->Add(page1_bitmap2, 0, wxALL, 0);
    //page0_content_sizer->Add(bitmap_sizer, 0);

    //auto page0_prev_btn = m_page0->get_prev_btn();
    //page0_prev_btn->Hide();

    //auto page0_next_btn = m_page0->get_next_btn();
    //page0_next_btn->SetLabel(_L("Start"));
    //page0_next_btn->SetButtonType(ButtonType::Start);

    //m_all_pages_sizer->Add(m_page0, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 1 : preset page
    m_page1 = new CalibrationWizardPage(m_scrolledWindow, true);
    m_page1->set_page_title(_L("Pressure Advance"));
    m_page1->set_page_index(_L("1/3"));

    auto page1_content_sizer = m_page1->get_content_vsizer();

    m_from_text->Hide();
    m_to_text->Hide();
    m_from_value->Hide();
    m_to_value->Hide();
    m_step_text->Hide();
    m_step->Hide();

    add_presets_panel_to_page(m_page1, page1_content_sizer);

    auto page1_prev_btn = m_page1->get_prev_btn();
    page1_prev_btn->Hide();

    auto page1_next_btn = m_page1->get_next_btn();
    page1_next_btn->SetLabel(_L("Calibrate"));
    page1_next_btn->SetButtonType(ButtonType::Calibrate);

    m_all_pages_sizer->Add(m_page1, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 2 : print page
    m_page2 = new CalibrationWizardPage(m_scrolledWindow, false);
    m_page2->set_page_title(_L("Pressure Advance"));
    m_page2->set_page_index(_L("2/3"));
    auto page2_content_sizer = m_page2->get_content_vsizer();
    auto page2_bitmap = new wxStaticBitmap(m_page2, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    page2_bitmap->SetMinSize(wxSize(800, 600)); // todo modify
    page2_bitmap->SetBackgroundColour(*wxBLACK); // todo modify
    page2_content_sizer->Add(page2_bitmap, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);
    page2_content_sizer->AddSpacer(FromDIP(20));
    add_print_panel_to_page(m_page2, page2_content_sizer);

    auto page2_prev_btn = m_page2->get_prev_btn();
    page2_prev_btn->SetLabel(_L("Re-Calibrate"));
    page2_prev_btn->SetButtonType(ButtonType::Recalibrate);
    page2_prev_btn->Hide();

    auto page2_next_btn = m_page2->get_next_btn();
    page2_next_btn->SetLabel(_L("Next"));
    page2_next_btn->SetButtonType(ButtonType::Next);

    m_all_pages_sizer->Add(m_page2, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 3 : save page
    m_page3 = new CalibrationWizardPage(m_scrolledWindow, false);
    m_page3->set_page_title(_L("Pressure Advance"));
    m_page3->set_page_index(_L("3/3"));

    auto page3_top_sizer = m_page3->get_top_vsizer();
    auto page3_top_description = new wxStaticText(m_page3, wxID_ANY, _L("The calibration lines has been printed.\nPlease take out the build plate from the printer and find the line with the most uniform extrusion. The number next to it is the factor K. Fill in its value and click the Save button to save it to the printer. "), wxDefaultPosition, wxDefaultSize, 0);
    page3_top_description->SetFont(::Label::Body_14);
    page3_top_description->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page3_top_description->SetMinSize(wxSize(CALIBRATION_TEXT_MAX_LENGTH, -1));
    page3_top_sizer->Add(page3_top_description, 0, wxALIGN_CENTER, 0);
    page3_top_sizer->AddSpacer(FromDIP(20));

    auto page3_content_sizer = m_page3->get_content_vsizer();
    auto page3_bitmap = new wxStaticBitmap(m_page3, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    page3_bitmap->SetMinSize(wxSize(800, 600)); // todo modify
    page3_bitmap->SetBackgroundColour(*wxBLACK); // todo modify
    page3_content_sizer->Add(page3_bitmap, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);
    page3_content_sizer->AddSpacer(FromDIP(20));

    auto value_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto k_value_text = new wxStaticText(m_page3, wxID_ANY, _L("Factor K"), wxDefaultPosition, wxDefaultSize, 0);
    k_value_text->Wrap(-1);
    k_value_text->SetFont(::Label::Body_14);
    auto n_value_text = new wxStaticText(m_page3, wxID_ANY, _L("Factor N"), wxDefaultPosition, wxDefaultSize, 0);
    n_value_text->Wrap(-1);
    n_value_text->SetFont(::Label::Body_14);
    m_k_val = new TextInput(m_page3, wxEmptyString, "", "", wxDefaultPosition, CALIBRATION_OPTIMAL_INPUT_SIZE, 0);
    m_n_val = new TextInput(m_page3, wxEmptyString, "", "", wxDefaultPosition, CALIBRATION_OPTIMAL_INPUT_SIZE, 0);
    value_sizer->Add(k_value_text, 0, wxALIGN_CENTER_VERTICAL, 0);
    value_sizer->AddSpacer(FromDIP(10));
    value_sizer->Add(m_k_val, 0);
    value_sizer->AddSpacer(FromDIP(50));
    value_sizer->Add(n_value_text, 0, wxALIGN_CENTER_VERTICAL, 0);
    value_sizer->AddSpacer(FromDIP(10));
    value_sizer->Add(m_n_val, 0);
    page3_content_sizer->Add(value_sizer, 0, wxALIGN_CENTER);
    page3_content_sizer->AddSpacer(FromDIP(20));

    auto page3_prev_btn = m_page3->get_prev_btn();
    page3_prev_btn->Hide();

    auto page3_next_btn = m_page3->get_next_btn();
    page3_next_btn->SetLabel(_L("Save"));
    page3_next_btn->SetButtonType(ButtonType::Save);

    m_all_pages_sizer->Add(m_page3, 1, wxEXPAND | wxALL, FromDIP(25));

    // link page
    m_page1->chain(m_page2)->chain(m_page3);

    m_first_page = m_page1;
    m_curr_page = m_page1;
    show_page(m_curr_page);
}

bool PressureAdvanceWizard::start_calibration(std::vector<int> tray_ids)
{
    int nozzle_temp = -1;
    int bed_temp = -1;
    float max_volumetric_speed = -1;

    nozzle_temp = stoi(m_nozzle_temp->GetTextCtrl()->GetValue().ToStdString());
    bed_temp = stoi(m_bed_temp->GetTextCtrl()->GetValue().ToStdString());
    max_volumetric_speed = stof(m_max_volumetric_speed->GetTextCtrl()->GetValue().ToStdString());

    if (bed_temp < 0 || nozzle_temp < 0 || max_volumetric_speed < 0) {
        MessageDialog msg_dlg(nullptr, _L("Make sure bed_temp > 0 \nnozzle_temp > 0\nmax_volumetric_speed > 0"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    if (curr_obj->printer_type == "BL-P001" || curr_obj->printer_type == "BL-P002") {
        X1CCalibInfos calib_infos;
        for (int tray_id : tray_ids) {
            X1CCalibInfos::X1CCalibInfo calib_info;
            calib_info.tray_id = tray_id;
            calib_info.setting_id = m_filament_preset->setting_id;
            calib_info.bed_temp = bed_temp;
            calib_info.nozzle_temp = nozzle_temp;
            calib_info.max_volumetric_speed = max_volumetric_speed;
            calib_infos.calib_infos.push_back(calib_info);
        }
        std::string error_message;
        CalibUtils::calib_PA(calib_infos, error_message);
        show_page(get_curr_page()->get_next_page());

        if (error_message.empty())
            return true;
        else
            return false;
    }
    else if (curr_obj->printer_type == "C11") {
        curr_obj->command_start_extrusion_cali(tray_ids[0], nozzle_temp, bed_temp, max_volumetric_speed, m_filament_preset->setting_id);
        show_page(get_curr_page()->get_next_page());
        return true;
    }

    return false;
}

bool PressureAdvanceWizard::save_calibration_result()
{
    auto check_k_validation = [](wxString k_text)
    {
        if (k_text.IsEmpty())
            return false;
        double k = 0.0;
        try {
            k_text.ToDouble(&k);
        }
        catch (...) {
            ;
        }

        if (k < 0 || k > 0.5)
            return false;
        return true;
    };

    auto check_k_n_validation = [](wxString k_text, wxString n_text)
    {
        if (k_text.IsEmpty() || n_text.IsEmpty())
            return false;
        double k = 0.0;
        try {
            k_text.ToDouble(&k);
        }
        catch (...) {
            ;
        }

        double n = 0.0;
        try {
            n_text.ToDouble(&n);
        }
        catch (...) {
            ;
        }
        if (k < 0 || k > 0.5)
            return false;
        if (n < 0.6 || n > 2.0)
            return false;
        return true;
    };

    wxString k_text = m_k_val->GetTextCtrl()->GetValue();
    wxString n_text = m_n_val->GetTextCtrl()->GetValue();
    if (!check_k_n_validation(k_text, n_text)) {
        //wxString k_tips = _L("Please input a valid value (K in 0~0.5)");
        wxString kn_tips = _L("Please input a valid value (K in 0~0.5, N in 0.6~2.0)");
        MessageDialog msg_dlg(nullptr, kn_tips, wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    double k = 0.0;
    k_text.ToDouble(&k);

    double n = 0.0;
    n_text.ToDouble(&n);

    // set values
    int nozzle_temp = -1;
    int bed_temp = -1;
    float max_volumetric_speed = -1;
    std::string setting_id;
    std::string name;

    nozzle_temp = stoi(m_nozzle_temp->GetTextCtrl()->GetValue().ToStdString());
    bed_temp = stoi(m_bed_temp->GetTextCtrl()->GetValue().ToStdString());
    max_volumetric_speed = stof(m_max_volumetric_speed->GetTextCtrl()->GetValue().ToStdString());
    setting_id = m_filament_preset->setting_id;
    name = m_filament_preset->name;

    // send command
    std::string ams_id = 0;// m_ams_control->GetCurentAms();
    std::string tray_id = 0;// m_ams_control->GetCurrentCan(ams_id);
    if (ams_id.compare(std::to_string(VIRTUAL_TRAY_ID)) != 0) {
        if (tray_id.empty()) {
        }
        else {
            tray_id = std::to_string(atoi(tray_id.c_str()) + 4 * atoi(ams_id.c_str()));
        }
    }
    else
        tray_id = ams_id;

    curr_obj->command_extrusion_cali_set(stoi(tray_id), setting_id, name, k, n, bed_temp, nozzle_temp, max_volumetric_speed);
    MessageDialog msg_dlg(nullptr, _L("Saved success."), wxEmptyString, wxICON_WARNING | wxOK);
    msg_dlg.ShowModal();
    return true;
}

bool PressureAdvanceWizard::recommend_input_value()
{
    return CalibrationWizard::recommend_input_value();
}

FlowRateWizard::FlowRateWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, id, pos, size, style)
{
    create_pages();

    m_page3->get_next_btn()->Bind(wxEVT_BUTTON, &FlowRateWizard::on_fine_tune, this);
}

void FlowRateWizard::create_low_end_pages() {
    // page 3 : save coarse result
    m_page3 = new CalibrationWizardPage(m_scrolledWindow, false);
    m_page3->set_page_title(_L("Flow Rate"));
    m_page3->set_page_index(_L("3"));

    auto page3_top_sizer = m_page3->get_top_vsizer();
    auto page3_top_description = new wxStaticText(m_page3, wxID_ANY, _L("The calibration blocks has been printed. Examine the blocks and determine which one has the smoothest top surface."), wxDefaultPosition, wxDefaultSize, 0);
    page3_top_description->Wrap(-1);
    page3_top_description->SetFont(::Label::Body_14);
    page3_top_sizer->Add(page3_top_description, 0, wxALIGN_CENTER, 0);
    page3_top_sizer->AddSpacer(FromDIP(20));

    auto page3_content_sizer = m_page3->get_content_vsizer();
    auto page3_bitmap = new wxStaticBitmap(m_page3, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    page3_bitmap->SetMinSize(wxSize(560, 450)); // todo modify
    page3_bitmap->SetBackgroundColour(*wxBLACK); // todo modify
    page3_content_sizer->Add(page3_bitmap, 0, wxALIGN_CENTER, 0);

    page3_content_sizer->AddSpacer(FromDIP(20));

    auto coarse_value_sizer = new wxBoxSizer(wxVERTICAL);
    auto coarse_value_text = new wxStaticText(m_page3, wxID_ANY, _L("Fill in the value above the block with smoothest top surface"), wxDefaultPosition, wxDefaultSize, 0);
    coarse_value_text->Wrap(-1);
    coarse_value_text->SetFont(::Label::Head_14);
    m_optimal_block_coarse = new ComboBox(m_page3, wxID_ANY, "", wxDefaultPosition, CALIBRATION_OPTIMAL_INPUT_SIZE, 0, nullptr, wxCB_READONLY);
    wxArrayString coarse_block_items;
    for (int i = 0; i < 9; i++)
    {
        coarse_block_items.Add(std::to_string(-20 + (i * 5)));
    }
    m_optimal_block_coarse->Set(coarse_block_items);
    coarse_value_sizer->Add(coarse_value_text, 0, wxALIGN_CENTER);
    coarse_value_sizer->Add(m_optimal_block_coarse, 0, wxALIGN_CENTER);
    page3_content_sizer->Add(coarse_value_sizer, 0, wxALIGN_CENTER, 0);
    page3_content_sizer->AddSpacer(FromDIP(20));

    // todo add skip calibration 2

    auto page3_prev_btn = m_page3->get_prev_btn();
    page3_prev_btn->Hide();

    auto page3_next_btn = m_page3->get_next_btn();
    page3_next_btn->SetLabel(_L("Save & Calibrate"));
    page3_next_btn->SetButtonType(ButtonType::Calibrate);

    m_all_pages_sizer->Add(m_page3, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 4 : print page
    m_page4 = new CalibrationWizardPage(m_scrolledWindow, true);
    m_page4->set_page_title(_L("Flow Rate"));
    m_page4->set_page_index(_L("4"));
    auto page4_content_sizer = m_page4->get_content_vsizer();
    auto page4_bitmap = new wxStaticBitmap(m_page4, wxID_ANY, create_scaled_bitmap("max_volumetric_speed_wizard", nullptr, 400), wxDefaultPosition, wxDefaultSize, 0);
    page4_content_sizer->Add(page4_bitmap, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);
    page4_content_sizer->AddSpacer(FromDIP(20));
    m_all_pages_sizer->Add(m_page4, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 5 : save fine result
    m_page5 = new CalibrationWizardPage(m_scrolledWindow, false);
    m_page5->set_page_title(_L("Flow Rate"));
    m_page5->set_page_index(_L("5"));

    auto page5_top_sizer = m_page5->get_top_vsizer();
    auto page5_top_description = new wxStaticText(m_page5, wxID_ANY, _L("The calibration blocks has been printed. Examine the blocks and determine which one has the smoothest top surface. Fill in the number above that block and click the Save button to save the calibrated flow rate to the filament preset."), wxDefaultPosition, wxDefaultSize, 0);
    page5_top_description->SetFont(::Label::Body_14);
    page5_top_description->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page5_top_description->SetMinSize(wxSize(CALIBRATION_TEXT_MAX_LENGTH, -1));
    page5_top_sizer->Add(page5_top_description, 0, wxALIGN_CENTER, 0);
    page5_top_sizer->AddSpacer(FromDIP(20));

    auto page5_content_sizer = m_page5->get_content_vsizer();
    auto page5_bitmap = new wxStaticBitmap(m_page5, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    page5_bitmap->SetMinSize(wxSize(560, 450)); // todo modify
    page5_bitmap->SetBackgroundColour(*wxBLACK); // todo modify
    page5_content_sizer->Add(page5_bitmap, 0, wxALIGN_CENTER, 0);

    page5_content_sizer->AddSpacer(FromDIP(20));

    auto fine_value_sizer = new wxBoxSizer(wxVERTICAL);
    auto fine_value_text = new wxStaticText(m_page5, wxID_ANY, _L("Fill in the value above the block with smoothest top surface"), wxDefaultPosition, wxDefaultSize, 0);
    fine_value_text->Wrap(-1);
    fine_value_text->SetFont(::Label::Head_14);
    m_optimal_block_fine = new ComboBox(m_page5, wxID_ANY, "", wxDefaultPosition, CALIBRATION_OPTIMAL_INPUT_SIZE, 0, nullptr, wxCB_READONLY);
    wxArrayString fine_block_items;
    for (int i = 0; i < 10; i++)
    {
        fine_block_items.Add(std::to_string(-9 + (i)));
    }
    m_optimal_block_fine->Set(fine_block_items);
    fine_value_sizer->Add(fine_value_text, 0, wxALIGN_CENTER);
    fine_value_sizer->Add(m_optimal_block_fine, 0, wxALIGN_CENTER);
    page5_content_sizer->Add(fine_value_sizer, 0, wxALIGN_CENTER, 0);
    page5_content_sizer->AddSpacer(FromDIP(20));

    auto page5_prev_btn = m_page5->get_prev_btn();
    page5_prev_btn->Hide();

    auto page5_next_btn = m_page5->get_next_btn();
    page5_next_btn->SetLabel(_L("Save"));
    page5_next_btn->SetButtonType(ButtonType::Save);

    m_all_pages_sizer->Add(m_page5, 1, wxEXPAND | wxALL, FromDIP(25));

    // link page
    m_page1->chain(m_page2)->chain(m_page3)->chain(m_page4)->chain(m_page5);

    m_first_page = m_page1;
    m_curr_page = m_page1;
    show_page(m_curr_page);
}

void FlowRateWizard::create_high_end_pages() {
    // page 5 : save fine result
    m_high_end_page3 = new CalibrationWizardPage(m_scrolledWindow, false);
    m_high_end_page3->set_page_title(_L("Flow Rate"));
    m_high_end_page3->set_page_index(_L("3"));

    auto high_end_page3_top_sizer = m_high_end_page3->get_top_vsizer();
    auto high_end_page3_top_description = new wxStaticText(m_high_end_page3, wxID_ANY, _L("The calibration blocks has been printed. Examine the blocks and determine which one has the smoothest top surface. Fill in the number above that block and click the Save button to save the calibrated flow rate to the filament preset."), wxDefaultPosition, wxDefaultSize, 0);
    high_end_page3_top_description->SetFont(::Label::Body_14);
    high_end_page3_top_description->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    high_end_page3_top_description->SetMinSize(wxSize(CALIBRATION_TEXT_MAX_LENGTH, -1));
    high_end_page3_top_sizer->Add(high_end_page3_top_description, 0, wxALIGN_CENTER, 0);
    high_end_page3_top_sizer->AddSpacer(FromDIP(20));

    auto high_end_page3_content_sizer = m_high_end_page3->get_content_vsizer();
    auto high_end_page3_bitmap = new wxStaticBitmap(m_high_end_page3, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    high_end_page3_bitmap->SetMinSize(wxSize(560, 450)); // todo modify
    high_end_page3_bitmap->SetBackgroundColour(*wxBLACK); // todo modify
    high_end_page3_content_sizer->Add(high_end_page3_bitmap, 0, wxALIGN_CENTER, 0);

    high_end_page3_content_sizer->AddSpacer(FromDIP(20));

    auto high_end_page3_prev_btn = m_high_end_page3->get_prev_btn();
    high_end_page3_prev_btn->Hide();

    auto high_end_page3_next_btn = m_high_end_page3->get_next_btn();
    high_end_page3_next_btn->SetLabel(_L("Save"));
    high_end_page3_next_btn->SetButtonType(ButtonType::Save);

    m_all_pages_sizer->Add(m_high_end_page3, 1, wxEXPAND | wxALL, FromDIP(25));

    // link page
    m_page1->chain(m_page2)->chain(m_high_end_page3);

    m_first_page = m_page1;
    m_curr_page = m_page1;
    show_page(m_curr_page);
}

void FlowRateWizard::create_pages()
{
    // page 0 : start page
    //m_page0 = new CalibrationWizardPage(m_scrolledWindow, false);
    //m_page0->set_page_title(_L("Flow Rate"));
    //m_page0->set_page_index(_L("0/5"));

    //auto page0_top_sizer = m_page0->get_top_vsizer();
    //auto page0_top_description = new wxStaticText(m_page0, wxID_ANY, _L("Flow rate calibration in 3D printing is an important process that ensures the printer is extruding the correct amount of filament during the printing process. It is suitable for materials with significant thermal shrinkage/expansion and materials with inaccurate filament diameter."), wxDefaultPosition, wxDefaultSize, 0);
    //page0_top_description->SetFont(::Label::Body_14);
    //page0_top_description->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    //page0_top_description->SetMinSize(wxSize(CALIBRATION_TEXT_MAX_LENGTH, -1));
    //page0_top_sizer->Add(page1_top_description, 0, wxALL, 0);
    //page0_top_sizer->AddSpacer(FromDIP(20));

    //auto page0_content_sizer = m_page0->get_content_vsizer();
    //auto bitmap_sizer1 = new wxBoxSizer(wxHORIZONTAL);
    //auto page0_bitmap1 = new wxStaticBitmap(m_page0, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    //page0_bitmap1->SetMinSize(wxSize(560, 450)); // todo modify
    //page0_bitmap1->SetBackgroundColour(*wxBLACK); // todo modify
    //bitmap_sizer1->Add(page0_bitmap1, 0, wxALL, 0);

    //auto page0_bitmap2 = new wxStaticBitmap(m_page0, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    //page0_bitmap2->SetMinSize(wxSize(560, 450)); // todo modify
    //page0_bitmap2->SetBackgroundColour(*wxBLACK); // todo modify
    //bitmap_sizer1->Add(page0_bitmap2, 0, wxALL, 0);
    //page0_content_sizer->Add(bitmap_sizer1, 0, wxALL, 0);

    //auto page0_prev_btn = m_page0->get_prev_btn();
    //page0_prev_btn->Hide();

    //auto page0_next_btn = m_page0->get_next_btn();
    //page0_next_btn->SetLabel(_L("Start"));
    //page0_next_btn->SetButtonType(ButtonType::Start);

    //m_all_pages_sizer->Add(m_page0, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 1 : preset page
    m_page1 = new CalibrationWizardPage(m_scrolledWindow, true);
    m_page1->set_page_title(_L("Flow Rate"));
    m_page1->set_page_index(_L("1"));

    auto page1_content_sizer = m_page1->get_content_vsizer();

    m_from_text->Hide();
    m_to_text->Hide();
    m_step_text->Hide();
    m_from_value->Hide();
    m_to_value->Hide();
    m_step->Hide();

    add_presets_panel_to_page(m_page1, page1_content_sizer);

    add_send_progress_to_page(m_page1, m_page1->get_btn_hsizer());

    auto page1_prev_btn = m_page1->get_prev_btn();
    page1_prev_btn->Hide();

    auto page1_next_btn = m_page1->get_next_btn();
    page1_next_btn->SetLabel(_L("Coarse Tune"));
    page1_next_btn->SetButtonType(ButtonType::Calibrate);

    m_all_pages_sizer->Add(m_page1, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 2 : print page
    m_page2 = new CalibrationWizardPage(m_scrolledWindow, false);
    m_page2->set_page_title(_L("Flow Rate"));
    m_page2->set_page_index(_L("2"));
    auto page2_content_sizer = m_page2->get_content_vsizer();
    auto page2_bitmap = new wxStaticBitmap(m_page2, wxID_ANY, create_scaled_bitmap("max_volumetric_speed_wizard", nullptr, 400), wxDefaultPosition, wxDefaultSize, 0);
    page2_content_sizer->Add(page2_bitmap, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);
    page2_content_sizer->AddSpacer(FromDIP(20));
    add_print_panel_to_page(m_page2, page2_content_sizer);

    auto page2_prev_btn = m_page2->get_prev_btn();
    page2_prev_btn->SetLabel(_L("Re-Calibrate"));
    page2_prev_btn->SetButtonType(ButtonType::Recalibrate);
    page2_prev_btn->Hide();

    auto page2_next_btn = m_page2->get_next_btn();
    page2_next_btn->SetLabel(_L("Next"));
    page2_next_btn->SetButtonType(ButtonType::Next);

    m_all_pages_sizer->Add(m_page2, 1, wxEXPAND | wxALL, FromDIP(25));

    create_low_end_pages();

    m_first_page = m_page1;
    m_curr_page = m_page1;
    show_page(m_curr_page);
}

void FlowRateWizard::on_select_printer(wxCommandEvent& evt) {
    CalibrationWizard::on_select_printer(evt);
    if (curr_obj) {
        if (curr_obj->printer_type == "BL-P001" || curr_obj->printer_type == "BL-P002")
        {
            if (m_page3) {
                m_page3->Destroy();
                m_page3 = nullptr;
            }
            if (m_page4) {
                m_page4->Destroy();
                m_page4 = nullptr;
            }
            if (m_page5) {
                m_page5->Destroy();
                m_page5 = nullptr;
            }
            if (!m_high_end_page3)
                create_high_end_pages();
        }
        else if (curr_obj->printer_type == "C11")
        {
            if (m_high_end_page3) {
                m_high_end_page3->Destroy();
                m_high_end_page3 = nullptr;
            }
            if (!m_page3)
                create_low_end_pages();
        }
    }
}

bool FlowRateWizard::start_calibration(std::vector<int> tray_ids)
{
    int nozzle_temp = -1;
    int bed_temp = -1;
    float max_volumetric_speed = -1;

    nozzle_temp = stoi(m_nozzle_temp->GetTextCtrl()->GetValue().ToStdString());
    bed_temp = stoi(m_bed_temp->GetTextCtrl()->GetValue().ToStdString());
    max_volumetric_speed = stof(m_max_volumetric_speed->GetTextCtrl()->GetValue().ToStdString());

    if (bed_temp < 0 || nozzle_temp < 0 || max_volumetric_speed < 0) {
        MessageDialog msg_dlg(nullptr, _L("Make sure bed_temp > 0 \nnozzle_temp > 0\nmax_volumetric_speed > 0"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    if (curr_obj->printer_type == "BL-P001" || curr_obj->printer_type == "BL-P002") {
        X1CCalibInfos calib_infos;
        for (int tray_id : tray_ids) {
            X1CCalibInfos::X1CCalibInfo calib_info;
            calib_info.tray_id = tray_id;
            calib_info.setting_id = m_filament_preset->setting_id;
            calib_info.bed_temp = bed_temp;
            calib_info.nozzle_temp = nozzle_temp;
            calib_info.max_volumetric_speed = max_volumetric_speed;
            calib_infos.calib_infos.push_back(calib_info);
        }
        std::string error_message;
        CalibUtils::calib_flowrate_X1C(calib_infos, error_message);
        show_page(get_curr_page()->get_next_page());
        if (error_message.empty())
            return true;
        else
            return false;
    }
    else {
        int pass = -1;
        if (get_curr_page() == m_page1)
            pass = 1;
        else if (get_curr_page() == m_page3)
            pass = 2;
        else
            return false;

        CalibInfo calib_info;
        calib_info.dev_id = curr_obj->dev_id;
        calib_info.select_ams = "[" + std::to_string(tray_ids[0]) + "]";
        calib_info.process_bar = m_send_progress_bar;
        calib_info.bed_type = BedType(m_comboBox_bed_type->GetSelection() + btDefault + 1);
        calib_info.printer_prest = m_printer_preset;
        calib_info.filament_prest = m_filament_preset;
        calib_info.print_prest = m_print_preset;

        std::string error_message;
        CalibUtils::calib_flowrate(pass, calib_info, error_message);
        show_send_progress_bar(true);
        return true;
    }
}

bool FlowRateWizard::save_calibration_result()
{
    if (curr_obj->printer_type == "C11") {
        if (m_optimal_block_coarse->GetValue().IsEmpty() || m_optimal_block_fine->GetValue().IsEmpty())
        {
            MessageDialog msg_dlg(nullptr, _L("Choose a block with smoothest top surface."), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return false;
        }

        DynamicPrintConfig& filament_config = m_filament_preset->config;
        auto old_flow_ratio = filament_config.option<ConfigOptionFloats>("filament_flow_ratio")->get_at(0);
        int fine_value = stoi(m_optimal_block_fine->GetValue().ToStdString());
        auto new_flow_ratio = old_flow_ratio * (100 + fine_value) / 100;
        if (save_presets("filament_flow_ratio", new ConfigOptionFloats{ new_flow_ratio })) {
            add_send_progress_to_page(m_page1, m_page1->get_btn_hsizer());
            add_print_panel_to_page(m_page2, m_page2->get_content_vsizer());
            return true;
        }
        else {
            return false;
        }
    }
    else
        return false; // todo high end machine save logic
}

bool FlowRateWizard::recommend_input_value()
{
    return CalibrationWizard::recommend_input_value();
}

void FlowRateWizard::on_fine_tune(wxCommandEvent& e) {
    if (m_optimal_block_coarse->GetValue().IsEmpty()){
        MessageDialog msg_dlg(nullptr, _L("Choose a block with smoothest top surface."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    DynamicPrintConfig& filament_config = m_filament_preset->config;
    int coarse_value = stoi(m_optimal_block_coarse->GetValue().ToStdString());
    auto old_flow_ratio = filament_config.option<ConfigOptionFloats>("filament_flow_ratio")->get_at(0);
    auto new_flow_ratio = old_flow_ratio * (100 + coarse_value) / 100;
    filament_config.set_key_value("filament_flow_ratio", new ConfigOptionFloats{ new_flow_ratio });

    add_send_progress_to_page(m_page3, m_page3->get_btn_hsizer());

    add_print_panel_to_page(m_page4, m_page4->get_content_vsizer());

    e.Skip();
}

MaxVolumetricSpeedWizard::MaxVolumetricSpeedWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, id, pos, size, style) 
{
    create_pages();
}

void MaxVolumetricSpeedWizard::create_pages() 
{
    //// page 0 : start page
    //m_page0 = new CalibrationWizardPage(m_scrolledWindow, false);
    //m_page0->set_page_title(_L("Max Volumetric Speed"));
    //m_page0->set_page_index(_L("1/3"));

    //auto page0_top_sizer = m_page0->get_top_vsizer();
    //auto page0_top_description = new wxStaticText(m_page0, wxID_ANY, _L("This setting stands for how much volume of filament can be melted and extruded per second."), wxDefaultPosition, wxDefaultSize, 0);
    //page0_top_description->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    //page0_top_description->SetFont(::Label::Body_14);
    //page0_top_description->SetMinSize(wxSize(CALIBRATION_TEXT_MAX_LENGTH, -1));
    //page0_top_sizer->Add(page1_top_description, 0, wxALL, 0);
    //page0_top_sizer->AddSpacer(FromDIP(20));

    //auto page0_content_sizer = m_page0->get_content_vsizer();

    //wxFlexGridSizer* fgSizer;
    //fgSizer = new wxFlexGridSizer(0, 2, FromDIP(60), FromDIP(20));
    //fgSizer->SetFlexibleDirection(wxBOTH);
    //fgSizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    //auto page0_description1 = new wxStaticText(m_page0, wxID_ANY, _L("If the value is set too high, under-extrusion will happen and cause poor apperance on the printed model."), wxDefaultPosition, wxDefaultSize, 0);
    //page0_description1->Wrap(FromDIP(500));
    //page0_description1->SetFont(::Label::Body_14);
    //fgSizer->Add(page0_description1, 1, wxALL | wxEXPAND, 0);

    //auto page0_bitmap1 = new wxStaticBitmap(m_page0, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    //page0_bitmap1->SetMinSize(wxSize(450, 300)); // todo modify
    //page0_bitmap1->SetBackgroundColour(*wxBLACK); // todo modify
    //fgSizer->Add(page0_bitmap1, 0, wxALL, 0);

    //auto page1_description2 = new wxStaticText(m_page0, wxID_ANY, _L("If the value is set too low, the print speed will be limited and make the print time longer. Take the model on the right picture for example.\nmax volumetric speed [n] mm^3/s costs [x] minutes.\nmax volumetric speed [m] mm^3/s costs [y] minutes"), wxDefaultPosition, wxDefaultSize, 0);
    //page1_description2->Wrap(FromDIP(500));
    //page1_description2->SetFont(::Label::Body_14);
    //fgSizer->Add(page1_description2, 1, wxALL | wxEXPAND, 0);

    //auto page0_bitmap2 = new wxStaticBitmap(m_page0, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    //page0_bitmap2->SetMinSize(wxSize(450, 300)); // todo modify
    //page0_bitmap2->SetBackgroundColour(*wxBLACK); // todo modify
    //fgSizer->Add(page0_bitmap2, 0, wxALL, 0);

    //page0_content_sizer->Add(fgSizer, 1, wxEXPAND, 0);

    //auto page0_prev_btn = m_page0->get_prev_btn();
    //page0_prev_btn->Hide();

    //auto page0_next_btn = m_page0->get_next_btn();
    //page0_next_btn->SetLabel(_L("Start"));
    //page0_next_btn->SetButtonType(ButtonType::Start);

    //m_all_pages_sizer->Add(m_page0, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 1 : preset page
    m_page1 = new CalibrationWizardPage(m_scrolledWindow, true);
    m_page1->set_page_title(_L("Max Volumetric Speed"));
    m_page1->set_page_index(_L("1/3"));

    auto page1_content_sizer = m_page1->get_content_vsizer();

    m_from_text->SetLabel(_L("From Speed"));
    m_to_text->SetLabel(_L("To Speed"));
    m_from_value->SetLabel(_L("mm\u00B3/s"));
    m_to_value->SetLabel(_L("mm\u00B3/s"));
    m_step->SetLabel(_L("mm\u00B3/s"));
    m_step->GetTextCtrl()->SetLabel("0.5");

    add_presets_panel_to_page(m_page1, page1_content_sizer);

    add_send_progress_to_page(m_page1, m_page1->get_btn_hsizer());

    auto page1_prev_btn = m_page1->get_prev_btn();
    page1_prev_btn->Hide();

    auto page1_next_btn = m_page1->get_next_btn();
    page1_next_btn->SetLabel(_L("Calibrate"));
    page1_next_btn->SetButtonType(ButtonType::Calibrate);

    m_all_pages_sizer->Add(m_page1, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 2 : print page
    m_page2 = new CalibrationWizardPage(m_scrolledWindow, false);
    m_page2->set_page_title(_L("Max Volumetric Speed"));
    m_page2->set_page_index(_L("2/3"));
    auto page2_content_sizer = m_page2->get_content_vsizer();
    auto page2_bitmap = new wxStaticBitmap(m_page2, wxID_ANY, create_scaled_bitmap("max_volumetric_speed_wizard", nullptr, 400), wxDefaultPosition, wxDefaultSize, 0);
    page2_content_sizer->Add(page2_bitmap, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);
    page2_content_sizer->AddSpacer(FromDIP(20));
    add_print_panel_to_page(m_page2, page2_content_sizer);

    auto page2_prev_btn = m_page2->get_prev_btn();
    page2_prev_btn->SetLabel(_L("Re-Calibrate"));
    page2_prev_btn->SetButtonType(ButtonType::Recalibrate);
    page2_prev_btn->Hide();

    auto page2_next_btn = m_page2->get_next_btn();
    page2_next_btn->SetLabel(_L("Next"));
    page2_next_btn->SetButtonType(ButtonType::Next);

    m_all_pages_sizer->Add(m_page2, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 3 : save page
    m_page3 = new CalibrationWizardPage(m_scrolledWindow, false);
    m_page3->set_page_title(_L("Max Volumetric Speed"));
    m_page3->set_page_index(_L("3/3"));

    auto page3_top_sizer = m_page3->get_top_vsizer();
    auto page3_top_description = new wxStaticText(m_page3, wxID_ANY, _L("The calibration model has been printed."), wxDefaultPosition, wxDefaultSize, 0);
    page3_top_description->Wrap(-1);
    page3_top_description->SetFont(::Label::Body_14);
    page3_top_sizer->Add(page3_top_description, 0, wxALL, 0);
    page3_top_sizer->AddSpacer(FromDIP(20));

    auto page3_content_sizer = m_page3->get_content_vsizer();
    auto page3_bitmap = new wxStaticBitmap(m_page3, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    page3_bitmap->SetMinSize(wxSize(800, 600)); // todo modify
    page3_bitmap->SetBackgroundColour(*wxBLACK); // todo modify
    page3_content_sizer->Add(page3_bitmap, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);

    page3_content_sizer->AddSpacer(FromDIP(20));

    auto value_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto value_text = new wxStaticText(m_page3, wxID_ANY, _L("Input Value"), wxDefaultPosition, wxDefaultSize, 0);
    value_text->Wrap(-1);
    value_text->SetFont(::Label::Body_14);
    m_optimal_max_speed = new TextInput(m_page3, wxEmptyString, "", "", wxDefaultPosition, CALIBRATION_OPTIMAL_INPUT_SIZE, 0);
    value_sizer->Add(value_text, 0, wxALIGN_CENTER_VERTICAL, 0);
    value_sizer->Add(FromDIP(10), 0, 0, wxEXPAND, 0);
    value_sizer->Add(m_optimal_max_speed, 0);
    page3_content_sizer->Add(value_sizer, 0, wxALIGN_CENTER);

    page3_content_sizer->AddSpacer(FromDIP(20));

    auto page3_prev_btn = m_page3->get_prev_btn();
    page3_prev_btn->Hide();

    auto page3_next_btn = m_page3->get_next_btn();
    page3_next_btn->SetLabel(_L("Save"));
    page3_next_btn->SetButtonType(ButtonType::Save);

    m_all_pages_sizer->Add(m_page3, 1, wxEXPAND | wxALL, FromDIP(25));

    // link page
    m_page1->chain(m_page2)->chain(m_page3);

    m_first_page = m_page1;
    m_curr_page = m_page1;
    show_page(m_curr_page);
}

bool MaxVolumetricSpeedWizard::start_calibration(std::vector<int> tray_ids)
{
    Calib_Params params;
    m_from_value->GetTextCtrl()->GetValue().ToDouble(&params.start);
    m_to_value->GetTextCtrl()->GetValue().ToDouble(&params.end);
    m_step->GetTextCtrl()->GetValue().ToDouble(&params.step);
    params.mode = CalibMode::Calib_Vol_speed_Tower;

    if (params.start <= 0 || params.step <= 0 || params.end < (params.start + params.step)) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nFrom > 0 \Step >= 0\nTo > From + Step"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    CalibInfo calib_info;
    calib_info.params      = params;
    calib_info.dev_id      = curr_obj->dev_id;
    calib_info.select_ams  = "[" + std::to_string(tray_ids[0]) + "]";
    calib_info.process_bar = m_send_progress_bar;
    calib_info.bed_type       = BedType(m_comboBox_bed_type->GetSelection() + btDefault + 1);
    calib_info.printer_prest  = m_printer_preset;
    calib_info.filament_prest = m_filament_preset;
    calib_info.print_prest    = m_print_preset;

    std::string error_message;
    CalibUtils::calib_max_vol_speed(calib_info, error_message);
    show_send_progress_bar(true);
    return true;
}

bool MaxVolumetricSpeedWizard::save_calibration_result()
{
    if (m_optimal_max_speed->GetTextCtrl()->GetValue().IsEmpty())
    {
        MessageDialog msg_dlg(nullptr, _L("Input a value."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    double max_volumetric_speed;
    m_optimal_max_speed->GetTextCtrl()->GetValue().ToDouble(&max_volumetric_speed);
    return save_presets("filament_max_volumetric_speed", new ConfigOptionFloats{ max_volumetric_speed });
}

bool MaxVolumetricSpeedWizard::recommend_input_value()
{
    if (!CalibrationWizard::recommend_input_value()) {
        m_from_value->GetTextCtrl()->SetValue(wxEmptyString);
        m_to_value->GetTextCtrl()->SetValue(wxEmptyString);
        return false;
    }
    else {
        m_from_value->GetTextCtrl()->SetValue("5");
        m_to_value->GetTextCtrl()->SetValue("20");
        return true;
    }
}

TemperatureWizard::TemperatureWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, id, pos, size, style)
{
    create_pages();
}

void TemperatureWizard::create_pages()
{
    // page 1 : preset page
    m_page1 = new CalibrationWizardPage(m_scrolledWindow, true);
    m_page1->set_page_title(_L("Temperature Calibration"));
    m_page1->set_page_index(_L("1/3"));

    auto page1_content_sizer = m_page1->get_content_vsizer();

    add_presets_panel_to_page(m_page1, page1_content_sizer);

    //auto description1 = new wxStaticText(m_page1, wxID_ANY, _L("Temp tower is a straightforward test. The temp tower is a vertical tower with multiple blocks, each printed at a different temperature. Once the print is complete, we can examine each block of the tower and determine the optimal temperature for the filament.\n\nBy click the \"Calibrate\" button, the temp tower will be sent to your printer and start printing. The printing job may take about 50 minutes."), wxDefaultPosition, wxDefaultSize, 0);
    //description1->Wrap(500);
    //description1->SetFont(::Label::Body_14);
    //description1->SetMinSize({ FromDIP(500), -1 });
    //page1_content_sizer->Add(description1, 0, wxALL | wxEXPAND, 0);

    add_send_progress_to_page(m_page1, m_page1->get_btn_hsizer());

    m_from_text->SetLabel(_L("From Temp"));
    m_to_text->SetLabel(_L("To Temp"));
    m_step->Enable(false);

    auto page1_prev_btn = m_page1->get_prev_btn();
    page1_prev_btn->Hide();

    auto page1_next_btn = m_page1->get_next_btn();
    page1_next_btn->SetLabel(_L("Calibrate"));
    page1_next_btn->SetButtonType(ButtonType::Calibrate);

    m_all_pages_sizer->Add(m_page1, 1, wxEXPAND | wxALL, FromDIP(25));


    // page 2 : print page
    m_page2 = new CalibrationWizardPage(m_scrolledWindow, false);
    m_page2->set_page_title(_L("Temperature Calibration"));
    m_page2->set_page_index(_L("2/3"));

    auto page2_content_sizer = m_page2->get_content_vsizer();
    auto page2_picture_description = new wxStaticBitmap(m_page2, wxID_ANY, create_scaled_bitmap("temperature_wizard1", nullptr, 400), wxDefaultPosition, wxDefaultSize, 0);
    page2_content_sizer->Add(page2_picture_description, 0, wxALIGN_CENTER, 0);
    add_print_panel_to_page(m_page2, page2_content_sizer);

    auto page2_prev_btn = m_page2->get_prev_btn();
    page2_prev_btn->SetLabel(_L("Re-Calibrate"));
    page2_prev_btn->SetButtonType(ButtonType::Recalibrate);
    page2_prev_btn->Hide();

    auto page2_next_btn = m_page2->get_next_btn();
    page2_next_btn->SetLabel(_L("Next"));
    page2_next_btn->SetButtonType(ButtonType::Next);

    m_all_pages_sizer->Add(m_page2, 1, wxEXPAND | wxALL, FromDIP(25));


    // page 3 : save page
    m_page3 = new CalibrationWizardPage(m_scrolledWindow, false);
    m_page3->set_page_title(_L("Temperature Calibration"));
    m_page3->set_page_index(_L("3/3"));

    auto page3_content_sizer = m_page3->get_content_vsizer();
    auto page3_picture_description = new wxStaticBitmap(m_page3, wxID_ANY, create_scaled_bitmap("temperature_wizard2", nullptr, 400), wxDefaultPosition, wxDefaultSize, 0);
    page3_content_sizer->Add(page3_picture_description, 0, wxALIGN_CENTER, 0);

    auto description = new wxStaticText(m_page3, wxID_ANY, _L("The calibration model has been printed.\nPlease find the optimal temperature is the one that produces the highest quality print with the least amount of issues, such as stringing, layer adhesion, warping (overhang), and bridging."), wxDefaultPosition, wxDefaultSize, 0);
    description->SetFont(::Label::Body_14);
    description->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page3_content_sizer->Add(description, 0, wxALIGN_CENTER, 0);

    page3_content_sizer->Add(0, FromDIP(20), 0, wxEXPAND, 0);

    wxBoxSizer* optimal_temp_szier;
    optimal_temp_szier = new wxBoxSizer(wxHORIZONTAL);

    auto optimal_temp_text = new wxStaticText(m_page3, wxID_ANY, _L("Optimal Temp"), wxDefaultPosition, wxDefaultSize, 0);
    optimal_temp_text->Wrap(-1);
    optimal_temp_text->SetFont(::Label::Body_14);
    optimal_temp_szier->Add(optimal_temp_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, 0);

    optimal_temp_szier->Add(FromDIP(10), 0, 0, wxEXPAND, 0);

    m_optimal_temp = new TextInput(m_page3, wxEmptyString, _L("\u2103"), "", wxDefaultPosition, CALIBRATION_OPTIMAL_INPUT_SIZE, 0);
    optimal_temp_szier->Add(m_optimal_temp, 0, wxALL, 0);

    page3_content_sizer->Add(optimal_temp_szier, 0, wxALIGN_CENTER, 0);

    page3_content_sizer->AddSpacer(FromDIP(20));

    auto page3_prev_btn = m_page3->get_prev_btn();
    page3_prev_btn->Hide();

    auto page3_next_btn = m_page3->get_next_btn();
    page3_next_btn->SetLabel(_L("Save"));
    page3_next_btn->SetButtonType(ButtonType::Save);

    m_all_pages_sizer->Add(m_page3, 1, wxEXPAND | wxALL, FromDIP(25));
    

    // link pages
    m_page1->chain(m_page2)->chain(m_page3);

    m_first_page = m_page1;
    m_curr_page = m_page1;
    show_page(m_curr_page);
}

bool TemperatureWizard::start_calibration(std::vector<int> tray_ids)
{
    Calib_Params params;
    m_from_value->GetTextCtrl()->GetValue().ToDouble(&params.start);
    m_to_value->GetTextCtrl()->GetValue().ToDouble(&params.end);
    params.mode = CalibMode::Calib_Temp_Tower;

    if (params.start < 180 || params.end > 350 || params.end < (params.start + 5) || (params.end - params.start) >= 120) { // todo
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nFrom temp: >= 180\nTo temp: <= 350\nFrom temp <= To temp - Step\n From temp - To temp < 120"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    CalibInfo calib_info;
    calib_info.params = params;
    calib_info.dev_id = curr_obj->dev_id;
    calib_info.select_ams = "[" + std::to_string(tray_ids[0]) + "]";
    calib_info.process_bar = m_send_progress_bar;
    calib_info.bed_type = BedType(m_comboBox_bed_type->GetSelection() + btDefault + 1);
    calib_info.printer_prest  = m_printer_preset;
    calib_info.filament_prest = m_filament_preset;
    calib_info.print_prest    = m_print_preset;

    std::string error_message;
    CalibUtils::calib_temptue(calib_info, error_message);
    show_send_progress_bar(true);
    return true;
}

bool TemperatureWizard::save_calibration_result()
{
    if (m_optimal_temp->GetTextCtrl()->GetValue().IsEmpty()) // todo need a valid range
    {
        MessageDialog msg_dlg(nullptr, _L("Input an optiaml temperature."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    int temp = stoi(m_optimal_temp->GetTextCtrl()->GetValue().ToStdString());
    return save_presets("nozzle_temperature", new ConfigOptionInts(1, temp));
}

bool TemperatureWizard::recommend_input_value()
{
    if (!CalibrationWizard::recommend_input_value()) {
        m_from_value->GetTextCtrl()->SetValue(wxEmptyString);
        m_to_value->GetTextCtrl()->SetValue(wxEmptyString);
        return false;
    }
    else {
        wxString filament_name = m_filament_preset->alias;

        int start, end;
        if (filament_name.Contains("ABS") || filament_name.Contains("ASA")) {//todo supplement
            start = 230;
            end = 270;
        }
        else if (filament_name.Contains("PETG")) {
            start = 230;
            end = 260;
        }
        else if (filament_name.Contains("TPU")) {
            start = 210;
            end = 240;
        }
        else if (filament_name.Contains("PA-CF")) {
            start = 280;
            end = 320;
        }
        else if (filament_name.Contains("PET-CF")) {
            start = 280;
            end = 320;
        }
        else if (filament_name.Contains("PLA")) {
            start = 190;
            end = 230;
        }
        else {
            start = 190;
            end = 230;
        }
        m_from_value->GetTextCtrl()->SetValue(std::to_string(start));
        m_to_value->GetTextCtrl()->SetValue(std::to_string(end));

        return true;
    }
}

}}