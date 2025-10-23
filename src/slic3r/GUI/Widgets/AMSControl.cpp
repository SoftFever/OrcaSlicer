#include "AMSControl.hpp"
#include "Label.hpp"
#include "../BitmapCache.hpp"
#include "../I18N.hpp"
#include "../GUI_App.hpp"

#include "slic3r/GUI/DeviceTab/uiAmsHumidityPopup.h"

#include <wx/simplebook.h>
#include <wx/dcgraph.h>

#include <boost/log/trivial.hpp>

#include "CalibUtils.hpp"

namespace Slic3r { namespace GUI {

#define AMS_CANS_SIZE wxSize(FromDIP(284), FromDIP(196))
#define AMS_CANS_WINDOW_SIZE wxSize(FromDIP(264), FromDIP(196))

#define IS_GENERIC_AMS(model) (model != AMSModel::AMS_LITE && model != AMSModel::EXT_AMS)

AMSControl::AMSControl(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size)
    : wxSimplebook(parent, wxID_ANY, pos, size)
    , m_Humidity_tip_popup(AmsHumidityTipPopup(this))
    , m_percent_humidity_dry_popup(new uiAmsPercentHumidityDryPopup(this))
    , m_ams_introduce_popup(AmsIntroducePopup(this))
{
    SetBackgroundColour(*wxWHITE);
    // normal mode
    //Freeze();
    wxBoxSizer *m_sizer_body = new wxBoxSizer(wxVERTICAL);
    m_amswin                 = new wxWindow(this, wxID_ANY, wxDefaultPosition, wxSize(-1, AMS_CAN_ITEM_HEIGHT_SIZE));
    m_amswin->SetBackgroundColour(*wxWHITE);

    // top - ams tag
    m_simplebook_amsprvs = new wxSimplebook(m_amswin, wxID_ANY);
    m_simplebook_amsprvs->SetSize(wxSize(-1, AMS_CAN_ITEM_HEIGHT_SIZE));
    m_simplebook_amsprvs->SetMinSize(wxSize(-1, AMS_CAN_ITEM_HEIGHT_SIZE));
    auto m_sizer_amspreviews = new wxBoxSizer(wxHORIZONTAL);
    m_simplebook_amsprvs->SetSizer(m_sizer_amspreviews);
    m_simplebook_amsprvs->Layout();
    m_sizer_amspreviews->Fit(m_simplebook_amsprvs);
    
    m_panel_prv = new wxPanel(m_simplebook_amsprvs, wxID_ANY, wxDefaultPosition, wxSize(-1, AMS_CAN_ITEM_HEIGHT_SIZE));
    m_sizer_prv = new wxBoxSizer(wxHORIZONTAL);
    m_panel_prv->SetSizer(m_sizer_prv);
    m_panel_prv->Layout();
    m_sizer_prv->Fit(m_panel_prv);

    auto m_panel_top_empty = new wxPanel(m_simplebook_amsprvs, wxID_ANY, wxDefaultPosition, wxSize(-1, AMS_CAN_ITEM_HEIGHT_SIZE));
    auto m_sizer_top_empty = new wxBoxSizer(wxHORIZONTAL);
    m_panel_top_empty->SetSizer(m_sizer_top_empty);
    m_panel_top_empty->Layout();
    m_sizer_top_empty->Fit(m_panel_top_empty);

    m_simplebook_amsprvs->AddPage(m_panel_top_empty, wxEmptyString, false);
    m_simplebook_amsprvs->AddPage(m_panel_prv, wxEmptyString, false);


    wxBoxSizer *m_sizer_bottom = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *m_sizer_left   = new wxBoxSizer(wxVERTICAL);

    //ams tip
    m_sizer_ams_tips = new wxBoxSizer(wxHORIZONTAL);
    m_ams_tip = new Label(m_amswin, _L("AMS"));
    m_ams_tip->SetFont(::Label::Body_12);
    m_ams_tip->SetBackgroundColour(*wxWHITE);
    m_img_amsmapping_tip = new wxStaticBitmap(m_amswin, wxID_ANY, create_scaled_bitmap("enable_ams", this, 16), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), 0);
    m_img_amsmapping_tip->SetBackgroundColour(*wxWHITE);

    m_sizer_ams_tips->Add(m_ams_tip, 0, wxTOP, FromDIP(5));
    m_sizer_ams_tips->Add(m_img_amsmapping_tip, 0, wxALL, FromDIP(3));

