#include "GLCanvas3DManager.hpp"
#include "../../slic3r/GUI/GUI.hpp"
#include "../../slic3r/GUI/AppConfig.hpp"

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

bool GLCanvas3DManager::GLInfo::detect()
{
    const char* data = (const char*)::glGetString(GL_VERSION);
    if (data == nullptr)
        return false;

    version = data;

    data = (const char*)::glGetString(GL_SHADING_LANGUAGE_VERSION);
    if (data == nullptr)
        return false;

    glsl_version = data;

    data = (const char*)::glGetString(GL_VENDOR);
    if (data == nullptr)
        return false;

    vendor = data;

    data = (const char*)::glGetString(GL_RENDERER);
    if (data == nullptr)
        return false;

    renderer = data;

    return true;
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
    std::string h2_end   = format_as_html ? "</b>" : "";
    std::string b_start  = format_as_html ? "<b>" : "";
    std::string b_end    = format_as_html ? "</b>" : "";
    std::string line_end = format_as_html ? "<br>" : "\n";

    out << h2_start << "OpenGL installation" << h2_end << line_end;
    out << b_start  << "GL version:   " << b_end << version << line_end;
    out << b_start  << "Vendor:       " << b_end << vendor << line_end;
    out << b_start  << "Renderer:     " << b_end << renderer << line_end;
    out << b_start  << "GLSL version: " << b_end << glsl_version << line_end;

    if (extensions)
    {
        out << h2_start << "Installed extensions:" << h2_end << line_end;

        std::vector<std::string> extensions_list;
        GLint num_extensions;
        ::glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
         
        for (unsigned int i = 0; i < num_extensions; ++i)
        {
            const char* e = (const char*)::glGetStringi(GL_EXTENSIONS, i);
            extensions_list.push_back(e);
        }

        std::sort(extensions_list.begin(), extensions_list.end());
        for (const std::string& ext : extensions_list)
        {
            out << ext << line_end;
        }
    }

    return out.str();
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

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//    if (!m_gl_initialized)
//    {
//        canvas3D->set_current();
//        init_gl();
//    }
//
//    if (!canvas3D->init(m_use_VBOs, m_use_legacy_opengl))
//    {
//        delete canvas3D;
//        canvas3D = nullptr;
//        return false;
//    }
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

    canvas->Bind(wxEVT_SIZE, [canvas3D](wxSizeEvent& evt) { canvas3D->on_size(evt); });
    canvas->Bind(wxEVT_IDLE, [canvas3D](wxIdleEvent& evt) { canvas3D->on_idle(evt); });
    canvas->Bind(wxEVT_CHAR, [canvas3D](wxKeyEvent& evt)  { canvas3D->on_char(evt); });
    canvas->Bind(wxEVT_MOUSEWHEEL, [canvas3D](wxMouseEvent& evt) { canvas3D->on_mouse_wheel(evt); });
    canvas->Bind(wxEVT_TIMER, [canvas3D](wxTimerEvent& evt) { canvas3D->on_timer(evt); });
    canvas->Bind(wxEVT_LEFT_DOWN, [canvas3D](wxMouseEvent& evt) { canvas3D->on_mouse(evt); });
    canvas->Bind(wxEVT_LEFT_UP, [canvas3D](wxMouseEvent& evt) { canvas3D->on_mouse(evt); });
    canvas->Bind(wxEVT_MIDDLE_DOWN, [canvas3D](wxMouseEvent& evt) { canvas3D->on_mouse(evt); });
    canvas->Bind(wxEVT_MIDDLE_UP, [canvas3D](wxMouseEvent& evt) { canvas3D->on_mouse(evt); });
    canvas->Bind(wxEVT_RIGHT_DOWN, [canvas3D](wxMouseEvent& evt) { canvas3D->on_mouse(evt); });
    canvas->Bind(wxEVT_RIGHT_UP, [canvas3D](wxMouseEvent& evt) { canvas3D->on_mouse(evt); });
    canvas->Bind(wxEVT_MOTION, [canvas3D](wxMouseEvent& evt) { canvas3D->on_mouse(evt); });
    canvas->Bind(wxEVT_ENTER_WINDOW, [canvas3D](wxMouseEvent& evt) { canvas3D->on_mouse(evt); });
    canvas->Bind(wxEVT_LEAVE_WINDOW, [canvas3D](wxMouseEvent& evt) { canvas3D->on_mouse(evt); });
    canvas->Bind(wxEVT_LEFT_DCLICK, [canvas3D](wxMouseEvent& evt) { canvas3D->on_mouse(evt); });
    canvas->Bind(wxEVT_MIDDLE_DCLICK, [canvas3D](wxMouseEvent& evt) { canvas3D->on_mouse(evt); });
    canvas->Bind(wxEVT_RIGHT_DCLICK, [canvas3D](wxMouseEvent& evt) { canvas3D->on_mouse(evt); });
    canvas->Bind(wxEVT_PAINT, [canvas3D](wxPaintEvent& evt) { canvas3D->on_paint(evt); });

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
        if (m_gl_info.detect())
        {
            const AppConfig* config = GUI::get_app_config();
            m_use_legacy_opengl = (config == nullptr) || (config->get("use_legacy_opengl") == "1");
            m_use_VBOs = !m_use_legacy_opengl && m_gl_info.is_version_greater_or_equal_to(2, 0);
            m_gl_initialized = true;

            std::cout << "DETECTED OPENGL: " << m_gl_info.version << std::endl;
            std::cout << "USE VBOS = " << (m_use_VBOs ? "YES" : "NO") << std::endl;
            std::cout << "LAYER EDITING ALLOWED = " << (!m_use_legacy_opengl ? "YES" : "NO") << std::endl;
        }
        else
            throw std::runtime_error(std::string("Unable to initialize OpenGL driver\n"));
    }
}

