#include "CalibrationWizard.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "../../libslic3r/Calib.hpp"
#include "Tabbook.hpp"
#include "MainFrame.hpp"

namespace Slic3r { namespace GUI {

#define CALIBRATION_DEBUG

#define PRESET_GAP                         FromDIP(25)
#define CALIBRATION_COMBOX_SIZE            wxSize(FromDIP(500), FromDIP(24))
#define CALIBRATION_FILAMENT_COMBOX_SIZE   wxSize(FromDIP(250), FromDIP(24))
#define CALIBRATION_OPTIMAL_INPUT_SIZE     wxSize(FromDIP(300), FromDIP(24))
#define CALIBRATION_FROM_TO_INPUT_SIZE     wxSize(FromDIP(160), FromDIP(24))
#define CALIBRATION_FGSIZER_HGAP           FromDIP(50)
#define CALIBRATION_TEXT_MAX_LENGTH        FromDIP(90) + CALIBRATION_FGSIZER_HGAP + 2 * CALIBRATION_FILAMENT_COMBOX_SIZE.x
#define CALIBRATION_PROGRESSBAR_LENGTH     FromDIP(600)
static const wxString NA_STR = _L("N/A");

wxDEFINE_EVENT(EVT_CALIBRATION_TRAY_SELECTION_CHANGED, SimpleEvent);
wxDEFINE_EVENT(EVT_CALIBRATION_NOTIFY_CHANGE_PAGES, SimpleEvent);
wxDEFINE_EVENT(EVT_CALIBRATION_TAB_CHANGED, wxCommandEvent);

static bool is_high_end_type(MachineObject* obj) {
    if (obj) {
        if (obj->printer_type == "BL-P001" || obj->printer_type == "BL-P002")
            return true;
        else if (obj->printer_type == "C11" || obj->printer_type == "C12")
            return false;
    }

    return false;
}

static bool validate_input_flow_ratio(wxString flow_ratio, float* output_value) {
    float default_flow_ratio = 1.0f;

    if (flow_ratio.IsEmpty()) {
        *output_value = default_flow_ratio;
        MessageDialog msg_dlg(nullptr, _L("Please input a valid value (0.0 < flow ratio < 0.2)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    double flow_ratio_value = 0.0;
    try {
        flow_ratio.ToDouble(&flow_ratio_value);
    }
    catch (...) {
        ;
    }

    if (flow_ratio_value <= 0.0 || flow_ratio_value >= 2.0) {
        *output_value = default_flow_ratio;
        MessageDialog msg_dlg(nullptr, _L("Please input a valid value (0.0 < flow ratio < 0.2)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    *output_value = flow_ratio_value;
    return true;
}

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

wxString get_calibration_wiki_page(CalibMode cali_mode)
{
    switch (cali_mode) {
    case CalibMode::Calib_PA_Line:
        return wxString("https://wiki.bambulab.com/en/software/bambu-studio/calibration_pa");
    case CalibMode::Calib_Flow_Rate:
        return wxString("https://wiki.bambulab.com/en/software/bambu-studio/calibration_flow_rate");
    case CalibMode::Calib_Vol_speed_Tower:
        return wxString("https://wiki.bambulab.com/en/software/bambu-studio/calibration_volumetric");
    case CalibMode::Calib_Temp_Tower:
        return wxString("https://wiki.bambulab.com/en/software/bambu-studio/calibration_temperature");
    case CalibMode::Calib_Retraction_tower:
        return wxString("https://wiki.bambulab.com/en/software/bambu-studio/calibration_retraction");
    default:
        return "";
    }
}

FilamentComboBox::FilamentComboBox(wxWindow* parent, const wxPoint& pos, const wxSize& size)
    : wxPanel(parent, wxID_ANY, pos, size, wxTAB_TRAVERSAL)
{
    wxBoxSizer* main_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_comboBox = new CalibrateFilamentComboBox(this);
    m_comboBox->SetSize(CALIBRATION_FILAMENT_COMBOX_SIZE);
    m_comboBox->SetMinSize(CALIBRATION_FILAMENT_COMBOX_SIZE);
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
    m_comboBox->load_tray(tray);

    m_tray_id = id;
    m_tray_name = m_comboBox->get_tray_name();
    m_is_bbl_filamnet = MachineObject::is_bbl_filament(m_comboBox->get_tag_uid());
    Enable(m_comboBox->is_tray_exist());
    if (m_comboBox->is_tray_exist()) {
        if (!m_comboBox->is_compatible_with_printer())
            SetValue(false);
        if (m_radioBox)
            m_radioBox->Enable(m_comboBox->is_compatible_with_printer());
        if (m_checkBox)
            m_checkBox->Enable(m_comboBox->is_compatible_with_printer());
    }
}

void FilamentComboBox::update_from_preset() { m_comboBox->update(); }

bool FilamentComboBox::Show(bool show)
{
    bool result = true;
    if (m_radioBox && m_mode == FSMRadioMode)
        result = result && m_radioBox->Show(show);
    if (m_checkBox && m_mode == FSMCheckBoxMode)
        result = result && m_checkBox->Show(show);
    result = result && wxPanel::Show(show);
    return result;
}

bool FilamentComboBox::Enable(bool enable) {
    if (!enable)
        SetValue(false);
    
    bool result = true;
    if (m_radioBox)
        result = result && m_radioBox->Enable(enable);
    if (m_checkBox)
        result = result && m_checkBox->Enable(enable);
    result = result && wxPanel::Enable(enable);
    return result;
}

void FilamentComboBox::SetValue(bool value, bool send_event) {
    if (m_radioBox) {
        if (value == m_radioBox->GetValue()) {
            if (m_checkBox) {
                if (value == m_checkBox->GetValue())
                    return;
            }
            else {
                return;
            }
        }
    }
    if (m_radioBox)
        m_radioBox->SetValue(value);
    if (m_checkBox)
        m_checkBox->SetValue(value);
    if (send_event) {
        SimpleEvent e(EVT_CALIBRATION_TRAY_SELECTION_CHANGED);
        e.ResumePropagation(wxEVENT_PROPAGATE_MAX);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(e);
    }
}

CalibrationWizard::CalibrationWizard(wxWindow* parent, CalibMode mode, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style) 
    , m_mode(mode)
{
    m_wiki_url = get_calibration_wiki_page(m_mode);

    SetBackgroundColour(wxColour(0xEEEEEE));

    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    m_scrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxVSCROLL);
    m_scrolledWindow->SetScrollRate(5, 5);
    m_scrolledWindow->SetBackgroundColour(*wxWHITE);

    m_all_pages_sizer = new wxBoxSizer(wxVERTICAL);

    m_scrolledWindow->SetSizer(m_all_pages_sizer);
    m_scrolledWindow->Layout();
    m_all_pages_sizer->Fit(m_scrolledWindow);

    main_sizer->Add(m_scrolledWindow, 1, wxEXPAND | wxALL, FromDIP(10));

    this->SetSizer(main_sizer);
    this->Layout();
    main_sizer->Fit(this);

    init_bitmaps();
    init_printer_calib_info_from_appconfig();

    Bind(EVT_CALIBRATIONPAGE_PREV, &CalibrationWizard::on_click_btn_prev, this);
    Bind(EVT_CALIBRATIONPAGE_NEXT, &CalibrationWizard::on_click_btn_next, this);
    Bind(EVT_CALIBRATION_TRAY_SELECTION_CHANGED, &CalibrationWizard::on_select_tray, this);
    Bind(EVT_SHOW_ERROR_INFO, [this](auto& e) {show_send_failed_info(true); });

#ifdef CALIBRATION_DEBUG
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

CalibrationWizardPage* CalibrationWizard::create_presets_page(bool need_custom_range) {
    auto page = new CalibrationWizardPage(m_scrolledWindow);
    page->set_page_type(PageType::Preset);
    page->set_highlight_step_text(PageType::Preset);

    auto page_content_sizer = page->get_content_vsizer();

    m_presets_panel = new wxPanel(page);
    page_content_sizer->Add(m_presets_panel, 0, wxEXPAND, 0);
    auto panel_sizer = new wxBoxSizer(wxVERTICAL);
    m_presets_panel->SetSizer(panel_sizer);

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
    filament_list_tips->Hide();
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

    m_filaments_incompatible_tips = new wxStaticText(m_presets_panel, wxID_ANY, _L(""));
    m_filaments_incompatible_tips->SetFont(Label::Body_13);
    m_filaments_incompatible_tips->SetForegroundColour(wxColour(230, 92, 92));
    m_filaments_incompatible_tips->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    m_filaments_incompatible_tips->Hide();
    panel_sizer->Add(m_filaments_incompatible_tips, 0, wxEXPAND);

    m_bed_type_incompatible_tips = new wxStaticText(m_presets_panel, wxID_ANY, _L(""));
    m_bed_type_incompatible_tips->SetFont(Label::Body_13);
    m_bed_type_incompatible_tips->SetForegroundColour(wxColour(230, 92, 92));
    m_bed_type_incompatible_tips->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    m_bed_type_incompatible_tips->Hide();
    panel_sizer->Add(m_bed_type_incompatible_tips, 0, wxEXPAND);

    panel_sizer->AddSpacer(PRESET_GAP);

    if (need_custom_range) {
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

        panel_sizer->AddSpacer(PRESET_GAP);
    }
    else {
        m_from_text = nullptr;
        m_to_text = nullptr;
        m_from_value = nullptr;
        m_to_value = nullptr;
        m_step_text = nullptr;
        m_step = nullptr;
    }

    auto printing_param_panel = new wxPanel(m_presets_panel);
    printing_param_panel->SetBackgroundColour(wxColour(238, 238, 238));
    printing_param_panel->SetMinSize(wxSize(CALIBRATION_TEXT_MAX_LENGTH * 1.7f, -1));
    auto printing_param_sizer = new wxBoxSizer(wxVERTICAL);
    printing_param_panel->SetSizer(printing_param_sizer);

    printing_param_sizer->AddSpacer(FromDIP(10));

    auto preset_panel_tips = new wxStaticText(printing_param_panel, wxID_ANY, _L("A test model will be printed. Please clear the build plate and place it back to the hot bed before calibration."));
    preset_panel_tips->SetFont(Label::Body_14);
    preset_panel_tips->Wrap(CALIBRATION_TEXT_MAX_LENGTH * 1.5f);
    printing_param_sizer->Add(preset_panel_tips, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    printing_param_sizer->AddSpacer(FromDIP(10));

    auto info_sizer = new wxFlexGridSizer(0, 3, 0, FromDIP(10));
    info_sizer->SetFlexibleDirection(wxBOTH);
    info_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    auto nozzle_temp_sizer = new wxBoxSizer(wxVERTICAL);
    auto nozzle_temp_text = new wxStaticText(printing_param_panel, wxID_ANY, _L("Nozzle temperature"));
    nozzle_temp_text->SetFont(Label::Body_12);
    m_nozzle_temp = new TextInput(printing_param_panel, wxEmptyString, _L("\u2103"), "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, wxTE_READONLY);
    m_nozzle_temp->SetBorderWidth(0);
    nozzle_temp_sizer->Add(nozzle_temp_text, 0, wxALIGN_LEFT);
    nozzle_temp_sizer->Add(m_nozzle_temp, 0, wxEXPAND);
    nozzle_temp_text->Hide();
    m_nozzle_temp->Hide();

    auto bed_temp_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto printing_param_text = new wxStaticText(printing_param_panel, wxID_ANY, _L("Printing Parameters"));
    printing_param_text->SetFont(Label::Head_12);
    printing_param_text->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    bed_temp_sizer->Add(printing_param_text, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(20));
    auto bed_temp_text = new wxStaticText(printing_param_panel, wxID_ANY, _L("Bed temperature"));
    bed_temp_text->SetFont(Label::Body_12);
    m_bed_temp = new TextInput(printing_param_panel, wxEmptyString, _L("\u2103"), "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, wxTE_READONLY);
    m_bed_temp->SetBorderWidth(0);
    bed_temp_sizer->Add(bed_temp_text, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(10));
    bed_temp_sizer->Add(m_bed_temp, 0, wxALIGN_CENTER);

    auto max_flow_sizer = new wxBoxSizer(wxVERTICAL);
    auto max_flow_text = new wxStaticText(printing_param_panel, wxID_ANY, _L("Max volumetric speed"));
    max_flow_text->SetFont(Label::Body_12);
    m_max_volumetric_speed = new TextInput(printing_param_panel, wxEmptyString, _L("mm\u00B3"), "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, wxTE_READONLY);
    m_max_volumetric_speed->SetBorderWidth(0);
    max_flow_sizer->Add(max_flow_text, 0, wxALIGN_LEFT);
    max_flow_sizer->Add(m_max_volumetric_speed, 0, wxEXPAND);
    max_flow_text->Hide();
    m_max_volumetric_speed->Hide();

    m_nozzle_temp->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [](auto&) {});
    m_bed_temp->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [](auto&) {});
    m_max_volumetric_speed->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [](auto&) {});

    info_sizer->Add(nozzle_temp_sizer);
    info_sizer->Add(bed_temp_sizer);
    info_sizer->Add(max_flow_sizer);
    printing_param_sizer->Add(info_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    printing_param_sizer->AddSpacer(FromDIP(10));

    panel_sizer->Add(printing_param_panel, 0);

    init_presets_selections();


    auto page_btn_sizer = page->get_btn_hsizer();

    auto page_prev_btn = page->get_prev_btn();
    page_prev_btn->SetLabel(_L("Restart"));
    page_prev_btn->SetButtonType(ButtonType::Restart);

    auto page_next_btn = page->get_next_btn();
    page_next_btn->SetLabel(_L("Calibrate"));
    page_next_btn->SetButtonType(ButtonType::Calibrate);

    // send bar
    m_send_progress_panel = new wxPanel(page);
    m_send_progress_panel->Hide();
    auto send_panel_sizer = new wxBoxSizer(wxVERTICAL);
    m_send_progress_panel->SetSizer(send_panel_sizer);

    m_send_progress_bar = std::shared_ptr<BBLStatusBarSend>(new BBLStatusBarSend(m_send_progress_panel));
    m_send_progress_bar->set_cancel_callback_fina([this]() {
        BOOST_LOG_TRIVIAL(info) << "CalibrationWizard::print_job: enter canceled";
        if (CalibUtils::print_job) {
            if (CalibUtils::print_job->is_running()) {
                BOOST_LOG_TRIVIAL(info) << "calibration_print_job: canceled";
                CalibUtils::print_job->cancel();
            }
            CalibUtils::print_job->join();
        }
        show_send_progress_bar(false);
        });
    send_panel_sizer->Add(m_send_progress_bar->get_panel(), 0);
    page_btn_sizer->Insert(1, m_send_progress_panel, 0, wxALIGN_CENTER, 0);

    // show bind failed info
    m_sw_print_failed_info = new wxScrolledWindow(m_send_progress_panel, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(380), FromDIP(125)), wxVSCROLL);
    m_sw_print_failed_info->SetBackgroundColour(*wxWHITE);
    m_sw_print_failed_info->SetScrollRate(0, 5);
    m_sw_print_failed_info->SetMinSize(wxSize(FromDIP(380), FromDIP(125)));
    m_sw_print_failed_info->SetMaxSize(wxSize(FromDIP(380), FromDIP(125)));
    m_sw_print_failed_info->Hide();
    send_panel_sizer->Add(m_sw_print_failed_info, 0);

    wxBoxSizer* sizer_print_failed_info = new wxBoxSizer(wxVERTICAL);
    m_sw_print_failed_info->SetSizer(sizer_print_failed_info);

    wxBoxSizer* sizer_error_code = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* sizer_error_desc = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer* sizer_extra_info = new wxBoxSizer(wxHORIZONTAL);

    auto st_title_error_code = new wxStaticText(m_sw_print_failed_info, wxID_ANY, _L("Error code"));
    auto st_title_error_code_doc = new wxStaticText(m_sw_print_failed_info, wxID_ANY, ": ");
    m_st_txt_error_code = new Label(m_sw_print_failed_info, wxEmptyString);
    st_title_error_code->SetForegroundColour(0x909090);
    st_title_error_code_doc->SetForegroundColour(0x909090);
    m_st_txt_error_code->SetForegroundColour(0x909090);
    st_title_error_code->SetFont(::Label::Body_13);
    st_title_error_code_doc->SetFont(::Label::Body_13);
    m_st_txt_error_code->SetFont(::Label::Body_13);
    st_title_error_code->SetMinSize(wxSize(FromDIP(74), -1));
    st_title_error_code->SetMaxSize(wxSize(FromDIP(74), -1));
    m_st_txt_error_code->SetMinSize(wxSize(FromDIP(260), -1));
    m_st_txt_error_code->SetMaxSize(wxSize(FromDIP(260), -1));
    sizer_error_code->Add(st_title_error_code, 0, wxALL, 0);
    sizer_error_code->Add(st_title_error_code_doc, 0, wxALL, 0);
    sizer_error_code->Add(m_st_txt_error_code, 0, wxALL, 0);

    auto st_title_error_desc = new wxStaticText(m_sw_print_failed_info, wxID_ANY, _L("Error desc"));
    auto st_title_error_desc_doc = new wxStaticText(m_sw_print_failed_info, wxID_ANY, ": ");
    m_st_txt_error_desc = new Label(m_sw_print_failed_info, wxEmptyString);
    st_title_error_desc->SetForegroundColour(0x909090);
    st_title_error_desc_doc->SetForegroundColour(0x909090);
    m_st_txt_error_desc->SetForegroundColour(0x909090);
    st_title_error_desc->SetFont(::Label::Body_13);
    st_title_error_desc_doc->SetFont(::Label::Body_13);
    m_st_txt_error_desc->SetFont(::Label::Body_13);
    st_title_error_desc->SetMinSize(wxSize(FromDIP(74), -1));
    st_title_error_desc->SetMaxSize(wxSize(FromDIP(74), -1));
    m_st_txt_error_desc->SetMinSize(wxSize(FromDIP(260), -1));
    m_st_txt_error_desc->SetMaxSize(wxSize(FromDIP(260), -1));
    sizer_error_desc->Add(st_title_error_desc, 0, wxALL, 0);
    sizer_error_desc->Add(st_title_error_desc_doc, 0, wxALL, 0);
    sizer_error_desc->Add(m_st_txt_error_desc, 0, wxALL, 0);

    auto st_title_extra_info = new wxStaticText(m_sw_print_failed_info, wxID_ANY, _L("Extra info"));
    auto st_title_extra_info_doc = new wxStaticText(m_sw_print_failed_info, wxID_ANY, ": ");
    m_st_txt_extra_info = new Label(m_sw_print_failed_info, wxEmptyString);
    st_title_extra_info->SetForegroundColour(0x909090);
    st_title_extra_info_doc->SetForegroundColour(0x909090);
    m_st_txt_extra_info->SetForegroundColour(0x909090);
    st_title_extra_info->SetFont(::Label::Body_13);
    st_title_extra_info_doc->SetFont(::Label::Body_13);
    m_st_txt_extra_info->SetFont(::Label::Body_13);
    st_title_extra_info->SetMinSize(wxSize(FromDIP(74), -1));
    st_title_extra_info->SetMaxSize(wxSize(FromDIP(74), -1));
    m_st_txt_extra_info->SetMinSize(wxSize(FromDIP(260), -1));
    m_st_txt_extra_info->SetMaxSize(wxSize(FromDIP(260), -1));
    sizer_extra_info->Add(st_title_extra_info, 0, wxALL, 0);
    sizer_extra_info->Add(st_title_extra_info_doc, 0, wxALL, 0);
    sizer_extra_info->Add(m_st_txt_extra_info, 0, wxALL, 0);

    sizer_print_failed_info->Add(sizer_error_code, 0, wxLEFT, 5);
    sizer_print_failed_info->Add(0, 0, 0, wxTOP, FromDIP(3));
    sizer_print_failed_info->Add(sizer_error_desc, 0, wxLEFT, 5);
    sizer_print_failed_info->Add(0, 0, 0, wxTOP, FromDIP(3));
    sizer_print_failed_info->Add(sizer_extra_info, 0, wxLEFT, 5);
    // send bar

    m_presets_panel->Layout();
    panel_sizer->Fit(m_presets_panel);

    m_comboBox_nozzle_dia->Bind(wxEVT_COMBOBOX, &CalibrationWizard::on_select_nozzle, this);
    Bind(EVT_CALIBRATION_TAB_CHANGED, &CalibrationWizard::on_select_nozzle, this);
    m_comboBox_bed_type->Bind(wxEVT_COMBOBOX, &CalibrationWizard::on_select_bed_type, this);
    m_ams_sync_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
        on_update_ams_filament();
        });
    m_ams_radiobox->Bind(wxEVT_RADIOBUTTON, &CalibrationWizard::on_choose_ams, this);
    m_ext_spool_radiobox->Bind(wxEVT_RADIOBUTTON, &CalibrationWizard::on_choose_ext_spool, this);

    return page;
}

CalibrationWizardPage* CalibrationWizard::create_print_page() {
    auto page = new CalibrationWizardPage(m_scrolledWindow);
    page->set_page_type(PageType::Calibration);
    page->set_highlight_step_text(PageType::Calibration);
    auto sizer = page->get_content_vsizer();

    m_print_picture = new wxStaticBitmap(page, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    sizer->Add(m_print_picture, 0, wxALIGN_CENTER, 0);
    sizer->AddSpacer(FromDIP(20));

    m_print_panel = new wxPanel(page);
    m_print_panel->SetSize({ CALIBRATION_PROGRESSBAR_LENGTH, -1 });
    m_print_panel->SetMinSize({ CALIBRATION_PROGRESSBAR_LENGTH, -1 });

    page->get_prev_btn()->Hide();

    m_btn_next = page->get_next_btn();
    m_btn_next->SetLabel(_L("Next"));
    m_btn_next->SetButtonType(ButtonType::Next);

    m_btn_recali = page->get_prev_btn();
    m_btn_recali->SetLabel(_L("Re-Calibrate"));
    m_btn_recali->SetButtonType(ButtonType::Recalibrate);

    sizer->Add(m_print_panel, 0, wxALIGN_CENTER, 0);
    auto panel_sizer = new wxBoxSizer(wxVERTICAL);


    m_staticText_profile_value = new wxStaticText(m_print_panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT | wxST_ELLIPSIZE_END);
    m_staticText_profile_value->Wrap(-1);
    m_staticText_profile_value->SetFont(Label::Body_14);
    m_staticText_profile_value->SetForegroundColour(0x6B6B6B);


    m_printing_stage_value = new wxStaticText(m_print_panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT | wxST_ELLIPSIZE_END);
    m_printing_stage_value->SetFont(Label::Body_14);
    m_printing_stage_value->Wrap(-1);
    m_printing_stage_value->SetForegroundColour(wxColour(0, 174, 66));


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

    m_button_abort = new ScalableButton(panel_progressbar, wxID_ANY, "print_control_stop", wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true);
    m_button_abort->SetToolTip(_L("Stop"));

    progressbar_sizer->Add(m_print_gauge_progress, 1, wxALIGN_CENTER_VERTICAL, 0);
    progressbar_sizer->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(18));
    progressbar_sizer->Add(m_button_pause_resume, 0, wxALL, FromDIP(5));
    progressbar_sizer->Add(0, 0, 0, wxEXPAND | wxLEFT, FromDIP(18));
    progressbar_sizer->Add(m_button_abort, 0, wxALL, FromDIP(5));

    panel_progressbar->SetSizer(progressbar_sizer);

    panel_sizer->AddSpacer(FromDIP(15));
    panel_sizer->Add(m_staticText_profile_value, 0, wxEXPAND | wxTOP, FromDIP(5));
    panel_sizer->Add(m_printing_stage_value, 0, wxEXPAND | wxTOP, FromDIP(5));
    panel_sizer->Add(panel_text, 0, wxEXPAND, 0);
    panel_sizer->Add(panel_progressbar, 0, wxEXPAND, 0);
    panel_sizer->AddSpacer(FromDIP(15));

    m_print_panel->SetSizer(panel_sizer);

    m_print_panel->SetSizer(panel_sizer);
    m_print_panel->Layout();
    panel_sizer->Fit(m_print_panel);


    m_button_pause_resume->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        if (m_button_pause_resume->GetToolTipText() == _L("Pause")) {
            m_button_pause_resume->SetBitmap(m_bitmap_pause_hover.bmp());
        }

        if (m_button_pause_resume->GetToolTipText() == _L("Resume")) {
            m_button_pause_resume->SetBitmap(m_bitmap_resume_hover.bmp());
        }
        });
    m_button_pause_resume->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {
        if (m_button_pause_resume->GetToolTipText() == _L("Pause")) {
            m_button_pause_resume->SetBitmap(m_bitmap_pause.bmp());
        }

        if (m_button_pause_resume->GetToolTipText() == _L("Resume")) {
            m_button_pause_resume->SetBitmap(m_bitmap_resume.bmp());
        }
        });
    m_button_pause_resume->Bind(wxEVT_BUTTON, &CalibrationWizard::on_subtask_pause_resume, this);
    m_button_abort->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        m_button_abort->SetBitmap(m_bitmap_abort_hover.bmp());
        });
    m_button_abort->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {
        m_button_abort->SetBitmap(m_bitmap_abort.bmp()); }
    );
    m_button_abort->Bind(wxEVT_BUTTON, &CalibrationWizard::on_subtask_abort, this);