    m_img_amsmapping_tip->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
         wxPoint img_pos = m_img_amsmapping_tip->ClientToScreen(wxPoint(0, 0));
         wxPoint popup_pos(img_pos.x, img_pos.y + m_img_amsmapping_tip->GetRect().height);
         m_ams_introduce_popup.set_mode(true);
         m_ams_introduce_popup.Position(popup_pos, wxSize(0, 0));
         m_ams_introduce_popup.Popup();

#ifdef __WXMSW__
         wxCommandEvent close_event(EVT_CLEAR_SPEED_CONTROL);
         wxPostEvent(this, close_event);
#endif // __WXMSW__
    });
    m_img_amsmapping_tip->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {
         m_ams_introduce_popup.Dismiss();
    });


    //backup tips
    m_ams_backup_tip = new Label(m_amswin, _L("Auto Refill"));
    m_ams_backup_tip->SetFont(::Label::Head_12);
    m_ams_backup_tip->SetForegroundColour(wxColour(0x009688));
    m_ams_backup_tip->SetBackgroundColour(*wxWHITE);
    m_img_ams_backup = new wxStaticBitmap(m_amswin, wxID_ANY, create_scaled_bitmap("automatic_material_renewal", this, 16), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), 0);
    m_img_ams_backup->SetBackgroundColour(*wxWHITE);

    m_sizer_ams_tips->Add(0, 0, 1, wxEXPAND, 0);
    m_sizer_ams_tips->Add(m_img_ams_backup, 0, wxALL, FromDIP(3));
    m_sizer_ams_tips->Add(m_ams_backup_tip, 0, wxTOP, FromDIP(5));

    m_ams_backup_tip->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_HAND); });
    m_img_ams_backup->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_HAND); });

    m_ams_backup_tip->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_ARROW); });
    m_img_ams_backup->Bind(wxEVT_LEAVE_WINDOW, [this](auto& e) {SetCursor(wxCURSOR_ARROW); });

    m_ams_backup_tip->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {post_event(SimpleEvent(EVT_AMS_FILAMENT_BACKUP)); });
    m_img_ams_backup->Bind(wxEVT_LEFT_DOWN, [this](auto& e) {post_event(SimpleEvent(EVT_AMS_FILAMENT_BACKUP)); });
   

    //ams cans
    m_panel_can = new StaticBox(m_amswin, wxID_ANY, wxDefaultPosition, AMS_CANS_SIZE, wxBORDER_NONE);
    m_panel_can->SetMinSize(AMS_CANS_SIZE);
    m_panel_can->SetCornerRadius(FromDIP(10));
    m_panel_can->SetBackgroundColor(StateColor(std::pair<wxColour, int>(AMS_CONTROL_DEF_LIB_BK_COLOUR, StateColor::Normal)));

    m_sizer_cans = new wxBoxSizer(wxHORIZONTAL);

    m_simplebook_ams = new wxSimplebook(m_panel_can, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_simplebook_ams->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);
    m_sizer_cans->Add(m_simplebook_ams, 0, wxLEFT | wxLEFT, FromDIP(10));

    // ams mode
    m_simplebook_generic_ams = new wxSimplebook(m_simplebook_ams, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_simplebook_generic_ams->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);

    // none ams mode
    m_none_ams_panel = new wxPanel(m_simplebook_ams, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_none_ams_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);

    auto m_tip_none_ams = new wxStaticText(m_none_ams_panel, wxID_ANY, _L("AMS not connected"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
    m_tip_none_ams->SetMinSize(wxSize(AMS_CANS_SIZE.x - FromDIP(20), -1));
    m_tip_none_ams->SetFont(::Label::Head_16);
    m_tip_none_ams->SetForegroundColour(AMS_CONTROL_DISABLE_COLOUR);

    wxBoxSizer *sizer_ams_panel_v = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *sizer_ams_panel_h = new wxBoxSizer(wxHORIZONTAL);

    sizer_ams_panel_v->Add(m_tip_none_ams, 0, wxALIGN_CENTER, 0);
    sizer_ams_panel_h->Add(sizer_ams_panel_v, 0, wxALIGN_CENTER, 0);

    m_none_ams_panel->SetSizer(sizer_ams_panel_h);
    m_none_ams_panel->Layout();

    //extra ams mode
    m_simplebook_extra_ams = new wxSimplebook(m_simplebook_ams, wxID_ANY, wxDefaultPosition, AMS_CANS_WINDOW_SIZE, 0);
    m_simplebook_extra_ams->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);

    m_simplebook_ams->AddPage(m_none_ams_panel, wxEmptyString, false);
    m_simplebook_ams->AddPage(m_simplebook_generic_ams, wxEmptyString, false);
    m_simplebook_ams->AddPage(m_simplebook_extra_ams, wxEmptyString, false);

    m_panel_can->SetSizer(m_sizer_cans);
    m_panel_can->Layout();
    m_sizer_cans->Fit(m_panel_can);

    m_sizer_left->Add(m_sizer_ams_tips, 0, wxEXPAND, 0);
    m_sizer_left->Add(m_panel_can, 1, wxEXPAND, 0);

    wxBoxSizer *m_sizer_left_bottom = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *sizer_sextruder     = new wxBoxSizer(wxVERTICAL);

    auto extruder_pane = new wxPanel(m_amswin, wxID_ANY, wxDefaultPosition, AMS_EXTRUDER_SIZE);

    extruder_pane->SetSizer(sizer_sextruder);
    extruder_pane->Layout();

    m_extruder = new AMSextruder(extruder_pane, wxID_ANY, wxDefaultPosition, AMS_EXTRUDER_SIZE);
    sizer_sextruder->Add(m_extruder, 0, wxALIGN_CENTER, 0);

    m_sizer_left_bottom->Add(extruder_pane, 0, wxALL,0);

    //m_sizer_left_bottom->Add(0, 0, 0, wxEXPAND, 0);

    StateColor btn_bg_green(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled),
                            std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), 
                            std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
                            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Disabled), 
                            std::pair<wxColour, int>(AMS_CONTROL_DISABLE_COLOUR, StateColor::Pressed),
                            std::pair<wxColour, int>(AMS_CONTROL_DEF_BLOCK_BK_COLOUR, StateColor::Hovered),
                            std::pair<wxColour, int>(AMS_CONTROL_WHITE_COLOUR, StateColor::Normal));

    StateColor btn_bd_green(std::pair<wxColour, int>(wxColour(255,255,254), StateColor::Disabled), 
                            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Enabled));

    StateColor btn_bd_white(std::pair<wxColour, int>(wxColour(255,255,254), StateColor::Disabled), 
                            std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    StateColor btn_text_green(std::pair<wxColour, int>(wxColour(255,255,254), StateColor::Disabled), 
                              std::pair<wxColour, int>(wxColour(255,255,254), StateColor::Enabled));

    StateColor btn_text_white(std::pair<wxColour, int>(wxColour(255, 255, 254), StateColor::Disabled),
                              std::pair<wxColour, int>(wxColour(38, 46, 48), StateColor::Enabled));

    m_button_area = new wxWindow(m_amswin, wxID_ANY);
    m_button_area->SetBackgroundColour(m_amswin->GetBackgroundColour());


    wxBoxSizer *m_sizer_button = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer *m_sizer_button_area = new wxBoxSizer(wxHORIZONTAL);

    m_button_extruder_feed = new Button(m_button_area, _L("Load"));
    m_button_extruder_feed->SetFont(Label::Body_13);

    m_button_extruder_feed->SetBackgroundColor(btn_bg_green);
    m_button_extruder_feed->SetBorderColor(btn_bd_green);
    m_button_extruder_feed->SetTextColor(btn_text_green);
    

    if (wxGetApp().app_config->get("language") == "de_DE") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "fr_FR") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ru_RU") m_button_extruder_feed->SetLabel("Load");
    if (wxGetApp().app_config->get("language") == "nl_NL") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "hu_HU") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ja_JP") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "sv_SE") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "cs_CZ") m_button_extruder_feed->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "uk_UA") m_button_extruder_feed->SetFont(Label::Body_9);

    m_button_extruder_back = new Button(m_button_area, _L("Unload"));
    m_button_extruder_back->SetBackgroundColor(btn_bg_white);
    m_button_extruder_back->SetBorderColor(btn_bd_white);
    m_button_extruder_back->SetTextColor(btn_text_white);
    m_button_extruder_back->SetFont(Label::Body_13);

    if (wxGetApp().app_config->get("language") == "de_DE") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "fr_FR") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ru_RU") m_button_extruder_back->SetLabel("Unload");
    if (wxGetApp().app_config->get("language") == "nl_NL") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "hu_HU") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ja_JP") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "sv_SE") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "cs_CZ") m_button_extruder_back->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "uk_UA") m_button_extruder_back->SetFont(Label::Body_9);

    m_sizer_button_area->Add(0, 0, 1, wxEXPAND, 0);
    m_sizer_button_area->Add(m_button_extruder_back, 0, wxLEFT, FromDIP(6));
    m_sizer_button_area->Add(m_button_extruder_feed, 0, wxLEFT, FromDIP(6));

    m_sizer_button->Add(m_sizer_button_area, 0, 1, wxEXPAND, 0);

    m_button_area->SetSizer(m_sizer_button);
    m_button_area->Layout();
    m_button_area->Fit();

    m_sizer_left_bottom->Add(0, 0, 1, wxEXPAND, 0);
    m_sizer_left_bottom->Add(m_button_area, 0, wxEXPAND | wxTOP, FromDIP(18));
    m_sizer_left->Add(m_sizer_left_bottom, 0, wxEXPAND, 0);


    //virtual ams
    m_panel_virtual = new StaticBox(m_amswin, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    m_panel_virtual->SetBackgroundColor(StateColor(std::pair<wxColour, int>(AMS_CONTROL_DEF_LIB_BK_COLOUR, StateColor::Normal)));
    m_panel_virtual->SetMinSize(wxSize(AMS_CAN_LIB_SIZE.x + FromDIP(16), AMS_CANS_SIZE.y));
    m_panel_virtual->SetMaxSize(wxSize(AMS_CAN_LIB_SIZE.x + FromDIP(16), AMS_CANS_SIZE.y));

    m_vams_info.material_state = AMSCanType::AMS_CAN_TYPE_VIRTUAL;
    m_vams_info.can_id = wxString::Format("%d", VIRTUAL_TRAY_ID).ToStdString();

    auto vams_panel = new wxWindow(m_panel_virtual, wxID_ANY);
    vams_panel->SetBackgroundColour(AMS_CONTROL_DEF_LIB_BK_COLOUR);

    m_vams_lib = new AMSLib(vams_panel, m_vams_info.can_id, m_vams_info);
    m_vams_lib->m_slot_id = m_vams_info.can_id;
    m_vams_road = new AMSRoad(vams_panel, wxID_ANY, m_vams_info, -1, -1, wxDefaultPosition, AMS_CAN_ROAD_SIZE);

    m_vams_lib->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        //clear all selected
        m_current_ams = m_vams_info.can_id;
        m_vams_lib->OnSelected();

        SwitchAms(m_current_ams);
        for (auto ams_item : m_ams_item_list) {
            AmsItem* item = ams_item.second;
            item->SelectCan(m_current_ams);
        }

        e.Skip();
    });

    Bind(EVT_AMS_UNSELETED_VAMS, [this](wxCommandEvent& e) {
        /*if (m_current_ams == e.GetString().ToStdString()) {
            return;
        }*/
        m_current_ams = e.GetString().ToStdString();
        SwitchAms(m_current_ams);
        m_vams_lib->UnSelected();
        e.Skip();
    });

    wxBoxSizer* m_vams_top_sizer = new wxBoxSizer(wxVERTICAL);

    m_vams_top_sizer->Add(0, 0, 0, wxEXPAND | wxTOP, FromDIP(14));
    m_vams_top_sizer->Add(0, 0, 0, wxEXPAND | wxTOP, AMS_REFRESH_SIZE.y);
    m_vams_top_sizer->Add(m_vams_lib, 0, wxEXPAND | wxTOP | wxLEFT | wxRIGHT, FromDIP(4));
    m_vams_top_sizer->Add(m_vams_road, 0, wxALL, 0);

    //extra road

    vams_panel->SetSizer(m_vams_top_sizer);
    vams_panel->Layout();
    vams_panel->Fit();

    wxBoxSizer* m_sizer_vams_panel = new wxBoxSizer(wxVERTICAL);

    m_sizer_vams_panel->Add(vams_panel, 0, wxALIGN_CENTER_HORIZONTAL, 0);
    m_panel_virtual->SetSizer(m_sizer_vams_panel);
    m_panel_virtual->Layout();
    m_panel_virtual->Fit();

    m_vams_sizer =  new wxBoxSizer(wxVERTICAL);
    m_sizer_vams_tips = new wxBoxSizer(wxHORIZONTAL);

    auto m_vams_tip = new wxStaticText(m_amswin, wxID_ANY, _L("Ext Spool"), wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    m_vams_tip->SetMaxSize(wxSize(FromDIP(66), -1));
    m_vams_tip->SetFont(::Label::Body_12);
    m_vams_tip->SetBackgroundColour(*wxWHITE);
    m_img_vams_tip = new wxStaticBitmap(m_amswin, wxID_ANY, create_scaled_bitmap("enable_ams", this, 16), wxDefaultPosition, wxSize(FromDIP(16), FromDIP(16)), 0);
    m_img_vams_tip->SetBackgroundColour(*wxWHITE);
    m_img_vams_tip->Bind(wxEVT_ENTER_WINDOW, [this](auto& e) {
        wxPoint img_pos = m_img_vams_tip->ClientToScreen(wxPoint(0, 0));
        wxPoint popup_pos(img_pos.x, img_pos.y + m_img_vams_tip->GetRect().height);
        m_ams_introduce_popup.set_mode(false);
        m_ams_introduce_popup.Position(popup_pos, wxSize(0, 0));
        m_ams_introduce_popup.Popup();

#ifdef __WXMSW__
        wxCommandEvent close_event(EVT_CLEAR_SPEED_CONTROL);
        wxPostEvent(this, close_event);
#endif // __WXMSW__
    });

    m_img_vams_tip->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) {
        m_ams_introduce_popup.Dismiss();
    });

    m_sizer_vams_tips->Add(m_vams_tip, 0, wxTOP, FromDIP(5));
    m_sizer_vams_tips->Add(m_img_vams_tip, 0, wxALL, FromDIP(3));

    m_vams_extra_road = new AMSVirtualRoad(m_amswin, wxID_ANY);
    m_vams_extra_road->SetMinSize(wxSize(m_panel_virtual->GetSize().x + FromDIP(16), -1));

    m_vams_sizer->Add(m_sizer_vams_tips, 0, wxALIGN_CENTER, 0);
    m_vams_sizer->Add(m_panel_virtual, 0, wxALIGN_CENTER, 0);
    m_vams_sizer->Add(m_vams_extra_road, 1, wxEXPAND, 0);


    //Right
    wxBoxSizer *m_sizer_right = new wxBoxSizer(wxVERTICAL);
    m_simplebook_right        = new wxSimplebook(m_amswin, wxID_ANY);
    m_simplebook_right->SetMinSize(wxSize(AMS_STEP_SIZE.x, AMS_STEP_SIZE.y + FromDIP(19)));
    m_simplebook_right->SetMaxSize(wxSize(AMS_STEP_SIZE.x, AMS_STEP_SIZE.y + FromDIP(19)));
    m_simplebook_right->SetBackgroundColour(*wxWHITE);

    m_sizer_right->Add(m_simplebook_right, 0, wxALL, 0);

    auto tip_right    = new wxPanel(m_simplebook_right, wxID_ANY, wxDefaultPosition, AMS_STEP_SIZE, wxTAB_TRAVERSAL);
    m_sizer_right_tip = new wxBoxSizer(wxVERTICAL);

    m_tip_right_top   = new wxStaticText(tip_right, wxID_ANY, _L("Tips"), wxDefaultPosition, wxDefaultSize, 0);
    m_tip_right_top->SetFont(::Label::Head_13);
    m_tip_right_top->SetForegroundColour(AMS_CONTROL_BRAND_COLOUR);
    m_tip_right_top->Wrap(AMS_STEP_SIZE.x);


    m_tip_load_info = new ::Label(tip_right, wxEmptyString);
    m_tip_load_info->SetFont(::Label::Body_13);
    m_tip_load_info->SetBackgroundColour(*wxWHITE);
    m_tip_load_info->SetForegroundColour(AMS_CONTROL_GRAY700);

    m_sizer_right_tip->Add(m_tip_right_top, 0, 0, 0);
    m_sizer_right_tip->Add(0, 0, 0, wxEXPAND, FromDIP(10));
    m_sizer_right_tip->Add(m_tip_load_info, 0, 0, 0);

    tip_right->SetSizer(m_sizer_right_tip);
    tip_right->Layout();

    m_filament_load_step = new ::StepIndicator(m_simplebook_right, wxID_ANY);
    m_filament_load_step->SetMinSize(AMS_STEP_SIZE);
    m_filament_load_step->SetMaxSize(AMS_STEP_SIZE);
    m_filament_load_step->SetBackgroundColour(*wxWHITE);

    m_filament_unload_step = new ::StepIndicator(m_simplebook_right, wxID_ANY);
    m_filament_unload_step->SetMinSize(AMS_STEP_SIZE);
    m_filament_unload_step->SetMaxSize(AMS_STEP_SIZE);
    m_filament_unload_step->SetBackgroundColour(*wxWHITE);

    m_filament_vt_load_step = new ::StepIndicator(m_simplebook_right, wxID_ANY);
    m_filament_vt_load_step->SetMinSize(AMS_STEP_SIZE);
    m_filament_vt_load_step->SetMaxSize(AMS_STEP_SIZE);
    m_filament_vt_load_step->SetBackgroundColour(*wxWHITE);

    m_simplebook_right->AddPage(tip_right, wxEmptyString, false);
    m_simplebook_right->AddPage(m_filament_load_step, wxEmptyString, false);
    m_simplebook_right->AddPage(m_filament_unload_step, wxEmptyString, false);
    m_simplebook_right->AddPage(m_filament_vt_load_step, wxEmptyString, false);


    m_button_ams_setting_normal = ScalableBitmap(this, "ams_setting_normal", 24);
    m_button_ams_setting_hover = ScalableBitmap(this, "ams_setting_hover", 24);
    m_button_ams_setting_press = ScalableBitmap(this, "ams_setting_press", 24);

    wxBoxSizer *m_sizer_right_bottom = new wxBoxSizer(wxHORIZONTAL);
    m_button_ams_setting = new wxStaticBitmap(m_amswin, wxID_ANY, m_button_ams_setting_normal.bmp(), wxDefaultPosition, wxSize(FromDIP(24), FromDIP(24)));
    m_button_ams_setting->SetBackgroundColour(m_amswin->GetBackgroundColour());

    m_button_guide = new Button(m_amswin, _L("Guide"));
    m_button_guide->SetFont(Label::Body_13);
    if (wxGetApp().app_config->get("language") == "de_DE") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "fr_FR") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ru_RU") m_button_guide->SetLabel("Guide");
    if (wxGetApp().app_config->get("language") == "nl_NL") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "hu_HU") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ja_JP") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "sv_SE") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "cs_CZ") m_button_guide->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "uk_UA") m_button_guide->SetFont(Label::Body_9);

    m_button_guide->SetCornerRadius(FromDIP(12));
    m_button_guide->SetBorderColor(btn_bd_white);
    m_button_guide->SetTextColor(btn_text_white);
    m_button_guide->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_guide->SetBackgroundColor(btn_bg_white);

    m_button_retry = new Button(m_amswin, _L("Retry"));
    m_button_retry->SetFont(Label::Body_13);
    if (wxGetApp().app_config->get("language") == "de_DE") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "fr_FR") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ru_RU") m_button_retry->SetLabel("Retry");
    if (wxGetApp().app_config->get("language") == "nl_NL") m_button_retry->SetLabel("Retry");
    if (wxGetApp().app_config->get("language") == "hu_HU") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "ja_JP") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "sv_SE") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "cs_CZ") m_button_retry->SetFont(Label::Body_9);
    if (wxGetApp().app_config->get("language") == "uk_UA") m_button_retry->SetFont(Label::Body_9);

    m_button_retry->SetCornerRadius(FromDIP(12));
    m_button_retry->SetBorderColor(btn_bd_white);
    m_button_retry->SetTextColor(btn_text_white);
    m_button_retry->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_retry->SetBackgroundColor(btn_bg_white);

    m_sizer_right_bottom->Add(m_button_ams_setting, 0);
    m_sizer_right_bottom->Add(m_button_guide, 0, wxLEFT, FromDIP(10));
    m_sizer_right_bottom->Add(m_button_retry, 0, wxLEFT, FromDIP(10));
    m_sizer_right->Add(m_sizer_right_bottom, 0, wxEXPAND | wxTOP, FromDIP(20));


    m_sizer_bottom->Add(m_vams_sizer, 0, wxEXPAND, 0);
    m_sizer_bottom->Add(m_sizer_left, 0, wxEXPAND, 0);
    m_sizer_bottom->Add(0, 0, 0, wxLEFT, FromDIP(15));
    m_sizer_bottom->Add(m_sizer_right, 0, wxEXPAND, FromDIP(0));

    m_sizer_body->Add(m_simplebook_amsprvs, 0, wxEXPAND, 0);
    m_sizer_body->Add(0, 0, 1, wxEXPAND | wxTOP, FromDIP(18));
    m_sizer_body->Add(m_sizer_bottom, 0, wxEXPAND | wxLEFT, FromDIP(6));

    init_scaled_buttons();
    m_amswin->SetSizer(m_sizer_body);
    m_amswin->Layout();
    m_amswin->Fit();
    //Thaw();

    SetSize(m_amswin->GetSize());
    SetMinSize(m_amswin->GetSize());

    // calibration mode
    m_simplebook_calibration = new wxSimplebook(this, wxID_ANY, wxDefaultPosition, m_amswin->GetSize(), wxTAB_TRAVERSAL);

    auto m_in_calibration_panel = new wxWindow(m_simplebook_calibration, wxID_ANY, wxDefaultPosition, m_amswin->GetSize(), wxTAB_TRAVERSAL);
    m_in_calibration_panel->SetBackgroundColour(AMS_CONTROL_WHITE_COLOUR);
    wxBoxSizer *sizer_calibration_h = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *sizer_calibration_v = new wxBoxSizer(wxVERTICAL);
    auto        thumbnail           = new wxStaticBitmap(m_in_calibration_panel, wxID_ANY, create_scaled_bitmap("ams_icon", nullptr, 126), wxDefaultPosition, wxDefaultSize);
    m_text_calibration_percent      = new wxStaticText(m_in_calibration_panel, wxID_ANY, wxT("0%"), wxDefaultPosition, wxDefaultSize, 0);
    m_text_calibration_percent->SetFont(::Label::Head_16);
    m_text_calibration_percent->SetForegroundColour(AMS_CONTROL_BRAND_COLOUR);
    auto m_text_calibration_tip = new wxStaticText(m_in_calibration_panel, wxID_ANY, _L("Calibrating AMS..."), wxDefaultPosition, wxDefaultSize, 0);
    m_text_calibration_tip->SetFont(::Label::Body_14);
    m_text_calibration_tip->SetForegroundColour(AMS_CONTROL_GRAY700);
    sizer_calibration_v->Add(thumbnail, 0, wxALIGN_CENTER, 0);
    sizer_calibration_v->Add(0, 0, 0, wxTOP, FromDIP(16));
    sizer_calibration_v->Add(m_text_calibration_percent, 0, wxALIGN_CENTER, 0);
    sizer_calibration_v->Add(0, 0, 0, wxTOP, FromDIP(8));
    sizer_calibration_v->Add(m_text_calibration_tip, 0, wxALIGN_CENTER, 0);
    sizer_calibration_h->Add(sizer_calibration_v, 1, wxALIGN_CENTER, 0);
    m_in_calibration_panel->SetSizer(sizer_calibration_h);
    m_in_calibration_panel->Layout();

    auto m_calibration_err_panel = new wxWindow(m_simplebook_calibration, wxID_ANY, wxDefaultPosition, m_amswin->GetSize(), wxTAB_TRAVERSAL);
    m_calibration_err_panel->SetBackgroundColour(AMS_CONTROL_WHITE_COLOUR);
    wxBoxSizer *sizer_err_calibration_h = new wxBoxSizer(wxHORIZONTAL);
    wxBoxSizer *sizer_err_calibration_v = new wxBoxSizer(wxVERTICAL);
    m_hyperlink = new wxHyperlinkCtrl(m_calibration_err_panel, wxID_ANY, wxEmptyString, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxHL_DEFAULT_STYLE);
    m_hyperlink->SetVisitedColour(wxColour(31, 142, 234));
    auto m_tip_calibration_err = new wxStaticText(m_calibration_err_panel, wxID_ANY, _L("A problem occurred during calibration. Click to view the solution."), wxDefaultPosition,
                                                  wxDefaultSize, 0);
    m_tip_calibration_err->SetFont(::Label::Body_14);
    m_tip_calibration_err->SetForegroundColour(AMS_CONTROL_GRAY700);

    wxBoxSizer *sizer_button = new wxBoxSizer(wxHORIZONTAL);

    auto m_button_calibration_again = new Button(m_calibration_err_panel, _L("Calibrate again"));
    m_button_calibration_again->SetBackgroundColor(btn_bg_green);
    m_button_calibration_again->SetBorderColor(AMS_CONTROL_BRAND_COLOUR);
    m_button_calibration_again->SetTextColor(AMS_CONTROL_WHITE_COLOUR);
    m_button_calibration_again->SetMinSize(AMS_CONTRO_CALIBRATION_BUTTON_SIZE);
    m_button_calibration_again->SetCornerRadius(FromDIP(12));
    m_button_calibration_again->Bind(wxEVT_LEFT_DOWN, &AMSControl::on_clibration_again_click, this);

    sizer_button->Add(m_button_calibration_again, 0, wxALL, 5);

    auto       m_button_calibration_cancel = new Button(m_calibration_err_panel, _L("Cancel calibration"));
    m_button_calibration_cancel->SetBackgroundColor(btn_bg_white);
    m_button_calibration_cancel->SetBorderColor(AMS_CONTROL_GRAY700);
    m_button_calibration_cancel->SetTextColor(AMS_CONTROL_GRAY800);
    m_button_calibration_cancel->SetMinSize(AMS_CONTRO_CALIBRATION_BUTTON_SIZE);
    m_button_calibration_cancel->SetCornerRadius(FromDIP(12));
    m_button_calibration_cancel->Bind(wxEVT_LEFT_DOWN, &AMSControl::on_clibration_cancel_click, this);
    sizer_button->Add(m_button_calibration_cancel, 0, wxALL, 5);

    sizer_err_calibration_v->Add(m_hyperlink, 0, wxALIGN_CENTER, 0);
    sizer_err_calibration_v->Add(0, 0, 0, wxTOP, FromDIP(6));
    sizer_err_calibration_v->Add(m_tip_calibration_err, 0, wxALIGN_CENTER, 0);
    sizer_err_calibration_v->Add(0, 0, 0, wxTOP, FromDIP(8));
    sizer_err_calibration_v->Add(sizer_button, 0, wxALIGN_CENTER | wxTOP, FromDIP(18));
    sizer_err_calibration_h->Add(sizer_err_calibration_v, 1, wxALIGN_CENTER, 0);
    m_calibration_err_panel->SetSizer(sizer_err_calibration_h);
    m_calibration_err_panel->Layout();

    m_simplebook_calibration->AddPage(m_in_calibration_panel, wxEmptyString, false);
    m_simplebook_calibration->AddPage(m_calibration_err_panel, wxEmptyString, false);

    AddPage(m_amswin, wxEmptyString, false);
    AddPage(m_simplebook_calibration, wxEmptyString, false);

    UpdateStepCtrl(false);

    m_button_extruder_feed->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AMSControl::on_filament_load), NULL, this);
    m_button_extruder_back->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(AMSControl::on_filament_unload), NULL, this);
    
    m_button_ams_setting->Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent& e) {
        m_button_ams_setting->SetBitmap(m_button_ams_setting_hover.bmp());
        e.Skip();
    });
    m_button_ams_setting->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        m_button_ams_setting->SetBitmap(m_button_ams_setting_press.bmp());
        on_ams_setting_click(e);
        e.Skip();
    });

    m_button_ams_setting->Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent& e) {
        m_button_ams_setting->SetBitmap(m_button_ams_setting_normal.bmp());
        e.Skip();
    });

    Bind(EVT_AMS_SHOW_HUMIDITY_TIPS, [this](wxCommandEvent& evt) {
        uiAmsHumidityInfo *info    = (uiAmsHumidityInfo *) evt.GetClientData();
        if (info)
        {
            if (info->humidity_percent >= 0)
            {
                m_percent_humidity_dry_popup->Update(info);

                wxPoint img_pos = ClientToScreen(wxPoint(0, 0));
                wxPoint popup_pos(img_pos.x - m_percent_humidity_dry_popup->GetSize().GetWidth() + FromDIP(150), img_pos.y - FromDIP(80));
                m_percent_humidity_dry_popup->Position(popup_pos, wxSize(0, 0));
                m_percent_humidity_dry_popup->Popup();
            }
            else
            {
                wxPoint img_pos = ClientToScreen(wxPoint(0, 0));
                wxPoint popup_pos(img_pos.x - m_Humidity_tip_popup.GetSize().GetWidth() + FromDIP(150), img_pos.y - FromDIP(80));
                m_Humidity_tip_popup.Position(popup_pos, wxSize(0, 0));

                int humidity_value = info->humidity_level;
                if (humidity_value > 0 && humidity_value <= 5) { m_Humidity_tip_popup.set_humidity_level(humidity_value); }
                m_Humidity_tip_popup.Popup();
            }
        }

        delete info;
    });
    Bind(EVT_AMS_ON_SELECTED, &AMSControl::AmsSelectedSwitch, this);

    m_button_guide->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
        post_event(wxCommandEvent(EVT_AMS_GUIDE_WIKI));
        });
    m_button_retry->Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
        post_event(wxCommandEvent(EVT_AMS_RETRY));
        });

    EnterNoneAMSMode();
}

