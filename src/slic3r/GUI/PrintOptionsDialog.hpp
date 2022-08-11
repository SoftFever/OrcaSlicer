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

namespace Slic3r { namespace GUI {

class PrintOptionsDialog : public DPIDialog
{
protected:
    // settings
    CheckBox* m_cb_first_layer;
    CheckBox* m_cb_spaghetti;
    CheckBox* m_cb_spaghetti_print_halt;
    wxStaticText* text_spaghetti_print_halt;
    wxBoxSizer* create_settings_group(wxWindow* parent);

public:
    PrintOptionsDialog(wxWindow* parent);
    ~PrintOptionsDialog();
    void on_dpi_changed(const wxRect &suggested_rect) override;
    void update_spaghetti();

    MachineObject *obj { nullptr };

    std::vector<int> last_stage_list_info; 
    int              m_state{0};
    void             update_options(MachineObject *obj_);
    void             update_machine_obj(MachineObject *obj_);
    bool             Show(bool show) override;
};

}} // namespace Slic3r::GUI

#endif
