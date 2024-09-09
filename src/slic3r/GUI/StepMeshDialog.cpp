#include "StepMeshDialog.hpp"
#include "BBLStatusBar.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "Widgets/Button.hpp"
#include "MainFrame.hpp"
#include <wx/sizer.h>
#include <wx/slider.h>

using namespace Slic3r;
using namespace Slic3r::GUI;

static int _scale(const int val) { return val * Slic3r::GUI::wxGetApp().em_unit() / 10; }
static int _ITEM_WIDTH() { return _scale(30); }
#define MIN_DIALOG_WIDTH        FromDIP(400)
#define SLIDER_WIDTH            FromDIP(150)
#define TEXT_CTRL_WIDTH         FromDIP(40)
#define BUTTON_SIZE             wxSize(FromDIP(58), FromDIP(24))
#define BUTTON_BORDER           FromDIP(int(400 - 58 * 2) / 8)
#define SLIDER_SCALE(val)       ((val) / 0.001)
#define SLIDER_UNSCALE(val)     ((val) * 0.001)
#define SLIDER_SCALE_10(val)    ((val) / 0.01)
#define SLIDER_UNSCALE_10(val)  ((val) * 0.01)
#define LEFT_RIGHT_PADING       FromDIP(20)

void StepMeshDialog::on_dpi_changed(const wxRect& suggested_rect) {
};

bool StepMeshDialog:: validate_number_range(const wxString& value, double min, double max) {
    double num;
    if (!value.ToDouble(&num)) {
        return false;
    }
    return (num >= min && num <= max);
}

