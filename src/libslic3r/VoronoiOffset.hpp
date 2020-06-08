#ifndef slic3r_VoronoiOffset_hpp_
#define slic3r_VoronoiOffset_hpp_

#include "libslic3r.h"

#include "Geometry.hpp"

namespace Slic3r {

Polygons voronoi_offset(const Geometry::VoronoiDiagram &vd, const Lines &lines, double offset_distance, double discretization_error);

} // namespace Slic3r

#endif // slic3r_VoronoiOffset_hpp_