void AMSControl::on_retry()
{
    post_event(wxCommandEvent(EVT_AMS_RETRY));
}

void AMSControl::init_scaled_buttons()
{
    m_button_extruder_feed->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_extruder_feed->SetCornerRadius(FromDIP(12));
    m_button_extruder_back->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_extruder_back->SetCornerRadius(FromDIP(12));
}

std::string AMSControl::GetCurentAms() {
    return m_current_ams;
}
std::string AMSControl::GetCurentShowAms() {
    return m_current_show_ams;
}

std::string AMSControl::GetCurrentCan(std::string amsid)
{
    std::string current_can;
    for (auto ams_item : m_ams_item_list) {
        AmsItem* item = ams_item.second;
        if (item == nullptr){
            continue;
        }
        if (item->get_ams_id() == amsid) {
            current_can = item->GetCurrentCan();
            return current_can;
        }
    }
    return current_can;
}

void AMSControl::AmsSelectedSwitch(wxCommandEvent& event) {
    std::string ams_id_selected = std::to_string(event.GetInt());
    if (m_current_ams != ams_id_selected){
        m_current_ams = ams_id_selected;
    }
    if (m_current_show_ams != ams_id_selected && m_current_show_ams != "") {
        auto iter = m_ams_item_list.find(m_current_show_ams);
        if (iter == m_ams_item_list.end()) return;
        try{
            const auto& can_lib_list = iter->second->get_can_lib_list();
            for (auto can : can_lib_list) {
                can.second->UnSelected();
            }
        }
        catch (...){
            ;
        }
    }
}

