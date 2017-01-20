#ifndef slic3r_ExtrusionEntity_hpp_
#define slic3r_ExtrusionEntity_hpp_

#include "libslic3r.h"
#include "Polygon.hpp"
#include "Polyline.hpp"

namespace Slic3r {

class ExPolygonCollection;
class ExtrusionEntityCollection;
class Extruder;

/* Each ExtrusionRole value identifies a distinct set of { extruder, speed } */
enum ExtrusionRole {
    erNone,
    erPerimeter,
    erExternalPerimeter,
    erOverhangPerimeter,
    erInternalInfill,
    erSolidInfill,
    erTopSolidInfill,
    erBridgeInfill,
    erGapFill,
    erSkirt,
    erSupportMaterial,
    erSupportMaterialInterface,
};

/* Special flags describing loop */
enum ExtrusionLoopRole {
    elrDefault,
    elrContourInternalPerimeter,
    elrSkirt,
};

class ExtrusionEntity
{
public:
    virtual bool is_collection() const { return false; }
    virtual bool is_loop() const { return false; }
    virtual bool can_reverse() const { return true; }
    virtual ExtrusionEntity* clone() const = 0;
    virtual ~ExtrusionEntity() {};
    virtual void reverse() = 0;
    virtual Point first_point() const = 0;
    virtual Point last_point() const = 0;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    virtual void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const = 0;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    virtual void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const = 0;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    virtual double min_mm3_per_mm() const = 0;
    virtual Polyline as_polyline() const = 0;
    virtual double length() const = 0;
};

typedef std::vector<ExtrusionEntity*> ExtrusionEntitiesPtr;

class ExtrusionPath : public ExtrusionEntity
{
public:
    Polyline polyline;
    ExtrusionRole role;
    // Volumetric velocity. mm^3 of plastic per mm of linear head motion. Used by the G-code generator.
    double mm3_per_mm;
    // Width of the extrusion, used for visualization purposes.
    float width;
    // Height of the extrusion, used for visualization purposed.
    float height;
    
    ExtrusionPath(ExtrusionRole role) : role(role), mm3_per_mm(-1), width(-1), height(-1) {};
    ExtrusionPath(ExtrusionRole role, double mm3_per_mm, float width, float height) : role(role), mm3_per_mm(mm3_per_mm), width(width), height(height) {};
    ExtrusionPath(const ExtrusionPath &rhs) : role(rhs.role), mm3_per_mm(rhs.mm3_per_mm), width(rhs.width), height(rhs.height), polyline(rhs.polyline) {}
    ExtrusionPath(ExtrusionPath &&rhs) : role(rhs.role), mm3_per_mm(rhs.mm3_per_mm), width(rhs.width), height(rhs.height), polyline(std::move(rhs.polyline)) {}
//    ExtrusionPath(ExtrusionRole role, const Flow &flow) : role(role), mm3_per_mm(flow.mm3_per_mm()), width(flow.width), height(flow.height) {};

    ExtrusionPath& operator=(const ExtrusionPath &rhs) { this->role = rhs.role; this->mm3_per_mm = rhs.mm3_per_mm; this->width = rhs.width; this->height = rhs.height; this->polyline = rhs.polyline; return *this; }
    ExtrusionPath& operator=(ExtrusionPath &&rhs) { this->role = rhs.role; this->mm3_per_mm = rhs.mm3_per_mm; this->width = rhs.width; this->height = rhs.height; this->polyline = std::move(rhs.polyline); return *this; }

    ExtrusionPath* clone() const { return new ExtrusionPath (*this); }
    void reverse() { this->polyline.reverse(); }
    Point first_point() const { return this->polyline.points.front(); }
    Point last_point() const { return this->polyline.points.back(); }
    // Produce a list of extrusion paths into retval by clipping this path by ExPolygonCollection.
    // Currently not used.
    void intersect_expolygons(const ExPolygonCollection &collection, ExtrusionEntityCollection* retval) const;
    // Produce a list of extrusion paths into retval by removing parts of this path by ExPolygonCollection.
    // Currently not used.
    void subtract_expolygons(const ExPolygonCollection &collection, ExtrusionEntityCollection* retval) const;
    void clip_end(double distance);
    void simplify(double tolerance);
    virtual double length() const;
    bool is_perimeter() const {
        return this->role == erPerimeter
            || this->role == erExternalPerimeter
            || this->role == erOverhangPerimeter;
    }
    bool is_infill() const {
        return this->role == erBridgeInfill
            || this->role == erInternalInfill
            || this->role == erSolidInfill
            || this->role == erTopSolidInfill;
    }
    bool is_solid_infill() const {
        return this->role == erBridgeInfill
            || this->role == erSolidInfill
            || this->role == erTopSolidInfill;
    }
    bool is_bridge() const {
        return this->role == erBridgeInfill
            || this->role == erOverhangPerimeter;
    }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    double min_mm3_per_mm() const { return this->mm3_per_mm; }
    Polyline as_polyline() const { return this->polyline; }

    private:
    void _inflate_collection(const Polylines &polylines, ExtrusionEntityCollection* collection) const;
};

typedef std::vector<ExtrusionPath> ExtrusionPaths;

// Single continuous extrusion path, possibly with varying extrusion thickness, extrusion height or bridging / non bridging.
class ExtrusionMultiPath : public ExtrusionEntity
{
public:
    ExtrusionPaths paths;
    
    ExtrusionMultiPath() {};
    ExtrusionMultiPath(const ExtrusionMultiPath &rhs) : paths(rhs.paths) {}
    ExtrusionMultiPath(ExtrusionMultiPath &&rhs) : paths(std::move(rhs.paths)) {}
    ExtrusionMultiPath(const ExtrusionPaths &paths) : paths(paths) {};
    ExtrusionMultiPath(const ExtrusionPath &path) { this->paths.push_back(path); }

