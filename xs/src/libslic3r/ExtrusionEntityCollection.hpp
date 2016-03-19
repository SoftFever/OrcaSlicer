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
    ExtrusionEntityCollection(const ExtrusionEntityCollection &collection);
    ExtrusionEntityCollection(const ExtrusionPaths &paths);
    ExtrusionEntityCollection& operator= (const ExtrusionEntityCollection &other);
    ~ExtrusionEntityCollection();
    operator ExtrusionPaths() const;
    
    bool is_collection() const {
        return true;
    };
    bool can_reverse() const {
        return !this->no_sort;
    };
    bool empty() const {
        return this->entities.empty();
    };
    void clear() {
        this->entities.clear();
    };
    void swap (ExtrusionEntityCollection &c);
    void append(const ExtrusionEntity &entity);
    void append(const ExtrusionEntitiesPtr &entities);
    void append(const ExtrusionPaths &paths);
    void replace(size_t i, const ExtrusionEntity &entity);
    void remove(size_t i);
    ExtrusionEntityCollection chained_path(bool no_reverse = false, std::vector<size_t>* orig_indices = NULL) const;
    void chained_path(ExtrusionEntityCollection* retval, bool no_reverse = false, std::vector<size_t>* orig_indices = NULL) const;
    void chained_path_from(Point start_near, ExtrusionEntityCollection* retval, bool no_reverse = false, std::vector<size_t>* orig_indices = NULL) const;
    void reverse();
    Point first_point() const;
    Point last_point() const;
    Polygons grow() const;
    size_t items_count() const;
    void flatten(ExtrusionEntityCollection* retval) const;
    ExtrusionEntityCollection flatten() const;
    double min_mm3_per_mm() const;
    Polyline as_polyline() const {
        CONFESS("Calling as_polyline() on a ExtrusionEntityCollection");
        return Polyline();
    };
};

}

#endif
