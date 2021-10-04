// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoCut.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>
#include <wx/sizer.h>

#include <algorithm>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_ObjectManipulation.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"

namespace Slic3r {
namespace GUI {

const double GLGizmoCut::Offset = 10.0;
const double GLGizmoCut::Margin = 20.0;
const std::array<float, 4> GLGizmoCut::GrabberColor = { 1.0, 0.5, 0.0, 1.0 };

GLGizmoCut::GLGizmoCut(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{}

std::string GLGizmoCut::get_tooltip() const
{
    double cut_z = m_cut_z;
    if (wxGetApp().app_config->get("use_inches") == "1")
        cut_z *= ObjectManipulation::mm_to_in;

    return (m_hover_id == 0 || m_grabbers[0].dragging) ? "Z: " + format(cut_z, 2) : "";
}

bool GLGizmoCut::on_init()
{
    m_grabbers.emplace_back();
    m_shortcut_key = WXK_CONTROL_C;
    return true;
}

std::string GLGizmoCut::on_get_name() const
{
    return _u8L("Cut");
}

void GLGizmoCut::on_set_state()
{
    // Reset m_cut_z on gizmo activation
    if (get_state() == On)
        m_cut_z = bounding_box().center().z();
}

bool GLGizmoCut::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();
    return selection.is_single_full_instance() && !selection.is_wipe_tower();
}

void GLGizmoCut::on_start_dragging()
{
    if (m_hover_id == -1)
        return;

    const BoundingBoxf3 box = bounding_box();
    m_max_z = box.max.z();
    m_start_z = m_cut_z;
    m_drag_pos = m_grabbers[m_hover_id].center;
    m_drag_center = box.center();
    m_drag_center.z() = m_cut_z;
}

void GLGizmoCut::on_update(const UpdateData& data)
{
    if (m_hover_id != -1)
        set_cut_z(m_start_z + calc_projection(data.mouse_ray));
}

void GLGizmoCut::on_render()
{
    const BoundingBoxf3 box = bounding_box();
    Vec3d plane_center = box.center();
    plane_center.z() = m_cut_z;
    m_max_z = box.max.z();
    set_cut_z(m_cut_z);

    update_contours();

    const float min_x = box.min.x() - Margin;
    const float max_x = box.max.x() + Margin;
    const float min_y = box.min.y() - Margin;
    const float max_y = box.max.y() + Margin;
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    // Draw the cutting plane
    ::glBegin(GL_QUADS);
    ::glColor4f(0.8f, 0.8f, 0.8f, 0.5f);
    ::glVertex3f(min_x, min_y, plane_center.z());
    ::glVertex3f(max_x, min_y, plane_center.z());
    ::glVertex3f(max_x, max_y, plane_center.z());
    ::glVertex3f(min_x, max_y, plane_center.z());
    glsafe(::glEnd());

    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glDisable(GL_BLEND));

    // TODO: draw cut part contour?

    // Draw the grabber and the connecting line
    m_grabbers[0].center = plane_center;
    m_grabbers[0].center.z() = plane_center.z() + Offset;

    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));

    glsafe(::glLineWidth(m_hover_id != -1 ? 2.0f : 1.5f));
    glsafe(::glColor3f(1.0, 1.0, 0.0));
    ::glBegin(GL_LINES);
    ::glVertex3dv(plane_center.data());
    ::glVertex3dv(m_grabbers[0].center.data());
    glsafe(::glEnd());

    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;
    shader->start_using();
    shader->set_uniform("emission_factor", 0.1f);

    m_grabbers[0].color = GrabberColor;
    m_grabbers[0].render(m_hover_id == 0, (float)((box.size().x() + box.size().y() + box.size().z()) / 3.0));

    shader->stop_using();

    glsafe(::glPushMatrix());
    glsafe(::glTranslated(m_cut_contours.shift.x(), m_cut_contours.shift.y(), m_cut_contours.shift.z()));
    glsafe(::glLineWidth(2.0f));
    m_cut_contours.contours.render();
    glsafe(::glPopMatrix());
}

void GLGizmoCut::on_render_for_picking()
{
    glsafe(::glDisable(GL_DEPTH_TEST));
    render_grabbers_for_picking(m_parent.get_selection().get_bounding_box());
}

