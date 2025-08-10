#include "CaliHistoryDialog.hpp"
#include "I18N.hpp"

#include "libslic3r/Utils.hpp"
#include "GUI.hpp"
#include "GUI_App.hpp"
#include "format.hpp"
#include "MsgDialog.hpp"
#include "slic3r/Utils/CalibUtils.hpp"

namespace Slic3r {
namespace GUI {


#define HISTORY_WINDOW_SIZE                wxSize(FromDIP(700), FromDIP(600))
#define EDIT_HISTORY_DIALOG_INPUT_SIZE     wxSize(FromDIP(160), FromDIP(24))
#define NEW_HISTORY_DIALOG_INPUT_SIZE      wxSize(FromDIP(250), FromDIP(24))
#define HISTORY_WINDOW_ITEMS_COUNT         5

static wxString get_preset_name_by_filament_id(std::string filament_id)
{
    auto preset_bundle = wxGetApp().preset_bundle;
    auto collection = &preset_bundle->filaments;
    wxString preset_name = "";
    for (auto it = preset_bundle->filaments.begin(); it != preset_bundle->filaments.end(); it++) {
        if (filament_id.compare(it->filament_id) == 0) {
            auto preset_parent = collection->get_preset_parent(*it);
            if (preset_parent) {
                if (preset_parent->is_system) {
                    if (!preset_parent->alias.empty())
                        preset_name = from_u8(preset_parent->alias);
                    else
                        preset_name = from_u8(preset_parent->name);
                }
                else { // is custom created filament
                    std::string name_str = preset_parent->name;
                    preset_name = from_u8(name_str.substr(0, name_str.find(" @")));
                }
            }
            else {
                if (it->is_system) {
                    if (!it->alias.empty())
                        preset_name = from_u8(it->alias);
                    else
                        preset_name = from_u8(it->name);
                }
                else { // is custom created filament
                    std::string name_str = it->name;
                    preset_name = from_u8(name_str.substr(0, name_str.find(" @")));
                }
            }
        }
    }
    return preset_name;
}

HistoryWindow::HistoryWindow(wxWindow* parent, const std::vector<PACalibResult>& calib_results_history, bool& show)
    : DPIDialog(parent, wxID_ANY, _L("Flow Dynamics Calibration Result"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_calib_results_history(calib_results_history)
    , m_show_history_dialog(show)
{
    this->SetBackgroundColour(*wxWHITE);
    auto main_sizer = new wxBoxSizer(wxVERTICAL);

    auto scroll_window = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxHSCROLL | wxVSCROLL);
    scroll_window->SetScrollRate(5, 5);
    scroll_window->SetBackgroundColour(*wxWHITE);
    scroll_window->SetMinSize(HISTORY_WINDOW_SIZE);
    scroll_window->SetSize(HISTORY_WINDOW_SIZE);
    scroll_window->SetMaxSize(HISTORY_WINDOW_SIZE);

    auto scroll_sizer = new wxBoxSizer(wxVERTICAL);
    scroll_window->SetSizer(scroll_sizer);

    Button *   mew_btn = new Button(scroll_window, _L("New"));
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(105, 75, 124), StateColor::Normal));
    mew_btn->SetBackgroundColour(*wxWHITE);
    mew_btn->SetBackgroundColor(btn_bg_green);
    mew_btn->SetBorderColor(wxColour(105, 75, 124));
    mew_btn->SetTextColor(wxColour("#FFFFFE"));
    mew_btn->SetMinSize(wxSize(FromDIP(100), FromDIP(24)));
    mew_btn->SetMaxSize(wxSize(FromDIP(100), FromDIP(24)));
    mew_btn->SetCornerRadius(FromDIP(12));
    mew_btn->Bind(wxEVT_BUTTON, &HistoryWindow::on_click_new_button, this);

    scroll_sizer->Add(mew_btn, 0, wxLEFT, FromDIP(20));
    scroll_sizer->AddSpacer(FromDIP(15));

