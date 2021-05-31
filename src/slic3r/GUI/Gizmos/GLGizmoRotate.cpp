// Include GLGizmoBase.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmoRotate.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/PresetBundle.hpp"

#include "slic3r/GUI/Jobs/RotoptimizeJob.hpp"

namespace Slic3r {
namespace GUI {


const float GLGizmoRotate::Offset = 5.0f;
const unsigned int GLGizmoRotate::CircleResolution = 64;
const unsigned int GLGizmoRotate::AngleResolution = 64;
const unsigned int GLGizmoRotate::ScaleStepsCount = 72;
const float GLGizmoRotate::ScaleStepRad = 2.0f * (float)PI / GLGizmoRotate::ScaleStepsCount;
const unsigned int GLGizmoRotate::ScaleLongEvery = 2;
const float GLGizmoRotate::ScaleLongTooth = 0.1f; // in percent of radius
const unsigned int GLGizmoRotate::SnapRegionsCount = 8;
const float GLGizmoRotate::GrabberOffset = 0.15f; // in percent of radius

GLGizmoRotate::GLGizmoRotate(GLCanvas3D& parent, GLGizmoRotate::Axis axis)
    : GLGizmoBase(parent, "", -1)
    , m_axis(axis)
    , m_angle(0.0)
    , m_center(0.0, 0.0, 0.0)
    , m_radius(0.0f)
    , m_snap_coarse_in_radius(0.0f)
    , m_snap_coarse_out_radius(0.0f)
    , m_snap_fine_in_radius(0.0f)
    , m_snap_fine_out_radius(0.0f)
{
}

GLGizmoRotate::GLGizmoRotate(const GLGizmoRotate& other)
    : GLGizmoBase(other.m_parent, other.m_icon_filename, other.m_sprite_id)
    , m_axis(other.m_axis)
    , m_angle(other.m_angle)
    , m_center(other.m_center)
    , m_radius(other.m_radius)
    , m_snap_coarse_in_radius(other.m_snap_coarse_in_radius)
    , m_snap_coarse_out_radius(other.m_snap_coarse_out_radius)
    , m_snap_fine_in_radius(other.m_snap_fine_in_radius)
    , m_snap_fine_out_radius(other.m_snap_fine_out_radius)
{
}


void GLGizmoRotate::set_angle(double angle)
{
    if (std::abs(angle - 2.0 * (double)PI) < EPSILON)
        angle = 0.0;

    m_angle = angle;
}

std::string GLGizmoRotate::get_tooltip() const
{
    std::string axis;
    switch (m_axis)
    {
    case X: { axis = "X"; break; }
    case Y: { axis = "Y"; break; }
    case Z: { axis = "Z"; break; }
    }
    return (m_hover_id == 0 || m_grabbers[0].dragging) ? axis + ": " + format((float)Geometry::rad2deg(m_angle), 4) : "";
}

bool GLGizmoRotate::on_init()
{
    m_grabbers.push_back(Grabber());
    return true;
}

void GLGizmoRotate::on_start_dragging()
{
    const BoundingBoxf3& box = m_parent.get_selection().get_bounding_box();
    m_center = box.center();
    m_radius = Offset + box.radius();
    m_snap_coarse_in_radius = m_radius / 3.0f;
    m_snap_coarse_out_radius = 2.0f * m_snap_coarse_in_radius;
    m_snap_fine_in_radius = m_radius;
    m_snap_fine_out_radius = m_snap_fine_in_radius + m_radius * ScaleLongTooth;
}

void GLGizmoRotate::on_update(const UpdateData& data)
{
    Vec2d mouse_pos = to_2d(mouse_position_in_local_plane(data.mouse_ray, m_parent.get_selection()));

    Vec2d orig_dir = Vec2d::UnitX();
    Vec2d new_dir = mouse_pos.normalized();

    double theta = ::acos(std::clamp(new_dir.dot(orig_dir), -1.0, 1.0));
    if (cross2(orig_dir, new_dir) < 0.0)
        theta = 2.0 * (double)PI - theta;

    double len = mouse_pos.norm();

    // snap to coarse snap region
    if ((m_snap_coarse_in_radius <= len) && (len <= m_snap_coarse_out_radius))
    {
        double step = 2.0 * (double)PI / (double)SnapRegionsCount;
        theta = step * (double)std::round(theta / step);
    }
    else
    {
        // snap to fine snap region (scale)
        if ((m_snap_fine_in_radius <= len) && (len <= m_snap_fine_out_radius))
        {
            double step = 2.0 * (double)PI / (double)ScaleStepsCount;
            theta = step * (double)std::round(theta / step);
        }
    }

    if (theta == 2.0 * (double)PI)
        theta = 0.0;

    m_angle = theta;
}

void GLGizmoRotate::on_render() const
{
    if (!m_grabbers[0].enabled)
        return;

    const Selection& selection = m_parent.get_selection();
    const BoundingBoxf3& box = selection.get_bounding_box();

    if (m_hover_id != 0 && !m_grabbers[0].dragging) {
        m_center = box.center();
        m_radius = Offset + box.radius();
        m_snap_coarse_in_radius = m_radius / 3.0f;
        m_snap_coarse_out_radius = 2.0f * m_snap_coarse_in_radius;
        m_snap_fine_in_radius = m_radius;
        m_snap_fine_out_radius = m_radius * (1.0f + ScaleLongTooth);
    }

    glsafe(::glEnable(GL_DEPTH_TEST));

    glsafe(::glPushMatrix());
    transform_to_local(selection);

    glsafe(::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f));
    glsafe(::glColor4fv((m_hover_id != -1) ? m_drag_color.data() : m_highlight_color.data()));

