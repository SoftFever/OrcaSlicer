#include "BoundingBox.hpp"
#include "MotionPlanner.hpp"
#include "MutablePriorityQueue.hpp"
#include "Utils.hpp"

#include <limits> // for numeric_limits
#include <assert.h>

#include "boost/polygon/voronoi.hpp"
using boost::polygon::voronoi_builder;
using boost::polygon::voronoi_diagram;

namespace Slic3r {

MotionPlanner::MotionPlanner(const ExPolygons &islands) : initialized(false)
{
    ExPolygons expp;
    for (const ExPolygon &island : islands) {
        island.simplify(SCALED_EPSILON, &expp);
        for (ExPolygon &island : expp)
            this->islands.push_back(MotionPlannerEnv(island));
        expp.clear();
    }
}

void MotionPlanner::initialize()
{
    // prevent initialization of empty BoundingBox
    if (this->initialized || this->islands.empty())
        return;
    
    // loop through islands in order to create inner expolygons and collect their contours
    Polygons outer_holes;
    for (MotionPlannerEnv &island : this->islands) {
        // generate the internal env boundaries by shrinking the island
        // we'll use these inner rings for motion planning (endpoints of the Voronoi-based
        // graph, visibility check) in order to avoid moving too close to the boundaries
        island.env = ExPolygonCollection(offset_ex(island.island, -MP_INNER_MARGIN));
        // island contours are holes of our external environment
        outer_holes.push_back(island.island.contour);
    }
    
    // Generate a box contour around everyting.
    Polygons contour = offset(get_extents(outer_holes).polygon(), +MP_OUTER_MARGIN*2);
    assert(contour.size() == 1);
    // make expolygon for outer environment
    ExPolygons outer = diff_ex(contour, outer_holes);
    assert(outer.size() == 1);
    //FIXME What if some of the islands are nested? Then the front contour may not be the outmost contour!
    this->outer.island = outer.front();
    this->outer.env = ExPolygonCollection(diff_ex(contour, offset(outer_holes, +MP_OUTER_MARGIN)));
    this->graphs.resize(this->islands.size() + 1);
    this->initialized = true;
}

Polyline MotionPlanner::shortest_path(const Point &from, const Point &to)
{
    // If we have an empty configuration space, return a straight move.
    if (this->islands.empty())
        return Line(from, to);
    
    // Are both points in the same island?
    int island_idx = -1;
    for (MotionPlannerEnv &island : islands) {
        if (island.island_bbox.contains(from) && island.island_bbox.contains(to) &&
            island.island.contains(from) && island.island.contains(to)) {
            // Since both points are in the same island, is a direct move possible?
            // If so, we avoid generating the visibility environment.
            if (island.island.contains(Line(from, to)))
                return Line(from, to);
            // Both points are inside a single island, but the straight line crosses the island boundary.
            island_idx = &island - this->islands.data();
            break;
        }
    }
    
    // lazy generation of configuration space.
    this->initialize();

    // get environment
    const MotionPlannerEnv &env = this->get_env(island_idx);
    if (env.env.expolygons.empty()) {
        // if this environment is empty (probably because it's too small), perform straight move
        // and avoid running the algorithms on empty dataset
        return Line(from, to);
    }
    
    // Now check whether points are inside the environment.
    Point inner_from = from;
    Point inner_to   = to;
    
    if (island_idx == -1) {
        // TODO: instead of using the nearest_env_point() logic, we should
        // create a temporary graph where we connect 'from' and 'to' to the
        // nodes which don't require more than one crossing, and let Dijkstra
        // figure out the entire path - this should also replace the call to
        // find_node() below
        if (! env.island_bbox.contains(inner_from) || ! env.island.contains(inner_from)) {
            // Find the closest inner point to start from.
            inner_from = env.nearest_env_point(from, to);
        }
        if (! env.island_bbox.contains(inner_to) || ! env.island.contains(inner_to)) {
            // Find the closest inner point to start from.
            inner_to = env.nearest_env_point(to, inner_from);
        }
    }
    
    // perform actual path search
    const MotionPlannerGraph &graph = this->init_graph(island_idx);
    Polyline polyline = graph.shortest_path(graph.find_closest_node(inner_from), graph.find_closest_node(inner_to));
    
    polyline.points.insert(polyline.points.begin(), from);
    polyline.points.push_back(to);
    
    {
        // grow our environment slightly in order for simplify_by_visibility()
        // to work best by considering moves on boundaries valid as well
        ExPolygonCollection grown_env(offset_ex(env.env.expolygons, +SCALED_EPSILON));
        
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
                while (polyline.points.size() > 2 && intersection_ln((Lines)Line(from, polyline.points[2]), grown_env).size() == 1)
                    polyline.points.erase(polyline.points.begin() + 1);
            }
            if (! grown_env.contains(to)) {
                while (polyline.points.size() > 2 && intersection_ln((Lines)Line(*(polyline.points.end() - 3), to), grown_env).size() == 1)
                    polyline.points.erase(polyline.points.end() - 2);
            }
        }
        
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
    if (! this->graphs[island_idx + 1]) {
        // if this graph doesn't exist, initialize it
        this->graphs[island_idx + 1] = make_unique<MotionPlannerGraph>();
        MotionPlannerGraph* graph = this->graphs[island_idx + 1].get();
        
        /*  We don't add polygon boundaries as graph edges, because we'd need to connect
            them to the Voronoi-generated edges by recognizing coinciding nodes. */
        
        typedef voronoi_diagram<double> VD;
        VD vd;
        
        // mapping between Voronoi vertices and graph nodes
        typedef std::map<const VD::vertex_type*,size_t> t_vd_vertices;
        t_vd_vertices vd_vertices;
        
        // get boundaries as lines
        const MotionPlannerEnv &env = this->get_env(island_idx);
        Lines lines = env.env.lines();
        boost::polygon::construct_voronoi(lines.begin(), lines.end(), &vd);
        
        // traverse the Voronoi diagram and generate graph nodes and edges
        for (VD::const_edge_iterator edge = vd.edges().begin(); edge != vd.edges().end(); ++edge) {
            if (edge->is_infinite()) continue;
            
            const VD::vertex_type* v0 = edge->vertex0();
            const VD::vertex_type* v1 = edge->vertex1();
            Point p0 = Point(v0->x(), v0->y());
            Point p1 = Point(v1->x(), v1->y());
            
            // skip edge if any of its endpoints is outside our configuration space
            //FIXME This test has a terrible O(n^2) time complexity.
            if (!env.island.contains_b(p0) || !env.island.contains_b(p1)) continue;
            
            t_vd_vertices::const_iterator i_v0 = vd_vertices.find(v0);
            size_t v0_idx;
            if (i_v0 == vd_vertices.end()) {
                graph->nodes.push_back(p0);
                vd_vertices[v0] = v0_idx = graph->nodes.size()-1;
            } else {
                v0_idx = i_v0->second;
            }
            
            t_vd_vertices::const_iterator i_v1 = vd_vertices.find(v1);
            size_t v1_idx;
            if (i_v1 == vd_vertices.end()) {
                graph->nodes.push_back(p1);
                vd_vertices[v1] = v1_idx = graph->nodes.size()-1;
            } else {
                v1_idx = i_v1->second;
            }
            
            // Euclidean distance is used as weight for the graph edge
            double dist = graph->nodes[v0_idx].distance_to(graph->nodes[v1_idx]);
            graph->add_edge(v0_idx, v1_idx, dist);
        }
    }

