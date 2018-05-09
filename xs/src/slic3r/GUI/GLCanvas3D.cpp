#include "GLCanvas3D.hpp"

#include <wx/glcanvas.h>

#include <iostream>

namespace Slic3r {
namespace GUI {

GLCanvas3D::Camera::Camera()
    : type(CT_Ortho)
    , zoom(1.0f)
    , phi(45.0f)
    , theta(45.0f)
    , distance(0.0f)
    , target(0.0, 0.0, 0.0)

{
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
    return m_camera.type;
}

void GLCanvas3D::set_camera_type(GLCanvas3D::Camera::EType type)
{
    m_camera.type = type;
}

float GLCanvas3D::get_camera_zoom() const
{
    return m_camera.zoom;
}

void GLCanvas3D::set_camera_zoom(float zoom)
{
    m_camera.zoom = zoom;
}

float GLCanvas3D::get_camera_phi() const
{
    return m_camera.phi;
}

void GLCanvas3D::set_camera_phi(float phi)
{
    m_camera.phi = phi;
}

float GLCanvas3D::get_camera_theta() const
{
    return m_camera.theta;
}

void GLCanvas3D::set_camera_theta(float theta)
{
    m_camera.theta = theta;
}

float GLCanvas3D::get_camera_distance() const
{
    return m_camera.distance;
}

void GLCanvas3D::set_camera_distance(float distance)
{
    m_camera.distance = distance;
}

const Pointf3& GLCanvas3D::get_camera_target() const
{
    return m_camera.target;
}

void GLCanvas3D::set_camera_target(const Pointf3& target)
{
    m_camera.target = target;
}

void GLCanvas3D::on_size(wxSizeEvent& evt)
{
    std::cout << "GLCanvas3D::on_size: " << (void*)this << std::endl;

    set_dirty(true);
}

} // namespace GUI
} // namespace Slic3r
