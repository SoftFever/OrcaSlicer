#ifndef slic3r_Geometry_hpp_
#define slic3r_Geometry_hpp_

#include "Polygon.hpp"

namespace Slic3r { namespace Geometry {

void convex_hull(Points points, Polygon &hull);
void chained_path(Points &points, std::vector<Points::size_type> &retval, Point start_near);
void chained_path(Points &points, std::vector<Points::size_type> &retval);

} }

#endif