std::string GLCanvas3DManager::get_gl_info(bool format_as_html, bool extensions) const
{
    return m_gl_info.to_string(format_as_html, extensions);
}

bool GLCanvas3DManager::use_VBOs() const
{
    return m_use_VBOs;
}

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
bool GLCanvas3DManager::init(wxGLCanvas* canvas)
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        return (it->second != nullptr) ? _init(*it->second) : false;
    else
        return false;
}

//bool GLCanvas3DManager::init(wxGLCanvas* canvas, bool useVBOs)
//{
//    CanvasesMap::const_iterator it = _get_canvas(canvas);
//    return (it != m_canvases.end()) ? it->second->init(useVBOs, m_use_legacy_opengl) : false;
//}
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

bool GLCanvas3DManager::is_shown_on_screen(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->is_shown_on_screen() : false;
}

void GLCanvas3DManager::set_volumes(wxGLCanvas* canvas, GLVolumeCollection* volumes)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_volumes(volumes);
}

void GLCanvas3DManager::reset_volumes(wxGLCanvas* canvas)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->reset_volumes();
}

void GLCanvas3DManager::deselect_volumes(wxGLCanvas* canvas)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->deselect_volumes();
}

void GLCanvas3DManager::select_volume(wxGLCanvas* canvas, unsigned int id)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->select_volume(id);
}

void GLCanvas3DManager::set_config(wxGLCanvas* canvas, DynamicPrintConfig* config)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_config(config);
}

void GLCanvas3DManager::set_print(wxGLCanvas* canvas, Print* print)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_print(print);
}

void GLCanvas3DManager::set_bed_shape(wxGLCanvas* canvas, const Pointfs& shape)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_bed_shape(shape);
}

void GLCanvas3DManager::set_auto_bed_shape(wxGLCanvas* canvas)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_auto_bed_shape();
}

BoundingBoxf3 GLCanvas3DManager::get_volumes_bounding_box(wxGLCanvas* canvas)
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->volumes_bounding_box() : BoundingBoxf3();
}

void GLCanvas3DManager::set_axes_length(wxGLCanvas* canvas, float length)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_axes_length(length);
}

void GLCanvas3DManager::set_cutting_plane(wxGLCanvas* canvas, float z, const ExPolygons& polygons)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_cutting_plane(z, polygons);
}

bool GLCanvas3DManager::is_layers_editing_enabled(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->is_layers_editing_enabled() : false;
}

bool GLCanvas3DManager::is_layers_editing_allowed(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->is_layers_editing_allowed() : false;
}

bool GLCanvas3DManager::is_shader_enabled(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->is_shader_enabled() : false;
}

void GLCanvas3DManager::enable_layers_editing(wxGLCanvas* canvas, bool enable)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->enable_layers_editing(enable);
}

void GLCanvas3DManager::enable_warning_texture(wxGLCanvas* canvas, bool enable)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->enable_warning_texture(enable);
}

void GLCanvas3DManager::enable_legend_texture(wxGLCanvas* canvas, bool enable)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->enable_legend_texture(enable);
}

void GLCanvas3DManager::enable_picking(wxGLCanvas* canvas, bool enable)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->enable_picking(enable);
}

void GLCanvas3DManager::enable_moving(wxGLCanvas* canvas, bool enable)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->enable_moving(enable);
}

void GLCanvas3DManager::enable_shader(wxGLCanvas* canvas, bool enable)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->enable_shader(enable);
}

void GLCanvas3DManager::allow_multisample(wxGLCanvas* canvas, bool allow)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->allow_multisample(allow);
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

void GLCanvas3DManager::select_view(wxGLCanvas* canvas, const std::string& direction)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->select_view(direction);
}

void GLCanvas3DManager::set_viewport_from_scene(wxGLCanvas* canvas, wxGLCanvas* other)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
    {
        CanvasesMap::iterator other_it = _get_canvas(other);
        if (other_it != m_canvases.end())
            it->second->set_viewport_from_scene(*other_it->second);
    }
}

void GLCanvas3DManager::update_volumes_colors_by_extruder(wxGLCanvas* canvas)
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->update_volumes_colors_by_extruder();
}

void GLCanvas3DManager::render(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->render();
}

void GLCanvas3DManager::register_on_viewport_changed_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_viewport_changed_callback(callback);
}

void GLCanvas3DManager::register_on_double_click_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_double_click_callback(callback);
}

void GLCanvas3DManager::register_on_right_click_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_right_click_callback(callback);
}

void GLCanvas3DManager::register_on_select_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_select_callback(callback);
}

void GLCanvas3DManager::register_on_model_update_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_model_update_callback(callback);
}

void GLCanvas3DManager::register_on_move_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_move_callback(callback);
}

GLCanvas3DManager::CanvasesMap::iterator GLCanvas3DManager::_get_canvas(wxGLCanvas* canvas)
{
    return (canvas == nullptr) ? m_canvases.end() : m_canvases.find(canvas);
}

GLCanvas3DManager::CanvasesMap::const_iterator GLCanvas3DManager::_get_canvas(wxGLCanvas* canvas) const
{
    return (canvas == nullptr) ? m_canvases.end() : m_canvases.find(canvas);
}

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
bool GLCanvas3DManager::_init(GLCanvas3D& canvas)
{
    if (!m_gl_initialized)
    {
//        canvas.set_current();
        init_gl();
    }

    return canvas.init(m_use_VBOs, m_use_legacy_opengl);
}
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

} // namespace GUI
} // namespace Slic3r
