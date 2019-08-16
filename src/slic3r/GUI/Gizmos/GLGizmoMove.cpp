// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoMove.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include <wx/utils.h> 

namespace Slic3r {
namespace GUI {

const double GLGizmoMove3D::Offset = 10.0;

GLGizmoMove3D::GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_displacement(Vec3d::Zero())
    , m_snap_step(1.0)
    , m_starting_drag_position(Vec3d::Zero())
    , m_starting_box_center(Vec3d::Zero())
    , m_starting_box_bottom_center(Vec3d::Zero())
    , m_quadric(nullptr)
{
    m_quadric = ::gluNewQuadric();
    if (m_quadric != nullptr)
        ::gluQuadricDrawStyle(m_quadric, GLU_FILL);
}

GLGizmoMove3D::~GLGizmoMove3D()
{
    if (m_quadric != nullptr)
        ::gluDeleteQuadric(m_quadric);
}

bool GLGizmoMove3D::on_init()
{
    for (int i = 0; i < 3; ++i)
    {
        m_grabbers.push_back(Grabber());
    }

    m_shortcut_key = WXK_CONTROL_M;

    return true;
}

std::string GLGizmoMove3D::on_get_name() const
{
    return (_(L("Move")) + " [M]").ToUTF8().data();
}

void GLGizmoMove3D::on_start_dragging()
{
    if (m_hover_id != -1)
    {
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
        m_displacement(0) = calc_projection(data);
    else if (m_hover_id == 1)
        m_displacement(1) = calc_projection(data);
    else if (m_hover_id == 2)
        m_displacement(2) = calc_projection(data);
}

void GLGizmoMove3D::on_render() const
{
    const Selection& selection = m_parent.get_selection();

    bool show_position = selection.is_single_full_instance();
    const Vec3d& position = selection.get_bounding_box().center();

    if ((show_position && (m_hover_id == 0)) || m_grabbers[0].dragging)
        set_tooltip("X: " + format(show_position ? position(0) : m_displacement(0), 2));
    else if (!m_grabbers[0].dragging && (m_hover_id == 0))
        set_tooltip("X");
    else if ((show_position && (m_hover_id == 1)) || m_grabbers[1].dragging)
        set_tooltip("Y: " + format(show_position ? position(1) : m_displacement(1), 2));
    else if (!m_grabbers[1].dragging && (m_hover_id == 1))
        set_tooltip("Y");
    else if ((show_position && (m_hover_id == 2)) || m_grabbers[2].dragging)
        set_tooltip("Z: " + format(show_position ? position(2) : m_displacement(2), 2));
    else if (!m_grabbers[2].dragging && (m_hover_id == 2))
        set_tooltip("Z");

    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));

    const BoundingBoxf3& box = selection.get_bounding_box();
    const Vec3d& center = box.center();

    // x axis
    m_grabbers[0].center = Vec3d(box.max(0) + Offset, center(1), center(2));
    ::memcpy((void*)m_grabbers[0].color, (const void*)&AXES_COLOR[0], 4 * sizeof(float));

    // y axis
    m_grabbers[1].center = Vec3d(center(0), box.max(1) + Offset, center(2));
    ::memcpy((void*)m_grabbers[1].color, (const void*)&AXES_COLOR[1], 4 * sizeof(float));

    // z axis
    m_grabbers[2].center = Vec3d(center(0), center(1), box.max(2) + Offset);
    ::memcpy((void*)m_grabbers[2].color, (const void*)&AXES_COLOR[2], 4 * sizeof(float));

    glsafe(::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f));

