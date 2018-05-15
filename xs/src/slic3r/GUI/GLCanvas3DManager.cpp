#include "GLCanvas3DManager.hpp"
#include "../../slic3r/GUI/GUI.hpp"
#include "../../slic3r/GUI/AppConfig.hpp"

#include <GL/glew.h>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <wx/glcanvas.h>

#include <vector>
#include <string>
#include <iostream>

namespace Slic3r {
namespace GUI {

GLCanvas3DManager::GLVersion::GLVersion()
    : vn_major(0)
    , vn_minor(0)
{
}

bool GLCanvas3DManager::GLVersion::detect()
{
    const char* gl_version = (const char*)::glGetString(GL_VERSION);
    if (gl_version == nullptr)
        return false;

    std::vector<std::string> tokens;
    boost::split(tokens, gl_version, boost::is_any_of(" "), boost::token_compress_on);

    if (tokens.empty())
        return false;

    std::vector<std::string> numbers;
    boost::split(numbers, tokens[0], boost::is_any_of("."), boost::token_compress_on);

    if (numbers.size() > 0)
        vn_major = ::atoi(numbers[0].c_str());

    if (numbers.size() > 1)
        vn_minor = ::atoi(numbers[1].c_str());

    return true;
}

bool GLCanvas3DManager::GLVersion::is_greater_or_equal_to(unsigned int major, unsigned int minor) const
{
    if (vn_major < major)
        return false;
    else if (vn_major > major)
        return true;
    else
        return vn_minor >= minor;
}

GLCanvas3DManager::LayerEditing::LayerEditing()
    : allowed(false)
{
}

GLCanvas3DManager::GLCanvas3DManager()
    : m_gl_initialized(false)
    , m_use_legacy_opengl(false)
    , m_use_VBOs(false)
{
}

bool GLCanvas3DManager::add(wxGLCanvas* canvas, wxGLContext* context)
{
    if (_get_canvas(canvas) != m_canvases.end())
        return false;

    GLCanvas3D* canvas3D = new GLCanvas3D(canvas, context);
    if (canvas3D == nullptr)
        return false;

    canvas->Bind(wxEVT_SIZE, [canvas3D](wxSizeEvent& evt) { canvas3D->on_size(evt); });
    canvas->Bind(wxEVT_IDLE, [canvas3D](wxIdleEvent& evt) { canvas3D->on_idle(evt); });

    m_canvases.insert(CanvasesMap::value_type(canvas, canvas3D));

    std::cout << "canvas added: " << (void*)canvas << " (" << (void*)canvas3D << ")" << std::endl;

    return true;
}

bool GLCanvas3DManager::remove(wxGLCanvas* canvas)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it == m_canvases.end())
        return false;

    delete it->second;
    m_canvases.erase(it);

    std::cout << "canvas removed: " << (void*)canvas << std::endl;

    return true;
}

void GLCanvas3DManager::remove_all()
{
    for (CanvasesMap::value_type& item : m_canvases)
    {
        std::cout << "canvas removed: " << (void*)item.second << std::endl;
        delete item.second;
    }
    m_canvases.clear();
}

unsigned int GLCanvas3DManager::count() const
{
    return (unsigned int)m_canvases.size();
}

void GLCanvas3DManager::init_gl()
{
    if (!m_gl_initialized)
    {
        std::cout << "GLCanvas3DManager::init_gl()" << std::endl;

        glewInit();
        m_gl_version.detect();

        const AppConfig* config = GUI::get_app_config();
        m_use_legacy_opengl = (config == nullptr) || (config->get("use_legacy_opengl") == "1");
        m_use_VBOs = !m_use_legacy_opengl && m_gl_version.is_greater_or_equal_to(2, 0);
        m_layer_editing.allowed = !m_use_legacy_opengl;
        m_gl_initialized = true;

        std::cout << "DETECTED OPENGL: " << m_gl_version.vn_major << "." << m_gl_version.vn_minor << std::endl;
        std::cout << "USE VBOS = " << (m_use_VBOs ? "YES" : "NO") << std::endl;
        std::cout << "LAYER EDITING ALLOWED = " << (m_layer_editing.allowed ? "YES" : "NO") << std::endl;
    }
}

bool GLCanvas3DManager::use_VBOs() const
{
    return m_use_VBOs;
}

bool GLCanvas3DManager::layer_editing_allowed() const
{
    return m_layer_editing.allowed;
}

