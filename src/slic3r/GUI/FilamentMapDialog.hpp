#ifndef slic3r_FilamentMapDialog_hpp_
#define slic3r_FilamentMapDialog_hpp_

#include "FilamentMapPanel.hpp"
#include <vector>
#include "CapsuleButton.hpp"

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
    enum PageType {
        ptAuto,
        ptManual,
        ptDefault
    };
public:
    FilamentMapDialog(wxWindow *parent,
        const std::vector<std::string>& filament_color,
        const std::vector<int> &filament_map,
        const std::vector<int> &filaments,
        const FilamentMapMode mode,
        bool machine_synced,
        bool show_default=true
    );

    FilamentMapMode get_mode();
    const std::vector<int>& get_filament_maps() const { return m_filament_map; }

    int ShowModal();
    void set_modal_btn_labels(const wxString& left_label, const wxString& right_label);
private:
    void on_ok(wxCommandEvent &event);
    void on_cancle(wxCommandEvent &event);
    void on_switch_mode(wxCommandEvent &event);

    void update_panel_status(PageType page);

 private:
    FilamentMapManualPanel* m_manual_map_panel;
    FilamentMapAutoPanel* m_auto_map_panel;
    FilamentMapDefaultPanel* m_default_map_panel;

    CapsuleButton* m_auto_btn;
    CapsuleButton* m_manual_btn;
    CapsuleButton* m_default_btn;

    Button* m_ok_btn;
    Button* m_cancel_btn;

    PageType m_page_type;

private:
    std::vector<int> m_filament_map;
    std::vector<std::string> m_filament_color;
};

}} // namespace Slic3r::GUI

#endif /* slic3r_FilamentMapDialog_hpp_ */
