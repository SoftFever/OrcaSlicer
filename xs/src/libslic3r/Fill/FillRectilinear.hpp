#ifndef slic3r_FillRectilinear_hpp_
#define slic3r_FillRectilinear_hpp_

#include "../libslic3r.h"

#include "FillBase.hpp"

namespace Slic3r {

class Surface;

class FillRectilinear : public FillWithDirection
{
public:
    virtual ~FillRectilinear() {}
    virtual Polylines fill_surface(const Surface *surface, const FillParams &params);

protected:
	coord_t _min_spacing;
	coord_t _line_spacing;
	// distance threshold for allowing the horizontal infill lines to be connected into a continuous path
	coord_t _diagonal_distance;
	// only for line infill
	coord_t _line_oscillation;

	// Enabled for the grid infill, disabled for the rectilinear and line infill.
	virtual bool _horizontal_lines() const { return false; }

	virtual Line _line(int i, coord_t x, coord_t y_min, coord_t y_max) const 
		{ return Line(Point(x, y_min), Point(x, y_max)); }
	
	virtual bool _can_connect(coord_t dist_X, coord_t dist_Y) {
	    return dist_X <= this->_diagonal_distance
	        && dist_Y <= this->_diagonal_distance;
    }
};

class FillLine : public FillRectilinear
{
public:
    virtual ~FillLine() {}

protected:
	virtual Line _line(int i, coord_t x, coord_t y_min, coord_t y_max) const {
		coord_t osc = (i & 1) ? this->_line_oscillation : 0;
		return Line(Point(x - osc, y_min), Point(x + osc, y_max));
	}

	virtual bool _can_connect(coord_t dist_X, coord_t dist_Y)
	{
	    coord_t TOLERANCE = 10 * SCALED_EPSILON;
    	return (dist_X >= (this->_line_spacing - this->_line_oscillation) - TOLERANCE)
        	&& (dist_X <= (this->_line_spacing + this->_line_oscillation) + TOLERANCE)
        	&& (dist_Y <= this->_diagonal_distance);
    }
};

class FillGrid : public FillRectilinear
{
public:
    virtual ~FillGrid() {}

protected:
	// The grid fill will keep the angle constant between the layers, see the implementation of Slic3r::Fill::Base.
    virtual float _layer_angle(size_t idx) const { return 0.f; }
	// Flag for Slic3r::Fill::Rectilinear to fill both directions.
	virtual bool _horizontal_lines() const { return true; }
};

}; // namespace Slic3r

#endif // slic3r_FillRectilinear_hpp_
