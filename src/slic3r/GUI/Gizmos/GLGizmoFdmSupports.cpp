// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoFdmSupports.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmos.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MeshUtils.hpp"
#include "slic3r/GUI/PresetBundle.hpp"



namespace Slic3r {
namespace GUI {

GLGizmoFdmSupports::GLGizmoFdmSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_quadric(nullptr)
    , m_its(nullptr)
{
    m_clipping_plane.reset(new ClippingPlane(Vec3d::Zero(), 0.));
    m_quadric = ::gluNewQuadric();
    if (m_quadric != nullptr)
        // using GLU_FILL does not work when the instance's transformation
        // contains mirroring (normals are reverted)
        ::gluQuadricDrawStyle(m_quadric, GLU_FILL);
}

GLGizmoFdmSupports::~GLGizmoFdmSupports()
{
    if (m_quadric != nullptr)
        ::gluDeleteQuadric(m_quadric);
}

bool GLGizmoFdmSupports::on_init()
{
    m_shortcut_key = WXK_CONTROL_L;

    m_desc["head_diameter"]    = _(L("Head diameter")) + ": ";
    m_desc["lock_supports"]    = _(L("Lock supports under new islands"));
    m_desc["remove_selected"]  = _(L("Remove selected points"));
    m_desc["remove_all"]       = _(L("Remove all points"));
    m_desc["apply_changes"]    = _(L("Apply changes"));
    m_desc["discard_changes"]  = _(L("Discard changes"));
    m_desc["minimal_distance"] = _(L("Minimal points distance")) + ": ";
    m_desc["points_density"]   = _(L("Support points density")) + ": ";
    m_desc["auto_generate"]    = _(L("Auto-generate points"));
    m_desc["manual_editing"]   = _(L("Manual editing"));
    m_desc["clipping_of_view"] = _(L("Clipping of view"))+ ": ";
    m_desc["reset_direction"]  = _(L("Reset direction"));

    return true;
}

void GLGizmoFdmSupports::set_fdm_support_data(ModelObject* model_object, const Selection& selection)
{
    if (! model_object || selection.is_empty()) {
        m_model_object = nullptr;
        return;
    }

    if (m_model_object != model_object || m_model_object_id != model_object->id())
        m_model_object = model_object;

    m_active_instance = selection.get_instance_idx();

    if (model_object && selection.is_from_single_instance())
    {
        // Cache the bb - it's needed for dealing with the clipping plane quite often
        // It could be done inside update_mesh but one has to account for scaling of the instance.
        //FIXME calling ModelObject::instance_bounding_box() is expensive!
        m_active_instance_bb_radius = m_model_object->instance_bounding_box(m_active_instance).radius();

        if (is_mesh_update_necessary())
            update_mesh();

        if (m_state == On) {
            m_parent.toggle_model_objects_visibility(false);
            m_parent.toggle_model_objects_visibility(true, m_model_object, m_active_instance);
        }
        else
            m_parent.toggle_model_objects_visibility(true, nullptr, -1);
    }
}



void GLGizmoFdmSupports::on_render() const
{
    const Selection& selection = m_parent.get_selection();

    // If current m_model_object does not match selection, ask GLCanvas3D to turn us off
    if (m_state == On
     && (m_model_object != selection.get_model()->objects[selection.get_object_idx()]
      || m_active_instance != selection.get_instance_idx()
      || m_model_object_id != m_model_object->id())) {
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_RESETGIZMOS));
        return;
    }

    if (! m_its || ! m_mesh)
        const_cast<GLGizmoFdmSupports*>(this)->update_mesh();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));


    render_clipping_plane(selection);

    glsafe(::glDisable(GL_BLEND));
}



void GLGizmoFdmSupports::render_clipping_plane(const Selection& selection) const
{
    if (m_clipping_plane_distance == 0.f)
        return;

    // Get transformation of the instance
    const GLVolume* vol = selection.get_volume(*selection.get_volume_idxs().begin());
    Geometry::Transformation trafo = vol->get_instance_transformation();


    // Now initialize the TMS for the object, perform the cut and save the result.
    if (! m_object_clipper) {
        m_object_clipper.reset(new MeshClipper);
        m_object_clipper->set_mesh(*m_mesh);
    }
    m_object_clipper->set_plane(*m_clipping_plane);
    m_object_clipper->set_transformation(trafo);

    // At this point we have the triangulated cuts for both the object and supports - let's render.
    if (! m_object_clipper->get_triangles().empty()) {
		::glPushMatrix();
        ::glColor3f(1.0f, 0.37f, 0.0f);
        ::glBegin(GL_TRIANGLES);
        for (const Vec3f& point : m_object_clipper->get_triangles())
            ::glVertex3f(point(0), point(1), point(2));
        ::glEnd();
		::glPopMatrix();
	}
}


