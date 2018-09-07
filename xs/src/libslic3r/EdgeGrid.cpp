#include <algorithm>
#include <vector>
#include <float.h>
#include <unordered_map>

#if 0
// #ifdef SLIC3R_GUI
#include <wx/image.h>
#endif /* SLIC3R_GUI */

#include "libslic3r.h"
#include "EdgeGrid.hpp"

#if 0
// Enable debugging and assert in this file.
#define DEBUG
#define _DEBUG
#undef NDEBUG
#endif

#include <assert.h>

namespace Slic3r {

EdgeGrid::Grid::Grid() : 
	m_rows(0), m_cols(0) 
{
}

EdgeGrid::Grid::~Grid() 
{
	m_contours.clear();
	m_cell_data.clear();
	m_cells.clear();
}

void EdgeGrid::Grid::create(const Polygons &polygons, coord_t resolution)
{
	// Count the contours.
	size_t ncontours = 0;
	for (size_t j = 0; j < polygons.size(); ++ j)
		if (! polygons[j].points.empty())
			++ ncontours;

	// Collect the contours.
	m_contours.assign(ncontours, NULL);
	ncontours = 0;
	for (size_t j = 0; j < polygons.size(); ++ j)
		if (! polygons[j].points.empty())
			m_contours[ncontours++] = &polygons[j].points;

	create_from_m_contours(resolution);
}

void EdgeGrid::Grid::create(const ExPolygon &expoly, coord_t resolution)
{
	// Count the contours.
	size_t ncontours = 0;
	if (! expoly.contour.points.empty())
		++ ncontours;
	for (size_t j = 0; j < expoly.holes.size(); ++ j)
		if (! expoly.holes[j].points.empty())
			++ ncontours;

	// Collect the contours.
	m_contours.assign(ncontours, NULL);
	ncontours = 0;
	if (! expoly.contour.points.empty())
		m_contours[ncontours++] = &expoly.contour.points;
	for (size_t j = 0; j < expoly.holes.size(); ++ j)
		if (! expoly.holes[j].points.empty())
			m_contours[ncontours++] = &expoly.holes[j].points;

	create_from_m_contours(resolution);
}

void EdgeGrid::Grid::create(const ExPolygons &expolygons, coord_t resolution)
{
	// Count the contours.
	size_t ncontours = 0;
	for (size_t i = 0; i < expolygons.size(); ++ i) {
		const ExPolygon &expoly = expolygons[i];
		if (! expoly.contour.points.empty())
			++ ncontours;
		for (size_t j = 0; j < expoly.holes.size(); ++ j)
			if (! expoly.holes[j].points.empty())
				++ ncontours;
	}

	// Collect the contours.
	m_contours.assign(ncontours, NULL);
	ncontours = 0;
	for (size_t i = 0; i < expolygons.size(); ++ i) {
		const ExPolygon &expoly = expolygons[i];
		if (! expoly.contour.points.empty())
			m_contours[ncontours++] = &expoly.contour.points;
		for (size_t j = 0; j < expoly.holes.size(); ++ j)
			if (! expoly.holes[j].points.empty())
				m_contours[ncontours++] = &expoly.holes[j].points;
	}

	create_from_m_contours(resolution);
}

void EdgeGrid::Grid::create(const ExPolygonCollection &expolygons, coord_t resolution)
{
	create(expolygons.expolygons, resolution);
}

// m_contours has been initialized. Now fill in the edge grid.
void EdgeGrid::Grid::create_from_m_contours(coord_t resolution)
{
	// 1) Measure the bounding box.
	for (size_t i = 0; i < m_contours.size(); ++ i) {
		const Slic3r::Points &pts = *m_contours[i];
		for (size_t j = 0; j < pts.size(); ++ j)
			m_bbox.merge(pts[j]);
	}
	coord_t eps = 16;
	m_bbox.min.x -= eps;
	m_bbox.min.y -= eps;
	m_bbox.max.x += eps;
	m_bbox.max.y += eps;

	// 2) Initialize the edge grid.
	m_resolution = resolution;
	m_cols = (m_bbox.max.x - m_bbox.min.x + m_resolution - 1) / m_resolution;
	m_rows = (m_bbox.max.y - m_bbox.min.y + m_resolution - 1) / m_resolution;
	m_cells.assign(m_rows * m_cols, Cell());

	// 3) First round of contour rasterization, count the edges per grid cell.
	for (size_t i = 0; i < m_contours.size(); ++ i) {
		const Slic3r::Points &pts = *m_contours[i];
		for (size_t j = 0; j < pts.size(); ++ j) {
			// End points of the line segment.
			Slic3r::Point p1(pts[j]);
			Slic3r::Point p2 = pts[(j + 1 == pts.size()) ? 0 : j + 1];
			p1.x -= m_bbox.min.x;
			p1.y -= m_bbox.min.y;
			p2.x -= m_bbox.min.x;
			p2.y -= m_bbox.min.y;
		   	// Get the cells of the end points.
		    coord_t ix    = p1.x / m_resolution;
		    coord_t iy    = p1.y / m_resolution;
		    coord_t ixb   = p2.x / m_resolution;
		    coord_t iyb   = p2.y / m_resolution;
			assert(ix >= 0 && ix < m_cols);
			assert(iy >= 0 && iy < m_rows);
			assert(ixb >= 0 && ixb < m_cols);
			assert(iyb >= 0 && iyb < m_rows);
			// Account for the end points.
			++ m_cells[iy*m_cols+ix].end;
			if (ix == ixb && iy == iyb)
				// Both ends fall into the same cell.
				continue;
		    // Raster the centeral part of the line.
			coord_t dx = std::abs(p2.x - p1.x);
			coord_t dy = std::abs(p2.y - p1.y);
			if (p1.x < p2.x) {
				int64_t ex = int64_t((ix + 1)*m_resolution - p1.x) * int64_t(dy);
				if (p1.y < p2.y) {
					// x positive, y positive
					int64_t ey = int64_t((iy + 1)*m_resolution - p1.y) * int64_t(dx);
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
						++m_cells[iy*m_cols + ix].end;
					} while (ix != ixb || iy != iyb);
				}
				else {
					// x positive, y non positive
					int64_t ey = int64_t(p1.y - iy*m_resolution) * int64_t(dx);
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
						++m_cells[iy*m_cols + ix].end;
					} while (ix != ixb || iy != iyb);
				}
			}
			else {
				int64_t ex = int64_t(p1.x - ix*m_resolution) * int64_t(dy);
				if (p1.y < p2.y) {
					// x non positive, y positive
					int64_t ey = int64_t((iy + 1)*m_resolution - p1.y) * int64_t(dx);
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
						++m_cells[iy*m_cols + ix].end;
					} while (ix != ixb || iy != iyb);
				}
				else {
					// x non positive, y non positive
					int64_t ey = int64_t(p1.y - iy*m_resolution) * int64_t(dx);
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
						++m_cells[iy*m_cols + ix].end;
					} while (ix != ixb || iy != iyb);
				}
			}
		}
	}

	// 4) Prefix sum the numbers of hits per cells to get an index into m_cell_data.
	size_t cnt = m_cells.front().end;
	for (size_t i = 1; i < m_cells.size(); ++ i) {
		m_cells[i].begin = cnt;
		cnt += m_cells[i].end;
		m_cells[i].end = cnt;
	}

	// 5) Allocate the cell data.
	m_cell_data.assign(cnt, std::pair<size_t, size_t>(size_t(-1), size_t(-1)));

	// 6) Finally fill in m_cell_data by rasterizing the lines once again.
	for (size_t i = 0; i < m_cells.size(); ++i)
		m_cells[i].end = m_cells[i].begin;
	for (size_t i = 0; i < m_contours.size(); ++i) {
		const Slic3r::Points &pts = *m_contours[i];
		for (size_t j = 0; j < pts.size(); ++j) {
			// End points of the line segment.
			Slic3r::Point p1(pts[j]);
			Slic3r::Point p2 = pts[(j + 1 == pts.size()) ? 0 : j + 1];
			p1.x -= m_bbox.min.x;
			p1.y -= m_bbox.min.y;
			p2.x -= m_bbox.min.x;
			p2.y -= m_bbox.min.y;
			// Get the cells of the end points.
			coord_t ix = p1.x / m_resolution;
			coord_t iy = p1.y / m_resolution;
			coord_t ixb = p2.x / m_resolution;
			coord_t iyb = p2.y / m_resolution;
			assert(ix >= 0 && ix < m_cols);
			assert(iy >= 0 && iy < m_rows);
			assert(ixb >= 0 && ixb < m_cols);
			assert(iyb >= 0 && iyb < m_rows);
			// Account for the end points.
			m_cell_data[m_cells[iy*m_cols + ix].end++] = std::pair<size_t, size_t>(i, j);
			if (ix == ixb && iy == iyb)
				// Both ends fall into the same cell.
				continue;
			// Raster the centeral part of the line.
			coord_t dx = std::abs(p2.x - p1.x);
			coord_t dy = std::abs(p2.y - p1.y);
			if (p1.x < p2.x) {
				int64_t ex = int64_t((ix + 1)*m_resolution - p1.x) * int64_t(dy);
				if (p1.y < p2.y) {
					// x positive, y positive
					int64_t ey = int64_t((iy + 1)*m_resolution - p1.y) * int64_t(dx);
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
						m_cell_data[m_cells[iy*m_cols + ix].end++] = std::pair<size_t, size_t>(i, j);
					} while (ix != ixb || iy != iyb);
				}
				else {
					// x positive, y non positive
					int64_t ey = int64_t(p1.y - iy*m_resolution) * int64_t(dx);
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
						m_cell_data[m_cells[iy*m_cols + ix].end++] = std::pair<size_t, size_t>(i, j);
					} while (ix != ixb || iy != iyb);
				}
			}
			else {
				int64_t ex = int64_t(p1.x - ix*m_resolution) * int64_t(dy);
				if (p1.y < p2.y) {
					// x non positive, y positive
					int64_t ey = int64_t((iy + 1)*m_resolution - p1.y) * int64_t(dx);
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
						m_cell_data[m_cells[iy*m_cols + ix].end++] = std::pair<size_t, size_t>(i, j);
					} while (ix != ixb || iy != iyb);
				}
				else {
					// x non positive, y non positive
					int64_t ey = int64_t(p1.y - iy*m_resolution) * int64_t(dx);
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
						m_cell_data[m_cells[iy*m_cols + ix].end++] = std::pair<size_t, size_t>(i, j);
					} while (ix != ixb || iy != iyb);
				}
			}
		}
	}
}