    render_circle();

    if (m_hover_id != -1) {
        render_scale();
        render_snap_radii();
        render_reference_radius();
    }

    glsafe(::glColor4fv(m_highlight_color.data()));

    if (m_hover_id != -1)
        render_angle();

    render_grabber(box);
    render_grabber_extension(box, false);

    glsafe(::glPopMatrix());
}

void GLGizmoRotate::on_render_for_picking() const
{
    const Selection& selection = m_parent.get_selection();

    glsafe(::glDisable(GL_DEPTH_TEST));

    glsafe(::glPushMatrix());

    transform_to_local(selection);

    const BoundingBoxf3& box = selection.get_bounding_box();
    render_grabbers_for_picking(box);
    render_grabber_extension(box, true);

    glsafe(::glPopMatrix());
}

void GLGizmoRotate3D::on_render_input_window(float x, float y, float bottom_limit)
{
    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA)
        return;

    RotoptimzeWindow popup{m_imgui, m_rotoptimizewin_state, {x, y, bottom_limit}};
}

void GLGizmoRotate3D::load_rotoptimize_state()
{
    std::string accuracy_str =
        wxGetApp().app_config->get("sla_auto_rotate", "accuracy");

    std::string method_str =
        wxGetApp().app_config->get("sla_auto_rotate", "method_id");

    if (!accuracy_str.empty()) {
        float accuracy = std::stof(accuracy_str);
        accuracy = std::max(0.f, std::min(accuracy, 1.f));

        m_rotoptimizewin_state.accuracy = accuracy;
    }

    if (!method_str.empty()) {
        int method_id = std::stoi(method_str);
        if (method_id < int(RotoptimizeJob::get_methods_count()))
            m_rotoptimizewin_state.method_id = method_id;
    }
}

void GLGizmoRotate::render_circle() const
{
    ::glBegin(GL_LINE_LOOP);
    for (unsigned int i = 0; i < ScaleStepsCount; ++i)
    {
        float angle = (float)i * ScaleStepRad;
        float x = ::cos(angle) * m_radius;
        float y = ::sin(angle) * m_radius;
        float z = 0.0f;
        ::glVertex3f((GLfloat)x, (GLfloat)y, (GLfloat)z);
    }
    glsafe(::glEnd());
}

