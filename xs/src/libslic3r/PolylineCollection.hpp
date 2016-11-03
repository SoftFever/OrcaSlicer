#ifndef slic3r_PolylineCollection_hpp_
#define slic3r_PolylineCollection_hpp_

#include "libslic3r.h"
#include "Polyline.hpp"

namespace Slic3r {

class PolylineCollection
{
    static Polylines _chained_path_from(
        const Polylines &src,
        Point start_near,
        bool no_reverse
#if SLIC3R_CPPVER >= 11
        , bool move_from_src
#endif
    );

public:
    Polylines polylines;
    void chained_path(PolylineCollection* retval, bool no_reverse = false) const
    	{ retval->polylines = chained_path(this->polylines, no_reverse); }
    void chained_path_from(Point start_near, PolylineCollection* retval, bool no_reverse = false) const
    	{ retval->polylines = chained_path_from(this->polylines, start_near, no_reverse); }
    Point leftmost_point() const
    	{ return leftmost_point(polylines); }
    void append(const Polylines &polylines);

	static Point     leftmost_point(const Polylines &polylines);
#if SLIC3R_CPPVER >= 11
	static Polylines chained_path(Polylines &&src, bool no_reverse = false);
	static Polylines chained_path_from(Polylines &&src, Point start_near, bool no_reverse = false);
#endif
    static Polylines chained_path(const Polylines &src, bool no_reverse = false);
    static Polylines chained_path_from(const Polylines &src, Point start_near, bool no_reverse = false);
};

}

#endif