#if 0
// Divide, round to a grid coordinate.
// Divide x/y, round down. y is expected to be positive.
static inline coord_t div_floor(coord_t x, coord_t y)
{
	assert(y > 0);
	return ((x < 0) ? (x - y + 1) : x) / y;
}

// Walk the polyline, test whether any lines of this polyline does not intersect
// any line stored into the grid.
bool EdgeGrid::Grid::intersect(const MultiPoint &polyline, bool closed)
{
	size_t n = polyline.points.size();
	if (closed)
		++ n;
	for (size_t i = 0; i < n; ++ i) {
		size_t j = i + 1;
		if (j == polyline.points.size())
			j = 0;
		Point p1src = polyline.points[i];
		Point p2src = polyline.points[j];
		Point p1 = p1src;
		Point p2 = p2src;
		// Discretize the line segment p1, p2.
		p1.x -= m_bbox.min.x;
		p1.y -= m_bbox.min.y;
		p2.x -= m_bbox.min.x;
		p2.y -= m_bbox.min.y;
		// Get the cells of the end points.
		coord_t ix = div_floor(p1.x, m_resolution);
		coord_t iy = div_floor(p1.y, m_resolution);
		coord_t ixb = div_floor(p2.x, m_resolution);
		coord_t iyb = div_floor(p2.y, m_resolution);
//		assert(ix >= 0 && ix < m_cols);
//		assert(iy >= 0 && iy < m_rows);
//		assert(ixb >= 0 && ixb < m_cols);
//		assert(iyb >= 0 && iyb < m_rows);
		// Account for the end points.
		if (line_cell_intersect(p1src, p2src, m_cells[iy*m_cols + ix]))
			return true;
		if (ix == ixb && iy == iyb)
			// Both ends fall into the same cell.
			continue;
		// Raster the centeral part of the line.
		coord_t dx = std::abs(p2.x - p1.x);
		coord_t dy = std::abs(p2.y - p1.y);
		if (p1.x < p2.x) {
			int64_t ex = int64_t((ix + 1)*m_resolution - p1.x) * int64_t(dy);
			if (p1.y < p2.y) {
				int64_t ey = int64_t((iy + 1)*m_resolution - p1.y) * int64_t(dx);
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
					if (line_cell_intersect(p1src, p2src, m_cells[iy*m_cols + ix]))
						return true;
				} while (ix != ixb || iy != iyb);
			}
			else {
				int64_t ey = int64_t(p1.y - iy*m_resolution) * int64_t(dx);
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
					if (line_cell_intersect(p1src, p2src, m_cells[iy*m_cols + ix]))
						return true;
				} while (ix != ixb || iy != iyb);
			}
		}
		else {
			int64_t ex = int64_t(p1.x - ix*m_resolution) * int64_t(dy);
			if (p1.y < p2.y) {
				int64_t ey = int64_t((iy + 1)*m_resolution - p1.y) * int64_t(dx);
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
					if (line_cell_intersect(p1src, p2src, m_cells[iy*m_cols + ix]))
						return true;
				} while (ix != ixb || iy != iyb);
			}
			else {
				int64_t ey = int64_t(p1.y - iy*m_resolution) * int64_t(dx);
				do {
					assert(ix >= ixb && iy >= iyb);
					if (ex < ey) {
						ey -= ex;
						ex = int64_t(dy) * m_resolution;
						ix -= 1;
					}
					else if (ex == ey) {
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
					if (line_cell_intersect(p1src, p2src, m_cells[iy*m_cols + ix]))
						return true;
				} while (ix != ixb || iy != iyb);
			}
		}
	}
	return false;
}

