#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "ExPolygonCollection.hpp"
#include "ClipperUtils.hpp"

namespace Slic3r {

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

ExtrusionEntityCollection*
ExtrusionPath::intersect_expolygons(ExPolygonCollection* collection) const
{
    // perform clipping
    Polylines clipped;
    intersection(this->polyline, *collection, clipped);
    return this->_inflate_collection(clipped);
}

ExtrusionEntityCollection*
ExtrusionPath::subtract_expolygons(ExPolygonCollection* collection) const
{
    // perform clipping
    Polylines clipped;
    diff(this->polyline, *collection, clipped);
    return this->_inflate_collection(clipped);
}

ExtrusionEntityCollection*
ExtrusionPath::_inflate_collection(const Polylines &polylines) const
{
    ExtrusionEntityCollection* retval = new ExtrusionEntityCollection();
    for (Polylines::const_iterator it = polylines.begin(); it != polylines.end(); ++it) {
        ExtrusionPath* path = this->clone();
        path->polyline = *it;
        retval->entities.push_back(path);
    }
    return retval;
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
    path->height        = this->height;
    path->flow_spacing  = this->flow_spacing;
    
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
