#ifndef slic3r_3DBed_hpp_
#define slic3r_3DBed_hpp_

#include "GLTexture.hpp"
#include "3DScene.hpp"
#if ENABLE_TEXTURES_FROM_SVG
#include "GLShader.hpp"
#endif // ENABLE_TEXTURES_FROM_SVG

class GLUquadric;
typedef class GLUquadric GLUquadricObj;

namespace Slic3r {
namespace GUI {

class GLCanvas3D;

class GeometryBuffer
{
#if ENABLE_TEXTURES_FROM_SVG
    struct Vertex
    {
        float position[3];
        float tex_coords[2];

        Vertex()
        {
            position[0] = 0.0f; position[1] = 0.0f; position[2] = 0.0f;
            tex_coords[0] = 0.0f; tex_coords[1] = 0.0f;
        }
    };

    std::vector<Vertex> m_vertices;
#else
    std::vector<float> m_vertices;
    std::vector<float> m_tex_coords;
#endif // ENABLE_TEXTURES_FROM_SVG

public:
    bool set_from_triangles(const Polygons& triangles, float z, bool generate_tex_coords);
    bool set_from_lines(const Lines& lines, float z);

#if ENABLE_TEXTURES_FROM_SVG
    const float* get_vertices_data() const;
    unsigned int get_vertices_data_size() const { return (unsigned int)m_vertices.size() * get_vertex_data_size(); }
    unsigned int get_vertex_data_size() const { return (unsigned int)(5 * sizeof(float)); }
    size_t get_position_offset() const { return 0; }
    size_t get_tex_coords_offset() const { return (size_t)(3 * sizeof(float)); }
    unsigned int get_vertices_count() const { return (unsigned int)m_vertices.size(); }
#else
    const float* get_vertices() const { return m_vertices.data(); }
    const float* get_tex_coords() const { return m_tex_coords.data(); }
    unsigned int get_vertices_count() const { return (unsigned int)m_vertices.size() / 3; }
#endif // ENABLE_TEXTURES_FROM_SVG
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
    mutable BoundingBoxf3 m_bounding_box;
    mutable BoundingBoxf3 m_extended_bounding_box;
    Polygon m_polygon;
    GeometryBuffer m_triangles;
    GeometryBuffer m_gridlines;
#if ENABLE_TEXTURES_FROM_SVG
    mutable GLTexture m_texture;
    // temporary texture shown until the main texture has still no levels compressed
    mutable GLTexture m_temp_texture;
    // used to trigger 3D scene update once all compressed textures have been sent to GPU
    mutable bool m_requires_canvas_update;
    mutable Shader m_shader;
    mutable unsigned int m_vbo_id;
#else
    mutable GLTexture m_top_texture;
    mutable GLTexture m_bottom_texture;
#endif // ENABLE_TEXTURES_FROM_SVG
    mutable GLBed m_model;
    Axes m_axes;

    mutable float m_scale_factor;

public:
    Bed3D();
#if ENABLE_TEXTURES_FROM_SVG
    ~Bed3D() { reset(); }
#endif // ENABLE_TEXTURES_FROM_SVG

    EType get_type() const { return m_type; }

    bool is_prusa() const { return (m_type == MK2) || (m_type == MK3) || (m_type == SL1); }
    bool is_custom() const { return m_type == Custom; }

    const Pointfs& get_shape() const { return m_shape; }
    // Return true if the bed shape changed, so the calee will update the UI.
    bool set_shape(const Pointfs& shape);

    const BoundingBoxf3& get_bounding_box(bool extended) const { return extended ? m_extended_bounding_box : m_bounding_box; }
    bool contains(const Point& point) const;
    Point point_projection(const Point& point) const;

#if ENABLE_TEXTURES_FROM_SVG
    void render(GLCanvas3D* canvas, float theta, float scale_factor) const;
#else
    void render(float theta, float scale_factor) const;
#endif // ENABLE_TEXTURES_FROM_SVG
    void render_axes() const;

private:
    void calc_bounding_boxes() const;
    void calc_triangles(const ExPolygon& poly);
    void calc_gridlines(const ExPolygon& poly, const BoundingBox& bed_bbox);
    EType detect_type(const Pointfs& shape) const;
#if ENABLE_TEXTURES_FROM_SVG
    void render_prusa(GLCanvas3D* canvas, const std::string& key, bool bottom) const;
    void render_prusa_shader(bool transparent) const;
#else
    void render_prusa(const std::string& key, float theta) const;
#endif // ENABLE_TEXTURES_FROM_SVG
    void render_custom() const;
#if ENABLE_TEXTURES_FROM_SVG
    void reset();
#endif // ENABLE_TEXTURES_FROM_SVG
};

} // GUI
} // Slic3r

#endif // slic3r_3DBed_hpp_
