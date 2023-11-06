// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoAdvancedCut.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>
#include <wx/sizer.h>

#include <algorithm>
#include "GLGizmosCommon.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/AppConfig.hpp"

#include <imgui/imgui_internal.h>

namespace Slic3r {
namespace GUI {

const double       units_in_to_mm = 25.4;
const double       units_mm_to_in = 1 / units_in_to_mm;

const int c_connectors_group_id = 6;
const int c_cube_z_move_id      = 3;
const int c_cube_x_move_id      = 4;
const int c_plate_move_id       = 5;
const int c_connectors_start_id = c_connectors_group_id - c_cube_z_move_id;
const float UndefFloat = -999.f;

// connector colors
static const ColorRGBA BLACK() { return {0.0f, 0.0f, 0.0f, 1.0f}; }
static const ColorRGBA BLUE() { return {0.0f, 0.0f, 1.0f, 1.0f}; }
static const ColorRGBA BLUEISH() { return {0.5f, 0.5f, 1.0f, 1.0f}; }
static const ColorRGBA CYAN() { return {0.0f, 1.0f, 1.0f, 1.0f}; }
static const ColorRGBA DARK_GRAY() { return {0.25f, 0.25f, 0.25f, 1.0f}; }
static const ColorRGBA DARK_YELLOW() { return {0.5f, 0.5f, 0.0f, 1.0f}; }
static const ColorRGBA GRAY() { return {0.5f, 0.5f, 0.5f, 1.0f}; }
static const ColorRGBA GREEN() { return {0.0f, 1.0f, 0.0f, 1.0f}; }
static const ColorRGBA GREENISH() { return {0.5f, 1.0f, 0.5f, 1.0f}; }
static const ColorRGBA LIGHT_GRAY() { return {0.75f, 0.75f, 0.75f, 1.0f}; }
static const ColorRGBA MAGENTA() { return {1.0f, 0.0f, 1.0f, 1.0f}; }
static const ColorRGBA ORANGE() { return {0.923f, 0.504f, 0.264f, 1.0f}; }
static const ColorRGBA RED() { return {1.0f, 0.0f, 0.0f, 1.0f}; }
static const ColorRGBA REDISH() { return {1.0f, 0.5f, 0.5f, 1.0f}; }
static const ColorRGBA YELLOW() { return {1.0f, 1.0f, 0.0f, 1.0f}; }
static const ColorRGBA WHITE() { return {1.0f, 1.0f, 1.0f, 1.0f}; }

static const ColorRGBA PLAG_COLOR           = YELLOW();
static const ColorRGBA DOWEL_COLOR          = DARK_YELLOW();
static const ColorRGBA HOVERED_PLAG_COLOR   = CYAN();
static const ColorRGBA HOVERED_DOWEL_COLOR  = {0.0f, 0.5f, 0.5f, 1.0f};
static const ColorRGBA SELECTED_PLAG_COLOR  = GRAY();
static const ColorRGBA SELECTED_DOWEL_COLOR = GRAY(); // DARK_GRAY();
static const ColorRGBA CONNECTOR_DEF_COLOR  = {1.0f, 1.0f, 1.0f, 0.5f};
static const ColorRGBA CONNECTOR_ERR_COLOR  = {1.0f, 0.3f, 0.3f, 0.5f};
static const ColorRGBA HOVERED_ERR_COLOR    = {1.0f, 0.3f, 0.3f, 1.0f};

static const ColorRGBA CUT_PLANE_DEF_COLOR = {0.9f, 0.9f, 0.9f, 0.5f};
static const ColorRGBA CUT_PLANE_ERR_COLOR = {1.0f, 0.8f, 0.8f, 0.5f};

static const ColorRGBA UPPER_PART_COLOR = CYAN();
static const ColorRGBA LOWER_PART_COLOR = MAGENTA();
static const ColorRGBA MODIFIER_COLOR   = {0.75f, 0.75f, 0.75f, 0.5f};

static Vec3d rotate_vec3d_around_vec3d_with_rotate_matrix(
    const Vec3d& rotate_point,
    const Vec3d& origin_point,
    const Transform3d& rotate_matrix)
{
    Transform3d translate_to_point = Transform3d::Identity();
    translate_to_point.translate(origin_point);
    Transform3d translate_to_zero = Transform3d::Identity();
    translate_to_zero.translate(-origin_point);
    return (translate_to_point * rotate_matrix * translate_to_zero) * rotate_point;
}

static inline void rotate_point_2d(double& x, double& y, const double c, const double s)
{
    double xold = x;
    double yold = y;
    x = c * xold - s * yold;
    y = s * xold + c * yold;
}

static void rotate_x_3d(std::array<Vec3d, 4>& verts, float radian_angle)
{
    double c = cos(radian_angle);
    double s = sin(radian_angle);
    for (uint32_t i = 0; i < verts.size(); ++i)
        rotate_point_2d(verts[i](1), verts[i](2), c, s);
}

static void rotate_y_3d(std::array<Vec3d, 4>& verts, float radian_angle)
{
    double c = cos(radian_angle);
    double s = sin(radian_angle);
    for (uint32_t i = 0; i < verts.size(); ++i)
        rotate_point_2d(verts[i](2), verts[i](0), c, s);
}

static void rotate_z_3d(std::array<Vec3d, 4>& verts, float radian_angle)
{
    double c = cos(radian_angle);
    double s = sin(radian_angle);
    for (uint32_t i = 0; i < verts.size(); ++i)
        rotate_point_2d(verts[i](0), verts[i](1), c, s);
}

const double GLGizmoAdvancedCut::Offset = 10.0;
const double GLGizmoAdvancedCut::Margin = 20.0;
const std::array<float, 4> GLGizmoAdvancedCut::GrabberColor      = { 1.0, 1.0, 0.0, 1.0 };
const std::array<float, 4> GLGizmoAdvancedCut::GrabberHoverColor = { 0.7, 0.7, 0.0, 1.0};

GLGizmoAdvancedCut::GLGizmoAdvancedCut(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoRotate3D(parent, icon_filename, sprite_id, nullptr)
    , m_movement(0.0)
    , m_buffered_movement(0.0)
    , m_keep_upper(true)
    , m_keep_lower(true)
    , m_cut_to_parts(false)
    , m_do_segment(false)
    , m_segment_smoothing_alpha(0.5)
    , m_segment_number(5)
    , m_connector_type(CutConnectorType::Plug)
    , m_connector_style(size_t(CutConnectorStyle::Prizm))
    , m_connector_shape_id(size_t(CutConnectorShape::Circle))
{
    set_group_id(m_gizmos.size());
    m_rotation.setZero();
    m_buffered_rotation.setZero();
}

bool GLGizmoAdvancedCut::gizmo_event(SLAGizmoEventType action, const Vec2d &mouse_position, bool shift_down, bool alt_down, bool control_down)
{
    CutConnectors &connectors = m_c->selection_info()->model_object()->cut_connectors;

    if (shift_down && !m_connectors_editing &&
        (action == SLAGizmoEventType::LeftDown || action == SLAGizmoEventType::LeftUp || action == SLAGizmoEventType::Dragging)) {
        process_cut_line(action, mouse_position);
        return true;
    } else { // if (!shift_down)
        discard_cut_line_processing();
    }

    if (action == SLAGizmoEventType::LeftDown) {
        if (m_hover_id == c_plate_move_id) {
            Vec3d pos;
            Vec3d pos_world;
            if (unproject_on_cut_plane(mouse_position, pos, pos_world, false)) {
                m_plane_drag_start = pos_world;
            }
        }
        if (!m_connectors_editing)
            return false;

        if (m_hover_id != -1) {
            start_dragging();
            return true;
        }

        if (shift_down || alt_down) {
            // left down with shift - show the selection rectangle:
            //if (m_hover_id == -1)
            //    m_selection_rectangle.start_dragging(mouse_position, shift_down ? GLSelectionRectangle::EState::Select : GLSelectionRectangle::EState::Deselect);
        } else {
            // If there is no selection and no hovering, add new point
            if (m_hover_id == -1 && !shift_down && !alt_down)
                add_connector(connectors, mouse_position);
                    //m_ldown_mouse_position = mouse_position;
        }
        return true;
    }
    else if (action == SLAGizmoEventType::LeftUp) {
        if (m_connectors_editing && m_connector_type == CutConnectorType::Snap && m_add_connector_ok && m_hover_id == -1) {
            return true;//Snap connector special logic, due to gaps in the middle of the snap, other connectors such as cylinders are solid
        }
        if (m_hover_id == -1 && !shift_down && !alt_down)
            unselect_all_connectors();

        is_selection_changed(alt_down, shift_down);
        return true;
    }
    else if (action == SLAGizmoEventType::RightDown) {
        if (!m_connectors_editing && m_cut_mode == CutMode::cutPlanar) { //&& control_down
            // Check the internal part raycasters.
            if (m_part_selection && m_part_selection->valid()) {
                m_part_selection->toggle_selection(mouse_position);
                check_and_update_connectors_state(); // after a contour is deactivated, its connectors are inside the object
            }
            return true;
        }
        if (m_hover_id < c_connectors_group_id)
            return false;

        unselect_all_connectors();
        select_connector(m_hover_id - c_connectors_group_id, true);
        return delete_selected_connectors();
    }
    else if (action == SLAGizmoEventType::RightUp) {
        // catch right click event
        return true;
    }

    return false;
}

bool GLGizmoAdvancedCut::on_key(wxKeyEvent &evt)
{
    bool ctrl_down = evt.GetModifiers() & wxMOD_CONTROL;

    if (evt.GetKeyCode() == WXK_DELETE) {
        return delete_selected_connectors();
    }
    else if (ctrl_down
        && (evt.GetKeyCode() == 'A' || evt.GetKeyCode() == 'a'))
    {
        select_all_connectors();
        return true;
    }
    return false;
}

std::string GLGizmoAdvancedCut::get_tooltip() const
{
    std::string tooltip;
    if (m_dragging && (m_hover_id == c_plate_move_id || m_hover_id == c_cube_z_move_id)) {
        double               koef     = m_imperial_units ? units_mm_to_in : 1.0;
        std::string          unit_str = " " + (m_imperial_units ? _u8L("in") : _u8L("mm"));
        const BoundingBoxf3 &tbb      = m_transformed_bounding_box;

        const std::string name = m_cut_to_parts ? _u8L("Part") : _u8L("Object");
        if (tbb.max.z() >= 0.0) {
            double top = (tbb.min.z() <= 0.0 ? tbb.max.z() : tbb.size().z()) * koef;
            tooltip += format(static_cast<float>(top), 2) + " " + unit_str + " (" + name + " A)";
            if (tbb.min.z() <= 0.0) tooltip += "\n";
        }
        if (tbb.min.z() <= 0.0) {
            double bottom = (tbb.max.z() <= 0.0 ? tbb.size().z() : (tbb.min.z() * (-1))) * koef;
            tooltip += format(static_cast<float>(bottom), 2) + " " + unit_str + " (" + name + " B)";
        }
        return tooltip;
    }

    if (!m_dragging && m_hover_id == c_plate_move_id) {
        if (m_cut_mode == CutMode::cutTongueAndGroove) return _u8L("Drag to move the cut plane");
        return _u8L("Drag to move the cut plane\n"
                    "Right-click a part to assign it to the other side");
    }

    if (tooltip.empty() && (m_hover_id == X || m_hover_id == Y || m_hover_id == Z)) {
        std::string axis = m_hover_id == X ? "X" : m_hover_id == Y ? "Y" : "Z";
        return axis + ": " + format(float(Geometry::rad2deg(m_rotate_angle)), 1) + _u8L("°");
    }

    return tooltip;
}

BoundingBoxf3 GLGizmoAdvancedCut::bounding_box() const
{
    BoundingBoxf3                 ret;
    const Selection &             selection = m_parent.get_selection();
    const Selection::IndicesList &idxs      = selection.get_volume_idxs();
    for (unsigned int i : idxs) {
        const GLVolume *volume = selection.get_volume(i);
        // respect just to the solid parts for FFF and ignore pad and supports for SLA
        if (!volume->is_modifier && !volume->is_sla_pad() && !volume->is_sla_support())
            ret.merge(volume->transformed_convex_hull_bounding_box());
    }
    return ret;
}

BoundingBoxf3 GLGizmoAdvancedCut::transformed_bounding_box(const Vec3d &plane_center, const Transform3d &rotation_m) const
{
    const Selection &selection = m_parent.get_selection();

    const auto first_volume    = selection.get_first_volume();
    Vec3d      instance_offset = first_volume->get_instance_offset();
    instance_offset[Z] += first_volume->get_sla_shift_z();

    const auto cut_matrix = Transform3d::Identity() * rotation_m.inverse() * Geometry::translation_transform(instance_offset - plane_center);

    const Selection::IndicesList &idxs = selection.get_volume_idxs();
    BoundingBoxf3                 ret;
    for (unsigned int i : idxs) {
        const GLVolume *volume = selection.get_volume(i);
        // respect just to the solid parts for FFF and ignore pad and supports for SLA
        if (!volume->is_modifier && !volume->is_sla_pad() && !volume->is_sla_support()) {
            const auto instance_matrix = volume->get_instance_transformation().get_matrix_no_offset();
            auto       volume_trafo    = instance_matrix * volume->get_volume_transformation().get_matrix();
            ret.merge(volume->transformed_convex_hull_bounding_box(cut_matrix * volume_trafo));
        }
    }
    return ret;
}

bool GLGizmoAdvancedCut::is_looking_forward() const
{
    const Camera &camera = wxGetApp().plater()->get_camera();
    const double  dot    = camera.get_dir_forward().dot(m_plane_normal);
    return dot < 0.05;
}

// Unprojects the mouse position on the mesh and saves hit point and normal of the facet into pos_and_normal
// Return false if no intersection was found, true otherwise.
bool GLGizmoAdvancedCut::unproject_on_cut_plane(const Vec2d &mouse_pos, Vec3d &pos, Vec3d &pos_world, bool respect_contours)
{
    const float sla_shift = m_c->selection_info()->get_sla_shift();

    const ModelObject *  mo     = m_c->selection_info()->model_object();
    const ModelInstance *mi     = mo->instances[m_c->selection_info()->get_active_instance()];
    const Camera &       camera = wxGetApp().plater()->get_camera();

    // Calculate intersection with the clipping plane.
    const ClippingPlane *cp = m_c->object_clipper()->get_clipping_plane();
    Vec3d                point;
    Vec3d                direction;
    Vec3d                hit;
    MeshRaycaster::line_from_mouse_pos_static(mouse_pos, Transform3d::Identity(), camera, point, direction);
    Vec3d  normal = -cp->get_normal().cast<double>();
    double den    = normal.dot(direction);
    if (den != 0.) {
        double t = (-cp->get_offset() - normal.dot(point)) / den;
        hit      = (point + t * direction);
    } else
        return false;

    if (respect_contours) {
        // Do not react to clicks outside a contour (or inside a contour that is ignored)
        int cont_id = m_c->object_clipper()->is_projection_inside_cut(hit);
        if (cont_id == -1)
            return false;
        if (m_part_selection&&m_part_selection->valid()) {
            const std::vector<size_t> &ign = *m_part_selection->get_ignored_contours_ptr();
            if (std::find(ign.begin(), ign.end(), cont_id) != ign.end())
                return false;
        }
    }

    // recalculate hit to object's local position
    Vec3d hit_d = hit;
    hit_d -= mi->get_offset();
    hit_d[Z] -= sla_shift;

    // Return both the point and the facet normal.
    pos       = hit_d;
    pos_world = hit;

    return true;
}

void GLGizmoAdvancedCut::render_glmodel(GLModel &model, const std::array<float, 4> &color, Transform3d view_model_matrix, bool for_picking)
{
    glPushMatrix();
    GLShaderProgram *shader = nullptr;
    if (for_picking)
        shader = wxGetApp().get_shader("cali");
    else
        shader = wxGetApp().get_shader("gouraud_light");
    if (shader) {
        shader->start_using();

        glsafe(::glMultMatrixd(view_model_matrix.data()));

        model.set_color(-1, color);
        model.render();

        shader->stop_using();
    }
    glPopMatrix();
}

void GLGizmoAdvancedCut::reset_cut_plane()
{
    m_transformed_bounding_box = transformed_bounding_box(m_bb_center);
    set_center(m_bb_center);
    m_start_dragging_m = m_rotate_matrix = Transform3d::Identity();
    update_plane_normal();
    m_ar_plane_center  = m_plane_center;

    reset_cut_by_contours();
    m_parent.request_extra_frame();

    const Selection& selection = m_parent.get_selection();
    const BoundingBoxf3& box = selection.get_bounding_box();

    m_movement = 0.0;
    m_height = box.size()[2] / 2.0;
    m_rotation.setZero();

    m_buffered_movement = 0.0;
    m_buffered_height = m_height;
    m_buffered_rotation.setZero();
}

void GLGizmoAdvancedCut::reset_all()
{
    Plater::TakeSnapshot snapshot(wxGetApp().plater(), "reset cut");
    reset_connectors();
    reset_cut_plane();

    m_keep_upper = true;
    m_keep_lower = true;
    m_cut_to_parts = false;
    m_place_on_cut_upper = true;
    m_place_on_cut_lower = false;
    m_rotate_upper = false;
    m_rotate_lower = false;
}

bool GLGizmoAdvancedCut::on_init()
{
    if (!GLGizmoRotate3D::on_init())
        return false;

    m_shortcut_key = WXK_CONTROL_C;

    // initiate info shortcuts
    const wxString ctrl  = GUI::shortkey_ctrl_prefix();
    const wxString alt   = GUI::shortkey_alt_prefix();
    const wxString shift = "Shift+";

    m_connector_shortcuts.push_back(std::make_pair(_L("Left click"), _L("Add connector")));
    m_connector_shortcuts.push_back(std::make_pair(_L("Right click"), _L("Remove connector")));
    m_connector_shortcuts.push_back(std::make_pair(_L("Drag"), _L("Move connector")));
    m_connector_shortcuts.push_back(std::make_pair(shift + _L("Left click"), _L("Add connector to selection")));
    m_connector_shortcuts.push_back(std::make_pair(alt + _L("Left click"), _L("Remove connector from selection")));
    m_connector_shortcuts.push_back(std::make_pair(ctrl + "A", _L("Select all connectors")));

    m_cut_plane_shortcuts.push_back(std::make_pair(shift + _L("Left drag"), _L("Plot cut plane")));
    m_cut_plane_shortcuts.push_back(std::make_pair(_L("right click"), _L("Assign the part to the other side")));

    m_cut_groove_shortcuts.push_back(std::make_pair(shift + _L("Left click"), _L("Plot cut plane")));

    init_connector_shapes();
    return true;
}

std::string GLGizmoAdvancedCut::on_get_name() const
{
    return (_(L("Cut"))).ToUTF8().data();
}

void GLGizmoAdvancedCut::on_load(cereal::BinaryInputArchive &ar)
{
    size_t mode;
    float  groove_depth;
    float  groove_width;
    float  groove_flaps_angle;
    float  groove_angle;
    float  groove_depth_tolerance;
    float  groove_width_tolerance;

    ar(m_keep_upper, m_keep_lower, m_rotate_lower, m_rotate_upper, m_hide_cut_plane, mode, m_connectors_editing, m_ar_plane_center, m_rotate_matrix, groove_depth, groove_width,
       groove_flaps_angle, groove_angle, groove_depth_tolerance, groove_width_tolerance);

    m_start_dragging_m = m_rotate_matrix;

    m_transformed_bounding_box = transformed_bounding_box(m_ar_plane_center, m_rotate_matrix);
    set_center_pos(m_ar_plane_center);

    if (m_cut_mode != (CutMode) mode)
        switch_to_mode((CutMode) mode);
    else if (m_cut_mode == CutMode::cutTongueAndGroove) {
        if (!is_approx(m_groove.depth, groove_depth) || !is_approx(m_groove.width, groove_width) || !is_approx(m_groove.flaps_angle, groove_flaps_angle) ||
            !is_approx(m_groove.angle, groove_angle) || !is_approx(m_groove.depth_tolerance, groove_depth_tolerance) ||
            !is_approx(m_groove.width_tolerance, groove_width_tolerance)) {
            m_groove.depth           = groove_depth;
            m_groove.width           = groove_width;
            m_groove.flaps_angle     = groove_flaps_angle;
            m_groove.angle           = groove_angle;
            m_groove.depth_tolerance = groove_depth_tolerance;
            m_groove.width_tolerance = groove_width_tolerance;
            update_plane_model();
        }
        reset_cut_by_contours();
    }

    m_parent.request_extra_frame();
}

void GLGizmoAdvancedCut::on_save(cereal::BinaryOutputArchive &ar) const
{
    ar(m_keep_upper, m_keep_lower, m_rotate_lower, m_rotate_upper, m_hide_cut_plane, (size_t) m_cut_mode, m_connectors_editing, m_ar_plane_center, m_start_dragging_m,
       m_groove.depth, m_groove.width, m_groove.flaps_angle, m_groove.angle, m_groove.depth_tolerance, m_groove.width_tolerance);
}

void GLGizmoAdvancedCut::data_changed(bool is_serializing)
{
    if (m_hover_id < 0) { // BBL
        update_bb();
        if (auto oc = m_c->object_clipper()) {
            oc->set_behaviour(m_connectors_editing, m_connectors_editing, double(m_contour_width));
            reset_cut_by_contours();
        }
    }
}

void GLGizmoAdvancedCut::on_set_state()
{
    GLGizmoRotate3D::on_set_state();

    // Reset m_cut_z on gizmo activation
    if (get_state() == On) {
        m_hover_id           = -1;
        m_connectors_editing = false;
        reset_cut_plane();

        update_bb();
        m_connectors_editing       = !m_selected.empty();
        m_transformed_bounding_box = transformed_bounding_box(m_plane_center, m_rotate_matrix);

        // initiate archived values
        m_ar_plane_center  = m_plane_center;
        m_start_dragging_m = m_rotate_matrix;

        m_parent.request_extra_frame();
    }
    else if (get_state() == Off) {
        toggle_model_objects_visibility(true);
        if (auto oc = m_c->object_clipper()) {
            oc->set_behaviour(true, true, 0.);
            oc->release();
        }
        m_selected.clear();
        m_c->selection_info()->set_use_shift(false);

        // Make sure that the part selection data are released when the gizmo is closed.
        // The CallAfter is needed because in perform_cut, the gizmo is closed BEFORE
        // the cut is performed (because of undo/redo snapshots), so the data would
        // be deleted prematurely.
        if (m_part_selection && m_part_selection->valid()) {
            wxGetApp().CallAfter([this]() {
                delete_part_selection();
                m_part_selection = new PartSelection();
            });
        }
    }
}

bool GLGizmoAdvancedCut::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();
    return selection.is_single_full_instance() && !selection.is_wipe_tower();
}

CommonGizmosDataID GLGizmoAdvancedCut::on_get_requirements() const
{
    return CommonGizmosDataID(int(CommonGizmosDataID::SelectionInfo)
        | int(CommonGizmosDataID::InstancesHider)
        | int(CommonGizmosDataID::Raycaster)
        | int(CommonGizmosDataID::ObjectClipper));
}

void GLGizmoAdvancedCut::on_start_dragging()
{
    if (m_connectors_editing && m_hover_id >= c_connectors_group_id) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Move connector");
        return;
    } else if (m_hover_id <= 2) {
        for (auto gizmo : m_gizmos) {
            if (m_hover_id == gizmo.get_group_id()) {
                gizmo.start_dragging();
                return;
            }
        }
        m_rotate_angle = 0;
        m_start_dragging_m = m_rotate_matrix;
    } else if (m_hover_id >= c_cube_z_move_id && c_connectors_group_id) {
        const Selection &    selection = m_parent.get_selection();
        const BoundingBoxf3 &box       = selection.get_bounding_box();
        m_start_movement               = 0;
        m_start_height                 = m_height;
        m_plane_center_drag_start      = m_plane_center;
        if (m_hover_id == c_cube_z_move_id) {
            m_drag_pos_start = m_move_z_grabber.center;
        } else if (m_hover_id == c_cube_x_move_id) {
            m_drag_pos_start = m_move_x_grabber.center;
        } else if (m_hover_id == c_plate_move_id) {
            m_drag_pos_start = m_plane_drag_start;
        }
        m_start_dragging_m = m_rotate_matrix;
    }
}

void GLGizmoAdvancedCut::on_stop_dragging()
{
    m_is_dragging = false;
    if (m_hover_id == X || m_hover_id == Y || m_hover_id == Z) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Rotate cut plane");
    } else if (m_hover_id == c_cube_z_move_id || m_hover_id == c_cube_x_move_id || m_hover_id == c_plate_move_id) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Move cut plane"); // todo
        m_ar_plane_center = m_plane_center;
    }
    m_plane_center_drag_start = m_plane_center;
    reset_cut_by_contours();
    m_movement     = 0.0;
    m_rotation.setZero();
}