bool EdgeGrid::Grid::line_cell_intersect(const Point &p1a, const Point &p2a, const Cell &cell)
{
	BoundingBox bbox(p1a, p1a);
	bbox.merge(p2a);
	int64_t va_x = p2a.x - p1a.x;
	int64_t va_y = p2a.y - p1a.y;
	for (size_t i = cell.begin; i != cell.end; ++ i) {
		const std::pair<size_t, size_t> &cell_data = m_cell_data[i];
		// Contour indexed by the ith line of this cell.
		const Slic3r::Points &contour = *m_contours[cell_data.first];
		// Point indices in contour indexed by the ith line of this cell.
		size_t idx1 = cell_data.second;
		size_t idx2 = idx1 + 1;
		if (idx2 == contour.size())
			idx2 = 0;
		// The points of the ith line of this cell and its bounding box.
		const Point &p1b = contour[idx1];
		const Point &p2b = contour[idx2];
		BoundingBox bbox2(p1b, p1b);
		bbox2.merge(p2b);
		// Do the bounding boxes intersect?
		if (! bbox.overlap(bbox2))
			continue;
		// Now intersect the two line segments using exact arithmetics.
		int64_t w1_x = p1b.x - p1a.x;
		int64_t w1_y = p1b.y - p1a.y;
		int64_t w2_x = p2b.x - p1a.x;
		int64_t w2_y = p2b.y - p1a.y;
		int64_t side1 = va_x * w1_y - va_y * w1_x;
		int64_t side2 = va_x * w2_y - va_y * w2_x;
		if (side1 == side2 && side1 != 0)
			// The line segments don't intersect.
			continue;
		w1_x = p1a.x - p1b.x;
		w1_y = p1a.y - p1b.y;
		w2_x = p2a.x - p1b.x;
		w2_y = p2a.y - p1b.y;
		int64_t vb_x = p2b.x - p1b.x;
		int64_t vb_y = p2b.y - p1b.y;
		side1 = vb_x * w1_y - vb_y * w1_x;
		side2 = vb_x * w2_y - vb_y * w2_x;
		if (side1 == side2 && side1 != 0)
			// The line segments don't intersect.
			continue;
		// The line segments intersect.
		return true;
	}
	// The line segment (p1a, p2a) does not intersect any of the line segments inside this cell.
	return false;
}

// Test, whether a point is inside a contour.
bool EdgeGrid::Grid::inside(const Point &pt_src)
{
	Point p = pt_src;
	p.x -= m_bbox.min.x;
	p.y -= m_bbox.min.y;
	// Get the cell of the point.
	if (p.x < 0 || p.y < 0)
		return false;
	coord_t ix = p.x / m_resolution;
	coord_t iy = p.y / m_resolution;
	if (ix >= this->m_cols || iy >= this->m_rows)
		return false;

	size_t i_closest = (size_t)-1;
	bool   inside = false;

	{
		// Hit in the first cell?
		const Cell &cell = m_cells[iy * m_cols + ix];
		for (size_t i = cell.begin; i != cell.end; ++ i) {
			const std::pair<size_t, size_t> &cell_data = m_cell_data[i];
			// Contour indexed by the ith line of this cell.
			const Slic3r::Points &contour = *m_contours[cell_data.first];
			// Point indices in contour indexed by the ith line of this cell.
			size_t idx1 = cell_data.second;
			size_t idx2 = idx1 + 1;
			if (idx2 == contour.size())
				idx2 = 0;
			const Point &p1 = contour[idx1];
			const Point &p2 = contour[idx2];
			if (p1.y < p2.y) {
				if (p.y < p1.y || p.y > p2.y)
					continue;
				//FIXME finish this!
				int64_t vx = 0;// pt_src
				//FIXME finish this!
				int64_t det = 0;
			} else if (p1.y != p2.y) {
				assert(p1.y > p2.y);
				if (p.y < p2.y || p.y > p1.y)
					continue;
			} else {
				assert(p1.y == p2.y);
				if (p1.y == p.y) {
					if (p.x >= p1.x && p.x <= p2.x)
						// On the segment.
						return true;
					// Before or after the segment.
					size_t idx0 = idx1 - 1;
					size_t idx2 = idx1 + 1;
					if (idx0 == (size_t)-1)
						idx0 = contour.size() - 1;
					if (idx2 == contour.size())
						idx2 = 0;
				}
			}
		}
	}

	//FIXME This code follows only a single direction. Better to follow the direction closest to the bounding box.
}
#endif

template<const int INCX, const int INCY>
struct PropagateDanielssonSingleStep {
	PropagateDanielssonSingleStep(float *aL, unsigned char *asigns, size_t astride, coord_t aresolution) :
		L(aL), signs(asigns), stride(astride), resolution(aresolution) {}
	inline void operator()(int r, int c, int addr_delta) {
		size_t  addr = r * stride + c;
		if ((signs[addr] & 2) == 0) {
			float  *v = &L[addr << 1];
			float   l = v[0] * v[0] + v[1] * v[1];
			float  *v2s = v + (addr_delta << 1);
			float	v2[2] = {
				v2s[0] + INCX * resolution,
				v2s[1] + INCY * resolution
			};
			float l2 = v2[0] * v2[0] + v2[1] * v2[1];
			if (l2 < l) {
				v[0] = v2[0];
				v[1] = v2[1];
			}
		}
	}
	float		   *L;
	unsigned char  *signs;
	size_t			stride;
	coord_t			resolution;
};

