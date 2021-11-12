// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoPainterBase.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/TriangleMesh.hpp"



namespace Slic3r::GUI {


GLGizmoPainterBase::GLGizmoPainterBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{
    // Make sphere and save it into a vertex buffer.
    m_vbo_sphere.load_its_flat_shading(its_make_sphere(1., (2*M_PI)/24.));
    m_vbo_sphere.finalize_geometry(true);
}

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

GLGizmoPainterBase::ClippingPlaneDataWrapper GLGizmoPainterBase::get_clipping_plane_data() const
{
    ClippingPlaneDataWrapper clp_data_out{{0.f, 0.f, 1.f, FLT_MAX}, {-FLT_MAX, FLT_MAX}};
    // Take care of the clipping plane. The normal of the clipping plane is
    // saved with opposite sign than we need to pass to OpenGL (FIXME)
    if (bool clipping_plane_active = m_c->object_clipper()->get_position() != 0.; clipping_plane_active) {
        const ClippingPlane *clp = m_c->object_clipper()->get_clipping_plane();
        for (size_t i = 0; i < 3; ++i)
            clp_data_out.clp_dataf[i] = -1.f * float(clp->get_data()[i]);
        clp_data_out.clp_dataf[3] = float(clp->get_data()[3]);
    }

    // z_range is calculated in the same way as in GLCanvas3D::_render_objects(GLVolumeCollection::ERenderType type)
    if (m_c->get_canvas()->get_use_clipping_planes()) {
        const std::array<ClippingPlane, 2> &clps = m_c->get_canvas()->get_clipping_planes();
        clp_data_out.z_range                     = {float(-clps[0].get_data()[3]), float(clps[1].get_data()[3])};
    }

    return clp_data_out;
}

void GLGizmoPainterBase::render_triangles(const Selection& selection) const
{
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    auto* shader = wxGetApp().get_shader("gouraud_mod");
#else
    auto* shader = wxGetApp().get_shader("gouraud");
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    if (! shader)
        return;
    shader->start_using();
    shader->set_uniform("slope.actived", false);
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    shader->set_uniform("print_volume.type", 0);
#else
    shader->set_uniform("print_box.actived", false);
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    shader->set_uniform("clipping_plane", this->get_clipping_plane_data().clp_dataf);
    ScopeGuard guard([shader]() { if (shader) shader->stop_using(); });

    const ModelObject *mo      = m_c->selection_info()->model_object();
    int                mesh_id = -1;
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
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
        shader->set_uniform("volume_world_matrix", trafo_matrix);
#else
        shader->set_uniform("print_box.volume_world_matrix", trafo_matrix);
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS

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

    if (m_tool_type == ToolType::BRUSH) {
        if (m_cursor_type == TriangleSelector::SPHERE)
            render_cursor_sphere(trafo_matrices[m_rr.mesh_id]);
        else if (m_cursor_type == TriangleSelector::CIRCLE)
            render_cursor_circle();
    }
}



void GLGizmoPainterBase::render_cursor_circle() const
{
    const Camera &camera   = wxGetApp().plater()->get_camera();
    auto          zoom     = (float) camera.get_zoom();
    float         inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    Size  cnv_size        = m_parent.get_canvas_size();
    float cnv_half_width  = 0.5f * (float) cnv_size.get_width();
    float cnv_half_height = 0.5f * (float) cnv_size.get_height();
    if ((cnv_half_width == 0.0f) || (cnv_half_height == 0.0f))
        return;
    Vec2d mouse_pos(m_parent.get_local_mouse_position()(0), m_parent.get_local_mouse_position()(1));
    Vec2d center(mouse_pos(0) - cnv_half_width, cnv_half_height - mouse_pos(1));
    center = center * inv_zoom;

    glsafe(::glLineWidth(1.5f));
    static const std::array<float, 3> color = {0.f, 1.f, 0.3f};
    glsafe(::glColor3fv(color.data()));
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
            if (m_tool_type == ToolType::BRUSH && (m_cursor_type == TriangleSelector::CursorType::SPHERE || m_cursor_type == TriangleSelector::CursorType::CIRCLE)) {
                m_cursor_radius = action == SLAGizmoEventType::MouseWheelDown ? std::max(m_cursor_radius - this->get_cursor_radius_step(), this->get_cursor_radius_min())
                                                                              : std::min(m_cursor_radius + this->get_cursor_radius_step(), this->get_cursor_radius_max());
                m_parent.set_as_dirty();
                return true;
            } else if (m_tool_type == ToolType::SMART_FILL) {
                m_smart_fill_angle = action == SLAGizmoEventType::MouseWheelDown ? std::max(m_smart_fill_angle - SmartFillAngleStep, SmartFillAngleMin)
                                                                                : std::min(m_smart_fill_angle + SmartFillAngleStep, SmartFillAngleMax);
                m_parent.set_as_dirty();
                if (m_rr.mesh_id != -1) {
                    const Selection     &selection                 = m_parent.get_selection();
                    const ModelObject   *mo                        = m_c->selection_info()->model_object();
                    const ModelInstance *mi                        = mo->instances[selection.get_instance_idx()];
                    const Transform3d   trafo_matrix_not_translate = mi->get_transformation().get_matrix(true) * mo->volumes[m_rr.mesh_id]->get_matrix(true);
                    const Transform3d   trafo_matrix = mi->get_transformation().get_matrix() * mo->volumes[m_rr.mesh_id]->get_matrix();
                    m_triangle_selectors[m_rr.mesh_id]->seed_fill_select_triangles(m_rr.hit, int(m_rr.facet), trafo_matrix_not_translate, this->get_clipping_plane_in_volume_coordinates(trafo_matrix), m_smart_fill_angle,
                                                                                   m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f, true);
                    m_triangle_selectors[m_rr.mesh_id]->request_update_render_data();
                    m_seed_fill_last_mesh_id = m_rr.mesh_id;
                }
                return true;
            }

            return false;
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

        const Camera        &camera                      = wxGetApp().plater()->get_camera();
        const Selection     &selection                   = m_parent.get_selection();
        const ModelObject   *mo                          = m_c->selection_info()->model_object();
        const ModelInstance *mi                          = mo->instances[selection.get_instance_idx()];
        const Transform3d   instance_trafo               = mi->get_transformation().get_matrix();
        const Transform3d   instance_trafo_not_translate = mi->get_transformation().get_matrix(true);

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
        std::vector<Transform3d> trafo_matrices_not_translate;
        for (const ModelVolume *mv : mo->volumes)
            if (mv->is_model_part()) {
                trafo_matrices.emplace_back(instance_trafo * mv->get_matrix());
                trafo_matrices_not_translate.emplace_back(instance_trafo_not_translate * mv->get_matrix(true));
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

            const Transform3d &trafo_matrix               = trafo_matrices[m_rr.mesh_id];
            const Transform3d &trafo_matrix_not_translate = trafo_matrices_not_translate[m_rr.mesh_id];

            // Calculate direction from camera to the hit (in mesh coords):
            Vec3f camera_pos = (trafo_matrix.inverse() * camera.get_position()).cast<float>();

            assert(m_rr.mesh_id < int(m_triangle_selectors.size()));
            const TriangleSelector::ClippingPlane &clp = this->get_clipping_plane_in_volume_coordinates(trafo_matrix);
            if (m_tool_type == ToolType::SMART_FILL || m_tool_type == ToolType::BUCKET_FILL || (m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::POINTER)) {
                m_triangle_selectors[m_rr.mesh_id]->seed_fill_apply_on_triangles(new_state);
                if (m_tool_type == ToolType::SMART_FILL)
                    m_triangle_selectors[m_rr.mesh_id]->seed_fill_select_triangles(m_rr.hit, int(m_rr.facet), trafo_matrix_not_translate, clp, m_smart_fill_angle,
                                                                                   m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f, true);
                else if (m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::POINTER)
                    m_triangle_selectors[m_rr.mesh_id]->bucket_fill_select_triangles(m_rr.hit, int(m_rr.facet), clp, false, true);
                else if (m_tool_type == ToolType::BUCKET_FILL)
                    m_triangle_selectors[m_rr.mesh_id]->bucket_fill_select_triangles(m_rr.hit, int(m_rr.facet), clp, true, true);

                m_seed_fill_last_mesh_id = -1;
            } else if (m_tool_type == ToolType::BRUSH)
                m_triangle_selectors[m_rr.mesh_id]->select_patch(m_rr.hit, int(m_rr.facet), camera_pos, m_cursor_radius, m_cursor_type,
                                                                 new_state, trafo_matrix, trafo_matrix_not_translate, m_triangle_splitting_enabled, clp,
                                                                 m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f);
            m_triangle_selectors[m_rr.mesh_id]->request_update_render_data();
            m_last_mouse_click = mouse_position;
        }

        return true;
    }

