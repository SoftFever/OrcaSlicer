#include "PrintOptionsDialog.hpp"
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "libslic3r/Utils.hpp"
#include "Widgets/SwitchButton.hpp"
#include "MsgDialog.hpp"

static const wxColour STATIC_BOX_LINE_COL = wxColour(238, 238, 238);
static const wxColour STATIC_TEXT_CAPTION_COL = wxColour(100, 100, 100);
static const wxColour STATIC_TEXT_EXPLAIN_COL = wxColour(100, 100, 100);

namespace Slic3r { namespace GUI {

PrintOptionsDialog::PrintOptionsDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Print Options"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    this->SetDoubleBuffered(true);

    SetBackgroundColour(*wxWHITE);

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

    m_cb_open_door->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &evt) {
        if (m_cb_open_door->GetValue()) {
            if (obj) { obj->command_set_door_open_check(MachineObject::DOOR_OPEN_CHECK_ENABLE_WARNING); }
        } else {
            if (obj) { obj->command_set_door_open_check(MachineObject::DOOR_OPEN_CHECK_DISABLE); }
        }

        evt.Skip();
    });

    open_door_switch_board->Bind(wxCUSTOMEVT_SWITCH_POS, [this](wxCommandEvent &evt)
    {
        if (evt.GetInt() == 0)
        {
            if (obj) { obj->command_set_door_open_check(MachineObject::DOOR_OPEN_CHECK_ENABLE_PAUSE_PRINT); }
        }
        else if (evt.GetInt() == 1)
        {
            if (obj) { obj->command_set_door_open_check(MachineObject::DOOR_OPEN_CHECK_ENABLE_WARNING); }
        }

        evt.Skip();
    });

    m_cb_save_remote_print_file_to_storage->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& evt)
    {
        if (obj) { obj->command_set_save_remote_print_file_to_storage(m_cb_save_remote_print_file_to_storage->GetValue());}
        evt.Skip();
    });

    m_cb_plate_mark->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& evt) {
        if (obj) {
            obj->command_xcam_control_buildplate_marker_detector(m_cb_plate_mark->GetValue());
        }
        evt.Skip();
    });
    m_cb_sup_sound->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& evt) {
        if (obj) {
            obj->command_xcam_control_allow_prompt_sound(m_cb_sup_sound->GetValue());
        }
        evt.Skip();
    });
    m_cb_filament_tangle->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& evt) {
        if (obj) {
            obj->command_xcam_control_filament_tangle_detect(m_cb_filament_tangle->GetValue());
        }
        evt.Skip();
    });
    m_cb_nozzle_blob->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent& evt) {
        if (obj) {
            obj->command_nozzle_blob_detect(m_cb_nozzle_blob->GetValue());
        }
        evt.Skip();
        });

    wxGetApp().UpdateDlgDarkUI(this);
}

PrintOptionsDialog::~PrintOptionsDialog()
{
    ai_monitoring_level_list->Disconnect( wxEVT_COMBOBOX, wxCommandEventHandler(PrintOptionsDialog::set_ai_monitor_sensitivity), NULL, this );
}

