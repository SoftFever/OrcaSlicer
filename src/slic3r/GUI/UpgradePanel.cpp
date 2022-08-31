#include "UpgradePanel.hpp"
#include <slic3r/GUI/Widgets/Label.hpp>
#include <slic3r/GUI/I18N.hpp>
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "libslic3r/Thread.hpp"
#include "ReleaseNote.hpp"

namespace Slic3r {
namespace GUI {

static const wxColour TEXT_NORMAL_CLR = wxColour(0, 174, 66);
static const wxColour TEXT_FAILED_CLR = wxColour(255, 111, 0);

MachineInfoPanel::MachineInfoPanel(wxWindow* parent, wxWindowID id, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
    :wxPanel(parent, id, pos, size, style)
{
    this->SetBackgroundColour(wxColour(255, 255, 255));

    init_bitmaps();

    wxBoxSizer *m_top_sizer = new wxBoxSizer(wxVERTICAL);

    m_panel_caption = create_caption_panel(this);

    m_top_sizer->Add(m_panel_caption, 0, wxEXPAND | wxALL, 0);

    wxBoxSizer *m_main_sizer = new wxBoxSizer(wxHORIZONTAL);

    wxBoxSizer *m_main_left_sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *m_ota_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_printer_img = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(FromDIP(200), FromDIP(200)));
   
    m_printer_img->SetBitmap(m_img_printer);
    m_ota_sizer->Add(m_printer_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, 0);

    wxBoxSizer *m_ota_content_sizer = new wxBoxSizer(wxVERTICAL);

    m_ota_content_sizer->Add(0, 0, 1, wxEXPAND, 0);

    wxFlexGridSizer *m_ota_info_sizer = new wxFlexGridSizer(0, 2, 0, 0);
    m_ota_info_sizer->AddGrowableCol(1);
    m_ota_info_sizer->SetFlexibleDirection(wxHORIZONTAL);
    m_ota_info_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    m_staticText_model_id = new wxStaticText(this, wxID_ANY, _L("Model:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_model_id->Wrap(-1);
    m_staticText_model_id->SetFont(Label::Head_14);
    m_ota_info_sizer->Add(m_staticText_model_id, 0, wxALIGN_RIGHT | wxALL, FromDIP(5));

    m_staticText_model_id_val = new wxStaticText(this, wxID_ANY, "-", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_model_id_val->Wrap(-1);
    m_ota_info_sizer->Add(m_staticText_model_id_val, 0, wxALL | wxEXPAND, FromDIP(5));

    m_staticText_sn = new wxStaticText(this, wxID_ANY, _L("Serial:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_sn->Wrap(-1);
    m_staticText_sn->SetFont(Label::Head_14);
    m_ota_info_sizer->Add(m_staticText_sn, 0, wxALIGN_RIGHT | wxALL | wxEXPAND, FromDIP(5));

    m_staticText_sn_val = new wxStaticText(this, wxID_ANY, "-", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_sn_val->Wrap(-1);
    m_ota_info_sizer->Add(m_staticText_sn_val, 0, wxALL | wxEXPAND, FromDIP(5));

    wxBoxSizer *m_ota_ver_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_ota_ver_sizer->Add(0, 0, 1, wxEXPAND, 0);

    m_ota_new_version_img = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(FromDIP(5), FromDIP(5)));
    m_ota_new_version_img->SetBitmap(upgrade_green_icon);
    m_ota_ver_sizer->Add(m_ota_new_version_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));

    m_staticText_ver = new wxStaticText(this, wxID_ANY, _L("Version:"), wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_ver->Wrap(-1);
    m_staticText_ver->SetFont(Label::Head_14);
    m_ota_ver_sizer->Add(m_staticText_ver, 0, wxALL, FromDIP(5));

    m_ota_info_sizer->Add(m_ota_ver_sizer, 0, wxEXPAND, 0);

    m_staticText_ver_val = new wxStaticText(this, wxID_ANY, "-", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_ver_val->Wrap(-1);
    m_ota_info_sizer->Add(m_staticText_ver_val, 0, wxALL | wxEXPAND, FromDIP(5));

    m_ota_content_sizer->Add(m_ota_info_sizer, 0, wxEXPAND, 0);

    m_ota_content_sizer->Add(0, 0, 1, wxEXPAND, 0);

    m_ota_sizer->Add(m_ota_content_sizer, 1, wxEXPAND, 0);

    m_main_left_sizer->Add(m_ota_sizer, 0, wxEXPAND, 0);

    m_staticline = new wxStaticLine(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLI_HORIZONTAL);
    m_staticline->SetBackgroundColour(wxColour(206,206,206));
    m_staticline->Show(false);
    m_main_left_sizer->Add(m_staticline, 0, wxEXPAND | wxLEFT, FromDIP(40));

    m_ams_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_ams_img   = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(FromDIP(200), FromDIP(200)));

   

    m_ams_img->SetBitmap(m_img_monitor_ams);
    m_ams_sizer->Add(m_ams_img, 0, wxALIGN_TOP | wxALL, FromDIP(5));

    wxBoxSizer *m_ams_content_sizer = new wxBoxSizer(wxVERTICAL);
    m_ams_content_sizer->Add(0, 40, 0, wxEXPAND, FromDIP(5));


    m_ahb_panel = new AmsPanel(this, wxID_ANY);
    m_ahb_panel->m_staticText_ams->SetLabel("AMS HUB");
    m_ams_content_sizer->Add(m_ahb_panel, 0, wxEXPAND, 0);
   

    m_ams_info_sizer = new wxGridSizer(0, 2, FromDIP(30), FromDIP(30));


    for (auto i = 0; i < 4; i++) {
        auto amspanel = new AmsPanel(this, wxID_ANY);
        m_ams_info_sizer->Add(amspanel, 1, wxEXPAND, 5);
        amspanel->Hide();

        /*AmsPanelItem item = AmsPanelItem();
        item.id           = i;
        item.item         = amspanel;*/
        m_amspanel_list.Add(amspanel);
    }

    m_ams_content_sizer->Add(m_ams_info_sizer, 0, wxEXPAND, 0);
    m_ams_sizer->Add(m_ams_content_sizer, 1, wxEXPAND, 0);

    m_main_left_sizer->Add(m_ams_sizer, 0, wxEXPAND, 0);

    //Hide ams
    show_ams(false, true);

    m_main_sizer->Add(m_main_left_sizer, 1, wxEXPAND, 0);

    wxBoxSizer *m_main_right_sizer = new wxBoxSizer(wxVERTICAL);

    m_main_right_sizer->SetMinSize(wxSize(FromDIP(137), -1));

    m_main_right_sizer->Add(0, FromDIP(50), 0, wxEXPAND, FromDIP(5));

    m_button_upgrade_firmware = new Button(this, _L("Upgrade firmware"));
    StateColor btn_bg(std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Disabled), std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed),
                      std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered), std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Enabled),
                      std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Normal));
    StateColor btn_bd(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(0, 174, 66), StateColor::Enabled));
    StateColor btn_text(std::pair<wxColour, int>(wxColour(144, 144, 144), StateColor::Disabled), std::pair<wxColour, int>(wxColour(255, 255, 255), StateColor::Enabled));
    m_button_upgrade_firmware->SetBackgroundColor(btn_bg);
    m_button_upgrade_firmware->SetBorderColor(btn_bd);
    m_button_upgrade_firmware->SetTextColor(btn_text);
    m_button_upgrade_firmware->SetFont(Label::Body_10);
    m_button_upgrade_firmware->SetMinSize(wxSize(FromDIP(-1), FromDIP(24)));
    m_button_upgrade_firmware->SetCornerRadius(FromDIP(12));
    m_main_right_sizer->Add(m_button_upgrade_firmware, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, FromDIP(5));

