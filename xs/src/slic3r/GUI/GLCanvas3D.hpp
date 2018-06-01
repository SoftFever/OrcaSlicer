#ifndef slic3r_GLCanvas3D_hpp_
#define slic3r_GLCanvas3D_hpp_

#include "../../libslic3r/BoundingBox.hpp"
#include "../../libslic3r/Utils.hpp"
#include "../../libslic3r/ExPolygon.hpp"

class wxGLCanvas;
class wxGLContext;
class wxTimer;
class wxSizeEvent;
class wxIdleEvent;
class wxKeyEvent;
class wxMouseEvent;
class wxTimerEvent;

namespace Slic3r {

class GLVolumeCollection;
class GLVolume;
class DynamicPrintConfig;
class GLShader;
class ExPolygon;
class Print;
class PrintObject;

namespace GUI {

class GeometryBuffer
{
    std::vector<float> m_data;

public:
    bool set_from_triangles(const Polygons& triangles, float z);
    bool set_from_lines(const Lines& lines, float z);

    const float* get_data() const;
    unsigned int get_data_size() const;
};

class Size
{
    int m_width;
    int m_height;

public:
    Size();
    Size(int width, int height);

    int get_width() const;
    void set_width(int width);

    int get_height() const;
    void set_height(int height);
};

class Rect
{
    float m_left;
    float m_top;
    float m_right;
    float m_bottom;

public:
    Rect();
    Rect(float left, float top, float right, float bottom);

    float get_left() const;
    void set_left(float left);

    float get_top() const;
    void set_top(float top);

    float get_right() const;
    void set_right(float right);

    float get_bottom() const;
    void set_bottom(float bottom);
};

class GLCanvas3D
{
public:
    class Camera
    {
    public:
        enum EType : unsigned char
        {
            CT_Unknown,
            CT_Perspective,
            CT_Ortho,
            CT_Count
        };

    private:
        EType m_type;
        float m_zoom;
        float m_phi;
        float m_theta;
        float m_distance;
        Pointf3 m_target;

    public:
        Camera();

        Camera::EType get_type() const;
        void set_type(Camera::EType type);
        std::string get_type_as_string() const;

        float get_zoom() const;
        void set_zoom(float zoom);

        float get_phi() const;
        void set_phi(float phi);

        float get_theta() const;
        void set_theta(float theta);

        float get_distance() const;
        void set_distance(float distance);

        const Pointf3& get_target() const;
        void set_target(const Pointf3& target);
    };

    class Bed
    {
        Pointfs m_shape;
        BoundingBoxf3 m_bounding_box;
        Polygon m_polygon;
        GeometryBuffer m_triangles;
        GeometryBuffer m_gridlines;

    public:
        const Pointfs& get_shape() const;
        void set_shape(const Pointfs& shape);

        const BoundingBoxf3& get_bounding_box() const;
        bool contains(const Point& point) const;
        Point point_projection(const Point& point) const;

        void render() const;

    private:
        void _calc_bounding_box();
        void _calc_triangles(const ExPolygon& poly);
        void _calc_gridlines(const ExPolygon& poly, const BoundingBox& bed_bbox);
    };

    class Axes
    {
        Pointf3 m_origin;
        float m_length;

    public:
        Axes();

        const Pointf3& get_origin() const;
        void set_origin(const Pointf3& origin);

        float get_length() const;
        void set_length(float length);

        void render() const;
    };

    class CuttingPlane
    {
        float m_z;
        GeometryBuffer m_lines;

    public:
        CuttingPlane();

        bool set(float z, const ExPolygons& polygons);

        void render(const BoundingBoxf3& bb) const;

    private:
        void _render_plane(const BoundingBoxf3& bb) const;
        void _render_contour() const;
    };

    class Shader
    {
        GLShader* m_shader;

    public:
        Shader();
        ~Shader();

        bool init(const std::string& vertex_shader_filename, const std::string& fragment_shader_filename);

        bool is_initialized() const;

        bool start_using() const;
        void stop_using() const;

        void set_uniform(const std::string& name, float value) const;

        const GLShader* get_shader() const;

    private:
        void _reset();
    };

    class LayersEditing
    {
    public:
        enum EState : unsigned char
        {
            Unknown,
            Editing,
            Completed,
            Num_States
        };

    private:
        struct GLTextureData
        {
            unsigned int id;
            int width;
            int height;

            GLTextureData();
            GLTextureData(unsigned int id, int width, int height);
        };

        EState m_state;
        bool m_use_legacy_opengl;
        bool m_enabled;
        Shader m_shader;
        unsigned int m_z_texture_id;
        mutable GLTextureData m_tooltip_texture;
        mutable GLTextureData m_reset_texture;
        float m_band_width;
        float m_strength;
        int m_last_object_id;
        float m_last_z;
        unsigned int m_last_action;

    public:
        LayersEditing();
        ~LayersEditing();

        bool init(const std::string& vertex_shader_filename, const std::string& fragment_shader_filename);