    return page;
}

CalibrationWizardPage* CalibrationWizard::create_save_page() {
    auto page = new CalibrationWizardPage(m_scrolledWindow);
    page->set_page_type(PageType::Save);
    page->set_highlight_step_text(PageType::Save);
    auto sizer = page->get_content_vsizer();

    m_save_panel = new wxPanel(page);
    sizer->Add(m_save_panel, 0, wxALIGN_CENTER, 0);
    auto panel_sizer = new wxBoxSizer(wxVERTICAL);
    m_save_panel->SetSizer(panel_sizer);

    create_save_panel_content(panel_sizer);

    auto page_prev_btn = page->get_prev_btn();
    page_prev_btn->SetLabel(_L("Restart"));
    page_prev_btn->SetButtonType(ButtonType::Restart);

    auto page_next_btn = page->get_next_btn();
    page_next_btn->SetLabel(_L("Save"));
    page_next_btn->SetButtonType(ButtonType::Save);

    return page;
}

void CalibrationWizard::update_print_error_info(int code, std::string msg, std::string extra)
{
    m_print_error_code = code;
    m_print_error_msg = msg;
    m_print_error_extra = extra;
}

void CalibrationWizard::show_send_failed_info(bool show, int code, wxString description, wxString extra) {
    if (show) {
        if (!m_sw_print_failed_info->IsShown()) {
            m_sw_print_failed_info->Show(true);

            m_st_txt_error_code->SetLabelText(wxString::Format("%d", m_print_error_code));
            m_st_txt_error_desc->SetLabelText(wxGetApp().filter_string(m_print_error_msg));
            m_st_txt_extra_info->SetLabelText(wxGetApp().filter_string(m_print_error_extra));

            m_st_txt_error_code->Wrap(FromDIP(260));
            m_st_txt_error_desc->Wrap(FromDIP(260));
            m_st_txt_extra_info->Wrap(FromDIP(260));
        }
        else {
            m_sw_print_failed_info->Show(false);
        }
        Layout();
        Fit();
    }
    else {
        if (!m_sw_print_failed_info->IsShown()) { return; }
        m_sw_print_failed_info->Show(false);
        m_st_txt_error_code->SetLabelText(wxEmptyString);
        m_st_txt_error_desc->SetLabelText(wxEmptyString);
        m_st_txt_extra_info->SetLabelText(wxEmptyString);
        Layout();
        Fit();
    }
}


void CalibrationWizard::show_send_progress_bar(bool show)
{
    m_send_progress_panel->Show(show);

    get_curr_page()->get_prev_btn()->Show(!show);
    get_curr_page()->get_next_btn()->Show(!show);

    Layout();
}

void CalibrationWizard::on_choose_ams(wxCommandEvent& event) {
    m_filament_list_panel->Show();
    m_virtual_panel->Hide();
    m_filament_from_ext_spool = false;
    m_virtual_tray_comboBox->SetValue(false);

    Layout();
    Refresh();
}

void CalibrationWizard::on_choose_ext_spool(wxCommandEvent& event) {
    m_virtual_panel->Show();
    m_filament_list_panel->Hide();
    m_filament_from_ext_spool = true;
    for (int i = 0; i < m_filament_comboBox_list.size(); i++) {
        m_filament_comboBox_list[i]->SetValue(false);
    }

    Layout();
    Refresh();
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

void CalibrationWizard::reset_preset_page() {
    m_filament_presets.clear();
    init_presets_selections();
    change_ams_select_mode();
    on_update_ams_filament(false);
    on_switch_ams(m_ams_item_list[0]->m_amsinfo.ams_id);
    for (auto fcb : m_filament_comboBox_list)
        fcb->SetValue(false);
    m_virtual_tray_comboBox->SetValue(false);
}

void CalibrationWizard::on_click_btn_prev(IntEvent& event)
{
    bool recalibration = false;
    ButtonType button_type = static_cast<ButtonType>(event.get_data());
    switch (button_type)
    {
    case Slic3r::GUI::Back:
        show_page(get_curr_page()->get_prev_page());
        break;
    case Slic3r::GUI::Recalibrate: {
        if (!curr_obj ||
            curr_obj->is_system_printing() ||
            curr_obj->is_in_printing()) {
            MessageDialog msg_dlg(nullptr, _L("Is in printing. Please wait for printing to complete"), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }
        MessageDialog msg_dlg(nullptr, _L("It will restart to get the results. Do you confirm to re-calibrate?"), wxEmptyString, wxICON_WARNING | wxYES | wxNO);
        auto answer  = msg_dlg.ShowModal();
        if (answer == wxID_NO)
            return;
        recalibration = true;
    }
    case Slic3r::GUI::Restart:
        if (!recalibration) {
            MessageDialog msg_dlg(nullptr, _L("It will restart to get the results. Do you confirm to restart?"), wxEmptyString, wxICON_WARNING | wxYES | wxNO);
            auto answer  = msg_dlg.ShowModal();
            if (answer == wxID_NO)
                return;
        }

        reset_preset_page();
        if (m_mode == CalibMode::Calib_Flow_Rate) {
            auto flow_rate_wizard = static_cast<FlowRateWizard*>(this);
            flow_rate_wizard->reset_reuse_panels();
        }
        show_page(get_frist_page());
        save_to_printer_calib_info(PageType::Start);
        break;
    }
}

void CalibrationWizard::on_click_btn_next(IntEvent& event)
{
    ButtonType button_type = static_cast<ButtonType>(event.get_data());
    switch (button_type)
    {
    case Slic3r::GUI::Start:
        show_page(get_curr_page()->get_next_page());
        break;
    case Slic3r::GUI::Next:
        set_save_name();
        show_page(get_curr_page()->get_next_page());
        save_to_printer_calib_info(m_curr_page->get_page_type());
        break;
    case Slic3r::GUI::Calibrate: {
        if(!curr_obj){
            MessageDialog msg_dlg(nullptr, _L("No Printer Connected!"), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }

        if (curr_obj->is_connecting() || !curr_obj->is_connected()) {
            MessageDialog msg_dlg(nullptr, _L("Printer is not connected yet."), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }

        if (!m_printer_preset || m_filament_presets.empty() || !m_print_preset) {
            wxString tips;
            if (!m_printer_preset) {
                tips = _L("Please select a printer and nozzle for calibration.");
            }
            else if (!m_print_preset) {
                tips = _L("No print preset");
            }
            else {
                tips = _L("Please select filament for calibration.");
            }
            MessageDialog msg_dlg(nullptr, tips, wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }

        if (curr_obj->is_system_printing() ||
            curr_obj->is_in_printing()) {
            MessageDialog msg_dlg(nullptr, _L("Is in printing. Please wait for printing to complete"), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }

        std::string nozzle_temp_str = m_nozzle_temp->GetTextCtrl()->GetValue().ToStdString();
        std::string bed_temp_str    = m_bed_temp->GetTextCtrl()->GetValue().ToStdString();
        std::string max_volumetric_speed_str = m_max_volumetric_speed->GetTextCtrl()->GetValue().ToStdString();
        if (nozzle_temp_str.empty() || bed_temp_str.empty() || max_volumetric_speed_str.empty()) {
            MessageDialog msg_dlg(nullptr, _L("The printing parameters is empty, please reselect nozzle and plate type."), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }

        std::vector<int> tray_ids = get_selected_tray();
        if (start_calibration(tray_ids)) {
            is_between_start_and_runing = true;
            if (m_mode != CalibMode::Calib_Flow_Rate) {
                save_to_printer_calib_info(PageType::Calibration);
            }
        }
        break;
    }
    case Slic3r::GUI::Save:
        if (!curr_obj ||
            curr_obj->is_system_printing() ||
            curr_obj->is_in_printing()) {
            MessageDialog msg_dlg(nullptr, _L("Is in printing. Please wait for printing to complete"), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }
        if (save_calibration_result()) {
            MessageDialog msg_dlg(nullptr, _L("Saved success."), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            save_to_printer_calib_info(PageType::Finish);
        }
        break;
    default:
        break;
    }
}

void CalibrationWizard::init_printer_calib_info_from_appconfig() {
    std::vector<PrinterCaliInfo> infos = wxGetApp().app_config->get_printer_cali_infos();
    for (int i = 0; i < infos.size(); i++) {
        if (m_printer_calib_infos.find(infos[i].dev_id) == m_printer_calib_infos.end()) {
            m_printer_calib_infos[infos[i].dev_id].dev_id = infos[i].dev_id;
            m_printer_calib_infos[infos[i].dev_id].filament_presets = infos[i].filament_presets;
            m_printer_calib_infos[infos[i].dev_id].mode = infos[i].mode;
            m_printer_calib_infos[infos[i].dev_id].state = infos[i].state;
            //m_printer_calib_infos[infos[i].dev_id].nozzle_dia = infos[i].nozzle_dia;
            //m_printer_calib_infos[infos[i].dev_id].bed_type = infos[i].bed_type;
        }
    }
}

void CalibrationWizard::save_to_printer_calib_info(PageType page_type) {
    if (curr_obj) {
        m_printer_calib_infos[curr_obj->dev_id].dev_id = curr_obj->dev_id;
        m_printer_calib_infos[curr_obj->dev_id].mode = m_mode;
        m_printer_calib_infos[curr_obj->dev_id].state = static_cast<Slic3r::CalibState>(page_type);
        for (auto filament_preset : m_filament_presets) {
            m_printer_calib_infos[curr_obj->dev_id].filament_presets[filament_preset.first] = filament_preset.second->name;
        }
        //m_printer_calib_infos[curr_obj->dev_id].nozzle_dia = stof(m_comboBox_nozzle_dia->GetValue().ToStdString());
        //m_printer_calib_infos[curr_obj->dev_id].bed_type = (int)(m_comboBox_bed_type->GetSelection() + btDefault + 1);
        wxGetApp().app_config->save_printer_cali_infos(m_printer_calib_infos[curr_obj->dev_id]);
    }
}

void CalibrationWizard::update_print_progress()
{
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev || !curr_obj) {
        reset_printing_values();
        return;
    }

    // check valid machine
    if (dev->get_my_machine(curr_obj->dev_id) == nullptr) {
        reset_printing_values();
        return;
    }

    if (curr_obj->is_connecting()) {
        reset_printing_values();
        return;
    }
    else if (!curr_obj->is_connected()) {
        reset_printing_values();
        return;
    }

    m_print_panel->Freeze();

    if (curr_obj->is_support_layer_num) {
        m_staticText_layers->Show();
    }
    else {
        m_staticText_layers->Hide();
    }

    if (curr_obj->print_status == "RUNNING") {
        is_between_start_and_runing = false;
        m_btn_recali->Hide();
        m_print_panel->GetParent()->Layout();
    }

    if (curr_obj->is_system_printing()) {
        reset_printing_values();
    }
    else if (curr_obj->is_in_printing() || curr_obj->print_status == "FINISH" || curr_obj->print_status == "IDLE") {
        if (curr_obj->is_in_prepare() || curr_obj->print_status == "SLICING") {
            reset_printing_values();

            wxString prepare_text;
            if (curr_obj->is_in_prepare())
                prepare_text = wxString::Format(_L("Downloading..."));
            else if (curr_obj->print_status == "SLICING") {
                if (curr_obj->queue_number <= 0) {
                    prepare_text = wxString::Format(_L("Cloud Slicing..."));
                }
                else {
                    prepare_text = wxString::Format(_L("In Cloud Slicing Queue, there are %s tasks ahead."), std::to_string(curr_obj->queue_number));
                }
            }
            else
                prepare_text = wxString::Format(_L("Downloading..."));

            if (curr_obj->gcode_file_prepare_percent >= 0 && curr_obj->gcode_file_prepare_percent <= 100)
                prepare_text += wxString::Format("(%d%%)", curr_obj->gcode_file_prepare_percent);
            m_printing_stage_value->SetLabelText(prepare_text);
            wxString subtask_text = wxString::Format("%s", GUI::from_u8(curr_obj->subtask_name));
            if (curr_obj->get_modeltask() && curr_obj->get_modeltask()->design_id > 0) {
                if (!m_staticText_profile_value->IsShown()) { m_staticText_profile_value->Show(); }
                m_staticText_profile_value->SetLabelText(wxString::FromUTF8(curr_obj->get_modeltask()->profile_name));
            }
            else {
                m_staticText_profile_value->SetLabelText(wxEmptyString);
                m_staticText_profile_value->Hide();
            }
        }
        else {
            if (curr_obj->can_resume()) {
                m_button_pause_resume->SetBitmap(m_bitmap_resume.bmp());
                if (m_button_pause_resume->GetToolTipText() != _L("Resume")) { m_button_pause_resume->SetToolTip(_L("Resume")); }
            }
            else {
                m_button_pause_resume->SetBitmap(m_bitmap_pause.bmp());
                if (m_button_pause_resume->GetToolTipText() != _L("Pause")) { m_button_pause_resume->SetToolTip(_L("Pause")); }
            }

            if ((curr_obj->print_status == "FINISH" || curr_obj->print_status == "IDLE")) {
                if (is_between_start_and_runing) {
                    // just entering, fake status
                    reset_printing_values();
                    m_print_panel->Thaw();
                    return;
                }
                else {
                    // true status
                    m_btn_recali->Show();
                    if (curr_obj->print_status == "IDLE")
                        reset_printing_values();

                    if (m_curr_page->get_page_type() == PageType::Calibration || m_curr_page->get_page_type() == PageType::Save)
                    {
                        request_calib_result();
                    }
                    if (curr_obj->print_status == "FINISH")
                    {
                        m_button_abort->Enable(false);
                        m_button_abort->SetBitmap(m_bitmap_abort_disable.bmp());
                        m_button_pause_resume->Enable(false);
                        m_button_pause_resume->SetBitmap(m_bitmap_resume_disable.bmp());
                        m_btn_next->Enable(true);
                    }
                    m_print_panel->GetParent()->Layout();
                }
            }
            else {
                m_button_abort->Enable(true);
                m_button_abort->SetBitmap(m_bitmap_abort.bmp());
                m_button_pause_resume->Enable(true);
                m_btn_next->Enable(false);
            }

            // update printing stage
            m_printing_stage_value->SetLabelText(curr_obj->get_curr_stage());
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

        wxString subtask_text = wxString::Format("%s", GUI::from_u8(curr_obj->subtask_name));

        if (curr_obj->get_modeltask() && curr_obj->get_modeltask()->design_id > 0) {
            if (!m_staticText_profile_value->IsShown()) { m_staticText_profile_value->Show(); }
            m_staticText_profile_value->SetLabelText(wxString::FromUTF8(curr_obj->get_modeltask()->profile_name));
        }
        else {
            m_staticText_profile_value->SetLabelText(wxEmptyString);
            m_staticText_profile_value->Hide();
        }
    }
    else {
        reset_printing_values();
        if (curr_obj->print_status == "FAILED" && !is_between_start_and_runing) {
            m_btn_recali->Show();
            m_printing_stage_value->SetLabelText(_L("FAILED"));
        }
    }

    if (is_between_start_and_runing) {
        m_btn_recali->Hide();
        m_print_panel->GetParent()->Layout();
    }

    check_sync_printer_status();

    m_print_panel->Layout();

    m_print_panel->Thaw();
}

void CalibrationWizard::reset_printing_values()
{
    m_button_pause_resume->Enable(false);
    m_button_pause_resume->SetBitmap(m_bitmap_pause_disable.bmp());

    m_button_abort->Enable(false);
    m_button_abort->SetBitmap(m_bitmap_abort_disable.bmp());

    m_btn_next->Enable(false);

    m_staticText_profile_value->SetLabelText(wxEmptyString);
    m_staticText_profile_value->Hide();
    m_printing_stage_value->SetLabelText("");
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
    MessageDialog msg_dlg(nullptr, _L("Are you sure you want to cancel this calibration? It will return to start page"), wxEmptyString, wxICON_WARNING | wxOK | wxCANCEL);
    if (msg_dlg.ShowModal() == wxID_OK) {
        if (curr_obj) curr_obj->command_task_abort();
        m_btn_recali->Show();
        if (m_mode == CalibMode::Calib_Flow_Rate) {
            auto flow_rate_wizard = static_cast<FlowRateWizard*>(this);
            flow_rate_wizard->reset_reuse_panels();
        }
        show_page(get_frist_page());
        save_to_printer_calib_info(PageType::Start);
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

void CalibrationWizard::set_ams_select_mode(FilamentSelectMode mode) {
    for (int i = 0; i < m_filament_comboBox_list.size(); i++) {
        m_filament_comboBox_list[i]->set_select_mode(mode);
        if (i >= 4)
            m_filament_comboBox_list[i]->GetCheckBox()->Show(false);
    }
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
            for (auto& fcb : m_filament_comboBox_list) {
                if (fcb->GetCheckBox()->GetValue()) {
                    tray_ids.push_back(fcb->get_tray_id());
                }
            }
        }
        else if (get_ams_select_mode() == FilamentSelectMode::FSMRadioMode) {
            for (auto& fcb : m_filament_comboBox_list) {
                if (fcb->GetRadioBox()->GetValue()) {
                    tray_ids.push_back(fcb->get_tray_id());
                }
            }
        }
    }
    return tray_ids;
}

void CalibrationWizard::set_selected_tray(const std::vector<int>& tray_ids)
{
    if (tray_ids.empty())
        return;

    if (tray_ids[0] == VIRTUAL_TRAY_ID) {
        assert(tray_ids.size() == 1);
        m_filament_from_ext_spool = true;
        m_virtual_tray_comboBox->SetValue(true, false);
    }
    else {
        m_filament_from_ext_spool = false;
        for (int tray_id : tray_ids) {
            if (get_ams_select_mode() == FilamentSelectMode::FSMCheckBoxMode) {
                for (auto& fcb : m_filament_comboBox_list) {
                    if (fcb->get_tray_id() == tray_id) {
                        fcb->SetValue(true, false);
                    }
                }
            }
            else if (get_ams_select_mode() == FilamentSelectMode::FSMRadioMode) {
                for (auto& fcb : m_filament_comboBox_list) {
                    if (fcb->get_tray_id() == tray_id) {
                        fcb->SetValue(true, false);
                    }
                }
            }
        }
    }
    recommend_input_value();
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
            for (auto& fcb : m_filament_comboBox_list) {
                if (fcb->GetCheckBox()->GetValue()) {
                    fcb_list.push_back(fcb);
                }
            }
        }
        else if (get_ams_select_mode() == FilamentSelectMode::FSMRadioMode) {
            for (auto& fcb : m_filament_comboBox_list) {
                if (fcb->GetRadioBox()->GetValue()) {
                    fcb_list.push_back(fcb);
                }
            }
        }
    }
    return fcb_list;
}

void CalibrationWizard::init_presets_selections() {
    init_nozzle_selections();
    init_bed_type_selections();
    init_process_selections();
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
        if (is_high_end_type(curr_obj) &&
                (abs(curr_obj->nozzle_diameter - 0.2f) < 1e-3 ||
                abs(curr_obj->nozzle_diameter - 0.4f) < 1e-3 ||
                abs(curr_obj->nozzle_diameter - 0.6f) < 1e-3 ||
                abs(curr_obj->nozzle_diameter - 0.8f) < 1e-3)
            ) {
            m_comboBox_nozzle_dia->SetValue(wxString::Format("%1.1f", curr_obj->nozzle_diameter));
            wxCommandEvent evt(wxEVT_COMBOBOX);
            evt.SetEventObject(m_comboBox_nozzle_dia);
            wxPostEvent(m_comboBox_nozzle_dia, evt);
        }
        else {
            m_comboBox_nozzle_dia->SetValue("");
        }
    }
}

void CalibrationWizard::init_bed_type_selections()
{
    m_comboBox_bed_type->Clear();
    int curr_selection = 0;
    if (curr_obj) {
        const ConfigOptionDef* bed_type_def = print_config_def.get("curr_bed_type");
        if (bed_type_def && bed_type_def->enum_keys_map) {
            for (auto item : bed_type_def->enum_labels) {
                m_comboBox_bed_type->AppendString(_L(item));
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

void CalibrationWizard::check_has_ams(MachineObject* obj) {
    if (obj->amsList.size() == 0 || obj->ams_exist_bits == 0) {
        m_select_ams_mode_panel->Hide();
        wxCommandEvent radioBox_evt(wxEVT_RADIOBUTTON);
        radioBox_evt.SetEventObject(m_ext_spool_radiobox);
        wxPostEvent(m_ext_spool_radiobox, radioBox_evt);
    }
    else {
        m_select_ams_mode_panel->Show();
        wxCommandEvent radioBox_evt(wxEVT_RADIOBUTTON);
        radioBox_evt.SetEventObject(m_ams_radiobox);
        wxPostEvent(m_ams_radiobox, radioBox_evt);
    }
}

void CalibrationWizard::update_printer() {
    DeviceManager* dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev) return;
    MachineObject* new_obj = dev->get_selected_machine();
    if (new_obj) {
        check_has_ams(new_obj);

        if (new_obj != curr_obj) {
            curr_obj = new_obj;
            wxGetApp().sidebar().load_ams_list(new_obj->dev_id, new_obj);
            wxGetApp().preset_bundle->set_calibrate_printer("");
            init_presets_selections();
            change_ams_select_mode();
            on_update_ams_filament(false);
            on_switch_ams(m_ams_item_list[0]->m_amsinfo.ams_id);
            for (auto& fcb : m_filament_comboBox_list)
                fcb->SetValue(false);
            m_virtual_tray_comboBox->SetValue(false);

            // for pa & flow rate calibration, need to change high end or low end pages
            SimpleEvent e(EVT_CALIBRATION_NOTIFY_CHANGE_PAGES);
            e.SetEventObject(this);
            GetEventHandler()->ProcessEvent(e);

            auto it = m_printer_calib_infos.find(curr_obj->dev_id);
            if (it != m_printer_calib_infos.end() && it->second.mode == m_mode) {
                PresetBundle* preset_bundle = wxGetApp().preset_bundle;
                if (preset_bundle) {
                    for (auto filament_preset : m_printer_calib_infos[curr_obj->dev_id].filament_presets) {
                        for (auto filament_it = preset_bundle->filaments.begin(); filament_it != preset_bundle->filaments.end(); filament_it++) {
                            if (filament_it->name == filament_preset.second) {
                                m_filament_presets[filament_preset.first] = (&*filament_it);
                            }
                        }
                    }
                }
                std::vector<int> tray_ids;
                for (auto filament_preset : it->second.filament_presets) {
                    tray_ids.push_back(filament_preset.first);
                }
                set_selected_tray(tray_ids);
                jump_to_page(static_cast<PageType>(m_printer_calib_infos[curr_obj->dev_id].state));
            }
            else {
                // reset to first page 
                show_page(m_first_page);
            }
        }
    }
    else {
        curr_obj = nullptr;
    }
}

void CalibrationWizard::on_select_nozzle(wxCommandEvent& evt) {
    if (curr_obj) {
        double nozzle_value = 0.0;
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

static std::map<std::string, bool> filament_is_high_temp{
        {"PLA",     false},
        {"PLA-CF",  false},
        //{"PETG",    true},
        {"ABS",     true},
        {"TPU",     false},
        {"PA",      true},
        {"PA-CF",   true},
        {"PET-CF",  true},
        {"PC",      true},
        {"ASA",     true},
        {"HIPS",    true}
};

void CalibrationWizard::on_select_tray(SimpleEvent& evt) {
    // when set selection of comboBox or select a checkbox/radio will enter

    FilamentComboBoxList fcb_list = get_selected_filament_comboBox();
    if (fcb_list.empty()) {
        m_filament_presets.clear();
        m_filaments_incompatible_tips->SetLabel("");
        m_filaments_incompatible_tips->Hide();
        recommend_input_value();
        return;
    }
    // check compatibility
    bool has_high_temperature_filament = false;
    bool has_low_temperature_filament = false;
    wxString hight_temp_filament_type = "";
    wxString low_temp_filament_type = "";
    for (auto& fcb : fcb_list) {
        wxString filament_type = "";
        if (fcb->GetComboBox()->get_selected_preset())
            filament_type = fcb->GetComboBox()->get_selected_preset()->alias;
        for (auto& item : filament_is_high_temp) {
            if (filament_type.Contains(item.first)) {
                if (item.second == true) {
                    has_high_temperature_filament = true;
                    hight_temp_filament_type = item.first;
                }
                else {
                    has_low_temperature_filament = true;
                    low_temp_filament_type = item.first;
                }
            }
        }
    }
    if (has_high_temperature_filament && has_low_temperature_filament) {
        m_filament_presets.clear();
        wxString tips = wxString::Format(_L("Unable to print %s and %s together. Filaments have large bed temperature difference"), hight_temp_filament_type, low_temp_filament_type);
        m_filaments_incompatible_tips->SetLabel(tips);
        m_filaments_incompatible_tips->Show();
        Layout();
    }
    else {
        for (auto& fcb : fcb_list) {
            Preset* preset = const_cast<Preset*>(fcb->GetComboBox()->get_selected_preset());
            if (preset)
                m_filament_presets[fcb->get_tray_id()] = preset;
        }
        m_filaments_incompatible_tips->SetLabel("");
        m_filaments_incompatible_tips->Hide();
        Layout();
    }

    recommend_input_value();
}

void CalibrationWizard::on_update_ams_filament(bool dialog)
{
    auto& list = wxGetApp().preset_bundle->filament_ams_list;
    if (list.empty() && dialog) {
        MessageDialog dlg(this, _L("No AMS filaments. Please select a printer to load AMS info."), _L("Sync filaments with AMS"), wxOK);
        dlg.ShowModal();
        return;
    }

    // clear selections while changing printer
    for (auto& fcb : m_filament_comboBox_list)
        fcb->update_from_preset();
    m_virtual_tray_comboBox->update_from_preset();

    // update tray info
    DynamicPrintConfig empty_config;
    empty_config.set_key_value("filament_id", new ConfigOptionStrings{ "" });
    empty_config.set_key_value("tag_uid", new ConfigOptionStrings{ "" });
    empty_config.set_key_value("filament_type", new ConfigOptionStrings{ "" });
    empty_config.set_key_value("tray_name", new ConfigOptionStrings{ "" });
    empty_config.set_key_value("filament_colour", new ConfigOptionStrings{ "" });
    empty_config.set_key_value("filament_exist", new ConfigOptionBools{ false });
    for (int i = 0; i < m_filament_comboBox_list.size(); i++) {
        auto it = std::find_if(list.begin(), list.end(), [i](auto& entry) {
            return entry.first == i;
            });
        if (it != list.end()) {
            m_filament_comboBox_list[i]->load_tray_from_ams(i, it->second);
        }
        else {
            m_filament_comboBox_list[i]->load_tray_from_ams(i, empty_config);
        }
    }
    auto it = std::find_if(list.begin(), list.end(), [](auto& entry) {
        return entry.first == VIRTUAL_TRAY_ID;
        });
    if (it != list.end()) {
        m_virtual_tray_comboBox->load_tray_from_ams(VIRTUAL_TRAY_ID, it->second);
    }
    else {
        m_virtual_tray_comboBox->load_tray_from_ams(VIRTUAL_TRAY_ID, empty_config);
    }

    // update m_ams_item_list
    std::vector<AMSinfo> ams_info;
    if (curr_obj) {
        for (auto ams = curr_obj->amsList.begin(); ams != curr_obj->amsList.end(); ams++) {
            AMSinfo info;
            info.ams_id = ams->first;
            if (ams->second->is_exists && info.parse_ams_info(ams->second, curr_obj->ams_calibrate_remain_flag, curr_obj->is_support_ams_humidity)) ams_info.push_back(info);
        }
    }
    for (auto i = 0; i < m_ams_item_list.size(); i++) {
        AMSItem* item = m_ams_item_list[i];
        if (ams_info.size() > 1) {
            m_muilti_ams_panel->Show();
            if (i < ams_info.size()) {
                item->Update(ams_info[i]);
                item->Open();
            }
            else {
                item->Close();
            }
        }
        else {
            m_muilti_ams_panel->Hide();
            item->Close();
        }
    }
    Layout();
}

bool CalibrationWizard::recommend_input_value() {
    if (m_filament_presets.empty()) {
        m_nozzle_temp->GetTextCtrl()->SetValue(wxEmptyString);
        m_bed_temp->GetTextCtrl()->SetValue(wxEmptyString);
        m_max_volumetric_speed->GetTextCtrl()->SetValue(wxEmptyString);
        m_bed_type_incompatible_tips->SetLabel("");
        m_bed_type_incompatible_tips->Hide();
        return false;
    }

    int bed_temp_int = -1;
    bool bed_temp_compatible = true;
    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        for (auto& filament_preset : m_filament_presets) {
            // update nozzle temperature
            ConfigOption* opt_nozzle_temp = filament_preset.second->config.option("nozzle_temperature");
            if (opt_nozzle_temp) {
                ConfigOptionInts* opt_min_ints = dynamic_cast<ConfigOptionInts*>(opt_nozzle_temp);
                if (opt_min_ints) {
                    wxString text_nozzle_temp = wxString::Format("%d", opt_min_ints->get_at(0));
                    m_nozzle_temp->GetTextCtrl()->SetValue(text_nozzle_temp);
                }
            }
            // update bed temperature
            bed_temp_int = get_bed_temp(&filament_preset.second->config);
            wxString bed_temp_text = wxString::Format("%d", bed_temp_int);
            m_bed_temp->GetTextCtrl()->SetValue(bed_temp_text);
            // update max flow speed
            ConfigOption* opt_flow_speed = filament_preset.second->config.option("filament_max_volumetric_speed");
            if (opt_flow_speed) {
                ConfigOptionFloats* opt_flow_floats = dynamic_cast<ConfigOptionFloats*>(opt_flow_speed);
                if (opt_flow_floats) {
                    wxString flow_val_text = wxString::Format("%0.2f", opt_flow_floats->get_at(0));
                    m_max_volumetric_speed->GetTextCtrl()->SetValue(flow_val_text);
                }
            }

            // check compatibility
            if (m_bed_temp->GetTextCtrl()->GetValue().compare("0") <= 0) {
                m_nozzle_temp->GetTextCtrl()->SetValue(wxEmptyString);
                m_bed_temp->GetTextCtrl()->SetValue(wxEmptyString);
                m_max_volumetric_speed->GetTextCtrl()->SetValue(wxEmptyString);
                wxString tips = wxString::Format(_L("%s is not compatible with %s"), m_comboBox_bed_type->GetValue(), filament_preset.second->alias);
                m_bed_type_incompatible_tips->SetLabel(tips);
                m_bed_type_incompatible_tips->Update();
                m_bed_type_incompatible_tips->Show();
                Layout();
                bed_temp_compatible = false;
            }
            else {
                m_bed_type_incompatible_tips->SetLabel("");
                m_bed_type_incompatible_tips->Hide();
            }
        }
    }
    return bed_temp_compatible;
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

bool CalibrationWizard::save_presets(Preset *preset, const std::string &config_key, ConfigOption *config_value, const std::string &new_preset_name, std::string& message)
{
    PresetCollection* filament_presets = &wxGetApp().preset_bundle->filaments;

    std::string new_name = filament_presets->get_preset_name_by_alias(new_preset_name);
    std::string curr_preset_name = preset->name;
    preset = filament_presets->find_preset(curr_preset_name);
    Preset temp_preset = *preset;

    bool exist_preset = false;
    // If name is current, get the editing preset
    Preset *new_preset = filament_presets->find_preset(new_name);
    if (new_preset) {
        if (new_preset->is_system) {
            message = "The name cannot be the same as the system preset name.";
            return false;
        }

        if (new_preset != preset) {
            message = "The name is the same as another existing preset name";
            return false;
        }
        if (new_preset != &filament_presets->get_edited_preset())
            new_preset = &temp_preset;
        exist_preset = true;
    } else {
        new_preset = &temp_preset;
    }

    new_preset->config.set_key_value(config_key, config_value);

    // Save the preset into Slic3r::data_dir / presets / section_name / preset_name.ini
    filament_presets->save_current_preset(new_name, false, false, new_preset);

    // BBS create new settings
    new_preset = filament_presets->find_preset(new_name, false, true);
    // Preset* preset = &m_presets.preset(it - m_presets.begin(), true);
    if (!new_preset) {
        BOOST_LOG_TRIVIAL(info) << "create new preset failed";
        return false;
    }

    // set sync_info for sync service
    if (exist_preset) {
        new_preset->sync_info = "update";
        BOOST_LOG_TRIVIAL(info) << "sync_preset: update preset = " << new_preset->name;
    } else {
        new_preset->sync_info = "create";
        if (wxGetApp().is_user_login()) new_preset->user_id = wxGetApp().getAgent()->get_user_id();
        BOOST_LOG_TRIVIAL(info) << "sync_preset: create preset = " << new_preset->name;
    }
    new_preset->save_info();

    // Mark the print & filament enabled if they are compatible with the currently selected preset.
    // If saving the preset changes compatibility with other presets, keep the now incompatible dependent presets selected, however with a "red flag" icon showing that they are
    // no more compatible.
    wxGetApp().preset_bundle->update_compatible(PresetSelectCompatibleType::Never);

    // BBS if create a new prset name, preset changed from preset name to new preset name
    if (!exist_preset) {
        wxGetApp().plater()->sidebar().update_presets_from_to(Preset::Type::TYPE_FILAMENT, curr_preset_name, new_preset->name);
    }

    wxGetApp().mainframe->update_filament_tab_ui();
    return true;
}

void CalibrationWizard::jump_to_page(PageType page_type) {
    if (page_type == PageType::Finish) {
        show_page(m_first_page);
        return;
    }

    auto tabbook = static_cast<Tabbook*>(GetParent());
    for (int i = 0; i < tabbook->GetPageCount(); i++) {
        if (static_cast<CalibrationWizard*>(tabbook->GetPage(i))->get_calibration_mode() == m_mode)
            tabbook->SetSelection(i);
    }
    auto page_node = m_first_page;
    while (page_node)
    {
        if (page_node->get_page_type() == page_type) {
            m_curr_page = page_node;
            show_page(m_curr_page);
            break;
        }
        page_node = page_node->get_next_page();
    }
}

void CalibrationWizard::init_bitmaps()
{
    m_bitmap_pause = ScalableBitmap(this, "print_control_pause", 18);
    m_bitmap_pause_hover = ScalableBitmap(this, "print_control_pause_hover", 18);
    m_bitmap_resume = ScalableBitmap(this, "print_control_resume", 18);
    m_bitmap_resume_hover = ScalableBitmap(this, "print_control_resume_hover", 18);
    m_bitmap_pause_disable = ScalableBitmap(this, "print_control_pause_disable", 18);
    m_bitmap_resume_disable = ScalableBitmap(this, "print_control_resume_disable", 18);
    m_bitmap_abort = ScalableBitmap(this, "print_control_stop", 18);
    m_bitmap_abort_hover = ScalableBitmap(this, "print_control_stop_hover", 18);
    m_bitmap_abort_disable = ScalableBitmap(this, "print_control_stop_disable", 18);
}

HistoryWindow::HistoryWindow(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
{
    create();
    wxGetApp().UpdateDlgDarkUI(this);
}

void HistoryWindow::create() {
    this->SetBackgroundColour(*wxWHITE);
    auto main_sizer = new wxBoxSizer(wxVERTICAL);

    auto scroll_window = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxVSCROLL);
    scroll_window->SetScrollRate(5, 5);
    scroll_window->SetBackgroundColour(*wxWHITE);

    auto scroll_sizer = new wxBoxSizer(wxVERTICAL);
    scroll_window->SetSizer(scroll_sizer);

    m_history_data_panel = new wxPanel(scroll_window);

    scroll_sizer->Add(m_history_data_panel, 1, wxALIGN_CENTER);

    main_sizer->Add(scroll_window, 1, wxEXPAND | wxALL, FromDIP(10));

    SetSizer(main_sizer);
    Layout();
    Fit();
    SetMinSize(wxSize(FromDIP(960), FromDIP(720)));
    SetSize(wxSize(FromDIP(960), FromDIP(720)));
    CenterOnParent();
}

PressureAdvanceWizard::PressureAdvanceWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, CalibMode::Calib_PA_Line, id, pos, size, style)
{
    create_pages();
    init_bitmaps();
    set_ams_select_mode(FSMCheckBoxMode);

    Bind(EVT_CALIBRATION_NOTIFY_CHANGE_PAGES, &PressureAdvanceWizard::switch_pages, this);
}

void PressureAdvanceWizard::create_save_panel_content(wxBoxSizer* sizer)
{
    // low end save panel
    {
        m_low_end_save_panel = new wxPanel(m_save_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
        auto low_end_sizer = new wxBoxSizer(wxVERTICAL);
        m_low_end_save_panel->SetSizer(low_end_sizer);

        auto complete_text_panel = new wxPanel(m_low_end_save_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
        wxBoxSizer* complete_text_sizer = new wxBoxSizer(wxVERTICAL);
        auto complete_text = new wxStaticText(complete_text_panel, wxID_ANY, _L("Please find the best line on your plate"));
        complete_text->SetFont(Label::Head_14);
        complete_text->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
        complete_text_sizer->Add(complete_text, 0, wxALIGN_CENTER);
        complete_text_panel->SetSizer(complete_text_sizer);
        low_end_sizer->Add(complete_text_panel, 0, wxALIGN_CENTER, 0);

        low_end_sizer->AddSpacer(FromDIP(20));

        m_record_picture = new wxStaticBitmap(m_low_end_save_panel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
        low_end_sizer->Add(m_record_picture, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);

        low_end_sizer->AddSpacer(FromDIP(20));

        auto value_sizer = new wxBoxSizer(wxHORIZONTAL);
        auto k_value_text = new wxStaticText(m_low_end_save_panel, wxID_ANY, _L("Factor K"), wxDefaultPosition, wxDefaultSize, 0);
        k_value_text->Wrap(-1);
        k_value_text->SetFont(::Label::Body_14);
        auto n_value_text = new wxStaticText(m_low_end_save_panel, wxID_ANY, _L("Factor N"), wxDefaultPosition, wxDefaultSize, 0);
        n_value_text->Wrap(-1);
        n_value_text->SetFont(::Label::Body_14);
        m_k_val = new TextInput(m_low_end_save_panel, wxEmptyString, "", "", wxDefaultPosition, CALIBRATION_OPTIMAL_INPUT_SIZE, 0);
        m_n_val = new TextInput(m_low_end_save_panel, wxEmptyString, "", "", wxDefaultPosition, CALIBRATION_OPTIMAL_INPUT_SIZE, 0);
        n_value_text->Hide();
        m_n_val->Hide();
        value_sizer->Add(k_value_text, 0, wxALIGN_CENTER_VERTICAL, 0);
        value_sizer->AddSpacer(FromDIP(10));
        value_sizer->Add(m_k_val, 0);
        value_sizer->AddSpacer(FromDIP(50));
        value_sizer->Add(n_value_text, 0, wxALIGN_CENTER_VERTICAL, 0);
        value_sizer->AddSpacer(FromDIP(10));
        value_sizer->Add(m_n_val, 0);
        low_end_sizer->Add(value_sizer, 0, wxALIGN_CENTER);

        sizer->Add(m_low_end_save_panel, 0, wxEXPAND);
    }

    // high end save panel (Hide)
    {
        m_high_end_save_panel = new wxPanel(m_save_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
        auto high_end_sizer = new wxBoxSizer(wxVERTICAL);
        m_high_end_save_panel->SetSizer(high_end_sizer);

        auto complete_text_panel = new wxPanel(m_high_end_save_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
        wxBoxSizer* complete_text_sizer = new wxBoxSizer(wxVERTICAL);
        auto complete_text = new wxStaticText(complete_text_panel, wxID_ANY, _L("We found the best Pressure Advance Factor"));
        complete_text->SetFont(Label::Head_14);
        complete_text->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
        complete_text_sizer->Add(complete_text, 0, wxALIGN_CENTER);
        complete_text_panel->SetSizer(complete_text_sizer);
        high_end_sizer->Add(complete_text_panel, 0, wxALIGN_CENTER, 0);

        high_end_sizer->AddSpacer(FromDIP(20));

        m_grid_panel = new wxPanel(m_high_end_save_panel, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
        high_end_sizer->Add(m_grid_panel, 0, wxALIGN_CENTER);

        sizer->Add(m_high_end_save_panel, 0, wxEXPAND);

        m_high_end_save_panel->Hide();
    }
}

CalibrationWizardPage* PressureAdvanceWizard::create_start_page()
{
    auto page = new CalibrationWizardPage(m_scrolledWindow);
    page->set_page_type(PageType::Start);
    page->set_highlight_step_text(PageType::Start);
    auto page_content_sizer = page->get_content_vsizer();

    auto when_title = new wxStaticText(page, wxID_ANY, _L("When you need Pressure Advance Calibration"));
    when_title->SetFont(Label::Head_14);
    when_title->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(when_title);
    auto when_text = new wxStaticText(page, wxID_ANY, _L("uneven extrusion"));
    when_text->SetFont(Label::Body_14);
    when_text->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(when_text);

    page_content_sizer->AddSpacer(PRESET_GAP);

    auto bitmap_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto bitmap1 = new wxStaticBitmap(page, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    bitmap1->SetMinSize(wxSize(560, 450));
    bitmap1->SetBackgroundColour(*wxBLACK);
    bitmap_sizer->Add(bitmap1, 0, wxALL, 0);
    bitmap_sizer->AddSpacer(FromDIP(20));
    auto bitmap2 = new wxStaticBitmap(page, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    bitmap2->SetMinSize(wxSize(560, 450));
    bitmap2->SetBackgroundColour(*wxBLACK);
    bitmap_sizer->Add(bitmap2, 0, wxALL, 0);
    page_content_sizer->Add(bitmap_sizer, 0, wxALL, 0);

    page_content_sizer->AddSpacer(PRESET_GAP);

    auto about_title = new wxStaticText(page, wxID_ANY, _L("About this calibration"));
    about_title->SetFont(Label::Head_14);
    about_title->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(about_title);
    auto about_text = new wxStaticText(page, wxID_ANY, _L("After calibration, the linear compensation factor(K) will be recorded and applied to printing. This factor would be different if device, degree of usage, material, and material family type are different"));
    about_text->SetFont(Label::Body_14);
    about_text->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(about_text);

    page_content_sizer->AddSpacer(PRESET_GAP);

    auto wiki = new wxStaticText(page, wxID_ANY, _L("Wiki"));
    wiki->SetFont(Label::Head_14);
    wiki->SetForegroundColour({ 0, 88, 220 });
    wiki->Bind(wxEVT_ENTER_WINDOW, [this, wiki](wxMouseEvent& e) {
        e.Skip();
        SetCursor(wxCURSOR_HAND);
        });
    wiki->Bind(wxEVT_LEAVE_WINDOW, [this, wiki](wxMouseEvent& e) {
        e.Skip();
        SetCursor(wxCURSOR_ARROW);
        });
    wiki->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
        if (!m_wiki_url.empty()) wxLaunchDefaultBrowser(m_wiki_url);
        });
    page_content_sizer->Add(wiki, 0);


    auto page_prev_btn = page->get_prev_btn();
    page_prev_btn->Hide();
    page_prev_btn->SetLabel(_L("Manage Result"));
    page_prev_btn->SetButtonType(ButtonType::Back);
    page_prev_btn->Bind(wxEVT_BUTTON, [this](auto& e) {
        if (is_high_end_type(curr_obj)) {
            sync_history_window_data();
        }
        });

    auto page_next_btn = page->get_next_btn();
    page_next_btn->SetLabel(_L("Start"));
    page_next_btn->SetButtonType(ButtonType::Start);

    return page;
}

void PressureAdvanceWizard::create_pages()
{
    create_history_window();
    // page 0 : start page
    m_page0 = create_start_page();
    m_page0->set_page_title(_L("Pressure Advance"));
    m_all_pages_sizer->Add(m_page0, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 1 : preset page
    m_page1 = create_presets_page(false);
    m_page1->set_page_title(_L("Pressure Advance"));
    m_all_pages_sizer->Add(m_page1, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 2 : print page
    m_page2 = create_print_page();
    m_page2->set_page_title(_L("Pressure Advance"));
    m_all_pages_sizer->Add(m_page2, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 3 : save page
    m_page3 = create_save_page();
    m_page3->set_page_title(_L("Pressure Advance"));
    m_page3->Bind(wxEVT_SHOW, [this](auto&) {
        sync_save_page_data();
        });
    m_all_pages_sizer->Add(m_page3, 1, wxEXPAND | wxALL, FromDIP(25));

    // link page
    m_first_page = m_page0;
    m_curr_page = m_page0;
    m_page0->chain(m_page1)->chain(m_page2)->chain(m_page3);
    show_page(m_curr_page);
}

void PressureAdvanceWizard::create_history_window()
{
    m_history_page = new HistoryWindow(this);
}

void PressureAdvanceWizard::request_calib_result() {
    if (is_high_end_type(curr_obj)) {
        if (is_first_time_get_result) {
            curr_obj->has_get_pa_calib_result = false;
            CalibUtils::emit_get_PA_calib_results(curr_obj->nozzle_diameter);
            is_first_time_get_result = false;
        }
        if (curr_obj->has_get_pa_calib_result) {
            if (!has_get_result) {
                CalibUtils::get_PA_calib_results(m_calib_results);
                if (m_calib_results.size() > 0) {
                    has_get_result = true;
                    sync_save_page_data();
                }
            }
        }
    }
}

void PressureAdvanceWizard::sync_history_window_data() {
    auto history_data_panel = m_history_page->m_history_data_panel;
    history_data_panel->DestroyChildren();

    wxGridBagSizer* gbSizer;
    gbSizer = new wxGridBagSizer(FromDIP(15), FromDIP(0));
    gbSizer->SetFlexibleDirection(wxBOTH);
    gbSizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    history_data_panel->SetSizer(gbSizer, true);

    auto title_nozzle = new wxStaticText(history_data_panel, wxID_ANY, _L("Nozzle Diameter"));
    title_nozzle->SetFont(Label::Head_14);
    gbSizer->Add(title_nozzle, { 0, 0 }, {1, 1}, wxRIGHT, FromDIP(80));

    auto title_material = new wxStaticText(history_data_panel, wxID_ANY, _L("Material"));
    title_material->SetFont(Label::Head_14);
    gbSizer->Add(title_material, { 0, 1 }, { 1, 1 }, wxRIGHT, FromDIP(80));

    auto title_k = new wxStaticText(history_data_panel, wxID_ANY, _L("K"));
    title_k->SetFont(Label::Head_14);
    gbSizer->Add(title_k, { 0, 2 }, { 1, 1 }, wxRIGHT, FromDIP(80));

    // Hide
    //auto title_n = new wxStaticText(history_data_panel, wxID_ANY, _L("N"));
    //title_n->SetFont(Label::Head_14);
    //gbSizer->Add(title_n, { 0, 3 }, { 1, 1 }, wxRIGHT, FromDIP(80));

    auto title_action = new wxStaticText(history_data_panel, wxID_ANY, _L("Action"));
    title_action->SetFont(Label::Head_14);
    gbSizer->Add(title_action, { 0, 3 }, { 1, 1 });

    int i = 1;
    for (auto& result : m_calib_results_history) {
        auto nozzle_dia_str = wxString::Format("%1.1f", result.nozzle_diameter);
        auto nozzle_dia_value = new wxStaticText(history_data_panel, wxID_ANY, nozzle_dia_str);
        wxString material_name = NA_STR;
        material_name = result.name;
        auto material_name_value = new wxStaticText(history_data_panel, wxID_ANY, material_name);
        auto k_str = wxString::Format("%.3f", result.k_value);
        auto n_str = wxString::Format("%.3f", result.n_coef);
        auto k_value = new wxStaticText(history_data_panel, wxID_ANY, k_str);
        auto n_value = new wxStaticText(history_data_panel, wxID_ANY, n_str);
        n_value->Hide();
        auto delete_button = new PageButton(history_data_panel, _L("Delete"), ButtonType::Back);
        delete_button->Bind(wxEVT_BUTTON, [gbSizer, i, history_data_panel, &result](auto &e) {
            for (int j = 0; j < 5; j++) {
                auto item = gbSizer->FindItemAtPosition({ i, j });
                item->GetWindow()->Hide();
            }
            gbSizer->SetEmptyCellSize({ 0,0 });
            history_data_panel->Layout();
            history_data_panel->Fit();
            CalibUtils::delete_PA_calib_result({result.tray_id, result.cali_idx, result.nozzle_diameter, result.filament_id});
            });
        auto edit_button = new PageButton(history_data_panel, _L("Edit"), ButtonType::Next);
        edit_button->Bind(wxEVT_BUTTON, [this, &result, nozzle_dia_str, k_value, n_value, material_name_value](auto& e) {
            EditCalibrationHistoryDialog dlg(m_history_page, k_value->GetLabel(), n_value->GetLabel(), material_name_value->GetLabel(), nozzle_dia_str);
            if (dlg.ShowModal() == wxID_OK) {
                float k = 0.0f;
                k = dlg.get_k_value();
                wxString new_k_str = wxString::Format("%.3f", k);

                wxString new_material_name = dlg.get_material_name_value();

                k_value->SetLabel(new_k_str);
                material_name_value->SetLabel(new_material_name);
                PACalibResult new_result = result;
                new_result.tray_id = -1;
                new_result.name = new_material_name.ToStdString();
                new_result.k_value = k;
                CalibUtils::set_PA_calib_result({ new_result });
            }
            });

        gbSizer->Add(nozzle_dia_value, { i, 0 }, { 1, 1 }, wxRIGHT, FromDIP(80));
        gbSizer->Add(material_name_value, { i, 1 }, { 1, 1 }, wxRIGHT, FromDIP(80));
        gbSizer->Add(k_value, { i, 2 }, { 1, 1 }, wxRIGHT, FromDIP(80));
        //gbSizer->Add(n_value, { i, 3 }, { 1, 1 }, wxRIGHT, FromDIP(80));
        gbSizer->Add(delete_button, { i, 3 }, { 1, 1 }, wxRIGHT, FromDIP(25));
        gbSizer->Add(edit_button, { i, 4 }, { 1, 1 });
        i++;
    }

    if (m_calib_results_history.empty()) {
        MessageDialog msg_dlg(nullptr, _L("No History Result"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
    }
    else {
        m_history_page->ShowModal();
    }

    m_history_page->Layout();
}

void PressureAdvanceWizard::sync_save_page_data() {
    FilamentComboBoxList fcb_list = get_selected_filament_comboBox();

    m_grid_panel->DestroyChildren();
    wxBoxSizer* grid_sizer = new wxBoxSizer(wxHORIZONTAL);
    const int COLUMN_GAP = FromDIP(50);
    const int ROW_GAP = FromDIP(30);
    wxBoxSizer* left_title_sizer = new wxBoxSizer(wxVERTICAL);
    left_title_sizer->AddSpacer(FromDIP(52));
    auto k_title = new wxStaticText(m_grid_panel, wxID_ANY, _L("FactorK"), wxDefaultPosition, wxDefaultSize, 0);
    k_title->SetFont(Label::Head_14);
    left_title_sizer->Add(k_title, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);
    auto n_title = new wxStaticText(m_grid_panel, wxID_ANY, _L("FactorN"), wxDefaultPosition, wxDefaultSize, 0);
    n_title->SetFont(Label::Head_14);
    // hide n value
    n_title->Hide();
    left_title_sizer->Add(n_title, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);
    auto brand_title = new wxStaticText(m_grid_panel, wxID_ANY, _L("Brand Name"), wxDefaultPosition, wxDefaultSize, 0);
    brand_title->SetFont(Label::Head_14);
    left_title_sizer->Add(brand_title, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);
    grid_sizer->Add(left_title_sizer);
    grid_sizer->AddSpacer(COLUMN_GAP);

    for (auto& fcb : fcb_list) {
        bool result_failed = false;
        auto it_result = std::find_if(m_calib_results.begin(), m_calib_results.end(), [fcb](auto& calib_result) {
            return calib_result.tray_id == fcb->get_tray_id();
            });
        if (it_result != m_calib_results.end()) {
            result_failed = false;
        }
        else {
            result_failed = true;
        }
        int index = it_result - m_calib_results.begin();

        wxBoxSizer* column_data_sizer = new wxBoxSizer(wxVERTICAL);
        auto tray_title = new wxStaticText(m_grid_panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0);
        tray_title->SetFont(Label::Head_14);
        tray_title->SetLabel(fcb->get_tray_name());
        column_data_sizer->Add(tray_title, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);

        auto k_value = new TextInput(m_grid_panel, NA_STR, "", "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, wxTE_PROCESS_ENTER);
        auto n_value = new TextInput(m_grid_panel, NA_STR, "", "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, wxTE_PROCESS_ENTER);
        k_value->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
        n_value->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
        auto k_value_failed = new wxStaticText(m_grid_panel, wxID_ANY, _L("Failed"), wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE);
        auto n_value_failed = new wxStaticText(m_grid_panel, wxID_ANY, _L("Failed"), wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE);

        if (!result_failed) {
            auto k_str = wxString::Format("%.3f", m_calib_results[index].k_value);
            auto n_str = wxString::Format("%.3f", m_calib_results[index].n_coef);
            k_value->GetTextCtrl()->SetValue(k_str);
            n_value->GetTextCtrl()->SetValue(n_str);
        }

        k_value->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this, result_failed, index, fcb, k_value](auto& e) {
            if (!result_failed) {
                float k = 0.0f;
                validate_input_k_value(k_value->GetTextCtrl()->GetValue(), &k);
                wxString k_str = wxString::Format("%.3f", k);
                k_value->GetTextCtrl()->SetValue(k_str);
                m_calib_results[index].k_value = k;
            }
            });
        k_value->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this, result_failed, index, fcb, k_value](auto& e) {
            if (!result_failed) {
                float k = 0.0f;
                validate_input_k_value(k_value->GetTextCtrl()->GetValue(), &k);
                wxString k_str = wxString::Format("%.3f", k);
                k_value->GetTextCtrl()->SetValue(k_str);
                m_calib_results[index].k_value = k;
            }
            e.Skip();
            });
        column_data_sizer->Add(k_value, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);
        column_data_sizer->Add(n_value, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);
        column_data_sizer->Add(k_value_failed, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);
        column_data_sizer->Add(n_value_failed, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);

        auto comboBox_tray_name = new ComboBox(m_grid_panel, wxID_ANY, "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, 0, nullptr);
        auto tray_name_failed = new wxStaticText(m_grid_panel, wxID_ANY, " - ", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE);
        wxArrayString selections;
        static std::vector<PACalibResult> filtered_results;
        filtered_results.clear();
        for (auto history : m_calib_results_history) {
            if (history.setting_id == m_filament_presets[fcb->get_tray_id()]->setting_id) {
                filtered_results.push_back(history);
                selections.push_back(history.name);
            }
        }
        comboBox_tray_name->Set(selections);

        auto set_edit_mode = [this, k_value, n_value, k_value_failed, n_value_failed, comboBox_tray_name, tray_name_failed](std::string str) {
            if (str == "normal") {
                comboBox_tray_name->Show();
                tray_name_failed->Show(false);
                k_value->Show();
                n_value->Show();
                k_value_failed->Show(false);
                n_value_failed->Show(false);
            }
            if (str == "failed") {
                comboBox_tray_name->Show(false);
                tray_name_failed->Show();
                k_value->Show(false);
                n_value->Show(false);
                k_value_failed->Show();
                n_value_failed->Show();
            }

            // hide n value
            n_value->Hide();
            n_value_failed->Hide();

            m_grid_panel->Layout();
            m_grid_panel->Update();
        };

        if (result_failed) {
            set_edit_mode("failed");
        }
        else {
            comboBox_tray_name->SetValue(fcb->GetComboBox()->GetValue());
            m_calib_results[index].name = comboBox_tray_name->GetValue().ToStdString();
            set_edit_mode("normal");
            comboBox_tray_name->GetTextCtrl()->Bind(wxEVT_KEY_DOWN, [this, result_failed, index, fcb, comboBox_tray_name](auto& e) {
                if (wxGetKeyState(WXK_RETURN)) {
                    this->m_calib_results[index].name = comboBox_tray_name->GetTextCtrl()->GetValue().ToStdString();
                }
                else
                    e.Skip();
                });
            comboBox_tray_name->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this, result_failed, index, fcb, comboBox_tray_name](auto& e) {
                this->m_calib_results[index].name = comboBox_tray_name->GetTextCtrl()->GetValue().ToStdString();
                e.Skip();
                });
        }

        comboBox_tray_name->Bind(wxEVT_COMBOBOX, [this, result_failed, index, fcb, comboBox_tray_name, k_value, n_value, set_edit_mode](auto& e) {
            int selection = comboBox_tray_name->GetSelection();
            set_edit_mode("normal");
            auto history = filtered_results[selection];

            if (!result_failed) {
                this->m_calib_results[index].name = history.name;
            }
            });

        column_data_sizer->Add(comboBox_tray_name, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);
        column_data_sizer->Add(tray_name_failed, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);

        grid_sizer->Add(column_data_sizer);
        grid_sizer->AddSpacer(COLUMN_GAP);
    }
    m_grid_panel->SetSizer(grid_sizer, true);
    m_grid_panel->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        m_grid_panel->SetFocusIgnoringChildren();
        });
    Layout();
}

void PressureAdvanceWizard::switch_pages(SimpleEvent& evt) {
    if (curr_obj) {
        if (is_high_end_type(curr_obj))
        {
            m_low_end_save_panel->Hide();
            m_high_end_save_panel->Show();

            m_page0->get_prev_btn()->Show();

            sync_save_page_data(); // CALIBRATION_DEBUG
        }
        else
        {
            m_high_end_save_panel->Hide();
            m_low_end_save_panel->Show();

            m_page0->get_prev_btn()->Hide();
        }
        Layout();
    }
}

void PressureAdvanceWizard::change_ams_select_mode() {
    if (is_high_end_type(curr_obj)) {
        set_ams_select_mode(FSMCheckBoxMode);
    }
    else{
        set_ams_select_mode(FSMRadioMode);
    }
}

bool PressureAdvanceWizard::start_calibration(std::vector<int> tray_ids)
{
    int nozzle_temp = -1;
    int bed_temp = -1;
    float max_volumetric_speed = -1;

    if (m_nozzle_temp->GetTextCtrl()->GetValue().IsEmpty() ||
        m_bed_temp->GetTextCtrl()->GetValue().IsEmpty() ||
        m_max_volumetric_speed->GetTextCtrl()->GetValue().IsEmpty())
        return false;
    nozzle_temp = stoi(m_nozzle_temp->GetTextCtrl()->GetValue().ToStdString());
    bed_temp = stoi(m_bed_temp->GetTextCtrl()->GetValue().ToStdString());
    max_volumetric_speed = stof(m_max_volumetric_speed->GetTextCtrl()->GetValue().ToStdString());

    if (bed_temp < 0 || nozzle_temp < 0 || max_volumetric_speed < 0) {
        MessageDialog msg_dlg(nullptr, _L("Make sure bed_temp > 0 \nnozzle_temp > 0\nmax_volumetric_speed > 0"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    if (is_high_end_type(curr_obj)) {
        m_calib_results.clear();
        X1CCalibInfos calib_infos;
        for (int tray_id : tray_ids) {
            if (m_filament_presets.find(tray_id) == m_filament_presets.end())
                return false;
            X1CCalibInfos::X1CCalibInfo calib_info;
            calib_info.tray_id = tray_id;
            calib_info.nozzle_diameter = dynamic_cast<ConfigOptionFloats *>(m_printer_preset->config.option("nozzle_diameter"))->get_at(0);
            calib_info.filament_id = m_filament_presets[tray_id]->filament_id;
            calib_info.setting_id = m_filament_presets[tray_id]->setting_id;
            calib_info.bed_temp = bed_temp;
            calib_info.nozzle_temp = nozzle_temp;
            calib_info.max_volumetric_speed = max_volumetric_speed;
            calib_infos.calib_datas.push_back(calib_info);
        }
        std::string error_message;
        CalibUtils::calib_PA(calib_infos, error_message);
        if (!error_message.empty()) {
            MessageDialog msg_dlg(nullptr, error_message, wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return false;
        }
        is_first_time_get_result = true;
        has_get_result = false;
        show_page(get_curr_page()->get_next_page());
        return true;
    }
    else {
        curr_obj->command_start_extrusion_cali(tray_ids[0], nozzle_temp, bed_temp, max_volumetric_speed, m_filament_presets.begin()->second->setting_id);
        show_page(get_curr_page()->get_next_page());
        return true;
    }

    return false;
}

bool PressureAdvanceWizard::save_calibration_result()
{
    if (is_high_end_type(curr_obj)) {
        for (auto& result : m_calib_results) {
            if (result.k_value > 0.5 || result.k_value < 0) {
                wxString k_tips = _L("Please input a valid value (K in 0~0.5)");
                MessageDialog msg_dlg(nullptr, k_tips, wxEmptyString, wxICON_WARNING | wxOK);
                msg_dlg.ShowModal();
                return false;
            }
        }
        CalibUtils::set_PA_calib_result(m_calib_results);
        return true;
    }
    else {
        float k = 0.0f;
        validate_input_k_value(m_k_val->GetTextCtrl()->GetValue(), &k);
        wxString k_str = wxString::Format("%.3f", k);
        m_k_val->GetTextCtrl()->SetValue(k_str);

        double n = 0.0;
        //n_text.ToDouble(&n);

        // set values
        int nozzle_temp = -1;
        int bed_temp = -1;
        float max_volumetric_speed = -1;
        std::string setting_id;
        std::string name;

        nozzle_temp = stoi(m_nozzle_temp->GetTextCtrl()->GetValue().ToStdString());
        bed_temp = stoi(m_bed_temp->GetTextCtrl()->GetValue().ToStdString());
        max_volumetric_speed = stof(m_max_volumetric_speed->GetTextCtrl()->GetValue().ToStdString());
        setting_id = m_filament_presets.begin()->second->setting_id;
        name = m_filament_presets.begin()->second->name;

        // send command
        std::vector<int> tray_ids = get_selected_tray();
        curr_obj->command_extrusion_cali_set(tray_ids[0], setting_id, name, k, n, bed_temp, nozzle_temp, max_volumetric_speed);
        return true;
    }
    return false;
}

bool PressureAdvanceWizard::recommend_input_value()
{
    return CalibrationWizard::recommend_input_value();
}

void PressureAdvanceWizard::init_bitmaps()
{
    m_print_picture->SetBitmap(create_scaled_bitmap("extrusion_calibration_tips_en", nullptr, 400));
    m_record_picture->SetBitmap(create_scaled_bitmap("extrusion_calibration_tips_en", nullptr, 400));
}

void PressureAdvanceWizard::check_sync_printer_status()
{
    // todo: sync the printer result
    DeviceManager *dev  = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;

    MachineObject *obj_ = dev->get_selected_machine();
    if (!obj_)
        return;

    if (m_cali_version != obj_->cali_version) {
        m_cali_version        = obj_->cali_version;
        CalibUtils::emit_get_PA_calib_info(obj_->nozzle_diameter, "");
    }
    
    if (CalibUtils::get_PA_calib_tab(m_calib_results_history)) {
        obj_->has_get_pa_calib_tab = false;

        //PACalibIndexInfo cali_info;
        //PACalibResult result_0 = m_calib_results_history[0];

        //cali_info.tray_id = 0;
        //cali_info.cali_idx = result_0.cali_idx;
        //cali_info.nozzle_diameter = result_0.nozzle_diameter;
        //cali_info.filament_id = result_0.filament_id;
        //CalibUtils::select_PA_calib_result(cali_info);

        //result_0                  = m_calib_results_history[2];
        //cali_info.tray_id         = 2;
        //cali_info.cali_idx        = result_0.cali_idx;
        //cali_info.nozzle_diameter = result_0.nozzle_diameter;
        //cali_info.filament_id     = result_0.filament_id;
        //CalibUtils::select_PA_calib_result(cali_info);

        //result_0                  = m_calib_results_history[3];
        //cali_info.tray_id         = 3;
        //cali_info.cali_idx        = result_0.cali_idx;
        //cali_info.nozzle_diameter = result_0.nozzle_diameter;
        //cali_info.filament_id     = result_0.filament_id;
        //CalibUtils::select_PA_calib_result(cali_info);
    }

    //if (m_calib_results_history.size() > 10) {
    //     PACalibIndexInfo cali_info;
    //     PACalibResult result_0 = m_calib_results_history[0];

    //     cali_info.tray_id = 0;
    //     cali_info.cali_idx = result_0.cali_idx;
    //     cali_info.nozzle_diameter = result_0.nozzle_diameter;
    //     cali_info.filament_id = result_0.filament_id;
    //     CalibUtils::delete_PA_calib_result(cali_info);
    //}
}

FlowRateWizard::FlowRateWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, CalibMode::Calib_Flow_Rate, id, pos, size, style)
{
    create_pages();
    init_bitmaps();
    Bind(EVT_CALIBRATION_NOTIFY_CHANGE_PAGES, &FlowRateWizard::switch_pages, this);
}

void FlowRateWizard::set_save_name() {
    if (m_filament_presets.size() > 0) {
        m_save_name = m_filament_presets.begin()->second->alias + "-Calibrated";
    }
    else { m_save_name = ""; }
    if (!is_high_end_type(curr_obj)) {
        m_save_name_input1->GetTextCtrl()->SetValue(m_save_name);
        m_save_name_input2->GetTextCtrl()->SetValue(m_save_name);
    }
}

void FlowRateWizard::create_save_panel_content(wxBoxSizer* sizer)
{
    auto complete_text = new wxStaticText(m_save_panel, wxID_ANY, _L("Please find the best object on your plate"), wxDefaultPosition, wxDefaultSize, 0);
    complete_text->SetFont(Label::Head_14);
    complete_text->Wrap(-1);
    sizer->Add(complete_text, 0, 0, 0);
    sizer->AddSpacer(FromDIP(20));

    m_low_record_picture2 = new wxStaticBitmap(m_save_panel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    m_low_record_picture2->SetMinSize(wxSize(560, 450));
    m_low_record_picture2->SetBackgroundColour(*wxBLACK);
    sizer->Add(m_low_record_picture2, 0, wxALIGN_CENTER, 0);

    sizer->AddSpacer(FromDIP(20));

    auto fine_value_sizer = new wxBoxSizer(wxVERTICAL);
    auto fine_value_text = new wxStaticText(m_save_panel, wxID_ANY, _L("Fill in the value above the block with smoothest top surface"), wxDefaultPosition, wxDefaultSize, 0);
    fine_value_text->Wrap(-1);
    fine_value_text->SetFont(::Label::Head_14);
    m_optimal_block_fine = new ComboBox(m_save_panel, wxID_ANY, "", wxDefaultPosition, CALIBRATION_OPTIMAL_INPUT_SIZE, 0, nullptr, wxCB_READONLY);
    wxArrayString fine_block_items;
    for (int i = 0; i < 10; i++)
    {
        fine_block_items.Add(std::to_string(-9 + (i)));
    }
    m_optimal_block_fine->Set(fine_block_items);
    m_fine_calc_result_text = new wxStaticText(m_save_panel, wxID_ANY, "");
    fine_value_sizer->Add(fine_value_text, 0, 0);
    fine_value_sizer->Add(m_optimal_block_fine, 0, 0);
    fine_value_sizer->Add(m_fine_calc_result_text, 0);
    sizer->Add(fine_value_sizer, 0, 0, 0);
    sizer->AddSpacer(FromDIP(20));

    auto save_text = new wxStaticText(m_save_panel, wxID_ANY, _L("Save to Filament Preset"), wxDefaultPosition, wxDefaultSize, 0);
    save_text->Wrap(-1);
    save_text->SetFont(Label::Head_14);
    sizer->Add(save_text, 0, 0, 0);

    m_save_name_input2 = new TextInput(m_save_panel, m_save_name, "", "", wxDefaultPosition, { CALIBRATION_TEXT_MAX_LENGTH, FromDIP(24) }, 0);
    sizer->Add(m_save_name_input2, 0, 0, 0);
    m_save_name_input2->GetTextCtrl()->Bind(wxEVT_TEXT, [this](auto& e) {
        if (!m_save_name_input2->GetTextCtrl()->GetValue().IsEmpty())
            m_save_name = m_save_name_input2->GetTextCtrl()->GetValue().ToStdString();
        else
            m_save_name = "";
        });
}

void FlowRateWizard::create_low_end_pages() {
    // page 3 : save coarse result
    m_low_end_page3 = new CalibrationWizardPage(m_scrolledWindow, false);
    m_low_end_page3->set_page_title(_L("Flow Rate"));
    m_low_end_page3->set_page_type(PageType::CoarseSave);
    m_low_end_page3->set_highlight_step_text(PageType::CoarseSave);

    auto page3_content_sizer = m_low_end_page3->get_content_vsizer();

    auto page3_description = new wxStaticText(m_low_end_page3, wxID_ANY, _L("Please find the best object on your plate"), wxDefaultPosition, wxDefaultSize, 0);
    page3_description->SetFont(Label::Head_14);
    page3_description->Wrap(-1);
    page3_content_sizer->Add(page3_description, 0, 0, 0);
    page3_content_sizer->AddSpacer(FromDIP(20));

    m_low_record_picture1 = new wxStaticBitmap(m_low_end_page3, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    m_low_record_picture1->SetMinSize(wxSize(560, 450));
    m_low_record_picture1->SetBackgroundColour(*wxBLACK);
    page3_content_sizer->Add(m_low_record_picture1, 0, 0, 0);

    page3_content_sizer->AddSpacer(FromDIP(20));

    auto coarse_value_sizer = new wxBoxSizer(wxVERTICAL);
    auto coarse_value_text = new wxStaticText(m_low_end_page3, wxID_ANY, _L("Fill in the value above the block with smoothest top surface"), wxDefaultPosition, wxDefaultSize, 0);
    coarse_value_text->SetFont(Label::Head_14);
    coarse_value_text->Wrap(-1);
    m_optimal_block_coarse = new ComboBox(m_low_end_page3, wxID_ANY, "", wxDefaultPosition, CALIBRATION_OPTIMAL_INPUT_SIZE, 0, nullptr, wxCB_READONLY);
    wxArrayString coarse_block_items;
    for (int i = 0; i < 9; i++)
    {
        coarse_block_items.Add(std::to_string(-20 + (i * 5)));
    }
    m_optimal_block_coarse->Set(coarse_block_items);
    m_coarse_calc_result_text = new wxStaticText(m_low_end_page3, wxID_ANY, "");
    coarse_value_sizer->Add(coarse_value_text, 0, 0);
    coarse_value_sizer->Add(m_optimal_block_coarse, 0, 0);
    coarse_value_sizer->Add(m_coarse_calc_result_text, 0);
    page3_content_sizer->Add(coarse_value_sizer, 0, 0, 0);
    page3_content_sizer->AddSpacer(FromDIP(20));

    auto checkBox_panel = new wxPanel(m_low_end_page3);
    auto cb_sizer = new wxBoxSizer(wxHORIZONTAL);
    checkBox_panel->SetSizer(cb_sizer);
    m_checkBox_skip_calibration = new CheckBox(checkBox_panel);
    cb_sizer->Add(m_checkBox_skip_calibration);

    auto cb_text = new wxStaticText(checkBox_panel, wxID_ANY, _L("Skip Calibration2"));
    cb_sizer->Add(cb_text);
    cb_text->Bind(wxEVT_LEFT_DOWN, [this](auto &) {
        m_checkBox_skip_calibration->SetValue(!m_checkBox_skip_calibration->GetValue());
        wxCommandEvent event(wxEVT_TOGGLEBUTTON);
        event.SetEventObject(m_checkBox_skip_calibration);
        m_checkBox_skip_calibration->GetEventHandler()->ProcessEvent(event);
        });

    page3_content_sizer->Add(checkBox_panel, 0, 0, 0);

    auto save_panel = new wxPanel(m_low_end_page3);
    auto save_sizer = new wxBoxSizer(wxVERTICAL);
    save_panel->SetSizer(save_sizer);

    auto save_text = new wxStaticText(save_panel, wxID_ANY, _L("Save to Filament Preset"), wxDefaultPosition, wxDefaultSize, 0);
    save_text->Wrap(-1);
    save_text->SetFont(Label::Head_14);
    save_sizer->Add(save_text, 0, 0, 0);

    m_save_name_input1 = new TextInput(save_panel, m_save_name, "", "", wxDefaultPosition, { CALIBRATION_TEXT_MAX_LENGTH, FromDIP(24) }, 0);
    save_sizer->Add(m_save_name_input1, 0, 0, 0);
    m_save_name_input1->GetTextCtrl()->Bind(wxEVT_TEXT, [this](auto& e) {
        if (!m_save_name_input1->GetTextCtrl()->GetValue().IsEmpty())
            m_save_name = m_save_name_input1->GetTextCtrl()->GetValue().ToStdString();
        else
            m_save_name = "";
        });

    page3_content_sizer->Add(save_panel, 0, 0, 0);
    save_panel->Hide();

    auto page3_prev_btn = m_low_end_page3->get_prev_btn();
    page3_prev_btn->SetLabel(_L("Restart"));
    page3_prev_btn->SetButtonType(ButtonType::Restart);

    auto page3_next_btn = m_low_end_page3->get_next_btn();
    page3_next_btn->SetLabel(_L("Calibrate"));
    page3_next_btn->SetButtonType(ButtonType::Calibrate);

    m_checkBox_skip_calibration->Bind(wxEVT_TOGGLEBUTTON, [this, save_panel](auto& e) {
        if (m_checkBox_skip_calibration->GetValue()) {
            m_low_end_page3->get_next_btn()->SetLabel(_L("Save"));
            m_low_end_page3->get_next_btn()->SetButtonType(ButtonType::Save);
            save_panel->Show();
        }
        else {
            m_low_end_page3->get_next_btn()->SetLabel(_L("Calibrate"));
            m_low_end_page3->get_next_btn()->SetButtonType(ButtonType::Calibrate);
            save_panel->Hide();
            m_low_end_page3->get_next_btn()->Bind(wxEVT_BUTTON, &FlowRateWizard::on_fine_tune, this);
        }
        Layout();
        e.Skip();
        });

    m_all_pages_sizer->Add(m_low_end_page3, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 4 : print page
    m_low_end_page4 = new CalibrationWizardPage(m_scrolledWindow, true);
    m_low_end_page4->set_page_title(_L("Flow Rate"));
    m_low_end_page4->set_page_type(PageType::FineCalibration);
    m_low_end_page4->set_highlight_step_text(PageType::FineCalibration);

    m_low_end_page4->get_prev_btn()->Hide();

    auto page4_next_btn = m_low_end_page4->get_next_btn();
    page4_next_btn->SetLabel(_L("Next"));
    page4_next_btn->SetButtonType(ButtonType::Next);

    auto page4_content_sizer = m_low_end_page4->get_content_vsizer();
    m_low_print_picture2 = new wxStaticBitmap(m_low_end_page4, wxID_ANY, create_scaled_bitmap("flow_rate_calibration_fine", nullptr, 400), wxDefaultPosition, wxDefaultSize, 0);
    page4_content_sizer->Add(m_low_print_picture2, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);
    page4_content_sizer->AddSpacer(FromDIP(20));
    m_all_pages_sizer->Add(m_low_end_page4, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 5 : save fine result
    m_low_end_page5 = create_save_page();
    m_low_end_page5->set_page_title(_L("Flow Rate"));
    m_all_pages_sizer->Add(m_low_end_page5, 1, wxEXPAND | wxALL, FromDIP(25));

    // link page
    m_page2->chain(m_low_end_page3)->chain(m_low_end_page4)->chain(m_low_end_page5);
    show_page(m_curr_page);

    m_optimal_block_coarse->Bind(wxEVT_COMBOBOX, [this](auto& e) {
        if (m_filament_presets.begin()->second) {
            DynamicPrintConfig& filament_config = m_filament_presets.begin()->second->config;
            auto curr_flow_ratio = filament_config.option<ConfigOptionFloats>("filament_flow_ratio")->get_at(0);
            m_coarse_calc_result = curr_flow_ratio * (100.0f + stof(m_optimal_block_coarse->GetValue().ToStdString())) / 100.0f;
            m_coarse_calc_result_text->SetLabel(wxString::Format(_L("flow ratio : %s "), std::to_string(m_coarse_calc_result)));
        }
        });
    m_optimal_block_fine->Bind(wxEVT_COMBOBOX, [this](auto& e) {
        if (m_filament_presets.begin()->second) {
            DynamicPrintConfig& filament_config = m_filament_presets.begin()->second->config;
            auto curr_flow_ratio = filament_config.option<ConfigOptionFloats>("filament_flow_ratio")->get_at(0);
            m_fine_calc_result = curr_flow_ratio * (100.0f + stof(m_optimal_block_fine->GetValue().ToStdString())) / 100.0f;
            m_fine_calc_result_text->SetLabel(wxString::Format(_L("flow ratio : %s "), std::to_string(m_fine_calc_result)));
        }
        });
}

void FlowRateWizard::create_high_end_pages() {
    // page 3 : save fine result
    m_high_end_page3 = new CalibrationWizardPage(m_scrolledWindow, false);
    m_high_end_page3->set_page_title(_L("Flow Rate"));
    m_high_end_page3->set_page_type(PageType::Save);
    m_high_end_page3->set_highlight_step_text(PageType::Save);

    auto high_end_page3_content_sizer = m_high_end_page3->get_content_vsizer();
    auto complete_text_panel = new wxPanel(m_high_end_page3, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    wxBoxSizer* complete_text_sizer = new wxBoxSizer(wxVERTICAL);
    auto complete_text = new wxStaticText(complete_text_panel, wxID_ANY, _L("We found the best flow ratio for you"));
    complete_text->SetFont(Label::Head_14);
    complete_text->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    complete_text_sizer->Add(complete_text, 0, wxALIGN_CENTER);
    complete_text_panel->SetSizer(complete_text_sizer);
    high_end_page3_content_sizer->Add(complete_text_panel, 0, wxALIGN_CENTER, 0);

    high_end_page3_content_sizer->AddSpacer(FromDIP(20));

    m_grid_panel = new wxPanel(m_high_end_page3, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    high_end_page3_content_sizer->Add(m_grid_panel, 0, wxALIGN_CENTER);

    auto high_end_page3_prev_btn = m_high_end_page3->get_prev_btn();
    high_end_page3_prev_btn->SetLabel(_L("Restart"));
    high_end_page3_prev_btn->SetButtonType(ButtonType::Restart);

    auto high_end_page3_next_btn = m_high_end_page3->get_next_btn();
    high_end_page3_next_btn->SetLabel(_L("Save"));
    high_end_page3_next_btn->SetButtonType(ButtonType::Save);

    m_all_pages_sizer->Add(m_high_end_page3, 1, wxEXPAND | wxALL, FromDIP(25));

    m_high_end_page3->Bind(wxEVT_SHOW, [this](auto&) {
        sync_save_page_data();
        });
    // link page
    m_page2->chain(m_high_end_page3);
    show_page(m_curr_page);
}

CalibrationWizardPage* FlowRateWizard::create_start_page()
{
    auto page = new CalibrationWizardPage(m_scrolledWindow);
    page->set_page_type(PageType::Start);
    page->set_highlight_step_text(PageType::Start);
    auto page_content_sizer = page->get_content_vsizer();

    auto when_title = new wxStaticText(page, wxID_ANY, _L("When you need Flow Rate Calibration"));
    when_title->SetFont(Label::Head_14);
    when_title->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(when_title);
    auto when_text = new wxStaticText(page, wxID_ANY, _L("Over-extrusion or under extrusion"));
    when_text->SetFont(Label::Body_14);
    when_text->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(when_text);

    page_content_sizer->AddSpacer(PRESET_GAP);

    auto recommend_title = new wxStaticText(page, wxID_ANY, _L("Flow Rate calibration is recommended when you print with:"));
    recommend_title->SetFont(Label::Head_14);
    recommend_title->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(recommend_title);
    auto recommend_text1 = new wxStaticText(page, wxID_ANY, _L("material with significant thermal shrinkage/expansion, such as..."));
    recommend_text1->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    recommend_text1->SetFont(Label::Body_14);
    page_content_sizer->Add(recommend_text1);
    auto recommend_text2 = new wxStaticText(page, wxID_ANY, _L("materials with inaccurate filament diameter"));
    recommend_text2->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    recommend_text2->SetFont(Label::Body_14);
    page_content_sizer->Add(recommend_text2);

    page_content_sizer->AddSpacer(PRESET_GAP);

    auto bitmap_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto bitmap1 = new wxStaticBitmap(page, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    bitmap1->SetMinSize(wxSize(560, 450));
    bitmap1->SetBackgroundColour(*wxBLACK);
    bitmap_sizer->Add(bitmap1, 0, wxALL, 0);
    bitmap_sizer->AddSpacer(FromDIP(20));
    auto bitmap2 = new wxStaticBitmap(page, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    bitmap2->SetMinSize(wxSize(560, 450));
    bitmap2->SetBackgroundColour(*wxBLACK);
    bitmap_sizer->Add(bitmap2, 0, wxALL, 0);
    page_content_sizer->Add(bitmap_sizer, 0, wxALL, 0);

    page_content_sizer->AddSpacer(PRESET_GAP);

    auto wiki = new wxStaticText(page, wxID_ANY, _L("Wiki")); //todo set wiki url
    wiki->SetFont(Label::Head_14);
    wiki->SetForegroundColour({ 0, 88, 220 });
    wiki->Bind(wxEVT_ENTER_WINDOW, [this, wiki](wxMouseEvent& e) {
        e.Skip();
        SetCursor(wxCURSOR_HAND);
        });
    wiki->Bind(wxEVT_LEAVE_WINDOW, [this, wiki](wxMouseEvent& e) {
        e.Skip();
        SetCursor(wxCURSOR_ARROW);
        });
    wiki->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
        if (!m_wiki_url.empty()) wxLaunchDefaultBrowser(m_wiki_url);
        });
    page_content_sizer->Add(wiki, 0);


    page->get_prev_btn()->Hide();

    auto page_next_btn = page->get_next_btn();
    page_next_btn->SetLabel(_L("Start"));
    page_next_btn->SetButtonType(ButtonType::Start);

    return page;
}

void FlowRateWizard::create_pages()
{
    // page 0 : start page
    m_page0 = create_start_page();
    m_page0->set_page_title(_L("Flow Rate"));
    m_all_pages_sizer->Add(m_page0, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 1 : preset page
    m_page1 = create_presets_page(false);
    m_page1->set_page_title(_L("Flow Rate"));

    // Hide
    {
        auto page1_content_sizer = m_page1->get_content_vsizer();
        m_choose_step_panel = new wxPanel(m_page1);
        auto choose_step_sizer = new wxBoxSizer(wxVERTICAL);
        m_choose_step_panel->SetSizer(choose_step_sizer);
        m_complete_radioBox = new wxRadioButton(m_choose_step_panel, wxID_ANY, _L("Complete Calibration"));
        m_complete_radioBox->SetValue(true);
        choose_step_sizer->Add(m_complete_radioBox);
        choose_step_sizer->AddSpacer(FromDIP(10));
        m_fine_radioBox = new wxRadioButton(m_choose_step_panel, wxID_ANY, _L("Fine Calibration based on flow ratio"));
        choose_step_sizer->Add(m_fine_radioBox);
        choose_step_sizer->AddSpacer(FromDIP(10));
        TextInput* flow_ratio_input = new TextInput(m_choose_step_panel, wxEmptyString,"", "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE);
        flow_ratio_input->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
        float default_flow_ratio = 1.0f;
        auto flow_ratio_str = wxString::Format("%.3f", default_flow_ratio);
        flow_ratio_input->GetTextCtrl()->SetValue(flow_ratio_str);
        flow_ratio_input->Hide();
        choose_step_sizer->Add(flow_ratio_input, 0, wxLEFT, FromDIP(18));
        flow_ratio_input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this, flow_ratio_input](auto& e) {
            float flow_ratio = 0.0f;
            validate_input_flow_ratio(flow_ratio_input->GetTextCtrl()->GetValue(), &flow_ratio);
            auto flow_ratio_str = wxString::Format("%.3f", flow_ratio);
            flow_ratio_input->GetTextCtrl()->SetValue(flow_ratio_str);
            m_coarse_calc_result = flow_ratio;
            });
        flow_ratio_input->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this, flow_ratio_input](auto& e) {
            float flow_ratio = 0.0f;
            validate_input_flow_ratio(flow_ratio_input->GetTextCtrl()->GetValue(), &flow_ratio);
            auto flow_ratio_str = wxString::Format("%.3f", flow_ratio);
            flow_ratio_input->GetTextCtrl()->SetValue(flow_ratio_str);
            m_coarse_calc_result = flow_ratio;
            e.Skip();
            });
        m_page1->Bind(wxEVT_LEFT_DOWN, [this](auto&e) {
            m_page1->SetFocusIgnoringChildren();
            });
        m_complete_radioBox->Bind(wxEVT_RADIOBUTTON, [this, flow_ratio_input](auto& e) {
            flow_ratio_input->Show(false);
            this->Layout();
            });
        m_fine_radioBox->Bind(wxEVT_RADIOBUTTON, [this, flow_ratio_input](auto& e) {
            flow_ratio_input->Show();
            this->Layout();
            });

        page1_content_sizer->AddSpacer(PRESET_GAP);
        page1_content_sizer->Add(m_choose_step_panel, 0);
    }
    // Hide

    m_all_pages_sizer->Add(m_page1, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 2 : print page
    m_page2 = create_print_page();
    m_page2->set_page_title(_L("Flow Rate"));
    m_all_pages_sizer->Add(m_page2, 1, wxEXPAND | wxALL, FromDIP(25));

    m_page0->chain(m_page1)->chain(m_page2);
    m_first_page = m_page0;
    m_curr_page = m_page0;
    show_page(m_curr_page);

    create_low_end_pages();
}

void FlowRateWizard::request_calib_result() {

    if (is_high_end_type(curr_obj)) {
        if (is_first_time_get_result) {
            curr_obj->has_get_flow_ratio_result = false;
            CalibUtils::emit_get_flow_ratio_calib_results(curr_obj->nozzle_diameter);
            is_first_time_get_result = false;
        }
        if (curr_obj->has_get_flow_ratio_result) {
            if (!has_get_result) {
                CalibUtils::get_flow_ratio_calib_results(m_calib_results);
                if (m_calib_results.size() > 0) {
                    has_get_result = true;
                    sync_save_page_data();
                }
            }
        }
    }
}

void FlowRateWizard::sync_save_page_data() {
    m_high_end_save_names.clear();
    m_grid_panel->DestroyChildren();
    wxBoxSizer* grid_sizer = new wxBoxSizer(wxHORIZONTAL);
    const int COLUMN_GAP = FromDIP(50);
    const int ROW_GAP = FromDIP(30);
    wxBoxSizer* left_title_sizer = new wxBoxSizer(wxVERTICAL);
    left_title_sizer->AddSpacer(FromDIP(49));
    auto flow_ratio_title = new wxStaticText(m_grid_panel, wxID_ANY, _L("Flow Ratio"), wxDefaultPosition, wxDefaultSize, 0);
    flow_ratio_title->SetFont(Label::Head_14);
    left_title_sizer->Add(flow_ratio_title, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);
    auto brand_title = new wxStaticText(m_grid_panel, wxID_ANY, _L("Brand Name"), wxDefaultPosition, wxDefaultSize, 0);
    brand_title->SetFont(Label::Head_14);
    left_title_sizer->Add(brand_title, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);
    grid_sizer->Add(left_title_sizer);
    grid_sizer->AddSpacer(COLUMN_GAP);

    FilamentComboBoxList fcb_list = get_selected_filament_comboBox();
    for (auto& fcb : fcb_list) {
        bool result_failed = false;
        auto it_result = std::find_if(m_calib_results.begin(), m_calib_results.end(), [fcb](auto& calib_result) {
            return calib_result.tray_id == fcb->get_tray_id();
            });
        if (it_result != m_calib_results.end()) {
            result_failed = false;
        }
        else {
            result_failed = true;
        }
        int index = it_result - m_calib_results.begin();

        wxBoxSizer* column_data_sizer = new wxBoxSizer(wxVERTICAL);
        auto tray_title = new wxStaticText(m_grid_panel, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0);
        tray_title->SetFont(Label::Head_14);
        tray_title->SetLabel(fcb->get_tray_name());
        column_data_sizer->Add(tray_title, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);

        auto flow_ratio_value = new TextInput(m_grid_panel, NA_STR, "", "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, wxTE_PROCESS_ENTER);
        auto flow_ratio_value_failed = new wxStaticText(m_grid_panel, wxID_ANY, _L("Failed"), wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE);
        flow_ratio_value->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
        if (!result_failed) {
            auto flow_ratio_str = wxString::Format("%.3f", it_result->flow_ratio);
            flow_ratio_value->GetTextCtrl()->SetValue(flow_ratio_str);
        }
        flow_ratio_value->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this, fcb, flow_ratio_value, index](auto& e) {
            float flow_ratio = 0.0f;
            validate_input_flow_ratio(flow_ratio_value->GetTextCtrl()->GetValue(), &flow_ratio);
            auto flow_ratio_str = wxString::Format("%.3f", flow_ratio);
            flow_ratio_value->GetTextCtrl()->SetValue(flow_ratio_str);
            m_calib_results[index].flow_ratio = flow_ratio;
            });
        flow_ratio_value->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this, fcb, flow_ratio_value, index](auto& e) {
            float flow_ratio = 0.0f;
            validate_input_flow_ratio(flow_ratio_value->GetTextCtrl()->GetValue(), &flow_ratio);
            auto flow_ratio_str = wxString::Format("%.3f", flow_ratio);
            flow_ratio_value->GetTextCtrl()->SetValue(flow_ratio_str);
            m_calib_results[index].flow_ratio = flow_ratio;
            e.Skip();
            });
        column_data_sizer->Add(flow_ratio_value, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);
        column_data_sizer->Add(flow_ratio_value_failed, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);

        auto save_name_input = new TextInput(m_grid_panel, fcb->GetComboBox()->GetValue() + "-Calibrated", "", "", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE, wxTE_PROCESS_ENTER);
        auto save_name_input_failed = new wxStaticText(m_grid_panel, wxID_ANY, " - ", wxDefaultPosition, CALIBRATION_FROM_TO_INPUT_SIZE);

        auto set_edit_mode = [this, flow_ratio_value, flow_ratio_value_failed, save_name_input, save_name_input_failed](std::string str) {
            if (str == "normal") {
                save_name_input->Show();
                save_name_input_failed->Show(false);
                flow_ratio_value->Show();
                flow_ratio_value_failed->Show(false);
            }
            if (str == "failed") {
                save_name_input->Show(false);
                save_name_input_failed->Show();
                flow_ratio_value->Show(false);
                flow_ratio_value_failed->Show();
            }
            m_grid_panel->Layout();
            m_grid_panel->Update();
        };

        if (!result_failed) {
            m_high_end_save_names[fcb->get_tray_id()] = save_name_input->GetTextCtrl()->GetValue().ToStdString();
            save_name_input->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this, fcb, save_name_input](auto& e) {
                m_high_end_save_names[fcb->get_tray_id()] = save_name_input->GetTextCtrl()->GetValue().ToStdString();
                });
            save_name_input->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this, fcb, save_name_input](auto& e) {
                m_high_end_save_names[fcb->get_tray_id()] = save_name_input->GetTextCtrl()->GetValue().ToStdString();
                e.Skip();
                });
            set_edit_mode("normal");
        }
        else {
            set_edit_mode("failed");
        }
        column_data_sizer->Add(save_name_input, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);
        column_data_sizer->Add(save_name_input_failed, 0, wxALIGN_CENTER | wxBOTTOM, ROW_GAP);

        grid_sizer->Add(column_data_sizer);
        grid_sizer->AddSpacer(COLUMN_GAP);
    }
    m_grid_panel->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {
        m_grid_panel->SetFocusIgnoringChildren();
        });
    m_grid_panel->SetSizer(grid_sizer, true);
    Layout();
}

void FlowRateWizard::switch_pages(SimpleEvent& evt) {
    if (curr_obj) {
        if (is_high_end_type(curr_obj)) {
            m_choose_step_panel->Hide();
        }
        else { 
            m_choose_step_panel->Show(); 
        }

        if (is_high_end_type(curr_obj))
        {
            if (m_low_end_page3) {
                m_low_end_page3->Destroy();
                m_low_end_page3 = nullptr;
            }
            if (m_low_end_page4) {
                m_low_end_page4->Destroy();
                m_low_end_page4 = nullptr;
            }
            if (m_low_end_page5) {
                m_low_end_page5->Destroy();
                m_low_end_page5 = nullptr;
            }
            if (m_high_end_page3) {
                m_high_end_page3->Destroy();
                m_high_end_page3 = nullptr;
            }

            create_high_end_pages();

            sync_save_page_data(); // CALIBRATION_DEBUG
        }
        else
        {
            if (m_high_end_page3) {
                m_high_end_page3->Destroy();
                m_high_end_page3 = nullptr;
            }
            if (m_low_end_page3) {
                m_low_end_page3->Destroy();
                m_low_end_page3 = nullptr;
            }
            if (m_low_end_page4) {
                m_low_end_page4->Destroy();
                m_low_end_page4 = nullptr;
            }
            if (m_low_end_page5) {
                m_low_end_page5->Destroy();
                m_low_end_page5 = nullptr;
            }

            create_low_end_pages();
        }
        Layout();
    }
}

void FlowRateWizard::change_ams_select_mode() {
    //if (is_high_end_type(curr_obj)) {
    //    set_ams_select_mode(FSMCheckBoxMode);
    //}
    //else {
    //    set_ams_select_mode(FSMRadioMode);
    //}
    set_ams_select_mode(FSMRadioMode);
}

bool FlowRateWizard::start_calibration(std::vector<int> tray_ids)
{
    int nozzle_temp = -1;
    int bed_temp = -1;
    float max_volumetric_speed = -1;

    if (m_nozzle_temp->GetTextCtrl()->GetValue().IsEmpty() ||
        m_bed_temp->GetTextCtrl()->GetValue().IsEmpty() ||
        m_max_volumetric_speed->GetTextCtrl()->GetValue().IsEmpty())
        return false;
    nozzle_temp = stoi(m_nozzle_temp->GetTextCtrl()->GetValue().ToStdString());
    bed_temp = stoi(m_bed_temp->GetTextCtrl()->GetValue().ToStdString());
    max_volumetric_speed = stof(m_max_volumetric_speed->GetTextCtrl()->GetValue().ToStdString());

    if (bed_temp < 0 || nozzle_temp < 0 || max_volumetric_speed < 0) {
        MessageDialog msg_dlg(nullptr, _L("Make sure bed_temp > 0 \nnozzle_temp > 0\nmax_volumetric_speed > 0"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    if (is_high_end_type(curr_obj)) {
        m_calib_results.clear();
        X1CCalibInfos calib_infos;
        for (int tray_id : tray_ids) {
            if (m_filament_presets.find(tray_id) == m_filament_presets.end())
                return false;
            X1CCalibInfos::X1CCalibInfo calib_info;
            calib_info.tray_id = tray_id;
            calib_info.nozzle_diameter = dynamic_cast<ConfigOptionFloats *>(m_printer_preset->config.option("nozzle_diameter"))->get_at(0);
            calib_info.filament_id = m_filament_presets.at(tray_id)->filament_id;
            calib_info.setting_id = m_filament_presets.at(tray_id)->setting_id;
            calib_info.bed_temp = bed_temp;
            calib_info.nozzle_temp = nozzle_temp;
            calib_info.max_volumetric_speed = max_volumetric_speed;
            calib_info.flow_rate = m_filament_presets.at(tray_id)->config.option<ConfigOptionFloats>("filament_flow_ratio")->get_at(0);
            calib_infos.calib_datas.push_back(calib_info);
        }
        std::string error_message;
        CalibUtils::calib_flowrate_X1C(calib_infos, error_message);
        if (!error_message.empty()) {
            MessageDialog msg_dlg(nullptr, error_message, wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return false;
        }
        is_first_time_get_result = true;
        has_get_result = false;
        show_page(get_curr_page()->get_next_page());
        save_to_printer_calib_info(PageType::Calibration);
        return true;
    }
    else {
        int pass = -1;
        if (m_fine_radioBox->GetValue()) {
            m_curr_page = m_low_end_page3;
            reset_print_panel_to_page(m_low_end_page4, m_low_end_page4->get_content_vsizer());
            DynamicPrintConfig* filament_config = &m_filament_presets.begin()->second->config;
            filament_config->set_key_value("filament_flow_ratio", new ConfigOptionFloats{ m_coarse_calc_result });
        }
        if (get_curr_page() == m_page1) {
            pass = 1;
        }
        else if (get_curr_page() == m_low_end_page3)
            pass = 2;
        else
            return false;

        CalibInfo calib_info;
        calib_info.dev_id = curr_obj->dev_id;
        calib_info.select_ams = "[" + std::to_string(tray_ids[0]) + "]";
        calib_info.process_bar = m_send_progress_bar;
        calib_info.bed_type = BedType(m_comboBox_bed_type->GetSelection() + btDefault + 1);
        calib_info.printer_prest = m_printer_preset;
        calib_info.filament_prest = m_filament_presets.begin()->second;
        calib_info.print_prest = m_print_preset;

        std::string error_message;
        CalibUtils::calib_flowrate(pass, calib_info, error_message);
        if (!error_message.empty()) {
            MessageDialog msg_dlg(nullptr, error_message, wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return false;
        }
        show_send_progress_bar(true);
        if (pass == 1)
            save_to_printer_calib_info(PageType::Calibration);
        else if(pass == 2)
            save_to_printer_calib_info(PageType::FineCalibration);
        return true;
    }
}

bool FlowRateWizard::save_calibration_result()
{
    if (is_high_end_type(curr_obj)) {
        for (auto& result : m_calib_results) {
            if (result.flow_ratio <= 0 || result.flow_ratio >= 2) {
                MessageDialog msg_dlg(nullptr, _L("Please input a valid value (0.0 < flow ratio < 0.2)"), wxEmptyString, wxICON_WARNING | wxOK);
                msg_dlg.ShowModal();
                return false;
            }
        }
        for (int i = 0; i < m_calib_results.size(); i++) {
            auto it = m_high_end_save_names.find(m_calib_results[i].tray_id);
            if (it != m_high_end_save_names.end() && !it->second.empty()) {
                if (m_filament_presets.find(m_calib_results[i].tray_id) == m_filament_presets.end())
                    return false;
                std::string message;
                if(save_presets(m_filament_presets.at(m_calib_results[i].tray_id), "filament_flow_ratio", new ConfigOptionFloats{ m_calib_results[i].flow_ratio }, it->second, message))
                    return true;
                else {
                    MessageDialog msg_dlg(nullptr, _L(message), wxEmptyString, wxICON_WARNING | wxOK);
                    msg_dlg.ShowModal();
                    return false;
                }
            }

            if (it != m_high_end_save_names.end() && it->second.empty()) {
                MessageDialog msg_dlg(nullptr, _L("Input name of filament preset to save"), wxEmptyString, wxICON_WARNING | wxOK);
                msg_dlg.ShowModal();
                return false;
            }
        }
        return false;
    }
    else {
        bool valid = true;
        float result_value;
        if (m_fine_radioBox->GetValue()) {
            result_value = m_fine_calc_result;
            if (m_optimal_block_fine->GetValue().IsEmpty())
                valid = false;
        }
        else {
            if (m_checkBox_skip_calibration->GetValue()) {
                result_value = m_coarse_calc_result;
                if (m_optimal_block_coarse->GetValue().IsEmpty())
                    valid = false;
            }
            else {
                result_value = m_fine_calc_result;
                if (m_optimal_block_coarse->GetValue().IsEmpty() || m_optimal_block_fine->GetValue().IsEmpty())
                    valid = false;
            }
        }
        if (!valid)
        {
            MessageDialog msg_dlg(nullptr, _L("Choose a block with smoothest top surface."), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return false;
        }
        if (m_save_name.empty()) {
            MessageDialog msg_dlg(nullptr, _L("Input name of filament preset to save"), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return false;
        }
        std::string message;
        if (!save_presets(m_filament_presets.begin()->second, "filament_flow_ratio", new ConfigOptionFloats{ result_value }, m_save_name, message)) {
            MessageDialog msg_dlg(nullptr, _L(message), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return false;
        }
        reset_reuse_panels();
        return true;
    }
    return false;
}

bool FlowRateWizard::recommend_input_value()
{
    return CalibrationWizard::recommend_input_value();
}

void FlowRateWizard::reset_reuse_panels() {
    reset_send_progress_to_page(m_page1, m_page1->get_btn_hsizer());
    reset_print_panel_to_page(m_page2, m_page2->get_content_vsizer());
}

void FlowRateWizard::reset_print_panel_to_page(CalibrationWizardPage* page, wxBoxSizer* sizer)
{
    m_btn_next = page->get_next_btn();
    m_btn_recali = page->get_prev_btn();
    m_print_panel->Reparent(page);
    sizer->Remove(sizer->GetItemCount() - 1);
    sizer->Add(m_print_panel, 0, wxALIGN_CENTER, 0);
    Layout();
}

void FlowRateWizard::reset_send_progress_to_page(CalibrationWizardPage* page, wxBoxSizer* sizer)
{
    m_send_progress_panel->Reparent(page);
    sizer->Remove(1);
    sizer->Insert(1, m_send_progress_panel, 0, wxALIGN_CENTER, 0);
    Layout();
}

void FlowRateWizard::on_fine_tune(wxCommandEvent& e) {
    if (m_optimal_block_coarse->GetValue().IsEmpty()){
        MessageDialog msg_dlg(nullptr, _L("Choose a block with smoothest top surface."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    DynamicPrintConfig& filament_config = m_filament_presets.begin()->second->config;
    filament_config.set_key_value("filament_flow_ratio", new ConfigOptionFloats{ m_coarse_calc_result });

    reset_send_progress_to_page(m_low_end_page3, m_low_end_page3->get_btn_hsizer());

    reset_print_panel_to_page(m_low_end_page4, m_low_end_page4->get_content_vsizer());

    e.Skip();
}

void FlowRateWizard::init_bitmaps()
{
    m_print_picture->SetBitmap(create_scaled_bitmap("flow_rate_calibration", nullptr, 400));
    m_low_record_picture1->SetMinSize(wxSize(560, 450));
    m_low_record_picture1->SetBackgroundColour(*wxBLACK);
    m_low_print_picture2->SetBitmap(create_scaled_bitmap("flow_rate_calibration_fine", nullptr, 400));
    m_low_record_picture2->SetMinSize(wxSize(560, 450));
    m_low_record_picture2->SetBackgroundColour(*wxBLACK);
}

MaxVolumetricSpeedWizard::MaxVolumetricSpeedWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, CalibMode::Calib_Vol_speed_Tower, id, pos, size, style)
{
    create_pages();
    init_bitmaps();
    m_optimal_max_speed->GetTextCtrl()->Bind(wxEVT_TEXT, [this](auto& e) {
        if (!m_optimal_max_speed->GetTextCtrl()->GetValue().IsEmpty()) {
            m_calc_result = stof(m_optimal_max_speed->GetTextCtrl()->GetValue().ToStdString()); // todo determine to select an object or input target value directly
            m_calc_result_text->SetLabel(wxString::Format(_L("max volumetric speed : %s "), std::to_string(m_calc_result)));
        }
        });
}

void MaxVolumetricSpeedWizard::set_save_name() {
    if (m_filament_presets.begin()->second) {
        m_save_name = m_filament_presets.begin()->second->alias + "-Calibrated";
    }
    else { m_save_name = ""; }
    m_save_name_input->GetTextCtrl()->SetValue(m_save_name);
}

CalibrationWizardPage* MaxVolumetricSpeedWizard::create_start_page()
{
    auto page = new CalibrationWizardPage(m_scrolledWindow);
    page->set_page_type(PageType::Start);
    page->set_highlight_step_text(PageType::Start);
    auto page_content_sizer = page->get_content_vsizer();

    auto when_title = new wxStaticText(page, wxID_ANY, _L("When you need Max Volumetric Speed Calibration"));
    when_title->SetFont(Label::Head_14);
    when_title->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(when_title);
    auto when_text = new wxStaticText(page, wxID_ANY, _L("Under-extrusion"));
    when_text->SetFont(Label::Body_14);
    when_text->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(when_text);

    page_content_sizer->AddSpacer(PRESET_GAP);

    auto explain_title = new wxStaticText(page, wxID_ANY, _L("What will happen if the Max Volumetric Speed is not set up properly"));
    explain_title->SetFont(Label::Head_14);
    explain_title->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(explain_title);
    auto explain_text1 = new wxStaticText(page, wxID_ANY, _L("Under-extrusion: If the value is set too high, under-extrusion will happen and cause poor apperance on the printed model"));
    explain_text1->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    explain_text1->SetFont(Label::Body_14);
    page_content_sizer->Add(explain_text1);
    auto explain_text2 = new wxStaticText(page, wxID_ANY, _L("Print speed is limited: If the value is set too low, the print speed will be limited and make the print time longer. Take the model on the right picture for example. max volumetric speed [n] mm^3/s costs [x] minutes. max volumetric speed [m] mm^3/s costs [y] minutes"));
    explain_text2->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    explain_text2->SetFont(Label::Body_14);
    page_content_sizer->Add(explain_text2);

    page_content_sizer->AddSpacer(PRESET_GAP);

    auto bitmap_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto bitmap1 = new wxStaticBitmap(page, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    bitmap1->SetMinSize(wxSize(560, 450));
    bitmap1->SetBackgroundColour(*wxBLACK);
    bitmap_sizer->Add(bitmap1, 0, wxALL, 0);
    bitmap_sizer->AddSpacer(FromDIP(20));
    auto bitmap2 = new wxStaticBitmap(page, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    bitmap2->SetMinSize(wxSize(560, 450));
    bitmap2->SetBackgroundColour(*wxBLACK);
    bitmap_sizer->Add(bitmap2, 0, wxALL, 0);
    page_content_sizer->Add(bitmap_sizer, 0, wxALL, 0);

    page_content_sizer->AddSpacer(PRESET_GAP);

    auto wiki = new wxStaticText(page, wxID_ANY, _L("Wiki"));
    wiki->SetFont(Label::Head_14);
    wiki->SetForegroundColour({ 0, 88, 220 });
    wiki->Bind(wxEVT_ENTER_WINDOW, [this, wiki](wxMouseEvent& e) {
        e.Skip();
        SetCursor(wxCURSOR_HAND);
        });
    wiki->Bind(wxEVT_LEAVE_WINDOW, [this, wiki](wxMouseEvent& e) {
        e.Skip();
        SetCursor(wxCURSOR_ARROW);
        });
    wiki->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
        if (!m_wiki_url.empty()) wxLaunchDefaultBrowser(m_wiki_url);
        });
    page_content_sizer->Add(wiki, 0);

    page->get_prev_btn()->Hide();

    auto page_next_btn = page->get_next_btn();
    page_next_btn->SetLabel(_L("Start"));
    page_next_btn->SetButtonType(ButtonType::Start);

    return page;
}

void MaxVolumetricSpeedWizard::create_save_panel_content(wxBoxSizer* sizer)
{
    auto complete_text = new wxStaticText(m_save_panel, wxID_ANY, _L("Please find the best object on your plate"), wxDefaultPosition, wxDefaultSize, 0);
    complete_text->SetFont(::Label::Head_14);
    complete_text->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    sizer->Add(complete_text, 0, 0, 0);

    sizer->AddSpacer(FromDIP(20));

    m_record_picture = new wxStaticBitmap(m_save_panel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    sizer->Add(m_record_picture, 0, wxALIGN_CENTER, 0);

    sizer->AddSpacer(FromDIP(20));

    auto value_sizer = new wxBoxSizer(wxVERTICAL);
    auto value_text = new wxStaticText(m_save_panel, wxID_ANY, _L("Input Value"), wxDefaultPosition, wxDefaultSize, 0);
    value_text->SetFont(Label::Head_14);
    value_text->Wrap(-1);
    m_optimal_max_speed = new TextInput(m_save_panel, wxEmptyString, "", "", wxDefaultPosition, CALIBRATION_OPTIMAL_INPUT_SIZE, wxTE_PROCESS_ENTER);
    m_optimal_max_speed->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_calc_result_text = new wxStaticText(m_save_panel, wxID_ANY, "");
    value_sizer->Add(value_text, 0, 0, 0);
    value_sizer->Add(m_optimal_max_speed, 0);
    value_sizer->Add(m_calc_result_text, 0);
    sizer->Add(value_sizer, 0, 0);

    sizer->AddSpacer(FromDIP(20));

    auto save_text = new wxStaticText(m_save_panel, wxID_ANY, _L("Save to Filament Preset"), wxDefaultPosition, wxDefaultSize, 0);
    save_text->Wrap(-1);
    save_text->SetFont(Label::Head_14);
    sizer->Add(save_text, 0, 0, 0);

    m_save_name_input = new TextInput(m_save_panel, m_save_name, "", "", wxDefaultPosition, { CALIBRATION_TEXT_MAX_LENGTH, FromDIP(24) }, 0);
    sizer->Add(m_save_name_input, 0, 0, 0);
    m_save_name_input->GetTextCtrl()->Bind(wxEVT_TEXT, [this](auto& e) {
        if (!m_save_name_input->GetTextCtrl()->GetValue().IsEmpty())
            m_save_name = m_save_name_input->GetTextCtrl()->GetValue().ToStdString();
        else
            m_save_name = "";
        });
}

void MaxVolumetricSpeedWizard::create_pages() 
{
    // page 0 : start page
    m_page0 = create_start_page();
    m_page0->set_page_title(_L("Max Volumetric Speed"));
    m_all_pages_sizer->Add(m_page0, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 1 : preset page
    m_page1 = create_presets_page(true);
    m_page1->set_page_title(_L("Max Volumetric Speed"));

    m_from_text->SetLabel(_L("From Volumetric Speed"));
    m_to_text->SetLabel(_L("To Volumetric Speed"));
    m_from_value->SetLabel(_L("mm\u00B3/s"));
    m_to_value->SetLabel(_L("mm\u00B3/s"));
    m_step->SetLabel(_L("mm\u00B3/s"));
    auto step_str = wxString::Format("%1.1f", 0.5f);
    m_step->GetTextCtrl()->SetValue(step_str);

    m_all_pages_sizer->Add(m_page1, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 2 : print page
    m_page2 = create_print_page();
    m_page2->set_page_title(_L("Max Volumetric Speed"));
    m_all_pages_sizer->Add(m_page2, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 3 : save page
    m_page3 = create_save_page();
    m_page3->set_page_title(_L("Max Volumetric Speed"));
    m_all_pages_sizer->Add(m_page3, 1, wxEXPAND | wxALL, FromDIP(25));

    // link page
    m_page0->chain(m_page1)->chain(m_page2)->chain(m_page3);

    m_first_page = m_page0;
    m_curr_page = m_page0;
    show_page(m_curr_page);
}

bool MaxVolumetricSpeedWizard::start_calibration(std::vector<int> tray_ids)
{
    Calib_Params params;
    m_from_value->GetTextCtrl()->GetValue().ToDouble(&params.start);
    m_to_value->GetTextCtrl()->GetValue().ToDouble(&params.end);
    m_step->GetTextCtrl()->GetValue().ToDouble(&params.step);
    params.mode = CalibMode::Calib_Vol_speed_Tower;

    if (params.start <= 0 || params.step <= 0 || params.end < (params.start + params.step) || params.end > 60) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nFrom > 0\nTo < 60\nStep >= 0\nTo > From + Step"), wxEmptyString, wxICON_WARNING | wxOK);
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
    calib_info.filament_prest = m_filament_presets.begin()->second;
    calib_info.print_prest    = m_print_preset;

    std::string error_message;
    CalibUtils::calib_max_vol_speed(calib_info, error_message);
    if (!error_message.empty()) {
        MessageDialog msg_dlg(nullptr, _L(error_message), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }
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
    if (m_save_name.empty()) {
        MessageDialog msg_dlg(nullptr, _L("Input name of filament preset to save"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }
    std::string message;
    if (!save_presets(m_filament_presets.begin()->second, "filament_max_volumetric_speed", new ConfigOptionFloats{ max_volumetric_speed }, m_save_name, message)) {
        MessageDialog msg_dlg(nullptr, _L(message), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }
    return true;
}

bool MaxVolumetricSpeedWizard::recommend_input_value()
{
    if (!CalibrationWizard::recommend_input_value()) {
        m_from_value->GetTextCtrl()->SetValue(wxEmptyString);
        m_to_value->GetTextCtrl()->SetValue(wxEmptyString);
        m_step->GetTextCtrl()->SetValue(wxEmptyString);
        return false;
    }
    else {
        m_from_value->GetTextCtrl()->SetValue("5");
        m_to_value->GetTextCtrl()->SetValue("20");
        auto step_str = wxString::Format("%1.1f", 0.5f);
        m_step->GetTextCtrl()->SetValue(step_str);
        return true;
    }
}

void MaxVolumetricSpeedWizard::init_bitmaps()
{
    m_print_picture->SetBitmap(create_scaled_bitmap("max_volumetric_speed_calibration", nullptr, 400));
    m_record_picture->SetMinSize(wxSize(500, 400));
    m_record_picture->SetBackgroundColour(*wxBLACK);
}

TemperatureWizard::TemperatureWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, CalibMode::Calib_Temp_Tower, id, pos, size, style)
{
    create_pages();
    init_bitmaps();
}

void TemperatureWizard::set_save_name() {
    if (m_filament_presets.begin()->second) {
        m_save_name = m_filament_presets.begin()->second->alias + "-Calibrated";
    }
    else { m_save_name = ""; }
    m_save_name_input->GetTextCtrl()->SetValue(m_save_name);
}

CalibrationWizardPage* TemperatureWizard::create_start_page()
{
    auto page = new CalibrationWizardPage(m_scrolledWindow);
    page->set_page_type(PageType::Start);
    page->set_highlight_step_text(PageType::Start);
    auto page_content_sizer = page->get_content_vsizer();

    auto when_title = new wxStaticText(page, wxID_ANY, _L("When you need Temperature Calibration"));
    when_title->SetFont(Label::Head_14);
    when_title->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(when_title);
    auto when_text1 = new wxStaticText(page, wxID_ANY, _L("Model stringing"));
    when_text1->SetFont(Label::Body_14);
    when_text1->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(when_text1);
    auto when_text2 = new wxStaticText(page, wxID_ANY, _L("Layer adhesion problem"));
    when_text2->SetFont(Label::Body_14);
    when_text2->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(when_text2);
    auto when_text3 = new wxStaticText(page, wxID_ANY, _L("Warping(overhang)"));
    when_text3->SetFont(Label::Body_14);
    when_text3->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(when_text3);
    auto when_text4 = new wxStaticText(page, wxID_ANY, _L("Bridging"));
    when_text4->SetFont(Label::Body_14);
    when_text4->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(when_text4);

    page_content_sizer->AddSpacer(PRESET_GAP);

    auto bitmap_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto bitmap1 = new wxStaticBitmap(page, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    bitmap1->SetMinSize(wxSize(560, 450));
    bitmap1->SetBackgroundColour(*wxBLACK);
    bitmap_sizer->Add(bitmap1, 0, wxALL, 0);
    bitmap_sizer->AddSpacer(FromDIP(20));
    auto bitmap2 = new wxStaticBitmap(page, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    bitmap2->SetMinSize(wxSize(560, 450));
    bitmap2->SetBackgroundColour(*wxBLACK);
    bitmap_sizer->Add(bitmap2, 0, wxALL, 0);
    page_content_sizer->Add(bitmap_sizer, 0, wxALL, 0);

    page_content_sizer->AddSpacer(PRESET_GAP);

    auto wiki = new wxStaticText(page, wxID_ANY, _L("Wiki"));
    wiki->SetFont(Label::Head_14);
    wiki->SetForegroundColour({ 0, 88, 220 });
    wiki->Bind(wxEVT_ENTER_WINDOW, [this, wiki](wxMouseEvent& e) {
        e.Skip();
        SetCursor(wxCURSOR_HAND);
        });
    wiki->Bind(wxEVT_LEAVE_WINDOW, [this, wiki](wxMouseEvent& e) {
        e.Skip();
        SetCursor(wxCURSOR_ARROW);
        });
    wiki->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
        if (!m_wiki_url.empty()) wxLaunchDefaultBrowser(m_wiki_url);
        });
    page_content_sizer->Add(wiki, 0);


    page->get_prev_btn()->Hide();

    auto page_next_btn = page->get_next_btn();
    page_next_btn->SetLabel(_L("Start"));
    page_next_btn->SetButtonType(ButtonType::Start);

    return page;
}

void TemperatureWizard::create_save_panel_content(wxBoxSizer* sizer)
{
    auto complete_text = new wxStaticText(m_save_panel, wxID_ANY, _L("Please find the best object on your plate"), wxDefaultPosition, wxDefaultSize, 0);
    complete_text->SetFont(::Label::Head_14);
    complete_text->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    sizer->Add(complete_text, 0, 0, 0);
    sizer->AddSpacer(FromDIP(20));

    m_record_picture = new wxStaticBitmap(m_save_panel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    sizer->Add(m_record_picture, 0, wxALIGN_CENTER, 0);
    sizer->AddSpacer(FromDIP(20));

    auto optimal_temp_text = new wxStaticText(m_save_panel, wxID_ANY, _L("Optimal Temp"), wxDefaultPosition, wxDefaultSize, 0);
    optimal_temp_text->Wrap(-1);
    optimal_temp_text->SetFont(Label::Head_14);
    sizer->Add(optimal_temp_text, 0, 0, 0);

    m_optimal_temp = new TextInput(m_save_panel, wxEmptyString, _L("\u2103"), "", wxDefaultPosition, CALIBRATION_OPTIMAL_INPUT_SIZE, 0);
    m_optimal_temp->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    sizer->Add(m_optimal_temp, 0, 0, 0);

    sizer->AddSpacer(FromDIP(20));

    auto save_text = new wxStaticText(m_save_panel, wxID_ANY, _L("Save to Filament Preset"), wxDefaultPosition, wxDefaultSize, 0);
    save_text->Wrap(-1);
    save_text->SetFont(Label::Head_14);
    sizer->Add(save_text, 0, 0, 0);

    m_save_name_input = new TextInput(m_save_panel, m_save_name, "", "", wxDefaultPosition, { CALIBRATION_TEXT_MAX_LENGTH, FromDIP(24) }, 0);
    sizer->Add(m_save_name_input, 0, 0, 0);
    m_save_name_input->GetTextCtrl()->Bind(wxEVT_TEXT, [this](auto& e) {
        if (!m_save_name_input->GetTextCtrl()->GetValue().IsEmpty())
            m_save_name = m_save_name_input->GetTextCtrl()->GetValue().ToStdString();
        else
            m_save_name = "";
        });
}

void TemperatureWizard::create_pages()
{
    // page 0 : start page
    m_page0 = create_start_page();
    m_page0->set_page_title(_L("Temperature Calibration"));
    m_all_pages_sizer->Add(m_page0, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 1 : preset page
    m_page1 = create_presets_page(true);
    m_page1->set_page_title(_L("Temperature Calibration"));

    m_from_text->SetLabel(_L("From Temp"));
    m_to_text->SetLabel(_L("To Temp"));
    m_step->Enable(false);

    m_all_pages_sizer->Add(m_page1, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 2 : print page
    m_page2 = create_print_page();
    m_page2->set_page_title(_L("Temperature Calibration"));

    m_all_pages_sizer->Add(m_page2, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 3 : save page
    m_page3 = create_save_page();
    m_page3->set_page_title(_L("Temperature Calibration"));
    m_all_pages_sizer->Add(m_page3, 1, wxEXPAND | wxALL, FromDIP(25));
    

    // link pages
    m_page0->chain(m_page1)->chain(m_page2)->chain(m_page3);

    m_first_page = m_page0;
    m_curr_page = m_page0;
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
    calib_info.filament_prest = m_filament_presets.begin()->second;
    calib_info.print_prest    = m_print_preset;

    std::string error_message;
    CalibUtils::calib_temptue(calib_info, error_message);
    if (!error_message.empty()) {
        MessageDialog msg_dlg(nullptr, _L(error_message), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }
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
    if (m_save_name.empty()) {
        MessageDialog msg_dlg(nullptr, _L("Input name of filament preset to save"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }
    std::string message;
    if (!save_presets(m_filament_presets.begin()->second, "nozzle_temperature", new ConfigOptionInts(1, temp), m_save_name, message)) {
        MessageDialog msg_dlg(nullptr, _L(message), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }
    return true;
}

bool TemperatureWizard::recommend_input_value()
{
    if (!CalibrationWizard::recommend_input_value()) {
        m_from_value->GetTextCtrl()->SetValue(wxEmptyString);
        m_to_value->GetTextCtrl()->SetValue(wxEmptyString);
        m_step->GetTextCtrl()->SetValue(wxEmptyString);
        return false;
    }
    else {
        wxString filament_name = m_filament_presets.begin()->second->alias;

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
        m_step->GetTextCtrl()->SetValue("5");

        return true;
    }
}

void TemperatureWizard::init_bitmaps()
{
    m_print_picture->SetBitmap(create_scaled_bitmap("temperature_calibration", nullptr, 400));
    m_record_picture->SetBitmap(create_scaled_bitmap("temperature_record", nullptr, 400));
}

RetractionWizard::RetractionWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, CalibMode::Calib_Retraction_tower, id, pos, size, style)
{
    create_pages();
    init_bitmaps();
}

void RetractionWizard::set_save_name() {
    if (m_filament_presets.begin()->second) {
        m_save_name = m_filament_presets.begin()->second->alias + "-Calibrated";
    }
    else { m_save_name = ""; }
    m_save_name_input->GetTextCtrl()->SetValue(m_save_name);
}


void RetractionWizard::create_save_panel_content(wxBoxSizer* sizer)
{
    auto complete_text = new wxStaticText(m_save_panel, wxID_ANY, _L("Please find the best object on your plate"), wxDefaultPosition, wxDefaultSize, 0);
    complete_text->SetFont(::Label::Head_14);
    complete_text->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    sizer->Add(complete_text, 0, 0, 0);
    sizer->AddSpacer(FromDIP(20));

    m_record_picture = new wxStaticBitmap(m_save_panel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    sizer->Add(m_record_picture, 0, wxALIGN_CENTER, 0);
    sizer->AddSpacer(FromDIP(20));

    auto optimal_temp_text = new wxStaticText(m_save_panel, wxID_ANY, _L("Optimal Retraction Length"), wxDefaultPosition, wxDefaultSize, 0);
    optimal_temp_text->Wrap(-1);
    optimal_temp_text->SetFont(Label::Head_14);
    sizer->Add(optimal_temp_text, 0, 0, 0);

    m_optimal_retraction = new TextInput(m_save_panel, wxEmptyString, _L("mm"), "", wxDefaultPosition, CALIBRATION_OPTIMAL_INPUT_SIZE, 0);
    m_optimal_retraction->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    sizer->Add(m_optimal_retraction, 0, 0, 0);

    sizer->AddSpacer(FromDIP(20));

    auto save_text = new wxStaticText(m_save_panel, wxID_ANY, _L("Save to Filament Preset"), wxDefaultPosition, wxDefaultSize, 0);
    save_text->Wrap(-1);
    save_text->SetFont(Label::Head_14);
    sizer->Add(save_text, 0, 0, 0);

    m_save_name_input = new TextInput(m_save_panel, m_save_name, "", "", wxDefaultPosition, { CALIBRATION_TEXT_MAX_LENGTH, FromDIP(24) }, 0);
    sizer->Add(m_save_name_input, 0, 0, 0);
    m_save_name_input->GetTextCtrl()->Bind(wxEVT_TEXT, [this](auto& e) {
        if (!m_save_name_input->GetTextCtrl()->GetValue().IsEmpty())
            m_save_name = m_save_name_input->GetTextCtrl()->GetValue().ToStdString();
        else
            m_save_name = "";
        });
}

CalibrationWizardPage* RetractionWizard::create_start_page()
{
    auto page = new CalibrationWizardPage(m_scrolledWindow);
    page->set_page_type(PageType::Start);
    page->set_highlight_step_text(PageType::Start);
    auto page_content_sizer = page->get_content_vsizer();

    auto when_title = new wxStaticText(page, wxID_ANY, _L("When you need Temperature Calibration"));
    when_title->SetFont(Label::Head_14);
    when_title->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(when_title);
    auto when_text1 = new wxStaticText(page, wxID_ANY, _L("Model stringing"));
    when_text1->SetFont(Label::Body_14);
    when_text1->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(when_text1);
    auto when_text2 = new wxStaticText(page, wxID_ANY, _L("Layer adhesion problem"));
    when_text2->SetFont(Label::Body_14);
    when_text2->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(when_text2);
    auto when_text3 = new wxStaticText(page, wxID_ANY, _L("Warping(overhang)"));
    when_text3->SetFont(Label::Body_14);
    when_text3->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(when_text3);
    auto when_text4 = new wxStaticText(page, wxID_ANY, _L("Bridging"));
    when_text4->SetFont(Label::Body_14);
    when_text4->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    page_content_sizer->Add(when_text4);

    auto bitmap_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto bitmap1 = new wxStaticBitmap(page, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    bitmap1->SetMinSize(wxSize(560, 450));
    bitmap1->SetBackgroundColour(*wxBLACK);
    bitmap_sizer->Add(bitmap1, 0, wxALL, 0);
    bitmap_sizer->AddSpacer(FromDIP(20));
    auto bitmap2 = new wxStaticBitmap(page, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    bitmap2->SetMinSize(wxSize(560, 450));
    bitmap2->SetBackgroundColour(*wxBLACK);
    bitmap_sizer->Add(bitmap2, 0, wxALL, 0);
    page_content_sizer->Add(bitmap_sizer, 0, wxALL, 0);

    page_content_sizer->AddSpacer(PRESET_GAP);

    auto wiki = new wxStaticText(page, wxID_ANY, _L("Wiki"));
    wiki->SetFont(Label::Head_14);
    wiki->SetForegroundColour({ 0, 88, 220 });
    wiki->Bind(wxEVT_ENTER_WINDOW, [this, wiki](wxMouseEvent& e) {
        e.Skip();
        SetCursor(wxCURSOR_HAND);
        });
    wiki->Bind(wxEVT_LEAVE_WINDOW, [this, wiki](wxMouseEvent& e) {
        e.Skip();
        SetCursor(wxCURSOR_ARROW);
        });
    wiki->Bind(wxEVT_LEFT_UP, [this](wxMouseEvent& e) {
        if (!m_wiki_url.empty()) wxLaunchDefaultBrowser(m_wiki_url);
        });
    page_content_sizer->Add(wiki, 0);


    page->get_prev_btn()->Hide();

    auto page_next_btn = page->get_next_btn();
    page_next_btn->SetLabel(_L("Start"));
    page_next_btn->SetButtonType(ButtonType::Start);

    return page;
}

void RetractionWizard::create_pages()
{
    // page 1 : preset page
    m_page1 = create_presets_page(true);
    m_page1->set_page_title(_L("Retraction Calibration"));

    m_from_text->SetLabel(_L("From Retraction Length"));
    m_to_text->SetLabel(_L("To Retraction Length"));
    m_from_value->SetLabel(_L("mm"));
    m_to_value->SetLabel(_L("mm"));
    m_step->SetLabel(_L("mm"));
    auto step_str = wxString::Format("%1.1f", 0.1f);
    m_step->GetTextCtrl()->SetValue(step_str);

    m_all_pages_sizer->Add(m_page1, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 2 : print page
    m_page2 = create_print_page();
    m_page2->set_page_title(_L("Retraction Calibration"));

    m_all_pages_sizer->Add(m_page2, 1, wxEXPAND | wxALL, FromDIP(25));

    // page 3 : save page
    m_page3 = create_save_page();
    m_page3->set_page_title(_L("Retraction Calibration"));
    m_all_pages_sizer->Add(m_page3, 1, wxEXPAND | wxALL, FromDIP(25));


    // link pages
    m_page1->chain(m_page2)->chain(m_page3);

    m_first_page = m_page1;
    m_curr_page = m_page1;
    show_page(m_curr_page);
}

bool RetractionWizard::start_calibration(std::vector<int> tray_ids)
{
    Calib_Params params;
    m_from_value->GetTextCtrl()->GetValue().ToDouble(&params.start);
    m_to_value->GetTextCtrl()->GetValue().ToDouble(&params.end);
    params.mode = CalibMode::Calib_Retraction_tower;

    // todo limit
    //if (params.start < 180 || params.end > 350 || params.end < (params.start + 5) || (params.end - params.start) >= 120) { // todo
    //    MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nFrom temp: >= 180\nTo temp: <= 350\nFrom temp <= To temp - Step\n From temp - To temp < 120"), wxEmptyString, wxICON_WARNING | wxOK);
    //    msg_dlg.ShowModal();
    //    return false;
    //}

    CalibInfo calib_info;
    calib_info.params = params;
    calib_info.dev_id = curr_obj->dev_id;
    calib_info.select_ams = "[" + std::to_string(tray_ids[0]) + "]";
    calib_info.process_bar = m_send_progress_bar;
    calib_info.bed_type = BedType(m_comboBox_bed_type->GetSelection() + btDefault + 1);
    calib_info.printer_prest = m_printer_preset;
    calib_info.filament_prest = m_filament_presets.begin()->second;
    calib_info.print_prest = m_print_preset;

    std::string error_message;
    CalibUtils::calib_retraction(calib_info, error_message);
    if (!error_message.empty()) {
        MessageDialog msg_dlg(nullptr, _L(error_message), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }
    show_send_progress_bar(true);
    return true;
}

bool RetractionWizard::save_calibration_result()
{
    if (m_optimal_retraction->GetTextCtrl()->GetValue().IsEmpty()) // todo need a valid range
    {
        MessageDialog msg_dlg(nullptr, _L("Input an optiaml retraction length."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }
    float length = stof(m_optimal_retraction->GetTextCtrl()->GetValue().ToStdString());
    if (m_save_name.empty()) {
        MessageDialog msg_dlg(nullptr, _L("Input name of filament preset to save"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }
    std::string message;
    if (!save_presets(m_filament_presets.begin()->second, "retraction_length", new ConfigOptionFloats{ length }, m_save_name, message)) {
        MessageDialog msg_dlg(nullptr, _L(message), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }
    return true;
}

bool RetractionWizard::recommend_input_value()
{
    if (!CalibrationWizard::recommend_input_value()) {
        m_from_value->GetTextCtrl()->SetValue(wxEmptyString);
        m_to_value->GetTextCtrl()->SetValue(wxEmptyString);
        m_step->GetTextCtrl()->SetValue(wxEmptyString);
        return false;
    }
    else {
        m_from_value->GetTextCtrl()->SetValue("0");
        m_to_value->GetTextCtrl()->SetValue("2");
        auto step_str = wxString::Format("%1.1f", 0.1f);
        m_step->GetTextCtrl()->SetValue(step_str);
        return true;
    }
}

void RetractionWizard::init_bitmaps()
{
    m_print_picture->SetBitmap(create_scaled_bitmap("temperature_calibration", nullptr, 400));
    m_record_picture->SetBitmap(create_scaled_bitmap("temperature_record", nullptr, 400));
}

}}