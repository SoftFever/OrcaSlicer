// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoFdmSupports.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/PresetBundle.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/Model.hpp"



namespace Slic3r {
namespace GUI {


GLGizmoFdmSupports::GLGizmoFdmSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    , m_quadric(nullptr)
{
    m_clipping_plane.reset(new ClippingPlane());
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

    m_desc["clipping_of_view"] = _L("Clipping of view") + ": ";
    m_desc["reset_direction"]  = _L("Reset direction");
    m_desc["cursor_size"]      = _L("Cursor size") + ": ";
    m_desc["enforce_caption"]  = _L("Left mouse button") + ": ";
    m_desc["enforce"]          = _L("Enforce supports");
    m_desc["block_caption"]    = _L("Right mouse button") + " ";
    m_desc["block"]            = _L("Block supports");
    m_desc["remove_caption"]   = _L("Shift + Left mouse button") + ": ";
    m_desc["remove"]           = _L("Remove selection");
    m_desc["remove_all"]       = _L("Remove all");

    return true;
}


void GLGizmoFdmSupports::activate_internal_undo_redo_stack(bool activate)
{
    if (activate && ! m_internal_stack_active) {
        Plater::TakeSnapshot(wxGetApp().plater(), _L("FDM gizmo turned on"));
        wxGetApp().plater()->enter_gizmos_stack();
        m_internal_stack_active = true;
    }
    if (! activate && m_internal_stack_active) {
        wxGetApp().plater()->leave_gizmos_stack();
        Plater::TakeSnapshot(wxGetApp().plater(), _L("FDM gizmo turned off"));
        m_internal_stack_active = false;
    }
}

void GLGizmoFdmSupports::set_fdm_support_data(ModelObject* model_object, const Selection& selection)
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



void GLGizmoFdmSupports::on_render() const
{
    //const Selection& selection = m_parent.get_selection();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    //render_triangles(selection);
    m_triangle_selector->render();
    m_c->object_clipper()->render_cut();
    render_cursor_circle();

    glsafe(::glDisable(GL_BLEND));
}

void GLGizmoFdmSupports::render_triangles(const Selection& selection) const
{
    if (m_setting_angle)
        return;

    const ModelObject* mo = m_c->selection_info()->model_object();

    glsafe(::glEnable(GL_POLYGON_OFFSET_FILL));
    ScopeGuard offset_fill_guard([]() { glsafe(::glDisable(GL_POLYGON_OFFSET_FILL)); } );
    glsafe(::glPolygonOffset(-1.0, 1.0));

    // Take care of the clipping plane. The normal of the clipping plane is
    // saved with opposite sign than we need to pass to OpenGL (FIXME)
    bool clipping_plane_active = m_c->object_clipper()->get_position() != 0.;
    if (clipping_plane_active) {
        const ClippingPlane* clp = m_c->object_clipper()->get_clipping_plane();
        double clp_data[4];
        memcpy(clp_data, clp->get_data(), 4 * sizeof(double));
        for (int i=0; i<3; ++i)
            clp_data[i] = -1. * clp_data[i];

        glsafe(::glClipPlane(GL_CLIP_PLANE0, (GLdouble*)clp_data));
        glsafe(::glEnable(GL_CLIP_PLANE0));
    }

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

        // Now render both enforcers and blockers.
        //for (int i=0; i<2; ++i) {
        //    glsafe(::glColor4f(i ? 1.f : 0.2f, 0.2f, i ? 0.2f : 1.0f, 0.5f));
        //    for (const GLIndexedVertexArray& iva : m_ivas[mesh_id][i]) {
                if (m_iva.has_VBOs())
                    m_iva.render();
        //    }
        //}
        glsafe(::glPopMatrix());
        if (is_left_handed)
            glsafe(::glFrontFace(GL_CCW));
    }
    if (clipping_plane_active)
        glsafe(::glDisable(GL_CLIP_PLANE0));
}


void GLGizmoFdmSupports::render_cursor_circle() const
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
}


void GLGizmoFdmSupports::update_model_object() const
{
    return;
    /*ModelObject* mo = m_c->selection_info()->model_object();
    int idx = -1;
    for (ModelVolume* mv : mo->volumes) {
        ++idx;
        if (! mv->is_model_part())
            continue;
        for (int i=0; i<int(m_selected_facets[idx].size()); ++i)
            mv->m_supported_facets.set_facet(i, m_selected_facets[idx][i]);
    }*/
}


