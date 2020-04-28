#ifndef TRIANGULATEWALL_HPP
#define TRIANGULATEWALL_HPP

#include "libslic3r/Polygon.hpp"

namespace Slic3r {

using Wall = std::pair<std::vector<Vec3d>, std::vector<Vec3i>>;

Wall triangulate_wall(
    const Polygon &       lower,
    const Polygon &       upper,
    double                lower_z_mm,
    double                upper_z_mm);
}

#endif // TRIANGULATEWALL_HPP
