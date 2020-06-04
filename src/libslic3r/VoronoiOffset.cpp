// Polygon offsetting code inspired by OpenVoronoi by Anders Wallin
// https://github.com/aewallin/openvoronoi
// This offsetter uses results of boost::polygon Voronoi.

#include "VoronoiOffset.hpp"

#include <cmath>

namespace Slic3r {

using VD = Geometry::VoronoiDiagram;

namespace detail {
	// Intersect a circle with a ray, return the two parameters
	double first_circle_segment_intersection_parameter(
		const Vec2d &center, const double r, const Vec2d &pt, const Vec2d &v)
	{
		const Vec2d 	d = pt - center;
#ifndef NDEBUG
        double          d0 = (pt - center).norm();
        double          d1 = (pt + v - center).norm();
        assert(r < std::max(d0, d1) + EPSILON);
#endif /* NDEBUG */
        const double	a = v.squaredNorm();
		const double 	b = 2. * d.dot(v);
		const double    c = d.squaredNorm() - r * r;
		std::pair<int, std::array<double, 2>> out;
        double          u = b * b - 4. * a * c;
		assert(u > - EPSILON);
		double          t;
		if (u <= 0) {
			// Degenerate to a single closest point.
			t = - b / (2. * a);
			assert(t >= - EPSILON && t <= 1. + EPSILON);
			return Slic3r::clamp(0., 1., t);
		} else {
			u = sqrt(u);
			out.first = 2;
			double t0 = (- b - u) / (2. * a);
			double t1 = (- b + u) / (2. * a);
			// One of the intersections shall be found inside the segment.
			assert((t0 >= - EPSILON && t0 <= 1. + EPSILON) || (t1 >= - EPSILON && t1 <= 1. + EPSILON));
			if (t1 < 0.)
				return 0.;
			if (t0 > 1.)
				return 1.;
			return (t0 > 0.) ? t0 : t1;
		}
	}

