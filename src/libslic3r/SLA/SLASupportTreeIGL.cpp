#include <cmath>
#include "SLA/SLASupportTree.hpp"
#include "SLA/SLABoilerPlate.hpp"
#include "SLA/SLASpatIndex.hpp"

// Workaround: IGL signed_distance.h will define PI in the igl namespace.
#undef PI

// HEAVY headers... takes eternity to compile

// for concave hull merging decisions
#include "SLABoostAdapter.hpp"
#include "boost/geometry/index/rtree.hpp"

#include <igl/ray_mesh_intersect.h>
#include <igl/point_mesh_squared_distance.h>
#include <igl/remove_duplicate_vertices.h>
#include <igl/signed_distance.h>

#include "SLASpatIndex.hpp"
#include "ClipperUtils.hpp"

namespace Slic3r {
namespace sla {

// Bring back PI from the igl namespace
using igl::PI;

/* **************************************************************************
 * SpatIndex implementation
 * ************************************************************************** */

class SpatIndex::Impl {
public:
    using BoostIndex = boost::geometry::index::rtree< SpatElement,
                       boost::geometry::index::rstar<16, 4> /* ? */ >;

    BoostIndex m_store;
};

SpatIndex::SpatIndex(): m_impl(new Impl()) {}
SpatIndex::~SpatIndex() {}

SpatIndex::SpatIndex(const SpatIndex &cpy): m_impl(new Impl(*cpy.m_impl)) {}
SpatIndex::SpatIndex(SpatIndex&& cpy): m_impl(std::move(cpy.m_impl)) {}

SpatIndex& SpatIndex::operator=(const SpatIndex &cpy)
{
    m_impl.reset(new Impl(*cpy.m_impl));
    return *this;
}

SpatIndex& SpatIndex::operator=(SpatIndex &&cpy)
{
    m_impl.swap(cpy.m_impl);
    return *this;
}

void SpatIndex::insert(const SpatElement &el)
{
    m_impl->m_store.insert(el);
}

bool SpatIndex::remove(const SpatElement& el)
{
    return m_impl->m_store.remove(el) == 1;
}

std::vector<SpatElement>
SpatIndex::query(std::function<bool(const SpatElement &)> fn)
{
    namespace bgi = boost::geometry::index;

    std::vector<SpatElement> ret;
    m_impl->m_store.query(bgi::satisfies(fn), std::back_inserter(ret));
    return ret;
}

std::vector<SpatElement> SpatIndex::nearest(const Vec3d &el, unsigned k = 1)
{
    namespace bgi = boost::geometry::index;
    std::vector<SpatElement> ret; ret.reserve(k);
    m_impl->m_store.query(bgi::nearest(el, k), std::back_inserter(ret));
    return ret;
}

size_t SpatIndex::size() const
{
    return m_impl->m_store.size();
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
        const stl_facet* facet = stl.facet_start+i;
        V(3*i+0, 0) = double(facet->vertex[0](0));
        V(3*i+0, 1) = double(facet->vertex[0](1));
        V(3*i+0, 2) = double(facet->vertex[0](2));

        V(3*i+1, 0) = double(facet->vertex[1](0));
        V(3*i+1, 1) = double(facet->vertex[1](1));
        V(3*i+1, 2) = double(facet->vertex[1](2));

        V(3*i+2, 0) = double(facet->vertex[2](0));
        V(3*i+2, 1) = double(facet->vertex[2](1));
        V(3*i+2, 2) = double(facet->vertex[2](2));

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
    if(!std::isinf(hit.t) && !std::isnan(hit.t)) ret.m_face_id = hit.id;

    return ret;
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

/* ****************************************************************************
 * Misc functions
 * ****************************************************************************/

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

PointSet normals(const PointSet& points, const EigenMesh3D& mesh,
                 double eps,
                 std::function<void()> throw_on_cancel) {
    if(points.rows() == 0 || mesh.V().rows() == 0 || mesh.F().rows() == 0)
        return {};

    Eigen::VectorXd dists;
    Eigen::VectorXi I;
    PointSet C;

    igl::point_mesh_squared_distance( points, mesh.V(), mesh.F(), dists, I, C);

    PointSet ret(I.rows(), 3);
    for(int i = 0; i < I.rows(); i++) {
        throw_on_cancel();
        auto idx = I(i);
        auto trindex = mesh.F().row(idx);

        const Vec3d& p1 = mesh.V().row(trindex(0));
        const Vec3d& p2 = mesh.V().row(trindex(1));
        const Vec3d& p3 = mesh.V().row(trindex(2));

        // We should check if the point lies on an edge of the hosting triangle.
        // If it does than all the other triangles using the same two points
        // have to be searched and the final normal should be some kind of
        // aggregation of the participating triangle normals. We should also
        // consider the cases where the support point lies right on a vertex
        // of its triangle. The procedure is the same, get the neighbor
        // triangles and calculate an average normal.

        const Vec3d& p = C.row(i);

        // mark the vertex indices of the edge. ia and ib marks and edge ic
        // will mark a single vertex.
        int ia = -1, ib = -1, ic = -1;

        if(std::abs(distance(p, p1)) < eps) {
            ic = trindex(0);
        }
        else if(std::abs(distance(p, p2)) < eps) {
            ic = trindex(1);
        }
        else if(std::abs(distance(p, p3)) < eps) {
            ic = trindex(2);
        }
        else if(point_on_edge(p, p1, p2, eps)) {
            ia = trindex(0); ib = trindex(1);
        }
        else if(point_on_edge(p, p2, p3, eps)) {
            ia = trindex(1); ib = trindex(2);
        }
        else if(point_on_edge(p, p1, p3, eps)) {
            ia = trindex(0); ib = trindex(2);
        }

        // vector for the neigboring triangles including the detected one.
        std::vector<Vec3i> neigh;
        if(ic >= 0) { // The point is right on a vertex of the triangle
            for(int n = 0; n < mesh.F().rows(); ++n) {
                throw_on_cancel();
                Vec3i ni = mesh.F().row(n);
                if((ni(X) == ic || ni(Y) == ic || ni(Z) == ic))
                    neigh.emplace_back(ni);
            }
        }
        else if(ia >= 0 && ib >= 0) { // the point is on and edge
            // now get all the neigboring triangles
            for(int n = 0; n < mesh.F().rows(); ++n) {
                throw_on_cancel();
                Vec3i ni = mesh.F().row(n);
                if((ni(X) == ia || ni(Y) == ia || ni(Z) == ia) &&
                   (ni(X) == ib || ni(Y) == ib || ni(Z) == ib))
                    neigh.emplace_back(ni);
            }
        }

        // Calculate the normals for the neighboring triangles
        std::vector<Vec3d> neighnorms; neighnorms.reserve(neigh.size());
        for(const Vec3i& tri : neigh) {
            const Vec3d& pt1 = mesh.V().row(tri(0));
            const Vec3d& pt2 = mesh.V().row(tri(1));
            const Vec3d& pt3 = mesh.V().row(tri(2));
            Eigen::Vector3d U = pt2 - pt1;
            Eigen::Vector3d V = pt3 - pt1;
            neighnorms.emplace_back(U.cross(V).normalized());
        }

        // Throw out duplicates. They would cause trouble with summing. We will
        // use std::unique which works on sorted ranges. We will sort by the
        // coefficient-wise sum of the normals. It should force the same
        // elements to be consecutive.
        std::sort(neighnorms.begin(), neighnorms.end(),
                  [](const Vec3d& v1, const Vec3d& v2){
            return v1.sum() < v2.sum();
        });

        auto lend = std::unique(neighnorms.begin(), neighnorms.end(),
                                [](const Vec3d& n1, const Vec3d& n2) {
            // Compare normals for equivalence. This is controvers stuff.
            auto deq = [](double a, double b) { return std::abs(a-b) < 1e-3; };
            return deq(n1(X), n2(X)) && deq(n1(Y), n2(Y)) && deq(n1(Z), n2(Z));
        });

        if(!neighnorms.empty()) { // there were neighbors to count with
            // sum up the normals and then normalize the result again.
            // This unification seems to be enough.
            Vec3d sumnorm(0, 0, 0);
            sumnorm = std::accumulate(neighnorms.begin(), lend, sumnorm);
            sumnorm.normalize();
            ret.row(i) = sumnorm;
        }
        else { // point lies safely within its triangle
            Eigen::Vector3d U = p2 - p1;
            Eigen::Vector3d V = p3 - p1;
            ret.row(i) = U.cross(V).normalized();
        }
    }

    return ret;
}

// Clustering a set of points by the given criteria
ClusteredPoints cluster(
        const sla::PointSet& points,
        std::function<bool(const SpatElement&, const SpatElement&)> pred,
        unsigned max_points = 0)
{

    namespace bgi = boost::geometry::index;
    using Index3D = bgi::rtree< SpatElement, bgi::rstar<16, 4> /* ? */ >;

    // A spatial index for querying the nearest points
    Index3D sindex;

    // Build the index
    for(unsigned idx = 0; idx < points.rows(); idx++)
        sindex.insert( std::make_pair(points.row(idx), idx));

    using Elems = std::vector<SpatElement>;

    // Recursive function for visiting all the points in a given distance to
    // each other
    std::function<void(Elems&, Elems&)> group =
    [&sindex, &group, pred, max_points](Elems& pts, Elems& cluster)
    {
        for(auto& p : pts) {
            std::vector<SpatElement> tmp;

            sindex.query(
                bgi::satisfies([p, pred](const SpatElement& se) {
                    return pred(p, se);
                }),
                std::back_inserter(tmp)
            );

            auto cmp = [](const SpatElement& e1, const SpatElement& e2){
                return e1.second < e2.second;
            };

            std::sort(tmp.begin(), tmp.end(), cmp);

            Elems newpts;
            std::set_difference(tmp.begin(), tmp.end(),
                                cluster.begin(), cluster.end(),
                                std::back_inserter(newpts), cmp);

            int c = max_points && newpts.size() + cluster.size() > max_points?
                        int(max_points - cluster.size()) : int(newpts.size());

            cluster.insert(cluster.end(), newpts.begin(), newpts.begin() + c);
            std::sort(cluster.begin(), cluster.end(), cmp);

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

}
}
