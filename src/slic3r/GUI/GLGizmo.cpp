// Include GLGizmo.hpp before I18N.hpp as it includes some libigl code, which overrides our localization "L" macro.
#include "GLGizmo.hpp"

#include <GL/glew.h>
#include <Eigen/Dense>

#include "libslic3r/libslic3r.h"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/SLA/SLACommon.hpp"
#include "libslic3r/SLAPrint.hpp"

#include <cstdio>
#include <numeric>
#include <algorithm>

#include <wx/sizer.h>
#include <wx/panel.h>
#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/stattext.h>
#include <wx/debug.h>

#include "Tab.hpp"
#include "GUI.hpp"
#include "GUI_Utils.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectSettings.hpp"
#include "GUI_ObjectList.hpp"
#include "I18N.hpp"
#include "PresetBundle.hpp"

#include <wx/defs.h>

// TODO: Display tooltips quicker on Linux

static const float DEFAULT_BASE_COLOR[3] = { 0.625f, 0.625f, 0.625f };
static const float DEFAULT_DRAG_COLOR[3] = { 1.0f, 1.0f, 1.0f };
static const float DEFAULT_HIGHLIGHT_COLOR[3] = { 1.0f, 0.38f, 0.0f };

static const float AXES_COLOR[3][3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };

namespace Slic3r {
namespace GUI {

const float GLGizmoBase::Grabber::SizeFactor = 0.025f;
const float GLGizmoBase::Grabber::MinHalfSize = 1.5f;
const float GLGizmoBase::Grabber::DraggingScaleFactor = 1.25f;

GLGizmoBase::Grabber::Grabber()
    : center(Vec3d::Zero())
    , angles(Vec3d::Zero())
    , dragging(false)
    , enabled(true)
{
    color[0] = 1.0f;
    color[1] = 1.0f;
    color[2] = 1.0f;
}

void GLGizmoBase::Grabber::render(bool hover, float size) const
{
    float render_color[3];
    if (hover)
    {
        render_color[0] = 1.0f - color[0];
        render_color[1] = 1.0f - color[1];
        render_color[2] = 1.0f - color[2];
    }
    else
        ::memcpy((void*)render_color, (const void*)color, 3 * sizeof(float));

    render(size, render_color, true);
}

float GLGizmoBase::Grabber::get_half_size(float size) const
{
    return std::max(size * SizeFactor, MinHalfSize);
}

float GLGizmoBase::Grabber::get_dragging_half_size(float size) const
{
    return std::max(size * SizeFactor * DraggingScaleFactor, MinHalfSize);
}

void GLGizmoBase::Grabber::render(float size, const float* render_color, bool use_lighting) const
{
    float half_size = dragging ? get_dragging_half_size(size) : get_half_size(size);

    if (use_lighting)
        ::glEnable(GL_LIGHTING);

    ::glColor3fv(render_color);

    ::glPushMatrix();
    ::glTranslated(center(0), center(1), center(2));

    ::glRotated(Geometry::rad2deg(angles(2)), 0.0, 0.0, 1.0);
    ::glRotated(Geometry::rad2deg(angles(1)), 0.0, 1.0, 0.0);
    ::glRotated(Geometry::rad2deg(angles(0)), 1.0, 0.0, 0.0);

    // face min x
    ::glPushMatrix();
    ::glTranslatef(-(GLfloat)half_size, 0.0f, 0.0f);
    ::glRotatef(-90.0f, 0.0f, 1.0f, 0.0f);
    render_face(half_size);
    ::glPopMatrix();

    // face max x
    ::glPushMatrix();
    ::glTranslatef((GLfloat)half_size, 0.0f, 0.0f);
    ::glRotatef(90.0f, 0.0f, 1.0f, 0.0f);
    render_face(half_size);
    ::glPopMatrix();

    // face min y
    ::glPushMatrix();
    ::glTranslatef(0.0f, -(GLfloat)half_size, 0.0f);
    ::glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
    render_face(half_size);
    ::glPopMatrix();

    // face max y
    ::glPushMatrix();
    ::glTranslatef(0.0f, (GLfloat)half_size, 0.0f);
    ::glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
    render_face(half_size);
    ::glPopMatrix();

    // face min z
    ::glPushMatrix();
    ::glTranslatef(0.0f, 0.0f, -(GLfloat)half_size);
    ::glRotatef(180.0f, 1.0f, 0.0f, 0.0f);
    render_face(half_size);
    ::glPopMatrix();

    // face max z
    ::glPushMatrix();
    ::glTranslatef(0.0f, 0.0f, (GLfloat)half_size);
    render_face(half_size);
    ::glPopMatrix();

    ::glPopMatrix();

    if (use_lighting)
        ::glDisable(GL_LIGHTING);
}

void GLGizmoBase::Grabber::render_face(float half_size) const
{
    ::glBegin(GL_TRIANGLES);
    ::glNormal3f(0.0f, 0.0f, 1.0f);
    ::glVertex3f(-(GLfloat)half_size, -(GLfloat)half_size, 0.0f);
    ::glVertex3f((GLfloat)half_size, -(GLfloat)half_size, 0.0f);
    ::glVertex3f((GLfloat)half_size, (GLfloat)half_size, 0.0f);
    ::glVertex3f((GLfloat)half_size, (GLfloat)half_size, 0.0f);
    ::glVertex3f(-(GLfloat)half_size, (GLfloat)half_size, 0.0f);
    ::glVertex3f(-(GLfloat)half_size, -(GLfloat)half_size, 0.0f);
    ::glEnd();
}

#if ENABLE_SVG_ICONS
GLGizmoBase::GLGizmoBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
#else
GLGizmoBase::GLGizmoBase(GLCanvas3D& parent, unsigned int sprite_id)
#endif // ENABLE_SVG_ICONS
    : m_parent(parent)
    , m_group_id(-1)
    , m_state(Off)
    , m_shortcut_key(0)
#if ENABLE_SVG_ICONS
    , m_icon_filename(icon_filename)
#endif // ENABLE_SVG_ICONS
    , m_sprite_id(sprite_id)
    , m_hover_id(-1)
    , m_dragging(false)
#if ENABLE_IMGUI
    , m_imgui(wxGetApp().imgui())
#endif // ENABLE_IMGUI
{
    ::memcpy((void*)m_base_color, (const void*)DEFAULT_BASE_COLOR, 3 * sizeof(float));
    ::memcpy((void*)m_drag_color, (const void*)DEFAULT_DRAG_COLOR, 3 * sizeof(float));
    ::memcpy((void*)m_highlight_color, (const void*)DEFAULT_HIGHLIGHT_COLOR, 3 * sizeof(float));
}

void GLGizmoBase::set_hover_id(int id)
{
    if (m_grabbers.empty() || (id < (int)m_grabbers.size()))
    {
        m_hover_id = id;
        on_set_hover_id();
    }
}

void GLGizmoBase::set_highlight_color(const float* color)
{
    if (color != nullptr)
        ::memcpy((void*)m_highlight_color, (const void*)color, 3 * sizeof(float));
}

void GLGizmoBase::enable_grabber(unsigned int id)
{
    if ((0 <= id) && (id < (unsigned int)m_grabbers.size()))
        m_grabbers[id].enabled = true;

    on_enable_grabber(id);
}

void GLGizmoBase::disable_grabber(unsigned int id)
{
    if ((0 <= id) && (id < (unsigned int)m_grabbers.size()))
        m_grabbers[id].enabled = false;

    on_disable_grabber(id);
}

void GLGizmoBase::start_dragging(const GLCanvas3D::Selection& selection)
{
    m_dragging = true;

    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        m_grabbers[i].dragging = (m_hover_id == i);
    }

    on_start_dragging(selection);
}

void GLGizmoBase::stop_dragging()
{
    m_dragging = false;

    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        m_grabbers[i].dragging = false;
    }

    on_stop_dragging();
}

void GLGizmoBase::update(const UpdateData& data, const GLCanvas3D::Selection& selection)
{
    if (m_hover_id != -1)
        on_update(data, selection);
}

float GLGizmoBase::picking_color_component(unsigned int id) const
{
    int color = 254 - (int)id;
    if (m_group_id > -1)
        color -= m_group_id;

    return (float)color / 255.0f;
}

void GLGizmoBase::render_grabbers(const BoundingBoxf3& box) const
{
    float size = (float)box.max_size();

    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        if (m_grabbers[i].enabled)
            m_grabbers[i].render((m_hover_id == i), size);
    }
}

void GLGizmoBase::render_grabbers(float size) const
{
    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        if (m_grabbers[i].enabled)
            m_grabbers[i].render((m_hover_id == i), size);
    }
}

void GLGizmoBase::render_grabbers_for_picking(const BoundingBoxf3& box) const
{
    float size = (float)box.max_size();

    for (unsigned int i = 0; i < (unsigned int)m_grabbers.size(); ++i)
    {
        if (m_grabbers[i].enabled)
        {
            m_grabbers[i].color[0] = 1.0f;
            m_grabbers[i].color[1] = 1.0f;
            m_grabbers[i].color[2] = picking_color_component(i);
            m_grabbers[i].render_for_picking(size);
        }
    }
}

#if !ENABLE_IMGUI
void GLGizmoBase::create_external_gizmo_widgets(wxWindow *parent) {}
#endif // not ENABLE_IMGUI

void GLGizmoBase::set_tooltip(const std::string& tooltip) const
{
    m_parent.set_tooltip(tooltip);
}

std::string GLGizmoBase::format(float value, unsigned int decimals) const
{
    return Slic3r::string_printf("%.*f", decimals, value);
}

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
#if ENABLE_SVG_ICONS
    : GLGizmoBase(parent, "", -1)
#else
    : GLGizmoBase(parent, -1)
#endif // ENABLE_SVG_ICONS
    , m_axis(axis)
    , m_angle(0.0)
    , m_quadric(nullptr)
    , m_center(0.0, 0.0, 0.0)
    , m_radius(0.0f)
    , m_snap_coarse_in_radius(0.0f)
    , m_snap_coarse_out_radius(0.0f)
    , m_snap_fine_in_radius(0.0f)
    , m_snap_fine_out_radius(0.0f)
{
    m_quadric = ::gluNewQuadric();
    if (m_quadric != nullptr)
        ::gluQuadricDrawStyle(m_quadric, GLU_FILL);
}

GLGizmoRotate::GLGizmoRotate(const GLGizmoRotate& other)
#if ENABLE_SVG_ICONS
    : GLGizmoBase(other.m_parent, other.m_icon_filename, other.m_sprite_id)
#else
    : GLGizmoBase(other.m_parent, other.m_sprite_id)
