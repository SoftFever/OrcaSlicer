#include <functional>

#include <libslic3r/OpenVDBUtils.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/SLA/Hollowing.hpp>
#include <libslic3r/SLA/Contour3D.hpp>
#include <libslic3r/SLA/EigenMesh3D.hpp>
#include <libslic3r/SLA/SupportTreeBuilder.hpp>
#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/SimplifyMesh.hpp>

#include <boost/log/trivial.hpp>

#include <libslic3r/MTUtils.hpp>
#include <libslic3r/I18N.hpp>

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

namespace Slic3r {
namespace sla {

template<class S, class = FloatingOnly<S>>
inline void _scale(S s, TriangleMesh &m) { m.scale(float(s)); }

template<class S, class = FloatingOnly<S>>
inline void _scale(S s, Contour3D &m) { for (auto &p : m.points) p *= s; }

static TriangleMesh _generate_interior(const TriangleMesh  &mesh,
                                       const JobController &ctl,
                                       double               min_thickness,
                                       double               voxel_scale,
                                       double               closing_dist)
{
    TriangleMesh imesh{mesh};
    
    _scale(voxel_scale, imesh);
    
    double offset = voxel_scale * min_thickness;
    double D = voxel_scale * closing_dist;
    float  out_range = 0.1f * float(offset);
    float  in_range = 1.1f * float(offset + D);
    
    if (ctl.stopcondition()) return {};
    else ctl.statuscb(0, L("Hollowing"));
    
    auto gridptr = mesh_to_grid(imesh, {}, out_range, in_range);
    
    assert(gridptr);
    
    if (!gridptr) {
        BOOST_LOG_TRIVIAL(error) << "Returned OpenVDB grid is NULL";
        return {};
    }
    
    if (ctl.stopcondition()) return {};
    else ctl.statuscb(30, L("Hollowing"));
    
    if (closing_dist > .0) {
        gridptr = redistance_grid(*gridptr, -(offset + D), double(in_range));
    } else {
        D = -offset;
    }
    
    if (ctl.stopcondition()) return {};
    else ctl.statuscb(70, L("Hollowing"));
    
    double iso_surface = D;
    double adaptivity = 0.;
    auto omesh = grid_to_mesh(*gridptr, iso_surface, adaptivity);
    
    _scale(1. / voxel_scale, omesh);
    
    if (ctl.stopcondition()) return {};
    else ctl.statuscb(100, L("Hollowing"));
    
    return omesh;
}

std::unique_ptr<TriangleMesh> generate_interior(const TriangleMesh &   mesh,
                                                const HollowingConfig &hc,
                                                const JobController &  ctl)
{
    static const double MIN_OVERSAMPL = 3.;
    static const double MAX_OVERSAMPL = 8.;
        
    // I can't figure out how to increase the grid resolution through openvdb
    // API so the model will be scaled up before conversion and the result
    // scaled down. Voxels have a unit size. If I set voxelSize smaller, it
    // scales the whole geometry down, and doesn't increase the number of
    // voxels.
    //
    // max 8x upscale, min is native voxel size
    auto voxel_scale = MIN_OVERSAMPL + (MAX_OVERSAMPL - MIN_OVERSAMPL) * hc.quality;
    auto meshptr = std::make_unique<TriangleMesh>(
        _generate_interior(mesh, ctl, hc.min_thickness, voxel_scale,
                           hc.closing_distance));
    
    if (meshptr && !meshptr->empty()) {
        
        // This flips the normals to be outward facing...
        meshptr->require_shared_vertices();
        indexed_triangle_set its = std::move(meshptr->its);
        
        Slic3r::simplify_mesh(its);
        
        // flip normals back...
        for (stl_triangle_vertex_indices &ind : its.indices)
            std::swap(ind(0), ind(2));
        
        *meshptr = Slic3r::TriangleMesh{its};
    }
    
    return meshptr;
}

Contour3D DrainHole::to_mesh() const
{
    auto r = double(radius);
    auto h = double(height);
    sla::Contour3D hole = sla::cylinder(r, h, steps);
    Eigen::Quaterniond q;
    q.setFromTwoVectors(Vec3d{0., 0., 1.}, normal.cast<double>());
    for(auto& p : hole.points) p = q * p + pos.cast<double>();
    
    return hole;
}

bool DrainHole::operator==(const DrainHole &sp) const
{
    return (pos == sp.pos) && (normal == sp.normal) &&
            is_approx(radius, sp.radius) &&
            is_approx(height, sp.height);
}

bool DrainHole::is_inside(const Vec3f& pt) const
{
    Eigen::Hyperplane<float, 3> plane(normal, pos);
    float dist = plane.signedDistance(pt);
    if (dist < float(EPSILON) || dist > height)
        return false;

    Eigen::ParametrizedLine<float, 3> axis(pos, normal);
    if ( axis.squaredDistance(pt) < pow(radius, 2.f))
        return true;

    return false;
}


// Given a line s+dir*t, find parameter t of intersections with the hole
// and the normal (points inside the hole). Outputs through out reference,
// returns true if two intersections were found.
bool DrainHole::get_intersections(const Vec3f& s, const Vec3f& dir,
                                  std::array<std::pair<float, Vec3d>, 2>& out)
                                  const
{
    assert(is_approx(normal.norm(), 1.f));
    const Eigen::ParametrizedLine<float, 3> ray(s, dir.normalized());

    for (size_t i=0; i<2; ++i)
        out[i] = std::make_pair(sla::EigenMesh3D::hit_result::infty(), Vec3d::Zero());

    const float sqr_radius = pow(radius, 2.f);

    // first check a bounding sphere of the hole:
    Vec3f center = pos+normal*height/2.f;
    float sqr_dist_limit = pow(height/2.f, 2.f) + sqr_radius ;
    if (ray.squaredDistance(center) > sqr_dist_limit)
        return false;

    // The line intersects the bounding sphere, look for intersections with
    // bases of the cylinder.

    size_t found = 0; // counts how many intersections were found
    Eigen::Hyperplane<float, 3> base;
    if (! is_approx(ray.direction().dot(normal), 0.f)) {
        for (size_t i=1; i<=1; --i) {
            Vec3f cylinder_center = pos+i*height*normal;
            if (i == 0) {
                // The hole base can be identical to mesh surface if it is flat
                // let's better move the base outward a bit
                cylinder_center -= EPSILON*normal;
            }
            base = Eigen::Hyperplane<float, 3>(normal, cylinder_center);
            Vec3f intersection = ray.intersectionPoint(base);
            // Only accept the point if it is inside the cylinder base.
            if ((cylinder_center-intersection).squaredNorm() < sqr_radius) {
                out[found].first = ray.intersectionParameter(base);
                out[found].second = (i==0 ? 1. : -1.) * normal.cast<double>();
                ++found;
            }
        }
    }
    else
    {
        // In case the line was perpendicular to the cylinder axis, previous
        // block was skipped, but base will later be assumed to be valid.
        base = Eigen::Hyperplane<float, 3>(normal, pos-EPSILON*normal);
    }

    // In case there is still an intersection to be found, check the wall
    if (found != 2 && ! is_approx(std::abs(ray.direction().dot(normal)), 1.f)) {
        // Project the ray onto the base plane
        Vec3f proj_origin = base.projection(ray.origin());
        Vec3f proj_dir = base.projection(ray.origin()+ray.direction())-proj_origin;
        // save how the parameter scales and normalize the projected direction
        float par_scale = proj_dir.norm();
        proj_dir = proj_dir/par_scale;
        Eigen::ParametrizedLine<float, 3> projected_ray(proj_origin, proj_dir);
        // Calculate point on the secant that's closest to the center
        // and its distance to the circle along the projected line
        Vec3f closest = projected_ray.projection(pos);
        float dist = sqrt((sqr_radius - (closest-pos).squaredNorm()));
        // Unproject both intersections on the original line and check
        // they are on the cylinder and not past it:
        for (int i=-1; i<=1 && found !=2; i+=2) {
            Vec3f isect = closest + i*dist * projected_ray.direction();
            Vec3f to_isect = isect-proj_origin;
            float par = to_isect.norm() / par_scale;
            if (to_isect.normalized().dot(proj_dir.normalized()) < 0.f)
                par *= -1.f;
            Vec3d hit_normal = (pos-isect).normalized().cast<double>();
            isect = ray.pointAt(par);
            // check that the intersection is between the base planes:
            float vert_dist = base.signedDistance(isect);
            if (vert_dist > 0.f && vert_dist < height) {
                out[found].first = par;
                out[found].second = hit_normal;
                ++found;
            }
        }
    }

    // If only one intersection was found, it is some corner case,
    // no intersection will be returned:
    if (found != 2)
        return false;

    // Sort the intersections:
    if (out[0].first > out[1].first)
        std::swap(out[0], out[1]);

    return true;
}

void cut_drainholes(std::vector<ExPolygons> & obj_slices,
                    const std::vector<float> &slicegrid,
                    float                     closing_radius,
                    const sla::DrainHoles &   holes,
                    std::function<void(void)> thr)
{
    TriangleMesh mesh;
    for (const sla::DrainHole &holept : holes)
        mesh.merge(sla::to_triangle_mesh(holept.to_mesh()));
    
    if (mesh.empty()) return;
    
    mesh.require_shared_vertices();
    
    TriangleMeshSlicer slicer(&mesh);
    
    std::vector<ExPolygons> hole_slices;
    slicer.slice(slicegrid, SlicingMode::Regular, closing_radius, &hole_slices, thr);
    
    if (obj_slices.size() != hole_slices.size())
        BOOST_LOG_TRIVIAL(warning)
            << "Sliced object and drain-holes layer count does not match!";

    size_t until = std::min(obj_slices.size(), hole_slices.size());
    
    for (size_t i = 0; i < until; ++i)
        obj_slices[i] = diff_ex(obj_slices[i], hole_slices[i]);
}

}} // namespace Slic3r::sla