wxColour AMSControl::GetCanColour(std::string amsid, std::string canid)
{
    wxColour col = *wxWHITE;
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == amsid) {
            for (auto o = 0; o < m_ams_info[i].cans.size(); o++) {
                if (m_ams_info[i].cans[o].can_id == canid) {
                    col = m_ams_info[i].cans[o].material_colour;
                }
            }
        }
    }
    return col;
}

void AMSControl::SetActionState(bool button_status[])
{
    if (button_status[ActionButton::ACTION_BTN_LOAD]) m_button_extruder_feed->Enable();
    else m_button_extruder_feed->Disable();

    if (button_status[ActionButton::ACTION_BTN_UNLOAD]) m_button_extruder_back->Enable();
    else m_button_extruder_back->Disable();
}

void AMSControl::EnterNoneAMSMode()
{
    m_vams_lib->m_ams_model = m_ext_model;
    if(m_is_none_ams_mode == AMSModel::EXT_AMS) return;
    m_panel_prv->Hide();
    m_simplebook_amsprvs->Hide();
    m_simplebook_amsprvs->SetSelection(0);

    m_simplebook_ams->SetSelection(0);
    m_extruder->no_ams_mode(true);
    m_button_ams_setting->Hide();
    m_button_guide->Hide();
    m_button_extruder_feed->Show();
    m_button_extruder_back->Show();

    ShowFilamentTip(false);
    m_amswin->Layout();
    m_amswin->Fit();
    Layout();
    m_is_none_ams_mode = AMSModel::EXT_AMS;
}

