#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "ExPolygonCollection.hpp"
#include "ClipperUtils.hpp"
#include "Extruder.hpp"
#include <sstream>
#ifdef SLIC3RXS
#include "perlglue.hpp"
#endif

namespace Slic3r {

bool
ExtrusionEntity::is_perimeter() const
{
    return this->role == erPerimeter
        || this->role == erExternalPerimeter
        || this->role == erOverhangPerimeter
        || this->role == erContourInternalPerimeter;
}

bool
ExtrusionEntity::is_fill() const
{
    return this->role == erFill
        || this->role == erSolidFill
        || this->role == erTopSolidFill;
}

bool
ExtrusionEntity::is_bridge() const
{
    return this->role == erBrige
        || this->role == erInternalBridge
        || this->role == erOverhangPerimeter;
}

ExtrusionPath*
ExtrusionPath::clone() const
{
    return new ExtrusionPath (*this);
}
    
void
ExtrusionPath::reverse()
{
    this->polyline.reverse();
}

Point
ExtrusionPath::first_point() const
{
    return this->polyline.points.front();
}

Point
ExtrusionPath::last_point() const
{
    return this->polyline.points.back();
}

void
ExtrusionPath::intersect_expolygons(const ExPolygonCollection &collection, ExtrusionEntityCollection* retval) const
{
    // perform clipping
    Polylines clipped;
    intersection(this->polyline, collection, clipped);
    return this->_inflate_collection(clipped, retval);
}

void
ExtrusionPath::subtract_expolygons(const ExPolygonCollection &collection, ExtrusionEntityCollection* retval) const
{
    // perform clipping
    Polylines clipped;
    diff(this->polyline, collection, clipped);
    return this->_inflate_collection(clipped, retval);
}

void
ExtrusionPath::clip_end(double distance)
{
    this->polyline.clip_end(distance);
}

void
ExtrusionPath::simplify(double tolerance)
{
    this->polyline.simplify(tolerance);
}

double
ExtrusionPath::length() const
{
    return this->polyline.length();
}

void
ExtrusionPath::_inflate_collection(const Polylines &polylines, ExtrusionEntityCollection* collection) const
{
    for (Polylines::const_iterator it = polylines.begin(); it != polylines.end(); ++it) {
        ExtrusionPath* path = this->clone();
        path->polyline = *it;
        collection->entities.push_back(path);
    }
}

#ifdef SLIC3RXS

REGISTER_CLASS(ExtrusionPath, "ExtrusionPath");

std::string
ExtrusionPath::gcode(Extruder* extruder, double e, double F,
    double xofs, double yofs, std::string extrusion_axis,
    std::string gcode_line_suffix) const
{
    dSP;

    std::stringstream stream;
    stream.setf(std::ios::fixed);

    double local_F = F;

    Lines lines = this->polyline.lines();
    for (Lines::const_iterator line_it = lines.begin();
        line_it != lines.end(); ++line_it)
    {
        const double line_length = line_it->length() * SCALING_FACTOR;

        // calculate extrusion length for this line
        double E = (e == 0) ? 0 : extruder->extrude(e * line_length);

        // compose G-code line

        Point point = line_it->b;
        const double x = point.x * SCALING_FACTOR + xofs;
        const double y = point.y * SCALING_FACTOR + yofs;
        stream.precision(3);
        stream << "G1 X" << x << " Y" << y;

        if (E != 0) {
            stream.precision(5);
            stream << " " << extrusion_axis << E;
        }

        if (local_F != 0) {
            stream.precision(3);
            stream << " F" << local_F;
            local_F = 0;
        }

        stream << gcode_line_suffix;
        stream << "\n";
    }

    return stream.str();
}
#endif

ExtrusionLoop::ExtrusionLoop(const Polygon &polygon, ExtrusionRole role)
{
    this->role = role;
    this->set_polygon(polygon);
}

ExtrusionLoop*
ExtrusionLoop::clone() const
{
    return new ExtrusionLoop (*this);
}

void
ExtrusionLoop::split_at_index(int index, ExtrusionPath* path) const
{
    Polygon polygon;
    this->polygon(&polygon);
    
    polygon.split_at_index(index, &path->polyline);
    
    path->role          = this->role;
    path->mm3_per_mm    = this->mm3_per_mm;
    path->width         = this->width;
    path->height        = this->height;
}

void
ExtrusionLoop::split_at_first_point(ExtrusionPath* path) const
{
    return this->split_at_index(0, path);
}

bool
ExtrusionLoop::make_counter_clockwise()
{
    Polygon polygon;
    this->polygon(&polygon);
    
    bool was_cw = polygon.is_clockwise();
    if (was_cw) this->reverse();
    return was_cw;
}

void
ExtrusionLoop::reverse()
{
    for (Polylines::iterator polyline = this->polylines.begin(); polyline != this->polylines.end(); ++polyline)
        polyline->reverse();
    std::reverse(this->polylines.begin(), this->polylines.end());
}

Point
ExtrusionLoop::first_point() const
{
    return this->polylines.front().points.front();
}

Point
ExtrusionLoop::last_point() const
{
    return this->polylines.back().points.back();  // which coincides with first_point(), by the way
}

void
ExtrusionLoop::set_polygon(const Polygon &polygon)
{
    Polyline polyline;
    polygon.split_at_first_point(&polyline);
    this->polylines.clear();
    this->polylines.push_back(polyline);
}

void
ExtrusionLoop::polygon(Polygon* polygon) const
{
    for (Polylines::const_iterator polyline = this->polylines.begin(); polyline != this->polylines.end(); ++polyline) {
        // for each polyline, append all points except the last one (because it coincides with the first one of the next polyline)
        polygon->points.insert(polygon->points.end(), polyline->points.begin(), polyline->points.end()-1);
    }
}

#ifdef SLIC3RXS
REGISTER_CLASS(ExtrusionLoop, "ExtrusionLoop");
#endif

}