void GLGizmoFdmSupports::update_from_model_object()
{
    wxBusyCursor wait;

    const ModelObject* mo = m_c->selection_info()->model_object();
    /*size_t num_of_volumes = 0;
    for (const ModelVolume* mv : mo->volumes)
        if (mv->is_model_part())
            ++num_of_volumes;
    m_selected_facets.resize(num_of_volumes);*/

    m_triangle_selector = std::make_unique<TriangleSelector>(mo->volumes.front()->mesh());

    /*m_ivas.clear();
    m_ivas.resize(num_of_volumes);
    for (size_t i=0; i<num_of_volumes; ++i) {
        m_ivas[i][0].reserve(MaxVertexBuffers);
        m_ivas[i][1].reserve(MaxVertexBuffers);
    }


    int volume_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++volume_id;

        // This mesh does not account for the possible Z up SLA offset.
        const TriangleMesh* mesh = &mv->mesh();

        m_selected_facets[volume_id].assign(mesh->its.indices.size(), FacetSupportType::NONE);

        // Load current state from ModelVolume.
        for (FacetSupportType type : {FacetSupportType::ENFORCER, FacetSupportType::BLOCKER}) {
            const std::vector<int>& list = mv->m_supported_facets.get_facets(type);
            for (int i : list)
                m_selected_facets[volume_id][i] = type;
        }
        update_vertex_buffers(mesh, volume_id, FacetSupportType::ENFORCER);
        update_vertex_buffers(mesh, volume_id, FacetSupportType::BLOCKER);
    }*/
}



bool GLGizmoFdmSupports::is_mesh_point_clipped(const Vec3d& point) const
{
    if (m_c->object_clipper()->get_position() == 0.)
        return false;

    auto sel_info = m_c->selection_info();
    int active_inst = m_c->selection_info()->get_active_instance();
    const ModelInstance* mi = sel_info->model_object()->instances[active_inst];
    const Transform3d& trafo = mi->get_transformation().get_matrix();

    Vec3d transformed_point =  trafo * point;
    transformed_point(2) += sel_info->get_sla_shift();
    return m_c->object_clipper()->get_clipping_plane()->is_point_clipped(transformed_point);
}


// Following function is called from GLCanvas3D to inform the gizmo about a mouse/keyboard event.
// The gizmo has an opportunity to react - if it does, it should return true so that the Canvas3D is
// aware that the event was reacted to and stops trying to make different sense of it. If the gizmo
// concludes that the event was not intended for it, it should return false.
bool GLGizmoFdmSupports::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
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

        if (! m_triangle_selector)
            return false;

        FacetSupportType new_state = FacetSupportType::NONE;
        if (! shift_down) {
            if (action == SLAGizmoEventType::Dragging)
                new_state = m_button_down == Button::Left
                        ? FacetSupportType::ENFORCER
                        : FacetSupportType::BLOCKER;
            else
                new_state = action == SLAGizmoEventType::LeftDown
                        ? FacetSupportType::ENFORCER
                        : FacetSupportType::BLOCKER;
        }

        const Camera& camera = wxGetApp().plater()->get_camera();
        const Selection& selection = m_parent.get_selection();
        const ModelObject* mo = m_c->selection_info()->model_object();
        const ModelInstance* mi = mo->instances[selection.get_instance_idx()];
        const Transform3d& instance_trafo = mi->get_transformation().get_matrix();

        std::vector<std::vector<std::pair<Vec3f, size_t>>> hit_positions_and_facet_ids;
        bool clipped_mesh_was_hit = false;

        Vec3f normal =  Vec3f::Zero();
        Vec3f hit = Vec3f::Zero();
        size_t facet = 0;
        Vec3f closest_hit = Vec3f::Zero();
        double closest_hit_squared_distance = std::numeric_limits<double>::max();
        size_t closest_facet = 0;
        int closest_hit_mesh_id = -1;

        // Transformations of individual meshes
        std::vector<Transform3d> trafo_matrices;

        int mesh_id = -1;
        // Cast a ray on all meshes, pick the closest hit and save it for the respective mesh
        for (const ModelVolume* mv : mo->volumes) {
            if (! mv->is_model_part())
                continue;

            ++mesh_id;

            trafo_matrices.push_back(instance_trafo * mv->get_matrix());
            hit_positions_and_facet_ids.push_back(std::vector<std::pair<Vec3f, size_t>>());

            if (m_c->raycaster()->raycasters()[mesh_id]->unproject_on_mesh(
                       mouse_position,
                       trafo_matrices[mesh_id],
                       camera,
                       hit,
                       normal,
                       m_clipping_plane.get(),
                       &facet))
            {
                // In case this hit is clipped, skip it.
                if (is_mesh_point_clipped(hit.cast<double>())) {
                    clipped_mesh_was_hit = true;
                    continue;
                }

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

        bool dragging_while_painting = (action == SLAGizmoEventType::Dragging && m_button_down != Button::None);

        // The mouse button click detection is enabled when there is a valid hit
        // or when the user clicks the clipping plane. Missing the object entirely
        // shall not capture the mouse.
        if (closest_hit_mesh_id != -1 || clipped_mesh_was_hit) {
            if (m_button_down == Button::None)
                m_button_down = ((action == SLAGizmoEventType::LeftDown) ? Button::Left : Button::Right);
        }

        if (closest_hit_mesh_id == -1) {
            // In case we have no valid hit, we can return. The event will
            // be stopped in following two cases:
            //  1. clicking the clipping plane
            //  2. dragging while painting (to prevent scene rotations and moving the object)
            return clipped_mesh_was_hit
                || dragging_while_painting;
        }

        // Find respective mesh id.
        // FIXME We need a separate TriangleSelector for each volume mesh.
        mesh_id = -1;
        //const TriangleMesh* mesh = nullptr;
        for (const ModelVolume* mv : mo->volumes) {
            if (! mv->is_model_part())
                continue;
            ++mesh_id;
            if (mesh_id == closest_hit_mesh_id) {
                //mesh = &mv->mesh();
                break;
            }
        }

        // FIXME: just for now, only process first mesh
        if (mesh_id != 0)
            return false;

        const Transform3d& trafo_matrix = trafo_matrices[mesh_id];

        // Calculate how far can a point be from the line (in mesh coords).
        // FIXME: The scaling of the mesh can be non-uniform.
        const Vec3d sf = Geometry::Transformation(trafo_matrix).get_scaling_factor();
        const float avg_scaling = (sf(0) + sf(1) + sf(2))/3.;
        const float limit = pow(m_cursor_radius/avg_scaling , 2.f);

        // Calculate direction from camera to the hit (in mesh coords):
        Vec3f dir = ((trafo_matrix.inverse() * camera.get_position()).cast<float>() - closest_hit).normalized();

        m_triangle_selector->select_patch(closest_hit, closest_facet, dir, limit, new_state);

        return true;
    }

    if ((action == SLAGizmoEventType::LeftUp || action == SLAGizmoEventType::RightUp)
      && m_button_down != Button::None) {
        // Take snapshot and update ModelVolume data.
        wxString action_name = shift_down
                ? _L("Remove selection")
                : (m_button_down == Button::Left
                   ? _L("Add supports")
                   : _L("Block supports"));
        activate_internal_undo_redo_stack(true);
        Plater::TakeSnapshot(wxGetApp().plater(), action_name);
        update_model_object();

        m_button_down = Button::None;
        return true;
    }

    return false;
}


