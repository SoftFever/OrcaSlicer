#include "CalibrationWizardPage.hpp"
#include "I18N.hpp"
#include "Widgets/Label.hpp"
#include "MsgDialog.hpp"

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_CALI_ACTION, wxCommandEvent);
wxDEFINE_EVENT(EVT_CALI_TRAY_CHANGED, wxCommandEvent);


CalibrationStyle get_cali_style(MachineObject* obj)
{
    if (!obj) return CalibrationStyle::CALI_STYLE_DEFAULT;

    if (obj->printer_type == "BL-P001" || obj->printer_type == "BL-P002")
        return CalibrationStyle::CALI_STYLE_X1;
    else if (obj->printer_type == "C11" || obj->printer_type == "C12")
        return CalibrationStyle::CALI_STYLE_P1P;

    return CalibrationStyle::CALI_STYLE_DEFAULT;
}

wxString get_cali_mode_caption_string(CalibMode mode)
{
    if (mode == CalibMode::Calib_PA_Line)
        return _L("Dynamic Pressure Control Calibration");
    if (mode == CalibMode::Calib_Flow_Rate)
        return _L("Flow Rate Calibration");
    if (mode == CalibMode::Calib_Vol_speed_Tower)
        return _L("Max Volumetric Speed Calibration");
    return "no cali_mode_caption";
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

CalibrationFilamentMode get_cali_filament_mode(MachineObject* obj, CalibMode mode)
{
    // default 
    if (!obj) return CalibrationFilamentMode::CALI_MODEL_SINGLE;


    if (mode == CalibMode::Calib_PA_Line) {
        if (obj->printer_type == "BL-P001" || obj->printer_type == "BL-P002")
            return CalibrationFilamentMode::CALI_MODEL_MULITI;
        else if (obj->printer_type == "C11" || obj->printer_type == "C12")
            return CalibrationFilamentMode::CALI_MODEL_SINGLE;
    }
    else if (mode == CalibMode::Calib_Flow_Rate) {
        if (obj->printer_type == "BL-P001" || obj->printer_type == "BL-P002")
            return CalibrationFilamentMode::CALI_MODEL_SINGLE;
        else if (obj->printer_type == "C11" || obj->printer_type == "C12")
            return CalibrationFilamentMode::CALI_MODEL_SINGLE;
    }

    return CalibrationFilamentMode::CALI_MODEL_SINGLE;
}

CalibMode get_obj_calibration_mode(const MachineObject* obj)
{
    CalibrationMethod method;
    int cali_stage;
    return get_obj_calibration_mode(obj, method, cali_stage);
}

CalibMode get_obj_calibration_mode(const MachineObject* obj, int& cali_stage)
{
    CalibrationMethod method;
    return get_obj_calibration_mode(obj, method, cali_stage);
}

CalibMode get_obj_calibration_mode(const MachineObject* obj, CalibrationMethod& method, int& cali_stage)
{
    method = CalibrationMethod::CALI_METHOD_MANUAL;

    if (!obj) return CalibMode::Calib_None;

    if (boost::contains(obj->m_gcode_file, "auto_filament_cali")) {
        method = CalibrationMethod::CALI_METHOD_AUTO;
        return CalibMode::Calib_PA_Line;
    }
    if (boost::contains(obj->m_gcode_file, "user_cali_manual_pa")) {
        method = CalibrationMethod::CALI_METHOD_MANUAL;
        return CalibMode::Calib_PA_Line;
    }
    if (boost::contains(obj->m_gcode_file, "extrusion_cali")) {
        method == CalibrationMethod::CALI_METHOD_MANUAL;
        return CalibMode::Calib_PA_Line;
    }

    if (boost::contains(obj->m_gcode_file, "abs_flowcalib_cali")) {
        method = CalibrationMethod::CALI_METHOD_AUTO;
        return CalibMode::Calib_Flow_Rate;
    }

    if (obj->printer_type == "C11" || obj->printer_type == "C12") {
        if (boost::contains(obj->subtask_name, "auto_filament_cali")) {
            method = CalibrationMethod::CALI_METHOD_AUTO;
            return CalibMode::Calib_PA_Line;
        }
        if (boost::contains(obj->subtask_name, "user_cali_manual_pa")) {
            method = CalibrationMethod::CALI_METHOD_MANUAL;
            return CalibMode::Calib_PA_Line;
        }
        if (boost::contains(obj->subtask_name, "extrusion_cali")) {
            method == CalibrationMethod::CALI_METHOD_MANUAL;
            return CalibMode::Calib_PA_Line;
        }

        if (boost::contains(obj->subtask_name, "abs_flowcalib_cali")) {
            method = CalibrationMethod::CALI_METHOD_AUTO;
            return CalibMode::Calib_Flow_Rate;
        }
    }

    CalibMode cali_mode = CalibUtils::get_calib_mode_by_name(obj->subtask_name, cali_stage);
    if (cali_mode != CalibMode::Calib_None) {
        method = CalibrationMethod::CALI_METHOD_MANUAL;
    }
    return cali_mode;
}


CaliPageButton::CaliPageButton(wxWindow* parent, CaliPageActionType type, wxString text)
    : m_action_type(type),
    Button(parent, text)
{
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Normal));

    StateColor btn_bd_green(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Enabled));

    StateColor btn_bd_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    StateColor btn_text_green(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Enabled));

    StateColor btn_text_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
        std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    switch (m_action_type)
    {
    case CaliPageActionType::CALI_ACTION_MANAGE_RESULT:
        this->SetLabel(_L("Manage Result"));
        break;
    case CaliPageActionType::CALI_ACTION_MANUAL_CALI:
        this->SetLabel(_L("Maual Calibration"));
        this->SetToolTip(_L("Result can be read by human eyes."));
        break;
    case CaliPageActionType::CALI_ACTION_AUTO_CALI:
        this->SetLabel(_L("Auto-Calibration"));
        this->SetToolTip(_L("We would use Lidar to read the calibration result"));
        break;
    case CaliPageActionType::CALI_ACTION_START:
        this->SetLabel(_L("Start Calibration"));
        break;
    case CaliPageActionType::CALI_ACTION_PREV:
        this->SetLabel(_L("Prev"));
        break;
    case CaliPageActionType::CALI_ACTION_RECALI:
        this->SetLabel(_L("Recalibration"));
        break;
    case CaliPageActionType::CALI_ACTION_NEXT:
        this->SetLabel(_L("Next"));
        break;
    case CaliPageActionType::CALI_ACTION_CALI_NEXT:
        this->SetLabel(_L("Next"));
        break;
    case CaliPageActionType::CALI_ACTION_CALI:
        this->SetLabel(_L("Calibrate"));
        break;
    case CaliPageActionType::CALI_ACTION_FLOW_CALI_STAGE_2:
        this->SetLabel(_L("Calibrate"));
        break;
    case CaliPageActionType::CALI_ACTION_PA_SAVE:
        this->SetLabel(_L("Finish"));
        break;
    case CaliPageActionType::CALI_ACTION_FLOW_SAVE:
        this->SetLabel(_L("Finish"));
        break;
    case CaliPageActionType::CALI_ACTION_FLOW_COARSE_SAVE:
        this->SetLabel(_L("Finish"));
        break;
    case CaliPageActionType::CALI_ACTION_FLOW_FINE_SAVE:
        this->SetLabel(_L("Finish"));
        break;
    case CaliPageActionType::CALI_ACTION_COMMON_SAVE:
        this->SetLabel(_L("Finish"));
        break;
    default:
        this->SetLabel("Unknown");
        break;
    }

    switch (m_action_type)
    {
    case CaliPageActionType::CALI_ACTION_PREV:
    case CaliPageActionType::CALI_ACTION_RECALI:
        SetBackgroundColor(btn_bg_white);
        SetBorderColor(btn_bd_white);
        SetTextColor(btn_text_white);
        break;
    case CaliPageActionType::CALI_ACTION_START:
    case CaliPageActionType::CALI_ACTION_NEXT:
    case CaliPageActionType::CALI_ACTION_CALI:
    case CaliPageActionType::CALI_ACTION_CALI_NEXT:
    case CaliPageActionType::CALI_ACTION_FLOW_CALI_STAGE_2:
    case CaliPageActionType::CALI_ACTION_PA_SAVE:
    case CaliPageActionType::CALI_ACTION_FLOW_SAVE:
    case CaliPageActionType::CALI_ACTION_FLOW_COARSE_SAVE:
    case CaliPageActionType::CALI_ACTION_FLOW_FINE_SAVE:
    case CaliPageActionType::CALI_ACTION_COMMON_SAVE:
        SetBackgroundColor(btn_bg_green);
        SetBorderColor(btn_bd_green);
        SetTextColor(btn_text_green);
        break;
    default:
        break;
    }

    SetBackgroundColour(*wxWHITE);
    SetFont(Label::Body_13);
    SetMinSize(wxSize(-1, FromDIP(24)));
    SetCornerRadius(FromDIP(12));
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

