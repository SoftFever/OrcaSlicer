#ifndef slic3r_EdgeGrid_hpp_
#define slic3r_EdgeGrid_hpp_

#include <stdint.h>
#include <math.h>

#include "Point.hpp"
#include "BoundingBox.hpp"
#include "ExPolygon.hpp"
#include "ExPolygonCollection.hpp"

namespace Slic3r {
namespace EdgeGrid {

class Grid
{
public:
	Grid();
	~Grid();

	void set_bbox(const BoundingBox &bbox) { m_bbox = bbox; }

	void create(const Polygons &polygons, coord_t resolution);
	void create(const ExPolygon &expoly, coord_t resolution);
	void create(const ExPolygons &expolygons, coord_t resolution);
	void create(const ExPolygonCollection &expolygons, coord_t resolution);

	const std::vector<const Slic3r::Points*>& contours() const { return m_contours; }

#if 0
	// Test, whether the edges inside the grid intersect with the polygons provided.
	bool intersect(const MultiPoint &polyline, bool closed);
	bool intersect(const Polygon &polygon) { return intersect(static_cast<const MultiPoint&>(polygon), true); }
	bool intersect(const Polygons &polygons) { for (size_t i = 0; i < polygons.size(); ++ i) if (intersect(polygons[i])) return true; return false; }
	bool intersect(const ExPolygon &expoly) { if (intersect(expoly.contour)) return true; for (size_t i = 0; i < expoly.holes.size(); ++ i) if (intersect(expoly.holes[i])) return true; return false; }
	bool intersect(const ExPolygons &expolygons) { for (size_t i = 0; i < expolygons.size(); ++ i) if (intersect(expolygons[i])) return true; return false; }
	bool intersect(const ExPolygonCollection &expolygons) { return intersect(expolygons.expolygons); }

	// Test, whether a point is inside a contour.
	bool inside(const Point &pt);
#endif

	// Fill in a rough m_signed_distance_field from the edge grid.
	// The rough SDF is used by signed_distance() for distances outside of the search_radius.
	void calculate_sdf();

	// Return an estimate of the signed distance based on m_signed_distance_field grid.
	float signed_distance_bilinear(const Point &pt) const;

	// Calculate a signed distance to the contours in search_radius from the point.
	struct ClosestPointResult {
		size_t contour_idx  	= size_t(-1);
		size_t start_point_idx  = size_t(-1);
		// Signed distance to the closest point.
		double distance 		= std::numeric_limits<double>::max();
		// Parameter of the closest point on edge starting with start_point_idx <0, 1)
		double t 				= 0.;

		bool valid() const { return contour_idx != size_t(-1); }
	};
	ClosestPointResult closest_point(const Point &pt, coord_t search_radius) const;

	bool signed_distance_edges(const Point &pt, coord_t search_radius, coordf_t &result_min_dist, bool *pon_segment = nullptr) const;

	// Calculate a signed distance to the contours in search_radius from the point. If no edge is found in search_radius,
	// return an interpolated value from m_signed_distance_field, if it exists.
	bool signed_distance(const Point &pt, coord_t search_radius, coordf_t &result_min_dist) const;

	const BoundingBox& 	bbox() const { return m_bbox; }
	const coord_t 		resolution() const { return m_resolution; }
	const size_t		rows() const { return m_rows; }
	const size_t		cols() const { return m_cols; }

	// For supports: Contours enclosing the rasterized edges.
	Polygons 			contours_simplified(coord_t offset, bool fill_holes) const;

	typedef std::pair<const Slic3r::Points*, size_t> ContourPoint;
	typedef std::pair<const Slic3r::Points*, size_t> ContourEdge;
	std::vector<std::pair<ContourEdge, ContourEdge>> intersecting_edges() const;
	bool 											 has_intersecting_edges() const;

