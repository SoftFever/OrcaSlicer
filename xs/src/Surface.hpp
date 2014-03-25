#ifndef slic3r_Surface_hpp_
#define slic3r_Surface_hpp_

#include "ExPolygon.hpp"

namespace Slic3r {

enum SurfaceType { stTop, stBottom, stBottomBridge, stInternal, stInternalSolid, stInternalBridge, stInternalVoid };

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
    bool is_solid() const;
    bool is_external() const;
    bool is_bottom() const;
    bool is_bridge() const;
    
    #ifdef SLIC3RXS
    void from_SV_check(SV* surface_sv);
    SV* to_SV_ref();
    SV* to_SV_clone_ref() const;
    #endif
};

typedef std::vector<Surface> Surfaces;
typedef std::vector<Surface*> SurfacesPtr;

}

#endif
