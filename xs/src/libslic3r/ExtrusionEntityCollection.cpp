#include "ExtrusionEntityCollection.hpp"
#include <algorithm>
#include <cmath>
#include <map>

namespace Slic3r {

ExtrusionEntityCollection::ExtrusionEntityCollection(const ExtrusionEntityCollection& collection)
    : orig_indices(collection.orig_indices), no_sort(collection.no_sort)
{
    this->append(collection.entities);
}

ExtrusionEntityCollection::ExtrusionEntityCollection(const ExtrusionPaths &paths)
    : no_sort(false)
{
    this->append(paths);
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

ExtrusionEntityCollection::~ExtrusionEntityCollection()
{
    for (ExtrusionEntitiesPtr::iterator it = this->entities.begin(); it != this->entities.end(); ++it)
        delete *it;
}

ExtrusionEntityCollection::operator ExtrusionPaths() const
{
    ExtrusionPaths paths;
    for (ExtrusionEntitiesPtr::const_iterator it = this->entities.begin(); it != this->entities.end(); ++it) {
        if (const ExtrusionPath* path = dynamic_cast<const ExtrusionPath*>(*it))
            paths.push_back(*path);
    }
    return paths;
}

ExtrusionEntityCollection*
ExtrusionEntityCollection::clone() const
{
    ExtrusionEntityCollection* coll = new ExtrusionEntityCollection(*this);
    for (size_t i = 0; i < coll->entities.size(); ++i) {
        coll->entities[i] = this->entities[i]->clone();
    }
    return coll;
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
ExtrusionEntityCollection::append(const ExtrusionEntity &entity)
{
    this->entities.push_back(entity.clone());
}

void
ExtrusionEntityCollection::append(const ExtrusionEntitiesPtr &entities)
{
    for (ExtrusionEntitiesPtr::const_iterator ptr = entities.begin(); ptr != entities.end(); ++ptr)
        this->append(**ptr);
}

void
ExtrusionEntityCollection::append(const ExtrusionPaths &paths)
{
    for (ExtrusionPaths::const_iterator path = paths.begin(); path != paths.end(); ++path)
        this->append(*path);
}

void
ExtrusionEntityCollection::replace(size_t i, const ExtrusionEntity &entity)
{
    delete this->entities[i];
    this->entities[i] = entity.clone();
}

void
ExtrusionEntityCollection::remove(size_t i)
{
    delete this->entities[i];
    this->entities.erase(this->entities.begin() + i);
}

ExtrusionEntityCollection
ExtrusionEntityCollection::chained_path(bool no_reverse, std::vector<size_t>* orig_indices) const
{
    ExtrusionEntityCollection coll;
    this->chained_path(&coll, no_reverse, orig_indices);
    return coll;
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

/* Returns a single vector of pointers to all non-collection items contained in this one */
void
ExtrusionEntityCollection::flatten(ExtrusionEntityCollection* retval) const
{
    for (ExtrusionEntitiesPtr::const_iterator it = this->entities.begin(); it != this->entities.end(); ++it) {
        if ((*it)->is_collection()) {
            ExtrusionEntityCollection* collection = dynamic_cast<ExtrusionEntityCollection*>(*it);
            retval->append(collection->flatten().entities);
        } else {
            retval->append(**it);
        }
    }
}

ExtrusionEntityCollection
ExtrusionEntityCollection::flatten() const
{
    ExtrusionEntityCollection coll;
    this->flatten(&coll);
    return coll;
}

double
ExtrusionEntityCollection::min_mm3_per_mm() const
{
    double min_mm3_per_mm = 0;
    for (ExtrusionEntitiesPtr::const_iterator it = this->entities.begin(); it != this->entities.end(); ++it) {
        double mm3_per_mm = (*it)->min_mm3_per_mm();
        if (min_mm3_per_mm == 0) {
            min_mm3_per_mm = mm3_per_mm;
        } else {
            min_mm3_per_mm = fmin(min_mm3_per_mm, mm3_per_mm);
        }
    }
    return min_mm3_per_mm;
}

}