void AMSControl::EnterGenericAMSMode()
{
    m_vams_lib->m_ams_model = m_ext_model;
    if(m_is_none_ams_mode == AMSModel::GENERIC_AMS) return;
    m_panel_prv->Show();
    m_simplebook_amsprvs->Show();
    m_simplebook_amsprvs->SetSelection(1);

    m_vams_lib->m_ams_model = AMSModel::GENERIC_AMS;
    m_ams_tip->SetLabel(_L("AMS"));
    m_img_vams_tip->SetBitmap(create_scaled_bitmap("enable_ams", this, 16)); 
    m_img_vams_tip->Enable();
    m_img_amsmapping_tip->SetBitmap(create_scaled_bitmap("enable_ams", this, 16)); 
    m_img_amsmapping_tip->Enable();

    m_simplebook_ams->SetSelection(1);
    m_extruder->no_ams_mode(false);
    m_button_ams_setting->Show();
    m_button_guide->Show();
    m_button_retry->Show();
    m_button_extruder_feed->Show();
    m_button_extruder_back->Show();
    ShowFilamentTip(true);
    m_amswin->Layout();
    m_amswin->Fit();
    Layout();
    m_is_none_ams_mode = AMSModel::GENERIC_AMS;
}

void AMSControl::EnterExtraAMSMode()
{
    m_vams_lib->m_ams_model = m_ext_model;
    if(m_is_none_ams_mode == AMSModel::AMS_LITE) return;
    m_panel_prv->Hide();
    m_simplebook_amsprvs->Show();
    m_simplebook_amsprvs->SetSelection(1);

    
    m_vams_lib->m_ams_model = AMSModel::AMS_LITE;
    m_ams_tip->SetLabel(wxEmptyString);
    m_img_vams_tip->SetBitmap(create_scaled_bitmap("enable_ams_disable", this, 16));
    m_img_vams_tip->Disable();
    m_img_amsmapping_tip->SetBitmap(create_scaled_bitmap("enable_ams_disable", this, 16));
    m_img_amsmapping_tip->Disable();

    m_simplebook_ams->SetSelection(2);
    m_extruder->no_ams_mode(false);
    m_button_ams_setting->Show();
    m_button_guide->Show();
    m_button_retry->Show();
    m_button_extruder_feed->Show();
    m_button_extruder_back->Show();
    ShowFilamentTip(true);
    m_amswin->Layout();
    m_amswin->Fit();
    Layout();
    Refresh(true);
    m_is_none_ams_mode = AMSModel::AMS_LITE;

}

void AMSControl::EnterCalibrationMode(bool read_to_calibration)
{
    SetSelection(1);
    if (read_to_calibration)
        m_simplebook_calibration->SetSelection(0);
    else
        m_simplebook_calibration->SetSelection(1);
}

void AMSControl::ExitcClibrationMode() { SetSelection(0); }

void AMSControl::SetClibrationpercent(int percent) { m_text_calibration_percent->SetLabelText(wxString::Format("%d%%", percent)); }

void AMSControl::SetClibrationLink(wxString link)
{
    m_hyperlink->SetLabel(link);
    m_hyperlink->SetURL(link);
    m_hyperlink->Refresh();
    m_hyperlink->Update();
}

void AMSControl::PlayRridLoading(wxString amsid, wxString canid)
{
    auto iter = m_ams_item_list.find(amsid.ToStdString());

    if (iter != m_ams_item_list.end()) {
        AmsItem* cans = iter->second;
        cans->PlayRridLoading(canid);
    }
}

