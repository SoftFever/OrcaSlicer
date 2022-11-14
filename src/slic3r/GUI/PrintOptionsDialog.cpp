#include "PrintOptionsDialog.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"

#define DLG_SIZE  (wxSize(FromDIP(360), FromDIP(160)))

static const wxColour STATIC_BOX_LINE_COL = wxColour(238, 238, 238);
static const wxColour STATIC_TEXT_CAPTION_COL = wxColour(100, 100, 100);

namespace Slic3r { namespace GUI {

PrintOptionsDialog::PrintOptionsDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Print Options"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    this->SetDoubleBuffered(true);
    std::string icon_path = (boost::format("%1%/images/BambuStudioTitle.ico") % resources_dir()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    
    this->SetMinSize(DLG_SIZE);
    this->SetSize(DLG_SIZE);

    auto m_options_sizer = create_settings_group(this);
    this->SetSizer(m_options_sizer);
    this->Layout();
    m_options_sizer->Fit(this);
    this->Fit();

    m_cb_ai_monitoring->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& evt) {
        if (obj) {
            int         level = ai_monitoring_level_list->GetSelection();
            std::string lvl   = sensitivity_level_to_msg_string((AiMonitorSensitivityLevel) level);
            if (!lvl.empty())
                obj->command_xcam_control_ai_monitoring(m_cb_ai_monitoring->GetValue(), lvl);
            else
                BOOST_LOG_TRIVIAL(warning) << "print_option: lvl = " << lvl;
        }
        evt.Skip();
    });

    m_cb_first_layer->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& evt) {
        if (obj) {
            obj->command_xcam_control_first_layer_inspector(m_cb_first_layer->GetValue(), false);
        }
        evt.Skip();
    });

    m_cb_auto_recovery->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& evt) {
        if (obj) {
            obj->command_xcam_control_auto_recovery_step_loss(m_cb_auto_recovery->GetValue());
        }
        evt.Skip();
    });

    m_cb_plate_mark->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& evt) {
        if (obj) {
            obj->command_xcam_control_buildplate_marker_detector(m_cb_plate_mark->GetValue());
        }
        evt.Skip();
    });
}

PrintOptionsDialog::~PrintOptionsDialog()
{
    ai_monitoring_level_list->Disconnect( wxEVT_COMBOBOX, wxCommandEventHandler(PrintOptionsDialog::set_ai_monitor_sensitivity), NULL, this );
}

void PrintOptionsDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    this->SetMinSize(DLG_SIZE);
    this->SetSize(DLG_SIZE);
    Fit();
}

void PrintOptionsDialog::update_ai_monitor_status()
{
    if (m_cb_ai_monitoring->GetValue()) {
        ai_monitoring_level_list->Enable();
    }
    else {
        ai_monitoring_level_list->Disable();
    }
}

void PrintOptionsDialog::update_options(MachineObject* obj_)
{
    if (!obj_) return;
    if (obj_->is_function_supported(PrinterFunction::FUNC_AI_MONITORING)) {
        text_ai_monitoring->Show();
        m_cb_ai_monitoring->Show();
        text_ai_monitoring_caption->Show();
        ai_monitoring_level_list->Show();
        line1->Show();
    }
    else {
        text_ai_monitoring->Hide();
        m_cb_ai_monitoring->Hide();
        text_ai_monitoring_caption->Hide();
        ai_monitoring_level_list->Hide();
        line1->Hide();
    }

    if (obj_->is_function_supported(PrinterFunction::FUNC_BUILDPLATE_MARKER_DETECT)) {
        text_plate_mark->Show();
        m_cb_plate_mark->Show();
        text_plate_mark_caption->Show();
        line2->Show();
    }
    else {
        text_plate_mark->Hide();
        m_cb_plate_mark->Hide();
        text_plate_mark_caption->Hide();
        line2->Hide();
    }

    if (obj_->is_function_supported(PrinterFunction::FUNC_FIRSTLAYER_INSPECT)) {
        text_first_layer->Show();
        m_cb_first_layer->Show();
        line3->Show();
    }
    else {
        text_first_layer->Hide();
        m_cb_first_layer->Hide();
        line3->Hide();
    }

    if (obj_->is_function_supported(PrinterFunction::FUNC_AUTO_RECOVERY_STEP_LOSS)) {
        text_auto_recovery->Show();
        m_cb_auto_recovery->Show();
        line4->Show();
    }
    else {
        text_auto_recovery->Hide();
        m_cb_auto_recovery->Hide();
        line4->Hide();
    }

    this->Freeze();

    auto test1 = obj_->xcam_first_layer_inspector;
    auto test2 = obj_->xcam_buildplate_marker_detector;
    auto test3 = obj_->xcam_auto_recovery_step_loss;
    m_cb_first_layer->SetValue(obj_->xcam_first_layer_inspector);
    m_cb_plate_mark->SetValue(obj_->xcam_buildplate_marker_detector);
    m_cb_auto_recovery->SetValue(obj_->xcam_auto_recovery_step_loss);

    m_cb_ai_monitoring->SetValue(obj_->xcam_ai_monitoring);
    for (auto i = AiMonitorSensitivityLevel::LOW; i < LEVELS_NUM; i = (AiMonitorSensitivityLevel) (i + 1)) {
        if (sensitivity_level_to_msg_string(i) == obj_->xcam_ai_monitoring_sensitivity) {
            ai_monitoring_level_list->SetSelection((int)i);
            break;
        }
    }

    update_ai_monitor_status();
    this->Thaw();
}

