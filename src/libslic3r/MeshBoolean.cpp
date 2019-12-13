#include "MeshBoolean.hpp"

// Include igl first. It defines "L" macro which then clashes with our localization
#include <igl/copyleft/cgal/mesh_boolean.h>
#undef L

#include "libslic3r/TriangleMesh.hpp"


namespace Slic3r {
namespace MeshBoolean {

typedef Eigen::Map<const Eigen::Matrix<float, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor | Eigen::DontAlign>> MapMatrixXfUnaligned;
typedef Eigen::Map<const Eigen::Matrix<int,   Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor | Eigen::DontAlign>> MapMatrixXiUnaligned;

static TriangleMesh eigen_to_triangle_mesh(const Eigen::MatrixXd& VC, const Eigen::MatrixXi& FC)
{
    Pointf3s vertices;
    for (size_t i=0; i<VC.rows(); ++i)
        vertices.push_back(Vec3d(VC(i,0), VC(i,1), VC(i,2)));

    std::vector<Vec3crd> facets;
    for (size_t i=0; i<FC.rows(); ++i)
        facets.push_back(Vec3crd(FC(i,0), FC(i,1), FC(i,2)));

    TriangleMesh out(vertices, facets);
    out.require_shared_vertices();
    return out;
}

void minus(TriangleMesh& A, const TriangleMesh& B)
{
    Eigen::MatrixXd VA = MapMatrixXfUnaligned(A.its.vertices.front().data(), A.its.vertices.size(), 3).cast<double>();
    Eigen::MatrixXi FA = MapMatrixXiUnaligned(A.its.indices.front().data(), A.its.indices.size(), 3);
    Eigen::MatrixXd VB = MapMatrixXfUnaligned(B.its.vertices.front().data(), B.its.vertices.size(), 3).cast<double>();
    Eigen::MatrixXi FB = MapMatrixXiUnaligned(B.its.indices.front().data(), B.its.indices.size(), 3);

    Eigen::MatrixXd VC;
    Eigen::MatrixXi FC;
    igl::MeshBooleanType boolean_type(igl::MESH_BOOLEAN_TYPE_MINUS);
    igl::copyleft::cgal::mesh_boolean(VA, FA, VB, FB, boolean_type, VC, FC);

    A = eigen_to_triangle_mesh(VC, FC);
}


void self_union(TriangleMesh& mesh)
{
    Eigen::MatrixXd V = MapMatrixXfUnaligned(mesh.its.vertices.front().data(), mesh.its.vertices.size(), 3).cast<double>();
    Eigen::MatrixXi F = MapMatrixXiUnaligned(mesh.its.indices.front().data(), mesh.its.indices.size(), 3);

    Eigen::MatrixXd VC;
    Eigen::MatrixXi FC;

    igl::MeshBooleanType boolean_type(igl::MESH_BOOLEAN_TYPE_UNION);
    igl::copyleft::cgal::mesh_boolean(V, F, Eigen::MatrixXd(), Eigen::MatrixXi(), boolean_type, VC, FC);
    mesh = eigen_to_triangle_mesh(VC, FC);
}



} // namespace MeshBoolean
} // namespace Slic3r