#endif // ENABLE_SVG_ICONS
    , m_axis(other.m_axis)
    , m_angle(other.m_angle)
    , m_quadric(nullptr)
    , m_center(other.m_center)
    , m_radius(other.m_radius)
    , m_snap_coarse_in_radius(other.m_snap_coarse_in_radius)
    , m_snap_coarse_out_radius(other.m_snap_coarse_out_radius)
    , m_snap_fine_in_radius(other.m_snap_fine_in_radius)
    , m_snap_fine_out_radius(other.m_snap_fine_out_radius)
{
    m_quadric = ::gluNewQuadric();
    if (m_quadric != nullptr)
        ::gluQuadricDrawStyle(m_quadric, GLU_FILL);
}

GLGizmoRotate::~GLGizmoRotate()
{
    if (m_quadric != nullptr)
        ::gluDeleteQuadric(m_quadric);
}

void GLGizmoRotate::set_angle(double angle)
{
    if (std::abs(angle - 2.0 * (double)PI) < EPSILON)
        angle = 0.0;

    m_angle = angle;
}

bool GLGizmoRotate::on_init()
{
    m_grabbers.push_back(Grabber());
    return true;
}

void GLGizmoRotate::on_start_dragging(const GLCanvas3D::Selection& selection)
{
    const BoundingBoxf3& box = selection.get_bounding_box();
    m_center = box.center();
    m_radius = Offset + box.radius();
    m_snap_coarse_in_radius = m_radius / 3.0f;
    m_snap_coarse_out_radius = 2.0f * m_snap_coarse_in_radius;
    m_snap_fine_in_radius = m_radius;
    m_snap_fine_out_radius = m_snap_fine_in_radius + m_radius * ScaleLongTooth;
}

void GLGizmoRotate::on_update(const UpdateData& data, const GLCanvas3D::Selection& selection)
{
    Vec2d mouse_pos = to_2d(mouse_position_in_local_plane(data.mouse_ray, selection));

    Vec2d orig_dir = Vec2d::UnitX();
    Vec2d new_dir = mouse_pos.normalized();

    double theta = ::acos(clamp(-1.0, 1.0, new_dir.dot(orig_dir)));
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

void GLGizmoRotate::on_render(const GLCanvas3D::Selection& selection) const
{
    if (!m_grabbers[0].enabled)
        return;

    const BoundingBoxf3& box = selection.get_bounding_box();

    std::string axis;
    switch (m_axis)
    {
    case X: { axis = "X"; break; }
    case Y: { axis = "Y"; break; }
    case Z: { axis = "Z"; break; }
    }

    if (!m_dragging && (m_hover_id == 0))
        set_tooltip(axis);
    else if (m_dragging)
        set_tooltip(axis + ": " + format((float)Geometry::rad2deg(m_angle), 4) + "\u00B0");
    else
    {
        m_center = box.center();
        m_radius = Offset + box.radius();
        m_snap_coarse_in_radius = m_radius / 3.0f;
        m_snap_coarse_out_radius = 2.0f * m_snap_coarse_in_radius;
        m_snap_fine_in_radius = m_radius;
        m_snap_fine_out_radius = m_radius * (1.0f + ScaleLongTooth);
    }

    ::glEnable(GL_DEPTH_TEST);

    ::glPushMatrix();
    transform_to_local(selection);

    ::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f);
    ::glColor3fv((m_hover_id != -1) ? m_drag_color : m_highlight_color);

    render_circle();

    if (m_hover_id != -1)
    {
        render_scale();
        render_snap_radii();
        render_reference_radius();
    }

    ::glColor3fv(m_highlight_color);

    if (m_hover_id != -1)
        render_angle();

    render_grabber(box);
    render_grabber_extension(box, false);

    ::glPopMatrix();
}

void GLGizmoRotate::on_render_for_picking(const GLCanvas3D::Selection& selection) const
{
    ::glDisable(GL_DEPTH_TEST);

    ::glPushMatrix();

    transform_to_local(selection);

    const BoundingBoxf3& box = selection.get_bounding_box();
    render_grabbers_for_picking(box);
    render_grabber_extension(box, true);

    ::glPopMatrix();
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
    ::glEnd();
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
    ::glEnd();
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
    ::glEnd();
}

void GLGizmoRotate::render_reference_radius() const
{
    ::glBegin(GL_LINES);
    ::glVertex3f(0.0f, 0.0f, 0.0f);
    ::glVertex3f((GLfloat)(m_radius * (1.0f + GrabberOffset)), 0.0f, 0.0f);
    ::glEnd();
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
    ::glEnd();
}

void GLGizmoRotate::render_grabber(const BoundingBoxf3& box) const
{
    double grabber_radius = (double)m_radius * (1.0 + (double)GrabberOffset);
    m_grabbers[0].center = Vec3d(::cos(m_angle) * grabber_radius, ::sin(m_angle) * grabber_radius, 0.0);
    m_grabbers[0].angles(2) = m_angle;

    ::glColor3fv((m_hover_id != -1) ? m_drag_color : m_highlight_color);

    ::glBegin(GL_LINES);
    ::glVertex3f(0.0f, 0.0f, 0.0f);
    ::glVertex3dv(m_grabbers[0].center.data());
    ::glEnd();

    ::memcpy((void*)m_grabbers[0].color, (const void*)m_highlight_color, 3 * sizeof(float));
    render_grabbers(box);
}

void GLGizmoRotate::render_grabber_extension(const BoundingBoxf3& box, bool picking) const
{
    if (m_quadric == nullptr)
        return;

    double size = m_dragging ? (double)m_grabbers[0].get_dragging_half_size((float)box.max_size()) : (double)m_grabbers[0].get_half_size((float)box.max_size());

    float color[3];
    ::memcpy((void*)color, (const void*)m_grabbers[0].color, 3 * sizeof(float));
    if (!picking && (m_hover_id != -1))
    {
        color[0] = 1.0f - color[0];
        color[1] = 1.0f - color[1];
        color[2] = 1.0f - color[2];
    }

    if (!picking)
        ::glEnable(GL_LIGHTING);

    ::glColor3fv(color);
    ::glPushMatrix();
    ::glTranslated(m_grabbers[0].center(0), m_grabbers[0].center(1), m_grabbers[0].center(2));
    ::glRotated(Geometry::rad2deg(m_angle), 0.0, 0.0, 1.0);
    ::glRotated(90.0, 1.0, 0.0, 0.0);
    ::glTranslated(0.0, 0.0, 2.0 * size);
    ::gluQuadricOrientation(m_quadric, GLU_OUTSIDE);
    ::gluCylinder(m_quadric, 0.75 * size, 0.0, 3.0 * size, 36, 1);
    ::gluQuadricOrientation(m_quadric, GLU_INSIDE);
    ::gluDisk(m_quadric, 0.0, 0.75 * size, 36, 1);
    ::glPopMatrix();
    ::glPushMatrix();
    ::glTranslated(m_grabbers[0].center(0), m_grabbers[0].center(1), m_grabbers[0].center(2));
    ::glRotated(Geometry::rad2deg(m_angle), 0.0, 0.0, 1.0);
    ::glRotated(-90.0, 1.0, 0.0, 0.0);
    ::glTranslated(0.0, 0.0, 2.0 * size);
    ::gluQuadricOrientation(m_quadric, GLU_OUTSIDE);
    ::gluCylinder(m_quadric, 0.75 * size, 0.0, 3.0 * size, 36, 1);
    ::gluQuadricOrientation(m_quadric, GLU_INSIDE);
    ::gluDisk(m_quadric, 0.0, 0.75 * size, 36, 1);
    ::glPopMatrix();

    if (!picking)
        ::glDisable(GL_LIGHTING);
}

