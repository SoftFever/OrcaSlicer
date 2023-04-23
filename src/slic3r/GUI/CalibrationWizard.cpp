#include "CalibrationWizard.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "../../libslic3r/Calib.hpp"

#define CALIBRATION_COMBOX_SIZE            wxSize(FromDIP(500), FromDIP(24))
#define CALIBRATION_OPTIMAL_INPUT_SIZE     wxSize(FromDIP(300), FromDIP(24))
#define CALIBRATION_FROM_TO_INPUT_SIZE     wxSize(FromDIP(160), FromDIP(24))
namespace Slic3r { namespace GUI {

CalibrationWizard::CalibrationWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style) 
{
    SetBackgroundColour(wxColour(0xEEEEEE));

    m_background_panel = new wxPanel(this);
    m_background_panel->SetBackgroundColour(*wxWHITE);
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);

    create_presets_panel();
    init_presets_selections();

    create_progress_bar();

    main_sizer->Add(m_background_panel, 1, wxEXPAND | wxALL, FromDIP(10));

    this->SetSizer(main_sizer);
    this->Layout();
    main_sizer->Fit(this);

    m_comboBox_printer->Bind(wxEVT_COMBOBOX, &CalibrationWizard::on_select_printer, this);
    m_comboBox_filament->Bind(wxEVT_COMBOBOX, &CalibrationWizard::on_select_filament, this);
    m_comboBox_bed_type->Bind(wxEVT_COMBOBOX, &CalibrationWizard::on_select_bed_type, this);
    m_comboBox_process->Bind(wxEVT_COMBOBOX, &CalibrationWizard::on_select_process, this);
    Bind(EVT_CALIBRATIONPAGE_PREV, &CalibrationWizard::on_click_btn_prev, this);
    Bind(EVT_CALIBRATIONPAGE_NEXT, &CalibrationWizard::on_click_btn_next, this);
}

void CalibrationWizard::create_presets_panel()
{
    m_presets_panel = new wxPanel(m_background_panel);
    auto panel_sizer = new wxBoxSizer(wxVERTICAL);

    m_ams_control = new AMSControl(m_presets_panel);
    m_ams_control->EnterSimpleMode();
    panel_sizer->Add(m_ams_control, 0, wxALL, 0);

    auto printer_combo_text = new wxStaticText(m_presets_panel, wxID_ANY, _L("Printer"), wxDefaultPosition, wxDefaultSize, 0);
    printer_combo_text->Wrap(-1);
    panel_sizer->Add(printer_combo_text, 0, wxALL, 0);

    m_comboBox_printer = new ComboBox(m_presets_panel, wxID_ANY, "", wxDefaultPosition, CALIBRATION_COMBOX_SIZE, 0, nullptr, wxCB_READONLY);
    panel_sizer->Add(m_comboBox_printer, 0, wxALL, 0);

    panel_sizer->Add(0, FromDIP(10), 0, wxEXPAND, 0);

    auto filament_combo_text = new wxStaticText(m_presets_panel, wxID_ANY, _L("Filament"), wxDefaultPosition, wxDefaultSize, 0);
    filament_combo_text->Wrap(-1);
    panel_sizer->Add(filament_combo_text, 0, wxALL, 0);

    m_comboBox_filament = new ComboBox(m_presets_panel, wxID_ANY, "", wxDefaultPosition, CALIBRATION_COMBOX_SIZE, 0, nullptr, wxCB_READONLY);
    panel_sizer->Add(m_comboBox_filament, 0, wxALL, 0);

    panel_sizer->Add(0, FromDIP(10), 0, wxEXPAND, 0);

    auto plate_type_combo_text = new wxStaticText(m_presets_panel, wxID_ANY, _L("Plate Type"), wxDefaultPosition, wxDefaultSize, 0);
    plate_type_combo_text->Wrap(-1);
    panel_sizer->Add(plate_type_combo_text, 0, wxALL, 0);

    m_comboBox_bed_type = new ComboBox(m_presets_panel, wxID_ANY, "", wxDefaultPosition, CALIBRATION_COMBOX_SIZE, 0, nullptr, wxCB_READONLY);
    panel_sizer->Add(m_comboBox_bed_type, 0, wxALL, 0);

    panel_sizer->Add(0, FromDIP(10), 0, wxEXPAND, 0);

    auto process_combo_text = new wxStaticText(m_presets_panel, wxID_ANY, _L("Process"), wxDefaultPosition, wxDefaultSize, 0);
    process_combo_text->Wrap(-1);
    panel_sizer->Add(process_combo_text, 0, wxALL, 0);

    m_comboBox_process = new ComboBox(m_presets_panel, wxID_ANY, "", wxDefaultPosition, CALIBRATION_COMBOX_SIZE, 0, nullptr, wxCB_READONLY);
    panel_sizer->Add(m_comboBox_process, 0, wxALL, 0);

    m_presets_panel->SetSizer(panel_sizer);
    m_presets_panel->Layout();
    panel_sizer->Fit(m_presets_panel);

    panel_sizer->Add(0, FromDIP(10), 0, wxEXPAND, 0);

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
}

void CalibrationWizard::add_presets_panel_to_page(CalibrationWizardPage* page, wxBoxSizer* sizer)
{
    m_presets_panel->Reparent(page);
    sizer->Add(m_presets_panel, 0, wxEXPAND, 0);
}

void CalibrationWizard::create_progress_bar()
{
    m_progress_bar = new BBLStatusBarSend(m_background_panel);
    m_progress_bar->get_panel()->Hide();
    m_progress_bar->hide_cancel_button();
}

void CalibrationWizard::add_progress_bar_to_page(CalibrationWizardPage* page, wxBoxSizer* sizer)
{
    m_progress_bar->get_panel()->Reparent(page);
    sizer->Add(m_progress_bar->get_panel(), 0, wxEXPAND, 0);
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
#if 0   /*test*/
        show_page(get_curr_page()->get_next_page());
        return;
#endif

        if(!obj){
            MessageDialog msg_dlg(nullptr, _L("No Printer Connected!"), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }

        if (m_comboBox_printer->GetValue().IsEmpty() ||
            m_comboBox_filament->GetValue().IsEmpty() ||
            m_comboBox_bed_type->GetValue().IsEmpty() ||
            m_comboBox_process->GetValue().IsEmpty()) {
            MessageDialog msg_dlg(nullptr, _L("Please select presets"), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            return;
        }

        std::string ams_id = m_ams_control->GetCurentAms();
        std::string tray_id = m_ams_control->GetCurrentCan(ams_id);
        if (ams_id.compare(std::to_string(VIRTUAL_TRAY_ID)) != 0) {
            if (tray_id.empty()) {
                wxString txt = _L("Please select an AMS slot before calibration");
                MessageDialog msg_dlg(nullptr, txt, wxEmptyString, wxICON_WARNING | wxOK);
                msg_dlg.ShowModal();
                return;
            }
            else {
                tray_id = std::to_string(atoi(tray_id.c_str()) + 4 * atoi(ams_id.c_str()));
            }
        }
        else
            tray_id = ams_id;

        std::string result = "[" + tray_id + "]";

        if (start_calibration(result)) {
            show_progress_bar(true);
            m_progress_bar->set_cancel_callback([this]() {show_progress_bar(false); });
            //show_page(get_curr_page()->get_next_page());
        }
        break;
    }
    case Slic3r::GUI::Save:
        save_calibration_result();
        break;
    default:
        break;
    }
}

