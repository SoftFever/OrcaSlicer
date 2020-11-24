#include "BoundingBox.hpp"
#include "MotionPlanner.hpp"
#include "MutablePriorityQueue.hpp"
#include "Utils.hpp"

#include <limits> // for numeric_limits
#include <assert.h>

#define BOOST_VORONOI_USE_GMP 1
#include "boost/polygon/voronoi.hpp"
using boost::polygon::voronoi_builder;
using boost::polygon::voronoi_diagram;

namespace Slic3r {

MotionPlanner::MotionPlanner(const ExPolygons &islands) : m_initialized(false)
{
    ExPolygons expp;
    for (const ExPolygon &island : islands) {
        island.simplify(SCALED_EPSILON, &expp);
        for (ExPolygon &island : expp)
            m_islands.emplace_back(MotionPlannerEnv(island));
        expp.clear();
    }
}

void MotionPlanner::initialize()
{
    // prevent initialization of empty BoundingBox
    if (m_initialized || m_islands.empty())
        return;

    // loop through islands in order to create inner expolygons and collect their contours.
    Polygons outer_holes;
    for (MotionPlannerEnv &island : m_islands) {
        // Generate the internal env boundaries by shrinking the island
        // we'll use these inner rings for motion planning (endpoints of the Voronoi-based
        // graph, visibility check) in order to avoid moving too close to the boundaries.
        island.m_env = ExPolygonCollection(offset_ex(island.m_island, -MP_INNER_MARGIN));
        // Island contours are holes of our external environment.
        outer_holes.push_back(island.m_island.contour);
    }
    
    // Generate a box contour around everyting.
    Polygons contour = offset(get_extents(outer_holes).polygon(), +MP_OUTER_MARGIN*2);
    assert(contour.size() == 1);
    // make expolygon for outer environment
    ExPolygons outer = diff_ex(contour, outer_holes);
    assert(outer.size() == 1);
    // If some of the islands are nested, then the 0th contour is the outer contour due to the order of conversion
    // from Clipper data structure into the Slic3r expolygons inside diff_ex().
    m_outer = MotionPlannerEnv(outer.front());
    m_outer.m_env = ExPolygonCollection(diff_ex(contour, offset(outer_holes, +MP_OUTER_MARGIN)));
    m_graphs.resize(m_islands.size() + 1);
    m_initialized = true;
}

Polyline MotionPlanner::shortest_path(const Point &from, const Point &to)
{
    // If we have an empty configuration space, return a straight move.
    if (m_islands.empty())
        return Polyline(from, to);
    
    // Are both points in the same island?
    int island_idx_from = -1;
    int island_idx_to   = -1;
    int island_idx      = -1;
    for (MotionPlannerEnv &island : m_islands) {
        int idx = &island - m_islands.data();
        if (island.island_contains(from))
            island_idx_from = idx;
        if (island.island_contains(to))
            island_idx_to   = idx;
        if (island_idx_from == idx && island_idx_to == idx) {
            // Since both points are in the same island, is a direct move possible?
            // If so, we avoid generating the visibility environment.
            if (island.m_island.contains(Line(from, to)))
                return Polyline(from, to);
            // Both points are inside a single island, but the straight line crosses the island boundary.
            island_idx = idx;
            break;
        }
    }
    
    // lazy generation of configuration space.
    this->initialize();

    // Get environment. If the from / to points do not share an island, then they cross an open space,
    // therefore island_idx == -1 and env will be set to the environment of the empty space.
    const MotionPlannerEnv &env = this->get_env(island_idx);
    if (env.m_env.expolygons.empty()) {
        // if this environment is empty (probably because it's too small), perform straight move
        // and avoid running the algorithms on empty dataset
        return Polyline(from, to);
    }
    
    // Now check whether points are inside the environment.
    Point inner_from = from;
    Point inner_to   = to;
    
    if (island_idx == -1) {
        // The end points do not share the same island. In that case some of the travel
        // will be likely performed inside the empty space.
        // TODO: instead of using the nearest_env_point() logic, we should
        // create a temporary graph where we connect 'from' and 'to' to the
        // nodes which don't require more than one crossing, and let Dijkstra
        // figure out the entire path - this should also replace the call to
        // find_node() below
        if (island_idx_from != -1)
            // The start point is inside some island. Find the closest point at the empty space to start from.
            inner_from = env.nearest_env_point(from, to);
        if (island_idx_to != -1)
            // The start point is inside some island. Find the closest point at the empty space to start from.
            inner_to = env.nearest_env_point(to, inner_from);
    }

    // Perform a path search either in the open space, or in a common island of from/to.
    const MotionPlannerGraph &graph = this->init_graph(island_idx);
    // If no path exists without crossing perimeters, returns a straight segment.
    Polyline polyline = graph.shortest_path(inner_from, inner_to);
    polyline.points.insert(polyline.points.begin(), from);
    polyline.points.emplace_back(to);
    
    {
        // grow our environment slightly in order for simplify_by_visibility()
        // to work best by considering moves on boundaries valid as well
        ExPolygonCollection grown_env(offset_ex(env.m_env.expolygons, float(+SCALED_EPSILON)));
        
        if (island_idx == -1) {
            /*  If 'from' or 'to' are not inside our env, they were connected using the 
                nearest_env_point() search which maybe produce ugly paths since it does not
                include the endpoint in the Dijkstra search; the simplify_by_visibility() 
                call below will not work in many cases where the endpoint is not contained in
                grown_env (whose contour was arbitrarily constructed with MP_OUTER_MARGIN,
                which may not be enough for, say, including a skirt point). So we prune
                the extra points manually. */
            if (! grown_env.contains(from)) {
                // delete second point while the line connecting first to third crosses the
                // boundaries as many times as the current first to second
                while (polyline.points.size() > 2 && intersection_ln(Line(from, polyline.points[2]), (Polygons)grown_env).size() == 1)
                    polyline.points.erase(polyline.points.begin() + 1);
            }
            if (! grown_env.contains(to))
                while (polyline.points.size() > 2 && intersection_ln(Line(*(polyline.points.end() - 3), to), (Polygons)grown_env).size() == 1)
                    polyline.points.erase(polyline.points.end() - 2);
        }

        // Perform some quick simplification (simplify_by_visibility() would make this
        // unnecessary, but this is much faster)
        polyline.simplify(MP_INNER_MARGIN/10);
        
        // remove unnecessary vertices
        // Note: this is computationally intensive and does not look very necessary
        // now that we prune the endpoints with the logic above,
        // so we comment it for now until a good test case arises
        //polyline.simplify_by_visibility(grown_env);
    
        /*
        SVG svg("shortest_path.svg");
        svg.draw(grown_env.expolygons);
        svg.arrows = false;
        for (MotionPlannerGraph::adjacency_list_t::const_iterator it = graph->adjacency_list.begin(); it != graph->adjacency_list.end(); ++it) {
            Point a = graph->nodes[it - graph->adjacency_list.begin()];
            for (std::vector<MotionPlannerGraph::Neighbor>::const_iterator n = it->begin(); n != it->end(); ++n) {
                Point b = graph->nodes[n->target];
                svg.draw(Line(a, b));
            }
        }
        svg.arrows = true;
        svg.draw(from);
        svg.draw(inner_from, "red");
        svg.draw(to);
        svg.draw(inner_to, "red");
        svg.draw(polyline, "red");
        svg.Close();
        */
    }
    
    return polyline;
}

const MotionPlannerGraph& MotionPlanner::init_graph(int island_idx)
{
    // 0th graph is the graph for m_outer. Other graphs are 1 indexed.
    MotionPlannerGraph *graph = m_graphs[island_idx + 1].get();
    if (graph == nullptr) {
        // If this graph doesn't exist, initialize it.
        m_graphs[island_idx + 1] = make_unique<MotionPlannerGraph>();
        graph = m_graphs[island_idx + 1].get();
        
        /*  We don't add polygon boundaries as graph edges, because we'd need to connect
            them to the Voronoi-generated edges by recognizing coinciding nodes. */
        
        typedef voronoi_diagram<double> VD;
        VD vd;
        // Mapping between Voronoi vertices and graph nodes.
        std::map<const VD::vertex_type*, size_t> vd_vertices;
        // get boundaries as lines
        const MotionPlannerEnv &env = this->get_env(island_idx);
        Lines lines = env.m_env.lines();
        boost::polygon::construct_voronoi(lines.begin(), lines.end(), &vd);
        // traverse the Voronoi diagram and generate graph nodes and edges
        for (const VD::edge_type &edge : vd.edges()) {
            if (edge.is_infinite())
                continue;
            const VD::vertex_type* v0 = edge.vertex0();
            const VD::vertex_type* v1 = edge.vertex1();
            Point p0(v0->x(), v0->y());
            Point p1(v1->x(), v1->y());
            // Insert only Voronoi edges fully contained in the island.
            //FIXME This test has a terrible O(n^2) time complexity.
            if (env.island_contains_b(p0) && env.island_contains_b(p1)) {
                // Find v0 in the graph, allocate a new node if v0 does not exist in the graph yet.
                auto i_v0 = vd_vertices.find(v0);
                size_t v0_idx;
                if (i_v0 == vd_vertices.end())
                    vd_vertices[v0] = v0_idx = graph->add_node(p0);
                else
                    v0_idx = i_v0->second;
                // Find v1 in the graph, allocate a new node if v0 does not exist in the graph yet.
                auto i_v1 = vd_vertices.find(v1);
                size_t v1_idx;
                if (i_v1 == vd_vertices.end())
                    vd_vertices[v1] = v1_idx = graph->add_node(p1);
                else
                    v1_idx = i_v1->second;
                // Euclidean distance is used as weight for the graph edge
                graph->add_edge(v0_idx, v1_idx, (p1 - p0).cast<double>().norm());
            }
        }
    }

    return *graph;
}

// Find a middle point on the path from start_point to end_point with the shortest path.
static inline size_t nearest_waypoint_index(const Point &start_point, const Points &middle_points, const Point &end_point)
{
    size_t idx = size_t(-1);
    double dmin = std::numeric_limits<double>::infinity();
    for (const Point &p : middle_points) {
        double d = (p - start_point).cast<double>().norm() + (end_point - p).cast<double>().norm();
        if (d < dmin) {
            idx  = &p - middle_points.data();
            dmin = d;
            if (dmin < EPSILON)
                break;
        }
    }
    return idx;
}

Point MotionPlannerEnv::nearest_env_point(const Point &from, const Point &to) const
{
    /*  In order to ensure that the move between 'from' and the initial env point does
        not violate any of the configuration space boundaries, we limit our search to
        the points that satisfy this condition. */
    
    /*  Assume that this method is never called when 'env' contains 'from';
        so 'from' is either inside a hole or outside all contours */
    
    // get the points of the hole containing 'from', if any
    Points pp;
    for (const ExPolygon &ex : m_env.expolygons) {
        for (const Polygon &hole : ex.holes)
            if (hole.contains(from))
                pp = hole.points;
        if (! pp.empty())
            break;
    }
    
    // If 'from' is not inside a hole, it's outside of all contours, so take all contours' points.
    if (pp.empty())
        for (const ExPolygon &ex : m_env.expolygons)
            append(pp, ex.contour.points);
    
    // Find the candidate result and check that it doesn't cross too many boundaries.
    while (pp.size() > 1) {
        // find the point in pp that is closest to both 'from' and 'to'
        size_t result = nearest_waypoint_index(from, pp, to);
        // as we assume 'from' is outside env, any node will require at least one crossing
        if (intersection_ln(Line(from, pp[result]), m_island).size() > 1) {
            // discard result
            pp.erase(pp.begin() + result);
        } else
            return pp[result];
    }
    
    // if we're here, return last point if any (better than nothing)
    // if we have no points at all, then we have an empty environment and we
    // make this method behave as a no-op (we shouldn't get here by the way)
    return pp.empty() ? from : pp.front();
}

// Add a new directed edge to the adjacency graph.
void MotionPlannerGraph::add_edge(size_t from, size_t to, double weight)
{
    // Extend adjacency list until this start node.
    if (m_adjacency_list.size() < from + 1) {
        // Reserve in powers of two to avoid repeated reallocation.
        m_adjacency_list.reserve(std::max<uint32_t>(8, next_highest_power_of_2((uint32_t)(from + 1))));
        // Allocate new empty adjacency vectors.
        m_adjacency_list.resize(from + 1);
    }
    m_adjacency_list[from].emplace_back(Neighbor(node_t(to), weight));
}

// Dijkstra's shortest path in a weighted graph from node_start to node_end.
// The returned path contains the end points.
// If no path exists from node_start to node_end, a straight segment is returned.
Polyline MotionPlannerGraph::shortest_path(size_t node_start, size_t node_end) const
{
    // This prevents a crash in case for some reason we got here with an empty adjacency list.
    if (this->empty())
        return Polyline();

    // Dijkstra algorithm, previous node of the current node 'u' in the shortest path towards node_start.
    std::vector<node_t>   previous(m_adjacency_list.size(), -1);
    std::vector<weight_t> distance(m_adjacency_list.size(), std::numeric_limits<weight_t>::infinity());
    std::vector<size_t>   map_node_to_queue_id(m_adjacency_list.size(), size_t(-1));
    distance[node_start] = 0.;

    auto queue = make_mutable_priority_queue<node_t, false>(
        [&map_node_to_queue_id](const node_t node, size_t idx) { map_node_to_queue_id[node] = idx; },
        [&distance](const node_t node1, const node_t node2) { return distance[node1] < distance[node2]; });
    queue.reserve(m_adjacency_list.size());
    for (size_t i = 0; i < m_adjacency_list.size(); ++ i)
        queue.push(node_t(i));

    while (! queue.empty()) {
        // Get the next node with the lowest distance to node_start.
        node_t u = node_t(queue.top());
        queue.pop();
        map_node_to_queue_id[u] = size_t(-1);
        // Stop searching if we reached our destination.
        if (size_t(u) == node_end)
            break;
        // Visit each edge starting at node u.
        for (const Neighbor& neighbor : m_adjacency_list[u])
            if (map_node_to_queue_id[neighbor.target] != size_t(-1)) {
                weight_t alt = distance[u] + neighbor.weight;
                // If total distance through u is shorter than the previous
                // distance (if any) between node_start and neighbor.target, replace it.
                if (alt < distance[neighbor.target]) {
                    distance[neighbor.target] = alt;
                    previous[neighbor.target] = u;
                    queue.update(map_node_to_queue_id[neighbor.target]);
                }
            }
    }

    // In case the end point was not reached, previous[node_end] contains -1
    // and a straight line from node_start to node_end is returned.
    Polyline polyline;
    polyline.points.reserve(m_adjacency_list.size());
    for (node_t vertex = node_t(node_end); vertex != -1; vertex = previous[vertex])
        polyline.points.emplace_back(m_nodes[vertex]);
    polyline.points.emplace_back(m_nodes[node_start]);
    polyline.reverse();
    return polyline;
}

}
