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

namespace Slic3r { namespace GUI {

class PrintOptionsDialog : public DPIDialog
{
protected:
    // settings
    CheckBox* m_cb_first_layer;
    CheckBox* m_cb_ai_monitoring;
    CheckBox* m_cb_plate_mark;
    CheckBox* m_cb_auto_recovery;
    CheckBox* m_cb_sup_sound;
    CheckBox* m_cb_filament_tangle;
    wxStaticText* text_first_layer;
    wxStaticText* text_ai_monitoring;
    wxStaticText* text_ai_monitoring_caption;
    ComboBox* ai_monitoring_level_list;
    wxStaticText* text_plate_mark;
    wxStaticText* text_plate_mark_caption;
    wxStaticText* text_auto_recovery;
    wxStaticText* text_sup_sound;
    wxStaticText* text_filament_tangle;
    StaticLine* line1;
    StaticLine* line2;
    StaticLine* line3;
    StaticLine* line4;
    StaticLine* line5;
    StaticLine* line6;
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
};

}} // namespace Slic3r::GUI

#endif
