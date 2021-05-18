#ifndef slic3r_MeshUtils_hpp_
#define slic3r_MeshUtils_hpp_

#include "libslic3r/Point.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/SLA/IndexedMesh.hpp"
#include "admesh/stl.h"

#include "slic3r/GUI/3DScene.hpp"

#include <cfloat>

namespace Slic3r {

namespace GUI {

struct Camera;


// lm_FIXME: Following class might possibly be replaced by Eigen::Hyperplane
class ClippingPlane
{
    double m_data[4];

public:
    ClippingPlane()
    {
        *this = ClipsNothing();
    }

    ClippingPlane(const Vec3d& direction, double offset)
    {
        Vec3d norm_dir = direction.normalized();
        m_data[0] = norm_dir(0);
        m_data[1] = norm_dir(1);
        m_data[2] = norm_dir(2);
        m_data[3] = offset;
    }

    bool operator==(const ClippingPlane& cp) const {
        return m_data[0]==cp.m_data[0] && m_data[1]==cp.m_data[1] && m_data[2]==cp.m_data[2] && m_data[3]==cp.m_data[3];
    }
    bool operator!=(const ClippingPlane& cp) const { return ! (*this==cp); }

    double distance(const Vec3d& pt) const {
        // FIXME: this fails: assert(is_approx(get_normal().norm(), 1.));
        return (-get_normal().dot(pt) + m_data[3]);
    }

    bool is_point_clipped(const Vec3d& point) const { return distance(point) < 0.; }
    void set_normal(const Vec3d& normal) { for (size_t i=0; i<3; ++i) m_data[i] = normal(i); }
    void set_offset(double offset) { m_data[3] = offset; }
    Vec3d get_normal() const { return Vec3d(m_data[0], m_data[1], m_data[2]); }
    bool is_active() const { return m_data[3] != DBL_MAX; }
    static ClippingPlane ClipsNothing() { return ClippingPlane(Vec3d(0., 0., 1.), DBL_MAX); }
    const double* get_data() const { return m_data; }

    // Serialization through cereal library
    template <class Archive>
    void serialize( Archive & ar )
    {
        ar( m_data[0], m_data[1], m_data[2], m_data[3] );
    }
};


// MeshClipper class cuts a mesh and is able to return a triangulated cut.
class MeshClipper {
public:
    // Inform MeshClipper about which plane we want to use to cut the mesh
    // This is supposed to be in world coordinates.
    void set_plane(const ClippingPlane& plane);

    // Which mesh to cut. MeshClipper remembers const * to it, caller
    // must make sure that it stays valid.
    void set_mesh(const TriangleMesh& mesh);

    void set_negative_mesh(const TriangleMesh &mesh);

    // Inform the MeshClipper about the transformation that transforms the mesh
    // into world coordinates.
    void set_transformation(const Geometry::Transformation& trafo);

    // Render the triangulated cut. Transformation matrices should
    // be set in world coords.
    void render_cut();

private:
    void recalculate_triangles();

    Geometry::Transformation m_trafo;
    const TriangleMesh* m_mesh = nullptr;
    const TriangleMesh* m_negative_mesh = nullptr;
    ClippingPlane m_plane;
    std::vector<Vec2f> m_triangles2d;
    GLIndexedVertexArray m_vertex_array;
    bool m_triangles_valid = false;
};



// MeshRaycaster class answers queries such as where on the mesh someone clicked,
// whether certain points are visible or obscured by the mesh etc.
class MeshRaycaster {
public:
    // The class references extern TriangleMesh, which must stay alive
    // during MeshRaycaster existence.
    MeshRaycaster(const TriangleMesh& mesh)
        : m_emesh(mesh)
    {
        m_normals.reserve(mesh.stl.facet_start.size());
        for (const stl_facet& facet : mesh.stl.facet_start)
            m_normals.push_back(facet.normal);
    }

    void line_from_mouse_pos(const Vec2d& mouse_pos, const Transform3d& trafo, const Camera& camera,
                             Vec3d& point, Vec3d& direction) const;

    // Given a mouse position, this returns true in case it is on the mesh.
    bool unproject_on_mesh(
        const Vec2d& mouse_pos,
        const Transform3d& trafo, // how to get the mesh into world coords
        const Camera& camera, // current camera position
        Vec3f& position, // where to save the positibon of the hit (mesh coords)
        Vec3f& normal, // normal of the triangle that was hit
        const ClippingPlane* clipping_plane = nullptr, // clipping plane (if active)
        size_t* facet_idx = nullptr // index of the facet hit
    ) const;

    // Given a vector of points in woorld coordinates, this returns vector
    // of indices of points that are visible (i.e. not cut by clipping plane
    // or obscured by part of the mesh.
    std::vector<unsigned> get_unobscured_idxs(
        const Geometry::Transformation& trafo,  // how to get the mesh into world coords
        const Camera& camera,                   // current camera position
        const std::vector<Vec3f>& points,       // points in world coords
        const ClippingPlane* clipping_plane = nullptr // clipping plane (if active)
    ) const;

    // Given a point in world coords, the method returns closest point on the mesh.
    // The output is in mesh coords.
    // normal* can be used to also get normal of the respective triangle.

    Vec3f get_closest_point(const Vec3f& point, Vec3f* normal = nullptr) const;

    Vec3f get_triangle_normal(size_t facet_idx) const;

private:
    sla::IndexedMesh m_emesh;
    std::vector<stl_normal> m_normals;
};

    
} // namespace GUI
} // namespace Slic3r


#endif // slic3r_MeshUtils_hpp_
