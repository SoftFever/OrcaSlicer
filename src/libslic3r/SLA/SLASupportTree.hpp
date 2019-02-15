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

    // Radius in mm of the support pillars. The actual radius of the pillars
    // beginning with a head will not be higher than head_back_radius but the
    // headless pillars will have half of this value.
    double headless_pillar_radius_mm = 0.4;

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
    double tilt = M_PI/4;

    // The max length of a bridge in mm
    double max_bridge_length_mm = 15.0;

    // The elevation in Z direction upwards. This is the space between the pad
    // and the model object's bounding box bottom.
    double object_elevation_mm = 10;

    // The max Z angle for a normal at which it will get completely ignored.
    double normal_cutoff_angle = 150.0 * M_PI / 180.0;

};

struct PoolConfig;

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
class EigenMesh3D {
    class AABBImpl;

    Eigen::MatrixXd m_V;
    Eigen::MatrixXi m_F;
    double m_ground_level = 0;

    std::unique_ptr<AABBImpl> m_aabb;
public:

    EigenMesh3D(const TriangleMesh&);
    EigenMesh3D(const EigenMesh3D& other);
    EigenMesh3D& operator=(const EigenMesh3D&);

    ~EigenMesh3D();

    inline double ground_level() const { return m_ground_level; }

    inline const Eigen::MatrixXd& V() const { return m_V; }
    inline const Eigen::MatrixXi& F() const { return m_F; }

    // Result of a raycast
    class hit_result {
        double m_t = std::numeric_limits<double>::infinity();
        int m_face_id = -1;
        const EigenMesh3D& m_mesh;
        Vec3d m_dir;
        inline hit_result(const EigenMesh3D& em): m_mesh(em) {}
        friend class EigenMesh3D;
    public:

        inline double distance() const { return m_t; }

        inline int face() const { return m_face_id; }

        inline Vec3d normal() const {
            if(m_face_id < 0) return {};
            auto trindex    = m_mesh.m_F.row(m_face_id);
            const Vec3d& p1 = m_mesh.V().row(trindex(0));
            const Vec3d& p2 = m_mesh.V().row(trindex(1));
            const Vec3d& p3 = m_mesh.V().row(trindex(2));
            Eigen::Vector3d U = p2 - p1;
            Eigen::Vector3d V = p3 - p1;
            return U.cross(V).normalized();
        }

        inline bool is_inside() {
            return m_face_id >= 0 && normal().dot(m_dir) > 0;
        }
    };

    // Casting a ray on the mesh, returns the distance where the hit occures.
    hit_result query_ray_hit(const Vec3d &s, const Vec3d &dir) const;

    class si_result {
        double m_value;
        int m_fidx;
        Vec3d m_p;
        si_result(double val, int i, const Vec3d& c):
            m_value(val), m_fidx(i), m_p(c) {}
        friend class EigenMesh3D;
    public:

        si_result() = delete;

        double value() const { return m_value; }
        operator double() const { return m_value; }
        const Vec3d& point_on_mesh() const { return m_p; }
        int F_idx() const { return m_fidx; }
    };

    // The signed distance from a point to the mesh. Outputs the distance,
    // the index of the triangle and the closest point in mesh coordinate space.
    si_result signed_distance(const Vec3d& p) const;

    bool inside(const Vec3d& p) const;
};

using PointSet = Eigen::MatrixXd;

//EigenMesh3D to_eigenmesh(const TriangleMesh& m);

// needed for find best rotation
//EigenMesh3D to_eigenmesh(const ModelObject& model);

// Simple conversion of 'vector of points' to an Eigen matrix
PointSet    to_point_set(const std::vector<Vec3d>&);


/* ************************************************************************** */

/// Just a wrapper to the runtime error to be recognizable in try blocks
class SLASupportsStoppedException: public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    SLASupportsStoppedException();
};

/// The class containing mesh data for the generated supports.
class SLASupportTree {
    class Impl;
    std::unique_ptr<Impl> m_impl;

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

    SLASupportTree();

    SLASupportTree(const PointSet& pts,
                   const EigenMesh3D& em,
                   const SupportConfig& cfg = {},
                   const Controller& ctl = {});

    SLASupportTree(const SLASupportTree&);
    SLASupportTree& operator=(const SLASupportTree&);

    ~SLASupportTree();

    /// Get the whole mesh united into the output TriangleMesh
    /// WITHOUT THE PAD
    const TriangleMesh& merged_mesh() const;

    void merged_mesh_with_pad(TriangleMesh&) const;

    /// Get the sliced 2d layers of the support geometry.
    SlicedSupports slice(float layerh, float init_layerh = -1.0) const;

    /// Adding the "pad" (base pool) under the supports
    const TriangleMesh& add_pad(const SliceLayer& baseplate,
                                const PoolConfig& pcfg) const;

    /// Get the pad geometry
    const TriangleMesh& get_pad() const;

    void remove_pad();

};

}

}

#endif // SLASUPPORTTREE_HPP