    m_staticText_upgrading_info = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, 0);
    m_staticText_upgrading_info->Wrap(-1);
    m_main_right_sizer->Add(m_staticText_upgrading_info, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, FromDIP(5));

    m_upgrading_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_upgrading_sizer->Add(0, 0, 1, wxEXPAND, 0);

    m_upgrade_progress = new ProgressBar(this, wxID_ANY, 100, wxDefaultPosition, wxDefaultSize);
    m_upgrade_progress->SetValue(0);
    m_upgrade_progress->SetSize(wxSize(FromDIP(54), FromDIP(14)));
    m_upgrade_progress->SetMinSize(wxSize(FromDIP(54), FromDIP(14)));
    m_upgrading_sizer->Add(m_upgrade_progress, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));

    m_staticText_upgrading_percent = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize);
    m_staticText_upgrading_percent->Wrap(-1);
    m_upgrading_sizer->Add(m_staticText_upgrading_percent, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));

    m_upgrade_retry_img = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxDefaultSize);
    m_upgrading_sizer->Add(m_upgrade_retry_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));

    m_upgrading_sizer->Add(0, 0, 1, wxEXPAND, 0);

    m_main_right_sizer->Add(m_upgrading_sizer, 0, wxEXPAND, 0);

    wxBoxSizer *sizer_release_note = new wxBoxSizer(wxVERTICAL);


    m_staticText_release_note = new wxStaticText(this, wxID_ANY, _L("Release Note"), wxDefaultPosition, wxDefaultSize);
    m_staticText_release_note->Wrap(-1);
    m_staticText_release_note->SetForegroundColour(wxColour(0x1F,0x8E,0xEA));

    auto line_release_note = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    line_release_note->SetBackgroundColour(wxColour(0x1F, 0x8E, 0xEA));

    sizer_release_note->Add(m_staticText_release_note, 0, wxALL, 0);
    sizer_release_note->Add(line_release_note, 1, wxEXPAND | wxALL, 0);

    m_main_right_sizer->Add(sizer_release_note, 0, wxALIGN_CENTER_HORIZONTAL | wxALL, 0);

    m_main_right_sizer->Add(0, 0, 1, wxEXPAND, 0);

    m_main_sizer->Add(m_main_right_sizer, 0, wxEXPAND, 0);

    m_top_sizer->Add(m_main_sizer, 1, wxEXPAND, 0);

    this->SetSizer(m_top_sizer);
    this->Layout();

    // Connect Events
    m_upgrade_retry_img->Bind(wxEVT_LEFT_UP, [this](auto &e) {
        upgrade_firmware_internal();
        });

    m_staticText_release_note->Bind(wxEVT_LEFT_DOWN, &MachineInfoPanel::on_show_release_note, this);
    m_button_upgrade_firmware->Connect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MachineInfoPanel::on_upgrade_firmware), NULL, this);
}


