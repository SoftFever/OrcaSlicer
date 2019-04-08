#include "libslic3r/libslic3r.h"

#include "Camera.hpp"
#include "3DScene.hpp"

#include <GL/glew.h>

static const float GIMBALL_LOCK_THETA_MAX = 180.0f;

// phi / theta angles to orient the camera.
static const float VIEW_DEFAULT[2] = { 45.0f, 45.0f };
static const float VIEW_LEFT[2] = { 90.0f, 90.0f };
static const float VIEW_RIGHT[2] = { -90.0f, 90.0f };
static const float VIEW_TOP[2] = { 0.0f, 0.0f };
static const float VIEW_BOTTOM[2] = { 0.0f, 180.0f };
static const float VIEW_FRONT[2] = { 0.0f, 90.0f };
static const float VIEW_REAR[2] = { 180.0f, 90.0f };

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

bool Camera::select_view(const std::string& direction)
{
    const float* dir_vec = nullptr;

    if (direction == "iso")
        dir_vec = VIEW_DEFAULT;
    else if (direction == "left")
        dir_vec = VIEW_LEFT;
    else if (direction == "right")
        dir_vec = VIEW_RIGHT;
    else if (direction == "top")
        dir_vec = VIEW_TOP;
    else if (direction == "bottom")
        dir_vec = VIEW_BOTTOM;
    else if (direction == "front")
        dir_vec = VIEW_FRONT;
    else if (direction == "rear")
        dir_vec = VIEW_REAR;

    if (dir_vec != nullptr)
    {
        phi = dir_vec[0];
        set_theta(dir_vec[1], false);
        return true;
    }
    else
        return false;
}

void Camera::apply_viewport(int x, int y, unsigned int w, unsigned int h) const
{
    glsafe(::glViewport(0, 0, w, h));
    glsafe(::glGetIntegerv(GL_VIEWPORT, m_viewport.data()));
}

void Camera::apply_view_matrix() const
{
    glsafe(::glMatrixMode(GL_MODELVIEW));
    glsafe(::glLoadIdentity());

    glsafe(::glRotatef(-m_theta, 1.0f, 0.0f, 0.0f)); // pitch
    glsafe(::glRotatef(phi, 0.0f, 0.0f, 1.0f));          // yaw
    glsafe(::glTranslated(-m_target(0), -m_target(1), -m_target(2)));

    glsafe(::glGetDoublev(GL_MODELVIEW_MATRIX, m_view_matrix.data()));
}

void Camera::apply_ortho_projection(float x_min, float x_max, float y_min, float y_max, float z_min, float z_max) const
{
    glsafe(::glMatrixMode(GL_PROJECTION));
    glsafe(::glLoadIdentity());

    glsafe(::glOrtho(x_min, x_max, y_min, y_max, z_min, z_max));
    glsafe(::glGetDoublev(GL_PROJECTION_MATRIX, m_projection_matrix.data()));

    glsafe(::glMatrixMode(GL_MODELVIEW));
}

} // GUI
} // Slic3r

