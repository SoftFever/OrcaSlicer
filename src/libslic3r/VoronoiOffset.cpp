// Polygon offsetting using Voronoi diagram prodiced by boost::polygon.

#include "VoronoiOffset.hpp"

#include <cmath>

// #define VORONOI_DEBUG_OUT

#ifdef VORONOI_DEBUG_OUT
#include <libslic3r/VoronoiVisualUtils.hpp>
#endif

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

    struct Intersections
    {
        int     count;
        Vec2d   pts[2];
    };

    // Return maximum two points, that are at distance "d" from both points
    Intersections point_point_equal_distance_points(const Point &pt1, const Point &pt2, const double d)
    {
        // input points
        const auto cx = double(pt1.x());
        const auto cy = double(pt1.y());
        const auto qx = double(pt2.x());
        const auto qy = double(pt2.y());

        // Calculating determinant.
        auto x0  = 2. * qy;
        auto cx2 = cx * cx;
        auto cy2 = cy * cy;
        auto x5  = 2 * cx * qx;
        auto x6  = cy * x0;
        auto qx2 = qx * qx;
        auto qy2 = qy * qy;
        auto x9  = qx2 + qy2;
        auto x10 = cx2 + cy2 - x5 - x6 + x9;
        auto x11 = - cx2 - cy2;
        auto discr = x10 * (4. * d + x11 + x5 + x6 - qx2 - qy2);
        if (discr < 0.)
            // No intersection point found, the two circles are too far away.
            return Intersections { 0, { Vec2d(), Vec2d() } };

        // Some intersections are found.
        int  npoints = (discr > 0) ? 2 : 1;
        auto x1  = 2. * cy - x0;
        auto x2  = cx - qx;
        auto x12 = 0.5 * x2 * sqrt(discr) / x10;
        auto x13 = 0.5 * (cy + qy);
        auto x14 = - x12 + x13;
        auto x15 = x11 + x9;
        auto x16 = 0.5 / x2;
        auto x17 = x12 + x13;
        return Intersections { npoints, { Vec2d(- x16 * (x1 * x14 + x15), x14),
                                          Vec2d(- x16 * (x1 * x17 + x15), x17) } };
    }

    // Return maximum two points, that are at distance "d" from both the line and point.
    Intersections line_point_equal_distance_points(const Line &line, const Point &pt, const double d)
    {   
        assert(line.a != pt && line.b != pt);
        // Calculating two points of distance "d" to a ray and a point.
        // Point.
        auto   x0   = double(pt.x());
        auto   y0   = double(pt.y());
        // Ray equation. Vector (a, b) is perpendicular to line.
        auto   a    = double(line.a.y() - line.b.y());
        auto   b    = double(line.b.x() - line.a.x());
        // pt shall not lie on line.
        assert(std::abs((x0 - line.a.x()) * a + (y0 - line.a.y()) * b) < SCALED_EPSILON);
        // Orient line so that the vector (a, b) points towards pt.
        if (a * (x0 - line.a.x()) + b * (y0 - line.a.y()) < 0.)
            std::swap(x0, y0);
        double c    = - a * double(line.a.x()) - b * double(line.a.y());
        // Calculate the two points.
        double a2   = a * a;
        double b2   = b * b;
        double a2b2 = a2 + b2;
        double d2   = d * d;
        double s    = a2*d2 - a2*sqr(x0) - 2*a*b*x0*y0 - 2*a*c*x0 + 2*a*d*x0 + b2*d2 - b2*sqr(y0) - 2*b*c*y0 + 2*b*d*y0 - sqr(c) + 2*c*d - d2;
        if (s < 0.)
            // Distance of pt from line is bigger than 2 * d.
            return Intersections { 0 };
        double u;
        int    cnt;
        if (s == 0.) {
            // Distance of pt from line is 2 * d.
            cnt = 1;
            u   = 0.;
        } else {
            // Distance of pt from line is smaller than 2 * d.
            cnt = 2;
            u = a*sqrt(s)/a2b2;
        }
        double v = (-a2*y0 + a*b*x0 + b*c - b*d)/a2b2;
        return Intersections { cnt, { Vec2d((b * (  u + v) - c + d) / a, - u - v),
                                      Vec2d((b * (- u + v) - c + d) / a,   u - v) } };
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

static Vec2d foot_pt(const Line &iline, const Point &ipt)
{
    Vec2d  pt  = iline.a.cast<double>();
    Vec2d  dir = (iline.b - iline.a).cast<double>();
    Vec2d  v   = ipt.cast<double>() - pt;
    double l2  = dir.squaredNorm();
    double t   = (l2 == 0.) ? 0. : v.dot(dir) / l2;
    return pt + dir * t;
}

Polygons voronoi_offset(
    const Geometry::VoronoiDiagram  &vd, 
    const Lines                     &lines, 
    double                           offset_distance, 
    double                           discretization_error)
{
#ifndef NDEBUG
    // Verify that twin halfedges are stored next to the other in vd.
    for (size_t i = 0; i < vd.num_edges(); i += 2) {
        const VD::edge_type &e  = vd.edges()[i];
        const VD::edge_type &e2 = vd.edges()[i + 1];
        assert(e.twin() == &e2);
        assert(e2.twin() == &e);
        assert(e.is_secondary() == e2.is_secondary());
        if (e.is_secondary()) {
            assert(e.cell()->contains_point() != e2.cell()->contains_point());
            const VD::edge_type &ex = (e.cell()->contains_point() ? e : e2);
            // Verify that the Point defining the cell left of ex is an end point of a segment
            // defining the cell right of ex.
            const Line  &line0 = lines[ex.cell()->source_index()];
            const Line  &line1 = lines[ex.twin()->cell()->source_index()];
            const Point &pt    = (ex.cell()->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line0.a : line0.b;
            assert(pt == line1.a || pt == line1.b);
        }
    }
#endif // NDEBUG

    // Mark edges with outward vertex pointing outside the polygons, thus there is a chance
    // that such an edge will have an intersection with our desired offset curve.
    bool                 outside = offset_distance > 0.;
    std::vector<char>    edge_candidate(vd.num_edges(), 2); // unknown state
    const VD::edge_type *front_edge = &vd.edges().front();
    for (const VD::edge_type &edge : vd.edges())
        if (edge.vertex1() == nullptr) {
            // Infinite Voronoi edge separating two Point sites.
            // Infinite edge is always outside and it has at least one valid vertex.
            assert(edge.vertex0() != nullptr);
            edge_candidate[&edge - front_edge] = outside;
            // Opposite edge of an infinite edge is certainly not active.
            edge_candidate[edge.twin() - front_edge] = 0;
        } else if (edge.vertex1() != nullptr) {
            // Finite edge.
            const VD::cell_type *cell = edge.cell();
            const Line          *line = cell->contains_segment() ? &lines[cell->source_index()] : nullptr;
            if (line == nullptr) {
                cell = edge.twin()->cell();
                line = cell->contains_segment() ? &lines[cell->source_index()] : nullptr;
            }
            if (line) {
                const VD::vertex_type *v1 = edge.vertex1();
                assert(v1);
                Vec2d l0(line->a.cast<double>());
                Vec2d lv((line->b - line->a).cast<double>());
                double side = cross2(lv, Vec2d(v1->x(), v1->y()) - l0);
                edge_candidate[&edge - front_edge] = outside ? (side < 0.) : (side > 0.);
            }
        }
    for (const VD::edge_type &edge : vd.edges())
        if (edge_candidate[&edge - front_edge] == 2) {
            assert(edge.cell()->contains_point() && edge.twin()->cell()->contains_point());
            // Edge separating two point sources, not yet classified as inside / outside.
            const VD::edge_type *e = &edge;
            char state;
            do {
                state = edge_candidate[e - front_edge];
                if (state != 2)
                    break;
                e = e->next();
            } while (e != &edge);
            e = &edge;
            do {
                char &s = edge_candidate[e - front_edge];
                if (s == 2) {
                    assert(e->cell()->contains_point() && e->twin()->cell()->contains_point());
                    assert(edge_candidate[e->twin() - front_edge] == 2);
                    s = state;
                    edge_candidate[e->twin() - front_edge] = state;
                }
                e = e->next();
            } while (e != &edge);
        }
    if (! outside)
        offset_distance = - offset_distance;

#ifdef VORONOI_DEBUG_OUT
    BoundingBox bbox;
    {
        bbox.merge(get_extents(lines));
        bbox.min -= (0.01 * bbox.size().cast<double>()).cast<coord_t>();
        bbox.max += (0.01 * bbox.size().cast<double>()).cast<coord_t>();
    }
    {
        Lines helper_lines;
        for (const VD::edge_type &edge : vd.edges())
            if (edge_candidate[&edge - front_edge]) {
                const VD::vertex_type *v0 = edge.vertex0();
                const VD::vertex_type *v1 = edge.vertex1();
                assert(v0 != nullptr);
                Vec2d pt1(v0->x(), v0->y());
                Vec2d pt2;
                if (v1 == nullptr) {
                    // Unconstrained edge. Calculate a trimmed position.
                    assert(edge.is_linear());
                    const VD::cell_type *cell  = edge.cell();
                    const VD::cell_type *cell2 = edge.twin()->cell();
                    const Line          &line0 = lines[cell->source_index()];
                    const Line          &line1 = lines[cell2->source_index()];
                    if (cell->contains_point() && cell2->contains_point()) {
                        const Point &pt0 = (cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line0.a : line0.b;
                        const Point &pt1 = (cell2->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line1.a : line1.b;
                        // Direction vector of this unconstrained Voronoi edge.
                        Vec2d dir(double(pt0.y() - pt1.y()), double(pt1.x() - pt0.x()));
                        pt2 = Vec2d(v0->x(), v0->y()) + dir.normalized() * scale_(10.);
                    } else {
                        // Infinite edges could not be created by two segment sites.
                        assert(cell->contains_point() != cell2->contains_point());
                        // Linear edge goes through the endpoint of a segment.
                        assert(edge.is_secondary());
                        const Point &ipt = cell->contains_segment() ?
                            ((cell2->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line1.a : line1.b) :
                            ((cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line0.a : line0.b);
                        // Infinite edge starts at an input contour, therefore there is always an intersection with an offset curve.
                        const Line  &line = cell->contains_segment() ? line0 : line1;
                        assert(line.a == ipt || line.b == ipt);
                        // dir is perpendicular to line.
                        Vec2d dir(line.a.y() - line.b.y(), line.b.x() - line.a.x());
                        assert(dir.norm() > 0.);
                        if (((line.a == ipt) == cell->contains_point()) == (v0 == nullptr))
                            dir = - dir;
                        pt2 = ipt.cast<double>() + dir.normalized() * scale_(10.);
                    }
                } else {
                    pt2 = Vec2d(v1->x(), v1->y());
                    // Clip the line by the bounding box, so that the coloring of the line will be visible.
                    Geometry::liang_barsky_line_clipping(pt1, pt2, BoundingBoxf(bbox.min.cast<double>(), bbox.max.cast<double>()));
                }
                helper_lines.emplace_back(Line(Point(pt1.cast<coord_t>()), Point(((pt1 + pt2) * 0.5).cast<coord_t>())));
            }
        dump_voronoi_to_svg(debug_out_path("voronoi-offset-candidates1.svg").c_str(), vd, Points(), lines, Polygons(), helper_lines);
    }
#endif // VORONOI_DEBUG_OUT

    std::vector<Vec2d> edge_offset_point(vd.num_edges(), Vec2d());
    const double offset_distance2 = offset_distance * offset_distance;
    for (const VD::edge_type &edge : vd.edges()) {
        assert(edge_candidate[&edge - front_edge] != 2);
        size_t edge_idx = &edge - front_edge;
        if (edge_candidate[edge_idx] == 1) {
            // Edge candidate, intersection points were not calculated yet.
            const VD::vertex_type *v0    = edge.vertex0();
            const VD::vertex_type *v1    = edge.vertex1();
            assert(v0 != nullptr);
            const VD::cell_type   *cell  = edge.cell();
            const VD::cell_type   *cell2 = edge.twin()->cell();
            const Line            &line0 = lines[cell->source_index()];
            const Line            &line1 = lines[cell2->source_index()];
            size_t                 edge_idx2 = edge.twin() - front_edge;
            if (v1 == nullptr) {
                assert(edge.is_infinite());
                assert(edge_candidate[edge_idx2] == 0);
                if (cell->contains_point() && cell2->contains_point()) {
                    const Point &pt0 = (cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line0.a : line0.b;
                    const Point &pt1 = (cell2->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line1.a : line1.b;
                    double dmin2 = (Vec2d(v0->x(), v0->y()) - pt0.cast<double>()).squaredNorm();
                    if (dmin2 <= offset_distance2) {
                        // There shall be an intersection of this unconstrained edge with the offset curve.
                        // Direction vector of this unconstrained Voronoi edge.
                        Vec2d dir(double(pt0.y() - pt1.y()), double(pt1.x() - pt0.x()));
                        Vec2d pt(v0->x(), v0->y());
                        double t = detail::first_circle_segment_intersection_parameter(Vec2d(pt0.x(), pt0.y()), offset_distance, pt, dir);
                        edge_offset_point[edge_idx] = pt + t * dir;
                        edge_candidate[edge_idx] = 3;
                    } else
                        edge_candidate[edge_idx] = 0;
                } else {
                    // Infinite edges could not be created by two segment sites.
                    assert(cell->contains_point() != cell2->contains_point());
                    // Linear edge goes through the endpoint of a segment.
                    assert(edge.is_linear());
                    assert(edge.is_secondary());
                    const Point &ipt = cell->contains_segment() ?
                        ((cell2->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line1.a : line1.b) :
                        ((cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line0.a : line0.b);
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
                    assert((Vec2d(v0->x(), v0->y()) - ipt.cast<double>()).norm() < SCALED_EPSILON);
    #endif /* NDEBUG */
                    // Infinite edge starts at an input contour, therefore there is always an intersection with an offset curve.
                    const Line  &line = cell->contains_segment() ? line0 : line1;
                    assert(line.a == ipt || line.b == ipt);
                    Vec2d pt = ipt.cast<double>();
                    Vec2d dir(line.a.y() - line.b.y(), line.b.x() - line.a.x());
                    assert(dir.norm() > 0.);
                    double t = offset_distance / dir.norm();
                    if (((line.a == ipt) == cell->contains_point()) == (v0 == nullptr))
                        t = - t;
                    edge_offset_point[edge_idx] = pt + t * dir;
                    edge_candidate[edge_idx] = 3;
                }
                // The other edge of an unconstrained edge starting with null vertex shall never be intersected.
                edge_candidate[edge_idx2] = 0;
            } else if (edge.is_secondary()) {
                assert(cell->contains_point() != cell2->contains_point());
                const Line  &line0 = lines[edge.cell()->source_index()];
                const Line  &line1 = lines[edge.twin()->cell()->source_index()];
                const Point &pt    = cell->contains_point() ?
                    ((cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line0.a : line0.b) :
                    ((cell2->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line1.a : line1.b);
                const Line  &line  = cell->contains_segment() ? line0 : line1;
                assert(pt == line.a || pt == line.b);
                assert((pt.cast<double>() - Vec2d(v0->x(), v0->y())).norm() < SCALED_EPSILON);
                Vec2d dir(v1->x() - v0->x(), v1->y() - v0->y());
                double l2 = dir.squaredNorm();
                if (offset_distance2 <= l2) {
                    edge_offset_point[edge_idx] = pt.cast<double>() + (offset_distance / sqrt(l2)) * dir;
                    edge_candidate[edge_idx] = 3;
                } else {
                    edge_candidate[edge_idx] = 0;
                }
                edge_candidate[edge_idx2] = 0;
            } else {
                // Finite edge has valid points at both sides.
                bool done = false;
                if (cell->contains_segment() && cell2->contains_segment()) {
                    // This edge is a bisector of two line segments. Project v0, v1 onto one of the line segments.
                    Vec2d  pt(line0.a.cast<double>());
                    Vec2d  dir(line0.b.cast<double>() - pt);
                    Vec2d  vec0 = Vec2d(v0->x(), v0->y()) - pt;
                    Vec2d  vec1 = Vec2d(v1->x(), v1->y()) - pt;
                    double l2   = dir.squaredNorm();
                    assert(l2 > 0.);
                    double dmin = (dir * (vec0.dot(dir) / l2) - vec0).squaredNorm();
                    double dmax = (dir * (vec1.dot(dir) / l2) - vec1).squaredNorm();
                    bool   flip = dmin > dmax;
                    if (flip)
                        std::swap(dmin, dmax);
                    if (offset_distance2 >= dmin && offset_distance2 <= dmax) {
                        // Intersect. Maximum one intersection will be found.
                        // This edge is a bisector of two line segments. Distance to the input polygon increases/decreases monotonically.
                        dmin = sqrt(dmin);
                        dmax = sqrt(dmax);
                        assert(offset_distance > dmin - EPSILON && offset_distance < dmax + EPSILON);
                        double ddif = dmax - dmin;
                        if (ddif == 0.) {
                            // line, line2 are exactly parallel. This is a singular case, the offset curve should miss it.
                        } else {
                            if (flip) {
                                std::swap(edge_idx, edge_idx2);
                                std::swap(v0, v1);
                            }
                            double t = clamp(0., 1., (offset_distance - dmin) / ddif);
                            edge_offset_point[edge_idx] = Vec2d(lerp(v0->x(), v1->x(), t), lerp(v0->y(), v1->y(), t));
                            edge_candidate[edge_idx] = 3;
                            edge_candidate[edge_idx2] = 0;
                            done = true;
                        }
                    }
                } else {
                    assert(cell->contains_point() || cell2->contains_point());
                    bool point_vs_segment = cell->contains_point() != cell2->contains_point();
                    const Point &pt0 = cell->contains_point() ?
                        ((cell->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line0.a : line0.b) :
                        ((cell2->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line1.a : line1.b);
                    // Project p0 to line segment <v0, v1>.
                    Vec2d p0(v0->x(), v0->y());
                    Vec2d p1(v1->x(), v1->y());
                    Vec2d px(pt0.x(), pt0.y());
                    double d0 = (p0 - px).squaredNorm();
                    double d1 = (p1 - px).squaredNorm();
                    double dmin = std::min(d0, d1);
                    double dmax = std::max(d0, d1);
                    bool has_intersection = false;
                    if (offset_distance2 <= dmax) {
                        if (offset_distance2 >= dmin) {
                            has_intersection = true;
                        } else {
                            double dmin_new;
                            if (point_vs_segment) {
                                Vec2d ft = foot_pt(cell->contains_segment() ? line0 : line1, pt0);
                                dmin_new = (ft - px).squaredNorm() * 0.25;
                            } else {
                                // point vs. point
                                const Point &pt1 = (cell2->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line1.a : line1.b;
                                dmin_new = (pt1.cast<double>() - px).squaredNorm() * 0.25;
                            }
                            assert(dmin_new < dmax + SCALED_EPSILON);
                            assert(dmin_new < dmin + SCALED_EPSILON);
                            dmin = dmin_new;
                            has_intersection = offset_distance2 >= dmin;
                        }
                    }
                    if (has_intersection) {
                        detail::Intersections intersections;
                        if (point_vs_segment) {
                            assert(cell->contains_point() || cell2->contains_point());
                            intersections = detail::line_point_equal_distance_points(cell->contains_segment() ? line0 : line1, pt0, offset_distance);
                        } else {
                            const Point &pt1 = (cell2->source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line1.a : line1.b;
                            intersections = detail::point_point_equal_distance_points(pt0, pt1, offset_distance);
                        }
                        if (intersections.count == 2) {
                            // Now decide which points fall on this Voronoi edge.
                            // Tangential points (single intersection) are ignored.
                            Vec2d  v  = p1 - p0;
                            double l2 = v.squaredNorm();
                            double t0 = v.dot(intersections.pts[0] - p0);
                            double t1 = v.dot(intersections.pts[1] - p0);
                            if (t0 > t1) {
                                std::swap(t0, t1);
                                std::swap(intersections.pts[0], intersections.pts[1]);
                            }
                            // Remove points outside of the line range.
                            if (t0 < 0. || t0 > l2) {
                                if (t1 < 0. || t1 > l2)
                                    intersections.count = 0;
                                else {
                                    -- intersections.count;
                                    t0 = t1;
                                    intersections.pts[0] = intersections.pts[1];
                                }
                            } else if (t1 < 0. || t1 > l2)
                                -- intersections.count;
                            if (intersections.count == 2) {
                                edge_candidate[edge_idx] = edge_candidate[edge_idx2] = 3;
                                edge_offset_point[edge_idx]  = intersections.pts[0];
                                edge_offset_point[edge_idx2] = intersections.pts[1];
                                done = true;
                            } else if (intersections.count == 1) {
                                if (d1 > d0) {
                                    std::swap(edge_idx, edge_idx2);
                                    edge_candidate[edge_idx] = 3;
                                    edge_candidate[edge_idx2] = 0;
                                    edge_offset_point[edge_idx] = intersections.pts[0];
                                }
                                done = true;
                            }
                        }
                        if (! done)
                            edge_candidate[edge_idx] = edge_candidate[edge_idx2] = 0;
                    }
                }
            }
        }
    }


#ifdef VORONOI_DEBUG_OUT
    {
        Lines helper_lines;
        for (const VD::edge_type &edge : vd.edges())
            if (edge_candidate[&edge - front_edge] == 3)
                helper_lines.emplace_back(Line(Point(edge.vertex0()->x(), edge.vertex0()->y()), Point(edge_offset_point[&edge - front_edge].cast<coord_t>())));
        dump_voronoi_to_svg(debug_out_path("voronoi-offset-candidates2.svg").c_str(), vd, Points(), lines, Polygons(), helper_lines);
    }
#endif // VORONOI_DEBUG_OUT

    auto next_offset_edge = [&edge_candidate, front_edge](const VD::edge_type *start_edge) -> const VD::edge_type* {
	    for (const VD::edge_type *edge = start_edge->next(); edge != start_edge; edge = edge->next())
            if (edge_candidate[edge->twin() - front_edge] == 3)
                return edge->twin();
	    assert(false);
        return nullptr;
	};

#ifndef NDEBUG
	auto dist_to_site = [&lines](const VD::cell_type &cell, const Vec2d &point) {
        const Line &line = lines[cell.source_index()];
        return cell.contains_point() ?
            (((cell.source_category() == boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT) ? line.a : line.b).cast<double>() - point).norm() :
            line.distance_to(point.cast<coord_t>());
	};
#endif /* NDEBUG */

	// Track the offset curves.
	Polygons out;
	double angle_step    = 2. * acos((offset_distance - discretization_error) / offset_distance);
	double sin_threshold = sin(angle_step) + EPSILON;
	for (size_t seed_edge_idx = 0; seed_edge_idx < vd.num_edges(); ++ seed_edge_idx)
		if (edge_candidate[seed_edge_idx] == 3) {
            const VD::edge_type *start_edge = &vd.edges()[seed_edge_idx];
            const VD::edge_type *edge       = start_edge;
            Polygon  			 poly;
		    do {
		        // find the next edge
                const VD::edge_type  *next_edge = next_offset_edge(edge);
		        //std::cout << "offset-output: "; print_edge(edge); std::cout << " to "; print_edge(next_edge); std::cout << "\n";
		        // Interpolate a circular segment or insert a linear segment between edge and next_edge.
                const VD::cell_type  *cell      = edge->cell();
                edge_candidate[next_edge - front_edge] = 0;
                Vec2d p1 = edge_offset_point[edge - front_edge];
                Vec2d p2 = edge_offset_point[next_edge - front_edge];
#ifndef NDEBUG
                {
                    double err = dist_to_site(*cell, p1) - offset_distance;
                    assert(std::abs(err) < SCALED_EPSILON);
                    err = dist_to_site(*cell, p2) - offset_distance;
                    assert(std::abs(err) < SCALED_EPSILON);
                }
#endif /* NDEBUG */
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
                edge = next_edge;
		    } while (edge != start_edge);
		    out.emplace_back(std::move(poly));
		}

	return out;
}

} // namespace Slic3r
