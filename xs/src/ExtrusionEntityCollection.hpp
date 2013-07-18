#ifndef slic3r_ExtrusionEntityCollection_hpp_
#define slic3r_ExtrusionEntityCollection_hpp_

#include <myinit.h>
#include "ExtrusionEntity.hpp"

namespace Slic3r {

class ExtrusionEntityCollection : public ExtrusionEntity
{
    public:
    ExtrusionEntitiesPtr entities;
    bool no_sort;
};

}

#endif