void FilamentComboBox::set_select_mode(CalibrationFilamentMode mode)
{
    m_mode = mode;
    if (m_checkBox)
        m_checkBox->Show(m_mode == CalibrationFilamentMode::CALI_MODEL_MULITI);
    if (m_radioBox)
        m_radioBox->Show(m_mode == CalibrationFilamentMode::CALI_MODEL_SINGLE);

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
        if (!m_comboBox->is_compatible_with_printer()) {
            SetValue(false);
        }

        if (m_radioBox)
            m_radioBox->Enable(m_comboBox->is_compatible_with_printer());
            
        if (m_checkBox)
            m_checkBox->Enable(m_comboBox->is_compatible_with_printer());

    }

    // check compatibility
    wxCommandEvent event(EVT_CALI_TRAY_CHANGED);
    event.SetEventObject(GetParent());
    wxPostEvent(GetParent(), event);
}

void FilamentComboBox::update_from_preset() { m_comboBox->update(); }

bool FilamentComboBox::Show(bool show)
{
    return wxPanel::Show(show);
}

bool FilamentComboBox::Enable(bool enable) {
    if (!enable)
        SetValue(false);

    if (m_radioBox)
        m_radioBox->Enable(enable);
    if (m_checkBox)
        m_checkBox->Enable(enable);
    return wxPanel::Enable(enable);
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
}