void PrintOptionsDialog::on_dpi_changed(const wxRect& suggested_rect)
{
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

    if (obj_->is_support_ai_monitoring) {
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

    if (obj_->is_support_build_plate_marker_detect) {
        if (obj_->m_plate_maker_detect_type == MachineObject::POS_CHECK && (text_plate_mark->GetLabel() != _L("Enable detection of build plate position"))) {
            text_plate_mark->SetLabel(_L("Enable detection of build plate position"));
            text_plate_mark_caption->SetLabel(_L("The localization tag of build plate is detected, and printing is paused if the tag is not in predefined range."));
            text_plate_mark_caption->Wrap(FromDIP(260));
        } else if (obj_->m_plate_maker_detect_type == MachineObject::TYPE_POS_CHECK && (text_plate_mark->GetLabel() != _L("Build Plate Detection"))) {
            text_plate_mark->SetLabel(_L("Build Plate Detection"));
            text_plate_mark_caption->SetLabel(_L("Identifies the type and position of the build plate on the heatbed. Pausing printing if a mismatch is detected."));
            text_plate_mark_caption->Wrap(FromDIP(260));
        }

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

    if (obj_->is_support_first_layer_inspect) {
        text_first_layer->Show();
        m_cb_first_layer->Show();
        line3->Show();
    }
    else {
        text_first_layer->Hide();
        m_cb_first_layer->Hide();
        line3->Hide();
    }

    if (obj_->is_support_auto_recovery_step_loss) {
        text_auto_recovery->Show();
        m_cb_auto_recovery->Show();
        line4->Show();
    }
    else {
        text_auto_recovery->Hide();
        m_cb_auto_recovery->Hide();
        line4->Hide();
    }
    if (obj_->is_support_prompt_sound) {
        text_sup_sound->Show();
        m_cb_sup_sound->Show();
        line5->Show();
    }
    else {
        text_sup_sound->Hide();
        m_cb_sup_sound->Hide();
        line5->Hide();
    }
    if (obj_->is_support_filament_tangle_detect) {
        text_filament_tangle->Show();
        m_cb_filament_tangle->Show();
        line6->Show();
    }
    else {
        text_filament_tangle->Hide();
        m_cb_filament_tangle->Hide();
        line6->Hide();
    }
    if (false/*obj_->is_support_nozzle_blob_detection*/) {
        text_nozzle_blob->Show();
        m_cb_nozzle_blob->Show();
        text_nozzle_blob_caption->Show();
        line7->Show();
    }
    else {
        text_nozzle_blob->Hide();
        m_cb_nozzle_blob->Hide();
        text_nozzle_blob_caption->Hide();
        line7->Hide();
    }

    UpdateOptionOpenDoorCheck(obj_);
    UpdateOptionSavePrintFileToStorage(obj_);

    this->Freeze();

    m_cb_first_layer->SetValue(obj_->xcam_first_layer_inspector);
    m_cb_plate_mark->SetValue(obj_->xcam_buildplate_marker_detector);
    m_cb_auto_recovery->SetValue(obj_->xcam_auto_recovery_step_loss);
    m_cb_sup_sound->SetValue(obj_->xcam_allow_prompt_sound);
    m_cb_filament_tangle->SetValue(obj_->xcam_filament_tangle_detect);
    m_cb_nozzle_blob->SetValue(obj_->nozzle_blob_detection_enabled);

    m_cb_ai_monitoring->SetValue(obj_->xcam_ai_monitoring);
    for (auto i = AiMonitorSensitivityLevel::LOW; i < LEVELS_NUM; i = (AiMonitorSensitivityLevel) (i + 1)) {
        if (sensitivity_level_to_msg_string(i) == obj_->xcam_ai_monitoring_sensitivity) {
            ai_monitoring_level_list->SetSelection((int)i);
            break;
        }
    }

    update_ai_monitor_status();
    this->Thaw();
    Layout();
}

void PrintOptionsDialog::UpdateOptionOpenDoorCheck(MachineObject *obj) {
    if (!obj || !obj->support_door_open_check()) {
        m_cb_open_door->Hide();
        text_open_door->Hide();
        open_door_switch_board->Hide();
        return;
    }

    if (obj->get_door_open_check_state() != MachineObject::DOOR_OPEN_CHECK_DISABLE) {
        m_cb_open_door->SetValue(true);
        open_door_switch_board->Enable();

        if (obj->get_door_open_check_state() == MachineObject::DOOR_OPEN_CHECK_ENABLE_WARNING) {
            open_door_switch_board->updateState("left");
            open_door_switch_board->Refresh();
        } else if (obj->get_door_open_check_state() == MachineObject::DOOR_OPEN_CHECK_ENABLE_PAUSE_PRINT) {
            open_door_switch_board->updateState("right");
            open_door_switch_board->Refresh();
        }

    } else {
        m_cb_open_door->SetValue(false);
        open_door_switch_board->Disable();
    }

    m_cb_open_door->Show();
    text_open_door->Show();
    open_door_switch_board->Show();
}

void PrintOptionsDialog::UpdateOptionSavePrintFileToStorage(MachineObject *obj)
{
    if (obj && obj->support_save_remote_print_file_to_storage())
    {
        m_cb_save_remote_print_file_to_storage->SetValue(obj->get_save_remote_print_file_to_storage());
    }
    else
    {
        m_cb_save_remote_print_file_to_storage->Hide();
        text_save_remote_print_file_to_storage->Hide();
        text_save_remote_print_file_to_storage_explain->Hide();
    }
}

wxBoxSizer* PrintOptionsDialog::create_settings_group(wxWindow* parent)
{
    wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);
    wxBoxSizer* line_sizer = new wxBoxSizer(wxHORIZONTAL);

    auto m_line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line->SetBackgroundColour(wxColour(166, 169, 170));

    sizer->Add(m_line, 0, wxEXPAND, 0);

    // ai monitoring with levels
    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_cb_ai_monitoring = new CheckBox(parent);
    text_ai_monitoring = new Label(parent, _L("Enable AI monitoring of printing"));
    text_ai_monitoring->SetFont(Label::Body_14);
    line_sizer->Add(FromDIP(5), 0, 0, 0);
    line_sizer->Add(m_cb_ai_monitoring, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add(text_ai_monitoring, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    sizer->Add(0,0,0,wxTOP, FromDIP(18));
    sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));
    line_sizer->Add(FromDIP(5), 0, 0, 0);

    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    text_ai_monitoring_caption = new Label(parent, _L("Sensitivity of pausing is"));
    text_ai_monitoring_caption->SetFont(Label::Body_14);
    text_ai_monitoring_caption->SetForegroundColour(STATIC_TEXT_CAPTION_COL);
    text_ai_monitoring_caption->Wrap(-1);

    ai_monitoring_level_list = new ComboBox( this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(100),-1), 0, NULL, wxCB_READONLY );
    for (auto i = AiMonitorSensitivityLevel::LOW; i < LEVELS_NUM; i = (AiMonitorSensitivityLevel) (i + 1)) {
        wxString level_option = sensitivity_level_to_label_string(i);
        ai_monitoring_level_list->Append(level_option);
    }

    if (ai_monitoring_level_list->GetCount() > 0) {
        ai_monitoring_level_list->SetSelection(0);
    }


    line_sizer->Add(FromDIP(30), 0, 0, 0);
    line_sizer->Add(text_ai_monitoring_caption, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add( ai_monitoring_level_list, 0, wxEXPAND|wxALL, FromDIP(5) );
    sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));

    line1 = new StaticLine(parent, false);
    line1->SetLineColour(STATIC_BOX_LINE_COL);
    sizer->Add(line1, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));

    // detection of build plate position
    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_cb_plate_mark = new CheckBox(parent);
    text_plate_mark = new Label(parent, _L("Enable detection of build plate position"));
    text_plate_mark->SetFont(Label::Body_14);
    line_sizer->Add(FromDIP(5), 0, 0, 0);
    line_sizer->Add(m_cb_plate_mark, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add(text_plate_mark, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));
    line_sizer->Add(FromDIP(5), 0, 0, 0);

    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    wxString caption_text = _L(
        "The localization tag of build plate is detected, and printing is paused if the tag is not in predefined range."
    );
    text_plate_mark_caption = new Label(parent, caption_text);
    text_plate_mark_caption->Wrap(FromDIP(260));
    text_plate_mark_caption->SetFont(Label::Body_14);
    text_plate_mark_caption->SetForegroundColour(STATIC_TEXT_CAPTION_COL);
    line_sizer->Add(FromDIP(30), 0, 0, 0);
    line_sizer->Add(text_plate_mark_caption, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(0));
    sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));

    line2 = new StaticLine(parent, false);
    line2->SetLineColour(STATIC_BOX_LINE_COL);
    sizer->Add(line2, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    // detection of first layer
    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_cb_first_layer = new CheckBox(parent);
    text_first_layer = new Label(parent, _L("First Layer Inspection"));
    text_first_layer->SetFont(Label::Body_14);
    line_sizer->Add(FromDIP(5), 0, 0, 0);
    line_sizer->Add(m_cb_first_layer, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add(text_first_layer, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    sizer->Add(0,0,0,wxTOP, FromDIP(15));
    sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));
    line_sizer->Add(FromDIP(5), 0, 0, 0);

    line3 = new StaticLine(parent, false);
    line3->SetLineColour(STATIC_BOX_LINE_COL);
    sizer->Add(line3, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    // auto-recovery from step loss
    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_cb_auto_recovery = new CheckBox(parent);
    text_auto_recovery = new Label(parent, _L("Auto-recovery from step loss"));
    text_auto_recovery->SetFont(Label::Body_14);
    line_sizer->Add(FromDIP(5), 0, 0, 0);
    line_sizer->Add(m_cb_auto_recovery, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add(text_auto_recovery, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));
    line_sizer->Add(FromDIP(5), 0, 0, 0);

    line4 = new StaticLine(parent, false);
    line4->SetLineColour(wxColour(255,255,255));
    sizer->Add(line4, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    //Open Door Detection
    line_sizer         = new wxBoxSizer(wxHORIZONTAL);
    m_cb_open_door     = new CheckBox(parent);
    text_open_door     = new Label(parent, _L("Open Door Dectection"));
    text_open_door->SetFont(Label::Body_14);
    open_door_switch_board = new SwitchBoard(parent, _L("Notification"), _L("Pause printing"), wxSize(FromDIP(200), FromDIP(26)));
    open_door_switch_board->Disable();
    line_sizer->Add(FromDIP(5), 0, 0, 0);
    line_sizer->Add(m_cb_open_door, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add(text_open_door, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
    sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));
    sizer->Add(open_door_switch_board, 0, wxLEFT, FromDIP(58));
    line_sizer->Add(FromDIP(5), 0, 0, 0);

     //Save remote file to local storage
    line_sizer     = new wxBoxSizer(wxHORIZONTAL);
    m_cb_save_remote_print_file_to_storage = new CheckBox(parent);
    text_save_remote_print_file_to_storage = new Label(parent, _L("Store Sent Files on External Storage"));
    text_save_remote_print_file_to_storage->SetFont(Label::Body_14);
    line_sizer->Add(FromDIP(5), 0, 0, 0);
    line_sizer->Add(m_cb_save_remote_print_file_to_storage, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add(text_save_remote_print_file_to_storage, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    text_save_remote_print_file_to_storage_explain = new Label(parent, _L("Save the printing files initiated from Bambu Studio, Bambu Handy and MakerWorld on External Storage"));
    text_save_remote_print_file_to_storage_explain->SetForegroundColour(STATIC_TEXT_EXPLAIN_COL);
    text_save_remote_print_file_to_storage_explain->SetFont(Label::Body_14);
    text_save_remote_print_file_to_storage_explain->Wrap(FromDIP(260));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
    sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));
    sizer->Add(text_save_remote_print_file_to_storage_explain, 0, wxLEFT, FromDIP(58));
    line_sizer->Add(FromDIP(5), 0, 0, 0);

    //Allow prompt sound
    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_cb_sup_sound = new CheckBox(parent);
    text_sup_sound = new Label(parent, _L("Allow Prompt Sound"));
    text_sup_sound->SetFont(Label::Body_14);
    line_sizer->Add(FromDIP(5), 0, 0, 0);
    line_sizer->Add(m_cb_sup_sound, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add(text_sup_sound, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
    sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));
    line_sizer->Add(FromDIP(5), 0, 0, 0);

    line5 = new StaticLine(parent, false);
    line5->SetLineColour(STATIC_BOX_LINE_COL);
    sizer->Add(line5, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(20));

    //filament tangle detect
    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_cb_filament_tangle = new CheckBox(parent);
    text_filament_tangle = new Label(parent, _L("Filament Tangle Detect"));
    text_filament_tangle->SetFont(Label::Body_14);
    line_sizer->Add(FromDIP(5), 0, 0, 0);
    line_sizer->Add(m_cb_filament_tangle, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add(text_filament_tangle, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
    sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));
    line_sizer->Add(FromDIP(5), 0, 0, 0);

    line6 = new StaticLine(parent, false);
    line6->SetLineColour(STATIC_BOX_LINE_COL);
    sizer->Add(line6, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(20));

    //nozzle blob detect
    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    m_cb_nozzle_blob = new CheckBox(parent);
    text_nozzle_blob = new Label(parent, _L("Nozzle Clumping Detection"));
    text_nozzle_blob->SetFont(Label::Body_14);
    line_sizer->Add(FromDIP(5), 0, 0, 0);
    line_sizer->Add(m_cb_nozzle_blob, 0, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    line_sizer->Add(text_nozzle_blob, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(5));
    sizer->Add(0, 0, 0, wxTOP, FromDIP(15));
    sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));
    line_sizer->Add(FromDIP(5), 0, 0, 0);

    line_sizer = new wxBoxSizer(wxHORIZONTAL);
    wxString nozzle_blob_caption_text = _L("Check if the nozzle is clumping by filament or other foreign objects.");
    text_nozzle_blob_caption = new Label(parent, nozzle_blob_caption_text);
    text_nozzle_blob_caption->SetFont(Label::Body_14);
    text_nozzle_blob_caption->Wrap(-1);
    text_nozzle_blob_caption->SetForegroundColour(STATIC_TEXT_CAPTION_COL);
    line_sizer->Add(FromDIP(30), 0, 0, 0);
    line_sizer->Add(text_nozzle_blob_caption, 1, wxALL | wxALIGN_CENTER_VERTICAL, FromDIP(0));
    sizer->Add(line_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(18));

    line7 = new StaticLine(parent, false);
    line7->SetLineColour(STATIC_BOX_LINE_COL);
    sizer->Add(line7, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));

    text_nozzle_blob->Hide();
    m_cb_nozzle_blob->Hide();
    text_nozzle_blob_caption->Hide();
    line7->Hide();

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
        return "";
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
    if (show) {
        wxGetApp().UpdateDlgDarkUI(this);
        CentreOnParent();
    }
    return DPIDialog::Show(show);
}