struct PropagateDanielssonSingleVStep3 {
	PropagateDanielssonSingleVStep3(float *aL, unsigned char *asigns, size_t astride, coord_t aresolution) :
		L(aL), signs(asigns), stride(astride), resolution(aresolution) {}
	inline void operator()(int r, int c, int addr_delta, bool has_l, bool has_r) {
		size_t  addr = r * stride + c;
		if ((signs[addr] & 2) == 0) {
			float  *v    = &L[addr<<1];
			float   l    = v[0]*v[0]+v[1]*v[1];
			float  *v2s   = v+(addr_delta<<1);
			float	v2[2] = {
				v2s[0],
				v2s[1] + resolution
			};
			float   l2   = v2[0]*v2[0]+v2[1]*v2[1];
			if (l2 < l) {
				v[0] = v2[0];
				v[1] = v2[1];
			}
			if (has_l) {
				float  *v2sl = v2s - 1;
				v2[0] = v2sl[0] + resolution;
				v2[1] = v2sl[1] + resolution;
				l2   = v2[0]*v2[0]+v2[1]*v2[1];
				if (l2 < l) {
					v[0] = v2[0];
					v[1] = v2[1];
				}
			}
			if (has_r) {
				float  *v2sr = v2s + 1;
				v2[0] = v2sr[0] + resolution;
				v2[1] = v2sr[1] + resolution;
				l2   = v2[0]*v2[0]+v2[1]*v2[1];
				if (l2 < l) {
					v[0] = v2[0];
					v[1] = v2[1];
				}
			}
		}
	}
	float		   *L;
	unsigned char  *signs;
	size_t			stride;
	coord_t			resolution;
};