void GLGizmoFdmSupports::update_vertex_buffers(const TriangleMesh* mesh,
                                               int mesh_id,
                                               FacetSupportType type,
                                               const std::vector<size_t>* new_facets)
{
    //std::vector<GLIndexedVertexArray>& ivas = m_ivas[mesh_id][type == FacetSupportType::ENFORCER ? 0 : 1];

    // lambda to push facet into vertex buffer
    auto push_facet = [this, &mesh, &mesh_id](size_t idx, GLIndexedVertexArray& iva) {
        for (int i=0; i<3; ++i)
            iva.push_geometry(
                mesh->its.vertices[mesh->its.indices[idx](i)].cast<double>(),
                m_c->raycaster()->raycasters()[mesh_id]->get_triangle_normal(idx).cast<double>()
            );
        size_t num = iva.triangle_indices_size;
        iva.push_triangle(num, num+1, num+2);
    };


    //if (ivas.size() == MaxVertexBuffers || ! new_facets) {
        // If there are too many or they should be regenerated, make one large
        // GLVertexBufferArray.
        //ivas.clear(); // destructors release geometry
        //ivas.push_back(GLIndexedVertexArray());

    m_iva.release_geometry();
    m_iva.clear();

        bool pushed = false;
        for (size_t facet_idx=0; facet_idx<m_selected_facets[mesh_id].size(); ++facet_idx) {
            if (m_selected_facets[mesh_id][facet_idx] == type) {
                push_facet(facet_idx, m_iva);
                pushed = true;
            }
        }
        if (pushed)
            m_iva.finalize_geometry(true);

    /*} else {
        // we are only appending - let's make new vertex array and let the old ones live
        ivas.push_back(GLIndexedVertexArray());
        for (size_t facet_idx : *new_facets)
            push_facet(facet_idx, ivas.back());

        if (! new_facets->empty())
            ivas.back().finalize_geometry(true);
        else
            ivas.pop_back();
    }*/

}


