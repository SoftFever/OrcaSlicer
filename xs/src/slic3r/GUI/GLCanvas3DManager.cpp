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

GLCanvas3DManager::GLCanvas3DManager()
    : m_current(nullptr)
    , m_gl_initialized(false)
    , m_use_legacy_opengl(false)
    , m_use_VBOs(false)
{
}

bool GLCanvas3DManager::add(wxGLCanvas* canvas)
{
    if (canvas == nullptr)
        return false;

    if (_get_canvas(canvas) != m_canvases.end())
        return false;

    GLCanvas3D* canvas3D = new GLCanvas3D(canvas);
    if (canvas3D == nullptr)
        return false;

    canvas3D->bind_event_handlers();
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

bool GLCanvas3DManager::use_VBOs() const
{
    return m_use_VBOs;
}

bool GLCanvas3DManager::init(wxGLCanvas* canvas)
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        return (it->second != nullptr) ? _init(*it->second) : false;
    else
        return false;
}

void GLCanvas3DManager::set_as_dirty(wxGLCanvas* canvas)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_as_dirty();
}

unsigned int GLCanvas3DManager::get_volumes_count(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->get_volumes_count() : 0;
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

void GLCanvas3DManager::update_volumes_selection(wxGLCanvas* canvas, const std::vector<int>& selections)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->update_volumes_selection(selections);
}

int GLCanvas3DManager::check_volumes_outside_state(wxGLCanvas* canvas, const DynamicPrintConfig* config) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->check_volumes_outside_state(config) : false;
}

bool GLCanvas3DManager::move_volume_up(wxGLCanvas* canvas, unsigned int id)
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->move_volume_up(id) : false;
}

bool GLCanvas3DManager::move_volume_down(wxGLCanvas* canvas, unsigned int id)
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->move_volume_down(id) : false;
}

void GLCanvas3DManager::set_objects_selections(wxGLCanvas* canvas, const std::vector<int>& selections)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_objects_selections(selections);
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

void GLCanvas3DManager::set_model(wxGLCanvas* canvas, Model* model)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_model(model);
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

void GLCanvas3DManager::set_color_by(wxGLCanvas* canvas, const std::string& value)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_color_by(value);
}

void GLCanvas3DManager::set_select_by(wxGLCanvas* canvas, const std::string& value)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_select_by(value);
}

void GLCanvas3DManager::set_drag_by(wxGLCanvas* canvas, const std::string& value)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_drag_by(value);
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

bool GLCanvas3DManager::is_reload_delayed(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->is_reload_delayed() : false;
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

void GLCanvas3DManager::enable_gizmos(wxGLCanvas* canvas, bool enable)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->enable_gizmos(enable);
}

void GLCanvas3DManager::enable_shader(wxGLCanvas* canvas, bool enable)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->enable_shader(enable);
}

void GLCanvas3DManager::enable_force_zoom_to_bed(wxGLCanvas* canvas, bool enable)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->enable_force_zoom_to_bed(enable);
}

void GLCanvas3DManager::enable_dynamic_background(wxGLCanvas* canvas, bool enable)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->enable_dynamic_background(enable);
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

void GLCanvas3DManager::update_gizmos_data(wxGLCanvas* canvas)
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->update_gizmos_data();
}

void GLCanvas3DManager::render(wxGLCanvas* canvas) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->render();
}

std::vector<double> GLCanvas3DManager::get_current_print_zs(wxGLCanvas* canvas, bool active_only) const
{
    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->get_current_print_zs(active_only) : std::vector<double>();
}

void GLCanvas3DManager::set_toolpaths_range(wxGLCanvas* canvas, double low, double high)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->set_toolpaths_range(low, high);
}

std::vector<int> GLCanvas3DManager::load_object(wxGLCanvas* canvas, const ModelObject* model_object, int obj_idx, std::vector<int> instance_idxs)
{
    if (model_object == nullptr)
        return std::vector<int>();

    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->load_object(*model_object, obj_idx, instance_idxs) : std::vector<int>();
}

std::vector<int> GLCanvas3DManager::load_object(wxGLCanvas* canvas, const Model* model, int obj_idx)
{
    if (model == nullptr)
        return std::vector<int>();

    CanvasesMap::const_iterator it = _get_canvas(canvas);
    return (it != m_canvases.end()) ? it->second->load_object(*model, obj_idx) : std::vector<int>();
}

void GLCanvas3DManager::reload_scene(wxGLCanvas* canvas, bool force)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->reload_scene(force);
}

void GLCanvas3DManager::load_gcode_preview(wxGLCanvas* canvas, const GCodePreviewData* preview_data, const std::vector<std::string>& str_tool_colors)
{
    if (preview_data == nullptr)
        return;

    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->load_gcode_preview(*preview_data, str_tool_colors);
}

void GLCanvas3DManager::load_preview(wxGLCanvas* canvas, const std::vector<std::string>& str_tool_colors)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->load_preview(str_tool_colors);
}

void GLCanvas3DManager::reset_legend_texture()
{
    for (CanvasesMap::value_type& canvas : m_canvases)
    {
        if (canvas.second != nullptr)
            canvas.second->reset_legend_texture();
    }
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

void GLCanvas3DManager::register_on_select_object_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_select_object_callback(callback);
}

void GLCanvas3DManager::register_on_model_update_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_model_update_callback(callback);
}

void GLCanvas3DManager::register_on_remove_object_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_remove_object_callback(callback);
}

void GLCanvas3DManager::register_on_arrange_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_arrange_callback(callback);
}

void GLCanvas3DManager::register_on_rotate_object_left_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_rotate_object_left_callback(callback);
}

void GLCanvas3DManager::register_on_rotate_object_right_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_rotate_object_right_callback(callback);
}

void GLCanvas3DManager::register_on_scale_object_uniformly_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_scale_object_uniformly_callback(callback);
}

void GLCanvas3DManager::register_on_increase_objects_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_increase_objects_callback(callback);
}

void GLCanvas3DManager::register_on_decrease_objects_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_decrease_objects_callback(callback);
}

void GLCanvas3DManager::register_on_instance_moved_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_instance_moved_callback(callback);
}

void GLCanvas3DManager::register_on_wipe_tower_moved_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_wipe_tower_moved_callback(callback);
}

void GLCanvas3DManager::register_on_enable_action_buttons_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_enable_action_buttons_callback(callback);
}

void GLCanvas3DManager::register_on_gizmo_scale_uniformly_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_gizmo_scale_uniformly_callback(callback);
}

void GLCanvas3DManager::register_on_gizmo_rotate_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_gizmo_rotate_callback(callback);
}

void GLCanvas3DManager::register_on_update_geometry_info_callback(wxGLCanvas* canvas, void* callback)
{
    CanvasesMap::iterator it = _get_canvas(canvas);
    if (it != m_canvases.end())
        it->second->register_on_update_geometry_info_callback(callback);
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

} // namespace GUI
} // namespace Slic3r
