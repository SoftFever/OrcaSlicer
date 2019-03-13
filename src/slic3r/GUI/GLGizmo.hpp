#ifndef slic3r_GLGizmo_hpp_
#define slic3r_GLGizmo_hpp_

#include <igl/AABB.h>

#include "../../slic3r/GUI/GLTexture.hpp"
#include "../../slic3r/GUI/GLCanvas3D.hpp"

#include "libslic3r/Point.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/SLA/SLAAutoSupports.hpp"

#include <array>
#include <vector>
#include <memory>


class wxWindow;
class GLUquadric;
typedef class GLUquadric GLUquadricObj;


namespace Slic3r {

class BoundingBoxf3;
class Linef3;
class ModelObject;

namespace GUI {

class GLCanvas3D;
#if ENABLE_IMGUI
class ImGuiWrapper;
#endif // ENABLE_IMGUI

class GLGizmoBase
{
protected:
    struct Grabber
    {
        static const float SizeFactor;
        static const float MinHalfSize;
        static const float DraggingScaleFactor;

        Vec3d center;
        Vec3d angles;
        float color[3];
        bool enabled;
        bool dragging;

        Grabber();

        void render(bool hover, float size) const;
        void render_for_picking(float size) const { render(size, color, false); }

        float get_half_size(float size) const;
        float get_dragging_half_size(float size) const;

    private:
        void render(float size, const float* render_color, bool use_lighting) const;
        void render_face(float half_size) const;
    };

public:
    enum EState
    {
        Off,
        Hover,
        On,
        Num_States
    };

    struct UpdateData
    {
        const Linef3 mouse_ray;
        const Point* mouse_pos;
        bool shift_down;

        UpdateData(const Linef3& mouse_ray, const Point* mouse_pos = nullptr, bool shift_down = false)
            : mouse_ray(mouse_ray), mouse_pos(mouse_pos), shift_down(shift_down)
        {}
    };

protected:
    GLCanvas3D& m_parent;

    int m_group_id;
    EState m_state;
    int m_shortcut_key;
#if ENABLE_SVG_ICONS
    std::string m_icon_filename;
#endif // ENABLE_SVG_ICONS
    unsigned int m_sprite_id;
    int m_hover_id;
    bool m_dragging;
    float m_base_color[3];
    float m_drag_color[3];
    float m_highlight_color[3];
    mutable std::vector<Grabber> m_grabbers;
#if ENABLE_IMGUI
    ImGuiWrapper* m_imgui;
#endif // ENABLE_IMGUI

public:
#if ENABLE_SVG_ICONS
    GLGizmoBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
#else
    GLGizmoBase(GLCanvas3D& parent, unsigned int sprite_id);
#endif // ENABLE_SVG_ICONS
    virtual ~GLGizmoBase() {}

    bool init() { return on_init(); }

    std::string get_name() const { return on_get_name(); }

    int get_group_id() const { return m_group_id; }
    void set_group_id(int id) { m_group_id = id; }

    EState get_state() const { return m_state; }
    void set_state(EState state) { m_state = state; on_set_state(); }

    int get_shortcut_key() const { return m_shortcut_key; }
    void set_shortcut_key(int key) { m_shortcut_key = key; }

#if ENABLE_SVG_ICONS
    const std::string& get_icon_filename() const { return m_icon_filename; }
#endif // ENABLE_SVG_ICONS

    bool is_activable(const GLCanvas3D::Selection& selection) const { return on_is_activable(selection); }
    bool is_selectable() const { return on_is_selectable(); }

    unsigned int get_sprite_id() const { return m_sprite_id; }

    int get_hover_id() const { return m_hover_id; }
    void set_hover_id(int id);
    
    void set_highlight_color(const float* color);

    void enable_grabber(unsigned int id);
    void disable_grabber(unsigned int id);

    void start_dragging(const GLCanvas3D::Selection& selection);
    void stop_dragging();
    bool is_dragging() const { return m_dragging; }

    void update(const UpdateData& data, const GLCanvas3D::Selection& selection);

