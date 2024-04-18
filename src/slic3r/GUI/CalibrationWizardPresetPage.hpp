#ifndef slic3r_GUI_CalibrationWizardPresetPage_hpp_
#define slic3r_GUI_CalibrationWizardPresetPage_hpp_

#include "CalibrationWizardPage.hpp"

namespace Slic3r { namespace GUI {

enum CaliPresetStage {
    CALI_MANULA_STAGE_NONE = 0,
    CALI_MANUAL_STAGE_1,
    CALI_MANUAL_STAGE_2,
};

enum FlowRatioCaliSource {
    FROM_PRESET_PAGE = 0,
    FROM_COARSE_PAGE,
};

class CaliPresetCaliStagePanel : public wxPanel
{
public:
    CaliPresetCaliStagePanel(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);
    void create_panel(wxWindow* parent);

    void set_cali_stage(CaliPresetStage stage, float value);
    void get_cali_stage(CaliPresetStage& stage, float& value);

    void set_flow_ratio_value(float flow_ratio);

protected:
    CaliPresetStage m_stage;
    wxBoxSizer*   m_top_sizer;
    wxRadioButton* m_complete_radioBox;
    wxRadioButton* m_fine_radioBox;
    TextInput *    flow_ratio_input;
    float m_flow_ratio_value;
};

class CaliComboBox : public wxPanel
{
public:
    CaliComboBox(wxWindow *parent,
        wxString title,
        wxArrayString values,
        int default_index = 0,  // default delected id
        std::function<void(wxCommandEvent &)> on_value_change = nullptr,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    int get_selection() const;
    wxString get_value() const;
    void set_values(const wxArrayString& values);

private:
    wxBoxSizer* m_top_sizer;
    wxString m_title;
    ComboBox* m_combo_box;
    std::function<void(wxCommandEvent&)> m_on_value_change_call_back;
};

class CaliPresetWarningPanel : public wxPanel
{
public:
    CaliPresetWarningPanel(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_panel(wxWindow* parent);

    void set_warning(wxString text);
protected:
    wxBoxSizer*   m_top_sizer;
    Label* m_warning_text;
};

class CaliPresetTipsPanel : public wxPanel
{
public:
    CaliPresetTipsPanel(wxWindow* parent,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_panel(wxWindow* parent);

    void set_params(int nozzle_temp, int bed_temp, float max_volumetric);
    void get_params(int& nozzle_temp, int& bed_temp, float& max_volumetric);
protected:
    wxBoxSizer*     m_top_sizer;
    TextInput*      m_nozzle_temp;
    Label*   m_bed_temp;
    TextInput*      m_max_volumetric_speed;
};

class CaliPresetCustomRangePanel : public wxPanel
{
public:
    CaliPresetCustomRangePanel(wxWindow* parent,
        int input_value_nums = 3,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_panel(wxWindow* parent);

    void set_unit(wxString unit);
    void set_titles(wxArrayString titles);
    void set_values(wxArrayString values);
    wxArrayString get_values();

protected:
    wxBoxSizer*     m_top_sizer;
    int                       m_input_value_nums;
    std::vector<Label*> m_title_texts;
    std::vector<TextInput*>    m_value_inputs;
};

enum CaliPresetPageStatus
{
    CaliPresetStatusInit = 0,
    CaliPresetStatusNormal,
    CaliPresetStatusNoUserLogin,
    CaliPresetStatusInvalidPrinter,
    CaliPresetStatusConnectingServer,
    CaliPresetStatusInUpgrading,
    CaliPresetStatusInSystemPrinting,
    CaliPresetStatusInPrinting,
    CaliPresetStatusLanModeNoSdcard,
    CaliPresetStatusNoSdcard,
    CaliPresetStatusNeedForceUpgrading,
    CaliPresetStatusNeedConsistencyUpgrading,
    CaliPresetStatusUnsupportedPrinter,
    CaliPresetStatusInConnecting, 
    CaliPresetStatusFilamentIncompatible,
};

class CalibrationPresetPage : public CalibrationWizardPage
{
public:
    CalibrationPresetPage(wxWindow* parent,
        CalibMode cali_mode,
        bool custom_range = false,
        wxWindowID id = wxID_ANY,
        const wxPoint& pos = wxDefaultPosition,
        const wxSize& size = wxDefaultSize,
        long style = wxTAB_TRAVERSAL);

    void create_page(wxWindow* parent);

    void update_print_status_msg(wxString msg, bool is_warning);
    wxString format_text(wxString& m_msg);
    void stripWhiteSpace(std::string& str);
    void update_priner_status_msg(wxString msg, bool is_warning);
    void update(MachineObject* obj) override;

    void on_device_connected(MachineObject* obj) override;

    void update_print_error_info(int code, const std::string& msg, const std::string& extra) { m_sending_panel->update_print_error_info(code, msg, extra); }

    void set_cali_filament_mode(CalibrationFilamentMode mode) override;

