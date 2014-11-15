#include "Geometry.hpp"
#include "ExPolygon.hpp"
#include "Line.hpp"
#include "PolylineCollection.hpp"
#include "clipper.hpp"
#include <algorithm>
#include <cmath>
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
convex_hull(Points points, Polygon* hull)
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

void
convex_hull(const Polygons &polygons, Polygon* hull)
{
    Points pp;
    for (Polygons::const_iterator p = polygons.begin(); p != polygons.end(); ++p) {
        pp.insert(pp.end(), p->points.begin(), p->points.end());
    }
    convex_hull(pp, hull);
}

/* accepts an arrayref of points and returns a list of indices
   according to a nearest-neighbor walk */
void
chained_path(const Points &points, std::vector<Points::size_type> &retval, Point start_near)
{
    PointConstPtrs my_points;
    std::map<const Point*,Points::size_type> indices;
    my_points.reserve(points.size());
    for (Points::const_iterator it = points.begin(); it != points.end(); ++it) {
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
chained_path(const Points &points, std::vector<Points::size_type> &retval)
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

bool
directions_parallel(double angle1, double angle2, double max_diff)
{
    double diff = fabs(angle1 - angle2);
    max_diff += EPSILON;
    return diff < max_diff || fabs(diff - PI) < max_diff;
}

template<class T>
bool
contains_point(const std::vector<T> &vector, const Point &point)
{
    for (typename std::vector<T>::const_iterator it = vector.begin(); it != vector.end(); ++it) {
        if (it->contains_point(point)) return true;
    }
    return false;
}
template bool contains_point(const ExPolygons &vector, const Point &point);

double
rad2deg(double angle)
{
    return angle / PI * 180.0;
}

double
rad2deg_dir(double angle)
{
    angle = (angle < PI) ? (-angle + PI/2.0) : (angle + PI/2.0);
    if (angle < 0) angle += PI;
    return rad2deg(angle);
}

double
deg2rad(double angle)
{
    return PI * angle / 180.0;
}

Line
MedialAxis::edge_to_line(const VD::edge_type &edge) const
{
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
    
    /*
    // DEBUG: dump all Voronoi edges
    {
        for (VD::const_edge_iterator edge = this->vd.edges().begin(); edge != this->vd.edges().end(); ++edge) {
            if (edge->is_infinite()) continue;
            
            Polyline polyline;
            polyline.points.push_back(Point( edge->vertex0()->x(), edge->vertex0()->y() ));
            polyline.points.push_back(Point( edge->vertex1()->x(), edge->vertex1()->y() ));
            polylines->push_back(polyline);
        }
        return;
    }
    */
    
    // collect valid edges (i.e. prune those not belonging to MAT)
    // note: this keeps twins, so it contains twice the number of the valid edges
    this->edges.clear();
    for (VD::const_edge_iterator edge = this->vd.edges().begin(); edge != this->vd.edges().end(); ++edge) {
        // if we only process segments representing closed loops, none if the
        // infinite edges (if any) would be part of our MAT anyway
        if (edge->is_secondary() || edge->is_infinite()) continue;
        this->edges.insert(&*edge);
    }
    
    // count valid segments for each vertex
    std::map< const VD::vertex_type*,std::set<const VD::edge_type*> > vertex_edges;
    std::set<const VD::vertex_type*> entry_nodes;
    for (VD::const_vertex_iterator vertex = this->vd.vertices().begin(); vertex != this->vd.vertices().end(); ++vertex) {
        // get a reference to the list of valid edges originating from this vertex
        std::set<const VD::edge_type*>& edges = vertex_edges[&*vertex];
        
        // get one random edge originating from this vertex
        const VD::edge_type* edge = vertex->incident_edge();
        do {
            if (this->edges.count(edge) > 0)    // only count valid edges
                edges.insert(edge);
            edge = edge->rot_next();            // next edge originating from this vertex
        } while (edge != vertex->incident_edge());
        
        // if there's only one edge starting at this vertex then it's a leaf
        size_t edge_count = edges.size();
        if (edge_count == 1) {
            entry_nodes.insert(&*vertex);
        }
    }
    
    // prune recursively
    while (!entry_nodes.empty()) {
        // get a random entry node
        const VD::vertex_type* v = *entry_nodes.begin();
    
        // get edge starting from v
        assert(!vertex_edges[v].empty());
        const VD::edge_type* edge = *vertex_edges[v].begin();
        
        if (!this->is_valid_edge(*edge)) {
            // if edge is not valid, erase it from edge list
            (void)this->edges.erase(edge);
            (void)this->edges.erase(edge->twin());
            
            // decrement edge counters for the affected nodes
            const VD::vertex_type* v1 = edge->vertex1();
            (void)vertex_edges[v].erase(edge);
            (void)vertex_edges[v1].erase(edge->twin());
            
            // also, check whether the end vertex is a new leaf
            if (vertex_edges[v1].size() == 1) {
                entry_nodes.insert(v1);
            } else if (vertex_edges[v1].empty()) {
                entry_nodes.erase(v1);
            }
        }
        
        // remove node from the set to prevent it from being visited again
        entry_nodes.erase(v);
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
        
        // append polyline to result if it's not too small
        if (polyline.length() > this->max_width)
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
    /* If the cells sharing this edge have a common vertex, we're not interested
       in this edge. Why? Because it means that the edge lies on the bisector of
       two contiguous input lines and it was included in the Voronoi graph because
       it's the locus of centers of circles tangent to both vertices. Due to the 
       "thin" nature of our input, these edges will be very short and not part of
       our wanted output. */
    
    const VD::cell_type &cell1 = *edge.cell();
    const VD::cell_type &cell2 = *edge.twin()->cell();
    if (cell1.contains_segment() && cell2.contains_segment()) {
        Line segment1 = this->retrieve_segment(cell1);
        Line segment2 = this->retrieve_segment(cell2);
        if (segment1.a == segment2.b || segment1.b == segment2.a) return false;
        
        // calculate relative angle between the two boundary segments
        double angle = fabs(segment2.orientation() - segment1.orientation());
        
        // fabs(angle) ranges from 0 (collinear, same direction) to PI (collinear, opposite direction)
        // we're interested only in segments close to the second case (facing segments)
        // so we allow some tolerance (say, 30°)
        if (angle < PI*2/3 ) {
            return false;
        }
        
        // each vertex is equidistant to both cell segments
        // but such distance might differ between the two vertices;
        // in this case it means the shape is getting narrow (like a corner)
        // and we might need to skip the edge since it's not really part of
        // our skeleton
        Point v0( edge.vertex0()->x(), edge.vertex0()->y() );
        Point v1( edge.vertex1()->x(), edge.vertex1()->y() );
        double dist0 = v0.distance_to(segment1);
        double dist1 = v1.distance_to(segment1);
        
        /*
        double diff = fabs(dist1 - dist0);
        double dist_between_segments1 = segment1.a.distance_to(segment2);
        double dist_between_segments2 = segment1.b.distance_to(segment2);
        printf("w = %f/%f, dist0 = %f, dist1 = %f, diff = %f, seg1len = %f, seg2len = %f, edgelen = %f, s2s = %f / %f\n",
            unscale(this->max_width), unscale(this->min_width),
            unscale(dist0), unscale(dist1), unscale(diff),
            unscale(segment1.length()), unscale(segment2.length()),
            unscale(this->edge_to_line(edge).length()),
            unscale(dist_between_segments1), unscale(dist_between_segments2)
            );
        */
        
        // if this segment is the centerline for a very thin area, we might want to skip it
        // in case the area is too thin
        if (dist0 < this->min_width/2 || dist1 < this->min_width/2) {
            //printf(" => too thin, skipping\n");
            return false;
        }
        
        /*
        // if distance between this edge and the thin area boundary is greater
        // than half the max width, then it's not a true medial axis segment
        if (dist1 > this->width*2) {
            printf(" => too fat, skipping\n");
            //return false;
        }
        */
        
        return true;
    }
    
    return false;
}

Line
MedialAxis::retrieve_segment(const VD::cell_type& cell) const
{
    VD::cell_type::source_index_type index = cell.source_index() - this->points.size();
    return this->lines[index];
}

} }