wxPanel *MachineInfoPanel::create_caption_panel(wxWindow *parent)
{
    auto caption_panel = new wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL);
    caption_panel->SetBackgroundColour(wxColour(248, 248, 248));
    caption_panel->SetMinSize(wxSize(FromDIP(925), FromDIP(36)));

    wxBoxSizer *m_caption_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_caption_sizer->Add(17, 0, 0, wxEXPAND, 0);

    m_upgrade_status_img = new wxStaticBitmap(caption_panel, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(FromDIP(5), FromDIP(5)));
    m_upgrade_status_img->SetBitmap(upgrade_gray_icon);
    m_caption_sizer->Add(m_upgrade_status_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));

    m_caption_text = new wxStaticText(caption_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize);
    m_caption_text->Wrap(-1);
    m_caption_sizer->Add(m_caption_text, 1, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));

    caption_panel->SetSizer(m_caption_sizer);
    caption_panel->Layout();
    m_caption_sizer->Fit(caption_panel);

    return caption_panel;
}

void MachineInfoPanel::msw_rescale() 
{
    init_bitmaps();
    m_button_upgrade_firmware->SetSize(wxSize(FromDIP(-1), FromDIP(24)));
    m_button_upgrade_firmware->SetMinSize(wxSize(FromDIP(-1), FromDIP(24)));
    m_button_upgrade_firmware->SetMaxSize(wxSize(FromDIP(-1), FromDIP(24)));
    m_printer_img->SetBitmap(m_img_printer);
    m_ams_img->SetBitmap(m_img_monitor_ams);
    Layout();
    Fit();
}

void MachineInfoPanel::init_bitmaps()
{
    m_img_printer        = create_scaled_bitmap("monitor_upgrade_printer", nullptr, 200);
    m_img_monitor_ams    = create_scaled_bitmap("monitor_upgrade_ams", nullptr, 200);
    upgrade_green_icon   = create_scaled_bitmap("monitor_upgrade_online", nullptr, 5);
    upgrade_gray_icon    = create_scaled_bitmap("monitor_upgrade_offline", nullptr, 5);
    upgrade_yellow_icon  = create_scaled_bitmap("monitor_upgrade_busy", nullptr, 5);
}

MachineInfoPanel::~MachineInfoPanel()
{
    // Disconnect Events
    m_button_upgrade_firmware->Disconnect(wxEVT_COMMAND_BUTTON_CLICKED, wxCommandEventHandler(MachineInfoPanel::on_upgrade_firmware), NULL, this);
}

