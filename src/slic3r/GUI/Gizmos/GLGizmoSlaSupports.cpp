// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoSlaSupports.hpp"

#include <GL/glew.h>

#include <wx/msgdlg.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectSettings.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/PresetBundle.hpp"
#include "libslic3r/Tesselate.hpp"


namespace Slic3r {
namespace GUI {

#if ENABLE_SVG_ICONS
GLGizmoSlaSupports::GLGizmoSlaSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
#else
GLGizmoSlaSupports::GLGizmoSlaSupports(GLCanvas3D& parent, unsigned int sprite_id)
    : GLGizmoBase(parent, sprite_id)
#endif // ENABLE_SVG_ICONS
    , m_quadric(nullptr)
{
    m_quadric = ::gluNewQuadric();
    if (m_quadric != nullptr)
        // using GLU_FILL does not work when the instance's transformation
        // contains mirroring (normals are reverted)
        ::gluQuadricDrawStyle(m_quadric, GLU_FILL);
}

GLGizmoSlaSupports::~GLGizmoSlaSupports()
{
    if (m_quadric != nullptr)
        ::gluDeleteQuadric(m_quadric);
}

bool GLGizmoSlaSupports::on_init()
{
    m_shortcut_key = WXK_CONTROL_L;
    return true;
}

void GLGizmoSlaSupports::set_sla_support_data(ModelObject* model_object, const Selection& selection)
{
    if (selection.is_empty()) {
        m_model_object = nullptr;
        m_old_model_object = nullptr;
        return;
    }

    m_old_model_object = m_model_object;
    m_model_object = model_object;
    m_active_instance = selection.get_instance_idx();

    if (model_object && selection.is_from_single_instance())
    {
        // Cache the bb - it's needed for dealing with the clipping plane quite often
        // It could be done inside update_mesh but one has to account for scaling of the instance.
        m_active_instance_bb = m_model_object->instance_bounding_box(m_active_instance);

        if (is_mesh_update_necessary()) {
            update_mesh();
            editing_mode_reload_cache();
        }

        if (m_model_object != m_old_model_object)
            m_editing_mode = false;

        if (m_editing_mode_cache.empty() && m_model_object->sla_points_status != sla::PointsStatus::UserModified)
            get_data_from_backend();

        if (m_state == On) {
            m_parent.toggle_model_objects_visibility(false);
            m_parent.toggle_model_objects_visibility(true, m_model_object, m_active_instance);
        }
    }
}

void GLGizmoSlaSupports::on_render(const Selection& selection) const
{
    // If current m_model_object does not match selection, ask GLCanvas3D to turn us off
    if (m_state == On
     && (m_model_object != selection.get_model()->objects[selection.get_object_idx()]
      || m_active_instance != selection.get_instance_idx())) {
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_RESETGIZMOS));
        return;
    }

    ::glEnable(GL_BLEND);
    ::glEnable(GL_DEPTH_TEST);

    // we'll recover current look direction from the modelview matrix (in world coords):
    Eigen::Matrix<double, 4, 4, Eigen::DontAlign> modelview_matrix;
    ::glGetDoublev(GL_MODELVIEW_MATRIX, modelview_matrix.data());
    Vec3d direction_to_camera(modelview_matrix.data()[2], modelview_matrix.data()[6], modelview_matrix.data()[10]);

    if (m_quadric != nullptr && selection.is_from_single_instance())
        render_points(selection, direction_to_camera, false);

    render_selection_rectangle();
    render_clipping_plane(selection, direction_to_camera);

    ::glDisable(GL_BLEND);
}



void GLGizmoSlaSupports::render_clipping_plane(const Selection& selection, const Vec3d& direction_to_camera) const
{
    if (m_clipping_plane_distance == 0.f)
        return;

    const GLVolume* vol = selection.get_volume(*selection.get_volume_idxs().begin());
    double z_shift = vol->get_sla_shift_z();
    Transform3f instance_matrix = vol->get_instance_transformation().get_matrix().cast<float>();
    Transform3f instance_matrix_no_translation_no_scaling = vol->get_instance_transformation().get_matrix(true,false,true).cast<float>();
    Transform3f instance_matrix_no_translation = vol->get_instance_transformation().get_matrix(true).cast<float>();
    Vec3f scaling = vol->get_instance_scaling_factor().cast<float>();

    Vec3f up = instance_matrix_no_translation_no_scaling.inverse() * direction_to_camera.cast<float>().normalized();
    up = Vec3f(up(0)*scaling(0), up(1)*scaling(1), up(2)*scaling(2));
    float height      = m_active_instance_bb.radius() - m_clipping_plane_distance * 2*m_active_instance_bb.radius();
    float height_mesh = height;

    if (m_clipping_plane_distance != m_old_clipping_plane_distance
     || m_old_direction_to_camera != direction_to_camera) {

        std::vector<ExPolygons> list_of_expolys;
        m_tms->set_up_direction(up);
        m_tms->slice(std::vector<float>{height_mesh}, 0.f, &list_of_expolys, [](){});
        m_triangles = triangulate_expolygons_2f(list_of_expolys[0]);

        m_old_direction_to_camera = direction_to_camera;
        m_old_clipping_plane_distance = m_clipping_plane_distance;
    }

    ::glPushMatrix();
    ::glTranslated(0.0, 0.0, z_shift);
    ::glMultMatrixf(instance_matrix.data());
    Eigen::Quaternionf q;
    q.setFromTwoVectors(Vec3f::UnitZ(), up);
    Eigen::AngleAxisf aa(q);
    ::glRotatef(aa.angle() * (180./M_PI), aa.axis()(0), aa.axis()(1), aa.axis()(2));
    ::glTranslatef(0.f, 0.f, -0.001f); // to make sure the cut is safely beyond the near clipping plane

    ::glBegin(GL_TRIANGLES);
    ::glColor3f(1.0f, 0.37f, 0.0f);
    for (const Vec2f& point : m_triangles)
        ::glVertex3f(point(0), point(1), height_mesh);
    ::glEnd();

    ::glPopMatrix();
}



