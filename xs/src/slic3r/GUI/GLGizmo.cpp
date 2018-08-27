#include "GLGizmo.hpp"

#include "../../libslic3r/Utils.hpp"
#include "../../libslic3r/BoundingBox.hpp"
#include "../../libslic3r/Model.hpp"
#include "../../libslic3r/Geometry.hpp"

#include <GL/glew.h>

#include <iostream>
#include <numeric>

namespace Slic3r {
namespace GUI {

const float GLGizmoBase::Grabber::HalfSize = 2.0f;
const float GLGizmoBase::Grabber::HoverOffset = 0.5f;
const float GLGizmoBase::BaseColor[3] = { 1.0f, 1.0f, 1.0f };
const float GLGizmoBase::HighlightColor[3] = { 1.0f, 0.38f, 0.0f };

GLGizmoBase::Grabber::Grabber()
    : center(Pointf(0.0, 0.0))
    , angle_z(0.0f)
{
    color[0] = 1.0f;
    color[1] = 1.0f;
    color[2] = 1.0f;
}

void GLGizmoBase::Grabber::render(bool hover) const
{
    float min_x = -HalfSize;
    float max_x = +HalfSize;
    float min_y = -HalfSize;
    float max_y = +HalfSize;

    ::glColor3f((GLfloat)color[0], (GLfloat)color[1], (GLfloat)color[2]);

    float angle_z_in_deg = angle_z * 180.0f / (float)PI;
    ::glPushMatrix();
    ::glTranslatef((GLfloat)center.x, (GLfloat)center.y, 0.0f);
    ::glRotatef((GLfloat)angle_z_in_deg, 0.0f, 0.0f, 1.0f);

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

    if (hover)
    {
        min_x -= HoverOffset;
        max_x += HoverOffset;
        min_y -= HoverOffset;
        max_y += HoverOffset;

        ::glBegin(GL_LINE_LOOP);
        ::glVertex3f((GLfloat)min_x, (GLfloat)min_y, 0.0f);
        ::glVertex3f((GLfloat)max_x, (GLfloat)min_y, 0.0f);
        ::glVertex3f((GLfloat)max_x, (GLfloat)max_y, 0.0f);
        ::glVertex3f((GLfloat)min_x, (GLfloat)max_y, 0.0f);
        ::glEnd();
    }

    ::glPopMatrix();
}

GLGizmoBase::GLGizmoBase()
    : m_state(Off)
    , m_hover_id(-1)
{
}

GLGizmoBase::~GLGizmoBase()
{
}

bool GLGizmoBase::init()
{
    return on_init();
}

GLGizmoBase::EState GLGizmoBase::get_state() const
{
    return m_state;
}

void GLGizmoBase::set_state(GLGizmoBase::EState state)
{
    m_state = state;
    on_set_state();
}

unsigned int GLGizmoBase::get_texture_id() const
{
    return m_textures[m_state].get_id();
}

int GLGizmoBase::get_textures_size() const
{
    return m_textures[Off].get_width();
}

int GLGizmoBase::get_hover_id() const
{
    return m_hover_id;
}

void GLGizmoBase::set_hover_id(int id)
{
    //if (id < (int)m_grabbers.size())
        m_hover_id = id;
}

void GLGizmoBase::start_dragging()
{
    on_start_dragging();
}

void GLGizmoBase::stop_dragging()
{
    on_stop_dragging();
}

void GLGizmoBase::update(const Pointf& mouse_pos)
{
    if (m_hover_id != -1)
        on_update(mouse_pos);
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

void GLGizmoBase::on_set_state()
{
    // do nothing
}

void GLGizmoBase::on_start_dragging()
{
    // do nothing
}

void GLGizmoBase::on_stop_dragging()
{
    // do nothing
}

void GLGizmoBase::on_refresh()
{
    // do nothing
}

void GLGizmoBase::render_grabbers() const
{
    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        m_grabbers[i].render(m_hover_id == i);
    }
}

const float GLGizmoRotate::Offset = 5.0f;
const unsigned int GLGizmoRotate::CircleResolution = 64;
const unsigned int GLGizmoRotate::AngleResolution = 64;
const unsigned int GLGizmoRotate::ScaleStepsCount = 60;
const float GLGizmoRotate::ScaleStepRad = 2.0f * (float)PI / GLGizmoRotate::ScaleStepsCount;
const unsigned int GLGizmoRotate::ScaleLongEvery = 5;
const float GLGizmoRotate::ScaleLongTooth = 2.0f;
const float GLGizmoRotate::ScaleShortTooth = 1.0f;
const unsigned int GLGizmoRotate::SnapRegionsCount = 8;
const float GLGizmoRotate::GrabberOffset = 5.0f;

GLGizmoRotate::GLGizmoRotate()
    : GLGizmoBase()
    , m_angle_z(0.0f)
    , m_center(Pointf(0.0, 0.0))
    , m_radius(0.0f)
    , m_keep_initial_values(false)
{
}

float GLGizmoRotate::get_angle_z() const
{
    return m_angle_z;
}

void GLGizmoRotate::set_angle_z(float angle_z)
{
    if (std::abs(angle_z - 2.0f * PI) < EPSILON)
        angle_z = 0.0f;

    m_angle_z = angle_z;
}

bool GLGizmoRotate::on_init()
{
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

    m_grabbers.push_back(Grabber());

    return true;
}

void GLGizmoRotate::on_set_state()
{
    m_keep_initial_values = (m_state == On) ? false : true;
}

void GLGizmoRotate::on_update(const Pointf& mouse_pos)
{
    Vectorf orig_dir(1.0, 0.0);
    Vectorf new_dir = normalize(mouse_pos - m_center);
    coordf_t theta = ::acos(clamp(-1.0, 1.0, dot(new_dir, orig_dir)));
    if (cross(orig_dir, new_dir) < 0.0)
        theta = 2.0 * (coordf_t)PI - theta;

    // snap
    if (length(m_center.vector_to(mouse_pos)) < 2.0 * (double)m_radius / 3.0)
    {
        coordf_t step = 2.0 * (coordf_t)PI / (coordf_t)SnapRegionsCount;
        theta = step * (coordf_t)std::round(theta / step);
    }

    if (theta == 2.0 * (coordf_t)PI)
        theta = 0.0;

    m_angle_z = (float)theta;
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
        m_radius = Offset + ::sqrt(sqr(0.5f * size.x) + sqr(0.5f * size.y));
        m_keep_initial_values = true;
    }

