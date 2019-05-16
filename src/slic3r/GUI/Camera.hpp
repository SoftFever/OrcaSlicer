#ifndef slic3r_Camera_hpp_
#define slic3r_Camera_hpp_

#include "libslic3r/BoundingBox.hpp"
#include <array>

namespace Slic3r {
namespace GUI {

struct Camera
{
    enum EType : unsigned char
    {
        Unknown,
//        Perspective,
        Ortho,
        Num_types
    };

    EType type;
    float zoom;
    float phi;
//    float distance;
    bool requires_zoom_to_bed;
    bool inverted_phi;

private:
    Vec3d m_target;
    float m_theta;

    mutable std::array<int, 4> m_viewport;
    mutable Transform3d m_view_matrix;
    mutable Transform3d m_projection_matrix;

    BoundingBoxf3 m_scene_box;

public:
    Camera();

    std::string get_type_as_string() const;

    const Vec3d& get_target() const { return m_target; }
    void set_target(const Vec3d& target);

    float get_theta() const { return m_theta; }
    void set_theta(float theta, bool apply_limit);

    const BoundingBoxf3& get_scene_box() const { return m_scene_box; }
    void set_scene_box(const BoundingBoxf3& box){ m_scene_box = box; }

    bool select_view(const std::string& direction);

    const std::array<int, 4>& get_viewport() const { return m_viewport; }
    const Transform3d& get_view_matrix() const { return m_view_matrix; }
    const Transform3d& get_projection_matrix() const { return m_projection_matrix; }

    Vec3d get_dir_right() const { return m_view_matrix.matrix().block(0, 0, 3, 3).row(0); }
    Vec3d get_dir_up() const { return m_view_matrix.matrix().block(0, 0, 3, 3).row(1); }
    Vec3d get_dir_forward() const { return m_view_matrix.matrix().block(0, 0, 3, 3).row(2); }

    Vec3d get_position() const { return m_view_matrix.matrix().block(0, 3, 3, 1); }

    void apply_viewport(int x, int y, unsigned int w, unsigned int h) const;
    void apply_view_matrix() const;
    void apply_projection(const BoundingBoxf3& box) const;

private:
    void apply_ortho_projection(double x_min, double x_max, double y_min, double y_max, double z_min, double z_max) const;
};

} // GUI
} // Slic3r

#endif // slic3r_Camera_hpp_