	Vec2d voronoi_edge_offset_point(
        const VD                    &vd,
        const Lines                 &lines,
		// Distance of a VD vertex to the closest site (input polygon edge or vertex).
        const std::vector<double> 	&vertex_dist,
		// Minium distance of a VD edge to the closest site (input polygon edge or vertex).
		// For a parabolic segment the distance may be smaller than the distance of the two end points.
        const std::vector<double> 	&edge_dist,
		// Edge for which to calculate the offset point. If the distance towards the input polygon
		// is not monotonical, pick the offset point closer to edge.vertex0().
        const VD::edge_type         &edge,
		// Distance from the input polygon along the edge.
        const double 				 offset_distance)
	{
		const VD::vertex_type *v0    = edge.vertex0();
		const VD::vertex_type *v1    = edge.vertex1();
        const VD::cell_type   *cell  = edge.cell();
        const VD::cell_type   *cell2 = edge.twin()->cell();
		const Line  		  &line0 = lines[cell->source_index()];
		const Line  		  &line1 = lines[cell2->source_index()];
		if (v0 == nullptr || v1 == nullptr) {
            assert(edge.is_infinite());
            assert(v0 != nullptr || v1 != nullptr);
            // Offsetting on an unconstrained edge.
            assert(offset_distance > vertex_dist[(v0 ? v0 : v1) - &vd.vertices().front()] - EPSILON);
			Vec2d 	pt, dir;
			double  t;
            if (cell->contains_point() && cell2->contains_point()) {
                const Point &pt0 = (cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line0.a : line0.b;
                const Point &pt1 = (cell2->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line1.a : line1.b;
                // Direction vector of this unconstrained Voronoi edge.
                dir = Vec2d(double(pt0.y() - pt1.y()), double(pt1.x() - pt0.x()));
                if (v0 == nullptr) {
                	v0 = v1;
                	dir = - dir;
                }
				pt = Vec2d(v0->x(), v0->y());
                t = detail::first_circle_segment_intersection_parameter(Vec2d(pt0.x(), pt0.y()), offset_distance, pt, dir);
            } else {
                // Infinite edges could not be created by two segment sites.
                assert(cell->contains_point() != cell2->contains_point());
                // Linear edge goes through the endpoint of a segment.
                assert(edge.is_linear());
                assert(edge.is_secondary());
                const Line  &line = cell->contains_segment() ? line0 : line1;
                const Point &ipt  = cell->contains_segment() ?
                    ((cell2->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line1.a : line1.b) :
                    ((cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line0.a : line0.b);
                assert(line.a == ipt || line.b == ipt);
                pt  = Vec2d(ipt.x(), ipt.y());
                dir = Vec2d(line.a.y() - line.b.y(), line.b.x() - line.a.x());
                assert(dir.norm() > 0.);
                t   = offset_distance / dir.norm();
                if (((line.a == ipt) == cell->contains_point()) == (v0 == nullptr))
                	t = - t;
            }
            return pt + t * dir;
        } else {
            // Constrained edge.
            Vec2d p0(v0->x(), v0->y());
            Vec2d p1(v1->x(), v1->y());
            double d0 = vertex_dist[v0 - &vd.vertices().front()];
            double d1 = vertex_dist[v1 - &vd.vertices().front()];
            if (cell->contains_segment() && cell2->contains_segment()) {
                // This edge is a bisector of two line segments. Distance to the input polygon increases/decreases monotonically.
                double ddif = d1 - d0;
                assert(offset_distance > std::min(d0, d1) - EPSILON && offset_distance < std::max(d0, d1) + EPSILON);
                double t    = (ddif == 0) ? 0. : clamp(0., 1., (offset_distance - d0) / ddif);
                return Slic3r::lerp(p0, p1, t);
            } else {
	            // One cell contains a point, the other contains an edge or a point.
                assert(cell->contains_point() || cell2->contains_point());
                const Point &ipt = cell->contains_point() ?
                    ((cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line0.a : line0.b) :
                    ((cell2->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line1.a : line1.b);
	            double t = detail::first_circle_segment_intersection_parameter(
	            	Vec2d(ipt.x(), ipt.y()), offset_distance, p0, p1 - p0);
	            return Slic3r::lerp(p0, p1, t);
	        }
        }
	}
};

Polygons voronoi_offset(const VD &vd, const Lines &lines, double offset_distance, double discretization_error)
{
	// Distance of a VD vertex to the closest site (input polygon edge or vertex).
    std::vector<double> vertex_dist(vd.num_vertices(), std::numeric_limits<double>::max());

	// Minium distance of a VD edge to the closest site (input polygon edge or vertex).
	// For a parabolic segment the distance may be smaller than the distance of the two end points.
    std::vector<double> edge_dist(vd.num_edges(), std::numeric_limits<double>::max());

	// Calculate minimum distance of input polygons to voronoi vertices and voronoi edges.
    for (const VD::edge_type &edge : vd.edges()) {
		const VD::vertex_type *v0    = edge.vertex0();
		const VD::vertex_type *v1    = edge.vertex1();
        const VD::cell_type   *cell  = edge.cell();
        const VD::cell_type   *cell2 = edge.twin()->cell();
        const Line 			  &line0 = lines[cell->source_index()];
        const Line 			  &line1 = lines[cell2->source_index()];
		double 				   d0, d1, dmin;
		if (v0 == nullptr || v1 == nullptr) {
			assert(edge.is_infinite());
            if (cell->contains_point() && cell2->contains_point()) {
				const Point &pt0 = (cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line0.a : line0.b;
				const Point &pt1 = (cell2->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line1.a : line1.b;
                d0 = d1 = std::numeric_limits<double>::max();
		    	if (v0 == nullptr && v1 == nullptr) {
                    dmin = (pt1.cast<double>() - pt0.cast<double>()).norm();
		    	} else {
                    Vec2d pt((pt0 + pt1).cast<double>() * 0.5);
                    Vec2d dir(double(pt0.y() - pt1.y()), double(pt1.x() - pt0.x()));
					Vec2d pt0d(pt0.x(), pt0.y());
					if (v0) {
						Vec2d a(v0->x(), v0->y());
						d0 = (a - pt0d).norm();
						dmin = ((a - pt).dot(dir) < 0.) ? (a - pt0d).norm() : d0;
                        vertex_dist[v0 - &vd.vertices().front()] = d0;
                    } else {
						Vec2d a(v1->x(), v1->y());
						d1 = (a - pt0d).norm();
						dmin = ((a - pt).dot(dir) < 0.) ? (a - pt0d).norm() : d1;
                        vertex_dist[v1 - &vd.vertices().front()] = d1;
                    }
		    	}
		    } else {
			    // Infinite edges could not be created by two segment sites.
                assert(cell->contains_point() != cell2->contains_point());
                // Linear edge goes through the endpoint of a segment.
                assert(edge.is_linear());
                assert(edge.is_secondary());
#ifndef NDEBUG
                if (cell->contains_segment()) {
                    const Point &pt1 = (cell2->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line1.a : line1.b;
                    assert((pt1.x() == line0.a.x() && pt1.y() == line0.a.y()) ||
                           (pt1.x() == line0.b.x() && pt1.y() == line0.b.y()));
                } else {
                    const Point &pt0 = (cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line0.a : line0.b;
                    assert((pt0.x() == line1.a.x() && pt0.y() == line1.a.y()) ||
                           (pt0.x() == line1.b.x() && pt0.y() == line1.b.y()));
                }
                const Point &pt = cell->contains_segment() ?
                    ((cell2->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line1.a : line1.b) :
                    ((cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line0.a : line0.b);
#endif /* NDEBUG */
                if (v0) {
                    assert((Point(v0->x(), v0->y()) - pt).cast<double>().norm() < SCALED_EPSILON);
                    d0 = dmin = 0.;
                    vertex_dist[v0 - &vd.vertices().front()] = d0;
                } else {
                    assert((Point(v1->x(), v1->y()) - pt).cast<double>().norm() < SCALED_EPSILON);
                    d1 = dmin = 0.;
                    vertex_dist[v1 - &vd.vertices().front()] = d1;
                }
		    }
        } else {
			// Finite edge has valid points at both sides.
	        if (cell->contains_segment() && cell2->contains_segment()) {
				// This edge is a bisector of two line segments.
	            d0 = std::hypot(v0->x() - line0.a.x(), v0->y() - line0.a.y());
	            d1 = std::hypot(v0->x() - line0.b.x(), v0->y() - line0.b.y());
				if (d0 < d1)
	                d1 = std::hypot(v1->x() - line0.a.x(), v1->y() - line0.a.y());
				else {
					d0 = d1;
	                d1 = std::hypot(v1->x() - line0.b.x(), v1->y() - line0.b.y());
				}
				dmin = std::min(d0, d1);
			} else {
                assert(cell->contains_point() || cell2->contains_point());
                const Point &pt0 = cell->contains_point() ?
                    ((cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line0.a : line0.b) :
                    ((cell2->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line1.a : line1.b);
				// Project p0 to line segment <v0, v1>.
	            Vec2d p0(v0->x(), v0->y());
	            Vec2d p1(v1->x(), v1->y());
				Vec2d px(pt0.x(), pt0.y());
				Vec2d v = p1 - p0;
                d0 = (p0 - px).norm();
                d1 = (p1 - px).norm();
                double t = v.dot(px - p0);
				double l2 = v.squaredNorm();
				if (t > 0. && t < l2) {
					// Foot point on the line segment.
					Vec2d foot = p0 + (t / l2) * v;
					dmin = (foot - px).norm();
				} else
					dmin = std::min(d0, d1);
			}
            vertex_dist[v0 - &vd.vertices().front()] = d0;
            vertex_dist[v1 - &vd.vertices().front()] = d1;
        }
        edge_dist[&edge - &vd.edges().front()] = dmin;
	}

	// Mark cells intersected by the offset curve.
	std::vector<unsigned char> seed_cells(vd.num_cells(), false);
	for (const VD::cell_type &cell : vd.cells()) {
		const VD::edge_type *first_edge = cell.incident_edge();
		const VD::edge_type *edge       = first_edge;
		do {
            double dmin = edge_dist[edge - &vd.edges().front()];
			double dmax = std::numeric_limits<double>::max();
            const VD::vertex_type *v0 = edge->vertex0();
            const VD::vertex_type *v1 = edge->vertex1();
			if (v0 != nullptr)
                dmax = vertex_dist[v0 - &vd.vertices().front()];
			if (v1 != nullptr)
                dmax = std::max(dmax, vertex_dist[v1 - &vd.vertices().front()]);
            if (offset_distance >= dmin && offset_distance <= dmax) {
				// This cell is being intersected by the offset curve.
				seed_cells[&cell - &vd.cells().front()] = true;
				break;
			}
			edge = edge->next();
		} while (edge != first_edge);
	}

	auto edge_dir = [&vd, &vertex_dist, &edge_dist, offset_distance](const VD::edge_type *edge) {
        const VD::vertex_type *v0 = edge->vertex0();
        const VD::vertex_type *v1 = edge->vertex1();
        double d0 = v0 ? vertex_dist[v0 - &vd.vertices().front()] : std::numeric_limits<double>::max();
        double d1 = v1 ? vertex_dist[v1 - &vd.vertices().front()] : std::numeric_limits<double>::max();
    	if (d0 < offset_distance && offset_distance < d1)
        	return true;
    	else if (d1 < offset_distance && offset_distance < d0)
        	return false;
    	else {
        	assert(false);
        	return false;
    	}
    };

	/// \brief starting at e, find the next edge on the face that brackets t
	///
	/// we can be in one of two modes.
	/// if direction==false then we are looking for an edge where src_t < t < trg_t
	/// if direction==true we are looning for an edge where       trg_t < t < src_t
	auto next_offset_edge =
		[&vd, &vertex_dist, &edge_dist, offset_distance]
            (const VD::edge_type *start_edge, bool direction) -> const VD::edge_type* {
	    const VD::edge_type *edge = start_edge;
	    do {
            const VD::vertex_type *v0 = edge->vertex0();
            const VD::vertex_type *v1 = edge->vertex1();
            double d0 = v0 ? vertex_dist[v0 - &vd.vertices().front()] : std::numeric_limits<double>::max();
            double d1 = v1 ? vertex_dist[v1 - &vd.vertices().front()] : std::numeric_limits<double>::max();
	        if (direction ? (d1 < offset_distance && offset_distance < d0) : (d0 < offset_distance && offset_distance < d1))
	        	return edge;
	        edge = edge->next();
	    } while (edge != start_edge);
	    assert(false);
        return nullptr;
	};

	// Track the offset curves.
	Polygons out;
	double angle_step    = 2. * acos((offset_distance - discretization_error) / offset_distance);
	double sin_threshold = sin(angle_step) + EPSILON;
	for (size_t seed_cell_idx = 0; seed_cell_idx < vd.num_cells(); ++ seed_cell_idx)
		if (seed_cells[seed_cell_idx]) {
			seed_cells[seed_cell_idx] = false;
			// Initial direction should not matter, an offset curve shall intersect a cell at least at two points
			// (if it is not just touching the cell at a single vertex), and such two intersection points shall have
			// opposite direction.
    		bool direction = false; 
    		// the first edge on the start-face
            const VD::cell_type &cell       = vd.cells()[seed_cell_idx];
            const VD::edge_type *start_edge = next_offset_edge(cell.incident_edge(), direction);
            assert(start_edge->cell() == &cell);
            const VD::edge_type *edge       = start_edge;
            Polygon  			 poly;
		    do {
                direction = edge_dir(edge);
		        // find the next edge
                const VD::edge_type  *next_edge = next_offset_edge(edge->next(), direction);
		        //std::cout << "offset-output: "; print_edge(edge); std::cout << " to "; print_edge(next_edge); std::cout << "\n";
		        // Interpolate a circular segment or insert a linear segment between edge and next_edge.
                const VD::cell_type  *cell      = edge->cell();
                Vec2d p1 = detail::voronoi_edge_offset_point(vd, lines, vertex_dist, edge_dist, *edge, offset_distance);
                Vec2d p2 = detail::voronoi_edge_offset_point(vd, lines, vertex_dist, edge_dist, *next_edge, offset_distance);
                if (cell->contains_point()) {
					// Discretize an arc from p1 to p2 with radius = offset_distance and discretization_error.
					// The arc should cover angle < PI.
					//FIXME we should be able to produce correctly oriented output curves based on the first edge taken!
                    const Line  &line0  = lines[cell->source_index()];
					const Vec2d &center = ((cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line0.a : line0.b).cast<double>();
					const Vec2d  v1 	= p1 - center;
					const Vec2d  v2 	= p2 - center;
					double 		 orient = cross2(v1, v2);
                    double       orient_norm = v1.norm() * v2.norm();
					bool 		 ccw    = orient > 0;
                    bool         obtuse = v1.dot(v2) < 0.;
					if (! ccw)
						orient = - orient;
					assert(orient != 0.);
                    if (obtuse || orient > orient_norm * sin_threshold) {
						// Angle is bigger than the threshold, therefore the arc will be discretized.
                        double angle = asin(orient / orient_norm);
                        if (obtuse)
							angle = M_PI - angle;
						size_t n_steps = size_t(ceil(angle / angle_step));
						double astep = angle / n_steps;
						if (! ccw)
							astep *= -1.;
						double a = astep;
						for (size_t i = 1; i < n_steps; ++ i, a += astep) {
							double c = cos(a);
							double s = sin(a);
							Vec2d  p = center + Vec2d(c * v1.x() - s * v1.y(), s * v1.x() + c * v1.y());
                            poly.points.emplace_back(Point(coord_t(p.x()), coord_t(p.y())));
						}
					}
				}
                poly.points.emplace_back(Point(coord_t(p2.x()), coord_t(p2.y())));
		        // although we may revisit current_face (if it is non-convex), it seems safe to mark it "done" here.
		        seed_cells[cell - &vd.cells().front()] = false;
                edge = next_edge->twin();
		    } while (edge != start_edge);
		    out.emplace_back(std::move(poly));
		}

	return out;
}

} // namespace Slic3r
