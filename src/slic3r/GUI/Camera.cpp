#include "libslic3r/libslic3r.h"

#include "Camera.hpp"
#include "3DScene.hpp"
#if ENABLE_CAMERA_STATISTICS
#include "GUI_App.hpp"
#endif // ENABLE_CAMERA_STATISTICS

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
    , inverted_phi(false)
    , m_theta(45.0f)
    , m_target(Vec3d::Zero())
    , m_view_matrix(Transform3d::Identity())
    , m_projection_matrix(Transform3d::Identity())
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
    glsafe(::glRotatef(phi, 0.0f, 0.0f, 1.0f));      // yaw
    glsafe(::glTranslated(-m_target(0), -m_target(1), -m_target(2))); // target to origin

    glsafe(::glGetDoublev(GL_MODELVIEW_MATRIX, m_view_matrix.data()));
}

void Camera::apply_projection(const BoundingBoxf3& box) const
{
    switch (type)
    {
    case Ortho:
    {
        double w2 = (double)m_viewport[2];
        double h2 = (double)m_viewport[3];
        double two_zoom = 2.0 * zoom;
        if (two_zoom != 0.0)
        {
            double inv_two_zoom = 1.0 / two_zoom;
            w2 *= inv_two_zoom;
            h2 *= inv_two_zoom;
        }

        // FIXME: calculate a tighter value for depth will improve z-fighting
        // Set at least some minimum depth in case the bounding box is empty to avoid an OpenGL driver error.
        double depth = std::max(1.0, 5.0 * box.max_size());
        apply_ortho_projection(-w2, w2, -h2, h2, -depth, depth);

        break;
    }
//    case Perspective:
//    {
//    }
    }
}

#if ENABLE_CAMERA_STATISTICS
void Camera::debug_render() const
{
    ImGuiWrapper& imgui = *wxGetApp().imgui();
    imgui.set_next_window_bg_alpha(0.5f);
    imgui.begin(std::string("Camera statistics"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    Vec3f position = get_position().cast<float>();
    Vec3f target = m_target.cast<float>();
    Vec3f forward = get_dir_forward().cast<float>();
    Vec3f right = get_dir_right().cast<float>();
    Vec3f up = get_dir_up().cast<float>();

    ImGui::InputFloat3("Position", position.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("Target", target.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::Separator();
    ImGui::InputFloat3("Forward", forward.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("Right", right.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("Up", up.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    imgui.end();
}
#endif // ENABLE_CAMERA_STATISTICS

void Camera::apply_ortho_projection(double x_min, double x_max, double y_min, double y_max, double z_min, double z_max) const
{
    glsafe(::glMatrixMode(GL_PROJECTION));
    glsafe(::glLoadIdentity());

    glsafe(::glOrtho(x_min, x_max, y_min, y_max, z_min, z_max));
    glsafe(::glGetDoublev(GL_PROJECTION_MATRIX, m_projection_matrix.data()));

    glsafe(::glMatrixMode(GL_MODELVIEW));
}

} // GUI
} // Slic3r

