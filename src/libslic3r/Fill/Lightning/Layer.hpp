//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef LIGHTNING_LAYER_H
#define LIGHTNING_LAYER_H

#include "../../EdgeGrid.hpp"
#include "../../Polygon.hpp"

#include <memory>
#include <vector>
#include <list>
#include <unordered_map>

namespace Slic3r
{
class LightningTreeNode;

using LightningTreeNodeSPtr = std::shared_ptr<LightningTreeNode>;
using SparseLightningTreeNodeGrid = std::unordered_multimap<Point, std::weak_ptr<LightningTreeNode>, PointHash>;

struct GroundingLocation
{
    LightningTreeNodeSPtr tree_node; //!< not null if the gounding location is on a tree
    std::optional<Point> boundary_location; //!< in case the gounding location is on the boundary
    Point p() const;
};

/*!
 * A layer of the lightning fill.
 *
 * Contains the trees to be printed and propagated to the next layer below.
 */
class LightningLayer
{
public:
    std::vector<LightningTreeNodeSPtr> tree_roots;

    void generateNewTrees
    (
        const Polygons& current_overhang,
        const Polygons& current_outlines,
        const EdgeGrid::Grid& outline_locator,
        const coord_t supporting_radius,
        const coord_t wall_supporting_radius
    );

    /*! Determine & connect to connection point in tree/outline.
     * \param min_dist_from_boundary_for_tree If the unsupported point is closer to the boundary than this then don't consider connecting it to a tree
     */
    GroundingLocation getBestGroundingLocation
    (
        const Point& unsupported_location,
        const Polygons& current_outlines,
        const EdgeGrid::Grid& outline_locator,
        const coord_t supporting_radius,
        const coord_t wall_supporting_radius,
        const SparseLightningTreeNodeGrid& tree_node_locator,
        const LightningTreeNodeSPtr& exclude_tree = nullptr
    );

    /*!
     * \param[out] new_child The new child node introduced
     * \param[out] new_root The new root node if one had been made
     * \return Whether a new root was added
     */
    bool attach(const Point& unsupported_location, const GroundingLocation& ground, LightningTreeNodeSPtr& new_child, LightningTreeNodeSPtr& new_root);

    void reconnectRoots
    (
        std::vector<LightningTreeNodeSPtr>& to_be_reconnected_tree_roots,
        const Polygons& current_outlines,
        const EdgeGrid::Grid& outline_locator,
        const coord_t supporting_radius,
        const coord_t wall_supporting_radius
    );

    Polygons convertToLines(const Polygons& limit_to_outline, const coord_t line_width) const;

    coord_t getWeightedDistance(const Point& boundary_loc, const Point& unsupported_location);

    void fillLocator(SparseLightningTreeNodeGrid& tree_node_locator);
};

} // namespace Slic3r

#endif // LIGHTNING_LAYER_H
