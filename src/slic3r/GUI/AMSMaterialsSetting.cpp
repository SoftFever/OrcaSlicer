#include "AMSMaterialsSetting.hpp"
#include "ExtrusionCalibration.hpp"
#include "MsgDialog.hpp"
#include "GUI_App.hpp"
#include "libslic3r/Preset.hpp"
#include "I18N.hpp"

namespace Slic3r { namespace GUI {
static bool show_flag;

AMSMaterialsSetting::AMSMaterialsSetting(wxWindow *parent, wxWindowID id) 
    : DPIDialog(parent, id, _L("AMS Materials Setting"), wxDefaultPosition, wxDefaultSize, wxBORDER_NONE)
{
    create();
    wxGetApp().UpdateDlgDarkUI(this);
}

void AMSMaterialsSetting::create()
{
    SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);

    m_panel_normal = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    create_panel_normal(m_panel_normal);
    m_panel_kn = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    create_panel_kn(m_panel_kn);

    wxBoxSizer *m_sizer_button = new wxBoxSizer(wxHORIZONTAL);

    m_sizer_button->Add(0, 0, 1, wxEXPAND, 0);

    m_button_confirm = new Button(this, _L("Confirm"));
    m_btn_bg_green   = StateColor(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    m_button_confirm->SetBackgroundColor(m_btn_bg_green);
    m_button_confirm->SetBorderColor(wxColour(0, 174, 66));
    m_button_confirm->SetTextColor(wxColour("#FFFFFE"));
    m_button_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_button_confirm->SetCornerRadius(FromDIP(12));
    m_button_confirm->Bind(wxEVT_BUTTON, &AMSMaterialsSetting::on_select_ok, this);

    m_button_close = new Button(this, _L("Close"));
    m_btn_bg_gray  = StateColor(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(*wxWHITE, StateColor::Focused),
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                               std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));
    m_button_close->SetBackgroundColor(m_btn_bg_gray);
    m_button_close->SetBorderColor(AMS_MATERIALS_SETTING_GREY900);
    m_button_close->SetTextColor(AMS_MATERIALS_SETTING_GREY900);
    m_button_close->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_button_close->SetCornerRadius(FromDIP(12));
    m_button_close->Bind(wxEVT_BUTTON, &AMSMaterialsSetting::on_select_close, this);

    m_sizer_button->Add(m_button_confirm, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(20));
    m_sizer_button->Add(m_button_close, 0, wxALIGN_CENTER, 0);

    m_sizer_main->Add(m_panel_normal, 0, wxALL, FromDIP(2));
    
    m_sizer_main->Add(m_panel_kn, 0, wxALL, FromDIP(2));

    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(24));
    m_sizer_main->Add(m_sizer_button, 0,  wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    m_sizer_main->Add(0, 0, 0,  wxTOP, FromDIP(16));

    SetSizer(m_sizer_main);
    Layout();
    Fit();

    m_input_nozzle_min->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) {
        warning_text->Hide();
        Layout();
        Fit();
        e.Skip();
        });
    m_input_nozzle_min->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent& e) {
        input_min_finish();
        e.Skip();
        });
    m_input_nozzle_min->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
        input_min_finish();
        e.Skip();
        });

    m_input_nozzle_max->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent& e) {
        warning_text->Hide();
        Layout();
        Fit();
        e.Skip();
        });
    m_input_nozzle_max->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent& e) {
        input_max_finish();
        e.Skip();
        });
    m_input_nozzle_max->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent& e) {
        input_max_finish();
        e.Skip();
        });

    Bind(wxEVT_PAINT, &AMSMaterialsSetting::paintEvent, this);
     m_comboBox_filament->Connect(wxEVT_COMMAND_COMBOBOX_SELECTED, wxCommandEventHandler(AMSMaterialsSetting::on_select_filament), NULL, this);
}

