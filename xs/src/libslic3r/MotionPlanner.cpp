#include "BoundingBox.hpp"
#include "MotionPlanner.hpp"
#include <limits> // for numeric_limits
#include <assert.h>

#include "boost/polygon/voronoi.hpp"
using boost::polygon::voronoi_builder;
using boost::polygon::voronoi_diagram;

namespace Slic3r {

MotionPlanner::MotionPlanner(const ExPolygons &islands)
    : initialized(false)
{
    ExPolygons expp;
    for (ExPolygons::const_iterator island = islands.begin(); island != islands.end(); ++island)
        island->simplify(SCALED_EPSILON, &expp);
    
    for (ExPolygons::const_iterator island = expp.begin(); island != expp.end(); ++island)
        this->islands.push_back(MotionPlannerEnv(*island));
}

MotionPlanner::~MotionPlanner()
{
    for (std::vector<MotionPlannerGraph*>::iterator graph = this->graphs.begin(); graph != this->graphs.end(); ++graph)
        delete *graph;
}

size_t
MotionPlanner::islands_count() const
{
    return this->islands.size();
}

void
MotionPlanner::initialize()
{
    if (this->initialized) return;
    if (this->islands.empty()) return;  // prevent initialization of empty BoundingBox
    
    // loop through islands in order to create inner expolygons and collect their contours
    Polygons outer_holes;
    for (std::vector<MotionPlannerEnv>::iterator island = this->islands.begin(); island != this->islands.end(); ++island) {
        // generate the internal env boundaries by shrinking the island
        // we'll use these inner rings for motion planning (endpoints of the Voronoi-based
        // graph, visibility check) in order to avoid moving too close to the boundaries
        island->env = offset_ex(island->island, -MP_INNER_MARGIN);
        
        // island contours are holes of our external environment
        outer_holes.push_back(island->island.contour);
    }
    
    // generate outer contour as bounding box of everything
    BoundingBox bb;
    for (Polygons::const_iterator contour = outer_holes.begin(); contour != outer_holes.end(); ++contour)
        bb.merge(contour->bounding_box());
    
    // grow outer contour
    Polygons contour = offset(bb.polygon(), +MP_OUTER_MARGIN*2);
    assert(contour.size() == 1);
    
    // make expolygon for outer environment
    ExPolygons outer = diff_ex(contour, outer_holes);
    assert(outer.size() == 1);
    this->outer.island = outer.front();
    
    this->outer.env = ExPolygonCollection(diff_ex(contour, offset(outer_holes, +MP_OUTER_MARGIN)));
    
    this->graphs.resize(this->islands.size() + 1, NULL);
    this->initialized = true;
}

const MotionPlannerEnv&
MotionPlanner::get_env(int island_idx) const
{
    if (island_idx == -1) {
        return this->outer;
    } else {
        return this->islands[island_idx];
    }
}

Polyline
MotionPlanner::shortest_path(const Point &from, const Point &to)
{
    // if we have an empty configuration space, return a straight move
    if (this->islands.empty())
        return Line(from, to);
    
    // Are both points in the same island?
    int island_idx = -1;
    for (std::vector<MotionPlannerEnv>::const_iterator island = this->islands.begin(); island != this->islands.end(); ++island) {
        if (island->island.contains(from) && island->island.contains(to)) {
            // since both points are in the same island, is a direct move possible?
            // if so, we avoid generating the visibility environment
            if (island->island.contains(Line(from, to)))
                return Line(from, to);
            
            island_idx = island - this->islands.begin();
            break;
        }
    }
    
    // lazy generation of configuration space
    this->initialize();
    
    // get environment
    MotionPlannerEnv env = this->get_env(island_idx);
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
        if (!env.island.contains(inner_from)) {
            // Find the closest inner point to start from.
            inner_from = env.nearest_env_point(from, to);
        }
        if (!env.island.contains(inner_to)) {
            // Find the closest inner point to start from.
            inner_to = env.nearest_env_point(to, inner_from);
        }
    }
    
    // perform actual path search
    MotionPlannerGraph* graph = this->init_graph(island_idx);
    Polyline polyline = graph->shortest_path(graph->find_node(inner_from), graph->find_node(inner_to));
    
    polyline.points.insert(polyline.points.begin(), from);
    polyline.points.push_back(to);
    
