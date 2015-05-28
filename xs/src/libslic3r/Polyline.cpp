#include "Polyline.hpp"
#include "ExPolygon.hpp"
#include "ExPolygonCollection.hpp"
#include "Line.hpp"
#include "Polygon.hpp"
#include <iostream>

namespace Slic3r {

Polyline::operator Polylines() const
{
    Polylines polylines;
    polylines.push_back(*this);
    return polylines;
}

Polyline::operator Line() const
{
    if (this->points.size() > 2) CONFESS("Can't convert polyline with more than two points to a line");
    return Line(this->points.front(), this->points.back());
}

Point
Polyline::last_point() const
{
    return this->points.back();
}

Point
Polyline::leftmost_point() const
{
    Point p = this->points.front();
    for (Points::const_iterator it = this->points.begin() + 1; it != this->points.end(); ++it) {
        if (it->x < p.x) p = *it;
    }
    return p;
}

Lines
Polyline::lines() const
{
    Lines lines;
    if (this->points.size() >= 2) {
        lines.reserve(this->points.size() - 1);
        for (Points::const_iterator it = this->points.begin(); it != this->points.end()-1; ++it) {
            lines.push_back(Line(*it, *(it + 1)));
        }
    }
    return lines;
}

// removes the given distance from the end of the polyline
void
Polyline::clip_end(double distance)
{
    while (distance > 0) {
        Point last_point = this->last_point();
        this->points.pop_back();
        if (this->points.empty()) break;
        
        double last_segment_length = last_point.distance_to(this->last_point());
        if (last_segment_length <= distance) {
            distance -= last_segment_length;
            continue;
        }
        
        Line segment(last_point, this->last_point());
        this->points.push_back(segment.point_at(distance));
        distance = 0;
    }
}

// removes the given distance from the start of the polyline
void
Polyline::clip_start(double distance)
{
    this->reverse();
    this->clip_end(distance);
    if (this->points.size() >= 2) this->reverse();
}

void
Polyline::extend_end(double distance)
{
    // relocate last point by extending the last segment by the specified length
    Line line(this->points[ this->points.size()-2 ], this->points.back());
    this->points.pop_back();
    this->points.push_back(line.point_at(line.length() + distance));
}

void
Polyline::extend_start(double distance)
{
    // relocate first point by extending the first segment by the specified length
    Line line(this->points[1], this->points.front());
    this->points[0] = line.point_at(line.length() + distance);
}

/* this method returns a collection of points picked on the polygon contour
   so that they are evenly spaced according to the input distance */
Points
Polyline::equally_spaced_points(double distance) const
{
    Points points;
    points.push_back(this->first_point());
    double len = 0;
    
    for (Points::const_iterator it = this->points.begin() + 1; it != this->points.end(); ++it) {
        double segment_length = it->distance_to(*(it-1));
        len += segment_length;
        if (len < distance) continue;
        
        if (len == distance) {
            points.push_back(*it);
            len = 0;
            continue;
        }
        
        double take = segment_length - (len - distance);  // how much we take of this segment
        Line segment(*(it-1), *it);
        points.push_back(segment.point_at(take));
        it--;
        len = -take;
    }
    return points;
}

void
Polyline::simplify(double tolerance)
{
    this->points = MultiPoint::_douglas_peucker(this->points, tolerance);
}

/* This method simplifies all *lines* contained in the supplied area */
template <class T>
void
Polyline::simplify_by_visibility(const T &area)
{
    Points &pp = this->points;
    
    // find first point in area
    size_t s = 0;
    while (s < pp.size() && !area.contains(pp[s])) {
        ++s;
    }
    
    // find last point in area
    size_t e = pp.size()-1;
    while (e > 0 && !area.contains(pp[e])) {
        --e;
    }
    
    // this ugly algorithm resembles a binary search
    while (e > s + 1) {
        size_t mid = (s + e) / 2;
        if (area.contains(Line(pp[s], pp[mid]))) {
            pp.erase(pp.begin() + s + 1, pp.begin() + mid);
            // repeat recursively until no further simplification is possible
            ++s;
            e = pp.size()-1;
        } else {
            e = mid;
        }
    }
    /*
    // The following implementation is complete but it's not efficient at all:
    for (size_t s = start; s < pp.size() && !pp.empty(); ++s) {
        // find the farthest point to which we can build
        // a line that is contained in the supplied area
        // a binary search would be more efficient for this
        for (size_t e = pp.size()-1; e > (s + 1); --e) {
            if (area.contains(Line(pp[s], pp[e]))) {
                // we can suppress points between s and e
                pp.erase(pp.begin() + s + 1, pp.begin() + e);
                
                // repeat recursively until no further simplification is possible
                return this->simplify_by_visibility(area);
            }
        }
    }
    */
}
template void Polyline::simplify_by_visibility<ExPolygon>(const ExPolygon &area);
template void Polyline::simplify_by_visibility<ExPolygonCollection>(const ExPolygonCollection &area);

void
Polyline::split_at(const Point &point, Polyline* p1, Polyline* p2) const
{
    if (this->points.empty()) return;
    
    // find the line to split at
    size_t line_idx = 0;
    Point p = this->first_point();
    double min = point.distance_to(p);
    Lines lines = this->lines();
    for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line) {
        Point p_tmp = point.projection_onto(*line);
        if (point.distance_to(p_tmp) < min) {
	        p = p_tmp;
	        min = point.distance_to(p);
	        line_idx = line - lines.begin();
        }
    }
    
    // create first half
    p1->points.clear();
    for (Lines::const_iterator line = lines.begin(); line != lines.begin() + line_idx + 1; ++line) {
        if (!line->a.coincides_with(p)) p1->points.push_back(line->a);
    }
    // we add point instead of p because they might differ because of numerical issues
    // and caller might want to rely on point belonging to result polylines
    p1->points.push_back(point);
    
    // create second half
    p2->points.clear();
    p2->points.push_back(point);
    for (Lines::const_iterator line = lines.begin() + line_idx; line != lines.end(); ++line) {
        p2->points.push_back(line->b);
    }
}

bool
Polyline::is_straight() const
{
    /*  Check that each segment's direction is equal to the line connecting
        first point and last point. (Checking each line against the previous
        one would cause the error to accumulate.) */
    double dir = Line(this->first_point(), this->last_point()).direction();
    
    Lines lines = this->lines();
    for (Lines::const_iterator line = lines.begin(); line != lines.end(); ++line) {
        if (!line->parallel_to(dir)) return false;
    }
    return true;
}

std::string
Polyline::wkt() const
{
    std::ostringstream wkt;
    wkt << "LINESTRING((";
    for (Points::const_iterator p = this->points.begin(); p != this->points.end(); ++p) {
        wkt << p->x << " " << p->y;
        if (p != this->points.end()-1) wkt << ",";
    }
    wkt << "))";
    return wkt.str();
}


#ifdef SLIC3RXS
REGISTER_CLASS(Polyline, "Polyline");

void
Polyline::from_SV_check(SV* poly_sv)
{
    if (!sv_isa(poly_sv, perl_class_name(this)) && !sv_isa(poly_sv, perl_class_name_ref(this)))
        CONFESS("Not a valid %s object",perl_class_name(this));
    
    MultiPoint::from_SV_check(poly_sv);
}
#endif

}
