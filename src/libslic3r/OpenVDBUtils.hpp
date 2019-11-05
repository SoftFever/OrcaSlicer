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

openvdb::FloatGrid::Ptr meshToVolume(const sla::Contour3D &          mesh,
                                     const openvdb::math::Transform &tr = {},
                                     float exteriorBandWidth = 3.0f,
                                     float interiorBandWidth = 3.0f,
                                     int   flags             = 0);

sla::Contour3D volumeToMesh(const openvdb::FloatGrid &grid,
                            double                    isovalue   = 0.0,
                            double                    adaptivity = 0.0,
                            bool relaxDisorientedTriangles       = true);

} // namespace Slic3r

#endif // OPENVDBUTILS_HPP
