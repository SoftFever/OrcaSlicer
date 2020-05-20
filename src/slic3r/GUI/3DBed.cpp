#include "libslic3r/libslic3r.h"

#include "3DBed.hpp"

#include "libslic3r/Polygon.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/BoundingBox.hpp"
#if ENABLE_GCODE_VIEWER
#include "libslic3r/Geometry.hpp"
#endif // ENABLE_GCODE_VIEWER

#include "GUI_App.hpp"
#include "PresetBundle.hpp"
#include "GLCanvas3D.hpp"
#if ENABLE_GCODE_VIEWER
#include "3DScene.hpp"
#endif // ENABLE_GCODE_VIEWER
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
//#if ENABLE_SHADERS_MANAGER
//#include "GLShader.hpp"
//#endif // ENABLE_SHADERS_MANAGER
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

#include <GL/glew.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/filesystem/operations.hpp>
#if ENABLE_GCODE_VIEWER
#include <boost/log/trivial.hpp>
#endif // ENABLE_GCODE_VIEWER

static const float GROUND_Z = -0.02f;

namespace Slic3r {
namespace GUI {

bool GeometryBuffer::set_from_triangles(const Polygons& triangles, float z, bool generate_tex_coords)
{
    m_vertices.clear();
    unsigned int v_size = 3 * (unsigned int)triangles.size();

    if (v_size == 0)
        return false;

    m_vertices = std::vector<Vertex>(v_size, Vertex());

    float min_x = unscale<float>(triangles[0].points[0](0));
    float min_y = unscale<float>(triangles[0].points[0](1));
    float max_x = min_x;
    float max_y = min_y;

    unsigned int v_count = 0;
    for (const Polygon& t : triangles)
    {
        for (unsigned int i = 0; i < 3; ++i)
        {
            Vertex& v = m_vertices[v_count];

            const Point& p = t.points[i];
            float x = unscale<float>(p(0));
            float y = unscale<float>(p(1));

            v.position[0] = x;
            v.position[1] = y;
            v.position[2] = z;

            if (generate_tex_coords)
            {
                v.tex_coords[0] = x;
                v.tex_coords[1] = y;

                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }

            ++v_count;
        }
    }

    if (generate_tex_coords)
    {
        float size_x = max_x - min_x;
        float size_y = max_y - min_y;

        if ((size_x != 0.0f) && (size_y != 0.0f))
        {
            float inv_size_x = 1.0f / size_x;
            float inv_size_y = -1.0f / size_y;
            for (Vertex& v : m_vertices)
            {
                v.tex_coords[0] = (v.tex_coords[0] - min_x) * inv_size_x;
                v.tex_coords[1] = (v.tex_coords[1] - min_y) * inv_size_y;
            }
        }
    }

    return true;
}

bool GeometryBuffer::set_from_lines(const Lines& lines, float z)
{
    m_vertices.clear();

    unsigned int v_size = 2 * (unsigned int)lines.size();
    if (v_size == 0)
        return false;

    m_vertices = std::vector<Vertex>(v_size, Vertex());

    unsigned int v_count = 0;
    for (const Line& l : lines)
    {
        Vertex& v1 = m_vertices[v_count];
        v1.position[0] = unscale<float>(l.a(0));
        v1.position[1] = unscale<float>(l.a(1));
        v1.position[2] = z;
        ++v_count;

        Vertex& v2 = m_vertices[v_count];
        v2.position[0] = unscale<float>(l.b(0));
        v2.position[1] = unscale<float>(l.b(1));
        v2.position[2] = z;
        ++v_count;
    }

    return true;
}

const float* GeometryBuffer::get_vertices_data() const
{
    return (m_vertices.size() > 0) ? (const float*)m_vertices.data() : nullptr;
}

#if ENABLE_GCODE_VIEWER
const float Bed3D::Axes::DefaultStemRadius = 0.5f;
const float Bed3D::Axes::DefaultStemLength = 25.0f;
const float Bed3D::Axes::DefaultTipRadius = 2.5f * Bed3D::Axes::DefaultStemRadius;
const float Bed3D::Axes::DefaultTipLength = 5.0f;
#else
const double Bed3D::Axes::Radius = 0.5;
const double Bed3D::Axes::ArrowBaseRadius = 2.5 * Bed3D::Axes::Radius;
const double Bed3D::Axes::ArrowLength = 5.0;
#endif // ENABLE_GCODE_VIEWER

#if ENABLE_GCODE_VIEWER
void Bed3D::Axes::set_stem_length(float length)
{
    m_stem_length = length;
    m_arrow.reset();
}
#else
Bed3D::Axes::Axes()
{
    m_quadric = ::gluNewQuadric();
    if (m_quadric != nullptr)
        ::gluQuadricDrawStyle(m_quadric, GLU_FILL);
}

Bed3D::Axes::~Axes()
{
    if (m_quadric != nullptr)
        ::gluDeleteQuadric(m_quadric);
}
#endif // ENABLE_GCODE_VIEWER

void Bed3D::Axes::render() const
{
#if ENABLE_GCODE_VIEWER
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#if ENABLE_SHADERS_MANAGER
    auto render_axis = [this](const Transform3f& transform) {
#else
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
    auto render_axis = [this](const Transform3f& transform, GLint color_id, const std::array<float, 4>& color) {
        if (color_id >= 0)
            glsafe(::glUniform4fv(color_id, 1, (const GLfloat*)color.data()));
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#endif // ENABLE_SHADERS_MANAGER
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

        glsafe(::glPushMatrix());
        glsafe(::glMultMatrixf(transform.data()));
        m_arrow.render();
        glsafe(::glPopMatrix());
    };

    m_arrow.init_from(stilized_arrow(16, DefaultTipRadius, DefaultTipLength, DefaultStemRadius, m_stem_length));
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#if ENABLE_SHADERS_MANAGER
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;
#else
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
    if (!m_shader.init("gouraud_light.vs", "gouraud_light.fs"))
        BOOST_LOG_TRIVIAL(error) << "Unable to initialize gouraud_light shader: please, check that the files gouraud_light.vs and gouraud_light.fs are available";

    if (!m_shader.is_initialized())
        return;
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#endif // ENABLE_SHADERS_MANAGER
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

    glsafe(::glEnable(GL_DEPTH_TEST));

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#if ENABLE_SHADERS_MANAGER
    shader->start_using();

    // x axis
    shader->set_uniform("uniform_color", { 0.75f, 0.0f, 0.0f, 1.0f });
    render_axis(Geometry::assemble_transform(m_origin, { 0.0, 0.5 * M_PI, 0.0f }).cast<float>());

    // y axis
    shader->set_uniform("uniform_color", { 0.0f, 0.75f, 0.0f, 1.0f });
    render_axis(Geometry::assemble_transform(m_origin, { -0.5 * M_PI, 0.0, 0.0f }).cast<float>());

    // z axis
    shader->set_uniform("uniform_color", { 0.0f, 0.0f, 0.75f, 1.0f });
    render_axis(Geometry::assemble_transform(m_origin).cast<float>());

    shader->stop_using();
#else
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
    m_shader.start_using();
    GLint color_id = ::glGetUniformLocation(m_shader.get_shader_program_id(), "uniform_color");

    // x axis
    render_axis(Geometry::assemble_transform(m_origin, { 0.0, 0.5 * M_PI, 0.0f }).cast<float>(), color_id, { 0.75f, 0.0f, 0.0f, 1.0f });

    // y axis
    render_axis(Geometry::assemble_transform(m_origin, { -0.5 * M_PI, 0.0, 0.0f }).cast<float>(), color_id, { 0.0f, 0.75f, 0.0f, 1.0f });

    // z axis
    render_axis(Geometry::assemble_transform(m_origin).cast<float>(), color_id, { 0.0f, 0.0f, 0.75f, 1.0f });

    m_shader.stop_using();
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#endif // ENABLE_SHADERS_MANAGER
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

    glsafe(::glDisable(GL_DEPTH_TEST));
#else
    if (m_quadric == nullptr)
        return;

    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glEnable(GL_LIGHTING));

    // x axis
    glsafe(::glColor3fv(AXES_COLOR[0]));
    glsafe(::glPushMatrix());
    glsafe(::glTranslated(origin(0), origin(1), origin(2)));
    glsafe(::glRotated(90.0, 0.0, 1.0, 0.0));
    render_axis(length(0));
    glsafe(::glPopMatrix());

    // y axis
    glsafe(::glColor3fv(AXES_COLOR[1]));
    glsafe(::glPushMatrix());
    glsafe(::glTranslated(origin(0), origin(1), origin(2)));
    glsafe(::glRotated(-90.0, 1.0, 0.0, 0.0));
    render_axis(length(1));
    glsafe(::glPopMatrix());

    // z axis
    glsafe(::glColor3fv(AXES_COLOR[2]));
    glsafe(::glPushMatrix());
    glsafe(::glTranslated(origin(0), origin(1), origin(2)));
    render_axis(length(2));
    glsafe(::glPopMatrix());

    glsafe(::glDisable(GL_LIGHTING));
    glsafe(::glDisable(GL_DEPTH_TEST));
#endif // !ENABLE_GCODE_VIEWER
}

#if !ENABLE_GCODE_VIEWER
void Bed3D::Axes::render_axis(double length) const
{
    ::gluQuadricOrientation(m_quadric, GLU_OUTSIDE);
    ::gluCylinder(m_quadric, Radius, Radius, length, 32, 1);
    ::gluQuadricOrientation(m_quadric, GLU_INSIDE);
    ::gluDisk(m_quadric, 0.0, Radius, 32, 1);
    glsafe(::glTranslated(0.0, 0.0, length));
    ::gluQuadricOrientation(m_quadric, GLU_OUTSIDE);
    ::gluCylinder(m_quadric, ArrowBaseRadius, 0.0, ArrowLength, 32, 1);
    ::gluQuadricOrientation(m_quadric, GLU_INSIDE);
    ::gluDisk(m_quadric, 0.0, ArrowBaseRadius, 32, 1);
}
#endif // !ENABLE_GCODE_VIEWER

Bed3D::Bed3D()
    : m_type(Custom)
    , m_vbo_id(0)
    , m_scale_factor(1.0f)
{
}

bool Bed3D::set_shape(const Pointfs& shape, const std::string& custom_texture, const std::string& custom_model)
{
    auto check_texture = [](const std::string& texture) {
        return !texture.empty() && (boost::algorithm::iends_with(texture, ".png") || boost::algorithm::iends_with(texture, ".svg")) && boost::filesystem::exists(texture);
    };

    auto check_model = [](const std::string& model) {
        return !model.empty() && boost::algorithm::iends_with(model, ".stl") && boost::filesystem::exists(model);
    };

    auto [new_type, system_model, system_texture] = detect_type(shape);

    std::string texture_filename = custom_texture.empty() ? system_texture : custom_texture;
    if (!check_texture(texture_filename))
        texture_filename.clear();

    std::string model_filename = custom_model.empty() ? system_model : custom_model;
    if (!check_model(model_filename))
        model_filename.clear();

    if ((m_shape == shape) && (m_type == new_type) && (m_texture_filename == texture_filename) && (m_model_filename == model_filename))
        // No change, no need to update the UI.
        return false;

    m_shape = shape;
    m_texture_filename = texture_filename;
    m_model_filename = model_filename;
    m_type = new_type;

    calc_bounding_boxes();

    ExPolygon poly;
    for (const Vec2d& p : m_shape)
    {
        poly.contour.append(Point(scale_(p(0)), scale_(p(1))));
    }

    calc_triangles(poly);

    const BoundingBox& bed_bbox = poly.contour.bounding_box();
    calc_gridlines(poly, bed_bbox);

    m_polygon = offset_ex(poly.contour, (float)bed_bbox.radius() * 1.7f, jtRound, scale_(0.5))[0].contour;

    reset();
    m_texture.reset();
    m_model.reset();

    // Set the origin and size for rendering the coordinate system axes.
#if ENABLE_GCODE_VIEWER
    m_axes.set_origin({ 0.0, 0.0, static_cast<double>(GROUND_Z) });
    m_axes.set_stem_length(0.1f * static_cast<float>(m_bounding_box.max_size()));
#else
    m_axes.origin = Vec3d(0.0, 0.0, (double)GROUND_Z);
    m_axes.length = 0.1 * m_bounding_box.max_size() * Vec3d::Ones();
#endif // ENABLE_GCODE_VIEWER

    // Let the calee to update the UI.
    return true;
}

bool Bed3D::contains(const Point& point) const
{
    return m_polygon.contains(point);
}

Point Bed3D::point_projection(const Point& point) const
{
    return m_polygon.point_projection(point);
}

void Bed3D::render(GLCanvas3D& canvas, bool bottom, float scale_factor,
                   bool show_axes, bool show_texture) const
{
    m_scale_factor = scale_factor;

    if (show_axes)
        render_axes();

    glsafe(::glEnable(GL_DEPTH_TEST));

    switch (m_type)
    {
    case System: { render_system(canvas, bottom, show_texture); break; }
    default:
    case Custom: { render_custom(canvas, bottom, show_texture); break; }
    }

    glsafe(::glDisable(GL_DEPTH_TEST));
}

void Bed3D::calc_bounding_boxes() const
{
    m_bounding_box = BoundingBoxf3();
    for (const Vec2d& p : m_shape)
    {
        m_bounding_box.merge(Vec3d(p(0), p(1), 0.0));
    }

    m_extended_bounding_box = m_bounding_box;

    // extend to contain axes
#if ENABLE_GCODE_VIEWER
    m_extended_bounding_box.merge(m_axes.get_total_length() * Vec3d::Ones());
#else
    m_extended_bounding_box.merge(m_axes.length + Axes::ArrowLength * Vec3d::Ones());
#endif // ENABLE_GCODE_VIEWER

    // extend to contain model, if any
    if (!m_model.get_filename().empty())
        m_extended_bounding_box.merge(m_model.get_transformed_bounding_box());
}

void Bed3D::calc_triangles(const ExPolygon& poly)
{
    Polygons triangles;
    poly.triangulate(&triangles);

    if (!m_triangles.set_from_triangles(triangles, GROUND_Z, true))
        printf("Unable to create bed triangles\n");
}

void Bed3D::calc_gridlines(const ExPolygon& poly, const BoundingBox& bed_bbox)
{
    Polylines axes_lines;
    for (coord_t x = bed_bbox.min(0); x <= bed_bbox.max(0); x += scale_(10.0))
    {
        Polyline line;
        line.append(Point(x, bed_bbox.min(1)));
        line.append(Point(x, bed_bbox.max(1)));
        axes_lines.push_back(line);
    }
    for (coord_t y = bed_bbox.min(1); y <= bed_bbox.max(1); y += scale_(10.0))
    {
        Polyline line;
        line.append(Point(bed_bbox.min(0), y));
        line.append(Point(bed_bbox.max(0), y));
        axes_lines.push_back(line);
    }

    // clip with a slightly grown expolygon because our lines lay on the contours and may get erroneously clipped
    Lines gridlines = to_lines(intersection_pl(axes_lines, offset(poly, (float)SCALED_EPSILON)));

    // append bed contours
    Lines contour_lines = to_lines(poly);
    std::copy(contour_lines.begin(), contour_lines.end(), std::back_inserter(gridlines));

    if (!m_gridlines.set_from_lines(gridlines, GROUND_Z))
        printf("Unable to create bed grid lines\n");
}

static std::string system_print_bed_model(const Preset &preset)
{
	std::string out;
	const VendorProfile::PrinterModel *pm = PresetUtils::system_printer_model(preset);
	if (pm != nullptr && ! pm->bed_model.empty())
		out = Slic3r::resources_dir() + "/profiles/" + preset.vendor->id + "/" + pm->bed_model;
	return out;
}

static std::string system_print_bed_texture(const Preset &preset)
{
	std::string out;
	const VendorProfile::PrinterModel *pm = PresetUtils::system_printer_model(preset);
	if (pm != nullptr && ! pm->bed_texture.empty())
		out = Slic3r::resources_dir() + "/profiles/" + preset.vendor->id + "/" + pm->bed_texture;
	return out;
}

std::tuple<Bed3D::EType, std::string, std::string> Bed3D::detect_type(const Pointfs& shape) const
{
    auto bundle = wxGetApp().preset_bundle;
    if (bundle != nullptr)
    {
        const Preset* curr = &bundle->printers.get_selected_preset();
        while (curr != nullptr)
        {
            if (curr->config.has("bed_shape"))
            {
                if (shape == dynamic_cast<const ConfigOptionPoints*>(curr->config.option("bed_shape"))->values)
                {
                    std::string model_filename = system_print_bed_model(*curr);
                    std::string texture_filename = system_print_bed_texture(*curr);
                    if (!model_filename.empty() && !texture_filename.empty())
                        return std::make_tuple(System, model_filename, texture_filename);
                }
            }

            curr = bundle->printers.get_preset_parent(*curr);
        }
    }

    return std::make_tuple(Custom, "", "");
}

void Bed3D::render_axes() const
{
    if (!m_shape.empty())
        m_axes.render();
}

void Bed3D::render_system(GLCanvas3D& canvas, bool bottom, bool show_texture) const
{
    if (!bottom)
        render_model();

    if (show_texture)
        render_texture(bottom, canvas);
}

void Bed3D::render_texture(bool bottom, GLCanvas3D& canvas) const
{
    if (m_texture_filename.empty())
    {
        m_texture.reset();
        render_default(bottom);
        return;
    }

    if ((m_texture.get_id() == 0) || (m_texture.get_source() != m_texture_filename))
    {
        m_texture.reset();

        if (boost::algorithm::iends_with(m_texture_filename, ".svg"))
        {
            // use higher resolution images if graphic card and opengl version allow
            GLint max_tex_size = OpenGLManager::get_gl_info().get_max_tex_size();
            if ((m_temp_texture.get_id() == 0) || (m_temp_texture.get_source() != m_texture_filename))
            {
                // generate a temporary lower resolution texture to show while no main texture levels have been compressed
                if (!m_temp_texture.load_from_svg_file(m_texture_filename, false, false, false, max_tex_size / 8))
                {
                    render_default(bottom);
                    return;
                }
                canvas.request_extra_frame();
            }

            // starts generating the main texture, compression will run asynchronously
            if (!m_texture.load_from_svg_file(m_texture_filename, true, true, true, max_tex_size))
            {
                render_default(bottom);
                return;
            }
        }
        else if (boost::algorithm::iends_with(m_texture_filename, ".png"))
        {
            // generate a temporary lower resolution texture to show while no main texture levels have been compressed
            if ((m_temp_texture.get_id() == 0) || (m_temp_texture.get_source() != m_texture_filename))
            {
                if (!m_temp_texture.load_from_file(m_texture_filename, false, GLTexture::None, false))
                {
                    render_default(bottom);
                    return;
                }
                canvas.request_extra_frame();
            }

            // starts generating the main texture, compression will run asynchronously
            if (!m_texture.load_from_file(m_texture_filename, true, GLTexture::MultiThreaded, true))
            {
                render_default(bottom);
                return;
            }
        }
        else
        {
            render_default(bottom);
            return;
        }
    }
    else if (m_texture.unsent_compressed_data_available())
    {
        // sends to gpu the already available compressed levels of the main texture
        m_texture.send_compressed_data_to_gpu();

        // the temporary texture is not needed anymore, reset it
        if (m_temp_texture.get_id() != 0)
            m_temp_texture.reset();

        canvas.request_extra_frame();

    }

    if (m_triangles.get_vertices_count() > 0)
    {
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#if ENABLE_SHADERS_MANAGER
        GLShaderProgram* shader = wxGetApp().get_shader("printbed");
        if (shader != nullptr)
        {
            shader->start_using();
            shader->set_uniform("transparent_background", bottom);
            shader->set_uniform("svg_source", boost::algorithm::iends_with(m_texture.get_source(), ".svg"));
#else
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
        if (m_shader.get_shader_program_id() == 0)
            m_shader.init("printbed.vs", "printbed.fs");

        if (m_shader.is_initialized())
        {
            m_shader.start_using();
            m_shader.set_uniform("transparent_background", bottom);
            m_shader.set_uniform("svg_source", boost::algorithm::iends_with(m_texture.get_source(), ".svg"));
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#endif // ENABLE_SHADERS_MANAGER
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

            if (m_vbo_id == 0)
            {
                glsafe(::glGenBuffers(1, &m_vbo_id));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_vbo_id));
                glsafe(::glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)m_triangles.get_vertices_data_size(), (const GLvoid*)m_triangles.get_vertices_data(), GL_STATIC_DRAW));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
            }