void GLGizmoAdvancedCut::update_plate_center(Axis axis_type, double projection, bool is_abs_move) {
    const Vec3d shift = m_rotate_matrix * ((axis_type == Axis::Z ? Vec3d::UnitZ() : axis_type == Axis::Y ? Vec3d::UnitY() : Vec3d::UnitX()) * projection);
    if (shift != Vec3d::Zero()) {
        // update m_plane_center
        if (is_abs_move) {
            this->set_center(m_plane_center_drag_start + shift, true);
        } else {
            this->set_center(m_plane_center + shift, true);
        }
    }
}

void GLGizmoAdvancedCut::update_plate_normal_boundingbox_clipper(Vec3d rotation)
{
    const Transform3d rotation_tmp = Geometry::rotation_transform(rotation) * m_start_dragging_m;
    const bool        update_tbb   = !m_rotate_matrix.rotation().isApprox(rotation_tmp.rotation());
    m_rotate_matrix                = rotation_tmp;
    if (update_tbb) m_transformed_bounding_box = transformed_bounding_box(m_plane_center, m_rotate_matrix);
    update_clipper();
}

void GLGizmoAdvancedCut::on_update(const UpdateData& data)
{
    if (m_hover_id < 0)
        return;
    m_is_dragging = true;
    if (m_hover_id <= 2) { // drag rotate
        GLGizmoRotate3D::on_update(data);
        Vec3d rotation;
        for (int i = 0; i < 3; i++) {
            rotation(i) = m_gizmos[i].get_angle();
            if (rotation(i) < 0) rotation(i) = 2 * PI + rotation(i);
            if (rotation(i) != 0) { m_rotate_angle = rotation(i); }
        }
        m_rotation = rotation;
        // deal rotate
        if (!is_approx(rotation, Vec3d(0, 0, 0))) { update_plate_normal_boundingbox_clipper(rotation); }
    } // move plane
    else if (m_hover_id == c_cube_z_move_id || m_hover_id == c_plate_move_id) {
        double move = calc_projection(m_drag_pos_start, data.mouse_ray, m_plane_normal);
        m_movement  = m_start_movement + move;
        update_plate_center(Axis::Z, move, true);
    } // move x
    else if (m_hover_id == c_cube_x_move_id && m_cut_mode == CutMode::cutTongueAndGroove) {
        double move = calc_projection(m_drag_pos_start, data.mouse_ray, m_plane_x_direction);
        m_movement  = m_start_movement + move;
        update_plate_center(Axis::X, move, true);
    } // dragging connectors
    else if (m_connectors_editing && m_hover_id >= c_connectors_group_id) {
        CutConnectors &connectors = m_c->selection_info()->model_object()->cut_connectors;
        Vec3d          pos;
        Vec3d          pos_world;

        if (unproject_on_cut_plane(data.mouse_pos.cast<double>(), pos, pos_world)) {
            connectors[m_hover_id - c_connectors_group_id].pos = pos;
        }
    }
    check_and_update_connectors_state();
}

void GLGizmoAdvancedCut::on_render()
{
    if (m_state == On) {
        // This gizmo is showing the object elevated. Tell the common
        // SelectionInfo object to lie about the actual shift.
        m_c->selection_info()->set_use_shift(true);
    }
    // check objects visibility
    toggle_model_objects_visibility();

    update_clipper();
    init_picking_models();
    if (m_connectors_editing) {
        render_connectors();
    }

    if (m_part_selection) {
        if (!m_connectors_editing) {
            if (m_is_dragging == false) { m_part_selection->part_render(nullptr); }
        } else
            m_part_selection->part_render(&m_plane_normal);
    }
    if (!m_connectors_editing) {
        render_cut_plane_and_grabbers();
    }
    // render_clipper_cut for get the cut plane result
    render_clipper_cut();
    // render a cut line on screen by shift key and mouse move
    render_cut_line();
}