	template<typename VISITOR> void visit_cells_intersecting_line(Slic3r::Point p1, Slic3r::Point p2, VISITOR &visitor) const
	{
		// End points of the line segment.
		p1(0) -= m_bbox.min(0);
		p1(1) -= m_bbox.min(1);
		p2(0) -= m_bbox.min(0);
		p2(1) -= m_bbox.min(1);
		// Get the cells of the end points.
		coord_t ix = p1(0) / m_resolution;
		coord_t iy = p1(1) / m_resolution;
		coord_t ixb = p2(0) / m_resolution;
		coord_t iyb = p2(1) / m_resolution;
		assert(ix >= 0 && size_t(ix) < m_cols);
		assert(iy >= 0 && size_t(iy) < m_rows);
		assert(ixb >= 0 && size_t(ixb) < m_cols);
		assert(iyb >= 0 && size_t(iyb) < m_rows);
		// Account for the end points.
		if (! visitor(iy, ix) || (ix == ixb && iy == iyb))
			// Both ends fall into the same cell.
			return;
		// Raster the centeral part of the line.
		coord_t dx = std::abs(p2(0) - p1(0));
		coord_t dy = std::abs(p2(1) - p1(1));
		if (p1(0) < p2(0)) {
			int64_t ex = int64_t((ix + 1)*m_resolution - p1(0)) * int64_t(dy);
			if (p1(1) < p2(1)) {
				// x positive, y positive
				int64_t ey = int64_t((iy + 1)*m_resolution - p1(1)) * int64_t(dx);
				do {
					assert(ix <= ixb && iy <= iyb);
					if (ex < ey) {
						ey -= ex;
						ex = int64_t(dy) * m_resolution;
						ix += 1;
					}
					else if (ex == ey) {
						ex = int64_t(dy) * m_resolution;
						ey = int64_t(dx) * m_resolution;
						ix += 1;
						iy += 1;
					}
					else {
						assert(ex > ey);
						ex -= ey;
						ey = int64_t(dx) * m_resolution;
						iy += 1;
					}
					if (! visitor(iy, ix))
						return;
				} while (ix != ixb || iy != iyb);
			}
			else {
				// x positive, y non positive
				int64_t ey = int64_t(p1(1) - iy*m_resolution) * int64_t(dx);
				do {
					assert(ix <= ixb && iy >= iyb);
					if (ex <= ey) {
						ey -= ex;
						ex = int64_t(dy) * m_resolution;
						ix += 1;
					}
					else {
						ex -= ey;
						ey = int64_t(dx) * m_resolution;
						iy -= 1;
					}
					if (! visitor(iy, ix))
						return;
				} while (ix != ixb || iy != iyb);
			}
		}
		else {
			int64_t ex = int64_t(p1(0) - ix*m_resolution) * int64_t(dy);
			if (p1(1) < p2(1)) {
				// x non positive, y positive
				int64_t ey = int64_t((iy + 1)*m_resolution - p1(1)) * int64_t(dx);
				do {
					assert(ix >= ixb && iy <= iyb);
					if (ex < ey) {
						ey -= ex;
						ex = int64_t(dy) * m_resolution;
						ix -= 1;
					}
					else {
						assert(ex >= ey);
						ex -= ey;
						ey = int64_t(dx) * m_resolution;
						iy += 1;
					}
					if (! visitor(iy, ix))
						return;
				} while (ix != ixb || iy != iyb);
			}
			else {
				// x non positive, y non positive
				int64_t ey = int64_t(p1(1) - iy*m_resolution) * int64_t(dx);
				do {
					assert(ix >= ixb && iy >= iyb);
					if (ex < ey) {
						ey -= ex;
						ex = int64_t(dy) * m_resolution;
						ix -= 1;
					}
					else if (ex == ey) {
						// The lower edge of a grid cell belongs to the cell.
						// Handle the case where the ray may cross the lower left corner of a cell in a general case,
						// or a left or lower edge in a degenerate case (horizontal or vertical line).
						if (dx > 0) {
							ex = int64_t(dy) * m_resolution;
							ix -= 1;
						}
						if (dy > 0) {
							ey = int64_t(dx) * m_resolution;
							iy -= 1;
						}
					}
					else {
						assert(ex > ey);
						ex -= ey;
						ey = int64_t(dx) * m_resolution;
						iy -= 1;
					}
					if (! visitor(iy, ix))
						return;
				} while (ix != ixb || iy != iyb);
			}
		}
	}

	std::pair<std::vector<std::pair<size_t, size_t>>::const_iterator, std::vector<std::pair<size_t, size_t>>::const_iterator> cell_data_range(coord_t row, coord_t col) const
	{
		const EdgeGrid::Grid::Cell &cell = m_cells[row * m_cols + col];
		return std::make_pair(m_cell_data.begin() + cell.begin, m_cell_data.begin() + cell.end);
	}

	std::pair<const Slic3r::Point&, const Slic3r::Point&> segment(const std::pair<size_t, size_t> &contour_and_segment_idx) const
	{
		const Slic3r::Points &ipts = *m_contours[contour_and_segment_idx.first];
		size_t ipt = contour_and_segment_idx.second;
		return std::pair<const Slic3r::Point&, const Slic3r::Point&>(ipts[ipt], ipts[(ipt + 1 == ipts.size()) ? 0 : ipt + 1]);
	}

protected:
	struct Cell {
		Cell() : begin(0), end(0) {}
		size_t begin;
		size_t end;
	};

	void create_from_m_contours(coord_t resolution);
#if 0
	bool line_cell_intersect(const Point &p1, const Point &p2, const Cell &cell);
#endif
	bool cell_inside_or_crossing(int r, int c) const
	{
		if (r < 0 || (size_t)r >= m_rows ||
			c < 0 || (size_t)c >= m_cols)
			// The cell is outside the domain. Hoping that the contours were correctly oriented, so
			// there is a CCW outmost contour so the out of domain cells are outside.
			return false;
		const Cell &cell = m_cells[r * m_cols + c];
		return 
			(cell.begin < cell.end) || 
			(! m_signed_distance_field.empty() && m_signed_distance_field[r * (m_cols + 1) + c] <= 0.f);
	}

	// Bounding box around the contours.
	BoundingBox 								m_bbox;
	// Grid dimensions.
	coord_t										m_resolution;
	size_t										m_rows;
	size_t										m_cols;

	// Referencing the source contours.
	// This format allows one to work with any Slic3r fixed point contour format
	// (Polygon, ExPolygon, ExPolygonCollection etc).
	std::vector<const Slic3r::Points*>			m_contours;

	// Referencing a contour and a line segment of m_contours.
	std::vector<std::pair<size_t, size_t> >		m_cell_data;

	// Full grid of cells.
	std::vector<Cell> 							m_cells;

	// Distance field derived from the edge grid, seed filled by the Danielsson chamfer metric.
	// May be empty.
	std::vector<float>							m_signed_distance_field;
};

#if 0
// Debugging utility. Save the signed distance field.
extern void save_png(const Grid &grid, const BoundingBox &bbox, coord_t resolution, const char *path);
#endif /* SLIC3R_GUI */

} // namespace EdgeGrid

// Find all pairs of intersectiong edges from the set of polygons.
extern std::vector<std::pair<EdgeGrid::Grid::ContourEdge, EdgeGrid::Grid::ContourEdge>> intersecting_edges(const Polygons &polygons);

// Find all pairs of intersectiong edges from the set of polygons, highlight them in an SVG.
extern void export_intersections_to_svg(const std::string &filename, const Polygons &polygons);

} // namespace Slic3r

#endif /* slic3r_EdgeGrid_hpp_ */
