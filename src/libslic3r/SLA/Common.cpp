#include <cmath>
#include <libslic3r/SLA/Common.hpp>
#include <libslic3r/SLA/Concurrency.hpp>
#include <libslic3r/SLA/SpatIndex.hpp>
#include <libslic3r/SLA/EigenMesh3D.hpp>
#include <libslic3r/SLA/Contour3D.hpp>
#include <libslic3r/SLA/Clustering.hpp>
#include <libslic3r/AABBTreeIndirect.hpp>

// for concave hull merging decisions
#include <libslic3r/SLA/BoostAdapter.hpp>
#include "boost/geometry/index/rtree.hpp"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#endif


#include <igl/remove_duplicate_vertices.h>

#ifdef SLIC3R_HOLE_RAYCASTER
  #include <libslic3r/SLA/Hollowing.hpp>
#endif


#ifdef _MSC_VER
#pragma warning(pop)
#endif


namespace Slic3r {
namespace sla {


/* **************************************************************************
 * PointIndex implementation
 * ************************************************************************** */

class PointIndex::Impl {
public:
    using BoostIndex = boost::geometry::index::rtree< PointIndexEl,
                                                     boost::geometry::index::rstar<16, 4> /* ? */ >;
    
    BoostIndex m_store;
};

PointIndex::PointIndex(): m_impl(new Impl()) {}
PointIndex::~PointIndex() {}

PointIndex::PointIndex(const PointIndex &cpy): m_impl(new Impl(*cpy.m_impl)) {}
PointIndex::PointIndex(PointIndex&& cpy): m_impl(std::move(cpy.m_impl)) {}

PointIndex& PointIndex::operator=(const PointIndex &cpy)
{
    m_impl.reset(new Impl(*cpy.m_impl));
    return *this;
}

PointIndex& PointIndex::operator=(PointIndex &&cpy)
{
    m_impl.swap(cpy.m_impl);
    return *this;
}

void PointIndex::insert(const PointIndexEl &el)
{
    m_impl->m_store.insert(el);
}

bool PointIndex::remove(const PointIndexEl& el)
{
    return m_impl->m_store.remove(el) == 1;
}

std::vector<PointIndexEl>
PointIndex::query(std::function<bool(const PointIndexEl &)> fn) const
{
    namespace bgi = boost::geometry::index;
    
    std::vector<PointIndexEl> ret;
    m_impl->m_store.query(bgi::satisfies(fn), std::back_inserter(ret));
    return ret;
}

std::vector<PointIndexEl> PointIndex::nearest(const Vec3d &el, unsigned k = 1) const
{
    namespace bgi = boost::geometry::index;
    std::vector<PointIndexEl> ret; ret.reserve(k);
    m_impl->m_store.query(bgi::nearest(el, k), std::back_inserter(ret));
    return ret;
}

size_t PointIndex::size() const
{
    return m_impl->m_store.size();
}

void PointIndex::foreach(std::function<void (const PointIndexEl &)> fn)
{
    for(auto& el : m_impl->m_store) fn(el);
}

void PointIndex::foreach(std::function<void (const PointIndexEl &)> fn) const
{
    for(const auto &el : m_impl->m_store) fn(el);
}

/* **************************************************************************
 * BoxIndex implementation
 * ************************************************************************** */

class BoxIndex::Impl {
public:
    using BoostIndex = boost::geometry::index::
        rtree<BoxIndexEl, boost::geometry::index::rstar<16, 4> /* ? */>;
    
