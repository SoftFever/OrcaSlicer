#include "QuadricEdgeCollapse.hpp"
#include <tuple>
#include "MutablePriorityQueue.hpp"
#include "SimplifyMeshImpl.hpp"

using namespace Slic3r;

// only private namespace not neccessary be in hpp
namespace QuadricEdgeCollapse {
    using Vertices = std::vector<stl_vertex>;
    using Triangle = stl_triangle_vertex_indices;
    using Indices = std::vector<stl_triangle_vertex_indices>;
    using SymMat = SimplifyMesh::implementation::SymetricMatrix<double>;

    struct Error
    {
        float value;
        // range(0 .. 2), 
        unsigned char min_index; 
        Error(float value, unsigned char min_index): value(value), min_index(min_index) {
            assert(min_index < 3);
        }
        Error() = default;
    };
    using Errors = std::vector<Error>;
    // merge information together - faster access during processing
    struct TriangleInfo
    {
        Vec3f n; // normalized normal - speed up calcualtion of q and check flip
        Error e; // smallest error caused by edges, identify smallest edge in triangle
        TriangleInfo() = default;
        bool is_deleted() const { return e.min_index > 2; }
        void set_deleted() { e.min_index = 3; } 
    };  
    using TriangleInfos = std::vector<TriangleInfo>;
    struct VertexInfo
    {
        SymMat q; // sum quadric of surround triangles
        size_t start = 0, count = 0; // vertex neighbor triangles
        VertexInfo() = default;
        bool is_deleted() const { return count == 0; }
    };
    using VertexInfos = std::vector<VertexInfo>;
    struct EdgeInfo
    {
        size_t t_index=0; // triangle index
        unsigned char edge = 0; // 0 or 1 or 2
        EdgeInfo() = default;
    };
    using EdgeInfos = std::vector<EdgeInfo>;

    Vec3f create_normal(const Triangle &triangle, const Vertices &vertices);
    double calculate_error(size_t id_v1, size_t id_v2, SymMat & q, const Vertices &vertices);
    Vec3f calculate_vertex(size_t id_v1, size_t id_v2, SymMat & q, const Vertices &vertices);
    // calculate error for vertex and quadrics, triangle quadrics and triangle vertex give zero, only pozitive number
    double vertex_error(const SymMat &q, const Vec3d &vertex);
    SymMat create_quadric(const Triangle &t, const TriangleInfo &t_info, const Vertices &vertices);
    std::tuple<TriangleInfos, VertexInfos, EdgeInfos> init(const indexed_triangle_set &its);
    size_t find_triangle_index1(size_t vi, const VertexInfo& v_info, size_t ti, const EdgeInfos& e_infos, const Indices& indices);
    bool is_flipped(const Vec3f &vn, const Vec3f &v1, const Vec3f &v2, const Vec3f &normal);
    bool is_flipped(Vec3f &new_vertex, size_t ti0, size_t ti1, const VertexInfo& v_info, 
        const TriangleInfos &t_infos, const EdgeInfos &e_infos, const indexed_triangle_set &its);

    // find edge with smallest error in triangle
    Error calculate_error(const Triangle& t,const Vertices &vertices, const VertexInfos& v_infos);
    // subtract quadric of one triangle from triangle vertex
    void sub_quadric(const Triangle &t, const TriangleInfo &t_info, VertexInfos &v_infos, const Vertices &vertices);

    void remove_triangle(EdgeInfos &e_infos, VertexInfo &v_info, size_t ti);
    void change_neighbors(EdgeInfos &e_infos, VertexInfos &v_infos, size_t ti0, size_t ti1,
        size_t vi0, size_t vi1, size_t vi_top0, const Triangle &t1);

    void compact(const VertexInfos &v_infos, const TriangleInfos &t_infos, const EdgeInfos &e_infos, indexed_triangle_set &its);
    }

using namespace QuadricEdgeCollapse;

bool check_neighbors(TriangleInfos &t_infos,
                    Indices& indices,
                     VertexInfos &  v_infos)
{
    std::vector<size_t> t_counts(v_infos.size(), 0);
    for (size_t i = 0; i < indices.size(); i++) { 
        TriangleInfo &t_info = t_infos[i];
        if (t_info.is_deleted()) continue;
        Triangle &t = indices[i];
        for (size_t vidx : t) ++t_counts[vidx];
    }

    size_t prev_end = 0;
    for (size_t i = 0; i < v_infos.size(); i++) {
        VertexInfo &v_info = v_infos[i];
        if (v_info.is_deleted()) continue;
        if (v_info.count != t_counts[i]) { 
            // no correct count
            return false;
        }
        if (prev_end > v_info.start) {
            // overlap of start
            return false;
        }
        prev_end = v_info.start + v_info.count;
    }
    return true;
}