void AMSControl::StopRridLoading(wxString amsid, wxString canid)
{
    auto iter = m_ams_item_list.find(amsid.ToStdString());

    if (iter != m_ams_item_list.end()) {
        AmsItem* cans = iter->second;
        cans->StopRridLoading(canid);
    }
}

void AMSControl::msw_rescale()
{
    m_button_ams_setting_normal.msw_rescale();
    m_button_ams_setting_hover.msw_rescale();
    m_button_ams_setting_press.msw_rescale();
    m_button_ams_setting->SetBitmap(m_button_ams_setting_normal.bmp());

    m_extruder->msw_rescale();
    m_vams_extra_road->msw_rescale();

    m_button_extruder_feed->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_extruder_back->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_ams_setting->SetMinSize(wxSize(FromDIP(25), FromDIP(24)));
    m_button_guide->SetMinSize(wxSize(-1, FromDIP(24)));
    m_button_retry->SetMinSize(wxSize(-1, FromDIP(24)));
    m_vams_lib->msw_rescale();

    for (auto ams_item : m_ams_item_list) {
        if (ams_item.second){
            ams_item.second->msw_rescale();
        }
    }


    if (m_percent_humidity_dry_popup){
        m_percent_humidity_dry_popup->msw_rescale();
    }

    Layout();
    Refresh();
}

void AMSControl::UpdateStepCtrl(bool is_extrusion)
{
    wxString FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_COUNT] = {
            _L("Idling..."),
            _L("Heat the nozzle"),
            _L("Cut filament"),
            _L("Pull back current filament"),
            _L("Push new filament into extruder"),
            _L("Purge old filament"),
            _L("Feed Filament"),
            _L("Confirm extruded"),
            _L("Check filament location")
    };

    m_filament_load_step->DeleteAllItems();
    m_filament_unload_step->DeleteAllItems();
    m_filament_vt_load_step->DeleteAllItems();

    if (IS_GENERIC_AMS(m_ams_model) || IS_GENERIC_AMS(m_ext_model)) {
        if (is_extrusion) {
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);
        }
        else {
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
            m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);
        }

        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
        m_filament_vt_load_step->AppendItem(_L("Grab new filament"));
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);

        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
    }


    if (m_ams_model == AMSModel::AMS_LITE || m_ext_model == AMSModel::AMS_LITE) {
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CHECK_POSITION]);
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
        m_filament_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);

        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CHECK_POSITION]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PUSH_NEW_FILAMENT]);
        m_filament_vt_load_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PURGE_OLD_FILAMENT]);

        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_HEAT_NOZZLE]);
        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CHECK_POSITION]);
        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_CUT_FILAMENT]);
        m_filament_unload_step->AppendItem(FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_PULL_CURR_FILAMENT]);
    }
}

