#include <limits>
#include <exception>

//#include <libnest2d/optimizers/nlopt/genetic.hpp>
#include <libslic3r/Optimize/BruteforceOptimizer.hpp>
#include <libslic3r/SLA/Rotfinder.hpp>
#include <libslic3r/SLA/Concurrency.hpp>
#include <libslic3r/SLA/SupportTree.hpp>
#include <libslic3r/SLA/SupportPointGenerator.hpp>
#include <libslic3r/SimplifyMesh.hpp>
#include "Model.hpp"

#include <thread>

namespace Slic3r {
namespace sla {

using VertexFaceMap = std::vector<std::vector<size_t>>;

VertexFaceMap create_vertex_face_map(const TriangleMesh &mesh) {
    std::vector<std::vector<size_t>> vmap(mesh.its.vertices.size());

    size_t fi = 0;
    for (const Vec3i &tri : mesh.its.indices) {
        for (int vi = 0; vi < tri.size(); ++vi) {
            auto from = vmap[tri(vi)].begin(), to = vmap[tri(vi)].end();
            vmap[tri(vi)].insert(std::lower_bound(from, to, fi), fi);
        }
    }

    return vmap;
}

// Find transformed mesh ground level without copy and with parallel reduce.
double find_ground_level(const TriangleMesh &mesh,
                         const Transform3d & tr,
                         size_t              threads)
{
    size_t vsize = mesh.its.vertices.size();

    auto minfn = [](double a, double b) { return std::min(a, b); };

    auto accessfn = [&mesh, &tr] (size_t vi) {
        return (tr * mesh.its.vertices[vi].template cast<double>()).z();
    };

    double zmin = mesh.its.vertices.front().z();
    size_t granularity = vsize / threads;
    return ccr_par::reduce(size_t(0), vsize, zmin, minfn, accessfn, granularity);
}

// Try to guess the number of support points needed to support a mesh
double calculate_model_supportedness(const TriangleMesh & mesh,
//                                     const VertexFaceMap &vmap,
                                     const Transform3d &  tr)
{
    static constexpr double POINTS_PER_UNIT_AREA = 1.;

    if (mesh.its.vertices.empty()) return std::nan("");

    size_t Nthr = std::thread::hardware_concurrency();
    size_t facesize = mesh.its.indices.size();

    double zmin = find_ground_level(mesh, tr, Nthr);

    auto accessfn = [&mesh, &tr, zmin](size_t fi) {

        static const Vec3d DOWN = {0., 0., -1.};

        const auto &face = mesh.its.indices[fi];
        Vec3d p1 = tr * mesh.its.vertices[face(0)].template cast<double>();
        Vec3d p2 = tr * mesh.its.vertices[face(1)].template cast<double>();
        Vec3d p3 = tr * mesh.its.vertices[face(2)].template cast<double>();

        Vec3d U = p2 - p1;
        Vec3d V = p3 - p1;
        Vec3d C = U.cross(V);
        Vec3d N = C.normalized();
        double area = 0.5 * C.norm();

        double zlvl = zmin + 0.1;
        if (p1.z() <= zlvl && p2.z() <= zlvl && p3.z() <= zlvl) {
            //                score += area * POINTS_PER_UNIT_AREA;
            return 0.;
        }

        double phi = 1. - std::acos(N.dot(DOWN)) / PI;
//        phi = phi * (phi > 0.5);

        //                    std::cout << "area: " << area << std::endl;

        return area * POINTS_PER_UNIT_AREA * phi;
    };

    double score = ccr_par::reduce(size_t(0), facesize, 0., std::plus<double>{}, accessfn, facesize / Nthr);

    return score / mesh.its.indices.size();
}

std::array<double, 2> find_best_rotation(const ModelObject& modelobj,
                                         float accuracy,
                                         std::function<void(unsigned)> statuscb,
                                         std::function<bool()> stopcond)
{
    static const unsigned MAX_TRIES = 10000;

    // return value
    std::array<double, 2> rot;

    // We will use only one instance of this converted mesh to examine different
    // rotations
    TriangleMesh mesh = modelobj.raw_mesh();
    mesh.require_shared_vertices();
//    auto vmap = create_vertex_face_map(mesh);
//    simplify_mesh(mesh);

    // For current iteration number
    unsigned status = 0;

    // The maximum number of iterations
    auto max_tries = unsigned(accuracy * MAX_TRIES);

    // call status callback with zero, because we are at the start
    statuscb(status);

    // So this is the object function which is called by the solver many times
    // It has to yield a single value representing the current score. We will
    // call the status callback in each iteration but the actual value may be
    // the same for subsequent iterations (status goes from 0 to 100 but
    // iterations can be many more)
    auto objfunc = [&mesh, &status, &statuscb, &stopcond, max_tries]
        (const opt::Input<2> &in)
    {
        std::cout << "in: " << in[0] << " " << in[1] << std::endl;

        // prepare the rotation transformation
        Transform3d rt = Transform3d::Identity();
        rt.rotate(Eigen::AngleAxisd(in[1], Vec3d::UnitY()));
        rt.rotate(Eigen::AngleAxisd(in[0], Vec3d::UnitX()));

        double score = sla::calculate_model_supportedness(mesh, rt);
        std::cout << score << std::endl;

        // report status
        if(!stopcond()) statuscb( unsigned(++status * 100.0/max_tries) );

        return score;
    };

    // Firing up the genetic optimizer. For now it uses the nlopt library.

    opt::Optimizer<opt::AlgBruteForce> solver(opt::StopCriteria{}
                                                  .max_iterations(max_tries)
                                                  .rel_score_diff(1e-6)
                                                  .stop_condition(stopcond),
                                              100 /*grid size*/);

    // We are searching rotations around only two axes x, y. Thus the
    // problem becomes a 2 dimensional optimization task.
    // We can specify the bounds for a dimension in the following way:
    auto b = opt::Bound{-PI, PI};

    // Now we start the optimization process with initial angles (0, 0, 0)
    auto result = solver.to_min().optimize(objfunc, opt::initvals({0.0, 0.0}),
                                           opt::bounds({b, b}));

    // Save the result and fck off
    rot[0] = std::get<0>(result.optimum);
    rot[1] = std::get<1>(result.optimum);

    return rot;
}

}
}
