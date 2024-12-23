#include "FilamentMapDialog.hpp"
#include "DragDropPanel.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/SwitchButton.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "CapsuleButton.hpp"
#include "SelectMachine.hpp"

namespace Slic3r { namespace GUI {

FilamentMapDialog::FilamentMapDialog(wxWindow                       *parent,
                                     const std::vector<std::string> &filament_color,
                                     const std::vector<int>         &filament_map,
                                     const std::vector<int>         &filaments,
                                     const FilamentMapMode           mode,
                                     bool                            show_default)
    : wxDialog(parent, wxID_ANY, _L("Filament arrangement method of plate"), wxDefaultPosition, wxSize(2000, 1500))
    , m_filament_color(filament_color)
    , m_filament_map(filament_map)
{
    SetBackgroundColour(*wxWHITE);

    SetMinSize(wxSize(FromDIP(550), -1));
    SetMaxSize(wxSize(FromDIP(550), -1));

    if (mode < fmmManual)
        m_page_type = PageType::ptAuto;
    else if (mode == fmmManual)
        m_page_type = PageType::ptManual;
    else
        m_page_type = PageType::ptDefault;

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);
    main_sizer->AddSpacer(FromDIP(22));

    wxBoxSizer *mode_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_auto_btn   = new CapsuleButton(this, PageType::ptAuto, _L("Auto"), false);
    m_manual_btn = new CapsuleButton(this, PageType::ptManual, _L("Custom"), false);
    if (show_default)
        m_default_btn = new CapsuleButton(this, PageType::ptDefault, _L("Default"), true);
    else
        m_default_btn = nullptr;

    const int button_padding = FromDIP(2);
    mode_sizer->AddStretchSpacer();
    mode_sizer->Add(m_auto_btn, 1, wxALIGN_CENTER | wxLEFT | wxRIGHT, button_padding);
    mode_sizer->Add(m_manual_btn, 1, wxALIGN_CENTER | wxLEFT | wxRIGHT, button_padding);
    if (show_default) mode_sizer->Add(m_default_btn, 1, wxALIGN_CENTER | wxLEFT | wxRIGHT, button_padding);
    mode_sizer->AddStretchSpacer();

    main_sizer->Add(mode_sizer, 0, wxEXPAND);
    main_sizer->AddSpacer(FromDIP(24));

    auto            panel_sizer       = new wxBoxSizer(wxHORIZONTAL);
    FilamentMapMode default_auto_mode = (mode < FilamentMapMode::fmmManual ? mode : FilamentMapMode::fmmAutoForFlush);
    m_manual_map_panel                = new FilamentMapManualPanel(this, m_filament_color, filaments, filament_map);
    m_auto_map_panel                  = new FilamentMapAutoPanel(this, default_auto_mode);
    if (show_default)
        m_default_map_panel = new FilamentMapDefaultPanel(this);
    else
        m_default_map_panel = nullptr;

    panel_sizer->Add(m_manual_map_panel, 0, wxALIGN_CENTER | wxEXPAND);
    panel_sizer->Add(m_auto_map_panel, 0, wxALIGN_CENTER | wxEXPAND);
    if (show_default) panel_sizer->Add(m_default_map_panel, 0, wxALIGN_CENTER | wxEXPAND);
    main_sizer->Add(panel_sizer, 0, wxEXPAND);

    wxBoxSizer *button_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_ok_btn                 = new Button(this, _L("OK"));
    m_cancel_btn             = new Button(this, _L("Cancel"));
    m_ok_btn->SetFont(Label::Body_12);
    m_cancel_btn->SetFont(Label::Body_12);
    button_sizer->Add(m_ok_btn, 0, wxALL, FromDIP(8));
    button_sizer->Add(m_cancel_btn, 0, wxALL, FromDIP(8));
    main_sizer->Add(button_sizer, 0, wxALIGN_RIGHT | wxALL, FromDIP(15));

    m_ok_btn->Bind(wxEVT_BUTTON, &FilamentMapDialog::on_ok, this);
    m_cancel_btn->Bind(wxEVT_BUTTON, &FilamentMapDialog::on_cancle, this);

    m_auto_btn->Bind(wxEVT_BUTTON, &FilamentMapDialog::on_switch_mode, this);
    m_manual_btn->Bind(wxEVT_BUTTON, &FilamentMapDialog::on_switch_mode, this);
    if (show_default) m_default_btn->Bind(wxEVT_BUTTON, &FilamentMapDialog::on_switch_mode, this);

    SetSizer(main_sizer);
    Layout();
    Fit();

    CenterOnParent();
    GUI::wxGetApp().UpdateDarkUIWin(this);
}

FilamentMapMode FilamentMapDialog::get_mode()
{
    if (m_page_type == PageType::ptAuto)
        return m_auto_map_panel->GetMode();
    if (m_page_type == PageType::ptManual)
        return fmmManual;
    return fmmDefault;
}

int FilamentMapDialog::ShowModal()
{
    update_panel_status(m_page_type);
    return wxDialog::ShowModal();
}

void FilamentMapDialog::on_ok(wxCommandEvent &event)
{
    if (m_page_type == PageType::ptManual) {
        std::vector<int> left_filaments = m_manual_map_panel->GetLeftFilaments();
        std::vector<int> right_filaments = m_manual_map_panel->GetRightFilaments();

        for (int i = 0; i < m_filament_map.size(); ++i) {
            if (std::find(left_filaments.begin(), left_filaments.end(), i + 1) != left_filaments.end()) {
                m_filament_map[i] = 1;
            }
            else if (std::find(right_filaments.begin(), right_filaments.end(), i + 1) != right_filaments.end()) {
                m_filament_map[i] = 2;
            }
        }
    }

    EndModal(wxID_OK);
}

void FilamentMapDialog::on_cancle(wxCommandEvent &event) { EndModal(wxID_CANCEL); }

void FilamentMapDialog::update_panel_status(PageType page)
{
    if (page == PageType::ptDefault) {
        if (m_default_btn && m_default_map_panel) {
            m_default_btn->Select(true);
            m_default_map_panel->Show();
        }

        m_manual_btn->Select(false);
        m_manual_map_panel->Hide();

        m_auto_btn->Select(false);
        m_auto_map_panel->Hide();
    }
    if (page == PageType::ptManual) {
        if (m_default_btn && m_default_map_panel) {
            m_default_btn->Select(false);
            m_default_map_panel->Hide();
        }
        m_manual_btn->Select(true);
        m_manual_map_panel->Show();

        m_auto_btn->Select(false);
        m_auto_map_panel->Hide();
    }
    if (page == PageType::ptAuto) {
        if (m_default_btn && m_default_map_panel) {
            m_default_btn->Select(false);
            m_default_map_panel->Hide();
        }
        m_manual_btn->Select(false);
        m_manual_map_panel->Hide();

        m_auto_btn->Select(true);
        m_auto_map_panel->Show();
    }
    Layout();
    Fit();
}

void FilamentMapDialog::on_switch_mode(wxCommandEvent &event)
{
    int      win_id = event.GetId();
    m_page_type  = PageType(win_id);

    update_panel_status(m_page_type);
    event.Skip();
}

void FilamentMapDialog::set_modal_btn_labels(const wxString &ok_label, const wxString &cancel_label)
{
    m_ok_btn->SetLabel(ok_label);
    m_cancel_btn->SetLabel(cancel_label);
}


}} // namespace Slic3r::GUI
