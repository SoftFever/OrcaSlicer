#include "MeshClipper.hpp"
#include "GLCanvas3D.hpp"
#include "libslic3r/Tesselate.hpp"

namespace Slic3r {
namespace GUI {

void MeshClipper::set_plane(const ClippingPlane& plane)
{
    if (m_plane != plane) {
        m_plane = plane;
        m_triangles_valid = false;
    }
}

void MeshClipper::set_mesh(const TriangleMesh& mesh)
{
    if (m_mesh != &mesh) {
        m_mesh = &mesh;
        m_triangles_valid = false;
        m_triangles2d.resize(0);
        m_triangles3d.resize(0);
        m_tms.reset(nullptr);
    }
}

void MeshClipper::set_transformation(const Geometry::Transformation& trafo)
{
    if (! m_trafo.get_matrix().isApprox(trafo.get_matrix())) {
        m_trafo = trafo;
        m_triangles_valid = false;
        m_triangles2d.resize(0);
        m_triangles3d.resize(0);
    }
}



const std::vector<Vec3f>& MeshClipper::get_triangles()
{
    if (! m_triangles_valid)
        recalculate_triangles();

    return m_triangles3d;
}

void MeshClipper::recalculate_triangles()
{
    if (! m_tms) {
        m_tms.reset(new TriangleMeshSlicer);
        m_tms->init(m_mesh, [](){});
    }


    const Transform3f& instance_matrix_no_translation_no_scaling = m_trafo.get_matrix(true,false,true).cast<float>();
    const Vec3f& scaling = m_trafo.get_scaling_factor().cast<float>();
    // Calculate clipping plane normal in mesh coordinates.
    Vec3f up_noscale = instance_matrix_no_translation_no_scaling.inverse() * m_plane.get_normal().cast<float>();
    Vec3f up (up_noscale(0)*scaling(0), up_noscale(1)*scaling(1), up_noscale(2)*scaling(2));
    // Calculate distance from mesh origin to the clipping plane (in mesh coordinates).
    float height_mesh = m_plane.distance(m_trafo.get_offset()) * (up_noscale.norm()/up.norm());

    // Now do the cutting
    std::vector<ExPolygons> list_of_expolys;
    m_tms->set_up_direction(up);
    m_tms->slice(std::vector<float>{height_mesh}, 0.f, &list_of_expolys, [](){});
    m_triangles2d = triangulate_expolygons_2f(list_of_expolys[0], m_trafo.get_matrix().matrix().determinant() < 0.);

    // Rotate the cut into world coords:
    Eigen::Quaternionf q;
    q.setFromTwoVectors(Vec3f::UnitZ(), up);
    Transform3f tr = Transform3f::Identity();
    tr.rotate(q);
    tr = m_trafo.get_matrix().cast<float>() * tr;

    m_triangles3d.clear();
    m_triangles3d.reserve(m_triangles2d.size());
    for (const Vec2f& pt : m_triangles2d) {
        m_triangles3d.push_back(Vec3f(pt(0), pt(1), height_mesh+0.001f));
        m_triangles3d.back() = tr * m_triangles3d.back();
    }

    m_triangles_valid = true;
}



} // namespace GUI
} // namespace Slic3r
