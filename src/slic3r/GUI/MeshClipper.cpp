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
        m_triangles.resize(0);
        m_tms.reset(nullptr);
    }
}

void MeshClipper::set_transformation(const Geometry::Transformation& trafo)
{
    if (! m_trafo.get_matrix().isApprox(trafo.get_matrix())) {
        m_trafo = trafo;
        m_triangles_valid = false;
        m_triangles.resize(0);
    }
}


const std::vector<Vec2f>& MeshClipper::get_triangles()
{
    if (! m_triangles_valid)
        recalculate_triangles();

    return m_triangles;
}

void MeshClipper::recalculate_triangles()
{
    if (! m_tms) {
        m_tms.reset(new TriangleMeshSlicer);
        m_tms->init(m_mesh, [](){});
    }


    auto up_and_height = get_mesh_cut_normal();
    Vec3f up = up_and_height.first;
    float height_mesh = up_and_height.second;

    std::vector<ExPolygons> list_of_expolys;
    m_tms->set_up_direction(up);
    m_tms->slice(std::vector<float>{height_mesh}, 0.f, &list_of_expolys, [](){});
    m_triangles = triangulate_expolygons_2f(list_of_expolys[0]);

    m_triangles_valid = true;
}

std::pair<Vec3f, float> MeshClipper::get_mesh_cut_normal() const
{
    Transform3f instance_matrix_no_translation_no_scaling = m_trafo.get_matrix(true,false,true).cast<float>();
    Vec3f scaling = m_trafo.get_scaling_factor().cast<float>();

    // Calculate distance from mesh origin to the clipping plane (in mesh coordinates).
    Vec3f up_noscale = instance_matrix_no_translation_no_scaling.inverse() * m_plane.get_normal().cast<float>();
    Vec3f up (up_noscale(0)*scaling(0), up_noscale(1)*scaling(1), up_noscale(2)*scaling(2));

    float height_mesh = m_plane.distance(m_trafo.get_offset()) * (up_noscale.norm()/up.norm());


    return std::make_pair(up, height_mesh);
}




} // namespace GUI
} // namespace Slic3r
