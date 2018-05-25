#include "GLCanvas3D.hpp"

#include "../../slic3r/GUI/3DScene.hpp"
#include "../../slic3r/GUI/GLShader.hpp"
#include "../../libslic3r/ClipperUtils.hpp"
#include "../../libslic3r/PrintConfig.hpp"
#include "../../libslic3r/Print.hpp"

#include <GL/glew.h>

#include <wx/glcanvas.h>
#include <wx/image.h>

#include <iostream>
#include <float.h>

static const bool TURNTABLE_MODE = true;
static const float GIMBALL_LOCK_THETA_MAX = 180.0f;
static const float GROUND_Z = -0.02f;

// phi / theta angles to orient the camera.
static const float VIEW_DEFAULT[2] = { 45.0f, 45.0f };
static const float VIEW_LEFT[2] = { 90.0f, 90.0f };
static const float VIEW_RIGHT[2] = { -90.0f, 90.0f };
static const float VIEW_TOP[2] = { 0.0f, 0.0f };
static const float VIEW_BOTTOM[2] = { 0.0f, 180.0f };
static const float VIEW_FRONT[2] = { 0.0f, 90.0f };
static const float VIEW_REAR[2] = { 180.0f, 90.0f };

static const float VARIABLE_LAYER_THICKNESS_BAR_WIDTH = 70.0f;
static const float VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT = 22.0f;

namespace Slic3r {
namespace GUI {

bool GeometryBuffer::set_from_triangles(const Polygons& triangles, float z)
{
    m_data.clear();

    unsigned int size = 9 * (unsigned int)triangles.size();
    if (size == 0)
        return false;

    m_data = std::vector<float>(size, 0.0f);

    unsigned int coord = 0;
    for (const Polygon& t : triangles)
    {
        for (unsigned int v = 0; v < 3; ++v)
        {
            const Point& p = t.points[v];
            m_data[coord++] = (float)unscale(p.x);
            m_data[coord++] = (float)unscale(p.y);
            m_data[coord++] = z;
        }
    }

    return true;
}

bool GeometryBuffer::set_from_lines(const Lines& lines, float z)
{
    m_data.clear();

    unsigned int size = 6 * (unsigned int)lines.size();
    if (size == 0)
        return false;

    m_data = std::vector<float>(size, 0.0f);

    unsigned int coord = 0;
    for (const Line& l : lines)
    {
        m_data[coord++] = (float)unscale(l.a.x);
        m_data[coord++] = (float)unscale(l.a.y);
        m_data[coord++] = z;
        m_data[coord++] = (float)unscale(l.b.x);
        m_data[coord++] = (float)unscale(l.b.y);
        m_data[coord++] = z;
    }

    return true;
}

const float* GeometryBuffer::get_data() const
{
    return m_data.data();
}

unsigned int GeometryBuffer::get_data_size() const
{
    return (unsigned int)m_data.size();
}

Size::Size()
    : m_width(0)
    , m_height(0)
{
}

Size::Size(int width, int height)
    : m_width(width)
    , m_height(height)
{
}

int Size::get_width() const
{
    return m_width;
}

void Size::set_width(int width)
{
    m_width = width;
}

int Size::get_height() const
{
    return m_height;
}

void Size::set_height(int height)
{
    m_height = height;
}

Rect::Rect()
    : m_left(0.0f)
    , m_top(0.0f)
    , m_right(0.0f)
    , m_bottom(0.0f)
{
}

Rect::Rect(float left, float top, float right, float bottom)
    : m_left(left)
    , m_top(top)
    , m_right(right)
    , m_bottom(bottom)
{
}

float Rect::get_left() const
{
    return m_left;
}

void Rect::set_left(float left)
{
    m_left = left;
}

float Rect::get_top() const
{
    return m_top;
}

void Rect::set_top(float top)
{
    m_top = top;
}

float Rect::get_right() const
{
    return m_right;
}

void Rect::set_right(float right)
{
    m_right = right;
}

float Rect::get_bottom() const
{
    return m_bottom;
}

void Rect::set_bottom(float bottom)
{
    m_bottom = bottom;
}

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

    ExPolygon poly;
    for (const Pointf& p : m_shape)
    {
        poly.contour.append(Point(scale_(p.x), scale_(p.y)));
    }

    _calc_triangles(poly);

    const BoundingBox& bed_bbox = poly.contour.bounding_box();
    _calc_gridlines(poly, bed_bbox);

    m_polygon = offset_ex(poly.contour, bed_bbox.radius() * 1.7, jtRound, scale_(0.5))[0].contour;
}

const BoundingBoxf3& GLCanvas3D::Bed::get_bounding_box() const
{
    return m_bounding_box;
}

void GLCanvas3D::Bed::render() const
{
    unsigned int triangles_vcount = m_triangles.get_data_size() / 3;
    if (triangles_vcount > 0)
    {
        ::glDisable(GL_LIGHTING);
        ::glDisable(GL_DEPTH_TEST);

        ::glEnable(GL_BLEND);
        ::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        ::glEnableClientState(GL_VERTEX_ARRAY);

        ::glColor4f(0.8f, 0.6f, 0.5f, 0.4f);
        ::glNormal3d(0.0f, 0.0f, 1.0f);
        ::glVertexPointer(3, GL_FLOAT, 0, (GLvoid*)m_triangles.get_data());
        ::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)triangles_vcount);

        // we need depth test for grid, otherwise it would disappear when looking
        // the object from below
        glEnable(GL_DEPTH_TEST);

        // draw grid
        unsigned int gridlines_vcount = m_gridlines.get_data_size() / 3;

        ::glLineWidth(3.0f);
        ::glColor4f(0.2f, 0.2f, 0.2f, 0.4f);
        ::glVertexPointer(3, GL_FLOAT, 0, (GLvoid*)m_gridlines.get_data());
        ::glDrawArrays(GL_LINES, 0, (GLsizei)gridlines_vcount);

        ::glDisableClientState(GL_VERTEX_ARRAY);

        ::glDisable(GL_BLEND);
    }
}

