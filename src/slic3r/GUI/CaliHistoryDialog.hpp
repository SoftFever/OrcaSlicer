#ifndef slic3r_GUI_CaliHistory_hpp_
#define slic3r_GUI_CaliHistory_hpp_

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "Widgets/ComboBox.hpp"
#include "DeviceManager.hpp"

namespace Slic3r { 
namespace GUI {

class HistoryWindow : public DPIDialog {
public:
    HistoryWindow(wxWindow* parent, const std::vector<PACalibResult>& calib_results_history, bool& show);
    ~HistoryWindow();
    void on_dpi_changed(const wxRect& suggested_rect) {}
    void on_select_nozzle(wxCommandEvent& evt);
    void reqeust_history_result(MachineObject* obj);
    void sync_history_result(MachineObject* obj);
    void on_device_connected(MachineObject* obj);
    void on_timer(wxTimerEvent& event);
    void update(MachineObject* obj);
protected:
    void sync_history_data();
    void enbale_action_buttons(bool enable);
    float get_nozzle_value();

    void on_click_new_button(wxCommandEvent &event);

    wxPanel*                   m_history_data_panel;
    ComboBox*                  m_comboBox_nozzle_dia;
    Label*              m_tips;

    wxTimer*                   m_refresh_timer { nullptr };

    bool&                      m_show_history_dialog;
    std::vector<PACalibResult> m_calib_results_history;
    MachineObject*             curr_obj { nullptr };
};

class EditCalibrationHistoryDialog : public DPIDialog
{
public:
    EditCalibrationHistoryDialog(wxWindow* parent, const PACalibResult& result);
    ~EditCalibrationHistoryDialog();
    void on_dpi_changed(const wxRect& suggested_rect) override;
    PACalibResult get_result();

protected:
    virtual void on_save(wxCommandEvent& event);
    virtual void on_cancel(wxCommandEvent& event);

protected:
    PACalibResult m_new_result;

    TextInput* m_name_value{ nullptr };
    TextInput* m_k_value{ nullptr };
};

class NewCalibrationHistoryDialog : public DPIDialog
{
public:
    NewCalibrationHistoryDialog(wxWindow *parent, const std::vector<PACalibResult> history_results);
    ~NewCalibrationHistoryDialog(){};
    void on_dpi_changed(const wxRect &suggested_rect) override{};

protected:
    virtual void on_ok(wxCommandEvent &event);
    virtual void on_cancel(wxCommandEvent &event);


    wxArrayString get_all_filaments(const MachineObject *obj);

protected:
    PACalibResult m_new_result;
    std::vector<PACalibResult> m_history_results;
    MachineObject *  curr_obj;

    TextInput *m_name_value{nullptr};
    TextInput *m_k_value{nullptr};

    ComboBox *m_comboBox_nozzle_diameter;
    ComboBox *m_comboBox_filament;

    struct FilamentInfos
    {
        std::string filament_id;
        std::string setting_id;
    };
    std::map<std::string, FilamentInfos> map_filament_items;
};

} // namespace GUI
} // namespace Slic3r

#endif
