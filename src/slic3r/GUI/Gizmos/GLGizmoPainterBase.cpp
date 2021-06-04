// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoPainterBase.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/Model.hpp"



namespace Slic3r {
namespace GUI {


GLGizmoPainterBase::GLGizmoPainterBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{
    // Make sphere and save it into a vertex buffer.
    const TriangleMesh sphere_mesh = make_sphere(1., (2*M_PI)/24.);
    for (size_t i=0; i<sphere_mesh.its.vertices.size(); ++i)
        m_vbo_sphere.push_geometry(sphere_mesh.its.vertices[i].cast<double>(),
                                    sphere_mesh.stl.facet_start[i].normal.cast<double>());
    for (const stl_triangle_vertex_indices& indices : sphere_mesh.its.indices)
        m_vbo_sphere.push_triangle(indices(0), indices(1), indices(2));
    m_vbo_sphere.finalize_geometry(true);
}



#if ENABLE_PROJECT_DIRTY_STATE
// port of 948bc382655993721d93d3b9fce9b0186fcfb211
void GLGizmoPainterBase::activate_internal_undo_redo_stack(bool activate)
{
    Plater* plater = wxGetApp().plater();

    // Following is needed to prevent taking an extra snapshot when the activation of
    // the internal stack happens when the gizmo is already active (such as open gizmo,
    // close gizmo, undo, start painting). The internal stack does not activate on the
    // undo, because that would obliterate all future of the main stack (user would
    // have to close the gizmo himself, he has no access to main undo/redo after the
    // internal stack opens). We don't want the "entering" snapshot taken in this case,
    // because there already is one.
    std::string last_snapshot_name;
    plater->undo_redo_topmost_string_getter(plater->can_undo(), last_snapshot_name);

    if (activate && !m_internal_stack_active) {
        std::string str = get_painter_type() == PainterGizmoType::FDM_SUPPORTS
            ? _u8L("Entering Paint-on supports")
            : _u8L("Entering Seam painting");
        if (last_snapshot_name != str)
            Plater::TakeSnapshot(plater, str);
        plater->enter_gizmos_stack();
        m_internal_stack_active = true;
    }
    if (!activate && m_internal_stack_active) {
        plater->leave_gizmos_stack();
        std::string str = get_painter_type() == PainterGizmoType::SEAM
            ? _u8L("Leaving Seam painting")
            : _u8L("Leaving Paint-on supports");
        if (last_snapshot_name != str)
            Plater::TakeSnapshot(plater, str);
        m_internal_stack_active = false;
    }
}
#else
void GLGizmoPainterBase::activate_internal_undo_redo_stack(bool activate)
{
    if (activate && ! m_internal_stack_active) {
        wxString str = get_painter_type() == PainterGizmoType::FDM_SUPPORTS
                           ? _L("Entering Paint-on supports")
                           : (get_painter_type() == PainterGizmoType::MMU_SEGMENTATION ? _L("Entering MMU segmentation") : _L("Entering Seam painting"));
        Plater::TakeSnapshot(wxGetApp().plater(), str);
        wxGetApp().plater()->enter_gizmos_stack();
        m_internal_stack_active = true;
    }
    if (! activate && m_internal_stack_active) {
        wxString str = get_painter_type() == PainterGizmoType::SEAM
                           ? _L("Leaving Seam painting")
                           : (get_painter_type() == PainterGizmoType::MMU_SEGMENTATION ? _L("Leaving MMU segmentation") : _L("Leaving Paint-on supports"));
        wxGetApp().plater()->leave_gizmos_stack();
        Plater::TakeSnapshot(wxGetApp().plater(), str);
        m_internal_stack_active = false;
    }
}
#endif // ENABLE_PROJECT_DIRTY_STATE



void GLGizmoPainterBase::set_painter_gizmo_data(const Selection& selection)
{
    if (m_state != On)
        return;

    const ModelObject* mo = m_c->selection_info() ? m_c->selection_info()->model_object() : nullptr;

    if (mo && selection.is_from_single_instance()
     && (m_schedule_update || mo->id() != m_old_mo_id || mo->volumes.size() != m_old_volumes_size))
    {
        update_from_model_object();
        m_old_mo_id = mo->id();
        m_old_volumes_size = mo->volumes.size();
        m_schedule_update = false;
    }
}



void GLGizmoPainterBase::render_triangles(const Selection& selection) const
{
    const ModelObject* mo = m_c->selection_info()->model_object();

    glsafe(::glEnable(GL_POLYGON_OFFSET_FILL));
    ScopeGuard offset_fill_guard([]() { glsafe(::glDisable(GL_POLYGON_OFFSET_FILL)); } );
    glsafe(::glPolygonOffset(-5.0, -5.0));

    // Take care of the clipping plane. The normal of the clipping plane is
    // saved with opposite sign than we need to pass to OpenGL (FIXME)
    bool clipping_plane_active = m_c->object_clipper()->get_position() != 0.;
    float clp_dataf[4] = {0.f, 0.f, 1.f, FLT_MAX};
    if (clipping_plane_active) {
        const ClippingPlane* clp = m_c->object_clipper()->get_clipping_plane();
        for (size_t i=0; i<3; ++i)
            clp_dataf[i] = -1. * clp->get_data()[i];
        clp_dataf[3] = clp->get_data()[3];
    }

    auto *shader = wxGetApp().get_shader("gouraud");
    if (! shader)
        return;
    shader->start_using();
    shader->set_uniform("slope.actived", false);
    shader->set_uniform("print_box.actived", false);
    shader->set_uniform("clipping_plane", clp_dataf, 4);
    ScopeGuard guard([shader]() { if (shader) shader->stop_using(); });

    int mesh_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++mesh_id;

        const Transform3d trafo_matrix =
            mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() *
            mv->get_matrix();

        bool is_left_handed = trafo_matrix.matrix().determinant() < 0.;
        if (is_left_handed)
            glsafe(::glFrontFace(GL_CW));

        glsafe(::glPushMatrix());
        glsafe(::glMultMatrixd(trafo_matrix.data()));

        // For printers with multiple extruders, it is necessary to pass trafo_matrix
        // to the shader input variable print_box.volume_world_matrix before
        // rendering the painted triangles. When this matrix is not set, the
        // wrong transformation matrix is used for "Clipping of view".
        shader->set_uniform("print_box.volume_world_matrix", trafo_matrix);

        m_triangle_selectors[mesh_id]->render(m_imgui);

        glsafe(::glPopMatrix());
        if (is_left_handed)
            glsafe(::glFrontFace(GL_CCW));
    }
}


void GLGizmoPainterBase::render_cursor() const
{
    // First check that the mouse pointer is on an object.
    const ModelObject* mo = m_c->selection_info()->model_object();
    const Selection& selection = m_parent.get_selection();
    const ModelInstance* mi = mo->instances[selection.get_instance_idx()];
    const Camera& camera = wxGetApp().plater()->get_camera();

    // Precalculate transformations of individual meshes.
    std::vector<Transform3d> trafo_matrices;
    for (const ModelVolume* mv : mo->volumes) {
        if (mv->is_model_part())
            trafo_matrices.emplace_back(mi->get_transformation().get_matrix() * mv->get_matrix());
    }
    // Raycast and return if there's no hit.
    update_raycast_cache(m_parent.get_local_mouse_position(), camera, trafo_matrices);
    if (m_rr.mesh_id == -1)
        return;

    if (!m_seed_fill_enabled) {
        if (m_cursor_type == TriangleSelector::SPHERE)
            render_cursor_sphere(trafo_matrices[m_rr.mesh_id]);
        else
            render_cursor_circle();
    }
}



void GLGizmoPainterBase::render_cursor_circle() const
{
    const Camera& camera = wxGetApp().plater()->get_camera();
    float zoom = (float)camera.get_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    Size cnv_size = m_parent.get_canvas_size();
    float cnv_half_width = 0.5f * (float)cnv_size.get_width();
    float cnv_half_height = 0.5f * (float)cnv_size.get_height();
    if ((cnv_half_width == 0.0f) || (cnv_half_height == 0.0f))
        return;
    Vec2d mouse_pos(m_parent.get_local_mouse_position()(0), m_parent.get_local_mouse_position()(1));
    Vec2d center(mouse_pos(0) - cnv_half_width, cnv_half_height - mouse_pos(1));
    center = center * inv_zoom;

    glsafe(::glLineWidth(1.5f));
    float color[3];
    color[0] = 0.f;
    color[1] = 1.f;
    color[2] = 0.3f;
    glsafe(::glColor3fv(color));
    glsafe(::glDisable(GL_DEPTH_TEST));

    glsafe(::glPushMatrix());
    glsafe(::glLoadIdentity());
    // ensure that the circle is renderered inside the frustrum
    glsafe(::glTranslated(0.0, 0.0, -(camera.get_near_z() + 0.5)));
    // ensure that the overlay fits the frustrum near z plane
    double gui_scale = camera.get_gui_scale();
    glsafe(::glScaled(gui_scale, gui_scale, 1.0));

    glsafe(::glPushAttrib(GL_ENABLE_BIT));
    glsafe(::glLineStipple(4, 0xAAAA));
    glsafe(::glEnable(GL_LINE_STIPPLE));

    ::glBegin(GL_LINE_LOOP);
    for (double angle=0; angle<2*M_PI; angle+=M_PI/20.)
        ::glVertex2f(GLfloat(center.x()+m_cursor_radius*cos(angle)), GLfloat(center.y()+m_cursor_radius*sin(angle)));
    glsafe(::glEnd());

    glsafe(::glPopAttrib());
    glsafe(::glPopMatrix());
    glsafe(::glEnable(GL_DEPTH_TEST));
}


void GLGizmoPainterBase::render_cursor_sphere(const Transform3d& trafo) const
{
    const Transform3d complete_scaling_matrix_inverse = Geometry::Transformation(trafo).get_matrix(true, true, false, true).inverse();
    const bool is_left_handed = Geometry::Transformation(trafo).is_left_handed();

    glsafe(::glPushMatrix());
    glsafe(::glMultMatrixd(trafo.data()));
    // Inverse matrix of the instance scaling is applied so that the mark does not scale with the object.
    glsafe(::glTranslatef(m_rr.hit(0), m_rr.hit(1), m_rr.hit(2)));
    glsafe(::glMultMatrixd(complete_scaling_matrix_inverse.data()));
    glsafe(::glScaled(m_cursor_radius, m_cursor_radius, m_cursor_radius));

    if (is_left_handed)
        glFrontFace(GL_CW);

    std::array<float, 4> render_color = {0.f, 0.f, 0.f, 0.25f};
    if (m_button_down == Button::Left)
        render_color = this->get_cursor_sphere_left_button_color();
    else if (m_button_down == Button::Right)
        render_color = this->get_cursor_sphere_right_button_color();
    glsafe(::glColor4fv(render_color.data()));

    m_vbo_sphere.render();

    if (is_left_handed)
        glFrontFace(GL_CCW);

    glsafe(::glPopMatrix());
}


bool GLGizmoPainterBase::is_mesh_point_clipped(const Vec3d& point, const Transform3d& trafo) const
{
    if (m_c->object_clipper()->get_position() == 0.)
        return false;

    auto sel_info = m_c->selection_info();
    Vec3d transformed_point =  trafo * point;
    transformed_point(2) += sel_info->get_sla_shift();
    return m_c->object_clipper()->get_clipping_plane()->is_point_clipped(transformed_point);
}


// Following function is called from GLCanvas3D to inform the gizmo about a mouse/keyboard event.
// The gizmo has an opportunity to react - if it does, it should return true so that the Canvas3D is
// aware that the event was reacted to and stops trying to make different sense of it. If the gizmo
// concludes that the event was not intended for it, it should return false.
bool GLGizmoPainterBase::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    if (action == SLAGizmoEventType::MouseWheelUp
     || action == SLAGizmoEventType::MouseWheelDown) {
        if (control_down) {
            double pos = m_c->object_clipper()->get_position();
            pos = action == SLAGizmoEventType::MouseWheelDown
                      ? std::max(0., pos - 0.01)
                      : std::min(1., pos + 0.01);
            m_c->object_clipper()->set_position(pos, true);
            return true;
        }
        else if (alt_down) {
            m_cursor_radius = action == SLAGizmoEventType::MouseWheelDown
                    ? std::max(m_cursor_radius - CursorRadiusStep, CursorRadiusMin)
                    : std::min(m_cursor_radius + CursorRadiusStep, CursorRadiusMax);
            m_parent.set_as_dirty();
            return true;
        }
    }

