#ifndef SLACOMMON_HPP
#define SLACOMMON_HPP

#include <Eigen/Geometry>


namespace Slic3r {
    
// Typedef from Point.hpp
typedef Eigen::Matrix<float, 3, 1, Eigen::DontAlign> Vec3f;

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
    pos(data(0), data(1), data(2)), head_front_radius(data(3)), is_new_island(data(4)) {}

    bool operator==(const SupportPoint& sp) const { return (pos==sp.pos) && head_front_radius==sp.head_front_radius && is_new_island==sp.is_new_island; }
};


/// An index-triangle structure for libIGL functions. Also serves as an
/// alternative (raw) input format for the SLASupportTree
struct EigenMesh3D {
    Eigen::MatrixXd V;
    Eigen::MatrixXi F;
    double ground_level = 0;
};


} // namespace sla
} // namespace Slic3r


#endif // SLASUPPORTTREE_HPP