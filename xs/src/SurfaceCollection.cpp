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
SurfaceCollection::group(std::vector<SurfacesPtr> *retval)
{
    for (Surfaces::iterator it = this->surfaces.begin(); it != this->surfaces.end(); ++it) {
        // find a group with the same properties
        SurfacesPtr* group = NULL;
        for (std::vector<SurfacesPtr>::iterator git = retval->begin(); git != retval->end(); ++git) {
            Surface* gkey = git->front();
            if (   gkey->surface_type      == it->surface_type
                && gkey->thickness         == it->thickness
                && gkey->thickness_layers  == it->thickness_layers
                && gkey->bridge_angle      == it->bridge_angle) {
                group = &*git;
                break;
            }
        }
        
        // if no group with these properties exists, add one
        if (group == NULL) {
            retval->resize(retval->size() + 1);
            group = &retval->back();
        }
        
        // append surface to group
        group->push_back(&*it);
    }
}

}
