// Polygon offsetting using Voronoi diagram prodiced by boost::polygon.

#ifndef slic3r_VoronoiOffset_hpp_
#define slic3r_VoronoiOffset_hpp_

#include "libslic3r.h"

#include "Geometry.hpp"

namespace Slic3r {

// Offset a polygon or a set of polygons possibly with holes by traversing a Voronoi diagram.
// The input polygons are stored in lines and lines are referenced by vd.
// Outer curve will be extracted for a positive offset_distance,
// inner curve will be extracted for a negative offset_distance.
// Circular arches will be discretized to achieve discretization_error.
Polygons voronoi_offset(
	const Geometry::VoronoiDiagram 	&vd, 
	const Lines 					&lines, 
	double 							 offset_distance, 
	double 							 discretization_error);

} // namespace Slic3r

#endif // slic3r_VoronoiOffset_hpp_