    ::glLineWidth(2.0f);
    ::glColor3fv(BaseColor);

    _render_circle();
    _render_scale();
    _render_snap_radii();
    _render_reference_radius();

    ::glColor3fv(HighlightColor);
    _render_angle_z();
    _render_grabber();
}

void GLGizmoRotate::on_render_for_picking(const BoundingBoxf3& box) const
{
    ::glDisable(GL_DEPTH_TEST);

    m_grabbers[0].color[0] = 1.0f;
    m_grabbers[0].color[1] = 1.0f;
    m_grabbers[0].color[2] = 254.0f / 255.0f;
    render_grabbers();
}

void GLGizmoRotate::_render_circle() const
{
    ::glBegin(GL_LINE_LOOP);
    for (unsigned int i = 0; i < ScaleStepsCount; ++i)
    {
        float angle = (float)i * ScaleStepRad;
        float x = m_center.x + ::cos(angle) * m_radius;
        float y = m_center.y + ::sin(angle) * m_radius;
        ::glVertex3f((GLfloat)x, (GLfloat)y, 0.0f);
    }
    ::glEnd();
}

void GLGizmoRotate::_render_scale() const
{
    float out_radius_long = m_radius + ScaleLongTooth;
    float out_radius_short = m_radius + ScaleShortTooth;

    ::glBegin(GL_LINES);
    for (unsigned int i = 0; i < ScaleStepsCount; ++i)
    {
        float angle = (float)i * ScaleStepRad;
        float cosa = ::cos(angle);
        float sina = ::sin(angle);
        float in_x = m_center.x + cosa * m_radius;
        float in_y = m_center.y + sina * m_radius;
        float out_x = (i % ScaleLongEvery == 0) ? m_center.x + cosa * out_radius_long : m_center.x + cosa * out_radius_short;
        float out_y = (i % ScaleLongEvery == 0) ? m_center.y + sina * out_radius_long : m_center.y + sina * out_radius_short;
        ::glVertex3f((GLfloat)in_x, (GLfloat)in_y, 0.0f);
        ::glVertex3f((GLfloat)out_x, (GLfloat)out_y, 0.0f);
    }
    ::glEnd();
}

void GLGizmoRotate::_render_snap_radii() const
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
        float in_x = m_center.x + cosa * in_radius;
        float in_y = m_center.y + sina * in_radius;
        float out_x = m_center.x + cosa * out_radius;
        float out_y = m_center.y + sina * out_radius;
        ::glVertex3f((GLfloat)in_x, (GLfloat)in_y, 0.0f);
        ::glVertex3f((GLfloat)out_x, (GLfloat)out_y, 0.0f);
    }
    ::glEnd();
}

