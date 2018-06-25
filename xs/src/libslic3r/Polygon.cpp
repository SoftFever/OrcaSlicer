#include "BoundingBox.hpp"
#include "ClipperUtils.hpp"
#include "Polygon.hpp"
#include "Polyline.hpp"

namespace Slic3r {

Polygon::operator Polygons() const
{
    Polygons pp;
    pp.push_back(*this);
    return pp;
}

Polygon::operator Polyline() const
{
    return this->split_at_first_point();
}

Point&
Polygon::operator[](Points::size_type idx)
{
    return this->points[idx];
}

const Point&
Polygon::operator[](Points::size_type idx) const
{
    return this->points[idx];
}

Point
Polygon::last_point() const
{
    return this->points.front();  // last point == first point for polygons
}

Lines Polygon::lines() const
{
    return to_lines(*this);
}

Polyline
Polygon::split_at_vertex(const Point &point) const
{
    // find index of point
    for (Points::const_iterator it = this->points.begin(); it != this->points.end(); ++it) {
        if (it->coincides_with(point)) {
            return this->split_at_index(it - this->points.begin());
        }
    }
    CONFESS("Point not found");
    return Polyline();
}

// Split a closed polygon into an open polyline, with the split point duplicated at both ends.
Polyline
Polygon::split_at_index(int index) const
{
    Polyline polyline;
    polyline.points.reserve(this->points.size() + 1);
    for (Points::const_iterator it = this->points.begin() + index; it != this->points.end(); ++it)
        polyline.points.push_back(*it);
    for (Points::const_iterator it = this->points.begin(); it != this->points.begin() + index + 1; ++it)
        polyline.points.push_back(*it);
    return polyline;
}

// Split a closed polygon into an open polyline, with the split point duplicated at both ends.
Polyline
Polygon::split_at_first_point() const
{
    return this->split_at_index(0);
}

Points
Polygon::equally_spaced_points(double distance) const
{
    return this->split_at_first_point().equally_spaced_points(distance);
}

/*
int64_t Polygon::area2x() const
{
    size_t n = poly.size();
    if (n < 3) 
        return 0;

    int64_t a = 0;
    for (size_t i = 0, j = n - 1; i < n; ++i)
        a += int64_t(poly[j].x + poly[i].x) * int64_t(poly[j].y - poly[i].y);
        j = i;
    }
    return -a * 0.5;
}
*/

double Polygon::area() const
{
    size_t n = points.size();
    if (n < 3) 
        return 0.;

    double a = 0.;
    for (size_t i = 0, j = n - 1; i < n; ++i) {
        a += ((double)points[j].x + (double)points[i].x) * ((double)points[i].y - (double)points[j].y);
        j = i;
    }
    return 0.5 * a;
}

bool
Polygon::is_counter_clockwise() const
{
    return ClipperLib::Orientation(Slic3rMultiPoint_to_ClipperPath(*this));
}

bool
Polygon::is_clockwise() const
{
    return !this->is_counter_clockwise();
}

bool
Polygon::make_counter_clockwise()
{
    if (!this->is_counter_clockwise()) {
        this->reverse();
        return true;
    }
    return false;
}

bool
Polygon::make_clockwise()
{
    if (this->is_counter_clockwise()) {
        this->reverse();
        return true;
    }
    return false;
}

bool
Polygon::is_valid() const
{
    return this->points.size() >= 3;
}

// Does an unoriented polygon contain a point?
// Tested by counting intersections along a horizontal line.
bool
Polygon::contains(const Point &point) const
{
    // http://www.ecse.rpi.edu/Homepages/wrf/Research/Short_Notes/pnpoly.html
    bool result = false;
    Points::const_iterator i = this->points.begin();
    Points::const_iterator j = this->points.end() - 1;
    for (; i != this->points.end(); j = i++) {
        //FIXME this test is not numerically robust. Particularly, it does not handle horizontal segments at y == point.y well.
        // Does the ray with y == point.y intersect this line segment?
#if 1
        if ( ((i->y > point.y) != (j->y > point.y))
            && ((double)point.x < (double)(j->x - i->x) * (double)(point.y - i->y) / (double)(j->y - i->y) + (double)i->x) )
            result = !result;
#else
        if ((i->y > point.y) != (j->y > point.y)) {
            // Orientation predicated relative to i-th point.
            double orient = (double)(point.x - i->x) * (double)(j->y - i->y) - (double)(point.y - i->y) * (double)(j->x - i->x);
            if ((i->y > j->y) ? (orient > 0.) : (orient < 0.))
                result = !result;
        }
#endif
    }
    return result;
}

// this only works on CCW polygons as CW will be ripped out by Clipper's simplify_polygons()
Polygons
Polygon::simplify(double tolerance) const
{
    // repeat first point at the end in order to apply Douglas-Peucker
    // on the whole polygon
    Points points = this->points;
    points.push_back(points.front());
    Polygon p(MultiPoint::_douglas_peucker(points, tolerance));
    p.points.pop_back();
    
    Polygons pp;
    pp.push_back(p);
    return simplify_polygons(pp);
}

void
Polygon::simplify(double tolerance, Polygons &polygons) const
{
    Polygons pp = this->simplify(tolerance);
    polygons.reserve(polygons.size() + pp.size());
    polygons.insert(polygons.end(), pp.begin(), pp.end());
}

// Only call this on convex polygons or it will return invalid results
void
Polygon::triangulate_convex(Polygons* polygons) const
{
    for (Points::const_iterator it = this->points.begin() + 2; it != this->points.end(); ++it) {
        Polygon p;
        p.points.reserve(3);
        p.points.push_back(this->points.front());
        p.points.push_back(*(it-1));
        p.points.push_back(*it);
        
        // this should be replaced with a more efficient call to a merge_collinear_segments() method
        if (p.area() > 0) polygons->push_back(p);
    }
}

// center of mass
Point
Polygon::centroid() const
{
    double area_temp = this->area();
    double x_temp = 0;
    double y_temp = 0;
    
    Polyline polyline = this->split_at_first_point();
    for (Points::const_iterator point = polyline.points.begin(); point != polyline.points.end() - 1; ++point) {
        x_temp += (double)( point->x + (point+1)->x ) * ( (double)point->x*(point+1)->y - (double)(point+1)->x*point->y );
        y_temp += (double)( point->y + (point+1)->y ) * ( (double)point->x*(point+1)->y - (double)(point+1)->x*point->y );
    }
    
    return Point(x_temp/(6*area_temp), y_temp/(6*area_temp));
}

std::string
Polygon::wkt() const
{
    std::ostringstream wkt;
    wkt << "POLYGON((";
    for (Points::const_iterator p = this->points.begin(); p != this->points.end(); ++p) {
        wkt << p->x << " " << p->y;
        if (p != this->points.end()-1) wkt << ",";
    }
    wkt << "))";
    return wkt.str();
}

// find all concave vertices (i.e. having an internal angle greater than the supplied angle)
// (external = right side, thus we consider ccw orientation)
Points
Polygon::concave_points(double angle) const
{
    Points points;
    angle = 2*PI - angle;
    
    // check whether first point forms a concave angle
    if (this->points.front().ccw_angle(this->points.back(), *(this->points.begin()+1)) <= angle)
        points.push_back(this->points.front());
    
    // check whether points 1..(n-1) form concave angles
    for (Points::const_iterator p = this->points.begin()+1; p != this->points.end()-1; ++p) {
        if (p->ccw_angle(*(p-1), *(p+1)) <= angle) points.push_back(*p);
    }
    
    // check whether last point forms a concave angle
    if (this->points.back().ccw_angle(*(this->points.end()-2), this->points.front()) <= angle)
        points.push_back(this->points.back());
    
    return points;
}

// find all convex vertices (i.e. having an internal angle smaller than the supplied angle)
// (external = right side, thus we consider ccw orientation)
Points
Polygon::convex_points(double angle) const
{
    Points points;
    angle = 2*PI - angle;
    
    // check whether first point forms a convex angle
    if (this->points.front().ccw_angle(this->points.back(), *(this->points.begin()+1)) >= angle)
        points.push_back(this->points.front());
    
    // check whether points 1..(n-1) form convex angles
    for (Points::const_iterator p = this->points.begin()+1; p != this->points.end()-1; ++p) {
        if (p->ccw_angle(*(p-1), *(p+1)) >= angle) points.push_back(*p);
    }
    
    // check whether last point forms a convex angle
    if (this->points.back().ccw_angle(*(this->points.end()-2), this->points.front()) >= angle)
        points.push_back(this->points.back());
    
    return points;
}

// Projection of a point onto the polygon.
Point Polygon::point_projection(const Point &point) const
{
    Point proj = point;
    double dmin = std::numeric_limits<double>::max();
    if (! this->points.empty()) {
        for (size_t i = 0; i < this->points.size(); ++ i) {
            const Point &pt0 = this->points[i];
            const Point &pt1 = this->points[(i + 1 == this->points.size()) ? 0 : i + 1];
            double d = pt0.distance_to(point);
            if (d < dmin) {
                dmin = d;
                proj = pt0;
            }
            d = pt1.distance_to(point);
            if (d < dmin) {
                dmin = d;
                proj = pt1;
            }
            Pointf v1(coordf_t(pt1.x - pt0.x), coordf_t(pt1.y - pt0.y));
            coordf_t div = dot(v1);
            if (div > 0.) {
                Pointf v2(coordf_t(point.x - pt0.x), coordf_t(point.y - pt0.y));
                coordf_t t = dot(v1, v2) / div;
                if (t > 0. && t < 1.) {
                    Point foot(coord_t(floor(coordf_t(pt0.x) + t * v1.x + 0.5)), coord_t(floor(coordf_t(pt0.y) + t * v1.y + 0.5)));
                    d = foot.distance_to(point);
                    if (d < dmin) {
                        dmin = d;
                        proj = foot;
                    }
                }
            }
        }
    }
    return proj;
}

BoundingBox get_extents(const Polygon &poly) 
{ 
    return poly.bounding_box();
}

BoundingBox get_extents(const Polygons &polygons)
{
    BoundingBox bb;
    if (! polygons.empty()) {
        bb = get_extents(polygons.front());
        for (size_t i = 1; i < polygons.size(); ++ i)
            bb.merge(get_extents(polygons[i]));
    }
    return bb;
}

BoundingBox get_extents_rotated(const Polygon &poly, double angle) 
{ 
    return get_extents_rotated(poly.points, angle);
}

BoundingBox get_extents_rotated(const Polygons &polygons, double angle)
{
    BoundingBox bb;
    if (! polygons.empty()) {
        bb = get_extents_rotated(polygons.front().points, angle);
        for (size_t i = 1; i < polygons.size(); ++ i)
            bb.merge(get_extents_rotated(polygons[i].points, angle));
    }
    return bb;
}

extern std::vector<BoundingBox> get_extents_vector(const Polygons &polygons)
{
    std::vector<BoundingBox> out;
    out.reserve(polygons.size());
    for (Polygons::const_iterator it = polygons.begin(); it != polygons.end(); ++ it)
        out.push_back(get_extents(*it));
    return out;
}

static inline bool is_stick(const Point &p1, const Point &p2, const Point &p3)
{
    Point v1 = p2 - p1;
    Point v2 = p3 - p2;
    int64_t dir = int64_t(v1.x) * int64_t(v2.x) + int64_t(v1.y) * int64_t(v2.y);
    if (dir > 0)
        // p3 does not turn back to p1. Do not remove p2.
        return false;
    double l2_1 = double(v1.x) * double(v1.x) + double(v1.y) * double(v1.y);
    double l2_2 = double(v2.x) * double(v2.x) + double(v2.y) * double(v2.y);
    if (dir == 0)
        // p1, p2, p3 may make a perpendicular corner, or there is a zero edge length.
        // Remove p2 if it is coincident with p1 or p2.
        return l2_1 == 0 || l2_2 == 0;
    // p3 turns back to p1 after p2. Are p1, p2, p3 collinear?
    // Calculate distance from p3 to a segment (p1, p2) or from p1 to a segment(p2, p3),
    // whichever segment is longer
    double cross = double(v1.x) * double(v2.y) - double(v2.x) * double(v1.y);
    double dist2 = cross * cross / std::max(l2_1, l2_2);
    return dist2 < EPSILON * EPSILON;
}

bool remove_sticks(Polygon &poly)
{
    bool modified = false;
    size_t j = 1;
    for (size_t i = 1; i + 1 < poly.points.size(); ++ i) {
        if (! is_stick(poly[j-1], poly[i], poly[i+1])) {
            // Keep the point.
            if (j < i)
                poly.points[j] = poly.points[i];
            ++ j;
        }
    }
    if (++ j < poly.points.size()) {
        poly.points[j-1] = poly.points.back();
        poly.points.erase(poly.points.begin() + j, poly.points.end());
        modified = true;
    }
    while (poly.points.size() >= 3 && is_stick(poly.points[poly.points.size()-2], poly.points.back(), poly.points.front())) {
        poly.points.pop_back();
        modified = true;
    }
    while (poly.points.size() >= 3 && is_stick(poly.points.back(), poly.points.front(), poly.points[1]))
        poly.points.erase(poly.points.begin());
    return modified;
}

bool remove_sticks(Polygons &polys)
{
    bool modified = false;
    size_t j = 0;
    for (size_t i = 0; i < polys.size(); ++ i) {
        modified |= remove_sticks(polys[i]);
        if (polys[i].points.size() >= 3) {
            if (j < i) 
                std::swap(polys[i].points, polys[j].points);
            ++ j;
        }
    }
    if (j < polys.size())
        polys.erase(polys.begin() + j, polys.end());
    return modified;
}

bool remove_degenerate(Polygons &polys)
{
    bool modified = false;
    size_t j = 0;
    for (size_t i = 0; i < polys.size(); ++ i) {
        if (polys[i].points.size() >= 3) {
            if (j < i) 
                std::swap(polys[i].points, polys[j].points);
            ++ j;
        } else
            modified = true;
    }
    if (j < polys.size())
        polys.erase(polys.begin() + j, polys.end());
    return modified;
}

bool remove_small(Polygons &polys, double min_area)
{
    bool modified = false;
    size_t j = 0;
    for (size_t i = 0; i < polys.size(); ++ i) {
        if (std::abs(polys[i].area()) >= min_area) {
            if (j < i) 
                std::swap(polys[i].points, polys[j].points);
            ++ j;
        } else
            modified = true;
    }
    if (j < polys.size())
        polys.erase(polys.begin() + j, polys.end());
    return modified;
}

}
