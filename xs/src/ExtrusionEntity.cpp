#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "ExPolygonCollection.hpp"
#include "ClipperUtils.hpp"

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

Point*
ExtrusionPath::first_point() const
{
    return new Point(this->polyline.points.front());
}

Point*
ExtrusionPath::last_point() const
{
    return new Point(this->polyline.points.back());
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

ExtrusionLoop*
ExtrusionLoop::clone() const
{
    return new ExtrusionLoop (*this);
}

ExtrusionPath*
ExtrusionLoop::split_at_index(int index) const
{
    Polyline* poly = this->polygon.split_at_index(index);
    
    ExtrusionPath* path = new ExtrusionPath();
    path->polyline      = *poly;
    path->role          = this->role;
    path->mm3_per_mm    = this->mm3_per_mm;
    
    delete poly;
    return path;
}

ExtrusionPath*
ExtrusionLoop::split_at_first_point() const
{
    return this->split_at_index(0);
}

bool
ExtrusionLoop::make_counter_clockwise()
{
    return this->polygon.make_counter_clockwise();
}

void
ExtrusionLoop::reverse()
{
    // no-op
}

Point*
ExtrusionLoop::first_point() const
{
    return new Point(this->polygon.points.front());
}

Point*
ExtrusionLoop::last_point() const
{
    return new Point(this->polygon.points.front());  // in polygons, first == last
}

}