bool check_new_vertex(const Vec3f& nv, const Vec3f& v0, const Vec3f& v1) {
    float epsilon = 1.f;
    for (size_t i = 0; i < 3; i++) { 
        if ((nv[i] > (v0[i] + epsilon) && nv[i] > (v1[i] + epsilon)) ||
            (nv[i] < (v0[i] - epsilon) && nv[i] < (v1[i] - epsilon))) {
            return false;
        }
    }
    return true;
}

bool Slic3r::its_quadric_edge_collapse(indexed_triangle_set &its,
                                       size_t                triangle_count)
{
    TriangleInfos t_infos;
    VertexInfos   v_infos;
    EdgeInfos     e_infos;
    std::tie(t_infos, v_infos, e_infos) = init(its);

    static constexpr float max_error = std::numeric_limits<float>::max();
    
    auto cmp = [&t_infos](size_t vi0, size_t vi1) -> bool { 
        const Error &e0 = t_infos[vi0].e;
        const Error &e1 = t_infos[vi1].e;
        return e0.value < e1.value;
    };
    // convert triangle index to priority queue index
    std::vector<size_t> i_convert(its.indices.size(), {0});
    auto setter = [&i_convert](size_t it, size_t index) { i_convert[it] = index; };
    MutablePriorityQueue<size_t, decltype(setter), decltype(cmp)> mpq(std::move(setter), std::move(cmp));
    mpq.reserve(its.indices.size());
    for (size_t i = 0; i < its.indices.size(); i++) mpq.push(i);   

    size_t actual_triangle_count = its.indices.size();
    while (actual_triangle_count > triangle_count && !mpq.empty()) {
        // triangle index 0
        size_t ti0 = mpq.top();
        mpq.pop();
        TriangleInfo &t_info0 = t_infos[ti0];
        if (t_info0.is_deleted()) continue;

        Error &e = t_info0.e;
        
        const Triangle &t0 = its.indices[ti0];
        size_t vi0 = t0[e.min_index];
        size_t vi1 = t0[(e.min_index+1) %3];
        // Need by move of neighbor edge infos in function: change_neighbors
        if (vi0 > vi1) std::swap(vi0, vi1);
        VertexInfo &v_info0 = v_infos[vi0];
        VertexInfo &v_info1 = v_infos[vi1];
        assert(!v_info0.is_deleted() && !v_info1.is_deleted());
        
        // new vertex position
        SymMat q(v_info0.q);
        q += v_info1.q;
        Vec3f new_vertex0 = calculate_vertex(vi0, vi1, q, its.vertices);
        assert(check_new_vertex(new_vertex0, its.vertices[vi0], its.vertices[vi1]));
        // set of triangle indices that change quadric
        size_t ti1 = (v_info0.count < v_info1.count)?
            find_triangle_index1(vi1, v_info0, ti0, e_infos, its.indices) :
            find_triangle_index1(vi0, v_info1, ti0, e_infos, its.indices) ;

        if (is_flipped(new_vertex0, ti0, ti1, v_info0, t_infos, e_infos, its) ||
            is_flipped(new_vertex0, ti0, ti1, v_info1, t_infos, e_infos, its)) {
            // IMPROVE1: what about other edges in triangle?
            // IMPROVE2: check mpq top if it is ti1 with same edge
            e.value = max_error;
            mpq.push(ti0);
            continue;
        }

        std::vector<size_t> changed_triangle_indices;
        changed_triangle_indices.reserve(v_info0.count + v_info1.count - 4);

        sub_quadric(t0, t_info0, v_infos, its.vertices);
        TriangleInfo &t_info1 = t_infos[ti1];
        const Triangle &t1 = its.indices[ti1];
        sub_quadric(t1, t_info1, v_infos, its.vertices);
        // for each vertex0 triangles
        size_t v_info0_end = v_info0.start + v_info0.count;
        for (size_t di = v_info0.start; di < v_info0_end; ++di) {
            assert(di < e_infos.size());
            EdgeInfo &e_info = e_infos[di];
            size_t    ti     = e_info.t_index;
            if (ti == ti0) continue; // ti0 will be deleted
            if (ti == ti1) continue; // ti1 will be deleted
            sub_quadric(its.indices[ti], t_infos[ti], v_infos, its.vertices);
            changed_triangle_indices.emplace_back(ti);
        }

        // for each vertex1 triangles
        size_t v_info1_end = v_info1.start + v_info1.count;
        for (size_t di = v_info1.start; di < v_info1_end; ++di) {
            assert(di < e_infos.size());
            EdgeInfo &e_info = e_infos[di];
            size_t    ti     = e_info.t_index;
            if (ti == ti0) continue; // ti0 will be deleted
            if (ti == ti1) continue; // ti1 will be deleted
            Triangle &t = its.indices[ti];
            sub_quadric(t, t_infos[ti], v_infos, its.vertices);
            t[e_info.edge] = vi0; // change index
            changed_triangle_indices.emplace_back(ti);
        }

        // fix neighbors
        
        // vertex index of triangle 0 which is not vi0 nor vi1
        size_t vi_top0 = t0[(e.min_index + 2) % 3];
        change_neighbors(e_infos, v_infos, ti0, ti1, vi0, vi1, vi_top0, t1);
        
        // Change vertex
        // Has to be set after subtract quadric
        its.vertices[vi0] = new_vertex0;

        // add new quadrics
        v_info0.q = SymMat(); // zero value
        for (size_t ti : changed_triangle_indices) {
            const Triangle& t = its.indices[ti];
            TriangleInfo &t_info = t_infos[ti];
            t_info.n = create_normal(t, its.vertices); // new normal
            SymMat q = create_quadric(t, t_info, its.vertices);
            for (const size_t vi: t) v_infos[vi].q += q;
        }

        // fix errors - must be after calculate all quadric
        t_info1.e.value = max_error; // not neccessary when check deleted triangles at begining
        //mpq.remove(i_convert[ti1]);
        for (size_t ti : changed_triangle_indices) {
            const Triangle &t = its.indices[ti];
            t_infos[ti].e = calculate_error(t, its.vertices, v_infos);
            mpq.update(i_convert[ti]);
        }

        // set triangle(0 + 1) indices as deleted
        t_info0.set_deleted();
        t_info1.set_deleted();
        // triangle counter decrementation
        actual_triangle_count-=2;

        //assert(check_neighbors(t_infos, its.indices, v_infos));
    }

    // compact triangle
    compact(v_infos, t_infos, e_infos, its);
    return true;
}

