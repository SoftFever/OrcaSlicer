#include "AMSMaterialsSetting.hpp"
#include "GUI_App.hpp"
#include "libslic3r/Preset.hpp"
#include "I18N.hpp"

namespace Slic3r { namespace GUI {
static bool show_flag;

#ifdef __APPLE__
#define COMBOBOX_FILAMENT (m_comboBox_filament_mac)
#else
#define COMBOBOX_FILAMENT (m_comboBox_filament)
#endif
AMSMaterialsSetting::AMSMaterialsSetting(wxWindow *parent, wxWindowID id) : wxPopupTransientWindow(parent, wxPU_CONTAINS_CONTROLS) {
    create();
}

void AMSMaterialsSetting::create()
{
    SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_main = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);

    m_panel_body                 = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_BODY_WIDTH, -1), wxTAB_TRAVERSAL);
    m_panel_body->SetBackgroundColour(*wxWHITE);
    wxBoxSizer *m_sizer_filament = new wxBoxSizer(wxHORIZONTAL);

    m_title_filament = new wxStaticText(m_panel_body, wxID_ANY, _L("Filament"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_filament->SetFont(::Label::Body_13);
    m_title_filament->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_filament->Wrap(-1);
    m_sizer_filament->Add(m_title_filament, 0, wxALIGN_CENTER, 0);

    m_sizer_filament->Add(0, 0, 0, wxEXPAND, 0);

#ifdef __APPLE__
    m_comboBox_filament_mac = new wxComboBox(m_panel_body, wxID_ANY, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_COMBOX_WIDTH, 0, nullptr, wxCB_READONLY);
#else
    m_comboBox_filament = new ::ComboBox(m_panel_body, wxID_ANY, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_COMBOX_WIDTH, 0, nullptr, wxCB_READONLY);
#endif

    m_sizer_filament->Add(COMBOBOX_FILAMENT, 1, wxALIGN_CENTER, 0);
    wxBoxSizer *m_sizer_colour = new wxBoxSizer(wxHORIZONTAL);

    m_title_colour = new wxStaticText(m_panel_body, wxID_ANY, _L("Colour"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_colour->SetFont(::Label::Body_13);
    m_title_colour->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_colour->Wrap(-1);
    m_sizer_colour->Add(m_title_colour, 0, wxALIGN_CENTER, 0);

    m_sizer_colour->Add(0, 0, 0, wxEXPAND, 0);

    m_clrData = new wxColourData();
    m_clrData->SetChooseFull(true);
    m_clrData->SetChooseAlpha(false);
    m_clr_picker    = new wxButton(m_panel_body, wxID_ANY, "", wxDefaultPosition, wxSize(FromDIP(20), FromDIP(20)), wxBU_EXACTFIT | wxBORDER_NONE);
    m_clr_picker->Bind(wxEVT_BUTTON, &AMSMaterialsSetting::on_clr_picker, this);
    m_sizer_colour->Add(m_clr_picker, 0, 0, 0);


    m_panel_SN = new wxPanel(m_panel_body, wxID_ANY);
    wxBoxSizer *m_sizer_SN = new wxBoxSizer(wxHORIZONTAL);

    auto m_title_SN = new wxStaticText(m_panel_SN, wxID_ANY, _L("SN"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_SN->SetFont(::Label::Body_13);
    m_title_SN->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_SN->Wrap(-1);
    m_sizer_SN->Add(m_title_SN, 0, wxALIGN_CENTER, 0);

    m_sizer_SN->Add(0, 0, 0, wxEXPAND, 0);

    m_sn_number = new wxStaticText(m_panel_SN, wxID_ANY, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_COMBOX_WIDTH);
    m_sizer_SN->Add(m_sn_number, 0, wxALIGN_CENTER, 0);
    m_panel_SN->SetSizer(m_sizer_SN);
    m_panel_SN->Layout();
    m_panel_SN->Fit();

    wxBoxSizer *m_sizer_temperature = new wxBoxSizer(wxHORIZONTAL);
    m_title_temperature             = new wxStaticText(m_panel_body, wxID_ANY, _L("Nozzle\nTemperature"), wxDefaultPosition, wxSize(AMS_MATERIALS_SETTING_LABEL_WIDTH, -1), 0);
    m_title_temperature->SetFont(::Label::Body_13);
    m_title_temperature->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_temperature->Wrap(-1);
    m_sizer_temperature->Add(m_title_temperature, 0, wxALIGN_CENTER, 0);

    m_sizer_temperature->Add(0, 0, 0, wxEXPAND, 0);

    wxBoxSizer *sizer_other           = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *sizer_tempinput       = new wxBoxSizer(wxHORIZONTAL);

    m_input_nozzle_max = new ::TextInput(m_panel_body, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_INPUT_SIZE, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_input_nozzle_min = new ::TextInput(m_panel_body, wxEmptyString, wxEmptyString, wxEmptyString, wxDefaultPosition, AMS_MATERIALS_SETTING_INPUT_SIZE, wxTE_CENTRE | wxTE_PROCESS_ENTER);
    m_input_nozzle_max->Enable(false);
    m_input_nozzle_min->Enable(false);

    m_input_nozzle_max->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_input_nozzle_max->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(20)));
    m_input_nozzle_min->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_input_nozzle_min->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(20)));

    auto bitmap_max_degree = new wxStaticBitmap(m_panel_body, -1, create_scaled_bitmap("degree", nullptr, 16), wxDefaultPosition, wxDefaultSize);
    auto bitmap_min_degree = new wxStaticBitmap(m_panel_body, -1, create_scaled_bitmap("degree", nullptr, 16), wxDefaultPosition, wxDefaultSize);

    sizer_tempinput->Add(m_input_nozzle_max, 1, wxALIGN_CENTER, 0);
    sizer_tempinput->Add(bitmap_min_degree, 0, wxALIGN_CENTER, 0);
    sizer_tempinput->Add(FromDIP(10), 0, wxEXPAND, 0);
    sizer_tempinput->Add(m_input_nozzle_min, 1, wxALIGN_CENTER, 0);
    sizer_tempinput->Add(bitmap_max_degree, 0, wxALIGN_CENTER, 0);

    wxBoxSizer *sizer_temp_txt    = new wxBoxSizer(wxHORIZONTAL);
    auto   m_title_max = new wxStaticText(m_panel_body, wxID_ANY, _L("max"), wxDefaultPosition, AMS_MATERIALS_SETTING_INPUT_SIZE);
    m_title_max->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_max->SetFont(::Label::Body_13);
    auto   m_title_min = new wxStaticText(m_panel_body, wxID_ANY, _L("min"), wxDefaultPosition, AMS_MATERIALS_SETTING_INPUT_SIZE);
    m_title_min->SetForegroundColour(AMS_MATERIALS_SETTING_GREY800);
    m_title_min->SetFont(::Label::Body_13);
    sizer_temp_txt->Add(m_title_max, 1, wxALIGN_CENTER, 0);
    sizer_temp_txt->Add(FromDIP(10), 0, wxEXPAND, 0);
    sizer_temp_txt->Add(m_title_min, 1, wxALIGN_CENTER|wxRIGHT, FromDIP(16));


    sizer_other->Add(sizer_temp_txt, 0, wxALIGN_CENTER, 0);
    sizer_other->Add(sizer_tempinput, 0, wxALIGN_CENTER, 0);

    m_sizer_temperature->Add(sizer_other, 0, wxALL | wxALIGN_CENTER, 0);
    m_sizer_temperature->AddStretchSpacer();

    wxString warning_string = wxString::FromUTF8(
        (boost::format(_u8L("The input value should be greater than %1% and less than %2%")) % FILAMENT_MIN_TEMP % FILAMENT_MAX_TEMP).str());
    warning_text = new wxStaticText(m_panel_body, wxID_ANY, warning_string, wxDefaultPosition, wxDefaultSize, 0);
    warning_text->SetFont(::Label::Body_13);
    warning_text->SetForegroundColour(wxColour(255, 111, 0));

    warning_text->Wrap(AMS_MATERIALS_SETTING_BODY_WIDTH);
    warning_text->SetMinSize(wxSize(AMS_MATERIALS_SETTING_BODY_WIDTH, -1));
    warning_text->Hide();

     m_input_nozzle_min->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &e) {
        warning_text->Hide();
        Layout();
        Fit();
        e.Skip();
    });
    m_input_nozzle_min->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &e) {
        input_min_finish();
        e.Skip();
    });
     m_input_nozzle_min->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &e) {
        input_min_finish();
        e.Skip();
    });

     m_input_nozzle_max->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &e) {
        warning_text->Hide();
        Layout();
        Fit();
        e.Skip();
        });
     m_input_nozzle_max->GetTextCtrl()->Bind(wxEVT_TEXT_ENTER, [this](wxCommandEvent &e) {
        input_max_finish();
        e.Skip();
        });
     m_input_nozzle_max->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &e) {
        input_max_finish();
        e.Skip();
        });

    wxBoxSizer *m_sizer_button = new wxBoxSizer(wxHORIZONTAL);
    m_sizer_button->Add(0, 0, 1, wxEXPAND, 0);

    m_button_confirm = new Button(m_panel_body, _L("Confirm"));
    m_btn_bg_green   = StateColor(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    m_button_confirm->SetBackgroundColor(m_btn_bg_green);
    m_button_confirm->SetBorderColor(wxColour(0, 174, 66));
    m_button_confirm->SetTextColor(AMS_MATERIALS_SETTING_GREY200);
    m_button_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
    m_button_confirm->SetCornerRadius(12);
    m_button_confirm->Bind(wxEVT_LEFT_DOWN, &AMSMaterialsSetting::on_select_ok, this);
    m_sizer_button->Add(m_button_confirm, 0, wxALIGN_CENTER, 0);

    m_sizer_body->Add(m_sizer_filament, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(16));
    m_sizer_body->Add(m_sizer_colour, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(16));
    m_sizer_body->Add(m_panel_SN, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(10));
    m_sizer_body->Add(m_sizer_temperature, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(5));
    m_sizer_body->Add(warning_text, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(24));
    m_sizer_body->Add(m_sizer_button, 0, wxEXPAND, 0);

    m_panel_body->SetSizer(m_sizer_body);
    m_panel_body->Layout();
    m_sizer_main->Add(m_panel_body, 0, wxALL | wxEXPAND, 24);

    this->SetSizer(m_sizer_main);
    this->Layout();
    m_sizer_main->Fit(this);

    this->Centre(wxBOTH);

     Bind(wxEVT_PAINT, &AMSMaterialsSetting::paintEvent, this);
     COMBOBOX_FILAMENT->Connect(wxEVT_COMMAND_COMBOBOX_SELECTED, wxCommandEventHandler(AMSMaterialsSetting::on_select_filament), NULL, this);
}

