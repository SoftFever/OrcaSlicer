#ifndef slic3r_GUI_Preview_hpp_
#define slic3r_GUI_Preview_hpp_

#include <wx/panel.h>

#include "libslic3r/Point.hpp"
#include "libslic3r/CustomGCode.hpp"

#include <string>
#include "libslic3r/GCode/GCodeProcessor.hpp"

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
    wxBoxSizer* m_left_sizer;
    wxBoxSizer* m_layers_slider_sizer;
    wxPanel* m_bottom_toolbar_panel;
    wxStaticText* m_label_view_type;
    wxChoice* m_choice_view_type;
    wxStaticText* m_label_show;
    wxComboCtrl* m_combochecklist_features;
    size_t m_combochecklist_features_pos;
    wxComboCtrl* m_combochecklist_options;

    DynamicPrintConfig* m_config;
    BackgroundSlicingProcess* m_process;
    GCodeProcessor::Result* m_gcode_result;

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

    DoubleSlider::Control* m_layers_slider{ nullptr };
    DoubleSlider::Control* m_moves_slider{ nullptr };

public:
    enum class OptionType : unsigned int
    {
        Travel,
#if ENABLE_SHOW_WIPE_MOVES
        Wipe,
#endif // ENABLE_SHOW_WIPE_MOVES
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
    virtual ~Preview();

    wxGLCanvas* get_wxglcanvas() { return m_canvas_widget; }
    GLCanvas3D* get_canvas3d() { return m_canvas; }

    void set_as_dirty();

    void set_number_extruders(unsigned int number_extruders);
    void bed_shape_changed();
    void select_view(const std::string& direction);
    void set_drop_target(wxDropTarget* target);

    void load_print(bool keep_z_range = false);
    void reload_print(bool keep_volumes = false);
    void refresh_print();

    void msw_rescale();
    void jump_layers_slider(wxKeyEvent& evt);
    void move_layers_slider(wxKeyEvent& evt);
    void edit_layers_slider(wxKeyEvent& evt);

    void update_view_type(bool keep_volumes);

    bool is_loaded() const { return m_loaded; }

    void update_bottom_toolbar();
    void update_moves_slider();
    void enable_moves_slider(bool enable);
    void hide_layers_slider();

private:
    bool init(wxWindow* parent, Model* model);

    void bind_event_handlers();
    void unbind_event_handlers();

    void on_size(wxSizeEvent& evt);
    void on_choice_view_type(wxCommandEvent& evt);
    void on_combochecklist_features(wxCommandEvent& evt);
    void on_combochecklist_options(wxCommandEvent& evt);

    // Create/Update/Reset double slider on 3dPreview
    wxBoxSizer* create_layers_slider_sizer();
    void check_layers_slider_values(std::vector<CustomGCode::Item>& ticks_from_model,
        const std::vector<double>& layers_z);
    void reset_layers_slider();
    void update_layers_slider(const std::vector<double>& layers_z, bool keep_z_range = false);
    void update_layers_slider_mode();
    // update vertical DoubleSlider after keyDown in canvas
    void update_layers_slider_from_canvas(wxKeyEvent& event);

    void load_print_as_fff(bool keep_z_range = false);
    void load_print_as_sla();

    void on_layers_slider_scroll_changed(wxCommandEvent& event);
    void on_moves_slider_scroll_changed(wxCommandEvent& event);
    wxString get_option_type_string(OptionType type) const;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GUI_Preview_hpp_