void GLGizmoRotate::_render_reference_radius() const
{
    ::glBegin(GL_LINES);
    ::glVertex3f((GLfloat)m_center.x, (GLfloat)m_center.y, 0.0f);
    ::glVertex3f((GLfloat)m_center.x + m_radius + GrabberOffset, (GLfloat)m_center.y, 0.0f);
    ::glEnd();
}

void GLGizmoRotate::_render_angle_z() const
{
    float step_angle = m_angle_z / AngleResolution;
    float ex_radius = m_radius + GrabberOffset;

    ::glBegin(GL_LINE_STRIP);
    for (unsigned int i = 0; i <= AngleResolution; ++i)
    {
        float angle = (float)i * step_angle;
        float x = m_center.x + ::cos(angle) * ex_radius;
        float y = m_center.y + ::sin(angle) * ex_radius;
        ::glVertex3f((GLfloat)x, (GLfloat)y, 0.0f);
    }
    ::glEnd();
}

void GLGizmoRotate::_render_grabber() const
{
    float grabber_radius = m_radius + GrabberOffset;
    m_grabbers[0].center.x = m_center.x + ::cos(m_angle_z) * grabber_radius;
    m_grabbers[0].center.y = m_center.y + ::sin(m_angle_z) * grabber_radius;
    m_grabbers[0].angle_z = m_angle_z;

    ::glColor3fv(BaseColor);
    ::glBegin(GL_LINES);
    ::glVertex3f((GLfloat)m_center.x, (GLfloat)m_center.y, 0.0f);
    ::glVertex3f((GLfloat)m_grabbers[0].center.x, (GLfloat)m_grabbers[0].center.y, 0.0f);
    ::glEnd();

    ::memcpy((void*)m_grabbers[0].color, (const void*)HighlightColor, 3 * sizeof(float));
    render_grabbers();
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

void GLGizmoScale::on_update(const Pointf& mouse_pos)
{
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
    ::glColor3fv(BaseColor);
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
        ::memcpy((void*)m_grabbers[i].color, (const void*)HighlightColor, 3 * sizeof(float));
    }
    render_grabbers();
}

void GLGizmoScale::on_render_for_picking(const BoundingBoxf3& box) const
{
    static const GLfloat INV_255 = 1.0f / 255.0f;

    ::glDisable(GL_DEPTH_TEST);

    for (unsigned int i = 0; i < 4; ++i)
    {
        m_grabbers[i].color[0] = 1.0f;
        m_grabbers[i].color[1] = 1.0f;
        m_grabbers[i].color[2] = (254.0f - (float)i) * INV_255;
    }
    render_grabbers();
}


