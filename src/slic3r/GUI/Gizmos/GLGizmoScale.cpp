// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoScale.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"

#include <GL/glew.h>

#include <wx/utils.h>

namespace Slic3r {
namespace GUI {


const float GLGizmoScale3D::Offset = 5.0f;

// get intersection of ray and plane
Vec3d GetIntersectionOfRayAndPlane(Vec3d ray_position, Vec3d ray_dir, Vec3d plane_position, Vec3d plane_normal)
{
    double t = (plane_normal.dot(plane_position) - plane_normal.dot(ray_position)) / (plane_normal.dot(ray_dir));
    Vec3d  intersection = ray_position + t * ray_dir;
    return intersection;
}

//BBS: GUI refactor: add obj manipulation
GLGizmoScale3D::GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_scale(Vec3d::Ones())
    , m_offset(Vec3d::Zero())
    , m_snap_step(0.05)
    //BBS: GUI refactor: add obj manipulation
    , m_object_manipulation(obj_manipulation)
{}

const Vec3d &GLGizmoScale3D::get_scale()
{
    if (m_object_manipulation) {
        Vec3d cache_scale = m_object_manipulation->get_cache().scale.cwiseQuotient(Vec3d(100,100,100));
        Vec3d temp_scale  = cache_scale.cwiseProduct(m_scale);
        m_object_manipulation->limit_scaling_ratio(temp_scale);
        m_scale = temp_scale.cwiseQuotient(cache_scale);
    }
    return m_scale;
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
        std::string tooltip = "X: " + format(scale(0), 2) + "%\n";
        tooltip += "Y: " + format(scale(1), 2) + "%\n";
        tooltip += "Z: " + format(scale(2), 2) + "%";
        return tooltip;
    }
    else
        return "";
}

void GLGizmoScale3D::data_changed(bool is_serializing)
{
    change_cs_by_selection();
}

void GLGizmoScale3D::enable_ununiversal_scale(bool enable)
{
    for (unsigned int i = 0; i < 6; ++i)
        m_grabbers[i].enabled = enable;
}

bool GLGizmoScale3D::on_init()
{
    for (int i = 0; i < 10; ++i)
    {
        m_grabbers.push_back(Grabber());
    }

    double half_pi = 0.5 * (double)PI;


    // BBS
    m_grabbers[4].enabled = false;
    m_shortcut_key = WXK_CONTROL_S;

    return true;
}

std::string GLGizmoScale3D::on_get_name() const
{
    if (!on_is_activable() && m_state == EState::Off) {
        return _u8L("Scale") + ":\n" + _u8L("Please select at least one object.");
    } else {
        return _u8L("Scale");
    }
}

bool GLGizmoScale3D::on_is_activable() const
{
    const Selection &selection = m_parent.get_selection();
    return !selection.is_empty() && !selection.is_wipe_tower() && !selection.is_any_cut_volume() && !selection.is_any_connector();
}

void GLGizmoScale3D::on_set_state() {
    if (get_state() == On) {
        m_last_selected_obejct_idx = -1;
        m_last_selected_volume_idx = -1;
        change_cs_by_selection();
    }
}

static int constraint_id(int grabber_id)
{
    static const std::vector<int> id_map = {1, 0, 3, 2, 5, 4, 8, 9, 6, 7};
    return (0 <= grabber_id && grabber_id < (int) id_map.size()) ? id_map[grabber_id] : -1;
}

void GLGizmoScale3D::on_start_dragging()
{
    if (m_hover_id != -1) {
        auto grabbers_transform  = m_grabbers_tran.get_matrix();
        m_starting.drag_position = grabbers_transform * m_grabbers[m_hover_id].center;
        m_starting.plane_center  = grabbers_transform * m_grabbers[4].center; // plane_center = bottom center
        m_starting.plane_nromal  = (grabbers_transform * m_grabbers[5].center - grabbers_transform * m_grabbers[4].center).normalized();
        m_starting.ctrl_down = wxGetKeyState(WXK_CONTROL);
        m_starting.box           = m_bounding_box;

        m_starting.center              = m_center;
        m_starting.instance_center     = m_instance_center;

        const Vec3d  box_half_size = 0.5 * m_bounding_box.size();

        m_starting.local_pivots[0] = Vec3d(box_half_size.x(), 0.0, -box_half_size.z());
        m_starting.local_pivots[1] = Vec3d(-box_half_size.x(), 0.0, -box_half_size.z());
        m_starting.local_pivots[2] = Vec3d(0.0, box_half_size.y(), -box_half_size.z());
        m_starting.local_pivots[3] = Vec3d(0.0, -box_half_size.y(), -box_half_size.z());
        m_starting.local_pivots[4] = Vec3d(0.0, 0.0, box_half_size.z());
        m_starting.local_pivots[5] = Vec3d(0.0, 0.0, -box_half_size.z());
        for (size_t i = 0; i < 6; i++) {
            m_starting.pivots[i] = grabbers_transform * m_starting.local_pivots[i]; // todo delete
        }
        m_starting.constraint_position = grabbers_transform * m_grabbers[constraint_id(m_hover_id)].center;
        m_scale = m_starting.scale = Vec3d::Ones() ;
        m_offset                   = Vec3d::Zero();
    }
}

void GLGizmoScale3D::on_stop_dragging()
{
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

void GLGizmoScale3D::update_grabbers_data()
{
    const Selection &selection = m_parent.get_selection();
    const auto &[box, box_trafo] = selection.get_bounding_box_in_current_reference_system();
    m_bounding_box               = box;
    m_center                     = box_trafo.translation();
    m_grabbers_tran.set_matrix(box_trafo);
    m_instance_center = (selection.is_single_full_instance() || selection.is_single_volume_or_modifier()) ? selection.get_first_volume()->get_instance_offset() : m_center;

    const Vec3d box_half_size   = 0.5 * m_bounding_box.size();
    bool        ctrl_down     = wxGetKeyState(WXK_CONTROL);


    bool single_instance = selection.is_single_full_instance();
    bool single_volume   = selection.is_single_modifier() || selection.is_single_volume();

    // x axis
    m_grabbers[0].center = Vec3d(-(box_half_size.x()), 0.0, -box_half_size.z());
    m_grabbers[0].color  = (ctrl_down && m_hover_id == 1) ? CONSTRAINED_COLOR : AXES_COLOR[0];
    m_grabbers[1].center = Vec3d(box_half_size.x(), 0.0, -box_half_size.z());
    m_grabbers[1].color  = (ctrl_down && m_hover_id == 0) ? CONSTRAINED_COLOR : AXES_COLOR[0];
    // y axis
    m_grabbers[2].center = Vec3d(0.0, -(box_half_size.y()), -box_half_size.z());
    m_grabbers[2].color  = (ctrl_down && m_hover_id == 3) ? CONSTRAINED_COLOR : AXES_COLOR[1];
    m_grabbers[3].center = Vec3d(0.0, box_half_size.y(), -box_half_size.z());
    m_grabbers[3].color  = (ctrl_down && m_hover_id == 2) ? CONSTRAINED_COLOR : AXES_COLOR[1];
    // z axis do not show 4
    m_grabbers[4].center  = Vec3d(0.0, 0.0, -(box_half_size.z()));
    m_grabbers[4].enabled = false;

    m_grabbers[5].center = Vec3d(0.0, 0.0, box_half_size.z());
    m_grabbers[5].color  = (ctrl_down && m_hover_id == 4) ? CONSTRAINED_COLOR : AXES_COLOR[2];
    // uniform
    m_grabbers[6].center = Vec3d(-box_half_size.x(), -box_half_size.y(), -box_half_size.z());
    m_grabbers[6].color  = (ctrl_down && m_hover_id == 8) ? CONSTRAINED_COLOR : GRABBER_UNIFORM_COL;
    m_grabbers[7].center = Vec3d(box_half_size.x(), -box_half_size.y(), -box_half_size.z());
    m_grabbers[7].color  = (ctrl_down && m_hover_id == 9) ? CONSTRAINED_COLOR : GRABBER_UNIFORM_COL;
    m_grabbers[8].center = Vec3d(box_half_size.x(), box_half_size.y(), -box_half_size.z());
    m_grabbers[8].color  = (ctrl_down && m_hover_id == 6) ? CONSTRAINED_COLOR : GRABBER_UNIFORM_COL;
    m_grabbers[9].center = Vec3d(-box_half_size.x(), box_half_size.y(), -box_half_size.z());
    m_grabbers[9].color  = (ctrl_down && m_hover_id == 7) ? CONSTRAINED_COLOR : GRABBER_UNIFORM_COL;
    for (int i = 0; i < 6; ++i) {
        //m_grabbers[i].color       = AXES_COLOR[i / 2];
        m_grabbers[i].hover_color = AXES_HOVER_COLOR[i / 2];
    }
    for (int i = 6; i < 10; ++i) {
        //m_grabbers[i].color       = GRABBER_UNIFORM_COL;
        m_grabbers[i].hover_color = GRABBER_UNIFORM_HOVER_COL;
    }
}


void GLGizmoScale3D::change_cs_by_selection() {
    int          obejct_idx, volume_idx;
    ModelVolume *model_volume = m_parent.get_selection().get_selected_single_volume(obejct_idx, volume_idx);
    if (m_last_selected_obejct_idx == obejct_idx && m_last_selected_volume_idx == volume_idx) { return; }
    m_last_selected_obejct_idx = obejct_idx;
    m_last_selected_volume_idx = volume_idx;
    if (m_parent.get_selection().is_multiple_full_object()) {
        m_object_manipulation->set_coordinates_type(ECoordinatesType::World);
    } else if (model_volume) {
        m_object_manipulation->set_coordinates_type(ECoordinatesType::Local);
    }
}

void GLGizmoScale3D::on_render()
{
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));

