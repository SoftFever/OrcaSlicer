#ifndef SLA_CONTOUR3D_HPP
#define SLA_CONTOUR3D_HPP

#include <libslic3r/SLA/Common.hpp>

#include <libslic3r/TriangleMesh.hpp>

namespace Slic3r { namespace sla {

class EigenMesh3D;

/// Dumb vertex mesh consisting of triangles (or) quads. Capable of merging with
/// other meshes of this type and converting to and from other mesh formats.
struct Contour3D {
    std::vector<Vec3d> points;
    std::vector<Vec3i> faces3;
    std::vector<Vec4i> faces4;
    
    Contour3D() = default;
    Contour3D(const TriangleMesh &trmesh);
    Contour3D(TriangleMesh &&trmesh);
    Contour3D(const EigenMesh3D  &emesh);
    
    Contour3D& merge(const Contour3D& ctr);
    Contour3D& merge(const Pointf3s& triangles);
    
    // Write the index triangle structure to OBJ file for debugging purposes.
    void to_obj(std::ostream& stream);
    void from_obj(std::istream &stream);

    inline bool empty() const
    {
        return points.empty() || (faces4.empty() && faces3.empty());
    }
};

/// Mesh from an existing contour.
TriangleMesh to_triangle_mesh(const Contour3D& ctour);

/// Mesh from an evaporating 3D contour
TriangleMesh to_triangle_mesh(Contour3D&& ctour);

}} // namespace Slic3r::sla

#endif // CONTOUR3D_HPP
