#ifndef slic3r_GUI_CalibrationWizard_hpp_
#define slic3r_GUI_CalibrationWizard_hpp_

#include "DeviceManager.hpp"
#include "CalibrationWizardPage.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/AMSControl.hpp"
#include "AMSMaterialsSetting.hpp"
#include "Widgets/ProgressBar.hpp"
#include "SavePresetDialog.hpp"
#include "PresetComboBoxes.hpp"
#include "../slic3r/Utils/CalibUtils.hpp"

namespace Slic3r { namespace GUI {

wxDECLARE_EVENT(EVT_CALIBRATION_TRAY_SELECTION_CHANGED, SimpleEvent);
wxDECLARE_EVENT(EVT_CALIBRATION_NOTIFY_CHANGE_PAGES, SimpleEvent);
wxDECLARE_EVENT(EVT_CALIBRATION_TAB_CHANGED, wxCommandEvent);

enum FilamentSelectMode {
    FSMCheckBoxMode,
    FSMRadioMode
};
class FilamentComboBox : public wxPanel
{
public:
    FilamentComboBox(wxWindow* parent, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize);
    ~FilamentComboBox() {};

    void set_select_mode(FilamentSelectMode mode);
    FilamentSelectMode get_select_mode() { return m_mode; }
    void load_tray_from_ams(int id, DynamicPrintConfig& tray);
    void update_from_preset();
    int get_tray_id() { return m_tray_id; }
    bool is_bbl_filament() { return m_is_bbl_filamnet; }
    std::string get_tray_name() { return m_tray_name; }
    CalibrateFilamentComboBox* GetComboBox() { return m_comboBox; }
    CheckBox* GetCheckBox() { return m_checkBox; }
    void SetCheckBox(CheckBox* cb) { m_checkBox = cb; }
    wxRadioButton* GetRadioBox() { return m_radioBox; }
    void SetRadioBox(wxRadioButton* btn) { m_radioBox = btn; }
    virtual bool Show(bool show = true);
    virtual bool Enable(bool enable);
    virtual void SetValue(bool value);

protected:
    int m_tray_id;
    std::string m_tray_name;
    bool m_is_bbl_filamnet{ false };