        EState get_state() const;
        void set_state(EState state);

        bool is_allowed() const;
        void set_use_legacy_opengl(bool use_legacy_opengl);

        bool is_enabled() const;
        void set_enabled(bool enabled);

        unsigned int get_z_texture_id() const;

        float get_band_width() const;
        void set_band_width(float band_width);

        float get_strength() const;
        void set_strength(float strength);

        int get_last_object_id() const;
        void set_last_object_id(int id);

        float get_last_z() const;
        void set_last_z(float z);

        unsigned int get_last_action() const;
        void set_last_action(unsigned int action);

        void render(const GLCanvas3D& canvas, const PrintObject& print_object, const GLVolume& volume) const;

        const GLShader* get_shader() const;

        static float get_cursor_z_relative(const GLCanvas3D& canvas);
        static int get_first_selected_object_id(const GLVolumeCollection& volumes, unsigned int objects_count);
        static bool bar_rect_contains(const GLCanvas3D& canvas, float x, float y);
        static bool reset_rect_contains(const GLCanvas3D& canvas, float x, float y);
        static Rect get_bar_rect_screen(const GLCanvas3D& canvas);
        static Rect get_reset_rect_screen(const GLCanvas3D& canvas);
        static Rect get_bar_rect_viewport(const GLCanvas3D& canvas);
        static Rect get_reset_rect_viewport(const GLCanvas3D& canvas);

    private:
        bool _is_initialized() const;
        void _render_tooltip_texture(const GLCanvas3D& canvas, const Rect& bar_rect, const Rect& reset_rect) const;
        void _render_reset_texture(const GLCanvas3D& canvas, const Rect& reset_rect) const;
        void _render_active_object_annotations(const GLCanvas3D& canvas, const GLVolume& volume, const PrintObject& print_object, const Rect& bar_rect) const;
        void _render_profile(const PrintObject& print_object, const Rect& bar_rect) const;
        static GLTextureData _load_texture_from_file(const std::string& filename);
    };

    class Mouse
    {
        bool m_dragging;
        Pointf m_position;

    public:
        Mouse();

        bool is_dragging() const;
        void set_dragging(bool dragging);

        const Pointf& get_position() const;
        void set_position(const Pointf& position);
    };

    class Drag
    {
        Point m_start_position_2D;
        Pointf3 m_start_position_3D;
        Vectorf3 m_volume_center_offset;
        int m_volume_idx;
        
    public:
        Drag();

        const Point& get_start_position_2D() const;
        void set_start_position_2D(const Point& position);

        const Pointf3& get_start_position_3D() const;
        void set_start_position_3D(const Pointf3& position);

        bool is_start_position_2D_defined() const;
        bool is_start_position_3D_defined() const;

        const Vectorf3& get_volume_center_offset() const;
        void set_volume_center_offset(const Vectorf3& offset);

        int get_volume_idx() const;
        void set_volume_idx(int idx);
    };

private:
    wxGLCanvas* m_canvas;
    wxGLContext* m_context;
    wxTimer* m_timer;
    Camera m_camera;
    Bed m_bed;
    Axes m_axes;
    CuttingPlane m_cutting_plane;
    LayersEditing m_layers_editing;
    Shader m_shader;
    Mouse m_mouse;
    Drag m_drag;

    GLVolumeCollection* m_volumes;
    DynamicPrintConfig* m_config;
    Print* m_print;

    bool m_dirty;
    bool m_apply_zoom_to_volumes_filter;
    mutable int m_hover_volume_id;
    bool m_warning_texture_enabled;
    bool m_legend_texture_enabled;
    bool m_picking_enabled;
    bool m_moving_enabled;
    bool m_shader_enabled;
    bool m_multisample_allowed;

    PerlCallback m_on_viewport_changed_callback;
    PerlCallback m_on_double_click_callback;
    PerlCallback m_on_right_click_callback;
    PerlCallback m_on_select_callback;
    PerlCallback m_on_model_update_callback;
    PerlCallback m_on_move_callback;

public:
    GLCanvas3D(wxGLCanvas* canvas, wxGLContext* context);
    ~GLCanvas3D();

    bool init(bool useVBOs, bool use_legacy_opengl);

    bool set_current();

    bool is_dirty() const;
    void set_dirty(bool dirty);

    bool is_shown_on_screen() const;

    void resize(unsigned int w, unsigned int h);

    GLVolumeCollection* get_volumes();
    void set_volumes(GLVolumeCollection* volumes);
    void reset_volumes();
    void deselect_volumes();
    void select_volume(unsigned int id);

    void set_config(DynamicPrintConfig* config);
    void set_print(Print* print);

    // Set the bed shape to a single closed 2D polygon(array of two element arrays),
    // triangulate the bed and store the triangles into m_bed.m_triangles,
    // fills the m_bed.m_grid_lines and sets m_bed.m_origin.
    // Sets m_bed.m_polygon to limit the object placement.
    void set_bed_shape(const Pointfs& shape);
    // Used by ObjectCutDialog and ObjectPartsPanel to generate a rectangular ground plane to support the scene objects.
    void set_auto_bed_shape();

