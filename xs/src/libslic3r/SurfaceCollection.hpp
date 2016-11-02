#ifndef slic3r_SurfaceCollection_hpp_
#define slic3r_SurfaceCollection_hpp_

#include "libslic3r.h"
#include "Surface.hpp"
#include <vector>

namespace Slic3r {

class SurfaceCollection
{
    public:
    Surfaces surfaces;
    
    SurfaceCollection() {};
    SurfaceCollection(const Surfaces &_surfaces)
        : surfaces(_surfaces) {};
    operator Polygons() const;
    operator ExPolygons() const;
    void simplify(double tolerance);
    void group(std::vector<SurfacesPtr> *retval);
    template <class T> bool any_internal_contains(const T &item) const;
    template <class T> bool any_bottom_contains(const T &item) const;
    SurfacesPtr filter_by_type(const SurfaceType type);
    SurfacesPtr filter_by_types(const SurfaceType *types, int ntypes);
    void keep_type(const SurfaceType type);
    void keep_types(const SurfaceType *types, int ntypes);
    void remove_type(const SurfaceType type);
    void remove_types(const SurfaceType *types, int ntypes);
    void filter_by_type(SurfaceType type, Polygons* polygons);
    void append(const SurfaceCollection &coll);
    void append(const SurfaceType surfaceType, const ExPolygons &expoly);

    // For debugging purposes:
    void export_to_svg(const char *path, bool show_labels);
};

}

#endif
