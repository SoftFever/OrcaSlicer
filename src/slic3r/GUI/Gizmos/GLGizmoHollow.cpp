// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoHollow.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmos.hpp"

#include <GL/glew.h>

//#include <wx/msgdlg.h>
//#include <wx/settings.h>
//#include <wx/stattext.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_ObjectSettings.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/MeshUtils.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/PresetBundle.hpp"
#include "libslic3r/SLAPrint.hpp"


namespace Slic3r {
namespace GUI {

GLGizmoHollow::GLGizmoHollow(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
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

GLGizmoHollow::~GLGizmoHollow()
{
    if (m_quadric != nullptr)
        ::gluDeleteQuadric(m_quadric);
}

bool GLGizmoHollow::on_init()
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

void GLGizmoHollow::set_sla_support_data(ModelObject* model_object, const Selection& selection)
{
    if (! model_object || selection.is_empty()) {
        m_model_object = nullptr;
        return;
    }

    if (m_model_object != model_object || m_model_object_id != model_object->id()) {
        m_model_object = model_object;
        m_print_object_idx = -1;
    }

    m_active_instance = selection.get_instance_idx();

    if (model_object && selection.is_from_single_instance())
    {
        // Cache the bb - it's needed for dealing with the clipping plane quite often
        // It could be done inside update_mesh but one has to account for scaling of the instance.
        //FIXME calling ModelObject::instance_bounding_box() is expensive!
        m_active_instance_bb_radius = m_model_object->instance_bounding_box(m_active_instance).radius();

        if (is_mesh_update_necessary()) {
            update_mesh();
            reload_cache();
        }

        if (m_state == On) {
            m_parent.toggle_model_objects_visibility(false);
            m_parent.toggle_model_objects_visibility(true, m_model_object, m_active_instance);
        }
        else
            m_parent.toggle_model_objects_visibility(true, nullptr, -1);
    }
}



void GLGizmoHollow::on_render() const
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
        const_cast<GLGizmoHollow*>(this)->update_mesh();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    m_z_shift = selection.get_volume(*selection.get_volume_idxs().begin())->get_sla_shift_z();

    if (m_quadric != nullptr && selection.is_from_single_instance())
        render_points(selection, false);

    m_selection_rectangle.render(m_parent);
    render_clipping_plane(selection);

    glsafe(::glDisable(GL_BLEND));
}



void GLGizmoHollow::render_clipping_plane(const Selection& selection) const
{
    if (m_clipping_plane_distance == 0.f)
        return;

    // Get transformation of the instance
    const GLVolume* vol = selection.get_volume(*selection.get_volume_idxs().begin());
    Geometry::Transformation trafo = vol->get_instance_transformation();
    trafo.set_offset(trafo.get_offset() + Vec3d(0., 0., m_z_shift));

    // Get transformation of supports
    Geometry::Transformation supports_trafo;
    supports_trafo.set_offset(Vec3d(trafo.get_offset()(0), trafo.get_offset()(1), vol->get_sla_shift_z()));
    supports_trafo.set_rotation(Vec3d(0., 0., trafo.get_rotation()(2)));
    // I don't know why, but following seems to be correct.
    supports_trafo.set_mirror(Vec3d(trafo.get_mirror()(0) * trafo.get_mirror()(1) * trafo.get_mirror()(2),
                                    1,
                                    1.));

    // Now initialize the TMS for the object, perform the cut and save the result.
    if (! m_object_clipper) {
        m_object_clipper.reset(new MeshClipper);
        m_object_clipper->set_mesh(*m_mesh);
    }
    m_object_clipper->set_plane(*m_clipping_plane);
    m_object_clipper->set_transformation(trafo);


    // Next, ask the backend if supports are already calculated. If so, we are gonna cut them too.
    // First we need a pointer to the respective SLAPrintObject. The index into objects vector is
    // cached so we don't have todo it on each render. We only search for the po if needed:
    if (m_print_object_idx < 0 || (int)m_parent.sla_print()->objects().size() != m_print_objects_count) {
        m_print_objects_count = m_parent.sla_print()->objects().size();
        m_print_object_idx = -1;
        for (const SLAPrintObject* po : m_parent.sla_print()->objects()) {
            ++m_print_object_idx;
            if (po->model_object()->id() == m_model_object->id())
                break;
        }
    }
    if (m_print_object_idx >= 0) {
        const SLAPrintObject* print_object = m_parent.sla_print()->objects()[m_print_object_idx];

        if (print_object->is_step_done(slaposSupportTree)) {
            // If the supports are already calculated, save the timestamp of the respective step
            // so we can later tell they were recalculated.
            size_t timestamp = print_object->step_state_with_timestamp(slaposSupportTree).timestamp;

            if (! m_supports_clipper || (int)timestamp != m_old_timestamp) {
                // The timestamp has changed.
                m_supports_clipper.reset(new MeshClipper);
                // The mesh should already have the shared vertices calculated.
                m_supports_clipper->set_mesh(print_object->support_mesh());
                m_old_timestamp = timestamp;
            }
            m_supports_clipper->set_plane(*m_clipping_plane);
            m_supports_clipper->set_transformation(supports_trafo);
        }
        else
            // The supports are not valid. We better dump the cached data.
            m_supports_clipper.reset();
    }

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

    if (m_supports_clipper && ! m_supports_clipper->get_triangles().empty() && !m_editing_mode) {
        // The supports are hidden in the editing mode, so it makes no sense to render the cuts.
        ::glPushMatrix();
        ::glColor3f(1.0f, 0.f, 0.37f);
        ::glBegin(GL_TRIANGLES);
        for (const Vec3f& point : m_supports_clipper->get_triangles())
            ::glVertex3f(point(0), point(1), point(2));
        ::glEnd();
        ::glPopMatrix();
    }
}


void GLGizmoHollow::on_render_for_picking() const
{
    const Selection& selection = m_parent.get_selection();
#if ENABLE_RENDER_PICKING_PASS
    m_z_shift = selection.get_volume(*selection.get_volume_idxs().begin())->get_sla_shift_z();
#endif

    glsafe(::glEnable(GL_DEPTH_TEST));
    render_points(selection, true);
}

void GLGizmoHollow::render_points(const Selection& selection, bool picking) const
{
    if (!picking)
        glsafe(::glEnable(GL_LIGHTING));

    const GLVolume* vol = selection.get_volume(*selection.get_volume_idxs().begin());
    const Transform3d& instance_scaling_matrix_inverse = vol->get_instance_transformation().get_matrix(true, true, false, true).inverse();
    const Transform3d& instance_matrix = vol->get_instance_transformation().get_matrix();

    glsafe(::glPushMatrix());
    glsafe(::glTranslated(0.0, 0.0, m_z_shift));
    glsafe(::glMultMatrixd(instance_matrix.data()));

    float render_color[4];
    size_t cache_size = m_editing_mode ? m_editing_cache.size() : m_normal_cache.size();
    for (size_t i = 0; i < cache_size; ++i)
    {
        const sla::SupportPoint& support_point = m_editing_mode ? m_editing_cache[i].support_point : m_normal_cache[i];
        const bool& point_selected = m_editing_mode ? m_editing_cache[i].selected : false;

        if (is_mesh_point_clipped(support_point.pos.cast<double>()))
            continue;

        // First decide about the color of the point.
        if (picking) {
            std::array<float, 4> color = picking_color_component(i);
            render_color[0] = color[0];
            render_color[1] = color[1];
            render_color[2] = color[2];
            render_color[3] = color[3];
        }
        else {
            render_color[3] = 1.f;
            if ((size_t(m_hover_id) == i && m_editing_mode)) { // ignore hover state unless editing mode is active
                render_color[0] = 0.f;
                render_color[1] = 1.0f;
                render_color[2] = 1.0f;
            }
            else { // neigher hover nor picking
                render_color[0] = point_selected ? 1.0f : 0.7f;
                render_color[1] = point_selected ? 0.3f : 0.7f;
                render_color[2] = point_selected ? 0.3f : 0.7f;
                render_color[3] = 0.5f;
            }
        }
        glsafe(::glColor4fv(render_color));
        float render_color_emissive[4] = { 0.5f * render_color[0], 0.5f * render_color[1], 0.5f * render_color[2], 1.f};
        glsafe(::glMaterialfv(GL_FRONT, GL_EMISSION, render_color_emissive));

        // Inverse matrix of the instance scaling is applied so that the mark does not scale with the object.
        glsafe(::glPushMatrix());
        glsafe(::glTranslatef(support_point.pos(0), support_point.pos(1), support_point.pos(2)));
        glsafe(::glMultMatrixd(instance_scaling_matrix_inverse.data()));

        if (vol->is_left_handed())
            glFrontFace(GL_CW);

        // Matrices set, we can render the point mark now.
        // If in editing mode, we'll also render a cone pointing to the sphere.
        if (m_editing_mode) {
            // in case the normal is not yet cached, find and cache it
            if (m_editing_cache[i].normal == Vec3f::Zero())
                m_mesh_raycaster->get_closest_point(m_editing_cache[i].support_point.pos, &m_editing_cache[i].normal);

            Eigen::Quaterniond q;
            q.setFromTwoVectors(Vec3d{0., 0., 1.}, instance_scaling_matrix_inverse * m_editing_cache[i].normal.cast<double>());
            Eigen::AngleAxisd aa(q);
            glsafe(::glRotated(aa.angle() * (180. / M_PI), aa.axis()(0), aa.axis()(1), aa.axis()(2)));

            const double cone_radius = double(support_point.head_front_radius) * RenderPointScale; //0.25; // mm
            const double cone_height = m_new_cone_height;
            //const double cone_rad_diff = m_new_cone_angle*(cone_radius/(cone_height/2.))*(cone_height/2.);
            const double cone_rad_diff = m_new_cone_angle*cone_radius;
            glsafe(::glPushMatrix());
            glsafe(::glTranslatef(0.f, 0.f, -cone_height/2.));
            //::gluCylinder(m_quadric, cone_radius, cone_radius, cone_height, 24, 1);
            ::gluCylinder(m_quadric, cone_radius+cone_rad_diff, cone_radius-cone_rad_diff, cone_height, 24, 1);
            glsafe(::glTranslatef(0.f, 0.f, cone_height));
            //::gluDisk(m_quadric, 0.0, cone_radius, 24, 1);
            ::gluDisk(m_quadric, 0.0, cone_radius-cone_rad_diff, 24, 1);
            glsafe(::glPopMatrix());
        }
        //::gluSphere(m_quadric, (double)support_point.head_front_radius * RenderPointScale, 24, 12);
        if (vol->is_left_handed())
            glFrontFace(GL_CCW);

        glsafe(::glPopMatrix());
    }

    {
        // Reset emissive component to zero (the default value)
        float render_color_emissive[4] = { 0.f, 0.f, 0.f, 1.f };
        glsafe(::glMaterialfv(GL_FRONT, GL_EMISSION, render_color_emissive));
    }

    if (!picking)
        glsafe(::glDisable(GL_LIGHTING));

    glsafe(::glPopMatrix());
}



bool GLGizmoHollow::is_mesh_point_clipped(const Vec3d& point) const
{
    if (m_clipping_plane_distance == 0.f)
        return false;

    Vec3d transformed_point = m_model_object->instances.front()->get_transformation().get_matrix() * point;
    transformed_point(2) += m_z_shift;
    return m_clipping_plane->is_point_clipped(transformed_point);
}



bool GLGizmoHollow::is_mesh_update_necessary() const
{
    return ((m_state == On) && (m_model_object != nullptr) && !m_model_object->instances.empty())
        && ((m_model_object->id() != m_model_object_id) || m_its == nullptr);
}



void GLGizmoHollow::update_mesh()
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
bool GLGizmoHollow::unproject_on_mesh(const Vec2d& mouse_pos, std::pair<Vec3f, Vec3f>& pos_and_normal)
{
    // if the gizmo doesn't have the V, F structures for igl, calculate them first:
    if (! m_mesh_raycaster)
        update_mesh();

    const Camera& camera = m_parent.get_camera();
    const Selection& selection = m_parent.get_selection();
    const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
    Geometry::Transformation trafo = volume->get_instance_transformation();
    trafo.set_offset(trafo.get_offset() + Vec3d(0., 0., m_z_shift));

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
bool GLGizmoHollow::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    if (m_editing_mode) {

        // left down with shift - show the selection rectangle:
        if (action == SLAGizmoEventType::LeftDown && (shift_down || alt_down || control_down)) {
            if (m_hover_id == -1) {
                if (shift_down || alt_down) {
                    m_selection_rectangle.start_dragging(mouse_position, shift_down ? GLSelectionRectangle::Select : GLSelectionRectangle::Deselect);
                }
            }
            else {
                if (m_editing_cache[m_hover_id].selected)
                    unselect_point(m_hover_id);
                else {
                    if (!alt_down)
                        select_point(m_hover_id);
                }
            }

            return true;
        }

        // left down without selection rectangle - place point on the mesh:
        if (action == SLAGizmoEventType::LeftDown && !m_selection_rectangle.is_dragging() && !shift_down) {
            // If any point is in hover state, this should initiate its move - return control back to GLCanvas:
            if (m_hover_id != -1)
                return false;

            // If there is some selection, don't add new point and deselect everything instead.
            if (m_selection_empty) {
                std::pair<Vec3f, Vec3f> pos_and_normal;
                if (unproject_on_mesh(mouse_position, pos_and_normal)) { // we got an intersection
                    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Add support point")));
                    m_editing_cache.emplace_back(sla::SupportPoint(pos_and_normal.first, m_new_point_head_diameter/2.f, false), false, pos_and_normal.second);
                    m_parent.set_as_dirty();
                    m_wait_for_up_event = true;
                }
                else
                    return false;
            }
            else
                select_point(NoPoints);

            return true;
        }

        // left up with selection rectangle - select points inside the rectangle:
        if ((action == SLAGizmoEventType::LeftUp || action == SLAGizmoEventType::ShiftUp || action == SLAGizmoEventType::AltUp) && m_selection_rectangle.is_dragging()) {
            // Is this a selection or deselection rectangle?
            GLSelectionRectangle::EState rectangle_status = m_selection_rectangle.get_state();

            // First collect positions of all the points in world coordinates.
            Geometry::Transformation trafo = m_model_object->instances[m_active_instance]->get_transformation();
            trafo.set_offset(trafo.get_offset() + Vec3d(0., 0., m_z_shift));
            std::vector<Vec3d> points;
            for (unsigned int i=0; i<m_editing_cache.size(); ++i)
                points.push_back(trafo.get_matrix() * m_editing_cache[i].support_point.pos.cast<double>());

            // Now ask the rectangle which of the points are inside.
            std::vector<Vec3f> points_inside;
            std::vector<unsigned int> points_idxs = m_selection_rectangle.stop_dragging(m_parent, points);
            for (size_t idx : points_idxs)
                points_inside.push_back(points[idx].cast<float>());

            // Only select/deselect points that are actually visible
            for (size_t idx :  m_mesh_raycaster->get_unobscured_idxs(trafo, m_parent.get_camera(), points_inside, m_clipping_plane.get()))
            {
                if (rectangle_status == GLSelectionRectangle::Deselect)
                    unselect_point(points_idxs[idx]);
                else
                    select_point(points_idxs[idx]);
            }
            return true;
        }

        // left up with no selection rectangle
        if (action == SLAGizmoEventType::LeftUp) {
            if (m_wait_for_up_event) {
                m_wait_for_up_event = false;
                return true;
            }
        }

        // dragging the selection rectangle:
        if (action == SLAGizmoEventType::Dragging) {
            if (m_wait_for_up_event)
                return true; // point has been placed and the button not released yet
                             // this prevents GLCanvas from starting scene rotation

            if (m_selection_rectangle.is_dragging())  {
                m_selection_rectangle.dragging(mouse_position);
                return true;
            }

            return false;
        }

        if (action == SLAGizmoEventType::Delete) {
            // delete key pressed
            delete_selected_points();
            return true;
        }

        if (action == SLAGizmoEventType::RightDown) {
            if (m_hover_id != -1) {
                select_point(NoPoints);
                select_point(m_hover_id);
                delete_selected_points();
                return true;
            }
            return false;
        }

        if (action == SLAGizmoEventType::SelectAll) {
            select_point(AllPoints);
            return true;
        }
    }

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

void GLGizmoHollow::delete_selected_points(bool force)
{
    if (! m_editing_mode) {
        std::cout << "DEBUGGING: delete_selected_points called out of editing mode!" << std::endl;
        std::abort();
    }

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Delete support point")));

    for (unsigned int idx=0; idx<m_editing_cache.size(); ++idx) {
        if (m_editing_cache[idx].selected) {
            m_editing_cache.erase(m_editing_cache.begin() + (idx--));
        }
    }

    select_point(NoPoints);

    //m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
}

void GLGizmoHollow::on_update(const UpdateData& data)
{
    if (! m_editing_mode)
        return;
    else {
        if (m_hover_id != -1) {
            std::pair<Vec3f, Vec3f> pos_and_normal;
            if (! unproject_on_mesh(data.mouse_pos.cast<double>(), pos_and_normal))
                return;
            m_editing_cache[m_hover_id].support_point.pos = pos_and_normal.first;
            m_editing_cache[m_hover_id].support_point.is_new_island = false;
            m_editing_cache[m_hover_id].normal = pos_and_normal.second;
            // Do not update immediately, wait until the mouse is released.
            // m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
        }
    }
}

std::vector<const ConfigOption*> GLGizmoHollow::get_config_options(const std::vector<std::string>& keys) const
{
    std::vector<const ConfigOption*> out;

    if (!m_model_object)
        return out;

    const DynamicPrintConfig& object_cfg = m_model_object->config;
    const DynamicPrintConfig& print_cfg = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
    std::unique_ptr<DynamicPrintConfig> default_cfg = nullptr;

    for (const std::string& key : keys) {
        if (object_cfg.has(key))
            out.push_back(object_cfg.option(key));
        else
            if (print_cfg.has(key))
                out.push_back(print_cfg.option(key));
            else { // we must get it from defaults
                if (default_cfg == nullptr)
                    default_cfg.reset(DynamicPrintConfig::new_from_defaults_keys(keys));
                out.push_back(default_cfg->option(key));
            }
    }

    return out;
}


ClippingPlane GLGizmoHollow::get_sla_clipping_plane() const
{
    if (!m_model_object || m_state == Off || m_clipping_plane_distance == 0.f)
        return ClippingPlane::ClipsNothing();
    else
        return ClippingPlane(-m_clipping_plane->get_normal(), m_clipping_plane->get_data()[3]);
}


void GLGizmoHollow::on_render_input_window(float x, float y, float bottom_limit)
{
    if (!m_model_object)
        return;

    bool first_run = true; // This is a hack to redraw the button when all points are removed,
                           // so it is not delayed until the background process finishes.
RENDER_AGAIN:
    //m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);
    //const ImVec2 window_size(m_imgui->scaled(18.f, 16.f));
    //ImGui::SetNextWindowPos(ImVec2(x, y - std::max(0.f, y+window_size.y-bottom_limit) ));
    //ImGui::SetNextWindowSize(ImVec2(window_size));

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


    bool force_refresh = false;
    bool remove_selected = false;
    bool remove_all = false;

    if (m_editing_mode) {

        float diameter_upper_cap = static_cast<ConfigOptionFloat*>(wxGetApp().preset_bundle->sla_prints.get_edited_preset().config.option("support_pillar_diameter"))->value;
        if (m_new_point_head_diameter > diameter_upper_cap)
            m_new_point_head_diameter = diameter_upper_cap;
        m_imgui->text(m_desc.at("head_diameter"));
        ImGui::SameLine(diameter_slider_left);
        ImGui::PushItemWidth(window_width - diameter_slider_left);

        // Following is a nasty way to:
        //  - save the initial value of the slider before one starts messing with it
        //  - keep updating the head radius during sliding so it is continuosly refreshed in 3D scene
        //  - take correct undo/redo snapshot after the user is done with moving the slider
        float initial_value = m_new_point_head_diameter;
        ImGui::SliderFloat("", &m_new_point_head_diameter, 0.1f, diameter_upper_cap, "%.1f");
        if (ImGui::IsItemClicked()) {
            if (m_old_point_head_diameter == 0.f)
                m_old_point_head_diameter = initial_value;
        }
        if (ImGui::IsItemEdited()) {
            for (auto& cache_entry : m_editing_cache)
                if (cache_entry.selected)
                    cache_entry.support_point.head_front_radius = m_new_point_head_diameter / 2.f;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            // momentarily restore the old value to take snapshot
            for (auto& cache_entry : m_editing_cache)
                if (cache_entry.selected)
                    cache_entry.support_point.head_front_radius = m_old_point_head_diameter / 2.f;
            float backup = m_new_point_head_diameter;
            m_new_point_head_diameter = m_old_point_head_diameter;
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Change point head diameter")));
            m_new_point_head_diameter = backup;
            for (auto& cache_entry : m_editing_cache)
                if (cache_entry.selected)
                    cache_entry.support_point.head_front_radius = m_new_point_head_diameter / 2.f;
            m_old_point_head_diameter = 0.f;
        }

        // !!!! Something as above should be done for the cone angle
        ImGui::SliderFloat(" ", &m_new_cone_angle, -1.f, 1.f, "%.1f");
        ImGui::SliderFloat("  ", &m_new_cone_height, 0.1f, 10.f, "%.1f");

        m_imgui->disabled_begin(m_selection_empty);
        remove_selected = m_imgui->button(m_desc.at("remove_selected"));
        m_imgui->disabled_end();

        m_imgui->disabled_begin(m_editing_cache.empty());
        remove_all = m_imgui->button(m_desc.at("remove_all"));
        m_imgui->disabled_end();

        m_imgui->text(" "); // vertical gap
    }
    else { // not in editing mode:
        m_imgui->text(m_desc.at("minimal_distance"));
        ImGui::SameLine(settings_sliders_left);
        ImGui::PushItemWidth(window_width - settings_sliders_left);

        std::vector<const ConfigOption*> opts = get_config_options({"support_points_density_relative", "support_points_minimal_distance"});
        float density = static_cast<const ConfigOptionInt*>(opts[0])->value;
        float minimal_point_distance = static_cast<const ConfigOptionFloat*>(opts[1])->value;

        ImGui::SliderFloat("", &minimal_point_distance, 0.f, 20.f, "%.f mm");
        bool slider_clicked = ImGui::IsItemClicked(); // someone clicked the slider
        bool slider_edited = ImGui::IsItemEdited(); // someone is dragging the slider
        bool slider_released = ImGui::IsItemDeactivatedAfterEdit(); // someone has just released the slider

        m_imgui->text(m_desc.at("points_density"));
        ImGui::SameLine(settings_sliders_left);

        ImGui::SliderFloat(" ", &density, 0.f, 200.f, "%.f %%");
        slider_clicked |= ImGui::IsItemClicked();
        slider_edited |= ImGui::IsItemEdited();
        slider_released |= ImGui::IsItemDeactivatedAfterEdit();

        if (slider_clicked) { // stash the values of the settings so we know what to revert to after undo
            m_minimal_point_distance_stash = minimal_point_distance;
            m_density_stash = density;
        }
        if (slider_edited) {
            m_model_object->config.opt<ConfigOptionFloat>("support_points_minimal_distance", true)->value = minimal_point_distance;
            m_model_object->config.opt<ConfigOptionInt>("support_points_density_relative", true)->value = (int)density;
        }
        if (slider_released) {
            m_model_object->config.opt<ConfigOptionFloat>("support_points_minimal_distance", true)->value = m_minimal_point_distance_stash;
            m_model_object->config.opt<ConfigOptionInt>("support_points_density_relative", true)->value = (int)m_density_stash;
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Support parameter change")));
            m_model_object->config.opt<ConfigOptionFloat>("support_points_minimal_distance", true)->value = minimal_point_distance;
            m_model_object->config.opt<ConfigOptionInt>("support_points_density_relative", true)->value = (int)density;
            wxGetApp().obj_list()->update_and_show_object_settings_item();
        }


        m_imgui->disabled_begin(m_normal_cache.empty());
        remove_all = m_imgui->button(m_desc.at("remove_all"));
        m_imgui->disabled_end();

    }


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
    if (ImGui::SliderFloat("   ", &m_clipping_plane_distance, 0.f, 1.f, "%.2f"))
        update_clipping_plane(true);


    if (m_imgui->button("?")) {
        wxGetApp().CallAfter([]() {
            SlaGizmoHelpDialog help_dlg;
            help_dlg.ShowModal();
        });
    }

    m_imgui->end();

    if (m_editing_mode != m_old_editing_state) { // user toggled between editing/non-editing mode
        m_parent.toggle_sla_auxiliaries_visibility(!m_editing_mode, m_model_object, m_active_instance);
        force_refresh = true;
    }
    m_old_editing_state = m_editing_mode;

    if (remove_selected || remove_all) {
        force_refresh = false;
        m_parent.set_as_dirty();
        bool was_in_editing = m_editing_mode;
        if (remove_all) {
            select_point(AllPoints);
            delete_selected_points(true); // true - delete regardless of locked status
        }
        if (remove_selected)
            delete_selected_points(false); // leave locked points

        if (first_run) {
            first_run = false;
            goto RENDER_AGAIN;
        }
    }

    if (force_refresh)
        m_parent.set_as_dirty();
}

bool GLGizmoHollow::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();

    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA
        || !selection.is_from_single_instance())
        return false;

