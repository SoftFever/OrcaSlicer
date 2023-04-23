#ifndef slic3r_GUI_CalibrationWizard_hpp_
#define slic3r_GUI_CalibrationWizard_hpp_

#include "GUI_Utils.hpp"
#include "DeviceManager.hpp"
#include "CalibrationWizardPage.hpp"
#include "Widgets/ComboBox.hpp"
#include "Widgets/TextInput.hpp"
#include "Widgets/AMSControl.hpp"
#include "SavePresetDialog.hpp"
#include "../slic3r/Utils/CalibUtils.hpp"

namespace Slic3r { namespace GUI {

class CalibrationWizard : public wxPanel {
public:
    CalibrationWizard(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~CalibrationWizard() {};
    CalibrationWizardPage* get_curr_page() { return m_curr_page; }
    CalibrationWizardPage* get_frist_page() { return m_first_page; }
    void show_page(CalibrationWizardPage* page);
    void update_obj(MachineObject* rhs_obj) { obj = rhs_obj; }
    void update_ams(MachineObject* obj);
    void update_progress();

protected:
    virtual void create_pages() = 0;
    virtual bool start_calibration(std::string tray_id) = 0;
    virtual void save_calibration_result() {};
    virtual void update_calibration_value() = 0;

protected:
    wxPanel*  m_background_panel;
    wxPanel*  m_presets_panel;
    AMSControl* m_ams_control;
    ComboBox* m_comboBox_printer;
    ComboBox* m_comboBox_filament;
    ComboBox* m_comboBox_bed_type;
    ComboBox* m_comboBox_process;
    wxStaticText* m_from_text;
    wxStaticText* m_to_text;
    wxStaticText* m_step_text;
    TextInput* m_from_value;
    TextInput* m_to_value;
    TextInput* m_step;
    BBLStatusBarSend* m_progress_bar;

    MachineObject* obj{ nullptr };

    CalibrationWizardPage* m_curr_page{ nullptr };
    CalibrationWizardPage* m_first_page{ nullptr };

    void add_presets_panel_to_page(CalibrationWizardPage* page, wxBoxSizer* sizer);
    void add_progress_bar_to_page(CalibrationWizardPage* page, wxBoxSizer* sizer);
    void show_progress_bar(bool show);
    wxString get_presets_incompatible() { return wxString(); }

    void on_select_printer(wxCommandEvent& evt);
    void on_select_filament(wxCommandEvent& evt);
    void on_select_bed_type(wxCommandEvent& evt);
    void on_select_process(wxCommandEvent& evt);
    void on_click_btn_prev(IntEvent& event);
    void on_click_btn_next(IntEvent& event);

private:
    void create_presets_panel();
    void create_progress_bar();

    void init_printer_selections();
    void init_filaments_selections();
    void init_bed_type_selections();
    void init_process_selections();
    void init_presets_selections();
};

class PressureAdvanceWizard : public CalibrationWizard{};

class FlowRateWizard : public CalibrationWizard {
public:
    FlowRateWizard(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~FlowRateWizard() {};
protected:
    virtual void create_pages() override;
    virtual bool start_calibration(std::string tray_id) override;
    virtual void update_calibration_value() override;
private:
    // page 1
    CalibrationWizardPage* m_page1;

    // page 2
    CalibrationWizardPage* m_page2;

    // page 3
    CalibrationWizardPage* m_page3;
    ComboBox* m_optimal_block_coarse;

    // page 4
    CalibrationWizardPage* m_page4;
    AMSControl* m_readonly_ams_control;
    TextInput* m_readonly_printer;
    TextInput* m_readonly_filament;
    TextInput* m_readonly_bed_type;
    TextInput* m_readonly_process;

    // page 5
    CalibrationWizardPage* m_page5;
    ComboBox* m_optimal_block_fine;

    void create_readonly_presets_panel();
};

class MaxVolumetricSpeedWizard : public CalibrationWizard {
public:
    MaxVolumetricSpeedWizard(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~MaxVolumetricSpeedWizard() {};
protected:
    virtual void create_pages() override;
    virtual bool start_calibration(std::string tray_id) override;
    virtual void update_calibration_value() override;
private:
    // page 1
    CalibrationWizardPage* m_page1;

    // page 2
    CalibrationWizardPage* m_page2;

    // page 3
    CalibrationWizardPage* m_page3;
    TextInput* m_optimal_max_speed;
};

class TemperatureWizard : public CalibrationWizard {
public:
    TemperatureWizard(wxWindow* parent, wxWindowID id = wxID_ANY, const wxPoint& pos = wxDefaultPosition, const wxSize& size = wxDefaultSize, long style = wxTAB_TRAVERSAL);
    ~TemperatureWizard() {};
protected:
    virtual void create_pages() override;
    virtual bool start_calibration(std::string tray_id) override;
    virtual void save_calibration_result();
    virtual void update_calibration_value() override;
private:
    // page 1
    CalibrationWizardPage* m_page1;

    // page 2
    CalibrationWizardPage* m_page2;
    TextInput* m_optimal_temp;
};

class VFAWizard : public CalibrationWizard {};

}} // namespace Slic3r::GUI

#endif