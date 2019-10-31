#include "openvdb_utils.hpp"

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

openvdb::FloatGrid::Ptr meshToVolume(const TriangleMesh &            mesh,
                                     const openvdb::math::Transform &tr)
{
    return openvdb::tools::meshToVolume<openvdb::FloatGrid>(
                TriangleMeshDataAdapter{mesh}, tr);
}

openvdb::FloatGrid::Ptr meshToVolume(const sla::Contour3D &          mesh,
                                     const openvdb::math::Transform &tr)
{
    return openvdb::tools::meshToVolume<openvdb::FloatGrid>(
        Contour3DDataAdapter{mesh}, tr);
}

inline Vec3f to_vec3f(const openvdb::Vec3s &v) { return Vec3f{v.x(), v.y(), v.z()}; }
inline Vec3d to_vec3d(const openvdb::Vec3s &v) { return to_vec3f(v).cast<double>(); }
inline Vec3i to_vec3i(const openvdb::Vec3I &v) { return Vec3i{int(v[0]), int(v[1]), int(v[2])}; }
inline Vec4i to_vec4i(const openvdb::Vec4I &v) { return Vec4i{int(v[0]), int(v[1]), int(v[2]), int(v[3])}; }

sla::Contour3D volumeToMesh(const openvdb::FloatGrid &grid,
                            double                    isovalue,
                            double                    adaptivity,
                            bool relaxDisorientedTriangles)
{
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

} // namespace Slic3r
