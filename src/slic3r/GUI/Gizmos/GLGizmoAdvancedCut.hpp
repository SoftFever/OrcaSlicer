#ifndef slic3r_GLGizmoAdvancedCut_hpp_
#define slic3r_GLGizmoAdvancedCut_hpp_

#include "GLGizmoBase.hpp"
#include "GLGizmoRotate.hpp"

namespace Slic3r {
namespace GUI {

class GLGizmoAdvancedCut : public GLGizmoRotate3D
{
struct Rotate_data {
    double  angle;
    Axis    ax;

    Rotate_data(double an, Axis a)
        : angle(an), ax(a)
    {
    }
};
private:
    static const double Offset;
    static const double Margin;
    static const std::array<float, 4> GrabberColor;
    static const std::array<float, 4> GrabberHoverColor;

    mutable double m_movement;
    mutable double m_height;  // height of cut plane to heatbed
    mutable double m_height_delta;  // height of cut plane to heatbed
    double m_start_movement;
    double m_start_height;

    Vec3d m_rotation;
    //Vec3d m_current_base_rotation;
    std::vector<Rotate_data> m_rotate_cmds;

    Vec3d m_buffered_rotation;
    double m_buffered_movement;
    double m_buffered_height;

    Vec3d m_drag_pos;

    bool m_keep_upper;
    bool m_keep_lower;
    bool m_cut_to_parts;
    bool m_rotate_lower;
    bool m_do_segment;
    double m_segment_smoothing_alpha;
    int m_segment_number;

    std::array<Vec3d, 4> m_cut_plane_points;

    mutable Grabber m_move_grabber;

    unsigned int m_last_active_id;

public:
    GLGizmoAdvancedCut(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    double get_movement() const { return m_movement; }
    void set_movement(double movement) const;
    void finish_rotation();
    std::string get_tooltip() const override;

protected:
    virtual bool on_init();
    virtual std::string on_get_name() const;
    virtual void on_set_state();
    virtual bool on_is_activable() const;
    virtual void on_start_dragging();
    virtual void on_update(const UpdateData& data);
    virtual void on_render();
    virtual void on_render_for_picking();
    virtual void on_render_input_window(float x, float y, float bottom_limit);

    virtual void on_enable_grabber(unsigned int id)
    {
        if (id < 3)
            m_gizmos[id].enable_grabber(0);
        else if (id == 3)
            this->enable_grabber(0);
    }

    virtual void on_disable_grabber(unsigned int id)
    {
        if (id < 3)
            m_gizmos[id].disable_grabber(0);
        else if (id == 3)
            this->disable_grabber(0);
    }

    virtual void on_set_hover_id()
    {
        for (int i = 0; i < 3; ++i)
            m_gizmos[i].set_hover_id((m_hover_id == i) ? 0 : -1);
    }

private:
    void perform_cut(const Selection& selection);
    double calc_projection(const Linef3& mouse_ray) const;
    Vec3d calc_plane_normal(const std::array<Vec3d, 4>& plane_points) const;
    Vec3d calc_plane_center(const std::array<Vec3d, 4>& plane_points) const;
    Vec3d get_plane_normal() const;
    Vec3d get_plane_center() const;
    void update_plane_points();
    std::array<Vec3d, 4> get_plane_points() const;
    std::array<Vec3d, 4> get_plane_points_world_coord() const;
    void reset_cut_plane();
    void reset_all();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoAdvancedCut_hpp_