    ExtrusionMultiPath& operator=(const ExtrusionMultiPath &rhs) { this->paths = rhs.paths; return *this; }
    ExtrusionMultiPath& operator=(ExtrusionMultiPath &&rhs) { this->paths = std::move(rhs.paths); return *this; }

    bool is_loop() const { return false; }
    bool can_reverse() const { return true; }
    ExtrusionMultiPath* clone() const { return new ExtrusionMultiPath(*this); }
    void reverse();
    Point first_point() const { return this->paths.front().polyline.points.front(); }
    Point last_point() const { return this->paths.back().polyline.points.back(); }
    virtual double length() const;
    bool is_perimeter() const {
        return this->paths.front().role == erPerimeter
            || this->paths.front().role == erExternalPerimeter
            || this->paths.front().role == erOverhangPerimeter;
    }
    bool is_infill() const {
        return this->paths.front().role == erBridgeInfill
            || this->paths.front().role == erInternalInfill
            || this->paths.front().role == erSolidInfill
            || this->paths.front().role == erTopSolidInfill;
    }
    bool is_solid_infill() const {
        return this->paths.front().role == erBridgeInfill
            || this->paths.front().role == erSolidInfill
            || this->paths.front().role == erTopSolidInfill;
    }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    double min_mm3_per_mm() const;
    Polyline as_polyline() const;
};

// Single continuous extrusion loop, possibly with varying extrusion thickness, extrusion height or bridging / non bridging.
class ExtrusionLoop : public ExtrusionEntity
{
    public:
    ExtrusionPaths paths;
    ExtrusionLoopRole role;
    
    ExtrusionLoop(ExtrusionLoopRole role = elrDefault) : role(role) {};
    ExtrusionLoop(const ExtrusionPaths &paths, ExtrusionLoopRole role = elrDefault)
        : paths(paths), role(role) {};
    ExtrusionLoop(const ExtrusionPath &path, ExtrusionLoopRole role = elrDefault)
        : role(role) {
        this->paths.push_back(path);
    };
    bool is_loop() const { return true; }
    bool can_reverse() const { return false; }
    ExtrusionLoop* clone() const { return new ExtrusionLoop (*this); }
    bool make_clockwise();
    bool make_counter_clockwise();
    void reverse();
    Point first_point() const { return this->paths.front().polyline.points.front(); }
    Point last_point() const { assert(first_point() == this->paths.back().polyline.points.back()); return first_point(); }
    Polygon polygon() const;
    virtual double length() const;
    bool split_at_vertex(const Point &point);
    void split_at(const Point &point);
    void clip_end(double distance, ExtrusionPaths* paths) const;
    // Test, whether the point is extruded by a bridging flow.
    // This used to be used to avoid placing seams on overhangs, but now the EdgeGrid is used instead.
    bool has_overhang_point(const Point &point) const;
    bool is_perimeter() const {
        return this->paths.front().role == erPerimeter
            || this->paths.front().role == erExternalPerimeter
            || this->paths.front().role == erOverhangPerimeter;
    }
    bool is_infill() const {
        return this->paths.front().role == erBridgeInfill
            || this->paths.front().role == erInternalInfill
            || this->paths.front().role == erSolidInfill
            || this->paths.front().role == erTopSolidInfill;
    }
    bool is_solid_infill() const {
        return this->paths.front().role == erBridgeInfill
            || this->paths.front().role == erSolidInfill
            || this->paths.front().role == erTopSolidInfill;
    }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    // Minimum volumetric velocity of this extrusion entity. Used by the constant nozzle pressure algorithm.
    double min_mm3_per_mm() const;
    Polyline as_polyline() const { return this->polygon().split_at_first_point(); }
};

inline void extrusion_paths_append(ExtrusionPaths &dst, Polylines &polylines, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polylines::const_iterator it_polyline = polylines.begin(); it_polyline != polylines.end(); ++ it_polyline) {
        dst.push_back(ExtrusionPath(role, mm3_per_mm, width, height));
        dst.back().polyline = *it_polyline;
    }
}

#if SLIC3R_CPPVER >= 11
inline void extrusion_paths_append(ExtrusionPaths &dst, Polylines &&polylines, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polylines::const_iterator it_polyline = polylines.begin(); it_polyline != polylines.end(); ++ it_polyline) {
        dst.push_back(ExtrusionPath(role, mm3_per_mm, width, height));
        dst.back().polyline = std::move(*it_polyline);
    }
}
#endif // SLIC3R_CPPVER >= 11

inline void extrusion_entities_append_paths(ExtrusionEntitiesPtr &dst, Polylines &polylines, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polylines::const_iterator it_polyline = polylines.begin(); it_polyline != polylines.end(); ++ it_polyline) {
        ExtrusionPath *extrusion_path = new ExtrusionPath(role, mm3_per_mm, width, height);
        dst.push_back(extrusion_path);
        extrusion_path->polyline = *it_polyline;
    }
}

#if SLIC3R_CPPVER >= 11
inline void extrusion_entities_append_paths(ExtrusionEntitiesPtr &dst, Polylines &&polylines, ExtrusionRole role, double mm3_per_mm, float width, float height)
{
    dst.reserve(dst.size() + polylines.size());
    for (Polylines::const_iterator it_polyline = polylines.begin(); it_polyline != polylines.end(); ++ it_polyline) {
        ExtrusionPath *extrusion_path = new ExtrusionPath(role, mm3_per_mm, width, height);
        dst.push_back(extrusion_path);
        extrusion_path->polyline = std::move(*it_polyline);
    }
}
#endif // SLIC3R_CPPVER >= 11

}

#endif