PrinterPartsDialog::PrinterPartsDialog(wxWindow* parent)
: DPIDialog(parent, wxID_ANY, _L("Printer Parts"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    nozzle_type_map[NozzleType::ntHardenedSteel]    = _L("Hardened Steel");
    nozzle_type_map[NozzleType::ntStainlessSteel]   = _L("Stainless Steel");

    nozzle_flow_map[NozzleFlowType::S_FLOW]         = _L("Standard");
    nozzle_flow_map[NozzleFlowType::H_FLOW]         = _L("High flow");

    nozzle_type_selection_map[NozzleType::ntHardenedSteel]  = 0;
    nozzle_type_selection_map[NozzleType::ntStainlessSteel] = 1;

    nozzle_flow_selection_map[NozzleFlowType::S_FLOW]  = 0;
    nozzle_flow_selection_map[NozzleFlowType::H_FLOW]  = 1;

    nozzle_stainless_diameter_map[0] = 0.2;
    nozzle_stainless_diameter_map[1] = 0.4;

    nozzle_hard_diameter_map[0] = 0.4;
    nozzle_hard_diameter_map[1] = 0.6;
    nozzle_hard_diameter_map[2] = 0.8;

    SetBackgroundColour(*wxWHITE);

    wxBoxSizer *sizer = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer* single_sizer = new wxBoxSizer(wxVERTICAL);
    single_panel = new wxPanel(this);
    single_panel->SetBackgroundColour(*wxWHITE);

    wxBoxSizer* multiple_sizer = new wxBoxSizer(wxVERTICAL);
    multiple_panel = new wxPanel(this);
    multiple_panel->SetBackgroundColour(*wxWHITE);

    auto m_line = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(-1, 1), wxTAB_TRAVERSAL);
    m_line->SetBackgroundColour(wxColour(166, 169, 170));

    /*single nozzle*/
    //nozzle type
    wxBoxSizer* line_sizer_nozzle_type = new wxBoxSizer(wxHORIZONTAL);

    auto nozzle_type = new Label(single_panel, _L("Nozzle Type"));
    nozzle_type->SetFont(Label::Body_14);
    nozzle_type->SetMinSize(wxSize(FromDIP(180), -1));
    nozzle_type->SetMaxSize(wxSize(FromDIP(180), -1));
    nozzle_type->SetForegroundColour(STATIC_TEXT_CAPTION_COL);
    nozzle_type->Wrap(-1);

    ID_NOZZLE_TYPE_CHECKBOX_SINGLE = wxNewId();
    ID_NOZZLE_TYPE_CHECKBOX_LEFT = wxNewId();
    ID_NOZZLE_TYPE_CHECKBOX_RIGHT = wxNewId();
    ID_NOZZLE_DIAMETER_CHECKBOX_SINGLE = wxNewId();
    ID_NOZZLE_DIAMETER_CHECKBOX_LEFT = wxNewId();
    ID_NOZZLE_DIAMETER_CHECKBOX_RIGHT = wxNewId();
    ID_NOZZLE_FLOW_CHECKBOX_LEFT = wxNewId();
    ID_NOZZLE_FLOW_CHECKBOX_RIGHT = wxNewId();

    nozzle_type_checkbox = new ComboBox(single_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(180), -1), 0, NULL, wxCB_READONLY);
    nozzle_type_checkbox->Append(nozzle_type_map[NozzleType::ntHardenedSteel]);
    nozzle_type_checkbox->Append(nozzle_type_map[NozzleType::ntStainlessSteel]);
    nozzle_type_checkbox->SetSelection(0);


    line_sizer_nozzle_type->Add(nozzle_type, 0, wxALIGN_CENTER, 5);
    line_sizer_nozzle_type->Add(0, 0, 1, wxEXPAND, 5);
    line_sizer_nozzle_type->Add(nozzle_type_checkbox, 0, wxALIGN_CENTER, 5);


    //nozzle diameter
    wxBoxSizer* line_sizer_nozzle_diameter = new wxBoxSizer(wxHORIZONTAL);
    auto nozzle_diameter  = new Label(single_panel, _L("Nozzle Diameter"));
    nozzle_diameter->SetFont(Label::Body_14);
    nozzle_diameter->SetMinSize(wxSize(FromDIP(180), -1));
    nozzle_diameter->SetMaxSize(wxSize(FromDIP(180), -1));
    nozzle_diameter->SetForegroundColour(STATIC_TEXT_CAPTION_COL);
    nozzle_diameter->Wrap(-1);

    nozzle_diameter_checkbox = new ComboBox(single_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(180), -1), 0, NULL, wxCB_READONLY);

    line_sizer_nozzle_diameter->Add(nozzle_diameter, 0, wxALIGN_CENTER, 5);
    line_sizer_nozzle_diameter->Add(0, 0, 1, wxEXPAND, 5);
    line_sizer_nozzle_diameter->Add(nozzle_diameter_checkbox, 0, wxALIGN_CENTER, 5);

    change_nozzle_tips = new Label(single_panel, _L("*Tips: If you changed your nozzle lately, please change settings on printer screen."));
    change_nozzle_tips->SetFont(Label::Body_13);
    change_nozzle_tips->SetForegroundColour(STATIC_TEXT_CAPTION_COL);

    single_sizer->Add(m_line, 0, wxEXPAND, 0);
    single_sizer->Add(0, 0, 0, wxTOP, FromDIP(24));
    single_sizer->Add(line_sizer_nozzle_type, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(10));
    single_sizer->Add(0, 0, 0, wxTOP, FromDIP(24));
    single_sizer->Add(line_sizer_nozzle_diameter, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(10));
    single_sizer->Add(0, 0, 0, wxTOP, FromDIP(24));
    single_sizer->Add(change_nozzle_tips, 0, wxLEFT, FromDIP(24));
    single_sizer->Add(0, 0, 0, wxTOP, FromDIP(10));

    single_panel->SetSizer(single_sizer);
    single_panel->Layout();
    single_panel->Fit();

    /*multiple nozzle*/
    /*left*/
    auto leftTitle = new Label(multiple_panel, _L("Left Nozzle"));
    leftTitle->SetFont(::Label::Head_15);
    leftTitle->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#2C2C2E")));

    wxBoxSizer *multiple_left_line_sizer = new wxBoxSizer(wxHORIZONTAL);
    auto multiple_left_nozzle_type = new Label(multiple_panel, _L("Nozzle Type"));
    multiple_left_nozzle_type->SetFont(Label::Body_14);
    multiple_left_nozzle_type->SetForegroundColour(STATIC_TEXT_CAPTION_COL);

    multiple_left_nozzle_type_checkbox = new ComboBox(multiple_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(180), -1), 0, NULL, wxCB_READONLY);
    multiple_left_nozzle_type_checkbox->Append(nozzle_type_map[NozzleType::ntHardenedSteel]);
    multiple_left_nozzle_type_checkbox->Append(nozzle_type_map[NozzleType::ntStainlessSteel]);
    multiple_left_nozzle_type_checkbox->SetSelection(0);

    auto multiple_left_nozzle_diameter = new Label(multiple_panel, _L("Nozzle Diameter"));
    multiple_left_nozzle_diameter->SetFont(Label::Body_14);
    multiple_left_nozzle_diameter->SetForegroundColour(STATIC_TEXT_CAPTION_COL);
    multiple_left_nozzle_diameter_checkbox = new ComboBox(multiple_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(140), -1), 0, NULL, wxCB_READONLY);

    auto multiple_left_nozzle_flow = new Label(multiple_panel, _L("Nozzle Flow"));
    multiple_left_nozzle_flow->SetFont(Label::Body_14);
    multiple_left_nozzle_flow->SetForegroundColour(STATIC_TEXT_CAPTION_COL);
    multiple_left_nozzle_flow_checkbox = new ComboBox(multiple_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(140), -1), 0, NULL, wxCB_READONLY);
    multiple_left_nozzle_flow_checkbox->Append(nozzle_flow_map[NozzleFlowType::S_FLOW]);
    multiple_left_nozzle_flow_checkbox->Append(nozzle_flow_map[NozzleFlowType::H_FLOW]);

    multiple_left_line_sizer->Add(multiple_left_nozzle_type, 0, wxALIGN_CENTER, 0);
    multiple_left_line_sizer->Add(0, 0, 0, wxLEFT, FromDIP(8));
    multiple_left_line_sizer->Add(multiple_left_nozzle_type_checkbox, 0, wxALIGN_CENTER, 0);
    multiple_left_line_sizer->Add(0, 0, 0, wxLEFT, FromDIP(15));
    multiple_left_line_sizer->Add(multiple_left_nozzle_diameter, 0, wxALIGN_CENTER, 0);
    multiple_left_line_sizer->Add(0, 0, 1, wxLEFT, FromDIP(8));
    multiple_left_line_sizer->Add(multiple_left_nozzle_diameter_checkbox, 0, wxALIGN_CENTER, 0);
    multiple_left_line_sizer->Add(0, 0, 0, wxLEFT, FromDIP(15));
    multiple_left_line_sizer->Add(multiple_left_nozzle_flow, 0, wxALIGN_CENTER, 0);
    multiple_left_line_sizer->Add(0, 0, 1, wxLEFT, FromDIP(8));
    multiple_left_line_sizer->Add(multiple_left_nozzle_flow_checkbox, 0, wxALIGN_CENTER, 0);

    /*right*/
    auto rightTitle = new Label(multiple_panel, _L("Right Nozzle"));
    rightTitle->SetFont(::Label::Head_15);
    rightTitle->SetForegroundColour(StateColor::darkModeColorFor(wxColour("#2C2C2E")));

    wxBoxSizer *multiple_right_line_sizer  = new wxBoxSizer(wxHORIZONTAL);
    auto        multiple_right_nozzle_type = new Label(multiple_panel, _L("Nozzle Type"));
    multiple_right_nozzle_type->SetFont(Label::Body_14);
    multiple_right_nozzle_type->SetForegroundColour(STATIC_TEXT_CAPTION_COL);

    multiple_right_nozzle_type_checkbox = new ComboBox(multiple_panel, ID_NOZZLE_TYPE_CHECKBOX_RIGHT, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(180), -1), 0, NULL, wxCB_READONLY);
    multiple_right_nozzle_type_checkbox->Append(nozzle_type_map[NozzleType::ntHardenedSteel]);
    multiple_right_nozzle_type_checkbox->Append(nozzle_type_map[NozzleType::ntStainlessSteel]);
    multiple_right_nozzle_type_checkbox->SetSelection(0);

    auto multiple_right_nozzle_diameter = new Label(multiple_panel, _L("Nozzle Diameter"));
    multiple_right_nozzle_diameter->SetFont(Label::Body_14);
    multiple_right_nozzle_diameter->SetForegroundColour(STATIC_TEXT_CAPTION_COL);
    multiple_right_nozzle_diameter_checkbox = new ComboBox(multiple_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(140), -1), 0, NULL, wxCB_READONLY);

    auto multiple_right_nozzle_flow = new Label(multiple_panel, _L("Nozzle Flow"));
    multiple_right_nozzle_flow->SetFont(Label::Body_14);
    multiple_right_nozzle_flow->SetForegroundColour(STATIC_TEXT_CAPTION_COL);
    multiple_right_nozzle_flow_checkbox = new ComboBox(multiple_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(FromDIP(140), -1), 0, NULL, wxCB_READONLY);
    multiple_right_nozzle_flow_checkbox->Append(nozzle_flow_map[NozzleFlowType::S_FLOW]);
    multiple_right_nozzle_flow_checkbox->Append(nozzle_flow_map[NozzleFlowType::H_FLOW]);

    multiple_right_line_sizer->Add(multiple_right_nozzle_type, 0, wxALIGN_CENTER, 0);
    multiple_right_line_sizer->Add(0, 0, 0, wxLEFT, FromDIP(8));
    multiple_right_line_sizer->Add(multiple_right_nozzle_type_checkbox, 0, wxALIGN_CENTER, 0);
    multiple_right_line_sizer->Add(0, 0, 0, wxLEFT, FromDIP(15));
    multiple_right_line_sizer->Add(multiple_right_nozzle_diameter, 0, wxALIGN_CENTER, 0);
    multiple_right_line_sizer->Add(0, 0, 1, wxLEFT, FromDIP(8));
    multiple_right_line_sizer->Add(multiple_right_nozzle_diameter_checkbox, 0, wxALIGN_CENTER, 0);
    multiple_right_line_sizer->Add(0, 0, 0, wxLEFT, FromDIP(15));
    multiple_right_line_sizer->Add(multiple_right_nozzle_flow, 0, wxALIGN_CENTER, 0);
    multiple_right_line_sizer->Add(0, 0, 1, wxLEFT, FromDIP(8));
    multiple_right_line_sizer->Add(multiple_right_nozzle_flow_checkbox, 0, wxALIGN_CENTER, 0);

    multiple_change_nozzle_tips = new Label(multiple_panel, _L("*Tips: If you changed your nozzle lately, please change settings on printer screen."));
    multiple_change_nozzle_tips->SetFont(Label::Body_13);
    multiple_change_nozzle_tips->SetForegroundColour(STATIC_TEXT_CAPTION_COL);

    multiple_sizer->Add(0, 0, 0, wxTOP, FromDIP(40));
    multiple_sizer->Add(leftTitle, 0, wxLEFT, FromDIP(18));
    multiple_sizer->Add(multiple_left_line_sizer, 0, wxALIGN_CENTER|wxLEFT|wxRIGHT, FromDIP(18));
    multiple_sizer->Add(0, 0, 0, wxTOP, FromDIP(24));
    multiple_sizer->Add(rightTitle, 0, wxLEFT, FromDIP(18));
    multiple_sizer->Add(multiple_right_line_sizer, 0, wxALIGN_CENTER | wxLEFT | wxRIGHT, FromDIP(18));
    multiple_sizer->Add(0, 0, 0, wxTOP, FromDIP(20));
    multiple_sizer->Add(multiple_change_nozzle_tips, 0, wxLEFT, FromDIP(18));
    multiple_sizer->Add(0, 0, 0, wxTOP, FromDIP(10));

    multiple_panel->SetSizer(multiple_sizer);
    multiple_panel->Layout();
    multiple_panel->Fit();

    /*inset data*/


    sizer->Add(single_panel, 0, wxEXPAND, 0);
    sizer->Add(multiple_panel, 0, wxEXPAND, 0);
    SetSizer(sizer);
    Layout();
    Fit();

    single_panel->Hide();

    wxGetApp().UpdateDlgDarkUI(this);

    nozzle_type_checkbox->Connect(wxEVT_COMBOBOX, wxCommandEventHandler(PrinterPartsDialog::set_nozzle_data), NULL, this);
    nozzle_diameter_checkbox->Connect(wxEVT_COMBOBOX, wxCommandEventHandler(PrinterPartsDialog::set_nozzle_data), NULL, this);

    multiple_left_nozzle_type_checkbox->Connect(wxEVT_COMBOBOX, wxCommandEventHandler(PrinterPartsDialog::set_nozzle_data), NULL, this);
    multiple_left_nozzle_diameter_checkbox->Connect(wxEVT_COMBOBOX, wxCommandEventHandler(PrinterPartsDialog::set_nozzle_data), NULL, this);
    multiple_left_nozzle_flow_checkbox->Connect(wxEVT_COMBOBOX, wxCommandEventHandler(PrinterPartsDialog::set_nozzle_data), NULL, this);

    multiple_right_nozzle_type_checkbox->Connect(wxEVT_COMBOBOX, wxCommandEventHandler(PrinterPartsDialog::set_nozzle_data), NULL, this);
    multiple_right_nozzle_diameter_checkbox->Connect(wxEVT_COMBOBOX, wxCommandEventHandler(PrinterPartsDialog::set_nozzle_data), NULL, this);
    multiple_right_nozzle_flow_checkbox->Connect(wxEVT_COMBOBOX, wxCommandEventHandler(PrinterPartsDialog::set_nozzle_data), NULL, this);

    nozzle_type_checkbox->SetId(ID_NOZZLE_TYPE_CHECKBOX_SINGLE);
    multiple_left_nozzle_type_checkbox->SetId(ID_NOZZLE_TYPE_CHECKBOX_LEFT);
    multiple_right_nozzle_type_checkbox->SetId(ID_NOZZLE_TYPE_CHECKBOX_RIGHT);

    nozzle_diameter_checkbox->SetId(ID_NOZZLE_DIAMETER_CHECKBOX_SINGLE);
    multiple_left_nozzle_diameter_checkbox->SetId(ID_NOZZLE_DIAMETER_CHECKBOX_LEFT);
    multiple_right_nozzle_diameter_checkbox->SetId(ID_NOZZLE_DIAMETER_CHECKBOX_RIGHT);

    multiple_left_nozzle_flow_checkbox->SetId(ID_NOZZLE_FLOW_CHECKBOX_LEFT);
    multiple_right_nozzle_flow_checkbox->SetId(ID_NOZZLE_FLOW_CHECKBOX_RIGHT);
}