    // Check that none of the selected volumes is outside. Only SLA auxiliaries (supports) are allowed outside.
    const Selection::IndicesList& list = selection.get_volume_idxs();
    for (const auto& idx : list)
        if (selection.get_volume(idx)->is_outside && selection.get_volume(idx)->composite_id.volume_id >= 0)
            return false;

    return true;
}

bool GLGizmoHollow::on_is_selectable() const
{
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA);
}

std::string GLGizmoHollow::on_get_name() const
{
    return (_(L("Hollowing")) + " [H]").ToUTF8().data();
}



void GLGizmoHollow::on_set_state()
{
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
            reload_cache();

        m_parent.toggle_model_objects_visibility(false);
        if (m_model_object)
            m_parent.toggle_model_objects_visibility(true, m_model_object, m_active_instance);

        // Set default head diameter from config.
        const DynamicPrintConfig& cfg = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
        m_new_point_head_diameter = static_cast<const ConfigOptionFloat*>(cfg.option("support_head_front_diameter"))->value;
    }
    if (m_state == Off && m_old_state != Off) { // the gizmo was just turned Off
        bool will_ask = m_model_object && false;
        if (will_ask) {
            wxGetApp().CallAfter([this]() {
            });
            // refuse to be turned off so the gizmo is active when the CallAfter is executed
            m_state = m_old_state;
        }
        else {
            // we are actually shutting down
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("SLA gizmo turned off")));
            m_parent.toggle_model_objects_visibility(true);
            m_normal_cache.clear();
            m_clipping_plane_distance = 0.f;
            // Release clippers and the AABB raycaster.
            m_its = nullptr;
            m_object_clipper.reset();
            m_supports_clipper.reset();
            m_mesh_raycaster.reset();
        }
    }
    m_old_state = m_state;
}



