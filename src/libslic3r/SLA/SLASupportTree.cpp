/**
 * In this file we will implement the automatic SLA support tree generation.
 *
 */

#include <numeric>
#include "SLASupportTree.hpp"
#include "SLABoilerPlate.hpp"
#include "SLASpatIndex.hpp"
#include "SLABasePool.hpp"

#include <libslic3r/MTUtils.hpp>
#include <libslic3r/ClipperUtils.hpp>
#include <libslic3r/Model.hpp>

#include <libnest2d/optimizers/nlopt/genetic.hpp>
#include <libnest2d/optimizers/nlopt/subplex.hpp>
#include <boost/log/trivial.hpp>
#include <tbb/parallel_for.h>
#include <libslic3r/I18N.hpp>

//! macro used to mark string used at localization,
//! return same string
#define L(s) Slic3r::I18N::translate(s)

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

// Compile time configuration value definitions:

// The max Z angle for a normal at which it will get completely ignored.
const double SupportConfig::normal_cutoff_angle = 150.0 * M_PI / 180.0;

// The shortest distance of any support structure from the model surface
const double SupportConfig::safety_distance_mm = 0.5;

const double SupportConfig::max_solo_pillar_height_mm = 15.0;
const double SupportConfig::max_dual_pillar_height_mm = 35.0;
const double   SupportConfig::optimizer_rel_score_diff = 1e-6;
const unsigned SupportConfig::optimizer_max_iterations = 1000;
const unsigned SupportConfig::pillar_cascade_neighbors = 3;
const unsigned SupportConfig::max_bridges_on_pillar = 3;

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
    points.emplace_back(jp); int ci = int(points.size() - 1);
    for(int i = 0; i < steps - 1; ++i)
        indices.emplace_back(i + offs + 1, i + offs, ci);

    indices.emplace_back(offs, steps + offs - 1, ci);

    points.emplace_back(endp); ci = int(points.size() - 1);
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

    static double fullwidth(const SupportConfig& cfg) {
        return 2 * cfg.head_front_radius_mm + cfg.head_width_mm +
               2 * cfg.head_back_radius_mm - cfg.head_penetration_mm;
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
    Vec3d endpt;
    double height = 0;

    long id = -1;

    // If the pillar connects to a head, this is the id of that head
    bool starts_from_head = true; // Could start from a junction as well
    long start_junction_id = -1;

    // How many bridges are connected to this pillar
    unsigned bridges = 0;

    // How many pillars are cascaded with this one
    unsigned links = 0;

    Pillar(const Vec3d& jp, const Vec3d& endp,
           double radius = 1, size_t st = 45):
        r(radius), steps(st), endpt(endp), starts_from_head(false)
    {
        assert(steps > 0);

        height = jp(Z) - endp(Z);
        if(height > EPSILON) { // Endpoint is below the starting point

            // We just create a bridge geometry with the pillar parameters and
            // move the data.
            Contour3D body = cylinder(radius, height, st, endp);
            mesh.points.swap(body.points);
            mesh.indices.swap(body.indices);
        }
    }

    Pillar(const Junction& junc, const Vec3d& endp):
        Pillar(junc.pos, endp, junc.r, junc.steps){}

    Pillar(const Head& head, const Vec3d& endp, double radius = 1):
        Pillar(head.junction_point(), endp, head.request_pillar_radius(radius),
               head.steps)
    {
    }

    inline Vec3d startpoint() const {
        return {endpt(X), endpt(Y), endpt(Z) + height};
    }

    inline const Vec3d& endpoint() const { return endpt; }

    Pillar& add_base(double baseheight = 3, double radius = 2) {
        if(baseheight <= 0) return *this;
        if(baseheight > height) baseheight = height;

        assert(steps >= 0);
        auto last = int(steps - 1);

        if(radius < r ) radius = r;

        double a = 2*PI/steps;
        double z = endpt(Z) + baseheight;

        for(size_t i = 0; i < steps; ++i) {
            double phi = i*a;
            double x = endpt(X) + r*std::cos(phi);
            double y = endpt(Y) + r*std::sin(phi);
            base.points.emplace_back(x, y, z);
        }

        for(size_t i = 0; i < steps; ++i) {
            double phi = i*a;
            double x = endpt(X) + radius*std::cos(phi);
            double y = endpt(Y) + radius*std::sin(phi);
            base.points.emplace_back(x, y, z - baseheight);
        }

        auto ep = endpt; ep(Z) += baseheight;
        base.points.emplace_back(endpt);
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
        return *this;
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
                  bool endball = true,
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
        
        if(endball) {
            auto lowerball = sphere(r, Portion{0, PI/2 + 2*fa}, fa);
            for(auto& p : lowerball.points) p += endp;
            mesh.merge(lowerball);
        }
        
        mesh.merge(upperball);
    }
};

// A wrapper struct around the base pool (pad)
struct Pad {
    TriangleMesh tmesh;
    PoolConfig cfg;
    double zlevel = 0;

    Pad() = default;

    Pad(const TriangleMesh& support_mesh,
        const ExPolygons& modelbase,
        double ground_level,
        const PoolConfig& pcfg) :
        cfg(pcfg),
        zlevel(ground_level + 
               sla::get_pad_fullheight(pcfg) -
               sla::get_pad_elevation(pcfg))
    {
        Polygons basep;
        auto &thr = cfg.throw_on_cancel;
        
        thr();
        
        // Get a sample for the pad from the support mesh
        {
            ExPolygons platetmp;

            float zstart = float(zlevel);
            float zend   = zstart + float(get_pad_fullheight(pcfg) + EPSILON);

            base_plate(support_mesh, platetmp, grid(zstart, zend, 0.1f), thr);

            // We don't need no... holes control...
            for (const ExPolygon &bp : platetmp)
                basep.emplace_back(std::move(bp.contour));
        }
        
        if(pcfg.embed_object) {
            
            // If the zero elevation mode is ON, we need to process the model
            // base silhouette. Create the offsetted version and punch the
            // breaksticks across its perimeter.
            
            ExPolygons modelbase_offs = modelbase;
            
            if (pcfg.embed_object.object_gap_mm > 0.0)
                modelbase_offs
                    = offset_ex(modelbase_offs,
                                float(scaled(pcfg.embed_object.object_gap_mm)));
            
            // Create a spatial index of the support silhouette polygons.
            // This will be used to check for intersections with the model
            // silhouette polygons. If there is no intersection, then a certain
            // part of the pad is redundant as it does not host any supports.
            BoxIndex bindex;
            {
                unsigned idx = 0;
                for(auto &bp : basep) {
                    auto bb = bp.bounding_box();
                    bb.offset(float(scaled(pcfg.min_wall_thickness_mm)));
                    bindex.insert(bb, idx++);
                }
            }
            
            // Punching the breaksticks across the offsetted polygon perimeters
            ExPolygons pad_stickholes; pad_stickholes.reserve(modelbase.size());
            for(auto& poly : modelbase_offs) {
                
                std::vector<BoxIndexEl> qres =
                    bindex.query(poly.contour.bounding_box(),
                                 BoxIndex::qtIntersects);
                    
                if (!qres.empty()) {
                    
                    // The model silhouette polygon 'poly' HAS an intersection
                    // with the support silhouettes. Include this polygon
                    // in the pad holes with the breaksticks and merge the
                    // original (offsetted) version with the rest of the pad
                    // base plate.
                    
                    basep.emplace_back(poly.contour);
                    
                    // The holes of 'poly' will become positive parts of the
                    // pad, so they has to be checked for intersections as well
                    // and erased if there is no intersection with the supports
                    auto it = poly.holes.begin();
                    while(it != poly.holes.end()) {
                        if (bindex.query(it->bounding_box(),
                                         BoxIndex::qtIntersects).empty())
                            it = poly.holes.erase(it);
                        else
                            ++it;
                    }
                    
                    // Punch the breaksticks
                    sla::breakstick_holes(
                        poly,
                        pcfg.embed_object.object_gap_mm,   // padding
                        pcfg.embed_object.stick_stride_mm,
                        pcfg.embed_object.stick_width_mm,
                        pcfg.embed_object.stick_penetration_mm);
                    
                    pad_stickholes.emplace_back(poly);
                }
            }
            
            create_base_pool(basep, tmesh, pad_stickholes, cfg);
        } else {
            for (const ExPolygon &bp : modelbase) basep.emplace_back(bp.contour);
            create_base_pool(basep, tmesh, {}, cfg);
        }

        tmesh.translate(0, 0, float(zlevel));
    }