    if (action == SLAGizmoEventType::Moving && (m_tool_type == ToolType::SMART_FILL || m_tool_type == ToolType::BUCKET_FILL || (m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::POINTER))) {
        if (m_triangle_selectors.empty())
            return false;

        const Camera        &camera                       = wxGetApp().plater()->get_camera();
        const Selection     &selection                    = m_parent.get_selection();
        const ModelObject   *mo                           = m_c->selection_info()->model_object();
        const ModelInstance *mi                           = mo->instances[selection.get_instance_idx()];
        const Transform3d    instance_trafo               = mi->get_transformation().get_matrix();
        const Transform3d    instance_trafo_not_translate = mi->get_transformation().get_matrix(true);

        // Precalculate transformations of individual meshes.
        std::vector<Transform3d> trafo_matrices;
        std::vector<Transform3d> trafo_matrices_not_translate;
        for (const ModelVolume *mv : mo->volumes)
            if (mv->is_model_part()) {
                trafo_matrices.emplace_back(instance_trafo * mv->get_matrix());
                trafo_matrices_not_translate.emplace_back(instance_trafo_not_translate * mv->get_matrix(true));
            }

        // Now "click" into all the prepared points and spill paint around them.
        update_raycast_cache(mouse_position, camera, trafo_matrices);

        auto seed_fill_unselect_all = [this]() {
            for (auto &triangle_selector : m_triangle_selectors) {
                triangle_selector->seed_fill_unselect_all_triangles();
                triangle_selector->request_update_render_data();
            }
        };

        if (m_rr.mesh_id == -1) {
            // Clean selected by seed fill for all triangles in all meshes when a mouse isn't pointing on any mesh.
            seed_fill_unselect_all();
            m_seed_fill_last_mesh_id = -1;

            // In case we have no valid hit, we can return.
            return false;
        }

        // The mouse moved from one object's volume to another one. So it is needed to unselect all triangles selected by seed fill.
        if(m_rr.mesh_id != m_seed_fill_last_mesh_id)
            seed_fill_unselect_all();

        const Transform3d &trafo_matrix = trafo_matrices[m_rr.mesh_id];
        const Transform3d &trafo_matrix_not_translate = trafo_matrices_not_translate[m_rr.mesh_id];

        assert(m_rr.mesh_id < int(m_triangle_selectors.size()));
        const TriangleSelector::ClippingPlane &clp = this->get_clipping_plane_in_volume_coordinates(trafo_matrix);
        if (m_tool_type == ToolType::SMART_FILL)
            m_triangle_selectors[m_rr.mesh_id]->seed_fill_select_triangles(m_rr.hit, int(m_rr.facet), trafo_matrix_not_translate, clp, m_smart_fill_angle,
                                                                           m_paint_on_overhangs_only ? m_highlight_by_angle_threshold_deg : 0.f);
        else if (m_tool_type == ToolType::BRUSH && m_cursor_type == TriangleSelector::CursorType::POINTER)
            m_triangle_selectors[m_rr.mesh_id]->bucket_fill_select_triangles(m_rr.hit, int(m_rr.facet), clp, false);
        else if (m_tool_type == ToolType::BUCKET_FILL)
            m_triangle_selectors[m_rr.mesh_id]->bucket_fill_select_triangles(m_rr.hit, int(m_rr.facet), clp, true);
        m_triangle_selectors[m_rr.mesh_id]->request_update_render_data();
        m_seed_fill_last_mesh_id = m_rr.mesh_id;
        return true;
    }