    if (action == SLAGizmoEventType::ResetClippingPlane) {
        m_c->object_clipper()->set_position(-1., false);
        return true;
    }

    if (action == SLAGizmoEventType::LeftDown
     || action == SLAGizmoEventType::RightDown
    || (action == SLAGizmoEventType::Dragging && m_button_down != Button::None)) {

        if (m_triangle_selectors.empty())
            return false;

        EnforcerBlockerType new_state = EnforcerBlockerType::NONE;
        if (! shift_down) {
            if (action == SLAGizmoEventType::Dragging)
                new_state = m_button_down == Button::Left ? this->get_left_button_state_type() : this->get_right_button_state_type();
            else
                new_state = action == SLAGizmoEventType::LeftDown ? this->get_left_button_state_type() : this->get_right_button_state_type();
        }

        const Camera& camera = wxGetApp().plater()->get_camera();
        const Selection& selection = m_parent.get_selection();
        const ModelObject* mo = m_c->selection_info()->model_object();
        const ModelInstance* mi = mo->instances[selection.get_instance_idx()];
        const Transform3d& instance_trafo = mi->get_transformation().get_matrix();

        // List of mouse positions that will be used as seeds for painting.
        std::vector<Vec2d> mouse_positions{mouse_position};

        // In case current mouse position is far from the last one,
        // add several positions from between into the list, so there
        // are no gaps in the painted region.
        {
            if (m_last_mouse_click == Vec2d::Zero())
                m_last_mouse_click = mouse_position;
            // resolution describes minimal distance limit using circle radius
            // as a unit (e.g., 2 would mean the patches will be touching).
            double resolution = 0.7;
            double diameter_px =  resolution  * m_cursor_radius * camera.get_zoom();
            int patches_in_between = int(((mouse_position - m_last_mouse_click).norm() - diameter_px) / diameter_px);
            if (patches_in_between > 0) {
                Vec2d diff = (mouse_position - m_last_mouse_click)/(patches_in_between+1);
                for (int i=1; i<=patches_in_between; ++i)
                    mouse_positions.emplace_back(m_last_mouse_click + i*diff);
            }
        }
        m_last_mouse_click = Vec2d::Zero(); // only actual hits should be saved

        // Precalculate transformations of individual meshes.
        std::vector<Transform3d> trafo_matrices;
        for (const ModelVolume* mv : mo->volumes) {
            if (mv->is_model_part())
                trafo_matrices.emplace_back(instance_trafo * mv->get_matrix());
        }

        // Now "click" into all the prepared points and spill paint around them.
        for (const Vec2d& mp : mouse_positions) {
            update_raycast_cache(mp, camera, trafo_matrices);

            bool dragging_while_painting = (action == SLAGizmoEventType::Dragging && m_button_down != Button::None);

            // The mouse button click detection is enabled when there is a valid hit.
            // Missing the object entirely
            // shall not capture the mouse.
            if (m_rr.mesh_id != -1) {
                if (m_button_down == Button::None)
                    m_button_down = ((action == SLAGizmoEventType::LeftDown) ? Button::Left : Button::Right);
            }

            if (m_rr.mesh_id == -1) {
                // In case we have no valid hit, we can return. The event will be stopped when
                // dragging while painting (to prevent scene rotations and moving the object)
                return dragging_while_painting;
            }

            const Transform3d& trafo_matrix = trafo_matrices[m_rr.mesh_id];

            // Calculate direction from camera to the hit (in mesh coords):
            Vec3f camera_pos = (trafo_matrix.inverse() * camera.get_position()).cast<float>();

            assert(m_rr.mesh_id < int(m_triangle_selectors.size()));
            if (m_seed_fill_enabled)
                m_triangle_selectors[m_rr.mesh_id]->seed_fill_apply_on_triangles(new_state);
            else
                m_triangle_selectors[m_rr.mesh_id]->select_patch(m_rr.hit, int(m_rr.facet), camera_pos, m_cursor_radius, m_cursor_type,
                                                                 new_state, trafo_matrix, m_triangle_splitting_enabled);
            m_last_mouse_click = mouse_position;
        }

        return true;
    }

