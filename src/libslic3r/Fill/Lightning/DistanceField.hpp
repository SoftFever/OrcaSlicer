//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef LIGHTNING_DISTANCE_FIELD_H
#define LIGHTNING_DISTANCE_FIELD_H

#include "../../Point.hpp"
#include "../../Polygon.hpp"

namespace Slic3r::FillLightning
{

/*!
 * 2D field that maintains locations which need to be supported for Lightning
 * Infill.
 *
 * This field contains a set of "cells", spaced out in a grid. Each cell
 * maintains how far it is removed from the edge, which is used to determine
 * how it gets supported by Lightning Infill.
 */
class DistanceField
{
public:
    /*!
     * Construct a new field to calculate Lightning Infill with.
     * \param radius The radius of influence that an infill line is expected to
     * support in the layer above.
     * \param current_outline The total infill area on this layer.
     * \param current_overhang The overhang that needs to be supported on this
     * layer.
     */
    DistanceField(const coord_t& radius, const Polygons& current_outline, const Polygons& current_overhang);
    
    /*!
     * Gets the next unsupported location to be supported by a new branch.
     * \param p Output variable for the next point to support.
     * \return ``true`` if successful, or ``false`` if there are no more points
     * to consider.
     */
    bool tryGetNextPoint(Point* p) const {
        if (m_unsupported_points.empty())
            return false;
        *p = m_unsupported_points.front().loc;
        return true;
    }

    /*!
     * Update the distance field with a newly added branch.
     *
     * The branch is a line extending from \p to_node to \p added_leaf . This
     * function updates the grid cells so that the distance field knows how far
     * off it is from being supported by the current pattern. Grid points are
     * updated with sampling points spaced out by the supporting radius along
     * the line.
     * \param to_node The node endpoint of the newly added branch.
     * \param added_leaf The location of the leaf of the newly added branch,
     * drawing a straight line to the node.
     */
    void update(const Point& to_node, const Point& added_leaf);

protected:
    /*!
     * Spacing between grid points to consider supporting.
     */
    coord_t m_cell_size;

    /*!
     * The radius of the area of the layer above supported by a point on a
     * branch of a tree.
     */
    coord_t m_supporting_radius;
    double  m_supporting_radius2;

    /*!
     * Represents a small discrete area of infill that needs to be supported.
     */
    struct UnsupportedCell
    {
        UnsupportedCell(Point loc, coord_t dist_to_boundary) : loc(loc), dist_to_boundary(dist_to_boundary) {}
        // The position of the center of this cell.
        Point loc;
        // How far this cell is removed from the ``current_outline`` polygon, the edge of the infill area.
        coord_t dist_to_boundary;
    };

    /*!
     * Cells which still need to be supported at some point.
     */
    std::list<UnsupportedCell> m_unsupported_points;

    /*!
     * Links the unsupported points to a grid point, so that we can quickly look
     * up the cell belonging to a certain position in the grid.
     */
    std::unordered_map<Point, std::list<UnsupportedCell>::iterator, PointHash> m_unsupported_points_grid;
};

} // namespace Slic3r::FillLightning

#endif //LIGHTNING_DISTANCE_FIELD_H