void AMSControl::CreateAms()
{
    auto caninfo0_0 = Caninfo{"def_can_0", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};
    auto caninfo0_1 = Caninfo{"def_can_1", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};
    auto caninfo0_2 = Caninfo{"def_can_2", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};
    auto caninfo0_3 = Caninfo{"def_can_3", (""), *wxWHITE, AMSCanType::AMS_CAN_TYPE_NONE};

    AMSinfo                        ams1 = AMSinfo{"0", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    AMSinfo                        ams2 = AMSinfo{"1", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    AMSinfo                        ams3 = AMSinfo{"2", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    AMSinfo                        ams4 = AMSinfo{"3", std::vector<Caninfo>{caninfo0_0, caninfo0_1, caninfo0_2, caninfo0_3}};
    std::vector<AMSinfo>           ams_info{ams1, ams2, ams3, ams4};
    std::vector<AMSinfo>::iterator it;
    Freeze();
    for (it = ams_info.begin(); it != ams_info.end(); it++) { 
        AddAmsPreview(*it); 
        AddAms(*it);
        AddExtraAms(*it);
        m_ams_info.push_back(*it);
    }

    m_sizer_prv->Layout();
    Thaw();
}


void AMSControl::ClearAms() {
    m_simplebook_generic_ams->DeleteAllPages();
    m_simplebook_extra_ams->DeleteAllPages();
    m_simplebook_generic_ams->DestroyChildren();
    m_simplebook_extra_ams->DestroyChildren();
    m_simplebook_generic_ams->Layout();
    m_simplebook_extra_ams->Layout();
    m_simplebook_generic_ams->Refresh();
    m_simplebook_extra_ams->Refresh();

    for (auto it : m_ams_preview_list) {
        delete it.second;
    }
    m_ams_preview_list.clear();

    m_current_show_ams = "";
    m_current_ams      = "";
    m_current_select   = "";

    m_ams_generic_item_list.clear();
    m_ams_extra_item_list.clear();
    m_ams_item_list.clear();
    m_sizer_prv->Clear();
}

void AMSControl::CreateAmsSingleNozzle()
{
    //add ams data
    for (auto ams_info = m_ams_info.begin(); ams_info != m_ams_info.end(); ams_info++) {
        if (ams_info->cans.size() == GENERIC_AMS_SLOT_NUM) {
            AddAmsPreview(*ams_info);
            AddAms(*ams_info);
            AddExtraAms(*ams_info);
        }
        else if (ams_info->cans.size() == 1) {
            AddAmsPreview(*ams_info);
            AddAms(*ams_info);
        }
    }
}

void AMSControl::Reset() 
{
    m_ams_info.clear();
    ClearAms();

    Layout();
}

void AMSControl::show_noams_mode()
{
    show_vams(true);
    m_sizer_ams_tips->Show(true);

    if (m_ams_model == AMSModel::EXT_AMS) {
        EnterNoneAMSMode();
    } else if (IS_GENERIC_AMS(m_ams_model)){
        EnterGenericAMSMode();
    } else if (m_ams_model == AMSModel::AMS_LITE) {
        EnterExtraAMSMode();
    }
}

void AMSControl::show_auto_refill(bool show)
{
    m_ams_backup_tip->Show(show);
    m_img_ams_backup->Show(show);
    m_amswin->Layout();
    m_amswin->Fit();
}

void AMSControl::show_vams(bool show)
{
    m_panel_virtual->Show(show);
    m_vams_sizer->Show(show);
    m_vams_extra_road->Show(show);
    m_extruder->has_ams(show);
    show_vams_kn_value(show);
    Layout();

    if (show && m_is_none_ams_mode) {
        if (m_current_ams == "") {
            wxMouseEvent event(wxEVT_LEFT_DOWN);
            event.SetEventObject(m_vams_lib);
            wxPostEvent(m_vams_lib, event);
        }
    }
}

void AMSControl::show_vams_kn_value(bool show)
{
    m_vams_lib->show_kn_value(show);
}

void AMSControl::update_vams_kn_value(AmsTray tray, MachineObject* obj)
{
    m_vams_lib->m_obj = obj;
    if (obj->cali_version >= 0) {
        float k_value = 0;
        float n_value = 0;
        CalibUtils::get_pa_k_n_value_by_cali_idx(obj, tray.cali_idx, k_value, n_value);
        m_vams_info.k        = k_value;
        m_vams_info.n        = n_value;
        m_vams_lib->m_info.k = k_value;
        m_vams_lib->m_info.n = n_value;
    }
    else { // the remaining printer types
        m_vams_info.k        = tray.k;
        m_vams_info.n        = tray.n;
        m_vams_lib->m_info.k = tray.k;
        m_vams_lib->m_info.n = tray.n;
    }
    m_vams_info.material_name = tray.get_display_filament_type();
    m_vams_info.material_colour = tray.get_color();
    m_vams_lib->m_info.material_name = tray.get_display_filament_type();
    m_vams_lib->m_info.material_colour = tray.get_color();
    m_vams_lib->Refresh();
}

void AMSControl::reset_vams()
{
    m_vams_lib->m_info.k = 0;
    m_vams_lib->m_info.n = 0;
    m_vams_lib->m_info.material_name = wxEmptyString;
    m_vams_lib->m_info.material_colour = AMS_CONTROL_WHITE_COLOUR;
    m_vams_info.material_name = wxEmptyString;
    m_vams_info.material_colour = AMS_CONTROL_WHITE_COLOUR;
    m_vams_lib->Refresh();
}


void AMSControl::UpdateAms(std::vector<AMSinfo> ams_info, bool is_reset)
{
    m_button_area->Layout();
    m_button_area->Fit();

    /*if (!test)*/{
        // update item
        bool fresh = false;

        // basic check
        if (m_ams_info.size() == ams_info.size() ) {
            for (int i = 0; i < m_ams_info.size(); i++){
                if (m_ams_info[i].ams_id != ams_info[i].ams_id){
                    fresh = true;
                }
            }
        }
        else{
            fresh = true;
        }

        m_ams_info.clear();
        m_ams_info = ams_info;
        if (fresh){
            ClearAms();
            //if (m_extder_data.total_extder_count >= 2){
            //    CreateAmsDoubleNozzle(series_name, printer_type);
            //}else{
                CreateAmsSingleNozzle();
            //}
            SetSize(wxSize(FromDIP(578), -1));
            SetMinSize(wxSize(FromDIP(578), -1));
            Layout();
        }
		
        if (IS_GENERIC_AMS(m_ams_model)) {
            m_ams_item_list = m_ams_generic_item_list;
        }
        else if (m_ams_model == AMSModel::AMS_LITE) {
            m_ams_item_list = m_ams_extra_item_list;
        }

        if (ams_info.size() > 1) {
            m_simplebook_amsprvs->Show();
            m_amswin->Layout();
            m_amswin->Fit();
            SetSize(m_amswin->GetSize());
            SetMinSize(m_amswin->GetSize());
        } else {
            m_simplebook_amsprvs->Hide();
            m_amswin->Layout();
            m_amswin->Fit();
            SetSize(m_amswin->GetSize());
            SetMinSize(m_amswin->GetSize());
        }

        // update cans

        for (auto ams_item : m_ams_item_list) {
            if (ams_item.second == nullptr){
                continue;
            }
            std::string ams_id = ams_item.second->get_ams_id();
            AmsItem* cans = ams_item.second;
            for (auto ifo : m_ams_info) {
                if (ifo.ams_id == ams_id) {
                    cans->Update(ifo);
                    cans->show_sn_value(m_ams_model == AMSModel::AMS_LITE?false:true);
                }
            }
        }

        for (auto ams_prv : m_ams_preview_list) {
            std::string id = ams_prv.second->get_ams_id();
            auto item = m_ams_item_list.find(id);
            if (item != m_ams_item_list.end())
            { ams_prv.second->Update(item->second->get_ams_info());
            }
        }

        if ( m_current_show_ams.empty() && !is_reset ) {
            if (ams_info.size() > 0) {
                SwitchAms(ams_info[0].ams_id);
            }
        }

        if (m_ams_model == AMSModel::EXT_AMS && !m_vams_lib->is_selected()) {
            m_vams_lib->OnSelected();
        }
    }

    /*update humidity popup*/
    if (m_percent_humidity_dry_popup->IsShown())
    {
        string target_id = m_percent_humidity_dry_popup->get_owner_ams_id();
        for (const auto& the_info : ams_info)
        {
            if (target_id == the_info.ams_id)
            {
                uiAmsHumidityInfo humidity_info;
                humidity_info.ams_id = the_info.ams_id;
                humidity_info.humidity_level = the_info.ams_humidity;
                humidity_info.humidity_percent = the_info.humidity_raw;
                humidity_info.left_dry_time = the_info.left_dray_time;
                humidity_info.current_temperature = the_info.current_temperature;
                m_percent_humidity_dry_popup->Update(&humidity_info);
                break;
            }
        }
    }
}

void AMSControl::AddAmsPreview(AMSinfo info)
{
    auto ams_prv = new AMSPreview(m_panel_prv, wxID_ANY, info);
    m_sizer_prv->Add(ams_prv, 0, wxALIGN_CENTER | wxRIGHT, 6);

    ams_prv->Bind(wxEVT_LEFT_DOWN, [this, ams_prv](wxMouseEvent& e) {
        SwitchAms(ams_prv->get_ams_id());
        e.Skip();
        });
    m_ams_preview_list[info.ams_id] = ams_prv;
}

void AMSControl::AddAms(AMSinfo info)
{
    auto ams_item = new AmsItem(m_simplebook_generic_ams, info, AMSModel::GENERIC_AMS);
    m_simplebook_generic_ams->AddPage(ams_item, wxEmptyString, false);
    ams_item->set_selection(m_simplebook_generic_ams->GetPageCount() - 1);

    m_ams_generic_item_list[info.ams_id] = ams_item;
}

void AMSControl::AddExtraAms(AMSinfo info)
{
    auto ams_item = new AmsItem(m_simplebook_extra_ams, info, AMSModel::AMS_LITE);
    m_simplebook_extra_ams->AddPage(ams_item, wxEmptyString, false);
    ams_item->set_selection(m_simplebook_extra_ams->GetPageCount() - 1);

    m_ams_extra_item_list[info.ams_id] = ams_item;
}

void AMSControl::SwitchAms(std::string ams_id)
{
    if(ams_id == m_current_show_ams){return;}

    if (ams_id != std::to_string(VIRTUAL_TRAY_ID)) {
        if (m_current_show_ams != ams_id) {
            m_current_show_ams = ams_id;
            m_extruder->OnAmsLoading(false);
        }
    }

    for (auto prv_it : m_ams_preview_list) {
        AMSPreview* prv = prv_it.second;
        if (prv->get_ams_id() == m_current_show_ams) {
            prv->OnSelected();
            m_current_select = ams_id;

            bool ready_selected = false;
            for (auto item_it : m_ams_item_list) {
                AmsItem* item = item_it.second;
                if (item->get_ams_id() == ams_id) {
                    for (auto lib_it : item->get_can_lib_list()) {
                        AMSLib* lib = lib_it.second;
                        if (lib->is_selected()) {
                            ready_selected = true;
                        }
                    }
                }
            }

            if (!ready_selected) {
                m_current_ams = std::to_string(VIRTUAL_TRAY_ID);
                m_vams_lib->OnSelected();
            }
            else {
                m_current_ams = ams_id;
                m_vams_lib->UnSelected();
            }

        } else {
            prv->UnSelected();
        }
        m_sizer_prv->Layout();
        m_panel_prv->Fit();
    }

    for (auto ams_item : m_ams_item_list) {
        AmsItem* item = ams_item.second;
        if (item->get_ams_id() == ams_id) {

            if (IS_GENERIC_AMS(m_ams_model)) {
                m_simplebook_generic_ams->SetSelection(item->get_selection());
            }
            else if (m_ams_model == AMSModel::AMS_LITE) {
                m_simplebook_extra_ams->SetSelection(item->get_selection());
            }
        }
    }


     // update extruder
    //m_extruder->OnAmsLoading(false);
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams) {
            switch (m_ams_info[i].current_step) {
            case AMSPassRoadSTEP::AMS_ROAD_STEP_NONE: m_extruder->TurnOff(); break;

            case AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1: m_extruder->TurnOff(); break;

            case AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2: m_extruder->TurnOn(GetCanColour(m_current_ams, m_ams_info[i].current_can_id)); break;

            case AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3: m_extruder->TurnOn(GetCanColour(m_current_ams, m_ams_info[i].current_can_id)); break;
            }
        }
    }
}

void AMSControl::SetFilamentStep(int item_idx, FilamentStepType f_type)
{
    wxString FILAMENT_CHANGE_STEP_STRING[FilamentStep::STEP_COUNT] = {
        _L("Idling..."),
        _L("Heat the nozzle"),
        _L("Cut filament"),
        _L("Pull back current filament"),
        _L("Push new filament into extruder"),
        _L("Purge old filament"),
        _L("Feed Filament"),
        _L("Confirm extruded"),
        _L("Check filament location")
    };


    if (item_idx == FilamentStep::STEP_IDLE) {
        m_simplebook_right->SetSelection(0);
        m_filament_load_step->Idle();
        m_filament_unload_step->Idle();
        m_filament_vt_load_step->Idle();
        return;
    }

    wxString step_str = wxEmptyString;
    if (item_idx < FilamentStep::STEP_COUNT) {
        step_str = FILAMENT_CHANGE_STEP_STRING[item_idx];
    }

    if (f_type == FilamentStepType::STEP_TYPE_LOAD) {
        if (item_idx > 0 && item_idx < FilamentStep::STEP_COUNT) {
            if (m_simplebook_right->GetSelection() != 1) {
                m_simplebook_right->SetSelection(1);
            }

            m_filament_load_step->SelectItem( m_filament_load_step->GetItemUseText(step_str) );
        } else {
            m_filament_load_step->Idle();
        }
    } else if (f_type == FilamentStepType::STEP_TYPE_UNLOAD) {
        if (item_idx > 0 && item_idx < FilamentStep::STEP_COUNT) {
            if (m_simplebook_right->GetSelection() != 2) {
                m_simplebook_right->SetSelection(2);
            }
            m_filament_unload_step->SelectItem( m_filament_unload_step->GetItemUseText(step_str) );
        }
        else {
            m_filament_unload_step->Idle();
        }
    } else if (f_type == FilamentStepType::STEP_TYPE_VT_LOAD) {
        m_simplebook_right->SetSelection(3);
        if (item_idx > 0 && item_idx < FilamentStep::STEP_COUNT) {
            if (item_idx == STEP_CONFIRM_EXTRUDED) {
                m_filament_vt_load_step->SelectItem(2);
            }
            else {
                m_filament_vt_load_step->SelectItem( m_filament_vt_load_step->GetItemUseText(step_str) );
            }
        }
        else {
            m_filament_vt_load_step->Idle();
        }
    } else {
        if (item_idx > 0 && item_idx < FilamentStep::STEP_COUNT) {
            m_simplebook_right->SetSelection(1);
            m_filament_load_step->SelectItem( m_filament_load_step->GetItemUseText(step_str) );
        }
        else {
            m_filament_load_step->Idle();
        }
    }
}

void AMSControl::ShowFilamentTip(bool hasams)
{
    m_simplebook_right->SetSelection(0);
    if (hasams) {
        m_tip_right_top->Show();
        m_tip_load_info->SetLabelText(_L("Choose an AMS slot then press \"Load\" or \"Unload\" button to automatically load or unload filaments."));
    } else {
        // m_tip_load_info->SetLabelText(_L("Before loading, please make sure the filament is pushed into toolhead."));
        m_tip_right_top->Hide();
        m_tip_load_info->SetLabelText(wxEmptyString);
    }

    m_tip_load_info->SetMinSize(AMS_STEP_SIZE);
    m_tip_load_info->Wrap(AMS_STEP_SIZE.x - FromDIP(5));
    m_sizer_right_tip->Layout();
}

bool AMSControl::Enable(bool enable)
{
    for (auto prv_it : m_ams_preview_list) {
        AMSPreview* prv = prv_it.second;
        prv->Enable(enable);
    }

    for (auto item_it : m_ams_item_list) {
        AmsItem* item = item_it.second;
        item->Enable(enable);
    }

    m_button_extruder_feed->Enable(enable);
    m_button_extruder_back->Enable(enable);
    m_button_ams_setting->Enable(enable);

    m_filament_load_step->Enable(enable);
    return wxWindow::Enable(enable);
}

void AMSControl::SetExtruder(bool on_off, bool is_vams, std::string ams_now, wxColour col)
{
    if (IS_GENERIC_AMS(m_ams_model) || IS_GENERIC_AMS(m_ext_model)) {
        if (!on_off) {
            m_extruder->TurnOff();
            m_vams_extra_road->OnVamsLoading(false);
            m_extruder->OnVamsLoading(false);
            m_vams_road->OnVamsLoading(false);
        }
        else {
            m_extruder->TurnOn(col);

            if (ams_now != GetCurentShowAms()) {
                m_extruder->OnAmsLoading(false, col);
            }
            else {
                m_extruder->OnAmsLoading(true, col);
            }
        }

        if (is_vams && on_off) {
            m_extruder->OnAmsLoading(false);
            m_vams_extra_road->OnVamsLoading(true, col);
            m_extruder->OnVamsLoading(true, col);
            m_vams_road->OnVamsLoading(true, col);
        }
        else {
            m_vams_extra_road->OnVamsLoading(false);
            m_extruder->OnVamsLoading(false);
            m_vams_road->OnVamsLoading(false);
        }
    }
    else if (m_ams_model == AMSModel::AMS_LITE || m_ext_model == AMSModel::AMS_LITE) {
        if (!is_vams && !on_off) {
            m_extruder->TurnOff();
            m_extruder->OnVamsLoading(false);
            m_vams_extra_road->OnVamsLoading(false);
            m_vams_road->OnVamsLoading(false);
        }
        else {
            m_extruder->TurnOn(col);
        }

        if (is_vams && on_off) {
            m_vams_extra_road->OnVamsLoading(true, col);
            m_extruder->OnVamsLoading(true, col);
            m_vams_road->OnVamsLoading(true, col);
        }
        else {
            m_vams_extra_road->OnVamsLoading(false);
            m_extruder->OnVamsLoading(false);
            m_vams_road->OnVamsLoading(false);
        }
    }
}

void AMSControl::SetAmsStep(std::string ams_id, std::string canid, AMSPassRoadType type, AMSPassRoadSTEP step)
{
    AmsItem* ams = nullptr;
    auto amsit = m_ams_item_list.find(ams_id);

    if (ams_id != m_last_ams_id || m_last_tray_id != canid) {
        SetAmsStep(m_last_ams_id, m_last_tray_id, AMSPassRoadType::AMS_ROAD_TYPE_UNLOAD, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
        m_vams_extra_road->OnVamsLoading(false);
        m_extruder->OnVamsLoading(false);
        m_vams_road->OnVamsLoading(false);
    }

    if (amsit != m_ams_item_list.end()) {ams = amsit->second;}
    else {return;}
    if (ams == nullptr) return;

    m_last_ams_id = ams_id;
    m_last_tray_id = canid;


    if (IS_GENERIC_AMS(m_ams_model)) {
        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
            ams->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_NONE);
            m_extruder->OnAmsLoading(false);
        }

        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP1) {
            ams->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
            m_extruder->OnAmsLoading(false);
        }

        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP2) {
            ams->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
            ams->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
            if (m_current_show_ams == ams_id) {
                m_extruder->OnAmsLoading(true, ams->GetTagColr(canid));
            }
        }

        if (step == AMSPassRoadSTEP::AMS_ROAD_STEP_COMBO_LOAD_STEP3) {
            ams->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_1);
            ams->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_2);
            ams->SetAmsStep(canid, type, AMSPassRoadSTEP::AMS_ROAD_STEP_3);
            m_extruder->OnAmsLoading(true, ams->GetTagColr(canid));
        }
    }
    else if (m_ams_model == AMSModel::AMS_LITE) {
        ams->SetAmsStepExtra(canid, type, step);
        if (step != AMSPassRoadSTEP::AMS_ROAD_STEP_NONE) {
            m_extruder->OnAmsLoading(true, ams->GetTagColr(canid));
        }
        else {
            m_extruder->OnAmsLoading(false);
        }
    }

    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == ams_id) {
            m_ams_info[i].current_step   = step;
            m_ams_info[i].current_can_id = canid;
        }
    }
}