    update_grabbers_data();

    glsafe(::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f));

    float grabber_mean_size = (float) ((m_bounding_box.size().x() + m_bounding_box.size().y() + m_bounding_box.size().z()) / 3.0);
    glsafe(::glPushMatrix());
    glsafe(::glMultMatrixd(m_grabbers_tran.get_matrix().data()));
     //draw connections

    // BBS: when select multiple objects, uniform scale can be deselected, display the connection(4,5)
    //if (single_instance || single_volume) {

    if (m_grabbers[4].enabled && m_grabbers[5].enabled) {
        glsafe(::glColor4fv(m_grabbers[4].color.data()));
        render_grabbers_connection(4, 5);
    }

    glsafe(::glColor4fv(m_grabbers[2].color.data()));
    render_grabbers_connection(6, 7);
    render_grabbers_connection(8, 9);

    glsafe(::glColor4fv(m_grabbers[0].color.data()));
    render_grabbers_connection(7, 8);
    render_grabbers_connection(9, 6);

    // draw grabbers
    render_grabbers(grabber_mean_size);
    glsafe(::glPopMatrix());
}

void GLGizmoScale3D::on_render_for_picking()
{
    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glPushMatrix());
    glsafe(::glMultMatrixd(m_grabbers_tran.get_matrix().data()));
    render_grabbers_for_picking(m_parent.get_selection().get_bounding_box());
    glsafe(::glPopMatrix());
}

