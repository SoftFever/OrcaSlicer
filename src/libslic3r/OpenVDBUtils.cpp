#define NOMINMAX
#include "OpenVDBUtils.hpp"

#ifdef _MSC_VER
// Suppress warning C4146 in OpenVDB: unary minus operator applied to unsigned type, result still unsigned 
#pragma warning(push)
#pragma warning(disable : 4146)
#endif // _MSC_VER
#include <openvdb/tools/MeshToVolume.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif // _MSC_VER

#include <openvdb/tools/VolumeToMesh.h>
#include <openvdb/tools/Composite.h>
#include <openvdb/tools/LevelSetRebuild.h>

//#include "MTUtils.hpp"

namespace Slic3r {

class TriangleMeshDataAdapter {
public:
    const TriangleMesh &mesh;
    float voxel_scale;

    size_t polygonCount() const { return mesh.its.indices.size(); }
    size_t pointCount() const   { return mesh.its.vertices.size(); }
    size_t vertexCount(size_t) const { return 3; }

    // Return position pos in local grid index space for polygon n and vertex v
    // The actual mesh will appear to openvdb as scaled uniformly by voxel_size
    // And the voxel count per unit volume can be affected this way.
    void getIndexSpacePoint(size_t n, size_t v, openvdb::Vec3d& pos) const
    {
        auto vidx = size_t(mesh.its.indices[n](Eigen::Index(v)));
        Slic3r::Vec3d p = mesh.its.vertices[vidx].cast<double>() * voxel_scale;
        pos = {p.x(), p.y(), p.z()};
    }

    TriangleMeshDataAdapter(const TriangleMesh &m, float voxel_sc = 1.f)
        : mesh{m}, voxel_scale{voxel_sc} {};
};

// TODO: Do I need to call initialize? Seems to work without it as well but the
// docs say it should be called ones. It does a mutex lock-unlock sequence all
// even if was called previously.
openvdb::FloatGrid::Ptr mesh_to_grid(const TriangleMesh &            mesh,
                                     const openvdb::math::Transform &tr,
                                     float voxel_scale,
                                     float exteriorBandWidth,
                                     float interiorBandWidth,
                                     int   flags)
{
    openvdb::initialize();

    TriangleMeshPtrs meshparts_raw = mesh.split();
    auto meshparts = reserve_vector<std::unique_ptr<TriangleMesh>>(meshparts_raw.size());
    for (auto *p : meshparts_raw)
        meshparts.emplace_back(p);

    auto it = std::remove_if(meshparts.begin(), meshparts.end(), [](auto &m) {
         m->require_shared_vertices();
         return m->volume() < EPSILON;
     });

    meshparts.erase(it, meshparts.end());

    openvdb::FloatGrid::Ptr grid;
    for (auto &m : meshparts) {
        auto subgrid = openvdb::tools::meshToVolume<openvdb::FloatGrid>(
            TriangleMeshDataAdapter{*m, voxel_scale}, tr, exteriorBandWidth,
            interiorBandWidth, flags);

        if (grid && subgrid) openvdb::tools::csgUnion(*grid, *subgrid);
        else if (subgrid) grid = std::move(subgrid);
    }

    if (grid) {
        grid = openvdb::tools::levelSetRebuild(*grid, 0., exteriorBandWidth,
                                               interiorBandWidth);
    } else if(meshparts.empty()) {
        // Splitting failed, fall back to hollow the original mesh
        grid = openvdb::tools::meshToVolume<openvdb::FloatGrid>(
            TriangleMeshDataAdapter{mesh}, tr, exteriorBandWidth,
            interiorBandWidth, flags);
    }

    grid->insertMeta("voxel_scale", openvdb::FloatMetadata(voxel_scale));

    return grid;
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

    float scale = 1.;
    try {
        scale = grid.template metaValue<float>("voxel_scale");
    }  catch (...) { }

    sla::Contour3D ret;
    ret.points.reserve(points.size());
    ret.faces3.reserve(triangles.size());
    ret.faces4.reserve(quads.size());

    for (auto &v : points) ret.points.emplace_back(to_vec3d(v) / scale);
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

openvdb::FloatGrid::Ptr redistance_grid(const openvdb::FloatGrid &grid,
                                        double                    iso,
                                        double                    er,
                                        double                    ir)
{
    auto new_grid = openvdb::tools::levelSetRebuild(grid, float(iso),
                                                    float(er), float(ir));

    // Copies voxel_scale metadata, if it exists.
    new_grid->insertMeta(*grid.deepCopyMeta());

    return new_grid;
}

} // namespace Slic3r
