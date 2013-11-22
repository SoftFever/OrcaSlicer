#include "SurfaceCollection.hpp"
#include <map>

namespace Slic3r {

void
SurfaceCollection::simplify(double tolerance)
{
    Surfaces ss;
    for (Surfaces::const_iterator it_s = this->surfaces.begin(); it_s != this->surfaces.end(); ++it_s) {
        ExPolygons expp;
        it_s->expolygon.simplify(tolerance, expp);
        for (ExPolygons::const_iterator it_e = expp.begin(); it_e != expp.end(); ++it_e) {
            Surface s = *it_s;
            s.expolygon = *it_e;
            ss.push_back(s);
        }
    }
    this->surfaces = ss;
}

/* group surfaces by common properties */
void
SurfaceCollection::group(std::vector<Surfaces> &retval, bool merge_solid) const
{
    typedef std::map<t_surface_group_key,Surfaces> t_unique_map;
    t_unique_map unique_map;
    
    for (Surfaces::const_iterator it = this->surfaces.begin(); it != this->surfaces.end(); ++it) {
        // build the t_surface_group_key struct with this surface's properties
        t_surface_group_key key;
        if (merge_solid && it->is_solid()) {
            key.surface_type = stTop;
        } else {
            key.surface_type = it->surface_type;
        }
        key.thickness           = it->thickness;
        key.thickness_layers    = it->thickness_layers;
        key.bridge_angle        = it->bridge_angle;
        
        // check whether we already have a group for these properties
        if (unique_map.find(key) == unique_map.end()) {
            // no group exists, add it
            unique_map[key] = Surfaces();
        }
        unique_map[key].push_back(*it);
    }
    
    retval.reserve(unique_map.size());
    for (t_unique_map::const_iterator it = unique_map.begin(); it != unique_map.end(); ++it)
        retval.push_back(it->second);
}

}
