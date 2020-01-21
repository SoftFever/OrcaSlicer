#ifndef slic3r_GLGizmoScale_hpp_
#define slic3r_GLGizmoScale_hpp_

#include "GLGizmoBase.hpp"


namespace Slic3r {
namespace GUI {

class GLGizmoScale3D : public GLGizmoBase
{
    static const float Offset;

    struct StartingData
    {
        Vec3d scale;
        Vec3d drag_position;
        BoundingBoxf3 box;
        Vec3d pivots[6];
        bool ctrl_down;

        StartingData() : scale(Vec3d::Ones()), drag_position(Vec3d::Zero()), ctrl_down(false) { for (int i = 0; i < 5; ++i) { pivots[i] = Vec3d::Zero(); } }
    };

    mutable BoundingBoxf3 m_box;
    mutable Transform3d m_transform;
    // Transforms grabbers offsets to the proper reference system (world for instances, instance for volumes)
    mutable Transform3d m_offsets_transform;
    Vec3d m_scale;
    Vec3d m_offset;
    double m_snap_step;
    StartingData m_starting;

public:
    GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    double get_snap_step(double step) const { return m_snap_step; }
    void set_snap_step(double step) { m_snap_step = step; }

    const Vec3d& get_scale() const { return m_scale; }
    void set_scale(const Vec3d& scale) { m_starting.scale = scale; m_scale = scale; }

    const Vec3d& get_offset() const { return m_offset; }

protected:
    virtual bool on_init();
    virtual std::string on_get_name() const;
    virtual bool on_is_activable() const;
    virtual void on_start_dragging();
    virtual void on_update(const UpdateData& data);
    virtual void on_render() const;
    virtual void on_render_for_picking() const;

private:
    void render_grabbers_connection(unsigned int id_1, unsigned int id_2) const;

    void do_scale_along_axis(Axis axis, const UpdateData& data);
    void do_scale_uniform(const UpdateData& data);

    double calc_ratio(const UpdateData& data) const;
};


} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoScale_hpp_
