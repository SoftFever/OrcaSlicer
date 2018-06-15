#include "GLGizmo.hpp"

#include "../../libslic3r/Utils.hpp"
#include "../../libslic3r/BoundingBox.hpp"

#include <GL/glew.h>

#include <iostream>

namespace Slic3r {
namespace GUI {

const float GLGizmoBase::Grabber::HalfSize = 2.0f;
const float GLGizmoBase::Grabber::HoverOffset = 0.5f;
const float GLGizmoBase::BaseColor[3] = { 1.0f, 1.0f, 1.0f };
const float GLGizmoBase::HighlightColor[3] = { 1.0f, 0.38f, 0.0f };

GLGizmoBase::Grabber::Grabber()
    : center(Pointf(0.0, 0.0))
{
    color[0] = 1.0f;
    color[1] = 1.0f;
    color[2] = 1.0f;
}

void GLGizmoBase::Grabber::render(bool hover) const
{
    float min_x = (float)center.x - HalfSize;
    float max_x = (float)center.x + HalfSize;
    float min_y = (float)center.y - HalfSize;
    float max_y = (float)center.y + HalfSize;

    ::glColor3f((GLfloat)color[0], (GLfloat)color[1], (GLfloat)color[2]);

    ::glDisable(GL_CULL_FACE);
    ::glBegin(GL_TRIANGLES);
    ::glVertex3f((GLfloat)min_x, (GLfloat)min_y, 0.0f);
    ::glVertex3f((GLfloat)max_x, (GLfloat)min_y, 0.0f);
    ::glVertex3f((GLfloat)max_x, (GLfloat)max_y, 0.0f);
    ::glVertex3f((GLfloat)max_x, (GLfloat)max_y, 0.0f);
    ::glVertex3f((GLfloat)min_x, (GLfloat)max_y, 0.0f);
    ::glVertex3f((GLfloat)min_x, (GLfloat)min_y, 0.0f);
    ::glEnd();
    ::glEnable(GL_CULL_FACE);

    if (hover)
    {
        min_x -= HoverOffset;
        max_x += HoverOffset;
        min_y -= HoverOffset;
        max_y += HoverOffset;

        ::glBegin(GL_LINE_LOOP);
        ::glVertex3f((GLfloat)min_x, (GLfloat)min_y, 0.0f);
        ::glVertex3f((GLfloat)max_x, (GLfloat)min_y, 0.0f);
        ::glVertex3f((GLfloat)max_x, (GLfloat)max_y, 0.0f);
        ::glVertex3f((GLfloat)min_x, (GLfloat)max_y, 0.0f);
        ::glEnd();
    }
}

GLGizmoBase::GLGizmoBase()
    : m_state(Off)
    , m_hover_id(-1)
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

int GLGizmoBase::get_hover_id() const
{
    return m_hover_id;
}

void GLGizmoBase::set_hover_id(int id)
{
    if (id < (int)m_grabbers.size())
        m_hover_id = id;
}

void GLGizmoBase::start_dragging()
{
    if (m_hover_id != -1)
        m_start_drag_position = m_grabbers[m_hover_id].center;
}

void GLGizmoBase::update(const Pointf& mouse_pos)
{
    if (m_hover_id != -1)
        on_update(mouse_pos);
}

void GLGizmoBase::render(const BoundingBoxf3& box) const
{
    on_render(box);
}

void GLGizmoBase::render_for_picking(const BoundingBoxf3& box) const
{
    on_render_for_picking(box);
}

void GLGizmoBase::render_grabbers() const
{
    for (unsigned int i = 0; i < (unsigned int)m_grabbers.size(); ++i)
    {
        m_grabbers[i].render(m_hover_id == i);
    }
}

const float GLGizmoRotate::Offset = 5.0f;
const unsigned int GLGizmoRotate::CircleResolution = 64;
const unsigned int GLGizmoRotate::ScaleStepsCount = 60;
const float GLGizmoRotate::ScaleStepRad = 2.0f * (float)PI / GLGizmoRotate::ScaleStepsCount;
const unsigned int GLGizmoRotate::ScaleLongEvery = 5;
const float GLGizmoRotate::ScaleLongTooth = 2.0f;
const float GLGizmoRotate::ScaleShortTooth = 1.0f;
const unsigned int GLGizmoRotate::SnapRegionsCount = 8;
const float GLGizmoRotate::GrabberOffset = 5.0f;

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
    if (!m_textures[Off].load_from_file(filename, false))
        return false;

