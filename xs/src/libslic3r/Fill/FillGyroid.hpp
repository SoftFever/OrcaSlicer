#ifndef slic3r_FillGyroid_hpp_
#define slic3r_FillGyroid_hpp_

#include <map>

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

class FillGyroid : public Fill
{
public:

    FillGyroid(){ scaling = 1.75; }
    virtual Fill* clone() const { return new FillGyroid(*this); };
    virtual ~FillGyroid() {}

    // require bridge flow since most of this pattern hangs in air
    virtual bool use_bridge_flow() const { return true; }

protected:
    
    // mult of density, to have a good %of weight for each density parameter
    float scaling;


    virtual void _fill_surface_single(
        const FillParams                &params, 
        unsigned int                     thickness_layers,
        const std::pair<float, Point>   &direction, 
        ExPolygon                       &expolygon, 
        Polylines                       &polylines_out);
    
    // create the gyroid grid to clip.
    Polylines makeGrid(coord_t gridZ, double density, double layer_width, size_t gridWidth, size_t gridHeight, size_t curveType);
    //add line poly in reverse if needed into array
    inline void correctOrderAndAdd(const int num, Polyline poly, Polylines &array);
    //create a curved horinzontal line  (for each x, compute y)
    Polyline makeLineHori(double xPos, double yPos, double width, double height, 
        double currentYBegin, double segmentSize, coord_t scaleFactor, 
        double zCs, double zSn, 
        bool flip, double decal=0);
    //create a curved vertival line (for each y, compute x)
    Polyline makeLineVert(double xPos, double yPos, double width, double height, 
        double currentXBegin, double segmentSize, coord_t scaleFactor, 
        double zCs, double zSn, 
        bool flip, double decal=0);

};

} // namespace Slic3r

#endif // slic3r_FillGyroid_hpp_