void AMSMaterialsSetting::create_panel_normal(wxWindow* parent)
{
    auto sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* m_sizer_filament = new wxBoxSizer(wxHORIZONTAL);

    m_title_filament = new wxStaticText(parent, wxID_ANY, _L("Filament"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_filament->SetFont(::Label::Body_13);
    m_title_filament->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_filament->Wrap(-1);
    m_sizer_filament->Add(m_title_filament, 0, wxALIGN_CENTER, 0);

    m_sizer_filament->Add(0, 0, 0, wxEXPAND, 0);

#ifdef __APPLE__
    m_comboBox_filament = new wxComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_COMBOX_WIDTH, 0, nullptr, wxCB_READONLY);
#else
    m_comboBox_filament = new ::ComboBox(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_COMBOX_WIDTH, 0, nullptr, wxCB_READONLY);
#endif

    m_sizer_filament->Add(m_comboBox_filament, 1, wxALIGN_CENTER, 0);

    m_readonly_filament = new TextInput(parent, wxEmptyString, "", "", wxDefaultPosition, AMS_MATERIALS_SETTING_COMBOX_WIDTH, wxTE_READONLY);
    m_readonly_filament->SetBorderColor(StateColor(std::make_pair(0xDBDBDB, (int)StateColor::Focused), std::make_pair(0x00AE42, (int)StateColor::Hovered),
        std::make_pair(0xDBDBDB, (int)StateColor::Normal)));
    m_readonly_filament->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [](auto& e) {
        ;
        });
    m_sizer_filament->Add(m_readonly_filament, 1, wxALIGN_CENTER, 0);
    m_readonly_filament->Hide();

    wxBoxSizer* m_sizer_colour = new wxBoxSizer(wxHORIZONTAL);

    m_title_colour = new wxStaticText(parent, wxID_ANY, _L("Colour"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_colour->SetFont(::Label::Body_13);
    m_title_colour->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_colour->Wrap(-1);
    m_sizer_colour->Add(m_title_colour, 0, wxALIGN_CENTER, 0);

    m_sizer_colour->Add(0, 0, 0, wxEXPAND, 0);

    m_clrData = new wxColourData();
    m_clrData->SetChooseFull(true);
    m_clrData->SetChooseAlpha(false);

    m_clr_picker = new Button(parent, wxEmptyString, wxEmptyString, wxBU_AUTODRAW);
    m_clr_picker->SetCanFocus(false);
    m_clr_picker->SetSize(FromDIP(50), FromDIP(25));
    m_clr_picker->SetMinSize(wxSize(FromDIP(50), FromDIP(25)));
    m_clr_picker->SetCornerRadius(FromDIP(6));
    m_clr_picker->SetBorderColor(wxColour(172, 172, 172));
    m_clr_picker->Bind(wxEVT_BUTTON, &AMSMaterialsSetting::on_clr_picker, this);
    m_sizer_colour->Add(m_clr_picker, 0, 0, 0);

    wxBoxSizer* m_sizer_temperature = new wxBoxSizer(wxHORIZONTAL);
    m_title_temperature = new wxStaticText(parent, wxID_ANY, _L("Nozzle\nTemperature"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_temperature->SetFont(::Label::Body_13);
    m_title_temperature->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_temperature->Wrap(-1);
    m_sizer_temperature->Add(m_title_temperature, 0, wxALIGN_CENTER, 0);

    m_sizer_temperature->Add(0, 0, 0, wxEXPAND, 0);

    wxBoxSizer* sizer_other = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* sizer_tempinput = new wxBoxSizer(wxHORIZONTAL);

    m_input_nozzle_max = new ::TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_INPUT_SIZE, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_input_nozzle_min = new ::TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_INPUT_SIZE, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_input_nozzle_max->Enable(false);
    m_input_nozzle_min->Enable(false);

    m_input_nozzle_max->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_input_nozzle_max->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(20)));
    m_input_nozzle_min->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_input_nozzle_min->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(20)));

    auto bitmap_max_degree = new wxStaticBitmap(parent, -1, create_scaled_bitmap("degree", nullptr, 16), wxDefaultPosition, wxDefaultSize);
    auto bitmap_min_degree = new wxStaticBitmap(parent, -1, create_scaled_bitmap("degree", nullptr, 16), wxDefaultPosition, wxDefaultSize);

    sizer_tempinput->Add(m_input_nozzle_max, 1, wxALIGN_CENTER, 0);
    sizer_tempinput->Add(bitmap_min_degree, 0, wxALIGN_CENTER, 0);
    sizer_tempinput->Add(FromDIP(10), 0, 0, 0);
    sizer_tempinput->Add(m_input_nozzle_min, 1, wxALIGN_CENTER, 0);
    sizer_tempinput->Add(bitmap_max_degree, 0, wxALIGN_CENTER, 0);

    wxBoxSizer* sizer_temp_txt = new wxBoxSizer(wxHORIZONTAL);
    auto        m_title_max = new wxStaticText(parent, wxID_ANY, _L("max"), wxDefaultPosition, AMS_MATERIALS_SETTING_INPUT_SIZE);
    m_title_max->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_max->SetFont(::Label::Body_13);
    auto m_title_min = new wxStaticText(parent, wxID_ANY, _L("min"), wxDefaultPosition, AMS_MATERIALS_SETTING_INPUT_SIZE);
    m_title_min->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_min->SetFont(::Label::Body_13);
    sizer_temp_txt->Add(m_title_max, 1, wxALIGN_CENTER, 0);
    sizer_temp_txt->Add(FromDIP(10), 0, 0, 0);
    sizer_temp_txt->Add(m_title_min, 1, wxALIGN_CENTER | wxRIGHT, FromDIP(16));


    sizer_other->Add(sizer_temp_txt, 0, wxALIGN_CENTER, 0);
    sizer_other->Add(sizer_tempinput, 0, wxALIGN_CENTER, 0);

    m_sizer_temperature->Add(sizer_other, 0, wxALL | wxALIGN_CENTER, 0);
    m_sizer_temperature->AddStretchSpacer();

    wxString warning_string = wxString::FromUTF8(
        (boost::format(_u8L("The input value should be greater than %1% and less than %2%")) % FILAMENT_MIN_TEMP % FILAMENT_MAX_TEMP).str());
    warning_text = new wxStaticText(parent, wxID_ANY, warning_string, wxDefaultPosition, wxDefaultSize, 0);
    warning_text->SetFont(::Label::Body_13);
    warning_text->SetForegroundColour(wxColour(255, 111, 0));

    warning_text->Wrap(AMS_MATERIALS_SETTING_BODY_WIDTH);
    warning_text->SetMinSize(wxSize(AMS_MATERIALS_SETTING_BODY_WIDTH, -1));
    warning_text->Hide();

    m_panel_SN = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    wxBoxSizer* m_sizer_SN = new wxBoxSizer(wxVERTICAL);
    m_sizer_SN->AddSpacer(FromDIP(16));

    wxBoxSizer* m_sizer_SN_inside = new wxBoxSizer(wxHORIZONTAL);

    auto m_title_SN = new wxStaticText(m_panel_SN, wxID_ANY, _L("SN"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_SN->SetFont(::Label::Body_13);
    m_title_SN->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_SN->Wrap(-1);
    m_sizer_SN_inside->Add(m_title_SN, 0, wxALIGN_CENTER, 0);

    m_sizer_SN_inside->Add(0, 0, 0, wxEXPAND, 0);

    m_sn_number = new wxStaticText(m_panel_SN, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize);
    m_sn_number->SetForegroundColour(*wxBLACK);
    m_sizer_SN_inside->Add(m_sn_number, 0, wxALIGN_CENTER, 0);
    m_sizer_SN->Add(m_sizer_SN_inside);

    m_panel_SN->SetSizer(m_sizer_SN);
    m_panel_SN->Layout();
    m_panel_SN->Fit();

    wxBoxSizer* m_tip_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_tip_readonly = new wxStaticText(parent, wxID_ANY, _L("Setting AMS slot information while printing is not supported"), wxDefaultPosition, wxSize(-1, AMS_MATERIALS_SETTING_INPUT_SIZE.y));
    m_tip_readonly->SetForegroundColour(*wxBLACK);
    m_tip_readonly->Hide();
    m_tip_sizer->Add(m_tip_readonly, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(20));

    sizer->Add(0, 0, 0, wxTOP, FromDIP(16));
    sizer->Add(m_sizer_filament, 0, wxLEFT | wxRIGHT, FromDIP(20));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(16));
    sizer->Add(m_sizer_colour, 0, wxLEFT | wxRIGHT, FromDIP(20));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(16));
    sizer->Add(m_sizer_temperature, 0, wxLEFT | wxRIGHT, FromDIP(20));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(5));
    sizer->Add(warning_text, 0, wxLEFT | wxRIGHT, FromDIP(20));
    sizer->Add(m_panel_SN, 0, wxLEFT, FromDIP(20));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(24));
    sizer->Add(m_tip_sizer, 0, wxLEFT, FromDIP(20));
    parent->SetSizer(sizer);
}