void GLGizmoHollow::on_start_dragging()
{
    if (m_hover_id != -1) {
        select_point(NoPoints);
        select_point(m_hover_id);
        m_point_before_drag = m_editing_cache[m_hover_id];
    }
    else
        m_point_before_drag = CacheEntry();
}


void GLGizmoHollow::on_stop_dragging()
{
    if (m_hover_id != -1) {
        CacheEntry backup = m_editing_cache[m_hover_id];

        if (m_point_before_drag.support_point.pos != Vec3f::Zero() // some point was touched
         && backup.support_point.pos != m_point_before_drag.support_point.pos) // and it was moved, not just selected
        {
            m_editing_cache[m_hover_id] = m_point_before_drag;
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("Move support point")));
            m_editing_cache[m_hover_id] = backup;
        }
    }
    m_point_before_drag = CacheEntry();
}



void GLGizmoHollow::on_load(cereal::BinaryInputArchive& ar)
{
    ar(m_clipping_plane_distance,
       *m_clipping_plane,
       m_model_object_id,
       m_new_point_head_diameter,
       m_normal_cache,
       m_editing_cache,
       m_selection_empty
    );
}



void GLGizmoHollow::on_save(cereal::BinaryOutputArchive& ar) const
{
    ar(m_clipping_plane_distance,
       *m_clipping_plane,
       m_model_object_id,
       m_new_point_head_diameter,
       m_normal_cache,
       m_editing_cache,
       m_selection_empty
    );
}