void GLCanvas3D::Bed::_calc_bounding_box()
{
    m_bounding_box = BoundingBoxf3();
    for (const Pointf& p : m_shape)
    {
        m_bounding_box.merge(Pointf3(p.x, p.y, 0.0));
    }
}

void GLCanvas3D::Bed::_calc_triangles(const ExPolygon& poly)
{
    Polygons triangles;
    poly.triangulate(&triangles);

    if (!m_triangles.set_from_triangles(triangles, GROUND_Z))
        printf("Unable to create bed triangles\n");
}

void GLCanvas3D::Bed::_calc_gridlines(const ExPolygon& poly, const BoundingBox& bed_bbox)
{
    Polylines axes_lines;
    for (coord_t x = bed_bbox.min.x; x <= bed_bbox.max.x; x += scale_(10.0))
    {
        Polyline line;
        line.append(Point(x, bed_bbox.min.y));
        line.append(Point(x, bed_bbox.max.y));
        axes_lines.push_back(line);
    }
    for (coord_t y = bed_bbox.min.y; y <= bed_bbox.max.y; y += scale_(10.0))
    {
        Polyline line;
        line.append(Point(bed_bbox.min.x, y));
        line.append(Point(bed_bbox.max.x, y));
        axes_lines.push_back(line);
    }

    // clip with a slightly grown expolygon because our lines lay on the contours and may get erroneously clipped
    Lines gridlines = to_lines(intersection_pl(axes_lines, offset(poly, SCALED_EPSILON)));

    // append bed contours
    Lines contour_lines = to_lines(poly);
    std::copy(contour_lines.begin(), contour_lines.end(), std::back_inserter(gridlines));

    if (!m_gridlines.set_from_lines(gridlines, GROUND_Z))
        printf("Unable to create bed grid lines\n");
}

GLCanvas3D::Axes::Axes()
    : m_length(0.0f)
{
}

const Pointf3& GLCanvas3D::Axes::get_origin() const
{
    return m_origin;
}

void GLCanvas3D::Axes::set_origin(const Pointf3& origin)
{
    m_origin = origin;
}

float GLCanvas3D::Axes::get_length() const
{
    return m_length;
}

void GLCanvas3D::Axes::set_length(float length)
{
    m_length = length;
}

void GLCanvas3D::Axes::render() const
{
    ::glDisable(GL_LIGHTING);
    // disable depth testing so that axes are not covered by ground
    ::glDisable(GL_DEPTH_TEST);
    ::glLineWidth(2.0f);
    ::glBegin(GL_LINES);
    // draw line for x axis
    ::glColor3f(1.0f, 0.0f, 0.0f);
    ::glVertex3f((float)m_origin.x, (float)m_origin.y, (float)m_origin.z);
    ::glVertex3f((float)m_origin.x + m_length, (float)m_origin.y, (float)m_origin.z);
     // draw line for y axis
    ::glColor3f(0.0f, 1.0f, 0.0f);
    ::glVertex3f((float)m_origin.x, (float)m_origin.y, (float)m_origin.z);
    ::glVertex3f((float)m_origin.x, (float)m_origin.y + m_length, (float)m_origin.z);
    ::glEnd();
    // draw line for Z axis
    // (re-enable depth test so that axis is correctly shown when objects are behind it)
    ::glEnable(GL_DEPTH_TEST);
    ::glBegin(GL_LINES);
    ::glColor3f(0.0f, 0.0f, 1.0f);
    ::glVertex3f((float)m_origin.x, (float)m_origin.y, (float)m_origin.z);
    ::glVertex3f((float)m_origin.x, (float)m_origin.y, (float)m_origin.z + m_length);
    ::glEnd();
}

GLCanvas3D::CuttingPlane::CuttingPlane()
    : m_z(-1.0f)
{
}

bool GLCanvas3D::CuttingPlane::set(float z, const ExPolygons& polygons)
{
    m_z = z;

    // grow slices in order to display them better
    ExPolygons expolygons = offset_ex(polygons, scale_(0.1));
    Lines lines = to_lines(expolygons);
    return m_lines.set_from_lines(lines, m_z);
}

void GLCanvas3D::CuttingPlane::render(const BoundingBoxf3& bb) const
{
    ::glDisable(GL_LIGHTING);
    _render_plane(bb);
    _render_contour();
}

void GLCanvas3D::CuttingPlane::_render_plane(const BoundingBoxf3& bb) const
{
    if (m_z >= 0.0f)
    {
        ::glDisable(GL_CULL_FACE);
        ::glEnable(GL_BLEND);
        ::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        float margin = 20.0f;
        float min_x = bb.min.x - margin;
        float max_x = bb.max.x + margin;
        float min_y = bb.min.y - margin;
        float max_y = bb.max.y + margin;

        ::glBegin(GL_QUADS);
        ::glColor4f(0.8f, 0.8f, 0.8f, 0.5f);
        ::glVertex3f(min_x, min_y, m_z);
        ::glVertex3f(max_x, min_y, m_z);
        ::glVertex3f(max_x, max_y, m_z);
        ::glVertex3f(min_x, max_y, m_z);
        ::glEnd();

        ::glEnable(GL_CULL_FACE);
        ::glDisable(GL_BLEND);
    }
}

void GLCanvas3D::CuttingPlane::_render_contour() const
{
    ::glEnableClientState(GL_VERTEX_ARRAY);

    if (m_z >= 0.0f)
    {
        unsigned int lines_vcount = m_lines.get_data_size() / 3;

        ::glLineWidth(2.0f);
        ::glColor3f(0.0f, 0.0f, 0.0f);
        ::glVertexPointer(3, GL_FLOAT, 0, (GLvoid*)m_lines.get_data());
        ::glDrawArrays(GL_LINES, 0, (GLsizei)lines_vcount);
    }

    ::glDisableClientState(GL_VERTEX_ARRAY);
}

GLCanvas3D::Shader::Shader()
    : m_shader(nullptr)
{
}

GLCanvas3D::Shader::~Shader()
{
    _reset();
}

