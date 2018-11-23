#include "GLCanvas3D.hpp"

#include "admesh/stl.h"
#include "libslic3r/libslic3r.h"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/BackgroundSlicingProcess.hpp"
#include "slic3r/GUI/GLShader.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/PresetBundle.hpp"
#include "slic3r/GUI/GLGizmo.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "libslic3r/GCode/PreviewData.hpp"
#include "libslic3r/Geometry.hpp"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "GUI_ObjectManipulation.hpp"

#include <GL/glew.h>

#include <wx/glcanvas.h>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/settings.h>
#include <wx/tooltip.h>

// Print now includes tbb, and tbb includes Windows. This breaks compilation of wxWidgets if included before wx.
#include "libslic3r/Print.hpp"
#include "libslic3r/SLAPrint.hpp"

#include "wxExtensions.hpp"

#include <tbb/parallel_for.h>
#include <tbb/spin_mutex.h>

#include <boost/log/trivial.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <iostream>
#include <float.h>
#include <algorithm>

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
static const float GIZMO_RESET_BUTTON_HEIGHT = 22.0f;
static const float GIZMO_RESET_BUTTON_WIDTH = 70.f;

static const float UNIT_MATRIX[] = { 1.0f, 0.0f, 0.0f, 0.0f,
                                     0.0f, 1.0f, 0.0f, 0.0f,
                                     0.0f, 0.0f, 1.0f, 0.0f,
                                     0.0f, 0.0f, 0.0f, 1.0f };

static const float DEFAULT_BG_COLOR[3] = { 10.0f / 255.0f, 98.0f / 255.0f, 144.0f / 255.0f };
static const float ERROR_BG_COLOR[3] = { 144.0f / 255.0f, 49.0f / 255.0f, 10.0f / 255.0f };

namespace Slic3r {
namespace GUI {

bool GeometryBuffer::set_from_triangles(const Polygons& triangles, float z, bool generate_tex_coords)
{
    m_vertices.clear();
    m_tex_coords.clear();

    unsigned int v_size = 9 * (unsigned int)triangles.size();
    unsigned int t_size = 6 * (unsigned int)triangles.size();
    if (v_size == 0)
        return false;

    m_vertices = std::vector<float>(v_size, 0.0f);
    if (generate_tex_coords)
        m_tex_coords = std::vector<float>(t_size, 0.0f);

    float min_x = unscale<float>(triangles[0].points[0](0));
    float min_y = unscale<float>(triangles[0].points[0](1));
    float max_x = min_x;
    float max_y = min_y;

    unsigned int v_coord = 0;
    unsigned int t_coord = 0;
    for (const Polygon& t : triangles)
    {
        for (unsigned int v = 0; v < 3; ++v)
        {
            const Point& p = t.points[v];
            float x = unscale<float>(p(0));
            float y = unscale<float>(p(1));

            m_vertices[v_coord++] = x;
            m_vertices[v_coord++] = y;
            m_vertices[v_coord++] = z;

            if (generate_tex_coords)
            {
                m_tex_coords[t_coord++] = x;
                m_tex_coords[t_coord++] = y;

                min_x = std::min(min_x, x);
                max_x = std::max(max_x, x);
                min_y = std::min(min_y, y);
                max_y = std::max(max_y, y);
            }
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
            for (unsigned int i = 0; i < m_tex_coords.size(); i += 2)
            {
                m_tex_coords[i] *= inv_size_x;
                m_tex_coords[i + 1] *= inv_size_y;
            }
        }
    }

    return true;
}

bool GeometryBuffer::set_from_lines(const Lines& lines, float z)
{
    m_vertices.clear();
    m_tex_coords.clear();

    unsigned int size = 6 * (unsigned int)lines.size();
    if (size == 0)
        return false;

    m_vertices = std::vector<float>(size, 0.0f);

    unsigned int coord = 0;
    for (const Line& l : lines)
    {
        m_vertices[coord++] = unscale<float>(l.a(0));
        m_vertices[coord++] = unscale<float>(l.a(1));
        m_vertices[coord++] = z;
        m_vertices[coord++] = unscale<float>(l.b(0));
        m_vertices[coord++] = unscale<float>(l.b(1));
        m_vertices[coord++] = z;
    }

    return true;
}

const float* GeometryBuffer::get_vertices() const
{
    return m_vertices.data();
}

const float* GeometryBuffer::get_tex_coords() const
{
    return m_tex_coords.data();
}

unsigned int GeometryBuffer::get_vertices_count() const
{
    return (unsigned int)m_vertices.size() / 3;
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

GLCanvas3D::Bed::Bed()
    : m_type(Custom)
{
}

bool GLCanvas3D::Bed::is_prusa() const
{
    return (m_type == MK2) || (m_type == MK3);
}

bool GLCanvas3D::Bed::is_custom() const
{
    return m_type == Custom;
}

const Pointfs& GLCanvas3D::Bed::get_shape() const
{
    return m_shape;
}

bool GLCanvas3D::Bed::set_shape(const Pointfs& shape)
{
    EType new_type = _detect_type();
    if (m_shape == shape && m_type == new_type)
        // No change, no need to update the UI.
        return false;

    m_shape = shape;
    m_type = new_type;

    _calc_bounding_box();

    ExPolygon poly;
    for (const Vec2d& p : m_shape)
    {
        poly.contour.append(Point(scale_(p(0)), scale_(p(1))));
    }

    _calc_triangles(poly);

    const BoundingBox& bed_bbox = poly.contour.bounding_box();
    _calc_gridlines(poly, bed_bbox);

    m_polygon = offset_ex(poly.contour, (float)bed_bbox.radius() * 1.7f, jtRound, scale_(0.5))[0].contour;
    // Let the calee to update the UI.
    return true;
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

void GLCanvas3D::Bed::render(float theta) const
{
    switch (m_type)
    {
    case MK2:
    {
        _render_mk2(theta);
        break;
    }
    case MK3:
    {
        _render_mk3(theta);
        break;
    }
    default:
    case Custom:
    {
        _render_custom();
        break;
    }
    }
}

void GLCanvas3D::Bed::_calc_bounding_box()
{
    m_bounding_box = BoundingBoxf3();
    for (const Vec2d& p : m_shape)
    {
        m_bounding_box.merge(Vec3d(p(0), p(1), 0.0));
    }
}

void GLCanvas3D::Bed::_calc_triangles(const ExPolygon& poly)
{
    Polygons triangles;
    poly.triangulate(&triangles);

    if (!m_triangles.set_from_triangles(triangles, GROUND_Z, m_type != Custom))
        printf("Unable to create bed triangles\n");
}

void GLCanvas3D::Bed::_calc_gridlines(const ExPolygon& poly, const BoundingBox& bed_bbox)
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

GLCanvas3D::Bed::EType GLCanvas3D::Bed::_detect_type() const
{
    EType type = Custom;

    auto bundle = wxGetApp().preset_bundle;
    if (bundle != nullptr)
    {
        const Preset* curr = &bundle->printers.get_selected_preset();
        while (curr != nullptr)
        {
            if (curr->config.has("bed_shape") && _are_equal(m_shape, dynamic_cast<const ConfigOptionPoints*>(curr->config.option("bed_shape"))->values))
            {
                if ((curr->vendor != nullptr) && (curr->vendor->name == "Prusa Research"))
                {
                    if (boost::contains(curr->name, "MK2"))
                    {
                        type = MK2;
                        break;
                    }
                    else if (boost::contains(curr->name, "MK3"))
                    {
                        type = MK3;
                        break;
                    }
                }
            }

            curr = bundle->printers.get_preset_parent(*curr);
        }
    }

    return type;
}

void GLCanvas3D::Bed::_render_mk2(float theta) const
{
    std::string filename = resources_dir() + "/icons/bed/mk2_top.png";
    if ((m_top_texture.get_id() == 0) || (m_top_texture.get_source() != filename))
    {
        if (!m_top_texture.load_from_file(filename, true))
        {
            _render_custom();
            return;
        }
    }

    filename = resources_dir() + "/icons/bed/mk2_bottom.png";
    if ((m_bottom_texture.get_id() == 0) || (m_bottom_texture.get_source() != filename))
    {
        if (!m_bottom_texture.load_from_file(filename, true))
        {
            _render_custom();
            return;
        }
    }

    _render_prusa(theta);
}

void GLCanvas3D::Bed::_render_mk3(float theta) const
{
    std::string filename = resources_dir() + "/icons/bed/mk3_top.png";
    if ((m_top_texture.get_id() == 0) || (m_top_texture.get_source() != filename))
    {
        if (!m_top_texture.load_from_file(filename, true))
        {
            _render_custom();
            return;
        }
    }

    filename = resources_dir() + "/icons/bed/mk3_bottom.png";
    if ((m_bottom_texture.get_id() == 0) || (m_bottom_texture.get_source() != filename))
    {
        if (!m_bottom_texture.load_from_file(filename, true))
        {
            _render_custom();
            return;
        }
    }

    _render_prusa(theta);
}

void GLCanvas3D::Bed::_render_prusa(float theta) const
{
    unsigned int triangles_vcount = m_triangles.get_vertices_count();
    if (triangles_vcount > 0)
    {
        ::glEnable(GL_DEPTH_TEST);
        ::glDepthMask(GL_FALSE);

        ::glEnable(GL_BLEND);
        ::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        ::glEnable(GL_TEXTURE_2D);
        ::glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

        ::glEnableClientState(GL_VERTEX_ARRAY);
        ::glEnableClientState(GL_TEXTURE_COORD_ARRAY);

        if (theta > 90.0f)
            ::glFrontFace(GL_CW);

        ::glBindTexture(GL_TEXTURE_2D, (theta <= 90.0f) ? (GLuint)m_top_texture.get_id() : (GLuint)m_bottom_texture.get_id());
        ::glVertexPointer(3, GL_FLOAT, 0, (GLvoid*)m_triangles.get_vertices());
        ::glTexCoordPointer(2, GL_FLOAT, 0, (GLvoid*)m_triangles.get_tex_coords());
        ::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)triangles_vcount);

        if (theta > 90.0f)
            ::glFrontFace(GL_CCW);

        ::glBindTexture(GL_TEXTURE_2D, 0);
        ::glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        ::glDisableClientState(GL_VERTEX_ARRAY);

        ::glDisable(GL_TEXTURE_2D);

        ::glDisable(GL_BLEND);
        ::glDepthMask(GL_TRUE);
    }
}

void GLCanvas3D::Bed::_render_custom() const
{
    m_top_texture.reset();
    m_bottom_texture.reset();

    unsigned int triangles_vcount = m_triangles.get_vertices_count();
    if (triangles_vcount > 0)
    {
        ::glEnable(GL_LIGHTING);
        ::glDisable(GL_DEPTH_TEST);

        ::glEnable(GL_BLEND);
        ::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        ::glEnableClientState(GL_VERTEX_ARRAY);

        ::glColor4f(0.8f, 0.6f, 0.5f, 0.4f);
        ::glNormal3d(0.0f, 0.0f, 1.0f);
        ::glVertexPointer(3, GL_FLOAT, 0, (GLvoid*)m_triangles.get_vertices());
        ::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)triangles_vcount);

        // draw grid
        unsigned int gridlines_vcount = m_gridlines.get_vertices_count();

        // we need depth test for grid, otherwise it would disappear when looking the object from below
        ::glEnable(GL_DEPTH_TEST);
        ::glLineWidth(3.0f);
        ::glColor4f(0.2f, 0.2f, 0.2f, 0.4f);
        ::glVertexPointer(3, GL_FLOAT, 0, (GLvoid*)m_gridlines.get_vertices());
        ::glDrawArrays(GL_LINES, 0, (GLsizei)gridlines_vcount);

        ::glDisableClientState(GL_VERTEX_ARRAY);

        ::glDisable(GL_BLEND);
        ::glDisable(GL_LIGHTING);
    }
}

bool GLCanvas3D::Bed::_are_equal(const Pointfs& bed_1, const Pointfs& bed_2)
{
    if (bed_1.size() != bed_2.size())
        return false;

    for (unsigned int i = 0; i < (unsigned int)bed_1.size(); ++i)
    {
        if (bed_1[i] != bed_2[i])
            return false;
    }

    return true;
}

GLCanvas3D::Axes::Axes()
    : origin(Vec3d::Zero())
    , length(0.0f)
{
}

void GLCanvas3D::Axes::render(bool depth_test) const
{
    if (depth_test)
        ::glEnable(GL_DEPTH_TEST);
    else
        ::glDisable(GL_DEPTH_TEST);

    ::glLineWidth(2.0f);
    ::glBegin(GL_LINES);
    // draw line for x axis
    ::glColor3f(1.0f, 0.0f, 0.0f);
    ::glVertex3dv(origin.data());
    ::glVertex3f((GLfloat)origin(0) + length, (GLfloat)origin(1), (GLfloat)origin(2));
    // draw line for y axis
    ::glColor3f(0.0f, 1.0f, 0.0f);
    ::glVertex3dv(origin.data());
    ::glVertex3f((GLfloat)origin(0), (GLfloat)origin(1) + length, (GLfloat)origin(2));
    ::glEnd();
    // draw line for Z axis
    // (re-enable depth test so that axis is correctly shown when objects are behind it)
    if (!depth_test)
        ::glEnable(GL_DEPTH_TEST);

    ::glBegin(GL_LINES);
    ::glColor3f(0.0f, 0.0f, 1.0f);
    ::glVertex3dv(origin.data());
    ::glVertex3f((GLfloat)origin(0), (GLfloat)origin(1), (GLfloat)origin(2) + length);
    ::glEnd();
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

void GLCanvas3D::Shader::set_uniform(const std::string& name, const float* matrix) const
{
    if (m_shader != nullptr)
        m_shader->set_uniform(name.c_str(), matrix);
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
    _render_reset_texture(reset_rect);
    _render_active_object_annotations(canvas, volume, print_object, bar_rect);
    _render_profile(print_object, bar_rect);

    // Revert the matrices.
    ::glPopMatrix();

    ::glEnable(GL_DEPTH_TEST);
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
    float x = (float)mouse_pos(0);
    float y = (float)mouse_pos(1);
    float t = rect.get_top();
    float b = rect.get_bottom();

    return ((rect.get_left() <= x) && (x <= rect.get_right()) && (t <= y) && (y <= b)) ?
        // Inside the bar.
        (b - y - 1.0f) / (b - t - 1.0f) :
        // Outside the bar.
        -1000.0f;
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
    if (m_tooltip_texture.get_id() == 0)
    {
        std::string filename = resources_dir() + "/icons/variable_layer_height_tooltip.png";
        if (!m_tooltip_texture.load_from_file(filename, false))
            return;
    }

    float zoom = canvas.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
    float gap = 10.0f * inv_zoom;

    float bar_left = bar_rect.get_left();
    float reset_bottom = reset_rect.get_bottom();

    float l = bar_left - (float)m_tooltip_texture.get_width() * inv_zoom - gap;
    float r = bar_left - gap;
    float t = reset_bottom + (float)m_tooltip_texture.get_height() * inv_zoom + gap;
    float b = reset_bottom + gap;

    GLTexture::render_texture(m_tooltip_texture.get_id(), l, r, b, t);
}

void GLCanvas3D::LayersEditing::_render_reset_texture(const Rect& reset_rect) const
{
    if (m_reset_texture.get_id() == 0)
    {
        std::string filename = resources_dir() + "/icons/variable_layer_height_reset.png";
        if (!m_reset_texture.load_from_file(filename, false))
            return;
    }

    GLTexture::render_texture(m_reset_texture.get_id(), reset_rect.get_left(), reset_rect.get_right(), reset_rect.get_bottom(), reset_rect.get_top());
}

void GLCanvas3D::LayersEditing::_render_active_object_annotations(const GLCanvas3D& canvas, const GLVolume& volume, const PrintObject& print_object, const Rect& bar_rect) const
{
    float max_z = print_object.model_object()->bounding_box().max(2);

    m_shader.start_using();

    m_shader.set_uniform("z_to_texture_row", (float)volume.layer_height_texture_z_to_row_id());
    m_shader.set_uniform("z_texture_row_to_normalized", 1.0f / (float)volume.layer_height_texture_height());
    m_shader.set_uniform("z_cursor", max_z * get_cursor_z_relative(canvas));
    m_shader.set_uniform("z_cursor_band_width", band_width);
    // The shader requires the original model coordinates when rendering to the texture, so we pass it the unit matrix
    m_shader.set_uniform("volume_world_matrix", UNIT_MATRIX);

    GLsizei w = (GLsizei)volume.layer_height_texture_width();
    GLsizei h = (GLsizei)volume.layer_height_texture_height();
    GLsizei half_w = w / 2;
    GLsizei half_h = h / 2;

    ::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    ::glBindTexture(GL_TEXTURE_2D, m_z_texture_id);
    ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
    ::glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, half_w, half_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
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
    const PrintConfig& print_config = print_object.print()->config();
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

    double max_z = unscale<double>(print_object.size(2));
    double layer_height = dynamic_cast<const ConfigOptionFloat*>(print_object.config().option("layer_height"))->value;
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
        const std::vector<double>& profile = model_object->layer_height_profile;

        ::glColor3f(0.0f, 0.0f, 1.0f);
        ::glBegin(GL_LINE_STRIP);
        for (unsigned int i = 0; i < profile.size(); i += 2)
        {
            ::glVertex2f(l + (float)profile[i + 1] * scale_x, b + (float)profile[i] * scale_y);
        }
        ::glEnd();
    }
}

const Point GLCanvas3D::Mouse::Drag::Invalid_2D_Point(INT_MAX, INT_MAX);
const Vec3d GLCanvas3D::Mouse::Drag::Invalid_3D_Point(DBL_MAX, DBL_MAX, DBL_MAX);

GLCanvas3D::Mouse::Drag::Drag()
    : start_position_2D(Invalid_2D_Point)
    , start_position_3D(Invalid_3D_Point)
    , move_volume_idx(-1)
{
}

GLCanvas3D::Mouse::Mouse()
    : dragging(false)
    , position(DBL_MAX, DBL_MAX)
#if ENABLE_GIZMOS_ON_TOP
    , scene_position(DBL_MAX, DBL_MAX, DBL_MAX)
#endif // ENABLE_GIZMOS_ON_TOP
#if ENABLE_GIZMOS_RESET
    , ignore_up_event(false)
#endif // ENABLE_GIZMOS_RESET
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

#if ENABLE_MODELVOLUME_TRANSFORM
GLCanvas3D::Selection::VolumeCache::TransformCache::TransformCache()
    : position(Vec3d::Zero())
    , rotation(Vec3d::Zero())
    , scaling_factor(Vec3d::Ones())
    , rotation_matrix(Transform3d::Identity())
    , scale_matrix(Transform3d::Identity())
{
}

GLCanvas3D::Selection::VolumeCache::TransformCache::TransformCache(const Geometry::Transformation& transform)
    : position(transform.get_offset())
    , rotation(transform.get_rotation())
    , scaling_factor(transform.get_scaling_factor())
{
    rotation_matrix = Geometry::assemble_transform(Vec3d::Zero(), rotation);
    scale_matrix = Geometry::assemble_transform(Vec3d::Zero(), Vec3d::Zero(), scaling_factor);
}

GLCanvas3D::Selection::VolumeCache::VolumeCache(const Geometry::Transformation& volume_transform, const Geometry::Transformation& instance_transform)
    : m_volume(volume_transform)
    , m_instance(instance_transform)
{
}
#else
GLCanvas3D::Selection::VolumeCache::VolumeCache()
    : m_position(Vec3d::Zero())
    , m_rotation(Vec3d::Zero())
    , m_scaling_factor(Vec3d::Ones())
{
    m_rotation_matrix = Transform3d::Identity();
    m_scale_matrix = Transform3d::Identity();
}

GLCanvas3D::Selection::VolumeCache::VolumeCache(const Vec3d& position, const Vec3d& rotation, const Vec3d& scaling_factor)
    : m_position(position)
    , m_rotation(rotation)
    , m_scaling_factor(scaling_factor)
{
    m_rotation_matrix = Geometry::assemble_transform(Vec3d::Zero(), m_rotation);
    m_scale_matrix = Geometry::assemble_transform(Vec3d::Zero(), Vec3d::Zero(), m_scaling_factor);
}
#endif // ENABLE_MODELVOLUME_TRANSFORM

GLCanvas3D::Selection::Selection()
    : m_volumes(nullptr)
    , m_model(nullptr)
    , m_mode(Instance)
    , m_type(Empty)
    , m_valid(false)
    , m_bounding_box_dirty(true)
{
}

void GLCanvas3D::Selection::set_volumes(GLVolumePtrs* volumes)
{
    m_volumes = volumes;
    _update_valid();
}

void GLCanvas3D::Selection::set_model(Model* model)
{
    m_model = model;
    _update_valid();
}

void GLCanvas3D::Selection::add(unsigned int volume_idx, bool as_single_selection)
{
    if (!m_valid || ((unsigned int)m_volumes->size() <= volume_idx))
        return;

    const GLVolume* volume = (*m_volumes)[volume_idx];
    // wipe tower is already selected
    if (is_wipe_tower() && volume->is_wipe_tower)
        return;

    // resets the current list if needed
    bool needs_reset = as_single_selection;
    needs_reset |= volume->is_wipe_tower;
    needs_reset |= is_wipe_tower() && !volume->is_wipe_tower;
    needs_reset |= !is_modifier() && volume->is_modifier;
    needs_reset |= is_modifier() && !volume->is_modifier;

    if (needs_reset)
        clear();

    if (volume->is_modifier)
        m_mode = Volume;

    switch (m_mode)
    {
    case Volume:
    {
        if (volume->volume_idx() >= 0 && (is_empty() || (volume->instance_idx() == get_instance_idx())))
            _add_volume(volume_idx);

        break;
    }
    case Instance:
    {
        _add_instance(volume->object_idx(), volume->instance_idx());
        break;
    }
#if !ENABLE_MODELVOLUME_TRANSFORM
    case Object:
    {
        _add_object(volume->object_idx());
        break;
    }
#endif // !ENABLE_MODELVOLUME_TRANSFORM
    }

    _update_type();
    m_bounding_box_dirty = true;
}

void GLCanvas3D::Selection::remove(unsigned int volume_idx)
{
    if (!m_valid || ((unsigned int)m_volumes->size() <= volume_idx))
        return;

    GLVolume* volume = (*m_volumes)[volume_idx];

    switch (m_mode)
    {
    case Volume:
    {
        _remove_volume(volume_idx);
        break;
    }
    case Instance:
    {
        _remove_instance(volume->object_idx(), volume->instance_idx());
        break;
    }
#if !ENABLE_MODELVOLUME_TRANSFORM
    case Object:
    {
        _remove_object(volume->object_idx());
        break;
    }
#endif // !ENABLE_MODELVOLUME_TRANSFORM
    }

    _update_type();
    m_bounding_box_dirty = true;
}

void GLCanvas3D::Selection::add_object(unsigned int object_idx, bool as_single_selection)
{
    if (!m_valid)
        return;

    // resets the current list if needed
    if (as_single_selection)
        clear();

    m_mode = Instance;

    _add_object(object_idx);

    _update_type();
    m_bounding_box_dirty = true;
}

void GLCanvas3D::Selection::remove_object(unsigned int object_idx)
{
    if (!m_valid)
        return;

    _remove_object(object_idx);

    _update_type();
    m_bounding_box_dirty = true;
}

void GLCanvas3D::Selection::add_instance(unsigned int object_idx, unsigned int instance_idx, bool as_single_selection)
{
    if (!m_valid)
        return;

    // resets the current list if needed
    if (as_single_selection)
        clear();

    m_mode = Instance;

    _add_instance(object_idx, instance_idx);

    _update_type();
    m_bounding_box_dirty = true;
}

void GLCanvas3D::Selection::remove_instance(unsigned int object_idx, unsigned int instance_idx)
{
    if (!m_valid)
        return;

    _remove_instance(object_idx, instance_idx);

    _update_type();
    m_bounding_box_dirty = true;
}

void GLCanvas3D::Selection::add_volume(unsigned int object_idx, unsigned int volume_idx, int instance_idx, bool as_single_selection)
{
    if (!m_valid)
        return;

    // resets the current list if needed
    if (as_single_selection)
        clear();

    m_mode = Volume;

    for (unsigned int i = 0; i < (unsigned int)m_volumes->size(); ++i)
    {
        GLVolume* v = (*m_volumes)[i];
        if ((v->object_idx() == object_idx) && (v->volume_idx() == volume_idx))
        {
            if ((instance_idx != -1) && (v->instance_idx() == instance_idx))
                _add_volume(i);
        }
    }

    _update_type();
    m_bounding_box_dirty = true;
}

void GLCanvas3D::Selection::remove_volume(unsigned int object_idx, unsigned int volume_idx)
{
    if (!m_valid)
        return;

    for (unsigned int i = 0; i < (unsigned int)m_volumes->size(); ++i)
    {
        GLVolume* v = (*m_volumes)[i];
        if ((v->object_idx() == object_idx) && (v->volume_idx() == volume_idx))
            _remove_volume(i);
    }

    _update_type();
    m_bounding_box_dirty = true;
}

void GLCanvas3D::Selection::add_all()
{
    if (!m_valid)
        return;

    m_mode = Instance;
    clear();

    for (unsigned int i = 0; i < (unsigned int)m_volumes->size(); ++i)
    {
        if (!(*m_volumes)[i]->is_wipe_tower)
            _add_volume(i);
    }

    _update_type();
    m_bounding_box_dirty = true;
}

void GLCanvas3D::Selection::clear()
{
    if (!m_valid)
        return;

    for (unsigned int i : m_list)
    {
        (*m_volumes)[i]->selected = false;
    }

    m_list.clear();

    _update_type();
    m_bounding_box_dirty = true;
}

