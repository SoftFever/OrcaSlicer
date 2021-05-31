#ifndef slic3r_GLGizmoRotate_hpp_
#define slic3r_GLGizmoRotate_hpp_

#include "GLGizmoBase.hpp"
#include "../Jobs/RotoptimizeJob.hpp"


namespace Slic3r {
namespace GUI {

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
    GLGizmoRotate(const GLGizmoRotate& other);
    virtual ~GLGizmoRotate() = default;

    double get_angle() const { return m_angle; }
    void set_angle(double angle);

    std::string get_tooltip() const override;

protected:
    bool on_init() override;
    std::string on_get_name() const override { return ""; }
    void on_start_dragging() override;
    void on_update(const UpdateData& data) override;
    void on_render() const override;
    void on_render_for_picking() const override;

private:
    void render_circle() const;
    void render_scale() const;
    void render_snap_radii() const;
    void render_reference_radius() const;
    void render_angle() const;
    void render_grabber(const BoundingBoxf3& box) const;
    void render_grabber_extension(const BoundingBoxf3& box, bool picking) const;

    void transform_to_local(const Selection& selection) const;
    // returns the intersection of the mouse ray with the plane perpendicular to the gizmo axis, in local coordinate
    Vec3d mouse_position_in_local_plane(const Linef3& mouse_ray, const Selection& selection) const;
};

class GLGizmoRotate3D : public GLGizmoBase
{
    std::vector<GLGizmoRotate> m_gizmos;

public:
    GLGizmoRotate3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    Vec3d get_rotation() const { return Vec3d(m_gizmos[X].get_angle(), m_gizmos[Y].get_angle(), m_gizmos[Z].get_angle()); }
    void set_rotation(const Vec3d& rotation) { m_gizmos[X].set_angle(rotation(0)); m_gizmos[Y].set_angle(rotation(1)); m_gizmos[Z].set_angle(rotation(2)); }

    std::string get_tooltip() const override
    {
        std::string tooltip = m_gizmos[X].get_tooltip();
        if (tooltip.empty())
            tooltip = m_gizmos[Y].get_tooltip();
        if (tooltip.empty())
            tooltip = m_gizmos[Z].get_tooltip();
        return tooltip;
    }

protected:
    bool on_init() override;
    std::string on_get_name() const override;
    void on_set_state() override
    {
        for (GLGizmoRotate& g : m_gizmos)
            g.set_state(m_state);
    }
    void on_set_hover_id() override
    {
        for (int i = 0; i < 3; ++i)
            m_gizmos[i].set_hover_id((m_hover_id == i) ? 0 : -1);
    }
    void on_enable_grabber(unsigned int id) override
    {
        if (id < 3)
            m_gizmos[id].enable_grabber(0);
    }
    void on_disable_grabber(unsigned int id) override
    {
        if (id < 3)
            m_gizmos[id].disable_grabber(0);
    }
    bool on_is_activable() const override;
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_update(const UpdateData& data) override
    {
        for (GLGizmoRotate& g : m_gizmos) {
            g.update(data);
        }
    }
    void on_render() const override;
    void on_render_for_picking() const override
    {
        for (const GLGizmoRotate& g : m_gizmos) {
            g.render_for_picking();
        }
    }

    void on_render_input_window(float x, float y, float bottom_limit) override;

private:

    class RotoptimzeWindow {
        ImGuiWrapper *m_imgui = nullptr;

        static const char * options [];
        static bool options_valid;

    public:

        struct State {
            float  accuracy  = 1.f;
            int    method_id = 0;
        };

        struct Alignment { float x, y, bottom_limit; };

        RotoptimzeWindow(ImGuiWrapper *   imgui,
                         State &          state,
                         const Alignment &bottom_limit);

        ~RotoptimzeWindow();

        RotoptimzeWindow(const RotoptimzeWindow&) = delete;
        RotoptimzeWindow(RotoptimzeWindow &&) = delete;
        RotoptimzeWindow& operator=(const RotoptimzeWindow &) = delete;
        RotoptimzeWindow& operator=(RotoptimzeWindow &&) = delete;
    };

    RotoptimzeWindow::State m_rotoptimizewin_state = {};

    void load_rotoptimize_state();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoRotate_hpp_
