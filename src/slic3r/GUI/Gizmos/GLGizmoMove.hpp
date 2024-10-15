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
    Vec3d         origin = Vec3d::Zero();
    Vec3d         m_center{Vec3d::Zero()};
    BoundingBoxf3 m_bounding_box;
    Transform3d   m_orient_matrix{Transform3d::Identity()};
    double m_snap_step;

    Vec3d m_starting_drag_position;
    Vec3d m_starting_box_center;
    Vec3d m_starting_box_bottom_center;

    GLModel m_vbo_cone;

    //BBS: add size adjust related
    GizmoObjectManipulation* m_object_manipulation;

public:
    //BBS: add obj manipulation logic
    //GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation);
    virtual ~GLGizmoMove3D() = default;

    double get_snap_step(double step) const { return m_snap_step; }
    void set_snap_step(double step) { m_snap_step = step; }

    const Vec3d& get_displacement() const { return m_displacement; }

    std::string get_tooltip() const override;
    void        data_changed(bool is_serializing) override;

protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    std::string on_get_name_str() override { return "Move"; }
    virtual bool on_is_activable() const override;
    virtual void on_set_state() override;
    virtual void on_start_dragging() override;
    virtual void on_stop_dragging() override;
    virtual void on_update(const UpdateData& data) override;
    virtual void on_render() override;
    virtual void on_render_for_picking() override;
    //BBS: GUI refactor: add object manipulation
    virtual void on_render_input_window(float x, float y, float bottom_limit);

private:
    double calc_projection(const UpdateData& data) const;
    void render_grabber_extension(Axis axis, const BoundingBoxf3& box, bool picking) const;
    void   change_cs_by_selection(); //cs mean Coordinate System
private:
    int m_last_selected_obejct_idx, m_last_selected_volume_idx;
};



} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoMove_hpp_