    if ((action == SLAGizmoEventType::LeftUp || action == SLAGizmoEventType::RightUp)
      && m_button_down != Button::None) {
        // Take snapshot and update ModelVolume data.
        wxString action_name = this->handle_snapshot_action_name(shift_down, m_button_down);
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), action_name, UndoRedo::SnapshotType::GizmoAction);
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
        || !selection.is_single_full_instance() || wxGetApp().get_mode() == comSimple)
        return false;

    // Check that none of the selected volumes is outside. Only SLA auxiliaries (supports) are allowed outside.
    const Selection::IndicesList& list = selection.get_volume_idxs();
    return std::all_of(list.cbegin(), list.cend(), [&selection](unsigned int idx) { return !selection.get_volume(idx)->is_outside; });
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
    }
    if (m_state == Off && m_old_state != Off) { // the gizmo was just turned Off
        // we are actually shutting down
        on_shutdown();
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

TriangleSelector::ClippingPlane GLGizmoPainterBase::get_clipping_plane_in_volume_coordinates(const Transform3d &trafo) const {
    const ::Slic3r::GUI::ClippingPlane *const clipping_plane = m_c->object_clipper()->get_clipping_plane();
    if (clipping_plane == nullptr || !clipping_plane->is_active())
        return {};

    const Vec3d  clp_normal = clipping_plane->get_normal();
    const double clp_offset = clipping_plane->get_offset();

    const Transform3d trafo_normal = Transform3d(trafo.linear().transpose());
    const Transform3d trafo_inv    = trafo.inverse();

    Vec3d point_on_plane             = clp_normal * clp_offset;
    Vec3d point_on_plane_transformed = trafo_inv * point_on_plane;
    Vec3d normal_transformed         = trafo_normal * clp_normal;
    auto offset_transformed          = float(point_on_plane_transformed.dot(normal_transformed));

    return TriangleSelector::ClippingPlane({float(normal_transformed.x()), float(normal_transformed.y()), float(normal_transformed.z()), offset_transformed});
}

std::array<float, 4> TriangleSelectorGUI::get_seed_fill_color(const std::array<float, 4> &base_color)
{
    return {base_color[0] * 0.75f, base_color[1] * 0.75f, base_color[2] * 0.75f, 1.f};
}

void TriangleSelectorGUI::render(ImGuiWrapper* imgui)
{
    static constexpr std::array<float, 4> enforcers_color{0.47f, 0.47f, 1.f, 1.f};
    static constexpr std::array<float, 4> blockers_color{1.f, 0.44f, 0.44f, 1.f};

    if (m_update_render_data) {
        update_render_data();
        m_update_render_data = false;
    }

    auto* shader = wxGetApp().get_current_shader();
    if (! shader)
        return;
#if ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    assert(shader->get_name() == "gouraud_mod");
#else
    assert(shader->get_name() == "gouraud");
#endif // ENABLE_OUT_OF_BED_DETECTION_IMPROVEMENTS
    ScopeGuard guard([shader]() { if (shader) shader->set_uniform("offset_depth_buffer", false);});
    shader->set_uniform("offset_depth_buffer", true);
    for (auto iva : {std::make_pair(&m_iva_enforcers, enforcers_color),
                     std::make_pair(&m_iva_blockers, blockers_color)}) {
        if (iva.first->has_VBOs()) {
            shader->set_uniform("uniform_color", iva.second);
            iva.first->render();
        }
    }

    for (auto &iva : m_iva_seed_fills)
        if (iva.has_VBOs()) {
            size_t                      color_idx = &iva - &m_iva_seed_fills.front();
            const std::array<float, 4> &color     = TriangleSelectorGUI::get_seed_fill_color(color_idx == 1 ? enforcers_color :
                                                                                             color_idx == 2 ? blockers_color :
                                                                                                              GLVolume::NEUTRAL_COLOR);
            shader->set_uniform("uniform_color", color);
            iva.render();
        }

    if (m_paint_contour.has_VBO()) {
        ScopeGuard guard_gouraud([shader]() { shader->start_using(); });
        shader->stop_using();

        auto *contour_shader = wxGetApp().get_shader("mm_contour");
        contour_shader->start_using();

        glsafe(::glDepthFunc(GL_LEQUAL));
        m_paint_contour.render();
        glsafe(::glDepthFunc(GL_LESS));

        contour_shader->stop_using();
    }

#ifdef PRUSASLICER_TRIANGLE_SELECTOR_DEBUG
    if (imgui)
        render_debug(imgui);
    else
        assert(false); // If you want debug output, pass ptr to ImGuiWrapper.
#endif
}

void TriangleSelectorGUI::update_render_data()
{
    int              enf_cnt = 0;
    int              blc_cnt = 0;
    std::vector<int> seed_fill_cnt(m_iva_seed_fills.size(), 0);

    for (auto *iva : {&m_iva_enforcers, &m_iva_blockers})
        iva->release_geometry();

    for (auto &iva : m_iva_seed_fills)
        iva.release_geometry();

    for (const Triangle &tr : m_triangles) {
        if (!tr.valid() || tr.is_split() || (tr.get_state() == EnforcerBlockerType::NONE && !tr.is_selected_by_seed_fill()))
            continue;

        int tr_state = int(tr.get_state());
        GLIndexedVertexArray &iva = tr.is_selected_by_seed_fill()                   ? m_iva_seed_fills[tr_state] :
                                    tr.get_state() == EnforcerBlockerType::ENFORCER ? m_iva_enforcers :
                                                                                      m_iva_blockers;
        int                  &cnt = tr.is_selected_by_seed_fill()                   ? seed_fill_cnt[tr_state] :
                                    tr.get_state() == EnforcerBlockerType::ENFORCER ? enf_cnt :
                                                                                      blc_cnt;
        const Vec3f          &v0  = m_vertices[tr.verts_idxs[0]].v;
        const Vec3f          &v1  = m_vertices[tr.verts_idxs[1]].v;
        const Vec3f          &v2  = m_vertices[tr.verts_idxs[2]].v;
        //FIXME the normal may likely be pulled from m_triangle_selectors, but it may not be worth the effort
        // or the current implementation may be more cache friendly.
        const Vec3f           n   = (v1 - v0).cross(v2 - v1).normalized();
        iva.push_geometry(v0, n);
        iva.push_geometry(v1, n);
        iva.push_geometry(v2, n);
        iva.push_triangle(cnt, cnt + 1, cnt + 2);
        cnt += 3;
    }

    for (auto *iva : {&m_iva_enforcers, &m_iva_blockers})
        iva->finalize_geometry(true);

    for (auto &iva : m_iva_seed_fills)
        iva.finalize_geometry(true);

    m_paint_contour.release_geometry();
    std::vector<Vec2i> contour_edges = this->get_seed_fill_contour();
    m_paint_contour.contour_vertices.reserve(contour_edges.size() * 6);
    for (const Vec2i &edge : contour_edges) {
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(0)].v.x());
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(0)].v.y());
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(0)].v.z());

        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(1)].v.x());
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(1)].v.y());
        m_paint_contour.contour_vertices.emplace_back(m_vertices[edge(1)].v.z());
    }

    m_paint_contour.contour_indices.assign(m_paint_contour.contour_vertices.size() / 3, 0);
    std::iota(m_paint_contour.contour_indices.begin(), m_paint_contour.contour_indices.end(), 0);
    m_paint_contour.contour_indices_size = m_paint_contour.contour_indices.size();

    m_paint_contour.finalize_geometry();
}