CaliPageCaption::CaliPageCaption(wxWindow* parent, CalibMode cali_mode,
    wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    init_bitmaps();

    auto top_sizer = new wxBoxSizer(wxVERTICAL);
    auto caption_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_prev_btn = new ScalableButton(this, wxID_ANY, "cali_page_caption_prev",
        wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 30);
    m_prev_btn->SetBackgroundColour(*wxWHITE);
    caption_sizer->Add(m_prev_btn, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(10));

    wxString title = get_cali_mode_caption_string(cali_mode);
    wxStaticText* title_text = new wxStaticText(this, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, 0);
    title_text->Wrap(-1);
    title_text->SetFont(Label::Head_16);
    caption_sizer->Add(title_text, 0, wxALIGN_CENTER | wxRIGHT, FromDIP(10));

    m_help_btn = new ScalableButton(this, wxID_ANY, "cali_page_caption_help",
        wxEmptyString, wxDefaultSize, wxDefaultPosition, wxBU_EXACTFIT | wxNO_BORDER, true, 30);
    m_help_btn->SetBackgroundColour(*wxWHITE);
    caption_sizer->Add(m_help_btn, 0, wxALIGN_CENTER);

    top_sizer->Add(caption_sizer, 1, wxEXPAND);
    top_sizer->AddSpacer(FromDIP(35));
    this->SetSizer(top_sizer);
    top_sizer->Fit(this);

    // hover effect
    m_prev_btn->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        m_prev_btn->SetBitmap(m_prev_bmp_hover.bmp());
    });

    m_prev_btn->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {
        m_prev_btn->SetBitmap(m_prev_bmp_normal.bmp());
    });

    // hover effect
    m_help_btn->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        m_help_btn->SetBitmap(m_help_bmp_hover.bmp());
        });

    m_help_btn->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {
        m_help_btn->SetBitmap(m_help_bmp_normal.bmp());
        });

    // send event
    m_prev_btn->Bind(wxEVT_BUTTON, [this](auto& e) {
        wxCommandEvent event(EVT_CALI_ACTION);
        event.SetEventObject(m_parent);
        event.SetInt((int)(CaliPageActionType::CALI_ACTION_GO_HOME));
        wxPostEvent(m_parent, event);
        });
}

