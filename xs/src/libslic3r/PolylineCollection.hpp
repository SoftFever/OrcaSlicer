#ifndef slic3r_PolylineCollection_hpp_
#define slic3r_PolylineCollection_hpp_

#include <myinit.h>
#include "Polyline.hpp"

namespace Slic3r {

class PolylineCollection
{
    public:
    Polylines polylines;
    void chained_path(PolylineCollection* retval, bool no_reverse = false) const;
    void chained_path_from(Point start_near, PolylineCollection* retval, bool no_reverse = false) const;
    Point leftmost_point() const;
};

}

#endif