void GLGizmoAdvancedCut::on_render_for_picking()
{
    if (!m_connectors_editing) {
        glsafe(::glDisable(GL_DEPTH_TEST));
        std::array<float, 4> color;
        // pick plane
        {
            color = picking_color_component(2);
            render_glmodel(m_plane, color, Geometry::translation_transform(m_plane_center) * m_rotate_matrix, true);
        }
        // pick Rotate
        GLGizmoRotate3D::on_render_for_picking();

        BoundingBoxf3 box = m_parent.get_selection().get_bounding_box();
#if ENABLE_FIXED_GRABBER
        float mean_size = (float) (GLGizmoBase::Grabber::FixedGrabberSize);
#else
        float mean_size = (float) ((box.size().x() + box.size().y() + box.size().z()) / 3.0);
#endif
        // pick grabber
        {
            color                     = picking_color_component(0);
            m_move_z_grabber.color[0] = color[0];
            m_move_z_grabber.color[1] = color[1];
            m_move_z_grabber.color[2] = color[2];
            m_move_z_grabber.color[3] = color[3];
            m_move_z_grabber.render_for_picking(mean_size);
            if (m_cut_mode == CutMode::cutTongueAndGroove) {
                color                     = picking_color_component(1);
                m_move_x_grabber.color[0] = color[0];
                m_move_x_grabber.color[1] = color[1];
                m_move_x_grabber.color[2] = color[2];
                m_move_x_grabber.color[3] = color[3];
                m_move_x_grabber.render_for_picking(mean_size);
            }
        }

    } else {
        glsafe(::glEnable(GL_DEPTH_TEST));
        auto inst_id = m_c->selection_info()->get_active_instance();
        if (inst_id < 0) return;

        const ModelObject *  mo              = m_c->selection_info()->model_object();
        const ModelInstance *mi              = mo->instances[inst_id];
        const Vec3d &        instance_offset = mi->get_offset();
        const double         sla_shift       = double(m_c->selection_info()->get_sla_shift());

        const CutConnectors &connectors      = mo->cut_connectors;
        const bool           looking_forward = is_looking_forward();
        for (int i = 0; i < connectors.size(); ++i) {
            CutConnector connector = connectors[i];
            Vec3d        pos       = connector.pos + instance_offset + sla_shift * Vec3d::UnitZ();
            float        height    = connector.height;

            deal_connector_pos_by_type(pos, height, connector.attribs.type, connector.attribs.style, looking_forward, m_connectors_editing, m_clp_normal);

            Transform3d translate_tf = Transform3d::Identity();
            translate_tf.translate(pos);

            Transform3d scale_tf = Transform3d::Identity();
            scale_tf.scale(Vec3f(connector.radius, connector.radius, height).cast<double>());

            const Transform3d view_model_matrix = translate_tf * m_rotate_matrix * scale_tf;

            std::array<float, 4> color = picking_color_component(i + c_connectors_start_id);
            render_glmodel(m_shapes[connectors[i].attribs], color, view_model_matrix, true);
        }
    }
}

void GLGizmoAdvancedCut::on_render_input_window(float x, float y, float bottom_limit)
{
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);
    ImGuiWrapper::push_toolbar_style(m_parent.get_scale());
    GizmoImguiBegin(on_get_name(),
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    if (m_connectors_editing) {
        init_connectors_input_window_data();
        render_connectors_input_window(x, y, bottom_limit);
    }
    else
        render_cut_plane_input_window(x, y, bottom_limit);

    render_input_window_warning();

    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoAdvancedCut::show_tooltip_information(float x, float y)
{
    float caption_max = 0.f;
    if (m_connectors_editing) {
        for (const auto &short_cut : m_connector_shortcuts) { caption_max = std::max(caption_max, m_imgui->calc_text_size(short_cut.first).x); }
    } else if (m_cut_mode == CutMode::cutPlanar) {
        for (const auto &short_cut : m_cut_plane_shortcuts) { caption_max = std::max(caption_max, m_imgui->calc_text_size(short_cut.first).x); }
    } else if (m_cut_mode == CutMode::cutTongueAndGroove) {
        for (const auto &short_cut : m_cut_groove_shortcuts) { caption_max = std::max(caption_max, m_imgui->calc_text_size(short_cut.first).x); }
    }

    ImTextureID normal_id = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP);
    ImTextureID hover_id  = m_parent.get_gizmos_manager().get_icon_texture_id(GLGizmosManager::MENU_ICON_NAME::IC_TOOLBAR_TOOLTIP_HOVER);

    caption_max += m_imgui->calc_text_size(": ").x + 35.f;

    float  font_size   = ImGui::GetFontSize();
    ImVec2 button_size = ImVec2(font_size * 1.8, font_size * 1.3);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, ImGui::GetStyle().FramePadding.y});
    ImGui::ImageButton3(normal_id, hover_id, button_size);

    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip2(ImVec2(x, y));
        auto draw_text_with_caption = [this, &caption_max](const wxString &caption, const wxString &text) {
            m_imgui->text_colored(ImGuiWrapper::COL_ACTIVE, caption);
            ImGui::SameLine(caption_max);
            m_imgui->text_colored(ImGuiWrapper::COL_WINDOW_BG, text);
        };
        if (m_connectors_editing) {
            for (const auto &short_cut : m_connector_shortcuts) draw_text_with_caption(short_cut.first + ": ", short_cut.second);
        } else if (m_cut_mode == CutMode::cutPlanar) {
            for (const auto &short_cut : m_cut_plane_shortcuts) draw_text_with_caption(short_cut.first + ": ", short_cut.second);
        } else if (m_cut_mode == CutMode::cutTongueAndGroove) {
            for (const auto &short_cut : m_cut_groove_shortcuts) draw_text_with_caption(short_cut.first + ": ", short_cut.second);
        }
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar(2);
}

void update_object_cut_id(CutObjectBase &cut_id, ModelObjectCutAttributes attributes, const int dowels_count)
{
    // we don't save cut information, if result will not contains all parts of initial object
    if (!attributes.has(ModelObjectCutAttribute::KeepUpper) || !attributes.has(ModelObjectCutAttribute::KeepLower) || attributes.has(ModelObjectCutAttribute::InvalidateCutInfo))
        return;

    if (cut_id.id().invalid()) cut_id.init();
    // increase check sum, if it's needed
    {
        int cut_obj_cnt = -1;
        if (attributes.has(ModelObjectCutAttribute::KeepUpper)) cut_obj_cnt++;
        if (attributes.has(ModelObjectCutAttribute::KeepLower)) cut_obj_cnt++;
        if (attributes.has(ModelObjectCutAttribute::CreateDowels)) cut_obj_cnt += dowels_count;
        if (cut_obj_cnt > 0) cut_id.increase_check_sum(size_t(cut_obj_cnt));
    }
}

void synchronize_model_after_cut(Model &model, const CutObjectBase &cut_id)
{
    for (ModelObject *obj : model.objects)
        if (obj->is_cut() && obj->cut_id.has_same_id(cut_id) && !obj->cut_id.is_equal(cut_id)) obj->cut_id.copy(cut_id);
}

void GLGizmoAdvancedCut::perform_cut(const Selection& selection)
{
    if (!can_perform_cut()) return;

    const int instance_idx = selection.get_instance_idx();
    const int object_idx   = selection.get_object_idx();

    wxCHECK_RET(instance_idx >= 0 && object_idx >= 0, "GLGizmoAdvancedCut: Invalid object selection");

    Plater *     plater = wxGetApp().plater();
    ModelObject *mo     = plater->model().objects[object_idx];
    if (!mo) return;
    // deactivate CutGizmo and than perform a cut
    m_parent.reset_all_gizmos();
    // m_cut_z is the distance from the bed. Subtract possible SLA elevation.
    // const GLVolume* first_glvolume = selection.get_volume(*selection.get_volume_idxs().begin());

    // perform cut
    {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Cut by Plane");

        // This shall delete the part selection class and deallocate the memory.
        ScopeGuard part_selection_killer([this]() {
            delete_part_selection();
            m_part_selection = new PartSelection();
        });

        const bool cut_with_groove = m_cut_mode == CutMode::cutTongueAndGroove;
        bool       cut_by_contour  = !cut_with_groove && !m_cut_to_parts && m_part_selection->valid();

        ModelObject *cut_mo = cut_by_contour ? m_part_selection->model_object() : nullptr;
        if (cut_mo)
            cut_mo->cut_connectors = mo->cut_connectors;
        else
            cut_mo = mo;

        int        dowels_count   = 0;
        const bool has_connectors = !mo->cut_connectors.empty();
        // update connectors pos as offset of its center before cut performing
        apply_connectors_in_model(cut_mo, dowels_count);
        if (dowels_count > 0) { cut_by_contour = false; }
        wxBusyCursor wait;

        ModelObjectCutAttributes attributes = only_if(has_connectors ? true : m_keep_upper, ModelObjectCutAttribute::KeepUpper) |
                                              only_if(has_connectors ? true : m_keep_lower, ModelObjectCutAttribute::KeepLower) |
                                              only_if(has_connectors ? false : m_cut_to_parts, ModelObjectCutAttribute::CutToParts) |
                                              only_if(m_place_on_cut_upper, ModelObjectCutAttribute::PlaceOnCutUpper) |
                                              only_if(m_place_on_cut_lower, ModelObjectCutAttribute::PlaceOnCutLower) |
                                              only_if(m_rotate_upper, ModelObjectCutAttribute::FlipUpper) | only_if(m_rotate_lower, ModelObjectCutAttribute::FlipLower) |
                                              only_if(dowels_count > 0, ModelObjectCutAttribute::CreateDowels) |
                                              only_if(!has_connectors && !cut_with_groove && cut_mo->cut_id.id().invalid(), ModelObjectCutAttribute::InvalidateCutInfo);

        // update cut_id for the cut object in respect to the attributes
        update_object_cut_id(cut_mo->cut_id, attributes, dowels_count);

        Cut cut(cut_mo, instance_idx, get_cut_matrix(selection), attributes);
        cut.set_offset_for_two_part        = true;
        const ModelObjectPtrs &new_objects = cut_by_contour  ? cut.perform_by_contour(m_part_selection->get_cut_parts(), dowels_count) :
                                             cut_with_groove ? cut.perform_with_groove(m_groove, m_rotate_matrix) :
                                                               cut.perform_with_plane();
        // set offset for new_objects

        // save cut_id to post update synchronization
        const CutObjectBase cut_id = cut_mo->cut_id;

        // update cut results on plater and in the model
        plater->apply_cut_object_to_model(object_idx, new_objects);

        synchronize_model_after_cut(plater->model(), cut_id);
    }
}

bool GLGizmoAdvancedCut::can_perform_cut() const
{
    if (!m_invalid_connectors_idxs.empty() || (!m_keep_upper && !m_keep_lower) || m_connectors_editing) return false;

    if (m_cut_mode == CutMode::cutTongueAndGroove) return has_valid_groove();

    if (m_part_selection && m_part_selection->valid()) return !m_part_selection->is_one_object();

    return true;
}

void GLGizmoAdvancedCut::apply_connectors_in_model(ModelObject *mo, int &dowels_count) {
    if (m_cut_mode == CutMode::cutTongueAndGroove) return;
    {
        clear_selection();

        for (CutConnector &connector : mo->cut_connectors) {
            connector.rotation_m = m_rotate_matrix; // m_rotation_m

            if (connector.attribs.type == CutConnectorType::Dowel) {
                if (connector.attribs.style == CutConnectorStyle::Prizm) connector.height *= 2;
                dowels_count++;
            } else {
                // calculate shift of the connector center regarding to the position on the cut plane
                connector.pos += m_plane_normal * 0.5 * double(connector.height);
            }
        }
        mo->apply_cut_connectors(_u8L("Connector"));
    }
}

bool GLGizmoAdvancedCut::is_selection_changed(bool alt_down, bool shift_down)
{
    if (m_hover_id >= c_connectors_group_id) {
        if (alt_down)
            select_connector(m_hover_id - c_connectors_group_id, false);
        else {
            if (!shift_down) unselect_all_connectors();
            select_connector(m_hover_id - c_connectors_group_id, true);
        }
        return true;
    }
    return false;
}

void GLGizmoAdvancedCut::select_connector(int idx, bool select)
{
    m_selected[idx] = select;
    if (select)
        ++m_selected_count;
    else
        --m_selected_count;
}

double GLGizmoAdvancedCut::calc_projection(const Vec3d &drag_pos, const Linef3 &mouse_ray, const Vec3d &project_dir) const
{
    Vec3d mouse_dir  = mouse_ray.unit_vector();
    Vec3d inters     = mouse_ray.a + (drag_pos - mouse_ray.a).dot(mouse_dir) / mouse_dir.squaredNorm() * mouse_dir;
    Vec3d inters_vec = inters - drag_pos;
    return inters_vec.dot(project_dir);
}

Vec3d GLGizmoAdvancedCut::get_plane_normal() const {
    return m_plane_normal;
}

Vec3d GLGizmoAdvancedCut::get_plane_center() const {
    return m_plane_center;
}

void GLGizmoAdvancedCut::finish_rotation()
{
    for (int i = 0; i < 3; i++) {
        m_gizmos[i].set_angle(0.);
    }
}

void GLGizmoAdvancedCut::put_connectors_on_cut_plane(const Vec3d &cp_normal, double cp_offset)
{
    ModelObject *mo = m_c->selection_info()->model_object();
    if (CutConnectors &connectors = mo->cut_connectors; !connectors.empty()) {
        const float  sla_shift       = m_c->selection_info()->get_sla_shift();
        const Vec3d &instance_offset = mo->instances[m_c->selection_info()->get_active_instance()]->get_offset();

        for (auto &connector : connectors) {
            // convert connetor pos to the world coordinates
            Vec3d pos = connector.pos + instance_offset;
            pos[Z] += sla_shift;
            // scalar distance from point to plane along the normal
            double distance = -cp_normal.dot(pos) + cp_offset;
            // move connector
            connector.pos += distance * cp_normal;
        }
    }
}

void GLGizmoAdvancedCut::update_plane_normal()
{
    // update cut_normal
    Vec3d normal        = m_rotate_matrix * Vec3d::UnitZ();
    m_plane_normal      = normal;                           // core
    m_plane_x_direction = m_rotate_matrix * Vec3d::UnitX(); // core
    m_clp_normal        = normal;
}