wxBoxSizer* PrintOptionsDialog::create_settings_group(wxWindow* parent)
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* line_sizer = new wxBoxSizer(wxHORIZONTAL);

    // ai monitoring with levels
    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_cb_ai_monitoring = new CheckBox(parent);
    text_ai_monitoring = new wxStaticText(parent, wxID_ANY, _L("Enable AI monitoring of printing"));
    text_ai_monitoring->SetFont(Label::Body_14);
    line_sizer->Add(FromDIP(5), 0, 0, 0);
    line_sizer->Add(m_cb_ai_monitoring, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add(text_ai_monitoring, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    sizer->Add(line_sizer, 0, wxEXPAND | wxALL, FromDIP(5));
    line_sizer->Add(FromDIP(5), 0, 0, 0);

    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    text_ai_monitoring_caption = new wxStaticText(parent, wxID_ANY, _L("Sensitivity of pausing is"));
    text_ai_monitoring_caption->SetFont(Label::Body_14);
    text_ai_monitoring_caption->SetForegroundColour(STATIC_TEXT_CAPTION_COL);
    text_ai_monitoring_caption->Wrap(-1);

    ai_monitoring_level_list = new ComboBox( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(120),-1), 0, NULL, wxCB_READONLY );
    for (auto i = AiMonitorSensitivityLevel::LOW; i < LEVELS_NUM; i = (AiMonitorSensitivityLevel) (i + 1)) {
        wxString level_option = sensitivity_level_to_label_string(i);
        ai_monitoring_level_list->Append(level_option);
    }

    line_sizer->Add(FromDIP(30), 0, 0, 0);
    line_sizer->Add(text_ai_monitoring_caption, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add( ai_monitoring_level_list, 0, wxEXPAND|wxALL, FromDIP(5) );
    sizer->Add(line_sizer, 0, wxEXPAND | wxALL , 0);

    line1 = new StaticLine(parent, false);
    line1->SetLineColour(STATIC_BOX_LINE_COL);
    sizer->Add(line1, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    // detection of build plate position
    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_cb_plate_mark = new CheckBox(parent);
    text_plate_mark = new wxStaticText(parent, wxID_ANY, _L("Enable detection of build plate position"));
    text_plate_mark->SetFont(Label::Body_14);
    line_sizer->Add(FromDIP(5), 0, 0, 0);
    line_sizer->Add(m_cb_plate_mark, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add(text_plate_mark, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    sizer->Add(line_sizer, 0, wxEXPAND | wxALL, FromDIP(5));
    line_sizer->Add(FromDIP(5), 0, 0, 0);

    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    wxString caption_text = _L(
        "The localization tag of build plate is detected, and printing is paused if the tag is not in predefined range."
    );
    caption_text = format_text(text_plate_mark, caption_text, FromDIP(250));
    text_plate_mark_caption = new wxStaticText(parent, wxID_ANY, caption_text);
    if (is_english_text(caption_text)) {
       text_plate_mark_caption->Wrap(FromDIP(250));
    }
    text_plate_mark_caption->SetFont(Label::Body_14);
    text_plate_mark_caption->SetForegroundColour(STATIC_TEXT_CAPTION_COL);
    line_sizer->Add(FromDIP(30), 0, 0, 0);
    line_sizer->Add(text_plate_mark_caption, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    sizer->Add(line_sizer, 0, wxEXPAND | wxALL , FromDIP(5));

    line2 = new StaticLine(parent, false);
    line2->SetLineColour(STATIC_BOX_LINE_COL);
    sizer->Add(line2, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    // detection of first layer
    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_cb_first_layer = new CheckBox(parent);
    text_first_layer = new wxStaticText(parent, wxID_ANY, _L("First Layer Inspection"));
    text_first_layer->SetFont(Label::Body_14);
    line_sizer->Add(FromDIP(5), 0, 0, 0);
    line_sizer->Add(m_cb_first_layer, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add(text_first_layer, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    sizer->Add(line_sizer, 0, wxEXPAND | wxALL, FromDIP(5));
    line_sizer->Add(FromDIP(5), 0, 0, 0);

    line3 = new StaticLine(parent, false);
    line3->SetLineColour(STATIC_BOX_LINE_COL);
    sizer->Add(line3, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    // auto-recovery from step loss
    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_cb_auto_recovery = new CheckBox(parent);
    text_auto_recovery = new wxStaticText(parent, wxID_ANY, _L("Auto-recovery from step loss"));
    text_auto_recovery->SetFont(Label::Body_14);
    line_sizer->Add(FromDIP(5), 0, 0, 0);
    line_sizer->Add(m_cb_auto_recovery, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add(text_auto_recovery, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    sizer->Add(line_sizer, 0, wxEXPAND | wxALL, FromDIP(5));
    line_sizer->Add(FromDIP(5), 0, 0, 0);

    line4 = new StaticLine(parent, false);
    line4->SetLineColour(wxColour(255,255,255));
    sizer->Add(line4, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    ai_monitoring_level_list->Connect( wxEVT_COMBOBOX, wxCommandEventHandler(PrintOptionsDialog::set_ai_monitor_sensitivity), NULL, this );

    return sizer;
}

wxString PrintOptionsDialog::sensitivity_level_to_label_string(enum AiMonitorSensitivityLevel level) {
    switch (level) {
    case LOW:
        return _L("Low");
    case MEDIUM:
        return _L("Medium");
    case HIGH:
        return _L("High");
    default:
        return _L("");
    }
    return "";
}

std::string PrintOptionsDialog::sensitivity_level_to_msg_string(enum AiMonitorSensitivityLevel level) {
    switch (level) {
    case LOW:
        return "low";
    case MEDIUM:
        return "medium";
    case HIGH:
        return "high";
    default:
        return "";
    }
    return "";
}

void PrintOptionsDialog::set_ai_monitor_sensitivity(wxCommandEvent& evt)
{
    int level = ai_monitoring_level_list->GetSelection();
    std::string lvl = sensitivity_level_to_msg_string((AiMonitorSensitivityLevel)level);

    if (obj && !lvl.empty()) {
        obj->command_xcam_control_ai_monitoring(m_cb_ai_monitoring->GetValue(), lvl);
    } else {
        BOOST_LOG_TRIVIAL(warning) << "print_option: obj is null or lvl = " << lvl;
    }
}

void PrintOptionsDialog::update_machine_obj(MachineObject *obj_)
{
    obj = obj_;
}

bool PrintOptionsDialog::Show(bool show)
{
    if (show) { CentreOnParent(); }
    return DPIDialog::Show(show);
}

bool PrintOptionsDialog::is_english_text(wxString str)
{
    std::regex reg("^[0-9a-zA-Z]+$");
    std::smatch matchResult;

    std::string pattern_Special = "{}[]<>~!@#$%^&*(),.?/ :";
    for (auto i = 0; i < str.Length(); i++) {
        std::string regex_str = wxString(str[i]).ToStdString();
        if(std::regex_match(regex_str, matchResult, reg)){
            continue;
        }
        else {
            int result = pattern_Special.find(regex_str.c_str());
            if (result < 0 || result > pattern_Special.length()) {
                return false;
            }
        }
    }
    return true;
}

wxString PrintOptionsDialog::format_text(wxStaticText* st, wxString str, int warp)
{
    if (is_english_text(str)) return str;

    wxString out_txt = str;
    wxString count_txt = "";
    int      new_line_pos = 0;

    for (int i = 0; i < str.length(); i++) {
        if (str[i] == '\n') {
            count_txt = "";
            continue;
        }
        auto text_size = st->GetTextExtent(count_txt);
        if (text_size.x < warp) {
            count_txt += str[i];
        }
        else {
            out_txt.insert(i - 1, '\n');
            count_txt = "";
        }
    }
    return out_txt;
}

}} // namespace Slic3r::GUI
