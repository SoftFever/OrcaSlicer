#ifndef slic3r_GLCanvas3DManager_hpp_
#define slic3r_GLCanvas3DManager_hpp_

#include "../../slic3r/GUI/GLCanvas3D.hpp"

#include <map>

namespace Slic3r {
namespace GUI {

class GLCanvas3DManager
{
    struct GLInfo
    {
        std::string version;
        std::string glsl_version;
        std::string vendor;
        std::string renderer;

        GLInfo();

        bool detect();
        bool is_version_greater_or_equal_to(unsigned int major, unsigned int minor) const;

        std::string to_string(bool format_as_html, bool extensions) const;
    };

    typedef std::map<wxGLCanvas*, GLCanvas3D*> CanvasesMap;

    CanvasesMap m_canvases;
    GLInfo m_gl_info;
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
    std::string get_gl_info(bool format_as_html, bool extensions) const;

    bool use_VBOs() const;
    bool layer_editing_allowed() const;

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
    bool init(wxGLCanvas* canvas);
//    bool init(wxGLCanvas* canvas, bool useVBOs);
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

    bool is_shown_on_screen(wxGLCanvas* canvas) const;

    void set_volumes(wxGLCanvas* canvas, GLVolumeCollection* volumes);
    void reset_volumes(wxGLCanvas* canvas);
    void deselect_volumes(wxGLCanvas* canvas);
    void select_volume(wxGLCanvas* canvas, unsigned int id);

    void set_config(wxGLCanvas* canvas, DynamicPrintConfig* config);
    void set_print(wxGLCanvas* canvas, Print* print);

    void set_bed_shape(wxGLCanvas* canvas, const Pointfs& shape);
    void set_auto_bed_shape(wxGLCanvas* canvas);

    BoundingBoxf3 get_volumes_bounding_box(wxGLCanvas* canvas);

    void set_axes_length(wxGLCanvas* canvas, float length);

    void set_cutting_plane(wxGLCanvas* canvas, float z, const ExPolygons& polygons);

    bool is_layers_editing_enabled(wxGLCanvas* canvas) const;
    bool is_layers_editing_allowed(wxGLCanvas* canvas) const;
    bool is_shader_enabled(wxGLCanvas* canvas) const;

    void enable_layers_editing(wxGLCanvas* canvas, bool enable);
    void enable_warning_texture(wxGLCanvas* canvas, bool enable);
    void enable_legend_texture(wxGLCanvas* canvas, bool enable);
    void enable_picking(wxGLCanvas* canvas, bool enable);
    void enable_moving(wxGLCanvas* canvas, bool enable);
    void enable_shader(wxGLCanvas* canvas, bool enable);
    void allow_multisample(wxGLCanvas* canvas, bool allow);

    void zoom_to_bed(wxGLCanvas* canvas);
    void zoom_to_volumes(wxGLCanvas* canvas);
    void select_view(wxGLCanvas* canvas, const std::string& direction);
    void set_viewport_from_scene(wxGLCanvas* canvas, wxGLCanvas* other);

    void update_volumes_colors_by_extruder(wxGLCanvas* canvas);

    void render(wxGLCanvas* canvas) const;

    void register_on_viewport_changed_callback(wxGLCanvas* canvas, void* callback);
    void register_on_double_click_callback(wxGLCanvas* canvas, void* callback);
    void register_on_right_click_callback(wxGLCanvas* canvas, void* callback);
    void register_on_select_callback(wxGLCanvas* canvas, void* callback);
    void register_on_model_update_callback(wxGLCanvas* canvas, void* callback);
    void register_on_move_callback(wxGLCanvas* canvas, void* callback);

private:
    CanvasesMap::iterator _get_canvas(wxGLCanvas* canvas);
    CanvasesMap::const_iterator _get_canvas(wxGLCanvas* canvas) const;

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
    bool _init(GLCanvas3D& canvas);
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvas3DManager_hpp_
