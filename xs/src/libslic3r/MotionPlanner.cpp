#include "BoundingBox.hpp"
#include "MotionPlanner.hpp"
#include <limits> // for numeric_limits

#include "boost/polygon/voronoi.hpp"
using boost::polygon::voronoi_builder;
using boost::polygon::voronoi_diagram;

namespace Slic3r {

MotionPlanner::MotionPlanner(const ExPolygons &islands)
    : islands(islands), initialized(false)
{}

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
    
    ExPolygons expp;
    for (ExPolygons::const_iterator island = this->islands.begin(); island != this->islands.end(); ++island) {
        island->simplify(SCALED_EPSILON, expp);
    }
    this->islands = expp;
    
    // loop through islands in order to create inner expolygons and collect their contours
    this->inner.reserve(this->islands.size());
    Polygons outer_holes;
    for (ExPolygons::const_iterator island = this->islands.begin(); island != this->islands.end(); ++island) {
        this->inner.push_back(ExPolygonCollection());
        offset(*island, &this->inner.back().expolygons, -MP_INNER_MARGIN);
        
        outer_holes.push_back(island->contour);
    }
    
    // grow island contours in order to prepare holes of the outer environment
    // This is actually wrong because it might merge contours that are close,
    // thus confusing the island check in shortest_path() below
    //offset(outer_holes, &outer_holes, +MP_OUTER_MARGIN);
    
    // generate outer contour as bounding box of everything
    Points points;
    for (Polygons::const_iterator contour = outer_holes.begin(); contour != outer_holes.end(); ++contour)
        points.insert(points.end(), contour->points.begin(), contour->points.end());
    BoundingBox bb(points);
    
    // grow outer contour
    Polygons contour;
    offset(bb.polygon(), &contour, +MP_OUTER_MARGIN);
    assert(contour.size() == 1);
    
    // make expolygon for outer environment
    ExPolygons outer;
    diff(contour, outer_holes, &outer);
    assert(outer.size() == 1);
    this->outer = outer.front();
    
    this->graphs.resize(this->islands.size() + 1, NULL);
    this->initialized = true;
}

void
MotionPlanner::shortest_path(const Point &from, const Point &to, Polyline* polyline)
{
    if (!this->initialized) this->initialize();
    
    if (this->islands.empty()) {
        polyline->points.push_back(from);
        polyline->points.push_back(to);
        return;
    }
    
    // Are both points in the same island?
    int island_idx = -1;
    for (ExPolygons::const_iterator island = this->islands.begin(); island != this->islands.end(); ++island) {
        if (island->contains_point(from) && island->contains_point(to)) {
            // since both points are in the same island, is a direct move possible?
            // if so, we avoid generating the visibility environment
            if (island->contains_line(Line(from, to))) {
                polyline->points.push_back(from);
                polyline->points.push_back(to);
                return;
            }
            island_idx = island - this->islands.begin();
            break;
        }
    }
    
    // Now check whether points are inside the environment.
    Point inner_from    = from;
    Point inner_to      = to;
    bool from_is_inside, to_is_inside;
    if (island_idx == -1) {
        if (!(from_is_inside = this->outer.contains_point(from))) {
            // Find the closest inner point to start from.
            from.nearest_point(this->outer, &inner_from);
        }
        if (!(to_is_inside = this->outer.contains_point(to))) {
            // Find the closest inner point to start from.
            to.nearest_point(this->outer, &inner_to);
        }
    } else {
        if (!(from_is_inside = this->inner[island_idx].contains_point(from))) {
            // Find the closest inner point to start from.
            from.nearest_point(this->inner[island_idx], &inner_from);
        }
        if (!(to_is_inside = this->inner[island_idx].contains_point(to))) {
            // Find the closest inner point to start from.
            to.nearest_point(this->inner[island_idx], &inner_to);
        }
    }
    
    // perform actual path search
    MotionPlannerGraph* graph = this->init_graph(island_idx);
    graph->shortest_path(graph->find_node(inner_from), graph->find_node(inner_to), polyline);
    
    polyline->points.insert(polyline->points.begin(), from);
    polyline->points.push_back(to);
}

