#include "libslic3r/libslic3r.h"

#include "CoordAxes.hpp"
#include "GUI_App.hpp"
#include "3DScene.hpp"
#include "Plater.hpp"
#include "Camera.hpp"

#include <GL/glew.h>

namespace Slic3r {
namespace GUI {

const float CoordAxes::DefaultStemRadius = 0.5f;
const float CoordAxes::DefaultStemLength = 25.0f;
const float CoordAxes::DefaultTipRadius = 2.5f * CoordAxes::DefaultStemRadius;
const float CoordAxes::DefaultTipLength = 5.0f;

void CoordAxes::render(const Transform3d& trafo, float emission_factor)
{
    auto render_axis = [this](GLShaderProgram& shader, const Transform3d& transform) {
        const Camera& camera = wxGetApp().plater()->get_camera();
        const Transform3d& view_matrix = camera.get_view_matrix();
        const Transform3d matrix = view_matrix * transform;
        shader.set_uniform("view_model_matrix", matrix);
        shader.set_uniform("projection_matrix", camera.get_projection_matrix());
        shader.set_uniform("view_normal_matrix", (Matrix3d)(view_matrix.matrix().block(0, 0, 3, 3) * transform.matrix().block(0, 0, 3, 3).inverse().transpose()));
        m_arrow.render();
    };

    if (!m_arrow.is_initialized())
        m_arrow.init_from(stilized_arrow(16, m_tip_radius, m_tip_length, m_stem_radius, m_stem_length));

    GLShaderProgram* curr_shader = wxGetApp().get_current_shader();
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;

    if (curr_shader != nullptr)
        curr_shader->stop_using();

    shader->start_using();
    shader->set_uniform("emission_factor", emission_factor);

    // Scale the axes if the camera is close to them to avoid issues
    // such as https://github.com/prusa3d/PrusaSlicer/issues/9483
    const Camera& camera = wxGetApp().plater()->get_camera();
    Transform3d scale_tr = Transform3d::Identity();
    scale_tr.scale(std::min(1., camera.get_inv_zoom() * 10.));

    // x axis
    m_arrow.set_color(ColorRGBA::X());
    render_axis(*shader, trafo * Geometry::translation_transform(m_origin) * Geometry::rotation_transform({ 0.0, 0.5 * M_PI, 0.0 }) * scale_tr);

    // y axis
    m_arrow.set_color(ColorRGBA::Y());
    render_axis(*shader, trafo * Geometry::translation_transform(m_origin) * Geometry::rotation_transform({ -0.5 * M_PI, 0.0, 0.0 }) * scale_tr);

    // z axis
    m_arrow.set_color(ColorRGBA::Z());
    render_axis(*shader, trafo * Geometry::translation_transform(m_origin) * scale_tr);

    shader->stop_using();
    if (curr_shader != nullptr)
        curr_shader->start_using();
}

} // GUI
} // Slic3r