void GLGizmoRotate::transform_to_local(const GLCanvas3D::Selection& selection) const
{
    ::glTranslated(m_center(0), m_center(1), m_center(2));

    if (selection.is_single_volume() || selection.is_single_modifier())
    {
        Transform3d orient_matrix = selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_transformation().get_matrix(true, false, true, true);
        ::glMultMatrixd(orient_matrix.data());
    }

    switch (m_axis)
    {
    case X:
    {
        ::glRotatef(90.0f, 0.0f, 1.0f, 0.0f);
        ::glRotatef(-90.0f, 0.0f, 0.0f, 1.0f);
        break;
    }
    case Y:
    {
        ::glRotatef(-90.0f, 0.0f, 0.0f, 1.0f);
        ::glRotatef(-90.0f, 0.0f, 1.0f, 0.0f);
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

Vec3d GLGizmoRotate::mouse_position_in_local_plane(const Linef3& mouse_ray, const GLCanvas3D::Selection& selection) const
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

    if (selection.is_single_volume() || selection.is_single_modifier())
        m = m * selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_transformation().get_matrix(true, false, true, true).inverse();

    m.translate(-m_center);

    return transform(mouse_ray, m).intersect_plane(0.0);
}

#if ENABLE_SVG_ICONS
GLGizmoRotate3D::GLGizmoRotate3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
#else
GLGizmoRotate3D::GLGizmoRotate3D(GLCanvas3D& parent, unsigned int sprite_id)
    : GLGizmoBase(parent, sprite_id)
#endif // ENABLE_SVG_ICONS
{
    m_gizmos.emplace_back(parent, GLGizmoRotate::X);
    m_gizmos.emplace_back(parent, GLGizmoRotate::Y);
    m_gizmos.emplace_back(parent, GLGizmoRotate::Z);

    for (unsigned int i = 0; i < 3; ++i)
    {
        m_gizmos[i].set_group_id(i);
    }
}

bool GLGizmoRotate3D::on_init()
{
    for (GLGizmoRotate& g : m_gizmos)
    {
        if (!g.init())
            return false;
    }

    for (unsigned int i = 0; i < 3; ++i)
    {
        m_gizmos[i].set_highlight_color(AXES_COLOR[i]);
    }

    m_shortcut_key = WXK_CONTROL_R;

    return true;
}

std::string GLGizmoRotate3D::on_get_name() const
{    
    return L("Rotate [R]");
}

void GLGizmoRotate3D::on_start_dragging(const GLCanvas3D::Selection& selection)
{
    if ((0 <= m_hover_id) && (m_hover_id < 3))
        m_gizmos[m_hover_id].start_dragging(selection);
}

void GLGizmoRotate3D::on_stop_dragging()
{
    if ((0 <= m_hover_id) && (m_hover_id < 3))
        m_gizmos[m_hover_id].stop_dragging();
}

void GLGizmoRotate3D::on_render(const GLCanvas3D::Selection& selection) const
{
    ::glClear(GL_DEPTH_BUFFER_BIT);

    if ((m_hover_id == -1) || (m_hover_id == 0))
        m_gizmos[X].render(selection);

    if ((m_hover_id == -1) || (m_hover_id == 1))
        m_gizmos[Y].render(selection);

    if ((m_hover_id == -1) || (m_hover_id == 2))
        m_gizmos[Z].render(selection);
}

#if ENABLE_IMGUI
void GLGizmoRotate3D::on_render_input_window(float x, float y, float bottom_limit, const GLCanvas3D::Selection& selection)
{
#if !DISABLE_MOVE_ROTATE_SCALE_GIZMOS_IMGUI
    Vec3d rotation(Geometry::rad2deg(m_gizmos[0].get_angle()), Geometry::rad2deg(m_gizmos[1].get_angle()), Geometry::rad2deg(m_gizmos[2].get_angle()));
    wxString label = _(L("Rotation (deg)"));

    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);
    m_imgui->set_next_window_bg_alpha(0.5f);
    m_imgui->begin(label, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    m_imgui->input_vec3("", rotation, 100.0f, "%.2f");
    m_imgui->end();
#endif // !DISABLE_MOVE_ROTATE_SCALE_GIZMOS_IMGUI
}
#endif // ENABLE_IMGUI

const float GLGizmoScale3D::Offset = 5.0f;

#if ENABLE_SVG_ICONS
GLGizmoScale3D::GLGizmoScale3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
#else
GLGizmoScale3D::GLGizmoScale3D(GLCanvas3D& parent, unsigned int sprite_id)
    : GLGizmoBase(parent, sprite_id)
#endif // ENABLE_SVG_ICONS
    , m_scale(Vec3d::Ones())
    , m_snap_step(0.05)
    , m_starting_scale(Vec3d::Ones())
{
}

bool GLGizmoScale3D::on_init()
{
    for (int i = 0; i < 10; ++i)
    {
        m_grabbers.push_back(Grabber());
    }

    double half_pi = 0.5 * (double)PI;

    // x axis
    m_grabbers[0].angles(1) = half_pi;
    m_grabbers[1].angles(1) = half_pi;

    // y axis
    m_grabbers[2].angles(0) = half_pi;
    m_grabbers[3].angles(0) = half_pi;

    m_shortcut_key = WXK_CONTROL_S;

    return true;
}

std::string GLGizmoScale3D::on_get_name() const
{
    return L("Scale [S]");
}

void GLGizmoScale3D::on_start_dragging(const GLCanvas3D::Selection& selection)
{
    if (m_hover_id != -1)
    {
        m_starting_drag_position = m_grabbers[m_hover_id].center;
        m_starting_box = selection.get_bounding_box();
    }
}

void GLGizmoScale3D::on_update(const UpdateData& data, const GLCanvas3D::Selection& selection)
{
    if ((m_hover_id == 0) || (m_hover_id == 1))
        do_scale_x(data);
    else if ((m_hover_id == 2) || (m_hover_id == 3))
        do_scale_y(data);
    else if ((m_hover_id == 4) || (m_hover_id == 5))
        do_scale_z(data);
    else if (m_hover_id >= 6)
        do_scale_uniform(data);
}

void GLGizmoScale3D::on_render(const GLCanvas3D::Selection& selection) const
{
    bool single_instance = selection.is_single_full_instance();
    bool single_volume = selection.is_single_modifier() || selection.is_single_volume();
    bool single_selection = single_instance || single_volume;

    Vec3f scale = 100.0f * Vec3f::Ones();
    if (single_instance)
        scale = 100.0f * selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_scaling_factor().cast<float>();
    else if (single_volume)
        scale = 100.0f * selection.get_volume(*selection.get_volume_idxs().begin())->get_volume_scaling_factor().cast<float>();

    if ((single_selection && ((m_hover_id == 0) || (m_hover_id == 1))) || m_grabbers[0].dragging || m_grabbers[1].dragging)
        set_tooltip("X: " + format(scale(0), 4) + "%");
    else if (!m_grabbers[0].dragging && !m_grabbers[1].dragging && ((m_hover_id == 0) || (m_hover_id == 1)))
        set_tooltip("X");
    else if ((single_selection && ((m_hover_id == 2) || (m_hover_id == 3))) || m_grabbers[2].dragging || m_grabbers[3].dragging)
        set_tooltip("Y: " + format(scale(1), 4) + "%");
    else if (!m_grabbers[2].dragging && !m_grabbers[3].dragging && ((m_hover_id == 2) || (m_hover_id == 3)))
        set_tooltip("Y");
    else if ((single_selection && ((m_hover_id == 4) || (m_hover_id == 5))) || m_grabbers[4].dragging || m_grabbers[5].dragging)
        set_tooltip("Z: " + format(scale(2), 4) + "%");
    else if (!m_grabbers[4].dragging && !m_grabbers[5].dragging && ((m_hover_id == 4) || (m_hover_id == 5)))
        set_tooltip("Z");
    else if ((single_selection && ((m_hover_id == 6) || (m_hover_id == 7) || (m_hover_id == 8) || (m_hover_id == 9)))
        || m_grabbers[6].dragging || m_grabbers[7].dragging || m_grabbers[8].dragging || m_grabbers[9].dragging)
    {
        std::string tooltip = "X: " + format(scale(0), 4) + "%\n";
        tooltip += "Y: " + format(scale(1), 4) + "%\n";
        tooltip += "Z: " + format(scale(2), 4) + "%";
        set_tooltip(tooltip);
    }
    else if (!m_grabbers[6].dragging && !m_grabbers[7].dragging && !m_grabbers[8].dragging && !m_grabbers[9].dragging &&
        ((m_hover_id == 6) || (m_hover_id == 7) || (m_hover_id == 8) || (m_hover_id == 9)))
        set_tooltip("X/Y/Z");

    ::glClear(GL_DEPTH_BUFFER_BIT);
    ::glEnable(GL_DEPTH_TEST);

    BoundingBoxf3 box;
    Transform3d transform = Transform3d::Identity();
    Vec3d angles = Vec3d::Zero();
    Transform3d offsets_transform = Transform3d::Identity();

    Vec3d grabber_size = Vec3d::Zero();

    if (single_instance)
    {
        // calculate bounding box in instance local reference system
        const GLCanvas3D::Selection::IndicesList& idxs = selection.get_volume_idxs();
        for (unsigned int idx : idxs)
        {
            const GLVolume* vol = selection.get_volume(idx);
            box.merge(vol->bounding_box.transformed(vol->get_volume_transformation().get_matrix()));
        }

        // gets transform from first selected volume
        const GLVolume* v = selection.get_volume(*idxs.begin());
        transform = v->get_instance_transformation().get_matrix();
        // gets angles from first selected volume
        angles = v->get_instance_rotation();
        // consider rotation+mirror only components of the transform for offsets
        offsets_transform = Geometry::assemble_transform(Vec3d::Zero(), angles, Vec3d::Ones(), v->get_instance_mirror());
        grabber_size = v->get_instance_transformation().get_matrix(true, true, false, true) * box.size();
    }
    else if (single_volume)
    {
        const GLVolume* v = selection.get_volume(*selection.get_volume_idxs().begin());
        box = v->bounding_box;
        transform = v->world_matrix();
        angles = Geometry::extract_euler_angles(transform);
        // consider rotation+mirror only components of the transform for offsets
        offsets_transform = Geometry::assemble_transform(Vec3d::Zero(), angles, Vec3d::Ones(), v->get_instance_mirror());
        grabber_size = v->get_volume_transformation().get_matrix(true, true, false, true) * box.size();
    }
    else
    {
        box = selection.get_bounding_box();
        grabber_size = box.size();
    }

    m_box = box;

    const Vec3d& center = m_box.center();
    Vec3d offset_x = offsets_transform * Vec3d((double)Offset, 0.0, 0.0);
    Vec3d offset_y = offsets_transform * Vec3d(0.0, (double)Offset, 0.0);
    Vec3d offset_z = offsets_transform * Vec3d(0.0, 0.0, (double)Offset);

    // x axis
    m_grabbers[0].center = transform * Vec3d(m_box.min(0), center(1), center(2)) - offset_x;
    m_grabbers[1].center = transform * Vec3d(m_box.max(0), center(1), center(2)) + offset_x;
    ::memcpy((void*)m_grabbers[0].color, (const void*)&AXES_COLOR[0], 3 * sizeof(float));
    ::memcpy((void*)m_grabbers[1].color, (const void*)&AXES_COLOR[0], 3 * sizeof(float));

    // y axis
    m_grabbers[2].center = transform * Vec3d(center(0), m_box.min(1), center(2)) - offset_y;
    m_grabbers[3].center = transform * Vec3d(center(0), m_box.max(1), center(2)) + offset_y;
    ::memcpy((void*)m_grabbers[2].color, (const void*)&AXES_COLOR[1], 3 * sizeof(float));
    ::memcpy((void*)m_grabbers[3].color, (const void*)&AXES_COLOR[1], 3 * sizeof(float));

    // z axis
    m_grabbers[4].center = transform * Vec3d(center(0), center(1), m_box.min(2)) - offset_z;
    m_grabbers[5].center = transform * Vec3d(center(0), center(1), m_box.max(2)) + offset_z;
    ::memcpy((void*)m_grabbers[4].color, (const void*)&AXES_COLOR[2], 3 * sizeof(float));
    ::memcpy((void*)m_grabbers[5].color, (const void*)&AXES_COLOR[2], 3 * sizeof(float));

    // uniform
    m_grabbers[6].center = transform * Vec3d(m_box.min(0), m_box.min(1), center(2)) - offset_x - offset_y;
    m_grabbers[7].center = transform * Vec3d(m_box.max(0), m_box.min(1), center(2)) + offset_x - offset_y;
    m_grabbers[8].center = transform * Vec3d(m_box.max(0), m_box.max(1), center(2)) + offset_x + offset_y;
    m_grabbers[9].center = transform * Vec3d(m_box.min(0), m_box.max(1), center(2)) - offset_x + offset_y;
    for (int i = 6; i < 10; ++i)
    {
        ::memcpy((void*)m_grabbers[i].color, (const void*)m_highlight_color, 3 * sizeof(float));
    }

    // sets grabbers orientation
    for (int i = 0; i < 10; ++i)
    {
        m_grabbers[i].angles = angles;
    }

    ::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f);

    float grabber_max_size = (float)std::max(grabber_size(0), std::max(grabber_size(1), grabber_size(2)));

    if (m_hover_id == -1)
    {
        // draw connections
        if (m_grabbers[0].enabled && m_grabbers[1].enabled)
        {
            ::glColor3fv(m_grabbers[0].color);
            render_grabbers_connection(0, 1);
        }
        if (m_grabbers[2].enabled && m_grabbers[3].enabled)
        {
            ::glColor3fv(m_grabbers[2].color);
            render_grabbers_connection(2, 3);
        }
        if (m_grabbers[4].enabled && m_grabbers[5].enabled)
        {
            ::glColor3fv(m_grabbers[4].color);
            render_grabbers_connection(4, 5);
        }
        ::glColor3fv(m_base_color);
        render_grabbers_connection(6, 7);
        render_grabbers_connection(7, 8);
        render_grabbers_connection(8, 9);
        render_grabbers_connection(9, 6);
        // draw grabbers
        render_grabbers(grabber_max_size);
    }
    else if ((m_hover_id == 0) || (m_hover_id == 1))
    {
        // draw connection
        ::glColor3fv(m_grabbers[0].color);
        render_grabbers_connection(0, 1);
        // draw grabbers
        m_grabbers[0].render(true, grabber_max_size);
        m_grabbers[1].render(true, grabber_max_size);
    }
    else if ((m_hover_id == 2) || (m_hover_id == 3))
    {
        // draw connection
        ::glColor3fv(m_grabbers[2].color);
        render_grabbers_connection(2, 3);
        // draw grabbers
        m_grabbers[2].render(true, grabber_max_size);
        m_grabbers[3].render(true, grabber_max_size);
    }
    else if ((m_hover_id == 4) || (m_hover_id == 5))
    {
        // draw connection
        ::glColor3fv(m_grabbers[4].color);
        render_grabbers_connection(4, 5);
        // draw grabbers
        m_grabbers[4].render(true, grabber_max_size);
        m_grabbers[5].render(true, grabber_max_size);
    }
    else if (m_hover_id >= 6)
    {
        // draw connection
        ::glColor3fv(m_drag_color);
        render_grabbers_connection(6, 7);
        render_grabbers_connection(7, 8);
        render_grabbers_connection(8, 9);
        render_grabbers_connection(9, 6);
        // draw grabbers
        for (int i = 6; i < 10; ++i)
        {
            m_grabbers[i].render(true, grabber_max_size);
        }
    }
}

void GLGizmoScale3D::on_render_for_picking(const GLCanvas3D::Selection& selection) const
{
    ::glDisable(GL_DEPTH_TEST);

    render_grabbers_for_picking(selection.get_bounding_box());
}

#if ENABLE_IMGUI
void GLGizmoScale3D::on_render_input_window(float x, float y, float bottom_limit, const GLCanvas3D::Selection& selection)
{
#if !DISABLE_MOVE_ROTATE_SCALE_GIZMOS_IMGUI
    bool single_instance = selection.is_single_full_instance();
    wxString label = _(L("Scale (%)"));

    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);
    m_imgui->set_next_window_bg_alpha(0.5f);
    m_imgui->begin(label, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    m_imgui->input_vec3("", m_scale * 100.f, 100.0f, "%.2f");
    m_imgui->end();
#endif // !DISABLE_MOVE_ROTATE_SCALE_GIZMOS_IMGUI
}
#endif // ENABLE_IMGUI

void GLGizmoScale3D::render_grabbers_connection(unsigned int id_1, unsigned int id_2) const
{
    unsigned int grabbers_count = (unsigned int)m_grabbers.size();
    if ((id_1 < grabbers_count) && (id_2 < grabbers_count))
    {
        ::glBegin(GL_LINES);
        ::glVertex3dv(m_grabbers[id_1].center.data());
        ::glVertex3dv(m_grabbers[id_2].center.data());
        ::glEnd();
    }
}

void GLGizmoScale3D::do_scale_x(const UpdateData& data)
{
    double ratio = calc_ratio(data);
    if (ratio > 0.0)
        m_scale(0) = m_starting_scale(0) * ratio;
}

void GLGizmoScale3D::do_scale_y(const UpdateData& data)
{
    double ratio = calc_ratio(data);
    if (ratio > 0.0)
        m_scale(1) = m_starting_scale(1) * ratio;
}

void GLGizmoScale3D::do_scale_z(const UpdateData& data)
{
    double ratio = calc_ratio(data);
    if (ratio > 0.0)
        m_scale(2) = m_starting_scale(2) * ratio;
}

void GLGizmoScale3D::do_scale_uniform(const UpdateData& data)
{
    double ratio = calc_ratio(data);
    if (ratio > 0.0)
        m_scale = m_starting_scale * ratio;
}

double GLGizmoScale3D::calc_ratio(const UpdateData& data) const
{
    double ratio = 0.0;

    // vector from the center to the starting position
    Vec3d starting_vec = m_starting_drag_position - m_starting_box.center();
    double len_starting_vec = starting_vec.norm();
    if (len_starting_vec != 0.0)
    {
        Vec3d mouse_dir = data.mouse_ray.unit_vector();
        // finds the intersection of the mouse ray with the plane parallel to the camera viewport and passing throught the starting position
        // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection algebric form
        // in our case plane normal and ray direction are the same (orthogonal view)
        // when moving to perspective camera the negative z unit axis of the camera needs to be transformed in world space and used as plane normal
        Vec3d inters = data.mouse_ray.a + (m_starting_drag_position - data.mouse_ray.a).dot(mouse_dir) / mouse_dir.squaredNorm() * mouse_dir;
        // vector from the starting position to the found intersection
        Vec3d inters_vec = inters - m_starting_drag_position;

        // finds projection of the vector along the staring direction
        double proj = inters_vec.dot(starting_vec.normalized());

        ratio = (len_starting_vec + proj) / len_starting_vec;
    }

    if (data.shift_down)
        ratio = m_snap_step * (double)std::round(ratio / m_snap_step);

    return ratio;
}

const double GLGizmoMove3D::Offset = 10.0;

#if ENABLE_SVG_ICONS
GLGizmoMove3D::GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
#else
GLGizmoMove3D::GLGizmoMove3D(GLCanvas3D& parent, unsigned int sprite_id)
    : GLGizmoBase(parent, sprite_id)
#endif // ENABLE_SVG_ICONS
    , m_displacement(Vec3d::Zero())
    , m_snap_step(1.0)
    , m_starting_drag_position(Vec3d::Zero())
    , m_starting_box_center(Vec3d::Zero())
    , m_starting_box_bottom_center(Vec3d::Zero())
    , m_quadric(nullptr)
{
    m_quadric = ::gluNewQuadric();
    if (m_quadric != nullptr)
        ::gluQuadricDrawStyle(m_quadric, GLU_FILL);
}

GLGizmoMove3D::~GLGizmoMove3D()
{
    if (m_quadric != nullptr)
        ::gluDeleteQuadric(m_quadric);
}

bool GLGizmoMove3D::on_init()
{
    for (int i = 0; i < 3; ++i)
    {
        m_grabbers.push_back(Grabber());
    }

    m_shortcut_key = WXK_CONTROL_M;

    return true;
}

std::string GLGizmoMove3D::on_get_name() const
{
    return L("Move [M]");
}

void GLGizmoMove3D::on_start_dragging(const GLCanvas3D::Selection& selection)
{
    if (m_hover_id != -1)
    {
        m_displacement = Vec3d::Zero();
        const BoundingBoxf3& box = selection.get_bounding_box();
        m_starting_drag_position = m_grabbers[m_hover_id].center;
        m_starting_box_center = box.center();
        m_starting_box_bottom_center = box.center();
        m_starting_box_bottom_center(2) = box.min(2);
    }
}

void GLGizmoMove3D::on_stop_dragging()
{
    m_displacement = Vec3d::Zero();
}

void GLGizmoMove3D::on_update(const UpdateData& data, const GLCanvas3D::Selection& selection)
{
    if (m_hover_id == 0)
        m_displacement(0) = calc_projection(data);
    else if (m_hover_id == 1)
        m_displacement(1) = calc_projection(data);
    else if (m_hover_id == 2)
        m_displacement(2) = calc_projection(data);
}

void GLGizmoMove3D::on_render(const GLCanvas3D::Selection& selection) const
{
    bool show_position = selection.is_single_full_instance();
    const Vec3d& position = selection.get_bounding_box().center();

    if ((show_position && (m_hover_id == 0)) || m_grabbers[0].dragging)
        set_tooltip("X: " + format(show_position ? position(0) : m_displacement(0), 2));
    else if (!m_grabbers[0].dragging && (m_hover_id == 0))
        set_tooltip("X");
    else if ((show_position && (m_hover_id == 1)) || m_grabbers[1].dragging)
        set_tooltip("Y: " + format(show_position ? position(1) : m_displacement(1), 2));
    else if (!m_grabbers[1].dragging && (m_hover_id == 1))
        set_tooltip("Y");
    else if ((show_position && (m_hover_id == 2)) || m_grabbers[2].dragging)
        set_tooltip("Z: " + format(show_position ? position(2) : m_displacement(2), 2));
    else if (!m_grabbers[2].dragging && (m_hover_id == 2))
        set_tooltip("Z");

    ::glClear(GL_DEPTH_BUFFER_BIT);
    ::glEnable(GL_DEPTH_TEST);

    const BoundingBoxf3& box = selection.get_bounding_box();
    const Vec3d& center = box.center();

    // x axis
    m_grabbers[0].center = Vec3d(box.max(0) + Offset, center(1), center(2));
    ::memcpy((void*)m_grabbers[0].color, (const void*)&AXES_COLOR[0], 3 * sizeof(float));

    // y axis
    m_grabbers[1].center = Vec3d(center(0), box.max(1) + Offset, center(2));
    ::memcpy((void*)m_grabbers[1].color, (const void*)&AXES_COLOR[1], 3 * sizeof(float));

    // z axis
    m_grabbers[2].center = Vec3d(center(0), center(1), box.max(2) + Offset);
    ::memcpy((void*)m_grabbers[2].color, (const void*)&AXES_COLOR[2], 3 * sizeof(float));

    ::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f);

    if (m_hover_id == -1)
    {
        // draw axes
        for (unsigned int i = 0; i < 3; ++i)
        {
            if (m_grabbers[i].enabled)
            {
                ::glColor3fv(AXES_COLOR[i]);
                ::glBegin(GL_LINES);
                ::glVertex3dv(center.data());
                ::glVertex3dv(m_grabbers[i].center.data());
                ::glEnd();
            }
        }

        // draw grabbers
        render_grabbers(box);
        for (unsigned int i = 0; i < 3; ++i)
        {
            if (m_grabbers[i].enabled)
                render_grabber_extension((Axis)i, box, false);
        }
    }
    else
    {
        // draw axis
        ::glColor3fv(AXES_COLOR[m_hover_id]);
        ::glBegin(GL_LINES);
        ::glVertex3dv(center.data());
        ::glVertex3dv(m_grabbers[m_hover_id].center.data());
        ::glEnd();

        // draw grabber
        m_grabbers[m_hover_id].render(true, box.max_size());
        render_grabber_extension((Axis)m_hover_id, box, false);
    }
}

