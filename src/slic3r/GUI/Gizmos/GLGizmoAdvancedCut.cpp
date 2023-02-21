// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoAdvancedCut.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>
#include <wx/sizer.h>

#include <algorithm>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/AppConfig.hpp"

#include <imgui/imgui_internal.h>

namespace Slic3r {
namespace GUI {
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
    , m_last_active_id(0)
    , m_keep_upper(true)
    , m_keep_lower(true)
    , m_rotate_lower(false)
    , m_cut_to_parts(false)
    , m_do_segment(false)
    , m_segment_smoothing_alpha(0.5)
    , m_segment_number(5)
{
    for (int i = 0; i < 4; i++)
        m_cut_plane_points[i] = { 0., 0., 0. };

    set_group_id(m_gizmos.size());
    m_rotation.setZero();
    //m_current_base_rotation.setZero();
    m_rotate_cmds.clear();
    m_buffered_rotation.setZero();
}

std::string GLGizmoAdvancedCut::get_tooltip() const
{
    return "";
}

void GLGizmoAdvancedCut::update_plane_points()
{
    Vec3d plane_center = get_plane_center();

    std::array<Vec3d, 4> plane_points_rot;
    for (int i = 0; i < plane_points_rot.size(); i++) {
        plane_points_rot[i] = m_cut_plane_points[i] - plane_center;
    }

    if (m_rotation(0) > EPSILON) {
        rotate_x_3d(plane_points_rot, m_rotation(0));
        m_rotate_cmds.emplace(m_rotate_cmds.begin(), m_rotation(0), X);
    }
    if (m_rotation(1) > EPSILON) {
        rotate_y_3d(plane_points_rot, m_rotation(1));
        m_rotate_cmds.emplace(m_rotate_cmds.begin(), m_rotation(1), Y);
    }
    if (m_rotation(2) > EPSILON) {
        rotate_z_3d(plane_points_rot, m_rotation(2));
        m_rotate_cmds.emplace(m_rotate_cmds.begin(), m_rotation(2), Z);
    }

    Vec3d plane_normal = calc_plane_normal(plane_points_rot);
    if (m_movement == 0 && m_height_delta != 0)
        m_movement = plane_normal(2) * m_height_delta;// plane_normal.dot(Vec3d(0, 0, m_height_delta))
    for (int i = 0; i < plane_points_rot.size(); i++) {
        m_cut_plane_points[i] = plane_points_rot[i] + plane_center + plane_normal * m_movement;
    }

    //m_current_base_rotation += m_rotation;
    m_rotation.setZero();
    m_movement = 0.0;
    m_height_delta = 0;
}

std::array<Vec3d, 4> GLGizmoAdvancedCut::get_plane_points() const
{
    return m_cut_plane_points;
}

std::array<Vec3d, 4> GLGizmoAdvancedCut::get_plane_points_world_coord() const
{
    std::array<Vec3d, 4> plane_world_coord = m_cut_plane_points;

    const Selection& selection = m_parent.get_selection();
    const BoundingBoxf3& box = selection.get_bounding_box();
    Vec3d object_offset = box.center();

    for (Vec3d& point : plane_world_coord) {
        point += object_offset;
    }

    return plane_world_coord;
}

void GLGizmoAdvancedCut::reset_cut_plane()
{
    const Selection& selection = m_parent.get_selection();
    const BoundingBoxf3& box = selection.get_bounding_box();
    const float max_x = box.size()(0) / 2.0 + Margin;
    const float min_x = -max_x;
    const float max_y = box.size()(1) / 2.0 + Margin;
    const float min_y = -max_y;

    m_cut_plane_points[0] = { min_x, min_y, 0 };
    m_cut_plane_points[1] = { max_x, min_y, 0 };
    m_cut_plane_points[2] = { max_x, max_y, 0 };
    m_cut_plane_points[3] = { min_x, max_y, 0 };
    m_movement = 0.0;
    m_height = box.size()[2] / 2.0;
    m_height_delta = 0;
    m_rotation.setZero();
    //m_current_base_rotation.setZero();
    m_rotate_cmds.clear();

    m_buffered_movement = 0.0;
    m_buffered_height = m_height;
    m_buffered_rotation.setZero();
}

void GLGizmoAdvancedCut::reset_all()
{
    reset_cut_plane();

    m_keep_upper = true;
    m_keep_lower = true;
    m_cut_to_parts = false;
}

bool GLGizmoAdvancedCut::on_init()
{
    if (!GLGizmoRotate3D::on_init())
        return false;

    m_shortcut_key = WXK_CONTROL_C;
    return true;
}

std::string GLGizmoAdvancedCut::on_get_name() const
{
    return (_(L("Cut"))).ToUTF8().data();
}

void GLGizmoAdvancedCut::on_set_state()
{
    GLGizmoRotate3D::on_set_state();

    // Reset m_cut_z on gizmo activation
    if (get_state() == On) {
        reset_cut_plane();
    }
}

bool GLGizmoAdvancedCut::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();
    return selection.is_single_full_instance() && !selection.is_wipe_tower();
}

void GLGizmoAdvancedCut::on_start_dragging()
{
    for (auto gizmo : m_gizmos) {
        if (m_hover_id == gizmo.get_group_id()) {
            gizmo.start_dragging();
            return;
        }
    }

    if (m_hover_id != get_group_id())
        return;

    const Selection& selection = m_parent.get_selection();
    const BoundingBoxf3& box = selection.get_bounding_box();
    m_start_movement = m_movement;
    m_start_height = m_height;
    m_drag_pos = m_move_grabber.center;
}

void GLGizmoAdvancedCut::on_update(const UpdateData& data)
{
    GLGizmoRotate3D::on_update(data);

    Vec3d rotation;
    for (int i = 0; i < 3; i++)
    {
        rotation(i) = m_gizmos[i].get_angle();
        if (rotation(i) < 0)
            rotation(i) = 2*PI + rotation(i);
    }

    m_rotation = rotation;
    //m_move_grabber.angles = m_current_base_rotation + m_rotation;

    if (m_hover_id == get_group_id()) {
        double move = calc_projection(data.mouse_ray);
        set_movement(m_start_movement + move);
        Vec3d plane_normal = get_plane_normal();
        m_height = m_start_height + plane_normal(2) * move;
    }
}

void GLGizmoAdvancedCut::on_render()
{
    const Selection& selection = m_parent.get_selection();
    const BoundingBoxf3& box = selection.get_bounding_box();
    // box center is the coord of object in the world coordinate
    Vec3d object_offset = box.center();
    // plane points is in object coordinate
    Vec3d plane_center = get_plane_center();

    // rotate plane
    std::array<Vec3d, 4> plane_points_rot;
    {
        for (int i = 0; i < plane_points_rot.size(); i++) {
            plane_points_rot[i] = m_cut_plane_points[i] - plane_center;
        }

        if (m_rotation(0) > EPSILON)
            rotate_x_3d(plane_points_rot, m_rotation(0));
        if (m_rotation(1) > EPSILON)
            rotate_y_3d(plane_points_rot, m_rotation(1));
        if (m_rotation(2) > EPSILON)
            rotate_z_3d(plane_points_rot, m_rotation(2));

        for (int i = 0; i < plane_points_rot.size(); i++) {
            plane_points_rot[i] += plane_center;
        }
    }

    // move plane
    Vec3d plane_normal_rot = calc_plane_normal(plane_points_rot);
    for (int i = 0; i < plane_points_rot.size(); i++) {
        plane_points_rot[i] += plane_normal_rot * m_movement;
    }

    // transfer from object coordindate to the world coordinate
    for (Vec3d& point : plane_points_rot) {
        point += object_offset;
    }

    // draw plane
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    ::glBegin(GL_QUADS);
    ::glColor4f(0.8f, 0.8f, 0.8f, 0.5f);
    for (const Vec3d& point : plane_points_rot) {
        ::glVertex3f(point(0), point(1), point(2));
    }
    glsafe(::glEnd());

    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glDisable(GL_BLEND));