void EdgeGrid::Grid::calculate_sdf()
{
	// 1) Initialize a signum and an unsigned vector to a zero iso surface.
	size_t nrows = m_rows + 1;
	size_t ncols = m_cols + 1;
	// Unsigned vectors towards the closest point on the surface.
	std::vector<float> L(nrows * ncols * 2, FLT_MAX);
	// Bit 0 set - negative.
	// Bit 1 set - original value, the distance value shall not be changed by the Danielsson propagation.
	// Bit 2 set - signum not propagated yet.
	std::vector<unsigned char> signs(nrows * ncols, 4);
	// SDF will be initially filled with unsigned DF.
//	m_signed_distance_field.assign(nrows * ncols, FLT_MAX);
	float search_radius = float(m_resolution<<1);
	m_signed_distance_field.assign(nrows * ncols, search_radius);
	// For each cell:
	for (size_t r = 0; r < m_rows; ++ r) {
		for (size_t c = 0; c < m_cols; ++ c) {
			const Cell &cell = m_cells[r * m_cols + c];
			// For each segment in the cell:
			for (size_t i = cell.begin; i != cell.end; ++ i) {
				const Slic3r::Points &pts = *m_contours[m_cell_data[i].first];
				size_t ipt = m_cell_data[i].second;
				// End points of the line segment.
				const Slic3r::Point &p1 = pts[ipt];
				const Slic3r::Point &p2 = pts[(ipt + 1 == pts.size()) ? 0 : ipt + 1];
				// Segment vector
				const Slic3r::Point v_seg = p1.vector_to(p2);
				// l2 of v_seg
				const int64_t l2_seg = int64_t(v_seg.x) * int64_t(v_seg.x) + int64_t(v_seg.y) * int64_t(v_seg.y);
				// For each corner of this cell and its 1 ring neighbours:
				for (int corner_y = -1; corner_y < 3; ++ corner_y) {
					coord_t corner_r = r + corner_y;
					if (corner_r < 0 || corner_r >= nrows)
						continue;
					for (int corner_x = -1; corner_x < 3; ++ corner_x) {
						coord_t corner_c = c + corner_x;
						if (corner_c < 0 || corner_c >= ncols)
							continue;
						float  &d_min = m_signed_distance_field[corner_r * ncols + corner_c];
						Slic3r::Point pt(m_bbox.min.x + corner_c * m_resolution, m_bbox.min.y + corner_r * m_resolution);
						Slic3r::Point v_pt = p1.vector_to(pt);
						// dot(p2-p1, pt-p1)
						int64_t t_pt = int64_t(v_seg.x) * int64_t(v_pt.x) + int64_t(v_seg.y) * int64_t(v_pt.y);
						if (t_pt < 0) {
							// Closest to p1.
							double dabs = sqrt(int64_t(v_pt.x) * int64_t(v_pt.x) + int64_t(v_pt.y) * int64_t(v_pt.y));
							if (dabs < d_min) {
								// Previous point.
								const Slic3r::Point &p0 = pts[(ipt == 0) ? (pts.size() - 1) : ipt - 1];
								Slic3r::Point v_seg_prev = p0.vector_to(p1);
								int64_t t2_pt = int64_t(v_seg_prev.x) * int64_t(v_pt.x) + int64_t(v_seg_prev.y) * int64_t(v_pt.y);
								if (t2_pt > 0) {
									// Inside the wedge between the previous and the next segment.
									// Set the signum depending on whether the vertex is convex or reflex.
									int64_t det = int64_t(v_seg_prev.x) * int64_t(v_seg.y) - int64_t(v_seg_prev.y) * int64_t(v_seg.x);
									assert(det != 0);
									d_min = dabs;
									// Fill in an unsigned vector towards the zero iso surface.
									float *l = &L[(corner_r * ncols + corner_c) << 1];
									l[0] = std::abs(v_pt.x);
									l[1] = std::abs(v_pt.y);
								#ifdef _DEBUG
									double dabs2 = sqrt(l[0]*l[0]+l[1]*l[1]);
									assert(std::abs(dabs-dabs2) < 1e-4 * std::max(dabs, dabs2));
								#endif /* _DEBUG */
									signs[corner_r * ncols + corner_c] = ((det < 0) ? 1 : 0) | 2;
								}
							}
						}
						else if (t_pt > l2_seg) {
							// Closest to p2. Then p2 is the starting point of another segment, which shall be discovered in the same cell.
							continue;
						} else {
							// Closest to the segment.
							assert(t_pt >= 0 && t_pt <= l2_seg);
							int64_t d_seg = int64_t(v_seg.y) * int64_t(v_pt.x) - int64_t(v_seg.x) * int64_t(v_pt.y);
							double d = double(d_seg) / sqrt(double(l2_seg));
							double dabs = std::abs(d);
							if (dabs < d_min) {
								d_min = dabs;
								// Fill in an unsigned vector towards the zero iso surface.
								float *l = &L[(corner_r * ncols + corner_c) << 1];
								float linv = float(d_seg) / float(l2_seg);
								l[0] = std::abs(float(v_seg.y) * linv);
								l[1] = std::abs(float(v_seg.x) * linv);
								#ifdef _DEBUG
									double dabs2 = sqrt(l[0]*l[0]+l[1]*l[1]);
									assert(std::abs(dabs-dabs2) <= 1e-4 * std::max(dabs, dabs2));
								#endif /* _DEBUG */
								signs[corner_r * ncols + corner_c] = ((d_seg < 0) ? 1 : 0) | 2;
							}
						}
					}
				}
			}
		}
	}

#if 0
	static int iRun = 0;
	++ iRun;
//#ifdef SLIC3R_GUI
	{ 
		wxImage img(ncols, nrows);
		unsigned char *data = img.GetData();
		memset(data, 0, ncols * nrows * 3);
		for (coord_t r = 0; r < nrows; ++r) {
			for (coord_t c = 0; c < ncols; ++c) {
				unsigned char *pxl = data + (((nrows - r - 1) * ncols) + c) * 3;
				float d = m_signed_distance_field[r * ncols + c];
				if (d != search_radius) {
					float s = 255 * d / search_radius;
					int is = std::max(0, std::min(255, int(floor(s + 0.5f))));
					pxl[0] = 255;
					pxl[1] = 255 - is;
					pxl[2] = 255 - is;
				}
				else {
					pxl[0] = 0;
					pxl[1] = 255;
					pxl[2] = 0;
				}
			}
		}
		img.SaveFile(debug_out_path("unsigned_df-%d.png", iRun), wxBITMAP_TYPE_PNG);
	}
	{
		wxImage img(ncols, nrows);
		unsigned char *data = img.GetData();
		memset(data, 0, ncols * nrows * 3);
		for (coord_t r = 0; r < nrows; ++r) {
			for (coord_t c = 0; c < ncols; ++c) {
				unsigned char *pxl = data + (((nrows - r - 1) * ncols) + c) * 3;
				float d = m_signed_distance_field[r * ncols + c];
				if (d != search_radius) {
					float s = 255 * d / search_radius;
					int is = std::max(0, std::min(255, int(floor(s + 0.5f))));
					if ((signs[r * ncols + c] & 1) == 0) {
						// Positive
						pxl[0] = 255;
						pxl[1] = 255 - is;
						pxl[2] = 255 - is;
					}
					else {
						// Negative
						pxl[0] = 255 - is;
						pxl[1] = 255 - is;
						pxl[2] = 255;
					}
				}
				else {
					pxl[0] = 0;
					pxl[1] = 255;
					pxl[2] = 0;
				}
			}
		}
		img.SaveFile(debug_out_path("signed_df-%d.png", iRun), wxBITMAP_TYPE_PNG);
	}
#endif /* SLIC3R_GUI */

	// 2) Propagate the signum.
	#define PROPAGATE_SIGNUM_SINGLE_STEP(DELTA) do { \
		size_t 		   addr    = r * ncols + c;				\
		unsigned char &cur_val = signs[addr]; 				\
		if (cur_val & 4) {	 								\
			unsigned char old_val = signs[addr + (DELTA)];  \
			if ((old_val & 4) == 0)							\
				cur_val = old_val & 1; 						\
		} 													\
	} while (0);
	// Top to bottom propagation.
	for (size_t r = 0; r < nrows; ++ r) {
		if (r > 0)
			for (size_t c = 0; c < ncols; ++ c)
				PROPAGATE_SIGNUM_SINGLE_STEP(- int(ncols));
		for (size_t c = 1; c < ncols; ++ c)
			PROPAGATE_SIGNUM_SINGLE_STEP(- 1);
		for (int c = int(ncols) - 2; c >= 0; -- c)
			PROPAGATE_SIGNUM_SINGLE_STEP(+ 1);
	}
	// Bottom to top propagation.
	for (int r = int(nrows) - 2; r >= 0; -- r) {
		for (size_t c = 0; c < ncols; ++ c)
			PROPAGATE_SIGNUM_SINGLE_STEP(+ ncols);
		for (size_t c = 1; c < ncols; ++ c)
			PROPAGATE_SIGNUM_SINGLE_STEP(- 1);
		for (int c = int(ncols) - 2; c >= 0; -- c)
			PROPAGATE_SIGNUM_SINGLE_STEP(+ 1);
	}
	#undef PROPAGATE_SIGNUM_SINGLE_STEP

	// 3) Propagate the distance by the Danielsson chamfer metric.
	// Top to bottom propagation.
	PropagateDanielssonSingleStep<1, 0> danielsson_hstep(L.data(), signs.data(), ncols, m_resolution);
	PropagateDanielssonSingleStep<0, 1> danielsson_vstep(L.data(), signs.data(), ncols, m_resolution);
	PropagateDanielssonSingleVStep3 	danielsson_vstep3(L.data(), signs.data(), ncols, m_resolution);
	// Top to bottom propagation.
	for (size_t r = 0; r < nrows; ++ r) {
		if (r > 0)
			for (size_t c = 0; c < ncols; ++ c)
				danielsson_vstep(r, c, -int(ncols));
//				PROPAGATE_DANIELSSON_SINGLE_VSTEP3(-int(ncols), c != 0, c + 1 != ncols);
		for (size_t c = 1; c < ncols; ++ c)
			danielsson_hstep(r, c, -1);
		for (int c = int(ncols) - 2; c >= 0; -- c)
			danielsson_hstep(r, c, +1);
	}
	// Bottom to top propagation.
	for (int r = int(nrows) - 2; r >= 0; -- r) {
		for (size_t c = 0; c < ncols; ++ c)
			danielsson_vstep(r, c, +ncols);
//			PROPAGATE_DANIELSSON_SINGLE_VSTEP3(+int(ncols), c != 0, c + 1 != ncols);
		for (size_t c = 1; c < ncols; ++ c)
			danielsson_hstep(r, c, -1);
		for (int c = int(ncols) - 2; c >= 0; -- c)
			danielsson_hstep(r, c, +1);
	}

	// Update signed distance field from absolte vectors to the iso-surface.
	for (size_t r = 0; r < nrows; ++ r) {
		for (size_t c = 0; c < ncols; ++ c) {
			size_t  addr = r * ncols + c;
			float  *v    = &L[addr<<1];
			float   d    = sqrt(v[0]*v[0]+v[1]*v[1]);
			if (signs[addr] & 1)
				d = -d;
			m_signed_distance_field[addr] = d;
		}
	}

