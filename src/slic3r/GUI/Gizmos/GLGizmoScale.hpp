#ifndef slic3r_GLGizmoScale_hpp_
#define slic3r_GLGizmoScale_hpp_

#include "GLGizmoBase.hpp"


namespace Slic3r {
namespace GUI {

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
    virtual bool on_is_activable(const Selection& selection) const { return !selection.is_wipe_tower(); }
    virtual void on_start_dragging(const Selection& selection);
    virtual void on_update(const UpdateData& data, const Selection& selection);
    virtual void on_render(const Selection& selection) const;
    virtual void on_render_for_picking(const Selection& selection) const;
    virtual void on_render_input_window(float x, float y, float bottom_limit, const Selection& selection);

private:
    void render_grabbers_connection(unsigned int id_1, unsigned int id_2) const;

    void do_scale_x(const UpdateData& data);
    void do_scale_y(const UpdateData& data);
    void do_scale_z(const UpdateData& data);
    void do_scale_uniform(const UpdateData& data);

    double calc_ratio(const UpdateData& data) const;
};


} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoScale_hpp_
