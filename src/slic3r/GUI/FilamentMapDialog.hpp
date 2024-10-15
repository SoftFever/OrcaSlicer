#ifndef slic3r_FilamentMapDialog_hpp_
#define slic3r_FilamentMapDialog_hpp_

#include "GUI.hpp"
#include <wx/simplebook.h>
#include <wx/dialog.h>
#include <wx/timer.h>
#include <vector>

class SwitchButton;
class ScalableButton;
class Button;
class wxStaticText;

namespace Slic3r {
class DynamicPrintConfig;

namespace GUI {
class DragDropPanel;
class FilamentMapDialog : public wxDialog
{
public:
    FilamentMapDialog(wxWindow *parent,
        const DynamicPrintConfig *config,
        const std::vector<int> &filament_map,
        const std::vector<int> &extruders,
        bool is_auto,
        bool has_auto_result
    );

    bool is_auto() const;
    const std::vector<int>& get_filament_maps() const { return m_filament_map; }

private:
    void on_ok(wxCommandEvent &event);
    void on_cancle(wxCommandEvent &event);
    void on_switch_mode(wxCommandEvent &event);
    void on_switch_filaments(wxCommandEvent &event);

private:
    wxStaticText * m_tip_text;
    SwitchButton * m_mode_switch_btn;
    wxBoxSizer *   m_extruder_panel_sizer;
    DragDropPanel* m_manual_left_panel;
    DragDropPanel* m_manual_right_panel;
    DragDropPanel* m_auto_left_panel;
    DragDropPanel* m_auto_right_panel;
    ScalableButton* m_switch_filament_btn;
    ScalableButton* m_switch_filament_btn_auto; // for placeholder

private:
    const DynamicPrintConfig* m_config;
    std::vector<int> m_filament_map;
    bool m_has_auto_result;
};

}} // namespace Slic3r::GUI

#endif /* slic3r_FilamentMapDialog_hpp_ */