void MachineInfoPanel::update(MachineObject* obj)
{
    m_obj = obj;
    if (obj) {
        this->Freeze();
        //update online status img
        m_panel_caption->Freeze();
        if (!obj->is_connected()) {
            m_upgrade_status_img->SetBitmap(upgrade_gray_icon);
            wxString caption_text = wxString::Format("%s(%s)", from_u8(obj->dev_name), _L("Offline"));
            m_caption_text->SetLabelText(caption_text);
            show_status(MachineObject::UpgradingDisplayState::UpgradingUnavaliable);
        } else {
            show_status(obj->upgrade_display_state, obj->upgrade_status);
            if (obj->upgrade_display_state == (int) MachineObject::UpgradingDisplayState::UpgradingUnavaliable) {
                if (obj->can_abort()) {
                    wxString caption_text = wxString::Format("%s(%s)", from_u8(obj->dev_name), _L("Printing"));
                    m_caption_text->SetLabelText(caption_text);
                } else {
                    wxString caption_text = wxString::Format("%s", from_u8(obj->dev_name));
                    m_caption_text->SetLabelText(caption_text);
                }
                m_upgrade_status_img->SetBitmap(upgrade_yellow_icon);
            } else {
                wxString caption_text = wxString::Format("%s(%s)", from_u8(obj->dev_name), _L("Idle"));
                m_caption_text->SetLabelText(caption_text);
                m_upgrade_status_img->SetBitmap(upgrade_green_icon);
            }
        }
        m_panel_caption->Layout();
        m_panel_caption->Thaw();

        // update version
        update_version_text(obj);

        // update ams
        update_ams(obj);

        //update progress
        int upgrade_percent = obj->get_upgrade_percent();
        if (obj->upgrade_display_state == (int) MachineObject::UpgradingDisplayState::UpgradingInProgress) {
            m_upgrade_progress->SetValue(upgrade_percent);
            m_staticText_upgrading_percent->SetLabelText(wxString::Format("%d%%", upgrade_percent));
        } else if (obj->upgrade_display_state == (int) MachineObject::UpgradingDisplayState::UpgradingFinished) {
            wxString result_text = obj->get_upgrade_result_str(obj->upgrade_err_code);
            m_staticText_upgrading_info->SetLabelText(result_text);
            m_upgrade_progress->SetValue(upgrade_percent);
            m_staticText_upgrading_percent->SetLabelText(wxString::Format("%d%%", upgrade_percent));
        }

        wxString model_id_text = obj->get_printer_type_display_str();
        m_staticText_model_id_val->SetLabelText(model_id_text);
        wxString sn_text = obj->dev_id;
        m_staticText_sn_val->SetLabelText(sn_text.MakeUpper());

        this->Layout();
        this->Thaw();
    }
}

void MachineInfoPanel::update_version_text(MachineObject* obj)
{
    if (obj->upgrade_display_state == (int)MachineObject::UpgradingDisplayState::UpgradingInProgress) {
        m_staticText_ver_val->SetLabelText("-");
        //m_staticText_ams_ver_val->SetLabelText("-");
        m_ota_new_version_img->Hide();
    } else {
        // update version text
        auto it = obj->module_vers.find("ota");

        // old protocol
        if (obj->new_ver_list.empty() && !obj->m_new_ver_list_exist) {
            if (obj->upgrade_new_version
                && !obj->ota_new_version_number.empty()) {
                if (it != obj->module_vers.end()) {
                    wxString ver_text = wxString::Format("%s->%s", it->second.sw_ver, obj->ota_new_version_number);
                    m_staticText_ver_val->SetLabelText(ver_text);
                }
                else {
                    m_staticText_ver_val->SetLabelText("-");
                }
                m_ota_new_version_img->Show();
            }
            else {
                if (it != obj->module_vers.end()) {
                    wxString ver_text = wxString::Format("%s(%s)", it->second.sw_ver, _L("Latest version"));
                    m_staticText_ver_val->SetLabelText(ver_text);
                }
                else {
                    m_staticText_ver_val->SetLabelText("-");
                }
                m_ota_new_version_img->Hide();
            }
        } else {
            auto ota_it = obj->new_ver_list.find("ota");
            if (ota_it == obj->new_ver_list.end()) {
                wxString ver_text = wxString::Format("%s(%s)", it->second.sw_ver, _L("Latest version"));
                m_staticText_ver_val->SetLabelText(ver_text);
                m_ota_new_version_img->Hide();
            } else {
                if (ota_it->second.sw_new_ver != ota_it->second.sw_ver) {
                    m_ota_new_version_img->Show();
                    wxString ver_text = wxString::Format("%s->%s", ota_it->second.sw_ver, ota_it->second.sw_new_ver);
                    m_staticText_ver_val->SetLabelText(ver_text);
                } else {
                    m_ota_new_version_img->Hide();
                    wxString ver_text = wxString::Format("%s(%s)", it->second.sw_ver, _L("Latest version"));
                    m_staticText_ver_val->SetLabelText(ver_text);
                }
            }
        }
    }
}

