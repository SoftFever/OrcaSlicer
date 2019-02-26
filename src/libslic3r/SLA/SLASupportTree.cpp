/**
 * In this file we will implement the automatic SLA support tree generation.
 *
 */

#include <numeric>
#include "SLASupportTree.hpp"
#include "SLABoilerPlate.hpp"
#include "SLASpatIndex.hpp"
#include "SLABasePool.hpp"

#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/Model.hpp>

#include <libnest2d/optimizers/nlopt/simplex.hpp>
#include <boost/log/trivial.hpp>
#include <tbb/parallel_for.h>

/**
 * Terminology:
 *
 * Support point:
 * The point on the model surface that needs support.
 *
 * Pillar:
 * A thick column that spans from a support point to the ground and has
 * a thick cone shaped base where it touches the ground.
 *
 * Ground facing support point:
 * A support point that can be directly connected with the ground with a pillar
 * that does not collide or cut through the model.
 *
 * Non ground facing support point:
 * A support point that cannot be directly connected with the ground (only with
 * the model surface).
 *
 * Head:
 * The pinhead that connects to the model surface with the sharp end end
 * to a pillar or bridge stick with the dull end.
 *
 * Headless support point:
 * A support point on the model surface for which there is not enough place for
 * the head. It is either in a hole or there is some barrier that would collide
 * with the head geometry. The headless support point can be ground facing and
 * non ground facing as well.
 *
 * Bridge:
 * A stick that connects two pillars or a head with a pillar.
 *
 * Junction:
 * A small ball in the intersection of two or more sticks (pillar, bridge, ...)
 *
 * CompactBridge:
 * A bridge that connects a headless support point with the model surface or a
 * nearby pillar.
 */

namespace Slic3r {
namespace sla {

using Coordf = double;
using Portion = std::tuple<double, double>;

inline Portion make_portion(double a, double b) {
    return std::make_tuple(a, b);
}

template<class Vec> double distance(const Vec& p) {
    return std::sqrt(p.transpose() * p);
}

template<class Vec> double distance(const Vec& pp1, const Vec& pp2) {
    auto p = pp2 - pp1;
    return distance(p);
}

Contour3D sphere(double rho, Portion portion = make_portion(0.0, 2.0*PI),
                 double fa=(2*PI/360)) {

    Contour3D ret;

    // prohibit close to zero radius
    if(rho <= 1e-6 && rho >= -1e-6) return ret;

    auto& vertices = ret.points;
    auto& facets = ret.indices;

    // Algorithm:
    // Add points one-by-one to the sphere grid and form facets using relative
    // coordinates. Sphere is composed effectively of a mesh of stacked circles.

    // adjust via rounding to get an even multiple for any provided angle.
    double angle = (2*PI / floor(2*PI / fa));

    // Ring to be scaled to generate the steps of the sphere
    std::vector<double> ring;

    for (double i = 0; i < 2*PI; i+=angle) ring.emplace_back(i);

    const auto sbegin = size_t(2*std::get<0>(portion)/angle);
    const auto send = size_t(2*std::get<1>(portion)/angle);

    const size_t steps = ring.size();
    const double increment = 1.0 / double(steps);

    // special case: first ring connects to 0,0,0
    // insert and form facets.
    if(sbegin == 0)
        vertices.emplace_back(Vec3d(0.0, 0.0, -rho + increment*sbegin*2.0*rho));

    auto id = coord_t(vertices.size());
    for (size_t i = 0; i < ring.size(); i++) {
        // Fixed scaling
        const double z = -rho + increment*rho*2.0 * (sbegin + 1.0);
        // radius of the circle for this step.
        const double r = std::sqrt(std::abs(rho*rho - z*z));
        Vec2d b = Eigen::Rotation2Dd(ring[i]) * Eigen::Vector2d(0, r);
        vertices.emplace_back(Vec3d(b(0), b(1), z));

        if(sbegin == 0)
        facets.emplace_back((i == 0) ? Vec3crd(coord_t(ring.size()), 0, 1) :
                                       Vec3crd(id - 1, 0, id));
        ++ id;
    }

    // General case: insert and form facets for each step,
    // joining it to the ring below it.
    for (size_t s = sbegin + 2; s < send - 1; s++) {
        const double z = -rho + increment*double(s*2.0*rho);
        const double r = std::sqrt(std::abs(rho*rho - z*z));

        for (size_t i = 0; i < ring.size(); i++) {
            Vec2d b = Eigen::Rotation2Dd(ring[i]) * Eigen::Vector2d(0, r);
            vertices.emplace_back(Vec3d(b(0), b(1), z));
            auto id_ringsize = coord_t(id - int(ring.size()));
            if (i == 0) {
                // wrap around
                facets.emplace_back(Vec3crd(id - 1, id,
                                            id + coord_t(ring.size() - 1)));
                facets.emplace_back(Vec3crd(id - 1, id_ringsize, id));
            } else {
                facets.emplace_back(Vec3crd(id_ringsize - 1, id_ringsize, id));
                facets.emplace_back(Vec3crd(id - 1, id_ringsize - 1, id));
            }
            id++;
        }
    }

    // special case: last ring connects to 0,0,rho*2.0
    // only form facets.
    if(send >= size_t(2*PI / angle)) {
        vertices.emplace_back(Vec3d(0.0, 0.0, -rho + increment*send*2.0*rho));
        for (size_t i = 0; i < ring.size(); i++) {
            auto id_ringsize = coord_t(id - int(ring.size()));
            if (i == 0) {
                // third vertex is on the other side of the ring.
                facets.emplace_back(Vec3crd(id - 1, id_ringsize, id));
            } else {
                auto ci = coord_t(id_ringsize + coord_t(i));
                facets.emplace_back(Vec3crd(ci - 1, ci, id));
            }
        }
    }
    id++;

    return ret;
}

// Down facing cylinder in Z direction with arguments:
// r: radius
// h: Height
// ssteps: how many edges will create the base circle
// sp: starting point
Contour3D cylinder(double r, double h, size_t ssteps, const Vec3d sp = {0,0,0})
{
    Contour3D ret;

    auto steps = int(ssteps);
    auto& points = ret.points;
    auto& indices = ret.indices;
    points.reserve(2*ssteps);
    double a = 2*PI/steps;

    Vec3d jp = sp;
    Vec3d endp = {sp(X), sp(Y), sp(Z) + h};

    // Upper circle points
    for(int i = 0; i < steps; ++i) {
        double phi = i*a;
        double ex = endp(X) + r*std::cos(phi);
        double ey = endp(Y) + r*std::sin(phi);
        points.emplace_back(ex, ey, endp(Z));
    }

    // Lower circle points
    for(int i = 0; i < steps; ++i) {
        double phi = i*a;
        double x = jp(X) + r*std::cos(phi);
        double y = jp(Y) + r*std::sin(phi);
        points.emplace_back(x, y, jp(Z));
    }

    // Now create long triangles connecting upper and lower circles
    indices.reserve(2*ssteps);
    auto offs = steps;
    for(int i = 0; i < steps - 1; ++i) {
        indices.emplace_back(i, i + offs, offs + i + 1);
        indices.emplace_back(i, offs + i + 1, i + 1);
    }

    // Last triangle connecting the first and last vertices
    auto last = steps - 1;
    indices.emplace_back(0, last, offs);
    indices.emplace_back(last, offs + last, offs);

    // According to the slicing algorithms, we need to aid them with generating
    // a watertight body. So we create a triangle fan for the upper and lower
    // ending of the cylinder to close the geometry.
    points.emplace_back(jp); size_t ci = points.size() - 1;
    for(int i = 0; i < steps - 1; ++i)
        indices.emplace_back(i + offs + 1, i + offs, ci);

    indices.emplace_back(offs, steps + offs - 1, ci);

    points.emplace_back(endp); ci = points.size() - 1;
    for(int i = 0; i < steps - 1; ++i)
        indices.emplace_back(ci, i, i + 1);

    indices.emplace_back(steps - 1, 0, ci);

    return ret;
}

struct Head {
    Contour3D mesh;

    size_t steps = 45;
    Vec3d dir = {0, 0, -1};
    Vec3d tr = {0, 0, 0};

    double r_back_mm = 1;
    double r_pin_mm = 0.5;
    double width_mm = 2;
    double penetration_mm = 0.5;

    // For identification purposes. This will be used as the index into the
    // container holding the head structures. See SLASupportTree::Impl
    long id = -1;

    // If there is a pillar connecting to this head, then the id will be set.
    long pillar_id = -1;

    inline void invalidate() { id = -1; }
    inline bool is_valid() const { return id >= 0; }

