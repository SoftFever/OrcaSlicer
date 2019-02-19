#ifndef slic3r_3DBed_hpp_
#define slic3r_3DBed_hpp_

#include "GLTexture.hpp"
#include "3DScene.hpp"

class GLUquadric;
typedef class GLUquadric GLUquadricObj;

namespace Slic3r {
namespace GUI {

#if ENABLE_UNIQUE_BED
class GeometryBuffer
{
    std::vector<float> m_vertices;
    std::vector<float> m_tex_coords;

public:
    bool set_from_triangles(const Polygons& triangles, float z, bool generate_tex_coords);
    bool set_from_lines(const Lines& lines, float z);

    const float* get_vertices() const { return m_vertices.data(); }
    const float* get_tex_coords() const { return m_tex_coords.data(); }

    unsigned int get_vertices_count() const { return (unsigned int)m_vertices.size() / 3; }
};

class Bed3D
{
    struct Axes
    {
        static const double Radius;
        static const double ArrowBaseRadius;
        static const double ArrowLength;
        Vec3d origin;
        Vec3d length;
        GLUquadricObj* m_quadric;

        Axes();
        ~Axes();

        void render() const;

    private:
        void render_axis(double length) const;
    };

public:
    enum EType : unsigned char
    {
        MK2,
        MK3,
        SL1,
        Custom,
        Num_Types
    };

private:
    EType m_type;
    Pointfs m_shape;
    BoundingBoxf3 m_bounding_box;
    Polygon m_polygon;
    GeometryBuffer m_triangles;
    GeometryBuffer m_gridlines;
    mutable GLTexture m_top_texture;
    mutable GLTexture m_bottom_texture;
    mutable GLBed m_model;
    Axes m_axes;

    mutable float m_scale_factor;

public:
    Bed3D();

    EType get_type() const { return m_type; }

    bool is_prusa() const { return (m_type == MK2) || (m_type == MK3) || (m_type == SL1); }
    bool is_custom() const { return m_type == Custom; }

    const Pointfs& get_shape() const { return m_shape; }
    // Return true if the bed shape changed, so the calee will update the UI.
    bool set_shape(const Pointfs& shape);

    const BoundingBoxf3& get_bounding_box() const { return m_bounding_box; }
    bool contains(const Point& point) const;
    Point point_projection(const Point& point) const;

    void render(float theta, bool useVBOs, float scale_factor) const;
    void render_axes() const;

private:
    void calc_bounding_box();
    void calc_triangles(const ExPolygon& poly);
    void calc_gridlines(const ExPolygon& poly, const BoundingBox& bed_bbox);
    EType detect_type(const Pointfs& shape) const;
    void render_prusa(const std::string &key, float theta, bool useVBOs) const;
    void render_custom() const;
};
#endif // ENABLE_UNIQUE_BED

} // GUI
} // Slic3r

#endif // slic3r_3DBed_hpp_
