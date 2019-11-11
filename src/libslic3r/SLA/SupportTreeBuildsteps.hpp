#ifndef SLASUPPORTTREEALGORITHM_H
#define SLASUPPORTTREEALGORITHM_H

#include <cstdint>

#include <libslic3r/SLA/SupportTreeBuilder.hpp>
#include <libslic3r/SLA/Clustering.hpp>

namespace Slic3r {
namespace sla {

// The minimum distance for two support points to remain valid.
const double /*constexpr*/ D_SP = 0.1;

enum { // For indexing Eigen vectors as v(X), v(Y), v(Z) instead of numbers
    X, Y, Z
};

inline Vec2d to_vec2(const Vec3d& v3) {
    return {v3(X), v3(Y)};
}

// This function returns the position of the centroid in the input 'clust'
// vector of point indices.
template<class DistFn>
long cluster_centroid(const ClusterEl& clust,
                      const std::function<Vec3d(size_t)> &pointfn,
                      DistFn df)
{
    switch(clust.size()) {
    case 0: /* empty cluster */ return ID_UNSET;
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

class PillarIndex {
    PointIndex m_index;
    using Mutex = ccr::BlockingMutex;
    mutable Mutex m_mutex;

public:

    template<class...Args> inline void guarded_insert(Args&&...args)
    {
        std::lock_guard<Mutex> lck(m_mutex);
        m_index.insert(std::forward<Args>(args)...);
    }

    template<class...Args>
    inline std::vector<PointIndexEl> guarded_query(Args&&...args) const
    {
        std::lock_guard<Mutex> lck(m_mutex);
        return m_index.query(std::forward<Args>(args)...);
    }

    template<class...Args> inline void insert(Args&&...args)
    {
        m_index.insert(std::forward<Args>(args)...);
    }

    template<class...Args>
    inline std::vector<PointIndexEl> query(Args&&...args) const
    {
        return m_index.query(std::forward<Args>(args)...);
    }

    template<class Fn> inline void foreach(Fn fn) { m_index.foreach(fn); }
    template<class Fn> inline void guarded_foreach(Fn fn)
    {
        std::lock_guard<Mutex> lck(m_mutex);
        m_index.foreach(fn);
    }

    PointIndex guarded_clone()
    {
        std::lock_guard<Mutex> lck(m_mutex);
        return m_index;
    }
};

// Helper function for pillar interconnection where pairs of already connected
// pillars should be checked for not to be processed again. This can be done
// in constant time with a set of hash values uniquely representing a pair of
// integers. The order of numbers within the pair should not matter, it has
// the same unique hash. The hash value has to have twice as many bits as the
// arguments need. If the same integral type is used for args and return val,
// make sure the arguments use only the half of the type's bit depth.
template<class I, class DoubleI = IntegerOnly<I>>
IntegerOnly<DoubleI> pairhash(I a, I b)
{
    using std::ceil; using std::log2; using std::max; using std::min;
    static const auto constexpr Ibits = int(sizeof(I) * CHAR_BIT);
    static const auto constexpr DoubleIbits = int(sizeof(DoubleI) * CHAR_BIT);
    static const auto constexpr shift = DoubleIbits / 2 < Ibits ? Ibits / 2 : Ibits;

    I g = min(a, b), l = max(a, b);

    // Assume the hash will fit into the output variable
    assert((g ? (ceil(log2(g))) : 0) <= shift);
    assert((l ? (ceil(log2(l))) : 0) <= shift);

    return (DoubleI(g) << shift) + l;
}

class SupportTreeBuildsteps {
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

    // This algorithm uses the SupportTreeBuilder class to fill gradually
    // the support elements (heads, pillars, bridges, ...)
    SupportTreeBuilder& m_builder;

    // support points in Eigen/IGL format
    PointSet m_points;

    // throw if canceled: It will be called many times so a shorthand will
    // come in handy.
    ThrowOnCancel m_thr;

    // A spatial index to easily find strong pillars to connect to.
    PillarIndex m_pillar_index;

    // When bridging heads to pillars... TODO: find a cleaner solution
    ccr::BlockingMutex m_bridge_mutex;

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
        double width);

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
        bool ins_check = false);

    // Helper function for interconnecting two pillars with zig-zag bridges.
    bool interconnect(const Pillar& pillar, const Pillar& nextpillar);

    // For connecting a head to a nearby pillar.
    bool connect_to_nearpillar(const Head& head, long nearpillar_id);

    bool search_pillar_and_connect(const Head& head);

    // This is a proxy function for pillar creation which will mind the gap
    // between the pad and the model bottom in zero elevation mode.
    void create_ground_pillar(const Vec3d &jp,
                              const Vec3d &sourcedir,
                              double       radius,
                              long         head_id = ID_UNSET);
public:
    SupportTreeBuildsteps(SupportTreeBuilder & builder, const SupportableMesh &sm);

    // Now let's define the individual steps of the support generation algorithm

    // Filtering step: here we will discard inappropriate support points
    // and decide the future of the appropriate ones. We will check if a
    // pinhead is applicable and adjust its angle at each support point. We
    // will also merge the support points that are just too close and can
    // be considered as one.
    void filter();

    // Pinhead creation: based on the filtering results, the Head objects
    // will be constructed (together with their triangle meshes).
    void add_pinheads();

    // Further classification of the support points with pinheads. If the
    // ground is directly reachable through a vertical line parallel to the
    // Z axis we consider a support point as pillar candidate. If touches
    // the model geometry, it will be marked as non-ground facing and
    // further steps will process it. Also, the pillars will be grouped
    // into clusters that can be interconnected with bridges. Elements of
    // these groups may or may not be interconnected. Here we only run the
    // clustering algorithm.
    void classify();

    // Step: Routing the ground connected pinheads, and interconnecting
    // them with additional (angled) bridges. Not all of these pinheads
    // will be a full pillar (ground connected). Some will connect to a
    // nearby pillar using a bridge. The max number of such side-heads for
    // a central pillar is limited to avoid bad weight distribution.
    void routing_to_ground();

    // Step: routing the pinheads that would connect to the model surface
    // along the Z axis downwards. For now these will actually be connected with
    // the model surface with a flipped pinhead. In the future here we could use
    // some smart algorithms to search for a safe path to the ground or to a
    // nearby pillar that can hold the supported weight.
    void routing_to_model();

    void interconnect_pillars();

    // Step: process the support points where there is not enough space for a
    // full pinhead. In this case we will use a rounded sphere as a touching
    // point and use a thinner bridge (let's call it a stick).
    void routing_headless ();

    inline void merge_result() { m_builder.merged_mesh(); }

    static bool execute(SupportTreeBuilder & builder, const SupportableMesh &sm);
};

}
}

#endif // SLASUPPORTTREEALGORITHM_H