void AMSMaterialsSetting::create_panel_kn(wxWindow* parent)
{
    auto sizer = new wxBoxSizer(wxVERTICAL);
    // title
    auto ratio_text = new wxStaticText(parent, wxID_ANY, _L("Factors of dynamic flow cali"));
    ratio_text->SetFont(Label::Head_14);

    auto kn_val_sizer = new wxFlexGridSizer(0, 2, 0, 0);
    kn_val_sizer->SetFlexibleDirection(wxBOTH);
    kn_val_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);
    kn_val_sizer->AddGrowableCol(1);

    // k params input
    m_k_param = new wxStaticText(parent, wxID_ANY, _L("Factor K"), wxDefaultPosition, wxDefaultSize, 0);
    m_k_param->SetFont(::Label::Body_13);
    m_k_param->SetForegroundColour(wxColour(50, 58, 61));
    m_k_param->Wrap(-1);
    kn_val_sizer->Add(m_k_param, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));

    m_input_k_val = new TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_input_k_val->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    kn_val_sizer->Add(m_input_k_val, 0, wxALL | wxEXPAND | wxALIGN_CENTER_VERTICAL, FromDIP(5));

    // n params input
    wxBoxSizer* n_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_n_param = new wxStaticText(parent, wxID_ANY, _L("Factor N"), wxDefaultPosition, wxDefaultSize, 0);
    m_n_param->SetFont(::Label::Body_13);
    m_n_param->SetForegroundColour(wxColour(50, 58, 61));
    m_n_param->Wrap(-1);
    kn_val_sizer->Add(m_n_param, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    m_input_n_val = new TextInput(parent, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_input_n_val->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    kn_val_sizer->Add(m_input_n_val, 0, wxALL | wxEXPAND | wxALIGN_CENTER_VERTICAL, FromDIP(5));

    // hide n
    m_n_param->Hide();
    m_input_n_val->Hide();

    sizer->Add(0, 0, 0, wxTOP, FromDIP(10));
    sizer->Add(ratio_text, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(20));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(10));
    sizer->Add(kn_val_sizer, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(20));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(10));
    parent->SetSizer(sizer);
}