    wxPanel* comboBox_panel = new wxPanel(scroll_window);
    comboBox_panel->SetBackgroundColour(wxColour(238, 238, 238));
    auto comboBox_sizer = new wxBoxSizer(wxVERTICAL);
    comboBox_panel->SetSizer(comboBox_sizer);
    comboBox_sizer->AddSpacer(10);

    auto nozzle_dia_title = new Label(comboBox_panel, _L("Nozzle Diameter"));
    nozzle_dia_title->SetFont(Label::Head_14);
    comboBox_sizer->Add(nozzle_dia_title, 0, wxLEFT | wxRIGHT, FromDIP(15));
    comboBox_sizer->AddSpacer(10);

    m_comboBox_nozzle_dia = new ComboBox(comboBox_panel, wxID_ANY, "", wxDefaultPosition, wxSize(-1, FromDIP(24)), 0, nullptr, wxCB_READONLY);
    comboBox_sizer->Add(m_comboBox_nozzle_dia, 0, wxLEFT | wxEXPAND | wxRIGHT, FromDIP(15));
    comboBox_sizer->AddSpacer(10);

    scroll_sizer->Add(comboBox_panel, 0, wxEXPAND | wxRIGHT, FromDIP(10));

    scroll_sizer->AddSpacer(FromDIP(15));

    wxPanel* tips_panel = new wxPanel(scroll_window, wxID_ANY);
    tips_panel->SetBackgroundColour(*wxWHITE);
    auto tips_sizer = new wxBoxSizer(wxVERTICAL);
    tips_panel->SetSizer(tips_sizer);
    m_tips = new Label(tips_panel, "");
    m_tips->SetForegroundColour({ 145, 145, 145 });
    tips_sizer->Add(m_tips, 0, wxEXPAND);

    scroll_sizer->Add(tips_panel, 0, wxEXPAND);

    scroll_sizer->AddSpacer(FromDIP(15));

    m_history_data_panel = new wxPanel(scroll_window);
    m_history_data_panel->SetBackgroundColour(*wxWHITE);

    scroll_sizer->Add(m_history_data_panel, 1, wxEXPAND);

    main_sizer->Add(scroll_window, 1, wxEXPAND | wxALL, FromDIP(10));

    SetSizer(main_sizer);
    Layout();
    main_sizer->Fit(this);
    CenterOnParent();

    wxGetApp().UpdateDlgDarkUI(this);

    m_comboBox_nozzle_dia->Bind(wxEVT_COMBOBOX, &HistoryWindow::on_select_nozzle, this);

    m_refresh_timer = new wxTimer();
    m_refresh_timer->SetOwner(this);
    m_refresh_timer->Start(200);
    Bind(wxEVT_TIMER, &HistoryWindow::on_timer, this);