PrinterPartsDialog::~PrinterPartsDialog()
{
    nozzle_type_checkbox->Disconnect(wxEVT_COMBOBOX, wxCommandEventHandler(PrinterPartsDialog::set_nozzle_data), NULL, this);
    nozzle_diameter_checkbox->Disconnect(wxEVT_COMBOBOX, wxCommandEventHandler(PrinterPartsDialog::set_nozzle_data), NULL, this);
    multiple_left_nozzle_type_checkbox->Disconnect(wxEVT_COMBOBOX, wxCommandEventHandler(PrinterPartsDialog::set_nozzle_data), NULL, this);
    multiple_left_nozzle_diameter_checkbox->Disconnect(wxEVT_COMBOBOX, wxCommandEventHandler(PrinterPartsDialog::set_nozzle_data), NULL, this);
    multiple_left_nozzle_flow_checkbox->Disconnect(wxEVT_COMBOBOX, wxCommandEventHandler(PrinterPartsDialog::set_nozzle_data), NULL, this);

    multiple_right_nozzle_type_checkbox->Disconnect(wxEVT_COMBOBOX, wxCommandEventHandler(PrinterPartsDialog::set_nozzle_data), NULL, this);
    multiple_right_nozzle_diameter_checkbox->Disconnect(wxEVT_COMBOBOX, wxCommandEventHandler(PrinterPartsDialog::set_nozzle_data), NULL, this);
    multiple_right_nozzle_flow_checkbox->Disconnect(wxEVT_COMBOBOX, wxCommandEventHandler(PrinterPartsDialog::set_nozzle_data), NULL, this);
}