void GLGizmoMove3D::on_render_for_picking(const GLCanvas3D::Selection& selection) const
{
    ::glDisable(GL_DEPTH_TEST);

    const BoundingBoxf3& box = selection.get_bounding_box();
    render_grabbers_for_picking(box);
    render_grabber_extension(X, box, true);
    render_grabber_extension(Y, box, true);
    render_grabber_extension(Z, box, true);
}

#if ENABLE_IMGUI
void GLGizmoMove3D::on_render_input_window(float x, float y, float bottom_limit, const GLCanvas3D::Selection& selection)
{
#if !DISABLE_MOVE_ROTATE_SCALE_GIZMOS_IMGUI
    bool show_position = selection.is_single_full_instance();
    const Vec3d& position = selection.get_bounding_box().center();

    Vec3d displacement = show_position ? position : m_displacement;
    wxString label = show_position ? _(L("Position (mm)")) : _(L("Displacement (mm)"));

    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);
    m_imgui->set_next_window_bg_alpha(0.5f);
    m_imgui->begin(label, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    m_imgui->input_vec3("", displacement, 100.0f, "%.2f");

    m_imgui->end();
#endif // !DISABLE_MOVE_ROTATE_SCALE_GIZMOS_IMGUI
}
#endif // ENABLE_IMGUI

