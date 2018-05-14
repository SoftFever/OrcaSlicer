#include "GLCanvas3D.hpp"

#include "../../slic3r/GUI/3DScene.hpp"

#include <wx/glcanvas.h>

#include <iostream>

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
            Pointf3 bb_size = bbox.size();
            float depth = 5.0f * (float)std::max(bb_size.x, std::max(bb_size.y, bb_size.z));
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

void GLCanvas3D::_zoom_to_bed()
{
    _zoom_to_bounding_box(bed_bounding_box());
}

void GLCanvas3D::_zoom_to_volumes()
{
    m_apply_zoom_to_volumes_filter = true;
    _zoom_to_bounding_box(volumes_bounding_box());
    m_apply_zoom_to_volumes_filter = false;
}

void GLCanvas3D::_zoom_to_bounding_box(const BoundingBoxf3& bbox)
{
    // >>>>>>>>>>>>>>>>>>>> TODO <<<<<<<<<<<<<<<<<<<<<<<<
}

std::pair<int, int> GLCanvas3D::_get_canvas_size() const
{
    std::pair<int, int> ret(0, 0);

    if (m_canvas != nullptr)
        m_canvas->GetSize(&ret.first, &ret.second);

    return ret;
}

} // namespace GUI
} // namespace Slic3r
