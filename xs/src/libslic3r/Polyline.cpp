#include "BoundingBox.hpp"
#include "Polyline.hpp"
#include "ExPolygon.hpp"
#include "ExPolygonCollection.hpp"
#include "Line.hpp"
#include "Polygon.hpp"
#include <iostream>
#include <utility>

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
    Line line(
        this->points.back(),
        *(this->points.end() - 2)
    );
    this->points.back() = line.point_at(-distance);
}

void
Polyline::extend_start(double distance)
{
    // relocate first point by extending the first segment by the specified length
    this->points.front() = Line(this->points.front(), this->points[1]).point_at(-distance);
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
        --it;
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
    
    size_t s = 0;
    bool did_erase = false;
    for (size_t i = s+2; i < pp.size(); i = s + 2) {
        if (area.contains(Line(pp[s], pp[i]))) {
            pp.erase(pp.begin() + s + 1, pp.begin() + i);
            did_erase = true;
        } else {
            ++s;
        }
    }
    if (did_erase)
        this->simplify_by_visibility(area);
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

bool Polyline::is_straight() const
{
    // Check that each segment's direction is equal to the line connecting
    // first point and last point. (Checking each line against the previous
    // one would cause the error to accumulate.)
    double dir = Line(this->first_point(), this->last_point()).direction();
    for (const auto &line: this->lines())
        if (! line.parallel_to(dir))
            return false;
    return true;
}

std::string Polyline::wkt() const
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

BoundingBox get_extents(const Polyline &polyline)
{
    return polyline.bounding_box();
}

BoundingBox get_extents(const Polylines &polylines)
{
    BoundingBox bb;
    if (! polylines.empty()) {
        bb = polylines.front().bounding_box();
        for (size_t i = 1; i < polylines.size(); ++ i)
            bb.merge(polylines[i]);
    }
    return bb;
}

bool remove_degenerate(Polylines &polylines)
{
    bool modified = false;
    size_t j = 0;
    for (size_t i = 0; i < polylines.size(); ++ i) {
        if (polylines[i].points.size() >= 2) {
            if (j < i) 
                std::swap(polylines[i].points, polylines[j].points);
            ++ j;
        } else
            modified = true;
    }
    if (j < polylines.size())
        polylines.erase(polylines.begin() + j, polylines.end());
    return modified;
}

ThickLines
ThickPolyline::thicklines() const
{
    ThickLines lines;
    if (this->points.size() >= 2) {
        lines.reserve(this->points.size() - 1);
        for (size_t i = 0; i < this->points.size()-1; ++i) {
            ThickLine line(this->points[i], this->points[i+1]);
            line.a_width = this->width[2*i];
            line.b_width = this->width[2*i+1];
            lines.push_back(line);
        }
    }
    return lines;
}

void
ThickPolyline::reverse()
{
    Polyline::reverse();
    std::reverse(this->width.begin(), this->width.end());
    std::swap(this->endpoints.first, this->endpoints.second);
}

Lines3 Polyline3::lines() const
{
    Lines3 lines;
    if (points.size() >= 2)
    {
        lines.reserve(points.size() - 1);
        for (Points3::const_iterator it = points.begin(); it != points.end() - 1; ++it)
        {
            lines.emplace_back(*it, *(it + 1));
        }
    }
    return lines;
}

}