void GLGizmoFdmSupports::select_facets_by_angle(float threshold_deg, bool overwrite, bool block)
{
    return;
/*
    float threshold = (M_PI/180.)*threshold_deg;
    const Selection& selection = m_parent.get_selection();
    const ModelObject* mo = m_c->selection_info()->model_object();
    const ModelInstance* mi = mo->instances[selection.get_instance_idx()];

    int mesh_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++mesh_id;

        const Transform3d trafo_matrix = mi->get_matrix(true) * mv->get_matrix(true);
        Vec3f down  = (trafo_matrix.inverse() * (-Vec3d::UnitZ())).cast<float>().normalized();
        Vec3f limit = (trafo_matrix.inverse() * Vec3d(std::sin(threshold), 0, -std::cos(threshold))).cast<float>().normalized();

        float dot_limit = limit.dot(down);

        // Now calculate dot product of vert_direction and facets' normals.
        int idx = -1;
        for (const stl_facet& facet : mv->mesh().stl.facet_start) {
            ++idx;
            if (facet.normal.dot(down) > dot_limit && (overwrite || m_selected_facets[mesh_id][idx] == FacetSupportType::NONE))
                m_selected_facets[mesh_id][idx] = block
                        ? FacetSupportType::BLOCKER
                        : FacetSupportType::ENFORCER;
        }
        update_vertex_buffers(&mv->mesh(), mesh_id, FacetSupportType::ENFORCER);
        update_vertex_buffers(&mv->mesh(), mesh_id, FacetSupportType::BLOCKER);
    }

    activate_internal_undo_redo_stack(true);

    Plater::TakeSnapshot(wxGetApp().plater(), block ? _L("Block supports by angle")
                                                    : _L("Add supports by angle"));
    update_model_object();
    m_parent.set_as_dirty();
    m_setting_angle = false;
*/
}


void GLGizmoFdmSupports::on_render_input_window(float x, float y, float bottom_limit)
{
    if (! m_c->selection_info()->model_object())
        return;

    const float approx_height = m_imgui->scaled(18.0f);
    y = std::min(y, bottom_limit - approx_height);
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);

    if (! m_setting_angle) {
        m_imgui->begin(on_get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

        // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:
        const float clipping_slider_left = std::max(m_imgui->calc_text_size(m_desc.at("clipping_of_view")).x, m_imgui->calc_text_size(m_desc.at("reset_direction")).x) + m_imgui->scaled(1.5f);
        const float cursor_slider_left = m_imgui->calc_text_size(m_desc.at("cursor_size")).x + m_imgui->scaled(1.f);
        const float button_width = m_imgui->calc_text_size(m_desc.at("remove_all")).x + m_imgui->scaled(1.f);
        const float minimal_slider_width = m_imgui->scaled(4.f);

        float caption_max = 0.f;
        float total_text_max = 0.;
        for (const std::string& t : {"enforce", "block", "remove"}) {
            caption_max = std::max(caption_max, m_imgui->calc_text_size(m_desc.at(t+"_caption")).x);
            total_text_max = std::max(total_text_max, caption_max + m_imgui->calc_text_size(m_desc.at(t)).x);
        }
        caption_max += m_imgui->scaled(1.f);
        total_text_max += m_imgui->scaled(1.f);

        float window_width = minimal_slider_width + std::max(cursor_slider_left, clipping_slider_left);
        window_width = std::max(window_width, total_text_max);
        window_width = std::max(window_width, button_width);

        auto draw_text_with_caption = [this, &caption_max](const wxString& caption, const wxString& text) {
            static const ImVec4 ORANGE(1.0f, 0.49f, 0.22f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ORANGE);
            m_imgui->text(caption);
            ImGui::PopStyleColor();
            ImGui::SameLine(caption_max);
            m_imgui->text(text);
        };

        for (const std::string& t : {"enforce", "block", "remove"})
            draw_text_with_caption(m_desc.at(t + "_caption"), m_desc.at(t));

        m_imgui->text("");

        if (m_imgui->button("Autoset by angle...")) {
            m_setting_angle = true;
        }

        ImGui::SameLine();

        if (m_imgui->button(m_desc.at("remove_all"))) {
            /*ModelObject* mo = m_c->selection_info()->model_object();
            int idx = -1;
            for (ModelVolume* mv : mo->volumes) {
                ++idx;
                if (mv->is_model_part()) {
                    m_selected_facets[idx].assign(m_selected_facets[idx].size(), FacetSupportType::NONE);
                    mv->m_supported_facets.clear();
                    update_vertex_buffers(&mv->mesh(), idx, FacetSupportType::ENFORCER);
                    update_vertex_buffers(&mv->mesh(), idx, FacetSupportType::BLOCKER);
                    m_parent.set_as_dirty();
                }
            }*/
        }

        const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;

        m_imgui->text(m_desc.at("cursor_size"));
        ImGui::SameLine(clipping_slider_left);
        ImGui::PushItemWidth(window_width - clipping_slider_left);
        ImGui::SliderFloat(" ", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f");
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(max_tooltip_width);
            ImGui::TextUnformatted(_L("Alt + Mouse wheel").ToUTF8().data());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        ImGui::Separator();
        if (m_c->object_clipper()->get_position() == 0.f)
            m_imgui->text(m_desc.at("clipping_of_view"));
        else {
            if (m_imgui->button(m_desc.at("reset_direction"))) {
                wxGetApp().CallAfter([this](){
                        m_c->object_clipper()->set_position(-1., false);
                    });
            }
        }

        ImGui::SameLine(clipping_slider_left);
        ImGui::PushItemWidth(window_width - clipping_slider_left);
        float clp_dist = m_c->object_clipper()->get_position();
        if (ImGui::SliderFloat("  ", &clp_dist, 0.f, 1.f, "%.2f"))
        m_c->object_clipper()->set_position(clp_dist, true);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(max_tooltip_width);
            ImGui::TextUnformatted(_L("Ctrl + Mouse wheel").ToUTF8().data());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }

        m_imgui->end();
        if (m_setting_angle) {
            m_parent.show_slope(false);
            m_parent.set_slope_range({90.f - m_angle_threshold_deg, 90.f - m_angle_threshold_deg});
            m_parent.use_slope(true);
            m_parent.set_as_dirty();
        }
    }
    else {
        std::string name = "Autoset custom supports";
        m_imgui->begin(wxString(name), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
        m_imgui->text("Threshold:");
        ImGui::SameLine();
        if (m_imgui->slider_float("", &m_angle_threshold_deg, 0.f, 90.f, "%.f"))
            m_parent.set_slope_range({90.f - m_angle_threshold_deg, 90.f - m_angle_threshold_deg});
        m_imgui->checkbox(wxString("Overwrite already selected facets"), m_overwrite_selected);
        if (m_imgui->button("Enforce"))
            select_facets_by_angle(m_angle_threshold_deg, m_overwrite_selected, false);
        ImGui::SameLine();
        if (m_imgui->button("Block"))
            select_facets_by_angle(m_angle_threshold_deg, m_overwrite_selected, true);
        ImGui::SameLine();
        if (m_imgui->button("Cancel"))
            m_setting_angle = false;
        m_imgui->end();
        if (! m_setting_angle) {
            m_parent.use_slope(false);
            m_parent.set_as_dirty();
        }
    }
}

bool GLGizmoFdmSupports::on_is_activable() const
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

bool GLGizmoFdmSupports::on_is_selectable() const
{
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF );
}

