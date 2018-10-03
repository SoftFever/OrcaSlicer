#ifndef slic3r_GUI_Preview_hpp_
#define slic3r_GUI_Preview_hpp_

#include <wx/panel.h>
#include "../../libslic3r/Point.hpp"

#include <string>

class wxNotebook;
class wxGLCanvas;
class wxBoxSizer;
class wxStaticText;
class wxChoice;
class wxComboCtrl;
class wxCheckBox;

namespace Slic3r {

class DynamicPrintConfig;
class Print;
class GCodePreviewData;

namespace GUI {

class Preview : public wxPanel
{
    wxGLCanvas* m_canvas;
    wxBoxSizer* m_double_slider_sizer;
    wxStaticText* m_label_view_type;
    wxChoice* m_choice_view_type;
    wxStaticText* m_label_show_features;
    wxComboCtrl* m_combochecklist_features;
    wxCheckBox* m_checkbox_travel;
    wxCheckBox* m_checkbox_retractions;
    wxCheckBox* m_checkbox_unretractions;
    wxCheckBox* m_checkbox_shells;

    DynamicPrintConfig* m_config;
    Print* m_print;
    GCodePreviewData* m_gcode_preview_data;

    unsigned int m_number_extruders;
    std::string m_preferred_color_mode;

    bool m_loaded;
    bool m_enabled;
    bool m_force_sliders_full_range;

public:
    Preview(wxNotebook* notebook, DynamicPrintConfig* config, Print* print, GCodePreviewData* gcode_preview_data);
    virtual ~Preview();

    wxGLCanvas* get_canvas() { return m_canvas; }

    void set_number_extruders(unsigned int number_extruders);
    void reset_gcode_preview_data();
    void set_canvas_as_dirty();
    void set_enabled(bool enabled);
    void set_bed_shape(const Pointfs& shape);
    void select_view(const std::string& direction);
    void set_viewport_from_scene(wxGLCanvas* canvas);
    void set_viewport_into_scene(wxGLCanvas* canvas);
    void set_drop_target(wxDropTarget* target);

    void load_print();
    void reload_print(bool force = false);
    void refresh_print();

private:
    bool init(wxNotebook* notebook, DynamicPrintConfig* config, Print* print, GCodePreviewData* gcode_preview_data);

    void bind_event_handlers();
    void unbind_event_handlers();

    void show_hide_ui_elements(const std::string& what);

    void reset_sliders();
    void update_sliders();

    void on_size(wxSizeEvent& evt);
    void on_choice_view_type(wxCommandEvent& evt);
    void on_combochecklist_features(wxCommandEvent& evt);
    void on_checkbox_travel(wxCommandEvent& evt);
    void on_checkbox_retractions(wxCommandEvent& evt);
    void on_checkbox_unretractions(wxCommandEvent& evt);
    void on_checkbox_shells(wxCommandEvent& evt);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_Preview_hpp_
