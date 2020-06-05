#ifndef SLA_SUPPORTTREEBUILDER_HPP
#define SLA_SUPPORTTREEBUILDER_HPP

#include <libslic3r/SLA/Concurrency.hpp>
#include <libslic3r/SLA/Common.hpp>
#include <libslic3r/SLA/SupportTree.hpp>
#include <libslic3r/SLA/Contour3D.hpp>
#include <libslic3r/SLA/Pad.hpp>
#include <libslic3r/MTUtils.hpp>

namespace Slic3r {
namespace sla {

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
                 double fa=(2*PI/360));

// Down facing cylinder in Z direction with arguments:
// r: radius
// h: Height
// ssteps: how many edges will create the base circle
// sp: starting point
Contour3D cylinder(double r, double h, size_t ssteps = 45, const Vec3d &sp = {0,0,0});

Contour3D pinhead(double r_pin, double r_back, double length, size_t steps = 45);

const constexpr long ID_UNSET = -1;

struct Head {
    Contour3D mesh;
    
    size_t steps = 45;
    Vec3d dir = {0, 0, -1};
    Vec3d pos = {0, 0, 0};
    
    double r_back_mm = 1;
    double r_pin_mm = 0.5;
    double width_mm = 2;
    double penetration_mm = 0.5;
    
    // For identification purposes. This will be used as the index into the
    // container holding the head structures. See SLASupportTree::Impl
    long id = ID_UNSET;
    
    // If there is a pillar connecting to this head, then the id will be set.
    long pillar_id = ID_UNSET;
    
    long bridge_id = ID_UNSET;
    
    inline void invalidate() { id = ID_UNSET; }
    inline bool is_valid() const { return id >= 0; }
    
    Head(double r_big_mm,
         double r_small_mm,
         double length_mm,
         double penetration,
         const Vec3d &direction = {0, 0, -1},  // direction (normal to the dull end)
         const Vec3d &offset = {0, 0, 0},      // displacement
         const size_t circlesteps = 45);
    
    void transform()
    {
        using Quaternion = Eigen::Quaternion<double>;
        
        // We rotate the head to the specified direction The head's pointing
        // side is facing upwards so this means that it would hold a support
        // point with a normal pointing straight down. This is the reason of
        // the -1 z coordinate
        auto quatern = Quaternion::FromTwoVectors(Vec3d{0, 0, -1}, dir);
        
        for(auto& p : mesh.points) p = quatern * p + pos;
    }
    
    inline double real_width() const
    {
        return 2 * r_pin_mm + width_mm + 2 * r_back_mm ;
    }

    inline double fullwidth() const
    {
        return real_width() - penetration_mm;
    }
    
    inline Vec3d junction_point() const
    {
        return pos + (fullwidth() - r_back_mm) * dir;
    }
    
    inline double request_pillar_radius(double radius) const
    {
        const double rmax = r_back_mm;
        return radius > 0 && radius < rmax ? radius : rmax;
    }
};

struct Junction {
    Contour3D mesh;
    double r = 1;
    size_t steps = 45;
    Vec3d pos;
    
    long id = ID_UNSET;
    
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
    
    long id = ID_UNSET;
    
    // If the pillar connects to a head, this is the id of that head
    bool starts_from_head = true; // Could start from a junction as well
    long start_junction_id = ID_UNSET;
    
    // How many bridges are connected to this pillar
    unsigned bridges = 0;
    
    // How many pillars are cascaded with this one
    unsigned links = 0;
    
    Pillar(const Vec3d& jp, const Vec3d& endp,
           double radius = 1, size_t st = 45);
    
    Pillar(const Junction &junc, const Vec3d &endp)
        : Pillar(junc.pos, endp, junc.r, junc.steps)
    {}
    
    Pillar(const Head &head, const Vec3d &endp, double radius = 1)
        : Pillar(head.junction_point(), endp,
                 head.request_pillar_radius(radius), head.steps)
    {}
    
    inline Vec3d startpoint() const
    {
        return {endpt(X), endpt(Y), endpt(Z) + height};
    }
    