    BoostIndex m_store;
};

BoxIndex::BoxIndex(): m_impl(new Impl()) {}
BoxIndex::~BoxIndex() {}

BoxIndex::BoxIndex(const BoxIndex &cpy): m_impl(new Impl(*cpy.m_impl)) {}
BoxIndex::BoxIndex(BoxIndex&& cpy): m_impl(std::move(cpy.m_impl)) {}

BoxIndex& BoxIndex::operator=(const BoxIndex &cpy)
{
    m_impl.reset(new Impl(*cpy.m_impl));
    return *this;
}

BoxIndex& BoxIndex::operator=(BoxIndex &&cpy)
{
    m_impl.swap(cpy.m_impl);
    return *this;
}

void BoxIndex::insert(const BoxIndexEl &el)
{
    m_impl->m_store.insert(el);
}

bool BoxIndex::remove(const BoxIndexEl& el)
{
    return m_impl->m_store.remove(el) == 1;
}

std::vector<BoxIndexEl> BoxIndex::query(const BoundingBox &qrbb,
                                        BoxIndex::QueryType qt)
{
    namespace bgi = boost::geometry::index;
    
    std::vector<BoxIndexEl> ret; ret.reserve(m_impl->m_store.size());
    
    switch (qt) {
    case qtIntersects:
        m_impl->m_store.query(bgi::intersects(qrbb), std::back_inserter(ret));
        break;
    case qtWithin:
        m_impl->m_store.query(bgi::within(qrbb), std::back_inserter(ret));
    }
    
    return ret;
}

size_t BoxIndex::size() const
{
    return m_impl->m_store.size();
}

void BoxIndex::foreach(std::function<void (const BoxIndexEl &)> fn)
{
    for(auto& el : m_impl->m_store) fn(el);
}


/* ****************************************************************************
 * EigenMesh3D implementation
 * ****************************************************************************/


class EigenMesh3D::AABBImpl {
private:
    AABBTreeIndirect::Tree3f m_tree;

public:
    void init(const TriangleMesh& tm)
    {
        m_tree = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(
                    tm.its.vertices, tm.its.indices);
    }

    void intersect_ray(const TriangleMesh& tm,
                       const Vec3d& s, const Vec3d& dir, igl::Hit& hit)
    {
        AABBTreeIndirect::intersect_ray_first_hit(tm.its.vertices,
                                                  tm.its.indices,
                                                  m_tree,
                                                  s, dir, hit);
    }

    void intersect_ray(const TriangleMesh& tm,
                       const Vec3d& s, const Vec3d& dir, std::vector<igl::Hit>& hits)
    {
        AABBTreeIndirect::intersect_ray_all_hits(tm.its.vertices,
                                                 tm.its.indices,
                                                 m_tree,
                                                 s, dir, hits);
    }

