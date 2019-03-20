#ifndef slic3r_GLCanvas3D_hpp_
#define slic3r_GLCanvas3D_hpp_

#include <stddef.h>
#include <memory>

#include "3DScene.hpp"
#include "GLToolbar.hpp"
#include "Event.hpp"
#include "3DBed.hpp"
#include "Camera.hpp"
#include "Selection.hpp"

#include <float.h>

#include <wx/timer.h>

class wxWindow;
class wxSizeEvent;
class wxIdleEvent;
class wxKeyEvent;
class wxMouseEvent;
class wxTimerEvent;
class wxPaintEvent;
class wxGLCanvas;

// Support for Retina OpenGL on Mac OS
#define ENABLE_RETINA_GL __APPLE__

namespace Slic3r {

class GLShader;
class ExPolygon;
class BackgroundSlicingProcess;
class GCodePreviewData;
struct SlicingParameters;
enum LayerHeightEditActionType : unsigned int;

namespace GUI {

class GLGizmoBase;

#if ENABLE_RETINA_GL
class RetinaHelper;
#endif

class Size
{
    int m_width;
    int m_height;
    float m_scale_factor;

public:
    Size();
    Size(int width, int height, float scale_factor = 1.0);

    int get_width() const;
    void set_width(int width);

    int get_height() const;
    void set_height(int height);

    int get_scale_factor() const;
    void set_scale_factor(int height);
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

    float get_width() const { return m_right - m_left; }
    float get_height() const { return m_top - m_bottom; }
};

wxDECLARE_EVENT(EVT_GLCANVAS_OBJECT_SELECT, SimpleEvent);

using Vec2dEvent = Event<Vec2d>;
template <size_t N> using Vec2dsEvent = ArrayEvent<Vec2d, N>;

using Vec3dEvent = Event<Vec3d>;
template <size_t N> using Vec3dsEvent = ArrayEvent<Vec3d, N>;

wxDECLARE_EVENT(EVT_GLCANVAS_INIT, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_RIGHT_CLICK, Vec2dEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_REMOVE_OBJECT, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_ARRANGE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_SELECT_ALL, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_QUESTION_MARK, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_INCREASE_INSTANCES, Event<int>); // data: +1 => increase, -1 => decrease
wxDECLARE_EVENT(EVT_GLCANVAS_INSTANCE_MOVED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_WIPETOWER_MOVED, Vec3dEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_INSTANCE_ROTATED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_INSTANCE_SCALED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, Event<bool>);
wxDECLARE_EVENT(EVT_GLCANVAS_UPDATE_GEOMETRY, Vec3dsEvent<2>);
wxDECLARE_EVENT(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_UPDATE_BED_SHAPE, SimpleEvent);
wxDECLARE_EVENT(EVT_GLCANVAS_TAB, SimpleEvent);

// this describes events being passed from GLCanvas3D to SlaSupport gizmo
enum class SLAGizmoEventType {
    LeftDown = 1,
    LeftUp,
    RightDown,
    Dragging,
    Delete,
    SelectAll,
    ShiftUp,
    ApplyChanges,
    DiscardChanges,
    AutomaticGeneration,
    ManualEditing
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

#if !ENABLE_TEXTURES_FROM_SVG
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
#endif // !ENABLE_TEXTURES_FROM_SVG

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
        static const float THICKNESS_BAR_WIDTH;
        static const float THICKNESS_RESET_BUTTON_HEIGHT;

        bool                        m_use_legacy_opengl;
        bool                        m_enabled;
        Shader                      m_shader;
        unsigned int                m_z_texture_id;
        mutable GLTexture           m_tooltip_texture;
        mutable GLTexture           m_reset_texture;
        // Not owned by LayersEditing.
        const DynamicPrintConfig   *m_config;
        // ModelObject for the currently selected object (Model::objects[last_object_id]).
        const ModelObject          *m_model_object;
        // Maximum z of the currently selected object (Model::objects[last_object_id]).
        float                       m_object_max_z;
        // Owned by LayersEditing.
        SlicingParameters          *m_slicing_parameters;
        std::vector<coordf_t>       m_layer_height_profile;
        bool                        m_layer_height_profile_modified;