StepMeshDialog::StepMeshDialog(wxWindow* parent, fs::path file)
    : DPIDialog(parent ? parent : static_cast<wxWindow *>(wxGetApp().mainframe),
                wxID_ANY,
                _(L("Step file import parameters")),
                wxDefaultPosition,
                wxDefaultSize,
                wxDEFAULT_DIALOG_STYLE /* | wxRESIZE_BORDER*/), m_file(file)
{
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico")
                             % Slic3r::resources_dir()).str();
    SetIcon(wxIcon(Slic3r::encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);

    wxBoxSizer* bSizer = new wxBoxSizer(wxVERTICAL);
    bSizer->SetMinSize(wxSize(MIN_DIALOG_WIDTH, -1));

    wxBoxSizer* linear_sizer = new wxBoxSizer(wxHORIZONTAL);
    //linear_sizer->SetMinSize(wxSize(MIN_DIALOG_WIDTH, -1));
    wxStaticText* linear_title = new wxStaticText(this,
                                                  wxID_ANY,
                                                  _L("Linear Deflection:"));
    linear_sizer->Add(linear_title, 0, wxALIGN_LEFT);
    linear_sizer->AddStretchSpacer(1);
    wxSlider* linear_slider = new wxSlider(this, wxID_ANY,
                                           SLIDER_SCALE(get_linear_defletion()),
                                           1, 100, wxDefaultPosition,
                                           wxSize(SLIDER_WIDTH, -1),
                                           wxSL_HORIZONTAL);
    linear_sizer->Add(linear_slider, 0, wxALIGN_RIGHT | wxLEFT, FromDIP(5));
    wxTextValidator valid_number(wxFILTER_NUMERIC);
    wxTextCtrl* linear_textctrl = new wxTextCtrl(this, wxID_ANY,
                                                 m_linear_last,
                                                 wxDefaultPosition, wxSize(TEXT_CTRL_WIDTH, -1),
                                                 0, valid_number);
    linear_sizer->Add(linear_textctrl, 0, wxALIGN_RIGHT | wxLEFT, FromDIP(5));
    // textctrl loss focus
    linear_textctrl->Bind(wxEVT_KILL_FOCUS, ([this, linear_textctrl](wxFocusEvent& e) {
        wxString value = linear_textctrl->GetValue();
        if(!validate_number_range(value, 0.001, 0.1)) {
            linear_textctrl->SetValue(m_linear_last);
        }
        m_linear_last = value;
        update_mesh_number_text();
        e.Skip();
    }));
    // slider bind textctrl
    linear_slider->Bind(wxEVT_SLIDER, ([this, linear_slider, linear_textctrl](wxCommandEvent& e) {
        double slider_value = SLIDER_UNSCALE(linear_slider->GetValue());
        linear_textctrl->SetValue(wxString::Format("%.3f", slider_value));
        m_linear_last = wxString::Format("%.3f", slider_value);
        update_mesh_number_text();
    }));
    // textctrl bind slider
    linear_textctrl->Bind(wxEVT_TEXT, ([this, linear_textctrl, linear_slider](wxCommandEvent& e) {
        double slider_value_long;
        int slider_value;
        wxString value = linear_textctrl->GetValue();
        if (value.ToDouble(&slider_value_long)) {
            slider_value = SLIDER_SCALE(slider_value_long);
            if (slider_value >= linear_slider->GetMin() && slider_value <= linear_slider->GetMax()) {
                linear_slider->SetValue(slider_value);
            }
        }
    }));

    bSizer->Add(linear_sizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, LEFT_RIGHT_PADING);

    wxBoxSizer* angle_sizer = new wxBoxSizer(wxHORIZONTAL);
    wxStaticText* angle_title = new wxStaticText(this,
                                                  wxID_ANY,
                                                  _L("Angle Deflection:"));
    angle_sizer->Add(angle_title, 0, wxALIGN_LEFT);
    angle_sizer->AddStretchSpacer(1);
    wxSlider* angle_slider = new wxSlider(this, wxID_ANY,
                                           SLIDER_SCALE_10(get_angle_defletion()),
                                           1, 100, wxDefaultPosition,
                                           wxSize(SLIDER_WIDTH, -1),
                                           wxSL_HORIZONTAL);
    angle_sizer->Add(angle_slider, 0, wxALIGN_RIGHT | wxLEFT, FromDIP(5));
    wxTextCtrl* angle_textctrl = new wxTextCtrl(this, wxID_ANY,
                                                 m_angle_last,
                                                 wxDefaultPosition, wxSize(TEXT_CTRL_WIDTH, -1),
                                                 0, valid_number);
    angle_sizer->Add(angle_textctrl, 0, wxALIGN_RIGHT | wxLEFT, FromDIP(5));
    // textctrl loss focus
    angle_textctrl->Bind(wxEVT_KILL_FOCUS, ([this, angle_textctrl](wxFocusEvent& e) {
        wxString value = angle_textctrl->GetValue();
        if (!validate_number_range(value, 0.01, 1)) {
            angle_textctrl->SetValue(m_angle_last);
        }
        m_angle_last = value;
        update_mesh_number_text();
        e.Skip();
    }));
    // slider bind textctrl
    angle_slider->Bind(wxEVT_SLIDER, ([this, angle_slider, angle_textctrl](wxCommandEvent& e) {
        double slider_value = SLIDER_UNSCALE_10(angle_slider->GetValue());
        angle_textctrl->SetValue(wxString::Format("%.2f", slider_value));
        m_angle_last = wxString::Format("%.2f", slider_value);
        update_mesh_number_text();
    }));
    // textctrl bind slider
    linear_textctrl->Bind(wxEVT_TEXT, ([this, angle_slider, angle_textctrl](wxCommandEvent& e) {
        double slider_value_long;
        int slider_value;
        wxString value = angle_textctrl->GetValue();
        if (value.ToDouble(&slider_value_long)) {
            slider_value = SLIDER_SCALE_10(slider_value_long);
            if (slider_value >= angle_slider->GetMin() && slider_value <= angle_slider->GetMax()) {
                angle_slider->SetValue(slider_value);
            }
        }
    }));

    bSizer->Add(angle_sizer, 1, wxEXPAND | wxLEFT | wxRIGHT, LEFT_RIGHT_PADING);


    mesh_face_number_text = new wxStaticText(this, wxID_ANY, _L("Number of generated surfaces: 0"));
    bSizer->Add(mesh_face_number_text, 1, wxEXPAND | wxLEFT | wxRIGHT, LEFT_RIGHT_PADING);

    wxBoxSizer* bSizer_button = new wxBoxSizer(wxHORIZONTAL);
    bSizer_button->SetMinSize(wxSize(FromDIP(100), -1));

    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(27, 136, 68), StateColor::Pressed), std::pair<wxColour, int>(wxColour(61, 203, 115), StateColor::Hovered),
                            std::pair<wxColour, int>(AMS_CONTROL_BRAND_COLOUR, StateColor::Normal));
    m_button_ok = new Button(this, _L("OK"));
    m_button_ok->SetBackgroundColor(btn_bg_green);
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour(0xFFFFFE));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(BUTTON_SIZE);
    m_button_ok->SetMinSize(BUTTON_SIZE);
    m_button_ok->SetCornerRadius(FromDIP(12));
    bSizer_button->Add(m_button_ok, 0, wxALIGN_RIGHT, BUTTON_BORDER);

    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { EndModal(wxID_OK); });

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(wxColour(38, 46, 48));
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(BUTTON_SIZE);
    m_button_cancel->SetMinSize(BUTTON_SIZE);
    m_button_cancel->SetCornerRadius(FromDIP(12));
    bSizer_button->Add(m_button_cancel, 0, wxALIGN_RIGHT | wxLEFT, BUTTON_BORDER);

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) { EndModal(wxID_CANCEL); });

    bSizer->Add(bSizer_button, 0, wxALIGN_RIGHT | wxRIGHT| wxBOTTOM, LEFT_RIGHT_PADING);

    this->SetSizer(bSizer);
    update_mesh_number_text();
    this->Layout();
    bSizer->Fit(this);

    this->Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) { });

    wxGetApp().UpdateDlgDarkUI(this);
}

long StepMeshDialog::get_mesh_number()
{
    Model model;
    long number = 0;
    const std::string file_path = m_file.string();
    bool is_cb_cancel = false;
    bool result = load_step(file_path.c_str(), &model, is_cb_cancel, get_linear_defletion(), get_angle_defletion(), nullptr, nullptr, number);
    return number;
}

void StepMeshDialog::update_mesh_number_text()
{
    long number = get_mesh_number();
    wxString newText = wxString::Format("Number of generated surfaces: %d", number);
    mesh_face_number_text->SetLabel(newText);
}