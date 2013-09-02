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
    bool no_sort;
    ExtrusionEntityCollection(): no_sort(false) {};
    ExtrusionEntityCollection* chained_path(bool no_reverse) const;
    ExtrusionEntityCollection* chained_path_from(Point* start_near, bool no_reverse) const;
    void reverse();
    Point* first_point() const;
    Point* last_point() const;
};

}

#endif
