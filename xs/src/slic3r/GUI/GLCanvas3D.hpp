#ifndef slic3r_GLCanvas3D_hpp_
#define slic3r_GLCanvas3D_hpp_

#include "../../slic3r/GUI/3DScene.hpp"
#include "../../slic3r/GUI/GLTexture.hpp"

class wxTimer;
class wxSizeEvent;
class wxIdleEvent;
class wxKeyEvent;
class wxMouseEvent;
class wxTimerEvent;
class wxPaintEvent;

namespace Slic3r {

class GLShader;
class ExPolygon;

namespace GUI {

class GLGizmoBase;

class GeometryBuffer
{
    std::vector<float> m_vertices;
    std::vector<float> m_tex_coords;

public:
    bool set_from_triangles(const Polygons& triangles, float z, bool generate_tex_coords);
    bool set_from_lines(const Lines& lines, float z);

    const float* get_vertices() const;
    const float* get_tex_coords() const;

    unsigned int get_vertices_count() const;
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
    struct GCodePreviewVolumeIndex
    {
        enum EType
        {
            Extrusion,
            Travel,
            Retraction,
            Unretraction,
            Shell,
            Num_Geometry_Types
        };

        struct FirstVolume
        {
            EType type;
            unsigned int flag;
            // Index of the first volume in a GLVolumeCollection.
            unsigned int id;

            FirstVolume(EType type, unsigned int flag, unsigned int id) : type(type), flag(flag), id(id) {}
        };

        std::vector<FirstVolume> first_volumes;

        void reset() { first_volumes.clear(); }
    };

public:
    struct Camera
    {
        enum EType : unsigned char
        {
            Unknown,
//            Perspective,
            Ortho,
            Num_types
        };

        EType type;
        float zoom;
        float phi;
//        float distance;
        Pointf3 target;

    private:
        float m_theta;

    public:
        Camera();

        std::string get_type_as_string() const;

        float get_theta() const;
        void set_theta(float theta);
    };

    class Bed
    {
    public:
        enum EType : unsigned char
        {
            MK2,
            MK3,
            Custom,
            Num_Types
        };

    private:
        EType m_type;
        Pointfs m_shape;
        BoundingBoxf3 m_bounding_box;
        Polygon m_polygon;
        GeometryBuffer m_triangles;
        GeometryBuffer m_gridlines;
        mutable GLTexture m_top_texture;
        mutable GLTexture m_bottom_texture;

    public:
        Bed();

        bool is_prusa() const;
        bool is_custom() const;

        const Pointfs& get_shape() const;
        // Return true if the bed shape changed, so the calee will update the UI.
        bool set_shape(const Pointfs& shape);

        const BoundingBoxf3& get_bounding_box() const;
        bool contains(const Point& point) const;
        Point point_projection(const Point& point) const;

        void render(float theta) const;

    private:
        void _calc_bounding_box();
        void _calc_triangles(const ExPolygon& poly);
        void _calc_gridlines(const ExPolygon& poly, const BoundingBox& bed_bbox);
        EType _detect_type() const;
        void _render_mk2(float theta) const;
        void _render_mk3(float theta) const;
        void _render_prusa(float theta) const;
        void _render_custom() const;
        static bool _are_equal(const Pointfs& bed_1, const Pointfs& bed_2);
    };

    struct Axes
    {
        Pointf3 origin;
        float length;

        Axes();

        void render(bool depth_test) const;
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
        void set_uniform(const std::string& name, const float* matrix) const;

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
        bool m_use_legacy_opengl;
        bool m_enabled;
        Shader m_shader;
        unsigned int m_z_texture_id;
        mutable GLTexture m_tooltip_texture;
        mutable GLTexture m_reset_texture;

    public:
        EState state;
        float band_width;
        float strength;
        int last_object_id;
        float last_z;
        unsigned int last_action;

        LayersEditing();
        ~LayersEditing();

        bool init(const std::string& vertex_shader_filename, const std::string& fragment_shader_filename);

        bool is_allowed() const;
        void set_use_legacy_opengl(bool use_legacy_opengl);

        bool is_enabled() const;
        void set_enabled(bool enabled);

        unsigned int get_z_texture_id() const;

        void render(const GLCanvas3D& canvas, const PrintObject& print_object, const GLVolume& volume) const;

        int get_shader_program_id() const;

        static float get_cursor_z_relative(const GLCanvas3D& canvas);
        static bool bar_rect_contains(const GLCanvas3D& canvas, float x, float y);
        static bool reset_rect_contains(const GLCanvas3D& canvas, float x, float y);
        static Rect get_bar_rect_screen(const GLCanvas3D& canvas);
        static Rect get_reset_rect_screen(const GLCanvas3D& canvas);
        static Rect get_bar_rect_viewport(const GLCanvas3D& canvas);
        static Rect get_reset_rect_viewport(const GLCanvas3D& canvas);

