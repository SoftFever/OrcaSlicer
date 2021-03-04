#include <limits>

#include <libslic3r/SLA/Rotfinder.hpp>
#include <libslic3r/SLA/Concurrency.hpp>

#include <libslic3r/Optimize/BruteforceOptimizer.hpp>

#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <libslic3r/Geometry.hpp>
#include "Model.hpp"

#include <thread>

#include <libnest2d/tools/benchmark.h>

namespace Slic3r { namespace sla {

namespace {

// Get the vertices of a triangle directly in an array of 3 points
std::array<Vec3d, 3> get_triangle_vertices(const TriangleMesh &mesh,
                                           size_t              faceidx)
{
    const auto &face = mesh.its.indices[faceidx];
    return {Vec3d{mesh.its.vertices[face(0)].cast<double>()},
            Vec3d{mesh.its.vertices[face(1)].cast<double>()},
            Vec3d{mesh.its.vertices[face(2)].cast<double>()}};
}

std::array<Vec3d, 3> get_transformed_triangle(const TriangleMesh &mesh,
                                              const Transform3d & tr,
                                              size_t              faceidx)
{
    const auto &tri = get_triangle_vertices(mesh, faceidx);
    return {tr * tri[0], tr * tri[1], tr * tri[2]};
}

// Get area and normal of a triangle
struct Facestats {
    Vec3d  normal;
    double area;

    explicit Facestats(const std::array<Vec3d, 3> &triangle)
    {
        Vec3d U = triangle[1] - triangle[0];
        Vec3d V = triangle[2] - triangle[0];
        Vec3d C = U.cross(V);
        normal = C.normalized();
        area = 0.5 * C.norm();
    }
};

template<class AccessFn>
double sum_score(AccessFn &&accessfn, size_t facecount, size_t Nthreads)
{
    double initv     = 0.;
    auto    mergefn   = [](double a, double b) { return a + b; };
    size_t  grainsize = facecount / Nthreads;
    size_t  from = 0, to = facecount;

    return ccr_par::reduce(from, to, initv, mergefn, accessfn, grainsize);
}

// Try to guess the number of support points needed to support a mesh
double get_model_supportedness(const TriangleMesh &mesh, const Transform3d &tr)
{
    if (mesh.its.vertices.empty()) return std::nan("");

    auto accessfn = [&mesh, &tr](size_t fi) {
        Facestats fc{get_transformed_triangle(mesh, tr, fi)};

        // We should score against the alignment with the reference planes
        return std::abs(fc.normal.dot(Vec3d::UnitX())) +
               std::abs(fc.normal.dot(Vec3d::UnitY())) +
               std::abs(fc.normal.dot(Vec3d::UnitZ()));
    };

    size_t facecount = mesh.its.indices.size();
    size_t Nthreads  = std::thread::hardware_concurrency();
    double S = sum_score(accessfn, facecount, Nthreads);

    return S / facecount;
}

using XYRotation = std::array<double, 2>;

// prepare the rotation transformation
Transform3d to_transform3d(const XYRotation &rot)
{
    Transform3d rt = Transform3d::Identity();
    rt.rotate(Eigen::AngleAxisd(rot[1], Vec3d::UnitY()));
    rt.rotate(Eigen::AngleAxisd(rot[0], Vec3d::UnitX()));
    return rt;
}

} // namespace

Vec2d find_best_rotation(const SLAPrintObject &        po,
                         float                         accuracy,
                         std::function<void(unsigned)> statuscb,
                         std::function<bool()>         stopcond)
{
    static const unsigned MAX_TRIES = 1000;

    // return value
    XYRotation rot;

    // We will use only one instance of this converted mesh to examine different
    // rotations
    TriangleMesh mesh = po.model_object()->raw_mesh();
    mesh.require_shared_vertices();

    // To keep track of the number of iterations
    unsigned status = 0;

    // The maximum number of iterations
    auto max_tries = unsigned(accuracy * MAX_TRIES);

    // call status callback with zero, because we are at the start
    statuscb(status);

    auto statusfn = [&statuscb, &status, &max_tries] {
        // report status
        statuscb(unsigned(++status * 100.0/max_tries) );
    };

    // Preparing the optimizer.
    size_t gridsize = std::sqrt(max_tries);
    opt::Optimizer<opt::AlgBruteForce> solver(opt::StopCriteria{}
                                                .max_iterations(max_tries)
                                                .stop_condition(stopcond),
                                              gridsize);

    // We are searching rotations around only two axes x, y. Thus the
    // problem becomes a 2 dimensional optimization task.
    // We can specify the bounds for a dimension in the following way:
    auto bounds = opt::bounds({ {-PI/2, PI/2}, {-PI/2, PI/2} });

    auto result = solver.to_max().optimize(
        [&mesh, &statusfn] (const XYRotation &rot)
        {
            statusfn();
            return get_model_supportedness(mesh, to_transform3d(rot));
        }, opt::initvals({0., 0.}), bounds);

    rot = result.optimum;

    return {rot[0], rot[1]};
}

double get_model_supportedness(const SLAPrintObject &po, const Transform3d &tr)
{
    TriangleMesh mesh = po.model_object()->raw_mesh();
    mesh.require_shared_vertices();

    return get_model_supportedness(mesh, tr);
}

}} // namespace Slic3r::sla
