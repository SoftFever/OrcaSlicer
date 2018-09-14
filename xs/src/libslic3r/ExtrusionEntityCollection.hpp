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
    explicit ExtrusionEntityCollection(const ExtrusionPaths &paths);
    ExtrusionEntityCollection& operator=(const ExtrusionEntityCollection &other);
    ExtrusionEntityCollection& operator=(ExtrusionEntityCollection &&other) 
        { this->entities = std::move(other.entities); this->orig_indices = std::move(other.orig_indices); this->no_sort = other.no_sort; return *this; }
    ~ExtrusionEntityCollection() { clear(); }
    explicit operator ExtrusionPaths() const;
    
    bool is_collection() const { return true; };
    ExtrusionRole role() const override {
        ExtrusionRole out = erNone;
        for (const ExtrusionEntity *ee : entities) {
            ExtrusionRole er = ee->role();
            out = (out == erNone || out == er) ? er : erMixed;
        }
        return out;
    }
    bool can_reverse() const { return !this->no_sort; };
    bool empty() const { return this->entities.empty(); };
    void clear();
    void swap (ExtrusionEntityCollection &c);
    void append(const ExtrusionEntity &entity) { this->entities.push_back(entity.clone()); }
    void append(const ExtrusionEntitiesPtr &entities) { 
        this->entities.reserve(this->entities.size() + entities.size());
        for (ExtrusionEntitiesPtr::const_iterator ptr = entities.begin(); ptr != entities.end(); ++ptr)
            this->entities.push_back((*ptr)->clone());
    }
    void append(ExtrusionEntitiesPtr &&src) {
        if (entities.empty())
            entities = std::move(src);
        else {
            std::move(std::begin(src), std::end(src), std::back_inserter(entities));
            src.clear();
        }
    }
    void append(const ExtrusionPaths &paths) { 
        this->entities.reserve(this->entities.size() + paths.size());
        for (ExtrusionPaths::const_iterator path = paths.begin(); path != paths.end(); ++path)
            this->entities.push_back(path->clone());
    }
    void replace(size_t i, const ExtrusionEntity &entity);
    void remove(size_t i);
    ExtrusionEntityCollection chained_path(bool no_reverse = false, ExtrusionRole role = erMixed) const;
    void chained_path(ExtrusionEntityCollection* retval, bool no_reverse = false, ExtrusionRole role = erMixed, std::vector<size_t>* orig_indices = nullptr) const;
    ExtrusionEntityCollection chained_path_from(Point start_near, bool no_reverse = false, ExtrusionRole role = erMixed) const;
    void chained_path_from(Point start_near, ExtrusionEntityCollection* retval, bool no_reverse = false, ExtrusionRole role = erMixed, std::vector<size_t>* orig_indices = nullptr) const;
    void reverse();
    Point first_point() const { return this->entities.front()->first_point(); }
    Point last_point() const { return this->entities.back()->last_point(); }
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion width.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    void polygons_covered_by_width(Polygons &out, const float scaled_epsilon) const override;
    // Produce a list of 2D polygons covered by the extruded paths, offsetted by the extrusion spacing.
    // Increase the offset by scaled_epsilon to achieve an overlap, so a union will produce no gaps.
    // Useful to calculate area of an infill, which has been really filled in by a 100% rectilinear infill.
    void polygons_covered_by_spacing(Polygons &out, const float scaled_epsilon) const override;
    Polygons polygons_covered_by_width(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_width(out, scaled_epsilon); return out; }
    Polygons polygons_covered_by_spacing(const float scaled_epsilon = 0.f) const
        { Polygons out; this->polygons_covered_by_spacing(out, scaled_epsilon); return out; }
    size_t items_count() const;
    void flatten(ExtrusionEntityCollection* retval) const;
    ExtrusionEntityCollection flatten() const;
    double min_mm3_per_mm() const;
    double total_volume() const override { double volume=0.; for (const auto& ent : entities) volume+=ent->total_volume(); return volume; }

    // Following methods shall never be called on an ExtrusionEntityCollection.
    Polyline as_polyline() const {
        CONFESS("Calling as_polyline() on a ExtrusionEntityCollection");
        return Polyline();
    };

    void collect_polylines(Polylines &dst) const override {
        for (ExtrusionEntity* extrusion_entity : this->entities)
            extrusion_entity->collect_polylines(dst);
    }

    double length() const override {
        CONFESS("Calling length() on a ExtrusionEntityCollection");
        return 0.;        
    }
};

}

#endif
