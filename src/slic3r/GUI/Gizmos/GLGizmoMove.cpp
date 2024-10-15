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
    try {
        float value                             = std::stof(wxGetApp().app_config->get("grabber_size_factor"));
        GLGizmoBase::Grabber::GrabberSizeFactor = value;
    } catch (const std::invalid_argument &e) {
        GLGizmoBase::Grabber::GrabberSizeFactor = 1.0f;
    }
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

void GLGizmoMove3D::data_changed(bool is_serializing)
{
    change_cs_by_selection();
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
    if (!on_is_activable() && m_state == EState::Off) {
        return _u8L("Move") + ":\n" + _u8L("Please select at least one object.");
    } else {
        return _u8L("Move");
    }
}

bool GLGizmoMove3D::on_is_activable() const
{
    const Selection &selection = m_parent.get_selection();
    return !selection.is_any_cut_volume() && !selection.is_any_connector() && !selection.is_empty();
}

void GLGizmoMove3D::on_set_state() {
    if (get_state() == On) {
        m_last_selected_obejct_idx = -1;
        m_last_selected_volume_idx = -1;
        change_cs_by_selection();
    }
}

void GLGizmoMove3D::on_start_dragging()
{
    if (m_hover_id != -1) {
        m_displacement = Vec3d::Zero();
        const BoundingBoxf3& box = m_parent.get_selection().get_bounding_box();
        m_starting_drag_position = m_orient_matrix *m_grabbers[m_hover_id].center;
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
    Selection& selection = m_parent.get_selection();

    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));

    const auto &[box, box_trafo]    = selection.get_bounding_box_in_current_reference_system();
    m_bounding_box                  = box;
    m_center                        = box_trafo.translation();
    if (m_object_manipulation) {
        m_object_manipulation->cs_center = box_trafo.translation();
    }
    m_orient_matrix                 = box_trafo;
    float space_size = 20.f *INV_ZOOM;
    space_size *= GLGizmoBase::Grabber::GrabberSizeFactor;
#if ENABLE_FIXED_GRABBER
    // x axis
    m_grabbers[0].center = {m_bounding_box.max.x() + space_size, 0, 0};
    // y axis
    m_grabbers[1].center = {0, m_bounding_box.max.y() + space_size,0};
    // z axis
    m_grabbers[2].center = {0,0, m_bounding_box.max.z() + space_size};

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
    glsafe(::glPushMatrix());
    glsafe(::glMultMatrixd(Geometry::Transformation(m_orient_matrix).get_matrix().data()));
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
            ::glVertex3dv(origin.data());
            // use extension center
            ::glVertex3dv(m_grabbers[i].center.data());
            glsafe(::glEnd());
            glDisable(GL_LINE_STIPPLE);
        }
    }
    glsafe(::glPopMatrix());

    if (m_object_manipulation->is_instance_coordinates()) {
        glsafe(::glPushMatrix());
        Geometry::Transformation cur_tran;
        if (auto mi = m_parent.get_selection().get_selected_single_intance()) {
            cur_tran = mi->get_transformation();
        }
        else {
            cur_tran = selection.get_first_volume()->get_instance_transformation();
        }
        glsafe(::glMultMatrixd(cur_tran.get_matrix().data()));
        render_cross_mark(Vec3f::Zero(), true);
        glsafe(::glPopMatrix());
    }
}

void GLGizmoMove3D::on_render_for_picking()
{
    glsafe(::glDisable(GL_DEPTH_TEST));

    //BBS donot render base grabber for picking
    //render_grabbers_for_picking(box);

    //get picking colors only
    for (unsigned int i = 0; i < (unsigned int) m_grabbers.size(); ++i) {
        if (m_grabbers[i].enabled) {
            std::array<float, 4> color = picking_color_component(i);
            m_grabbers[i].color        = color;
        }
    }
    glsafe(::glPushMatrix());
    glsafe(::glMultMatrixd(Geometry::Transformation(m_orient_matrix).get_matrix().data()));
    render_grabber_extension(X, m_bounding_box, true);
    render_grabber_extension(Y, m_bounding_box, true);
    render_grabber_extension(Z, m_bounding_box, true);
    glsafe(::glPopMatrix());
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
    double size = 0.75 * GLGizmoBase::Grabber::FixedGrabberSize * GLGizmoBase::INV_ZOOM;
    size                       = size * GLGizmoBase::Grabber::GrabberSizeFactor;
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

void GLGizmoMove3D::change_cs_by_selection() {
    int          obejct_idx, volume_idx;
    ModelVolume *model_volume = m_parent.get_selection().get_selected_single_volume(obejct_idx, volume_idx);
    if (m_last_selected_obejct_idx == obejct_idx && m_last_selected_volume_idx == volume_idx) {
        return;
    }
    m_last_selected_obejct_idx = obejct_idx;
    m_last_selected_volume_idx = volume_idx;
    if (m_parent.get_selection().is_multiple_full_object()) {
        m_object_manipulation->set_use_object_cs(false);
    }
    else if (model_volume) {
         m_object_manipulation->set_use_object_cs(true);
    } else {
        m_object_manipulation->set_use_object_cs(false);
    }
    if (m_object_manipulation->get_use_object_cs()) {
        m_object_manipulation->set_coordinates_type(ECoordinatesType::Instance);
    } else {
        m_object_manipulation->set_coordinates_type(ECoordinatesType::World);
    }
}


} // namespace GUI
} // namespace Slic3r
