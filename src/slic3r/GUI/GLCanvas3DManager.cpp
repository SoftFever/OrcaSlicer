#include "GLCanvas3DManager.hpp"
#include "../../slic3r/GUI/GUI.hpp"
#include "../../slic3r/GUI/AppConfig.hpp"
#include "../../slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>

#include <wx/glcanvas.h>
#include <wx/timer.h>

#include <vector>
#include <string>
#include <iostream>

namespace Slic3r {
namespace GUI {

GLCanvas3DManager::GLInfo::GLInfo()
    : version("")
    , glsl_version("")
    , vendor("")
    , renderer("")
{
}

void GLCanvas3DManager::GLInfo::detect()
{
    const char* data = (const char*)::glGetString(GL_VERSION);
    if (data != nullptr)
        version = data;

    data = (const char*)::glGetString(GL_SHADING_LANGUAGE_VERSION);
    if (data != nullptr)
        glsl_version = data;

    data = (const char*)::glGetString(GL_VENDOR);
    if (data != nullptr)
        vendor = data;

    data = (const char*)::glGetString(GL_RENDERER);
    if (data != nullptr)
        renderer = data;
}

bool GLCanvas3DManager::GLInfo::is_version_greater_or_equal_to(unsigned int major, unsigned int minor) const
{
    std::vector<std::string> tokens;
    boost::split(tokens, version, boost::is_any_of(" "), boost::token_compress_on);

    if (tokens.empty())
        return false;

    std::vector<std::string> numbers;
    boost::split(numbers, tokens[0], boost::is_any_of("."), boost::token_compress_on);

    unsigned int gl_major = 0;
    unsigned int gl_minor = 0;

    if (numbers.size() > 0)
        gl_major = ::atoi(numbers[0].c_str());

    if (numbers.size() > 1)
        gl_minor = ::atoi(numbers[1].c_str());

    if (gl_major < major)
        return false;
    else if (gl_major > major)
        return true;
    else
        return gl_minor >= minor;
}

std::string GLCanvas3DManager::GLInfo::to_string(bool format_as_html, bool extensions) const
{
    std::stringstream out;

    std::string h2_start = format_as_html ? "<b>" : "";
    std::string h2_end = format_as_html ? "</b>" : "";
    std::string b_start = format_as_html ? "<b>" : "";
    std::string b_end = format_as_html ? "</b>" : "";
    std::string line_end = format_as_html ? "<br>" : "\n";

    out << h2_start << "OpenGL installation" << h2_end << line_end;
    out << b_start << "GL version:   " << b_end << (version.empty() ? "N/A" : version) << line_end;
    out << b_start << "Vendor:       " << b_end << (vendor.empty() ? "N/A" : vendor) << line_end;
    out << b_start << "Renderer:     " << b_end << (renderer.empty() ? "N/A" : renderer) << line_end;
    out << b_start << "GLSL version: " << b_end << (glsl_version.empty() ? "N/A" : glsl_version) << line_end;

    if (extensions)
    {
        std::vector<std::string> extensions_list;
        std::string extensions_str = (const char*)::glGetString(GL_EXTENSIONS);
        boost::split(extensions_list, extensions_str, boost::is_any_of(" "), boost::token_compress_off);

        if (!extensions_list.empty())
        {
            out << h2_start << "Installed extensions:" << h2_end << line_end;

            std::sort(extensions_list.begin(), extensions_list.end());
            for (const std::string& ext : extensions_list)
            {
                out << ext << line_end;
            }
        }
    }

    return out.str();
}

GLCanvas3DManager::EMultisampleState GLCanvas3DManager::s_multisample = GLCanvas3DManager::MS_Unknown;

GLCanvas3DManager::GLCanvas3DManager()
    : m_context(nullptr)
    , m_gl_initialized(false)
    , m_use_legacy_opengl(false)
    , m_use_VBOs(false)
{
}

GLCanvas3DManager::~GLCanvas3DManager()
{
    if (m_context != nullptr)
    {
        delete m_context;
        m_context = nullptr;
    }
}

bool GLCanvas3DManager::add(wxGLCanvas* canvas, Bed3D& bed, Camera& camera, GLToolbar& view_toolbar)
{
    if (canvas == nullptr)
        return false;

    if (_get_canvas(canvas) != m_canvases.end())
        return false;

    GLCanvas3D* canvas3D = new GLCanvas3D(canvas, bed, camera, view_toolbar);
    if (canvas3D == nullptr)
        return false;

    canvas3D->bind_event_handlers();

    if (m_context == nullptr)
    {
        m_context = new wxGLContext(canvas);
        if (m_context == nullptr)
            return false;
    }

    canvas3D->set_context(m_context);

    m_canvases.insert(CanvasesMap::value_type(canvas, canvas3D));

    return true;
}

bool GLCanvas3DManager::remove(wxGLCanvas* canvas)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it == m_canvases.end())
        return false;

