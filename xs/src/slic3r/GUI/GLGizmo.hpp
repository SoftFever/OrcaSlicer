#ifndef slic3r_GLGizmo_hpp_
#define slic3r_GLGizmo_hpp_

#include "../../slic3r/GUI/GLTexture.hpp"
#include "../../libslic3r/Point.hpp"
#include "../../libslic3r/BoundingBox.hpp"

#include <vector>

namespace Slic3r {

class BoundingBoxf3;
class Linef3;
class ModelObject;

namespace GUI {

class GLCanvas3D;

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

        void render(bool hover, const BoundingBoxf3& box) const;
        void render_for_picking(const BoundingBoxf3& box) const { render(box, color, false); }

    private:
        void render(const BoundingBoxf3& box, const float* render_color, bool use_lighting) const;
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

protected:
    GLCanvas3D& m_parent;

    int m_group_id;
    EState m_state;
    // textures are assumed to be square and all with the same size in pixels, no internal check is done
    GLTexture m_textures[Num_States];
    int m_hover_id;
    bool m_dragging;
    float m_base_color[3];
    float m_drag_color[3];
    float m_highlight_color[3];
    mutable std::vector<Grabber> m_grabbers;

public:
    explicit GLGizmoBase(GLCanvas3D& parent);
    virtual ~GLGizmoBase() {}

    bool init() { return on_init(); }

    int get_group_id() const { return m_group_id; }
    void set_group_id(int id) { m_group_id = id; }

    EState get_state() const { return m_state; }
    void set_state(EState state) { m_state = state; on_set_state(); }

    unsigned int get_texture_id() const { return m_textures[m_state].get_id(); }
    int get_textures_size() const { return m_textures[Off].get_width(); }

    int get_hover_id() const { return m_hover_id; }
    void set_hover_id(int id);
    
    void set_highlight_color(const float* color);

    void enable_grabber(unsigned int id);
    void disable_grabber(unsigned int id);

    void start_dragging(const BoundingBoxf3& box);
    void stop_dragging();
    bool is_dragging() const { return m_dragging; }

    void update(const Linef3& mouse_ray);

#if ENABLE_GIZMOS_RESET
    void process_double_click() { on_process_double_click(); }
#endif // ENABLE_GIZMOS_RESET

    void render(const BoundingBoxf3& box) const { on_render(box); }
    void render_for_picking(const BoundingBoxf3& box) const { on_render_for_picking(box); }

protected:
    virtual bool on_init() = 0;
    virtual void on_set_state() {}
    virtual void on_set_hover_id() {}
    virtual void on_enable_grabber(unsigned int id) {}
    virtual void on_disable_grabber(unsigned int id) {}
    virtual void on_start_dragging(const BoundingBoxf3& box) {}
    virtual void on_stop_dragging() {}
    virtual void on_update(const Linef3& mouse_ray) = 0;
#if ENABLE_GIZMOS_RESET
    virtual void on_process_double_click() {}
#endif // ENABLE_GIZMOS_RESET
    virtual void on_render(const BoundingBoxf3& box) const = 0;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const = 0;

    float picking_color_component(unsigned int id) const;
    void render_grabbers(const BoundingBoxf3& box) const;
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

    mutable Vec3d m_center;
    mutable float m_radius;

    mutable float m_snap_coarse_in_radius;
    mutable float m_snap_coarse_out_radius;
    mutable float m_snap_fine_in_radius;
    mutable float m_snap_fine_out_radius;

public:
    GLGizmoRotate(GLCanvas3D& parent, Axis axis);

    double get_angle() const { return m_angle; }
    void set_angle(double angle);

protected:
    virtual bool on_init();
    virtual void on_start_dragging(const BoundingBoxf3& box);
    virtual void on_update(const Linef3& mouse_ray);
#if ENABLE_GIZMOS_RESET
    virtual void on_process_double_click() { m_angle = 0.0; }
#endif // ENABLE_GIZMOS_RESET
    virtual void on_render(const BoundingBoxf3& box) const;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const;

private:
    void render_circle() const;
    void render_scale() const;
    void render_snap_radii() const;
    void render_reference_radius() const;
    void render_angle() const;
    void render_grabber(const BoundingBoxf3& box) const;

