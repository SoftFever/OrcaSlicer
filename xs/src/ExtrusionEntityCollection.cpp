#include "ExtrusionEntityCollection.hpp"

namespace Slic3r {

ExtrusionEntityCollection*
ExtrusionEntityCollection::clone() const
{
    ExtrusionEntityCollection* collection = new ExtrusionEntityCollection (*this);
    for (ExtrusionEntitiesPtr::iterator it = collection->entities.begin(); it != collection->entities.end(); ++it) {
        *it = (*it)->clone();
    }
    return collection;
}

void
ExtrusionEntityCollection::reverse()
{
    for (ExtrusionEntitiesPtr::iterator it = this->entities.begin(); it != this->entities.end(); ++it) {
        (*it)->reverse();
    }
    std::reverse(this->entities.begin(), this->entities.end());
}

Point*
ExtrusionEntityCollection::first_point() const
{
    return this->entities.front()->first_point();
}

Point*
ExtrusionEntityCollection::last_point() const
{
    return this->entities.back()->last_point();
}

ExtrusionEntityCollection*
ExtrusionEntityCollection::chained_path(bool no_reverse) const
{
    if (this->entities.empty()) {
        return new ExtrusionEntityCollection ();
    }
    return this->chained_path_from(this->entities.front()->first_point(), no_reverse);
}

ExtrusionEntityCollection*
ExtrusionEntityCollection::chained_path_from(Point* start_near, bool no_reverse) const
{
    if (this->no_sort) return new ExtrusionEntityCollection(*this);
    ExtrusionEntityCollection* retval = new ExtrusionEntityCollection;
    
    ExtrusionEntitiesPtr my_paths;
    for (ExtrusionEntitiesPtr::const_iterator it = this->entities.begin(); it != this->entities.end(); ++it) {
        my_paths.push_back((*it)->clone());
    }
    
    Points endpoints;
    for (ExtrusionEntitiesPtr::iterator it = my_paths.begin(); it != my_paths.end(); ++it) {
        endpoints.push_back(*(*it)->first_point());
        if (no_reverse) {
            endpoints.push_back(*(*it)->first_point());
        } else {
            endpoints.push_back(*(*it)->last_point());
        }
    }
    
    while (!my_paths.empty()) {
        // find nearest point
        int start_index = start_near->nearest_point_index(endpoints);
        int path_index = start_index/2;
        if (start_index % 2 && !no_reverse) {
            my_paths.at(path_index)->reverse();
        }
        retval->entities.push_back(my_paths.at(path_index));
        my_paths.erase(my_paths.begin() + path_index);
        endpoints.erase(endpoints.begin() + 2*path_index, endpoints.begin() + 2*path_index + 2);
        start_near = retval->entities.back()->last_point();
    }
    
    return retval;
}

}
