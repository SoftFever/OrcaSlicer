#include "libslic3r/libslic3r.h"

#include "3DBed.hpp"

#include "libslic3r/Polygon.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/BoundingBox.hpp"

#include "GUI_App.hpp"
#include "PresetBundle.hpp"

#include <GL/glew.h>

#include <boost/algorithm/string/predicate.hpp>

static const float GROUND_Z = -0.02f;

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
                m_tex_coords[i] = (m_tex_coords[i] - min_x) * inv_size_x;
                m_tex_coords[i + 1] = (m_tex_coords[i + 1] - min_y) * inv_size_y;
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

const double Bed3D::Axes::Radius = 0.5;
const double Bed3D::Axes::ArrowBaseRadius = 2.5 * Bed3D::Axes::Radius;
const double Bed3D::Axes::ArrowLength = 5.0;

Bed3D::Axes::Axes()
: origin(Vec3d::Zero())
, length(Vec3d::Zero())
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

void Bed3D::Axes::render() const
{
    if (m_quadric == nullptr)
        return;

    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glEnable(GL_LIGHTING));

    // x axis
    glsafe(::glColor3f(1.0f, 0.0f, 0.0f));
    glsafe(::glPushMatrix());
    glsafe(::glTranslated(origin(0), origin(1), origin(2)));
    glsafe(::glRotated(90.0, 0.0, 1.0, 0.0));
    render_axis(length(0));
    glsafe(::glPopMatrix());

    // y axis
    glsafe(::glColor3f(0.0f, 1.0f, 0.0f));
    glsafe(::glPushMatrix());
    glsafe(::glTranslated(origin(0), origin(1), origin(2)));
    glsafe(::glRotated(-90.0, 1.0, 0.0, 0.0));
    render_axis(length(1));
    glsafe(::glPopMatrix());

    // z axis
    glsafe(::glColor3f(0.0f, 0.0f, 1.0f));
    glsafe(::glPushMatrix());
    glsafe(::glTranslated(origin(0), origin(1), origin(2)));
    render_axis(length(2));
    glsafe(::glPopMatrix());

    glsafe(::glDisable(GL_LIGHTING));
}

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

Bed3D::Bed3D()
: m_type(Custom)
, m_scale_factor(1.0f)
{
}