void AMSMaterialsSetting::paintEvent(wxPaintEvent &evt) 
{
    auto      size = GetSize();
    wxPaintDC dc(this);
    dc.SetPen(wxPen(StateColor::darkModeColorFor(wxColour("#000000")), 1, wxSOLID));
    dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
    dc.DrawRectangle(0, 0, size.x, size.y);
}

AMSMaterialsSetting::~AMSMaterialsSetting()
{
    m_comboBox_filament->Disconnect(wxEVT_COMMAND_COMBOBOX_SELECTED, wxCommandEventHandler(AMSMaterialsSetting::on_select_filament), NULL, this);
}

void AMSMaterialsSetting::input_min_finish() 
{
    if (m_input_nozzle_min->GetTextCtrl()->GetValue().empty()) return;

    auto val = std::atoi(m_input_nozzle_min->GetTextCtrl()->GetValue().c_str());

    if (val < FILAMENT_MIN_TEMP || val > FILAMENT_MAX_TEMP) {
        warning_text->Show();
    } else {
        warning_text->Hide();
    }
    Layout();
    Fit();
}

void AMSMaterialsSetting::input_max_finish()
{
    if (m_input_nozzle_max->GetTextCtrl()->GetValue().empty()) return;

    auto val = std::atoi(m_input_nozzle_max->GetTextCtrl()->GetValue().c_str());

    if (val < FILAMENT_MIN_TEMP || val > FILAMENT_MAX_TEMP) {
        warning_text->Show();
    }
    else {
        warning_text->Hide();
    }
    Layout();
    Fit();
}

