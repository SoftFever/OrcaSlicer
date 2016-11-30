#ifndef slic3r_FillConcentric_hpp_
#define slic3r_FillConcentric_hpp_

#include "FillBase.hpp"

namespace Slic3r {

class FillConcentric : public Fill
{
public:
    virtual ~FillConcentric() {}

protected:
    virtual Fill* clone() const { return new FillConcentric(*this); };
	virtual void _fill_surface_single(
	    const FillParams                &params, 
	    unsigned int                     thickness_layers,
	    const std::pair<float, Point>   &direction, 
	    ExPolygon                       &expolygon, 
	    Polylines                       &polylines_out);

	virtual bool no_sort() const { return true; }
};

} // namespace Slic3r

#endif // slic3r_FillConcentric_hpp_
