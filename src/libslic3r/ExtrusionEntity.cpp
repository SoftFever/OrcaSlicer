#include "ExtrusionEntity.hpp"
#include "ExtrusionEntityCollection.hpp"
#include "ExPolygonCollection.hpp"
#include "ClipperUtils.hpp"
#include "Extruder.hpp"
#include "Flow.hpp"
#include <cmath>
#include <limits>
#include <sstream>

#define L(s) (s)

namespace Slic3r {
    
void ExtrusionPath::intersect_expolygons(const ExPolygonCollection &collection, ExtrusionEntityCollection* retval) const
{
    this->_inflate_collection(intersection_pl(Polylines{ polyline }, collection.expolygons), retval);
}

void ExtrusionPath::subtract_expolygons(const ExPolygonCollection &collection, ExtrusionEntityCollection* retval) const
{
    this->_inflate_collection(diff_pl(Polylines{ this->polyline }, collection.expolygons), retval);
}

void ExtrusionPath::clip_end(double distance)
{
    this->polyline.clip_end(distance);
}

void ExtrusionPath::simplify(double tolerance)
{
    this->polyline.simplify(tolerance);
}

double ExtrusionPath::length() const
{
    return this->polyline.length();
}

void ExtrusionPath::_inflate_collection(const Polylines &polylines, ExtrusionEntityCollection* collection) const
{
    for (const Polyline &polyline : polylines)
        collection->entities.emplace_back(new ExtrusionPath(polyline, *this));
}

void ExtrusionPath::polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const
{
    polygons_append(out, offset(this->polyline, float(scale_(this->width/2)) + scaled_epsilon));
}

void ExtrusionPath::polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const
{
    // Instantiating the Flow class to get the line spacing.
    // Don't know the nozzle diameter, setting to zero. It shall not matter it shall be optimized out by the compiler.
    bool bridge = is_bridge(this->role());
    assert(! bridge || this->width == this->height);
    auto flow = bridge ? Flow::bridging_flow(this->width, 0.f) : Flow(this->width, this->height, 0.f);
    polygons_append(out, offset(this->polyline, 0.5f * float(flow.scaled_spacing()) + scaled_epsilon));
}

void ExtrusionMultiPath::reverse()
{
    for (ExtrusionPath &path : this->paths)
        path.reverse();
    std::reverse(this->paths.begin(), this->paths.end());
}

double ExtrusionMultiPath::length() const
{
    double len = 0;
    for (const ExtrusionPath &path : this->paths)
        len += path.polyline.length();
    return len;
}

void ExtrusionMultiPath::polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const
{
    for (const ExtrusionPath &path : this->paths)
        path.polygons_covered_by_width(out, scaled_epsilon);
}

void ExtrusionMultiPath::polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const
{
    for (const ExtrusionPath &path : this->paths)
        path.polygons_covered_by_spacing(out, scaled_epsilon);
}

double ExtrusionMultiPath::min_mm3_per_mm() const
{
    double min_mm3_per_mm = std::numeric_limits<double>::max();
    for (const ExtrusionPath &path : this->paths)
        min_mm3_per_mm = std::min(min_mm3_per_mm, path.mm3_per_mm);
    return min_mm3_per_mm;
}

Polyline ExtrusionMultiPath::as_polyline() const
{
    Polyline out;
    if (! paths.empty()) {
        size_t len = 0;
        for (size_t i_path = 0; i_path < paths.size(); ++ i_path) {
            assert(! paths[i_path].polyline.points.empty());
            assert(i_path == 0 || paths[i_path - 1].polyline.points.back() == paths[i_path].polyline.points.front());
            len += paths[i_path].polyline.points.size();
        }
        // The connecting points between the segments are equal.
        len -= paths.size() - 1;
        assert(len > 0);
        out.points.reserve(len);
        out.points.push_back(paths.front().polyline.points.front());
        for (size_t i_path = 0; i_path < paths.size(); ++ i_path)
            out.points.insert(out.points.end(), paths[i_path].polyline.points.begin() + 1, paths[i_path].polyline.points.end());
    }
    return out;
}

bool ExtrusionLoop::make_clockwise()
{
    bool was_ccw = this->polygon().is_counter_clockwise();
    if (was_ccw) this->reverse();
    return was_ccw;
}

bool ExtrusionLoop::make_counter_clockwise()
{
    bool was_cw = this->polygon().is_clockwise();
    if (was_cw) this->reverse();
    return was_cw;
}

void ExtrusionLoop::reverse()
{
    for (ExtrusionPath &path : this->paths)
        path.reverse();
    std::reverse(this->paths.begin(), this->paths.end());
}

Polygon ExtrusionLoop::polygon() const
{
    Polygon polygon;
    for (const ExtrusionPath &path : this->paths) {
        // for each polyline, append all points except the last one (because it coincides with the first one of the next polyline)
        polygon.points.insert(polygon.points.end(), path.polyline.points.begin(), path.polyline.points.end()-1);
    }
    return polygon;
}

double ExtrusionLoop::length() const
{
    double len = 0;
    for (const ExtrusionPath &path : this->paths)
        len += path.polyline.length();
    return len;
}

bool ExtrusionLoop::split_at_vertex(const Point &point)
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
                new_paths.reserve(this->paths.size() + 1);
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
                std::swap(this->paths, new_paths);
            }
            return true;
        }
    }
    return false;
}

