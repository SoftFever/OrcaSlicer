#include <limits>

#include <libslic3r/SLA/Rotfinder.hpp>

#include <libslic3r/Execution/ExecutionTBB.hpp>
#include <libslic3r/Execution/ExecutionSeq.hpp>

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
std::array<Vec3f, 3> get_triangle_vertices(const TriangleMesh &mesh,
                                           size_t              faceidx)
{
    const auto &face = mesh.its.indices[faceidx];
    return {mesh.its.vertices[face(0)],
            mesh.its.vertices[face(1)],
            mesh.its.vertices[face(2)]};
}

std::array<Vec3f, 3> get_transformed_triangle(const TriangleMesh &mesh,
                                              const Transform3f & tr,
                                              size_t              faceidx)
{
    const auto &tri = get_triangle_vertices(mesh, faceidx);
    return {tr * tri[0], tr * tri[1], tr * tri[2]};
}

template<class T> Vec<3, T> normal(const std::array<Vec<3, T>, 3> &tri)
{
    Vec<3, T> U = tri[1] - tri[0];
    Vec<3, T> V = tri[2] - tri[0];
    return U.cross(V).normalized();
}

template<class T, class AccessFn>
T sum_score(AccessFn &&accessfn, size_t facecount, size_t Nthreads)
{
    T  initv         = 0.;
    auto   mergefn   = [](T a, T b) { return a + b; };
    size_t grainsize = facecount / Nthreads;
    size_t from = 0, to = facecount;

    return execution::reduce(ex_seq, from, to, initv, mergefn, accessfn, grainsize);
}

// Try to guess the number of support points needed to support a mesh
double get_model_supportedness(const TriangleMesh &mesh, const Transform3f &tr)
{
    if (mesh.its.vertices.empty()) return std::nan("");

    auto accessfn = [&mesh, &tr](size_t fi) {
        Vec3f n = normal(get_transformed_triangle(mesh, tr, fi));

        // We should score against the alignment with the reference planes
        return scaled<int_fast64_t>(std::abs(n.dot(Vec3f::UnitX())) +
               std::abs(n.dot(Vec3f::UnitY())) +
               std::abs(n.dot(Vec3f::UnitZ())));
    };

    size_t facecount = mesh.its.indices.size();
    size_t Nthreads  = std::thread::hardware_concurrency();
    double S = unscaled(sum_score<int_fast64_t>(accessfn, facecount, Nthreads));

    return S / facecount;
}

using XYRotation = std::array<double, 2>;

// prepare the rotation transformation
Transform3f to_transform3f(const XYRotation &rot)
{
    Transform3f rt = Transform3f::Identity();
    rt.rotate(Eigen::AngleAxisf(float(rot[1]), Vec3f::UnitY()));
    rt.rotate(Eigen::AngleAxisf(float(rot[0]), Vec3f::UnitX()));

    return rt;
}

} // namespace

Vec2d find_best_misalignment_rotation(const SLAPrintObject &   po,
                                      float                    accuracy,
                                      std::function<bool(int)> statuscb)
{
    static const unsigned MAX_TRIES = 1000;

    // return value
    XYRotation rot;

    // We will use only one instance of this converted mesh to examine different
    // rotations
    TriangleMesh mesh = po.model_object()->raw_mesh();
    mesh.require_shared_vertices();

    // To keep track of the number of iterations
    int status = 0;

    // The maximum number of iterations
    auto max_tries = unsigned(accuracy * MAX_TRIES);

    // call status callback with zero, because we are at the start
    statuscb(status);

    auto statusfn = [&statuscb, &status, &max_tries] {
        // report status
        statuscb(++status * 100.0/max_tries);
    };

    auto stopcond = [&statuscb] {
        return ! statuscb(-1);
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

    Benchmark bench;

    bench.start();
    auto result = solver.to_max().optimize(
        [&mesh, &statusfn] (const XYRotation &rot)
        {
            statusfn();
            return get_model_supportedness(mesh, to_transform3f(rot));
        }, opt::initvals({0., 0.}), bounds);
    bench.stop();

    rot = result.optimum;

    std::cout << "Optimum score: " << result.score << std::endl;
    std::cout << "Optimum rotation: " << result.optimum[0] << " " << result.optimum[1] << std::endl;
    std::cout << "Optimization took: " << bench.getElapsedSec() << " seconds" << std::endl;

    return {rot[0], rot[1]};
}

}} // namespace Slic3r::sla
