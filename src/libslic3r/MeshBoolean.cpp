#include "MeshBoolean.hpp"
#include "libslic3r/TriangleMesh.hpp"
#undef PI

// Include igl first. It defines "L" macro which then clashes with our localization
#include <igl/copyleft/cgal/mesh_boolean.h>
#undef L

// CGAL headers
#include <CGAL/Polygon_mesh_processing/corefinement.h>
#include <CGAL/Exact_integer.h>
#include <CGAL/Surface_mesh.h>

namespace Slic3r {
namespace MeshBoolean {

typedef Eigen::Map<const Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor | Eigen::DontAlign>> MapMatrixXfUnaligned;
typedef Eigen::Map<const Eigen::Matrix<int,   Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor | Eigen::DontAlign>> MapMatrixXiUnaligned;

typedef std::pair<Eigen::MatrixXd, Eigen::MatrixXi> EigenMesh;

static TriangleMesh eigen_to_triangle_mesh(const Eigen::MatrixXd& VC, const Eigen::MatrixXi& FC)
{
    Pointf3s points(size_t(VC.rows())); 
    std::vector<Vec3crd> facets(size_t(FC.rows()));
    
    for (Eigen::Index i = 0; i < VC.rows(); ++i)
        points[size_t(i)] = VC.row(i);
    
    for (Eigen::Index i = 0; i < FC.rows(); ++i)
        facets[size_t(i)] = FC.row(i);
    
    TriangleMesh out{points, facets};
    out.require_shared_vertices();
    return out;
}

static EigenMesh triangle_mesh_to_eigen_mesh(const TriangleMesh &mesh)
{
    EigenMesh emesh;
    emesh.first = MapMatrixXfUnaligned(mesh.its.vertices.front().data(),
                                       Eigen::Index(mesh.its.vertices.size()),
                                       3).cast<double>();

    emesh.second = MapMatrixXiUnaligned(mesh.its.indices.front().data(),
                                        Eigen::Index(mesh.its.indices.size()),
                                        3);
    return emesh;
}

void minus(TriangleMesh& A, const TriangleMesh& B)
{
    auto [VA, FA] = triangle_mesh_to_eigen_mesh(A);
    auto [VB, FB] = triangle_mesh_to_eigen_mesh(B);

    Eigen::MatrixXd VC;
    Eigen::MatrixXi FC;
    igl::MeshBooleanType boolean_type(igl::MESH_BOOLEAN_TYPE_MINUS);
    igl::copyleft::cgal::mesh_boolean(VA, FA, VB, FB, boolean_type, VC, FC);

    A = eigen_to_triangle_mesh(VC, FC);
}

void self_union(TriangleMesh& mesh)
{
    auto [V, F] = triangle_mesh_to_eigen_mesh(mesh);

    Eigen::MatrixXd VC;
    Eigen::MatrixXi FC;

    igl::MeshBooleanType boolean_type(igl::MESH_BOOLEAN_TYPE_UNION);
    igl::copyleft::cgal::mesh_boolean(V, F, Eigen::MatrixXd(), Eigen::MatrixXi(), boolean_type, VC, FC);
    
    mesh = eigen_to_triangle_mesh(VC, FC);
}

namespace cgal {

namespace CGALProc   = CGAL::Polygon_mesh_processing;
namespace CGALParams = CGAL::Polygon_mesh_processing::parameters;

using Kernel = CGAL::Exact_predicates_inexact_constructions_kernel;
using _CGALMesh = CGAL::Surface_mesh<Kernel::Point_3>;

struct CGALMesh { _CGALMesh m; };

static void triangle_mesh_to_cgal(const TriangleMesh &M, _CGALMesh &out)
{
    for (const Vec3f &v : M.its.vertices)
        out.add_vertex(_CGALMesh::Point(v.x(), v.y(), v.z()));

    for (const Vec3crd &face : M.its.indices) {
        auto f = face.cast<CGAL::SM_Vertex_index>();
        out.add_face(f(0), f(1), f(2));
    }
}

static TriangleMesh cgal_to_triangle_mesh(const _CGALMesh &cgalmesh)
{
    Pointf3s points;
    std::vector<Vec3crd> facets;
    points.reserve(cgalmesh.num_vertices());
    facets.reserve(cgalmesh.num_faces());

    for (auto &vi : cgalmesh.vertices()) {
        auto &v = cgalmesh.point(vi); // Don't ask...
        points.emplace_back(v.x(), v.y(), v.z());
    }

    for (auto &face : cgalmesh.faces()) {
        auto    vtc = cgalmesh.vertices_around_face(cgalmesh.halfedge(face));
        int     i   = 0;
        Vec3crd trface;
        for (auto v : vtc) trface(i++) = int(v.idx());
        facets.emplace_back(trface);
    }
    
    TriangleMesh out{points, facets};
    out.require_shared_vertices();
    return out;
}

std::unique_ptr<CGALMesh> triangle_mesh_to_cgal(const TriangleMesh &M)
{
    auto out = std::make_unique<CGALMesh>();
    triangle_mesh_to_cgal(M, out->m);
    return out;
}

void cgal_to_triangle_mesh(const CGALMesh &cgalmesh, TriangleMesh &out)
{
    out = cgal_to_triangle_mesh(cgalmesh.m);
}

void minus(CGALMesh &A, CGALMesh &B)
{
    CGALProc::corefine_and_compute_difference(A.m, B.m, A.m);
}

void self_union(CGALMesh &A)
{
    CGALProc::corefine(A.m, A.m);
}

void minus(TriangleMesh &A, const TriangleMesh &B)
{   
    CGALMesh meshA;
    CGALMesh meshB;
    triangle_mesh_to_cgal(A, meshA.m);
    triangle_mesh_to_cgal(B, meshB.m);
    
    CGALMesh meshResult;
    bool success = false;
    try {
        success = CGALProc::corefine_and_compute_difference(meshA.m, meshB.m, meshResult.m,
            CGALParams::throw_on_self_intersection(true), CGALParams::throw_on_self_intersection(true));
    }
    catch (const CGAL::Polygon_mesh_processing::Corefinement::Self_intersection_exception&) {
        success = false;
    }
    if (! success)
        throw std::runtime_error("CGAL corefine_and_compute_difference failed");

    A = cgal_to_triangle_mesh(meshResult.m);
}

void self_union(TriangleMesh &m)
{
    _CGALMesh cgalmesh;
    triangle_mesh_to_cgal(m, cgalmesh);
    CGALProc::corefine(cgalmesh, cgalmesh);
    
    m = cgal_to_triangle_mesh(cgalmesh);
}

} // namespace cgal

} // namespace MeshBoolean
} // namespace Slic3r