    void transform_to_local() const;
    // returns the intersection of the mouse ray with the plane perpendicular to the gizmo axis, in local coordinate
    Vec3d mouse_position_in_local_plane(const Linef3& mouse_ray) const;
};

class GLGizmoRotate3D : public GLGizmoBase
{
    std::vector<GLGizmoRotate> m_gizmos;

public:
    explicit GLGizmoRotate3D(GLCanvas3D& parent);

#if ENABLE_MODELINSTANCE_3D_ROTATION
    Vec3d get_rotation() const { return Vec3d(m_gizmos[X].get_angle(), m_gizmos[Y].get_angle(), m_gizmos[Z].get_angle()); }
    void set_rotation(const Vec3d& rotation) { m_gizmos[X].set_angle(rotation(0)); m_gizmos[Y].set_angle(rotation(1)); m_gizmos[Z].set_angle(rotation(2)); }
#else
    double get_angle_x() const { return m_gizmos[X].get_angle(); }
    void set_angle_x(double angle) { m_gizmos[X].set_angle(angle); }

    double get_angle_y() const { return m_gizmos[Y].get_angle(); }
    void set_angle_y(double angle) { m_gizmos[Y].set_angle(angle); }

    double get_angle_z() const { return m_gizmos[Z].get_angle(); }
    void set_angle_z(double angle) { m_gizmos[Z].set_angle(angle); }
#endif // ENABLE_MODELINSTANCE_3D_ROTATION

protected:
    virtual bool on_init();
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
    virtual void on_start_dragging(const BoundingBoxf3& box);
    virtual void on_stop_dragging();
    virtual void on_update(const Linef3& mouse_ray)
    {
        for (GLGizmoRotate& g : m_gizmos)
        {
            g.update(mouse_ray);
        }
    }
#if ENABLE_GIZMOS_RESET
    virtual void on_process_double_click()
    {
        if (m_hover_id != -1)
            m_gizmos[m_hover_id].process_double_click();
    }
#endif // ENABLE_GIZMOS_RESET
    virtual void on_render(const BoundingBoxf3& box) const;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const
    {
        for (const GLGizmoRotate& g : m_gizmos)
        {
            g.render_for_picking(box);
        }
    }
};

class GLGizmoScale3D : public GLGizmoBase
{
    static const float Offset;
    static const Vec3d OffsetVec;

    mutable BoundingBoxf3 m_box;

    Vec3d m_scale;

    Vec3d m_starting_scale;
    Vec3d m_starting_drag_position;
    bool m_show_starting_box;
    BoundingBoxf3 m_starting_box;

public:
    explicit GLGizmoScale3D(GLCanvas3D& parent);

#if ENABLE_MODELINSTANCE_3D_SCALE
    const Vec3d& get_scale() const { return m_scale; }
    void set_scale(const Vec3d& scale) { m_starting_scale = scale; }
#else
    double get_scale_x() const { return m_scale(0); }
    void set_scale_x(double scale) { m_starting_scale(0) = scale; }

    double get_scale_y() const { return m_scale(1); }
    void set_scale_y(double scale) { m_starting_scale(1) = scale; }

    double get_scale_z() const { return m_scale(2); }
    void set_scale_z(double scale) { m_starting_scale(2) = scale; }

    void set_scale(double scale) { m_starting_scale = scale * Vec3d::Ones(); }
#endif // ENABLE_MODELINSTANCE_3D_SCALE

protected:
    virtual bool on_init();
    virtual void on_start_dragging(const BoundingBoxf3& box);
    virtual void on_stop_dragging() { m_show_starting_box = false; }
    virtual void on_update(const Linef3& mouse_ray);
#if ENABLE_GIZMOS_RESET
    virtual void on_process_double_click();
#endif // ENABLE_GIZMOS_RESET
    virtual void on_render(const BoundingBoxf3& box) const;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const;

private:
    void render_box(const BoundingBoxf3& box) const;
    void render_grabbers_connection(unsigned int id_1, unsigned int id_2) const;

