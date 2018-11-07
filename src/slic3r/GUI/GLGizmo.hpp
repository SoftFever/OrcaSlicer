#ifndef slic3r_GLGizmo_hpp_
#define slic3r_GLGizmo_hpp_

#include "../../slic3r/GUI/GLTexture.hpp"
#include "../../slic3r/GUI/GLCanvas3D.hpp"
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

        void render(bool hover, float size) const;
        void render_for_picking(float size) const { render(size, color, false); }

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

    std::string get_name() const { return on_get_name(); }

    int get_group_id() const { return m_group_id; }
    void set_group_id(int id) { m_group_id = id; }

    EState get_state() const { return m_state; }
    void set_state(EState state) {
        // FIXME: this is my workaround to react on the disabling event (Tamas)
        bool call_deactivate = ((m_state == On || m_state == Hover) &&
                                state == Off);

        m_state = state; on_set_state();

        if(call_deactivate) {
            on_deactivate();
        }
    }

    bool is_activable(const GLCanvas3D::Selection& selection) const { return on_is_activable(selection); }

    unsigned int get_texture_id() const { return m_textures[m_state].get_id(); }
    int get_textures_size() const { return m_textures[Off].get_width(); }

    int get_hover_id() const { return m_hover_id; }
    void set_hover_id(int id);
    
    void set_highlight_color(const float* color);

    void enable_grabber(unsigned int id);
    void disable_grabber(unsigned int id);

    void start_dragging(const GLCanvas3D::Selection& selection);
    void stop_dragging();
    bool is_dragging() const { return m_dragging; }

    void update(const Linef3& mouse_ray, const Point* mouse_pos);

#if ENABLE_GIZMOS_RESET
    void process_double_click() { on_process_double_click(); }
#endif // ENABLE_GIZMOS_RESET

    void render(const GLCanvas3D::Selection& selection) const { on_render(selection); }
    void render_for_picking(const GLCanvas3D::Selection& selection) const { on_render_for_picking(selection); }

protected:
    virtual bool on_init() = 0;
    virtual std::string on_get_name() const = 0;
    virtual void on_set_state() {}
    virtual void on_deactivate() {}     // FIXME: how to react to disabling the Gizmo? (Tamas)
    virtual void on_set_hover_id() {}
    virtual bool on_is_activable(const GLCanvas3D::Selection& selection) const { return true; }
    virtual void on_enable_grabber(unsigned int id) {}
    virtual void on_disable_grabber(unsigned int id) {}
    virtual void on_start_dragging(const GLCanvas3D::Selection& selection) {}
    virtual void on_stop_dragging() {}
    virtual void on_update(const Linef3& mouse_ray, const Point* mouse_pos) = 0;
#if ENABLE_GIZMOS_RESET
    virtual void on_process_double_click() {}
#endif // ENABLE_GIZMOS_RESET
    virtual void on_render(const GLCanvas3D::Selection& selection) const = 0;
    virtual void on_render_for_picking(const GLCanvas3D::Selection& selection) const = 0;

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
    virtual std::string on_get_name() const { return ""; }
    virtual void on_start_dragging(const GLCanvas3D::Selection& selection);
    virtual void on_update(const Linef3& mouse_ray, const Point* mouse_pos);
#if ENABLE_GIZMOS_RESET
    virtual void on_process_double_click() { m_angle = 0.0; }
#endif // ENABLE_GIZMOS_RESET
    virtual void on_render(const GLCanvas3D::Selection& selection) const;
    virtual void on_render_for_picking(const GLCanvas3D::Selection& selection) const;

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
    virtual void on_update(const Linef3& mouse_ray, const Point* mouse_pos)
    {
        for (GLGizmoRotate& g : m_gizmos)
        {
            g.update(mouse_ray, mouse_pos);
        }
    }
#if ENABLE_GIZMOS_RESET
    virtual void on_process_double_click()
    {
        if (m_hover_id != -1)
            m_gizmos[m_hover_id].process_double_click();
    }
#endif // ENABLE_GIZMOS_RESET
    virtual void on_render(const GLCanvas3D::Selection& selection) const;
    virtual void on_render_for_picking(const GLCanvas3D::Selection& selection) const
    {
        for (const GLGizmoRotate& g : m_gizmos)
        {
            g.render_for_picking(selection);
        }
    }
};

class GLGizmoScale3D : public GLGizmoBase
{
    static const float Offset;

    mutable BoundingBoxf3 m_box;

    Vec3d m_scale;

    Vec3d m_starting_scale;
    Vec3d m_starting_drag_position;
    BoundingBoxf3 m_starting_box;

public:
    explicit GLGizmoScale3D(GLCanvas3D& parent);