bool GLCanvas3D::Shader::init(const std::string& vertex_shader_filename, const std::string& fragment_shader_filename)
{
    if (is_initialized())
        return true;

    m_shader = new GLShader();
    if (m_shader != nullptr)
    {
        if (!m_shader->load_from_file(fragment_shader_filename.c_str(), vertex_shader_filename.c_str()))
        {
            std::cout << "Compilaton of shader failed:" << std::endl;
            std::cout << m_shader->last_error << std::endl;
            _reset();
            return false;
        }
    }

    return true;
}

bool GLCanvas3D::Shader::is_initialized() const
{
    return (m_shader != nullptr);
}

bool GLCanvas3D::Shader::start_using() const
{
    if (is_initialized())
    {
        m_shader->enable();
        return true;
    }
    else
        return false;
}

void GLCanvas3D::Shader::stop_using() const
{
    if (m_shader != nullptr)
        m_shader->disable();
}

GLShader* GLCanvas3D::Shader::get_shader()
{
    return m_shader;
}

void GLCanvas3D::Shader::_reset()
{
    if (m_shader != nullptr)
    {
        m_shader->release();
        delete m_shader;
        m_shader = nullptr;
    }
}

GLCanvas3D::LayersEditing::GLTextureData::GLTextureData()
    : id(0)
    , width(0)
    , height(0)
{
}

GLCanvas3D::LayersEditing::GLTextureData::GLTextureData(unsigned int id, int width, int height)
    : id(id)
    , width(width)
    , height(height)
{
}

GLCanvas3D::LayersEditing::LayersEditing()
    : m_allowed(false)
    , m_enabled(false)
    , m_z_texture_id(0)
{
}

GLCanvas3D::LayersEditing::~LayersEditing()
{
    if (m_tooltip_texture.id != 0)
    {
        ::glDeleteTextures(1, &m_tooltip_texture.id);
        m_tooltip_texture = GLTextureData();
    }
    
    if (m_reset_texture.id != 0)
    {
        ::glDeleteTextures(1, &m_reset_texture.id);
        m_reset_texture = GLTextureData();
    }

    if (m_z_texture_id != 0)
    {
        ::glDeleteTextures(1, &m_z_texture_id);
        m_z_texture_id = 0;
    }
}

bool GLCanvas3D::LayersEditing::init(const std::string& vertex_shader_filename, const std::string& fragment_shader_filename)
{
    if (!m_shader.init(vertex_shader_filename, fragment_shader_filename))
        return false;

    ::glGenTextures(1, (GLuint*)&m_z_texture_id);
    ::glBindTexture(GL_TEXTURE_2D, m_z_texture_id);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
    ::glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

bool GLCanvas3D::LayersEditing::is_allowed() const
{
    return m_allowed;
}

void GLCanvas3D::LayersEditing::set_allowed(bool allowed)
{
    m_allowed = allowed;
}

bool GLCanvas3D::LayersEditing::is_enabled() const
{
    return m_enabled;
}

void GLCanvas3D::LayersEditing::set_enabled(bool enabled)
{
    m_enabled = m_allowed && m_shader.is_initialized() && enabled;
}

unsigned int GLCanvas3D::LayersEditing::get_z_texture_id() const
{
    return m_z_texture_id;
}

void GLCanvas3D::LayersEditing::render(const GLCanvas3D& canvas, const PrintObject& print_object) const
{
    const Rect& bar_rect = _get_bar_rect_viewport(canvas);
    const Rect& reset_rect = _get_reset_rect_viewport(canvas);
    _render_tooltip_texture(canvas, bar_rect, reset_rect);
    _render_reset_texture(canvas, reset_rect);
    _render_profile(print_object, bar_rect);
}

GLShader* GLCanvas3D::LayersEditing::get_shader()
{
    return m_shader.get_shader();
}

bool GLCanvas3D::LayersEditing::_is_initialized() const
{
    return m_shader.is_initialized();
}

GLCanvas3D::LayersEditing::GLTextureData GLCanvas3D::LayersEditing::_load_texture_from_file(const std::string& filename) const
{
    const std::string& path = resources_dir() + "/icons/";

    // Load a PNG with an alpha channel.
    wxImage image;
    if (!image.LoadFile(path + filename, wxBITMAP_TYPE_PNG))
        return GLTextureData();

    int width = image.GetWidth();
    int height = image.GetHeight();
    int n_pixels = width * height;

    if (n_pixels <= 0)
        return GLTextureData();

    // Get RGB & alpha raw data from wxImage, pack them into an array.
    unsigned char* img_rgb = image.GetData();
    if (img_rgb == nullptr)
        return GLTextureData();

    unsigned char* img_alpha = image.GetAlpha();

    std::vector<unsigned char> data(n_pixels * 4, 0);
    for (int i = 0; i < n_pixels; ++i)
    {
        int data_id = i * 4;
        int img_id = i * 3;
        data[data_id + 0] = img_rgb[img_id + 0];
        data[data_id + 1] = img_rgb[img_id + 1];
        data[data_id + 2] = img_rgb[img_id + 2];
        data[data_id + 3] = (img_alpha != nullptr) ? img_alpha[i] : 255;
    }

    // sends data to gpu
    GLuint tex_id;
    ::glGenTextures(1, &tex_id);
    ::glBindTexture(GL_TEXTURE_2D, tex_id);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
    ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)width, (GLsizei)height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data());
    ::glBindTexture(GL_TEXTURE_2D, 0);

    return GLTextureData((unsigned int)tex_id, width, height);
}

void GLCanvas3D::LayersEditing::_render_tooltip_texture(const GLCanvas3D& canvas, const Rect& bar_rect, const Rect& reset_rect) const
{
    if (m_tooltip_texture.id == 0)
    {
        m_tooltip_texture = _load_texture_from_file("variable_layer_height_tooltip.png");
        if (m_tooltip_texture.id == 0)
            return;
    }

    float zoom = canvas.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
    float gap = 10.0f * inv_zoom;

    float bar_left = bar_rect.get_left();
    float reset_bottom = reset_rect.get_bottom();

    float l = bar_left - (float)m_tooltip_texture.width * inv_zoom - gap;
    float r = bar_left - gap;
    float t = reset_bottom + (float)m_tooltip_texture.height * inv_zoom + gap;
    float b = reset_bottom + gap;

    canvas.render_texture(m_tooltip_texture.id, l, r, b, t);
}