    if (action == SLAGizmoEventType::Moving && m_seed_fill_enabled) {
        if (m_triangle_selectors.empty())
            return false;

        const Camera &       camera         = wxGetApp().plater()->get_camera();
        const Selection &    selection      = m_parent.get_selection();
        const ModelObject *  mo             = m_c->selection_info()->model_object();
        const ModelInstance *mi             = mo->instances[selection.get_instance_idx()];
        const Transform3d &  instance_trafo = mi->get_transformation().get_matrix();

        // Precalculate transformations of individual meshes.
        std::vector<Transform3d> trafo_matrices;
        for (const ModelVolume *mv : mo->volumes)
            if (mv->is_model_part())
                trafo_matrices.emplace_back(instance_trafo * mv->get_matrix());

        // Now "click" into all the prepared points and spill paint around them.
        update_raycast_cache(mouse_position, camera, trafo_matrices);

        if (m_rr.mesh_id == -1) {
            // Clean selected by seed fill for all triangles
            for (auto &triangle_selector : m_triangle_selectors)
                triangle_selector->seed_fill_unselect_all_triangles();

            // In case we have no valid hit, we can return.
            return false;
        }

        assert(m_rr.mesh_id < int(m_triangle_selectors.size()));
        m_triangle_selectors[m_rr.mesh_id]->seed_fill_select_triangles(m_rr.hit, int(m_rr.facet), m_seed_fill_angle);
        return true;
    }

