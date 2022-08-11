// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoMove.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
//BBS: GUI refactor
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/AppConfig.hpp"


#include <GL/glew.h>

#include <wx/utils.h>

namespace Slic3r {
namespace GUI {

#if ENABLE_FIXED_GRABBER
const double GLGizmoMove3D::Offset = 50.0;
#else
const double GLGizmoMove3D::Offset = 10.0;
#endif

//BBS: GUI refactor: add obj manipulation
GLGizmoMove3D::GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_displacement(Vec3d::Zero())
    , m_snap_step(1.0)
    , m_starting_drag_position(Vec3d::Zero())
    , m_starting_box_center(Vec3d::Zero())
    , m_starting_box_bottom_center(Vec3d::Zero())
    //BBS: GUI refactor: add obj manipulation
    , m_object_manipulation(obj_manipulation)
{
    m_vbo_cone.init_from(its_make_cone(1., 1., 2*PI/36));
}

std::string GLGizmoMove3D::get_tooltip() const
{
    const Selection& selection = m_parent.get_selection();
    bool show_position = selection.is_single_full_instance();
    const Vec3d& position = selection.get_bounding_box().center();

    if (m_hover_id == 0 || m_grabbers[0].dragging)
        return "X: " + format(show_position ? position(0) : m_displacement(0), 2);
    else if (m_hover_id == 1 || m_grabbers[1].dragging)
        return "Y: " + format(show_position ? position(1) : m_displacement(1), 2);
    else if (m_hover_id == 2 || m_grabbers[2].dragging)
        return "Z: " + format(show_position ? position(2) : m_displacement(2), 2);
    else
        return "";
}

bool GLGizmoMove3D::on_init()
{
    for (int i = 0; i < 3; ++i) {
        m_grabbers.push_back(Grabber());
    }

    m_shortcut_key = WXK_CONTROL_M;

    return true;
}

std::string GLGizmoMove3D::on_get_name() const
{
    return _u8L("Move");
}

bool GLGizmoMove3D::on_is_activable() const
{
    return !m_parent.get_selection().is_empty();
}

void GLGizmoMove3D::on_start_dragging()
{
    if (m_hover_id != -1) {
        m_displacement = Vec3d::Zero();
        const BoundingBoxf3& box = m_parent.get_selection().get_bounding_box();
        m_starting_drag_position = m_grabbers[m_hover_id].center;
        m_starting_box_center = box.center();
        m_starting_box_bottom_center = box.center();
        m_starting_box_bottom_center(2) = box.min(2);
    }
}

void GLGizmoMove3D::on_stop_dragging()
{
    m_displacement = Vec3d::Zero();
}

void GLGizmoMove3D::on_update(const UpdateData& data)
{
    if (m_hover_id == 0)
        m_displacement.x() = calc_projection(data);
    else if (m_hover_id == 1)
        m_displacement.y() = calc_projection(data);
    else if (m_hover_id == 2)
        m_displacement.z() = calc_projection(data);
}

void GLGizmoMove3D::on_render()
{
    const Selection& selection = m_parent.get_selection();

    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));

    const BoundingBoxf3& box = selection.get_bounding_box();
    const Vec3d& center = box.center();
    float space_size = 20.f *INV_ZOOM;

#if ENABLE_FIXED_GRABBER
    // x axis
    m_grabbers[0].center = { box.max.x() + space_size, center.y(), center.z() };
    // y axis
    m_grabbers[1].center = { center.x(), box.max.y() + space_size, center.z() };
    // z axis
    m_grabbers[2].center = { center.x(), center.y(), box.max.z() + space_size };

    for (int i = 0; i < 3; ++i) {
        m_grabbers[i].color       = AXES_COLOR[i];
        m_grabbers[i].hover_color = AXES_HOVER_COLOR[i];
    }
#else
    // x axis
    m_grabbers[0].center = { box.max.x() + Offset, center.y(), center.z() };
    m_grabbers[0].color = AXES_COLOR[0];

    // y axis
    m_grabbers[1].center = { center.x(), box.max.y() + Offset, center.z() };
    m_grabbers[1].color = AXES_COLOR[1];

    // z axis
    m_grabbers[2].center = { center.x(), center.y(), box.max.z() + Offset };
    m_grabbers[2].color = AXES_COLOR[2];
#endif