// Update the selection based on the map from old indices to new indices after m_volumes changed.
// If the current selection is by instance, this call may select newly added volumes, if they belong to already selected instances.
void GLCanvas3D::Selection::volumes_changed(const std::vector<size_t> &map_volume_old_to_new)
{
    assert(m_valid);

    // 1) Update the selection set.
    IndicesList list_new;
    std::vector<std::pair<unsigned int, unsigned int>> model_instances;
    for (unsigned int idx : m_list) {
		if (map_volume_old_to_new[idx] != size_t(-1)) {
			unsigned int new_idx = (unsigned int)map_volume_old_to_new[idx];
			list_new.insert(new_idx);
			if (m_mode == Instance) {
                // Save the object_idx / instance_idx pair of selected old volumes,
                // so we may add the newly added volumes of the same object_idx / instance_idx pair
                // to the selection.
				const GLVolume *volume = (*m_volumes)[new_idx];
				model_instances.emplace_back(volume->object_idx(), volume->instance_idx());
			}
        }
    }
	m_list = std::move(list_new);

    if (! model_instances.empty()) {
        // Instance selection mode. Add the newly added volumes of the same object_idx / instance_idx pair
        // to the selection.
        assert(m_mode == Instance);
        sort_remove_duplicates(model_instances);
        for (unsigned int i = 0; i < (unsigned int)m_volumes->size(); ++ i) {
			const GLVolume* volume = (*m_volumes)[i];
            for (const std::pair<int, int> &model_instance : model_instances)
				if (volume->object_idx() == model_instance.first && volume->instance_idx() == model_instance.second)
                    this->_add_volume(i);
        }
    }

    _update_type();
    m_bounding_box_dirty = true;
}

bool GLCanvas3D::Selection::is_single_full_instance() const
{
    if (m_type == SingleFullInstance)
        return true;

    int object_idx = m_valid ? get_object_idx() : -1;
    if ((0 <= object_idx) && (object_idx < (int)m_model->objects.size()))
        return m_model->objects[object_idx]->volumes.size() == m_list.size();

    return false;
}

bool GLCanvas3D::Selection::is_from_single_object() const
{
    int idx = get_object_idx();
    return (0 <= idx) && (idx < 1000);
}

int GLCanvas3D::Selection::get_object_idx() const
{
    return (m_cache.content.size() == 1) ? m_cache.content.begin()->first : -1;
}

int GLCanvas3D::Selection::get_instance_idx() const
{
    if (m_cache.content.size() == 1)
    {
        const InstanceIdxsList& idxs = m_cache.content.begin()->second;
        if (idxs.size() == 1)
            return *idxs.begin();
    }

    return -1;
}

const GLCanvas3D::Selection::InstanceIdxsList& GLCanvas3D::Selection::get_instance_idxs() const
{
    assert(m_cache.content.size() == 1);
    return m_cache.content.begin()->second;
}

const GLVolume* GLCanvas3D::Selection::get_volume(unsigned int volume_idx) const
{
    return (m_valid && (volume_idx < (unsigned int)m_volumes->size())) ? (*m_volumes)[volume_idx] : nullptr;
}

const BoundingBoxf3& GLCanvas3D::Selection::get_bounding_box() const
{
    if (m_bounding_box_dirty)
        _calc_bounding_box();

    return m_bounding_box;
}

void GLCanvas3D::Selection::start_dragging()
{
    if (!m_valid)
        return;

    _set_caches();
}

void GLCanvas3D::Selection::translate(const Vec3d& displacement)
{
    if (!m_valid)
        return;

    for (unsigned int i : m_list)
    {
#if ENABLE_MODELVOLUME_TRANSFORM
        if (m_mode == Instance)
            (*m_volumes)[i]->set_instance_offset(m_cache.volumes_data[i].get_instance_position() + displacement);
        else if (m_mode == Volume)
        {
            Vec3d local_displacement = m_cache.volumes_data[i].get_instance_rotation_matrix().inverse() * displacement;
            (*m_volumes)[i]->set_volume_offset(m_cache.volumes_data[i].get_volume_position() + local_displacement);
        }
#else
        (*m_volumes)[i]->set_offset(m_cache.volumes_data[i].get_position() + displacement);
#endif // ENABLE_MODELVOLUME_TRANSFORM
    }

#if !DISABLE_INSTANCES_SYNCH
    if (m_mode == Instance)
        _synchronize_unselected_instances();
    else if (m_mode == Volume)
        _synchronize_unselected_volumes();
#endif // !DISABLE_INSTANCES_SYNCH

    m_bounding_box_dirty = true;
}

void GLCanvas3D::Selection::rotate(const Vec3d& rotation, bool local)
{
    if (!m_valid)
        return;

    for (unsigned int i : m_list)
    {
        if (is_single_full_instance())
#if ENABLE_WORLD_ROTATIONS
        {
            Transform3d m = Geometry::assemble_transform(Vec3d::Zero(), rotation);
            Vec3d new_rotation = Geometry::extract_euler_angles(m * m_cache.volumes_data[i].get_instance_rotation_matrix());
            (*m_volumes)[i]->set_instance_rotation(new_rotation);
        }
#else
#if ENABLE_MODELVOLUME_TRANSFORM
            (*m_volumes)[i]->set_instance_rotation(rotation);
#else
            (*m_volumes)[i]->set_rotation(rotation);
#endif // ENABLE_MODELVOLUME_TRANSFORM
#endif // ENABLE_WORLD_ROTATIONS
#if ENABLE_MODELVOLUME_TRANSFORM
        else if (is_single_volume() || is_single_modifier())
#if ENABLE_WORLD_ROTATIONS
        {
            Transform3d m = Geometry::assemble_transform(Vec3d::Zero(), rotation);
            Vec3d new_rotation = Geometry::extract_euler_angles(m * m_cache.volumes_data[i].get_volume_rotation_matrix());
            (*m_volumes)[i]->set_volume_rotation(new_rotation);
    }
#else
            (*m_volumes)[i]->set_volume_rotation(rotation);
#endif // ENABLE_WORLD_ROTATIONS
#endif // ENABLE_MODELVOLUME_TRANSFORM
        else
        {
            Transform3d m = Geometry::assemble_transform(Vec3d::Zero(), rotation);
#if ENABLE_MODELVOLUME_TRANSFORM
            if (m_mode == Instance)
            {
                // extracts rotations from the composed transformation
                Vec3d new_rotation = Geometry::extract_euler_angles(m * m_cache.volumes_data[i].get_instance_rotation_matrix());
                if (!local)
                    (*m_volumes)[i]->set_instance_offset(m_cache.dragging_center + m * (m_cache.volumes_data[i].get_instance_position() - m_cache.dragging_center));

                (*m_volumes)[i]->set_instance_rotation(new_rotation);
            }
            else if (m_mode == Volume)
            {
                // extracts rotations from the composed transformation
                Vec3d new_rotation = Geometry::extract_euler_angles(m * m_cache.volumes_data[i].get_volume_rotation_matrix());
                if (!local)
                {
                    Vec3d offset = m * (m_cache.volumes_data[i].get_volume_position() + m_cache.volumes_data[i].get_instance_position() - m_cache.dragging_center);
                    (*m_volumes)[i]->set_volume_offset(m_cache.dragging_center - m_cache.volumes_data[i].get_instance_position() + offset);
                }
                (*m_volumes)[i]->set_volume_rotation(new_rotation);
            }
#else
            // extracts rotations from the composed transformation
            Vec3d new_rotation = Geometry::extract_euler_angles(m * m_cache.volumes_data[i].get_rotation_matrix());
            (*m_volumes)[i]->set_offset(m_cache.dragging_center + m * (m_cache.volumes_data[i].get_position() - m_cache.dragging_center));
            (*m_volumes)[i]->set_rotation(new_rotation);
#endif // ENABLE_MODELVOLUME_TRANSFORM
        }
    }

#if !DISABLE_INSTANCES_SYNCH
    if (m_mode == Instance)
        _synchronize_unselected_instances();
    else if (m_mode == Volume)
        _synchronize_unselected_volumes();
#endif // !DISABLE_INSTANCES_SYNCH

    m_bounding_box_dirty = true;
}

void GLCanvas3D::Selection::flattening_rotate(const Vec3d& normal)
{
    // We get the normal in untransformed coordinates. We must transform it using the instance matrix, find out
    // how to rotate the instance so it faces downwards and do the rotation. All that for all selected instances.
    // The function assumes that is_from_single_object() holds.

    if (!m_valid)
        return;

    for (unsigned int i : m_list)
    {
#if ENABLE_MODELVOLUME_TRANSFORM
        Transform3d wst = m_cache.volumes_data[i].get_instance_scale_matrix() * m_cache.volumes_data[i].get_volume_scale_matrix();
        Vec3d scaling_factor = Vec3d(1./wst(0,0), 1./wst(1,1), 1./wst(2,2));

        Vec3d rotation = Geometry::extract_euler_angles(m_cache.volumes_data[i].get_instance_rotation_matrix() * m_cache.volumes_data[i].get_volume_rotation_matrix());
        Vec3d transformed_normal = Geometry::assemble_transform(Vec3d::Zero(), rotation, scaling_factor) * normal;
        transformed_normal.normalize();

        Vec3d axis = transformed_normal(2) > 0.999f ? Vec3d(1., 0., 0.) : Vec3d(transformed_normal.cross(Vec3d(0., 0., -1.)));
        axis.normalize();

        Transform3d extra_rotation = Transform3d::Identity();
        extra_rotation.rotate(Eigen::AngleAxisd(acos(-transformed_normal(2)), axis));

        Vec3d new_rotation = Geometry::extract_euler_angles(extra_rotation * m_cache.volumes_data[i].get_instance_rotation_matrix() );
        (*m_volumes)[i]->set_instance_rotation(new_rotation);
#else
        Transform3d wst = m_cache.volumes_data[i].get_scale_matrix() * m_cache.volumes_data[i].get_scale_matrix();
        Vec3d scaling_factor = Vec3d(1. / wst(0, 0), 1. / wst(1, 1), 1. / wst(2, 2));

        Vec3d rotation = Geometry::extract_euler_angles(m_cache.volumes_data[i].get_rotation_matrix() * m_cache.volumes_data[i].get_rotation_matrix());
        Vec3d transformed_normal = Geometry::assemble_transform(Vec3d::Zero(), rotation, scaling_factor) * normal;
        transformed_normal.normalize();

        Vec3d axis = transformed_normal(2) > 0.999f ? Vec3d(1., 0., 0.) : Vec3d(transformed_normal.cross(Vec3d(0., 0., -1.)));
        axis.normalize();

        Transform3d extra_rotation = Transform3d::Identity();
        extra_rotation.rotate(Eigen::AngleAxisd(acos(-transformed_normal(2)), axis));

        Vec3d new_rotation = Geometry::extract_euler_angles(extra_rotation * m_cache.volumes_data[i].get_rotation_matrix());
        (*m_volumes)[i]->set_rotation(new_rotation);
#endif // ENABLE_MODELVOLUME_TRANSFORM
    }

#if !DISABLE_INSTANCES_SYNCH
    if (m_mode == Instance)
        _synchronize_unselected_instances();
#endif // !DISABLE_INSTANCES_SYNCH

    m_bounding_box_dirty = true;
}

void GLCanvas3D::Selection::scale(const Vec3d& scale, bool local)
{
    if (!m_valid)
        return;

    for (unsigned int i : m_list)
    {
        if (is_single_full_instance())
#if ENABLE_MODELVOLUME_TRANSFORM
            (*m_volumes)[i]->set_instance_scaling_factor(scale);
#else
            (*m_volumes)[i]->set_scaling_factor(scale);
#endif // ENABLE_MODELVOLUME_TRANSFORM
#if ENABLE_MODELVOLUME_TRANSFORM
        else if (is_single_volume() || is_single_modifier())
            (*m_volumes)[i]->set_volume_scaling_factor(scale);
#endif // ENABLE_MODELVOLUME_TRANSFORM
        else
        {
            Transform3d m = Geometry::assemble_transform(Vec3d::Zero(), Vec3d::Zero(), scale);
#if ENABLE_MODELVOLUME_TRANSFORM
            if (m_mode == Instance)
            {
                Eigen::Matrix<double, 3, 3, Eigen::DontAlign> new_matrix = (m * m_cache.volumes_data[i].get_instance_scale_matrix()).matrix().block(0, 0, 3, 3);
                // extracts scaling factors from the composed transformation
                Vec3d new_scale(new_matrix.col(0).norm(), new_matrix.col(1).norm(), new_matrix.col(2).norm());
                if (!local)
                    (*m_volumes)[i]->set_instance_offset(m_cache.dragging_center + m * (m_cache.volumes_data[i].get_instance_position() - m_cache.dragging_center));

                (*m_volumes)[i]->set_instance_scaling_factor(new_scale);
            }
            else if (m_mode == Volume)
            {
                Eigen::Matrix<double, 3, 3, Eigen::DontAlign> new_matrix = (m * m_cache.volumes_data[i].get_volume_scale_matrix()).matrix().block(0, 0, 3, 3);
                // extracts scaling factors from the composed transformation
                Vec3d new_scale(new_matrix.col(0).norm(), new_matrix.col(1).norm(), new_matrix.col(2).norm());
                if (!local)
                {
                    Vec3d offset = m * (m_cache.volumes_data[i].get_volume_position() + m_cache.volumes_data[i].get_instance_position() - m_cache.dragging_center);
                    std::cout << to_string(offset) << std::endl;
                    (*m_volumes)[i]->set_volume_offset(m_cache.dragging_center - m_cache.volumes_data[i].get_instance_position() + offset);
                }
                (*m_volumes)[i]->set_volume_scaling_factor(new_scale);
            }
#else
            Eigen::Matrix<double, 3, 3, Eigen::DontAlign> new_matrix = (m * m_cache.volumes_data[i].get_scale_matrix()).matrix().block(0, 0, 3, 3);
            // extracts scaling factors from the composed transformation
            Vec3d new_scale(new_matrix.col(0).norm(), new_matrix.col(1).norm(), new_matrix.col(2).norm());
            (*m_volumes)[i]->set_offset(m_cache.dragging_center + m * (m_cache.volumes_data[i].get_position() - m_cache.dragging_center));
            (*m_volumes)[i]->set_scaling_factor(new_scale);
#endif // ENABLE_MODELVOLUME_TRANSFORM
        }
    }

#if !DISABLE_INSTANCES_SYNCH
    if (m_mode == Instance)
        _synchronize_unselected_instances();
    else if (m_mode == Volume)
        _synchronize_unselected_volumes();
#endif // !DISABLE_INSTANCES_SYNCH

#if ENABLE_ENSURE_ON_BED_WHILE_SCALING
    _ensure_on_bed();
#endif // ENABLE_ENSURE_ON_BED_WHILE_SCALING

    m_bounding_box_dirty = true;
}

void GLCanvas3D::Selection::mirror(Axis axis)
{
    if (!m_valid)
        return;

    bool single_full_instance = is_single_full_instance();

    for (unsigned int i : m_list)
    {
        if (single_full_instance)
#if ENABLE_MODELVOLUME_TRANSFORM
            (*m_volumes)[i]->set_instance_mirror(axis, -(*m_volumes)[i]->get_instance_mirror(axis));
        else if (m_mode == Volume)
            (*m_volumes)[i]->set_volume_mirror(axis, -(*m_volumes)[i]->get_volume_mirror(axis));
#else
            (*m_volumes)[i]->set_mirror(axis, -(*m_volumes)[i]->get_mirror(axis));
#endif // ENABLE_MODELVOLUME_TRANSFORM
    }

#if !DISABLE_INSTANCES_SYNCH
    if (m_mode == Instance)
        _synchronize_unselected_instances();
    else if (m_mode == Volume)
        _synchronize_unselected_volumes();
#endif // !DISABLE_INSTANCES_SYNCH

    m_bounding_box_dirty = true;
}

void GLCanvas3D::Selection::translate(unsigned int object_idx, const Vec3d& displacement)
{
    if (!m_valid)
        return;

    for (unsigned int i : m_list)
    {
        GLVolume* v = (*m_volumes)[i];
        if (v->object_idx() == object_idx)
#if ENABLE_MODELVOLUME_TRANSFORM
            v->set_instance_offset(v->get_instance_offset() + displacement);
#else
            v->set_offset(v->get_offset() + displacement);
#endif // ENABLE_MODELVOLUME_TRANSFORM
    }

    std::set<unsigned int> done;  // prevent processing volumes twice
    done.insert(m_list.begin(), m_list.end());

    for (unsigned int i : m_list)
    {
        if (done.size() == m_volumes->size())
            break;

        int object_idx = (*m_volumes)[i]->object_idx();
        if (object_idx >= 1000)
            continue;

        // Process unselected volumes of the object.
        for (unsigned int j = 0; j < (unsigned int)m_volumes->size(); ++j)
        {
            if (done.size() == m_volumes->size())
                break;

            if (done.find(j) != done.end())
                continue;

            GLVolume* v = (*m_volumes)[j];
            if (v->object_idx() != object_idx)
                continue;

#if ENABLE_MODELVOLUME_TRANSFORM
            v->set_instance_offset(v->get_instance_offset() + displacement);
#else
            v->set_offset(v->get_offset() + displacement);
#endif // ENABLE_MODELVOLUME_TRANSFORM
            done.insert(j);
        }
    }

    m_bounding_box_dirty = true;
}

void GLCanvas3D::Selection::translate(unsigned int object_idx, unsigned int instance_idx, const Vec3d& displacement)
{
    if (!m_valid)
        return;

    for (unsigned int i : m_list)
    {
        GLVolume* v = (*m_volumes)[i];
        if ((v->object_idx() == object_idx) && (v->instance_idx() == instance_idx))
#if ENABLE_MODELVOLUME_TRANSFORM
            v->set_instance_offset(v->get_instance_offset() + displacement);
#else
            v->set_offset(v->get_offset() + displacement);
#endif // ENABLE_MODELVOLUME_TRANSFORM
    }

    std::set<unsigned int> done;  // prevent processing volumes twice
    done.insert(m_list.begin(), m_list.end());

    for (unsigned int i : m_list)
    {
        if (done.size() == m_volumes->size())
            break;

        int object_idx = (*m_volumes)[i]->object_idx();
        if (object_idx >= 1000)
            continue;

        // Process unselected volumes of the object.
        for (unsigned int j = 0; j < (unsigned int)m_volumes->size(); ++j)
        {
            if (done.size() == m_volumes->size())
                break;

            if (done.find(j) != done.end())
                continue;

            GLVolume* v = (*m_volumes)[j];
            if ((v->object_idx() != object_idx) || (v->instance_idx() != instance_idx))
                continue;

#if ENABLE_MODELVOLUME_TRANSFORM
            v->set_instance_offset(v->get_instance_offset() + displacement);
#else
            v->set_offset(v->get_offset() + displacement);
#endif // ENABLE_MODELVOLUME_TRANSFORM
            done.insert(j);
        }
    }

    m_bounding_box_dirty = true;
}

void GLCanvas3D::Selection::erase()
{
    if (!m_valid)
        return;

    if (is_single_full_object())
        wxGetApp().obj_list()->delete_from_model_and_list(ItemType::itObject, get_object_idx(), 0);
    else if (is_multiple_full_object())
    {
        std::vector<ItemForDelete> items;
        items.reserve(m_cache.content.size());
        for (ObjectIdxsToInstanceIdxsMap::iterator it = m_cache.content.begin(); it != m_cache.content.end(); ++it)
        {
            items.emplace_back(ItemType::itObject, it->first, 0);
        }
        wxGetApp().obj_list()->delete_from_model_and_list(items);
    }
    else if (is_single_full_instance())
        wxGetApp().obj_list()->delete_from_model_and_list(ItemType::itInstance, get_object_idx(), get_instance_idx());
    else if (is_multiple_full_instance())
    {
        std::set<std::pair<int, int>> instances_idxs;
        for (ObjectIdxsToInstanceIdxsMap::iterator obj_it = m_cache.content.begin(); obj_it != m_cache.content.end(); ++obj_it)
        {
            for (InstanceIdxsList::reverse_iterator inst_it = obj_it->second.rbegin(); inst_it != obj_it->second.rend(); ++inst_it)
            {
                instances_idxs.insert(std::make_pair(obj_it->first, *inst_it));
            }
        }

        std::vector<ItemForDelete> items;
        items.reserve(instances_idxs.size());
        for (const std::pair<int, int>& i : instances_idxs)
        {
            items.emplace_back(ItemType::itInstance, i.first, i.second);
        }

        wxGetApp().obj_list()->delete_from_model_and_list(items);
    }
    else
    {
        std::set<std::pair<int, int>> volumes_idxs;
        for (unsigned int i : m_list)
        {
            const GLVolume* v = (*m_volumes)[i];
			// Only remove volumes associated with ModelVolumes from the object list.
			// Temporary meshes (SLA supports or pads) are not managed by the object list.
			if (v->volume_idx() >= 0)
	            volumes_idxs.insert(std::make_pair(v->object_idx(), v->volume_idx()));
        }

        std::vector<ItemForDelete> items;
        items.reserve(volumes_idxs.size());
        for (const std::pair<int, int>& v : volumes_idxs)
        {
            items.emplace_back(ItemType::itVolume, v.first, v.second);
        }

        wxGetApp().obj_list()->delete_from_model_and_list(items);
    }
}

void GLCanvas3D::Selection::render() const
{
    if (is_empty())
        return;

    // render cumulative bounding box of selected volumes
    _render_selected_volumes();
    _render_synchronized_volumes();
}

void GLCanvas3D::Selection::_update_valid()
{
    m_valid = (m_volumes != nullptr) && (m_model != nullptr);
}

void GLCanvas3D::Selection::_update_type()
{
    m_cache.content.clear();
    m_type = Mixed;

    for (unsigned int i : m_list)
    {
        const GLVolume* volume = (*m_volumes)[i];
        int obj_idx = volume->object_idx();
        int inst_idx = volume->instance_idx();
        ObjectIdxsToInstanceIdxsMap::iterator obj_it = m_cache.content.find(obj_idx);
        if (obj_it == m_cache.content.end())
            obj_it = m_cache.content.insert(ObjectIdxsToInstanceIdxsMap::value_type(obj_idx, InstanceIdxsList())).first;

        obj_it->second.insert(inst_idx);
    }

    bool requires_disable = false;

    if (!m_valid)
        m_type = Invalid;
    else
    {
        if (m_list.empty())
            m_type = Empty;
        else if (m_list.size() == 1)
        {
            const GLVolume* first = (*m_volumes)[*m_list.begin()];
            if (first->is_wipe_tower)
                m_type = WipeTower;
            else if (first->is_modifier)
            {
                m_type = SingleModifier;
                requires_disable = true;
            }
            else
            {
                const ModelObject* model_object = m_model->objects[first->object_idx()];
                unsigned int volumes_count = (unsigned int)model_object->volumes.size();
                unsigned int instances_count = (unsigned int)model_object->instances.size();
                if (volumes_count * instances_count == 1)
                {
                    m_type = SingleFullObject;
                    // ensures the correct mode is selected
                    m_mode = Instance;
                }
                else if (volumes_count == 1) // instances_count > 1
                {
                    m_type = SingleFullInstance;
                    // ensures the correct mode is selected
                    m_mode = Instance;
                }
                else
                {
                    m_type = SingleVolume;
                    requires_disable = true;
                }
            }
        }
        else
        {
            if (m_cache.content.size() == 1) // single object
            {
                const ModelObject* model_object = m_model->objects[m_cache.content.begin()->first];
                unsigned int model_volumes_count = (unsigned int)model_object->volumes.size();
                unsigned int sla_volumes_count = 0;
                for (unsigned int i : m_list)
                {
                    if ((*m_volumes)[i]->volume_idx() < 0)
                        ++sla_volumes_count;
                }
                unsigned int volumes_count = model_volumes_count + sla_volumes_count;
                unsigned int instances_count = (unsigned int)model_object->instances.size();
                unsigned int selected_instances_count = (unsigned int)m_cache.content.begin()->second.size();
                if (volumes_count * instances_count == (unsigned int)m_list.size())
                {
                    m_type = SingleFullObject;
                    // ensures the correct mode is selected
                    m_mode = Instance;
                }
                else if (selected_instances_count == 1)
                {
                    if (volumes_count == (unsigned int)m_list.size())
                    {
                        m_type = SingleFullInstance;
                        // ensures the correct mode is selected
                        m_mode = Instance;
                    }
                    else
                    {
                        unsigned int modifiers_count = 0;
                        for (unsigned int i : m_list)
                        {
                            if ((*m_volumes)[i]->is_modifier)
                                ++modifiers_count;
                        }

                        if (modifiers_count == 0)
                        {
                            m_type = MultipleVolume;
                            requires_disable = true;
                        }
                        else if (modifiers_count == (unsigned int)m_list.size())
                        {
                            m_type = MultipleModifier;
                            requires_disable = true;
                        }
                    }
                }
                else if ((selected_instances_count > 1) && (selected_instances_count * volumes_count == (unsigned int)m_list.size()))
                {
                    m_type = MultipleFullInstance;
                    // ensures the correct mode is selected
                    m_mode = Instance;
                }
            }
            else
            {
                int sels_cntr = 0;
                for (ObjectIdxsToInstanceIdxsMap::iterator it = m_cache.content.begin(); it != m_cache.content.end(); ++it)
                {
                    const ModelObject* model_object = m_model->objects[it->first];
                    unsigned int volumes_count = (unsigned int)model_object->volumes.size();
                    unsigned int instances_count = (unsigned int)model_object->instances.size();
                        sels_cntr += volumes_count * instances_count;
                }
                if (sels_cntr == (unsigned int)m_list.size())
                {
                    m_type = MultipleFullObject;
                    // ensures the correct mode is selected
                    m_mode = Instance;
                }
            }
        }
    }

    int object_idx = get_object_idx();
    int instance_idx = get_instance_idx();
    for (GLVolume* v : *m_volumes)
    {
        v->disabled = requires_disable ? (v->object_idx() != object_idx) || (v->instance_idx() != instance_idx) : false;
    }

    std::cout << "Selection: ";
    std::cout << "mode: ";
    switch (m_mode)
    {
    case Volume:
    {
        std::cout << "Volume";
        break;
    }
    case Instance:
    {
        std::cout << "Instance";
        break;
    }
    }

    std::cout << " - type: ";

    switch (m_type)
    {
    case Invalid:
    {
        std::cout << "Invalid" << std::endl;
        break;
    }
    case Empty:
    {
        std::cout << "Empty" << std::endl;
        break;
    }
    case WipeTower:
    {
        std::cout << "WipeTower" << std::endl;
        break;
    }
    case SingleModifier:
    {
        std::cout << "SingleModifier" << std::endl;
        break;
    }
    case MultipleModifier:
    {
        std::cout << "MultipleModifier" << std::endl;
        break;
    }
    case SingleVolume:
    {
        std::cout << "SingleVolume" << std::endl;
        break;
    }
    case MultipleVolume:
    {
        std::cout << "MultipleVolume" << std::endl;
        break;
    }
    case SingleFullObject:
    {
        std::cout << "SingleFullObject" << std::endl;
        break;
    }
    case MultipleFullObject:
    {
        std::cout << "MultipleFullObject" << std::endl;
        break;
    }
    case SingleFullInstance:
    {
        std::cout << "SingleFullInstance" << std::endl;
        break;
    }
    case MultipleFullInstance:
    {
        std::cout << "MultipleFullInstance" << std::endl;
        break;
    }
    case Mixed:
    {
        std::cout << "Mixed" << std::endl;
        break;
    }
    }
}