    void render(const GLCanvas3D::Selection& selection) const { on_render(selection); }
    void render_for_picking(const GLCanvas3D::Selection& selection) const { on_render_for_picking(selection); }

#if !ENABLE_IMGUI
    virtual void create_external_gizmo_widgets(wxWindow *parent);
#endif // not ENABLE_IMGUI

#if ENABLE_IMGUI
    void render_input_window(float x, float y, float bottom_limit, const GLCanvas3D::Selection& selection) { on_render_input_window(x, y, bottom_limit, selection); }
#endif // ENABLE_IMGUI

protected:
    virtual bool on_init() = 0;
    virtual std::string on_get_name() const = 0;
    virtual void on_set_state() {}
    virtual void on_set_hover_id() {}
    virtual bool on_is_activable(const GLCanvas3D::Selection& selection) const { return true; }
    virtual bool on_is_selectable() const { return true; }
    virtual void on_enable_grabber(unsigned int id) {}
    virtual void on_disable_grabber(unsigned int id) {}
    virtual void on_start_dragging(const GLCanvas3D::Selection& selection) {}
    virtual void on_stop_dragging() {}
    virtual void on_update(const UpdateData& data, const GLCanvas3D::Selection& selection) = 0;
    virtual void on_render(const GLCanvas3D::Selection& selection) const = 0;
    virtual void on_render_for_picking(const GLCanvas3D::Selection& selection) const = 0;

#if ENABLE_IMGUI
    virtual void on_render_input_window(float x, float y, float bottom_limit, const GLCanvas3D::Selection& selection) {}
#endif // ENABLE_IMGUI

    float picking_color_component(unsigned int id) const;
    void render_grabbers(const BoundingBoxf3& box) const;
    void render_grabbers(float size) const;
    void render_grabbers_for_picking(const BoundingBoxf3& box) const;

    void set_tooltip(const std::string& tooltip) const;
    std::string format(float value, unsigned int decimals) const;
};

class GLGizmoRotate : public GLGizmoBase
{
    static const float Offset;
    static const unsigned int CircleResolution;
    static const unsigned int AngleResolution;
    static const unsigned int ScaleStepsCount;
    static const float ScaleStepRad;
    static const unsigned int ScaleLongEvery;
    static const float ScaleLongTooth;
    static const unsigned int SnapRegionsCount;
    static const float GrabberOffset;

public:
    enum Axis : unsigned char
    {
        X,
        Y,
        Z
    };

private:
    Axis m_axis;
    double m_angle;

    GLUquadricObj* m_quadric;

    mutable Vec3d m_center;
    mutable float m_radius;

    mutable float m_snap_coarse_in_radius;
    mutable float m_snap_coarse_out_radius;
    mutable float m_snap_fine_in_radius;
    mutable float m_snap_fine_out_radius;

public:
    GLGizmoRotate(GLCanvas3D& parent, Axis axis);
    GLGizmoRotate(const GLGizmoRotate& other);
    virtual ~GLGizmoRotate();

    double get_angle() const { return m_angle; }
    void set_angle(double angle);

protected:
    virtual bool on_init();
    virtual std::string on_get_name() const { return ""; }
    virtual void on_start_dragging(const GLCanvas3D::Selection& selection);
    virtual void on_update(const UpdateData& data, const GLCanvas3D::Selection& selection);
    virtual void on_render(const GLCanvas3D::Selection& selection) const;
    virtual void on_render_for_picking(const GLCanvas3D::Selection& selection) const;

private:
    void render_circle() const;
    void render_scale() const;
    void render_snap_radii() const;
    void render_reference_radius() const;
    void render_angle() const;
    void render_grabber(const BoundingBoxf3& box) const;
    void render_grabber_extension(const BoundingBoxf3& box, bool picking) const;

    void transform_to_local(const GLCanvas3D::Selection& selection) const;
    // returns the intersection of the mouse ray with the plane perpendicular to the gizmo axis, in local coordinate
    Vec3d mouse_position_in_local_plane(const Linef3& mouse_ray, const GLCanvas3D::Selection& selection) const;
};

class GLGizmoRotate3D : public GLGizmoBase
{
    std::vector<GLGizmoRotate> m_gizmos;

public:
#if ENABLE_SVG_ICONS
    GLGizmoRotate3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
#else
    GLGizmoRotate3D(GLCanvas3D& parent, unsigned int sprite_id);
#endif // ENABLE_SVG_ICONS