void GLGizmoRotate::render_scale() const
{
    float out_radius_long = m_snap_fine_out_radius;
    float out_radius_short = m_radius * (1.0f + 0.5f * ScaleLongTooth);

    ::glBegin(GL_LINES);
    for (unsigned int i = 0; i < ScaleStepsCount; ++i)
    {
        float angle = (float)i * ScaleStepRad;
        float cosa = ::cos(angle);
        float sina = ::sin(angle);
        float in_x = cosa * m_radius;
        float in_y = sina * m_radius;
        float in_z = 0.0f;
        float out_x = (i % ScaleLongEvery == 0) ? cosa * out_radius_long : cosa * out_radius_short;
        float out_y = (i % ScaleLongEvery == 0) ? sina * out_radius_long : sina * out_radius_short;
        float out_z = 0.0f;
        ::glVertex3f((GLfloat)in_x, (GLfloat)in_y, (GLfloat)in_z);
        ::glVertex3f((GLfloat)out_x, (GLfloat)out_y, (GLfloat)out_z);
    }
    glsafe(::glEnd());
}

void GLGizmoRotate::render_snap_radii() const
{
    float step = 2.0f * (float)PI / (float)SnapRegionsCount;

    float in_radius = m_radius / 3.0f;
    float out_radius = 2.0f * in_radius;

    ::glBegin(GL_LINES);
    for (unsigned int i = 0; i < SnapRegionsCount; ++i)
    {
        float angle = (float)i * step;
        float cosa = ::cos(angle);
        float sina = ::sin(angle);
        float in_x = cosa * in_radius;
        float in_y = sina * in_radius;
        float in_z = 0.0f;
        float out_x = cosa * out_radius;
        float out_y = sina * out_radius;
        float out_z = 0.0f;
        ::glVertex3f((GLfloat)in_x, (GLfloat)in_y, (GLfloat)in_z);
        ::glVertex3f((GLfloat)out_x, (GLfloat)out_y, (GLfloat)out_z);
    }
    glsafe(::glEnd());
}

void GLGizmoRotate::render_reference_radius() const
{
    ::glBegin(GL_LINES);
    ::glVertex3f(0.0f, 0.0f, 0.0f);
    ::glVertex3f((GLfloat)(m_radius * (1.0f + GrabberOffset)), 0.0f, 0.0f);
    glsafe(::glEnd());
}

void GLGizmoRotate::render_angle() const
{
    float step_angle = (float)m_angle / AngleResolution;
    float ex_radius = m_radius * (1.0f + GrabberOffset);

    ::glBegin(GL_LINE_STRIP);
    for (unsigned int i = 0; i <= AngleResolution; ++i)
    {
        float angle = (float)i * step_angle;
        float x = ::cos(angle) * ex_radius;
        float y = ::sin(angle) * ex_radius;
        float z = 0.0f;
        ::glVertex3f((GLfloat)x, (GLfloat)y, (GLfloat)z);
    }
    glsafe(::glEnd());
}

void GLGizmoRotate::render_grabber(const BoundingBoxf3& box) const
{
    double grabber_radius = (double)m_radius * (1.0 + (double)GrabberOffset);
    m_grabbers[0].center = Vec3d(::cos(m_angle) * grabber_radius, ::sin(m_angle) * grabber_radius, 0.0);
    m_grabbers[0].angles(2) = m_angle;

    glsafe(::glColor4fv((m_hover_id != -1) ? m_drag_color.data() : m_highlight_color.data()));

    ::glBegin(GL_LINES);
    ::glVertex3f(0.0f, 0.0f, 0.0f);
    ::glVertex3dv(m_grabbers[0].center.data());
    glsafe(::glEnd());

    m_grabbers[0].color = m_highlight_color;
    render_grabbers(box);
}