void GLGizmoAdvancedCut::update_clipper()
{
    update_plane_normal();
    auto normal = m_plane_normal;
    // calculate normal and offset for clipping plane
    Vec3d beg = m_bb_center;
    beg[Z] -= m_radius;
    rotate_vec3d_around_plane_center(beg, m_rotate_matrix, m_plane_center);

    double offset = normal.dot(m_plane_center);
    double dist   = normal.dot(beg);

    if (!is_looking_forward()) {
        // recalculate normal and offset for clipping plane, if camera is looking downward to cut plane
        normal = m_rotate_matrix * (-1. * Vec3d::UnitZ());
        normal.normalize();

        beg = m_bb_center;
        beg[Z] += m_radius;
        rotate_vec3d_around_plane_center(beg, m_rotate_matrix, m_plane_center);

        m_clp_normal = normal;
        offset       = normal.dot(m_plane_center);
        dist         = normal.dot(beg);
    }

    if (m_c->object_clipper()) {
        m_c->object_clipper()->set_range_and_pos(normal, offset, dist);
        put_connectors_on_cut_plane(normal, offset);
    }
}

void GLGizmoAdvancedCut::render_cut_plane_and_grabbers()
{
    // plane points is in object coordinate
    // draw plane
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    bool      is_valid = can_perform_cut() && has_valid_groove();
    ColorRGBA cp_clr   = is_valid ? CUT_PLANE_DEF_COLOR : CUT_PLANE_ERR_COLOR;
    if (m_cut_mode == CutMode::cutTongueAndGroove) {
        cp_clr[3] = cp_clr[3] - 0.1f; // cp_clr.a(cp_clr.a() - 0.1f);
    }
    render_glmodel(m_plane, cp_clr, Geometry::translation_transform(m_plane_center) * m_rotate_matrix);

    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glDisable(GL_BLEND));

    // Draw the grabber and the connecting line
    m_move_z_grabber.center  = m_plane_center + m_plane_normal * Offset;
    bool is_render_z_grabber = true;                                      // m_hover_id < 0 || m_hover_id == cube_z_move_id;
    bool is_render_x_grabber = m_cut_mode == CutMode::cutTongueAndGroove; // m_hover_id < 0 || m_hover_id == cube_x_move_id;
    glsafe(::glDisable(GL_DEPTH_TEST));
    if (is_render_z_grabber) {
        glsafe(::glLineWidth(m_hover_id != -1 ? 2.0f : 1.5f));
        glsafe(::glColor3f(1.0, 1.0, 0.0));
        glLineStipple(1, 0x0FFF);
        glEnable(GL_LINE_STIPPLE);
        ::glBegin(GL_LINES);
        ::glVertex3dv(m_plane_center.data());
        ::glVertex3dv(m_move_z_grabber.center.data());
        glsafe(::glEnd());
        glDisable(GL_LINE_STIPPLE);
    }
    m_move_x_grabber.center = m_plane_center + m_plane_x_direction * Offset;
    if (is_render_x_grabber) {
        glsafe(::glLineWidth(m_hover_id != -1 ? 2.0f : 1.5f));
        glsafe(::glColor3f(1.0, 1.0, 0.0));
        glLineStipple(1, 0x0FFF);
        glEnable(GL_LINE_STIPPLE);
        ::glBegin(GL_LINES);
        ::glVertex3dv(m_plane_center.data());
        ::glVertex3dv(m_move_x_grabber.center.data());
        glsafe(::glEnd());
        glDisable(GL_LINE_STIPPLE);
    }

    bool                 hover = (m_hover_id == get_group_id());
    std::array<float, 4> render_color;
    if (hover) {
        render_color = GrabberHoverColor;
    } else
        render_color = GrabberColor;

    // BBS set to fixed size grabber
    // float fullsize = 2 * (dragging ? get_dragging_half_size(size) : get_half_size(size));
    float fullsize = 8.0f;
    if (GLGizmoBase::INV_ZOOM > 0) { fullsize = m_move_z_grabber.FixedGrabberSize * GLGizmoBase::INV_ZOOM; }
    GLModel &cube_z = m_move_z_grabber.get_cube();
    GLModel &cube_x = m_move_x_grabber.get_cube();
    if (is_render_z_grabber) {
        Transform3d cube_mat = Geometry::translation_transform(m_move_z_grabber.center) * m_rotate_matrix * Geometry::scale_transform(fullsize); //
        render_glmodel(cube_z, render_color, cube_mat);
    }

    if (is_render_x_grabber) {
        Transform3d cube_mat = Geometry::translation_transform(m_move_x_grabber.center) * m_rotate_matrix * Geometry::scale_transform(fullsize); //
        render_glmodel(cube_x, render_color, cube_mat);
    }
    // Should be placed at last, because GLGizmoRotate3D clears depth buffer
    GLGizmoRotate3D::set_center(m_plane_center);
    on_render_rotate_gizmos();//replace GLGizmoRotate3D::on_render();
}

void GLGizmoAdvancedCut::on_render_rotate_gizmos() {

    if (m_is_dragging) {
        if (m_hover_id == 0)
            m_gizmos[X].render();
        if ( m_hover_id == 1)
            m_gizmos[Y].render();
        if (m_hover_id == 2)
            m_gizmos[Z].render();
    }
    else {
        m_gizmos[X].render();
        m_gizmos[Y].render();
        m_gizmos[Z].render();
    }
}

void GLGizmoAdvancedCut::render_connectors()
{
    ::glEnable(GL_DEPTH_TEST);
    if (cut_line_processing() || m_cut_mode != CutMode::cutPlanar || !m_c->selection_info())
        return;
    const ModelObject *mo      = m_c->selection_info()->model_object();
    auto               inst_id = m_c->selection_info()->get_active_instance();
    if (inst_id < 0) return;

    const CutConnectors &connectors = mo->cut_connectors;
    if (connectors.size() != m_selected.size()) {
        clear_selection();
        m_selected.resize(connectors.size(), false);
    }

    const ModelInstance *mi              = mo->instances[inst_id];
    const Vec3d &        instance_offset = mi->get_offset();
    const double         sla_shift       = double(m_c->selection_info()->get_sla_shift());

    ColorRGBA  render_color    = CONNECTOR_DEF_COLOR;
    const bool looking_forward = is_looking_forward();
    for (size_t i = 0; i < connectors.size(); ++i) {
        const CutConnector &connector = connectors[i];

        float height = connector.height;
        // recalculate connector position to world position
        Vec3d pos = connector.pos + instance_offset + sla_shift * Vec3d::UnitZ();

        // First decide about the color of the point.
        assert(std::is_sorted(m_invalid_connectors_idxs.begin(), m_invalid_connectors_idxs.end()));
        const bool conflict_connector = std::binary_search(m_invalid_connectors_idxs.begin(), m_invalid_connectors_idxs.end(), i);
        if (conflict_connector) {
            render_color = CONNECTOR_ERR_COLOR;
        } else // default connector color
            render_color = connector.attribs.type == CutConnectorType::Dowel ? DOWEL_COLOR : PLAG_COLOR;

        if (!m_connectors_editing)
            render_color = CONNECTOR_ERR_COLOR;
        else if (size_t(m_hover_id - c_connectors_group_id) == i)
            render_color = conflict_connector ? HOVERED_ERR_COLOR : connector.attribs.type == CutConnectorType::Dowel ? HOVERED_DOWEL_COLOR : HOVERED_PLAG_COLOR;
        else if (m_selected[i])
            render_color = connector.attribs.type == CutConnectorType::Dowel ? SELECTED_DOWEL_COLOR : SELECTED_PLAG_COLOR;

        deal_connector_pos_by_type(pos, height, connector.attribs.type, connector.attribs.style, looking_forward, m_connectors_editing, m_clp_normal); // BBL

        Transform3d translate_tf = Transform3d::Identity();
        translate_tf.translate(pos);

        Transform3d scale_tf = Transform3d::Identity();
        scale_tf.scale(Vec3f(connector.radius, connector.radius, height).cast<double>());

        const Transform3d view_model_matrix = translate_tf * m_rotate_matrix * scale_tf;

        render_glmodel(m_shapes[connector.attribs], render_color, view_model_matrix);
    }
}

void GLGizmoAdvancedCut::render_clipper_cut()
{
    if (!m_connectors_editing)
        ::glDisable(GL_DEPTH_TEST);

    GLboolean cull_face = GL_FALSE;
    ::glGetBooleanv(GL_CULL_FACE, &cull_face);
    ::glDisable(GL_CULL_FACE);
    if (m_part_selection) {
        m_c->object_clipper()->render_cut(m_part_selection->get_ignored_contours_ptr());
    }

    if (cull_face)
        ::glEnable(GL_CULL_FACE);

    if (!m_connectors_editing)
        ::glEnable(GL_DEPTH_TEST);
}

void GLGizmoAdvancedCut::render_cut_line()
{
    if (!cut_line_processing() || m_cut_line_end.isApprox(Vec3d::Zero()))
        return;

    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glColor3f(0.0, 1.0, 0.0));
    glEnable(GL_LINE_STIPPLE);
    ::glBegin(GL_LINES);
    ::glVertex3dv(m_cut_line_begin.data());
    ::glVertex3dv(m_cut_line_end.data());
    glsafe(::glEnd());
    glDisable(GL_LINE_STIPPLE);
}

void GLGizmoAdvancedCut::clear_selection()
{
    m_selected.clear();
    m_selected_count = 0;
}

void GLGizmoAdvancedCut::init_connector_shapes()
{
    for (const CutConnectorType &type : {CutConnectorType::Snap, CutConnectorType::Dowel, CutConnectorType::Plug})
        for (const CutConnectorStyle &style : {CutConnectorStyle::Frustum, CutConnectorStyle::Prizm})
            for (const CutConnectorShape &shape : {CutConnectorShape::Circle, CutConnectorShape::Hexagon, CutConnectorShape::Square, CutConnectorShape::Triangle}) {
                CutConnectorAttributes     attribs = {type, style, shape};
                CutConnectorParas          paras   = {m_snap_space_proportion, m_snap_bulge_proportion};
                const indexed_triangle_set its     = ModelObject::get_connector_mesh(attribs, paras);
                m_shapes[attribs].init_from(its);
            }
}

void GLGizmoAdvancedCut::set_connectors_editing(bool connectors_editing)
{
    if (m_connectors_editing == connectors_editing)
        return;

    m_connectors_editing = connectors_editing;
    m_c->object_clipper()->set_behaviour(m_connectors_editing, m_connectors_editing, double(m_contour_width));
    m_parent.request_extra_frame();
    // todo: zhimin need a better method
    // after change render mode, need update for scene
    on_render();
}

void GLGizmoAdvancedCut::reset_connectors()
{
    m_c->selection_info()->model_object()->cut_connectors.clear();
    clear_selection();
    check_and_update_connectors_state();
}

void GLGizmoAdvancedCut::update_connector_shape()//update mesh
{
    CutConnectorAttributes attribs = { m_connector_type, CutConnectorStyle(m_connector_style), CutConnectorShape(m_connector_shape_id)};
    CutConnectorParas       paras  = {m_snap_space_proportion, m_snap_bulge_proportion};
    if (m_connector_type == CutConnectorType::Snap) {
        indexed_triangle_set its = ModelObject::get_connector_mesh(attribs, paras);
        m_shapes[attribs].reset();
        m_shapes[attribs].init_from(its);
    }
}

void GLGizmoAdvancedCut::apply_selected_connectors(std::function<void(size_t idx)> apply_fn)
{
    for (size_t idx = 0; idx < m_selected.size(); idx++)
        if (m_selected[idx])
            apply_fn(idx);
}

void GLGizmoAdvancedCut::select_all_connectors()
{
    std::fill(m_selected.begin(), m_selected.end(), true);
    m_selected_count = int(m_selected.size());
}

void GLGizmoAdvancedCut::unselect_all_connectors()
{
    std::fill(m_selected.begin(), m_selected.end(), false);
    m_selected_count = 0;
    validate_connector_settings();
}

void GLGizmoAdvancedCut::validate_connector_settings()
{
    if (m_connector_depth_ratio < 0.f)
        m_connector_depth_ratio = 3.f;
    if (m_connector_depth_ratio_tolerance < 0.f)
        m_connector_depth_ratio_tolerance = 0.1f;
    if (m_connector_size < 0.f)
        m_connector_size = 2.5f;
    if (m_connector_size_tolerance < 0.f)
        m_connector_size_tolerance = 0.f;

    if (m_connector_type == CutConnectorType::Undef)
        m_connector_type = CutConnectorType::Plug;
    if (m_connector_style == size_t(CutConnectorStyle::Undef))
        m_connector_style = size_t(CutConnectorStyle::Prizm);
    if (m_connector_shape_id == size_t(CutConnectorShape::Undef))
        m_connector_shape_id = size_t(CutConnectorShape::Circle);
}

bool GLGizmoAdvancedCut::add_connector(CutConnectors &connectors, const Vec2d &mouse_position)
{
    m_add_connector_ok = false;
    if (!m_connectors_editing)
        return false;

    Vec3d pos;
    Vec3d pos_world;
    if (unproject_on_cut_plane(mouse_position.cast<double>(), pos, pos_world)) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Add connector");
        unselect_all_connectors();

        connectors.emplace_back(pos, m_rotate_matrix, m_connector_size * 0.5f, m_connector_depth_ratio, m_connector_size_tolerance, m_connector_depth_ratio_tolerance,
                                CutConnectorAttributes(CutConnectorType(m_connector_type), CutConnectorStyle(m_connector_style), CutConnectorShape(m_connector_shape_id)));
        m_selected.push_back(true);
        m_selected_count = 1;
        assert(m_selected.size() == connectors.size());
        m_parent.set_as_dirty();
        check_and_update_connectors_state();
        m_add_connector_ok = true;
        return true;
    }
    return false;
}

bool GLGizmoAdvancedCut::delete_selected_connectors()
{
    CutConnectors &connectors = m_c->selection_info()->model_object()->cut_connectors;
    if (connectors.empty())
        return false;

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Delete connector");

    // remove  connectors
    for (int i = int(connectors.size()) - 1; i >= 0; i--)
        if (m_selected[i]) connectors.erase(connectors.begin() + i);
    // remove selections
    m_selected.erase(std::remove_if(m_selected.begin(), m_selected.end(),
        [](const auto &selected) {
            return selected;}),
        m_selected.end());

    m_selected_count = 0;

    assert(m_selected.size() == connectors.size());
    m_parent.set_as_dirty();
    return true;
}

