#include "GLGizmo.hpp"

#include "../../libslic3r/Utils.hpp"
#include "../../slic3r/GUI/GLCanvas3D.hpp"

#include <Eigen/Dense>
#include "../../libslic3r/Geometry.hpp"

#include <GL/glew.h>

#include <iostream>
#include <numeric>

static const float DEFAULT_BASE_COLOR[3] = { 0.625f, 0.625f, 0.625f };
static const float DEFAULT_DRAG_COLOR[3] = { 1.0f, 1.0f, 1.0f };
static const float DEFAULT_HIGHLIGHT_COLOR[3] = { 1.0f, 0.38f, 0.0f };

static const float AXES_COLOR[3][3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } };

namespace Slic3r {
namespace GUI {

const float GLGizmoBase::Grabber::HalfSize = 2.0f;
const float GLGizmoBase::Grabber::DraggingScaleFactor = 1.25f;

GLGizmoBase::Grabber::Grabber()
    : center(Vec3d::Zero())
    , angles(Vec3d::Zero())
    , dragging(false)
{
    color[0] = 1.0f;
    color[1] = 1.0f;
    color[2] = 1.0f;
}

void GLGizmoBase::Grabber::render(bool hover) const
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

    render(render_color, true);
}

void GLGizmoBase::Grabber::render(const float* render_color, bool use_lighting) const
{
    float half_size = dragging ? HalfSize * DraggingScaleFactor : HalfSize;
    if (use_lighting)
        ::glEnable(GL_LIGHTING);

    ::glColor3f((GLfloat)render_color[0], (GLfloat)render_color[1], (GLfloat)render_color[2]);

    ::glPushMatrix();
    ::glTranslatef((GLfloat)center(0), (GLfloat)center(1), (GLfloat)center(2));

    float rad_to_deg = 180.0f / (GLfloat)PI;
    ::glRotatef((GLfloat)angles(0) * rad_to_deg, 1.0f, 0.0f, 0.0f);
    ::glRotatef((GLfloat)angles(1) * rad_to_deg, 0.0f, 1.0f, 0.0f);
    ::glRotatef((GLfloat)angles(2) * rad_to_deg, 0.0f, 0.0f, 1.0f);

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

GLGizmoBase::GLGizmoBase(GLCanvas3D& parent)
    : m_parent(parent)
    , m_group_id(-1)
    , m_state(Off)
    , m_hover_id(-1)
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

void GLGizmoBase::start_dragging()
{
    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        m_grabbers[i].dragging = (m_hover_id == i);
    }

    on_start_dragging();
}

void GLGizmoBase::stop_dragging()
{
    set_tooltip("");

    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        m_grabbers[i].dragging = false;
    }

    on_stop_dragging();
}

void GLGizmoBase::update(const Linef3& mouse_ray)
{
    if (m_hover_id != -1)
        on_update(mouse_ray);
}

float GLGizmoBase::picking_color_component(unsigned int id) const
{
    int color = 254 - (int)id;
    if (m_group_id > -1)
        color -= m_group_id;

    return (float)color / 255.0f;
}

void GLGizmoBase::render_grabbers() const
{
    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        m_grabbers[i].render(m_hover_id == i);
    }
}

void GLGizmoBase::render_grabbers_for_picking() const
{
    for (unsigned int i = 0; i < (unsigned int)m_grabbers.size(); ++i)
    {
        m_grabbers[i].color[0] = 1.0f;
        m_grabbers[i].color[1] = 1.0f;
        m_grabbers[i].color[2] = picking_color_component(i);
        m_grabbers[i].render_for_picking();
    }
}

void GLGizmoBase::set_tooltip(const std::string& tooltip) const
{
    m_parent.set_tooltip(tooltip);
}

std::string GLGizmoBase::format(float value, unsigned int decimals) const
{
    char buf[1024];
    ::sprintf(buf, "%.*f", decimals, value);
    return buf;
}

const float GLGizmoRotate::Offset = 5.0f;
const unsigned int GLGizmoRotate::CircleResolution = 64;
const unsigned int GLGizmoRotate::AngleResolution = 64;
const unsigned int GLGizmoRotate::ScaleStepsCount = 72;
const float GLGizmoRotate::ScaleStepRad = 2.0f * (float)PI / GLGizmoRotate::ScaleStepsCount;
const unsigned int GLGizmoRotate::ScaleLongEvery = 2;
const float GLGizmoRotate::ScaleLongTooth = 2.0f;
const float GLGizmoRotate::ScaleShortTooth = 1.0f;
const unsigned int GLGizmoRotate::SnapRegionsCount = 8;
const float GLGizmoRotate::GrabberOffset = 5.0f;

