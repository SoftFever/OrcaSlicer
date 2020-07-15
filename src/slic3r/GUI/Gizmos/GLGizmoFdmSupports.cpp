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
    m_desc["remove_all"]       = _L("Remove all selection");

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
    const Selection& selection = m_parent.get_selection();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_triangles(selection);

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

        if (! m_setting_angle)
            m_triangle_selectors[mesh_id]->render(m_imgui);

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
    bool updated = false;
    ModelObject* mo = m_c->selection_info()->model_object();
    int idx = -1;
    for (ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;
        ++idx;
        updated |= mv->m_supported_facets.set(*m_triangle_selectors[idx].get());
    }

    if (updated)
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
}


void GLGizmoFdmSupports::update_from_model_object()
{
    wxBusyCursor wait;

    const ModelObject* mo = m_c->selection_info()->model_object();
    m_triangle_selectors.clear();

    int volume_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++volume_id;

        // This mesh does not account for the possible Z up SLA offset.
        const TriangleMesh* mesh = &mv->mesh();

        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorGUI>(*mesh));
        m_triangle_selectors.back()->deserialize(mv->m_supported_facets.get_data());
    }
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

        if (m_triangle_selectors.empty())
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

        const Transform3d& trafo_matrix = trafo_matrices[mesh_id];

        // Calculate how far can a point be from the line (in mesh coords).
        // FIXME: The scaling of the mesh can be non-uniform.
        const Vec3d sf = Geometry::Transformation(trafo_matrix).get_scaling_factor();
        const float avg_scaling = (sf(0) + sf(1) + sf(2))/3.;
        const float limit = std::pow(m_cursor_radius/avg_scaling , 2.f);

        // Calculate direction from camera to the hit (in mesh coords):
        Vec3f camera_pos = (trafo_matrix.inverse() * camera.get_position()).cast<float>();
        Vec3f dir = (closest_hit - camera_pos).normalized();

        assert(mesh_id < int(m_triangle_selectors.size()));
        m_triangle_selectors[mesh_id]->select_patch(closest_hit, closest_facet, camera_pos,
                                          dir, limit, new_state);

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



void GLGizmoFdmSupports::select_facets_by_angle(float threshold_deg, bool block)
{
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
            if (facet.normal.dot(down) > dot_limit)
                m_triangle_selectors[mesh_id]->set_facet(idx,
                                                         block
                                                         ? FacetSupportType::BLOCKER
                                                         : FacetSupportType::ENFORCER);
        }
    }

    activate_internal_undo_redo_stack(true);

    Plater::TakeSnapshot(wxGetApp().plater(), block ? _L("Block supports by angle")
                                                    : _L("Add supports by angle"));
    update_model_object();
    m_parent.set_as_dirty();
    m_setting_angle = false;
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
            Plater::TakeSnapshot(wxGetApp().plater(), wxString(_L("Reset selection")));
            ModelObject* mo = m_c->selection_info()->model_object();
            int idx = -1;
            for (ModelVolume* mv : mo->volumes) {
                if (mv->is_model_part()) {
                    ++idx;
                    m_triangle_selectors[idx]->reset();
                }
            }
            update_model_object();
            m_parent.set_as_dirty();
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
        if (m_imgui->button("Enforce"))
            select_facets_by_angle(m_angle_threshold_deg, false);
        ImGui::SameLine();
        if (m_imgui->button("Block"))
            select_facets_by_angle(m_angle_threshold_deg, true);
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
              | int(CommonGizmosDataID::ObjectClipper));
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
        //m_iva.release_geometry();
        m_triangle_selectors.clear();
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


void TriangleSelectorGUI::render(ImGuiWrapper* imgui)
{
    int enf_cnt = 0;
    int blc_cnt = 0;

    m_iva_enforcers.release_geometry();
    m_iva_blockers.release_geometry();

    for (const Triangle& tr : m_triangles) {
        if (! tr.valid || tr.is_split() || tr.get_state() == FacetSupportType::NONE)
            continue;

        GLIndexedVertexArray& va = tr.get_state() == FacetSupportType::ENFORCER
                                   ? m_iva_enforcers
                                   : m_iva_blockers;
        int& cnt = tr.get_state() == FacetSupportType::ENFORCER
                ? enf_cnt
                : blc_cnt;

        for (int i=0; i<3; ++i)
            va.push_geometry(double(m_vertices[tr.verts_idxs[i]].v[0]),
                             double(m_vertices[tr.verts_idxs[i]].v[1]),
                             double(m_vertices[tr.verts_idxs[i]].v[2]),
                             0., 0., 1.);
        va.push_triangle(cnt,
                         cnt+1,
                         cnt+2);
        cnt += 3;
    }

    m_iva_enforcers.finalize_geometry(true);
    m_iva_blockers.finalize_geometry(true);

    if (m_iva_enforcers.has_VBOs()) {
        ::glColor4f(0.f, 0.f, 1.f, 0.2f);
        m_iva_enforcers.render();
    }


    if (m_iva_blockers.has_VBOs()) {
        ::glColor4f(1.f, 0.f, 0.f, 0.2f);
        m_iva_blockers.render();
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