std::string GLGizmoFdmSupports::on_get_name() const
{
    return (_(L("FDM Support Editing")) + " [L]").ToUTF8().data();
}


CommonGizmosDataID GLGizmoFdmSupports::on_get_requirements() const
{
    return CommonGizmosDataID(
                int(CommonGizmosDataID::SelectionInfo)
              | int(CommonGizmosDataID::InstancesHider)
              | int(CommonGizmosDataID::Raycaster)
              | int(CommonGizmosDataID::HollowedMesh)
              | int(CommonGizmosDataID::ObjectClipper)
              | int(CommonGizmosDataID::SupportsClipper));
}


void GLGizmoFdmSupports::on_set_state()
{
    if (m_state == m_old_state)
        return;

    if (m_state == On && m_old_state != On) { // the gizmo was just turned on
        if (! m_parent.get_gizmos_manager().is_serializing()) {
            wxGetApp().CallAfter([this]() {
                activate_internal_undo_redo_stack(true);
            });
        }
    }
    if (m_state == Off && m_old_state != Off) { // the gizmo was just turned Off
        // we are actually shutting down
        if (m_setting_angle) {
            m_setting_angle = false;
            m_parent.use_slope(false);
        }
        activate_internal_undo_redo_stack(false);
        m_old_mo_id = -1;
        m_iva.release_geometry();
        m_selected_facets.clear();
    }
    m_old_state = m_state;
}



void GLGizmoFdmSupports::on_start_dragging()
{

}


void GLGizmoFdmSupports::on_stop_dragging()
{

}



void GLGizmoFdmSupports::on_load(cereal::BinaryInputArchive&)
{
    // We should update the gizmo from current ModelObject, but it is not
    // possible at this point. That would require having updated selection and
    // common gizmos data, which is not done at this point. Instead, save
    // a flag to do the update in set_fdm_support_data, which will be called
    // soon after.
    m_schedule_update = true;
}



void GLGizmoFdmSupports::on_save(cereal::BinaryOutputArchive&) const
{

}



