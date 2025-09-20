#ifndef slic3r_GUI_PrintOptionsDialog_hpp_
#define slic3r_GUI_PrintOptionsDialog_hpp_

#include <wx/wx.h>
#include <wx/font.h>
#include <wx/colour.h>
#include <wx/string.h>
#include <wx/sizer.h>
#include <wx/dialog.h>

#include "GUI_Utils.hpp"
#include "wxExtensions.hpp"
#include "DeviceManager.hpp"
#include "Widgets/Label.hpp"
#include "Widgets/CheckBox.hpp"
#include "Widgets/StaticLine.hpp"
#include "Widgets/ComboBox.hpp"

// Previous definitions
class SwitchBoard;

namespace Slic3r { namespace GUI {

class PrinterPartsDialog : public DPIDialog
{
protected:
    MachineObject* obj{ nullptr };

    ComboBox* nozzle_type_checkbox;
    ComboBox* nozzle_diameter_checkbox;

    Label*    nozzle_flow_type_label;
    ComboBox* nozzle_flow_type_checkbox;
    Label    *change_nozzle_tips;

    ComboBox* multiple_left_nozzle_type_checkbox;
    ComboBox *multiple_left_nozzle_diameter_checkbox;
    ComboBox *multiple_left_nozzle_flow_checkbox;

    ComboBox *multiple_right_nozzle_type_checkbox;
    ComboBox *multiple_right_nozzle_diameter_checkbox;
    ComboBox *multiple_right_nozzle_flow_checkbox;

    Label *multiple_change_nozzle_tips;

    wxPanel *single_panel;
    wxPanel *multiple_panel;

public:
    PrinterPartsDialog(wxWindow* parent);
    ~PrinterPartsDialog();

    void on_dpi_changed(const wxRect& suggested_rect) override;
    void update_machine_obj(MachineObject* obj_);
    bool Show(bool show) override;

private:
    void  EnableEditing(bool enable);

    wxString GetString(NozzleType nozzle_type) const;
    wxString GetString(NozzleFlowType nozzle_flow_type) const;
    wxString GetString(float diameter) const { return wxString::FromDouble(diameter); };
};


class PrintOptionsDialog : public DPIDialog
{
protected:
    // settings
    CheckBox* m_cb_first_layer;
    CheckBox* m_cb_ai_monitoring;
    CheckBox* m_cb_plate_mark;
    CheckBox* m_cb_auto_recovery;
    CheckBox* m_cb_open_door;
    CheckBox* m_cb_save_remote_print_file_to_storage;
    CheckBox* m_cb_sup_sound;
    CheckBox* m_cb_filament_tangle;
    CheckBox* m_cb_nozzle_blob;
    Label* text_first_layer;
    Label* text_ai_monitoring;
    Label* text_ai_monitoring_caption;
    ComboBox* ai_monitoring_level_list;
    Label* text_plate_mark;
    Label* text_plate_mark_caption;
    Label* text_auto_recovery;
    Label* text_open_door;
    Label* text_save_remote_print_file_to_storage;
    Label* text_save_remote_print_file_to_storage_explain;
    Label* text_sup_sound;
    Label* text_filament_tangle;
    Label* text_nozzle_blob;
    Label* text_nozzle_blob_caption;
    StaticLine* line1;
    StaticLine* line2;
    StaticLine* line3;
    StaticLine* line4;
    StaticLine* line5;
    StaticLine* line6;
    StaticLine* line7;
    SwitchBoard* open_door_switch_board;
    wxBoxSizer* create_settings_group(wxWindow* parent);

    bool print_halt = false;

public:
    PrintOptionsDialog(wxWindow* parent);
    ~PrintOptionsDialog();
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void update_ai_monitor_status();

    MachineObject *obj { nullptr };

    std::vector<int> last_stage_list_info;
    int              m_state{0};
    void             update_options(MachineObject *obj_);
    void             update_machine_obj(MachineObject *obj_);
    bool             Show(bool show) override;

    enum AiMonitorSensitivityLevel {
        LOW         = 0,
        MEDIUM      = 1,
        HIGH        = 2,
        LEVELS_NUM  = 3
    };
    wxString sensitivity_level_to_label_string(enum AiMonitorSensitivityLevel level);
    std::string sensitivity_level_to_msg_string(enum AiMonitorSensitivityLevel level);
    void set_ai_monitor_sensitivity(wxCommandEvent& evt);

private:
    void UpdateOptionOpenDoorCheck(MachineObject *obj);
    void UpdateOptionSavePrintFileToStorage(MachineObject *obj);
};

}} // namespace Slic3r::GUI

#endif
