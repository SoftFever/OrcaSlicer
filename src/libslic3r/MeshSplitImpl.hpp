#ifndef MESHSPLITIMPL_HPP
#define MESHSPLITIMPL_HPP

#include "TriangleMesh.hpp"
#include "libnest2d/tools/benchmark.h"
#include "Execution/ExecutionTBB.hpp"

namespace Slic3r {

template<class ExPolicy>
std::vector<Vec3i> create_face_neighbors_index(ExPolicy &&ex, const indexed_triangle_set &its);

namespace meshsplit_detail {

template<class Its, class Enable = void> struct ItsWithNeighborsIndex_ {
    using Index = typename Its::Index;
    static const indexed_triangle_set &get_its(const Its &m) { return m.get_its();}
    static const Index &get_index(const Its &m) { return m.get_index(); }
};

// Define a default neighbors index for indexed_triangle_set
template<> struct ItsWithNeighborsIndex_<indexed_triangle_set> {
    using Index = std::vector<Vec3i>;
    static const indexed_triangle_set &get_its(const indexed_triangle_set &its) noexcept { return its; }
    static Index get_index(const indexed_triangle_set &its) noexcept
    {
        return create_face_neighbors_index(ex_tbb, its);
    }
};

// Visit all unvisited neighboring facets that are reachable from the first unvisited facet,
// and return them.
template<class NeighborIndex>
std::vector<size_t> its_find_unvisited_neighbors(
    const indexed_triangle_set &its,
    const NeighborIndex &       neighbor_index,
    std::vector<char> &         visited)
{
    using stack_el = size_t;

    auto facestack = reserve_vector<stack_el>(its.indices.size());
    auto push = [&facestack] (const stack_el &s) { facestack.emplace_back(s); };
    auto pop  = [&facestack] () -> stack_el {
        stack_el ret = facestack.back();
        facestack.pop_back();
        return ret;
    };

    // find the next unvisited facet and push the index
    auto facet = std::find(visited.begin(), visited.end(), false);
    std::vector<size_t> ret;

    if (facet != visited.end()) {
        ret.reserve(its.indices.size());
        auto idx = size_t(facet - visited.begin());
        push(idx);
        ret.emplace_back(idx);
        visited[idx] = true;
    }

    while (!facestack.empty()) {
        size_t facet_idx = pop();
        const auto &neighbors = neighbor_index[facet_idx];
        for (auto neighbor_idx : neighbors) {
            if (size_t(neighbor_idx) < visited.size() && !visited[size_t(neighbor_idx)]) {
                visited[size_t(neighbor_idx)] = true;
                push(stack_el(neighbor_idx));
                ret.emplace_back(size_t(neighbor_idx));
            }
        }
    }

    return ret;
}

} // namespace meshsplit_detail

template<class IndexT> struct ItsNeighborsWrapper
{
    using Index = IndexT;
    const indexed_triangle_set *its;
    IndexT index;

    ItsNeighborsWrapper(const indexed_triangle_set &m, IndexT &&idx)
        : its{&m}, index{std::move(idx)}
    {}

    const auto& get_its() const noexcept { return *its; }
    const auto& get_index() const noexcept { return index; }
};

// Splits a mesh into multiple meshes when possible.
template<class Its, class OutputIt>
void its_split(const Its &m, OutputIt out_it)
{
    using namespace meshsplit_detail;

    const indexed_triangle_set &its = ItsWithNeighborsIndex_<Its>::get_its(m);

    std::vector<char> visited(its.indices.size(), false);

    struct VertexConv {
        size_t part_id      = std::numeric_limits<size_t>::max();
        size_t vertex_image;
    };
    std::vector<VertexConv> vidx_conv(its.vertices.size());

    const auto& neighbor_index = ItsWithNeighborsIndex_<Its>::get_index(m);

    for (size_t part_id = 0;; ++part_id) {
        std::vector<size_t> facets =
            its_find_unvisited_neighbors(its, neighbor_index, visited);

        if (facets.empty())
            break;

        // Create a new mesh for the part that was just split off.
        indexed_triangle_set mesh;
        mesh.indices.reserve(facets.size());
        mesh.vertices.reserve(std::min(facets.size() * 3, its.vertices.size()));

        // Assign the facets to the new mesh.
        for (size_t face_id : facets) {
            const auto &face = its.indices[face_id];
            Vec3i       new_face;
            for (size_t v = 0; v < 3; ++v) {
                auto vi = face(v);

                if (vidx_conv[vi].part_id != part_id) {
                    vidx_conv[vi] = {part_id, mesh.vertices.size()};
                    mesh.vertices.emplace_back(its.vertices[size_t(vi)]);
                }

                new_face(v) = vidx_conv[vi].vertex_image;
            }

            mesh.indices.emplace_back(new_face);
        }

        out_it = std::move(mesh);
    }
}

template<class Its>
std::vector<indexed_triangle_set> its_split(const Its &its)
{
    auto ret = reserve_vector<indexed_triangle_set>(3);
    its_split(its, std::back_inserter(ret));

    return ret;
}

template<class Its> bool its_is_splittable(const Its &m)
{
    using namespace meshsplit_detail;
    const indexed_triangle_set &its = ItsWithNeighborsIndex_<Its>::get_its(m);
    const auto& neighbor_index = ItsWithNeighborsIndex_<Its>::get_index(m);

    std::vector<char> visited(its.indices.size(), false);
    its_find_unvisited_neighbors(its, neighbor_index, visited);
    auto faces = its_find_unvisited_neighbors(its, neighbor_index, visited);

    return !faces.empty();
}

template<class ExPolicy>
std::vector<Vec3i> create_face_neighbors_index(ExPolicy &&ex, const indexed_triangle_set &its)
{
    const std::vector<stl_triangle_vertex_indices> &indices = its.indices;

    if (indices.empty()) return {};

    assert(! its.vertices.empty());

    auto               vertex_triangles = VertexFaceIndex{its};
    static constexpr int no_value         = -1;
    std::vector<Vec3i> neighbors(indices.size(),
                                 Vec3i(no_value, no_value, no_value));

    //for (const stl_triangle_vertex_indices& triangle_indices : indices) {
    execution::for_each(ex, size_t(0), indices.size(),
        [&neighbors, &indices, &vertex_triangles] (size_t face_idx)
        {
            Vec3i& neighbor = neighbors[face_idx];
            const stl_triangle_vertex_indices & triangle_indices = indices[face_idx];
            for (int edge_index = 0; edge_index < 3; ++edge_index) {
                // check if done
                int& neighbor_edge = neighbor[edge_index];
                if (neighbor_edge != no_value) 
                    // This edge already has a neighbor assigned.
                    continue;
                Vec2i edge_indices = its_triangle_edge(triangle_indices, edge_index);
                // IMPROVE: use same vector for 2 sides of triangle
                for (const size_t other_face : vertex_triangles[edge_indices[0]]) {
                    if (other_face <= face_idx) continue;
                    const stl_triangle_vertex_indices &face_indices = indices[other_face];
                    int vertex_index = its_triangle_vertex_index(face_indices, edge_indices[1]);
                    // NOT Contain second vertex?
                    if (vertex_index < 0) continue;
                    // Has NOT oposite direction?
                    if (edge_indices[0] != face_indices[(vertex_index + 1) % 3]) continue;
                    neighbor_edge = other_face;
                    neighbors[other_face][vertex_index] = face_idx;
                    break;
                }
            }
        }, execution::max_concurrency(ex));

    return neighbors;
}

} // namespace Slic3r

#endif // MESHSPLITIMPL_HPP