void MachineInfoPanel::update_ams(MachineObject *obj)
{
    bool has_hub_model = false;

    //hub
    if (!obj->online_ahb || obj->module_vers.find("ahb") == obj->module_vers.end()) 
        m_ahb_panel->Hide();
    else {
        has_hub_model = true;
        show_ams(true);

        for (auto i = 0; i < m_amspanel_list.GetCount(); i++) {
            AmsPanel *amspanel = m_amspanel_list[i];
            amspanel->Hide();
        }

        m_ahb_panel->Show();

        wxString hub_sn = "-";
        if (!obj->module_vers.find("ahb")->second.sn.empty()) {
            wxString sn_text = obj->module_vers.find("ahb")->second.sn;
            hub_sn           = sn_text.MakeUpper();
        }
        

        wxString hub_ver = "-";
        if (!obj->module_vers.find("ahb")->second.sw_ver.empty()) {
            wxString sn_text = obj->module_vers.find("ahb")->second.sw_ver;
            hub_ver          = sn_text.MakeUpper();
        }
        
       /* auto ver_item = obj->new_ver_list.find("ahb");
        if (ver_item != obj->new_ver_list.end()) {
            m_ahb_panel->m_ams_new_version_img->Show();
            hub_ver = wxString::Format("%s->%s", hub_ver, ver_item->second.sw_new_ver);
        } else {
            m_ahb_panel->m_ams_new_version_img->Hide();
            hub_ver = wxString::Format("%s(%s)", hub_ver, _L("Latest version"));
        }*/

        if (obj->new_ver_list.empty() && !obj->m_new_ver_list_exist) {
            if (obj->upgrade_new_version && obj->ahb_new_version_number.compare(obj->module_vers.find("ahb")->second.sw_ver) != 0) {
                m_ahb_panel->m_ams_new_version_img->Show();

                if (obj->ahb_new_version_number.empty()) {
                    hub_ver = wxString::Format("%s", obj->module_vers.find("ahb")->second.sw_ver);
                } else {
                    hub_ver = wxString::Format("%s->%s", obj->module_vers.find("ahb")->second.sw_ver, obj->ahb_new_version_number);
                }
            } else {
                m_ahb_panel->m_ams_new_version_img->Hide();
                if (obj->ahb_new_version_number.empty()) {
                    wxString ver_text = wxString::Format("%s", obj->module_vers.find("ahb")->second.sw_ver);
                    hub_ver           = ver_text;
                } else {
                    wxString ver_text = wxString::Format("%s(%s)", obj->module_vers.find("ahb")->second.sw_ver, _L("Latest version"));
                    hub_ver           = ver_text;
                }
            }
        } else {
            auto ver_item = obj->new_ver_list.find("ahb");

            if (ver_item == obj->new_ver_list.end()) {
                m_ahb_panel->m_ams_new_version_img->Hide();
                wxString ver_text = wxString::Format("%s(%s)", obj->module_vers.find("ahb")->second.sw_ver, _L("Latest version"));
                hub_ver           = ver_text;
            } else {
                if (ver_item->second.sw_new_ver != ver_item->second.sw_ver) {
                    m_ahb_panel->m_ams_new_version_img->Show();
                    wxString ver_text = wxString::Format("%s->%s", ver_item->second.sw_ver, ver_item->second.sw_new_ver);
                    hub_ver           = ver_text;
                } else {
                    m_ahb_panel->m_ams_new_version_img->Hide();
                    wxString ver_text = wxString::Format("%s(%s)", ver_item->second.sw_ver, _L("Latest version"));
                    hub_ver           = ver_text;
                }
            }
        }

        m_ahb_panel->m_staticText_ams_sn_val->SetLabelText(hub_sn);
        m_ahb_panel->m_staticText_ams_ver_val->SetLabelText(hub_ver);
    }

    //ams
    if (obj->ams_exist_bits != 0) {
        show_ams(true);
        std::map<int, MachineObject::ModuleVersionInfo> ver_list = obj->get_ams_version();

        AmsPanelHash::iterator iter = m_amspanel_list.begin();

        for (auto i = 0; i < m_amspanel_list.GetCount(); i++) {
            AmsPanel *amspanel = m_amspanel_list[i];
             amspanel->Hide();
        }


        auto ams_index = 0;
        for (std::map<std::string, Ams *>::iterator iter = obj->amsList.begin(); iter != obj->amsList.end(); iter++) {
            wxString ams_name;
            wxString ams_sn;
            wxString ams_ver;

            AmsPanel *amspanel = m_amspanel_list[ams_index];
            amspanel->Show();


            auto it = ver_list.find(atoi(iter->first.c_str()));
            auto ams_id = std::stoi(iter->second->id);


            if (it == ver_list.end()) {
                // hide this ams
                wxString ams_text = wxString::Format("AMS%s", std::to_string(ams_id + 1));
                
                ams_name          = ams_text;
                ams_sn   = "-";
                ams_ver  = "-";
            } else {
                // update ams img
                wxString ams_text = wxString::Format("AMS%s", std::to_string(ams_id + 1));
                ams_name = ams_text;

                if (obj->new_ver_list.empty() && !obj->m_new_ver_list_exist) {
                    if (obj->upgrade_new_version
                        && obj->ams_new_version_number.compare(it->second.sw_ver) != 0) { 
                        amspanel->m_ams_new_version_img->Show();

                        if (obj->ams_new_version_number.empty()) {
                            ams_ver = wxString::Format("%s", it->second.sw_ver);
                        } else {
                            ams_ver = wxString::Format("%s->%s", it->second.sw_ver, obj->ams_new_version_number);
                        }
                    } else {
                        amspanel->m_ams_new_version_img->Hide();
                        if (obj->ams_new_version_number.empty()) {
                            wxString ver_text = wxString::Format("%s", it->second.sw_ver);
                            ams_ver           = ver_text;
                        } else {
                            wxString ver_text = wxString::Format("%s(%s)", it->second.sw_ver, _L("Latest version"));
                            ams_ver           = ver_text;
                        }
                    }
                } else {
                    std::string ams_id   = "ams/" + std::to_string(ams_index);
                    auto        ver_item = obj->new_ver_list.find(ams_id);

                    if (ver_item == obj->new_ver_list.end()) {
                        amspanel->m_ams_new_version_img->Hide();
                        wxString ver_text = wxString::Format("%s(%s)", it->second.sw_ver, _L("Latest version"));
                        ams_ver           = ver_text;
                    } else {
                        if (ver_item->second.sw_new_ver != ver_item->second.sw_ver) {
                            amspanel->m_ams_new_version_img->Show();
                            wxString ver_text = wxString::Format("%s->%s", ver_item->second.sw_ver, ver_item->second.sw_new_ver);
                            ams_ver           = ver_text;
                        } else {
                            amspanel->m_ams_new_version_img->Hide();
                            wxString ver_text = wxString::Format("%s(%s)", ver_item->second.sw_ver, _L("Latest version"));
                            ams_ver           = ver_text;
                        }
                    }
                }

                // update ams sn
                if (it->second.sn.empty()) {
                    ams_sn = "-";
                } else {
                    wxString sn_text = it->second.sn;
                    ams_sn = sn_text.MakeUpper();
                }
            }

            amspanel->m_staticText_ams->SetLabelText(ams_name);
            amspanel->m_staticText_ams_sn_val->SetLabelText(ams_sn);
            amspanel->m_staticText_ams_ver_val->SetLabelText(ams_ver);

            ams_index++;
        }
    } else {
        if (!has_hub_model) { show_ams(false); }
        
    }
    this->Layout();
}