void AMSMaterialsSetting::update()
{
    if (obj) {
        update_widgets();
        if (obj->is_in_printing() || obj->can_resume()) {
            enable_confirm_button(false);
        } else {
            enable_confirm_button(true);
        }
    }
}

void AMSMaterialsSetting::enable_confirm_button(bool en)
{
    m_button_confirm->Show(en);
    if (!m_is_third) {
        m_tip_readonly->Hide(); 
    }
    else {
        m_comboBox_filament->Show(en);
        m_readonly_filament->Show(!en);
        m_tip_readonly->Show(!en);
    }
}

void AMSMaterialsSetting::on_select_ok(wxCommandEvent &event)
{
    wxString k_text = m_input_k_val->GetTextCtrl()->GetValue();
    wxString n_text = m_input_n_val->GetTextCtrl()->GetValue();

    if (is_virtual_tray()) {
        if (!ExtrusionCalibration::check_k_validation(k_text)) {
            wxString k_tips = _L("Please input a valid value (K in 0~0.5)");
            wxString kn_tips = _L("Please input a valid value (K in 0~0.5, N in 0.6~2.0)");
            MessageDialog msg_dlg(nullptr, k_tips, wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }
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
        obj->command_extrusion_cali_set(VIRTUAL_TRAY_ID, "", "", k, n);
        Close();
    } else {
        if (!m_is_third) {
            // check and set k n
            if (obj->is_function_supported(PrinterFunction::FUNC_EXTRUSION_CALI)) {
                if (!ExtrusionCalibration::check_k_validation(k_text)) {
                    wxString k_tips = _L("Please input a valid value (K in 0~0.5)");
                    wxString kn_tips = _L("Please input a valid value (K in 0~0.5, N in 0.6~2.0)");
                    MessageDialog msg_dlg(nullptr, k_tips, wxEmptyString, wxICON_WARNING | wxOK);
                    msg_dlg.ShowModal();
                    return;
                }
            }


            // set k / n value
            if (obj->is_function_supported(PrinterFunction::FUNC_EXTRUSION_CALI)) {
                // set extrusion cali ratio
                int cali_tray_id = ams_id * 4 + tray_id;

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
                obj->command_extrusion_cali_set(cali_tray_id, "", "", k, n);
            }
            Close();
            return;
        }
        wxString nozzle_temp_min = m_input_nozzle_min->GetTextCtrl()->GetValue();
        auto     filament        = m_comboBox_filament->GetValue();

        wxString nozzle_temp_max = m_input_nozzle_max->GetTextCtrl()->GetValue();

        long nozzle_temp_min_int, nozzle_temp_max_int;
        nozzle_temp_min.ToLong(&nozzle_temp_min_int);
        nozzle_temp_max.ToLong(&nozzle_temp_max_int);
        wxColour color = m_clrData->GetColour();
        char col_buf[10];
        sprintf(col_buf, "%02X%02X%02XFF", (int) color.Red(), (int) color.Green(), (int) color.Blue());
        ams_filament_id = "";
        ams_setting_id = "";

        PresetBundle *preset_bundle = wxGetApp().preset_bundle;
        if (preset_bundle) {
            for (auto it = preset_bundle->filaments.begin(); it != preset_bundle->filaments.end(); it++) {
                if (it->alias.compare(m_comboBox_filament->GetValue().ToStdString()) == 0) {
                    ams_filament_id = it->filament_id;
                    ams_setting_id = it->setting_id;
                }
            }
        }

        if (ams_filament_id.empty() || nozzle_temp_min.empty() || nozzle_temp_max.empty() || m_filament_type.empty()) {
            BOOST_LOG_TRIVIAL(trace) << "Invalid Setting id";
        } else {
            if (obj) {
                if (obj->is_function_supported(PrinterFunction::FUNC_EXTRUSION_CALI)) {
                    if (!ExtrusionCalibration::check_k_validation(k_text)) {
                        wxString k_tips = _L("Please input a valid value (K in 0~0.5)");
                        wxString kn_tips = _L("Please input a valid value (K in 0~0.5, N in 0.6~2.0)");
                        MessageDialog msg_dlg(nullptr, k_tips, wxEmptyString, wxICON_WARNING | wxOK);
                        msg_dlg.ShowModal();
                        return;
                    }
                }

                // set filament
                if (!is_virtual_tray()) {
                    obj->command_ams_filament_settings(ams_id, tray_id, ams_filament_id, ams_setting_id, std::string(col_buf), m_filament_type, nozzle_temp_min_int, nozzle_temp_max_int);
                }

                // set k / n value
                if (obj->is_function_supported(PrinterFunction::FUNC_EXTRUSION_CALI)) {
                    // set extrusion cali ratio
                    int cali_tray_id = ams_id * 4 + tray_id;

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
                    obj->command_extrusion_cali_set(cali_tray_id, "", "", k, n);
                }
            }
        }
    }
    Close();
}

void AMSMaterialsSetting::on_select_close(wxCommandEvent &event)
{
    Close();
}

void AMSMaterialsSetting::set_color(wxColour color)
{
    m_clrData->SetColour(color);
}

void AMSMaterialsSetting::on_clr_picker(wxCommandEvent & event) 
{
    if(!m_is_third || obj->is_in_printing() || obj->can_resume())
        return;
    auto clr_dialog = new wxColourDialog(this, m_clrData);
    show_flag = true;
    if (clr_dialog->ShowModal() == wxID_OK) {
        m_clrData = &(clr_dialog->GetColourData());
        m_clr_picker->SetBackgroundColor(wxColour(
            m_clrData->GetColour().Red(),
            m_clrData->GetColour().Green(),
            m_clrData->GetColour().Blue(),
            254
        ));
    }
}

bool AMSMaterialsSetting::is_virtual_tray()
{
    if (tray_id == VIRTUAL_TRAY_ID)
        return true;
    return false;
}

void AMSMaterialsSetting::update_widgets()
{
    // virtual tray
    if (is_virtual_tray()) {
        m_panel_normal->Hide();
        m_panel_kn->Show();
    } else if (obj && obj->is_function_supported(PrinterFunction::FUNC_EXTRUSION_CALI)) {
        m_panel_normal->Show();
        m_panel_kn->Show();
    } else {
        m_panel_normal->Show();
        m_panel_kn->Hide();
    }
    Layout();
}

bool AMSMaterialsSetting::Show(bool show) 
{ 
    if (show) {
        m_button_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
        m_input_nozzle_max->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(20)));
        m_input_nozzle_min->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(20)));
        m_clr_picker->SetBackgroundColour(m_clr_picker->GetParent()->GetBackgroundColour());
    }
    return DPIDialog::Show(show); 
}

