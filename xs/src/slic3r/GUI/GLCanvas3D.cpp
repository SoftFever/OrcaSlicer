#include "GLCanvas3D.hpp"

#include "../../slic3r/GUI/3DScene.hpp"

#include <wx/glcanvas.h>

#include <iostream>

static const bool TURNTABLE_MODE = true;
static const float GIMBALL_LOCK_THETA_MAX = 180.0f;

namespace Slic3r {
namespace GUI {

GLCanvas3D::Camera::Camera()
    : m_type(CT_Ortho)
    , m_zoom(1.0f)
    , m_phi(45.0f)
    , m_theta(45.0f)
    , m_distance(0.0f)
    , m_target(0.0, 0.0, 0.0)
{
}

GLCanvas3D::Camera::EType GLCanvas3D::Camera::get_type() const
{
    return m_type;
}

void GLCanvas3D::Camera::set_type(GLCanvas3D::Camera::EType type)
{
    m_type = type;
}

std::string GLCanvas3D::Camera::get_type_as_string() const
{
    switch (m_type)
    {
    default:
    case CT_Unknown:
        return "unknown";
    case CT_Perspective:
        return "perspective";
    case CT_Ortho:
        return "ortho";
    };
}

float GLCanvas3D::Camera::get_zoom() const
{
    return m_zoom;
}

void GLCanvas3D::Camera::set_zoom(float zoom)
{
    m_zoom = zoom;
}

float GLCanvas3D::Camera::get_phi() const
{
    return m_phi;
}

void GLCanvas3D::Camera::set_phi(float phi)
{
    m_phi = phi;
}

float GLCanvas3D::Camera::get_theta() const
{
    return m_theta;
}

void GLCanvas3D::Camera::set_theta(float theta)
{
    m_theta = theta;

    // clamp angle
    if (m_theta > GIMBALL_LOCK_THETA_MAX)
        m_theta = GIMBALL_LOCK_THETA_MAX;

    if (m_theta < 0.0f)
        m_theta = 0.0f;
}

float GLCanvas3D::Camera::get_distance() const
{
    return m_distance;
}

void GLCanvas3D::Camera::set_distance(float distance)
{
    m_distance = distance;
}

const Pointf3& GLCanvas3D::Camera::get_target() const
{
    return m_target;
}

void GLCanvas3D::Camera::set_target(const Pointf3& target)
{
    m_target = target;
}

const Pointfs& GLCanvas3D::Bed::get_shape() const
{
    return m_shape;
}

void GLCanvas3D::Bed::set_shape(const Pointfs& shape)
{
    m_shape = shape;
    _calc_bounding_box();
}

const BoundingBoxf3& GLCanvas3D::Bed::get_bounding_box() const
{
    return m_bounding_box;
}

const Pointf& GLCanvas3D::Bed::get_origin() const
{
    return m_origin;
}

void GLCanvas3D::Bed::set_origin(const Pointf& origin)
{
    m_origin = origin;
}

void GLCanvas3D::Bed::_calc_bounding_box()
{
    m_bounding_box = BoundingBoxf3();
    for (const Pointf& p : m_shape)
    {
        m_bounding_box.merge(Pointf3(p.x, p.y, 0.0));
    }
}

GLCanvas3D::GLCanvas3D(wxGLCanvas* canvas, wxGLContext* context)
    : m_canvas(canvas)
    , m_context(context)
    , m_volumes(nullptr)
    , m_dirty(true)
    , m_apply_zoom_to_volumes_filter(false)
{
}

GLCanvas3D::~GLCanvas3D()
{
    _deregister_callbacks();
}

void GLCanvas3D::set_current()
{
    if ((m_canvas != nullptr) && (m_context != nullptr))
        m_canvas->SetCurrent(*m_context);
}

bool GLCanvas3D::is_shown_on_screen() const
{
    return (m_canvas != nullptr) ? m_canvas->IsShownOnScreen() : false;
}

void GLCanvas3D::resize(unsigned int w, unsigned int h)
{
    if (m_context == nullptr)
        return;
    
    set_current();
    ::glViewport(0, 0, w, h);

    ::glMatrixMode(GL_PROJECTION);
    ::glLoadIdentity();

    BoundingBoxf3 bbox = max_bounding_box();

    switch (get_camera_type())
    {
    case Camera::CT_Ortho:
        {
            float w2 = w;
            float h2 = h;
            float two_zoom = 2.0f * get_camera_zoom();
            if (two_zoom != 0.0f)
            {
                float inv_two_zoom = 1.0f / two_zoom;
                w2 *= inv_two_zoom;
                h2 *= inv_two_zoom;
            }

            // FIXME: calculate a tighter value for depth will improve z-fighting
            float depth = 5.0f * (float)bbox.max_size();
            ::glOrtho(-w2, w2, -h2, h2, -depth, depth);

            break;
        }
    case Camera::CT_Perspective:
        {
            float bbox_r = (float)bbox.radius();
            float fov = PI * 45.0f / 180.0f;
            float fov_tan = tan(0.5f * fov);
            float cam_distance = 0.5f * bbox_r / fov_tan;
            set_camera_distance(cam_distance);

            float nr = cam_distance - bbox_r * 1.1f;
            float fr = cam_distance + bbox_r * 1.1f;
            if (nr < 1.0f)
                nr = 1.0f;

            if (fr < nr + 1.0f)
                fr = nr + 1.0f;

            float h2 = fov_tan * nr;
            float w2 = h2 * w / h;
            ::glFrustum(-w2, w2, -h2, h2, nr, fr);

            break;
        }
    default:
        {
            throw std::runtime_error("Invalid camera type.");
            break;
        }
    }

    ::glMatrixMode(GL_MODELVIEW);

    set_dirty(false);
}

GLVolumeCollection* GLCanvas3D::get_volumes()
{
    return m_volumes;
}

void GLCanvas3D::set_volumes(GLVolumeCollection* volumes)
{
    m_volumes = volumes;
}

void GLCanvas3D::set_bed_shape(const Pointfs& shape)
{
    m_bed.set_shape(shape);
}

const Pointf& GLCanvas3D::get_bed_origin() const
{
    return m_bed.get_origin();
}

void GLCanvas3D::set_bed_origin(const Pointf& origin)
{
    m_bed.set_origin(origin);
}

bool GLCanvas3D::is_dirty() const
{
    return m_dirty;
}

void GLCanvas3D::set_dirty(bool dirty)
{
    m_dirty = dirty;
}

GLCanvas3D::Camera::EType GLCanvas3D::get_camera_type() const
{
    return m_camera.get_type();
}

void GLCanvas3D::set_camera_type(GLCanvas3D::Camera::EType type)
{
    m_camera.set_type(type);
}

std::string GLCanvas3D::get_camera_type_as_string() const
{
    return m_camera.get_type_as_string();
}

float GLCanvas3D::get_camera_zoom() const
{
    return m_camera.get_zoom();
}

void GLCanvas3D::set_camera_zoom(float zoom)
{
    m_camera.set_zoom(zoom);
}

float GLCanvas3D::get_camera_phi() const
{
    return m_camera.get_phi();
}

void GLCanvas3D::set_camera_phi(float phi)
{
    m_camera.set_phi(phi);
}

float GLCanvas3D::get_camera_theta() const
{
    return m_camera.get_theta();
}

void GLCanvas3D::set_camera_theta(float theta)
{
    m_camera.set_theta(theta);
}

float GLCanvas3D::get_camera_distance() const
{
    return m_camera.get_distance();
}

void GLCanvas3D::set_camera_distance(float distance)
{
    m_camera.set_distance(distance);
}

const Pointf3& GLCanvas3D::get_camera_target() const
{
    return m_camera.get_target();
}

void GLCanvas3D::set_camera_target(const Pointf3& target)
{
    m_camera.set_target(target);
}

BoundingBoxf3 GLCanvas3D::bed_bounding_box() const
{
    return m_bed.get_bounding_box();
}

BoundingBoxf3 GLCanvas3D::volumes_bounding_box() const
{
    BoundingBoxf3 bb;
    if (m_volumes != nullptr)
    {
        for (const GLVolume* volume : m_volumes->volumes)
        {
            if (!m_apply_zoom_to_volumes_filter || ((volume != nullptr) && volume->zoom_to_volumes))
                bb.merge(volume->transformed_bounding_box());
        }
    }
    return bb;
}

BoundingBoxf3 GLCanvas3D::max_bounding_box() const
{
    BoundingBoxf3 bb = bed_bounding_box();
    bb.merge(volumes_bounding_box());
    return bb;
}

void GLCanvas3D::zoom_to_bed()
{
    _zoom_to_bounding_box(bed_bounding_box());
}

void GLCanvas3D::zoom_to_volumes()
{
    m_apply_zoom_to_volumes_filter = true;
    _zoom_to_bounding_box(volumes_bounding_box());
    m_apply_zoom_to_volumes_filter = false;
}

void GLCanvas3D::register_on_viewport_changed_callback(void* callback)
{
    if (callback != nullptr)
        m_on_viewport_changed_callback.register_callback(callback);
}

void GLCanvas3D::on_size(wxSizeEvent& evt)
{
    set_dirty(true);
}

void GLCanvas3D::on_idle(wxIdleEvent& evt)
{
    if (!is_dirty() || !is_shown_on_screen())
        return;

    if (m_canvas != nullptr)
    {
        std::pair<int, int> size = _get_canvas_size();
        resize((unsigned int)size.first, (unsigned int)size.second);
        m_canvas->Refresh();
    }
}

void GLCanvas3D::_zoom_to_bounding_box(const BoundingBoxf3& bbox)
{
    // Calculate the zoom factor needed to adjust viewport to bounding box.
    float zoom = _get_zoom_to_bounding_box_factor(bbox);
    if (zoom > 0.0f)
    {
        set_camera_zoom(zoom);
        // center view around bounding box center
        set_camera_target(bbox.center());

        m_on_viewport_changed_callback.call();

        if (is_shown_on_screen())
        {
            std::pair<int, int> size = _get_canvas_size();
            resize((unsigned int)size.first, (unsigned int)size.second);
            if (m_canvas != nullptr)
                m_canvas->Refresh();
        }
    }
}

std::pair<int, int> GLCanvas3D::_get_canvas_size() const
{
    std::pair<int, int> ret(0, 0);

    if (m_canvas != nullptr)
        m_canvas->GetSize(&ret.first, &ret.second);

    return ret;
}

float GLCanvas3D::_get_zoom_to_bounding_box_factor(const BoundingBoxf3& bbox) const
{
    float max_bb_size = bbox.max_size();
    if (max_bb_size == 0.0f)
        return -1.0f;

    // project the bbox vertices on a plane perpendicular to the camera forward axis
    // then calculates the vertices coordinate on this plane along the camera xy axes

    // we need the view matrix, we let opengl calculate it(same as done in render sub)
    ::glMatrixMode(GL_MODELVIEW);
    ::glLoadIdentity();

    if (TURNTABLE_MODE)
    {
        // Turntable mode is enabled by default.
        ::glRotatef(-get_camera_theta(), 1.0f, 0.0f, 0.0f); // pitch
        ::glRotatef(get_camera_phi(), 0.0f, 0.0f, 1.0f);    // yaw
    }
    else
    {
        // Shift the perspective camera.
        Pointf3 camera_pos(0.0, 0.0, -(coordf_t)get_camera_distance());
        ::glTranslatef((float)camera_pos.x, (float)camera_pos.y, (float)camera_pos.z);
//        my @rotmat = quat_to_rotmatrix($self->quat); <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< TEMPORARY COMMENTED OUT
//        glMultMatrixd_p(@rotmat[0..15]);             <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< TEMPORARY COMMENTED OUT
    }

    const Pointf3& target = get_camera_target();
    ::glTranslatef(-(float)target.x, -(float)target.y, -(float)target.z);

    // get the view matrix back from opengl
    GLfloat matrix[16];
    ::glGetFloatv(GL_MODELVIEW_MATRIX, matrix);

    // camera axes
    Pointf3 right((coordf_t)matrix[0], (coordf_t)matrix[4], (coordf_t)matrix[8]);
    Pointf3 up((coordf_t)matrix[1], (coordf_t)matrix[5], (coordf_t)matrix[9]);
    Pointf3 forward((coordf_t)matrix[2], (coordf_t)matrix[6], (coordf_t)matrix[10]);

    Pointf3 bb_min = bbox.min;
    Pointf3 bb_max = bbox.max;
    Pointf3 bb_center = bbox.center();

    // bbox vertices in world space
    std::vector<Pointf3> vertices;
    vertices.reserve(8);
    vertices.push_back(bb_min);
    vertices.emplace_back(bb_max.x, bb_min.y, bb_min.z);
    vertices.emplace_back(bb_max.x, bb_max.y, bb_min.z);
    vertices.emplace_back(bb_min.x, bb_max.y, bb_min.z);
    vertices.emplace_back(bb_min.x, bb_min.y, bb_max.z);
    vertices.emplace_back(bb_max.x, bb_min.y, bb_max.z);
    vertices.push_back(bb_max);
    vertices.emplace_back(bb_min.x, bb_max.y, bb_max.z);

    coordf_t max_x = 0.0;
    coordf_t max_y = 0.0;

    // margin factor to give some empty space around the bbox
    coordf_t margin_factor = 1.25;

    for (const Pointf3 v : vertices)
    {
        // project vertex on the plane perpendicular to camera forward axis
        Pointf3 pos(v.x - bb_center.x, v.y - bb_center.y, v.z - bb_center.z);
        Pointf3 proj_on_plane = pos - dot(pos, forward) * forward;

        // calculates vertex coordinate along camera xy axes
        coordf_t x_on_plane = dot(proj_on_plane, right);
        coordf_t y_on_plane = dot(proj_on_plane, up);

        max_x = std::max(max_x, margin_factor * std::abs(x_on_plane));
        max_y = std::max(max_y, margin_factor * std::abs(y_on_plane));
    }

    if ((max_x == 0.0) || (max_y == 0.0))
        return -1.0f;

    max_x *= 2.0;
    max_y *= 2.0;

    std::pair<int, int> cvs_size = _get_canvas_size();
    return (float)std::min((coordf_t)cvs_size.first / max_x, (coordf_t)cvs_size.second / max_y);
}

void GLCanvas3D::_deregister_callbacks()
{
    m_on_viewport_changed_callback.deregister_callback();
}

} // namespace GUI
} // namespace Slic3r
