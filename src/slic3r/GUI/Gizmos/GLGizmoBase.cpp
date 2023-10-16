#include "GLGizmoBase.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

#include <GL/glew.h>

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_Colors.hpp"
#include "slic3r/GUI/Plater.hpp"

// TODO: Display tooltips quicker on Linux

namespace Slic3r {
namespace GUI {

float GLGizmoBase::INV_ZOOM = 1.0f;

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
const float GLGizmoBase::Grabber::SizeFactor = 0.05f;
const float GLGizmoBase::Grabber::MinHalfSize = 1.5f;
const float GLGizmoBase::Grabber::DraggingScaleFactor = 1.25f;

PickingModel GLGizmoBase::Grabber::s_cube;
PickingModel GLGizmoBase::Grabber::s_cone;

GLGizmoBase::Grabber::~Grabber()
{
    if (s_cube.model.is_initialized())
        s_cube.model.reset();

    if (s_cone.model.is_initialized())
        s_cone.model.reset();
}

float GLGizmoBase::Grabber::get_half_size(float size) const
{
    return std::max(size * SizeFactor, MinHalfSize);
}

float GLGizmoBase::Grabber::get_dragging_half_size(float size) const
{
    return get_half_size(size) * DraggingScaleFactor;
}

void GLGizmoBase::Grabber::register_raycasters_for_picking(int id)
{
    picking_id = id;
    // registration will happen on next call to render()
}

void GLGizmoBase::Grabber::unregister_raycasters_for_picking()
{
    wxGetApp().plater()->canvas3D()->remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo, picking_id);
    picking_id = -1;
    raycasters = { nullptr };
}

void GLGizmoBase::Grabber::render(float size, const ColorRGBA& render_color)
{
    GLShaderProgram* shader = wxGetApp().get_current_shader();
    if (shader == nullptr)
        return;

    if (!s_cube.model.is_initialized()) {
        // This cannot be done in constructor, OpenGL is not yet
        // initialized at that point (on Linux at least).
        indexed_triangle_set its = its_make_cube(1.0, 1.0, 1.0);
        its_translate(its, -0.5f * Vec3f::Ones());
        s_cube.model.init_from(its);
        s_cube.mesh_raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(std::move(its)));
    }

