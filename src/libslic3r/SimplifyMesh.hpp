#ifndef MESHSIMPLIFY_HPP
#define MESHSIMPLIFY_HPP

#include <vector>

#include <libslic3r/TriangleMesh.hpp>

namespace Slic3r {

void simplify_mesh(indexed_triangle_set &);

// TODO: (but this can be done with IGL as well)
// void simplify_mesh(indexed_triangle_set &, int face_count, float agressiveness = 0.5f);

template<class...Args> void simplify_mesh(TriangleMesh &m, Args &&...a)
{
    m.require_shared_vertices();
    simplify_mesh(m.its, std::forward<Args>(a)...);
    m = TriangleMesh{m.its};
    m.require_shared_vertices();
}

} // namespace Slic3r

#endif // MESHSIMPLIFY_H