    private:
        bool _is_initialized() const;
        void _render_tooltip_texture(const GLCanvas3D& canvas, const Rect& bar_rect, const Rect& reset_rect) const;
        void _render_reset_texture(const Rect& reset_rect) const;
        void _render_active_object_annotations(const GLCanvas3D& canvas, const GLVolume& volume, const PrintObject& print_object, const Rect& bar_rect) const;
        void _render_profile(const PrintObject& print_object, const Rect& bar_rect) const;
    };

    struct Mouse
    {
        struct Drag
        {
            static const Point Invalid_2D_Point;
            static const Pointf3 Invalid_3D_Point;

            Point start_position_2D;
            Pointf3 start_position_3D;
            Vectorf3 volume_center_offset;

            bool move_with_shift;
            int move_volume_idx;
            int gizmo_volume_idx;

        public:
            Drag();
        };

        bool dragging;
        Pointf position;
        Drag drag;

        Mouse();

        void set_start_position_2D_as_invalid();
        void set_start_position_3D_as_invalid();

        bool is_start_position_2D_defined() const;
        bool is_start_position_3D_defined() const;
    };

    class Gizmos
    {
        static const float OverlayTexturesScale;
        static const float OverlayOffsetX;
        static const float OverlayGapY;

    public:
        enum EType : unsigned char
        {
            Undefined,
            Scale,
            Rotate,
            Num_Types
        };

    private:
        bool m_enabled;
        typedef std::map<EType, GLGizmoBase*> GizmosMap;
        GizmosMap m_gizmos;
        EType m_current;
        bool m_dragging;

    public:
        Gizmos();
        ~Gizmos();

        bool init();

        bool is_enabled() const;
        void set_enabled(bool enable);

        void update_hover_state(const GLCanvas3D& canvas, const Pointf& mouse_pos);
        void update_on_off_state(const GLCanvas3D& canvas, const Pointf& mouse_pos);
        void reset_all_states();

        void set_hover_id(int id);

        bool overlay_contains_mouse(const GLCanvas3D& canvas, const Pointf& mouse_pos) const;
        bool grabber_contains_mouse() const;
        void update(const Pointf& mouse_pos);
        void refresh();

        EType get_current_type() const;

        bool is_running() const;

        bool is_dragging() const;
        void start_dragging();
        void stop_dragging();

        float get_scale() const;
        void set_scale(float scale);

        float get_angle_z() const;
        void set_angle_z(float angle_z);

        void render(const GLCanvas3D& canvas, const BoundingBoxf3& box) const;
        void render_current_gizmo_for_picking_pass(const BoundingBoxf3& box) const;

    private:
        void _reset();

        void _render_overlay(const GLCanvas3D& canvas) const;
        void _render_current_gizmo(const BoundingBoxf3& box) const;

        float _get_total_overlay_height() const;
        GLGizmoBase* _get_current() const;
    };

    class WarningTexture : public GUI::GLTexture
    {
        static const unsigned char Background_Color[3];
        static const unsigned char Opacity;

    public:
        bool generate(const std::string& msg);
    };

    class LegendTexture : public GUI::GLTexture
    {
        static const int Px_Title_Offset = 5;
        static const int Px_Text_Offset = 5;
        static const int Px_Square = 20;
        static const int Px_Square_Contour = 1;
        static const int Px_Border = Px_Square / 2;
        static const unsigned char Squares_Border_Color[3];
        static const unsigned char Background_Color[3];
        static const unsigned char Opacity;

    public:
        bool generate(const GCodePreviewData& preview_data, const std::vector<float>& tool_colors);
    };

private:
    wxGLCanvas* m_canvas;
    wxGLContext* m_context;
    LegendTexture m_legend_texture;
    WarningTexture m_warning_texture;
    wxTimer* m_timer;
    Camera m_camera;
    Bed m_bed;
    Axes m_axes;
    CuttingPlane m_cutting_plane;
    LayersEditing m_layers_editing;
    Shader m_shader;
    Mouse m_mouse;
    mutable Gizmos m_gizmos;

    mutable GLVolumeCollection m_volumes;
    DynamicPrintConfig* m_config;
    Print* m_print;
    Model* m_model;

    bool m_dirty;
    bool m_initialized;
    bool m_use_VBOs;
    bool m_force_zoom_to_bed_enabled;
    bool m_apply_zoom_to_volumes_filter;
    mutable int m_hover_volume_id;
    bool m_warning_texture_enabled;
    bool m_legend_texture_enabled;
    bool m_picking_enabled;
    bool m_moving_enabled;
    bool m_shader_enabled;
    bool m_dynamic_background_enabled;
    bool m_multisample_allowed;

    std::string m_color_by;
    std::string m_select_by;
    std::string m_drag_by;

