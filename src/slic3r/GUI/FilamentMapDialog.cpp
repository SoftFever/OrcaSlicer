#include "FilamentMapDialog.hpp"
#include "DragDropPanel.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/SwitchButton.hpp"
#include "I18N.hpp"

namespace Slic3r { namespace GUI {

const wxString manual_tips = _L("You can drag the filaments to change which extruder they are assigned to,\n"
                                "and we will slice according to this grouping method.\n"
                                "But your filament arrangement may not be the most filament-efficient.");

const wxString auto_tips = _L("Automatic filament grouping will be performed to reduce flushing consumption\n"
                              "and filament changes during the slicing process.\n"
                              "The recommended results will be displayed below after slicing:");

const wxString auto_tips_with_result = _L("Automatic filament grouping will be performed to reduce flushing consumption\n"
                                          "and filament changes during the slicing process.\n"
                                          "The recommended results are shown below:");

wxColour hex_to_color(const std::string &hex)
{
    if ((hex.length() != 7 && hex.length() != 9) || hex[0] != '#') {
        throw std::invalid_argument("Invalid hex color format");
    }

    unsigned int r, g, b, a = 255;
    std::stringstream ss;

    // r
    ss << std::hex << hex.substr(1, 2);
    ss >> r;
    ss.clear();
    ss.str("");

    // g
    ss << std::hex << hex.substr(3, 2);
    ss >> g;
    ss.clear();
    ss.str("");

    // b
    ss << std::hex << hex.substr(5, 2);
    ss >> b;

    // a
    if (hex.length() == 9) {
        ss.clear();
        ss.str("");
        ss << std::hex << hex.substr(7, 2);
        ss >> a;
    }

    return wxColour(r, g, b, a);
}

FilamentMapDialog::FilamentMapDialog(wxWindow *parent,
    const DynamicPrintConfig *config,
    const std::vector<int> &filament_map,
    const std::vector<int> &extruders,
    bool is_auto,
    bool has_auto_result
)
    : wxDialog(parent, wxID_ANY, _L("Filament arrangement method of plate"), wxDefaultPosition, wxSize(2000, 1500))
    , m_config(config)
    , m_filament_map(filament_map)
    , m_has_auto_result(has_auto_result)
{
    SetBackgroundColour(*wxWHITE);

    SetMinSize(wxSize(500, 100));
    SetMaxSize(wxSize(500, 1500));

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);

    m_mode_switch_btn = new SwitchButton(this);
    m_mode_switch_btn->SetMaxSize({em_unit(this) * 12, -1});
    m_mode_switch_btn->SetLabels(_L("Customize"), _L("Auto"));
    m_mode_switch_btn->Bind(wxEVT_TOGGLEBUTTON, &FilamentMapDialog::on_switch_mode, this);
    m_mode_switch_btn->SetValue(is_auto);
    main_sizer->Add(m_mode_switch_btn, 0, wxCENTER | wxALL, 10);