    bool empty() const { return tmesh.facets_count() == 0; }
};

// The minimum distance for two support points to remain valid.
static const double /*constexpr*/ D_SP   = 0.1;

enum { // For indexing Eigen vectors as v(X), v(Y), v(Z) instead of numbers
  X, Y, Z
};

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

bool operator==(const PointIndexEl& e1, const PointIndexEl& e2) {
    return e1.second == e2.second;
}

// Clustering a set of points by the given distance.
ClusteredPoints cluster(const std::vector<unsigned>& indices,
                        std::function<Vec3d(unsigned)> pointfn,
                        double dist,
                        unsigned max_points);

ClusteredPoints cluster(const PointSet& points,
                        double dist,
                        unsigned max_points);

ClusteredPoints cluster(
        const std::vector<unsigned>& indices,
        std::function<Vec3d(unsigned)> pointfn,
        std::function<bool(const PointIndexEl&, const PointIndexEl&)> predicate,
        unsigned max_points);

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

    void increment_bridges(const Pillar& pillar) {
        assert(pillar.id >= 0 && size_t(pillar.id) < m_pillars.size());

        if(pillar.id >= 0 && size_t(pillar.id) < m_pillars.size())
            m_pillars[size_t(pillar.id)].bridges++;
    }

    void increment_links(const Pillar& pillar) {
        assert(pillar.id >= 0 && size_t(pillar.id) < m_pillars.size());

        if(pillar.id >= 0 && size_t(pillar.id) < m_pillars.size())
            m_pillars[size_t(pillar.id)].links++;
    }

    template<class...Args> Pillar& add_pillar(Args&&...args)
    {
        m_pillars.emplace_back(std::forward<Args>(args)...);
        Pillar& pillar = m_pillars.back();
        pillar.id = long(m_pillars.size() - 1);
        pillar.starts_from_head = false;
        meshcache_valid = false;
        return m_pillars.back();
    }

    const Head& pillar_head(long pillar_id) const {
        assert(pillar_id >= 0 && pillar_id < long(m_pillars.size()));
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
        assert(h.pillar_id >= 0 && h.pillar_id < long(m_pillars.size()));
        return pillar(h.pillar_id);
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

    template<class T> inline const Pillar& pillar(T id) const {
        static_assert(std::is_integral<T>::value, "Invalid index type");
        assert(id >= 0 && size_t(id) < m_pillars.size() &&
               size_t(id) < std::numeric_limits<size_t>::max());
        return m_pillars[size_t(id)];
    }

    const Pad& create_pad(const TriangleMesh& object_supports,
                          const ExPolygons& modelbase,
                          const PoolConfig& cfg) {
        m_pad = Pad(object_supports, modelbase, ground_level, cfg);
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

        // The mesh will be passed by const-pointer to TriangleMeshSlicer,
        // which will need this.
        if (!meshcache.empty()) meshcache.require_shared_vertices();

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
    
    // Intended to be called after the generation is fully complete
    void clear_support_data() {
        merged_mesh(); // in case the mesh is not generated, it should be...
        m_heads.clear();
        m_pillars.clear();
        m_junctions.clear();
        m_bridges.clear();
        m_compact_bridges.clear();
    }

};

// This function returns the position of the centroid in the input 'clust'
// vector of point indices.
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

    // The complexity should be O(n^2) but we will mostly apply this function
    // for small clusters only (cca 3 elements)

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

inline Vec3d dirv(const Vec3d& startp, const Vec3d& endp) {
    return (endp - startp).normalized();
}

class SLASupportTree::Algorithm {
    const SupportConfig& m_cfg;
    const EigenMesh3D& m_mesh;
    const std::vector<SupportPoint>& m_support_pts;

    using PtIndices = std::vector<unsigned>;

    PtIndices m_iheads;            // support points with pinhead
    PtIndices m_iheadless;         // headless support points

    // supp. pts. connecting to model: point index and the ray hit data
    std::vector<std::pair<unsigned, EigenMesh3D::hit_result>> m_iheads_onmodel;

    // normals for support points from model faces.
    PointSet  m_support_nmls;

    // Clusters of points which can reach the ground directly and can be
    // bridged to one central pillar
    std::vector<PtIndices> m_pillar_clusters;

    // This algorithm uses the Impl class as its output stream. It will be
    // filled gradually with support elements (heads, pillars, bridges, ...)
    using Result = SLASupportTree::Impl;

    Result& m_result;

    // support points in Eigen/IGL format
    PointSet m_points;

    // throw if canceled: It will be called many times so a shorthand will
    // come in handy.
    ThrowOnCancel m_thr;

    // A spatial index to easily find strong pillars to connect to.
    PointIndex m_pillar_index;

    inline double ray_mesh_intersect(const Vec3d& s,
                                     const Vec3d& dir)
    {
        return m_mesh.query_ray_hit(s, dir).distance();
    }

    // This function will test if a future pinhead would not collide with the
    // model geometry. It does not take a 'Head' object because those are
    // created after this test. Parameters: s: The touching point on the model
    // surface. dir: This is the direction of the head from the pin to the back
    // r_pin, r_back: the radiuses of the pin and the back sphere width: This
    // is the full width from the pin center to the back center m: The object
    // mesh.
    // The return value is the hit result from the ray casting. If the starting
    // point was inside the model, an "invalid" hit_result will be returned
    // with a zero distance value instead of a NAN. This way the result can
    // be used safely for comparison with other distances.
    EigenMesh3D::hit_result pinhead_mesh_intersect(
            const Vec3d& s,
            const Vec3d& dir,
            double r_pin,
            double r_back,
            double width)
    {
        static const size_t SAMPLES = 8;

        // method based on:
        // https://math.stackexchange.com/questions/73237/parametric-equation-of-a-circle-in-3d-space

        // We will shoot multiple rays from the head pinpoint in the direction
        // of the pinhead robe (side) surface. The result will be the smallest
        // hit distance.

        // Move away slightly from the touching point to avoid raycasting on the
        // inner surface of the mesh.
        Vec3d v = dir;     // Our direction (axis)
        Vec3d c = s + width * dir;
        const double& sd = m_cfg.safety_distance_mm;

        // Two vectors that will be perpendicular to each other and to the
        // axis. Values for a(X) and a(Y) are now arbitrary, a(Z) is just a
        // placeholder.
        Vec3d a(0, 1, 0), b;

        // The portions of the circle (the head-back circle) for which we will
        // shoot rays.
        std::array<double, SAMPLES> phis;
        for(size_t i = 0; i < phis.size(); ++i) phis[i] = i*2*PI/phis.size();

        auto& m = m_mesh;
        using HitResult = EigenMesh3D::hit_result;

        // Hit results
        std::array<HitResult, SAMPLES> hits;

        // We have to address the case when the direction vector v (same as
        // dir) is coincident with one of the world axes. In this case two of
        // its components will be completely zero and one is 1.0. Our method
        // becomes dangerous here due to division with zero. Instead, vector
        // 'a' can be an element-wise rotated version of 'v'
        auto chk1 = [] (double val) {
            return std::abs(std::abs(val) - 1) < 1e-20;
        };

        if(chk1(v(X)) || chk1(v(Y)) || chk1(v(Z))) {
            a = {v(Z), v(X), v(Y)};
            b = {v(Y), v(Z), v(X)};
        }
        else {
            a(Z) = -(v(Y)*a(Y)) / v(Z); a.normalize();
            b = a.cross(v);
        }

        // Now a and b vectors are perpendicular to v and to each other.
        // Together they define the plane where we have to iterate with the
        // given angles in the 'phis' vector
        tbb::parallel_for(size_t(0), phis.size(),
                          [&phis, &hits, &m, sd, r_pin, r_back, s, a, b, c]
                          (size_t i)
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

            // Point ps is not on mesh but can be inside or outside as well.
            // This would cause many problems with ray-casting. To detect the
            // position we will use the ray-casting result (which has an
            // is_inside predicate).

            // This is the point on the circle on the back sphere
            Vec3d p(c(X) + rpbcos * a(X) + rpbsin * b(X),
                    c(Y) + rpbcos * a(Y) + rpbsin * b(Y),
                    c(Z) + rpbcos * a(Z) + rpbsin * b(Z));

            Vec3d n = (p - ps).normalized();
            auto q = m.query_ray_hit(ps + sd*n, n);

            if(q.is_inside()) { // the hit is inside the model
                if(q.distance() > r_pin + sd)  {
                    // If we are inside the model and the hit distance is bigger
                    // than our pin circle diameter, it probably indicates that
                    // the support point was already inside the model, or there
                    // is really no space around the point. We will assign a
                    // zero hit distance to these cases which will enforce the
                    // function return value to be an invalid ray with zero hit
                    // distance. (see min_element at the end)
                    hits[i] = HitResult(0.0);
                }
                else {
                    // re-cast the ray from the outside of the object.
                    // The starting point has an offset of 2*safety_distance
                    // because the original ray has also had an offset
                    auto q2 = m.query_ray_hit(ps + (q.distance() + 2*sd)*n, n);
                    hits[i] = q2;
                }
            } else hits[i] = q;
        });

        auto mit = std::min_element(hits.begin(), hits.end());

        return *mit;
    }

    // Checking bridge (pillar and stick as well) intersection with the model.
    // If the function is used for headless sticks, the ins_check parameter
    // have to be true as the beginning of the stick might be inside the model
    // geometry.
    // The return value is the hit result from the ray casting. If the starting
    // point was inside the model, an "invalid" hit_result will be returned
    // with a zero distance value instead of a NAN. This way the result can
    // be used safely for comparison with other distances.
    EigenMesh3D::hit_result bridge_mesh_intersect(
            const Vec3d& s,
            const Vec3d& dir,
            double r,
            bool ins_check = false)
    {
        static const size_t SAMPLES = 8;

        // helper vector calculations
        Vec3d a(0, 1, 0), b;
        const double& sd = m_cfg.safety_distance_mm;

        // INFO: for explanation of the method used here, see the previous
        // method's comments.

        auto chk1 = [] (double val) {
            return std::abs(std::abs(val) - 1) < 1e-20;
        };

        if(chk1(dir(X)) || chk1(dir(Y)) || chk1(dir(Z))) {
            a = {dir(Z), dir(X), dir(Y)};
            b = {dir(Y), dir(Z), dir(X)};
        }
        else {
            a(Z) = -(dir(Y)*a(Y)) / dir(Z); a.normalize();
            b = a.cross(dir);
        }

        // circle portions
        std::array<double, SAMPLES> phis;
        for(size_t i = 0; i < phis.size(); ++i) phis[i] = i*2*PI/phis.size();

        auto& m = m_mesh;
        using HitResult = EigenMesh3D::hit_result;

        // Hit results
        std::array<HitResult, SAMPLES> hits;

        tbb::parallel_for(size_t(0), phis.size(),
                          [&m, &phis, a, b, sd, dir, r, s, ins_check, &hits]
                          (size_t i)
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
                if(hr.distance() > 2 * r + sd) hits[i] = HitResult(0.0);
                else {
                    // re-cast the ray from the outside of the object
                    auto hr2 =
                           m.query_ray_hit(p + (hr.distance() + 2*sd)*dir, dir);

                    hits[i] = hr2;
                }
            } else hits[i] = hr;
        });

        auto mit = std::min_element(hits.begin(), hits.end());

        return *mit;
    }

    // Helper function for interconnecting two pillars with zig-zag bridges.
    bool interconnect(const Pillar& pillar, const Pillar& nextpillar)
    {
        // We need to get the starting point of the zig-zag pattern. We have to
        // be aware that the two head junctions are at different heights. We
        // may start from the lowest junction and call it a day but this
        // strategy would leave unconnected a lot of pillar duos where the
        // shorter pillar is too short to start a new bridge but the taller
        // pillar could still be bridged with the shorter one.
        bool was_connected = false;

        Vec3d supper = pillar.startpoint();
        Vec3d slower = nextpillar.startpoint();
        Vec3d eupper = pillar.endpoint();
        Vec3d elower = nextpillar.endpoint();

        double zmin = m_result.ground_level + m_cfg.base_height_mm;
        eupper(Z) = std::max(eupper(Z), zmin);
        elower(Z) = std::max(elower(Z), zmin);

        // The usable length of both pillars should be positive
        if(slower(Z) - elower(Z) < 0) return false;
        if(supper(Z) - eupper(Z) < 0) return false;

        double pillar_dist = distance(Vec2d{slower(X), slower(Y)},
                                      Vec2d{supper(X), supper(Y)});
        double bridge_distance = pillar_dist / std::cos(-m_cfg.bridge_slope);
        double zstep = pillar_dist * std::tan(-m_cfg.bridge_slope);

        if(pillar_dist < 2 * m_cfg.head_back_radius_mm ||
           pillar_dist > m_cfg.max_pillar_link_distance_mm) return false;

        if(supper(Z) < slower(Z)) supper.swap(slower);
        if(eupper(Z) < elower(Z)) eupper.swap(elower);

        double startz = 0, endz = 0;

        startz = slower(Z) - zstep < supper(Z) ? slower(Z) - zstep : slower(Z);
        endz = eupper(Z) + zstep > elower(Z) ? eupper(Z) + zstep : eupper(Z);

        if(slower(Z) - eupper(Z) < std::abs(zstep)) {
            // no space for even one cross

            // Get max available space
            startz = std::min(supper(Z), slower(Z) - zstep);
            endz = std::max(eupper(Z) + zstep, elower(Z));

            // Align to center
            double available_dist = (startz - endz);
            double rounds = std::floor(available_dist / std::abs(zstep));
            startz -= 0.5 * (available_dist - rounds * std::abs(zstep));;
        }

        auto pcm = m_cfg.pillar_connection_mode;
        bool docrosses =
                pcm == PillarConnectionMode::cross ||
                (pcm == PillarConnectionMode::dynamic &&
                 pillar_dist > 2*m_cfg.base_radius_mm);

        // 'sj' means starting junction, 'ej' is the end junction of a bridge.
        // They will be swapped in every iteration thus the zig-zag pattern.
        // According to a config parameter, a second bridge may be added which
        // results in a cross connection between the pillars.
        Vec3d sj = supper, ej = slower; sj(Z) = startz; ej(Z) = sj(Z) + zstep;

        // TODO: This is a workaround to not have a faulty last bridge
        while(ej(Z) >= eupper(Z) /*endz*/) {
            if(bridge_mesh_intersect(sj,
                                     dirv(sj, ej),
                                     pillar.r) >= bridge_distance)
            {
                m_result.add_bridge(sj, ej, pillar.r);
                was_connected = true;
            }

            // double bridging: (crosses)
            if(docrosses) {
                Vec3d sjback(ej(X), ej(Y), sj(Z));
                Vec3d ejback(sj(X), sj(Y), ej(Z));
                if(sjback(Z) <= slower(Z) && ejback(Z) >= eupper(Z) &&
                   bridge_mesh_intersect(sjback,
                                         dirv(sjback, ejback),
                                         pillar.r) >= bridge_distance)
                {
                    // need to check collision for the cross stick
                    m_result.add_bridge(sjback, ejback, pillar.r);
                    was_connected = true;
                }
            }

            sj.swap(ej);
            ej(Z) = sj(Z) + zstep;
        }

        return was_connected;
    }

    // For connecting a head to a nearby pillar.
    bool connect_to_nearpillar(const Head& head, long nearpillar_id) {
        
        auto nearpillar = [this, nearpillar_id]() {
            return m_result.pillar(nearpillar_id);
        };
        
        if (nearpillar().bridges > m_cfg.max_bridges_on_pillar) return false;

        Vec3d headjp = head.junction_point();
        Vec3d nearjp_u = nearpillar().startpoint();
        Vec3d nearjp_l = nearpillar().endpoint();

        double r = head.r_back_mm;
        double d2d = distance(to_2d(headjp), to_2d(nearjp_u));
        double d3d = distance(headjp, nearjp_u);

        double hdiff = nearjp_u(Z) - headjp(Z);
        double slope = std::atan2(hdiff, d2d);

        Vec3d bridgestart = headjp;
        Vec3d bridgeend = nearjp_u;
        double max_len = m_cfg.max_bridge_length_mm;
        double max_slope = m_cfg.bridge_slope;
        double zdiff = 0.0;

        // check the default situation if feasible for a bridge
        if(d3d > max_len || slope > -max_slope) {
            // not feasible to connect the two head junctions. We have to search
            // for a suitable touch point.

            double Zdown = headjp(Z) + d2d * std::tan(-max_slope);
            Vec3d touchjp = bridgeend; touchjp(Z) = Zdown;
            double D = distance(headjp, touchjp);
            zdiff = Zdown - nearjp_u(Z);

            if(zdiff > 0) {
                Zdown -= zdiff;
                bridgestart(Z) -= zdiff;
                touchjp(Z) = Zdown;

                double t = bridge_mesh_intersect(headjp, {0,0,-1}, r);

                // We can't insert a pillar under the source head to connect
                // with the nearby pillar's starting junction
                if(t < zdiff) return false;
            }

            if(Zdown <= nearjp_u(Z) && Zdown >= nearjp_l(Z) && D < max_len)
                bridgeend(Z) = Zdown;
            else
                return false;
        }

        // There will be a minimum distance from the ground where the
        // bridge is allowed to connect. This is an empiric value.
        double minz = m_result.ground_level + 2 * m_cfg.head_width_mm;
        if(bridgeend(Z) < minz) return false;

        double t = bridge_mesh_intersect(bridgestart,
                                         dirv(bridgestart, bridgeend), r);

        // Cannot insert the bridge. (further search might not worth the hassle)
        if(t < distance(bridgestart, bridgeend)) return false;

        // A partial pillar is needed under the starting head.
        if(zdiff > 0) {
            m_result.add_pillar(unsigned(head.id), bridgestart, r);
            m_result.add_junction(bridgestart, r);
        }

        m_result.add_bridge(bridgestart, bridgeend, r);
        m_result.increment_bridges(nearpillar());

        return true;
    }

    bool search_pillar_and_connect(const Head& head) {
        PointIndex spindex = m_pillar_index;

        long nearest_id = -1;

        Vec3d querypoint = head.junction_point();

        while(nearest_id < 0 && !spindex.empty()) { m_thr();
            // loop until a suitable head is not found
            // if there is a pillar closer than the cluster center
            // (this may happen as the clustering is not perfect)
            // than we will bridge to this closer pillar

            Vec3d qp(querypoint(X), querypoint(Y), m_result.ground_level);
            auto qres = spindex.nearest(qp, 1);
            if(qres.empty()) break;

            auto ne = qres.front();
            nearest_id = ne.second;

            if(nearest_id >= 0) {
                auto nearpillarID = unsigned(nearest_id);
                if(nearpillarID < m_result.pillars().size()) {
                    if(!connect_to_nearpillar(head, nearpillarID)) {
                        nearest_id = -1;    // continue searching
                        spindex.remove(ne); // without the current pillar
                    }
                }
            }
        }

        return nearest_id >= 0;
    }
    
    // This is a proxy function for pillar creation which will mind the gap
    // between the pad and the model bottom in zero elevation mode.
    void create_ground_pillar(const Vec3d &jp,
                              const Vec3d &sourcedir,
                              double       radius,
                              int          head_id = -1)
    {
        // People were killed for this number (seriously)
        static const double SQR2 = std::sqrt(2.0);
        static const Vec3d  DOWN = {0.0, 0.0, -1.0};

        double gndlvl       = m_result.ground_level;
        Vec3d  endp         = {jp(X), jp(Y), gndlvl};
        double sd           = m_cfg.pillar_base_safety_distance_mm;
        int    pillar_id    = -1;
        double min_dist     = sd + m_cfg.base_radius_mm + EPSILON;
        double dist         = 0;
        bool   can_add_base = true;
        bool   normal_mode  = true;

        if (m_cfg.object_elevation_mm < EPSILON
            && (dist = std::sqrt(m_mesh.squared_distance(endp))) < min_dist) {
            // Get the distance from the mesh. This can be later optimized
            // to get the distance in 2D plane because we are dealing with
            // the ground level only.

            normal_mode     = false;
            double mv       = min_dist - dist;
            double azimuth  = std::atan2(sourcedir(Y), sourcedir(X));
            double sinpolar = std::sin(PI - m_cfg.bridge_slope);
            double cospolar = std::cos(PI - m_cfg.bridge_slope);
            double cosazm   = std::cos(azimuth);
            double sinazm   = std::sin(azimuth);

            auto dir = Vec3d(cosazm * sinpolar, sinazm * sinpolar, cospolar)
                           .normalized();

            using namespace libnest2d::opt;
            StopCriteria scr;
            scr.stop_score = min_dist;
            SubplexOptimizer solver(scr);

            auto result = solver.optimize_max(
                [this, dir, jp, gndlvl](double mv) {
                    Vec3d endp = jp + SQR2 * mv * dir;
                    endp(Z)    = gndlvl;
                    return std::sqrt(m_mesh.squared_distance(endp));
                },
                initvals(mv), bound(0.0, 2 * min_dist));

            mv           = std::get<0>(result.optimum);
            endp         = jp + SQR2 * mv * dir;
            Vec3d pgnd   = {endp(X), endp(Y), gndlvl};
            can_add_base = result.score > min_dist;
            
            double gnd_offs = m_mesh.ground_level_offset();
            auto abort_in_shame =
                [gnd_offs, &normal_mode, &can_add_base, &endp, jp, gndlvl]()
            {
                normal_mode  = true;
                can_add_base = false;   // Nothing left to do, hope for the best
                endp         = {jp(X), jp(Y), gndlvl - gnd_offs };
            };

            // We have to check if the bridge is feasible.
            if (bridge_mesh_intersect(jp, dir, radius) < (endp - jp).norm())
                abort_in_shame();
            else {
                // If the new endpoint is below ground, do not make a pillar
                if (endp(Z) < gndlvl)
                    endp = endp - SQR2 * (gndlvl - endp(Z)) * dir; // back off
                else {
                    
                    auto hit = bridge_mesh_intersect(endp, DOWN, radius);
                    if (!std::isinf(hit.distance())) abort_in_shame();

                    Pillar &plr = m_result.add_pillar(endp, pgnd, radius);

                    if (can_add_base)
                        plr.add_base(m_cfg.base_height_mm,
                                     m_cfg.base_radius_mm);

                    pillar_id = plr.id;
                }

                m_result.add_bridge(jp, endp, radius);
                m_result.add_junction(endp, radius);

                // Add a degenerated pillar and the bridge.
                // The degenerate pillar will have zero length and it will
                // prevent from queries of head_pillar() to have non-existing
                // pillar when the head should have one.
                if (head_id >= 0)
                    m_result.add_pillar(unsigned(head_id), jp, radius);
            }
        }
        
        if (normal_mode) {
            Pillar &plr = head_id >= 0
                              ? m_result.add_pillar(unsigned(head_id),
                                                    endp,
                                                    radius)
                              : m_result.add_pillar(jp, endp, radius);

            if (can_add_base)
                plr.add_base(m_cfg.base_height_mm, m_cfg.base_radius_mm);

            pillar_id = plr.id;
        } 
            
        if(pillar_id >= 0) // Save the pillar endpoint in the spatial index
            m_pillar_index.insert(endp, pillar_id);
    }

