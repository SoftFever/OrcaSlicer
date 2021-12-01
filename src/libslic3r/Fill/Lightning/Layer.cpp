//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "Layer.hpp" //The class we're implementing.

#include <iterator> // advance

#include "DistanceField.hpp"
#include "TreeNode.hpp"

#include "../../Geometry.hpp"

namespace Slic3r::FillLightning {

coord_t Layer::getWeightedDistance(const Point& boundary_loc, const Point& unsupported_location)
{
    return coord_t((boundary_loc - unsupported_location).cast<double>().norm());
}

Point GroundingLocation::p() const
{
    assert(tree_node || boundary_location);
    return tree_node ? tree_node->getLocation() : *boundary_location;
}

void Layer::fillLocator(SparseNodeGrid &tree_node_locator)
{
    std::function<void(NodeSPtr)> add_node_to_locator_func = [&tree_node_locator](NodeSPtr node) {
        tree_node_locator.insert(std::make_pair(Point(node->getLocation().x() / locator_cell_size, node->getLocation().y() / locator_cell_size), node)); 
    };
    for (auto& tree : tree_roots)
        tree->visitNodes(add_node_to_locator_func);
}

void Layer::generateNewTrees
(
    const Polygons& current_overhang,
    const Polygons& current_outlines,
    const EdgeGrid::Grid& outlines_locator,
    const coord_t supporting_radius,
    const coord_t wall_supporting_radius
)
{
    DistanceField distance_field(supporting_radius, current_outlines, current_overhang);

    SparseNodeGrid tree_node_locator;
    fillLocator(tree_node_locator);

    // Until no more points need to be added to support all:
    // Determine next point from tree/outline areas via distance-field
    Point unsupported_location;
    while (distance_field.tryGetNextPoint(&unsupported_location)) {
        GroundingLocation grounding_loc = getBestGroundingLocation(
            unsupported_location, current_outlines, outlines_locator, supporting_radius, wall_supporting_radius, tree_node_locator);

        NodeSPtr new_parent;
        NodeSPtr new_child;
        this->attach(unsupported_location, grounding_loc, new_child, new_parent);
        tree_node_locator.insert(std::make_pair(Point(new_child->getLocation().x() / locator_cell_size, new_child->getLocation().y() / locator_cell_size), new_child));
        if (new_parent)
            tree_node_locator.insert(std::make_pair(Point(new_parent->getLocation().x() / locator_cell_size, new_parent->getLocation().y() / locator_cell_size), new_parent));
        // update distance field
        distance_field.update(grounding_loc.p(), unsupported_location);
    }
}

static bool polygonCollidesWithLineSegment(const Point from, const Point to, const EdgeGrid::Grid &loc_to_line)
{
    struct Visitor {
        explicit Visitor(const EdgeGrid::Grid &grid) : grid(grid) {}

        bool operator()(coord_t iy, coord_t ix) {
            // Called with a row and colum of the grid cell, which is intersected by a line.
            auto cell_data_range = grid.cell_data_range(iy, ix);
            for (auto it_contour_and_segment = cell_data_range.first; it_contour_and_segment != cell_data_range.second; ++ it_contour_and_segment) {
                // End points of the line segment and their vector.
                auto segment = grid.segment(*it_contour_and_segment);
                if (Geometry::segments_intersect(segment.first, segment.second, line.a, line.b)) {
                    this->intersect = true;
                    return false;
                }
            }
            // Continue traversing the grid along the edge.
            return true;
        }

        const EdgeGrid::Grid& grid;
        Line                  line;
        bool                  intersect = false;
    } visitor(loc_to_line);

    loc_to_line.visit_cells_intersecting_line(from, to, visitor);
    return visitor.intersect;
}

GroundingLocation Layer::getBestGroundingLocation
(
    const Point& unsupported_location,
    const Polygons& current_outlines,
    const EdgeGrid::Grid& outline_locator,
    const coord_t supporting_radius,
    const coord_t wall_supporting_radius,
    const SparseNodeGrid& tree_node_locator,
    const NodeSPtr& exclude_tree
)
{
    // Closest point on current_outlines to unsupported_location:
    Point node_location;
    {
        double d2 = std::numeric_limits<double>::max();
        for (const Polygon &contour : current_outlines)
            if (contour.size() > 2) {
                Point prev = contour.points.back();
                for (const Point &p2 : contour.points) {
                    if (double d = Line::distance_to_squared(unsupported_location, prev, p2); d < d2) {
                        d2 = d;
                        node_location = Geometry::foot_pt({ prev, p2 }, unsupported_location).cast<coord_t>();
                    }
                    prev = p2;
                }
            }
    }

    const auto within_dist = coord_t((node_location - unsupported_location).cast<double>().norm());

    NodeSPtr sub_tree{ nullptr };
    coord_t current_dist = getWeightedDistance(node_location, unsupported_location);
    if (current_dist >= wall_supporting_radius) { // Only reconnect tree roots to other trees if they are not already close to the outlines.
        const coord_t search_radius = std::min(current_dist, within_dist);
        BoundingBox region(unsupported_location - Point(search_radius, search_radius), unsupported_location + Point(search_radius + locator_cell_size, search_radius + locator_cell_size));
        region.min /= locator_cell_size;
        region.max /= locator_cell_size;
        Point grid_addr;
        for (grid_addr.y() = region.min.y(); grid_addr.y() < region.max.y(); ++ grid_addr.y())
            for (grid_addr.x() = region.min.x(); grid_addr.x() < region.max.x(); ++ grid_addr.x()) {
                auto it_range = tree_node_locator.equal_range(grid_addr);
                for (auto it = it_range.first; it != it_range.second; ++ it) {
                    auto candidate_sub_tree = it->second.lock();
                    if ((candidate_sub_tree && candidate_sub_tree != exclude_tree) &&
                        !(exclude_tree && exclude_tree->hasOffspring(candidate_sub_tree)) &&
                        !polygonCollidesWithLineSegment(unsupported_location, candidate_sub_tree->getLocation(), outline_locator)) {
                        const coord_t candidate_dist = candidate_sub_tree->getWeightedDistance(unsupported_location, supporting_radius);
                        if (candidate_dist < current_dist) {
                            current_dist = candidate_dist;
                            sub_tree = candidate_sub_tree;
                        }
                    }
                }
        }
    }

    return ! sub_tree ?
        GroundingLocation{ nullptr, node_location } :
        GroundingLocation{ sub_tree, std::optional<Point>() };
}

bool Layer::attach(
    const Point& unsupported_location,
    const GroundingLocation& grounding_loc,
    NodeSPtr& new_child,
    NodeSPtr& new_root)
{
    // Update trees & distance fields.
    if (grounding_loc.boundary_location) {
        new_root = Node::create(grounding_loc.p(), std::make_optional(grounding_loc.p()));
        new_child = new_root->addChild(unsupported_location);
        tree_roots.push_back(new_root);
        return true;
    } else {
        new_child = grounding_loc.tree_node->addChild(unsupported_location);
        return false;
    }
}

void Layer::reconnectRoots
(
    std::vector<NodeSPtr>& to_be_reconnected_tree_roots,
    const Polygons& current_outlines,
    const EdgeGrid::Grid& outline_locator,
    const coord_t supporting_radius,
    const coord_t wall_supporting_radius
)
{
    constexpr coord_t tree_connecting_ignore_offset = 100;

    SparseNodeGrid tree_node_locator;
    fillLocator(tree_node_locator);

    const coord_t within_max_dist = outline_locator.resolution() * 2;
    for (auto root_ptr : to_be_reconnected_tree_roots)
    {
        auto old_root_it = std::find(tree_roots.begin(), tree_roots.end(), root_ptr);

        if (root_ptr->getLastGroundingLocation())
        {
            const Point& ground_loc = *root_ptr->getLastGroundingLocation();
            if (ground_loc != root_ptr->getLocation())
            {
                Point new_root_pt;
                // Find an intersection of the line segment from root_ptr->getLocation() to ground_loc, at within_max_dist from ground_loc.
                if (lineSegmentPolygonsIntersection(root_ptr->getLocation(), ground_loc, outline_locator, new_root_pt, within_max_dist)) {
                    auto new_root = Node::create(new_root_pt, new_root_pt);
                    root_ptr->addChild(new_root);
                    new_root->reroot();

                    tree_node_locator.insert(std::make_pair(Point(new_root->getLocation().x() / locator_cell_size, new_root->getLocation().y() / locator_cell_size), new_root));

                    *old_root_it = std::move(new_root); // replace old root with new root
                    continue;
                }
            }
        }

        const coord_t tree_connecting_ignore_width = wall_supporting_radius - tree_connecting_ignore_offset; // Ideally, the boundary size in which the valence rule is ignored would be configurable.
        GroundingLocation ground =
            getBestGroundingLocation
            (
                root_ptr->getLocation(),
                current_outlines,
                outline_locator,
                supporting_radius,
                tree_connecting_ignore_width,
                tree_node_locator,
                root_ptr
            );
        if (ground.boundary_location)
        {
            if (*ground.boundary_location == root_ptr->getLocation())
                continue; // Already on the boundary.

            auto new_root = Node::create(ground.p(), ground.p());
            auto attach_ptr = root_ptr->closestNode(new_root->getLocation());
            attach_ptr->reroot();

            new_root->addChild(attach_ptr);
            tree_node_locator.insert(std::make_pair(new_root->getLocation(), new_root));

            *old_root_it = std::move(new_root); // replace old root with new root
        }
        else
        {
            assert(ground.tree_node);
            assert(ground.tree_node != root_ptr);
            assert(!root_ptr->hasOffspring(ground.tree_node));
            assert(!ground.tree_node->hasOffspring(root_ptr));

            auto attach_ptr = root_ptr->closestNode(ground.tree_node->getLocation());
            attach_ptr->reroot();

            ground.tree_node->addChild(attach_ptr);

            // remove old root
            *old_root_it = std::move(tree_roots.back());
            tree_roots.pop_back();
        }
    }
}

/*
 * Implementation assumes moving inside, but moving outside should just as well be possible.
 */
static unsigned int moveInside(const Polygons& polygons, Point& from, int distance, int64_t maxDist2)
{
    Point ret = from;
    int64_t bestDist2 = std::numeric_limits<int64_t>::max();
    unsigned int bestPoly = static_cast<unsigned int>(-1);
    bool is_already_on_correct_side_of_boundary = false; // whether [from] is already on the right side of the boundary
    for (unsigned int poly_idx = 0; poly_idx < polygons.size(); poly_idx++)
    {
        const Polygon &poly = polygons[poly_idx];
        if (poly.size() < 2)
            continue;
        Point p0 = poly[poly.size() - 2];
        Point p1 = poly.back();
        // because we compare with vSize2 here (no division by zero), we also need to compare by vSize2 inside the loop
        // to avoid integer rounding edge cases
        bool projected_p_beyond_prev_segment = (p1 - p0).cast<int64_t>().dot((from - p0).cast<int64_t>()) >= (p1 - p0).cast<int64_t>().squaredNorm();
        for (const Point& p2 : poly)
        {
            // X = A + Normal(B-A) * (((B-A) dot (P-A)) / VSize(B-A));
            //   = A +       (B-A) *  ((B-A) dot (P-A)) / VSize2(B-A);
            // X = P projected on AB
            const Point& a = p1;
            const Point& b = p2;
            const Point& p = from;
            Point ab = b - a;
            Point ap = p - a;
            int64_t ab_length2 = ab.cast<int64_t>().squaredNorm();
            if (ab_length2 <= 0) //A = B, i.e. the input polygon had two adjacent points on top of each other.
            {
                p1 = p2; //Skip only one of the points.
                continue;
            }
            int64_t dot_prod = ab.cast<int64_t>().dot(ap.cast<int64_t>());
            if (dot_prod <= 0) // x is projected to before ab
            {
                if (projected_p_beyond_prev_segment)
                { //  case which looks like:   > .
                    projected_p_beyond_prev_segment = false;
                    Point& x = p1;

                    int64_t dist2 = (x - p).cast<int64_t>().squaredNorm();
                    if (dist2 < bestDist2)
                    {
                        bestDist2 = dist2;
                        bestPoly = poly_idx;
                        if (distance == 0) { 
                            ret = x;
                        } else {
                            // inward direction irrespective of sign of [distance]
                            Point inward_dir = perp((ab.cast<double>().normalized() * scaled<double>(10.0) + (p1 - p0).cast<double>().normalized() * scaled<double>(10.0)).eval()).cast<coord_t>();
                            // MM2INT(10.0) to retain precision for the eventual normalization
                            ret = x + (inward_dir.cast<double>().normalized() * distance).cast<coord_t>();
                            is_already_on_correct_side_of_boundary = inward_dir.cast<int64_t>().dot((p - x).cast<int64_t>()) * distance >= 0;
                        }
                    }
                }
                else
                {
                    projected_p_beyond_prev_segment = false;
                    p0 = p1;
                    p1 = p2;
                    continue;
                }
            }
            else if (dot_prod >= ab_length2) // x is projected to beyond ab
            {
                projected_p_beyond_prev_segment = true;
                p0 = p1;
                p1 = p2;
                continue;
            }
            else
            { // x is projected to a point properly on the line segment (not onto a vertex). The case which looks like | .
                projected_p_beyond_prev_segment = false;
                Point x = a + ab * dot_prod / ab_length2;

                int64_t dist2 = (p - x).cast<int64_t>().squaredNorm();
                if (dist2 < bestDist2)
                {
                    bestDist2 = dist2;
                    bestPoly = poly_idx;
                    if (distance == 0) { ret = x; }
                    else
                    {
                        // inward or outward depending on the sign of [distance]
                        Vec2d inward_dir = perp((ab.cast<double>().normalized() * distance).eval());
                        ret = x + inward_dir.cast<coord_t>();
                        is_already_on_correct_side_of_boundary = inward_dir.dot((p - x).cast<double>()) >= 0;
                    }
                }
            }
            p0 = p1;
            p1 = p2;
        }
    }
    if (is_already_on_correct_side_of_boundary) // when the best point is already inside and we're moving inside, or when the best point is already outside and we're moving outside
    {
        if (bestDist2 < distance * distance)
        {
            from = ret;
        }
        else
        {
            //            from = from; // original point stays unaltered. It is already inside by enough distance
        }
        return bestPoly;
    }
    else if (bestDist2 < maxDist2)
    {
        from = ret;
        return bestPoly;
    }
    return static_cast<unsigned int>(-1);
}

// Returns 'added someting'.
Polylines Layer::convertToLines(const Polygons& limit_to_outline, const coord_t line_width) const
{
    if (tree_roots.empty())
        return {};

    Polygons result_lines;
    for (const auto& tree : tree_roots) {
        // If even the furthest location in the tree is inside the polygon, the entire tree must be inside of the polygon.
        // (Don't take the root as that may be on the edge and cause rounding errors to register as 'outside'.)
        constexpr coord_t epsilon = 5;
        Point should_be_inside = tree->getLocation();
        moveInside(limit_to_outline, should_be_inside, epsilon, epsilon * epsilon);
        if (inside(limit_to_outline, should_be_inside))
            tree->convertToPolylines(result_lines, line_width);
    }

    // TODO: allow for polylines!
    Polylines split_lines;
    for (Polygon &line : result_lines) {
        if (line.size() <= 1)
            continue;
        Point last = line[0];
        for (size_t point_idx = 1; point_idx < line.size(); point_idx++) {
            Point here = line[point_idx];
            split_lines.push_back({ last, here });
            last = here;
        }
    }

    return split_lines;
}

} // namespace Slic3r::Lightning
