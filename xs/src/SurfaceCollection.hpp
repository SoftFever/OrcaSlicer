#ifndef slic3r_SurfaceCollection_hpp_
#define slic3r_SurfaceCollection_hpp_

#include "Surface.hpp"
#include <vector>

namespace Slic3r {

struct t_surface_group_key {
    SurfaceType     surface_type;
    double          thickness;
    unsigned short  thickness_layers;
    double          bridge_angle;
    
    bool operator< (const t_surface_group_key &key) const {
        return (this->surface_type      < key.surface_type)
            || (this->thickness         < key.thickness)
            || (this->thickness_layers  < key.thickness_layers)
            || (this->bridge_angle      < key.bridge_angle);
    }
};

class SurfaceCollection
{
    public:
    Surfaces surfaces;
    void simplify(double tolerance);
    void group(std::vector<SurfacesPtr> &retval, bool merge_solid = false);
};

}

#endif
