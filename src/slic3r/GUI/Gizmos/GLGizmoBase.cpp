#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_Colors.hpp"

// TODO: Display tooltips quicker on Linux

namespace Slic3r {
namespace GUI {

float GLGizmoBase::INV_ZOOM = 1.0f;


const float GLGizmoBase::Grabber::SizeFactor = 0.05f;
const float GLGizmoBase::Grabber::MinHalfSize = 4.0f;
const float GLGizmoBase::Grabber::DraggingScaleFactor = 1.25f;
const float GLGizmoBase::Grabber::FixedGrabberSize = 16.0f;
const float GLGizmoBase::Grabber::FixedRadiusSize = 80.0f;


ColorRGBA GLGizmoBase::DEFAULT_BASE_COLOR = { 0.625f, 0.625f, 0.625f, 1.0f };
ColorRGBA GLGizmoBase::DEFAULT_DRAG_COLOR = { 1.0f, 1.0f, 1.0f, 1.0f };
ColorRGBA GLGizmoBase::DEFAULT_HIGHLIGHT_COLOR = {1.0f, 0.38f, 0.0f, 1.0f};
std::array<ColorRGBA, 3> GLGizmoBase::AXES_HOVER_COLOR = {{
                                                                { 0.7f, 0.0f, 0.0f, 1.0f },
                                                                { 0.0f, 0.7f, 0.0f, 1.0f },
                                                                { 0.0f, 0.0f, 0.7f, 1.0f }
                                                                }};

std::array<ColorRGBA, 3> GLGizmoBase::AXES_COLOR = {{
                                                                { 1.0, 0.0f, 0.0f, 1.0f },
                                                                { 0.0f, 1.0f, 0.0f, 1.0f },
                                                                { 0.0f, 0.0f, 1.0f, 1.0f }
                                                                }};

ColorRGBA            GLGizmoBase::CONSTRAINED_COLOR   = {0.5f, 0.5f, 0.5f, 1.0f};
ColorRGBA            GLGizmoBase::FLATTEN_COLOR       = {0.96f, 0.93f, 0.93f, 0.5f};
ColorRGBA            GLGizmoBase::FLATTEN_HOVER_COLOR = {1.0f, 1.0f, 1.0f, 0.75f};

// new style color
ColorRGBA            GLGizmoBase::GRABBER_NORMAL_COL        = {1.0f, 1.0f, 1.0f, 1.0f};
ColorRGBA            GLGizmoBase::GRABBER_HOVER_COL         = {0.863f, 0.125f, 0.063f, 1.0f};
ColorRGBA            GLGizmoBase::GRABBER_UNIFORM_COL       = {0, 1.0, 1.0, 1.0f};
ColorRGBA            GLGizmoBase::GRABBER_UNIFORM_HOVER_COL = {0, 0.7, 0.7, 1.0f};


void GLGizmoBase::update_render_colors()
{
    GLGizmoBase::AXES_COLOR = { {
                                ImGuiWrapper::from_ImVec4(RenderColor::colors[RenderCol_Grabber_X]),
                                ImGuiWrapper::from_ImVec4(RenderColor::colors[RenderCol_Grabber_Y]),
                                ImGuiWrapper::from_ImVec4(RenderColor::colors[RenderCol_Grabber_Z])
                                } };

    GLGizmoBase::FLATTEN_COLOR       = ImGuiWrapper::from_ImVec4(RenderColor::colors[RenderCol_Flatten_Plane]);
    GLGizmoBase::FLATTEN_HOVER_COLOR = ImGuiWrapper::from_ImVec4(RenderColor::colors[RenderCol_Flatten_Plane_Hover]);
}

void GLGizmoBase::load_render_colors()
{
    RenderColor::colors[RenderCol_Grabber_X]           = ImGuiWrapper::to_ImVec4(GLGizmoBase::AXES_COLOR[0]);
    RenderColor::colors[RenderCol_Grabber_Y]           = ImGuiWrapper::to_ImVec4(GLGizmoBase::AXES_COLOR[1]);
    RenderColor::colors[RenderCol_Grabber_Z]           = ImGuiWrapper::to_ImVec4(GLGizmoBase::AXES_COLOR[2]);
    RenderColor::colors[RenderCol_Flatten_Plane]       = ImGuiWrapper::to_ImVec4(GLGizmoBase::FLATTEN_COLOR);
    RenderColor::colors[RenderCol_Flatten_Plane_Hover] = ImGuiWrapper::to_ImVec4(GLGizmoBase::FLATTEN_HOVER_COLOR);
}

void GLGizmoBase::Grabber::render(bool hover, float size)
{
    render(size, hover ? hover_color : color, false);
}

float GLGizmoBase::Grabber::get_half_size(float size) const
{
    return std::max(size * SizeFactor, MinHalfSize);
}

float GLGizmoBase::Grabber::get_dragging_half_size(float size) const
{
    return get_half_size(size) * DraggingScaleFactor;
}

GLModel& GLGizmoBase::Grabber::get_cube()
{
    if (!m_cube.is_initialized()) {
        // This cannot be done in constructor, OpenGL is not yet
        // initialized at that point (on Linux at least).
        indexed_triangle_set its = its_make_cube(1., 1., 1.);
        its_translate(its, Vec3f(-0.5, -0.5, -0.5));
        m_cube.init_from(its);
    }
    return m_cube;
}

void GLGizmoBase::Grabber::render(float size, const ColorRGBA& render_color, bool picking)
{
    GLShaderProgram* shader = wxGetApp().get_current_shader();
    if (shader == nullptr)
        return;

    if (!m_cube.is_initialized()) {
        // This cannot be done in constructor, OpenGL is not yet
        // initialized at that point (on Linux at least).
        indexed_triangle_set its = its_make_cube(1., 1., 1.);
        its_translate(its, -0.5f * Vec3f::Ones());
        m_cube.init_from(its);
    }

    //BBS set to fixed size grabber
    //float fullsize = 2 * (dragging ? get_dragging_half_size(size) : get_half_size(size));
    float fullsize = 8.0f;
    if (GLGizmoBase::INV_ZOOM > 0) {
        fullsize = FixedGrabberSize * GLGizmoBase::INV_ZOOM;
    }


    m_cube.set_color(render_color);

    const Camera& camera = wxGetApp().plater()->get_camera();
    const Transform3d view_model_matrix = camera.get_view_matrix() * matrix * Geometry::assemble_transform(center, angles, fullsize * Vec3d::Ones());
    const Transform3d& projection_matrix = camera.get_projection_matrix();
 
    shader->set_uniform("view_model_matrix", view_model_matrix);
    shader->set_uniform("projection_matrix", projection_matrix);
    shader->set_uniform("normal_matrix", (Matrix3d)view_model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose());
    m_cube.render();
}

GLGizmoBase::GLGizmoBase(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : m_parent(parent)
    , m_group_id(-1)
    , m_state(Off)
    , m_shortcut_key(0)
    , m_icon_filename(icon_filename)
    , m_sprite_id(sprite_id)
    , m_hover_id(-1)
    , m_dragging(false)
    , m_imgui(wxGetApp().imgui())
    , m_first_input_window_render(true)
    , m_dirty(false)
{
    m_base_color = DEFAULT_BASE_COLOR;
    m_drag_color = DEFAULT_DRAG_COLOR;
    m_highlight_color = DEFAULT_HIGHLIGHT_COLOR;
}

void GLGizmoBase::set_icon_filename(const std::string &filename) {
    m_icon_filename = filename;
}

void GLGizmoBase::set_hover_id(int id)
{
    if (m_grabbers.empty() || id < (int)m_grabbers.size()) {
        m_hover_id = id;
        on_set_hover_id();
    }
}


void GLGizmoBase::enable_grabber(unsigned int id)
{
    if (id < m_grabbers.size())
        m_grabbers[id].enabled = true;

    on_enable_grabber(id);
}

void GLGizmoBase::disable_grabber(unsigned int id)
{
    if (id < m_grabbers.size())
        m_grabbers[id].enabled = false;

    on_disable_grabber(id);
}

void GLGizmoBase::start_dragging()
{
    m_dragging = true;

    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        m_grabbers[i].dragging = (m_hover_id == i);
    }

    on_start_dragging();
    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format("this %1%, m_hover_id=%2%\n")%this %m_hover_id;
}

void GLGizmoBase::stop_dragging()
{
    m_dragging = false;

    for (int i = 0; i < (int)m_grabbers.size(); ++i)
    {
        m_grabbers[i].dragging = false;
    }

    on_stop_dragging();
    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format("this %1%, m_hover_id=%2%\n")%this %m_hover_id;
}

void GLGizmoBase::update(const UpdateData& data)
{
    if (m_hover_id != -1)
        on_update(data);
}

bool GLGizmoBase::update_items_state()
{
    bool res = m_dirty;
    m_dirty  = false;
    return res;
};

bool GLGizmoBase::GizmoImguiBegin(const std::string &name, int flags)
{
    return m_imgui->begin(name, flags);
}

void GLGizmoBase::GizmoImguiEnd()
{
    last_input_window_width = ImGui::GetWindowWidth();
    m_imgui->end();
}

void GLGizmoBase::GizmoImguiSetNextWIndowPos(float &x, float y, int flag, float pivot_x, float pivot_y)
{
    if (abs(last_input_window_width) > 0.01f) {
        if (x + last_input_window_width > m_parent.get_canvas_size().get_width()) {
            if (last_input_window_width > m_parent.get_canvas_size().get_width()) {
                x = 0;
            } else {
                x = m_parent.get_canvas_size().get_width() - last_input_window_width;
            }
        }
    }

    m_imgui->set_next_window_pos(x, y, flag, pivot_x, pivot_y);
}

ColorRGBA GLGizmoBase::picking_color_component(unsigned int id) const
{
    id = BASE_ID - id;
    if (m_group_id > -1)
        id -= m_group_id;

    return picking_decode(id);
}

void GLGizmoBase::render_grabbers(const BoundingBoxf3& box) const
{
#if ENABLE_FIXED_GRABBER
    render_grabbers((float)(GLGizmoBase::Grabber::FixedGrabberSize));
#else
    render_grabbers((float)((box.size().x() + box.size().y() + box.size().z()) / 3.0));
#endif
}

void GLGizmoBase::render_grabbers(float size) const
{
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;
    shader->start_using();
    shader->set_uniform("emission_factor", 0.1f);
    for (int i = 0; i < (int)m_grabbers.size(); ++i) {
        if (m_grabbers[i].enabled)
            m_grabbers[i].render(m_hover_id == i, size);
    }
    shader->stop_using();
}

void GLGizmoBase::render_grabbers_for_picking(const BoundingBoxf3& box) const
{
    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader != nullptr) {
        shader->start_using();

#if ENABLE_FIXED_GRABBER
        const float mean_size = (float)(GLGizmoBase::Grabber::FixedGrabberSize);
#else
        const float mean_size = (float)((box.size().x() + box.size().y() + box.size().z()) / 3.0);
#endif

        for (unsigned int i = 0; i < (unsigned int)m_grabbers.size(); ++i) {
            if (m_grabbers[i].enabled) {
                m_grabbers[i].color = picking_color_component(i);
                m_grabbers[i].render_for_picking(mean_size);
            }
        }
        shader->stop_using();
    }
}

std::string GLGizmoBase::format(float value, unsigned int decimals) const
{
    return Slic3r::string_printf("%.*f", decimals, value);
}

void GLGizmoBase::set_dirty() {
    m_dirty = true;
}

void GLGizmoBase::render_input_window(float x, float y, float bottom_limit)
{
    on_render_input_window(x, y, bottom_limit);
    if (m_first_input_window_render) {
        // for some reason, the imgui dialogs are not shown on screen in the 1st frame where they are rendered, but show up only with the 2nd rendered frame
        // so, we forces another frame rendering the first time the imgui window is shown
        m_parent.set_as_dirty();
        m_first_input_window_render = false;
    }
}



std::string GLGizmoBase::get_name(bool include_shortcut) const
{
    int key = get_shortcut_key();
    std::string out = on_get_name();
    if (include_shortcut && key >= WXK_CONTROL_A && key <= WXK_CONTROL_Z)
        out += std::string(" [") + char(int('A') + key - int(WXK_CONTROL_A)) + "]";
    return out;
}

} // namespace GUI
} // namespace Slic3r