    // Draw the grabber and the connecting line
    Vec3d plane_center_rot = calc_plane_center(plane_points_rot);
    m_move_grabber.center = plane_center_rot + plane_normal_rot * Offset;
    //m_move_grabber.angles = m_current_base_rotation + m_rotation;

    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glLineWidth(m_hover_id != -1 ? 2.0f : 1.5f));
    glsafe(::glColor3f(1.0, 1.0, 0.0));
    glLineStipple(1, 0x0FFF);
    glEnable(GL_LINE_STIPPLE);
    ::glBegin(GL_LINES);
    ::glVertex3dv(plane_center_rot.data());
    ::glVertex3dv(m_move_grabber.center.data());
    glsafe(::glEnd());
    glDisable(GL_LINE_STIPPLE);

    //std::copy(std::begin(GrabberColor), std::end(GrabberColor), m_move_grabber.color);
    //m_move_grabber.color = GrabberColor;
    //m_move_grabber.hover_color = GrabberHoverColor;
    //m_move_grabber.render(m_hover_id == get_group_id(), (float)((box.size()(0) + box.size()(1) + box.size()(2)) / 3.0));
    bool hover = (m_hover_id == get_group_id());
    std::array<float, 4> render_color;
    if (hover) {
        render_color = GrabberHoverColor;
    }
    else
        render_color = GrabberColor;

    const GLModel& cube = m_move_grabber.get_cube();
    //BBS set to fixed size grabber
    //float fullsize = 2 * (dragging ? get_dragging_half_size(size) : get_half_size(size));
    float fullsize = 8.0f;
    if (GLGizmoBase::INV_ZOOM > 0) {
        fullsize = m_move_grabber.FixedGrabberSize * GLGizmoBase::INV_ZOOM;
    }

    const_cast<GLModel*>(&cube)->set_color(-1, render_color);

    glsafe(::glPushMatrix());
    glsafe(::glTranslated(m_move_grabber.center.x(), m_move_grabber.center.y(), m_move_grabber.center.z()));

    if (m_rotation(0) > EPSILON)
        glsafe(::glRotated(Geometry::rad2deg(m_rotation(0)), 1.0, 0.0, 0.0));
    if (m_rotation(1) > EPSILON)
        glsafe(::glRotated(Geometry::rad2deg(m_rotation(1)), 0.0, 1.0, 0.0));
    if (m_rotation(2) > EPSILON)
        glsafe(::glRotated(Geometry::rad2deg(m_rotation(2)), 0.0, 0.0, 1.0));
    for (int index = 0; index < m_rotate_cmds.size(); index ++)
    {
        Rotate_data& data = m_rotate_cmds[index];
        if (data.ax == X)
            glsafe(::glRotated(Geometry::rad2deg(data.angle), 1.0, 0.0, 0.0));
        else if (data.ax == Y)
            glsafe(::glRotated(Geometry::rad2deg(data.angle), 0.0, 1.0, 0.0));
        else if (data.ax == Z)
            glsafe(::glRotated(Geometry::rad2deg(data.angle), 0.0, 0.0, 1.0));
    }
    //glsafe(::glRotated(Geometry::rad2deg(angles.z()), 0.0, 0.0, 1.0));
    //glsafe(::glRotated(Geometry::rad2deg(angles.y()), 0.0, 1.0, 0.0));
    //glsafe(::glRotated(Geometry::rad2deg(angles.x()), 1.0, 0.0, 0.0));
    glsafe(::glScaled(fullsize, fullsize, fullsize));
    cube.render();
    glsafe(::glPopMatrix());

    // Should be placed at last, because GLGizmoRotate3D clears depth buffer
    GLGizmoRotate3D::on_render();
}

