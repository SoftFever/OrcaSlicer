#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Colors.hpp"

// TODO: Display tooltips quicker on Linux

namespace Slic3r {
namespace GUI {

float GLGizmoBase::INV_ZOOM = 1.0f;


const float GLGizmoBase::Grabber::SizeFactor = 0.05f;
const float GLGizmoBase::Grabber::MinHalfSize = 4.0f;
const float GLGizmoBase::Grabber::DraggingScaleFactor = 1.25f;
const float GLGizmoBase::Grabber::FixedGrabberSize = 16.0f;
float       GLGizmoBase::Grabber::GrabberSizeFactor   = 1.0f;
const float GLGizmoBase::Grabber::FixedRadiusSize = 80.0f;


std::array<float, 4> GLGizmoBase::DEFAULT_BASE_COLOR = { 0.625f, 0.625f, 0.625f, 1.0f };
std::array<float, 4> GLGizmoBase::DEFAULT_DRAG_COLOR = { 1.0f, 1.0f, 1.0f, 1.0f };
std::array<float, 4> GLGizmoBase::DEFAULT_HIGHLIGHT_COLOR = { 1.0f, 0.38f, 0.0f, 1.0f };
std::array<std::array<float, 4>, 3> GLGizmoBase::AXES_HOVER_COLOR = {{
                                                                { 0.7f, 0.0f, 0.0f, 1.0f },
                                                                { 0.0f, 0.7f, 0.0f, 1.0f },
                                                                { 0.0f, 0.0f, 0.7f, 1.0f }
                                                                }};

std::array<std::array<float, 4>, 3> GLGizmoBase::AXES_COLOR = { {
                                                                { 1.0, 0.0f, 0.0f, 1.0f },
                                                                { 0.0f, 1.0f, 0.0f, 1.0f },
                                                                { 0.0f, 0.0f, 1.0f, 1.0f }
                                                                }};

std::array<float, 4> GLGizmoBase::CONSTRAINED_COLOR = { 0.5f, 0.5f, 0.5f, 1.0f };
std::array<float, 4> GLGizmoBase::FLATTEN_COLOR = { 0.96f, 0.93f, 0.93f, 0.5f };
std::array<float, 4> GLGizmoBase::FLATTEN_HOVER_COLOR = { 1.0f, 1.0f, 1.0f, 0.75f };

// new style color
std::array<float, 4> GLGizmoBase::GRABBER_NORMAL_COL = {1.0f, 1.0f, 1.0f, 1.0f};
std::array<float, 4> GLGizmoBase::GRABBER_HOVER_COL  = {0.863f, 0.125f, 0.063f, 1.0f};
std::array<float, 4> GLGizmoBase::GRABBER_UNIFORM_COL = {0, 1.0, 1.0, 1.0f};
std::array<float, 4> GLGizmoBase::GRABBER_UNIFORM_HOVER_COL = {0, 0.7, 0.7, 1.0f};


void GLGizmoBase::update_render_colors()
{
    GLGizmoBase::AXES_COLOR = { {
                                GLColor(RenderColor::colors[RenderCol_Grabber_X]),
                                GLColor(RenderColor::colors[RenderCol_Grabber_Y]),
                                GLColor(RenderColor::colors[RenderCol_Grabber_Z])
                                } };

    GLGizmoBase::FLATTEN_COLOR = GLColor(RenderColor::colors[RenderCol_Flatten_Plane]);
    GLGizmoBase::FLATTEN_HOVER_COLOR = GLColor(RenderColor::colors[RenderCol_Flatten_Plane_Hover]);
}

void GLGizmoBase::load_render_colors()
{
    RenderColor::colors[RenderCol_Grabber_X] = IMColor(GLGizmoBase::AXES_COLOR[0]);
    RenderColor::colors[RenderCol_Grabber_Y] = IMColor(GLGizmoBase::AXES_COLOR[1]);
    RenderColor::colors[RenderCol_Grabber_Z] = IMColor(GLGizmoBase::AXES_COLOR[2]);
    RenderColor::colors[RenderCol_Flatten_Plane] = IMColor(GLGizmoBase::FLATTEN_COLOR);
    RenderColor::colors[RenderCol_Flatten_Plane_Hover] = IMColor(GLGizmoBase::FLATTEN_HOVER_COLOR);
}

GLGizmoBase::Grabber::Grabber()
    : center(Vec3d::Zero())
    , angles(Vec3d::Zero())
    , dragging(false)
    , enabled(true)
{
    color = GRABBER_NORMAL_COL;
    hover_color = GRABBER_HOVER_COL;
}

void GLGizmoBase::Grabber::render(bool hover, float size) const
{
    std::array<float, 4> render_color;
    if (hover) {
        render_color = hover_color;
    }
    else
        render_color = color;

    render(size, render_color, false);
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
    if (! cube_initialized) {
        // This cannot be done in constructor, OpenGL is not yet
        // initialized at that point (on Linux at least).
        indexed_triangle_set mesh = its_make_cube(1., 1., 1.);
        its_translate(mesh, Vec3f(-0.5, -0.5, -0.5));
        const_cast<GLModel&>(cube).init_from(mesh, BoundingBoxf3{ { -0.5, -0.5, -0.5 }, { 0.5, 0.5, 0.5 } });
        const_cast<bool&>(cube_initialized) = true;
    }
    return cube;
}

void GLGizmoBase::Grabber::render(float size, const std::array<float, 4>& render_color, bool picking) const
{
    if (! cube_initialized) {
        // This cannot be done in constructor, OpenGL is not yet
        // initialized at that point (on Linux at least).
        indexed_triangle_set mesh = its_make_cube(1., 1., 1.);
        its_translate(mesh, Vec3f(-0.5, -0.5, -0.5));
        const_cast<GLModel&>(cube).init_from(mesh, BoundingBoxf3{ { -0.5, -0.5, -0.5 }, { 0.5, 0.5, 0.5 } });
        const_cast<bool&>(cube_initialized) = true;
    }

    //BBS set to fixed size grabber
    //float fullsize = 2 * (dragging ? get_dragging_half_size(size) : get_half_size(size));
    float fullsize = 8.0f;
    if (GLGizmoBase::INV_ZOOM > 0) {
        fullsize = FixedGrabberSize * GLGizmoBase::INV_ZOOM;
    }
    fullsize = fullsize * Grabber::GrabberSizeFactor;

    const_cast<GLModel*>(&cube)->set_color(-1, render_color);

    glsafe(::glPushMatrix());
    glsafe(::glTranslated(center.x(), center.y(), center.z()));
    glsafe(::glRotated(Geometry::rad2deg(angles.z()), 0.0, 0.0, 1.0));
    glsafe(::glRotated(Geometry::rad2deg(angles.y()), 0.0, 1.0, 0.0));
    glsafe(::glRotated(Geometry::rad2deg(angles.x()), 1.0, 0.0, 0.0));
    glsafe(::glScaled(fullsize, fullsize, fullsize));
    cube.render();
    glsafe(::glPopMatrix());
}


bool GLGizmoBase::render_slider_double_input_by_format(
    const SliderInputLayout &layout, const std::string &label, float &value_in, float value_min, float value_max, int keep_digit, DoubleShowType show_type)
{
    ImGui::AlignTextToFramePadding();
    m_imgui->text(label);
    ImGui::SameLine(layout.sliders_left_width);
    ImGui::PushItemWidth(layout.sliders_width);

    float       old_val    = value_in; // (show_type == DoubleShowType::Normal)
    float       value      = value_in; // (show_type == DoubleShowType::Normal)
    std::string format     = "%." + std::to_string(keep_digit) + "f";
    if (show_type == DoubleShowType::PERCENTAGE) {
        format  = "%." + std::to_string(keep_digit) + "f %%";
        old_val = value_in;
        value   = value_in * 100;
    } else if (show_type == DoubleShowType::DEGREE) {
        format  = "%." + std::to_string(keep_digit) + "f " + _u8L("°");
        old_val = value_in;
        value   = Geometry::rad2deg(value_in);
    }

    if (m_imgui->bbl_slider_float_style(("##" + label).c_str(), &value, value_min, value_max, format.c_str())) {
        if (show_type == DoubleShowType::PERCENTAGE) {
            value_in = value * 0.01f;
        } else if (show_type == DoubleShowType::DEGREE) {
            value_in = Geometry::deg2rad(value);
        } else { //(show_type == DoubleShowType::Normal)
            value_in = value;
        }
    }

    ImGui::SameLine(layout.input_left_width);
    ImGui::PushItemWidth(layout.input_width);
    if (ImGui::BBLDragFloat(("##input_" + label).c_str(), &value, 0.05f, value_min, value_max, format.c_str())) {
        if (show_type == DoubleShowType::PERCENTAGE) {
            value_in = value * 0.01f;
        } else if (show_type == DoubleShowType::DEGREE) {
            value_in = Geometry::deg2rad(value);
        } else { //(show_type == DoubleShowType::Normal)
            value_in = value;
        }
    }
    return !is_approx(old_val, value_in);
}

bool GLGizmoBase::render_combo(const std::string &label, const std::vector<std::string> &lines, size_t &selection_idx, float label_width, float item_width)
{
    ImGui::AlignTextToFramePadding();
    m_imgui->text(label);
    ImGui::SameLine(label_width);
    ImGui::PushItemWidth(item_width);

    size_t selection_out = selection_idx;

    const char *selected_str = (selection_idx >= 0 && selection_idx < int(lines.size())) ? lines[selection_idx].c_str() : "";
    if (ImGui::BBLBeginCombo(("##" + label).c_str(), selected_str, 0)) {
        for (size_t line_idx = 0; line_idx < lines.size(); ++line_idx) {
            ImGui::PushID(int(line_idx));
            if (ImGui::Selectable("", line_idx == selection_idx)) selection_out = line_idx;

            ImGui::SameLine();
            ImGui::Text("%s", lines[line_idx].c_str());
            ImGui::PopID();
        }

        ImGui::EndCombo();
    }

    bool is_changed = selection_idx != selection_out;
    selection_idx   = selection_out;

    return is_changed;
}

void GLGizmoBase::render_cross_mark(const Vec3f &target, bool is_single)
{
    double half_length = 4.0;

    glsafe(::glDisable(GL_DEPTH_TEST));

    glsafe(::glLineWidth(2.0f));
    ::glBegin(GL_LINES);
    // draw line for x axis
    ::glColor3f(1.0f, 0.0f, 0.0f);
    if (!is_single) {
        ::glVertex3f(target(0) - half_length, target(1), target(2));
    }
    else {
        ::glVertex3f(target(0), target(1), target(2));
    }
    ::glVertex3f(target(0) + half_length, target(1), target(2));
    // draw line for y axis
    ::glColor3f(0.0f, 1.0f, 0.0f);
    if (!is_single) {
        ::glVertex3f(target(0), target(1) - half_length, target(2));
    } else {
        ::glVertex3f(target(0), target(1), target(2));
    }
    ::glVertex3f(target(0), target(1) + half_length, target(2));
    // draw line for z axis
    ::glColor3f(0.0f, 0.0f, 1.0f);
    if (!is_single) {
        ::glVertex3f(target(0), target(1), target(2) - half_length);
    } else {
        ::glVertex3f(target(0), target(1), target(2));
    }
    ::glVertex3f(target(0), target(1), target(2) + half_length);
    glsafe(::glEnd());
}

GLGizmoBase::GLGizmoBase(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id)
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
    m_cone.init_from(its_make_cone(1., 1., 2 * PI / 24));
    m_sphere.init_from(its_make_sphere(1., (2 * M_PI) / 24.));
    m_cylinder.init_from(its_make_cylinder(1., 1., 2 * PI / 24.));
}

void GLGizmoBase::set_state(EState state)
{
    std::string name = on_get_name_str();
    if (name != "") {
        if (m_state == Off && state == On) {
            start = std::chrono::system_clock::now();
        }
        else if (m_state == On && state == Off) {
            std::chrono::system_clock::time_point end = std::chrono::system_clock::now();
            std::chrono::duration<int> duration = std::chrono::duration_cast<std::chrono::duration<int>>(end - start);
            int times = duration.count();

            NetworkAgent* agent = GUI::wxGetApp().getAgent();
            if (agent) {
                std::string full_name = name + "_duration";
                std::string value = "";
                int existing_time = 0;

                agent->track_get_property(full_name, value);
                try {
                    if (value != "") {
                        existing_time = std::stoi(value);
                    }
                }
                catch (...) {}

                BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << " tool name:" << full_name << " duration: " << times + existing_time;
                agent->track_update_property(full_name, std::to_string(times + existing_time));
            }
        }
    }
    if (m_parent.get_canvas_type() == GLCanvas3D::ECanvasType::CanvasView3D) {
        m_parent.enable_return_toolbar(state == On);
    }
    m_state = state;
    on_set_state();
}

void GLGizmoBase::set_icon_filename(const std::string &filename) {
    m_icon_filename = filename;
}

void GLGizmoBase::set_hover_id(int id)
{
    if (m_grabbers.empty() || (id < (int)m_grabbers.size()))
    {
        m_hover_id = id;
        on_set_hover_id();
    }
}

void GLGizmoBase::set_highlight_color(const std::array<float, 4>& color)
{
    m_highlight_color = color;
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

std::array<float, 4> GLGizmoBase::picking_color_component(unsigned int id) const
{
    static const float INV_255 = 1.0f / 255.0f;

    id = BASE_ID - id;

    if (m_group_id > -1)
        id -= m_group_id;

    // color components are encoded to match the calculation of volume_id made into GLCanvas3D::_picking_pass()
    return std::array<float, 4> {
		float((id >> 0) & 0xff) * INV_255, // red
		float((id >> 8) & 0xff) * INV_255, // green
		float((id >> 16) & 0xff) * INV_255, // blue
		float(picking_checksum_alpha_channel(id & 0xff, (id >> 8) & 0xff, (id >> 16) & 0xff))* INV_255 // checksum for validating against unwanted alpha blending and multi sampling
	};
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
#if ENABLE_FIXED_GRABBER
    float mean_size = (float)(GLGizmoBase::Grabber::FixedGrabberSize);
#else
    float mean_size = (float)((box.size().x() + box.size().y() + box.size().z()) / 3.0);
#endif

    for (unsigned int i = 0; i < (unsigned int)m_grabbers.size(); ++i) {
        if (m_grabbers[i].enabled) {
            std::array<float, 4> color = picking_color_component(i);
            m_grabbers[i].color = color;
            m_grabbers[i].render_for_picking(mean_size);
        }
    }
}

std::string GLGizmoBase::format(float value, unsigned int decimals) const
{
    return Slic3r::string_printf("%.*f", decimals, value);
}

void GLGizmoBase::set_dirty() {
    m_dirty = true;
}

bool GLGizmoBase::use_grabbers(const wxMouseEvent &mouse_event)
{
    bool is_dragging_finished = false;
    if (mouse_event.Moving()) {
        // it should not happen but for sure
        assert(!m_dragging);
        if (m_dragging)
            is_dragging_finished = true;
        else
            return false;
    }

    if (mouse_event.LeftDown()) {
        Selection &selection = m_parent.get_selection();
        if (!selection.is_empty() && m_hover_id != -1 /* &&
            (m_grabbers.empty() || m_hover_id < static_cast<int>(m_grabbers.size()))*/) {
            selection.setup_cache();

            m_dragging = true;
            for (auto &grabber : m_grabbers) grabber.dragging = false;
            //            if (!m_grabbers.empty() && m_hover_id < int(m_grabbers.size()))
            //                m_grabbers[m_hover_id].dragging = true;

            on_start_dragging();

            // Let the plater know that the dragging started
            m_parent.post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_STARTED));
            m_parent.set_as_dirty();
            return true;
        }
    } else if (m_dragging) {
        // when mouse cursor leave window than finish actual dragging operation
        bool is_leaving = mouse_event.Leaving();
        if (mouse_event.Dragging()) {
            Point      mouse_coord(mouse_event.GetX(), mouse_event.GetY());
            auto       ray = m_parent.mouse_ray(mouse_coord);
            UpdateData data(ray, mouse_coord);

            update(data);

            wxGetApp().obj_manipul()->set_dirty();
            m_parent.set_as_dirty();
            return true;
        } else if (mouse_event.LeftUp() || is_leaving || is_dragging_finished) {
            do_stop_dragging(is_leaving);
            return true;
        }
    }
    return false;
}

