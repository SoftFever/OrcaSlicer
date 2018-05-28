#ifndef slic3r_GLCanvas3DManager_hpp_
#define slic3r_GLCanvas3DManager_hpp_

#include "../../slic3r/GUI/GLCanvas3D.hpp"

#include <map>

namespace Slic3r {
namespace GUI {

class GLCanvas3DManager
{
    struct GLVersion
    {
        unsigned int vn_major;
        unsigned int vn_minor;

        GLVersion();
        bool detect();

        bool is_greater_or_equal_to(unsigned int major, unsigned int minor) const;
    };

    typedef std::map<wxGLCanvas*, GLCanvas3D*> CanvasesMap;

    CanvasesMap m_canvases;
    GLVersion m_gl_version;
    bool m_gl_initialized;
    bool m_use_legacy_opengl;
    bool m_use_VBOs;

public:
    GLCanvas3DManager();

    bool add(wxGLCanvas* canvas, wxGLContext* context);
    bool remove(wxGLCanvas* canvas);

    void remove_all();

    unsigned int count() const;

    void init_gl();

    bool use_VBOs() const;
    bool layer_editing_allowed() const;

    bool init(wxGLCanvas* canvas, bool useVBOs);

    bool is_dirty(wxGLCanvas* canvas) const;
    void set_dirty(wxGLCanvas* canvas, bool dirty);

    bool is_shown_on_screen(wxGLCanvas* canvas) const;

    void resize(wxGLCanvas* canvas, unsigned int w, unsigned int h);

    GLVolumeCollection* get_volumes(wxGLCanvas* canvas);
    void set_volumes(wxGLCanvas* canvas, GLVolumeCollection* volumes);
    void reset_volumes(wxGLCanvas* canvas);
    void deselect_volumes(wxGLCanvas* canvas);
    void select_volume(wxGLCanvas* canvas, unsigned int id);

    DynamicPrintConfig* get_config(wxGLCanvas* canvas);
    void set_config(wxGLCanvas* canvas, DynamicPrintConfig* config);

    void set_bed_shape(wxGLCanvas* canvas, const Pointfs& shape);
    void set_auto_bed_shape(wxGLCanvas* canvas);

    BoundingBoxf3 get_bed_bounding_box(wxGLCanvas* canvas);
    BoundingBoxf3 get_volumes_bounding_box(wxGLCanvas* canvas);
    BoundingBoxf3 get_max_bounding_box(wxGLCanvas* canvas);

    Pointf3 get_axes_origin(wxGLCanvas* canvas) const;
    void set_axes_origin(wxGLCanvas* canvas, const Pointf3& origin);

    float get_axes_length(wxGLCanvas* canvas) const;
    void set_axes_length(wxGLCanvas* canvas, float length);

    void set_cutting_plane(wxGLCanvas* canvas, float z, const ExPolygons& polygons);

    unsigned int get_camera_type(wxGLCanvas* canvas) const;
    void set_camera_type(wxGLCanvas* canvas, unsigned int type);
    std::string get_camera_type_as_string(wxGLCanvas* canvas) const;
    
    float get_camera_zoom(wxGLCanvas* canvas) const;
    void set_camera_zoom(wxGLCanvas* canvas, float zoom);

    float get_camera_phi(wxGLCanvas* canvas) const;
    void set_camera_phi(wxGLCanvas* canvas, float phi);

    float get_camera_theta(wxGLCanvas* canvas) const;
    void set_camera_theta(wxGLCanvas* canvas, float theta);

    float get_camera_distance(wxGLCanvas* canvas) const;
    void set_camera_distance(wxGLCanvas* canvas, float distance);

    Pointf3 get_camera_target(wxGLCanvas* canvas) const;
    void set_camera_target(wxGLCanvas* canvas, const Pointf3& target);

    bool is_layers_editing_enabled(wxGLCanvas* canvas) const;
    bool is_picking_enabled(wxGLCanvas* canvas) const;
    bool is_layers_editing_allowed(wxGLCanvas* canvas) const;
    bool is_multisample_allowed(wxGLCanvas* canvas) const;

    void enable_layers_editing(wxGLCanvas* canvas, bool enable);
    void enable_warning_texture(wxGLCanvas* canvas, bool enable);
    void enable_legend_texture(wxGLCanvas* canvas, bool enable);
    void enable_picking(wxGLCanvas* canvas, bool enable);
    void enable_shader(wxGLCanvas* canvas, bool enable);
    void allow_multisample(wxGLCanvas* canvas, bool allow);

    bool is_mouse_dragging(wxGLCanvas* canvas) const;
    void set_mouse_dragging(wxGLCanvas* canvas, bool dragging);

    Pointf get_mouse_position(wxGLCanvas* canvas) const;
    void set_mouse_position(wxGLCanvas* canvas, const Pointf& position);

    int get_hover_volume_id(wxGLCanvas* canvas) const;
    void set_hover_volume_id(wxGLCanvas* canvas, int id);

    unsigned int get_layers_editing_z_texture_id(wxGLCanvas* canvas) const;

    float get_layers_editing_band_width(wxGLCanvas* canvas) const;
    void set_layers_editing_band_width(wxGLCanvas* canvas, float band_width);

    float get_layers_editing_strength(wxGLCanvas* canvas) const;
    void set_layers_editing_strength(wxGLCanvas* canvas, float strength);

    int get_layers_editing_last_object_id(wxGLCanvas* canvas) const;
    void set_layers_editing_last_object_id(wxGLCanvas* canvas, int id);

    float get_layers_editing_last_z(wxGLCanvas* canvas) const;
    void set_layers_editing_last_z(wxGLCanvas* canvas, float z);

    unsigned int get_layers_editing_last_action(wxGLCanvas* canvas) const;
    void set_layers_editing_last_action(wxGLCanvas* canvas, unsigned int action);

    GLShader* get_layers_editing_shader(wxGLCanvas* canvas);

    float get_layers_editing_cursor_z_relative(wxGLCanvas* canvas) const;
    int get_layers_editing_first_selected_object_id(wxGLCanvas* canvas, unsigned int objects_count) const;

    void zoom_to_bed(wxGLCanvas* canvas);
    void zoom_to_volumes(wxGLCanvas* canvas);
    void select_view(wxGLCanvas* canvas, const std::string& direction);

    bool start_using_shader(wxGLCanvas* canvas) const;
    void stop_using_shader(wxGLCanvas* canvas) const;

    void picking_pass(wxGLCanvas* canvas);

    void render_background(wxGLCanvas* canvas) const;
    void render_bed(wxGLCanvas* canvas) const;
    void render_axes(wxGLCanvas* canvas) const;
    void render_volumes(wxGLCanvas* canvas, bool fake_colors) const;
    void render_objects(wxGLCanvas* canvas, bool useVBOs);
    void render_cutting_plane(wxGLCanvas* canvas) const;
    void render_warning_texture(wxGLCanvas* canvas) const;
    void render_legend_texture(wxGLCanvas* canvas) const;
    void render_layer_editing_overlay(wxGLCanvas* canvas, const Print& print) const;

    void render_texture(wxGLCanvas* canvas, unsigned int tex_id, float left, float right, float bottom, float top) const;

    void register_on_viewport_changed_callback(wxGLCanvas* canvas, void* callback);
    void register_on_mark_volumes_for_layer_height_callback(wxGLCanvas* canvas, void* callback);

private:
    CanvasesMap::iterator _get_canvas(wxGLCanvas* canvas);
    CanvasesMap::const_iterator _get_canvas(wxGLCanvas* canvas) const;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvas3DManager_hpp_