void GLGizmoRotate::render_grabber_extension(const BoundingBoxf3& box, bool picking) const
{
    float mean_size = (float)((box.size()(0) + box.size()(1) + box.size()(2)) / 3.0);
    double size = m_dragging ? (double)m_grabbers[0].get_dragging_half_size(mean_size) : (double)m_grabbers[0].get_half_size(mean_size);

    std::array<float, 4> color = m_grabbers[0].color;
    if (!picking && m_hover_id != -1) {
        color[0] = 1.0f - color[0];
        color[1] = 1.0f - color[1];
        color[2] = 1.0f - color[2];
    }

    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;

    if (! picking) {
        shader->start_using();
        shader->set_uniform("emission_factor", 0.1);
#if ENABLE_SEQUENTIAL_LIMITS
        const_cast<GLModel*>(&m_cone)->set_color(-1, color);
#else
        shader->set_uniform("uniform_color", color);
#endif // ENABLE_SEQUENTIAL_LIMITS
    } else
        glsafe(::glColor4fv(color.data()));

    glsafe(::glPushMatrix());
    glsafe(::glTranslated(m_grabbers[0].center.x(), m_grabbers[0].center.y(), m_grabbers[0].center.z()));
    glsafe(::glRotated(Geometry::rad2deg(m_angle), 0.0, 0.0, 1.0));
    glsafe(::glRotated(90.0, 1.0, 0.0, 0.0));
    glsafe(::glTranslated(0.0, 0.0, 2.0 * size));
    glsafe(::glScaled(0.75 * size, 0.75 * size, 3.0 * size));
    m_cone.render();
    glsafe(::glPopMatrix());
    glsafe(::glPushMatrix());
    glsafe(::glTranslated(m_grabbers[0].center.x(), m_grabbers[0].center.y(), m_grabbers[0].center.z()));
    glsafe(::glRotated(Geometry::rad2deg(m_angle), 0.0, 0.0, 1.0));
    glsafe(::glRotated(-90.0, 1.0, 0.0, 0.0));
    glsafe(::glTranslated(0.0, 0.0, 2.0 * size));
    glsafe(::glScaled(0.75 * size, 0.75 * size, 3.0 * size));
    m_cone.render();
    glsafe(::glPopMatrix());

    if (! picking)
        shader->stop_using();
}

void GLGizmoRotate::transform_to_local(const Selection& selection) const
{
    glsafe(::glTranslated(m_center(0), m_center(1), m_center(2)));

    if (selection.is_single_volume() || selection.is_single_modifier() || selection.requires_local_axes()) {
        Transform3d orient_matrix = selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_transformation().get_matrix(true, false, true, true);
        glsafe(::glMultMatrixd(orient_matrix.data()));
    }

    switch (m_axis)
    {
    case X:
    {
        glsafe(::glRotatef(90.0f, 0.0f, 1.0f, 0.0f));
        glsafe(::glRotatef(-90.0f, 0.0f, 0.0f, 1.0f));
        break;
    }
    case Y:
    {
        glsafe(::glRotatef(-90.0f, 0.0f, 0.0f, 1.0f));
        glsafe(::glRotatef(-90.0f, 0.0f, 1.0f, 0.0f));
        break;
    }
    default:
    case Z:
    {
        // no rotation
        break;
    }
    }
}

Vec3d GLGizmoRotate::mouse_position_in_local_plane(const Linef3& mouse_ray, const Selection& selection) const
{
    double half_pi = 0.5 * (double)PI;

    Transform3d m = Transform3d::Identity();

    switch (m_axis)
    {
    case X:
    {
        m.rotate(Eigen::AngleAxisd(half_pi, Vec3d::UnitZ()));
        m.rotate(Eigen::AngleAxisd(-half_pi, Vec3d::UnitY()));
        break;
    }
    case Y:
    {
        m.rotate(Eigen::AngleAxisd(half_pi, Vec3d::UnitY()));
        m.rotate(Eigen::AngleAxisd(half_pi, Vec3d::UnitZ()));
        break;
    }
    default:
    case Z:
    {
        // no rotation applied
        break;
    }
    }

    if (selection.is_single_volume() || selection.is_single_modifier() || selection.requires_local_axes())
        m = m * selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_transformation().get_matrix(true, false, true, true).inverse();

    m.translate(-m_center);

    return transform(mouse_ray, m).intersect_plane(0.0);
}