public:

    Algorithm(const SupportConfig& config,
              const EigenMesh3D& emesh,
              const std::vector<SupportPoint>& support_pts,
              Result& result,
              ThrowOnCancel thr) :
        m_cfg(config),
        m_mesh(emesh),
        m_support_pts(support_pts),
        m_support_nmls(support_pts.size(), 3),
        m_result(result),
        m_points(support_pts.size(), 3),
        m_thr(thr)
    {
        // Prepare the support points in Eigen/IGL format as well, we will use
        // it mostly in this form.

        long i = 0;
        for(const SupportPoint& sp : m_support_pts) {
            m_points.row(i)(X) = double(sp.pos(X));
            m_points.row(i)(Y) = double(sp.pos(Y));
            m_points.row(i)(Z) = double(sp.pos(Z));
            ++i;
        }
    }


    // Now let's define the individual steps of the support generation algorithm

    // Filtering step: here we will discard inappropriate support points
    // and decide the future of the appropriate ones. We will check if a
    // pinhead is applicable and adjust its angle at each support point. We
    // will also merge the support points that are just too close and can
    // be considered as one.
    void filter() {
        // Get the points that are too close to each other and keep only the
        // first one
        auto aliases = cluster(m_points, D_SP, 2);

        PtIndices filtered_indices;
        filtered_indices.reserve(aliases.size());
        m_iheads.reserve(aliases.size());
        m_iheadless.reserve(aliases.size());
        for(auto& a : aliases) {
            // Here we keep only the front point of the cluster.
            filtered_indices.emplace_back(a.front());
        }

        // calculate the normals to the triangles for filtered points
        auto nmls = sla::normals(m_points, m_mesh, m_cfg.head_front_radius_mm,
                                 m_thr, filtered_indices);

        // Not all of the support points have to be a valid position for
        // support creation. The angle may be inappropriate or there may
        // not be enough space for the pinhead. Filtering is applied for
        // these reasons.

        using libnest2d::opt::bound;
        using libnest2d::opt::initvals;
        using libnest2d::opt::GeneticOptimizer;
        using libnest2d::opt::StopCriteria;

        for(unsigned i = 0, fidx = 0; i < filtered_indices.size(); ++i)
        {
            m_thr();

            fidx = filtered_indices[i];
            auto n = nmls.row(i);

            // for all normals we generate the spherical coordinates and
            // saturate the polar angle to 45 degrees from the bottom then
            // convert back to standard coordinates to get the new normal.
            // Then we just create a quaternion from the two normals
            // (Quaternion::FromTwoVectors) and apply the rotation to the
            // arrow head.

            double z       = n(2);
            double r       = 1.0; // for normalized vector
            double polar   = std::acos(z / r);
            double azimuth = std::atan2(n(1), n(0));

            // skip if the tilt is not sane
            if(polar >= PI - m_cfg.normal_cutoff_angle) {

                // We saturate the polar angle to 3pi/4
                polar = std::max(polar, 3*PI / 4);

                // save the head (pinpoint) position
                Vec3d hp = m_points.row(fidx);

                double w = m_cfg.head_width_mm +
                           m_cfg.head_back_radius_mm +
                           2*m_cfg.head_front_radius_mm;

                double pin_r = double(m_support_pts[fidx].head_front_radius);

                // Reassemble the now corrected normal
                auto nn = Vec3d(std::cos(azimuth) * std::sin(polar),
                                std::sin(azimuth) * std::sin(polar),
                                std::cos(polar)).normalized();

                // check available distance
                EigenMesh3D::hit_result t
                    = pinhead_mesh_intersect(hp, // touching point
                                             nn, // normal
                                             pin_r,
                                             m_cfg.head_back_radius_mm,
                                             w);

                if(t.distance() <= w) {

                    // Let's try to optimize this angle, there might be a
                    // viable normal that doesn't collide with the model
                    // geometry and its very close to the default.

                    StopCriteria stc;
                    stc.max_iterations = m_cfg.optimizer_max_iterations;
                    stc.relative_score_difference = m_cfg.optimizer_rel_score_diff;
                    stc.stop_score = w; // space greater than w is enough
                    GeneticOptimizer solver(stc);
                    solver.seed(0); // we want deterministic behavior

                    auto oresult = solver.optimize_max(
                        [this, pin_r, w, hp](double plr, double azm)
                    {
                        auto n = Vec3d(std::cos(azm) * std::sin(plr),
                                       std::sin(azm) * std::sin(plr),
                                       std::cos(plr)).normalized();

                        double score = pinhead_mesh_intersect( hp, n, pin_r,
                                          m_cfg.head_back_radius_mm, w);

                        return score;
                    },
                    initvals(polar, azimuth), // start with what we have
                    bound(3*PI/4, PI),  // Must not exceed the tilt limit
                    bound(-PI, PI)      // azimuth can be a full search
                    );

                    if(oresult.score > w) {
                        polar = std::get<0>(oresult.optimum);
                        azimuth = std::get<1>(oresult.optimum);
                        nn = Vec3d(std::cos(azimuth) * std::sin(polar),
                                   std::sin(azimuth) * std::sin(polar),
                                   std::cos(polar)).normalized();
                        t = oresult.score;
                    }
                }

                // save the verified and corrected normal
                m_support_nmls.row(fidx) = nn;

                if (t.distance() > w) {
                    // Check distance from ground, we might have zero elevation.
                    if (hp(Z) + w * nn(Z) < m_result.ground_level) {
                        m_iheadless.emplace_back(fidx);
                    } else {
                        // mark the point for needing a head.
                        m_iheads.emplace_back(fidx);
                    }
                } else if (polar >= 3 * PI / 4) {
                    // Headless supports do not tilt like the headed ones
                    // so the normal should point almost to the ground.
                    m_iheadless.emplace_back(fidx);
                }
            }
        }

        m_thr();
    }

    // Pinhead creation: based on the filtering results, the Head objects
    // will be constructed (together with their triangle meshes).
    void add_pinheads()
    {
        for (unsigned i : m_iheads) {
            m_thr();
            m_result.add_head(
                        i,
                        m_cfg.head_back_radius_mm,
                        m_support_pts[i].head_front_radius,
                        m_cfg.head_width_mm,
                        m_cfg.head_penetration_mm,
                        m_support_nmls.row(i),         // dir
                        m_support_pts[i].pos.cast<double>() // displacement
                        );
        }
    }

    // Further classification of the support points with pinheads. If the
    // ground is directly reachable through a vertical line parallel to the
    // Z axis we consider a support point as pillar candidate. If touches
    // the model geometry, it will be marked as non-ground facing and
    // further steps will process it. Also, the pillars will be grouped
    // into clusters that can be interconnected with bridges. Elements of
    // these groups may or may not be interconnected. Here we only run the
    // clustering algorithm.
    void classify()
    {
        // We should first get the heads that reach the ground directly
        PtIndices ground_head_indices;
        ground_head_indices.reserve(m_iheads.size());
        m_iheads_onmodel.reserve(m_iheads.size());

        // First we decide which heads reach the ground and can be full
        // pillars and which shall be connected to the model surface (or
        // search a suitable path around the surface that leads to the
        // ground -- TODO)
        for(unsigned i : m_iheads) {
            m_thr();

            auto& head = m_result.head(i);
            Vec3d n(0, 0, -1);
            double r = head.r_back_mm;
            Vec3d headjp = head.junction_point();

            // collision check
            auto hit = bridge_mesh_intersect(headjp, n, r);

            if(std::isinf(hit.distance())) ground_head_indices.emplace_back(i);
            else if(m_cfg.ground_facing_only)  head.invalidate();
            else m_iheads_onmodel.emplace_back(std::make_pair(i, hit));
        }

        // We want to search for clusters of points that are far enough
        // from each other in the XY plane to not cross their pillar bases
        // These clusters of support points will join in one pillar,
        // possibly in their centroid support point.
        
        auto pointfn = [this](unsigned i) {
            return m_result.head(i).junction_point();
        };

        auto predicate = [this](const PointIndexEl &e1,
                                const PointIndexEl &e2) {
            double d2d = distance(to_2d(e1.first), to_2d(e2.first));
            double d3d = distance(e1.first, e2.first);
            return d2d < 2 * m_cfg.base_radius_mm
                   && d3d < m_cfg.max_bridge_length_mm;
        };

        m_pillar_clusters = cluster(ground_head_indices,
                                    pointfn,
                                    predicate,
                                    m_cfg.max_bridges_on_pillar);
    }

    // Step: Routing the ground connected pinheads, and interconnecting
    // them with additional (angled) bridges. Not all of these pinheads
    // will be a full pillar (ground connected). Some will connect to a
    // nearby pillar using a bridge. The max number of such side-heads for
    // a central pillar is limited to avoid bad weight distribution.
    void routing_to_ground()
    {
        const double pradius = m_cfg.head_back_radius_mm;
        // const double gndlvl = m_result.ground_level;

        ClusterEl cl_centroids;
        cl_centroids.reserve(m_pillar_clusters.size());

        for(auto& cl : m_pillar_clusters) { m_thr();
            // place all the centroid head positions into the index. We
            // will query for alternative pillar positions. If a sidehead
            // cannot connect to the cluster centroid, we have to search
            // for another head with a full pillar. Also when there are two
            // elements in the cluster, the centroid is arbitrary and the
            // sidehead is allowed to connect to a nearby pillar to
            // increase structural stability.

            if(cl.empty()) continue;

            // get the current cluster centroid
            auto& thr = m_thr; const auto& points = m_points;
            long lcid = cluster_centroid(cl,
                [&points](size_t idx) { return points.row(long(idx)); },
                [thr](const Vec3d& p1, const Vec3d& p2)
            {
                thr();
                return distance(Vec2d(p1(X), p1(Y)), Vec2d(p2(X), p2(Y)));
            });

            assert(lcid >= 0);
            unsigned hid = cl[size_t(lcid)]; // Head ID

            cl_centroids.emplace_back(hid);

            Head& h = m_result.head(hid);
            h.transform();

            create_ground_pillar(h.junction_point(), h.dir, h.r_back_mm, h.id);
        }

        // now we will go through the clusters ones again and connect the
        // sidepoints with the cluster centroid (which is a ground pillar)
        // or a nearby pillar if the centroid is unreachable.
        size_t ci = 0;
        for(auto cl : m_pillar_clusters) { m_thr();

            auto cidx = cl_centroids[ci++];

            // TODO: don't consider the cluster centroid but calculate a
            // central position where the pillar can be placed. this way
            // the weight is distributed more effectively on the pillar.

            auto centerpillarID = m_result.head_pillar(cidx).id;

            for(auto c : cl) { m_thr();
                if(c == cidx) continue;

                auto& sidehead = m_result.head(c);
                sidehead.transform();

                if(!connect_to_nearpillar(sidehead, centerpillarID) &&
                   !search_pillar_and_connect(sidehead))
                {
                    Vec3d pstart = sidehead.junction_point();
                    //Vec3d pend = Vec3d{pstart(X), pstart(Y), gndlvl};
                    // Could not find a pillar, create one
                    create_ground_pillar(pstart,
                                         sidehead.dir,
                                         pradius,
                                         sidehead.id);
                }
            }
        }
    }

    // Step: routing the pinheads that would connect to the model surface
    // along the Z axis downwards. For now these will actually be connected with
    // the model surface with a flipped pinhead. In the future here we could use
    // some smart algorithms to search for a safe path to the ground or to a
    // nearby pillar that can hold the supported weight.
    void routing_to_model()
    {

        // We need to check if there is an easy way out to the bed surface.
        // If it can be routed there with a bridge shorter than
        // min_bridge_distance.

        // First we want to index the available pillars. The best is to connect
        // these points to the available pillars

        auto routedown = [this](Head& head, const Vec3d& dir, double dist)
        {
            head.transform();
            Vec3d hjp = head.junction_point();
            Vec3d endp = hjp + dist * dir;
            m_result.add_bridge(hjp, endp, head.r_back_mm);
            m_result.add_junction(endp, head.r_back_mm);

            this->create_ground_pillar(endp, dir, head.r_back_mm);
        };

        std::vector<unsigned> modelpillars;

        // TODO: connect these to the ground pillars if possible
        for(auto item : m_iheads_onmodel) { m_thr();
            unsigned idx = item.first;
            EigenMesh3D::hit_result hit = item.second;

            auto& head = m_result.head(idx);
            Vec3d hjp = head.junction_point();

            // /////////////////////////////////////////////////////////////////
            // Search nearby pillar
            // /////////////////////////////////////////////////////////////////

            if(search_pillar_and_connect(head)) { head.transform(); continue; }

            // /////////////////////////////////////////////////////////////////
            // Try straight path
            // /////////////////////////////////////////////////////////////////

            // Cannot connect to nearby pillar. We will try to search for
            // a route to the ground.

            double t = bridge_mesh_intersect(hjp, head.dir, head.r_back_mm);
            double d = 0, tdown = 0;
            Vec3d dirdown(0.0, 0.0, -1.0);

            t = std::min(t, m_cfg.max_bridge_length_mm);

            while(d < t && !std::isinf(tdown = bridge_mesh_intersect(
                                           hjp + d*head.dir,
                                           dirdown, head.r_back_mm))) {
                d += head.r_back_mm;
            }

            if(std::isinf(tdown)) { // we heave found a route to the ground
                routedown(head, head.dir, d); continue;
            }

            // /////////////////////////////////////////////////////////////////
            // Optimize bridge direction
            // /////////////////////////////////////////////////////////////////

            // Straight path failed so we will try to search for a suitable
            // direction out of the cavity.

            // Get the spherical representation of the normal. its easier to
            // work with.
            double z = head.dir(Z);
            double r = 1.0;     // for normalized vector
            double polar = std::acos(z / r);
            double azimuth = std::atan2(head.dir(Y), head.dir(X));

            using libnest2d::opt::bound;
            using libnest2d::opt::initvals;
            using libnest2d::opt::GeneticOptimizer;
            using libnest2d::opt::StopCriteria;

            StopCriteria stc;
            stc.max_iterations = m_cfg.optimizer_max_iterations;
            stc.relative_score_difference = m_cfg.optimizer_rel_score_diff;
            stc.stop_score = 1e6;
            GeneticOptimizer solver(stc);
            solver.seed(0); // we want deterministic behavior

            double r_back = head.r_back_mm;

            auto oresult = solver.optimize_max(
                        [this, hjp, r_back](double plr, double azm)
            {
                Vec3d n = Vec3d(std::cos(azm) * std::sin(plr),
                               std::sin(azm) * std::sin(plr),
                               std::cos(plr)).normalized();
                return bridge_mesh_intersect(hjp, n, r_back);
            },
            initvals(polar, azimuth),  // let's start with what we have
            bound(3*PI/4, PI),  // Must not exceed the slope limit
            bound(-PI, PI)      // azimuth can be a full range search
            );

            d = 0; t = oresult.score;

            polar = std::get<0>(oresult.optimum);
            azimuth = std::get<1>(oresult.optimum);
            Vec3d bridgedir = Vec3d(std::cos(azimuth) * std::sin(polar),
                              std::sin(azimuth) * std::sin(polar),
                              std::cos(polar)).normalized();

            t = std::min(t, m_cfg.max_bridge_length_mm);

            while(d < t && !std::isinf(tdown = bridge_mesh_intersect(
                                           hjp + d*bridgedir,
                                           dirdown,
                                           head.r_back_mm))) {
                d += head.r_back_mm;
            }

            if(std::isinf(tdown)) { // we heave found a route to the ground
                routedown(head, bridgedir, d); continue;
            }

            // /////////////////////////////////////////////////////////////////
            // Route to model body
            // /////////////////////////////////////////////////////////////////

            double zangle = std::asin(hit.direction()(Z));
            zangle = std::max(zangle, PI/4);
            double h = std::sin(zangle) * head.fullwidth();

            // The width of the tail head that we would like to have...
            h = std::min(hit.distance() - head.r_back_mm, h);

            if(h > 0) {
                Vec3d endp{hjp(X), hjp(Y), hjp(Z) - hit.distance() + h};
                auto center_hit = m_mesh.query_ray_hit(hjp, dirdown);

                double hitdiff = center_hit.distance() - hit.distance();
                Vec3d hitp = std::abs(hitdiff) < 2*head.r_back_mm?
                                center_hit.position() : hit.position();

                head.transform();

                Pillar& pill = m_result.add_pillar(unsigned(head.id),
                                                   endp,
                                                   head.r_back_mm);

                Vec3d taildir = endp - hitp;
                double dist = distance(endp, hitp) + m_cfg.head_penetration_mm;
                double w = dist - 2 * head.r_pin_mm - head.r_back_mm;

                Head tailhead(head.r_back_mm,
                              head.r_pin_mm,
                              w,
                              m_cfg.head_penetration_mm,
                              taildir,
                              hitp);

                tailhead.transform();
                pill.base = tailhead.mesh;

                // Experimental: add the pillar to the index for cascading
                modelpillars.emplace_back(unsigned(pill.id));
                continue;
            }

            // We have failed to route this head.
            BOOST_LOG_TRIVIAL(warning)
                    << "Failed to route model facing support point."
                    << " ID: " << idx;
            head.invalidate();
        }

        for(auto pillid : modelpillars) {
            auto& pillar = m_result.pillar(pillid);
            m_pillar_index.insert(pillar.endpoint(), pillid);
        }
    }
    
    // Helper function for interconnect_pillars where pairs of already connected
    // pillars should be checked for not to be processed again. This can be done
    // in O(log) or even constant time with a set or an unordered set of hash
    // values uniquely representing a pair of integers. The order of numbers
    // within the pair should not matter, it has the same unique hash.
    template<class I> static I pairhash(I a, I b)
    {
        using std::ceil; using std::log2; using std::max; using std::min;
        
        static_assert(std::is_integral<I>::value,
                      "This function works only for integral types.");

        I g = min(a, b), l = max(a, b);
        
        auto bits_g = g ? int(ceil(log2(g))) : 0;

        // Assume the hash will fit into the output variable
        assert((l ? (ceil(log2(l))) : 0) + bits_g < int(sizeof(I) * CHAR_BIT));
        
        return (l << bits_g) + g;
    }

    void interconnect_pillars() {
        // Now comes the algorithm that connects pillars with each other.
        // Ideally every pillar should be connected with at least one of its
        // neighbors if that neighbor is within max_pillar_link_distance

        // Pillars with height exceeding H1 will require at least one neighbor
        // to connect with. Height exceeding H2 require two neighbors.
        double H1 = m_cfg.max_solo_pillar_height_mm;
        double H2 = m_cfg.max_dual_pillar_height_mm;
        double d = m_cfg.max_pillar_link_distance_mm;

        //A connection between two pillars only counts if the height ratio is
        // bigger than 50%
        double min_height_ratio = 0.5;

        std::set<unsigned long> pairs;
        
        // A function to connect one pillar with its neighbors. THe number of
        // neighbors is given in the configuration. This function if called
        // for every pillar in the pillar index. A pair of pillar will not
        // be connected multiple times this is ensured by the 'pairs' set which
        // remembers the processed pillar pairs
        auto cascadefn =
                [this, d, &pairs, min_height_ratio, H1] (const PointIndexEl& el)
        {
            Vec3d qp = el.first;    // endpoint of the pillar

            const Pillar& pillar = m_result.pillar(el.second); // actual pillar
            
            // Get the max number of neighbors a pillar should connect to
            unsigned neighbors = m_cfg.pillar_cascade_neighbors;

            // connections are already enough for the pillar
            if(pillar.links >= neighbors) return;

            // Query all remaining points within reach
            auto qres = m_pillar_index.query([qp, d](const PointIndexEl& e){
                return distance(e.first, qp) < d;
            });

            // sort the result by distance (have to check if this is needed)
            std::sort(qres.begin(), qres.end(),
                      [qp](const PointIndexEl& e1, const PointIndexEl& e2){
                return distance(e1.first, qp) < distance(e2.first, qp);
            });

            for(auto& re : qres) { // process the queried neighbors

                if(re.second == el.second) continue; // Skip self

                auto a = el.second, b = re.second;

                // Get unique hash for the given pair (order doesn't matter)
                auto hashval = pairhash(a, b);
                
                // Search for the pair amongst the remembered pairs
                if(pairs.find(hashval) != pairs.end()) continue;

                const Pillar& neighborpillar = m_result.pillars()[re.second];

                // this neighbor is occupied, skip
                if(neighborpillar.links >= neighbors) continue;

                if(interconnect(pillar, neighborpillar)) {
                    pairs.insert(hashval);

                    // If the interconnection length between the two pillars is
                    // less than 50% of the longer pillar's height, don't count
                    if(pillar.height < H1 ||
                       neighborpillar.height / pillar.height > min_height_ratio)
                        m_result.increment_links(pillar);

                    if(neighborpillar.height < H1 ||
                       pillar.height / neighborpillar.height > min_height_ratio)
                        m_result.increment_links(neighborpillar);

                }

                // connections are enough for one pillar
                if(pillar.links >= neighbors) break;
            }
        };
        
        // Run the cascade for the pillars in the index
        m_pillar_index.foreach(cascadefn);
       
        // We would be done here if we could allow some pillars to not be
        // connected with any neighbors. But this might leave the support tree
        // unprintable.
        //
        // The current solution is to insert additional pillars next to these
        // lonely pillars. One or even two additional pillar might get inserted
        // depending on the length of the lonely pillar.
        
        size_t pillarcount = m_result.pillars().size();
        
        // Again, go through all pillars, this time in the whole support tree
        // not just the index.
        for(size_t pid = 0; pid < pillarcount; pid++) {
            auto pillar = [this, pid]() { return m_result.pillar(pid); };
           
            // Decide how many additional pillars will be needed:
            
            unsigned needpillars = 0;
            if (pillar().bridges > m_cfg.max_bridges_on_pillar)
                needpillars = 3;
            else if (pillar().links < 2 && pillar().height > H2) {
                // Not enough neighbors to support this pillar
                needpillars = 2 - pillar().links;
            } else if (pillar().links < 1 && pillar().height > H1) {
                // No neighbors could be found and the pillar is too long.
                needpillars = 1;
            }

            // Search for new pillar locations:

            bool   found    = false;
            double alpha    = 0; // goes to 2Pi
            double r        = 2 * m_cfg.base_radius_mm;
            Vec3d  pillarsp = pillar().startpoint();

            // temp value for starting point detection
            Vec3d sp(pillarsp(X), pillarsp(Y), pillarsp(Z) - r);

            // A vector of bool for placement feasbility
            std::vector<bool>  canplace(needpillars, false);
            std::vector<Vec3d> spts(needpillars); // vector of starting points

            double gnd      = m_result.ground_level;
            double min_dist = m_cfg.pillar_base_safety_distance_mm +
                              m_cfg.base_radius_mm + EPSILON;
            
            while(!found && alpha < 2*PI) {
                for (unsigned n = 0;
                     n < needpillars && (!n || canplace[n - 1]);
                     n++)
                {
                    double a = alpha + n * PI / 3;
                    Vec3d  s = sp;
                    s(X) += std::cos(a) * r;
                    s(Y) += std::sin(a) * r;
                    spts[n] = s;
                    
                    // Check the path vertically down                    
                    auto hr = bridge_mesh_intersect(s, {0, 0, -1}, pillar().r);
                    Vec3d gndsp{s(X), s(Y), gnd};
                    
                    // If the path is clear, check for pillar base collisions
                    canplace[n] = std::isinf(hr.distance()) &&
                                  std::sqrt(m_mesh.squared_distance(gndsp)) >
                                      min_dist;
                }

                found = std::all_of(canplace.begin(), canplace.end(),
                                    [](bool v) { return v; });

                // 20 angles will be tried...
                alpha += 0.1 * PI;
            }

            std::vector<long> newpills;
            newpills.reserve(needpillars);

            if(found) for(unsigned n = 0; n < needpillars; n++) {
                Vec3d s = spts[n]; 
                Pillar p(s, Vec3d(s(X), s(Y), gnd), pillar().r);
                p.add_base(m_cfg.base_height_mm, m_cfg.base_radius_mm);

                if(interconnect(pillar(), p)) {
                    Pillar& pp = m_result.add_pillar(p);
                    m_pillar_index.insert(pp.endpoint(), unsigned(pp.id));

                    m_result.add_junction(s, pillar().r);
                    double t = bridge_mesh_intersect(pillarsp,
                                                     dirv(pillarsp, s),
                                                     pillar().r);
                    if(distance(pillarsp, s) < t)
                        m_result.add_bridge(pillarsp, s, pillar().r);

                    if(pillar().endpoint()(Z) > m_result.ground_level)
                        m_result.add_junction(pillar().endpoint(), pillar().r);

                    newpills.emplace_back(pp.id);
                    m_result.increment_links(pillar());
                }
            }

            if(!newpills.empty()) {
                for(auto it = newpills.begin(), nx = std::next(it);
                    nx != newpills.end(); ++it, ++nx) {
                    const Pillar& itpll = m_result.pillar(*it);
                    const Pillar& nxpll = m_result.pillar(*nx);
                    if(interconnect(itpll, nxpll)) {
                        m_result.increment_links(itpll);
                        m_result.increment_links(nxpll);
                    }
                }

                m_pillar_index.foreach(cascadefn);
            }
        }
    }

    // Step: process the support points where there is not enough space for a
    // full pinhead. In this case we will use a rounded sphere as a touching
    // point and use a thinner bridge (let's call it a stick).
    void routing_headless ()
    {
        // For now we will just generate smaller headless sticks with a sharp
        // ending point that connects to the mesh surface.

        // We will sink the pins into the model surface for a distance of 1/3 of
        // the pin radius
        for(unsigned i : m_iheadless) { m_thr();

            const auto R = double(m_support_pts[i].head_front_radius);
            const double HWIDTH_MM = R/3;

            // Exact support position
            Vec3d sph = m_support_pts[i].pos.cast<double>();
            Vec3d n = m_support_nmls.row(i);   // mesh outward normal
            Vec3d sp = sph - n * HWIDTH_MM;     // stick head start point

            Vec3d dir = {0, 0, -1};
            Vec3d sj = sp + R * n;              // stick start point

            // This is only for checking
            double idist = bridge_mesh_intersect(sph, dir, R, true);
            double dist = ray_mesh_intersect(sj, dir);
            if (std::isinf(dist))
                dist = sph(Z) - m_mesh.ground_level()
                       + m_mesh.ground_level_offset();

            if(std::isnan(idist) || idist < 2*R ||
               std::isnan(dist)  || dist  < 2*R)
            {
                BOOST_LOG_TRIVIAL(warning) << "Can not find route for headless"
                                           << " support stick at: "
                                           << sj.transpose();
                continue;
            }

            Vec3d ej = sj + (dist + HWIDTH_MM)* dir;
            m_result.add_compact_bridge(sp, ej, n, R, !std::isinf(dist));
        }
    }
};

