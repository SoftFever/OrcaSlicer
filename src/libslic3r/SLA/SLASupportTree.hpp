#ifndef SLASUPPORTTREE_HPP
#define SLASUPPORTTREE_HPP

#include <vector>
#include <array>
#include <cstdint>
#include <memory>
#include <Eigen/Geometry>

#include "SLACommon.hpp"

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
class Polygon;
class ExPolygon;

using Polygons = std::vector<Polygon>;
using ExPolygons = std::vector<ExPolygon>;

namespace sla {

enum class PillarConnectionMode {
    zigzag,
    cross,
    dynamic
};

struct SupportConfig {
    // Radius in mm of the pointing side of the head.
    double head_front_radius_mm = 0.2;

    // How much the pinhead has to penetrate the model surface
    double head_penetration_mm = 0.5;

    // Radius of the back side of the 3d arrow.
    double head_back_radius_mm = 0.5;

    // Width in mm from the back sphere center to the front sphere center.
    double head_width_mm = 1.0;

    // How to connect pillars
    PillarConnectionMode pillar_connection_mode = PillarConnectionMode::dynamic;

    // Only generate pillars that can be routed to ground
    bool ground_facing_only = false;

    // TODO: unimplemented at the moment. This coefficient will have an impact
    // when bridges and pillars are merged. The resulting pillar should be a bit
    // thicker than the ones merging into it. How much thicker? I don't know
    // but it will be derived from this value.
    double pillar_widening_factor = 0.5;

    // Radius in mm of the pillar base.
    double base_radius_mm = 2.0;

    // The height of the pillar base cone in mm.
    double base_height_mm = 1.0;

    // The default angle for connecting support sticks and junctions.
    double bridge_slope = M_PI/4;

    // The max length of a bridge in mm
    double max_bridge_length_mm = 10.0;

    // The max distance of a pillar to pillar link.
    double max_pillar_link_distance_mm = 10.0;

    // The elevation in Z direction upwards. This is the space between the pad
    // and the model object's bounding box bottom.
    double object_elevation_mm = 10;
    
    // The shortest distance between a pillar base perimeter from the model
    // body. This is only useful when elevation is set to zero.
    double pillar_base_safety_distance_mm = 0.5;
    
    double head_fullwidth() const {
        return 2 * head_front_radius_mm + head_width_mm +
               2 * head_back_radius_mm - head_penetration_mm;
    }

    // /////////////////////////////////////////////////////////////////////////
    // Compile time configuration values (candidates for runtime)
    // /////////////////////////////////////////////////////////////////////////

    // The max Z angle for a normal at which it will get completely ignored.
    static const double normal_cutoff_angle;

    // The shortest distance of any support structure from the model surface
    static const double safety_distance_mm;

    static const double max_solo_pillar_height_mm;
    static const double max_dual_pillar_height_mm;
    static const double   optimizer_rel_score_diff;
    static const unsigned optimizer_max_iterations;
    static const unsigned pillar_cascade_neighbors;
    static const unsigned max_bridges_on_pillar;
};

struct PadConfig;

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

/* ************************************************************************** */

/// The class containing mesh data for the generated supports.
class SLASupportTree {
    class Impl;     // persistent support data
    std::unique_ptr<Impl> m_impl;

    Impl& get() { return *m_impl; }
    const Impl& get() const { return *m_impl; }

    friend void add_sla_supports(Model&,
                                 const SupportConfig&,
                                 const Controller&);

    // The generation algorithm is quite long and will be captured in a separate
    // class with private data, helper methods, etc... This data is only needed
    // during the calculation whereas the Impl class contains the persistent
    // data, mostly the meshes.
    class Algorithm;

    // Generate the 3D supports for a model intended for SLA print. This
    // will instantiate the Algorithm class and call its appropriate methods
    // with status indication.
    bool generate(const std::vector<SupportPoint>& pts,
                  const EigenMesh3D& mesh,
                  const SupportConfig& cfg = {},
                  const Controller& ctl = {});

public:

    SLASupportTree(double ground_level = 0.0);

    SLASupportTree(const std::vector<SupportPoint>& pts,
                   const EigenMesh3D& em,
                   const SupportConfig& cfg = {},
                   const Controller& ctl = {});
    
    SLASupportTree(const SLASupportTree&) = delete;
    SLASupportTree& operator=(const SLASupportTree&) = delete;
    
    SLASupportTree(SLASupportTree &&o);
    SLASupportTree &operator=(SLASupportTree &&o);

    ~SLASupportTree();

    /// Get the whole mesh united into the output TriangleMesh
    /// WITHOUT THE PAD
    const TriangleMesh& merged_mesh() const;

    void merged_mesh_with_pad(TriangleMesh&) const;

    std::vector<ExPolygons> slice(const std::vector<float> &,
                                  float closing_radius) const;

    /// Adding the "pad" (base pool) under the supports
    /// modelbase will be used according to the embed_object flag in PoolConfig.
    /// If set, the plate will interpreted as the model's intrinsic pad. 
    /// Otherwise, the modelbase will be unified with the base plate calculated
    /// from the supports.
    const TriangleMesh& add_pad(const ExPolygons& modelbase,
                                const PadConfig& pcfg) const;

    /// Get the pad geometry
    const TriangleMesh& get_pad() const;

    void remove_pad();

};

}

}

#endif // SLASUPPORTTREE_HPP
