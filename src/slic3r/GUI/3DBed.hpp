#ifndef slic3r_3DBed_hpp_
#define slic3r_3DBed_hpp_

#include "GLTexture.hpp"
#include "3DScene.hpp"
#include "GLModel.hpp"

#include <libslic3r/BuildVolume.hpp>

#include <tuple>
#include <array>

namespace Slic3r {
namespace GUI {

class GLCanvas3D;

class GeometryBuffer
{
    struct Vertex
    {
        Vec3f position{ Vec3f::Zero() };
        Vec2f tex_coords{ Vec2f::Zero() };
    };

    std::vector<Vertex> m_vertices;

public:
    bool set_from_triangles(const std::vector<Vec2f> &triangles, float z);
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
    class Axes
    {
    public:
        static const float DefaultStemRadius;
        static const float DefaultStemLength;
        static const float DefaultTipRadius;
        static const float DefaultTipLength;

    private:
        Vec3d m_origin{ Vec3d::Zero() };
        float m_stem_length{ DefaultStemLength };
        GLModel m_arrow;

    public:
        const Vec3d& get_origin() const { return m_origin; }
        void set_origin(const Vec3d& origin) { m_origin = origin; }
        void set_stem_length(float length) {
            m_stem_length = length;
            m_arrow.reset();
        }
        float get_total_length() const { return m_stem_length + DefaultTipLength; }
        void render() const;
    };

public:
    enum class Type : unsigned char
    {
        // The print bed model and texture are available from some printer preset.
        System,
        // The print bed model is unknown, thus it is rendered procedurally.
        Custom
    };

private:
    BuildVolume m_build_volume;
    Type m_type{ Type::Custom };
    std::string m_texture_filename;
    std::string m_model_filename;
    // Print volume bounding box exteded with axes and model.
    BoundingBoxf3 m_extended_bounding_box;
    // Slightly expanded print bed polygon, for collision detection.
    Polygon m_polygon;
    GeometryBuffer m_triangles;
    GeometryBuffer m_gridlines;
    GLTexture m_texture;
    // temporary texture shown until the main texture has still no levels compressed
    GLTexture m_temp_texture;
    GLModel m_model;
    Vec3d m_model_offset{ Vec3d::Zero() };
    unsigned int m_vbo_id{ 0 };
    Axes m_axes;

    float m_scale_factor{ 1.0f };

public:
    Bed3D() = default;
    ~Bed3D() { release_VBOs(); }

    // Update print bed model from configuration.
    // Return true if the bed shape changed, so the calee will update the UI.
    //FIXME if the build volume max print height is updated, this function still returns zero
    // as this class does not use it, thus there is no need to update the UI.
    bool set_shape(const Pointfs& bed_shape, const double max_print_height, const std::string& custom_texture, const std::string& custom_model, bool force_as_custom = false);

    // Build volume geometry for various collision detection tasks.
    const BuildVolume& build_volume() const { return m_build_volume; }

    // Was the model provided, or was it generated procedurally?
    Type get_type() const { return m_type; }
    // Was the model generated procedurally?
    bool is_custom() const { return m_type == Type::Custom; }

    // Bounding box around the print bed, axes and model, for rendering.
    const BoundingBoxf3& extended_bounding_box() const { return m_extended_bounding_box; }

    // Check against an expanded 2d bounding box.
    //FIXME shall one check against the real build volume?
    bool contains(const Point& point) const;
    Point point_projection(const Point& point) const;

    void render(GLCanvas3D& canvas, bool bottom, float scale_factor, bool show_axes, bool show_texture);
    void render_for_picking(GLCanvas3D& canvas, bool bottom, float scale_factor);

private:
    // Calculate an extended bounding box from axes and current model for visualization purposes.
    BoundingBoxf3 calc_extended_bounding_box() const;
    void calc_triangles(const ExPolygon& poly);
    void calc_gridlines(const ExPolygon& poly, const BoundingBox& bed_bbox);
    static std::tuple<Type, std::string, std::string> detect_type(const Pointfs& shape);
    void render_internal(GLCanvas3D& canvas, bool bottom, float scale_factor,
        bool show_axes, bool show_texture, bool picking);
    void render_axes() const;
    void render_system(GLCanvas3D& canvas, bool bottom, bool show_texture) const;
    void render_texture(bool bottom, GLCanvas3D& canvas) const;
    void render_model() const;
    void render_custom(GLCanvas3D& canvas, bool bottom, bool show_texture, bool picking) const;
    void render_default(bool bottom, bool picking) const;
    void release_VBOs();
};

} // GUI
} // Slic3r

#endif // slic3r_3DBed_hpp_
