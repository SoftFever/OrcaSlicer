#include "GLCanvas3D.hpp"

#include "../../slic3r/GUI/3DScene.hpp"
#include "../../slic3r/GUI/GLShader.hpp"
#include "../../libslic3r/ClipperUtils.hpp"
#include "../../libslic3r/PrintConfig.hpp"
#include "../../libslic3r/Print.hpp"
#include "../../libslic3r/GCode/PreviewData.hpp"

#include <GL/glew.h>

#include <wx/glcanvas.h>
#include <wx/image.h>
#include <wx/timer.h>

#include <iostream>
#include <float.h>

static const float TRACKBALLSIZE = 0.8f;
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
    : type(Ortho)
    , zoom(1.0f)
    , phi(45.0f)
//    , distance(0.0f)
    , target(0.0, 0.0, 0.0)
    , m_theta(45.0f)
{
}

std::string GLCanvas3D::Camera::get_type_as_string() const
{
    switch (type)
    {
    default:
    case Unknown:
        return "unknown";
//    case Perspective:
//        return "perspective";
    case Ortho:
        return "ortho";
    };
}

float GLCanvas3D::Camera::get_theta() const
{
    return m_theta;
}

void GLCanvas3D::Camera::set_theta(float theta)
{
    m_theta = clamp(0.0f, GIMBALL_LOCK_THETA_MAX, theta);
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

bool GLCanvas3D::Bed::contains(const Point& point) const
{
    return m_polygon.contains(point);
}

Point GLCanvas3D::Bed::point_projection(const Point& point) const
{
    return m_polygon.point_projection(point);
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
    : length(0.0f)
{
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
    ::glVertex3f((GLfloat)origin.x, (GLfloat)origin.y, (GLfloat)origin.z);
    ::glVertex3f((GLfloat)origin.x + length, (GLfloat)origin.y, (GLfloat)origin.z);
    // draw line for y axis
    ::glColor3f(0.0f, 1.0f, 0.0f);
    ::glVertex3f((GLfloat)origin.x, (GLfloat)origin.y, (GLfloat)origin.z);
    ::glVertex3f((GLfloat)origin.x, (GLfloat)origin.y + length, (GLfloat)origin.z);
    ::glEnd();
    // draw line for Z axis
    // (re-enable depth test so that axis is correctly shown when objects are behind it)
    ::glEnable(GL_DEPTH_TEST);
    ::glBegin(GL_LINES);
    ::glColor3f(0.0f, 0.0f, 1.0f);
    ::glVertex3f((GLfloat)origin.x, (GLfloat)origin.y, (GLfloat)origin.z);
    ::glVertex3f((GLfloat)origin.x, (GLfloat)origin.y, (GLfloat)origin.z + length);
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

void GLCanvas3D::Shader::set_uniform(const std::string& name, float value) const
{
    if (m_shader != nullptr)
        m_shader->set_uniform(name.c_str(), value);
}

const GLShader* GLCanvas3D::Shader::get_shader() const
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
    : m_use_legacy_opengl(false)
    , m_enabled(false)
    , m_z_texture_id(0)
    , state(Unknown)
    , band_width(2.0f)
    , strength(0.005f)
    , last_object_id(-1)
    , last_z(0.0f)
    , last_action(0)
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
    return !m_use_legacy_opengl && m_shader.is_initialized();
}

void GLCanvas3D::LayersEditing::set_use_legacy_opengl(bool use_legacy_opengl)
{
    m_use_legacy_opengl = use_legacy_opengl;
}

bool GLCanvas3D::LayersEditing::is_enabled() const
{
    return m_enabled;
}

void GLCanvas3D::LayersEditing::set_enabled(bool enabled)
{
    m_enabled = is_allowed() && enabled;
}

unsigned int GLCanvas3D::LayersEditing::get_z_texture_id() const
{
    return m_z_texture_id;
}

void GLCanvas3D::LayersEditing::render(const GLCanvas3D& canvas, const PrintObject& print_object, const GLVolume& volume) const
{
    if (!m_enabled)
        return;

    const Rect& bar_rect = get_bar_rect_viewport(canvas);
    const Rect& reset_rect = get_reset_rect_viewport(canvas);

    ::glDisable(GL_DEPTH_TEST);

    // The viewport and camera are set to complete view and glOrtho(-$x / 2, $x / 2, -$y / 2, $y / 2, -$depth, $depth),
    // where x, y is the window size divided by $self->_zoom.
    ::glPushMatrix();
    ::glLoadIdentity();

    _render_tooltip_texture(canvas, bar_rect, reset_rect);
    _render_reset_texture(canvas, reset_rect);
    _render_active_object_annotations(canvas, volume, print_object, bar_rect);
    _render_profile(print_object, bar_rect);

    // Revert the matrices.
    glPopMatrix();

    glEnable(GL_DEPTH_TEST);
}

int GLCanvas3D::LayersEditing::get_shader_program_id() const
{
    const GLShader* shader = m_shader.get_shader();
    return (shader != nullptr) ? shader->shader_program_id : -1;
}

float GLCanvas3D::LayersEditing::get_cursor_z_relative(const GLCanvas3D& canvas)
{
    const Point& mouse_pos = canvas.get_local_mouse_position();
    const Rect& rect = get_bar_rect_screen(canvas);
    float x = (float)mouse_pos.x;
    float y = (float)mouse_pos.y;
    float t = rect.get_top();
    float b = rect.get_bottom();

    return ((rect.get_left() <= x) && (x <= rect.get_right()) && (t <= y) && (y <= b)) ?
        // Inside the bar.
        (b - y - 1.0f) / (b - t - 1.0f) :
        // Outside the bar.
        -1000.0f;
}

int GLCanvas3D::LayersEditing::get_first_selected_object_id(const GLVolumeCollection& volumes, unsigned int objects_count)
{
    for (const GLVolume* vol : volumes.volumes)
    {
        if ((vol != nullptr) && vol->selected)
        {
            int object_id = vol->select_group_id / 1000000;
            // Objects with object_id >= 1000 have a specific meaning, for example the wipe tower proxy.
            if (object_id < 10000)
                return (object_id >= (int)objects_count) ? -1 : object_id;
        }
    }
    return -1;
}

bool GLCanvas3D::LayersEditing::bar_rect_contains(const GLCanvas3D& canvas, float x, float y)
{
    const Rect& rect = get_bar_rect_screen(canvas);
    return (rect.get_left() <= x) && (x <= rect.get_right()) && (rect.get_top() <= y) && (y <= rect.get_bottom());
}

bool GLCanvas3D::LayersEditing::reset_rect_contains(const GLCanvas3D& canvas, float x, float y)
{
    const Rect& rect = get_reset_rect_screen(canvas);
    return (rect.get_left() <= x) && (x <= rect.get_right()) && (rect.get_top() <= y) && (y <= rect.get_bottom());
}

Rect GLCanvas3D::LayersEditing::get_bar_rect_screen(const GLCanvas3D& canvas)
{
    const Size& cnv_size = canvas.get_canvas_size();
    float w = (float)cnv_size.get_width();
    float h = (float)cnv_size.get_height();

    return Rect(w - VARIABLE_LAYER_THICKNESS_BAR_WIDTH, 0.0f, w, h - VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT);
}

Rect GLCanvas3D::LayersEditing::get_reset_rect_screen(const GLCanvas3D& canvas)
{
    const Size& cnv_size = canvas.get_canvas_size();
    float w = (float)cnv_size.get_width();
    float h = (float)cnv_size.get_height();

    return Rect(w - VARIABLE_LAYER_THICKNESS_BAR_WIDTH, h - VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT, w, h);
}

Rect GLCanvas3D::LayersEditing::get_bar_rect_viewport(const GLCanvas3D& canvas)
{
    const Size& cnv_size = canvas.get_canvas_size();
    float half_w = 0.5f * (float)cnv_size.get_width();
    float half_h = 0.5f * (float)cnv_size.get_height();

    float zoom = canvas.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    return Rect((half_w - VARIABLE_LAYER_THICKNESS_BAR_WIDTH) * inv_zoom, half_h * inv_zoom, half_w * inv_zoom, (-half_h + VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT) * inv_zoom);
}

Rect GLCanvas3D::LayersEditing::get_reset_rect_viewport(const GLCanvas3D& canvas)
{
    const Size& cnv_size = canvas.get_canvas_size();
    float half_w = 0.5f * (float)cnv_size.get_width();
    float half_h = 0.5f * (float)cnv_size.get_height();

    float zoom = canvas.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    return Rect((half_w - VARIABLE_LAYER_THICKNESS_BAR_WIDTH) * inv_zoom, (-half_h + VARIABLE_LAYER_THICKNESS_RESET_BUTTON_HEIGHT) * inv_zoom, half_w * inv_zoom, -half_h * inv_zoom);
}


bool GLCanvas3D::LayersEditing::_is_initialized() const
{
    return m_shader.is_initialized();
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

void GLCanvas3D::LayersEditing::_render_active_object_annotations(const GLCanvas3D& canvas, const GLVolume& volume, const PrintObject& print_object, const Rect& bar_rect) const
{
    float max_z = print_object.model_object()->bounding_box().max.z;

    m_shader.start_using();

    m_shader.set_uniform("z_to_texture_row", (float)volume.layer_height_texture_z_to_row_id());
    m_shader.set_uniform("z_texture_row_to_normalized", 1.0f / (float)volume.layer_height_texture_height());
    m_shader.set_uniform("z_cursor", max_z * get_cursor_z_relative(canvas));
    m_shader.set_uniform("z_cursor_band_width", band_width);

    GLsizei w = (GLsizei)volume.layer_height_texture_width();
    GLsizei h = (GLsizei)volume.layer_height_texture_height();
    GLsizei half_w = w / 2;
    GLsizei half_h = h / 2;

    ::glBindTexture(GL_TEXTURE_2D, m_z_texture_id);
    ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    ::glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA8, half_w, half_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    ::glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, volume.layer_height_texture_data_ptr_level0());
    ::glTexSubImage2D(GL_TEXTURE_2D, 1, 0, 0, half_w, half_h, GL_RGBA, GL_UNSIGNED_BYTE, volume.layer_height_texture_data_ptr_level1());

    // Render the color bar
    float l = bar_rect.get_left();
    float r = bar_rect.get_right();
    float t = bar_rect.get_top();
    float b = bar_rect.get_bottom();

    ::glBegin(GL_QUADS);
    ::glVertex3f(l, b, 0.0f);
    ::glVertex3f(r, b, 0.0f);
    ::glVertex3f(r, t, max_z);
    ::glVertex3f(l, t, max_z);
    ::glEnd();
    ::glBindTexture(GL_TEXTURE_2D, 0);

    m_shader.stop_using();
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

GLCanvas3D::LayersEditing::GLTextureData GLCanvas3D::LayersEditing::_load_texture_from_file(const std::string& filename)
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

const Point GLCanvas3D::Mouse::Drag::Invalid_2D_Point(INT_MAX, INT_MAX);
const Pointf3 GLCanvas3D::Mouse::Drag::Invalid_3D_Point(DBL_MAX, DBL_MAX, DBL_MAX);

GLCanvas3D::Mouse::Drag::Drag()
    : start_position_2D(Invalid_2D_Point)
    , start_position_3D(Invalid_3D_Point)
    , volume_idx(-1)
{
}

GLCanvas3D::Mouse::Mouse()
    : dragging(false)
    , position(DBL_MAX, DBL_MAX)
{
}

void GLCanvas3D::Mouse::set_start_position_2D_as_invalid()
{
    drag.start_position_2D = Drag::Invalid_2D_Point;
}

void GLCanvas3D::Mouse::set_start_position_3D_as_invalid()
{
    drag.start_position_3D = Drag::Invalid_3D_Point;
}

bool GLCanvas3D::Mouse::is_start_position_2D_defined() const
{
    return (drag.start_position_2D != Drag::Invalid_2D_Point);
}

bool GLCanvas3D::Mouse::is_start_position_3D_defined() const
{
    return (drag.start_position_3D != Drag::Invalid_3D_Point);
}

GLCanvas3D::GLCanvas3D(wxGLCanvas* canvas, wxGLContext* context)
    : m_canvas(canvas)
    , m_context(context)
    , m_timer(nullptr)
    , m_volumes(nullptr)
    , m_config(nullptr)
    , m_print(nullptr)
    , m_dirty(true)
    , m_initialized(false)
    , m_use_VBOs(false)
    , m_force_zoom_to_bed_enabled(false)
    , m_apply_zoom_to_volumes_filter(false)
    , m_hover_volume_id(-1)
    , m_warning_texture_enabled(false)
    , m_legend_texture_enabled(false)
    , m_picking_enabled(false)
    , m_moving_enabled(false)
    , m_shader_enabled(false)
    , m_multisample_allowed(false)
{
    if (m_canvas != nullptr)
        m_timer = new wxTimer(m_canvas);
}

GLCanvas3D::~GLCanvas3D()
{
    if (m_timer != nullptr)
    {
        delete m_timer;
        m_timer = nullptr;
    }

    _deregister_callbacks();
}

bool GLCanvas3D::init(bool useVBOs, bool use_legacy_opengl)
{
    if (m_initialized)
        return true;

    std::cout << "init: " << (void*)m_canvas << " (" << (void*)this << ")" << std::endl;

    ::glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    ::glClearDepth(1.0f);

    ::glDepthFunc(GL_LESS);

    ::glEnable(GL_DEPTH_TEST);
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
    GLfloat specular[4] = { 0.3f, 0.3f, 0.3f, 1.0f };
    ::glLightfv(GL_LIGHT1, GL_SPECULAR, specular);
    GLfloat diffuse[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
    ::glLightfv(GL_LIGHT1, GL_DIFFUSE, diffuse);

    // light from above
    GLfloat specular1[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
    ::glLightfv(GL_LIGHT0, GL_SPECULAR, specular1);
    GLfloat diffuse1[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
    ::glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse1);

    // Enables Smooth Color Shading; try GL_FLAT for (lack of) fun.
    ::glShadeModel(GL_SMOOTH);

    // A handy trick -- have surface material mirror the color.
    ::glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    ::glEnable(GL_COLOR_MATERIAL);

    if (m_multisample_allowed)
        ::glEnable(GL_MULTISAMPLE);

    if (useVBOs && !m_shader.init("gouraud.vs", "gouraud.fs"))
        return false;

    if (useVBOs && !m_layers_editing.init("variable_layer_height.vs", "variable_layer_height.fs"))
        return false;

    m_use_VBOs = useVBOs;
    m_layers_editing.set_use_legacy_opengl(use_legacy_opengl);

    // on linux the gl context is not valid until the canvas is not shown on screen
    // we defer the geometry finalization of volumes until the first call to render()
    if ((m_volumes != nullptr) && !m_volumes->empty())
        m_volumes->finalize_geometry(m_use_VBOs);

    m_initialized = true;

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

bool GLCanvas3D::is_shown_on_screen() const
{
    return (m_canvas != nullptr) ? m_canvas->IsShownOnScreen() : false;
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
        m_dirty = true;
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

void GLCanvas3D::set_config(DynamicPrintConfig* config)
{
    m_config = config;
}

void GLCanvas3D::set_print(Print* print)
{
    m_print = print;
}

void GLCanvas3D::set_bed_shape(const Pointfs& shape)
{
    m_bed.set_shape(shape);

    // Set the origin and size for painting of the coordinate system axes.
    m_axes.origin = Pointf3(0.0, 0.0, (coordf_t)GROUND_Z);
    set_axes_length(0.3f * (float)m_bed.get_bounding_box().max_size());
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
    m_axes.origin = Pointf3(center.x, center.y, (coordf_t)GROUND_Z);
}

void GLCanvas3D::set_axes_length(float length)
{
    m_axes.length = length;
}

void GLCanvas3D::set_cutting_plane(float z, const ExPolygons& polygons)
{
    m_cutting_plane.set(z, polygons);
}

float GLCanvas3D::get_camera_zoom() const
{
    return m_camera.zoom;
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

bool GLCanvas3D::is_layers_editing_enabled() const
{
    return m_layers_editing.is_enabled();
}

bool GLCanvas3D::is_layers_editing_allowed() const
{
    return m_layers_editing.is_allowed();
}

bool GLCanvas3D::is_shader_enabled() const
{
    return m_shader_enabled;
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

void GLCanvas3D::enable_moving(bool enable)
{
    m_moving_enabled = enable;
}

void GLCanvas3D::enable_shader(bool enable)
{
    m_shader_enabled = enable;
}

void GLCanvas3D::enable_force_zoom_to_bed(bool enable)
{
    m_force_zoom_to_bed_enabled = enable;
}

void GLCanvas3D::allow_multisample(bool allow)
{
    m_multisample_allowed = allow;
}

void GLCanvas3D::zoom_to_bed()
{
    _zoom_to_bounding_box(m_bed.get_bounding_box());
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
        m_camera.phi = dir_vec[0];
        m_camera.set_theta(dir_vec[1]);

        m_on_viewport_changed_callback.call();
        
        if (m_canvas != nullptr)
            m_canvas->Refresh();
    }
}

void GLCanvas3D::set_viewport_from_scene(const GLCanvas3D& other)
{
    m_camera.phi = other.m_camera.phi;
    m_camera.set_theta(other.m_camera.get_theta());
    m_camera.target = other.m_camera.target;
    m_camera.zoom = other.m_camera.zoom;
    m_dirty = true;
}

void GLCanvas3D::update_volumes_colors_by_extruder()
{
    if ((m_volumes == nullptr) || (m_config == nullptr))
        return;

    m_volumes->update_colors_by_extruder(m_config);
}

void GLCanvas3D::render()
{
    if (m_canvas == nullptr)
        return;

    if (!is_shown_on_screen())
        return;

    // ensures that the proper context is selected and that this canvas is initialized
    if (!set_current() || !_3DScene::init(m_canvas))
        return;

    if (m_force_zoom_to_bed_enabled)
        _force_zoom_to_bed();

    _camera_tranform();

    GLfloat position[4] = { 1.0f, 0.0f, 1.0f, 0.0f };
    ::glLightfv(GL_LIGHT1, GL_POSITION, position);
    GLfloat position1[4] = { -0.5f, -0.5f, 1.0f, 0.0f };
    ::glLightfv(GL_LIGHT0, GL_POSITION, position1);

    _picking_pass();
    _render_background();
    _render_bed();
    _render_axes();
    _render_objects();
    _render_cutting_plane();
    _render_warning_texture();
    _render_legend_texture();
    _render_layer_editing_overlay();

    m_canvas->SwapBuffers();
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

std::vector<double> GLCanvas3D::get_current_print_zs(bool active_only) const
{
    return (m_volumes != nullptr) ? m_volumes->get_current_print_zs(active_only) : std::vector<double>();
}

void GLCanvas3D::set_toolpaths_range(double low, double high)
{
    if (m_volumes != nullptr)
        m_volumes->set_range(low, high);
}

void GLCanvas3D::load_gcode_preview(const GCodePreviewData& preview_data, const std::vector<std::string>& str_tool_colors)
{
    if ((m_canvas != nullptr) && (m_volumes != nullptr) && (m_print != nullptr))
    {
        // ensures that the proper context is selected
        if (!set_current())
            return;

        if (m_volumes->empty())
        {
            std::vector<float> tool_colors = _parse_colors(str_tool_colors);
            
            m_gcode_preview_volume_index.reset();
            
            _load_gcode_extrusion_paths(preview_data, tool_colors);
            _load_gcode_travel_paths(preview_data, tool_colors);
            _load_gcode_retractions(preview_data);
            _load_gcode_unretractions(preview_data);
            
            if (m_volumes->empty())
                _3DScene::reset_legend_texture();
            else
            {
                _3DScene::generate_legend_texture(preview_data, tool_colors);
                
                // removes empty volumes
                m_volumes->volumes.erase(std::remove_if(m_volumes->volumes.begin(), m_volumes->volumes.end(),
                    [](const GLVolume* volume) { return volume->print_zs.empty(); }),
                    m_volumes->volumes.end());
                
                _load_shells();
            }
        }
        
        _update_gcode_volumes_visibility(preview_data);
    }
}

void GLCanvas3D::register_on_viewport_changed_callback(void* callback)
{
    if (callback != nullptr)
        m_on_viewport_changed_callback.register_callback(callback);
}

void GLCanvas3D::register_on_double_click_callback(void* callback)
{
    if (callback != nullptr)
        m_on_double_click_callback.register_callback(callback);
}

void GLCanvas3D::register_on_right_click_callback(void* callback)
{
    if (callback != nullptr)
        m_on_right_click_callback.register_callback(callback);
}

void GLCanvas3D::register_on_select_callback(void* callback)
{
    if (callback != nullptr)
        m_on_select_callback.register_callback(callback);
}

void GLCanvas3D::register_on_model_update_callback(void* callback)
{
    if (callback != nullptr)
        m_on_model_update_callback.register_callback(callback);
}

void GLCanvas3D::register_on_move_callback(void* callback)
{
    if (callback != nullptr)
        m_on_move_callback.register_callback(callback);
}

void GLCanvas3D::on_size(wxSizeEvent& evt)
{
    m_dirty = true;
}

void GLCanvas3D::on_idle(wxIdleEvent& evt)
{
    if (!m_dirty)
        return;

    _refresh_if_shown_on_screen();
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
                case 98: { zoom_to_bed(); break; }
                // key Z/z
                case 90:
                case 122: { zoom_to_volumes(); break; }
                default: { evt.Skip(); break; }
                }
            }
        }
    }
}

void GLCanvas3D::on_mouse_wheel(wxMouseEvent& evt)
{
    // Ignore the wheel events if the middle button is pressed.
    if (evt.MiddleIsDown())
        return;

    // Performs layers editing updates, if enabled
    if (is_layers_editing_enabled() && (m_print != nullptr))
    {
        int object_idx_selected = _get_layers_editing_first_selected_object_id((unsigned int)m_print->objects.size());
        if (object_idx_selected != -1)
        {
            // A volume is selected. Test, whether hovering over a layer thickness bar.
            if (_bar_rect_contains((float)evt.GetX(), (float)evt.GetY()))
            {
                // Adjust the width of the selection.
                m_layers_editing.band_width = std::max(std::min(m_layers_editing.band_width * (1.0f + 0.1f * (float)evt.GetWheelRotation() / (float)evt.GetWheelDelta()), 10.0f), 1.5f);
                if (m_canvas != nullptr)
                    m_canvas->Refresh();
                
                return;
            }
        }
    }

    // Calculate the zoom delta and apply it to the current zoom factor
    float zoom = (float)evt.GetWheelRotation() / (float)evt.GetWheelDelta();
    zoom = std::max(std::min(zoom, 4.0f), -4.0f) / 10.0f;
    zoom = get_camera_zoom() / (1.0f - zoom);
    
    // Don't allow to zoom too far outside the scene.
    float zoom_min = _get_zoom_to_bounding_box_factor(_max_bounding_box());
    if (zoom_min > 0.0f)
    {
        zoom_min *= 0.4f;
        if (zoom < zoom_min)
            zoom = zoom_min;
    }
    
    m_camera.zoom = zoom;
    m_on_viewport_changed_callback.call();

    _refresh_if_shown_on_screen();
}

void GLCanvas3D::on_timer(wxTimerEvent& evt)
{
    if (m_layers_editing.state != LayersEditing::Editing)
        return;

    _perform_layer_editing_action();
}

void GLCanvas3D::on_mouse(wxMouseEvent& evt)
{
    if (m_volumes == nullptr)
        return;

    Point pos(evt.GetX(), evt.GetY());

    int selected_object_idx = (is_layers_editing_enabled() && (m_print != nullptr)) ? _get_layers_editing_first_selected_object_id(m_print->objects.size()) : -1;
    m_layers_editing.last_object_id = selected_object_idx;

    if (evt.Entering())
    {
#if defined(__WXMSW__) || defined(__linux__)
        // On Windows and Linux needs focus in order to catch key events
        if (m_canvas != nullptr)
            m_canvas->SetFocus();

        m_mouse.set_start_position_2D_as_invalid();
#endif
    } 
    else if (evt.LeftDClick())
        m_on_double_click_callback.call();
    else if (evt.LeftDown() || evt.RightDown())
    {
        // If user pressed left or right button we first check whether this happened
        // on a volume or not.
        int volume_idx = m_hover_volume_id;
        m_layers_editing.state = LayersEditing::Unknown;
        if ((selected_object_idx != -1) && _bar_rect_contains(pos.x, pos.y))
        {
            // A volume is selected and the mouse is inside the layer thickness bar.
            // Start editing the layer height.
            m_layers_editing.state = LayersEditing::Editing;
            _perform_layer_editing_action(&evt);
        }
        else if ((selected_object_idx != -1) && _reset_rect_contains(pos.x, pos.y))
        {
            if (evt.LeftDown())
            {
                // A volume is selected and the mouse is inside the reset button.
                m_print->get_object(selected_object_idx)->reset_layer_height_profile();
                // Index 2 means no editing, just wait for mouse up event.
                m_layers_editing.state = LayersEditing::Completed;

                m_dirty = true;
            }
        }
        else
        {
            // Select volume in this 3D canvas.
            // Don't deselect a volume if layer editing is enabled. We want the object to stay selected
            // during the scene manipulation.

            if (m_picking_enabled && ((volume_idx != -1) || !is_layers_editing_enabled()))
            {
                deselect_volumes();
                select_volume(volume_idx);

                if (volume_idx != -1)
                {
                    int group_id = m_volumes->volumes[volume_idx]->select_group_id;
                    if (group_id != -1)
                    {
                        for (GLVolume* vol : m_volumes->volumes)
                        {
                            if ((vol != nullptr) && (vol->select_group_id == group_id))
                                vol->selected = true;
                        }
                    }
                }

                m_dirty = true;
            }

            // propagate event through callback
            m_on_select_callback.call(volume_idx);

            // The mouse_to_3d gets the Z coordinate from the Z buffer at the screen coordinate pos x, y,
            // an converts the screen space coordinate to unscaled object space.
            Pointf3 pos3d = (volume_idx == -1) ? Pointf3(DBL_MAX, DBL_MAX) : _mouse_to_3d(pos);

            if (volume_idx != -1)
            {
                if (evt.LeftDown() && m_moving_enabled)
                {
                    // Only accept the initial position, if it is inside the volume bounding box.
                    BoundingBoxf3 volume_bbox = m_volumes->volumes[volume_idx]->transformed_bounding_box();
                    volume_bbox.offset(1.0);
                    if (volume_bbox.contains(pos3d))
                    {
                        // The dragging operation is initiated.
                        m_mouse.drag.volume_idx = volume_idx;
                        m_mouse.drag.start_position_3D = pos3d;
                        // Remember the shift to to the object center.The object center will later be used
                        // to limit the object placement close to the bed.
                        m_mouse.drag.volume_center_offset = pos3d.vector_to(volume_bbox.center());
                    }
                }
                else if (evt.RightDown())
                {
                    // if right clicking on volume, propagate event through callback
                    if (m_volumes->volumes[volume_idx]->hover)
                        m_on_right_click_callback.call(pos.x, pos.y);
                }
            }
        }
    }
    else if (evt.Dragging() && evt.LeftIsDown() && (m_layers_editing.state == LayersEditing::Unknown) && (m_mouse.drag.volume_idx != -1))
    {
        m_mouse.dragging = true;

        // Get new position at the same Z of the initial click point.
        float z0 = 0.0f;
        float z1 = 1.0f;
        Pointf3 cur_pos = Linef3(_mouse_to_3d(pos, &z0), _mouse_to_3d(pos, &z1)).intersect_plane(m_mouse.drag.start_position_3D.z);

        // Clip the new position, so the object center remains close to the bed.
        cur_pos.translate(m_mouse.drag.volume_center_offset);
        Point cur_pos2(scale_(cur_pos.x), scale_(cur_pos.y));
        if (!m_bed.contains(cur_pos2))
        {
            Point ip = m_bed.point_projection(cur_pos2);
            cur_pos.x = unscale(ip.x);
            cur_pos.y = unscale(ip.y);
        }
        cur_pos.translate(m_mouse.drag.volume_center_offset.negative());

        // Calculate the translation vector.
        Vectorf3 vector = m_mouse.drag.start_position_3D.vector_to(cur_pos);
        // Get the volume being dragged.
        GLVolume* volume = m_volumes->volumes[m_mouse.drag.volume_idx];
        // Get all volumes belonging to the same group, if any.
        std::vector<GLVolume*> volumes;
        if (volume->drag_group_id == -1)
            volumes.push_back(volume);
        else
        {
            for (GLVolume* v : m_volumes->volumes)
            {
                if ((v != nullptr) && (v->drag_group_id == volume->drag_group_id))
                    volumes.push_back(v);
            }
        }

        // Apply new temporary volume origin and ignore Z.
        for (GLVolume* v : volumes)
        {
            v->origin.translate(vector.x, vector.y, 0.0);
        }

        m_mouse.drag.start_position_3D = cur_pos;

        m_dirty = true;
    }
    else if (evt.Dragging())
    {
        m_mouse.dragging = true;

        if ((m_layers_editing.state != LayersEditing::Unknown) && (selected_object_idx != -1))
        {
            if (m_layers_editing.state == LayersEditing::Editing)
                _perform_layer_editing_action(&evt);
        }
        else if (evt.LeftIsDown())
        {
            // if dragging over blank area with left button, rotate
            if (m_mouse.is_start_position_3D_defined())
            {
                const Pointf3& orig = m_mouse.drag.start_position_3D;
                m_camera.phi += (((float)pos.x - (float)orig.x) * TRACKBALLSIZE);
                m_camera.set_theta(m_camera.get_theta() - ((float)pos.y - (float)orig.y) * TRACKBALLSIZE);

                m_on_viewport_changed_callback.call();

                m_dirty = true;
            }
            m_mouse.drag.start_position_3D = Pointf3((coordf_t)pos.x, (coordf_t)pos.y, 0.0);
        }
        else if (evt.MiddleIsDown() || evt.RightIsDown())
        {
            // If dragging over blank area with right button, pan.
            if (m_mouse.is_start_position_2D_defined())
            {
                // get point in model space at Z = 0
                float z = 0.0f;
                const Pointf3& cur_pos = _mouse_to_3d(pos, &z);
                Pointf3 orig = _mouse_to_3d(m_mouse.drag.start_position_2D, &z);
                Pointf3 camera_target = m_camera.target;
                camera_target.translate(orig.vector_to(cur_pos).negative());
                m_camera.target = camera_target;

                m_on_viewport_changed_callback.call();

                m_dirty = true;
            }
            
            m_mouse.drag.start_position_2D = pos;
        }
    }
    else if (evt.LeftUp() || evt.MiddleUp() || evt.RightUp())
    {
        if (m_layers_editing.state != LayersEditing::Unknown)
        {
            m_layers_editing.state = LayersEditing::Unknown;
            _stop_timer();

            if (selected_object_idx != -1)
                m_on_model_update_callback.call();
        }
        else if ((m_mouse.drag.volume_idx != -1) && m_mouse.dragging)
        {
            // get all volumes belonging to the same group, if any
            std::vector<int> volume_idxs;
            int vol_id = m_mouse.drag.volume_idx;
            int group_id = m_volumes->volumes[vol_id]->drag_group_id;
            if (group_id == -1)
                volume_idxs.push_back(vol_id);
            else
            {
                for (int i = 0; i < m_volumes->volumes.size(); ++i)
                {
                    if (m_volumes->volumes[i]->drag_group_id == group_id)
                        volume_idxs.push_back(i);
                }
            }
            
            m_on_move_callback.call(volume_idxs);
        }
        
        m_mouse.drag.volume_idx = -1;
        m_mouse.set_start_position_3D_as_invalid();
        m_mouse.set_start_position_2D_as_invalid();
        m_mouse.dragging = false;
    }
    else if (evt.Moving())
    {
        m_mouse.position = Pointf((coordf_t)pos.x, (coordf_t)pos.y);
        // Only refresh if picking is enabled, in that case the objects may get highlighted if the mouse cursor hovers over.
        if (m_picking_enabled)
            m_dirty = true;
    }
    else
        evt.Skip();
}

void GLCanvas3D::on_paint(wxPaintEvent& evt)
{
    render();
}

Size GLCanvas3D::get_canvas_size() const
{
    int w = 0;
    int h = 0;

    if (m_canvas != nullptr)
        m_canvas->GetSize(&w, &h);

    return Size(w, h);
}

Point GLCanvas3D::get_local_mouse_position() const
{
    if (m_canvas == nullptr)
        return Point();

    wxPoint mouse_pos = m_canvas->ScreenToClient(wxGetMousePosition());
    return Point(mouse_pos.x, mouse_pos.y);
}

void GLCanvas3D::_force_zoom_to_bed()
{
    zoom_to_bed();
    m_force_zoom_to_bed_enabled = false;
}

void GLCanvas3D::_resize(unsigned int w, unsigned int h)
{
    if (m_context == nullptr)
        return;

    set_current();
    ::glViewport(0, 0, w, h);

    ::glMatrixMode(GL_PROJECTION);
    ::glLoadIdentity();

    const BoundingBoxf3& bbox = _max_bounding_box();

    switch (m_camera.type)
    {
    case Camera::Ortho:
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
//    case Camera::Perspective:
//    {
//        float bbox_r = (float)bbox.radius();
//        float fov = PI * 45.0f / 180.0f;
//        float fov_tan = tan(0.5f * fov);
//        float cam_distance = 0.5f * bbox_r / fov_tan;
//        m_camera.distance = cam_distance;
//
//        float nr = cam_distance - bbox_r * 1.1f;
//        float fr = cam_distance + bbox_r * 1.1f;
//        if (nr < 1.0f)
//            nr = 1.0f;
//
//        if (fr < nr + 1.0f)
//            fr = nr + 1.0f;
//
//        float h2 = fov_tan * nr;
//        float w2 = h2 * w / h;
//        ::glFrustum(-w2, w2, -h2, h2, nr, fr);
//
//        break;
//    }
    default:
    {
        throw std::runtime_error("Invalid camera type.");
        break;
    }
    }

    ::glMatrixMode(GL_MODELVIEW);

    m_dirty = false;
}

BoundingBoxf3 GLCanvas3D::_max_bounding_box() const
{
    BoundingBoxf3 bb = m_bed.get_bounding_box();
    bb.merge(volumes_bounding_box());
    return bb;
}

void GLCanvas3D::_zoom_to_bounding_box(const BoundingBoxf3& bbox)
{
    // Calculate the zoom factor needed to adjust viewport to bounding box.
    float zoom = _get_zoom_to_bounding_box_factor(bbox);
    if (zoom > 0.0f)
    {
        m_camera.zoom = zoom;
        // center view around bounding box center
        m_camera.target = bbox.center();

        m_on_viewport_changed_callback.call();

        _refresh_if_shown_on_screen();
    }
}

float GLCanvas3D::_get_zoom_to_bounding_box_factor(const BoundingBoxf3& bbox) const
{
    float max_bb_size = bbox.max_size();
    if (max_bb_size == 0.0f)
        return -1.0f;

    // project the bbox vertices on a plane perpendicular to the camera forward axis
    // then calculates the vertices coordinate on this plane along the camera xy axes

    // we need the view matrix, we let opengl calculate it (same as done in render())
    _camera_tranform();

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
    m_on_double_click_callback.deregister_callback();
    m_on_right_click_callback.deregister_callback();
    m_on_select_callback.deregister_callback();
    m_on_model_update_callback.deregister_callback();
    m_on_move_callback.deregister_callback();
}

void GLCanvas3D::_mark_volumes_for_layer_height() const
{
    if ((m_volumes == nullptr) || (m_print == nullptr))
        return;

    for (GLVolume* vol : m_volumes->volumes)
    {
        int object_id = int(vol->select_group_id / 1000000);
        int shader_id = m_layers_editing.get_shader_program_id();

        if (is_layers_editing_enabled() && (shader_id != -1) && vol->selected &&
            vol->has_layer_height_texture() && (object_id < (int)m_print->objects.size()))
        {
            vol->set_layer_height_texture_data(m_layers_editing.get_z_texture_id(), shader_id,
                m_print->get_object(object_id), _get_layers_editing_cursor_z_relative(), m_layers_editing.band_width);
        }
        else
            vol->reset_layer_height_texture_data();
    }
}

void GLCanvas3D::_refresh_if_shown_on_screen()
{
    if (is_shown_on_screen())
    {
        const Size& cnv_size = get_canvas_size();
        _resize((unsigned int)cnv_size.get_width(), (unsigned int)cnv_size.get_height());
        if (m_canvas != nullptr)
            m_canvas->Refresh();
    }
}

void GLCanvas3D::_camera_tranform() const
{
    ::glMatrixMode(GL_MODELVIEW);
    ::glLoadIdentity();

    ::glRotatef(-m_camera.get_theta(), 1.0f, 0.0f, 0.0f); // pitch
    ::glRotatef(m_camera.phi, 0.0f, 0.0f, 1.0f);    // yaw

    Pointf3 neg_target = m_camera.target.negative();
    ::glTranslatef((GLfloat)neg_target.x, (GLfloat)neg_target.y, (GLfloat)neg_target.z);
}

void GLCanvas3D::_picking_pass() const
{
    const Pointf& pos = m_mouse.position;

    if (m_picking_enabled && !m_mouse.dragging && (pos != Pointf(DBL_MAX, DBL_MAX)) && (m_volumes != nullptr))
    {
        // Render the object for picking.
        // FIXME This cannot possibly work in a multi - sampled context as the color gets mangled by the anti - aliasing.
        // Better to use software ray - casting on a bounding - box hierarchy.

        if (m_multisample_allowed)
            ::glDisable(GL_MULTISAMPLE);

        ::glDisable(GL_LIGHTING);
        ::glDisable(GL_BLEND);

        ::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ::glPushAttrib(GL_ENABLE_BIT);

        _render_volumes(true);

        ::glPopAttrib();

        if (m_multisample_allowed)
            ::glEnable(GL_MULTISAMPLE);

        const Size& cnv_size = get_canvas_size();

        GLubyte color[4];
        ::glReadPixels(pos.x, cnv_size.get_height() - pos.y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, (void*)color);
        int volume_id = color[0] + color[1] * 256 + color[2] * 256 * 256;

        m_hover_volume_id = -1;

        for (GLVolume* vol : m_volumes->volumes)
        {
            vol->hover = false;
        }

        if (volume_id < m_volumes->volumes.size())
        {
            m_hover_volume_id = volume_id;
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

void GLCanvas3D::_render_background() const
{
    ::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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

void GLCanvas3D::_render_bed() const
{
    m_bed.render();
}

void GLCanvas3D::_render_axes() const
{
    m_axes.render();
}

void GLCanvas3D::_render_objects() const
{
    if ((m_volumes == nullptr) || m_volumes->empty())
        return;

    ::glEnable(GL_LIGHTING);

    if (!m_shader_enabled)
        _render_volumes(false);
    else if (m_use_VBOs)
    {
        if (m_picking_enabled)
        {
            _mark_volumes_for_layer_height();

            if (m_config != nullptr)
            {
                const BoundingBoxf3& bed_bb = m_bed.get_bounding_box();
                m_volumes->set_print_box((float)bed_bb.min.x, (float)bed_bb.min.y, 0.0f, (float)bed_bb.max.x, (float)bed_bb.max.y, (float)m_config->opt_float("max_print_height"));
                m_volumes->check_outside_state(m_config);
            }
            // do not cull backfaces to show broken geometry, if any
            ::glDisable(GL_CULL_FACE);
        }

        m_shader.start_using();
        m_volumes->render_VBOs();
        m_shader.stop_using();

        if (m_picking_enabled)
            ::glEnable(GL_CULL_FACE);
    }
    else
    {
        // do not cull backfaces to show broken geometry, if any
        if (m_picking_enabled)
            ::glDisable(GL_CULL_FACE);

        m_volumes->render_legacy();

        if (m_picking_enabled)
            ::glEnable(GL_CULL_FACE);
    }
}

void GLCanvas3D::_render_cutting_plane() const
{
    m_cutting_plane.render(volumes_bounding_box());
}

void GLCanvas3D::_render_warning_texture() const
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

void GLCanvas3D::_render_legend_texture() const
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

void GLCanvas3D::_render_layer_editing_overlay() const
{
    if ((m_volumes == nullptr) || (m_print == nullptr))
        return;

    GLVolume* volume = nullptr;

    for (GLVolume* vol : m_volumes->volumes)
    {
        if ((vol != nullptr) && vol->selected && vol->has_layer_height_texture())
        {
            volume = vol;
            break;
        }
    }

    if (volume == nullptr)
        return;

    // If the active object was not allocated at the Print, go away.This should only be a momentary case between an object addition / deletion
    // and an update by Platter::async_apply_config.
    int object_idx = int(volume->select_group_id / 1000000);
    if ((int)m_print->objects.size() < object_idx)
        return;

    const PrintObject* print_object = m_print->get_object(object_idx);
    if (print_object == nullptr)
        return;

    m_layers_editing.render(*this, *print_object, *volume);
}

void GLCanvas3D::_render_volumes(bool fake_colors) const
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

float GLCanvas3D::_get_layers_editing_cursor_z_relative() const
{
    return m_layers_editing.get_cursor_z_relative(*this);
}

int GLCanvas3D::_get_layers_editing_first_selected_object_id(unsigned int objects_count) const
{
    return (m_volumes != nullptr) ? m_layers_editing.get_first_selected_object_id(*m_volumes, objects_count) : -1;
}

void GLCanvas3D::_perform_layer_editing_action(wxMouseEvent* evt)
{
    int object_idx_selected = m_layers_editing.last_object_id;
    if (object_idx_selected == -1)
        return;

    if ((m_volumes == nullptr) || (m_print == nullptr))
        return;

    PrintObject* selected_obj = m_print->get_object(object_idx_selected);
    if (selected_obj == nullptr)
        return;

    // A volume is selected. Test, whether hovering over a layer thickness bar.
    if (evt != nullptr)
    {
        const Rect& rect = LayersEditing::get_bar_rect_screen(*this);
        float b = rect.get_bottom();
        m_layers_editing.last_z = unscale(selected_obj->size.z) * (b - evt->GetY() - 1.0f) / (b - rect.get_top());
        m_layers_editing.last_action = evt->ShiftDown() ? (evt->RightIsDown() ? 3 : 2) : (evt->RightIsDown() ? 0 : 1);
    }

    // Mark the volume as modified, so Print will pick its layer height profile ? Where to mark it ?
    // Start a timer to refresh the print ? schedule_background_process() ?
    // The PrintObject::adjust_layer_height_profile() call adjusts the profile of its associated ModelObject, it does not modify the profile of the PrintObject itself.
    selected_obj->adjust_layer_height_profile(m_layers_editing.last_z, m_layers_editing.strength, m_layers_editing.band_width, m_layers_editing.last_action);

    // searches the id of the first volume of the selected object
    int volume_idx = 0;
    for (int i = 0; i < object_idx_selected; ++i)
    {
        PrintObject* obj = m_print->get_object(i);
        if (obj != nullptr)
        {
            for (int j = 0; j < (int)obj->region_volumes.size(); ++j)
            {
                volume_idx += (int)obj->region_volumes[j].size();
            }
        }
    }

    m_volumes->volumes[volume_idx]->generate_layer_height_texture(selected_obj, 1);
    _refresh_if_shown_on_screen();

    // Automatic action on mouse down with the same coordinate.
    _start_timer();
}

bool GLCanvas3D::_bar_rect_contains(float x, float y) const
{
    return m_layers_editing.bar_rect_contains(*this, x, y);
}

bool GLCanvas3D::_reset_rect_contains(float x, float y) const
{
    return m_layers_editing.reset_rect_contains(*this, x, y);
}

Pointf3 GLCanvas3D::_mouse_to_3d(const Point& mouse_pos, float* z)
{
    if (!set_current())
        return Pointf3(DBL_MAX, DBL_MAX, DBL_MAX);

    GLint viewport[4];
    ::glGetIntegerv(GL_VIEWPORT, viewport);
    GLdouble modelview_matrix[16];
    ::glGetDoublev(GL_MODELVIEW_MATRIX, modelview_matrix);
    GLdouble projection_matrix[16];
    ::glGetDoublev(GL_PROJECTION_MATRIX, projection_matrix);

    GLint y = viewport[3] - (GLint)mouse_pos.y;
    GLfloat mouse_z;
    if (z == nullptr)
        ::glReadPixels((GLint)mouse_pos.x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, (void*)&mouse_z);
    else
        mouse_z = *z;

    GLdouble out_x, out_y, out_z;
    ::gluUnProject((GLdouble)mouse_pos.x, (GLdouble)y, mouse_z, modelview_matrix, projection_matrix, viewport, &out_x, &out_y, &out_z);
    return Pointf3((coordf_t)out_x, (coordf_t)out_y, (coordf_t)out_z);
}

void GLCanvas3D::_start_timer()
{
    if (m_timer != nullptr)
        m_timer->Start(100, wxTIMER_CONTINUOUS);
}

void GLCanvas3D::_stop_timer()
{
    if (m_timer != nullptr)
        m_timer->Stop();
}

static inline int hex_digit_to_int(const char c)
{
    return
        (c >= '0' && c <= '9') ? int(c - '0') :
        (c >= 'A' && c <= 'F') ? int(c - 'A') + 10 :
        (c >= 'a' && c <= 'f') ? int(c - 'a') + 10 : -1;
}

std::vector<float> GLCanvas3D::_parse_colors(const std::vector<std::string>& colors)
{
    std::vector<float> output(colors.size() * 4, 1.0f);
    for (size_t i = 0; i < colors.size(); ++i) {
        const std::string& color = colors[i];
        const char* c = color.data() + 1;
        if ((color.size() == 7) && (color.front() == '#'))
        {
            for (size_t j = 0; j < 3; ++j)
            {
                int digit1 = hex_digit_to_int(*c++);
                int digit2 = hex_digit_to_int(*c++);
                if ((digit1 == -1) || (digit2 == -1))
                    break;

                output[i * 4 + j] = float(digit1 * 16 + digit2) / 255.0f;
            }
        }
    }
    return output;
}

void GLCanvas3D::_load_gcode_extrusion_paths(const GCodePreviewData& preview_data, const std::vector<float>& tool_colors)
{
    // helper functions to select data in dependence of the extrusion view type
    struct Helper
    {
        static float path_filter(GCodePreviewData::Extrusion::EViewType type, const ExtrusionPath& path)
        {
            switch (type)
            {
            case GCodePreviewData::Extrusion::FeatureType:
                return (float)path.role();
            case GCodePreviewData::Extrusion::Height:
                return path.height;
            case GCodePreviewData::Extrusion::Width:
                return path.width;
            case GCodePreviewData::Extrusion::Feedrate:
                return path.feedrate;
            case GCodePreviewData::Extrusion::VolumetricRate:
                return path.feedrate * (float)path.mm3_per_mm;
            case GCodePreviewData::Extrusion::Tool:
                return (float)path.extruder_id;
            }

            return 0.0f;
        }

        static GCodePreviewData::Color path_color(const GCodePreviewData& data, const std::vector<float>& tool_colors, float value)
        {
            switch (data.extrusion.view_type)
            {
            case GCodePreviewData::Extrusion::FeatureType:
                return data.get_extrusion_role_color((ExtrusionRole)(int)value);
            case GCodePreviewData::Extrusion::Height:
                return data.get_height_color(value);
            case GCodePreviewData::Extrusion::Width:
                return data.get_width_color(value);
            case GCodePreviewData::Extrusion::Feedrate:
                return data.get_feedrate_color(value);
            case GCodePreviewData::Extrusion::VolumetricRate:
                return data.get_volumetric_rate_color(value);
            case GCodePreviewData::Extrusion::Tool:
            {
                GCodePreviewData::Color color;
                ::memcpy((void*)color.rgba, (const void*)(tool_colors.data() + (unsigned int)value * 4), 4 * sizeof(float));
                return color;
            }
            }

            return GCodePreviewData::Color::Dummy;
        }
    };

    // Helper structure for filters
    struct Filter
    {
        float value;
        ExtrusionRole role;
        GLVolume* volume;

        Filter(float value, ExtrusionRole role)
            : value(value)
            , role(role)
            , volume(nullptr)
        {
        }

        bool operator == (const Filter& other) const
        {
            if (value != other.value)
                return false;

            if (role != other.role)
                return false;

            return true;
        }
    };

    typedef std::vector<Filter> FiltersList;
    size_t initial_volumes_count = m_volumes->volumes.size();

    // detects filters
    FiltersList filters;
    for (const GCodePreviewData::Extrusion::Layer& layer : preview_data.extrusion.layers)
    {
        for (const ExtrusionPath& path : layer.paths)
        {
            ExtrusionRole role = path.role();
            float path_filter = Helper::path_filter(preview_data.extrusion.view_type, path);
            if (std::find(filters.begin(), filters.end(), Filter(path_filter, role)) == filters.end())
                filters.emplace_back(path_filter, role);
        }
    }

    // nothing to render, return
    if (filters.empty())
        return;

    // creates a new volume for each filter
    for (Filter& filter : filters)
    {
        m_gcode_preview_volume_index.first_volumes.emplace_back(GCodePreviewVolumeIndex::Extrusion, (unsigned int)filter.role, (unsigned int)m_volumes->volumes.size());
        GLVolume* volume = new GLVolume(Helper::path_color(preview_data, tool_colors, filter.value).rgba);
        if (volume != nullptr)
        {
            filter.volume = volume;
            m_volumes->volumes.emplace_back(volume);
        }
        else
        {
            // an error occourred - restore to previous state and return
            m_gcode_preview_volume_index.first_volumes.pop_back();
            if (initial_volumes_count != m_volumes->volumes.size())
            {
                std::vector<GLVolume*>::iterator begin = m_volumes->volumes.begin() + initial_volumes_count;
                std::vector<GLVolume*>::iterator end = m_volumes->volumes.end();
                for (std::vector<GLVolume*>::iterator it = begin; it < end; ++it)
                {
                    GLVolume* volume = *it;
                    delete volume;
                }
                m_volumes->volumes.erase(begin, end);
                return;
            }
        }
    }

    // populates volumes
    for (const GCodePreviewData::Extrusion::Layer& layer : preview_data.extrusion.layers)
    {
        for (const ExtrusionPath& path : layer.paths)
        {
            float path_filter = Helper::path_filter(preview_data.extrusion.view_type, path);
            FiltersList::iterator filter = std::find(filters.begin(), filters.end(), Filter(path_filter, path.role()));
            if (filter != filters.end())
            {
                filter->volume->print_zs.push_back(layer.z);
                filter->volume->offsets.push_back(filter->volume->indexed_vertex_array.quad_indices.size());
                filter->volume->offsets.push_back(filter->volume->indexed_vertex_array.triangle_indices.size());

                _3DScene::extrusionentity_to_verts(path, layer.z, *filter->volume);
            }
        }
    }

    // finalize volumes and sends geometry to gpu
    if (m_volumes->volumes.size() > initial_volumes_count)
    {
        for (size_t i = initial_volumes_count; i < m_volumes->volumes.size(); ++i)
        {
            GLVolume* volume = m_volumes->volumes[i];
            volume->bounding_box = volume->indexed_vertex_array.bounding_box();
            volume->indexed_vertex_array.finalize_geometry(m_use_VBOs && m_initialized);
        }
    }
}

void GLCanvas3D::_load_gcode_travel_paths(const GCodePreviewData& preview_data, const std::vector<float>& tool_colors)
{
    size_t initial_volumes_count = m_volumes->volumes.size();
    m_gcode_preview_volume_index.first_volumes.emplace_back(GCodePreviewVolumeIndex::Travel, 0, (unsigned int)initial_volumes_count);

    bool res = true;
    switch (preview_data.extrusion.view_type)
    {
    case GCodePreviewData::Extrusion::Feedrate:
    {
        res = _travel_paths_by_feedrate(preview_data);
        break;
    }
    case GCodePreviewData::Extrusion::Tool:
    {
        res = _travel_paths_by_tool(preview_data, tool_colors);
        break;
    }
    default:
    {
        res = _travel_paths_by_type(preview_data);
        break;
    }
    }

    if (!res)
    {
        // an error occourred - restore to previous state and return
        if (initial_volumes_count != m_volumes->volumes.size())
        {
            std::vector<GLVolume*>::iterator begin = m_volumes->volumes.begin() + initial_volumes_count;
            std::vector<GLVolume*>::iterator end = m_volumes->volumes.end();
            for (std::vector<GLVolume*>::iterator it = begin; it < end; ++it)
            {
                GLVolume* volume = *it;
                delete volume;
            }
            m_volumes->volumes.erase(begin, end);
        }

        return;
    }

    // finalize volumes and sends geometry to gpu
    if (m_volumes->volumes.size() > initial_volumes_count)
    {
        for (size_t i = initial_volumes_count; i < m_volumes->volumes.size(); ++i)
        {
            GLVolume* volume = m_volumes->volumes[i];
            volume->bounding_box = volume->indexed_vertex_array.bounding_box();
            volume->indexed_vertex_array.finalize_geometry(m_use_VBOs && m_initialized);
        }
    }
}

bool GLCanvas3D::_travel_paths_by_type(const GCodePreviewData& preview_data)
{
    // Helper structure for types
    struct Type
    {
        GCodePreviewData::Travel::EType value;
        GLVolume* volume;

        explicit Type(GCodePreviewData::Travel::EType value)
            : value(value)
            , volume(nullptr)
        {
        }

        bool operator == (const Type& other) const
        {
            return value == other.value;
        }
    };

    typedef std::vector<Type> TypesList;

    // colors travels by travel type

    // detects types
    TypesList types;
    for (const GCodePreviewData::Travel::Polyline& polyline : preview_data.travel.polylines)
    {
        if (std::find(types.begin(), types.end(), Type(polyline.type)) == types.end())
            types.emplace_back(polyline.type);
    }

    // nothing to render, return
    if (types.empty())
        return true;

    // creates a new volume for each type
    for (Type& type : types)
    {
        GLVolume* volume = new GLVolume(preview_data.travel.type_colors[type.value].rgba);
        if (volume == nullptr)
            return false;
        else
        {
            type.volume = volume;
            m_volumes->volumes.emplace_back(volume);
        }
    }

    // populates volumes
    for (const GCodePreviewData::Travel::Polyline& polyline : preview_data.travel.polylines)
    {
        TypesList::iterator type = std::find(types.begin(), types.end(), Type(polyline.type));
        if (type != types.end())
        {
            type->volume->print_zs.push_back(unscale(polyline.polyline.bounding_box().min.z));
            type->volume->offsets.push_back(type->volume->indexed_vertex_array.quad_indices.size());
            type->volume->offsets.push_back(type->volume->indexed_vertex_array.triangle_indices.size());

            _3DScene::polyline3_to_verts(polyline.polyline, preview_data.travel.width, preview_data.travel.height, *type->volume);
        }
    }

    return true;
}

bool GLCanvas3D::_travel_paths_by_feedrate(const GCodePreviewData& preview_data)
{
    // Helper structure for feedrate
    struct Feedrate
    {
        float value;
        GLVolume* volume;

        explicit Feedrate(float value)
            : value(value)
            , volume(nullptr)
        {
        }

        bool operator == (const Feedrate& other) const
        {
            return value == other.value;
        }
    };

    typedef std::vector<Feedrate> FeedratesList;

    // colors travels by feedrate

    // detects feedrates
    FeedratesList feedrates;
    for (const GCodePreviewData::Travel::Polyline& polyline : preview_data.travel.polylines)
    {
        if (std::find(feedrates.begin(), feedrates.end(), Feedrate(polyline.feedrate)) == feedrates.end())
            feedrates.emplace_back(polyline.feedrate);
    }

    // nothing to render, return
    if (feedrates.empty())
        return true;

    // creates a new volume for each feedrate
    for (Feedrate& feedrate : feedrates)
    {
        GLVolume* volume = new GLVolume(preview_data.get_feedrate_color(feedrate.value).rgba);
        if (volume == nullptr)
            return false;
        else
        {
            feedrate.volume = volume;
            m_volumes->volumes.emplace_back(volume);
        }
    }

    // populates volumes
    for (const GCodePreviewData::Travel::Polyline& polyline : preview_data.travel.polylines)
    {
        FeedratesList::iterator feedrate = std::find(feedrates.begin(), feedrates.end(), Feedrate(polyline.feedrate));
        if (feedrate != feedrates.end())
        {
            feedrate->volume->print_zs.push_back(unscale(polyline.polyline.bounding_box().min.z));
            feedrate->volume->offsets.push_back(feedrate->volume->indexed_vertex_array.quad_indices.size());
            feedrate->volume->offsets.push_back(feedrate->volume->indexed_vertex_array.triangle_indices.size());

            _3DScene::polyline3_to_verts(polyline.polyline, preview_data.travel.width, preview_data.travel.height, *feedrate->volume);
        }
    }

    return true;
}

bool GLCanvas3D::_travel_paths_by_tool(const GCodePreviewData& preview_data, const std::vector<float>& tool_colors)
{
    // Helper structure for tool
    struct Tool
    {
        unsigned int value;
        GLVolume* volume;

        explicit Tool(unsigned int value)
            : value(value)
            , volume(nullptr)
        {
        }

        bool operator == (const Tool& other) const
        {
            return value == other.value;
        }
    };

    typedef std::vector<Tool> ToolsList;

    // colors travels by tool

    // detects tools
    ToolsList tools;
    for (const GCodePreviewData::Travel::Polyline& polyline : preview_data.travel.polylines)
    {
        if (std::find(tools.begin(), tools.end(), Tool(polyline.extruder_id)) == tools.end())
            tools.emplace_back(polyline.extruder_id);
    }

    // nothing to render, return
    if (tools.empty())
        return true;

    // creates a new volume for each tool
    for (Tool& tool : tools)
    {
        GLVolume* volume = new GLVolume(tool_colors.data() + tool.value * 4);
        if (volume == nullptr)
            return false;
        else
        {
            tool.volume = volume;
            m_volumes->volumes.emplace_back(volume);
        }
    }

    // populates volumes
    for (const GCodePreviewData::Travel::Polyline& polyline : preview_data.travel.polylines)
    {
        ToolsList::iterator tool = std::find(tools.begin(), tools.end(), Tool(polyline.extruder_id));
        if (tool != tools.end())
        {
            tool->volume->print_zs.push_back(unscale(polyline.polyline.bounding_box().min.z));
            tool->volume->offsets.push_back(tool->volume->indexed_vertex_array.quad_indices.size());
            tool->volume->offsets.push_back(tool->volume->indexed_vertex_array.triangle_indices.size());

            _3DScene::polyline3_to_verts(polyline.polyline, preview_data.travel.width, preview_data.travel.height, *tool->volume);
        }
    }

    return true;
}

void GLCanvas3D::_load_gcode_retractions(const GCodePreviewData& preview_data)
{
    m_gcode_preview_volume_index.first_volumes.emplace_back(GCodePreviewVolumeIndex::Retraction, 0, (unsigned int)m_volumes->volumes.size());

    // nothing to render, return
    if (preview_data.retraction.positions.empty())
        return;

    GLVolume* volume = new GLVolume(preview_data.retraction.color.rgba);
    if (volume != nullptr)
    {
        m_volumes->volumes.emplace_back(volume);

        GCodePreviewData::Retraction::PositionsList copy(preview_data.retraction.positions);
        std::sort(copy.begin(), copy.end(), [](const GCodePreviewData::Retraction::Position& p1, const GCodePreviewData::Retraction::Position& p2){ return p1.position.z < p2.position.z; });

        for (const GCodePreviewData::Retraction::Position& position : copy)
        {
            volume->print_zs.push_back(unscale(position.position.z));
            volume->offsets.push_back(volume->indexed_vertex_array.quad_indices.size());
            volume->offsets.push_back(volume->indexed_vertex_array.triangle_indices.size());

            _3DScene::point3_to_verts(position.position, position.width, position.height, *volume);
        }

        // finalize volumes and sends geometry to gpu
        volume->bounding_box = volume->indexed_vertex_array.bounding_box();
        volume->indexed_vertex_array.finalize_geometry(m_use_VBOs && m_initialized);
    }
}

void GLCanvas3D::_load_gcode_unretractions(const GCodePreviewData& preview_data)
{
    m_gcode_preview_volume_index.first_volumes.emplace_back(GCodePreviewVolumeIndex::Unretraction, 0, (unsigned int)m_volumes->volumes.size());

    // nothing to render, return
    if (preview_data.unretraction.positions.empty())
        return;

    GLVolume* volume = new GLVolume(preview_data.unretraction.color.rgba);
    if (volume != nullptr)
    {
        m_volumes->volumes.emplace_back(volume);

        GCodePreviewData::Retraction::PositionsList copy(preview_data.unretraction.positions);
        std::sort(copy.begin(), copy.end(), [](const GCodePreviewData::Retraction::Position& p1, const GCodePreviewData::Retraction::Position& p2){ return p1.position.z < p2.position.z; });

        for (const GCodePreviewData::Retraction::Position& position : copy)
        {
            volume->print_zs.push_back(unscale(position.position.z));
            volume->offsets.push_back(volume->indexed_vertex_array.quad_indices.size());
            volume->offsets.push_back(volume->indexed_vertex_array.triangle_indices.size());

            _3DScene::point3_to_verts(position.position, position.width, position.height, *volume);
        }

        // finalize volumes and sends geometry to gpu
        volume->bounding_box = volume->indexed_vertex_array.bounding_box();
        volume->indexed_vertex_array.finalize_geometry(m_use_VBOs && m_initialized);
    }
}

void GLCanvas3D::_load_shells()
{
    size_t initial_volumes_count = m_volumes->volumes.size();
    m_gcode_preview_volume_index.first_volumes.emplace_back(GCodePreviewVolumeIndex::Shell, 0, (unsigned int)initial_volumes_count);

    if (m_print->objects.empty())
        // nothing to render, return
        return;

    // adds objects' volumes 
    unsigned int object_id = 0;
    for (PrintObject* obj : m_print->objects)
    {
        ModelObject* model_obj = obj->model_object();

        std::vector<int> instance_ids(model_obj->instances.size());
        for (int i = 0; i < model_obj->instances.size(); ++i)
        {
            instance_ids[i] = i;
        }

        for (ModelInstance* instance : model_obj->instances)
        {
            m_volumes->load_object(model_obj, object_id, instance_ids, "object", "object", "object", m_use_VBOs && m_initialized);
        }

        ++object_id;
    }

    // adds wipe tower's volume
    coordf_t max_z = m_print->objects[0]->model_object()->get_model()->bounding_box().max.z;
    const PrintConfig& config = m_print->config;
    unsigned int extruders_count = config.nozzle_diameter.size();
    if ((extruders_count > 1) && config.single_extruder_multi_material && config.wipe_tower && !config.complete_objects) {
        const float width_per_extruder = 15.0f; // a simple workaround after wipe_tower_per_color_wipe got obsolete
        m_volumes->load_wipe_tower_preview(1000, config.wipe_tower_x, config.wipe_tower_y, config.wipe_tower_width, width_per_extruder * (extruders_count - 1), max_z, config.wipe_tower_rotation_angle, m_use_VBOs && m_initialized);
    }
}

void GLCanvas3D::_update_gcode_volumes_visibility(const GCodePreviewData& preview_data)
{
    unsigned int size = (unsigned int)m_gcode_preview_volume_index.first_volumes.size();
    for (unsigned int i = 0; i < size; ++i)
    {
        std::vector<GLVolume*>::iterator begin = m_volumes->volumes.begin() + m_gcode_preview_volume_index.first_volumes[i].id;
        std::vector<GLVolume*>::iterator end = (i + 1 < size) ? m_volumes->volumes.begin() + m_gcode_preview_volume_index.first_volumes[i + 1].id : m_volumes->volumes.end();

        for (std::vector<GLVolume*>::iterator it = begin; it != end; ++it)
        {
            GLVolume* volume = *it;
            volume->outside_printer_detection_enabled = false;

            switch (m_gcode_preview_volume_index.first_volumes[i].type)
            {
            case GCodePreviewVolumeIndex::Extrusion:
            {
                if ((ExtrusionRole)m_gcode_preview_volume_index.first_volumes[i].flag == erCustom)
                    volume->zoom_to_volumes = false;

                volume->is_active = preview_data.extrusion.is_role_flag_set((ExtrusionRole)m_gcode_preview_volume_index.first_volumes[i].flag);
                break;
            }
            case GCodePreviewVolumeIndex::Travel:
            {
                volume->is_active = preview_data.travel.is_visible;
                volume->zoom_to_volumes = false;
                break;
            }
            case GCodePreviewVolumeIndex::Retraction:
            {
                volume->is_active = preview_data.retraction.is_visible;
                volume->zoom_to_volumes = false;
                break;
            }
            case GCodePreviewVolumeIndex::Unretraction:
            {
                volume->is_active = preview_data.unretraction.is_visible;
                volume->zoom_to_volumes = false;
                break;
            }
            case GCodePreviewVolumeIndex::Shell:
            {
                volume->is_active = preview_data.shell.is_visible;
                volume->color[3] = 0.25f;
                volume->zoom_to_volumes = false;
                break;
            }
            default:
            {
                volume->is_active = false;
                volume->zoom_to_volumes = false;
                break;
            }
            }
        }
    }
}

} // namespace GUI
} // namespace Slic3r