    if (!s_cone.model.is_initialized()) {
        indexed_triangle_set its = its_make_cone(0.375, 1.5, double(PI) / 18.0);
        s_cone.model.init_from(its);
        s_cone.mesh_raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(std::move(its)));
    }

    const float half_size = dragging ? get_dragging_half_size(size) : get_half_size(size);

    s_cube.model.set_color(render_color);
    s_cone.model.set_color(render_color);

    const Camera& camera = wxGetApp().plater()->get_camera();
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    const Transform3d& view_matrix = camera.get_view_matrix();
    const Matrix3d view_matrix_no_offset = view_matrix.matrix().block(0, 0, 3, 3);
    std::vector<Transform3d> elements_matrices(GRABBER_ELEMENTS_MAX_COUNT, Transform3d::Identity());
    elements_matrices[0] = matrix * Geometry::translation_transform(center) * Geometry::rotation_transform(angles) * Geometry::scale_transform(2.0 * half_size);
    Transform3d view_model_matrix = view_matrix * elements_matrices[0];

    shader->set_uniform("view_model_matrix", view_model_matrix);
    Matrix3d view_normal_matrix = view_matrix_no_offset * elements_matrices[0].matrix().block(0, 0, 3, 3).inverse().transpose();
    shader->set_uniform("view_normal_matrix", view_normal_matrix);
    s_cube.model.render();

    auto render_extension = [&view_matrix, &view_matrix_no_offset, shader](const Transform3d& matrix) {
        const Transform3d view_model_matrix = view_matrix * matrix;
        shader->set_uniform("view_model_matrix", view_model_matrix);
        const Matrix3d view_normal_matrix = view_matrix_no_offset * matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
        shader->set_uniform("view_normal_matrix", view_normal_matrix);
        s_cone.model.render();
    };

    if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::PosX)) != 0) {
        elements_matrices[1] = elements_matrices[0] * Geometry::translation_transform(Vec3d::UnitX()) * Geometry::rotation_transform({ 0.0, 0.5 * double(PI), 0.0 });
        render_extension(elements_matrices[1]);
    }
    if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::NegX)) != 0) {
        elements_matrices[2] = elements_matrices[0] * Geometry::translation_transform(-Vec3d::UnitX()) * Geometry::rotation_transform({ 0.0, -0.5 * double(PI), 0.0 });
        render_extension(elements_matrices[2]);
    }
    if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::PosY)) != 0) {
        elements_matrices[3] = elements_matrices[0] * Geometry::translation_transform(Vec3d::UnitY()) * Geometry::rotation_transform({ -0.5 * double(PI), 0.0, 0.0 });
        render_extension(elements_matrices[3]);
    }
    if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::NegY)) != 0) {
        elements_matrices[4] = elements_matrices[0] * Geometry::translation_transform(-Vec3d::UnitY()) * Geometry::rotation_transform({ 0.5 * double(PI), 0.0, 0.0 });
        render_extension(elements_matrices[4]);
    }
    if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::PosZ)) != 0) {
        elements_matrices[5] = elements_matrices[0] * Geometry::translation_transform(Vec3d::UnitZ());
        render_extension(elements_matrices[5]);
    }
    if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::NegZ)) != 0) {
        elements_matrices[6] = elements_matrices[0] * Geometry::translation_transform(-Vec3d::UnitZ()) * Geometry::rotation_transform({ double(PI), 0.0, 0.0 });
        render_extension(elements_matrices[6]);
    }

    if (raycasters[0] == nullptr) {
        GLCanvas3D& canvas = *wxGetApp().plater()->canvas3D();
        raycasters[0] = canvas.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, picking_id, *s_cube.mesh_raycaster, elements_matrices[0]);
        if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::PosX)) != 0)
            raycasters[1] = canvas.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, picking_id, *s_cone.mesh_raycaster, elements_matrices[1]);
        if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::NegX)) != 0)
            raycasters[2] = canvas.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, picking_id, *s_cone.mesh_raycaster, elements_matrices[2]);
        if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::PosY)) != 0)
            raycasters[3] = canvas.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, picking_id, *s_cone.mesh_raycaster, elements_matrices[3]);
        if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::NegY)) != 0)
            raycasters[4] = canvas.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, picking_id, *s_cone.mesh_raycaster, elements_matrices[4]);
        if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::PosZ)) != 0)
            raycasters[5] = canvas.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, picking_id, *s_cone.mesh_raycaster, elements_matrices[5]);
        if ((int(extensions) & int(GLGizmoBase::EGrabberExtension::NegZ)) != 0)
            raycasters[6] = canvas.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, picking_id, *s_cone.mesh_raycaster, elements_matrices[6]);
    }
    else {
        for (size_t i = 0; i < GRABBER_ELEMENTS_MAX_COUNT; ++i) {
            if (raycasters[i] != nullptr)
                raycasters[i]->set_transform(elements_matrices[i]);
        }
    }
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
    m_cone.init_from(its_make_cone(1., 1., 2 * PI / 24));
    m_sphere.init_from(its_make_sphere(1., (2 * M_PI) / 24.));
    m_cylinder.init_from(its_make_cylinder(1., 1., 2 * PI / 24.));
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
void GLGizmoBase::register_grabbers_for_picking()
{
    for (size_t i = 0; i < m_grabbers.size(); ++i) {
        m_grabbers[i].register_raycasters_for_picking((m_group_id >= 0) ? m_group_id : i);
    }
}

void GLGizmoBase::unregister_grabbers_for_picking()
{
    for (size_t i = 0; i < m_grabbers.size(); ++i) {
        m_grabbers[i].unregister_raycasters_for_picking();
    }
}
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


void GLGizmoBase::render_grabbers(const BoundingBoxf3& box) const
{
    render_grabbers((float)((box.size().x() + box.size().y() + box.size().z()) / 3.0));
}

void GLGizmoBase::render_grabbers(float size) const
{
    render_grabbers(0, m_grabbers.size() - 1, size, false);
}

void GLGizmoBase::render_grabbers(size_t first, size_t last, float size, bool force_hover) const
{
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;
    shader->start_using();
    shader->set_uniform("emission_factor", 0.1f);
    glsafe(::glDisable(GL_CULL_FACE));
    for (size_t i = first; i <= last; ++i) {
        if (m_grabbers[i].enabled)
            m_grabbers[i].render(force_hover ? true : m_hover_id == (int)i, size);
    }
    glsafe(::glEnable(GL_CULL_FACE));
    shader->stop_using();
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