    CheckBox* m_checkBox{nullptr};
    //RadioBox* m_radioBox;
    wxRadioButton* m_radioBox{ nullptr };
    CalibrateFilamentComboBox* m_comboBox{ nullptr };
    FilamentSelectMode m_mode{ FSMRadioMode };
};

typedef std::vector<FilamentComboBox*> FilamentComboBoxList;

class CalibrationWizard : public wxPanel {
public:
    CalibrationWizard(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~CalibrationWizard() {};
    CalibrationWizardPage* get_curr_page() { return m_curr_page; }
    CalibrationWizardPage* get_frist_page() { return m_first_page; }
    void show_page(CalibrationWizardPage* page);
    void show_send_progress_bar(bool show);
    void update_printer_selections();
    void update_print_progress();
    void update_filaments_from_preset();

protected:
    virtual void create_pages() = 0;
    virtual bool start_calibration(std::vector<int> tray_ids) = 0;
    virtual bool save_calibration_result() = 0;
    virtual bool recommend_input_value();
    virtual void request_calib_result() {};
    virtual void change_ams_select_mode() {};

protected:
    MachineObject* curr_obj{ nullptr };
    std::vector<MachineObject*> obj_list{ nullptr };

    wxScrolledWindow* m_scrolledWindow;
    wxBoxSizer* m_all_pages_sizer;

    CalibrationWizardPage* m_curr_page{ nullptr };
    CalibrationWizardPage* m_first_page{ nullptr };

    // preset panel
    wxPanel*  m_presets_panel;
    wxPanel* m_select_ams_mode_panel;
    //RadioBox* m_ams_radiobox;
    //RadioBox* m_ext_spool_radiobox;
    wxRadioButton* m_ams_radiobox;
    wxRadioButton* m_ext_spool_radiobox;
    bool m_filament_from_ext_spool{ false };
    wxPanel* m_muilti_ams_panel;
    std::vector<AMSItem*> m_ams_item_list;
    wxPanel* m_filament_list_panel;
    ScalableButton* m_ams_sync_button;
    FilamentComboBoxList m_filament_comboBox_list;
    wxPanel* m_virtual_panel;
    FilamentComboBox* m_virtual_tray_comboBox;
    ComboBox* m_comboBox_printer;
    ComboBox* m_comboBox_nozzle_dia;
    ComboBox* m_comboBox_bed_type;
    ComboBox* m_comboBox_process;
    Preset* m_printer_preset{nullptr};
    Preset* m_filament_preset{ nullptr };
    Preset* m_print_preset{ nullptr };
    wxStaticText* m_from_text;
    wxStaticText* m_to_text;
    wxStaticText* m_step_text;
    TextInput* m_from_value;
    TextInput* m_to_value;
    TextInput* m_step;
    TextInput* m_nozzle_temp;
    TextInput* m_bed_temp;
    TextInput* m_max_volumetric_speed;
    wxStaticText* m_filaments_incompatible_tips;
    wxStaticText* m_bed_type_incompatible_tips;
    wxPanel* m_send_progress_panel;
    std::shared_ptr<BBLStatusBarSend> m_send_progress_bar;  // for send 

    // print panel
    wxPanel* m_print_panel;
    wxStaticText* m_staticText_profile_value;
    wxStaticText* m_printing_stage_value;
    wxStaticText* m_staticText_progress_percent;
    wxStaticText* m_staticText_progress_left_time;
    wxStaticText* m_staticText_layers;
    ScalableButton* m_button_pause_resume;
    ScalableButton* m_button_abort;
    ProgressBar* m_print_gauge_progress; // for print
    PageButton* m_btn_next;
    PageButton* m_btn_recali;

    // save panel
    wxPanel* m_save_panel;

    void create_presets_panel(CalibrationWizardPage* page, wxBoxSizer* sizer, bool need_custom_range = true);
    void create_send_progress_bar(CalibrationWizardPage* page, wxBoxSizer* sizer);
    void init_presets_selections();
    void init_nozzle_selections();
    void init_bed_type_selections();
    void init_process_selections();
    int get_bed_temp(DynamicPrintConfig* config);
    FilamentSelectMode get_ams_select_mode() { if (!m_filament_comboBox_list.empty()) return m_filament_comboBox_list[0]->get_select_mode(); return FilamentSelectMode::FSMRadioMode; }
    void set_ams_select_mode(FilamentSelectMode mode);
    std::vector<int> get_selected_tray();
    FilamentComboBoxList get_selected_filament_comboBox();

    void create_print_panel(CalibrationWizardPage* page, wxBoxSizer* sizer);
    void reset_printing_values();

    void create_save_panel(CalibrationWizardPage* page, wxBoxSizer* sizer);
    virtual void create_save_panel_content(wxBoxSizer* sizer) {}
    bool save_presets(const std::string& config_key, ConfigOption* config_value, const std::string& name);

    // event handlers
    void on_select_printer(wxCommandEvent& evt);
    void on_select_nozzle(wxCommandEvent& evt);
    void on_select_tray(SimpleEvent& evt);
    void on_select_bed_type(wxCommandEvent& evt);
    void on_click_btn_prev(IntEvent& event);
    void on_click_btn_next(IntEvent& event);
    void on_subtask_abort(wxCommandEvent& event);
    void on_subtask_pause_resume(wxCommandEvent& event);
    void on_choose_ams(wxCommandEvent& event);
    void on_choose_ext_spool(wxCommandEvent& event);
    void on_update_ams_filament(bool dialog = true);
    void on_switch_ams(std::string ams_id);
};

class PressureAdvanceWizard : public CalibrationWizard{
public:
    PressureAdvanceWizard(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~PressureAdvanceWizard() {};
protected:
    void create_history_window();
    virtual void create_pages() override;
    virtual void create_save_panel_content(wxBoxSizer* sizer) override;
    virtual bool start_calibration(std::vector<int> tray_ids) override;
    virtual bool save_calibration_result() override;
    virtual bool recommend_input_value() override;
    virtual void request_calib_result() override;
    virtual void change_ams_select_mode() override;

    void sync_save_page_data();
    void switch_pages(SimpleEvent& evt);
private:
    // history page
    //CalibrationWizardPage* m_history_page{ nullptr };

    // start page
    CalibrationWizardPage* m_page0{ nullptr };

    // preset page
    CalibrationWizardPage* m_page1{ nullptr };

    // print page
    CalibrationWizardPage* m_page2{ nullptr };

    // save page
    CalibrationWizardPage* m_page3{ nullptr };
    wxPanel* m_low_end_save_panel;
    TextInput* m_k_val;
    TextInput* m_n_val;

    wxPanel* m_high_end_save_panel;
    std::vector<PACalibResult> m_calib_results;
    std::vector<PACalibResult> m_calib_results_history;
    wxPanel* m_grid_panel;
};

class FlowRateWizard : public CalibrationWizard {
public:
    FlowRateWizard(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~FlowRateWizard() {};
protected:
    void create_low_end_pages();
    void create_high_end_pages();
    virtual void create_pages() override;
    virtual void create_save_panel_content(wxBoxSizer* sizer) override;
    virtual bool start_calibration(std::vector<int> tray_ids) override;
    virtual bool save_calibration_result() override;
    virtual bool recommend_input_value() override;
    virtual void request_calib_result() override;
    virtual void change_ams_select_mode() override;

    void sync_save_page_data();
    void switch_pages(SimpleEvent& evt);
private:
    // preset page
    CalibrationWizardPage* m_page1{ nullptr };

    // print page
    CalibrationWizardPage* m_page2{ nullptr };

    // page 3
    CalibrationWizardPage* m_low_end_page3{ nullptr };
    ComboBox* m_optimal_block_coarse;
    wxStaticText* m_coarse_calc_result_text;
    float m_coarse_calc_result;
    CheckBox* m_checkBox_skip_calibration;

    CalibrationWizardPage* m_high_end_page3{ nullptr };
    std::vector<FlowRatioCalibResult> m_calib_results;
    wxPanel* m_grid_panel;
    std::map<int, std::string> m_high_end_save_names;

    // page 4
    CalibrationWizardPage* m_low_end_page4{ nullptr };

    // save page
    CalibrationWizardPage* m_low_end_page5{ nullptr };
    ComboBox* m_optimal_block_fine;
    wxStaticText* m_fine_calc_result_text;
    float m_fine_calc_result;
    std::string m_save_name;

    void reset_print_panel_to_page(CalibrationWizardPage* page, wxBoxSizer* sizer);
    void reset_send_progress_to_page(CalibrationWizardPage* page, wxBoxSizer* sizer);
    void on_fine_tune(wxCommandEvent&);
};

class MaxVolumetricSpeedWizard : public CalibrationWizard {
public:
    MaxVolumetricSpeedWizard(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~MaxVolumetricSpeedWizard() {};
protected:
    virtual void create_pages() override;
    virtual void create_save_panel_content(wxBoxSizer* sizer) override;
    virtual bool start_calibration(std::vector<int> tray_ids) override;
    virtual bool save_calibration_result() override;
    virtual bool recommend_input_value() override;
private:
    // preset page
    CalibrationWizardPage* m_page1;

    // print page
    CalibrationWizardPage* m_page2;

    // save page
    CalibrationWizardPage* m_page3;
    TextInput* m_optimal_max_speed;
    wxStaticText* m_calc_result_text;
    float m_calc_result;
    std::string m_save_name;
};

class TemperatureWizard : public CalibrationWizard {
public:
    TemperatureWizard(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~TemperatureWizard() {};
protected:
    virtual void create_pages() override;
    virtual void create_save_panel_content(wxBoxSizer* sizer) override;
    virtual bool start_calibration(std::vector<int> tray_ids) override;
    virtual bool save_calibration_result() override;
    virtual bool recommend_input_value() override;
private:
    // preset page
    CalibrationWizardPage* m_page1;

    // print page
    CalibrationWizardPage* m_page2;

    // save page
    CalibrationWizardPage* m_page3;
    TextInput* m_optimal_temp;
    std::string m_save_name;
};

class VFAWizard : public CalibrationWizard {};

}} // namespace Slic3r::GUI

#endif