void GLGizmoHollow::select_point(int i)
{
    if (! m_editing_mode) {
        std::cout << "DEBUGGING: select_point called when out of editing mode!" << std::endl;
        std::abort();
    }

    if (i == AllPoints || i == NoPoints) {
        for (auto& point_and_selection : m_editing_cache)
            point_and_selection.selected = ( i == AllPoints );
        m_selection_empty = (i == NoPoints);

        if (i == AllPoints)
            m_new_point_head_diameter = m_editing_cache[0].support_point.head_front_radius * 2.f;
    }
    else {
        m_editing_cache[i].selected = true;
        m_selection_empty = false;
        m_new_point_head_diameter = m_editing_cache[i].support_point.head_front_radius * 2.f;
    }
}


void GLGizmoHollow::unselect_point(int i)
{
    if (! m_editing_mode) {
        std::cout << "DEBUGGING: unselect_point called when out of editing mode!" << std::endl;
        std::abort();
    }

    m_editing_cache[i].selected = false;
    m_selection_empty = true;
    for (const CacheEntry& ce : m_editing_cache) {
        if (ce.selected) {
            m_selection_empty = false;
            break;
        }
    }
}


bool GLGizmoHollow::unsaved_changes() const
{
    if (m_editing_cache.size() != m_normal_cache.size())
        return true;

    for (size_t i=0; i<m_editing_cache.size(); ++i)
        if (m_editing_cache[i].support_point != m_normal_cache[i])
            return true;

    return false;
}

void GLGizmoHollow::reload_cache()
{

}


void GLGizmoHollow::update_clipping_plane(bool keep_normal) const
{
    Vec3d normal = (keep_normal && m_clipping_plane->get_normal() != Vec3d::Zero() ?
                        m_clipping_plane->get_normal() : -m_parent.get_camera().get_dir_forward());

    const Vec3d& center = m_model_object->instances[m_active_instance]->get_offset() + Vec3d(0., 0., m_z_shift);
    float dist = normal.dot(center);
    *m_clipping_plane = ClippingPlane(normal, (dist - (-m_active_instance_bb_radius) - m_clipping_plane_distance * 2*m_active_instance_bb_radius));
    m_parent.set_as_dirty();
}




} // namespace GUI
} // namespace Slic3r
