#ifndef slic3r_GLGizmoCut_hpp_
#define slic3r_GLGizmoCut_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLModel.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/ObjectID.hpp"

namespace Slic3r {
namespace GUI {

class GLGizmoCut : public GLGizmoBase
{
    static const double Offset;
    static const double Margin;
    static const std::array<float, 4> GrabberColor;

    double m_cut_z{ 0.0 };
    double m_max_z{ 0.0 };
    double m_start_z{ 0.0 };
    Vec3d m_drag_pos;
    Vec3d m_drag_center;
    bool m_keep_upper{ true };
    bool m_keep_lower{ true };
    bool m_rotate_lower{ false };
    // BBS: m_do_segment
    bool m_cut_to_parts {false};
    bool m_do_segment{ false };
    double m_segment_smoothing_alpha{ 0.5 };
    int m_segment_number{ 5 };

    struct CutContours
    {
        TriangleMesh mesh;
        GLModel contours;
        double cut_z{ 0.0 };
        Vec3d position{ Vec3d::Zero() };
        Vec3d shift{ Vec3d::Zero() };
        ObjectID object_id;
        int instance_idx{ -1 };
    };

    CutContours m_cut_contours;

public:
    GLGizmoCut(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    double get_cut_z() const { return m_cut_z; }
    void set_cut_z(double cut_z);

    std::string get_tooltip() const override;

protected:
    virtual bool on_init() override;
    virtual void on_load(cereal::BinaryInputArchive& ar)  override { ar(m_cut_z, m_keep_upper, m_keep_lower, m_rotate_lower); }
    virtual void on_save(cereal::BinaryOutputArchive& ar) const override { ar(m_cut_z, m_keep_upper, m_keep_lower, m_rotate_lower); }
    virtual std::string on_get_name() const override;
    virtual void on_set_state() override;
    virtual bool on_is_activable() const override;
    virtual void on_start_dragging() override;
    virtual void on_update(const UpdateData& data) override;
    virtual void on_render() override;
    virtual void on_render_for_picking() override;
    virtual void on_render_input_window(float x, float y, float bottom_limit) override;

private:
    void perform_cut(const Selection& selection);
    double calc_projection(const Linef3& mouse_ray) const;
    BoundingBoxf3 bounding_box() const;
    void update_contours();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoCut_hpp_
