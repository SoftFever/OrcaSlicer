#ifndef slic3r_GUI_Preview_hpp_
#define slic3r_GUI_Preview_hpp_

#include <wx/panel.h>
#include "libslic3r/Point.hpp"

#include <string>
#include "libslic3r/Model.hpp"
#if ENABLE_GCODE_VIEWER
#include "libslic3r/GCode/GCodeProcessor.hpp"
#endif // ENABLE_GCODE_VIEWER

class wxNotebook;
class wxGLCanvas;
class wxBoxSizer;
class wxStaticText;
class wxChoice;
class wxComboCtrl;
class wxBitmapComboBox;
class wxCheckBox;

namespace Slic3r {

class DynamicPrintConfig;
class Print;
class BackgroundSlicingProcess;
class GCodePreviewData;
class Model;

namespace DoubleSlider {
    class Control;
};

namespace GUI {

class GLCanvas3D;
class GLToolbar;
class Bed3D;
struct Camera;
#if ENABLE_NON_STATIC_CANVAS_MANAGER
class Plater;
#endif // ENABLE_NON_STATIC_CANVAS_MANAGER

class View3D : public wxPanel
{
    wxGLCanvas* m_canvas_widget;
    GLCanvas3D* m_canvas;

public:
#if ENABLE_NON_STATIC_CANVAS_MANAGER
    View3D(wxWindow* parent, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process);
#else
    View3D(wxWindow* parent, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process);
#endif // ENABLE_NON_STATIC_CANVAS_MANAGER
    virtual ~View3D();

    wxGLCanvas* get_wxglcanvas() { return m_canvas_widget; }
    GLCanvas3D* get_canvas3d() { return m_canvas; }

    void set_as_dirty();
    void bed_shape_changed();

    void select_view(const std::string& direction);
    void select_all();
    void deselect_all();
    void delete_selected();
    void mirror_selection(Axis axis);

    int check_volumes_outside_state() const;

    bool is_layers_editing_enabled() const;
    bool is_layers_editing_allowed() const;
    void enable_layers_editing(bool enable);

    bool is_dragging() const;
    bool is_reload_delayed() const;

    void reload_scene(bool refresh_immediately, bool force_full_scene_refresh = false);
    void render();

private:
#if ENABLE_NON_STATIC_CANVAS_MANAGER
    bool init(wxWindow* parent, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process);
#else
    bool init(wxWindow* parent, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process);
#endif // ENABLE_NON_STATIC_CANVAS_MANAGER
};

class Preview : public wxPanel
{
    wxGLCanvas* m_canvas_widget;
    GLCanvas3D* m_canvas;
    wxBoxSizer* m_double_slider_sizer;
    wxStaticText* m_label_view_type;
    wxChoice* m_choice_view_type;
    wxStaticText* m_label_show_features;
    wxComboCtrl* m_combochecklist_features;
    wxCheckBox* m_checkbox_travel;
    wxCheckBox* m_checkbox_retractions;
    wxCheckBox* m_checkbox_unretractions;
#if ENABLE_GCODE_VIEWER
    wxCheckBox* m_checkbox_tool_changes;
    wxCheckBox* m_checkbox_color_changes;
    wxCheckBox* m_checkbox_pause_prints;
    wxCheckBox* m_checkbox_custom_gcodes;
#endif // ENABLE_GCODE_VIEWER
    wxCheckBox* m_checkbox_shells;
    wxCheckBox* m_checkbox_legend;

    DynamicPrintConfig* m_config;
    BackgroundSlicingProcess* m_process;
    GCodePreviewData* m_gcode_preview_data;
#if ENABLE_GCODE_VIEWER
    GCodeProcessor::Result* m_gcode_result;
#endif // ENABLE_GCODE_VIEWER

#ifdef __linux__
    // We are getting mysterious crashes on Linux in gtk due to OpenGL context activation GH #1874 #1955.
    // So we are applying a workaround here.
    bool m_volumes_cleanup_required;
#endif /* __linux__ */

    // Calling this function object forces Plater::schedule_background_process.
    std::function<void()> m_schedule_background_process;

    unsigned int m_number_extruders;
    std::string m_preferred_color_mode;

    bool m_loaded;
    bool m_enabled;

