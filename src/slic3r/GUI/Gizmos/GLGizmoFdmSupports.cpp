// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoFdmSupports.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/PresetBundle.hpp"
#include "slic3r/GUI/Camera.hpp"



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
    const ModelObject* mo = m_c->selection_info() ? m_c->selection_info()->model_object() : nullptr;
    if (! mo)
        return;

    if (mo && selection.is_from_single_instance()
     && (mo != m_old_mo || mo->volumes.size() != m_old_volumes_size))
    {
        update_mesh();
        m_old_mo = mo;
        m_old_volumes_size = mo->volumes.size();
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
    const ModelObject* mo = m_c->selection_info()->model_object();

    for (size_t mesh_id=0; mesh_id<mo->volumes.size(); ++mesh_id) {

        const Transform3d trafo_matrix =
            mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() *
            mo->volumes[mesh_id]->get_matrix();
        const TriangleMesh* mesh = &mo->volumes[mesh_id]->mesh();



        ::glColor3f(0.0f, 0.37f, 1.0f);

        for (size_t facet_idx=0; facet_idx<m_selected_facets[mesh_id].size(); ++facet_idx) {
            if (! m_selected_facets[mesh_id][facet_idx])
                continue;
            stl_normal normal = 0.01f * MeshRaycaster::get_triangle_normal(mesh->its, facet_idx);
            ::glPushMatrix();
            ::glTranslatef(normal(0), normal(1), normal(2));
            ::glMultMatrixd(trafo_matrix.data());

            ::glBegin(GL_TRIANGLES);
            ::glVertex3f(mesh->its.vertices[mesh->its.indices[facet_idx](0)](0), mesh->its.vertices[mesh->its.indices[facet_idx](0)](1), mesh->its.vertices[mesh->its.indices[facet_idx](0)](2));
            ::glVertex3f(mesh->its.vertices[mesh->its.indices[facet_idx](1)](0), mesh->its.vertices[mesh->its.indices[facet_idx](1)](1), mesh->its.vertices[mesh->its.indices[facet_idx](1)](2));
            ::glVertex3f(mesh->its.vertices[mesh->its.indices[facet_idx](2)](0), mesh->its.vertices[mesh->its.indices[facet_idx](2)](1), mesh->its.vertices[mesh->its.indices[facet_idx](2)](2));
            ::glEnd();
            ::glPopMatrix();
        }
    }
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


void GLGizmoFdmSupports::on_render_for_picking() const
{

}



void GLGizmoFdmSupports::update_mesh()
{
    wxBusyCursor wait;

    const ModelObject* mo = m_c->selection_info()->model_object();
    size_t num_of_volumes = mo->volumes.size();
    m_selected_facets.resize(num_of_volumes);
    m_neighbors.resize(num_of_volumes);

    for (size_t volume_id=0; volume_id<num_of_volumes; ++volume_id) {
        // This mesh does not account for the possible Z up SLA offset.
        const TriangleMesh* mesh = &mo->volumes[volume_id]->mesh();

        m_selected_facets[volume_id].assign(mesh->its.indices.size(), false);
        m_neighbors[volume_id].resize(3 * mesh->its.indices.size());

        // Prepare vector of vertex_index - facet_index pairs to quickly find adjacent facets
        for (size_t i=0; i<mesh->its.indices.size(); ++i) {
            const stl_triangle_vertex_indices& ind  = mesh->its.indices[i];
            m_neighbors[volume_id][3*i] = std::make_pair(ind(0), i);
            m_neighbors[volume_id][3*i+1] = std::make_pair(ind(1), i);
            m_neighbors[volume_id][3*i+2] = std::make_pair(ind(2), i);
        }
        std::sort(m_neighbors[volume_id].begin(), m_neighbors[volume_id].end());
    }
}




bool operator<(const GLGizmoFdmSupports::NeighborData& a, const GLGizmoFdmSupports::NeighborData& b) {
    return a.first < b.first;
}


// Following function is called from GLCanvas3D to inform the gizmo about a mouse/keyboard event.
// The gizmo has an opportunity to react - if it does, it should return true so that the Canvas3D is
// aware that the event was reacted to and stops trying to make different sense of it. If the gizmo
// concludes that the event was not intended for it, it should return false.
bool GLGizmoFdmSupports::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    if (action == SLAGizmoEventType::MouseWheelUp && control_down) {
        double pos = m_c->object_clipper()->get_position();
        pos = std::min(1., pos + 0.01);
        m_c->object_clipper()->set_position(pos, true);
        return true;
    }

    if (action == SLAGizmoEventType::MouseWheelDown && control_down) {
        double pos = m_c->object_clipper()->get_position();
        pos = std::max(0., pos - 0.01);
        m_c->object_clipper()->set_position(pos, true);
        return true;
    }

    if (action == SLAGizmoEventType::ResetClippingPlane) {
        m_c->object_clipper()->set_position(-1., false);
        return true;
    }

    if (action == SLAGizmoEventType::LeftDown || (action == SLAGizmoEventType::Dragging && m_wait_for_up_event)) {
        bool select = ! shift_down;
        const Camera& camera = wxGetApp().plater()->get_camera();
        const Selection& selection = m_parent.get_selection();
        const ModelInstance* mi = m_c->selection_info()->model_object()->instances[selection.get_instance_idx()];
        const Transform3d& instance_trafo = mi->get_transformation().get_matrix();

        // Precalculate transformations of individual meshes
        std::vector<Transform3d> trafo_matrices;
        const std::vector<ModelVolume*>& volumes = m_c->selection_info()->model_object()->volumes;
        for (const ModelVolume* mv : volumes)
            trafo_matrices.push_back(instance_trafo * mv->get_matrix());

        std::vector<std::vector<std::pair<Vec3f, size_t>>> hit_positions_and_facet_ids(volumes.size());
        bool some_mesh_was_hit = false;

        // Cast a ray on all meshes, pick the closest hit and save it for the respective mesh
        Vec3f normal =  Vec3f::Zero();
        Vec3f hit = Vec3f::Zero();
        size_t facet = 0;
        Vec3f closest_hit = Vec3f::Zero();
        double closest_hit_squared_distance = std::numeric_limits<double>::max();
        size_t closest_facet = 0;
        size_t closest_hit_mesh_id = size_t(-1);

        for (size_t mesh_id=0; mesh_id<volumes.size(); ++mesh_id) {

            if (m_c->raycaster()->raycasters()[mesh_id]->unproject_on_mesh(
                       mouse_position,
                       trafo_matrices[mesh_id],
                       camera,
                       hit,
                       normal,
                       m_clipping_plane.get(),
                       &facet))
            {
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
        // We now know where the ray hit, let's save it and cast another ray
        if (closest_hit_mesh_id != size_t(-1)) // only if there is at least one hit
            hit_positions_and_facet_ids[closest_hit_mesh_id].emplace_back(closest_hit, closest_facet);


        // Now propagate the hits
        for (size_t mesh_id=0; mesh_id<volumes.size(); ++mesh_id) {
            // For all hits on this mesh...
            for (const std::pair<Vec3f, size_t>& hit_and_facet : hit_positions_and_facet_ids[mesh_id]) {
                some_mesh_was_hit = true;
                const TriangleMesh* mesh = &volumes[mesh_id]->mesh();
                std::vector<NeighborData>& neighbors = m_neighbors[mesh_id];

                // Calculate direction from camera to the hit (in mesh coords):
                const Transform3d& trafo_matrix = trafo_matrices[mesh_id];

                Vec3f dir = ((trafo_matrix.inverse() * camera.get_position()).cast<float>() - hit_and_facet.first).normalized();

                // Calculate how far can a point be from the line (in mesh coords).
                // FIXME: This should account for (possibly non-uniform) scaling of the mesh.
                float limit = pow(m_cursor_radius, 2.f);

                // A lambda to calculate distance from the centerline:
                auto squared_distance_from_line = [&hit_and_facet, &dir](const Vec3f point) -> float {
                    Vec3f diff = hit_and_facet.first - point;
                    return (diff - diff.dot(dir) * dir).squaredNorm();
                };

                // A lambda to determine whether this facet is potentionally visible (still can be obscured)
                auto faces_camera = [&dir, &volumes](const size_t& mesh_id, const size_t& facet) -> bool {
                    return (volumes[mesh_id]->mesh().stl.facet_start[facet].normal.dot(dir) > 0.);
                };
                // Now start with the facet the pointer points to and check all adjacent facets. neighbors vector stores
                // pairs of vertex_idx - facet_idx and is sorted with respect to the former. Neighboring facet index can be
                // quickly found by finding a vertex in the list and read the respective facet ids.
                std::vector<size_t> facets_to_select{hit_and_facet.second};
                NeighborData vertex = std::make_pair(0, 0);
                std::vector<bool> visited(m_selected_facets[mesh_id].size(), false); // keep track of facets we already processed
                size_t facet_idx = 0; // index into facets_to_select
                auto it = neighbors.end();
                while (facet_idx < facets_to_select.size()) {
                    size_t facet = facets_to_select[facet_idx];
                    if (! visited[facet]) {
                        // check all three vertices and in case they're close enough, find the remaining facets
                        // and add them to the list to be proccessed later
                        for (size_t i=0; i<3; ++i) {
                            vertex.first = mesh->its.indices[facet](i); // vertex index
                            float dist = squared_distance_from_line(mesh->its.vertices[vertex.first]);
                            if (dist < limit) {
                                it = std::lower_bound(neighbors.begin(), neighbors.end(), vertex);
                                while (it != neighbors.end() && it->first == vertex.first) {
                                    if (it->second != facet && faces_camera(mesh_id, it->second))
                                        facets_to_select.push_back(it->second);
                                    ++it;
                                }
                            }
                        }
                        visited[facet] = true;
                    }
                    ++facet_idx;
                }
                // Now just select all facets that passed
                for (size_t next_facet : facets_to_select)
                    m_selected_facets[mesh_id][next_facet] = select;
            }
        }

        if (some_mesh_was_hit)
        {
            m_wait_for_up_event = true;
            m_parent.set_as_dirty();
            return true;
        }
        if (action == SLAGizmoEventType::Dragging && m_wait_for_up_event)
            return true;
    }

    if (action == SLAGizmoEventType::LeftUp && m_wait_for_up_event) {
        m_wait_for_up_event = false;
        return true;
    }

    return false;
}



void GLGizmoFdmSupports::on_render_input_window(float x, float y, float bottom_limit)
{
    if (! m_c->selection_info()->model_object())
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

     ImGui::SliderFloat(" ", &m_cursor_radius, 0.f, 8.f, "%.2f");

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
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("FDM gizmo turned on")));
    }
    if (m_state == Off && m_old_state != Off) { // the gizmo was just turned Off
        // we are actually shutting down
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _(L("FDM gizmo turned off")));
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



} // namespace GUI
} // namespace Slic3r
