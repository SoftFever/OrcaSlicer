#ifndef slic3r_FillConcentricWGapFil_hpp_
#define slic3r_FillConcentricWGapFil_hpp_

#include "FillBase.hpp"

namespace Slic3r {

class FillConcentricWGapFill : public Fill
{
public:
    ~FillConcentricWGapFill() override = default;
    void fill_surface_extrusion(const Surface *surface, const FillParams &params, ExtrusionEntitiesPtr &out) override;

protected:
    Fill* clone() const override { return new FillConcentricWGapFill(*this); };
    bool no_sort() const override { return true; }
};

} // namespace Slic3r

#endif // slic3r_FillConcentricWGapFil_hpp_