    if ((action == SLAGizmoEventType::LeftUp || action == SLAGizmoEventType::RightUp)
      && m_button_down != Button::None) {
        // Take snapshot and update ModelVolume data.
        wxString action_name = this->handle_snapshot_action_name(shift_down, m_button_down);
        activate_internal_undo_redo_stack(true);
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), action_name);
        update_model_object();

        m_button_down = Button::None;
        m_last_mouse_click = Vec2d::Zero();
        return true;
    }

    return false;
}



void GLGizmoPainterBase::update_raycast_cache(const Vec2d& mouse_position,
                                              const Camera& camera,
                                              const std::vector<Transform3d>& trafo_matrices) const
{
    if (m_rr.mouse_position == mouse_position) {
        // Same query as last time - the answer is already in the cache.
        return;
    }

    Vec3f normal =  Vec3f::Zero();
    Vec3f hit = Vec3f::Zero();
    size_t facet = 0;
    Vec3f closest_hit = Vec3f::Zero();
    double closest_hit_squared_distance = std::numeric_limits<double>::max();
    size_t closest_facet = 0;
    int closest_hit_mesh_id = -1;

    // Cast a ray on all meshes, pick the closest hit and save it for the respective mesh
    for (int mesh_id = 0; mesh_id < int(trafo_matrices.size()); ++mesh_id) {

        if (m_c->raycaster()->raycasters()[mesh_id]->unproject_on_mesh(
                   mouse_position,
                   trafo_matrices[mesh_id],
                   camera,
                   hit,
                   normal,
                   m_c->object_clipper()->get_clipping_plane(),
                   &facet))
        {
            // In case this hit is clipped, skip it.
            if (is_mesh_point_clipped(hit.cast<double>(), trafo_matrices[mesh_id]))
                continue;

            // Is this hit the closest to the camera so far?
            double hit_squared_distance = (camera.get_position()-trafo_matrices[mesh_id]*hit.cast<double>()).squaredNorm();
            if (hit_squared_distance < closest_hit_squared_distance) {
                closest_hit_squared_distance = hit_squared_distance;
                closest_facet = facet;
                closest_hit_mesh_id = mesh_id;
                closest_hit = hit;
            }
        }
    }

    m_rr = {mouse_position, closest_hit_mesh_id, closest_hit, closest_facet};
}