std::pair<size_t, Point> ExtrusionLoop::get_closest_path_and_point(const Point& point, bool prefer_non_overhang) const
{
    // Find the closest path and closest point belonging to that path. Avoid overhangs, if asked for.
    size_t path_idx = 0;
    Point  p;
    {
        double min = std::numeric_limits<double>::max();
        Point  p_non_overhang;
        size_t path_idx_non_overhang = 0;
        double min_non_overhang = std::numeric_limits<double>::max();
        for (const ExtrusionPath& path : this->paths) {
            Point p_tmp = point.projection_onto(path.polyline);
            double dist = (p_tmp - point).cast<double>().norm();
            if (dist < min) {
                p = p_tmp;
                min = dist;
                path_idx = &path - &this->paths.front();
            }
            if (prefer_non_overhang && !is_bridge(path.role()) && dist < min_non_overhang) {
                p_non_overhang = p_tmp;
                min_non_overhang = dist;
                path_idx_non_overhang = &path - &this->paths.front();
            }
        }
        if (prefer_non_overhang && min_non_overhang != std::numeric_limits<double>::max()) {
            // Only apply the non-overhang point if there is one.
            path_idx = path_idx_non_overhang;
            p = p_non_overhang;
        }
    }
    return std::make_pair(path_idx, p);
}

// Splitting an extrusion loop, possibly made of multiple segments, some of the segments may be bridging.
void ExtrusionLoop::split_at(const Point &point, bool prefer_non_overhang)
{
    if (this->paths.empty())
        return;
    
    auto [path_idx, p] = get_closest_path_and_point(point, prefer_non_overhang);
    
    // now split path_idx in two parts
    const ExtrusionPath &path = this->paths[path_idx];
    ExtrusionPath p1(path.role(), path.mm3_per_mm, path.width, path.height);
    ExtrusionPath p2(path.role(), path.mm3_per_mm, path.width, path.height);
    path.polyline.split_at(p, &p1.polyline, &p2.polyline);
    
    if (this->paths.size() == 1) {
        if (! p1.polyline.is_valid())
            std::swap(this->paths.front().polyline.points, p2.polyline.points);
        else if (! p2.polyline.is_valid())
            std::swap(this->paths.front().polyline.points, p1.polyline.points);
        else {
            p2.polyline.points.insert(p2.polyline.points.end(), p1.polyline.points.begin() + 1, p1.polyline.points.end());
            std::swap(this->paths.front().polyline.points, p2.polyline.points);
        }
    } else {
        // install the two paths
        this->paths.erase(this->paths.begin() + path_idx);
        if (p2.polyline.is_valid()) this->paths.insert(this->paths.begin() + path_idx, p2);
        if (p1.polyline.is_valid()) this->paths.insert(this->paths.begin() + path_idx, p1);
    }
    
    // split at the new vertex
    this->split_at_vertex(p);
}

