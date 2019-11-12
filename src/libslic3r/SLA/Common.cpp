#include <cmath>
#include <libslic3r/SLA/Common.hpp>
#include <libslic3r/SLA/Concurrency.hpp>
#include <libslic3r/SLA/SupportTree.hpp>
#include <libslic3r/SLA/SpatIndex.hpp>
#include <libslic3r/SLA/EigenMesh3D.hpp>
#include <libslic3r/SLA/Contour3D.hpp>
#include <libslic3r/SLA/Clustering.hpp>


// Workaround: IGL signed_distance.h will define PI in the igl namespace.
#undef PI

// HEAVY headers... takes eternity to compile

// for concave hull merging decisions
#include <libslic3r/SLA/BoostAdapter.hpp>
#include "boost/geometry/index/rtree.hpp"

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4244)
#pragma warning(disable: 4267)
#endif
#include <igl/ray_mesh_intersect.h>
#include <igl/point_mesh_squared_distance.h>
#include <igl/remove_duplicate_vertices.h>
#include <igl/signed_distance.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <tbb/parallel_for.h>

#include "ClipperUtils.hpp"

namespace Slic3r {
namespace sla {

// Bring back PI from the igl namespace
using igl::PI;

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

class EigenMesh3D::AABBImpl: public igl::AABB<Eigen::MatrixXd, 3> {
public:
#ifdef SLIC3R_SLA_NEEDS_WINDTREE
    igl::WindingNumberAABB<Vec3d, Eigen::MatrixXd, Eigen::MatrixXi> windtree;
#endif /* SLIC3R_SLA_NEEDS_WINDTREE */
};

EigenMesh3D::EigenMesh3D(const TriangleMesh& tmesh): m_aabb(new AABBImpl()) {
    static const double dEPS = 1e-6;
    
    const stl_file& stl = tmesh.stl;
    
    auto&& bb = tmesh.bounding_box();
    m_ground_level += bb.min(Z);
    
    Eigen::MatrixXd V;
    Eigen::MatrixXi F;
    
    V.resize(3*stl.stats.number_of_facets, 3);
    F.resize(stl.stats.number_of_facets, 3);
    for (unsigned int i = 0; i < stl.stats.number_of_facets; ++i) {
        const stl_facet &facet = stl.facet_start[i];
        V.block<1, 3>(3 * i + 0, 0) = facet.vertex[0].cast<double>();
        V.block<1, 3>(3 * i + 1, 0) = facet.vertex[1].cast<double>();
        V.block<1, 3>(3 * i + 2, 0) = facet.vertex[2].cast<double>();
        F(i, 0) = int(3*i+0);
        F(i, 1) = int(3*i+1);
        F(i, 2) = int(3*i+2);
    }
    
    // We will convert this to a proper 3d mesh with no duplicate points.
    Eigen::VectorXi SVI, SVJ;
    igl::remove_duplicate_vertices(V, F, dEPS, m_V, SVI, SVJ, m_F);
    
    // Build the AABB accelaration tree
    m_aabb->init(m_V, m_F);
#ifdef SLIC3R_SLA_NEEDS_WINDTREE
    m_aabb->windtree.set_mesh(m_V, m_F);
#endif /* SLIC3R_SLA_NEEDS_WINDTREE */
}

EigenMesh3D::~EigenMesh3D() {}

EigenMesh3D::EigenMesh3D(const EigenMesh3D &other):
    m_V(other.m_V), m_F(other.m_F), m_ground_level(other.m_ground_level),
    m_aabb( new AABBImpl(*other.m_aabb) ) {}

EigenMesh3D::EigenMesh3D(const Contour3D &other)
{
    m_V.resize(Eigen::Index(other.points.size()), 3);
    m_F.resize(Eigen::Index(other.faces3.size() + 2 * other.faces4.size()), 3);
    
    for (Eigen::Index i = 0; i < Eigen::Index(other.points.size()); ++i)
        m_V.row(i) = other.points[size_t(i)];
    
    for (Eigen::Index i = 0; i < Eigen::Index(other.faces3.size()); ++i)
        m_F.row(i) = other.faces3[size_t(i)];
    
    size_t N = other.faces3.size() + 2 * other.faces4.size();
    for (size_t i = other.faces3.size(); i < N; i += 2) {
        size_t quad_idx = (i - other.faces3.size()) / 2;
        auto & quad     = other.faces4[quad_idx];
        m_F.row(Eigen::Index(i)) = Vec3i{quad(0), quad(1), quad(2)};
        m_F.row(Eigen::Index(i + 1)) = Vec3i{quad(2), quad(3), quad(0)};
    }
}

EigenMesh3D &EigenMesh3D::operator=(const EigenMesh3D &other)
{
    m_V = other.m_V;
    m_F = other.m_F;
    m_ground_level = other.m_ground_level;
    m_aabb.reset(new AABBImpl(*other.m_aabb)); return *this;
}

EigenMesh3D::hit_result
EigenMesh3D::query_ray_hit(const Vec3d &s, const Vec3d &dir) const
{
    igl::Hit hit;
    hit.t = std::numeric_limits<float>::infinity();
    m_aabb->intersect_ray(m_V, m_F, s, dir, hit);
    
    hit_result ret(*this);
    ret.m_t = double(hit.t);
    ret.m_dir = dir;
    ret.m_source = s;
    if(!std::isinf(hit.t) && !std::isnan(hit.t)) ret.m_face_id = hit.id;
    
    return ret;
}

std::vector<EigenMesh3D::hit_result>
EigenMesh3D::query_ray_hits(const Vec3d &s, const Vec3d &dir) const
{
    std::vector<EigenMesh3D::hit_result> outs;
    std::vector<igl::Hit> hits;
    m_aabb->intersect_ray(m_V, m_F, s, dir, hits);
    
    // The sort is necessary, the hits are not always sorted.
    std::sort(hits.begin(), hits.end(),
              [](const igl::Hit& a, const igl::Hit& b) { return a.t < b.t; });
    
    //  Convert the igl::Hit into hit_result
    outs.reserve(hits.size());
    for (const igl::Hit& hit : hits) {
        outs.emplace_back(EigenMesh3D::hit_result(*this));
        outs.back().m_t = double(hit.t);
        outs.back().m_dir = dir;
        outs.back().m_source = s;
        if(!std::isinf(hit.t) && !std::isnan(hit.t))
            outs.back().m_face_id = hit.id;
    }
    
    return outs;
}

#ifdef SLIC3R_SLA_NEEDS_WINDTREE
EigenMesh3D::si_result EigenMesh3D::signed_distance(const Vec3d &p) const {
    double sign = 0; double sqdst = 0; int i = 0;  Vec3d c;
    igl::signed_distance_winding_number(*m_aabb, m_V, m_F, m_aabb->windtree,
                                        p, sign, sqdst, i, c);
    
    return si_result(sign * std::sqrt(sqdst), i, c);
}

bool EigenMesh3D::inside(const Vec3d &p) const {
    return m_aabb->windtree.inside(p);
}
#endif /* SLIC3R_SLA_NEEDS_WINDTREE */

double EigenMesh3D::squared_distance(const Vec3d &p, int& i, Vec3d& c) const {
    double sqdst = 0;
    Eigen::Matrix<double, 1, 3> pp = p;
    Eigen::Matrix<double, 1, 3> cc;
    sqdst = m_aabb->squared_distance(m_V, m_F, pp, i, cc);
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
    if (points.rows() == 0 || mesh.V().rows() == 0 || mesh.F().rows() == 0)
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

            auto trindex = mesh.F().row(faceid);

            const Vec3d &p1 = mesh.V().row(trindex(0));
            const Vec3d &p2 = mesh.V().row(trindex(1));
            const Vec3d &p3 = mesh.V().row(trindex(2));

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
            std::vector<Vec3i> neigh;
            if (ic >= 0) { // The point is right on a vertex of the triangle
                for (int n = 0; n < mesh.F().rows(); ++n) {
                    thr();
                    Vec3i ni = mesh.F().row(n);
                    if ((ni(X) == ic || ni(Y) == ic || ni(Z) == ic))
                        neigh.emplace_back(ni);
                }
            } else if (ia >= 0 && ib >= 0) { // the point is on and edge
                // now get all the neigboring triangles
                for (int n = 0; n < mesh.F().rows(); ++n) {
                    thr();
                    Vec3i ni = mesh.F().row(n);
                    if ((ni(X) == ia || ni(Y) == ia || ni(Z) == ia) &&
                        (ni(X) == ib || ni(Y) == ib || ni(Z) == ib))
                        neigh.emplace_back(ni);
                }
            }

            // Calculate the normals for the neighboring triangles
            std::vector<Vec3d> neighnorms;
            neighnorms.reserve(neigh.size());
            for (const Vec3i &tri : neigh) {
                const Vec3d &   pt1 = mesh.V().row(tri(0));
                const Vec3d &   pt2 = mesh.V().row(tri(1));
                const Vec3d &   pt3 = mesh.V().row(tri(2));
                Eigen::Vector3d U   = pt2 - pt1;
                Eigen::Vector3d V   = pt3 - pt1;
                neighnorms.emplace_back(U.cross(V).normalized());
            }

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
