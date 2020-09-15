#include <limits>

#include <libslic3r/SLA/Rotfinder.hpp>
#include <libslic3r/SLA/Concurrency.hpp>

#include <libslic3r/Optimize/BruteforceOptimizer.hpp>

#include "libslic3r/SLAPrint.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <libslic3r/Geometry.hpp>
#include "Model.hpp"

#include <thread>

namespace Slic3r { namespace sla {

inline bool is_on_floor(const SLAPrintObject &mo)
{
    auto opt_elevation = mo.config().support_object_elevation.getFloat();
    auto opt_padaround = mo.config().pad_around_object.getBool();

    return opt_elevation < EPSILON || opt_padaround;
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

    double zmin = std::numeric_limits<double>::max();
    size_t granularity = vsize / threads;
    return ccr_par::reduce(size_t(0), vsize, zmin, minfn, accessfn, granularity);
}

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

inline const Vec3d DOWN = {0., 0., -1.};
constexpr double POINTS_PER_UNIT_AREA = 1.;

// The score function for a particular face
inline double get_score(const Facestats &fc)
{
    // Simply get the angle (acos of dot product) between the face normal and
    // the DOWN vector.
    double phi = 1. - std::acos(fc.normal.dot(DOWN)) / PI;

    // Only consider faces that have have slopes below 90 deg:
    phi = phi * (phi > 0.5);

    // Make the huge slopes more significant than the smaller slopes
    phi = phi * phi * phi;

    // Multiply with the area of the current face
    return fc.area * POINTS_PER_UNIT_AREA * phi;
}

template<class AccessFn>
double sum_score(AccessFn &&accessfn, size_t facecount, size_t Nthreads)
{
    double initv     = 0.;
    auto   mergefn   = std::plus<double>{};
    size_t grainsize = facecount / Nthreads;
    size_t from = 0, to = facecount;

    return ccr_par::reduce(from, to, initv, mergefn, accessfn, grainsize);
}

// Try to guess the number of support points needed to support a mesh
double get_model_supportedness(const TriangleMesh &mesh, const Transform3d &tr)
{
    if (mesh.its.vertices.empty()) return std::nan("");

    auto accessfn = [&mesh, &tr](size_t fi) {
        Facestats fc{get_transformed_triangle(mesh, tr, fi)};
        return get_score(fc);
    };

    size_t facecount = mesh.its.indices.size();
    size_t Nthreads  = std::thread::hardware_concurrency();
    return sum_score(accessfn, facecount, Nthreads) / facecount;
}

double get_model_supportedness_onfloor(const TriangleMesh &mesh,
                                       const Transform3d & tr)
{
    if (mesh.its.vertices.empty()) return std::nan("");

    size_t Nthreads = std::thread::hardware_concurrency();

    double zmin = find_ground_level(mesh, tr, Nthreads);
    double zlvl = zmin + 0.1; // Set up a slight tolerance from z level

    auto accessfn = [&mesh, &tr, zlvl](size_t fi) {
        std::array<Vec3d, 3> tri = get_transformed_triangle(mesh, tr, fi);
        Facestats fc{tri};

        if (tri[0].z() <= zlvl && tri[1].z() <= zlvl && tri[2].z() <= zlvl)
            return -fc.area * POINTS_PER_UNIT_AREA;

        return get_score(fc);
    };

    size_t facecount = mesh.its.indices.size();
    return sum_score(accessfn, facecount, Nthreads) / facecount;
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

XYRotation from_transform3d(const Transform3d &tr)
{
    Vec3d rot3d = Geometry::Transformation {tr}.get_rotation();
    return {rot3d.x(), rot3d.y()};
}

// Find the best score from a set of function inputs. Evaluate for every point.
template<size_t N, class Fn, class It, class StopCond>
std::array<double, N> find_min_score(Fn &&fn, It from, It to, StopCond &&stopfn)
{
    std::array<double, N> ret;

    double score = std::numeric_limits<double>::max();

    size_t Nthreads = std::thread::hardware_concurrency();
    size_t dist = std::distance(from, to);
    std::vector<double> scores(dist, score);

    ccr_par::for_each(size_t(0), dist, [&stopfn, &scores, &fn, &from](size_t i) {
        if (stopfn()) return;

        scores[i] = fn(*(from + i));
    }, dist / Nthreads);

    auto it = std::min_element(scores.begin(), scores.end());

    if (it != scores.end()) ret = *(from + std::distance(scores.begin(), it));

    return ret;
}

// collect the rotations for each face of the convex hull
std::vector<XYRotation> get_chull_rotations(const TriangleMesh &mesh, size_t max_count)
{
    TriangleMesh chull = mesh.convex_hull_3d();
    chull.require_shared_vertices();
    double chull2d_area = chull.convex_hull().area();
    double area_threshold = chull2d_area / (scaled<double>(1e3) * scaled(1.));

    size_t facecount = chull.its.indices.size();

    struct RotArea { XYRotation rot; double area; };

    auto inputs = reserve_vector<RotArea>(facecount);

    auto rotcmp = [](const RotArea &r1, const RotArea &r2) {
        double xdiff = r1.rot[X] - r2.rot[X], ydiff = r1.rot[Y] - r2.rot[Y];
        return std::abs(xdiff) < EPSILON ? ydiff < 0. : xdiff < 0.;
    };

    auto eqcmp = [](const XYRotation &r1, const XYRotation &r2) {
        double xdiff = r1[X] - r2[X], ydiff = r1[Y] - r2[Y];
        return std::abs(xdiff) < EPSILON  && std::abs(ydiff) < EPSILON;
    };

    for (size_t fi = 0; fi < facecount; ++fi) {
        Facestats fc{get_triangle_vertices(chull, fi)};

        if (fc.area > area_threshold)  {
            auto q = Eigen::Quaterniond{}.FromTwoVectors(fc.normal, DOWN);
            XYRotation rot = from_transform3d(Transform3d::Identity() * q);
            RotArea ra = {rot, fc.area};

            auto it = std::lower_bound(inputs.begin(), inputs.end(), ra, rotcmp);

            if (it == inputs.end() || !eqcmp(it->rot, rot))
                inputs.insert(it, ra);
        }
    }

    inputs.shrink_to_fit();
    if (!max_count) max_count = inputs.size();
    std::sort(inputs.begin(), inputs.end(),
              [](const RotArea &ra, const RotArea &rb) {
                  return ra.area > rb.area;
              });

    auto ret = reserve_vector<XYRotation>(std::min(max_count, inputs.size()));
    for (const RotArea &ra : inputs) ret.emplace_back(ra.rot);

    return ret;
}

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

    // Different search methods have to be used depending on the model elevation
    if (is_on_floor(po)) {

        std::vector<XYRotation> inputs = get_chull_rotations(mesh, max_tries);
        max_tries = inputs.size();

        // If the model can be placed on the bed directly, we only need to
        // check the 3D convex hull face rotations.

        auto objfn = [&mesh, &statusfn](const XYRotation &rot) {
            statusfn();
            Transform3d tr = to_transform3d(rot);
            return get_model_supportedness_onfloor(mesh, tr);
        };

        rot = find_min_score<2>(objfn, inputs.begin(), inputs.end(), stopcond);
    } else {
        // Preparing the optimizer.
        size_t gridsize = std::sqrt(max_tries); // 2D grid has gridsize^2 calls
        opt::Optimizer<opt::AlgBruteForce> solver(opt::StopCriteria{}
                                                    .max_iterations(max_tries)
                                                    .stop_condition(stopcond),
                                                  gridsize);

        // We are searching rotations around only two axes x, y. Thus the
        // problem becomes a 2 dimensional optimization task.
        // We can specify the bounds for a dimension in the following way:
        auto bounds = opt::bounds({ {-PI, PI}, {-PI, PI} });

        auto result = solver.to_min().optimize(
            [&mesh, &statusfn] (const XYRotation &rot)
            {
                statusfn();
                return get_model_supportedness(mesh, to_transform3d(rot));
            }, opt::initvals({0., 0.}), bounds);

        // Save the result and fck off
        rot = result.optimum;
    }

    return {rot[0], rot[1]};
}

double get_model_supportedness(const SLAPrintObject &po, const Transform3d &tr)
{
    TriangleMesh mesh = po.model_object()->raw_mesh();
    mesh.require_shared_vertices();

    return is_on_floor(po) ? get_model_supportedness_onfloor(mesh, tr) :
                             get_model_supportedness(mesh, tr);
}

}} // namespace Slic3r::sla