void AMSMaterialsSetting::Popup(wxString filament, wxString sn, wxString temp_min, wxString temp_max, wxString k, wxString n)
{
    update_widgets();
    // set default value
    if (k.IsEmpty())
        k = "0.000";
    if (n.IsEmpty())
        n = "0.000";

    m_input_k_val->GetTextCtrl()->SetValue(k);
    m_input_n_val->GetTextCtrl()->SetValue(n);

    if (is_virtual_tray()) {
        m_button_confirm->Show();
        update();
        Layout();
        Fit();
        ShowModal();
        return;
    } else {
        m_clr_picker->SetBackgroundColor(wxColour(
            m_clrData->GetColour().Red(),
            m_clrData->GetColour().Green(),
            m_clrData->GetColour().Blue(),
            254
        ));

        if (!m_is_third) {
            if (obj && obj->is_function_supported(PrinterFunction::FUNC_EXTRUSION_CALI)) {
                m_button_confirm->Show();
            } else {
                m_button_confirm->Hide();
            }

            m_sn_number->SetLabel(sn);
            m_panel_SN->Show();
            m_comboBox_filament->Hide();
            m_readonly_filament->Show();
            m_readonly_filament->GetTextCtrl()->SetLabel("Bambu " + filament);
            m_input_nozzle_min->GetTextCtrl()->SetValue(temp_min);
            m_input_nozzle_max->GetTextCtrl()->SetValue(temp_max);

            update();
            Layout();
            Fit();
            ShowModal();
            return;
        }

        m_button_confirm->Show();
        m_panel_SN->Hide();
        m_comboBox_filament->Show();
        m_readonly_filament->Hide();


        int selection_idx = -1, idx = 0;
        wxArrayString filament_items;
        std::set<std::string> filament_id_set;

        PresetBundle* preset_bundle = wxGetApp().preset_bundle;
        if (preset_bundle) {
            BOOST_LOG_TRIVIAL(trace) << "system_preset_bundle filament number=" << preset_bundle->filaments.size();
            for (auto filament_it = preset_bundle->filaments.begin(); filament_it != preset_bundle->filaments.end(); filament_it++) {
                // filter by system preset
                if (!filament_it->is_system) continue;

                for (auto printer_it = preset_bundle->printers.begin(); printer_it != preset_bundle->printers.end(); printer_it++) {
                    // filter by system preset
                    if (!printer_it->is_system) continue;
                    // get printer_model
                    ConfigOption* printer_model_opt = printer_it->config.option("printer_model");
                    ConfigOptionString* printer_model_str = dynamic_cast<ConfigOptionString*>(printer_model_opt);
                    if (!printer_model_str || !obj)
                        continue;

                    // use printer_model as printer type
                    if (printer_model_str->value != MachineObject::get_preset_printer_model_name(obj->printer_type))
                        continue;
                    ConfigOption* printer_opt = filament_it->config.option("compatible_printers");
                    ConfigOptionStrings* printer_strs = dynamic_cast<ConfigOptionStrings*>(printer_opt);
                    for (auto printer_str : printer_strs->values) {
                        if (printer_it->name == printer_str) {
                            if (filament_id_set.find(filament_it->filament_id) != filament_id_set.end()) {
                                continue;
                            }
                            else {
                                filament_id_set.insert(filament_it->filament_id);
                                // name matched
                                filament_items.push_back(filament_it->alias);
                                if (filament_it->filament_id == ams_filament_id) {
                                    selection_idx = idx;

                                    // update if nozzle_temperature_range is found
                                    ConfigOption* opt_min = filament_it->config.option("nozzle_temperature_range_low");
                                    if (opt_min) {
                                        ConfigOptionInts* opt_min_ints = dynamic_cast<ConfigOptionInts*>(opt_min);
                                        if (opt_min_ints) {
                                            wxString text_nozzle_temp_min = wxString::Format("%d", opt_min_ints->get_at(0));
                                            m_input_nozzle_min->GetTextCtrl()->SetValue(text_nozzle_temp_min);
                                        }
                                    }
                                    ConfigOption* opt_max = filament_it->config.option("nozzle_temperature_range_high");
                                    if (opt_max) {
                                        ConfigOptionInts* opt_max_ints = dynamic_cast<ConfigOptionInts*>(opt_max);
                                        if (opt_max_ints) {
                                            wxString text_nozzle_temp_max = wxString::Format("%d", opt_max_ints->get_at(0));
                                            m_input_nozzle_max->GetTextCtrl()->SetValue(text_nozzle_temp_max);
                                        }
                                    }
                                }
                                idx++;
                            }
                        }
                    }
                }
            }
            m_comboBox_filament->Set(filament_items);
            m_comboBox_filament->SetSelection(selection_idx);
            post_select_event();
        }
    }

    update();
    Layout();
    Fit();
    ShowModal();
}