    it->second->unbind_event_handlers();
    delete it->second;
    m_canvases.erase(it);

    return true;
}

void GLCanvas3DManager::remove_all()
{
    for (CanvasesMap::value_type& item : m_canvases)
    {
        item.second->unbind_event_handlers();
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
        glewInit();
        m_gl_info.detect();
        const AppConfig* config = GUI::get_app_config();
        m_use_legacy_opengl = (config == nullptr) || (config->get("use_legacy_opengl") == "1");
        m_use_VBOs = !m_use_legacy_opengl && m_gl_info.is_version_greater_or_equal_to(2, 0);
        m_gl_initialized = true;
    }
}

std::string GLCanvas3DManager::get_gl_info(bool format_as_html, bool extensions) const
{
    return m_gl_info.to_string(format_as_html, extensions);
}

bool GLCanvas3DManager::init(wxGLCanvas* canvas)
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        return (it->second != nullptr) ? _init(*it->second) : false;
    else
        return false;
}

GLCanvas3D* GLCanvas3DManager::get_canvas(wxGLCanvas* canvas)
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second : nullptr;
}

wxGLCanvas* GLCanvas3DManager::create_wxglcanvas(wxWindow *parent)
{
    int attribList[] = { WX_GL_RGBA, WX_GL_DOUBLEBUFFER, WX_GL_DEPTH_SIZE, 24, WX_GL_SAMPLE_BUFFERS, GL_TRUE, WX_GL_SAMPLES, 4, 0 };

    if (s_multisample == MS_Unknown)
    {
        _detect_multisample(attribList);
        // debug output
        std::cout << "Multisample " << (can_multisample() ? "enabled" : "disabled") << std::endl;
    }

    if (! can_multisample()) {
        attribList[4] = 0;
    }

    return new wxGLCanvas(parent, wxID_ANY, attribList, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS);
}

GLCanvas3DManager::CanvasesMap::iterator GLCanvas3DManager::_get_canvas(wxGLCanvas* canvas)
{
    return (canvas == nullptr) ? m_canvases.end() : m_canvases.find(canvas);
}

GLCanvas3DManager::CanvasesMap::const_iterator GLCanvas3DManager::_get_canvas(wxGLCanvas* canvas) const
{
    return (canvas == nullptr) ? m_canvases.end() : m_canvases.find(canvas);
}

bool GLCanvas3DManager::_init(GLCanvas3D& canvas)
{
    if (!m_gl_initialized)
        init_gl();

    return canvas.init(m_use_VBOs, m_use_legacy_opengl);
}

void GLCanvas3DManager::_detect_multisample(int* attribList)
{
    int wxVersion = wxMAJOR_VERSION * 10000 + wxMINOR_VERSION * 100 + wxRELEASE_NUMBER;
    const AppConfig* app_config = GUI::get_app_config();
    bool enable_multisample = app_config != nullptr
        && app_config->get("use_legacy_opengl") != "1"
        && wxVersion >= 30003;

    s_multisample = (enable_multisample && wxGLCanvas::IsDisplaySupported(attribList)) ? MS_Enabled : MS_Disabled;
    // Alternative method: it was working on previous version of wxWidgets but not with the latest, at least on Windows
    // s_multisample = enable_multisample && wxGLCanvas::IsExtensionSupported("WGL_ARB_multisample");
}

} // namespace GUI
} // namespace Slic3r