    glsafe(::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f));

    // draw grabbers
    for (unsigned int i = 0; i < 3; ++i) {
        if (m_grabbers[i].enabled) render_grabber_extension((Axis) i, box, false);
    }

    // draw axes line
    // draw axes
    for (unsigned int i = 0; i < 3; ++i) {
        if (m_grabbers[i].enabled) {
            glsafe(::glColor4fv(AXES_COLOR[i].data()));
            glLineStipple(1, 0x0FFF);
            glEnable(GL_LINE_STIPPLE);
            ::glBegin(GL_LINES);
            ::glVertex3dv(center.data());
            // use extension center
            ::glVertex3dv(m_grabbers[i].center.data());
            glsafe(::glEnd());
            glDisable(GL_LINE_STIPPLE);
        }
    }
}

void GLGizmoMove3D::on_render_for_picking()
{
    glsafe(::glDisable(GL_DEPTH_TEST));

    const BoundingBoxf3& box = m_parent.get_selection().get_bounding_box();
    //BBS donot render base grabber for picking
    //render_grabbers_for_picking(box);

    //get picking colors only
    for (unsigned int i = 0; i < (unsigned int) m_grabbers.size(); ++i) {
        if (m_grabbers[i].enabled) {
            std::array<float, 4> color = picking_color_component(i);
            m_grabbers[i].color        = color;
        }
    }

    render_grabber_extension(X, box, true);
    render_grabber_extension(Y, box, true);
    render_grabber_extension(Z, box, true);
}

//BBS: add input window for move
void GLGizmoMove3D::on_render_input_window(float x, float y, float bottom_limit)
{
    if (m_object_manipulation)
        m_object_manipulation->do_render_move_window(m_imgui, "Move", x, y, bottom_limit);
}


double GLGizmoMove3D::calc_projection(const UpdateData& data) const
{
    double projection = 0.0;

    Vec3d starting_vec = m_starting_drag_position - m_starting_box_center;
    double len_starting_vec = starting_vec.norm();
    if (len_starting_vec != 0.0) {
        Vec3d mouse_dir = data.mouse_ray.unit_vector();
        // finds the intersection of the mouse ray with the plane parallel to the camera viewport and passing throught the starting position
        // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection algebric form
        // in our case plane normal and ray direction are the same (orthogonal view)
        // when moving to perspective camera the negative z unit axis of the camera needs to be transformed in world space and used as plane normal
        Vec3d inters = data.mouse_ray.a + (m_starting_drag_position - data.mouse_ray.a).dot(mouse_dir) / mouse_dir.squaredNorm() * mouse_dir;
        // vector from the starting position to the found intersection
        Vec3d inters_vec = inters - m_starting_drag_position;

        // finds projection of the vector along the staring direction
        projection = inters_vec.dot(starting_vec.normalized());
    }

    if (wxGetKeyState(WXK_SHIFT))
        projection = m_snap_step * (double)std::round(projection / m_snap_step);

    return projection;
}

void GLGizmoMove3D::render_grabber_extension(Axis axis, const BoundingBoxf3& box, bool picking) const
{
#if ENABLE_FIXED_GRABBER
    float mean_size = (float)(GLGizmoBase::Grabber::FixedGrabberSize);
#else
    float mean_size = (float)((box.size().x() + box.size().y() + box.size().z()) / 3.0);
#endif

    double size = 0.75 * GLGizmoBase::Grabber::FixedGrabberSize * GLGizmoBase::INV_ZOOM;

    std::array<float, 4> color = m_grabbers[axis].color;
    if (!picking && m_hover_id != -1) {
        if (m_hover_id == axis) {
            color = m_grabbers[axis].hover_color;
        }
    }

    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;

    const_cast<GLModel*>(&m_vbo_cone)->set_color(-1, color);
    if (!picking) {
        shader->start_using();
        shader->set_uniform("emission_factor", 0.1f);
    }

    glsafe(::glPushMatrix());
    glsafe(::glTranslated(m_grabbers[axis].center.x(), m_grabbers[axis].center.y(), m_grabbers[axis].center.z()));
    if (axis == X)
        glsafe(::glRotated(90.0, 0.0, 1.0, 0.0));
    else if (axis == Y)
        glsafe(::glRotated(-90.0, 1.0, 0.0, 0.0));

    //glsafe(::glTranslated(0.0, 0.0, 2.0 * size));
    glsafe(::glScaled(0.75 * size, 0.75 * size, 2.0 * size));
    m_vbo_cone.render();
    glsafe(::glPopMatrix());

    if (! picking)
        shader->stop_using();
}



} // namespace GUI
} // namespace Slic3r