void GLGizmoSlaSupports::render_selection_rectangle() const
{
    if (!m_selection_rectangle_active)
        return;

    ::glLineWidth(1.5f);
    float render_color[3] = {1.f, 0.f, 0.f};
    ::glColor3fv(render_color);

    ::glPushAttrib(GL_TRANSFORM_BIT);   // remember current MatrixMode

    ::glMatrixMode(GL_MODELVIEW);       // cache modelview matrix and set to identity
    ::glPushMatrix();
    ::glLoadIdentity();

    ::glMatrixMode(GL_PROJECTION);      // cache projection matrix and set to identity
    ::glPushMatrix();
    ::glLoadIdentity();

    ::glOrtho(0.f, m_canvas_width, m_canvas_height, 0.f, -1.f, 1.f); // set projection matrix so that world coords = window coords

    // render the selection  rectangle (window coordinates):
    ::glPushAttrib(GL_ENABLE_BIT);
    ::glLineStipple(4, 0xAAAA);
    ::glEnable(GL_LINE_STIPPLE);

    ::glBegin(GL_LINE_LOOP);
    ::glVertex3f((GLfloat)m_selection_rectangle_start_corner(0), (GLfloat)m_selection_rectangle_start_corner(1), (GLfloat)0.5f);
    ::glVertex3f((GLfloat)m_selection_rectangle_end_corner(0), (GLfloat)m_selection_rectangle_start_corner(1), (GLfloat)0.5f);
    ::glVertex3f((GLfloat)m_selection_rectangle_end_corner(0), (GLfloat)m_selection_rectangle_end_corner(1), (GLfloat)0.5f);
    ::glVertex3f((GLfloat)m_selection_rectangle_start_corner(0), (GLfloat)m_selection_rectangle_end_corner(1), (GLfloat)0.5f);
    ::glEnd();
    ::glPopAttrib();

    ::glPopMatrix();                // restore former projection matrix
    ::glMatrixMode(GL_MODELVIEW);
    ::glPopMatrix();                // restore former modelview matrix
    ::glPopAttrib();                // restore former MatrixMode
}

void GLGizmoSlaSupports::on_render_for_picking(const Selection& selection) const
{
    ::glEnable(GL_DEPTH_TEST);

    // we'll recover current look direction from the modelview matrix (in world coords):
    Eigen::Matrix<double, 4, 4, Eigen::DontAlign> modelview_matrix;
    ::glGetDoublev(GL_MODELVIEW_MATRIX, modelview_matrix.data());
    Vec3d direction_to_camera(modelview_matrix.data()[2], modelview_matrix.data()[6], modelview_matrix.data()[10]);

    render_points(selection, direction_to_camera, true);
}