    inline const Vec3d& endpoint() const { return endpt; }
    
    Pillar& add_base(double baseheight = 3, double radius = 2);
};

// A Bridge between two pillars (with junction endpoints)
struct Bridge {
    Contour3D mesh;
    double r = 0.8;
    long id = ID_UNSET;
    Vec3d startp = Vec3d::Zero(), endp = Vec3d::Zero();
    
    Bridge(const Vec3d &j1,
           const Vec3d &j2,
           double       r_mm  = 0.8,
           size_t       steps = 45);

    Bridge(const Vec3d &j1,
           const Vec3d &j2,
           double       r1_mm,
           double       r2_mm,
           size_t       steps = 45);
};

// A wrapper struct around the pad
struct Pad {
    TriangleMesh tmesh;
    PadConfig cfg;
    double zlevel = 0;
    
    Pad() = default;
    
    Pad(const TriangleMesh &support_mesh,
        const ExPolygons &  model_contours,
        double              ground_level,
        const PadConfig &   pcfg,
        ThrowOnCancel       thr);
    
    bool empty() const { return tmesh.facets_count() == 0; }
};

// Give points on a 3D ring with given center, radius and orientation
// method based on:
// https://math.stackexchange.com/questions/73237/parametric-equation-of-a-circle-in-3d-space
template<size_t N>
class PointRing {
    std::array<double, N> m_phis;

    // Two vectors that will be perpendicular to each other and to the
    // axis. Values for a(X) and a(Y) are now arbitrary, a(Z) is just a
    // placeholder.
    // a and b vectors are perpendicular to the ring direction and to each other.
    // Together they define the plane where we have to iterate with the
    // given angles in the 'm_phis' vector
    Vec3d a = {0, 1, 0}, b;
    double m_radius = 0.;

    static inline bool constexpr is_one(double val)
    {
        return std::abs(std::abs(val) - 1) < 1e-20;
    }

public:

    PointRing(const Vec3d &n)
    {
        m_phis = linspace_array<N>(0., 2 * PI);

        // We have to address the case when the direction vector v (same as
        // dir) is coincident with one of the world axes. In this case two of
        // its components will be completely zero and one is 1.0. Our method
        // becomes dangerous here due to division with zero. Instead, vector
        // 'a' can be an element-wise rotated version of 'v'
        if(is_one(n(X)) || is_one(n(Y)) || is_one(n(Z))) {
            a = {n(Z), n(X), n(Y)};
            b = {n(Y), n(Z), n(X)};
        }
        else {
            a(Z) = -(n(Y)*a(Y)) / n(Z); a.normalize();
            b = a.cross(n);
        }
    }

    Vec3d get(size_t idx, const Vec3d src, double r) const
    {
        double phi = m_phis[idx];
        double sinphi = std::sin(phi);
        double cosphi = std::cos(phi);

        double rpscos = r * cosphi;
        double rpssin = r * sinphi;

        // Point on the sphere
        return {src(X) + rpscos * a(X) + rpssin * b(X),
                src(Y) + rpscos * a(Y) + rpssin * b(Y),
                src(Z) + rpscos * a(Z) + rpssin * b(Z)};
    }
};

EigenMesh3D::hit_result query_hit(const SupportableMesh &msh, const Bridge &br, double safety_d = std::nan(""));
EigenMesh3D::hit_result query_hit(const SupportableMesh &msh, const Head &br, double safety_d = std::nan(""));

// This class will hold the support tree meshes with some additional
// bookkeeping as well. Various parts of the support geometry are stored
// separately and are merged when the caller queries the merged mesh. The
// merged result is cached for fast subsequent delivery of the merged mesh
// which can be quite complex. The support tree creation algorithm can use an
// instance of this class as a somewhat higher level tool for crafting the 3D
// support mesh. Parts can be added with the appropriate methods such as
// add_head or add_pillar which forwards the constructor arguments and fills
// the IDs of these substructures. The IDs are basically indices into the
// arrays of the appropriate type (heads, pillars, etc...). One can later query
// e.g. a pillar for a specific head...
//
// The support pad is considered an auxiliary geometry and is not part of the
// merged mesh. It can be retrieved using a dedicated method (pad())
class SupportTreeBuilder: public SupportTree {
    // For heads it is beneficial to use the same IDs as for the support points.
    std::vector<Head> m_heads;
    std::vector<size_t> m_head_indices;
    std::vector<Pillar> m_pillars;
    std::vector<Junction> m_junctions;
    std::vector<Bridge> m_bridges;
    std::vector<Bridge> m_crossbridges;
    Pad m_pad;
    
