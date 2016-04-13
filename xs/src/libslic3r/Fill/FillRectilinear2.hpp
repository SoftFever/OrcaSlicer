#ifndef slic3r_FillRectilinear2_hpp_
#define slic3r_FillRectilinear2_hpp_

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

class Surface;

class FillRectilinear2 : public FillWithDirection
{
public:
    virtual ~FillRectilinear2() {}
    virtual Polylines fill_surface(const Surface *surface, const FillParams &params);

protected:
	coord_t _min_spacing;
	coord_t _line_spacing;
	// distance threshold for allowing the horizontal infill lines to be connected into a continuous path
	coord_t _diagonal_distance;
};

}; // namespace Slic3r

#endif // slic3r_FillRectilinear2_hpp_