void GLCanvas3D::Selection::_set_caches()
{
    m_cache.volumes_data.clear();
    for (unsigned int i : m_list)
    {
        const GLVolume* v = (*m_volumes)[i];
#if ENABLE_MODELVOLUME_TRANSFORM
        m_cache.volumes_data.emplace(i, VolumeCache(v->get_volume_transformation(), v->get_instance_transformation()));
#else
        m_cache.volumes_data.emplace(i, VolumeCache(v->get_offset(), v->get_rotation(), v->get_scaling_factor()));
#endif // ENABLE_MODELVOLUME_TRANSFORM
    }
    m_cache.dragging_center = get_bounding_box().center();
}

void GLCanvas3D::Selection::_add_volume(unsigned int volume_idx)
{
    m_list.insert(volume_idx);
    (*m_volumes)[volume_idx]->selected = true;
}

void GLCanvas3D::Selection::_add_instance(unsigned int object_idx, unsigned int instance_idx)
{
    for (unsigned int i = 0; i < (unsigned int)m_volumes->size(); ++i)
    {
        GLVolume* v = (*m_volumes)[i];
        if ((v->object_idx() == object_idx) && (v->instance_idx() == instance_idx))
            _add_volume(i);
    }
}

void GLCanvas3D::Selection::_add_object(unsigned int object_idx)
{
    for (unsigned int i = 0; i < (unsigned int)m_volumes->size(); ++i)
    {
        GLVolume* v = (*m_volumes)[i];
        if (v->object_idx() == object_idx)
            _add_volume(i);
    }
}

void GLCanvas3D::Selection::_remove_volume(unsigned int volume_idx)
{
    IndicesList::iterator v_it = m_list.find(volume_idx);
    if (v_it == m_list.end())
        return;

    m_list.erase(v_it);

    (*m_volumes)[volume_idx]->selected = false;
}

void GLCanvas3D::Selection::_remove_instance(unsigned int object_idx, unsigned int instance_idx)
{
    for (unsigned int i = 0; i < (unsigned int)m_volumes->size(); ++i)
    {
        GLVolume* v = (*m_volumes)[i];
        if ((v->object_idx() == object_idx) && (v->instance_idx() == instance_idx))
            _remove_volume(i);
    }
}

void GLCanvas3D::Selection::_remove_object(unsigned int object_idx)
{
    for (unsigned int i = 0; i < (unsigned int)m_volumes->size(); ++i)
    {
        GLVolume* v = (*m_volumes)[i];
        if (v->object_idx() == object_idx)
            _remove_volume(i);
    }
}

void GLCanvas3D::Selection::_calc_bounding_box() const
{
    m_bounding_box = BoundingBoxf3();
    if (m_valid)
    {
        for (unsigned int i : m_list)
        {
            m_bounding_box.merge((*m_volumes)[i]->transformed_convex_hull_bounding_box());
        }
    }
    m_bounding_box_dirty = false;
}

void GLCanvas3D::Selection::_render_selected_volumes() const
{
    float color[3] = { 1.0f, 1.0f, 1.0f };
    _render_bounding_box(get_bounding_box(), color);
}

void GLCanvas3D::Selection::_render_synchronized_volumes() const
{
    if (m_mode == Instance)
        return;

    float color[3] = { 1.0f, 1.0f, 0.0f };

    for (unsigned int i : m_list)
    {
        const GLVolume* volume = (*m_volumes)[i];
        int object_idx = volume->object_idx();
        int instance_idx = volume->instance_idx();
        int volume_idx = volume->volume_idx();
        for (unsigned int j = 0; j < (unsigned int)m_volumes->size(); ++j)
        {
            if (i == j)
                continue;

            const GLVolume* v = (*m_volumes)[j];
            if ((v->object_idx() != object_idx) || (v->volume_idx() != volume_idx))
                continue;

            _render_bounding_box(v->transformed_convex_hull_bounding_box(), color);
        }
    }
}

void GLCanvas3D::Selection::_render_bounding_box(const BoundingBoxf3& box, float* color) const
{
    if (color == nullptr)
        return;

    Vec3f b_min = box.min.cast<float>();
    Vec3f b_max = box.max.cast<float>();
    Vec3f size = 0.2f * box.size().cast<float>();

    ::glEnable(GL_DEPTH_TEST);

    ::glColor3fv(color);
    ::glLineWidth(2.0f);

    ::glBegin(GL_LINES);

    ::glVertex3f(b_min(0), b_min(1), b_min(2)); ::glVertex3f(b_min(0) + size(0), b_min(1), b_min(2));
    ::glVertex3f(b_min(0), b_min(1), b_min(2)); ::glVertex3f(b_min(0), b_min(1) + size(1), b_min(2));
    ::glVertex3f(b_min(0), b_min(1), b_min(2)); ::glVertex3f(b_min(0), b_min(1), b_min(2) + size(2));

    ::glVertex3f(b_max(0), b_min(1), b_min(2)); ::glVertex3f(b_max(0) - size(0), b_min(1), b_min(2));
    ::glVertex3f(b_max(0), b_min(1), b_min(2)); ::glVertex3f(b_max(0), b_min(1) + size(1), b_min(2));
    ::glVertex3f(b_max(0), b_min(1), b_min(2)); ::glVertex3f(b_max(0), b_min(1), b_min(2) + size(2));

    ::glVertex3f(b_max(0), b_max(1), b_min(2)); ::glVertex3f(b_max(0) - size(0), b_max(1), b_min(2));
    ::glVertex3f(b_max(0), b_max(1), b_min(2)); ::glVertex3f(b_max(0), b_max(1) - size(1), b_min(2));
    ::glVertex3f(b_max(0), b_max(1), b_min(2)); ::glVertex3f(b_max(0), b_max(1), b_min(2) + size(2));

    ::glVertex3f(b_min(0), b_max(1), b_min(2)); ::glVertex3f(b_min(0) + size(0), b_max(1), b_min(2));
    ::glVertex3f(b_min(0), b_max(1), b_min(2)); ::glVertex3f(b_min(0), b_max(1) - size(1), b_min(2));
    ::glVertex3f(b_min(0), b_max(1), b_min(2)); ::glVertex3f(b_min(0), b_max(1), b_min(2) + size(2));

    ::glVertex3f(b_min(0), b_min(1), b_max(2)); ::glVertex3f(b_min(0) + size(0), b_min(1), b_max(2));
    ::glVertex3f(b_min(0), b_min(1), b_max(2)); ::glVertex3f(b_min(0), b_min(1) + size(1), b_max(2));
    ::glVertex3f(b_min(0), b_min(1), b_max(2)); ::glVertex3f(b_min(0), b_min(1), b_max(2) - size(2));

    ::glVertex3f(b_max(0), b_min(1), b_max(2)); ::glVertex3f(b_max(0) - size(0), b_min(1), b_max(2));
    ::glVertex3f(b_max(0), b_min(1), b_max(2)); ::glVertex3f(b_max(0), b_min(1) + size(1), b_max(2));
    ::glVertex3f(b_max(0), b_min(1), b_max(2)); ::glVertex3f(b_max(0), b_min(1), b_max(2) - size(2));

    ::glVertex3f(b_max(0), b_max(1), b_max(2)); ::glVertex3f(b_max(0) - size(0), b_max(1), b_max(2));
    ::glVertex3f(b_max(0), b_max(1), b_max(2)); ::glVertex3f(b_max(0), b_max(1) - size(1), b_max(2));
    ::glVertex3f(b_max(0), b_max(1), b_max(2)); ::glVertex3f(b_max(0), b_max(1), b_max(2) - size(2));

    ::glVertex3f(b_min(0), b_max(1), b_max(2)); ::glVertex3f(b_min(0) + size(0), b_max(1), b_max(2));
    ::glVertex3f(b_min(0), b_max(1), b_max(2)); ::glVertex3f(b_min(0), b_max(1) - size(1), b_max(2));
    ::glVertex3f(b_min(0), b_max(1), b_max(2)); ::glVertex3f(b_min(0), b_max(1), b_max(2) - size(2));

    ::glEnd();
}

void GLCanvas3D::Selection::_synchronize_unselected_instances()
{
    std::set<unsigned int> done;  // prevent processing volumes twice
    done.insert(m_list.begin(), m_list.end());

    for (unsigned int i : m_list)
    {
        if (done.size() == m_volumes->size())
            break;

        const GLVolume* volume = (*m_volumes)[i];
        int object_idx = volume->object_idx();
        if (object_idx >= 1000)
            continue;

        int instance_idx = volume->instance_idx();
#if ENABLE_MODELVOLUME_TRANSFORM
        const Vec3d& rotation = volume->get_instance_rotation();
        const Vec3d& scaling_factor = volume->get_instance_scaling_factor();
        const Vec3d& mirror = volume->get_instance_mirror();
#else
        const Vec3d& rotation = volume->get_rotation();
        const Vec3d& scaling_factor = volume->get_scaling_factor();
        const Vec3d& mirror = volume->get_mirror();
#endif // ENABLE_MODELVOLUME_TRANSFORM

        // Process unselected instances.
        for (unsigned int j = 0; j < (unsigned int)m_volumes->size(); ++j)
        {
            if (done.size() == m_volumes->size())
                break;

            if (done.find(j) != done.end())
                continue;

            GLVolume* v = (*m_volumes)[j];
            if ((v->object_idx() != object_idx) || (v->instance_idx() == instance_idx))
                continue;

#if ENABLE_MODELVOLUME_TRANSFORM
            v->set_instance_rotation(Vec3d(rotation(0), rotation(1), v->get_instance_rotation()(2)));
            v->set_instance_scaling_factor(scaling_factor);
            v->set_instance_mirror(mirror);
#else
            v->set_rotation(Vec3d(rotation(0), rotation(1), v->get_rotation()(2)));
            v->set_scaling_factor(scaling_factor);
            v->set_mirror(mirror);
#endif // ENABLE_MODELVOLUME_TRANSFORM

            done.insert(j);
        }
    }
}

void GLCanvas3D::Selection::_synchronize_unselected_volumes()
{
    for (unsigned int i : m_list)
    {
        const GLVolume* volume = (*m_volumes)[i];
        int object_idx = volume->object_idx();
        if (object_idx >= 1000)
            continue;

        int volume_idx = volume->volume_idx();
#if ENABLE_MODELVOLUME_TRANSFORM
        const Vec3d& offset = volume->get_volume_offset();
        const Vec3d& rotation = volume->get_volume_rotation();
        const Vec3d& scaling_factor = volume->get_volume_scaling_factor();
        const Vec3d& mirror = volume->get_volume_mirror();
#else
        const Vec3d& offset = volume->get_offset();
        const Vec3d& rotation = volume->get_rotation();
        const Vec3d& scaling_factor = volume->get_scaling_factor();
        const Vec3d& mirror = volume->get_mirror();
#endif // ENABLE_MODELVOLUME_TRANSFORM

        // Process unselected volumes.
        for (unsigned int j = 0; j < (unsigned int)m_volumes->size(); ++j)
        {
            if (j == i)
                continue;

            GLVolume* v = (*m_volumes)[j];
            if ((v->object_idx() != object_idx) || (v->volume_idx() != volume_idx))
                continue;

#if ENABLE_MODELVOLUME_TRANSFORM
            v->set_volume_offset(offset);
            v->set_volume_rotation(rotation);
            v->set_volume_scaling_factor(scaling_factor);
            v->set_volume_mirror(mirror);
#else
            v->set_offset(offset);
            v->set_rotation(Vec3d(rotation));
            v->set_scaling_factor(scaling_factor);
            v->set_mirror(mirror);
#endif // ENABLE_MODELVOLUME_TRANSFORM
        }
    }
}

#if ENABLE_ENSURE_ON_BED_WHILE_SCALING
void GLCanvas3D::Selection::_ensure_on_bed()
{
    typedef std::map<std::pair<int, int>, double> InstancesToZMap;
    InstancesToZMap instances_min_z;

    for (unsigned int i : m_list)
    {
        GLVolume* volume = (*m_volumes)[i];
        if (!volume->is_modifier)
        {
            double min_z = volume->transformed_convex_hull_bounding_box().min(2);
            std::pair<int, int> instance = std::make_pair(volume->object_idx(), volume->instance_idx());
            InstancesToZMap::iterator it = instances_min_z.find(instance);
            if (it == instances_min_z.end())
                it = instances_min_z.insert(InstancesToZMap::value_type(instance, DBL_MAX)).first;

            it->second = std::min(it->second, min_z);
        }
    }

    for (GLVolume* volume : *m_volumes)
    {
        std::pair<int, int> instance = std::make_pair(volume->object_idx(), volume->instance_idx());
        InstancesToZMap::iterator it = instances_min_z.find(instance);
        if (it != instances_min_z.end())
            volume->set_instance_offset(Z, volume->get_instance_offset(Z) - it->second);
    }
}
#endif // ENABLE_ENSURE_ON_BED_WHILE_SCALING

const float GLCanvas3D::Gizmos::OverlayTexturesScale = 0.75f;
const float GLCanvas3D::Gizmos::OverlayOffsetX = 10.0f * OverlayTexturesScale;
const float GLCanvas3D::Gizmos::OverlayGapY = 5.0f * OverlayTexturesScale;

GLCanvas3D::Gizmos::Gizmos()
    : m_enabled(false)
    , m_current(Undefined)
{
}

GLCanvas3D::Gizmos::~Gizmos()
{
    _reset();
}

bool GLCanvas3D::Gizmos::init(GLCanvas3D& parent)
{
    GLGizmoBase* gizmo = new GLGizmoMove3D(parent);
    if (gizmo == nullptr)
        return false;

    if (!gizmo->init())
        return false;

    m_gizmos.insert(GizmosMap::value_type(Move, gizmo));

    gizmo = new GLGizmoScale3D(parent);
    if (gizmo == nullptr)
        return false;

    if (!gizmo->init())
        return false;

    m_gizmos.insert(GizmosMap::value_type(Scale, gizmo));

    gizmo = new GLGizmoRotate3D(parent);
    if (gizmo == nullptr)
    {
        _reset();
        return false;
    }

    if (!gizmo->init())
    {
        _reset();
        return false;
    }

    m_gizmos.insert(GizmosMap::value_type(Rotate, gizmo));

    gizmo = new GLGizmoFlatten(parent);
    if (gizmo == nullptr)
        return false;

    if (!gizmo->init()) {
        _reset();
        return false;
    }

    m_gizmos.insert(GizmosMap::value_type(Flatten, gizmo));

    gizmo = new GLGizmoCut(parent);
    if (gizmo == nullptr)
        return false;

    if (!gizmo->init()) {
        _reset();
        return false;
    }

    m_gizmos.insert(GizmosMap::value_type(Cut, gizmo));

    gizmo = new GLGizmoSlaSupports(parent);
    if (gizmo == nullptr)
        return false;

    if (!gizmo->init()) {
        _reset();
        return false;
    }

    m_gizmos.insert(GizmosMap::value_type(SlaSupports, gizmo));

    return true;
}

bool GLCanvas3D::Gizmos::is_enabled() const
{
    return m_enabled;
}

void GLCanvas3D::Gizmos::set_enabled(bool enable)
{
    m_enabled = enable;
}

std::string GLCanvas3D::Gizmos::update_hover_state(const GLCanvas3D& canvas, const Vec2d& mouse_pos, const GLCanvas3D::Selection& selection)
{
    std::string name = "";

    if (!m_enabled)
        return name;

    float cnv_h = (float)canvas.get_canvas_size().get_height();
    float height = _get_total_overlay_height();
    float top_y = 0.5f * (cnv_h - height);
    for (GizmosMap::iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if ((it->second == nullptr) || !it->second->is_selectable())
            continue;

        float tex_size = (float)it->second->get_textures_size() * OverlayTexturesScale;
        float half_tex_size = 0.5f * tex_size;

        // we currently use circular icons for gizmo, so we check the radius
        if (it->second->is_activable(selection) && (it->second->get_state() != GLGizmoBase::On))
        {
            bool inside = (mouse_pos - Vec2d(OverlayOffsetX + half_tex_size, top_y + half_tex_size)).norm() < half_tex_size;
            it->second->set_state(inside ? GLGizmoBase::Hover : GLGizmoBase::Off);
            if (inside)
                name = it->second->get_name();
        }
        top_y += (tex_size + OverlayGapY);
    }

    return name;
}

void GLCanvas3D::Gizmos::update_on_off_state(const GLCanvas3D& canvas, const Vec2d& mouse_pos, const GLCanvas3D::Selection& selection)
{
    if (!m_enabled)
        return;

    float cnv_h = (float)canvas.get_canvas_size().get_height();
    float height = _get_total_overlay_height();
    float top_y = 0.5f * (cnv_h - height);
    for (GizmosMap::iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if ((it->second == nullptr) || !it->second->is_selectable())
            continue;

        float tex_size = (float)it->second->get_textures_size() * OverlayTexturesScale;
        float half_tex_size = 0.5f * tex_size;

        // we currently use circular icons for gizmo, so we check the radius
        if (it->second->is_activable(selection) && ((mouse_pos - Vec2d(OverlayOffsetX + half_tex_size, top_y + half_tex_size)).norm() < half_tex_size))
        {
            if ((it->second->get_state() == GLGizmoBase::On))
            {
                it->second->set_state(GLGizmoBase::Hover);
                m_current = Undefined;
            }
            else if ((it->second->get_state() == GLGizmoBase::Hover))
            {
                it->second->set_state(GLGizmoBase::On);
                m_current = it->first;
            }
        }
        else
            it->second->set_state(GLGizmoBase::Off);

        top_y += (tex_size + OverlayGapY);
    }

    GizmosMap::iterator it = m_gizmos.find(m_current);
    if ((it != m_gizmos.end()) && (it->second != nullptr) && (it->second->get_state() != GLGizmoBase::On))
        it->second->set_state(GLGizmoBase::On);
}

void GLCanvas3D::Gizmos::update_on_off_state(const Selection& selection)
{
    GizmosMap::iterator it = m_gizmos.find(m_current);
    if ((it != m_gizmos.end()) && (it->second != nullptr))
    {
        if (!it->second->is_activable(selection))
        {
            it->second->set_state(GLGizmoBase::Off);
            m_current = Undefined;
        }
    }
}

void GLCanvas3D::Gizmos::reset_all_states()
{
    if (!m_enabled)
        return;

    for (GizmosMap::const_iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if (it->second != nullptr)
        {
            it->second->set_state(GLGizmoBase::Off);
            it->second->set_hover_id(-1);
        }
    }

    m_current = Undefined;
}

void GLCanvas3D::Gizmos::set_hover_id(int id)
{
    if (!m_enabled)
        return;

    for (GizmosMap::const_iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if ((it->second != nullptr) && (it->second->get_state() == GLGizmoBase::On))
            it->second->set_hover_id(id);
    }
}

void GLCanvas3D::Gizmos::enable_grabber(EType type, unsigned int id, bool enable)
{
    if (!m_enabled)
        return;

    GizmosMap::const_iterator it = m_gizmos.find(type);
    if (it != m_gizmos.end())
    {
        if (enable)
            it->second->enable_grabber(id);
        else
            it->second->disable_grabber(id);
    }
}

bool GLCanvas3D::Gizmos::overlay_contains_mouse(const GLCanvas3D& canvas, const Vec2d& mouse_pos) const
{
    if (!m_enabled)
        return false;

    float cnv_h = (float)canvas.get_canvas_size().get_height();
    float height = _get_total_overlay_height();
    float top_y = 0.5f * (cnv_h - height);
    for (GizmosMap::const_iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if ((it->second == nullptr) || !it->second->is_selectable())
            continue;

        float tex_size = (float)it->second->get_textures_size() * OverlayTexturesScale;
        float half_tex_size = 0.5f * tex_size;

        // we currently use circular icons for gizmo, so we check the radius
        if ((mouse_pos - Vec2d(OverlayOffsetX + half_tex_size, top_y + half_tex_size)).norm() < half_tex_size)
            return true;

        top_y += (tex_size + OverlayGapY);
    }

    return false;
}

bool GLCanvas3D::Gizmos::grabber_contains_mouse() const
{
    if (!m_enabled)
        return false;

    GLGizmoBase* curr = _get_current();
    return (curr != nullptr) ? (curr->get_hover_id() != -1) : false;
}

void GLCanvas3D::Gizmos::update(const Linef3& mouse_ray, bool shift_down, const Point* mouse_pos)
{
    if (!m_enabled)
        return;

    GLGizmoBase* curr = _get_current();
    if (curr != nullptr)
        curr->update(GLGizmoBase::UpdateData(mouse_ray, mouse_pos, shift_down));
}

#if ENABLE_GIZMOS_RESET
void GLCanvas3D::Gizmos::process_double_click()
{
    if (!m_enabled)
        return;

    GLGizmoBase* curr = _get_current();
    if (curr != nullptr)
        curr->process_double_click();
}
#endif // ENABLE_GIZMOS_RESET

GLCanvas3D::Gizmos::EType GLCanvas3D::Gizmos::get_current_type() const
{
    return m_current;
}

bool GLCanvas3D::Gizmos::is_running() const
{
    if (!m_enabled)
        return false;

    GLGizmoBase* curr = _get_current();
    return (curr != nullptr) ? (curr->get_state() == GLGizmoBase::On) : false;
}

#if ENABLE_GIZMOS_SHORTCUT
bool GLCanvas3D::Gizmos::handle_shortcut(int key, const Selection& selection)
{
    if (!m_enabled)
        return false;

    bool handled = false;
    for (GizmosMap::iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if ((it->second == nullptr) || !it->second->is_selectable())
            continue;

        int it_key = it->second->get_shortcut_key();

        if (it->second->is_activable(selection) && ((it_key == key - 64) || (it_key == key - 96)))
        {
            if ((it->second->get_state() == GLGizmoBase::On))
            {
                it->second->set_state(GLGizmoBase::Off);
                m_current = Undefined;
                handled = true;
            }
            else if ((it->second->get_state() == GLGizmoBase::Off))
            {
                it->second->set_state(GLGizmoBase::On);
                m_current = it->first;
                handled = true;
            }
        }
        else
            it->second->set_state(GLGizmoBase::Off);
    }

    return handled;
}
#endif // ENABLE_GIZMOS_SHORTCUT

bool GLCanvas3D::Gizmos::is_dragging() const
{
    if (!m_enabled)
        return false;

    GLGizmoBase* curr = _get_current();
    return (curr != nullptr) ? curr->is_dragging() : false;
}

void GLCanvas3D::Gizmos::start_dragging(const GLCanvas3D::Selection& selection)
{
    if (!m_enabled)
        return;

    GLGizmoBase* curr = _get_current();
    if (curr != nullptr)
        curr->start_dragging(selection);
}

void GLCanvas3D::Gizmos::stop_dragging()
{
    if (!m_enabled)
        return;

    GLGizmoBase* curr = _get_current();
    if (curr != nullptr)
        curr->stop_dragging();
}

Vec3d GLCanvas3D::Gizmos::get_displacement() const
{
    if (!m_enabled)
        return Vec3d::Zero();

    GizmosMap::const_iterator it = m_gizmos.find(Move);
    return (it != m_gizmos.end()) ? reinterpret_cast<GLGizmoMove3D*>(it->second)->get_displacement() : Vec3d::Zero();
}

Vec3d GLCanvas3D::Gizmos::get_scale() const
{
    if (!m_enabled)
        return Vec3d::Ones();

    GizmosMap::const_iterator it = m_gizmos.find(Scale);
    return (it != m_gizmos.end()) ? reinterpret_cast<GLGizmoScale3D*>(it->second)->get_scale() : Vec3d::Ones();
}

void GLCanvas3D::Gizmos::set_scale(const Vec3d& scale)
{
    if (!m_enabled)
        return;

    GizmosMap::const_iterator it = m_gizmos.find(Scale);
    if (it != m_gizmos.end())
        reinterpret_cast<GLGizmoScale3D*>(it->second)->set_scale(scale);
}

Vec3d GLCanvas3D::Gizmos::get_rotation() const
{
    if (!m_enabled)
        return Vec3d::Zero();

    GizmosMap::const_iterator it = m_gizmos.find(Rotate);
    return (it != m_gizmos.end()) ? reinterpret_cast<GLGizmoRotate3D*>(it->second)->get_rotation() : Vec3d::Zero();
}

void GLCanvas3D::Gizmos::set_rotation(const Vec3d& rotation)
{
    if (!m_enabled)
        return;

    GizmosMap::const_iterator it = m_gizmos.find(Rotate);
    if (it != m_gizmos.end())
        reinterpret_cast<GLGizmoRotate3D*>(it->second)->set_rotation(rotation);
}

Vec3d GLCanvas3D::Gizmos::get_flattening_normal() const
{
    if (!m_enabled)
        return Vec3d::Zero();

    GizmosMap::const_iterator it = m_gizmos.find(Flatten);
    return (it != m_gizmos.end()) ? reinterpret_cast<GLGizmoFlatten*>(it->second)->get_flattening_normal() : Vec3d::Zero();
}

void GLCanvas3D::Gizmos::set_flattening_data(const ModelObject* model_object)
{
    if (!m_enabled)
        return;

    GizmosMap::const_iterator it = m_gizmos.find(Flatten);
    if (it != m_gizmos.end())
        reinterpret_cast<GLGizmoFlatten*>(it->second)->set_flattening_data(model_object);
}

void GLCanvas3D::Gizmos::set_model_object_ptr(ModelObject* model_object)
{
    if (!m_enabled)
        return;

    GizmosMap::const_iterator it = m_gizmos.find(SlaSupports);
    if (it != m_gizmos.end())
        reinterpret_cast<GLGizmoSlaSupports*>(it->second)->set_model_object_ptr(model_object);
}

void GLCanvas3D::Gizmos::clicked_on_object(const Vec2d& mouse_position)
{
    if (!m_enabled)
        return;

    GizmosMap::const_iterator it = m_gizmos.find(SlaSupports);
    if (it != m_gizmos.end())
        reinterpret_cast<GLGizmoSlaSupports*>(it->second)->clicked_on_object(mouse_position);
}

void GLCanvas3D::Gizmos::delete_current_grabber(bool delete_all)
{
    if (!m_enabled)
        return;

    GizmosMap::const_iterator it = m_gizmos.find(SlaSupports);
    if (it != m_gizmos.end())
        reinterpret_cast<GLGizmoSlaSupports*>(it->second)->delete_current_grabber(delete_all);
}

