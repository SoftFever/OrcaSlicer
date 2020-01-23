// ///////////////////////////////////////////
//
// Mesh Simplification Tutorial
//
// (C) by Sven Forstmann in 2014
//
// License : MIT
// http://opensource.org/licenses/MIT
//
// https://github.com/sp4cerat/Fast-Quadric-Mesh-Simplification
//
// 5/2016: Chris Rorden created minimal version for OSX/Linux/Windows compile
// https://github.com/sp4cerat/Fast-Quadric-Mesh-Simplification/
//
// libslic3r refactor by tamasmeszaros

#ifndef SIMPLIFYMESHIMPL_HPP
#define SIMPLIFYMESHIMPL_HPP

#include <vector>
#include <array>
#include <type_traits>
#include <algorithm>

#ifndef NDEBUG
#include <iostream>
#endif

namespace SimplifyMesh {

using Bary = std::array<double, 3>;
using Index3 = std::array<size_t, 3>;

template<class Vertex> struct vertex_traits {
    using coord_type = typename Vertex::coord_type;
    using compute_type = coord_type;
    
    static coord_type x(const Vertex &v);
    static coord_type& x(Vertex &v);
    
    static coord_type y(const Vertex &v);
    static coord_type& y(Vertex &v);
    
    static coord_type z(const Vertex &v);
    static coord_type& z(Vertex &v);
};

template<class Mesh> struct mesh_traits {
    using vertex_t = typename Mesh::vertex_t;
    
    static size_t   face_count(const Mesh &m);
    static size_t   vertex_count(const Mesh &m);
    static vertex_t vertex(const Mesh &m, size_t vertex_idx);
    static void     vertex(Mesh &m, size_t vertex_idx, const vertex_t &v);
    static Index3   triangle(const Mesh &m, size_t face_idx);
    static void     triangle(Mesh &m, size_t face_idx, const Index3 &t);
    static void     update(Mesh &m, size_t vertex_count, size_t face_count);
};

namespace implementation {

// A shorter C++14 style form of the enable_if metafunction
template<bool B, class T>
using enable_if_t = typename std::enable_if<B, T>::type;

// Meta predicates for floating, 'scaled coord' and generic arithmetic types
template<class T, class O = T>
using FloatingOnly = enable_if_t<std::is_floating_point<T>::value, O>;

template<class T, class O = T>
using IntegerOnly = enable_if_t<std::is_integral<T>::value, O>;

template<class T, class O = T>
using ArithmeticOnly = enable_if_t<std::is_arithmetic<T>::value, O>;

template< class T >
struct remove_cvref {
    using type = typename std::remove_cv<
        typename std::remove_reference<T>::type>::type;
};

template< class T >
using remove_cvref_t = typename remove_cvref<T>::type;

struct DOut {
#ifndef NDEBUG
    std::ostream& out = std::cout;
#endif
};

template<class T>
inline DOut&& operator<<( DOut&& out, T&& d) {
#ifndef NDEBUG
    out.out << d;
#endif
    return std::move(out);
}

inline DOut dout() { return DOut(); }

template<class T> FloatingOnly<T, bool> is_approx(T val, T ref) { return std::abs(val - ref) < 1e-8; }
template<class T> IntegerOnly <T, bool> is_approx(T val, T ref) { val == ref; }

template<class T, size_t N = 10> class SymetricMatrix {
public:
    
    explicit SymetricMatrix(ArithmeticOnly<T> c = T()) { std::fill(m, m + N, c); }
    
    SymetricMatrix(T m11, T m12, T m13, T m14,
                   T m22, T m23, T m24,
                   T m33, T m34,
                   T m44)
    {
        m[0] = m11;  m[1] = m12;  m[2] = m13;  m[3] = m14;
        m[4] = m22;  m[5] = m23;  m[6] = m24;
        m[7] = m33;  m[8] = m34;
        m[9] = m44;
    }
    
    // Make plane
    SymetricMatrix(T a, T b, T c, T d)
    {
        m[0] = a * a; m[1] = a * b; m[2] = a * c; m[3] = a * d;
        m[4] = b * b; m[5] = b * c; m[6] = b * d;
        m[7] = c * c; m[8] = c * d;
        m[9] = d * d;
    }
    
    T operator[](int c) const { return m[c]; }
    
    // Determinant
    T det(int a11, int a12, int a13,
          int a21, int a22, int a23,
          int a31, int a32, int a33)
    {
        T det = m[a11] * m[a22] * m[a33] + m[a13] * m[a21] * m[a32] +
                m[a12] * m[a23] * m[a31] - m[a13] * m[a22] * m[a31] -
                m[a11] * m[a23] * m[a32] - m[a12] * m[a21] * m[a33];
        
        return det;
    }
    
    const SymetricMatrix operator+(const SymetricMatrix& n) const
    {
        return SymetricMatrix(m[0] + n[0], m[1] + n[1], m[2] + n[2], m[3]+n[3],
                              m[4] + n[4], m[5] + n[5], m[6] + n[6], 
                              m[7] + n[7], m[8] + n[8],
                              m[9] + n[9]);
    }
    
    SymetricMatrix& operator+=(const SymetricMatrix& n)
    {
        m[0]+=n[0]; m[1]+=n[1]; m[2]+=n[2]; m[3]+=n[3];
        m[4]+=n[4]; m[5]+=n[5]; m[6]+=n[6]; m[7]+=n[7];
        m[8]+=n[8]; m[9]+=n[9];
        
        return *this;
    }
    
    T m[N];
};

template<class V> using TCoord = typename vertex_traits<remove_cvref_t<V>>::coord_type;
template<class V> using TCompute = typename vertex_traits<remove_cvref_t<V>>::compute_type;
template<class V> inline TCoord<V> x(const V &v) { return vertex_traits<remove_cvref_t<V>>::x(v); }
template<class V> inline TCoord<V> y(const V &v) { return vertex_traits<remove_cvref_t<V>>::y(v); }
template<class V> inline TCoord<V> z(const V &v) { return vertex_traits<remove_cvref_t<V>>::z(v); }
template<class V> inline TCoord<V>& x(V &v) { return vertex_traits<remove_cvref_t<V>>::x(v); }
template<class V> inline TCoord<V>& y(V &v) { return vertex_traits<remove_cvref_t<V>>::y(v); }
template<class V> inline TCoord<V>& z(V &v) { return vertex_traits<remove_cvref_t<V>>::z(v); }
template<class M> using TVertex = typename mesh_traits<remove_cvref_t<M>>::vertex_t;
template<class Mesh> using TMeshCoord = TCoord<TVertex<Mesh>>;

template<class Vertex> TCompute<Vertex> dot(const Vertex &v1, const Vertex &v2)
{
    return TCompute<Vertex>(x(v1)) * x(v2) +
           TCompute<Vertex>(y(v1)) * y(v2) +
           TCompute<Vertex>(z(v1)) * z(v2);
}

template<class Vertex> Vertex cross(const Vertex &a, const Vertex &b)
{
    return Vertex{y(a) * z(b) - z(a) * y(b),
                  z(a) * x(b) - x(a) * z(b),
                  x(a) * y(b) - y(a) * x(b)};
}

template<class Vertex> TCompute<Vertex> lengthsq(const Vertex &v)
{
    return TCompute<Vertex>(x(v)) * x(v) + TCompute<Vertex>(y(v)) * y(v) +
           TCompute<Vertex>(z(v)) * z(v);
}

template<class Vertex> void normalize(Vertex &v)
{
    double square = std::sqrt(lengthsq(v));
    x(v) /= square; y(v) /= square; z(v) /= square;
}

using Bary = std::array<double, 3>;

template<class Vertex>
Bary barycentric(const Vertex &p, const Vertex &a, const Vertex &b, const Vertex &c)
{
    Vertex v0 = (b - a);
    Vertex v1 = (c - a);
    Vertex v2 = (p - a);

    double d00   = dot(v0, v0);
    double d01   = dot(v0, v1);
    double d11   = dot(v1, v1);
    double d20   = dot(v2, v0);
    double d21   = dot(v2, v1);
    double denom = d00 * d11 - d01 * d01;
    double v     = (d11 * d20 - d01 * d21) / denom;
    double w     = (d00 * d21 - d01 * d20) / denom;
    double u     = 1.0 - v - w;

    return {u, v, w};
}

template<class Mesh> class SimplifiableMesh {
    Mesh *m_mesh;

    using Vertex     = TVertex<Mesh>;
    using Coord      = TMeshCoord<Mesh>;
    using HiPrecison = TCompute<TVertex<Mesh>>;
    using SymMat     = SymetricMatrix<HiPrecison>;

    struct FaceInfo {
        size_t idx;
        double err[4] = {0.};
        bool   deleted = false, dirty = false;
        Vertex n;
        explicit FaceInfo(size_t id): idx(id) {}
    };

    struct VertexInfo {
        size_t idx;
        size_t tstart = 0, tcount = 0;
        bool border = false;
        SymMat q;
        explicit VertexInfo(size_t id): idx(id) {}
    };
    
    struct Ref { size_t face; size_t vertex; };
    
    std::vector<Ref> m_refs;
    std::vector<FaceInfo> m_faceinfo;
    std::vector<VertexInfo> m_vertexinfo;
    
    void compact_faces();
    void compact();
    
    size_t mesh_vcount() const { return mesh_traits<Mesh>::vertex_count(*m_mesh); }
    size_t mesh_facecount() const { return mesh_traits<Mesh>::face_count(*m_mesh); }
    
    size_t vcount() const { return m_vertexinfo.size(); }
    
    inline Vertex read_vertex(size_t vi) const
    {
        return mesh_traits<Mesh>::vertex(*m_mesh, vi);
    }
    
    inline Vertex read_vertex(const VertexInfo &vinf) const
    {
        return read_vertex(vinf.idx);
    }
    
    inline void write_vertex(size_t idx, const Vertex &v) const
    {
        mesh_traits<Mesh>::vertex(*m_mesh, idx, v);
    }
    
    inline void write_vertex(const VertexInfo &vinf, const Vertex &v) const
    {
        write_vertex(vinf.idx, v);
    }
    
    inline Index3 read_triangle(size_t fi) const
    {
        return mesh_traits<Mesh>::triangle(*m_mesh, fi);
    }
    
    inline Index3 read_triangle(const FaceInfo &finf) const
    {
        return read_triangle(finf.idx);
    }
    
    inline void write_triangle(size_t idx, const Index3 &t)
    {
        return mesh_traits<Mesh>::triangle(*m_mesh, idx, t);
    }
    
    inline void write_triangle(const FaceInfo &finf, const Index3 &t)
    {
        return write_triangle(finf.idx, t);
    }    
    
    inline std::array<Vertex, 3> triangle_vertices(const Index3 &f) const
    {
        std::array<Vertex, 3> p;
        for (size_t i = 0; i < 3; ++i) p[i] = read_vertex(f[i]);
        return p;
    }

    // Error between vertex and Quadric    
    static double vertex_error(const SymMat &q, const Vertex &v)
    {
        Coord _x = x(v) , _y = y(v), _z = z(v);
        return q[0] * _x * _x + 2 * q[1] * _x * _y + 2 * q[2] * _x * _z +
               2 * q[3] * _x + q[4] * _y * _y + 2 * q[5] * _y * _z +
               2 * q[6] * _y + q[7] * _z * _z + 2 * q[8] * _z + q[9];
    }
    
    // Error for one edge    
    double calculate_error(size_t id_v1, size_t id_v2, Vertex &p_result);
    
    void calculate_error(FaceInfo &fi)
    {
        Vertex p;
        Index3 t = read_triangle(fi);
        for (size_t j = 0; j < 3; ++j)
            fi.err[j] = calculate_error(t[j], t[(j + 1) % 3], p);
        
        fi.err[3] = std::min(fi.err[0], std::min(fi.err[1], fi.err[2]));
    }
    
    void update_mesh(int iteration);
    
    // Update triangle connections and edge error after a edge is collapsed
    void update_triangles(size_t i, VertexInfo &vi, std::vector<bool> &deleted, int &deleted_triangles);
    
    // Check if a triangle flips when this edge is removed
    bool flipped(const Vertex &p, size_t i0, size_t i1, VertexInfo &v0, VertexInfo &v1, std::vector<bool> &deleted);
    
public:
    
    explicit SimplifiableMesh(Mesh *m) : m_mesh{m}
    {
        static_assert(
            std::is_arithmetic<Coord>::value,
            "Coordinate type of mesh has to be an arithmetic type!");
        
        m_faceinfo.reserve(mesh_traits<Mesh>::face_count(*m));
        m_vertexinfo.reserve(mesh_traits<Mesh>::vertex_count(*m));
        for (size_t i = 0; i < mesh_facecount(); ++i) m_faceinfo.emplace_back(i);
        for (size_t i = 0; i < mesh_vcount(); ++i) m_vertexinfo.emplace_back(i);
        
    }
    
    void simplify_mesh_lossless();
};


template<class Mesh> void SimplifiableMesh<Mesh>::compact_faces()
{
    auto it = std::remove_if(m_faceinfo.begin(), m_faceinfo.end(),
                             [](const FaceInfo &inf) { return inf.deleted; });
    
    m_faceinfo.erase(it, m_faceinfo.end());
}

template<class M> void SimplifiableMesh<M>::compact()
{   
    for (auto &vi : m_vertexinfo) vi.tcount = 0;
    
    compact_faces();
    
    for (FaceInfo &fi : m_faceinfo)
        for (size_t vidx : read_triangle(fi)) m_vertexinfo[vidx].tcount = 1;
    
    size_t dst = 0;
    for (VertexInfo &vi : m_vertexinfo) {
        if (vi.tcount) {
            vi.tstart = dst;
            write_vertex(dst++, read_vertex(vi)); 
        }
    }
    
    size_t vertex_count = dst;
    
    dst = 0;
    for (const FaceInfo &fi : m_faceinfo) {
        Index3 t = read_triangle(fi);
        for (size_t &idx : t) idx = m_vertexinfo[idx].tstart;
        write_triangle(dst++, t);
    }
    
    mesh_traits<M>::update(*m_mesh, vertex_count, m_faceinfo.size());
}

template<class Mesh>
double SimplifiableMesh<Mesh>::calculate_error(size_t id_v1, size_t id_v2, Vertex &p_result)
{
    // compute interpolated vertex
    
    SymMat q = m_vertexinfo[id_v1].q + m_vertexinfo[id_v2].q;
    
    bool border = m_vertexinfo[id_v1].border & m_vertexinfo[id_v2].border;
    double     error = 0;
    HiPrecison det   = q.det(0, 1, 2, 1, 4, 5, 2, 5, 7);
    
    if (!is_approx(det, HiPrecison(0)) && !border)
    {
        // q_delta is invertible
        x(p_result) = Coord(-1) / det * q.det(1, 2, 3, 4, 5, 6, 5, 7, 8);	// vx = A41/det(q_delta)
        y(p_result) = Coord( 1) / det * q.det(0, 2, 3, 1, 5, 6, 2, 7, 8);	// vy = A42/det(q_delta)
        z(p_result) = Coord(-1) / det * q.det(0, 1, 3, 1, 4, 6, 2, 5, 8);	// vz = A43/det(q_delta)
        
        error = vertex_error(q, p_result);
    } else {
        // det = 0 -> try to find best result
        Vertex p1     = read_vertex(id_v1);
        Vertex p2     = read_vertex(id_v2);
        Vertex p3     = (p1 + p2) / 2;
        double error1 = vertex_error(q, p1);
        double error2 = vertex_error(q, p2);
        double error3 = vertex_error(q, p3);
        error         = std::min(error1, std::min(error2, error3));

        if (is_approx(error1, error)) p_result = p1;
        if (is_approx(error2, error)) p_result = p2;
        if (is_approx(error3, error)) p_result = p3;
    }

    return error;
}

template<class Mesh> void SimplifiableMesh<Mesh>::update_mesh(int iteration)
{
    if (iteration > 0) compact_faces();
    
    assert(mesh_vcount() == m_vertexinfo.size());
        
    //
    // Init Quadrics by Plane & Edge Errors
    //
    // required at the beginning ( iteration == 0 )
    // recomputing during the simplification is not required,
    // but mostly improves the result for closed meshes
    //
    if (iteration == 0) {
                
        for (VertexInfo &vinf : m_vertexinfo) vinf.q = SymMat{};
        for (FaceInfo   &finf : m_faceinfo) {
            Index3 t = read_triangle(finf);
            std::array<Vertex, 3> p = triangle_vertices(t);
            Vertex                n = cross(Vertex(p[1] - p[0]), Vertex(p[2] - p[0]));
            normalize(n);
            finf.n = n;
            
            for (size_t fi : t)
                m_vertexinfo[fi].q += SymMat(x(n), y(n), z(n), -dot(n, p[0]));
            
            calculate_error(finf);
        }
    }
    
    // Init Reference ID list
    for (VertexInfo &vi : m_vertexinfo) { vi.tstart = 0; vi.tcount = 0; }
    
    for (FaceInfo &fi : m_faceinfo)
        for (size_t vidx : read_triangle(fi))
            m_vertexinfo[vidx].tcount++;
    
    size_t tstart = 0;
    for (VertexInfo &vi : m_vertexinfo) {
        vi.tstart = tstart;
        tstart += vi.tcount;
        vi.tcount = 0;
    }
    
    // Write References
    m_refs.resize(m_faceinfo.size() * 3);
    for (size_t i = 0; i < m_faceinfo.size(); ++i) {
        const FaceInfo &fi = m_faceinfo[i];
        Index3 t = read_triangle(fi);
        for (size_t j = 0; j < 3; ++j) {
            VertexInfo &vi = m_vertexinfo[t[j]];
            
            assert(vi.tstart + vi.tcount < m_refs.size());
            
            Ref &ref = m_refs[vi.tstart + vi.tcount];
            ref.face = i;
            ref.vertex = j;
            vi.tcount++;
        }
    }
    
    // Identify boundary : vertices[].border=0,1
    if (iteration == 0) {
        for (VertexInfo &vi: m_vertexinfo) vi.border = false;
        
        std::vector<size_t> vcount, vids;
        
        for (VertexInfo &vi: m_vertexinfo) {
            vcount.clear();
            vids.clear();
            
            for(size_t j = 0; j < vi.tcount; ++j) {
                assert(vi.tstart + j < m_refs.size());
                FaceInfo &fi = m_faceinfo[m_refs[vi.tstart + j].face];
                Index3 t = read_triangle(fi);
                
                for (size_t fid : t) {
                    size_t ofs=0;
                    while (ofs < vcount.size())
                    {
                        if (vids[ofs] == fid) break;
                        ofs++;
                    }
                    if (ofs == vcount.size())
                    {
                        vcount.emplace_back(1);
                        vids.emplace_back(fid);
                    }
                    else
                        vcount[ofs]++;
                }
            }
            
            for (size_t j = 0; j < vcount.size(); ++j)
                if(vcount[j] == 1) m_vertexinfo[vids[j]].border = true;
        }
    }
}

template<class Mesh>
void SimplifiableMesh<Mesh>::update_triangles(size_t             i0,
                                              VertexInfo &       vi,
                                              std::vector<bool> &deleted,
                                              int &deleted_triangles)
{
    Vertex p;
    for (size_t k = 0; k < vi.tcount; ++k) {
        assert(vi.tstart + k < m_refs.size());
        
        Ref &r = m_refs[vi.tstart + k];
        FaceInfo &fi = m_faceinfo[r.face];
        
        if (fi.deleted) continue;
        
        if (deleted[k]) {
            fi.deleted = true;
            deleted_triangles++;
            continue;
        }
        
        Index3 t = read_triangle(fi);
        t[r.vertex] = i0;
        write_triangle(fi, t);
        
        fi.dirty  = true;
        fi.err[0] = calculate_error(t[0], t[1], p);
        fi.err[1] = calculate_error(t[1], t[2], p);
        fi.err[2] = calculate_error(t[2], t[0], p);
        fi.err[3] = std::min(fi.err[0], std::min(fi.err[1], fi.err[2]));
        m_refs.emplace_back(r);
    }
}

template<class Mesh>
bool SimplifiableMesh<Mesh>::flipped(const Vertex &    p,
                                     size_t            /*i0*/,
                                     size_t            i1,
                                     VertexInfo &      v0,
                                     VertexInfo &      /*v1*/,
                                     std::vector<bool> &deleted)
{
    for (size_t k = 0; k < v0.tcount; ++k) {
        size_t ridx = v0.tstart + k;
        assert(ridx < m_refs.size());
        
        FaceInfo &fi = m_faceinfo[m_refs[ridx].face];
        if (fi.deleted) continue;
        
        Index3 t = read_triangle(fi);
        int s = m_refs[ridx].vertex;
        size_t id1 = t[(s+1) % 3];
        size_t id2 = t[(s+2) % 3];

        if(id1 == i1 || id2 == i1) // delete ?
        {
            deleted[k] = true;
            continue;
        }
        
        Vertex d1 = read_vertex(id1) - p;
        normalize(d1);
        Vertex d2 = read_vertex(id2) - p;
        normalize(d2);
        
        if (std::abs(dot(d1, d2)) > 0.999) return true;
        
        Vertex n = cross(d1, d2);
        normalize(n);
        
        deleted[k] = false;
        if (dot(n, fi.n) < 0.2) return true;
    }
    
    return false;
}

template<class Mesh>
void SimplifiableMesh<Mesh>::simplify_mesh_lossless()
{
    // init
    for (FaceInfo &fi : m_faceinfo) fi.deleted = false;
    
    // main iteration loop
    int deleted_triangles=0;
    std::vector<bool> deleted0, deleted1;
    
    for (int iteration = 0; iteration < 9999; iteration ++) {
        // update mesh constantly
        update_mesh(iteration);
        
        // clear dirty flag
        for (FaceInfo &fi : m_faceinfo) fi.dirty = false;
        
        //
        // All triangles with edges below the threshold will be removed
        //
        // The following numbers works well for most models.
        // If it does not, try to adjust the 3 parameters
        //
        double threshold = std::numeric_limits<double>::epsilon(); //1.0E-3 EPS; // Really? (tm)
        
        dout() << "lossless iteration " << iteration << "\n";
        
        for (FaceInfo &fi : m_faceinfo) {
            if (fi.err[3] > threshold || fi.deleted || fi.dirty) continue;
            
            for (size_t j = 0; j < 3; ++j) {
                if (fi.err[j] > threshold) continue;
                
                Index3 t = read_triangle(fi);
                size_t i0 = t[j];
                VertexInfo &v0 = m_vertexinfo[i0];
                
                size_t i1 = t[(j + 1) % 3];
                VertexInfo &v1 = m_vertexinfo[i1];

                // Border check
                if(v0.border != v1.border) continue;

                // Compute vertex to collapse to
                Vertex p;
                calculate_error(i0, i1, p);

                deleted0.resize(v0.tcount); // normals temporarily
                deleted1.resize(v1.tcount); // normals temporarily

                // don't remove if flipped
                if (flipped(p, i0, i1, v0, v1, deleted0)) continue;
                if (flipped(p, i1, i0, v1, v0, deleted1)) continue;

                // not flipped, so remove edge
                write_vertex(v0, p);
                v0.q = v1.q + v0.q;
                size_t tstart = m_refs.size();

                update_triangles(i0, v0, deleted0, deleted_triangles);
                update_triangles(i0, v1, deleted1, deleted_triangles);
                
                assert(m_refs.size() >= tstart);
                
                size_t tcount = m_refs.size() - tstart;

                if(tcount <= v0.tcount)
                {
                    // save ram
                    if (tcount) {
                        auto from = m_refs.begin() + tstart, to = from + tcount;
                        std::copy(from, to, m_refs.begin() + v0.tstart);
                    }
                }
                else
                    // append
                    v0.tstart = tstart;

                v0.tcount = tcount;
                break;
            }
        }
        
        if (deleted_triangles <= 0) break;
        deleted_triangles = 0;
    }
    
    compact();
}

} // namespace implementation
} // namespace SimplifyMesh

#endif // SIMPLIFYMESHIMPL_HPP
