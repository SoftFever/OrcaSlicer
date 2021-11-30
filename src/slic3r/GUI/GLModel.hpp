#ifndef slic3r_GLModel_hpp_
#define slic3r_GLModel_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/BoundingBox.hpp"
#include <vector>
#include <string>

struct indexed_triangle_set;

namespace Slic3r {

class TriangleMesh;
class Polygon;
using Polygons = std::vector<Polygon>;

namespace GUI {

    class GLModel
    {
    public:
        enum class PrimitiveType : unsigned char
        {
            Triangles,
            Lines,
            LineStrip,
            LineLoop
        };

        struct RenderData
        {
            PrimitiveType type;
            unsigned int vbo_id{ 0 };
            unsigned int ibo_id{ 0 };
            size_t indices_count{ 0 };
            std::array<float, 4> color{ 1.0f, 1.0f, 1.0f, 1.0f };
        };

        struct InitializationData
        {
            struct Entity
            {
                PrimitiveType type;
                std::vector<Vec3f> positions;
                std::vector<Vec3f> normals;
                std::vector<unsigned int> indices;
                std::array<float, 4> color{ 1.0f, 1.0f, 1.0f, 1.0f };
            };

            std::vector<Entity> entities;

            size_t vertices_count() const;
            size_t vertices_size_floats() const { return vertices_count() * 6; }
            size_t vertices_size_bytes() const { return vertices_size_floats() * sizeof(float); }

            size_t indices_count() const;
            size_t indices_size_bytes() const { return indices_count() * sizeof(unsigned int); }
        };

    private:
        std::vector<RenderData> m_render_data;

        BoundingBoxf3 m_bounding_box;
        std::string m_filename;

    public:
        GLModel() = default;
        virtual ~GLModel() { reset(); }

        void init_from(const InitializationData& data);
        void init_from(const indexed_triangle_set& its, const BoundingBoxf3& bbox);
        void init_from(const indexed_triangle_set& its);
        void init_from(const Polygons& polygons, float z);
        bool init_from_file(const std::string& filename);

        // if entity_id == -1 set the color of all entities
        void set_color(int entity_id, const std::array<float, 4>& color);

        void reset();
        void render() const;
        void render_instanced(unsigned int instances_vbo, unsigned int instances_count) const;

        bool is_initialized() const { return !m_render_data.empty(); }

        const BoundingBoxf3& get_bounding_box() const { return m_bounding_box; }
        const std::string& get_filename() const { return m_filename; }

    private:
        void send_to_gpu(RenderData& data, const std::vector<float>& vertices, const std::vector<unsigned int>& indices);
    };

    // create an arrow with cylindrical stem and conical tip, with the given dimensions and resolution
    // the origin of the arrow is in the center of the stem cap
    // the arrow has its axis of symmetry along the Z axis and is pointing upward
    // used to render bed axes and sequential marker
    GLModel::InitializationData stilized_arrow(int resolution, float tip_radius, float tip_height, float stem_radius, float stem_height);

    // create an arrow whose stem is a quarter of circle, with the given dimensions and resolution
    // the origin of the arrow is in the center of the circle
    // the arrow is contained in the 1st quadrant of the XY plane and is pointing counterclockwise
    // used to render sidebar hints for rotations
    GLModel::InitializationData circular_arrow(int resolution, float radius, float tip_height, float tip_width, float stem_width, float thickness);

    // create an arrow with the given dimensions
    // the origin of the arrow is in the center of the stem cap
    // the arrow is contained in XY plane and has its main axis along the Y axis
    // used to render sidebar hints for position and scale
    GLModel::InitializationData straight_arrow(float tip_width, float tip_height, float stem_width, float stem_height, float thickness);

    // create a diamond with the given resolution
    // the origin of the diamond is in its center
    // the diamond is contained into a box with size [1, 1, 1]
    GLModel::InitializationData diamond(int resolution);

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLModel_hpp_

