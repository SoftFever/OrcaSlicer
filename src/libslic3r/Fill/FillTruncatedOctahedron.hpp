#ifndef slic3r_FillTruncatedOctahedron_hpp_
#define slic3r_FillTruncatedOctahedron_hpp_

#include "FillBase.hpp"

namespace Slic3r {

class FillTruncatedOctahedron : public Fill
{
public:
    Fill* clone() const override { return new FillTruncatedOctahedron(*this); }

protected:
    void _fill_surface_single(
        const FillParams                &params,
        unsigned int                     thickness_layers,
        const std::pair<float, Point>   &direction,
        ExPolygon                        expolygon,
        Polylines                       &polylines_out) override;
};

} // namespace Slic3r

#endif // slic3r_FillTruncatedOctahedron_hpp_
