#ifndef slic3r_Camera_hpp_
#define slic3r_Camera_hpp_

#include "libslic3r/BoundingBox.hpp"

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

private:
    Vec3d m_target;
    float m_theta;

    BoundingBoxf3 m_scene_box;

public:
    Camera();

    std::string get_type_as_string() const;

    const Vec3d& get_target() const { return m_target; }
    void set_target(const Vec3d& target);

    float get_theta() const { return m_theta; }
    void set_theta(float theta, bool apply_limit);

    const BoundingBoxf3& get_scene_box() const { return m_scene_box; }
    void set_scene_box(const BoundingBoxf3& box);
};

} // GUI
} // Slic3r

#endif // slic3r_Camera_hpp_