void GLGizmoSlaSupports::render_points(const Selection& selection, const Vec3d& direction_to_camera, bool picking) const
{
    if (!picking)
        ::glEnable(GL_LIGHTING);

    const GLVolume* vol = selection.get_volume(*selection.get_volume_idxs().begin());
    double z_shift = vol->get_sla_shift_z();
    const Transform3d& instance_scaling_matrix_inverse = vol->get_instance_transformation().get_matrix(true, true, false, true).inverse();
    const Transform3d& instance_matrix = vol->get_instance_transformation().get_matrix();

    ::glPushMatrix();
    ::glTranslated(0.0, 0.0, z_shift);
    ::glMultMatrixd(instance_matrix.data());

    float render_color[3];
    for (int i = 0; i < (int)m_editing_mode_cache.size(); ++i)
    {
        const sla::SupportPoint& support_point = m_editing_mode_cache[i].support_point;
        const bool& point_selected = m_editing_mode_cache[i].selected;

        if (is_point_clipped(support_point.pos.cast<double>(), direction_to_camera, z_shift))
            continue;

        // First decide about the color of the point.
        if (picking) {
            std::array<float, 3> color = picking_color_component(i);
            render_color[0] = color[0];
            render_color[1] = color[1];
            render_color[2] = color[2];
        }
        else {
            if ((m_hover_id == i && m_editing_mode)) { // ignore hover state unless editing mode is active
                render_color[0] = 0.f;
                render_color[1] = 1.0f;
                render_color[2] = 1.0f;
            }
            else { // neigher hover nor picking
                bool supports_new_island = m_lock_unique_islands && m_editing_mode_cache[i].support_point.is_new_island;
                if (m_editing_mode) {
                    render_color[0] = point_selected ? 1.0f : (supports_new_island ? 0.3f : 0.7f);
                    render_color[1] = point_selected ? 0.3f : (supports_new_island ? 0.3f : 0.7f);
                    render_color[2] = point_selected ? 0.3f : (supports_new_island ? 1.0f : 0.7f);
                }
                else
                    for (unsigned char i=0; i<3; ++i) render_color[i] = 0.5f;
            }
        }
        ::glColor3fv(render_color);
        float render_color_emissive[4] = { 0.5f * render_color[0], 0.5f * render_color[1], 0.5f * render_color[2], 1.f};
        ::glMaterialfv(GL_FRONT, GL_EMISSION, render_color_emissive);

        // Inverse matrix of the instance scaling is applied so that the mark does not scale with the object.
        ::glPushMatrix();
        ::glTranslated(support_point.pos(0), support_point.pos(1), support_point.pos(2));
        ::glMultMatrixd(instance_scaling_matrix_inverse.data());

        // Matrices set, we can render the point mark now.
        // If in editing mode, we'll also render a cone pointing to the sphere.
        if (m_editing_mode) {
            if (m_editing_mode_cache[i].normal == Vec3f::Zero())
                update_cache_entry_normal(i); // in case the normal is not yet cached, find and cache it

            Eigen::Quaterniond q;
            q.setFromTwoVectors(Vec3d{0., 0., 1.}, instance_scaling_matrix_inverse * m_editing_mode_cache[i].normal.cast<double>());
            Eigen::AngleAxisd aa(q);
            ::glRotated(aa.angle() * (180./M_PI), aa.axis()(0), aa.axis()(1), aa.axis()(2));

            const float cone_radius = 0.25f; // mm
            const float cone_height = 0.75f;
            ::glPushMatrix();
            ::glTranslatef(0.f, 0.f, m_editing_mode_cache[i].support_point.head_front_radius * RenderPointScale);
            ::gluCylinder(m_quadric, 0.f, cone_radius, cone_height, 24, 1);
            ::glTranslatef(0.f, 0.f, cone_height);
            ::gluDisk(m_quadric, 0.0, cone_radius, 24, 1);
            ::glPopMatrix();
        }
        ::gluSphere(m_quadric, m_editing_mode_cache[i].support_point.head_front_radius * RenderPointScale, 24, 12);
        ::glPopMatrix();
    }

    {
        // Reset emissive component to zero (the default value)
        float render_color_emissive[4] = { 0.f, 0.f, 0.f, 1.f };
        ::glMaterialfv(GL_FRONT, GL_EMISSION, render_color_emissive);
    }

    if (!picking)
        ::glDisable(GL_LIGHTING);

    ::glPopMatrix();
}



bool GLGizmoSlaSupports::is_point_clipped(const Vec3d& point, const Vec3d& direction_to_camera, float z_shift) const
{
    if (m_clipping_plane_distance == 0.f)
        return false;

    Vec3d transformed_point = m_model_object->instances.front()->get_transformation().get_matrix() * point;
    transformed_point(2) += z_shift;
    return direction_to_camera.dot(m_active_instance_bb.center()) + m_active_instance_bb.radius()
            - m_clipping_plane_distance * 2*m_active_instance_bb.radius() < direction_to_camera.dot(transformed_point);
}



bool GLGizmoSlaSupports::is_mesh_update_necessary() const
{
    return ((m_state == On) && (m_model_object != nullptr) && !m_model_object->instances.empty())
        && ((m_model_object != m_old_model_object) || m_V.size()==0);
}

void GLGizmoSlaSupports::update_mesh()
{
    wxBusyCursor wait;
    Eigen::MatrixXf& V = m_V;
    Eigen::MatrixXi& F = m_F;
    // This mesh does not account for the possible Z up SLA offset.
    m_mesh = m_model_object->raw_mesh();
    const stl_file& stl = m_mesh.stl;
    V.resize(3 * stl.stats.number_of_facets, 3);
    F.resize(stl.stats.number_of_facets, 3);
    for (unsigned int i=0; i<stl.stats.number_of_facets; ++i) {
        const stl_facet* facet = stl.facet_start+i;
        V(3*i+0, 0) = facet->vertex[0](0); V(3*i+0, 1) = facet->vertex[0](1); V(3*i+0, 2) = facet->vertex[0](2);
        V(3*i+1, 0) = facet->vertex[1](0); V(3*i+1, 1) = facet->vertex[1](1); V(3*i+1, 2) = facet->vertex[1](2);
        V(3*i+2, 0) = facet->vertex[2](0); V(3*i+2, 1) = facet->vertex[2](1); V(3*i+2, 2) = facet->vertex[2](2);
        F(i, 0) = 3*i+0;
        F(i, 1) = 3*i+1;
        F(i, 2) = 3*i+2;
    }

    m_AABB = igl::AABB<Eigen::MatrixXf,3>();
    m_AABB.init(m_V, m_F);

    m_tms.reset(new TriangleMeshSlicer);
    m_tms->init(&m_mesh, [](){});
}

