#ifndef slic3r_3DBed_hpp_
#define slic3r_3DBed_hpp_

#include "GLTexture.hpp"
#include "3DScene.hpp"
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#if !ENABLE_SHADERS_MANAGER
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#include "GLShader.hpp"
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#endif // !ENABLE_SHADERS_MANAGER
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#if ENABLE_GCODE_VIEWER
#include "GLModel.hpp"
#endif // ENABLE_GCODE_VIEWER

#include <tuple>

#if !ENABLE_GCODE_VIEWER
class GLUquadric;
typedef class GLUquadric GLUquadricObj;
#endif // !ENABLE_GCODE_VIEWER

namespace Slic3r {
namespace GUI {

class GLCanvas3D;

class GeometryBuffer
{
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

public:
    bool set_from_triangles(const Polygons& triangles, float z, bool generate_tex_coords);
    bool set_from_lines(const Lines& lines, float z);

    const float* get_vertices_data() const;
    unsigned int get_vertices_data_size() const { return (unsigned int)m_vertices.size() * get_vertex_data_size(); }
    unsigned int get_vertex_data_size() const { return (unsigned int)(5 * sizeof(float)); }
    size_t get_position_offset() const { return 0; }
    size_t get_tex_coords_offset() const { return (size_t)(3 * sizeof(float)); }
    unsigned int get_vertices_count() const { return (unsigned int)m_vertices.size(); }
};

class Bed3D
{
#if ENABLE_GCODE_VIEWER
    class Axes
    {
        static const float DefaultStemRadius;
        static const float DefaultStemLength;
        static const float DefaultTipRadius;
        static const float DefaultTipLength;
#else
    struct Axes
    {
        static const double Radius;
        static const double ArrowBaseRadius;
        static const double ArrowLength;
#endif // ENABLE_GCODE_VIEWER

#if ENABLE_GCODE_VIEWER
        Vec3d m_origin{ Vec3d::Zero() };
        float m_stem_length{ DefaultStemLength };
        mutable GL_Model m_arrow;
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#if !ENABLE_SHADERS_MANAGER
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
        mutable Shader m_shader;
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#endif // !ENABLE_SHADERS_MANAGER
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@

    public:
#else
        Vec3d origin;
        Vec3d length;
        GLUquadricObj* m_quadric;
#endif // ENABLE_GCODE_VIEWER

#if !ENABLE_GCODE_VIEWER
        Axes();
        ~Axes();
#endif // !ENABLE_GCODE_VIEWER

#if ENABLE_GCODE_VIEWER
        void set_origin(const Vec3d& origin) { m_origin = origin; }
        void set_stem_length(float length);
        float get_total_length() const { return m_stem_length + DefaultTipLength; }
#endif // ENABLE_GCODE_VIEWER
        void render() const;

#if !ENABLE_GCODE_VIEWER
    private:
        void render_axis(double length) const;
#endif // !ENABLE_GCODE_VIEWER
    };

public:
    enum EType : unsigned char
    {
        System,
        Custom,
        Num_Types
    };

private:
    EType m_type;
    Pointfs m_shape;
    std::string m_texture_filename;
    std::string m_model_filename;
    mutable BoundingBoxf3 m_bounding_box;
    mutable BoundingBoxf3 m_extended_bounding_box;
    Polygon m_polygon;
    GeometryBuffer m_triangles;
    GeometryBuffer m_gridlines;
    mutable GLTexture m_texture;
    mutable GLBed m_model;
    // temporary texture shown until the main texture has still no levels compressed
    mutable GLTexture m_temp_texture;
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#if !ENABLE_SHADERS_MANAGER
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
    mutable Shader m_shader;
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
#endif // !ENABLE_SHADERS_MANAGER
//@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
    mutable unsigned int m_vbo_id;
    Axes m_axes;

    mutable float m_scale_factor;

public:
    Bed3D();
    ~Bed3D() { reset(); }

    EType get_type() const { return m_type; }

    bool is_custom() const { return m_type == Custom; }

    const Pointfs& get_shape() const { return m_shape; }
    // Return true if the bed shape changed, so the calee will update the UI.
    bool set_shape(const Pointfs& shape, const std::string& custom_texture, const std::string& custom_model);

    const BoundingBoxf3& get_bounding_box(bool extended) const {
        return extended ? m_extended_bounding_box : m_bounding_box;
    }

    bool contains(const Point& point) const;
    Point point_projection(const Point& point) const;

    void render(GLCanvas3D& canvas, bool bottom, float scale_factor,
                bool show_axes, bool show_texture) const;

private:
    void calc_bounding_boxes() const;
    void calc_triangles(const ExPolygon& poly);
    void calc_gridlines(const ExPolygon& poly, const BoundingBox& bed_bbox);
    std::tuple<EType, std::string, std::string> detect_type(const Pointfs& shape) const;
    void render_axes() const;
    void render_system(GLCanvas3D& canvas, bool bottom, bool show_texture) const;
    void render_texture(bool bottom, GLCanvas3D& canvas) const;
    void render_model() const;
    void render_custom(GLCanvas3D& canvas, bool bottom, bool show_texture) const;
    void render_default(bool bottom) const;
    void reset();
};

} // GUI
} // Slic3r

#endif // slic3r_3DBed_hpp_
