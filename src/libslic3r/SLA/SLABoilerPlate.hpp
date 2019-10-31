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

typedef Eigen::Matrix<int, 4, 1, Eigen::DontAlign> Vec4i;

namespace sla {

/// Intermediate struct for a 3D mesh
struct Contour3D {
    Pointf3s points;
    std::vector<Vec3i> faces3;
    std::vector<Vec4i> faces4;

    Contour3D& merge(const Contour3D& ctr)
    {
        auto N = coord_t(points.size());
        auto N_f3 = faces3.size();
        auto N_f4 = faces4.size();

        points.insert(points.end(), ctr.points.begin(), ctr.points.end());
        faces3.insert(faces3.end(), ctr.faces3.begin(), ctr.faces3.end());
        faces4.insert(faces4.end(), ctr.faces4.begin(), ctr.faces4.end());

        for(size_t n = N_f3; n < faces3.size(); n++) {
            auto& idx = faces3[n]; idx.x() += N; idx.y() += N; idx.z() += N;
        }
        
        for(size_t n = N_f4; n < faces4.size(); n++) {
            auto& idx = faces4[n]; for (int k = 0; k < 4; k++) idx(k) += N;
        }        
        
        return *this;
    }

    Contour3D& merge(const Pointf3s& triangles)
    {
        const size_t offs = points.size();
        points.insert(points.end(), triangles.begin(), triangles.end());
        faces3.reserve(faces3.size() + points.size() / 3);
        
        for(int i = int(offs); i < int(points.size()); i += 3)
            faces3.emplace_back(i, i + 1, i + 2);
        
        return *this;
    }

    // Write the index triangle structure to OBJ file for debugging purposes.
    void to_obj(std::ostream& stream)
    {
        for(auto& p : points) {
            stream << "v " << p.transpose() << "\n";
        }

        for(auto& f : faces3) {
            stream << "f " << (f + Vec3i(1, 1, 1)).transpose() << "\n";
        }
        
        for(auto& f : faces4) {
            stream << "f " << (f + Vec4i(1, 1, 1, 1)).transpose() << "\n";
        }
    }
    
    bool empty() const { return points.empty() || (faces4.empty() && faces3.empty()); }
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
                 const EigenMesh3D& convert_mesh,
                 double eps = 0.05,  // min distance from edges
                 std::function<void()> throw_on_cancel = [](){},
                 const std::vector<unsigned>& selected_points = {});

/// Mesh from an existing contour.
inline TriangleMesh convert_mesh(const Contour3D& ctour) {
    return {ctour.points, ctour.faces3};
}

/// Mesh from an evaporating 3D contour
inline TriangleMesh convert_mesh(Contour3D&& ctour) {
    return {std::move(ctour.points), std::move(ctour.faces3)};
}

inline Contour3D convert_mesh(const TriangleMesh &trmesh) {
    Contour3D ret;
    ret.points.reserve(trmesh.its.vertices.size());
    ret.faces3.reserve(trmesh.its.indices.size());
    
    for (auto &v : trmesh.its.vertices)
        ret.points.emplace_back(v.cast<double>());
    
    std::copy(trmesh.its.indices.begin(), trmesh.its.indices.end(),
              std::back_inserter(ret.faces3));

    return ret;
}

inline Contour3D convert_mesh(TriangleMesh &&trmesh) {
    Contour3D ret;
    ret.points.reserve(trmesh.its.vertices.size());
    
    for (auto &v : trmesh.its.vertices)
        ret.points.emplace_back(v.cast<double>());
    
    ret.faces3.swap(trmesh.its.indices);
    
    return ret;
}

}
}

#endif // SLABOILERPLATE_HPP