void AMSMaterialsSetting::paintEvent(wxPaintEvent &evt) 
{
    auto      size = GetSize();
    wxPaintDC dc(this);
    dc.SetPen(wxPen(wxColour(38, 46, 48), 1, wxSOLID));
    dc.SetBrush(wxBrush(*wxTRANSPARENT_BRUSH));
    dc.DrawRectangle(0, 0, size.x, size.y);
}

AMSMaterialsSetting::~AMSMaterialsSetting()
{
    COMBOBOX_FILAMENT->Disconnect(wxEVT_COMMAND_COMBOBOX_SELECTED, wxCommandEventHandler(AMSMaterialsSetting::on_select_filament), NULL, this);
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
        if (obj->is_in_printing() || obj->can_resume()) {
            enable_confirm_button(false);
        } else {
            enable_confirm_button(true);
        }
    }
}

void AMSMaterialsSetting::enable_confirm_button(bool en)
{
    if (!en) {
        if (m_button_confirm->IsEnabled()) {
            m_button_confirm->Disable();
            m_button_confirm->SetBackgroundColor(wxColour(0x90, 0x90, 0x90));
            m_button_confirm->SetBorderColor(wxColour(0x90, 0x90, 0x90));
        }
    } else {
        if (!m_button_confirm->IsEnabled()) {
            m_button_confirm->Enable();
            m_button_confirm->SetBackgroundColor(m_btn_bg_green);
            m_button_confirm->SetBorderColor(m_btn_bg_green);
        }
    }
}

