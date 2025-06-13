#include "calib_dlg.hpp"
#include "GUI_App.hpp"
#include "MsgDialog.hpp"
#include "I18N.hpp"
#include <wx/dcgraph.h>
#include "MainFrame.hpp"
#include "Widgets/DialogButtons.hpp"
#include <string>

namespace Slic3r { namespace GUI {

namespace {

void ParseStringValues(std::string str, std::vector<double> &vec)
{
    vec.clear();
    std::replace(str.begin(), str.end(), ',', ' ');
    std::istringstream inss(str);
    std::copy_if(std::istream_iterator<int>(inss), std::istream_iterator<int>(), std::back_inserter(vec),
                 [](int x){ return x > 0; });
}

int GetTextMax(wxWindow* parent, const std::vector<wxString>& labels)
{
    wxSize text_size;
    for (wxString label : labels)
        text_size.IncTo(parent->GetTextExtent(label));
    return text_size.x + parent->FromDIP(10);
}

}

PA_Calibration_Dlg::PA_Calibration_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("PA Calibration"), wxDefaultPosition, parent->FromDIP(wxSize(-1, 280)), wxDEFAULT_DIALOG_STYLE), m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);

    // Extruder type Radio Group
    auto labeled_box_type = new LabeledStaticBox(this, _L("Extruder type"));
    auto type_box = new wxStaticBoxSizer(labeled_box_type, wxHORIZONTAL);

    m_rbExtruderType = new RadioGroup(this, {_L("DDE"), _L("Bowden")}, wxHORIZONTAL);
    type_box->Add(m_rbExtruderType, 0, wxALL | wxEXPAND, FromDIP(4));
    v_sizer->Add(type_box, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Method Radio Group
    auto labeled_box_method = new LabeledStaticBox(this, _L("Method"));
    auto method_box = new wxStaticBoxSizer(labeled_box_method, wxHORIZONTAL);

	m_rbMethod = new RadioGroup(this, { _L("PA Tower"), _L("PA Line"), _L("PA Pattern") }, wxHORIZONTAL);
    method_box->Add(m_rbMethod, 0, wxALL | wxEXPAND, FromDIP(4));
    v_sizer->Add(method_box, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Settings
    wxString start_pa_str    = _L("Start PA: ");
    wxString end_pa_str      = _L("End PA: ");
    wxString PA_step_str     = _L("PA step: ");
    wxString sp_accel_str    = _L("Accelerations: ");
    wxString sp_speed_str    = _L("Speeds: ");
    wxString cb_print_no_str = _L("Print numbers");

    int text_max = GetTextMax(this, std::vector<wxString>{start_pa_str, end_pa_str, PA_step_str, sp_accel_str, sp_speed_str, cb_print_no_str});

    auto st_size = FromDIP(wxSize(text_max, -1));
    auto ti_size = FromDIP(wxSize(120, -1));

    LabeledStaticBox* stb = new LabeledStaticBox(this, _L("Settings"));
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    settings_sizer->AddSpacer(FromDIP(5));

    // start PA
    auto start_PA_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_pa_text = new wxStaticText(this, wxID_ANY, start_pa_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStartPA = new TextInput(this, "", "", "", wxDefaultPosition, ti_size, wxTE_PROCESS_ENTER);
    m_tiStartPA->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
	start_PA_sizer->Add(start_pa_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    start_PA_sizer->Add(m_tiStartPA  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(start_PA_sizer, 0, wxLEFT, FromDIP(3));

    // end PA
    auto end_PA_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto end_pa_text = new wxStaticText(this, wxID_ANY, end_pa_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiEndPA = new TextInput(this, "", "", "", wxDefaultPosition, ti_size, wxTE_PROCESS_ENTER);
    m_tiStartPA->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    end_PA_sizer->Add(end_pa_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    end_PA_sizer->Add(m_tiEndPA  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(end_PA_sizer, 0, wxLEFT, FromDIP(3));

    // PA step
    auto PA_step_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto PA_step_text = new wxStaticText(this, wxID_ANY, PA_step_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiPAStep = new TextInput(this, "", "", "", wxDefaultPosition, ti_size, wxTE_PROCESS_ENTER);
    m_tiStartPA->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    PA_step_sizer->Add(PA_step_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    PA_step_sizer->Add(m_tiPAStep  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(PA_step_sizer, 0, wxLEFT, FromDIP(3));

    // Print Numbers
    wxBoxSizer* cb_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto cb_title = new wxStaticText(this, wxID_ANY, cb_print_no_str, wxDefaultPosition, st_size, 0);
    m_cbPrintNum = new CheckBox(this);
    m_cbPrintNum->SetValue(false);
    m_cbPrintNum->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& e) {
        (m_params.print_numbers) = (m_params.print_numbers) ? false : true;
        e.Skip();
    });
    cb_sizer->Add(cb_title      , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    cb_sizer->Add(m_cbPrintNum  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(cb_sizer, 0, wxLEFT | wxTOP | wxBOTTOM, FromDIP(3));

    wxTextValidator val_list_validator(wxFILTER_INCLUDE_CHAR_LIST);
    val_list_validator.SetCharIncludes(wxString("0123456789,"));

    auto sp_accel_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto sp_accel_text = new wxStaticText(this, wxID_ANY, sp_accel_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiBMAccels = new TextInput(this, "", "", "", wxDefaultPosition, ti_size, wxTE_PROCESS_ENTER);
    m_tiBMAccels->SetToolTip(_L("Comma-separated list of printing accelerations"));
    m_tiBMAccels->GetTextCtrl()->SetValidator(val_list_validator);
    sp_accel_sizer->Add(sp_accel_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    sp_accel_sizer->Add(m_tiBMAccels , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(sp_accel_sizer, 0, wxLEFT, FromDIP(3));

    auto sp_speed_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto sp_speed_text = new wxStaticText(this, wxID_ANY, sp_speed_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiBMSpeeds = new TextInput(this, "", "", "", wxDefaultPosition, ti_size, wxTE_PROCESS_ENTER);
    m_tiBMSpeeds->SetToolTip(_L("Comma-separated list of printing speeds"));
    m_tiBMSpeeds->GetTextCtrl()->SetValidator(val_list_validator);
    sp_speed_sizer->Add(sp_speed_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    sp_speed_sizer->Add(m_tiBMSpeeds , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(sp_speed_sizer, 0, wxLEFT, FromDIP(3));

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});
    v_sizer->Add(dlg_btns , 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &PA_Calibration_Dlg::on_start, this);

    PA_Calibration_Dlg::reset_params();

    // Connect Events
    m_rbExtruderType->Connect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(PA_Calibration_Dlg::on_extruder_type_changed), NULL, this);
    m_rbMethod->Connect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(PA_Calibration_Dlg::on_method_changed), NULL, this);
    this->Connect(wxEVT_SHOW, wxShowEventHandler(PA_Calibration_Dlg::on_show));
    
    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
}

PA_Calibration_Dlg::~PA_Calibration_Dlg() {
    // Disconnect Events
    m_rbExtruderType->Disconnect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(PA_Calibration_Dlg::on_extruder_type_changed), NULL, this);
    m_rbMethod->Disconnect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(PA_Calibration_Dlg::on_method_changed), NULL, this);
}

void PA_Calibration_Dlg::reset_params() {
    bool isDDE = m_rbExtruderType->GetSelection() == 0 ? true : false;
    int method = m_rbMethod->GetSelection();

    m_tiStartPA->GetTextCtrl()->SetValue(wxString::FromDouble(0.0));

    switch (method) {
        case 1:
            m_params.mode = CalibMode::Calib_PA_Line;
            m_tiEndPA->GetTextCtrl()->SetValue(wxString::FromDouble(0.1));
            m_tiPAStep->GetTextCtrl()->SetValue(wxString::FromDouble(0.002));
            m_cbPrintNum->SetValue(true);
            m_cbPrintNum->Enable(true);
            m_tiBMAccels->Enable(false);
            m_tiBMSpeeds->Enable(false);
            break;
        case 2:
            m_params.mode = CalibMode::Calib_PA_Pattern;
            m_tiEndPA->GetTextCtrl()->SetValue(wxString::FromDouble(0.08));
            m_tiPAStep->GetTextCtrl()->SetValue(wxString::FromDouble(0.005));
            m_cbPrintNum->SetValue(true);
            m_cbPrintNum->Enable(false);
            m_tiBMAccels->Enable(true);
            m_tiBMSpeeds->Enable(true);
            break;
        default:
            m_params.mode = CalibMode::Calib_PA_Tower;
            m_tiEndPA->GetTextCtrl()->SetValue(wxString::FromDouble(0.1));
            m_tiPAStep->GetTextCtrl()->SetValue(wxString::FromDouble(0.002));
            m_cbPrintNum->SetValue(false);
            m_cbPrintNum->Enable(false);
            m_tiBMAccels->Enable(false);
            m_tiBMSpeeds->Enable(false);
            break;
    }

    if (!isDDE) {
        m_tiEndPA->GetTextCtrl()->SetValue(wxString::FromDouble(1.0));
        
        if (m_params.mode == CalibMode::Calib_PA_Pattern) {
            m_tiPAStep->GetTextCtrl()->SetValue(wxString::FromDouble(0.05));
        } else {
            m_tiPAStep->GetTextCtrl()->SetValue(wxString::FromDouble(0.02));
        }
    }
}

void PA_Calibration_Dlg::on_start(wxCommandEvent& event) { 
    bool read_double = false;
    read_double = m_tiStartPA->GetTextCtrl()->GetValue().ToDouble(&m_params.start);
    read_double = read_double && m_tiEndPA->GetTextCtrl()->GetValue().ToDouble(&m_params.end);
    read_double = read_double && m_tiPAStep->GetTextCtrl()->GetValue().ToDouble(&m_params.step);
    if (!read_double || m_params.start < 0 || m_params.step < EPSILON || m_params.end < m_params.start + m_params.step) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nStart PA: >= 0.0\nEnd PA: > Start PA\nPA step: >= 0.001)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    switch (m_rbMethod->GetSelection()) {
        case 1:
            m_params.mode = CalibMode::Calib_PA_Line;
            break;
        case 2:
            m_params.mode = CalibMode::Calib_PA_Pattern;
            break;
        default:
            m_params.mode = CalibMode::Calib_PA_Tower;
    }

    m_params.print_numbers = m_cbPrintNum->GetValue();
    ParseStringValues(m_tiBMAccels->GetTextCtrl()->GetValue().ToStdString(), m_params.accelerations);
    ParseStringValues(m_tiBMSpeeds->GetTextCtrl()->GetValue().ToStdString(), m_params.speeds);

    m_plater->calib_pa(m_params);
    EndModal(wxID_OK);

}
void PA_Calibration_Dlg::on_extruder_type_changed(wxCommandEvent& event) { 
    PA_Calibration_Dlg::reset_params();
    event.Skip(); 
}
void PA_Calibration_Dlg::on_method_changed(wxCommandEvent& event) { 
    PA_Calibration_Dlg::reset_params();
    event.Skip(); 
}

void PA_Calibration_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh(); 
    Fit();
}

void PA_Calibration_Dlg::on_show(wxShowEvent& event) {
    PA_Calibration_Dlg::reset_params();
}

// Temp calib dlg
//
enum FILAMENT_TYPE : int
{
    tPLA = 0,
    tABS_ASA,
    tPETG,
    tPCTG,
    tTPU,
    tPA_CF,
    tPET_CF,
    tCustom
};

Temp_Calibration_Dlg::Temp_Calibration_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("Temperature calibration"), wxDefaultPosition, parent->FromDIP(wxSize(-1, 280)), wxDEFAULT_DIALOG_STYLE), m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);

    // Method Radio Group
    auto labeled_box_method = new LabeledStaticBox(this, _L("Filament type"));
    auto method_box = new wxStaticBoxSizer(labeled_box_method, wxHORIZONTAL);

	m_rbFilamentType = new RadioGroup(this, { _L("PLA"), _L("ABS/ASA"), _L("PETG"), _L("PCTG"), _L("TPU"), _L("PA-CF"), _L("PET-CF"), _L("Custom") }, wxVERTICAL, 2);
    method_box->Add(m_rbFilamentType, 0, wxALL | wxEXPAND, FromDIP(4));
    v_sizer->Add(method_box, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Settings
    wxString start_temp_str = _L("Start temp: ");
    wxString end_temp_str   = _L("End temp: ");
    wxString temp_step_str  = _L("Temp step: ");
    int text_max = GetTextMax(this, std::vector<wxString>{start_temp_str, end_temp_str, temp_step_str});

    auto st_size = FromDIP(wxSize(text_max, -1));
    auto ti_size = FromDIP(wxSize(120, -1));

    LabeledStaticBox* stb = new LabeledStaticBox(this, _L("Settings"));
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    settings_sizer->AddSpacer(FromDIP(5));

    // start temp
    auto start_temp_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_temp_text = new wxStaticText(this, wxID_ANY, start_temp_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStart = new TextInput(this, std::to_string(230), _L("\u2103"), "", wxDefaultPosition, ti_size);
    m_tiStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    start_temp_sizer->Add(start_temp_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    start_temp_sizer->Add(m_tiStart      , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(start_temp_sizer, 0, wxLEFT, FromDIP(3));

    // end temp
    auto end_temp_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto end_temp_text = new wxStaticText(this, wxID_ANY, end_temp_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiEnd = new TextInput(this, std::to_string(190), _L("\u2103"), "", wxDefaultPosition, ti_size);
    m_tiStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    end_temp_sizer->Add(end_temp_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    end_temp_sizer->Add(m_tiEnd      , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(end_temp_sizer, 0, wxLEFT, FromDIP(3));

    // temp step
    auto temp_step_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto temp_step_text = new wxStaticText(this, wxID_ANY, temp_step_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStep = new TextInput(this, wxString::FromDouble(5),_L("\u2103"), "", wxDefaultPosition, ti_size);
    m_tiStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_tiStep->Enable(false);
    temp_step_sizer->Add(temp_step_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    temp_step_sizer->Add(m_tiStep      , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(temp_step_sizer, 0, wxLEFT, FromDIP(3));

    settings_sizer->AddSpacer(FromDIP(5));

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});
    v_sizer->Add(dlg_btns , 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &Temp_Calibration_Dlg::on_start, this);

    m_rbFilamentType->Connect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(Temp_Calibration_Dlg::on_filament_type_changed), NULL, this);

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();

    auto validate_text = [this](TextInput* ti){
        unsigned long t = 0;
        if(!ti->GetTextCtrl()->GetValue().ToULong(&t))
            return;
        if(t> 350 || t < 170){
            MessageDialog msg_dlg(nullptr, wxString::Format(L"Supported range: 170%s - 350%s",_L("\u2103"),_L("\u2103")), wxEmptyString, wxICON_WARNING | wxOK);
            msg_dlg.ShowModal();
            if(t > 350)
                t = 350;
            else
                t = 170;
        }
        t = (t / 5) * 5;
        ti->GetTextCtrl()->SetValue(std::to_string(t));
    };

    m_tiStart->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [&](wxFocusEvent &e) {
        validate_text(this->m_tiStart);
        e.Skip();
        });

    m_tiEnd->GetTextCtrl()->Bind(wxEVT_KILL_FOCUS, [&](wxFocusEvent &e) {
        validate_text(this->m_tiEnd);
        e.Skip();
        });

    
}

Temp_Calibration_Dlg::~Temp_Calibration_Dlg() {
    // Disconnect Events
    m_rbFilamentType->Disconnect(wxEVT_COMMAND_RADIOBOX_SELECTED, wxCommandEventHandler(Temp_Calibration_Dlg::on_filament_type_changed), NULL, this);
}

void Temp_Calibration_Dlg::on_start(wxCommandEvent& event) {
    bool read_long = false;
    unsigned long start=0,end=0;
    read_long = m_tiStart->GetTextCtrl()->GetValue().ToULong(&start);
    read_long = read_long && m_tiEnd->GetTextCtrl()->GetValue().ToULong(&end);

    if (!read_long || start > 350 || end < 170  || end > (start - 5)) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nStart temp: <= 350\nEnd temp: >= 170\nStart temp > End temp + 5)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }
    m_params.start = start;
    m_params.end = end;
    m_params.mode =CalibMode::Calib_Temp_Tower;
    m_plater->calib_temp(m_params);
    EndModal(wxID_OK);

}

void Temp_Calibration_Dlg::on_filament_type_changed(wxCommandEvent& event) {
    int selection = event.GetSelection();
    unsigned long start,end;
    switch(selection)
    {
        case tABS_ASA:
            start = 270;
            end = 230;
            break;
        case tPETG:
            start = 250;
            end = 230;
            break;
	case tPCTG:
            start = 280;
            end = 240;
            break;
        case tTPU:
            start = 240;
            end = 210;
            break;
        case tPA_CF:
            start = 320;
            end = 280;
            break;
        case tPET_CF:
            start = 320;
            end = 280;
            break;
        case tPLA:
        case tCustom:
            start = 230;
            end = 190;
            break;
    }
    
    m_tiEnd->GetTextCtrl()->SetValue(std::to_string(end));
    m_tiStart->GetTextCtrl()->SetValue(std::to_string(start));
    event.Skip();
}

void Temp_Calibration_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh();
    Fit();

}


// MaxVolumetricSpeed_Test_Dlg
//

MaxVolumetricSpeed_Test_Dlg::MaxVolumetricSpeed_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("Max volumetric speed test"), wxDefaultPosition, parent->FromDIP(wxSize(-1, 280)), wxDEFAULT_DIALOG_STYLE), m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);

    // Settings
    wxString start_vol_str = _L("Start volumetric speed: ");
    wxString end_vol_str   = _L("End volumetric speed: ");
    wxString vol_step_str  = _L("Step") + ": ";
    int text_max = GetTextMax(this, std::vector<wxString>{start_vol_str, end_vol_str, vol_step_str});

    auto st_size = FromDIP(wxSize(text_max, -1));
    auto ti_size = FromDIP(wxSize(120, -1));

    LabeledStaticBox* stb = new LabeledStaticBox(this, _L("Settings"));
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    settings_sizer->AddSpacer(FromDIP(5));

    // start vol
    auto start_vol_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_vol_text = new wxStaticText(this, wxID_ANY, start_vol_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStart = new TextInput(this, std::to_string(5), _L("mm³/s"), "", wxDefaultPosition, ti_size);
    m_tiStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));

    start_vol_sizer->Add(start_vol_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    start_vol_sizer->Add(m_tiStart     , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(start_vol_sizer, 0, wxLEFT, FromDIP(3));

    // end vol
    auto end_vol_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto end_vol_text = new wxStaticText(this, wxID_ANY, end_vol_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiEnd = new TextInput(this, std::to_string(20), _L("mm³/s"), "", wxDefaultPosition, ti_size);
    m_tiStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    end_vol_sizer->Add(end_vol_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    end_vol_sizer->Add(m_tiEnd     , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(end_vol_sizer, 0, wxLEFT, FromDIP(3));

    // vol step
    auto vol_step_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto vol_step_text = new wxStaticText(this, wxID_ANY, vol_step_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStep = new TextInput(this, wxString::FromDouble(0.5), _L("mm³/s"), "", wxDefaultPosition, ti_size);
    m_tiStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    vol_step_sizer->Add(vol_step_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    vol_step_sizer->Add(m_tiStep     , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(vol_step_sizer, 0, wxLEFT, FromDIP(3));

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});
    v_sizer->Add(dlg_btns , 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &MaxVolumetricSpeed_Test_Dlg::on_start, this);

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
}

MaxVolumetricSpeed_Test_Dlg::~MaxVolumetricSpeed_Test_Dlg() {
    // Disconnect Events
}

void MaxVolumetricSpeed_Test_Dlg::on_start(wxCommandEvent& event) {
    bool read_double = false;
    read_double = m_tiStart->GetTextCtrl()->GetValue().ToDouble(&m_params.start);
    read_double = read_double && m_tiEnd->GetTextCtrl()->GetValue().ToDouble(&m_params.end);
    read_double = read_double && m_tiStep->GetTextCtrl()->GetValue().ToDouble(&m_params.step);

    if (!read_double || m_params.start <= 0 || m_params.step <= 0 || m_params.end < (m_params.start + m_params.step)) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nstart > 0 \nstep >= 0\nend > start + step)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    m_params.mode = CalibMode::Calib_Vol_speed_Tower;
    m_plater->calib_max_vol_speed(m_params);
    EndModal(wxID_OK);

}

void MaxVolumetricSpeed_Test_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh();
    Fit();

}


// VFA_Test_Dlg
//

VFA_Test_Dlg::VFA_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("VFA test"), wxDefaultPosition, parent->FromDIP(wxSize(-1, 280)), wxDEFAULT_DIALOG_STYLE)
    , m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);

    // Settings
    wxString start_str    = _L("Start speed: ");
    wxString end_vol_str  = _L("End speed: ");
    wxString vol_step_str = _L("Step") + ": ";
    int text_max = GetTextMax(this, std::vector<wxString>{start_str, end_vol_str, vol_step_str});

    auto st_size = FromDIP(wxSize(text_max, -1));
    auto ti_size = FromDIP(wxSize(120, -1));

    LabeledStaticBox* stb = new LabeledStaticBox(this, _L("Settings"));
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    settings_sizer->AddSpacer(FromDIP(5));

    // start vol
    auto start_vol_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_vol_text = new wxStaticText(this, wxID_ANY, start_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStart = new TextInput(this, std::to_string(40), _L("mm/s"), "", wxDefaultPosition, ti_size);
    m_tiStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));

    start_vol_sizer->Add(start_vol_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    start_vol_sizer->Add(m_tiStart     , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(start_vol_sizer, 0, wxLEFT, FromDIP(3));

    // end vol
    auto end_vol_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto end_vol_text = new wxStaticText(this, wxID_ANY, end_vol_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiEnd = new TextInput(this, std::to_string(200), _L("mm/s"), "", wxDefaultPosition, ti_size);
    m_tiStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    end_vol_sizer->Add(end_vol_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    end_vol_sizer->Add(m_tiEnd     , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(end_vol_sizer, 0, wxLEFT, FromDIP(3));

    // vol step
    auto vol_step_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto vol_step_text = new wxStaticText(this, wxID_ANY, vol_step_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStep = new TextInput(this, wxString::FromDouble(10), _L("mm/s"), "", wxDefaultPosition, ti_size);
    m_tiStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    vol_step_sizer->Add(vol_step_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    vol_step_sizer->Add(m_tiStep     , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(vol_step_sizer, 0, wxLEFT, FromDIP(3));

    settings_sizer->AddSpacer(FromDIP(5));

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});
    v_sizer->Add(dlg_btns , 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &VFA_Test_Dlg::on_start, this);

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
}

VFA_Test_Dlg::~VFA_Test_Dlg()
{
    // Disconnect Events
}

void VFA_Test_Dlg::on_start(wxCommandEvent& event)
{
    bool read_double = false;
    read_double = m_tiStart->GetTextCtrl()->GetValue().ToDouble(&m_params.start);
    read_double = read_double && m_tiEnd->GetTextCtrl()->GetValue().ToDouble(&m_params.end);
    read_double = read_double && m_tiStep->GetTextCtrl()->GetValue().ToDouble(&m_params.step);

    if (!read_double || m_params.start <= 10 || m_params.step <= 0 || m_params.end < (m_params.start + m_params.step)) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nstart > 10 \nstep >= 0\nend > start + step)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    m_params.mode = CalibMode::Calib_VFA_Tower;
    m_plater->calib_VFA(m_params);
    EndModal(wxID_OK);
}

void VFA_Test_Dlg::on_dpi_changed(const wxRect& suggested_rect)
{
    this->Refresh();
    Fit();
}



// Retraction_Test_Dlg
//

Retraction_Test_Dlg::Retraction_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("Retraction test"), wxDefaultPosition, parent->FromDIP(wxSize(-1, 280)), wxDEFAULT_DIALOG_STYLE), m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);

    // Settings
    wxString start_length_str = _L("Start retraction length: ");
    wxString end_length_str   = _L("End retraction length: ");
    wxString length_step_str  = _L("Step") + ": ";
    int text_max = GetTextMax(this, std::vector<wxString>{start_length_str, end_length_str, length_step_str});

    auto st_size = FromDIP(wxSize(text_max, -1));
    auto ti_size = FromDIP(wxSize(120, -1));

    LabeledStaticBox* stb = new LabeledStaticBox(this, _L("Settings"));
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    settings_sizer->AddSpacer(FromDIP(5));

    // start length
    auto start_length_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_length_text = new wxStaticText(this, wxID_ANY, start_length_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStart = new TextInput(this, std::to_string(0), _L("mm"), "", wxDefaultPosition, ti_size);
    m_tiStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));

    start_length_sizer->Add(start_length_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    start_length_sizer->Add(m_tiStart        , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(start_length_sizer, 0, wxLEFT, FromDIP(3));

    // end length
    auto end_length_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto end_length_text = new wxStaticText(this, wxID_ANY, end_length_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiEnd = new TextInput(this, std::to_string(2), _L("mm"), "", wxDefaultPosition, ti_size);
    m_tiStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    end_length_sizer->Add(end_length_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    end_length_sizer->Add(m_tiEnd        , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(end_length_sizer, 0, wxLEFT, FromDIP(3));

    // length step
    auto length_step_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto length_step_text = new wxStaticText(this, wxID_ANY, length_step_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiStep = new TextInput(this, wxString::FromDouble(0.1), _L("mm"), "", wxDefaultPosition, ti_size);
    m_tiStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    length_step_sizer->Add(length_step_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    length_step_sizer->Add(m_tiStep        , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(length_step_sizer, 0, wxLEFT, FromDIP(3));

    settings_sizer->AddSpacer(FromDIP(5));

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});
    v_sizer->Add(dlg_btns , 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &Retraction_Test_Dlg::on_start, this);

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
}

Retraction_Test_Dlg::~Retraction_Test_Dlg() {
    // Disconnect Events
}

void Retraction_Test_Dlg::on_start(wxCommandEvent& event) {
    bool read_double = false;
    read_double = m_tiStart->GetTextCtrl()->GetValue().ToDouble(&m_params.start);
    read_double = read_double && m_tiEnd->GetTextCtrl()->GetValue().ToDouble(&m_params.end);
    read_double = read_double && m_tiStep->GetTextCtrl()->GetValue().ToDouble(&m_params.step);

    if (!read_double || m_params.start < 0 || m_params.step <= 0 || m_params.end < (m_params.start + m_params.step)) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values:\nstart > 0 \nstep >= 0\nend > start + step)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    m_params.mode = CalibMode::Calib_Retraction_tower;
    m_plater->calib_retraction(m_params);
    EndModal(wxID_OK);

}

void Retraction_Test_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh();
    Fit();

}

// Input_Shaping_Freq_Test_Dlg
//

Input_Shaping_Freq_Test_Dlg::Input_Shaping_Freq_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("Input shaping Frequency test"), wxDefaultPosition, parent->FromDIP(wxSize(-1, 280)), wxDEFAULT_DIALOG_STYLE), m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);

    // Model selection
    auto labeled_box_model = new LabeledStaticBox(this, _L("Test model"));
    auto model_box = new wxStaticBoxSizer(labeled_box_model, wxHORIZONTAL);

    m_rbModel = new RadioGroup(this, { _L("Ringing Tower"), _L("Fast Tower") }, wxHORIZONTAL);
    model_box->Add(m_rbModel, 0, wxALL | wxEXPAND, FromDIP(4));
    v_sizer->Add(model_box, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Settings
    wxString x_axis_str = "X " + _L("Start / End") + ": ";
    wxString y_axis_str = "Y " + _L("Start / End") + ": ";
    int text_max = GetTextMax(this, std::vector<wxString>{x_axis_str, y_axis_str});

    auto st_size = FromDIP(wxSize(text_max, -1));
    auto ti_size = FromDIP(wxSize(120, -1));

    LabeledStaticBox* stb = new LabeledStaticBox(this, _L("Frequency settings"));
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    settings_sizer->AddSpacer(FromDIP(5));

    // X axis frequencies
    auto x_freq_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_x_text = new wxStaticText(this, wxID_ANY, x_axis_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiFreqStartX = new TextInput(this, std::to_string(15) , _L("hz"), "", wxDefaultPosition, ti_size);
    m_tiFreqStartX->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_tiFreqEndX   = new TextInput(this, std::to_string(110), _L("hz"), "", wxDefaultPosition, ti_size);
    m_tiFreqEndX->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    
    x_freq_sizer->Add(start_x_text  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    x_freq_sizer->Add(m_tiFreqStartX, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    x_freq_sizer->Add(m_tiFreqEndX  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(x_freq_sizer, 0, wxLEFT, FromDIP(3));

    // Y axis frequencies
    auto y_freq_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_y_text = new wxStaticText(this, wxID_ANY, y_axis_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiFreqStartY = new TextInput(this, std::to_string(15) , _L("hz"), "", wxDefaultPosition, ti_size);
    m_tiFreqStartY->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_tiFreqEndY =   new TextInput(this, std::to_string(110), _L("hz"), "", wxDefaultPosition, ti_size);
    m_tiFreqEndY->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    
    y_freq_sizer->Add(start_y_text  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    y_freq_sizer->Add(m_tiFreqStartY, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    y_freq_sizer->Add(m_tiFreqEndY  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(y_freq_sizer, 0, wxLEFT, FromDIP(3));

    // Damping Factor
    wxString damping_factor_str = _L("Damp: ");
    auto damping_factor_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto damping_factor_text = new wxStaticText(this, wxID_ANY, damping_factor_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiDampingFactor = new TextInput(this, wxString::Format("%.3f", 0.15), "", "", wxDefaultPosition, ti_size);
    m_tiDampingFactor->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    
    damping_factor_sizer->Add(damping_factor_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    damping_factor_sizer->Add(m_tiDampingFactor  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(damping_factor_sizer, 0, wxLEFT, FromDIP(3));
    
    settings_sizer->AddSpacer(FromDIP(5));

    // Add a note explaining that 0 means use default value
    auto note_text = new wxStaticText(this, wxID_ANY, _L("Recommended: Set Damp to 0.\nThis will use the printer's default or the last saved value."), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    note_text->SetForegroundColour(wxColour(128, 128, 128));
    settings_sizer->Add(note_text, 0, wxALL, FromDIP(5));

    settings_sizer->AddSpacer(FromDIP(5));

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});
    v_sizer->Add(dlg_btns , 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &Input_Shaping_Freq_Test_Dlg::on_start, this);

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
}

Input_Shaping_Freq_Test_Dlg::~Input_Shaping_Freq_Test_Dlg() {
    // Disconnect Events
}

void Input_Shaping_Freq_Test_Dlg::on_start(wxCommandEvent& event) {
    bool read_double = false;
    read_double = m_tiFreqStartX->GetTextCtrl()->GetValue().ToDouble(&m_params.freqStartX);
    read_double = read_double && m_tiFreqEndX->GetTextCtrl()->GetValue().ToDouble(&m_params.freqEndX);
    read_double = read_double && m_tiFreqStartY->GetTextCtrl()->GetValue().ToDouble(&m_params.freqStartY);
    read_double = read_double && m_tiFreqEndY->GetTextCtrl()->GetValue().ToDouble(&m_params.freqEndY);
    read_double = read_double && m_tiDampingFactor->GetTextCtrl()->GetValue().ToDouble(&m_params.start);
    
    if (!read_double ||
        m_params.freqStartX < 0 || m_params.freqEndX > 500 ||
        m_params.freqStartY < 0 || m_params.freqEndX > 500 ||
        m_params.freqStartX >= m_params.freqEndX ||
        m_params.freqStartY >= m_params.freqEndY) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values\n(0 < FreqStart < FreqEnd < 500"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }
    
    if (m_params.start < 0 || m_params.start >= 1) {
        MessageDialog msg_dlg(nullptr, _L("Please input a valid damping factor (0 < Damping/zeta factor <= 1)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    m_params.mode = CalibMode::Calib_Input_shaping_freq;
    
    // Set model type based on selection
    m_params.test_model = m_rbModel->GetSelection() == 0 ? 0 : 1; // 0 = Ringing Tower, 1 = Fast Tower
    
    m_plater->calib_input_shaping_freq(m_params);
    EndModal(wxID_OK);
}

void Input_Shaping_Freq_Test_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh();
    Fit();
}

// Input_Shaping_Damp_Test_Dlg
//

Input_Shaping_Damp_Test_Dlg::Input_Shaping_Damp_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("Input shaping Damp test"), wxDefaultPosition, parent->FromDIP(wxSize(-1, 280)), wxDEFAULT_DIALOG_STYLE), m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);

    // Model selection
    auto labeled_box_model = new LabeledStaticBox(this, _L("Test model"));
    auto model_box = new wxStaticBoxSizer(labeled_box_model, wxHORIZONTAL);

    m_rbModel = new RadioGroup(this, { _L("Ringing Tower"), _L("Fast Tower") }, wxHORIZONTAL);
    model_box->Add(m_rbModel, 0, wxALL | wxEXPAND, FromDIP(4));
    v_sizer->Add(model_box, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Settings
    wxString freq_str = _L("Frequency") + " X / Y: ";
    wxString damp_str = _L("Damp") + " " + _L("Start / End") + ": ";
    int text_max = GetTextMax(this, std::vector<wxString>{freq_str, damp_str});

    auto st_size = FromDIP(wxSize(text_max, -1));
    auto ti_size = FromDIP(wxSize(120, -1));

    LabeledStaticBox* stb = new LabeledStaticBox(this, _L("Frequency settings"));
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    settings_sizer->AddSpacer(FromDIP(5));

    auto freq_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto freq_text = new wxStaticText(this, wxID_ANY, freq_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiFreqX = new TextInput(this, std::to_string(30), _L("hz"), "", wxDefaultPosition, ti_size);
    m_tiFreqX->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_tiFreqY = new TextInput(this, std::to_string(30), _L("hz"), "", wxDefaultPosition, ti_size);
    m_tiFreqY->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    freq_sizer->Add(freq_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    freq_sizer->Add(m_tiFreqX, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    freq_sizer->Add(m_tiFreqY, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(freq_sizer, 0, wxLEFT, FromDIP(3));
    
    // Damping Factor Start and End
    auto damp_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto damp_text = new wxStaticText(this, wxID_ANY, damp_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiDampingFactorStart = new TextInput(this, wxString::Format("%.3f", 0.00), "", "", wxDefaultPosition, ti_size);
    m_tiDampingFactorStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    m_tiDampingFactorEnd   = new TextInput(this, wxString::Format("%.3f", 0.40), "", "", wxDefaultPosition, ti_size);
    m_tiDampingFactorEnd->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    damp_sizer->Add(damp_text             , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    damp_sizer->Add(m_tiDampingFactorStart, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    damp_sizer->Add(m_tiDampingFactorEnd  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(damp_sizer, 0, wxLEFT, FromDIP(3));

    settings_sizer->AddSpacer(FromDIP(5));

    // Add a note to explain users to use their previously calculated frequency
    auto note_text = new wxStaticText(this, wxID_ANY, _L("Note: Use previously calculated frequencies."), wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    note_text->SetForegroundColour(wxColour(128, 128, 128));
    settings_sizer->Add(note_text, 0, wxALL, FromDIP(5));

    settings_sizer->AddSpacer(FromDIP(5));

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});
    v_sizer->Add(dlg_btns , 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &Input_Shaping_Damp_Test_Dlg::on_start, this);

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
}

Input_Shaping_Damp_Test_Dlg::~Input_Shaping_Damp_Test_Dlg() {
    // Disconnect Events
}

void Input_Shaping_Damp_Test_Dlg::on_start(wxCommandEvent& event) {
    bool read_double = false;
    read_double = m_tiFreqX->GetTextCtrl()->GetValue().ToDouble(&m_params.freqStartX);
    read_double = read_double && m_tiFreqY->GetTextCtrl()->GetValue().ToDouble(&m_params.freqStartY);
    read_double = read_double && m_tiDampingFactorStart->GetTextCtrl()->GetValue().ToDouble(&m_params.start);
    read_double = read_double && m_tiDampingFactorEnd->GetTextCtrl()->GetValue().ToDouble(&m_params.end);

        
    if (!read_double ||
        m_params.freqStartX < 0 || m_params.freqStartX > 500 ||
        m_params.freqStartY < 0 || m_params.freqStartY > 500 ) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values\n(0 < Freq < 500"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    if (m_params.start < 0 || m_params.end > 1
        || m_params.start >= m_params.end) {
        MessageDialog msg_dlg(nullptr, _L("Please input a valid damping factor (0 <= DampingStart < DampingEnd <= 1)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    m_params.mode = CalibMode::Calib_Input_shaping_damp;
    
    // Set model type based on selection
    m_params.test_model = m_rbModel->GetSelection() == 0 ? 0 : 1; // 0 = Ringing Tower, 1 = Fast Tower
    
    m_plater->calib_input_shaping_damp(m_params);
    EndModal(wxID_OK);
}

void Input_Shaping_Damp_Test_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh();
    Fit();
}

// Junction_Deviation_Test_Dlg
//

Junction_Deviation_Test_Dlg::Junction_Deviation_Test_Dlg(wxWindow* parent, wxWindowID id, Plater* plater)
    : DPIDialog(parent, id, _L("Junction Deviation test"), wxDefaultPosition, parent->FromDIP(wxSize(-1, 280)), wxDEFAULT_DIALOG_STYLE), m_plater(plater)
{
    SetBackgroundColour(*wxWHITE); // make sure background color set for dialog
    SetForegroundColour(wxColour("#363636"));
    SetFont(Label::Body_14);

    wxBoxSizer* v_sizer = new wxBoxSizer(wxVERTICAL);
    SetSizer(v_sizer);

    // Model selection
    auto labeled_box_model = new LabeledStaticBox(this, _L("Test model"));
    auto model_box = new wxStaticBoxSizer(labeled_box_model, wxHORIZONTAL);

    m_rbModel = new RadioGroup(this, { _L("Ringing Tower"), _L("Fast Tower") }, wxHORIZONTAL);
    model_box->Add(m_rbModel, 0, wxALL | wxEXPAND, FromDIP(4));
    v_sizer->Add(model_box, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));

    // Settings
    wxString start_jd_str = _L("Start junction deviation: ");
    wxString end_jd_str   = _L("End junction deviation: ");
    int text_max = GetTextMax(this, std::vector<wxString>{start_jd_str, end_jd_str});

    auto st_size = FromDIP(wxSize(text_max, -1));
    auto ti_size = FromDIP(wxSize(120, -1));

    LabeledStaticBox* stb = new LabeledStaticBox(this, _L("Junction Deviation settings"));
    wxStaticBoxSizer* settings_sizer = new wxStaticBoxSizer(stb, wxVERTICAL);

    settings_sizer->AddSpacer(FromDIP(5));

    // Start junction deviation
    auto start_jd_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto start_jd_text = new wxStaticText(this, wxID_ANY, start_jd_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiJDStart = new TextInput(this, wxString::Format("%.3f", 0.000), _L("mm"), "", wxDefaultPosition, ti_size);
    m_tiJDStart->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    start_jd_sizer->Add(start_jd_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    start_jd_sizer->Add(m_tiJDStart  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(start_jd_sizer, 0, wxLEFT, FromDIP(3));

    // End junction deviation
    auto end_jd_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto end_jd_text = new wxStaticText(this, wxID_ANY, end_jd_str, wxDefaultPosition, st_size, wxALIGN_LEFT);
    m_tiJDEnd = new TextInput(this, wxString::Format("%.3f", 0.250), _L("mm"), "", wxDefaultPosition, ti_size);
    m_tiJDEnd->GetTextCtrl()->SetValidator(wxTextValidator(wxFILTER_NUMERIC));
    end_jd_sizer->Add(end_jd_text, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    end_jd_sizer->Add(m_tiJDEnd  , 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(2));
    settings_sizer->Add(end_jd_sizer, 0, wxLEFT, FromDIP(3));

    settings_sizer->AddSpacer(FromDIP(5));

    // Add note about junction deviation
    auto note_text = new wxStaticText(this, wxID_ANY, _L("Note: Lower values = sharper corners but slower speeds"), 
                                    wxDefaultPosition, wxDefaultSize, wxALIGN_LEFT);
    note_text->SetForegroundColour(wxColour(128, 128, 128));
    settings_sizer->Add(note_text, 0, wxALL, FromDIP(5));

    v_sizer->Add(settings_sizer, 0, wxTOP | wxRIGHT | wxLEFT | wxEXPAND, FromDIP(10));
    v_sizer->AddSpacer(FromDIP(5));

    auto dlg_btns = new DialogButtons(this, {"OK"});
    v_sizer->Add(dlg_btns , 0, wxEXPAND);

    dlg_btns->GetOK()->Bind(wxEVT_BUTTON, &Junction_Deviation_Test_Dlg::on_start, this);

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
}

Junction_Deviation_Test_Dlg::~Junction_Deviation_Test_Dlg() {
    // Disconnect Events
}

void Junction_Deviation_Test_Dlg::on_start(wxCommandEvent& event) {
    bool read_double = false;
    read_double = m_tiJDStart->GetTextCtrl()->GetValue().ToDouble(&m_params.start);
    read_double = read_double && m_tiJDEnd->GetTextCtrl()->GetValue().ToDouble(&m_params.end);

    if (!read_double || m_params.start < 0 || m_params.end >= 1 || m_params.start >= m_params.end) {
        MessageDialog msg_dlg(nullptr, _L("Please input valid values\n(0 <= Junction Deviation < 1)"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    } else if (m_params.end > 0.3) {
        MessageDialog msg_dlg(nullptr, _L("NOTE: High values may cause Layer shift"), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
    }

    m_params.mode = CalibMode::Calib_Junction_Deviation;
    
    // Set model type based on selection
    m_params.test_model = m_rbModel->GetSelection() == 0 ? 0 : 1; // 0 = Ringing Tower, 1 = Fast Tower
    
    m_plater->calib_junction_deviation(m_params);
    EndModal(wxID_OK);
}

void Junction_Deviation_Test_Dlg::on_dpi_changed(const wxRect& suggested_rect) {
    this->Refresh();
    Fit();
}

}} // namespace Slic3r::GUI