void AMSControl::on_filament_load(wxCommandEvent &event)
{
    m_button_extruder_back->Disable();
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_LOAD; }
    }
    post_event(SimpleEvent(EVT_AMS_LOAD));
}

void AMSControl::on_extrusion_cali(wxCommandEvent &event)
{
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_CALI; }
    }
    post_event(SimpleEvent(EVT_AMS_EXTRUSION_CALI));
}

void AMSControl::on_filament_unload(wxCommandEvent &event)
{
    m_button_extruder_feed->Disable();
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_UNLOAD; }
    }
    post_event(SimpleEvent(EVT_AMS_UNLOAD));
}

void AMSControl::on_ams_setting_click(wxMouseEvent &event)
{
    for (auto i = 0; i < m_ams_info.size(); i++) {
        if (m_ams_info[i].ams_id == m_current_ams) { m_ams_info[i].current_action = AMSAction::AMS_ACTION_CALI; }
    }
    post_event(SimpleEvent(EVT_AMS_SETTINGS));
}

void AMSControl::on_clibration_again_click(wxMouseEvent &event) { post_event(SimpleEvent(EVT_AMS_CLIBRATION_AGAIN)); }

void AMSControl::on_clibration_cancel_click(wxMouseEvent &event) { post_event(SimpleEvent(EVT_AMS_CLIBRATION_CANCEL)); }

void AMSControl::post_event(wxEvent &&event)
{
    event.SetEventObject(m_parent);
    wxPostEvent(m_parent, event);
}

}} // namespace Slic3r::GUI
