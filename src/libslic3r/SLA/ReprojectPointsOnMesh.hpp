#ifndef REPROJECTPOINTSONMESH_HPP
#define REPROJECTPOINTSONMESH_HPP

#include "libslic3r/Point.hpp"
#include "SupportPoint.hpp"
#include "Hollowing.hpp"
#include "EigenMesh3D.hpp"

#include <tbb/parallel_for.h>

namespace Slic3r { namespace sla {

template<class Pt> Vec3d pos(const Pt &p) { return p.pos.template cast<double>(); }
template<class Pt> void pos(Pt &p, const Vec3d &pp) { p.pos = pp.cast<float>(); }

template<class PointType>
void reproject_support_points(const EigenMesh3D &mesh, std::vector<PointType> &pts)
{
    tbb::parallel_for(size_t(0), pts.size(), [&mesh, &pts](size_t idx) {
        int junk;
        Vec3d new_pos;
        mesh.squared_distance(pos(pts[idx]), junk, new_pos);
        pos(pts[idx], new_pos);
    });
}

}}
#endif // REPROJECTPOINTSONMESH_HPP