void GLGizmoBase::do_stop_dragging(bool perform_mouse_cleanup)
{
    for (auto &grabber : m_grabbers) grabber.dragging = false;
    m_dragging = false;

    // NOTE: This should be part of GLCanvas3D
    // Reset hover_id when leave window
    if (perform_mouse_cleanup) m_parent.mouse_up_cleanup();

    on_stop_dragging();

    // There is prediction that after draggign, data are changed
    // Data are updated twice also by canvas3D::reload_scene.
    // Should be fixed.
    m_parent.get_gizmos_manager().update_data();

    wxGetApp().obj_manipul()->set_dirty();

    // Let the plater know that the dragging finished, so a delayed
    // refresh of the scene with the background processing data should
    // be performed.
    m_parent.post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED));
    // updates camera target constraints
    m_parent.refresh_camera_scene_box();
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

void GLGizmoBase::render_glmodel(GLModel &model, const std::array<float, 4> &color, Transform3d view_model_matrix, bool for_picking, float emission_factor)
{
    glPushMatrix();
    GLShaderProgram *shader = nullptr;
    if (for_picking)
        shader = wxGetApp().get_shader("cali");
    else
        shader = wxGetApp().get_shader("gouraud_light");
    if (shader) {
        shader->start_using();
        shader->set_uniform("emission_factor", emission_factor);
        glsafe(::glMultMatrixd(view_model_matrix.data()));

        model.set_color(-1, color);
        model.render();

        shader->stop_using();
    }
    glPopMatrix();
}

std::string GLGizmoBase::get_name(bool include_shortcut) const
{
    int key = get_shortcut_key();
    std::string out = on_get_name();
    if (include_shortcut && key >= WXK_CONTROL_A && key <= WXK_CONTROL_Z)
        out += std::string(" [") + char(int('A') + key - int(WXK_CONTROL_A)) + "]";
    return out;
}



// Produce an alpha channel checksum for the red green blue components. The alpha channel may then be used to verify, whether the rgb components
// were not interpolated by alpha blending or multi sampling.
unsigned char picking_checksum_alpha_channel(unsigned char red, unsigned char green, unsigned char blue)
{
	// 8 bit hash for the color
	unsigned char b = ((((37 * red) + green) & 0x0ff) * 37 + blue) & 0x0ff;
	// Increase enthropy by a bit reversal
	b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
	b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
	b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
	// Flip every second bit to increase the enthropy even more.
	b ^= 0x55;
	return b;
}


} // namespace GUI
} // namespace Slic3r