    m_tip_text = new wxStaticText(this, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    main_sizer->Add(m_tip_text, 0, wxALIGN_LEFT | wxLEFT, 15);

    m_extruder_panel_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_manual_left_panel  = new DragDropPanel(this, wxT("Left nozzle:"), false);
    m_manual_right_panel = new DragDropPanel(this, wxT("Right nozzle:"), false);

    std::vector<std::string> filament_color = config->option<ConfigOptionStrings>("filament_colour")->values;
    for (size_t i = 0; i < filament_map.size(); ++i) {
        auto iter = std::find(extruders.begin(), extruders.end(), i + 1);
        if (iter == extruders.end())
            continue;

        if (filament_map[i] == 1) {
            m_manual_left_panel->AddColorBlock(hex_to_color(filament_color[i]), i + 1);
        }
        else if (filament_map[i] == 2) {
            m_manual_right_panel->AddColorBlock(hex_to_color(filament_color[i]), i + 1);
        }
        else {
            assert(false);
        }
    }

    m_switch_filament_btn = new ScalableButton(this, wxID_ANY, "switch_filament_maps");
    m_switch_filament_btn->Bind(wxEVT_BUTTON, &FilamentMapDialog::on_switch_filaments, this);
    m_switch_filament_btn->SetCanFocus(false);
    // just for placeholder for auto
    m_switch_filament_btn_auto = new ScalableButton(this, wxID_ANY, "switch_filament_maps");
    m_switch_filament_btn_auto->Enable(false);

    m_extruder_panel_sizer->Add(m_manual_left_panel, 1, wxEXPAND | wxALL, 5);
    m_extruder_panel_sizer->Add(m_switch_filament_btn, 0, wxALIGN_CENTER_VERTICAL | wxALL, 1);
    m_extruder_panel_sizer->Add(m_manual_right_panel, 1, wxEXPAND | wxALL, 5);
    m_manual_left_panel->Layout();
    m_manual_left_panel->Fit();
    m_manual_right_panel->Layout();
    m_manual_right_panel->Fit();

    m_auto_left_panel  = new DragDropPanel(this, wxT("Left nozzle:"), true);
    m_auto_right_panel = new DragDropPanel(this, wxT("Right nozzle:"), true);

    for (size_t i = 0; i < filament_map.size(); ++i) {
        auto iter = std::find(extruders.begin(), extruders.end(), i + 1);
        if (iter == extruders.end()) continue;

        if (filament_map[i] == 1) {
            m_auto_left_panel->AddColorBlock(hex_to_color(filament_color[i]), i + 1);
        } else if (filament_map[i] == 2) {
            m_auto_right_panel->AddColorBlock(hex_to_color(filament_color[i]), i + 1);
        } else {
            assert(false);
        }
    }

    m_extruder_panel_sizer->Add(m_auto_left_panel, 1, wxEXPAND | wxALL, 5);
    m_extruder_panel_sizer->Add(m_switch_filament_btn_auto, 0, wxALIGN_CENTER_VERTICAL | wxALL, 1);
    m_extruder_panel_sizer->Add(m_auto_right_panel, 1, wxEXPAND | wxALL, 5);
    m_auto_left_panel->Layout();
    m_auto_left_panel->Fit();
    m_auto_right_panel->Layout();
    m_auto_right_panel->Fit();

    main_sizer->Add(m_extruder_panel_sizer, 1, wxEXPAND | wxALL, 10);

    if (is_auto) {
        m_manual_left_panel->Hide();
        m_manual_right_panel->Hide();
        m_switch_filament_btn->Hide();
        if (m_has_auto_result) {
            m_tip_text->SetLabel(auto_tips_with_result);
        }
        else {
            m_auto_left_panel->Hide();
            m_auto_right_panel->Hide();
            m_switch_filament_btn_auto->Hide();
            m_tip_text->SetLabel(auto_tips);
        }
    }
    else {
        m_auto_left_panel->Hide();
        m_auto_right_panel->Hide();
        m_switch_filament_btn_auto->Hide();
        m_tip_text->SetLabel(manual_tips);
    }

    wxBoxSizer *button_sizer  = new wxBoxSizer(wxHORIZONTAL);
    Button *  ok_btn     = new Button(this, _L("OK"));
    Button *  cancel_btn = new Button(this, _L("Cancel"));
    button_sizer->Add(ok_btn, 0, wxALL, 5);
    button_sizer->Add(cancel_btn, 0, wxALL, 5);
    main_sizer->Add(button_sizer, 0, wxALIGN_CENTER | wxALL, 10);

    ok_btn->Bind(wxEVT_BUTTON, &FilamentMapDialog::on_ok, this);
    cancel_btn->Bind(wxEVT_BUTTON, &FilamentMapDialog::on_cancle, this);

    SetSizer(main_sizer);
    Layout();
    Fit();

    CenterOnParent();
}

bool FilamentMapDialog::is_auto() const
{
    if (m_mode_switch_btn->GetValue()) {
        return true;
    }
    return false;
}

void FilamentMapDialog::on_ok(wxCommandEvent &event)
{
    if (!is_auto()) {
        std::vector<int> left_filaments  = m_manual_left_panel->GetAllFilaments();
        std::vector<int> right_filaments = m_manual_right_panel->GetAllFilaments();

        for (int i = 0; i < m_filament_map.size(); ++i) {
            if (std::find(left_filaments.begin(), left_filaments.end(), i + 1) != left_filaments.end()) {
                m_filament_map[i] = 1;
            } else if (std::find(right_filaments.begin(), right_filaments.end(), i + 1) != right_filaments.end()) {
                m_filament_map[i] = 2;
            }
        }
    }

    EndModal(wxID_OK);
}

void FilamentMapDialog::on_cancle(wxCommandEvent &event)
{
    EndModal(wxID_CANCEL);
}

void FilamentMapDialog::on_switch_mode(wxCommandEvent &event)
{
    bool value = dynamic_cast<SwitchButton *>(event.GetEventObject())->GetValue();
    if (value) { // auto
        m_manual_left_panel->Hide();
        m_manual_right_panel->Hide();
        m_switch_filament_btn->Hide();
        if (m_has_auto_result) {
            m_auto_left_panel->Show();
            m_auto_right_panel->Show();
            m_switch_filament_btn_auto->Show();
            m_tip_text->SetLabel(auto_tips_with_result);
        }
        else {
            m_auto_left_panel->Hide();
            m_auto_right_panel->Hide();
            m_switch_filament_btn_auto->Hide();
            m_tip_text->SetLabel(auto_tips);
        }
    } else { // manual
        m_manual_left_panel->Show();
        m_manual_right_panel->Show();
        m_switch_filament_btn->Show();

        m_auto_left_panel->Hide();
        m_auto_right_panel->Hide();
        m_switch_filament_btn_auto->Hide();

        m_tip_text->SetLabel(manual_tips);
    }
    Layout();
    Fit();
    event.Skip();
}

void FilamentMapDialog::on_switch_filaments(wxCommandEvent &event)
{
    std::vector<ColorPanel *> left_blocks  = m_manual_left_panel->get_filament_blocks();
    std::vector<ColorPanel *> right_blocks = m_manual_right_panel->get_filament_blocks();

    for (ColorPanel* block : left_blocks) {
        m_manual_right_panel->AddColorBlock(block->GetColor(), block->GetFilamentId(), false);
        m_manual_left_panel->RemoveColorBlock(block, false);
    }
    for (auto block : right_blocks) {
        m_manual_left_panel->AddColorBlock(block->GetColor(), block->GetFilamentId(), false);
        m_manual_right_panel->RemoveColorBlock(block, false);
    }
    Layout();
    Fit();
}

}} // namespace Slic3r::GUI