    const Pointf3& get_axes_origin() const;
    void set_axes_origin(const Pointf3& origin);

    float get_axes_length() const;
    void set_axes_length(float length);

    void set_cutting_plane(float z, const ExPolygons& polygons);

    Camera::EType get_camera_type() const;
    void set_camera_type(Camera::EType type);
    std::string get_camera_type_as_string() const;

    float get_camera_zoom() const;
    void set_camera_zoom(float zoom);

    float get_camera_phi() const;
    void set_camera_phi(float phi);

    float get_camera_theta() const;
    void set_camera_theta(float theta);

    float get_camera_distance() const;
    void set_camera_distance(float distance);

    const Pointf3& get_camera_target() const;
    void set_camera_target(const Pointf3& target);

    BoundingBoxf3 bed_bounding_box() const;
    BoundingBoxf3 volumes_bounding_box() const;
    BoundingBoxf3 max_bounding_box() const;

    bool is_layers_editing_enabled() const;
    bool is_picking_enabled() const;
    bool is_moving_enabled() const;
    bool is_layers_editing_allowed() const;
    bool is_multisample_allowed() const;

    void enable_layers_editing(bool enable);
    void enable_warning_texture(bool enable);
    void enable_legend_texture(bool enable);
    void enable_picking(bool enable);
    void enable_moving(bool enable);
    void enable_shader(bool enable);
    void allow_multisample(bool allow);

    bool is_mouse_dragging() const;
    void set_mouse_dragging(bool dragging);

    const Pointf& get_mouse_position() const;
    void set_mouse_position(const Pointf& position);

    int get_hover_volume_id() const;
    void set_hover_volume_id(int id);

    unsigned int get_layers_editing_z_texture_id() const;

    unsigned int get_layers_editing_state() const;
    void set_layers_editing_state(unsigned int state);

    float get_layers_editing_band_width() const;
    void set_layers_editing_band_width(float band_width);

    float get_layers_editing_strength() const;
    void set_layers_editing_strength(float strength);

    int get_layers_editing_last_object_id() const;
    void set_layers_editing_last_object_id(int id);

    float get_layers_editing_last_z() const;
    void set_layers_editing_last_z(float z);

    unsigned int get_layers_editing_last_action() const;
    void set_layers_editing_last_action(unsigned int action);

    const GLShader* get_layers_editing_shader() const;

    float get_layers_editing_cursor_z_relative() const;
    int get_layers_editing_first_selected_object_id(unsigned int objects_count) const;
    bool bar_rect_contains(float x, float y) const;
    bool reset_rect_contains(float x, float y) const;

    void zoom_to_bed();
    void zoom_to_volumes();
    void select_view(const std::string& direction);
    void set_viewport_from_scene(const GLCanvas3D& other);

    void update_volumes_colors_by_extruder();

    bool start_using_shader() const;
    void stop_using_shader() const;

    void render(bool useVBOs) const;
    void render_volumes(bool fake_colors) const;
    void render_texture(unsigned int tex_id, float left, float right, float bottom, float top) const;

    void register_on_viewport_changed_callback(void* callback);
    void register_on_double_click_callback(void* callback);
    void register_on_right_click_callback(void* callback);
    void register_on_select_callback(void* callback);
    void register_on_model_update_callback(void* callback);
    void register_on_move_callback(void* callback);

    void on_size(wxSizeEvent& evt);
    void on_idle(wxIdleEvent& evt);
    void on_char(wxKeyEvent& evt);
    void on_mouse_wheel(wxMouseEvent& evt);
    void on_timer(wxTimerEvent& evt);
    void on_mouse(wxMouseEvent& evt);

    Size get_canvas_size() const;
    Point get_local_mouse_position() const;

    void start_timer();
    void stop_timer();
    void perform_layer_editing_action(int y, bool shift_down, bool right_down);

private:
    void _zoom_to_bounding_box(const BoundingBoxf3& bbox);
    float _get_zoom_to_bounding_box_factor(const BoundingBoxf3& bbox) const;

    void _deregister_callbacks();

    void _mark_volumes_for_layer_height() const;
    void _refresh_if_shown_on_screen();

    void _camera_tranform() const;
    void _picking_pass() const;
    void _render_background() const;
    void _render_bed() const;
    void _render_axes() const;
    void _render_objects(bool useVBOs) const;
    void _render_cutting_plane() const;
    void _render_warning_texture() const;
    void _render_legend_texture() const;
    void _render_layer_editing_overlay() const;

    void _perform_layer_editing_action(wxMouseEvent* evt = nullptr);

    // Convert the screen space coordinate to an object space coordinate.
    // If the Z screen space coordinate is not provided, a depth buffer value is substituted.
    Pointf3 _mouse_to_3d(const Point& mouse_pos, float* z = nullptr);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvas3D_hpp_
