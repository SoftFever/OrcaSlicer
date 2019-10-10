#include "libslic3r/libslic3r.h"

#include "Camera.hpp"
#include "3DScene.hpp"
#include "GUI_App.hpp"
#include "AppConfig.hpp"

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

const double Camera::DefaultDistance = 1000.0;
double Camera::FrustrumMinZRange = 50.0;
double Camera::FrustrumMinNearZ = 100.0;
double Camera::FrustrumZMargin = 10.0;
double Camera::MaxFovDeg = 60.0;

Camera::Camera()
    : phi(45.0f)
    , requires_zoom_to_bed(false)
    , inverted_phi(false)
    , m_type(Perspective)
    , m_target(Vec3d::Zero())
    , m_theta(45.0f)
    , m_zoom(1.0)
    , m_distance(DefaultDistance)
    , m_gui_scale(1.0)
    , m_view_matrix(Transform3d::Identity())
    , m_projection_matrix(Transform3d::Identity())
{
}

std::string Camera::get_type_as_string() const
{
    switch (m_type)
    {
    case Unknown:
        return "unknown";
    case Perspective:
        return "perspective";
    default:
    case Ortho:
        return "orthographic";
    };
}

void Camera::set_type(EType type)
{
    if (m_type != type)
    {
        m_type = type;
        wxGetApp().app_config->set("use_perspective_camera", (m_type == Perspective) ? "1" : "0");
        wxGetApp().app_config->save();
    }
}

void Camera::set_type(const std::string& type)
{
    if (type == "1")
        set_type(Perspective);
    else
        set_type(Ortho);
}

void Camera::select_next_type()
{
    unsigned char next = (unsigned char)m_type + 1;
    if (next == (unsigned char)Num_types)
        next = 1;

    set_type((EType)next);
}