    Head(double r_big_mm,
         double r_small_mm,
         double length_mm,
         double penetration,
         Vec3d direction = {0, 0, -1},    // direction (normal to the dull end )
         Vec3d offset = {0, 0, 0},        // displacement
         const size_t circlesteps = 45):
            steps(circlesteps), dir(direction), tr(offset),
            r_back_mm(r_big_mm), r_pin_mm(r_small_mm), width_mm(length_mm),
            penetration_mm(penetration)
    {

        // We create two spheres which will be connected with a robe that fits
        // both circles perfectly.

        // Set up the model detail level
        const double detail = 2*PI/steps;

        // We don't generate whole circles. Instead, we generate only the
        // portions which are visible (not covered by the robe) To know the
        // exact portion of the bottom and top circles we need to use some
        // rules of tangent circles from which we can derive (using simple
        // triangles the following relations:

        // The height of the whole mesh
        const double h = r_big_mm + r_small_mm + width_mm;
        double phi = PI/2 - std::acos( (r_big_mm - r_small_mm) / h );

        // To generate a whole circle we would pass a portion of (0, Pi)
        // To generate only a half horizontal circle we can pass (0, Pi/2)
        // The calculated phi is an offset to the half circles needed to smooth
        // the transition from the circle to the robe geometry

        auto&& s1 = sphere(r_big_mm, make_portion(0, PI/2 + phi), detail);
        auto&& s2 = sphere(r_small_mm, make_portion(PI/2 + phi, PI), detail);

        for(auto& p : s2.points) z(p) += h;

        mesh.merge(s1);
        mesh.merge(s2);

        for(size_t idx1 = s1.points.size() - steps, idx2 = s1.points.size();
            idx1 < s1.points.size() - 1;
            idx1++, idx2++)
        {
            coord_t i1s1 = coord_t(idx1), i1s2 = coord_t(idx2);
            coord_t i2s1 = i1s1 + 1, i2s2 = i1s2 + 1;

            mesh.indices.emplace_back(i1s1, i2s1, i2s2);
            mesh.indices.emplace_back(i1s1, i2s2, i1s2);
        }

        auto i1s1 = coord_t(s1.points.size()) - coord_t(steps);
        auto i2s1 = coord_t(s1.points.size()) - 1;
        auto i1s2 = coord_t(s1.points.size());
        auto i2s2 = coord_t(s1.points.size()) + coord_t(steps) - 1;

        mesh.indices.emplace_back(i2s2, i2s1, i1s1);
        mesh.indices.emplace_back(i1s2, i2s2, i1s1);

        // To simplify further processing, we translate the mesh so that the
        // last vertex of the pointing sphere (the pinpoint) will be at (0,0,0)
        for(auto& p : mesh.points) z(p) -= (h + r_small_mm - penetration_mm);
    }

    void transform()
    {
        using Quaternion = Eigen::Quaternion<double>;

        // We rotate the head to the specified direction The head's pointing
        // side is facing upwards so this means that it would hold a support
        // point with a normal pointing straight down. This is the reason of
        // the -1 z coordinate
        auto quatern = Quaternion::FromTwoVectors(Vec3d{0, 0, -1}, dir);

        for(auto& p : mesh.points) p = quatern * p + tr;
    }

    double fullwidth() const {
        return 2 * r_pin_mm + width_mm + 2*r_back_mm - penetration_mm;
    }

    Vec3d junction_point() const {
        return tr + ( 2 * r_pin_mm + width_mm + r_back_mm - penetration_mm)*dir;
    }

    double request_pillar_radius(double radius) const {
        const double rmax = r_back_mm;
        return radius > 0 && radius < rmax ? radius : rmax;
    }
};

struct Junction {
    Contour3D mesh;
    double r = 1;
    size_t steps = 45;
    Vec3d pos;

    long id = -1;

    Junction(const Vec3d& tr, double r_mm, size_t stepnum = 45):
        r(r_mm), steps(stepnum), pos(tr)
    {
        mesh = sphere(r_mm, make_portion(0, PI), 2*PI/steps);
        for(auto& p : mesh.points) p += tr;
    }
};

struct Pillar {
    Contour3D mesh;
    Contour3D base;
    double r = 1;
    size_t steps = 0;
    Vec3d endpoint;

    long id = -1;

    // If the pillar connects to a head, this is the id of that head
    bool starts_from_head = true; // Could start from a junction as well
    long start_junction_id = -1;

    Pillar(const Vec3d& jp, const Vec3d& endp,
           double radius = 1, size_t st = 45):
        r(radius), steps(st), endpoint(endp), starts_from_head(false)
    {
        assert(steps > 0);

        double h = jp(Z) - endp(Z);
        assert(h > 0);    // Endpoint is below the starting point

        // We just create a bridge geometry with the pillar parameters and
        // move the data.
        Contour3D body = cylinder(radius, h, st, endp);
        mesh.points.swap(body.points);
        mesh.indices.swap(body.indices);
    }

    Pillar(const Junction& junc, const Vec3d& endp):
        Pillar(junc.pos, endp, junc.r, junc.steps){}

    Pillar(const Head& head, const Vec3d& endp, double radius = 1):
        Pillar(head.junction_point(), endp, head.request_pillar_radius(radius),
               head.steps)
    {
    }

    void add_base(double height = 3, double radius = 2) {
        if(height <= 0) return;

        assert(steps >= 0);
        auto last = int(steps - 1);

        if(radius < r ) radius = r;

        double a = 2*PI/steps;
        double z = endpoint(2) + height;

        for(size_t i = 0; i < steps; ++i) {
            double phi = i*a;
            double x = endpoint(0) + r*std::cos(phi);
            double y = endpoint(1) + r*std::sin(phi);
            base.points.emplace_back(x, y, z);
        }

        for(size_t i = 0; i < steps; ++i) {
            double phi = i*a;
            double x = endpoint(0) + radius*std::cos(phi);
            double y = endpoint(1) + radius*std::sin(phi);
            base.points.emplace_back(x, y, z - height);
        }

        auto ep = endpoint; ep(2) += height;
        base.points.emplace_back(endpoint);
        base.points.emplace_back(ep);

        auto& indices = base.indices;
        auto hcenter = int(base.points.size() - 1);
        auto lcenter = int(base.points.size() - 2);
        auto offs = int(steps);
        for(int i = 0; i < last; ++i) {
            indices.emplace_back(i, i + offs, offs + i + 1);
            indices.emplace_back(i, offs + i + 1, i + 1);
            indices.emplace_back(i, i + 1, hcenter);
            indices.emplace_back(lcenter, offs + i + 1, offs + i);
        }

        indices.emplace_back(0, last, offs);
        indices.emplace_back(last, offs + last, offs);
        indices.emplace_back(hcenter, last, 0);
        indices.emplace_back(offs, offs + last, lcenter);

    }

    bool has_base() const { return !base.points.empty(); }
};

// A Bridge between two pillars (with junction endpoints)
struct Bridge {
    Contour3D mesh;
    double r = 0.8;

    long id = -1;
    long start_jid = -1;
    long end_jid = -1;

    // We should reduce the radius a tiny bit to help the convex hull algorithm
    Bridge(const Vec3d& j1, const Vec3d& j2,
           double r_mm = 0.8, size_t steps = 45):
        r(r_mm)
    {
        using Quaternion = Eigen::Quaternion<double>;
        Vec3d dir = (j2 - j1).normalized();
        double d = distance(j2, j1);

        mesh = cylinder(r, d, steps);

        auto quater = Quaternion::FromTwoVectors(Vec3d{0,0,1}, dir);
        for(auto& p : mesh.points) p = quater * p + j1;
    }

    Bridge(const Junction& j1, const Junction& j2, double r_mm = 0.8):
        Bridge(j1.pos, j2.pos, r_mm, j1.steps) {}

};

// A bridge that spans from model surface to model surface with small connecting
// edges on the endpoints. Used for headless support points.
struct CompactBridge {
    Contour3D mesh;
    long id = -1;

    CompactBridge(const Vec3d& sp,
                  const Vec3d& ep,
                  const Vec3d& n,
                  double r,
                  size_t steps = 45)
    {
        Vec3d startp = sp + r * n;
        Vec3d dir = (ep - startp).normalized();
        Vec3d endp = ep - r * dir;

        Bridge br(startp, endp, r, steps);
        mesh.merge(br.mesh);

        // now add the pins
        double fa = 2*PI/steps;
        auto upperball = sphere(r, Portion{PI / 2 - fa, PI}, fa);
        for(auto& p : upperball.points) p += startp;

        auto lowerball = sphere(r, Portion{0, PI/2 + 2*fa}, fa);
        for(auto& p : lowerball.points) p += endp;

        mesh.merge(upperball);
        mesh.merge(lowerball);
    }
};

// A wrapper struct around the base pool (pad)
struct Pad {
    TriangleMesh tmesh;
    PoolConfig cfg;
    double zlevel = 0;

    Pad() {}

    Pad(const TriangleMesh& object_support_mesh,
        const ExPolygons& baseplate,
        double ground_level,
        const PoolConfig& pcfg) :
        cfg(pcfg),
        zlevel(ground_level +
               (sla::get_pad_fullheight(pcfg) - sla::get_pad_elevation(pcfg)) )
    {
        ExPolygons basep;
        cfg.throw_on_cancel();

        // The 0.1f is the layer height with which the mesh is sampled and then
        // the layers are unified into one vector of polygons.
        base_plate(object_support_mesh, basep,
                   float(cfg.min_wall_height_mm + cfg.min_wall_thickness_mm),
                   0.1f, pcfg.throw_on_cancel);

        for(auto& bp : baseplate) basep.emplace_back(bp);

        create_base_pool(basep, tmesh, cfg);
        tmesh.translate(0, 0, float(zlevel));
    }