bool GLGizmoAdvancedCut::is_outside_of_cut_contour(size_t idx, const CutConnectors &connectors, const Vec3d cur_pos)
{
    // check if connector pos is out of clipping plane
    if (m_c->object_clipper() && m_c->object_clipper()->is_projection_inside_cut(cur_pos) == -1) {
        m_info_stats.outside_cut_contour++;
        return true;
    }

    // check if connector bottom contour is out of clipping plane
    const CutConnector &    cur_connector = connectors[idx];
    const CutConnectorShape shape         = CutConnectorShape(cur_connector.attribs.shape);
    const int   sectorCount = shape == CutConnectorShape::Triangle  ? 3 :
                              shape == CutConnectorShape::Square    ? 4 :
                              shape == CutConnectorShape::Circle    ? 60: // supposably, 60 points are enough for conflict detection
                              shape == CutConnectorShape::Hexagon   ? 6 : 1 ;

    indexed_triangle_set mesh;
    auto &               vertices = mesh.vertices;
    vertices.reserve(sectorCount + 1);

    float fa  = 2 * PI / sectorCount;
    auto  vec = Eigen::Vector2f(0, cur_connector.radius);
    for (float angle = 0; angle < 2.f * PI; angle += fa) {
        Vec2f p = Eigen::Rotation2Df(angle) * vec;
        vertices.emplace_back(Vec3f(p(0), p(1), 0.f));
    }
    its_transform(mesh, Geometry::translation_transform(cur_pos) * m_rotate_matrix);

    for (const Vec3f &vertex : vertices) {
        if (m_c->object_clipper()) {
            int  contour_idx = m_c->object_clipper()->is_projection_inside_cut(vertex.cast<double>());
            bool is_invalid  = (contour_idx == -1);
            if (m_part_selection && m_part_selection->valid() && !is_invalid) {
                assert(contour_idx >= 0);
                const std::vector<size_t> &ignored = *(m_part_selection->get_ignored_contours_ptr());
                is_invalid                         = (std::find(ignored.begin(), ignored.end(), size_t(contour_idx)) != ignored.end());
            }
            if (is_invalid) {
                m_info_stats.outside_cut_contour++;
                return true;
            }
        }
    }

    return false;
}

bool GLGizmoAdvancedCut::is_conflict_for_connector(size_t idx, const CutConnectors &connectors, const Vec3d cur_pos)
{
    if (is_outside_of_cut_contour(idx, connectors, cur_pos))
        return true;

    const CutConnector &cur_connector = connectors[idx];

    Transform3d translate_tf = Transform3d::Identity();
    translate_tf.translate(cur_pos);
    Transform3d scale_tf = Transform3d::Identity();
    scale_tf.scale(Vec3f(cur_connector.radius, cur_connector.radius, cur_connector.height).cast<double>());
    const Transform3d   matrix  = translate_tf * m_rotate_matrix * scale_tf;

    const BoundingBoxf3 cur_tbb = m_shapes[cur_connector.attribs].get_bounding_box().transformed(matrix);

    // check if connector's bounding box is inside the object's bounding box
    if (!bounding_box().contains(cur_tbb)) {
        m_info_stats.outside_bb++;
        return true;
    }

    // check if connectors are overlapping
    for (size_t i = 0; i < connectors.size(); ++i) {
        if (i == idx) continue;
        const CutConnector &connector = connectors[i];

        if ((connector.pos - cur_connector.pos).norm() < double(connector.radius + cur_connector.radius)) {
            m_info_stats.is_overlap = true;
            return true;
        }
    }

    return false;
}

void GLGizmoAdvancedCut::switch_to_mode(CutMode new_mode) {
    m_cut_mode = new_mode;
    if (m_cut_mode == CutMode::cutTongueAndGroove) {
        m_cut_to_parts = false;//into Groove function,cancel m_cut_to_parts
    }
    if (auto oc = m_c->object_clipper()) {
        m_contour_width = m_cut_mode == CutMode::cutTongueAndGroove ? 0.f : 0.4f;
        oc->set_behaviour(m_connectors_editing, m_connectors_editing, double(m_contour_width)); // for debug
    }
    update_plane_model();
    reset_cut_by_contours();
}

void GLGizmoAdvancedCut::flip_cut_plane()
{
    m_rotate_matrix            = m_rotate_matrix * Geometry::rotation_transform(PI * Vec3d::UnitX());
    m_transformed_bounding_box = transformed_bounding_box(m_plane_center, m_rotate_matrix);

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), ("Flip cut plane"));
    m_start_dragging_m = m_rotate_matrix;

    update_clipper();
    if (m_part_selection) {
        m_part_selection->turn_over_selection();
    }
    if (m_cut_mode == CutMode::cutTongueAndGroove)
        reset_cut_by_contours();
}

void GLGizmoAdvancedCut::update_plane_model()
{
    m_plane.reset();
    init_picking_models();
}

static double get_grabber_mean_size(const BoundingBoxf3 &bb) {
    return (bb.size().x() + bb.size().y() + bb.size().z()) / 30.;
}

void GLGizmoAdvancedCut::init_picking_models()
{
    if (!m_plane.is_initialized() && !m_connectors_editing) {
        if (m_cut_mode == CutMode::cutTongueAndGroove) {
            indexed_triangle_set its = its_make_groove_plane(m_groove, m_radius, m_groove_vertices);
            m_plane.init_from(its);
            // m_plane.mesh_raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(std::move(its)));//build error
        } else if (m_cut_mode == CutMode::cutPlanar) {
            const double         cp_width = 0.02 * get_grabber_mean_size(m_bounding_box);
            indexed_triangle_set its      = its_make_frustum_dowel((double) m_cut_plane_radius_koef * m_radius, cp_width, m_cut_plane_as_circle ? 180 : 4);
            m_plane.init_from(its);
        }
    }
}

bool GLGizmoAdvancedCut::has_valid_groove() const
{
    if (m_cut_mode != CutMode::cutTongueAndGroove)
        return true;

    const float flaps_width = -2.f * m_groove.depth / tan(m_groove.flaps_angle);
    if (flaps_width > m_groove.width) return false;

    const Selection &selection = m_parent.get_selection();
    const auto &     list      = selection.get_volume_idxs();
    // is more volumes selected?
    if (list.empty())
        return false;

    const Transform3d cp_matrix = Geometry::translation_transform(m_plane_center) * m_rotate_matrix;

    for (size_t id = 0; id < m_groove_vertices.size(); id += 2) {
        const Vec3d beg = cp_matrix * m_groove_vertices[id];
        const Vec3d end = cp_matrix * m_groove_vertices[id + 1];

        bool intersection = false;
        for (const unsigned int volume_idx : list) {
            const GLVolume *glvol = selection.get_volume(volume_idx);
            if (!glvol->is_modifier && m_c->raycaster()) {
                for (size_t i = 0; i < m_c->raycaster()->raycasters().size(); i++) {
                    if (m_c->raycaster()->raycasters()[i]->intersects_line(beg, end - beg, glvol->world_matrix())) { return true; }
                }
            }
        }
        if (!intersection)
            return false;
    }
    return true;
}

bool GLGizmoAdvancedCut::has_valid_contour() const
{
    const auto clipper = m_c->object_clipper();
    return clipper && clipper->has_valid_contour();
}

void GLGizmoAdvancedCut::reset_cut_by_contours()
{
    delete_part_selection();
    m_part_selection = new PartSelection();

    if (m_cut_mode == CutMode::cutTongueAndGroove) {
        if (m_dragging || m_groove_editing || !has_valid_groove())
            return;
        process_contours();
    } else {
        process_contours();
    }
}

void GLGizmoAdvancedCut::process_contours()
{
    const Selection &      selection     = m_parent.get_selection();
    const ModelObjectPtrs &model_objects = selection.get_model()->objects;

    const int instance_idx = selection.get_instance_idx();
    if (instance_idx < 0)
        return;
    const int object_idx = selection.get_object_idx();

    wxBusyCursor wait;

    if (m_cut_mode == CutMode::cutTongueAndGroove) {
        if (has_valid_groove()) {
            Cut                    cut(model_objects[object_idx], instance_idx, get_cut_matrix(selection));
            const ModelObjectPtrs &new_objects = cut.perform_with_groove(m_groove, m_rotate_matrix, true);
            if (!new_objects.empty()) {
                delete_part_selection();
                m_part_selection = new PartSelection(new_objects.front(), instance_idx);
            }
        }
    } else {
        if (m_c->object_clipper()) {
            delete_part_selection();
            m_part_selection = new PartSelection(model_objects[object_idx], get_cut_matrix(selection), instance_idx, m_plane_center, m_plane_normal, *m_c->object_clipper());
        }
    }

    toggle_model_objects_visibility();
}

void GLGizmoAdvancedCut::toggle_model_objects_visibility(bool show_in_3d)
{
    if (m_part_selection && m_part_selection->valid() && show_in_3d == false && m_is_dragging == false) // BBL
        m_parent.toggle_model_objects_visibility(false);
    else // if (!m_part_selection.valid())
    {
        const Selection &      selection     = m_parent.get_selection();
        const ModelObjectPtrs &model_objects = selection.get_model()->objects;
        m_parent.toggle_model_objects_visibility(true, model_objects[selection.get_object_idx()], selection.get_instance_idx());
    }
}

void GLGizmoAdvancedCut::delete_part_selection()
{
    if (m_part_selection) {
        delete m_part_selection;
    }
}

void GLGizmoAdvancedCut::deal_connector_pos_by_type(
    Vec3d &pos, float &height, CutConnectorType connector_type, CutConnectorStyle connector_style, bool looking_forward, bool is_edit, const Vec3d &clp_normal)
{//deal pos and height,out :pos and height
    if (connector_type == CutConnectorType::Dowel && connector_style == CutConnectorStyle::Prizm) {
        if (is_edit) {
            height = 0.05f;
            if (!looking_forward) pos += 0.05 * m_clp_normal;
        } else {
            if (looking_forward)
                pos -= static_cast<double>(height) * m_clp_normal;
            else
                pos += static_cast<double>(height) * m_clp_normal;
            height *= 2;
        }
    } else if (!looking_forward) {
        pos += 0.05 * m_clp_normal;
    }
}

void GLGizmoAdvancedCut::update_bb()
{
    const BoundingBoxf3 box = bounding_box();
    if (!box.defined) return;
    if (!m_max_pos.isApprox(box.max) || !m_min_pos.isApprox(box.min)) {
        m_bounding_box = box;
        // check, if mode is set to Planar, when object has a connectors
        if (const int object_idx = m_parent.get_selection().get_object_idx(); object_idx >= 0 && !wxGetApp().plater()->model().objects[object_idx]->cut_connectors.empty())
            m_cut_mode = CutMode::cutPlanar;

        invalidate_cut_plane();

        m_max_pos                  = box.max;
        m_min_pos                  = box.min;
        m_bb_center                = box.center();
        m_transformed_bounding_box = transformed_bounding_box(m_bb_center);
        if (box.contains(m_center_offset))
            set_center_pos(m_bb_center + m_center_offset); // update m_plane_center
        else
            set_center_pos(m_bb_center);
        reset_cut_by_contours();

        m_contour_width = m_cut_mode == CutMode::cutTongueAndGroove ? 0.f : 0.4f;

        m_radius                 = box.radius();
        m_grabber_connection_len = 0.5 * m_radius; // std::min<double>(0.75 * m_radius, 35.0);
        m_grabber_radius         = m_grabber_connection_len * 0.85;

        //// input params for cut with tongue and groove
        m_groove.depth = m_groove.depth_init = std::max(1.f, 0.5f * float(get_grabber_mean_size(m_bounding_box)));
        m_groove.width = m_groove.width_init = 4.0f * m_groove.depth;
        m_groove.flaps_angle = m_groove.flaps_angle_init = float(PI) / 3.f;
        m_groove.angle = m_groove.angle_init = 0.f;
        m_plane.reset();

        clear_selection();
        if (CommonGizmosDataObjects::SelectionInfo *selection = m_c->selection_info(); selection && selection->model_object())
            m_selected.resize(selection->model_object()->cut_connectors.size(), false);
    }
}

void GLGizmoAdvancedCut::check_and_update_connectors_state()
{
    m_info_stats.invalidate();
    m_invalid_connectors_idxs.clear();
    if (m_cut_mode != CutMode::cutPlanar)
        return;
    if (m_c == nullptr || m_c->selection_info() == nullptr) {
        return;
    }
    const ModelObject *mo      = m_c->selection_info()->model_object();
    auto               inst_id = m_c->selection_info()->get_active_instance();
    if (inst_id < 0) return;
    const CutConnectors &connectors      = mo->cut_connectors;
    const ModelInstance *mi              = mo->instances[inst_id];
    const Vec3d &        instance_offset = mi->get_offset();
    const double         sla_shift       = double(m_c->selection_info()->get_sla_shift());

    for (size_t i = 0; i < connectors.size(); ++i) {
        const CutConnector &connector = connectors[i];
        Vec3d               pos       = connector.pos + instance_offset + sla_shift * Vec3d::UnitZ(); // recalculate connector position to world position
        if (is_conflict_for_connector(i, connectors, pos))
            m_invalid_connectors_idxs.emplace_back(i);
    }
}

void GLGizmoAdvancedCut::set_center(const Vec3d &center, bool update_tbb)
{
    set_center_pos(center, update_tbb);
    check_and_update_connectors_state();
    update_clipper();
}

bool GLGizmoAdvancedCut::set_center_pos(const Vec3d &center_pos, bool update_tbb)
{
    BoundingBoxf3 tbb = m_transformed_bounding_box;
    if (update_tbb) {
        Vec3d normal = m_rotate_matrix.inverse() * Vec3d(m_plane_center - center_pos);
        tbb.translate(normal);
    }

    bool can_set_center_pos = false;
    {
        double limit_val = 0.5;
        //&& (tbb.max.x() > m_transformed_bounding_box.min.x() && tbb.min.x() > m_transformed_bounding_box.max.x()
        if (tbb.max.z() > -limit_val && tbb.min.z() < limit_val)
            can_set_center_pos = true;
        else {
            const double old_dist = (m_bb_center - m_plane_center).norm();
            const double new_dist = (m_bb_center - center_pos).norm();
            // check if forcing is reasonable
            if (new_dist < old_dist)
                can_set_center_pos = true;
        }
    }

    if (can_set_center_pos) {
        m_transformed_bounding_box = tbb;
        m_plane_center             = center_pos;
        m_center_offset            = m_plane_center - m_bb_center;
        return true;
    }
    return false;
}

