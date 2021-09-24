#ifndef REPROJECTPOINTSONMESH_HPP
#define REPROJECTPOINTSONMESH_HPP

#include "libslic3r/Point.hpp"
#include "SupportPoint.hpp"
#include "Hollowing.hpp"
#include "IndexedMesh.hpp"
#include "libslic3r/Model.hpp"

#include <tbb/parallel_for.h>

namespace Slic3r { namespace sla {

template<class Pt> Vec3d pos(const Pt &p) { return p.pos.template cast<double>(); }
template<class Pt> void pos(Pt &p, const Vec3d &pp) { p.pos = pp.cast<float>(); }

template<class PointType>
void reproject_support_points(const IndexedMesh &mesh, std::vector<PointType> &pts)
{
    tbb::parallel_for(size_t(0), pts.size(), [&mesh, &pts](size_t idx) {
        int junk;
        Vec3d new_pos;
        mesh.squared_distance(pos(pts[idx]), junk, new_pos);
        pos(pts[idx], new_pos);
    });
}

inline void reproject_points_and_holes(ModelObject *object)
{
    bool has_sppoints = !object->sla_support_points.empty();
    bool has_holes    = !object->sla_drain_holes.empty();

    if (!object || (!has_holes && !has_sppoints)) return;

    TriangleMesh rmsh = object->raw_mesh();
    IndexedMesh emesh{rmsh};

    const Transform3f& tr = object->volumes.front()->get_matrix().cast<float>();

    if (has_sppoints) {
        SupportPoints transformed_points = object->sla_support_points;
        for (SupportPoint& pt : transformed_points)
            pt.pos = tr * pt.pos;
        reproject_support_points(emesh, transformed_points);
    }

    if (has_holes) {
        DrainHoles transformed_holes = object->sla_drain_holes;
        for (DrainHole& hole : transformed_holes) {
            // Hole normals are not transformed here, but the reprojection
            // does not use them.
            hole.pos = tr * hole.pos;
        }
        reproject_support_points(emesh, transformed_holes);
    }
}

}}
#endif // REPROJECTPOINTSONMESH_HPP