void MachineInfoPanel::show_status(int status, std::string upgrade_status_str)
{
    if (last_status == status && last_status_str == upgrade_status_str) return;
    last_status     = status;
    last_status_str = upgrade_status_str;

    BOOST_LOG_TRIVIAL(trace) << "MachineInfoPanel: show_status = " << status << ", str = " << upgrade_status_str;

    Freeze();
    
    if (status == (int)MachineObject::UpgradingDisplayState::UpgradingUnavaliable) {
        m_button_upgrade_firmware->Show();
        m_button_upgrade_firmware->Disable();
        for (size_t i = 0; i < m_upgrading_sizer->GetItemCount(); i++) {
            m_upgrading_sizer->Show(false);
        }
        m_upgrade_retry_img->Hide();
        m_staticText_upgrading_info->Hide();
        m_staticText_upgrading_percent->Hide();
    } else if (status == (int) MachineObject::UpgradingDisplayState::UpgradingAvaliable) {
        m_button_upgrade_firmware->Show();
        m_button_upgrade_firmware->Enable();
        for (size_t i = 0; i < m_upgrading_sizer->GetItemCount(); i++) { m_upgrading_sizer->Show(false); }
        m_upgrade_retry_img->Hide();
        m_staticText_upgrading_info->Hide();
        m_staticText_upgrading_percent->Hide();
    } else if (status == (int) MachineObject::UpgradingDisplayState::UpgradingInProgress) {
        m_button_upgrade_firmware->Hide();
        for (size_t i = 0; i < m_upgrading_sizer->GetItemCount(); i++) { m_upgrading_sizer->Show(true); }
        m_upgrade_retry_img->Hide();
        m_staticText_upgrading_info->Show();
        m_staticText_upgrading_info->SetLabel(_L("Upgrading"));
        m_staticText_upgrading_info->SetForegroundColour(TEXT_NORMAL_CLR);
        m_staticText_upgrading_percent->SetForegroundColour(TEXT_NORMAL_CLR);
        m_staticText_upgrading_percent->Show();
    } else if (status == (int) MachineObject::UpgradingDisplayState::UpgradingFinished) {
        if (upgrade_status_str == "UPGRADE_FAIL") {
            m_staticText_upgrading_info->SetLabel(_L("Upgrading failed"));
            m_staticText_upgrading_info->SetForegroundColour(TEXT_FAILED_CLR);
            for (size_t i = 0; i < m_upgrading_sizer->GetItemCount(); i++) { m_upgrading_sizer->Show(true); }
            m_button_upgrade_firmware->Hide();
            m_staticText_upgrading_info->Show();
            m_staticText_upgrading_percent->Hide();
            m_upgrade_retry_img->Show();
        } else {
            for (size_t i = 0; i < m_upgrading_sizer->GetItemCount(); i++) { m_upgrading_sizer->Show(true); }
            m_button_upgrade_firmware->Hide();
            m_staticText_upgrading_info->SetLabel(_L("Upgrading success"));
            m_staticText_upgrading_info->Show();
            m_staticText_upgrading_info->SetForegroundColour(TEXT_NORMAL_CLR);
            m_staticText_upgrading_percent->SetForegroundColour(TEXT_NORMAL_CLR);
            m_upgrade_retry_img->Hide();
        }
    } else {
        ;
    }
    Layout();
    Thaw();

}