    return *this->graphs[island_idx + 1].get();
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
    for (const ExPolygon &ex : this->env.expolygons) {
        for (const Polygon &hole : ex.holes)
            if (hole.contains(from))
                pp = hole;
        if (! pp.empty())
            break;
    }
    
    /*  If 'from' is not inside a hole, it's outside of all contours, so take all
        contours' points */
    if (pp.empty())
        for (const ExPolygon &ex : this->env.expolygons)
            append(pp, ex.contour.points);
    
    /*  Find the candidate result and check that it doesn't cross too many boundaries. */
    while (pp.size() >= 2) {
        // find the point in pp that is closest to both 'from' and 'to'
        size_t result = from.nearest_waypoint_index(pp, to);
        
        // as we assume 'from' is outside env, any node will require at least one crossing
        if (intersection_ln((Lines)Line(from, pp[result]), this->island).size() > 1) {
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
    if (this->adjacency_list.size() < from + 1) {
        // Reserve in powers of two to avoid repeated reallocation.
        this->adjacency_list.reserve(std::max<size_t>(8, next_highest_power_of_2(from + 1)));
        // Allocate new empty adjacency vectors.
        this->adjacency_list.resize(from + 1);
    }
    this->adjacency_list[from].emplace_back(Neighbor(node_t(to), weight));
}

// Dijkstra's shortest path in a weighted graph from node_start to node_end.
// The returned path contains the end points.
Polyline MotionPlannerGraph::shortest_path(size_t node_start, size_t node_end) const
{
    // This prevents a crash in case for some reason we got here with an empty adjacency list.
    if (this->adjacency_list.empty())
        return Polyline();

    // Dijkstra algorithm, previous node of the current node 'u' in the shortest path towards node_start.
    std::vector<node_t>   previous(this->adjacency_list.size(), -1);
    std::vector<weight_t> distance(this->adjacency_list.size(), std::numeric_limits<weight_t>::infinity());
    std::vector<size_t>   map_node_to_queue_id(this->adjacency_list.size(), size_t(-1));
    distance[node_start] = 0.;

    auto queue = make_mutable_priority_queue<node_t>(
        [&map_node_to_queue_id](const node_t &node, size_t idx) { map_node_to_queue_id[node] = idx; },
        [&distance](const node_t &node1, const node_t &node2) { return distance[node1] < distance[node2]; });
    queue.reserve(this->adjacency_list.size());
    for (size_t i = 0; i < this->adjacency_list.size(); ++ i)
        queue.push(node_t(i));

    while (! queue.empty()) {
        // Get the next node with the lowest distance to node_start.
        node_t u = node_t(queue.top());
        queue.pop();
        map_node_to_queue_id[u] = size_t(-1);
        // Stop searching if we reached our destination.
        if (u == node_end)
            break;
        // Visit each edge starting at node u.
        for (const Neighbor& neighbor : this->adjacency_list[u])
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

    Polyline polyline;
    polyline.points.reserve(previous.size());
    for (node_t vertex = node_t(node_end); vertex != -1; vertex = previous[vertex])
        polyline.points.push_back(this->nodes[vertex]);
    polyline.points.push_back(this->nodes[node_start]);
    polyline.reverse();
    return polyline;
}

}