    bool m_reload_delayed;
    std::vector<std::vector<int>> m_objects_volumes_idxs;
    std::vector<int> m_objects_selections;

    GCodePreviewVolumeIndex m_gcode_preview_volume_index;

    PerlCallback m_on_viewport_changed_callback;
    PerlCallback m_on_double_click_callback;
    PerlCallback m_on_right_click_callback;
    PerlCallback m_on_select_object_callback;
    PerlCallback m_on_model_update_callback;
    PerlCallback m_on_remove_object_callback;
    PerlCallback m_on_arrange_callback;
    PerlCallback m_on_rotate_object_left_callback;
    PerlCallback m_on_rotate_object_right_callback;
    PerlCallback m_on_scale_object_uniformly_callback;
    PerlCallback m_on_increase_objects_callback;
    PerlCallback m_on_decrease_objects_callback;
    PerlCallback m_on_instance_moved_callback;
    PerlCallback m_on_wipe_tower_moved_callback;
    PerlCallback m_on_enable_action_buttons_callback;
    PerlCallback m_on_gizmo_scale_uniformly_callback;
    PerlCallback m_on_gizmo_rotate_callback;
    PerlCallback m_on_update_geometry_info_callback;

public:
    GLCanvas3D(wxGLCanvas* canvas);
    ~GLCanvas3D();

    bool init(bool useVBOs, bool use_legacy_opengl);

    bool set_current();

    void set_as_dirty();

    unsigned int get_volumes_count() const;
    void reset_volumes();
    void deselect_volumes();
    void select_volume(unsigned int id);
    void update_volumes_selection(const std::vector<int>& selections);
    int check_volumes_outside_state(const DynamicPrintConfig* config) const;
    bool move_volume_up(unsigned int id);
    bool move_volume_down(unsigned int id);

    void set_objects_selections(const std::vector<int>& selections);

    void set_config(DynamicPrintConfig* config);
    void set_print(Print* print);
    void set_model(Model* model);

    // Set the bed shape to a single closed 2D polygon(array of two element arrays),
    // triangulate the bed and store the triangles into m_bed.m_triangles,
    // fills the m_bed.m_grid_lines and sets m_bed.m_origin.
    // Sets m_bed.m_polygon to limit the object placement.
    void set_bed_shape(const Pointfs& shape);
    // Used by ObjectCutDialog and ObjectPartsPanel to generate a rectangular ground plane to support the scene objects.
    void set_auto_bed_shape();

    void set_axes_length(float length);

    void set_cutting_plane(float z, const ExPolygons& polygons);

    void set_color_by(const std::string& value);
    void set_select_by(const std::string& value);
    void set_drag_by(const std::string& value);

    float get_camera_zoom() const;

    BoundingBoxf3 volumes_bounding_box() const;

    bool is_layers_editing_enabled() const;
    bool is_layers_editing_allowed() const;
    bool is_shader_enabled() const;

    bool is_reload_delayed() const;

    void enable_layers_editing(bool enable);
    void enable_warning_texture(bool enable);
    void enable_legend_texture(bool enable);
    void enable_picking(bool enable);
    void enable_moving(bool enable);
    void enable_gizmos(bool enable);
    void enable_shader(bool enable);
    void enable_force_zoom_to_bed(bool enable);
    void enable_dynamic_background(bool enable);
    void allow_multisample(bool allow);

    void zoom_to_bed();
    void zoom_to_volumes();
    void select_view(const std::string& direction);
    void set_viewport_from_scene(const GLCanvas3D& other);

    void update_volumes_colors_by_extruder();
    void update_gizmos_data();

    void render();

    std::vector<double> get_current_print_zs(bool active_only) const;
    void set_toolpaths_range(double low, double high);

    std::vector<int> load_object(const ModelObject& model_object, int obj_idx, std::vector<int> instance_idxs);
    std::vector<int> load_object(const Model& model, int obj_idx);

    void reload_scene(bool force);

    void load_gcode_preview(const GCodePreviewData& preview_data, const std::vector<std::string>& str_tool_colors);
    void load_preview(const std::vector<std::string>& str_tool_colors);

    void register_on_viewport_changed_callback(void* callback);
    void register_on_double_click_callback(void* callback);
    void register_on_right_click_callback(void* callback);
    void register_on_select_object_callback(void* callback);
    void register_on_model_update_callback(void* callback);
    void register_on_remove_object_callback(void* callback);
    void register_on_arrange_callback(void* callback);
    void register_on_rotate_object_left_callback(void* callback);
    void register_on_rotate_object_right_callback(void* callback);
    void register_on_scale_object_uniformly_callback(void* callback);
    void register_on_increase_objects_callback(void* callback);
    void register_on_decrease_objects_callback(void* callback);
    void register_on_instance_moved_callback(void* callback);
    void register_on_wipe_tower_moved_callback(void* callback);
    void register_on_enable_action_buttons_callback(void* callback);
    void register_on_gizmo_scale_uniformly_callback(void* callback);
    void register_on_gizmo_rotate_callback(void* callback);
    void register_on_update_geometry_info_callback(void* callback);

