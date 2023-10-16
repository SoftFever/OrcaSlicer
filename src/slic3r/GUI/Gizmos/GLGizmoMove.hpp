#ifndef slic3r_GLGizmoMove_hpp_
#define slic3r_GLGizmoMove_hpp_

#include "GLGizmoBase.hpp"
//BBS: add size adjust related
#include "GizmoObjectManipulation.hpp"


namespace Slic3r {
namespace GUI {

//BBS: GUI refactor: add object manipulation
class GizmoObjectManipulation;
class GLGizmoMove3D : public GLGizmoBase
{
    static const double Offset;

    Vec3d m_displacement;
    Vec3d m_center{ Vec3d::Zero() };
    BoundingBoxf3 m_bounding_box;
    double m_snap_step;

    Vec3d m_starting_drag_position;
    Vec3d m_starting_box_center;
    Vec3d m_starting_box_bottom_center;

    GLModel m_vbo_cone;

    //BBS: add size adjust related
    GizmoObjectManipulation* m_object_manipulation;
    struct GrabberConnection
    {
        GLModel model;
        Vec3d   old_center{Vec3d::Zero()};
    };
    std::array<GrabberConnection, 3> m_grabber_connections;

public:
    //BBS: add obj manipulation logic
    //GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation);
    virtual ~GLGizmoMove3D() = default;

    double get_snap_step(double step) const { return m_snap_step; }
    void set_snap_step(double step) { m_snap_step = step; }

    const Vec3d& get_displacement() const { return m_displacement; }

    std::string get_tooltip() const override;

protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    virtual bool on_is_activable() const override;
    virtual void on_start_dragging() override;
    virtual void on_stop_dragging() override;
    virtual void on_update(const UpdateData& data) override;
    virtual void on_render() override;
    //BBS: GUI refactor: add object manipulation
    virtual void on_render_input_window(float x, float y, float bottom_limit);

private:
    double calc_projection(const UpdateData& data) const;
};



} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoMove_hpp_