void CalibrationWizard::show_progress_bar(bool show)
{
    m_progress_bar->get_panel()->Show(show);
    show ? m_progress_bar->show_cancel_button() : m_progress_bar->hide_cancel_button();

    if (get_curr_page()->get_next_btn()->GetButtonType() == Calibrate)
        get_curr_page()->get_next_btn()->Show(!show);

    Layout();
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

void CalibrationWizard::update_ams(MachineObject* obj)
{
    // update obj in sub dlg
    bool is_none_ams_mode = false;

    if (!obj
        || !obj->is_connected()
        || obj->amsList.empty()
        || obj->ams_exist_bits == 0) {
        if (!obj || !obj->is_connected()) {
            BOOST_LOG_TRIVIAL(trace) << "machine object" << obj->dev_name << " was disconnected, set show_ams_group is false";
        }
        bool is_support_extrusion_cali = obj->is_function_supported(PrinterFunction::FUNC_EXTRUSION_CALI);
        bool is_support_virtual_tray = obj->is_function_supported(PrinterFunction::FUNC_VIRTUAL_TYAY);

        if (is_support_virtual_tray) {
            m_ams_control->update_vams_kn_value(obj->vt_tray, obj);
        }
        //show_ams_group(false, obj->is_function_supported(PrinterFunction::FUNC_VIRTUAL_TYAY), obj->is_function_supported(PrinterFunction::FUNC_EXTRUSION_CALI), obj->is_support_filament_edit_virtual_tray);
        is_none_ams_mode = true;
        //return;
    }

    bool is_support_extrusion_cali = obj->is_function_supported(PrinterFunction::FUNC_EXTRUSION_CALI);
    bool is_support_virtual_tray = obj->is_function_supported(PrinterFunction::FUNC_VIRTUAL_TYAY);
    bool is_support_filament_backup = obj->is_function_supported(PrinterFunction::FUNC_FILAMENT_BACKUP);

    m_ams_control->show_filament_backup(is_support_filament_backup);

    if (is_support_virtual_tray) {
        m_ams_control->update_vams_kn_value(obj->vt_tray, obj);
    }

    if (!is_none_ams_mode) {
        m_ams_control->Show(true);
        m_ams_control->show_noams_mode(true, obj->is_function_supported(PrinterFunction::FUNC_VIRTUAL_TYAY), obj->is_function_supported(PrinterFunction::FUNC_EXTRUSION_CALI), obj->is_support_filament_edit_virtual_tray, true);
    }

    std::vector<AMSinfo> ams_info;
    for (auto ams = obj->amsList.begin(); ams != obj->amsList.end(); ams++) {
        AMSinfo info;
        info.ams_id = ams->first;
        if (ams->second->is_exists && info.parse_ams_info(ams->second, obj->ams_calibrate_remain_flag, obj->is_support_ams_humidity)) ams_info.push_back(info);
    }

    // must select a current can
    m_ams_control->UpdateAms(ams_info, false, is_support_extrusion_cali);


    std::string curr_ams_id = m_ams_control->GetCurentAms();
    std::string curr_can_id = m_ams_control->GetCurrentCan(curr_ams_id);
    bool is_vt_tray = false;
    if (obj->m_tray_tar == std::to_string(VIRTUAL_TRAY_ID))
        is_vt_tray = true;

    // set segment 1, 2
    if (obj->m_tray_now == std::to_string(VIRTUAL_TRAY_ID)) {
        m_ams_control->SetAmsStep(obj->m_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
    }
    else {
        if (obj->m_tray_now != "255" && obj->is_filament_at_extruder() && !obj->m_tray_id.empty()) {
            m_ams_control->SetAmsStep(obj->m_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_LOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2);
        }
        else if (obj->m_tray_now != "255") {
            m_ams_control->SetAmsStep(obj->m_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_LOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1);
        }
        else {
            m_ams_control->SetAmsStep(obj->m_ams_id, obj->m_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
        }
    }

    // set segment 3
    if (obj->m_tray_now == std::to_string(VIRTUAL_TRAY_ID)) {
        m_ams_control->SetExtruder(obj->is_filament_at_extruder(), true, obj->vt_tray.get_color());
    }
    else {
        m_ams_control->SetExtruder(obj->is_filament_at_extruder(), false, m_ams_control->GetCanColour(obj->m_ams_id, obj->m_tray_id));

    }

    if (obj->ams_status_main == AMS_STATUS_MAIN_FILAMENT_CHANGE) {
        if (obj->m_tray_tar == std::to_string(VIRTUAL_TRAY_ID) && (obj->m_tray_now != std::to_string(VIRTUAL_TRAY_ID) || obj->m_tray_now != "255")) {
            // wait to heat hotend
            if (obj->ams_status_sub == 0x02) {
                m_ams_control->SetFilamentStep(FilamentStep::STEP_HEAT_NOZZLE, FilamentStepType::STEP_TYPE_VT_LOAD);
            }
            else if (obj->ams_status_sub == 0x05) {
                m_ams_control->SetFilamentStep(FilamentStep::STEP_FEED_FILAMENT, FilamentStepType::STEP_TYPE_VT_LOAD);
            }
            else if (obj->ams_status_sub == 0x06) {
                m_ams_control->SetFilamentStep(FilamentStep::STEP_CONFIRM_EXTRUDED, FilamentStepType::STEP_TYPE_VT_LOAD);
            }
            else if (obj->ams_status_sub == 0x07) {
                m_ams_control->SetFilamentStep(FilamentStep::STEP_PURGE_OLD_FILAMENT, FilamentStepType::STEP_TYPE_VT_LOAD);
            }
            else {
                m_ams_control->SetFilamentStep(FilamentStep::STEP_IDLE, FilamentStepType::STEP_TYPE_VT_LOAD);
            }
        }
        else {
            // wait to heat hotend
            if (obj->ams_status_sub == 0x02) {
                if (curr_ams_id == obj->m_ams_id) {
                    if (!obj->is_ams_unload()) {
                        m_ams_control->SetFilamentStep(FilamentStep::STEP_HEAT_NOZZLE, FilamentStepType::STEP_TYPE_LOAD);
                    }
                    else {
                        m_ams_control->SetFilamentStep(FilamentStep::STEP_HEAT_NOZZLE, FilamentStepType::STEP_TYPE_UNLOAD);
                    }
                }
                else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_IDLE, FilamentStepType::STEP_TYPE_UNLOAD);
                }
            }
            else if (obj->ams_status_sub == 0x03) {
                if (!obj->is_ams_unload()) {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_CUT_FILAMENT, FilamentStepType::STEP_TYPE_LOAD);
                }
                else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_CUT_FILAMENT, FilamentStepType::STEP_TYPE_UNLOAD);
                }
            }
            else if (obj->ams_status_sub == 0x04) {
                if (!obj->is_ams_unload()) {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PULL_CURR_FILAMENT, FilamentStepType::STEP_TYPE_LOAD);
                }
                else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PULL_CURR_FILAMENT, FilamentStepType::STEP_TYPE_UNLOAD);
                }
            }
            else if (obj->ams_status_sub == 0x05) {
                if (!obj->is_ams_unload()) {
                    if (/*m_is_load_with_temp*/false) {
                        m_ams_control->SetFilamentStep(FilamentStep::STEP_CUT_FILAMENT, FilamentStepType::STEP_TYPE_LOAD);
                    }
                    else {
                        m_ams_control->SetFilamentStep(FilamentStep::STEP_PUSH_NEW_FILAMENT, FilamentStepType::STEP_TYPE_LOAD);
                    }

                }
                else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PUSH_NEW_FILAMENT, FilamentStepType::STEP_TYPE_UNLOAD);
                }
            }
            else if (obj->ams_status_sub == 0x06) {
                if (!obj->is_ams_unload()) {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PUSH_NEW_FILAMENT, FilamentStepType::STEP_TYPE_LOAD);
                }
                else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PUSH_NEW_FILAMENT, FilamentStepType::STEP_TYPE_UNLOAD);
                }
            }
            else if (obj->ams_status_sub == 0x07) {
                if (!obj->is_ams_unload()) {
                    if (/*m_is_load_with_temp*/false) {
                        m_ams_control->SetFilamentStep(FilamentStep::STEP_PULL_CURR_FILAMENT, FilamentStepType::STEP_TYPE_LOAD);
                    }
                    else {
                        m_ams_control->SetFilamentStep(FilamentStep::STEP_PURGE_OLD_FILAMENT, FilamentStepType::STEP_TYPE_LOAD);
                    }
                }
                else {
                    m_ams_control->SetFilamentStep(FilamentStep::STEP_PURGE_OLD_FILAMENT, FilamentStepType::STEP_TYPE_UNLOAD);
                }
            }
            else {
                m_ams_control->SetFilamentStep(FilamentStep::STEP_IDLE, FilamentStepType::STEP_TYPE_UNLOAD);
            }
        }
    }
    else if (obj->ams_status_main == AMS_STATUS_MAIN_ASSIST) {
        m_ams_control->SetFilamentStep(FilamentStep::STEP_IDLE, FilamentStepType::STEP_TYPE_LOAD);
    }
    else {
        m_ams_control->SetFilamentStep(FilamentStep::STEP_IDLE, FilamentStepType::STEP_TYPE_LOAD);
    }


    for (auto ams_it = obj->amsList.begin(); ams_it != obj->amsList.end(); ams_it++) {
        std::string ams_id = ams_it->first;
        try {
            int ams_id_int = atoi(ams_id.c_str());
            for (auto tray_it = ams_it->second->trayList.begin(); tray_it != ams_it->second->trayList.end(); tray_it++) {
                std::string tray_id = tray_it->first;
                int         tray_id_int = atoi(tray_id.c_str());
                // new protocol
                if ((obj->tray_reading_bits & (1 << (ams_id_int * 4 + tray_id_int))) != 0) {
                    m_ams_control->PlayRridLoading(ams_id, tray_id);
                }
                else {
                    m_ams_control->StopRridLoading(ams_id, tray_id);
                }
            }
        }
        catch (...) {}
    }

    bool is_curr_tray_selected = false;
    if (!curr_ams_id.empty() && !curr_can_id.empty() && (curr_ams_id != std::to_string(VIRTUAL_TRAY_ID))) {
        if (curr_can_id == obj->m_tray_now) {
            is_curr_tray_selected = true;
        }
        else {
            std::map<std::string, Ams*>::iterator it = obj->amsList.find(curr_ams_id);
            if (it == obj->amsList.end()) {
                BOOST_LOG_TRIVIAL(trace) << "ams: find " << curr_ams_id << " failed";
                return;
            }
            auto tray_it = it->second->trayList.find(curr_can_id);
            if (tray_it == it->second->trayList.end()) {
                BOOST_LOG_TRIVIAL(trace) << "ams: find " << curr_can_id << " failed";
                return;
            }

            if (!tray_it->second->is_exists) {
                is_curr_tray_selected = true;
            }
        }
    }
    else if (curr_ams_id == std::to_string(VIRTUAL_TRAY_ID)) {
        if (curr_ams_id == obj->m_tray_now) {
            is_curr_tray_selected = true;
        }
    }
    else {
        is_curr_tray_selected = true;
    }

    //update_ams_control_state(is_support_extrusion_cali, is_curr_tray_selected);
}

