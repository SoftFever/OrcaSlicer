// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoScale.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include <GL/glew.h>

#include <wx/utils.h> 

namespace Slic3r {
namespace GUI {


const float GLGizmoScale3D::Offset = 5.0f;

GLGizmoScale3D::GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_scale(Vec3d::Ones())
    , m_offset(Vec3d::Zero())
    , m_snap_step(0.05)
{
}

std::string GLGizmoScale3D::get_tooltip() const
{
    const Selection& selection = m_parent.get_selection();

    bool single_instance = selection.is_single_full_instance();
    bool single_volume = selection.is_single_modifier() || selection.is_single_volume();

    Vec3f scale = 100.0f * Vec3f::Ones();
    if (single_instance)
        scale = 100.0f * selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_scaling_factor().cast<float>();
    else if (single_volume)
        scale = 100.0f * selection.get_volume(*selection.get_volume_idxs().begin())->get_volume_scaling_factor().cast<float>();

    if (m_hover_id == 0 || m_hover_id == 1 || m_grabbers[0].dragging || m_grabbers[1].dragging)
        return "X: " + format(scale(0), 4) + "%";
    else if (m_hover_id == 2 || m_hover_id == 3 || m_grabbers[2].dragging || m_grabbers[3].dragging)
        return "Y: " + format(scale(1), 4) + "%";
    else if (m_hover_id == 4 || m_hover_id == 5 || m_grabbers[4].dragging || m_grabbers[5].dragging)
        return "Z: " + format(scale(2), 4) + "%";
    else if (m_hover_id == 6 || m_hover_id == 7 || m_hover_id == 8 || m_hover_id == 9 || 
        m_grabbers[6].dragging || m_grabbers[7].dragging || m_grabbers[8].dragging || m_grabbers[9].dragging)
    {
        std::string tooltip = "X: " + format(scale(0), 4) + "%\n";
        tooltip += "Y: " + format(scale(1), 4) + "%\n";
        tooltip += "Z: " + format(scale(2), 4) + "%";
        return tooltip;
    }
    else
        return "";
}

bool GLGizmoScale3D::on_init()
{
    for (int i = 0; i < 10; ++i)
    {
        m_grabbers.push_back(Grabber());
    }

    double half_pi = 0.5 * (double)PI;

    // x axis
    m_grabbers[0].angles(1) = half_pi;
    m_grabbers[1].angles(1) = half_pi;

    // y axis
    m_grabbers[2].angles(0) = half_pi;
    m_grabbers[3].angles(0) = half_pi;

    m_shortcut_key = WXK_CONTROL_S;

    return true;
}

std::string GLGizmoScale3D::on_get_name() const
{
    return (_L("Scale") + " [S]").ToUTF8().data();
}

bool GLGizmoScale3D::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();
    return !selection.is_empty() && !selection.is_wipe_tower();
}

void GLGizmoScale3D::on_start_dragging()
{
    if (m_hover_id != -1)
    {
        m_starting.drag_position = m_grabbers[m_hover_id].center;
        m_starting.ctrl_down = wxGetKeyState(WXK_CONTROL);
        m_starting.box = (m_starting.ctrl_down && (m_hover_id < 6)) ? m_box : m_parent.get_selection().get_bounding_box();

        const Vec3d& center = m_starting.box.center();
        m_starting.pivots[0] = m_transform * Vec3d(m_starting.box.max(0), center(1), center(2));
        m_starting.pivots[1] = m_transform * Vec3d(m_starting.box.min(0), center(1), center(2));
        m_starting.pivots[2] = m_transform * Vec3d(center(0), m_starting.box.max(1), center(2));
        m_starting.pivots[3] = m_transform * Vec3d(center(0), m_starting.box.min(1), center(2));
        m_starting.pivots[4] = m_transform * Vec3d(center(0), center(1), m_starting.box.max(2));
        m_starting.pivots[5] = m_transform * Vec3d(center(0), center(1), m_starting.box.min(2));
    }
}

void GLGizmoScale3D::on_update(const UpdateData& data)
{
    if ((m_hover_id == 0) || (m_hover_id == 1))
        do_scale_along_axis(X, data);
    else if ((m_hover_id == 2) || (m_hover_id == 3))
        do_scale_along_axis(Y, data);
    else if ((m_hover_id == 4) || (m_hover_id == 5))
        do_scale_along_axis(Z, data);
    else if (m_hover_id >= 6)
        do_scale_uniform(data);
}

void GLGizmoScale3D::on_render() const
{
    const Selection& selection = m_parent.get_selection();

    bool single_instance = selection.is_single_full_instance();
    bool single_volume = selection.is_single_modifier() || selection.is_single_volume();

    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));

    m_box.reset();
    m_transform = Transform3d::Identity();
    // Transforms grabbers' offsets to world refefence system 
    Transform3d offsets_transform = Transform3d::Identity();
    m_offsets_transform = Transform3d::Identity();
    Vec3d angles = Vec3d::Zero();

    if (single_instance) {
        // calculate bounding box in instance local reference system
        const Selection::IndicesList& idxs = selection.get_volume_idxs();
        for (unsigned int idx : idxs) {
            const GLVolume* vol = selection.get_volume(idx);
            m_box.merge(vol->bounding_box().transformed(vol->get_volume_transformation().get_matrix()));
        }

        // gets transform from first selected volume
        const GLVolume* v = selection.get_volume(*idxs.begin());
        m_transform = v->get_instance_transformation().get_matrix();
        // gets angles from first selected volume
        angles = v->get_instance_rotation();
        // consider rotation+mirror only components of the transform for offsets
        offsets_transform = Geometry::assemble_transform(Vec3d::Zero(), angles, Vec3d::Ones(), v->get_instance_mirror());
        m_offsets_transform = offsets_transform;
    }
    else if (single_volume) {
        const GLVolume* v = selection.get_volume(*selection.get_volume_idxs().begin());
        m_box = v->bounding_box();
        m_transform = v->world_matrix();
        angles = Geometry::extract_euler_angles(m_transform);
        // consider rotation+mirror only components of the transform for offsets
        offsets_transform = Geometry::assemble_transform(Vec3d::Zero(), angles, Vec3d::Ones(), v->get_instance_mirror());
        m_offsets_transform = Geometry::assemble_transform(Vec3d::Zero(), v->get_volume_rotation(), Vec3d::Ones(), v->get_volume_mirror());
    }
    else
        m_box = selection.get_bounding_box();

    const Vec3d& center = m_box.center();
    Vec3d offset_x = offsets_transform * Vec3d((double)Offset, 0.0, 0.0);
    Vec3d offset_y = offsets_transform * Vec3d(0.0, (double)Offset, 0.0);
    Vec3d offset_z = offsets_transform * Vec3d(0.0, 0.0, (double)Offset);

    bool ctrl_down = (m_dragging && m_starting.ctrl_down) || (!m_dragging && wxGetKeyState(WXK_CONTROL));

    // x axis
    m_grabbers[0].center = m_transform * Vec3d(m_box.min(0), center(1), center(2)) - offset_x;
    m_grabbers[0].color = (ctrl_down && (m_hover_id == 1)) ? CONSTRAINED_COLOR : AXES_COLOR[0];
    m_grabbers[1].center = m_transform * Vec3d(m_box.max(0), center(1), center(2)) + offset_x;
    m_grabbers[1].color = (ctrl_down && (m_hover_id == 0)) ? CONSTRAINED_COLOR : AXES_COLOR[0];

    // y axis
    m_grabbers[2].center = m_transform * Vec3d(center(0), m_box.min(1), center(2)) - offset_y;
    m_grabbers[2].color = (ctrl_down && (m_hover_id == 3)) ? CONSTRAINED_COLOR : AXES_COLOR[1];
    m_grabbers[3].center = m_transform * Vec3d(center(0), m_box.max(1), center(2)) + offset_y;
    m_grabbers[3].color = (ctrl_down && (m_hover_id == 2)) ? CONSTRAINED_COLOR : AXES_COLOR[1];

    // z axis
    m_grabbers[4].center = m_transform * Vec3d(center(0), center(1), m_box.min(2)) - offset_z;
    m_grabbers[4].color = (ctrl_down && (m_hover_id == 5)) ? CONSTRAINED_COLOR : AXES_COLOR[2];
    m_grabbers[5].center = m_transform * Vec3d(center(0), center(1), m_box.max(2)) + offset_z;
    m_grabbers[5].color = (ctrl_down && (m_hover_id == 4)) ? CONSTRAINED_COLOR : AXES_COLOR[2];

    // uniform
    m_grabbers[6].center = m_transform * Vec3d(m_box.min(0), m_box.min(1), center(2)) - offset_x - offset_y;
    m_grabbers[7].center = m_transform * Vec3d(m_box.max(0), m_box.min(1), center(2)) + offset_x - offset_y;
    m_grabbers[8].center = m_transform * Vec3d(m_box.max(0), m_box.max(1), center(2)) + offset_x + offset_y;
    m_grabbers[9].center = m_transform * Vec3d(m_box.min(0), m_box.max(1), center(2)) - offset_x + offset_y;
    for (int i = 6; i < 10; ++i) {
        m_grabbers[i].color = m_highlight_color;
    }

    // sets grabbers orientation
    for (int i = 0; i < 10; ++i) {
        m_grabbers[i].angles = angles;
    }

    glsafe(::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f));

    const BoundingBoxf3& selection_box = selection.get_bounding_box();

    float grabber_mean_size = (float)((selection_box.size()(0) + selection_box.size()(1) + selection_box.size()(2)) / 3.0);

    if (m_hover_id == -1) {
        // draw connections
        if (m_grabbers[0].enabled && m_grabbers[1].enabled) {
            glsafe(::glColor4fv(m_grabbers[0].color.data()));
            render_grabbers_connection(0, 1);
        }
        if (m_grabbers[2].enabled && m_grabbers[3].enabled) {
            glsafe(::glColor4fv(m_grabbers[2].color.data()));
            render_grabbers_connection(2, 3);
        }
        if (m_grabbers[4].enabled && m_grabbers[5].enabled) {
            glsafe(::glColor4fv(m_grabbers[4].color.data()));
            render_grabbers_connection(4, 5);
        }
        glsafe(::glColor4fv(m_base_color.data()));
        render_grabbers_connection(6, 7);
        render_grabbers_connection(7, 8);
        render_grabbers_connection(8, 9);
        render_grabbers_connection(9, 6);
        // draw grabbers
        render_grabbers(grabber_mean_size);
    }
    else if (m_hover_id == 0 || m_hover_id == 1) {
        // draw connection
        glsafe(::glColor4fv(m_grabbers[0].color.data()));
        render_grabbers_connection(0, 1);

        GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
        if (shader != nullptr) {
            shader->start_using();
            shader->set_uniform("emission_factor", 0.1);
            // draw grabbers
            m_grabbers[0].render(true, grabber_mean_size);
            m_grabbers[1].render(true, grabber_mean_size);
            shader->stop_using();
        }
    }
    else if (m_hover_id == 2 || m_hover_id == 3) {
        // draw connection
        glsafe(::glColor4fv(m_grabbers[2].color.data()));
        render_grabbers_connection(2, 3);

        GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
        if (shader != nullptr) {
            shader->start_using();
            shader->set_uniform("emission_factor", 0.1);
            // draw grabbers
            m_grabbers[2].render(true, grabber_mean_size);
            m_grabbers[3].render(true, grabber_mean_size);
            shader->stop_using();
        }
    }
    else if (m_hover_id == 4 || m_hover_id == 5) {
        // draw connection
        glsafe(::glColor4fv(m_grabbers[4].color.data()));
        render_grabbers_connection(4, 5);

        GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
        if (shader != nullptr) {
            shader->start_using();
            shader->set_uniform("emission_factor", 0.1);
            // draw grabbers
            m_grabbers[4].render(true, grabber_mean_size);
            m_grabbers[5].render(true, grabber_mean_size);
            shader->stop_using();
        }
    }
    else if (m_hover_id >= 6) {
        // draw connection
        glsafe(::glColor4fv(m_drag_color.data()));
        render_grabbers_connection(6, 7);
        render_grabbers_connection(7, 8);
        render_grabbers_connection(8, 9);
        render_grabbers_connection(9, 6);

        GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
        if (shader != nullptr) {
            shader->start_using();
            shader->set_uniform("emission_factor", 0.1);
            // draw grabbers
            for (int i = 6; i < 10; ++i) {
                m_grabbers[i].render(true, grabber_mean_size);
            }
            shader->stop_using();
        }
    }
}

