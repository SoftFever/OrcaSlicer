#ifndef slic3r_FillConcentric_hpp_
#define slic3r_FillConcentric_hpp_

#include "FillBase.hpp"

namespace Slic3r {

class FillConcentric : public Fill
{
public:
    virtual ~FillConcentric() {}
    virtual Polylines fill_surface(const Surface *surface, const FillParams &params);

protected:
	virtual bool no_sort() const { return true; }
};

} // namespace Slic3r

#endif // slic3r_FillConcentric_hpp_