    bool empty() const { return tmesh.facets_count() == 0; }
};

// The minimum distance for two support points to remain valid.
static const double /*constexpr*/ D_SP   = 0.1;

enum { // For indexing Eigen vectors as v(X), v(Y), v(Z) instead of numbers
  X, Y, Z
};

//PointSet to_point_set(const std::vector<SupportPoint> &v)
//{
//    PointSet ret(v.size(), 3);
//    long i = 0;
//    for(const SupportPoint& support_point : v) {
//        ret.row(i)(X) = double(support_point.pos(X));
//        ret.row(i)(Y) = double(support_point.pos(Y));
//        ret.row(i)(Z) = double(support_point.pos(Z));
//        ++i;
//    }
//    return ret;
//}

Vec3d model_coord(const ModelInstance& object, const Vec3f& mesh_coord) {
    return object.transform_vector(mesh_coord.cast<double>());
}

inline double ray_mesh_intersect(const Vec3d& s,
                                 const Vec3d& dir,
                                 const EigenMesh3D& m)
{
    return m.query_ray_hit(s, dir).distance();
}

// This function will test if a future pinhead would not collide with the model
// geometry. It does not take a 'Head' object because those are created after
// this test.
// Parameters:
// s: The touching point on the model surface.
// dir: This is the direction of the head from the pin to the back
// r_pin, r_back: the radiuses of the pin and the back sphere
// width: This is the full width from the pin center to the back center
// m: The object mesh
//
// Optional:
// samples: how many rays will be shot
// safety distance: This will be added to the radiuses to have a safety distance
// from the mesh.
double pinhead_mesh_intersect(const Vec3d& s,
                              const Vec3d& dir,
                              double r_pin,
                              double r_back,
                              double width,
                              const EigenMesh3D& m,
                              unsigned samples = 16,
                              double safety_distance = 0.001)
{
    // method based on:
    // https://math.stackexchange.com/questions/73237/parametric-equation-of-a-circle-in-3d-space


    // We will shoot multiple rays from the head pinpoint in the direction of
    // the pinhead robe (side) surface. The result will be the smallest hit
    // distance.

    // Move away slightly from the touching point to avoid raycasting on the
    // inner surface of the mesh.
    Vec3d v = dir;     // Our direction (axis)
    Vec3d c = s + width * dir;
    const double& sd = safety_distance;

    // Two vectors that will be perpendicular to each other and to the axis.
    // Values for a(X) and a(Y) are now arbitrary, a(Z) is just a placeholder.
    Vec3d a(0, 1, 0), b;

    // The portions of the circle (the head-back circle) for which we will shoot
    // rays.
    std::vector<double> phis(samples);
    for(size_t i = 0; i < phis.size(); ++i) phis[i] = i*2*PI/phis.size();

    // We have to address the case when the direction vector v (same as dir)
    // is coincident with one of the world axes. In this case two of its
    // components will be completely zero and one is 1.0. Our method becomes
    // dangerous here due to division with zero. Instead, vector 'a' can be an
    // element-wise rotated version of 'v'
    auto chk1 = [] (double val) { return std::abs(std::abs(val) - 1) < 1e-20; };
    if(chk1(v(X)) || chk1(v(Y)) || chk1(v(Z))) {
        a = {v(Z), v(X), v(Y)};
        b = {v(Y), v(Z), v(X)};
    }
    else {
        a(Z) = -(v(Y)*a(Y)) / v(Z); a.normalize();
        b = a.cross(v);
    }

    // Now a and b vectors are perpendicular to v and to each other. Together
    // they define the plane where we have to iterate with the given angles
    // in the 'phis' vector

    tbb::parallel_for(size_t(0), phis.size(),
                      [&phis, &m, sd, r_pin, r_back, s, a, b, c](size_t i)
    {
        double& phi = phis[i];
        double sinphi = std::sin(phi);
        double cosphi = std::cos(phi);

        // Let's have a safety coefficient for the radiuses.
        double rpscos = (sd + r_pin) * cosphi;
        double rpssin = (sd + r_pin) * sinphi;
        double rpbcos = (sd + r_back) * cosphi;
        double rpbsin = (sd + r_back) * sinphi;

        // Point on the circle on the pin sphere
        Vec3d ps(s(X) + rpscos * a(X) + rpssin * b(X),
                 s(Y) + rpscos * a(Y) + rpssin * b(Y),
                 s(Z) + rpscos * a(Z) + rpssin * b(Z));

        // Point ps is not on mesh but can be inside or outside as well. This
        // would cause many problems with ray-casting. To detect the position we
        // will use the ray-casting result (which has an is_inside predicate).

        // This is the point on the circle on the back sphere
        Vec3d p(c(X) + rpbcos * a(X) + rpbsin * b(X),
                c(Y) + rpbcos * a(Y) + rpbsin * b(Y),
                c(Z) + rpbcos * a(Z) + rpbsin * b(Z));

        Vec3d n = (p - ps).normalized();
        auto hr = m.query_ray_hit(ps + sd*n, n);

        if(hr.is_inside()) { // the hit is inside the model
            if(hr.distance() > 2*r_pin) phi = 0;
            else {
                // re-cast the ray from the outside of the object
                auto hr2 = m.query_ray_hit(ps + (hr.distance() + 2*sd)*n, n);
                phi = hr2.distance();
            }
        } else phi = hr.distance();
    });

    auto mit = std::min_element(phis.begin(), phis.end());

    return *mit;
}


// Checking bridge (pillar and stick as well) intersection with the model. If
// the function is used for headless sticks, the ins_check parameter have to be
// true as the beginning of the stick might be inside the model geometry.
double bridge_mesh_intersect(const Vec3d& s,
                             const Vec3d& dir,
                             double r,
                             const EigenMesh3D& m,
                             bool ins_check = false,
                             unsigned samples = 4,
                             double safety_distance = 0.001)
{
    // helper vector calculations
    Vec3d a(0, 1, 0), b;
    const double& sd = safety_distance;

    // INFO: for explanation of the method used here, see the previous method's
    // comments.

    auto chk1 = [] (double val) { return std::abs(std::abs(val) - 1) < 1e-20; };
    if(chk1(dir(X)) || chk1(dir(Y)) || chk1(dir(Z))) {
        a = {dir(Z), dir(X), dir(Y)};
        b = {dir(Y), dir(Z), dir(X)};
    }
    else {
        a(Z) = -(dir(Y)*a(Y)) / dir(Z); a.normalize();
        b = a.cross(dir);
    }

    // circle portions
    std::vector<double> phis(samples);
    for(size_t i = 0; i < phis.size(); ++i) phis[i] = i*2*PI/phis.size();

    tbb::parallel_for(size_t(0), phis.size(),
                      [&phis, &m, a, b, sd, dir, r, s, ins_check](size_t i)
    {
        double& phi = phis[i];
        double sinphi = std::sin(phi);
        double cosphi = std::cos(phi);

        // Let's have a safety coefficient for the radiuses.
        double rcos = (sd + r) * cosphi;
        double rsin = (sd + r) * sinphi;

        // Point on the circle on the pin sphere
        Vec3d p (s(X) + rcos * a(X) + rsin * b(X),
                 s(Y) + rcos * a(Y) + rsin * b(Y),
                 s(Z) + rcos * a(Z) + rsin * b(Z));

        auto hr = m.query_ray_hit(p + sd*dir, dir);

        if(ins_check && hr.is_inside()) {
            if(hr.distance() > 2*r) phi = 0;
            else {
                // re-cast the ray from the outside of the object
                auto hr2 = m.query_ray_hit(p + (hr.distance() + 2*sd)*dir, dir);
                phi = hr2.distance();
            }
        } else phi = hr.distance();
    });

    auto mit = std::min_element(phis.begin(), phis.end());

    return *mit;
}

// Calculate the normals for the selected points (from 'points' set) on the
// mesh. This will call squared distance for each point.
PointSet normals(const PointSet& points,
                 const EigenMesh3D& mesh,
                 double eps = 0.05,  // min distance from edges
                 std::function<void()> throw_on_cancel = [](){},
                 const std::vector<unsigned>& selected_points = {});

inline Vec2d to_vec2(const Vec3d& v3) {
    return {v3(X), v3(Y)};
}

bool operator==(const SpatElement& e1, const SpatElement& e2) {
    return e1.second == e2.second;
}

// Clustering a set of points by the given criteria.
ClusteredPoints cluster(
        const PointSet& points,
        std::function<bool(const SpatElement&, const SpatElement&)> pred,
        unsigned max_points = 0, const std::vector<unsigned>& indices = {});

// This class will hold the support tree meshes with some additional bookkeeping
// as well. Various parts of the support geometry are stored separately and are
// merged when the caller queries the merged mesh. The merged result is cached
// for fast subsequent delivery of the merged mesh which can be quite complex.
// An object of this class will be used as the result type during the support
// generation algorithm. Parts will be added with the appropriate methods such
// as add_head or add_pillar which forwards the constructor arguments and fills
// the IDs of these substructures. The IDs are basically indices into the arrays
// of the appropriate type (heads, pillars, etc...). One can later query e.g. a
// pillar for a specific head...
//
// The support pad is considered an auxiliary geometry and is not part of the
// merged mesh. It can be retrieved using a dedicated method (pad())
class SLASupportTree::Impl {
    std::map<unsigned, Head> m_heads;
    std::vector<Pillar> m_pillars;
    std::vector<Junction> m_junctions;
    std::vector<Bridge> m_bridges;
    std::vector<CompactBridge> m_compact_bridges;
    Controller m_ctl;