bool GLGizmoPainterBase::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();

    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptFFF
        || !selection.is_single_full_instance())
        return false;

    // Check that none of the selected volumes is outside. Only SLA auxiliaries (supports) are allowed outside.
    const Selection::IndicesList& list = selection.get_volume_idxs();
    for (const auto& idx : list)
        if (selection.get_volume(idx)->is_outside)
            return false;

    return true;
}

bool GLGizmoPainterBase::on_is_selectable() const
{
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF
         && wxGetApp().get_mode() != comSimple );
}


CommonGizmosDataID GLGizmoPainterBase::on_get_requirements() const
{
    return CommonGizmosDataID(
                int(CommonGizmosDataID::SelectionInfo)
              | int(CommonGizmosDataID::InstancesHider)
              | int(CommonGizmosDataID::Raycaster)
              | int(CommonGizmosDataID::ObjectClipper));
}


void GLGizmoPainterBase::on_set_state()
{
    if (m_state == m_old_state)
        return;

    if (m_state == On && m_old_state != On) { // the gizmo was just turned on
        on_opening();
        if (! m_parent.get_gizmos_manager().is_serializing()) {
            wxGetApp().CallAfter([this]() {
                activate_internal_undo_redo_stack(true);
            });
        }
    }
    if (m_state == Off && m_old_state != Off) { // the gizmo was just turned Off
        // we are actually shutting down
        on_shutdown();
        activate_internal_undo_redo_stack(false);
        m_old_mo_id = -1;
        //m_iva.release_geometry();
        m_triangle_selectors.clear();
    }
    m_old_state = m_state;
}



