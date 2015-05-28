#include "Geometry.hpp"
#include "ClipperUtils.hpp"
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
Polygon
convex_hull(Points points)
{
    assert(points.size() >= 3);
    // sort input points
    std::sort(points.begin(), points.end(), sort_points);
    
    int n = points.size(), k = 0;
    Polygon hull;
    hull.points.resize(2*n);

    // Build lower hull
    for (int i = 0; i < n; i++) {
        while (k >= 2 && points[i].ccw(hull.points[k-2], hull.points[k-1]) <= 0) k--;
        hull.points[k++] = points[i];
    }

    // Build upper hull
    for (int i = n-2, t = k+1; i >= 0; i--) {
        while (k >= t && points[i].ccw(hull.points[k-2], hull.points[k-1]) <= 0) k--;
        hull.points[k++] = points[i];
    }

    hull.points.resize(k);
    
    assert( hull.points.front().coincides_with(hull.points.back()) );
    hull.points.pop_back();
    
    return hull;
}

Polygon
convex_hull(const Polygons &polygons)
{
    Points pp;
    for (Polygons::const_iterator p = polygons.begin(); p != polygons.end(); ++p) {
        pp.insert(pp.end(), p->points.begin(), p->points.end());
    }
    return convex_hull(pp);
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
contains(const std::vector<T> &vector, const Point &point)
{
    for (typename std::vector<T>::const_iterator it = vector.begin(); it != vector.end(); ++it) {
        if (it->contains(point)) return true;
    }
    return false;
}
template bool contains(const ExPolygons &vector, const Point &point);

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

void
simplify_polygons(const Polygons &polygons, double tolerance, Polygons* retval)
{
    Polygons pp;
    for (Polygons::const_iterator it = polygons.begin(); it != polygons.end(); ++it) {
        Polygon p = *it;
        p.points.push_back(p.points.front());
        p.points = MultiPoint::_douglas_peucker(p.points, tolerance);
        p.points.pop_back();
        pp.push_back(p);
    }
    Slic3r::simplify_polygons(pp, retval);
}

double
linint(double value, double oldmin, double oldmax, double newmin, double newmax)
{
    return (value - oldmin) * (newmax - newmin) / (oldmax - oldmin) + newmin;
}

Pointfs
arrange(size_t total_parts, Pointf part, coordf_t dist, const BoundingBoxf &bb)
{
    // use actual part size (the largest) plus separation distance (half on each side) in spacing algorithm
    part.x += dist;
    part.y += dist;
    
    Pointf area;
    if (bb.defined) {
        area = bb.size();
    } else {
        // bogus area size, large enough not to trigger the error below
        area.x = part.x * total_parts;
        area.y = part.y * total_parts;
    }
    
    // this is how many cells we have available into which to put parts
    size_t cellw = floor((area.x + dist) / part.x);
    size_t cellh = floor((area.x + dist) / part.x);
    
    if (total_parts > (cellw * cellh))
        CONFESS("%zu parts won't fit in your print area!\n", total_parts);
    
    // total space used by cells
    Pointf cells(cellw * part.x, cellh * part.y);
    
    // bounding box of total space used by cells
    BoundingBoxf cells_bb;
    cells_bb.merge(Pointf(0,0)); // min
    cells_bb.merge(cells);  // max
    
    // center bounding box to area
    cells_bb.translate(
        -(area.x - cells.x) / 2,
        -(area.y - cells.y) / 2
    );
    
    // list of cells, sorted by distance from center
    std::vector<ArrangeItemIndex> cellsorder;
    
    // work out distance for all cells, sort into list
    for (size_t i = 0; i <= cellw-1; ++i) {
        for (size_t j = 0; j <= cellh-1; ++j) {
            coordf_t cx = linint(i + 0.5, 0, cellw, cells_bb.min.x, cells_bb.max.x);
            coordf_t cy = linint(j + 0.5, 0, cellh, cells_bb.max.y, cells_bb.min.y);
            
            coordf_t xd = fabs((area.x / 2) - cx);
            coordf_t yd = fabs((area.y / 2) - cy);
            
            ArrangeItem c;
            c.pos.x = cx;
            c.pos.y = cy;
            c.index_x = i;
            c.index_y = j;
            c.dist = xd * xd + yd * yd - fabs((cellw / 2) - (i + 0.5));
            
            // binary insertion sort
            {
                coordf_t index = c.dist;
                size_t low = 0;
                size_t high = cellsorder.size();
                while (low < high) {
                    size_t mid = (low + ((high - low) / 2)) | 0;
                    coordf_t midval = cellsorder[mid].index;
                    
                    if (midval < index) {
                        low = mid + 1;
                    } else if (midval > index) {
                        high = mid;
                    } else {
                        cellsorder.insert(cellsorder.begin() + mid, ArrangeItemIndex(index, c));
                        goto ENDSORT;
                    }
                }
                cellsorder.insert(cellsorder.begin() + low, ArrangeItemIndex(index, c));
            }
            ENDSORT: true;
        }
    }
    
    // the extents of cells actually used by objects
    coordf_t lx = 0;
    coordf_t ty = 0;
    coordf_t rx = 0;
    coordf_t by = 0;

    // now find cells actually used by objects, map out the extents so we can position correctly
    for (size_t i = 1; i <= total_parts; ++i) {
        ArrangeItemIndex c = cellsorder[i - 1];
        coordf_t cx = c.item.index_x;
        coordf_t cy = c.item.index_y;
        if (i == 1) {
            lx = rx = cx;
            ty = by = cy;
        } else {
            if (cx > rx) rx = cx;
            if (cx < lx) lx = cx;
            if (cy > by) by = cy;
            if (cy < ty) ty = cy;
        }
    }
    // now we actually place objects into cells, positioned such that the left and bottom borders are at 0
    Pointfs positions;
    for (size_t i = 1; i <= total_parts; ++i) {
        ArrangeItemIndex c = cellsorder.front();
        cellsorder.erase(cellsorder.begin());
        coordf_t cx = c.item.index_x - lx;
        coordf_t cy = c.item.index_y - ty;
        
        positions.push_back(Pointf(cx * part.x, cy * part.y));
    }
    
    if (bb.defined) {
        for (Pointfs::iterator p = positions.begin(); p != positions.end(); ++p) {
            p->x += bb.min.x;
            p->y += bb.min.y;
        }
    }
    return positions;
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
    
    typedef const VD::vertex_type vert_t;
    typedef const VD::edge_type   edge_t;
    
    // collect valid edges (i.e. prune those not belonging to MAT)
    // note: this keeps twins, so it inserts twice the number of the valid edges
    this->edges.clear();
    for (VD::const_edge_iterator edge = this->vd.edges().begin(); edge != this->vd.edges().end(); ++edge) {
        // if we only process segments representing closed loops, none if the
        // infinite edges (if any) would be part of our MAT anyway
        if (edge->is_secondary() || edge->is_infinite()) continue;
        this->edges.insert(&*edge);
    }
    
    // count valid segments for each vertex
    std::map< vert_t*,std::set<edge_t*> > vertex_edges;  // collects edges connected for each vertex
    std::set<vert_t*> startpoints;                       // collects all vertices having a single starting edge
    for (VD::const_vertex_iterator it = this->vd.vertices().begin(); it != this->vd.vertices().end(); ++it) {
        vert_t* vertex = &*it;
        
        // loop through all edges originating from this vertex
        // starting from a random one
        edge_t* edge = vertex->incident_edge();
        do {
            // if this edge was not pruned by our filter above,
            // add it to vertex_edges
            if (this->edges.count(edge) > 0)
                vertex_edges[vertex].insert(edge);
            
            // continue looping next edge originating from this vertex
            edge = edge->rot_next();
        } while (edge != vertex->incident_edge());
        
        // if there's only one edge starting at this vertex then it's an endpoint
        if (vertex_edges[vertex].size() == 1) {
            startpoints.insert(vertex);
        }
    }
    
    // prune startpoints recursively if extreme segments are not valid
    while (!startpoints.empty()) {
        // get a random entry node
        vert_t* v = *startpoints.begin();
    
        // get edge starting from v
        assert(vertex_edges[v].size() == 1);
        edge_t* edge = *vertex_edges[v].begin();
        
        if (!this->is_valid_edge(*edge)) {
            // if edge is not valid, erase it and its twin from edge list
            (void)this->edges.erase(edge);
            (void)this->edges.erase(edge->twin());
            
            // decrement edge counters for the affected nodes
            vert_t* v1 = edge->vertex1();
            (void)vertex_edges[v].erase(edge);
            (void)vertex_edges[v1].erase(edge->twin());
            
            // also, check whether the end vertex is a new leaf
            if (vertex_edges[v1].size() == 1) {
                startpoints.insert(v1);
            } else if (vertex_edges[v1].empty()) {
                startpoints.erase(v1);
            }
        }
        
        // remove node from the set to prevent it from being visited again
        startpoints.erase(v);
    }
    
    // iterate through the valid edges to build polylines
    while (!this->edges.empty()) {
        edge_t &edge = **this->edges.begin();
        
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
        {
            Points pp;
            this->process_edge_neighbors(*edge.twin(), &pp);
            polyline.points.insert(polyline.points.begin(), pp.rbegin(), pp.rend());
        }
        
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
    /* If the cells sharing this edge have a common vertex, we're not interested
       in this edge. Why? Because it means that the edge lies on the bisector of
       two contiguous input lines and it was included in the Voronoi graph because
       it's the locus of centers of circles tangent to both vertices. Due to the 
       "thin" nature of our input, these edges will be very short and not part of
       our wanted output. */
    
    // retrieve the original line segments which generated the edge we're checking
    const VD::cell_type &cell1 = *edge.cell();
    const VD::cell_type &cell2 = *edge.twin()->cell();
    if (!cell1.contains_segment() || !cell2.contains_segment()) return false;
    const Line &segment1 = this->retrieve_segment(cell1);
    const Line &segment2 = this->retrieve_segment(cell2);
    
    // calculate the relative angle between the two boundary segments
    double angle = fabs(segment2.orientation() - segment1.orientation());
    
    // fabs(angle) ranges from 0 (collinear, same direction) to PI (collinear, opposite direction)
    // we're interested only in segments close to the second case (facing segments)
    // so we allow some tolerance.
    // this filter ensures that we're dealing with a narrow/oriented area (longer than thick)
    if (fabs(angle - PI) > PI/5) {
        return false;
    }
    
    // each edge vertex is equidistant to both cell segments
    // but such distance might differ between the two vertices;
    // in this case it means the shape is getting narrow (like a corner)
    // and we might need to skip the edge since it's not really part of
    // our skeleton
    
    // get perpendicular distance of each edge vertex to the segment(s)
    double dist0 = segment1.a.distance_to(segment2.b);
    double dist1 = segment1.b.distance_to(segment2.a);
    
    /*
    Line line = this->edge_to_line(edge);
    double diff = fabs(dist1 - dist0);
    double dist_between_segments1 = segment1.a.distance_to(segment2);
    double dist_between_segments2 = segment1.b.distance_to(segment2);
    printf("w = %f/%f, dist0 = %f, dist1 = %f, diff = %f, seg1len = %f, seg2len = %f, edgelen = %f, s2s = %f / %f\n",
        unscale(this->max_width), unscale(this->min_width),
        unscale(dist0), unscale(dist1), unscale(diff),
        unscale(segment1.length()), unscale(segment2.length()),
        unscale(line.length()),
        unscale(dist_between_segments1), unscale(dist_between_segments2)
        );
    */

    // if this edge is the centerline for a very thin area, we might want to skip it
    // in case the area is too thin
    if (dist0 < this->min_width && dist1 < this->min_width) {
        //printf(" => too thin, skipping\n");
        return false;
    }

    return true;
}

const Line&
MedialAxis::retrieve_segment(const VD::cell_type& cell) const
{
    VD::cell_type::source_index_type index = cell.source_index() - this->points.size();
    return this->lines[index];
}

} }