bool SLASupportTree::generate(const std::vector<SupportPoint> &support_points,
                              const EigenMesh3D& mesh,
                              const SupportConfig &cfg,
                              const Controller &ctl)
{
    if(support_points.empty()) return false;

    Algorithm alg(cfg, mesh, support_points, *m_impl, ctl.cancelfn);

    // Let's define the individual steps of the processing. We can experiment
    // later with the ordering and the dependencies between them.
    enum Steps {
        BEGIN,
        FILTER,
        PINHEADS,
        CLASSIFY,
        ROUTING_GROUND,
        ROUTING_NONGROUND,
        CASCADE_PILLARS,
        HEADLESS,
        DONE,
        ABORT,
        NUM_STEPS
        //...
    };

    // Collect the algorithm steps into a nice sequence
    std::array<std::function<void()>, NUM_STEPS> program = {
        [] () {
            // Begin...
            // Potentially clear up the shared data (not needed for now)
        },

        std::bind(&Algorithm::filter, &alg),

        std::bind(&Algorithm::add_pinheads, &alg),

        std::bind(&Algorithm::classify, &alg),

        std::bind(&Algorithm::routing_to_ground, &alg),

        std::bind(&Algorithm::routing_to_model, &alg),

        std::bind(&Algorithm::interconnect_pillars, &alg),

        std::bind(&Algorithm::routing_headless, &alg),

        [] () {
            // Done
        },

        [] () {
            // Abort
        }
    };

    Steps pc = BEGIN;

    if(cfg.ground_facing_only) {
        program[ROUTING_NONGROUND] = []() {
            BOOST_LOG_TRIVIAL(info)
                    << "Skipping model-facing supports as requested.";
        };
        program[HEADLESS] = []() {
            BOOST_LOG_TRIVIAL(info) << "Skipping headless stick generation as"
                                       " requested.";
        };
    }

    // Let's define a simple automaton that will run our program.
    auto progress = [&ctl, &pc] () {
        static const std::array<std::string, NUM_STEPS> stepstr {
            "Starting",
            "Filtering",
            "Generate pinheads",
            "Classification",
            "Routing to ground",
            "Routing supports to model surface",
            "Interconnecting pillars",
            "Processing small holes",
            "Done",
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
            90,
            100,
            0
        };

        if(ctl.stopcondition()) pc = ABORT;

        switch(pc) {
        case BEGIN: pc = FILTER; break;
        case FILTER: pc = PINHEADS; break;
        case PINHEADS: pc = CLASSIFY; break;
        case CLASSIFY: pc = ROUTING_GROUND; break;
        case ROUTING_GROUND: pc = ROUTING_NONGROUND; break;
        case ROUTING_NONGROUND: pc = CASCADE_PILLARS; break;
        case CASCADE_PILLARS: pc = HEADLESS; break;
        case HEADLESS: pc = DONE; break;
        case DONE:
        case ABORT: break;
        default: ;
        }
        ctl.statuscb(stepstate[pc], stepstr[pc]);
    };

    // Just here we run the computation...
    while(pc < DONE) {
        progress();
        program[pc]();
    }

    return pc == ABORT;
}