GLGizmoFlatten::GLGizmoFlatten()
    : GLGizmoBase(),
      m_normal(Pointf3(0.f, 0.f, 0.f))
{}


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
        m_center.reset(new Pointf3(box.center().x, box.center().y));
    Pointf3 dragged_offset = box.center() - *m_center;

    bool blending_was_enabled = ::glIsEnabled(GL_BLEND);
    bool depth_test_was_enabled = ::glIsEnabled(GL_DEPTH_TEST);
    ::glEnable(GL_BLEND);
    ::glEnable(GL_DEPTH_TEST);

    for (int i=0; i<(int)m_planes.size(); ++i) {
        if (i == m_hover_id)
            ::glColor4f(0.9f, 0.9f, 0.9f, 0.75f);
            else
                ::glColor4f(0.9f, 0.9f, 0.9f, 0.5f);

        for (Pointf offset : m_instances_positions) {
            offset += dragged_offset;
            ::glBegin(GL_POLYGON);
            for (const auto& vertex : m_planes[i].vertices)
                ::glVertex3f((GLfloat)vertex.x + offset.x, (GLfloat)vertex.y + offset.y, (GLfloat)vertex.z);
            ::glEnd();
        }
    }

    if (!blending_was_enabled)
        ::glDisable(GL_BLEND);
    if (!depth_test_was_enabled)
        ::glDisable(GL_DEPTH_TEST);
}

void GLGizmoFlatten::on_render_for_picking(const BoundingBoxf3& box) const
{
    static const GLfloat INV_255 = 1.0f / 255.0f;

    ::glDisable(GL_DEPTH_TEST);

    for (unsigned int i = 0; i < m_planes.size(); ++i)
    {
        ::glColor3f(1.f, 1.f, (254.0f - (float)i) * INV_255);
        for (const Pointf& offset : m_instances_positions) {
            ::glBegin(GL_POLYGON);
            for (const auto& vertex : m_planes[i].vertices)
                ::glVertex3f((GLfloat)vertex.x + offset.x, (GLfloat)vertex.y + offset.y, (GLfloat)vertex.z);
            ::glEnd();
        }
    }
}


