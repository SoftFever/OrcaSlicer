//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "DistanceField.hpp" //Class we're implementing.
#include "../FillRectilinear.hpp"
#include "../../ClipperUtils.hpp"

namespace Slic3r::FillLightning
{

constexpr coord_t radius_per_cell_size = 6;  // The cell-size should be small compared to the radius, but not so small as to be inefficient.

DistanceField::DistanceField(const coord_t& radius, const Polygons& current_outline, const Polygons& current_overhang) : 
    m_cell_size(radius / radius_per_cell_size), 
    m_supporting_radius(radius)
{
    m_supporting_radius2 = double(radius) * double(radius);
    // Sample source polygons with a regular grid sampling pattern.
    for (const ExPolygon &expoly : union_ex(current_outline)) {
        for (const Point &p : sample_grid_pattern(expoly, m_cell_size)) {
            // Find a squared distance to the source expolygon boundary.
            double d2 = std::numeric_limits<double>::max();
            for (size_t icontour = 0; icontour <= expoly.holes.size(); ++ icontour) {
                const Polygon &contour = icontour == 0 ? expoly.contour : expoly.holes[icontour - 1];
                if (contour.size() > 2) {
                    Point prev = contour.points.back();
                    for (const Point &p2 : contour.points) {
                        d2 = std::min(d2, Line::distance_to_squared(p, prev, p2));
                        prev = p2;
                    }
                }
            }
            m_unsupported_points.emplace_back(p, sqrt(d2));
        }
    }
    m_unsupported_points.sort([&radius](const UnsupportedCell &a, const UnsupportedCell &b) {
        constexpr coord_t prime_for_hash = 191;
        return std::abs(b.dist_to_boundary - a.dist_to_boundary) > radius ?
               a.dist_to_boundary < b.dist_to_boundary :
               (PointHash{}(a.loc) % prime_for_hash) < (PointHash{}(b.loc) % prime_for_hash);
        });
    for (auto it = m_unsupported_points.begin(); it != m_unsupported_points.end(); ++it) {
        UnsupportedCell& cell = *it;
        m_unsupported_points_grid.emplace(Point{ cell.loc.x() / m_cell_size, cell.loc.y() / m_cell_size }, it);
    }
}

void DistanceField::update(const Point& to_node, const Point& added_leaf)
{
    Vec2d       v  = (added_leaf - to_node).cast<double>();
    auto        l2 = v.squaredNorm();
    Vec2d       extent = Vec2d(-v.y(), v.x()) * m_supporting_radius / sqrt(l2);

    BoundingBox grid;
    {
        Point diagonal(m_supporting_radius, m_supporting_radius);
        Point iextent(extent.cast<coord_t>());
        grid = BoundingBox(added_leaf - diagonal, added_leaf + diagonal);
        grid.merge(to_node - iextent);
        grid.merge(to_node + iextent);
        grid.merge(added_leaf - iextent);
        grid.merge(added_leaf + iextent);
        grid.min /= m_cell_size;
        grid.max /= m_cell_size;
    }

    Point grid_loc;
    for (coord_t row = grid.min.y(); row <= grid.max.y(); ++ row) {
        grid_loc.y() = row * m_cell_size;
        for (coord_t col = grid.min.x(); col <= grid.max.y(); ++ col) {
            grid_loc.x() = col * m_cell_size;
            // Test inside a circle at the new leaf.
            if ((grid_loc - added_leaf).cast<double>().squaredNorm() > m_supporting_radius2) {
                // Not inside a circle at the end of the new leaf.
                // Test inside a rotated rectangle.
                Vec2d  vx = (grid_loc - to_node).cast<double>();
                double d  = v.dot(vx);
                if (d >= 0 && d <= l2) {
                    d = extent.dot(vx);
                    if (d < -1. || d > 1.)
                        // Not inside a rotated rectangle.
                        continue;
                }
            }
            // Inside a circle at the end of the new leaf, or inside a rotated rectangle.
            // Remove unsupported leafs at this grid location.
            if (auto it = m_unsupported_points_grid.find(grid_loc); it != m_unsupported_points_grid.end()) {
                std::list<UnsupportedCell>::iterator& list_it = it->second;
                UnsupportedCell& cell = *list_it;
                if ((cell.loc - added_leaf).cast<double>().squaredNorm() <= m_supporting_radius2) {
                    m_unsupported_points.erase(list_it);
                    m_unsupported_points_grid.erase(it);
                }
            }
        }
    }
}

} // namespace Slic3r::FillLightning