void GLCanvas3D::Gizmos::render_current_gizmo(const GLCanvas3D::Selection& selection) const
{
    if (!m_enabled)
        return;

    _render_current_gizmo(selection);
}

void GLCanvas3D::Gizmos::render_current_gizmo_for_picking_pass(const GLCanvas3D::Selection& selection) const
{
    if (!m_enabled)
        return;

    GLGizmoBase* curr = _get_current();
    if (curr != nullptr)
        curr->render_for_picking(selection);
}

void GLCanvas3D::Gizmos::render_overlay(const GLCanvas3D& canvas) const
{
    if (!m_enabled)
        return;

    ::glDisable(GL_DEPTH_TEST);

    ::glPushMatrix();
    ::glLoadIdentity();

    _render_overlay(canvas);

    ::glPopMatrix();
}

void GLCanvas3D::Gizmos::create_external_gizmo_widgets(wxWindow *parent)
{
    for (auto &entry : m_gizmos) {
        entry.second->create_external_gizmo_widgets(parent);
    }
}

void GLCanvas3D::Gizmos::_reset()
{
    for (GizmosMap::value_type& gizmo : m_gizmos)
    {
        delete gizmo.second;
        gizmo.second = nullptr;
    }

    m_gizmos.clear();
}

void GLCanvas3D::Gizmos::_render_overlay(const GLCanvas3D& canvas) const
{
    if (m_gizmos.empty())
        return;

    float cnv_w = (float)canvas.get_canvas_size().get_width();
    float zoom = canvas.get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    float height = _get_total_overlay_height();
    float top_x = (OverlayOffsetX - 0.5f * cnv_w) * inv_zoom;
    float top_y = 0.5f * height * inv_zoom;
    float scaled_gap_y = OverlayGapY * inv_zoom;
    for (GizmosMap::const_iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if ((it->second == nullptr) || !it->second->is_selectable())
            continue;

        float tex_size = (float)it->second->get_textures_size() * OverlayTexturesScale * inv_zoom;
        GLTexture::render_texture(it->second->get_texture_id(), top_x, top_x + tex_size, top_y - tex_size, top_y);
        top_y -= (tex_size + scaled_gap_y);
    }
}

void GLCanvas3D::Gizmos::_render_current_gizmo(const GLCanvas3D::Selection& selection) const
{
    GLGizmoBase* curr = _get_current();
    if (curr != nullptr)
        curr->render(selection);
}

float GLCanvas3D::Gizmos::_get_total_overlay_height() const
{
    float height = 0.0f;

    for (GizmosMap::const_iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if (it->first == SlaSupports && wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() != ptSLA)
            continue;
        height += (float)it->second->get_textures_size() + OverlayGapY;
    }

    return height - OverlayGapY;
}

GLGizmoBase* GLCanvas3D::Gizmos::_get_current() const
{
    GizmosMap::const_iterator it = m_gizmos.find(m_current);
    return (it != m_gizmos.end()) ? it->second : nullptr;
}

const unsigned char GLCanvas3D::WarningTexture::Background_Color[3] = { 9, 91, 134 };
const unsigned char GLCanvas3D::WarningTexture::Opacity = 255;

GLCanvas3D::WarningTexture::WarningTexture()
    : GUI::GLTexture()
    , m_original_width(0)
    , m_original_height(0)
{
}

bool GLCanvas3D::WarningTexture::generate(const std::string& msg)
{
    reset();

    if (msg.empty())
        return false;

    wxMemoryDC memDC;
    // select default font
    wxFont font = wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT);
    font.MakeLarger();
    font.MakeBold();
    memDC.SetFont(font);

    // calculates texture size
    wxCoord w, h;
    memDC.GetTextExtent(msg, &w, &h);

    int pow_of_two_size = next_highest_power_of_2(std::max<unsigned int>(w, h));

    m_original_width = (int)w;
    m_original_height = (int)h;
    m_width = pow_of_two_size;
    m_height = pow_of_two_size;

    // generates bitmap
    wxBitmap bitmap(m_width, m_height);

    memDC.SelectObject(bitmap);
    memDC.SetBackground(wxBrush(wxColour(Background_Color[0], Background_Color[1], Background_Color[2])));
    memDC.Clear();

    // draw message
    memDC.SetTextForeground(*wxWHITE);
    memDC.DrawText(msg, 0, 0);

    memDC.SelectObject(wxNullBitmap);

    // Convert the bitmap into a linear data ready to be loaded into the GPU.
    wxImage image = bitmap.ConvertToImage();
    image.SetMaskColour(Background_Color[0], Background_Color[1], Background_Color[2]);

    // prepare buffer
    std::vector<unsigned char> data(4 * m_width * m_height, 0);
    for (int h = 0; h < m_height; ++h)
    {
        int hh = h * m_width;
        unsigned char* px_ptr = data.data() + 4 * hh;
        for (int w = 0; w < m_width; ++w)
        {
            *px_ptr++ = image.GetRed(w, h);
            *px_ptr++ = image.GetGreen(w, h);
            *px_ptr++ = image.GetBlue(w, h);
            *px_ptr++ = image.IsTransparent(w, h) ? 0 : Opacity;
        }
    }

    // sends buffer to gpu
    ::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    ::glGenTextures(1, &m_id);
    ::glBindTexture(GL_TEXTURE_2D, (GLuint)m_id);
    ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data());
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
    ::glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

void GLCanvas3D::WarningTexture::render(const GLCanvas3D& canvas) const
{
    if ((m_id > 0) && (m_original_width > 0) && (m_original_height > 0) && (m_width > 0) && (m_height > 0))
    {
        ::glDisable(GL_DEPTH_TEST);
        ::glPushMatrix();
        ::glLoadIdentity();

        const Size& cnv_size = canvas.get_canvas_size();
        float zoom = canvas.get_camera_zoom();
        float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
        float left = (-0.5f * (float)m_original_width) * inv_zoom;
        float top = (-0.5f * (float)cnv_size.get_height() + (float)m_original_height + 2.0f) * inv_zoom;
        float right = left + (float)m_original_width * inv_zoom;
        float bottom = top - (float)m_original_height * inv_zoom;

        float uv_left = 0.0f;
        float uv_top = 0.0f;
        float uv_right = (float)m_original_width / (float)m_width;
        float uv_bottom = (float)m_original_height / (float)m_height;

        GLTexture::Quad_UVs uvs;
        uvs.left_top = { uv_left, uv_top };
        uvs.left_bottom = { uv_left, uv_bottom };
        uvs.right_bottom = { uv_right, uv_bottom };
        uvs.right_top = { uv_right, uv_top };

        GLTexture::render_sub_texture(m_id, left, right, bottom, top, uvs);

        ::glPopMatrix();
        ::glEnable(GL_DEPTH_TEST);
    }
}

const unsigned char GLCanvas3D::LegendTexture::Squares_Border_Color[3] = { 64, 64, 64 };
const unsigned char GLCanvas3D::LegendTexture::Background_Color[3] = { 9, 91, 134 };
const unsigned char GLCanvas3D::LegendTexture::Opacity = 255;

GLCanvas3D::LegendTexture::LegendTexture()
    : GUI::GLTexture()
    , m_original_width(0)
    , m_original_height(0)
{
}

bool GLCanvas3D::LegendTexture::generate(const GCodePreviewData& preview_data, const std::vector<float>& tool_colors)
{
    reset();

    // collects items to render
    auto title = _(preview_data.get_legend_title());
    const GCodePreviewData::LegendItemsList& items = preview_data.get_legend_items(tool_colors);

    unsigned int items_count = (unsigned int)items.size();
    if (items_count == 0)
        // nothing to render, return
        return false;

    wxMemoryDC memDC;
    // select default font
    memDC.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

    // calculates texture size
    wxCoord w, h;
    memDC.GetTextExtent(title, &w, &h);
    int title_width = (int)w;
    int title_height = (int)h;

    int max_text_width = 0;
    int max_text_height = 0;
    for (const GCodePreviewData::LegendItem& item : items)
    {
        memDC.GetTextExtent(GUI::from_u8(item.text), &w, &h);
        max_text_width = std::max(max_text_width, (int)w);
        max_text_height = std::max(max_text_height, (int)h);
    }

    m_original_width = std::max(2 * Px_Border + title_width, 2 * (Px_Border + Px_Square_Contour) + Px_Square + Px_Text_Offset + max_text_width);
    m_original_height = 2 * (Px_Border + Px_Square_Contour) + title_height + Px_Title_Offset + items_count * Px_Square;
    if (items_count > 1)
        m_original_height += (items_count - 1) * Px_Square_Contour;

    int pow_of_two_size = (int)next_highest_power_of_2(std::max<uint32_t>(m_original_width, m_original_height));

    m_width = pow_of_two_size;
    m_height = pow_of_two_size;

    // generates bitmap
    wxBitmap bitmap(m_width, m_height);

    memDC.SelectObject(bitmap);
    memDC.SetBackground(wxBrush(wxColour(Background_Color[0], Background_Color[1], Background_Color[2])));
    memDC.Clear();

    // draw title
    memDC.SetTextForeground(*wxWHITE);
    int title_x = Px_Border;
    int title_y = Px_Border;
    memDC.DrawText(title, title_x, title_y);

    // draw icons contours as background
    int squares_contour_x = Px_Border;
    int squares_contour_y = Px_Border + title_height + Px_Title_Offset;
    int squares_contour_width = Px_Square + 2 * Px_Square_Contour;
    int squares_contour_height = items_count * Px_Square + 2 * Px_Square_Contour;
    if (items_count > 1)
        squares_contour_height += (items_count - 1) * Px_Square_Contour;

    wxColour color(Squares_Border_Color[0], Squares_Border_Color[1], Squares_Border_Color[2]);
    wxPen pen(color);
    wxBrush brush(color);
    memDC.SetPen(pen);
    memDC.SetBrush(brush);
    memDC.DrawRectangle(wxRect(squares_contour_x, squares_contour_y, squares_contour_width, squares_contour_height));

    // draw items (colored icon + text)
    int icon_x = squares_contour_x + Px_Square_Contour;
    int icon_x_inner = icon_x + 1;
    int icon_y = squares_contour_y + Px_Square_Contour;
    int icon_y_step = Px_Square + Px_Square_Contour;

    int text_x = icon_x + Px_Square + Px_Text_Offset;
    int text_y_offset = (Px_Square - max_text_height) / 2;

    int px_inner_square = Px_Square - 2;

    for (const GCodePreviewData::LegendItem& item : items)
    {
        // draw darker icon perimeter
        const std::vector<unsigned char>& item_color_bytes = item.color.as_bytes();
        wxImage::HSVValue dark_hsv = wxImage::RGBtoHSV(wxImage::RGBValue(item_color_bytes[0], item_color_bytes[1], item_color_bytes[2]));
        dark_hsv.value *= 0.75;
        wxImage::RGBValue dark_rgb = wxImage::HSVtoRGB(dark_hsv);
        color.Set(dark_rgb.red, dark_rgb.green, dark_rgb.blue, item_color_bytes[3]);
        pen.SetColour(color);
        brush.SetColour(color);
        memDC.SetPen(pen);
        memDC.SetBrush(brush);
        memDC.DrawRectangle(wxRect(icon_x, icon_y, Px_Square, Px_Square));

        // draw icon interior
        color.Set(item_color_bytes[0], item_color_bytes[1], item_color_bytes[2], item_color_bytes[3]);
        pen.SetColour(color);
        brush.SetColour(color);
        memDC.SetPen(pen);
        memDC.SetBrush(brush);
        memDC.DrawRectangle(wxRect(icon_x_inner, icon_y + 1, px_inner_square, px_inner_square));

        // draw text
        memDC.DrawText(GUI::from_u8(item.text), text_x, icon_y + text_y_offset);

        // update y
        icon_y += icon_y_step;
    }

    memDC.SelectObject(wxNullBitmap);

    // Convert the bitmap into a linear data ready to be loaded into the GPU.
    wxImage image = bitmap.ConvertToImage();
    image.SetMaskColour(Background_Color[0], Background_Color[1], Background_Color[2]);

    // prepare buffer
    std::vector<unsigned char> data(4 * m_width * m_height, 0);
    for (int h = 0; h < m_height; ++h)
    {
        int hh = h * m_width;
        unsigned char* px_ptr = data.data() + 4 * hh;
        for (int w = 0; w < m_width; ++w)
        {
            *px_ptr++ = image.GetRed(w, h);
            *px_ptr++ = image.GetGreen(w, h);
            *px_ptr++ = image.GetBlue(w, h);
            *px_ptr++ = image.IsTransparent(w, h) ? 0 : Opacity;
        }
    }

    // sends buffer to gpu
    ::glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    ::glGenTextures(1, &m_id);
    ::glBindTexture(GL_TEXTURE_2D, (GLuint)m_id);
    ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, (GLsizei)m_width, (GLsizei)m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data.data());
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
    ::glBindTexture(GL_TEXTURE_2D, 0);

    return true;
}

void GLCanvas3D::LegendTexture::render(const GLCanvas3D& canvas) const
{
    if ((m_id > 0) && (m_original_width > 0) && (m_original_height > 0) && (m_width > 0) && (m_height > 0))
    {
        ::glDisable(GL_DEPTH_TEST);
        ::glPushMatrix();
        ::glLoadIdentity();

        const Size& cnv_size = canvas.get_canvas_size();
        float zoom = canvas.get_camera_zoom();
        float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;
        float left = (-0.5f * (float)cnv_size.get_width()) * inv_zoom;
        float top = (0.5f * (float)cnv_size.get_height()) * inv_zoom;
        float right = left + (float)m_original_width * inv_zoom;
        float bottom = top - (float)m_original_height * inv_zoom;

        float uv_left = 0.0f;
        float uv_top = 0.0f;
        float uv_right = (float)m_original_width / (float)m_width;
        float uv_bottom = (float)m_original_height / (float)m_height;

        GLTexture::Quad_UVs uvs;
        uvs.left_top = { uv_left, uv_top };
        uvs.left_bottom = { uv_left, uv_bottom };
        uvs.right_bottom = { uv_right, uv_bottom };
        uvs.right_top = { uv_right, uv_top };

        GLTexture::render_sub_texture(m_id, left, right, bottom, top, uvs);

        ::glPopMatrix();
        ::glEnable(GL_DEPTH_TEST);
    }
}

wxDEFINE_EVENT(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_OBJECT_SELECT, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_VIEWPORT_CHANGED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_RIGHT_CLICK, Vec2dEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_MODEL_UPDATE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_REMOVE_OBJECT, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_ARRANGE, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_INCREASE_INSTANCES, Event<int>);
wxDEFINE_EVENT(EVT_GLCANVAS_INSTANCE_MOVED, SimpleEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_WIPETOWER_MOVED, Vec3dEvent);
wxDEFINE_EVENT(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, Event<bool>);
wxDEFINE_EVENT(EVT_GLCANVAS_UPDATE_GEOMETRY, Vec3dsEvent<2>);
wxDEFINE_EVENT(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED, SimpleEvent);

GLCanvas3D::GLCanvas3D(wxGLCanvas* canvas)
    : m_canvas(canvas)
    , m_context(nullptr)
    , m_toolbar(*this)
    , m_config(nullptr)
    , m_process(nullptr)
    , m_model(nullptr)
    , m_dirty(true)
    , m_initialized(false)
    , m_use_VBOs(false)
    , m_force_zoom_to_bed_enabled(false)
    , m_apply_zoom_to_volumes_filter(false)
    , m_hover_volume_id(-1)
    , m_toolbar_action_running(false)
    , m_warning_texture_enabled(false)
    , m_legend_texture_enabled(false)
    , m_picking_enabled(false)
    , m_moving_enabled(false)
    , m_shader_enabled(false)
    , m_dynamic_background_enabled(false)
    , m_multisample_allowed(false)
    , m_regenerate_volumes(true)
    , m_color_by("volume")
    , m_reload_delayed(false)
    , m_external_gizmo_widgets_parent(nullptr)
{
    if (m_canvas != nullptr)
    {
#if !ENABLE_USE_UNIQUE_GLCONTEXT
        m_context = new wxGLContext(m_canvas);
#endif // !ENABLE_USE_UNIQUE_GLCONTEXT
        m_timer.SetOwner(m_canvas);
    }

    m_selection.set_volumes(&m_volumes.volumes);
}

GLCanvas3D::~GLCanvas3D()
{
    reset_volumes();

#if !ENABLE_USE_UNIQUE_GLCONTEXT
    if (m_context != nullptr)
    {
        delete m_context;
        m_context = nullptr;
    }
#endif // !ENABLE_USE_UNIQUE_GLCONTEXT
}

void GLCanvas3D::post_event(wxEvent &&event)
{
    event.SetEventObject(m_canvas);
    wxPostEvent(m_canvas, event);
}

void GLCanvas3D::viewport_changed()
{
    post_event(SimpleEvent(EVT_GLCANVAS_VIEWPORT_CHANGED));
}