void TriangleSelector::DivisionNode::set_division(int sides_to_split, int special_side_idx)
{
    assert(sides_to_split >=0 && sides_to_split <= 3);
    assert(special_side_idx >=-1 && special_side_idx < 3);

    // If splitting one or two sides, second argument must be provided.
    assert(sides_to_split != 1 || special_side_idx != -1);
    assert(sides_to_split != 2 || special_side_idx != -1);

    division_type = sides_to_split | ((special_side_idx != -1 ? special_side_idx : 0 ) <<2);
}



void TriangleSelector::DivisionNode::set_state(FacetSupportType type)
{
    // If this is not a leaf-node, this makes no sense and
    // the bits are used for storing index of an edge.
    assert(number_of_split_sides() == 0);
    division_type = (int8_t(type) << 2);
}



int TriangleSelector::DivisionNode::side_to_keep() const
{
    assert(number_of_split_sides() == 2);
    return (division_type & 0b1100) >> 2;
}



int TriangleSelector::DivisionNode::side_to_split() const
{
    assert(number_of_split_sides() == 1);
    return (division_type & 0b1100) >> 2;
}



FacetSupportType TriangleSelector::DivisionNode::get_state() const
{
    assert(number_of_split_sides() == 0); // this must be leaf
    return FacetSupportType((division_type & 0b1100) >> 2);
}



void TriangleSelector::select_patch(const Vec3f& hit, int facet_start, const Vec3f& dir,
                                    float radius_sqr, FacetSupportType new_state)
{
    assert(facet_start < m_orig_size_indices);

    // Save current cursor center, squared radius and camera direction,
    // so we don't have to pass it around.
    m_cursor = {hit, dir, radius_sqr};

    // Now start with the facet the pointer points to and check all adjacent facets.
    std::vector<int> facets_to_check{facet_start};
    std::vector<bool> visited(m_orig_size_indices, false); // keep track of facets we already processed
    int facet_idx = 0; // index into facets_to_check
    while (facet_idx < int(facets_to_check.size())) {
        int facet = facets_to_check[facet_idx];
        if (! visited[facet]) {
            int num_of_inside_vertices = vertices_inside(facet);
            // select the facet...
            select_triangle(facet, new_state, num_of_inside_vertices, facet == facet_start);

            // ...and add neighboring facets to be proccessed later
            for (int n=0; n<3; ++n) {
                if (faces_camera(m_mesh->stl.neighbors_start[facet].neighbor[n]))
                    facets_to_check.push_back(m_mesh->stl.neighbors_start[facet].neighbor[n]);
            }
        }
        visited[facet] = true;
        ++facet_idx;
    }
}



// Selects either the whole triangle (discarding any children it has), or divides
// the triangle recursively, selecting just subtriangles truly inside the circle.
// This is done by an actual recursive call.
void TriangleSelector::select_triangle(int facet_idx, FacetSupportType type,
                                       int num_of_inside_vertices, bool cursor_inside)
{
    assert(facet_idx < m_triangles.size());
//cursor_inside=false;
    if (num_of_inside_vertices == -1)
        num_of_inside_vertices = vertices_inside(facet_idx);

    if (num_of_inside_vertices == 0 && ! cursor_inside)
        return; // FIXME: just an edge can be inside

    if (num_of_inside_vertices == 3) {
        // dump any subdivision and select whole triangle
        undivide_triangle(facet_idx);
        m_triangles[facet_idx].div_info->set_state(type);
    } else {
        // the triangle is partially inside, let's recursively divide it
        // (if not already) and try selecting its children.
        split_triangle(facet_idx);
        assert(facet_idx < m_triangles.size());
        int num_of_children = m_triangles[facet_idx].div_info->number_of_split_sides() + 1;
        if (num_of_children != 1) {
            for (int i=0; i<num_of_children; ++i) {
                select_triangle(m_triangles[facet_idx].div_info->children[i], type, -1, cursor_inside);
            }
        }
    }
    //if (m_triangles[facet_idx].div_info->number_of_split_sides() != 0)
    //    remove_needless(m_triangles[facet_idx].div_info->children[0]);
}