Vec3f QuadricEdgeCollapse::create_normal(const Triangle &   triangle,
                                         const Vertices &vertices)
{
    const Vec3f &v0 = vertices[triangle[0]];
    const Vec3f &v1 = vertices[triangle[1]];
    const Vec3f &v2 = vertices[triangle[2]];
    // n = triangle normal
    Vec3f n = (v1 - v0).cross(v2 - v0);
    n.normalize();
    return n;
}

double QuadricEdgeCollapse::calculate_error(size_t          id_v1,
                                            size_t          id_v2,
                                            SymMat &  q,
                                            const Vertices &vertices)
{
    double det = q.det(0, 1, 2, 1, 4, 5, 2, 5, 7);
    if (abs(det) < std::numeric_limits<double>::epsilon()) {
        // can't divide by zero
        const Vec3f &v0 = vertices[id_v1];
        const Vec3f &v1 = vertices[id_v2];
        Vec3d verts[3]  = {v0.cast<double>(), v1.cast<double>(), Vec3d()};
        verts[2]        = (verts[0] + verts[1]) / 2;
        double errors[] = {vertex_error(q, verts[0]),
                           vertex_error(q, verts[1]),
                           vertex_error(q, verts[2])};
        return *std::min_element(std::begin(errors), std::end(errors));
    }

    double det_1 = -1 / det;
    double det_x = q.det(1, 2, 3, 4, 5, 6, 5, 7, 8); // vx = A41/det(q_delta)
    double det_y = q.det(0, 2, 3, 1, 5, 6, 2, 7, 8); // vy = A42/det(q_delta)
    double det_z = q.det(0, 1, 3, 1, 4, 6, 2, 5, 8); // vz = A43/det(q_delta)
    Vec3d  vertex(det_1 * det_x, -det_1 * det_y, det_1 * det_z);
    return vertex_error(q, vertex);
}

