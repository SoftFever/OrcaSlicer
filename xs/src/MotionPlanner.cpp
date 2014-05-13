#include "BoundingBox.hpp"
#include "MotionPlanner.hpp"

namespace Slic3r {

MotionPlanner::MotionPlanner(const ExPolygons &islands)
    : islands(islands), initialized(false)
{}

MotionPlanner::~MotionPlanner()
{
    for (std::vector<VisiLibity::Environment*>::iterator env = this->envs.begin(); env != this->envs.end(); ++env)
        delete *env;
    for (std::vector<VisiLibity::Visibility_Graph*>::iterator graph = this->graphs.begin(); graph != this->graphs.end(); ++graph)
        delete *graph;
}

void
MotionPlanner::initialize()
{
    if (this->initialized) return;
    
    // loop through islands in order to create inner expolygons and collect their contours
    this->inner.reserve(this->islands.size());
    Polygons outer_holes;
    for (ExPolygons::const_iterator island = this->islands.begin(); island != this->islands.end(); ++island) {
        this->inner.push_back(ExPolygonCollection());
        offset_ex(*island, this->inner.back(), -MP_INNER_MARGIN);
        
        outer_holes.push_back(island->contour);
    }
    
    // grow island contours in order to prepare holes of the outer environment
    // This is actually wrong because it might merge contours that are close,
    // thus confusing the island check in shortest_path() below
    //offset(outer_holes, outer_holes, +MP_OUTER_MARGIN);
    
    // generate outer contour as bounding box of everything
    Points points;
    for (Polygons::const_iterator contour = outer_holes.begin(); contour != outer_holes.end(); ++contour)
        points.insert(points.end(), contour->points.begin(), contour->points.end());
    BoundingBox bb(points);
    
    // grow outer contour
    Polygons contour;
    offset(bb.polygon(), contour, +MP_OUTER_MARGIN);
    assert(contour.size() == 1);
    
    // make expolygon for outer environment
    ExPolygons outer;
    diff(contour, outer_holes, outer);
    assert(outer.size() == 1);
    this->outer = outer.front();
    
    this->envs.resize(this->islands.size() + 1, NULL);
    this->graphs.resize(this->islands.size() + 1, NULL);
    this->initialized = true;
}

void
MotionPlanner::shortest_path(const Point &from, const Point &to, Polyline* polyline)
{
    if (!this->initialized) this->initialize();
    
    // Are both points in the same island?
    int island_idx = -1;
    for (ExPolygons::const_iterator island = this->islands.begin(); island != this->islands.end(); ++island) {
        if (island->contains_point(from) && island->contains_point(to)) {
            island_idx = island - this->islands.begin();
            break;
        }
    }
    
    // Generate environment.
    this->generate_environment(island_idx);
    
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
    *polyline = convert_polyline(this->envs[island_idx + 1]->shortest_path(convert_point(inner_from),
        convert_point(inner_to), *this->graphs[island_idx + 1], SCALED_EPSILON));
    
    // if start point was outside the environment, prepend it
    if (!from_is_inside) polyline->points.insert(polyline->points.begin(), from);
    
    // if end point was outside the environment, append it
    if (!to_is_inside) polyline->points.push_back(to);
}

void
MotionPlanner::generate_environment(int island_idx)
{
    if (this->envs[island_idx + 1] != NULL) return;
    
    Polygons pp;
    if (island_idx == -1) {
        pp = this->outer;
    } else {
        pp = this->inner[island_idx];
    }
        
    // populate VisiLibity polygons
    std::vector<VisiLibity::Polygon> v_polygons;
    for (Polygons::const_iterator p = pp.begin(); p != pp.end(); ++p)
        v_polygons.push_back(convert_polygon(*p));
    
    // generate graph and environment
    this->envs[island_idx + 1] = new VisiLibity::Environment(v_polygons);
    this->graphs[island_idx + 1] = new VisiLibity::Visibility_Graph(*this->envs[island_idx + 1], SCALED_EPSILON);
}

VisiLibity::Polyline
MotionPlanner::convert_polyline(const Polyline &polyline)
{
    VisiLibity::Polyline v_polyline;
    for (Points::const_iterator point = polyline.points.begin(); point != polyline.points.end(); ++point) {
        v_polyline.push_back(convert_point(*point));
    }
    return v_polyline;
}

Polyline
MotionPlanner::convert_polyline(const VisiLibity::Polyline &v_polyline)
{
    Polyline polyline;
    polyline.points.reserve(v_polyline.size());
    for (size_t i = 0; i < v_polyline.size(); ++i) {
        polyline.points.push_back(convert_point(v_polyline[i]));
    }
    return polyline;
}

VisiLibity::Polygon
MotionPlanner::convert_polygon(const Polygon &polygon)
{
    VisiLibity::Polygon v_polygon;
    for (Points::const_iterator point = polygon.points.begin(); point != polygon.points.end(); ++point) {
        v_polygon.push_back(convert_point(*point));
    }
    return v_polygon;
}

VisiLibity::Point
MotionPlanner::convert_point(const Point &point)
{
    return VisiLibity::Point(point.x, point.y);
}

Point
MotionPlanner::convert_point(const VisiLibity::Point &v_point)
{
    return Point((coord_t)v_point.x(), (coord_t)v_point.y());
}

#ifdef SLIC3RXS
REGISTER_CLASS(MotionPlanner, "MotionPlanner");
#endif

}