GLGizmoRotate::GLGizmoRotate(GLCanvas3D& parent, GLGizmoRotate::Axis axis)
    : GLGizmoBase(parent)
    , m_axis(axis)
    , m_angle(0.0)
    , m_center(0.0, 0.0, 0.0)
    , m_radius(0.0f)
    , m_keep_initial_values(false)
{
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

void GLGizmoRotate::on_update(const Linef3& mouse_ray)
{ 
    Vec2d mouse_pos = to_2d(mouse_position_in_local_plane(mouse_ray));

    Vec2d orig_dir = Vec2d::UnitX();
    Vec2d new_dir = mouse_pos.normalized();

    double theta = ::acos(clamp(-1.0, 1.0, new_dir.dot(orig_dir)));
    if (cross2(orig_dir, new_dir) < 0.0)
        theta = 2.0 * (double)PI - theta;

    double len = mouse_pos.norm();

    // snap to snap region
    double in_radius = (double)m_radius / 3.0;
    double out_radius = 2.0 * (double)in_radius;
    if ((in_radius <= len) && (len <= out_radius))
    {
        double step = 2.0 * (double)PI / (double)SnapRegionsCount;
        theta = step * (double)std::round(theta / step);
    }
    else
    {
        // snap to scale
        in_radius = (double)m_radius;
        out_radius = in_radius + (double)ScaleLongTooth;
        if ((in_radius <= len) && (len <= out_radius))
        {
            double step = 2.0 * (double)PI / (double)ScaleStepsCount;
            theta = step * (double)std::round(theta / step);
        }
    }

    if (theta == 2.0 * (double)PI)
        theta = 0.0;

    m_angle = theta;
}

void GLGizmoRotate::on_render(const BoundingBoxf3& box) const
{
    if (m_grabbers[0].dragging)
        set_tooltip(format(m_angle * 180.0f / (float)PI, 4));

    ::glEnable(GL_DEPTH_TEST);

    if (!m_keep_initial_values)
    {
        m_center = box.center();
        m_radius = Offset + box.radius();
        m_keep_initial_values = true;
    }

    ::glPushMatrix();
    transform_to_local();

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

    render_grabber();

    ::glPopMatrix();
}

void GLGizmoRotate::on_render_for_picking(const BoundingBoxf3& box) const
{
    ::glDisable(GL_DEPTH_TEST);

    ::glPushMatrix();

    transform_to_local();
    render_grabbers_for_picking();

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
    float out_radius_long = m_radius + ScaleLongTooth;
    float out_radius_short = m_radius + ScaleShortTooth;

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
    ::glVertex3f((GLfloat)(m_radius + GrabberOffset), 0.0f, 0.0f);
    ::glEnd();
}

void GLGizmoRotate::render_angle() const
{
    float step_angle = (float)m_angle / AngleResolution;
    float ex_radius = m_radius + GrabberOffset;

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

void GLGizmoRotate::render_grabber() const
{
    double grabber_radius = (double)(m_radius + GrabberOffset);
    m_grabbers[0].center = Vec3d(::cos(m_angle) * grabber_radius, ::sin(m_angle) * grabber_radius, 0.0);
    m_grabbers[0].angles(2) = m_angle;

    ::glColor3fv((m_hover_id != -1) ? m_drag_color : m_highlight_color);

    ::glBegin(GL_LINES);
    ::glVertex3f(0.0f, 0.0f, 0.0f);
    ::glVertex3f((GLfloat)m_grabbers[0].center(0), (GLfloat)m_grabbers[0].center(1), (GLfloat)m_grabbers[0].center(2));
    ::glEnd();

    ::memcpy((void*)m_grabbers[0].color, (const void*)m_highlight_color, 3 * sizeof(float));
    render_grabbers();
}

void GLGizmoRotate::transform_to_local() const
{
    ::glTranslatef((GLfloat)m_center(0), (GLfloat)m_center(1), (GLfloat)m_center(2));

    switch (m_axis)
    {
    case X:
    {
        ::glRotatef(90.0f, 0.0f, 1.0f, 0.0f);
        ::glRotatef(90.0f, 0.0f, 0.0f, 1.0f);
        break;
    }
    case Y:
    {
        ::glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
        ::glRotatef(180.0f, 0.0f, 0.0f, 1.0f);
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

Vec3d GLGizmoRotate::mouse_position_in_local_plane(const Linef3& mouse_ray) const
{
    double half_pi = 0.5 * (double)PI;

    Transform3d m = Transform3d::Identity();

    switch (m_axis)
    {
    case X:
    {
        m.rotate(Eigen::AngleAxisd(-half_pi, Vec3d::UnitZ()));
        m.rotate(Eigen::AngleAxisd(-half_pi, Vec3d::UnitY()));
        break;
    }
    case Y:
    {
        m.rotate(Eigen::AngleAxisd(-(double)PI, Vec3d::UnitZ()));
        m.rotate(Eigen::AngleAxisd(half_pi, Vec3d::UnitX()));
        break;
    }
    default:
    case Z:
    {
        // no rotation applied
        break;
    }
    }

    m.translate(-m_center);

    return transform(mouse_ray, m).intersect_plane(0.0);
}

GLGizmoRotate3D::GLGizmoRotate3D(GLCanvas3D& parent)
    : GLGizmoBase(parent)
    , m_x(parent, GLGizmoRotate::X)
    , m_y(parent, GLGizmoRotate::Y)
    , m_z(parent, GLGizmoRotate::Z)
{
    m_x.set_group_id(0);
    m_y.set_group_id(1);
    m_z.set_group_id(2);
}

bool GLGizmoRotate3D::on_init()
{
    if (!m_x.init() || !m_y.init() || !m_z.init())
        return false;

    m_x.set_highlight_color(AXES_COLOR[0]);
    m_y.set_highlight_color(AXES_COLOR[1]);
    m_z.set_highlight_color(AXES_COLOR[2]);

    std::string path = resources_dir() + "/icons/overlay/";

    std::string filename = path + "rotate_off.png";
    if (!m_textures[Off].load_from_file(filename, false))
        return false;

    filename = path + "rotate_hover.png";
    if (!m_textures[Hover].load_from_file(filename, false))
        return false;

    filename = path + "rotate_on.png";
    if (!m_textures[On].load_from_file(filename, false))
        return false;

    return true;
}

void GLGizmoRotate3D::on_start_dragging()
{
    switch (m_hover_id)
    {
    case 0:
    {
        m_x.start_dragging();
        break;
    }
    case 1:
    {
        m_y.start_dragging();
        break;
    }
    case 2:
    {
        m_z.start_dragging();
        break;
    }
    default:
    {
        break;
    }
    }
}

void GLGizmoRotate3D::on_stop_dragging()
{
    switch (m_hover_id)
    {
    case 0:
    {
        m_x.stop_dragging();
        break;
    }
    case 1:
    {
        m_y.stop_dragging();
        break;
    }
    case 2:
    {
        m_z.stop_dragging();
        break;
    }
    default:
    {
        break;
    }
    }
}

void GLGizmoRotate3D::on_render(const BoundingBoxf3& box) const
{
    if ((m_hover_id == -1) || (m_hover_id == 0))
        m_x.render(box);

    if ((m_hover_id == -1) || (m_hover_id == 1))
        m_y.render(box);

    if ((m_hover_id == -1) || (m_hover_id == 2))
        m_z.render(box);
}

const float GLGizmoScale3D::Offset = 5.0f;

GLGizmoScale3D::GLGizmoScale3D(GLCanvas3D& parent)
    : GLGizmoBase(parent)
    , m_scale(Vec3d::Ones())
    , m_starting_scale(Vec3d::Ones())
    , m_show_starting_box(false)
{
}

bool GLGizmoScale3D::on_init()
{
    std::string path = resources_dir() + "/icons/overlay/";

    std::string filename = path + "scale_off.png";
    if (!m_textures[Off].load_from_file(filename, false))
        return false;

    filename = path + "scale_hover.png";
    if (!m_textures[Hover].load_from_file(filename, false))
        return false;

    filename = path + "scale_on.png";
    if (!m_textures[On].load_from_file(filename, false))
        return false;

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

    return true;
}

void GLGizmoScale3D::on_start_dragging()
{
    if (m_hover_id != -1)
    {
        m_starting_drag_position = m_grabbers[m_hover_id].center;
        m_show_starting_box = true;
        m_starting_box = m_box;
    }
}

void GLGizmoScale3D::on_update(const Linef3& mouse_ray)
{
    if ((m_hover_id == 0) || (m_hover_id == 1))
        do_scale_x(mouse_ray);
    else if ((m_hover_id == 2) || (m_hover_id == 3))
        do_scale_y(mouse_ray);
    else if ((m_hover_id == 4) || (m_hover_id == 5))
        do_scale_z(mouse_ray);
    else if (m_hover_id >= 6)
        do_scale_uniform(mouse_ray);
}

void GLGizmoScale3D::on_render(const BoundingBoxf3& box) const
{
    if (m_grabbers[0].dragging || m_grabbers[1].dragging)
        set_tooltip("X: " + format(100.0f * m_scale(0), 4) + "%");
    else if (m_grabbers[2].dragging || m_grabbers[3].dragging)
        set_tooltip("Y: " + format(100.0f * m_scale(1), 4) + "%");
    else if (m_grabbers[4].dragging || m_grabbers[5].dragging)
        set_tooltip("Z: " + format(100.0f * m_scale(2), 4) + "%");
    else if (m_grabbers[6].dragging || m_grabbers[7].dragging || m_grabbers[8].dragging || m_grabbers[9].dragging)
    {
        std::string tooltip = "X: " + format(100.0f * m_scale(0), 4) + "%\n";
        tooltip += "Y: " + format(100.0f * m_scale(1), 4) + "%\n";
        tooltip += "Z: " + format(100.0f * m_scale(2), 4) + "%";
        set_tooltip(tooltip);
    }

    ::glEnable(GL_DEPTH_TEST);

    Vec3d offset_vec = (double)Offset * Vec3d::Ones();

    m_box = BoundingBoxf3(box.min - offset_vec, box.max + offset_vec);
    const Vec3d& center = m_box.center();

    // x axis
    m_grabbers[0].center = Vec3d(m_box.min(0), center(1), center(2));
    m_grabbers[1].center = Vec3d(m_box.max(0), center(1), center(2));
    ::memcpy((void*)m_grabbers[0].color, (const void*)&AXES_COLOR[0], 3 * sizeof(float));
    ::memcpy((void*)m_grabbers[1].color, (const void*)&AXES_COLOR[0], 3 * sizeof(float));

    // y axis
    m_grabbers[2].center = Vec3d(center(0), m_box.min(1), center(2));
    m_grabbers[3].center = Vec3d(center(0), m_box.max(1), center(2));
    ::memcpy((void*)m_grabbers[2].color, (const void*)&AXES_COLOR[1], 3 * sizeof(float));
    ::memcpy((void*)m_grabbers[3].color, (const void*)&AXES_COLOR[1], 3 * sizeof(float));

    // z axis
    m_grabbers[4].center = Vec3d(center(0), center(1), m_box.min(2));
    m_grabbers[5].center = Vec3d(center(0), center(1), m_box.max(2));
    ::memcpy((void*)m_grabbers[4].color, (const void*)&AXES_COLOR[2], 3 * sizeof(float));
    ::memcpy((void*)m_grabbers[5].color, (const void*)&AXES_COLOR[2], 3 * sizeof(float));

    // uniform
    m_grabbers[6].center = Vec3d(m_box.min(0), m_box.min(1), m_box.min(2));
    m_grabbers[7].center = Vec3d(m_box.max(0), m_box.min(1), m_box.min(2));
    m_grabbers[8].center = Vec3d(m_box.max(0), m_box.max(1), m_box.min(2));
    m_grabbers[9].center = Vec3d(m_box.min(0), m_box.max(1), m_box.min(2));
    for (int i = 6; i < 10; ++i)
    {
        ::memcpy((void*)m_grabbers[i].color, (const void*)m_highlight_color, 3 * sizeof(float));
    }

    ::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f);

    if (m_hover_id == -1)
    {
        // draw box
        ::glColor3fv(m_base_color);
        render_box(m_box);
        // draw connections
        ::glColor3fv(m_grabbers[0].color);
        render_grabbers_connection(0, 1);
        ::glColor3fv(m_grabbers[2].color);
        render_grabbers_connection(2, 3);
        ::glColor3fv(m_grabbers[4].color);
        render_grabbers_connection(4, 5);
        // draw grabbers
        render_grabbers();
    }
    else if ((m_hover_id == 0) || (m_hover_id == 1))
    {
        // draw starting box
        if (m_show_starting_box)
        {
            ::glColor3fv(m_base_color);
            render_box(m_starting_box);
        }
        // draw current box
        ::glColor3fv(m_drag_color);
        render_box(m_box);
        // draw connection
        ::glColor3fv(m_grabbers[0].color);
        render_grabbers_connection(0, 1);
        // draw grabbers
        m_grabbers[0].render(true);
        m_grabbers[1].render(true);
    }
    else if ((m_hover_id == 2) || (m_hover_id == 3))
    {
        // draw starting box
        if (m_show_starting_box)
        {
            ::glColor3fv(m_base_color);
            render_box(m_starting_box);
        }
        // draw current box
        ::glColor3fv(m_drag_color);
        render_box(m_box);
        // draw connection
        ::glColor3fv(m_grabbers[2].color);
        render_grabbers_connection(2, 3);
        // draw grabbers
        m_grabbers[2].render(true);
        m_grabbers[3].render(true);
    }
    else if ((m_hover_id == 4) || (m_hover_id == 5))
    {
        // draw starting box
        if (m_show_starting_box)
        {
            ::glColor3fv(m_base_color);
            render_box(m_starting_box);
        }
        // draw current box
        ::glColor3fv(m_drag_color);
        render_box(m_box);
        // draw connection
        ::glColor3fv(m_grabbers[4].color);
        render_grabbers_connection(4, 5);
        // draw grabbers
        m_grabbers[4].render(true);
        m_grabbers[5].render(true);
    }
    else if (m_hover_id >= 6)
    {
        // draw starting box
        if (m_show_starting_box)
        {
            ::glColor3fv(m_base_color);
            render_box(m_starting_box);
        }
        // draw current box
        ::glColor3fv(m_drag_color);
        render_box(m_box);
        // draw grabbers
        for (int i = 6; i < 10; ++i)
        {
            m_grabbers[i].render(true);
        }
    }
}

void GLGizmoScale3D::on_render_for_picking(const BoundingBoxf3& box) const
{
    ::glDisable(GL_DEPTH_TEST);

    render_grabbers_for_picking();
}

void GLGizmoScale3D::render_box(const BoundingBoxf3& box) const
{
    // bottom face
    ::glBegin(GL_LINE_LOOP);
    ::glVertex3f((GLfloat)box.min(0), (GLfloat)box.min(1), (GLfloat)box.min(2));
    ::glVertex3f((GLfloat)box.min(0), (GLfloat)box.max(1), (GLfloat)box.min(2));
    ::glVertex3f((GLfloat)box.max(0), (GLfloat)box.max(1), (GLfloat)box.min(2));
    ::glVertex3f((GLfloat)box.max(0), (GLfloat)box.min(1), (GLfloat)box.min(2));
    ::glEnd();

    // top face
    ::glBegin(GL_LINE_LOOP);
    ::glVertex3f((GLfloat)box.min(0), (GLfloat)box.min(1), (GLfloat)box.max(2));
    ::glVertex3f((GLfloat)box.min(0), (GLfloat)box.max(1), (GLfloat)box.max(2));
    ::glVertex3f((GLfloat)box.max(0), (GLfloat)box.max(1), (GLfloat)box.max(2));
    ::glVertex3f((GLfloat)box.max(0), (GLfloat)box.min(1), (GLfloat)box.max(2));
    ::glEnd();

    // vertical edges
    ::glBegin(GL_LINES);
    ::glVertex3f((GLfloat)box.min(0), (GLfloat)box.min(1), (GLfloat)box.min(2)); ::glVertex3f((GLfloat)box.min(0), (GLfloat)box.min(1), (GLfloat)box.max(2));
    ::glVertex3f((GLfloat)box.min(0), (GLfloat)box.max(1), (GLfloat)box.min(2)); ::glVertex3f((GLfloat)box.min(0), (GLfloat)box.max(1), (GLfloat)box.max(2));
    ::glVertex3f((GLfloat)box.max(0), (GLfloat)box.max(1), (GLfloat)box.min(2)); ::glVertex3f((GLfloat)box.max(0), (GLfloat)box.max(1), (GLfloat)box.max(2));
    ::glVertex3f((GLfloat)box.max(0), (GLfloat)box.min(1), (GLfloat)box.min(2)); ::glVertex3f((GLfloat)box.max(0), (GLfloat)box.min(1), (GLfloat)box.max(2));
    ::glEnd();
}

void GLGizmoScale3D::render_grabbers_connection(unsigned int id_1, unsigned int id_2) const
{
    unsigned int grabbers_count = (unsigned int)m_grabbers.size();
    if ((id_1 < grabbers_count) && (id_2 < grabbers_count))
    {
        ::glBegin(GL_LINES);
        ::glVertex3f((GLfloat)m_grabbers[id_1].center(0), (GLfloat)m_grabbers[id_1].center(1), (GLfloat)m_grabbers[id_1].center(2));
        ::glVertex3f((GLfloat)m_grabbers[id_2].center(0), (GLfloat)m_grabbers[id_2].center(1), (GLfloat)m_grabbers[id_2].center(2));
        ::glEnd();
    }
}

void GLGizmoScale3D::do_scale_x(const Linef3& mouse_ray)
{
    double ratio = calc_ratio(1, mouse_ray, m_starting_box.center());

    if (ratio > 0.0)
        m_scale(0) = m_starting_scale(0) * ratio;
}

void GLGizmoScale3D::do_scale_y(const Linef3& mouse_ray)
{
    double ratio = calc_ratio(2, mouse_ray, m_starting_box.center());

    if (ratio > 0.0)
        m_scale(0) = m_starting_scale(1) * ratio; // << this is temporary
//        m_scale(1) = m_starting_scale(1) * ratio;
}

void GLGizmoScale3D::do_scale_z(const Linef3& mouse_ray)
{
    double ratio = calc_ratio(1, mouse_ray, m_starting_box.center());

    if (ratio > 0.0)
        m_scale(0) = m_starting_scale(2) * ratio; // << this is temporary
//        m_scale(2) = m_starting_scale(2) * ratio;
}

void GLGizmoScale3D::do_scale_uniform(const Linef3& mouse_ray)
{
    Vec3d center = m_starting_box.center();
    center(2) = m_box.min(2);
    double ratio = calc_ratio(0, mouse_ray, center);

    if (ratio > 0.0)
        m_scale = m_starting_scale * ratio;
}

double GLGizmoScale3D::calc_ratio(unsigned int preferred_plane_id, const Linef3& mouse_ray, const Vec3d& center) const
{
    double ratio = 0.0;

    Vec3d starting_vec = m_starting_drag_position - center;
    double len_starting_vec = starting_vec.norm();
    if (len_starting_vec == 0.0)
        return ratio;

    Vec3d starting_vec_dir = starting_vec.normalized();
    Vec3d mouse_dir = mouse_ray.unit_vector();
    unsigned int plane_id = preferred_plane_id;

    // 1st try to see if the mouse direction is close enough to the preferred plane normal
    double dot_to_normal = 0.0;
    switch (plane_id)
    {
    case 0:
    {
        dot_to_normal = std::abs(mouse_dir.dot(Vec3d::UnitZ()));
        break;
    }
    case 1:
    {
        dot_to_normal = std::abs(mouse_dir.dot(-Vec3d::UnitY()));
        break;
    }
    case 2:
    {
        dot_to_normal = std::abs(mouse_dir.dot(Vec3d::UnitX()));
        break;
    }
    }

    if (dot_to_normal < 0.1)
    {
        // if not, select the plane who's normal is closest to the mouse direction

        typedef std::map<double, unsigned int> ProjsMap;
        ProjsMap projs_map;

        projs_map.insert(ProjsMap::value_type(std::abs(mouse_dir.dot(Vec3d::UnitZ())), 0));  // plane xy
        projs_map.insert(ProjsMap::value_type(std::abs(mouse_dir.dot(-Vec3d::UnitY())), 1)); // plane xz
        projs_map.insert(ProjsMap::value_type(std::abs(mouse_dir.dot(Vec3d::UnitX())), 2));  // plane yz
        plane_id = projs_map.rbegin()->second;
    }

    switch (plane_id)
    {
    case 0:
    {
        // calculates the intersection of the mouse ray with the plane parallel to plane XY and passing through the given center
        Transform3d m = Transform3d::Identity();
        m.translate(-center);
        Vec2d mouse_pos_2d = to_2d(transform(mouse_ray, m).intersect_plane(0.0));

        // ratio is given by the projection of the calculated intersection on the starting vector divided by the starting vector length
        ratio = starting_vec_dir.dot(Vec3d(mouse_pos_2d(0), mouse_pos_2d(1), 0.0)) / len_starting_vec;
        break;
    }
    case 1:
    {
        // calculates the intersection of the mouse ray with the plane parallel to plane XZ and passing through the given center
        Transform3d m = Transform3d::Identity();
        m.rotate(Eigen::AngleAxisd(-0.5 * (double)PI, Vec3d::UnitX()));
        m.translate(-center);
        Vec2d mouse_pos_2d = to_2d(transform(mouse_ray, m).intersect_plane(0.0));

        // ratio is given by the projection of the calculated intersection on the starting vector divided by the starting vector length
        ratio = starting_vec_dir.dot(Vec3d(mouse_pos_2d(0), 0.0, mouse_pos_2d(1))) / len_starting_vec;
        break;
    }
    case 2:
    {
        // calculates the intersection of the mouse ray with the plane parallel to plane YZ and passing through the given center
        Transform3d m = Transform3d::Identity();
        m.rotate(Eigen::AngleAxisd(-0.5f * (double)PI, Vec3d::UnitY()));
        m.translate(-center);
        Vec2d mouse_pos_2d = to_2d(transform(mouse_ray, m).intersect_plane(0.0));

        // ratio is given by the projection of the calculated intersection on the starting vector divided by the starting vector length
        ratio = starting_vec_dir.dot(Vec3d(0.0, mouse_pos_2d(1), -mouse_pos_2d(0))) / len_starting_vec;
        break;
    }
    }

    return ratio;
}


GLGizmoFlatten::GLGizmoFlatten(GLCanvas3D& parent)
    : GLGizmoBase(parent)
    , m_normal(0.0, 0.0, 0.0)
{
}

bool GLGizmoFlatten::on_init()
{
    std::string path = resources_dir() + "/icons/overlay/";

    std::string filename = path + "layflat_off.png";
    if (!m_textures[Off].load_from_file(filename, false))
        return false;

    filename = path + "layflat_hover.png";
    if (!m_textures[Hover].load_from_file(filename, false))
        return false;

    filename = path + "layflat_on.png";
    if (!m_textures[On].load_from_file(filename, false))
        return false;

    return true;
}

void GLGizmoFlatten::on_start_dragging()
{
    if (m_hover_id != -1)
        m_normal = m_planes[m_hover_id].normal;
}

void GLGizmoFlatten::on_render(const BoundingBoxf3& box) const
{
    // the dragged_offset is a vector measuring where was the object moved
    // with the gizmo being on. This is reset in set_flattening_data and
    // does not work correctly when there are multiple copies.
    if (!m_center) // this is the first bounding box that we see
        m_center.reset(new Vec3d(box.center()));

    Vec3d dragged_offset = box.center() - *m_center;

    ::glEnable(GL_BLEND);
    ::glEnable(GL_DEPTH_TEST);

    for (int i=0; i<(int)m_planes.size(); ++i) {
        if (i == m_hover_id)
            ::glColor4f(0.9f, 0.9f, 0.9f, 0.75f);
        else
            ::glColor4f(0.9f, 0.9f, 0.9f, 0.5f);

        for (Vec2d offset : m_instances_positions) {
            offset += to_2d(dragged_offset);
            ::glPushMatrix();
            ::glTranslatef((GLfloat)offset(0), (GLfloat)offset(1), 0.0f);
            ::glBegin(GL_POLYGON);
            for (const Vec3d& vertex : m_planes[i].vertices)
                ::glVertex3f((GLfloat)vertex(0), (GLfloat)vertex(1), (GLfloat)vertex(2));
            ::glEnd();
            ::glPopMatrix();
        }
    }

    ::glDisable(GL_BLEND);
}

void GLGizmoFlatten::on_render_for_picking(const BoundingBoxf3& box) const
{
    ::glEnable(GL_DEPTH_TEST);

    for (unsigned int i = 0; i < m_planes.size(); ++i)
    {
        ::glColor3f(1.0f, 1.0f, picking_color_component(i));
        for (const Vec2d& offset : m_instances_positions) {
            ::glPushMatrix();
            ::glTranslatef((GLfloat)offset(0), (GLfloat)offset(1), 0.0f);
            ::glBegin(GL_POLYGON);
            for (const Vec3d& vertex : m_planes[i].vertices)
                ::glVertex3f((GLfloat)vertex(0), (GLfloat)vertex(1), (GLfloat)vertex(2));
            ::glEnd();
            ::glPopMatrix();
        }
    }
}

void GLGizmoFlatten::set_flattening_data(const ModelObject* model_object)
{
    m_center.release(); // object is not being dragged (this would not be called otherwise) - we must forget about the bounding box position...
    m_model_object = model_object;

    // ...and save the updated positions of the object instances:
    if (m_model_object && !m_model_object->instances.empty()) {
        m_instances_positions.clear();
        for (const auto* instance : m_model_object->instances)
            m_instances_positions.emplace_back(instance->offset);
    }

    if (is_plane_update_necessary())
        update_planes();
}

void GLGizmoFlatten::update_planes()
{
    TriangleMesh ch;
    for (const ModelVolume* vol : m_model_object->volumes)
        ch.merge(vol->get_convex_hull());
    ch = ch.convex_hull_3d();
    ch.scale(m_model_object->instances.front()->scaling_factor);
    ch.rotate_z(m_model_object->instances.front()->rotation);

    m_planes.clear();

    // Now we'll go through all the facets and append Points of facets sharing the same normal:
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
                    m_planes.back().vertices.emplace_back(first_vertex[j](0), first_vertex[j](1), first_vertex[j](2));

                facet_visited[facet_idx] = true;
                for (int j = 0; j < 3; ++ j) {
                    int neighbor_idx = ch.stl.neighbors_start[facet_idx].neighbor[j];
                    if (! facet_visited[neighbor_idx])
                        facet_queue[facet_queue_cnt ++] = neighbor_idx;
                }
            }
        }
        m_planes.back().normal = Vec3d((double)(*normal_ptr)(0), (double)(*normal_ptr)(1), (double)(*normal_ptr)(2));

        // if this is a just a very small triangle, remove it to speed up further calculations (it would be rejected anyway):
        if (m_planes.back().vertices.size() == 3 &&
               (m_planes.back().vertices[0] - m_planes.back().vertices[1]).norm() < 1.f
            || (m_planes.back().vertices[0] - m_planes.back().vertices[2]).norm() < 1.f)
            m_planes.pop_back();
    }

    // Now we'll go through all the polygons, transform the points into xy plane to process them:
    for (unsigned int polygon_id=0; polygon_id < m_planes.size(); ++polygon_id) {
        Pointf3s& polygon = m_planes[polygon_id].vertices;
        const Vec3d& normal = m_planes[polygon_id].normal;

        // We are going to rotate about z and y to flatten the plane
        float angle_z = 0.f;
        float angle_y = 0.f;
        if (std::abs(normal(1)) > 0.001)
            angle_z = -atan2(normal(1), normal(0)); // angle to rotate so that normal ends up in xz-plane
        if (std::abs(normal(0)*cos(angle_z) - normal(1)*sin(angle_z)) > 0.001)
            angle_y = -atan2(normal(0)*cos(angle_z) - normal(1)*sin(angle_z), normal(2)); // angle to rotate to make normal point upwards
        else {
            // In case it already was in z-direction, we must ensure it is not the wrong way:
            angle_y = normal(2) > 0.f ? 0.f : -PI;
        }

        // Rotate all points to the xy plane:
        Transform3d m = Transform3d::Identity();
        m.rotate(Eigen::AngleAxisd((double)angle_y, Vec3d::UnitY()));
        m.rotate(Eigen::AngleAxisd((double)angle_z, Vec3d::UnitZ()));
        polygon = transform(polygon, m);

        polygon = Slic3r::Geometry::convex_hull(polygon); // To remove the inner points

        // We will calculate area of the polygon and discard ones that are too small
        // The limit is more forgiving in case the normal is in the direction of the coordinate axes
        const float minimal_area = (std::abs(normal(0)) > 0.999f || std::abs(normal(1)) > 0.999f || std::abs(normal(2)) > 0.999f) ? 1.f : 20.f;
        float& area = m_planes[polygon_id].area;
        area = 0.f;
        for (unsigned int i = 0; i < polygon.size(); i++) // Shoelace formula
            area += polygon[i](0)*polygon[i + 1 < polygon.size() ? i + 1 : 0](1) - polygon[i + 1 < polygon.size() ? i + 1 : 0](0)*polygon[i](1);
        area = std::abs(area / 2.f);
        if (area < minimal_area) {
            m_planes.erase(m_planes.begin()+(polygon_id--));
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

        // Transform back to 3D;
        for (auto& b : polygon) {
            b(2) += 0.1f; // raise a bit above the object surface to avoid flickering
        }

        m = m.inverse();
        polygon = transform(polygon, m);
    }

    // We'll sort the planes by area and only keep the 254 largest ones (because of the picking pass limitations):
    std::sort(m_planes.rbegin(), m_planes.rend(), [](const PlaneData& a, const PlaneData& b) { return a.area < b.area; });
    m_planes.resize(std::min((int)m_planes.size(), 254));

    // Planes are finished - let's save what we calculated it from:
    m_source_data.bounding_boxes.clear();
    for (const auto& vol : m_model_object->volumes)
        m_source_data.bounding_boxes.push_back(vol->get_convex_hull().bounding_box());
    m_source_data.scaling_factor = m_model_object->instances.front()->scaling_factor;
    m_source_data.rotation = m_model_object->instances.front()->rotation;
    const float* first_vertex = m_model_object->volumes.front()->get_convex_hull().first_vertex();
    m_source_data.mesh_first_point = Vec3d((double)first_vertex[0], (double)first_vertex[1], (double)first_vertex[2]);
}

// Check if the bounding boxes of each volume's convex hull is the same as before
// and that scaling and rotation has not changed. In that case we don't have to recalculate it.
bool GLGizmoFlatten::is_plane_update_necessary() const
{
    if (m_state != On || !m_model_object || m_model_object->instances.empty())
        return false;

    if (m_model_object->volumes.size() != m_source_data.bounding_boxes.size()
     || m_model_object->instances.front()->scaling_factor != m_source_data.scaling_factor
     || m_model_object->instances.front()->rotation != m_source_data.rotation)
         return true;

    // now compare the bounding boxes:
    for (unsigned int i=0; i<m_model_object->volumes.size(); ++i)
        if (m_model_object->volumes[i]->get_convex_hull().bounding_box() != m_source_data.bounding_boxes[i])
            return true;

    const float* first_vertex = m_model_object->volumes.front()->get_convex_hull().first_vertex();
    Vec3d first_point((double)first_vertex[0], (double)first_vertex[1], (double)first_vertex[2]);
    if (first_point != m_source_data.mesh_first_point)
        return true;

    return false;
}

Vec3d GLGizmoFlatten::get_flattening_normal() const {
    Vec3d normal = m_model_object->instances.front()->world_matrix(true).matrix().block(0, 0, 3, 3).inverse() * m_normal;
    m_normal = Vec3d::Zero();
    return normal.normalized();
}

} // namespace GUI
} // namespace Slic3r
