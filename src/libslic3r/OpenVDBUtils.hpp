#ifndef OPENVDBUTILS_HPP
#define OPENVDBUTILS_HPP

#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/SLA/SLACommon.hpp>
#include <openvdb/openvdb.h>

namespace Slic3r {

inline Vec3f to_vec3f(const openvdb::Vec3s &v) { return Vec3f{v.x(), v.y(), v.z()}; }
inline Vec3d to_vec3d(const openvdb::Vec3s &v) { return to_vec3f(v).cast<double>(); }
inline Vec3i to_vec3i(const openvdb::Vec3I &v) { return Vec3i{int(v[0]), int(v[1]), int(v[2])}; }
inline Vec4i to_vec4i(const openvdb::Vec4I &v) { return Vec4i{int(v[0]), int(v[1]), int(v[2]), int(v[3])}; }

openvdb::FloatGrid::Ptr mesh_to_grid(const TriangleMesh &            mesh,
                                     const openvdb::math::Transform &tr = {},
                                     float exteriorBandWidth = 3.0f,
                                     float interiorBandWidth = 3.0f,
                                     int   flags             = 0);

openvdb::FloatGrid::Ptr mesh_to_grid(const sla::Contour3D &          mesh,
                                     const openvdb::math::Transform &tr = {},
                                     float exteriorBandWidth = 3.0f,
                                     float interiorBandWidth = 3.0f,
                                     int   flags             = 0);

sla::Contour3D grid_to_contour3d(const openvdb::FloatGrid &grid,
                                 double                    isovalue,
                                 double                    adaptivity,
                                 bool relaxDisorientedTriangles = true);

TriangleMesh grid_to_mesh(const openvdb::FloatGrid &grid,
                          double                    isovalue   = 0.0,
                          double                    adaptivity = 0.0,
                          bool relaxDisorientedTriangles       = true);

openvdb::FloatGrid::Ptr redistance_grid(const openvdb::FloatGrid &grid,
                                        double                    iso,
                                        double ext_range = 3.,
                                        double int_range = 3.);

} // namespace Slic3r

#endif // OPENVDBUTILS_HPP