void CalibrationWizard::update_progress()
{
    if (obj) {
        if (/*obj->is_in_extrusion_cali()*/true) {
            //show_info(true, false, wxString::Format(_L("Calibrating... %d%%"), obj->mc_print_percent));
            //m_cali_cancel->Show();
            //m_cali_cancel->Enable();
            //m_button_cali->Hide();
            //m_button_next_step->Hide();
        }
        else if (obj->is_extrusion_cali_finished()) {
            if (/*m_bed_temp->GetTextCtrl()->GetValue().compare("0") == 0*/false) {
                wxString tips = get_presets_incompatible();
                //show_info(true, true, tips);
            }
            else {
                get_presets_incompatible();
                //show_info(true, false, _L("Calibration completed"));
            }
            //m_cali_cancel->Hide();
            //m_button_cali->Show();
            //m_button_next_step->Show();
        }
        else {
            if (/*m_bed_temp->GetTextCtrl()->GetValue().compare("0") == 0*/false) {
                wxString tips = get_presets_incompatible();
                //show_info(true, true, tips);
            }
            else {
                get_presets_incompatible();
                //show_info(true, false, wxEmptyString);
            }
            //m_cali_cancel->Hide();
            //m_button_cali->Show();
            //m_button_next_step->Hide();
        }
        Layout();
    }
}

void CalibrationWizard::on_select_printer(wxCommandEvent& evt) {
    init_filaments_selections();
    init_process_selections();
    update_calibration_value();
}
void CalibrationWizard::on_select_filament(wxCommandEvent& evt) {
    update_calibration_value();
}
void CalibrationWizard::on_select_bed_type(wxCommandEvent& evt) {
    update_calibration_value();
}
void CalibrationWizard::on_select_process(wxCommandEvent& evt) {
    update_calibration_value();
}

void CalibrationWizard::init_presets_selections() {
    init_printer_selections();
    init_filaments_selections();
    init_bed_type_selections();
    init_process_selections();
}

void CalibrationWizard::init_printer_selections()
{
    m_comboBox_printer->SetValue(wxEmptyString);
    int curr_selection = 1;
    wxArrayString printer_items;
    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        for (auto printer_it = preset_bundle->printers.begin(); printer_it != preset_bundle->printers.end(); printer_it++) {
            wxString printer_name = wxString::FromUTF8(printer_it->name);
            printer_items.Add(printer_name);
        }
        m_comboBox_printer->Set(printer_items);
        m_comboBox_printer->SetSelection(curr_selection);
    }
}

