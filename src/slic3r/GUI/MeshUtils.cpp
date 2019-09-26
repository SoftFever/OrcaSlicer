#include "MeshUtils.hpp"

#include "libslic3r/Tesselate.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "slic3r/GUI/Camera.hpp"

#include <GL/glew.h>


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
    m_tms->slice(std::vector<float>{height_mesh}, SlicingMode::Regular, 0.f, &list_of_expolys, [](){});
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


Vec3f MeshRaycaster::get_triangle_normal(const indexed_triangle_set& its, size_t facet_idx)
{
    Vec3f a(its.vertices[its.indices[facet_idx](1)] - its.vertices[its.indices[facet_idx](0)]);
    Vec3f b(its.vertices[its.indices[facet_idx](2)] - its.vertices[its.indices[facet_idx](0)]);
    return Vec3f(a.cross(b)).normalized();
}

bool MeshRaycaster::unproject_on_mesh(const Vec2d& mouse_pos, const Transform3d& trafo, const Camera& camera,
                                      Vec3f& position, Vec3f& normal, const ClippingPlane* clipping_plane,
                                      size_t* facet_idx) const
{
    const std::array<int, 4>& viewport = camera.get_viewport();
    const Transform3d& model_mat = camera.get_view_matrix();
    const Transform3d& proj_mat = camera.get_projection_matrix();

    Vec3d pt1;
    Vec3d pt2;
    ::gluUnProject(mouse_pos(0), viewport[3] - mouse_pos(1), 0., model_mat.data(), proj_mat.data(), viewport.data(), &pt1(0), &pt1(1), &pt1(2));
    ::gluUnProject(mouse_pos(0), viewport[3] - mouse_pos(1), 1., model_mat.data(), proj_mat.data(), viewport.data(), &pt2(0), &pt2(1), &pt2(2));

    Transform3d inv = trafo.inverse();
    pt1 = inv * pt1;
    pt2 = inv * pt2;

    point = pt1;
    direction = pt2-pt1;
}


bool MeshRaycaster::unproject_on_mesh(const Vec2d& mouse_pos, const Transform3d& trafo, const Camera& camera,
                                      Vec3f& position, Vec3f& normal, const ClippingPlane* clipping_plane,
                                      size_t* facet_idx) const
{
    Vec3d point;
    Vec3d direction;
    line_from_mouse_pos(mouse_pos, trafo, camera, point, direction);

    std::vector<sla::EigenMesh3D::hit_result> hits = m_emesh.query_ray_hits(point, direction);

    if (hits.empty())
        return false; // no intersection found

    unsigned i = 0;

    // Remove points that are obscured or cut by the clipping plane
    if (clipping_plane) {
        for (i=0; i<hits.size(); ++i)
            if (! clipping_plane->is_point_clipped(trafo * hits[i].position()))
                break;

        if (i==hits.size() || (hits.size()-i) % 2 != 0) {
            // All hits are either clipped, or there is an odd number of unclipped
            // hits - meaning the nearest must be from inside the mesh.
            return false;
        }
    }

    // Now stuff the points in the provided vector and calculate normals if asked about them:
    position = hits[i].position().cast<float>();
    normal = hits[i].normal().cast<float>();

    if (facet_idx)
        *facet_idx = hits[i].face();

    return true;
}


std::vector<unsigned> MeshRaycaster::get_unobscured_idxs(const Geometry::Transformation& trafo, const Camera& camera, const std::vector<Vec3f>& points,
                                                       const ClippingPlane* clipping_plane) const
{
    std::vector<unsigned> out;

    const Transform3d& instance_matrix_no_translation_no_scaling = trafo.get_matrix(true,false,true);
    Vec3f direction_to_camera = -camera.get_dir_forward().cast<float>();
    Vec3f direction_to_camera_mesh = (instance_matrix_no_translation_no_scaling.inverse().cast<float>() * direction_to_camera).normalized().eval();
    Vec3f scaling = trafo.get_scaling_factor().cast<float>();
    direction_to_camera_mesh = Vec3f(direction_to_camera_mesh(0)*scaling(0), direction_to_camera_mesh(1)*scaling(1), direction_to_camera_mesh(2)*scaling(2));
    const Transform3f inverse_trafo = trafo.get_matrix().inverse().cast<float>();

    for (size_t i=0; i<points.size(); ++i) {
        const Vec3f& pt = points[i];
        if (clipping_plane && clipping_plane->is_point_clipped(pt.cast<double>()))
            continue;

        bool is_obscured = false;
        // Cast a ray in the direction of the camera and look for intersection with the mesh:
        std::vector<sla::EigenMesh3D::hit_result> hits;
        // Offset the start of the ray by EPSILON to account for numerical inaccuracies.
        hits = m_emesh.query_ray_hits((inverse_trafo * pt + direction_to_camera_mesh * EPSILON).cast<double>(),
                                      direction_to_camera.cast<double>());


        if (! hits.empty()) {
            // If the closest hit facet normal points in the same direction as the ray,
            // we are looking through the mesh and should therefore discard the point:
            if (hits.front().normal().dot(direction_to_camera_mesh.cast<double>()) > 0)
                is_obscured = true;

            // Eradicate all hits that the caller wants to ignore
            for (unsigned j=0; j<hits.size(); ++j) {
                if (clipping_plane && clipping_plane->is_point_clipped(trafo.get_matrix() * hits[j].position())) {
                    hits.erase(hits.begin()+j);
                    --j;
                }
            }

            // FIXME: the intersection could in theory be behind the camera, but as of now we only have camera direction.
            // Also, the threshold is in mesh coordinates, not in actual dimensions.
            if (! hits.empty())
                is_obscured = true;
        }
        if (! is_obscured)
            out.push_back(i);
    }
    return out;
}


Vec3f MeshRaycaster::get_closest_point(const Vec3f& point, Vec3f* normal) const
{
    int idx = 0;
    Vec3d closest_point;
    m_emesh.squared_distance(point.cast<double>(), idx, closest_point);
    if (normal) {
        auto indices = m_emesh.F().row(idx);
        Vec3d a(m_emesh.V().row(indices(1)) - m_emesh.V().row(indices(0)));
        Vec3d b(m_emesh.V().row(indices(2)) - m_emesh.V().row(indices(0)));
        *normal = Vec3f(a.cross(b).cast<float>());
    }
    return closest_point.cast<float>();
}



} // namespace GUI
} // namespace Slic3r
