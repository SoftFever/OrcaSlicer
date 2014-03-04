#include "Geometry.hpp"
#include "Line.hpp"
#include "PolylineCollection.hpp"
#include "clipper.hpp"
#include <algorithm>
#include <list>
#include <map>
#include <set>
#include <vector>

#ifdef SLIC3R_DEBUG
#include "SVG.hpp"
#endif

using namespace boost::polygon;  // provides also high() and low()

namespace Slic3r { namespace Geometry {

static bool
sort_points (Point a, Point b)
{
    return (a.x < b.x) || (a.x == b.x && a.y < b.y);
}

/* This implementation is based on Andrew's monotone chain 2D convex hull algorithm */
void
convex_hull(Points &points, Polygon* hull)
{
    assert(points.size() >= 3);
    // sort input points
    std::sort(points.begin(), points.end(), sort_points);
    
    int n = points.size(), k = 0;
    hull->points.resize(2*n);

    // Build lower hull
    for (int i = 0; i < n; i++) {
        while (k >= 2 && points[i].ccw(hull->points[k-2], hull->points[k-1]) <= 0) k--;
        hull->points[k++] = points[i];
    }

    // Build upper hull
    for (int i = n-2, t = k+1; i >= 0; i--) {
        while (k >= t && points[i].ccw(hull->points[k-2], hull->points[k-1]) <= 0) k--;
        hull->points[k++] = points[i];
    }

    hull->points.resize(k);
    
    assert( hull->points.front().coincides_with(hull->points.back()) );
    hull->points.pop_back();
}

/* accepts an arrayref of points and returns a list of indices
   according to a nearest-neighbor walk */
void
chained_path(Points &points, std::vector<Points::size_type> &retval, Point start_near)
{
    PointPtrs my_points;
    std::map<Point*,Points::size_type> indices;
    my_points.reserve(points.size());
    for (Points::iterator it = points.begin(); it != points.end(); ++it) {
        my_points.push_back(&*it);
        indices[&*it] = it - points.begin();
    }
    
    retval.reserve(points.size());
    while (!my_points.empty()) {
        Points::size_type idx = start_near.nearest_point_index(my_points);
        start_near = *my_points[idx];
        retval.push_back(indices[ my_points[idx] ]);
        my_points.erase(my_points.begin() + idx);
    }
}

void
chained_path(Points &points, std::vector<Points::size_type> &retval)
{
    if (points.empty()) return;  // can't call front() on empty vector
    chained_path(points, retval, points.front());
}

/* retval and items must be different containers */
template<class T>
void
chained_path_items(Points &points, T &items, T &retval)
{
    std::vector<Points::size_type> indices;
    chained_path(points, indices);
    for (std::vector<Points::size_type>::const_iterator it = indices.begin(); it != indices.end(); ++it)
        retval.push_back(items[*it]);
}
template void chained_path_items(Points &points, ClipperLib::PolyNodes &items, ClipperLib::PolyNodes &retval);

Line
MedialAxis::edge_to_line(const VD::edge_type &edge) {
    Line line;
    line.a.x = edge.vertex0()->x();
    line.a.y = edge.vertex0()->y();
    line.b.x = edge.vertex1()->x();
    line.b.y = edge.vertex1()->y();
    return line;
}

void
MedialAxis::build(Polylines* polylines)
{
    /*
    // build bounding box (we use it for clipping infinite segments)
    // --> we have no infinite segments
    this->bb = BoundingBox(this->lines);
    */
    
    construct_voronoi(this->lines.begin(), this->lines.end(), &this->vd);
    
    // collect valid edges (i.e. prune those not belonging to MAT)
    // note: this keeps twins, so it contains twice the number of the valid edges
    this->edges.clear();
    for (VD::const_edge_iterator edge = this->vd.edges().begin(); edge != this->vd.edges().end(); ++edge) {
        if (this->is_valid_edge(*edge)) this->edges.insert(&*edge);
    }
    
    // count valid segments for each vertex
    std::map< const VD::vertex_type*,std::list<const VD::edge_type*> > vertex_edges;
    std::list<const VD::vertex_type*> entry_nodes;
    for (VD::const_vertex_iterator vertex = this->vd.vertices().begin(); vertex != this->vd.vertices().end(); ++vertex) {
        // get a reference to the list of valid edges originating from this vertex
        std::list<const VD::edge_type*>& edges = vertex_edges[&*vertex];
        
        // get one random edge originating from this vertex
        const VD::edge_type* edge = vertex->incident_edge();
        do {
            if (this->edges.count(edge) > 0)    // only count valid edges
                edges.push_back(edge);
            edge = edge->rot_next();            // next edge originating from this vertex
        } while (edge != vertex->incident_edge());
        
        // if there's only one edge starting at this vertex then it's a leaf
        if (edges.size() == 1) entry_nodes.push_back(&*vertex);
    }
    
    // iterate through the leafs to prune short branches
    for (std::list<const VD::vertex_type*>::const_iterator vertex = entry_nodes.begin(); vertex != entry_nodes.end(); ++vertex) {
        const VD::vertex_type* v = *vertex;
        
        // start a polyline from this vertex
        Polyline polyline;
        polyline.points.push_back(Point(v->x(), v->y()));
        
        // keep track of visited edges to prevent infinite loops
        std::set<const VD::edge_type*> visited_edges;
        
        do {
            // get edge starting from v
            const VD::edge_type* edge = vertex_edges[v].front();
            
            // if we picked the edge going backwards (thus the twin of the previous edge)
            if (visited_edges.count(edge->twin()) > 0) {
                edge = vertex_edges[v].back();
            }
            
            // avoid getting twice on the same edge
            if (visited_edges.count(edge) > 0) break;
            visited_edges.insert(edge);
            
            // get ending vertex for this edge and append it to the polyline
            v = edge->vertex1();
            polyline.points.push_back(Point( v->x(), v->y() ));
            
            // if two edges start at this vertex (one forward one backwards) then
            // it's not branching and we can go on
        } while (vertex_edges[v].size() == 2);
        
        // if this branch is too short, invalidate all of its edges so that 
        // they will be ignored when building actual polylines in the loop below
        if (polyline.length() < this->width) {
            for (std::set<const VD::edge_type*>::const_iterator edge = visited_edges.begin(); edge != visited_edges.end(); ++edge) {
                (void)this->edges.erase(*edge);
                (void)this->edges.erase((*edge)->twin());
            }
        }
    }
    
    // iterate through the valid edges to build polylines
    while (!this->edges.empty()) {
        const VD::edge_type& edge = **this->edges.begin();
        
        // start a polyline
        Polyline polyline;
        polyline.points.push_back(Point( edge.vertex0()->x(), edge.vertex0()->y() ));
        polyline.points.push_back(Point( edge.vertex1()->x(), edge.vertex1()->y() ));
        
        // remove this edge and its twin from the available edges
        (void)this->edges.erase(&edge);
        (void)this->edges.erase(edge.twin());
        
        // get next points
        this->process_edge_neighbors(edge, &polyline.points);
        
        // get previous points
        Points pp;
        this->process_edge_neighbors(*edge.twin(), &pp);
        polyline.points.insert(polyline.points.begin(), pp.rbegin(), pp.rend());
        
        // append polyline to result
        polylines->push_back(polyline);
    }
}

void
MedialAxis::process_edge_neighbors(const VD::edge_type& edge, Points* points)
{
    // Since rot_next() works on the edge starting point but we want
    // to find neighbors on the ending point, we just swap edge with
    // its twin.
    const VD::edge_type& twin = *edge.twin();
    
    // count neighbors for this edge
    std::vector<const VD::edge_type*> neighbors;
    for (const VD::edge_type* neighbor = twin.rot_next(); neighbor != &twin; neighbor = neighbor->rot_next()) {
        if (this->edges.count(neighbor) > 0) neighbors.push_back(neighbor);
    }
    
    // if we have a single neighbor then we can continue recursively
    if (neighbors.size() == 1) {
        const VD::edge_type& neighbor = *neighbors.front();
        points->push_back(Point( neighbor.vertex1()->x(), neighbor.vertex1()->y() ));
        (void)this->edges.erase(&neighbor);
        (void)this->edges.erase(neighbor.twin());
        this->process_edge_neighbors(neighbor, points);
    }
}

bool
MedialAxis::is_valid_edge(const VD::edge_type& edge) const
{
    // if we only process segments representing closed loops, none if the
    // infinite edges (if any) would be part of our MAT anyway
    if (edge.is_secondary() || edge.is_infinite()) return false;
    
    /* If the cells sharing this edge have a common vertex, we're not interested
       in this edge. Why? Because it means that the edge lies on the bisector of
       two contiguous input lines and it was included in the Voronoi graph because
       it's the locus of centers of circles tangent to both vertices. Due to the 
       "thin" nature of our input, these edges will be very short and not part of
       our wanted output. The best way would be to just filter out the edges that
       are not the locus of the maximally inscribed disks (requirement of MAT)
       but I don't know how to do it. Maybe we could check the relative angle of
       the two segments (we are only interested in facing segments). */
    
    const VD::cell_type &cell1 = *edge.cell();
    const VD::cell_type &cell2 = *edge.twin()->cell();
    if (cell1.contains_segment() && cell2.contains_segment()) {
        Line segment1 = this->retrieve_segment(cell1);
        Line segment2 = this->retrieve_segment(cell2);
        if (segment1.a == segment2.b || segment1.b == segment2.a) return false;
        if (fabs(segment1.atan2_() - segment2.atan2_()) < PI/3) return false;
        
        // we can assume that distance between any of the vertices and any of the cell segments
        // is about the same
        Point p0( edge.vertex0()->x(), edge.vertex0()->y() );
        double dist = p0.distance_to(segment1);
        
        // if distance between this edge and the thin area boundary is greater
        // than half the max width, then it's not a true medial axis segment
        if (dist > this->width/2) return false;
    }
    
    return true;
}

Line
MedialAxis::retrieve_segment(const VD::cell_type& cell) const
{
    VD::cell_type::source_index_type index = cell.source_index() - this->points.size();
    return this->lines[index];
}

} }