void GLGizmoCut::on_render_input_window(float x, float y, float bottom_limit)
{
    static float last_y = 0.0f;
    static float last_h = 0.0f;

    m_imgui->begin(_L("Cut"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    const bool imperial_units = wxGetApp().app_config->get("use_inches") == "1";

    // adjust window position to avoid overlap the view toolbar
    const float win_h = ImGui::GetWindowHeight();
    y = std::min(y, bottom_limit - win_h);
    ImGui::SetWindowPos(ImVec2(x, y), ImGuiCond_Always);
    if (last_h != win_h || last_y != y) {
        // ask canvas for another frame to render the window in the correct position
        m_parent.request_extra_frame();
        if (last_h != win_h)
            last_h = win_h;
        if (last_y != y)
            last_y = y;
    }

    ImGui::AlignTextToFramePadding();
    m_imgui->text("Z");
    ImGui::SameLine();
    ImGui::PushItemWidth(m_imgui->get_style_scaling() * 150.0f);

    double cut_z = m_cut_z;
    if (imperial_units)
        cut_z *= ObjectManipulation::mm_to_in;
    ImGui::InputDouble("", &cut_z, 0.0f, 0.0f, "%.2f", ImGuiInputTextFlags_CharsDecimal);

    ImGui::SameLine();
    m_imgui->text(imperial_units ? _L("in") : _L("mm"));

    m_cut_z = cut_z * (imperial_units ? ObjectManipulation::in_to_mm : 1.0);

    ImGui::Separator();

    m_imgui->checkbox(_L("Keep upper part"), m_keep_upper);
    m_imgui->checkbox(_L("Keep lower part"), m_keep_lower);
    m_imgui->checkbox(_L("Rotate lower part upwards"), m_rotate_lower);

    ImGui::Separator();

    m_imgui->disabled_begin((!m_keep_upper && !m_keep_lower) || m_cut_z <= 0.0 || m_max_z <= m_cut_z);
    const bool cut_clicked = m_imgui->button(_L("Perform cut"));
    m_imgui->disabled_end();

    m_imgui->end();

    if (cut_clicked && (m_keep_upper || m_keep_lower))
        perform_cut(m_parent.get_selection());
}

void GLGizmoCut::set_cut_z(double cut_z)
{
    // Clamp the plane to the object's bounding box
    m_cut_z = std::clamp(cut_z, 0.0, m_max_z);
}

void GLGizmoCut::perform_cut(const Selection& selection)
{
    const int instance_idx = selection.get_instance_idx();
    const int object_idx = selection.get_object_idx();

    wxCHECK_RET(instance_idx >= 0 && object_idx >= 0, "GLGizmoCut: Invalid object selection");

    // m_cut_z is the distance from the bed. Subtract possible SLA elevation.
    const GLVolume* first_glvolume = selection.get_volume(*selection.get_volume_idxs().begin());
    const double object_cut_z = m_cut_z - first_glvolume->get_sla_shift_z();

    if (0.0 < object_cut_z && object_cut_z < m_max_z)
        wxGetApp().plater()->cut(object_idx, instance_idx, object_cut_z,
            only_if(m_keep_upper, ModelObjectCutAttribute::KeepUpper) | 
            only_if(m_keep_lower, ModelObjectCutAttribute::KeepLower) | 
            only_if(m_rotate_lower, ModelObjectCutAttribute::FlipLower));
    else {
        // the object is SLA-elevated and the plane is under it.
    }
}

double GLGizmoCut::calc_projection(const Linef3& mouse_ray) const
{
    double projection = 0.0;

    const Vec3d starting_vec = m_drag_pos - m_drag_center;
    const double len_starting_vec = starting_vec.norm();
    if (len_starting_vec != 0.0) {
        const Vec3d mouse_dir = mouse_ray.unit_vector();
        // finds the intersection of the mouse ray with the plane parallel to the camera viewport and passing throught the starting position
        // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection algebric form
        // in our case plane normal and ray direction are the same (orthogonal view)
        // when moving to perspective camera the negative z unit axis of the camera needs to be transformed in world space and used as plane normal
        const Vec3d inters = mouse_ray.a + (m_drag_pos - mouse_ray.a).dot(mouse_dir) / mouse_dir.squaredNorm() * mouse_dir;
        // vector from the starting position to the found intersection
        const Vec3d inters_vec = inters - m_drag_pos;

        // finds projection of the vector along the staring direction
        projection = inters_vec.dot(starting_vec.normalized());
    }
    return projection;
}

BoundingBoxf3 GLGizmoCut::bounding_box() const
{
    BoundingBoxf3 ret;
    const Selection& selection = m_parent.get_selection();
    const Selection::IndicesList& idxs = selection.get_volume_idxs();
    for (unsigned int i : idxs) {
        const GLVolume* volume = selection.get_volume(i);
        if (!volume->is_modifier)
            ret.merge(volume->transformed_convex_hull_bounding_box());
    }
    return ret;
}

void GLGizmoCut::update_contours()
{
    const Selection& selection = m_parent.get_selection();
    const GLVolume* first_glvolume = selection.get_volume(*selection.get_volume_idxs().begin());
    const BoundingBoxf3& box = first_glvolume->transformed_convex_hull_bounding_box();

    const ModelObject* model_object = wxGetApp().model().objects[selection.get_object_idx()];
    const int instance_idx = selection.get_instance_idx();

    if (0.0 < m_cut_z && m_cut_z < m_max_z) {
        if (m_cut_contours.cut_z != m_cut_z || m_cut_contours.object_id != model_object->id() || m_cut_contours.instance_idx != instance_idx) {
            m_cut_contours.cut_z = m_cut_z;

            if (m_cut_contours.object_id != model_object->id())
                m_cut_contours.mesh = model_object->raw_mesh();

            m_cut_contours.position = box.center();
            m_cut_contours.shift = Vec3d::Zero();
            m_cut_contours.object_id = model_object->id();
            m_cut_contours.instance_idx = instance_idx;
            m_cut_contours.contours.reset();

            MeshSlicingParams slicing_params;
            slicing_params.trafo = first_glvolume->get_instance_transformation().get_matrix();
            const Polygons polys = slice_mesh(m_cut_contours.mesh.its, m_cut_z, slicing_params);
            if (!polys.empty()) {
                m_cut_contours.contours.init_from(polys, static_cast<float>(m_cut_z));
                m_cut_contours.contours.set_color(-1, { 1.0f, 1.0f, 1.0f, 1.0f });
            }
        }
        else if (box.center() != m_cut_contours.position) {
            m_cut_contours.shift = box.center() - m_cut_contours.position;
        }
    }
    else
        m_cut_contours.contours.reset();
}

} // namespace GUI
} // namespace Slic3r
