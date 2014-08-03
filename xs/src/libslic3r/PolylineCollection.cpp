#include "PolylineCollection.hpp"

namespace Slic3r {

void
PolylineCollection::chained_path(PolylineCollection* retval, bool no_reverse) const
{
    if (this->polylines.empty()) return;
    this->chained_path_from(this->polylines.front().first_point(), retval, no_reverse);
}

void
PolylineCollection::chained_path_from(Point start_near, PolylineCollection* retval, bool no_reverse) const
{
    Polylines my_paths = this->polylines;
    
    Points endpoints;
    for (Polylines::const_iterator it = my_paths.begin(); it != my_paths.end(); ++it) {
        endpoints.push_back(it->first_point());
        if (no_reverse) {
            endpoints.push_back(it->first_point());
        } else {
            endpoints.push_back(it->last_point());
        }
    }
    
    while (!my_paths.empty()) {
        // find nearest point
        int start_index = start_near.nearest_point_index(endpoints);
        int path_index = start_index/2;
        if (start_index % 2 && !no_reverse) {
            my_paths.at(path_index).reverse();
        }
        retval->polylines.push_back(my_paths.at(path_index));
        my_paths.erase(my_paths.begin() + path_index);
        endpoints.erase(endpoints.begin() + 2*path_index, endpoints.begin() + 2*path_index + 2);
        start_near = retval->polylines.back().last_point();
    }
}

Point
PolylineCollection::leftmost_point() const
{
    if (this->polylines.empty()) CONFESS("leftmost_point() called on empty PolylineCollection");
    Point p = this->polylines.front().leftmost_point();
    for (Polylines::const_iterator it = this->polylines.begin() + 1; it != this->polylines.end(); ++it) {
        Point p2 = it->leftmost_point();
        if (p2.x < p.x) p = p2;
    }
    return p;
}

#ifdef SLIC3RXS
REGISTER_CLASS(PolylineCollection, "Polyline::Collection");
#endif

}
