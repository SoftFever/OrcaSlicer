#include "PolylineCollection.hpp"

namespace Slic3r {

PolylineCollection*
PolylineCollection::chained_path(bool no_reverse) const
{
    if (this->polylines.empty()) return new PolylineCollection ();
    return this->chained_path_from(this->polylines.front().first_point(), no_reverse);
}

PolylineCollection*
PolylineCollection::chained_path_from(const Point* start_near, bool no_reverse) const
{
    PolylineCollection* retval = new PolylineCollection;
    Polylines my_paths = this->polylines;
    
    Points endpoints;
    for (Polylines::const_iterator it = my_paths.begin(); it != my_paths.end(); ++it) {
        endpoints.push_back(*(*it).first_point());
        if (no_reverse) {
            endpoints.push_back(*(*it).first_point());
        } else {
            endpoints.push_back(*(*it).last_point());
        }
    }
    
    while (!my_paths.empty()) {
        // find nearest point
        int start_index = start_near->nearest_point_index(endpoints);
        int path_index = start_index/2;
        if (start_index % 2 && !no_reverse) {
            my_paths.at(path_index).reverse();
        }
        retval->polylines.push_back(my_paths.at(path_index));
        my_paths.erase(my_paths.begin() + path_index);
        endpoints.erase(endpoints.begin() + 2*path_index, endpoints.begin() + 2*path_index + 2);
        start_near = retval->polylines.back().last_point();
    }
    
    return retval;
}

}