void Camera::set_target(const Vec3d& target)
{
#if ENABLE_3DCONNEXION_DEVICES
    // We may let these factors be customizable
    static const double ScaleFactor = 1.1;
    BoundingBoxf3 test_box = m_scene_box;
    test_box.scale(ScaleFactor);
    m_target = target;
    m_target(0) = clamp(test_box.min(0), test_box.max(0), m_target(0));
    m_target(1) = clamp(test_box.min(1), test_box.max(1), m_target(1));
    m_target(2) = clamp(test_box.min(2), test_box.max(2), m_target(2));
#else
    m_target = target;
    m_target(0) = clamp(m_scene_box.min(0), m_scene_box.max(0), m_target(0));
    m_target(1) = clamp(m_scene_box.min(1), m_scene_box.max(1), m_target(1));
    m_target(2) = clamp(m_scene_box.min(2), m_scene_box.max(2), m_target(2));
#endif // ENABLE_3DCONNEXION_DEVICES
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

#if ENABLE_3DCONNEXION_DEVICES
void Camera::update_zoom(double delta_zoom)
{
    set_zoom(m_zoom / (1.0 - std::max(std::min(delta_zoom, 4.0), -4.0) * 0.1));
}

void Camera::set_zoom(double zoom)
{
    // Don't allow to zoom too far outside the scene.
    double zoom_min = calc_zoom_to_bounding_box_factor(m_scene_box, (int)m_viewport[2], (int)m_viewport[3]);
    if (zoom_min > 0.0)
        zoom = std::max(zoom, zoom_min * 0.7);

    // Don't allow to zoom too close to the scene.
    m_zoom = std::min(zoom, 100.0);
}
#else
void Camera::set_zoom(double zoom, const BoundingBoxf3& max_box, int canvas_w, int canvas_h)
{
    zoom = std::max(std::min(zoom, 4.0), -4.0) / 10.0;
    zoom = m_zoom / (1.0 - zoom);

    // Don't allow to zoom too far outside the scene.
    double zoom_min = calc_zoom_to_bounding_box_factor(max_box, canvas_w, canvas_h);
    if (zoom_min > 0.0)
        zoom = std::max(zoom, zoom_min * 0.7);

    // Don't allow to zoom too close to the scene.
    zoom = std::min(zoom, 100.0);

    m_zoom = zoom;
}
#endif // ENABLE_3DCONNEXION_DEVICES

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

double Camera::get_fov() const
{
    switch (m_type)
    {
    case Perspective:
        return 2.0 * Geometry::rad2deg(std::atan(1.0 / m_projection_matrix.matrix()(1, 1)));
    default:
    case Ortho:
        return 0.0;
    };
}

void Camera::apply_viewport(int x, int y, unsigned int w, unsigned int h) const
{
    glsafe(::glViewport(0, 0, w, h));
    glsafe(::glGetIntegerv(GL_VIEWPORT, m_viewport.data()));
}

void Camera::apply_view_matrix() const
{
    double theta_rad = Geometry::deg2rad(-(double)m_theta);
    double phi_rad = Geometry::deg2rad((double)phi);
    double sin_theta = ::sin(theta_rad);
    Vec3d camera_pos = m_target + m_distance * Vec3d(sin_theta * ::sin(phi_rad), sin_theta * ::cos(phi_rad), ::cos(theta_rad));

    glsafe(::glMatrixMode(GL_MODELVIEW));
    glsafe(::glLoadIdentity());

    glsafe(::glRotatef(-m_theta, 1.0f, 0.0f, 0.0f)); // pitch
    glsafe(::glRotatef(phi, 0.0f, 0.0f, 1.0f));      // yaw

    glsafe(::glTranslated(-camera_pos(0), -camera_pos(1), -camera_pos(2))); 

    glsafe(::glGetDoublev(GL_MODELVIEW_MATRIX, m_view_matrix.data()));
}

void Camera::apply_projection(const BoundingBoxf3& box) const
{
    set_distance(DefaultDistance);

    double w = 0.0;
    double h = 0.0;

    while (true)
    {
        m_frustrum_zs = calc_tight_frustrum_zs_around(box);

        w = 0.5 * (double)m_viewport[2];
        h = 0.5 * (double)m_viewport[3];

        if (m_zoom != 0.0)
        {
            double inv_zoom = 1.0 / m_zoom;
            w *= inv_zoom;
            h *= inv_zoom;
        }

        switch (m_type)
        {
        default:
        case Ortho:
        {
            m_gui_scale = 1.0;
            break;
        }
        case Perspective:
        {
            // scale near plane to keep w and h constant on the plane at z = m_distance
            double scale = m_frustrum_zs.first / m_distance;
            w *= scale;
            h *= scale;
            m_gui_scale = scale;
            break;
        }
        }

        if (m_type == Perspective)
        {
            double fov_deg = Geometry::rad2deg(2.0 * std::atan(h / m_frustrum_zs.first));

            // adjust camera distance to keep fov in a limited range
            if (fov_deg > MaxFovDeg)
            {
                double delta_z = h / ::tan(0.5 * Geometry::deg2rad(MaxFovDeg)) - m_frustrum_zs.first;
                if (delta_z > 0.001)
                    set_distance(m_distance + delta_z);
                else
                    break;
            }
            else
                break;
        }
        else
            break;
    }

    glsafe(::glMatrixMode(GL_PROJECTION));
    glsafe(::glLoadIdentity());

    switch (m_type)
    {
    default:
    case Ortho:
    {
        glsafe(::glOrtho(-w, w, -h, h, m_frustrum_zs.first, m_frustrum_zs.second));
        break;
    }
    case Perspective:
    {
        glsafe(::glFrustum(-w, w, -h, h, m_frustrum_zs.first, m_frustrum_zs.second));
        break;
    }
    }

    glsafe(::glGetDoublev(GL_PROJECTION_MATRIX, m_projection_matrix.data()));
    glsafe(::glMatrixMode(GL_MODELVIEW));
}

void Camera::zoom_to_box(const BoundingBoxf3& box, int canvas_w, int canvas_h)
{
    // Calculate the zoom factor needed to adjust the view around the given box.
    double zoom = calc_zoom_to_bounding_box_factor(box, canvas_w, canvas_h);
    if (zoom > 0.0)
    {
        m_zoom = zoom;
        // center view around box center
        m_target = box.center();
    }
}

#if ENABLE_CAMERA_STATISTICS
void Camera::debug_render() const
{
    ImGuiWrapper& imgui = *wxGetApp().imgui();
    imgui.set_next_window_bg_alpha(0.5f);
    imgui.begin(std::string("Camera statistics"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    std::string type = get_type_as_string();
    Vec3f position = get_position().cast<float>();
    Vec3f target = m_target.cast<float>();
    float distance = (float)get_distance();
    Vec3f forward = get_dir_forward().cast<float>();
    Vec3f right = get_dir_right().cast<float>();
    Vec3f up = get_dir_up().cast<float>();
    float nearZ = (float)m_frustrum_zs.first;
    float farZ = (float)m_frustrum_zs.second;
    float deltaZ = farZ - nearZ;
    float zoom = (float)m_zoom;
    float fov = (float)get_fov();
    float gui_scale = (float)get_gui_scale();

    ImGui::InputText("Type", const_cast<char*>(type.data()), type.length(), ImGuiInputTextFlags_ReadOnly);
    ImGui::Separator();
    ImGui::InputFloat3("Position", position.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("Target", target.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat("Distance", &distance, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::Separator();
    ImGui::InputFloat3("Forward", forward.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("Right", right.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("Up", up.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::Separator();
    ImGui::InputFloat("Near Z", &nearZ, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat("Far Z", &farZ, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat("Delta Z", &deltaZ, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::Separator();
    ImGui::InputFloat("Zoom", &zoom, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat("Fov", &fov, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::Separator();
    ImGui::InputFloat("GUI scale", &gui_scale, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
    imgui.end();
}
#endif // ENABLE_CAMERA_STATISTICS

std::pair<double, double> Camera::calc_tight_frustrum_zs_around(const BoundingBoxf3& box) const
{
    std::pair<double, double> ret;

    while (true)
    {
        ret = std::make_pair(DBL_MAX, -DBL_MAX);

        // box vertices in world space
        std::vector<Vec3d> vertices;
        vertices.reserve(8);
        vertices.push_back(box.min);
        vertices.emplace_back(box.max(0), box.min(1), box.min(2));
        vertices.emplace_back(box.max(0), box.max(1), box.min(2));
        vertices.emplace_back(box.min(0), box.max(1), box.min(2));
        vertices.emplace_back(box.min(0), box.min(1), box.max(2));
        vertices.emplace_back(box.max(0), box.min(1), box.max(2));
        vertices.push_back(box.max);
        vertices.emplace_back(box.min(0), box.max(1), box.max(2));

        // set the Z range in eye coordinates (negative Zs are in front of the camera)
        for (const Vec3d& v : vertices)
        {
            double z = -(m_view_matrix * v)(2);
            ret.first = std::min(ret.first, z);
            ret.second = std::max(ret.second, z);
        }

        // apply margin
        ret.first -= FrustrumZMargin;
        ret.second += FrustrumZMargin;

        // ensure min size
        if (ret.second - ret.first < FrustrumMinZRange)
        {
            double mid_z = 0.5 * (ret.first + ret.second);
            double half_size = 0.5 * FrustrumMinZRange;
            ret.first = mid_z - half_size;
            ret.second = mid_z + half_size;
        }

        if (ret.first >= FrustrumMinNearZ)
            break;

        // ensure min Near Z
        set_distance(m_distance + FrustrumMinNearZ - ret.first);
    }

    return ret;
}

double Camera::calc_zoom_to_bounding_box_factor(const BoundingBoxf3& box, int canvas_w, int canvas_h) const
{
    double max_bb_size = box.max_size();
    if (max_bb_size == 0.0)
        return -1.0;

    // project the box vertices on a plane perpendicular to the camera forward axis
    // then calculates the vertices coordinate on this plane along the camera xy axes

    // ensure that the view matrix is updated
    apply_view_matrix();

    Vec3d right = get_dir_right();
    Vec3d up = get_dir_up();
    Vec3d forward = get_dir_forward();

    Vec3d bb_center = box.center();

    // box vertices in world space
    std::vector<Vec3d> vertices;
    vertices.reserve(8);
    vertices.push_back(box.min);
    vertices.emplace_back(box.max(0), box.min(1), box.min(2));
    vertices.emplace_back(box.max(0), box.max(1), box.min(2));
    vertices.emplace_back(box.min(0), box.max(1), box.min(2));
    vertices.emplace_back(box.min(0), box.min(1), box.max(2));
    vertices.emplace_back(box.max(0), box.min(1), box.max(2));
    vertices.push_back(box.max);
    vertices.emplace_back(box.min(0), box.max(1), box.max(2));

    double max_x = 0.0;
    double max_y = 0.0;

    // margin factor to give some empty space around the box
    double margin_factor = 1.25;

    for (const Vec3d& v : vertices)
    {
        // project vertex on the plane perpendicular to camera forward axis
        Vec3d pos(v(0) - bb_center(0), v(1) - bb_center(1), v(2) - bb_center(2));
        Vec3d proj_on_plane = pos - pos.dot(forward) * forward;

        // calculates vertex coordinate along camera xy axes
        double x_on_plane = proj_on_plane.dot(right);
        double y_on_plane = proj_on_plane.dot(up);

        max_x = std::max(max_x, std::abs(x_on_plane));
        max_y = std::max(max_y, std::abs(y_on_plane));
    }

    if ((max_x == 0.0) || (max_y == 0.0))
        return -1.0f;

    max_x *= margin_factor;
    max_y *= margin_factor;

    return std::min((double)canvas_w / (2.0 * max_x), (double)canvas_h / (2.0 * max_y));
}

void Camera::set_distance(double distance) const
{
    m_distance = distance;
    apply_view_matrix();
}

} // GUI
} // Slic3r

