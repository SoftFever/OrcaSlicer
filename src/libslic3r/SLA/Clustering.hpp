#ifndef SLA_CLUSTERING_HPP
#define SLA_CLUSTERING_HPP

#include <vector>
#include <libslic3r/SLA/Common.hpp>
#include <libslic3r/SLA/SpatIndex.hpp>

namespace Slic3r { namespace sla {

using ClusterEl = std::vector<unsigned>;
using ClusteredPoints = std::vector<ClusterEl>;

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

}}
#endif // CLUSTERING_HPP