void PrinterPartsDialog::set_nozzle_data(wxCommandEvent& evt)
{
    ComboBox* current_nozzle_type_combox = nullptr;
    ComboBox* current_nozzle_diameter_combox = nullptr;

    int nozzle_id = MAIN_NOZZLE_ID;

    if (evt.GetId() == ID_NOZZLE_TYPE_CHECKBOX_SINGLE ||
        evt.GetId() == ID_NOZZLE_DIAMETER_CHECKBOX_SINGLE) {
        current_nozzle_type_combox = nozzle_type_checkbox;
        current_nozzle_diameter_combox = nozzle_diameter_checkbox;
        nozzle_id = MAIN_NOZZLE_ID;
    }


    if (obj) {
        try {
            auto nozzle_type        = NozzleType::ntHardenedSteel;
            auto nozzle_diameter    = 0.4f;
            auto nozzle_flow        = NozzleFlowType::NONE_FLOWTYPE;

            for (auto sm : nozzle_type_selection_map) {
                if (sm.second == current_nozzle_type_combox->GetSelection()) {
                    nozzle_type = sm.first;
                }
            }


            /*update nozzle diameter*/
            if (evt.GetId() == ID_NOZZLE_TYPE_CHECKBOX_SINGLE) {
                nozzle_diameter_checkbox->Clear();
                std::map<int, float> diameter_map;
                if (nozzle_type == NozzleType::ntHardenedSteel) {
                    diameter_map = nozzle_hard_diameter_map;
                } else if (nozzle_type == NozzleType::ntStainlessSteel) {
                    diameter_map = nozzle_stainless_diameter_map;
                }

                for (int i = 0; i < diameter_map.size(); i++) { nozzle_diameter_checkbox->Append(wxString::Format(_L("%.1f"), diameter_map[i])); }
                nozzle_diameter_checkbox->SetSelection(0);
            }

            nozzle_diameter = std::stof(current_nozzle_diameter_combox->GetStringSelection().ToStdString());
            nozzle_diameter = round(nozzle_diameter * 10) / 10;

            if (!obj->is_enable_np) {
                obj->m_extder_data.extders[MAIN_NOZZLE_ID].current_nozzle_diameter = nozzle_diameter;
                obj->m_extder_data.extders[MAIN_NOZZLE_ID].current_nozzle_type     = nozzle_type;
                obj->command_set_printer_nozzle(NozzleTypeEumnToStr[nozzle_type], nozzle_diameter);
            }
        } catch (...) {}
    }
}