void GLGizmoScale3D::render_grabbers_connection(unsigned int id_1, unsigned int id_2) const
{
    unsigned int grabbers_count = (unsigned int)m_grabbers.size();
    if ((id_1 < grabbers_count) && (id_2 < grabbers_count))
    {
        glLineStipple(1, 0x0FFF);
        glEnable(GL_LINE_STIPPLE);
        ::glBegin(GL_LINES);
        ::glVertex3dv(m_grabbers[id_1].center.data());
        ::glVertex3dv(m_grabbers[id_2].center.data());
        glsafe(::glEnd());
        glDisable(GL_LINE_STIPPLE);
    }
}

//BBS: add input window for move
void GLGizmoScale3D::on_render_input_window(float x, float y, float bottom_limit)
{
    if (m_object_manipulation)
        m_object_manipulation->do_render_scale_input_window(m_imgui, "Scale", x, y, bottom_limit);
}

void GLGizmoScale3D::do_scale_along_axis(Axis axis, const UpdateData& data)
{
    double ratio = calc_ratio(data);
    if (ratio > 0.0)
    {
        m_scale(axis) = m_starting.scale(axis) * ratio;
        if (m_starting.ctrl_down && abs(ratio-1.0f)>0.001) {
            double local_offset = 0.5 * (m_scale(axis) - m_starting.scale(axis)) * m_starting.box.size()(axis);
            if (m_hover_id == 2 * axis) {
                local_offset *= -1.0;
            }
            Vec3d local_offset_vec;
            switch (axis)
            {
            case X: { local_offset_vec = local_offset * Vec3d::UnitX(); break; }
            case Y: { local_offset_vec = local_offset * Vec3d::UnitY(); break;}
            case Z: { local_offset_vec = local_offset * Vec3d::UnitZ(); break;
            }
            default: break;
            }
            if (m_object_manipulation->is_world_coordinates()) {
                m_offset = local_offset_vec;
            } else {//if (m_object_manipulation->is_instance_coordinates())
                m_offset = m_grabbers_tran.get_matrix_no_offset() *  local_offset_vec;
            }
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

    Vec3d  pivot       = (m_starting.ctrl_down && (m_hover_id < 6)) ? m_starting.constraint_position : m_starting.plane_center; // plane_center = bottom center
    Vec3d starting_vec = m_starting.drag_position - pivot;
    double len_starting_vec = starting_vec.norm();
    if (len_starting_vec != 0.0)
    {
        Vec3d mouse_dir = data.mouse_ray.unit_vector();
        Vec3d plane_normal = m_starting.plane_nromal;
        if (m_hover_id == 5) {
            // get z-axis moving plane normal
            Vec3d plane_vec = mouse_dir.cross(m_starting.plane_nromal);
            plane_normal    = plane_vec.cross(m_starting.plane_nromal);
        }
        plane_normal = plane_normal.normalized();
        // finds the intersection of the mouse ray with the plane that the drag point moves
        // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection
        auto  dot_value          = (plane_normal.dot(mouse_dir));
        auto  angle              = Geometry::rad2deg(acos(dot_value));
        auto  big_than_min_angle = abs(angle) < 95 && abs(angle) > 85;
        if (big_than_min_angle) {
            return 1;
        }
        Vec3d inters             = GetIntersectionOfRayAndPlane(data.mouse_ray.a, mouse_dir, m_starting.drag_position, plane_normal);

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
