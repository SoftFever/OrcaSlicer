#include "GLCanvas3D.hpp"

#include <wx/glcanvas.h>

#include <iostream>

static const float GIMBALL_LOCK_THETA_MAX = 180.0f;

namespace Slic3r {
namespace GUI {

GLCanvas3D::Camera::Camera()
    : m_type(CT_Ortho)
    , m_zoom(1.0f)
    , m_phi(45.0f)
    , m_theta(45.0f)
    , m_distance(0.0f)
    , m_target(0.0, 0.0, 0.0)
{
}

GLCanvas3D::Camera::EType GLCanvas3D::Camera::get_type() const
{
    return m_type;
}

void GLCanvas3D::Camera::set_type(GLCanvas3D::Camera::EType type)
{
    m_type = type;
}

std::string GLCanvas3D::Camera::get_type_as_string() const
{
    switch (m_type)
    {
    default:
    case CT_Unknown:
        return "unknown";
    case CT_Perspective:
        return "perspective";
    case CT_Ortho:
        return "ortho";
    };
}

float GLCanvas3D::Camera::get_zoom() const
{
    return m_zoom;
}

void GLCanvas3D::Camera::set_zoom(float zoom)
{
    m_zoom = zoom;
}

float GLCanvas3D::Camera::get_phi() const
{
    return m_phi;
}

void GLCanvas3D::Camera::set_phi(float phi)
{
    m_phi = phi;
}

float GLCanvas3D::Camera::get_theta() const
{
    return m_theta;
}

void GLCanvas3D::Camera::set_theta(float theta)
{
    m_theta = theta;

    // clamp angle
    if (m_theta > GIMBALL_LOCK_THETA_MAX)
        m_theta = GIMBALL_LOCK_THETA_MAX;

    if (m_theta < 0.0f)
        m_theta = 0.0f;
}

float GLCanvas3D::Camera::get_distance() const
{
    return m_distance;
}

void GLCanvas3D::Camera::set_distance(float distance)
{
    m_distance = distance;
}

const Pointf3& GLCanvas3D::Camera::get_target() const
{
    return m_target;
}

void GLCanvas3D::Camera::set_target(const Pointf3& target)
{
    m_target = target;
}

GLCanvas3D::GLCanvas3D(wxGLCanvas* canvas, wxGLContext* context)
    : m_canvas(canvas)
    , m_context(context)
    , m_dirty(true)
{
}

void GLCanvas3D::set_current()
{
    if ((m_canvas != nullptr) && (m_context != nullptr))
        m_canvas->SetCurrent(*m_context);
}

bool GLCanvas3D::is_dirty() const
{
    return m_dirty;
}

void GLCanvas3D::set_dirty(bool dirty)
{
    m_dirty = dirty;
}

bool GLCanvas3D::is_shown_on_screen() const
{
    return (m_canvas != nullptr) ? m_canvas->IsShownOnScreen() : false;
}

GLCanvas3D::Camera::EType GLCanvas3D::get_camera_type() const
{
    return m_camera.get_type();
}

void GLCanvas3D::set_camera_type(GLCanvas3D::Camera::EType type)
{
    m_camera.set_type(type);
}

std::string GLCanvas3D::get_camera_type_as_string() const
{
    return m_camera.get_type_as_string();
}

float GLCanvas3D::get_camera_zoom() const
{
    return m_camera.get_zoom();
}

void GLCanvas3D::set_camera_zoom(float zoom)
{
    m_camera.set_zoom(zoom);
}

float GLCanvas3D::get_camera_phi() const
{
    return m_camera.get_phi();
}

void GLCanvas3D::set_camera_phi(float phi)
{
    m_camera.set_phi(phi);
}

float GLCanvas3D::get_camera_theta() const
{
    return m_camera.get_theta();
}

void GLCanvas3D::set_camera_theta(float theta)
{
    m_camera.set_theta(theta);
}

float GLCanvas3D::get_camera_distance() const
{
    return m_camera.get_distance();
}

void GLCanvas3D::set_camera_distance(float distance)
{
    m_camera.set_distance(distance);
}

const Pointf3& GLCanvas3D::get_camera_target() const
{
    return m_camera.get_target();
}

void GLCanvas3D::set_camera_target(const Pointf3& target)
{
    m_camera.set_target(target);
}

void GLCanvas3D::on_size(wxSizeEvent& evt)
{
    std::cout << "GLCanvas3D::on_size: " << (void*)this << std::endl;

    set_dirty(true);
}

} // namespace GUI
} // namespace Slic3r