#if 0
//#ifdef SLIC3R_GUI
	{
		wxImage img(ncols, nrows);
		unsigned char *data = img.GetData();
		memset(data, 0, ncols * nrows * 3);
		float search_radius = float(m_resolution * 5);
		for (coord_t r = 0; r < nrows; ++r) {
			for (coord_t c = 0; c < ncols; ++c) {
				unsigned char *pxl = data + (((nrows - r - 1) * ncols) + c) * 3;
				unsigned char sign = signs[r * ncols + c];
				switch (sign) {
				case 0:
					// Positive, outside of a narrow band.
					pxl[0] = 0;
					pxl[1] = 0;
					pxl[2] = 255;
					break;
				case 1:
					// Negative, outside of a narrow band.
					pxl[0] = 255;
					pxl[1] = 0;
					pxl[2] = 0;
					break;
				case 2:
					// Positive, outside of a narrow band.
					pxl[0] = 100;
					pxl[1] = 100;
					pxl[2] = 255;
					break;
				case 3:
					// Negative, outside of a narrow band.
					pxl[0] = 255;
					pxl[1] = 100; 
					pxl[2] = 100;
					break;
				case 4:
					// This shall not happen. Undefined signum.
					pxl[0] = 0;
					pxl[1] = 255;
					pxl[2] = 0;
					break;
				default:
					// This shall not happen. Invalid signum value.
					pxl[0] = 255;
					pxl[1] = 255;
					pxl[2] = 255;
					break;
				}
			}
		}
		img.SaveFile(debug_out_path("signed_df-signs-%d.png", iRun), wxBITMAP_TYPE_PNG);
	}
#endif /* SLIC3R_GUI */

#if 0
//#ifdef SLIC3R_GUI
	{
		wxImage img(ncols, nrows);
		unsigned char *data = img.GetData();
		memset(data, 0, ncols * nrows * 3);
		float search_radius = float(m_resolution * 5);
		for (coord_t r = 0; r < nrows; ++r) {
			for (coord_t c = 0; c < ncols; ++c) {
				unsigned char *pxl = data + (((nrows - r - 1) * ncols) + c) * 3;
				float d = m_signed_distance_field[r * ncols + c];
				float s = 255.f * fabs(d) / search_radius;
				int is = std::max(0, std::min(255, int(floor(s + 0.5f))));
				if (d < 0.f) {
					pxl[0] = 255;
					pxl[1] = 255 - is;
					pxl[2] = 255 - is;
				}
				else {
					pxl[0] = 255 - is;
					pxl[1] = 255 - is;
					pxl[2] = 255;
				}
			}
		}
		img.SaveFile(debug_out_path("signed_df2-%d.png", iRun), wxBITMAP_TYPE_PNG);
	}
#endif /* SLIC3R_GUI */
}

float EdgeGrid::Grid::signed_distance_bilinear(const Point &pt) const
{
	coord_t x = pt.x - m_bbox.min.x;
	coord_t y = pt.y - m_bbox.min.y;
	coord_t w = m_resolution * m_cols;
	coord_t h = m_resolution * m_rows;
	bool    clamped = false;
	coord_t xcl = x;
	coord_t ycl = y;
	if (x < 0) {
		xcl = 0;
		clamped = true;
	} else if (x >= w) {
		xcl = w - 1;
		clamped = true;
	}
	if (y < 0) {
		ycl = 0;
		clamped = true;
	} else if (y >= h) {
		ycl = h - 1;
		clamped = true;
	}
 
	coord_t cell_c = coord_t(floor(xcl / m_resolution));
	coord_t cell_r = coord_t(floor(ycl / m_resolution));
	float   tx = float(xcl - cell_c * m_resolution) / float(m_resolution);
	assert(tx >= -1e-5 && tx < 1.f + 1e-5);
	float   ty = float(ycl - cell_r * m_resolution) / float(m_resolution);
	assert(ty >= -1e-5 && ty < 1.f + 1e-5);
	size_t  addr = cell_r * (m_cols + 1) + cell_c;
	float   f00 = m_signed_distance_field[addr];
	float   f01 = m_signed_distance_field[addr+1];
	addr += m_cols + 1;
	float   f10 = m_signed_distance_field[addr];
	float   f11 = m_signed_distance_field[addr+1];
	float   f0  = (1.f - tx) * f00 + tx * f01;
	float   f1  = (1.f - tx) * f10 + tx * f11;
	float	f   = (1.f - ty) * f0 + ty * f1;

	if (clamped) {
		if (f > 0) {
			if (x < 0)
				f += -x;
			else if (x >= w)
				f += x - w + 1;
			if (y < 0)
				f += -y;
			else if (y >= h)
				f += y - h + 1;
		} else {			
			if (x < 0)
				f -= -x;
			else if (x >= w)
				f -= x - w + 1;
			if (y < 0)
				f -= -y;
			else if (y >= h)
				f -= y - h + 1;
		}
	}

	return f;
}
 
