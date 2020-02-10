#include "libslic3r/libslic3r.h"

#include "Camera.hpp"
#if !ENABLE_THUMBNAIL_GENERATOR
#include "3DScene.hpp"
#endif // !ENABLE_THUMBNAIL_GENERATOR
#include "GUI_App.hpp"
#include "AppConfig.hpp"
#if ENABLE_CAMERA_STATISTICS
#if ENABLE_6DOF_CAMERA
#include "Mouse3DController.hpp"
#endif // ENABLE_6DOF_CAMERA
#endif // ENABLE_CAMERA_STATISTICS

#include <GL/glew.h>

#if !ENABLE_6DOF_CAMERA
static const float GIMBALL_LOCK_THETA_MAX = 180.0f;
#endif // !ENABLE_6DOF_CAMERA

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
#if ENABLE_THUMBNAIL_GENERATOR
const double Camera::DefaultZoomToBoxMarginFactor = 1.025;
const double Camera::DefaultZoomToVolumesMarginFactor = 1.025;
#endif // ENABLE_THUMBNAIL_GENERATOR
double Camera::FrustrumMinZRange = 50.0;
double Camera::FrustrumMinNearZ = 100.0;
double Camera::FrustrumZMargin = 10.0;
double Camera::MaxFovDeg = 60.0;

Camera::Camera()
#if ENABLE_6DOF_CAMERA
    : requires_zoom_to_bed(false)
#else
    : phi(45.0f)
    , requires_zoom_to_bed(false)
    , inverted_phi(false)
#endif // ENABLE_6DOF_CAMERA
    , m_type(Perspective)
    , m_target(Vec3d::Zero())
#if ENABLE_6DOF_CAMERA
    , m_zenit(45.0f)
#else
    , m_theta(45.0f)
#endif // ENABLE_6DOF_CAMERA
    , m_zoom(1.0)
    , m_distance(DefaultDistance)
    , m_gui_scale(1.0)
    , m_view_matrix(Transform3d::Identity())
    , m_projection_matrix(Transform3d::Identity())
{
#if ENABLE_6DOF_CAMERA
    set_default_orientation();
#endif // ENABLE_6DOF_CAMERA
}

std::string Camera::get_type_as_string() const
{
    switch (m_type)
    {
    case Unknown:     return "unknown";
    case Perspective: return "perspective";
    default:
    case Ortho:       return "orthographic";
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
    set_type((type == "1") ? Perspective : Ortho);
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
#if ENABLE_6DOF_CAMERA
    translate_world(target - m_target);
#else
    BoundingBoxf3 test_box = m_scene_box;
    test_box.translate(-m_scene_box.center());
    // We may let this factor be customizable
    static const double ScaleFactor = 1.5;
    test_box.scale(ScaleFactor);
    test_box.translate(m_scene_box.center());

    m_target(0) = clamp(test_box.min(0), test_box.max(0), target(0));
    m_target(1) = clamp(test_box.min(1), test_box.max(1), target(1));
    m_target(2) = clamp(test_box.min(2), test_box.max(2), target(2));
#endif // ENABLE_6DOF_CAMERA
}

#if !ENABLE_6DOF_CAMERA
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
#endif // !ENABLE_6DOF_CAMERA

void Camera::update_zoom(double delta_zoom)
{
    set_zoom(m_zoom / (1.0 - std::max(std::min(delta_zoom, 4.0), -4.0) * 0.1));
}

void Camera::set_zoom(double zoom)
{
    // Don't allow to zoom too far outside the scene.
    double zoom_min = min_zoom();
    if (zoom_min > 0.0)
        zoom = std::max(zoom, zoom_min);

    // Don't allow to zoom too close to the scene.
    m_zoom = std::min(zoom, max_zoom());
}

#if ENABLE_6DOF_CAMERA
void Camera::select_view(const std::string& direction)
{
    if (direction == "iso")
        set_default_orientation();
    else if (direction == "left")
        look_at(m_target - m_distance * Vec3d::UnitX(), m_target, Vec3d::UnitZ());
    else if (direction == "right")
        look_at(m_target + m_distance * Vec3d::UnitX(), m_target, Vec3d::UnitZ());
    else if (direction == "top")
        look_at(m_target + m_distance * Vec3d::UnitZ(), m_target, Vec3d::UnitY());
    else if (direction == "bottom")
        look_at(m_target - m_distance * Vec3d::UnitZ(), m_target, -Vec3d::UnitY());
    else if (direction == "front")
        look_at(m_target - m_distance * Vec3d::UnitY(), m_target, Vec3d::UnitZ());
    else if (direction == "rear")
        look_at(m_target + m_distance * Vec3d::UnitY(), m_target, Vec3d::UnitZ());
}
#else
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
#endif // ENABLE_6DOF_CAMERA

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
#if !ENABLE_6DOF_CAMERA
    double theta_rad = Geometry::deg2rad(-(double)m_theta);
    double phi_rad = Geometry::deg2rad((double)phi);
    double sin_theta = ::sin(theta_rad);
    Vec3d camera_pos = m_target + m_distance * Vec3d(sin_theta * ::sin(phi_rad), sin_theta * ::cos(phi_rad), ::cos(theta_rad));
#endif // !ENABLE_6DOF_CAMERA

    glsafe(::glMatrixMode(GL_MODELVIEW));
    glsafe(::glLoadIdentity());

#if ENABLE_6DOF_CAMERA
    glsafe(::glMultMatrixd(m_view_matrix.data()));
#else
    glsafe(::glRotatef(-m_theta, 1.0f, 0.0f, 0.0f)); // pitch
    glsafe(::glRotatef(phi, 0.0f, 0.0f, 1.0f));      // yaw

    glsafe(::glTranslated(-camera_pos(0), -camera_pos(1), -camera_pos(2))); 

    glsafe(::glGetDoublev(GL_MODELVIEW_MATRIX, m_view_matrix.data()));
#endif // ENABLE_6DOF_CAMERA
}