    Vec3d get_rotation() const { return Vec3d(m_gizmos[X].get_angle(), m_gizmos[Y].get_angle(), m_gizmos[Z].get_angle()); }
    void set_rotation(const Vec3d& rotation) { m_gizmos[X].set_angle(rotation(0)); m_gizmos[Y].set_angle(rotation(1)); m_gizmos[Z].set_angle(rotation(2)); }

protected:
    virtual bool on_init();
    virtual std::string on_get_name() const;
    virtual void on_set_state()
    {
        for (GLGizmoRotate& g : m_gizmos)
        {
            g.set_state(m_state);
        }
    }
    virtual void on_set_hover_id()
    {
        for (unsigned int i = 0; i < 3; ++i)
        {
            m_gizmos[i].set_hover_id((m_hover_id == i) ? 0 : -1);
        }
    }
    virtual bool on_is_activable(const GLCanvas3D::Selection& selection) const { return !selection.is_wipe_tower(); }
    virtual void on_enable_grabber(unsigned int id)
    {
        if ((0 <= id) && (id < 3))
            m_gizmos[id].enable_grabber(0);
    }
    virtual void on_disable_grabber(unsigned int id)
    {
        if ((0 <= id) && (id < 3))
            m_gizmos[id].disable_grabber(0);
    }
    virtual void on_start_dragging(const GLCanvas3D::Selection& selection);
    virtual void on_stop_dragging();
    virtual void on_update(const UpdateData& data, const GLCanvas3D::Selection& selection)
    {
        for (GLGizmoRotate& g : m_gizmos)
        {
            g.update(data, selection);
        }
    }
    virtual void on_render(const GLCanvas3D::Selection& selection) const;
    virtual void on_render_for_picking(const GLCanvas3D::Selection& selection) const
    {
        for (const GLGizmoRotate& g : m_gizmos)
        {
            g.render_for_picking(selection);
        }
    }

#if ENABLE_IMGUI
    virtual void on_render_input_window(float x, float y, float bottom_limit, const GLCanvas3D::Selection& selection);
#endif // ENABLE_IMGUI
};

class GLGizmoScale3D : public GLGizmoBase
{
    static const float Offset;

    mutable BoundingBoxf3 m_box;

    Vec3d m_scale;

    double m_snap_step;

    Vec3d m_starting_scale;
    Vec3d m_starting_drag_position;
    BoundingBoxf3 m_starting_box;

public:
#if ENABLE_SVG_ICONS
    GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
#else
    GLGizmoScale3D(GLCanvas3D& parent, unsigned int sprite_id);
#endif // ENABLE_SVG_ICONS

    double get_snap_step(double step) const { return m_snap_step; }
    void set_snap_step(double step) { m_snap_step = step; }

    const Vec3d& get_scale() const { return m_scale; }
    void set_scale(const Vec3d& scale) { m_starting_scale = scale; m_scale = scale; }

protected:
    virtual bool on_init();
    virtual std::string on_get_name() const;
    virtual bool on_is_activable(const GLCanvas3D::Selection& selection) const { return !selection.is_wipe_tower(); }
    virtual void on_start_dragging(const GLCanvas3D::Selection& selection);
    virtual void on_update(const UpdateData& data, const GLCanvas3D::Selection& selection);
    virtual void on_render(const GLCanvas3D::Selection& selection) const;
    virtual void on_render_for_picking(const GLCanvas3D::Selection& selection) const;

#if ENABLE_IMGUI
    virtual void on_render_input_window(float x, float y, float bottom_limit, const GLCanvas3D::Selection& selection);
#endif // ENABLE_IMGUI

private:
    void render_grabbers_connection(unsigned int id_1, unsigned int id_2) const;

    void do_scale_x(const UpdateData& data);
    void do_scale_y(const UpdateData& data);
    void do_scale_z(const UpdateData& data);
    void do_scale_uniform(const UpdateData& data);

    double calc_ratio(const UpdateData& data) const;
};

class GLGizmoMove3D : public GLGizmoBase
{
    static const double Offset;

    Vec3d m_displacement;

    double m_snap_step;

    Vec3d m_starting_drag_position;
    Vec3d m_starting_box_center;
    Vec3d m_starting_box_bottom_center;

    GLUquadricObj* m_quadric;

public:
#if ENABLE_SVG_ICONS
    GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
#else
    GLGizmoMove3D(GLCanvas3D& parent, unsigned int sprite_id);
#endif // ENABLE_SVG_ICONS
    virtual ~GLGizmoMove3D();

    double get_snap_step(double step) const { return m_snap_step; }
    void set_snap_step(double step) { m_snap_step = step; }

