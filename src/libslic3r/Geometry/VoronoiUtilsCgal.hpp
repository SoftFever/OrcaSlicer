#ifndef slic3r_VoronoiUtilsCgal_hpp_
#define slic3r_VoronoiUtilsCgal_hpp_

#include "Voronoi.hpp"

namespace Slic3r::Geometry {
class VoronoiDiagram;

class VoronoiUtilsCgal
{
public:
    // Check if the Voronoi diagram is planar using CGAL sweeping edge algorithm for enumerating all intersections between lines.
    static bool is_voronoi_diagram_planar_intersection(const VoronoiDiagram &voronoi_diagram);

    // Check if the Voronoi diagram is planar using verification that all neighboring edges are ordered CCW for each vertex.
    static bool is_voronoi_diagram_planar_angle(const VoronoiDiagram &voronoi_diagram);

};
} // namespace Slic3r::Geometry

#endif // slic3r_VoronoiUtilsCgal_hpp_
