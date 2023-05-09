#ifndef slic3r_FillArc_hpp_
#define slic3r_FillArc_hpp_

#include "FillBase.hpp"
#include "../libslic3r.h"

namespace Slic3r {

class FillArc : public Fill
{
public:
    ~FillArc() override = default;
	
protected:
     Fill* clone() const override { return new FillArc(*this); };
    //void init_spacing(coordf_t spacing, const FillParams &params) override;
	void _fill_surface_single(
        const FillParams                &params,
        unsigned int                     thickness_layers,
        const std::pair<float, Point>   &direction,
	const Polyline  		pedestal,
        ExPolygon                        expolygon,
        Polylines                       &polylines_out) const override;
	bool no_sort() const override { return true; }
	// The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill.
    float _layer_angle(size_t idx) const override { return 0.f; }
    coord_t _min_spacing;
	coord_t _line_spacing;
	// distance threshold for allowing the horizontal infill lines to be connected into a continuous path
	coord_t _diagonal_distance;
	// only for line infill
	coord_t _line_oscillation;
    bool _can_connect(coord_t dist_X, coord_t dist_Y) const
	{
	    const auto TOLERANCE = coord_t(10 * SCALED_EPSILON);
    	return (dist_X >= (this->_line_spacing - this->_line_oscillation) - TOLERANCE*40)
        	&& (dist_X <= (this->_line_spacing + this->_line_oscillation) + TOLERANCE*40)
        	&& (dist_Y <= this->_diagonal_distance);
    }
};


} // namespace Slic3r

#endif // slic3r_FillArc_hpp_