// Unprojects the mouse position on the mesh and return the hit point and normal of the facet.
// The function throws if no intersection if found.
std::pair<Vec3f, Vec3f> GLGizmoSlaSupports::unproject_on_mesh(const Vec2d& mouse_pos)
{
    // if the gizmo doesn't have the V, F structures for igl, calculate them first:
    if (m_V.size() == 0)
        update_mesh();

    Eigen::Matrix<GLint, 4, 1, Eigen::DontAlign> viewport;
    ::glGetIntegerv(GL_VIEWPORT, viewport.data());
    Eigen::Matrix<GLdouble, 4, 4, Eigen::DontAlign> modelview_matrix;
    ::glGetDoublev(GL_MODELVIEW_MATRIX, modelview_matrix.data());
    Eigen::Matrix<GLdouble, 4, 4, Eigen::DontAlign> projection_matrix;
    ::glGetDoublev(GL_PROJECTION_MATRIX, projection_matrix.data());

    Vec3d point1;
    Vec3d point2;
    ::gluUnProject(mouse_pos(0), viewport(3)-mouse_pos(1), 0.f, modelview_matrix.data(), projection_matrix.data(), viewport.data(), &point1(0), &point1(1), &point1(2));
    ::gluUnProject(mouse_pos(0), viewport(3)-mouse_pos(1), 1.f, modelview_matrix.data(), projection_matrix.data(), viewport.data(), &point2(0), &point2(1), &point2(2));

    std::vector<igl::Hit> hits;

    const Selection& selection = m_parent.get_selection();
    const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
    double z_offset = volume->get_sla_shift_z();

    // we'll recover current look direction from the modelview matrix (in world coords):
    Vec3d direction_to_camera(modelview_matrix.data()[2], modelview_matrix.data()[6], modelview_matrix.data()[10]);

    point1(2) -= z_offset;
	point2(2) -= z_offset;

    Transform3d inv = volume->get_instance_transformation().get_matrix().inverse();

    point1 = inv * point1;
    point2 = inv * point2;

    if (!m_AABB.intersect_ray(m_V, m_F, point1.cast<float>(), (point2-point1).cast<float>(), hits))
        throw std::invalid_argument("unproject_on_mesh(): No intersection found.");

    std::sort(hits.begin(), hits.end(), [](const igl::Hit& a, const igl::Hit& b) { return a.t < b.t; });

    // Now let's iterate through the points and find the first that is not clipped:
    unsigned int i=0;
    Vec3f bc;
    Vec3f a;
    Vec3f b;
    Vec3f result;
    for (i=0; i<hits.size(); ++i) {
        igl::Hit& hit = hits[i];
        int fid = hit.id;   // facet id
        bc = Vec3f(1-hit.u-hit.v, hit.u, hit.v); // barycentric coordinates of the hit
        a = (m_V.row(m_F(fid, 1)) - m_V.row(m_F(fid, 0)));
        b = (m_V.row(m_F(fid, 2)) - m_V.row(m_F(fid, 0)));
        result = bc(0) * m_V.row(m_F(fid, 0)) + bc(1) * m_V.row(m_F(fid, 1)) + bc(2)*m_V.row(m_F(fid, 2));
        if (m_clipping_plane_distance == 0.f || !is_point_clipped(result.cast<double>(), direction_to_camera, z_offset))
            break;
    }

    if (i==hits.size() || (hits.size()-i) % 2 != 0) {
        // All hits are either clipped, or there is an odd number of unclipped
        // hits - meaning the nearest must be from inside the mesh.
        throw std::invalid_argument("unproject_on_mesh(): No intersection found.");
    }

    // Calculate and return both the point and the facet normal.
    return std::make_pair(
            result,
            a.cross(b)
        );
}