    Pad m_pad;
    mutable TriangleMesh meshcache; mutable bool meshcache_valid = false;
    mutable double model_height = 0; // the full height of the model
public:
    double ground_level = 0;

    Impl() = default;
    inline Impl(const Controller& ctl): m_ctl(ctl) {}

    const Controller& ctl() const { return m_ctl; }

    template<class...Args> Head& add_head(unsigned id, Args&&... args) {
        auto el = m_heads.emplace(std::piecewise_construct,
                            std::forward_as_tuple(id),
                            std::forward_as_tuple(std::forward<Args>(args)...));
        el.first->second.id = id;
        meshcache_valid = false;
        return el.first->second;
    }

    template<class...Args> Pillar& add_pillar(unsigned headid, Args&&... args) {
        auto it = m_heads.find(headid);
        assert(it != m_heads.end());
        Head& head = it->second;
        m_pillars.emplace_back(head, std::forward<Args>(args)...);
        Pillar& pillar = m_pillars.back();
        pillar.id = long(m_pillars.size() - 1);
        head.pillar_id = pillar.id;
        pillar.start_junction_id = head.id;
        pillar.starts_from_head = true;
        meshcache_valid = false;
        return m_pillars.back();
    }

    const Head& pillar_head(long pillar_id) const {
        assert(pillar_id >= 0 && pillar_id < m_pillars.size());
        const Pillar& p = m_pillars[size_t(pillar_id)];
        assert(p.starts_from_head && p.start_junction_id >= 0);
        auto it = m_heads.find(unsigned(p.start_junction_id));
        assert(it != m_heads.end());
        return it->second;
    }

    const Pillar& head_pillar(unsigned headid) const {
        auto it = m_heads.find(headid);
        assert(it != m_heads.end());
        const Head& h = it->second;
        assert(h.pillar_id >= 0 && h.pillar_id < m_pillars.size());
        return m_pillars[size_t(h.pillar_id)];
    }

    template<class...Args> const Junction& add_junction(Args&&... args) {
        m_junctions.emplace_back(std::forward<Args>(args)...);
        m_junctions.back().id = long(m_junctions.size() - 1);
        meshcache_valid = false;
        return m_junctions.back();
    }

    template<class...Args> const Bridge& add_bridge(Args&&... args) {
        m_bridges.emplace_back(std::forward<Args>(args)...);
        m_bridges.back().id = long(m_bridges.size() - 1);
        meshcache_valid = false;
        return m_bridges.back();
    }

    template<class...Args>
    const CompactBridge& add_compact_bridge(Args&&...args) {
        m_compact_bridges.emplace_back(std::forward<Args>(args)...);
        m_compact_bridges.back().id = long(m_compact_bridges.size() - 1);
        meshcache_valid = false;
        return m_compact_bridges.back();
    }

    const std::map<unsigned, Head>& heads() const { return m_heads; }
    Head& head(unsigned idx) {
        meshcache_valid = false;
        auto it = m_heads.find(idx);
        assert(it != m_heads.end());
        return it->second;
    }
    const std::vector<Pillar>& pillars() const { return m_pillars; }
    const std::vector<Bridge>& bridges() const { return m_bridges; }
    const std::vector<Junction>& junctions() const { return m_junctions; }
    const std::vector<CompactBridge>& compact_bridges() const {
        return m_compact_bridges;
    }

    const Pad& create_pad(const TriangleMesh& object_supports,
                          const ExPolygons& baseplate,
                          const PoolConfig& cfg) {
        m_pad = Pad(object_supports, baseplate, ground_level, cfg);
        return m_pad;
    }

    void remove_pad() {
        m_pad = Pad();
    }

    const Pad& pad() const { return m_pad; }

    // WITHOUT THE PAD!!!
    const TriangleMesh& merged_mesh() const {
        if(meshcache_valid) return meshcache;

        Contour3D merged;

        for(auto& headel : heads()) {
            if(m_ctl.stopcondition()) break;
            if(headel.second.is_valid())
                merged.merge(headel.second.mesh);
        }

        for(auto& stick : pillars()) {
            if(m_ctl.stopcondition()) break;
            merged.merge(stick.mesh);
            merged.merge(stick.base);
        }

        for(auto& j : junctions()) {
            if(m_ctl.stopcondition()) break;
            merged.merge(j.mesh);
        }

        for(auto& cb : compact_bridges()) {
            if(m_ctl.stopcondition()) break;
            merged.merge(cb.mesh);
        }

        for(auto& bs : bridges()) {
            if(m_ctl.stopcondition()) break;
            merged.merge(bs.mesh);
        }


        if(m_ctl.stopcondition()) {
            // In case of failure we have to return an empty mesh
            meshcache = TriangleMesh();
            return meshcache;
        }

        meshcache = mesh(merged);

        // TODO: Is this necessary?
        //meshcache.repair();

        BoundingBoxf3&& bb = meshcache.bounding_box();
        model_height = bb.max(Z) - bb.min(Z);

        meshcache_valid = true;
        return meshcache;
    }

    // WITH THE PAD
    double full_height() const {
        if(merged_mesh().empty() && !pad().empty())
            return get_pad_fullheight(pad().cfg);

        double h = mesh_height();
        if(!pad().empty()) h += sla::get_pad_elevation(pad().cfg);
        return h;
    }