void GLGizmoAdvancedCut::on_render_for_picking()
{
    GLGizmoRotate3D::on_render_for_picking();

    glsafe(::glDisable(GL_DEPTH_TEST));

    BoundingBoxf3 box = m_parent.get_selection().get_bounding_box();
#if ENABLE_FIXED_GRABBER
    float mean_size = (float)(GLGizmoBase::Grabber::FixedGrabberSize);
#else
    float mean_size = (float)((box.size().x() + box.size().y() + box.size().z()) / 3.0);
#endif

    std::array<float, 4> color = picking_color_component(0);
    m_move_grabber.color[0] = color[0];
    m_move_grabber.color[1] = color[1];
    m_move_grabber.color[2] = color[2];
    m_move_grabber.color[3] = color[3];
    m_move_grabber.render_for_picking(mean_size);
}

void GLGizmoAdvancedCut::on_render_input_window(float x, float y, float bottom_limit)
{
    //float unit_size = m_imgui->get_style_scaling() * 48.0f;
    float space_size = m_imgui->get_style_scaling() * 8;
    float movement_cap = m_imgui->calc_text_size(_L("Movement:")).x;
    float rotate_cap   = m_imgui->calc_text_size(_L("Rotate")).x;
    float caption_size =  std::max(movement_cap, rotate_cap) + 2 * space_size;
    bool imperial_units = wxGetApp().app_config->get("use_inches") == "1";
    unsigned int current_active_id = ImGui::GetActiveID();

    Vec3d rotation = {Geometry::rad2deg(m_rotation(0)), Geometry::rad2deg(m_rotation(1)), Geometry::rad2deg(m_rotation(2))};
    char  buf[3][64];
    float buf_size[3];
    float vec_max = 0, unit_size = 0;
    for (int i = 0; i < 3; i++) {
        ImGui::DataTypeFormatString(buf[i], IM_ARRAYSIZE(buf[i]), ImGuiDataType_Double, (void *) &rotation[i], "%.2f");
        buf_size[i] = ImGui::CalcTextSize(buf[i]).x;
        vec_max = std::max(buf_size[i], vec_max);
    }

    float buf_size_max = ImGui::CalcTextSize("-100.00").x ;
    if (vec_max < buf_size_max){
        unit_size = buf_size_max + ImGui::GetStyle().FramePadding.x * 2.0f;
    } else {
        unit_size = vec_max + ImGui::GetStyle().FramePadding.x * 2.0f;
    }

    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);
    
    ImGuiWrapper::push_toolbar_style(m_parent.get_scale());

    GizmoImguiBegin(on_get_name(), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

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
    ImGui::BBLInputDouble("##cut_rotation_x", &rotation[0], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_size + 1 * unit_size + 2 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble("##cut_rotation_y", &rotation[1], 0.0f, 0.0f, "%.2f");
    ImGui::SameLine(caption_size + 2 * unit_size + 3 * space_size);
    ImGui::PushItemWidth(unit_size);
    ImGui::BBLInputDouble("##cut_rotation_z", &rotation[2], 0.0f, 0.0f, "%.2f");
    if (current_active_id != m_last_active_id) {
        if (std::abs(Geometry::rad2deg(m_rotation(0)) - m_buffered_rotation(0)) > EPSILON ||
            std::abs(Geometry::rad2deg(m_rotation(1)) - m_buffered_rotation(1)) > EPSILON ||
            std::abs(Geometry::rad2deg(m_rotation(2)) - m_buffered_rotation(2)) > EPSILON)
        {
            m_rotation = m_buffered_rotation;
            m_buffered_rotation.setZero();
            update_plane_points();
            m_parent.post_event(SimpleEvent(wxEVT_PAINT));
        }
    }
    else {
        m_buffered_rotation(0) = Geometry::deg2rad(rotation(0));
        m_buffered_rotation(1) = Geometry::deg2rad(rotation(1));
        m_buffered_rotation(2) = Geometry::deg2rad(rotation(2));
    }

    ImGui::Separator();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 10.0f));
    // Movement input box
    double movement = m_movement;
    ImGui::PushItemWidth(caption_size);
    ImGui::AlignTextToFramePadding();
    m_imgui->text(_L("Movement") + " ");
    ImGui::SameLine(caption_size + 1 * space_size);
    ImGui::PushItemWidth(3 * unit_size + 2 * space_size);
    ImGui::BBLInputDouble("##cut_movement", &movement, 0.0f, 0.0f, "%.2f");
    if (current_active_id != m_last_active_id) {
        if (std::abs(m_buffered_movement - m_movement) > EPSILON) {
            m_movement          = m_buffered_movement;
            m_buffered_movement = 0.0;

            // update absolute height
            Vec3d plane_normal  = get_plane_normal();
            m_height_delta = plane_normal(2) * m_movement;
            m_height += m_height_delta;
            m_buffered_height = m_height;

            update_plane_points();
            m_parent.post_event(SimpleEvent(wxEVT_PAINT));
        }
    } else {
        m_buffered_movement = movement;
    }
    
    // height input box
    double height = m_height;
    ImGui::PushItemWidth(caption_size);
    ImGui::AlignTextToFramePadding();
    // only allow setting height when cut plane is horizontal
    Vec3d plane_normal = get_plane_normal();
    m_imgui->disabled_begin(std::abs(plane_normal(0)) > EPSILON || std::abs(plane_normal(1)) > EPSILON);
    m_imgui->text(_L("Height") + " ");
    ImGui::SameLine(caption_size + 1 * space_size);
    ImGui::PushItemWidth(3 * unit_size + 2 * space_size);
    ImGui::BBLInputDouble("##cut_height", &height, 0.0f, 0.0f, "%.2f");
    if (current_active_id != m_last_active_id) {
        if (std::abs(m_buffered_height - m_height) > EPSILON) {
            m_height_delta = m_buffered_height - m_height;
            m_height = m_buffered_height;            
            update_plane_points();
            m_parent.post_event(SimpleEvent(wxEVT_PAINT));
        }
    }
    else {
        m_buffered_height = height;
    }
    ImGui::PopStyleVar(1);
    m_imgui->disabled_end();
    ImGui::Separator();
    // Part selection
    m_imgui->bbl_checkbox(_L("Keep upper part"), m_keep_upper);
    m_imgui->bbl_checkbox(_L("Keep lower part"), m_keep_lower);
    m_imgui->bbl_checkbox(_L("Cut to parts"), m_cut_to_parts);

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

    // Cut button
    m_imgui->disabled_begin((!m_keep_upper && !m_keep_lower && !m_cut_to_parts && !m_do_segment));
    const bool cut_clicked = m_imgui->button(_L("Perform cut"));
    m_imgui->disabled_end();
    ImGui::SameLine();
    const bool reset_clicked = m_imgui->button(_L("Reset"));
    if (reset_clicked) { reset_all(); }
    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();

    // Perform cut
    if (cut_clicked && (m_keep_upper || m_keep_lower || m_cut_to_parts || m_do_segment))
        perform_cut(m_parent.get_selection());

    m_last_active_id = current_active_id;
}

