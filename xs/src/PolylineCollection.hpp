#ifndef slic3r_PolylineCollection_hpp_
#define slic3r_PolylineCollection_hpp_

#include <myinit.h>
#include "Polyline.hpp"

namespace Slic3r {

class PolylineCollection
{
    public:
    Polylines polylines;
    PolylineCollection* chained_path(bool no_reverse) const;
    PolylineCollection* chained_path_from(const Point* start_near, bool no_reverse) const;
};

}

#endif