    const Vec3d& get_displacement() const { return m_displacement; }

protected:
    virtual bool on_init();
    virtual std::string on_get_name() const;
    virtual void on_start_dragging(const GLCanvas3D::Selection& selection);
    virtual void on_stop_dragging();
    virtual void on_update(const UpdateData& data, const GLCanvas3D::Selection& selection);
    virtual void on_render(const GLCanvas3D::Selection& selection) const;
    virtual void on_render_for_picking(const GLCanvas3D::Selection& selection) const;

#if ENABLE_IMGUI
    virtual void on_render_input_window(float x, float y, float bottom_limit, const GLCanvas3D::Selection& selection);
#endif // ENABLE_IMGUI

private:
    double calc_projection(const UpdateData& data) const;
    void render_grabber_extension(Axis axis, const BoundingBoxf3& box, bool picking) const;
};

class GLGizmoFlatten : public GLGizmoBase
{
// This gizmo does not use grabbers. The m_hover_id relates to polygon managed by the class itself.

private:
    mutable Vec3d m_normal;

    struct PlaneData {
        std::vector<Vec3d> vertices;
        Vec3d normal;
        float area;
    };

    // This holds information to decide whether recalculation is necessary:
    std::vector<Transform3d> m_volumes_matrices;
    std::vector<ModelVolumeType> m_volumes_types;
    Vec3d m_first_instance_scale;
    Vec3d m_first_instance_mirror;

    std::vector<PlaneData> m_planes;
    bool m_planes_valid = false;
    mutable Vec3d m_starting_center;
    const ModelObject* m_model_object = nullptr;
    std::vector<const Transform3d*> instances_matrices;

    void update_planes();
    bool is_plane_update_necessary() const;

public:
#if ENABLE_SVG_ICONS
    GLGizmoFlatten(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
#else
    GLGizmoFlatten(GLCanvas3D& parent, unsigned int sprite_id);
#endif // ENABLE_SVG_ICONS

    void set_flattening_data(const ModelObject* model_object);
    Vec3d get_flattening_normal() const;

protected:
    virtual bool on_init();
    virtual std::string on_get_name() const;
    virtual bool on_is_activable(const GLCanvas3D::Selection& selection) const;
    virtual void on_start_dragging(const GLCanvas3D::Selection& selection);
    virtual void on_update(const UpdateData& data, const GLCanvas3D::Selection& selection) {}
    virtual void on_render(const GLCanvas3D::Selection& selection) const;
    virtual void on_render_for_picking(const GLCanvas3D::Selection& selection) const;
    virtual void on_set_state()
    {
        if (m_state == On && is_plane_update_necessary())
            update_planes();
    }
};

#define SLAGIZMO_IMGUI_MODAL 0

class GLGizmoSlaSupports : public GLGizmoBase
{
private:
    ModelObject* m_model_object = nullptr;
    ModelObject* m_old_model_object = nullptr;
    int m_active_instance = -1;
    int m_old_instance_id = -1;
    Vec3f unproject_on_mesh(const Vec2d& mouse_pos);

    const float RenderPointScale = 1.f;

    GLUquadricObj* m_quadric;
    Eigen::MatrixXf m_V; // vertices
    Eigen::MatrixXi m_F; // facets indices
    igl::AABB<Eigen::MatrixXf,3> m_AABB;

    struct SourceDataSummary {
        Geometry::Transformation transformation;
    };

    // This holds information to decide whether recalculation is necessary:
    SourceDataSummary m_source_data;

    mutable Vec3d m_starting_center;

public:
#if ENABLE_SVG_ICONS
    GLGizmoSlaSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
#else
    GLGizmoSlaSupports(GLCanvas3D& parent, unsigned int sprite_id);
#endif // ENABLE_SVG_ICONS
    virtual ~GLGizmoSlaSupports();
    void set_sla_support_data(ModelObject* model_object, const GLCanvas3D::Selection& selection);
    bool mouse_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down);
    void delete_selected_points(bool force = false);

private:
    bool on_init();
    void on_update(const UpdateData& data, const GLCanvas3D::Selection& selection);
    virtual void on_render(const GLCanvas3D::Selection& selection) const;
    virtual void on_render_for_picking(const GLCanvas3D::Selection& selection) const;