    double squared_distance(const TriangleMesh& tm,
                            const Vec3d& point, int& i, Eigen::Matrix<double, 1, 3>& closest) {
        size_t idx_unsigned = 0;
        Vec3d closest_vec3d(closest);
        double dist = AABBTreeIndirect::squared_distance_to_indexed_triangle_set(
                          tm.its.vertices,
                          tm.its.indices,
                          m_tree, point, idx_unsigned, closest_vec3d);
        i = int(idx_unsigned);
        closest = closest_vec3d;
        return dist;
    }
};

static const constexpr double MESH_EPS = 1e-6;

EigenMesh3D::EigenMesh3D(const TriangleMesh& tmesh)
    : m_aabb(new AABBImpl()), m_tm(&tmesh)
{
    auto&& bb = tmesh.bounding_box();
    m_ground_level += bb.min(Z);
    
    // Build the AABB accelaration tree
    m_aabb->init(tmesh);
}

EigenMesh3D::~EigenMesh3D() {}

EigenMesh3D::EigenMesh3D(const EigenMesh3D &other):
    m_tm(other.m_tm), m_ground_level(other.m_ground_level),
    m_aabb( new AABBImpl(*other.m_aabb) ) {}


EigenMesh3D &EigenMesh3D::operator=(const EigenMesh3D &other)
{
    m_tm = other.m_tm;
    m_ground_level = other.m_ground_level;
    m_aabb.reset(new AABBImpl(*other.m_aabb)); return *this;
}

EigenMesh3D &EigenMesh3D::operator=(EigenMesh3D &&other) = default;

EigenMesh3D::EigenMesh3D(EigenMesh3D &&other) = default;



const std::vector<Vec3f>& EigenMesh3D::vertices() const
{
    return m_tm->its.vertices;
}



const std::vector<Vec3i>& EigenMesh3D::indices()  const
{
    return m_tm->its.indices;
}



const Vec3f& EigenMesh3D::vertices(size_t idx) const
{
    return m_tm->its.vertices[idx];
}



const Vec3i& EigenMesh3D::indices(size_t idx) const
{
    return m_tm->its.indices[idx];
}



Vec3d EigenMesh3D::normal_by_face_id(int face_id) const {
    return m_tm->stl.facet_start[face_id].normal.cast<double>();
}



EigenMesh3D::hit_result
EigenMesh3D::query_ray_hit(const Vec3d &s, const Vec3d &dir) const
{
    assert(is_approx(dir.norm(), 1.));
    igl::Hit hit;
    hit.t = std::numeric_limits<float>::infinity();

#ifdef SLIC3R_HOLE_RAYCASTER
    if (! m_holes.empty()) {

        // If there are holes, the hit_results will be made by
        // query_ray_hits (object) and filter_hits (holes):
        return filter_hits(query_ray_hits(s, dir));
    }
#endif

    m_aabb->intersect_ray(*m_tm, s, dir, hit);
    hit_result ret(*this);
    ret.m_t = double(hit.t);
    ret.m_dir = dir;
    ret.m_source = s;
    if(!std::isinf(hit.t) && !std::isnan(hit.t)) {
        ret.m_normal = this->normal_by_face_id(hit.id);
        ret.m_face_id = hit.id;
    }

    return ret;
}

std::vector<EigenMesh3D::hit_result>
EigenMesh3D::query_ray_hits(const Vec3d &s, const Vec3d &dir) const
{
    std::vector<EigenMesh3D::hit_result> outs;
    std::vector<igl::Hit> hits;
    m_aabb->intersect_ray(*m_tm, s, dir, hits);
    
    // The sort is necessary, the hits are not always sorted.
    std::sort(hits.begin(), hits.end(),
              [](const igl::Hit& a, const igl::Hit& b) { return a.t < b.t; });

    // Remove duplicates. They sometimes appear, for example when the ray is cast
    // along an axis of a cube due to floating-point approximations in igl (?)
    hits.erase(std::unique(hits.begin(), hits.end(),
                           [](const igl::Hit& a, const igl::Hit& b)
                              { return a.t == b.t; }),
               hits.end());

    //  Convert the igl::Hit into hit_result
    outs.reserve(hits.size());
    for (const igl::Hit& hit : hits) {
        outs.emplace_back(EigenMesh3D::hit_result(*this));
        outs.back().m_t = double(hit.t);
        outs.back().m_dir = dir;
        outs.back().m_source = s;
        if(!std::isinf(hit.t) && !std::isnan(hit.t)) {
            outs.back().m_normal = this->normal_by_face_id(hit.id);
            outs.back().m_face_id = hit.id;
        }
    }

    return outs;
}


#ifdef SLIC3R_HOLE_RAYCASTER
EigenMesh3D::hit_result EigenMesh3D::filter_hits(
                     const std::vector<EigenMesh3D::hit_result>& object_hits) const
{
    assert(! m_holes.empty());
    hit_result out(*this);

    if (object_hits.empty())
        return out;

    const Vec3d& s = object_hits.front().source();
    const Vec3d& dir = object_hits.front().direction();

    // A helper struct to save an intersetion with a hole
    struct HoleHit {
        HoleHit(float t_p, const Vec3d& normal_p, bool entry_p) :
            t(t_p), normal(normal_p), entry(entry_p) {}
        float t;
        Vec3d normal;
        bool entry;
    };
    std::vector<HoleHit> hole_isects;
    hole_isects.reserve(m_holes.size());
    
    auto sf = s.cast<float>();
    auto dirf = dir.cast<float>();

    // Collect hits on all holes, preserve information about entry/exit
    for (const sla::DrainHole& hole : m_holes) {
        std::array<std::pair<float, Vec3d>, 2> isects;
        if (hole.get_intersections(sf, dirf, isects)) {
            // Ignore hole hits behind the source
            if (isects[0].first > 0.f) hole_isects.emplace_back(isects[0].first, isects[0].second, true);
            if (isects[1].first > 0.f) hole_isects.emplace_back(isects[1].first, isects[1].second, false);
        }
    }

    // Holes can intersect each other, sort the hits by t
    std::sort(hole_isects.begin(), hole_isects.end(),
              [](const HoleHit& a, const HoleHit& b) { return a.t < b.t; });

    // Now inspect the intersections with object and holes, in the order of
    // increasing distance. Keep track how deep are we nested in mesh/holes and
    // pick the correct intersection.
    // This needs to be done twice - first to find out how deep in the structure
    // the source is, then to pick the correct intersection.
    int hole_nested = 0;
    int object_nested = 0;
    for (int dry_run=1; dry_run>=0; --dry_run) {
        hole_nested = -hole_nested;
        object_nested = -object_nested;

        bool is_hole = false;
        bool is_entry = false;
        const HoleHit* next_hole_hit = hole_isects.empty() ? nullptr : &hole_isects.front();
        const hit_result* next_mesh_hit = &object_hits.front();

        while (next_hole_hit || next_mesh_hit) {
            if (next_hole_hit && next_mesh_hit) // still have hole and obj hits
                is_hole = (next_hole_hit->t < next_mesh_hit->m_t);
            else
                is_hole = next_hole_hit; // one or the other ran out

            // Is this entry or exit hit?
            is_entry = is_hole ? next_hole_hit->entry : ! next_mesh_hit->is_inside();

            if (! dry_run) {
                if (! is_hole && hole_nested == 0) {
                    // This is a valid object hit
                    return *next_mesh_hit;
                }
                if (is_hole && ! is_entry && object_nested != 0) {
                    // This holehit is the one we seek
                    out.m_t = next_hole_hit->t;
                    out.m_normal = next_hole_hit->normal;
                    out.m_source = s;
                    out.m_dir = dir;
                    return out;
                }
            }

            // Increase/decrease the counter
            (is_hole ? hole_nested : object_nested) += (is_entry ? 1 : -1);

            // Advance the respective pointer
            if (is_hole && next_hole_hit++ == &hole_isects.back())
                next_hole_hit = nullptr;
            if (! is_hole && next_mesh_hit++ == &object_hits.back())
                next_mesh_hit = nullptr;
        }
    }

    // if we got here, the ray ended up in infinity
    return out;
}
#endif


double EigenMesh3D::squared_distance(const Vec3d &p, int& i, Vec3d& c) const {
    double sqdst = 0;
    Eigen::Matrix<double, 1, 3> pp = p;
    Eigen::Matrix<double, 1, 3> cc;
    sqdst = m_aabb->squared_distance(*m_tm, pp, i, cc);
    c = cc;
    return sqdst;
}

/* ****************************************************************************
 * Misc functions
 * ****************************************************************************/

namespace  {

bool point_on_edge(const Vec3d& p, const Vec3d& e1, const Vec3d& e2,
                   double eps = 0.05)
{
    using Line3D = Eigen::ParametrizedLine<double, 3>;
    
    auto line = Line3D::Through(e1, e2);
    double d = line.distance(p);
    return std::abs(d) < eps;
}

template<class Vec> double distance(const Vec& pp1, const Vec& pp2) {
    auto p = pp2 - pp1;
    return std::sqrt(p.transpose() * p);
}

}

PointSet normals(const PointSet& points,
                 const EigenMesh3D& mesh,
                 double eps,
                 std::function<void()> thr, // throw on cancel
                 const std::vector<unsigned>& pt_indices)
{
    if (points.rows() == 0 || mesh.vertices().empty() || mesh.indices().empty())
        return {};

    std::vector<unsigned> range = pt_indices;
    if (range.empty()) {
        range.resize(size_t(points.rows()), 0);
        std::iota(range.begin(), range.end(), 0);
    }

    PointSet ret(range.size(), 3);

    //    for (size_t ridx = 0; ridx < range.size(); ++ridx)
    ccr::enumerate(
        range.begin(), range.end(),
        [&ret, &mesh, &points, thr, eps](unsigned el, size_t ridx) {
            thr();
            auto  eidx   = Eigen::Index(el);
            int   faceid = 0;
            Vec3d p;

            mesh.squared_distance(points.row(eidx), faceid, p);

            auto trindex = mesh.indices(faceid);

            const Vec3d &p1 = mesh.vertices(trindex(0)).cast<double>();
            const Vec3d &p2 = mesh.vertices(trindex(1)).cast<double>();
            const Vec3d &p3 = mesh.vertices(trindex(2)).cast<double>();

            // We should check if the point lies on an edge of the hosting
            // triangle. If it does then all the other triangles using the
            // same two points have to be searched and the final normal should
            // be some kind of aggregation of the participating triangle
            // normals. We should also consider the cases where the support
            // point lies right on a vertex of its triangle. The procedure is
            // the same, get the neighbor triangles and calculate an average
            // normal.

            // mark the vertex indices of the edge. ia and ib marks and edge
            // ic will mark a single vertex.
            int ia = -1, ib = -1, ic = -1;

            if (std::abs(distance(p, p1)) < eps) {
                ic = trindex(0);
            } else if (std::abs(distance(p, p2)) < eps) {
                ic = trindex(1);
            } else if (std::abs(distance(p, p3)) < eps) {
                ic = trindex(2);
            } else if (point_on_edge(p, p1, p2, eps)) {
                ia = trindex(0);
                ib = trindex(1);
            } else if (point_on_edge(p, p2, p3, eps)) {
                ia = trindex(1);
                ib = trindex(2);
            } else if (point_on_edge(p, p1, p3, eps)) {
                ia = trindex(0);
                ib = trindex(2);
            }

            // vector for the neigboring triangles including the detected one.
            std::vector<size_t> neigh;
            if (ic >= 0) { // The point is right on a vertex of the triangle
                for (size_t n = 0; n < mesh.indices().size(); ++n) {
                    thr();
                    Vec3i ni = mesh.indices(n);
                    if ((ni(X) == ic || ni(Y) == ic || ni(Z) == ic))
                        neigh.emplace_back(n);
                }
            } else if (ia >= 0 && ib >= 0) { // the point is on and edge
                // now get all the neigboring triangles
                for (size_t n = 0; n < mesh.indices().size(); ++n) {
                    thr();
                    Vec3i ni = mesh.indices(n);
                    if ((ni(X) == ia || ni(Y) == ia || ni(Z) == ia) &&
                        (ni(X) == ib || ni(Y) == ib || ni(Z) == ib))
                        neigh.emplace_back(n);
                }
            }

            // Calculate the normals for the neighboring triangles
            std::vector<Vec3d> neighnorms;
            neighnorms.reserve(neigh.size());
            for (size_t &tri_id : neigh)
                neighnorms.emplace_back(mesh.normal_by_face_id(tri_id));

            // Throw out duplicates. They would cause trouble with summing. We
            // will use std::unique which works on sorted ranges. We will sort
            // by the coefficient-wise sum of the normals. It should force the
            // same elements to be consecutive.
            std::sort(neighnorms.begin(), neighnorms.end(),
                      [](const Vec3d &v1, const Vec3d &v2) {
                          return v1.sum() < v2.sum();
                      });

            auto lend = std::unique(neighnorms.begin(), neighnorms.end(),
                                    [](const Vec3d &n1, const Vec3d &n2) {
                                        // Compare normals for equivalence.
                                        // This is controvers stuff.
                                        auto deq = [](double a, double b) {
                                            return std::abs(a - b) < 1e-3;
                                        };
                                        return deq(n1(X), n2(X)) &&
                                               deq(n1(Y), n2(Y)) &&
                                               deq(n1(Z), n2(Z));
                                    });

            if (!neighnorms.empty()) { // there were neighbors to count with
                // sum up the normals and then normalize the result again.
                // This unification seems to be enough.
                Vec3d sumnorm(0, 0, 0);
                sumnorm = std::accumulate(neighnorms.begin(), lend, sumnorm);
                sumnorm.normalize();
                ret.row(long(ridx)) = sumnorm;
            } else { // point lies safely within its triangle
                Eigen::Vector3d U   = p2 - p1;
                Eigen::Vector3d V   = p3 - p1;
                ret.row(long(ridx)) = U.cross(V).normalized();
            }
        });

    return ret;
}

namespace bgi = boost::geometry::index;
using Index3D = bgi::rtree< PointIndexEl, bgi::rstar<16, 4> /* ? */ >;

namespace {

bool cmp_ptidx_elements(const PointIndexEl& e1, const PointIndexEl& e2)
{
    return e1.second < e2.second;
};

ClusteredPoints cluster(Index3D &sindex,
                        unsigned max_points,
                        std::function<std::vector<PointIndexEl>(
                            const Index3D &, const PointIndexEl &)> qfn)
{
    using Elems = std::vector<PointIndexEl>;
    
    // Recursive function for visiting all the points in a given distance to
    // each other
    std::function<void(Elems&, Elems&)> group =
        [&sindex, &group, max_points, qfn](Elems& pts, Elems& cluster)
    {        
        for(auto& p : pts) {
            std::vector<PointIndexEl> tmp = qfn(sindex, p);
            
            std::sort(tmp.begin(), tmp.end(), cmp_ptidx_elements);
            
            Elems newpts;
            std::set_difference(tmp.begin(), tmp.end(),
                                cluster.begin(), cluster.end(),
                                std::back_inserter(newpts), cmp_ptidx_elements);
            
            int c = max_points && newpts.size() + cluster.size() > max_points?
                        int(max_points - cluster.size()) : int(newpts.size());
            
            cluster.insert(cluster.end(), newpts.begin(), newpts.begin() + c);
            std::sort(cluster.begin(), cluster.end(), cmp_ptidx_elements);
            
            if(!newpts.empty() && (!max_points || cluster.size() < max_points))
                group(newpts, cluster);
        }
    };
    
    std::vector<Elems> clusters;
    for(auto it = sindex.begin(); it != sindex.end();) {
        Elems cluster = {};
        Elems pts = {*it};
        group(pts, cluster);
        
        for(auto& c : cluster) sindex.remove(c);
        it = sindex.begin();
        
        clusters.emplace_back(cluster);
    }
    
    ClusteredPoints result;
    for(auto& cluster : clusters) {
        result.emplace_back();
        for(auto c : cluster) result.back().emplace_back(c.second);
    }
    
    return result;
}

std::vector<PointIndexEl> distance_queryfn(const Index3D& sindex,
                                           const PointIndexEl& p,
                                           double dist,
                                           unsigned max_points)
{
    std::vector<PointIndexEl> tmp; tmp.reserve(max_points);
    sindex.query(
        bgi::nearest(p.first, max_points),
        std::back_inserter(tmp)
        );
    
    for(auto it = tmp.begin(); it < tmp.end(); ++it)
        if(distance(p.first, it->first) > dist) it = tmp.erase(it);
    
    return tmp;
}

} // namespace

// Clustering a set of points by the given criteria
ClusteredPoints cluster(
    const std::vector<unsigned>& indices,
    std::function<Vec3d(unsigned)> pointfn,
    double dist,
    unsigned max_points)
{
    // A spatial index for querying the nearest points
    Index3D sindex;
    
    // Build the index
    for(auto idx : indices) sindex.insert( std::make_pair(pointfn(idx), idx));
    
    return cluster(sindex, max_points,
                   [dist, max_points](const Index3D& sidx, const PointIndexEl& p)
                   {
                       return distance_queryfn(sidx, p, dist, max_points);
                   });
}

// Clustering a set of points by the given criteria
ClusteredPoints cluster(
    const std::vector<unsigned>& indices,
    std::function<Vec3d(unsigned)> pointfn,
    std::function<bool(const PointIndexEl&, const PointIndexEl&)> predicate,
    unsigned max_points)
{
    // A spatial index for querying the nearest points
    Index3D sindex;
    
    // Build the index
    for(auto idx : indices) sindex.insert( std::make_pair(pointfn(idx), idx));
    
    return cluster(sindex, max_points,
                   [max_points, predicate](const Index3D& sidx, const PointIndexEl& p)
                   {
                       std::vector<PointIndexEl> tmp; tmp.reserve(max_points);
                       sidx.query(bgi::satisfies([p, predicate](const PointIndexEl& e){
                                      return predicate(p, e);
                                  }), std::back_inserter(tmp));
                       return tmp;
                   });
}

ClusteredPoints cluster(const PointSet& pts, double dist, unsigned max_points)
{
    // A spatial index for querying the nearest points
    Index3D sindex;
    
    // Build the index
    for(Eigen::Index i = 0; i < pts.rows(); i++)
        sindex.insert(std::make_pair(Vec3d(pts.row(i)), unsigned(i)));
    
    return cluster(sindex, max_points,
                   [dist, max_points](const Index3D& sidx, const PointIndexEl& p)
                   {
                       return distance_queryfn(sidx, p, dist, max_points);
                   });
}

} // namespace sla
} // namespace Slic3r