void GLGizmoFdmSupports::on_render_for_picking() const
{

}




bool GLGizmoFdmSupports::is_point_clipped(const Vec3d& point) const
{
    if (m_clipping_plane_distance == 0.f)
        return false;

    Vec3d transformed_point = m_model_object->instances.front()->get_transformation().get_matrix() * point;
    return m_clipping_plane->distance(transformed_point) < 0.;
}



bool GLGizmoFdmSupports::is_mesh_update_necessary() const
{
    return ((m_state == On) && (m_model_object != nullptr) && !m_model_object->instances.empty())
        && ((m_model_object->id() != m_model_object_id) || m_its == nullptr);
}



void GLGizmoFdmSupports::update_mesh()
{
    if (! m_model_object)
        return;

    wxBusyCursor wait;
    // this way we can use that mesh directly.
    // This mesh does not account for the possible Z up SLA offset.
    m_mesh = &m_model_object->volumes.front()->mesh();
    m_its = &m_mesh->its;

    // If this is different mesh than last time or if the AABB tree is uninitialized, recalculate it.
    if (m_model_object_id != m_model_object->id() || ! m_mesh_raycaster)
        m_mesh_raycaster.reset(new MeshRaycaster(*m_mesh));

    m_model_object_id = m_model_object->id();
}



// Unprojects the mouse position on the mesh and saves hit point and normal of the facet into pos_and_normal
// Return false if no intersection was found, true otherwise.
bool GLGizmoFdmSupports::unproject_on_mesh(const Vec2d& mouse_pos, std::pair<Vec3f, Vec3f>& pos_and_normal)
{
    // if the gizmo doesn't have the V, F structures for igl, calculate them first:
    if (! m_mesh_raycaster)
        update_mesh();

    const Camera& camera = m_parent.get_camera();
    const Selection& selection = m_parent.get_selection();
    const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
    Geometry::Transformation trafo = volume->get_instance_transformation();
    trafo.set_offset(trafo.get_offset());

    // The raycaster query
    Vec3f hit;
    Vec3f normal;
    if (m_mesh_raycaster->unproject_on_mesh(mouse_pos, trafo.get_matrix(), camera, hit, normal, m_clipping_plane.get())) {
        // Return both the point and the facet normal.
        pos_and_normal = std::make_pair(hit, normal);
        return true;
    }
    else
        return false;
}

// Following function is called from GLCanvas3D to inform the gizmo about a mouse/keyboard event.
// The gizmo has an opportunity to react - if it does, it should return true so that the Canvas3D is
// aware that the event was reacted to and stops trying to make different sense of it. If the gizmo
// concludes that the event was not intended for it, it should return false.
bool GLGizmoFdmSupports::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    if (action == SLAGizmoEventType::MouseWheelUp && control_down) {
        m_clipping_plane_distance = std::min(1.f, m_clipping_plane_distance + 0.01f);
        update_clipping_plane(true);
        return true;
    }

    if (action == SLAGizmoEventType::MouseWheelDown && control_down) {
        m_clipping_plane_distance = std::max(0.f, m_clipping_plane_distance - 0.01f);
        update_clipping_plane(true);
        return true;
    }

    if (action == SLAGizmoEventType::ResetClippingPlane) {
        update_clipping_plane();
        return true;
    }

    return false;
}




ClippingPlane GLGizmoFdmSupports::get_fdm_clipping_plane() const
{
    if (!m_model_object || m_state == Off || m_clipping_plane_distance == 0.f)
        return ClippingPlane::ClipsNothing();
    else
        return ClippingPlane(-m_clipping_plane->get_normal(), m_clipping_plane->get_data()[3]);
}