void CaliPageCaption::init_bitmaps() {
    m_prev_bmp_normal = ScalableBitmap(this, "cali_page_caption_prev", 30);
    m_prev_bmp_hover = ScalableBitmap(this, "cali_page_caption_prev_hover", 30);
    m_help_bmp_normal = ScalableBitmap(this, "cali_page_caption_help", 30);
    m_help_bmp_hover = ScalableBitmap(this, "cali_page_caption_help_hover", 30);
}

void CaliPageCaption::show_prev_btn(bool show)
{
    m_prev_btn->Show(show);
}

void CaliPageCaption::show_help_icon(bool show)
{
    m_help_btn->Show(show);
}

CaliPageStepGuide::CaliPageStepGuide(wxWindow* parent, wxArrayString steps,
    wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style),
    m_steps(steps)
{
    auto top_sizer = new wxBoxSizer(wxVERTICAL);

    m_step_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_step_sizer->AddSpacer(FromDIP(90));
    for (int i = 0; i < m_steps.size(); i++) {
        wxStaticText* step_text = new wxStaticText(this, wxID_ANY, m_steps[i]);
        step_text->SetForegroundColour(wxColour(181, 181, 181));
        m_text_steps.push_back(step_text);
        m_step_sizer->Add(step_text, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(15));
        if (i != m_steps.size() - 1) {
            auto line = new wxPanel(this, wxID_ANY, wxDefaultPosition, { FromDIP(200), 1 });
            line->SetBackgroundColour(*wxBLACK);
            m_step_sizer->Add(line, 1, wxALIGN_CENTER);
        }
    }
    m_step_sizer->AddSpacer(FromDIP(90));

    top_sizer->Add(m_step_sizer, 0, wxEXPAND);
    top_sizer->AddSpacer(FromDIP(30));
    this->SetSizer(top_sizer);
    top_sizer->Fit(this);
}

void CaliPageStepGuide::set_steps(int index)
{
    for (wxStaticText* text_step : m_text_steps) {
        text_step->SetForegroundColour(wxColour(181, 181, 181));
    }
    m_text_steps[index]->SetForegroundColour(*wxBLACK);
}

void CaliPageStepGuide::set_steps_string(wxArrayString steps)
{
    m_steps.Clear();
    m_text_steps.clear();
    m_step_sizer->Clear(true);
    m_steps = steps;
    m_step_sizer->AddSpacer(FromDIP(90));
    for (int i = 0; i < m_steps.size(); i++) {
        wxStaticText* step_text = new wxStaticText(this, wxID_ANY, m_steps[i]);
        step_text->SetForegroundColour(wxColour(181, 181, 181));
        m_text_steps.push_back(step_text);
        m_step_sizer->Add(step_text, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(15));
        if (i != m_steps.size() - 1) {
            auto line = new wxPanel(this, wxID_ANY, wxDefaultPosition, { FromDIP(200), 1 });
            line->SetBackgroundColour(*wxBLACK);
            m_step_sizer->Add(line, 1, wxALIGN_CENTER);
        }
    }
    m_step_sizer->AddSpacer(FromDIP(90));
    Layout();
}