    m_show_history_dialog = true;
}

HistoryWindow::~HistoryWindow()
{
    m_refresh_timer->Stop();
    m_show_history_dialog = false;
}

void HistoryWindow::sync_history_result(MachineObject* obj)
{
    BOOST_LOG_TRIVIAL(info) << "sync_history_result";

    m_calib_results_history.clear();
    if (obj)
        m_calib_results_history = obj->pa_calib_tab;

    if (m_calib_results_history.empty()) {
        m_tips->SetLabel(_L("No History Result"));
        return;
    }
    else {
        m_tips->SetLabel(_L("Success to get history result"));
    }
    m_tips->Refresh();

    sync_history_data();
}

void HistoryWindow::on_device_connected(MachineObject* obj)
{
    if (!obj) {
        return;
    }

    curr_obj = obj;
    // init nozzle value
    static std::array<float, 4> nozzle_diameter_list = { 0.2f, 0.4f, 0.6f, 0.8f };
    int selection = 1;
    for (int i = 0; i < nozzle_diameter_list.size(); i++) {
        m_comboBox_nozzle_dia->AppendString(wxString::Format("%1.1f mm", nozzle_diameter_list[i]));
        if (abs(curr_obj->nozzle_diameter - nozzle_diameter_list[i]) < 1e-3) {
            selection = i;
        }
    }
    m_comboBox_nozzle_dia->SetSelection(selection);

    // trigger on_select nozzle
    wxCommandEvent evt(wxEVT_COMBOBOX);
    evt.SetEventObject(m_comboBox_nozzle_dia);
    wxPostEvent(m_comboBox_nozzle_dia, evt);
}

void HistoryWindow::on_timer(wxTimerEvent& event)
{
    update(curr_obj);
}

void HistoryWindow::update(MachineObject* obj)
{
    if (!obj) return;

    if (obj->cali_version != history_version) {
        if (obj->has_get_pa_calib_tab) {
            history_version = obj->cali_version;
            reqeust_history_result(obj);
        }
    }

    // sync when history is not empty
    if (obj->has_get_pa_calib_tab && m_calib_results_history.empty()) {
        sync_history_result(curr_obj);
    }
}

void HistoryWindow::on_select_nozzle(wxCommandEvent& evt)
{
    reqeust_history_result(curr_obj);

}

void HistoryWindow::reqeust_history_result(MachineObject* obj)
{
    if (curr_obj) {
        // reset
        curr_obj->reset_pa_cali_history_result();
        m_calib_results_history.clear();
        sync_history_data();

        float nozzle_value = get_nozzle_value();
        if (nozzle_value > 0) {
            CalibUtils::emit_get_PA_calib_infos(nozzle_value);
            m_tips->SetLabel(_L("Refreshing the historical Flow Dynamics Calibration records"));
            BOOST_LOG_TRIVIAL(info) << "request calib history";
        }
    }
}

void HistoryWindow::enbale_action_buttons(bool enable) {
    auto childern = m_history_data_panel->GetChildren();
    for (auto child : childern) {
        auto button = dynamic_cast<Button*>(child);
        if (button) {
            button->Enable(enable);
        }
    }
}

void HistoryWindow::sync_history_data() {
    Freeze();
    m_history_data_panel->DestroyChildren();
    m_history_data_panel->Enable();
    wxGridBagSizer* gbSizer;
    gbSizer = new wxGridBagSizer(FromDIP(0), FromDIP(50));
    gbSizer->SetFlexibleDirection(wxBOTH);
    gbSizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    m_history_data_panel->SetSizer(gbSizer, true);

    auto title_name = new Label(m_history_data_panel, _L("Name"));
    title_name->SetFont(Label::Head_14);
    gbSizer->Add(title_name, { 0, 0 }, { 1, 1 }, wxBOTTOM, FromDIP(15));

    auto title_preset_name = new Label(m_history_data_panel, _L("Filament"));
    title_preset_name->SetFont(Label::Head_14);
    gbSizer->Add(title_preset_name, { 0, 1 }, { 1, 1 }, wxBOTTOM, FromDIP(15));

    auto title_k = new Label(m_history_data_panel, _L("Factor K"));
    title_k->SetFont(Label::Head_14);
    gbSizer->Add(title_k, { 0, 2 }, { 1, 1 }, wxBOTTOM, FromDIP(15));

    // Hide
    //auto title_n = new Label(m_history_data_panel, wxID_ANY, _L("N"));
    //title_n->SetFont(Label::Head_14);
    //gbSizer->Add(title_n, { 0, 3 }, { 1, 1 }, wxBOTTOM, FromDIP(15));

    auto title_action = new Label(m_history_data_panel, _L("Action"));
    title_action->SetFont(Label::Head_14);
    gbSizer->Add(title_action, { 0, 3 }, { 1, 1 });

    int i = 1;
    for (auto& result : m_calib_results_history) {
        auto name_value = new Label(m_history_data_panel, from_u8(result.name));

        wxString preset_name = get_preset_name_by_filament_id(result.filament_id);
        auto preset_name_value = new Label(m_history_data_panel, preset_name);

        auto k_str = wxString::Format("%.3f", result.k_value);
        auto n_str = wxString::Format("%.3f", result.n_coef);
        auto k_value = new Label(m_history_data_panel, k_str);
        auto n_value = new Label(m_history_data_panel, n_str);
        n_value->Hide();
        auto delete_button = new Button(m_history_data_panel, _L("Delete"));
        delete_button->SetBackgroundColour(*wxWHITE);
        delete_button->SetMinSize(wxSize(-1, FromDIP(24)));
        delete_button->SetCornerRadius(FromDIP(12));
        delete_button->Bind(wxEVT_BUTTON, [this, gbSizer, i, &result](auto& e) {
            for (int j = 0; j < HISTORY_WINDOW_ITEMS_COUNT; j++) {
                auto item = gbSizer->FindItemAtPosition({ i, j });
                item->GetWindow()->Hide();
            }
            gbSizer->SetEmptyCellSize({ 0,0 });
            m_history_data_panel->Layout();
            m_history_data_panel->Fit();
            CalibUtils::delete_PA_calib_result({ result.tray_id, result.cali_idx, result.nozzle_diameter, result.filament_id });
            });

        auto edit_button = new Button(m_history_data_panel, _L("Edit"));
        StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
            std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
            std::pair<wxColour, int>(wxColour(105, 75, 124), StateColor::Normal));
        edit_button->SetBackgroundColour(*wxWHITE);
        edit_button->SetBackgroundColor(btn_bg_green);
        edit_button->SetBorderColor(wxColour(105, 75, 124));
        edit_button->SetTextColor(wxColour("#FFFFFE"));
        edit_button->SetMinSize(wxSize(-1, FromDIP(24)));
        edit_button->SetCornerRadius(FromDIP(12));
        edit_button->Bind(wxEVT_BUTTON, [this, result, k_value, name_value, edit_button](auto& e) {
            PACalibResult result_buffer = result;
            result_buffer.k_value = stof(k_value->GetLabel().ToStdString());
            result_buffer.name = name_value->GetLabel().ToUTF8().data();
            EditCalibrationHistoryDialog dlg(this, result_buffer);
            if (dlg.ShowModal() == wxID_OK) {
                auto new_result = dlg.get_result();

                wxString new_k_str = wxString::Format("%.3f", new_result.k_value);
                k_value->SetLabel(new_k_str);
                name_value->SetLabel(from_u8(new_result.name));

                new_result.tray_id = -1;
                CalibUtils::set_PA_calib_result({ new_result }, true);

                enbale_action_buttons(false);
            }
            });

        gbSizer->Add(name_value, { i, 0 }, { 1, 1 }, wxBOTTOM, FromDIP(15));
        gbSizer->Add(preset_name_value, { i, 1 }, { 1, 1 }, wxBOTTOM, FromDIP(15));
        gbSizer->Add(k_value, { i, 2 }, { 1, 1 }, wxBOTTOM, FromDIP(15));
        //gbSizer->Add(n_value, { i, 3 }, { 1, 1 }, wxBOTTOM, FromDIP(15));
        gbSizer->Add(delete_button, { i, 3 }, { 1, 1 }, wxBOTTOM, FromDIP(15));
        gbSizer->Add(edit_button, { i, 4 }, { 1, 1 }, wxBOTTOM, FromDIP(15));
        i++;
    }

