#ifndef MESHSPLITIMPL_HPP
#define MESHSPLITIMPL_HPP

#include "TriangleMesh.hpp"
#include "libnest2d/tools/benchmark.h"

namespace Slic3r {

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
        return its_create_neighbors_index(its);
    }
};

// Visit all unvisited neighboring facets that are reachable from the first unvisited facet,
// and return them.
template<class NeighborIndex>
std::vector<size_t> its_find_unvisited_neighbors(
    const indexed_triangle_set &its,
    const NeighborIndex &       neighbor_index,
    std::vector<bool> &         visited)
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
            if (neighbor_idx >= 0 && !visited[size_t(neighbor_idx)]) {
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

    std::vector<bool> visited(its.indices.size(), false);

    const size_t        UNASSIGNED = its.vertices.size();
    std::vector<size_t> vidx_conv(its.vertices.size());

    const auto& neighbor_index = ItsWithNeighborsIndex_<Its>::get_index(m);

    for (;;) {
        std::vector<size_t> facets =
            its_find_unvisited_neighbors(its, neighbor_index, visited);

        if (facets.empty())
            break;

        std::fill(vidx_conv.begin(), vidx_conv.end(), UNASSIGNED);

        // Create a new mesh for the part that was just split off.
        indexed_triangle_set mesh;

        // Assign the facets to the new mesh.
        for (size_t face_id : facets) {
            const auto &face = its.indices[face_id];
            Vec3i       new_face;
            for (size_t v = 0; v < 3; ++v) {
                auto vi = face(v);

                if (vidx_conv[vi] == UNASSIGNED) {
                    vidx_conv[vi] = mesh.vertices.size();
                    mesh.vertices.emplace_back(its.vertices[size_t(vi)]);
                }

                new_face(v) = vidx_conv[vi];
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

    std::vector<bool> visited(its.indices.size(), false);
    its_find_unvisited_neighbors(its, neighbor_index, visited);

    // Try finding an unvisited facet. If there are none, the mesh is not splittable.
    auto it = std::find(visited.begin(), visited.end(), false);

    return it != visited.end();
}

} // namespace Slic3r

#endif // MESHSPLITIMPL_HPP
