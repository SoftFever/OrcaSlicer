#ifndef libslic3r_MeshBoolean_hpp_
#define libslic3r_MeshBoolean_hpp_

#include <memory>

namespace Slic3r {

class TriangleMesh;

namespace MeshBoolean {

void minus(TriangleMesh& A, const TriangleMesh& B);
void self_union(TriangleMesh& mesh);

namespace cgal {

struct CGALMesh;

std::unique_ptr<CGALMesh> triangle_mesh_to_cgal(const TriangleMesh &M);
void cgal_to_triangle_mesh(const CGALMesh &cgalmesh, TriangleMesh &out);
    
// Do boolean mesh difference with CGAL bypassing igl.
void minus(TriangleMesh &A, const TriangleMesh &B);

// Do self union only with CGAL.
void self_union(TriangleMesh& mesh);

// does A = A - B
// CGAL takes non-const objects as arguments. I suppose it doesn't change B but
// there is no official garantee.
void minus(CGALMesh &A, CGALMesh &B);
void self_union(CGALMesh &A);

}

} // namespace MeshBoolean
} // namespace Slic3r
#endif // libslic3r_MeshBoolean_hpp_