bool EdgeGrid::Grid::signed_distance_edges(const Point &pt, coord_t search_radius, coordf_t &result_min_dist, bool *pon_segment) const {
	BoundingBox bbox;
	bbox.min = bbox.max = Point(pt.x - m_bbox.min.x, pt.y - m_bbox.min.y);
	bbox.defined = true;
	// Upper boundary, round to grid and test validity.
	bbox.max.x += search_radius;
	bbox.max.y += search_radius;
	if (bbox.max.x < 0 || bbox.max.y < 0)
		return false;
	bbox.max.x /= m_resolution;
	bbox.max.y /= m_resolution;
	if (bbox.max.x >= m_cols)
		bbox.max.x = m_cols - 1;
	if (bbox.max.y >= m_rows)
		bbox.max.y = m_rows - 1;
	// Lower boundary, round to grid and test validity.
	bbox.min.x -= search_radius;
	bbox.min.y -= search_radius;
	if (bbox.min.x < 0)
		bbox.min.x = 0;
	if (bbox.min.y < 0)
		bbox.min.y = 0;
	bbox.min.x /= m_resolution;
	bbox.min.y /= m_resolution;
	// Is the interval empty?
	if (bbox.min.x > bbox.max.x ||
		bbox.min.y > bbox.max.y)
		return false;
	// Traverse all cells in the bounding box.
	float d_min = search_radius;
	// Signum of the distance field at pt.
	int sign_min = 0;
	bool on_segment = false;
	for (int r = bbox.min.y; r <= bbox.max.y; ++ r) {
		for (int c = bbox.min.x; c <= bbox.max.x; ++ c) {
			const Cell &cell = m_cells[r * m_cols + c];
			for (size_t i = cell.begin; i < cell.end; ++ i) {
				const Slic3r::Points &pts = *m_contours[m_cell_data[i].first];
				size_t ipt = m_cell_data[i].second;
				// End points of the line segment.
				const Slic3r::Point &p1 = pts[ipt];
				const Slic3r::Point &p2 = pts[(ipt + 1 == pts.size()) ? 0 : ipt + 1];
				Slic3r::Point v_seg = p1.vector_to(p2);
				Slic3r::Point v_pt = p1.vector_to(pt);
				// dot(p2-p1, pt-p1)
				int64_t t_pt = int64_t(v_seg.x) * int64_t(v_pt.x) + int64_t(v_seg.y) * int64_t(v_pt.y);
				// l2 of seg
				int64_t l2_seg = int64_t(v_seg.x) * int64_t(v_seg.x) + int64_t(v_seg.y) * int64_t(v_seg.y);
				if (t_pt < 0) {
					// Closest to p1.
					double dabs = sqrt(int64_t(v_pt.x) * int64_t(v_pt.x) + int64_t(v_pt.y) * int64_t(v_pt.y));
					if (dabs < d_min) {
						// Previous point.
						const Slic3r::Point &p0 = pts[(ipt == 0) ? (pts.size() - 1) : ipt - 1];
						Slic3r::Point v_seg_prev = p0.vector_to(p1);
						int64_t t2_pt = int64_t(v_seg_prev.x) * int64_t(v_pt.x) + int64_t(v_seg_prev.y) * int64_t(v_pt.y);
						if (t2_pt > 0) {
							// Inside the wedge between the previous and the next segment.
							d_min = dabs;
							// Set the signum depending on whether the vertex is convex or reflex.
							int64_t det = int64_t(v_seg_prev.x) * int64_t(v_seg.y) - int64_t(v_seg_prev.y) * int64_t(v_seg.x);
							assert(det != 0);
							sign_min = (det > 0) ? 1 : -1;
							on_segment = false;
						}
					}
				}
				else if (t_pt > l2_seg) {
					// Closest to p2. Then p2 is the starting point of another segment, which shall be discovered in the same cell.
					continue;
				} else {
					// Closest to the segment.
					assert(t_pt >= 0 && t_pt <= l2_seg);
					int64_t d_seg = int64_t(v_seg.y) * int64_t(v_pt.x) - int64_t(v_seg.x) * int64_t(v_pt.y);
					double d = double(d_seg) / sqrt(double(l2_seg));
					double dabs = std::abs(d);
					if (dabs < d_min) {
						d_min = dabs;
						sign_min = (d_seg < 0) ? -1 : ((d_seg == 0) ? 0 : 1);
						on_segment = true;
					}
				}
			}
		}
	}
	if (d_min >= search_radius)
		return false;
	result_min_dist = d_min * sign_min;
	if (pon_segment != NULL)
		*pon_segment = on_segment;
	return true;
}

bool EdgeGrid::Grid::signed_distance(const Point &pt, coord_t search_radius, coordf_t &result_min_dist) const
{
	if (signed_distance_edges(pt, search_radius, result_min_dist))
		return true;
	if (m_signed_distance_field.empty())
		return false;
	result_min_dist = signed_distance_bilinear(pt);
	return true;
}

Polygons EdgeGrid::Grid::contours_simplified(coord_t offset, bool fill_holes) const
{
	assert(std::abs(2 * offset) < m_resolution);

	typedef std::unordered_multimap<Point, int, PointHash> EndPointMapType;
	// 0) Prepare a binary grid.
	size_t cell_rows = m_rows + 2;
	size_t cell_cols = m_cols + 2;
	std::vector<char> cell_inside(cell_rows * cell_cols, false);
	for (int r = 0; r < int(cell_rows); ++ r)
		for (int c = 0; c < int(cell_cols); ++ c)
			cell_inside[r * cell_cols + c] = cell_inside_or_crossing(r - 1, c - 1);
	// Fill in empty cells, which have a left / right neighbor filled.
	// Fill in empty cells, which have the top / bottom neighbor filled.
	if (fill_holes) {
		std::vector<char> cell_inside2(cell_inside);
		for (int r = 1; r + 1 < int(cell_rows); ++ r) {
			for (int c = 1; c + 1 < int(cell_cols); ++ c) {
				int addr = r * cell_cols + c;
				if ((cell_inside2[addr - 1] && cell_inside2[addr + 1]) ||
					(cell_inside2[addr - cell_cols] && cell_inside2[addr + cell_cols]))
					cell_inside[addr] = true;
			}
		}
	}

	// 1) Collect the lines.
	std::vector<Line> lines;
	EndPointMapType start_point_to_line_idx;
	for (int r = 0; r <= int(m_rows); ++ r) {
		for (int c = 0; c <= int(m_cols); ++ c) {
			int  addr    = (r + 1) * cell_cols + c + 1;
			bool left    = cell_inside[addr - 1];
			bool top     = cell_inside[addr - cell_cols];
			bool current = cell_inside[addr];
			if (left != current) {
				lines.push_back(
					left ? 
						Line(Point(c, r+1), Point(c, r  )) : 
						Line(Point(c, r  ), Point(c, r+1)));
				start_point_to_line_idx.insert(std::pair<Point, int>(lines.back().a, int(lines.size()) - 1));
			}
			if (top != current) {
				lines.push_back(
					top ? 
						Line(Point(c  , r), Point(c+1, r)) :
						Line(Point(c+1, r), Point(c  , r))); 
				start_point_to_line_idx.insert(std::pair<Point, int>(lines.back().a, int(lines.size()) - 1));
			}
		}
	}

	// 2) Chain the lines.
	std::vector<char> line_processed(lines.size(), false);
	Polygons out;
	for (int i_candidate = 0; i_candidate < int(lines.size()); ++ i_candidate) {
		if (line_processed[i_candidate])
			continue;
		Polygon poly;
		line_processed[i_candidate] = true;
		poly.points.push_back(lines[i_candidate].b);
		int i_line_current = i_candidate;
		for (;;) {
			std::pair<EndPointMapType::iterator,EndPointMapType::iterator> line_range = 
				start_point_to_line_idx.equal_range(lines[i_line_current].b);
			// The interval has to be non empty, there shall be at least one line continuing the current one.
			assert(line_range.first != line_range.second);
			int i_next = -1;
			for (EndPointMapType::iterator it = line_range.first; it != line_range.second; ++ it) {
				if (it->second == i_candidate) {
					// closing the loop.
					goto end_of_poly;
				}
				if (line_processed[it->second])
					continue;
				if (i_next == -1) {
					i_next = it->second;
				} else {
					// This is a corner, where two lines meet exactly. Pick the line, which encloses a smallest angle with
					// the current edge.
					const Line &line_current = lines[i_line_current];
					const Line &line_next = lines[it->second];
					const Vector v1 = line_current.vector();
					const Vector v2 = line_next.vector();
					int64_t cross = int64_t(v1.x) * int64_t(v2.y) - int64_t(v2.x) * int64_t(v1.y);
					if (cross > 0) {
						// This has to be a convex right angle. There is no better next line.
						i_next = it->second;
						break;
					}
				}
			}
			line_processed[i_next] = true;
			i_line_current = i_next;
			poly.points.push_back(lines[i_line_current].b);
		}
	end_of_poly:
		out.push_back(std::move(poly));
	}

	// 3) Scale the polygons back into world, shrink slightly and remove collinear points.
	for (size_t i = 0; i < out.size(); ++ i) {
		Polygon &poly = out[i];
		for (size_t j = 0; j < poly.points.size(); ++ j) {
			Point &p = poly.points[j];
			p.x *= m_resolution;
			p.y *= m_resolution;
			p.x += m_bbox.min.x;
			p.y += m_bbox.min.y;
		}
		// Shrink the contour slightly, so if the same contour gets discretized and simplified again, one will get the same result.
		// Remove collineaer points.
		Points pts;
		pts.reserve(poly.points.size());
		for (size_t j = 0; j < poly.points.size(); ++ j) {
			size_t j0 = (j == 0) ? poly.points.size() - 1 : j - 1;
			size_t j2 = (j + 1 == poly.points.size()) ? 0 : j + 1;
			Point  v  = poly.points[j2] - poly.points[j0];
			if (v.x != 0 && v.y != 0) {
				// This is a corner point. Copy it to the output contour.
				Point p = poly.points[j];
				p.y += (v.x < 0) ? - offset : offset;
				p.x += (v.y > 0) ? - offset : offset;
				pts.push_back(p);
			} 
		}
		poly.points = std::move(pts);
	}
	return out;
}

