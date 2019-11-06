#define NOMINMAX
#include "OpenVDBUtils.hpp"
#include <openvdb/tools/MeshToVolume.h>
#include <openvdb/tools/VolumeToMesh.h>
#include <openvdb/tools/Filter.h>
#include <boost/log/trivial.hpp>
#include "MTUtils.hpp"

namespace Slic3r {

class TriangleMeshDataAdapter {
public:
    const TriangleMesh &mesh;
    
    size_t polygonCount() const { return mesh.its.indices.size(); }
    size_t pointCount() const   { return mesh.its.vertices.size(); }
    size_t vertexCount(size_t) const { return 3; }
    
    // Return position pos in local grid index space for polygon n and vertex v
    void getIndexSpacePoint(size_t n, size_t v, openvdb::Vec3d& pos) const;
};

class Contour3DDataAdapter {
public:
    const sla::Contour3D &mesh;
    
    size_t polygonCount() const { return mesh.faces3.size() + mesh.faces4.size(); }
    size_t pointCount() const   { return mesh.points.size(); }
    size_t vertexCount(size_t n) const { return n < mesh.faces3.size() ? 3 : 4; }
    
    // Return position pos in local grid index space for polygon n and vertex v
    void getIndexSpacePoint(size_t n, size_t v, openvdb::Vec3d& pos) const;
};

void TriangleMeshDataAdapter::getIndexSpacePoint(size_t          n,
                                                 size_t          v,
                                                 openvdb::Vec3d &pos) const
{
    auto vidx = size_t(mesh.its.indices[n](Eigen::Index(v)));
    Slic3r::Vec3d p = mesh.its.vertices[vidx].cast<double>();
    pos = {p.x(), p.y(), p.z()};
}

void Contour3DDataAdapter::getIndexSpacePoint(size_t          n,
                                              size_t          v,
                                              openvdb::Vec3d &pos) const
{
    size_t vidx = 0;
    if (n < mesh.faces3.size()) vidx = size_t(mesh.faces3[n](Eigen::Index(v)));
    else vidx = size_t(mesh.faces4[n - mesh.faces3.size()](Eigen::Index(v)));
    
    Slic3r::Vec3d p = mesh.points[vidx];
    pos = {p.x(), p.y(), p.z()};
}


// TODO: Do I need to call initialize? Seems to work without it as well but the
// docs say it should be called ones. It does a mutex lock-unlock sequence all
// even if was called previously.

openvdb::FloatGrid::Ptr meshToVolume(const TriangleMesh &mesh,
                                     const openvdb::math::Transform &tr,
                                     float               exteriorBandWidth,
                                     float               interiorBandWidth,
                                     int                 flags)
{
    openvdb::initialize();
    return openvdb::tools::meshToVolume<openvdb::FloatGrid>(
        TriangleMeshDataAdapter{mesh}, tr, exteriorBandWidth,
        interiorBandWidth, flags);
}

static openvdb::FloatGrid::Ptr meshToVolume(const sla::Contour3D &mesh,
                                     const openvdb::math::Transform &tr,
                                     float exteriorBandWidth,
                                     float interiorBandWidth,
                                     int flags)
{
    openvdb::initialize();
    return openvdb::tools::meshToVolume<openvdb::FloatGrid>(
        Contour3DDataAdapter{mesh}, tr, exteriorBandWidth, interiorBandWidth,
        flags);
}

inline Vec3f to_vec3f(const openvdb::Vec3s &v) { return Vec3f{v.x(), v.y(), v.z()}; }
inline Vec3d to_vec3d(const openvdb::Vec3s &v) { return to_vec3f(v).cast<double>(); }
inline Vec3i to_vec3i(const openvdb::Vec3I &v) { return Vec3i{int(v[0]), int(v[1]), int(v[2])}; }
inline Vec4i to_vec4i(const openvdb::Vec4I &v) { return Vec4i{int(v[0]), int(v[1]), int(v[2]), int(v[3])}; }

template<class Grid>
sla::Contour3D __volumeToMesh(const Grid &grid,
                              double      isovalue,
                              double      adaptivity,
                              bool        relaxDisorientedTriangles)
{
    openvdb::initialize();
    
    std::vector<openvdb::Vec3s> points;
    std::vector<openvdb::Vec3I> triangles;
    std::vector<openvdb::Vec4I> quads;
    
    openvdb::tools::volumeToMesh(grid, points, triangles, quads, isovalue,
                                 adaptivity, relaxDisorientedTriangles);
    
    sla::Contour3D ret;
    ret.points.reserve(points.size());
    ret.faces3.reserve(triangles.size());
    ret.faces4.reserve(quads.size());

    for (auto &v : points) ret.points.emplace_back(to_vec3d(v));
    for (auto &v : triangles) ret.faces3.emplace_back(to_vec3i(v));
    for (auto &v : quads) ret.faces4.emplace_back(to_vec4i(v));

    return ret;
}

template<class Mesh = sla::Contour3D> inline
Mesh _volumeToMesh(const openvdb::FloatGrid &grid,
                   double      isovalue = 0.0,
                   double      adaptivity = 0.0,
                   bool        relaxDisorientedTriangles = true);

template<> inline 
TriangleMesh _volumeToMesh<TriangleMesh>(const openvdb::FloatGrid &grid,
                                         double                    isovalue,
                                         double                    adaptivity,
                                         bool relaxDisorientedTriangles)
{
    return to_triangle_mesh(__volumeToMesh(grid, isovalue, adaptivity,
                                           relaxDisorientedTriangles));
}

template<> inline
sla::Contour3D _volumeToMesh<sla::Contour3D>(const openvdb::FloatGrid &grid,
                                             double isovalue,
                                             double adaptivity,
                                             bool   relaxDisorientedTriangles)
{
    return __volumeToMesh(grid, isovalue, adaptivity,
                          relaxDisorientedTriangles);
}

TriangleMesh volumeToMesh(const openvdb::FloatGrid &grid,
                          double                    isovalue,
                          double                    adaptivity,
                          bool                      relaxDisorientedTriangles)
{
    return _volumeToMesh<TriangleMesh>(grid, isovalue, adaptivity,
                                         relaxDisorientedTriangles);
}

template<class S, class = FloatingOnly<S>>
inline void _scale(S s, TriangleMesh &m) { m.scale(float(s)); }

template<class S, class = FloatingOnly<S>>
inline void _scale(S s, sla::Contour3D &m)
{
    for (auto &p : m.points) p *= s;
}

template<class Mesh>
remove_cvref_t<Mesh> _hollowed_interior(Mesh &&mesh,
                                        double min_thickness,
                                        int    oversampling,
                                        double smoothing)
{
    using MMesh = remove_cvref_t<Mesh>;
    MMesh imesh{std::forward<Mesh>(mesh)};
    
    // I can't figure out how to increase the grid resolution through openvdb API
    // so the model will be scaled up before conversion and the result scaled
    // down. Voxels have a unit size. If I set voxelSize smaller, it scales
    // the whole geometry down, and doesn't increase the number of voxels.
    auto scale = double(oversampling);
    
    _scale(scale, imesh);
    
    double offset = scale * min_thickness;    
    float range = float(std::max(2 * offset, scale));
    auto gridptr = meshToVolume(imesh, {}, 0.1f * float(offset), range);
    
    assert(gridptr);
    
    if (!gridptr) {
        BOOST_LOG_TRIVIAL(error) << "Returned OpenVDB grid is NULL";
        return MMesh{};
    }
    
    // Filtering:
    int width = int(smoothing * scale);
    int count = 1;
    openvdb::tools::Filter<openvdb::FloatGrid>{*gridptr}.gaussian(width, count);
    
    double iso_surface = -offset;
    double adaptivity = 0.;
    auto omesh = _volumeToMesh<MMesh>(*gridptr, iso_surface, adaptivity);
    
    _scale(1. / scale, omesh);
    
    return omesh;
}

TriangleMesh hollowed_interior(const TriangleMesh &mesh,
                               double              min_thickness,
                               int                 oversampling,
                               double              smoothing)
{
    return _hollowed_interior(mesh, min_thickness, oversampling, smoothing);
}

} // namespace Slic3r