    void set_cali_method(CalibrationMethod method) override;

    void on_cali_start_job();

    void on_cali_finished_job();

    void on_cali_cancel_job();

    void init_with_machine(MachineObject* obj);

    void sync_ams_info(MachineObject* obj);

    void select_default_compatible_filament();

    std::vector<FilamentComboBox*> get_selected_filament_combobox();

    // key is tray_id
    std::map<int, Preset*> get_selected_filaments();

    void get_preset_info(
        float& nozzle_dia,
        BedType& plate_type);

    void get_cali_stage(CaliPresetStage& stage, float& value);

    std::shared_ptr<ProgressIndicator> get_sending_progress_bar() {
        return m_sending_panel->get_sending_progress_bar();
    }

    Preset* get_printer_preset(MachineObject* obj, float nozzle_value);
    Preset* get_print_preset();
    std::string get_print_preset_name();

    wxArrayString get_custom_range_values();
    CalibMode     get_pa_cali_method();

    CaliPresetPageStatus get_page_status() { return m_page_status; }

    void msw_rescale() override;
    void on_sys_color_changed() override;

protected:
    void create_selection_panel(wxWindow* parent);
    void create_filament_list_panel(wxWindow* parent);
    void create_ext_spool_panel(wxWindow* parent);

    void init_selection_values();
    void update_filament_combobox(std::string ams_id = "");

    void on_select_nozzle(wxCommandEvent& evt);
    void on_select_plate_type(wxCommandEvent& evt);

    void on_choose_ams(wxCommandEvent& event);
    void on_choose_ext_spool(wxCommandEvent& event);

    void on_select_tray(wxCommandEvent& event);

    void on_switch_ams(std::string ams_id = "");

    void on_recommend_input_value();

    void check_filament_compatible();
    bool is_filaments_compatiable(const std::vector<Preset*>& prests);
    bool is_filament_in_blacklist(Preset* preset, std::string& error_tips);
    bool is_filaments_compatiable(const std::vector<Preset*>& prests,
        int& bed_temp,
        std::string& incompatiable_filament_name,
        std::string& error_tips);

    float get_nozzle_value();

    void update_plate_type_collection(CalibrationMethod method);
    void update_combobox_filaments(MachineObject* obj);
    void update_show_status();
    void show_status(CaliPresetPageStatus status);
    void Enable_Send_Button(bool enable);
    bool is_blocking_printing();
    bool need_check_sdcard(MachineObject* obj);
    
    CaliPresetPageStatus  get_status() { return m_page_status; }

    CaliPageStepGuide* m_step_panel{ nullptr };
    CaliComboBox *            m_pa_cali_method_combox{nullptr};
    CaliPresetCaliStagePanel* m_cali_stage_panel { nullptr };
    wxPanel*                  m_selection_panel { nullptr };
    wxPanel*                  m_filament_from_panel { nullptr };
    Label*             m_filament_list_tips{ nullptr };
    wxPanel*                  m_multi_ams_panel { nullptr };
    wxPanel*                  m_filament_list_panel { nullptr };
    wxPanel*                  m_ext_spool_panel { nullptr };
    CaliPresetWarningPanel*   m_warning_panel { nullptr };
    CaliPresetCustomRangePanel* m_custom_range_panel { nullptr };
    CaliPresetTipsPanel*      m_tips_panel { nullptr };
    CaliPageSendingPanel*     m_sending_panel { nullptr };

    wxBoxSizer* m_top_sizer;

    // m_selection_panel widgets
    ComboBox*       m_comboBox_nozzle_dia;
    ComboBox*       m_comboBox_bed_type;
    ComboBox*       m_comboBox_process;
    
    wxRadioButton*      m_ams_radiobox;
    wxRadioButton*      m_ext_spool_radiobox;
    
    ScalableButton*      m_ams_sync_button;
    FilamentComboBoxList m_filament_comboBox_list;
    FilamentComboBox*    m_virtual_tray_comboBox;

    
    std::vector<AMSItem*> m_ams_item_list;

    // for update filament combobox, key : tray_id
    std::map<int, DynamicPrintConfig> filament_ams_list;

    CaliPresetPageStatus    m_page_status { CaliPresetPageStatus::CaliPresetStatusInit };
    bool                    m_stop_update_page_status{ false };

    bool m_show_custom_range { false };
    bool m_has_filament_incompatible { false };
    MachineObject* curr_obj { nullptr };
};

class MaxVolumetricSpeedPresetPage : public CalibrationPresetPage
{
public:
    MaxVolumetricSpeedPresetPage(wxWindow *     parent,
                                 CalibMode      cali_mode,
                                 bool           custom_range = false,
                                 wxWindowID     id           = wxID_ANY,
                                 const wxPoint &pos          = wxDefaultPosition,
                                 const wxSize & size         = wxDefaultSize,
                                 long           style        = wxTAB_TRAVERSAL);
};

}} // namespace Slic3r::GUI

#endif