void GLGizmoFdmSupports::on_render_input_window(float x, float y, float bottom_limit)
{
    if (!m_model_object)
        return;

    const float approx_height = m_imgui->scaled(18.0f);
    y = std::min(y, bottom_limit - approx_height);
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);
    m_imgui->set_next_window_bg_alpha(0.5f);
    m_imgui->begin(on_get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:

    const float settings_sliders_left = std::max(m_imgui->calc_text_size(m_desc.at("minimal_distance")).x, m_imgui->calc_text_size(m_desc.at("points_density")).x) + m_imgui->scaled(1.f);
    const float clipping_slider_left = std::max(m_imgui->calc_text_size(m_desc.at("clipping_of_view")).x, m_imgui->calc_text_size(m_desc.at("reset_direction")).x) + m_imgui->scaled(1.5f);
    const float diameter_slider_left = m_imgui->calc_text_size(m_desc.at("head_diameter")).x + m_imgui->scaled(1.f);
    const float minimal_slider_width = m_imgui->scaled(4.f);
    const float buttons_width_approx = m_imgui->calc_text_size(m_desc.at("apply_changes")).x + m_imgui->calc_text_size(m_desc.at("discard_changes")).x + m_imgui->scaled(1.5f);
    const float lock_supports_width_approx = m_imgui->calc_text_size(m_desc.at("lock_supports")).x + m_imgui->scaled(2.f);

    float window_width = minimal_slider_width + std::max(std::max(settings_sliders_left, clipping_slider_left), diameter_slider_left);
    window_width = std::max(std::max(window_width, buttons_width_approx), lock_supports_width_approx);


    // Following is rendered in both editing and non-editing mode:
    m_imgui->text("");
    if (m_clipping_plane_distance == 0.f)
        m_imgui->text(m_desc.at("clipping_of_view"));
    else {
        if (m_imgui->button(m_desc.at("reset_direction"))) {
            wxGetApp().CallAfter([this](){
                    update_clipping_plane();
                });
        }
    }

    ImGui::SameLine(clipping_slider_left);
    ImGui::PushItemWidth(window_width - clipping_slider_left);
    if (ImGui::SliderFloat("  ", &m_clipping_plane_distance, 0.f, 1.f, "%.2f"))
        update_clipping_plane(true);


    m_imgui->end();
}

bool GLGizmoFdmSupports::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();

    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptFFF
        || !selection.is_from_single_instance())
        return false;

    // Check that none of the selected volumes is outside. Only SLA auxiliaries (supports) are allowed outside.
    const Selection::IndicesList& list = selection.get_volume_idxs();
    for (const auto& idx : list)
        if (selection.get_volume(idx)->is_outside)
            return false;

    return true;
}

bool GLGizmoFdmSupports::on_is_selectable() const
{
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF );
}

std::string GLGizmoFdmSupports::on_get_name() const
{
    return (_(L("FDM Support Editing")) + " [L]").ToUTF8().data();
}



void GLGizmoFdmSupports::on_set_state()
{
    if (m_state == On)
        std::cout << "zapinam se..." << std::endl;
    else
        std::cout << "vypinam se..." << std::endl;
    return;
    // m_model_object pointer can be invalid (for instance because of undo/redo action),
    // we should recover it from the object id
    m_model_object = nullptr;
    for (const auto mo : wxGetApp().model().objects) {
        if (mo->id() == m_model_object_id) {
            m_model_object = mo;
            break;
        }
    }

    if (m_state == m_old_state)
        return;

    if (m_state == On && m_old_state != On) { // the gizmo was just turned on
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("SLA gizmo turned on")));
        if (is_mesh_update_necessary())
            update_mesh();

        // we'll now reload support points:
        if (m_model_object)
            ;// !!!! reload_cache();

        m_parent.toggle_model_objects_visibility(false);
        if (m_model_object)
            m_parent.toggle_model_objects_visibility(true, m_model_object, m_active_instance);
    }
    if (m_state == Off && m_old_state != Off) { // the gizmo was just turned Off
        // we are actually shutting down
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("FDM gizmo turned off")));
        m_parent.toggle_model_objects_visibility(true);
        m_clipping_plane_distance = 0.f;
        // Release clippers and the AABB raycaster.
        m_its = nullptr;
        m_object_clipper.reset();
        m_mesh_raycaster.reset();
    }
    m_old_state = m_state;
}



void GLGizmoFdmSupports::on_start_dragging()
{

}


void GLGizmoFdmSupports::on_stop_dragging()
{

}



void GLGizmoFdmSupports::on_load(cereal::BinaryInputArchive& ar)
{

}



void GLGizmoFdmSupports::on_save(cereal::BinaryOutputArchive& ar) const
{

}


void GLGizmoFdmSupports::update_clipping_plane(bool keep_normal) const
{
    Vec3d normal = (keep_normal && m_clipping_plane->get_normal() != Vec3d::Zero() ?
                        m_clipping_plane->get_normal() : -m_parent.get_camera().get_dir_forward());

    const Vec3d& center = m_model_object->instances[m_active_instance]->get_offset();
    float dist = normal.dot(center);
    *m_clipping_plane = ClippingPlane(normal, (dist - (-m_active_instance_bb_radius) - m_clipping_plane_distance * 2*m_active_instance_bb_radius));
    m_parent.set_as_dirty();
}




} // namespace GUI
} // namespace Slic3r