#if 0
void EdgeGrid::save_png(const EdgeGrid::Grid &grid, const BoundingBox &bbox, coord_t resolution, const char *path)
{
	unsigned int w = (bbox.max.x - bbox.min.x + resolution - 1) / resolution;
	unsigned int h = (bbox.max.y - bbox.min.y + resolution - 1) / resolution;
	wxImage img(w, h);
    unsigned char *data = img.GetData();
    memset(data, 0, w * h * 3);

	static int iRun = 0;
	++iRun;
    
    const coord_t search_radius = grid.resolution() * 2;
	const coord_t display_blend_radius = grid.resolution() * 2;
	for (coord_t r = 0; r < h; ++r) {
    	for (coord_t c = 0; c < w; ++ c) {
			unsigned char *pxl = data + (((h - r - 1) * w) + c) * 3;
			Point pt(c * resolution + bbox.min.x, r * resolution + bbox.min.y);
			coordf_t min_dist;
			bool on_segment = true;
			#if 0
			if (grid.signed_distance_edges(pt, search_radius, min_dist, &on_segment)) {
			#else
			if (grid.signed_distance(pt, search_radius, min_dist)) {
			#endif
				float s = 255 * std::abs(min_dist) / float(display_blend_radius);
				int is = std::max(0, std::min(255, int(floor(s + 0.5f))));
				if (min_dist < 0) {
					if (on_segment) {
						pxl[0] = 255;
						pxl[1] = 255 - is;
						pxl[2] = 255 - is;
					} else {
						pxl[0] = 255;
						pxl[1] = 0;
						pxl[2] = 255 - is;
					}
				}
				else {
					if (on_segment) {
						pxl[0] = 255 - is;
						pxl[1] = 255 - is;
						pxl[2] = 255;
					} else {
						pxl[0] = 255 - is;
						pxl[1] = 0;
						pxl[2] = 255;
					}
				}
			} else {
				pxl[0] = 0;
				pxl[1] = 255;
				pxl[2] = 0;
			}

			float gridx = float(pt.x - grid.bbox().min.x) / float(grid.resolution());
			float gridy = float(pt.y - grid.bbox().min.y) / float(grid.resolution());
			if (gridx >= -0.4f && gridy >= -0.4f && gridx <= grid.cols() + 0.4f && gridy <= grid.rows() + 0.4f) {
				int ix = int(floor(gridx + 0.5f));
				int iy = int(floor(gridy + 0.5f));
				float dx = gridx - float(ix);
				float dy = gridy - float(iy);
				float d = sqrt(dx*dx + dy*dy) * float(grid.resolution()) / float(resolution);
				if (d < 1.f) {
					// Less than 1 pixel from the grid point.
					float t = 0.5f + 0.5f * d;
					pxl[0] = (unsigned char)(t * pxl[0]);
					pxl[1] = (unsigned char)(t * pxl[1]);
					pxl[2] = (unsigned char)(t * pxl[2]);
				}
			}

			float dgrid = fabs(min_dist) / float(grid.resolution());
			float igrid = floor(dgrid + 0.5f);
			dgrid = std::abs(dgrid - igrid) * float(grid.resolution()) / float(resolution);
			if (dgrid < 1.f) {
				// Less than 1 pixel from the grid point.
				float t = 0.5f + 0.5f * dgrid;
				pxl[0] = (unsigned char)(t * pxl[0]);
				pxl[1] = (unsigned char)(t * pxl[1]);
				pxl[2] = (unsigned char)(t * pxl[2]);
				if (igrid > 0.f) {
					// Other than zero iso contour.
					int g = pxl[1] + 255.f * (1.f - t);
					pxl[1] = std::min(g, 255);
				}
			}
		}
    }

    img.SaveFile(path, wxBITMAP_TYPE_PNG);
}
#endif /* SLIC3R_GUI */

} // namespace Slic3r
