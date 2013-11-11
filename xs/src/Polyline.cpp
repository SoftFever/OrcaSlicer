#include "Polyline.hpp"

namespace Slic3r {

Point*
Polyline::last_point() const
{
    return new Point(this->points.back());
}

Lines
Polyline::lines() const
{
    Lines lines;
    lines.reserve(this->points.size() - 1);
    for (Points::const_iterator it = this->points.begin(); it != this->points.end()-1; ++it) {
        lines.push_back(Line(*it, *(it + 1)));
    }
    return lines;
}

// removes the given distance from the end of the polyline
void
Polyline::clip_end(double distance)
{
    while (distance > 0) {
        Point last_point = *this->last_point();
        this->points.pop_back();
        if (this->points.empty()) break;
        
        double last_segment_length = last_point.distance_to(this->last_point());
        if (last_segment_length <= distance) {
            distance -= last_segment_length;
            continue;
        }
        
        Line segment(last_point, *this->last_point());
        this->points.push_back(*segment.point_at(distance));
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

/* this method returns a collection of points picked on the polygon contour
   so that they are evenly spaced according to the input distance */
Points
Polyline::equally_spaced_points(double distance) const
{
    Points pts;
    pts.push_back(*this->first_point());
    double len = 0;
    
    for (Points::const_iterator it = this->points.begin() + 1; it != this->points.end(); ++it) {
        double segment_length = it->distance_to(&*(it-1));
        len += segment_length;
        if (len < distance) continue;
        
        if (len == distance) {
            pts.push_back(*it);
            len = 0;
            continue;
        }
        
        double take = segment_length - (len - distance);  // how much we take of this segment
        Line segment(*(it-1), *it);
        pts.push_back(*segment.point_at(take));
        it--;
        len = -take;
    }
    
    return pts;
}


#ifdef SLIC3RXS
SV*
Polyline::to_SV_ref()
{
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Polyline::Ref", (void*)this );
    return sv;
}

SV*
Polyline::to_SV_clone_ref() const
{
    SV* sv = newSV(0);
    sv_setref_pv( sv, "Slic3r::Polyline", new Polyline(*this) );
    return sv;
}
#endif

}
