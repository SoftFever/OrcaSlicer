#ifndef slic3r_GLGizmoMove_hpp_
#define slic3r_GLGizmoMove_hpp_

#include "GLGizmoBase.hpp"


namespace Slic3r {
namespace GUI {

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
    GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    virtual ~GLGizmoMove3D();

    double get_snap_step(double step) const { return m_snap_step; }
    void set_snap_step(double step) { m_snap_step = step; }

    const Vec3d& get_displacement() const { return m_displacement; }

protected:
    virtual bool on_init();
    virtual std::string on_get_name() const;
    virtual void on_start_dragging();
    virtual void on_stop_dragging();
    virtual void on_update(const UpdateData& data);
    virtual void on_render() const;
    virtual void on_render_for_picking() const;
#if !DISABLE_MOVE_ROTATE_SCALE_GIZMOS_IMGUI
    virtual void on_render_input_window(float x, float y, float bottom_limit);
#endif // !DISABLE_MOVE_ROTATE_SCALE_GIZMOS_IMGUI

private:
    double calc_projection(const UpdateData& data) const;
    void render_grabber_extension(Axis axis, const BoundingBoxf3& box, bool picking) const;
};



} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoMove_hpp_