    filename = path + "rotate_hover.png";
    if (!m_textures[Hover].load_from_file(filename, false))
        return false;

    filename = path + "rotate_on.png";
    if (!m_textures[On].load_from_file(filename, false))
        return false;

    m_grabbers.push_back(Grabber());

    return true;
}

void GLGizmoRotate::on_update(const Pointf& mouse_pos)
{
//    std::cout << "GLGizmoRotate::on_update() - delta (" << delta.x << ", " << delta.y << ")" << std::endl;
}

void GLGizmoRotate::on_render(const BoundingBoxf3& box) const
{
    ::glDisable(GL_LIGHTING);
    ::glDisable(GL_DEPTH_TEST);

    const Pointf3& size = box.size();
    const Pointf3& center = box.center();

    float radius = Offset + ::sqrt(sqr(0.5f * size.x) + sqr(0.5f * size.y));

    ::glLineWidth(2.0f);
    ::glColor3fv(BaseColor);

    _render_circle(center, radius);
    _render_scale(center, radius);
    _render_snap_radii(center, radius);
    _render_reference_radius(center, radius);
    _render_grabber(center, radius);
}

void GLGizmoRotate::on_render_for_picking(const BoundingBoxf3& box) const
{
    ::glDisable(GL_LIGHTING);
    ::glDisable(GL_DEPTH_TEST);

    m_grabbers[0].color[0] = 1.0f;
    m_grabbers[0].color[1] = 1.0f;
    m_grabbers[0].color[2] = 254.0f / 255.0f;
    render_grabbers();
}

void GLGizmoRotate::_render_circle(const Pointf3& center, float radius) const
{
    ::glBegin(GL_LINE_LOOP);
    for (unsigned int i = 0; i < ScaleStepsCount; ++i)
    {
        float angle = (float)i * ScaleStepRad;
        float x = center.x + ::cos(angle) * radius;
        float y = center.y + ::sin(angle) * radius;
        ::glVertex3f((GLfloat)x, (GLfloat)y, 0.0f);
    }
    ::glEnd();
}

void GLGizmoRotate::_render_scale(const Pointf3& center, float radius) const
{
    float out_radius_long = radius + ScaleLongTooth;
    float out_radius_short = radius + ScaleShortTooth;

    ::glBegin(GL_LINES);
    for (unsigned int i = 0; i < ScaleStepsCount; ++i)
    {
        float angle = (float)i * ScaleStepRad;
        float cosa = ::cos(angle);
        float sina = ::sin(angle);
        float in_x = center.x + cosa * radius;
        float in_y = center.y + sina * radius;
        float out_x = (i % ScaleLongEvery == 0) ? center.x + cosa * out_radius_long : center.x + cosa * out_radius_short;
        float out_y = (i % ScaleLongEvery == 0) ? center.y + sina * out_radius_long : center.y + sina * out_radius_short;
        ::glVertex3f((GLfloat)in_x, (GLfloat)in_y, 0.0f);
        ::glVertex3f((GLfloat)out_x, (GLfloat)out_y, 0.0f);
    }
    ::glEnd();
}

void GLGizmoRotate::_render_snap_radii(const Pointf3& center, float radius) const
{
    float step_deg = 2.0f * (float)PI / (float)SnapRegionsCount;

    float in_radius = radius / 3.0f;
    float out_radius = 2.0f * in_radius;

    ::glBegin(GL_LINES);
    for (unsigned int i = 0; i < SnapRegionsCount; ++i)
    {
        float angle = (float)i * step_deg;
        float cosa = ::cos(angle);
        float sina = ::sin(angle);
        float in_x = center.x + cosa * in_radius;
        float in_y = center.y + sina * in_radius;
        float out_x = center.x + cosa * out_radius;
        float out_y = center.y + sina * out_radius;
        ::glVertex3f((GLfloat)in_x, (GLfloat)in_y, 0.0f);
        ::glVertex3f((GLfloat)out_x, (GLfloat)out_y, 0.0f);
    }
    ::glEnd();
}

void GLGizmoRotate::_render_reference_radius(const Pointf3& center, float radius) const
{
    ::glBegin(GL_LINES);
    ::glVertex3f((GLfloat)center.x, (GLfloat)center.y, 0.0f);
    ::glVertex3f((GLfloat)center.x + radius, (GLfloat)center.y, 0.0f);
    ::glEnd();
}

void GLGizmoRotate::_render_grabber(const Pointf3& center, float radius) const
{
    float grabber_radius = radius + GrabberOffset;
    m_grabbers[0].center.x = center.x + ::cos(m_angle_z) * grabber_radius;
    m_grabbers[0].center.y = center.y + ::sin(m_angle_z) * grabber_radius;

    ::glBegin(GL_LINES);
    ::glVertex3f((GLfloat)center.x, (GLfloat)center.y, 0.0f);
    ::glVertex3f((GLfloat)m_grabbers[0].center.x, (GLfloat)m_grabbers[0].center.y, 0.0f);
    ::glEnd();

    ::memcpy((void*)m_grabbers[0].color, (const void*)HighlightColor, 4 * sizeof(float));
    render_grabbers();
}

const float GLGizmoScale::Offset = 5.0f;

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
    if (!m_textures[Off].load_from_file(filename, false))
        return false;

