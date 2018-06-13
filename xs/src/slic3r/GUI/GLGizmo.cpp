#include "GLGizmo.hpp"

#include "../../libslic3r/Utils.hpp"
#include "../../libslic3r/BoundingBox.hpp"

#include <GL/glew.h>

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

bool GLGizmoBase::init()
{
    return on_init();
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

void GLGizmoBase::render(const BoundingBoxf3& box) const
{
    on_render(box);
}

GLGizmoRotate::GLGizmoRotate()
    : GLGizmoBase()
    , m_angle_x(0.0f)
    , m_angle_y(0.0f)
    , m_angle_z(0.0f)
{
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

void GLGizmoRotate::on_render(const BoundingBoxf3& box) const
{
    std::cout << "GLGizmoRotate::render()" << std::endl;
}

const float GLGizmoScale::Offset = 5.0f;
const float GLGizmoScale::SquareHalfSize = 2.0f;

GLGizmoScale::GLGizmoScale()
    : GLGizmoBase()
    , m_scale_x(1.0f)
    , m_scale_y(1.0f)
    , m_scale_z(1.0f)
{
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

void GLGizmoScale::on_render(const BoundingBoxf3& box) const
{
    ::glDisable(GL_LIGHTING);
    ::glDisable(GL_DEPTH_TEST);

    const Pointf3& size = box.size();
    const Pointf3& center = box.center();

    Pointf3 half_scaled_size = 0.5 * Pointf3((coordf_t)m_scale_x * size.x, (coordf_t)m_scale_y * size.y, (coordf_t)m_scale_z * size.z);
    coordf_t min_x = center.x - half_scaled_size.x - (coordf_t)Offset;
    coordf_t max_x = center.x + half_scaled_size.x + (coordf_t)Offset;
    coordf_t min_y = center.y - half_scaled_size.y - (coordf_t)Offset;
    coordf_t max_y = center.y + half_scaled_size.y + (coordf_t)Offset;

    ::glLineWidth(2.0f);
    ::glBegin(GL_LINE_LOOP);
    // draw outline
    ::glColor3f(1.0f, 1.0f, 1.0f);
    ::glVertex3f((GLfloat)min_x, (GLfloat)min_y, 0.0f);
    ::glVertex3f((GLfloat)max_x, (GLfloat)min_y, 0.0f);
    ::glVertex3f((GLfloat)max_x, (GLfloat)max_y, 0.0f);
    ::glVertex3f((GLfloat)min_x, (GLfloat)max_y, 0.0f);
    ::glEnd();

    // draw grabbers
    ::glColor3f(1.0f, 0.38f, 0.0f);
    ::glDisable(GL_CULL_FACE);
    _render_square(Pointf3(min_x, min_y, 0.0));
    _render_square(Pointf3(max_x, min_y, 0.0));
    _render_square(Pointf3(max_x, max_y, 0.0));
    _render_square(Pointf3(min_x, max_y, 0.0));
    ::glEnable(GL_CULL_FACE);
}

void GLGizmoScale::_render_square(const Pointf3& center) const
{
    float min_x = (float)center.x - SquareHalfSize;
    float max_x = (float)center.x + SquareHalfSize;
    float min_y = (float)center.y - SquareHalfSize;
    float max_y = (float)center.y + SquareHalfSize;

    ::glBegin(GL_TRIANGLES);
    ::glVertex3f((GLfloat)min_x, (GLfloat)min_y, 0.0f);
    ::glVertex3f((GLfloat)max_x, (GLfloat)min_y, 0.0f);
    ::glVertex3f((GLfloat)max_x, (GLfloat)max_y, 0.0f);
    ::glVertex3f((GLfloat)max_x, (GLfloat)max_y, 0.0f);
    ::glVertex3f((GLfloat)min_x, (GLfloat)max_y, 0.0f);
    ::glVertex3f((GLfloat)min_x, (GLfloat)min_y, 0.0f);
    ::glEnd();
}

} // namespace GUI
} // namespace Slic3r