bool TriangleSelector::split_triangle(int facet_idx)
{
    if (m_triangles[facet_idx].div_info->number_of_split_sides() != 0) {
        // The triangle was divided already.
        return 0;
    }

    FacetSupportType old_type = m_triangles[facet_idx].div_info->get_state();

    const double limit_squared = 4;

    stl_triangle_vertex_indices& facet = m_triangles[facet_idx].verts_idxs;
    const stl_vertex* pts[3] = { &m_vertices[facet[0]], &m_vertices[facet[1]], &m_vertices[facet[2]]};
    double sides[3] = { (*pts[2]-*pts[1]).squaredNorm(), (*pts[0]-*pts[2]).squaredNorm(), (*pts[1]-*pts[0]).squaredNorm() };

    std::vector<int> sides_to_split;
    int side_to_keep = -1;
    for (int pt_idx = 0; pt_idx<3; ++pt_idx) {
        if (sides[pt_idx] > limit_squared)
            sides_to_split.push_back(pt_idx);
        else
            side_to_keep = pt_idx;
    }
    if (sides_to_split.empty()) {
        m_triangles[facet_idx].div_info->set_division(0);
        return 0;
    }

    // indices of triangle vertices
    std::vector<int> verts_idxs;
    int idx = sides_to_split.size() == 2 ? side_to_keep : sides_to_split[0];
    for (int j=0; j<3; ++j) {
        verts_idxs.push_back(facet[idx++]);
        if (idx == 3)
            idx = 0;
    }


   if (sides_to_split.size() == 1) {
        m_vertices.emplace_back((m_vertices[verts_idxs[1]] + m_vertices[verts_idxs[2]])/2.);
        verts_idxs.insert(verts_idxs.begin()+2, m_vertices.size() - 1);

        m_triangles.emplace_back(verts_idxs[0], verts_idxs[1], verts_idxs[2]);
        m_triangles.emplace_back(verts_idxs[2], verts_idxs[3], verts_idxs[0]);
    }

    if (sides_to_split.size() == 2) {
        m_vertices.emplace_back((m_vertices[verts_idxs[0]] + m_vertices[verts_idxs[1]])/2.);
        verts_idxs.insert(verts_idxs.begin()+1, m_vertices.size() - 1);

        m_vertices.emplace_back((m_vertices[verts_idxs[0]] + m_vertices[verts_idxs[3]])/2.);
        verts_idxs.insert(verts_idxs.begin()+4, m_vertices.size() - 1);

        m_triangles.emplace_back(verts_idxs[0], verts_idxs[1], verts_idxs[4]);
        m_triangles.emplace_back(verts_idxs[1], verts_idxs[2], verts_idxs[4]);
        m_triangles.emplace_back(verts_idxs[2], verts_idxs[3], verts_idxs[4]);
    }

    if (sides_to_split.size() == 3) {
        m_vertices.emplace_back((m_vertices[verts_idxs[0]] + m_vertices[verts_idxs[1]])/2.);
        verts_idxs.insert(verts_idxs.begin()+1, m_vertices.size() - 1);
        m_vertices.emplace_back((m_vertices[verts_idxs[2]] + m_vertices[verts_idxs[3]])/2.);
        verts_idxs.insert(verts_idxs.begin()+3, m_vertices.size() - 1);
        m_vertices.emplace_back((m_vertices[verts_idxs[4]] + m_vertices[verts_idxs[0]])/2.);
        verts_idxs.insert(verts_idxs.begin()+5, m_vertices.size() - 1);

        m_triangles.emplace_back(verts_idxs[0], verts_idxs[1], verts_idxs[5]);
        m_triangles.emplace_back(verts_idxs[1], verts_idxs[2], verts_idxs[3]);
        m_triangles.emplace_back(verts_idxs[3], verts_idxs[4], verts_idxs[5]);
        m_triangles.emplace_back(verts_idxs[1], verts_idxs[3], verts_idxs[5]);
    }

    // Save how the triangle was split. Second argument makes sense only for one
    // or two split sides, otherwise the value is ignored.
    m_triangles[facet_idx].div_info->set_division(sides_to_split.size(),
        sides_to_split.size() == 2 ? side_to_keep : sides_to_split[0]);

    // And save the children. All children should start in the same state as the triangle we just split.
    assert(! sides_to_split.empty() && int(sides_to_split.size()) <= 3);
    for (int i=0; i<=int(sides_to_split.size()); ++i) {
        m_triangles[facet_idx].div_info->children[i] = m_triangles.size()-1-i;
        m_triangles[m_triangles.size()-1-i].div_info->parent = facet_idx;
        m_triangles[m_triangles[facet_idx].div_info->children[i]].div_info->set_state(old_type);
    }


#ifndef NDEBUG
    int split_sides = m_triangles[facet_idx].div_info->number_of_split_sides();
    if (split_sides != 0) {
        // check that children are range
        for (int i=0; i<=split_sides; ++i)
            assert(m_triangles[facet_idx].div_info->children[i] >= 0 && m_triangles[facet_idx].div_info->children[i] < int(m_triangles.size()));

    }
#endif

    return 1;
}


// Calculate distance of a point from a line.
bool TriangleSelector::is_point_inside_cursor(const Vec3f& point) const
{
    Vec3f diff = m_cursor.center - point;
    return (diff - diff.dot(m_cursor.dir) * m_cursor.dir).squaredNorm() < m_cursor.radius_sqr;
}