    if (m_hover_id == -1)
    {
        // draw axes
        for (unsigned int i = 0; i < 3; ++i)
        {
            if (m_grabbers[i].enabled)
            {
                glsafe(::glColor4fv(AXES_COLOR[i]));
                ::glBegin(GL_LINES);
                ::glVertex3dv(center.data());
                ::glVertex3dv(m_grabbers[i].center.data());
                glsafe(::glEnd());
            }
        }

        // draw grabbers
        render_grabbers(box);
        for (unsigned int i = 0; i < 3; ++i)
        {
            if (m_grabbers[i].enabled)
                render_grabber_extension((Axis)i, box, false);
        }
    }
    else
    {
        // draw axis
        glsafe(::glColor4fv(AXES_COLOR[m_hover_id]));
        ::glBegin(GL_LINES);
        ::glVertex3dv(center.data());
        ::glVertex3dv(m_grabbers[m_hover_id].center.data());
        glsafe(::glEnd());

        // draw grabber
        float mean_size = (float)((box.size()(0) + box.size()(1) + box.size()(2)) / 3.0);
        m_grabbers[m_hover_id].render(true, mean_size);
        render_grabber_extension((Axis)m_hover_id, box, false);
    }
}

void GLGizmoMove3D::on_render_for_picking() const
{
    glsafe(::glDisable(GL_DEPTH_TEST));

    const BoundingBoxf3& box = m_parent.get_selection().get_bounding_box();
    render_grabbers_for_picking(box);
    render_grabber_extension(X, box, true);
    render_grabber_extension(Y, box, true);
    render_grabber_extension(Z, box, true);
}

#if !DISABLE_MOVE_ROTATE_SCALE_GIZMOS_IMGUI
void GLGizmoMove3D::on_render_input_window(float x, float y, float bottom_limit)
{
    const Selection& selection = m_parent.get_selection();
    bool show_position = selection.is_single_full_instance();
    const Vec3d& position = selection.get_bounding_box().center();

    Vec3d displacement = show_position ? position : m_displacement;
    wxString label = show_position ? _(L("Position (mm)")) : _(L("Displacement (mm)"));

    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);
    m_imgui->set_next_window_bg_alpha(0.5f);
    m_imgui->begin(label, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    m_imgui->input_vec3("", displacement, 100.0f, "%.2f");

    m_imgui->end();
}
#endif // !DISABLE_MOVE_ROTATE_SCALE_GIZMOS_IMGUI

double GLGizmoMove3D::calc_projection(const UpdateData& data) const
{
    double projection = 0.0;

    Vec3d starting_vec = m_starting_drag_position - m_starting_box_center;
    double len_starting_vec = starting_vec.norm();
    if (len_starting_vec != 0.0)
    {
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
    if (m_quadric == nullptr)
        return;

    float mean_size = (float)((box.size()(0) + box.size()(1) + box.size()(2)) / 3.0);
    double size = m_dragging ? (double)m_grabbers[axis].get_dragging_half_size(mean_size) : (double)m_grabbers[axis].get_half_size(mean_size);

    float color[4];
    ::memcpy((void*)color, (const void*)m_grabbers[axis].color, 4 * sizeof(float));
    if (!picking && (m_hover_id != -1))
    {
        color[0] = 1.0f - color[0];
        color[1] = 1.0f - color[1];
        color[2] = 1.0f - color[2];
        color[3] = color[3];
    }

    if (!picking)
        glsafe(::glEnable(GL_LIGHTING));

    glsafe(::glColor4fv(color));
    glsafe(::glPushMatrix());
    glsafe(::glTranslated(m_grabbers[axis].center(0), m_grabbers[axis].center(1), m_grabbers[axis].center(2)));
    if (axis == X)
        glsafe(::glRotated(90.0, 0.0, 1.0, 0.0));
    else if (axis == Y)
        glsafe(::glRotated(-90.0, 1.0, 0.0, 0.0));

    glsafe(::glTranslated(0.0, 0.0, 2.0 * size));
    ::gluQuadricOrientation(m_quadric, GLU_OUTSIDE);
    ::gluCylinder(m_quadric, 0.75 * size, 0.0, 3.0 * size, 36, 1);
    ::gluQuadricOrientation(m_quadric, GLU_INSIDE);
    ::gluDisk(m_quadric, 0.0, 0.75 * size, 36, 1);
    glsafe(::glPopMatrix());

    if (!picking)
        glsafe(::glDisable(GL_LIGHTING));
}



} // namespace GUI
} // namespace Slic3r