void GLGizmoAdvancedCut::invalidate_cut_plane()
{
    m_rotate_matrix = Transform3d::Identity();
    m_plane_center  = Vec3d::Zero();
    m_min_pos       = Vec3d::Zero();
    m_max_pos       = Vec3d::Zero();
    m_bb_center     = Vec3d::Zero();
    m_center_offset = Vec3d::Zero();
}

void GLGizmoAdvancedCut::rotate_vec3d_around_plane_center(Vec3d &vec, const Transform3d &rotate_matrix, const Vec3d &center) {
    vec = Geometry::Transformation(Geometry::translation_transform(center) * rotate_matrix * Geometry::translation_transform(-center)).get_matrix() * vec;
}

Transform3d GLGizmoAdvancedCut::get_cut_matrix(const Selection &selection)
{
    const int    instance_idx = selection.get_instance_idx();
    const int    object_idx   = selection.get_object_idx();
    ModelObject *mo           = selection.get_model()->objects[object_idx];
    if (!mo) return Transform3d::Identity();

    // m_cut_z is the distance from the bed. Subtract possible SLA elevation.
    const double sla_shift_z = selection.get_first_volume()->get_sla_shift_z();

    const Vec3d instance_offset   = mo->instances[instance_idx]->get_offset();
    Vec3d       cut_center_offset = m_plane_center - instance_offset;
    cut_center_offset[Z] -= sla_shift_z;

    return Geometry::translation_transform(cut_center_offset) * m_rotate_matrix;
}

bool GLGizmoAdvancedCut::render_cut_mode_combo(double label_width)
{
    ImGui::AlignTextToFramePadding();
    size_t                   selection_idx = int(m_cut_mode);
    std::vector<std::string> modes         = {_u8L("Planar"), _u8L("Dovetail")};
    bool                     is_changed    = false;
    float                    combo_width   = 220;
    ImGuiWrapper::push_combo_style(m_parent.get_scale());
    if (render_combo(_u8L("Mode"), modes, selection_idx, label_width, combo_width)) {
        is_changed = true;
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Change cut mode");
        switch_to_mode((CutMode) selection_idx);
        check_and_update_connectors_state();
    }
    ImGuiWrapper::pop_combo_style();
    return is_changed;
}

void GLGizmoAdvancedCut::render_color_marker(float size, const ColorRGBA &color)
{
    auto to_ImU32 = [](const ColorRGBA &color) -> ImU32 { return ImGui::GetColorU32({color[0], color[1], color[2], color[3]}); };
    ImGui::SameLine();
    const float radius = 0.5f * size;
    ImVec2      pos    = ImGui::GetCurrentWindow()->DC.CursorPos;
    pos.y += 1.7f * radius;
    ImGui::GetCurrentWindow()->DrawList->AddNgonFilled(pos, radius, to_ImU32(color), 6);
}

void GLGizmoAdvancedCut::render_cut_plane_input_window(float x, float y, float bottom_limit)
{
    // float unit_size = m_imgui->get_style_scaling() * 48.0f;
    float        space_size        = m_imgui->get_style_scaling() * 8;
    float        movement_cap      = m_imgui->calc_text_size(_L("Movement:")).x;
    float        rotate_cap        = m_imgui->calc_text_size(_L("Rotate")).x;
    float        caption_size      = std::max(movement_cap, rotate_cap) + 2 * space_size;
    m_imperial_units               = wxGetApp().app_config->get("use_inches") == "1";

    m_buffered_rotation = {Geometry::rad2deg(m_rotation(0)), Geometry::rad2deg(m_rotation(1)), Geometry::rad2deg(m_rotation(2))};
    char  buf[3][64];
    float buf_size[3];
    float vec_max = 0, unit_size = 0;
    for (int i = 0; i < 3; i++) {
        ImGui::DataTypeFormatString(buf[i], IM_ARRAYSIZE(buf[i]), ImGuiDataType_Double, (void *) &m_buffered_rotation[i], "%.2f");
        buf_size[i] = ImGui::CalcTextSize(buf[i]).x;
        vec_max     = std::max(buf_size[i], vec_max);
    }

    float buf_size_max = ImGui::CalcTextSize("-100.00").x;
    if (vec_max < buf_size_max) {
        unit_size = buf_size_max + ImGui::GetStyle().FramePadding.x * 2.0f;
    } else {
        unit_size = vec_max + ImGui::GetStyle().FramePadding.x * 2.0f;
    }

    CutConnectors &connectors     = m_c->selection_info()->model_object()->cut_connectors;
    const bool     has_connectors = !connectors.empty();

    m_imgui->disabled_begin(has_connectors);
    if (render_cut_mode_combo(caption_size + 1 * space_size)) {
        ;
    }
    ImGui::Separator();
    m_imgui->disabled_end();

    ImGui::PushItemWidth(caption_size);
    ImGui::Dummy(ImVec2(caption_size, -1));
    ImGui::SameLine(caption_size + 1 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::TextAlignCenter("X");
    ImGui::SameLine(caption_size + 1 * unit_size + 2 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::TextAlignCenter("Y");
    ImGui::SameLine(caption_size + 2 * unit_size + 3 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::TextAlignCenter("Z");

    ImGui::AlignTextToFramePadding();

    // Rotation input box
    ImGui::PushItemWidth(caption_size);
    m_imgui->text(_L("Rotation") + " ");
    ImGui::SameLine(caption_size + 1 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble("##cut_rotation_x", &m_buffered_rotation[0], 0.0f, 0.0f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine(caption_size + 1 * unit_size + 2 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble("##cut_rotation_y", &m_buffered_rotation[1], 0.0f, 0.0f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine(caption_size + 2 * unit_size + 3 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble("##cut_rotation_z", &m_buffered_rotation[2], 0.0f, 0.0f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue);
    if (std::abs(Geometry::rad2deg(m_rotation(0)) - m_buffered_rotation(0)) > EPSILON || std::abs(Geometry::rad2deg(m_rotation(1)) - m_buffered_rotation(1)) > EPSILON ||
        std::abs(Geometry::rad2deg(m_rotation(2)) - m_buffered_rotation(2)) > EPSILON) {
        m_rotation(0) = Geometry::deg2rad(m_buffered_rotation(0));
        m_rotation(1) = Geometry::deg2rad(m_buffered_rotation(1));
        m_rotation(2) = Geometry::deg2rad(m_buffered_rotation(2));
        update_plate_normal_boundingbox_clipper(m_rotation);
        reset_cut_by_contours();
        m_rotation.setZero();
    }

    ImGui::Separator();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 10.0f));
    // Movement input box
    m_buffered_movement = m_movement;
    ImGui::PushItemWidth(caption_size);
    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Movement") + " ");
    ImGui::SameLine(caption_size + 1 * space_size);
    ImGui::PushItemWidth(3 * unit_size + 2 * space_size);
    ImGui::BBLInputDouble("##cut_movement", &m_buffered_movement, 0.0f, 0.0f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue);
  
    if (std::abs(m_buffered_movement - m_movement) > EPSILON) {
        m_movement          = m_buffered_movement;
        // update absolute height
        update_plate_center(Axis::Z, m_movement, false);
        reset_cut_by_contours();
        m_movement = 0.0;
    }

    // height input box
    m_height          = m_plane_center.z();
    m_buffered_height = m_plane_center.z();
    ImGui::PushItemWidth(caption_size);
    ImGui::AlignTextToFramePadding();
    // only allow setting height when cut plane is horizontal
    Vec3d plane_normal = get_plane_normal();
    m_imgui->disabled_begin(std::abs(plane_normal(0)) > EPSILON || std::abs(plane_normal(1)) > EPSILON);
    m_imgui->text(_L("Height") + " ");
    ImGui::SameLine(caption_size + 1 * space_size);
    ImGui::PushItemWidth(3 * unit_size + 2 * space_size);
    ImGui::BBLInputDouble("##cut_height", &m_buffered_height, 0.0f, 0.0f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue);
    if (std::abs(m_buffered_height - m_height) > EPSILON) {
        update_plate_center(Axis::Z, m_buffered_height - m_height, false);
        reset_cut_by_contours();
    }
    ImGui::PopStyleVar(1);
    m_imgui->disabled_end();

    ImGui::Separator();
    if (m_cut_mode == CutMode::cutPlanar) {
        m_imgui->disabled_begin(!m_keep_upper || !m_keep_lower || m_cut_to_parts);
        if (m_imgui->button(has_connectors ? _L("Edit connectors") : _L("Add connectors")))
            set_connectors_editing(true);
        m_imgui->disabled_end();
    } else if (m_cut_mode == CutMode::cutTongueAndGroove) {
        m_is_slider_editing_done = false;
        m_imgui->text(_L("Groove") + ": "); // ImGuiWrapper::text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, m_labels_map["Groove"] + ": ");
        m_label_width          = 100;
        m_editing_window_width = 200;
        bool is_changed{false};
        is_changed |= render_slider_double_input(_u8L("Depth"), m_groove.depth, m_groove.depth_tolerance);
        is_changed |= render_slider_double_input(_u8L("Width"), m_groove.width, m_groove.width_tolerance);
        is_changed |= render_slider_double_input_by_format(_u8L("Flap Angle"), m_groove.flaps_angle, 30.f, 120.f, DoubleShowType::DEGREE);
        is_changed |= render_slider_double_input_by_format(_u8L("Groove Angle"), m_groove.angle, 0.f, 15.f, DoubleShowType::DEGREE);

        if (is_changed) {
            update_plane_model();
            reset_cut_by_contours();
        }

        if (m_is_slider_editing_done) {
            m_groove_editing = false;
            reset_cut_by_contours();
        }
    }
    ImGui::Separator();

    float label_width = 0;
    for (const wxString& label : {_L("Upper part"), _L("Lower part")}) {
        const float width = m_imgui->calc_text_size(label).x + m_imgui->scaled(1.5f);
        if (label_width < width)
            label_width = width;
    }

    auto render_part_action_line = [this, label_width,has_connectors](const wxString &label, const wxString &suffix, bool &keep_part, bool &place_on_cut_part, bool &rotate_part) {
        bool keep = true;
        m_imgui->disabled_begin(has_connectors || m_cut_to_parts);
        ImGui::AlignTextToFramePadding();
        m_imgui->bbl_checkbox(m_cut_to_parts ? _L("Part") : _L("Object") + label, has_connectors ? keep : keep_part);

        float marker_size = 12;
        render_color_marker(marker_size, suffix == "##upper" ? UPPER_PART_COLOR : LOWER_PART_COLOR);
        m_imgui->disabled_end();
        m_imgui->disabled_begin(!keep_part || m_cut_to_parts);
        float new_label_width = label_width + (m_cut_to_parts ? 10.0f : 20.0f);
        ImGui::SameLine(new_label_width);
        bool is_keep = !place_on_cut_part && !rotate_part;
        if (m_imgui->bbl_checkbox(_L("Keep orientation") + suffix, is_keep)){
            rotate_part       = false;
            place_on_cut_part = false;
        }
        ImGui::SameLine();
        if (m_imgui->bbl_checkbox(_L("Place on cut") + suffix, place_on_cut_part)) {
            // in bbl_checkbox have done//place_on_cut_part = !place_on_cut_part;
            rotate_part       = false;
        }
        ImGui::SameLine();
        if (m_imgui->bbl_checkbox(_L("Flip") + suffix, rotate_part)) {
            //in bbl_checkbox have done://rotate_part       = !rotate_part;
            place_on_cut_part = false;
        }
        m_imgui->disabled_end();
    };

    m_imgui->text(_L("After cut") + ": ");
    render_part_action_line( _L("A"), "##upper", m_keep_upper, m_place_on_cut_upper, m_rotate_upper);
    render_part_action_line( _L("B"), "##lower", m_keep_lower, m_place_on_cut_lower, m_rotate_lower);

    m_imgui->disabled_begin(has_connectors || m_cut_mode == CutMode::cutTongueAndGroove);
    m_imgui->bbl_checkbox(_L("Cut to parts"), m_cut_to_parts);
    if (m_cut_to_parts) {
        m_keep_upper = m_keep_lower = true;
        m_place_on_cut_upper = m_place_on_cut_lower = false;
        m_rotate_upper = m_rotate_lower = false;
    }
    m_imgui->disabled_end();

#if 0
    // Auto segment input
    ImGui::PushItemWidth(m_imgui->get_style_scaling() * 150.0);
    m_imgui->checkbox(_L("Auto Segment"), m_do_segment);
    m_imgui->disabled_begin(!m_do_segment);
    ImGui::InputDouble("smoothing_alpha", &m_segment_smoothing_alpha, 0.0f, 0.0f, "%.2f");
    m_segment_smoothing_alpha = std::max(0.1, std::min(100.0, m_segment_smoothing_alpha));
    ImGui::InputInt("segment number", &m_segment_number);
    m_segment_number = std::max(1, m_segment_number);
    m_imgui->disabled_end();

    ImGui::Separator();
#endif
    ImGui::Separator();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 10.0f));
    float get_cur_y = ImGui::GetContentRegionMax().y + ImGui::GetFrameHeight() + y;
    show_tooltip_information(x, get_cur_y);

    float f_scale = m_parent.get_gizmos_manager().get_layout_scale();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f * f_scale));

    ImGui::SameLine();
    // Cut button
    m_imgui->disabled_begin(!can_perform_cut());
    if (m_imgui->button(_L("Perform cut")))
        perform_cut(m_parent.get_selection());
    m_imgui->disabled_end();
    ImGui::SameLine();
    const bool reset_clicked = m_imgui->button(_L("Reset"));
    ImGui::PopStyleVar(2);
    if (reset_clicked) { reset_all(); }
}

void GLGizmoAdvancedCut::init_connectors_input_window_data()
{
    CutConnectors &connectors = m_c->selection_info()->model_object()->cut_connectors;

    float connectors_cap    = m_imgui->calc_text_size(_L("Connectors")).x;
    float type_cap          = m_imgui->calc_text_size(_L("Type")).x;
    float style_cap         = m_imgui->calc_text_size(_L("Style")).x;
    float shape_cap         = m_imgui->calc_text_size(_L("Shape")).x;
    float depth_ratio_cap   = m_imgui->calc_text_size(_L("Depth ratio")).x;
    float size_cap          = m_imgui->calc_text_size(_L("Size")).x;
    float max_lable_size = std::max(std::max(std::max(connectors_cap, type_cap), std::max(style_cap, shape_cap)), std::max(depth_ratio_cap, size_cap));

    m_label_width   = double(max_lable_size + 3 + ImGui::GetStyle().WindowPadding.x);
    m_control_width  = m_imgui->get_font_size() * 9.f;

    m_editing_window_width = 1.45 * m_control_width + 11;

    if (m_connectors_editing && m_selected_count > 0) {
        float             depth_ratio           {UndefFloat};
        float             depth_ratio_tolerance {UndefFloat};
        float             radius                {UndefFloat};
        float             radius_tolerance      {UndefFloat};
        CutConnectorType  type{CutConnectorType::Undef};
        CutConnectorStyle style{CutConnectorStyle::Undef};
        CutConnectorShape shape{CutConnectorShape::Undef};

        bool is_init = false;
        for (size_t idx = 0; idx < m_selected.size(); idx++)
            if (m_selected[idx]) {
                const CutConnector &connector = connectors[idx];
                if (!is_init) {
                    depth_ratio           = connector.height;
                    depth_ratio_tolerance = connector.height_tolerance;
                    radius                = connector.radius;
                    radius_tolerance      = connector.radius_tolerance;
                    type                  = connector.attribs.type;
                    style                 = connector.attribs.style;
                    shape                 = connector.attribs.shape;

                    if (m_selected_count == 1) break;
                    is_init = true;
                } else {
                    if (!is_approx(depth_ratio, connector.height))
                        depth_ratio = UndefFloat;
                    if (!is_approx(depth_ratio_tolerance, connector.height_tolerance))
                        depth_ratio_tolerance = UndefFloat;
                    if (!is_approx(radius, connector.radius))
                        radius = UndefFloat;
                    if (!is_approx(radius_tolerance, connector.radius_tolerance))
                        radius_tolerance = UndefFloat;

                    if (type != connector.attribs.type)
                        type = CutConnectorType::Undef;
                    if (style != connector.attribs.style)
                        style = CutConnectorStyle::Undef;
                    if (shape != connector.attribs.shape)
                        shape = CutConnectorShape::Undef;
                }
            }

        m_connector_depth_ratio           = depth_ratio;
        m_connector_depth_ratio_tolerance = depth_ratio_tolerance;
        m_connector_size                  = 2.f * radius;
        m_connector_size_tolerance        = radius_tolerance;
        m_connector_type                  = type;
        m_connector_style                 = size_t(style);
        m_connector_shape_id              = size_t(shape);
    }
}

void GLGizmoAdvancedCut::render_connectors_input_window(float x, float y, float bottom_limit)
{
    CutConnectors &connectors = m_c->selection_info()->model_object()->cut_connectors;

    // update when change input window
    m_imgui->set_requires_extra_frame();

    ImGui::AlignTextToFramePadding();
    m_imgui->text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, _L("Connectors"));

    m_imgui->disabled_begin(connectors.empty());
    ImGui::SameLine(m_label_width);
    if (render_reset_button("connectors", _u8L("Remove connectors")))
        reset_connectors();
    m_imgui->disabled_end();

    m_imgui->text(_L("Type"));
    ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.00f, 0.00f, 0.00f, 1.00f));
    bool type_changed = render_connect_type_radio_button(CutConnectorType::Plug);
    type_changed |= render_connect_type_radio_button(CutConnectorType::Dowel);
    type_changed |= render_connect_type_radio_button(CutConnectorType::Snap);
    if (type_changed)
        apply_selected_connectors([this, &connectors](size_t idx) { connectors[idx].attribs.type = CutConnectorType(m_connector_type); });
    ImGui::PopStyleColor(1);

    std::vector<std::string> connector_styles = {_u8L("Prizm"), _u8L("Frustum")};
    std::vector<std::string> connector_shapes = { _u8L("Triangle"), _u8L("Square"), _u8L("Hexagon"), _u8L("Circle") };

    m_imgui->disabled_begin(m_connector_type == CutConnectorType::Dowel || m_connector_type == CutConnectorType::Snap);
    if (type_changed && m_connector_type == CutConnectorType::Dowel) {
        m_connector_style = size_t(CutConnectorStyle::Prizm);
        apply_selected_connectors([this, &connectors](size_t idx) { connectors[idx].attribs.style = CutConnectorStyle(m_connector_style); });
    }

    ImGuiWrapper::push_combo_style(m_parent.get_scale());
    if (render_combo(_u8L("Style"), connector_styles, m_connector_style, m_label_width, m_editing_window_width))
        apply_selected_connectors([this, &connectors](size_t idx) { connectors[idx].attribs.style = CutConnectorStyle(m_connector_style); });
    ImGuiWrapper::pop_combo_style();
    m_imgui->disabled_end();

    m_imgui->disabled_begin(m_connector_type == CutConnectorType::Snap);
    if (type_changed && m_connector_type == CutConnectorType::Snap) {
        m_connector_shape_id = int(CutConnectorShape::Circle);
        apply_selected_connectors([this, &connectors](size_t idx) { connectors[idx].attribs.shape = CutConnectorShape(m_connector_shape_id); });
    }
    ImGuiWrapper::push_combo_style(m_parent.get_scale());
    if (render_combo(_u8L("Shape"), connector_shapes, m_connector_shape_id, m_label_width, m_editing_window_width))
        apply_selected_connectors([this, &connectors](size_t idx) { connectors[idx].attribs.shape = CutConnectorShape(m_connector_shape_id); });
    ImGuiWrapper::pop_combo_style();
    m_imgui->disabled_end();
    if (render_slider_double_input(_u8L("Depth ratio"), m_connector_depth_ratio, m_connector_depth_ratio_tolerance))
        apply_selected_connectors([this, &connectors](size_t idx) {
            if (m_connector_depth_ratio > 0)
                connectors[idx].height = m_connector_depth_ratio;
            if (m_connector_depth_ratio_tolerance >= 0)
                connectors[idx].height_tolerance = m_connector_depth_ratio_tolerance;
        });

    if (render_slider_double_input(_u8L("Size"), m_connector_size, m_connector_size_tolerance))
        apply_selected_connectors([this, &connectors](size_t idx) {
            if (m_connector_size > 0)
                connectors[idx].radius = 0.5f * m_connector_size;
            if (m_connector_size_tolerance >= 0)
                connectors[idx].radius_tolerance = m_connector_size_tolerance;
        });
    if (m_connector_type == CutConnectorType::Snap) {
        m_imgui->text(_L("Snap global parameters") +": ");
        const std::string format = "%.0f %%";
        bool is_changed = false;
        if (render_slider_double_input_by_format(_u8L("Bulge"), m_snap_bulge_proportion, 5.f, 100.f * m_snap_space_proportion, DoubleShowType::PERCENTAGE)) {
            is_changed = true;
            apply_selected_connectors([this, &connectors](size_t idx) { connectors[idx].paras.snap_bulge_proportion = m_snap_bulge_proportion; });
        }
        if (render_slider_double_input_by_format(_u8L("Gap"), m_snap_space_proportion, 10.f, 50.f, DoubleShowType::PERCENTAGE)) {
            is_changed = true;
            apply_selected_connectors([this, &connectors](size_t idx) { connectors[idx].paras.snap_space_proportion = m_snap_space_proportion; });
        }
        if (is_changed) {
            update_connector_shape();
        }
    }
    ImGui::Separator();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 10.0f));
    float get_cur_y = ImGui::GetContentRegionMax().y + ImGui::GetFrameHeight() + y;
    show_tooltip_information(x, get_cur_y);

    float f_scale = m_parent.get_gizmos_manager().get_layout_scale();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f * f_scale));

    ImGui::SameLine();
    if (m_imgui->button(_L("Confirm connectors"))) {
        unselect_all_connectors();
        set_connectors_editing(false);
    }

    ImGui::SameLine(m_label_width + m_editing_window_width - m_imgui->calc_text_size(_L("Cancel")).x - m_imgui->get_style_scaling() * 8);

    if (m_imgui->button(_L("Cancel"))) {
        reset_connectors();
        set_connectors_editing(false);
    }

    ImGui::PopStyleVar(2);
}