    void bind_event_handlers();
    void unbind_event_handlers();

    void on_size(wxSizeEvent& evt);
    void on_idle(wxIdleEvent& evt);
    void on_char(wxKeyEvent& evt);
    void on_mouse_wheel(wxMouseEvent& evt);
    void on_timer(wxTimerEvent& evt);
    void on_mouse(wxMouseEvent& evt);
    void on_paint(wxPaintEvent& evt);
    void on_key_down(wxKeyEvent& evt);

    Size get_canvas_size() const;
    Point get_local_mouse_position() const;

    void reset_legend_texture();

private:
    bool _is_shown_on_screen() const;
    void _force_zoom_to_bed();

    void _resize(unsigned int w, unsigned int h);

    BoundingBoxf3 _max_bounding_box() const;
    BoundingBoxf3 _selected_volumes_bounding_box() const;

    void _zoom_to_bounding_box(const BoundingBoxf3& bbox);
    float _get_zoom_to_bounding_box_factor(const BoundingBoxf3& bbox) const;

    void _deregister_callbacks();

    void _mark_volumes_for_layer_height() const;
    void _refresh_if_shown_on_screen();

    void _camera_tranform() const;
    void _picking_pass() const;
    void _render_background() const;
    void _render_bed(float theta) const;
    void _render_axes(bool depth_test) const;
    void _render_objects() const;
    void _render_cutting_plane() const;
    void _render_warning_texture() const;
    void _render_legend_texture() const;
    void _render_layer_editing_overlay() const;
    void _render_volumes(bool fake_colors) const;
    void _render_gizmo() const;

    float _get_layers_editing_cursor_z_relative() const;
    void _perform_layer_editing_action(wxMouseEvent* evt = nullptr);

    // Convert the screen space coordinate to an object space coordinate.
    // If the Z screen space coordinate is not provided, a depth buffer value is substituted.
    Pointf3 _mouse_to_3d(const Point& mouse_pos, float* z = nullptr);

    // Convert the screen space coordinate to world coordinate on the bed.
    Pointf3 _mouse_to_bed_3d(const Point& mouse_pos);

    void _start_timer();
    void _stop_timer();

    int _get_first_selected_object_id() const;
    int _get_first_selected_volume_id(int object_id) const;

    // Create 3D thick extrusion lines for a skirt and brim.
    // Adds a new Slic3r::GUI::3DScene::Volume to volumes.
    void _load_print_toolpaths();
    // Create 3D thick extrusion lines for object forming extrusions.
    // Adds a new Slic3r::GUI::3DScene::Volume to $self->volumes,
    // one for perimeters, one for infill and one for supports.
    void _load_print_object_toolpaths(const PrintObject& print_object, const std::vector<std::string>& str_tool_colors);
    // Create 3D thick extrusion lines for wipe tower extrusions
    void _load_wipe_tower_toolpaths(const std::vector<std::string>& str_tool_colors);

    // generates gcode extrusion paths geometry
    void _load_gcode_extrusion_paths(const GCodePreviewData& preview_data, const std::vector<float>& tool_colors);
    // generates gcode travel paths geometry
    void _load_gcode_travel_paths(const GCodePreviewData& preview_data, const std::vector<float>& tool_colors);
    bool _travel_paths_by_type(const GCodePreviewData& preview_data);
    bool _travel_paths_by_feedrate(const GCodePreviewData& preview_data);
    bool _travel_paths_by_tool(const GCodePreviewData& preview_data, const std::vector<float>& tool_colors);
    // generates gcode retractions geometry
    void _load_gcode_retractions(const GCodePreviewData& preview_data);
    // generates gcode unretractions geometry
    void _load_gcode_unretractions(const GCodePreviewData& preview_data);
    // generates objects and wipe tower geometry
    void _load_shells();
    // sets gcode geometry visibility according to user selection
    void _update_gcode_volumes_visibility(const GCodePreviewData& preview_data);
    void _update_toolpath_volumes_outside_state();
    void _show_warning_texture_if_needed();

    void _on_move(const std::vector<int>& volume_idxs);
    void _on_select(int volume_idx);

    // generates the legend texture in dependence of the current shown view type
    void _generate_legend_texture(const GCodePreviewData& preview_data, const std::vector<float>& tool_colors);

    // generates a warning texture containing the given message
    void _generate_warning_texture(const std::string& msg);
    void _reset_warning_texture();

    bool _is_any_volume_outside() const;

    static std::vector<float> _parse_colors(const std::vector<std::string>& colors);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvas3D_hpp_