// similar as calculate error but focus on new vertex without calculation of error
Vec3f QuadricEdgeCollapse::calculate_vertex(size_t          id_v1,
                                            size_t          id_v2,
                                            SymMat &        q,
                                            const Vertices &vertices)
{
    double det = q.det(0, 1, 2, 1, 4, 5, 2, 5, 7);
    if (abs(det) < std::numeric_limits<double>::epsilon()) {
        // can't divide by zero
        const Vec3f &v0 = vertices[id_v1];
        const Vec3f &v1 = vertices[id_v2];
        Vec3d verts[3]  = {v0.cast<double>(), v1.cast<double>(), Vec3d()};
        verts[2]        = (verts[0] + verts[1]) / 2;
        double errors[] = {vertex_error(q, verts[0]),
                           vertex_error(q, verts[1]),
                           vertex_error(q, verts[2])};
        auto   mit = std::min_element(std::begin(errors), std::end(errors));
        return verts[mit - std::begin(errors)].cast<float>();
    }

    double det_1 = -1 / det;
    double det_x = q.det(1, 2, 3, 4, 5, 6, 5, 7, 8); // vx = A41/det(q_delta)
    double det_y = q.det(0, 2, 3, 1, 5, 6, 2, 7, 8); // vy = A42/det(q_delta)
    double det_z = q.det(0, 1, 3, 1, 4, 6, 2, 5, 8); // vz = A43/det(q_delta)
    return Vec3f(det_1 * det_x, -det_1 * det_y, det_1 * det_z);
}

double QuadricEdgeCollapse::vertex_error(const SymMat &q, const Vec3d &vertex)
{
    const double &x = vertex.x(), &y = vertex.y(), &z = vertex.z();
    return q[0] * x * x + 2 * q[1] * x * y + 2 * q[2] * x * z +
           2 * q[3] * x + q[4] * y * y + 2 * q[5] * y * z +
           2 * q[6] * y + q[7] * z * z + 2 * q[8] * z + q[9];
}

SymMat QuadricEdgeCollapse::create_quadric(const Triangle &       t,
                                           const TriangleInfo &t_info,
                                           const Vertices &    vertices)
{
    Vec3d n  = t_info.n.cast<double>();
    Vec3d v0 = vertices[t[0]].cast<double>();
    return SymMat(n.x(), n.y(), n.z(), -n.dot(v0));
}

std::tuple<TriangleInfos, VertexInfos, EdgeInfos> QuadricEdgeCollapse::init(
    const indexed_triangle_set &its)
{
    TriangleInfos t_infos(its.indices.size());
    VertexInfos   v_infos(its.vertices.size());
    EdgeInfos     e_infos(its.indices.size() * 3);

    for (size_t i = 0; i < its.indices.size(); i++) { 
        const Triangle &t = its.indices[i];
        TriangleInfo &t_info = t_infos[i];
        t_info.n = create_normal(t, its.vertices);
        SymMat q  = create_quadric(t, t_info, its.vertices);
        for (size_t e = 0; e < 3; e++) { 
            VertexInfo &v_info = v_infos[t[e]];
            v_info.q += q;
            ++v_info.count; // triangle count
        }        
    }

    // set offseted starts
    size_t triangle_start = 0;
    for (VertexInfo &v_info : v_infos) {
        v_info.start = triangle_start;
        triangle_start += v_info.count;
        // set filled vertex to zero
        v_info.count = 0;
    }
    assert(its.indices.size() * 3 == triangle_start);
    
    // calc error + create reference
    for (size_t i = 0; i < its.indices.size(); i++) {
        const Triangle &t = its.indices[i];
        TriangleInfo &t_info = t_infos[i];
        t_info.e = calculate_error(t, its.vertices, v_infos);         
        for (size_t j = 0; j < 3; ++j) {
            VertexInfo &v_info = v_infos[t[j]];
            size_t ei = v_info.start + v_info.count;
            assert(ei < e_infos.size());
            EdgeInfo &e_info = e_infos[ei];
            e_info.t_index  = i;
            e_info.edge      = j;
            ++v_info.count;
        }
    }
    return {t_infos, v_infos, e_infos};
}

size_t QuadricEdgeCollapse::find_triangle_index1(size_t            vi,
                                                 const VertexInfo &v_info,
                                                 size_t ti0,
                                                 const EdgeInfos & e_infos,
                                                 const Indices &indices)
{
    coord_t vi_coord = static_cast<coord_t>(vi);
    size_t end = v_info.start + v_info.count;
    for (size_t ei = v_info.start; ei < end; ++ei) {
        const EdgeInfo &e_info = e_infos[ei];
        if (e_info.t_index == ti0) continue;
        const Triangle& t = indices[e_info.t_index];
        if (t[(e_info.edge + 1) % 3] == vi_coord || 
            t[(e_info.edge + 2) % 3] == vi_coord)
            return e_info.t_index;
    }
    // triangle0 is on border and do NOT have twin edge
    assert(false);
    return -1;
}


