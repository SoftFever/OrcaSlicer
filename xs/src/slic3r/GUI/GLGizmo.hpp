#ifndef slic3r_GLGizmo_hpp_
#define slic3r_GLGizmo_hpp_

#include "../../slic3r/GUI/GLTexture.hpp"
#include "../../libslic3r/Point.hpp"

#include <vector>

#define ENABLE_GIZMOS_3D 1

namespace Slic3r {

class BoundingBoxf3;
class Pointf3;
class Linef3;

namespace GUI {

class GLGizmoBase
{
protected:
    struct Grabber
    {
        static const float HalfSize;
        static const float DraggingScaleFactor;

        Pointf3 center;
        float angle_x;
        float angle_y;
        float angle_z;
        float color[3];
        bool dragging;

        Grabber();

        void render(bool hover) const;
        void render_for_picking() const;

    private:
        void render(const float* render_color) const;
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
    int m_group_id;
    EState m_state;
    // textures are assumed to be square and all with the same size in pixels, no internal check is done
    GLTexture m_textures[Num_States];
    int m_hover_id;
    float m_base_color[3];
    float m_drag_color[3];
    float m_highlight_color[3];
    mutable std::vector<Grabber> m_grabbers;
    bool m_is_container;

public:
    GLGizmoBase();
    virtual ~GLGizmoBase() {}

    bool init() { return on_init(); }

    int get_group_id() const { return m_group_id; }
    void set_group_id(int id) { m_group_id = id; }

    EState get_state() const { return m_state; }
    void set_state(EState state) { m_state = state; on_set_state(); }

    unsigned int get_texture_id() const;
    int get_textures_size() const;

    int get_hover_id() const { return m_hover_id; }
    void set_hover_id(int id);

    void set_highlight_color(const float* color);

    void start_dragging();
    void stop_dragging();
    void update(const Linef3& mouse_ray);
    void refresh();

    void render(const BoundingBoxf3& box) const;
    void render_for_picking(const BoundingBoxf3& box) const;

protected:
    virtual bool on_init() = 0;
    virtual void on_set_state() {}
    virtual void on_set_hover_id() {}
    virtual void on_start_dragging() {}
    virtual void on_stop_dragging() {}
    virtual void on_update(const Linef3& mouse_ray) = 0;
    virtual void on_refresh() {}
    virtual void on_render(const BoundingBoxf3& box) const = 0;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const = 0;

    float picking_color_component(unsigned int id) const;
    void render_grabbers() const;
    void render_grabbers_for_picking() const;
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
    static const float ScaleShortTooth;
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
    float m_angle;

    mutable Pointf3 m_center;
    mutable float m_radius;
    mutable bool m_keep_initial_values;

public:
    explicit GLGizmoRotate(Axis axis);

    float get_angle() const;
    void set_angle(float angle);

protected:
    virtual bool on_init();
    virtual void on_set_state();
    virtual void on_update(const Linef3& mouse_ray);
    virtual void on_refresh();
    virtual void on_render(const BoundingBoxf3& box) const;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const;

private:
    void render_circle() const;
    void render_scale() const;
    void render_snap_radii() const;
    void render_reference_radius() const;
    void render_angle() const;
    void render_grabber() const;

    void transform_to_local() const;
    Pointf mouse_position_in_local_plane(const Linef3& mouse_ray) const;
};

class GLGizmoRotate3D : public GLGizmoBase
{
    GLGizmoRotate m_x;
    GLGizmoRotate m_y;
    GLGizmoRotate m_z;

public:
    GLGizmoRotate3D();

    float get_angle_x() const;
    void set_angle_x(float angle);

    float get_angle_y() const;
    void set_angle_y(float angle);

    float get_angle_z() const;
    void set_angle_z(float angle);

protected:
    virtual bool on_init();
    virtual void on_set_state();
    virtual void on_set_hover_id();
    virtual void on_start_dragging();
    virtual void on_stop_dragging();
    virtual void on_update(const Linef3& mouse_ray);
    virtual void on_refresh();
    virtual void on_render(const BoundingBoxf3& box) const;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const;
};

class GLGizmoScale : public GLGizmoBase
{
    static const float Offset;

    float m_scale;
    float m_starting_scale;

    Pointf m_starting_drag_position;

public:
    GLGizmoScale();

    float get_scale() const;
    void set_scale(float scale);

protected:
    virtual bool on_init();
    virtual void on_start_dragging();
    virtual void on_update(const Linef3& mouse_ray);
    virtual void on_render(const BoundingBoxf3& box) const;
    virtual void on_render_for_picking(const BoundingBoxf3& box) const;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmo_hpp_