void AMSMaterialsSetting::post_select_event() {
    wxCommandEvent event(wxEVT_COMBOBOX);
    event.SetEventObject(m_comboBox_filament);
    wxPostEvent(m_comboBox_filament, event);
}

void AMSMaterialsSetting::on_select_filament(wxCommandEvent &evt)
{
    m_filament_type = "";
    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        for (auto it = preset_bundle->filaments.begin(); it != preset_bundle->filaments.end(); it++) {
            if (it->alias.compare(m_comboBox_filament->GetValue().ToStdString()) == 0) {

                //check is it in the filament blacklist
                bool in_blacklist = false;
                std::string action;
                std::string info;
                std::string filamnt_type;
                it->get_filament_type(filamnt_type);

                if (it->vendor) {
                    DeviceManager::check_filaments_in_blacklist(it->vendor->name, filamnt_type, in_blacklist, action, info);
                }
                
                if (in_blacklist) {
                    if (action == "prohibition") {
                        MessageDialog msg_wingow(nullptr, info, _L("Error"), wxICON_WARNING | wxOK);
                        msg_wingow.ShowModal();
                        m_comboBox_filament->SetSelection(m_filament_selection);
                        return;
                    }
                    else if (action == "warning") {
                        MessageDialog msg_wingow(nullptr, info, _L("Warning"), wxICON_INFORMATION | wxOK);
                        msg_wingow.ShowModal();
                    }
                }

                // ) if nozzle_temperature_range is found
                ConfigOption* opt_min = it->config.option("nozzle_temperature_range_low");
                if (opt_min) {
                    ConfigOptionInts* opt_min_ints = dynamic_cast<ConfigOptionInts*>(opt_min);
                    if (opt_min_ints) {
                        wxString text_nozzle_temp_min = wxString::Format("%d", opt_min_ints->get_at(0));
                        m_input_nozzle_min->GetTextCtrl()->SetValue(text_nozzle_temp_min);
                    }
                }
                ConfigOption* opt_max = it->config.option("nozzle_temperature_range_high");
                if (opt_max) {
                    ConfigOptionInts* opt_max_ints = dynamic_cast<ConfigOptionInts*>(opt_max);
                    if (opt_max_ints) {
                        wxString text_nozzle_temp_max = wxString::Format("%d", opt_max_ints->get_at(0));
                        m_input_nozzle_max->GetTextCtrl()->SetValue(text_nozzle_temp_max);
                    }
                }
                ConfigOption* opt_type = it->config.option("filament_type");
                bool found_filament_type = false;
                if (opt_type) {
                    ConfigOptionStrings* opt_type_strs = dynamic_cast<ConfigOptionStrings*>(opt_type);
                    if (opt_type_strs) {
                        found_filament_type = true;
                        //m_filament_type = opt_type_strs->get_at(0);
                        std::string display_filament_type;
                        m_filament_type = it->config.get_filament_type(display_filament_type);
                    }
                }
                if (!found_filament_type)
                    m_filament_type = "";

                break;
            }
        }
    }
    if (m_input_nozzle_min->GetTextCtrl()->GetValue().IsEmpty()) {
         m_input_nozzle_min->GetTextCtrl()->SetValue("220");
    }
    if (m_input_nozzle_max->GetTextCtrl()->GetValue().IsEmpty()) {
         m_input_nozzle_max->GetTextCtrl()->SetValue("220");
    }

    m_filament_selection = evt.GetSelection();
}

void AMSMaterialsSetting::on_dpi_changed(const wxRect &suggested_rect) { this->Refresh(); }

}} // namespace Slic3r::GUI
