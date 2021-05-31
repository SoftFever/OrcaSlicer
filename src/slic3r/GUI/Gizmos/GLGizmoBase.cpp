#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"

// TODO: Display tooltips quicker on Linux

namespace Slic3r {
namespace GUI {

const float GLGizmoBase::Grabber::SizeFactor = 0.05f;
const float GLGizmoBase::Grabber::MinHalfSize = 1.5f;
const float GLGizmoBase::Grabber::DraggingScaleFactor = 1.25f;

GLGizmoBase::Grabber::Grabber()
    : center(Vec3d::Zero())
    , angles(Vec3d::Zero())
    , dragging(false)
    , enabled(true)
{
    color = { 1.0f, 1.0f, 1.0f, 1.0f };
}

void GLGizmoBase::Grabber::render(bool hover, float size) const
{
    std::array<float, 4> render_color;
    if (hover) {
        render_color[0] = (1.0f - color[0]);
        render_color[1] = (1.0f - color[1]);
        render_color[2] = (1.0f - color[2]);
        render_color[3] = color[3];
    }
    else
        render_color = color;

    render(size, render_color, false);
}

float GLGizmoBase::Grabber::get_half_size(float size) const
{
    return std::max(size * SizeFactor, MinHalfSize);
}

float GLGizmoBase::Grabber::get_dragging_half_size(float size) const
{
    return get_half_size(size) * DraggingScaleFactor;
}

void GLGizmoBase::Grabber::render(float size, const std::array<float, 4>& render_color, bool picking) const
{
    if (! cube_initialized) {
        // This cannot be done in constructor, OpenGL is not yet
        // initialized at that point (on Linux at least).
        TriangleMesh mesh = make_cube(1., 1., 1.);
        mesh.translate(Vec3f(-0.5, -0.5, -0.5));
        const_cast<GLModel&>(cube).init_from(mesh);
        const_cast<bool&>(cube_initialized) = true;
    }

    float fullsize = 2 * (dragging ? get_dragging_half_size(size) : get_half_size(size));

    GLShaderProgram* shader = picking ? nullptr : wxGetApp().get_current_shader();
    if (shader != nullptr)
#if ENABLE_SEQUENTIAL_LIMITS
        const_cast<GLModel*>(&cube)->set_color(-1, render_color);
#else
        shader->set_uniform("uniform_color", render_color);
#endif // ENABLE_SEQUENTIAL_LIMITS
    else
        glsafe(::glColor4fv(render_color.data())); // picking

    glsafe(::glPushMatrix());
    glsafe(::glTranslated(center.x(), center.y(), center.z()));
    glsafe(::glRotated(Geometry::rad2deg(angles.z()), 0.0, 0.0, 1.0));
    glsafe(::glRotated(Geometry::rad2deg(angles.y()), 0.0, 1.0, 0.0));
    glsafe(::glRotated(Geometry::rad2deg(angles.x()), 1.0, 0.0, 0.0));
    glsafe(::glScaled(fullsize, fullsize, fullsize));
    cube.render();
    glsafe(::glPopMatrix());
}


GLGizmoBase::GLGizmoBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : m_parent(parent)
    , m_group_id(-1)
    , m_state(Off)
    , m_shortcut_key(0)
    , m_icon_filename(icon_filename)
    , m_sprite_id(sprite_id)
    , m_hover_id(-1)
    , m_dragging(false)
    , m_imgui(wxGetApp().imgui())
    , m_first_input_window_render(true)
{
    m_base_color = DEFAULT_BASE_COLOR;
    m_drag_color = DEFAULT_DRAG_COLOR;
    m_highlight_color = DEFAULT_HIGHLIGHT_COLOR;
    m_cone.init_from(make_cone(1., 1., 2 * PI / 24));
    m_sphere.init_from(make_sphere(1., (2 * M_PI) / 24.));
    m_cylinder.init_from(make_cylinder(1., 1., 2 * PI / 24.));
}

void GLGizmoBase::set_hover_id(int id)
{
    if (m_grabbers.empty() || (id < (int)m_grabbers.size()))
    {
        m_hover_id = id;
        on_set_hover_id();
    }
}

void GLGizmoBase::set_highlight_color(const std::array<float, 4>& color)
{
    m_highlight_color = color;
}

void GLGizmoBase::enable_grabber(unsigned int id)
{
    if (id < m_grabbers.size())
        m_grabbers[id].enabled = true;

    on_enable_grabber(id);
}

void GLGizmoBase::disable_grabber(unsigned int id)
{
    if (id < m_grabbers.size())
        m_grabbers[id].enabled = false;

    on_disable_grabber(id);
}

void GLGizmoBase::start_dragging()
{
    m_dragging = true;

    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        m_grabbers[i].dragging = (m_hover_id == i);
    }

