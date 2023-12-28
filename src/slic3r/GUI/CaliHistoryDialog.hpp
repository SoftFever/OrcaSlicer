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
    HistoryWindow(wxWindow* parent, const std::vector<PACalibResult>& calib_results_history);
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

    wxPanel*                   m_history_data_panel;
    ComboBox*                  m_comboBox_nozzle_dia;
    Label*              m_tips;

    wxTimer*                   m_refresh_timer { nullptr };

    std::vector<PACalibResult> m_calib_results_history;
    MachineObject*             curr_obj { nullptr };
    int                        history_version = -1;
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

} // namespace GUI
} // namespace Slic3r

#endif
