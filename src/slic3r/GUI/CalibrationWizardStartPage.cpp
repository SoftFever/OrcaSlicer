#include "CalibrationWizardStartPage.hpp"
#include "I18N.hpp"
#include "Widgets/Label.hpp"

namespace Slic3r { namespace GUI {

#define CALIBRATION_START_PAGE_TEXT_MAX_LENGTH FromDIP(1000)
CalibrationStartPage::CalibrationStartPage(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style)
    :CalibrationWizardPage(parent, id, pos, size, style)
{
    m_top_sizer = new wxBoxSizer(wxVERTICAL);
}

void CalibrationStartPage::create_when(wxWindow* parent, wxString title, wxString content)
{
    m_when_title = new wxStaticText(this, wxID_ANY, title);
    m_when_title->SetFont(Label::Head_14);
    m_when_title->Wrap(CALIBRATION_START_PAGE_TEXT_MAX_LENGTH);
    
    m_when_content = new wxStaticText(this, wxID_ANY, content);
    m_when_content->SetFont(Label::Body_14);
    m_when_content->Wrap(CALIBRATION_START_PAGE_TEXT_MAX_LENGTH);
}

void CalibrationStartPage::create_about(wxWindow* parent, wxString title, wxString content)
{
    m_about_title = new wxStaticText(this, wxID_ANY, title);
    m_about_title->SetFont(Label::Head_14);
    m_about_title->Wrap(CALIBRATION_START_PAGE_TEXT_MAX_LENGTH);

    m_about_content = new wxStaticText(this, wxID_ANY, content);
    m_about_content->SetFont(Label::Body_14);
    m_about_content->Wrap(CALIBRATION_START_PAGE_TEXT_MAX_LENGTH);
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
    wxBitmap before_bmp = create_scaled_bitmap(before_img, nullptr, 350);
    wxBitmap after_bmp = create_scaled_bitmap(after_img, nullptr, 350);

    create_bitmap(parent, before_bmp, after_bmp);
}

void CalibrationStartPage::create_bitmap(wxWindow* parent, std::string img) {
    wxBitmap before_bmp = create_scaled_bitmap(img, nullptr, 350);
    m_images_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_bmp_intro = new wxStaticBitmap(parent, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize, 0);
    m_bmp_intro->SetBitmap(before_bmp);
    m_images_sizer->Add(m_bmp_intro, 0, wxALL, 0);
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
        _L("We now have added the auto-calibration for different filaments, which is fully automated and the result will be saved into the printer for future use. You only need to do the calibration in the following limited cases:\
\n1. If you introduce a new filament of different brands/models or the filament is damp;\
\n2. if the nozzle is worn out or replaced with a new one;\
\n3. If the max volumetric speed or print temperature is changed in the filament setting."));

    m_top_sizer->Add(m_when_title);
    m_top_sizer->Add(m_when_content);
    m_top_sizer->AddSpacer(PRESET_GAP);

    create_bitmap(parent, "cali_page_before_pa", "cali_page_after_pa");
    m_top_sizer->Add(m_images_sizer, 0, wxALL, 0);
    m_top_sizer->AddSpacer(PRESET_GAP);

    create_about(parent,
        _L("About this calibration"),
        _L("Please find the details of Flow Dynamics Calibration from our wiki.\
\n\nUsually the calibration is unnecessary. When you start a single color/material print, with the \"flow dynamics calibration\" option checked in the print start menu, the printer will follow the old way, calibrate the filament before the print; When you start a multi color/material print, the printer will use the default compensation parameter for the filament during every filament switch which will have a good result in most cases.\
\n\nPlease note there are a few cases that will make the calibration result not reliable: using a texture plate to do the calibration; the build plate is not sticky (please wash the build plate or apply gluestick ! ) ...You can find more from our wiki.\
\n\nThe calibration results have about 10% jitter in our test, which may cause the result not exactly the same in each calibration. We are still investing the root cause."));
    m_top_sizer->Add(m_about_title);
    m_top_sizer->Add(m_about_content);
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

