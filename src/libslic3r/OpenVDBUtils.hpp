#ifndef OPENVDBUTILS_HPP
#define OPENVDBUTILS_HPP

#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/SLA/SLACommon.hpp>
#include <openvdb/openvdb.h>

namespace Slic3r {

openvdb::FloatGrid::Ptr meshToVolume(const TriangleMesh &            mesh,
                                     const openvdb::math::Transform &tr = {},
                                     float exteriorBandWidth = 3.0f,
                                     float interiorBandWidth = 3.0f,
                                     int   flags             = 0);

TriangleMesh volumeToMesh(const openvdb::FloatGrid &grid,
                          double                    isovalue   = 0.0,
                          double                    adaptivity = 0.0,
                          bool relaxDisorientedTriangles       = true);

using HollowingFilter = std::function<void(openvdb::FloatGrid& grid, double thickness, double scale)>;

// Generate an interior for any solid geometry maintaining a given minimum
// wall thickness. The returned mesh has triangles with normals facing inside
// the mesh so the result can be directly merged with the input to finish the
// hollowing.
TriangleMesh hollowed_interior(const TriangleMesh &mesh, double min_thickness,
                               double quality = 0.5,
                               HollowingFilter filt = nullptr);

} // namespace Slic3r

#endif // OPENVDBUTILS_HPP
