#include "PolylineCollection.hpp"

namespace Slic3r {

struct Chaining
{
    Point first;
    Point last;
    size_t idx;
};

template<typename T>
inline int nearest_point_index(const std::vector<Chaining> &pairs, const Point &start_near, bool no_reverse)
{
    T dmin = std::numeric_limits<T>::max();
    int idx = 0;
    for (std::vector<Chaining>::const_iterator it = pairs.begin(); it != pairs.end(); ++it) {
        T d = sqr(T(start_near.x - it->first.x));
        if (d <= dmin) {
            d += sqr(T(start_near.y - it->first.y));
            if (d < dmin) {
                idx = (it - pairs.begin()) * 2;
                dmin = d;
                if (dmin < EPSILON)
                    break;
            }
        }
        if (! no_reverse) {
            d = sqr(T(start_near.x - it->last.x));
            if (d <= dmin) {
                d += sqr(T(start_near.y - it->last.y));
                if (d < dmin) {
                    idx = (it - pairs.begin()) * 2 + 1;
                    dmin = d;
                    if (dmin < EPSILON)
                        break;
                }
            }
        }
    }
    return idx;
}

Polylines PolylineCollection::_chained_path_from(
    const Polylines &src,
    Point start_near,
    bool  no_reverse, 
    bool  move_from_src)
{
    std::vector<Chaining> endpoints;
    endpoints.reserve(src.size());
    for (size_t i = 0; i < src.size(); ++ i) {
        Chaining c;
        c.first = src[i].first_point();
        if (! no_reverse)
            c.last = src[i].last_point();
        c.idx = i;
        endpoints.push_back(c);
    }
    Polylines retval;
    while (! endpoints.empty()) {
        // find nearest point
        int endpoint_index = nearest_point_index<double>(endpoints, start_near, no_reverse);
        assert(endpoint_index >= 0 && endpoint_index < endpoints.size() * 2);
        if (move_from_src) {
            retval.push_back(std::move(src[endpoints[endpoint_index/2].idx]));
        } else {
            retval.push_back(src[endpoints[endpoint_index/2].idx]);
        }
        if (endpoint_index & 1)
            retval.back().reverse();
        endpoints.erase(endpoints.begin() + endpoint_index/2);
        start_near = retval.back().last_point();
    }
    return retval;
}

Point PolylineCollection::leftmost_point(const Polylines &polylines)
{
    if (polylines.empty()) CONFESS("leftmost_point() called on empty PolylineCollection");
    Polylines::const_iterator it = polylines.begin();
    Point p = it->leftmost_point();
    for (++ it; it != polylines.end(); ++it) {
        Point p2 = it->leftmost_point();
        if (p2.x < p.x) 
            p = p2;
    }
    return p;
}

} // namespace Slic3r
