#include "GLGizmoBase.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"





// TODO: Display tooltips quicker on Linux



namespace Slic3r {
namespace GUI {

const float GLGizmoBase::Grabber::SizeFactor = 0.025f;
const float GLGizmoBase::Grabber::MinHalfSize = 1.5f;
const float GLGizmoBase::Grabber::DraggingScaleFactor = 1.25f;

GLGizmoBase::Grabber::Grabber()
    : center(Vec3d::Zero())
    , angles(Vec3d::Zero())
    , dragging(false)
    , enabled(true)
{
    color[0] = 1.0f;
    color[1] = 1.0f;
    color[2] = 1.0f;
}

void GLGizmoBase::Grabber::render(bool hover, float size) const
{
    float render_color[3];
    if (hover)
    {
        render_color[0] = 1.0f - color[0];
        render_color[1] = 1.0f - color[1];
        render_color[2] = 1.0f - color[2];
    }
    else
        ::memcpy((void*)render_color, (const void*)color, 3 * sizeof(float));

    render(size, render_color, true);
}

float GLGizmoBase::Grabber::get_half_size(float size) const
{
    return std::max(size * SizeFactor, MinHalfSize);
}

float GLGizmoBase::Grabber::get_dragging_half_size(float size) const
{
    return std::max(size * SizeFactor * DraggingScaleFactor, MinHalfSize);
}

void GLGizmoBase::Grabber::render(float size, const float* render_color, bool use_lighting) const
{
    float half_size = dragging ? get_dragging_half_size(size) : get_half_size(size);

    if (use_lighting)
        ::glEnable(GL_LIGHTING);

    ::glColor3fv(render_color);

    ::glPushMatrix();
    ::glTranslated(center(0), center(1), center(2));

    ::glRotated(Geometry::rad2deg(angles(2)), 0.0, 0.0, 1.0);
    ::glRotated(Geometry::rad2deg(angles(1)), 0.0, 1.0, 0.0);
    ::glRotated(Geometry::rad2deg(angles(0)), 1.0, 0.0, 0.0);

    // face min x
    ::glPushMatrix();
    ::glTranslatef(-(GLfloat)half_size, 0.0f, 0.0f);
    ::glRotatef(-90.0f, 0.0f, 1.0f, 0.0f);
    render_face(half_size);
    ::glPopMatrix();

    // face max x
    ::glPushMatrix();
    ::glTranslatef((GLfloat)half_size, 0.0f, 0.0f);
    ::glRotatef(90.0f, 0.0f, 1.0f, 0.0f);
    render_face(half_size);
    ::glPopMatrix();

    // face min y
    ::glPushMatrix();
    ::glTranslatef(0.0f, -(GLfloat)half_size, 0.0f);
    ::glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
    render_face(half_size);
    ::glPopMatrix();

    // face max y
    ::glPushMatrix();
    ::glTranslatef(0.0f, (GLfloat)half_size, 0.0f);
    ::glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
    render_face(half_size);
    ::glPopMatrix();

    // face min z
    ::glPushMatrix();
    ::glTranslatef(0.0f, 0.0f, -(GLfloat)half_size);
    ::glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
    render_face(half_size);
    ::glPopMatrix();

    // face max z
    ::glPushMatrix();
    ::glTranslatef(0.0f, 0.0f, (GLfloat)half_size);
    render_face(half_size);
    ::glPopMatrix();

    ::glPopMatrix();

    if (use_lighting)
        ::glDisable(GL_LIGHTING);
}

void GLGizmoBase::Grabber::render_face(float half_size) const
{
    ::glBegin(GL_TRIANGLES);
    ::glNormal3f(0.0f, 0.0f, 1.0f);
    ::glVertex3f(-(GLfloat)half_size, -(GLfloat)half_size, 0.0f);
    ::glVertex3f((GLfloat)half_size, -(GLfloat)half_size, 0.0f);
    ::glVertex3f((GLfloat)half_size, (GLfloat)half_size, 0.0f);
    ::glVertex3f((GLfloat)half_size, (GLfloat)half_size, 0.0f);
    ::glVertex3f(-(GLfloat)half_size, (GLfloat)half_size, 0.0f);
    ::glVertex3f(-(GLfloat)half_size, -(GLfloat)half_size, 0.0f);
    ::glEnd();
}

#if ENABLE_SVG_ICONS
GLGizmoBase::GLGizmoBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
#else
GLGizmoBase::GLGizmoBase(GLCanvas3D& parent, unsigned int sprite_id)
#endif // ENABLE_SVG_ICONS
    : m_parent(parent)
    , m_group_id(-1)
    , m_state(Off)
    , m_shortcut_key(0)
#if ENABLE_SVG_ICONS
    , m_icon_filename(icon_filename)
#endif // ENABLE_SVG_ICONS
    , m_sprite_id(sprite_id)
    , m_hover_id(-1)
    , m_dragging(false)
    , m_imgui(wxGetApp().imgui())
{
    ::memcpy((void*)m_base_color, (const void*)DEFAULT_BASE_COLOR, 3 * sizeof(float));
    ::memcpy((void*)m_drag_color, (const void*)DEFAULT_DRAG_COLOR, 3 * sizeof(float));
    ::memcpy((void*)m_highlight_color, (const void*)DEFAULT_HIGHLIGHT_COLOR, 3 * sizeof(float));
}

void GLGizmoBase::set_hover_id(int id)
{
    if (m_grabbers.empty() || (id < (int)m_grabbers.size()))
    {
        m_hover_id = id;
        on_set_hover_id();
    }
}

void GLGizmoBase::set_highlight_color(const float* color)
{
    if (color != nullptr)
        ::memcpy((void*)m_highlight_color, (const void*)color, 3 * sizeof(float));
}

void GLGizmoBase::enable_grabber(unsigned int id)
{
    if ((0 <= id) && (id < (unsigned int)m_grabbers.size()))
        m_grabbers[id].enabled = true;

    on_enable_grabber(id);
}

void GLGizmoBase::disable_grabber(unsigned int id)
{
    if ((0 <= id) && (id < (unsigned int)m_grabbers.size()))
        m_grabbers[id].enabled = false;

    on_disable_grabber(id);
}

void GLGizmoBase::start_dragging(const Selection& selection)
{
    m_dragging = true;

    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        m_grabbers[i].dragging = (m_hover_id == i);
    }