void GLCanvas3D::LayersEditing::_render_reset_texture(const GLCanvas3D& canvas, const Rect& reset_rect) const
{
    if (m_reset_texture.id == 0)
    {
        m_reset_texture = _load_texture_from_file("variable_layer_height_reset.png");
        if (m_reset_texture.id == 0)
            return;
    }

    canvas.render_texture(m_reset_texture.id, reset_rect.get_left(), reset_rect.get_right(), reset_rect.get_bottom(), reset_rect.get_top());
}

void GLCanvas3D::LayersEditing::_render_profile(const PrintObject& print_object, const Rect& bar_rect) const
{
    // FIXME show some kind of legend.

    // Get a maximum layer height value.
    // FIXME This is a duplicate code of Slicing.cpp.
    double layer_height_max = DBL_MAX;
    const PrintConfig& print_config = print_object.print()->config;
    const std::vector<double>& nozzle_diameters = dynamic_cast<const ConfigOptionFloats*>(print_config.option("nozzle_diameter"))->values;
    const std::vector<double>& layer_heights_min = dynamic_cast<const ConfigOptionFloats*>(print_config.option("min_layer_height"))->values;
    const std::vector<double>& layer_heights_max = dynamic_cast<const ConfigOptionFloats*>(print_config.option("max_layer_height"))->values;
    for (unsigned int i = 0; i < (unsigned int)nozzle_diameters.size(); ++i)
    {
        double lh_min = (layer_heights_min[i] == 0.0) ? 0.07 : std::max(0.01, layer_heights_min[i]);
        double lh_max = (layer_heights_max[i] == 0.0) ? (0.75 * nozzle_diameters[i]) : layer_heights_max[i];
        layer_height_max = std::min(layer_height_max, std::max(lh_min, lh_max));
    }

    // Make the vertical bar a bit wider so the layer height curve does not touch the edge of the bar region.
    layer_height_max *= 1.12;

    coordf_t max_z = unscale(print_object.size.z);
    double layer_height = dynamic_cast<const ConfigOptionFloat*>(print_object.config.option("layer_height"))->value;
    float l = bar_rect.get_left();
    float w = bar_rect.get_right() - l;
    float b = bar_rect.get_bottom();
    float t = bar_rect.get_top();
    float h = t - b;
    float scale_x = w / (float)layer_height_max;
    float scale_y = h / (float)max_z;
    float x = l + (float)layer_height * scale_x;

    // Baseline
    ::glColor3f(0.0f, 0.0f, 0.0f);
    ::glBegin(GL_LINE_STRIP);
    ::glVertex2f(x, b);
    ::glVertex2f(x, t);
    ::glEnd();

    // Curve
    const ModelObject* model_object = print_object.model_object();
    if (model_object->layer_height_profile_valid)
    {
        const std::vector<coordf_t>& profile = model_object->layer_height_profile;

        ::glColor3f(0.0f, 0.0f, 1.0f);
        ::glBegin(GL_LINE_STRIP);
        for (unsigned int i = 0; i < profile.size(); i += 2)
        {
            ::glVertex2f(l + (float)profile[i + 1] * scale_x, b + (float)profile[i] * scale_y);
        }
        ::glEnd();
    }
}

Rect GLCanvas3D::LayersEditing::_get_bar_rect_screen(const GLCanvas3D& canvas) const
{
    const Size& cnv_size = canvas.get_canvas_size();
    float w = (float)cnv_size.get_width();
    float h = (float)cnv_size.get_height();

    return Rect(w - VARIABLE_LAYER_THICKNESS_BAR_WIDTH, 0.0f, w, h - VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT);
}

Rect GLCanvas3D::LayersEditing::_get_reset_rect_screen(const GLCanvas3D& canvas) const
{
    const Size& cnv_size = canvas.get_canvas_size();
    float w = (float)cnv_size.get_width();
    float h = (float)cnv_size.get_height();

    return Rect(w - VARIABLE_LAYER_THICKNESS_BAR_WIDTH, h - VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT, w, h);
}

Rect GLCanvas3D::LayersEditing::_get_bar_rect_viewport(const GLCanvas3D& canvas) const
{
    const Size& cnv_size = canvas.get_canvas_size();
    float half_w = 0.5f * (float)cnv_size.get_width();
    float half_h = 0.5f * (float)cnv_size.get_height();

    float zoom = canvas.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    return Rect((half_w - VARIABLE_LAYER_THICKNESS_BAR_WIDTH) * inv_zoom, half_h * inv_zoom, half_w * inv_zoom, (-half_h + VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT) * inv_zoom);
}

Rect GLCanvas3D::LayersEditing::_get_reset_rect_viewport(const GLCanvas3D& canvas) const
{
    const Size& cnv_size = canvas.get_canvas_size();
    float half_w = 0.5f * (float)cnv_size.get_width();
    float half_h = 0.5f * (float)cnv_size.get_height();

    float zoom = canvas.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    return Rect((half_w - VARIABLE_LAYER_THICKNESS_BAR_WIDTH) * inv_zoom, (-half_h + VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT) * inv_zoom, half_w * inv_zoom, -half_h * inv_zoom);
}

GLCanvas3D::Mouse::Mouse()
    : m_dragging(false)
{
}

bool GLCanvas3D::Mouse::is_dragging() const
{
    return m_dragging;
}

void GLCanvas3D::Mouse::set_dragging(bool dragging)
{
    m_dragging = dragging;
}

const Pointf& GLCanvas3D::Mouse::get_position() const
{
    return m_position;
}

void GLCanvas3D::Mouse::set_position(const Pointf& position)
{
    m_position = position;
}

GLCanvas3D::GLCanvas3D(wxGLCanvas* canvas, wxGLContext* context)
    : m_canvas(canvas)
    , m_context(context)
    , m_volumes(nullptr)
    , m_config(nullptr)
    , m_dirty(true)
    , m_apply_zoom_to_volumes_filter(false)
    , m_hover_volume_id(-1)
    , m_warning_texture_enabled(false)
    , m_legend_texture_enabled(false)
    , m_picking_enabled(false)
    , m_shader_enabled(false)
    , m_multisample_allowed(false)
{
}