void MachineInfoPanel::show_ams(bool show, bool force_update)
{
    if (m_last_ams_show != show || force_update) {
        m_ams_img->Show(show);
        m_ams_sizer->Show(show);
        m_staticline->Show(show);
        BOOST_LOG_TRIVIAL(trace) << "upgrade: show_ams = " << show;
    }
    m_last_ams_show = show;
}

void MachineInfoPanel::upgrade_firmware_internal() {
    if (!m_obj)
        return;
    if (panel_type == ptOtaPanel) {
        m_obj->command_upgrade_firmware(m_ota_info);
    } else if (panel_type == ptAmsPanel) {
        m_obj->command_upgrade_firmware(m_ams_info);
    } else if (panel_type == ptPushPanel) {
        m_obj->command_upgrade_confirm();
    }
}

void MachineInfoPanel::on_upgrade_firmware(wxCommandEvent &event)
{
    if (m_obj)
        m_obj->command_upgrade_confirm();
}

void MachineInfoPanel::on_show_release_note(wxMouseEvent &event) 
{
    DeviceManager *dev = wxGetApp().getDeviceManager();
    if (!dev) return;


    wxString next_version_release_note;
    wxString now_version_release_note;
    std::string version_number            = "";

    for (auto iter : m_obj->firmware_list) {
        if (iter.version == m_obj->ota_new_version_number) {
            version_number            = m_obj->ota_new_version_number;
            next_version_release_note = wxString::FromUTF8(iter.description);
        }
        if (iter.version == m_obj->get_ota_version()) { 
            version_number           = m_obj->get_ota_version();
            now_version_release_note = wxString::FromUTF8(iter.description);
        }
    }

    ReleaseNoteDialog dlg;

    if (!next_version_release_note.empty()) { 
        dlg.update_release_note(next_version_release_note, version_number);
        dlg.ShowModal();
        return;
    }

    if (!now_version_release_note.empty()) {
        dlg.update_release_note(now_version_release_note, version_number);
        dlg.ShowModal();
        return;
    }
}

UpgradePanel::UpgradePanel(wxWindow *parent, wxWindowID id, const wxPoint &pos, const wxSize &size, long style)
    :wxPanel(parent, id, pos, size, style)
{
    this->SetBackgroundColour(wxColour(238, 238, 238));

    auto m_main_sizer = new wxBoxSizer(wxVERTICAL);

    m_scrolledWindow = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxVSCROLL);
    m_scrolledWindow->SetScrollRate(5, 5);

    m_machine_list_sizer = new wxBoxSizer(wxVERTICAL);

    m_scrolledWindow->SetSizerAndFit(m_machine_list_sizer);

    m_main_sizer->Add(m_scrolledWindow, 1, wxEXPAND, 0);

    this->SetSizerAndFit(m_main_sizer);

    Layout();
}

UpgradePanel::~UpgradePanel()
{

}

void UpgradePanel::msw_rescale() 
{ 
    /*if (m_push_upgrade_panel)
        m_push_upgrade_panel->msw_rescale();*/
}

void UpgradePanel::clean_push_upgrade_panel()
{
    if (m_push_upgrade_panel) {
        delete m_push_upgrade_panel;
        m_push_upgrade_panel = nullptr;
    }
}

void UpgradePanel::refresh_version_and_firmware(MachineObject* obj)
{
    BOOST_LOG_TRIVIAL(trace) << "refresh version";
    if (obj) {
        obj->command_get_version();
        obj->get_firmware_info();
        m_need_update = true;
    }
}

