#include "FilamentMapDialog.hpp"
#include "DragDropPanel.hpp"
#include "Widgets/Button.hpp"
#include "I18N.hpp"

namespace Slic3r { namespace GUI {

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

FilamentMapDialog::FilamentMapDialog(wxWindow *parent, const DynamicPrintConfig *config, const std::vector<int> &filament_map, bool is_auto)
    : wxDialog(parent, wxID_ANY, _L("Filament arrangement method of plate"), wxDefaultPosition, wxSize(2000, 1500))
    , m_config(config)
    , m_filament_map(filament_map)
{
    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);

    wxStaticBoxSizer *mode_sizer = new wxStaticBoxSizer(wxHORIZONTAL, this, _L("Mode"));
    m_auto_radio             = new wxRadioButton(this, wxID_ANY, _L("Auto"));
    m_manual_radio               = new wxRadioButton(this, wxID_ANY, _L("Customize"));

    if (is_auto)
        m_auto_radio->SetValue(true);
    else
        m_manual_radio->SetValue(true);

    mode_sizer->Add(m_auto_radio, 1, wxALL, 5);
    mode_sizer->Add(m_manual_radio, 1, wxALL, 5);
    main_sizer->Add(mode_sizer, 0, wxEXPAND | wxALL, 10);

    wxStaticText *tip_text = new wxStaticText(this, wxID_ANY, _L("You could arrange your filament like this, this is the best solution we calculated"));
    main_sizer->Add(tip_text, 0, wxALIGN_CENTER | wxALL, 5);

    wxBoxSizer *panel_sizer = new wxBoxSizer(wxHORIZONTAL);

    m_left_panel  = new DragDropPanel(this, wxT("Left nozzle:"));
    m_right_panel = new DragDropPanel(this, wxT("Right nozzle:"));

    std::vector<std::string> filament_color = config->option<ConfigOptionStrings>("filament_colour")->values;
    for (size_t i = 0; i < filament_map.size(); ++i) {
        if (filament_map[i] == 1) {
            m_left_panel->AddColorBlock(hex_to_color(filament_color[i]), i + 1);
        }
        else if (filament_map[i] == 2) {
            m_right_panel->AddColorBlock(hex_to_color(filament_color[i]), i + 1);
        }
        else {
            assert(false);
        }
    }

    panel_sizer->Add(m_left_panel, 1, wxEXPAND | wxALL, 5);
    panel_sizer->Add(m_right_panel, 1, wxEXPAND | wxALL, 5);
    m_left_panel->Layout();
    m_left_panel->Fit();
    m_right_panel->Layout();
    m_right_panel->Fit();
    main_sizer->Add(panel_sizer, 1, wxEXPAND | wxALL, 10);

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
    if (m_auto_radio->GetValue()) {
        return true;
    }
    return false;
}

void FilamentMapDialog::on_ok(wxCommandEvent &event)
{
    std::vector<int> left_filaments = m_left_panel->GetAllFilaments();
    std::vector<int> right_filaments = m_right_panel->GetAllFilaments();

    for (int i = 0; i < m_filament_map.size(); ++i) {
        if (std::find(left_filaments.begin(), left_filaments.end(), i + 1) != left_filaments.end()) {
            m_filament_map[i] = 1;
        }
        else if (std::find(right_filaments.begin(), right_filaments.end(), i + 1) != right_filaments.end()) {
            m_filament_map[i] = 2;
        }
    }

    EndModal(wxID_OK);
}

void FilamentMapDialog::on_cancle(wxCommandEvent &event)
{
    EndModal(wxID_CANCEL);
}

}} // namespace Slic3r::GUI