MotionPlannerGraph*
MotionPlanner::init_graph(int island_idx)
{
    if (this->graphs[island_idx + 1] == NULL) {
        Polygons pp;
        if (island_idx == -1) {
            pp = this->outer;
        } else {
            pp = this->inner[island_idx];
        }
        
        MotionPlannerGraph* graph = this->graphs[island_idx + 1] = new MotionPlannerGraph();
        
        // add polygon boundaries as edges
        size_t node_idx = 0;
        Lines lines;
        for (Polygons::const_iterator polygon = pp.begin(); polygon != pp.end(); ++polygon) {
            graph->nodes.push_back(polygon->points.back());
            node_idx++;
            for (Points::const_iterator p = polygon->points.begin(); p != polygon->points.end(); ++p) {
                graph->nodes.push_back(*p);
                double dist = graph->nodes[node_idx-1].distance_to(*p);
                graph->add_edge(node_idx-1, node_idx, dist);
                graph->add_edge(node_idx, node_idx-1, dist);
                node_idx++;
            }
            polygon->lines(&lines);
        }
        
        // add Voronoi edges as internal edges
        {
            typedef voronoi_diagram<double> VD;
            typedef std::map<const VD::vertex_type*,size_t> t_vd_vertices;
            VD vd;
            t_vd_vertices vd_vertices;
            
            boost::polygon::construct_voronoi(lines.begin(), lines.end(), &vd);
            for (VD::const_edge_iterator edge = vd.edges().begin(); edge != vd.edges().end(); ++edge) {
                if (edge->is_infinite()) continue;
                
                const VD::vertex_type* v0 = edge->vertex0();
                const VD::vertex_type* v1 = edge->vertex1();
                Point p0 = Point(v0->x(), v0->y());
                Point p1 = Point(v1->x(), v1->y());
                // contains_point() should probably be faster than contains_line(),
                // and should it fail on any boundary points it's not a big problem
                if (island_idx == -1) {
                    if (!this->outer.contains_point(p0) || !this->outer.contains_point(p1)) continue;
                } else {
                    if (!this->inner[island_idx].contains_point(p0) || !this->inner[island_idx].contains_point(p1)) continue;
                }
                
                t_vd_vertices::const_iterator i_v0 = vd_vertices.find(v0);
                size_t v0_idx;
                if (i_v0 == vd_vertices.end()) {
                    graph->nodes.push_back(p0);
                    v0_idx = node_idx;
                    vd_vertices[v0] = node_idx;
                    node_idx++;
                } else {
                    v0_idx = i_v0->second;
                }
                
                t_vd_vertices::const_iterator i_v1 = vd_vertices.find(v1);
                size_t v1_idx;
                if (i_v1 == vd_vertices.end()) {
                    graph->nodes.push_back(p1);
                    v1_idx = node_idx;
                    vd_vertices[v1] = node_idx;
                    node_idx++;
                } else {
                    v1_idx = i_v1->second;
                }
                
                double dist = graph->nodes[v0_idx].distance_to(graph->nodes[v1_idx]);
                graph->add_edge(v0_idx, v1_idx, dist);
            }
        }
        
        return graph;
    }
    return this->graphs[island_idx + 1];
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

void
MotionPlannerGraph::shortest_path(size_t from, size_t to, Polyline* polyline)
{
    // this prevents a crash in case for some reason we got here with an empty adjacency list
    if (this->adjacency_list.empty()) return;
    
    const weight_t max_weight = std::numeric_limits<weight_t>::infinity();
    
    std::vector<weight_t> min_distance;
    std::vector<node_t> previous;
    {
        int n = this->adjacency_list.size();
        min_distance.clear();
        min_distance.resize(n, max_weight);
        min_distance[from] = 0;
        previous.clear();
        previous.resize(n, -1);
        std::set<std::pair<weight_t, node_t> > vertex_queue;
        vertex_queue.insert(std::make_pair(min_distance[from], from));
 
        while (!vertex_queue.empty()) 
        {
            weight_t dist = vertex_queue.begin()->first;
            node_t u = vertex_queue.begin()->second;
            vertex_queue.erase(vertex_queue.begin());
 
            // Visit each edge exiting u
            const std::vector<neighbor> &neighbors = this->adjacency_list[u];
            for (std::vector<neighbor>::const_iterator neighbor_iter = neighbors.begin();
                 neighbor_iter != neighbors.end();
                 neighbor_iter++)
            {
                node_t v = neighbor_iter->target;
                weight_t weight = neighbor_iter->weight;
                weight_t distance_through_u = dist + weight;
                if (distance_through_u < min_distance[v]) {
                    vertex_queue.erase(std::make_pair(min_distance[v], v));
                    min_distance[v] = distance_through_u;
                    previous[v] = u;
                    vertex_queue.insert(std::make_pair(min_distance[v], v));
                }

            }
        }
    }
    
    for (node_t vertex = to; vertex != -1; vertex = previous[vertex])
        polyline->points.push_back(this->nodes[vertex]);
    polyline->reverse();
}

#ifdef SLIC3RXS
REGISTER_CLASS(MotionPlanner, "MotionPlanner");
#endif

}
