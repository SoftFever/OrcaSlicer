#ifndef slic3r_EditGCodeDialog_hpp_
#define slic3r_EditGCodeDialog_hpp_

#include <vector>

#include <wx/gdicmn.h>
#include <slic3r/GUI/Widgets/Button.hpp>

#include "GUI_Utils.hpp"

class wxListBox;
class wxTextCtrl;
//class wxStaticText;
class ScalableButton;
//class wxBoxSizer;

namespace Slic3r {

namespace GUI {

class PresetComboBox;

//------------------------------------------
//          EditGCodeDialog
//------------------------------------------

class EditGCodeDialog : public DPIDialog
{
    wxListBox*          m_patterns_list {nullptr};
    ScalableButton*     m_add_btn       {nullptr};
    wxTextCtrl*         m_gcode_editor  {nullptr};

    void OnOK(wxEvent& event);

public:
    EditGCodeDialog(wxWindow* parent, const std::string& key, const std::string& value);
    ~EditGCodeDialog() {}

protected:
    std::unordered_map<int, Button *> m_button_list;

    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override;
    wxBoxSizer* EditGCodeDialog::create_btn_sizer(long flags);
};


} // namespace GUI
} // namespace Slic3r

#endif