GLCanvas3D::~GLCanvas3D()
{
    _deregister_callbacks();
}

bool GLCanvas3D::init(bool useVBOs, bool use_legacy_opengl)
{
    ::glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    ::glEnable(GL_DEPTH_TEST);
    ::glClearDepth(1.0f);
    ::glDepthFunc(GL_LEQUAL);
    ::glEnable(GL_CULL_FACE);
    ::glEnable(GL_BLEND);
    ::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Set antialiasing / multisampling
    ::glDisable(GL_LINE_SMOOTH);
    ::glDisable(GL_POLYGON_SMOOTH);

    // ambient lighting
    GLfloat ambient[4] = { 0.3f, 0.3f, 0.3f, 1.0f };
    ::glLightModelfv(GL_LIGHT_MODEL_AMBIENT, ambient);

    ::glEnable(GL_LIGHT0);
    ::glEnable(GL_LIGHT1);

    // light from camera
    GLfloat position[4] = { 1.0f, 0.0f, 1.0f, 0.0f };
    ::glLightfv(GL_LIGHT1, GL_POSITION, position);
    GLfloat specular[4] = { 0.3f, 0.3f, 0.3f, 1.0f };
    ::glLightfv(GL_LIGHT1, GL_SPECULAR, specular);
    GLfloat diffuse[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
    ::glLightfv(GL_LIGHT1, GL_DIFFUSE, diffuse);

    // Enables Smooth Color Shading; try GL_FLAT for (lack of) fun.
    ::glShadeModel(GL_SMOOTH);

    // A handy trick -- have surface material mirror the color.
    ::glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    ::glEnable(GL_COLOR_MATERIAL);

    if (is_multisample_allowed())
        ::glEnable(GL_MULTISAMPLE);

    if (useVBOs && !m_shader.init("gouraud.vs", "gouraud.fs"))
        return false;

    if (useVBOs && !m_layers_editing.init("variable_layer_height.vs", "variable_layer_height.fs"))
        return false;

    m_layers_editing.set_allowed(!use_legacy_opengl);

    return true;
}

bool GLCanvas3D::set_current()
{
    if ((m_canvas != nullptr) && (m_context != nullptr))
    {
        m_canvas->SetCurrent(*m_context);
        return true;
    }

    return false;
}

bool GLCanvas3D::is_dirty() const
{
    return m_dirty;
}

void GLCanvas3D::set_dirty(bool dirty)
{
    m_dirty = dirty;
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
            float depth = 5.0f * (float)bbox.max_size();
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

void GLCanvas3D::reset_volumes()
{
    if (set_current() && (m_volumes != nullptr))
    {
        m_volumes->release_geometry();
        m_volumes->clear();
        set_dirty(true);
    }
}

void GLCanvas3D::deselect_volumes()
{
    if (m_volumes != nullptr)
    {
        for (GLVolume* vol : m_volumes->volumes)
        {
            if (vol != nullptr)
                vol->selected = false;
        }
    }
}

void GLCanvas3D::select_volume(unsigned int id)
{
    if ((m_volumes != nullptr) && (id < (unsigned int)m_volumes->volumes.size()))
    {
        GLVolume* vol = m_volumes->volumes[id];
        if (vol != nullptr)
            vol->selected = true;
    }
}

DynamicPrintConfig* GLCanvas3D::get_config()
{
    return m_config;
}

void GLCanvas3D::set_config(DynamicPrintConfig* config)
{
    m_config = config;
}

void GLCanvas3D::set_bed_shape(const Pointfs& shape)
{
    m_bed.set_shape(shape);

    // Set the origin and size for painting of the coordinate system axes.
    set_axes_origin(Pointf3(0.0, 0.0, (coordf_t)GROUND_Z));
    set_axes_length(0.3f * (float)bed_bounding_box().max_size());
}

void GLCanvas3D::set_auto_bed_shape()
{
    // draw a default square bed around object center
    const BoundingBoxf3& bbox = volumes_bounding_box();
    coordf_t max_size = bbox.max_size();
    const Pointf3& center = bbox.center();

    Pointfs bed_shape;
    bed_shape.reserve(4);
    bed_shape.emplace_back(center.x - max_size, center.y - max_size);
    bed_shape.emplace_back(center.x + max_size, center.y - max_size);
    bed_shape.emplace_back(center.x + max_size, center.y + max_size);
    bed_shape.emplace_back(center.x - max_size, center.y + max_size);

    set_bed_shape(bed_shape);

    // Set the origin for painting of the coordinate system axes.
    set_axes_origin(Pointf3(center.x, center.y, (coordf_t)GROUND_Z));
}

const Pointf3& GLCanvas3D::get_axes_origin() const
{
    return m_axes.get_origin();
}

void GLCanvas3D::set_axes_origin(const Pointf3& origin)
{
    m_axes.set_origin(origin);
}

float GLCanvas3D::get_axes_length() const
{
    return m_axes.get_length();
}

void GLCanvas3D::set_axes_length(float length)
{
    return m_axes.set_length(length);
}

void GLCanvas3D::set_cutting_plane(float z, const ExPolygons& polygons)
{
    m_cutting_plane.set(z, polygons);
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

bool GLCanvas3D::is_layers_editing_enabled() const
{
    return m_layers_editing.is_enabled();
}

bool GLCanvas3D::is_picking_enabled() const
{
    return m_picking_enabled;
}

bool GLCanvas3D::is_multisample_allowed() const
{
    return m_multisample_allowed;
}

void GLCanvas3D::enable_layers_editing(bool enable)
{
    m_layers_editing.set_enabled(enable);
}

void GLCanvas3D::enable_warning_texture(bool enable)
{
    m_warning_texture_enabled = enable;
}

void GLCanvas3D::enable_legend_texture(bool enable)
{
    m_legend_texture_enabled = enable;
}

void GLCanvas3D::enable_picking(bool enable)
{
    m_picking_enabled = enable;
}

void GLCanvas3D::enable_shader(bool enable)
{
    m_shader_enabled = enable;
}
void GLCanvas3D::allow_multisample(bool allow)
{
    m_multisample_allowed = allow;
}

bool GLCanvas3D::is_mouse_dragging() const
{
    return m_mouse.is_dragging();
}

void GLCanvas3D::set_mouse_dragging(bool dragging)
{
    m_mouse.set_dragging(dragging);
}

const Pointf& GLCanvas3D::get_mouse_position() const
{
    return m_mouse.get_position();
}

void GLCanvas3D::set_mouse_position(const Pointf& position)
{
    m_mouse.set_position(position);
}

int GLCanvas3D::get_hover_volume_id() const
{
    return m_hover_volume_id;
}

void GLCanvas3D::set_hover_volume_id(int id)
{
    m_hover_volume_id = id;
}

void GLCanvas3D::zoom_to_bed()
{
    _zoom_to_bounding_box(bed_bounding_box());
}

void GLCanvas3D::zoom_to_volumes()
{
    m_apply_zoom_to_volumes_filter = true;
    _zoom_to_bounding_box(volumes_bounding_box());
    m_apply_zoom_to_volumes_filter = false;
}

void GLCanvas3D::select_view(const std::string& direction)
{
    const float* dir_vec = nullptr;

    if (direction == "iso")
        dir_vec = VIEW_DEFAULT;
    else if (direction == "left")
        dir_vec = VIEW_LEFT;
    else if (direction == "right")
        dir_vec = VIEW_RIGHT;
    else if (direction == "top")
        dir_vec = VIEW_TOP;
    else if (direction == "bottom")
        dir_vec = VIEW_BOTTOM;
    else if (direction == "front")
        dir_vec = VIEW_FRONT;
    else if (direction == "rear")
        dir_vec = VIEW_REAR;

    if ((dir_vec != nullptr) && !empty(volumes_bounding_box()))
    {
        m_camera.set_phi(dir_vec[0]);
        m_camera.set_theta(dir_vec[1]);

        m_on_viewport_changed_callback.call();
        
        if (m_canvas != nullptr)
            m_canvas->Refresh();
    }
}

bool GLCanvas3D::start_using_shader() const
{
    return m_shader.start_using();
}

void GLCanvas3D::stop_using_shader() const
{
    m_shader.stop_using();
}

void GLCanvas3D::picking_pass()
{
    if (is_picking_enabled() && !is_mouse_dragging() && (m_volumes != nullptr))
    {
        // Render the object for picking.
        // FIXME This cannot possibly work in a multi - sampled context as the color gets mangled by the anti - aliasing.
        // Better to use software ray - casting on a bounding - box hierarchy.

        if (is_multisample_allowed())
            ::glDisable(GL_MULTISAMPLE);

        ::glDisable(GL_LIGHTING);
        ::glDisable(GL_BLEND);

        ::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ::glPushAttrib(GL_ENABLE_BIT);

        render_volumes(true);
        
        ::glPopAttrib();

        if (is_multisample_allowed())
            ::glEnable(GL_MULTISAMPLE);

        const Size& cnv_size = get_canvas_size();

        const Pointf& pos = get_mouse_position();
        GLubyte color[4];
        ::glReadPixels(pos.x, cnv_size.get_height() - pos.y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, (void*)color);
        int volume_id = color[0] + color[1] * 256 + color[2] * 256 * 256;

        set_hover_volume_id(-1);

        for (GLVolume* vol : m_volumes->volumes)
        {
            vol->hover = false;
        }

        if (volume_id < m_volumes->volumes.size())
        {
            set_hover_volume_id(volume_id);
            m_volumes->volumes[volume_id]->hover = true;
            int group_id = m_volumes->volumes[volume_id]->select_group_id;
            if (group_id != -1)
            {
                for (GLVolume* vol : m_volumes->volumes)
                {
                    if (vol->select_group_id == group_id)
                        vol->hover = true;
                }
            }
        }
    }
}

void GLCanvas3D::render_background() const
{
    static const float COLOR[3] = { 10.0f / 255.0f, 98.0f / 255.0f, 144.0f / 255.0f };

    ::glDisable(GL_LIGHTING);

    ::glPushMatrix();
    ::glLoadIdentity();
    ::glMatrixMode(GL_PROJECTION);
    ::glPushMatrix();
    ::glLoadIdentity();

    // Draws a bluish bottom to top gradient over the complete screen.
    ::glDisable(GL_DEPTH_TEST);

    ::glBegin(GL_QUADS);
    ::glColor3f(0.0f, 0.0f, 0.0f);
    ::glVertex3f(-1.0f, -1.0f, 1.0f);
    ::glVertex3f(1.0f, -1.0f, 1.0f);
    ::glColor3f(COLOR[0], COLOR[1], COLOR[2]);
    ::glVertex3f(1.0f, 1.0f, 1.0f);
    ::glVertex3f(-1.0f, 1.0f, 1.0f);
    ::glEnd();

    ::glEnable(GL_DEPTH_TEST);

    ::glPopMatrix();
    ::glMatrixMode(GL_MODELVIEW);
    ::glPopMatrix();
}

unsigned int GLCanvas3D::get_layers_editing_z_texture_id() const
{
    return m_layers_editing.get_z_texture_id();
}

GLShader* GLCanvas3D::get_layers_editing_shader()
{
    return m_layers_editing.get_shader();
}

void GLCanvas3D::render_bed() const
{
    m_bed.render();
}

void GLCanvas3D::render_axes() const
{
    m_axes.render();
}

void GLCanvas3D::render_volumes(bool fake_colors) const
{
    static const float INV_255 = 1.0f / 255.0f;

    if (m_volumes == nullptr)
        return;

    if (fake_colors)
        ::glDisable(GL_LIGHTING);
    else
        ::glEnable(GL_LIGHTING);

    // do not cull backfaces to show broken geometry, if any
    ::glDisable(GL_CULL_FACE);

    ::glEnable(GL_BLEND);
    ::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ::glEnableClientState(GL_VERTEX_ARRAY);
    ::glEnableClientState(GL_NORMAL_ARRAY);

    unsigned int volume_id = 0;
    for (GLVolume* vol : m_volumes->volumes)
    {
        if (fake_colors)
        {
            // Object picking mode. Render the object with a color encoding the object index.
            unsigned int r = (volume_id & 0x000000FF) >> 0;
            unsigned int g = (volume_id & 0x0000FF00) >> 8;
            unsigned int b = (volume_id & 0x00FF0000) >> 16;
            ::glColor4f((float)r * INV_255, (float)g * INV_255, (float)b * INV_255, 1.0f);
        }
        else
        {
            vol->set_render_color();
            ::glColor4f(vol->render_color[0], vol->render_color[1], vol->render_color[2], vol->render_color[3]);
        }

        vol->render();
        ++volume_id;
    }

    ::glDisableClientState(GL_NORMAL_ARRAY);
    ::glDisableClientState(GL_VERTEX_ARRAY);
    ::glDisable(GL_BLEND);

    ::glEnable(GL_CULL_FACE);
}

void GLCanvas3D::render_objects(bool useVBOs)
{
    if ((m_volumes == nullptr) || m_volumes->empty())
        return;

    ::glEnable(GL_LIGHTING);

    if (!m_shader_enabled)
        render_volumes(false);
    else if (useVBOs)
    {
        if (is_picking_enabled())
        {
            m_on_mark_volumes_for_layer_height_callback.call();

            if (m_config != nullptr)
            {
                const BoundingBoxf3& bed_bb = bed_bounding_box();
                m_volumes->set_print_box((float)bed_bb.min.x, (float)bed_bb.min.y, 0.0f, (float)bed_bb.max.x, (float)bed_bb.max.y, (float)m_config->opt_float("max_print_height"));
                m_volumes->check_outside_state(m_config);
            }
            // do not cull backfaces to show broken geometry, if any
            ::glDisable(GL_CULL_FACE);
        }

        start_using_shader();
        m_volumes->render_VBOs();
        stop_using_shader();

        if (is_picking_enabled())
            ::glEnable(GL_CULL_FACE);
    }
    else
    {
        // do not cull backfaces to show broken geometry, if any
        if (is_picking_enabled())
            ::glDisable(GL_CULL_FACE);

        m_volumes->render_legacy();

        if (is_picking_enabled())
            ::glEnable(GL_CULL_FACE);
    }
}

void GLCanvas3D::render_cutting_plane() const
{
    m_cutting_plane.render(volumes_bounding_box());
}

void GLCanvas3D::render_warning_texture() const
{
    if (!m_warning_texture_enabled)
        return;

    // If the warning texture has not been loaded into the GPU, do it now.
    unsigned int tex_id = _3DScene::finalize_warning_texture();
    if (tex_id > 0)
    {
        unsigned int w = _3DScene::get_warning_texture_width();
        unsigned int h = _3DScene::get_warning_texture_height();
        if ((w > 0) && (h > 0))
        {
            ::glDisable(GL_DEPTH_TEST);
            ::glPushMatrix();
            ::glLoadIdentity();

            const Size& cnv_size = get_canvas_size();
            float zoom = get_camera_zoom();
            float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
            float l = (-0.5f * (float)w) * inv_zoom;
            float t = (-0.5f * (float)cnv_size.get_height() + (float)h) * inv_zoom;
            float r = l + (float)w * inv_zoom;
            float b = t - (float)h * inv_zoom;

            render_texture(tex_id, l, r, b, t);

            ::glPopMatrix();
            ::glEnable(GL_DEPTH_TEST);
        }
    }
}

void GLCanvas3D::render_legend_texture() const
{
    if (!m_legend_texture_enabled)
        return;

    // If the legend texture has not been loaded into the GPU, do it now.
    unsigned int tex_id = _3DScene::finalize_legend_texture();
    if (tex_id > 0)
    {
        unsigned int w = _3DScene::get_legend_texture_width();
        unsigned int h = _3DScene::get_legend_texture_height();
        if ((w > 0) && (h > 0))
        {
            ::glDisable(GL_DEPTH_TEST);
            ::glPushMatrix();
            ::glLoadIdentity();
            
            const Size& cnv_size = get_canvas_size();
            float zoom = get_camera_zoom();
            float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
            float l = (-0.5f * (float)cnv_size.get_width()) * inv_zoom;
            float t = (0.5f * (float)cnv_size.get_height()) * inv_zoom;
            float r = l + (float)w * inv_zoom;
            float b = t - (float)h * inv_zoom;
            render_texture(tex_id, l, r, b, t);

            ::glPopMatrix();
            ::glEnable(GL_DEPTH_TEST);
        }
    }
}

void GLCanvas3D::render_layer_editing_textures(const PrintObject& print_object) const
{
    m_layers_editing.render(*this, print_object);
}

void GLCanvas3D::render_texture(unsigned int tex_id, float left, float right, float bottom, float top) const
{
    ::glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    ::glDisable(GL_LIGHTING);
    ::glEnable(GL_BLEND);
    ::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    ::glEnable(GL_TEXTURE_2D);

    ::glBindTexture(GL_TEXTURE_2D, (GLuint)tex_id);

    ::glBegin(GL_QUADS);
    ::glTexCoord2d(0.0f, 1.0f); glVertex3f(left, bottom, 0.0f);
    ::glTexCoord2d(1.0f, 1.0f); glVertex3f(right, bottom, 0.0f);
    ::glTexCoord2d(1.0f, 0.0f); glVertex3f(right, top, 0.0f);
    ::glTexCoord2d(0.0f, 0.0f); glVertex3f(left, top, 0.0f);
    ::glEnd();

    ::glBindTexture(GL_TEXTURE_2D, 0);

    ::glDisable(GL_TEXTURE_2D);
    ::glDisable(GL_BLEND);
    ::glEnable(GL_LIGHTING);
}

void GLCanvas3D::register_on_viewport_changed_callback(void* callback)
{
    if (callback != nullptr)
        m_on_viewport_changed_callback.register_callback(callback);
}

void GLCanvas3D::register_on_mark_volumes_for_layer_height_callback(void* callback)
{
    if (callback != nullptr)
        m_on_mark_volumes_for_layer_height_callback.register_callback(callback);
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
        const Size& cnv_size = get_canvas_size();
        resize((unsigned int)cnv_size.get_width(), (unsigned int)cnv_size.get_height());
        m_canvas->Refresh();
    }
}

void GLCanvas3D::on_char(wxKeyEvent& evt)
{
    if (evt.HasModifiers())
        evt.Skip();
    else
    {
        int keyCode = evt.GetKeyCode();
        switch (keyCode - 48)
        {
        // numerical input
        case 0: { select_view("iso"); break; }
        case 1: { select_view("top"); break; }
        case 2: { select_view("bottom"); break; }
        case 3: { select_view("front"); break; }
        case 4: { select_view("rear"); break; }
        case 5: { select_view("left"); break; }
        case 6: { select_view("right"); break; }
        default:
            {
                // text input
                switch (keyCode)
                {
                // key B/b
                case 66:
                case 98:  { zoom_to_bed(); break; }
                // key Z/z
                case 90:
                case 122: { zoom_to_volumes(); break; }
                default: { evt.Skip(); break; }
                }
            }
        }
    }
}

Size GLCanvas3D::get_canvas_size() const
{
    int w = 0;
    int h = 0;

    if (m_canvas != nullptr)
        m_canvas->GetSize(&w, &h);

    return Size(w, h);
}

void GLCanvas3D::_zoom_to_bounding_box(const BoundingBoxf3& bbox)
{
    // Calculate the zoom factor needed to adjust viewport to bounding box.
    float zoom = _get_zoom_to_bounding_box_factor(bbox);
    if (zoom > 0.0f)
    {
        set_camera_zoom(zoom);
        // center view around bounding box center
        set_camera_target(bbox.center());

        m_on_viewport_changed_callback.call();

        if (is_shown_on_screen())
        {
            const Size& cnv_size = get_canvas_size();
            resize((unsigned int)cnv_size.get_width(), (unsigned int)cnv_size.get_height());
            if (m_canvas != nullptr)
                m_canvas->Refresh();
        }
    }
}

float GLCanvas3D::_get_zoom_to_bounding_box_factor(const BoundingBoxf3& bbox) const
{
    float max_bb_size = bbox.max_size();
    if (max_bb_size == 0.0f)
        return -1.0f;

    // project the bbox vertices on a plane perpendicular to the camera forward axis
    // then calculates the vertices coordinate on this plane along the camera xy axes

    // we need the view matrix, we let opengl calculate it(same as done in render sub)
    ::glMatrixMode(GL_MODELVIEW);
    ::glLoadIdentity();

    if (TURNTABLE_MODE)
    {
        // Turntable mode is enabled by default.
        ::glRotatef(-get_camera_theta(), 1.0f, 0.0f, 0.0f); // pitch
        ::glRotatef(get_camera_phi(), 0.0f, 0.0f, 1.0f);    // yaw
    }
    else
    {
        // Shift the perspective camera.
        Pointf3 camera_pos(0.0, 0.0, -(coordf_t)get_camera_distance());
        ::glTranslatef((float)camera_pos.x, (float)camera_pos.y, (float)camera_pos.z);
//        my @rotmat = quat_to_rotmatrix($self->quat); <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< TEMPORARY COMMENTED OUT
//        glMultMatrixd_p(@rotmat[0..15]);             <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<< TEMPORARY COMMENTED OUT
    }

    const Pointf3& target = get_camera_target();
    ::glTranslatef(-(float)target.x, -(float)target.y, -(float)target.z);

    // get the view matrix back from opengl
    GLfloat matrix[16];
    ::glGetFloatv(GL_MODELVIEW_MATRIX, matrix);

    // camera axes
    Pointf3 right((coordf_t)matrix[0], (coordf_t)matrix[4], (coordf_t)matrix[8]);
    Pointf3 up((coordf_t)matrix[1], (coordf_t)matrix[5], (coordf_t)matrix[9]);
    Pointf3 forward((coordf_t)matrix[2], (coordf_t)matrix[6], (coordf_t)matrix[10]);

    Pointf3 bb_min = bbox.min;
    Pointf3 bb_max = bbox.max;
    Pointf3 bb_center = bbox.center();

    // bbox vertices in world space
    std::vector<Pointf3> vertices;
    vertices.reserve(8);
    vertices.push_back(bb_min);
    vertices.emplace_back(bb_max.x, bb_min.y, bb_min.z);
    vertices.emplace_back(bb_max.x, bb_max.y, bb_min.z);
    vertices.emplace_back(bb_min.x, bb_max.y, bb_min.z);
    vertices.emplace_back(bb_min.x, bb_min.y, bb_max.z);
    vertices.emplace_back(bb_max.x, bb_min.y, bb_max.z);
    vertices.push_back(bb_max);
    vertices.emplace_back(bb_min.x, bb_max.y, bb_max.z);

    coordf_t max_x = 0.0;
    coordf_t max_y = 0.0;

    // margin factor to give some empty space around the bbox
    coordf_t margin_factor = 1.25;

    for (const Pointf3 v : vertices)
    {
        // project vertex on the plane perpendicular to camera forward axis
        Pointf3 pos(v.x - bb_center.x, v.y - bb_center.y, v.z - bb_center.z);
        Pointf3 proj_on_plane = pos - dot(pos, forward) * forward;

        // calculates vertex coordinate along camera xy axes
        coordf_t x_on_plane = dot(proj_on_plane, right);
        coordf_t y_on_plane = dot(proj_on_plane, up);

        max_x = std::max(max_x, margin_factor * std::abs(x_on_plane));
        max_y = std::max(max_y, margin_factor * std::abs(y_on_plane));
    }

    if ((max_x == 0.0) || (max_y == 0.0))
        return -1.0f;

    max_x *= 2.0;
    max_y *= 2.0;

    const Size& cnv_size = get_canvas_size();
    return (float)std::min((coordf_t)cnv_size.get_width() / max_x, (coordf_t)cnv_size.get_height() / max_y);
}

void GLCanvas3D::_deregister_callbacks()
{
    m_on_viewport_changed_callback.deregister_callback();
    m_on_mark_volumes_for_layer_height_callback.deregister_callback();
}

} // namespace GUI
} // namespace Slic3r
