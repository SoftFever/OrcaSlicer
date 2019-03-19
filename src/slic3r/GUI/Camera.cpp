#include "libslic3r/libslic3r.h"

#include "Camera.hpp"

static const float GIMBALL_LOCK_THETA_MAX = 180.0f;

namespace Slic3r {
namespace GUI {

Camera::Camera()
    : type(Ortho)
    , zoom(1.0f)
    , phi(45.0f)
//    , distance(0.0f)
    , requires_zoom_to_bed(false)
    , m_theta(45.0f)
    , m_target(Vec3d::Zero())
{
}

std::string Camera::get_type_as_string() const
{
    switch (type)
    {
    default:
    case Unknown:
        return "unknown";
//    case Perspective:
//        return "perspective";
    case Ortho:
        return "ortho";
    };
}

void Camera::set_target(const Vec3d& target)
{
    m_target = target;
    m_target(0) = clamp(m_scene_box.min(0), m_scene_box.max(0), m_target(0));
    m_target(1) = clamp(m_scene_box.min(1), m_scene_box.max(1), m_target(1));
    m_target(2) = clamp(m_scene_box.min(2), m_scene_box.max(2), m_target(2));
}

void Camera::set_theta(float theta, bool apply_limit)
{
    if (apply_limit)
        m_theta = clamp(0.0f, GIMBALL_LOCK_THETA_MAX, theta);
    else
    {
        m_theta = fmod(theta, 360.0f);
        if (m_theta < 0.0f)
            m_theta += 360.0f;
    }
}

void Camera::set_scene_box(const BoundingBoxf3& box)
{
    m_scene_box = box;
}

} // GUI
} // Slic3r