        class LayersTexture
        {
        public:
            LayersTexture() : width(0), height(0), levels(0), cells(0), valid(false) {}

            // Texture data
            std::vector<char>   data;
            // Width of the texture, top level.
            size_t              width;
            // Height of the texture, top level.
            size_t              height;
            // For how many levels of detail is the data allocated?
            size_t              levels;
            // Number of texture cells allocated for the height texture.
            size_t              cells;
            // Does it need to be refreshed?
            bool                valid;
        };
        LayersTexture   m_layers_texture;

    public:
        EState state;
        float band_width;
        float strength;
        int last_object_id;
        float last_z;
        LayerHeightEditActionType last_action;

        LayersEditing();
        ~LayersEditing();

        bool init(const std::string& vertex_shader_filename, const std::string& fragment_shader_filename);
		void set_config(const DynamicPrintConfig* config);
        void select_object(const Model &model, int object_id);

        bool is_allowed() const;
        void set_use_legacy_opengl(bool use_legacy_opengl);

        bool is_enabled() const;
        void set_enabled(bool enabled);

        void render_overlay(const GLCanvas3D& canvas) const;
        void render_volumes(const GLCanvas3D& canvas, const GLVolumeCollection& volumes) const;

		void adjust_layer_height_profile();
		void accept_changes(GLCanvas3D& canvas);
        void reset_layer_height_profile(GLCanvas3D& canvas);

        static float get_cursor_z_relative(const GLCanvas3D& canvas);
        static bool bar_rect_contains(const GLCanvas3D& canvas, float x, float y);
        static bool reset_rect_contains(const GLCanvas3D& canvas, float x, float y);
        static Rect get_bar_rect_screen(const GLCanvas3D& canvas);
        static Rect get_reset_rect_screen(const GLCanvas3D& canvas);
        static Rect get_bar_rect_viewport(const GLCanvas3D& canvas);
        static Rect get_reset_rect_viewport(const GLCanvas3D& canvas);

        float object_max_z() const { return m_object_max_z; }

    private:
        bool _is_initialized() const;
        void generate_layer_height_texture();
        void _render_tooltip_texture(const GLCanvas3D& canvas, const Rect& bar_rect, const Rect& reset_rect) const;
        void _render_reset_texture(const Rect& reset_rect) const;
        void _render_active_object_annotations(const GLCanvas3D& canvas, const Rect& bar_rect) const;
        void _render_profile(const Rect& bar_rect) const;
        void update_slicing_parameters();

        static float thickness_bar_width(const GLCanvas3D &canvas);
        static float reset_button_height(const GLCanvas3D &canvas);
    };

    struct Mouse
    {
        struct Drag
        {
            static const Point Invalid_2D_Point;
            static const Vec3d Invalid_3D_Point;
            static const int MoveThresholdPx;

            Point start_position_2D;
            Vec3d start_position_3D;
            int move_volume_idx;
            bool move_requires_threshold;
            Point move_start_threshold_position_2D;

        public:
            Drag();
        };

        bool dragging;
        Vec2d position;
        Vec3d scene_position;
        Drag drag;
        bool ignore_up_event;

        Mouse();

        void set_start_position_2D_as_invalid() { drag.start_position_2D = Drag::Invalid_2D_Point; }
        void set_start_position_3D_as_invalid() { drag.start_position_3D = Drag::Invalid_3D_Point; }
        void set_move_start_threshold_position_2D_as_invalid() { drag.move_start_threshold_position_2D = Drag::Invalid_2D_Point; }