void GLGizmoPainterBase::on_load(cereal::BinaryInputArchive&)
{
    // We should update the gizmo from current ModelObject, but it is not
    // possible at this point. That would require having updated selection and
    // common gizmos data, which is not done at this point. Instead, save
    // a flag to do the update in set_painter_gizmo_data, which will be called
    // soon after.
    m_schedule_update = true;
}



void TriangleSelectorGUI::render(ImGuiWrapper* imgui)
{
    int enf_cnt = 0;
    int blc_cnt = 0;
    int seed_fill_cnt = 0;

    m_iva_enforcers.release_geometry();
    m_iva_blockers.release_geometry();
    m_iva_seed_fill.release_geometry();

    for (const Triangle& tr : m_triangles) {
        if (!tr.valid || tr.is_split() || tr.get_state() == EnforcerBlockerType::NONE || tr.is_selected_by_seed_fill())
            continue;

        GLIndexedVertexArray& va = tr.get_state() == EnforcerBlockerType::ENFORCER
                                   ? m_iva_enforcers
                                   : m_iva_blockers;
        int& cnt = tr.get_state() == EnforcerBlockerType::ENFORCER
                ? enf_cnt
                : blc_cnt;

        for (int i=0; i<3; ++i)
            va.push_geometry(double(m_vertices[tr.verts_idxs[i]].v[0]),
                             double(m_vertices[tr.verts_idxs[i]].v[1]),
                             double(m_vertices[tr.verts_idxs[i]].v[2]),
                             double(tr.normal[0]),
                             double(tr.normal[1]),
                             double(tr.normal[2]));
        va.push_triangle(cnt, cnt + 1, cnt + 2);
        cnt += 3;
    }

    for (const Triangle &tr : m_triangles) {
        if (!tr.valid || tr.is_split() || !tr.is_selected_by_seed_fill())
            continue;

        for (int i = 0; i < 3; ++i)
            m_iva_seed_fill.push_geometry(double(m_vertices[tr.verts_idxs[i]].v[0]),
                                          double(m_vertices[tr.verts_idxs[i]].v[1]),
                                          double(m_vertices[tr.verts_idxs[i]].v[2]),
                                          double(tr.normal[0]),
                                          double(tr.normal[1]),
                                          double(tr.normal[2]));
        m_iva_seed_fill.push_triangle(seed_fill_cnt, seed_fill_cnt + 1, seed_fill_cnt + 2);
        seed_fill_cnt += 3;
    }

    m_iva_enforcers.finalize_geometry(true);
    m_iva_blockers.finalize_geometry(true);
    m_iva_seed_fill.finalize_geometry(true);

    bool render_enf = m_iva_enforcers.has_VBOs();
    bool render_blc = m_iva_blockers.has_VBOs();
    bool render_seed_fill = m_iva_seed_fill.has_VBOs();

    auto* shader = wxGetApp().get_current_shader();
    if (! shader)
        return;
    assert(shader->get_name() == "gouraud");

    if (render_enf) {
        std::array<float, 4> color = { 0.47f, 0.47f, 1.f, 1.f };
        shader->set_uniform("uniform_color", color);
        m_iva_enforcers.render();
    }

    if (render_blc) {
        std::array<float, 4> color = { 1.f, 0.44f, 0.44f, 1.f };
        shader->set_uniform("uniform_color", color);
        m_iva_blockers.render();
    }

    if (render_seed_fill) {
        std::array<float, 4> color = { 0.f, 1.00f, 0.44f, 1.f };
        shader->set_uniform("uniform_color", color);
        m_iva_seed_fill.render();
    }


#ifdef PRUSASLICER_TRIANGLE_SELECTOR_DEBUG
    if (imgui)
        render_debug(imgui);
    else
        assert(false); // If you want debug output, pass ptr to ImGuiWrapper.
#endif
}