    on_start_dragging();
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

void GLGizmoBase::update(const UpdateData& data)
{
    if (m_hover_id != -1)
        on_update(data);
}

std::array<float, 4> GLGizmoBase::picking_color_component(unsigned int id) const
{
    static const float INV_255 = 1.0f / 255.0f;

    id = BASE_ID - id;

    if (m_group_id > -1)
        id -= m_group_id;

    // color components are encoded to match the calculation of volume_id made into GLCanvas3D::_picking_pass()
    return std::array<float, 4> { 
		float((id >> 0) & 0xff) * INV_255, // red
		float((id >> 8) & 0xff) * INV_255, // green
		float((id >> 16) & 0xff) * INV_255, // blue
		float(picking_checksum_alpha_channel(id & 0xff, (id >> 8) & 0xff, (id >> 16) & 0xff))* INV_255 // checksum for validating against unwanted alpha blending and multi sampling
	};
}

void GLGizmoBase::render_grabbers(const BoundingBoxf3& box) const
{
    render_grabbers((float)((box.size().x() + box.size().y() + box.size().z()) / 3.0));
}

void GLGizmoBase::render_grabbers(float size) const
{
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;
    shader->start_using();
    shader->set_uniform("emission_factor", 0.1);
    for (int i = 0; i < (int)m_grabbers.size(); ++i) {
        if (m_grabbers[i].enabled)
            m_grabbers[i].render(m_hover_id == i, size);
    }
    shader->stop_using();
}

void GLGizmoBase::render_grabbers_for_picking(const BoundingBoxf3& box) const
{
    float mean_size = (float)((box.size().x() + box.size().y() + box.size().z()) / 3.0);

    for (unsigned int i = 0; i < (unsigned int)m_grabbers.size(); ++i) {
        if (m_grabbers[i].enabled) {
            std::array<float, 4> color = picking_color_component(i);
            m_grabbers[i].color = color;
            m_grabbers[i].render_for_picking(mean_size);
        }
    }
}

std::string GLGizmoBase::format(float value, unsigned int decimals) const
{
    return Slic3r::string_printf("%.*f", decimals, value);
}

void GLGizmoBase::render_input_window(float x, float y, float bottom_limit)
{
    on_render_input_window(x, y, bottom_limit);
    if (m_first_input_window_render) {
        // for some reason, the imgui dialogs are not shown on screen in the 1st frame where they are rendered, but show up only with the 2nd rendered frame
        // so, we forces another frame rendering the first time the imgui window is shown
        m_parent.set_as_dirty();
        m_first_input_window_render = false;
    }
}

// Produce an alpha channel checksum for the red green blue components. The alpha channel may then be used to verify, whether the rgb components
// were not interpolated by alpha blending or multi sampling.
unsigned char picking_checksum_alpha_channel(unsigned char red, unsigned char green, unsigned char blue)
{
	// 8 bit hash for the color
	unsigned char b = ((((37 * red) + green) & 0x0ff) * 37 + blue) & 0x0ff;
	// Increase enthropy by a bit reversal
	b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
	b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
	b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
	// Flip every second bit to increase the enthropy even more.
	b ^= 0x55;
	return b;
}


} // namespace GUI
} // namespace Slic3r