        bool is_start_position_2D_defined() const { return (drag.start_position_2D != Drag::Invalid_2D_Point); }
        bool is_start_position_3D_defined() const { return (drag.start_position_3D != Drag::Invalid_3D_Point); }
        bool is_move_start_threshold_position_2D_defined() const { return (drag.move_start_threshold_position_2D != Drag::Invalid_2D_Point); }
        bool is_move_threshold_met(const Point& mouse_pos) const {
            return (std::abs(mouse_pos(0) - drag.move_start_threshold_position_2D(0)) > Drag::MoveThresholdPx)
                || (std::abs(mouse_pos(1) - drag.move_start_threshold_position_2D(1)) > Drag::MoveThresholdPx);
        }
    };

public:
    class ClippingPlane
    {
        double m_data[4];

    public:
        ClippingPlane()
        {
            m_data[0] = 0.0;
            m_data[1] = 0.0;
            m_data[2] = 1.0;
            m_data[3] = 0.0;
        }

        ClippingPlane(const Vec3d& direction, double offset)
        {
            Vec3d norm_dir = direction.normalized();
            m_data[0] = norm_dir(0);
            m_data[1] = norm_dir(1);
            m_data[2] = norm_dir(2);
            m_data[3] = offset;
        }

        const double* get_data() const { return m_data; }
    };

private:
    class Gizmos
    {
    public:
#if ENABLE_SVG_ICONS
        static const float Default_Icons_Size;
#endif // ENABLE_SVG_ICONS

        enum EType : unsigned char
        {
            Undefined,
            Move,
            Scale,
            Rotate,
            Flatten,
            Cut,
            SlaSupports,
            Num_Types
        };

    private:
        bool m_enabled;
        typedef std::map<EType, GLGizmoBase*> GizmosMap;
        GizmosMap m_gizmos;
#if ENABLE_SVG_ICONS
        mutable GLTexture m_icons_texture;
        mutable bool m_icons_texture_dirty;
#else
        ItemsIconsTexture m_icons_texture;
#endif // ENABLE_SVG_ICONS
        BackgroundTexture m_background_texture;
        EType m_current;

#if ENABLE_SVG_ICONS
        float m_overlay_icons_size;
        float m_overlay_scale;
#else
        float m_overlay_icons_scale;
#endif // ENABLE_SVG_ICONS
        float m_overlay_border;
        float m_overlay_gap_y;

    public:
        Gizmos();
        ~Gizmos();

        bool init(GLCanvas3D& parent);

        bool is_enabled() const;
        void set_enabled(bool enable);

#if ENABLE_SVG_ICONS
        void set_overlay_icon_size(float size);
#endif // ENABLE_SVG_ICONS
        void set_overlay_scale(float scale);

        std::string update_hover_state(const GLCanvas3D& canvas, const Vec2d& mouse_pos, const Selection& selection);
        void update_on_off_state(const GLCanvas3D& canvas, const Vec2d& mouse_pos, const Selection& selection);
        void update_on_off_state(const Selection& selection);
        void reset_all_states();

        void set_hover_id(int id);
        void enable_grabber(EType type, unsigned int id, bool enable);

        bool overlay_contains_mouse(const GLCanvas3D& canvas, const Vec2d& mouse_pos) const;
        bool grabber_contains_mouse() const;
        void update(const Linef3& mouse_ray, const Selection& selection, bool shift_down, const Point* mouse_pos = nullptr);
        Rect get_reset_rect_viewport(const GLCanvas3D& canvas) const;
        EType get_current_type() const;

        bool is_running() const;
        bool handle_shortcut(int key, const Selection& selection);

        bool is_dragging() const;
        void start_dragging(const Selection& selection);
        void stop_dragging();

        Vec3d get_displacement() const;

        Vec3d get_scale() const;
        void set_scale(const Vec3d& scale);

        Vec3d get_rotation() const;
        void set_rotation(const Vec3d& rotation);

        Vec3d get_flattening_normal() const;

        void set_flattening_data(const ModelObject* model_object);

        void set_sla_support_data(ModelObject* model_object, const Selection& selection);
        bool mouse_event(SLAGizmoEventType action, const Vec2d& mouse_position = Vec2d::Zero(), bool shift_down = false);
        void delete_current_grabber(bool delete_all = false);

