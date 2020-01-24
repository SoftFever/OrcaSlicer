#ifndef SLA_COMMON_HPP
#define SLA_COMMON_HPP

#include <memory>
#include <vector>
#include <numeric>
#include <functional>
#include <Eigen/Geometry>

//#include "SLASpatIndex.hpp"

//#include <libslic3r/ExPolygon.hpp>
//#include <libslic3r/TriangleMesh.hpp>

// #define SLIC3R_SLA_NEEDS_WINDTREE

namespace Slic3r {
    
// Typedefs from Point.hpp
typedef Eigen::Matrix<float, 3, 1, Eigen::DontAlign> Vec3f;
typedef Eigen::Matrix<double, 3, 1, Eigen::DontAlign> Vec3d;
typedef Eigen::Matrix<int, 4, 1, Eigen::DontAlign> Vec4i;

namespace sla {

using PointSet = Eigen::MatrixXd;

} // namespace sla
} // namespace Slic3r


#endif // SLASUPPORTTREE_HPP