void Camera::apply_projection(const BoundingBoxf3& box, double near_z, double far_z) const
{
#if !ENABLE_6DOF_CAMERA
    set_distance(DefaultDistance);
#endif // !ENABLE_6DOF_CAMERA

    double w = 0.0;
    double h = 0.0;

#if ENABLE_6DOF_CAMERA
    double old_distance = m_distance;
    m_frustrum_zs = calc_tight_frustrum_zs_around(box);
    if (m_distance != old_distance)
        // the camera has been moved re-apply view matrix
        apply_view_matrix();
#else
    while (true)
    {
        m_frustrum_zs = calc_tight_frustrum_zs_around(box);
#endif // !ENABLE_6DOF_CAMERA

        if (near_z > 0.0)
            m_frustrum_zs.first = std::max(std::min(m_frustrum_zs.first, near_z), FrustrumMinNearZ);

        if (far_z > 0.0)
            m_frustrum_zs.second = std::max(m_frustrum_zs.second, far_z);

        w = 0.5 * (double)m_viewport[2];
        h = 0.5 * (double)m_viewport[3];

        double inv_zoom = get_inv_zoom();
        w *= inv_zoom;
        h *= inv_zoom;

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

#if !ENABLE_6DOF_CAMERA
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
#endif // !ENABLE_6DOF_CAMERA

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

#if ENABLE_THUMBNAIL_GENERATOR
#if ENABLE_6DOF_CAMERA
void Camera::zoom_to_box(const BoundingBoxf3& box, double margin_factor)
#else
void Camera::zoom_to_box(const BoundingBoxf3& box, int canvas_w, int canvas_h, double margin_factor)
#endif // ENABLE_6DOF_CAMERA
#else
void Camera::zoom_to_box(const BoundingBoxf3& box, int canvas_w, int canvas_h)
#endif // ENABLE_THUMBNAIL_GENERATOR
{
    // Calculate the zoom factor needed to adjust the view around the given box.
#if ENABLE_THUMBNAIL_GENERATOR
#if ENABLE_6DOF_CAMERA
    double zoom = calc_zoom_to_bounding_box_factor(box, margin_factor);
#else
    double zoom = calc_zoom_to_bounding_box_factor(box, canvas_w, canvas_h, margin_factor);
#endif // ENABLE_6DOF_CAMERA
#else
    double zoom = calc_zoom_to_bounding_box_factor(box, canvas_w, canvas_h);
#endif // ENABLE_THUMBNAIL_GENERATOR
    if (zoom > 0.0)
    {
        m_zoom = zoom;
        // center view around box center
#if ENABLE_6DOF_CAMERA
        set_target(box.center());
#else
        m_target = box.center();
#endif // ENABLE_6DOF_CAMERA
    }
}

#if ENABLE_THUMBNAIL_GENERATOR
#if ENABLE_6DOF_CAMERA
void Camera::zoom_to_volumes(const GLVolumePtrs& volumes, double margin_factor)
#else
void Camera::zoom_to_volumes(const GLVolumePtrs& volumes, int canvas_w, int canvas_h, double margin_factor)
#endif // ENABLE_6DOF_CAMERA
{
    Vec3d center;
#if ENABLE_6DOF_CAMERA
    double zoom = calc_zoom_to_volumes_factor(volumes, center, margin_factor);
#else
    double zoom = calc_zoom_to_volumes_factor(volumes, canvas_w, canvas_h, center, margin_factor);
#endif // ENABLE_6DOF_CAMERA
    if (zoom > 0.0)
    {
        m_zoom = zoom;
        // center view around the calculated center
#if ENABLE_6DOF_CAMERA
        set_target(center);
#else
        m_target = center;
#endif // ENABLE_6DOF_CAMERA
    }
}
#endif // ENABLE_THUMBNAIL_GENERATOR

#if ENABLE_CAMERA_STATISTICS
void Camera::debug_render() const
{
    ImGuiWrapper& imgui = *wxGetApp().imgui();
    imgui.begin(std::string("Camera statistics"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    std::string type = get_type_as_string();
#if ENABLE_6DOF_CAMERA
    if (wxGetApp().plater()->get_mouse3d_controller().is_running() || (wxGetApp().app_config->get("use_free_camera") == "1"))
        type += "/free";
    else
        type += "/constrained";
#endif // ENABLE_6DOF_CAMERA
    Vec3f position = get_position().cast<float>();
    Vec3f target = m_target.cast<float>();
    float distance = (float)get_distance();
#if ENABLE_6DOF_CAMERA
    float zenit = (float)m_zenit;
#endif // ENABLE_6DOF_CAMERA
    Vec3f forward = get_dir_forward().cast<float>();
    Vec3f right = get_dir_right().cast<float>();
    Vec3f up = get_dir_up().cast<float>();
    float nearZ = (float)m_frustrum_zs.first;
    float farZ = (float)m_frustrum_zs.second;
    float deltaZ = farZ - nearZ;
    float zoom = (float)m_zoom;
    float fov = (float)get_fov();
    std::array<int, 4>viewport = get_viewport();
    float gui_scale = (float)get_gui_scale();

    ImGui::InputText("Type", type.data(), type.length(), ImGuiInputTextFlags_ReadOnly);
    ImGui::Separator();
    ImGui::InputFloat3("Position", position.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat3("Target", target.data(), "%.6f", ImGuiInputTextFlags_ReadOnly);
    ImGui::InputFloat("Distance", &distance, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
#if ENABLE_6DOF_CAMERA
    ImGui::Separator();
    ImGui::InputFloat("Zenit", &zenit, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
#endif // ENABLE_6DOF_CAMERA
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
    ImGui::InputInt4("Viewport", viewport.data(), ImGuiInputTextFlags_ReadOnly);
    ImGui::Separator();
    ImGui::InputFloat("GUI scale", &gui_scale, 0.0f, 0.0f, "%.6f", ImGuiInputTextFlags_ReadOnly);
    imgui.end();
}
#endif // ENABLE_CAMERA_STATISTICS

#if ENABLE_6DOF_CAMERA
void Camera::translate_world(const Vec3d& displacement)
{
    Vec3d new_target = validate_target(m_target + displacement);
    Vec3d new_displacement = new_target - m_target;
    if (!new_displacement.isApprox(Vec3d::Zero()))
    {
        m_target += new_displacement;
        m_view_matrix.translate(-new_displacement);
    }
}

void Camera::rotate_on_sphere(double delta_azimut_rad, double delta_zenit_rad, bool apply_limits)
{
    m_zenit += Geometry::rad2deg(delta_zenit_rad);
    if (apply_limits) {
        if (m_zenit > 90.0f) {
            delta_zenit_rad -= Geometry::deg2rad(m_zenit - 90.0f);
            m_zenit = 90.0f;
        }
        else if (m_zenit < -90.0f) {
            delta_zenit_rad -= Geometry::deg2rad(m_zenit + 90.0f);
            m_zenit = -90.0f;
        }
    }

    // FIXME -> The following is a HACK !!!
    // When the value of the zenit rotation is large enough, the following call to rotate() shows
    // numerical instability introducing some scaling into m_view_matrix (verified by checking
    // that the camera space unit vectors are no more unit).
    // See also https://dev.prusa3d.com/browse/SPE-1082
    // We split the zenit rotation into a set of smaller rotations which are then applied.
    static const double MAX_ALLOWED = Geometry::deg2rad(0.1);
    unsigned int zenit_steps_count = 1 + (unsigned int)(std::abs(delta_zenit_rad) / MAX_ALLOWED);
    double zenit_step = delta_zenit_rad / (double)zenit_steps_count;

    Vec3d target = m_target;
    translate_world(-target);

    if (zenit_step != 0.0)
    {
        Vec3d right = get_dir_right();
        for (unsigned int i = 0; i < zenit_steps_count; ++i)
        {
            m_view_matrix.rotate(Eigen::AngleAxisd(zenit_step, right));
        }
    }

    if (delta_azimut_rad != 0.0)
        m_view_matrix.rotate(Eigen::AngleAxisd(delta_azimut_rad, Vec3d::UnitZ()));

    translate_world(target);
}

void Camera::rotate_local_around_target(const Vec3d& rotation_rad)
{
    rotate_local_around_pivot(rotation_rad, m_target);
}

void Camera::rotate_local_around_pivot(const Vec3d& rotation_rad, const Vec3d& pivot)
{
    // we use a copy of the pivot because a reference to the current m_target may be passed in (see i.e. rotate_local_around_target())
    // and m_target is modified by the translate_world() calls
    Vec3d center = pivot;
    translate_world(-center);
    m_view_matrix.rotate(Eigen::AngleAxisd(rotation_rad(0), get_dir_right()));
    m_view_matrix.rotate(Eigen::AngleAxisd(rotation_rad(1), get_dir_up()));
    m_view_matrix.rotate(Eigen::AngleAxisd(rotation_rad(2), get_dir_forward()));
    translate_world(center);
    update_zenit();
}
#endif // ENABLE_6DOF_CAMERA

double Camera::min_zoom() const
{
#if ENABLE_6DOF_CAMERA
    return 0.7 * calc_zoom_to_bounding_box_factor(m_scene_box);
#else
    return 0.7 * calc_zoom_to_bounding_box_factor(m_scene_box, m_viewport[2], m_viewport[3]);
#endif // ENABLE_6DOF_CAMERA
}

std::pair<double, double> Camera::calc_tight_frustrum_zs_around(const BoundingBoxf3& box) const
{
    std::pair<double, double> ret;
    auto& [near_z, far_z] = ret;

#if !ENABLE_6DOF_CAMERA
    while (true)
    {
#endif // !ENABLE_6DOF_CAMERA
        // box in eye space
        BoundingBoxf3 eye_box = box.transformed(m_view_matrix);
        near_z = -eye_box.max(2);
        far_z = -eye_box.min(2);

        // apply margin
        near_z -= FrustrumZMargin;
        far_z += FrustrumZMargin;

        // ensure min size
        if (far_z - near_z < FrustrumMinZRange)
        {
            double mid_z = 0.5 * (near_z + far_z);
            double half_size = 0.5 * FrustrumMinZRange;
            near_z = mid_z - half_size;
            far_z = mid_z + half_size;
        }

#if ENABLE_6DOF_CAMERA
        if (near_z < FrustrumMinNearZ)
        {
            float delta = FrustrumMinNearZ - near_z;
            set_distance(m_distance + delta);
            near_z += delta;
            far_z += delta;
        }
        else if ((near_z > 2.0 * FrustrumMinNearZ) && (m_distance > DefaultDistance))
        {
            float delta = m_distance - DefaultDistance;
            set_distance(DefaultDistance);
            near_z -= delta;
            far_z -= delta;
        }
#else
        if (near_z >= FrustrumMinNearZ)
            break;

        // ensure min near z
        set_distance(m_distance + FrustrumMinNearZ - near_z);
    }
#endif // ENABLE_6DOF_CAMERA

    return ret;
}

#if ENABLE_THUMBNAIL_GENERATOR
#if ENABLE_6DOF_CAMERA
double Camera::calc_zoom_to_bounding_box_factor(const BoundingBoxf3& box, double margin_factor) const
#else
double Camera::calc_zoom_to_bounding_box_factor(const BoundingBoxf3& box, int canvas_w, int canvas_h, double margin_factor) const
#endif // ENABLE_6DOF_CAMERA
#else
double Camera::calc_zoom_to_bounding_box_factor(const BoundingBoxf3& box, int canvas_w, int canvas_h) const
#endif // ENABLE_THUMBNAIL_GENERATOR
{
    double max_bb_size = box.max_size();
    if (max_bb_size == 0.0)
        return -1.0;

    // project the box vertices on a plane perpendicular to the camera forward axis
    // then calculates the vertices coordinate on this plane along the camera xy axes

#if !ENABLE_6DOF_CAMERA
    // ensure that the view matrix is updated
    apply_view_matrix();
#endif // !ENABLE_6DOF_CAMERA

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

    double min_x = DBL_MAX;
    double min_y = DBL_MAX;
    double max_x = -DBL_MAX;
    double max_y = -DBL_MAX;

#if !ENABLE_THUMBNAIL_GENERATOR
    // margin factor to give some empty space around the box
    double margin_factor = 1.25;
#endif // !ENABLE_THUMBNAIL_GENERATOR

    for (const Vec3d& v : vertices)
    {
        // project vertex on the plane perpendicular to camera forward axis
        Vec3d pos = v - bb_center;
        Vec3d proj_on_plane = pos - pos.dot(forward) * forward;

        // calculates vertex coordinate along camera xy axes
        double x_on_plane = proj_on_plane.dot(right);
        double y_on_plane = proj_on_plane.dot(up);

        min_x = std::min(min_x, x_on_plane);
        min_y = std::min(min_y, y_on_plane);
        max_x = std::max(max_x, x_on_plane);
        max_y = std::max(max_y, y_on_plane);
    }

    double dx = max_x - min_x;
    double dy = max_y - min_y;
    if ((dx <= 0.0) || (dy <= 0.0))
        return -1.0f;

    double med_x = 0.5 * (max_x + min_x);
    double med_y = 0.5 * (max_y + min_y);

    dx *= margin_factor;
    dy *= margin_factor;

#if ENABLE_6DOF_CAMERA
    return std::min((double)m_viewport[2] / dx, (double)m_viewport[3] / dy);
#else
    return std::min((double)canvas_w / dx, (double)canvas_h / dy);
#endif // ENABLE_6DOF_CAMERA
}

#if ENABLE_THUMBNAIL_GENERATOR
#if ENABLE_6DOF_CAMERA
double Camera::calc_zoom_to_volumes_factor(const GLVolumePtrs& volumes, Vec3d& center, double margin_factor) const
#else
double Camera::calc_zoom_to_volumes_factor(const GLVolumePtrs& volumes, int canvas_w, int canvas_h, Vec3d& center, double margin_factor) const
#endif // ENABLE_6DOF_CAMERA
{
    if (volumes.empty())
        return -1.0;

    // project the volumes vertices on a plane perpendicular to the camera forward axis
    // then calculates the vertices coordinate on this plane along the camera xy axes

#if !ENABLE_6DOF_CAMERA
    // ensure that the view matrix is updated
    apply_view_matrix();
#endif // !ENABLE_6DOF_CAMERA

    Vec3d right = get_dir_right();
    Vec3d up = get_dir_up();
    Vec3d forward = get_dir_forward();

    BoundingBoxf3 box;
    for (const GLVolume* volume : volumes)
    {
        box.merge(volume->transformed_bounding_box());
    }
    center = box.center();

    double min_x = DBL_MAX;
    double min_y = DBL_MAX;
    double max_x = -DBL_MAX;
    double max_y = -DBL_MAX;

    for (const GLVolume* volume : volumes)
    {
        const Transform3d& transform = volume->world_matrix();
        const TriangleMesh* hull = volume->convex_hull();
        if (hull == nullptr)
            continue;

        for (const Vec3f& vertex : hull->its.vertices)
        {
            Vec3d v = transform * vertex.cast<double>();

            // project vertex on the plane perpendicular to camera forward axis
            Vec3d pos = v - center;
            Vec3d proj_on_plane = pos - pos.dot(forward) * forward;

            // calculates vertex coordinate along camera xy axes
            double x_on_plane = proj_on_plane.dot(right);
            double y_on_plane = proj_on_plane.dot(up);

            min_x = std::min(min_x, x_on_plane);
            min_y = std::min(min_y, y_on_plane);
            max_x = std::max(max_x, x_on_plane);
            max_y = std::max(max_y, y_on_plane);
        }
    }

    center += 0.5 * (max_x + min_x) * right + 0.5 * (max_y + min_y) * up;

    double dx = margin_factor * (max_x - min_x);
    double dy = margin_factor * (max_y - min_y);

    if ((dx <= 0.0) || (dy <= 0.0))
        return -1.0f;

#if ENABLE_6DOF_CAMERA
    return std::min((double)m_viewport[2] / dx, (double)m_viewport[3] / dy);
#else
    return std::min((double)canvas_w / dx, (double)canvas_h / dy);
#endif // ENABLE_6DOF_CAMERA
}
#endif // ENABLE_THUMBNAIL_GENERATOR

void Camera::set_distance(double distance) const
{
#if ENABLE_6DOF_CAMERA
    if (m_distance != distance)
    {
        m_view_matrix.translate((distance - m_distance) * get_dir_forward());
        m_distance = distance;
    }
#else
    m_distance = distance;
    apply_view_matrix();
#endif // ENABLE_6DOF_CAMERA
}

#if ENABLE_6DOF_CAMERA
void Camera::look_at(const Vec3d& position, const Vec3d& target, const Vec3d& up)
{
    Vec3d unit_z = (position - target).normalized();
    Vec3d unit_x = up.cross(unit_z).normalized();
    Vec3d unit_y = unit_z.cross(unit_x).normalized();

    m_target = target;
    Vec3d new_position = m_target + m_distance * unit_z;

    m_view_matrix(0, 0) = unit_x(0);
    m_view_matrix(0, 1) = unit_x(1);
    m_view_matrix(0, 2) = unit_x(2);
    m_view_matrix(0, 3) = -unit_x.dot(new_position);

    m_view_matrix(1, 0) = unit_y(0);
    m_view_matrix(1, 1) = unit_y(1);
    m_view_matrix(1, 2) = unit_y(2);
    m_view_matrix(1, 3) = -unit_y.dot(new_position);

    m_view_matrix(2, 0) = unit_z(0);
    m_view_matrix(2, 1) = unit_z(1);
    m_view_matrix(2, 2) = unit_z(2);
    m_view_matrix(2, 3) = -unit_z.dot(new_position);

    m_view_matrix(3, 0) = 0.0;
    m_view_matrix(3, 1) = 0.0;
    m_view_matrix(3, 2) = 0.0;
    m_view_matrix(3, 3) = 1.0;

    update_zenit();
}

void Camera::set_default_orientation()
{
    m_zenit = 45.0f;
    double theta_rad = Geometry::deg2rad(-(double)m_zenit);
    double phi_rad = Geometry::deg2rad(45.0);
    double sin_theta = ::sin(theta_rad);
    Vec3d camera_pos = m_target + m_distance * Vec3d(sin_theta * ::sin(phi_rad), sin_theta * ::cos(phi_rad), ::cos(theta_rad));
    m_view_matrix = Transform3d::Identity();
    m_view_matrix.rotate(Eigen::AngleAxisd(theta_rad, Vec3d::UnitX())).rotate(Eigen::AngleAxisd(phi_rad, Vec3d::UnitZ())).translate(-camera_pos);
}

Vec3d Camera::validate_target(const Vec3d& target) const
{
    BoundingBoxf3 test_box = m_scene_box;
    test_box.translate(-m_scene_box.center());
    // We may let this factor be customizable
    static const double ScaleFactor = 1.5;
    test_box.scale(ScaleFactor);
    test_box.translate(m_scene_box.center());

    return Vec3d(std::clamp(target(0), test_box.min(0), test_box.max(0)),
        std::clamp(target(1), test_box.min(1), test_box.max(1)),
        std::clamp(target(2), test_box.min(2), test_box.max(2)));
}

void Camera::update_zenit()
{
    m_zenit = Geometry::rad2deg(0.5 * M_PI - std::acos(std::clamp(-get_dir_forward().dot(Vec3d::UnitZ()), -1.0, 1.0)));
}
#endif // ENABLE_6DOF_CAMERA

} // GUI
} // Slic3r

