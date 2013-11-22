#ifndef slic3r_Geometry_hpp_
#define slic3r_Geometry_hpp_

#include "Polygon.hpp"

namespace Slic3r {

void convex_hull(Points points, Polygon &hull);

}

#endif