SLASupportTree::SLASupportTree(double gnd_lvl): m_impl(new Impl()) {
    m_impl->ground_level = gnd_lvl;
}

const TriangleMesh &SLASupportTree::merged_mesh() const
{
    return get().merged_mesh();
}

void SLASupportTree::merged_mesh_with_pad(TriangleMesh &outmesh) const {
    outmesh.merge(merged_mesh());
    outmesh.merge(get_pad());
}

std::vector<ExPolygons> SLASupportTree::slice(float layerh, float init_layerh) const
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
    if (!fullmesh.empty()) fullmesh.require_shared_vertices();
    TriangleMeshSlicer slicer(&fullmesh);
    std::vector<ExPolygons> ret;
    slicer.slice(heights, 0.f, &ret, get().ctl().cancelfn);

    return ret;
}

std::vector<ExPolygons> SLASupportTree::slice(const std::vector<float> &heights,
                                     float cr) const
{
    TriangleMesh fullmesh = m_impl->merged_mesh();
    fullmesh.merge(get_pad());
    if (!fullmesh.empty()) fullmesh.require_shared_vertices();
    TriangleMeshSlicer slicer(&fullmesh);
    std::vector<ExPolygons> ret;
    slicer.slice(heights, cr, &ret, get().ctl().cancelfn);

    return ret;
}

const TriangleMesh &SLASupportTree::add_pad(const ExPolygons& modelbase,
                                            const PoolConfig& pcfg) const
{
    return m_impl->create_pad(merged_mesh(), modelbase, pcfg).tmesh;
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
    m_impl->clear_support_data();
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