double GLGizmoMove3D::calc_projection(const UpdateData& data) const
{
    double projection = 0.0;

    Vec3d starting_vec = m_starting_drag_position - m_starting_box_center;
    double len_starting_vec = starting_vec.norm();
    if (len_starting_vec != 0.0)
    {
        Vec3d mouse_dir = data.mouse_ray.unit_vector();
        // finds the intersection of the mouse ray with the plane parallel to the camera viewport and passing throught the starting position
        // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection algebric form
        // in our case plane normal and ray direction are the same (orthogonal view)
        // when moving to perspective camera the negative z unit axis of the camera needs to be transformed in world space and used as plane normal
        Vec3d inters = data.mouse_ray.a + (m_starting_drag_position - data.mouse_ray.a).dot(mouse_dir) / mouse_dir.squaredNorm() * mouse_dir;
        // vector from the starting position to the found intersection
        Vec3d inters_vec = inters - m_starting_drag_position;

        // finds projection of the vector along the staring direction
        projection = inters_vec.dot(starting_vec.normalized());
    }

    if (data.shift_down)
        projection = m_snap_step * (double)std::round(projection / m_snap_step);

    return projection;
}

void GLGizmoMove3D::render_grabber_extension(Axis axis, const BoundingBoxf3& box, bool picking) const
{
    if (m_quadric == nullptr)
        return;

    double size = m_dragging ? (double)m_grabbers[axis].get_dragging_half_size((float)box.max_size()) : (double)m_grabbers[axis].get_half_size((float)box.max_size());

    float color[3];
    ::memcpy((void*)color, (const void*)m_grabbers[axis].color, 3 * sizeof(float));
    if (!picking && (m_hover_id != -1))
    {
        color[0] = 1.0f - color[0];
        color[1] = 1.0f - color[1];
        color[2] = 1.0f - color[2];
    }

    if (!picking)
        ::glEnable(GL_LIGHTING);

    ::glColor3fv(color);
    ::glPushMatrix();
    ::glTranslated(m_grabbers[axis].center(0), m_grabbers[axis].center(1), m_grabbers[axis].center(2));
    if (axis == X)
        ::glRotated(90.0, 0.0, 1.0, 0.0);
    else if (axis == Y)
        ::glRotated(-90.0, 1.0, 0.0, 0.0);

    ::glTranslated(0.0, 0.0, 2.0 * size);
    ::gluQuadricOrientation(m_quadric, GLU_OUTSIDE);
    ::gluCylinder(m_quadric, 0.75 * size, 0.0, 3.0 * size, 36, 1);
    ::gluQuadricOrientation(m_quadric, GLU_INSIDE);
    ::gluDisk(m_quadric, 0.0, 0.75 * size, 36, 1);
    ::glPopMatrix();

    if (!picking)
        ::glDisable(GL_LIGHTING);
}