// TODO - remove and use Eigen instead
static Pointf3 super_rotation(Pointf3 axis, float angle, const Pointf3& point)
{
    axis = normalize(axis);
    const float& x = axis.x;
    const float& y = axis.y;
    const float& z = axis.z;
    float s = sin(angle);
    float c = cos(angle);
    float D = 1-c;
    float matrix[3][3] = { { c + x*x*D, x*y*D-z*s, x*z*D+y*s },
                           { y*x*D+z*s, c+y*y*D,   y*z*D-x*s },
                           { z*x*D-y*s, z*y*D+x*s, c+z*z*D   } };
    float in[3] = { (float)point.x, (float)point.y, (float)point.z };
    float out[3] = { 0, 0, 0 };

    for (unsigned char i=0; i<3; ++i)
        for (unsigned char j=0; j<3; ++j)
            out[i] += matrix[i][j] * in[j];

    return Pointf3(out[0], out[1], out[2]);
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
            const stl_normal* this_normal_ptr = &ch.stl.facet_start[facet_idx].normal;
            //if (this_normal_ptr->x == normal_ptr->x && this_normal_ptr->y == normal_ptr->y && this_normal_ptr->z == normal_ptr->z) {
            if (std::abs(this_normal_ptr->x-normal_ptr->x) < 0.001 && std::abs(this_normal_ptr->y-normal_ptr->y) < 0.001 && std::abs(this_normal_ptr->z-normal_ptr->z) < 0.001) {
                stl_vertex* first_vertex = ch.stl.facet_start[facet_idx].vertex;
                for (int j=0; j<3; ++j)
                    m_planes.back().vertices.emplace_back(first_vertex[j].x, first_vertex[j].y, first_vertex[j].z);

                facet_visited[facet_idx] = true;
                for (int j = 0; j < 3; ++ j) {
                    int neighbor_idx = ch.stl.neighbors_start[facet_idx].neighbor[j];
                    if (! facet_visited[neighbor_idx])
                        facet_queue[facet_queue_cnt ++] = neighbor_idx;
                }
            }
        }
        m_planes.back().normal = Pointf3(normal_ptr->x, normal_ptr->y, normal_ptr->z);

        // if this is a just a very small triangle, remove it to speed up further calculations (it would be rejected anyway):
        if (m_planes.back().vertices.size() == 3 &&
             ( m_planes.back().vertices[0].distance_to(m_planes.back().vertices[1]) < 1.f
            || m_planes.back().vertices[0].distance_to(m_planes.back().vertices[2]) < 1.f))
            m_planes.pop_back();
    }

    // Now we'll go through all the polygons, transform the points into xy plane to process them:
    for (unsigned int polygon_id=0; polygon_id < m_planes.size(); ++polygon_id) {
        Pointf3s& polygon = m_planes[polygon_id].vertices;
        const Pointf3& normal = m_planes[polygon_id].normal;

        // We are going to rotate about z and y to flatten the plane
        float angle_z = 0.f;
        float angle_y = 0.f;
        if (std::abs(normal.y) > 0.001)
            angle_z = -atan2(normal.y, normal.x); // angle to rotate so that normal ends up in xz-plane
        if (std::abs(normal.x*cos(angle_z)-normal.y*sin(angle_z)) > 0.001)
            angle_y = - atan2(normal.x*cos(angle_z)-normal.y*sin(angle_z), normal.z); // angle to rotate to make normal point upwards
        else {
            // In case it already was in z-direction, we must ensure it is not the wrong way:
            angle_y = normal.z > 0.f ? 0 : -M_PI;
        }

        // Rotate all points to the xy plane:
        for (auto& vertex : polygon) {
            vertex = super_rotation(Pointf3(0,0,1), angle_z, vertex);
            vertex = super_rotation(Pointf3(0,1,0), angle_y, vertex);
        }
        polygon = Slic3r::Geometry::convex_hull(polygon); // To remove the inner points

        // We will calculate area of the polygon and discard ones that are too small
        // The limit is more forgiving in case the normal is in the direction of the coordinate axes
        const float minimal_area = (std::abs(normal.x) > 0.999f || std::abs(normal.y) > 0.999f || std::abs(normal.z) > 0.999f) ? 1.f : 20.f;
        float& area = m_planes[polygon_id].area;
        area = 0.f;
        for (unsigned int i = 0; i < polygon.size(); i++) // Shoelace formula
            area += polygon[i].x*polygon[i+1 < polygon.size() ? i+1 : 0 ].y - polygon[i+1 < polygon.size() ? i+1 : 0].x*polygon[i].y;
        area = std::abs(area/2.f);
        if (area < minimal_area) {
            m_planes.erase(m_planes.begin()+(polygon_id--));
            continue;
        }

        // We will shrink the polygon a little bit so it does not touch the object edges:
        Pointf3 centroid = std::accumulate(polygon.begin(), polygon.end(), Pointf3(0.f, 0.f, 0.f));
        centroid.scale(1.f/polygon.size());
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
            b.z += 0.1f; // raise a bit above the object surface to avoid flickering
            b = super_rotation(Pointf3(0,1,0), -angle_y, b);
            b = super_rotation(Pointf3(0,0,1), -angle_z, b);
        }
    }

    // We'll sort the planes by area and only keep the 255 largest ones (because of the picking pass limitations):
    std::sort(m_planes.rbegin(), m_planes.rend(), [](const PlaneData& a, const PlaneData& b) { return a.area < b.area; });
    m_planes.resize(std::min((int)m_planes.size(), 255));

    // Planes are finished - let's save what we calculated it from:
    m_source_data.bounding_boxes.clear();
    for (const auto& vol : m_model_object->volumes)
        m_source_data.bounding_boxes.push_back(vol->get_convex_hull().bounding_box());
    m_source_data.scaling_factor = m_model_object->instances.front()->scaling_factor;
    m_source_data.rotation = m_model_object->instances.front()->rotation;
    const float* first_vertex = m_model_object->volumes.front()->get_convex_hull().first_vertex();
    m_source_data.mesh_first_point = Pointf3(first_vertex[0], first_vertex[1], first_vertex[2]);
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
    Pointf3 first_point(first_vertex[0], first_vertex[1], first_vertex[2]);
    if (first_point != m_source_data.mesh_first_point)
        return true;

    return false;
}

Pointf3 GLGizmoFlatten::get_flattening_normal() const {
    Pointf3 normal = m_normal;
    normal.rotate(-m_model_object->instances.front()->rotation);
    m_normal = Pointf3(0.f, 0.f, 0.f);
    return normal;
}



} // namespace GUI
} // namespace Slic3r