GLGizmoRotate3D::GLGizmoRotate3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{
    m_gizmos.emplace_back(parent, GLGizmoRotate::X);
    m_gizmos.emplace_back(parent, GLGizmoRotate::Y);
    m_gizmos.emplace_back(parent, GLGizmoRotate::Z);

    for (unsigned int i = 0; i < 3; ++i) {
        m_gizmos[i].set_group_id(i);
    }

    load_rotoptimize_state();
}

bool GLGizmoRotate3D::on_init()
{
    for (GLGizmoRotate& g : m_gizmos) {
        if (!g.init())
            return false;
    }

    for (unsigned int i = 0; i < 3; ++i) {
        m_gizmos[i].set_highlight_color(AXES_COLOR[i]);
    }

    m_shortcut_key = WXK_CONTROL_R;

    return true;
}

std::string GLGizmoRotate3D::on_get_name() const
{
    return (_L("Rotate") + " [R]").ToUTF8().data();
}

bool GLGizmoRotate3D::on_is_activable() const
{
    return !m_parent.get_selection().is_empty();
}

void GLGizmoRotate3D::on_start_dragging()
{
    if ((0 <= m_hover_id) && (m_hover_id < 3))
        m_gizmos[m_hover_id].start_dragging();
}

void GLGizmoRotate3D::on_stop_dragging()
{
    if ((0 <= m_hover_id) && (m_hover_id < 3))
        m_gizmos[m_hover_id].stop_dragging();
}

void GLGizmoRotate3D::on_render() const
{
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));

    if ((m_hover_id == -1) || (m_hover_id == 0))
        m_gizmos[X].render();

    if ((m_hover_id == -1) || (m_hover_id == 1))
        m_gizmos[Y].render();

    if ((m_hover_id == -1) || (m_hover_id == 2))
        m_gizmos[Z].render();
}

const char * GLGizmoRotate3D::RotoptimzeWindow::options[RotoptimizeJob::get_methods_count()];
bool GLGizmoRotate3D::RotoptimzeWindow::options_valid = false;

GLGizmoRotate3D::RotoptimzeWindow::RotoptimzeWindow(ImGuiWrapper *   imgui,
                                                    State &          state,
                                                    const Alignment &alignment)
    : m_imgui{imgui}
{
    imgui->begin(_L("Optimize orientation"), ImGuiWindowFlags_NoMove |
                                     ImGuiWindowFlags_AlwaysAutoResize |
                                     ImGuiWindowFlags_NoCollapse);

    // adjust window position to avoid overlap the view toolbar
    float win_h = ImGui::GetWindowHeight();
    float x = alignment.x, y = alignment.y;
    y = std::min(y, alignment.bottom_limit - win_h);
    ImGui::SetWindowPos(ImVec2(x, y), ImGuiCond_Always);

    ImGui::PushItemWidth(200.f);

    size_t methods_cnt = RotoptimizeJob::get_methods_count();
    if (!options_valid) {
        for (size_t i = 0; i < methods_cnt; ++i)
            options[i] = RotoptimizeJob::get_method_names()[i].c_str();

        options_valid = true;
    }

    int citem = state.method_id;
    if (ImGui::Combo(_L("Choose goal").c_str(), &citem, options, methods_cnt) ) {
        state.method_id = citem;
        wxGetApp().app_config->set("sla_auto_rotate", "method_id", std::to_string(state.method_id));
    }

    ImGui::Separator();

    if ( imgui->button(_L("Optimize")) ) {
        wxGetApp().plater()->optimize_rotation();
    }
}

GLGizmoRotate3D::RotoptimzeWindow::~RotoptimzeWindow()
{
    m_imgui->end();
}

} // namespace GUI
} // namespace Slic3r
