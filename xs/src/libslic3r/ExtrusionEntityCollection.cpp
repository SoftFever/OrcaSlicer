#include "ExtrusionEntityCollection.hpp"
#include <algorithm>
#include <map>

namespace Slic3r {

ExtrusionEntityCollection::ExtrusionEntityCollection(const ExtrusionEntityCollection& collection)
    : no_sort(collection.no_sort), orig_indices(collection.orig_indices)
{
    this->entities.reserve(collection.entities.size());
    for (ExtrusionEntitiesPtr::const_iterator it = collection.entities.begin(); it != collection.entities.end(); ++it)
        this->entities.push_back((*it)->clone());
}

ExtrusionEntityCollection& ExtrusionEntityCollection::operator= (const ExtrusionEntityCollection &other)
{
    ExtrusionEntityCollection tmp(other);
    this->swap(tmp);
    return *this;
}

void
ExtrusionEntityCollection::swap (ExtrusionEntityCollection &c)
{
    std::swap(this->entities, c.entities);
    std::swap(this->orig_indices, c.orig_indices);
    std::swap(this->no_sort, c.no_sort);
}

ExtrusionEntityCollection*
ExtrusionEntityCollection::clone() const
{
    return new ExtrusionEntityCollection(*this);
}

void
ExtrusionEntityCollection::reverse()
{
    for (ExtrusionEntitiesPtr::iterator it = this->entities.begin(); it != this->entities.end(); ++it) {
        // Don't reverse it if it's a loop, as it doesn't change anything in terms of elements ordering
        // and caller might rely on winding order
        if (!(*it)->is_loop()) (*it)->reverse();
    }
    std::reverse(this->entities.begin(), this->entities.end());
}

Point
ExtrusionEntityCollection::first_point() const
{
    return this->entities.front()->first_point();
}

Point
ExtrusionEntityCollection::last_point() const
{
    return this->entities.back()->last_point();
}

void
ExtrusionEntityCollection::chained_path(ExtrusionEntityCollection* retval, bool no_reverse, std::vector<size_t>* orig_indices) const
{
    if (this->entities.empty()) return;
    this->chained_path_from(this->entities.front()->first_point(), retval, no_reverse, orig_indices);
}

void
ExtrusionEntityCollection::chained_path_from(Point start_near, ExtrusionEntityCollection* retval, bool no_reverse, std::vector<size_t>* orig_indices) const
{
    if (this->no_sort) {
        *retval = *this;
        return;
    }
    retval->entities.reserve(this->entities.size());
    retval->orig_indices.reserve(this->entities.size());
    
    // if we're asked to return the original indices, build a map
    std::map<ExtrusionEntity*,size_t> indices_map;
    
    ExtrusionEntitiesPtr my_paths;
    for (ExtrusionEntitiesPtr::const_iterator it = this->entities.begin(); it != this->entities.end(); ++it) {
        ExtrusionEntity* entity = (*it)->clone();
        my_paths.push_back(entity);
        if (orig_indices != NULL) indices_map[entity] = it - this->entities.begin();
    }
    
    Points endpoints;
    for (ExtrusionEntitiesPtr::iterator it = my_paths.begin(); it != my_paths.end(); ++it) {
        endpoints.push_back((*it)->first_point());
        if (no_reverse || !(*it)->can_reverse()) {
            endpoints.push_back((*it)->first_point());
        } else {
            endpoints.push_back((*it)->last_point());
        }
    }
    
    while (!my_paths.empty()) {
        // find nearest point
        int start_index = start_near.nearest_point_index(endpoints);
        int path_index = start_index/2;
        ExtrusionEntity* entity = my_paths.at(path_index);
        // never reverse loops, since it's pointless for chained path and callers might depend on orientation
        if (start_index % 2 && !no_reverse && entity->can_reverse()) {
            entity->reverse();
        }
        retval->entities.push_back(my_paths.at(path_index));
        if (orig_indices != NULL) orig_indices->push_back(indices_map[entity]);
        my_paths.erase(my_paths.begin() + path_index);
        endpoints.erase(endpoints.begin() + 2*path_index, endpoints.begin() + 2*path_index + 2);
        start_near = retval->entities.back()->last_point();
    }
}

Polygons
ExtrusionEntityCollection::grow() const
{
    Polygons pp;
    for (ExtrusionEntitiesPtr::const_iterator it = this->entities.begin(); it != this->entities.end(); ++it) {
        Polygons entity_pp = (*it)->grow();
        pp.insert(pp.end(), entity_pp.begin(), entity_pp.end());
    }
    return pp;
}

/* Recursively count paths and loops contained in this collection */
size_t
ExtrusionEntityCollection::items_count() const
{
    size_t count = 0;
    for (ExtrusionEntitiesPtr::const_iterator it = this->entities.begin(); it != this->entities.end(); ++it) {
        if ((*it)->is_collection()) {
            ExtrusionEntityCollection* collection = dynamic_cast<ExtrusionEntityCollection*>(*it);
            count += collection->items_count();
        } else {
            ++count;
        }
    }
    return count;
}

#ifdef SLIC3RXS
// there is no ExtrusionLoop::Collection or ExtrusionEntity::Collection
REGISTER_CLASS(ExtrusionEntityCollection, "ExtrusionPath::Collection");
#endif

}
