#include "Geometry.hpp"
#include <algorithm>

namespace Slic3r {

static bool
sort_points (Point a, Point b)
{
    return (a.x < b.x) || (a.x == b.x && a.y < b.y);
}

/* This implementation is based on Steffen Mueller's work for 
   the Perl module Math::ConvexHull::MonotoneChain (available 
   on CPAN under the GPL terms) */
void
convex_hull(Points points, Polygon &hull)
{
    assert(points.size() >= 2);
    // sort input points
    std::sort(points.begin(), points.end(), sort_points);
    
    typedef const Point* PointPtr;
    PointPtr* out_hull = (PointPtr*)malloc(points.size()*2*sizeof(PointPtr));
    
    /* lower hull */
    size_t k = 0;
    for (Points::const_iterator it = points.begin(); it != points.end(); ++it) {
        while (k >= 2 && it->ccw(out_hull[k-2], out_hull[k-1]) <= 0) --k;
        Point pz = *&*it;
        out_hull[k++] = &*it;
    }
    
    /* upper hull */
    size_t t = k+1;
    for (Points::const_iterator it = points.end() - 2; it != points.begin(); --it) {
        while (k >= t && it->ccw(out_hull[k-2], out_hull[k-1]) <= 0) --k;
        out_hull[k++] = &*it;
    }
    
    // we assume hull is empty
    hull.points.reserve(k);
    for (size_t i = 0; i < k; ++i) {
        hull.points.push_back(*(out_hull[i]));
    }
    
    // not sure why this happens randomly
    if (hull.points.front().coincides_with(hull.points.back()))
        hull.points.pop_back();
    
    free(out_hull);
}

}
