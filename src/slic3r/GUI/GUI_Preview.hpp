#ifndef slic3r_GUI_Preview_hpp_
#define slic3r_GUI_Preview_hpp_

#include <wx/panel.h>

#include "libslic3r/Point.hpp"
#include "libslic3r/CustomGCode.hpp"

#include <string>
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
#if !ENABLE_GCODE_VIEWER
class GCodePreviewData;
#endif // !ENABLE_GCODE_VIEWER
class Model;

namespace DoubleSlider {
    class Control;
};

namespace GUI {

class GLCanvas3D;
class GLToolbar;
class Bed3D;
struct Camera;
class Plater;

class View3D : public wxPanel
{
    wxGLCanvas* m_canvas_widget;
    GLCanvas3D* m_canvas;

public:
    View3D(wxWindow* parent, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process);
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
    bool init(wxWindow* parent, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process);
};

class Preview : public wxPanel
{
    wxGLCanvas* m_canvas_widget;
    GLCanvas3D* m_canvas;
#if ENABLE_GCODE_VIEWER
    wxBoxSizer* m_left_sizer;
    wxBoxSizer* m_layers_slider_sizer;
    wxPanel* m_bottom_toolbar_panel;
#else
    wxBoxSizer* m_double_slider_sizer;
#endif // ENABLE_GCODE_VIEWER
    wxStaticText* m_label_view_type;
    wxChoice* m_choice_view_type;
    wxStaticText* m_label_show;
    wxComboCtrl* m_combochecklist_features;
#if ENABLE_GCODE_VIEWER
    size_t m_combochecklist_features_pos;
    wxComboCtrl* m_combochecklist_options;
#else
    wxCheckBox* m_checkbox_travel;
    wxCheckBox* m_checkbox_retractions;
    wxCheckBox* m_checkbox_unretractions;
    wxCheckBox* m_checkbox_shells;
    wxCheckBox* m_checkbox_legend;
#endif // ENABLE_GCODE_VIEWER

    DynamicPrintConfig* m_config;
    BackgroundSlicingProcess* m_process;
#if ENABLE_GCODE_VIEWER
    GCodeProcessor::Result* m_gcode_result;
#else
    GCodePreviewData* m_gcode_preview_data;
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
#if !ENABLE_GCODE_VIEWER
    bool m_enabled;
#endif // !ENABLE_GCODE_VIEWER

#if ENABLE_GCODE_VIEWER
    DoubleSlider::Control* m_layers_slider{ nullptr };
    DoubleSlider::Control* m_moves_slider{ nullptr };
#else
    DoubleSlider::Control*       m_slider {nullptr};
#endif // ENABLE_GCODE_VIEWER

public:
#if ENABLE_GCODE_VIEWER
    enum class OptionType : unsigned int
    {
        Travel,
        Retractions,
        Unretractions,
        ToolChanges,
        ColorChanges,
        PausePrints,
        CustomGCodes,
        Shells,
        ToolMarker,
        Legend
    };

Preview(wxWindow* parent, Model* model, DynamicPrintConfig* config, BackgroundSlicingProcess* process, 
    GCodeProcessor::Result* gcode_result, std::function<void()> schedule_background_process = []() {});
#else
Preview(wxWindow* parent, Model* model, DynamicPrintConfig* config,
        BackgroundSlicingProcess* process, GCodePreviewData* gcode_preview_data, std::function<void()> schedule_background_process = []() {});
#endif // ENABLE_GCODE_VIEWER
    virtual ~Preview();

    wxGLCanvas* get_wxglcanvas() { return m_canvas_widget; }
    GLCanvas3D* get_canvas3d() { return m_canvas; }

    void set_as_dirty();

    void set_number_extruders(unsigned int number_extruders);
#if !ENABLE_GCODE_VIEWER
    void set_enabled(bool enabled);
#endif // !ENABLE_GCODE_VIEWER
    void bed_shape_changed();
    void select_view(const std::string& direction);
    void set_drop_target(wxDropTarget* target);

    void load_print(bool keep_z_range = false);
    void reload_print(bool keep_volumes = false);
    void refresh_print();

    void msw_rescale();
#if ENABLE_GCODE_VIEWER
    void move_layers_slider(wxKeyEvent& evt);
    void edit_layers_slider(wxKeyEvent& evt);
#else
    void move_double_slider(wxKeyEvent& evt);
    void edit_double_slider(wxKeyEvent& evt);
#endif // ENABLE_GCODE_VIEWER

    void update_view_type(bool keep_volumes);

    bool is_loaded() const { return m_loaded; }

#if ENABLE_GCODE_VIEWER
    void update_bottom_toolbar();
    void update_moves_slider();
    void hide_layers_slider();
#endif // ENABLE_GCODE_VIEWER

private:
    bool init(wxWindow* parent, Model* model);

    void bind_event_handlers();
    void unbind_event_handlers();

#if !ENABLE_GCODE_VIEWER
    void show_hide_ui_elements(const std::string& what);

    void reset_sliders(bool reset_all);
    void update_sliders(const std::vector<double>& layers_z, bool keep_z_range = false);
#endif // !ENABLE_GCODE_VIEWER

    void on_size(wxSizeEvent& evt);
    void on_choice_view_type(wxCommandEvent& evt);
    void on_combochecklist_features(wxCommandEvent& evt);
#if ENABLE_GCODE_VIEWER
    void on_combochecklist_options(wxCommandEvent& evt);
#else
    void on_checkbox_travel(wxCommandEvent& evt);
    void on_checkbox_retractions(wxCommandEvent& evt);
    void on_checkbox_unretractions(wxCommandEvent& evt);
    void on_checkbox_shells(wxCommandEvent& evt);
    void on_checkbox_legend(wxCommandEvent& evt);
#endif // ENABLE_GCODE_VIEWER

#if ENABLE_GCODE_VIEWER
    // Create/Update/Reset double slider on 3dPreview
    wxBoxSizer* create_layers_slider_sizer();
    void check_layers_slider_values(std::vector<CustomGCode::Item>& ticks_from_model,
        const std::vector<double>& layers_z);
    void reset_layers_slider();
    void update_layers_slider(const std::vector<double>& layers_z, bool keep_z_range = false);
    void update_layers_slider_mode();
    // update vertical DoubleSlider after keyDown in canvas
    void update_layers_slider_from_canvas(wxKeyEvent& event);
#else
    // Create/Update/Reset double slider on 3dPreview
    void create_double_slider();
    void check_slider_values(std::vector<CustomGCode::Item>& ticks_from_model,
        const std::vector<double>& layers_z);
    void reset_double_slider();
    void update_double_slider(const std::vector<double>& layers_z, bool keep_z_range = false);
    void update_double_slider_mode();
    // update DoubleSlider after keyDown in canvas
    void update_double_slider_from_canvas(wxKeyEvent& event);
#endif // ENABLE_GCODE_VIEWER

    void load_print_as_fff(bool keep_z_range = false);
    void load_print_as_sla();

#if ENABLE_GCODE_VIEWER
    void on_layers_slider_scroll_changed(wxCommandEvent& event);
    void on_moves_slider_scroll_changed(wxCommandEvent& event);
    wxString get_option_type_string(OptionType type) const;
#else
    void on_sliders_scroll_changed(wxCommandEvent& event);
#endif // ENABLE_GCODE_VIEWER
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_Preview_hpp_
