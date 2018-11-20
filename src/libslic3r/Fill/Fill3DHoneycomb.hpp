#ifndef slic3r_Fill3DHoneycomb_hpp_
#define slic3r_Fill3DHoneycomb_hpp_

#include <map>

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

class Fill3DHoneycomb : public Fill
{
public:
    virtual Fill* clone() const { return new Fill3DHoneycomb(*this); };
    virtual ~Fill3DHoneycomb() {}

	// require bridge flow since most of this pattern hangs in air
    virtual bool use_bridge_flow() const { return true; }

protected:
	virtual void _fill_surface_single(
	    const FillParams                &params, 
	    unsigned int                     thickness_layers,
	    const std::pair<float, Point>   &direction, 
	    ExPolygon                       &expolygon, 
	    Polylines                       &polylines_out);
};

} // namespace Slic3r

#endif // slic3r_Fill3DHoneycomb_hpp_
