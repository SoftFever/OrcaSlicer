#ifndef slic3r_FilamentMapDialog_hpp_
#define slic3r_FilamentMapDialog_hpp_

#include "GUI.hpp"
#include <wx/simplebook.h>
#include <wx/dialog.h>
#include <wx/timer.h>
#include <vector>

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
        bool is_auto);

    bool is_auto() const;
    const std::vector<int>& get_filament_maps() { return m_filament_map; }

private:
    void on_ok(wxCommandEvent &event);
    void on_cancle(wxCommandEvent &event);

private:
    wxRadioButton* m_auto_radio;
    wxRadioButton* m_manual_radio;
    DragDropPanel* m_left_panel;
    DragDropPanel* m_right_panel;

private:
    const DynamicPrintConfig* m_config;
    std::vector<int> m_filament_map;
};

}} // namespace Slic3r::GUI

#endif /* slic3r_FilamentMapDialog_hpp_ */
