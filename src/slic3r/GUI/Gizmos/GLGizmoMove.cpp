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
    //BBS: GUI refactor: add obj manipulation
    , m_object_manipulation(obj_manipulation)
{}

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

bool GLGizmoMove3D::on_mouse(const wxMouseEvent &mouse_event) {
    return use_grabbers(mouse_event);
}

void GLGizmoMove3D::data_changed(bool is_serializing) {
    m_grabbers[2].enabled = !m_parent.get_selection().is_wipe_tower();
}

bool GLGizmoMove3D::on_init()
{
    for (int i = 0; i < 3; ++i) {
        m_grabbers.push_back(Grabber());
        m_grabbers.back().extensions = GLGizmoBase::EGrabberExtension::PosZ;
    }

    m_grabbers[0].angles = { 0.0, 0.5 * double(PI), 0.0 };
    m_grabbers[1].angles = { -0.5 * double(PI), 0.0, 0.0 };

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
    assert(m_hover_id != -1);

    m_displacement = Vec3d::Zero();
    const BoundingBoxf3& box = m_parent.get_selection().get_bounding_box();
    m_starting_drag_position = m_grabbers[m_hover_id].center;
    m_starting_box_center = box.center();
    m_starting_box_bottom_center = box.center();
    m_starting_box_bottom_center(2) = box.min(2);
}

void GLGizmoMove3D::on_stop_dragging()
{
    m_parent.do_move(L("Gizmo-Move"));
    m_displacement = Vec3d::Zero();
}

void GLGizmoMove3D::on_dragging(const UpdateData& data)
{
    if (m_hover_id == 0)
        m_displacement.x() = calc_projection(data);
    else if (m_hover_id == 1)
        m_displacement.y() = calc_projection(data);
    else if (m_hover_id == 2)
        m_displacement.z() = calc_projection(data);
        
    Selection &selection = m_parent.get_selection();
    TransformationType trafo_type;
    trafo_type.set_relative();
    switch (wxGetApp().obj_manipul()->get_coordinates_type())
    {
    case ECoordinatesType::Instance: { trafo_type.set_instance(); break; }
    case ECoordinatesType::Local: { trafo_type.set_local(); break; }
    default: { break; }
    }
    selection.translate(m_displacement, trafo_type);
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

    auto render_grabber_connection = [this, &center](unsigned int id) {
        if (m_grabbers[id].enabled) {
            //if (!m_grabber_connections[id].model.is_initialized() || !m_grabber_connections[id].old_center.isApprox(center)) {
                m_grabber_connections[id].old_center = center;
                m_grabber_connections[id].model.reset();

                GLModel::Geometry init_data;
                init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
                init_data.color = AXES_COLOR[id];
                init_data.reserve_vertices(2);
                init_data.reserve_indices(2);

                // vertices
                init_data.add_vertex((Vec3f)center.cast<float>());
                init_data.add_vertex((Vec3f)m_grabbers[id].center.cast<float>());

                // indices
                init_data.add_line(0, 1);

                m_grabber_connections[id].model.init_from(std::move(init_data));
            //}

            m_grabber_connections[id].model.render();
        }
    };

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader != nullptr) {
        shader->start_using();
        const Camera& camera = wxGetApp().plater()->get_camera();
        shader->set_uniform("view_model_matrix", camera.get_view_matrix());
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());

        // draw axes
        for (unsigned int i = 0; i < 3; ++i) {
            render_grabber_connection(i);
        }

        shader->stop_using();
    }
	
	// draw grabbers
    render_grabbers(box);
}

void GLGizmoMove3D::on_register_raycasters_for_picking()
{
    // the gizmo grabbers are rendered on top of the scene, so the raytraced picker should take it into account
    m_parent.set_raycaster_gizmos_on_top(true);
}

void GLGizmoMove3D::on_unregister_raycasters_for_picking()
{
    m_parent.set_raycaster_gizmos_on_top(false);
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

    const Vec3d starting_vec = m_starting_drag_position - m_starting_box_center;
    const double len_starting_vec = starting_vec.norm();
    if (len_starting_vec != 0.0) {
        const Vec3d mouse_dir = data.mouse_ray.unit_vector();
        // finds the intersection of the mouse ray with the plane parallel to the camera viewport and passing throught the starting position
        // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection algebric form
        // in our case plane normal and ray direction are the same (orthogonal view)
        // when moving to perspective camera the negative z unit axis of the camera needs to be transformed in world space and used as plane normal
        const Vec3d inters = data.mouse_ray.a + (m_starting_drag_position - data.mouse_ray.a).dot(mouse_dir) * mouse_dir;
        // vector from the starting position to the found intersection
        const Vec3d inters_vec = inters - m_starting_drag_position;

        // finds projection of the vector along the staring direction
        projection = inters_vec.dot(starting_vec.normalized());
    }

    if (wxGetKeyState(WXK_SHIFT))
        projection = m_snap_step * (double)std::round(projection / m_snap_step);

    return projection;
}
} // namespace GUI
} // namespace Slic3r
