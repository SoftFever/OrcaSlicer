#ifndef slic3r_GLCanvas3DManager_hpp_
#define slic3r_GLCanvas3DManager_hpp_

#include "../../libslic3r/BoundingBox.hpp"

#include <map>
#include <vector>

class wxWindow;
class wxGLCanvas;
class wxGLContext;

namespace Slic3r {

class DynamicPrintConfig;
class Print;
class Model;
class ExPolygon;
typedef std::vector<ExPolygon> ExPolygons;
class ModelObject;
class PrintObject;
class GCodePreviewData;

namespace GUI {

class GLCanvas3D;

class GLCanvas3DManager
{
    struct GLInfo
    {
        std::string version;
        std::string glsl_version;
        std::string vendor;
        std::string renderer;

        GLInfo();

        void detect();
        bool is_version_greater_or_equal_to(unsigned int major, unsigned int minor) const;

        std::string to_string(bool format_as_html, bool extensions) const;
    };

    enum EMultisampleState : unsigned char
    {
        MS_Unknown,
        MS_Enabled,
        MS_Disabled
    };

    typedef std::map<wxGLCanvas*, GLCanvas3D*> CanvasesMap;

    CanvasesMap m_canvases;
#if ENABLE_USE_UNIQUE_GLCONTEXT
    wxGLContext* m_context;
#endif // ENABLE_USE_UNIQUE_GLCONTEXT
    wxGLCanvas* m_current;
    GLInfo m_gl_info;
    bool m_gl_initialized;
    bool m_use_legacy_opengl;
    bool m_use_VBOs;
    static EMultisampleState s_multisample;

public:
    GLCanvas3DManager();
#if ENABLE_USE_UNIQUE_GLCONTEXT
    ~GLCanvas3DManager();
#endif // ENABLE_USE_UNIQUE_GLCONTEXT

    bool add(wxGLCanvas* canvas);
    bool remove(wxGLCanvas* canvas);

    void remove_all();

    unsigned int count() const;

    void init_gl();
    std::string get_gl_info(bool format_as_html, bool extensions) const;

    bool use_VBOs() const;
    bool layer_editing_allowed() const;

    bool init(wxGLCanvas* canvas);

    void set_as_dirty(wxGLCanvas* canvas);

    unsigned int get_volumes_count(wxGLCanvas* canvas) const;
    void reset_volumes(wxGLCanvas* canvas);
    int check_volumes_outside_state(wxGLCanvas* canvas, const DynamicPrintConfig* config) const;
    bool move_volume_up(wxGLCanvas* canvas, unsigned int id);
    bool move_volume_down(wxGLCanvas* canvas, unsigned int id);

    GLCanvas3D* get_canvas(wxGLCanvas* canvas);

    void set_config(wxGLCanvas* canvas, DynamicPrintConfig* config);
    void set_print(wxGLCanvas* canvas, Print* print);
    void set_model(wxGLCanvas* canvas, Model* model);

    void set_bed_shape(wxGLCanvas* canvas, const Pointfs& shape);
    void set_auto_bed_shape(wxGLCanvas* canvas);

    BoundingBoxf3 get_volumes_bounding_box(wxGLCanvas* canvas);

    void set_axes_length(wxGLCanvas* canvas, float length);

    void set_cutting_plane(wxGLCanvas* canvas, float z, const ExPolygons& polygons);

    void set_color_by(wxGLCanvas* canvas, const std::string& value);

    bool is_layers_editing_enabled(wxGLCanvas* canvas) const;
    bool is_layers_editing_allowed(wxGLCanvas* canvas) const;
    bool is_shader_enabled(wxGLCanvas* canvas) const;

    bool is_reload_delayed(wxGLCanvas* canvas) const;

    void enable_layers_editing(wxGLCanvas* canvas, bool enable);
    void enable_warning_texture(wxGLCanvas* canvas, bool enable);
    void enable_legend_texture(wxGLCanvas* canvas, bool enable);
    void enable_picking(wxGLCanvas* canvas, bool enable);
    void enable_moving(wxGLCanvas* canvas, bool enable);
    void enable_gizmos(wxGLCanvas* canvas, bool enable);
    void enable_toolbar(wxGLCanvas* canvas, bool enable);
    void enable_shader(wxGLCanvas* canvas, bool enable);
    void enable_force_zoom_to_bed(wxGLCanvas* canvas, bool enable);
    void enable_dynamic_background(wxGLCanvas* canvas, bool enable);
    void allow_multisample(wxGLCanvas* canvas, bool allow);

    void enable_toolbar_item(wxGLCanvas* canvas, const std::string& name, bool enable);
    bool is_toolbar_item_pressed(wxGLCanvas* canvas, const std::string& name) const;

    void zoom_to_bed(wxGLCanvas* canvas);
    void zoom_to_volumes(wxGLCanvas* canvas);
    void select_view(wxGLCanvas* canvas, const std::string& direction);
    void set_viewport_from_scene(wxGLCanvas* canvas, wxGLCanvas* other);

    void update_volumes_colors_by_extruder(wxGLCanvas* canvas);

    void render(wxGLCanvas* canvas) const;

    std::vector<double> get_current_print_zs(wxGLCanvas* canvas, bool active_only) const;
    void set_toolpaths_range(wxGLCanvas* canvas, double low, double high);

    std::vector<int> load_object(wxGLCanvas* canvas, const ModelObject* model_object, int obj_idx, std::vector<int> instance_idxs);
    std::vector<int> load_object(wxGLCanvas* canvas, const Model* model, int obj_idx);

    int get_first_volume_id(wxGLCanvas* canvas, int obj_idx) const;
    int get_in_object_volume_id(wxGLCanvas* canvas, int scene_vol_idx) const;

    void mirror_selection(wxGLCanvas* canvas, Axis axis);

    void reload_scene(wxGLCanvas* canvas, bool force);

    void load_gcode_preview(wxGLCanvas* canvas, const GCodePreviewData* preview_data, const std::vector<std::string>& str_tool_colors);
    void load_preview(wxGLCanvas* canvas, const std::vector<std::string>& str_tool_colors);

    void reset_legend_texture(wxGLCanvas* canvas);

    static bool can_multisample() { return s_multisample == MS_Enabled; }
    static wxGLCanvas* create_wxglcanvas(wxWindow *parent);

private:
    CanvasesMap::iterator _get_canvas(wxGLCanvas* canvas);
    CanvasesMap::const_iterator _get_canvas(wxGLCanvas* canvas) const;

    bool _init(GLCanvas3D& canvas);
    static void _detect_multisample(int* attribList);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvas3DManager_hpp_