void GLGizmoScale3D::on_render_for_picking() const
{
    glsafe(::glDisable(GL_DEPTH_TEST));
    render_grabbers_for_picking(m_parent.get_selection().get_bounding_box());
}

void GLGizmoScale3D::render_grabbers_connection(unsigned int id_1, unsigned int id_2) const
{
    unsigned int grabbers_count = (unsigned int)m_grabbers.size();
    if ((id_1 < grabbers_count) && (id_2 < grabbers_count))
    {
        ::glBegin(GL_LINES);
        ::glVertex3dv(m_grabbers[id_1].center.data());
        ::glVertex3dv(m_grabbers[id_2].center.data());
        glsafe(::glEnd());
    }
}

void GLGizmoScale3D::do_scale_along_axis(Axis axis, const UpdateData& data)
{
    double ratio = calc_ratio(data);
    if (ratio > 0.0)
    {
        m_scale(axis) = m_starting.scale(axis) * ratio;
        if (m_starting.ctrl_down)
        {
            double local_offset = 0.5 * (m_scale(axis) - m_starting.scale(axis)) * m_starting.box.size()(axis);
            if (m_hover_id == 2 * axis)
                local_offset *= -1.0;

            Vec3d local_offset_vec;
            switch (axis)
            {
            case X: { local_offset_vec = local_offset * Vec3d::UnitX(); break; }
            case Y: { local_offset_vec = local_offset * Vec3d::UnitY(); break; }
            case Z: { local_offset_vec = local_offset * Vec3d::UnitZ(); break; }
            default: break;
            }

            m_offset = m_offsets_transform * local_offset_vec;
        }
        else
            m_offset = Vec3d::Zero();
    }
}