    {
        // grow our environment slightly in order for simplify_by_visibility()
        // to work best by considering moves on boundaries valid as well
        ExPolygonCollection grown_env(offset_ex(env.env, +SCALED_EPSILON));
        
        if (island_idx == -1) {
            /*  If 'from' or 'to' are not inside our env, they were connected using the 
                nearest_env_point() search which maybe produce ugly paths since it does not
                include the endpoint in the Dijkstra search; the simplify_by_visibility() 
                call below will not work in many cases where the endpoint is not contained in
                grown_env (whose contour was arbitrarily constructed with MP_OUTER_MARGIN,
                which may not be enough for, say, including a skirt point). So we prune
                the extra points manually. */
            if (!grown_env.contains(from)) {
                // delete second point while the line connecting first to third crosses the
                // boundaries as many times as the current first to second
                while (polyline.points.size() > 2 && intersection((Lines)Line(from, polyline.points[2]), grown_env).size() == 1) {
                    polyline.points.erase(polyline.points.begin() + 1);
                }
            }
            if (!grown_env.contains(to)) {
                while (polyline.points.size() > 2 && intersection((Lines)Line(*(polyline.points.end() - 3), to), grown_env).size() == 1) {
                    polyline.points.erase(polyline.points.end() - 2);
                }
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
            for (std::vector<MotionPlannerGraph::neighbor>::const_iterator n = it->begin(); n != it->end(); ++n) {
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

MotionPlannerGraph*
MotionPlanner::init_graph(int island_idx)
{
    if (this->graphs[island_idx + 1] == NULL) {
        // if this graph doesn't exist, initialize it
        MotionPlannerGraph* graph = this->graphs[island_idx + 1] = new MotionPlannerGraph();
        
        /*  We don't add polygon boundaries as graph edges, because we'd need to connect
            them to the Voronoi-generated edges by recognizing coinciding nodes. */
        
        typedef voronoi_diagram<double> VD;
        VD vd;
        
        // mapping between Voronoi vertices and graph nodes
        typedef std::map<const VD::vertex_type*,size_t> t_vd_vertices;
        t_vd_vertices vd_vertices;
        
        // get boundaries as lines
        MotionPlannerEnv env = this->get_env(island_idx);
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
        
        return graph;
    }
    return this->graphs[island_idx + 1];
}

Point
MotionPlannerEnv::nearest_env_point(const Point &from, const Point &to) const
{
    /*  In order to ensure that the move between 'from' and the initial env point does
        not violate any of the configuration space boundaries, we limit our search to
        the points that satisfy this condition. */
    
    /*  Assume that this method is never called when 'env' contains 'from';
        so 'from' is either inside a hole or outside all contours */
    
    // get the points of the hole containing 'from', if any
    Points pp;
    for (ExPolygons::const_iterator ex = this->env.expolygons.begin(); ex != this->env.expolygons.end(); ++ex) {
        for (Polygons::const_iterator h = ex->holes.begin(); h != ex->holes.end(); ++h) {
            if (h->contains(from)) {
                pp = *h;
            }
        }
        if (!pp.empty()) break;
    }
    
    /*  If 'from' is not inside a hole, it's outside of all contours, so take all
        contours' points */
    if (pp.empty()) {
        for (ExPolygons::const_iterator ex = this->env.expolygons.begin(); ex != this->env.expolygons.end(); ++ex) {
            Points contour_pp = ex->contour;
            pp.insert(pp.end(), contour_pp.begin(), contour_pp.end());
        }
    }
    
    /*  Find the candidate result and check that it doesn't cross too many boundaries. */
    while (pp.size() >= 2) {
        // find the point in pp that is closest to both 'from' and 'to'
        size_t result = from.nearest_waypoint_index(pp, to);
        
        // as we assume 'from' is outside env, any node will require at least one crossing
        if (intersection((Lines)Line(from, pp[result]), this->island).size() > 1) {
            // discard result
            pp.erase(pp.begin() + result);
        } else {
            return pp[result];
        }
    }
    
    // if we're here, return last point if any (better than nothing)
    if (!pp.empty()) {
        return pp.front();
    }
    
    // if we have no points at all, then we have an empty environment and we
    // make this method behave as a no-op (we shouldn't get here by the way)
    return from;
}

void
MotionPlannerGraph::add_edge(size_t from, size_t to, double weight)
{
    // extend adjacency list until this start node
    if (this->adjacency_list.size() < from+1)
        this->adjacency_list.resize(from+1);
    
    this->adjacency_list[from].push_back(neighbor(to, weight));
}

size_t
MotionPlannerGraph::find_node(const Point &point) const
{
    /*
    for (Points::const_iterator p = this->nodes.begin(); p != this->nodes.end(); ++p) {
        if (p->coincides_with(point)) return p - this->nodes.begin();
    }
    */
    return point.nearest_point_index(this->nodes);
}

Polyline
MotionPlannerGraph::shortest_path(size_t from, size_t to)
{
    // this prevents a crash in case for some reason we got here with an empty adjacency list
    if (this->adjacency_list.empty()) return Polyline();
    
    const weight_t max_weight = std::numeric_limits<weight_t>::infinity();
    
    std::vector<weight_t> dist;
    std::vector<node_t> previous;
    {
        // number of nodes
        size_t n = this->adjacency_list.size();
        
        // initialize dist and previous
        dist.clear();
        dist.resize(n, max_weight);
        dist[from] = 0;  // distance from 'from' to itself
        previous.clear();
        previous.resize(n, -1);
        
        // initialize the Q with all nodes
        std::set<node_t> Q;
        for (node_t i = 0; i < n; ++i) Q.insert(i);
        
        while (!Q.empty()) 
        {
            // get node in Q having the minimum dist ('from' in the first loop)
            node_t u;
            {
                double min_dist = -1;
                for (std::set<node_t>::const_iterator n = Q.begin(); n != Q.end(); ++n) {
                    if (dist[*n] < min_dist || min_dist == -1) {
                        u = *n;
                        min_dist = dist[*n];
                    }
                }
            }
            Q.erase(u);
            
            // stop searching if we reached our destination
            if (u == to) break;
            
            // Visit each edge starting from node u
            const std::vector<neighbor> &neighbors = this->adjacency_list[u];
            for (std::vector<neighbor>::const_iterator neighbor_iter = neighbors.begin();
                 neighbor_iter != neighbors.end();
                 ++neighbor_iter)
            {
                // neighbor node is v
                node_t v = neighbor_iter->target;
                
                // skip if we already visited this
                if (Q.find(v) == Q.end()) continue;
                
                // calculate total distance
                weight_t alt = dist[u] + neighbor_iter->weight;
                
                // if total distance through u is shorter than the previous
                // distance (if any) between 'from' and 'v', replace it
                if (alt < dist[v]) {
                    dist[v]     = alt;
                    previous[v] = u;
                }

            }
        }
    }
    
    Polyline polyline;
    for (node_t vertex = to; vertex != -1; vertex = previous[vertex])
        polyline.points.push_back(this->nodes[vertex]);
    polyline.points.push_back(this->nodes[from]);
    polyline.reverse();
    return polyline;
}

}
