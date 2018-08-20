#include "GLGizmo.hpp"

#include "../../libslic3r/Utils.hpp"
#include "../../libslic3r/BoundingBox.hpp"

#include <Eigen/Dense>

#include <GL/glew.h>

#include <iostream>

static const float DEFAULT_BASE_COLOR[3] = { 0.625f, 0.625f, 0.625f };
static const float DEFAULT_DRAG_COLOR[3] = { 1.0f, 1.0f, 1.0f };
static const float DEFAULT_HIGHLIGHT_COLOR[3] = { 1.0f, 0.38f, 0.0f };

namespace Slic3r {
namespace GUI {

const float GLGizmoBase::Grabber::HalfSize = 2.0f;
const float GLGizmoBase::Grabber::DraggingScaleFactor = 1.25f;

GLGizmoBase::Grabber::Grabber()
    : center(Pointf3(0.0, 0.0, 0.0))
    , angle_x(0.0f)
    , angle_y(0.0f)
    , angle_z(0.0f)
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

    render(render_color);
}

void GLGizmoBase::Grabber::render_for_picking() const
{
    render(color);
}

void GLGizmoBase::Grabber::render(const float* render_color) const
{
    float half_size = dragging ? HalfSize * DraggingScaleFactor : HalfSize;
    float min_x = -half_size;
    float max_x = +half_size;
    float min_y = -half_size;
    float max_y = +half_size;

    ::glColor3f((GLfloat)render_color[0], (GLfloat)render_color[1], (GLfloat)render_color[2]);

    ::glPushMatrix();
    ::glTranslatef((GLfloat)center.x, (GLfloat)center.y, (GLfloat)center.z);

    float rad_to_deg = 180.0f / (GLfloat)PI;
    ::glRotatef((GLfloat)angle_x * rad_to_deg, 1.0f, 0.0f, 0.0f);
    ::glRotatef((GLfloat)angle_y * rad_to_deg, 0.0f, 1.0f, 0.0f);
    ::glRotatef((GLfloat)angle_z * rad_to_deg, 0.0f, 0.0f, 1.0f);

    ::glDisable(GL_CULL_FACE);
    ::glBegin(GL_TRIANGLES);
    ::glVertex3f((GLfloat)min_x, (GLfloat)min_y, 0.0f);
    ::glVertex3f((GLfloat)max_x, (GLfloat)min_y, 0.0f);
    ::glVertex3f((GLfloat)max_x, (GLfloat)max_y, 0.0f);
    ::glVertex3f((GLfloat)max_x, (GLfloat)max_y, 0.0f);
    ::glVertex3f((GLfloat)min_x, (GLfloat)max_y, 0.0f);
    ::glVertex3f((GLfloat)min_x, (GLfloat)min_y, 0.0f);
    ::glEnd();
    ::glEnable(GL_CULL_FACE);

    ::glPopMatrix();
}

GLGizmoBase::GLGizmoBase()
    : m_group_id(-1)
    , m_state(Off)
    , m_hover_id(-1)
    , m_is_container(false)
{
    ::memcpy((void*)m_base_color, (const void*)DEFAULT_BASE_COLOR, 3 * sizeof(float));
    ::memcpy((void*)m_drag_color, (const void*)DEFAULT_DRAG_COLOR, 3 * sizeof(float));
    ::memcpy((void*)m_highlight_color, (const void*)DEFAULT_HIGHLIGHT_COLOR, 3 * sizeof(float));
}

unsigned int GLGizmoBase::get_texture_id() const
{
    return m_textures[m_state].get_id();
}

int GLGizmoBase::get_textures_size() const
{
    return m_textures[Off].get_width();
}

void GLGizmoBase::set_hover_id(int id)
{
    if (m_is_container || (id < (int)m_grabbers.size()))
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

void GLGizmoBase::refresh()
{
    on_refresh();
}

void GLGizmoBase::render(const BoundingBoxf3& box) const
{
    on_render(box);
}

void GLGizmoBase::render_for_picking(const BoundingBoxf3& box) const
{
    on_render_for_picking(box);
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
    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        m_grabbers[i].render_for_picking();
    }
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

GLGizmoRotate::GLGizmoRotate(GLGizmoRotate::Axis axis)
    : GLGizmoBase()
    , m_axis(axis)
    , m_angle(0.0f)
    , m_center(Pointf3(0.0, 0.0, 0.0))
    , m_radius(0.0f)
    , m_keep_initial_values(false)
{
}

float GLGizmoRotate::get_angle() const
{
    return m_angle;
}

void GLGizmoRotate::set_angle(float angle)
{
    if (std::abs(angle - 2.0f * PI) < EPSILON)
        angle = 0.0f;

    m_angle = angle;
}

bool GLGizmoRotate::on_init()
{
#if !ENABLE_GIZMOS_3D
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
#endif // !ENABLE_GIZMOS_3D

    m_grabbers.push_back(Grabber());

    return true;
}

void GLGizmoRotate::on_set_state()
{
    m_keep_initial_values = (m_state == On) ? false : true;
}

void GLGizmoRotate::on_update(const Linef3& mouse_ray)
{
    Pointf mouse_pos = mouse_position_in_local_plane(mouse_ray);

    Vectorf orig_dir(1.0, 0.0);
    Vectorf new_dir = normalize(mouse_pos);

    coordf_t theta = ::acos(clamp(-1.0, 1.0, dot(new_dir, orig_dir)));
    if (cross(orig_dir, new_dir) < 0.0)
        theta = 2.0 * (coordf_t)PI - theta;

    // snap
    double len = length(mouse_pos);
    double in_radius = (double)m_radius / 3.0;
    double out_radius = 2.0 * (double)in_radius;
    if ((in_radius <= len) && (len <= out_radius))
    {
        coordf_t step = 2.0 * (coordf_t)PI / (coordf_t)SnapRegionsCount;
        theta = step * (coordf_t)std::round(theta / step);
    }

    if (theta == 2.0 * (coordf_t)PI)
        theta = 0.0;

    m_angle = (float)theta;
}

void GLGizmoRotate::on_refresh()
{
    m_keep_initial_values = false;
}

void GLGizmoRotate::on_render(const BoundingBoxf3& box) const
{
    ::glDisable(GL_DEPTH_TEST);

    if (!m_keep_initial_values)
    {
        const Pointf3& size = box.size();
        m_center = box.center();
#if !ENABLE_GIZMOS_3D
        m_center.z = 0.0;
#endif // !ENABLE_GIZMOS_3D

#if ENABLE_GIZMOS_3D
        m_radius = Offset + box.radius();
#else
        m_radius = Offset + ::sqrt(sqr(0.5f * size.x) + sqr(0.5f * size.y));
#endif // ENABLE_GIZMOS_3D
        m_keep_initial_values = true;
    }

    ::glPushMatrix();
    transform_to_local();

    ::glLineWidth(2.0f);

#if ENABLE_GIZMOS_3D
    ::glColor3fv((m_hover_id != -1) ? m_drag_color : m_base_color);
#else
    ::glColor3fv(m_drag_color);
#endif // ENABLE_GIZMOS_3D

    render_circle();
#if ENABLE_GIZMOS_3D
    if (m_hover_id != -1)
    {
#endif // ENABLE_GIZMOS_3D
        render_scale();
        render_snap_radii();
        render_reference_radius();
#if ENABLE_GIZMOS_3D
    }
#endif // ENABLE_GIZMOS_3D

    ::glColor3fv(m_highlight_color);
#if ENABLE_GIZMOS_3D
    if (m_hover_id != -1)
#endif // ENABLE_GIZMOS_3D
        render_angle();

    render_grabber();

    ::glPopMatrix();
}

void GLGizmoRotate::on_render_for_picking(const BoundingBoxf3& box) const
{
    ::glDisable(GL_DEPTH_TEST);

    ::glPushMatrix();
    transform_to_local();

    m_grabbers[0].color[0] = 1.0f;
    m_grabbers[0].color[1] = 1.0f;
    m_grabbers[0].color[2] = picking_color_component(0);

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
    float step_angle = m_angle / AngleResolution;
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
    float grabber_radius = m_radius + GrabberOffset;
    m_grabbers[0].center.x = ::cos(m_angle) * grabber_radius;
    m_grabbers[0].center.y = ::sin(m_angle) * grabber_radius;
    m_grabbers[0].center.z = 0.0f;
    m_grabbers[0].angle_z = m_angle;

#if ENABLE_GIZMOS_3D
    ::glColor3fv((m_hover_id != -1) ? m_drag_color : m_base_color);
#else
    ::glColor3fv(m_drag_color);
#endif // ENABLE_GIZMOS_3D

    ::glBegin(GL_LINES);
    ::glVertex3f(0.0f, 0.0f, 0.0f);
    ::glVertex3f((GLfloat)m_grabbers[0].center.x, (GLfloat)m_grabbers[0].center.y, (GLfloat)m_grabbers[0].center.z);
    ::glEnd();

    ::memcpy((void*)m_grabbers[0].color, (const void*)m_highlight_color, 3 * sizeof(float));
    render_grabbers();
}

void GLGizmoRotate::transform_to_local() const
{
    ::glTranslatef((GLfloat)m_center.x, (GLfloat)m_center.y, (GLfloat)m_center.z);

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
        ::glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
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

Pointf GLGizmoRotate::mouse_position_in_local_plane(const Linef3& mouse_ray) const
{
    Eigen::Transform<float, 3, Eigen::Affine> m = Eigen::Transform<float, 3, Eigen::Affine>::Identity();

    switch (m_axis)
    {
    case X:
    {
        m.rotate(Eigen::AngleAxisf(-0.5f * (float)PI, Eigen::Vector3f::UnitZ()));
        m.rotate(Eigen::AngleAxisf(-0.5f * (float)PI, Eigen::Vector3f::UnitY()));
        break;
    }
    case Y:
    {
        m.rotate(Eigen::AngleAxisf(-(float)PI, Eigen::Vector3f::UnitZ()));
        m.rotate(Eigen::AngleAxisf(-0.5f * (float)PI, Eigen::Vector3f::UnitX()));
        break;
    }
    default:
    case Z:
    {
        // no rotation applied
        break;
    }
    }

    m.translate(Eigen::Vector3f((float)-m_center.x, (float)-m_center.y, (float)-m_center.z));

    Eigen::Matrix<float, 3, 2> world_ray;
    Eigen::Matrix<float, 3, 2> local_ray;
    world_ray(0, 0) = (float)mouse_ray.a.x;
    world_ray(1, 0) = (float)mouse_ray.a.y;
    world_ray(2, 0) = (float)mouse_ray.a.z;
    world_ray(0, 1) = (float)mouse_ray.b.x;
    world_ray(1, 1) = (float)mouse_ray.b.y;
    world_ray(2, 1) = (float)mouse_ray.b.z;
    local_ray = m * world_ray.colwise().homogeneous();

    return Linef3(Pointf3(local_ray(0, 0), local_ray(1, 0), local_ray(2, 0)), Pointf3(local_ray(0, 1), local_ray(1, 1), local_ray(2, 1))).intersect_plane(0.0);
}

GLGizmoRotate3D::GLGizmoRotate3D()
    : GLGizmoBase()
    , m_x(GLGizmoRotate::X)
    , m_y(GLGizmoRotate::Y)
    , m_z(GLGizmoRotate::Z)
{
    m_is_container = true;

    m_x.set_group_id(0);
    m_y.set_group_id(1);
    m_z.set_group_id(2);
}

float GLGizmoRotate3D::get_angle_x() const
{
    return m_x.get_angle();
}

void GLGizmoRotate3D::set_angle_x(float angle)
{
    m_x.set_angle(angle);
}

float GLGizmoRotate3D::get_angle_y() const
{
    return m_y.get_angle();
}

void GLGizmoRotate3D::set_angle_y(float angle)
{
    m_y.set_angle(angle);
}

float GLGizmoRotate3D::get_angle_z() const
{
    return m_z.get_angle();
}

void GLGizmoRotate3D::set_angle_z(float angle)
{
    m_z.set_angle(angle);
}

bool GLGizmoRotate3D::on_init()
{
    if (!m_x.init() || !m_y.init() || !m_z.init())
        return false;

    float red[3] = { 1.0f, 0.0f, 0.0f };
    float green[3] = { 0.0f, 1.0f, 0.0f };
    float blue[3] = { 0.0f, 0.0f, 1.0f };

    m_x.set_highlight_color(red);
    m_y.set_highlight_color(green);
    m_z.set_highlight_color(blue);

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

void GLGizmoRotate3D::on_set_state()
{
    m_x.set_state(m_state);
    m_y.set_state(m_state);
    m_z.set_state(m_state);
}

void GLGizmoRotate3D::on_set_hover_id()
{
    m_x.set_hover_id(m_hover_id == 0 ? 0 : -1);
    m_y.set_hover_id(m_hover_id == 1 ? 0 : -1);
    m_z.set_hover_id(m_hover_id == 2 ? 0 : -1);
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

void GLGizmoRotate3D::on_update(const Linef3& mouse_ray)
{
    m_x.update(mouse_ray);
    m_y.update(mouse_ray);
    m_z.update(mouse_ray);
}

void GLGizmoRotate3D::on_refresh()
{
    m_x.refresh();
    m_y.refresh();
    m_z.refresh();
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

void GLGizmoRotate3D::on_render_for_picking(const BoundingBoxf3& box) const
{
    m_x.render_for_picking(box);
    m_y.render_for_picking(box);
    m_z.render_for_picking(box);
}

const float GLGizmoScale::Offset = 5.0f;

GLGizmoScale::GLGizmoScale()
    : GLGizmoBase()
    , m_scale(1.0f)
    , m_starting_scale(1.0f)
{
}

float GLGizmoScale::get_scale() const
{
    return m_scale;
}

void GLGizmoScale::set_scale(float scale)
{
    m_starting_scale = scale;
}

bool GLGizmoScale::on_init()
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

    for (unsigned int i = 0; i < 4; ++i)
    {
        m_grabbers.push_back(Grabber());
    }

    return true;
}

void GLGizmoScale::on_start_dragging()
{
    if (m_hover_id != -1)
        m_starting_drag_position = m_grabbers[m_hover_id].center;
}

void GLGizmoScale::on_update(const Linef3& mouse_ray)
{
    Pointf mouse_pos = mouse_ray.intersect_plane(0.0);
    Pointf center(0.5 * (m_grabbers[1].center.x + m_grabbers[0].center.x), 0.5 * (m_grabbers[3].center.y + m_grabbers[0].center.y));

    coordf_t orig_len = length(m_starting_drag_position - center);
    coordf_t new_len = length(mouse_pos - center);
    coordf_t ratio = (orig_len != 0.0) ? new_len / orig_len : 1.0;

    m_scale = m_starting_scale * (float)ratio;
}

void GLGizmoScale::on_render(const BoundingBoxf3& box) const
{
    ::glDisable(GL_DEPTH_TEST);

    coordf_t min_x = box.min.x - (coordf_t)Offset;
    coordf_t max_x = box.max.x + (coordf_t)Offset;
    coordf_t min_y = box.min.y - (coordf_t)Offset;
    coordf_t max_y = box.max.y + (coordf_t)Offset;

    m_grabbers[0].center.x = min_x;
    m_grabbers[0].center.y = min_y;
    m_grabbers[1].center.x = max_x;
    m_grabbers[1].center.y = min_y;
    m_grabbers[2].center.x = max_x;
    m_grabbers[2].center.y = max_y;
    m_grabbers[3].center.x = min_x;
    m_grabbers[3].center.y = max_y;

    ::glLineWidth(2.0f);
    ::glColor3fv(m_drag_color);

    // draw outline
    ::glBegin(GL_LINE_LOOP);
    for (unsigned int i = 0; i < 4; ++i)
    {
        ::glVertex3f((GLfloat)m_grabbers[i].center.x, (GLfloat)m_grabbers[i].center.y, 0.0f);
    }
    ::glEnd();

    // draw grabbers
    for (unsigned int i = 0; i < 4; ++i)
    {
        ::memcpy((void*)m_grabbers[i].color, (const void*)m_highlight_color, 3 * sizeof(float));
    }
    render_grabbers();
}

void GLGizmoScale::on_render_for_picking(const BoundingBoxf3& box) const
{
    ::glDisable(GL_DEPTH_TEST);

    for (unsigned int i = 0; i < 4; ++i)
    {
        m_grabbers[i].color[0] = 1.0f;
        m_grabbers[i].color[1] = 1.0f;
        m_grabbers[i].color[2] = picking_color_component(i);
    }
    render_grabbers_for_picking();
}

} // namespace GUI
} // namespace Slic3r