    const Vec3d& get_scale() const { return m_scale; }
    void set_scale(const Vec3d& scale) { m_starting_scale = scale; m_scale = scale; }

protected:
    virtual bool on_init();
    virtual std::string on_get_name() const;
    virtual bool on_is_activable(const GLCanvas3D::Selection& selection) const { return !selection.is_wipe_tower(); }
    virtual void on_start_dragging(const GLCanvas3D::Selection& selection);
    virtual void on_update(const Linef3& mouse_ray, const Point* mouse_pos);
#if ENABLE_GIZMOS_RESET
    virtual void on_process_double_click();
#endif // ENABLE_GIZMOS_RESET
    virtual void on_render(const GLCanvas3D::Selection& selection) const;
    virtual void on_render_for_picking(const GLCanvas3D::Selection& selection) const;

private:
    void render_grabbers_connection(unsigned int id_1, unsigned int id_2) const;

    void do_scale_x(const Linef3& mouse_ray);
    void do_scale_y(const Linef3& mouse_ray);
    void do_scale_z(const Linef3& mouse_ray);
    void do_scale_uniform(const Linef3& mouse_ray);

    double calc_ratio(const Linef3& mouse_ray) const;
};

class GLGizmoMove3D : public GLGizmoBase
{
    static const double Offset;

    Vec3d m_displacement;
    Vec3d m_starting_drag_position;
    Vec3d m_starting_box_center;
    Vec3d m_starting_box_bottom_center;

public:
    explicit GLGizmoMove3D(GLCanvas3D& parent);

    const Vec3d& get_displacement() const { return m_displacement; }

protected:
    virtual bool on_init();
    virtual std::string on_get_name() const;
    virtual void on_start_dragging(const GLCanvas3D::Selection& selection);
    virtual void on_stop_dragging();
    virtual void on_update(const Linef3& mouse_ray, const Point* mouse_pos);
    virtual void on_render(const GLCanvas3D::Selection& selection) const;
    virtual void on_render_for_picking(const GLCanvas3D::Selection& selection) const;

private:
    double calc_projection(const Linef3& mouse_ray) const;
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
        Vec3d mesh_first_point;
    };

    // This holds information to decide whether recalculation is necessary:
    SourceDataSummary m_source_data;

    std::vector<PlaneData> m_planes;
    mutable Vec3d m_starting_center;
    const ModelObject* m_model_object = nullptr;
    std::vector<const Transform3d*> instances_matrices;

    void update_planes();
    bool is_plane_update_necessary() const;

public:
    explicit GLGizmoFlatten(GLCanvas3D& parent);

    void set_flattening_data(const ModelObject* model_object);
    Vec3d get_flattening_normal() const;

protected:
    virtual bool on_init();
    virtual std::string on_get_name() const;
    virtual bool on_is_activable(const GLCanvas3D::Selection& selection) const { return (selection.is_from_single_object() && !selection.is_wipe_tower() && !selection.is_modifier());  }
    virtual void on_start_dragging(const GLCanvas3D::Selection& selection);
    virtual void on_update(const Linef3& mouse_ray, const Point* mouse_pos) {}
    virtual void on_render(const GLCanvas3D::Selection& selection) const;
    virtual void on_render_for_picking(const GLCanvas3D::Selection& selection) const;
    virtual void on_set_state()
    {
        if (m_state == On && is_plane_update_necessary())
            update_planes();
    }
};

class GLGizmoSlaSupports : public GLGizmoBase
{
private:
    ModelObject* m_model_object = nullptr;
    Transform3d m_model_object_matrix;
    Vec3f unproject_on_mesh(const Vec2d& mouse_pos);

    Eigen::MatrixXf m_V; // vertices
    Eigen::MatrixXi m_F; // facets indices
    struct SourceDataSummary {
        BoundingBoxf3 bounding_box;
        Transform3d matrix;
        Vec3d mesh_first_point;
    };

    // This holds information to decide whether recalculation is necessary:
    SourceDataSummary m_source_data;

    mutable Vec3d m_starting_center;

public:
    explicit GLGizmoSlaSupports(GLCanvas3D& parent);
    void set_model_object_ptr(ModelObject* model_object);
    void clicked_on_object(const Vec2d& mouse_position);
    void delete_current_grabber(bool delete_all);

private:
    bool on_init();
    void on_update(const Linef3& mouse_ray, const Point* mouse_pos);
    virtual void on_render(const GLCanvas3D::Selection& selection) const;
    virtual void on_render_for_picking(const GLCanvas3D::Selection& selection) const;

    void render_grabbers(bool picking = false) const;
    void render_tooltip_texture() const;
    bool is_mesh_update_necessary() const;
    void update_mesh();

    mutable GLTexture m_tooltip_texture;
    mutable GLTexture m_reset_texture;

protected:
    void on_set_state() override {
        if (m_state == On && is_mesh_update_necessary()) {
            update_mesh();
        }
    }

    void on_deactivate() override;

    std::string on_get_name() const override;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmo_hpp_