            glsafe(::glEnable(GL_DEPTH_TEST));
            glsafe(::glDepthMask(GL_FALSE));

            glsafe(::glEnable(GL_BLEND));
            glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

            if (bottom)
                glsafe(::glFrontFace(GL_CW));

            unsigned int stride = m_triangles.get_vertex_data_size();

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#if ENABLE_SHADERS_MANAGER
            GLint position_id = shader->get_attrib_location("v_position");
            GLint tex_coords_id = shader->get_attrib_location("v_tex_coords");
#else
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
            GLint position_id = m_shader.get_attrib_location("v_position");
            GLint tex_coords_id = m_shader.get_attrib_location("v_tex_coords");
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#endif // ENABLE_SHADERS_MANAGER
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

            // show the temporary texture while no compressed data is available
            GLuint tex_id = (GLuint)m_temp_texture.get_id();
            if (tex_id == 0)
                tex_id = (GLuint)m_texture.get_id();

            glsafe(::glBindTexture(GL_TEXTURE_2D, tex_id));
            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, m_vbo_id));

            if (position_id != -1)
            {
                glsafe(::glEnableVertexAttribArray(position_id));
                glsafe(::glVertexAttribPointer(position_id, 3, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(intptr_t)m_triangles.get_position_offset()));
            }
            if (tex_coords_id != -1)
            {
                glsafe(::glEnableVertexAttribArray(tex_coords_id));
                glsafe(::glVertexAttribPointer(tex_coords_id, 2, GL_FLOAT, GL_FALSE, stride, (GLvoid*)(intptr_t)m_triangles.get_tex_coords_offset()));
            }

            glsafe(::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)m_triangles.get_vertices_count()));

            if (tex_coords_id != -1)
                glsafe(::glDisableVertexAttribArray(tex_coords_id));

            if (position_id != -1)
                glsafe(::glDisableVertexAttribArray(position_id));

            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
            glsafe(::glBindTexture(GL_TEXTURE_2D, 0));

            if (bottom)
                glsafe(::glFrontFace(GL_CCW));

            glsafe(::glDisable(GL_BLEND));
            glsafe(::glDepthMask(GL_TRUE));

