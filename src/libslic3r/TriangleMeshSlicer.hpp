#ifndef slic3r_TriangleMeshSlicer_hpp_
#define slic3r_TriangleMeshSlicer_hpp_

#include "libslic3r.h"
#include <admesh/stl.h>
#include <functional>
#include <vector>
#include <boost/thread.hpp>
#include "BoundingBox.hpp"
#include "Line.hpp"
#include "Point.hpp"
#include "Polygon.hpp"
#include "ExPolygon.hpp"

namespace Slic3r {

class TriangleMesh;

enum class SlicingMode : uint32_t {
    // Regular slicing, maintain all contours and their orientation.
    Regular,
    // Maintain all contours, orient all contours CCW, therefore all holes are being closed.
    Positive,
    // Orient all contours CCW and keep only the contour with the largest area.
    // This mode is useful for slicing complex objects in vase mode.
    PositiveLargestContour,
};

struct MeshSlicingParams
{
    SlicingMode   mode { SlicingMode::Regular };
    // For vase mode: below this layer a different slicing mode will be used to produce a single contour.
    // 0 = ignore.
    size_t        slicing_mode_normal_below_layer { 0 };
    // Mode to apply below slicing_mode_normal_below_layer. Ignored if slicing_mode_nromal_below_layer == 0.
    SlicingMode   mode_below { SlicingMode::Regular };
};

struct MeshSlicingParamsExtended : public MeshSlicingParams
{
    // Morphological closing operation when creating output expolygons.
    float         closing_radius { 0 };
    // Positive offset applied when creating output expolygons.
    float         extra_offset { 0 };
    // Resolution for contour simplification.
    // 0 = don't simplify.
    double        resolution { 0 };
    // Transformation of the object owning the ModelVolume.
//    Transform3d         object_trafo;
};

class TriangleMeshSlicer
{
public:
    using throw_on_cancel_callback_type = std::function<void()>;
    TriangleMeshSlicer() = default;
    TriangleMeshSlicer(const TriangleMesh *mesh) { this->init(mesh, []{}); }
    TriangleMeshSlicer(const indexed_triangle_set *its) { this->init(its, []{}); }
    void init(const TriangleMesh *mesh, throw_on_cancel_callback_type throw_on_cancel);
    void init(const indexed_triangle_set *its, throw_on_cancel_callback_type);

    void slice(
        const std::vector<float>         &z,
        const MeshSlicingParams          &params,
        std::vector<Polygons>           *layers,
        throw_on_cancel_callback_type    throw_on_cancel = []{}) const;

    void slice(
        // Where to slice.
        const std::vector<float>         &z,
        const MeshSlicingParamsExtended  &params,
        std::vector<ExPolygons>         *layers,
        throw_on_cancel_callback_type    throw_on_cancel = []{}) const;

    void cut(float z, indexed_triangle_set *upper, indexed_triangle_set *lower) const;
    void cut(float z, TriangleMesh* upper, TriangleMesh* lower) const;

    void set_up_direction(const Vec3f& up);

private:
    const indexed_triangle_set *m_its { nullptr };
//    const TriangleMesh      *mesh { nullptr };
    // Map from a facet to an edge index.
    std::vector<int>         facets_edges;
    // Scaled copy of this->mesh->stl.v_shared
    std::vector<stl_vertex>  v_scaled_shared;
    // Quaternion that will be used to rotate every facet before the slicing
    Eigen::Quaternion<float, Eigen::DontAlign> m_quaternion;
    // Whether or not the above quaterion should be used
    bool                     m_use_quaternion = false;
};

inline void slice_mesh(
    const TriangleMesh                               &mesh,
    const std::vector<float>                         &z,
    std::vector<Polygons>                            &layers,
    TriangleMeshSlicer::throw_on_cancel_callback_type thr = []{})
{
    if (! mesh.empty()) {
        TriangleMeshSlicer slicer(&mesh);
        slicer.slice(z, MeshSlicingParams{}, &layers, thr);
    }
}

inline void slice_mesh(
    const TriangleMesh                               &mesh,
    const std::vector<float>                         &z,
    const MeshSlicingParamsExtended                  &params,
    std::vector<ExPolygons>                          &layers,
    TriangleMeshSlicer::throw_on_cancel_callback_type thr = []{})
{
    if (! mesh.empty()) {
        TriangleMeshSlicer slicer(&mesh);
        slicer.slice(z, params, &layers, thr);
    }
}

inline void slice_mesh(
    const TriangleMesh                               &mesh,
    const std::vector<float>                         &z,
    float                                             closing_radius,
    std::vector<ExPolygons>                          &layers,
    TriangleMeshSlicer::throw_on_cancel_callback_type thr = []{})
{
    MeshSlicingParamsExtended params;
    params.closing_radius = closing_radius;
    slice_mesh(mesh, z, params, layers);
}

inline void slice_mesh(
    const TriangleMesh                               &mesh,
    const std::vector<float>                         &z,
    std::vector<ExPolygons>                          &layers,
    TriangleMeshSlicer::throw_on_cancel_callback_type thr = []{})
{
    slice_mesh(mesh, z, MeshSlicingParamsExtended{}, layers);
}

}

#endif // slic3r_TriangleMeshSlicer_hpp_
