#ifndef SLACOMMON_HPP
#define SLACOMMON_HPP

#include <Eigen/Geometry>

// #define SLIC3R_SLA_NEEDS_WINDTREE

namespace Slic3r {
    
// Typedefs from Point.hpp
typedef Eigen::Matrix<float, 3, 1, Eigen::DontAlign> Vec3f;
typedef Eigen::Matrix<double, 3, 1, Eigen::DontAlign> Vec3d;

class TriangleMesh;

namespace sla {
    
struct SupportPoint {
    Vec3f pos;
    float head_front_radius;
    bool is_new_island;

    SupportPoint() :
    pos(Vec3f::Zero()), head_front_radius(0.f), is_new_island(false) {}

    SupportPoint(float pos_x, float pos_y, float pos_z, float head_radius, bool new_island) :
    pos(pos_x, pos_y, pos_z), head_front_radius(head_radius), is_new_island(new_island) {}

    SupportPoint(Vec3f position, float head_radius, bool new_island) :
    pos(position), head_front_radius(head_radius), is_new_island(new_island) {}

    SupportPoint(Eigen::Matrix<float, 5, 1, Eigen::DontAlign> data) :
    pos(data(0), data(1), data(2)), head_front_radius(data(3)), is_new_island(data(4) != 0.f) {}

    bool operator==(const SupportPoint& sp) const { return (pos==sp.pos) && head_front_radius==sp.head_front_radius && is_new_island==sp.is_new_island; }
};


/// An index-triangle structure for libIGL functions. Also serves as an
/// alternative (raw) input format for the SLASupportTree
/*struct EigenMesh3D {
    Eigen::MatrixXd V;
    Eigen::MatrixXi F;
    double ground_level = 0;
};*/

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
        inline const Vec3d& direction() const { return m_dir; }
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

#ifdef SLIC3R_SLA_NEEDS_WINDTREE
    // The signed distance from a point to the mesh. Outputs the distance,
    // the index of the triangle and the closest point in mesh coordinate space.
    si_result signed_distance(const Vec3d& p) const;

    bool inside(const Vec3d& p) const;
#endif /* SLIC3R_SLA_NEEDS_WINDTREE */
};




} // namespace sla
} // namespace Slic3r


#endif // SLASUPPORTTREE_HPP