    on_start_dragging(selection);
}

void GLGizmoBase::stop_dragging()
{
    m_dragging = false;

    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        m_grabbers[i].dragging = false;
    }

    on_stop_dragging();
}

void GLGizmoBase::update(const UpdateData& data, const Selection& selection)
{
    if (m_hover_id != -1)
        on_update(data, selection);
}

std::array<float, 3> GLGizmoBase::picking_color_component(unsigned int id) const
{
    static const float INV_255 = 1.0f / 255.0f;

    id = BASE_ID - id;

    if (m_group_id > -1)
        id -= m_group_id;

    // color components are encoded to match the calculation of volume_id made into GLCanvas3D::_picking_pass()
    return std::array<float, 3> { (float)((id >> 0) & 0xff) * INV_255, // red
                                  (float)((id >> 8) & 0xff) * INV_255, // green
                                  (float)((id >> 16) & 0xff) * INV_255 }; // blue
}

void GLGizmoBase::render_grabbers(const BoundingBoxf3& box) const
{
    float size = (float)box.max_size();

    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        if (m_grabbers[i].enabled)
            m_grabbers[i].render((m_hover_id == i), size);
    }
}

void GLGizmoBase::render_grabbers(float size) const
{
    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        if (m_grabbers[i].enabled)
            m_grabbers[i].render((m_hover_id == i), size);
    }
}

void GLGizmoBase::render_grabbers_for_picking(const BoundingBoxf3& box) const
{
    float size = (float)box.max_size();

    for (unsigned int i = 0; i < (unsigned int)m_grabbers.size(); ++i)
    {
        if (m_grabbers[i].enabled)
        {
            std::array<float, 3> color = picking_color_component(i);
            m_grabbers[i].color[0] = color[0];
            m_grabbers[i].color[1] = color[1];
            m_grabbers[i].color[2] = color[2];
            m_grabbers[i].render_for_picking(size);
        }
    }
}


void GLGizmoBase::set_tooltip(const std::string& tooltip) const
{
    m_parent.set_tooltip(tooltip);
}

std::string GLGizmoBase::format(float value, unsigned int decimals) const
{
    return Slic3r::string_printf("%.*f", decimals, value);
}

} // namespace GUI
} // namespace Slic3r
