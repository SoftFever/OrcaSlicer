#include <limits>
#include <exception>

//#include <libnest2d/optimizers/nlopt/genetic.hpp>
#include <libslic3r/Optimizer.hpp>
#include <libslic3r/SLA/Rotfinder.hpp>
#include <libslic3r/SLA/SupportTree.hpp>
#include <libslic3r/SLA/SupportPointGenerator.hpp>
#include <libslic3r/SimplifyMesh.hpp>
#include "Model.hpp"

namespace Slic3r {
namespace sla {

double area(const Vec3d &p1, const Vec3d &p2, const Vec3d &p3) {
    Vec3d a = p2 - p1;
    Vec3d b = p3 - p1;
    Vec3d c = a.cross(b);
    return 0.5 * c.norm();
}

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

// Try to guess the number of support points needed to support a mesh
double calculate_model_supportedness(const TriangleMesh & mesh,
                                     const VertexFaceMap &vmap,
                                     const Transform3d &  tr)
{
    static const double POINTS_PER_UNIT_AREA = 1.;
    static const Vec3d DOWN = {0., 0., -1.};

    double score = 0.;

//    double zmin = mesh.bounding_box().min.z();

//    std::vector<Vec3d> normals(mesh.its.indices.size(), Vec3d::Zero());

    double zmin = 0;
    for (auto & v : mesh.its.vertices)
        zmin = std::min(zmin, double((tr * v.cast<double>()).z()));

    for (size_t fi = 0; fi < mesh.its.indices.size(); ++fi) {
        const auto &face = mesh.its.indices[fi];
        Vec3d p1 = tr * mesh.its.vertices[face(0)].cast<double>();
        Vec3d p2 = tr * mesh.its.vertices[face(1)].cast<double>();
        Vec3d p3 = tr * mesh.its.vertices[face(2)].cast<double>();

//        auto triang = std::array<Vec3d, 3> {p1, p2, p3};
//        double a = area(triang.begin(), triang.end());
        double a = area(p1, p2, p3);

        double zlvl = zmin + 0.1;
        if (p1.z() <= zlvl && p2.z() <= zlvl && p3.z() <= zlvl) {
            score += a * POINTS_PER_UNIT_AREA;
            continue;
        }


        Eigen::Vector3d U = p2 - p1;
        Eigen::Vector3d V = p3 - p1;
        Vec3d           N = U.cross(V).normalized();

        double phi = std::acos(N.dot(DOWN)) / PI;

        std::cout << "area: " << a << std::endl;

        score += a * POINTS_PER_UNIT_AREA * phi;
//        normals[fi] = N;
    }

//    for (size_t vi = 0; vi < mesh.its.vertices.size(); ++vi) {
//        const std::vector<size_t> &neighbors = vmap[vi];

//        const auto &v = mesh.its.vertices[vi];
//        Vec3d vt =  tr * v.cast<double>();
//    }

    return score;
}

std::array<double, 2> find_best_rotation(const ModelObject& modelobj,
                                         float accuracy,
                                         std::function<void(unsigned)> statuscb,
                                         std::function<bool()> stopcond)
{
    static const unsigned MAX_TRIES = 1000000;

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
        // prepare the rotation transformation
        Transform3d rt = Transform3d::Identity();
        rt.rotate(Eigen::AngleAxisd(in[1], Vec3d::UnitY()));
        rt.rotate(Eigen::AngleAxisd(in[0], Vec3d::UnitX()));

        double score = sla::calculate_model_supportedness(mesh, {}, rt);
        std::cout << score << std::endl;

        // report status
        if(!stopcond()) statuscb( unsigned(++status * 100.0/max_tries) );

        return score;
    };

    // Firing up the genetic optimizer. For now it uses the nlopt library.

    opt::Optimizer<opt::AlgNLoptDIRECT> solver(opt::StopCriteria{}
                                           .max_iterations(max_tries)
                                           .rel_score_diff(1e-3)
                                           .stop_condition(stopcond));

    // We are searching rotations around the three axes x, y, z. Thus the
    // problem becomes a 3 dimensional optimization task.
    // We can specify the bounds for a dimension in the following way:
    auto b = opt::Bound{-PI, PI};

    // Now we start the optimization process with initial angles (0, 0, 0)
    auto result = solver.to_max().optimize(objfunc, opt::initvals({0.0, 0.0}),
                                           opt::bounds({b, b}));

    // Save the result and fck off
    rot[0] = std::get<0>(result.optimum);
    rot[1] = std::get<1>(result.optimum);

    return rot;
}

}
}