// Determine whether this facet is potentially visible (still can be obscured).
bool TriangleSelector::faces_camera(int facet) const
{
    assert(facet < m_orig_size_indices);
    // The normal is cached in mesh->stl, use it.
    return (m_mesh->stl.facet_start[facet].normal.dot(m_cursor.dir) > 0.);
}



// How many vertices of a triangle are inside the circle?
int TriangleSelector::vertices_inside(int facet_idx) const
{
    int inside = 0;
    for (size_t i=0; i<3; ++i) {
        if (is_point_inside_cursor(m_vertices[m_triangles[facet_idx].verts_idxs[i]]))
            ++inside;
    }
    return inside;
}


// Is mouse pointer inside a triangle?
/*bool TriangleSelector::is_pointer_inside_triangle(int facet_idx) const
{

}*/



// Recursively remove all subtriangles.
void TriangleSelector::undivide_triangle(int facet_idx)
{
    assert(facet_idx < m_triangles.size());
    auto& dn_ptr = m_triangles[facet_idx].div_info;
    assert(dn_ptr);

    if (dn_ptr->number_of_split_sides() != 0) {
        for (int i=0; i<=dn_ptr->number_of_split_sides(); ++i) {
            undivide_triangle(dn_ptr->children[i]);
            m_triangles[dn_ptr->children[i]].div_info->valid = false;
        }
    }

    dn_ptr->set_division(0); // not split
}


void TriangleSelector::remove_needless(int child_facet)
{
    assert(m_triangles[child_facet].div_info->number_of_split_sides() == 0);
    int parent = m_triangles[child_facet].div_info->parent;
    if (parent == -1)
        return; // root
    // Check type of all valid children.
    FacetSupportType type = m_triangles[m_triangles[parent].div_info->children[0]].div_info->get_state();
    for (int i=0; i<=m_triangles[parent].div_info->number_of_split_sides(); ++i)
        if (m_triangles[m_triangles[parent].div_info->children[0]].div_info->get_state() != type)
            return; // not all children are the same

    // All children are the same, let's kill them.
    undivide_triangle(parent);
    m_triangles[parent].div_info->set_state(type);

    // And not try the same for grandparent.
    remove_needless(parent);
}


TriangleSelector::TriangleSelector(const TriangleMesh& mesh)
{
    for (const stl_vertex& vert : mesh.its.vertices)
        m_vertices.push_back(vert);
    for (const stl_triangle_vertex_indices& ind : mesh.its.indices)
        m_triangles.emplace_back(Triangle(ind[0], ind[1], ind[2]));
    m_orig_size_vertices = m_vertices.size();
    m_orig_size_indices = m_triangles.size();
    m_mesh = &mesh;
}


void TriangleSelector::render() const
{
    ::glColor3f(0.f, 0.f, 1.f);
    ::glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

    Vec3d offset = wxGetApp().model().objects.front()->instances.front()->get_transformation().get_offset();
    ::glTranslatef(offset.x(), offset.y(), offset.z());
    ::glScalef(1.01f, 1.01f, 1.01f);

    ::glBegin( GL_TRIANGLES);

    for (int tr_id=0; tr_id<m_triangles.size(); ++tr_id) {
        if (tr_id == m_orig_size_indices)
            ::glColor3f(1.f, 0.f, 0.f);
        const Triangle& tr = m_triangles[tr_id];
        if (tr.div_info->valid) {
            for (int i=0; i<3; ++i)
                ::glVertex3f(m_vertices[tr.verts_idxs[i]][0], m_vertices[tr.verts_idxs[i]][1], m_vertices[tr.verts_idxs[i]][2]);
        }
    }
    ::glEnd();
    ::glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );

    ::glBegin( GL_TRIANGLES);
    for (int tr_id=0; tr_id<m_triangles.size(); ++tr_id) {
        const Triangle& tr = m_triangles[tr_id];
        if (! tr.div_info->valid)
            continue;

        if (tr.div_info->number_of_split_sides() == 0) {
            if (tr.div_info->get_state() == FacetSupportType::ENFORCER)
                ::glColor4f(0.f, 0.f, 1.f, 0.2f);
            else if (tr.div_info->get_state() == FacetSupportType::BLOCKER)
                ::glColor4f(1.f, 0.f, 0.f, 0.2f);
            else
                continue;
        }
        else
            continue;


        if (tr.div_info->valid) {
            for (int i=0; i<3; ++i)
                ::glVertex3f(m_vertices[tr.verts_idxs[i]][0], m_vertices[tr.verts_idxs[i]][1], m_vertices[tr.verts_idxs[i]][2]);
        }
    }
    ::glEnd();
}


TriangleSelector::DivisionNode::DivisionNode()
{
    set_division(0);
    set_state(FacetSupportType::NONE);
}

} // namespace GUI
} // namespace Slic3r
