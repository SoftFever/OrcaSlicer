#include <cmath>
#include "SLA/SLASupportTree.hpp"
#include "SLA/SLABoilerPlate.hpp"
#include "SLA/SLASpatIndex.hpp"

// HEAVY headers... takes eternity to compile

// for concave hull merging decisions
#include "SLABoostAdapter.hpp"
#include "boost/geometry/index/rtree.hpp"

#include <igl/ray_mesh_intersect.h>
#include <igl/point_mesh_squared_distance.h>
#include <igl/remove_duplicate_vertices.h>

#include "SLASpatIndex.hpp"
#include "ClipperUtils.hpp"

namespace Slic3r {
namespace sla {

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

PointSet normals(const PointSet& points, const EigenMesh3D& emesh,
                 double eps,
                 std::function<void()> throw_on_cancel) {
    if(points.rows() == 0 || emesh.V.rows() == 0 || emesh.F.rows() == 0)
        return {};

    Eigen::VectorXd dists;
    Eigen::VectorXi I;
    PointSet C;

    // We need to remove duplicate vertices and have a true index triangle
    // structure
    EigenMesh3D  mesh;
    Eigen::VectorXi SVI, SVJ;
    igl::remove_duplicate_vertices(emesh.V, emesh.F, 1e-6,
                                   mesh.V, SVI, SVJ, mesh.F);

    igl::point_mesh_squared_distance( points, mesh.V, mesh.F, dists, I, C);

    PointSet ret(I.rows(), 3);
    for(int i = 0; i < I.rows(); i++) {
        throw_on_cancel();
        auto idx = I(i);
        auto trindex = mesh.F.row(idx);

        const Vec3d& p1 = mesh.V.row(trindex(0));
        const Vec3d& p2 = mesh.V.row(trindex(1));
        const Vec3d& p3 = mesh.V.row(trindex(2));

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

        std::vector<Vec3i> neigh;
        if(ic >= 0) { // The point is right on a vertex of the triangle
            for(int n = 0; n < mesh.F.rows(); ++n) {
                throw_on_cancel();
                Vec3i ni = mesh.F.row(n);
                if((ni(X) == ic || ni(Y) == ic || ni(Z) == ic))
                    neigh.emplace_back(ni);
            }
        }
        else if(ia >= 0 && ib >= 0) { // the point is on and edge
            // now get all the neigboring triangles
            for(int n = 0; n < mesh.F.rows(); ++n) {
                throw_on_cancel();
                Vec3i ni = mesh.F.row(n);
                if((ni(X) == ia || ni(Y) == ia || ni(Z) == ia) &&
                   (ni(X) == ib || ni(Y) == ib || ni(Z) == ib))
                    neigh.emplace_back(ni);
            }
        }

        if(!neigh.empty()) { // there were neighbors to count with
            Vec3d sumnorm(0, 0, 0);
            for(const Vec3i& tri : neigh) {
                const Vec3d& pt1 = mesh.V.row(tri(0));
                const Vec3d& pt2 = mesh.V.row(tri(1));
                const Vec3d& pt3 = mesh.V.row(tri(2));
                Eigen::Vector3d U = pt2 - pt1;
                Eigen::Vector3d V = pt3 - pt1;
                sumnorm += U.cross(V).normalized();
            }
            sumnorm /= neigh.size();
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

double ray_mesh_intersect(const Vec3d& s,
                          const Vec3d& dir,
                          const EigenMesh3D& m)
{
    igl::Hit hit;
    hit.t = std::numeric_limits<float>::infinity();
    igl::ray_mesh_intersect(s, dir, m.V, m.F, hit);
    return double(hit.t);
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

using Segments = std::vector<std::pair<Vec2d, Vec2d>>;

Segments model_boundary(const EigenMesh3D& emesh, double offs)
{
    Segments ret;
    Polygons pp;
    pp.reserve(size_t(emesh.F.rows()));

    for (int i = 0; i < emesh.F.rows(); i++) {
        auto trindex = emesh.F.row(i);
        auto& p1 = emesh.V.row(trindex(0));
        auto& p2 = emesh.V.row(trindex(1));
        auto& p3 = emesh.V.row(trindex(2));

        Polygon p;
        p.points.resize(3);
        p.points[0] = Point::new_scale(p1(X), p1(Y));
        p.points[1] = Point::new_scale(p2(X), p2(Y));
        p.points[2] = Point::new_scale(p3(X), p3(Y));
        p.make_counter_clockwise();
        pp.emplace_back(p);
    }

    ExPolygons merged = union_ex(Slic3r::offset(pp, float(scale_(offs))), true);

    for(auto& expoly : merged) {
        auto lines = expoly.lines();
        for(Line& l : lines) {
            Vec2d a(l.a(X) * SCALING_FACTOR, l.a(Y) * SCALING_FACTOR);
            Vec2d b(l.b(X) * SCALING_FACTOR, l.b(Y) * SCALING_FACTOR);
            ret.emplace_back(std::make_pair(a, b));
        }
    }

    return ret;
}

//struct SegmentIndex {

//};

//using SegmentIndexEl = std::pair<Segment, unsigned>;

//SegmentIndexEl




}
}
