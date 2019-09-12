#ifndef slic3r_MeshClipper_hpp_
#define slic3r_MeshClipper_hpp_

#include "libslic3r/Point.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"

namespace Slic3r {
namespace GUI {

class MeshClipper {
public:
    void set_plane(const ClippingPlane& plane);
    void set_mesh(const TriangleMesh& mesh);
    void set_transformation(const Geometry::Transformation& trafo);

    const std::vector<Vec2f>& get_triangles();

private:
    void recalculate_triangles();
    std::pair<Vec3f, float> get_mesh_cut_normal() const;


    Geometry::Transformation m_trafo;
    const TriangleMesh* m_mesh = nullptr;
    ClippingPlane m_plane;
    std::vector<Vec2f> m_triangles;
    bool m_triangles_valid = false;
    std::unique_ptr<TriangleMeshSlicer> m_tms;
};


    
} // namespace GUI
} // namespace Slic3r


#endif // slic3r_MeshClipper_hpp_
