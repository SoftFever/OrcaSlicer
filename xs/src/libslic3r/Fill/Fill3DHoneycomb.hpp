#ifndef slic3r_Fill3DHoneycomb_hpp_
#define slic3r_Fill3DHoneycomb_hpp_

#include <map>

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

class Fill3DHoneycomb : public FillWithDirection
{
public:
    virtual ~Fill3DHoneycomb() {}
    virtual Polylines fill_surface(const Surface *surface, const FillParams &params);
	
	// require bridge flow since most of this pattern hangs in air
    virtual bool use_bridge_flow() const { return true; }
};

} // namespace Slic3r

#endif // slic3r_Fill3DHoneycomb_hpp_