void CalibrationWizard::init_filaments_selections()
{
    m_comboBox_filament->SetValue(wxEmptyString);
    int filament_index = -1;
    int curr_selection = -1;
    wxArrayString filament_items;
    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        for (auto filament_it = preset_bundle->filaments.begin(); filament_it != preset_bundle->filaments.end(); filament_it++) {
            ConfigOption* printer_opt = filament_it->config.option("compatible_printers");
            ConfigOptionStrings* printer_strs = dynamic_cast<ConfigOptionStrings*>(printer_opt);
            for (auto printer_str : printer_strs->values) {
                if (printer_str == m_comboBox_printer->GetValue()) {// todo modify
                    // set default filament id
                    filament_index++;
                    if (filament_it->is_system
                        /*&& !ams_filament_id.empty()*/
                        /*&& filament_it->filament_id == ams_filament_id*/)
                        ;//curr_selection = filament_index;

                    wxString filament_name = wxString::FromUTF8(filament_it->name);
                    filament_items.Add(filament_name);
                    break;
                }
            }
        }
        m_comboBox_filament->Set(filament_items);
        m_comboBox_filament->SetSelection(curr_selection);
    }
}

void CalibrationWizard::init_bed_type_selections()
{
    m_comboBox_bed_type->SetValue(wxEmptyString);
    int curr_selection = 0;
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

void CalibrationWizard::init_process_selections()
{
    m_comboBox_process->SetValue(wxEmptyString);
    int process_index = -1;
    int curr_selection = -1;
    wxArrayString process_items;
    PresetBundle* preset_bundle = wxGetApp().preset_bundle;
    if (preset_bundle) {
        for (auto process_it = preset_bundle->prints.begin(); process_it != preset_bundle->prints.end(); process_it++) {
            ConfigOption* printer_opt = process_it->config.option("compatible_printers");
            ConfigOptionStrings* printer_strs = dynamic_cast<ConfigOptionStrings*>(printer_opt);
            for (auto printer_str : printer_strs->values) {
                if (printer_str == m_comboBox_printer->GetValue()) {// todo modify
                    // set default filament id
                    process_index++;
                    if (process_it->is_system)
                        ;//curr_selection = process_index;

                    wxString process_name = wxString::FromUTF8(process_it->name);
                    process_items.Add(process_name);
                    break;
                }
            }
        }
        m_comboBox_process->Set(process_items);
        m_comboBox_process->SetSelection(curr_selection);
    }
}

FlowRateWizard::FlowRateWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, id, pos, size, style)
{
    create_pages();
}