void GLPaintContour::render() const
{
    assert(this->m_contour_VBO_id != 0);
    assert(this->m_contour_EBO_id != 0);

    glsafe(::glLineWidth(4.0f));

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->m_contour_VBO_id));
    glsafe(::glVertexPointer(3, GL_FLOAT, 3 * sizeof(float), nullptr));

    glsafe(::glEnableClientState(GL_VERTEX_ARRAY));

    if (this->contour_indices_size > 0) {
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->m_contour_EBO_id));
        glsafe(::glDrawElements(GL_LINES, GLsizei(this->contour_indices_size), GL_UNSIGNED_INT, nullptr));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
    }

    glsafe(::glDisableClientState(GL_VERTEX_ARRAY));

    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
}

void GLPaintContour::finalize_geometry()
{
    assert(this->m_contour_VBO_id == 0);
    assert(this->m_contour_EBO_id == 0);

    if (!this->contour_vertices.empty()) {
        glsafe(::glGenBuffers(1, &this->m_contour_VBO_id));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->m_contour_VBO_id));
        glsafe(::glBufferData(GL_ARRAY_BUFFER, this->contour_vertices.size() * sizeof(float), this->contour_vertices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
        this->contour_vertices.clear();
    }

    if (!this->contour_indices.empty()) {
        glsafe(::glGenBuffers(1, &this->m_contour_EBO_id));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, this->m_contour_EBO_id));
        glsafe(::glBufferData(GL_ARRAY_BUFFER, this->contour_indices.size() * sizeof(unsigned int), this->contour_indices.data(), GL_STATIC_DRAW));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
        this->contour_indices.clear();
    }
}

void GLPaintContour::release_geometry()
{
    if (this->m_contour_VBO_id) {
        glsafe(::glDeleteBuffers(1, &this->m_contour_VBO_id));
        this->m_contour_VBO_id = 0;
    }
    if (this->m_contour_EBO_id) {
        glsafe(::glDeleteBuffers(1, &this->m_contour_EBO_id));
        this->m_contour_EBO_id = 0;
    }
    this->clear();
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
        else if (tr.valid()) {
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



} // namespace Slic3r::GUI
