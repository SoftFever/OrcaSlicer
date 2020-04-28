#ifndef SLICESTOTRIANGLEMESH_HPP
#define SLICESTOTRIANGLEMESH_HPP

#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/ExPolygon.hpp"

namespace Slic3r {

void slices_to_triangle_mesh(TriangleMesh &                 mesh,
                             const std::vector<ExPolygons> &slices,
                             double                         zmin,
                             double                         lh,
                             double                         ilh);

inline TriangleMesh slices_to_triangle_mesh(
    const std::vector<ExPolygons> &slices, double zmin, double lh, double ilh)
{
    TriangleMesh out; slices_to_triangle_mesh(out, slices, zmin, lh, ilh);
    return out;
}

} // namespace Slic3r

#endif // SLICESTOTRIANGLEMESH_HPP