    DoubleSlider::Control*       m_slider {nullptr};

public:
#if ENABLE_NON_STATIC_CANVAS_MANAGER
#if ENABLE_GCODE_VIEWER
    Preview(wxWindow* parent, Model* model, DynamicPrintConfig* config,
        BackgroundSlicingProcess* process, GCodePreviewData* gcode_preview_data, GCodeProcessor::Result* gcode_result, std::function<void()> schedule_background_process = []() {});
#else
    Preview(wxWindow* parent, Model* model, DynamicPrintConfig* config,
        BackgroundSlicingProcess* process, GCodePreviewData* gcode_preview_data, std::function<void()> schedule_background_process = []() {});
#endif // ENABLE_GCODE_VIEWER
#else
#if ENABLE_GCODE_VIEWER
    Preview(wxWindow* parent, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar, Model* model, DynamicPrintConfig* config,
        BackgroundSlicingProcess* process, GCodePreviewData* gcode_preview_data, GCodeProcessor::Result* gcode_result, std::function<void()> schedule_background_process = []() {});
#else
    Preview(wxWindow* parent, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar, Model* model, DynamicPrintConfig* config,
        BackgroundSlicingProcess* process, GCodePreviewData* gcode_preview_data, std::function<void()> schedule_background_process = []() {});
#endif // ENABLE_GCODE_VIEWER
#endif // ENABLE_NON_STATIC_CANVAS_MANAGER
    virtual ~Preview();

    wxGLCanvas* get_wxglcanvas() { return m_canvas_widget; }
    GLCanvas3D* get_canvas3d() { return m_canvas; }

    void set_as_dirty();

    void set_number_extruders(unsigned int number_extruders);
    void set_enabled(bool enabled);
    void bed_shape_changed();
    void select_view(const std::string& direction);
    void set_drop_target(wxDropTarget* target);

    void load_print(bool keep_z_range = false);
    void reload_print(bool keep_volumes = false);
    void refresh_print();

    void msw_rescale();
    void move_double_slider(wxKeyEvent& evt);
    void edit_double_slider(wxKeyEvent& evt);

    void update_view_type(bool slice_completed);

    bool is_loaded() const { return m_loaded; }

private:
#if ENABLE_NON_STATIC_CANVAS_MANAGER
    bool init(wxWindow* parent, Model* model);
#else
    bool init(wxWindow* parent, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar, Model* model);
#endif // ENABLE_NON_STATIC_CANVAS_MANAGER

    void bind_event_handlers();
    void unbind_event_handlers();

    void show_hide_ui_elements(const std::string& what);

    void reset_sliders(bool reset_all);
    void update_sliders(const std::vector<double>& layers_z, bool keep_z_range = false);

    void on_size(wxSizeEvent& evt);
    void on_choice_view_type(wxCommandEvent& evt);
    void on_combochecklist_features(wxCommandEvent& evt);
    void on_checkbox_travel(wxCommandEvent& evt);
    void on_checkbox_retractions(wxCommandEvent& evt);
    void on_checkbox_unretractions(wxCommandEvent& evt);
#if ENABLE_GCODE_VIEWER
    void on_checkbox_tool_changes(wxCommandEvent& evt);
    void on_checkbox_color_changes(wxCommandEvent& evt);
    void on_checkbox_pause_prints(wxCommandEvent& evt);
    void on_checkbox_custom_gcodes(wxCommandEvent& evt);
#endif // ENABLE_GCODE_VIEWER
    void on_checkbox_shells(wxCommandEvent& evt);
    void on_checkbox_legend(wxCommandEvent& evt);

    // Create/Update/Reset double slider on 3dPreview
    void create_double_slider();
    void check_slider_values(std::vector<CustomGCode::Item> &ticks_from_model,
                             const std::vector<double> &layers_z);
    void reset_double_slider();
    void update_double_slider(const std::vector<double>& layers_z, bool keep_z_range = false);
    void update_double_slider_mode();
    // update DoubleSlider after keyDown in canvas
    void update_double_slider_from_canvas(wxKeyEvent& event);

    void load_print_as_fff(bool keep_z_range = false);
    void load_print_as_sla();

    void on_sliders_scroll_changed(wxCommandEvent& event);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_Preview_hpp_
