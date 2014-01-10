#include "Geometry.hpp"
#include "clipper.hpp"
#include <algorithm>
#include <map>
#include <set>
#include <vector>
#include "voronoi_visual_utils.hpp"

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

void
MedialAxis::build(Polylines* polylines)
{
    // build bounding box (we use it for clipping infinite segments)
    this->bb = BoundingBox(this->lines);
    
    construct_voronoi(this->lines.begin(), this->lines.end(), &this->vd);
    
    // prepare a cache of twin edges to prevent getting the same edge twice
    // (Boost.Polygon returns it duplicated in both directions)
    std::set<const voronoi_diagram<double>::edge_type*> edge_cache;
    
    // iterate through the diagram
    for (voronoi_diagram<double>::const_edge_iterator it = this->vd.edges().begin(); it != this->vd.edges().end(); ++it) {
        (void)edge_cache.insert(it->twin());
        if (edge_cache.count(&*it) > 0) continue;
        if (!it->is_primary()) continue;
        
        Polyline p;
        if (!it->is_finite()) {
            this->clip_infinite_edge(*it, &p.points);
        } else {
            p.points.push_back(Point( it->vertex0()->x(), it->vertex0()->y() ));
            p.points.push_back(Point( it->vertex1()->x(), it->vertex1()->y() ));
            if (it->is_curved()) {
                this->sample_curved_edge(*it, &p.points);
            }
        }
        polylines->push_back(p);
    }
}

void
MedialAxis::clip_infinite_edge(const voronoi_diagram<double>::edge_type& edge, Points* clipped_edge)
{
    const voronoi_diagram<double>::cell_type& cell1 = *edge.cell();
    const voronoi_diagram<double>::cell_type& cell2 = *edge.twin()->cell();
    Point origin, direction;
    // Infinite edges could not be created by two segment sites.
    if (cell1.contains_point() && cell2.contains_point()) {
        Point p1 = retrieve_point(cell1);
        Point p2 = retrieve_point(cell2);
        origin.x = (p1.x + p2.x) * 0.5;
        origin.y = (p1.y + p2.y) * 0.5;
        direction.x = p1.y - p2.y;
        direction.y = p2.x - p1.x;
    } else {
        origin = cell1.contains_segment()
            ? retrieve_point(cell2)
            : retrieve_point(cell1);
        Line segment = cell1.contains_segment()
            ? retrieve_segment(cell1)
            : retrieve_segment(cell2);
        coord_t dx = high(segment).x - low(segment).x;
        coord_t dy = high(segment).y - low(segment).y;
        if ((low(segment) == origin) ^ cell1.contains_point()) {
            direction.x = dy;
            direction.y = -dx;
        } else {
            direction.x = -dy;
            direction.y = dx;
        }
    }
    coord_t side = this->bb.size().x;
    coord_t koef = side / (std::max)(fabs(direction.x), fabs(direction.y));
    if (edge.vertex0() == NULL) {
        clipped_edge->push_back(Point(
            origin.x - direction.x * koef,
            origin.y - direction.y * koef
        ));
    } else {
        clipped_edge->push_back(
        Point(edge.vertex0()->x(), edge.vertex0()->y()));
    }
    if (edge.vertex1() == NULL) {
        clipped_edge->push_back(Point(
            origin.x + direction.x * koef,
            origin.y + direction.y * koef
        ));
    } else {
        clipped_edge->push_back(
        Point(edge.vertex1()->x(), edge.vertex1()->y()));
    }
}

void
MedialAxis::sample_curved_edge(const voronoi_diagram<double>::edge_type& edge, Points* sampled_edge)
{
    Point point = edge.cell()->contains_point()
        ? retrieve_point(*edge.cell())
        : retrieve_point(*edge.twin()->cell());
    
    Line segment = edge.cell()->contains_point()
        ? retrieve_segment(*edge.twin()->cell())
        : retrieve_segment(*edge.cell());
    
    double max_dist = 1E-3 * this->bb.size().x;
    voronoi_visual_utils<double>::discretize<coord_t,coord_t,Point,Line>(point, segment, max_dist, sampled_edge);
}

Point
MedialAxis::retrieve_point(const voronoi_diagram<double>::cell_type& cell)
{
    voronoi_diagram<double>::cell_type::source_index_type index = cell.source_index();
    voronoi_diagram<double>::cell_type::source_category_type category = cell.source_category();
    if (category == SOURCE_CATEGORY_SINGLE_POINT) {
        return this->points[index];
    }
    index -= this->points.size();
    if (category == SOURCE_CATEGORY_SEGMENT_START_POINT) {
        return low(this->lines[index]);
    } else {
        return high(this->lines[index]);
    }
}

Line
MedialAxis::retrieve_segment(const voronoi_diagram<double>::cell_type& cell)
{
    voronoi_diagram<double>::cell_type::source_index_type index = cell.source_index() - this->points.size();
    return this->lines[index];
}

} }