CaliPageActionPanel::CaliPageActionPanel(wxWindow* parent,
    CalibMode cali_mode,
    CaliPageType page_type,
    wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
{
    m_parent = parent;

    wxWindow* btn_parent = this;

    if (cali_mode == CalibMode::Calib_PA_Line) {
        if (page_type == CaliPageType::CALI_PAGE_START) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_MANAGE_RESULT));
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_MANUAL_CALI));
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_AUTO_CALI));
        }
        else if (page_type == CaliPageType::CALI_PAGE_PRESET) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_CALI));
        }
        else if (page_type == CaliPageType::CALI_PAGE_CALI) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_CALI_NEXT));
        }
        else if (page_type == CaliPageType::CALI_PAGE_PA_SAVE) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_PA_SAVE));
        }
    }
    else if (cali_mode == CalibMode::Calib_Flow_Rate) {
        if (page_type == CaliPageType::CALI_PAGE_START) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_MANUAL_CALI));
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_AUTO_CALI));
        }
        else if (page_type == CaliPageType::CALI_PAGE_PRESET) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_CALI));
        }
        else if (page_type == CaliPageType::CALI_PAGE_CALI) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_NEXT));
        }
        else if (page_type == CaliPageType::CALI_PAGE_COARSE_SAVE) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_FLOW_COARSE_SAVE));
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_FLOW_CALI_STAGE_2));
        }
        else if (page_type == CaliPageType::CALI_PAGE_FINE_SAVE) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_FLOW_FINE_SAVE));
        }
        else if (page_type == CaliPageType::CALI_PAGE_FLOW_SAVE) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_FLOW_SAVE));
        }
    }
    else {
        if (page_type == CaliPageType::CALI_PAGE_START) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_START));
        }
        else if (page_type == CaliPageType::CALI_PAGE_PRESET) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_CALI));
        }
        else if (page_type == CaliPageType::CALI_PAGE_CALI) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_NEXT));
        }
        else if (page_type == CaliPageType::CALI_PAGE_COMMON_SAVE) {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_COMMON_SAVE));
        }
        else {
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_PREV));
            m_action_btns.push_back(new CaliPageButton(btn_parent, CaliPageActionType::CALI_ACTION_NEXT));
        }
        
    }

    auto top_sizer = new wxBoxSizer(wxHORIZONTAL);

    top_sizer->Add(0, 0, 1, wxEXPAND, 5);
    for (int i = 0; i < m_action_btns.size(); i++) {
        top_sizer->Add(m_action_btns[i], 0, wxALL, 5);

        m_action_btns[i]->Bind(wxEVT_BUTTON,
            [this, i](wxCommandEvent& evt) {
                wxCommandEvent event(EVT_CALI_ACTION);
                event.SetEventObject(m_parent);
                event.SetInt((int)m_action_btns[i]->get_action_type());
                wxPostEvent(m_parent, event);
            });
    }
    top_sizer->Add(0, 0, 1, wxEXPAND, 5);

    this->SetSizer(top_sizer);
    top_sizer->Fit(this);
}

void CaliPageActionPanel::bind_button(CaliPageActionType action_type, bool is_block)
{
    for (int i = 0; i < m_action_btns.size(); i++) {
        if (m_action_btns[i]->get_action_type() == action_type) {

            if (is_block) {
                m_action_btns[i]->Bind(wxEVT_BUTTON,
                    [this](wxCommandEvent& evt) {
                        MessageDialog msg(nullptr, _L("The current firmware version of the printer does not support calibration.\nPlease upgrade the printer firmware."), _L("Calibration not supported"), wxOK | wxICON_WARNING);
                        msg.ShowModal();
                    });
            }
            else {
                m_action_btns[i]->Bind(wxEVT_BUTTON,
                    [this, i](wxCommandEvent& evt) {
                        wxCommandEvent event(EVT_CALI_ACTION);
                        event.SetEventObject(m_parent);
                        event.SetInt((int)m_action_btns[i]->get_action_type());
                        wxPostEvent(m_parent, event);
                    });
            }
        }
    }

}

void CaliPageActionPanel::show_button(CaliPageActionType action_type, bool show)
{
    for (int i = 0; i < m_action_btns.size(); i++) {
        if (m_action_btns[i]->get_action_type() == action_type) {
            m_action_btns[i]->Show(show);
        }
    }
}

void CaliPageActionPanel::enable_button(CaliPageActionType action_type, bool enable)
{
    for (int i = 0; i < m_action_btns.size(); i++) {
        if (m_action_btns[i]->get_action_type() == action_type) {
            m_action_btns[i]->Enable(enable);
        }
    }
}

CalibrationWizardPage::CalibrationWizardPage(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : wxPanel(parent, id, pos, size, style)
    , m_parent(parent)
{
    SetBackgroundColour(*wxWHITE);
}


}}