    void render_selection_rectangle() const;
    void render_points(const GLCanvas3D::Selection& selection, bool picking = false) const;
    bool is_mesh_update_necessary() const;
    void update_mesh();

#if !ENABLE_IMGUI
    void render_tooltip_texture() const;
    mutable GLTexture m_tooltip_texture;
    mutable GLTexture m_reset_texture;
#endif // not ENABLE_IMGUI

    bool m_lock_unique_islands = false;
    bool m_editing_mode = false;            // Is editing mode active?
    bool m_old_editing_state = false;       // To keep track of whether the user toggled between the modes (needed for imgui refreshes).
    float m_new_point_head_diameter;        // Size of a new point.
    float m_minimal_point_distance = 20.f;
    float m_density = 100.f;
    std::vector<std::pair<sla::SupportPoint, bool>> m_editing_mode_cache; // a support point and whether it is currently selected

    bool m_selection_rectangle_active = false;
    Vec2d m_selection_rectangle_start_corner;
    Vec2d m_selection_rectangle_end_corner;
    bool m_ignore_up_event = false;
    bool m_combo_box_open = false;  // To ensure proper rendering of the imgui combobox.
    bool m_unsaved_changes = false; // Are there unsaved changes in manual mode?
    bool m_selection_empty = true;
    EState m_old_state = Off; // to be able to see that the gizmo has just been closed (see on_set_state)
    int m_canvas_width;
    int m_canvas_height;

    std::vector<const ConfigOption*> get_config_options(const std::vector<std::string>& keys) const;

    // Methods that do the model_object and editing cache synchronization,
    // editing mode selection, etc:
    enum {
        AllPoints = -2,
        NoPoints,
    };
    void select_point(int i);
    void editing_mode_apply_changes();
    void editing_mode_discard_changes();
    void editing_mode_reload_cache();
    void get_data_from_backend();
    void auto_generate();
    void switch_to_editing_mode();

protected:
    void on_set_state() override;
    void on_start_dragging(const GLCanvas3D::Selection& selection) override;

#if ENABLE_IMGUI
    virtual void on_render_input_window(float x, float y, float bottom_limit, const GLCanvas3D::Selection& selection) override;
#endif // ENABLE_IMGUI

    virtual std::string on_get_name() const;
    virtual bool on_is_activable(const GLCanvas3D::Selection& selection) const;
    virtual bool on_is_selectable() const;
};


#if !ENABLE_IMGUI
class GLGizmoCutPanel;
#endif // not ENABLE_IMGUI

class GLGizmoCut : public GLGizmoBase
{
    static const double Offset;
    static const double Margin;
    static const std::array<float, 3> GrabberColor;

    mutable double m_cut_z;
    double m_start_z;
    mutable double m_max_z;
    Vec3d m_drag_pos;
    Vec3d m_drag_center;
    bool m_keep_upper;
    bool m_keep_lower;
    bool m_rotate_lower;
#if !ENABLE_IMGUI
    GLGizmoCutPanel *m_panel;
#endif // not ENABLE_IMGUI

public:
#if ENABLE_SVG_ICONS
    GLGizmoCut(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
#else
    GLGizmoCut(GLCanvas3D& parent, unsigned int sprite_id);
#endif // ENABLE_SVG_ICONS

#if !ENABLE_IMGUI
    virtual void create_external_gizmo_widgets(wxWindow *parent);
#endif // not ENABLE_IMGUI
#if !ENABLE_IMGUI
#endif // not ENABLE_IMGUI

protected:
    virtual bool on_init();
    virtual std::string on_get_name() const;
    virtual void on_set_state();
    virtual bool on_is_activable(const GLCanvas3D::Selection& selection) const;
    virtual void on_start_dragging(const GLCanvas3D::Selection& selection);
    virtual void on_update(const UpdateData& data, const GLCanvas3D::Selection& selection);
    virtual void on_render(const GLCanvas3D::Selection& selection) const;
    virtual void on_render_for_picking(const GLCanvas3D::Selection& selection) const;

#if ENABLE_IMGUI
    virtual void on_render_input_window(float x, float y, float bottom_limit, const GLCanvas3D::Selection& selection);
#endif // ENABLE_IMGUI
private:
    void update_max_z(const GLCanvas3D::Selection& selection) const;
    void set_cut_z(double cut_z) const;
    void perform_cut(const GLCanvas3D::Selection& selection);
    double calc_projection(const Linef3& mouse_ray) const;
};


} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmo_hpp_

