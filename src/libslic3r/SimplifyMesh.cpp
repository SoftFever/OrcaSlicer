#include "SimplifyMesh.hpp"
#include "SimplifyMeshImpl.hpp"

namespace SimplifyMesh {

template<> struct vertex_traits<stl_vertex> {
    using coord_type = float;
    using compute_type = double;
    
    static inline float x(const stl_vertex &v) { return v.x(); }
    static inline float& x(stl_vertex &v) { return v.x(); }
    
    static inline float y(const stl_vertex &v) { return v.y(); }
    static inline float& y(stl_vertex &v) { return v.y(); }
    
    static inline float z(const stl_vertex &v) { return v.z(); }
    static inline float& z(stl_vertex &v) { return v.z(); }
};

template<> struct mesh_traits<indexed_triangle_set> {
    using vertex_t = stl_vertex;
    static size_t face_count(const indexed_triangle_set &m)
    {
        return m.indices.size();
    }
    static size_t vertex_count(const indexed_triangle_set &m)
    {
        return m.vertices.size();
    }
    static vertex_t vertex(const indexed_triangle_set &m, size_t idx)
    {
        return m.vertices[idx];
    }
    static void vertex(indexed_triangle_set &m, size_t idx, const vertex_t &v)
    {
        m.vertices[idx] = v;
    }
    static Index3 triangle(const indexed_triangle_set &m, size_t idx)
    {
        std::array<size_t, 3> t;
        for (size_t i = 0; i < 3; ++i) t[i] = size_t(m.indices[idx](int(i)));
        return t;
    }
    static void triangle(indexed_triangle_set &m, size_t fidx, const Index3 &t)
    {
        auto &face = m.indices[fidx];
        face(0) = int(t[0]); face(1) = int(t[1]); face(2) = int(t[2]);
    }
    static void update(indexed_triangle_set &m, size_t vc, size_t fc)
    {
        m.vertices.resize(vc);
        m.indices.resize(fc);
    }
};

} // namespace SimplifyMesh

namespace Slic3r {

void simplify_mesh(indexed_triangle_set &m)
{
    SimplifyMesh::implementation::SimplifiableMesh sm{&m};
    sm.simplify_mesh_lossless();
}

}