        void render_current_gizmo(const Selection& selection) const;
        void render_current_gizmo_for_picking_pass(const Selection& selection) const;

        void render_overlay(const GLCanvas3D& canvas, const Selection& selection) const;

    private:
        void reset();

        void do_render_overlay(const GLCanvas3D& canvas, const Selection& selection) const;
        void do_render_current_gizmo(const Selection& selection) const;

        float get_total_overlay_height() const;
        float get_total_overlay_width() const;

        GLGizmoBase* get_current() const;

#if ENABLE_SVG_ICONS
        bool generate_icons_texture() const;
#endif // ENABLE_SVG_ICONS
    };

    struct SlaCap
    {
        struct Triangles
        {
            Pointf3s object;
            Pointf3s supports;
        };
        typedef std::map<unsigned int, Triangles> ObjectIdToTrianglesMap;
        double z;
        ObjectIdToTrianglesMap triangles;

        SlaCap() { reset(); }
        void reset() { z = DBL_MAX; triangles.clear(); }
        bool matches(double z) const { return this->z == z; }
    };

    class WarningTexture : public GUI::GLTexture
    {
    public:
        WarningTexture();

        enum Warning {
            ObjectOutside,
            ToolpathOutside,
            SomethingNotShown,
            ObjectClashed
        };

        // Sets a warning of the given type to be active/inactive. If several warnings are active simultaneously,
        // only the last one is shown (decided by the order in the enum above).
        void activate(WarningTexture::Warning warning, bool state, const GLCanvas3D& canvas);
        void render(const GLCanvas3D& canvas) const;

    private:
        static const unsigned char Background_Color[3];
        static const unsigned char Opacity;

        int m_original_width;
        int m_original_height;

        // Information about which warnings are currently active.
        std::vector<Warning> m_warnings;

        // Generates the texture with given text.
        bool _generate(const std::string& msg, const GLCanvas3D& canvas, const bool red_colored = false);
    };

    class LegendTexture : public GUI::GLTexture
    {
        static const int Px_Title_Offset = 5;
        static const int Px_Text_Offset = 5;
        static const int Px_Square = 20;
        static const int Px_Square_Contour = 1;
        static const int Px_Border = Px_Square / 2;
        static const unsigned char Squares_Border_Color[3];
        static const unsigned char Default_Background_Color[3];
        static const unsigned char Error_Background_Color[3];
        static const unsigned char Opacity;

        int m_original_width;
        int m_original_height;

    public:
        LegendTexture();
        void fill_color_print_legend_values(const GCodePreviewData& preview_data, const GLCanvas3D& canvas,
                                     std::vector<std::pair<double, double>>& cp_legend_values);

        bool generate(const GCodePreviewData& preview_data, const std::vector<float>& tool_colors, const GLCanvas3D& canvas);

        void render(const GLCanvas3D& canvas) const;
    };

    wxGLCanvas* m_canvas;
    wxGLContext* m_context;
#if ENABLE_RETINA_GL
    std::unique_ptr<RetinaHelper> m_retina_helper;
#endif
    bool m_in_render;
    LegendTexture m_legend_texture;
    WarningTexture m_warning_texture;
    wxTimer m_timer;
    Bed3D& m_bed;
    Camera& m_camera;
    GLToolbar& m_view_toolbar;
    LayersEditing m_layers_editing;
    Shader m_shader;
    Mouse m_mouse;
    mutable Gizmos m_gizmos;
    mutable GLToolbar m_toolbar;
    ClippingPlane m_clipping_planes[2];
    bool m_use_clipping_planes;
    mutable SlaCap m_sla_caps[2];
    std::string m_sidebar_field;

    mutable GLVolumeCollection m_volumes;
    Selection m_selection;
    const DynamicPrintConfig* m_config;
    Model* m_model;
    BackgroundSlicingProcess *m_process;