void GLGizmoAdvancedCut::render_input_window_warning() const
{
    if (!m_invalid_connectors_idxs.empty()) {
        wxString out = /*wxString(ImGui::WarningMarkerSmall)*/ _L("Warning") + ": " + _L("Invalid connectors detected") + ":";
        if (m_info_stats.outside_cut_contour > size_t(0))
            out += "\n - " + std::to_string(m_info_stats.outside_cut_contour) +
                   (m_info_stats.outside_cut_contour == 1 ? _L("connector is out of cut contour") : _L("connectors are out of cut contour"));
        if (m_info_stats.outside_bb > size_t(0))
            out += "\n - " + std::to_string(m_info_stats.outside_bb) +
                   (m_info_stats.outside_bb == 1 ? _L("connector is out of object") : _L("connectors is out of object"));
        if (m_info_stats.is_overlap)
            out += "\n - " + _L("Some connectors are overlapped");
        m_imgui->text(out);
    }
    if (!m_keep_upper && !m_keep_lower)
        m_imgui->text(/*wxString(ImGui::WarningMarkerSmall)*/_L("Warning") + ": " + _L("Invalid state. \nNo one part is selected for keep after cut"));
}

bool GLGizmoAdvancedCut::render_reset_button(const std::string &label_id, const std::string &tooltip) const
{
    const ImGuiStyle &style = ImGui::GetStyle();

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {1, style.ItemSpacing.y});

    ImGui::PushStyleColor(ImGuiCol_Button, {0.25f, 0.25f, 0.25f, 0.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.4f, 0.4f, 0.4f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.4f, 0.4f, 0.4f, 1.0f});

    bool revert = m_imgui->button(wxString(ImGui::RevertBtn));

    ImGui::PopStyleColor(3);

    if (ImGui::IsItemHovered())
        m_imgui->tooltip(tooltip.c_str(), ImGui::GetFontSize() * 20.0f);

    ImGui::PopStyleVar();

    return revert;
}

bool GLGizmoAdvancedCut::render_connect_type_radio_button(CutConnectorType type)
{
    ImGui::SameLine(type == CutConnectorType::Plug ? m_label_width : (type == CutConnectorType::Dowel ? 2 * m_label_width : 3 * m_label_width));
    ImGui::PushItemWidth(m_control_width);

    wxString radio_name;
    switch (type) {
    case CutConnectorType::Plug:
        radio_name = _L("Plug");
        break;
    case CutConnectorType::Dowel:
        radio_name = _L("Dowel");
        break;
    case CutConnectorType::Snap:
        radio_name = _L("Snap");
        break;
    default:
        break;
    }

    if (m_imgui->radio_button(radio_name, m_connector_type == type)) {
        m_connector_type = type;
        return true;
    }
    return false;
}

bool GLGizmoAdvancedCut::render_combo(const std::string &label, const std::vector<std::string> &lines, size_t &selection_idx, float label_width, float item_width)
{
    ImGui::AlignTextToFramePadding();
    m_imgui->text(label);
    ImGui::SameLine(label_width);
    ImGui::PushItemWidth(item_width);

    size_t selection_out = selection_idx;

    const char *selected_str = (selection_idx >= 0 && selection_idx < int(lines.size())) ? lines[selection_idx].c_str() : "";
    if (ImGui::BBLBeginCombo(("##" + label).c_str(), selected_str, 0)) {
        for (size_t line_idx = 0; line_idx < lines.size(); ++line_idx) {
            ImGui::PushID(int(line_idx));
            if (ImGui::Selectable("", line_idx == selection_idx)) selection_out = line_idx;

            ImGui::SameLine();
            ImGui::Text("%s", lines[line_idx].c_str());
            ImGui::PopID();
        }

        ImGui::EndCombo();
    }

    bool is_changed = selection_idx != selection_out;
    selection_idx   = selection_out;

    return is_changed;
}

bool GLGizmoAdvancedCut::render_slider_double_input(const std::string &label, float &value_in, float &tolerance_in)
{
    // -------- [ ] -------- [ ]
    // slider_with + item_in_gap + first_input_width + item_out_gap + slider_with + item_in_gap + second_input_width
    double slider_with          = 0.24 * m_editing_window_width; // m_control_width * 0.35;
    double item_in_gap          = 0.01 * m_editing_window_width;
    double item_out_gap         = 0.01 * m_editing_window_width;
    double first_input_width    = 0.29  * m_editing_window_width;
    double second_input_width   = 0.29  * m_editing_window_width;

    ImGui::AlignTextToFramePadding();
    m_imgui->text(label);
    ImGui::SameLine(m_label_width);
    ImGui::PushItemWidth(slider_with);

    double left_width = m_label_width + slider_with + item_in_gap;

    float value = value_in;
    if (m_imperial_units) value *= float(units_mm_to_in);
    float old_val = value;

    constexpr float UndefMinVal = -0.1f;

    const BoundingBoxf3 bbox      = bounding_box();
    float               mean_size = float((bbox.size().x() + bbox.size().y() + bbox.size().z()) / 9.0);
    float               min_size  = value_in < 0.f ? UndefMinVal : 2.f;
    if (m_imperial_units) {
        mean_size *= float(units_mm_to_in);
        min_size *= float(units_mm_to_in);
    }
    std::string format = value_in < 0.f ? " " : m_imperial_units ? "%.4f  " + _u8L("in") : "%.2f  " + _u8L("mm");

    m_imgui->bbl_slider_float_style(("##" + label).c_str(), &value, min_size, mean_size, format.c_str());

    ImGui::SameLine(left_width);
    ImGui::PushItemWidth(first_input_width);
    ImGui::BBLDragFloat(("##input_" + label).c_str(), &value, 0.05f, min_size, mean_size, format.c_str());

    value_in = value * float(m_imperial_units ? units_in_to_mm : 1.0);

    left_width += (first_input_width + item_out_gap);
    ImGui::SameLine(left_width);
    ImGui::PushItemWidth(slider_with);

    float tolerance = tolerance_in;
    if (m_imperial_units)
        tolerance *= float(units_mm_to_in);
    float old_tolerance = tolerance;
    //std::string format_t      = tolerance_in < 0.f ? " " : "%.f %%";
    float       min_tolerance = tolerance_in < 0.f ? UndefMinVal : 0.f;

    m_imgui->bbl_slider_float_style(("##tolerance_" + label).c_str(), &tolerance, min_tolerance, 2.f, format.c_str(), 1.f, true, _L("Tolerance"));

    left_width += (slider_with + item_in_gap);
    ImGui::SameLine(left_width);
    ImGui::PushItemWidth(second_input_width);
    ImGui::BBLDragFloat(("##tolerance_input_" + label).c_str(), &tolerance, 0.05f, min_tolerance, 2.f, format.c_str());

    tolerance_in = tolerance * float(m_imperial_units ? units_in_to_mm : 1.0);

    return !is_approx(old_val, value) || !is_approx(old_tolerance, tolerance);
}