#if ENABLE_SVG_ICONS
GLGizmoFlatten::GLGizmoFlatten(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
#else
GLGizmoFlatten::GLGizmoFlatten(GLCanvas3D& parent, unsigned int sprite_id)
    : GLGizmoBase(parent, sprite_id)
#endif // ENABLE_SVG_ICONS
    , m_normal(Vec3d::Zero())
    , m_starting_center(Vec3d::Zero())
{
}

bool GLGizmoFlatten::on_init()
{
    m_shortcut_key = WXK_CONTROL_F;
    return true;
}

std::string GLGizmoFlatten::on_get_name() const
{
    return L("Place on face [F]");
}

bool GLGizmoFlatten::on_is_activable(const GLCanvas3D::Selection& selection) const
{
    return selection.is_single_full_instance();
}

void GLGizmoFlatten::on_start_dragging(const GLCanvas3D::Selection& selection)
{
    if (m_hover_id != -1)
    {
        assert(m_planes_valid);
        m_normal = m_planes[m_hover_id].normal;
        m_starting_center = selection.get_bounding_box().center();
    }
}

void GLGizmoFlatten::on_render(const GLCanvas3D::Selection& selection) const
{
    ::glClear(GL_DEPTH_BUFFER_BIT);

    ::glEnable(GL_DEPTH_TEST);
    ::glEnable(GL_BLEND);

    if (selection.is_single_full_instance())
    {
        const Transform3d& m = selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_transformation().get_matrix();
        ::glPushMatrix();
        ::glTranslatef(0.f, 0.f, selection.get_volume(*selection.get_volume_idxs().begin())->get_sla_shift_z());
        ::glMultMatrixd(m.data());
        if (this->is_plane_update_necessary())
			const_cast<GLGizmoFlatten*>(this)->update_planes();
        for (int i = 0; i < (int)m_planes.size(); ++i)
        {
            if (i == m_hover_id)
                ::glColor4f(0.9f, 0.9f, 0.9f, 0.75f);
            else
                ::glColor4f(0.9f, 0.9f, 0.9f, 0.5f);

            ::glBegin(GL_POLYGON);
            for (const Vec3d& vertex : m_planes[i].vertices)
            {
                ::glVertex3dv(vertex.data());
            }
            ::glEnd();
        }
        ::glPopMatrix();
    }

    ::glEnable(GL_CULL_FACE);
    ::glDisable(GL_BLEND);
}

void GLGizmoFlatten::on_render_for_picking(const GLCanvas3D::Selection& selection) const
{
    ::glDisable(GL_DEPTH_TEST);
    ::glDisable(GL_BLEND);

    if (selection.is_single_full_instance())
    {
        const Transform3d& m = selection.get_volume(*selection.get_volume_idxs().begin())->get_instance_transformation().get_matrix();
        ::glPushMatrix();
        ::glTranslatef(0.f, 0.f, selection.get_volume(*selection.get_volume_idxs().begin())->get_sla_shift_z());
        ::glMultMatrixd(m.data());
        if (this->is_plane_update_necessary())
			const_cast<GLGizmoFlatten*>(this)->update_planes();
        for (int i = 0; i < (int)m_planes.size(); ++i)
        {
            ::glColor3f(1.0f, 1.0f, picking_color_component(i));
            ::glBegin(GL_POLYGON);
            for (const Vec3d& vertex : m_planes[i].vertices)
            {
                ::glVertex3dv(vertex.data());
            }
            ::glEnd();
        }
        ::glPopMatrix();
    }

    ::glEnable(GL_CULL_FACE);
}

void GLGizmoFlatten::set_flattening_data(const ModelObject* model_object)
{
    m_starting_center = Vec3d::Zero();
    if (m_model_object != model_object) {
        m_planes.clear();
        m_planes_valid = false;
    }
    m_model_object = model_object;
}

void GLGizmoFlatten::update_planes()
{
    TriangleMesh ch;
    for (const ModelVolume* vol : m_model_object->volumes)
    {
        if (vol->type() != ModelVolumeType::MODEL_PART)
            continue;
        TriangleMesh vol_ch = vol->get_convex_hull();
        vol_ch.transform(vol->get_matrix());
        ch.merge(vol_ch);
    }
    ch = ch.convex_hull_3d();
    m_planes.clear();
    const Transform3d& inst_matrix = m_model_object->instances.front()->get_matrix(true);

    // Following constants are used for discarding too small polygons.
    const float minimal_area = 5.f; // in square mm (world coordinates)
    const float minimal_side = 1.f; // mm

    // Now we'll go through all the facets and append Points of facets sharing the same normal.
    // This part is still performed in mesh coordinate system.
    const int num_of_facets = ch.stl.stats.number_of_facets;
    std::vector<int>  facet_queue(num_of_facets, 0);
    std::vector<bool> facet_visited(num_of_facets, false);
    int               facet_queue_cnt = 0;
    const stl_normal* normal_ptr = nullptr;
    while (1) {
        // Find next unvisited triangle:
        int facet_idx = 0;
        for (; facet_idx < num_of_facets; ++ facet_idx)
            if (!facet_visited[facet_idx]) {
                facet_queue[facet_queue_cnt ++] = facet_idx;
                facet_visited[facet_idx] = true;
                normal_ptr = &ch.stl.facet_start[facet_idx].normal;
                m_planes.emplace_back();
                break;
            }
        if (facet_idx == num_of_facets)
            break; // Everything was visited already

        while (facet_queue_cnt > 0) {
            int facet_idx = facet_queue[-- facet_queue_cnt];
            const stl_normal& this_normal = ch.stl.facet_start[facet_idx].normal;
            if (std::abs(this_normal(0) - (*normal_ptr)(0)) < 0.001 && std::abs(this_normal(1) - (*normal_ptr)(1)) < 0.001 && std::abs(this_normal(2) - (*normal_ptr)(2)) < 0.001) {
                stl_vertex* first_vertex = ch.stl.facet_start[facet_idx].vertex;
                for (int j=0; j<3; ++j)
                    m_planes.back().vertices.emplace_back((double)first_vertex[j](0), (double)first_vertex[j](1), (double)first_vertex[j](2));

                facet_visited[facet_idx] = true;
                for (int j = 0; j < 3; ++ j) {
                    int neighbor_idx = ch.stl.neighbors_start[facet_idx].neighbor[j];
                    if (! facet_visited[neighbor_idx])
                        facet_queue[facet_queue_cnt ++] = neighbor_idx;
                }
            }
        }
        m_planes.back().normal = normal_ptr->cast<double>();

        // Now we'll transform all the points into world coordinates, so that the areas, angles and distances
        // make real sense.
        m_planes.back().vertices = transform(m_planes.back().vertices, inst_matrix);

        // if this is a just a very small triangle, remove it to speed up further calculations (it would be rejected later anyway):
        if (m_planes.back().vertices.size() == 3 &&
            ((m_planes.back().vertices[0] - m_planes.back().vertices[1]).norm() < minimal_side
            || (m_planes.back().vertices[0] - m_planes.back().vertices[2]).norm() < minimal_side
            || (m_planes.back().vertices[1] - m_planes.back().vertices[2]).norm() < minimal_side))
            m_planes.pop_back();
    }

    // Let's prepare transformation of the normal vector from mesh to instance coordinates.
    Geometry::Transformation t(inst_matrix);
    Vec3d scaling = t.get_scaling_factor();
    t.set_scaling_factor(Vec3d(1./scaling(0), 1./scaling(1), 1./scaling(2)));

    // Now we'll go through all the polygons, transform the points into xy plane to process them:
    for (unsigned int polygon_id=0; polygon_id < m_planes.size(); ++polygon_id) {
        Pointf3s& polygon = m_planes[polygon_id].vertices;
        const Vec3d& normal = m_planes[polygon_id].normal;

        // transform the normal according to the instance matrix:
        Vec3d normal_transformed = t.get_matrix() * normal;

        // We are going to rotate about z and y to flatten the plane
        Eigen::Quaterniond q;
        Transform3d m = Transform3d::Identity();
        m.matrix().block(0, 0, 3, 3) = q.setFromTwoVectors(normal_transformed, Vec3d::UnitZ()).toRotationMatrix();
        polygon = transform(polygon, m);

        // Now to remove the inner points. We'll misuse Geometry::convex_hull for that, but since
        // it works in fixed point representation, we will rescale the polygon to avoid overflows.
        // And yes, it is a nasty thing to do. Whoever has time is free to refactor.
        Vec3d bb_size = BoundingBoxf3(polygon).size();
        float sf = std::min(1./bb_size(0), 1./bb_size(1));
        Transform3d tr = Geometry::assemble_transform(Vec3d::Zero(), Vec3d::Zero(), Vec3d(sf, sf, 1.f));
        polygon = transform(polygon, tr);
        polygon = Slic3r::Geometry::convex_hull(polygon);
        polygon = transform(polygon, tr.inverse());

        // Calculate area of the polygons and discard ones that are too small
        float& area = m_planes[polygon_id].area;
        area = 0.f;
        for (unsigned int i = 0; i < polygon.size(); i++) // Shoelace formula
            area += polygon[i](0)*polygon[i + 1 < polygon.size() ? i + 1 : 0](1) - polygon[i + 1 < polygon.size() ? i + 1 : 0](0)*polygon[i](1);
        area = 0.5f * std::abs(area);

        bool discard = false;
        if (area < minimal_area)
            discard = true;
        else {
            // We also check the inner angles and discard polygons with angles smaller than the following threshold
            const double angle_threshold = ::cos(10.0 * (double)PI / 180.0);

            for (unsigned int i = 0; i < polygon.size(); ++i) {
                const Vec3d& prec = polygon[(i == 0) ? polygon.size() - 1 : i - 1];
                const Vec3d& curr = polygon[i];
                const Vec3d& next = polygon[(i == polygon.size() - 1) ? 0 : i + 1];

                if ((prec - curr).normalized().dot((next - curr).normalized()) > angle_threshold) {
                    discard = true;
                    break;
                }
            }
        }

        if (discard) {
            m_planes.erase(m_planes.begin() + (polygon_id--));
            continue;
        }

        // We will shrink the polygon a little bit so it does not touch the object edges:
        Vec3d centroid = std::accumulate(polygon.begin(), polygon.end(), Vec3d(0.0, 0.0, 0.0));
        centroid /= (double)polygon.size();
        for (auto& vertex : polygon)
            vertex = 0.9f*vertex + 0.1f*centroid;

        // Polygon is now simple and convex, we'll round the corners to make them look nicer.
        // The algorithm takes a vertex, calculates middles of respective sides and moves the vertex
        // towards their average (controlled by 'aggressivity'). This is repeated k times.
        // In next iterations, the neighbours are not always taken at the middle (to increase the
        // rounding effect at the corners, where we need it most).
        const unsigned int k = 10; // number of iterations
        const float aggressivity = 0.2f;  // agressivity
        const unsigned int N = polygon.size();
        std::vector<std::pair<unsigned int, unsigned int>> neighbours;
        if (k != 0) {
            Pointf3s points_out(2*k*N); // vector long enough to store the future vertices
            for (unsigned int j=0; j<N; ++j) {
                points_out[j*2*k] = polygon[j];
                neighbours.push_back(std::make_pair((int)(j*2*k-k) < 0 ? (N-1)*2*k+k : j*2*k-k, j*2*k+k));
            }

            for (unsigned int i=0; i<k; ++i) {
                // Calculate middle of each edge so that neighbours points to something useful:
                for (unsigned int j=0; j<N; ++j)
                    if (i==0)
                        points_out[j*2*k+k] = 0.5f * (points_out[j*2*k] + points_out[j==N-1 ? 0 : (j+1)*2*k]);
                    else {
                        float r = 0.2+0.3/(k-1)*i; // the neighbours are not always taken in the middle
                        points_out[neighbours[j].first] = r*points_out[j*2*k] + (1-r) * points_out[neighbours[j].first-1];
                        points_out[neighbours[j].second] = r*points_out[j*2*k] + (1-r) * points_out[neighbours[j].second+1];
                    }
                // Now we have a triangle and valid neighbours, we can do an iteration:
                for (unsigned int j=0; j<N; ++j)
                    points_out[2*k*j] = (1-aggressivity) * points_out[2*k*j] +
                                        aggressivity*0.5f*(points_out[neighbours[j].first] + points_out[neighbours[j].second]);

                for (auto& n : neighbours) {
                    ++n.first;
                    --n.second;
                }
            }
            polygon = points_out; // replace the coarse polygon with the smooth one that we just created
        }


        // Raise a bit above the object surface to avoid flickering:
        for (auto& b : polygon)
            b(2) += 0.1f;

        // Transform back to 3D (and also back to mesh coordinates)
        polygon = transform(polygon, inst_matrix.inverse() * m.inverse());
    }

    // We'll sort the planes by area and only keep the 254 largest ones (because of the picking pass limitations):
    std::sort(m_planes.rbegin(), m_planes.rend(), [](const PlaneData& a, const PlaneData& b) { return a.area < b.area; });
    m_planes.resize(std::min((int)m_planes.size(), 254));

    // Planes are finished - let's save what we calculated it from:
    m_volumes_matrices.clear();
    m_volumes_types.clear();
    for (const ModelVolume* vol : m_model_object->volumes) {
        m_volumes_matrices.push_back(vol->get_matrix());
        m_volumes_types.push_back(vol->type());
    }
    m_first_instance_scale = m_model_object->instances.front()->get_scaling_factor();
    m_first_instance_mirror = m_model_object->instances.front()->get_mirror();

    m_planes_valid = true;
}


bool GLGizmoFlatten::is_plane_update_necessary() const
{
    if (m_state != On || !m_model_object || m_model_object->instances.empty())
        return false;

    if (! m_planes_valid || m_model_object->volumes.size() != m_volumes_matrices.size())
        return true;

    // We want to recalculate when the scale changes - some planes could (dis)appear.
    if (! m_model_object->instances.front()->get_scaling_factor().isApprox(m_first_instance_scale)
     || ! m_model_object->instances.front()->get_mirror().isApprox(m_first_instance_mirror))
        return true;

    for (unsigned int i=0; i < m_model_object->volumes.size(); ++i)
        if (! m_model_object->volumes[i]->get_matrix().isApprox(m_volumes_matrices[i])
         || m_model_object->volumes[i]->type() != m_volumes_types[i])
            return true;

    return false;
}

Vec3d GLGizmoFlatten::get_flattening_normal() const
{
    Vec3d out = m_normal;
    m_normal = Vec3d::Zero();
    m_starting_center = Vec3d::Zero();
    return out;
}

#if ENABLE_SVG_ICONS
GLGizmoSlaSupports::GLGizmoSlaSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
#else
GLGizmoSlaSupports::GLGizmoSlaSupports(GLCanvas3D& parent, unsigned int sprite_id)
    : GLGizmoBase(parent, sprite_id)
#endif // ENABLE_SVG_ICONS
    , m_starting_center(Vec3d::Zero()), m_quadric(nullptr)
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

void GLGizmoSlaSupports::set_sla_support_data(ModelObject* model_object, const GLCanvas3D::Selection& selection)
{
    m_starting_center = Vec3d::Zero();
    m_old_model_object = m_model_object;
    m_model_object = model_object;
    if (selection.is_empty())
        m_old_instance_id = -1;

    m_active_instance = selection.get_instance_idx();

    if (model_object && selection.is_from_single_instance())
    {
        if (is_mesh_update_necessary())
            update_mesh();

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

void GLGizmoSlaSupports::on_render(const GLCanvas3D::Selection& selection) const
{
    ::glEnable(GL_BLEND);
    ::glEnable(GL_DEPTH_TEST);

    render_points(selection, false);
    render_selection_rectangle();

#if !ENABLE_IMGUI
    render_tooltip_texture();
#endif // not ENABLE_IMGUI

    ::glDisable(GL_BLEND);
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

void GLGizmoSlaSupports::on_render_for_picking(const GLCanvas3D::Selection& selection) const
{
    ::glEnable(GL_DEPTH_TEST);

    render_points(selection, true);
}

void GLGizmoSlaSupports::render_points(const GLCanvas3D::Selection& selection, bool picking) const
{
    if (m_quadric == nullptr || !selection.is_from_single_instance())
        return;

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
        const sla::SupportPoint& support_point = m_editing_mode_cache[i].first;
        const bool& point_selected = m_editing_mode_cache[i].second;

        // First decide about the color of the point.
        if (picking) {
            render_color[0] = 1.0f;
            render_color[1] = 1.0f;
            render_color[2] = picking_color_component(i);
        }
        else {
            if ((m_hover_id == i && m_editing_mode)) { // ignore hover state unless editing mode is active
                render_color[0] = 0.f;
                render_color[1] = 1.0f;
                render_color[2] = 1.0f;
            }
            else { // neigher hover nor picking
                bool supports_new_island = m_lock_unique_islands && m_editing_mode_cache[i].first.is_new_island;
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

        // Now render the sphere. Inverse matrix of the instance scaling is applied so that the
        // sphere does not scale with the object.
        ::glPushMatrix();
        ::glTranslated(support_point.pos(0), support_point.pos(1), support_point.pos(2));
        ::glMultMatrixd(instance_scaling_matrix_inverse.data());
        ::gluSphere(m_quadric, m_editing_mode_cache[i].first.head_front_radius * RenderPointScale, 64, 36);
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

bool GLGizmoSlaSupports::is_mesh_update_necessary() const
{
    return (m_state == On) && (m_model_object != m_old_model_object) && (m_model_object != nullptr) && !m_model_object->instances.empty();

    //if (m_state != On || !m_model_object || m_model_object->instances.empty() || ! m_instance_matrix.isApprox(m_source_data.matrix))
    //    return false;
}

void GLGizmoSlaSupports::update_mesh()
{
    Eigen::MatrixXf& V = m_V;
    Eigen::MatrixXi& F = m_F;
    // Composite mesh of all instances in the world coordinate system.
    // This mesh does not account for the possible Z up SLA offset.
    TriangleMesh mesh = m_model_object->raw_mesh();
    const stl_file& stl = mesh.stl;
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

    // we'll now reload support points (selection might have changed):
    editing_mode_reload_cache();
}

Vec3f GLGizmoSlaSupports::unproject_on_mesh(const Vec2d& mouse_pos)
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

    igl::Hit hit;

    const GLCanvas3D::Selection& selection = m_parent.get_selection();
    const GLVolume* volume = selection.get_volume(*selection.get_volume_idxs().begin());
    double z_offset = volume->get_sla_shift_z();

    point1(2) -= z_offset;
	point2(2) -= z_offset;

    Transform3d inv = volume->get_instance_transformation().get_matrix().inverse();

    point1 = inv * point1;
    point2 = inv * point2;

    if (!m_AABB.intersect_ray(m_V, m_F, point1.cast<float>(), (point2-point1).cast<float>(), hit))
        throw std::invalid_argument("unproject_on_mesh(): No intersection found.");

    int fid = hit.id;
    Vec3f bc(1-hit.u-hit.v, hit.u, hit.v);
    return bc(0) * m_V.row(m_F(fid, 0)) + bc(1) * m_V.row(m_F(fid, 1)) + bc(2)*m_V.row(m_F(fid, 2));
}

// Following function is called from GLCanvas3D to inform the gizmo about a mouse/keyboard event.
// The gizmo has an opportunity to react - if it does, it should return true so that the Canvas3D is
// aware that the event was reacted to and stops trying to make different sense of it. If the gizmo
// concludes that the event was not intended for it, it should return false.
bool GLGizmoSlaSupports::mouse_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down)
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
            else
                select_point(m_hover_id);

            return true;
        }

        // dragging the selection rectangle:
        if (action == SLAGizmoEventType::Dragging && m_selection_rectangle_active) {
            m_selection_rectangle_end_corner = mouse_position;
            return true;
        }

        // mouse up without selection rectangle - place point on the mesh:
        if (action == SLAGizmoEventType::LeftUp && !m_selection_rectangle_active && !shift_down) {
            if (m_ignore_up_event) {
                m_ignore_up_event = false;
                return false;
            }

            int instance_id = m_parent.get_selection().get_instance_idx();
            if (m_old_instance_id != instance_id)
            {
                bool something_selected = (m_old_instance_id != -1);
                m_old_instance_id = instance_id;
                if (something_selected)
                    return false;
            }
            if (instance_id == -1)
                return false;

            // If there is some selection, don't add new point and deselect everything instead.
            if (m_selection_empty) {
                Vec3f new_pos;
                try {
                    new_pos = unproject_on_mesh(mouse_position); // this can throw - we don't want to create a new point in that case
                    m_editing_mode_cache.emplace_back(std::make_pair(sla::SupportPoint(new_pos, m_new_point_head_diameter/2.f, false), false));
                    m_unsaved_changes = true;
                }
                catch (...) {      // not clicked on object
                    return true;   // prevents deselection of the gizmo by GLCanvas3D
                }
            }
            else
                select_point(NoPoints);

            return true;
        }

        // left up with selection rectangle - select points inside the rectangle:
        if ((action == SLAGizmoEventType::LeftUp || action == SLAGizmoEventType::ShiftUp)
          && m_selection_rectangle_active) {
            if (action == SLAGizmoEventType::ShiftUp)
                m_ignore_up_event = true;
            const Transform3d& instance_matrix = m_model_object->instances[m_active_instance]->get_transformation().get_matrix();
            GLint viewport[4];
            ::glGetIntegerv(GL_VIEWPORT, viewport);
            GLdouble modelview_matrix[16];
            ::glGetDoublev(GL_MODELVIEW_MATRIX, modelview_matrix);
            GLdouble projection_matrix[16];
            ::glGetDoublev(GL_PROJECTION_MATRIX, projection_matrix);

            const GLCanvas3D::Selection& selection = m_parent.get_selection();
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
                const sla::SupportPoint &support_point = m_editing_mode_cache[i].first;
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
        if (m_editing_mode_cache[idx].second && (!m_editing_mode_cache[idx].first.is_new_island || !m_lock_unique_islands || force)) {
            m_editing_mode_cache.erase(m_editing_mode_cache.begin() + (idx--));
            m_unsaved_changes = true;
        }
            // This should trigger the support generation
            // wxGetApp().plater()->reslice_SLA_supports(*m_model_object);
    }

    select_point(NoPoints);

    //m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
}

void GLGizmoSlaSupports::on_update(const UpdateData& data, const GLCanvas3D::Selection& selection)
{
    if (m_editing_mode && m_hover_id != -1 && data.mouse_pos && (!m_editing_mode_cache[m_hover_id].first.is_new_island || !m_lock_unique_islands)) {
        Vec3f new_pos;
        try {
            new_pos = unproject_on_mesh(Vec2d((*data.mouse_pos)(0), (*data.mouse_pos)(1)));
        }
        catch (...) { return; }
        m_editing_mode_cache[m_hover_id].first.pos = new_pos;
        m_editing_mode_cache[m_hover_id].first.is_new_island = false;
        m_unsaved_changes = true;
        // Do not update immediately, wait until the mouse is released.
        // m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    }
}

#if !ENABLE_IMGUI
void GLGizmoSlaSupports::render_tooltip_texture() const {
    if (m_tooltip_texture.get_id() == 0)
        if (!m_tooltip_texture.load_from_file(resources_dir() + "/icons/sla_support_points_tooltip.png", false))
            return;
    if (m_reset_texture.get_id() == 0)
        if (!m_reset_texture.load_from_file(resources_dir() + "/icons/sla_support_points_reset.png", false))
            return;

    float zoom = m_parent.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
    float gap = 30.0f * inv_zoom;

    const Size& cnv_size = m_parent.get_canvas_size();
    float l = gap - cnv_size.get_width()/2.f * inv_zoom;
    float r = l + (float)m_tooltip_texture.get_width() * inv_zoom;
    float b = gap - cnv_size.get_height()/2.f * inv_zoom;
    float t = b + (float)m_tooltip_texture.get_height() * inv_zoom;

    Rect reset_rect = m_parent.get_gizmo_reset_rect(m_parent, true);

    ::glDisable(GL_DEPTH_TEST);
    ::glPushMatrix();
    ::glLoadIdentity();
    GLTexture::render_texture(m_tooltip_texture.get_id(), l, r, b, t);
    GLTexture::render_texture(m_reset_texture.get_id(), reset_rect.get_left(), reset_rect.get_right(), reset_rect.get_bottom(), reset_rect.get_top());
    ::glPopMatrix();
    ::glEnable(GL_DEPTH_TEST);
}
#endif // not ENABLE_IMGUI


std::vector<ConfigOption*> GLGizmoSlaSupports::get_config_options(const std::vector<std::string>& keys) const
{
    std::vector<ConfigOption*> out;

    if (!m_model_object)
        return out;

    DynamicPrintConfig& object_cfg = m_model_object->config;
    DynamicPrintConfig& print_cfg = wxGetApp().preset_bundle->sla_prints.get_edited_preset().config;
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



#if ENABLE_IMGUI
void GLGizmoSlaSupports::on_render_input_window(float x, float y, float bottom_limit, const GLCanvas3D::Selection& selection)
{
    if (!m_model_object)
        return;

    bool first_run = true; // This is a hack to redraw the button when all points are removed,
                           // so it is not delayed until the background process finishes.
RENDER_AGAIN:
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);

    static const ImVec2 window_size(285.f, 260.f);
    ImGui::SetNextWindowPos(ImVec2(x, y - std::max(0.f, y+window_size.y-bottom_limit) ));
    ImGui::SetNextWindowSize(ImVec2(window_size));

    m_imgui->set_next_window_bg_alpha(0.5f);
    m_imgui->begin(on_get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::PushItemWidth(100.0f);

    bool force_refresh = false;
    bool remove_selected = false;
    bool remove_all = false;

    if (m_editing_mode) {
        m_imgui->text(_(L("Left mouse click - add point")));
        m_imgui->text(_(L("Right mouse click - remove point")));
        m_imgui->text(_(L("Shift + Left (+ drag) - select point(s)")));
        m_imgui->text(" ");  // vertical gap

        static const std::vector<float> options = {0.2f, 0.4f, 0.6f, 0.8f, 1.0f};
        static const std::vector<std::string> options_str = {"0.2", "0.4", "0.6", "0.8", "1.0"};
        int selection = -1;
        for (size_t i = 0; i < options.size(); i++) {
            if (options[i] == m_new_point_head_diameter) { selection = (int)i; }
        }

        bool old_combo_state = m_combo_box_open;
        // The combo is commented out for now, until the feature is supported by backend.
        // m_combo_box_open = m_imgui->combo(_(L("Head diameter")), options_str, selection);
        force_refresh |= (old_combo_state != m_combo_box_open);

        // float current_number = atof(str);
        const float current_number = selection < options.size() ? options[selection] : m_new_point_head_diameter;
        if (old_combo_state && !m_combo_box_open) // closing the combo must always change the sizes (even if the selection did not change)
            for (auto& point_and_selection : m_editing_mode_cache)
                if (point_and_selection.second) {
                    point_and_selection.first.head_front_radius = current_number / 2.f;
                    m_unsaved_changes = true;
                }

        if (std::abs(current_number - m_new_point_head_diameter) > 0.001) {
            force_refresh = true;
            m_new_point_head_diameter = current_number;
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

        std::vector<ConfigOption*> opts = get_config_options({"support_points_density_relative", "support_points_minimal_distance"});
        float density = static_cast<ConfigOptionInt*>(opts[0])->value;
        float minimal_point_distance = static_cast<ConfigOptionFloat*>(opts[1])->value;

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

    m_imgui->end();

    if (m_editing_mode != m_old_editing_state) { // user toggled between editing/non-editing mode
        m_parent.toggle_sla_auxiliaries_visibility(!m_editing_mode);
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
#endif // ENABLE_IMGUI

bool GLGizmoSlaSupports::on_is_activable(const GLCanvas3D::Selection& selection) const
{
    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA
        || !selection.is_from_single_instance())
            return false;

    // Check that none of the selected volumes is outside.
    const GLCanvas3D::Selection::IndicesList& list = selection.get_volume_idxs();
    for (const auto& idx : list)
        if (selection.get_volume(idx)->is_outside)
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
    if (m_state == On) {
        if (is_mesh_update_necessary())
            update_mesh();

        m_parent.toggle_model_objects_visibility(false);
        if (m_model_object)
            m_parent.toggle_model_objects_visibility(true, m_model_object, m_active_instance);
    }
    if (m_state == Off) {
        if (m_old_state != Off) { // the gizmo was just turned Off

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
        }
    }
    m_old_state = m_state;
}



void GLGizmoSlaSupports::on_start_dragging(const GLCanvas3D::Selection& selection)
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
            point_and_selection.second = ( i == AllPoints );
        m_selection_empty = (i == NoPoints);
    }
    else {
        m_editing_mode_cache[i].second = true;
        m_selection_empty = false;
    }
}



void GLGizmoSlaSupports::editing_mode_discard_changes()
{
    m_editing_mode_cache.clear();
    for (const sla::SupportPoint& point : m_model_object->sla_support_points)
        m_editing_mode_cache.push_back(std::make_pair(point, false));
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
        for (const std::pair<sla::SupportPoint, bool>& point_and_selection : m_editing_mode_cache)
            m_model_object->sla_support_points.push_back(point_and_selection.first);

        // Recalculate support structures once the editing mode is left.
        // m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
        // m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
        wxGetApp().plater()->reslice_SLA_supports(*m_model_object);
    }
    m_editing_mode = false;
    m_unsaved_changes = false;
}



void GLGizmoSlaSupports::editing_mode_reload_cache()
{
    m_editing_mode_cache.clear();
    for (const sla::SupportPoint& point : m_model_object->sla_support_points)
        m_editing_mode_cache.push_back(std::make_pair(point, false));

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
        wxGetApp().plater()->reslice_SLA_supports(*m_model_object);
    }
}



void GLGizmoSlaSupports::switch_to_editing_mode()
{
    if (m_model_object->sla_points_status != sla::PointsStatus::AutoGenerated)
        editing_mode_reload_cache();
    m_unsaved_changes = false;
    m_editing_mode = true;
}






// GLGizmoCut

class GLGizmoCutPanel : public wxPanel
{
public:
    GLGizmoCutPanel(wxWindow *parent);

    void display(bool display);
private:
    bool m_active;
    wxCheckBox *m_cb_rotate;
    wxButton *m_btn_cut;
    wxButton *m_btn_cancel;
};

GLGizmoCutPanel::GLGizmoCutPanel(wxWindow *parent)
    : wxPanel(parent)
    , m_active(false)
    , m_cb_rotate(new wxCheckBox(this, wxID_ANY, _(L("Rotate lower part upwards"))))
    , m_btn_cut(new wxButton(this, wxID_OK, _(L("Perform cut"))))
    , m_btn_cancel(new wxButton(this, wxID_CANCEL, _(L("Cancel"))))
{
    enum { MARGIN = 5 };

    auto *sizer = new wxBoxSizer(wxHORIZONTAL);    

    auto *label = new wxStaticText(this, wxID_ANY, _(L("Cut object:")));
    sizer->Add(label, 0, wxALL | wxALIGN_CENTER, MARGIN);
    sizer->Add(m_cb_rotate, 0, wxALL | wxALIGN_CENTER, MARGIN);
    sizer->AddStretchSpacer();
    sizer->Add(m_btn_cut, 0, wxALL | wxALIGN_CENTER, MARGIN);
    sizer->Add(m_btn_cancel, 0, wxALL | wxALIGN_CENTER, MARGIN);

    SetSizer(sizer);
}

void GLGizmoCutPanel::display(bool display)
{
    Show(display);
    GetParent()->Layout();
}


const double GLGizmoCut::Offset = 10.0;
const double GLGizmoCut::Margin = 20.0;
const std::array<float, 3> GLGizmoCut::GrabberColor = { 1.0, 0.5, 0.0 };

#if ENABLE_SVG_ICONS
GLGizmoCut::GLGizmoCut(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
#else
GLGizmoCut::GLGizmoCut(GLCanvas3D& parent, unsigned int sprite_id)
    : GLGizmoBase(parent, sprite_id)
#endif // ENABLE_SVG_ICONS
    , m_cut_z(0.0)
    , m_max_z(0.0)
#if !ENABLE_IMGUI
    , m_panel(nullptr)
#endif // not ENABLE_IMGUI
    , m_keep_upper(true)
    , m_keep_lower(true)
    , m_rotate_lower(false)
{}

#if !ENABLE_IMGUI
void GLGizmoCut::create_external_gizmo_widgets(wxWindow *parent)
{
    wxASSERT(m_panel == nullptr);

    m_panel = new GLGizmoCutPanel(parent);
    parent->GetSizer()->Add(m_panel, 0, wxEXPAND);

    parent->Layout();
    parent->Fit();
    auto prev_heigh = parent->GetMinSize().GetHeight();
    parent->SetMinSize(wxSize(-1, std::max(prev_heigh, m_panel->GetSize().GetHeight())));

    m_panel->Hide();
    m_panel->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
        perform_cut(m_parent.get_selection());
    }, wxID_OK);
}
#endif // not ENABLE_IMGUI

bool GLGizmoCut::on_init()
{
    m_grabbers.emplace_back();
    m_shortcut_key = WXK_CONTROL_C;
    return true;
}

std::string GLGizmoCut::on_get_name() const
{
    return L("Cut [C]");
}

void GLGizmoCut::on_set_state()
{
    // Reset m_cut_z on gizmo activation
    if (get_state() == On) {
        m_cut_z = m_parent.get_selection().get_bounding_box().size()(2) / 2.0;
    }

#if !ENABLE_IMGUI
    // Display or hide the extra panel
    if (m_panel != nullptr) {
        m_panel->display(get_state() == On);
    }
#endif // not ENABLE_IMGUI
}

bool GLGizmoCut::on_is_activable(const GLCanvas3D::Selection& selection) const
{
    return selection.is_single_full_instance() && !selection.is_wipe_tower();
}

void GLGizmoCut::on_start_dragging(const GLCanvas3D::Selection& selection)
{
    if (m_hover_id == -1) { return; }

    const BoundingBoxf3& box = selection.get_bounding_box();
    m_start_z = m_cut_z;
    update_max_z(selection);
    m_drag_pos = m_grabbers[m_hover_id].center;
    m_drag_center = box.center();
    m_drag_center(2) = m_cut_z;
}

void GLGizmoCut::on_update(const UpdateData& data, const GLCanvas3D::Selection& selection)
{
    if (m_hover_id != -1) {
        set_cut_z(m_start_z + calc_projection(data.mouse_ray));
    }
}

void GLGizmoCut::on_render(const GLCanvas3D::Selection& selection) const
{
    if (m_grabbers[0].dragging) {
        set_tooltip("Z: " + format(m_cut_z, 2));
    }

    update_max_z(selection);

    const BoundingBoxf3& box = selection.get_bounding_box();
    Vec3d plane_center = box.center();
    plane_center(2) = m_cut_z;

    const float min_x = box.min(0) - Margin;
    const float max_x = box.max(0) + Margin;
    const float min_y = box.min(1) - Margin;
    const float max_y = box.max(1) + Margin;
    ::glEnable(GL_DEPTH_TEST);
    ::glDisable(GL_CULL_FACE);
    ::glEnable(GL_BLEND);
    ::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Draw the cutting plane
    ::glBegin(GL_QUADS);
    ::glColor4f(0.8f, 0.8f, 0.8f, 0.5f);
    ::glVertex3f(min_x, min_y, plane_center(2));
    ::glVertex3f(max_x, min_y, plane_center(2));
    ::glVertex3f(max_x, max_y, plane_center(2));
    ::glVertex3f(min_x, max_y, plane_center(2));
    ::glEnd();

    ::glEnable(GL_CULL_FACE);
    ::glDisable(GL_BLEND);

    // TODO: draw cut part contour?

    // Draw the grabber and the connecting line
    m_grabbers[0].center = plane_center;
    m_grabbers[0].center(2) = plane_center(2) + Offset;

    ::glDisable(GL_DEPTH_TEST);
    ::glLineWidth(m_hover_id != -1 ? 2.0f : 1.5f);
    ::glColor3f(1.0, 1.0, 0.0);
    ::glBegin(GL_LINES);
    ::glVertex3dv(plane_center.data());
    ::glVertex3dv(m_grabbers[0].center.data());
    ::glEnd();

    std::copy(std::begin(GrabberColor), std::end(GrabberColor), m_grabbers[0].color);
    m_grabbers[0].render(m_hover_id == 0, box.max_size());
}

void GLGizmoCut::on_render_for_picking(const GLCanvas3D::Selection& selection) const
{
    ::glDisable(GL_DEPTH_TEST);

    render_grabbers_for_picking(selection.get_bounding_box());
}

#if ENABLE_IMGUI
void GLGizmoCut::on_render_input_window(float x, float y, float bottom_limit, const GLCanvas3D::Selection& selection)
{
    m_imgui->set_next_window_pos(x, y, ImGuiCond_Always);
    m_imgui->set_next_window_bg_alpha(0.5f);
    m_imgui->begin(_(L("Cut")), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    ImGui::PushItemWidth(100.0f);
    bool _value_changed = ImGui::InputDouble("Z", &m_cut_z, 0.0f, 0.0f, "%.2f");

    m_imgui->checkbox(_(L("Keep upper part")), m_keep_upper);
    m_imgui->checkbox(_(L("Keep lower part")), m_keep_lower);
    m_imgui->checkbox(_(L("Rotate lower part upwards")), m_rotate_lower);

    m_imgui->disabled_begin(!m_keep_upper && !m_keep_lower);
    const bool cut_clicked = m_imgui->button(_(L("Perform cut")));
    m_imgui->disabled_end();

    m_imgui->end();

    if (cut_clicked && (m_keep_upper || m_keep_lower)) {
        perform_cut(selection);
    }
}
#endif // ENABLE_IMGUI

void GLGizmoCut::update_max_z(const GLCanvas3D::Selection& selection) const
{
    m_max_z = selection.get_bounding_box().size()(2);
    set_cut_z(m_cut_z);
}

void GLGizmoCut::set_cut_z(double cut_z) const
{
    // Clamp the plane to the object's bounding box
    m_cut_z = std::max(0.0, std::min(m_max_z, cut_z));
}

void GLGizmoCut::perform_cut(const GLCanvas3D::Selection& selection)
{
    const auto instance_idx = selection.get_instance_idx();
    const auto object_idx = selection.get_object_idx();

    wxCHECK_RET(instance_idx >= 0 && object_idx >= 0, "GLGizmoCut: Invalid object selection");

    wxGetApp().plater()->cut(object_idx, instance_idx, m_cut_z, m_keep_upper, m_keep_lower, m_rotate_lower);
}

double GLGizmoCut::calc_projection(const Linef3& mouse_ray) const
{
    double projection = 0.0;

    const Vec3d starting_vec = m_drag_pos - m_drag_center;
    const double len_starting_vec = starting_vec.norm();
    if (len_starting_vec != 0.0)
    {
        Vec3d mouse_dir = mouse_ray.unit_vector();
        // finds the intersection of the mouse ray with the plane parallel to the camera viewport and passing throught the starting position
        // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection algebric form
        // in our case plane normal and ray direction are the same (orthogonal view)
        // when moving to perspective camera the negative z unit axis of the camera needs to be transformed in world space and used as plane normal
        Vec3d inters = mouse_ray.a + (m_drag_pos - mouse_ray.a).dot(mouse_dir) / mouse_dir.squaredNorm() * mouse_dir;
        // vector from the starting position to the found intersection
        Vec3d inters_vec = inters - m_drag_pos;

        // finds projection of the vector along the staring direction
        projection = inters_vec.dot(starting_vec.normalized());
    }
    return projection;
}


} // namespace GUI
} // namespace Slic3r