    if (obj->get_printer_series() == PrinterSeries::SERIES_X1) {
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
    else if (obj->get_printer_series() == PrinterSeries::SERIES_P1P) {
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
        _L("After using Flow Dynamics Calibration, there are still some extrusion issues, such as:\
\n1. Over-Extrusion: If you see excess material on your printed object, forming blobs or zits, or the layers seem too thick, it could be a sign of over-extrusion.\
\n2. Under-Extrusion: Signs include missing layers, weak infill, or gaps in the print. This could mean that your printer isn't extruding enough filament.\
\n3. Poor Surface Quality: If the surface of your prints seems rough or uneven, this could be a result of an incorrect flow rate.\
\n4. Weak Structural Integrity: If your prints break easily or don't seem as sturdy as they should be, this might be due to under-extrusion or poor layer adhesion."));

    m_top_sizer->Add(m_when_title);
    m_top_sizer->Add(m_when_content);
    m_top_sizer->AddSpacer(PRESET_GAP);

    create_bitmap(parent, "cali_page_flow_introduction");
    m_top_sizer->Add(m_images_sizer, 0, wxALL, 0);
    m_top_sizer->AddSpacer(PRESET_GAP);

    auto extra_text = new wxStaticText(parent, wxID_ANY, _L("Beyond fixing the noted printing defects, Flow Rate Calibration is crucial for foaming materials like LW-PLA used in RC planes. These materials expand greatly when heated, and calibration provides a useful reference flow rate to achieve good printing results with these special filaments."));
    extra_text->SetFont(Label::Body_14);
    extra_text->Wrap(CALIBRATION_START_PAGE_TEXT_MAX_LENGTH);
    m_top_sizer->Add(extra_text);
    m_top_sizer->AddSpacer(PRESET_GAP);

    create_about(parent,
        _L("About this calibration"),
        _L("This flow rate calibration measures the ratio of expected to actual extrusion volumes. For Bambu Lab printer users using our official filaments, adjustments are seldom needed, as default settings ensure an optimal flow rate. Calibration is typically unnecessary with common filaments. Refer to our wiki article for more information.\
\n\nFlow Rate Calibration utilizes Bambu Lab's Micro-Lidar technology, directly measuring calibration the calibration patterns. However, please be advised that the efficacy and accuracy of this method may be compromised with specific types of materials. Particularly, filaments that are transparent or semi-transparent, sparkling-particled, or have a high-reflective finish may not be suitable for this calibration.\
\n\nThe calibration results may vary between each calibration or filament. We are still improving the accuracy and compatibility of this calibration."));
    m_top_sizer->Add(m_about_title);
    m_top_sizer->Add(m_about_content);
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

    if (obj->get_printer_series() == PrinterSeries::SERIES_X1) {
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
    else if (obj->get_printer_series() == PrinterSeries::SERIES_P1P) {
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
    recommend_title->Wrap(CALIBRATION_START_PAGE_TEXT_MAX_LENGTH);
    m_top_sizer->Add(recommend_title);
    auto recommend_text1 = new wxStaticText(parent, wxID_ANY, _L("material with significant thermal shrinkage/expansion, such as..."));
    recommend_text1->Wrap(CALIBRATION_START_PAGE_TEXT_MAX_LENGTH);
    recommend_text1->SetFont(Label::Body_14);
    m_top_sizer->Add(recommend_text1);
    auto recommend_text2 = new wxStaticText(parent, wxID_ANY, _L("materials with inaccurate filament diameter"));
    recommend_text2->Wrap(CALIBRATION_START_PAGE_TEXT_MAX_LENGTH);
    recommend_text2->SetFont(Label::Body_14);
    m_top_sizer->Add(recommend_text2);

    m_top_sizer->AddSpacer(PRESET_GAP);

    create_bitmap(parent, "cali_page_before_pa", "cali_page_after_pa");

    m_top_sizer->Add(m_images_sizer, 0, wxALL, 0);

    m_top_sizer->AddSpacer(PRESET_GAP);

    m_action_panel = new CaliPageActionPanel(parent, m_cali_mode, CaliPageType::CALI_PAGE_START);

    m_top_sizer->Add(m_action_panel, 0, wxEXPAND, 0);
}

}}