bool GLGizmoAdvancedCut::render_slider_double_input_by_format(const std::string &label, float &value_in, float value_min, float value_max, DoubleShowType show_type)
{
    // slider_with + item_in_gap + first_input_width + item_out_gap
    double slider_with       = 0.24 * m_editing_window_width; // m_control_width * 0.35;
    double item_in_gap       = 0.01 * m_editing_window_width;
    double item_out_gap      = 0.01 * m_editing_window_width;
    double first_input_width = 0.29 * m_editing_window_width;

    ImGui::AlignTextToFramePadding();
    m_imgui->text(label);
    ImGui::SameLine(m_label_width);
    ImGui::PushItemWidth(slider_with);

    double      left_width = m_label_width + slider_with + item_in_gap;
    float       old_val    = value_in; // (show_type == DoubleShowType::Normal)
    float       value      = value_in; // (show_type == DoubleShowType::Normal)
    std::string format     = "%.0f";
    if (show_type == DoubleShowType::PERCENTAGE) {
        format  = "%.0f %%";
        old_val = value_in;
        value   = value_in * 100;
    } else if (show_type == DoubleShowType::DEGREE) {
        format  = "%.0f " + _u8L("°");
        old_val = value_in;
        value   = Geometry::rad2deg(value_in);
    }

    if (m_imgui->bbl_slider_float_style(("##" + label).c_str(), &value, value_min, value_max, format.c_str())) {
        if (show_type == DoubleShowType::PERCENTAGE) {
            value_in = value * 0.01f;
        } else if (show_type == DoubleShowType::DEGREE) {
            value_in = Geometry::deg2rad(value);
        } else { //(show_type == DoubleShowType::Normal)
            value_in = value;
        }
    }

    ImGui::SameLine(left_width);
    ImGui::PushItemWidth(first_input_width);
    if (ImGui::BBLDragFloat(("##input_" + label).c_str(), &value, 0.05f, value_min, value_max, format.c_str())) {
        if (show_type == DoubleShowType::PERCENTAGE) {
            value_in = value * 0.01f;
        } else if (show_type == DoubleShowType::DEGREE) {
            value_in = Geometry::deg2rad(value);
        } else { //(show_type == DoubleShowType::Normal)
            value_in = value;
        }
    }
    return !is_approx(old_val, value_in);
}

bool GLGizmoAdvancedCut::cut_line_processing() const {
    return m_cut_line_begin != Vec3d::Zero();
}

void GLGizmoAdvancedCut::discard_cut_line_processing()
{
    m_cut_line_begin = m_cut_line_end = Vec3d::Zero();
}

bool GLGizmoAdvancedCut::process_cut_line(SLAGizmoEventType action, const Vec2d &mouse_position)
{
    const Camera &camera = wxGetApp().plater()->get_camera();

    Vec3d pt;
    Vec3d dir;
    MeshRaycaster::line_from_mouse_pos_static(mouse_position, Transform3d::Identity(), camera, pt, dir);
    dir.normalize();
    pt += dir; // Move the pt along dir so it is not clipped.

    if (action == SLAGizmoEventType::LeftDown && !cut_line_processing()) {
        m_cut_line_begin = pt;
        m_cut_line_end   = pt;
        return true;
    }

    if (cut_line_processing()) {
        if (m_cut_mode == CutMode::cutTongueAndGroove)
            m_groove_editing = true;

        m_cut_line_end = pt;
        if (action == SLAGizmoEventType::LeftUp) {
            Vec3d line_dir = m_cut_line_end - m_cut_line_begin;
            if (line_dir.norm() < 3.0) {
                discard_cut_line_processing();
                return true;
            }

            Vec3d              cross_dir = line_dir.cross(dir).normalized();
            Eigen::Quaterniond q;
            Transform3d        m         = Transform3d::Identity();
            m.matrix().block(0, 0, 3, 3) = q.setFromTwoVectors(Vec3d::UnitZ(), cross_dir).toRotationMatrix();

            const Vec3d new_plane_center = m_bb_center + cross_dir * cross_dir.dot(pt - m_bb_center);
            // update transformed bb
            const auto      new_tbb         = transformed_bounding_box(new_plane_center, m);
            const GLVolume *first_volume    = m_parent.get_selection().get_first_volume();
            Vec3d           instance_offset = first_volume->get_instance_offset();
            instance_offset[Z] += first_volume->get_sla_shift_z();

            const Vec3d trans_center_pos = m.inverse() * (new_plane_center - instance_offset) + new_tbb.center();
            if (new_tbb.contains(trans_center_pos)) {
                Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Cut by line");
                m_transformed_bounding_box = new_tbb;
                set_center(new_plane_center);
                m_start_dragging_m = m_rotate_matrix = m;
                m_plane_normal                       = m_rotate_matrix * Vec3d::UnitZ();
                m_ar_plane_center                    = m_plane_center;
                reset_cut_by_contours();
            }

            discard_cut_line_processing();

            if (m_cut_mode == CutMode::cutTongueAndGroove) {
                m_groove_editing = false;
                if (new_tbb.contains(trans_center_pos)) {
                    reset_cut_by_contours();
                }
            }
        } else if (action == SLAGizmoEventType::Moving) {
            this->set_dirty();
        }
        return true;
    }
    return false;
}

PartSelection::PartSelection(
    const ModelObject *mo, const Transform3d &cut_matrix, int instance_idx_in, const Vec3d &center, const Vec3d &normal, const CommonGizmosDataObjects::ObjectClipper &oc)
    : m_instance_idx(instance_idx_in)
{
    Cut cut(mo, instance_idx_in, cut_matrix);
    add_object(cut.perform_with_plane().front());

    const ModelVolumePtrs &volumes = model_object()->volumes;

    // split to parts
    for (int id = int(volumes.size()) - 1; id >= 0; id--)
        if (volumes[id]->is_splittable()) volumes[id]->split(1);

    const Vec3d inst_offset = model_object()->instances[m_instance_idx]->get_offset();
    int         i           = 0;
    m_cut_parts.resize(volumes.size());
    for (const ModelVolume *volume : volumes) {
        assert(volume != nullptr);
        m_cut_parts[i].is_up_part = false;
        if (m_cut_parts[i].raycaster) { delete m_cut_parts[i].raycaster; }
        m_cut_parts[i].raycaster = new MeshRaycaster(volume->mesh());
        m_cut_parts[i].glmodel.reset();
        m_cut_parts[i].glmodel.init_from(volume->mesh_ptr()->its);
        m_cut_parts[i].trans = Geometry::translation_transform(inst_offset) * model_object()->volumes[i]->get_matrix();
        // Now check whether this part is below or above the plane.
        Transform3d tr   = (model_object()->instances[m_instance_idx]->get_matrix() * volume->get_matrix()).inverse();
        Vec3f       pos  = (tr * center).cast<float>();
        Vec3f       norm = (tr.linear().inverse().transpose() * normal).cast<float>();

        for (const Vec3f &v : volume->mesh().its.vertices) {
            double p = (v - pos).dot(norm);
            if (std::abs(p) > EPSILON) {
                m_cut_parts[i].is_up_part = p > 0.;
                break;
            }
        }
        i++;
    }

    // Now go through the contours and create a map from contours to parts.
    m_contour_points.clear();
    m_contour_to_parts.clear();
    m_debug_pts = std::vector<std::vector<Vec3d>>(m_cut_parts.size(), std::vector<Vec3d>());
    if (std::vector<Vec3d> pts = oc.point_per_contour(); !pts.empty()) {
        m_contour_to_parts.resize(pts.size());

        for (size_t pt_idx = 0; pt_idx < pts.size(); ++pt_idx) {
            const Vec3d &pt  = pts[pt_idx];
            const Vec3d  dir = (center - pt).dot(normal) * normal;
            m_contour_points.emplace_back(dir + pt); // the result is in world coordinates.

            // Now, cast a ray from every contour point and see which volumes of the ones above
            // the plane are hit from the inside.
            for (size_t part_id = 0; part_id < m_cut_parts.size(); ++part_id) {
                const sla::IndexedMesh &aabb = m_cut_parts[part_id].raycaster->get_aabb_mesh();
                const Transform3d &     tr   = (Geometry::translation_transform(model_object()->instances[m_instance_idx]->get_offset()) *
                                         Geometry::translation_transform(model_object()->volumes[part_id]->get_offset()))
                                            .inverse();
                for (double d : {-1., 1.}) {
                    const Vec3d dir_mesh = d * tr.linear().inverse().transpose() * normal;
                    const Vec3d src      = tr * (m_contour_points[pt_idx] + d * 0.01 * normal);
                    auto        hit      = aabb.query_ray_hit(src, dir_mesh);

                    m_debug_pts[part_id].emplace_back(src);

                    if (hit.is_inside()) {
                        // This part belongs to this point.
                        if (d == 1.)
                            m_contour_to_parts[pt_idx].first.emplace_back(part_id);
                        else
                            m_contour_to_parts[pt_idx].second.emplace_back(part_id);
                    }
                }
            }
        }
    }

    m_valid = true;
}

// In CutMode::cutTongueAndGroove we use PartSelection just for rendering
PartSelection::PartSelection(const ModelObject *object, int instance_idx_in) : m_instance_idx(instance_idx_in)
{
    add_object(object);
    const ModelVolumePtrs &volumes     = model_object()->volumes;
    const Vec3d            inst_offset = model_object()->instances[m_instance_idx]->get_offset();
    int                    i           = 0;
    m_cut_parts.resize(volumes.size());
    for (const ModelVolume *volume : volumes) {
        assert(volume != nullptr);
        if (m_cut_parts[i].raycaster) { delete m_cut_parts[i].raycaster; }
        m_cut_parts[i].raycaster = new MeshRaycaster(volume->mesh());
        m_cut_parts[i].glmodel.reset();
        m_cut_parts[i].glmodel.init_from(volume->mesh_ptr()->its);
        m_cut_parts[i].trans    = Geometry::translation_transform(inst_offset) * model_object()->volumes[i]->get_matrix();
        m_cut_parts[i].is_up_part = volume->is_from_upper();
        i++;
    }

    m_valid = true;
}

void PartSelection::part_render(const Vec3d *normal)
{
    if (!valid())
        return;

    const Camera &camera             = wxGetApp().plater()->get_camera();
    const bool    is_looking_forward = normal && camera.get_dir_forward().dot(*normal) < 0.05;

    glEnable(GL_DEPTH_TEST);
    for (size_t id = 0; id < m_cut_parts.size(); ++id) { // m_parts.size() test
        if (normal && ((is_looking_forward && m_cut_parts[id].is_up_part) || (!is_looking_forward && !m_cut_parts[id].is_up_part)))
            continue;
        GLGizmoAdvancedCut::render_glmodel(m_cut_parts[id].glmodel, m_cut_parts[id].is_up_part ? UPPER_PART_COLOR : LOWER_PART_COLOR, m_cut_parts[id].trans);
    }
}

void PartSelection::add_object(const ModelObject *object)
{
    m_model = Model();
    m_model.add_object(*object);

    const double sla_shift_z = wxGetApp().plater()->canvas3D()->get_selection().get_first_volume()->get_sla_shift_z();
    if (!is_approx(sla_shift_z, 0.)) {
        Vec3d inst_offset = model_object()->instances[m_instance_idx]->get_offset();
        inst_offset[Z] += sla_shift_z;
        model_object()->instances[m_instance_idx]->set_offset(inst_offset);
    }
}

bool PartSelection::is_one_object() const
{
    // In theory, the implementation could be just this:
    // return m_contour_to_parts.size() == m_ignored_contours.size();
    // However, this would require that the part-contour correspondence works
    // flawlessly. Because it is currently not always so for self-intersecting
    // objects, let's better check the parts itself:
    if (m_cut_parts.size() < 2) return true;
    return std::all_of(m_cut_parts.begin(), m_cut_parts.end(), [this](const PartPara &part) { return part.is_up_part == m_cut_parts.front().is_up_part; });
}

std::vector<Cut::Part> PartSelection::get_cut_parts()
{
    std::vector<Cut::Part> parts;
    for (const auto &part : m_cut_parts) parts.push_back({part.is_up_part, false});
    return parts;
}

void PartSelection::toggle_selection(const Vec2d &mouse_pos)
{
    const Camera &camera     = wxGetApp().plater()->get_camera();
    const Vec3d & camera_pos = camera.get_position();

    Vec3f pos;
    Vec3f normal;

    std::vector<std::pair<size_t, double>> hits_id_and_sqdist;

    for (size_t id = 0; id < m_cut_parts.size(); ++id) {
        //        const Vec3d volume_offset = model_object()->volumes[id]->get_offset();
        Transform3d tr = Geometry::translation_transform(model_object()->instances[m_instance_idx]->get_offset()) *
                         Geometry::translation_transform(model_object()->volumes[id]->get_offset());
        if (m_cut_parts[id].raycaster->unproject_on_mesh(mouse_pos, tr, camera, pos, normal)) {
            hits_id_and_sqdist.emplace_back(id, (camera_pos - tr * (pos.cast<double>())).squaredNorm());
        }
    }
    if (!hits_id_and_sqdist.empty()) {
        size_t id = std::min_element(hits_id_and_sqdist.begin(), hits_id_and_sqdist.end(), [](const std::pair<size_t, double> &a, const std::pair<size_t, double> &b) {
                        return a.second < b.second;
                    })->first;
        toggle_selection(id);
    }
}

void PartSelection::toggle_selection(int id)
{
    if (id >= 0) {
        m_cut_parts[id].is_up_part = !m_cut_parts[id].is_up_part;

        // And now recalculate the contours which should be ignored.
        /* m_ignored_contours.clear();
         size_t cont_id = 0;
         for (const auto &[parts_above, parts_below] : m_contour_to_parts) {
             for (size_t upper : parts_above) {
                 bool upper_sel = m_cut_parts[upper].is_up_part;
                 if (std::find_if(parts_below.begin(), parts_below.end(), [this, &upper_sel](const size_t &i) {
                     return m_cut_parts[i].is_up_part == upper_sel; }) !=
                     parts_below.end()) {
                     m_ignored_contours.emplace_back(cont_id);
                     break;
                 }
             }
             ++cont_id;
         }*/
    }
}

void PartSelection::turn_over_selection()
{
    for (PartPara &part : m_cut_parts) part.is_up_part = !part.is_up_part;
}



}} // namespace Slic3r
