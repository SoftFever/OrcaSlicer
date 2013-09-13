#ifndef slic3r_Surface_hpp_
#define slic3r_Surface_hpp_

#include "ExPolygon.hpp"

namespace Slic3r {

enum SurfaceType { stTop, stBottom, stInternal, stInternalSolid, stInternalBridge, stInternalVoid };

class Surface
{
    public:
    ExPolygon       expolygon;
    SurfaceType     surface_type;
    double          thickness;          // in mm
    unsigned short  thickness_layers;   // in layers
    double          bridge_angle;
    unsigned short  extra_perimeters;
    double area() const;
    
    #ifdef SLIC3RXS
    SV* to_SV_ref();
    #endif
};

typedef std::vector<Surface> Surfaces;

}

#endif