void ExtrusionLoop::clip_end(double distance, ExtrusionPaths* paths) const
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

bool ExtrusionLoop::has_overhang_point(const Point &point) const
{
    for (const ExtrusionPath &path : this->paths) {
        int pos = path.polyline.find_point(point);
        if (pos != -1) {
            // point belongs to this path
            // we consider it overhang only if it's not an endpoint
            return (is_bridge(path.role()) && pos > 0 && pos != (int)(path.polyline.points.size())-1);
        }
    }
    return false;
}

void ExtrusionLoop::polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const
{
    for (const ExtrusionPath &path : this->paths)
        path.polygons_covered_by_width(out, scaled_epsilon);
}

void ExtrusionLoop::polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const
{
    for (const ExtrusionPath &path : this->paths)
        path.polygons_covered_by_spacing(out, scaled_epsilon);
}

double ExtrusionLoop::min_mm3_per_mm() const
{
    double min_mm3_per_mm = std::numeric_limits<double>::max();
    for (const ExtrusionPath &path : this->paths)
        min_mm3_per_mm = std::min(min_mm3_per_mm, path.mm3_per_mm);
    return min_mm3_per_mm;
}


std::string ExtrusionEntity::role_to_string(ExtrusionRole role)
{
    switch (role) {
        case erNone                         : return L("Unknown");
        case erPerimeter                    : return L("Perimeter");
        case erExternalPerimeter            : return L("External perimeter");
        case erOverhangPerimeter            : return L("Overhang perimeter");
        case erInternalInfill               : return L("Internal infill");
        case erSolidInfill                  : return L("Solid infill");
        case erTopSolidInfill               : return L("Top solid infill");
        case erIroning                      : return L("Ironing");
        case erBridgeInfill                 : return L("Bridge infill");
        case erGapFill                      : return L("Gap fill");
        case erSkirt                        : return L("Skirt/Brim");
        case erSupportMaterial              : return L("Support material");
        case erSupportMaterialInterface     : return L("Support material interface");
        case erWipeTower                    : return L("Wipe tower");
        case erCustom                       : return L("Custom");
        case erMixed                        : return L("Mixed");
        default                             : assert(false);
    }
    return "";
}

ExtrusionRole ExtrusionEntity::string_to_role(const std::string_view role)
{
    if (role == L("Perimeter"))
        return erPerimeter;
    else if (role == L("External perimeter"))
        return erExternalPerimeter;
    else if (role == L("Overhang perimeter"))
        return erOverhangPerimeter;
    else if (role == L("Internal infill"))
        return erInternalInfill;
    else if (role == L("Solid infill"))
        return erSolidInfill;
    else if (role == L("Top solid infill"))
        return erTopSolidInfill;
    else if (role == L("Ironing"))
        return erIroning;
    else if (role == L("Bridge infill"))
        return erBridgeInfill;
    else if (role == L("Gap fill"))
        return erGapFill;
    else if (role == L("Skirt") || role == L("Skirt/Brim")) // "Skirt" is for backward compatibility with 2.3.1 and earlier
        return erSkirt;
    else if (role == L("Support material"))
        return erSupportMaterial;
    else if (role == L("Support material interface"))
        return erSupportMaterialInterface;
    else if (role == L("Wipe tower"))
        return erWipeTower;
    else if (role == L("Custom"))
        return erCustom;
    else if (role == L("Mixed"))
        return erMixed;
    else
        return erNone;
}

}