void GLGizmoScale3D::do_scale_uniform(const UpdateData& data)
{
    double ratio = calc_ratio(data);
    if (ratio > 0.0)
    {
        m_scale = m_starting.scale * ratio;
        m_offset = Vec3d::Zero();
    }
}

double GLGizmoScale3D::calc_ratio(const UpdateData& data) const
{
    double ratio = 0.0;

    Vec3d pivot = (m_starting.ctrl_down && (m_hover_id < 6)) ? m_starting.pivots[m_hover_id] : m_starting.box.center();

    Vec3d starting_vec = m_starting.drag_position - pivot;
    double len_starting_vec = starting_vec.norm();
    if (len_starting_vec != 0.0)
    {
        Vec3d mouse_dir = data.mouse_ray.unit_vector();
        // finds the intersection of the mouse ray with the plane parallel to the camera viewport and passing throught the starting position
        // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection algebric form
        // in our case plane normal and ray direction are the same (orthogonal view)
        // when moving to perspective camera the negative z unit axis of the camera needs to be transformed in world space and used as plane normal
        Vec3d inters = data.mouse_ray.a + (m_starting.drag_position - data.mouse_ray.a).dot(mouse_dir) / mouse_dir.squaredNorm() * mouse_dir;
        // vector from the starting position to the found intersection
        Vec3d inters_vec = inters - m_starting.drag_position;

        // finds projection of the vector along the staring direction
        double proj = inters_vec.dot(starting_vec.normalized());

        ratio = (len_starting_vec + proj) / len_starting_vec;
    }

    if (wxGetKeyState(WXK_SHIFT))
        ratio = m_snap_step * (double)std::round(ratio / m_snap_step);

    return ratio;
}

} // namespace GUI
} // namespace Slic3r
