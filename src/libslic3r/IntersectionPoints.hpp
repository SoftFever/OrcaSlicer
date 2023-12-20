///|/ Copyright (c) Prusa Research 2023 Vojtěch Bubník @bubnikv
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_IntersectionPoints_hpp_
#define slic3r_IntersectionPoints_hpp_

#include "ExPolygon.hpp"

namespace Slic3r {

struct IntersectionLines {
    uint32_t line_index1;
    uint32_t line_index2;
    Vec2d intersection;
};
using IntersectionsLines = std::vector<IntersectionLines>;

// collect all intersecting points
IntersectionsLines get_intersections(const Lines &lines);
IntersectionsLines get_intersections(const Polygon &polygon);
IntersectionsLines get_intersections(const Polygons &polygons);
IntersectionsLines get_intersections(const ExPolygon &expolygon);
IntersectionsLines get_intersections(const ExPolygons &expolygons);

} // namespace Slic3r
#endif // slic3r_IntersectionPoints_hpp_