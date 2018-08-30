#include "GLCanvas3D.hpp"

#include "../../admesh/stl.h"
#include "../../libslic3r/libslic3r.h"
#include "../../slic3r/GUI/3DScene.hpp"
#include "../../slic3r/GUI/GLShader.hpp"
#include "../../slic3r/GUI/GUI.hpp"
#include "../../slic3r/GUI/PresetBundle.hpp"
#include "../../slic3r/GUI/GLGizmo.hpp"
#include "../../libslic3r/ClipperUtils.hpp"
#include "../../libslic3r/PrintConfig.hpp"
#include "../../libslic3r/Print.hpp"
#include "../../libslic3r/GCode/PreviewData.hpp"

#include <GL/glew.h>

#include <wx/glcanvas.h>
#include <wx/timer.h>
#include <wx/bitmap.h>
#include <wx/dcmemory.h>
#include <wx/image.h>
#include <wx/settings.h>

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

    float min_x = (float)unscale(triangles[0].points[0].x);
    float min_y = (float)unscale(triangles[0].points[0].y);
    float max_x = min_x;
    float max_y = min_y;

    unsigned int v_coord = 0;
    unsigned int t_coord = 0;
    for (const Polygon& t : triangles)
    {
        for (unsigned int v = 0; v < 3; ++v)
        {
            const Point& p = t.points[v];
            float x = (float)unscale(p.x);
            float y = (float)unscale(p.y);

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
        m_vertices[coord++] = (float)unscale(l.a.x);
        m_vertices[coord++] = (float)unscale(l.a.y);
        m_vertices[coord++] = z;
        m_vertices[coord++] = (float)unscale(l.b.x);
        m_vertices[coord++] = (float)unscale(l.b.y);
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
    for (const Pointf& p : m_shape)
    {
        poly.contour.append(Point(scale_(p.x), scale_(p.y)));
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
    for (const Pointf& p : m_shape)
    {
        m_bounding_box.merge(Pointf3(p.x, p.y, 0.0));
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

GLCanvas3D::Bed::EType GLCanvas3D::Bed::_detect_type() const
{
    EType type = Custom;

    const PresetBundle* bundle = get_preset_bundle();
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
    : length(0.0f)
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
    ::glVertex3f((GLfloat)origin.x, (GLfloat)origin.y, (GLfloat)origin.z);
    ::glVertex3f((GLfloat)origin.x + length, (GLfloat)origin.y, (GLfloat)origin.z);
    // draw line for y axis
    ::glColor3f(0.0f, 1.0f, 0.0f);
    ::glVertex3f((GLfloat)origin.x, (GLfloat)origin.y, (GLfloat)origin.z);
    ::glVertex3f((GLfloat)origin.x, (GLfloat)origin.y + length, (GLfloat)origin.z);
    ::glEnd();
    // draw line for Z axis
    // (re-enable depth test so that axis is correctly shown when objects are behind it)
    if (!depth_test)
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
        unsigned int lines_vcount = m_lines.get_vertices_count();

        ::glLineWidth(2.0f);
        ::glColor3f(0.0f, 0.0f, 0.0f);
        ::glVertexPointer(3, GL_FLOAT, 0, (GLvoid*)m_lines.get_vertices());
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
    float max_z = print_object.model_object()->bounding_box().max.z;

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

const Point GLCanvas3D::Mouse::Drag::Invalid_2D_Point(INT_MAX, INT_MAX);
const Pointf3 GLCanvas3D::Mouse::Drag::Invalid_3D_Point(DBL_MAX, DBL_MAX, DBL_MAX);

GLCanvas3D::Mouse::Drag::Drag()
    : start_position_2D(Invalid_2D_Point)
    , start_position_3D(Invalid_3D_Point)
    , move_with_shift(false)
    , move_volume_idx(-1)
    , gizmo_volume_idx(-1)
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

const float GLCanvas3D::Gizmos::OverlayTexturesScale = 0.75f;
const float GLCanvas3D::Gizmos::OverlayOffsetX = 10.0f * OverlayTexturesScale;
const float GLCanvas3D::Gizmos::OverlayGapY = 5.0f * OverlayTexturesScale;

GLCanvas3D::Gizmos::Gizmos()
    : m_enabled(false)
    , m_current(Undefined)
    , m_dragging(false)
{
}

GLCanvas3D::Gizmos::~Gizmos()
{
    _reset();
}

bool GLCanvas3D::Gizmos::init()
{
    GLGizmoBase* gizmo = new GLGizmoScale;
    if (gizmo == nullptr)
        return false;

    if (!gizmo->init())
        return false;

    m_gizmos.insert(GizmosMap::value_type(Scale, gizmo));

    gizmo = new GLGizmoRotate;
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

    gizmo = new GLGizmoFlatten;
    if (gizmo == nullptr)
        return false;

    if (!gizmo->init()) {
        _reset();
        return false;
    }

    m_gizmos.insert(GizmosMap::value_type(Flatten, gizmo));


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

void GLCanvas3D::Gizmos::update_hover_state(const GLCanvas3D& canvas, const Pointf& mouse_pos)
{
    if (!m_enabled)
        return;

    float cnv_h = (float)canvas.get_canvas_size().get_height();
    float height = _get_total_overlay_height();
    float top_y = 0.5f * (cnv_h - height);
    for (GizmosMap::const_iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if (it->second == nullptr)
            continue;

        float tex_size = (float)it->second->get_textures_size() * OverlayTexturesScale;
        float half_tex_size = 0.5f * tex_size;

        // we currently use circular icons for gizmo, so we check the radius
        if (it->second->get_state() != GLGizmoBase::On)
        {
            bool inside = length(Pointf(OverlayOffsetX + half_tex_size, top_y + half_tex_size).vector_to(mouse_pos)) < half_tex_size;
            it->second->set_state(inside ? GLGizmoBase::Hover : GLGizmoBase::Off);
        }
        top_y += (tex_size + OverlayGapY);
    }
}

void GLCanvas3D::Gizmos::update_on_off_state(const GLCanvas3D& canvas, const Pointf& mouse_pos)
{
    if (!m_enabled)
        return;

    float cnv_h = (float)canvas.get_canvas_size().get_height();
    float height = _get_total_overlay_height();
    float top_y = 0.5f * (cnv_h - height);
    for (GizmosMap::const_iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if (it->second == nullptr)
            continue;

        float tex_size = (float)it->second->get_textures_size() * OverlayTexturesScale;
        float half_tex_size = 0.5f * tex_size;

        // we currently use circular icons for gizmo, so we check the radius
        if (length(Pointf(OverlayOffsetX + half_tex_size, top_y + half_tex_size).vector_to(mouse_pos)) < half_tex_size)
        {
            if ((it->second->get_state() == GLGizmoBase::On))
            {
                it->second->set_state(GLGizmoBase::Off);
                m_current = Undefined;
            }
            else
            {
                it->second->set_state(GLGizmoBase::On);
                m_current = it->first;
            }
        }
        else
            it->second->set_state(GLGizmoBase::Off);

        top_y += (tex_size + OverlayGapY);
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

bool GLCanvas3D::Gizmos::overlay_contains_mouse(const GLCanvas3D& canvas, const Pointf& mouse_pos) const
{
    if (!m_enabled)
        return false;

    float cnv_h = (float)canvas.get_canvas_size().get_height();
    float height = _get_total_overlay_height();
    float top_y = 0.5f * (cnv_h - height);
    for (GizmosMap::const_iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        if (it->second == nullptr)
            continue;

        float tex_size = (float)it->second->get_textures_size() * OverlayTexturesScale;
        float half_tex_size = 0.5f * tex_size;

        // we currently use circular icons for gizmo, so we check the radius
        if (length(Pointf(OverlayOffsetX + half_tex_size, top_y + half_tex_size).vector_to(mouse_pos)) < half_tex_size)
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

void GLCanvas3D::Gizmos::update(const Pointf& mouse_pos)
{
    if (!m_enabled)
        return;

    GLGizmoBase* curr = _get_current();
    if (curr != nullptr)
        curr->update(mouse_pos);
}

void GLCanvas3D::Gizmos::refresh()
{
    if (!m_enabled)
        return;

    GLGizmoBase* curr = _get_current();
    if (curr != nullptr)
        curr->refresh();
}

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

bool GLCanvas3D::Gizmos::is_dragging() const
{
    return m_dragging;
}

void GLCanvas3D::Gizmos::start_dragging()
{
    m_dragging = true;
    GLGizmoBase* curr = _get_current();
    if (curr != nullptr)
        curr->start_dragging();
}

void GLCanvas3D::Gizmos::stop_dragging()
{
    m_dragging = false;
    GLGizmoBase* curr = _get_current();
    if (curr != nullptr)
        curr->stop_dragging();
}

float GLCanvas3D::Gizmos::get_scale() const
{
    if (!m_enabled)
        return 1.0f;

    GizmosMap::const_iterator it = m_gizmos.find(Scale);
    return (it != m_gizmos.end()) ? reinterpret_cast<GLGizmoScale*>(it->second)->get_scale() : 1.0f;
}

void GLCanvas3D::Gizmos::set_scale(float scale)
{
    if (!m_enabled)
        return;

    GizmosMap::const_iterator it = m_gizmos.find(Scale);
    if (it != m_gizmos.end())
        reinterpret_cast<GLGizmoScale*>(it->second)->set_scale(scale);
}

float GLCanvas3D::Gizmos::get_angle_z() const
{
    if (!m_enabled)
        return 0.0f;

    GizmosMap::const_iterator it = m_gizmos.find(Rotate);
    return (it != m_gizmos.end()) ? reinterpret_cast<GLGizmoRotate*>(it->second)->get_angle_z() : 0.0f;
}

void GLCanvas3D::Gizmos::set_angle_z(float angle_z)
{
    if (!m_enabled)
        return;

    GizmosMap::const_iterator it = m_gizmos.find(Rotate);
    if (it != m_gizmos.end())
        reinterpret_cast<GLGizmoRotate*>(it->second)->set_angle_z(angle_z);
}

Pointf3 GLCanvas3D::Gizmos::get_flattening_normal() const
{
    if (!m_enabled)
        return Pointf3(0.f, 0.f, 0.f);

    GizmosMap::const_iterator it = m_gizmos.find(Flatten);
    return (it != m_gizmos.end()) ? reinterpret_cast<GLGizmoFlatten*>(it->second)->get_flattening_normal() : Pointf3(0.f, 0.f, 0.f);
}

void GLCanvas3D::Gizmos::set_flattening_data(const ModelObject* model_object)
{
    if (!m_enabled)
        return;

    GizmosMap::const_iterator it = m_gizmos.find(Flatten);
    if (it != m_gizmos.end())
        reinterpret_cast<GLGizmoFlatten*>(it->second)->set_flattening_data(model_object);
}

void GLCanvas3D::Gizmos::render(const GLCanvas3D& canvas, const BoundingBoxf3& box, RenderOrder render_order) const
{
    if (!m_enabled)
        return;

    ::glDisable(GL_DEPTH_TEST);

    if ((render_order == BeforeBed && dynamic_cast<GLGizmoFlatten*>(_get_current()))
     || (render_order == AfterBed && !dynamic_cast<GLGizmoFlatten*>(_get_current()))) {
        if (box.radius() > 0.0)
            _render_current_gizmo(box);
     }

    if  (render_order == AfterBed) {
        ::glPushMatrix();
        ::glLoadIdentity();

        _render_overlay(canvas);

        ::glPopMatrix();
    }
}

void GLCanvas3D::Gizmos::render_current_gizmo_for_picking_pass(const BoundingBoxf3& box) const
{
    if (!m_enabled)
        return;

    ::glDisable(GL_DEPTH_TEST);

    GLGizmoBase* curr = _get_current();
    if (curr != nullptr)
        curr->render_for_picking(box);
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
        float tex_size = (float)it->second->get_textures_size() * OverlayTexturesScale * inv_zoom;
        GLTexture::render_texture(it->second->get_texture_id(), top_x, top_x + tex_size, top_y - tex_size, top_y);
        top_y -= (tex_size + scaled_gap_y);
    }
}

void GLCanvas3D::Gizmos::_render_current_gizmo(const BoundingBoxf3& box) const
{
    GLGizmoBase* curr = _get_current();
    if (curr != nullptr)
        curr->render(box);
}

float GLCanvas3D::Gizmos::_get_total_overlay_height() const
{
    float height = 0.0f;

    for (GizmosMap::const_iterator it = m_gizmos.begin(); it != m_gizmos.end(); ++it)
    {
        height += (float)it->second->get_textures_size();
        if (std::distance(it, m_gizmos.end()) > 1)
            height += OverlayGapY;
    }

    return height;
}

const unsigned char GLCanvas3D::WarningTexture::Background_Color[3] = { 9, 91, 134 };
const unsigned char GLCanvas3D::WarningTexture::Opacity = 255;

bool GLCanvas3D::WarningTexture::generate(const std::string& msg)
{
    reset();

    if (msg.empty())
        return false;

    wxMemoryDC memDC;
    // select default font
    memDC.SetFont(wxSystemSettings::GetFont(wxSYS_DEFAULT_GUI_FONT));

    // calculates texture size
    wxCoord w, h;
    memDC.GetTextExtent(msg, &w, &h);
    m_width = (int)w;
    m_height = (int)h;

    // generates bitmap
    wxBitmap bitmap(m_width, m_height);

#if defined(__APPLE__) || defined(_MSC_VER)
    bitmap.UseAlpha();
#endif

    memDC.SelectObject(bitmap);
    memDC.SetBackground(wxBrush(wxColour(Background_Color[0], Background_Color[1], Background_Color[2])));
    memDC.Clear();

    memDC.SetTextForeground(*wxWHITE);

    // draw message
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

const unsigned char GLCanvas3D::LegendTexture::Squares_Border_Color[3] = { 64, 64, 64 };
const unsigned char GLCanvas3D::LegendTexture::Background_Color[3] = { 9, 91, 134 };
const unsigned char GLCanvas3D::LegendTexture::Opacity = 255;

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

    m_width = std::max(2 * Px_Border + title_width, 2 * (Px_Border + Px_Square_Contour) + Px_Square + Px_Text_Offset + max_text_width);
    m_height = 2 * (Px_Border + Px_Square_Contour) + title_height + Px_Title_Offset + items_count * Px_Square;
    if (items_count > 1)
        m_height += (items_count - 1) * Px_Square_Contour;

    // generates bitmap
    wxBitmap bitmap(m_width, m_height);

#if defined(__APPLE__) || defined(_MSC_VER)
    bitmap.UseAlpha();
#endif

    memDC.SelectObject(bitmap);
    memDC.SetBackground(wxBrush(wxColour(Background_Color[0], Background_Color[1], Background_Color[2])));
    memDC.Clear();

    memDC.SetTextForeground(*wxWHITE);

    // draw title
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

GLGizmoBase* GLCanvas3D::Gizmos::_get_current() const
{
    GizmosMap::const_iterator it = m_gizmos.find(m_current);
    return (it != m_gizmos.end()) ? it->second : nullptr;
}

GLCanvas3D::GLCanvas3D(wxGLCanvas* canvas)
    : m_canvas(canvas)
    , m_context(nullptr)
    , m_timer(nullptr)
    , m_config(nullptr)
    , m_print(nullptr)
    , m_model(nullptr)
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
    , m_dynamic_background_enabled(false)
    , m_multisample_allowed(false)
    , m_color_by("volume")
    , m_select_by("object")
    , m_drag_by("instance")
    , m_reload_delayed(false)
{
    if (m_canvas != nullptr)
    {
        m_context = new wxGLContext(m_canvas);
        m_timer = new wxTimer(m_canvas);
    }
}

GLCanvas3D::~GLCanvas3D()
{
    reset_volumes();

    if (m_timer != nullptr)
    {
        delete m_timer;
        m_timer = nullptr;
    }

    if (m_context != nullptr)
    {
        delete m_context;
        m_context = nullptr;
    }

    _deregister_callbacks();
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

    if (m_gizmos.is_enabled() && !m_gizmos.init())
        return false;

    m_initialized = true;

    return true;
}

bool GLCanvas3D::set_current()
{
    if ((m_canvas != nullptr) && (m_context != nullptr))
        return m_canvas->SetCurrent(*m_context);

    return false;
}

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
        // ensures this canvas is current
        if (!set_current())
            return;

        m_volumes.release_geometry();
        m_volumes.clear();
        m_dirty = true;
    }

    enable_warning_texture(false);
    _reset_warning_texture();
}

void GLCanvas3D::deselect_volumes()
{
    for (GLVolume* vol : m_volumes.volumes)
    {
        if (vol != nullptr)
            vol->selected = false;
    }
}

void GLCanvas3D::select_volume(unsigned int id)
{
    if (id < (unsigned int)m_volumes.volumes.size())
    {
        GLVolume* vol = m_volumes.volumes[id];
        if (vol != nullptr)
            vol->selected = true;
    }
}

void GLCanvas3D::update_volumes_selection(const std::vector<int>& selections)
{
    if (m_model == nullptr)
        return;

    for (unsigned int obj_idx = 0; obj_idx < (unsigned int)m_model->objects.size(); ++obj_idx)
    {
        if ((selections[obj_idx] == 1) && (obj_idx < (unsigned int)m_objects_volumes_idxs.size()))
        {
            const std::vector<int>& volume_idxs = m_objects_volumes_idxs[obj_idx];
            for (int v : volume_idxs)
            {
                select_volume(v);
            }
        }
    }
}

int GLCanvas3D::check_volumes_outside_state(const DynamicPrintConfig* config) const
{
    ModelInstance::EPrintVolumeState state;
    m_volumes.check_outside_state(config, &state);
    return (int)state;
}

bool GLCanvas3D::move_volume_up(unsigned int id)
{
    if ((id > 0) && (id < (unsigned int)m_volumes.volumes.size()))
    {
        std::swap(m_volumes.volumes[id - 1], m_volumes.volumes[id]);
        std::swap(m_volumes.volumes[id - 1]->composite_id, m_volumes.volumes[id]->composite_id);
        std::swap(m_volumes.volumes[id - 1]->select_group_id, m_volumes.volumes[id]->select_group_id);
        std::swap(m_volumes.volumes[id - 1]->drag_group_id, m_volumes.volumes[id]->drag_group_id);
        return true;
    }

    return false;
}

bool GLCanvas3D::move_volume_down(unsigned int id)
{
    if ((id >= 0) && (id + 1 < (unsigned int)m_volumes.volumes.size()))
    {
        std::swap(m_volumes.volumes[id + 1], m_volumes.volumes[id]);
        std::swap(m_volumes.volumes[id + 1]->composite_id, m_volumes.volumes[id]->composite_id);
        std::swap(m_volumes.volumes[id + 1]->select_group_id, m_volumes.volumes[id]->select_group_id);
        std::swap(m_volumes.volumes[id + 1]->drag_group_id, m_volumes.volumes[id]->drag_group_id);
        return true;
    }

    return false;
}

void GLCanvas3D::set_objects_selections(const std::vector<int>& selections)
{
    m_objects_selections = selections;
}

void GLCanvas3D::set_config(DynamicPrintConfig* config)
{
    m_config = config;
}

void GLCanvas3D::set_print(Print* print)
{
    m_print = print;
}

void GLCanvas3D::set_model(Model* model)
{
    m_model = model;
}

void GLCanvas3D::set_bed_shape(const Pointfs& shape)
{
    bool new_shape = m_bed.set_shape(shape);

    // Set the origin and size for painting of the coordinate system axes.
    m_axes.origin = Pointf3(0.0, 0.0, (coordf_t)GROUND_Z);
    set_axes_length(0.3f * (float)m_bed.get_bounding_box().max_size());

    if (new_shape)
    {
        // forces the selection of the proper camera target
        if (m_volumes.volumes.empty())
            zoom_to_bed();
        else
            zoom_to_volumes();
    }

    m_dirty = true;
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

void GLCanvas3D::set_color_by(const std::string& value)
{
    m_color_by = value;
}

void GLCanvas3D::set_select_by(const std::string& value)
{
    m_select_by = value;
}

void GLCanvas3D::set_drag_by(const std::string& value)
{
    m_drag_by = value;
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

bool GLCanvas3D::is_shader_enabled() const
{
    return m_shader_enabled;
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
}

void GLCanvas3D::enable_moving(bool enable)
{
    m_moving_enabled = enable;
}

void GLCanvas3D::enable_gizmos(bool enable)
{
    m_gizmos.set_enabled(enable);
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
    if (m_config != nullptr)
        m_volumes.update_colors_by_extruder(m_config);
}

void GLCanvas3D::update_gizmos_data()
{
    if (!m_gizmos.is_enabled())
        return;

    int id = _get_first_selected_object_id();
    if ((id != -1) && (m_model != nullptr))
    {
        ModelObject* model_object = m_model->objects[id];
        if (model_object != nullptr)
        {
            ModelInstance* model_instance = model_object->instances[0];
            if (model_instance != nullptr)
            {
                m_gizmos.set_scale(model_instance->scaling_factor);
                m_gizmos.set_angle_z(model_instance->rotation);
                m_gizmos.set_flattening_data(model_object);
            }
        }
    }
    else
    {
        m_gizmos.set_scale(1.0f);
        m_gizmos.set_angle_z(0.0f);
        m_gizmos.set_flattening_data(nullptr);
    }
}

void GLCanvas3D::render()
{
    if (m_canvas == nullptr)
        return;

    if (!_is_shown_on_screen())
        return;

    // ensures this canvas is current and initialized
    if (!set_current() || !_3DScene::init(m_canvas))
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

    _picking_pass();
    _render_background();
    // untextured bed needs to be rendered before objects
    if (is_custom_bed)
    {
        _render_bed(theta);
        // disable depth testing so that axes are not covered by ground
        _render_axes(false);
    }
    _render_objects();
    _render_gizmo(Gizmos::RenderOrder::BeforeBed);
    // textured bed needs to be rendered after objects
    if (!is_custom_bed)
    {
        _render_axes(true);
        _render_bed(theta);
    }
    _render_cutting_plane();
    _render_warning_texture();
    _render_legend_texture();
    _render_gizmo(Gizmos::RenderOrder::AfterBed);
    _render_layer_editing_overlay();

    m_canvas->SwapBuffers();
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
    return m_volumes.load_object(&model_object, obj_idx, instance_idxs, m_color_by, m_select_by, m_drag_by, m_use_VBOs && m_initialized);
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

void GLCanvas3D::reload_scene(bool force)
{
    if ((m_canvas == nullptr) || (m_config == nullptr) || (m_model == nullptr))
        return;

    reset_volumes();

    // ensures this canvas is current
    if (!set_current())
        return;

    set_bed_shape(dynamic_cast<const ConfigOptionPoints*>(m_config->option("bed_shape"))->values);

    if (!m_canvas->IsShown() && !force)
    {
        m_reload_delayed = true;
        return;
    }

    m_reload_delayed = false;

    m_objects_volumes_idxs.clear();

    for (unsigned int obj_idx = 0; obj_idx < (unsigned int)m_model->objects.size(); ++obj_idx)
    {
        m_objects_volumes_idxs.push_back(load_object(*m_model, obj_idx));
    }

    // 1st call to reset if no objects left
    update_gizmos_data();
    update_volumes_selection(m_objects_selections);
    // 2nd call to restore if something selected
    if (!m_objects_selections.empty())
        update_gizmos_data();

    if (m_config->has("nozzle_diameter"))
    {
        // Should the wipe tower be visualized ?
        unsigned int extruders_count = (unsigned int)dynamic_cast<const ConfigOptionFloats*>(m_config->option("nozzle_diameter"))->values.size();

        bool semm = dynamic_cast<const ConfigOptionBool*>(m_config->option("single_extruder_multi_material"))->value;
        bool wt = dynamic_cast<const ConfigOptionBool*>(m_config->option("wipe_tower"))->value;
        bool co = dynamic_cast<const ConfigOptionBool*>(m_config->option("complete_objects"))->value;

        if ((extruders_count > 1) && semm && wt && !co)
        {
            // Height of a print (Show at least a slab)
            coordf_t height = std::max(m_model->bounding_box().max.z, 10.0);

            float x = dynamic_cast<const ConfigOptionFloat*>(m_config->option("wipe_tower_x"))->value;
            float y = dynamic_cast<const ConfigOptionFloat*>(m_config->option("wipe_tower_y"))->value;
            float w = dynamic_cast<const ConfigOptionFloat*>(m_config->option("wipe_tower_width"))->value;
            float a = dynamic_cast<const ConfigOptionFloat*>(m_config->option("wipe_tower_rotation_angle"))->value;

            float depth = m_print->get_wipe_tower_depth();
            if (!m_print->state.is_done(psWipeTower))
                depth = (900.f/w) * (float)(extruders_count - 1) ;

            m_volumes.load_wipe_tower_preview(1000, x, y, w, depth, (float)height, a, m_use_VBOs && m_initialized, !m_print->state.is_done(psWipeTower),
                                              m_print->config.nozzle_diameter.values[0] * 1.25f * 4.5f);
        }
    }

    update_volumes_colors_by_extruder();

    // checks for geometry outside the print volume to render it accordingly
    if (!m_volumes.empty())
    {
        ModelInstance::EPrintVolumeState state;
        bool contained = m_volumes.check_outside_state(m_config, &state);

        if (!contained)
        {
            enable_warning_texture(true);
            _generate_warning_texture(L("Detected object outside print volume"));
            m_on_enable_action_buttons_callback.call(state == ModelInstance::PVS_Fully_Outside);
        }
        else
        {
            enable_warning_texture(false);
            m_volumes.reset_outside_state();
            _reset_warning_texture();
            m_on_enable_action_buttons_callback.call(!m_model->objects.empty());
        }
    }
    else
    {
        enable_warning_texture(false);
        _reset_warning_texture();
        m_on_enable_action_buttons_callback.call(false);
    }
}

void GLCanvas3D::load_gcode_preview(const GCodePreviewData& preview_data, const std::vector<std::string>& str_tool_colors)
{
    if ((m_canvas != nullptr) && (m_print != nullptr))
    {
        // ensures that this canvas is current
        if (!set_current())
            return;

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
    if (m_print == nullptr)
        return;

    _load_print_toolpaths();
    _load_wipe_tower_toolpaths(str_tool_colors);
    for (const PrintObject* object : m_print->objects)
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

void GLCanvas3D::register_on_select_object_callback(void* callback)
{
    if (callback != nullptr)
        m_on_select_object_callback.register_callback(callback);
}

void GLCanvas3D::register_on_model_update_callback(void* callback)
{
    if (callback != nullptr)
        m_on_model_update_callback.register_callback(callback);
}

void GLCanvas3D::register_on_remove_object_callback(void* callback)
{
    if (callback != nullptr)
        m_on_remove_object_callback.register_callback(callback);
}

void GLCanvas3D::register_on_arrange_callback(void* callback)
{
    if (callback != nullptr)
        m_on_arrange_callback.register_callback(callback);
}

void GLCanvas3D::register_on_rotate_object_left_callback(void* callback)
{
    if (callback != nullptr)
        m_on_rotate_object_left_callback.register_callback(callback);
}

void GLCanvas3D::register_on_rotate_object_right_callback(void* callback)
{
    if (callback != nullptr)
        m_on_rotate_object_right_callback.register_callback(callback);
}

void GLCanvas3D::register_on_scale_object_uniformly_callback(void* callback)
{
    if (callback != nullptr)
        m_on_scale_object_uniformly_callback.register_callback(callback);
}

void GLCanvas3D::register_on_increase_objects_callback(void* callback)
{
    if (callback != nullptr)
        m_on_increase_objects_callback.register_callback(callback);
}

void GLCanvas3D::register_on_decrease_objects_callback(void* callback)
{
    if (callback != nullptr)
        m_on_decrease_objects_callback.register_callback(callback);
}

void GLCanvas3D::register_on_instance_moved_callback(void* callback)
{
    if (callback != nullptr)
        m_on_instance_moved_callback.register_callback(callback);
}

void GLCanvas3D::register_on_wipe_tower_moved_callback(void* callback)
{
    if (callback != nullptr)
        m_on_wipe_tower_moved_callback.register_callback(callback);
}

void GLCanvas3D::register_on_enable_action_buttons_callback(void* callback)
{
    if (callback != nullptr)
        m_on_enable_action_buttons_callback.register_callback(callback);
}

void GLCanvas3D::register_on_gizmo_scale_uniformly_callback(void* callback)
{
    if (callback != nullptr)
        m_on_gizmo_scale_uniformly_callback.register_callback(callback);
}

void GLCanvas3D::register_on_gizmo_rotate_callback(void* callback)
{
    if (callback != nullptr)
        m_on_gizmo_rotate_callback.register_callback(callback);
}

void GLCanvas3D::register_on_update_geometry_info_callback(void* callback)
{
    if (callback != nullptr)
        m_on_update_geometry_info_callback.register_callback(callback);
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
                case 43: { m_on_increase_objects_callback.call(); break; }
                // key -
                case 45: { m_on_decrease_objects_callback.call(); break; }
                // key A/a
                case 65:
                case 97: { m_on_arrange_callback.call(); break; }
                // key B/b
                case 66:
                case 98: { zoom_to_bed(); break; }
                // key L/l
                case 76:
                case 108: { m_on_rotate_object_left_callback.call(); break; }
                // key R/r
                case 82:
                case 114: { m_on_rotate_object_right_callback.call(); break; }
                // key S/s
                case 83:
                case 115: { m_on_scale_object_uniformly_callback.call(); break; }
                // key Z/z
                case 90:
                case 122: { zoom_to_volumes(); break; }
                default:
                {
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
        int object_idx_selected = _get_first_selected_object_id();
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
    Point pos(evt.GetX(), evt.GetY());

    int selected_object_idx = _get_first_selected_object_id();
    int layer_editing_object_idx = is_layers_editing_enabled() ? selected_object_idx : -1;
    m_layers_editing.last_object_id = layer_editing_object_idx;
    bool gizmos_overlay_contains_mouse = m_gizmos.overlay_contains_mouse(*this, m_mouse.position);

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
        m_mouse.position = Pointf(-1.0, -1.0);
        m_dirty = true;
    }
    else if (evt.LeftDClick() && (m_hover_volume_id != -1))
        m_on_double_click_callback.call();
    else if (evt.LeftDown() || evt.RightDown())
    {
        // If user pressed left or right button we first check whether this happened
        // on a volume or not.
        int volume_idx = m_hover_volume_id;
        m_layers_editing.state = LayersEditing::Unknown;
        if ((layer_editing_object_idx != -1) && m_layers_editing.bar_rect_contains(*this, pos.x, pos.y))
        {
            // A volume is selected and the mouse is inside the layer thickness bar.
            // Start editing the layer height.
            m_layers_editing.state = LayersEditing::Editing;
            _perform_layer_editing_action(&evt);
        }
        else if ((layer_editing_object_idx != -1) && m_layers_editing.reset_rect_contains(*this, pos.x, pos.y))
        {
            if (evt.LeftDown())
            {
                // A volume is selected and the mouse is inside the reset button.
                m_print->get_object(layer_editing_object_idx)->reset_layer_height_profile();
                // Index 2 means no editing, just wait for mouse up event.
                m_layers_editing.state = LayersEditing::Completed;

                m_dirty = true;
            }
        }
        else if ((selected_object_idx != -1) && gizmos_overlay_contains_mouse)
        {
            update_gizmos_data();
            m_gizmos.update_on_off_state(*this, m_mouse.position);
            m_dirty = true;
        }
        else if ((selected_object_idx != -1) && m_gizmos.grabber_contains_mouse())
        {
            update_gizmos_data();
            m_gizmos.start_dragging();
            m_mouse.drag.gizmo_volume_idx = _get_first_selected_volume_id(selected_object_idx);
            m_dirty = true;

            if (m_gizmos.get_current_type() == Gizmos::Flatten) {
                // Rotate the object so the normal points downward:
                Pointf3 normal = m_gizmos.get_flattening_normal();
                if (normal.x != 0.f || normal.y != 0.f || normal.z != 0.f) {
                    Pointf3 axis = normal.z > 0.999f ? Pointf3(1, 0, 0) : cross(normal, Pointf3(0.f, 0.f, -1.f));
                    float angle = -acos(-normal.z);
                    m_on_gizmo_rotate_callback.call(angle, axis.x, axis.y, axis.z);
                }
            }
        }
        else
        {
            // Select volume in this 3D canvas.
            // Don't deselect a volume if layer editing is enabled. We want the object to stay selected
            // during the scene manipulation.

            if (m_picking_enabled && ((volume_idx != -1) || !is_layers_editing_enabled()))
            {
                if (volume_idx != -1)
                {
                    deselect_volumes();
                    select_volume(volume_idx);
                    int group_id = m_volumes.volumes[volume_idx]->select_group_id;
                    if (group_id != -1)
                    {
                        for (GLVolume* vol : m_volumes.volumes)
                        {
                            if ((vol != nullptr) && (vol->select_group_id == group_id))
                                vol->selected = true;
                        }
                    }

                    update_gizmos_data();
                    m_gizmos.refresh();
                    m_dirty = true;
                }
            }

            // propagate event through callback
            if (m_picking_enabled && (volume_idx != -1))
                _on_select(volume_idx);

            if (volume_idx != -1)
            {
                if (evt.LeftDown() && m_moving_enabled)
                {
                    // The mouse_to_3d gets the Z coordinate from the Z buffer at the screen coordinate pos x, y,
                    // an converts the screen space coordinate to unscaled object space.
                    Pointf3 pos3d = (volume_idx == -1) ? Pointf3(DBL_MAX, DBL_MAX) : _mouse_to_3d(pos);

                    // Only accept the initial position, if it is inside the volume bounding box.
                    BoundingBoxf3 volume_bbox = m_volumes.volumes[volume_idx]->transformed_bounding_box();
                    volume_bbox.offset(1.0);
                    if (volume_bbox.contains(pos3d))
                    {
                        // The dragging operation is initiated.
                        m_mouse.drag.move_with_shift = evt.ShiftDown();
                        m_mouse.drag.move_volume_idx = volume_idx;
                        m_mouse.drag.start_position_3D = pos3d;
                        // Remember the shift to to the object center.The object center will later be used
                        // to limit the object placement close to the bed.
                        m_mouse.drag.volume_center_offset = pos3d.vector_to(volume_bbox.center());
                    }
                }
                else if (evt.RightDown())
                {
                    // if right clicking on volume, propagate event through callback
                    if (m_volumes.volumes[volume_idx]->hover)
                        m_on_right_click_callback.call(pos.x, pos.y);
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
        GLVolume* volume = m_volumes.volumes[m_mouse.drag.move_volume_idx];
        // Get all volumes belonging to the same group, if any.
        std::vector<GLVolume*> volumes;
        int group_id = m_mouse.drag.move_with_shift ? volume->select_group_id : volume->drag_group_id;
        if (group_id == -1)
            volumes.push_back(volume);
        else
        {
            for (GLVolume* v : m_volumes.volumes)
            {
                if (v != nullptr)
                {
                    if ((m_mouse.drag.move_with_shift && (v->select_group_id == group_id)) || (!m_mouse.drag.move_with_shift && (v->drag_group_id == group_id)))
                        volumes.push_back(v);
                }
            }
        }

        // Apply new temporary volume origin and ignore Z.
        for (GLVolume* v : volumes)
        {
            Pointf3 origin = v->get_origin();
            origin.translate(vector.x, vector.y, 0.0);
            v->set_origin(origin);
        }

        m_mouse.drag.start_position_3D = cur_pos;
        m_gizmos.refresh();

        m_dirty = true;
    }
    else if (evt.Dragging() && m_gizmos.is_dragging())
    {
        m_mouse.dragging = true;

        const Pointf3& cur_pos = _mouse_to_bed_3d(pos);
        m_gizmos.update(Pointf(cur_pos.x, cur_pos.y));

        std::vector<GLVolume*> volumes;
        if (m_mouse.drag.gizmo_volume_idx != -1)
        {
            GLVolume* volume = m_volumes.volumes[m_mouse.drag.gizmo_volume_idx];
            // Get all volumes belonging to the same group, if any.
            if (volume->select_group_id == -1)
                volumes.push_back(volume);
            else
            {
                for (GLVolume* v : m_volumes.volumes)
                {
                    if ((v != nullptr) && (v->select_group_id == volume->select_group_id))
                        volumes.push_back(v);
                }
            }
        }

        switch (m_gizmos.get_current_type())
        {
        case Gizmos::Scale:
        {
            // Apply new temporary scale factor
            float scale_factor = m_gizmos.get_scale();
            for (GLVolume* v : volumes)
            {
                v->set_scale_factor(scale_factor);
            }
            break;
        }
        case Gizmos::Rotate:
        {
            // Apply new temporary angle_z
            float angle_z = m_gizmos.get_angle_z();
            for (GLVolume* v : volumes)
            {
                v->set_angle_z(angle_z);
            }
            break;
        }
        default:
            break;
        }

        if (!volumes.empty())
        {
            BoundingBoxf3 bb;
            for (const GLVolume* volume : volumes)
            {
                bb.merge(volume->transformed_bounding_box());
            }
            const Pointf3& size = bb.size();
            m_on_update_geometry_info_callback.call(size.x, size.y, size.z, m_gizmos.get_scale());
        }

        if ((m_gizmos.get_current_type() != Gizmos::Rotate) && (volumes.size() > 1))
            m_gizmos.refresh();

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

            if (layer_editing_object_idx != -1)
                m_on_model_update_callback.call();
        }
        else if ((m_mouse.drag.move_volume_idx != -1) && m_mouse.dragging)
        {
            // get all volumes belonging to the same group, if any
            std::vector<int> volume_idxs;
            int vol_id = m_mouse.drag.move_volume_idx;
            int group_id = m_mouse.drag.move_with_shift ? m_volumes.volumes[vol_id]->select_group_id : m_volumes.volumes[vol_id]->drag_group_id;
            if (group_id == -1)
                volume_idxs.push_back(vol_id);
            else
            {
                for (int i = 0; i < (int)m_volumes.volumes.size(); ++i)
                {
                    if ((m_mouse.drag.move_with_shift && (m_volumes.volumes[i]->select_group_id == group_id)) || (m_volumes.volumes[i]->drag_group_id == group_id))
                        volume_idxs.push_back(i);
                }
            }
            
            _on_move(volume_idxs);

            // force re-selection of the wipe tower, if needed
            if ((volume_idxs.size() == 1) && m_volumes.volumes[volume_idxs[0]]->is_wipe_tower)
                select_volume(volume_idxs[0]);
        }
        else if (!m_mouse.dragging && (m_hover_volume_id == -1) && !gizmos_overlay_contains_mouse && !m_gizmos.is_dragging() && !is_layers_editing_enabled())
        {
            // deselect and propagate event through callback
            if (m_picking_enabled)
            {
                deselect_volumes();
                _on_select(-1);
                update_gizmos_data();
            }
        }
        else if (evt.LeftUp() && m_gizmos.is_dragging())
        {
            switch (m_gizmos.get_current_type())
            {
            case Gizmos::Scale:
            {
                m_on_gizmo_scale_uniformly_callback.call((double)m_gizmos.get_scale());
                break;
            }
            case Gizmos::Rotate:
            {
                m_on_gizmo_rotate_callback.call((double)m_gizmos.get_angle_z());
                break;
            }
            default:
                break;
            }
            m_gizmos.stop_dragging();
        }

        m_mouse.drag.move_volume_idx = -1;
        m_mouse.drag.gizmo_volume_idx = -1;
        m_mouse.set_start_position_3D_as_invalid();
        m_mouse.set_start_position_2D_as_invalid();
        m_mouse.dragging = false;
        m_dirty = true;
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

void GLCanvas3D::on_key_down(wxKeyEvent& evt)
{
    if (evt.HasModifiers())
        evt.Skip();
    else
    {
        int key = evt.GetKeyCode();
        if (key == WXK_DELETE)
            m_on_remove_object_callback.call();
        else
		{
#ifdef __WXOSX__
			if (key == WXK_BACK)
				m_on_remove_object_callback.call();
#endif
			evt.Skip();
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

Point GLCanvas3D::get_local_mouse_position() const
{
    if (m_canvas == nullptr)
        return Point();

    wxPoint mouse_pos = m_canvas->ScreenToClient(wxGetMousePosition());
    return Point(mouse_pos.x, mouse_pos.y);
}

void GLCanvas3D::reset_legend_texture()
{
    if (!set_current())
        return;

    m_legend_texture.reset();
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

void GLCanvas3D::_resize(unsigned int w, unsigned int h)
{
    if ((m_canvas == nullptr) && (m_context == nullptr))
        return;

    // ensures that this canvas is current
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

BoundingBoxf3 GLCanvas3D::_selected_volumes_bounding_box() const
{
    BoundingBoxf3 bb;

    std::vector<const GLVolume*> selected_volumes;
    for (const GLVolume* volume : m_volumes.volumes)
    {
        if ((volume != nullptr) && !volume->is_wipe_tower && volume->selected)
            selected_volumes.push_back(volume);
    }

    bool use_drag_group_id = selected_volumes.size() > 1;
    if (use_drag_group_id)
    {
        int drag_group_id = selected_volumes[0]->drag_group_id;
        for (const GLVolume* volume : selected_volumes)
        {
            if (drag_group_id != volume->drag_group_id)
            {
                use_drag_group_id = false;
                break;
            }
        }
    }

    if (use_drag_group_id)
    {
        for (const GLVolume* volume : selected_volumes)
        {
            bb.merge(volume->bounding_box);
        }

        bb = bb.transformed(selected_volumes[0]->world_matrix());
    }
    else
    {
        for (const GLVolume* volume : selected_volumes)
        {
            bb.merge(volume->transformed_bounding_box());
        }
    }

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
    m_on_select_object_callback.deregister_callback();
    m_on_model_update_callback.deregister_callback();
    m_on_remove_object_callback.deregister_callback();
    m_on_arrange_callback.deregister_callback();
    m_on_rotate_object_left_callback.deregister_callback();
    m_on_rotate_object_right_callback.deregister_callback();
    m_on_scale_object_uniformly_callback.deregister_callback();
    m_on_increase_objects_callback.deregister_callback();
    m_on_decrease_objects_callback.deregister_callback();
    m_on_instance_moved_callback.deregister_callback();
    m_on_wipe_tower_moved_callback.deregister_callback();
    m_on_enable_action_buttons_callback.deregister_callback();
    m_on_gizmo_scale_uniformly_callback.deregister_callback();
    m_on_gizmo_rotate_callback.deregister_callback();
    m_on_update_geometry_info_callback.deregister_callback();
}

void GLCanvas3D::_mark_volumes_for_layer_height() const
{
    if (m_print == nullptr)
        return;

    for (GLVolume* vol : m_volumes.volumes)
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
    if (_is_shown_on_screen())
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

    ::glRotatef(-m_camera.get_theta(), 1.0f, 0.0f, 0.0f); // pitch
    ::glRotatef(m_camera.phi, 0.0f, 0.0f, 1.0f);          // yaw

    Pointf3 neg_target = m_camera.target.negative();
    ::glTranslatef((GLfloat)neg_target.x, (GLfloat)neg_target.y, (GLfloat)neg_target.z);
}

void GLCanvas3D::_picking_pass() const
{
    const Pointf& pos = m_mouse.position;

    if (m_picking_enabled && !m_mouse.dragging && (pos != Pointf(DBL_MAX, DBL_MAX)))
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
        m_gizmos.render_current_gizmo_for_picking_pass(_selected_volumes_bounding_box());

        if (m_multisample_allowed)
            ::glEnable(GL_MULTISAMPLE);

        int volume_id = -1;
        for (GLVolume* vol : m_volumes.volumes)
        {
            vol->hover = false;
        }

        GLubyte color[4] = { 0, 0, 0, 0 };
        const Size& cnv_size = get_canvas_size();
        bool inside = (0 <= pos.x) && (pos.x < cnv_size.get_width()) && (0 <= pos.y) && (pos.y < cnv_size.get_height());
        if (inside)
        {
            ::glReadPixels(pos.x, cnv_size.get_height() - pos.y - 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, (void*)color);
            volume_id = color[0] + color[1] * 256 + color[2] * 256 * 256;
        }

        if ((0 <= volume_id) && (volume_id < (int)m_volumes.volumes.size()))
        {
            m_hover_volume_id = volume_id;
            m_volumes.volumes[volume_id]->hover = true;
            int group_id = m_volumes.volumes[volume_id]->select_group_id;
            if (group_id != -1)
            {
                for (GLVolume* vol : m_volumes.volumes)
                {
                    if (vol->select_group_id == group_id)
                        vol->hover = true;
                }
            }
            m_gizmos.set_hover_id(-1);
        }
        else
        {
            m_hover_volume_id = -1;
            m_gizmos.set_hover_id(inside ? (254 - (int)color[2]) : -1);
        }

        // updates gizmos overlay
        if (_get_first_selected_object_id() != -1)
            m_gizmos.update_hover_state(*this, pos);
        else
            m_gizmos.reset_all_states();
    }
}

void GLCanvas3D::_render_background() const
{
    ::glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

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
        ::glColor3f(ERROR_BG_COLOR[0], ERROR_BG_COLOR[1], ERROR_BG_COLOR[2]);
    else
        ::glColor3f(DEFAULT_BG_COLOR[0], DEFAULT_BG_COLOR[1], DEFAULT_BG_COLOR[2]);

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
                m_volumes.set_print_box((float)bed_bb.min.x, (float)bed_bb.min.y, 0.0f, (float)bed_bb.max.x, (float)bed_bb.max.y, (float)m_config->opt_float("max_print_height"));
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

void GLCanvas3D::_render_cutting_plane() const
{
    m_cutting_plane.render(volumes_bounding_box());
}

void GLCanvas3D::_render_warning_texture() const
{
    if (!m_warning_texture_enabled)
        return;

    // If the warning texture has not been loaded into the GPU, do it now.
    unsigned int tex_id = m_warning_texture.get_id();
    if (tex_id > 0)
    {
        int w = m_warning_texture.get_width();
        int h = m_warning_texture.get_height();
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

            GLTexture::render_texture(tex_id, l, r, b, t);

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
    unsigned int tex_id = m_legend_texture.get_id();
    if (tex_id > 0)
    {
        int w = m_legend_texture.get_width();
        int h = m_legend_texture.get_height();
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

            GLTexture::render_texture(tex_id, l, r, b, t);

            ::glPopMatrix();
            ::glEnable(GL_DEPTH_TEST);
        }
    }
}

void GLCanvas3D::_render_layer_editing_overlay() const
{
    if (m_print == nullptr)
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
            ::glColor4f(vol->render_color[0], vol->render_color[1], vol->render_color[2], vol->render_color[3]);
        }

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

void GLCanvas3D::_render_gizmo(Gizmos::RenderOrder render_order) const
{
    m_gizmos.render(*this, _selected_volumes_bounding_box(), render_order);
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

    if (m_print == nullptr)
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

    m_volumes.volumes[volume_idx]->generate_layer_height_texture(selected_obj, 1);
    _refresh_if_shown_on_screen();

    // Automatic action on mouse down with the same coordinate.
    _start_timer();
}

Pointf3 GLCanvas3D::_mouse_to_3d(const Point& mouse_pos, float* z)
{
    if (m_canvas == nullptr)
        return Pointf3(DBL_MAX, DBL_MAX, DBL_MAX);

    _camera_tranform();

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
    ::gluUnProject((GLdouble)mouse_pos.x, (GLdouble)y, (GLdouble)mouse_z, modelview_matrix, projection_matrix, viewport, &out_x, &out_y, &out_z);
    return Pointf3((coordf_t)out_x, (coordf_t)out_y, (coordf_t)out_z);
}

Pointf3 GLCanvas3D::_mouse_to_bed_3d(const Point& mouse_pos)
{
    float z0 = 0.0f;
    float z1 = 1.0f;
    return Linef3(_mouse_to_3d(mouse_pos, &z0), _mouse_to_3d(mouse_pos, &z1)).intersect_plane(0.0);
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

int GLCanvas3D::_get_first_selected_object_id() const
{
    if (m_print != nullptr)
    {
        int objects_count = (int)m_print->objects.size();

        for (const GLVolume* vol : m_volumes.volumes)
        {
            if ((vol != nullptr) && vol->selected)
            {
                int object_id = vol->select_group_id / 1000000;
                // Objects with object_id >= 1000 have a specific meaning, for example the wipe tower proxy.
                if (object_id < 10000)
                    return (object_id >= objects_count) ? -1 : object_id;
            }
        }
    }
    return -1;
}

int GLCanvas3D::_get_first_selected_volume_id(int object_id) const
{
    int volume_id = -1;

    for (const GLVolume* vol : m_volumes.volumes)
    {
        ++volume_id;
        if ((vol != nullptr) && vol->selected && (object_id == vol->select_group_id / 1000000))
            return volume_id;
    }

    return -1;
}

void GLCanvas3D::_load_print_toolpaths()
{
    // ensures this canvas is current
    if (!set_current())
        return;

    if (m_print == nullptr)
        return;

    if (!m_print->state.is_done(psSkirt) || !m_print->state.is_done(psBrim))
        return;

    if (!m_print->has_skirt() && (m_print->config.brim_width.value == 0))
        return;

    const float color[] = { 0.5f, 1.0f, 0.5f, 1.0f }; // greenish

    // number of skirt layers
    size_t total_layer_count = 0;
    for (const PrintObject* print_object : m_print->objects)
    {
        total_layer_count = std::max(total_layer_count, print_object->total_layer_count());
    }
    size_t skirt_height = m_print->has_infinite_skirt() ? total_layer_count : std::min<size_t>(m_print->config.skirt_height.value, total_layer_count);
    if ((skirt_height == 0) && (m_print->config.brim_width.value > 0))
        skirt_height = 1;

    // get first skirt_height layers (maybe this should be moved to a PrintObject method?)
    const PrintObject* object0 = m_print->objects.front();
    std::vector<float> print_zs;
    print_zs.reserve(skirt_height * 2);
    for (size_t i = 0; i < std::min(skirt_height, object0->layers.size()); ++i)
    {
        print_zs.push_back(float(object0->layers[i]->print_z));
    }
    //FIXME why there are support layers?
    for (size_t i = 0; i < std::min(skirt_height, object0->support_layers.size()); ++i)
    {
        print_zs.push_back(float(object0->support_layers[i]->print_z));
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
            _3DScene::extrusionentity_to_verts(m_print->brim, print_zs[i], Point(0, 0), volume);

        _3DScene::extrusionentity_to_verts(m_print->skirt, print_zs[i], Point(0, 0), volume);
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

    ctxt.shifted_copies = &print_object._shifted_copies;

    // order layers by print_z
    ctxt.layers.reserve(print_object.layers.size() + print_object.support_layers.size());
    for (const Layer *layer : print_object.layers)
        ctxt.layers.push_back(layer);
    for (const Layer *layer : print_object.support_layers)
        ctxt.layers.push_back(layer);
    std::sort(ctxt.layers.begin(), ctxt.layers.end(), [](const Layer *l1, const Layer *l2) { return l1->print_z < l2->print_z; });

    // Maximum size of an allocation block: 32MB / sizeof(float)
    ctxt.has_perimeters = print_object.state.is_done(posPerimeters);
    ctxt.has_infill = print_object.state.is_done(posInfill);
    ctxt.has_support = print_object.state.is_done(posSupportMaterial);
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
        std::vector<GLVolume*> vols;
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
                for (const LayerRegion *layerm : layer->regions) {
                    if (ctxt.has_perimeters)
                        _3DScene::extrusionentity_to_verts(layerm->perimeters, float(layer->print_z), copy,
                        *vols[ctxt.volume_idx(layerm->region()->config.perimeter_extruder.value, 0)]);
                    if (ctxt.has_infill) {
                        for (const ExtrusionEntity *ee : layerm->fills.entities) {
                            // fill represents infill extrusions of a single island.
                            const auto *fill = dynamic_cast<const ExtrusionEntityCollection*>(ee);
                            if (!fill->entities.empty())
                                _3DScene::extrusionentity_to_verts(*fill, float(layer->print_z), copy,
                                *vols[ctxt.volume_idx(
                                is_solid_infill(fill->entities.front()->role()) ?
                                layerm->region()->config.solid_infill_extruder :
                                layerm->region()->config.infill_extruder,
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
                            support_layer->object()->config.support_material_extruder :
                            support_layer->object()->config.support_material_interface_extruder,
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
    if ((m_print == nullptr) || m_print->m_wipe_tower_tool_changes.empty())
        return;

    if (!m_print->state.is_done(psWipeTower))
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
            return priming.empty() ?
                ((idx == print->m_wipe_tower_tool_changes.size()) ? final : print->m_wipe_tower_tool_changes[idx]) :
                ((idx == 0) ? priming : (idx == print->m_wipe_tower_tool_changes.size() + 1) ? final : print->m_wipe_tower_tool_changes[idx - 1]);
        }
        std::vector<WipeTower::ToolChangeResult> priming;
        std::vector<WipeTower::ToolChangeResult> final;
    } ctxt;

    ctxt.print = m_print;
    ctxt.tool_colors = tool_colors.empty() ? nullptr : &tool_colors;
    if (m_print->m_wipe_tower_priming && m_print->config.single_extruder_multi_material_priming)
        ctxt.priming.emplace_back(*m_print->m_wipe_tower_priming.get());
    if (m_print->m_wipe_tower_final_purge)
        ctxt.final.emplace_back(*m_print->m_wipe_tower_final_purge.get());

    ctxt.wipe_tower_angle = ctxt.print->config.wipe_tower_rotation_angle.value/180.f * M_PI;
    ctxt.wipe_tower_pos = WipeTower::xy(ctxt.print->config.wipe_tower_x.value, ctxt.print->config.wipe_tower_y.value);

    BOOST_LOG_TRIVIAL(debug) << "Loading wipe tower toolpaths in parallel - start";

    //FIXME Improve the heuristics for a grain size.
    size_t          n_items = m_print->m_wipe_tower_tool_changes.size() + (ctxt.priming.empty() ? 0 : 1);
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
        std::vector<GLVolume*> vols;
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
                std::vector<GLVolume*>::iterator begin = m_volumes.volumes.begin() + initial_volumes_count;
                std::vector<GLVolume*>::iterator end = m_volumes.volumes.end();
                for (std::vector<GLVolume*>::iterator it = begin; it < end; ++it)
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
            std::vector<GLVolume*>::iterator begin = m_volumes.volumes.begin() + initial_volumes_count;
            std::vector<GLVolume*>::iterator end = m_volumes.volumes.end();
            for (std::vector<GLVolume*>::iterator it = begin; it < end; ++it)
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
            m_volumes.volumes.emplace_back(volume);
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
            m_volumes.volumes.emplace_back(volume);
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
    m_gcode_preview_volume_index.first_volumes.emplace_back(GCodePreviewVolumeIndex::Retraction, 0, (unsigned int)m_volumes.volumes.size());

    // nothing to render, return
    if (preview_data.retraction.positions.empty())
        return;

    GLVolume* volume = new GLVolume(preview_data.retraction.color.rgba);
    if (volume != nullptr)
    {
        m_volumes.volumes.emplace_back(volume);

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
    m_gcode_preview_volume_index.first_volumes.emplace_back(GCodePreviewVolumeIndex::Unretraction, 0, (unsigned int)m_volumes.volumes.size());

    // nothing to render, return
    if (preview_data.unretraction.positions.empty())
        return;

    GLVolume* volume = new GLVolume(preview_data.unretraction.color.rgba);
    if (volume != nullptr)
    {
        m_volumes.volumes.emplace_back(volume);

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
    size_t initial_volumes_count = m_volumes.volumes.size();
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
        for (int i = 0; i < (int)model_obj->instances.size(); ++i)
        {
            instance_ids[i] = i;
        }

        m_volumes.load_object(model_obj, object_id, instance_ids, "object", "object", "object", m_use_VBOs && m_initialized);

        ++object_id;
    }

    // adds wipe tower's volume
    coordf_t max_z = m_print->objects[0]->model_object()->get_model()->bounding_box().max.z;
    const PrintConfig& config = m_print->config;
    unsigned int extruders_count = config.nozzle_diameter.size();
    if ((extruders_count > 1) && config.single_extruder_multi_material && config.wipe_tower && !config.complete_objects) {
        float depth = m_print->get_wipe_tower_depth();
        if (!m_print->state.is_done(psWipeTower))
            depth = (900.f/config.wipe_tower_width) * (float)(extruders_count - 1) ;
        m_volumes.load_wipe_tower_preview(1000, config.wipe_tower_x, config.wipe_tower_y, config.wipe_tower_width, depth, max_z, config.wipe_tower_rotation_angle,
                                          m_use_VBOs && m_initialized, !m_print->state.is_done(psWipeTower), m_print->config.nozzle_diameter.values[0] * 1.25f * 4.5f);
    }
}

void GLCanvas3D::_update_gcode_volumes_visibility(const GCodePreviewData& preview_data)
{
    unsigned int size = (unsigned int)m_gcode_preview_volume_index.first_volumes.size();
    for (unsigned int i = 0; i < size; ++i)
    {
        std::vector<GLVolume*>::iterator begin = m_volumes.volumes.begin() + m_gcode_preview_volume_index.first_volumes[i].id;
        std::vector<GLVolume*>::iterator end = (i + 1 < size) ? m_volumes.volumes.begin() + m_gcode_preview_volume_index.first_volumes[i + 1].id : m_volumes.volumes.end();

        for (std::vector<GLVolume*>::iterator it = begin; it != end; ++it)
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
    static const coordf_t tolerance_x = 0.05;
    static const coordf_t tolerance_y = 0.05;

    BoundingBoxf3 print_volume;
    if (m_config != nullptr)
    {
        const ConfigOptionPoints* opt = dynamic_cast<const ConfigOptionPoints*>(m_config->option("bed_shape"));
        if (opt != nullptr)
        {
            BoundingBox bed_box_2D = get_extents(Polygon::new_scale(opt->values));
            print_volume = BoundingBoxf3(Pointf3(unscale(bed_box_2D.min.x) - tolerance_x, unscale(bed_box_2D.min.y) - tolerance_y, 0.0), Pointf3(unscale(bed_box_2D.max.x) + tolerance_x, unscale(bed_box_2D.max.y) + tolerance_y, m_config->opt_float("max_print_height")));
            // Allow the objects to protrude below the print bed
            print_volume.min.z = -1e10;
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

void GLCanvas3D::_on_move(const std::vector<int>& volume_idxs)
{
    if (m_model == nullptr)
        return;

    std::set<std::string> done;  // prevent moving instances twice
    bool object_moved = false;
    Pointf3 wipe_tower_origin(0.0, 0.0, 0.0);
    for (int volume_idx : volume_idxs)
    {
        GLVolume* volume = m_volumes.volumes[volume_idx];
        int obj_idx = volume->object_idx();
        int instance_idx = volume->instance_idx();

        // prevent moving instances twice
        char done_id[64];
        ::sprintf(done_id, "%d_%d", obj_idx, instance_idx);
        if (done.find(done_id) != done.end())
            continue;

        done.insert(done_id);

        if (obj_idx < 1000)
        {
            // Move a regular object.
            ModelObject* model_object = m_model->objects[obj_idx];
            const Pointf3& origin = volume->get_origin();
            model_object->instances[instance_idx]->offset = Pointf(origin.x, origin.y);
            model_object->invalidate_bounding_box();
            object_moved = true;
        }
        else if (obj_idx == 1000)
            // Move a wipe tower proxy.
            wipe_tower_origin = volume->get_origin();
    }

    if (object_moved)
        m_on_instance_moved_callback.call();

    if (wipe_tower_origin != Pointf3(0.0, 0.0, 0.0))
        m_on_wipe_tower_moved_callback.call(wipe_tower_origin.x, wipe_tower_origin.y);
}

void GLCanvas3D::_on_select(int volume_idx)
{
    int id = -1;
    if ((volume_idx != -1) && (volume_idx < (int)m_volumes.volumes.size()))
    {
        if (m_select_by == "volume")
            id = m_volumes.volumes[volume_idx]->volume_idx();
        else if (m_select_by == "object")
            id = m_volumes.volumes[volume_idx]->object_idx();
    }
    m_on_select_object_callback.call(id);
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
    if (!set_current())
        return;

    m_legend_texture.generate(preview_data, tool_colors);
}

void GLCanvas3D::_generate_warning_texture(const std::string& msg)
{
    if (!set_current())
        return;

    m_warning_texture.generate(msg);
}

void GLCanvas3D::_reset_warning_texture()
{
    if (!set_current())
        return;

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

} // namespace GUI
} // namespace Slic3r
