#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "ExPolygonCollection.hpp"
#include "ClipperUtils.hpp"
#include "Extruder.hpp"
#include <sstream>

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
    intersection<Polylines,Polylines>(this->polyline, collection, &clipped);
    return this->_inflate_collection(clipped, retval);
}

void
ExtrusionPath::subtract_expolygons(const ExPolygonCollection &collection, ExtrusionEntityCollection* retval) const
{
    // perform clipping
    Polylines clipped;
    diff<Polylines,Polylines>(this->polyline, collection, &clipped);
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

bool
ExtrusionPath::is_perimeter() const
{
    return this->role == erPerimeter
        || this->role == erExternalPerimeter
        || this->role == erOverhangPerimeter;
}

bool
ExtrusionPath::is_infill() const
{
    return this->role == erBridgeInfill
        || this->role == erInternalInfill
        || this->role == erSolidInfill
        || this->role == erTopSolidInfill;
}

bool
ExtrusionPath::is_solid_infill() const
{
    return this->role == erBridgeInfill
        || this->role == erSolidInfill
        || this->role == erTopSolidInfill;
}

bool
ExtrusionPath::is_bridge() const
{
    return this->role == erBridgeInfill
        || this->role == erOverhangPerimeter;
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
#endif

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
        double E = 0;
        if (e > 0) {
            extruder->extrude(e * line_length);
            E = extruder->E;
        }

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

Polygons
ExtrusionPath::grow() const
{
    Polygons pp;
    offset(this->polyline, &pp, +this->width/2);
    return pp;
}

ExtrusionLoop*
ExtrusionLoop::clone() const
{
    return new ExtrusionLoop (*this);
}

bool
ExtrusionLoop::make_clockwise()
{
    bool was_ccw = this->polygon().is_counter_clockwise();
    if (was_ccw) this->reverse();
    return was_ccw;
}

bool
ExtrusionLoop::make_counter_clockwise()
{
    bool was_cw = this->polygon().is_clockwise();
    if (was_cw) this->reverse();
    return was_cw;
}

void
ExtrusionLoop::reverse()
{
    for (ExtrusionPaths::iterator path = this->paths.begin(); path != this->paths.end(); ++path)
        path->reverse();
    std::reverse(this->paths.begin(), this->paths.end());
}

Point
ExtrusionLoop::first_point() const
{
    return this->paths.front().polyline.points.front();
}

Point
ExtrusionLoop::last_point() const
{
    return this->paths.back().polyline.points.back();  // which coincides with first_point(), by the way
}

Polygon
ExtrusionLoop::polygon() const
{
    Polygon polygon;
    for (ExtrusionPaths::const_iterator path = this->paths.begin(); path != this->paths.end(); ++path) {
        // for each polyline, append all points except the last one (because it coincides with the first one of the next polyline)
        polygon.points.insert(polygon.points.end(), path->polyline.points.begin(), path->polyline.points.end()-1);
    }
    return polygon;
}

double
ExtrusionLoop::length() const
{
    double len = 0;
    for (ExtrusionPaths::const_iterator path = this->paths.begin(); path != this->paths.end(); ++path)
        len += path->polyline.length();
    return len;
}

bool
ExtrusionLoop::split_at_vertex(const Point &point)
{
    for (ExtrusionPaths::iterator path = this->paths.begin(); path != this->paths.end(); ++path) {
        int idx = path->polyline.find_point(point);
        if (idx != -1) {
            if (this->paths.size() == 1) {
                // just change the order of points
                path->polyline.points.insert(path->polyline.points.end(), path->polyline.points.begin() + 1, path->polyline.points.begin() + idx + 1);
                path->polyline.points.erase(path->polyline.points.begin(), path->polyline.points.begin() + idx);
            } else {
                // new paths list starts with the second half of current path
                ExtrusionPaths new_paths;
                {
                    ExtrusionPath p = *path;
                    p.polyline.points.erase(p.polyline.points.begin(), p.polyline.points.begin() + idx);
                    if (p.polyline.is_valid()) new_paths.push_back(p);
                }
            
                // then we add all paths until the end of current path list
                new_paths.insert(new_paths.end(), path+1, this->paths.end());  // not including this path
            
                // then we add all paths since the beginning of current list up to the previous one
                new_paths.insert(new_paths.end(), this->paths.begin(), path);  // not including this path
            
                // finally we add the first half of current path
                {
                    ExtrusionPath p = *path;
                    p.polyline.points.erase(p.polyline.points.begin() + idx + 1, p.polyline.points.end());
                    if (p.polyline.is_valid()) new_paths.push_back(p);
                }
                // we can now override the old path list with the new one and stop looping
                this->paths = new_paths;
            }
            return true;
        }
    }
    return false;
}

void
ExtrusionLoop::split_at(const Point &point)
{
    if (this->paths.empty()) return;
    
    // find the closest path and closest point belonging to that path
    size_t path_idx = 0;
    Point p = this->paths.front().first_point();
    double min = point.distance_to(p);
    for (ExtrusionPaths::const_iterator path = this->paths.begin(); path != this->paths.end(); ++path) {
        Point p_tmp = point.projection_onto(path->polyline);
        double dist = point.distance_to(p_tmp);
        if (dist < min) {
            p = p_tmp;
            min = dist;
            path_idx = path - this->paths.begin();
        }
    }
    
    // now split path_idx in two parts
    ExtrusionPath p1 = this->paths[path_idx];
    ExtrusionPath p2 = p1;
    this->paths[path_idx].polyline.split_at(p, &p1.polyline, &p2.polyline);
    
    // install the two paths
    this->paths.erase(this->paths.begin() + path_idx);
    if (p2.polyline.is_valid()) this->paths.insert(this->paths.begin() + path_idx, p2);
    if (p1.polyline.is_valid()) this->paths.insert(this->paths.begin() + path_idx, p1);
    
    // split at the new vertex
    this->split_at_vertex(p);
}

void
ExtrusionLoop::clip_end(double distance, ExtrusionPaths* paths) const
{
    *paths = this->paths;
    
    while (distance > 0 && !paths->empty()) {
        ExtrusionPath &last = paths->back();
        double len = last.length();
        if (len <= distance) {
            paths->pop_back();
            distance -= len;
        } else {
            last.polyline.clip_end(distance);
            break;
        }
    }
}

bool
ExtrusionLoop::has_overhang_point(const Point &point) const
{
    for (ExtrusionPaths::const_iterator path = this->paths.begin(); path != this->paths.end(); ++path) {
        int pos = path->polyline.find_point(point);
        if (pos != -1) {
            // point belongs to this path
            // we consider it overhang only if it's not an endpoint
            return (path->is_bridge() && pos > 0 && pos != path->polyline.points.size()-1);
        }
    }
    return false;
}

bool
ExtrusionLoop::is_perimeter() const
{
    return this->paths.front().role == erPerimeter
        || this->paths.front().role == erExternalPerimeter
        || this->paths.front().role == erOverhangPerimeter;
}

bool
ExtrusionLoop::is_infill() const
{
    return this->paths.front().role == erBridgeInfill
        || this->paths.front().role == erInternalInfill
        || this->paths.front().role == erSolidInfill
        || this->paths.front().role == erTopSolidInfill;
}

bool
ExtrusionLoop::is_solid_infill() const
{
    return this->paths.front().role == erBridgeInfill
        || this->paths.front().role == erSolidInfill
        || this->paths.front().role == erTopSolidInfill;
}

Polygons
ExtrusionLoop::grow() const
{
    Polygons pp;
    for (ExtrusionPaths::const_iterator path = this->paths.begin(); path != this->paths.end(); ++path) {
        Polygons path_pp = path->grow();
        pp.insert(pp.end(), path_pp.begin(), path_pp.end());
    }
    return pp;
}

#ifdef SLIC3RXS
REGISTER_CLASS(ExtrusionLoop, "ExtrusionLoop");
#endif

}