// Following function is called from GLCanvas3D to inform the gizmo about a mouse/keyboard event.
// The gizmo has an opportunity to react - if it does, it should return true so that the Canvas3D is
// aware that the event was reacted to and stops trying to make different sense of it. If the gizmo
// concludes that the event was not intended for it, it should return false.
bool GLGizmoSlaSupports::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down)
{
    if (m_editing_mode) {

        // left down - show the selection rectangle:
        if (action == SLAGizmoEventType::LeftDown && shift_down) {
            if (m_hover_id == -1) {
                m_selection_rectangle_active = true;
                m_selection_rectangle_start_corner = mouse_position;
                m_selection_rectangle_end_corner = mouse_position;
                m_canvas_width = m_parent.get_canvas_size().get_width();
                m_canvas_height = m_parent.get_canvas_size().get_height();
            }
            else {
                if (m_editing_mode_cache[m_hover_id].selected)
                    unselect_point(m_hover_id);
                else
                    select_point(m_hover_id);
            }

            return true;
        }

        // left down without selection rectangle - place point on the mesh:
        if (action == SLAGizmoEventType::LeftDown && !m_selection_rectangle_active && !shift_down) {
            // If any point is in hover state, this should initiate its move - return control back to GLCanvas:
            if (m_hover_id != -1)
                return false;

            // If there is some selection, don't add new point and deselect everything instead.
            if (m_selection_empty) {
                try {
                    std::pair<Vec3f, Vec3f> pos_and_normal = unproject_on_mesh(mouse_position); // don't create anything if this throws
                    m_editing_mode_cache.emplace_back(sla::SupportPoint(pos_and_normal.first, m_new_point_head_diameter/2.f, false), false, pos_and_normal.second);
                    m_unsaved_changes = true;
                    m_parent.set_as_dirty();
                    m_wait_for_up_event = true;
                }
                catch (...) {   // not clicked on object
                    return false;
                }
            }
            else
                select_point(NoPoints);

            return true;
        }

        // left up with selection rectangle - select points inside the rectangle:
        if ((action == SLAGizmoEventType::LeftUp || action == SLAGizmoEventType::ShiftUp) && m_selection_rectangle_active) {
            const Transform3d& instance_matrix = m_model_object->instances[m_active_instance]->get_transformation().get_matrix();
            GLint viewport[4];
            ::glGetIntegerv(GL_VIEWPORT, viewport);
            GLdouble modelview_matrix[16];
            ::glGetDoublev(GL_MODELVIEW_MATRIX, modelview_matrix);
            GLdouble projection_matrix[16];
            ::glGetDoublev(GL_PROJECTION_MATRIX, projection_matrix);

            const Selection& selection = m_parent.get_selection();
            const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
            double z_offset = volume->get_sla_shift_z();

            // bounding box created from the rectangle corners - will take care of order of the corners
            BoundingBox rectangle(Points{Point(m_selection_rectangle_start_corner.cast<int>()), Point(m_selection_rectangle_end_corner.cast<int>())});

            const Transform3d& instance_matrix_no_translation = volume->get_instance_transformation().get_matrix(true);
            // we'll recover current look direction from the modelview matrix (in world coords)...
            Vec3f direction_to_camera(modelview_matrix[2], modelview_matrix[6], modelview_matrix[10]);
            // ...and transform it to model coords.
            direction_to_camera = (instance_matrix_no_translation.inverse().cast<float>() * direction_to_camera).normalized().eval();

            // Iterate over all points, check if they're in the rectangle and if so, check that they are not obscured by the mesh:
            for (unsigned int i=0; i<m_editing_mode_cache.size(); ++i) {
                const sla::SupportPoint &support_point = m_editing_mode_cache[i].support_point;
                Vec3f pos = instance_matrix.cast<float>() * support_point.pos;
                pos(2) += z_offset;
                  GLdouble out_x, out_y, out_z;
                 ::gluProject((GLdouble)pos(0), (GLdouble)pos(1), (GLdouble)pos(2), modelview_matrix, projection_matrix, viewport, &out_x, &out_y, &out_z);
                 out_y = m_canvas_height - out_y;

                if (rectangle.contains(Point(out_x, out_y))) {
                    bool is_obscured = false;
                    // Cast a ray in the direction of the camera and look for intersection with the mesh:
                    std::vector<igl::Hit> hits;
                    // Offset the start of the ray to the front of the ball + EPSILON to account for numerical inaccuracies.
                    if (m_AABB.intersect_ray(m_V, m_F, support_point.pos + direction_to_camera * (support_point.head_front_radius + EPSILON), direction_to_camera, hits))
                        // FIXME: the intersection could in theory be behind the camera, but as of now we only have camera direction.
                        // Also, the threshold is in mesh coordinates, not in actual dimensions.
                        if (hits.size() > 1 || hits.front().t > 0.001f)
                            is_obscured = true;

                    if (!is_obscured)
                        select_point(i);
                }
            }
            m_selection_rectangle_active = false;
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

            if (m_selection_rectangle_active)  {
                m_selection_rectangle_end_corner = mouse_position;
                return true;
            }

            return false;
        }

        if (action == SLAGizmoEventType::Delete) {
            // delete key pressed
            delete_selected_points();
            return true;
        }

        if (action ==  SLAGizmoEventType::ApplyChanges) {
            editing_mode_apply_changes();
            return true;
        }

        if (action ==  SLAGizmoEventType::DiscardChanges) {
            editing_mode_discard_changes();
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

    if (!m_editing_mode) {
        if (action == SLAGizmoEventType::AutomaticGeneration) {
            auto_generate();
            return true;
        }

        if (action == SLAGizmoEventType::ManualEditing) {
            switch_to_editing_mode();
            return true;
        }
    }

    return false;
}

void GLGizmoSlaSupports::delete_selected_points(bool force)
{
    for (unsigned int idx=0; idx<m_editing_mode_cache.size(); ++idx) {
        if (m_editing_mode_cache[idx].selected && (!m_editing_mode_cache[idx].support_point.is_new_island || !m_lock_unique_islands || force)) {
            m_editing_mode_cache.erase(m_editing_mode_cache.begin() + (idx--));
            m_unsaved_changes = true;
        }
            // This should trigger the support generation
            // wxGetApp().plater()->reslice_SLA_supports(*m_model_object);
    }

    select_point(NoPoints);

    //m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
}

void GLGizmoSlaSupports::on_update(const UpdateData& data, const Selection& selection)
{
    if (m_editing_mode && m_hover_id != -1 && data.mouse_pos && (!m_editing_mode_cache[m_hover_id].support_point.is_new_island || !m_lock_unique_islands)) {
        std::pair<Vec3f, Vec3f> pos_and_normal;
        try {
            pos_and_normal = unproject_on_mesh(Vec2d((*data.mouse_pos)(0), (*data.mouse_pos)(1)));
        }
        catch (...) { return; }
        m_editing_mode_cache[m_hover_id].support_point.pos = pos_and_normal.first;
        m_editing_mode_cache[m_hover_id].support_point.is_new_island = false;
        m_editing_mode_cache[m_hover_id].normal = pos_and_normal.second;
        m_unsaved_changes = true;
        // Do not update immediately, wait until the mouse is released.
        // m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    }
}

std::vector<const ConfigOption*> GLGizmoSlaSupports::get_config_options(const std::vector<std::string>& keys) const
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


void GLGizmoSlaSupports::update_cache_entry_normal(unsigned int i) const
{
    int idx = 0;
    Eigen::Matrix<float, 1, 3> pp = m_editing_mode_cache[i].support_point.pos;
    Eigen::Matrix<float, 1, 3> cc;
    m_AABB.squared_distance(m_V, m_F, pp, idx, cc);
    Vec3f a = (m_V.row(m_F(idx, 1)) - m_V.row(m_F(idx, 0)));
    Vec3f b = (m_V.row(m_F(idx, 2)) - m_V.row(m_F(idx, 0)));
    m_editing_mode_cache[i].normal = a.cross(b);
}




GLCanvas3D::ClippingPlane GLGizmoSlaSupports::get_sla_clipping_plane() const
{
    if (!m_model_object)
        return GLCanvas3D::ClippingPlane::ClipsNothing();

    Eigen::Matrix<GLdouble, 4, 4, Eigen::DontAlign> modelview_matrix;
    ::glGetDoublev(GL_MODELVIEW_MATRIX, modelview_matrix.data());

    // we'll recover current look direction from the modelview matrix (in world coords):
    Vec3d direction_to_camera(modelview_matrix.data()[2], modelview_matrix.data()[6], modelview_matrix.data()[10]);
    float dist = direction_to_camera.dot(m_active_instance_bb.center());

    return GLCanvas3D::ClippingPlane(-direction_to_camera.normalized(),(dist - (-m_active_instance_bb.radius()) - m_clipping_plane_distance * 2*m_active_instance_bb.radius()));
}


/*
void GLGizmoSlaSupports::find_intersecting_facets(const igl::AABB<Eigen::MatrixXf, 3>* aabb, const Vec3f& normal, double offset, std::vector<unsigned int>& idxs) const
{
    if (aabb->is_leaf()) { // this is a facet
        // corner.dot(normal) - offset
        idxs.push_back(aabb->m_primitive);
    }
    else { // not a leaf
    using CornerType = Eigen::AlignedBox<float, 3>::CornerType;
        bool sign = std::signbit(offset - normal.dot(aabb->m_box.corner(CornerType(0))));
        for (unsigned int i=1; i<8; ++i)
            if (std::signbit(offset - normal.dot(aabb->m_box.corner(CornerType(i)))) != sign) {
                find_intersecting_facets(aabb->m_left, normal, offset, idxs);
                find_intersecting_facets(aabb->m_right, normal, offset, idxs);
            }
    }
}



void GLGizmoSlaSupports::make_line_segments() const
{
    TriangleMeshSlicer tms(&m_model_object->volumes.front()->mesh);
    Vec3f normal(0.f, 1.f, 1.f);
    double d = 0.;

    std::vector<IntersectionLine> lines;
    find_intersections(&m_AABB, normal, d, lines);
    ExPolygons expolys;
    tms.make_expolygons_simple(lines, &expolys);

    SVG svg("slice_loops.svg", get_extents(expolys));
    svg.draw(expolys);
    //for (const IntersectionLine &l : lines[i])
    //    svg.draw(l, "red", 0);
    //svg.draw_outline(expolygons, "black", "blue", 0);
    svg.Close();
}
*/


void GLGizmoSlaSupports::on_render_input_window(float x, float y, float bottom_limit, const Selection& selection)
{
    if (!m_model_object)
        return;

    bool first_run = true; // This is a hack to redraw the button when all points are removed,
                           // so it is not delayed until the background process finishes.
RENDER_AGAIN:
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);

    const ImVec2 window_size(m_imgui->scaled(17.f, 18.f));
    ImGui::SetNextWindowPos(ImVec2(x, y - std::max(0.f, y+window_size.y-bottom_limit) ));
    ImGui::SetNextWindowSize(ImVec2(window_size));

    m_imgui->set_next_window_bg_alpha(0.5f);
    m_imgui->begin(on_get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::PushItemWidth(100.0f);

    bool force_refresh = false;
    bool remove_selected = false;
    bool remove_all = false;

    if (m_editing_mode) {
        m_imgui->text(_(L("Left mouse click - add point")));
        m_imgui->text(_(L("Right mouse click - remove point")));
        m_imgui->text(_(L("Shift + Left (+ drag) - select point(s)")));
        m_imgui->text(" ");  // vertical gap

        float diameter_upper_cap = static_cast<ConfigOptionFloat*>(wxGetApp().preset_bundle->sla_prints.get_edited_preset().config.option("support_pillar_diameter"))->value;
        if (m_new_point_head_diameter > diameter_upper_cap)
            m_new_point_head_diameter = diameter_upper_cap;

        m_imgui->text(_(L("Head diameter: ")));
        ImGui::SameLine();
        if (ImGui::SliderFloat("", &m_new_point_head_diameter, 0.1f, diameter_upper_cap, "%.1f")) {
            // value was changed
            for (auto& cache_entry : m_editing_mode_cache)
                if (cache_entry.selected) {
                    cache_entry.support_point.head_front_radius = m_new_point_head_diameter / 2.f;
                    m_unsaved_changes = true;
                }
        }

        bool changed = m_lock_unique_islands;
        m_imgui->checkbox(_(L("Lock supports under new islands")), m_lock_unique_islands);
        force_refresh |= changed != m_lock_unique_islands;

        m_imgui->disabled_begin(m_selection_empty);
        remove_selected = m_imgui->button(_(L("Remove selected points")));
        m_imgui->disabled_end();

        m_imgui->disabled_begin(m_editing_mode_cache.empty());
        remove_all = m_imgui->button(_(L("Remove all points")));
        m_imgui->disabled_end();

        m_imgui->text(" "); // vertical gap

        if (m_imgui->button(_(L("Apply changes")))) {
            editing_mode_apply_changes();
            force_refresh = true;
        }
        ImGui::SameLine();
        bool discard_changes = m_imgui->button(_(L("Discard changes")));
        if (discard_changes) {
            editing_mode_discard_changes();
            force_refresh = true;
        }
    }
    else { // not in editing mode:
        ImGui::PushItemWidth(100.0f);
        m_imgui->text(_(L("Minimal points distance: ")));
        ImGui::SameLine();

        std::vector<const ConfigOption*> opts = get_config_options({"support_points_density_relative", "support_points_minimal_distance"});
        float density = static_cast<const ConfigOptionInt*>(opts[0])->value;
        float minimal_point_distance = static_cast<const ConfigOptionFloat*>(opts[1])->value;

        bool value_changed = ImGui::SliderFloat("", &minimal_point_distance, 0.f, 20.f, "%.f mm");
        if (value_changed)
            m_model_object->config.opt<ConfigOptionFloat>("support_points_minimal_distance", true)->value = minimal_point_distance;

        m_imgui->text(_(L("Support points density: ")));
        ImGui::SameLine();
        if (ImGui::SliderFloat(" ", &density, 0.f, 200.f, "%.f %%")) {
            value_changed = true;
            m_model_object->config.opt<ConfigOptionInt>("support_points_density_relative", true)->value = (int)density;
        }

        if (value_changed) { // Update side panel
            wxTheApp->CallAfter([]() {
                wxGetApp().obj_settings()->UpdateAndShow(true);
                wxGetApp().obj_list()->update_settings_items();
            });
        }

        bool generate = m_imgui->button(_(L("Auto-generate points [A]")));

        if (generate)
            auto_generate();

        m_imgui->text("");
        if (m_imgui->button(_(L("Manual editing [M]"))))
            switch_to_editing_mode();

        m_imgui->disabled_begin(m_editing_mode_cache.empty());
        remove_all = m_imgui->button(_(L("Remove all points")));
        m_imgui->disabled_end();

        m_imgui->text("");

        m_imgui->text(m_model_object->sla_points_status == sla::PointsStatus::None ? "No points  (will be autogenerated)" :
                     (m_model_object->sla_points_status == sla::PointsStatus::AutoGenerated ? "Autogenerated points (no modifications)" :
                     (m_model_object->sla_points_status == sla::PointsStatus::UserModified ? "User-modified points" :
                     (m_model_object->sla_points_status == sla::PointsStatus::Generating ? "Generation in progress..." : "UNKNOWN STATUS"))));
    }


    // Following is rendered in both editing and non-editing mode:
    m_imgui->text("Clipping of view: ");
    ImGui::SameLine();
    ImGui::PushItemWidth(150.0f);
    bool value_changed = ImGui::SliderFloat("  ", &m_clipping_plane_distance, 0.f, 1.f, "%.2f");
    
    m_imgui->end();

    if (m_editing_mode != m_old_editing_state) { // user toggled between editing/non-editing mode
        m_parent.toggle_sla_auxiliaries_visibility(!m_editing_mode, m_model_object, m_active_instance);
        force_refresh = true;
    }
    m_old_editing_state = m_editing_mode;

    if (remove_selected || remove_all) {
        force_refresh = false;
        m_parent.set_as_dirty();
        if (remove_all)
            select_point(AllPoints);
        delete_selected_points(remove_all);
        if (remove_all && !m_editing_mode)
            editing_mode_apply_changes();
        if (first_run) {
            first_run = false;
            goto RENDER_AGAIN;
        }
    }

    if (force_refresh)
        m_parent.set_as_dirty();
}

bool GLGizmoSlaSupports::on_is_activable(const Selection& selection) const
{
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

bool GLGizmoSlaSupports::on_is_selectable() const
{
    return (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptSLA);
}

std::string GLGizmoSlaSupports::on_get_name() const
{
    return L("SLA Support Points [L]");
}

void GLGizmoSlaSupports::on_set_state()
{
    if (m_state == On && m_old_state != On) { // the gizmo was just turned on

        if (is_mesh_update_necessary())
            update_mesh();

        // we'll now reload support points:
        if (m_model_object)
            editing_mode_reload_cache();

        m_parent.toggle_model_objects_visibility(false);
        if (m_model_object)
            m_parent.toggle_model_objects_visibility(true, m_model_object, m_active_instance);

        // Set default head diameter from config.
        const DynamicPrintConfig& cfg = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
        m_new_point_head_diameter = static_cast<const ConfigOptionFloat*>(cfg.option("support_head_front_diameter"))->value;
    }
    if (m_state == Off && m_old_state != Off) { // the gizmo was just turned Off
        if (m_model_object) {
            if (m_unsaved_changes) {
                wxMessageDialog dlg(GUI::wxGetApp().plater(), _(L("Do you want to save your manually edited support points ?\n")),
                                    _(L("Save changes?")), wxICON_QUESTION | wxYES | wxNO);
                if (dlg.ShowModal() == wxID_YES)
                    editing_mode_apply_changes();
                else
                    editing_mode_discard_changes();
            }
        }

        m_parent.toggle_model_objects_visibility(true);
        m_editing_mode = false; // so it is not active next time the gizmo opens
        m_editing_mode_cache.clear();
        m_clipping_plane_distance = 0.f;
    }
    m_old_state = m_state;
}



void GLGizmoSlaSupports::on_start_dragging(const Selection& selection)
{
    if (m_hover_id != -1) {
        select_point(NoPoints);
        select_point(m_hover_id);
    }
}



void GLGizmoSlaSupports::select_point(int i)
{
    if (i == AllPoints || i == NoPoints) {
        for (auto& point_and_selection : m_editing_mode_cache)
            point_and_selection.selected = ( i == AllPoints );
        m_selection_empty = (i == NoPoints);

        if (i == AllPoints)
            m_new_point_head_diameter = m_editing_mode_cache[0].support_point.head_front_radius * 2.f;
    }
    else {
        m_editing_mode_cache[i].selected = true;
        m_selection_empty = false;
        m_new_point_head_diameter = m_editing_mode_cache[i].support_point.head_front_radius * 2.f;
    }
}


void GLGizmoSlaSupports::unselect_point(int i)
{
    m_editing_mode_cache[i].selected = false;
    m_selection_empty = true;
    for (const CacheEntry& ce : m_editing_mode_cache) {
        if (ce.selected) {
            m_selection_empty = false;
            break;
        }
    }
}



void GLGizmoSlaSupports::editing_mode_discard_changes()
{
    m_editing_mode_cache.clear();
    for (const sla::SupportPoint& point : m_model_object->sla_support_points)
        m_editing_mode_cache.emplace_back(point, false);
    m_editing_mode = false;
    m_unsaved_changes = false;
}



void GLGizmoSlaSupports::editing_mode_apply_changes()
{
    // If there are no changes, don't touch the front-end. The data in the cache could have been
    // taken from the backend and copying them to ModelObject would needlessly invalidate them.
    if (m_unsaved_changes) {
        m_model_object->sla_points_status = sla::PointsStatus::UserModified;
        m_model_object->sla_support_points.clear();
        for (const CacheEntry& cache_entry : m_editing_mode_cache)
            m_model_object->sla_support_points.push_back(cache_entry.support_point);

        // Recalculate support structures once the editing mode is left.
        // m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
        // m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
        wxGetApp().CallAfter([this]() { wxGetApp().plater()->reslice_SLA_supports(*m_model_object); });
    }
    m_editing_mode = false;
    m_unsaved_changes = false;
}



void GLGizmoSlaSupports::editing_mode_reload_cache()
{
    m_editing_mode_cache.clear();
    for (const sla::SupportPoint& point : m_model_object->sla_support_points)
        m_editing_mode_cache.emplace_back(point, false);

    m_unsaved_changes = false;
}



void GLGizmoSlaSupports::get_data_from_backend()
{
    for (const SLAPrintObject* po : m_parent.sla_print()->objects()) {
        if (po->model_object()->id() == m_model_object->id() && po->is_step_done(slaposSupportPoints)) {
            m_editing_mode_cache.clear();
            const std::vector<sla::SupportPoint>& points = po->get_support_points();
            auto mat = po->trafo().inverse().cast<float>();
            for (unsigned int i=0; i<points.size();++i)
                m_editing_mode_cache.emplace_back(sla::SupportPoint(mat * points[i].pos, points[i].head_front_radius, points[i].is_new_island), false);

            if (m_model_object->sla_points_status != sla::PointsStatus::UserModified)
                m_model_object->sla_points_status = sla::PointsStatus::AutoGenerated;

            break;
        }
    }
    m_unsaved_changes = false;

    // We don't copy the data into ModelObject, as this would stop the background processing.
}



void GLGizmoSlaSupports::auto_generate()
{
    wxMessageDialog dlg(GUI::wxGetApp().plater(), _(L(
                "Autogeneration will erase all manually edited points.\n\n"
                "Are you sure you want to do it?\n"
                )), _(L("Warning")), wxICON_WARNING | wxYES | wxNO);

    if (m_model_object->sla_points_status != sla::PointsStatus::UserModified || m_editing_mode_cache.empty() || dlg.ShowModal() == wxID_YES) {
        m_model_object->sla_support_points.clear();
        m_model_object->sla_points_status = sla::PointsStatus::Generating;
        m_editing_mode_cache.clear();
        wxGetApp().CallAfter([this]() { wxGetApp().plater()->reslice_SLA_supports(*m_model_object); });
    }
}



void GLGizmoSlaSupports::switch_to_editing_mode()
{
    if (m_model_object->sla_points_status != sla::PointsStatus::AutoGenerated)
        editing_mode_reload_cache();
    m_unsaved_changes = false;
    m_editing_mode = true;
}

} // namespace GUI
} // namespace Slic3r