void PrinterPartsDialog::on_dpi_changed(const wxRect& suggested_rect)
{
     Fit();
}

void PrinterPartsDialog::update_machine_obj(MachineObject* obj_)
{
    if (!obj_)
    {
        return;
    }
    obj = obj_;

    nozzle_stainless_diameter_map.clear();
    if (obj->is_series_o())
    {
        /*STUDIO-10089 there are only 0.2 stainless nozzle in O series*/
        nozzle_stainless_diameter_map[0] = 0.2;
    }
    else
    {
        nozzle_stainless_diameter_map[0] = 0.2;
        nozzle_stainless_diameter_map[1] = 0.4;
    }
}

bool PrinterPartsDialog::Show(bool show)
{
    if (show) {
        wxGetApp().UpdateDlgDarkUI(this);
        CentreOnParent();

        /*disable editing*/
        EnableEditing(DeviceManager::get_printer_can_set_nozzle(obj->printer_type));

        if (obj->m_extder_data.extders.size() <= 1) {
            single_panel->Show();
            multiple_panel->Hide();

            auto type     = obj->m_extder_data.extders[MAIN_NOZZLE_ID].current_nozzle_type;
            auto diameter = obj->m_extder_data.extders[MAIN_NOZZLE_ID].current_nozzle_diameter;

            nozzle_diameter_checkbox->Clear();

            if (type ==  NozzleType::ntUndefine) {
                nozzle_type_checkbox->SetValue(wxEmptyString);
                nozzle_diameter_checkbox->SetValue(wxEmptyString);
            } else {
                std::map<int, float> diameter_map;
                if (type == NozzleType::ntHardenedSteel) {
                    diameter_map = nozzle_hard_diameter_map;
                } else if (type == NozzleType::ntStainlessSteel) {
                    diameter_map = nozzle_stainless_diameter_map;
                }

                for (int i = 0; i < diameter_map.size(); i++) {
                    nozzle_diameter_checkbox->Append(wxString::Format(_L("%.1f"), diameter_map[i]));
                    if (diameter == diameter_map[i]) {
                        nozzle_diameter_checkbox->SetSelection(i);
                    }
                }

                nozzle_type_checkbox->SetSelection(nozzle_type_selection_map[type]);
            }

        } else {
            single_panel->Hide();
            multiple_panel->Show();

            //left
            auto type      = obj->m_extder_data.extders[DEPUTY_NOZZLE_ID].current_nozzle_type;
            auto diameter  = obj->m_extder_data.extders[DEPUTY_NOZZLE_ID].current_nozzle_diameter;
            auto flow_type = obj->m_extder_data.extders[DEPUTY_NOZZLE_ID].current_nozzle_flow_type;

            multiple_left_nozzle_diameter_checkbox->Clear();

            if (type == NozzleType::ntUndefine)
            {
                multiple_left_nozzle_type_checkbox->SetValue(wxEmptyString);
                multiple_left_nozzle_diameter_checkbox->SetValue(wxEmptyString);
                multiple_left_nozzle_flow_checkbox->SetValue(wxEmptyString);
            }
            else
            {
                std::map<int, float> diameter_map;
                if (type == NozzleType::ntHardenedSteel)
                {
                    diameter_map = nozzle_hard_diameter_map;
                }
                else if (type == NozzleType::ntStainlessSteel)
                {
                    diameter_map = nozzle_stainless_diameter_map;
                }

                for (int i = 0; i < diameter_map.size(); i++)
                {
                    multiple_left_nozzle_diameter_checkbox->Append(wxString::Format(_L("%.1f"), diameter_map[i]));
                    if (diameter == diameter_map[i])
                    {
                        multiple_left_nozzle_diameter_checkbox->SetSelection(i);
                    }
                }

                multiple_left_nozzle_type_checkbox->SetSelection(nozzle_type_selection_map[type]);
                if (flow_type != NozzleFlowType::NONE_FLOWTYPE) {multiple_left_nozzle_flow_checkbox->SetSelection(nozzle_flow_selection_map[flow_type]);}
            }

            //right
            type      = obj->m_extder_data.extders[MAIN_NOZZLE_ID].current_nozzle_type;
            diameter  = obj->m_extder_data.extders[MAIN_NOZZLE_ID].current_nozzle_diameter;
            flow_type = obj->m_extder_data.extders[MAIN_NOZZLE_ID].current_nozzle_flow_type;

            multiple_right_nozzle_diameter_checkbox->Clear();

            if (type == NozzleType::ntUndefine)
            {
                multiple_right_nozzle_type_checkbox->SetValue(wxEmptyString);
                multiple_right_nozzle_diameter_checkbox->SetValue(wxEmptyString);
                multiple_right_nozzle_flow_checkbox->SetValue(wxEmptyString);
            }
            else
            {
                std::map<int, float> diameter_map;
                if (type == NozzleType::ntHardenedSteel)
                {
                    diameter_map = nozzle_hard_diameter_map;
                }
                else if (type == NozzleType::ntStainlessSteel)
                {
                    diameter_map = nozzle_stainless_diameter_map;
                }

                for (int i = 0; i < diameter_map.size(); i++)
                {
                    multiple_right_nozzle_diameter_checkbox->Append(wxString::Format(_L("%.1f"), diameter_map[i]));
                    if (diameter == diameter_map[i])
                    {
                        multiple_right_nozzle_diameter_checkbox->SetSelection(i);
                    }
                }

                multiple_right_nozzle_type_checkbox->SetSelection(nozzle_type_selection_map[type]);
                if (flow_type != NozzleFlowType::NONE_FLOWTYPE) { multiple_right_nozzle_flow_checkbox->SetSelection(nozzle_flow_selection_map[flow_type]); };
            }
        }

        Layout();
        Fit();
    }
    return DPIDialog::Show(show);
}

void PrinterPartsDialog::EnableEditing(bool enable) {

    nozzle_type_checkbox->Enable(enable);
    nozzle_diameter_checkbox->Enable(enable);

    multiple_left_nozzle_type_checkbox->Enable(enable);
    multiple_left_nozzle_diameter_checkbox->Enable(enable);
    multiple_left_nozzle_flow_checkbox->Enable(enable);

    multiple_right_nozzle_type_checkbox->Enable(enable);
    multiple_right_nozzle_diameter_checkbox->Enable(enable);
    multiple_right_nozzle_flow_checkbox->Enable(enable);

    change_nozzle_tips->Show(!enable);
    multiple_change_nozzle_tips->Show(!enable);
}
}} // namespace Slic3r::GUI