void AMSMaterialsSetting::on_select_ok(wxMouseEvent &event)
{
    wxString nozzle_temp_min = m_input_nozzle_min->GetTextCtrl()->GetValue();
    auto     filament        = COMBOBOX_FILAMENT->GetValue();

    wxString nozzle_temp_max = m_input_nozzle_max->GetTextCtrl()->GetValue();

    long nozzle_temp_min_int, nozzle_temp_max_int;
    nozzle_temp_min.ToLong(&nozzle_temp_min_int);
    nozzle_temp_max.ToLong(&nozzle_temp_max_int);
    wxColour color = m_clrData->GetColour();
    char col_buf[10];
    sprintf(col_buf, "%02X%02X%02XFF", (int) color.Red(), (int) color.Green(), (int) color.Blue());
    ams_filament_id = "";

    PresetBundle *preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        for (auto it = preset_bundle->filaments.begin(); it != preset_bundle->filaments.end(); it++) {
            if (it->alias.compare(COMBOBOX_FILAMENT->GetValue().ToStdString()) == 0) {
                ams_filament_id = it->filament_id;
            }
        }
    }

    if (ams_filament_id.empty() || nozzle_temp_min.empty() || nozzle_temp_max.empty() || m_filament_type.empty()) {
        BOOST_LOG_TRIVIAL(trace) << "Invalid Setting id";
    } else {
        if (obj) {
            obj->command_ams_filament_settings(ams_id, tray_id, ams_filament_id, std::string(col_buf), m_filament_type, nozzle_temp_min_int, nozzle_temp_max_int);
        }
    }
    wxPopupTransientWindow::Dismiss();
}

