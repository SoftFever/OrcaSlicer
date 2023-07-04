#include "CalibrationWizard.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "CalibrationWizardPage.hpp"
#include "../../libslic3r/Calib.hpp"
#include "Tabbook.hpp"
#include "CaliHistoryDialog.hpp"

namespace Slic3r { namespace GUI {

#define CALIBRATION_DEBUG

wxDEFINE_EVENT(EVT_DEVICE_CHANGED, wxCommandEvent);
wxDEFINE_EVENT(EVT_CALIBRATION_JOB_FINISHED, wxCommandEvent);

static const wxString NA_STR = _L("N/A");


CalibrationWizard::CalibrationWizard(wxWindow* parent, CalibMode mode, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style) 
    , m_mode(mode)
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

    main_sizer->Add(m_scrolledWindow, 1, wxEXPAND | wxALL, FromDIP(10));

    this->SetSizer(main_sizer);
    this->Layout();
    main_sizer->Fit(this);

    Bind(EVT_CALIBRATION_JOB_FINISHED, &CalibrationWizard::on_cali_job_finished, this);
}

CalibrationWizard::~CalibrationWizard()
{
    ;
}

void CalibrationWizard::on_cali_job_finished(wxCommandEvent& event)
{
    this->on_cali_job_finished(event.GetString());
    event.Skip();
}

void CalibrationWizard::show_step(CalibrationWizardPageStep* step)
{
    if (!step)
        return;

    if (m_curr_step) {
        m_curr_step->page->Hide();
    }

    m_curr_step = step;

    if (m_curr_step) {
        m_curr_step->page->Show();
    }

    Layout();
}

void CalibrationWizard::update(MachineObject* obj)
{
    curr_obj = obj;

    /* only update curr step
    if (m_curr_step) {
        m_curr_step->page->update(obj);
    }
    */
    if (!obj) {
        for (int i = 0; i < m_page_steps.size(); i++) {
            if (m_page_steps[i]->page)
                m_page_steps[i]->page->on_reset_page();
        }
    }

    // update all page steps
    for (int i = 0; i < m_page_steps.size(); i++) {
        if (m_page_steps[i]->page)
            m_page_steps[i]->page->update(obj);
    }
}

void CalibrationWizard::on_device_connected(MachineObject* obj)
{
    if (!m_page_steps.empty())
        show_step(m_page_steps.front());

    for (int i = 0; i < m_page_steps.size(); i++) {
        if (m_page_steps[i]->page)
            m_page_steps[i]->page->on_device_connected(obj);
    }
}

void CalibrationWizard::set_cali_method(CalibrationMethod method)
{
    m_cali_method = method;
    for (int i = 0; i < m_page_steps.size(); i++) {
        if (m_page_steps[i]->page)
            m_page_steps[i]->page->set_cali_method(method);
    }
}

bool CalibrationWizard::save_preset(const std::string &old_preset_name, const std::string &new_preset_name, const std::map<std::string, ConfigOption *> &key_values, std::string& message)
{
    if (new_preset_name.empty()) {
        message = L("The name cannot be empty.");
        return false;
    }

    PresetCollection *filament_presets = &wxGetApp().preset_bundle->filaments;
    Preset* preset = filament_presets->find_preset(old_preset_name);
    if (!preset) {
        message = L("The selected preset has been deleted.");
        return false;
    }

    Preset temp_preset = *preset;

    std::string new_name = filament_presets->get_preset_name_by_alias(new_preset_name);
    bool exist_preset = false;
    // If name is current, get the editing preset
    Preset *new_preset = filament_presets->find_preset(new_name);
    if (new_preset) {
        if (new_preset->is_system) {
            message = L("The name cannot be the same as the system preset name.");
            return false;
        }

        if (new_preset != preset) {
            message = L("The name is the same as another existing preset name");
            return false;
        }
        if (new_preset != &filament_presets->get_edited_preset()) new_preset = &temp_preset;
        exist_preset = true;
    } else {
        new_preset = &temp_preset;
    }

    for (auto item : key_values) {
        new_preset->config.set_key_value(item.first, item.second);
    }

    // Save the preset into Slic3r::data_dir / presets / section_name / preset_name.ini
    filament_presets->save_current_preset(new_name, false, false, new_preset);

    // BBS create new settings
    new_preset = filament_presets->find_preset(new_name, false, true);
    // Preset* preset = &m_presets.preset(it - m_presets.begin(), true);
    if (!new_preset) {
        BOOST_LOG_TRIVIAL(info) << "create new preset failed";
        message = L("create new preset failed.");
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
    if (!exist_preset) { wxGetApp().plater()->sidebar().update_presets_from_to(Preset::Type::TYPE_FILAMENT, old_preset_name, new_preset->name); }

    // todo: zhimin
    // wxGetApp().mainframe->update_filament_tab_ui();
    return true;
}

void CalibrationWizard::cache_preset_info(MachineObject* obj, float nozzle_dia)
{
    if (!obj) return;

    CalibrationPresetPage* preset_page = (static_cast<CalibrationPresetPage*>(preset_step->page));

    std::map<int, Preset*> selected_filaments = preset_page->get_selected_filaments();

    obj->selected_cali_preset.clear();
    for (auto& item : selected_filaments) {
        CaliPresetInfo result;
        result.tray_id = item.first;
        result.nozzle_diameter = nozzle_dia;
        result.filament_id = item.second->filament_id;
        result.setting_id = item.second->setting_id;
        result.name = item.second->name;
        obj->selected_cali_preset.push_back(result);
    }

    CaliPresetStage stage;
    preset_page->get_cali_stage(stage, obj->cache_flow_ratio);
}

void CalibrationWizard::on_cali_go_home()
{
    // can go home? confirm to continue
    CalibrationMethod method;
    int cali_stage = 0;
    CalibMode obj_cali_mode = get_obj_calibration_mode(curr_obj, method, cali_stage);

    if (obj_cali_mode == m_mode && curr_obj && curr_obj->is_in_printing()) {
        if (go_home_dialog == nullptr)
            go_home_dialog = new SecondaryCheckDialog(this, wxID_ANY, _L("Confirm"));

        go_home_dialog->Bind(EVT_SECONDARY_CHECK_CONFIRM, [this, method](wxCommandEvent &e) {
            if (curr_obj) {
                curr_obj->command_task_abort();
            } else {
                assert(false);
            }
            if (!m_page_steps.empty())
                show_step(m_page_steps.front());
        });

        go_home_dialog->update_text(_L("Are you sure to cancel the current calibration and return to the home page?"));
        go_home_dialog->on_show();
    } else {
        if (!m_page_steps.empty()) show_step(m_page_steps.front());
    }
}

PressureAdvanceWizard::PressureAdvanceWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, CalibMode::Calib_PA_Line, id, pos, size, style)
{
    create_pages();
}

void PressureAdvanceWizard::create_pages()
{
    start_step  = new CalibrationWizardPageStep(new CalibrationPAStartPage(m_scrolledWindow));
    preset_step = new CalibrationWizardPageStep(new CalibrationPresetPage(m_scrolledWindow, m_mode, false));
    cali_step   = new CalibrationWizardPageStep(new CalibrationCaliPage(m_scrolledWindow, m_mode));
    save_step   = new CalibrationWizardPageStep(new CalibrationPASavePage(m_scrolledWindow));
    
    m_all_pages_sizer->Add(start_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(preset_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(cali_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(save_step->page, 1, wxEXPAND | wxALL, FromDIP(25));


    m_page_steps.push_back(start_step);
    m_page_steps.push_back(preset_step);
    m_page_steps.push_back(cali_step);
    m_page_steps.push_back(save_step);

    for (int i = 0; i < m_page_steps.size() -1; i++) {
        m_page_steps[i]->chain(m_page_steps[i+1]);
    }

    for (int i = 0; i < m_page_steps.size(); i++) {
        m_page_steps[i]->page->Hide();
        m_page_steps[i]->page->Bind(EVT_CALI_ACTION, &PressureAdvanceWizard::on_cali_action, this);
    }

    if (!m_page_steps.empty())
        show_step(m_page_steps.front());
}

void PressureAdvanceWizard::on_cali_action(wxCommandEvent& evt)
{
    CaliPageActionType action = static_cast<CaliPageActionType>(evt.GetInt());
    if (action == CaliPageActionType::CALI_ACTION_MANAGE_RESULT) {
        HistoryWindow history_dialog(this, m_calib_results_history);
        history_dialog.on_device_connected(curr_obj);
        history_dialog.ShowModal();
    }
    else if (action == CaliPageActionType::CALI_ACTION_MANUAL_CALI) {
        preset_step->page->set_cali_filament_mode(CalibrationFilamentMode::CALI_MODEL_SINGLE);
        set_cali_method(CalibrationMethod::CALI_METHOD_MANUAL);
        preset_step->page->on_device_connected(curr_obj);
        show_step(m_curr_step->next);
    }
    else if (action == CaliPageActionType::CALI_ACTION_AUTO_CALI) {
        CalibrationFilamentMode fila_mode = get_cali_filament_mode(curr_obj, m_mode);
        preset_step->page->set_cali_filament_mode(fila_mode);
        set_cali_method(CalibrationMethod::CALI_METHOD_AUTO);
        preset_step->page->on_device_connected(curr_obj);
        show_step(m_curr_step->next);
    }
    else if (action == CaliPageActionType::CALI_ACTION_NEXT) {
        show_step(m_curr_step->next);
    }
    else if (action == CaliPageActionType::CALI_ACTION_CALI_NEXT) {
        (static_cast<CalibrationPASavePage*>(save_step->page))->sync_cali_result(curr_obj);
        show_step(m_curr_step->next);
    }
    else if (action == CaliPageActionType::CALI_ACTION_PREV) {
        show_step(m_curr_step->prev);
    }
    else if (action == CaliPageActionType::CALI_ACTION_CALI) {
        on_cali_start();
        show_step(m_curr_step->next);
    }
    else if (action == CaliPageActionType::CALI_ACTION_GO_HOME) {
        on_cali_go_home();
    } else if (action == CaliPageActionType::CALI_ACTION_PA_SAVE) {
        on_cali_save();
    }
}

void PressureAdvanceWizard::update(MachineObject* obj)
{
    CalibrationWizard::update(obj);
}

void PressureAdvanceWizard::on_device_connected(MachineObject* obj)
{
    CalibrationWizard::on_device_connected(obj);

    CalibrationMethod method;
    int cali_stage = 0;
    CalibMode obj_cali_mode = get_obj_calibration_mode(obj, method, cali_stage);

    // show cali step when obj is in pa calibration
    if (obj) {
        CalibrationWizard::set_cali_method(method);

        if (m_curr_step != cali_step) {
            if (obj_cali_mode == m_mode) {
                if (obj->is_in_printing() || obj->is_printing_finished()) {
                    CalibrationWizard::set_cali_method(method);
                    show_step(cali_step);
                }
            }
        }
    }
}

static bool get_preset_info(const DynamicConfig& config, const BedType plate_type, int& nozzle_temp, int& bed_temp, float& max_volumetric_speed)
{
    const ConfigOptionInts* nozzle_temp_opt = config.option<ConfigOptionInts>("nozzle_temperature");
    const ConfigOptionInts* opt_bed_temp_ints = config.option<ConfigOptionInts>(get_bed_temp_key(plate_type));
    const ConfigOptionFloats* speed_opt = config.option<ConfigOptionFloats>("filament_max_volumetric_speed");
    if (nozzle_temp_opt && speed_opt && opt_bed_temp_ints) {
        nozzle_temp = nozzle_temp_opt->get_at(0);
        max_volumetric_speed = speed_opt->get_at(0);
        bed_temp = opt_bed_temp_ints->get_at(0);
        if (bed_temp >= 0 && nozzle_temp >= 0 && max_volumetric_speed >= 0) {
            return true;
        }
    }
    return false;
}

void PressureAdvanceWizard::on_cali_start()
{
    if (!curr_obj) {
        MessageDialog msg_dlg(nullptr, _L("No Printer Connected!"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    if (curr_obj->is_connecting() || !curr_obj->is_connected()) {
        MessageDialog msg_dlg(nullptr, _L("Printer is not connected yet."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    //clean PA result
    curr_obj->reset_pa_cali_result();

    float nozzle_dia = -1;
    std::string setting_id;
    BedType plate_type = BedType::btDefault;

    // save preset info to machine object
    CalibrationPresetPage* preset_page = (static_cast<CalibrationPresetPage*>(preset_step->page));
    std::map<int, Preset*> selected_filaments = preset_page->get_selected_filaments();

    preset_page->get_preset_info(nozzle_dia, plate_type);

    CalibrationWizard::cache_preset_info(curr_obj, nozzle_dia);

    if (nozzle_dia < 0 || plate_type == BedType::btDefault) {
        BOOST_LOG_TRIVIAL(error) << "CaliPreset: get preset info, nozzle and plate type error";
        return;
    }

    if (curr_obj->printer_type == "BL-P001"
        || curr_obj->printer_type == "BL-P002") {
        X1CCalibInfos calib_infos;
        for (auto& item : selected_filaments) {
            int nozzle_temp = -1;
            int bed_temp = -1;
            float max_volumetric_speed = -1;

            if (!get_preset_info(item.second->config, plate_type, nozzle_temp, bed_temp, max_volumetric_speed)) {
                BOOST_LOG_TRIVIAL(error) << "CaliPreset: get preset info error";
                continue;
            }

            X1CCalibInfos::X1CCalibInfo calib_info;
            calib_info.tray_id = item.first;
            calib_info.nozzle_diameter = nozzle_dia;
            calib_info.filament_id = item.second->filament_id;
            calib_info.setting_id = item.second->setting_id;
            calib_info.bed_temp = bed_temp;
            calib_info.nozzle_temp = nozzle_temp;
            calib_info.max_volumetric_speed = max_volumetric_speed;
            calib_infos.calib_datas.push_back(calib_info);
        }

        std::string error_message;
        wxString wx_err_string;
        if (m_cali_method == CalibrationMethod::CALI_METHOD_AUTO) {
            CalibUtils::calib_PA(calib_infos, 0, error_message);    // mode = 0 for auto
            wx_err_string = from_u8(error_message);
        } else if (m_cali_method == CalibrationMethod::CALI_METHOD_MANUAL) {
            CalibUtils::calib_PA(calib_infos, 1, error_message);    // mode = 1 for manual
            wx_err_string = from_u8(error_message);
        } else {
            assert(false);
        }
        if (!wx_err_string.empty()) {
            MessageDialog msg_dlg(nullptr, wx_err_string, wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
        }
    }
    else if (curr_obj->printer_type == "C11"
        || curr_obj->printer_type == "C12") {
        if (selected_filaments.empty()) {
            BOOST_LOG_TRIVIAL(warning) << "CaliPreset: selected filaments is empty";
            return;
        }

        int nozzle_temp = -1;
        int bed_temp = -1;
        float max_volumetric_speed = -1;
        if (!get_preset_info(selected_filaments.begin()->second->config, plate_type, nozzle_temp, bed_temp, max_volumetric_speed)) {
            BOOST_LOG_TRIVIAL(error) << "CaliPreset: get preset info error";
            return;
        }

        curr_obj->command_start_extrusion_cali(selected_filaments.begin()->first,
            nozzle_temp, bed_temp, max_volumetric_speed, setting_id);
    } else {
        assert(false);
    }
}

void PressureAdvanceWizard::on_cali_save()
{
    if (curr_obj) {
        if (curr_obj->printer_type == "BL-P001"
            || curr_obj->printer_type == "BL-P002") {
            if (m_cali_method == CalibrationMethod::CALI_METHOD_AUTO) {
                std::vector<PACalibResult> new_pa_cali_results;
                auto save_page = static_cast<CalibrationPASavePage*>(save_step->page);
                if (!save_page->get_auto_result(new_pa_cali_results)) {
                    return;
                }
                if (save_page->is_all_failed()) {
                    MessageDialog msg_dlg(nullptr, _L("The failed test result has been droped."), wxEmptyString, wxICON_WARNING | wxOK);
                    msg_dlg.ShowModal();
                    show_step(start_step);
                    return;
                }

                CalibUtils::set_PA_calib_result(new_pa_cali_results);
            }
            else if (m_cali_method == CalibrationMethod::CALI_METHOD_MANUAL) {
                PACalibResult new_pa_cali_result;
                auto save_page = static_cast<CalibrationPASavePage*>(save_step->page);
                if (!save_page->get_manual_result(new_pa_cali_result)) {
                    return;
                }
                CalibUtils::set_PA_calib_result({ new_pa_cali_result });
            }
        }
        else if (curr_obj->printer_type == "C11"
            || curr_obj->printer_type == "C12") {
            auto save_page = static_cast<CalibrationPASavePage*>(save_step->page);
            float new_k_value = 0.0f;
            float new_n_value = 0.0f;
            if (!save_page->get_p1p_result(&new_k_value, &new_n_value)) {
                return;
            }

            float nozzle_dia = 0.4;
            BedType plate_type = BedType::btDefault;
            CalibrationPresetPage* preset_page = (static_cast<CalibrationPresetPage*>(preset_step->page));
            preset_page->get_preset_info(nozzle_dia, plate_type);
            std::map<int, Preset*> selected_filaments = preset_page->get_selected_filaments();
            int tray_id = selected_filaments.begin()->first;
            std::string setting_id = selected_filaments.begin()->second->setting_id;

            int nozzle_temp = -1;
            int bed_temp = -1;
            float max_volumetric_speed = -1;
            if (!get_preset_info(selected_filaments.begin()->second->config, plate_type, nozzle_temp, bed_temp, max_volumetric_speed)) {
                BOOST_LOG_TRIVIAL(error) << "CaliPreset: get preset info error";
                return;
            }

            curr_obj->command_extrusion_cali_set(tray_id, setting_id, "", new_k_value, new_n_value, bed_temp, nozzle_temp, max_volumetric_speed);
        }
        else {
            assert(false);
        }
        MessageDialog msg_dlg(nullptr, _L("Dynamic Pressure Control calibration result has been saved to the printer"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
    }
    show_step(start_step);
}

FlowRateWizard::FlowRateWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, CalibMode::Calib_Flow_Rate, id, pos, size, style)
{
    create_pages();
}

void FlowRateWizard::create_pages()
{
    start_step = new CalibrationWizardPageStep(new CalibrationFlowRateStartPage(m_scrolledWindow));
    preset_step = new CalibrationWizardPageStep(new CalibrationPresetPage(m_scrolledWindow, m_mode, false));

    // manual
    cali_coarse_step = new CalibrationWizardPageStep(new CalibrationCaliPage(m_scrolledWindow, m_mode, CaliPageType::CALI_PAGE_CALI));
    coarse_save_step = new CalibrationWizardPageStep(new CalibrationFlowCoarseSavePage(m_scrolledWindow));
    cali_fine_step = new CalibrationWizardPageStep(new CalibrationCaliPage(m_scrolledWindow, m_mode, CaliPageType::CALI_PAGE_FINE_CALI));
    fine_save_step = new CalibrationWizardPageStep(new CalibrationFlowFineSavePage(m_scrolledWindow));
    
    // auto
    cali_step = new CalibrationWizardPageStep(new CalibrationCaliPage(m_scrolledWindow, m_mode));
    save_step = new CalibrationWizardPageStep(new CalibrationFlowX1SavePage(m_scrolledWindow));

    m_all_pages_sizer->Add(start_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(preset_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(cali_coarse_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(coarse_save_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(cali_fine_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(fine_save_step->page, 1, wxEXPAND | wxALL, FromDIP(25));

    m_all_pages_sizer->Add(cali_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(save_step->page, 1, wxEXPAND | wxALL, FromDIP(25));

    m_page_steps.push_back(start_step);
    m_page_steps.push_back(preset_step);
    m_page_steps.push_back(cali_coarse_step);
    m_page_steps.push_back(coarse_save_step);
    m_page_steps.push_back(cali_fine_step);
    m_page_steps.push_back(fine_save_step);

    //m_page_steps.push_back(cali_step);
    //m_page_steps.push_back(save_step);

    for (int i = 0; i < m_page_steps.size() - 1; i++) {
        m_page_steps[i]->chain(m_page_steps[i + 1]);
    }

    // hide all pages
    cali_step->page->Hide();
    save_step->page->Hide();
    for (int i = 0; i < m_page_steps.size(); i++) {
        m_page_steps[i]->page->Hide();
        m_page_steps[i]->page->Bind(EVT_CALI_ACTION, &FlowRateWizard::on_cali_action, this);
    }


    cali_step->page->Bind(EVT_CALI_ACTION, &FlowRateWizard::on_cali_action, this);
    save_step->page->Bind(EVT_CALI_ACTION, &FlowRateWizard::on_cali_action, this);

    if (!m_page_steps.empty())
        show_step(m_page_steps.front());

    set_cali_method(CalibrationMethod::CALI_METHOD_MANUAL);
}

void FlowRateWizard::on_cali_action(wxCommandEvent& evt)
{
    CaliPageActionType action = static_cast<CaliPageActionType>(evt.GetInt());
    if (action == CaliPageActionType::CALI_ACTION_MANAGE_RESULT) {
        ;
    }
    else if (action == CaliPageActionType::CALI_ACTION_MANUAL_CALI) {
        preset_step->page->set_cali_filament_mode(CalibrationFilamentMode::CALI_MODEL_SINGLE);
        this->set_cali_method(CalibrationMethod::CALI_METHOD_MANUAL);
        preset_step->page->on_device_connected(curr_obj);
        show_step(m_curr_step->next);
    }
    else if (action == CaliPageActionType::CALI_ACTION_AUTO_CALI) {
        CalibrationFilamentMode fila_mode = get_cali_filament_mode(curr_obj, m_mode);
        preset_step->page->set_cali_filament_mode(fila_mode);
        this->set_cali_method(CalibrationMethod::CALI_METHOD_AUTO);
        preset_step->page->on_device_connected(curr_obj);
        show_step(m_curr_step->next);
    }
    else if (action == CaliPageActionType::CALI_ACTION_NEXT) {
        show_step(m_curr_step->next);
    }
    else if (action == CaliPageActionType::CALI_ACTION_PREV) {
        show_step(m_curr_step->prev);
    }
    else if (action == CaliPageActionType::CALI_ACTION_CALI) {
        if (m_cali_method == CalibrationMethod::CALI_METHOD_AUTO) {
            on_cali_start();
            show_step(m_curr_step->next);
        } 
        else if (m_cali_method == CalibrationMethod::CALI_METHOD_MANUAL) {
            CaliPresetStage stage = CaliPresetStage::CALI_MANULA_STAGE_NONE;
            float cali_value = 0.0f;
            static_cast<CalibrationPresetPage*>(preset_step->page)->get_cali_stage(stage, cali_value);
            on_cali_start(stage, cali_value);
            if (stage == CaliPresetStage::CALI_MANUAL_STAGE_2) {
                // set next step page
                m_curr_step->chain(cali_fine_step);
            }
            // automatically jump to next step when print job is sending finished.
        } 
        else {
            on_cali_start();
            show_step(m_curr_step->next);
        }
    }
    else if (action == CaliPageActionType::CALI_ACTION_FLOW_CALI_STAGE_2) {
        if (m_cali_method == CalibrationMethod::CALI_METHOD_MANUAL) {
            float new_flow_ratio = 0.0f;
            wxString temp_name = ""; // unused
            auto coarse_save_page = static_cast<CalibrationFlowCoarseSavePage*>(m_curr_step->page);
            if (!coarse_save_page->get_result(&new_flow_ratio, &temp_name)) {
                return;
            }
            on_cali_start(CaliPresetStage::CALI_MANUAL_STAGE_2, new_flow_ratio);
            // automatically jump to next step when print job is sending finished.
        }
    }
    else if (action == CaliPageActionType::CALI_ACTION_FLOW_SAVE) {
        on_cali_save();
    }
    else if (action == CaliPageActionType::CALI_ACTION_FLOW_COARSE_SAVE) {
        auto coarse_save_page = static_cast<CalibrationFlowCoarseSavePage*>(coarse_save_step->page);
        if (coarse_save_page->is_skip_fine_calibration()) {
            on_cali_save();
        }
    }
    else if (action == CaliPageActionType::CALI_ACTION_FLOW_FINE_SAVE) {
        on_cali_save();
    }
    else if (action == CaliPageActionType::CALI_ACTION_GO_HOME) {
        on_cali_go_home();
    }
}

void FlowRateWizard::on_cali_start(CaliPresetStage stage, float cali_value)
{
    if (!curr_obj) return;

    //clean flow rate result
    curr_obj->flow_ratio_results.clear();

    float nozzle_dia = 0.4;
    std::string setting_id;
    BedType plate_type = BedType::btDefault;

    CalibrationPresetPage* preset_page = (static_cast<CalibrationPresetPage*>(preset_step->page));

    preset_page->get_preset_info(nozzle_dia, plate_type);

    std::map<int, Preset*> selected_filaments = preset_page->get_selected_filaments();

    CalibrationWizard::cache_preset_info(curr_obj, nozzle_dia);

    if (m_cali_method == CalibrationMethod::CALI_METHOD_AUTO) {
        X1CCalibInfos calib_infos;
        for (auto& item : selected_filaments) {
            int nozzle_temp = -1;
            int bed_temp = -1;
            float max_volumetric_speed = -1;

            if (!get_preset_info(item.second->config, plate_type, nozzle_temp, bed_temp, max_volumetric_speed)) {
                BOOST_LOG_TRIVIAL(error) << "CaliPreset: get preset info error";
            }

            X1CCalibInfos::X1CCalibInfo calib_info;
            calib_info.tray_id          = item.first;
            calib_info.nozzle_diameter  = nozzle_dia;
            calib_info.filament_id      = item.second->filament_id;
            calib_info.setting_id       = item.second->setting_id;
            calib_info.bed_temp         = bed_temp;
            calib_info.nozzle_temp      = nozzle_temp;
            calib_info.max_volumetric_speed = max_volumetric_speed;
            calib_infos.calib_datas.push_back(calib_info);
        }

        wxString wx_err_string;
        std::string error_message;
        CalibUtils::calib_flowrate_X1C(calib_infos, error_message);
        wx_err_string = from_u8(error_message);
        if (!wx_err_string.empty()) {
            MessageDialog msg_dlg(nullptr, wx_err_string, wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
        }
    }
    else if (m_cali_method == CalibrationMethod::CALI_METHOD_MANUAL) {
        CalibInfo calib_info;
        calib_info.dev_id            = curr_obj->dev_id;
        Preset* temp_filament_preset = nullptr;
        int cali_stage = -1;
        wxString wx_err_string;

        if (!selected_filaments.empty()) {
            calib_info.select_ams     = "[" + std::to_string(selected_filaments.begin()->first) + "]";
            Preset* preset = selected_filaments.begin()->second;
            temp_filament_preset = new Preset(preset->type, preset->name + "_temp");
            temp_filament_preset->config = preset->config;

            calib_info.bed_type = plate_type;
            calib_info.process_bar = preset_page->get_sending_progress_bar();
            calib_info.printer_prest = preset_page->get_printer_preset(curr_obj, nozzle_dia);
            calib_info.print_prest = preset_page->get_print_preset();
            calib_info.params.mode = CalibMode::Calib_Flow_Rate;

            if (stage == CaliPresetStage::CALI_MANUAL_STAGE_1) {
                cali_stage = 1;
            }
            else if (stage == CaliPresetStage::CALI_MANUAL_STAGE_2) {
                cali_stage = 2;
                temp_filament_preset->config.set_key_value("filament_flow_ratio", new ConfigOptionFloats{ cali_value });
            }
            calib_info.filament_prest = temp_filament_preset;

            if (cali_stage > 0) {
                std::string error_message;
                CalibUtils::calib_flowrate(cali_stage, calib_info, error_message);
                wx_err_string = from_u8(error_message);
            }
            else {
                wx_err_string = _L("Internal Error") + wxString(": Invalid calibration stage");
            }
        } else {
            wx_err_string = _L("Please select at least one filament for calibration");
        }

        if (!wx_err_string.empty()) {
            MessageDialog msg_dlg(nullptr, wx_err_string, wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
        }
        preset_page->on_cali_start_job();
        if (temp_filament_preset)
            delete temp_filament_preset;
    } else {
        assert(false);
    }
}

void FlowRateWizard::on_cali_save()
{
    if (curr_obj) {
        if (m_cali_method == CalibrationMethod::CALI_METHOD_AUTO) {
            std::vector<std::pair<wxString, float>> new_results;
            auto save_page = static_cast<CalibrationFlowX1SavePage*>(save_step->page);
            if (!save_page->get_result(new_results)) {
                return;
            }
            if (save_page->is_all_failed()) {
                MessageDialog msg_dlg(nullptr, _L("The failed test result has been droped."), wxEmptyString, wxICON_WARNING | wxOK);
                msg_dlg.ShowModal();
                show_step(start_step);
                return;
            }

            std::string old_preset_name;
            CalibrationPresetPage* preset_page = (static_cast<CalibrationPresetPage*>(preset_step->page));
            std::map<int, Preset*> selected_filaments = preset_page->get_selected_filaments();
            old_preset_name = selected_filaments.begin()->second->name;

            for (int i = 0; i < new_results.size(); i++) {
                std::map<std::string, ConfigOption*> key_value_map;
                key_value_map.insert(std::make_pair("filament_flow_ratio", new ConfigOptionFloats{ new_results[i].second }));
                std::string message;
                if (!save_preset(old_preset_name, new_results[i].first.ToStdString(), key_value_map, message)) {
                    MessageDialog error_msg_dlg(nullptr, message, wxEmptyString, wxICON_WARNING | wxOK);
                    error_msg_dlg.ShowModal();
                    return;
                }
            }

            MessageDialog msg_dlg(nullptr, _L("Flow rate calibration result has been saved to preset"), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
        }
        else if (m_cali_method == CalibrationMethod::CALI_METHOD_MANUAL) {
            float new_flow_ratio = 0.0f;
            wxString new_preset_name = "";
            if(m_curr_step->page->get_page_type() == CaliPageType::CALI_PAGE_COARSE_SAVE)
            {
                auto coarse_save_page = static_cast<CalibrationFlowCoarseSavePage*>(m_curr_step->page);
                if (!coarse_save_page->get_result(&new_flow_ratio, &new_preset_name)) {
                    return;
                }
            }
            else if (m_curr_step->page->get_page_type() == CaliPageType::CALI_PAGE_FINE_SAVE)
            {
                auto fine_save_page = static_cast<CalibrationFlowFineSavePage*>(m_curr_step->page);
                if (!fine_save_page->get_result(&new_flow_ratio, &new_preset_name)) {
                    return;
                }
            }
            else {
                return;
            }
            std::string old_preset_name;
            CalibrationPresetPage* preset_page = (static_cast<CalibrationPresetPage*>(preset_step->page));
            std::map<int, Preset*> selected_filaments = preset_page->get_selected_filaments();
            old_preset_name = selected_filaments.begin()->second->name;

            std::map<std::string, ConfigOption*> key_value_map;
            key_value_map.insert(std::make_pair("filament_flow_ratio", new ConfigOptionFloats{ new_flow_ratio }));

            std::string message;
            if (!save_preset(old_preset_name, new_preset_name.ToStdString(), key_value_map, message)) {
                MessageDialog error_msg_dlg(nullptr, message, wxEmptyString, wxICON_WARNING | wxOK);
                error_msg_dlg.ShowModal();
                return;
            }

            MessageDialog msg_dlg(nullptr, _L("Flow rate calibration result has been saved to preset"), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
        }
        else {
            assert(false);
        }
    }
    show_step(start_step);
}

void FlowRateWizard::update(MachineObject* obj)
{
    CalibrationWizard::update(obj);
}

void FlowRateWizard::on_device_connected(MachineObject* obj)
{
    CalibrationWizard::on_device_connected(obj);

    CalibrationMethod method;
    int cali_stage = 0;
    CalibMode obj_cali_mode = get_obj_calibration_mode(obj, method, cali_stage);

    // show cali step when obj is in pa calibration
    if (obj) {
        this->set_cali_method(method);
        if (obj_cali_mode == m_mode) {
            if (obj->is_in_printing() || obj->is_printing_finished()) {
                if (method == CalibrationMethod::CALI_METHOD_MANUAL) {
                    if (cali_stage == 1) {
                        if (m_curr_step != cali_coarse_step)
                            show_step(cali_coarse_step);
                    }
                    else if (cali_stage == 2) {
                        if (m_curr_step != cali_fine_step) {
                            show_step(cali_fine_step);
                        }
                    }
                    else
                        show_step(cali_coarse_step);
                }
                else if (method == CalibrationMethod::CALI_METHOD_AUTO) {
                    if (m_curr_step != cali_step)
                        show_step(cali_step);
                }
            }
        }
    }
}

void FlowRateWizard::set_cali_method(CalibrationMethod method)
{
    m_cali_method = method;
    if (method == CalibrationMethod::CALI_METHOD_AUTO) {
        m_page_steps.clear();
        m_page_steps.push_back(start_step);
        m_page_steps.push_back(preset_step);
        m_page_steps.push_back(cali_step);
        m_page_steps.push_back(save_step);

        for (int i = 0; i < m_page_steps.size() - 1; i++) {
            m_page_steps[i]->chain(m_page_steps[i + 1]);
        }
    }
    else if (method == CalibrationMethod::CALI_METHOD_MANUAL) {
        m_page_steps.clear();
        m_page_steps.push_back(start_step);
        m_page_steps.push_back(preset_step);
        m_page_steps.push_back(cali_coarse_step);
        m_page_steps.push_back(coarse_save_step);
        m_page_steps.push_back(cali_fine_step);
        m_page_steps.push_back(fine_save_step);

        for (int i = 0; i < m_page_steps.size() - 1; i++) {
            m_page_steps[i]->chain(m_page_steps[i + 1]);
        }
    }
    else {
        assert(false);
    }
    CalibrationWizard::set_cali_method(method);
}

void FlowRateWizard::on_cali_job_finished(wxString evt_data)
{
    int       cali_stage = 0;
    CalibMode obj_cali_mode = CalibUtils::get_calib_mode_by_name(evt_data.ToStdString(), cali_stage);

    if (obj_cali_mode == m_mode) {
        if (cali_stage == 1) {
            if (m_curr_step != cali_coarse_step)
                show_step(cali_coarse_step);
        }
        else if (cali_stage == 2) {
            if (m_curr_step != cali_fine_step) {
                show_step(cali_fine_step);
            }
        }
        else
            show_step(cali_coarse_step);
    }
    // change ui, hide
    static_cast<CalibrationPresetPage*>(preset_step->page)->on_cali_finished_job();
}


MaxVolumetricSpeedWizard::MaxVolumetricSpeedWizard(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationWizard(parent, CalibMode::Calib_Vol_speed_Tower, id, pos, size, style)
{
    create_pages();
}

void MaxVolumetricSpeedWizard::create_pages() 
{
    start_step  = new CalibrationWizardPageStep(new CalibrationMaxVolumetricSpeedStartPage(m_scrolledWindow));
    preset_step = new CalibrationWizardPageStep(new MaxVolumetricSpeedPresetPage(m_scrolledWindow, m_mode, true));

    // manual
    cali_step = new CalibrationWizardPageStep(new CalibrationCaliPage(m_scrolledWindow, m_mode));
    save_step = new CalibrationWizardPageStep(new CalibrationMaxVolumetricSpeedSavePage(m_scrolledWindow));

    m_all_pages_sizer->Add(start_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(preset_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(cali_step->page, 1, wxEXPAND | wxALL, FromDIP(25));
    m_all_pages_sizer->Add(save_step->page, 1, wxEXPAND | wxALL, FromDIP(25));

    m_page_steps.push_back(start_step);
    m_page_steps.push_back(preset_step);
    m_page_steps.push_back(cali_step);
    m_page_steps.push_back(save_step);

    for (int i = 0; i < m_page_steps.size() - 1; i++) {
        m_page_steps[i]->chain(m_page_steps[i + 1]);
    }

    for (int i = 0; i < m_page_steps.size(); i++) {
        m_page_steps[i]->page->Hide();
        m_page_steps[i]->page->Bind(EVT_CALI_ACTION, &MaxVolumetricSpeedWizard::on_cali_action, this);
    }

    for (auto page_step : m_page_steps) {
        page_step->page->Hide();
    }

    if (!m_page_steps.empty())
        show_step(m_page_steps.front());
    return;
}

void MaxVolumetricSpeedWizard::on_cali_action(wxCommandEvent& evt)
{
    CaliPageActionType action = static_cast<CaliPageActionType>(evt.GetInt());

    if (action == CaliPageActionType::CALI_ACTION_START) {
        show_step(m_curr_step->next);
    }
    else if (action == CaliPageActionType::CALI_ACTION_PREV) {
        show_step(m_curr_step->prev);
    }
    else if (action == CaliPageActionType::CALI_ACTION_CALI) {
        on_cali_start();
    }
    else if (action == CaliPageActionType::CALI_ACTION_NEXT) {
        show_step(m_curr_step->next);
    }
    else if (action == CaliPageActionType::CALI_ACTION_COMMON_SAVE) {
        on_cali_save();
    }
    else if (action == CaliPageActionType::CALI_ACTION_GO_HOME) {
        on_cali_go_home();
    }
}

void MaxVolumetricSpeedWizard::on_cali_start()
{
    float       nozzle_dia = 0.4;
    std::string setting_id;
    BedType     plate_type = BedType::btDefault;

    CalibrationPresetPage *preset_page = (static_cast<CalibrationPresetPage *>(preset_step->page));

    preset_page->get_preset_info(nozzle_dia, plate_type);

    CalibrationWizard::cache_preset_info(curr_obj, nozzle_dia);

    wxArrayString values = preset_page->get_custom_range_values();
    Calib_Params  params;
    if (values.size() != 3) {
        MessageDialog msg_dlg(nullptr, _L("The input value size must be 3."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }
    else {
        values[0].ToDouble(&params.start);
        values[1].ToDouble(&params.end);
        values[2].ToDouble(&params.step);
    }
    params.mode = m_mode;

    std::map<int, Preset *> selected_filaments = preset_page->get_selected_filaments();

    CalibInfo calib_info;
    calib_info.params = params;
    calib_info.dev_id = curr_obj->dev_id;
    if (!selected_filaments.empty()) {
        calib_info.select_ams     = "[" + std::to_string(selected_filaments.begin()->first) + "]";
        calib_info.filament_prest = selected_filaments.begin()->second;
    }

    calib_info.bed_type      = plate_type;
    calib_info.process_bar   = preset_page->get_sending_progress_bar();
    calib_info.printer_prest = preset_page->get_printer_preset(curr_obj, nozzle_dia);
    calib_info.print_prest   = preset_page->get_print_preset();

    wxString wx_err_string;
    std::string error_message;
    CalibUtils::calib_max_vol_speed(calib_info, error_message);
    wx_err_string = from_u8(error_message);
    if (!wx_err_string.empty()) {
        MessageDialog msg_dlg(nullptr, wx_err_string, wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
    }

    preset_page->on_cali_start_job();
}

void MaxVolumetricSpeedWizard::on_cali_save()
{
    std::string old_preset_name;
    std::string new_preset_name;

    CalibrationPresetPage *preset_page = (static_cast<CalibrationPresetPage *>(preset_step->page));
    std::map<int, Preset *> selected_filaments = preset_page->get_selected_filaments();
    if (!selected_filaments.empty()) {
        old_preset_name = selected_filaments.begin()->second->name;
    }

    double value = 0;
    CalibrationMaxVolumetricSpeedSavePage *save_page = (static_cast<CalibrationMaxVolumetricSpeedSavePage *>(save_step->page));
    if (!save_page->get_save_result(value, new_preset_name)) {
        return;
    }

    std::map<std::string, ConfigOption *> key_value_map;
    key_value_map.insert(std::make_pair("filament_max_volumetric_speed", new ConfigOptionFloats{ value }));

    std::string message;
    if (!save_preset(old_preset_name, new_preset_name, key_value_map, message)) {
        MessageDialog error_msg_dlg(nullptr, message, wxEmptyString, wxICON_WARNING | wxOK);
        error_msg_dlg.ShowModal();
        return;
    }

    MessageDialog msg_dlg(nullptr, _L("Max volumetric speed calibration result has been saved to preset"), wxEmptyString, wxICON_WARNING | wxOK);
    msg_dlg.ShowModal();
    show_step(start_step);
}

void MaxVolumetricSpeedWizard::on_cali_job_finished(wxString evt_data)
{
    int       cali_stage = 0;
    CalibMode obj_cali_mode = CalibUtils::get_calib_mode_by_name(evt_data.ToStdString(), cali_stage);

    if (obj_cali_mode == m_mode) {
        if (m_curr_step != cali_step) {
            show_step(cali_step);
        }
    }
    // change ui, hide
    static_cast<CalibrationPresetPage*>(preset_step->page)->on_cali_finished_job();
}

void MaxVolumetricSpeedWizard::on_device_connected(MachineObject *obj)
{
    CalibrationWizard::on_device_connected(obj);

    CalibrationMethod method;
    int               cali_stage    = 0;
    CalibMode         obj_cali_mode = get_obj_calibration_mode(obj, method, cali_stage);

    if (obj) {
        this->set_cali_method(method);
        if (obj_cali_mode == m_mode) {
            if (obj->is_in_printing() || obj->is_printing_finished()) {
                if (m_curr_step != cali_step) {
                    show_step(cali_step);
                }
            }
        }
    }
}
}}