//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#if ENABLE_SHADERS_MANAGER
            shader->stop_using();
#else
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
            m_shader.stop_using();
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#endif // ENABLE_SHADERS_MANAGER
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
        }
    }
}

void Bed3D::render_model() const
{
    if (m_model_filename.empty())
        return;

    if ((m_model.get_filename() != m_model_filename) && m_model.init_from_file(m_model_filename))
    {
        // move the model so that its origin (0.0, 0.0, 0.0) goes into the bed shape center and a bit down to avoid z-fighting with the texture quad
        Vec3d shift = m_bounding_box.center();
        shift(2) = -0.03;
        m_model.set_offset(shift);

        // update extended bounding box
        calc_bounding_boxes();
    }

    if (!m_model.get_filename().empty())
    {
        glsafe(::glEnable(GL_LIGHTING));
        m_model.render();
        glsafe(::glDisable(GL_LIGHTING));
    }
}

void Bed3D::render_custom(GLCanvas3D& canvas, bool bottom, bool show_texture) const
{
    if (m_texture_filename.empty() && m_model_filename.empty())
    {
        render_default(bottom);
        return;
    }

    if (!bottom)
        render_model();

    if (show_texture)
        render_texture(bottom, canvas);
}

void Bed3D::render_default(bool bottom) const
{
    m_texture.reset();

    unsigned int triangles_vcount = m_triangles.get_vertices_count();
    if (triangles_vcount > 0)
    {
        bool has_model = !m_model.get_filename().empty();

        glsafe(::glEnable(GL_DEPTH_TEST));
        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

        glsafe(::glEnableClientState(GL_VERTEX_ARRAY));

        if (!has_model && !bottom)
        {
            // draw background
            glsafe(::glDepthMask(GL_FALSE));
            glsafe(::glColor4f(0.35f, 0.35f, 0.35f, 0.4f));
            glsafe(::glNormal3d(0.0f, 0.0f, 1.0f));
            glsafe(::glVertexPointer(3, GL_FLOAT, m_triangles.get_vertex_data_size(), (GLvoid*)m_triangles.get_vertices_data()));
            glsafe(::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)triangles_vcount));
            glsafe(::glDepthMask(GL_TRUE));
        }

        // draw grid
        glsafe(::glLineWidth(3.0f * m_scale_factor));
        if (has_model && !bottom)
            glsafe(::glColor4f(0.75f, 0.75f, 0.75f, 1.0f));
        else
            glsafe(::glColor4f(0.2f, 0.2f, 0.2f, 0.4f));
        glsafe(::glVertexPointer(3, GL_FLOAT, m_triangles.get_vertex_data_size(), (GLvoid*)m_gridlines.get_vertices_data()));
        glsafe(::glDrawArrays(GL_LINES, 0, (GLsizei)m_gridlines.get_vertices_count()));

        glsafe(::glDisableClientState(GL_VERTEX_ARRAY));

        glsafe(::glDisable(GL_BLEND));
    }
}

void Bed3D::reset()
{
    if (m_vbo_id > 0)
    {
        glsafe(::glDeleteBuffers(1, &m_vbo_id));
        m_vbo_id = 0;
    }
}

} // GUI
} // Slic3r
