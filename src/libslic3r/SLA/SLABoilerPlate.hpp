#ifndef SLABOILERPLATE_HPP
#define SLABOILERPLATE_HPP

#include <iostream>
#include <functional>
#include <numeric>

#include <libslic3r/ExPolygon.hpp>
#include <libslic3r/TriangleMesh.hpp>

namespace Slic3r {
namespace sla {

using coord_t = Point::coord_type;

/// get the scaled clipper units for a millimeter value
inline coord_t mm(double v) { return coord_t(v/SCALING_FACTOR); }

/// Get x and y coordinates (because we are eigenizing...)
inline coord_t x(const Point& p) { return p(0); }
inline coord_t y(const Point& p) { return p(1); }
inline coord_t& x(Point& p) { return p(0); }
inline coord_t& y(Point& p) { return p(1); }

inline coordf_t x(const Vec3d& p) { return p(0); }
inline coordf_t y(const Vec3d& p) { return p(1); }
inline coordf_t z(const Vec3d& p) { return p(2); }
inline coordf_t& x(Vec3d& p) { return p(0); }
inline coordf_t& y(Vec3d& p) { return p(1); }
inline coordf_t& z(Vec3d& p) { return p(2); }

inline coord_t& x(Vec3crd& p) { return p(0); }
inline coord_t& y(Vec3crd& p) { return p(1); }
inline coord_t& z(Vec3crd& p) { return p(2); }
inline coord_t x(const Vec3crd& p) { return p(0); }
inline coord_t y(const Vec3crd& p) { return p(1); }
inline coord_t z(const Vec3crd& p) { return p(2); }

using Indices = std::vector<Vec3crd>;

/// Intermediate struct for a 3D mesh
struct Contour3D {
    Pointf3s points;
    Indices indices;

    void merge(const Contour3D& ctr) {
        auto s3 = coord_t(points.size());
        auto s = indices.size();

        points.insert(points.end(), ctr.points.begin(), ctr.points.end());
        indices.insert(indices.end(), ctr.indices.begin(), ctr.indices.end());

        for(size_t n = s; n < indices.size(); n++) {
            auto& idx = indices[n]; x(idx) += s3; y(idx) += s3; z(idx) += s3;
        }
    }

    void merge(const Pointf3s& triangles) {
        const size_t offs = points.size();
        points.insert(points.end(), triangles.begin(), triangles.end());
        indices.reserve(indices.size() + points.size() / 3);

        for(size_t i = offs; i < points.size(); i += 3)
            indices.emplace_back(i, i + 1, i + 2);
    }

    // Write the index triangle structure to OBJ file for debugging purposes.
    void to_obj(std::ostream& stream) {
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