bool Bed3D::set_shape(const Pointfs& shape)
{
    EType new_type = detect_type(shape);
    if (m_shape == shape && m_type == new_type)
        // No change, no need to update the UI.
        return false;

    m_shape = shape;
    m_type = new_type;

    calc_bounding_box();

    ExPolygon poly;
    for (const Vec2d& p : m_shape)
    {
        poly.contour.append(Point(scale_(p(0)), scale_(p(1))));
    }

    calc_triangles(poly);

    const BoundingBox& bed_bbox = poly.contour.bounding_box();
    calc_gridlines(poly, bed_bbox);

    m_polygon = offset_ex(poly.contour, (float)bed_bbox.radius() * 1.7f, jtRound, scale_(0.5))[0].contour;

    // Set the origin and size for painting of the coordinate system axes.
    m_axes.origin = Vec3d(0.0, 0.0, (double)GROUND_Z);
    m_axes.length = 0.1 * get_bounding_box().max_size() * Vec3d::Ones();

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

void Bed3D::render(float theta, bool useVBOs, float scale_factor) const
{
    m_scale_factor = scale_factor;

    if (m_shape.empty())
        return;

    switch (m_type)
    {
    case MK2:
    {
        render_prusa("mk2", theta, useVBOs);
        break;
    }
    case MK3:
    {
        render_prusa("mk3", theta, useVBOs);
        break;
    }
    case SL1:
    {
        render_prusa("sl1", theta, useVBOs);
        break;
    }
    default:
    case Custom:
    {
        render_custom();
        break;
    }
    }
}

void Bed3D::render_axes() const
{
    if (!m_shape.empty())
        m_axes.render();
}

void Bed3D::calc_bounding_box()
{
    m_bounding_box = BoundingBoxf3();
    for (const Vec2d& p : m_shape)
    {
        m_bounding_box.merge(Vec3d(p(0), p(1), 0.0));
    }
}

void Bed3D::calc_triangles(const ExPolygon& poly)
{
    Polygons triangles;
    poly.triangulate(&triangles);

    if (!m_triangles.set_from_triangles(triangles, GROUND_Z, m_type != Custom))
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

Bed3D::EType Bed3D::detect_type(const Pointfs& shape) const
{
    EType type = Custom;

    auto bundle = wxGetApp().preset_bundle;
    if (bundle != nullptr)
    {
        const Preset* curr = &bundle->printers.get_selected_preset();
        while (curr != nullptr)
        {
            if (curr->config.has("bed_shape"))
            {
                if ((curr->vendor != nullptr) && (curr->vendor->name == "Prusa Research") && (shape == dynamic_cast<const ConfigOptionPoints*>(curr->config.option("bed_shape"))->values))
                {
                    if (boost::contains(curr->name, "SL1"))
                    {
                        type = SL1;
                        break;
                    }
                    else if (boost::contains(curr->name, "MK3") || boost::contains(curr->name, "MK2.5"))
                    {
                        type = MK3;
                        break;
                    }
                    else if (boost::contains(curr->name, "MK2"))
                    {
                        type = MK2;
                        break;
                    }
                }
            }

            curr = bundle->printers.get_preset_parent(*curr);
        }
    }

    return type;
}

void Bed3D::render_prusa(const std::string &key, float theta, bool useVBOs) const
{
    std::string tex_path = resources_dir() + "/icons/bed/" + key;

    // use higher resolution images if graphic card allows
    GLint max_tex_size;
    ::glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex_size);

    // temporary set to lowest resolution
    max_tex_size = 2048;

    if (max_tex_size >= 8192)
        tex_path += "_8192";
    else if (max_tex_size >= 4096)
        tex_path += "_4096";

    std::string model_path = resources_dir() + "/models/" + key;

    // use anisotropic filter if graphic card allows
    GLfloat max_anisotropy = 0.0f;
    if (glewIsSupported("GL_EXT_texture_filter_anisotropic"))
        ::glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &max_anisotropy);

    std::string filename = tex_path + "_top.png";
    if ((m_top_texture.get_id() == 0) || (m_top_texture.get_source() != filename))
    {
        if (!m_top_texture.load_from_file(filename, true))
        {
            render_custom();
            return;
        }

        if (max_anisotropy > 0.0f)
        {
            glsafe(::glBindTexture(GL_TEXTURE_2D, m_top_texture.get_id()));
            glsafe(::glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_anisotropy));
            glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
        }
    }

    filename = tex_path + "_bottom.png";
    if ((m_bottom_texture.get_id() == 0) || (m_bottom_texture.get_source() != filename))
    {
        if (!m_bottom_texture.load_from_file(filename, true))
        {
            render_custom();
            return;
        }

        if (max_anisotropy > 0.0f)
        {
            glsafe(::glBindTexture(GL_TEXTURE_2D, m_bottom_texture.get_id()));
            glsafe(::glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, max_anisotropy));
            glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
        }
    }

    if (theta <= 90.0f)
    {
        filename = model_path + "_bed.stl";
        if ((m_model.get_filename() != filename) && m_model.init_from_file(filename, useVBOs)) {
            Vec3d offset = m_bounding_box.center() - Vec3d(0.0, 0.0, 0.5 * m_model.get_bounding_box().size()(2));
            if (key == "mk2")
                // hardcoded value to match the stl model
                offset += Vec3d(0.0, 7.5, -0.03);
            else if (key == "mk3")
                // hardcoded value to match the stl model
                offset += Vec3d(0.0, 5.5, 2.43);
            else if (key == "sl1")
                // hardcoded value to match the stl model
                offset += Vec3d(0.0, 0.0, -0.03);

            m_model.center_around(offset);
        }

        if (!m_model.get_filename().empty())
        {
            glsafe(::glEnable(GL_LIGHTING));
            m_model.render();
            glsafe(::glDisable(GL_LIGHTING));
        }
    }

    unsigned int triangles_vcount = m_triangles.get_vertices_count();
    if (triangles_vcount > 0)
    {
        glsafe(::glEnable(GL_DEPTH_TEST));
        glsafe(::glDepthMask(GL_FALSE));

        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

        glsafe(::glEnable(GL_TEXTURE_2D));
        glsafe(::glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE));

        glsafe(::glEnableClientState(GL_VERTEX_ARRAY));
        glsafe(::glEnableClientState(GL_TEXTURE_COORD_ARRAY));

        if (theta > 90.0f)
            glsafe(::glFrontFace(GL_CW));

        glsafe(::glBindTexture(GL_TEXTURE_2D, (theta <= 90.0f) ? (GLuint)m_top_texture.get_id() : (GLuint)m_bottom_texture.get_id()));
        glsafe(::glVertexPointer(3, GL_FLOAT, 0, (GLvoid*)m_triangles.get_vertices()));
        glsafe(::glTexCoordPointer(2, GL_FLOAT, 0, (GLvoid*)m_triangles.get_tex_coords()));
        glsafe(::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)triangles_vcount));

        if (theta > 90.0f)
            glsafe(::glFrontFace(GL_CCW));

        glsafe(::glBindTexture(GL_TEXTURE_2D, 0));
        glsafe(::glDisableClientState(GL_TEXTURE_COORD_ARRAY));
        glsafe(::glDisableClientState(GL_VERTEX_ARRAY));

        glsafe(::glDisable(GL_TEXTURE_2D));

        glsafe(::glDisable(GL_BLEND));
        glsafe(::glDepthMask(GL_TRUE));
    }
}

void Bed3D::render_custom() const
{
    m_top_texture.reset();
    m_bottom_texture.reset();

    unsigned int triangles_vcount = m_triangles.get_vertices_count();
    if (triangles_vcount > 0)
    {
        glsafe(::glEnable(GL_LIGHTING));
        glsafe(::glDisable(GL_DEPTH_TEST));

        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

        glsafe(::glEnableClientState(GL_VERTEX_ARRAY));

        glsafe(::glColor4f(0.35f, 0.35f, 0.35f, 0.4f));
        glsafe(::glNormal3d(0.0f, 0.0f, 1.0f));
        glsafe(::glVertexPointer(3, GL_FLOAT, 0, (GLvoid*)m_triangles.get_vertices()));
        glsafe(::glDrawArrays(GL_TRIANGLES, 0, (GLsizei)triangles_vcount));

        // draw grid
        unsigned int gridlines_vcount = m_gridlines.get_vertices_count();

        // we need depth test for grid, otherwise it would disappear when looking the object from below
        glsafe(::glEnable(GL_DEPTH_TEST));
        glsafe(::glLineWidth(3.0f * m_scale_factor));
        glsafe(::glColor4f(0.2f, 0.2f, 0.2f, 0.4f));
        glsafe(::glVertexPointer(3, GL_FLOAT, 0, (GLvoid*)m_gridlines.get_vertices()));
        glsafe(::glDrawArrays(GL_LINES, 0, (GLsizei)gridlines_vcount));

        glsafe(::glDisableClientState(GL_VERTEX_ARRAY));

        glsafe(::glDisable(GL_BLEND));
        glsafe(::glDisable(GL_LIGHTING));
    }
}

} // GUI
} // Slic3r