void GLGizmoAdvancedCut::set_movement(double movement) const
{
    m_movement = movement;
}

void GLGizmoAdvancedCut::perform_cut(const Selection& selection)
{
    const int instance_idx = selection.get_instance_idx();
    const int object_idx = selection.get_object_idx();

    wxCHECK_RET(instance_idx >= 0 && object_idx >= 0, "GLGizmoAdvancedCut: Invalid object selection");

    // m_cut_z is the distance from the bed. Subtract possible SLA elevation.
    const GLVolume* first_glvolume = selection.get_volume(*selection.get_volume_idxs().begin());

    // BBS: do segment
    if (m_do_segment)
    {
        wxGetApp().plater()->segment(object_idx, instance_idx, m_segment_smoothing_alpha, m_segment_number);
    }
    else {
        wxGetApp().plater()->cut(object_idx, instance_idx, get_plane_points_world_coord(),
            only_if(m_keep_upper, ModelObjectCutAttribute::KeepUpper) |
            only_if(m_keep_lower, ModelObjectCutAttribute::KeepLower) |
            only_if(m_cut_to_parts, ModelObjectCutAttribute::CutToParts));
    }
}

Vec3d GLGizmoAdvancedCut::calc_plane_normal(const std::array<Vec3d, 4>& plane_points) const
{
    Vec3d v01 = plane_points[1] - plane_points[0];
    Vec3d v12 = plane_points[2] - plane_points[1];

    Vec3d plane_normal = v01.cross(v12);
    plane_normal.normalize();
    return plane_normal;
}