void FlowRateWizard::create_pages()
{
    wxBoxSizer* all_pages_sizer;
    all_pages_sizer = new wxBoxSizer(wxVERTICAL);

    // page 1
    m_page1 = new CalibrationWizardPage(m_background_panel, false);
    m_page1->set_page_title(_L("Flow Rate"));
    m_page1->set_page_index(_L("1/5"));

    auto page1_top_sizer = m_page1->get_top_vsizer();
    auto page1_top_description = new wxStaticText(m_page1, wxID_ANY, _L("Flow rate calibration in 3D printing is an important process that ensures the printer is extruding the correct amount of filament during the printing process. It is suitable for materials with significant thermal shrinkage/expansion and materials with inaccurate filament diameter."), wxDefaultPosition, wxDefaultSize, 0);
    page1_top_description->Wrap(FromDIP(1000));
    page1_top_description->SetMinSize(wxSize(1000, -1));
    page1_top_description->SetFont(::Label::Body_14);
    page1_top_sizer->Add(page1_top_description, 0, wxALL, 0);
    page1_top_sizer->AddSpacer(FromDIP(20));

    auto page1_right_content_sizer = m_page1->get_right_content_vsizer();
    auto page1_bitmaps_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto page1_bitmap1 = new wxStaticBitmap(m_page1, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    auto page1_bitmap2 = new wxStaticBitmap(m_page1, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    page1_bitmaps_sizer->Add(page1_bitmap1, 1, wxEXPAND, 0);
    page1_bitmaps_sizer->Add(page1_bitmap2, 1, wxEXPAND, 0);
    page1_right_content_sizer->Add(page1_bitmaps_sizer, 0, wxEXPAND, 0);

    auto page1_prev_btn = m_page1->get_prev_btn();
    page1_prev_btn->Hide();

    auto page1_next_btn = m_page1->get_next_btn();
    page1_next_btn->SetLabel(_L("Start"));
    page1_next_btn->SetButtonType(ButtonType::Start);

    all_pages_sizer->Add(m_page1, 1, wxEXPAND | wxALL, FromDIP(10));

    // page 2
    m_page2 = new CalibrationWizardPage(m_background_panel, true);
    m_page2->set_page_title(_L("Flow Rate"));
    m_page2->set_page_index(_L("2/5"));

    auto page2_left_sizer = m_page2->get_left_vsizer();

    m_from_text->Hide();
    m_to_text->Hide();
    m_step_text->Hide();
    m_from_value->Hide();
    m_to_value->Hide();
    m_step->Hide();

    add_presets_panel_to_page(m_page2, page2_left_sizer);

    auto page2_right_content_sizer = m_page2->get_right_content_vsizer();

    auto page2_description = new wxStaticText(m_page2, wxID_ANY, _L("Please select the printer, the slot that contains the target filament, and fill the settings before calibrating.\n\nA test model will be printed.This step may take about 50 minutes."), wxDefaultPosition, wxDefaultSize, 0);
    page2_description->Wrap(FromDIP(500));
    page2_description->SetFont(::Label::Body_14);
    page2_right_content_sizer->Add(page2_description, 0, wxEXPAND, 0);

    auto page2_bitmap = new wxStaticBitmap(m_page2, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    page2_right_content_sizer->Add(page2_bitmap, 0, wxEXPAND, 0);

    add_progress_bar_to_page(m_page2, page2_right_content_sizer);

    auto page2_prev_btn = m_page2->get_prev_btn();
    page2_prev_btn->SetLabel(_L("Back"));
    page2_prev_btn->SetButtonType(ButtonType::Back);

    auto page2_next_btn = m_page2->get_next_btn();
    page2_next_btn->SetLabel(_L("Coarse Tune"));
    page2_next_btn->SetButtonType(ButtonType::Calibrate);

    all_pages_sizer->Add(m_page2, 1, wxEXPAND | wxALL, FromDIP(10));

    // page 3
    m_page3 = new CalibrationWizardPage(m_background_panel, false);
    m_page3->set_page_title(_L("Flow Rate"));
    m_page3->set_page_index(_L("3/5"));

    auto page3_top_sizer = m_page3->get_top_vsizer();
    auto page3_top_description = new wxStaticText(m_page3, wxID_ANY, _L("The calibration blocks has been printed. Examine the blocks and determine which one has the smoothest top surface."), wxDefaultPosition, wxDefaultSize, 0);
    page3_top_description->Wrap(-1);
    page3_top_description->SetFont(::Label::Body_14);
    page3_top_sizer->Add(page3_top_description, 0, wxALL, 0);
    page3_top_sizer->AddSpacer(FromDIP(20));

    auto page3_right_content_sizer = m_page3->get_right_content_vsizer();
    auto page3_bitmap = new wxStaticBitmap(m_page3, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    page3_right_content_sizer->Add(page3_bitmap, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);

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
    coarse_value_sizer->Add(coarse_value_text, 0, wxBOTTOM, FromDIP(20));
    coarse_value_sizer->Add(m_optimal_block_coarse, 0);
    page3_right_content_sizer->Add(coarse_value_sizer, 0, wxEXPAND);

    auto page3_prev_btn = m_page3->get_prev_btn();
    page3_prev_btn->SetLabel(_L("Back"));
    page3_prev_btn->SetButtonType(ButtonType::Back);

    auto page3_next_btn = m_page3->get_next_btn();
    page3_next_btn->SetLabel(_L("Next"));
    page3_next_btn->SetButtonType(ButtonType::Next);

    all_pages_sizer->Add(m_page3, 1, wxEXPAND | wxALL, FromDIP(10));

    // page 4
    m_page4 = new CalibrationWizardPage(m_background_panel, true);
    m_page4->set_page_title(_L("Flow Rate"));
    m_page4->set_page_index(_L("4/5"));

    create_readonly_presets_panel();

    auto page4_right_content_sizer = m_page4->get_right_content_vsizer();

    auto page4_description = new wxStaticText(m_page4, wxID_ANY, _L("Perform find tunning for to find the more fitting flow rate. A test model will be printed. This step may take about 30 minutes."), wxDefaultPosition, wxDefaultSize, 0);
    page4_description->Wrap(FromDIP(500));
    page4_description->SetFont(::Label::Body_14);
    page4_right_content_sizer->Add(page4_description, 0, wxEXPAND, 0);

    auto page4_bitmap = new wxStaticBitmap(m_page4, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    page4_right_content_sizer->Add(page4_bitmap, 0, wxEXPAND, 0);

    auto clear_tips = new wxStaticText(m_page4, wxID_ANY, _L("Clear the build plate and place it back to the hot bed before calibration"), wxDefaultPosition, wxDefaultSize, 0);
    clear_tips->Wrap(FromDIP(500));
    clear_tips->SetFont(::Label::Body_14);
    clear_tips->SetForegroundColour(wxColour(255, 151, 0));
    page4_right_content_sizer->Add(clear_tips, 0, wxEXPAND, 0);

    add_progress_bar_to_page(m_page4, page4_right_content_sizer);

    auto page4_prev_btn = m_page4->get_prev_btn();
    page4_prev_btn->SetLabel(_L("Back"));
    page4_prev_btn->SetButtonType(ButtonType::Back);

    auto page4_next_btn = m_page4->get_next_btn();
    page4_next_btn->SetLabel(_L("Fine Tune"));
    page4_next_btn->SetButtonType(ButtonType::Calibrate);

    all_pages_sizer->Add(m_page4, 1, wxEXPAND | wxALL, FromDIP(10));

    // page 5
    m_page5 = new CalibrationWizardPage(m_background_panel, false);
    m_page5->set_page_title(_L("Flow Rate"));
    m_page5->set_page_index(_L("5/5"));

    auto page5_top_sizer = m_page5->get_top_vsizer();
    auto page5_top_description = new wxStaticText(m_page5, wxID_ANY, _L("The calibration blocks has been printed. Examine the blocks and determine which one has the smoothest top surface. Fill in the number above that block and click the Save button to save the calibrated flow rate to the filament preset."), wxDefaultPosition, wxDefaultSize, 0);
    page5_top_description->Wrap(-1);
    page5_top_description->SetFont(::Label::Body_14);
    page5_top_sizer->Add(page5_top_description, 0, wxALL, 0);
    page5_top_sizer->AddSpacer(FromDIP(20));

    auto page5_right_content_sizer = m_page5->get_right_content_vsizer();
    auto page5_bitmap = new wxStaticBitmap(m_page5, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    page5_right_content_sizer->Add(page5_bitmap, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);

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
    fine_value_sizer->Add(fine_value_text, 0, wxBOTTOM, FromDIP(20));
    fine_value_sizer->Add(m_optimal_block_fine, 0);
    page5_right_content_sizer->Add(fine_value_sizer, 0, wxEXPAND);

    auto page5_prev_btn = m_page5->get_prev_btn();
    page5_prev_btn->SetLabel(_L("Back"));
    page5_prev_btn->SetButtonType(ButtonType::Back);

    auto page5_next_btn = m_page5->get_next_btn();
    page5_next_btn->SetLabel(_L("Save"));
    page5_next_btn->SetButtonType(ButtonType::Save);

    all_pages_sizer->Add(m_page5, 1, wxEXPAND | wxALL, FromDIP(10));


    m_background_panel->SetSizer(all_pages_sizer);
    m_background_panel->Layout();
    all_pages_sizer->Fit(m_background_panel);

    // link page
    m_page1->chain(m_page2)->chain(m_page3)->chain(m_page4)->chain(m_page5);

    m_first_page = m_page1;
    m_curr_page = m_page1;
    show_page(m_curr_page);
}

void FlowRateWizard::create_readonly_presets_panel()
{
    auto page4_left_sizer = m_page4->get_left_vsizer();

    auto readonly_presets_panel = new wxPanel(m_page4);
    auto panel_sizer = new wxBoxSizer(wxVERTICAL);

    m_readonly_ams_control = new AMSControl(readonly_presets_panel);
    m_readonly_ams_control->EnterSimpleMode();
    panel_sizer->Add(m_readonly_ams_control, 0, wxALL, 0);

    auto printer_text = new wxStaticText(readonly_presets_panel, wxID_ANY, _L("Printer"), wxDefaultPosition, wxDefaultSize, 0);
    printer_text->Wrap(-1);
    panel_sizer->Add(printer_text, 0, wxALL, 0);

    m_readonly_printer = new TextInput(readonly_presets_panel, wxEmptyString, "", "", wxDefaultPosition, CALIBRATION_COMBOX_SIZE, wxTE_READONLY);
    panel_sizer->Add(m_readonly_printer, 0, wxALL, 0);

    panel_sizer->Add(0, FromDIP(10), 0, wxEXPAND, 0);

    auto filament_text = new wxStaticText(readonly_presets_panel, wxID_ANY, _L("Filament"), wxDefaultPosition, wxDefaultSize, 0);
    filament_text->Wrap(-1);
    panel_sizer->Add(filament_text, 0, wxALL, 0);

    m_readonly_filament = new TextInput(readonly_presets_panel, wxEmptyString, "", "", wxDefaultPosition, CALIBRATION_COMBOX_SIZE, wxTE_READONLY);
    panel_sizer->Add(m_readonly_filament, 0, wxALL, 0);

    panel_sizer->Add(0, FromDIP(10), 0, wxEXPAND, 0);

    auto plate_type_text = new wxStaticText(readonly_presets_panel, wxID_ANY, _L("Plate Type"), wxDefaultPosition, wxDefaultSize, 0);
    plate_type_text->Wrap(-1);
    panel_sizer->Add(plate_type_text, 0, wxALL, 0);

    m_readonly_bed_type = new TextInput(readonly_presets_panel, wxEmptyString, "", "", wxDefaultPosition, CALIBRATION_COMBOX_SIZE, wxTE_READONLY);
    panel_sizer->Add(m_readonly_bed_type, 0, wxALL, 0);

    panel_sizer->Add(0, FromDIP(10), 0, wxEXPAND, 0);

    auto process_text = new wxStaticText(readonly_presets_panel, wxID_ANY, _L("Process"), wxDefaultPosition, wxDefaultSize, 0);
    process_text->Wrap(-1);
    panel_sizer->Add(process_text, 0, wxALL, 0);

    m_readonly_process = new TextInput(readonly_presets_panel, wxEmptyString, "", "", wxDefaultPosition, CALIBRATION_COMBOX_SIZE, wxTE_READONLY);
    panel_sizer->Add(m_readonly_process, 0, wxALL, 0);

    readonly_presets_panel->SetSizer(panel_sizer);
    readonly_presets_panel->Layout();
    panel_sizer->Fit(readonly_presets_panel);

    page4_left_sizer->Add(readonly_presets_panel, 0, wxEXPAND, 0);

    m_readonly_printer->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [](auto&) {});
    m_readonly_filament->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [](auto&) {});
    m_readonly_bed_type->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [](auto&) {});
    m_readonly_process->GetTextCtrl()->Bind(wxEVT_SET_FOCUS, [](auto&) {});
}

bool FlowRateWizard::start_calibration(std::string tray_id)
{
    int pass = -1;
    if (m_curr_page == m_page2)
        pass = 1;
    else if (m_curr_page == m_page4)
        pass = 2;
    else 
        return false;

    CalibUtils::calib_flowrate(pass, obj->dev_id, tray_id, std::shared_ptr<ProgressIndicator>(m_progress_bar));
    return true;
}

void FlowRateWizard::update_calibration_value()
{
    m_readonly_printer->GetTextCtrl()->SetLabel(m_comboBox_printer->GetValue());
    m_readonly_filament->GetTextCtrl()->SetLabel(m_comboBox_filament->GetValue());
    m_readonly_bed_type->GetTextCtrl()->SetLabel(m_comboBox_bed_type->GetValue());
    m_readonly_process->GetTextCtrl()->SetLabel(m_comboBox_process->GetValue());
}

MaxVolumetricSpeedWizard::MaxVolumetricSpeedWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, id, pos, size, style) 
{
    create_pages();
}

void MaxVolumetricSpeedWizard::create_pages() 
{
    wxBoxSizer* all_pages_sizer;
    all_pages_sizer = new wxBoxSizer(wxVERTICAL);

    // page 1
    m_page1 = new CalibrationWizardPage(m_background_panel, false);
    m_page1->set_page_title(_L("Max Volumetric Speed"));
    m_page1->set_page_index(_L("1/3"));

    auto page1_top_sizer = m_page1->get_top_vsizer();
    auto page1_top_description = new wxStaticText(m_page1, wxID_ANY, _L("This setting stands for how much volume of filament can be melted and extruded per second."), wxDefaultPosition, wxDefaultSize, 0);
    page1_top_description->Wrap(-1); 
    page1_top_description->SetFont(::Label::Body_14);
    page1_top_sizer->Add(page1_top_description, 0, wxALL, 0);
    page1_top_sizer->AddSpacer(FromDIP(20));

    auto page1_right_content_sizer = m_page1->get_right_content_vsizer();

    wxFlexGridSizer* fgSizer;
    fgSizer = new wxFlexGridSizer(0, 2, FromDIP(60), FromDIP(20));
    fgSizer->SetFlexibleDirection(wxBOTH);
    fgSizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    auto page1_description1 = new wxStaticText(m_page1, wxID_ANY, _L("If the value is set too high, under-extrusion will happen and cause poor apperance on the printed model."), wxDefaultPosition, wxDefaultSize, 0);
    page1_description1->Wrap(FromDIP(500));
    page1_description1->SetFont(::Label::Body_14);
    fgSizer->Add(page1_description1, 1, wxALL | wxEXPAND, 0);

    auto page1_bitmap1 = new wxStaticBitmap(m_page1, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    fgSizer->Add(page1_bitmap1, 1, wxALL | wxEXPAND, 0);

    auto page1_description2 = new wxStaticText(m_page1, wxID_ANY, _L("If the value is set too low, the print speed will be limited and make the print time longer. Take the model on the right picture for example.\nmax volumetric speed [n] mm^3/s costs [x] minutes.\nmax volumetric speed [m] mm^3/s costs [y] minutes"), wxDefaultPosition, wxDefaultSize, 0);
    page1_description2->Wrap(FromDIP(500));
    page1_description2->SetFont(::Label::Body_14);
    fgSizer->Add(page1_description2, 1, wxALL | wxEXPAND, 0);

    auto page1_bitmap2 = new wxStaticBitmap(m_page1, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    fgSizer->Add(page1_bitmap2, 1, wxALL | wxEXPAND, 0);

    page1_right_content_sizer->Add(fgSizer, 1, wxEXPAND, 0);

    auto page1_prev_btn = m_page1->get_prev_btn();
    page1_prev_btn->Hide();

    auto page1_next_btn = m_page1->get_next_btn();
    page1_next_btn->SetLabel(_L("Start"));
    page1_next_btn->SetButtonType(ButtonType::Start);

    all_pages_sizer->Add(m_page1, 1, wxEXPAND | wxALL, FromDIP(10));

    // page 2
    m_page2 = new CalibrationWizardPage(m_background_panel, true);
    m_page2->set_page_title(_L("Max Volumetric Speed"));
    m_page2->set_page_index(_L("2/3"));

    auto page2_left_sizer = m_page2->get_left_vsizer();

    m_from_text->SetLabel(_L("From Speed"));
    m_to_text->SetLabel(_L("To Speed"));
    m_from_value->SetLabel(_L("mm\u00B3/s"));
    m_to_value->SetLabel(_L("mm\u00B3/s"));
    m_step->SetLabel(_L("mm\u00B3/s"));
    m_step->GetTextCtrl()->SetLabel("0.5");

    add_presets_panel_to_page(m_page2, page2_left_sizer);

    auto page2_right_content_sizer = m_page2->get_right_content_vsizer();

    auto page2_description = new wxStaticText(m_page2, wxID_ANY, _L("Please select the printer, the slot that contains the target filament, and fill the settings before calibrating.\n\nA test model will be printed. This step may take about 50 minutes."), wxDefaultPosition, wxDefaultSize, 0);
    page2_description->Wrap(FromDIP(500));
    page2_description->SetFont(::Label::Body_14);
    page2_right_content_sizer->Add(page2_description, 0, wxEXPAND, 0);

    auto page2_bitmap = new wxStaticBitmap(m_page2, wxID_ANY, create_scaled_bitmap("max_volumetric_speed_wizard", nullptr, 400), wxDefaultPosition, wxDefaultSize, 0);
    page2_right_content_sizer->Add(page2_bitmap, 0, wxEXPAND, 0);

    add_progress_bar_to_page(m_page2, page2_right_content_sizer);

    auto page2_prev_btn = m_page2->get_prev_btn();
    page2_prev_btn->Hide();

    auto page2_next_btn = m_page2->get_next_btn();
    page2_next_btn->SetLabel(_L("Calibrate"));
    page2_next_btn->SetButtonType(ButtonType::Calibrate);

    all_pages_sizer->Add(m_page2, 1, wxEXPAND | wxALL, FromDIP(10));

    // page 3
    m_page3 = new CalibrationWizardPage(m_background_panel, false);
    m_page3->set_page_title(_L("Max Volumetric Speed"));
    m_page3->set_page_index(_L("3/3"));

    auto page3_top_sizer = m_page3->get_top_vsizer();
    auto page3_top_description = new wxStaticText(m_page3, wxID_ANY, _L("The calibration model has been printed."), wxDefaultPosition, wxDefaultSize, 0);
    page3_top_description->Wrap(-1);
    page3_top_description->SetFont(::Label::Body_14);
    page3_top_sizer->Add(page3_top_description, 0, wxALL, 0);
    page3_top_sizer->AddSpacer(FromDIP(20));

    auto page3_right_content_sizer = m_page3->get_right_content_vsizer();
    auto page3_bitmap = new wxStaticBitmap(m_page3, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    page3_right_content_sizer->Add(page3_bitmap, 0, wxALL | wxALIGN_CENTER_HORIZONTAL, 0);

    auto value_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto value_text = new wxStaticText(m_page3, wxID_ANY, _L("Input Value"), wxDefaultPosition, wxDefaultSize, 0);
    value_text->Wrap(-1);
    value_text->SetFont(::Label::Body_14);
    m_optimal_max_speed = new TextInput(m_page3, wxEmptyString, "", "", wxDefaultPosition, CALIBRATION_OPTIMAL_INPUT_SIZE, 0);
    value_sizer->Add(value_text, 0, wxRight, FromDIP(20));
    value_sizer->Add(m_optimal_max_speed, 0);
    page3_right_content_sizer->Add(value_sizer, 0, wxEXPAND);

    auto page3_prev_btn = m_page3->get_prev_btn();
    page3_prev_btn->SetLabel(_L("Re-Calibrate"));
    page3_prev_btn->SetButtonType(ButtonType::Recalibrate);

    auto page3_next_btn = m_page3->get_next_btn();
    page3_next_btn->SetLabel(_L("Save"));
    page3_next_btn->SetButtonType(ButtonType::Save);

    all_pages_sizer->Add(m_page3, 1, wxEXPAND | wxALL, FromDIP(10));

    m_background_panel->SetSizer(all_pages_sizer);
    m_background_panel->Layout();
    all_pages_sizer->Fit(m_background_panel);

    // link page
    m_page1->chain(m_page2)->chain(m_page3);

    m_first_page = m_page1;
    m_curr_page = m_page1;
    show_page(m_curr_page);
}

bool MaxVolumetricSpeedWizard::start_calibration(std::string tray_id)
{
    Calib_Params params;
    m_from_value->GetTextCtrl()->GetValue().ToDouble(&params.start);
    m_to_value->GetTextCtrl()->GetValue().ToDouble(&params.end);
    params.mode = CalibMode::Calib_Vol_speed_Tower;

    if (params.start <= 0 || params.step <= 0 || params.end < (params.start + params.step)) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nFrom > 0 \Step >= 0\nTo > From + Step"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    CalibUtils::calib_max_vol_speed(params, obj->dev_id, tray_id, std::shared_ptr<ProgressIndicator>(m_progress_bar));
    return true;
}

void MaxVolumetricSpeedWizard::update_calibration_value()
{
    if (m_comboBox_printer->GetValue().IsEmpty() ||
        m_comboBox_filament->GetValue().IsEmpty() ||
        m_comboBox_bed_type->GetValue().IsEmpty() ||
        m_comboBox_process->GetValue().IsEmpty())
    {
        m_from_value->GetTextCtrl()->SetValue(wxEmptyString);
        m_to_value->GetTextCtrl()->SetValue(wxEmptyString);
        return;
    }

    m_from_value->GetTextCtrl()->SetValue("5");
    m_to_value->GetTextCtrl()->SetValue("20");
}

TemperatureWizard::TemperatureWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, id, pos, size, style)
{
    create_pages();
}

void TemperatureWizard::create_pages()
{
    wxBoxSizer* all_pages_sizer;
    all_pages_sizer = new wxBoxSizer(wxVERTICAL);

    // page 1
    m_page1 = new CalibrationWizardPage(m_background_panel, true);
    m_page1->set_page_title(_L("Temperature Calibration"));
    m_page1->set_page_index(_L("1/2"));

    auto page1_left_sizer = m_page1->get_left_vsizer();

    add_presets_panel_to_page(m_page1, page1_left_sizer);

    auto page1_right_content_sizer = m_page1->get_right_content_vsizer();
    auto description1 = new wxStaticText(m_page1, wxID_ANY, _L("Temp tower is a straightforward test. The temp tower is a vertical tower with multiple blocks, each printed at a different temperature. Once the print is complete, we can examine each block of the tower and determine the optimal temperature for the filament.\n\nBy click the \"Calibrate\" button, the temp tower will be sent to your printer and start printing. The printing job may take about 50 minutes."), wxDefaultPosition, wxDefaultSize, 0);
    description1->Wrap(500);
    description1->SetFont(::Label::Body_14);
    description1->SetMinSize({ FromDIP(500), -1 });
    page1_right_content_sizer->Add(description1, 0, wxALL | wxEXPAND, 0);

    auto picture_description1 = new wxStaticBitmap(m_page1, wxID_ANY, create_scaled_bitmap("temperature_wizard1", nullptr, 450), wxDefaultPosition, wxDefaultSize, 0);
    page1_right_content_sizer->Add(picture_description1, 1, wxALIGN_LEFT | wxEXPAND, 0);

    page1_right_content_sizer->Add(0, FromDIP(20), 0, wxEXPAND, 0);

    m_from_text->SetLabel(_L("From Temp"));
    m_to_text->SetLabel(_L("To Temp"));
    m_step->Enable(false);

    add_progress_bar_to_page(m_page1, page1_right_content_sizer);

    auto page1_prev_btn = m_page1->get_prev_btn();
    page1_prev_btn->Hide();

    auto page1_next_btn = m_page1->get_next_btn();
    page1_next_btn->SetLabel(_L("Calibrate"));
    page1_next_btn->SetButtonType(ButtonType::Calibrate);

    all_pages_sizer->Add(m_page1, 1, wxEXPAND | wxALL, FromDIP(10));


    // page 2
    m_page2 = new CalibrationWizardPage(m_background_panel, false);
    m_page2->set_page_title(_L("Temperature Calibration"));
    m_page2->set_page_index(_L("2/2"));

    auto page2_left_sizer = m_page2->get_left_vsizer();
    auto picture_description2 = new wxStaticBitmap(m_page2, wxID_ANY, create_scaled_bitmap("temperature_wizard2", nullptr, 650), wxDefaultPosition, wxDefaultSize, 0);
    page2_left_sizer->Add(picture_description2, 1, wxALL | wxEXPAND, 0);

    auto page2_right_content_sizer = m_page2->get_right_content_vsizer();
    auto description2 = new wxStaticText(m_page2, wxID_ANY, _L("The calibration model has been printed.\nPlease find the optimal temperature is the one that produces the highest quality print with the least amount of issues, such as stringing, layer adhesion, warping (overhang), and bridging."), wxDefaultPosition, wxDefaultSize, 0);
    description2->Wrap(500);
    description2->SetFont(::Label::Body_14);
    page2_right_content_sizer->Add(description2, 0, wxALL, 0);

    page2_right_content_sizer->Add(0, FromDIP(20), 0, wxEXPAND, 0);

    wxBoxSizer* optimal_temp_szier;
    optimal_temp_szier = new wxBoxSizer(wxHORIZONTAL);

    auto optimal_temp_text = new wxStaticText(m_page2, wxID_ANY, _L("Optimal Temp"), wxDefaultPosition, wxDefaultSize, 0);
    optimal_temp_text->Wrap(-1);
    optimal_temp_text->SetFont(::Label::Body_14);
    optimal_temp_szier->Add(optimal_temp_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, 0);

    optimal_temp_szier->Add(FromDIP(10), 0, 0, wxEXPAND, 0);

    m_optimal_temp = new TextInput(m_page2, wxEmptyString, _L("\u2103"), "", wxDefaultPosition, CALIBRATION_OPTIMAL_INPUT_SIZE, 0);
    optimal_temp_szier->Add(m_optimal_temp, 0, wxALL, 0);

    page2_right_content_sizer->Add(optimal_temp_szier, 0, wxEXPAND, 0);

    auto page2_prev_btn = m_page2->get_prev_btn();
    page2_prev_btn->SetLabel(_L("Re-Calibrate"));
    page2_prev_btn->SetButtonType(ButtonType::Recalibrate);

    auto page2_next_btn = m_page2->get_next_btn();
    page2_next_btn->SetLabel(_L("Save"));
    page2_next_btn->SetButtonType(ButtonType::Save);

    all_pages_sizer->Add(m_page2, 1, wxEXPAND | wxALL, FromDIP(10));

    m_background_panel->SetSizer(all_pages_sizer);
    m_background_panel->Layout();
    all_pages_sizer->Fit(m_background_panel);

    // link pages
    m_page1->chain(m_page2);

    m_first_page = m_page1;
    m_curr_page = m_page1;
    show_page(m_curr_page);
}

bool TemperatureWizard::start_calibration(std::string tray_id)
{
    Calib_Params params;
    m_from_value->GetTextCtrl()->GetValue().ToDouble(&params.start);
    m_to_value->GetTextCtrl()->GetValue().ToDouble(&params.end);
    params.mode = CalibMode::Calib_Temp_Tower;

    if (params.start < 180 || params.end > 350 || params.end < (params.start + 5)) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nFrom temp: >= 180\nTo temp: <= 350\nFrom temp <= To temp - Step"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return false;
    }

    CalibUtils::calib_temptue(params, obj->dev_id, tray_id, std::shared_ptr<ProgressIndicator>(m_progress_bar));
    return true;
}

void TemperatureWizard::save_calibration_result()
{
    SavePresetDialog dlg(m_parent, Preset::Type::TYPE_FILAMENT, "");
    if (dlg.ShowModal() != wxID_OK)
        return;
    std::string name = dlg.get_name();
    bool save_to_project = dlg.get_save_to_project_selection(Preset::TYPE_FILAMENT); // todo remove save_to_project chioce


    auto m_presets = &wxGetApp().preset_bundle->filaments;
    // BBS record current preset name
    std::string curr_preset_name = m_presets->get_edited_preset().name;

    bool exist_preset = false; // todo remove new_preset->sync_info = "update" related logic 
    Preset* new_preset = m_presets->find_preset(name, false);
    if (new_preset) {
        exist_preset = true;
    }

    // Save the preset into Slic3r::data_dir / presets / section_name / preset_name.ini
    m_presets->save_current_preset(name, false, save_to_project);

    new_preset = m_presets->find_preset(name, false, true);
    if (!new_preset) {
        BOOST_LOG_TRIVIAL(info) << "create new preset failed";
        return;
    }

    // set sync_info for sync service
    if (exist_preset) {
        new_preset->sync_info = "update";
        BOOST_LOG_TRIVIAL(info) << "sync_preset: update preset = " << new_preset->name;
    }
    else {
        new_preset->sync_info = "create";
        if (wxGetApp().is_user_login())
            new_preset->user_id = wxGetApp().getAgent()->get_user_id();
        BOOST_LOG_TRIVIAL(info) << "sync_preset: create preset = " << new_preset->name;
    }
    new_preset->save_info();

    // Mark the print & filament enabled if they are compatible with the currently selected preset.
    // If saving the preset changes compatibility with other presets, keep the now incompatible dependent presets selected, however with a "red flag" icon showing that they are no more compatible.
    wxGetApp().preset_bundle->update_compatible(PresetSelectCompatibleType::Never);

    // update current comboBox selected preset
    if (!exist_preset) {
        wxGetApp().plater()->sidebar().update_presets_from_to(Preset::TYPE_FILAMENT, curr_preset_name, new_preset->name);
    }
}

void TemperatureWizard::update_calibration_value()
{
    if (m_comboBox_printer->GetValue().IsEmpty() ||
        m_comboBox_filament->GetValue().IsEmpty() || 
        m_comboBox_bed_type->GetValue().IsEmpty() || 
        m_comboBox_process->GetValue().IsEmpty()) 
    {
        m_from_value->GetTextCtrl()->SetValue(wxEmptyString);
        m_to_value->GetTextCtrl()->SetValue(wxEmptyString);
        return;
    }

    wxString filament_name = m_comboBox_filament->GetValue();

    unsigned long start, end;
    if (filament_name.Contains("ABS") || filament_name.Contains("ASA")) {//todo supplement
        start = 230;
        end = 270;
    }
    else if (filament_name.Contains("PETG")) {
        start = 230;
        end = 260;
    }
    else if(filament_name.Contains("TPU")) {
        start = 210;
        end = 240;
    }
    else if(filament_name.Contains("PA-CF")) {
        start = 280;
        end = 320;
    }
    else if(filament_name.Contains("PET-CF")) {
        start = 280;
        end = 320;
    }
    else if(filament_name.Contains("PLA")) {
        start = 190;
        end = 230;
    }
    else {
        start = 190;
        end = 230;
    }
    m_from_value->GetTextCtrl()->SetValue(std::to_string(start));
    m_to_value->GetTextCtrl()->SetValue(std::to_string(end));
}

}}