    wxGetApp().UpdateDlgDarkUI(this);

    Layout();
    Fit();
    Thaw();
}

float HistoryWindow::get_nozzle_value()
{
    double nozzle_value = 0.0;
    wxString nozzle_value_str = m_comboBox_nozzle_dia->GetValue();
    try {
        nozzle_value_str.ToDouble(&nozzle_value);
    }
    catch (...) {
        ;
    }

    return nozzle_value;
}

void HistoryWindow::on_click_new_button(wxCommandEvent& event)
{
    if (curr_obj && curr_obj->get_printer_series() == PrinterSeries::SERIES_P1P && m_calib_results_history.size() >= 16) {
        MessageDialog msg_dlg(nullptr, wxString::Format(_L("This machine type can only hold %d history results per nozzle."), 16), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    NewCalibrationHistoryDialog dlg(this, m_calib_results_history);
    dlg.ShowModal();
}

EditCalibrationHistoryDialog::EditCalibrationHistoryDialog(wxWindow* parent, const PACalibResult& result)
    : DPIDialog(parent, wxID_ANY, _L("Edit Flow Dynamics Calibration"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_new_result(result)
{
    this->SetBackgroundColour(*wxWHITE);
    auto main_sizer = new wxBoxSizer(wxVERTICAL);

    auto top_panel = new wxPanel(this);
    top_panel->SetBackgroundColour(*wxWHITE);
    auto panel_sizer = new wxBoxSizer(wxVERTICAL);
    top_panel->SetSizer(panel_sizer);

    auto flex_sizer = new wxFlexGridSizer(0, 2, FromDIP(15), FromDIP(30));
    flex_sizer->SetFlexibleDirection(wxBOTH);
    flex_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    Label* name_title = new Label(top_panel, _L("Name"));
    m_name_value = new TextInput(top_panel, from_u8(m_new_result.name), "", "", wxDefaultPosition, EDIT_HISTORY_DIALOG_INPUT_SIZE, wxTE_PROCESS_ENTER);

    flex_sizer->Add(name_title);
    flex_sizer->Add(m_name_value);

    Label* preset_name_title = new Label(top_panel, _L("Filament"));
    wxString preset_name = get_preset_name_by_filament_id(result.filament_id);
    Label* preset_name_value = new Label(top_panel, preset_name);
    flex_sizer->Add(preset_name_title);
    flex_sizer->Add(preset_name_value);

    Label* k_title = new Label(top_panel, _L("Factor K"));
    auto k_str = wxString::Format("%.3f", m_new_result.k_value);
    m_k_value = new TextInput(top_panel, k_str, "", "", wxDefaultPosition, EDIT_HISTORY_DIALOG_INPUT_SIZE, wxTE_PROCESS_ENTER);
    flex_sizer->Add(k_title);
    flex_sizer->Add(m_k_value);

    // Hide:
    //Label* n_title = new Label(top_panel, _L("Factor N"));
    //TextInput* n_value = new TextInput(top_panel, n, "", "", wxDefaultPosition, EDIT_HISTORY_DIALOG_INPUT_SIZE, wxTE_PROCESS_ENTER);
    //flex_sizer->Add(n_title);
    //flex_sizer->Add(n_value);

    panel_sizer->Add(flex_sizer);

    panel_sizer->AddSpacer(FromDIP(25));

    auto btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    Button* save_btn = new Button(top_panel, _L("Save"));
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed),
        std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(105, 75, 124), StateColor::Normal));
    save_btn->SetBackgroundColour(*wxWHITE);
    save_btn->SetBackgroundColor(btn_bg_green);
    save_btn->SetBorderColor(wxColour(105, 75, 124));
    save_btn->SetTextColor(wxColour("#FFFFFE"));
    save_btn->SetMinSize(wxSize(-1, FromDIP(24)));
    save_btn->SetCornerRadius(FromDIP(12));
    Button* cancel_btn = new Button(top_panel, _L("Cancel"));
    cancel_btn->SetBackgroundColour(*wxWHITE);
    cancel_btn->SetMinSize(wxSize(-1, FromDIP(24)));
    cancel_btn->SetCornerRadius(FromDIP(12));
    save_btn->Bind(wxEVT_BUTTON, &EditCalibrationHistoryDialog::on_save, this);
    cancel_btn->Bind(wxEVT_BUTTON, &EditCalibrationHistoryDialog::on_cancel, this);
    btn_sizer->AddStretchSpacer();
    btn_sizer->Add(save_btn);
    btn_sizer->AddSpacer(FromDIP(20));
    btn_sizer->Add(cancel_btn);
    panel_sizer->Add(btn_sizer, 0, wxEXPAND, 0);


    main_sizer->Add(top_panel, 1, wxEXPAND | wxALL, FromDIP(20));

    SetSizer(main_sizer);
    Layout();
    Fit();
    CenterOnParent();

    wxGetApp().UpdateDlgDarkUI(this);
}

EditCalibrationHistoryDialog::~EditCalibrationHistoryDialog() {
}

PACalibResult EditCalibrationHistoryDialog::get_result() {
    return m_new_result;
}

void EditCalibrationHistoryDialog::on_save(wxCommandEvent& event) {
    wxString name = m_name_value->GetTextCtrl()->GetValue();
    if (!CalibUtils::validate_input_name(name))
        return;

    m_new_result.name = m_name_value->GetTextCtrl()->GetValue().ToUTF8().data();

    float k = 0.0f;
    if (!CalibUtils::validate_input_k_value(m_k_value->GetTextCtrl()->GetValue(), &k)) {
        MessageDialog msg_dlg(nullptr, wxString::Format(_L("Please input a valid value (K in %.1f~%.1f)"), MIN_PA_K_VALUE, MAX_PA_K_VALUE), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }
    wxString k_str = wxString::Format("%.3f", k);
    m_k_value->GetTextCtrl()->SetValue(k_str);
    m_new_result.k_value = k;


    EndModal(wxID_OK);
}

void EditCalibrationHistoryDialog::on_cancel(wxCommandEvent& event) {
    EndModal(wxID_CANCEL);
}

void EditCalibrationHistoryDialog::on_dpi_changed(const wxRect& suggested_rect)
{
}

wxArrayString NewCalibrationHistoryDialog::get_all_filaments(const MachineObject *obj)
{
    PresetBundle *preset_bundle = wxGetApp().preset_bundle;

    wxArrayString         filament_items;
    std::set<std::string> filament_id_set;
    std::set<std::string> printer_names;
    std::ostringstream    stream;
    stream << std::fixed << std::setprecision(1) << obj->nozzle_diameter;
    std::string nozzle_diameter_str = stream.str();

    for (auto printer_it = preset_bundle->printers.begin(); printer_it != preset_bundle->printers.end(); printer_it++) {
        // filter by system preset
        if (!printer_it->is_system)
            continue;
        // get printer_model
        ConfigOption *      printer_model_opt = printer_it->config.option("printer_model");
        ConfigOptionString *printer_model_str = dynamic_cast<ConfigOptionString *>(printer_model_opt);
        if (!printer_model_str)
            continue;

        // use printer_model as printer type
        if (printer_model_str->value != MachineObject::get_preset_printer_model_name(obj->printer_type))
            continue;

        if (printer_it->name.find(nozzle_diameter_str) != std::string::npos)
            printer_names.insert(printer_it->name);
    }

    if (preset_bundle) {
        BOOST_LOG_TRIVIAL(trace) << "system_preset_bundle filament number=" << preset_bundle->filaments.size();
        for (auto filament_it = preset_bundle->filaments.begin(); filament_it != preset_bundle->filaments.end(); filament_it++) {
            // filter by system preset
            Preset &preset = *filament_it;
            /*The situation where the user preset is not displayed is as follows:
                1. Not a root preset
                2. Not system preset and the printer firmware does not support user preset */
            if (preset_bundle->filaments.get_preset_base(*filament_it) != &preset || (!filament_it->is_system && ! obj->is_support_user_preset)) { continue; }

            ConfigOption *       printer_opt  = filament_it->config.option("compatible_printers");
            ConfigOptionStrings *printer_strs = dynamic_cast<ConfigOptionStrings *>(printer_opt);
            for (auto printer_str : printer_strs->values) {
                if (printer_names.find(printer_str) != printer_names.end()) {
                    if (filament_id_set.find(filament_it->filament_id) != filament_id_set.end()) {
                        continue;
                    } else {
                        filament_id_set.insert(filament_it->filament_id);
                        // name matched
                        if (filament_it->is_system) {
                            filament_items.push_back(filament_it->alias);
                            FilamentInfos filament_infos;
                            filament_infos.filament_id             = filament_it->filament_id;
                            filament_infos.setting_id              = filament_it->setting_id;
                            map_filament_items[filament_it->alias] = filament_infos;
                        } else {
                            char   target = '@';
                            size_t pos    = filament_it->name.find(target);
                            if (pos != std::string::npos) {
                                std::string user_preset_alias    = filament_it->name.substr(0, pos - 1);
                                wxString    wx_user_preset_alias = wxString(user_preset_alias.c_str(), wxConvUTF8);
                                user_preset_alias                = wx_user_preset_alias.ToStdString();

                                filament_items.push_back(user_preset_alias);
                                FilamentInfos filament_infos;
                                filament_infos.filament_id            = filament_it->filament_id;
                                filament_infos.setting_id             = filament_it->setting_id;
                                map_filament_items[user_preset_alias] = filament_infos;
                            }
                        }
                    }
                }
            }
        }
    }
    return filament_items;
}

NewCalibrationHistoryDialog::NewCalibrationHistoryDialog(wxWindow *parent, const std::vector<PACalibResult> history_results)
    : DPIDialog(parent, wxID_ANY, _L("New Flow Dynamic Calibration"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
    , m_history_results(history_results)
{
    Slic3r::DeviceManager *dev = Slic3r::GUI::wxGetApp().getDeviceManager();
    if (!dev)
        return;
    MachineObject *obj = dev->get_selected_machine();
    if (!obj)
        return;

    curr_obj = obj;

    this->SetBackgroundColour(*wxWHITE);
    auto main_sizer = new wxBoxSizer(wxVERTICAL);

    auto top_panel = new wxPanel(this);
    top_panel->SetBackgroundColour(*wxWHITE);
    auto panel_sizer = new wxBoxSizer(wxVERTICAL);
    top_panel->SetSizer(panel_sizer);

    auto flex_sizer = new wxFlexGridSizer(0, 2, FromDIP(15), FromDIP(30));
    flex_sizer->SetFlexibleDirection(wxBOTH);
    flex_sizer->SetNonFlexibleGrowMode(wxFLEX_GROWMODE_SPECIFIED);

    Label *name_title = new Label(top_panel, _L("Name"));
    m_name_value      = new TextInput(top_panel, "", "", "", wxDefaultPosition, NEW_HISTORY_DIALOG_INPUT_SIZE, wxTE_PROCESS_ENTER);

    // Name
    flex_sizer->Add(name_title);
    flex_sizer->Add(m_name_value);

    Label *  preset_name_title = new Label(top_panel, _L("Filament"));
    m_comboBox_filament = new ::ComboBox(top_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, NEW_HISTORY_DIALOG_INPUT_SIZE, 0, nullptr, wxCB_READONLY);

    wxArrayString filament_items = get_all_filaments(obj);
    m_comboBox_filament->Set(filament_items);
    m_comboBox_filament->SetSelection(-1);

    // Filament
    flex_sizer->Add(preset_name_title);
    flex_sizer->Add(m_comboBox_filament);

    Label *nozzle_diameter_title = new Label(top_panel, _L("Nozzle Diameter"));
    m_comboBox_nozzle_diameter = new ::ComboBox(top_panel, wxID_ANY, wxEmptyString, wxDefaultPosition, NEW_HISTORY_DIALOG_INPUT_SIZE, 0, nullptr, wxCB_READONLY);
    static std::array<float, 4> nozzle_diameter_list = {0.2f, 0.4f, 0.6f, 0.8f};
    for (int i = 0; i < nozzle_diameter_list.size(); i++) {
        m_comboBox_nozzle_diameter->AppendString(wxString::Format("%1.1f mm", nozzle_diameter_list[i]));
        if (abs(obj->nozzle_diameter - nozzle_diameter_list[i]) < 1e-3) {
            m_comboBox_nozzle_diameter->SetSelection(i);
        }
    }

    // Nozzle Diameter
    flex_sizer->Add(nozzle_diameter_title);
    flex_sizer->Add(m_comboBox_nozzle_diameter);

    Label *k_title = new Label(top_panel, _L("Factor K"));
    auto   k_str   = wxString::Format("%.3f", m_new_result.k_value);
    m_k_value      = new TextInput(top_panel, k_str, "", "", wxDefaultPosition, NEW_HISTORY_DIALOG_INPUT_SIZE, wxTE_PROCESS_ENTER);

    // Factor K
    flex_sizer->Add(k_title);
    flex_sizer->Add(m_k_value);

    panel_sizer->Add(flex_sizer);

    panel_sizer->AddSpacer(FromDIP(25));

    auto       btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    Button *   ok_btn  = new Button(top_panel, _L("Ok"));
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(0, 137, 123), StateColor::Pressed), std::pair<wxColour, int>(wxColour(38, 166, 154), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(105, 75, 124), StateColor::Normal));
    ok_btn->SetBackgroundColour(*wxWHITE);
    ok_btn->SetBackgroundColor(btn_bg_green);
    ok_btn->SetBorderColor(wxColour(105, 75, 124));
    ok_btn->SetTextColor(wxColour("#FFFFFE"));
    ok_btn->SetMinSize(wxSize(-1, FromDIP(24)));
    ok_btn->SetCornerRadius(FromDIP(12));
    Button *cancel_btn = new Button(top_panel, _L("Cancel"));
    cancel_btn->SetBackgroundColour(*wxWHITE);
    cancel_btn->SetMinSize(wxSize(-1, FromDIP(24)));
    cancel_btn->SetCornerRadius(FromDIP(12));
    ok_btn->Bind(wxEVT_BUTTON, &NewCalibrationHistoryDialog::on_ok, this);
    cancel_btn->Bind(wxEVT_BUTTON, &NewCalibrationHistoryDialog::on_cancel, this);
    btn_sizer->AddStretchSpacer();
    btn_sizer->Add(ok_btn);
    btn_sizer->AddSpacer(FromDIP(20));
    btn_sizer->Add(cancel_btn);
    panel_sizer->Add(btn_sizer, 0, wxEXPAND, 0);

    main_sizer->Add(top_panel, 1, wxEXPAND | wxALL, FromDIP(20));

    SetSizer(main_sizer);
    Layout();
    Fit();
    CenterOnParent();

    wxGetApp().UpdateDlgDarkUI(this);
}

void NewCalibrationHistoryDialog::on_ok(wxCommandEvent &event)
{
    wxString name = m_name_value->GetTextCtrl()->GetValue();
    if (!CalibUtils::validate_input_name(name))
        return;

    float k = 0.0f;
    if (!CalibUtils::validate_input_k_value(m_k_value->GetTextCtrl()->GetValue(), &k)) {
        MessageDialog msg_dlg(nullptr, wxString::Format(_L("Please input a valid value (K in %.1f~%.1f)"), MIN_PA_K_VALUE, MAX_PA_K_VALUE), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }
    wxString k_str = wxString::Format("%.3f", k);
    m_k_value->GetTextCtrl()->SetValue(k_str);

    double   nozzle_value     = 0.0;
    wxString nozzle_value_str = m_comboBox_nozzle_diameter->GetValue();
    nozzle_value_str.ToDouble(&nozzle_value);

    std::string filament_name = m_comboBox_filament->GetValue().ToStdString();
    if (filament_name.empty()) {
        MessageDialog msg_dlg(nullptr, _L("The filament must be selected."), wxEmptyString, wxICON_WARNING | wxOK);
        msg_dlg.ShowModal();
        return;
    }

    auto filament_item = map_filament_items[m_comboBox_filament->GetValue().ToStdString()];
    std::string filament_id   = filament_item.filament_id;
    std::string setting_id    = filament_item.setting_id;

    m_new_result.name = name.ToUTF8().data();
    m_new_result.k_value  = k;
    m_new_result.tray_id = -1;
    m_new_result.cali_idx = -1;

    m_new_result.nozzle_diameter = nozzle_value;
    m_new_result.filament_id = filament_id;
    m_new_result.setting_id = setting_id;

    // Check for duplicate names from history
    {
        auto iter = std::find_if(m_history_results.begin(), m_history_results.end(), [this](const PACalibResult &item) {
            return item.name == m_new_result.name && item.filament_id == m_new_result.filament_id;
        });

        if (iter != m_history_results.end()) {
            MessageDialog msg_dlg(nullptr,
                                  wxString::Format(_L("There is already a historical calibration result with the same name: %s. Only one of the results with the same name "
                                                      "is saved. Are you sure you want to override the historical result?"),
                                                   m_new_result.name),
                                  wxEmptyString, wxICON_WARNING | wxYES_NO);
            if (msg_dlg.ShowModal() != wxID_YES)
                return;
        }
    }

    CalibUtils::set_PA_calib_result({m_new_result}, true);

    EndModal(wxID_OK);
}

void NewCalibrationHistoryDialog::on_cancel(wxCommandEvent &event)
{
    EndModal(wxID_CANCEL);
}

} // namespace GUI
} // namespace Slic3r