Vec3d GLGizmoAdvancedCut::calc_plane_center(const std::array<Vec3d, 4>& plane_points) const
{
    Vec3d plane_center;
    plane_center.setZero();
    for (const Vec3d& point : plane_points)
        plane_center = plane_center + point;

    return plane_center / (float)m_cut_plane_points.size();
}

double GLGizmoAdvancedCut::calc_projection(const Linef3& mouse_ray) const
{
    Vec3d mouse_dir = mouse_ray.unit_vector();
    Vec3d inters = mouse_ray.a + (m_drag_pos - mouse_ray.a).dot(mouse_dir) / mouse_dir.squaredNorm() * mouse_dir;
    Vec3d inters_vec = inters - m_drag_pos;

    Vec3d plane_normal = get_plane_normal();
    return inters_vec.dot(plane_normal);
}

Vec3d GLGizmoAdvancedCut::get_plane_normal() const
{
    return calc_plane_normal(m_cut_plane_points);
}

Vec3d GLGizmoAdvancedCut::get_plane_center() const
{
    return calc_plane_center(m_cut_plane_points);
}

void GLGizmoAdvancedCut::finish_rotation()
{
    for (int i = 0; i < 3; i++) {
        m_gizmos[i].set_angle(0.);
    }

    update_plane_points();
}
} // namespace GUI
} // namespace Slic3r
