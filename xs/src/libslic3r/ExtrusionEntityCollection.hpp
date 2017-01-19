#ifndef slic3r_ExtrusionEntityCollection_hpp_
#define slic3r_ExtrusionEntityCollection_hpp_

#include "libslic3r.h"
#include "ExtrusionEntity.hpp"

namespace Slic3r {

class ExtrusionEntityCollection : public ExtrusionEntity
{
public:
    ExtrusionEntityCollection* clone() const;
    ExtrusionEntitiesPtr entities;     // we own these entities
    std::vector<size_t> orig_indices;  // handy for XS
    bool no_sort;
    ExtrusionEntityCollection(): no_sort(false) {};
    ExtrusionEntityCollection(const ExtrusionEntityCollection &other) : orig_indices(other.orig_indices), no_sort(other.no_sort) { this->append(other.entities); }
    ExtrusionEntityCollection(ExtrusionEntityCollection &&other) : entities(std::move(other.entities)), orig_indices(std::move(other.orig_indices)), no_sort(other.no_sort) {}
    ExtrusionEntityCollection(const ExtrusionPaths &paths);
    ExtrusionEntityCollection& operator=(const ExtrusionEntityCollection &other);
    ExtrusionEntityCollection& operator=(ExtrusionEntityCollection &&other) 
        { this->entities = std::move(other.entities); this->orig_indices = std::move(other.orig_indices); this->no_sort = other.no_sort; return *this; }
    ~ExtrusionEntityCollection() { clear(); }
    operator ExtrusionPaths() const;
    
    bool is_collection() const { return true; };
    bool can_reverse() const { return !this->no_sort; };
    bool empty() const { return this->entities.empty(); };
    void clear();
    void swap (ExtrusionEntityCollection &c);
    void append(const ExtrusionEntity &entity) { this->entities.push_back(entity.clone()); }
    void append(const ExtrusionEntitiesPtr &entities);
    void append(const ExtrusionPaths &paths);
    void replace(size_t i, const ExtrusionEntity &entity);
    void remove(size_t i);
    ExtrusionEntityCollection chained_path(bool no_reverse = false, std::vector<size_t>* orig_indices = NULL) const;
    void chained_path(ExtrusionEntityCollection* retval, bool no_reverse = false, std::vector<size_t>* orig_indices = NULL) const;
    void chained_path_from(Point start_near, ExtrusionEntityCollection* retval, bool no_reverse = false, std::vector<size_t>* orig_indices = NULL) const;
    void reverse();
    Point first_point() const { return this->entities.front()->first_point(); }
    Point last_point() const { return this->entities.back()->last_point(); }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    virtual void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    virtual void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    size_t items_count() const;
    void flatten(ExtrusionEntityCollection* retval) const;
    ExtrusionEntityCollection flatten() const;
    double min_mm3_per_mm() const;
    Polyline as_polyline() const {
        CONFESS("Calling as_polyline() on a ExtrusionEntityCollection");
        return Polyline();
    };
    virtual double length() const {
        CONFESS("Calling length() on a ExtrusionEntityCollection");
        return 0.;        
    }
};

}

#endif
