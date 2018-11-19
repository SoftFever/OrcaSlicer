#ifndef SLASUPPORTTREE_HPP
#define SLASUPPORTTREE_HPP

#include <vector>
#include <array>
#include <cstdint>
#include <memory>
#include <Eigen/Geometry>

namespace Slic3r {

// Needed types from Point.hpp
typedef int32_t coord_t;
typedef Eigen::Matrix<double,   3, 1, Eigen::DontAlign> Vec3d;
typedef Eigen::Matrix<float,    3, 1, Eigen::DontAlign> Vec3f;
typedef Eigen::Matrix<coord_t,  3, 1, Eigen::DontAlign> Vec3crd;
typedef std::vector<Vec3d>                              Pointf3s;
typedef std::vector<Vec3crd>                            Points3;

class TriangleMesh;
class Model;
class ModelInstance;
class ModelObject;
class ExPolygon;

using SliceLayer = std::vector<ExPolygon>;
using SlicedSupports = std::vector<SliceLayer>;

namespace sla {

struct SupportConfig {
    // Radius in mm of the pointing side of the head.
    double head_front_radius_mm = 0.2;

    // How much the pinhead has to penetrate the model surface
    double head_penetraiton_mm = 0.5;

    // Radius of the back side of the 3d arrow.
    double head_back_radius_mm = 0.5;

    // Width in mm from the back sphere center to the front sphere center.
    double head_width_mm = 1.0;

    // Radius in mm of the support pillars.
    // Warning: this value will be at most 65% of head_back_radius_mm
    // TODO: This parameter is invalid. The pillar radius will be dynamic in
    // nature. Merged pillars will have an increased thickness. This parameter
    // may serve as the maximum radius, or maybe an increase when two are merged
    // The default radius will be derived from head_back_radius_mm
    double pillar_radius_mm = 0.8;

    // Radius in mm of the pillar base.
    double base_radius_mm = 2.0;

    // The height of the pillar base cone in mm.
    double base_height_mm = 1.0;

    // The default angle for connecting support sticks and junctions.
    double tilt = M_PI/4;

    // The max length of a bridge in mm
    double max_bridge_length_mm = 15.0;

    // The elevation in Z direction upwards. This is the space between the pad
    // and the model object's bounding box bottom.
    double object_elevation_mm = 10;
};

/// A Control structure for the support calculation. Consists of the status
/// indicator callback and the stop condition predicate.
struct Controller {

    // This will signal the status of the calculation to the front-end
    std::function<void(unsigned, const std::string&)> statuscb =
            [](unsigned, const std::string&){};

    // Returns true if the calculation should be aborted.
    std::function<bool(void)> stopcondition = [](){ return false; };

    // Similar to cancel callback. This should check the stop condition and
    // if true, throw an appropriate exception. (TriangleMeshSlicer needs this)
    // consider it a hard abort. stopcondition is permits the algorithm to
    // terminate itself
    std::function<void(void)> cancelfn = [](){};
};

/// An index-triangle structure for libIGL functions. Also serves as an
/// alternative (raw) input format for the SLASupportTree
struct EigenMesh3D {
    Eigen::MatrixXd V;
    Eigen::MatrixXi F;
    double ground_level = 0;
};

using PointSet = Eigen::MatrixXd;

EigenMesh3D to_eigenmesh(const TriangleMesh& m);

// needed for find best rotation
EigenMesh3D to_eigenmesh(const ModelObject& model);

// Simple conversion of 'vector of points' to an Eigen matrix
PointSet    to_point_set(const std::vector<Vec3d>&);


/* ************************************************************************** */

/// Just a wrapper to the runtime error to be recognizable in try blocks
class SLASupportsStoppedException: public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    SLASupportsStoppedException(): std::runtime_error("") {}
};

/// The class containing mesh data for the generated supports.
class SLASupportTree {
    class Impl;
    std::unique_ptr<Impl> m_impl;
    Controller m_ctl;

    // the only value from config that is also needed after construction
    double m_elevation = 0;

    Impl& get() { return *m_impl; }
    const Impl& get() const { return *m_impl; }

    friend void add_sla_supports(Model&,
                                 const SupportConfig&,
                                 const Controller&);

    /// Generate the 3D supports for a model intended for SLA print.
    bool generate(const PointSet& pts,
                  const EigenMesh3D& mesh,
                  const SupportConfig& cfg = {},
                  const Controller& ctl = {});
public:

    SLASupportTree(const PointSet& pts,
                   const EigenMesh3D& em,
                   const SupportConfig& cfg = {},
                   const Controller& ctl = {});

    SLASupportTree(const SLASupportTree&);
    SLASupportTree& operator=(const SLASupportTree&);

    ~SLASupportTree();

    /// Get the whole mesh united into the output TriangleMesh
    /// WITHOUT THE PAD
    void merged_mesh(TriangleMesh& outmesh) const;

    void merged_mesh_with_pad(TriangleMesh&) const;

    /// Get the sliced 2d layers of the support geometry.
    SlicedSupports slice(float layerh, float init_layerh = -1.0) const;

    /// Adding the "pad" (base pool) under the supports
    const TriangleMesh& add_pad(const SliceLayer& baseplate,
                                double min_wall_thickness_mm,
                                double min_wall_height_mm,
                                double max_merge_distance_mm,
                                double edge_radius_mm) const;

    /// Get the pad geometry
    const TriangleMesh& get_pad() const;

    /// The Z offset to raise the model and the supports to the ground level.
    /// This is the elevation given in the support config and the height of the
    /// pad (if requested).
    double get_elevation() const;

};

}

}

#endif // SLASUPPORTTREE_HPP