    // Screen is only refreshed from the OnIdle handler if it is dirty.
    bool m_dirty;
    bool m_initialized;
    bool m_use_VBOs;
    bool m_apply_zoom_to_volumes_filter;
    mutable int m_hover_volume_id;
    bool m_warning_texture_enabled;
    bool m_legend_texture_enabled;
    bool m_picking_enabled;
    bool m_moving_enabled;
    bool m_dynamic_background_enabled;
    bool m_multisample_allowed;
    bool m_regenerate_volumes;
    bool m_moving;
    bool m_tab_down;
    bool m_render_sla_auxiliaries;

    std::string m_color_by;

    bool m_reload_delayed;

    GCodePreviewVolumeIndex m_gcode_preview_volume_index;

public:
    GLCanvas3D(wxGLCanvas* canvas, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar);
    ~GLCanvas3D();

    void set_context(wxGLContext* context) { m_context = context; }

    wxGLCanvas* get_wxglcanvas() { return m_canvas; }
	const wxGLCanvas* get_wxglcanvas() const { return m_canvas; }

    bool init(bool useVBOs, bool use_legacy_opengl);
    void post_event(wxEvent &&event);

    void set_as_dirty();

    unsigned int get_volumes_count() const;
    void reset_volumes();
    int check_volumes_outside_state() const;

    void toggle_sla_auxiliaries_visibility(bool visible);
    void toggle_model_objects_visibility(bool visible, const ModelObject* mo = nullptr, int instance_idx = -1);

    void set_config(const DynamicPrintConfig* config);
    void set_process(BackgroundSlicingProcess* process);
    void set_model(Model* model);

    const Selection& get_selection() const { return m_selection; }
    Selection& get_selection() { return m_selection; }

    void bed_shape_changed();

    void set_clipping_plane(unsigned int id, const ClippingPlane& plane)
    {
        if (id < 2)
        {
            m_clipping_planes[id] = plane;
            m_sla_caps[id].reset();
        }
    }
    void set_use_clipping_planes(bool use) { m_use_clipping_planes = use; }

    void set_color_by(const std::string& value);

    float get_camera_zoom() const;

    BoundingBoxf3 volumes_bounding_box() const;
    BoundingBoxf3 scene_bounding_box() const;

    bool is_layers_editing_enabled() const;
    bool is_layers_editing_allowed() const;

    bool is_reload_delayed() const;

    void enable_layers_editing(bool enable);
    void enable_legend_texture(bool enable);
    void enable_picking(bool enable);
    void enable_moving(bool enable);
    void enable_gizmos(bool enable);
    void enable_toolbar(bool enable);
    void enable_dynamic_background(bool enable);
    void allow_multisample(bool allow);

    void zoom_to_bed();
    void zoom_to_volumes();
    void zoom_to_selection();
    void select_view(const std::string& direction);

    void update_volumes_colors_by_extruder();

    bool is_dragging() const { return m_gizmos.is_dragging() || m_moving; }

    void render();

    void select_all();
    void delete_selected();
    void ensure_on_bed(unsigned int object_idx);

    std::vector<double> get_current_print_zs(bool active_only) const;
    void set_toolpaths_range(double low, double high);

    std::vector<int> load_object(const ModelObject& model_object, int obj_idx, std::vector<int> instance_idxs);
    std::vector<int> load_object(const Model& model, int obj_idx);

    void mirror_selection(Axis axis);

    void reload_scene(bool refresh_immediately, bool force_full_scene_refresh = false);

    void load_gcode_preview(const GCodePreviewData& preview_data, const std::vector<std::string>& str_tool_colors);
    void load_sla_preview();
    void load_preview(const std::vector<std::string>& str_tool_colors, const std::vector<double>& color_print_values);
    void bind_event_handlers();
    void unbind_event_handlers();

    void on_size(wxSizeEvent& evt);
    void on_idle(wxIdleEvent& evt);
    void on_char(wxKeyEvent& evt);
    void on_key(wxKeyEvent& evt);
    void on_mouse_wheel(wxMouseEvent& evt);
    void on_timer(wxTimerEvent& evt);
    void on_mouse(wxMouseEvent& evt);
    void on_paint(wxPaintEvent& evt);

