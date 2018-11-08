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
    double head_penetraiton = 0.2;

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
};

/// A Control structure for the support calculation. Consists of the status
/// indicator callback and the stop condition predicate.
struct Controller {
    std::function<void(unsigned, const std::string&)> statuscb =
            [](unsigned, const std::string&){};

    std::function<bool(void)> stopcondition = [](){ return false; };
};

/// An index-triangle structure for libIGL functions. Also serves as an
/// alternative (raw) input format for the SLASupportTree
struct EigenMesh3D {
//    Eigen::MatrixXd V;
//    Eigen::MatrixXi F;
    Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::DontAlign> V;
    Eigen::Matrix<int, Eigen::Dynamic, Eigen::Dynamic, Eigen::DontAlign> F;
};

using PointSet = Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::DontAlign>; //Eigen::MatrixXd;

/* ************************************************************************** */
/* TODO: May not be needed:                                                   */
/* ************************************************************************** */

void create_head(TriangleMesh&, double r1_mm, double r2_mm, double width_mm);

/// Add support volumes to the model directly
void add_sla_supports(Model& model, const SupportConfig& cfg = {},
                      const Controller& ctl = {});

EigenMesh3D to_eigenmesh(const TriangleMesh& m);
EigenMesh3D to_eigenmesh(const Model& model);
EigenMesh3D to_eigenmesh(const ModelObject& model);

PointSet support_points(const Model& model);
PointSet support_points(const ModelObject& modelobject, size_t instance_id = 0);

/* ************************************************************************** */

/// Just a wrapper to the runtime error to be recognizable in try blocks
class SLASupportsStoppedException: public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    SLASupportsStoppedException(): std::runtime_error("") {}
};

/// A simple type carrying mouse event info. For support editing purposes
struct MouseEvent {
    enum Buttons {
        M_RIGHT, M_LEFT, M_MIDDLE
    } button;

    enum Type {
        ENGAGE, RELEASE, MOVE
    } type;

    Vec3d coords;
};

/// The class containing mesh data for the generated supports.
class SLASupportTree {
    class Impl;
    std::unique_ptr<Impl> m_impl;
    std::function<void()> m_vcallback;

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

    // Constructors will throw if the stop condition becomes true.
    SLASupportTree(const Model& model,
                   const SupportConfig& cfg = {},
                   const Controller& ctl = {});

    SLASupportTree(const PointSet& pts,
                   const EigenMesh3D& em,
                   const SupportConfig& cfg = {},
                   const Controller& ctl = {});

    SLASupportTree(const SLASupportTree&);
    SLASupportTree& operator=(const SLASupportTree&);

    ~SLASupportTree();

    /// Get the whole mesh united into the output TriangleMesh
    void merged_mesh(TriangleMesh& outmesh) const;

    /// Get the sliced 2d layers of the support geometry.
    SlicedSupports slice(float layerh, float init_layerh = -1.0) const;

    /// The function to call when mouse events should be propagated to the
    /// supports for editing
    void mouse_event(const MouseEvent&);

    /// The provided callback will be called if the supports change their shape
    /// or need to be repainted
    inline void on_supports_changed(std::function<void()> callback) {
        m_vcallback = callback;
    }
};

}

}

#endif // SLASUPPORTTREE_HPP