    filename = path + "scale_hover.png";
    if (!m_textures[Hover].load_from_file(filename, false))
        return false;

    filename = path + "scale_on.png";
    if (!m_textures[On].load_from_file(filename, false))
        return false;

    for (unsigned int i = 0; i < 4; ++i)
    {
        m_grabbers.push_back(Grabber());
    }

    return true;
}

void GLGizmoScale::on_update(const Pointf& mouse_pos)
{
    Pointf center(0.5 * (m_grabbers[1].center.x + m_grabbers[0].center.x), 0.5 * (m_grabbers[3].center.y + m_grabbers[0].center.y));

    coordf_t orig_len = length(m_start_drag_position - center);
    coordf_t new_len = length(mouse_pos - center);

    coordf_t ratio = (orig_len != 0.0) ? new_len / orig_len : 1.0;

    m_scale_x = (float)ratio;
    m_scale_y = (float)ratio;
    m_scale_z = (float)ratio;
}

void GLGizmoScale::on_render(const BoundingBoxf3& box) const
{
    ::glDisable(GL_LIGHTING);
    ::glDisable(GL_DEPTH_TEST);

    const Pointf3& size = box.size();
    const Pointf3& center = box.center();

    Pointf half_scaled_size = 0.5 * Pointf((coordf_t)m_scale_x * size.x, (coordf_t)m_scale_y * size.y);
    coordf_t min_x = center.x - half_scaled_size.x - (coordf_t)Offset;
    coordf_t max_x = center.x + half_scaled_size.x + (coordf_t)Offset;
    coordf_t min_y = center.y - half_scaled_size.y - (coordf_t)Offset;
    coordf_t max_y = center.y + half_scaled_size.y + (coordf_t)Offset;

    m_grabbers[0].center.x = min_x;
    m_grabbers[0].center.y = min_y;
    m_grabbers[1].center.x = max_x;
    m_grabbers[1].center.y = min_y;
    m_grabbers[2].center.x = max_x;
    m_grabbers[2].center.y = max_y;
    m_grabbers[3].center.x = min_x;
    m_grabbers[3].center.y = max_y;

    ::glLineWidth(2.0f);
    ::glColor3fv(BaseColor);
    // draw outline
    ::glBegin(GL_LINE_LOOP);
    for (unsigned int i = 0; i < 4; ++i)
    {
        ::glVertex3f((GLfloat)m_grabbers[i].center.x, (GLfloat)m_grabbers[i].center.y, 0.0f);
    }
    ::glEnd();

    // draw grabbers
    for (unsigned int i = 0; i < 4; ++i)
    {
        ::memcpy((void*)m_grabbers[i].color, (const void*)HighlightColor, 4 * sizeof(float));
    }
    render_grabbers();
}

void GLGizmoScale::on_render_for_picking(const BoundingBoxf3& box) const
{
    static const GLfloat INV_255 = 1.0f / 255.0f;

    ::glDisable(GL_LIGHTING);
    ::glDisable(GL_DEPTH_TEST);

    for (unsigned int i = 0; i < 4; ++i)
    {
        m_grabbers[i].color[0] = 1.0f;
        m_grabbers[i].color[1] = 1.0f;
        m_grabbers[i].color[2] = (254.0f - (float)i) * INV_255;
    }
    render_grabbers();
}

} // namespace GUI
} // namespace Slic3r