void AMSMaterialsSetting::set_color(wxColour color)
{
    m_clrData->SetColour(color);
}

void AMSMaterialsSetting::on_clr_picker(wxCommandEvent & event) 
{
    auto clr_dialog = new wxColourDialog(this, m_clrData);
    show_flag = true;
    if (clr_dialog->ShowModal() == wxID_OK) {
        m_clrData = &(clr_dialog->GetColourData());
        m_clr_picker->SetBackgroundColour(m_clrData->GetColour());
    }
}

void AMSMaterialsSetting::Dismiss() 
{ 
    if (show_flag) 
    {
        show_flag = false;
    } else 
    {
#ifdef __APPLE__

#else
        wxPopupTransientWindow::Dismiss();
#endif
    }
}

bool AMSMaterialsSetting::Show(bool show) 
{ 
    if (show) {
        m_button_confirm->SetMinSize(AMS_MATERIALS_SETTING_BUTTON_SIZE);
        m_input_nozzle_max->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(20)));
        m_input_nozzle_min->GetTextCtrl()->SetSize(wxSize(-1, FromDIP(20)));
    }
    return wxPopupTransientWindow::Show(show); 
}

void AMSMaterialsSetting::Popup(bool show, bool third, wxString filament, wxColour colour, wxString sn, wxString tep)
{
    if (!m_is_third) {
        m_panel_SN->Show();
        COMBOBOX_FILAMENT->SetValue(filament);
        m_sn_number->SetLabelText(sn);
        m_input_nozzle_min->GetTextCtrl()->SetValue(tep);
        m_clrData->SetColour(colour);
        COMBOBOX_FILAMENT->Disable();

        m_input_nozzle_min->Disable();

        wxPopupTransientWindow::Popup();
        Layout();
        return;
    }
    m_panel_SN->Hide();
    Layout();
    Fit();

    m_clr_picker->SetBackgroundColour(m_clrData->GetColour());

    int selection_idx = -1, idx = 0;
    wxArrayString filament_items;
    std::set<std::string> filament_id_set;

    if (show) {
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
                            } else {
                                filament_id_set.insert(filament_it->filament_id);
                                // name matched
                                filament_items.push_back(filament_it->alias);
                                if (filament_it->filament_id == ams_filament_id) {
                                    selection_idx = idx;

                                    // update if nozzle_temperature_range is found
                                    ConfigOption* opt_min = filament_it->config.option("nozzle_temperature_range_low");
                                    if(opt_min) {
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
        }
        COMBOBOX_FILAMENT->Set(filament_items);
        if (selection_idx >= 0 && selection_idx < filament_items.size()) {
            COMBOBOX_FILAMENT->SetSelection(selection_idx);
            post_select_event();
        }
        else {
            COMBOBOX_FILAMENT->SetSelection(selection_idx);
            post_select_event();
        }
    }
    wxPopupTransientWindow::Popup();
}

void AMSMaterialsSetting::post_select_event() {
    wxCommandEvent event(wxEVT_COMBOBOX);
    event.SetEventObject(COMBOBOX_FILAMENT);
    wxPostEvent(COMBOBOX_FILAMENT, event);
}

void AMSMaterialsSetting::on_select_filament(wxCommandEvent &evt)
{
    m_filament_type = "";
    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        for (auto it = preset_bundle->filaments.begin(); it != preset_bundle->filaments.end(); it++) {
            auto a = it->alias;
            if (it->alias.compare(COMBOBOX_FILAMENT->GetValue().ToStdString()) == 0) {
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
                        m_filament_type = it->config.get_filament_type();
                    }
                }
                if (!found_filament_type)
                    m_filament_type = "";
            }
        }
    }
    if (m_input_nozzle_min->GetTextCtrl()->GetValue().IsEmpty()) {
         m_input_nozzle_min->GetTextCtrl()->SetValue("220");
    }
    if (m_input_nozzle_max->GetTextCtrl()->GetValue().IsEmpty()) {
         m_input_nozzle_max->GetTextCtrl()->SetValue("220");
    }
}

bool AMSMaterialsSetting::ProcessLeftDown(wxMouseEvent &evt)
{
    wxPoint mouse_pos    = ClientToScreen(evt.GetPosition());
    wxPoint top_left     = this->ClientToScreen(wxPoint(0, 0));
    wxPoint range        = wxPoint(this->GetRect().width, this->GetRect().height);
    wxPoint bottom_right = top_left + range;
    if (mouse_pos.x > top_left.x && mouse_pos.y > top_left.y && mouse_pos.x < bottom_right.x && mouse_pos.y < bottom_right.y) {
        return true;
    } else {
        wxPopupTransientWindow::Dismiss();
        return false;
    }
}

}} // namespace Slic3r::GUI