    using Mutex = ccr::SpinningMutex;
    
    mutable TriangleMesh m_meshcache;
    mutable Mutex m_mutex;
    mutable bool m_meshcache_valid = false;
    mutable double m_model_height = 0; // the full height of the model
    
    template<class...Args>
    const Bridge& _add_bridge(std::vector<Bridge> &br, Args&&... args)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        br.emplace_back(std::forward<Args>(args)...);
        br.back().id = long(br.size() - 1);
        m_meshcache_valid = false;
        return br.back();
    }
    
public:
    double ground_level = 0;
    
    SupportTreeBuilder() = default;
    SupportTreeBuilder(SupportTreeBuilder &&o);
    SupportTreeBuilder(const SupportTreeBuilder &o);
    SupportTreeBuilder& operator=(SupportTreeBuilder &&o);
    SupportTreeBuilder& operator=(const SupportTreeBuilder &o);

    template<class...Args> Head& add_head(unsigned id, Args&&... args)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        m_heads.emplace_back(std::forward<Args>(args)...);
        m_heads.back().id = id;
        
        if (id >= m_head_indices.size()) m_head_indices.resize(id + 1);
        m_head_indices[id] = m_heads.size() - 1;
        
        m_meshcache_valid = false;
        return m_heads.back();
    }
    
    template<class...Args> long add_pillar(long headid, Args&&... args)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        if (m_pillars.capacity() < m_heads.size())
            m_pillars.reserve(m_heads.size() * 10);
        
        assert(headid >= 0 && size_t(headid) < m_head_indices.size());
        Head &head = m_heads[m_head_indices[size_t(headid)]];
        
        m_pillars.emplace_back(head, std::forward<Args>(args)...);
        Pillar& pillar = m_pillars.back();
        pillar.id = long(m_pillars.size() - 1);
        head.pillar_id = pillar.id;
        pillar.start_junction_id = head.id;
        pillar.starts_from_head = true;
        
        m_meshcache_valid = false;
        return pillar.id;
    }
    
    void add_pillar_base(long pid, double baseheight = 3, double radius = 2)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        assert(pid >= 0 && size_t(pid) < m_pillars.size());
        m_pillars[size_t(pid)].add_base(baseheight, radius);
    }
    
    void increment_bridges(const Pillar& pillar)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        assert(pillar.id >= 0 && size_t(pillar.id) < m_pillars.size());
        
        if(pillar.id >= 0 && size_t(pillar.id) < m_pillars.size())
            m_pillars[size_t(pillar.id)].bridges++;
    }
    
    void increment_links(const Pillar& pillar)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        assert(pillar.id >= 0 && size_t(pillar.id) < m_pillars.size());
        
        if(pillar.id >= 0 && size_t(pillar.id) < m_pillars.size()) 
            m_pillars[size_t(pillar.id)].links++;
    }
    
    unsigned bridgecount(const Pillar &pillar) const {
        std::lock_guard<Mutex> lk(m_mutex);
        assert(pillar.id >= 0 && size_t(pillar.id) < m_pillars.size());
        return pillar.bridges;
    }
    
    template<class...Args> long add_pillar(Args&&...args)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        if (m_pillars.capacity() < m_heads.size())
            m_pillars.reserve(m_heads.size() * 10);
        
        m_pillars.emplace_back(std::forward<Args>(args)...);
        Pillar& pillar = m_pillars.back();
        pillar.id = long(m_pillars.size() - 1);
        pillar.starts_from_head = false;
        m_meshcache_valid = false;
        return pillar.id;
    }
    
    const Pillar& head_pillar(unsigned headid) const
    {
        std::lock_guard<Mutex> lk(m_mutex);
        assert(headid < m_head_indices.size());
        
        const Head& h = m_heads[m_head_indices[headid]];
        assert(h.pillar_id >= 0 && h.pillar_id < long(m_pillars.size()));
        
        return m_pillars[size_t(h.pillar_id)];
    }
    
    template<class...Args> const Junction& add_junction(Args&&... args)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        m_junctions.emplace_back(std::forward<Args>(args)...);
        m_junctions.back().id = long(m_junctions.size() - 1);
        m_meshcache_valid = false;
        return m_junctions.back();
    }
    
    const Bridge& add_bridge(const Vec3d &s, const Vec3d &e, double r, size_t n = 45)
    {
        return _add_bridge(m_bridges, s, e, r, n);
    }
    
    const Bridge& add_bridge(long headid, const Vec3d &endp, size_t s = 45)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        assert(headid >= 0 && size_t(headid) < m_head_indices.size());
        
        Head &h = m_heads[m_head_indices[size_t(headid)]];
        m_bridges.emplace_back(h.junction_point(), endp, h.r_back_mm, s);
        m_bridges.back().id = long(m_bridges.size() - 1);
        
        h.bridge_id = m_bridges.back().id;
        m_meshcache_valid = false;
        return m_bridges.back();
    }
    
    template<class...Args> const Bridge& add_crossbridge(Args&&... args)
    {
        return _add_bridge(m_crossbridges, std::forward<Args>(args)...);
    }
    
    Head &head(unsigned id)
    {
        std::lock_guard<Mutex> lk(m_mutex);
        assert(id < m_head_indices.size());
        
        m_meshcache_valid = false;
        return m_heads[m_head_indices[id]];
    }
    
    inline size_t pillarcount() const {
        std::lock_guard<Mutex> lk(m_mutex);
        return m_pillars.size();
    }
    
    inline const std::vector<Pillar> &pillars() const { return m_pillars; }
    inline const std::vector<Head> &heads() const { return m_heads; }
    inline const std::vector<Bridge> &bridges() const { return m_bridges; }
    inline const std::vector<Bridge> &crossbridges() const { return m_crossbridges; }
    
    template<class T> inline IntegerOnly<T, const Pillar&> pillar(T id) const
    {
        std::lock_guard<Mutex> lk(m_mutex);
        assert(id >= 0 && size_t(id) < m_pillars.size() &&
               size_t(id) < std::numeric_limits<size_t>::max());
        
        return m_pillars[size_t(id)];
    }
    
    template<class T> inline IntegerOnly<T, Pillar&> pillar(T id) 
    {
        std::lock_guard<Mutex> lk(m_mutex);
        assert(id >= 0 && size_t(id) < m_pillars.size() &&
               size_t(id) < std::numeric_limits<size_t>::max());
        
        return m_pillars[size_t(id)];
    }
    
    const Pad& pad() const { return m_pad; }
    
    // WITHOUT THE PAD!!!
    const TriangleMesh &merged_mesh() const;
    
    // WITH THE PAD
    double full_height() const;
    
    // WITHOUT THE PAD!!!
    inline double mesh_height() const
    {
        if (!m_meshcache_valid) merged_mesh();
        return m_model_height;
    }
    
    // Intended to be called after the generation is fully complete
    const TriangleMesh & merge_and_cleanup();
    
    // Implement SupportTree interface:

    const TriangleMesh &add_pad(const ExPolygons &modelbase,
                                const PadConfig & pcfg) override;
    
    void remove_pad() override { m_pad = Pad(); }
    
    virtual const TriangleMesh &retrieve_mesh(
        MeshType meshtype = MeshType::Support) const override;
};

}} // namespace Slic3r::sla

#endif // SUPPORTTREEBUILDER_HPP