    Size get_canvas_size() const;
    Point get_local_mouse_position() const;

    void reset_legend_texture();

    void set_tooltip(const std::string& tooltip) const;

    void do_move();
    void do_rotate();
    void do_scale();
    void do_flatten();
    void do_mirror();

    void set_camera_zoom(float zoom);

    void update_gizmos_on_off_state();

    void handle_sidebar_focus_event(const std::string& opt_key, bool focus_on);

    void update_ui_from_settings();

private:
    bool _is_shown_on_screen() const;

    bool _init_toolbar();

    bool _set_current();
    void _resize(unsigned int w, unsigned int h);

    BoundingBoxf3 _max_bounding_box() const;

    void _zoom_to_bounding_box(const BoundingBoxf3& bbox);
    float _get_zoom_to_bounding_box_factor(const BoundingBoxf3& bbox) const;

    void _refresh_if_shown_on_screen();

    void _camera_tranform() const;
    void _picking_pass() const;
    void _render_background() const;
    void _render_bed(float theta) const;
    void _render_axes() const;
    void _render_objects() const;
    void _render_selection() const;
#if ENABLE_RENDER_SELECTION_CENTER
    void _render_selection_center() const;
#endif // ENABLE_RENDER_SELECTION_CENTER
    void _render_warning_texture() const;
    void _render_legend_texture() const;
    void _render_volumes(bool fake_colors) const;
    void _render_current_gizmo() const;
    void _render_gizmos_overlay() const;
    void _render_toolbar() const;
    void _render_view_toolbar() const;
#if ENABLE_SHOW_CAMERA_TARGET
    void _render_camera_target() const;
#endif // ENABLE_SHOW_CAMERA_TARGET
    void _render_sla_slices() const;
    void _render_selection_sidebar_hints() const;

    void _update_volumes_hover_state() const;
    void _update_gizmos_data();

    void _perform_layer_editing_action(wxMouseEvent* evt = nullptr);

    // Convert the screen space coordinate to an object space coordinate.
    // If the Z screen space coordinate is not provided, a depth buffer value is substituted.
    Vec3d _mouse_to_3d(const Point& mouse_pos, float* z = nullptr);

    // Convert the screen space coordinate to world coordinate on the bed.
    Vec3d _mouse_to_bed_3d(const Point& mouse_pos);

    // Returns the view ray line, in world coordinate, at the given mouse position.
    Linef3 mouse_ray(const Point& mouse_pos);

    void _start_timer();
    void _stop_timer();

    // Create 3D thick extrusion lines for a skirt and brim.
    // Adds a new Slic3r::GUI::3DScene::Volume to volumes.
    void _load_print_toolpaths();
    // Create 3D thick extrusion lines for object forming extrusions.
    // Adds a new Slic3r::GUI::3DScene::Volume to $self->volumes,
    // one for perimeters, one for infill and one for supports.
    void _load_print_object_toolpaths(const PrintObject& print_object, const std::vector<std::string>& str_tool_colors,
                                      const std::vector<double>& color_print_values);
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
    void _load_shells_fff();
    // generates objects geometry for sla
    void _load_shells_sla();
    // sets gcode geometry visibility according to user selection
    void _update_gcode_volumes_visibility(const GCodePreviewData& preview_data);
    void _update_toolpath_volumes_outside_state();
    void _show_warning_texture_if_needed();

    // generates the legend texture in dependence of the current shown view type
    void _generate_legend_texture(const GCodePreviewData& preview_data, const std::vector<float>& tool_colors);

    // generates a warning texture containing the given message
    void _set_warning_texture(WarningTexture::Warning warning, bool state);

    bool _is_any_volume_outside() const;

#if !ENABLE_SVG_ICONS
    void _resize_toolbars() const;
#endif // !ENABLE_SVG_ICONS

    static std::vector<float> _parse_colors(const std::vector<std::string>& colors);

public:
    const Print* fff_print() const;
    const SLAPrint* sla_print() const;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLCanvas3D_hpp_
