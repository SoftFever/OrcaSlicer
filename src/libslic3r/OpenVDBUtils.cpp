#define NOMINMAX
#include "OpenVDBUtils.hpp"
#include <openvdb/tools/MeshToVolume.h>
#include <openvdb/tools/VolumeToMesh.h>
#include <openvdb/tools/Composite.h>
#include <openvdb/tools/LevelSetRebuild.h>

//#include "MTUtils.hpp"

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
openvdb::FloatGrid::Ptr mesh_to_grid(const TriangleMesh &mesh,
                                     const openvdb::math::Transform &tr,
                                     float               exteriorBandWidth,
                                     float               interiorBandWidth,
                                     int                 flags)
{
//    openvdb::initialize();
//    return openvdb::tools::meshToVolume<openvdb::FloatGrid>(
//        TriangleMeshDataAdapter{mesh}, tr, exteriorBandWidth,
//        interiorBandWidth, flags);

    openvdb::initialize();

    TriangleMeshPtrs meshparts = mesh.split();

    auto it = std::remove_if(meshparts.begin(), meshparts.end(),
    [](TriangleMesh *m){
        m->require_shared_vertices();
        return !m->is_manifold() || m->volume() < EPSILON;
    });

    meshparts.erase(it, meshparts.end());

    openvdb::FloatGrid::Ptr grid;
    for (TriangleMesh *m : meshparts) {
        auto gridptr = openvdb::tools::meshToVolume<openvdb::FloatGrid>(
            TriangleMeshDataAdapter{*m}, tr, exteriorBandWidth,
            interiorBandWidth, flags);

        if (grid && gridptr) openvdb::tools::csgUnion(*grid, *gridptr);
        else if (gridptr) grid = std::move(gridptr);
    }

    grid = openvdb::tools::levelSetRebuild(*grid, 0., exteriorBandWidth, interiorBandWidth);

    return grid;
}

openvdb::FloatGrid::Ptr mesh_to_grid(const sla::Contour3D &mesh,
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

template<class Grid>
sla::Contour3D _volumeToMesh(const Grid &grid,
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

TriangleMesh grid_to_mesh(const openvdb::FloatGrid &grid,
                          double                    isovalue,
                          double                    adaptivity,
                          bool                      relaxDisorientedTriangles)
{
    return to_triangle_mesh(
        _volumeToMesh(grid, isovalue, adaptivity, relaxDisorientedTriangles));
}

sla::Contour3D grid_to_contour3d(const openvdb::FloatGrid &grid,
                                 double                    isovalue,
                                 double                    adaptivity,
                                 bool relaxDisorientedTriangles)
{
    return _volumeToMesh(grid, isovalue, adaptivity,
                         relaxDisorientedTriangles);
}

openvdb::FloatGrid::Ptr redistance_grid(const openvdb::FloatGrid &grid, double iso, double er, double ir)
{
    return openvdb::tools::levelSetRebuild(grid, float(iso), float(er), float(ir));
}

} // namespace Slic3r