bool GLCanvas3DManager::is_shown_on_screen(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->is_shown_on_screen() : false;
}

void GLCanvas3DManager::resize(wxGLCanvas* canvas, unsigned int w, unsigned int h)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->resize(w, h);
}

GLVolumeCollection* GLCanvas3DManager::get_volumes(wxGLCanvas* canvas)
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->get_volumes() : nullptr;
}

void GLCanvas3DManager::set_volumes(wxGLCanvas* canvas, GLVolumeCollection* volumes)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_volumes(volumes);
}

void GLCanvas3DManager::set_bed_shape(wxGLCanvas* canvas, const Pointfs& shape)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_bed_shape(shape);
}

Pointf GLCanvas3DManager::get_bed_origin(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->get_bed_origin() : Pointf();
}

void GLCanvas3DManager::set_bed_origin(wxGLCanvas* canvas, const Pointf& origin)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_bed_origin(origin);
}

BoundingBoxf3 GLCanvas3DManager::get_bed_bounding_box(wxGLCanvas* canvas)
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->bed_bounding_box() : BoundingBoxf3();
}

BoundingBoxf3 GLCanvas3DManager::get_volumes_bounding_box(wxGLCanvas* canvas)
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->volumes_bounding_box() : BoundingBoxf3();
}

BoundingBoxf3 GLCanvas3DManager::get_max_bounding_box(wxGLCanvas* canvas)
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->max_bounding_box() : BoundingBoxf3();
}

bool GLCanvas3DManager::is_dirty(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->is_dirty() : false;
}

void GLCanvas3DManager::set_dirty(wxGLCanvas* canvas, bool dirty)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_dirty(dirty);
}

unsigned int GLCanvas3DManager::get_camera_type(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? (unsigned int)it->second->get_camera_type() : 0;
}

void GLCanvas3DManager::set_camera_type(wxGLCanvas* canvas, unsigned int type)
{
    if ((type <= (unsigned int)GLCanvas3D::Camera::CT_Unknown) || ((unsigned int)GLCanvas3D::Camera::CT_Count <= type))
        return;

    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_camera_type((GLCanvas3D::Camera::EType)type);
}

std::string GLCanvas3DManager::get_camera_type_as_string(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->get_camera_type_as_string() : "unknown";
}

float GLCanvas3DManager::get_camera_zoom(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->get_camera_zoom() : 1.0f;
}

void GLCanvas3DManager::set_camera_zoom(wxGLCanvas* canvas, float zoom)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_camera_zoom(zoom);
}

float GLCanvas3DManager::get_camera_phi(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->get_camera_phi() : 0.0f;
}

void GLCanvas3DManager::set_camera_phi(wxGLCanvas* canvas, float phi)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_camera_phi(phi);
}

float GLCanvas3DManager::get_camera_theta(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->get_camera_theta() : 0.0f;
}

void GLCanvas3DManager::set_camera_theta(wxGLCanvas* canvas, float theta)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_camera_theta(theta);
}

float GLCanvas3DManager::get_camera_distance(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->get_camera_distance() : 0.0f;
}

void GLCanvas3DManager::set_camera_distance(wxGLCanvas* canvas, float distance)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_camera_distance(distance);
}

Pointf3 GLCanvas3DManager::get_camera_target(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->get_camera_target() : Pointf3(0.0, 0.0, 0.0);
}

void GLCanvas3DManager::set_camera_target(wxGLCanvas* canvas, const Pointf3* target)
{
    if (target == nullptr)
        return;

    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_camera_target(*target);
}

void GLCanvas3DManager::zoom_to_bed(wxGLCanvas* canvas)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->zoom_to_bed();
}

void GLCanvas3DManager::zoom_to_volumes(wxGLCanvas* canvas)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->zoom_to_volumes();
}

void GLCanvas3DManager::register_on_viewport_changed_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_viewport_changed_callback(callback);
}

GLCanvas3DManager::CanvasesMap::iterator GLCanvas3DManager::_get_canvas(wxGLCanvas* canvas)
{
    return (canvas == nullptr) ? m_canvases.end() : m_canvases.find(canvas);
}

GLCanvas3DManager::CanvasesMap::const_iterator GLCanvas3DManager::_get_canvas(wxGLCanvas* canvas) const
{
    return (canvas == nullptr) ? m_canvases.end() : m_canvases.find(canvas);
}

} // namespace GUI
} // namespace Slic3r
