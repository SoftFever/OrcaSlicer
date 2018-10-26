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
	bool signed_distance_edges(const Point &pt, coord_t search_radius, coordf_t &result_min_dist, bool *pon_segment = NULL) const;

	// Calculate a signed distance to the contours in search_radius from the point. If no edge is found in search_radius,
	// return an interpolated value from m_signed_distance_field, if it exists.
	bool signed_distance(const Point &pt, coord_t search_radius, coordf_t &result_min_dist) const;

	const BoundingBox& 	bbox() const { return m_bbox; }
	const coord_t 		resolution() const { return m_resolution; }
	const size_t		rows() const { return m_rows; }
	const size_t		cols() const { return m_cols; }

	// For supports: Contours enclosing the rasterized edges.
	Polygons 			contours_simplified(coord_t offset, bool fill_holes) const;

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
		if (r < 0 || r >= m_rows ||
			c < 0 || c >= m_cols)
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
} // namespace Slic3r

#endif /* slic3r_EdgeGrid_hpp_ */
