#ifndef libslic3r_MeshBoolean_hpp_
#define libslic3r_MeshBoolean_hpp_


namespace Slic3r {

class TriangleMesh;

namespace MeshBoolean {

void minus(TriangleMesh& A, const TriangleMesh& B);
void self_union(TriangleMesh& mesh);


} // namespace MeshBoolean
} // namespace Slic3r
#endif // libslic3r_MeshBoolean_hpp_
