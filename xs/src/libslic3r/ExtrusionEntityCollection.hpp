#ifndef slic3r_ExtrusionEntityCollection_hpp_
#define slic3r_ExtrusionEntityCollection_hpp_

#include <myinit.h>
#include "ExtrusionEntity.hpp"

namespace Slic3r {

class ExtrusionEntityCollection : public ExtrusionEntity
{
    public:
    ExtrusionEntityCollection* clone() const;
    ExtrusionEntitiesPtr entities;
    std::vector<size_t> orig_indices;  // handy for XS
    bool no_sort;
    ExtrusionEntityCollection(): no_sort(false) {};
    ExtrusionEntityCollection(const ExtrusionEntityCollection &collection);
    ExtrusionEntityCollection& operator= (const ExtrusionEntityCollection &other);
    bool is_collection() const {
        return true;
    };
    bool can_reverse() const {
        return !this->no_sort;
    };
    void swap (ExtrusionEntityCollection &c);
    void chained_path(ExtrusionEntityCollection* retval, bool no_reverse = false, std::vector<size_t>* orig_indices = NULL) const;
    void chained_path_from(Point start_near, ExtrusionEntityCollection* retval, bool no_reverse = false, std::vector<size_t>* orig_indices = NULL) const;
    void reverse();
    Point first_point() const;
    Point last_point() const;
    Polygons grow() const;
    size_t items_count() const;
};

}

#endif