bool GLCanvas3D::init(bool useVBOs, bool use_legacy_opengl)
{
    if (m_initialized)
        return true;

    if ((m_canvas == nullptr) || (m_context == nullptr))
        return false;

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
    GLfloat specular_cam[4] = { 0.3f, 0.3f, 0.3f, 1.0f };
    ::glLightfv(GL_LIGHT1, GL_SPECULAR, specular_cam);
    GLfloat diffuse_cam[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
    ::glLightfv(GL_LIGHT1, GL_DIFFUSE, diffuse_cam);

    // light from above
    GLfloat specular_top[4] = { 0.2f, 0.2f, 0.2f, 1.0f };
    ::glLightfv(GL_LIGHT0, GL_SPECULAR, specular_top);
    GLfloat diffuse_top[4] = { 0.5f, 0.5f, 0.5f, 1.0f };
    ::glLightfv(GL_LIGHT0, GL_DIFFUSE, diffuse_top);

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
    if (!m_volumes.empty())
        m_volumes.finalize_geometry(m_use_VBOs);

    if (m_gizmos.is_enabled()) {
        if (! m_gizmos.init(*this)) { 
            std::cout << "Unable to initialize gizmos: please, check that all the required textures are available" << std::endl;
            return false;
        }

        if (m_external_gizmo_widgets_parent != nullptr) {
            m_gizmos.create_external_gizmo_widgets(m_external_gizmo_widgets_parent);
            m_canvas->GetParent()->Layout();
        }
    }

    if (!_init_toolbar())
        return false;

    m_initialized = true;

    return true;
}

#if !ENABLE_USE_UNIQUE_GLCONTEXT
bool GLCanvas3D::set_current()
{
    if ((m_canvas != nullptr) && (m_context != nullptr))
        return m_canvas->SetCurrent(*m_context);

    return false;
}
#endif // !ENABLE_USE_UNIQUE_GLCONTEXT

void GLCanvas3D::set_as_dirty()
{
    m_dirty = true;
}

unsigned int GLCanvas3D::get_volumes_count() const
{
    return (unsigned int)m_volumes.volumes.size();
}

void GLCanvas3D::reset_volumes()
{
    if (!m_volumes.empty())
    {
#if !ENABLE_USE_UNIQUE_GLCONTEXT
        // ensures this canvas is current
        if (!set_current())
            return;
#endif // !ENABLE_USE_UNIQUE_GLCONTEXT

        m_selection.clear();
        m_volumes.release_geometry();
        m_volumes.clear();
        m_dirty = true;
    }

    enable_warning_texture(false);
    _reset_warning_texture();
}

int GLCanvas3D::check_volumes_outside_state(const DynamicPrintConfig* config) const
{
    ModelInstance::EPrintVolumeState state;
    m_volumes.check_outside_state(config, &state);
    return (int)state;
}

void GLCanvas3D::set_config(DynamicPrintConfig* config)
{
    m_config = config;
}

void GLCanvas3D::set_process(BackgroundSlicingProcess *process)
{
    m_process = process;
}

void GLCanvas3D::set_model(Model* model)
{
    m_model = model;
    m_selection.set_model(m_model);
}

void GLCanvas3D::set_bed_shape(const Pointfs& shape)
{
    bool new_shape = m_bed.set_shape(shape);

    // Set the origin and size for painting of the coordinate system axes.
    m_axes.origin = Vec3d(0.0, 0.0, (double)GROUND_Z);
    set_axes_length(0.3f * (float)m_bed.get_bounding_box().max_size());

    if (new_shape)
        zoom_to_bed();

    m_dirty = true;
}

void GLCanvas3D::set_axes_length(float length)
{
    m_axes.length = length;
}

void GLCanvas3D::set_color_by(const std::string& value)
{
    m_color_by = value;
}

float GLCanvas3D::get_camera_zoom() const
{
    return m_camera.zoom;
}

BoundingBoxf3 GLCanvas3D::volumes_bounding_box() const
{
    BoundingBoxf3 bb;
    for (const GLVolume* volume : m_volumes.volumes)
    {
        if (!m_apply_zoom_to_volumes_filter || ((volume != nullptr) && volume->zoom_to_volumes))
            bb.merge(volume->transformed_bounding_box());
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

bool GLCanvas3D::is_reload_delayed() const
{
    return m_reload_delayed;
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
    m_selection.set_mode(Selection::Instance);
}

void GLCanvas3D::enable_moving(bool enable)
{
    m_moving_enabled = enable;
}

void GLCanvas3D::enable_gizmos(bool enable)
{
    m_gizmos.set_enabled(enable);
}

void GLCanvas3D::enable_toolbar(bool enable)
{
    m_toolbar.set_enabled(enable);
}

void GLCanvas3D::enable_shader(bool enable)
{
    m_shader_enabled = enable;
}

void GLCanvas3D::enable_force_zoom_to_bed(bool enable)
{
    m_force_zoom_to_bed_enabled = enable;
}

void GLCanvas3D::enable_dynamic_background(bool enable)
{
    m_dynamic_background_enabled = enable;
}

void GLCanvas3D::allow_multisample(bool allow)
{
    m_multisample_allowed = allow;
}

void GLCanvas3D::enable_toolbar_item(const std::string& name, bool enable)
{
    if (enable)
        m_toolbar.enable_item(name);
    else
        m_toolbar.disable_item(name);
}

bool GLCanvas3D::is_toolbar_item_pressed(const std::string& name) const
{
    return m_toolbar.is_item_pressed(name);
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

#if ENABLE_MODIFIED_CAMERA_TARGET
void GLCanvas3D::zoom_to_selection()
{
    if (!m_selection.is_empty())
        _zoom_to_bounding_box(m_selection.get_bounding_box());
}
#endif // ENABLE_MODIFIED_CAMERA_TARGET

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

    if (dir_vec != nullptr)
    {
        m_camera.phi = dir_vec[0];
        m_camera.set_theta(dir_vec[1]);

        viewport_changed();
        
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
    if (m_config != nullptr)
        m_volumes.update_colors_by_extruder(m_config);
}

// Returns a Rect object denoting size and position of the Reset button used by a gizmo.
// Returns in either screen or viewport coords.
Rect GLCanvas3D::get_gizmo_reset_rect(const GLCanvas3D& canvas, bool viewport) const
{
    const Size& cnv_size = canvas.get_canvas_size();
    float w = (viewport ? -0.5f : 0.f) * (float)cnv_size.get_width();
    float h = (viewport ? 0.5f : 1.f) * (float)cnv_size.get_height();
    float zoom = canvas.get_camera_zoom();
    float inv_zoom = viewport ? ((zoom != 0.0f) ? 1.0f / zoom : 0.0f) : 1.f;
    const float gap = 30.f;
    return Rect((w + gap + 80.f) * inv_zoom, (viewport ? -1.f : 1.f) * (h - GIZMO_RESET_BUTTON_HEIGHT) * inv_zoom,
                (w + gap + 80.f + GIZMO_RESET_BUTTON_WIDTH) * inv_zoom, (viewport ? -1.f : 1.f) * (h * inv_zoom));
}

bool GLCanvas3D::gizmo_reset_rect_contains(const GLCanvas3D& canvas, float x, float y) const
{
    const Rect& rect = get_gizmo_reset_rect(canvas, false);
    return (rect.get_left() <= x) && (x <= rect.get_right()) && (rect.get_top() <= y) && (y <= rect.get_bottom());
}

void GLCanvas3D::render()
{
    if (m_canvas == nullptr)
        return;

    if (!_is_shown_on_screen())
        return;

    // ensures this canvas is current and initialized
#if ENABLE_USE_UNIQUE_GLCONTEXT
    if (!_set_current() || !_3DScene::init(m_canvas))
#else
    if (!set_current() || !_3DScene::init(m_canvas))
#endif // ENABLE_USE_UNIQUE_GLCONTEXT
        return;

    if (m_force_zoom_to_bed_enabled)
        _force_zoom_to_bed();

    _camera_tranform();

    GLfloat position_cam[4] = { 1.0f, 0.0f, 1.0f, 0.0f };
    ::glLightfv(GL_LIGHT1, GL_POSITION, position_cam);
    GLfloat position_top[4] = { -0.5f, -0.5f, 1.0f, 0.0f };
    ::glLightfv(GL_LIGHT0, GL_POSITION, position_top);

    float theta = m_camera.get_theta();
    bool is_custom_bed = m_bed.is_custom();

    set_tooltip("");

    // picking pass
    _picking_pass();

    // draw scene
    ::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    _render_background();

    if (is_custom_bed) // untextured bed needs to be rendered before objects
    {
        _render_bed(theta);
        // disable depth testing so that axes are not covered by ground
        _render_axes(false);
    }

    _render_objects();
    _render_selection();

    if (!is_custom_bed) // textured bed needs to be rendered after objects
    {
        _render_axes(true);
        _render_bed(theta);
    }

#if ENABLE_GIZMOS_ON_TOP
    // we need to set the mouse's scene position here because the depth buffer
    // could be invalidated by the following gizmo render methods
    // this position is used later into on_mouse() to drag the objects
    m_mouse.scene_position = _mouse_to_3d(m_mouse.position.cast<int>());
#endif // ENABLE_GIZMOS_ON_TOP

    _render_current_gizmo();
#if ENABLE_SHOW_CAMERA_TARGET
    _render_camera_target();
#endif // ENABLE_SHOW_CAMERA_TARGET

    // draw overlays
    _render_gizmos_overlay();
    _render_warning_texture();
    _render_legend_texture();
    _render_toolbar();
    _render_layer_editing_overlay();

    m_canvas->SwapBuffers();
}

void GLCanvas3D::select_all()
{
    m_selection.add_all();
}

void GLCanvas3D::delete_selected()
{
    m_selection.erase();
}

void GLCanvas3D::ensure_on_bed(unsigned int object_idx)
{
    typedef std::map<std::pair<int, int>, double> InstancesToZMap;
    InstancesToZMap instances_min_z;

    for (GLVolume* volume : m_volumes.volumes)
    {
        if ((volume->object_idx() == object_idx) && !volume->is_modifier)
        {
            double min_z = volume->transformed_convex_hull_bounding_box().min(2);
            std::pair<int, int> instance = std::make_pair(volume->object_idx(), volume->instance_idx());
            InstancesToZMap::iterator it = instances_min_z.find(instance);
            if (it == instances_min_z.end())
                it = instances_min_z.insert(InstancesToZMap::value_type(instance, DBL_MAX)).first;

            it->second = std::min(it->second, min_z);
        }
    }

    for (GLVolume* volume : m_volumes.volumes)
    {
        std::pair<int, int> instance = std::make_pair(volume->object_idx(), volume->instance_idx());
        InstancesToZMap::iterator it = instances_min_z.find(instance);
        if (it != instances_min_z.end())
            volume->set_instance_offset(Z, volume->get_instance_offset(Z) - it->second);
    }
}

std::vector<double> GLCanvas3D::get_current_print_zs(bool active_only) const
{
    return m_volumes.get_current_print_zs(active_only);
}

void GLCanvas3D::set_toolpaths_range(double low, double high)
{
    m_volumes.set_range(low, high);
}

std::vector<int> GLCanvas3D::load_object(const ModelObject& model_object, int obj_idx, std::vector<int> instance_idxs)
{
    if (instance_idxs.empty())
    {
        for (unsigned int i = 0; i < model_object.instances.size(); ++i)
        {
            instance_idxs.push_back(i);
        }
    }
    return m_volumes.load_object(&model_object, obj_idx, instance_idxs, m_color_by, m_use_VBOs && m_initialized);
}

std::vector<int> GLCanvas3D::load_object(const Model& model, int obj_idx)
{
    if ((0 <= obj_idx) && (obj_idx < (int)model.objects.size()))
    {
        const ModelObject* model_object = model.objects[obj_idx];
        if (model_object != nullptr)
            return load_object(*model_object, obj_idx, std::vector<int>());
    }

    return std::vector<int>();
}

void GLCanvas3D::mirror_selection(Axis axis)
{
    m_selection.mirror(axis);
    do_mirror();
    wxGetApp().obj_manipul()->update_settings_value(m_selection);
}

// Reload the 3D scene of 
// 1) Model / ModelObjects / ModelInstances / ModelVolumes
// 2) Print bed
// 3) SLA support meshes for their respective ModelObjects / ModelInstances
// 4) Wipe tower preview
// 5) Out of bed collision status & message overlay (texture)
void GLCanvas3D::reload_scene(bool refresh_immediately, bool force_full_scene_refresh)
{
    if ((m_canvas == nullptr) || (m_config == nullptr) || (m_model == nullptr))
        return;
#if !ENABLE_USE_UNIQUE_GLCONTEXT
    // ensures this canvas is current
    if (!set_current())
        return;
#endif // !ENABLE_USE_UNIQUE_GLCONTEXT

    struct ModelVolumeState {
        ModelVolumeState(const GLVolume *volume) : 
			model_volume(nullptr), geometry_id(volume->geometry_id), volume_idx(-1) {}
		ModelVolumeState(const ModelVolume *model_volume, const ModelID &instance_id, const GLVolume::CompositeID &composite_id) :
			model_volume(model_volume), geometry_id(std::make_pair(model_volume->id().id, instance_id.id)), composite_id(composite_id), volume_idx(-1) {}
		ModelVolumeState(const ModelID &volume_id, const ModelID &instance_id) :
			model_volume(nullptr), geometry_id(std::make_pair(volume_id.id, instance_id.id)), volume_idx(-1) {}
		bool new_geometry() const { return this->volume_idx == size_t(-1); }
		const ModelVolume		   *model_volume;
        // ModelID of ModelVolume + ModelID of ModelInstance
        // or timestamp of an SLAPrintObjectStep + ModelID of ModelInstance
        std::pair<size_t, size_t>   geometry_id;
        GLVolume::CompositeID       composite_id;
        // Volume index in the new GLVolume vector.
		size_t                      volume_idx;
    };
    std::vector<ModelVolumeState> model_volume_state;
	std::vector<ModelVolumeState> aux_volume_state;

    // SLA steps to pull the preview meshes for.
	typedef std::array<SLAPrintObjectStep, 2> SLASteps;
	SLASteps sla_steps = { slaposSupportTree, slaposBasePool };
    struct SLASupportState {
		std::array<PrintStateBase::StateWithTimeStamp, std::tuple_size<SLASteps>::value> step;
    };
    // State of the sla_steps for all SLAPrintObjects.
    std::vector<SLASupportState>   sla_support_state;

    std::vector<size_t> map_glvolume_old_to_new(m_volumes.volumes.size(), size_t(-1));
    std::vector<GLVolume*> glvolumes_new;
    glvolumes_new.reserve(m_volumes.volumes.size());
    auto model_volume_state_lower = [](const ModelVolumeState &m1, const ModelVolumeState &m2) { return m1.geometry_id < m2.geometry_id; };

    m_reload_delayed = ! m_canvas->IsShown() && ! refresh_immediately && ! force_full_scene_refresh;

    PrinterTechnology printer_technology = m_process->current_printer_technology();

    if (m_regenerate_volumes)
    {
        // Release invalidated volumes to conserve GPU memory in case of delayed refresh (see m_reload_delayed).
        // First initialize model_volumes_new_sorted & model_instances_new_sorted.
        for (int object_idx = 0; object_idx < (int)m_model->objects.size(); ++ object_idx) {
            const ModelObject *model_object = m_model->objects[object_idx];
            for (int instance_idx = 0; instance_idx < (int)model_object->instances.size(); ++ instance_idx) {
                const ModelInstance *model_instance = model_object->instances[instance_idx];
                for (int volume_idx = 0; volume_idx < (int)model_object->volumes.size(); ++ volume_idx) {
                    const ModelVolume *model_volume = model_object->volumes[volume_idx];
					model_volume_state.emplace_back(model_volume, model_instance->id(), GLVolume::CompositeID(object_idx, volume_idx, instance_idx));
                }
            }
        }
        if (printer_technology == ptSLA) {
            const SLAPrint *sla_print = this->sla_print();
        #ifdef _DEBUG
            // Verify that the SLAPrint object is synchronized with m_model.
            check_model_ids_equal(*m_model, sla_print->model());
        #endif /* _DEBUG */
            sla_support_state.reserve(sla_print->objects().size());
            for (const SLAPrintObject *print_object : sla_print->objects()) {
                SLASupportState state;
				for (size_t istep = 0; istep < sla_steps.size(); ++ istep) {
					state.step[istep] = print_object->step_state_with_timestamp(sla_steps[istep]);
					if (state.step[istep].state == PrintStateBase::DONE) {
                        if (! print_object->has_mesh(sla_steps[istep]))
                            // Consider the DONE step without a valid mesh as invalid for the purpose
                            // of mesh visualization.
                            state.step[istep].state = PrintStateBase::INVALID;
                        else
    						for (const ModelInstance *model_instance : print_object->model_object()->instances)
                                aux_volume_state.emplace_back(state.step[istep].timestamp, model_instance->id());
                    }
				}
				sla_support_state.emplace_back(state);
            }
        }
        std::sort(model_volume_state.begin(), model_volume_state.end(), model_volume_state_lower);
        std::sort(aux_volume_state  .begin(), aux_volume_state  .end(), model_volume_state_lower);
        // Release all ModelVolume based GLVolumes not found in the current Model.
        for (size_t volume_id = 0; volume_id < m_volumes.volumes.size(); ++ volume_id) {
            GLVolume         *volume = m_volumes.volumes[volume_id];
            ModelVolumeState  key(volume);
            ModelVolumeState *mvs = nullptr;
            if (volume->volume_idx() < 0) {
				auto it = std::lower_bound(aux_volume_state.begin(), aux_volume_state.end(), key, model_volume_state_lower);
                if (it != aux_volume_state.end() && it->geometry_id == key.geometry_id)
                    mvs = &(*it);
            } else {
				auto it = std::lower_bound(model_volume_state.begin(), model_volume_state.end(), key, model_volume_state_lower);
                if (it != model_volume_state.end() && it->geometry_id == key.geometry_id)
					mvs = &(*it);
            }
            if (mvs == nullptr || force_full_scene_refresh) {
                // This GLVolume will be released.
                volume->release_geometry();
                if (! m_reload_delayed)
                    delete volume;
            } else {
                // This GLVolume will be reused.
                volume->set_sla_shift_z(0.0);
                map_glvolume_old_to_new[volume_id] = glvolumes_new.size();
                mvs->volume_idx = glvolumes_new.size();
                glvolumes_new.emplace_back(volume);
                // Update color of the volume based on the current extruder.
				if (mvs->model_volume != nullptr) {
					int extruder_id = mvs->model_volume->extruder_id();
					if (extruder_id != -1)
						volume->extruder_id = extruder_id;

                    // updates volumes transformations
                    volume->set_instance_transformation(mvs->model_volume->get_object()->instances[volume->instance_idx()]->get_transformation());
                    volume->set_volume_transformation(mvs->model_volume->get_transformation());
                }
            }
        }
    }

    if (m_reload_delayed)
        return;

    set_bed_shape(dynamic_cast<const ConfigOptionPoints*>(m_config->option("bed_shape"))->values);

    if (m_regenerate_volumes)
    {
        m_volumes.volumes = std::move(glvolumes_new);
        for (unsigned int obj_idx = 0; obj_idx < (unsigned int)m_model->objects.size(); ++ obj_idx) {
            const ModelObject &model_object = *m_model->objects[obj_idx];
            // Object will share a single common layer height texture between all printable volumes.
            std::shared_ptr<LayersTexture> layer_height_texture;
            for (int volume_idx = 0; volume_idx < (int)model_object.volumes.size(); ++ volume_idx) {
				const ModelVolume &model_volume = *model_object.volumes[volume_idx];
                for (int instance_idx = 0; instance_idx < (int)model_object.instances.size(); ++ instance_idx) {
					const ModelInstance &model_instance = *model_object.instances[instance_idx];
					ModelVolumeState key(model_volume.id(), model_instance.id());
					auto it = std::lower_bound(model_volume_state.begin(), model_volume_state.end(), key, model_volume_state_lower);
					assert(it != model_volume_state.end() && it->geometry_id == key.geometry_id);
                    if (it->new_geometry()) {
                        // New volume.
						if (model_volume.is_model_part() && ! layer_height_texture) {
                            // New object part needs to have the layer height texture assigned, which is shared with the other volumes of the same part.
                            // Search for the layer height texture in the other volumes.
                            for (int iv = volume_idx; iv < (int)model_object.volumes.size(); ++ iv) {
								const ModelVolume &mv = *model_object.volumes[iv];
								if (mv.is_model_part())
									for (int ii = instance_idx; ii < (int)model_object.instances.size(); ++ ii) {
										const ModelInstance &mi = *model_object.instances[ii];
										ModelVolumeState key(mv.id(), mi.id());
										auto it = std::lower_bound(model_volume_state.begin(), model_volume_state.end(), key, model_volume_state_lower);
										assert(it != model_volume_state.end() && it->geometry_id == key.geometry_id);
										if (! it->new_geometry()) {
											// Found an old printable GLVolume (existing before this function was called).
                                            assert(m_volumes.volumes[it->volume_idx]->geometry_id == key.geometry_id);
											// Reuse the layer height texture.
											const GLVolume *volume = m_volumes.volumes[it->volume_idx];
											assert(volume->layer_height_texture);
											layer_height_texture = volume->layer_height_texture;
											goto iv_end;
										}
									}
							}
                        iv_end:
                            if (! layer_height_texture)
                                layer_height_texture = std::make_shared<LayersTexture>();
                        }
                        m_volumes.load_object_volume(&model_object, layer_height_texture, obj_idx, volume_idx, instance_idx, m_color_by, m_use_VBOs && m_initialized);
						m_volumes.volumes.back()->geometry_id = key.geometry_id;
                    } else {
						// Recycling an old GLVolume.
						GLVolume &existing_volume = *m_volumes.volumes[it->volume_idx];
                        assert(existing_volume.geometry_id == key.geometry_id);
						// Update the Object/Volume/Instance indices into the current Model.
                        existing_volume.composite_id = it->composite_id;
						if (model_volume.is_model_part() && ! layer_height_texture) {
                            assert(existing_volume.layer_height_texture);
                            // cache its layer height texture
                            layer_height_texture = existing_volume.layer_height_texture;
                        }
                    }
                }
            }
        }
        if (printer_technology == ptSLA) {
            size_t idx = 0;
            const SLAPrint *sla_print = this->sla_print();
            for (const SLAPrintObject *print_object : sla_print->objects()) {
                std::cout << "Current elevation: "<< print_object->get_current_elevation() << std::endl;
                SLASupportState   &state        = sla_support_state[idx ++];
                const ModelObject *model_object = print_object->model_object();
                // Find an index of the ModelObject
                int object_idx;
				if (std::all_of(state.step.begin(), state.step.end(), [](const PrintStateBase::StateWithTimeStamp &state){ return state.state != PrintStateBase::DONE; }))
					continue;
                // There may be new SLA volumes added to the scene for this print_object.
                // Find the object index of this print_object in the Model::objects list.
                auto it = std::find(sla_print->model().objects.begin(), sla_print->model().objects.end(), model_object);
                assert(it != sla_print->model().objects.end());
				object_idx = it - sla_print->model().objects.begin();
                // Collect indices of this print_object's instances, for which the SLA support meshes are to be added to the scene.
                // pairs of <instance_idx, print_instance_idx>
				std::vector<std::pair<size_t, size_t>> instances[std::tuple_size<SLASteps>::value];
                for (size_t print_instance_idx = 0; print_instance_idx < print_object->instances().size(); ++ print_instance_idx) {
                    const SLAPrintObject::Instance &instance = print_object->instances()[print_instance_idx];
                    // Find index of ModelInstance corresponding to this SLAPrintObject::Instance.
					auto it = std::find_if(model_object->instances.begin(), model_object->instances.end(), 
                        [&instance](const ModelInstance *mi) { return mi->id() == instance.instance_id; });
                    assert(it != model_object->instances.end());
                    int instance_idx = it - model_object->instances.begin();
                    for (size_t istep = 0; istep < sla_steps.size(); ++ istep)
                        if (state.step[istep].state == PrintStateBase::DONE) {
                            ModelVolumeState key(state.step[istep].timestamp, instance.instance_id.id);
                            auto it = std::lower_bound(aux_volume_state.begin(), aux_volume_state.end(), key, model_volume_state_lower);
                            assert(it != aux_volume_state.end() && it->geometry_id == key.geometry_id);
                            if (it->new_geometry())
                                instances[istep].emplace_back(std::pair<size_t, size_t>(instance_idx, print_instance_idx));
                            else
								// Recycling an old GLVolume. Update the Object/Instance indices into the current Model.
                                m_volumes.volumes[it->volume_idx]->composite_id = GLVolume::CompositeID(object_idx, m_volumes.volumes[it->volume_idx]->volume_idx(), instance_idx);
                        }
                }

                // stores the current volumes count
                size_t volumes_count = m_volumes.volumes.size();

                for (size_t istep = 0; istep < sla_steps.size(); ++istep)
                    if (!instances[istep].empty())
                        m_volumes.load_object_auxiliary(print_object, object_idx, instances[istep], sla_steps[istep], state.step[istep].timestamp, m_use_VBOs && m_initialized);

                if (volumes_count != m_volumes.volumes.size())
                {
                    // If any volume has been added
                    // Shift-up all volumes of the object so that it has the right elevation with respect to the print bed
                    double shift_z = print_object->get_current_elevation();
                    for (GLVolume* volume : m_volumes.volumes)
                    {
                        if (volume->object_idx() == object_idx)
                            volume->set_sla_shift_z(shift_z);
                    }
                }
            }
        }

        if (printer_technology == ptFFF && m_config->has("nozzle_diameter"))
        {
            // Should the wipe tower be visualized ?
            unsigned int extruders_count = (unsigned int)dynamic_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"))->values.size();

            bool semm = dynamic_cast<const ConfigOptionBool*>(m_config->option("single_extruder_multi_material"))->value;
            bool wt = dynamic_cast<const ConfigOptionBool*>(m_config->option("wipe_tower"))->value;
            bool co = dynamic_cast<const ConfigOptionBool*>(m_config->option("complete_objects"))->value;

            if ((extruders_count > 1) && semm && wt && !co)
            {
                // Height of a print (Show at least a slab)
                double height = std::max(m_model->bounding_box().max(2), 10.0);

                float x = dynamic_cast<const ConfigOptionFloat*>(m_config->option("wipe_tower_x"))->value;
                float y = dynamic_cast<const ConfigOptionFloat*>(m_config->option("wipe_tower_y"))->value;
                float w = dynamic_cast<const ConfigOptionFloat*>(m_config->option("wipe_tower_width"))->value;
                float a = dynamic_cast<const ConfigOptionFloat*>(m_config->option("wipe_tower_rotation_angle"))->value;

                const Print *print = m_process->fff_print();
                float depth = print->get_wipe_tower_depth();
                if (!print->is_step_done(psWipeTower))
                    depth = (900.f/w) * (float)(extruders_count - 1) ;
                m_volumes.load_wipe_tower_preview(1000, x, y, w, depth, (float)height, a, m_use_VBOs && m_initialized, !print->is_step_done(psWipeTower),
                                                  print->config().nozzle_diameter.values[0] * 1.25f * 4.5f);
            }
        }

        update_volumes_colors_by_extruder();
		// Update selection indices based on the old/new GLVolumeCollection.
		m_selection.volumes_changed(map_glvolume_old_to_new);
	}

    _update_gizmos_data();

    // Update the toolbar
    post_event(SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));

    // checks for geometry outside the print volume to render it accordingly
    if (!m_volumes.empty())
    {
        ModelInstance::EPrintVolumeState state;
        bool contained = m_volumes.check_outside_state(m_config, &state);

        if (!contained)
        {
            enable_warning_texture(true);
            _generate_warning_texture(L("Detected object outside print volume"));
            post_event(Event<bool>(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, state == ModelInstance::PVS_Fully_Outside));
        }
        else
        {
            enable_warning_texture(false);
            m_volumes.reset_outside_state();
            _reset_warning_texture();
            post_event(Event<bool>(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, !m_model->objects.empty()));
        }
    }
    else
    {
        enable_warning_texture(false);
        _reset_warning_texture();
        post_event(Event<bool>(EVT_GLCANVAS_ENABLE_ACTION_BUTTONS, false));
    }

    // restore to default value
    m_regenerate_volumes = true;
    // and force this canvas to be redrawn.
    m_dirty = true;
}

void GLCanvas3D::load_gcode_preview(const GCodePreviewData& preview_data, const std::vector<std::string>& str_tool_colors)
{
    const Print *print = this->fff_print();
    if ((m_canvas != nullptr) && (print != nullptr))
    {
#if !ENABLE_USE_UNIQUE_GLCONTEXT
        // ensures that this canvas is current
        if (!set_current())
            return;
#endif // !ENABLE_USE_UNIQUE_GLCONTEXT

        if (m_volumes.empty())
        {
            std::vector<float> tool_colors = _parse_colors(str_tool_colors);
            
            m_gcode_preview_volume_index.reset();
            
            _load_gcode_extrusion_paths(preview_data, tool_colors);
            _load_gcode_travel_paths(preview_data, tool_colors);
            _load_gcode_retractions(preview_data);
            _load_gcode_unretractions(preview_data);
            
            if (m_volumes.empty())
                reset_legend_texture();
            else
            {
                _generate_legend_texture(preview_data, tool_colors);

                // removes empty volumes
                m_volumes.volumes.erase(std::remove_if(m_volumes.volumes.begin(), m_volumes.volumes.end(),
                    [](const GLVolume* volume) { return volume->print_zs.empty(); }), m_volumes.volumes.end());

                _load_shells();
            }
            _update_toolpath_volumes_outside_state();
        }
        
        _update_gcode_volumes_visibility(preview_data);
        _show_warning_texture_if_needed();
    }
}

void GLCanvas3D::load_preview(const std::vector<std::string>& str_tool_colors)
{
    const Print *print = this->fff_print();
    if (print == nullptr)
        return;

    _load_print_toolpaths();
    _load_wipe_tower_toolpaths(str_tool_colors);
    for (const PrintObject* object : print->objects())
    {
        if (object != nullptr)
            _load_print_object_toolpaths(*object, str_tool_colors);
    }

    for (GLVolume* volume : m_volumes.volumes)
    {
        volume->is_extrusion_path = true;
    }

    _update_toolpath_volumes_outside_state();
    _show_warning_texture_if_needed();
    reset_legend_texture();
}

void GLCanvas3D::bind_event_handlers()
{
    if (m_canvas != nullptr)
    {
        m_canvas->Bind(wxEVT_SIZE, &GLCanvas3D::on_size, this);
        m_canvas->Bind(wxEVT_IDLE, &GLCanvas3D::on_idle, this);
        m_canvas->Bind(wxEVT_CHAR, &GLCanvas3D::on_char, this);
        m_canvas->Bind(wxEVT_MOUSEWHEEL, &GLCanvas3D::on_mouse_wheel, this);
        m_canvas->Bind(wxEVT_TIMER, &GLCanvas3D::on_timer, this);
        m_canvas->Bind(wxEVT_LEFT_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_LEFT_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_MIDDLE_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_MIDDLE_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_RIGHT_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_RIGHT_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_MOTION, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_ENTER_WINDOW, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_LEAVE_WINDOW, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_LEFT_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_MIDDLE_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_RIGHT_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Bind(wxEVT_PAINT, &GLCanvas3D::on_paint, this);
        m_canvas->Bind(wxEVT_KEY_DOWN, &GLCanvas3D::on_key_down, this);
    }
}

void GLCanvas3D::unbind_event_handlers()
{
    if (m_canvas != nullptr)
    {
        m_canvas->Unbind(wxEVT_SIZE, &GLCanvas3D::on_size, this);
        m_canvas->Unbind(wxEVT_IDLE, &GLCanvas3D::on_idle, this);
        m_canvas->Unbind(wxEVT_CHAR, &GLCanvas3D::on_char, this);
        m_canvas->Unbind(wxEVT_MOUSEWHEEL, &GLCanvas3D::on_mouse_wheel, this);
        m_canvas->Unbind(wxEVT_TIMER, &GLCanvas3D::on_timer, this);
        m_canvas->Unbind(wxEVT_LEFT_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_LEFT_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_MIDDLE_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_MIDDLE_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_RIGHT_DOWN, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_RIGHT_UP, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_MOTION, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_ENTER_WINDOW, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_LEAVE_WINDOW, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_LEFT_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_MIDDLE_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_RIGHT_DCLICK, &GLCanvas3D::on_mouse, this);
        m_canvas->Unbind(wxEVT_PAINT, &GLCanvas3D::on_paint, this);
        m_canvas->Unbind(wxEVT_KEY_DOWN, &GLCanvas3D::on_key_down, this);
    }
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
                // key +
                case 43: { post_event(Event<int>(EVT_GLCANVAS_INCREASE_INSTANCES, +1)); break; }
                // key -
                case 45: { post_event(Event<int>(EVT_GLCANVAS_INCREASE_INSTANCES, -1)); break; }
                // key A/a
                case 65:
                case 97: { post_event(SimpleEvent(EVT_GLCANVAS_ARRANGE)); break; }
                // key B/b
                case 66:
                case 98: { zoom_to_bed(); break; }
#if ENABLE_MODIFIED_CAMERA_TARGET
                // key Z/z
                case 90:
                case 122:
                {
                    if (m_selection.is_empty())
                        zoom_to_volumes();
                    else
                        zoom_to_selection();

                    break;
                }
#else
                // key Z/z
                case 90:
                case 122: { zoom_to_volumes(); break; }
#endif // ENABLE_MODIFIED_CAMERA_TARGET
                default:
                {
#if ENABLE_GIZMOS_SHORTCUT
                    if (m_gizmos.handle_shortcut(keyCode, m_selection))
                    {
                        _update_gizmos_data();
                        render();
                    }
                    else
#endif // ENABLE_GIZMOS_SHORTCUT
                        evt.Skip();

                    break;
                }
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
    if (is_layers_editing_enabled())
    {
        int object_idx_selected = m_selection.get_object_idx();
        if (object_idx_selected != -1)
        {
            // A volume is selected. Test, whether hovering over a layer thickness bar.
            if (m_layers_editing.bar_rect_contains(*this, (float)evt.GetX(), (float)evt.GetY()))
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
        zoom = std::max(zoom, zoom_min * 0.8f);
    
    m_camera.zoom = zoom;
    viewport_changed();

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
    Point pos(evt.GetX(), evt.GetY());

    int selected_object_idx = m_selection.get_object_idx();
    int layer_editing_object_idx = is_layers_editing_enabled() ? selected_object_idx : -1;
    m_layers_editing.last_object_id = layer_editing_object_idx;
    bool gizmos_overlay_contains_mouse = m_gizmos.overlay_contains_mouse(*this, m_mouse.position);
    int toolbar_contains_mouse = m_toolbar.contains_mouse(m_mouse.position);

    if (evt.Entering())
    {
#if defined(__WXMSW__) || defined(__linux__)
        // On Windows and Linux needs focus in order to catch key events
        if (m_canvas != nullptr)
            m_canvas->SetFocus();

        m_mouse.set_start_position_2D_as_invalid();
#endif
    }
    else if (evt.Leaving())
    {
        // to remove hover on objects when the mouse goes out of this canvas
        m_mouse.position = Vec2d(-1.0, -1.0);
        m_dirty = true;
    }
    else if (evt.LeftDClick() && (toolbar_contains_mouse != -1))
    {
        m_toolbar_action_running = true;
        m_toolbar.do_action((unsigned int)toolbar_contains_mouse);
    }
#if ENABLE_GIZMOS_RESET
    else if (evt.LeftDClick() && m_gizmos.grabber_contains_mouse())
    {
        m_mouse.ignore_up_event = true;
        m_gizmos.process_double_click();
        switch (m_gizmos.get_current_type())
        {
        case Gizmos::Scale:
        {
            m_selection.scale(m_gizmos.get_scale(), false);
            do_scale();
            wxGetApp().obj_manipul()->update_settings_value(m_selection);
            m_dirty = true;
            break;
        }
#if !ENABLE_WORLD_ROTATIONS
        case Gizmos::Rotate:
        {
            m_selection.rotate(m_gizmos.get_rotation(), false);
            do_rotate();
            wxGetApp().obj_manipul()->update_settings_value(m_selection);
            m_dirty = true;
            break;
        }
#endif // !ENABLE_WORLD_ROTATIONS
        default:
        {
            break;
        }
        }
    }
#endif // ENABLE_GIZMOS_RESET
    else if (evt.LeftDown() || evt.RightDown())
    {
        // If user pressed left or right button we first check whether this happened
        // on a volume or not.
        m_layers_editing.state = LayersEditing::Unknown;
        if ((layer_editing_object_idx != -1) && m_layers_editing.bar_rect_contains(*this, pos(0), pos(1)))
        {
            // A volume is selected and the mouse is inside the layer thickness bar.
            // Start editing the layer height.
            m_layers_editing.state = LayersEditing::Editing;
            _perform_layer_editing_action(&evt);
        }
        else if ((layer_editing_object_idx != -1) && m_layers_editing.reset_rect_contains(*this, pos(0), pos(1)))
        {
            if (evt.LeftDown())
            {
                // A volume is selected and the mouse is inside the reset button.
                // The PrintObject::adjust_layer_height_profile() call adjusts the profile of its associated ModelObject, it does not modify the profile of the PrintObject itself,
                // therefore it is safe to call it while the background processing is running.
                const_cast<PrintObject*>(this->fff_print()->get_object(layer_editing_object_idx))->reset_layer_height_profile();
                // Index 2 means no editing, just wait for mouse up event.
                m_layers_editing.state = LayersEditing::Completed;

                m_dirty = true;
            }
        }
        else if ((m_gizmos.get_current_type() == Gizmos::SlaSupports) && gizmo_reset_rect_contains(*this, pos(0), pos(1)))
        {
            if (evt.LeftDown())
            {
                m_gizmos.delete_current_grabber(true);
#if ENABLE_GIZMOS_RESET
                m_mouse.ignore_up_event = true;
#endif // ENABLE_GIZMOS_RESET
                m_dirty = true;
            }
        }
        else if (!m_selection.is_empty() && gizmos_overlay_contains_mouse)
        {
            m_gizmos.update_on_off_state(*this, m_mouse.position, m_selection);
            _update_gizmos_data();
            m_dirty = true;
        }
        else if (evt.LeftDown() && !m_selection.is_empty() && m_gizmos.grabber_contains_mouse())
        {
            _update_gizmos_data();
            m_selection.start_dragging();
            m_gizmos.start_dragging(m_selection);

            if (m_gizmos.get_current_type() == Gizmos::Flatten) {
                // Rotate the object so the normal points downward:
                m_selection.flattening_rotate(m_gizmos.get_flattening_normal());
                do_flatten();
                wxGetApp().obj_manipul()->update_settings_value(m_selection);
            }

            m_dirty = true;
        }
        else if ((selected_object_idx != -1) && m_gizmos.grabber_contains_mouse() && evt.RightDown()) {
            if (m_gizmos.get_current_type() == Gizmos::SlaSupports)
                m_gizmos.delete_current_grabber();
        }
        else if (toolbar_contains_mouse != -1)
        {
            m_toolbar_action_running = true;
            m_toolbar.do_action((unsigned int)toolbar_contains_mouse);
        }
        else
        {
            // Select volume in this 3D canvas.
            // Don't deselect a volume if layer editing is enabled. We want the object to stay selected
            // during the scene manipulation.

            if (m_picking_enabled && ((m_hover_volume_id != -1) || !is_layers_editing_enabled()))
            {
                if (evt.LeftDown() && (m_hover_volume_id != -1))
                {
                    bool already_selected = m_selection.contains_volume(m_hover_volume_id);
                    bool shift_down = evt.ShiftDown();

                    if (already_selected && shift_down)
                        m_selection.remove(m_hover_volume_id);
                    else
                    {
                        bool add_as_single = !already_selected && !evt.ShiftDown();
                        m_selection.add(m_hover_volume_id, add_as_single);
                    }

                    m_gizmos.update_on_off_state(m_selection);
                    _update_gizmos_data();
                    wxGetApp().obj_manipul()->update_settings_value(m_selection);
                    post_event(SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
                    m_dirty = true;
                }
            }

            // propagate event through callback

            if (m_hover_volume_id != -1)
            {
                if (evt.LeftDown() && m_moving_enabled && (m_mouse.drag.move_volume_idx == -1))
                {
#if !ENABLE_GIZMOS_ON_TOP
                    // The mouse_to_3d gets the Z coordinate from the Z buffer at the screen coordinate pos x, y,
                    // an converts the screen space coordinate to unscaled object space.
                    Vec3d pos3d = _mouse_to_3d(pos);
#endif // !ENABLE_GIZMOS_ON_TOP

                    // Only accept the initial position, if it is inside the volume bounding box.
                    BoundingBoxf3 volume_bbox = m_volumes.volumes[m_hover_volume_id]->transformed_bounding_box();
                    volume_bbox.offset(1.0);
#if ENABLE_GIZMOS_ON_TOP
                    if (volume_bbox.contains(m_mouse.scene_position))
#else
                    if (volume_bbox.contains(pos3d))
#endif // ENABLE_GIZMOS_ON_TOP
                    {
                        // The dragging operation is initiated.
                        m_mouse.drag.move_volume_idx = m_hover_volume_id;
                        m_selection.start_dragging();
#if ENABLE_GIZMOS_ON_TOP
                        m_mouse.drag.start_position_3D = m_mouse.scene_position;
#else
                        m_mouse.drag.start_position_3D = pos3d;
#endif // ENABLE_GIZMOS_ON_TOP
                    }
                }
                else if (evt.RightDown())
                {
                    // forces a frame render to ensure that m_hover_volume_id is updated even when the user right clicks while
                    // the context menu is already shown, ensuring it to disappear if the mouse is outside any volume
                    m_mouse.position = Vec2d((double)pos(0), (double)pos(1));
                    render();
                    if (m_hover_volume_id != -1)
                    {
                        // if right clicking on volume, propagate event through callback (shows context menu)
                        if (m_volumes.volumes[m_hover_volume_id]->hover && !m_volumes.volumes[m_hover_volume_id]->is_wipe_tower)
                        {
                            // forces the selection of the volume
                            m_selection.add(m_hover_volume_id);
                            m_gizmos.update_on_off_state(m_selection);
                            post_event(SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
                            _update_gizmos_data();
                            wxGetApp().obj_manipul()->update_settings_value(m_selection);
                            // forces a frame render to update the view before the context menu is shown
                            render();
                            post_event(Vec2dEvent(EVT_GLCANVAS_RIGHT_CLICK, pos.cast<double>()));
                        }
                    }
                }
            }
        }
    }
    else if (evt.Dragging() && evt.LeftIsDown() && !gizmos_overlay_contains_mouse && (m_layers_editing.state == LayersEditing::Unknown) && (m_mouse.drag.move_volume_idx != -1))
    {
        m_mouse.dragging = true;

        // Get new position at the same Z of the initial click point.
        float z0 = 0.0f;
        float z1 = 1.0f;
        // we do not want to translate objects if the user just clicked on an object while pressing shift to remove it from the selection and then drag
        Vec3d cur_pos = m_selection.contains_volume(m_hover_volume_id) ? Linef3(_mouse_to_3d(pos, &z0), _mouse_to_3d(pos, &z1)).intersect_plane(m_mouse.drag.start_position_3D(2)) : m_mouse.drag.start_position_3D;

        m_selection.translate(cur_pos - m_mouse.drag.start_position_3D);
        wxGetApp().obj_manipul()->update_settings_value(m_selection);

        m_dirty = true;
    }
    else if (evt.Dragging() && m_gizmos.is_dragging())
    {
        if (!m_canvas->HasCapture())
            m_canvas->CaptureMouse();

        m_mouse.dragging = true;
        m_gizmos.update(mouse_ray(pos), evt.ShiftDown(), &pos);

        switch (m_gizmos.get_current_type())
        {
        case Gizmos::Move:
        {
            // Apply new temporary offset
            m_selection.translate(m_gizmos.get_displacement());
            wxGetApp().obj_manipul()->update_settings_value(m_selection);
            break;
        }
        case Gizmos::Scale:
        {
            // Apply new temporary scale factors
            m_selection.scale(m_gizmos.get_scale(), evt.AltDown());
            wxGetApp().obj_manipul()->update_settings_value(m_selection);
            break;
        }
        case Gizmos::Rotate:
        {
            // Apply new temporary rotations
            m_selection.rotate(m_gizmos.get_rotation(), evt.AltDown());
            wxGetApp().obj_manipul()->update_settings_value(m_selection);
            break;
        }
        default:
            break;
        }

//        if (!volumes.empty())
//        {
//          BoundingBoxf3 bb;
//            for (const GLVolume* volume : volumes)
//            {
//                bb.merge(volume->transformed_bounding_box());
//            }
//            const Vec3d& size = bb.size();
//            const Vec3d& scale = m_gizmos.get_scale();
//            post_event(Vec3dsEvent<2>(EVT_GLCANVAS_UPDATE_GEOMETRY, {size, scale}));
//        }

        m_dirty = true;
    }
    else if (evt.Dragging() && !gizmos_overlay_contains_mouse)
    {
        m_mouse.dragging = true;

        if ((m_layers_editing.state != LayersEditing::Unknown) && (layer_editing_object_idx != -1))
        {
            if (m_layers_editing.state == LayersEditing::Editing)
                _perform_layer_editing_action(&evt);
        }
        else if (evt.LeftIsDown())
        {
            // if dragging over blank area with left button, rotate
            if (m_mouse.is_start_position_3D_defined())
            {
                const Vec3d& orig = m_mouse.drag.start_position_3D;
                m_camera.phi += (((float)pos(0) - (float)orig(0)) * TRACKBALLSIZE);
                m_camera.set_theta(m_camera.get_theta() - ((float)pos(1) - (float)orig(1)) * TRACKBALLSIZE);

                viewport_changed();

                m_dirty = true;
            }
            m_mouse.drag.start_position_3D = Vec3d((double)pos(0), (double)pos(1), 0.0);
        }
        else if (evt.MiddleIsDown() || evt.RightIsDown())
        {
            // If dragging over blank area with right button, pan.
            if (m_mouse.is_start_position_2D_defined())
            {
                // get point in model space at Z = 0
                float z = 0.0f;
                const Vec3d& cur_pos = _mouse_to_3d(pos, &z);
                Vec3d orig = _mouse_to_3d(m_mouse.drag.start_position_2D, &z);
                m_camera.target += orig - cur_pos;

                viewport_changed();

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

            if (layer_editing_object_idx != -1)
                post_event(SimpleEvent(EVT_GLCANVAS_MODEL_UPDATE));
        }
        else if ((m_mouse.drag.move_volume_idx != -1) && m_mouse.dragging)
        {
            m_regenerate_volumes = false;
            do_move();
            wxGetApp().obj_manipul()->update_settings_value(m_selection);
        }
        else if (m_gizmos.get_current_type() == Gizmos::SlaSupports && m_hover_volume_id != -1)
        {
            int id = m_selection.get_object_idx();

            if ((id != -1) && (m_model != nullptr)) {
                m_gizmos.clicked_on_object(Vec2d(pos(0), pos(1)));
            }
        }
        else if (evt.LeftUp() && !m_mouse.dragging && (m_hover_volume_id == -1) && !gizmos_overlay_contains_mouse && !m_gizmos.is_dragging() && !is_layers_editing_enabled())
        {
            // deselect and propagate event through callback
#if ENABLE_GIZMOS_RESET
            if (!m_mouse.ignore_up_event && m_picking_enabled && !m_toolbar_action_running)
#else
            if (m_picking_enabled && !m_toolbar_action_running)
#endif // ENABLE_GIZMOS_RESET
            {
                m_selection.clear();
                m_selection.set_mode(Selection::Instance);
                wxGetApp().obj_manipul()->update_settings_value(m_selection);
                post_event(SimpleEvent(EVT_GLCANVAS_OBJECT_SELECT));
                _update_gizmos_data();
            }
#if ENABLE_GIZMOS_RESET
            else if (m_mouse.ignore_up_event)
                m_mouse.ignore_up_event = false;
#endif // ENABLE_GIZMOS_RESET
        }
        else if (evt.LeftUp() && m_gizmos.is_dragging())
        {
            switch (m_gizmos.get_current_type())
            {
            case Gizmos::Move:
            {
                m_regenerate_volumes = false;
                do_move();
                break;
            }
            case Gizmos::Scale:
            {
                do_scale();
                break;
            }
            case Gizmos::Rotate:
            {
                do_rotate();
                break;
            }
            default:
                break;
            }
            m_gizmos.stop_dragging();
#if ENABLE_WORLD_ROTATIONS
            _update_gizmos_data();
#endif // ENABLE_WORLD_ROTATIONS
            wxGetApp().obj_manipul()->update_settings_value(m_selection);
            // Let the platter know that the dragging finished, so a delayed refresh
            // of the scene with the background processing data should be performed.
            post_event(SimpleEvent(EVT_GLCANVAS_MOUSE_DRAGGING_FINISHED));
        }

        m_mouse.drag.move_volume_idx = -1;
        m_mouse.set_start_position_3D_as_invalid();
        m_mouse.set_start_position_2D_as_invalid();
        m_mouse.dragging = false;
        m_toolbar_action_running = false;
        m_dirty = true;

        if (m_canvas->HasCapture())
            m_canvas->ReleaseMouse();
    }
    else if (evt.Moving())
    {
        m_mouse.position = Vec2d((double)pos(0), (double)pos(1));
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

void GLCanvas3D::on_key_down(wxKeyEvent& evt)
{
    if (evt.HasModifiers())
        evt.Skip();
    else
    {
        int key = evt.GetKeyCode();
#ifdef __WXOSX__
        if (key == WXK_BACK)
#else
        if (key == WXK_DELETE)
#endif // __WXOSX__
            post_event(SimpleEvent(EVT_GLCANVAS_REMOVE_OBJECT));
        else
            evt.Skip();
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

Point GLCanvas3D::get_local_mouse_position() const
{
    if (m_canvas == nullptr)
        return Point();

    wxPoint mouse_pos = m_canvas->ScreenToClient(wxGetMousePosition());
    return Point(mouse_pos.x, mouse_pos.y);
}

void GLCanvas3D::reset_legend_texture()
{
#if !ENABLE_USE_UNIQUE_GLCONTEXT
    if (!set_current())
        return;
#endif // !ENABLE_USE_UNIQUE_GLCONTEXT

    m_legend_texture.reset();
}

void GLCanvas3D::set_tooltip(const std::string& tooltip) const
{
    if (m_canvas != nullptr)
    {
        wxToolTip* t = m_canvas->GetToolTip();
        if (t != nullptr)
        {
            if (t->GetTip() != tooltip)
                t->SetTip(tooltip);
        }
        else
            m_canvas->SetToolTip(tooltip);
    }
}

void GLCanvas3D::set_external_gizmo_widgets_parent(wxWindow *parent)
{
    m_external_gizmo_widgets_parent = parent;
}

void GLCanvas3D::do_move()
{
    if (m_model == nullptr)
        return;

    std::set<std::pair<int, int>> done;  // keeps track of modified instances
    bool object_moved = false;
    Vec3d wipe_tower_origin = Vec3d::Zero();

    Selection::EMode selection_mode = m_selection.get_mode();

    for (const GLVolume* v : m_volumes.volumes)
    {
        int object_idx = v->object_idx();
        int instance_idx = v->instance_idx();
        int volume_idx = v->volume_idx();

        std::pair<int, int> done_id(object_idx, instance_idx);

        if ((0 <= object_idx) && (object_idx < (int)m_model->objects.size()))
        {
            done.insert(done_id);

            // Move instances/volumes
            ModelObject* model_object = m_model->objects[object_idx];
            if (model_object != nullptr)
            {
#if ENABLE_MODELVOLUME_TRANSFORM
                if (selection_mode == Selection::Instance)
                {
                    model_object->instances[instance_idx]->set_offset(v->get_instance_offset());
                    object_moved = true;
                }
                else if (selection_mode == Selection::Volume)
                {
                    model_object->volumes[volume_idx]->set_offset(v->get_volume_offset());
                    object_moved = true;
                }
                if (object_moved)
#else
                model_object->instances[instance_idx]->set_offset(v->get_offset());
                object_moved = true;
#endif // ENABLE_MODELVOLUME_TRANSFORM
                model_object->invalidate_bounding_box();
            }
        }
        else if (object_idx == 1000)
            // Move a wipe tower proxy.
#if ENABLE_MODELVOLUME_TRANSFORM
            wipe_tower_origin = v->get_volume_offset();
#else
            wipe_tower_origin = v->get_offset();
#endif // ENABLE_MODELVOLUME_TRANSFORM
    }

    // Fixes sinking/flying instances
    for (const std::pair<int, int>& i : done)
    {
        ModelObject* m = m_model->objects[i.first];
        Vec3d shift(0.0, 0.0, -m->get_instance_min_z(i.second));
        m_selection.translate(i.first, i.second, shift);
        m->translate_instance(i.second, shift);
    }

    if (object_moved)
        post_event(SimpleEvent(EVT_GLCANVAS_INSTANCE_MOVED));

    if (wipe_tower_origin != Vec3d::Zero())
        post_event(Vec3dEvent(EVT_GLCANVAS_WIPETOWER_MOVED, std::move(wipe_tower_origin)));
}

void GLCanvas3D::do_rotate()
{
    if (m_model == nullptr)
        return;

    std::set<std::pair<int, int>> done;  // keeps track of modified instances

    Selection::EMode selection_mode = m_selection.get_mode();

    for (const GLVolume* v : m_volumes.volumes)
    {
        int object_idx = v->object_idx();
        if ((object_idx < 0) || ((int)m_model->objects.size() <= object_idx))
            continue;

        int instance_idx = v->instance_idx();
        int volume_idx = v->volume_idx();

        done.insert(std::pair<int, int>(object_idx, instance_idx));

        // Rotate instances/volumes.
        ModelObject* model_object = m_model->objects[object_idx];
        if (model_object != nullptr)
        {
#if ENABLE_MODELVOLUME_TRANSFORM
            if (selection_mode == Selection::Instance)
            {
                model_object->instances[instance_idx]->set_rotation(v->get_instance_rotation());
                model_object->instances[instance_idx]->set_offset(v->get_instance_offset());
            }
            else if (selection_mode == Selection::Volume)
            {
                model_object->volumes[volume_idx]->set_rotation(v->get_volume_rotation());
                model_object->volumes[volume_idx]->set_offset(v->get_volume_offset());
            }
#else
            model_object->instances[instance_idx]->set_rotation(v->get_rotation());
            model_object->instances[instance_idx]->set_offset(v->get_offset());
#endif // ENABLE_MODELVOLUME_TRANSFORM
            model_object->invalidate_bounding_box();
        }
    }

    // Fixes sinking/flying instances
    for (const std::pair<int, int>& i : done)
    {
        ModelObject* m = m_model->objects[i.first];
        Vec3d shift(0.0, 0.0, -m->get_instance_min_z(i.second));
        m_selection.translate(i.first, i.second, shift);
        m->translate_instance(i.second, shift);
    }

    post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
}

void GLCanvas3D::do_scale()
{
    if (m_model == nullptr)
        return;

    std::set<std::pair<int, int>> done;  // keeps track of modified instances

    Selection::EMode selection_mode = m_selection.get_mode();

    for (const GLVolume* v : m_volumes.volumes)
    {
        int object_idx = v->object_idx();
        if ((object_idx < 0) || ((int)m_model->objects.size() <= object_idx))
            continue;

        int instance_idx = v->instance_idx();
        int volume_idx = v->volume_idx();

        done.insert(std::pair<int, int>(object_idx, instance_idx));

        // Rotate instances/volumes
        ModelObject* model_object = m_model->objects[object_idx];
        if (model_object != nullptr)
        {
#if ENABLE_MODELVOLUME_TRANSFORM
            if (selection_mode == Selection::Instance)
            {
                model_object->instances[instance_idx]->set_scaling_factor(v->get_instance_scaling_factor());
                model_object->instances[instance_idx]->set_offset(v->get_instance_offset());
            }
            else if (selection_mode == Selection::Volume)
            {
                model_object->instances[instance_idx]->set_offset(v->get_instance_offset());
                model_object->volumes[volume_idx]->set_scaling_factor(v->get_volume_scaling_factor());
                model_object->volumes[volume_idx]->set_offset(v->get_volume_offset());
            }
#else
            model_object->instances[instance_idx]->set_scaling_factor(v->get_scaling_factor());
            model_object->instances[instance_idx]->set_offset(v->get_offset());
#endif // ENABLE_MODELVOLUME_TRANSFORM
            model_object->invalidate_bounding_box();
        }
    }

    // Fixes sinking/flying instances
    for (const std::pair<int, int>& i : done)
    {
        ModelObject* m = m_model->objects[i.first];
        Vec3d shift(0.0, 0.0, -m->get_instance_min_z(i.second));
        m_selection.translate(i.first, i.second, shift);
        m->translate_instance(i.second, shift);
    }

    post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
}

void GLCanvas3D::do_flatten()
{
    do_rotate();
}

void GLCanvas3D::do_mirror()
{
    if (m_model == nullptr)
        return;

    std::set<std::pair<int, int>> done;  // keeps track of modified instances

    Selection::EMode selection_mode = m_selection.get_mode();

    for (const GLVolume* v : m_volumes.volumes)
    {
        int object_idx = v->object_idx();
        if ((object_idx < 0) || ((int)m_model->objects.size() <= object_idx))
            continue;

        int instance_idx = v->instance_idx();
        int volume_idx = v->volume_idx();

        done.insert(std::pair<int, int>(object_idx, instance_idx));

        // Mirror instances/volumes
        ModelObject* model_object = m_model->objects[object_idx];
        if (model_object != nullptr)
        {
#if ENABLE_MODELVOLUME_TRANSFORM
            if (selection_mode == Selection::Instance)
                model_object->instances[instance_idx]->set_mirror(v->get_instance_mirror());
            else if (selection_mode == Selection::Volume)
                model_object->volumes[volume_idx]->set_mirror(v->get_volume_mirror());
#else
            model_object->instances[instance_idx]->set_mirror(v->get_mirror());
#endif // ENABLE_MODELVOLUME_TRANSFORM
            model_object->invalidate_bounding_box();
        }
    }

    post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
}

bool GLCanvas3D::_is_shown_on_screen() const
{
    return (m_canvas != nullptr) ? m_canvas->IsShownOnScreen() : false;
}

void GLCanvas3D::_force_zoom_to_bed()
{
    zoom_to_bed();
    m_force_zoom_to_bed_enabled = false;
}

bool GLCanvas3D::_init_toolbar()
{
    if (!m_toolbar.is_enabled())
        return true;

    if (!m_toolbar.init("toolbar.png", 36, 1, 1))
    {
        // unable to init the toolbar texture, disable it
        m_toolbar.set_enabled(false);
        return true;
    }

//    m_toolbar.set_layout_type(GLToolbar::Layout::Vertical);
    m_toolbar.set_layout_type(GLToolbar::Layout::Horizontal);
    m_toolbar.set_separator_size(5);
    m_toolbar.set_gap_size(2);

    GLToolbarItem::Data item;

    item.name = "add";
    item.tooltip = GUI::L_str("Add...");
    item.sprite_id = 0;
    item.is_toggable = false;
    item.action_event = EVT_GLTOOLBAR_ADD;
    if (!m_toolbar.add_item(item))
        return false;

    item.name = "delete";
    item.tooltip = GUI::L_str("Delete");
    item.sprite_id = 1;
    item.is_toggable = false;
    item.action_event = EVT_GLTOOLBAR_DELETE;
    if (!m_toolbar.add_item(item))
        return false;

    item.name = "deleteall";
    item.tooltip = GUI::L_str("Delete all");
    item.sprite_id = 2;
    item.is_toggable = false;
    item.action_event = EVT_GLTOOLBAR_DELETE_ALL;
    if (!m_toolbar.add_item(item))
        return false;

    item.name = "arrange";
    item.tooltip = GUI::L_str("Arrange");
    item.sprite_id = 3;
    item.is_toggable = false;
    item.action_event = EVT_GLTOOLBAR_ARRANGE;
    if (!m_toolbar.add_item(item))
        return false;

    if (!m_toolbar.add_separator())
        return false;

    item.name = "more";
    item.tooltip = GUI::L_str("Add instance");
    item.sprite_id = 4;
    item.is_toggable = false;
    item.action_event = EVT_GLTOOLBAR_MORE;
    if (!m_toolbar.add_item(item))
        return false;

    item.name = "fewer";
    item.tooltip = GUI::L_str("Remove instance");
    item.sprite_id = 5;
    item.is_toggable = false;
    item.action_event = EVT_GLTOOLBAR_FEWER;
    if (!m_toolbar.add_item(item))
        return false;

    if (!m_toolbar.add_separator())
        return false;

    item.name = "splitobjects";
    item.tooltip = GUI::L_str("Split to objects");
    item.sprite_id = 6;
    item.is_toggable = false;
    item.action_event = EVT_GLTOOLBAR_SPLIT_OBJECTS;
    if (!m_toolbar.add_item(item))
        return false;

    item.name = "splitvolumes";
    item.tooltip = GUI::L_str("Split to parts");
    item.sprite_id = 11;
    item.is_toggable = false;
    item.action_event = EVT_GLTOOLBAR_SPLIT_VOLUMES;
    if (!m_toolbar.add_item(item))
        return false;

    if (!m_toolbar.add_separator())
        return false;

    item.name = "layersediting";
    item.tooltip = GUI::L_str("Layers editing");
    item.sprite_id = 9;
    item.is_toggable = true;
    item.action_event = EVT_GLTOOLBAR_LAYERSEDITING;
    if (!m_toolbar.add_item(item))
        return false;

    if (!m_toolbar.add_separator())
        return false;

    enable_toolbar_item("add", true);

    return true;
}

#if ENABLE_USE_UNIQUE_GLCONTEXT
bool GLCanvas3D::_set_current()
{
    if ((m_canvas != nullptr) && (m_context != nullptr))
        return m_canvas->SetCurrent(*m_context);

    return false;
}
#endif ENABLE_USE_UNIQUE_GLCONTEXT

void GLCanvas3D::_resize(unsigned int w, unsigned int h)
{
    if ((m_canvas == nullptr) && (m_context == nullptr))
        return;

    // ensures that this canvas is current
#if ENABLE_USE_UNIQUE_GLCONTEXT
    _set_current();
#else
    set_current();
#endif // ENABLE_USE_UNIQUE_GLCONTEXT
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

        viewport_changed();

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
    Vec3d right((double)matrix[0], (double)matrix[4], (double)matrix[8]);
    Vec3d up((double)matrix[1], (double)matrix[5], (double)matrix[9]);
    Vec3d forward((double)matrix[2], (double)matrix[6], (double)matrix[10]);

    Vec3d bb_min = bbox.min;
    Vec3d bb_max = bbox.max;
    Vec3d bb_center = bbox.center();

    // bbox vertices in world space
    std::vector<Vec3d> vertices;
    vertices.reserve(8);
    vertices.push_back(bb_min);
    vertices.emplace_back(bb_max(0), bb_min(1), bb_min(2));
    vertices.emplace_back(bb_max(0), bb_max(1), bb_min(2));
    vertices.emplace_back(bb_min(0), bb_max(1), bb_min(2));
    vertices.emplace_back(bb_min(0), bb_min(1), bb_max(2));
    vertices.emplace_back(bb_max(0), bb_min(1), bb_max(2));
    vertices.push_back(bb_max);
    vertices.emplace_back(bb_min(0), bb_max(1), bb_max(2));

    double max_x = 0.0;
    double max_y = 0.0;

    // margin factor to give some empty space around the bbox
    double margin_factor = 1.25;

    for (const Vec3d v : vertices)
    {
        // project vertex on the plane perpendicular to camera forward axis
        Vec3d pos(v(0) - bb_center(0), v(1) - bb_center(1), v(2) - bb_center(2));
        Vec3d proj_on_plane = pos - pos.dot(forward) * forward;

        // calculates vertex coordinate along camera xy axes
        double x_on_plane = proj_on_plane.dot(right);
        double y_on_plane = proj_on_plane.dot(up);

        max_x = std::max(max_x, margin_factor * std::abs(x_on_plane));
        max_y = std::max(max_y, margin_factor * std::abs(y_on_plane));
    }

    if ((max_x == 0.0) || (max_y == 0.0))
        return -1.0f;

    max_x *= 2.0;
    max_y *= 2.0;

    const Size& cnv_size = get_canvas_size();
    return (float)std::min((double)cnv_size.get_width() / max_x, (double)cnv_size.get_height() / max_y);
}

void GLCanvas3D::_mark_volumes_for_layer_height() const
{
    const Print *print = (m_process == nullptr) ? nullptr : m_process->fff_print();
    if (print == nullptr)
        return;

    for (GLVolume* vol : m_volumes.volumes)
    {
        int object_id = vol->object_idx();
        int shader_id = m_layers_editing.get_shader_program_id();

        if (is_layers_editing_enabled() && (shader_id != -1) && vol->selected &&
            vol->has_layer_height_texture() && (object_id < (int)print->objects().size()))
        {
            vol->set_layer_height_texture_data(m_layers_editing.get_z_texture_id(), shader_id,
                print->get_object(object_id), _get_layers_editing_cursor_z_relative(), m_layers_editing.band_width);
        }
        else
            vol->reset_layer_height_texture_data();
    }
}

void GLCanvas3D::_refresh_if_shown_on_screen()
{
    if (_is_shown_on_screen())
    {
        const Size& cnv_size = get_canvas_size();
        _resize((unsigned int)cnv_size.get_width(), (unsigned int)cnv_size.get_height());

        // Because of performance problems on macOS, where PaintEvents are not delivered
        // frequently enough, we call render() here directly when we can.
        // We can't do that when m_force_zoom_to_bed_enabled == true, because then render()
        // ends up calling back here via _force_zoom_to_bed(), causing a stack overflow.
        if (m_canvas != nullptr) {
            m_force_zoom_to_bed_enabled ? m_canvas->Refresh() : render();
        }
    }
}

void GLCanvas3D::_camera_tranform() const
{
    ::glMatrixMode(GL_MODELVIEW);
    ::glLoadIdentity();

    ::glRotatef(-m_camera.get_theta(), 1.0f, 0.0f, 0.0f); // pitch
    ::glRotatef(m_camera.phi, 0.0f, 0.0f, 1.0f);          // yaw

    ::glTranslated(-m_camera.target(0), -m_camera.target(1), -m_camera.target(2));
}

void GLCanvas3D::_picking_pass() const
{
    const Vec2d& pos = m_mouse.position;

    if (m_picking_enabled && !m_mouse.dragging && (pos != Vec2d(DBL_MAX, DBL_MAX)))
    {
        // Render the object for picking.
        // FIXME This cannot possibly work in a multi - sampled context as the color gets mangled by the anti - aliasing.
        // Better to use software ray - casting on a bounding - box hierarchy.

        if (m_multisample_allowed)
            ::glDisable(GL_MULTISAMPLE);

        ::glDisable(GL_BLEND);
        ::glEnable(GL_DEPTH_TEST);

        ::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        _render_volumes(true);
        m_gizmos.render_current_gizmo_for_picking_pass(m_selection);

        if (m_multisample_allowed)
            ::glEnable(GL_MULTISAMPLE);

        int volume_id = -1;

        GLubyte color[4] = { 0, 0, 0, 0 };
        const Size& cnv_size = get_canvas_size();
        bool inside = (0 <= pos(0)) && (pos(0) < cnv_size.get_width()) && (0 <= pos(1)) && (pos(1) < cnv_size.get_height());
        if (inside)
        {
            ::glReadPixels(pos(0), cnv_size.get_height() - pos(1) - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, (void*)color);
            volume_id = color[0] + color[1] * 256 + color[2] * 256 * 256;
        }

        if ((0 <= volume_id) && (volume_id < (int)m_volumes.volumes.size()))
        {
            m_hover_volume_id = volume_id;
            m_gizmos.set_hover_id(-1);
        }
        else
        {
            m_hover_volume_id = -1;
            m_gizmos.set_hover_id(inside ? (254 - (int)color[2]) : -1);
        }

        _update_volumes_hover_state();

        // updates gizmos overlay
        if (!m_selection.is_empty())
        {
            std::string name = m_gizmos.update_hover_state(*this, pos, m_selection);
            if (!name.empty())
                set_tooltip(name);
        }
        else
            m_gizmos.reset_all_states();

        m_toolbar.update_hover_state(pos);
    }
}

void GLCanvas3D::_render_background() const
{
    ::glPushMatrix();
    ::glLoadIdentity();
    ::glMatrixMode(GL_PROJECTION);
    ::glPushMatrix();
    ::glLoadIdentity();

    // Draws a bluish bottom to top gradient over the complete screen.
    ::glDisable(GL_DEPTH_TEST);

    ::glBegin(GL_QUADS);
    ::glColor3f(0.0f, 0.0f, 0.0f);
    ::glVertex2f(-1.0f, -1.0f);
    ::glVertex2f(1.0f, -1.0f);

    if (m_dynamic_background_enabled && _is_any_volume_outside())
        ::glColor3fv(ERROR_BG_COLOR);
    else
        ::glColor3fv(DEFAULT_BG_COLOR);

    ::glVertex2f(1.0f, 1.0f);
    ::glVertex2f(-1.0f, 1.0f);
    ::glEnd();

    ::glEnable(GL_DEPTH_TEST);

    ::glPopMatrix();
    ::glMatrixMode(GL_MODELVIEW);
    ::glPopMatrix();
}

void GLCanvas3D::_render_bed(float theta) const
{
    m_bed.render(theta);
}

void GLCanvas3D::_render_axes(bool depth_test) const
{
    m_axes.render(depth_test);
}

void GLCanvas3D::_render_objects() const
{
    if (m_volumes.empty())
        return;

    ::glEnable(GL_LIGHTING);
    ::glEnable(GL_DEPTH_TEST);

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
                m_volumes.set_print_box((float)bed_bb.min(0), (float)bed_bb.min(1), 0.0f, (float)bed_bb.max(0), (float)bed_bb.max(1), (float)m_config->opt_float("max_print_height"));
                m_volumes.check_outside_state(m_config, nullptr);
            }
            // do not cull backfaces to show broken geometry, if any
            ::glDisable(GL_CULL_FACE);
        }

        m_shader.start_using();
        m_volumes.render_VBOs();
        m_shader.stop_using();

        if (m_picking_enabled)
            ::glEnable(GL_CULL_FACE);
    }
    else
    {
        // do not cull backfaces to show broken geometry, if any
        if (m_picking_enabled)
            ::glDisable(GL_CULL_FACE);

        m_volumes.render_legacy();

        if (m_picking_enabled)
            ::glEnable(GL_CULL_FACE);
    }

    ::glDisable(GL_LIGHTING);
}

void GLCanvas3D::_render_selection() const
{
    if (!m_gizmos.is_running())
        m_selection.render();
}

void GLCanvas3D::_render_warning_texture() const
{
    if (!m_warning_texture_enabled)
        return;

    m_warning_texture.render(*this);
}

void GLCanvas3D::_render_legend_texture() const
{
    if (!m_legend_texture_enabled)
        return;

    m_legend_texture.render(*this);
}

void GLCanvas3D::_render_layer_editing_overlay() const
{
    const Print *print = this->fff_print();
    if ((print == nullptr) || print->objects().empty())
        return;

    GLVolume* volume = nullptr;

    for (GLVolume* vol : m_volumes.volumes)
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
    int object_idx = volume->object_idx();
    if ((int)print->objects().size() <= object_idx)
        return;

    const PrintObject* print_object = print->get_object(object_idx);
    if (print_object == nullptr)
        return;

    m_layers_editing.render(*this, *print_object, *volume);
}

void GLCanvas3D::_render_volumes(bool fake_colors) const
{
    static const GLfloat INV_255 = 1.0f / 255.0f;

    if (!fake_colors)
        ::glEnable(GL_LIGHTING);

    // do not cull backfaces to show broken geometry, if any
    ::glDisable(GL_CULL_FACE);

    ::glEnable(GL_BLEND);
    ::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    ::glEnableClientState(GL_VERTEX_ARRAY);
    ::glEnableClientState(GL_NORMAL_ARRAY);

    unsigned int volume_id = 0;
    for (GLVolume* vol : m_volumes.volumes)
    {
        if (fake_colors)
        {
            // Object picking mode. Render the object with a color encoding the object index.
            unsigned int r = (volume_id & 0x000000FF) >> 0;
            unsigned int g = (volume_id & 0x0000FF00) >> 8;
            unsigned int b = (volume_id & 0x00FF0000) >> 16;
            ::glColor3f((GLfloat)r * INV_255, (GLfloat)g * INV_255, (GLfloat)b * INV_255);
        }
        else
        {
            vol->set_render_color();
            ::glColor4fv(vol->render_color);
        }

        if (!fake_colors || !vol->disabled)
            vol->render();

        ++volume_id;
    }

    ::glDisableClientState(GL_NORMAL_ARRAY);
    ::glDisableClientState(GL_VERTEX_ARRAY);
    ::glDisable(GL_BLEND);

    ::glEnable(GL_CULL_FACE);

    if (!fake_colors)
        ::glDisable(GL_LIGHTING);
}

void GLCanvas3D::_render_current_gizmo() const
{
    m_gizmos.render_current_gizmo(m_selection);
}

void GLCanvas3D::_render_gizmos_overlay() const
{
    m_gizmos.render_overlay(*this);
}

void GLCanvas3D::_render_toolbar() const
{
    _resize_toolbar();
    m_toolbar.render();
}

#if ENABLE_SHOW_CAMERA_TARGET
void GLCanvas3D::_render_camera_target() const
{
    double half_length = 5.0;

    ::glDisable(GL_DEPTH_TEST);

    ::glLineWidth(2.0f);
    ::glBegin(GL_LINES);
    // draw line for x axis
    ::glColor3f(1.0f, 0.0f, 0.0f);
    ::glVertex3d(m_camera.target(0) - half_length, m_camera.target(1), m_camera.target(2));
    ::glVertex3d(m_camera.target(0) + half_length, m_camera.target(1), m_camera.target(2));
    // draw line for y axis
    ::glColor3f(0.0f, 1.0f, 0.0f);
    ::glVertex3d(m_camera.target(0), m_camera.target(1) - half_length, m_camera.target(2));
    ::glVertex3d(m_camera.target(0), m_camera.target(1) + half_length, m_camera.target(2));
    ::glEnd();

    ::glBegin(GL_LINES);
    ::glColor3f(0.0f, 0.0f, 1.0f);
    ::glVertex3d(m_camera.target(0), m_camera.target(1), m_camera.target(2) - half_length);
    ::glVertex3d(m_camera.target(0), m_camera.target(1), m_camera.target(2) + half_length);
    ::glEnd();
}
#endif // ENABLE_SHOW_CAMERA_TARGET

void GLCanvas3D::_update_volumes_hover_state() const
{
    for (GLVolume* v : m_volumes.volumes)
    {
        v->hover = false;
    }

    if (m_hover_volume_id == -1)
        return;

    GLVolume* volume = m_volumes.volumes[m_hover_volume_id];

    switch (m_selection.get_mode())
    {
    case Selection::Volume:
    {
        volume->hover = true;
        break;
    }
    case Selection::Instance:
    {
        int object_idx = volume->object_idx();
        int instance_idx = volume->instance_idx();

        for (GLVolume* v : m_volumes.volumes)
        {
            if ((v->object_idx() == object_idx) && (v->instance_idx() == instance_idx))
                v->hover = true;
        }

        break;
    }
    }
}

void GLCanvas3D::_update_gizmos_data()
{
    if (!m_gizmos.is_enabled())
        return;

    bool enable_move_z = !m_selection.is_wipe_tower();
    m_gizmos.enable_grabber(Gizmos::Move, 2, enable_move_z);
    bool enable_scale_xyz = m_selection.is_single_full_instance() || m_selection.is_single_volume() || m_selection.is_single_modifier();
    for (int i = 0; i < 6; ++i)
    {
        m_gizmos.enable_grabber(Gizmos::Scale, i, enable_scale_xyz);
    }

    if (m_selection.is_single_full_instance())
    {
#if ENABLE_MODELVOLUME_TRANSFORM
        // all volumes in the selection belongs to the same instance, any of them contains the needed data, so we take the first
        const GLVolume* volume = m_volumes.volumes[*m_selection.get_volume_idxs().begin()];
        m_gizmos.set_scale(volume->get_instance_scaling_factor());
#if ENABLE_WORLD_ROTATIONS
        m_gizmos.set_rotation(Vec3d::Zero());
#else
        m_gizmos.set_rotation(volume->get_instance_rotation());
#endif // ENABLE_WORLD_ROTATIONS
        ModelObject* model_object = m_model->objects[m_selection.get_object_idx()];
        m_gizmos.set_flattening_data(model_object);
        m_gizmos.set_model_object_ptr(model_object);
#else
        ModelObject* model_object = m_model->objects[m_selection.get_object_idx()];
        ModelInstance* model_instance = model_object->instances[m_selection.get_instance_idx()];
        m_gizmos.set_scale(model_instance->get_scaling_factor());
#if ENABLE_WORLD_ROTATIONS
        m_gizmos.set_rotation(Vec3d::Zero());
#else
        m_gizmos.set_rotation(model_instance->get_rotation());
#endif // ENABLE_WORLD_ROTATIONS
        m_gizmos.set_flattening_data(model_object);
        m_gizmos.set_model_object_ptr(model_object);
#endif // ENABLE_MODELVOLUME_TRANSFORM
    }
#if ENABLE_MODELVOLUME_TRANSFORM
    else if (m_selection.is_single_volume() || m_selection.is_single_modifier())
    {
        const GLVolume* volume = m_volumes.volumes[*m_selection.get_volume_idxs().begin()];
        m_gizmos.set_scale(volume->get_volume_scaling_factor());
#if ENABLE_WORLD_ROTATIONS
        m_gizmos.set_rotation(Vec3d::Zero());
#else
        m_gizmos.set_rotation(volume->get_volume_rotation());
#endif // ENABLE_WORLD_ROTATIONS
        m_gizmos.set_flattening_data(nullptr);
        m_gizmos.set_model_object_ptr(nullptr);
    }
#endif // ENABLE_MODELVOLUME_TRANSFORM
    else
    {
        m_gizmos.set_scale(Vec3d::Ones());
        m_gizmos.set_rotation(Vec3d::Zero());
        m_gizmos.set_flattening_data(m_selection.is_from_single_object() ? m_model->objects[m_selection.get_object_idx()] : nullptr);
        m_gizmos.set_model_object_ptr(nullptr);
    }
}

float GLCanvas3D::_get_layers_editing_cursor_z_relative() const
{
    return m_layers_editing.get_cursor_z_relative(*this);
}

void GLCanvas3D::_perform_layer_editing_action(wxMouseEvent* evt)
{
    int object_idx_selected = m_layers_editing.last_object_id;
    if (object_idx_selected == -1)
        return;

    const Print *print = this->fff_print();
    if (print == nullptr)
        return;

    const PrintObject* selected_obj = print->get_object(object_idx_selected);
    if (selected_obj == nullptr)
        return;

    // A volume is selected. Test, whether hovering over a layer thickness bar.
    if (evt != nullptr)
    {
        const Rect& rect = LayersEditing::get_bar_rect_screen(*this);
        float b = rect.get_bottom();
        m_layers_editing.last_z = unscale<double>(selected_obj->size(2)) * (b - evt->GetY() - 1.0f) / (b - rect.get_top());
        m_layers_editing.last_action = evt->ShiftDown() ? (evt->RightIsDown() ? 3 : 2) : (evt->RightIsDown() ? 0 : 1);
    }

    // Mark the volume as modified, so Print will pick its layer height profile ? Where to mark it ?
    // Start a timer to refresh the print ? schedule_background_process() ?
    // The PrintObject::adjust_layer_height_profile() call adjusts the profile of its associated ModelObject, it does not modify the profile of the PrintObject itself,
    // therefore it is safe to call it while the background processing is running.
    const_cast<PrintObject*>(selected_obj)->adjust_layer_height_profile(m_layers_editing.last_z, m_layers_editing.strength, m_layers_editing.band_width, m_layers_editing.last_action);

    // searches the id of the first volume of the selected object
    int volume_idx = 0;
    for (int i = 0; i < object_idx_selected; ++i)
    {
        const PrintObject* obj = print->get_object(i);
        if (obj != nullptr)
        {
            for (int j = 0; j < (int)obj->region_volumes.size(); ++j)
            {
                volume_idx += (int)obj->region_volumes[j].size();
            }
        }
    }

    m_volumes.volumes[volume_idx]->generate_layer_height_texture(selected_obj, 1);
    _refresh_if_shown_on_screen();

    // Automatic action on mouse down with the same coordinate.
    _start_timer();
}

Vec3d GLCanvas3D::_mouse_to_3d(const Point& mouse_pos, float* z)
{
    if (m_canvas == nullptr)
        return Vec3d(DBL_MAX, DBL_MAX, DBL_MAX);

    _camera_tranform();

    GLint viewport[4];
    ::glGetIntegerv(GL_VIEWPORT, viewport);
    GLdouble modelview_matrix[16];
    ::glGetDoublev(GL_MODELVIEW_MATRIX, modelview_matrix);
    GLdouble projection_matrix[16];
    ::glGetDoublev(GL_PROJECTION_MATRIX, projection_matrix);

    GLint y = viewport[3] - (GLint)mouse_pos(1);
    GLfloat mouse_z;
    if (z == nullptr)
        ::glReadPixels((GLint)mouse_pos(0), y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, (void*)&mouse_z);
    else
        mouse_z = *z;

    GLdouble out_x, out_y, out_z;
    ::gluUnProject((GLdouble)mouse_pos(0), (GLdouble)y, (GLdouble)mouse_z, modelview_matrix, projection_matrix, viewport, &out_x, &out_y, &out_z);
    return Vec3d((double)out_x, (double)out_y, (double)out_z);
}

Vec3d GLCanvas3D::_mouse_to_bed_3d(const Point& mouse_pos)
{
    return mouse_ray(mouse_pos).intersect_plane(0.0);
}

Linef3 GLCanvas3D::mouse_ray(const Point& mouse_pos)
{
    float z0 = 0.0f;
    float z1 = 1.0f;
    return Linef3(_mouse_to_3d(mouse_pos, &z0), _mouse_to_3d(mouse_pos, &z1));
}

void GLCanvas3D::_start_timer()
{
    m_timer.Start(100, wxTIMER_CONTINUOUS);
}

void GLCanvas3D::_stop_timer()
{
    m_timer.Stop();
}

void GLCanvas3D::_load_print_toolpaths()
{
#if !ENABLE_USE_UNIQUE_GLCONTEXT
    // ensures this canvas is current
    if (!set_current())
        return;
#endif // !ENABLE_USE_UNIQUE_GLCONTEXT

    const Print *print = this->fff_print();
    if (print == nullptr)
        return;

    if (!print->is_step_done(psSkirt) || !print->is_step_done(psBrim))
        return;

    if (!print->has_skirt() && (print->config().brim_width.value == 0))
        return;

    const float color[] = { 0.5f, 1.0f, 0.5f, 1.0f }; // greenish

    // number of skirt layers
    size_t total_layer_count = 0;
    for (const PrintObject* print_object : print->objects())
    {
        total_layer_count = std::max(total_layer_count, print_object->total_layer_count());
    }
    size_t skirt_height = print->has_infinite_skirt() ? total_layer_count : std::min<size_t>(print->config().skirt_height.value, total_layer_count);
    if ((skirt_height == 0) && (print->config().brim_width.value > 0))
        skirt_height = 1;

    // get first skirt_height layers (maybe this should be moved to a PrintObject method?)
    const PrintObject* object0 = print->objects().front();
    std::vector<float> print_zs;
    print_zs.reserve(skirt_height * 2);
    for (size_t i = 0; i < std::min(skirt_height, object0->layers().size()); ++i)
    {
        print_zs.push_back(float(object0->layers()[i]->print_z));
    }
    //FIXME why there are support layers?
    for (size_t i = 0; i < std::min(skirt_height, object0->support_layers().size()); ++i)
    {
        print_zs.push_back(float(object0->support_layers()[i]->print_z));
    }
    sort_remove_duplicates(print_zs);
    if (print_zs.size() > skirt_height)
        print_zs.erase(print_zs.begin() + skirt_height, print_zs.end());

    m_volumes.volumes.emplace_back(new GLVolume(color));
    GLVolume& volume = *m_volumes.volumes.back();
    for (size_t i = 0; i < skirt_height; ++i) {
        volume.print_zs.push_back(print_zs[i]);
        volume.offsets.push_back(volume.indexed_vertex_array.quad_indices.size());
        volume.offsets.push_back(volume.indexed_vertex_array.triangle_indices.size());
        if (i == 0)
            _3DScene::extrusionentity_to_verts(print->brim(), print_zs[i], Point(0, 0), volume);

        _3DScene::extrusionentity_to_verts(print->skirt(), print_zs[i], Point(0, 0), volume);
    }
    volume.bounding_box = volume.indexed_vertex_array.bounding_box();
    volume.indexed_vertex_array.finalize_geometry(m_use_VBOs && m_initialized);
}

void GLCanvas3D::_load_print_object_toolpaths(const PrintObject& print_object, const std::vector<std::string>& str_tool_colors)
{
    std::vector<float> tool_colors = _parse_colors(str_tool_colors);

    struct Ctxt
    {
        const Points                *shifted_copies;
        std::vector<const Layer*>    layers;
        bool                         has_perimeters;
        bool                         has_infill;
        bool                         has_support;
        const std::vector<float>*    tool_colors;

        // Number of vertices (each vertex is 6x4=24 bytes long)
        static const size_t          alloc_size_max() { return 131072; } // 3.15MB
        //        static const size_t          alloc_size_max    () { return 65536; } // 1.57MB 
        //        static const size_t          alloc_size_max    () { return 32768; } // 786kB
        static const size_t          alloc_size_reserve() { return alloc_size_max() * 2; }

        static const float*          color_perimeters() { static float color[4] = { 1.0f, 1.0f, 0.0f, 1.f }; return color; } // yellow
        static const float*          color_infill() { static float color[4] = { 1.0f, 0.5f, 0.5f, 1.f }; return color; } // redish
        static const float*          color_support() { static float color[4] = { 0.5f, 1.0f, 0.5f, 1.f }; return color; } // greenish

        // For cloring by a tool, return a parsed color.
        bool                         color_by_tool() const { return tool_colors != nullptr; }
        size_t                       number_tools()  const { return this->color_by_tool() ? tool_colors->size() / 4 : 0; }
        const float*                 color_tool(size_t tool) const { return tool_colors->data() + tool * 4; }
        int                          volume_idx(int extruder, int feature) const
        {
            return this->color_by_tool() ? std::min<int>(this->number_tools() - 1, std::max<int>(extruder - 1, 0)) : feature;
        }
    } ctxt;

    ctxt.shifted_copies = &print_object.copies();

    // order layers by print_z
    ctxt.layers.reserve(print_object.layers().size() + print_object.support_layers().size());
    for (const Layer *layer : print_object.layers())
        ctxt.layers.push_back(layer);
    for (const Layer *layer : print_object.support_layers())
        ctxt.layers.push_back(layer);
    std::sort(ctxt.layers.begin(), ctxt.layers.end(), [](const Layer *l1, const Layer *l2) { return l1->print_z < l2->print_z; });

    // Maximum size of an allocation block: 32MB / sizeof(float)
    ctxt.has_perimeters = print_object.is_step_done(posPerimeters);
    ctxt.has_infill = print_object.is_step_done(posInfill);
    ctxt.has_support = print_object.is_step_done(posSupportMaterial);
    ctxt.tool_colors = tool_colors.empty() ? nullptr : &tool_colors;

    BOOST_LOG_TRIVIAL(debug) << "Loading print object toolpaths in parallel - start";

    //FIXME Improve the heuristics for a grain size.
    size_t          grain_size = std::max(ctxt.layers.size() / 16, size_t(1));
    tbb::spin_mutex new_volume_mutex;
    auto            new_volume = [this, &new_volume_mutex](const float *color) -> GLVolume* {
        auto *volume = new GLVolume(color);
        new_volume_mutex.lock();
        m_volumes.volumes.emplace_back(volume);
        new_volume_mutex.unlock();
        return volume;
    };
    const size_t   volumes_cnt_initial = m_volumes.volumes.size();
    std::vector<GLVolumeCollection> volumes_per_thread(ctxt.layers.size());
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, ctxt.layers.size(), grain_size),
        [&ctxt, &new_volume](const tbb::blocked_range<size_t>& range) {
        GLVolumePtrs vols;
        if (ctxt.color_by_tool()) {
            for (size_t i = 0; i < ctxt.number_tools(); ++i)
                vols.emplace_back(new_volume(ctxt.color_tool(i)));
        }
        else
            vols = { new_volume(ctxt.color_perimeters()), new_volume(ctxt.color_infill()), new_volume(ctxt.color_support()) };
        for (GLVolume *vol : vols)
            vol->indexed_vertex_array.reserve(ctxt.alloc_size_reserve());
        for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++idx_layer) {
            const Layer *layer = ctxt.layers[idx_layer];
            for (size_t i = 0; i < vols.size(); ++i) {
                GLVolume &vol = *vols[i];
                if (vol.print_zs.empty() || vol.print_zs.back() != layer->print_z) {
                    vol.print_zs.push_back(layer->print_z);
                    vol.offsets.push_back(vol.indexed_vertex_array.quad_indices.size());
                    vol.offsets.push_back(vol.indexed_vertex_array.triangle_indices.size());
                }
            }
            for (const Point &copy : *ctxt.shifted_copies) {
                for (const LayerRegion *layerm : layer->regions()) {
                    if (ctxt.has_perimeters)
                        _3DScene::extrusionentity_to_verts(layerm->perimeters, float(layer->print_z), copy,
                        *vols[ctxt.volume_idx(layerm->region()->config().perimeter_extruder.value, 0)]);
                    if (ctxt.has_infill) {
                        for (const ExtrusionEntity *ee : layerm->fills.entities) {
                            // fill represents infill extrusions of a single island.
                            const auto *fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                            if (!fill->entities.empty())
                                _3DScene::extrusionentity_to_verts(*fill, float(layer->print_z), copy,
                                *vols[ctxt.volume_idx(
                                is_solid_infill(fill->entities.front()->role()) ?
                                layerm->region()->config().solid_infill_extruder :
                                layerm->region()->config().infill_extruder,
                                1)]);
                        }
                    }
                }
                if (ctxt.has_support) {
                    const SupportLayer *support_layer = dynamic_cast<const SupportLayer*>(layer);
                    if (support_layer) {
                        for (const ExtrusionEntity *extrusion_entity : support_layer->support_fills.entities)
                            _3DScene::extrusionentity_to_verts(extrusion_entity, float(layer->print_z), copy,
                            *vols[ctxt.volume_idx(
                            (extrusion_entity->role() == erSupportMaterial) ?
                            support_layer->object()->config().support_material_extruder :
                            support_layer->object()->config().support_material_interface_extruder,
                            2)]);
                    }
                }
            }
            for (size_t i = 0; i < vols.size(); ++i) {
                GLVolume &vol = *vols[i];
                if (vol.indexed_vertex_array.vertices_and_normals_interleaved.size() / 6 > ctxt.alloc_size_max()) {
                    // Store the vertex arrays and restart their containers, 
                    vols[i] = new_volume(vol.color);
                    GLVolume &vol_new = *vols[i];
                    // Assign the large pre-allocated buffers to the new GLVolume.
                    vol_new.indexed_vertex_array = std::move(vol.indexed_vertex_array);
                    // Copy the content back to the old GLVolume.
                    vol.indexed_vertex_array = vol_new.indexed_vertex_array;
                    // Finalize a bounding box of the old GLVolume.
                    vol.bounding_box = vol.indexed_vertex_array.bounding_box();
                    // Clear the buffers, but keep them pre-allocated.
                    vol_new.indexed_vertex_array.clear();
                    // Just make sure that clear did not clear the reserved memory.
                    vol_new.indexed_vertex_array.reserve(ctxt.alloc_size_reserve());
                }
            }
        }
        for (GLVolume *vol : vols) {
            vol->bounding_box = vol->indexed_vertex_array.bounding_box();
            vol->indexed_vertex_array.shrink_to_fit();
        }
    });

    BOOST_LOG_TRIVIAL(debug) << "Loading print object toolpaths in parallel - finalizing results";
    // Remove empty volumes from the newly added volumes.
    m_volumes.volumes.erase(
        std::remove_if(m_volumes.volumes.begin() + volumes_cnt_initial, m_volumes.volumes.end(),
        [](const GLVolume *volume) { return volume->empty(); }),
        m_volumes.volumes.end());
    for (size_t i = volumes_cnt_initial; i < m_volumes.volumes.size(); ++i)
        m_volumes.volumes[i]->indexed_vertex_array.finalize_geometry(m_use_VBOs && m_initialized);

    BOOST_LOG_TRIVIAL(debug) << "Loading print object toolpaths in parallel - end";
}

void GLCanvas3D::_load_wipe_tower_toolpaths(const std::vector<std::string>& str_tool_colors)
{
    const Print *print = this->fff_print();
    if ((print == nullptr) || print->wipe_tower_data().tool_changes.empty())
        return;

    if (!print->is_step_done(psWipeTower))
        return;

    std::vector<float> tool_colors = _parse_colors(str_tool_colors);

    struct Ctxt
    {
        const Print                 *print;
        const std::vector<float>    *tool_colors;
        WipeTower::xy                wipe_tower_pos;
        float                        wipe_tower_angle;

        // Number of vertices (each vertex is 6x4=24 bytes long)
        static const size_t          alloc_size_max() { return 131072; } // 3.15MB
        static const size_t          alloc_size_reserve() { return alloc_size_max() * 2; }

        static const float*          color_support() { static float color[4] = { 0.5f, 1.0f, 0.5f, 1.f }; return color; } // greenish

        // For cloring by a tool, return a parsed color.
        bool                         color_by_tool() const { return tool_colors != nullptr; }
        size_t                       number_tools()  const { return this->color_by_tool() ? tool_colors->size() / 4 : 0; }
        const float*                 color_tool(size_t tool) const { return tool_colors->data() + tool * 4; }
        int                          volume_idx(int tool, int feature) const
        {
            return this->color_by_tool() ? std::min<int>(this->number_tools() - 1, std::max<int>(tool, 0)) : feature;
        }

        const std::vector<WipeTower::ToolChangeResult>& tool_change(size_t idx) {
            const auto &tool_changes = print->wipe_tower_data().tool_changes;
            return priming.empty() ?
                ((idx == tool_changes.size()) ? final : tool_changes[idx]) :
                ((idx == 0) ? priming : (idx == tool_changes.size() + 1) ? final : tool_changes[idx - 1]);
        }
        std::vector<WipeTower::ToolChangeResult> priming;
        std::vector<WipeTower::ToolChangeResult> final;
    } ctxt;

    ctxt.print = print;
    ctxt.tool_colors = tool_colors.empty() ? nullptr : &tool_colors;
    if (print->wipe_tower_data().priming && print->config().single_extruder_multi_material_priming)
        ctxt.priming.emplace_back(*print->wipe_tower_data().priming.get());
    if (print->wipe_tower_data().final_purge)
        ctxt.final.emplace_back(*print->wipe_tower_data().final_purge.get());

    ctxt.wipe_tower_angle = ctxt.print->config().wipe_tower_rotation_angle.value/180.f * PI;
    ctxt.wipe_tower_pos = WipeTower::xy(ctxt.print->config().wipe_tower_x.value, ctxt.print->config().wipe_tower_y.value);

    BOOST_LOG_TRIVIAL(debug) << "Loading wipe tower toolpaths in parallel - start";

    //FIXME Improve the heuristics for a grain size.
    size_t          n_items = print->wipe_tower_data().tool_changes.size() + (ctxt.priming.empty() ? 0 : 1);
    size_t          grain_size = std::max(n_items / 128, size_t(1));
    tbb::spin_mutex new_volume_mutex;
    auto            new_volume = [this, &new_volume_mutex](const float *color) -> GLVolume* {
        auto *volume = new GLVolume(color);
        new_volume_mutex.lock();
        m_volumes.volumes.emplace_back(volume);
        new_volume_mutex.unlock();
        return volume;
    };
    const size_t   volumes_cnt_initial = m_volumes.volumes.size();
    std::vector<GLVolumeCollection> volumes_per_thread(n_items);
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, n_items, grain_size),
        [&ctxt, &new_volume](const tbb::blocked_range<size_t>& range) {
        // Bounding box of this slab of a wipe tower.
        GLVolumePtrs vols;
        if (ctxt.color_by_tool()) {
            for (size_t i = 0; i < ctxt.number_tools(); ++i)
                vols.emplace_back(new_volume(ctxt.color_tool(i)));
        }
        else
            vols = { new_volume(ctxt.color_support()) };
        for (GLVolume *volume : vols)
            volume->indexed_vertex_array.reserve(ctxt.alloc_size_reserve());
        for (size_t idx_layer = range.begin(); idx_layer < range.end(); ++idx_layer) {
            const std::vector<WipeTower::ToolChangeResult> &layer = ctxt.tool_change(idx_layer);
            for (size_t i = 0; i < vols.size(); ++i) {
                GLVolume &vol = *vols[i];
                if (vol.print_zs.empty() || vol.print_zs.back() != layer.front().print_z) {
                    vol.print_zs.push_back(layer.front().print_z);
                    vol.offsets.push_back(vol.indexed_vertex_array.quad_indices.size());
                    vol.offsets.push_back(vol.indexed_vertex_array.triangle_indices.size());
                }
            }
            for (const WipeTower::ToolChangeResult &extrusions : layer) {
                for (size_t i = 1; i < extrusions.extrusions.size();) {
                    const WipeTower::Extrusion &e = extrusions.extrusions[i];
                    if (e.width == 0.) {
                        ++i;
                        continue;
                    }
                    size_t j = i + 1;
                    if (ctxt.color_by_tool())
                        for (; j < extrusions.extrusions.size() && extrusions.extrusions[j].tool == e.tool && extrusions.extrusions[j].width > 0.f; ++j);
                    else
                        for (; j < extrusions.extrusions.size() && extrusions.extrusions[j].width > 0.f; ++j);
                    size_t              n_lines = j - i;
                    Lines               lines;
                    std::vector<double> widths;
                    std::vector<double> heights;
                    lines.reserve(n_lines);
                    widths.reserve(n_lines);
                    heights.assign(n_lines, extrusions.layer_height);
                    WipeTower::Extrusion e_prev = extrusions.extrusions[i-1];

                    if (!extrusions.priming) { // wipe tower extrusions describe the wipe tower at the origin with no rotation
                        e_prev.pos.rotate(ctxt.wipe_tower_angle);
                        e_prev.pos.translate(ctxt.wipe_tower_pos);
                    }

                    for (; i < j; ++i) {
                        WipeTower::Extrusion e = extrusions.extrusions[i];
                        assert(e.width > 0.f);
                        if (!extrusions.priming) {
                            e.pos.rotate(ctxt.wipe_tower_angle);
                            e.pos.translate(ctxt.wipe_tower_pos);
                        }

                        lines.emplace_back(Point::new_scale(e_prev.pos.x, e_prev.pos.y), Point::new_scale(e.pos.x, e.pos.y));
                        widths.emplace_back(e.width);

                        e_prev = e;
                    }
                    _3DScene::thick_lines_to_verts(lines, widths, heights, lines.front().a == lines.back().b, extrusions.print_z,
                        *vols[ctxt.volume_idx(e.tool, 0)]);
                }
            }
        }
        for (size_t i = 0; i < vols.size(); ++i) {
            GLVolume &vol = *vols[i];
            if (vol.indexed_vertex_array.vertices_and_normals_interleaved.size() / 6 > ctxt.alloc_size_max()) {
                // Store the vertex arrays and restart their containers, 
                vols[i] = new_volume(vol.color);
                GLVolume &vol_new = *vols[i];
                // Assign the large pre-allocated buffers to the new GLVolume.
                vol_new.indexed_vertex_array = std::move(vol.indexed_vertex_array);
                // Copy the content back to the old GLVolume.
                vol.indexed_vertex_array = vol_new.indexed_vertex_array;
                // Finalize a bounding box of the old GLVolume.
                vol.bounding_box = vol.indexed_vertex_array.bounding_box();
                // Clear the buffers, but keep them pre-allocated.
                vol_new.indexed_vertex_array.clear();
                // Just make sure that clear did not clear the reserved memory.
                vol_new.indexed_vertex_array.reserve(ctxt.alloc_size_reserve());
            }
        }
        for (GLVolume *vol : vols) {
            vol->bounding_box = vol->indexed_vertex_array.bounding_box();
            vol->indexed_vertex_array.shrink_to_fit();
        }
    });

    BOOST_LOG_TRIVIAL(debug) << "Loading wipe tower toolpaths in parallel - finalizing results";
    // Remove empty volumes from the newly added volumes.
    m_volumes.volumes.erase(
        std::remove_if(m_volumes.volumes.begin() + volumes_cnt_initial, m_volumes.volumes.end(),
        [](const GLVolume *volume) { return volume->empty(); }),
        m_volumes.volumes.end());
    for (size_t i = volumes_cnt_initial; i < m_volumes.volumes.size(); ++i)
        m_volumes.volumes[i]->indexed_vertex_array.finalize_geometry(m_use_VBOs && m_initialized);

    BOOST_LOG_TRIVIAL(debug) << "Loading wipe tower toolpaths in parallel - end";
}

static inline int hex_digit_to_int(const char c)
{
    return
        (c >= '0' && c <= '9') ? int(c - '0') :
        (c >= 'A' && c <= 'F') ? int(c - 'A') + 10 :
        (c >= 'a' && c <= 'f') ? int(c - 'a') + 10 : -1;
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
            default:
                return 0.0f;
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
            default:
                return GCodePreviewData::Color::Dummy;
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
    size_t initial_volumes_count = m_volumes.volumes.size();

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
        m_gcode_preview_volume_index.first_volumes.emplace_back(GCodePreviewVolumeIndex::Extrusion, (unsigned int)filter.role, (unsigned int)m_volumes.volumes.size());
        GLVolume* volume = new GLVolume(Helper::path_color(preview_data, tool_colors, filter.value).rgba);
        if (volume != nullptr)
        {
            filter.volume = volume;
            volume->is_extrusion_path = true;
            m_volumes.volumes.emplace_back(volume);
        }
        else
        {
            // an error occourred - restore to previous state and return
            m_gcode_preview_volume_index.first_volumes.pop_back();
            if (initial_volumes_count != m_volumes.volumes.size())
            {
                GLVolumePtrs::iterator begin = m_volumes.volumes.begin() + initial_volumes_count;
                GLVolumePtrs::iterator end = m_volumes.volumes.end();
                for (GLVolumePtrs::iterator it = begin; it < end; ++it)
                {
                    GLVolume* volume = *it;
                    delete volume;
                }
                m_volumes.volumes.erase(begin, end);
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
    if (m_volumes.volumes.size() > initial_volumes_count)
    {
        for (size_t i = initial_volumes_count; i < m_volumes.volumes.size(); ++i)
        {
            GLVolume* volume = m_volumes.volumes[i];
            volume->bounding_box = volume->indexed_vertex_array.bounding_box();
            volume->indexed_vertex_array.finalize_geometry(m_use_VBOs && m_initialized);
        }
    }
}

void GLCanvas3D::_load_gcode_travel_paths(const GCodePreviewData& preview_data, const std::vector<float>& tool_colors)
{
    size_t initial_volumes_count = m_volumes.volumes.size();
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
        if (initial_volumes_count != m_volumes.volumes.size())
        {
            GLVolumePtrs::iterator begin = m_volumes.volumes.begin() + initial_volumes_count;
            GLVolumePtrs::iterator end = m_volumes.volumes.end();
            for (GLVolumePtrs::iterator it = begin; it < end; ++it)
            {
                GLVolume* volume = *it;
                delete volume;
            }
            m_volumes.volumes.erase(begin, end);
        }

        return;
    }

    // finalize volumes and sends geometry to gpu
    if (m_volumes.volumes.size() > initial_volumes_count)
    {
        for (size_t i = initial_volumes_count; i < m_volumes.volumes.size(); ++i)
        {
            GLVolume* volume = m_volumes.volumes[i];
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
            m_volumes.volumes.emplace_back(volume);
        }
    }

    // populates volumes
    for (const GCodePreviewData::Travel::Polyline& polyline : preview_data.travel.polylines)
    {
        TypesList::iterator type = std::find(types.begin(), types.end(), Type(polyline.type));
        if (type != types.end())
        {
            type->volume->print_zs.push_back(unscale<double>(polyline.polyline.bounding_box().min(2)));
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
            m_volumes.volumes.emplace_back(volume);
        }
    }

    // populates volumes
    for (const GCodePreviewData::Travel::Polyline& polyline : preview_data.travel.polylines)
    {
        FeedratesList::iterator feedrate = std::find(feedrates.begin(), feedrates.end(), Feedrate(polyline.feedrate));
        if (feedrate != feedrates.end())
        {
            feedrate->volume->print_zs.push_back(unscale<double>(polyline.polyline.bounding_box().min(2)));
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
            m_volumes.volumes.emplace_back(volume);
        }
    }

    // populates volumes
    for (const GCodePreviewData::Travel::Polyline& polyline : preview_data.travel.polylines)
    {
        ToolsList::iterator tool = std::find(tools.begin(), tools.end(), Tool(polyline.extruder_id));
        if (tool != tools.end())
        {
            tool->volume->print_zs.push_back(unscale<double>(polyline.polyline.bounding_box().min(2)));
            tool->volume->offsets.push_back(tool->volume->indexed_vertex_array.quad_indices.size());
            tool->volume->offsets.push_back(tool->volume->indexed_vertex_array.triangle_indices.size());

            _3DScene::polyline3_to_verts(polyline.polyline, preview_data.travel.width, preview_data.travel.height, *tool->volume);
        }
    }

    return true;
}

void GLCanvas3D::_load_gcode_retractions(const GCodePreviewData& preview_data)
{
    m_gcode_preview_volume_index.first_volumes.emplace_back(GCodePreviewVolumeIndex::Retraction, 0, (unsigned int)m_volumes.volumes.size());

    // nothing to render, return
    if (preview_data.retraction.positions.empty())
        return;

    GLVolume* volume = new GLVolume(preview_data.retraction.color.rgba);
    if (volume != nullptr)
    {
        m_volumes.volumes.emplace_back(volume);

        GCodePreviewData::Retraction::PositionsList copy(preview_data.retraction.positions);
        std::sort(copy.begin(), copy.end(), [](const GCodePreviewData::Retraction::Position& p1, const GCodePreviewData::Retraction::Position& p2){ return p1.position(2) < p2.position(2); });

        for (const GCodePreviewData::Retraction::Position& position : copy)
        {
            volume->print_zs.push_back(unscale<double>(position.position(2)));
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
    m_gcode_preview_volume_index.first_volumes.emplace_back(GCodePreviewVolumeIndex::Unretraction, 0, (unsigned int)m_volumes.volumes.size());

    // nothing to render, return
    if (preview_data.unretraction.positions.empty())
        return;

    GLVolume* volume = new GLVolume(preview_data.unretraction.color.rgba);
    if (volume != nullptr)
    {
        m_volumes.volumes.emplace_back(volume);

        GCodePreviewData::Retraction::PositionsList copy(preview_data.unretraction.positions);
        std::sort(copy.begin(), copy.end(), [](const GCodePreviewData::Retraction::Position& p1, const GCodePreviewData::Retraction::Position& p2){ return p1.position(2) < p2.position(2); });

        for (const GCodePreviewData::Retraction::Position& position : copy)
        {
            volume->print_zs.push_back(unscale<double>(position.position(2)));
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
    size_t initial_volumes_count = m_volumes.volumes.size();
    m_gcode_preview_volume_index.first_volumes.emplace_back(GCodePreviewVolumeIndex::Shell, 0, (unsigned int)initial_volumes_count);

    const Print *print = this->fff_print();
    if (print->objects().empty())
        // nothing to render, return
        return;

    // adds objects' volumes 
    unsigned int object_id = 0;
    for (const PrintObject* obj : print->objects())
    {
        const ModelObject* model_obj = obj->model_object();

        std::vector<int> instance_ids(model_obj->instances.size());
        for (int i = 0; i < (int)model_obj->instances.size(); ++i)
        {
            instance_ids[i] = i;
        }

        m_volumes.load_object(model_obj, object_id, instance_ids, "object", m_use_VBOs && m_initialized);

        ++object_id;
    }

    if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF) {
        // adds wipe tower's volume
        double max_z = print->objects()[0]->model_object()->get_model()->bounding_box().max(2);
        const PrintConfig& config = print->config();
        unsigned int extruders_count = config.nozzle_diameter.size();
        if ((extruders_count > 1) && config.single_extruder_multi_material && config.wipe_tower && !config.complete_objects) {
            float depth = print->get_wipe_tower_depth();
            if (!print->is_step_done(psWipeTower))
                depth = (900.f/config.wipe_tower_width) * (float)(extruders_count - 1) ;
            m_volumes.load_wipe_tower_preview(1000, config.wipe_tower_x, config.wipe_tower_y, config.wipe_tower_width, depth, max_z, config.wipe_tower_rotation_angle,
                                              m_use_VBOs && m_initialized, !print->is_step_done(psWipeTower), print->config().nozzle_diameter.values[0] * 1.25f * 4.5f);
        }
    }
}

void GLCanvas3D::_update_gcode_volumes_visibility(const GCodePreviewData& preview_data)
{
    unsigned int size = (unsigned int)m_gcode_preview_volume_index.first_volumes.size();
    for (unsigned int i = 0; i < size; ++i)
    {
        GLVolumePtrs::iterator begin = m_volumes.volumes.begin() + m_gcode_preview_volume_index.first_volumes[i].id;
        GLVolumePtrs::iterator end = (i + 1 < size) ? m_volumes.volumes.begin() + m_gcode_preview_volume_index.first_volumes[i + 1].id : m_volumes.volumes.end();

        for (GLVolumePtrs::iterator it = begin; it != end; ++it)
        {
            GLVolume* volume = *it;

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

void GLCanvas3D::_update_toolpath_volumes_outside_state()
{
    // tolerance to avoid false detection at bed edges
    static const double tolerance_x = 0.05;
    static const double tolerance_y = 0.05;

    BoundingBoxf3 print_volume;
    if (m_config != nullptr)
    {
        const ConfigOptionPoints* opt = dynamic_cast<const ConfigOptionPoints*>(m_config->option("bed_shape"));
        if (opt != nullptr)
        {
            BoundingBox bed_box_2D = get_extents(Polygon::new_scale(opt->values));
            print_volume = BoundingBoxf3(Vec3d(unscale<double>(bed_box_2D.min(0)) - tolerance_x, unscale<double>(bed_box_2D.min(1)) - tolerance_y, 0.0), Vec3d(unscale<double>(bed_box_2D.max(0)) + tolerance_x, unscale<double>(bed_box_2D.max(1)) + tolerance_y, m_config->opt_float("max_print_height")));
            // Allow the objects to protrude below the print bed
            print_volume.min(2) = -1e10;
        }
    }

    for (GLVolume* volume : m_volumes.volumes)
    {
        volume->is_outside = ((print_volume.radius() > 0.0) && volume->is_extrusion_path) ? !print_volume.contains(volume->bounding_box) : false;
    }
}

void GLCanvas3D::_show_warning_texture_if_needed()
{
    if (_is_any_volume_outside())
    {
        enable_warning_texture(true);
        _generate_warning_texture(L("Detected toolpath outside print volume"));
    }
    else
    {
        enable_warning_texture(false);
        _reset_warning_texture();
    }
}

std::vector<float> GLCanvas3D::_parse_colors(const std::vector<std::string>& colors)
{
    static const float INV_255 = 1.0f / 255.0f;

    std::vector<float> output(colors.size() * 4, 1.0f);
    for (size_t i = 0; i < colors.size(); ++i)
    {
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

                output[i * 4 + j] = float(digit1 * 16 + digit2) * INV_255;
            }
        }
    }
    return output;
}

void GLCanvas3D::_generate_legend_texture(const GCodePreviewData& preview_data, const std::vector<float>& tool_colors)
{
#if !ENABLE_USE_UNIQUE_GLCONTEXT
    if (!set_current())
        return;
#endif // !ENABLE_USE_UNIQUE_GLCONTEXT

    m_legend_texture.generate(preview_data, tool_colors);
}

void GLCanvas3D::_generate_warning_texture(const std::string& msg)
{
#if !ENABLE_USE_UNIQUE_GLCONTEXT
    if (!set_current())
        return;
#endif // !ENABLE_USE_UNIQUE_GLCONTEXT

    m_warning_texture.generate(msg);
}

void GLCanvas3D::_reset_warning_texture()
{
#if !ENABLE_USE_UNIQUE_GLCONTEXT
    if (!set_current())
        return;
#endif // !ENABLE_USE_UNIQUE_GLCONTEXT

    m_warning_texture.reset();
}

bool GLCanvas3D::_is_any_volume_outside() const
{
    for (const GLVolume* volume : m_volumes.volumes)
    {
        if ((volume != nullptr) && volume->is_outside)
            return true;
    }

    return false;
}

void GLCanvas3D::_resize_toolbar() const
{
    Size cnv_size = get_canvas_size();
    float zoom = get_camera_zoom();
    float inv_zoom = (zoom != 0.0f) ? 1.0f / zoom : 0.0f;

    switch (m_toolbar.get_layout_type())
    {
    default:
    case GLToolbar::Layout::Horizontal:
    {
        // centers the toolbar on the top edge of the 3d scene
        unsigned int toolbar_width = m_toolbar.get_width();
        float top = (0.5f * (float)cnv_size.get_height() - 2.0f) * inv_zoom;
        float left = -0.5f * (float)toolbar_width * inv_zoom;
        m_toolbar.set_position(top, left);
        break;
    }
    case GLToolbar::Layout::Vertical:
    {
        // centers the toolbar on the right edge of the 3d scene
        unsigned int toolbar_width = m_toolbar.get_width();
        unsigned int toolbar_height = m_toolbar.get_height();
        float top = 0.5f * (float)toolbar_height * inv_zoom;
        float left = (0.5f * (float)cnv_size.get_width() - toolbar_width - 2.0f) * inv_zoom;
        m_toolbar.set_position(top, left);
        break;
    }
    }
}

const Print* GLCanvas3D::fff_print() const
{
    return (m_process == nullptr) ? nullptr : m_process->fff_print();
}

const SLAPrint* GLCanvas3D::sla_print() const
{
    return (m_process == nullptr) ? nullptr : m_process->sla_print();
}

} // namespace GUI
} // namespace Slic3r