bool QuadricEdgeCollapse::is_flipped(const Vec3f &vn,
                                     const Vec3f &v1,
                                     const Vec3f &v2,
                                     const Vec3f &normal)
{
    static const float thr_pos = 1.0f - std::numeric_limits<float>::epsilon();
    static const float thr_neg = -thr_pos;
    static const float dot_thr = 0.2f; // Value from simplify mesh

    Vec3f d1 = v1 - vn;
    d1.normalize();
    Vec3f d2 = v2 - vn;
    d2.normalize();

    float dot = d1.dot(d2);
    if (dot > thr_pos || dot < thr_neg) return true;

    // IMPROVE: propagate new normal
    Vec3f n = d1.cross(d2);
    n.normalize();
    return n.dot(normal) < dot_thr;
}


bool QuadricEdgeCollapse::is_flipped(Vec3f &              new_vertex,
                                     size_t               ti0,
                                     size_t               ti1,
                                     const VertexInfo &   v_info,
                                     const TriangleInfos &t_infos,
                                     const EdgeInfos &    e_infos,
                                     const indexed_triangle_set &its)
{
    // for each vertex triangles
    size_t v_info_end = v_info.start + v_info.count;
    for (size_t ei = v_info.start; ei < v_info_end; ++ei) {
        assert(ei < e_infos.size());
        const EdgeInfo &e_info = e_infos[ei];
        if (e_info.t_index == ti0) continue; // ti0 will be deleted
        if (e_info.t_index == ti1) continue; // ti1 will be deleted
        const Triangle &t      = its.indices[e_info.t_index];
        const Vec3f &normal = t_infos[e_info.t_index].n;
        const Vec3f &vf     = its.vertices[t[(e_info.edge + 1) % 3]];
        const Vec3f &vs     = its.vertices[t[(e_info.edge + 2) % 3]];
        if (is_flipped(new_vertex, vf, vs, normal))
            return true;
    }
    return false;
}

Error QuadricEdgeCollapse::calculate_error(const Triangle &   t,
                                           const Vertices &   vertices,
                                           const VertexInfos &v_infos)
{
    Vec3d error;
    for (size_t j = 0; j < 3; ++j) {
        size_t      j2     = (j == 2) ? 0 : (j + 1);
        size_t      vi0    = t[j];
        size_t      vi1    = t[j2];
        SymMat      q(v_infos[vi0].q); // copy
        q += v_infos[vi1].q;
        error[j] = calculate_error(vi0, vi1, q, vertices);
    }
    unsigned char min_index = (error[0] < error[1]) ?
                                  ((error[0] < error[2]) ? 0 : 2) :
                                  ((error[1] < error[2]) ? 1 : 2);
    return Error(static_cast<float>(error[min_index]), min_index);    
}

void QuadricEdgeCollapse::sub_quadric(const Triangle &t,
                                      const TriangleInfo & t_info,
                                      VertexInfos &v_infos,
                                      const Vertices &vertices)
{
    SymMat quadric = create_quadric(t, t_info, vertices);
    for (size_t vi: t) v_infos[vi].q -= quadric;
}

void QuadricEdgeCollapse::remove_triangle(EdgeInfos & e_infos,
                                          VertexInfo &v_info,
                                          size_t      ti)
{
    auto e_info     = e_infos.begin() + v_info.start;
    auto e_info_end = e_info + v_info.count - 1;
    for (; e_info != e_info_end; ++e_info) {
        if (e_info->t_index == ti) {
            *e_info = *e_info_end;
            --v_info.count;
            return;
        }
    }    
    assert(e_info_end->t_index == ti);
    // last triangle is ti
    --v_info.count;
}