    // WITHOUT THE PAD!!!
    double mesh_height() const {
        if(!meshcache_valid) merged_mesh();
        return model_height;
    }

};

template<class DistFn>
long cluster_centroid(const ClusterEl& clust,
                      std::function<Vec3d(size_t)> pointfn,
                      DistFn df)
{
    switch(clust.size()) {
    case 0: /* empty cluster */ return -1;
    case 1: /* only one element */ return 0;
    case 2: /* if two elements, there is no center */ return 0;
    default: ;
    }

    // The function works by calculating for each point the average distance
    // from all the other points in the cluster. We create a selector bitmask of
    // the same size as the cluster. The bitmask will have two true bits and
    // false bits for the rest of items and we will loop through all the
    // permutations of the bitmask (combinations of two points). Get the
    // distance for the two points and add the distance to the averages.
    // The point with the smallest average than wins.

    std::vector<bool> sel(clust.size(), false);   // create full zero bitmask
    std::fill(sel.end() - 2, sel.end(), true);    // insert the two ones
    std::vector<double> avgs(clust.size(), 0.0);  // store the average distances

    do {
        std::array<size_t, 2> idx;
        for(size_t i = 0, j = 0; i < clust.size(); i++) if(sel[i]) idx[j++] = i;

        double d = df(pointfn(clust[idx[0]]),
                      pointfn(clust[idx[1]]));

        // add the distance to the sums for both associated points
        for(auto i : idx) avgs[i] += d;

        // now continue with the next permutation of the bitmask with two 1s
    } while(std::next_permutation(sel.begin(), sel.end()));

    // Divide by point size in the cluster to get the average (may be redundant)
    for(auto& a : avgs) a /= clust.size();

    // get the lowest average distance and return the index
    auto minit = std::min_element(avgs.begin(), avgs.end());
    return long(minit - avgs.begin());
}

/**
 * This function will calculate the convex hull of the input point set and
 * return the indices of those points belonging to the chull in the right
 * (counter clockwise) order. The input is also the set of indices and a
 * functor to get the actual point form the index.
 *
 * I've adapted this algorithm from here:
 * https://www.geeksforgeeks.org/convex-hull-set-1-jarviss-algorithm-or-wrapping/
 * and modified it so that it starts with the leftmost lower vertex. Also added
 * support for floating point coordinates.
 *
 * This function is a modded version of the standard convex hull. If the points
 * are all collinear with each other, it will return their indices in spatially
 * subsequent order (the order they appear on the screen).
 */
ClusterEl pts_convex_hull(const ClusterEl& inpts,
                          std::function<Vec2d(unsigned)> pfn)
{
    using Point = Vec2d;
    using std::vector;

    static const double ERR = 1e-3;

    auto orientation = [](const Point& p, const Point& q, const Point& r)
    {
        double val = (q(Y) - p(Y)) * (r(X) - q(X)) -
                     (q(X) - p(X)) * (r(Y) - q(Y));

        if (std::abs(val) < ERR) return 0;  // collinear
        return (val > ERR)? 1: 2; // clock or counterclockwise
    };

    size_t n = inpts.size();

    if (n < 3) return inpts;

    // Initialize Result
    ClusterEl hull;
    vector<Point> points; points.reserve(n);
    for(auto i : inpts) {
        points.emplace_back(pfn(i));
    }

    // Check if the triplet of points is collinear. The standard convex hull
    // algorithms are not capable of handling such input properly.
    bool collinear = true;
    for(auto one = points.begin(), two = std::next(one), three = std::next(two);
        three != points.end() && collinear;
        ++one, ++two, ++three)
    {
        // check if the points are collinear
        if(orientation(*one, *two, *three) != 0) collinear = false;
    }

    // Find the leftmost (bottom) point
    size_t l = 0;
    for (size_t i = 1; i < n; i++) {
        if(std::abs(points[i](X) - points[l](X)) < ERR) {
            if(points[i](Y) < points[l](Y)) l = i;
        }
        else if (points[i](X) < points[l](X)) l = i;
    }

    if(collinear) {
        // fill the output with the spatially ordered set of points.

        // find the direction
        hull = inpts;
        auto& lp = points[l];
        std::sort(hull.begin(), hull.end(),
                  [&lp, points](unsigned i1, unsigned i2) {
            // compare the distance from the leftmost point
            return distance(lp, points[i1]) < distance(lp, points[i2]);
        });

        return hull;
    }

    // TODO: this algorithm is O(m*n) and O(n^2) in the worst case so it needs
    // to be replaced with a graham scan or something O(nlogn)

    // Start from leftmost point, keep moving counterclockwise
    // until reach the start point again.  This loop runs O(h)
    // times where h is number of points in result or output.
    size_t p = l;
    do
    {
        // Add current point to result
        hull.push_back(inpts[p]);

        // Search for a point 'q' such that orientation(p, x,
        // q) is counterclockwise for all points 'x'. The idea
        // is to keep track of last visited most counterclock-
        // wise point in q. If any point 'i' is more counterclock-
        // wise than q, then update q.
        size_t q = (p + 1) % n;
        for (size_t i = 0; i < n; i++)
        {
           // If i is more counterclockwise than current q, then
           // update q
           if (orientation(points[p], points[i], points[q]) == 2) q = i;
        }

        // Now q is the most counterclockwise with respect to p
        // Set p as q for next iteration, so that q is added to
        // result 'hull'
        p = q;

    } while (p != l);  // While we don't come to first point

    auto first = hull.front();
    hull.emplace_back(first);

    return hull;
}

Vec3d dirv(const Vec3d& startp, const Vec3d& endp) {
    return (endp - startp).normalized();
}

bool SLASupportTree::generate(const std::vector<SupportPoint> &support_points,
                              const EigenMesh3D& mesh,
                              const SupportConfig &cfg,
                              const Controller &ctl)
{
    // Prepare the support points in Eigen/IGL format as well, we will use it
    // mostly in this form.
    Eigen::MatrixXd points(support_points.size(), 3); long i = 0;
    for(const SupportPoint& sp : support_points) {
        points.row(i)(X) = double(sp.pos(X));
        points.row(i)(Y) = double(sp.pos(Y));
        points.row(i)(Z) = double(sp.pos(Z));
        ++i;
    }

    // If there are no input points there is no point in doing anything
    if(points.rows() == 0) return false;

    using PtIndices = std::vector<unsigned>;
    const size_t pcount = size_t(points.rows());

    PtIndices filtered_indices;        // all valid support points
    PtIndices head_indices;            // support points with pinhead
    PtIndices headless_indices;        // headless support points
    PtIndices onmodel_head_indices;    // supp. pts. connecting to model

    PointSet  support_normals(pcount, 3);  // support point normals

    // Captures the height of the processed support points from the ground
    // or the model surface
    std::vector<double> pt_heights(size_t(points.rows()), 0.0);

    // Clusters of points which can reach the ground directly
    std::vector<PtIndices> pillar_clusters;

    // This algorithm uses the Impl class as its output stream. It will be
    // filled gradually with support elements (heads, pillars, bridges, ...)
    using Result = SLASupportTree::Impl;
    Result& result = *m_impl;

    // Let's define the individual steps of the processing. We can experiment
    // later with the ordering and the dependencies between them.
    enum Steps {
        BEGIN,
        FILTER,
        PINHEADS,
        CLASSIFY,
        ROUTING_GROUND,
        ROUTING_NONGROUND,
        HEADLESS,
        DONE,
        HALT,
        ABORT,
        NUM_STEPS
        //...
    };

    // throw if canceled: It will be called many times so a shorthand will
    // come in handy.
    ThrowOnCancel thr = ctl.cancelfn;

    // Each step has a processing block in a form of a function.

    // Filtering step: here we will discard inappropriate support points
    // and decide the future of the appropriate ones. We will check if a
    // pinhead is applicable and adjust its angle at each support point. We
    // will also merge the support points that are just too close and can
    // be considered as one.
    auto filter_fn = [thr](const SupportConfig& cfg,
                           const PointSet& points,
                           const EigenMesh3D& mesh,
                           PointSet&  support_normals,
                           PtIndices& filtered_indices,
                           PtIndices& head_indices,
                           PtIndices& headless_indices)
    {
        // Get the points that are too close to each other and keep only the
        // first one
        auto aliases = cluster(points,
                         [thr](const SpatElement& p, const SpatElement& se)
        {
            thr(); return distance(p.first, se.first) < D_SP;
        }, 2);


        filtered_indices.resize(aliases.size());
        head_indices.reserve(aliases.size());
        headless_indices.reserve(aliases.size());
        unsigned count = 0;
        for(auto& a : aliases) {
            // Here we keep only the front point of the cluster.
            filtered_indices[count++] = a.front();
        }

        // calculate the normals to the triangles for filtered points
        auto nmls = sla::normals(points, mesh, cfg.head_front_radius_mm,
                                 thr, filtered_indices);

        // Not all of the support points have to be a valid position for
        // support creation. The angle may be inappropriate or there may
        // not be enough space for the pinhead. Filtering is applied for
        // these reasons.

        using libnest2d::opt::bound;
        using libnest2d::opt::initvals;
        using libnest2d::opt::SimplexOptimizer;
        using libnest2d::opt::StopCriteria;
        static const unsigned MAX_TRIES = 100;

        for(unsigned i = 0; i < count; i++) {
            thr();
            auto n = nmls.row(i);

            // for all normals we generate the spherical coordinates and
            // saturate the polar angle to 45 degrees from the bottom then
            // convert back to standard coordinates to get the new normal.
            // Then we just create a quaternion from the two normals
            // (Quaternion::FromTwoVectors) and apply the rotation to the
            // arrow head.

            double z = n(2);
            double r = 1.0;     // for normalized vector
            double polar = std::acos(z / r);
            double azimuth = std::atan2(n(1), n(0));

            // skip if the tilt is not sane
            if(polar >= PI - cfg.normal_cutoff_angle) {

                // We saturate the polar angle to 3pi/4
                polar = std::max(polar, 3*PI / 4);

                // save the head (pinpoint) position
                Vec3d hp = points.row(filtered_indices[i]);

                double w = cfg.head_width_mm +
                           cfg.head_back_radius_mm +
                           2*cfg.head_front_radius_mm;

                // Reassemble the now corrected normal
                auto nn = Vec3d(std::cos(azimuth) * std::sin(polar),
                                std::sin(azimuth) * std::sin(polar),
                                std::cos(polar)).normalized();

                // check available distance
                double t = pinhead_mesh_intersect(
                                  hp, // touching point
                                  nn, // normal
                                  cfg.head_front_radius_mm,
                                  cfg.head_back_radius_mm,
                                  w,
                                  mesh);

                if(t <= w) {

                    // Let's try to optimize this angle, there might be a
                    // viable normal that doesn't collide with the model
                    // geometry and its very close to the default.

                    StopCriteria stc;
                    stc.max_iterations = MAX_TRIES;
                    stc.relative_score_difference = 1e-3;
                    stc.stop_score = w; // space greater than w is enough
                    SimplexOptimizer solver(stc);

                    auto oresult = solver.optimize_max(
                                [&mesh, &cfg, w, hp](double plr, double azm)
                    {
                        auto n = Vec3d(std::cos(azm) * std::sin(plr),
                                       std::sin(azm) * std::sin(plr),
                                       std::cos(plr)).normalized();

                        double score = pinhead_mesh_intersect(
                                          hp, n,
                                          cfg.head_front_radius_mm,
                                          cfg.head_back_radius_mm,
                                          w,
                                          mesh);
                        return score;
                    },
                    initvals(polar, azimuth), // start with what we have
                    bound(3*PI/4, PI),  // Must not exceed the tilt limit
                    bound(-PI, PI)      // azimuth can be a full search
                    );

                    t = oresult.score;
                    polar = std::get<0>(oresult.optimum);
                    azimuth = std::get<1>(oresult.optimum);
                    nn = Vec3d(std::cos(azimuth) * std::sin(polar),
                               std::sin(azimuth) * std::sin(polar),
                               std::cos(polar)).normalized();
                }

                // save the verified and corrected normal
                support_normals.row(filtered_indices[i]) = nn;

                if(t > w) {
                    // mark the point for needing a head.
                    head_indices.emplace_back(filtered_indices[i]);
                } else if( polar >= 3*PI/4 ) {
                    // Headless supports do not tilt like the headed ones so
                    // the normal should point almost to the ground.
                    headless_indices.emplace_back(filtered_indices[i]);
                }
            }
        }

        thr();
    };

    // Pinhead creation: based on the filtering results, the Head objects
    // will be constructed (together with their triangle meshes).
    auto pinheads_fn = [thr](const SupportConfig& cfg,
                             const std::vector<SupportPoint> support_points,
                             const PointSet& support_normals,
                             const PtIndices& head_indices,
                             Result& result)
    {
        /* ******************************************************** */
        /* Generating Pinheads                                      */
        /* ******************************************************** */

        for (unsigned i : head_indices) {
            thr();
            result.add_head(i,
                        cfg.head_back_radius_mm,
                        support_points[i].head_front_radius,
                        cfg.head_width_mm,
                        cfg.head_penetration_mm,
                        support_normals.row(i),         // dir
                        support_points[i].pos.cast<double>() // displacement
                        );
        }
    };

    // Further classification of the support points with pinheads. If the
    // ground is directly reachable through a vertical line parallel to the
    // Z axis we consider a support point as pillar candidate. If touches
    // the model geometry, it will be marked as non-ground facing and
    // further steps will process it. Also, the pillars will be grouped
    // into clusters that can be interconnected with bridges. Elements of
    // these groups may or may not be interconnected. Here we only run the
    // clustering algorithm.
    auto classify_fn = [thr](const SupportConfig& cfg,
                             const PointSet& points,
                             const EigenMesh3D& mesh,
                             const PtIndices& head_indices,
                             PtIndices& onmodel_head_indices,
                             std::vector<double>& pt_heights,
                             std::vector<PtIndices>& pillar_clusters,
                             Result& result)
    {
        /* ******************************************************** */
        /* Classification                                           */
        /* ******************************************************** */

        // We should first get the heads that reach the ground directly
        pt_heights.reserve(head_indices.size());
        PtIndices ground_head_indices;
        ground_head_indices.reserve(head_indices.size());
        onmodel_head_indices.reserve(head_indices.size());

        // First we decide which heads reach the ground and can be full
        // pillars and which shall be connected to the model surface (or
        // search a suitable path around the surface that leads to the
        // ground -- TODO)
        for(unsigned i : head_indices) {
            thr();

            auto& head = result.head(i);
            Vec3d n(0, 0, -1);
            double r = head.r_back_mm;
            Vec3d headjp = head.junction_point();

            // collision check
            double t = bridge_mesh_intersect(headjp, n, r, mesh);

            // Precise distance measurement
            double tprec = ray_mesh_intersect(headjp, n, mesh);

            // Save the distance from a surface in the Z axis downwards. It
            // may be infinity but that is telling us that it touches the
            // ground.
            pt_heights.emplace_back(tprec);

            if(std::isinf(t)) ground_head_indices.emplace_back(i);
            else onmodel_head_indices.emplace_back(i);
        }

        // We want to search for clusters of points that are far enough
        // from each other in the XY plane to not cross their pillar bases
        // These clusters of support points will join in one pillar,
        // possibly in their centroid support point.
        auto d_base = 2*cfg.base_radius_mm;
        pillar_clusters = cluster(points,
                [thr, d_base](const SpatElement& p, const SpatElement& s)
        {
            thr();
            return distance(Vec2d(p.first(X), p.first(Y)),
                            Vec2d(s.first(X), s.first(Y))) < d_base;
        }, 3, ground_head_indices); // max 3 heads to connect to one pillar
    };

    // Helper function for interconnecting two pillars with zig-zag bridges.
    // This is not an individual step.
    auto interconnect = [&cfg](const Pillar& pillar,
                               const Pillar& nextpillar,
                               const EigenMesh3D& m,
                               Result& result)
    {
        const Head& phead = result.pillar_head(pillar.id);
        const Head& nextphead = result.pillar_head(nextpillar.id);

        Vec3d sj = phead.junction_point();
        sj(Z) = std::min(sj(Z), nextphead.junction_point()(Z));
        Vec3d ej = nextpillar.endpoint;
        double pillar_dist = distance(Vec2d{sj(X), sj(Y)},
                                      Vec2d{ej(X), ej(Y)});
        double zstep = pillar_dist * std::tan(-cfg.head_slope);
        ej(Z) = sj(Z) + zstep;

        double chkd = bridge_mesh_intersect(sj, dirv(sj, ej), pillar.r, m);
        double bridge_distance = pillar_dist / std::cos(-cfg.head_slope);

        // If the pillars are so close that they touch each other,
        // there is no need to bridge them together.
        if(pillar_dist > 2*cfg.head_back_radius_mm &&
           bridge_distance < cfg.max_bridge_length_mm)
            while(sj(Z) > pillar.endpoint(Z) + cfg.base_radius_mm &&
                  ej(Z) > nextpillar.endpoint(Z) + cfg.base_radius_mm)
        {
            if(chkd >= bridge_distance) {
                result.add_bridge(sj, ej, pillar.r);

                auto pcm = cfg.pillar_connection_mode;

                // double bridging: (crosses)
                if( pcm == PillarConnectionMode::cross ||
                   (pcm == PillarConnectionMode::dynamic &&
                    pillar_dist > 2*cfg.base_radius_mm))
                {
                    // If the columns are close together, no need to
                    // double bridge them
                    Vec3d bsj(ej(X), ej(Y), sj(Z));
                    Vec3d bej(sj(X), sj(Y), ej(Z));

                    // need to check collision for the cross stick
                    double backchkd = bridge_mesh_intersect(bsj,
                                                            dirv(bsj, bej),
                                                            pillar.r, m);


                    if(backchkd >= bridge_distance) {
                        result.add_bridge(bsj, bej, pillar.r);
                    }
                }
            }
            sj.swap(ej);
            ej(Z) = sj(Z) + zstep;
            chkd = bridge_mesh_intersect(sj, dirv(sj, ej), pillar.r, m);
        }
    };

    // Step: Routing the ground connected pinheads, and interconnecting
    // them with additional (angled) bridges. Not all of these pinheads
    // will be a full pillar (ground connected). Some will connect to a
    // nearby pillar using a bridge. The max number of such side-heads for
    // a central pillar is limited to avoid bad weight distribution.
    auto routing_ground_fn =
            [thr, interconnect](const SupportConfig& cfg,
                                const PointSet& points,
                                const std::vector<PtIndices>& pillar_clusters,
                                const EigenMesh3D& emesh,
                                Result& result)
    {
        const double hbr = cfg.head_back_radius_mm;
        const double pradius = cfg.head_back_radius_mm;
        const double maxbridgelen = cfg.max_bridge_length_mm;
        const double gndlvl = result.ground_level;

        ClusterEl cl_centroids;
        cl_centroids.reserve(pillar_clusters.size());

        SpatIndex pheadindex; // spatial index for the junctions
        for(auto& cl : pillar_clusters) { thr();
            // place all the centroid head positions into the index. We
            // will query for alternative pillar positions. If a sidehead
            // cannot connect to the cluster centroid, we have to search
            // for another head with a full pillar. Also when there are two
            // elements in the cluster, the centroid is arbitrary and the
            // sidehead is allowed to connect to a nearby pillar to
            // increase structural stability.

            if(cl.empty()) continue;

            // get the current cluster centroid
            long lcid = cluster_centroid(cl,
                [&points](size_t idx) { return points.row(long(idx)); },
                [thr](const Vec3d& p1, const Vec3d& p2)
            {
                thr();
                return distance(Vec2d(p1(X), p1(Y)), Vec2d(p2(X), p2(Y)));
            });

            assert(lcid >= 0);
            auto cid = unsigned(lcid);

            cl_centroids.push_back(unsigned(cid));


            unsigned hid = cl[cid]; // Head ID
            Head& h = result.head(hid);
            h.transform();
            Vec3d p = h.junction_point(); p(Z) = gndlvl;
            pheadindex.insert(p, hid);
        }

        // now we will go through the clusters ones again and connect the
        // sidepoints with the cluster centroid (which is a ground pillar)
        // or a nearby pillar if the centroid is unreachable.
        size_t ci = 0;
        for(auto cl : pillar_clusters) { thr();

            auto cidx = cl_centroids[ci];
            cl_centroids[ci++] = cl[cidx];

            auto& head = result.head(cl[cidx]);

            Vec3d startpoint = head.junction_point();
            auto endpoint = startpoint; endpoint(Z) = gndlvl;

            // Create the central pillar of the cluster with its base on the
            // ground
            result.add_pillar(unsigned(head.id), endpoint, pradius)
                  .add_base(cfg.base_height_mm, cfg.base_radius_mm);

            // Process side point in current cluster
            cl.erase(cl.begin() + cidx); // delete the centroid

            // TODO: don't consider the cluster centroid but calculate a
            // central position where the pillar can be placed. this way
            // the weight is distributed more effectively on the pillar.
            auto search_nearest =
                [thr, &cfg, &result, &emesh, maxbridgelen, gndlvl, pradius]
                (SpatIndex& spindex, const Vec3d& jsh)
            {
                long nearest_id = -1;
                const double max_len = maxbridgelen / 2;
                while(nearest_id < 0 && !spindex.empty()) { thr();
                    // loop until a suitable head is not found
                    // if there is a pillar closer than the cluster center
                    // (this may happen as the clustering is not perfect)
                    // than we will bridge to this closer pillar

                    Vec3d qp(jsh(X), jsh(Y), gndlvl);
                    auto ne = spindex.nearest(qp, 1).front();
                    const Head& nearhead = result.head(ne.second);

                    Vec3d jh = nearhead.junction_point();
                    Vec3d jp = jsh;
                    double dist2d = distance(qp, ne.first);

                    // Bridge endpoint on the main pillar
                    Vec3d jn(jh(X), jh(Y), jp(Z) +
                             dist2d * std::tan(-cfg.head_slope));

                    if(jn(Z) > jh(Z)) {
                        // If the sidepoint cannot connect to the pillar
                        // from its head junction, just skip this pillar.
                        spindex.remove(ne);
                        continue;
                    }

                    double d = distance(jp, jn);

                    if(jn(Z) <= gndlvl + 2*cfg.head_width_mm || d > max_len)
                        break;

                    double chkd = bridge_mesh_intersect(jp, dirv(jp, jn),
                                                        pradius,
                                                        emesh);
                    if(chkd >= d) nearest_id = ne.second;

                    spindex.remove(ne);
                }
                return nearest_id;
            };

            for(auto c : cl) { thr();
                auto& sidehead = result.head(c);
                sidehead.transform();

                Vec3d jsh = sidehead.junction_point();
                SpatIndex spindex = pheadindex;
                long nearest_id = search_nearest(spindex, jsh);

                // at this point we either have our pillar index or we have
                // to connect the sidehead to the ground
                if(nearest_id < 0) {
                    // Could not find a pillar, create one
                    Vec3d jp = jsh; jp(Z) = gndlvl;
                    result.add_pillar(unsigned(sidehead.id), jp, pradius).
                        add_base(cfg.base_height_mm, cfg.base_radius_mm);

                    // connects to ground, eligible for bridging
                    cl_centroids.emplace_back(c);
                } else {
                    // Creating the bridge to the nearest pillar

                    auto nearest_uid = unsigned(nearest_id);
                    const Head& nearhead = result.head(nearest_uid);
                    Vec3d jp = jsh;
                    Vec3d jh = nearhead.junction_point();

                    double d = distance(Vec2d{jp(X), jp(Y)},
                                        Vec2d{jh(X), jh(Y)});
                    Vec3d jn(jh(X), jh(Y), jp(Z) +
                             d * std::tan(-cfg.head_slope));

                    if(jn(Z) > jh(Z)) {
                        double hdiff = jn(Z) - jh(Z);
                        jp(Z) -= hdiff;
                        jn(Z) -= hdiff;

                        // pillar without base, doesn't connect to ground.
                        result.add_pillar(nearest_uid, jp, pradius);
                    }

                    if(jp(Z) < jsh(Z)) result.add_junction(jp, hbr);
                    if(jn(Z) >= jh(Z)) result.add_junction(jn, hbr);

                    result.add_bridge(jp, jn,
                                sidehead.request_pillar_radius(pradius));
                }
            }
        }

        // We will break down the pillar positions in 2D into concentric
        // rings. Connecting the pillars belonging to the same ring will
        // prevent bridges from crossing each other. After bridging the
        // rings we can create bridges between the rings without the
        // possibility of crossing bridges. Two pillars will be bridged
        // with X shaped stick pairs. If they are really close to each
        // other, than only one stick will be used in zig-zag mode.

        // Breaking down the points into rings will be done with a modified
        // convex hull algorithm (see pts_convex_hull()), that works for
        // collinear points as well. If the points are on the same surface,
        // they can be part of an imaginary line segment for which the
        // convex hull is not defined. I this case it is enough to sort the
        // points spatially and create the bridge stick from one endpoint to
        // another.

        ClusterEl rem = cl_centroids;
        ClusterEl ring;

        while(!rem.empty()) { // loop until all the points belong to some ring
            thr();
            std::sort(rem.begin(), rem.end());

            auto newring = pts_convex_hull(rem,
                                           [&points](unsigned i) {
                auto&& p = points.row(i);
                return Vec2d(p(X), p(Y)); // project to 2D in along Z axis
            });

            if(!ring.empty()) {
                // inner ring is now in 'newring' and outer ring is in 'ring'
                SpatIndex innerring;
                for(unsigned i : newring) { thr();
                    const Pillar& pill = result.head_pillar(i);
                    assert(pill.id >= 0);
                    innerring.insert(pill.endpoint, unsigned(pill.id));
                }

                // For all pillars in the outer ring find the closest in the
                // inner ring and connect them. This will create the spider web
                // fashioned connections between pillars
                for(unsigned i : ring) { thr();
                    const Pillar& outerpill = result.head_pillar(i);
                    auto res = innerring.nearest(outerpill.endpoint, 1);
                    if(res.empty()) continue;

                    auto ne = res.front();
                    const Pillar& innerpill = result.pillars()[ne.second];
                    interconnect(outerpill, innerpill, emesh, result);
                }
            }

            // no need for newring anymore in the current iteration
            ring.swap(newring);

            /*std::cout << "ring: \n";
            for(auto ri : ring) {
                std::cout << ri << " " << " X = " << gnd_head_pt(ri)(X)
                          << " Y = " << gnd_head_pt(ri)(Y) << std::endl;
            }
            std::cout << std::endl;*/

            // now the ring has to be connected with bridge sticks
            for(auto it = ring.begin(), next = std::next(it);
                next != ring.end();
                ++it, ++next)
            {
                thr();
                const Pillar& pillar = result.head_pillar(*it);
                const Pillar& nextpillar = result.head_pillar(*next);
                interconnect(pillar, nextpillar, emesh, result);
            }

            auto sring = ring; ClusterEl tmp;
            std::sort(sring.begin(), sring.end());
            std::set_difference(rem.begin(), rem.end(),
                                sring.begin(), sring.end(),
                                std::back_inserter(tmp));
            rem.swap(tmp);
        }

    };

    // Step: routing the pinheads that would connect to the model surface
    // along the Z axis downwards. For now these will actually be connected with
    // the model surface with a flipped pinhead. In the future here we could use
    // some smart algorithms to search for a safe path to the ground or to a
    // nearby pillar that can hold the supported weight.
    auto routing_nongnd_fn = [thr](
            const SupportConfig& cfg,
            const std::vector<double>& pt_heights,
            const PtIndices& nonground_head_indices,
            const EigenMesh3D& mesh,
            Result& result)
    {
        // TODO: connect these to the ground pillars if possible
        for(auto idx : nonground_head_indices) { thr();
            double gh = pt_heights[idx];
            double base_width = cfg.head_width_mm;

            auto& head = result.head(idx);

            if(std::isinf(gh)) { // in this case the the pillar geometry
                head.invalidate(); continue;
//                // is partially inside the model geometry. We cannot go
//                // straight down but at an angle. We will search for a suitable
//                // direction with the optimizer, optimizing for the smallest
//                // difference between the bridge body hit distance and the
//                // bridge center hit distance.

//                // Get the spherical representation of the normal. its easier to
//                // work with.
//                double z = head.dir(Z);
//                double r = 1.0;     // for normalized vector
//                double polar = std::acos(z / r);
//                double azimuth = std::atan2(head.dir(Y), head.dir(X));

//                using libnest2d::opt::bound;
//                using libnest2d::opt::initvals;
//                using libnest2d::opt::SimplexOptimizer;
//                using libnest2d::opt::StopCriteria;

//                StopCriteria stc;
//                stc.max_iterations = 100;
//                stc.relative_score_difference = 1e-3;
//                stc.stop_score = head.r_pin_mm;
//                SimplexOptimizer solver(stc);

//                auto oresult = solver.optimize_max(
//                            [&head, &mesh](double plr, double azm)
//                {
//                    auto n = Vec3d(std::cos(azm) * std::sin(plr),
//                                   std::sin(azm) * std::sin(plr),
//                                   std::cos(plr)).normalized();

//                    double score = bridge_mesh_intersect(head.junction_point(),
//                                                         n,
//                                                         head.r_back_mm,
//                                                         mesh);
//                    return score;
//                },
//                initvals(polar, azimuth),  // let's start with what we have
//                bound(3*PI/4, PI),  // Must not exceed the slope limit
//                bound(-PI, PI)      // azimuth can be a full range search
//                );

//                t = oresult.score;
//                polar = std::get<0>(oresult.optimum);
//                azimuth = std::get<1>(oresult.optimum);
//                nn = Vec3d(std::cos(azimuth) * std::sin(polar),
//                           std::sin(azimuth) * std::sin(polar),
//                           std::cos(polar)).normalized();

            }

            // In this case there is no room for the base pinhead.
            if(gh < head.fullwidth()) {
                double min_l =
                        2 * cfg.head_front_radius_mm +
                        2 * cfg.head_back_radius_mm - cfg.head_penetration_mm;

                base_width = gh - min_l;
            }

            if(base_width < 0) {
                // There is really no space for even a reduced size head. We
                // have to replace that with a small half sphere that touches
                // the model surface. (TODO)
                head.invalidate();
                continue;
            }

            head.transform();

            Vec3d headend = head.junction_point();

            Head base_head(cfg.head_back_radius_mm,
                 cfg.head_front_radius_mm,
                 base_width,
                 cfg.head_penetration_mm,
                 {0.0, 0.0, 1.0},
                 {headend(X), headend(Y), headend(Z) - gh});

            base_head.transform();

            // Robustness check:
            if(headend(Z) < base_head.junction_point()(Z)) {
                // This should not happen it is against all assumptions
                BOOST_LOG_TRIVIAL(warning)
                        << "Ignoring invalid supports connecting to model body";
                head.invalidate();
                continue;
            }

            double hl = base_head.fullwidth() - head.r_back_mm;

            result.add_pillar(idx,
                Vec3d{headend(X), headend(Y), headend(Z) - gh + hl},
                cfg.head_back_radius_mm
            ).base = base_head.mesh;
        }
    };

    // Step: process the support points where there is not enough space for a
    // full pinhead. In this case we will use a rounded sphere as a touching
    // point and use a thinner bridge (let's call it a stick).
    auto process_headless_fn = [thr](
            const std::vector<SupportPoint>& support_points,
            const PointSet& support_normals,
            const PtIndices& headless_indices,
            const EigenMesh3D& emesh,
            Result& result)
    {
        // For now we will just generate smaller headless sticks with a sharp
        // ending point that connects to the mesh surface.


        // We will sink the pins into the model surface for a distance of 1/3 of
        // the pin radius
        for(unsigned i : headless_indices) { thr();

            const auto R = double(support_points[i].head_front_radius);
            const double HWIDTH_MM = R/3;

            Vec3d sph = support_points[i].pos.cast<double>();    // Exact support position
            Vec3d n = support_normals.row(i);   // mesh outward normal
            Vec3d sp = sph - n * HWIDTH_MM;     // stick head start point

            Vec3d dir = {0, 0, -1};
            Vec3d sj = sp + R * n;              // stick start point

            // This is only for checking
            double idist = bridge_mesh_intersect(sph, dir, R, emesh, true);
            double dist = ray_mesh_intersect(sj, dir, emesh);

            if(std::isinf(idist) || std::isnan(idist) || idist < 2*R ||
               std::isinf(dist)  || std::isnan(dist)   || dist < 2*R) {
                BOOST_LOG_TRIVIAL(warning) << "Can not find route for headless"
                                           << " support stick at: "
                                           << sj.transpose();
                continue;
            }

            Vec3d ej = sj + (dist + HWIDTH_MM)* dir;
            result.add_compact_bridge(sp, ej, n, R);
        }
    };

    // Now that the individual blocks are defined, lets connect the wires. We
    // will create an array of functions which represents a program. Place the
    // step methods in the array and bind the right arguments to the methods
    // This way the data dependencies will be easily traceable between
    // individual steps.
    // There will be empty steps as well like the begin step or the done or
    // abort steps. These are slots for future initialization or cleanup.

    using std::cref;    // Bind inputs with cref (read-only)
    using std::ref;     // Bind outputs with ref (writable)
    using std::bind;

    // Here we can easily track what goes in and what comes out of each step:
    // (see the cref-s as inputs and ref-s as outputs)
    std::array<std::function<void()>, NUM_STEPS> program = {
        [] () {
            // Begin...
            // Potentially clear up the shared data (not needed for now)
        },

        bind(filter_fn,
             // inputs:
             cref(cfg), cref(points), cref(mesh),
             // outputs:
             ref(support_normals), ref(filtered_indices), ref(head_indices),
             ref(headless_indices)),

        bind(pinheads_fn,
             // inputs:
             cref(cfg), cref(support_points), cref(support_normals),
             cref(head_indices),
             // outputs:
             ref(result)),

        bind(classify_fn,
             // inputs:
             cref(cfg), cref(points), cref(mesh), cref(head_indices),
             // outputs:
             ref(onmodel_head_indices), ref(pt_heights), ref(pillar_clusters),
             ref(result)),

        bind(routing_ground_fn,
             // inputs:
             cref(cfg), cref(points), cref(pillar_clusters), cref(mesh),
             // outputs:
             ref(result)),

        bind(routing_nongnd_fn,
             // inputs:
             cref(cfg), cref(pt_heights), cref(onmodel_head_indices), cref(mesh),
             // outputs:
             ref(result)),

        bind(process_headless_fn,
             // inputs:
             cref(support_points), cref(support_normals),
             cref(headless_indices), cref(mesh),
             // outputs:
             ref(result)),

        [] () {
            // Done
        },

        [] () {
            // Halt
        },

        [] () {
            // Abort
        }
    };

    if(cfg.ground_facing_only) { // Delete the non-gnd steps if necessary
        program[ROUTING_NONGROUND] = []() {
            BOOST_LOG_TRIVIAL(info) << "Skipping non-ground facing supports as "
                                       "requested.";
        };
        program[HEADLESS]  = [](){
            BOOST_LOG_TRIVIAL(info) << "Skipping headless stick generation as "
                                       "requested";
        };
    }

    Steps pc = BEGIN, pc_prev = BEGIN;

    // Let's define a simple automaton that will run our program.
    auto progress = [&ctl, &pc, &pc_prev] () {
        static const std::array<std::string, NUM_STEPS> stepstr {
            "Starting",
            "Filtering",
            "Generate pinheads",
            "Classification",
            "Routing to ground",
            "Routing supports to model surface",
            "Processing small holes",
            "Done",
            "Halt",
            "Abort"
        };

        static const std::array<unsigned, NUM_STEPS> stepstate {
            0,
            10,
            30,
            50,
            60,
            70,
            80,
            100,
            0,
            0
        };

        if(ctl.stopcondition()) pc = ABORT;

        switch(pc) {
        case BEGIN: pc = FILTER; break;
        case FILTER: pc = PINHEADS; break;
        case PINHEADS: pc = CLASSIFY; break;
        case CLASSIFY: pc = ROUTING_GROUND; break;
        case ROUTING_GROUND: pc = ROUTING_NONGROUND; break;
        case ROUTING_NONGROUND: pc = HEADLESS; break;
        case HEADLESS: pc = DONE; break;
        case HALT: pc = pc_prev; break;
        case DONE:
        case ABORT: break;
        default: ;
        }
        ctl.statuscb(stepstate[pc], stepstr[pc]);
    };

    // Just here we run the computation...
    while(pc < DONE || pc == HALT) {
        progress();
        program[pc]();
    }

    return pc == ABORT;
}

SLASupportTree::SLASupportTree(): m_impl(new Impl()) {}

const TriangleMesh &SLASupportTree::merged_mesh() const
{
    return get().merged_mesh();
}

void SLASupportTree::merged_mesh_with_pad(TriangleMesh &outmesh) const {
    outmesh.merge(merged_mesh());
    outmesh.merge(get_pad());
}

SlicedSupports SLASupportTree::slice(float layerh, float init_layerh) const
{
    if(init_layerh < 0) init_layerh = layerh;
    auto& stree = get();

    const auto modelh = float(stree.full_height());
    auto gndlvl = float(this->m_impl->ground_level);
    const Pad& pad = m_impl->pad();
    if(!pad.empty()) gndlvl -= float(get_pad_elevation(pad.cfg));

    std::vector<float> heights;
    heights.reserve(size_t(modelh/layerh) + 1);

    for(float h = gndlvl + init_layerh; h < gndlvl + modelh; h += layerh) {
        heights.emplace_back(h);
    }

    TriangleMesh fullmesh = m_impl->merged_mesh();
    fullmesh.merge(get_pad());
    TriangleMeshSlicer slicer(&fullmesh);
    SlicedSupports ret;
    slicer.slice(heights, &ret, get().ctl().cancelfn);

    return ret;
}

const TriangleMesh &SLASupportTree::add_pad(const SliceLayer& baseplate,
                                            const PoolConfig& pcfg) const
{
//    PoolConfig pcfg;
//    pcfg.min_wall_thickness_mm = min_wall_thickness_mm;
//    pcfg.min_wall_height_mm    = min_wall_height_mm;
//    pcfg.max_merge_distance_mm = max_merge_distance_mm;
//    pcfg.edge_radius_mm        = edge_radius_mm;
    return m_impl->create_pad(merged_mesh(), baseplate, pcfg).tmesh;
}

const TriangleMesh &SLASupportTree::get_pad() const
{
    return m_impl->pad().tmesh;
}

void SLASupportTree::remove_pad()
{
    m_impl->remove_pad();
}

SLASupportTree::SLASupportTree(const std::vector<SupportPoint> &points,
                               const EigenMesh3D& emesh,
                               const SupportConfig &cfg,
                               const Controller &ctl):
    m_impl(new Impl(ctl))
{
    m_impl->ground_level = emesh.ground_level() - cfg.object_elevation_mm;
    generate(points, emesh, cfg, ctl);
}

SLASupportTree::SLASupportTree(const SLASupportTree &c):
    m_impl(new Impl(*c.m_impl)) {}

SLASupportTree &SLASupportTree::operator=(const SLASupportTree &c)
{
    m_impl = make_unique<Impl>(*c.m_impl);
    return *this;
}

SLASupportTree::~SLASupportTree() {}

}
}
