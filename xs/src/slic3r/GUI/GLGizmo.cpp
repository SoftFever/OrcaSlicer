#include "GLGizmo.hpp"

#include "../../libslic3r/Utils.hpp"

#include <iostream>

namespace Slic3r {
namespace GUI {

GLGizmoBase::GLGizmoBase()
    : m_state(Off)
{
}

GLGizmoBase::~GLGizmoBase()
{
}

GLGizmoBase::EState GLGizmoBase::get_state() const
{
    return m_state;
}

void GLGizmoBase::set_state(GLGizmoBase::EState state)
{
    m_state = state;
}

unsigned int GLGizmoBase::get_textures_id() const
{
    return m_textures[m_state].get_id();
}

int GLGizmoBase::get_textures_size() const
{
    return m_textures[Off].get_width();
}

bool GLGizmoBase::init()
{
    return on_init();
}

GLGizmoRotate::GLGizmoRotate()
    : GLGizmoBase()
    , m_angle_x(0.0f)
    , m_angle_y(0.0f)
    , m_angle_z(0.0f)
{
}

void GLGizmoRotate::render() const
{
    std::cout << "GLGizmoRotate::render()" << std::endl;
}

bool GLGizmoRotate::on_init()
{
    std::string path = resources_dir() + "/icons/overlay/";

    std::string filename = path + "rotate_off.png";
    if (!m_textures[Off].load_from_file(filename))
        return false;

    filename = path + "rotate_hover.png";
    if (!m_textures[Hover].load_from_file(filename))
        return false;

    filename = path + "rotate_on.png";
    if (!m_textures[On].load_from_file(filename))
        return false;

    return true;
}

GLGizmoScale::GLGizmoScale()
    : GLGizmoBase()
    , m_scale_x(1.0f)
    , m_scale_y(1.0f)
    , m_scale_z(1.0f)
{
}

void GLGizmoScale::render() const
{
    std::cout << "GLGizmoScale::render()" << std::endl;
}

bool GLGizmoScale::on_init()
{
    std::string path = resources_dir() + "/icons/overlay/";

    std::string filename = path + "scale_off.png";
    if (!m_textures[Off].load_from_file(filename))
        return false;

    filename = path + "scale_hover.png";
    if (!m_textures[Hover].load_from_file(filename))
        return false;

    filename = path + "scale_on.png";
    if (!m_textures[On].load_from_file(filename))
        return false;

    return true;
}

} // namespace GUI
} // namespace Slic3r
