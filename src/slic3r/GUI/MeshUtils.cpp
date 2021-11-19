#include "MeshUtils.hpp"

#include "libslic3r/Tesselate.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleMeshSlicer.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Model.hpp"

#include "slic3r/GUI/Camera.hpp"

#include <GL/glew.h>

#include <igl/unproject.h>


namespace Slic3r {
namespace GUI {

void MeshClipper::set_plane(const ClippingPlane& plane)
{
    if (m_plane != plane) {
        m_plane = plane;
        m_triangles_valid = false;
    }
}


void MeshClipper::set_limiting_plane(const ClippingPlane& plane)
{
    if (m_limiting_plane != plane) {
        m_limiting_plane = plane;
        m_triangles_valid = false;
    }
}



void MeshClipper::set_mesh(const TriangleMesh& mesh)
{
    if (m_mesh != &mesh) {
        m_mesh = &mesh;
        m_triangles_valid = false;
        m_triangles2d.resize(0);
    }
}

void MeshClipper::set_negative_mesh(const TriangleMesh& mesh)
{
    if (m_negative_mesh != &mesh) {
        m_negative_mesh = &mesh;
        m_triangles_valid = false;
        m_triangles2d.resize(0);
    }
}



void MeshClipper::set_transformation(const Geometry::Transformation& trafo)
{
    if (! m_trafo.get_matrix().isApprox(trafo.get_matrix())) {
        m_trafo = trafo;
        m_triangles_valid = false;
        m_triangles2d.resize(0);
    }
}



void MeshClipper::render_cut()
{
    if (! m_triangles_valid)
        recalculate_triangles();

    if (m_vertex_array.has_VBOs())
        m_vertex_array.render();
}



void MeshClipper::recalculate_triangles()
{
    const Transform3f& instance_matrix_no_translation_no_scaling = m_trafo.get_matrix(true,false,true).cast<float>();
    // Calculate clipping plane normal in mesh coordinates.
    const Vec3f up_noscale = instance_matrix_no_translation_no_scaling.inverse() * m_plane.get_normal().cast<float>();
    const Vec3d up = up_noscale.cast<double>().cwiseProduct(m_trafo.get_scaling_factor());
    // Calculate distance from mesh origin to the clipping plane (in mesh coordinates).
    const float height_mesh = m_plane.distance(m_trafo.get_offset()) * (up_noscale.norm()/up.norm());

    // Now do the cutting
    MeshSlicingParams slicing_params;
    slicing_params.trafo.rotate(Eigen::Quaternion<double, Eigen::DontAlign>::FromTwoVectors(up, Vec3d::UnitZ()));

    ExPolygons expolys = union_ex(slice_mesh(m_mesh->its, height_mesh, slicing_params));

    if (m_negative_mesh && !m_negative_mesh->empty()) {
        const ExPolygons neg_expolys = union_ex(slice_mesh(m_negative_mesh->its, height_mesh, slicing_params));
        expolys = diff_ex(expolys, neg_expolys);
    }

    // Triangulate and rotate the cut into world coords:
    Eigen::Quaterniond q;
    q.setFromTwoVectors(Vec3d::UnitZ(), up);
    Transform3d tr = Transform3d::Identity();
    tr.rotate(q);
    tr = m_trafo.get_matrix() * tr;

    if (m_limiting_plane != ClippingPlane::ClipsNothing())
    {
        // Now remove whatever ended up below the limiting plane (e.g. sinking objects).
        // First transform the limiting plane from world to mesh coords.
        // Note that inverse of tr transforms the plane from world to horizontal.
        const Vec3d normal_old = m_limiting_plane.get_normal().normalized();
        const Vec3d normal_new = (tr.matrix().block<3,3>(0,0).transpose() * normal_old).normalized();

        // normal_new should now be the plane normal in mesh coords. To find the offset,
        // transform a point and set offset so it belongs to the transformed plane.
        Vec3d pt = Vec3d::Zero();
        const double plane_offset = m_limiting_plane.get_data()[3];
        if (std::abs(normal_old.z()) > 0.5) // normal is normalized, at least one of the coords if larger than sqrt(3)/3 = 0.57
            pt.z() = - plane_offset / normal_old.z();
        else if (std::abs(normal_old.y()) > 0.5)
            pt.y() = - plane_offset / normal_old.y();
        else
            pt.x() = - plane_offset / normal_old.x();
        pt = tr.inverse() * pt;
        const double offset = -(normal_new.dot(pt));

        if (std::abs(normal_old.dot(m_plane.get_normal().normalized())) > 0.99) {
            // The cuts are parallel, show all or nothing.
            if (normal_old.dot(m_plane.get_normal().normalized()) < 0.0 && offset < height_mesh)
                expolys.clear();
        } else {
            // The cut is a horizontal plane defined by z=height_mesh.
            // ax+by+e=0 is the line of intersection with the limiting plane.
            // Normalized so a^2 + b^2 = 1.
            const double len = std::hypot(normal_new.x(), normal_new.y());
            if (len == 0.)
                return;
            const double a = normal_new.x() / len;
            const double b = normal_new.y() / len;
            const double e = (normal_new.z() * height_mesh + offset) / len;

            // We need a half-plane to limit the cut. Get angle of the intersecting line.
            double angle = (b != 0.0) ? std::atan(-a / b) : ((a < 0.0) ? -0.5 * M_PI : 0.5 * M_PI);
            if (b > 0) // select correct half-plane
                angle += M_PI;

            // We'll take a big rectangle above x-axis and rotate and translate
            // it so it lies on our line. This will be the figure to subtract
            // from the cut. The coordinates must not overflow after the transform,
            // make the rectangle a bit smaller.
            const coord_t size = (std::numeric_limits<coord_t>::max() - scale_(std::max(std::abs(e*a), std::abs(e*b)))) / 4;
            Polygons ep {Polygon({Point(-size, 0), Point(size, 0), Point(size, 2*size), Point(-size, 2*size)})};
            ep.front().rotate(angle);
            ep.front().translate(scale_(-e * a), scale_(-e * b));
            expolys = diff_ex(expolys, ep);
        }
    }

    m_triangles2d = triangulate_expolygons_2f(expolys, m_trafo.get_matrix().matrix().determinant() < 0.);

    tr.pretranslate(0.001 * m_plane.get_normal().normalized()); // to avoid z-fighting

    m_vertex_array.release_geometry();
    for (auto it=m_triangles2d.cbegin(); it != m_triangles2d.cend(); it=it+3) {
        m_vertex_array.push_geometry(tr * Vec3d((*(it+0))(0), (*(it+0))(1), height_mesh), up);
        m_vertex_array.push_geometry(tr * Vec3d((*(it+1))(0), (*(it+1))(1), height_mesh), up);
        m_vertex_array.push_geometry(tr * Vec3d((*(it+2))(0), (*(it+2))(1), height_mesh), up);
        const size_t idx = it - m_triangles2d.cbegin();
        m_vertex_array.push_triangle(idx, idx+1, idx+2);
    }
    m_vertex_array.finalize_geometry(true);

    m_triangles_valid = true;
}


Vec3f MeshRaycaster::get_triangle_normal(size_t facet_idx) const
{
    return m_normals[facet_idx];
}

void MeshRaycaster::line_from_mouse_pos(const Vec2d& mouse_pos, const Transform3d& trafo, const Camera& camera,
                                        Vec3d& point, Vec3d& direction) const
{
    Matrix4d modelview = camera.get_view_matrix().matrix();
    Matrix4d projection= camera.get_projection_matrix().matrix();
    Vec4i viewport(camera.get_viewport().data());

    Vec3d pt1;
    Vec3d pt2;
    igl::unproject(Vec3d(mouse_pos(0), viewport[3] - mouse_pos(1), 0.),
                   modelview, projection, viewport, pt1);
    igl::unproject(Vec3d(mouse_pos(0), viewport[3] - mouse_pos(1), 1.),
                   modelview, projection, viewport, pt2);

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

    std::vector<sla::IndexedMesh::hit_result> hits = m_emesh.query_ray_hits(point, direction);

    if (hits.empty())
        return false; // no intersection found

    unsigned i = 0;

    // Remove points that are obscured or cut by the clipping plane.
    // Also, remove anything below the bed (sinking objects).
    for (i=0; i<hits.size(); ++i) {
        Vec3d transformed_hit = trafo * hits[i].position();
        if (transformed_hit.z() >= SINKING_Z_THRESHOLD &&
            (! clipping_plane || ! clipping_plane->is_point_clipped(transformed_hit)))
            break;
    }

    if (i==hits.size() || (hits.size()-i) % 2 != 0) {
        // All hits are either clipped, or there is an odd number of unclipped
        // hits - meaning the nearest must be from inside the mesh.
        return false;
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
    Vec3d direction_to_camera = -camera.get_dir_forward();
    Vec3d direction_to_camera_mesh = (instance_matrix_no_translation_no_scaling.inverse() * direction_to_camera).normalized().eval();
    direction_to_camera_mesh = direction_to_camera_mesh.cwiseProduct(trafo.get_scaling_factor());
    const Transform3d inverse_trafo = trafo.get_matrix().inverse();

    for (size_t i=0; i<points.size(); ++i) {
        const Vec3f& pt = points[i];
        if (clipping_plane && clipping_plane->is_point_clipped(pt.cast<double>()))
            continue;

        bool is_obscured = false;
        // Cast a ray in the direction of the camera and look for intersection with the mesh:
        std::vector<sla::IndexedMesh::hit_result> hits;
        // Offset the start of the ray by EPSILON to account for numerical inaccuracies.
        hits = m_emesh.query_ray_hits((inverse_trafo * pt.cast<double>() + direction_to_camera_mesh * EPSILON),
                                      direction_to_camera_mesh);

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
    if (normal)
        *normal = m_normals[idx];

    return closest_point.cast<float>();
}



} // namespace GUI
} // namespace Slic3r
