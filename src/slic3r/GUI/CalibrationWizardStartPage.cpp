#include "CalibrationWizardStartPage.hpp"
#include "I18N.hpp"
#include "Widgets/Label.hpp"

namespace Slic3r { namespace GUI {


CalibrationStartPage::CalibrationStartPage(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    :CalibrationWizardPage(parent, id, pos, size, style)
{
    m_top_sizer = new wxBoxSizer(wxVERTICAL);
}

void CalibrationStartPage::create_when(wxWindow* parent, wxString title, wxString content)
{
    m_when_title = new wxStaticText(this, wxID_ANY, title);
    m_when_title->SetFont(Label::Head_14);
    m_when_title->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    
    m_when_content = new wxStaticText(this, wxID_ANY, content);
    m_when_content->SetFont(Label::Body_14);
    m_when_content->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
}

void CalibrationStartPage::create_bitmap(wxWindow* parent, const wxBitmap& before_img, const wxBitmap& after_img)
{
    m_images_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_before_bmp = new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    m_before_bmp->SetBitmap(before_img);
    m_images_sizer->Add(m_before_bmp, 0, wxALL, 0);
    m_images_sizer->AddSpacer(FromDIP(20));
    m_after_bmp = new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    m_after_bmp->SetBitmap(after_img);
    m_images_sizer->Add(m_after_bmp, 0, wxALL, 0);
}

void CalibrationStartPage::create_bitmap(wxWindow* parent, std::string before_img, std::string after_img)
{
    wxBitmap before_bmp = create_scaled_bitmap(before_img, nullptr, 400);
    wxBitmap after_bmp = create_scaled_bitmap(after_img, nullptr, 400);

    create_bitmap(parent, before_bmp, after_bmp);
}

CalibrationPAStartPage::CalibrationPAStartPage(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationStartPage(parent, id, pos, size, style)
{
    m_cali_mode = CalibMode::Calib_PA_Line;
    m_page_type = CaliPageType::CALI_PAGE_START;

    create_page(this);

    this->SetSizer(m_top_sizer);
    m_top_sizer->Fit(this);
}

void CalibrationPAStartPage::create_page(wxWindow* parent)
{
    m_page_caption = new CaliPageCaption(parent, CalibMode::Calib_PA_Line);
    m_page_caption->show_prev_btn(false);
    m_top_sizer->Add(m_page_caption, 0, wxEXPAND, 0);
    create_when(parent,
                _L("When you need Flow Dynamics Calibration"),
                _L("uneven extrusion"));

    m_top_sizer->Add(m_when_title);
    m_top_sizer->Add(m_when_content);
    m_top_sizer->AddSpacer(PRESET_GAP);

    create_bitmap(parent, "cali_page_before_pa", "cali_page_after_pa");
    m_top_sizer->Add(m_images_sizer, 0, wxALL, 0);

    m_top_sizer->AddSpacer(PRESET_GAP);

    auto about_title = new wxStaticText(parent, wxID_ANY, _L("About this calibration"));
    about_title->SetFont(Label::Head_14);
    about_title->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    m_top_sizer->Add(about_title);
    auto about_text = new wxStaticText(parent, wxID_ANY, _L("After calibration, the linear compensation factor(K) will be recorded and applied to printing. This factor would be different if device, degree of usage, material, and material family type are different"));
    about_text->SetFont(Label::Body_14);
    about_text->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    m_top_sizer->Add(about_text);
    
    m_top_sizer->AddSpacer(PRESET_GAP);

    m_action_panel = new CaliPageActionPanel(parent, CalibMode::Calib_PA_Line, CaliPageType::CALI_PAGE_START);

    m_top_sizer->Add(m_action_panel, 0, wxEXPAND, 0);
}

void CalibrationPAStartPage::on_reset_page()
{
    m_action_panel->enable_button(CaliPageActionType::CALI_ACTION_MANAGE_RESULT, false);
    m_action_panel->enable_button(CaliPageActionType::CALI_ACTION_AUTO_CALI, false);
    m_action_panel->enable_button(CaliPageActionType::CALI_ACTION_MANUAL_CALI, false);
}

void CalibrationPAStartPage::on_device_connected(MachineObject* obj)
{
    //enable all button
    m_action_panel->enable_button(CaliPageActionType::CALI_ACTION_MANAGE_RESULT, true);
    m_action_panel->enable_button(CaliPageActionType::CALI_ACTION_AUTO_CALI, true);
    m_action_panel->enable_button(CaliPageActionType::CALI_ACTION_MANUAL_CALI, true);

    if (obj->printer_type == "BL-P001" || obj->printer_type == "BL-P002") {
        m_action_panel->show_button(CaliPageActionType::CALI_ACTION_MANAGE_RESULT, true);
        m_action_panel->show_button(CaliPageActionType::CALI_ACTION_AUTO_CALI, true);
        m_action_panel->show_button(CaliPageActionType::CALI_ACTION_MANUAL_CALI, true);

        if (obj->cali_version <= -1) {
            m_action_panel->bind_button(CaliPageActionType::CALI_ACTION_MANAGE_RESULT, true);
            m_action_panel->bind_button(CaliPageActionType::CALI_ACTION_AUTO_CALI, true);
            m_action_panel->bind_button(CaliPageActionType::CALI_ACTION_MANUAL_CALI, true);
        }
        else {
            m_action_panel->bind_button(CaliPageActionType::CALI_ACTION_MANAGE_RESULT, false);
            m_action_panel->bind_button(CaliPageActionType::CALI_ACTION_MANUAL_CALI, false);
            m_action_panel->bind_button(CaliPageActionType::CALI_ACTION_MANUAL_CALI, false);
        }
    }
    else if (obj->printer_type == "C11" || obj->printer_type == "C12") {
        m_action_panel->show_button(CaliPageActionType::CALI_ACTION_MANAGE_RESULT, false);
        m_action_panel->show_button(CaliPageActionType::CALI_ACTION_AUTO_CALI, false);
        m_action_panel->show_button(CaliPageActionType::CALI_ACTION_MANUAL_CALI, true);

        if (!obj->is_function_supported(PrinterFunction::FUNC_EXTRUSION_CALI)) {
            m_action_panel->bind_button(CaliPageActionType::CALI_ACTION_MANUAL_CALI, true);
        }
        else {
            m_action_panel->bind_button(CaliPageActionType::CALI_ACTION_MANUAL_CALI, false);
        }
    }

    //is support auto cali 
    bool is_support_pa_auto = (obj->home_flag >> 16 & 1) == 1;
    if (!is_support_pa_auto) {
        m_action_panel->show_button(CaliPageActionType::CALI_ACTION_AUTO_CALI, false);
    }
}

CalibrationFlowRateStartPage::CalibrationFlowRateStartPage(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationStartPage(parent, id, pos, size, style)
{
    m_cali_mode = CalibMode::Calib_Flow_Rate;

    create_page(this);

    this->SetSizer(m_top_sizer);
    m_top_sizer->Fit(this);
}

void CalibrationFlowRateStartPage::create_page(wxWindow* parent)
{
    m_page_caption = new CaliPageCaption(parent, CalibMode::Calib_Flow_Rate);
    m_page_caption->show_prev_btn(false);
    m_top_sizer->Add(m_page_caption, 0, wxEXPAND, 0);
    create_when(parent,
        _L("When you need Flow Rate Calibration"),
        _L("Over-extrusion or under extrusion"));

    m_top_sizer->Add(m_when_title);
    m_top_sizer->Add(m_when_content);
    m_top_sizer->AddSpacer(PRESET_GAP);

    auto recommend_title = new wxStaticText(parent, wxID_ANY, _L("Flow Rate calibration is recommended when you print with:"));
    recommend_title->SetFont(Label::Head_14);
    recommend_title->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    m_top_sizer->Add(recommend_title);
    auto recommend_text1 = new wxStaticText(parent, wxID_ANY, _L("material with significant thermal shrinkage/expansion, such as..."));
    recommend_text1->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    recommend_text1->SetFont(Label::Body_14);
    m_top_sizer->Add(recommend_text1);
    auto recommend_text2 = new wxStaticText(parent, wxID_ANY, _L("materials with inaccurate filament diameter"));
    recommend_text2->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    recommend_text2->SetFont(Label::Body_14);
    m_top_sizer->Add(recommend_text2);

    m_top_sizer->AddSpacer(PRESET_GAP);

    create_bitmap(parent, "cali_page_before_flow", "cali_page_after_flow");

    m_top_sizer->Add(m_images_sizer, 0, wxALL, 0);

    m_top_sizer->AddSpacer(PRESET_GAP);

    m_action_panel = new CaliPageActionPanel(parent, CalibMode::Calib_Flow_Rate, CaliPageType::CALI_PAGE_START);

    m_top_sizer->Add(m_action_panel, 0, wxEXPAND, 0);
}

void CalibrationFlowRateStartPage::on_reset_page()
{
    //disable all button
    m_action_panel->enable_button(CaliPageActionType::CALI_ACTION_MANAGE_RESULT, false);
    m_action_panel->enable_button(CaliPageActionType::CALI_ACTION_AUTO_CALI, false);
    m_action_panel->enable_button(CaliPageActionType::CALI_ACTION_MANUAL_CALI, false);
}

void CalibrationFlowRateStartPage::on_device_connected(MachineObject* obj)
{
    //enable all button
    m_action_panel->enable_button(CaliPageActionType::CALI_ACTION_MANAGE_RESULT, true);
    m_action_panel->enable_button(CaliPageActionType::CALI_ACTION_AUTO_CALI, true);
    m_action_panel->enable_button(CaliPageActionType::CALI_ACTION_MANUAL_CALI, true);

    if (obj->printer_type == "BL-P001" || obj->printer_type == "BL-P002") {
        m_action_panel->show_button(CaliPageActionType::CALI_ACTION_MANAGE_RESULT, false);
        m_action_panel->show_button(CaliPageActionType::CALI_ACTION_AUTO_CALI, true);
        m_action_panel->show_button(CaliPageActionType::CALI_ACTION_MANUAL_CALI, true);

        if (obj->cali_version <= -1) {
            m_action_panel->bind_button(CaliPageActionType::CALI_ACTION_AUTO_CALI, true);
        }
        else {
            m_action_panel->bind_button(CaliPageActionType::CALI_ACTION_MANUAL_CALI, false);
        }
    }
    else if (obj->printer_type == "C11" || obj->printer_type == "C12") {
        m_action_panel->show_button(CaliPageActionType::CALI_ACTION_MANAGE_RESULT, false);
        m_action_panel->show_button(CaliPageActionType::CALI_ACTION_AUTO_CALI, false);
        m_action_panel->show_button(CaliPageActionType::CALI_ACTION_MANUAL_CALI, true);

        m_action_panel->bind_button(CaliPageActionType::CALI_ACTION_MANUAL_CALI, false);
    }

    //is support auto cali 
    bool is_support_flow_rate_auto = (obj->home_flag >> 15 & 1) == 1;
    if (!is_support_flow_rate_auto) {
        m_action_panel->show_button(CaliPageActionType::CALI_ACTION_AUTO_CALI, false);
    }
}

CalibrationMaxVolumetricSpeedStartPage::CalibrationMaxVolumetricSpeedStartPage(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    : CalibrationStartPage(parent, id, pos, size, style)
{
    m_cali_mode = CalibMode::Calib_Vol_speed_Tower;

    create_page(this);

    this->SetSizer(m_top_sizer);
    m_top_sizer->Fit(this);
}

void CalibrationMaxVolumetricSpeedStartPage::create_page(wxWindow* parent)
{
    m_page_caption = new CaliPageCaption(parent, m_cali_mode);
    m_page_caption->show_prev_btn(false);
    m_top_sizer->Add(m_page_caption, 0, wxEXPAND, 0);
    create_when(parent, _L("When you need Max Volumetric Speed Calibration"), _L("Over-extrusion or under extrusion"));

    m_top_sizer->Add(m_when_title);
    m_top_sizer->Add(m_when_content);
    m_top_sizer->AddSpacer(PRESET_GAP);

    auto recommend_title = new wxStaticText(parent, wxID_ANY, _L("Max Volumetric Speed calibration is recommended when you print with:"));
    recommend_title->SetFont(Label::Head_14);
    recommend_title->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    m_top_sizer->Add(recommend_title);
    auto recommend_text1 = new wxStaticText(parent, wxID_ANY, _L("material with significant thermal shrinkage/expansion, such as..."));
    recommend_text1->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    recommend_text1->SetFont(Label::Body_14);
    m_top_sizer->Add(recommend_text1);
    auto recommend_text2 = new wxStaticText(parent, wxID_ANY, _L("materials with inaccurate filament diameter"));
    recommend_text2->Wrap(CALIBRATION_TEXT_MAX_LENGTH);
    recommend_text2->SetFont(Label::Body_14);
    m_top_sizer->Add(recommend_text2);

    m_top_sizer->AddSpacer(PRESET_GAP);

    create_bitmap(parent, "cali_page_before_flow", "cali_page_after_flow");

    m_top_sizer->Add(m_images_sizer, 0, wxALL, 0);

    m_top_sizer->AddSpacer(PRESET_GAP);

    m_action_panel = new CaliPageActionPanel(parent, m_cali_mode, CaliPageType::CALI_PAGE_START);

    m_top_sizer->Add(m_action_panel, 0, wxEXPAND, 0);
}

}}