#ifdef PRUSASLICER_TRIANGLE_SELECTOR_DEBUG
void TriangleSelectorGUI::render_debug(ImGuiWrapper* imgui)
{
    imgui->begin(std::string("TriangleSelector dialog (DEV ONLY)"),
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
    static float edge_limit = 1.f;
    imgui->text("Edge limit (mm): ");
    imgui->slider_float("", &edge_limit, 0.1f, 8.f);
    set_edge_limit(edge_limit);
    imgui->checkbox("Show split triangles: ", m_show_triangles);
    imgui->checkbox("Show invalid triangles: ", m_show_invalid);

    int valid_triangles = m_triangles.size() - m_invalid_triangles;
    imgui->text("Valid triangles: " + std::to_string(valid_triangles) +
                  "/" + std::to_string(m_triangles.size()));
    imgui->text("Vertices: " + std::to_string(m_vertices.size()));
    if (imgui->button("Force garbage collection"))
        garbage_collect();

    if (imgui->button("Serialize - deserialize")) {
        auto map = serialize();
        deserialize(map);
    }

    imgui->end();

    if (! m_show_triangles)
        return;

    enum vtype {
        ORIGINAL = 0,
        SPLIT,
        INVALID
    };

    for (auto& va : m_varrays)
        va.release_geometry();

    std::array<int, 3> cnts;

    ::glScalef(1.01f, 1.01f, 1.01f);

    for (int tr_id=0; tr_id<int(m_triangles.size()); ++tr_id) {
        const Triangle& tr = m_triangles[tr_id];
        GLIndexedVertexArray* va = nullptr;
        int* cnt = nullptr;
        if (tr_id < m_orig_size_indices) {
            va = &m_varrays[ORIGINAL];
            cnt = &cnts[ORIGINAL];
        }
        else if (tr.valid) {
            va = &m_varrays[SPLIT];
            cnt = &cnts[SPLIT];
        }
        else {
            if (! m_show_invalid)
                continue;
            va = &m_varrays[INVALID];
            cnt = &cnts[INVALID];
        }

        for (int i=0; i<3; ++i)
            va->push_geometry(double(m_vertices[tr.verts_idxs[i]].v[0]),
                              double(m_vertices[tr.verts_idxs[i]].v[1]),
                              double(m_vertices[tr.verts_idxs[i]].v[2]),
                              0., 0., 1.);
        va->push_triangle(*cnt,
                          *cnt+1,
                          *cnt+2);
        *cnt += 3;
    }

    ::glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
    for (vtype i : {ORIGINAL, SPLIT, INVALID}) {
        GLIndexedVertexArray& va = m_varrays[i];
        va.finalize_geometry(true);
        if (va.has_VBOs()) {
            switch (i) {
            case ORIGINAL : ::glColor3f(0.f, 0.f, 1.f); break;
            case SPLIT    : ::glColor3f(1.f, 0.f, 0.f); break;
            case INVALID  : ::glColor3f(1.f, 1.f, 0.f); break;
            }
            va.render();
        }
    }
    ::glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
}
#endif



} // namespace GUI
} // namespace Slic3r
