#ifndef SLABOILERPLATE_HPP
#define SLABOILERPLATE_HPP

#include <iostream>
#include <functional>
#include <numeric>

#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/TriangleMesh.hpp>

#include "SLACommon.hpp"
#include "SLASpatIndex.hpp"

namespace Slic3r {
namespace sla {

/// Intermediate struct for a 3D mesh
struct Contour3D {
    Pointf3s points;
    std::vector<Vec3i> indices;

    Contour3D& merge(const Contour3D& ctr)
    {
        auto s3 = coord_t(points.size());
        auto s = indices.size();

        points.insert(points.end(), ctr.points.begin(), ctr.points.end());
        indices.insert(indices.end(), ctr.indices.begin(), ctr.indices.end());

        for(size_t n = s; n < indices.size(); n++) {
            auto& idx = indices[n]; idx.x() += s3; idx.y() += s3; idx.z() += s3;
        }
        
        return *this;
    }

    Contour3D& merge(const Pointf3s& triangles)
    {
        const size_t offs = points.size();
        points.insert(points.end(), triangles.begin(), triangles.end());
        indices.reserve(indices.size() + points.size() / 3);
        
        for(int i = int(offs); i < int(points.size()); i += 3)
            indices.emplace_back(i, i + 1, i + 2);
        
        return *this;
    }

    // Write the index triangle structure to OBJ file for debugging purposes.
    void to_obj(std::ostream& stream)
    {
        for(auto& p : points) {
            stream << "v " << p.transpose() << "\n";
        }

        for(auto& f : indices) {
            stream << "f " << (f + Vec3i(1, 1, 1)).transpose() << "\n";
        }
    }
};

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


// Calculate the normals for the selected points (from 'points' set) on the
// mesh. This will call squared distance for each point.
PointSet normals(const PointSet& points,
                 const EigenMesh3D& mesh,
                 double eps = 0.05,  // min distance from edges
                 std::function<void()> throw_on_cancel = [](){},
                 const std::vector<unsigned>& selected_points = {});

/// Mesh from an existing contour.
inline TriangleMesh mesh(const Contour3D& ctour) {
    return {ctour.points, ctour.indices};
}

/// Mesh from an evaporating 3D contour
inline TriangleMesh mesh(Contour3D&& ctour) {
    return {std::move(ctour.points), std::move(ctour.indices)};
}

}
}

#endif // SLABOILERPLATE_HPP