void UpgradePanel::update(MachineObject *obj)
{
    if (m_obj != obj) {
        m_obj = obj;
        refresh_version_and_firmware(obj);
    }

    Freeze();
    if (m_obj && m_need_update) {
        if (m_obj->is_firmware_info_valid()) {
            clean_push_upgrade_panel();
            m_push_upgrade_panel = new MachineInfoPanel(m_scrolledWindow);
            m_machine_list_sizer->Add(m_push_upgrade_panel, 0, wxTOP | wxALIGN_CENTER_HORIZONTAL, FromDIP(8));
            m_need_update = false;
        }
    }

    //update panels
    if (m_push_upgrade_panel) {
        m_push_upgrade_panel->update(obj);
    }

    if (!obj)
        clean_push_upgrade_panel();
    this->Layout();
    Thaw();

    m_obj = obj;
}

bool UpgradePanel::Show(bool show)
{
    if (show) {
        DeviceManager* dev = wxGetApp().getDeviceManager();
        if (dev) {
            MachineObject* obj = dev->get_default_machine();
            refresh_version_and_firmware(obj);
        }
    }
    return wxPanel::Show(show);
}

 AmsPanel::AmsPanel(wxWindow *      parent,
                   wxWindowID      id /*= wxID_ANY*/,
                   const wxPoint & pos /*= wxDefaultPosition*/,
                   const wxSize &  size /*= wxDefaultSize*/,
                   long            style /*= wxTAB_TRAVERSAL*/,
                   const wxString &name /*= wxEmptyString*/)
    : wxPanel(parent,id,pos,size,style)
{
     auto upgrade_green_icon = create_scaled_bitmap("monitor_upgrade_online", nullptr, 5);

     auto ams_sizer = new wxFlexGridSizer(0, 2, 0, 0);
     ams_sizer->AddGrowableCol(1);
     ams_sizer->SetFlexibleDirection(wxHORIZONTAL);
     ams_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

     m_staticText_ams = new wxStaticText(this, wxID_ANY, "-", wxDefaultPosition, wxDefaultSize, 0);
     m_staticText_ams->SetFont(Label::Head_14);
     m_staticText_ams->Wrap(-1);

     auto m_staticText_ams_sn = new wxStaticText(this, wxID_ANY, _L("Serial:"), wxDefaultPosition, wxDefaultSize, 0);
     m_staticText_ams_sn->Wrap(-1);
     m_staticText_ams_sn->SetFont(Label::Head_14);

     m_staticText_ams_sn_val = new wxStaticText(this, wxID_ANY, "-", wxDefaultPosition, wxDefaultSize, 0);
     m_staticText_ams_sn_val->Wrap(-1);

     wxBoxSizer *m_ams_ver_sizer = new wxBoxSizer(wxHORIZONTAL);

     m_ams_ver_sizer->Add(0, 0, 1, wxEXPAND, 0);

     m_ams_new_version_img = new wxStaticBitmap(this, wxID_ANY, wxNullBitmap, wxDefaultPosition, wxSize(FromDIP(5), FromDIP(5)));
     m_ams_new_version_img->SetBitmap(upgrade_green_icon);
     m_ams_ver_sizer->Add(m_ams_new_version_img, 0, wxALIGN_CENTER_VERTICAL | wxALL, FromDIP(5));
     m_ams_new_version_img->Hide();

     auto m_staticText_ams_ver = new wxStaticText(this, wxID_ANY, _L("Version:"), wxDefaultPosition, wxDefaultSize, 0);
     m_staticText_ams_ver->Wrap(-1);
     m_staticText_ams_ver->SetFont(Label::Head_14);
     m_ams_ver_sizer->Add(m_staticText_ams_ver, 0, wxALL, FromDIP(5));

     m_staticText_ams_ver_val = new wxStaticText(this, wxID_ANY, "-", wxDefaultPosition, wxDefaultSize, 0);
     m_staticText_ams_ver_val->Wrap(-1);

     ams_sizer->Add(m_staticText_ams, 0, wxALIGN_RIGHT | wxALL, FromDIP(5));
     ams_sizer->Add(0, 0, 1, wxEXPAND, 5);
     ams_sizer->Add(m_staticText_ams_sn, 0, wxALIGN_RIGHT | wxALL, FromDIP(5));
     ams_sizer->Add(m_staticText_ams_sn_val, 0, wxALL | wxEXPAND, FromDIP(5));
     ams_sizer->Add(m_ams_ver_sizer, 1, wxEXPAND, 5);
     ams_sizer->Add(m_staticText_ams_ver_val, 0, wxALL | wxEXPAND, FromDIP(5));
     ams_sizer->Add(0, 0, 1, wxEXPAND, 0);

     SetSizer(ams_sizer);
     Layout();
 }

 AmsPanel::~AmsPanel() 
 {

 }

}
}