void QuadricEdgeCollapse::change_neighbors(EdgeInfos &     e_infos,
                                           VertexInfos &   v_infos,
                                           size_t          ti0,
                                           size_t          ti1,
                                           size_t          vi0,
                                           size_t          vi1,
                                           size_t          vi_top0,
                                           const Triangle &t1)
{
    // have to copy Edge info from higher vertex index into smaller
    assert(vi0 < vi1);

    
    // vertex index of triangle 1 which is not vi0 nor vi1
    size_t vi_top1 = t1[0];
    if (vi_top1 == vi0 || vi_top1 == vi1) {
        vi_top1 = t1[1];
        if (vi_top1 == vi0 || vi_top1 == vi1) vi_top1 = t1[2];
    }

    remove_triangle(e_infos, v_infos[vi_top0], ti0);
    remove_triangle(e_infos, v_infos[vi_top1], ti1);

    VertexInfo &v_info0 = v_infos[vi0];
    VertexInfo &v_info1 = v_infos[vi1];

    size_t new_triangle_count = v_info0.count + v_info1.count - 4;
    remove_triangle(e_infos, v_info0, ti0);
    remove_triangle(e_infos, v_info0, ti1);

    // copy second's edge infos out of e_infos, to free size
    EdgeInfos e_infos1;
    e_infos1.reserve(v_info1.count - 2);
    size_t v_info_s_end = v_info1.start + v_info1.count;
    for (size_t ei = v_info1.start; ei < v_info_s_end; ++ei) {
        const EdgeInfo &e_info = e_infos[ei];
        if (e_info.t_index == ti0) continue;
        if (e_info.t_index == ti1) continue;
        e_infos1.emplace_back(e_info);
    }
    v_info1.count = 0;

    size_t need = (new_triangle_count < v_info0.count)? 0:
                  (new_triangle_count - v_info0.count);

    size_t      act_vi     = vi0 + 1;
    VertexInfo *act_v_info = &v_infos[act_vi];
    size_t      act_start  = act_v_info->start;
    size_t      last_end   = v_info0.start + v_info0.count;

    struct CopyEdgeInfo
    {
        size_t start;
        size_t count;
        unsigned char move;
        CopyEdgeInfo(size_t start, size_t count, unsigned char move)
            : start(start), count(count), move(move)
        {}
    };
    std::vector<CopyEdgeInfo> c_infos;
    c_infos.reserve(need);
    while (true) {
        size_t save = act_start - last_end;
        if (save > 0) {
            if (save >= need) break;
            need -= save;
            c_infos.emplace_back(act_v_info->start, act_v_info->count, need);
        } else {
            c_infos.back().count += act_v_info->count;
        }
        last_end = act_v_info->start + act_v_info->count;
        act_v_info->start += need; 
        ++act_vi;
        if (act_vi < v_infos.size()) {
            act_v_info = &v_infos[act_vi];
            act_start  = act_v_info->start;
        } else
            act_start = e_infos.size(); // fix for edge between last two triangles
    }

    // copy by c_infos
    for (size_t i = c_infos.size(); i > 0; --i) {
        const CopyEdgeInfo &c_info = c_infos[i - 1];
        for (size_t ei = c_info.start + c_info.count - 1; ei >= c_info.start; --ei)
            e_infos[ei + c_info.move] = e_infos[ei]; // copy
    }

    // copy triangle from first info into second
    for (size_t ei_s = 0; ei_s < e_infos1.size(); ++ei_s) {
        size_t ei_f = v_info0.start + v_info0.count;
        e_infos[ei_f] = e_infos1[ei_s]; // copy
        ++v_info0.count;
    }
}

void QuadricEdgeCollapse::compact(const VertexInfos &   v_infos,
                                  const TriangleInfos & t_infos,
                                  const EdgeInfos &     e_infos,
                                  indexed_triangle_set &its)
{
    size_t vi_new = 0;
    for (size_t vi = 0; vi < v_infos.size(); vi++) {
        const VertexInfo &v_info = v_infos[vi];
        if (v_info.is_deleted()) continue; // deleted
        size_t e_info_end = v_info.start + v_info.count;
        for (size_t ei = v_info.start; ei < e_info_end; ei++) { 
            const EdgeInfo &e_info = e_infos[ei];
            // change vertex index
            its.indices[e_info.t_index][e_info.edge] = vi_new;
        }
        // compact vertices
        its.vertices[vi_new++] = its.vertices[vi];
    }
    // remove vertices tail
    its.vertices.erase(its.vertices.begin() + vi_new, its.vertices.end());

    size_t ti_new = 0;
    for (size_t ti = 0; ti < t_infos.size(); ti++) { 
        const TriangleInfo &t_info = t_infos[ti];
        if (t_info.is_deleted()) continue;
        its.indices[ti_new++] = its.indices[ti];
    }
    its.indices.erase(its.indices.begin() + ti_new, its.indices.end());
}