    void do_scale_x(const Linef3& mouse_ray);
    void do_scale_y(const Linef3& mouse_ray);
    void do_scale_z(const Linef3& mouse_ray);
    void do_scale_uniform(const Linef3& mouse_ray);

    double calc_ratio(unsigned int preferred_plane_id, const Linef3& mouse_ray, const Vec3d& center) const;
};

class GLGizmoMove3D : public GLGizmoBase
{
    static const double Offset;

    Vec3d m_position;
    Vec3d m_starting_drag_position;
    Vec3d m_starting_box_center;
    Vec3d m_starting_box_bottom_center;

public:
    explicit GLGizmoMove3D(GLCanvas3D& parent);

    const Vec3d& get_position() const { return m_position; }
    void set_position(const Vec3d& position) { m_position = position; }

protected:
    virtual bool on_init();
    virtual void on_start_dragging(const BoundingBoxf3& box);
    virtual void on_update(const Linef3& mouse_ray);
    virtual void on_render(const BoundingBoxf3& box) const;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const;

private:
    double calc_projection(Axis axis, unsigned int preferred_plane_id, const Linef3& mouse_ray) const;
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
    struct SourceDataSummary {
        std::vector<BoundingBoxf3> bounding_boxes; // bounding boxes of convex hulls of individual volumes
#if !ENABLE_MODELINSTANCE_3D_ROTATION
        float scaling_factor;
        float rotation;
#endif // !ENABLE_MODELINSTANCE_3D_ROTATION
        Vec3d mesh_first_point;
    };

    // This holds information to decide whether recalculation is necessary:
    SourceDataSummary m_source_data;

    std::vector<PlaneData> m_planes;
#if ENABLE_MODELINSTANCE_3D_OFFSET
#if ENABLE_MODELINSTANCE_3D_ROTATION
    struct InstanceData
    {
        Vec3d position;
        Vec3d rotation;
#if ENABLE_MODELINSTANCE_3D_SCALE
        Vec3d scaling_factor;

        InstanceData(const Vec3d& position, const Vec3d& rotation, const Vec3d& scaling_factor) : position(position), rotation(rotation), scaling_factor(scaling_factor) {}
#else
        double scaling_factor;

        InstanceData(const Vec3d& position, const Vec3d& rotation, double scaling_factor) : position(position), rotation(rotation), scaling_factor(scaling_factor) {}
#endif // ENABLE_MODELINSTANCE_3D_SCALE
    };
    std::vector<InstanceData> m_instances;
#else
    Pointf3s m_instances_positions;
#endif // ENABLE_MODELINSTANCE_3D_ROTATION
#else
    std::vector<Vec2d> m_instances_positions;
#endif // ENABLE_MODELINSTANCE_3D_OFFSET
    Vec3d m_starting_center;
    const ModelObject* m_model_object = nullptr;

    void update_planes();
    bool is_plane_update_necessary() const;

public:
    explicit GLGizmoFlatten(GLCanvas3D& parent);

    void set_flattening_data(const ModelObject* model_object);
#if ENABLE_MODELINSTANCE_3D_ROTATION
    Vec3d get_flattening_rotation() const;
#else
    Vec3d get_flattening_normal() const;
#endif // ENABLE_MODELINSTANCE_3D_ROTATION

protected:
    virtual bool on_init();
    virtual void on_start_dragging(const BoundingBoxf3& box);
    virtual void on_update(const Linef3& mouse_ray) {}
    virtual void on_render(const BoundingBoxf3& box) const;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const;
    virtual void on_set_state()
    {
        if (m_state == On && is_plane_update_necessary())
            update_planes();
    }
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmo_hpp_

